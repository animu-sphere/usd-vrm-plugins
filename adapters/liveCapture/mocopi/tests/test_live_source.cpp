// SPDX-License-Identifier: Apache-2.0
//
// Where the adapter ends and the motion runtime begins.
//
// Two things are checked here that no test below this layer could be:
//
// **That the two halves meet.** Every layer below produces something and stops;
// this is the first one whose output a consumer samples. The claim the corpus
// pass makes is cross-layer and is the reason this file exists: *every frame the
// assembler emitted was admitted by the intake*, because the assembler emits
// strictly advancing frames within a session and that is exactly the ordering
// `LiveCaptureSource::Push` requires. Neither layer's own tests can state that —
// one does not know what the intake requires and the other has never seen this
// protocol.
//
// **What a restart costs, as a choice rather than as a behaviour.** The
// assembler reports a restart and refuses to repair it; this is the layer that
// decides what that means, and the two settings are replayed over the same bytes
// so the cost is recorded rather than described.
//
// ## The capture this file needed, and why the corpus did not already have one
//
// `frame-loss-60hz` restarts and *stops*: its new session never declares a rig,
// so no frame of it is ever emitted. Every layer up to the assembler is fully
// exercised by that — the restart is detected, the rig dropped, the frames
// refused — and this layer is left with nothing to act on, because it can only
// act on a restart that reaches a frame. Replayed under both policies that
// capture produces identical numbers, which is a true statement about it and a
// useless one about the policy.
//
// `session-restart-60hz` is the same event with the recovery attached: the rig
// is re-declared and the session resumes. It is the device's real shape rather
// than a contrivance — the rest table is repeated about every 3.5 s — and it is
// the only capture in the corpus that can tell `Reset` from `Refuse`.
//
// The unit tests build `MotionPacket` values directly rather than bytes, for the
// reason `test_frame_assembler.cpp` gives: this layer takes decoded packets, and
// a test that went through the wire format would be re-testing the decoder and
// could not state a session the corpus does not contain. The one claim that
// genuinely needs bytes — that a datagram need not outlive the call — is made in
// the corpus pass, which replays every capture through a single reused buffer.
#include "vrmAdapterMocopi/LiveSource.h"

#include "vrmAdapterMocopi/MotionPacket.h"
#include "vrmAdapterMocopi/PacketCapture.h"
#include "vrmAdapterMocopi/SkeletonMap.h"

#include "motionCore/Compare.h"

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

using motion::HumanBone;
using vrmAdapterMocopi::BoneDefinition;
using vrmAdapterMocopi::BoneFrame;
using vrmAdapterMocopi::Diagnostic;
using vrmAdapterMocopi::DiagnosticCode;
using vrmAdapterMocopi::MeasuredBoneCount;
using vrmAdapterMocopi::MeasuredParentColumn;
using vrmAdapterMocopi::MocopiFrame;
using vrmAdapterMocopi::MocopiLiveSource;
using vrmAdapterMocopi::MocopiLiveSourceConfig;
using vrmAdapterMocopi::MotionFrame;
using vrmAdapterMocopi::MotionPacket;
using vrmAdapterMocopi::MotionPacketKind;
using vrmAdapterMocopi::MotionSkeleton;
using vrmAdapterMocopi::SessionRestartPolicy;

// The generator's invented offsets, in the same order and for the same reason
// the two tests below this one keep them: what a pose is compared against is a
// stated table rather than a fixture agreeing with itself.
const float kRestOffsets[MeasuredBoneCount][3] = {
    {0.0f, 0.90f, 0.0f},     {0.0f, 0.06f, 0.0f},   {0.0f, 0.06f, 0.0f},
    {0.0f, 0.06f, 0.0f},     {0.0f, 0.06f, 0.0f},   {0.0f, 0.06f, 0.0f},
    {0.0f, 0.06f, 0.0f},     {0.0f, 0.10f, 0.0f},   {0.0f, 0.05f, 0.0f},
    {0.0f, 0.05f, 0.0f},     {0.0f, 0.05f, 0.0f},   {0.02f, -0.08f, 0.08f},
    {0.14f, 0.0f, 0.0f},     {0.30f, 0.0f, 0.0f},   {0.25f, 0.0f, 0.0f},
    {-0.02f, -0.08f, 0.08f}, {-0.14f, 0.0f, 0.0f},  {-0.30f, 0.0f, 0.0f},
    {-0.25f, 0.0f, 0.0f},    {0.09f, -0.05f, 0.0f}, {0.0f, -0.40f, 0.0f},
    {0.0f, -0.42f, 0.0f},    {0.0f, -0.10f, 0.13f}, {-0.09f, -0.05f, 0.0f},
    {0.0f, -0.40f, 0.0f},    {0.0f, -0.42f, 0.0f},  {0.0f, -0.10f, 0.13f},
};

