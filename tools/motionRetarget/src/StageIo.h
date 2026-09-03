// SPDX-License-Identifier: Apache-2.0
//
// The stage half of the retarget tool.
//
// Everything that knows about UsdStage lives here; `vrmRetarget` itself takes
// and returns plain values (WORKSPACE.md §2). This file reads the target rig
// and the source clip off stages and writes the result back to one.
#pragma once

#include "vrmRetarget/ExpressionResolver.h"
#include "vrmRetarget/HumanoidMap.h"
#include "vrmRetarget/LookAtEvaluator.h"
#include "vrmRetarget/PoseRetargeter.h"
#include "vrmRetarget/RestPose.h"
#include "vrmRetarget/TargetSkeleton.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/vec3f.h"
#include "pxr/usd/usd/stage.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace motionRetargetTool
{

struct Avatar
{
    pxr::UsdStageRefPtr stage;
    pxr::SdfPath skeletonPath;
    // The prim the output layer references — the stage's default prim, which
    // must be an ancestor of the skeleton for the binding override to compose.
    pxr::SdfPath defaultPrimPath;
    vrmRetarget::TargetSkeleton skeleton;
    vrmRetarget::HumanoidMap map;

    // What this rig declares about its face: the expressions keyed by
    // `vrm:expressionName`, with the binds each one drives.
    vrmRetarget::ExpressionRig expressionRig;

    // Blend-shape prim path -> the token the mesh binding it names it by.
    // A UsdSkelAnimation names blend shapes by token and UsdSkel joins those
    // tokens to a skinned prim's `skel:blendShapes`, so a weight resolved onto
    // a prim path cannot be authored until it is translated — and a blend shape
    // no mesh binds cannot be driven from an animation at all.
    std::map<std::string, std::string> blendShapeTokens;

    // What this rig declares about its gaze: the VrmLookAtAPI prim's type, its
    // eye joint tokens, and the four range-map curves the importer preserved as
    // raw JSON. `hasLookAt` is separate from the rig's own emptiness because a
    // rig that declares a look-at and states nothing useful in it is a rig with
    // a defect, while one that declares none is an ordinary `.usda` skeleton.
    bool hasLookAt = false;
    vrmRetarget::LookAtRig lookAtRig;

    std::vector<std::string> warnings;
};

struct Clip
{
    pxr::UsdStageRefPtr stage;
    pxr::SdfPath skeletonPath;
    motion::HumanoidAnimation animation;
    vrmRetarget::SourceRestPose restPose;
    double timeCodesPerSecond = 30.0;

    // The `vrm:lookAtOffsetFromHeadBone` the clip carries: a measurement of the
    // rig the clip was authored on, and constant over the clip, which is why it
    // is here rather than on every sample. Absent when the clip authored none.
    std::optional<pxr::GfVec3f> lookAtOffsetFromHeadBone;

    // True when the clip authored a `vrm:lookAtTarget` with a value -- a
    // channel or a single default. Separate from any sample's own target,
    // because it is what lets the caller tell "the clip named a gaze this rig
    // cannot follow" from "the clip named none".
    bool hasLookAtTrack = false;

    std::vector<std::string> warnings;
};

// Reads a `humanBone -> joint token` JSON object, e.g.
// `{"hips": "Root/Pelvis", "spine": "Root/Pelvis/SpineA"}`.
bool ReadHumanoidMapFile(const std::string& path,
                         std::map<std::string, std::string>* entries,
                         std::string* error);

// Opens the avatar and resolves its target skeleton plus humanoid mapping.
// `skeletonPathOverride` and `extraMappings` may be empty; extra mappings are
// applied over anything found on the stage.
bool ReadAvatar(const std::string& path,
                const std::string& skeletonPathOverride,
                const std::map<std::string, std::string>& extraMappings,
                Avatar* avatar, std::string* error);

// Opens the clip and reads its semantic humanoid animation.
bool ReadClip(const std::string& path, const std::string& skeletonPathOverride,
              Clip* clip, std::string* error);

// What the write put on the stage, for the caller's summary and diagnostics.
//
// The blend-shape count comes from the authoring rather than from the resolve
// on purpose: a target the avatar declares but no mesh binds resolves to a
// weight and is still not authored, so a count taken one step earlier would
// report a face this layer did not drive.
struct WriteResult
{
    std::size_t blendShapesAuthored = 0;
    std::vector<std::string> warnings;
};

// Authors `animation` into a new layer that references the avatar and binds the
// result to the target skeleton.
//
// `expressions` is either empty or one entry per sample of `animation`, in the
// same order — the face half of the same samples the body was expanded from.
bool WriteRetargetedAnimation(
    const std::string& outputPath, const Avatar& avatar, const Clip& clip,
    const vrmRetarget::RetargetedAnimation& animation,
    const std::vector<vrmRetarget::ResolvedExpressions>& expressions,
    const std::string& animationName, WriteResult* result, std::string* error);

} // namespace motionRetargetTool
