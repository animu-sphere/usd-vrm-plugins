// SPDX-License-Identifier: Apache-2.0
//
// Whether a datagram is a frame, and whether the stream is still the same
// stream.
//
// Two things are checked here that no test below this layer could be:
//
// **Sequence.** Every layer below sees one datagram at a time and says so — the
// decoder's header states outright that a restart is something "no single packet
// can see". `frame-loss-60hz` was written for this file: it carries a transport
// gap, a duplicate delivery and a restart, and until now the only assertion any
// test could make about it was that all seven of its frames map the whole rig.
//
// **The three shapes a backwards clock has.** The corpus's restart goes backwards
// by 0.1 s, which is inside any jitter threshold worth having — so the assertion
// that it is a *restart* and not a regression is the one that pins the rule the
// header argues for, where a clock-only rule of the sibling's kind would fail it.
//
// The unit tests build `MotionPacket` values directly rather than bytes, for the
// reason `test_skeleton_map.cpp` gives: this layer takes decoded packets, and a
// test that went through the wire format would be re-testing the decoder and
// could not state a session the corpus does not contain.
//
// ## The two captures that differ by a rig
//
// `VRM_MOCOPI_FRAME_INCOMPLETE` is only meaningful against a rig, so pinning it
// took a capture the corpus did not have. `refused-bones-60hz` has the damaged
// frames — the decoder drops three bone records and the map loses three canonical
// bones to them — but it deliberately carries **no skeleton packet**, because
// `CheckRefusedBones` in the map's own corpus test needs a capture whose rig is
// undeclared. To this layer that file is two frames refused for having no rig,
// and the assertion below says exactly that rather than approximating.
//
// `incomplete-frame-60hz` is the same three damaged records with a skeleton
// packet in front and a clean frame behind. The pair is worth more than either
// alone: the decoder's and the map's corpus passes assert that the two captures
// decode and map **identically**, so the only thing that differs between them is
// the declared rig — and this is the layer where that difference becomes an
// incomplete frame rather than a refused one.
#include "vrmAdapterMocopi/FrameAssembler.h"

#include "vrmAdapterMocopi/MotionPacket.h"
#include "vrmAdapterMocopi/PacketCapture.h"
#include "vrmAdapterMocopi/SkeletonMap.h"

#include "fixtures.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace vrmAdapterMocopiTests;

using motion::HumanBone;
using vrmAdapterMocopi::BodyPlacementPolicy;
using vrmAdapterMocopi::BoneDefinition;
using vrmAdapterMocopi::BoneFrame;
using vrmAdapterMocopi::Diagnostic;
using vrmAdapterMocopi::DiagnosticCode;
using vrmAdapterMocopi::MeasuredBoneCount;
using vrmAdapterMocopi::MeasuredParentColumn;
using vrmAdapterMocopi::MocopiFrame;
using vrmAdapterMocopi::MocopiFrameAssembler;
using vrmAdapterMocopi::MocopiFrameConfig;
using vrmAdapterMocopi::MotionFrame;
using vrmAdapterMocopi::MotionPacket;
using vrmAdapterMocopi::MotionPacketKind;
using vrmAdapterMocopi::MotionSkeleton;

// The measured agreement between the two clocks: under 2 µs over 33 s
// (MotionPacket.h). Used as the bound for "these two clocks still agree", which
// makes the assertion the measurement rather than a number picked here.
constexpr double kClockAgreement = 2e-6;

std::size_t
Count(const std::vector<Diagnostic>& diagnostics, DiagnosticCode code)
{
    std::size_t total = 0;
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            ++total;
        }
    }
    return total;
}

// ---------------------------------------------------------------------------
// One datagram is one frame
// ---------------------------------------------------------------------------

void
TestOneDatagramIsOneFrame()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;

    // A skeleton packet is never a frame, whatever it did to the rig.
    assert(!assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics));
    assert(frames.empty());
    assert(assembler.GetSkeletonMap() != nullptr);
    assert(assembler.GetStats().skeletonsAccepted == 1);

    assert(assembler.Push(FrameAt(3000, 0.0), 0.1, &frames, &diagnostics));
    assert(frames.size() == 1);
    assert(diagnostics.empty());
    assert(frames[0].pose.validRotations.count() == kCanonicalBoneCount);
    assert(frames[0].pose.timestamp == 0.0);
    assert(frames[0].frameNumber == 3000);
    assert(frames[0].missing.none());
    assert(!frames[0].beginsNewSession);
    assert(frames[0].lostFrames == 0);

    // There is nothing held open, so there is nothing a caller could forget to
    // flush. The sibling's `Flush()` has no counterpart here and this is where
    // that is asserted rather than only described.
    assert(assembler.GetStats().framesEmitted == 1);
}

