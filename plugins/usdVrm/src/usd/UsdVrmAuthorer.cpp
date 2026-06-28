// SPDX-License-Identifier: Apache-2.0
#include "usd/UsdVrmAuthorer.h"

#include "util/PathUtil.h"

#include "pxr/base/gf/range3f.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/kind/registry.h"
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
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/materialBindingAPI.h"
#include "pxr/usd/usdShade/shader.h"
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
        shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f)
            .Set(vm.baseColor);
        shader.CreateInput(TfToken("emissiveColor"), SdfValueTypeNames->Color3f)
            .Set(vm.emissiveColor);
        shader.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float)
            .Set(vm.metallic);
        shader.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float)
            .Set(vm.roughness);
        shader.CreateInput(TfToken("opacity"), SdfValueTypeNames->Float)
            .Set(vm.opacity);
        if (vm.alphaMode == "MASK") {
            shader.CreateInput(TfToken("opacityThreshold"), SdfValueTypeNames->Float)
                .Set(vm.alphaCutoff);
        }
        UsdShadeOutput surfaceOut =
            shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);
        mat.CreateSurfaceOutput().ConnectToSource(surfaceOut);

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
            mesh.CreateNormalsAttr(
                VtValue(VtVec3fArray(m.normals.begin(), m.normals.end())));
            mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
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

    if (!stage->ExportToString(outUsda)) {
        return false;
    }
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