constexpr std::size_t kCanonicalBoneCount = 22;
constexpr double kFrameRate = 60.0;
constexpr double kEpoch = 1786492800.0;

// `motionCore`'s, never a number picked here (Compare.h).
constexpr motion::MotionTolerance kTolerance{};

std::array<float, 4>
WireIdentity()
{
    return {{0.0f, 0.0f, 0.0f, 1.0f}};
}

std::array<float, 3>
RestOffset(std::size_t jointId)
{
    return {{kRestOffsets[jointId][0], kRestOffsets[jointId][1],
             kRestOffsets[jointId][2]}};
}

MotionPacket
SkeletonPacket()
{
    MotionSkeleton skeleton;
    for (std::size_t jointId = 0; jointId < MeasuredBoneCount; ++jointId) {
        BoneDefinition joint;
        joint.boneId = static_cast<std::uint16_t>(jointId);
        joint.parentBoneId = MeasuredParentColumn[jointId];
        joint.restTransform.rotation = WireIdentity();
        joint.restTransform.translation = RestOffset(jointId);
        skeleton.bones.push_back(joint);
    }
    MotionPacket packet;
    packet.kind = MotionPacketKind::Skeleton;
    packet.skeleton = std::move(skeleton);
    return packet;
}

MotionPacket
FramePacket(std::uint32_t frameNumber, double streamSeconds,
            double senderUnixSeconds)
{
    MotionFrame frame;
    frame.frameNumber = frameNumber;
    frame.streamSeconds = static_cast<float>(streamSeconds);
    frame.senderUnixSeconds = senderUnixSeconds;
    for (std::size_t jointId = 0; jointId < MeasuredBoneCount; ++jointId) {
        BoneFrame bone;
        bone.boneId = static_cast<std::uint16_t>(jointId);
        bone.transform.rotation = WireIdentity();
        bone.transform.translation = RestOffset(jointId);
        frame.bones.push_back(bone);
    }
    MotionPacket packet;
    packet.kind = MotionPacketKind::Frame;
    packet.frame = std::move(frame);
    return packet;
}

MotionPacket
FrameAt(std::uint32_t frameNumber, double streamSeconds)
{
    return FramePacket(frameNumber, streamSeconds, kEpoch + streamSeconds);
}

// Removes a joint's record, so the bones on its path cannot be formed.
void
DropJoint(MotionPacket* packet, std::uint16_t boneId)
{
    std::vector<BoneFrame>& bones = packet->frame->bones;
    bones.erase(std::remove_if(bones.begin(), bones.end(),
                               [boneId](const BoneFrame& bone) {
                                   return bone.boneId == boneId;
                               }),
                bones.end());
}

// ---------------------------------------------------------------------------
// The path, end to end
// ---------------------------------------------------------------------------

void
TestAFrameBecomesASampleablePose()
{
    MocopiLiveSource source;
    // Nothing has arrived, so there is nothing to answer with — the runtime's
    // own status, reported through this class rather than invented by it.
    assert(source.Sample(0.0).status == motion::PoseSampleStatus::Unavailable);
    assert(!source.GetSkeletonMap());

    assert(source.PushPacket(SkeletonPacket(), 0.0) == 0);
    // A skeleton packet is a rig and never a pose.
    assert(source.GetSkeletonMap());
    assert(source.Sample(0.0).status == motion::PoseSampleStatus::Unavailable);

    assert(source.PushPacket(FrameAt(1, 0.0), 0.0) == 1);
    const motion::PoseSampleResult result = source.Sample(0.0);
    assert(result.status == motion::PoseSampleStatus::Sampled);
    assert(result.pose);
    assert(result.pose->validRotations.count() == kCanonicalBoneCount);
}