void
TestTheSessionMetadataIsProtocolOnly()
{
    MocopiFrameAssembler assembler;
    const motion::MotionSourceMetadata& metadata = assembler.GetSourceMetadata();
    assert(metadata.kind == motion::MotionSourceKind::LiveCapture);
    assert(metadata.protocol == "mocopi");
    // The only per-session identifier this protocol carries is `sndf/ipad`, and
    // it is treated as possibly device-identifying. Nothing may promote it into
    // provenance, which is something published.
    assert(metadata.sourceId.empty());
    assert(metadata.provider.empty());
}

// ---------------------------------------------------------------------------
// A frame with no rig behind it
// ---------------------------------------------------------------------------

void
TestAFrameBeforeAnyRigIsRefusedAndReportedOnce()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;

    for (std::uint32_t index = 0; index < 5; ++index) {
        assert(!assembler.Push(FrameAt(3000 + index, index / kFrameRate), 0.0,
                               &frames, &diagnostics));
    }
    assert(frames.empty());
    assert(assembler.GetStats().framesRefusedNoRig == 5);
    // Once per episode, not once per frame: a 60 Hz stream waiting up to 3.5 s
    // for its rig would otherwise bury the log in two hundred copies of it.
    assert(diagnostics.size() == 1);
    assert(diagnostics[0].code == DiagnosticCode::FrameIncomplete);

    // And the episode ends when the rig arrives.
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assert(assembler.Push(FrameAt(3005, 5.0 / kFrameRate), 0.0, &frames,
                          &diagnostics));
    assert(frames.size() == 1);
    assert(diagnostics.size() == 1);
}

void
TestARefusedSkeletonLeavesTheRigStanding()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);

    // Eleven joints: a different rig, which `MakeSkeletonMap` refuses.
    MotionPacket shortRig = SkeletonPacket();
    shortRig.skeleton->bones.resize(11);
    assert(!assembler.Push(shortRig, 0.0, &frames, &diagnostics));
    assert(assembler.GetStats().skeletonsRefused == 1);

    // The session keeps working: the device repeats the table every 3.5 s, so a
    // single corrupt skeleton packet costs nothing, where dropping the rig would
    // blind the session until the next one arrived.
    assert(assembler.GetSkeletonMap() != nullptr);
    assert(assembler.Push(FrameAt(3000, 0.0), 0.0, &frames, &diagnostics));
    assert(frames.size() == 1);
}

// ---------------------------------------------------------------------------
// The three shapes of a backwards clock
// ---------------------------------------------------------------------------

void
TestADuplicateDeliveryIsRefused()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assert(assembler.Push(FrameAt(3000, 0.0), 0.0, &frames, &diagnostics));

    // The same bytes arriving twice produce the same counter and the same clock.
    // A stream that emitted the same instant twice has not moved, and refusing it
    // is what keeps a duplicated datagram from becoming a duplicated pose.
    assert(!assembler.Push(FrameAt(3000, 0.0), 0.0, &frames, &diagnostics));
    assert(frames.size() == 1);
    assert(assembler.GetStats().framesRefusedOutOfOrder == 1);
    assert(assembler.GetStats().sessionRestarts == 0);
    assert(Count(diagnostics, DiagnosticCode::TimestampInvalid) == 1);
}

void
TestAClockThatDoesNotAdvanceIsRefused()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assert(assembler.Push(FrameAt(3000, 1.0), 0.0, &frames, &diagnostics));

    // A new frame whose clock went back a tenth of a second, with the counter
    // still advancing. Nothing says restart, so nothing invents one.
    assert(!assembler.Push(FrameAt(3001, 0.9), 0.0, &frames, &diagnostics));
    assert(frames.size() == 1);
    assert(assembler.GetStats().framesRefusedOutOfOrder == 1);
    assert(assembler.GetStats().sessionRestarts == 0);
}

void
TestAnAppRestartIsDetectedByTheCounter()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assembler.Push(FrameAt(3000, 0.0), 0.0, &frames, &diagnostics);
    assembler.Push(FrameAt(3001, 1.0 / kFrameRate), 0.0, &frames, &diagnostics);

    // The stream clock goes back by a sixtieth of a second — far inside the one
    // second threshold, so a clock-only rule of the sibling's kind reads this as
    // a regression and refuses the new session's every frame until its clock
    // passes the old one's. The counter is what makes it legible, and this is the
    // assertion that pins the header's argument for reading both.
    assert(assembler.GetConfig().restartBackwardsSeconds == 1.0);
    assembler.Push(FrameAt(1, 0.0), 0.0, &frames, &diagnostics);
    assert(assembler.GetStats().sessionRestarts == 1);
    assert(Count(diagnostics, DiagnosticCode::SourceRestarted) == 1);
    assert(assembler.GetStats().framesRefusedOutOfOrder == 0);
}

