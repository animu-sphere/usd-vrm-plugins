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
//     !motion-capture-trace 3
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
//     lookat 0.000000 1.400000 -2.000000
//     b hips  1.000000 0.000000 0.000000 0.000000
//     b spine 0.999962 0.008727 0.000000 0.000000 0.95
//     e aa    0.250000
//     e happy 0.750000
//
// The three provenance keys take the **rest of the line**, trimmed at both
// ends, because their values are free text somebody outside this repository
// supplied — a VMC sender's `sourceId` is the model title a person typed into
// an application, and "Example Avatar" has a space in it. Everything else in
// the format is token-separated, frame keys included.
//
// `t` opens a frame and every line after it belongs to that frame. Rotations
// are `w x y z`; the trailing number on a `b` line is an optional confidence
// in [0, 1]. Contact values are `unknown`, `contact`, or `free`. Bone names are
// the VRM 1.0 vocabulary spelled as `motion::HumanBoneName` spells it.
//
// `lookat` is the point the frame said the character is looking at (format 3
// on), in the same space as `root pos`. At most one per frame, like `contacts`
// and unlike `b` and `e`, because a sample looks at one place.
//
// `e` is an expression weight (format 2 on). Unlike a bone, its name is the
// producer's own and this layer knows no vocabulary to check it against, so the
// two things a name is checked for are the two that would break the file: it
// must carry no whitespace, because the format is token-separated and a quoting
// rule would need an escaping rule behind it; and it must appear once per
// frame. Weights are written in name order, which is the order
// `ExpressionWeights` keeps them in.
//
// The parser is strict on purpose, in all three of the ways a fixture goes
// wrong silently: an unknown bone name is an error rather than a skip (a typo
// must not read as a missing limb), a line with text left over after its
// operands is an error rather than a truncated read, and a rotation that is not
// unit length is an error rather than a joint UsdSkel will quietly skew.
//
// ## Versions
//
// A version is a claim about content, so it is checked in both directions: a
// format 1 trace carrying an `e` line is refused rather than read leniently.
// Format 1 files still parse -- an old recording on disk stays readable -- but
// the writer only ever emits the current version, so byte-identical round trips
// are a property of traces this writer produced.
#pragma once

#include "motionRuntime/api.h"

#include "motionCore/Humanoid.h"

#include <cstddef>
#include <iosfwd>
#include <string>

namespace motion
{

inline constexpr int CaptureTraceFormatVersion = 3;

// The oldest format the reader accepts. A trace is a recording, and a recording
// that stops being readable because the format moved on is a recording lost.
inline constexpr int CaptureTraceMinReadableVersion = 1;

// The format version that introduced `e` expression lines.
inline constexpr int CaptureTraceExpressionsVersion = 2;

// The format version that introduced the `lookat` target line.
inline constexpr int CaptureTraceLookAtVersion = 3;

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
// bones in humanoid enum order, expressions in name order, and only the fields
// the pose actually carries -- so re-reading and rewriting a trace this writer
// produced is byte-identical and two runs of the same pipeline diff cleanly.
// (Confidence is all-or-nothing per frame: a hand-written trace that annotates
// only some bones is normalised to annotate all of them on the way back out.)
//
// Returns false without writing anything when a value cannot be spelled in this
// format: an expression name that is empty or carries whitespace, or a
// provenance string carrying a line break or padded with whitespace at either
// end. Refusing is the point: emitting it would produce a file that reads back
// as a different animation, or as none. The check runs before the first byte,
// so a caller that is refused still has an untouched stream.
//
// The provenance half of that was added after the writer was found emitting a
// `sourceId` its own reader refused — a sender's model title with a space in
// it. The fix was mostly the reader's (the header takes rest-of-line now), and
// what is left here is the residue no line-oriented format can carry.
MOTIONRUNTIME_API bool WriteCaptureTrace(
    std::ostream& output, const HumanoidAnimation& animation);

MOTIONRUNTIME_API bool WriteCaptureTraceFile(
    const std::string& path, const HumanoidAnimation& animation);

} // namespace motion