void
TestTheBridgeContributesNoArithmetic()
{
    // The pose a consumer samples at a frame's own instant is the same *motion*
    // the assembler produced. Smoothing is off by default and the sample lands
    // exactly on an observed frame, so anything that differed here would be
    // arithmetic this class added.
    MocopiLiveSource source;
    source.PushPacket(SkeletonPacket(), 0.0);
    source.PushPacket(FrameAt(1, 0.0), 0.0);

    assert(source.GetFramesFromLastPush().size() == 1);
    const motion::HumanoidPose assembled = source.GetFramesFromLastPush()[0].pose;
    const motion::PoseSampleResult result = source.Sample(0.0);
    assert(result.pose);
    assert(motion::NearlyEqual(*result.pose, assembled, kTolerance));
    assert(result.pose->timestamp == assembled.timestamp);

    // `NearlyEqual` and not `==`, and the difference is the point rather than a
    // looser test: the two poses differ in exactly one field, and it is the one
    // field this layer is *supposed* to add. The intake stamps each pose with
    // the session's provenance as it buffers it, so the assembler's pose is
    // unlabelled and the sampled one says where it came from — a difference in
    // the recorded value and not in the motion, which is the distinction
    // Compare.h exists to draw. Asserted both ways so that "only provenance
    // changed" is checked rather than assumed.
    assert(!(*result.pose == assembled));
    motion::HumanoidPose labelled = assembled;
    labelled.source = source.GetSourceMetadata();
    assert(*result.pose == labelled);
}

void
TestTheMissingBonePolicyIsTheIntakes()
{
    // The same input under the two policies, so the answer visibly changes with
    // the runtime's configuration rather than with the adapter. An adapter that
    // resolved a missing bone itself would make these two identical.
    const auto run = [](motion::MissingBonePolicy policy) {
        MocopiLiveSourceConfig config;
        config.intake.missingBones = policy;
        MocopiLiveSource source(config);
        source.PushPacket(SkeletonPacket(), 0.0);
        source.PushPacket(FrameAt(1, 0.0), 0.0);

        // The second frame loses the head joint, so the bones on its path
        // cannot be formed.
        MotionPacket damaged = FrameAt(2, 1.0 / kFrameRate);
        DropJoint(&damaged, 10);
        source.PushPacket(damaged, 1.0 / kFrameRate);

        // Sampled at the frame's *stored* instant rather than at the nominal
        // `1/60`, and the difference is not pedantry. `time` is binary32 on the
        // wire, so the stored value is a hair above the double a consumer
        // computes — and a request that falls between two samples is
        // interpolated, where `LerpPose` carries a bone present in either
        // endpoint into the result. Against a complete frame followed by a
        // damaged one that union restores the missing bone under *both*
        // policies, and the test would pass while measuring nothing. Landing on
        // the sample is what makes the intake's answer the one being read.
        const double stored = source.GetFramesFromLastPush()[0].pose.timestamp;
        const motion::PoseSampleResult result = source.Sample(stored);
        assert(result.status == motion::PoseSampleStatus::Sampled);
        assert(result.pose);
        return result.pose->validRotations.count();
    };

    // Held: the head keeps the rotation the first frame gave it. Unbound: it is
    // gone, and downstream is free to fall back to the target's rest.
    assert(run(motion::MissingBonePolicy::HoldLast) == kCanonicalBoneCount);
    assert(run(motion::MissingBonePolicy::LeaveUnbound) == kCanonicalBoneCount - 1);
}

void
TestProvenanceIsToldOnceAndNeverChanges()
{
    MocopiLiveSource source;
    const motion::MotionSourceMetadata before = source.GetSourceMetadata();
    assert(before.kind == motion::MotionSourceKind::LiveCapture);
    assert(before.protocol == "mocopi");
    // Deliberately empty, and asserted rather than left to drift: the only
    // per-session identifier this protocol carries is unidentified and possibly
    // device-identifying, and provenance is something published.
    assert(before.sourceId.empty());
    assert(before.provider.empty());

    source.PushPacket(SkeletonPacket(), 0.0);
    source.PushPacket(FrameAt(1, 0.0), 0.0);
    // This protocol has no handshake, so there is nothing a session can learn
    // about itself later. The sibling's per-frame metadata comparison has no
    // work to do here, and this is the assertion that says so.
    assert(source.GetSourceMetadata() == before);

    // `SetSource` names the endpoint for diagnostics and is not provenance.
    source.SetSource("0.0.0.0:12351");
    assert(source.GetSourceMetadata() == before);
}

// ---------------------------------------------------------------------------
// The one decision this layer takes
// ---------------------------------------------------------------------------

