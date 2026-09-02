// SPDX-License-Identifier: Apache-2.0
#include "io/CgltfVrmaDocumentReader.h"

#include <vrmContainer/GlbContainer.h>

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/transform.h"
#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"

#include "cgltf.h"

#include <algorithm>
#include <bitset>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

const JsValue* Find(const JsObject& object, const char* key)
{
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

const JsObject* AsObject(const JsValue* value)
{
    return value && value->IsObject() ? &value->GetJsObject() : nullptr;
}

int AsInt(const JsValue* value, int fallback = -1)
{
    return value && value->IsInt() ? value->GetInt() : fallback;
}

GfMatrix4d NodeLocal(const cgltf_node& node)
{
    if (node.has_matrix) {
        const float* m = node.matrix;
        // glTF stores column vectors in a column-major array; GfMatrix4d is
        // row-vector/row-major, so reading in array order performs the transpose.
        return GfMatrix4d(
            m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
            m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
    }
    float t[3] = {0.0f, 0.0f, 0.0f};
    float r[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float s[3] = {1.0f, 1.0f, 1.0f};
    if (node.has_translation) std::memcpy(t, node.translation, sizeof(t));
    if (node.has_rotation) std::memcpy(r, node.rotation, sizeof(r));
    if (node.has_scale) std::memcpy(s, node.scale, sizeof(s));

    GfMatrix4d scale(1.0), rotate(1.0), translate(1.0);
    scale.SetScale(GfVec3d(s[0], s[1], s[2]));
    rotate.SetRotate(GfQuatd(r[3], r[0], r[1], r[2]));
    translate.SetTranslate(GfVec3d(t[0], t[1], t[2]));
    return scale * rotate * translate;
}

// Whether the file placed `node` at all -- its own stated transform, or any
// ancestor's.
//
// A node that states nothing under a parent that states a translation is placed
// exactly as deliberately as one that states its own: the position is in the
// parent. What separates "the file gave a position" from "the file gave none"
// is therefore whether *anything in the chain* wrote one, never whether the
// composed number happens to be the origin -- the same rule an un-animated
// expression node's weight is under. Only translation and matrix are read,
// because a rotation or a scale cannot move a point that sits at its parent's
// own origin.
bool StatesPlacement(const cgltf_node& node)
{
    for (const cgltf_node* step = &node; step; step = step->parent) {
        if (step->has_translation || step->has_matrix) {
            return true;
        }
    }
    return false;
}

// The composed local transforms of everything above `node`, so a position
// stated in its own space can be placed where the file put it. Row-vector
// convention, matching NodeLocal: a point is `local * parent * grandparent`.
GfMatrix4d AncestorTransform(const cgltf_node& node)
{
    GfMatrix4d composed(1.0);
    for (const cgltf_node* ancestor = node.parent; ancestor;
         ancestor = ancestor->parent) {
        composed = composed * NodeLocal(*ancestor);
    }
    return composed;
}

void ReadKey(const cgltf_animation_sampler* sampler, std::size_t key,
             int components, float* out)
{
    const std::size_t sourceIndex =
        sampler->interpolation == cgltf_interpolation_type_cubic_spline
        ? key * 3 + 1 : key;
    cgltf_accessor_read_float(sampler->output, sourceIndex, out, components);
}

void Sample(const cgltf_animation_sampler* sampler,
            const std::vector<float>& keys, float time, int components,
            float* out)
{
    if (keys.empty()) return;
    if (time <= keys.front() || keys.size() == 1) {
        ReadKey(sampler, 0, components, out);
        return;
    }
    if (time >= keys.back()) {
        ReadKey(sampler, keys.size() - 1, components, out);
        return;
    }
    const std::size_t high = static_cast<std::size_t>(
        std::upper_bound(keys.begin(), keys.end(), time) - keys.begin());
    const std::size_t low = high - 1;
    if (sampler->interpolation == cgltf_interpolation_type_step) {
        ReadKey(sampler, low, components, out);
        return;
    }
    const float alpha = (time - keys[low]) / (keys[high] - keys[low]);
    float a[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float b[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    ReadKey(sampler, low, components, a);
    ReadKey(sampler, high, components, b);
    if (components == 4) {
        const GfQuatd rotation = GfSlerp(alpha,
            GfQuatd(a[3], a[0], a[1], a[2]),
            GfQuatd(b[3], b[0], b[1], b[2]));
        out[0] = static_cast<float>(rotation.GetImaginary()[0]);
        out[1] = static_cast<float>(rotation.GetImaginary()[1]);
        out[2] = static_cast<float>(rotation.GetImaginary()[2]);
        out[3] = static_cast<float>(rotation.GetReal());
        return;
    }
    for (int component = 0; component != components; ++component) {
        out[component] = a[component] + (b[component] - a[component]) * alpha;
    }
}

} // namespace

bool
CgltfVrmaDocumentReader::Read(const std::string& resolvedPath,
                              const std::vector<std::byte>& bytes,
                              VrmaCanonicalDocument* outDocument,
                              std::string* outError)
{
    auto fail = [&](const std::string& message) {
        if (outError) *outError = message;
        return false;
    };
    if (!outDocument || bytes.empty()) return fail("empty VRMA input");

    vrmContainer::GlbView glb;
    vrmContainer::Error containerError;
    if (!vrmContainer::ParseGlb({bytes.data(), bytes.size()}, &glb, &containerError)) {
        return fail(std::string("VRMA container: ") +
                    vrmContainer::ErrorMessage(containerError.code));
    }

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &data) !=
            cgltf_result_success || !data) {
        return fail("cgltf_parse failed (not a valid GLB)");
    }
    const auto release = [&]() { cgltf_free(data); };
    if (cgltf_load_buffers(&options, data, resolvedPath.c_str()) !=
        cgltf_result_success) {
        release();
        return fail("cgltf_load_buffers failed");
    }

    std::string extensionJson;
    for (cgltf_size index = 0; index != data->data_extensions_count; ++index) {
        const cgltf_extension& extension = data->data_extensions[index];
        if (extension.name && extension.data &&
            std::strcmp(extension.name, "VRMC_vrm_animation") == 0) {
            extensionJson = extension.data;
            break;
        }
    }
    if (extensionJson.empty()) {
        release();
        return fail("[VRMA001] VRMC_vrm_animation extension is required");
    }

    JsParseError parseError;
    const JsValue rootValue = JsParseString(extensionJson, &parseError);
    const JsObject* root = AsObject(&rootValue);
    if (!root) {
        release();
        return fail("[VRMA002] could not parse VRMC_vrm_animation extension JSON");
    }
    const JsValue* specVersion = Find(*root, "specVersion");
    if (!specVersion || !specVersion->IsString() ||
        specVersion->GetString() != "1.0") {
        release();
        return fail("[VRMA003] only VRMC_vrm_animation specVersion 1.0 is supported");
    }
    const JsObject* humanoid = AsObject(Find(*root, "humanoid"));
    const JsObject* humanBones = humanoid ? AsObject(Find(*humanoid, "humanBones")) : nullptr;
    if (!humanBones) {
        release();
        return fail("[VRMA004] humanoid.humanBones is required for VRMA import");
    }

    std::map<motion::HumanBone, int> boneNodes;
    for (const auto& entry : *humanBones) {
        const auto bone = motion::FindHumanBone(entry.first);
        const JsObject* nodeObject = AsObject(&entry.second);
        const int node = nodeObject ? AsInt(Find(*nodeObject, "node")) : -1;
        if (!bone || node < 0 || node >= static_cast<int>(data->nodes_count)) {
            outDocument->warnings.push_back(
                "[VRMA101] ignored invalid humanoid bone mapping '" + entry.first + "'");
            continue;
        }
        boneNodes[*bone] = node;
    }
    if (boneNodes.empty()) {
        release();
        return fail("[VRMA005] no valid humanoid bone mappings");
    }

    // Expressions. VRMA names an expression and points it at a node whose
    // translation carries the weight; the vocabulary is open, so nothing here
    // validates the *name* -- a preset this build has never heard of is still a
    // name a producer used, and motionCore carries it verbatim
    // (MOTION_CONTRACT.md, "Expression semantics"). Only the node is checked.
    std::map<int, std::size_t> expressionByNodeIndex;
    std::set<std::string> expressionNames;
    if (const JsObject* expressions = AsObject(Find(*root, "expressions"))) {
        for (const char* group : {"preset", "custom"}) {
            const JsObject* declared = AsObject(Find(*expressions, group));
            if (!declared) continue;
            for (const auto& entry : *declared) {
                const JsObject* nodeObject = AsObject(&entry.second);
                const int node = nodeObject ? AsInt(Find(*nodeObject, "node")) : -1;
                if (node < 0 || node >= static_cast<int>(data->nodes_count)) {
                    outDocument->warnings.push_back(
                        "[VRMA105] ignored expression '" + entry.first +
                        "' with no usable node");
                    continue;
                }
                bool isBone = false;
                for (const auto& mapping : boneNodes) {
                    if (mapping.second == node) { isBone = true; break; }
                }
                if (isBone || expressionByNodeIndex.count(node) != 0) {
                    outDocument->warnings.push_back(
                        "[VRMA106] ignored expression '" + entry.first +
                        "' whose node is already driven");
                    continue;
                }
                if (!expressionNames.insert(entry.first).second) {
                    outDocument->warnings.push_back(
                        "[VRMA107] ignored duplicate expression '" + entry.first + "'");
                    continue;
                }
                expressionByNodeIndex.emplace(node, outDocument->expressions.size());
                VrmaExpression expression;
                expression.name = entry.first;
                expression.isPreset = std::strcmp(group, "preset") == 0;
                outDocument->expressions.push_back(std::move(expression));
            }
        }
    }

    // Look-at. VRMA points it at one node whose position is what the character
    // is watching, so unlike an expression there is no name and no vocabulary --
    // only the node, and the offset the source rig measured from its head bone.
    // Nothing here turns either into a gaze direction: that needs a head, which
    // is a property of the rig this clip binds to no avatar of (motion policy
    // 4.3), and it is `LookAtEvaluate`'s job.
    int lookAtNodeIndex = -1;
    if (const JsObject* lookAt = AsObject(Find(*root, "lookAt"))) {
        // The block itself is the declaration, and it survives a node this
        // reader cannot use. A file that raised the subject and measured its
        // rig's offset said something; dropping the whole block on a bad node
        // would leave a stage indistinguishable from a clip that never
        // mentioned look-at at all, which is a different statement. What an
        // unusable node costs is the target, and that is exactly the state a
        // clip declaring a node it never places is already in.
        outDocument->lookAt.present = true;

        const JsValue* offset = Find(*lookAt, "offsetFromHeadBone");
        float components[3] = {0.0f, 0.0f, 0.0f};
        bool readOffset = false;
        if (offset && offset->IsArray() && offset->GetJsArray().size() == 3) {
            readOffset = true;
            const JsArray& array = offset->GetJsArray();
            for (int axis = 0; axis != 3; ++axis) {
                if (array[axis].IsReal()) {
                    components[axis] = static_cast<float>(array[axis].GetReal());
                } else if (array[axis].IsInt()) {
                    components[axis] = static_cast<float>(array[axis].GetInt());
                } else {
                    readOffset = false;
                }
            }
        }
        if (readOffset) {
            outDocument->lookAt.offsetFromHeadBone =
                GfVec3f(components[0], components[1], components[2]);
        } else {
            // Left at zero, which is a gaze starting at the head bone itself.
            // That is a claim about the source rig rather than a neutral
            // default, so it is warned about instead of assumed.
            outDocument->warnings.push_back(
                "[VRMA112] look-at states no usable offsetFromHeadBone; "
                "the offset is taken as zero");
        }

        const int node = AsInt(Find(*lookAt, "node"));
        bool isDriven = expressionByNodeIndex.count(node) != 0;
        for (const auto& mapping : boneNodes) {
            if (mapping.second == node) { isDriven = true; break; }
        }
        if (node < 0 || node >= static_cast<int>(data->nodes_count)) {
            outDocument->warnings.push_back(
                "[VRMA110] look-at names no usable node; no target is read");
        } else if (isDriven) {
            // One node cannot be both a bone's transform and a target position
            // in the space that bone moves in; taking it as both would read the
            // same channel as two different things.
            outDocument->warnings.push_back(
                "[VRMA111] look-at names a node that is already driven; no "
                "target is read");
        } else {
            lookAtNodeIndex = node;
        }
    }

    // The semantic hierarchy comes from motionCore, not from a private table
    // here: the live-capture path authors the same skeleton, and two humanoid
    // taxonomies that can disagree would produce two skeletons that look alike
    // and do not compose.
    std::bitset<motion::HumanBoneCount> presentBones;
    for (const auto& mapping : boneNodes) {
        presentBones.set(static_cast<std::size_t>(mapping.first));
    }

    std::map<motion::HumanBone, std::size_t> jointByBone;
    for (std::size_t value = 0; value != motion::HumanBoneCount; ++value) {
        const auto bone = static_cast<motion::HumanBone>(value);
        const auto nodeIt = boneNodes.find(bone);
        if (nodeIt == boneNodes.end()) continue;

        VrmaJoint joint;
        joint.bone = bone;
        joint.restTransform = NodeLocal(data->nodes[nodeIt->second]);
        const GfTransform transform(joint.restTransform);
        const GfVec3d translation = transform.GetTranslation();
        joint.restTranslation = GfVec3f(translation[0], translation[1], translation[2]);
        const GfQuaternion rotation = transform.GetRotation().GetQuaternion();
        joint.restRotation = GfQuatf(static_cast<float>(rotation.GetReal()),
            GfVec3f(static_cast<float>(rotation.GetImaginary()[0]),
                    static_cast<float>(rotation.GetImaginary()[1]),
                    static_cast<float>(rotation.GetImaginary()[2])));

        joint.path = motion::HumanBoneJointPath(bone, presentBones);
        jointByBone[bone] = outDocument->joints.size();
        outDocument->joints.push_back(std::move(joint));
    }

    if (data->animations_count == 0) {
        release();
        return fail("[VRMA006] VRMA contains no glTF animation");
    }

    const cgltf_animation& animation = data->animations[0];
    outDocument->clipName = animation.name ? animation.name : "Animation";
    std::unordered_map<const cgltf_node*, std::size_t> jointByNode;
    for (const auto& mapping : boneNodes) {
        const auto jointIt = jointByBone.find(mapping.first);
        if (jointIt != jointByBone.end()) {
            jointByNode[&data->nodes[mapping.second]] = jointIt->second;
        }
    }
    std::unordered_map<const cgltf_node*, std::size_t> expressionByNode;
    for (const auto& mapping : expressionByNodeIndex) {
        expressionByNode[&data->nodes[mapping.first]] = mapping.second;
    }

    const cgltf_node* lookAtNode =
        lookAtNodeIndex >= 0 ? &data->nodes[lookAtNodeIndex] : nullptr;
    const cgltf_animation_sampler* lookAtSampler = nullptr;

    std::map<std::size_t, const cgltf_animation_sampler*> rotations;
    std::map<std::size_t, const cgltf_animation_sampler*> translations;
    std::map<std::size_t, const cgltf_animation_sampler*> weights;
    std::unordered_map<const cgltf_animation_sampler*, std::vector<float>> samplerKeys;
    std::set<float> timeSet;
    bool warnedCubic = false;
    // Every accepted channel contributes its key times to one union and every
    // sample is evaluated there. An expression that keys on its own beats is
    // therefore not resampled onto the body's -- it adds instants the body is
    // evaluated at too, which is the rule the body channels already followed and
    // the reason expressions can ride on the pose (MOTION_CONTRACT.md).
    const auto keepKeys = [&](const cgltf_animation_sampler* sampler) {
        std::vector<float>& keys = samplerKeys[sampler];
        if (keys.empty()) {
            keys.resize(sampler->input->count);
            for (cgltf_size key = 0; key != sampler->input->count; ++key) {
                cgltf_accessor_read_float(sampler->input, key, &keys[key], 1);
            }
        }
        timeSet.insert(keys.begin(), keys.end());
    };
    for (cgltf_size channelIndex = 0; channelIndex != animation.channels_count; ++channelIndex) {
        const cgltf_animation_channel& channel = animation.channels[channelIndex];
        if (!channel.target_node || !channel.sampler || !channel.sampler->input ||
            !channel.sampler->output) continue;
        const auto jointIt = jointByNode.find(channel.target_node);
        const auto expressionIt = expressionByNode.find(channel.target_node);
        const bool isLookAt = lookAtNode && channel.target_node == lookAtNode;
        if (jointIt == jointByNode.end() && expressionIt == expressionByNode.end()
            && !isLookAt) {
            continue;
        }
        if (channel.sampler->interpolation == cgltf_interpolation_type_cubic_spline &&
            !warnedCubic) {
            outDocument->warnings.push_back(
                "[VRMA102] CUBICSPLINE interpolation is approximated as linear");
            warnedCubic = true;
        }

        if (isLookAt) {
            // The target is a position, so only a translation channel says
            // anything about it. A rotation on this node turns the target
            // around a point rather than moving it, which is not a gaze.
            if (channel.target_path != cgltf_animation_path_type_translation) {
                outDocument->warnings.push_back(
                    "[VRMA113] ignored non-translation channel on the look-at "
                    "node");
                continue;
            }
            lookAtSampler = channel.sampler;
            outDocument->lookAt.isAnimated = true;
            keepKeys(channel.sampler);
            continue;
        }

        if (expressionIt != expressionByNode.end()) {
            // VRMA animates an expression weight as the X component of its
            // node's translation. No other path on that node carries anything.
            if (channel.target_path != cgltf_animation_path_type_translation) {
                outDocument->warnings.push_back(
                    "[VRMA108] ignored non-translation channel on expression '" +
                    outDocument->expressions[expressionIt->second].name + "'");
                continue;
            }
            weights[expressionIt->second] = channel.sampler;
            outDocument->expressions[expressionIt->second].isAnimated = true;
            keepKeys(channel.sampler);
            continue;
        }

        if (channel.target_path == cgltf_animation_path_type_rotation) {
            rotations[jointIt->second] = channel.sampler;
        } else if (channel.target_path == cgltf_animation_path_type_translation) {
            if (outDocument->joints[jointIt->second].bone == motion::HumanBone::Hips) {
                translations[jointIt->second] = channel.sampler;
            } else {
                outDocument->warnings.push_back(
                    "[VRMA103] ignored non-hips translation channel");
                continue;
            }
        } else if (channel.target_path == cgltf_animation_path_type_scale) {
            outDocument->warnings.push_back("[VRMA104] ignored scale channel");
            continue;
        } else {
            continue;
        }
        keepKeys(channel.sampler);
    }
    if (timeSet.empty()) {
        release();
        return fail("[VRMA007] first animation has no humanoid, expression or "
                    "look-at channel");
    }

    // An expression the clip declares and never animates can still be saying
    // something. glTF leaves an un-animated node at its own TRS, and VRMA reads
    // the weight out of that node's translation X -- so a node that authored a
    // transform states a constant weight for the whole clip, and dropping it
    // would lose a value the file gave. A node that authored none gave no
    // weight at all. What separates the two is what the file wrote, never
    // whether the number happens to be zero: a stated 0 is a weight, and an
    // absent transform is not.
    for (const auto& mapping : expressionByNodeIndex) {
        VrmaExpression& expression = outDocument->expressions[mapping.second];
        if (expression.isAnimated) continue;
        const cgltf_node& node = data->nodes[mapping.first];
        if (!node.has_translation && !node.has_matrix) continue;
        expression.constantWeight =
            static_cast<float>(NodeLocal(node).ExtractTranslation()[0]);
    }

    // The look-at node's position is where the file put it, not where its own
    // translation says: an exporter is free to parent the target under
    // something, and a position read in its parent's space would be a gaze at
    // the wrong place. Only the ancestors' *stated* transforms are composed --
    // an ancestor this animation drives would need its own channel evaluated at
    // every instant, which is a scene evaluation rather than a clip read.
    GfMatrix4d lookAtAncestors(1.0);
    if (lookAtNode) {
        lookAtAncestors = AncestorTransform(*lookAtNode);
        for (cgltf_size channelIndex = 0;
             channelIndex != animation.channels_count; ++channelIndex) {
            const cgltf_node* target = animation.channels[channelIndex].target_node;
            bool animatedAncestor = false;
            for (const cgltf_node* ancestor = lookAtNode->parent; ancestor;
                 ancestor = ancestor->parent) {
                if (ancestor == target) { animatedAncestor = true; break; }
            }
            if (animatedAncestor) {
                outDocument->warnings.push_back(
                    "[VRMA114] an ancestor of the look-at node is animated; "
                    "only its stated transform is composed into the target");
                break;
            }
        }
        if (!outDocument->lookAt.isAnimated && StatesPlacement(*lookAtNode)) {
            // Nothing drives it and the file placed it, so glTF leaves it there
            // for the whole clip: a target the file really did give, by the same
            // rule an un-animated expression node states a weight. The placement
            // can be entirely the parent's -- a target node with no transform of
            // its own under a positioned parent is at that parent, and reading
            // only the node's own TRS would report a gaze the file never
            // withheld.
            const GfVec3d target =
                (NodeLocal(*lookAtNode) * lookAtAncestors).ExtractTranslation();
            outDocument->lookAt.constantTarget =
                GfVec3f(static_cast<float>(target[0]),
                        static_cast<float>(target[1]),
                        static_cast<float>(target[2]));
        }
    }

    bool warnedWeightRange = false;
    outDocument->animation.samples.reserve(timeSet.size());
    for (const float time : timeSet) {
        motion::HumanoidPose pose;
        pose.timestamp = time;
        for (std::size_t index = 0; index != outDocument->joints.size(); ++index) {
            const VrmaJoint& joint = outDocument->joints[index];
            pose.localRotations[static_cast<std::size_t>(joint.bone)] = joint.restRotation;
            const auto rotationIt = rotations.find(index);
            if (rotationIt != rotations.end()) {
                float value[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                Sample(rotationIt->second, samplerKeys[rotationIt->second], time, 4, value);
                pose.localRotations[static_cast<std::size_t>(joint.bone)] =
                    GfQuatf(value[3], GfVec3f(value[0], value[1], value[2]));
                pose.validRotations.set(static_cast<std::size_t>(joint.bone));
            }
            const auto translationIt = translations.find(index);
            if (translationIt != translations.end()) {
                float value[3] = {0.0f, 0.0f, 0.0f};
                Sample(translationIt->second, samplerKeys[translationIt->second], time, 3, value);
                pose.root.worldPosition = GfVec3f(value[0], value[1], value[2]);
                pose.root.hasPosition = true;
            }
        }
        // The specification says a weight outside [0, 1] is clamped. It is
        // carried verbatim here and the clamp is left to whoever applies it to a
        // rig: a file that said 1.5 said 1.5, and correcting it in the reader
        // would hide the authoring tool from the operator reading the clip. The
        // warning is what keeps that choice visible.
        const auto report = [&](const std::string& name, float value) {
            if ((value < 0.0f || value > 1.0f) && !warnedWeightRange) {
                outDocument->warnings.push_back(
                    "[VRMA109] expression '" + name +
                    "' has a weight outside [0, 1]; carried unclamped");
                warnedWeightRange = true;
            }
            pose.expressions.Set(name, value);
        };
        for (const auto& weight : weights) {
            float value[3] = {0.0f, 0.0f, 0.0f};
            Sample(weight.second, samplerKeys[weight.second], time, 3, value);
            report(outDocument->expressions[weight.first].name, value[0]);
        }
        for (const VrmaExpression& expression : outDocument->expressions) {
            if (expression.constantWeight) {
                report(expression.name, *expression.constantWeight);
            }
        }
        if (lookAtSampler) {
            float value[3] = {0.0f, 0.0f, 0.0f};
            Sample(lookAtSampler, samplerKeys[lookAtSampler], time, 3, value);
            const GfVec3d target = lookAtAncestors.Transform(
                GfVec3d(value[0], value[1], value[2]));
            pose.lookAtTarget = GfVec3f(static_cast<float>(target[0]),
                                        static_cast<float>(target[1]),
                                        static_cast<float>(target[2]));
        } else if (outDocument->lookAt.constantTarget) {
            pose.lookAtTarget = *outDocument->lookAt.constantTarget;
        }
        outDocument->animation.samples.push_back(std::move(pose));
    }
    outDocument->animation.startTime = outDocument->animation.samples.front().timestamp;
    outDocument->animation.endTime = outDocument->animation.samples.back().timestamp;
    outDocument->animation.nominalFrameRate = 30.0;
    outDocument->animation.source.kind = motion::MotionSourceKind::Clip;
    outDocument->animation.source.provider = "VRMC_vrm_animation";
    outDocument->specVersion = specVersion->GetString();
    outDocument->rawExtensionJson = extensionJson;

    release();
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
