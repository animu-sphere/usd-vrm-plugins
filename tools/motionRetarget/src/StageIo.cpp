// SPDX-License-Identifier: Apache-2.0
#include "StageIo.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/types.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/references.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdSkel/animation.h"
#include "pxr/usd/usdSkel/bindingAPI.h"
#include "pxr/usd/usdSkel/skeleton.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE

namespace motionRetargetTool
{
namespace
{

const char* const kHumanBonesPrefix = "vrm:humanBones:";

// The semantic joint tokens the motion contract defines are paths whose leaf is
// the human bone name ("hips", "hips/spine", "hips/spine/chest"). Reading the
// leaf back is the documented inverse, not a name heuristic.
std::string
LeafToken(const std::string& jointPath)
{
    const std::size_t separator = jointPath.rfind('/');
    return separator == std::string::npos ? jointPath
                                          : jointPath.substr(separator + 1);
}

GfQuatf
ToQuatf(const GfQuatd& q)
{
    return GfQuatf(static_cast<float>(q.GetReal()),
                   GfVec3f(static_cast<float>(q.GetImaginary()[0]),
                           static_cast<float>(q.GetImaginary()[1]),
                           static_cast<float>(q.GetImaginary()[2])));
}

GfVec3f
ToVec3f(const GfVec3d& v)
{
    return GfVec3f(static_cast<float>(v[0]), static_cast<float>(v[1]),
                   static_cast<float>(v[2]));
}

// Finds the skeleton to work with: the override when given, otherwise the first
// UsdSkelSkeleton in stage order. Reporting "which one" back to the caller is
// what makes --skeleton actionable when a stage carries several.
bool
FindSkeleton(const UsdStageRefPtr& stage, const std::string& override_,
             const char* what, UsdSkelSkeleton* skeleton, std::string* error)
{
    if (!override_.empty()) {
        if (!SdfPath::IsValidPathString(override_)) {
            *error = std::string("not a valid prim path: ") + override_;
            return false;
        }
        const UsdPrim prim = stage->GetPrimAtPath(SdfPath(override_));
        if (!prim) {
            *error = std::string("no prim at ") + override_;
            return false;
        }
        *skeleton = UsdSkelSkeleton(prim);
        if (!*skeleton) {
            *error = override_ + " is not a UsdSkelSkeleton";
            return false;
        }
        return true;
    }

    std::vector<SdfPath> found;
    for (const UsdPrim& prim : stage->Traverse()) {
        if (prim.IsA<UsdSkelSkeleton>()) {
            found.push_back(prim.GetPath());
        }
    }
    if (found.empty()) {
        *error = std::string("the ") + what + " stage has no UsdSkelSkeleton";
        return false;
    }
    *skeleton = UsdSkelSkeleton(stage->GetPrimAtPath(found.front()));
    return true;
}

// Decomposes a rest transform. Scale and shear are dropped on purpose: the
// motion contract ignores scale channels, so carrying them here would author
// something the retargeter never animates.
void
DecomposeRest(const GfMatrix4d& matrix, GfQuatf* rotation, GfVec3f* translation)
{
    *translation = ToVec3f(matrix.ExtractTranslation());
    *rotation = ToQuatf(matrix.RemoveScaleShear().ExtractRotationQuat())
                    .GetNormalized();
}

bool
ReadSkeletonRest(const UsdSkelSkeleton& skeleton, VtTokenArray* joints,
                 VtMatrix4dArray* restTransforms, std::vector<std::string>* warnings)
{
    if (!skeleton.GetJointsAttr().Get(joints) || joints->empty()) {
        return false;
    }
    if (!skeleton.GetRestTransformsAttr().Get(restTransforms)
        || restTransforms->size() != joints->size()) {
        // bindTransforms are world-space, so they cannot substitute for local
        // rest transforms without the topology walk UsdSkel already provides
        // elsewhere. Fall back to identity and say so rather than guess.
        warnings->push_back(
            "skeleton <" + skeleton.GetPath().GetString()
            + "> has no usable restTransforms; assuming identity rest pose");
        restTransforms->assign(joints->size(), GfMatrix4d(1.0));
    }
    return true;
}

} // namespace

bool
ReadHumanoidMapFile(const std::string& path,
                    std::map<std::string, std::string>* entries,
                    std::string* error)
{
    std::ifstream file(path);
    if (!file) {
        *error = "could not open humanoid map file: " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    JsParseError parseError;
    const JsValue parsed = JsParseString(buffer.str(), &parseError);
    if (parsed.IsNull()) {
        *error = "could not parse " + path + ": " + parseError.reason + " (line "
            + std::to_string(parseError.line) + ")";
        return false;
    }
    if (!parsed.IsObject()) {
        *error = path + " must contain a JSON object of humanBone -> joint token";
        return false;
    }

    for (const auto& entry : parsed.GetJsObject()) {
        if (!entry.second.IsString()) {
            *error = path + ": value for '" + entry.first + "' is not a string";
            return false;
        }
        if (!motion::FindHumanBone(entry.first)) {
            *error = path + ": '" + entry.first
                + "' is not a VRM human bone name";
            return false;
        }
        (*entries)[entry.first] = entry.second.GetString();
    }
    return true;
}

bool
ReadAvatar(const std::string& path, const std::string& skeletonPathOverride,
           const std::map<std::string, std::string>& extraMappings,
           Avatar* avatar, std::string* error)
{
    avatar->stage = UsdStage::Open(path);
    if (!avatar->stage) {
        *error = "could not open avatar stage: " + path;
        return false;
    }

    const UsdPrim defaultPrim = avatar->stage->GetDefaultPrim();
    if (!defaultPrim) {
        *error = "avatar stage " + path
            + " has no defaultPrim; the output layer references it by name";
        return false;
    }
    avatar->defaultPrimPath = defaultPrim.GetPath();

    // A VrmHumanoidAPI prim points at its own skeleton, so prefer that over
    // "first skeleton on the stage" when the avatar carries one.
    UsdPrim humanoidPrim;
    for (const UsdPrim& prim : avatar->stage->Traverse()) {
        if (prim.HasAttribute(TfToken(std::string(kHumanBonesPrefix) + "hips"))) {
            humanoidPrim = prim;
            break;
        }
    }

    std::string skeletonOverride = skeletonPathOverride;
    if (skeletonOverride.empty() && humanoidPrim) {
        const UsdRelationship skeletonRel =
            humanoidPrim.GetRelationship(TfToken("vrm:skeleton"));
        SdfPathVector targets;
        if (skeletonRel && skeletonRel.GetTargets(&targets)
            && !targets.empty()) {
            skeletonOverride = targets.front().GetString();
        }
    }

    UsdSkelSkeleton skeleton;
    if (!FindSkeleton(avatar->stage, skeletonOverride, "avatar", &skeleton,
                      error)) {
        return false;
    }
    avatar->skeletonPath = skeleton.GetPath();

    if (!avatar->skeletonPath.HasPrefix(avatar->defaultPrimPath)) {
        *error = "target skeleton <" + avatar->skeletonPath.GetString()
            + "> is not under the avatar's defaultPrim <"
            + avatar->defaultPrimPath.GetString()
            + ">, so referencing the avatar would not bring it in";
        return false;
    }

    VtTokenArray joints;
    VtMatrix4dArray restTransforms;
    if (!ReadSkeletonRest(skeleton, &joints, &restTransforms,
                          &avatar->warnings)) {
        *error = "target skeleton <" + avatar->skeletonPath.GetString()
            + "> has no joints";
        return false;
    }

    for (std::size_t i = 0; i < joints.size(); ++i) {
        vrmRetarget::TargetJoint joint;
        joint.token = joints[i].GetString();
        DecomposeRest(restTransforms[i], &joint.restRotation,
                      &joint.restTranslation);
        avatar->skeleton.AddJoint(joint);
    }
    avatar->skeleton.ResolveParentsFromTokens();
    if (!avatar->skeleton.IsTopologicallyOrdered()) {
        avatar->warnings.push_back(
            "target skeleton joints are not in parent-before-child order");
    }

    // Stage mappings first, explicit file mappings over the top.
    if (humanoidPrim) {
        for (std::size_t slot = 0; slot < motion::HumanBoneCount; ++slot) {
            const auto bone = static_cast<motion::HumanBone>(slot);
            const std::string name(motion::HumanBoneName(bone));
            const UsdAttribute attribute = humanoidPrim.GetAttribute(
                TfToken(std::string(kHumanBonesPrefix) + name));
            TfToken jointToken;
            if (!attribute || !attribute.Get(&jointToken)
                || jointToken.IsEmpty()) {
                continue;
            }
            if (!avatar->map.SetJointToken(bone, jointToken.GetString(),
                                           avatar->skeleton)) {
                avatar->warnings.push_back(
                    "avatar maps '" + name + "' to joint '"
                    + jointToken.GetString()
                    + "', which the target skeleton does not contain");
            }
        }
    }

    for (const auto& entry : extraMappings) {
        const auto bone = motion::FindHumanBone(entry.first);
        if (!bone) {
            continue;
        }
        if (!avatar->map.SetJointToken(*bone, entry.second, avatar->skeleton)) {
            *error = "humanoid map binds '" + entry.first + "' to joint '"
                + entry.second + "', which the target skeleton does not contain";
            return false;
        }
    }

    if (avatar->map.GetMappedCount() == 0) {
        *error = "no humanoid mapping was found on " + path
            + " and none was supplied; pass --humanoid-map";
        return false;
    }
    return true;
}

bool
ReadClip(const std::string& path, const std::string& skeletonPathOverride,
         Clip* clip, std::string* error)
{
    clip->stage = UsdStage::Open(path);
    if (!clip->stage) {
        *error = "could not open animation stage: " + path;
        return false;
    }
    clip->timeCodesPerSecond = clip->stage->GetTimeCodesPerSecond();
    if (clip->timeCodesPerSecond <= 0.0) {
        clip->timeCodesPerSecond = 30.0;
    }

    UsdSkelSkeleton skeleton;
    if (!FindSkeleton(clip->stage, skeletonPathOverride, "animation", &skeleton,
                      error)) {
        return false;
    }
    clip->skeletonPath = skeleton.GetPath();

    UsdPrim animationPrim;
    if (!UsdSkelBindingAPI(skeleton.GetPrim()).GetAnimationSource(&animationPrim)
        || !animationPrim) {
        // A clip layer whose skeleton carries no binding is still usable when
        // the stage holds exactly one SkelAnimation.
        std::vector<UsdPrim> animations;
        for (const UsdPrim& prim : clip->stage->Traverse()) {
            if (prim.IsA<UsdSkelAnimation>()) {
                animations.push_back(prim);
            }
        }
        if (animations.size() != 1) {
            *error = "clip skeleton <" + clip->skeletonPath.GetString()
                + "> has no skel:animationSource and the stage does not hold "
                  "exactly one UsdSkelAnimation";
            return false;
        }
        animationPrim = animations.front();
    }

    const UsdSkelAnimation animation(animationPrim);
    VtTokenArray animationJoints;
    if (!animation.GetJointsAttr().Get(&animationJoints)
        || animationJoints.empty()) {
        *error = "clip animation <" + animationPrim.GetPath().GetString()
            + "> has no joints";
        return false;
    }

    // Semantic joint -> human bone, plus the clip's own rest pose.
    std::vector<motion::HumanBone> boneForJoint(animationJoints.size(),
                                                motion::HumanBone::Count);
    std::size_t recognized = 0;
    for (std::size_t i = 0; i < animationJoints.size(); ++i) {
        const auto bone =
            motion::FindHumanBone(LeafToken(animationJoints[i].GetString()));
        if (bone) {
            boneForJoint[i] = *bone;
            ++recognized;
        } else {
            clip->warnings.push_back(
                "clip joint '" + animationJoints[i].GetString()
                + "' is not a VRM human bone and was ignored");
        }
    }
    if (recognized == 0) {
        *error = "no joint of clip animation <"
            + animationPrim.GetPath().GetString()
            + "> names a VRM human bone; is this an avatar-independent "
              "semantic clip?";
        return false;
    }

    VtTokenArray restJoints;
    VtMatrix4dArray restTransforms;
    if (ReadSkeletonRest(skeleton, &restJoints, &restTransforms,
                         &clip->warnings)) {
        std::map<std::string, std::size_t> restIndexByJoint;
        for (std::size_t i = 0; i < restJoints.size(); ++i) {
            restIndexByJoint[restJoints[i].GetString()] = i;
        }
        // The clip skeleton's joint paths give the semantic parent, so the rest
        // correction does not need a second copy of the humanoid taxonomy.
        for (std::size_t i = 0; i < restJoints.size(); ++i) {
            const std::string jointPath = restJoints[i].GetString();
            const auto bone = motion::FindHumanBone(LeafToken(jointPath));
            if (!bone) {
                continue;
            }
            const auto slot = static_cast<std::size_t>(*bone);
            GfQuatf rotation;
            GfVec3f translation;
            DecomposeRest(restTransforms[i], &rotation, &translation);
            clip->restPose.localRotations[slot] = rotation;
            clip->restPose.localTranslations[slot] = translation;

            const std::size_t separator = jointPath.rfind('/');
            if (separator == std::string::npos) {
                continue;
            }
            const auto parentBone = motion::FindHumanBone(
                LeafToken(jointPath.substr(0, separator)));
            if (parentBone) {
                clip->restPose.SetParent(*bone, *parentBone);
            }
        }
    }

    const UsdAttribute rotationsAttr = animation.GetRotationsAttr();
    const UsdAttribute translationsAttr = animation.GetTranslationsAttr();

    std::vector<double> rotationTimes;
    std::vector<double> translationTimes;
    rotationsAttr.GetTimeSamples(&rotationTimes);
    translationsAttr.GetTimeSamples(&translationTimes);

    std::set<double> timeCodes(rotationTimes.begin(), rotationTimes.end());
    timeCodes.insert(translationTimes.begin(), translationTimes.end());
    if (timeCodes.empty()) {
        // A clip with no time samples still has a default value; treat it as a
        // single pose at the stage's start.
        timeCodes.insert(clip->stage->GetStartTimeCode());
    }

    int hipsJointIndex = -1;
    for (std::size_t i = 0; i < boneForJoint.size(); ++i) {
        if (boneForJoint[i] == motion::HumanBone::Hips) {
            hipsJointIndex = static_cast<int>(i);
            break;
        }
    }

    clip->animation.samples.reserve(timeCodes.size());
    for (const double timeCode : timeCodes) {
        motion::HumanoidPose pose;
        pose.timestamp = timeCode / clip->timeCodesPerSecond;

        VtQuatfArray rotations;
        if (rotationsAttr.Get(&rotations, timeCode)
            && rotations.size() == animationJoints.size()) {
            for (std::size_t i = 0; i < rotations.size(); ++i) {
                if (boneForJoint[i] == motion::HumanBone::Count) {
                    continue;
                }
                const auto slot = static_cast<std::size_t>(boneForJoint[i]);
                pose.localRotations[slot] = rotations[i].GetNormalized();
                pose.validRotations.set(slot);
            }
        }

        VtVec3fArray translations;
        if (hipsJointIndex >= 0 && translationsAttr.Get(&translations, timeCode)
            && translations.size() == animationJoints.size()) {
            // Only hips translation is body translation (motion contract); the
            // rest is rest-pose data the retargeter re-derives per rig.
            pose.root.worldPosition =
                translations[static_cast<std::size_t>(hipsJointIndex)];
            pose.root.hasPosition = true;
        }

        clip->animation.samples.push_back(std::move(pose));
    }

    clip->animation.startTime = clip->animation.samples.front().timestamp;
    clip->animation.endTime = clip->animation.samples.back().timestamp;
    clip->animation.nominalFrameRate = clip->timeCodesPerSecond;
    clip->animation.source.kind = motion::MotionSourceKind::Clip;
    clip->animation.source.sourceId = path;
    return true;
}

namespace
{

// Asset paths are layer-relative and always forward-slashed, including on
// Windows, so the authored layer stays portable.
std::string
RelativeAssetPath(const std::string& target, const std::string& fromLayer)
{
    namespace fs = std::filesystem;
    std::error_code code;
    const fs::path absoluteTarget = fs::absolute(fs::path(target), code);
    if (code) {
        return target;
    }
    const fs::path layerDirectory =
        fs::absolute(fs::path(fromLayer), code).parent_path();
    if (code) {
        return target;
    }
    const fs::path relative = fs::relative(absoluteTarget, layerDirectory, code);
    if (code || relative.empty()) {
        return absoluteTarget.generic_string();
    }
    std::string result = relative.generic_string();
    if (result.compare(0, 2, "..") != 0 && result.compare(0, 2, "./") != 0) {
        result = "./" + result;
    }
    return result;
}

} // namespace

bool
WriteRetargetedAnimation(const std::string& outputPath, const Avatar& avatar,
                         const Clip& clip,
                         const vrmRetarget::RetargetedAnimation& animation,
                         const std::string& animationName, std::string* error)
{
    if (!TfIsValidIdentifier(animationName)) {
        *error = "'" + animationName + "' is not a valid prim name";
        return false;
    }

    // Re-baking over a previous run is the normal case, so clear an existing
    // layer instead of failing the way UsdStage::CreateNew would.
    SdfLayerRefPtr layer = SdfLayer::FindOrOpen(outputPath);
    if (layer) {
        // FindOrOpen goes through the layer registry, so an output naming an
        // input returns the very layer we just read — Clear() would empty it and
        // Save() would write the result over the user's asset. Compare layer
        // identity rather than the paths, so a different spelling of the same
        // file is caught too.
        const SdfLayerHandle opened(layer);
        if (opened == avatar.stage->GetRootLayer()) {
            *error = "--output would overwrite the avatar layer " + outputPath;
            return false;
        }
        if (opened == clip.stage->GetRootLayer()) {
            *error = "--output would overwrite the animation layer " + outputPath;
            return false;
        }
        layer->Clear();
    } else {
        layer = SdfLayer::CreateNew(outputPath);
    }
    if (!layer) {
        *error = "could not create output layer: " + outputPath;
        return false;
    }
    const UsdStageRefPtr stage = UsdStage::Open(layer);
    if (!stage) {
        *error = "could not open output layer as a stage: " + outputPath;
        return false;
    }

    // Reference the avatar's defaultPrim onto a prim of the same name, so every
    // path under it — including the skeleton's — composes at its original path
    // and the binding override lands where the rig actually is.
    const std::string rootName = avatar.defaultPrimPath.GetName();
    const SdfPath rootPath = SdfPath::AbsoluteRootPath().AppendChild(
        TfToken(rootName));
    const UsdPrim rootPrim = stage->DefinePrim(rootPath);
    const std::string avatarLayerPath =
        avatar.stage->GetRootLayer()->GetIdentifier();
    rootPrim.GetReferences().AddReference(
        RelativeAssetPath(avatarLayerPath, outputPath));
    stage->SetDefaultPrim(rootPrim);

    const SdfPath animationPath = rootPath.AppendChild(TfToken(animationName));
    const UsdSkelAnimation authored = UsdSkelAnimation::Define(stage,
                                                               animationPath);

    VtTokenArray joints;
    joints.reserve(animation.joints.size());
    for (const std::string& joint : animation.joints) {
        joints.push_back(TfToken(joint));
    }
    authored.CreateJointsAttr().Set(joints);

    // UsdSkel fetches translations, rotations, and scales as a unit and fails
    // as a unit; `scales` has no schema fallback, so an animation without it
    // binds cleanly, reads back correctly attribute by attribute, and then
    // resolves no joint transforms at all. Retargeting never animates scale, so
    // author one constant identity array rather than a per-sample track.
    const VtVec3hArray identityScales(animation.joints.size(), GfVec3h(1.0f));
    authored.CreateScalesAttr().Set(identityScales);

    const UsdAttribute rotations = authored.CreateRotationsAttr();
    const UsdAttribute translations = authored.CreateTranslationsAttr();
    for (const vrmRetarget::RetargetedPose& sample : animation.samples) {
        const UsdTimeCode timeCode(sample.timestamp * clip.timeCodesPerSecond);
        rotations.Set(VtQuatfArray(sample.rotations.begin(),
                                   sample.rotations.end()),
                      timeCode);
        translations.Set(VtVec3fArray(sample.translations.begin(),
                                      sample.translations.end()),
                         timeCode);
    }

    // Bind the result on an override of the referenced skeleton rather than by
    // redefining it, so the avatar keeps owning its own rig.
    const UsdPrim skeletonOverride =
        stage->OverridePrim(avatar.skeletonPath);
    if (!skeletonOverride) {
        *error = "could not author a binding override at <"
            + avatar.skeletonPath.GetString() + ">";
        return false;
    }
    // Apply the schema, don't just author the relationship: UsdSkel resolves
    // skel:animationSource only on a prim that carries SkelBindingAPI, so a
    // bare rel leaves the rig in its rest pose with no diagnostic.
    const UsdSkelBindingAPI skeletonBinding =
        UsdSkelBindingAPI::Apply(skeletonOverride);
    if (!skeletonBinding) {
        *error = "could not apply SkelBindingAPI at <"
            + avatar.skeletonPath.GetString() + ">";
        return false;
    }
    skeletonBinding.CreateAnimationSourceRel().SetTargets({animationPath});

    stage->SetTimeCodesPerSecond(clip.timeCodesPerSecond);
    if (!animation.samples.empty()) {
        stage->SetStartTimeCode(animation.startTime * clip.timeCodesPerSecond);
        stage->SetEndTimeCode(animation.endTime * clip.timeCodesPerSecond);
    }

    // Stage metrics come from the root layer alone — the reference authored
    // above does not carry them. An output that declares neither resolves to
    // USD's defaults, so a `.vrm` rig that says `metersPerUnit = 1` composes
    // back as a 1.6 cm avatar. Carry what the avatar resolves rather than
    // assuming VRM's values: the tool takes any rig OpenUSD can open, and a
    // plain `.usda` one may legitimately declare either.
    UsdGeomSetStageMetersPerUnit(stage,
                                 UsdGeomGetStageMetersPerUnit(avatar.stage));
    UsdGeomSetStageUpAxis(stage, UsdGeomGetStageUpAxis(avatar.stage));

    if (!stage->GetRootLayer()->Save()) {
        *error = "could not save " + outputPath;
        return false;
    }
    return true;
}

} // namespace motionRetargetTool