// A session, a restart, and a rig re-declared — the shape `session-restart-01`
// records, driven here from packets so the policies can be compared without a
// file.
std::size_t
RunRestart(SessionRestartPolicy policy, MocopiLiveSource* source)
{
    source->SetRestartPolicy(policy);
    std::size_t admitted = 0;
    source->PushPacket(SkeletonPacket(), 0.0);
    for (std::uint32_t index = 0; index < 3; ++index) {
        const double seconds = 20.0 + index / kFrameRate;
        admitted += source->PushPacket(FrameAt(4000 + index, seconds), seconds);
    }
    // The restart: the counter goes back and the clock returns to zero.
    admitted += source->PushPacket(FrameAt(1, 0.0), 0.0);
    admitted += source->PushPacket(FrameAt(2, 1.0 / kFrameRate), 0.0);
    // The new session declares its rig, and is a session again.
    source->PushPacket(SkeletonPacket(), 0.0);
    for (std::uint32_t index = 2; index < 4; ++index) {
        const double seconds = index / kFrameRate;
        admitted += source->PushPacket(FrameAt(1 + index, seconds), seconds);
    }
    return admitted;
}

void
TestARestartUnderResetContinuesTheStream()
{
    MocopiLiveSource source;
    const std::size_t admitted = RunRestart(SessionRestartPolicy::Reset, &source);
    // Three frames of the old session and two of the new. The two in between
    // reached no frame at all: the rig went with the restart, and the assembler
    // refused them for want of one.
    assert(admitted == 5);
    assert(source.GetStats().framesDelivered == 5);
    assert(source.GetStats().framesRefused == 0);
    assert(source.GetStats().sessionsReset == 1);
    assert(source.GetAssembler().GetStats().framesRefusedNoRig == 2);
}

void
TestARestartUnderRefuseStopsTheStream()
{
    MocopiLiveSource source;
    const std::size_t admitted = RunRestart(SessionRestartPolicy::Refuse, &source);
    // The old session's three, and nothing after: the new stream's clock begins
    // at zero, which is behind every frame the intake holds, so it refuses them
    // and the session visibly stops.
    assert(admitted == 3);
    assert(source.GetStats().framesDelivered == 5);
    assert(source.GetStats().framesRefused == 2);
    assert(source.GetStats().sessionsReset == 0);
    // The restart was still *seen*, which is what makes the stall diagnosable
    // rather than mysterious.
    assert(source.GetAssembler().GetStats().sessionRestarts == 1);
}

void
TestTheRestartLatchFiresOnTheFrameAndNotOnTheDetection()
{
    // The finding this layer paid for. On this protocol a restart is detected
    // and then the session is dark until a skeleton packet arrives — up to 3.5 s
    // on a real device — so "the restart happened" and "the new session has a
    // pose" are different instants, where on the sibling they are the same one.
    //
    // The latch has to fire on the second, because `AlignClock` pins the newest
    // *buffered* frame: firing at detection would pin the dead session's head.
    MocopiLiveSource source;
    source.PushPacket(SkeletonPacket(), 0.0);
    source.PushPacket(FrameAt(4000, 20.0), 20.0);
    assert(!source.ConsumeSessionRestart());

    // Detected here — the assembler says so — and no frame came of it.
    source.PushPacket(FrameAt(1, 0.0), 0.0);
    assert(source.GetAssembler().GetStats().sessionRestarts == 1);
    assert(source.GetFramesFromLastPush().empty());
    assert(!source.ConsumeSessionRestart());

    // Still nothing: the rig has not been re-declared.
    source.PushPacket(FrameAt(2, 1.0 / kFrameRate), 0.0);
    assert(!source.ConsumeSessionRestart());

    // The rig arrives, the next frame is emitted, and the flag lands on it.
    source.PushPacket(SkeletonPacket(), 0.0);
    source.PushPacket(FrameAt(3, 2.0 / kFrameRate), 0.0);
    assert(source.GetFramesFromLastPush().size() == 1);
    assert(source.GetFramesFromLastPush()[0].beginsNewSession);
    assert(source.ConsumeSessionRestart());
    // Consumed exactly once.
    assert(!source.ConsumeSessionRestart());
}

void
TestTheClockOffsetIsHandedBackRatherThanRepaired()
{
    // The reason the latch exists. A restart invalidates the intake's clock
    // offset — measured against a clock that no longer exists — and
    // `LiveCaptureSource::Reset` deliberately keeps it. Only the consumer knows
    // the evaluation time to re-align to.
    MocopiLiveSource source;
    source.PushPacket(SkeletonPacket(), 0.0);
    source.PushPacket(FrameAt(4000, 20.0), 20.0);
    // A consumer whose own clock reads zero when the stream reads 20.
    source.GetIntake().AlignClock(0.0);
    assert(source.Sample(0.0).status == motion::PoseSampleStatus::Sampled);

    source.PushPacket(FrameAt(1, 0.0), 0.0);
    source.PushPacket(SkeletonPacket(), 0.0);
    source.PushPacket(FrameAt(2, 1.0 / kFrameRate), 0.0);

    // Nothing repaired the offset, so the source now reads as far ahead of its
    // data: the stale offset is still adding twenty seconds.
    const motion::PoseSampleResult stale = source.Sample(0.0);
    assert(stale.status != motion::PoseSampleStatus::Sampled);

    // The latch is what a consumer acts on, and re-aligning is all it takes.
    assert(source.ConsumeSessionRestart());
    assert(source.GetIntake().AlignClock(0.0));
    assert(source.Sample(0.0).status == motion::PoseSampleStatus::Sampled);
}