void
TestAStreamRestartIsDetectedByTheClock()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assembler.Push(FrameAt(3000, 0.0), 0.0, &frames, &diagnostics);
    assembler.Push(FrameAt(3120, 2.0), 0.0, &frames, &diagnostics);

    // The measured shape: `time` returns to 0.0 while `fnum` keeps rising,
    // because the counter counts the application's frames rather than the
    // stream's. Four of the five measured sessions began this way.
    assembler.Push(FramePacket(3121, 0.0, kEpoch + 30.0), 0.0, &frames,
                   &diagnostics);
    assert(assembler.GetStats().sessionRestarts == 1);
    assert(Count(diagnostics, DiagnosticCode::SourceRestarted) == 1);
}

void
TestARestartDropsTheRigAndTheFlagLandsOnTheNextRealFrame()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assembler.Push(FrameAt(3000, 0.0), 0.0, &frames, &diagnostics);
    assert(frames.size() == 1);

    // A restart drops the rig, so the frame that carried the restart is itself
    // refused: a skeleton packet is a body measurement and a restart is when a
    // recalibration happens.
    assert(!assembler.Push(FrameAt(1, 0.0), 0.0, &frames, &diagnostics));
    assert(assembler.GetSkeletonMap() == nullptr);
    assert(assembler.GetStats().framesRefusedNoRig == 1);
    assert(frames.size() == 1);

    // Still dark until the device repeats its table.
    assert(!assembler.Push(FrameAt(2, 1.0 / kFrameRate), 0.0, &frames,
                           &diagnostics));
    assert(assembler.GetStats().framesRefusedNoRig == 2);

    // And the flag outlives the frame it was raised on, landing on the first
    // frame the new session actually emits.
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assert(assembler.Push(FrameAt(3, 2.0 / kFrameRate), 0.0, &frames,
                          &diagnostics));
    assert(frames.size() == 2);
    assert(frames[1].beginsNewSession);

    // And it is raised once, not on every frame of the new session.
    assert(assembler.Push(FrameAt(4, 3.0 / kFrameRate), 0.0, &frames,
                          &diagnostics));
    assert(!frames[2].beginsNewSession);
}

// ---------------------------------------------------------------------------
// Transport loss
// ---------------------------------------------------------------------------

void
TestATransportGapIsCountedAndNotRefused()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assembler.Push(FrameAt(3000, 0.0), 0.0, &frames, &diagnostics);

    // The measured Wi-Fi loss shape: the counter jumps by three and the clock
    // jumps by exactly three sixtieths, because the sender's clock never skipped.
    assert(assembler.Push(FrameAt(3003, 3.0 / kFrameRate), 0.0, &frames,
                          &diagnostics));
    assert(frames.size() == 2);
    assert(frames[1].lostFrames == 2);
    assert(assembler.GetStats().framesLost == 2);

    // Counted, never diagnosed. The measurement calls a gap ordinary and the
    // frozen set has no code for it, so inventing one would be the wrong way to
    // earn a tenth code.
    assert(diagnostics.empty());
}

// ---------------------------------------------------------------------------
// Incomplete, and what this layer will not decide about it
// ---------------------------------------------------------------------------

void
TestAnIncompleteFrameIsEmittedAndReported()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);

    // Joint 20 is the left lower leg. Dropping its record costs that bone and
    // nothing below it: absence is reported, never propagated down a chain.
    MotionPacket damaged = FrameAt(3000, 0.0);
    DropJoint(&damaged, 20);
    assert(assembler.Push(damaged, 0.0, &frames, &diagnostics));

    // Emitted, not refused. Whether nineteen of twenty-two bones is a usable
    // frame is `MissingBonePolicy`'s answer one layer up, and an adapter that
    // dropped the frame would have taken it.
    assert(frames.size() == 1);
    assert(frames[0].missing.count() == 1);
    assert(frames[0].missing.test(static_cast<std::size_t>(
        HumanBone::LeftLowerLeg)));
    assert(frames[0].pose.validRotations.test(static_cast<std::size_t>(
        HumanBone::LeftFoot)));
    assert(assembler.GetStats().framesIncomplete == 1);
    assert(Count(diagnostics, DiagnosticCode::FrameIncomplete) == 1);
}

void
TestMissingIsMeasuredAgainstTheDeclaredRigAndNotTheHumanoid()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assembler.Push(FrameAt(3000, 0.0), 0.0, &frames, &diagnostics);

    // The rig carries twenty-two of the humanoid's bones and has no fingers. A
    // complete frame of it is complete, and reporting thirty-odd absent bones
    // sixty times a second would be reporting a rig as a fault.
    assert(frames[0].missing.none());
    assert(frames[0].pose.validRotations.count() == kCanonicalBoneCount);
    assert(kCanonicalBoneCount < motion::HumanBoneCount);
    assert(assembler.GetStats().framesIncomplete == 0);
}

