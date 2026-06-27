// SPDX-License-Identifier: Apache-2.0
#include "io/CgltfVrmDocumentReader.h"

#include "util/PathUtil.h"
#include "util/TransformUtil.h"

#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"

#include "cgltf.h"

#include <cstring>
#include <unordered_map>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// ---------------------------------------------------------------------------
// Small JSON helpers over pxr/base/js (VRM extension blocks are plain JSON).
// ---------------------------------------------------------------------------
const JsValue* _Find(const JsObject& obj, const char* key)
{
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
}

const JsObject* _AsObject(const JsValue* v)
{
    return (v && v->IsObject()) ? &v->GetJsObject() : nullptr;
}

const JsArray* _AsArray(const JsValue* v)
{
    return (v && v->IsArray()) ? &v->GetJsArray() : nullptr;
}

int _AsInt(const JsValue* v, int fallback = -1)
{
    return (v && v->IsInt()) ? v->GetInt() : fallback;
}

// ---------------------------------------------------------------------------
// cgltf node helpers
// ---------------------------------------------------------------------------
GfMatrix4d _NodeLocal(const cgltf_node& n)
{
    if (n.has_matrix) {
        return VrmConvertGltfMatrix(n.matrix);
    }
    float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
    if (n.has_translation) std::memcpy(t, n.translation, sizeof(t));
    if (n.has_rotation) std::memcpy(r, n.rotation, sizeof(r));
    if (n.has_scale) std::memcpy(s, n.scale, sizeof(s));
    return VrmComposeTrs(t, r, s);
}

// World transform in USD row-vector convention: leaf-to-root left multiply.
GfMatrix4d _NodeWorld(const cgltf_node* n)
{
    GfMatrix4d m(1.0);
    for (const cgltf_node* cur = n; cur; cur = cur->parent) {
        m = m * _NodeLocal(*cur);
    }
    return m;
}

template <typename T>
int _IndexOf(const T* element, const T* base)
{
    return element ? static_cast<int>(element - base) : -1;
}

// ---------------------------------------------------------------------------
// Accessor readers
// ---------------------------------------------------------------------------
std::vector<GfVec3f> _ReadVec3(const cgltf_accessor* acc)
{
    std::vector<GfVec3f> out;
    if (!acc) return out;
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i) {
        float v[3] = {0, 0, 0};
        cgltf_accessor_read_float(acc, i, v, 3);
        out[i] = GfVec3f(v[0], v[1], v[2]);
    }
    return out;
}

std::vector<GfVec2f> _ReadVec2(const cgltf_accessor* acc)
{
    std::vector<GfVec2f> out;
    if (!acc) return out;
    out.resize(acc->count);
    for (cgltf_size i = 0; i < acc->count; ++i) {
        float v[2] = {0, 0};
        cgltf_accessor_read_float(acc, i, v, 2);
        out[i] = GfVec2f(v[0], v[1]);
    }
    return out;
}

} // namespace