// ---------------------------------------------------------------------------
// What a pose cannot carry
// ---------------------------------------------------------------------------

void
TestNoRootMotionReachesThePoseWhateverTheIntakeIsToldToDo()
{
    // The hips translation is the body's placement and the release's open
    // record; this class is the last one that could have quietly composed it
    // into a `RootMotion`, and it does not. So `RootMotionIntake` is inert here
    // — asserted over all three settings rather than over the default, because
    // "no root motion arrives" is the claim and any one setting could hide it.
    for (const motion::RootMotionIntake intake :
         {motion::RootMotionIntake::Passthrough,
          motion::RootMotionIntake::Ignore,
          motion::RootMotionIntake::DeriveVelocity}) {
        MocopiLiveSourceConfig config;
        config.intake.rootMotion = intake;
        MocopiLiveSource source(config);
        source.PushPacket(SkeletonPacket(), 0.0);
        source.PushPacket(FrameAt(1, 0.0), 0.0);
        source.PushPacket(FrameAt(2, 1.0 / kFrameRate), 1.0 / kFrameRate);

        const motion::PoseSampleResult result = source.Sample(1.0 / kFrameRate);
        assert(result.pose);
        assert(!result.pose->root.hasPosition);
        assert(!result.pose->root.hasLinearVelocity);
        // And it is readable where it was put: on the frame, not in the pose.
        assert(source.GetFramesFromLastPush()[0].hipsPosition);
    }
    // Nothing anywhere on this path derived a velocity, which is the statistic
    // that would have caught a root sneaking in through the intake.
    MocopiLiveSource source;
    source.PushPacket(SkeletonPacket(), 0.0);
    source.PushPacket(FrameAt(1, 0.0), 0.0);
    assert(source.GetIntake().GetStats().rootSamplesObserved == 0);
    assert(source.GetIntake().GetStats().rootVelocitiesDerived == 0);
}

void
TestTheEvidenceWindowShowsWhatThePoseDropped()
{
    MocopiLiveSource source;
    source.PushPacket(SkeletonPacket(), 0.0);
    source.PushPacket(FrameAt(1, 0.0), 0.0);

    // A gap and a damaged frame, so the window has something to show.
    MotionPacket damaged = FrameAt(4, 3.0 / kFrameRate);
    DropJoint(&damaged, 10);
    source.PushPacket(damaged, 3.0 / kFrameRate);

    assert(source.GetFramesFromLastPush().size() == 1);
    const MocopiFrame& frame = source.GetFramesFromLastPush()[0];
    assert(frame.lostFrames == 2);
    assert(frame.missing.test(static_cast<std::size_t>(HumanBone::Head)));
    assert(frame.hipsPosition);
    // None of the four reached the pose, which is the point of the window.
    const motion::PoseSampleResult result = source.Sample(3.0 / kFrameRate);
    assert(result.pose);
    assert(!result.pose->root.hasPosition);
}

void
TestARefusedDatagramIsCountedAndClearsTheWindow()
{
    MocopiLiveSource source;
    source.PushPacket(SkeletonPacket(), 0.0);
    source.PushPacket(FrameAt(1, 0.0), 0.0);
    assert(source.GetFramesFromLastPush().size() == 1);

    // Not this protocol at all. The decoder refuses the datagram whole, and this
    // is the only tally in the path that holds that number.
    const std::uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    std::vector<Diagnostic> diagnostics;
    assert(source.PushDatagram(garbage, sizeof(garbage), 0.0, &diagnostics) == 0);
    assert(source.GetStats().datagramsRefused == 1);
    assert(source.GetStats().datagramsDecoded == 0);
    assert(!diagnostics.empty());
    // The window is a view on the delivery that just happened. A caller reading
    // it after a refusal must not be shown the frame before last as though it
    // had just arrived.
    assert(source.GetFramesFromLastPush().empty());

    // And the refusal is stamped with the delivery it came from, not with a
    // packet the assembler was never handed.
    assert(diagnostics.back().sequence.has_value());
    assert(*diagnostics.back().sequence == 1);
}

