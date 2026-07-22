// SPDX-License-Identifier: Apache-2.0
#include "usd/UsdVrmaAuthorer.h"

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

    return stage->GetRootLayer()->ExportToString(outUsda);
}

PXR_NAMESPACE_CLOSE_SCOPE
