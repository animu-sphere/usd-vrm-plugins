// SPDX-License-Identifier: Apache-2.0
//
// Stable diagnostic codes for import-time messages.
//
// The importer tags every non-fatal diagnostic with one of these codes so
// downstream tooling (tools/validate_vrm.py, tools/vrm_report.py) can classify
// it by severity without parsing free text. The message stays human-readable;
// the code is a stable "[VRMxxx] " prefix on the front of it.
//
// Severity for each code is defined once, in the shared taxonomy
// `tools/vrm_diagnostics.py` (mirrored in `docs/DIAGNOSTICS.md`). Keep the code
// list here in sync with that catalog — the code strings are the contract.
#pragma once

#include "pxr/pxr.h"

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

namespace VrmDiag {

// Reader (glTF / VRM ingest) codes.
inline constexpr const char* NoVrmExtension            = "VRM001";
inline constexpr const char* VrmJsonParseFailed        = "VRM002";
inline constexpr const char* ContainerUnreadable       = "VRM003";
inline constexpr const char* TextureFormatUnsupported  = "VRM101";
inline constexpr const char* TextureDataUriUnsupported = "VRM102";
inline constexpr const char* TextureTexcoordUnsupported = "VRM103";
inline constexpr const char* SkinIbmConflict           = "VRM110";
inline constexpr const char* SkinJointIndexOutOfRange  = "VRM111";
inline constexpr const char* PrimitiveNotTriangles     = "VRM120";
inline constexpr const char* PrimitiveNoPosition       = "VRM121";
inline constexpr const char* HumanoidBoneUnmapped      = "VRM140";
inline constexpr const char* HumanoidBoneDuplicate     = "VRM141";
inline constexpr const char* ExpressionVrm0MaterialValues = "VRM150";
inline constexpr const char* ExpressionMorphIndexOutOfRange = "VRM151";
inline constexpr const char* AnimationCubicSpline      = "VRM160";
inline constexpr const char* ConstraintNoSource        = "VRM170";
inline constexpr const char* SpringColliderGroupOutOfRange = "VRM190";

// Authorer (USD write) codes.
inline constexpr const char* MorphNoSkeleton           = "VRM180";
inline constexpr const char* HumanoidNoSkeleton        = "VRM181";

}  // namespace VrmDiag

// Prefix a diagnostic message with its stable code: VrmDiagMsg("VRM101", "...")
// -> "[VRM101] ...". The prefix is machine-parseable (^\[(VRM\d+)\]) while the
// tail stays the same human-readable text the importer has always emitted.
inline std::string VrmDiagMsg(const char* code, const std::string& message)
{
    return "[" + std::string(code) + "] " + message;
}

PXR_NAMESPACE_CLOSE_SCOPE