void
TestResetForgetsTheStreamAndKeepsTheStats()
{
    MocopiLiveSource source;
    source.PushPacket(SkeletonPacket(), 0.0);
    source.PushPacket(FrameAt(1, 0.0), 0.0);
    assert(source.GetSkeletonMap());

    source.Reset();
    // The rig went with the stream, which is what a new session means here.
    assert(!source.GetSkeletonMap());
    assert(source.Sample(0.0).status == motion::PoseSampleStatus::Unavailable);
    assert(source.GetFramesFromLastPush().empty());
    assert(!source.ConsumeSessionRestart());
    // Stats survive, because they describe the session the caller is judging.
    assert(source.GetStats().framesAdmitted == 1);
    // And the provenance is re-stated, so the next pose out is not unlabelled.
    assert(source.GetSourceMetadata().protocol == "mocopi");

    // The same object takes a second stream, from the beginning.
    source.PushPacket(SkeletonPacket(), 0.0);
    assert(source.PushPacket(FrameAt(1, 0.0), 0.0) == 1);
}

// ---------------------------------------------------------------------------
// The corpus
// ---------------------------------------------------------------------------

struct ReplayedCapture
{
    std::vector<Diagnostic> diagnostics;
    vrmAdapterMocopi::MocopiLiveSourceStats stats;
    vrmAdapterMocopi::MocopiFrameStats frameStats;
    motion::LiveCaptureStats intakeStats;
    // Every pose the intake admitted, in order, sampled back out at its own
    // timestamp — which is what makes "the two halves meet" a claim about poses
    // rather than about counters.
    std::vector<motion::HumanoidPose> delivered;
    std::size_t restartsLatched = 0;
};

int
Failed(const std::string& name, const std::string& detail)
{
    std::fprintf(stderr, "%s: %s\n", name.c_str(), detail.c_str());
    return 1;
}

ReplayedCapture
Replay(const vrmAdapterMocopi::PacketCapture& capture,
       SessionRestartPolicy policy)
{
    MocopiLiveSourceConfig config;
    config.restart = policy;
    MocopiLiveSource source(config);
    source.SetSource(capture.sourceId);

    ReplayedCapture out;
    // One buffer for the whole replay, deliberately: every datagram is copied
    // into it and the previous one's bytes are gone. If anything the push
    // produced still pointed into them, the poses compared below would be wrong
    // — so the lifetime claim is checked by the results rather than by an
    // assertion about pointers.
    std::vector<std::uint8_t> buffer;
    for (const vrmAdapterMocopi::RecordedDatagram& datagram :
         capture.datagrams) {
        buffer.assign(datagram.bytes.begin(), datagram.bytes.end());
        const std::size_t admitted =
            source.PushDatagram(buffer, datagram.receiveTime, &out.diagnostics);
        std::fill(buffer.begin(), buffer.end(), std::uint8_t{0xcd});

        if (source.ConsumeSessionRestart()) {
            ++out.restartsLatched;
        }
        if (admitted != 0) {
            const motion::HumanoidPose& pose =
                source.GetFramesFromLastPush().back().pose;
            const motion::PoseSampleResult result = source.Sample(pose.timestamp);
            if (result.pose) {
                out.delivered.push_back(*result.pose);
            }
        }
    }

    out.stats = source.GetStats();
    out.frameStats = source.GetAssembler().GetStats();
    out.intakeStats = source.GetIntake().GetStats();
    return out;
}

// The claim this file exists for, and it holds for every capture: the assembler
// emits strictly advancing frames within a session, which is exactly the
// ordering `LiveCaptureSource::Push` requires, so under the default policy every
// frame it emitted was admitted and none was refused.
int
CheckTheTwoHalvesMeet(const ReplayedCapture& replayed, const std::string& name)
{
    if (replayed.stats.framesDelivered != replayed.frameStats.framesEmitted) {
        return Failed(name, "a frame the assembler emitted was not delivered "
                            "to the intake");
    }
    if (replayed.stats.framesRefused != 0
        || replayed.stats.framesAdmitted != replayed.stats.framesDelivered) {
        return Failed(name, "the intake refused a frame the assembler emitted");
    }
    if (replayed.intakeStats.framesAccepted != replayed.stats.framesAdmitted) {
        return Failed(name, "the intake's tally and this layer's disagree");
    }
    // Every admitted pose came back out at its own timestamp. A pose that went
    // in and could not be sampled would be a buffer this layer filled and
    // nothing could read.
    if (replayed.delivered.size() != replayed.stats.framesAdmitted) {
        return Failed(name, "an admitted pose could not be sampled back");
    }
    // And no root motion reached any of them, anywhere in the corpus.
    for (const motion::HumanoidPose& pose : replayed.delivered) {
        if (pose.root.hasPosition || pose.root.hasLinearVelocity) {
            return Failed(name, "the hips translation reached a RootMotion");
        }
    }
    return 0;
}

