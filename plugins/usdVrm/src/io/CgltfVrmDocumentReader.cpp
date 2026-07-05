// SPDX-License-Identifier: Apache-2.0
#include "io/CgltfVrmDocumentReader.h"

#include "util/PathUtil.h"
#include "util/TransformUtil.h"

#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/transform.h"
#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"
#include "pxr/usd/ar/packageUtils.h"

#include "cgltf.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
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
    std::string vrm1Json, vrm0Json, springBone1Json;
    for (cgltf_size i = 0; i < data->data_extensions_count; ++i) {
        const cgltf_extension& ext = data->data_extensions[i];
        if (!ext.name || !ext.data) continue;
        if (std::strcmp(ext.name, "VRMC_vrm") == 0) vrm1Json = ext.data;
        else if (std::strcmp(ext.name, "VRM") == 0) vrm0Json = ext.data;
        // VRM 1.0 SpringBone is its own top-level extension (not under VRMC_vrm).
        else if (std::strcmp(ext.name, "VRMC_springBone") == 0)
            springBone1Json = ext.data;
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
    // Texture paths. Embedded images are addressed as package-relative assets
    // inside the .vrm container; UsdVrmPackageResolver serves those bytes to Hio
    // without a temp-dir extraction dependency.
    // -----------------------------------------------------------------------
    namespace fs = std::filesystem;
    const fs::path sourceDir = fs::path(resolvedPath).parent_path();
    const std::string packagePath = fs::path(resolvedPath).generic_string();
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
                char name[48];
                std::snprintf(name, sizeof(name), "images/%016llx.%s",
                              static_cast<unsigned long long>(
                                  _HashBytes(base, bv->size)),
                              ext);
                result = ArJoinPackageRelativePath(packagePath, name);
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
        vm.unlit = m.unlit;  // KHR_materials_unlit
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

            // Compact to the vertices this primitive actually references. glTF
            // primitives within one mesh may share a single vertex accessor (each
            // using a sub-range), so the full-accessor arrays read above can be
            // larger than the topology uses — which Hydra rejects ("Vertex
            // primvar has N elements, topology references only up to ...") and
            // then drops the primvars (incl. normals -> flat shading).
            {
                std::unordered_map<int, int> remap;
                std::vector<int> used;  // newIndex -> oldIndex
                used.reserve(out.points.size());
                for (int& idx : out.faceVertexIndices) {
                    auto it = remap.find(idx);
                    if (it != remap.end()) {
                        idx = it->second;
                    } else {
                        int n = static_cast<int>(used.size());
                        remap.emplace(idx, n);
                        used.push_back(idx);
                        idx = n;
                    }
                }
                bool identity = used.size() == out.points.size();
                for (size_t i = 0; identity && i < used.size(); ++i)
                    identity = used[i] == static_cast<int>(i);
                if (!identity) {
                    auto gather3 = [&](std::vector<GfVec3f>& v) {
                        if (v.empty()) return;
                        std::vector<GfVec3f> r;
                        r.reserve(used.size());
                        for (int o : used)
                            r.push_back(o < static_cast<int>(v.size()) ? v[o]
                                                                       : GfVec3f(0));
                        v.swap(r);
                    };
                    gather3(out.points);
                    gather3(out.normals);
                    if (!out.uvs.empty()) {
                        std::vector<GfVec2f> r;
                        r.reserve(used.size());
                        for (int o : used)
                            r.push_back(o < static_cast<int>(out.uvs.size())
                                            ? out.uvs[o]
                                            : GfVec2f(0));
                        out.uvs.swap(r);
                    }
                    if (out.skinned) {
                        const int wc = static_cast<int>(out.jointWeights.size());
                        std::vector<GfVec4f> w;
                        std::vector<int> ji;
                        w.reserve(used.size());
                        ji.reserve(used.size() * 4);
                        for (int o : used) {
                            const bool ok = o < wc;
                            w.push_back(ok ? out.jointWeights[o] : GfVec4f(0));
                            for (int k = 0; k < 4; ++k)
                                ji.push_back(ok ? out.jointIndices[o * 4 + k] : 0);
                        }
                        out.jointWeights.swap(w);
                        out.jointIndices.swap(ji);
                    }
                    for (auto& mt : out.morphTargets) {
                        gather3(mt.positionDeltas);
                        gather3(mt.normalDeltas);
                    }
                }
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
                            } else {
                                outDoc->warnings.push_back("humanoid bone '" +
                                    kv.first + "' references node " +
                                    std::to_string(nodeIndex) +
                                    " with no skeleton joint; skipped");
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
                            // materialColorBinds: drive a material color slot to a
                            // target RGBA. material is a direct glTF material index.
                            if (const JsArray* cbinds =
                                    _AsArray(_Find(*e, "materialColorBinds"))) {
                                for (const JsValue& bv : *cbinds) {
                                    const JsObject* b = _AsObject(&bv);
                                    if (!b) continue;
                                    int mat = _AsInt(_Find(*b, "material"));
                                    if (mat < 0 || mat >= static_cast<int>(
                                            outDoc->materials.size())) {
                                        continue;
                                    }
                                    VrmExpression::MaterialColorBind mb;
                                    mb.materialIndex = mat;
                                    const JsValue* tv = _Find(*b, "type");
                                    mb.type = (tv && tv->IsString())
                                        ? tv->GetString() : "color";
                                    GfVec4f c(1.0f);
                                    if (const JsArray* val =
                                            _AsArray(_Find(*b, "targetValue"))) {
                                        for (size_t k = 0; k < val->size() && k < 4; ++k)
                                            c[k] = readWeight(&(*val)[k], c[k]);
                                    }
                                    mb.targetValue = c;
                                    expr.materialColorBinds.push_back(std::move(mb));
                                }
                            }
                            outDoc->expressions.push_back(std::move(expr));
                        }
                    }
                }
                // lookAt: { type: "bone"|"expression", rangeMap*… }. Eyes come
                // from the humanoid leftEye/rightEye bones (resolved below).
                if (const JsObject* la = _AsObject(_Find(*rootObj, "lookAt"))) {
                    outDoc->lookAt.present = true;
                    const JsValue* ty = _Find(*la, "type");
                    outDoc->lookAt.type =
                        (ty && ty->IsString()) ? ty->GetString() : "bone";
                    outDoc->lookAt.rawJson = JsWriteToString(JsValue(*la));
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
                            } else if (boneName && boneName->IsString()) {
                                outDoc->warnings.push_back("humanoid bone '" +
                                    boneName->GetString() + "' references node " +
                                    std::to_string(nodeIndex) +
                                    " with no skeleton joint; skipped");
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
                            // VRM 0.x material-value binds (MToon _Color etc.) are
                            // not mapped to USD; they remain in vrm:rawExtension.
                            if (_AsArray(_Find(*g, "materialValues"))) {
                                outDoc->warnings.push_back("expression '" + expr.name +
                                    "' has VRM 0.x materialValues binds; preserved in "
                                    "vrm:rawExtension only (not mapped to USD)");
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
                // firstPerson.lookAtTypeName ("Bone" | "BlendShape"). The 0.x
                // lookAt config lives directly under firstPerson alongside
                // unrelated first-person data (firstPersonBone, meshAnnotations),
                // so preserve only the lookAt-related keys (matching the 1.0
                // lookAt block) rather than the whole firstPerson object.
                if (const JsObject* fp =
                        _AsObject(_Find(*rootObj, "firstPerson"))) {
                    outDoc->lookAt.present = true;
                    const JsValue* tn = _Find(*fp, "lookAtTypeName");
                    std::string t = (tn && tn->IsString()) ? tn->GetString() : "Bone";
                    outDoc->lookAt.type =
                        (t == "BlendShape") ? "expression" : "bone";
                    JsObject lookAtRaw;
                    for (const char* key :
                         {"lookAtTypeName", "lookAtHorizontalInner",
                          "lookAtHorizontalOuter", "lookAtVerticalDown",
                          "lookAtVerticalUp"}) {
                        if (const JsValue* v = _Find(*fp, key))
                            lookAtRaw[key] = *v;
                    }
                    outDoc->lookAt.rawJson = JsWriteToString(JsValue(lookAtRaw));
                }
            }
            // Resolve lookAt eyes from the humanoid leftEye/rightEye bones
            // (both VRM versions express the eyes via the humanoid, not lookAt).
            if (outDoc->lookAt.present) {
                for (const VrmHumanoidBone& hb : outDoc->humanoidBones) {
                    if (hb.semanticName == "leftEye")
                        outDoc->lookAt.leftEyeJoint = hb.jointIndex;
                    else if (hb.semanticName == "rightEye")
                        outDoc->lookAt.rightEyeJoint = hb.jointIndex;
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Skeletal animation. VRM files rarely embed animation, but an imported
    // .glb may. Each glTF clip's joint TRS channels are resampled onto the union
    // of their key times so the result maps straight onto UsdSkelAnimation.
    // (Morph-weight animation is a tracked follow-up.)
    // -----------------------------------------------------------------------
    if (data->animations_count > 0 && !outDoc->joints.empty()) {
        // Read one key's `comp` floats, honoring the cubic-spline layout (which
        // stores in-tangent / value / out-tangent — we take the value vertex).
        auto readKey = [](const cgltf_animation_sampler* s, size_t key,
                          int comp, float* out) {
            size_t idx = (s->interpolation == cgltf_interpolation_type_cubic_spline)
                ? key * 3 + 1 : key;
            cgltf_accessor_read_float(s->output, idx, out, comp);
        };
        // Sample a comp-vector channel at time t (STEP / LINEAR; cubic spline is
        // approximated as linear between value vertices). comp==4 => quaternion.
        // `keys` is the sampler's input times, pre-read once by the caller, so
        // the interval is found by binary search instead of rescanning the
        // accessor on every (time x joint x channel) sample.
        auto sample = [&](const cgltf_animation_sampler* s,
                          const std::vector<float>& keys, float t, int comp,
                          float* out) {
            const size_t n = keys.size();
            if (n == 0) return;
            if (t <= keys.front() || n == 1) { readKey(s, 0, comp, out); return; }
            if (t >= keys.back()) { readKey(s, n - 1, comp, out); return; }
            // keys[i] <= t < keys[hi]; upper_bound is strict so an exact key time
            // lands on that key (fixes STEP returning the previous key at t==key).
            const size_t hi = static_cast<size_t>(
                std::upper_bound(keys.begin(), keys.end(), t) - keys.begin());
            const size_t i = hi - 1;
            if (s->interpolation == cgltf_interpolation_type_step) {
                readKey(s, i, comp, out);  // right-continuous hold on [keys[i], keys[hi])
                return;
            }
            const float a = keys[i], b = keys[hi];
            const float f = (b > a) ? (t - a) / (b - a) : 0.0f;
            float va[4], vb[4];
            readKey(s, i, comp, va);
            readKey(s, hi, comp, vb);
            if (comp == 4) {  // quaternion (xyzw) -> slerp
                GfQuatd q = GfSlerp(f, GfQuatd(va[3], va[0], va[1], va[2]),
                                       GfQuatd(vb[3], vb[0], vb[1], vb[2]));
                out[0] = static_cast<float>(q.GetImaginary()[0]);
                out[1] = static_cast<float>(q.GetImaginary()[1]);
                out[2] = static_cast<float>(q.GetImaginary()[2]);
                out[3] = static_cast<float>(q.GetReal());
            } else {
                for (int c = 0; c < comp; ++c)
                    out[c] = va[c] + (vb[c] - va[c]) * f;
            }
        };

        // Each sampler's input times, read once and shared by the union-time
        // collection and the resampling below.
        std::unordered_map<const cgltf_animation_sampler*, std::vector<float>>
            samplerKeys;

        std::vector<std::string> rawAnimNames(data->animations_count);
        for (cgltf_size a = 0; a < data->animations_count; ++a) {
            rawAnimNames[a] = data->animations[a].name ? data->animations[a].name : "";
        }
        std::vector<std::string> animNames =
            VrmMakeUniqueNames(rawAnimNames, "Clip");

        for (cgltf_size a = 0; a < data->animations_count; ++a) {
            const cgltf_animation& ga = data->animations[a];
            std::set<float> timeSet;
            std::map<int, const cgltf_animation_sampler*> chT, chR, chS;
            bool cubicWarned = false;

            for (cgltf_size c = 0; c < ga.channels_count; ++c) {
                const cgltf_animation_channel& ch = ga.channels[c];
                if (!ch.target_node || !ch.sampler) continue;
                auto jit = nodeToJoint.find(ch.target_node);
                if (jit == nodeToJoint.end()) continue;  // non-joint channel
                std::map<int, const cgltf_animation_sampler*>* dst = nullptr;
                if (ch.target_path == cgltf_animation_path_type_translation) dst = &chT;
                else if (ch.target_path == cgltf_animation_path_type_rotation) dst = &chR;
                else if (ch.target_path == cgltf_animation_path_type_scale) dst = &chS;
                else continue;
                (*dst)[jit->second] = ch.sampler;
                if (ch.sampler->interpolation ==
                        cgltf_interpolation_type_cubic_spline && !cubicWarned) {
                    outDoc->warnings.push_back("animation '" + animNames[a] +
                        "' uses CUBICSPLINE; approximated as linear");
                    cubicWarned = true;
                }
                std::vector<float>& keys = samplerKeys[ch.sampler];
                if (keys.empty()) {  // read this sampler's input times once
                    const cgltf_accessor* in = ch.sampler->input;
                    keys.resize(in->count);
                    for (cgltf_size i = 0; i < in->count; ++i)
                        cgltf_accessor_read_float(in, i, &keys[i], 1);
                }
                timeSet.insert(keys.begin(), keys.end());
            }
            if (timeSet.empty()) continue;

            VrmAnimation clip;
            clip.name = animNames[a];
            clip.times.assign(timeSet.begin(), timeSet.end());

            std::set<int> jointSet;
            for (auto& kv : chT) jointSet.insert(kv.first);
            for (auto& kv : chR) jointSet.insert(kv.first);
            for (auto& kv : chS) jointSet.insert(kv.first);
            clip.jointIndices.assign(jointSet.begin(), jointSet.end());

            // Rest TRS fills components a joint doesn't animate.
            const size_t nj = clip.jointIndices.size();
            std::vector<GfVec3f> restT(nj), restS(nj);
            std::vector<GfQuatf> restR(nj);
            for (size_t k = 0; k < nj; ++k) {
                GfTransform xf(outDoc->joints[clip.jointIndices[k]].restTransform);
                GfVec3d tr = xf.GetTranslation();
                restT[k] = GfVec3f(tr[0], tr[1], tr[2]);
                GfQuaternion q = xf.GetRotation().GetQuaternion();
                restR[k] = GfQuatf(static_cast<float>(q.GetReal()),
                                   GfVec3f(q.GetImaginary()[0], q.GetImaginary()[1],
                                           q.GetImaginary()[2]));
                GfVec3d sc = xf.GetScale();
                restS[k] = GfVec3f(sc[0], sc[1], sc[2]);
            }

            const size_t T = clip.times.size();
            clip.translations.resize(T);
            clip.rotations.resize(T);
            clip.scales.resize(T);
            for (size_t ti = 0; ti < T; ++ti) {
                const float t = clip.times[ti];
                clip.translations[ti].resize(nj);
                clip.rotations[ti].resize(nj);
                clip.scales[ti].resize(nj);
                for (size_t k = 0; k < nj; ++k) {
                    const int j = clip.jointIndices[k];
                    float v[4] = {0, 0, 0, 0};
                    auto itT = chT.find(j);
                    if (itT != chT.end()) {
                        sample(itT->second, samplerKeys[itT->second], t, 3, v);
                        clip.translations[ti][k] = GfVec3f(v[0], v[1], v[2]);
                    } else {
                        clip.translations[ti][k] = restT[k];
                    }
                    auto itR = chR.find(j);
                    if (itR != chR.end()) {
                        sample(itR->second, samplerKeys[itR->second], t, 4, v);
                        clip.rotations[ti][k] =
                            GfQuatf(v[3], GfVec3f(v[0], v[1], v[2]));
                    } else {
                        clip.rotations[ti][k] = restR[k];
                    }
                    auto itS = chS.find(j);
                    if (itS != chS.end()) {
                        sample(itS->second, samplerKeys[itS->second], t, 3, v);
                        clip.scales[ti][k] = GfVec3f(v[0], v[1], v[2]);
                    } else {
                        clip.scales[ti][k] = restS[k];
                    }
                }
            }
            outDoc->animations.push_back(std::move(clip));
        }
    }

    // -----------------------------------------------------------------------
    // Secondary motion (SpringBone). VRM 1.0: top-level VRMC_springBone. VRM
    // 0.x: VRM.secondaryAnimation. Imported as data only (no simulation). Node
    // refs resolve to skeleton joints where possible; the source node index/name
    // (and the raw block) are kept for whatever can't be resolved.
    // -----------------------------------------------------------------------
    {
        VrmSecondaryMotion& sm = outDoc->secondaryMotion;
        auto nodeJoint = [&](int ni) -> int {
            if (ni < 0 || ni >= static_cast<int>(data->nodes_count)) return -1;
            auto it = nodeToJoint.find(&data->nodes[ni]);
            return it != nodeToJoint.end() ? it->second : -1;
        };
        auto nodeName = [&](int ni) -> std::string {
            if (ni < 0 || ni >= static_cast<int>(data->nodes_count)) return {};
            return data->nodes[ni].name ? data->nodes[ni].name : std::string();
        };
        auto fval = [&](const JsValue* v, float fb) -> float {
            if (v && v->IsReal()) return static_cast<float>(v->GetReal());
            if (v && v->IsInt()) return static_cast<float>(v->GetInt());
            return fb;
        };
        auto vec3 = [&](const JsValue* v, GfVec3f fb) -> GfVec3f {
            if (const JsArray* a = _AsArray(v)) {
                if (a->size() >= 3)
                    return GfVec3f(fval(&(*a)[0], fb[0]), fval(&(*a)[1], fb[1]),
                                   fval(&(*a)[2], fb[2]));
            }
            if (const JsObject* o = _AsObject(v)) {  // VRM 0.x {x,y,z}
                return GfVec3f(fval(_Find(*o, "x"), fb[0]),
                               fval(_Find(*o, "y"), fb[1]),
                               fval(_Find(*o, "z"), fb[2]));
            }
            return fb;
        };

        if (outDoc->version == VrmVersion::Vrm1 && !springBone1Json.empty()) {
            JsParseError perr;
            JsValue root = JsParseString(springBone1Json, &perr);
            if (const JsObject* obj = _AsObject(&root)) {
                sm.present = true;
                sm.rawJson = springBone1Json;
                if (const JsArray* cols = _AsArray(_Find(*obj, "colliders"))) {
                    for (const JsValue& cv : *cols) {
                        const JsObject* co = _AsObject(&cv);
                        if (!co) continue;
                        VrmCollider c;
                        int ni = _AsInt(_Find(*co, "node"));
                        c.sourceNodeIndex = ni;
                        c.sourceNodeName = nodeName(ni);
                        c.jointIndex = nodeJoint(ni);
                        const JsObject* shape = _AsObject(_Find(*co, "shape"));
                        const JsObject* sph =
                            shape ? _AsObject(_Find(*shape, "sphere")) : nullptr;
                        const JsObject* cap =
                            shape ? _AsObject(_Find(*shape, "capsule")) : nullptr;
                        if (sph) {
                            c.shape = "sphere";
                            c.offset = vec3(_Find(*sph, "offset"), GfVec3f(0));
                            c.radius = fval(_Find(*sph, "radius"), 0);
                        } else if (cap) {
                            c.shape = "capsule";
                            c.offset = vec3(_Find(*cap, "offset"), GfVec3f(0));
                            c.radius = fval(_Find(*cap, "radius"), 0);
                            c.tail = vec3(_Find(*cap, "tail"), GfVec3f(0));
                        }
                        sm.colliders.push_back(c);
                    }
                }
                if (const JsArray* grps =
                        _AsArray(_Find(*obj, "colliderGroups"))) {
                    for (const JsValue& gv : *grps) {
                        const JsObject* g = _AsObject(&gv);
                        if (!g) continue;
                        VrmColliderGroup grp;
                        const JsValue* nm = _Find(*g, "name");
                        grp.name = (nm && nm->IsString()) ? nm->GetString()
                            : "ColliderGroup_" + std::to_string(sm.colliderGroups.size());
                        if (const JsArray* ci = _AsArray(_Find(*g, "colliders")))
                            for (const JsValue& iv : *ci)
                                grp.colliderIndices.push_back(_AsInt(&iv));
                        sm.colliderGroups.push_back(std::move(grp));
                    }
                }
                if (const JsArray* springs = _AsArray(_Find(*obj, "springs"))) {
                    for (const JsValue& sv : *springs) {
                        const JsObject* s = _AsObject(&sv);
                        if (!s) continue;
                        VrmSpring spr;
                        const JsValue* nm = _Find(*s, "name");
                        spr.name = (nm && nm->IsString()) ? nm->GetString()
                            : "Spring_" + std::to_string(sm.springs.size());
                        int centerNode = _AsInt(_Find(*s, "center"));
                        spr.centerSourceNodeIndex = centerNode;
                        spr.centerSourceNodeName = nodeName(centerNode);
                        spr.centerJoint = nodeJoint(centerNode);
                        if (const JsArray* cg =
                                _AsArray(_Find(*s, "colliderGroups")))
                            for (const JsValue& iv : *cg)
                                spr.colliderGroupIndices.push_back(_AsInt(&iv));
                        if (const JsArray* joints = _AsArray(_Find(*s, "joints"))) {
                            for (const JsValue& jv : *joints) {
                                const JsObject* j = _AsObject(&jv);
                                if (!j) continue;
                                VrmSpringJoint sj;
                                int ni = _AsInt(_Find(*j, "node"));
                                sj.sourceNodeIndex = ni;
                                sj.sourceNodeName = nodeName(ni);
                                sj.jointIndex = nodeJoint(ni);
                                sj.hitRadius = fval(_Find(*j, "hitRadius"), 0);
                                sj.stiffness = fval(_Find(*j, "stiffness"), 1);
                                sj.gravityPower = fval(_Find(*j, "gravityPower"), 0);
                                sj.dragForce = fval(_Find(*j, "dragForce"), 0.4f);
                                sj.gravityDir = vec3(_Find(*j, "gravityDir"),
                                                     GfVec3f(0, -1, 0));
                                spr.joints.push_back(sj);
                            }
                        }
                        sm.springs.push_back(std::move(spr));
                    }
                }
            }
        } else if (outDoc->version == VrmVersion::Vrm0 &&
                   !outDoc->rawVrmExtensionJson.empty()) {
            JsParseError perr;
            JsValue root = JsParseString(outDoc->rawVrmExtensionJson, &perr);
            const JsObject* vrm = _AsObject(&root);
            const JsObject* sa =
                vrm ? _AsObject(_Find(*vrm, "secondaryAnimation")) : nullptr;
            if (sa) {
                sm.present = true;
                sm.rawJson = JsWriteToString(JsValue(*sa));
                // colliderGroups: [{ node, colliders: [{ offset, radius }] }].
                if (const JsArray* grps =
                        _AsArray(_Find(*sa, "colliderGroups"))) {
                    for (const JsValue& gv : *grps) {
                        const JsObject* g = _AsObject(&gv);
                        if (!g) continue;
                        int ni = _AsInt(_Find(*g, "node"));
                        VrmColliderGroup grp;
                        grp.name =
                            "ColliderGroup_" + std::to_string(sm.colliderGroups.size());
                        if (const JsArray* cs = _AsArray(_Find(*g, "colliders"))) {
                            for (const JsValue& cv : *cs) {
                                const JsObject* co = _AsObject(&cv);
                                if (!co) continue;
                                VrmCollider c;
                                c.shape = "sphere";  // VRM 0.x is spheres only
                                c.sourceNodeIndex = ni;
                                c.sourceNodeName = nodeName(ni);
                                c.jointIndex = nodeJoint(ni);
                                c.offset = vec3(_Find(*co, "offset"), GfVec3f(0));
                                c.radius = fval(_Find(*co, "radius"), 0);
                                grp.colliderIndices.push_back(
                                    static_cast<int>(sm.colliders.size()));
                                sm.colliders.push_back(c);
                            }
                        }
                        sm.colliderGroups.push_back(std::move(grp));
                    }
                }
                // boneGroups: per-group params replicated onto each bone joint.
                if (const JsArray* bgs = _AsArray(_Find(*sa, "boneGroups"))) {
                    for (const JsValue& bv : *bgs) {
                        const JsObject* bg = _AsObject(&bv);
                        if (!bg) continue;
                        VrmSpring spr;
                        const JsValue* cm = _Find(*bg, "comment");
                        spr.name = (cm && cm->IsString() && !cm->GetString().empty())
                            ? cm->GetString()
                            : "Spring_" + std::to_string(sm.springs.size());
                        int centerNode = _AsInt(_Find(*bg, "center"));
                        spr.centerSourceNodeIndex = centerNode;
                        spr.centerSourceNodeName = nodeName(centerNode);
                        spr.centerJoint = nodeJoint(centerNode);
                        // VRM 0.x spells stiffness "stiffiness".
                        float stiff = fval(_Find(*bg, "stiffiness"), 1);
                        float gp = fval(_Find(*bg, "gravityPower"), 0);
                        float df = fval(_Find(*bg, "dragForce"), 0.4f);
                        float hr = fval(_Find(*bg, "hitRadius"), 0);
                        GfVec3f gd = vec3(_Find(*bg, "gravityDir"), GfVec3f(0, -1, 0));
                        if (const JsArray* cg =
                                _AsArray(_Find(*bg, "colliderGroups")))
                            for (const JsValue& iv : *cg)
                                spr.colliderGroupIndices.push_back(_AsInt(&iv));
                        if (const JsArray* bones = _AsArray(_Find(*bg, "bones"))) {
                            for (const JsValue& nv : *bones) {
                                int ni = _AsInt(&nv);
                                VrmSpringJoint sj;
                                sj.sourceNodeIndex = ni;
                                sj.sourceNodeName = nodeName(ni);
                                sj.jointIndex = nodeJoint(ni);
                                sj.stiffness = stiff;
                                sj.gravityPower = gp;
                                sj.dragForce = df;
                                sj.hitRadius = hr;
                                sj.gravityDir = gd;
                                spr.joints.push_back(sj);
                            }
                        }
                        sm.springs.push_back(std::move(spr));
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Node constraints (VRMC_node_constraint, VRM 1.0 only). Each constrained
    // node carries the extension and names a source node; imported as data only.
    // -----------------------------------------------------------------------
    if (outDoc->version == VrmVersion::Vrm1) {
        auto nodeJoint = [&](int ni) -> int {
            if (ni < 0 || ni >= static_cast<int>(data->nodes_count)) return -1;
            auto it = nodeToJoint.find(&data->nodes[ni]);
            return it != nodeToJoint.end() ? it->second : -1;
        };
        auto nodeName = [&](int ni) -> std::string {
            if (ni < 0 || ni >= static_cast<int>(data->nodes_count)) return {};
            return data->nodes[ni].name ? data->nodes[ni].name : std::string();
        };
        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
            const cgltf_node& node = data->nodes[ni];
            for (cgltf_size e = 0; e < node.extensions_count; ++e) {
                if (!node.extensions[e].name || !node.extensions[e].data ||
                    std::strcmp(node.extensions[e].name,
                                "VRMC_node_constraint") != 0) {
                    continue;
                }
                JsParseError perr;
                JsValue root = JsParseString(node.extensions[e].data, &perr);
                const JsObject* obj = _AsObject(&root);
                const JsObject* c =
                    obj ? _AsObject(_Find(*obj, "constraint")) : nullptr;
                if (!c) continue;

                const JsObject* spec = nullptr;
                const char* axisKey = nullptr;
                std::string type;
                if ((spec = _AsObject(_Find(*c, "roll")))) {
                    type = "roll"; axisKey = "rollAxis";
                } else if ((spec = _AsObject(_Find(*c, "aim")))) {
                    type = "aim"; axisKey = "aimAxis";
                } else if ((spec = _AsObject(_Find(*c, "rotation")))) {
                    type = "rotation";
                }
                if (!spec) continue;

                VrmConstraint con;
                con.type = type;
                con.rawJson = node.extensions[e].data;
                con.constrainedNodeIndex = static_cast<int>(ni);
                con.constrainedNodeName = nodeName(static_cast<int>(ni));
                con.constrainedJoint = nodeJoint(static_cast<int>(ni));
                int src = _AsInt(_Find(*spec, "source"));
                if (src < 0 || src >= static_cast<int>(data->nodes_count)) {
                    outDoc->warnings.push_back("node constraint on '" +
                        con.constrainedNodeName + "' (" + type +
                        ") has no valid source node; skipped");
                    continue;
                }
                con.sourceNodeIndex = src;
                con.sourceNodeName = nodeName(src);
                con.sourceJoint = nodeJoint(src);
                if (axisKey) {
                    const JsValue* ax = _Find(*spec, axisKey);
                    if (ax && ax->IsString()) con.axis = ax->GetString();
                }
                const JsValue* w = _Find(*spec, "weight");
                con.weight = (w && w->IsReal()) ? static_cast<float>(w->GetReal())
                           : (w && w->IsInt())  ? static_cast<float>(w->GetInt())
                                                : 1.0f;
                outDoc->constraints.push_back(std::move(con));
            }
        }
    }

    cgltf_free(data);
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
