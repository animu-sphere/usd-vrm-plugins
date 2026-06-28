// SPDX-License-Identifier: Apache-2.0
#include "io/CgltfVrmDocumentReader.h"

#include "util/PathUtil.h"
#include "util/TransformUtil.h"

#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"

#include "cgltf.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <unordered_map>
#include <vector>

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

const char* _WrapStr(cgltf_wrap_mode w)
{
    switch (w) {
        case cgltf_wrap_mode_clamp_to_edge: return "clamp";
        case cgltf_wrap_mode_mirrored_repeat: return "mirror";
        default: return "repeat";
    }
}

// Supported embedded image formats (USD's image plugins read png/jpg). KTX2 /
// WebP / Basis are out of scope for Phase 2.
const char* _ImageExt(const cgltf_image* img)
{
    if (img->mime_type) {
        if (std::strcmp(img->mime_type, "image/png") == 0) return "png";
        if (std::strcmp(img->mime_type, "image/jpeg") == 0) return "jpg";
        return nullptr;
    }
    return nullptr;
}

std::uint64_t _HashBytes(const void* p, size_t n)
{
    std::uint64_t h = 1469598103934665603ull;
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
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
    // Texture extraction. Embedded images are written once to a content-hashed
    // file under a managed cache dir; the USD asset path points at the extracted
    // file so usdview/usdcat resolve it. (A future Ar resolver could serve bytes
    // straight from the .vrm; for now this is the documented, simple option.)
    // -----------------------------------------------------------------------
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path cacheDir = fs::temp_directory_path(ec) / "usdVrm_cache";
    fs::create_directories(cacheDir, ec);
    const fs::path sourceDir = fs::path(resolvedPath).parent_path();
    std::unordered_map<const cgltf_image*, std::string> imageCache;

    auto extractImage = [&](const cgltf_image* img) -> std::string {
        if (!img) return {};
        auto cached = imageCache.find(img);
        if (cached != imageCache.end()) return cached->second;

        std::string result;
        if (img->buffer_view) {
            const cgltf_buffer_view* bv = img->buffer_view;
            const unsigned char* base = bv->data
                ? static_cast<const unsigned char*>(bv->data)
                : static_cast<const unsigned char*>(bv->buffer->data) + bv->offset;
            // Sniff the magic bytes (more reliable than the declared mimeType,
            // which some exporters/parsers drop); fall back to the mime hint.
            const char* ext = nullptr;
            if (bv->size >= 4 && base[0] == 0x89 && base[1] == 'P' &&
                base[2] == 'N' && base[3] == 'G') {
                ext = "png";
            } else if (bv->size >= 3 && base[0] == 0xFF && base[1] == 0xD8 &&
                       base[2] == 0xFF) {
                ext = "jpg";
            } else {
                ext = _ImageExt(img);
            }
            if (!ext) {
                outDoc->warnings.push_back(
                    "unsupported embedded image format (not PNG/JPEG); "
                    "texture skipped");
            } else {
                const std::uint64_t h = _HashBytes(base, bv->size);
                char name[32];
                std::snprintf(name, sizeof(name), "%016llx.%s",
                              static_cast<unsigned long long>(h), ext);
                const fs::path out = cacheDir / name;
                if (!fs::exists(out, ec)) {
                    // Write to a process-unique temp then rename, so a
                    // concurrent import (or a usdcat/usdview resolving the same
                    // path) never observes a half-written image.
                    static thread_local std::mt19937_64 rng(
                        std::random_device{}());
                    const fs::path tmp = cacheDir /
                        (std::string(name) + ".tmp." + std::to_string(rng()));
                    std::ofstream f(tmp, std::ios::binary);
                    if (f) {
                        f.write(reinterpret_cast<const char*>(base), bv->size);
                        f.close();
                        fs::rename(tmp, out, ec);
                        if (ec) fs::remove(tmp, ec);  // lost the race; out is fine
                    }
                }
                result = out.generic_string();
            }
        } else if (img->uri && std::strncmp(img->uri, "data:", 5) != 0) {
            // External file reference, resolved relative to the source.
            result = (sourceDir / img->uri).generic_string();
        } else {
            outDoc->warnings.push_back(
                "data-URI image not supported in Phase 2; texture skipped");
        }
        imageCache[img] = result;
        return result;
    };

    auto makeTexRef = [&](const cgltf_texture_view& tv) -> VrmTextureRef {
        VrmTextureRef r;
        if (!tv.texture || !tv.texture->image) return r;
        std::string path = extractImage(tv.texture->image);
        if (path.empty()) return r;
        r.present = true;
        r.filePath = path;
        r.uvSet = tv.texcoord;
        r.scale = tv.scale;
        if (tv.texcoord != 0) {
            outDoc->warnings.push_back(
                "texture uses TEXCOORD_" + std::to_string(tv.texcoord) +
                "; only UV set 0 is wired in Phase 2 (sampling may be wrong)");
        }
        if (tv.texture->sampler) {
            r.wrapS = _WrapStr(tv.texture->sampler->wrap_s);
            r.wrapT = _WrapStr(tv.texture->sampler->wrap_t);
        }
        if (tv.has_transform) {
            r.hasTransform = true;
            r.uvOffset = GfVec2f(tv.transform.offset[0], tv.transform.offset[1]);
            r.uvScale = GfVec2f(tv.transform.scale[0], tv.transform.scale[1]);
            r.uvRotation = tv.transform.rotation;
        }
        return r;
    };

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
            vm.baseColorTex = makeTexRef(pbr.base_color_texture);
            vm.metallicRoughnessTex = makeTexRef(pbr.metallic_roughness_texture);
        }
        vm.emissiveColor = GfVec3f(
            m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]);
        vm.normalTex = makeTexRef(m.normal_texture);
        vm.occlusionTex = makeTexRef(m.occlusion_texture);
        vm.emissiveTex = makeTexRef(m.emissive_texture);
        vm.doubleSided = m.double_sided;
        vm.alphaMode = (m.alpha_mode == cgltf_alpha_mode_mask)    ? "MASK"
                       : (m.alpha_mode == cgltf_alpha_mode_blend) ? "BLEND"
                                                                  : "OPAQUE";
        vm.alphaCutoff = m.alpha_cutoff;

        // MToon (VRM 1.0): preserved as metadata; the glTF PBR factors/textures
        // above already give a UsdPreviewSurface approximation. (VRM 0.x MToon
        // lives in VRM.materialProperties and is handled in the extension pass.)
        for (cgltf_size e = 0; e < m.extensions_count; ++e) {
            if (m.extensions[e].name &&
                std::strcmp(m.extensions[e].name, "VRMC_materials_mtoon") == 0) {
                vm.isMToon = true;
                if (m.extensions[e].data) vm.rawShaderJson = m.extensions[e].data;
            }
        }
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

        // UsdSkel requires the joints array to be topologically ordered: every
        // parent must precede its children. The skin encounter order does not
        // guarantee that, so reorder by depth (a stable sort keeps sibling order
        // and, since depth(parent) < depth(child), yields a valid topo order),
        // then remap parent links, the node->joint map (used by the mesh and
        // humanoid passes below), so all downstream joint indices stay correct.
        const int nJoints = static_cast<int>(outDoc->joints.size());
        std::vector<int> depth(nJoints, -1);
        std::function<int(int)> getDepth = [&](int i) -> int {
            if (depth[i] >= 0) return depth[i];
            int p = outDoc->joints[i].parentJointIndex;
            depth[i] = (p < 0) ? 0 : getDepth(p) + 1;
            return depth[i];
        };
        for (int i = 0; i < nJoints; ++i) getDepth(i);

        std::vector<int> order(nJoints);
        for (int i = 0; i < nJoints; ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(),
            [&](int a, int b) { return depth[a] < depth[b]; });

        std::vector<int> oldToNew(nJoints);
        for (int newIdx = 0; newIdx < nJoints; ++newIdx)
            oldToNew[order[newIdx]] = newIdx;

        std::vector<VrmJoint> reordered(nJoints);
        for (int newIdx = 0; newIdx < nJoints; ++newIdx) {
            reordered[newIdx] = outDoc->joints[order[newIdx]];
            int p = reordered[newIdx].parentJointIndex;
            reordered[newIdx].parentJointIndex = (p < 0) ? -1 : oldToNew[p];
        }
        outDoc->joints = std::move(reordered);
        for (auto& kv : nodeToJoint) kv.second = oldToNew[kv.second];
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

            // A glTF node / mesh may expand into several canonical primitives;
            // expression binds reference a node (VRM 1.0) or mesh (VRM 0.x) plus
            // a morph-target index, which we fan out to the matching primitives.
            std::unordered_map<int, std::vector<int>> nodeToPrims, meshToPrims;
            for (int mi = 0; mi < static_cast<int>(outDoc->meshes.size()); ++mi) {
                nodeToPrims[outDoc->meshes[mi].sourceNodeIndex].push_back(mi);
                meshToPrims[outDoc->meshes[mi].sourceMeshIndex].push_back(mi);
            }
            auto readWeight = [&](const JsValue* wv, float fallback) -> float {
                if (wv && wv->IsReal()) return static_cast<float>(wv->GetReal());
                if (wv && wv->IsInt()) return static_cast<float>(wv->GetInt());
                return fallback;
            };
            auto addBinds = [&](VrmExpression& expr, const std::vector<int>* prims,
                                int morphIndex, float weight) {
                if (!prims) return;
                for (int mi : *prims) {
                    if (morphIndex >= 0 && morphIndex <
                            static_cast<int>(outDoc->meshes[mi].morphTargets.size())) {
                        expr.morphBinds.push_back({mi, morphIndex, weight});
                    }
                }
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
                // expressions.preset.<name> and expressions.custom.<name>,
                // each with morphTargetBinds: [{ node, index, weight(0..1) }].
                if (const JsObject* exprs =
                        _AsObject(_Find(*rootObj, "expressions"))) {
                    for (const char* group : {"preset", "custom"}) {
                        const JsObject* g = _AsObject(_Find(*exprs, group));
                        if (!g) continue;
                        const bool preset = std::strcmp(group, "preset") == 0;
                        for (const auto& kv : *g) {
                            const JsObject* e = _AsObject(&kv.second);
                            if (!e) continue;
                            VrmExpression expr;
                            expr.name = kv.first;
                            expr.isPreset = preset;
                            const JsValue* ib = _Find(*e, "isBinary");
                            expr.isBinary = ib && ib->IsBool() && ib->GetBool();
                            if (const JsArray* binds =
                                    _AsArray(_Find(*e, "morphTargetBinds"))) {
                                for (const JsValue& bv : *binds) {
                                    const JsObject* b = _AsObject(&bv);
                                    if (!b) continue;
                                    int node = _AsInt(_Find(*b, "node"));
                                    int index = _AsInt(_Find(*b, "index"));
                                    float w = readWeight(_Find(*b, "weight"), 1.0f);
                                    auto it = nodeToPrims.find(node);
                                    addBinds(expr,
                                        it != nodeToPrims.end() ? &it->second : nullptr,
                                        index, w);
                                }
                            }
                            outDoc->expressions.push_back(std::move(expr));
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
                // blendShapeMaster.blendShapeGroups: [{ name, presetName,
                // isBinary, binds: [{ mesh, index, weight(0..100) }] }].
                if (const JsObject* bsm =
                        _AsObject(_Find(*rootObj, "blendShapeMaster"))) {
                    if (const JsArray* groups =
                            _AsArray(_Find(*bsm, "blendShapeGroups"))) {
                        for (const JsValue& gv : *groups) {
                            const JsObject* g = _AsObject(&gv);
                            if (!g) continue;
                            const JsValue* nm = _Find(*g, "name");
                            const JsValue* pn = _Find(*g, "presetName");
                            std::string preset =
                                (pn && pn->IsString()) ? pn->GetString() : "";
                            const bool isPreset =
                                !preset.empty() && preset != "unknown";
                            VrmExpression expr;
                            expr.name = isPreset ? preset
                                        : (nm && nm->IsString() ? nm->GetString()
                                                                : std::string());
                            expr.isPreset = isPreset;
                            const JsValue* ib = _Find(*g, "isBinary");
                            expr.isBinary = ib && ib->IsBool() && ib->GetBool();
                            if (const JsArray* binds =
                                    _AsArray(_Find(*g, "binds"))) {
                                for (const JsValue& bv : *binds) {
                                    const JsObject* b = _AsObject(&bv);
                                    if (!b) continue;
                                    int meshIdx = _AsInt(_Find(*b, "mesh"));
                                    int index = _AsInt(_Find(*b, "index"));
                                    // VRM 0.x weights are 0..100.
                                    float w = readWeight(_Find(*b, "weight"),
                                                         100.0f) / 100.0f;
                                    auto it = meshToPrims.find(meshIdx);
                                    addBinds(expr,
                                        it != meshToPrims.end() ? &it->second : nullptr,
                                        index, w);
                                }
                            }
                            outDoc->expressions.push_back(std::move(expr));
                        }
                    }
                }
                // materialProperties[]: VRM 0.x MToon, aligned with glTF
                // materials by index. Preserved as metadata (the glTF PBR gives
                // the UsdPreviewSurface approximation).
                if (const JsArray* mprops =
                        _AsArray(_Find(*rootObj, "materialProperties"))) {
                    for (size_t i = 0;
                         i < mprops->size() && i < outDoc->materials.size(); ++i) {
                        const JsObject* mp = _AsObject(&(*mprops)[i]);
                        if (!mp) continue;
                        const JsValue* shader = _Find(*mp, "shader");
                        if (shader && shader->IsString() &&
                            shader->GetString().find("MToon") != std::string::npos) {
                            outDoc->materials[i].isMToon = true;
                            // Only MToon blocks are surfaced (vrm:mtoon:raw); skip
                            // the serialization for Unlit/standard properties.
                            outDoc->materials[i].rawShaderJson =
                                JsWriteToString(JsValue(*mp));
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