int
CheckNeutralStanding(const ReplayedCapture& replayed, const std::string& name)
{
    if (replayed.stats.framesAdmitted != 5 || replayed.restartsLatched != 0) {
        return Failed(name, "five poses and no restart were expected");
    }
    for (const motion::HumanoidPose& pose : replayed.delivered) {
        if (pose.validRotations.count() != kCanonicalBoneCount) {
            return Failed(name, "a delivered pose did not carry the whole rig");
        }
    }
    if (!replayed.diagnostics.empty()) {
        return Failed(name, "a clean session produced a diagnostic");
    }
    return 0;
}

int
CheckSessionRestart(const vrmAdapterMocopi::PacketCapture& capture,
                    const ReplayedCapture& replayed, const std::string& name)
{
    // The capture this file needed. Under the default policy the stream
    // continues across the restart.
    if (replayed.stats.framesAdmitted != 5) {
        return Failed(name, "the stream did not continue across the restart");
    }
    if (replayed.restartsLatched != 1) {
        return Failed(name, "the restart was not latched exactly once");
    }
    if (replayed.stats.sessionsReset != 1) {
        return Failed(name, "the restart policy did not reset the intake");
    }
    // The rig went with the old session and two frames were refused waiting for
    // a new one — the cost this protocol's restart has and the sibling's does
    // not.
    if (replayed.frameStats.framesRefusedNoRig != 2
        || replayed.frameStats.skeletonsAccepted != 2) {
        return Failed(name, "the restart did not cost the rig, or the session "
                            "did not recover");
    }

    // And the same bytes under `Refuse`, which is what makes the policy a
    // recorded choice rather than a described one. The new stream's clock begins
    // behind everything the intake holds, so the session stops where the other
    // continues.
    const ReplayedCapture refused = Replay(capture, SessionRestartPolicy::Refuse);
    if (refused.stats.framesDelivered != replayed.stats.framesDelivered) {
        return Failed(name, "the policy changed what the assembler emitted");
    }
    if (refused.stats.framesAdmitted != 3 || refused.stats.framesRefused != 2) {
        return Failed(name, "under Refuse the session did not visibly stop");
    }
    if (refused.stats.sessionsReset != 0 || refused.restartsLatched != 1) {
        return Failed(name, "under Refuse the restart was hidden as well as "
                            "declined");
    }
    return 0;
}

int
CheckRestartWithoutRecovery(const vrmAdapterMocopi::PacketCapture& capture,
                            const ReplayedCapture& replayed,
                            const std::string& name)
{
    // `frame-loss-01`, and the assertion is what it *cannot* show. Its restart
    // never reaches a frame, so nothing here acts on it — the latch never fires
    // and the two policies produce identical numbers. That is stated rather than
    // left implicit, because it is the whole reason `session-restart-01` had to
    // be added, and a capture that quietly started exercising the policy would
    // make the new one look redundant.
    if (replayed.frameStats.sessionRestarts != 1) {
        return Failed(name, "the assembler did not see the restart");
    }
    if (replayed.restartsLatched != 0 || replayed.stats.sessionsReset != 0) {
        return Failed(name, "a restart that reached no frame was acted on");
    }
    if (replayed.stats.framesAdmitted != 5) {
        return Failed(name, "five poses were expected");
    }
    const ReplayedCapture refused = Replay(capture, SessionRestartPolicy::Refuse);
    if (refused.stats.framesAdmitted != replayed.stats.framesAdmitted
        || refused.stats.framesRefused != replayed.stats.framesRefused) {
        return Failed(name, "this capture can tell the two policies apart, so "
                            "session-restart-01 is no longer the only one that "
                            "can");
    }
    return 0;
}