void
TestAFrameThatFormsNoBoneIsRefused()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);

    MotionPacket empty = FrameAt(3000, 0.0);
    empty.frame->bones.clear();
    assert(!assembler.Push(empty, 0.0, &frames, &diagnostics));
    assert(frames.empty());
    assert(assembler.GetStats().framesRefusedEmpty == 1);
    // An absence of a pose rather than a sparse one: a caller that accepted it
    // would be publishing the previous frame's values as this frame's.
    assert(Count(diagnostics, DiagnosticCode::FrameIncomplete) == 1);
}

// ---------------------------------------------------------------------------
// What is carried beside the pose
// ---------------------------------------------------------------------------

void
TestTheHipsTranslationIsTheBodysRootMotion()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    MotionPacket moved = FrameAt(3000, 0.0);
    MoveHips(&moved, 0.25f, 0.90f, -1.50f);
    assembler.Push(moved, 0.0, &frames, &diagnostics);

    // The record, executed (MOTION_CONTRACT.md, "Root and hips"). The hips
    // translation is still on the frame — that is what the device sent — and it
    // is now also the pose's root, which is what was decided about it.
    assert(frames[0].hipsPosition.has_value());
    assert(std::fabs((*frames[0].hipsPosition)[0] - 0.25f) < 1e-6f);
    assert(std::fabs((*frames[0].hipsPosition)[2] + 1.50f) < 1e-6f);
    assert(frames[0].pose.root.hasPosition);
    // The same value and not a derived one: absolute, in the sender's space.
    assert(frames[0].pose.root.worldPosition == *frames[0].hipsPosition);

    // And the body's orientation, from the hips bone's own rotation. It stays
    // on the bone as well, which is what the recorded half does for a rig that
    // roots at its hips — the root path is one joint, so the composition down
    // it is that joint.
    assert(frames[0].pose.root.hasOrientation);
    assert(frames[0].pose.root.worldOrientation
           == frames[0].pose.localRotations[static_cast<std::size_t>(
               HumanBone::Hips)]);

    // The device reports no velocity and this layer derives none: that is the
    // intake's policy, and an assembler that did it would be a second runtime.
    assert(!frames[0].pose.root.hasLinearVelocity);
    assert(!frames[0].pose.root.hasAngularVelocity);
}

void
TestTheBodyPlacementPolicyIsWhatDecidesThat()
{
    // `None` is the shape every version of this adapter had before the record
    // was written, and it is reachable rather than historical: the sibling's
    // half of the record is still open, so a caller comparing this path against
    // one that composes nothing has to be able to ask for it.
    MocopiFrameConfig config;
    config.bodyPlacement = BodyPlacementPolicy::None;
    MocopiFrameAssembler assembler(config);
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    MotionPacket moved = FrameAt(3000, 0.0);
    MoveHips(&moved, 0.25f, 0.90f, -1.50f);
    assembler.Push(moved, 0.0, &frames, &diagnostics);

    assert(!frames[0].pose.root.hasPosition);
    assert(!frames[0].pose.root.hasOrientation);
    // The narrowing is of the pose and never of the measurement. A setting that
    // also dropped this would make the two policies differ in what the device
    // said rather than in what was decided about it.
    assert(frames[0].hipsPosition.has_value());
    assert(std::fabs((*frames[0].hipsPosition)[0] - 0.25f) < 1e-6f);
}

void
TestAFrameWithNoHipsRecordComposesNoRootPosition()
{
    // The hips record is what the position is read from, so a frame that lost
    // it has no placement to state — and stating one from the previous frame
    // would be the repair this assembler refuses to make everywhere else.
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assembler.Push(FrameAt(1, 0.0), 0.0, &frames, &diagnostics);
    frames.clear();

    MotionPacket rootless = FrameAt(2, 1.0 / kFrameRate);
    DropJoint(&rootless, 0);
    assembler.Push(rootless, 1.0 / kFrameRate, &frames, &diagnostics);

    assert(frames.size() == 1);
    assert(!frames[0].hipsPosition.has_value());
    assert(!frames[0].pose.root.hasPosition);
}

