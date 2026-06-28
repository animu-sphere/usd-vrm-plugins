// SPDX-License-Identifier: Apache-2.0
#include "usd/UsdVrmAuthorer.h"

#include "util/PathUtil.h"

#include "pxr/base/gf/range3f.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/kind/registry.h"
#include "pxr/usd/sdf/assetPath.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/primvarsAPI.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/materialBindingAPI.h"
#include "pxr/usd/usdShade/shader.h"
#include "pxr/usd/usdSkel/animation.h"
#include "pxr/usd/usdSkel/bindingAPI.h"
#include "pxr/usd/usdSkel/blendShape.h"
#include "pxr/usd/usdSkel/root.h"
#include "pxr/usd/usdSkel/skeleton.h"

#include <functional>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

const TfToken _kAssetName("Asset");

// Build the relative joint path (e.g. "Hips/Spine/Chest") for every joint, in
// canonical (skin) order, resolving parents via memoized recursion so the input
// order need not be parents-first.
std::vector<std::string>
_BuildJointPaths(const std::vector<VrmJoint>& joints)
{
    std::vector<std::string> paths(joints.size());
    std::vector<bool> done(joints.size(), false);
    std::function<const std::string&(int)> resolve = [&](int i) -> const std::string& {
        if (!done[i]) {
            const VrmJoint& j = joints[i];
            if (j.parentJointIndex >= 0 &&
                j.parentJointIndex < static_cast<int>(joints.size()) &&
                j.parentJointIndex != i) {
                paths[i] = resolve(j.parentJointIndex) + "/" + j.name;
            } else {
                paths[i] = j.name;
            }
            done[i] = true;
        }
        return paths[i];
    };
    for (size_t i = 0; i < joints.size(); ++i) resolve(static_cast<int>(i));
    return paths;
}

void _SetSourceMetadata(const UsdPrim& prim, const VrmMeshPrimitive& m)
{
    if (m.sourceNodeIndex >= 0)
        prim.SetCustomDataByKey(TfToken("vrm:sourceNodeIndex"), VtValue(m.sourceNodeIndex));
    if (!m.sourceNodeName.empty())
        prim.SetCustomDataByKey(TfToken("vrm:sourceNodeName"), VtValue(m.sourceNodeName));
    if (m.sourceMeshIndex >= 0)
        prim.SetCustomDataByKey(TfToken("vrm:sourceMeshIndex"), VtValue(m.sourceMeshIndex));
    if (m.sourcePrimitiveIndex >= 0)
        prim.SetCustomDataByKey(TfToken("vrm:sourcePrimitiveIndex"), VtValue(m.sourcePrimitiveIndex));
}

} // namespace

