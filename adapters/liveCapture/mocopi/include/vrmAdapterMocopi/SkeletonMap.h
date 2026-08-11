// SPDX-License-Identifier: Apache-2.0
//
// This product's joint ids and this product's axes, turned into canonical
// humanoid semantics. It is the first layer in this adapter that knows a
// `motion::HumanBone` exists, and it is the one conversion the adapter exists
// to perform (roadmap/adapters-mocopi-vmc-ardy.md §6).
//
// It converts and it does not decide. A decoded `MotionSkeleton` and
// `MotionFrame` go in, canonical values come out; whether a frame with three
// bones missing is a usable frame, whether the counter went backwards, and
// whether the hips position is also the body's root are the frame assembler's
// and the bridge's questions and are deliberately not answered here. Nor does
// anything below resolve a target joint: a joint becomes a `HumanBone` and
// stops, because the map from there to a rig belongs to `vrmRetarget` and to
// the avatar's own `vrm:humanBones` bindings (§5.1).
//
// ## A bone id is a position, not a name
//
// The sibling adapter maps a *string*: a VMC sender writes "LeftUpperArm" and
// the name means the same thing whoever sent it. This protocol has no strings.
// A `bnid` is the position of a record in a fixed-length list, so id 12 means
// "the twelfth joint of whatever rig this session is sending" and nothing more
// — which is why `MotionPacket.h` reports ids and refuses to name them, and why
// this layer cannot simply hold a 27-entry table and index it.
//
// So the map is built from a **skeleton packet**, and the rig it declares is
// checked against the measured one before any id is trusted. The parent column
// below is the topology all five measured sessions sent, and a session that
// disagrees with it gets no map at all rather than a map that reads the wrong
// joint. That is the same refusal the recorded track's profile makes with
// `unmappedJoints: refuse`, arrived at from the other direction: there a joint
// is named and the name might be unknown, here a joint is numbered and the
// number is only meaningful relative to a shape.
//
// A rig **longer** than the measured one keeps its first 27 joints mapped and
// reports the rest as unsupported, provided those 27 match the column exactly.
// This is a judgement and worth naming as one: it assumes a producer that adds
// fingers appends them, which is what a fixed-position encoding all but forces
// but is not something this project has measured. The topology check is what
// makes it safe rather than hopeful — a rig that inserted a joint would shift
// every parent after it and be refused whole.
//
// ## Which joint carries which bone, and where the table comes from
//
// The table is **not new evidence**. It is the same measurement
// `profiles/motion/mocopi-mobile-bvh-default-v1.yaml` was written from on
// 2026-08-04, transferred here on 2026-08-12 when the rest offsets of the two
// transports were shown to agree sign for sign to 4.4e-7 m (MotionPacket.h).
// Twenty-two of twenty-seven joints carry a canonical bone; the five that do
// not are four spine segments and the second neck segment, exactly the five
// that profile lists as `ignoredJoints`.
//
// The two tables are deliberately **not** shared, and this is the plan's rule
// rather than an oversight: a file reader and a socket meet at `motionCore` and
// nowhere earlier (§2.1, motion policy §8.3), so a table imported from the
// recorded track would give a live session a file's assumptions the first time
// the two rigs stopped being the same rig. What keeps them honest is that they
// are checked against each other from outside — `scripts/check_docs.py` reads
// both and fails when they disagree — so the duplication costs a check rather
// than a silent divergence.
//
// ## An unmapped joint is not a rotation thrown away
//
// The canonical humanoid has four joints between the pelvis and the neck where
// this rig has eight. A map that took `torso_2`'s local rotation verbatim as
// `spine` would lose `torso_1` entirely, and the result reads as a subtly
// misassembled body rather than as a failure. So a bound bone's local rotation
// is the composition of the source local rotations from just below its nearest
// bound ancestor down to itself, root-first — the path rule, stated once in
// [MOTION_CONTRACT.md](../../../../../docs/design/MOTION_CONTRACT.md) and
// implemented here for the second time.
//
// Which joints a map binds therefore changes how a bend is *distributed* and
// never the chain's total orientation. That is what makes the three-of-seven
// torso choice a decision this layer can hold without being wrong about the
// motion.
//
// **A path is only as present as its joints.** If any joint on a bone's path is
// missing from the frame — the decoder refused its record, or the rig never sent
// it — the bone is absent from the mapping rather than composed from what
// arrived. An absent bone is not an identity sample, and a partial composition
// would be a rotation the device never sent. That is reported and not repaired:
// what a consumer does with a bone that stopped arriving is `LiveCaptureSource`'s
// missing-bone policy, one layer up and already written.
//
// ## The basis change is the identity, and that is a measurement
//
// Canonical motion is right-handed, +Y up, +Z forward, metres
// (MOTION_CONTRACT.md). This device's basis is right-handed, +Y up, +Z forward,
// metres, with +X the body's left — measured, and the last of the four settled
// on 2026-08-12. They are the same basis. So the change of basis is the
// identity: no permutation, no reflection, determinant +1, nothing scaled.
//
// That is worth a named function rather than an absence of one, for two
// reasons. The claim "nothing is converted" is a measurement that can be wrong,
// and a function is where it is written down and tested; and the sibling
// adapter's is *not* the identity — a VMC sender is left-handed and needs its X
// flipped — so a reader comparing the two adapters should find the same
// question answered in both, with different answers.
//
// One thing does change, and it is not a basis: the wire carries a quaternion
// scalar-**last** and `pxr::GfQuatf` stores it scalar-**first**, so the
// components are reordered and the result normalised. A reorder is not a
// rotation, and a reader who mistook it for one would be looking for a
// handedness change that is not there.
//
// The tests check the rotation half **physically** — a direction is rotated and
// the answer compared against where the body has to have put it — because a
// component-against-component test agrees with a mirrored implementation as
// readily as with a correct one. This is the same trap `motionSource_conversion`
// documents, and the fixtures make it concrete: `arms-lowered-60hz` turns the
// two upper arms in opposite directions, so a map that swapped the sides would
// raise one arm rather than lowering both.
//
// ## One translating joint, and it is the hips
//
// The sibling adapter has two candidate root translations — `/VMC/Ext/Root/Pos`
// and the hips local position — cannot compose them, and says so
// ([§5.2](../../../../../docs/roadmap/adapters-mocopi-vmc-ardy.md)). Natively
// that ambiguity does not arise: the rig's root joint *is* the hips, there is no
// second channel, and in 207,064 measured bone-frames every non-root translation
// equalled its rest offset bit for bit. So the body's placement is the hips
// joint's own translation, absolute and in metres, and this layer reports it as
// that.
//
// It is reported beside the bones rather than as a `motion::RootMotion`, which
// keeps one decision where it belongs: whether the body's placement is root
// motion, and whether the hips rotation is also the root's orientation, is a
// policy question with an open record in this release, and a converter that
// answered it by filling in a `RootMotion` would have taken it silently.
//
// A non-hips joint whose translation leaves its rest offset is **counted**, not
// carried and not refused. The canonical pose has nowhere to put it — only hips
// translation is body translation, by the Phase A rule — so it is dropped, and
// the count is how an operator finds out that a session did something the
// measurement says it does not do.
//
// ## The rest pose is the device's, not the first frame's
//
// The sibling's header names the thing an adapter must not do: manufacture a
// source rest pose from the first frame it happens to see. Natively there is
// nothing to manufacture. A skeleton packet is a rest pose — the device sends
// one about every 3.5 s — so the map carries the real one, composed along the
// same paths and in canonical values.
//
// It is carried as values rather than as a `vrmRetarget::SourceRestPose`
// because that type lives across an edge this library may not have
// (WORKSPACE.md §2: an adapter reaches `motionCore` and `motionRuntime` and
// nothing else). A tool may compose the two, and that is where it happens.
//
// ## The codes this layer raises
//
// `VRM_MOCOPI_UNSUPPORTED_JOINT` — a rig this map cannot read, or a joint in a
// longer rig whose meaning has not been measured. Raised while the map is being
// built and never per frame: both are properties of the session, and a frame
// that repeated them would say the same thing sixty times a second. Never for
// the five segments the canonical vocabulary has no bone for either: those are
// known, deliberate, and on the path, so reporting them would be reporting a
// decision as a surprise.
//
// `VRM_MOCOPI_NON_FINITE_TRANSFORM` — a transform that names no orientation.
// The decoder refuses those before they reach here, so this is unreachable
// through `DecodeMotionPacket`; it is checked anyway, because these functions
// take structs rather than datagrams and nothing in the type system says where
// a struct came from.
//
// Not `VRM_MOCOPI_FRAME_INCOMPLETE`: how many of the twenty-two bones make a
// usable frame is the assembler's question and its code. Not
// `VRM_MOCOPI_TRACKING_LOST`: the measured grammar carries no per-joint
// confidence or state, so there is nothing here to decode into it.
#pragma once