void
TestTheGrammarCarriesNoTrackingStateAndNoneIsInvented()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);

    MotionPacket damaged = FrameAt(3000, 0.0);
    DropJoint(&damaged, 20);
    assembler.Push(damaged, 0.0, &frames, &diagnostics);
    assembler.Push(FrameAt(3001, 1.0 / kFrameRate), 0.0, &frames, &diagnostics);

    // `VRM_MOCOPI_TRACKING_LOST` is frozen and unraised: the measured grammar
    // carries no per-joint confidence and no state field, so a bone that stopped
    // arriving is missing rather than untracked, and the two are not the same
    // claim.
    assert(Count(diagnostics, DiagnosticCode::TrackingLost) == 0);
    // And `confidence` stays absent for the same reason — an array of 1.0 would
    // be a claim the device never made.
    assert(!frames[0].pose.confidence.has_value());
    assert(!frames[0].pose.contacts.has_value());
    // This protocol has no expression channel at all.
    assert(frames[0].pose.expressions.IsEmpty());
}

// ---------------------------------------------------------------------------
// The two clocks
// ---------------------------------------------------------------------------

void
TestTheTwoClocksGiveADriftCheck()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);

    // A session whose two clocks agree, which is what the device sends: the
    // offset between them is the session's start instant and it is constant.
    for (std::uint32_t index = 0; index < 3; ++index) {
        assembler.Push(FrameAt(3000 + index, index / kFrameRate), 0.0, &frames,
                       &diagnostics);
    }
    assert(frames.size() == 3);
    // Zero on the first frame by construction: it is the frame that fixes the
    // offset. The rest agree with it to inside the measured 2 µs, and the
    // residue that is there is the binary32 stream clock against the binary64
    // absolute one rather than anything the sender did.
    assert(frames[0].clockDrift == 0.0);
    for (const MocopiFrame& frame : frames) {
        assert(std::fabs(frame.clockDrift) < kClockAgreement);
    }

    // A sender whose absolute clock stepped half a second while its stream clock
    // did not. Nothing refuses it — it is not a frame ordering question — and the
    // number is put where an operator can see it.
    assembler.Push(FramePacket(3003, 3.0 / kFrameRate,
                               kEpoch + 3.0 / kFrameRate + 0.5),
                   0.0, &frames, &diagnostics);
    assert(frames.size() == 4);
    assert(std::fabs(frames[3].clockDrift - 0.5) < 1e-6);
    assert(std::fabs(frames[3].senderUnixSeconds
                     - (kEpoch + 3.0 / kFrameRate + 0.5))
           < 1e-6);
}

void
TestResetDropsTheSessionAndKeepsTheStats()
{
    MocopiFrameAssembler assembler;
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assembler.Push(FrameAt(3000, 5.0), 0.0, &frames, &diagnostics);

    assembler.Reset();
    assert(assembler.GetSkeletonMap() == nullptr);
    // Stats describe the session the caller is judging rather than the stream's
    // state, so they survive.
    assert(assembler.GetStats().framesEmitted == 1);

    // And the clock history went with it: a frame that would have been backwards
    // against the old session is simply the first frame of this one.
    assembler.Push(SkeletonPacket(), 0.0, &frames, &diagnostics);
    assert(assembler.Push(FrameAt(1, 0.0), 0.0, &frames, &diagnostics));
    assert(assembler.GetStats().sessionRestarts == 0);
    assert(assembler.GetStats().framesRefusedOutOfOrder == 0);
}

// ---------------------------------------------------------------------------
// The corpus
// ---------------------------------------------------------------------------

struct AssembledCapture
{
    std::vector<MocopiFrame> frames;
    std::vector<Diagnostic> diagnostics;
    vrmAdapterMocopi::MocopiFrameStats stats;
};

int
Failed(const std::string& name, const std::string& detail)
{
    std::fprintf(stderr, "%s: %s\n", name.c_str(), detail.c_str());
    return 1;
}

int
CheckNeutralStanding(const AssembledCapture& capture, const std::string& name)
{
    // The happy path: a rig, then five frames that each carry all of it.
    if (capture.stats.skeletonsAccepted != 1 || capture.frames.size() != 5) {
        return Failed(name, "a rig and five assembled frames were expected");
    }
    for (const MocopiFrame& frame : capture.frames) {
        if (frame.missing.any() || frame.lostFrames != 0
            || frame.beginsNewSession) {
            return Failed(name, "an unremarkable frame was reported as one");
        }
        if (frame.pose.validRotations.count() != kCanonicalBoneCount) {
            return Failed(name, "a frame did not carry the whole rig");
        }
        if (std::fabs(frame.clockDrift) >= kClockAgreement) {
            return Failed(name, "the two clocks disagree by more than measured");
        }
    }
    // Sixty hertz, and the stream clock starts at zero.
    if (capture.frames[0].pose.timestamp != 0.0) {
        return Failed(name, "the stream clock did not start at zero");
    }
    if (!capture.diagnostics.empty()) {
        return Failed(name, "a clean session produced a diagnostic");
    }
    return 0;
}

