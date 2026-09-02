// SPDX-License-Identifier: Apache-2.0
//
// Comparing motion values, and why one comparison cannot serve both callers.
//
// `ExecTypeRegistry::RegisterType` wants `operator==`: a value that crosses an
// OpenExec computation boundary has to be able to say whether it changed, and
// "changed" there means the recorded value changed. A trace round-trip wants
// the same comparison from the other side -- write, read, write, and the second
// file must equal the first, bit for bit.
//
// A parity check and a corpus test want the opposite. Two evaluation paths that
// do the same arithmetic in a different order disagree in the last float bits,
// and a fixture recorded through six decimals disagrees with the pose that
// produced it. Comparing those with `==` measures the rounding, not the motion.
//
// So there are two, and each is named for the question it answers:
//
//     a == b               is this the same recorded value?
//     NearlyEqual(a, b)    is this the same motion?
//
// They differ in three places, and each difference is a decision.
//
// **A quaternion and its negation are the same orientation and different
// values.** `q` and `-q` rotate identically -- the double cover -- and which
// one a producer emits is an arbitrary sign it never promised to keep.
// `NearlyEqual` measures the angle between two orientations and so calls them
// equal; `==` compares components and so does not. Downstream of an exec
// computation the conservative answer is the right one: a flipped sign
// recomputes what depends on it, which is wasteful and never wrong.
//
// **Provenance is part of the value and not part of the motion.**
// `MotionSourceMetadata` says where a pose came from. Two poses recorded from
// different senders are different values, so `==` reads the field; they can
// still be the same motion, so `NearlyEqual` does not. This is the one place
// the two comparisons read different fields, and it is the reason a parity
// check needs no knob to switch provenance off.
//
// **The tolerance is stated once, here.** A test that picks its own epsilon is
// asserting a contract nobody reviewed. The defaults below are derived from
// what the values are made of, not chosen to make a test pass.
//
// Both comparisons read exactly the fields a pose says it carries. An absent
// bone's rotation slot and an unset root field hold whatever the producer left
// in them, and comparing that would make two identical motions differ over
// bytes neither pose claims to mean. The *claim* is compared: a pose that omits
// a bone never equals one that carries it, whatever the two slots hold.
//
// A NaN equals nothing, including itself, under both comparisons. That is a
// property of the sample rather than of the comparison -- a non-finite
// transform is a defect an adapter is expected to refuse at its own boundary --
// and a comparison that hid it would make the defect arrive later and quieter.
#pragma once

#include "motionCore/Humanoid.h"
#include "motionCore/api.h"

#include "pxr/base/gf/quatf.h"

#include <string>

namespace motion
{

// The angle in radians between the orientations `a` and `b` describe, in
// [0, pi]: the shortest rotation carrying one onto the other, so `q` and `-q`
// are zero apart. Length is not orientation, so a quaternion of any length
// answers for the orientation it points at -- and one compared with itself, or
// with its negation, answers exactly 0 rather than approximately 0. That
// exactness is load-bearing rather than tidy: `acos` is infinitely steep at 1,
// so anything sloppy in how the two magnitudes are formed reappears as
// milliradians of angle at precisely the place the answer should be zero.
//
// NaN when either quaternion has no length -- a zero quaternion is not an
// orientation, and answering "0 radians from identity" would be inventing one.
MOTIONCORE_API float AngleBetween(const pxr::GfQuatf& a,
                                  const pxr::GfQuatf& b) noexcept;

// How far apart two samples may be and still describe the same motion.
//
// The floor every default has to clear is the recorded-trace format, which
// writes six decimals: a value that survived a round trip is already up to
// 5e-7 away from the one that was recorded, so a tolerance at or below that
// would fail on a fixture that is byte-identical to the session it came from.
// Each default sits at least an order above its floor and far below anything a
// viewer could see.
struct MotionTolerance
{
    // Radians between two orientations. 1e-4 rad is 0.0057 degrees; six-decimal
    // rounding costs about 1e-6 rad, and a retarget composition accumulating a
    // dozen float operations costs a similar amount.
    float angle = 1e-4f;

    // Metres. 10 um -- twenty times the rounding, and below the precision any
    // capture device claims.
    float distance = 1e-5f;

    // Metres or radians per second. Looser than `distance` because a velocity
    // is derived by dividing a position delta by a frame interval, which
    // multiplies the error in it by the frame rate.
    float velocity = 1e-4f;

    // Dimensionless, in [0, 1]. Confidence is reported, never computed, so it
    // carries no accumulated error -- only the rounding.
    float confidence = 1e-6f;

    // Dimensionless, conventionally in [0, 1]. Looser than `confidence`, which
    // is otherwise the same kind of number, because an expression weight is
    // *interpolated*: `LerpPose` blends it between two frames and a glTF
    // sampler evaluates it between two keys, so it accumulates where a reported
    // confidence does not. Twenty times the six-decimal rounding.
    float expression = 1e-5f;

    // Seconds. The trace format's own quantum: a timestamp is exactly what was
    // written unless something recomputed it.
    double time = 1e-6;
};

// Is this the same motion? Provenance is not read (see the header comment);
// every other field is, under `tolerance`.
//
// When the two differ and `difference` is given, it receives one line naming
// the first field that differed and by how much -- "leftUpperArm rotation
// differs by 0.0031 rad", "sample 12: timestamp differs by 0.002 s". The
// order is fixed, so the same pair always reports the same line: timestamp,
// root, bones in humanoid enum order, confidence, contacts, expressions by
// name, look-at target. `difference` is assigned only on a false return and is
// left untouched otherwise.
//
// An expression *name* is an identifier rather than a measurement, so it is
// compared exactly by both -- there is no tolerance under which "happy" and
// "happyy" are the same channel. Only the weight takes one.
MOTIONCORE_API bool NearlyEqual(const RootMotion& a, const RootMotion& b,
                                const MotionTolerance& tolerance = {},
                                std::string* difference = nullptr);

MOTIONCORE_API bool NearlyEqual(const HumanoidPose& a, const HumanoidPose& b,
                                const MotionTolerance& tolerance = {},
                                std::string* difference = nullptr);

// Sample counts must match exactly. Two clips of the same motion at different
// rates are not near each other; resampling one onto the other's timeline is a
// `motionRuntime` operation and the caller's decision to make.
MOTIONCORE_API bool NearlyEqual(const HumanoidAnimation& a,
                                const HumanoidAnimation& b,
                                const MotionTolerance& tolerance = {},
                                std::string* difference = nullptr);

} // namespace motion
