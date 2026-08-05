// SPDX-License-Identifier: Apache-2.0
#include "ClipWriter.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/vec3d.h"
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

#include <cstddef>
#include <vector>

namespace motionBvhTool
{

bool
WriteSemanticClip(const std::string& outputPath,
                  const motion::HumanoidAnimation& animation,
                  const motionSource::CanonicalRestPose& rest,
                  const std::string& clipName,
                  const std::map<std::string, std::string>& provenance,
                  std::string* error)
{
    if (animation.samples.empty()) {
        *error = "the conversion produced no frames";
        return false;
    }
    // Checked before any work: a bad prim name is an argument error, and
    // discovering it after authoring the joint set only obscures that.
    if (!pxr::TfIsValidIdentifier(clipName)) {
        *error = "'" + clipName + "' is not a valid prim name";
        return false;
    }
    if (!rest.present.any()) {
        *error = "the conversion bound no humanoid bone";
        return false;
    }
    // Guaranteed by `ValidateSourceProfile`, which requires every profile to
    // bind the hips and to mark that mapping required -- so a matched profile
    // cannot reach here without one. Checked anyway rather than assumed,
    // because the alternative is authoring body translation onto a joint that
    // is not in the joint set, which USD would accept and no reader would
    // notice.
    const auto hips = static_cast<std::size_t>(motion::HumanBone::Hips);
    if (!rest.present.test(hips)) {
        *error = "the profile bound no hips, so the clip has nowhere to carry "
                 "body translation";
        return false;
    }

    std::vector<motion::HumanBone> bones;
    pxr::VtTokenArray joints;
    bones.reserve(rest.present.count());
    joints.reserve(rest.present.count());
    for (std::size_t index = 0; index < motion::HumanBoneCount; ++index) {
        if (!rest.present.test(index)) {
            continue;
        }
        const auto bone = static_cast<motion::HumanBone>(index);
        bones.push_back(bone);
        joints.push_back(
            pxr::TfToken(motion::HumanBoneJointPath(bone, rest.present)));
    }

    const double frameRate =
        animation.nominalFrameRate > 0.0 ? animation.nominalFrameRate : 30.0;

    // Re-converting over a previous output is the normal case, so clear an
    // existing layer instead of failing the way `UsdStage::CreateNew` would.
    // There is no input stage to guard against: this tool's input is a `.bvh`
    // file and a profile, neither of which is a USD layer.
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
    // Canonical, not the source's: the basis change already happened, and a
    // stage restating the file's centimetres would undo it downstream.
    pxr::UsdGeomSetStageUpAxis(stage, pxr::UsdGeomTokens->y);
    pxr::UsdGeomSetStageMetersPerUnit(stage,
                                      motionSource::CanonicalUnitInMeters);
    stage->SetTimeCodesPerSecond(frameRate);
    stage->SetFramesPerSecond(frameRate);
    stage->SetStartTimeCode(animation.startTime * frameRate);
    stage->SetEndTimeCode(animation.endTime * frameRate);

    const pxr::SdfPath rootPath("/Source");
    const pxr::UsdPrim root =
        pxr::UsdGeomScope::Define(stage, rootPath).GetPrim();
    stage->SetDefaultPrim(root);
    for (const auto& entry : provenance) {
        root.SetCustomDataByKey(pxr::TfToken("source:" + entry.first),
                                pxr::VtValue(entry.second));
    }

    // The rest pose the converter built, joint for joint. This is the whole of
    // what separates this writer from `motion_capture`'s: the file states a
    // rest and the profile says how to read it, so authoring identity here
    // would tell `vrmRetarget` that the source rig stands exactly as the target
    // does and silently skip the correction §4 exists for.
    const pxr::SdfPath skeletonPath =
        rootPath.AppendChild(pxr::TfToken("HumanoidSkeleton"));
    const pxr::UsdSkelSkeleton skeleton =
        pxr::UsdSkelSkeleton::Define(stage, skeletonPath);
    pxr::VtMatrix4dArray restTransforms;
    restTransforms.reserve(bones.size());
    for (const motion::HumanBone bone : bones) {
        const auto slot = static_cast<std::size_t>(bone);
        const pxr::GfVec3f& translation = rest.localTranslations[slot];
        restTransforms.push_back(pxr::GfMatrix4d(
            pxr::GfRotation(pxr::GfQuatd(rest.localRotations[slot])),
            pxr::GfVec3d(translation[0], translation[1], translation[2])));
    }
    skeleton.CreateJointsAttr(pxr::VtValue(joints));
    skeleton.CreateRestTransformsAttr(pxr::VtValue(restTransforms));

    const pxr::SdfPath clipPath = rootPath.AppendChild(pxr::TfToken(clipName));
    const pxr::UsdSkelAnimation clip =
        pxr::UsdSkelAnimation::Define(stage, clipPath);
    clip.CreateJointsAttr(pxr::VtValue(joints));
    pxr::UsdAttribute translations = clip.CreateTranslationsAttr();
    pxr::UsdAttribute rotations = clip.CreateRotationsAttr();
    // See ClipWriter.h: `scales` has no schema fallback, and a clip without it
    // resolves to the rest pose rather than to unscaled motion.
    const pxr::VtVec3hArray identityScales(bones.size(), pxr::GfVec3h(1.0f));
    clip.CreateScalesAttr(pxr::VtValue(identityScales));

    for (const motion::HumanoidPose& pose : animation.samples) {
        pxr::VtVec3fArray valuesT;
        pxr::VtQuatfArray valuesR;
        valuesT.reserve(bones.size());
        valuesR.reserve(bones.size());
        for (const motion::HumanBone bone : bones) {
            const auto slot = static_cast<std::size_t>(bone);
            // Every joint holds its rest translation, and the hips carry body
            // motion over theirs. Canonical motion puts body translation on the
            // root alone (MOTION_CONTRACT.md), so authoring zero for the others
            // would not mean "unmoving" -- it would collapse each bone onto its
            // parent, in a clip whose own skeleton says otherwise.
            pxr::GfVec3f translation = rest.localTranslations[slot];
            if (bone == motion::HumanBone::Hips && pose.root.hasPosition) {
                translation = pose.root.worldPosition;
            }
            valuesT.push_back(translation);
            // A bone no frame rotated authors its **rest** rotation rather than
            // identity. The two are the same thing for `motion_capture`, whose
            // rest is identity, and they are not here: a rig whose profile maps
            // a `CHANNELS 0` joint states a rest orientation for it and no
            // motion, and identity would move it.
            valuesR.push_back(pose.validRotations.test(slot)
                                  ? pose.localRotations[slot]
                                  : rest.localRotations[slot]);
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

} // namespace motionBvhTool