int
CheckArmsLowered(const AssembledCapture& capture, const std::string& name)
{
    if (capture.stats.skeletonsAccepted != 1 || capture.frames.size() != 3) {
        return Failed(name, "a rig and three assembled frames were expected");
    }
    // The generator moves the root every frame, which is the one translation
    // this rig sends. It reaches the caller twice: as the body's placement on
    // the frame, and as the pose's root motion, which is the same value under
    // the policy the record chose.
    for (const MocopiFrame& frame : capture.frames) {
        if (!frame.hipsPosition || !frame.pose.root.hasPosition) {
            return Failed(name, "the hips translation did not reach a "
                                "RootMotion");
        }
        if (frame.pose.root.worldPosition != *frame.hipsPosition) {
            return Failed(name, "the root position was not the hips "
                                "translation");
        }
        if (!frame.pose.root.hasOrientation) {
            return Failed(name, "the hips rotation did not reach the root's "
                                "orientation");
        }
    }
    if (!((*capture.frames[0].hipsPosition)[1]
          > (*capture.frames[2].hipsPosition)[1])) {
        return Failed(name, "the body's placement did not move");
    }
    // And the movement survives the composition. Comparing the frame against
    // itself would pass on a root that was authored once and then held.
    if (!(capture.frames[0].pose.root.worldPosition[1]
          > capture.frames[2].pose.root.worldPosition[1])) {
        return Failed(name, "the root motion did not move");
    }
    return 0;
}

int
CheckFrameLoss(const AssembledCapture& capture, const std::string& name)
{
    // The capture this file was waiting for. Seven frame datagrams, of which
    // five are frames, one is a duplicate delivery and one is a restart.
    if (capture.frames.size() != 5) {
        return Failed(name, "five of the seven frame datagrams were expected to "
                            "become frames");
    }
    // The measured Wi-Fi shape: `fnum` jumps by three and the clock jumps by
    // exactly three sixtieths, so two datagrams were lost and no frame was.
    if (capture.frames[3].lostFrames != 2 || capture.stats.framesLost != 2) {
        return Failed(name, "the transport gap was not counted as two lost "
                            "datagrams");
    }
    if (Count(capture.diagnostics, DiagnosticCode::PacketMalformed) != 0) {
        return Failed(name, "a lost datagram was reported as a defect");
    }
    // The duplicate delivery.
    if (capture.stats.framesRefusedOutOfOrder != 1
        || Count(capture.diagnostics, DiagnosticCode::TimestampInvalid) != 1) {
        return Failed(name, "the duplicate delivery was not refused exactly "
                            "once");
    }
    // The restart, and the assertion this whole layer's rule exists for: the
    // stream clock goes back by only a tenth of a second here, so a clock-only
    // threshold reads it as a regression. The counter is what makes it a
    // restart.
    if (capture.stats.sessionRestarts != 1
        || Count(capture.diagnostics, DiagnosticCode::SourceRestarted) != 1) {
        return Failed(name, "the restart was not detected");
    }
    // And it dropped the rig, so the restart's own frame waits for a skeleton
    // packet the capture ends before sending.
    if (capture.stats.framesRefusedNoRig != 1) {
        return Failed(name, "the restart did not drop the session's rig");
    }
    return 0;
}

int
CheckSessionRestart(const AssembledCapture& capture, const std::string& name)
{
    // `frame-loss-01` restarts and stops; this one restarts and recovers, which
    // is the half of a restart that capture cannot show. Three frames, two
    // refused for want of a rig, then a rig again and two more.
    if (capture.stats.skeletonsAccepted != 2 || capture.frames.size() != 5) {
        return Failed(name, "two rigs and five assembled frames were expected");
    }
    if (capture.stats.sessionRestarts != 1
        || Count(capture.diagnostics, DiagnosticCode::SourceRestarted) != 1) {
        return Failed(name, "the restart was not detected exactly once");
    }
    // The cost, measured: the rig went with the old session and both frames
    // that arrived before the new one was declared were refused. Reported once
    // per episode, not twice.
    if (capture.stats.framesRefusedNoRig != 2
        || Count(capture.diagnostics, DiagnosticCode::FrameIncomplete) != 1) {
        return Failed(name, "the rig's loss was not counted twice and reported "
                            "once");
    }
    // The flag outlives the frames it was refused on and lands on the first
    // frame the new session actually emits — which is the frame a consumer can
    // act on, and the only one it could.
    if (capture.frames[3].beginsNewSession != true
        || capture.frames[2].beginsNewSession
        || capture.frames[4].beginsNewSession) {
        return Failed(name, "the new session's flag did not land on its first "
                            "emitted frame");
    }
    // The two streams are ordered against each other only by their own clocks,
    // and the new one is behind: that is what the layer above has to decide
    // about, and it is a property of these bytes rather than of the assembler.
    if (!(capture.frames[2].pose.timestamp > capture.frames[3].pose.timestamp)) {
        return Failed(name, "the new session's clock is not behind the old "
                            "session's");
    }
    // A restart is not a gap: the counter's difference across one means nothing
    // and is not counted as loss.
    if (capture.stats.framesLost != 0 || capture.frames[3].lostFrames != 0) {
        return Failed(name, "a restart was counted as transport loss");
    }
    // And the new session fixes its own clock offset, so drift is measured
    // within a session rather than across the discontinuity.
    if (std::fabs(capture.frames[3].clockDrift) >= kClockAgreement) {
        return Failed(name, "the new session did not re-fix its clock offset");
    }
    return 0;
}

