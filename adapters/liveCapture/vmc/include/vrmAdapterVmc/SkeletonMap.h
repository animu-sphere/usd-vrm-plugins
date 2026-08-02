// SPDX-License-Identifier: Apache-2.0
//
// VMC's names and VMC's axes, turned into canonical humanoid semantics. This is
// the one conversion the adapter exists to perform
// (roadmap/adapters-mocopi-vmc-ardy.md §2), and it is the first layer that
// knows a `motion::HumanBone` exists.
//
// It converts and it does not decide. A `VmcMessage` goes in, canonical values
// come out; which of them belong to one frame, what a missing bone means, and
// how the root and the hips combine are the frame assembler's business (§5.2)
// and are deliberately not answered here. Nor does anything below resolve a
// target joint: a bone becomes a `HumanBone` and stops, because the map from
// there to `/Asset/skel/Skeleton` belongs to `vrmRetarget` and to the avatar's
// own `vrm:humanBones` bindings (§5.1).
//
// Four things are decisions rather than details.
//
// **The vocabulary is Unity's `HumanBodyBones`, and it is not VRM 1.0's.** A
// VMC sender is a Unity application and writes Unity's spelling, which is
// PascalCase where VRM 1.0 is lowerCamel. The two vocabularies have the same 55
// bones and — for every bone but three — the same word in a different case. The
// exception is the thumb, which VRM 1.0 renamed one joint down the chain:
//
//     Unity / VRM 0.x            VRM 1.0 / motion::HumanBone
//     LeftThumbProximal      ->  leftThumbMetacarpal
//     LeftThumbIntermediate  ->  leftThumbProximal
//     LeftThumbDistal        ->  leftThumbDistal
//
// A map that lowercased the first letter and matched would send every thumb
// rotation to the wrong joint while every other bone in the hand arrived
// correctly — which is why the table below is written out rather than derived,
// and why the thumb is the one case the tests state twice.
//
// **The spelling is matched exactly.** A sender writing `leftUpperArm` is not
// writing VMC, and accepting both spellings would leave the corpus unable to
// pin which one the protocol uses. An unrecognised name is reported as
// `VRM_VMC_UNSUPPORTED_MESSAGE` — info, recoverable, the bone ignored and the
// frame kept — for the same reason the message layer treats an unimplemented
// address that way: this adapter cannot tell a sender's extension from a typo,
// and refusing a frame over one unknown name would lose the twenty-one bones
// that arrived with it.
//
// **The basis change is VRM 1.0's, which is not VRM 0.x's.** Unity is
// left-handed with +Y up and the avatar facing +Z; the canonical contract is
// glTF's right-handed, Y-up, metre basis (MOTION_CONTRACT.md). VRM 1.0 defines
// that conversion as a reflection through the X axis, where VRM 0.x used the Z
// axis, and this adapter follows VRM 1.0 because the humanoid vocabulary it
// converts into is VRM 1.0's:
//
//     position   (x, y, z)     ->  (-x, y, z)
//     rotation   (x, y, z, w)  ->  (w, (x, -y, -z))
//
// The rotation follows from the position: a reflection maps the rotation axis
// as a vector, and reverses the sense of the rotation with the handedness, so
// the two sign flips on the imaginary part are one flip from the axis and one
// from the angle. Units are metres on both sides, so nothing is scaled.
//
// **A rotation that is not an orientation is refused, not repaired.** VMC's
// quaternions arrive un-normalised in practice, so they are normalised here —
// that is arithmetic, not a decision. A zero-length or non-finite one is a
// different thing: it names no orientation, and the value that would have to be
// invented to carry on is exactly the identity a downstream reader could not
// tell from a real sample. It is refused as `VRM_VMC_PACKET_MALFORMED`, per
// message and never per packet, the same way the layer below refuses an
// argument form the protocol does not describe.
//
// What a VMC bone rotation *means* is worth stating because nothing here can
// check it. It is the sender's local rotation for that bone, which equals the
// rotation away from rest only when the sender's humanoid rest is identity. A
// sender whose rest is not identity produces samples that are correct as data
// and need a source rest pose to be applied to another avatar — which
// `vrmRetarget` already owns as `SourceRestPose` / `RestPoseCorrection`
// (MOTION_CONTRACT.md, Phase C). The adapter must not manufacture one from the
// first frame it happens to see; whether real senders need it is evidence
// Milestone B collects and this layer stays out of.
#pragma once

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/VmcMessage.h"
#include "vrmAdapterVmc/api.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <array>
#include <optional>
#include <string_view>