#include "vrmAdapterMocopi/Diagnostics.h"
#include "vrmAdapterMocopi/MotionPacket.h"
#include "vrmAdapterMocopi/api.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace vrmAdapterMocopi
{

// The parent of each joint of the measured rig, by id, with -1 for the root.
// Measured: the column all five sessions sent, single-rooted and already in
// topological order. It is the rig's identity as far as this layer is
// concerned — ids mean nothing without it.
inline constexpr std::array<std::int16_t, MeasuredBoneCount>
    MeasuredParentColumn = {{
        // root, the seven torso segments, two neck segments, head
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
        // the two arms, off the top of the torso chain
        7, 11, 12, 13, 7, 15, 16, 17,
        // the two legs, off the root
        0, 19, 20, 21, 0, 23, 24, 25,
    }};

// The canonical bone the measured rig's joint `boneId` carries, or nullopt for
// the five it has no bone for and for any id the measured rig does not have.
// `IsMeasuredJoint` is what separates those two answers.
VRMADAPTERMOCOPI_API std::optional<motion::HumanBone> MeasuredHumanBone(
    std::uint16_t boneId) noexcept;

// Whether `boneId` is a joint of the measured rig at all. A measured joint with
// no canonical bone is one of the five on the path; anything else is a joint
// whose meaning this project has not measured.
VRMADAPTERMOCOPI_API bool IsMeasuredJoint(std::uint16_t boneId) noexcept;

// The device's axes into the canonical ones. The identity, measured — see the
// header. No validity check: a non-finite input converts to a non-finite
// output, because these are the arithmetic and the functions below are the
// boundary.
VRMADAPTERMOCOPI_API pxr::GfVec3f ToCanonicalPosition(
    const std::array<float, 3>& translation) noexcept;

// Reorders the wire's scalar-last components into `pxr::GfQuatf`'s scalar-first
// ones, and normalises. NaN components stay NaN and a zero-length quaternion
// converts to a zero-length one; neither is repaired here.
VRMADAPTERMOCOPI_API pxr::GfQuatf ToCanonicalRotation(
    const std::array<float, 4>& rotation) noexcept;

// One canonical joint of one frame.
struct BoneSample
{
    motion::HumanBone bone = motion::HumanBone::Count;

    // Local to the semantic humanoid parent, composed along the path from just
    // below the nearest bound ancestor. It is the *source's* local rotation:
    // it equals the rotation away from rest only for a rig whose rest is
    // identity, which is why the map carries the device's rest pose beside it
    // rather than folding one into the other.
    pxr::GfQuatf localRotation = pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f));
};