int
CheckRefusedBones(const AssembledCapture& capture, const std::string& name)
{
    // The corpus's near miss, stated as an assertion so it stops being true
    // loudly if a skeleton packet is ever added to this capture. The two frames
    // here are the damaged one and its clean twin, and to this layer they are
    // both frames of a session that has not declared a rig.
    if (capture.stats.skeletonsAccepted != 0
        || capture.stats.framesRefusedNoRig != 2
        || !capture.frames.empty()) {
        return Failed(name, "a capture with no skeleton packet produced a "
                            "frame");
    }
    // Once per episode.
    if (capture.diagnostics.size() != 1) {
        return Failed(name, "the missing rig was not reported exactly once");
    }
    return 0;
}

int
CheckIncompleteFrame(const AssembledCapture& capture, const std::string& name)
{
    // The capture this layer's `FRAME_INCOMPLETE` was missing. It is
    // `refused-bones-60hz`'s damage with a rig declared in front of it, and the
    // whole assertion is the difference that rig makes: the decoder and the map
    // treat the two identically (their corpus passes say so), and only here do
    // they diverge.
    if (capture.stats.skeletonsAccepted != 1 || capture.frames.size() != 2) {
        return Failed(name, "a rig and two assembled frames were expected");
    }

    // Emitted, not refused. Whether nineteen of twenty-two bones is usable is
    // `MissingBonePolicy`'s answer one layer up.
    const MocopiFrame& damaged = capture.frames[0];
    if (damaged.missing.count() != 3
        || damaged.pose.validRotations.count() != kCanonicalBoneCount - 3) {
        return Failed(name, "three refused records did not cost three bones");
    }
    const HumanBone missing[] = {HumanBone::UpperChest, HumanBone::Head,
                                 HumanBone::LeftLowerLeg};
    for (const HumanBone bone : missing) {
        if (!damaged.missing.test(static_cast<std::size_t>(bone))) {
            return Failed(name, "a bone whose path lost a joint is not "
                                "reported missing");
        }
    }
    if (capture.stats.framesIncomplete != 1
        || Count(capture.diagnostics, DiagnosticCode::FrameIncomplete) != 1) {
        return Failed(name, "the incomplete frame was not reported exactly "
                            "once");
    }

    // And the session recovers: the incompleteness belongs to the damaged
    // datagram rather than to the session, which is what the clean frame after
    // it is for. `missing` is measured against the declared rig, so a rig-wide
    // fault and a one-frame fault could not otherwise be told apart.
    if (capture.frames[1].missing.any()
        || capture.frames[1].pose.validRotations.count()
               != kCanonicalBoneCount) {
        return Failed(name, "the session did not recover on the clean frame");
    }
    // Nothing here is out of order or lost — the damage is inside a frame, not
    // between two.
    if (capture.stats.framesRefusedOutOfOrder != 0
        || capture.stats.framesRefusedEmpty != 0
        || capture.stats.framesLost != 0) {
        return Failed(name, "a bone-scoped fault was read as a sequence fault");
    }
    return 0;
}

int
CheckExtendedForm(const AssembledCapture& capture, const std::string& name)
{
    // The eleven-bone rig is refused, and no other skeleton packet arrives — so
    // this session never declares a rig at all and none of its three frames can
    // be read. The frame that *does* map in the skeleton map's own corpus test
    // maps only because that test pre-seeds the measured rig; nothing here does.
    if (capture.stats.skeletonsAccepted != 0
        || capture.stats.skeletonsRefused != 1) {
        return Failed(name, "a rig this map cannot read was accepted");
    }
    if (!capture.frames.empty() || capture.stats.framesRefusedNoRig != 3) {
        return Failed(name, "a frame was assembled without a rig");
    }
    if (Count(capture.diagnostics, DiagnosticCode::FrameIncomplete) != 1) {
        return Failed(name, "the missing rig was not reported exactly once");
    }
    return 0;
}

int
CheckNothingAssembles(const AssembledCapture& capture, const std::string& name)
{
    // Every datagram in the two malformed captures is refused below this layer,
    // so nothing reaches it. Registered rather than skipped, because "this
    // capture pins nothing here" is a claim that can stop being true.
    if (!capture.frames.empty() || capture.stats.skeletonsAccepted != 0
        || capture.stats.framesRefusedNoRig != 0) {
        return Failed(name, "a malformed capture produced a rig or a frame");
    }
    return 0;
}