bool
UsdVrmAuthorer::WriteToString(const VrmCanonicalDocument& doc,
                              std::string* outUsda,
                              std::vector<std::string>* outWarnings) const
{
    auto warn = [&](const std::string& w) {
        if (outWarnings) outWarnings->push_back(w);
    };

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    UsdStageRefPtr stage = UsdStage::Open(layer);
    if (!stage) {
        return false;
    }
    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y);
    UsdGeomSetStageMetersPerUnit(stage, 1.0);

    const bool hasSkel = !doc.joints.empty();
    const SdfPath assetPath("/Asset");
    const SdfPath geoPath = assetPath.AppendChild(TfToken("geo"));
    const SdfPath mtlPath = assetPath.AppendChild(TfToken("mtl"));
    const SdfPath skelScopePath = assetPath.AppendChild(TfToken("skel"));
    const SdfPath skelPath = skelScopePath.AppendChild(TfToken("Skeleton"));
    const SdfPath rigPath = assetPath.AppendChild(TfToken("rig"));

    // /Asset — a SkelRoot when there is a skeleton (UsdSkel needs a SkelRoot
    // ancestor enclosing the skinned meshes and the skeleton), otherwise a plain
    // Xform. Either way kind=component and the stage's default prim.
    UsdPrim assetPrim;
    if (hasSkel) {
        assetPrim = UsdSkelRoot::Define(stage, assetPath).GetPrim();
    } else {
        assetPrim = UsdGeomXform::Define(stage, assetPath).GetPrim();
    }
    UsdModelAPI(assetPrim).SetKind(KindTokens->component);
    stage->SetDefaultPrim(assetPrim);

    // VRM provenance + lossless preservation on /Asset.customData.
    const char* srcVer = doc.version == VrmVersion::Vrm1   ? "1.0"
                         : doc.version == VrmVersion::Vrm0 ? "0.x"
                                                          : "unknown";
    assetPrim.SetCustomDataByKey(TfToken("vrm:sourceFormat"), VtValue(std::string("VRM")));
    assetPrim.SetCustomDataByKey(TfToken("vrm:sourceVersion"), VtValue(std::string(srcVer)));
    if (!doc.specVersion.empty())
        assetPrim.SetCustomDataByKey(TfToken("vrm:specVersion"), VtValue(doc.specVersion));
    if (!doc.metaJson.empty())
        assetPrim.SetCustomDataByKey(TfToken("vrm:meta"), VtValue(doc.metaJson));
    // Lossless preservation: keep the full VRM/VRMC_vrm extension block verbatim
    // so later phases (and external tools) can recover anything not yet mapped.
    if (!doc.rawVrmExtensionJson.empty())
        assetPrim.SetCustomDataByKey(TfToken("vrm:rawExtension"),
                                     VtValue(doc.rawVrmExtensionJson));

    // Front-direction normalization. VRM 0.x avatars face -Z; VRM 1.0
    // standardized on +Z. Rotate VRM 0.x by 180 deg about the up axis at the
    // root so every imported avatar faces +Z. Because it's a single
    // SkelRoot-level transform, skinning, animation, blend shapes and lookAt all
    // keep working — the whole local frame just rotates.
    const char* frontAxis = doc.version == VrmVersion::Vrm0   ? "-Z"
                            : doc.version == VrmVersion::Vrm1 ? "+Z"
                                                             : "unknown";
    assetPrim.SetCustomDataByKey(TfToken("vrm:sourceFrontAxis"),
                                 VtValue(std::string(frontAxis)));
    // Only VRM 0.x needs rotating; record the flag for every version so
    // consumers never have to distinguish "not normalized" from "key absent".
    const bool normalized = (doc.version == VrmVersion::Vrm0);
    if (normalized)
        UsdGeomXformable(assetPrim).AddRotateYOp().Set(180.0f);
    assetPrim.SetCustomDataByKey(TfToken("vrm:frontAxisNormalized"),
                                 VtValue(normalized));

    // -----------------------------------------------------------------------
    // Materials.
    // -----------------------------------------------------------------------
    UsdGeomScope::Define(stage, mtlPath);
    std::vector<SdfPath> materialPaths(doc.materials.size());
    for (size_t i = 0; i < doc.materials.size(); ++i) {
        const VrmMaterial& vm = doc.materials[i];
        SdfPath matPath = mtlPath.AppendChild(TfToken(vm.name));
        materialPaths[i] = matPath;

        UsdShadeMaterial mat = UsdShadeMaterial::Define(stage, matPath);
        UsdShadeShader shader =
            UsdShadeShader::Define(stage, matPath.AppendChild(TfToken("Surface")));
        shader.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));
        // VRM materials are unlit (KHR_materials_unlit) / toon. Render unlit as
        // base color through emissive with no lit response, so scene lights
        // don't carve facets into the low-poly surface (the "polygonal" look).
        const bool unlit = vm.unlit;
        shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f)
            .Set(unlit ? GfVec3f(0.0f) : vm.baseColor);
        shader.CreateInput(TfToken("emissiveColor"), SdfValueTypeNames->Color3f)
            .Set(unlit ? vm.baseColor : vm.emissiveColor);
        shader.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float)
            .Set(unlit ? 0.0f : vm.metallic);
        shader.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float)
            .Set(unlit ? 1.0f : vm.roughness);
        shader.CreateInput(TfToken("opacity"), SdfValueTypeNames->Float)
            .Set(vm.opacity);
        if (vm.alphaMode == "MASK") {
            shader.CreateInput(TfToken("opacityThreshold"), SdfValueTypeNames->Float)
                .Set(vm.alphaCutoff);
        }
        UsdShadeOutput surfaceOut =
            shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);
        mat.CreateSurfaceOutput().ConnectToSource(surfaceOut);

        // Textures. A single UsdPrimvarReader_float2 feeds every UsdUVTexture's
        // st; each glTF texture slot becomes one UsdUVTexture wired into the
        // matching UsdPreviewSurface input. (glTF's factor*texture multiply is
        // approximated by the texture alone — a follow-up may insert multiplies.)
        bool anyTex = vm.baseColorTex.present || vm.metallicRoughnessTex.present ||
                      vm.normalTex.present || vm.emissiveTex.present ||
                      vm.occlusionTex.present;
        UsdShadeShader stReader;
        if (anyTex) {
            stReader = UsdShadeShader::Define(
                stage, matPath.AppendChild(TfToken("stReader")));
            stReader.CreateIdAttr(VtValue(TfToken("UsdPrimvarReader_float2")));
            stReader.CreateInput(TfToken("varname"), SdfValueTypeNames->Token)
                .Set(TfToken("st"));
            stReader.CreateOutput(TfToken("result"), SdfValueTypeNames->Float2);
        }

        auto makeTexture = [&](const VrmTextureRef& ref, const char* nodeName,
                               bool color) -> UsdShadeShader {
            UsdShadeShader tex = UsdShadeShader::Define(
                stage, matPath.AppendChild(TfToken(nodeName)));
            tex.CreateIdAttr(VtValue(TfToken("UsdUVTexture")));
            tex.CreateInput(TfToken("file"), SdfValueTypeNames->Asset)
                .Set(SdfAssetPath(ref.filePath));
            tex.CreateInput(TfToken("wrapS"), SdfValueTypeNames->Token)
                .Set(TfToken(ref.wrapS));
            tex.CreateInput(TfToken("wrapT"), SdfValueTypeNames->Token)
                .Set(TfToken(ref.wrapT));
            tex.CreateInput(TfToken("sourceColorSpace"), SdfValueTypeNames->Token)
                .Set(TfToken(color ? "sRGB" : "raw"));
            UsdShadeInput st =
                tex.CreateInput(TfToken("st"), SdfValueTypeNames->Float2);
            // KHR_texture_transform -> UsdTransform2d between the reader and st.
            if (ref.hasTransform) {
                UsdShadeShader xf = UsdShadeShader::Define(stage,
                    matPath.AppendChild(TfToken(std::string(nodeName) + "_xf")));
                xf.CreateIdAttr(VtValue(TfToken("UsdTransform2d")));
                xf.CreateInput(TfToken("in"), SdfValueTypeNames->Float2)
                    .ConnectToSource(stReader.GetOutput(TfToken("result")));
                xf.CreateInput(TfToken("translation"), SdfValueTypeNames->Float2)
                    .Set(ref.uvOffset);
                xf.CreateInput(TfToken("scale"), SdfValueTypeNames->Float2)
                    .Set(ref.uvScale);
                xf.CreateInput(TfToken("rotation"), SdfValueTypeNames->Float)
                    .Set(ref.uvRotation);
                st.ConnectToSource(
                    xf.CreateOutput(TfToken("result"), SdfValueTypeNames->Float2));
            } else {
                st.ConnectToSource(stReader.GetOutput(TfToken("result")));
            }
            tex.CreateOutput(TfToken("rgb"), SdfValueTypeNames->Float3);
            tex.CreateOutput(TfToken("r"), SdfValueTypeNames->Float);
            tex.CreateOutput(TfToken("g"), SdfValueTypeNames->Float);
            tex.CreateOutput(TfToken("b"), SdfValueTypeNames->Float);
            tex.CreateOutput(TfToken("a"), SdfValueTypeNames->Float);
            return tex;
        };

        if (vm.baseColorTex.present) {
            UsdShadeShader t = makeTexture(vm.baseColorTex, "baseColorTexture", true);
            // Unlit routes base color to emissive (flat); lit routes to diffuse.
            shader.GetInput(TfToken(unlit ? "emissiveColor" : "diffuseColor"))
                .ConnectToSource(t.GetOutput(TfToken("rgb")));
            if (vm.alphaMode != "OPAQUE") {
                shader.GetInput(TfToken("opacity"))
                    .ConnectToSource(t.GetOutput(TfToken("a")));
            }
        }
        // Lit-only slots (metallicRoughness / emissive / occlusion / normal) are
        // ignored by KHR_materials_unlit, so skip them on an unlit surface. This
        // also keeps the emissive texture from clobbering the base-color->emissive
        // connection authored above (a single UsdShade input takes one source).
        if (!unlit && vm.metallicRoughnessTex.present) {
            UsdShadeShader t =
                makeTexture(vm.metallicRoughnessTex, "metallicRoughnessTexture", false);
            // glTF packs roughness in G, metalness in B.
            shader.GetInput(TfToken("roughness"))
                .ConnectToSource(t.GetOutput(TfToken("g")));
            shader.GetInput(TfToken("metallic"))
                .ConnectToSource(t.GetOutput(TfToken("b")));
        }
        if (!unlit && vm.emissiveTex.present) {
            UsdShadeShader t = makeTexture(vm.emissiveTex, "emissiveTexture", true);
            shader.GetInput(TfToken("emissiveColor"))
                .ConnectToSource(t.GetOutput(TfToken("rgb")));
        }
        if (!unlit && vm.occlusionTex.present) {
            UsdShadeShader t = makeTexture(vm.occlusionTex, "occlusionTexture", false);
            // glTF occlusion strength: ao = 1 + strength * (sampled - 1), i.e.
            // out.r = sampled*strength + (1 - strength). Fold into the texture
            // scale/bias so the strength is honored, not dropped.
            const float os = vm.occlusionTex.scale;
            t.CreateInput(TfToken("scale"), SdfValueTypeNames->Float4)
                .Set(GfVec4f(os, os, os, os));
            t.CreateInput(TfToken("bias"), SdfValueTypeNames->Float4)
                .Set(GfVec4f(1.0f - os, 1.0f - os, 1.0f - os, 1.0f - os));
            shader.CreateInput(TfToken("occlusion"), SdfValueTypeNames->Float)
                .ConnectToSource(t.GetOutput(TfToken("r")));
        }
        if (!unlit && vm.normalTex.present) {
            UsdShadeShader t = makeTexture(vm.normalTex, "normalTexture", false);
            // Decode tangent-space normals ([0,1] -> [-1,1]) and fold in glTF's
            // normalTexture.scale, which scales only the X/Y components:
            //   x,y = (2c - 1) * scale ;  z = 2c - 1
            const float ns = vm.normalTex.scale;
            t.CreateInput(TfToken("scale"), SdfValueTypeNames->Float4)
                .Set(GfVec4f(2.0f * ns, 2.0f * ns, 2.0f, 2.0f));
            t.CreateInput(TfToken("bias"), SdfValueTypeNames->Float4)
                .Set(GfVec4f(-ns, -ns, -1.0f, -1.0f));
            shader.CreateInput(TfToken("normal"), SdfValueTypeNames->Normal3f)
                .ConnectToSource(t.GetOutput(TfToken("rgb")));
        }

        // MToon: keep the glTF/UsdPreviewSurface approximation, tag the shader
        // model, and preserve the raw extension block for a later MaterialX /
        // dedicated shader-graph pass.
        if (vm.isMToon) {
            mat.GetPrim().CreateAttribute(TfToken("vrm:shaderModel"),
                SdfValueTypeNames->Token, true, SdfVariabilityUniform)
                .Set(TfToken("MToon"));
            if (!vm.rawShaderJson.empty()) {
                mat.GetPrim().SetCustomDataByKey(TfToken("vrm:mtoon:raw"),
                                                 VtValue(vm.rawShaderJson));
            }
        }

        if (vm.sourceMaterialIndex >= 0)
            mat.GetPrim().SetCustomDataByKey(TfToken("vrm:sourceMaterialIndex"),
                                             VtValue(vm.sourceMaterialIndex));
    }

    // -----------------------------------------------------------------------
    // Geometry.
    // -----------------------------------------------------------------------
    UsdGeomScope::Define(stage, geoPath);
    for (const VrmMeshPrimitive& m : doc.meshes) {
        SdfPath meshPath = geoPath.AppendChild(TfToken(m.name));
        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, meshPath);

        VtVec3fArray points(m.points.begin(), m.points.end());
        mesh.CreatePointsAttr(VtValue(points));
        mesh.CreateFaceVertexIndicesAttr(
            VtValue(VtIntArray(m.faceVertexIndices.begin(), m.faceVertexIndices.end())));
        mesh.CreateFaceVertexCountsAttr(
            VtValue(VtIntArray(m.faceVertexCounts.begin(), m.faceVertexCounts.end())));
        mesh.CreateSubdivisionSchemeAttr(VtValue(UsdGeomTokens->none));

        // Extent.
        GfRange3f range;
        for (const GfVec3f& p : m.points) range.UnionWith(p);
        if (!range.IsEmpty()) {
            VtVec3fArray extent(2);
            extent[0] = range.GetMin();
            extent[1] = range.GetMax();
            mesh.CreateExtentAttr(VtValue(extent));
        }

        if (!m.normals.empty()) {
            // Author normals as primvars:normals (not the plain `normals`
            // attribute): UsdSkelImaging only skins normals expressed as a
            // primvar, and Hydra otherwise recomputes them from the skinned
            // points — which hardens shading at the source's split vertices
            // (UV / material seams). The primvar keeps the authored smoothing.
            UsdGeomPrimvar normals = UsdGeomPrimvarsAPI(mesh).CreatePrimvar(
                TfToken("normals"), SdfValueTypeNames->Normal3fArray,
                UsdGeomTokens->vertex);
            normals.Set(VtVec3fArray(m.normals.begin(), m.normals.end()));
        }
        if (!m.uvs.empty()) {
            UsdGeomPrimvar st = UsdGeomPrimvarsAPI(mesh).CreatePrimvar(
                TfToken("st"), SdfValueTypeNames->TexCoord2fArray,
                UsdGeomTokens->vertex);
            st.Set(VtVec2fArray(m.uvs.begin(), m.uvs.end()));
        }

        if (m.materialIndex >= 0 &&
            m.materialIndex < static_cast<int>(materialPaths.size())) {
            UsdShadeMaterialBindingAPI binding =
                UsdShadeMaterialBindingAPI::Apply(mesh.GetPrim());
            binding.Bind(UsdShadeMaterial::Get(stage, materialPaths[m.materialIndex]));
        }

        // Non-skinned meshes carry their glTF node placement; skinned meshes do
        // not (glTF ignores the node transform for skinning — verts are in
        // skel-root space and geomBindTransform is identity).
        if (!m.skinned &&
            !GfIsClose(m.nodeWorldTransform, GfMatrix4d(1.0), 1e-9)) {
            mesh.AddTransformOp().Set(m.nodeWorldTransform);
        }

        if (m.skinned && hasSkel) {
            UsdSkelBindingAPI binding = UsdSkelBindingAPI::Apply(mesh.GetPrim());
            binding.CreateSkeletonRel().SetTargets({skelPath});

            VtIntArray jointIndices(m.jointIndices.begin(), m.jointIndices.end());
            VtFloatArray jointWeights;
            jointWeights.reserve(m.jointWeights.size() * 4);
            for (const GfVec4f& w : m.jointWeights) {
                jointWeights.push_back(w[0]);
                jointWeights.push_back(w[1]);
                jointWeights.push_back(w[2]);
                jointWeights.push_back(w[3]);
            }
            UsdGeomPrimvar ji = binding.CreateJointIndicesPrimvar(false, 4);
            ji.Set(jointIndices);
            UsdGeomPrimvar jw = binding.CreateJointWeightsPrimvar(false, 4);
            jw.Set(jointWeights);
            binding.CreateGeomBindTransformAttr().Set(m.geomBindTransform);
        }

        _SetSourceMetadata(mesh.GetPrim(), m);
    }

    // -----------------------------------------------------------------------
    // Skeleton.
    // -----------------------------------------------------------------------
    std::vector<std::string> jointPaths;
    if (hasSkel) {
        UsdGeomScope::Define(stage, skelScopePath);
        UsdSkelSkeleton skel = UsdSkelSkeleton::Define(stage, skelPath);

        jointPaths = _BuildJointPaths(doc.joints);
        VtTokenArray jointTokens;
        VtTokenArray jointNames;
        VtMatrix4dArray bindXforms;
        VtMatrix4dArray restXforms;
        jointTokens.reserve(doc.joints.size());
        for (size_t i = 0; i < doc.joints.size(); ++i) {
            jointTokens.push_back(TfToken(jointPaths[i]));
            jointNames.push_back(TfToken(doc.joints[i].name));
            bindXforms.push_back(doc.joints[i].bindTransform);
            restXforms.push_back(doc.joints[i].restTransform);
        }
        skel.CreateJointsAttr(VtValue(jointTokens));
        skel.CreateJointNamesAttr(VtValue(jointNames));
        skel.CreateBindTransformsAttr(VtValue(bindXforms));
        skel.CreateRestTransformsAttr(VtValue(restXforms));
    }

    // -----------------------------------------------------------------------
    // Blend shapes. glTF morph targets become UsdSkelBlendShape prims under
    // /Asset/skel/BlendShapes, bound on the owning mesh. blendPath[meshIdx][t]
    // records each prim path so expressions can reference them below. (UsdSkel
    // blend shapes need a SkelRoot ancestor — present only when there's a
    // skeleton — so morph data without one is preserved in the model but not
    // authored here.)
    // -----------------------------------------------------------------------
    std::vector<std::vector<SdfPath>> blendPath(doc.meshes.size());
    bool anyMorph = false;
    for (const VrmMeshPrimitive& m : doc.meshes) {
        if (!m.morphTargets.empty()) { anyMorph = true; break; }
    }
    if (anyMorph && hasSkel) {
        const SdfPath blendScopePath = skelScopePath.AppendChild(TfToken("BlendShapes"));
        UsdGeomScope::Define(stage, blendScopePath);

        // Uniquify blend-shape names across every (mesh, morph) pair.
        std::vector<std::string> rawNames;
        for (const VrmMeshPrimitive& m : doc.meshes) {
            for (size_t t = 0; t < m.morphTargets.size(); ++t) {
                const std::string& mn = m.morphTargets[t].name;
                rawNames.push_back(m.name + "_" +
                    (mn.empty() ? "morph" + std::to_string(t) : mn));
            }
        }
        std::vector<std::string> blendNames =
            VrmMakeUniqueNames(rawNames, "Morph");

        size_t cursor = 0;
        for (size_t mi = 0; mi < doc.meshes.size(); ++mi) {
            const VrmMeshPrimitive& m = doc.meshes[mi];
            if (m.morphTargets.empty()) continue;
            UsdGeomMesh mesh =
                UsdGeomMesh::Get(stage, geoPath.AppendChild(TfToken(m.name)));
            if (!mesh) { cursor += m.morphTargets.size(); continue; }

            VtTokenArray names;
            SdfPathVector targets;
            for (size_t t = 0; t < m.morphTargets.size(); ++t) {
                const VrmMorphTarget& mt = m.morphTargets[t];
                const TfToken name(blendNames[cursor++]);
                SdfPath bsPath = blendScopePath.AppendChild(name);
                UsdSkelBlendShape bs = UsdSkelBlendShape::Define(stage, bsPath);
                VtVec3fArray offsets(mt.positionDeltas.begin(),
                                     mt.positionDeltas.end());
                // A POSITION-less morph target (normals only) would otherwise
                // author empty offsets while normalOffsets is per-point; UsdSkel
                // requires the two to be length-aligned, so pad offsets with
                // zeros to match.
                if (offsets.empty() && !mt.normalDeltas.empty()) {
                    offsets.assign(mt.normalDeltas.size(), GfVec3f(0.0f));
                }
                bs.CreateOffsetsAttr(VtValue(offsets));
                if (!mt.normalDeltas.empty()) {
                    bs.CreateNormalOffsetsAttr(VtValue(
                        VtVec3fArray(mt.normalDeltas.begin(), mt.normalDeltas.end())));
                }
                names.push_back(name);
                targets.push_back(bsPath);
                blendPath[mi].push_back(bsPath);
            }
            UsdSkelBindingAPI binding = UsdSkelBindingAPI::Apply(mesh.GetPrim());
            binding.CreateBlendShapesAttr(VtValue(names));
            binding.CreateBlendShapeTargetsRel().SetTargets(targets);
            // Blend-shape weights are driven through the bound skeleton's
            // SkelAnimation, so a morph-only (non-skinned) mesh still needs a
            // skel:skeleton relationship or the blend shapes can't be evaluated.
            // Idempotent for skinned meshes that already set it.
            if (!m.skinned) {
                binding.CreateSkeletonRel().SetTargets({skelPath});
            }
        }
    } else if (anyMorph) {
        warn("morph targets present but no skeleton/SkelRoot; blend shapes skipped");
    }

    // -----------------------------------------------------------------------
    // Rig / Humanoid (control semantics; not a bone hierarchy duplicate).
    // -----------------------------------------------------------------------
    UsdGeomScope::Define(stage, rigPath);
    if (!doc.humanoidBones.empty() && hasSkel) {
        SdfPath humanoidPath = rigPath.AppendChild(TfToken("Humanoid"));
        UsdPrim humanoid = UsdGeomScope::Define(stage, humanoidPath).GetPrim();
        // One resolvable relationship to the Skeleton prim; each human bone is a
        // token attribute holding the joint path (a real entry in
        // Skeleton.joints). Relationships can't target joint paths directly —
        // joints are tokens, not prims. (A typed VrmHumanoidAPI may formalize
        // this later.)
        humanoid.CreateRelationship(TfToken("vrm:skeleton"), false)
            .SetTargets({skelPath});
        for (const VrmHumanoidBone& b : doc.humanoidBones) {
            if (b.jointIndex < 0 ||
                b.jointIndex >= static_cast<int>(jointPaths.size())) {
                continue;
            }
            UsdAttribute attr = humanoid.CreateAttribute(
                TfToken("vrm:humanBones:" + b.semanticName),
                SdfValueTypeNames->Token, /*custom=*/true,
                SdfVariabilityUniform);
            attr.Set(TfToken(jointPaths[b.jointIndex]));
        }
    } else if (!doc.humanoidBones.empty()) {
        warn("humanoid bones present but no skeleton was imported; mapping skipped");
    }

    // -----------------------------------------------------------------------
    // Expressions (VRM 1.0 expressions / VRM 0.x BlendShapeGroups). Phase 2
    // authors the morph-target bindings as relationships to the blend-shape
    // prims + weights; evaluation is left to a downstream runtime.
    // -----------------------------------------------------------------------
    if (!doc.expressions.empty()) {
        SdfPath exprScopePath = rigPath.AppendChild(TfToken("Expressions"));
        UsdGeomScope::Define(stage, exprScopePath);

        std::vector<std::string> rawNames;
        rawNames.reserve(doc.expressions.size());
        for (const VrmExpression& e : doc.expressions) rawNames.push_back(e.name);
        std::vector<std::string> exprNames =
            VrmMakeUniqueNames(rawNames, "Expression");

        for (size_t i = 0; i < doc.expressions.size(); ++i) {
            const VrmExpression& e = doc.expressions[i];
            UsdPrim p = UsdGeomScope::Define(
                stage, exprScopePath.AppendChild(TfToken(exprNames[i]))).GetPrim();
            p.CreateAttribute(TfToken("vrm:expressionType"),
                              SdfValueTypeNames->Token, true, SdfVariabilityUniform)
                .Set(TfToken(e.isPreset ? "preset" : "custom"));
            p.CreateAttribute(TfToken("vrm:isBinary"),
                              SdfValueTypeNames->Bool, true, SdfVariabilityUniform)
                .Set(e.isBinary);

            SdfPathVector targets;
            VtFloatArray weights;
            for (const VrmExpression::MorphBind& b : e.morphBinds) {
                if (b.meshPrimitiveIndex < 0 ||
                    b.meshPrimitiveIndex >= static_cast<int>(blendPath.size())) {
                    continue;
                }
                const std::vector<SdfPath>& paths = blendPath[b.meshPrimitiveIndex];
                if (b.morphTargetIndex < 0 ||
                    b.morphTargetIndex >= static_cast<int>(paths.size())) {
                    continue;
                }
                targets.push_back(paths[b.morphTargetIndex]);
                weights.push_back(b.weight);
            }
            if (!targets.empty()) {
                p.CreateRelationship(TfToken("vrm:morphTargets"), false)
                    .SetTargets(targets);
                p.CreateAttribute(TfToken("vrm:morphTargetWeights"),
                                  SdfValueTypeNames->FloatArray, true,
                                  SdfVariabilityUniform)
                    .Set(weights);
            }
        }
    }

    // -----------------------------------------------------------------------
    // LookAt (control semantics). Eyes reference the skeleton joints via token
    // attributes (joints are tokens, not prims); curve/range-map parameters are
    // preserved verbatim in customData for a downstream runtime.
    // -----------------------------------------------------------------------
    if (doc.lookAt.present) {
        UsdPrim lookAt = UsdGeomScope::Define(
            stage, rigPath.AppendChild(TfToken("LookAt"))).GetPrim();
        lookAt.CreateAttribute(TfToken("vrm:type"), SdfValueTypeNames->Token,
                               true, SdfVariabilityUniform)
            .Set(TfToken(doc.lookAt.type));
        if (hasSkel) {
            lookAt.CreateRelationship(TfToken("vrm:skeleton"), false)
                .SetTargets({skelPath});
        }
        auto authorEye = [&](const char* name, int joint) {
            if (joint >= 0 && joint < static_cast<int>(jointPaths.size())) {
                lookAt.CreateAttribute(TfToken(name), SdfValueTypeNames->Token,
                                       true, SdfVariabilityUniform)
                    .Set(TfToken(jointPaths[joint]));
            }
        };
        authorEye("vrm:leftEye", doc.lookAt.leftEyeJoint);
        authorEye("vrm:rightEye", doc.lookAt.rightEyeJoint);
        if (!doc.lookAt.rawJson.empty()) {
            lookAt.SetCustomDataByKey(TfToken("vrm:lookAt:raw"),
                                      VtValue(doc.lookAt.rawJson));
        }
    }

    // -----------------------------------------------------------------------
    // Skeletal animation. Each clip becomes a UsdSkelAnimation under
    // /Asset/skel/Animations; the first is bound to the skeleton so it plays in
    // usdview. Times are authored as timecodes at a fixed 30 fps.
    // -----------------------------------------------------------------------
    if (hasSkel && !doc.animations.empty()) {
        const double fps = 30.0;
        SdfPath animScopePath = skelScopePath.AppendChild(TfToken("Animations"));
        UsdGeomScope::Define(stage, animScopePath);

        double startTc = 0.0, endTc = 0.0;
        bool haveRange = false;
        SdfPath firstClip;
        for (size_t i = 0; i < doc.animations.size(); ++i) {
            const VrmAnimation& a = doc.animations[i];
            if (a.jointIndices.empty() || a.times.empty()) continue;
            // Clip names are already sanitized + uniquified by the reader.
            SdfPath clipPath = animScopePath.AppendChild(TfToken(a.name));
            UsdSkelAnimation anim = UsdSkelAnimation::Define(stage, clipPath);

            VtTokenArray joints;
            joints.reserve(a.jointIndices.size());
            for (int j : a.jointIndices) joints.push_back(TfToken(jointPaths[j]));
            anim.CreateJointsAttr(VtValue(joints));

            UsdAttribute tAttr = anim.CreateTranslationsAttr();
            UsdAttribute rAttr = anim.CreateRotationsAttr();
            UsdAttribute sAttr = anim.CreateScalesAttr();
            for (size_t ti = 0; ti < a.times.size(); ++ti) {
                const double tc = a.times[ti] * fps;
                tAttr.Set(VtVec3fArray(a.translations[ti].begin(),
                                       a.translations[ti].end()), tc);
                rAttr.Set(VtQuatfArray(a.rotations[ti].begin(),
                                       a.rotations[ti].end()), tc);
                VtVec3hArray scales(a.scales[ti].size());
                for (size_t k = 0; k < a.scales[ti].size(); ++k)
                    scales[k] = GfVec3h(a.scales[ti][k]);
                sAttr.Set(scales, tc);
            }
            if (firstClip.IsEmpty()) {
                firstClip = clipPath;
                // Stage time range follows the bound (first) clip only; a.times
                // is sorted ascending by the reader.
                startTc = a.times.front() * fps;
                endTc = a.times.back() * fps;
                haveRange = true;
            }
        }

        if (!firstClip.IsEmpty()) {
            UsdSkelBindingAPI::Apply(stage->GetPrimAtPath(skelPath))
                .CreateAnimationSourceRel()
                .SetTargets({firstClip});
        }
        if (haveRange) {
            stage->SetTimeCodesPerSecond(fps);
            stage->SetFramesPerSecond(fps);
            stage->SetStartTimeCode(startTc);
            stage->SetEndTimeCode(endTc);
        }
    }

    // -----------------------------------------------------------------------
    // Secondary motion (SpringBone) under /Asset/rig/SecondaryMotion. Data
    // only (no simulation): spring chains with per-joint parameters + collider
    // groups, joints referenced by token (skeleton joint path where resolvable).
    // -----------------------------------------------------------------------
    const VrmSecondaryMotion& sm = doc.secondaryMotion;
    if (sm.present && (!sm.springs.empty() || !sm.colliders.empty())) {
        SdfPath smPath = rigPath.AppendChild(TfToken("SecondaryMotion"));
        UsdPrim smPrim = UsdGeomScope::Define(stage, smPath).GetPrim();
        if (!sm.rawJson.empty())
            smPrim.SetCustomDataByKey(TfToken("vrm:springBone:raw"),
                                      VtValue(sm.rawJson));

        auto jointTok = [&](int jointIndex, const std::string& srcName,
                            int srcIdx) -> TfToken {
            if (jointIndex >= 0 && jointIndex < static_cast<int>(jointPaths.size()))
                return TfToken(jointPaths[jointIndex]);
            if (!srcName.empty()) return TfToken(srcName);
            return TfToken("node_" + std::to_string(srcIdx));
        };

        // Collider groups (with their colliders as child prims).
        SdfPath colScopePath = smPath.AppendChild(TfToken("Colliders"));
        UsdGeomScope::Define(stage, colScopePath);
        std::vector<std::string> rawGrp;
        rawGrp.reserve(sm.colliderGroups.size());
        for (const VrmColliderGroup& g : sm.colliderGroups) rawGrp.push_back(g.name);
        std::vector<std::string> grpNames =
            VrmMakeUniqueNames(rawGrp, "ColliderGroup");
        std::vector<SdfPath> grpPaths(sm.colliderGroups.size());
        for (size_t gi = 0; gi < sm.colliderGroups.size(); ++gi) {
            SdfPath gp = colScopePath.AppendChild(TfToken(grpNames[gi]));
            UsdGeomScope::Define(stage, gp);
            grpPaths[gi] = gp;
            int ci = 0;
            for (int idx : sm.colliderGroups[gi].colliderIndices) {
                if (idx < 0 || idx >= static_cast<int>(sm.colliders.size())) continue;
                const VrmCollider& c = sm.colliders[idx];
                UsdPrim cp = UsdGeomScope::Define(
                    stage, gp.AppendChild(
                        TfToken("Collider_" + std::to_string(ci++)))).GetPrim();
                cp.CreateAttribute(TfToken("vrm:shape"), SdfValueTypeNames->Token,
                                   true, SdfVariabilityUniform)
                    .Set(TfToken(c.shape.empty() ? "sphere" : c.shape));
                cp.CreateAttribute(TfToken("vrm:node"), SdfValueTypeNames->Token,
                                   true, SdfVariabilityUniform)
                    .Set(jointTok(c.jointIndex, c.sourceNodeName, c.sourceNodeIndex));
                cp.CreateAttribute(TfToken("vrm:offset"), SdfValueTypeNames->Float3,
                                   true, SdfVariabilityUniform).Set(c.offset);
                cp.CreateAttribute(TfToken("vrm:radius"), SdfValueTypeNames->Float,
                                   true, SdfVariabilityUniform).Set(c.radius);
                if (c.shape == "capsule")
                    cp.CreateAttribute(TfToken("vrm:tail"), SdfValueTypeNames->Float3,
                                       true, SdfVariabilityUniform).Set(c.tail);
            }
        }

        // Spring chains.
        SdfPath sbScopePath = smPath.AppendChild(TfToken("SpringBones"));
        UsdGeomScope::Define(stage, sbScopePath);
        std::vector<std::string> rawSp;
        rawSp.reserve(sm.springs.size());
        for (const VrmSpring& s : sm.springs) rawSp.push_back(s.name);
        std::vector<std::string> spNames = VrmMakeUniqueNames(rawSp, "Spring");
        for (size_t si = 0; si < sm.springs.size(); ++si) {
            const VrmSpring& s = sm.springs[si];
            UsdPrim sp = UsdGeomScope::Define(
                stage, sbScopePath.AppendChild(TfToken(spNames[si]))).GetPrim();

            VtTokenArray jtoks;
            VtFloatArray stiff, gpow, drag, hit;
            VtVec3fArray gdir;
            for (const VrmSpringJoint& j : s.joints) {
                jtoks.push_back(jointTok(j.jointIndex, j.sourceNodeName,
                                         j.sourceNodeIndex));
                stiff.push_back(j.stiffness);
                gpow.push_back(j.gravityPower);
                drag.push_back(j.dragForce);
                hit.push_back(j.hitRadius);
                gdir.push_back(j.gravityDir);
            }
            auto arr = [&](const char* n, const SdfValueTypeName& t,
                           const VtValue& v) {
                sp.CreateAttribute(TfToken(n), t, true, SdfVariabilityUniform).Set(v);
            };
            arr("vrm:joints", SdfValueTypeNames->TokenArray, VtValue(jtoks));
            arr("vrm:stiffness", SdfValueTypeNames->FloatArray, VtValue(stiff));
            arr("vrm:gravityPower", SdfValueTypeNames->FloatArray, VtValue(gpow));
            arr("vrm:dragForce", SdfValueTypeNames->FloatArray, VtValue(drag));
            arr("vrm:hitRadius", SdfValueTypeNames->FloatArray, VtValue(hit));
            arr("vrm:gravityDir", SdfValueTypeNames->Float3Array, VtValue(gdir));
            if (s.centerJoint >= 0 &&
                s.centerJoint < static_cast<int>(jointPaths.size())) {
                sp.CreateAttribute(TfToken("vrm:center"), SdfValueTypeNames->Token,
                                   true, SdfVariabilityUniform)
                    .Set(TfToken(jointPaths[s.centerJoint]));
            }
            SdfPathVector cgTargets;
            for (int gi : s.colliderGroupIndices)
                if (gi >= 0 && gi < static_cast<int>(grpPaths.size()))
                    cgTargets.push_back(grpPaths[gi]);
            if (!cgTargets.empty())
                sp.CreateRelationship(TfToken("vrm:colliderGroups"), false)
                    .SetTargets(cgTargets);
        }
    }

    if (!stage->ExportToString(outUsda)) {
        return false;
    }
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
