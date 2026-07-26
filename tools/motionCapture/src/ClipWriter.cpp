// SPDX-License-Identifier: Apache-2.0
#include "ClipWriter.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/value.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdSkel/animation.h"
#include "pxr/usd/usdSkel/bindingAPI.h"
#include "pxr/usd/usdSkel/skeleton.h"

#include <bitset>
#include <cstddef>
#include <vector>

namespace motionCaptureTool
{

bool
WriteSemanticClip(const std::string& outputPath,
                  const motion::HumanoidAnimation& animation,
                  const std::string& clipName,
                  const std::map<std::string, std::string>& provenance,
                  std::string* error)
{
    if (animation.samples.empty()) {
        *error = "the recorded session produced no frames";
        return false;
    }

    // A joint exists in the clip when any frame observed it. A bone the rig
    // never solved is simply absent -- it is not authored at rest, because a
    // joint that is present and unmoving means something different downstream
    // from a joint that was never captured.
    std::bitset<motion::HumanBoneCount> present;
    for (const motion::HumanoidPose& pose : animation.samples) {
        present |= pose.validRotations;
    }
    if (!present.any()) {
        *error = "the recorded session observed no humanoid bone";
        return false;
    }

    std::vector<motion::HumanBone> bones;
    pxr::VtTokenArray joints;
    for (std::size_t index = 0; index < motion::HumanBoneCount; ++index) {
        if (!present.test(index)) {
            continue;
        }
        const auto bone = static_cast<motion::HumanBone>(index);
        bones.push_back(bone);
        joints.push_back(
            pxr::TfToken(motion::HumanBoneJointPath(bone, present)));
    }

    if (!pxr::TfIsValidIdentifier(clipName)) {
        *error = "'" + clipName + "' is not a valid prim name";
        return false;
    }

    const double frameRate =
        animation.nominalFrameRate > 0.0 ? animation.nominalFrameRate : 30.0;

    // Re-running a session over a previous output is the normal case, so clear
    // an existing layer instead of failing the way UsdStage::CreateNew would.
    // Unlike motion_retarget there is no input stage to guard against here:
    // this tool's input is a text trace, not a USD layer.
    pxr::SdfLayerRefPtr layer = pxr::SdfLayer::FindOrOpen(outputPath);
    if (layer) {
        layer->Clear();
    } else {
        layer = pxr::SdfLayer::CreateNew(outputPath);
    }
    if (!layer) {
        *error = "could not create output layer: " + outputPath;
        return false;
    }
    const pxr::UsdStageRefPtr stage = pxr::UsdStage::Open(layer);
    if (!stage) {
        *error = "could not open output layer as a stage: " + outputPath;
        return false;
    }
    pxr::UsdGeomSetStageUpAxis(stage, pxr::UsdGeomTokens->y);
    pxr::UsdGeomSetStageMetersPerUnit(stage, 1.0);
    stage->SetTimeCodesPerSecond(frameRate);
    stage->SetFramesPerSecond(frameRate);
    stage->SetStartTimeCode(animation.startTime * frameRate);
    stage->SetEndTimeCode(animation.endTime * frameRate);

    const pxr::SdfPath rootPath("/Capture");
    const pxr::UsdPrim root =
        pxr::UsdGeomScope::Define(stage, rootPath).GetPrim();
    stage->SetDefaultPrim(root);
    for (const auto& entry : provenance) {
        root.SetCustomDataByKey(pxr::TfToken("capture:" + entry.first),
                                pxr::VtValue(entry.second));
    }

    // The trace carries no rest pose -- a capture stream reports rotations
    // relative to the humanoid rest, not the rest itself. Authoring identity
    // rests makes the retargeter's rest-pose correction a no-op, which is the
    // honest reading. The one exception is the hips translation: it is seeded
    // with the session's first observed root position, so root motion arrives
    // downstream as a delta from where the capture started rather than as an
    // absolute height (see MOTION_CONTRACT.md).
    pxr::GfVec3f hipsRest(0.0f);
    for (const motion::HumanoidPose& pose : animation.samples) {
        if (pose.root.hasPosition) {
            hipsRest = pose.root.worldPosition;
            break;
        }
    }

    const pxr::SdfPath skeletonPath =
        rootPath.AppendChild(pxr::TfToken("HumanoidSkeleton"));
    const pxr::UsdSkelSkeleton skeleton =
        pxr::UsdSkelSkeleton::Define(stage, skeletonPath);
    pxr::VtMatrix4dArray restTransforms;
    restTransforms.reserve(bones.size());
    for (const motion::HumanBone bone : bones) {
        pxr::GfMatrix4d rest(1.0);
        if (bone == motion::HumanBone::Hips) {
            rest.SetTranslate(pxr::GfVec3d(hipsRest[0], hipsRest[1],
                                           hipsRest[2]));
        }
        restTransforms.push_back(rest);
    }
    skeleton.CreateJointsAttr(pxr::VtValue(joints));
    skeleton.CreateRestTransformsAttr(pxr::VtValue(restTransforms));

    const pxr::SdfPath clipPath = rootPath.AppendChild(pxr::TfToken(clipName));
    const pxr::UsdSkelAnimation clip =
        pxr::UsdSkelAnimation::Define(stage, clipPath);
    clip.CreateJointsAttr(pxr::VtValue(joints));
    pxr::UsdAttribute translations = clip.CreateTranslationsAttr();
    pxr::UsdAttribute rotations = clip.CreateRotationsAttr();
    // UsdSkel fetches translations, rotations and scales as a unit, and
    // `scales` has no schema fallback: omitting it does not mean "this clip
    // animates no scale", it silently drops the whole animation and leaves the
    // skeleton at its rest pose. Scale stays un-animated -- this constant
    // identity array exists only so the clip evaluates.
    const pxr::VtVec3hArray identityScales(bones.size(), pxr::GfVec3h(1.0f));
    clip.CreateScalesAttr(pxr::VtValue(identityScales));

    for (const motion::HumanoidPose& pose : animation.samples) {
        pxr::VtVec3fArray valuesT;
        pxr::VtQuatfArray valuesR;
        valuesT.reserve(bones.size());
        valuesR.reserve(bones.size());
        for (const motion::HumanBone bone : bones) {
            const auto slot = static_cast<std::size_t>(bone);
            pxr::GfVec3f translation(0.0f);
            if (bone == motion::HumanBone::Hips) {
                translation =
                    pose.root.hasPosition ? pose.root.worldPosition : hipsRest;
            }
            valuesT.push_back(translation);
            // A frame that did not observe a bone authors the rest rotation
            // rather than the previous frame's: holding is an intake policy
            // (LiveCaptureConfig::missingBones), and re-deciding it here would
            // hide which policy actually ran.
            valuesR.push_back(pose.validRotations.test(slot)
                                  ? pose.localRotations[slot]
                                  : pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f)));
        }
        const double timeCode = pose.timestamp * frameRate;
        translations.Set(valuesT, timeCode);
        rotations.Set(valuesR, timeCode);
    }

    pxr::UsdSkelBindingAPI::Apply(skeleton.GetPrim())
        .CreateAnimationSourceRel()
        .SetTargets({clipPath});

    if (!stage->GetRootLayer()->Save()) {
        *error = "could not save output layer: " + outputPath;
        return false;
    }
    return true;
}

} // namespace motionCaptureTool
