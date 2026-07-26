// SPDX-License-Identifier: Apache-2.0
//
// The stage half of the capture tool. Everything that knows about UsdStage
// lives here; `motionRuntime` itself takes and returns plain values
// (WORKSPACE.md §2).
//
// What it authors is deliberately not a new shape: it is the same
// avatar-independent semantic humanoid clip `usdVrmaFileFormat` produces from a
// `.vrma` -- a `UsdSkelSkeleton` over humanoid joint paths, a `UsdSkelAnimation`
// bound to it, and nothing that names a target rig. That is what lets
// `motion_retarget` bake a live session onto an avatar without knowing a live
// session ever happened.
#pragma once

#include "motionCore/Humanoid.h"

#include <map>
#include <string>

namespace motionCaptureTool
{

// `provenance` is written verbatim into the clip's customData under
// `capture:*`, so a baked result can be traced back to the session and the
// intake settings that produced it.
bool WriteSemanticClip(const std::string& outputPath,
                       const motion::HumanoidAnimation& animation,
                       const std::string& clipName,
                       const std::map<std::string, std::string>& provenance,
                       std::string* error);

} // namespace motionCaptureTool
