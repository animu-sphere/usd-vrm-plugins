// SPDX-License-Identifier: Apache-2.0
//
// The stage half of the convert tool. Everything that knows about `UsdStage`
// lives here; `motionSource` and `motionBvh` take and return plain values
// (WORKSPACE.md §2).
//
// What it authors is the same avatar-independent semantic humanoid clip
// `usdVrmaFileFormat` produces from a `.vrma` and `motion_capture` produces
// from a session — a `UsdSkelSkeleton` over humanoid joint paths, a
// `UsdSkelAnimation` bound to it, and nothing that names a target rig. That is
// what lets `motion_retarget` bake a recorded file onto an avatar without
// knowing a `.bvh` ever existed.
//
// **This is the third caller of that shape, and it stays a repeated shape
// rather than becoming shared code** — which is roadmap §10's open question,
// answered here because this is the change that made it three. The reason is
// that the writers differ in exactly the field that matters most, and sharing
// them would mean parameterising over it:
//
//   * `motion_capture` authors **identity** rest transforms, because a capture
//     stream reports rotations relative to the humanoid rest and never the rest
//     itself. Its one seeded value is the hips translation.
//   * this writer authors a **real** rest pose, because a recorded file states
//     one and the profile says how to read it. `CanonicalRestPose` is what §4
//     calls the source rest, and `vrmRetarget` is what corrects it onto a
//     target's — a converter that skipped it would hand the retargeter an
//     identity source rest and silently claim the rig stands the way the avatar
//     does.
//
// The condition that would change the answer is a fourth caller, or a third
// that needs neither variant: at that point the difference is a parameter and
// the shape is a function. What must not happen in the meantime is the two
// drifting on the parts that are *not* a choice, so `motion_bvh_convert_writer`
// pins them — the joint set is in `HumanBone` order, `scales` is authored, and
// the time codes are frames rather than seconds. `scales` is the one with a
// scar: `UsdSkel` fetches translations, rotations and scales as a unit and
// `scales` has no schema fallback, so omitting it does not mean "this clip
// animates no scale", it silently resolves the whole animation to the rest pose.
#pragma once

#include "motionSource/CanonicalConversion.h"

#include "motionCore/Humanoid.h"

#include <map>
#include <string>

namespace motionBvhTool
{

// Writes `animation` over `rest` as a semantic clip at `outputPath`.
//
// The joint set is `rest.present` — every bone the profile bound — and not the
// bones some frame happened to rotate. A rig carries a bone whether or not the
// file said how it turned, its rest is a real measurement either way, and a
// joint set that varied with the motion would make two recordings of one rig
// produce two skeletons that do not compose.
//
// `provenance` is written verbatim into the clip's customData under `source:*`,
// so a baked result can be traced back to the file and the profile that
// produced it. Neither this function nor its caller may branch on any of it:
// a producer name reaching an `if` is the failure the profile design exists to
// prevent (WORKSPACE.md §1).
bool WriteSemanticClip(const std::string& outputPath,
                       const motion::HumanoidAnimation& animation,
                       const motionSource::CanonicalRestPose& rest,
                       const std::string& clipName,
                       const std::map<std::string, std::string>& provenance,
                       std::string* error);

} // namespace motionBvhTool