int
CheckCorpus(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        // `is_regular_file` as well as the extension, so a directory that
        // happens to be named like a capture is not read as one — the guard the
        // other passes over this directory already use.
        if (entry.is_regular_file()
            && entry.path().extension() == ".mocopipackets") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::fprintf(stderr, "no captures in %s\n", directory.string().c_str());
        return 1;
    }

    int failures = 0;
    for (const std::filesystem::path& file : files) {
        const std::string name = file.filename().string();
        vrmAdapterMocopi::PacketCapture capture;
        vrmAdapterMocopi::PacketCaptureError error;
        if (!vrmAdapterMocopi::ReadPacketCaptureFile(file.string(), &capture,
                                                     &error)) {
            failures += Failed(name, "line " + std::to_string(error.line) + ": "
                                         + error.message);
            continue;
        }

        AssembledCapture assembled;
        MocopiFrameAssembler assembler;
        assembler.SetSource(capture.sourceId);
        for (const vrmAdapterMocopi::RecordedDatagram& datagram :
             capture.datagrams) {
            MotionPacket packet;
            // The decoder's own diagnostics are not this test's subject: its
            // corpus mode already pins them, and mixing the two lists would make
            // a refusal here indistinguishable from one there.
            if (!vrmAdapterMocopi::DecodeMotionPacket(datagram.bytes, &packet)) {
                continue;
            }
            assembler.Push(packet, datagram.receiveTime, &assembled.frames,
                           &assembled.diagnostics);
        }
        assembled.stats = assembler.GetStats();

        int result = 0;
        if (capture.sourceId == "neutral-standing-01") {
            result = CheckNeutralStanding(assembled, name);
        } else if (capture.sourceId == "arms-lowered-01") {
            result = CheckArmsLowered(assembled, name);
        } else if (capture.sourceId == "frame-loss-01") {
            result = CheckFrameLoss(assembled, name);
        } else if (capture.sourceId == "session-restart-01") {
            result = CheckSessionRestart(assembled, name);
        } else if (capture.sourceId == "refused-bones-01") {
            result = CheckRefusedBones(assembled, name);
        } else if (capture.sourceId == "incomplete-frame-01") {
            result = CheckIncompleteFrame(assembled, name);
        } else if (capture.sourceId == "extended-form-01") {
            result = CheckExtendedForm(assembled, name);
        } else if (capture.sourceId == "malformed-container-01"
                   || capture.sourceId == "malformed-packets-01") {
            result = CheckNothingAssembles(assembled, name);
        } else {
            result = Failed(name, "no assertion is registered for sourceId '"
                                      + capture.sourceId + "'");
        }

        // Frozen and unraised everywhere, not just in the unit tests: the
        // measured grammar carries no tracking state, so no capture can produce
        // one.
        if (result == 0
            && Count(assembled.diagnostics, DiagnosticCode::TrackingLost) != 0) {
            result = Failed(name, "a capture raised a tracking state the "
                                  "grammar does not carry");
        }

        failures += result;
        if (result == 0) {
            std::printf("%s: %zu frame(s) assembled\n", name.c_str(),
                        assembled.frames.size());
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("mocopi frame assembler corpus: %zu capture(s) verified\n",
                files.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestOneDatagramIsOneFrame();
    TestTheSessionMetadataIsProtocolOnly();
    TestAFrameBeforeAnyRigIsRefusedAndReportedOnce();
    TestARefusedSkeletonLeavesTheRigStanding();
    TestADuplicateDeliveryIsRefused();
    TestAClockThatDoesNotAdvanceIsRefused();
    TestAnAppRestartIsDetectedByTheCounter();
    TestAStreamRestartIsDetectedByTheClock();
    TestARestartDropsTheRigAndTheFlagLandsOnTheNextRealFrame();
    TestATransportGapIsCountedAndNotRefused();
    TestAnIncompleteFrameIsEmittedAndReported();
    TestMissingIsMeasuredAgainstTheDeclaredRigAndNotTheHumanoid();
    TestAFrameThatFormsNoBoneIsRefused();
    TestTheHipsTranslationIsTheBodysRootMotion();
    TestTheBodyPlacementPolicyIsWhatDecidesThat();
    TestAFrameWithNoHipsRecordComposesNoRootPosition();
    TestTheGrammarCarriesNoTrackingStateAndNoneIsInvented();
    TestTheTwoClocksGiveADriftCheck();
    TestResetDropsTheSessionAndKeepsTheStats();
    std::puts("vrmAdapterMocopi frame assembler tests passed");
    return 0;
}