int
CheckIncompleteFrame(const ReplayedCapture& replayed, const std::string& name)
{
    // An incomplete frame is a *pose* here, which is the division this whole
    // layer rests on: the assembler reported what was missing and did not
    // refuse the frame, and `MissingBonePolicy` — HoldLast by default — is what
    // decides the three bones' fate.
    if (replayed.stats.framesAdmitted != 2) {
        return Failed(name, "an incomplete frame was not admitted as a pose");
    }
    // Read off the delivered poses rather than off a bone counter, because the
    // counters are measured against the whole 55-bone humanoid and this rig
    // carries 22 — every frame of every capture "unbinds" the 33 bones the
    // device never had, so a tally there could not tell three missing bones
    // from a rig that ends at the wrists.
    if (replayed.delivered[0].validRotations.count() != kCanonicalBoneCount - 3
        || replayed.delivered[1].validRotations.count() != kCanonicalBoneCount) {
        return Failed(name, "the damaged frame did not arrive as an incomplete "
                            "pose followed by a whole one");
    }
    // And nothing was held into it — under `HoldLast`, which is the default.
    // That is not a contradiction: this capture's damaged frame is its
    // **first**, so the intake has no previous value for those bones and
    // holding is not available to it. A session that begins mid-dropout starts
    // unbound however it is configured, which is worth pinning because it is the
    // one arrangement where the two policies agree.
    if (replayed.intakeStats.bonesHeld != 0) {
        return Failed(name, "a bone was held from a frame that never arrived");
    }
    return 0;
}

int
CheckNothingIsDelivered(const ReplayedCapture& replayed,
                        const std::string& name)
{
    if (replayed.stats.framesAdmitted != 0 || !replayed.delivered.empty()) {
        return Failed(name, "a capture with no readable frame delivered a pose");
    }
    return 0;
}

int
CheckCorpus(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".mocopipackets") {
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

        const ReplayedCapture replayed =
            Replay(capture, SessionRestartPolicy::Reset);

        int result = CheckTheTwoHalvesMeet(replayed, name);
        if (result == 0) {
            if (capture.sourceId == "neutral-standing-01") {
                result = CheckNeutralStanding(replayed, name);
            } else if (capture.sourceId == "session-restart-01") {
                result = CheckSessionRestart(capture, replayed, name);
            } else if (capture.sourceId == "frame-loss-01") {
                result = CheckRestartWithoutRecovery(capture, replayed, name);
            } else if (capture.sourceId == "arms-lowered-01") {
                // Three frames of motion and nothing remarkable; the root/hips
                // assertion this capture exists for is made against the frame
                // window in `CheckTheTwoHalvesMeet` above, for every capture.
                result = replayed.stats.framesAdmitted == 3
                             ? 0
                             : Failed(name, "three poses were expected");
            } else if (capture.sourceId == "incomplete-frame-01") {
                result = CheckIncompleteFrame(replayed, name);
            } else if (capture.sourceId == "refused-bones-01"
                       || capture.sourceId == "extended-form-01"
                       || capture.sourceId == "malformed-container-01"
                       || capture.sourceId == "malformed-packets-01") {
                // No rig is ever declared in any of these, so no frame can be
                // read. Registered rather than skipped, because "this capture
                // delivers nothing" is a claim that can stop being true.
                result = CheckNothingIsDelivered(replayed, name);
            } else {
                result = Failed(name, "no assertion is registered for sourceId '"
                                          + capture.sourceId + "'");
            }
        }

        if (result == 0
            && std::count_if(replayed.diagnostics.begin(),
                             replayed.diagnostics.end(),
                             [](const Diagnostic& diagnostic) {
                                 return diagnostic.code
                                        == DiagnosticCode::TrackingLost;
                             })
                   != 0) {
            result = Failed(name, "a capture raised a tracking state the "
                                  "grammar does not carry");
        }

        failures += result;
        if (result == 0) {
            std::printf("%s: %llu pose(s) delivered\n", name.c_str(),
                        static_cast<unsigned long long>(
                            replayed.stats.framesAdmitted));
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("mocopi live source corpus: %zu capture(s) verified\n",
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

    TestAFrameBecomesASampleablePose();
    TestTheBridgeContributesNoArithmetic();
    TestTheMissingBonePolicyIsTheIntakes();
    TestProvenanceIsToldOnceAndNeverChanges();
    TestARestartUnderResetContinuesTheStream();
    TestARestartUnderRefuseStopsTheStream();
    TestTheRestartLatchFiresOnTheFrameAndNotOnTheDetection();
    TestTheClockOffsetIsHandedBackRatherThanRepaired();
    TestNoRootMotionReachesThePoseWhateverTheIntakeIsToldToDo();
    TestTheEvidenceWindowShowsWhatThePoseDropped();
    TestARefusedDatagramIsCountedAndClearsTheWindow();
    TestResetForgetsTheStreamAndKeepsTheStats();
    std::puts("vrmAdapterMocopi live source tests passed");
    return 0;
}
