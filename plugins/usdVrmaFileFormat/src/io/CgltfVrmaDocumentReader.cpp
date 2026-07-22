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

std::optional<motion::HumanBone> ParentOf(motion::HumanBone bone)
{
    using Bone = motion::HumanBone;
    switch (bone) {
    case Bone::Hips: return std::nullopt;
    case Bone::Spine: return Bone::Hips;
    case Bone::Chest: return Bone::Spine;
    case Bone::UpperChest: return Bone::Chest;
    case Bone::Neck: return Bone::UpperChest;
    case Bone::Head: return Bone::Neck;
    case Bone::LeftEye:
    case Bone::RightEye:
    case Bone::Jaw: return Bone::Head;
    case Bone::LeftUpperLeg:
    case Bone::RightUpperLeg: return Bone::Hips;
    case Bone::LeftLowerLeg: return Bone::LeftUpperLeg;
    case Bone::LeftFoot: return Bone::LeftLowerLeg;
    case Bone::LeftToes: return Bone::LeftFoot;
    case Bone::RightLowerLeg: return Bone::RightUpperLeg;
    case Bone::RightFoot: return Bone::RightLowerLeg;
    case Bone::RightToes: return Bone::RightFoot;
    case Bone::LeftShoulder:
    case Bone::RightShoulder: return Bone::UpperChest;
    case Bone::LeftUpperArm: return Bone::LeftShoulder;
    case Bone::LeftLowerArm: return Bone::LeftUpperArm;
    case Bone::LeftHand: return Bone::LeftLowerArm;
    case Bone::RightUpperArm: return Bone::RightShoulder;
    case Bone::RightLowerArm: return Bone::RightUpperArm;
    case Bone::RightHand: return Bone::RightLowerArm;
    case Bone::LeftThumbMetacarpal:
    case Bone::LeftIndexProximal:
    case Bone::LeftMiddleProximal:
    case Bone::LeftRingProximal:
    case Bone::LeftLittleProximal: return Bone::LeftHand;
    case Bone::LeftThumbProximal: return Bone::LeftThumbMetacarpal;
    case Bone::LeftThumbDistal: return Bone::LeftThumbProximal;
    case Bone::LeftIndexIntermediate: return Bone::LeftIndexProximal;
    case Bone::LeftIndexDistal: return Bone::LeftIndexIntermediate;
    case Bone::LeftMiddleIntermediate: return Bone::LeftMiddleProximal;
    case Bone::LeftMiddleDistal: return Bone::LeftMiddleIntermediate;
    case Bone::LeftRingIntermediate: return Bone::LeftRingProximal;
    case Bone::LeftRingDistal: return Bone::LeftRingIntermediate;
    case Bone::LeftLittleIntermediate: return Bone::LeftLittleProximal;
    case Bone::LeftLittleDistal: return Bone::LeftLittleIntermediate;
    case Bone::RightThumbMetacarpal:
    case Bone::RightIndexProximal:
    case Bone::RightMiddleProximal:
    case Bone::RightRingProximal:
    case Bone::RightLittleProximal: return Bone::RightHand;
    case Bone::RightThumbProximal: return Bone::RightThumbMetacarpal;
    case Bone::RightThumbDistal: return Bone::RightThumbProximal;
    case Bone::RightIndexIntermediate: return Bone::RightIndexProximal;
    case Bone::RightIndexDistal: return Bone::RightIndexIntermediate;
    case Bone::RightMiddleIntermediate: return Bone::RightMiddleProximal;
    case Bone::RightMiddleDistal: return Bone::RightMiddleIntermediate;
    case Bone::RightRingIntermediate: return Bone::RightRingProximal;
    case Bone::RightRingDistal: return Bone::RightRingIntermediate;
    case Bone::RightLittleIntermediate: return Bone::RightLittleProximal;
    case Bone::RightLittleDistal: return Bone::RightLittleIntermediate;
    case Bone::Count: return std::nullopt;
    }
    return std::nullopt;
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

        std::optional<motion::HumanBone> parent = ParentOf(bone);
        while (parent) {
            const auto parentIt = jointByBone.find(*parent);
            if (parentIt != jointByBone.end()) {
                joint.path = outDocument->joints[parentIt->second].path + "/" +
                    std::string(motion::HumanBoneName(bone));
                break;
            }
            parent = ParentOf(*parent);
        }
        if (joint.path.empty()) joint.path = std::string(motion::HumanBoneName(bone));
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

    std::map<std::size_t, const cgltf_animation_sampler*> rotations;
    std::map<std::size_t, const cgltf_animation_sampler*> translations;
    std::unordered_map<const cgltf_animation_sampler*, std::vector<float>> samplerKeys;
    std::set<float> timeSet;
    bool warnedCubic = false;
    for (cgltf_size channelIndex = 0; channelIndex != animation.channels_count; ++channelIndex) {
        const cgltf_animation_channel& channel = animation.channels[channelIndex];
        if (!channel.target_node || !channel.sampler || !channel.sampler->input ||
            !channel.sampler->output) continue;
        const auto jointIt = jointByNode.find(channel.target_node);
        if (jointIt == jointByNode.end()) continue;
        if (channel.sampler->interpolation == cgltf_interpolation_type_cubic_spline &&
            !warnedCubic) {
            outDocument->warnings.push_back(
                "[VRMA102] CUBICSPLINE interpolation is approximated as linear");
            warnedCubic = true;
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

        std::vector<float>& keys = samplerKeys[channel.sampler];
        if (keys.empty()) {
            keys.resize(channel.sampler->input->count);
            for (cgltf_size key = 0; key != channel.sampler->input->count; ++key) {
                cgltf_accessor_read_float(channel.sampler->input, key, &keys[key], 1);
            }
        }
        timeSet.insert(keys.begin(), keys.end());
    }
    if (timeSet.empty()) {
        release();
        return fail("[VRMA007] first animation has no humanoid rotation or hips translation");
    }

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