// Which joint of a session's rig carries which canonical bone, and what that
// rig's rest pose is. Built from a skeleton packet, because that is the only
// thing that says which rig a session is sending.
struct SkeletonMap
{
    // Identity rotations and zero translations until a skeleton packet fills
    // them, for the same reason `motion::HumanoidPose` declares one: a rest
    // pose that is uninitialised memory makes a caller's mistake a different
    // bug on every run.
    VRMADAPTERMOCOPI_API SkeletonMap();

    // The canonical bone `boneId` carries in this rig, or nullopt.
    VRMADAPTERMOCOPI_API std::optional<motion::HumanBone> Bone(
        std::uint16_t boneId) const noexcept;

    // The canonical bones this rig carries. Twenty-two for the measured rig.
    std::bitset<motion::HumanBoneCount> present;

    // The device's rest pose in canonical values: local to the semantic
    // humanoid parent, composed along the same paths the frames are. Every
    // measured skeleton packet states identity rotations and puts the whole
    // rest in the offsets, and this reports what the packet said rather than
    // that expectation.
    std::array<pxr::GfQuatf, motion::HumanBoneCount> restRotations;
    std::array<pxr::GfVec3f, motion::HumanBoneCount> restTranslations;

    // The joint count the session declared. `MeasuredBoneCount`, or more for a
    // longer rig whose leading joints matched.
    std::size_t jointCount = 0;

    // The measured joints' rest translations by id, exactly as the wire carried
    // them. Kept unconverted on purpose: it is what a frame's translations are
    // compared against, and the measured claim is that they are equal bit for
    // bit rather than nearly equal.
    //
    // Fixed at the measured rig's size rather than the session's. A longer rig's
    // extra joints carry no bone and no path, so their rest is never read — and
    // an array indexed by an id this map has already bounded cannot be indexed
    // past its end by any caller, which a vector sized from a separate field
    // could.
    std::array<std::array<float, 3>, MeasuredBoneCount> jointRestTranslations{};
};

