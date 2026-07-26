// SPDX-License-Identifier: Apache-2.0
//
// A recorded capture trace: the on-disk form of a live session.
//
// Motion Phase D's tests have to be reproducible, and a live capture is the
// least reproducible thing in the project. The resolution is to record what an
// adapter delivered -- after protocol decode and coordinate conversion, before
// any intake policy -- and replay it. A trace is therefore exactly a
// `HumanoidAnimation`, and replaying one through `LiveCaptureSource` is
// indistinguishable from the session that produced it.
//
// The format is line-oriented text on purpose. A capture fixture is reviewed
// in a pull request like any other fixture, so it has to diff; and a writer
// that emits fixed-precision decimals round-trips byte-identically, which is
// what lets a golden trace be compared rather than merely parsed.
//
//     # a comment
//     !motion-capture-trace 1
//     provider   example.replay
//     protocol   replay
//     sourceId   walk-01
//     frameRate  30
//
//     t 0.000000
//     root pos 0.000000 0.900000 0.000000
//     root rot 1.000000 0.000000 0.000000 0.000000
//     root vel 0.000000 0.000000 0.000000
//     contacts contact free
//     b hips  1.000000 0.000000 0.000000 0.000000
//     b spine 0.999962 0.008727 0.000000 0.000000 0.95
//
// `t` opens a frame and every line after it belongs to that frame. Rotations
// are `w x y z`; the trailing number on a `b` line is an optional confidence
// in [0, 1]. Contact values are `unknown`, `contact`, or `free`. Bone names are
// the VRM 1.0 vocabulary spelled as `motion::HumanBoneName` spells it.
//
// The parser is strict on purpose, in all three of the ways a fixture goes
// wrong silently: an unknown bone name is an error rather than a skip (a typo
// must not read as a missing limb), a line with text left over after its
// operands is an error rather than a truncated read, and a rotation that is not
// unit length is an error rather than a joint UsdSkel will quietly skew.
#pragma once

#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

#include <cstddef>
#include <iosfwd>
#include <string>

namespace motion
{

inline constexpr int CaptureTraceFormatVersion = 1;

// The format writes six decimals, so a timestamp read back from a trace can sit
// up to half of this away from the exact instant it was meant to represent --
// 1/30 s stores as 0.033333, 2/30 s as 0.066667, one below and one above.
//
// That matters more than the magnitude suggests, and it is why
// `PoseSampleTimeTolerance` (MotionSource.h) exists: a schedule computed as
// `k / rate` in exact arithmetic lands on the other side of the rounding for
// half the frames, so without a tolerance each of those frames misses the very
// tick it belongs to and the consumer extrapolates a whole frame interval
// instead of sampling.
inline constexpr double CaptureTraceTimeQuantum = 1e-6;

struct CaptureTraceError
{
    // 1-based; 0 when the failure is not tied to a line (a file that will not
    // open, for instance).
    std::size_t line = 0;
    std::string message;
};

// Parses a trace. On failure `animation` is left untouched and `error`, when
// given, names the line and the reason.
MOTIONRUNTIME_API bool ReadCaptureTrace(
    std::istream& input, HumanoidAnimation* animation,
    CaptureTraceError* error = nullptr);

MOTIONRUNTIME_API bool ReadCaptureTraceFile(
    const std::string& path, HumanoidAnimation* animation,
    CaptureTraceError* error = nullptr);

// Writes `animation` as a trace. Emission is deterministic: fixed precision,
// bones in humanoid enum order, and only the fields the pose actually carries
// -- so re-reading and rewriting a trace this writer produced is byte-identical
// and two runs of the same pipeline diff cleanly. (Confidence is all-or-nothing
// per frame: a hand-written trace that annotates only some bones is normalised
// to annotate all of them on the way back out.)
MOTIONRUNTIME_API bool WriteCaptureTrace(
    std::ostream& output, const HumanoidAnimation& animation);

MOTIONRUNTIME_API bool WriteCaptureTraceFile(
    const std::string& path, const HumanoidAnimation& animation);

} // namespace motion