namespace vrmAdapterVmc
{

// The Unity `HumanBodyBones` spelling a VMC sender writes for `bone`, e.g.
// "LeftUpperArm". Empty for `HumanBone::Count`.
VRMADAPTERVMC_API std::string_view VmcHumanBoneName(
    motion::HumanBone bone) noexcept;

// The bone a VMC name denotes. Exact match on the Unity spelling; nullopt for
// anything else, including the VRM 1.0 spelling of the same bone.
VRMADAPTERVMC_API std::optional<motion::HumanBone> FindVmcHumanBone(
    std::string_view name) noexcept;

// The sender's axes into the canonical ones, with no validity check: a
// non-finite input converts to a non-finite output rather than being caught
// here, because these are the arithmetic and the mapping functions below are
// the boundary.
VRMADAPTERVMC_API pxr::GfVec3f ToCanonicalPosition(
    const std::array<float, 3>& position) noexcept;

// Also normalises. NaN components stay NaN, and a zero-length quaternion
// converts to a zero-length one — neither is repaired here.
VRMADAPTERVMC_API pxr::GfQuatf ToCanonicalRotation(
    const std::array<float, 4>& rotation) noexcept;

// One `/VMC/Ext/Bone/Pos` in canonical terms.
struct VmcBoneSample
{
    motion::HumanBone bone = motion::HumanBone::Count;

    // Local to the bone's parent in the sender's rig — see the header comment
    // on what that does and does not promise about rest.
    pxr::GfQuatf localRotation = pxr::GfQuatf(1.0f, pxr::GfVec3f(0.0f));

    // The sender's local offset for this bone, converted. For every bone but
    // the hips this is the sender's own rig geometry rather than motion, and
    // the canonical pose has nowhere to put it: `HumanoidPose` carries
    // rotations and one `RootMotion`, by the Phase A rule that only hips
    // translation is body translation. It is converted and handed on anyway,
    // because deciding what to do with the hips offset — drop it, or compose it
    // with `/VMC/Ext/Root/Pos` into one root position — needs the frame the
    // assembler owns and a real sender's evidence, and a converter that
    // silently discarded it would take that decision away.
    pxr::GfVec3f localPosition = pxr::GfVec3f(0.0f);
};

// `message` must be a `VmcMessageKind::BoneTransform`. Returns false and fills
// `diagnostic` for an unrecognised bone name (`VRM_VMC_UNSUPPORTED_MESSAGE`,
// with the name as its subject) or for a position or rotation that is not
// finite, or a rotation of zero length (`VRM_VMC_PACKET_MALFORMED`). `out` is
// left untouched on every failure.
VRMADAPTERVMC_API bool MapVmcBoneTransform(const VmcMessage& message,
                                           VmcBoneSample* out,
                                           Diagnostic* diagnostic = nullptr);

// `/VMC/Ext/Root/Pos` into `motion::RootMotion`: position and orientation are
// set and flagged, and the two velocity fields are left absent. VMC reports no
// velocity, and deriving one from consecutive frames is `LiveCaptureSource`'s
// `DeriveVelocity` intake policy — an adapter that did it here would be a
// second motion runtime (§2).
//
// `message` must be a `VmcMessageKind::RootTransform`; the failures are the
// bone function's, minus the name.
VRMADAPTERVMC_API bool MapVmcRootTransform(const VmcMessage& message,
                                           motion::RootMotion* out,
                                           Diagnostic* diagnostic = nullptr);

} // namespace vrmAdapterVmc