bool
CgltfVrmDocumentReader::Read(const std::string& resolvedPath,
                             const std::vector<std::byte>& bytes,
                             VrmCanonicalDocument* outDoc,
                             std::string* outError)
{
    auto fail = [&](const std::string& msg) {
        if (outError) *outError = msg;
        return false;
    };

    if (bytes.empty()) {
        return fail("empty file");
    }

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result res =
        cgltf_parse(&options, bytes.data(), bytes.size(), &data);
    if (res != cgltf_result_success || !data) {
        return fail("cgltf_parse failed (not a valid glTF/GLB container)");
    }

    // Load buffer data. VRM embeds its bin chunk in the GLB, but pass the file
    // path so any external resources still resolve.
    if (cgltf_load_buffers(&options, data, resolvedPath.c_str()) !=
        cgltf_result_success) {
        cgltf_free(data);
        return fail("cgltf_load_buffers failed (missing or unreadable buffers)");
    }

    // -----------------------------------------------------------------------
    // VRM version + raw extension JSON (cgltf hands us unparsed extension data).
    // -----------------------------------------------------------------------
    std::string vrm1Json, vrm0Json;
    for (cgltf_size i = 0; i < data->data_extensions_count; ++i) {
        const cgltf_extension& ext = data->data_extensions[i];
        if (!ext.name || !ext.data) continue;
        if (std::strcmp(ext.name, "VRMC_vrm") == 0) vrm1Json = ext.data;
        else if (std::strcmp(ext.name, "VRM") == 0) vrm0Json = ext.data;
    }

    if (!vrm1Json.empty()) {
        outDoc->version = VrmVersion::Vrm1;
        outDoc->rawVrmExtensionJson = vrm1Json;
    } else if (!vrm0Json.empty()) {
        outDoc->version = VrmVersion::Vrm0;
        outDoc->rawVrmExtensionJson = vrm0Json;
    } else {
        outDoc->version = VrmVersion::Unknown;
        outDoc->warnings.push_back(
            "no VRM or VRMC_vrm extension found; importing as plain glTF");
    }

    // -----------------------------------------------------------------------
    // Materials
    // -----------------------------------------------------------------------
    std::vector<std::string> rawMatNames;
    rawMatNames.reserve(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        rawMatNames.push_back(m.name ? m.name : "");
    }
    std::vector<std::string> matNames = VrmMakeUniqueNames(rawMatNames, "Material");

    outDoc->materials.resize(data->materials_count);
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        VrmMaterial& vm = outDoc->materials[i];
        vm.name = matNames[i];
        vm.sourceMaterialIndex = static_cast<int>(i);
        if (m.has_pbr_metallic_roughness) {
            const auto& pbr = m.pbr_metallic_roughness;
            vm.baseColor = GfVec3f(pbr.base_color_factor[0],
                                   pbr.base_color_factor[1],
                                   pbr.base_color_factor[2]);
            vm.opacity = pbr.base_color_factor[3];
            vm.metallic = pbr.metallic_factor;
            vm.roughness = pbr.roughness_factor;
        }
        vm.emissiveColor = GfVec3f(
            m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]);
        vm.doubleSided = m.double_sided;
        vm.alphaMode = (m.alpha_mode == cgltf_alpha_mode_mask)    ? "MASK"
                       : (m.alpha_mode == cgltf_alpha_mode_blend) ? "BLEND"
                                                                  : "OPAQUE";
        vm.alphaCutoff = m.alpha_cutoff;
    }

    // -----------------------------------------------------------------------
    // Skeleton: unify the joints of *all* skins into a single skeleton.
    //
    // VRM avatars are routinely split across several glTF skins that each
    // reference a subset of a shared joint hierarchy. Importing only the first
    // skin (as a naive importer would) yields a partial skeleton and breaks the
    // humanoid mapping, so we take the union of all skin joints in a
    // deterministic encounter order and remap every mesh into it.
    // -----------------------------------------------------------------------
    std::unordered_map<const cgltf_node*, int> nodeToJoint;
    std::vector<const cgltf_node*> jointNodes;  // union, in encounter order
    for (cgltf_size s = 0; s < data->skins_count; ++s) {
        const cgltf_skin& skin = data->skins[s];
        for (cgltf_size j = 0; j < skin.joints_count; ++j) {
            const cgltf_node* jn = skin.joints[j];
            if (jn && nodeToJoint.find(jn) == nodeToJoint.end()) {
                nodeToJoint[jn] = static_cast<int>(jointNodes.size());
                jointNodes.push_back(jn);
            }
        }
    }

    if (!jointNodes.empty()) {
        // Pre-compute world transforms once (used for both bind and rest).
        std::vector<GfMatrix4d> world(jointNodes.size());
        std::vector<std::string> rawJointNames(jointNodes.size());
        for (size_t j = 0; j < jointNodes.size(); ++j) {
            world[j] = _NodeWorld(jointNodes[j]);
            rawJointNames[j] =
                jointNodes[j]->name ? jointNodes[j]->name : "";
        }
        std::vector<std::string> jointNames =
            VrmMakeUniqueNames(rawJointNames, "Joint");

        outDoc->joints.resize(jointNodes.size());
        for (size_t j = 0; j < jointNodes.size(); ++j) {
            const cgltf_node* jn = jointNodes[j];
            VrmJoint& vj = outDoc->joints[j];
            vj.name = jointNames[j];
            vj.sourceNodeIndex = _IndexOf(jn, data->nodes);

            // Parent = nearest ancestor node that is itself a joint.
            int parent = -1;
            for (const cgltf_node* a = jn->parent; a; a = a->parent) {
                auto it = nodeToJoint.find(a);
                if (it != nodeToJoint.end()) { parent = it->second; break; }
            }
            vj.parentJointIndex = parent;

            // rest = transform relative to the parent joint (robust even when
            // intermediate non-joint nodes are skipped). bind defaults to the
            // node world transform, then is overridden by the inverse bind
            // matrix below when the skin supplies one.
            vj.bindTransform = world[j];
            vj.restTransform = (parent >= 0)
                ? world[j] * world[parent].GetInverse()
                : world[j];
        }

        // World-space bind transforms come from each skin's inverse bind
        // matrices (bind = inverse(IBM)); the node world transform is only a
        // fallback. A node shared by several skins should agree across them.
        std::vector<bool> bindFromIbm(jointNodes.size(), false);
        bool warnedIbmConflict = false;
        for (cgltf_size s = 0; s < data->skins_count; ++s) {
            const cgltf_skin& skin = data->skins[s];
            if (!skin.inverse_bind_matrices) continue;
            for (cgltf_size j = 0; j < skin.joints_count; ++j) {
                auto it = nodeToJoint.find(skin.joints[j]);
                if (it == nodeToJoint.end()) continue;
                float ibm[16];
                if (!cgltf_accessor_read_float(
                        skin.inverse_bind_matrices, j, ibm, 16)) {
                    continue;
                }
                GfMatrix4d bind = VrmConvertGltfMatrix(ibm).GetInverse();
                const int u = it->second;
                if (bindFromIbm[u]) {
                    if (!warnedIbmConflict &&
                        !GfIsClose(outDoc->joints[u].bindTransform, bind, 1e-4)) {
                        outDoc->warnings.push_back(
                            "joint '" + outDoc->joints[u].name +
                            "' has conflicting inverse bind matrices across "
                            "skins; keeping the first");
                        warnedIbmConflict = true;
                    }
                    continue;
                }
                outDoc->joints[u].bindTransform = bind;
                bindFromIbm[u] = true;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Meshes (one USD Mesh per glTF primitive).
    // -----------------------------------------------------------------------
    // First pass: collect raw names so we can uniquify across all primitives.
    std::vector<std::string> rawMeshNames;
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh) continue;
        const cgltf_mesh& mesh = *node.mesh;
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            std::string base = mesh.name ? mesh.name
                               : (node.name ? node.name : "Mesh");
            if (mesh.primitives_count > 1) {
                base += "_" + std::to_string(p);
            }
            rawMeshNames.push_back(base);
        }
    }
    std::vector<std::string> meshNames = VrmMakeUniqueNames(rawMeshNames, "Mesh");

    size_t meshNameCursor = 0;
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh) continue;
        const cgltf_mesh& mesh = *node.mesh;
        const bool nodeSkinned = (node.skin != nullptr) && !outDoc->joints.empty();
        const GfMatrix4d nodeWorld = _NodeWorld(&node);

        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& prim = mesh.primitives[p];
            VrmMeshPrimitive out;
            out.name = meshNames[meshNameCursor++];
            out.sourceMeshIndex = _IndexOf(&mesh, data->meshes);
            out.sourcePrimitiveIndex = static_cast<int>(p);
            out.sourceNodeIndex = static_cast<int>(n);
            out.sourceNodeName = node.name ? node.name : "";
            out.materialIndex = _IndexOf(prim.material, data->materials);
            out.nodeWorldTransform = nodeWorld;

            if (prim.type != cgltf_primitive_type_triangles) {
                outDoc->warnings.push_back(
                    "primitive '" + out.name +
                    "' is not a triangle list; skipped");
                continue;
            }

            const cgltf_accessor* jointsAcc = nullptr;
            const cgltf_accessor* weightsAcc = nullptr;
            for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
                const cgltf_attribute& attr = prim.attributes[a];
                switch (attr.type) {
                    case cgltf_attribute_type_position:
                        out.points = _ReadVec3(attr.data);
                        break;
                    case cgltf_attribute_type_normal:
                        out.normals = _ReadVec3(attr.data);
                        break;
                    case cgltf_attribute_type_texcoord:
                        if (attr.index == 0) {
                            out.uvs = _ReadVec2(attr.data);
                            for (auto& uv : out.uvs) uv = VrmConvertUv(uv);
                        }
                        break;
                    case cgltf_attribute_type_joints:
                        if (attr.index == 0) jointsAcc = attr.data;
                        break;
                    case cgltf_attribute_type_weights:
                        if (attr.index == 0) weightsAcc = attr.data;
                        break;
                    default:
                        break;
                }
            }

            if (out.points.empty()) {
                outDoc->warnings.push_back(
                    "primitive '" + out.name + "' has no POSITION; skipped");
                continue;
            }

            // Indices -> triangle face topology.
            if (prim.indices) {
                const cgltf_size ic = prim.indices->count;
                out.faceVertexIndices.resize(ic);
                for (cgltf_size i = 0; i < ic; ++i) {
                    out.faceVertexIndices[i] =
                        static_cast<int>(cgltf_accessor_read_index(prim.indices, i));
                }
            } else {
                out.faceVertexIndices.resize(out.points.size());
                for (size_t i = 0; i < out.points.size(); ++i) {
                    out.faceVertexIndices[i] = static_cast<int>(i);
                }
            }
            out.faceVertexCounts.assign(out.faceVertexIndices.size() / 3, 3);

            // Skinning. JOINTS_0 indexes into this mesh's skin's joint list;
            // remap each into the unified skeleton order.
            if (nodeSkinned && jointsAcc && weightsAcc) {
                const cgltf_skin* skin = node.skin;
                out.skinned = true;
                // glTF skinned vertices already live in the common space the
                // inverse bind matrices map from (the scene/skel root); the mesh
                // node transform is ignored. So the geom bind is identity —
                // setting it to the node world would double-apply a transform
                // glTF discards and displace the mesh at bind pose.
                out.geomBindTransform = GfMatrix4d(1.0);
                const cgltf_size vc = out.points.size();
                out.jointWeights.resize(vc);
                out.jointIndices.resize(vc * 4);
                for (cgltf_size i = 0; i < vc; ++i) {
                    cgltf_uint ji[4] = {0, 0, 0, 0};
                    float jw[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(jointsAcc, i, ji, 4);
                    cgltf_accessor_read_float(weightsAcc, i, jw, 4);
                    for (int k = 0; k < 4; ++k) {
                        int unified = 0;
                        if (ji[k] < skin->joints_count) {
                            auto it = nodeToJoint.find(skin->joints[ji[k]]);
                            if (it != nodeToJoint.end()) unified = it->second;
                        }
                        out.jointIndices[i * 4 + k] = unified;
                    }
                    out.jointWeights[i] = GfVec4f(jw[0], jw[1], jw[2], jw[3]);
                }
            }

            // Morph targets (deltas only; expression binding handled separately).
            for (cgltf_size t = 0; t < prim.targets_count; ++t) {
                const cgltf_morph_target& target = prim.targets[t];
                VrmMorphTarget vt;
                if (t < mesh.target_names_count && mesh.target_names[t]) {
                    vt.name = mesh.target_names[t];
                }
                for (cgltf_size a = 0; a < target.attributes_count; ++a) {
                    const cgltf_attribute& attr = target.attributes[a];
                    if (attr.type == cgltf_attribute_type_position)
                        vt.positionDeltas = _ReadVec3(attr.data);
                    else if (attr.type == cgltf_attribute_type_normal)
                        vt.normalDeltas = _ReadVec3(attr.data);
                }
                out.morphTargets.push_back(std::move(vt));
            }

            outDoc->meshes.push_back(std::move(out));
        }
    }

    // -----------------------------------------------------------------------
    // VRM extension JSON: humanoid mapping + meta (normalized across 0.x/1.0).
    // -----------------------------------------------------------------------
    if (!outDoc->rawVrmExtensionJson.empty()) {
        JsParseError perr;
        JsValue root = JsParseString(outDoc->rawVrmExtensionJson, &perr);
        const JsObject* rootObj = _AsObject(&root);
        if (!rootObj) {
            outDoc->warnings.push_back("VRM extension JSON could not be parsed");
        } else {
            auto nodeIndexToJoint = [&](int nodeIndex) -> int {
                if (nodeIndex < 0 ||
                    nodeIndex >= static_cast<int>(data->nodes_count)) {
                    return -1;
                }
                auto it = nodeToJoint.find(&data->nodes[nodeIndex]);
                return it != nodeToJoint.end() ? it->second : -1;
            };

            if (outDoc->version == VrmVersion::Vrm1) {
                outDoc->specVersion =
                    _Find(*rootObj, "specVersion")
                        ? _Find(*rootObj, "specVersion")->GetString()
                        : "1.0";
                if (const JsObject* meta = _AsObject(_Find(*rootObj, "meta"))) {
                    outDoc->metaJson = JsWriteToString(JsValue(*meta));
                }
                // humanoid.humanBones: { "hips": { "node": N }, ... }
                if (const JsObject* hum = _AsObject(_Find(*rootObj, "humanoid"))) {
                    if (const JsObject* bones =
                            _AsObject(_Find(*hum, "humanBones"))) {
                        for (const auto& kv : *bones) {
                            const JsObject* b = _AsObject(&kv.second);
                            int nodeIndex = b ? _AsInt(_Find(*b, "node")) : -1;
                            int joint = nodeIndexToJoint(nodeIndex);
                            if (joint >= 0) {
                                outDoc->humanoidBones.push_back({kv.first, joint});
                            }
                        }
                    }
                }
            } else if (outDoc->version == VrmVersion::Vrm0) {
                outDoc->specVersion =
                    _Find(*rootObj, "specVersion")
                        ? _Find(*rootObj, "specVersion")->GetString()
                        : "0.0";
                if (const JsObject* meta = _AsObject(_Find(*rootObj, "meta"))) {
                    outDoc->metaJson = JsWriteToString(JsValue(*meta));
                }
                // humanoid.humanBones: [ { "bone": "hips", "node": N }, ... ]
                if (const JsObject* hum = _AsObject(_Find(*rootObj, "humanoid"))) {
                    if (const JsArray* bones = _AsArray(_Find(*hum, "humanBones"))) {
                        for (const JsValue& e : *bones) {
                            const JsObject* b = _AsObject(&e);
                            if (!b) continue;
                            const JsValue* boneName = _Find(*b, "bone");
                            int nodeIndex = _AsInt(_Find(*b, "node"));
                            int joint = nodeIndexToJoint(nodeIndex);
                            if (boneName && boneName->IsString() && joint >= 0) {
                                outDoc->humanoidBones.push_back(
                                    {boneName->GetString(), joint});
                            }
                        }
                    }
                }
            }
        }
    }

    cgltf_free(data);
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