// Builds the map for a session from its skeleton packet.
//
// Returns false and leaves `out` untouched when the rig is not one this map can
// read: fewer joints than the measured rig, or a leading joint whose id or
// parent disagrees with the measured column. The diagnostic is
// `VRM_MOCOPI_UNSUPPORTED_JOINT` and its subject is the first joint that
// disagreed, because that is the joint a reader would have to look at — a rig
// that is short says so at the id it stops at. A rest transform that names no
// orientation refuses the rig too, as `VRM_MOCOPI_NON_FINITE_TRANSFORM`: a rest
// table with a hole in it leaves every bone below the hole unplaced.
//
// Returns true with one further diagnostic per joint beyond the measured rig:
// those joints are carried by no bone and this is what says so — once per
// skeleton packet, which a device sends about every 3.5 s, rather than once per
// frame at sixty a second. A caller that rebuilds only when the rest table
// changed reports it once per session, and a caller that rebuilds on every
// packet gets it again each time, because a function cannot know it has already
// been asked. A trailing joint claiming an id *inside* the measured range
// refuses the rig instead: an id is a position, and that rig has broken the
// identity every id above rests on.
//
// The severity is the code's default — info — except when the whole rig is
// refused, which is reported as a warning. The code stays one of the frozen
// nine; what differs is that one unmapped joint leaves a session working and no
// mapped joint at all leaves it producing nothing, and an operator watching a
// live log should not have to read the detail text to tell those apart.
//
// `diagnostics`, when given, is appended to and never cleared.
VRMADAPTERMOCOPI_API bool MakeSkeletonMap(
    const MotionSkeleton& skeleton, SkeletonMap* out,
    std::vector<Diagnostic>* diagnostics = nullptr);

// What one frame said, in canonical terms.
struct FrameMapping
{
    // In `motion::HumanBone` order rather than wire order, so two frames of the
    // same session produce the same sequence whatever order the device sent its
    // records in.
    std::vector<BoneSample> bones;

    // The bones `bones` carries, as the canonical pose wants them.
    std::bitset<motion::HumanBoneCount> present;

    // The hips joint's translation: the body's placement, absolute, in metres.
    // False when the hips record did not arrive.
    bool hasHipsPosition = false;
    pxr::GfVec3f hipsPosition = pxr::GfVec3f(0.0f);

    // Records this mapping did not carry: a joint the session's skeleton packet
    // did not declare, a joint beyond the measured rig, or a repeat of an id
    // already read. Counted rather than refused, and counted rather than
    // ignored — a frame with records in it that no bone came from is a fact an
    // operator is entitled to.
    std::size_t unusedJoints = 0;

    // Non-hips joints whose translation left its rest offset. The canonical
    // pose has nowhere to put those, so they are dropped — see the header. Zero
    // in every measured session, which is the point of counting it.
    std::size_t droppedTranslations = 0;

    // Bones this rig carries that this frame could not form, because a joint on
    // their path did not arrive. `present` says which arrived; this says how
    // many did not, so a caller that only wants the count does not have to
    // subtract two bitsets.
    std::size_t missingBones = 0;
};

// Maps one decoded frame through `map`.
//
// Returns false when the frame formed no canonical bone at all — an empty
// mapping is not a pose, and a caller that treated one as a frame of motion
// would be publishing the last frame's values as this frame's. Everything
// short of that is reported in the mapping rather than refused: a bone whose
// path did not arrive is absent, a joint the rig does not have is counted, and
// how much of that is still a usable frame is the assembler's question.
//
// `out` is written on **every** return, including false, which is the opposite
// of `MakeSkeletonMap` above and is stated because the pair invites the
// assumption. A rig is a session-long thing worth keeping when a packet fails to
// replace it; a frame is not, and a receive loop reusing one `FrameMapping`
// across frames must never see the previous frame's bones survive this call.
//
// `diagnostics`, when given, is appended to and never cleared.
VRMADAPTERMOCOPI_API bool MapMotionFrame(
    const SkeletonMap& map, const MotionFrame& frame, FrameMapping* out,
    std::vector<Diagnostic>* diagnostics = nullptr);

} // namespace vrmAdapterMocopi
