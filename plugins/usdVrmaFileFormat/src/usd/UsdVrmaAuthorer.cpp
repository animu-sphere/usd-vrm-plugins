// SPDX-License-Identifier: Apache-2.0
#include "usd/UsdVrmaAuthorer.h"

#include "pxr/base/gf/vec3h.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/value.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/valueTypeName.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdSkel/animation.h"
#include "pxr/usd/usdSkel/bindingAPI.h"
#include "pxr/usd/usdSkel/skeleton.h"

#include <set>
#include <string>

PXR_NAMESPACE_OPEN_SCOPE

bool
UsdVrmaAuthorer::WriteToString(const VrmaCanonicalDocument& document,
                               std::string* outUsda) const
{
    if (!outUsda || document.joints.empty() || document.animation.samples.empty()) {
        return false;
    }
    const UsdStageRefPtr stage = UsdStage::CreateInMemory();
    if (!stage) return false;

    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y);
    UsdGeomSetStageMetersPerUnit(stage, 1.0);
    stage->SetTimeCodesPerSecond(document.animation.nominalFrameRate);
    stage->SetFramesPerSecond(document.animation.nominalFrameRate);
    stage->SetStartTimeCode(document.animation.startTime *
                             document.animation.nominalFrameRate);
    stage->SetEndTimeCode(document.animation.endTime *
                           document.animation.nominalFrameRate);

    const SdfPath animationPath("/Animation");
    UsdPrim root = UsdGeomScope::Define(stage, animationPath).GetPrim();
    stage->SetDefaultPrim(root);
    root.SetCustomDataByKey(TfToken("vrma:sourceFormat"), VtValue(std::string("VRMA")));
    root.SetCustomDataByKey(TfToken("vrma:specVersion"), VtValue(document.specVersion));
    root.SetCustomDataByKey(TfToken("vrma:rawExtension"), VtValue(document.rawExtensionJson));
    root.SetCustomDataByKey(TfToken("vrma:rootMotionSource"),
                            VtValue(std::string("hipsTranslation")));

    const SdfPath skeletonPath = animationPath.AppendChild(TfToken("HumanoidSkeleton"));
    const UsdSkelSkeleton skeleton = UsdSkelSkeleton::Define(stage, skeletonPath);
    VtTokenArray joints;
    VtMatrix4dArray restTransforms;
    joints.reserve(document.joints.size());
    restTransforms.reserve(document.joints.size());
    for (const VrmaJoint& joint : document.joints) {
        joints.push_back(TfToken(joint.path));
        restTransforms.push_back(joint.restTransform);
    }
    skeleton.CreateJointsAttr(VtValue(joints));
    skeleton.CreateRestTransformsAttr(VtValue(restTransforms));

    const SdfPath bodyPath = animationPath.AppendChild(TfToken("BodyAnimation"));
    const UsdSkelAnimation body = UsdSkelAnimation::Define(stage, bodyPath);
    body.CreateJointsAttr(VtValue(joints));
    UsdAttribute translations = body.CreateTranslationsAttr();
    UsdAttribute rotations = body.CreateRotationsAttr();
    // UsdSkel fetches translations, rotations and scales as a unit, and
    // `scales` has no schema fallback: omitting it does not mean "this clip
    // animates no scale", it silently drops the whole animation and leaves the
    // skeleton at its rest pose. Scale stays un-animated -- this constant
    // identity array exists only so the clip evaluates.
    const VtVec3hArray identityScales(document.joints.size(), GfVec3h(1.0f));
    body.CreateScalesAttr(VtValue(identityScales));
    for (const motion::HumanoidPose& pose : document.animation.samples) {
        VtVec3fArray valuesT;
        VtQuatfArray valuesR;
        valuesT.reserve(document.joints.size());
        valuesR.reserve(document.joints.size());
        for (const VrmaJoint& joint : document.joints) {
            GfVec3f translation = joint.restTranslation;
            if (joint.bone == motion::HumanBone::Hips && pose.root.hasPosition) {
                translation = pose.root.worldPosition;
            }
            valuesT.push_back(translation);
            valuesR.push_back(pose.localRotations[
                static_cast<std::size_t>(joint.bone)]);
        }
        const double timeCode = pose.timestamp * document.animation.nominalFrameRate;
        translations.Set(valuesT, timeCode);
        rotations.Set(valuesR, timeCode);
    }
    UsdSkelBindingAPI::Apply(skeleton.GetPrim())
        .CreateAnimationSourceRel()
        .SetTargets({bodyPath});

    // Expressions are named weights over time and nothing more here. A VRM
    // expression drives N morph targets across M meshes plus material colours,
    // and which ones is a property of the *avatar*, which a clip that binds to
    // no avatar cannot know -- so this authors no `blendShapes` binding and
    // expands nothing (motion policy §4.3).
    //
    // One prim per expression, laid out like the importer's
    // `/Asset/rig/Expressions/<name>`. The two are **not** joinable by path: the
    // importer sanitizes a name through its own private table and this bundle
    // cannot link it, so any name outside ASCII lands on a different prim name
    // on each side. `vrm:expressionName` is the key that survives that, and the
    // avatar side authors it too since 2026-09-01 -- so `ExpressionResolve`
    // joins on this attribute and never on a prim name.
    if (!document.expressions.empty()) {
        const SdfPath expressionsPath =
            animationPath.AppendChild(TfToken("Expressions"));
        UsdGeomScope::Define(stage, expressionsPath);
        std::set<std::string> claimedNames;
        for (const VrmaExpression& expression : document.expressions) {
            // USD prim names are identifiers and expression names are not: the
            // preset vocabulary happens to be safe, and a custom name is
            // whatever an author typed. `vrm:expressionName` carries the name
            // the file used, so sanitizing here loses nothing.
            const std::string base = TfMakeValidIdentifier(expression.name);
            // Uniquify against the names already *claimed*, not against the
            // bases seen. `<base>_2` can be another expression's own sanitized
            // base, and `UsdGeomScope::Define` on a path that already exists
            // returns the existing prim instead of failing -- so counting bases
            // lets two declared expressions collapse into one, the second
            // silently overwriting the first's weights.
            std::string primName = base;
            for (int suffix = 2; !claimedNames.insert(primName).second; ++suffix) {
                primName = base + "_" + std::to_string(suffix);
            }

            const UsdPrim prim = UsdGeomScope::Define(
                stage, expressionsPath.AppendChild(TfToken(primName))).GetPrim();
            prim.CreateAttribute(TfToken("vrm:expressionName"),
                                 SdfValueTypeNames->Token, false,
                                 SdfVariabilityUniform)
                .Set(TfToken(expression.name));
            prim.CreateAttribute(TfToken("vrm:expressionType"),
                                 SdfValueTypeNames->Token, false,
                                 SdfVariabilityUniform)
                .Set(TfToken(expression.isPreset ? "preset" : "custom"));

            if (expression.constantWeight) {
                // The node stated a weight and nothing animates it. That is one
                // value for the whole clip, so it is authored as a default --
                // a run of identical time samples would claim the file keyed
                // something it did not.
                prim.CreateAttribute(TfToken("vrm:expressionWeight"),
                                     SdfValueTypeNames->Float, false)
                    .Set(*expression.constantWeight);
                continue;
            }
            if (!expression.isAnimated) {
                // Declared, and the file gave no weight anywhere: no channel and
                // no transform on the node to read one out of. The attribute
                // stays unauthored rather than being written as zero, because an
                // unreported weight is not a weight of zero (MOTION_CONTRACT.md)
                // and a zero here would be this importer saying what the file
                // did not.
                continue;
            }
            UsdAttribute weight = prim.CreateAttribute(
                TfToken("vrm:expressionWeight"), SdfValueTypeNames->Float, false);
            for (const motion::HumanoidPose& pose : document.animation.samples) {
                if (const float* value = pose.expressions.Find(expression.name)) {
                    weight.Set(*value,
                               pose.timestamp * document.animation.nominalFrameRate);
                }
            }
        }
    }

    // Look-at is one target over time and the offset the source rig measured,
    // and nothing here is applied to a pair of eyes: which joints a gaze moves,
    // and through what curve, is the *avatar's* look-at configuration, and this
    // clip binds to no avatar (motion policy 4.3). `LookAtEvaluate` is the layer
    // that owns that, exactly as `ExpressionResolve` owns expanding a weight.
    //
    // Namespaced attributes on a plain prim, like the expression half: this is
    // what a `VrmAnimationLookAtAPI` would carry anyway, so applying one later
    // moves no path and renames no attribute (motion policy 4.1).
    if (document.lookAt.present) {
        const UsdPrim prim = UsdGeomScope::Define(
            stage, animationPath.AppendChild(TfToken("LookAt"))).GetPrim();
        prim.CreateAttribute(TfToken("vrm:lookAtOffsetFromHeadBone"),
                             SdfValueTypeNames->Float3, false,
                             SdfVariabilityUniform)
            .Set(document.lookAt.offsetFromHeadBone);

        // A point rather than a vector: it is a place in the clip's space, so
        // it translates with whatever the clip is placed into, and `point3f`
        // is what says so to every consumer that asks the type.
        if (document.lookAt.constantTarget) {
            // Stated once by the node and never animated -- authored as a
            // default, because a run of identical time samples would claim the
            // file keyed something it did not.
            prim.CreateAttribute(TfToken("vrm:lookAtTarget"),
                                 SdfValueTypeNames->Point3f, false)
                .Set(*document.lookAt.constantTarget);
        } else if (document.lookAt.isAnimated) {
            UsdAttribute target = prim.CreateAttribute(
                TfToken("vrm:lookAtTarget"), SdfValueTypeNames->Point3f, false);
            for (const motion::HumanoidPose& pose : document.animation.samples) {
                if (pose.lookAtTarget) {
                    target.Set(*pose.lookAtTarget,
                               pose.timestamp * document.animation.nominalFrameRate);
                }
            }
        }
        // Declared with no channel and no transform to read a position out of:
        // the attribute stays unauthored rather than being written at the
        // origin. A gaze the file never gave is not a gaze at (0, 0, 0), the
        // same distinction an unreported expression weight is under.
    }

    return stage->GetRootLayer()->ExportToString(outUsda);
}

PXR_NAMESPACE_CLOSE_SCOPE
