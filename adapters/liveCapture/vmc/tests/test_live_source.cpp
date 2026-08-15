// SPDX-License-Identifier: Apache-2.0
//
// The bridge: assembled frames into the runtime's live-capture intake.
//
// Most of these tests are about what this layer *does not* do, because that is
// what it is for. A gap is resolved by `MissingBonePolicy` and not here, which
// is checked by running the same input under both policies and watching the
// result change with the runtime's configuration rather than with the adapter.
// A pose is interpolated by `PoseBuffer` and not here. The one thing this layer
// decides — what a sender restart costs — is checked under both policies too,
// on the same recorded bytes.
//
// The unit tests build `VmcPacket` values directly, like every layer below, and
// two of them go through real datagrams instead: one for the refusal path that
// only exists at the OSC layer, and one for the lifetime claim the header makes
// about a receiver's reusable buffer.
//
// Corpus mode replays every committed capture from bytes, and makes the
// cross-layer claim this pairing exists for: every frame the assembler emitted
// was admitted by the intake, because the assembler's ordering contract is
// exactly the one the intake requires.
#include "vrmAdapterVmc/LiveSource.h"

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/FrameAssembler.h"
#include "vrmAdapterVmc/PacketCapture.h"
#include "vrmAdapterVmc/SkeletonMap.h"
#include "vrmAdapterVmc/VmcMessage.h"

#include "motionCore/Compare.h"
#include "motionCore/Humanoid.h"
#include "motionRuntime/LiveCaptureSource.h"
#include "motionRuntime/MotionSource.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using motion::HumanBone;
using motion::PoseSampleStatus;
using vrmAdapterVmc::Diagnostic;
using vrmAdapterVmc::DiagnosticCode;
using vrmAdapterVmc::SessionRestartPolicy;
using vrmAdapterVmc::VmcHumanBoneName;
using vrmAdapterVmc::VmcLiveSource;
using vrmAdapterVmc::VmcLiveSourceConfig;
using vrmAdapterVmc::VmcMessage;
using vrmAdapterVmc::VmcMessageKind;
using vrmAdapterVmc::VmcPacket;

constexpr std::array<float, 4> kUnityIdentity = {0.0f, 0.0f, 0.0f, 1.0f};

// The same short rig the assembler's tests use, so a claim here names four
// bones rather than looping over fifty-five.
constexpr std::array<HumanBone, 4> kRig = {HumanBone::Hips, HumanBone::Spine,
                                           HumanBone::Chest, HumanBone::Head};

VmcMessage
TimeMessage(double seconds)
{
    VmcMessage message;
    message.kind = VmcMessageKind::Time;
    message.seconds = seconds;
    return message;
}

VmcMessage
BoneMessage(HumanBone bone)
{
    VmcMessage message;
    message.kind = VmcMessageKind::BoneTransform;
    // Into the static table the map reads from, so the view outlives every
    // packet built here — a `VmcMessage::name` never owns its bytes.
    message.name = VmcHumanBoneName(bone);
    message.transform.rotation = kUnityIdentity;
    return message;
}

VmcMessage
RootMessage()
{
    VmcMessage message = BoneMessage(HumanBone::Hips);
    message.kind = VmcMessageKind::RootTransform;
    message.name = "root";
    return message;
}

VmcMessage
ModelMessage()
{
    VmcMessage message;
    message.kind = VmcMessageKind::Model;
    message.name = "C:/senders/avatar.vrm";
    message.title = "Example Avatar";
    return message;
}

VmcPacket
BundledFrame(double seconds, std::initializer_list<HumanBone> bones = {
                                 HumanBone::Hips, HumanBone::Spine,
                                 HumanBone::Chest, HumanBone::Head})
{
    VmcPacket packet;
    packet.messages.push_back(TimeMessage(seconds));
    packet.messages.push_back(RootMessage());
    for (const HumanBone bone : bones) {
        packet.messages.push_back(BoneMessage(bone));
    }
    return packet;
}

VmcPacket
Handshake()
{
    VmcPacket packet;
    packet.messages.push_back(ModelMessage());
    return packet;
}

bool
Near(double a, double b)
{
    return std::abs(a - b) <= 1e-9;
}

std::size_t
CountCode(const std::vector<Diagnostic>& diagnostics, DiagnosticCode code)
{
    std::size_t count = 0;
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            ++count;
        }
    }
    return count;
}

// ---------------------------------------------------------------------------
// Byte assembly, for the two claims a decoded packet cannot make
// ---------------------------------------------------------------------------

struct Bytes
{
    std::vector<std::uint8_t> data;

    // A NUL-terminated string padded to four bytes — always at least one NUL.
    // The padding is measured from the start of the datagram, which is correct
    // because nothing here builds a bundle.
    Bytes& Str(std::string_view text)
    {
        data.insert(data.end(), text.begin(), text.end());
        data.push_back(0);
        while (data.size() % 4 != 0) {
            data.push_back(0);
        }
        return *this;
    }

    Bytes& F32(float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        data.push_back(static_cast<std::uint8_t>(bits >> 24));
        data.push_back(static_cast<std::uint8_t>(bits >> 16));
        data.push_back(static_cast<std::uint8_t>(bits >> 8));
        data.push_back(static_cast<std::uint8_t>(bits));
        return *this;
    }
};

std::vector<std::uint8_t>
TimeDatagram(float seconds)
{
    Bytes out;
    out.Str("/VMC/Ext/T").Str(",f").F32(seconds);
    return out.data;
}

// A bone at rest apart from its rotation, which is what the caller is checking.
std::vector<std::uint8_t>
BoneDatagram(std::string_view name, float qx, float qy, float qz, float qw)
{
    Bytes out;
    out.Str("/VMC/Ext/Bone/Pos").Str(",sfffffff").Str(name);
    out.F32(0.0f).F32(0.0f).F32(0.0f).F32(qx).F32(qy).F32(qz).F32(qw);
    return out.data;
}

// Well-formed OSC and well-formed VMC, at an address this adapter does not
// implement. Refused by the layer that knows addresses and not sessions.
std::vector<std::uint8_t>
UnsupportedDatagram()
{
    Bytes out;
    out.Str("/VMC/Ext/Midi/Note").Str(",f").F32(60.0f);
    return out.data;
}

// ---------------------------------------------------------------------------
// The hand-off
// ---------------------------------------------------------------------------

void
TestFramesReachTheIntakeAndAreSampledByTheRuntime()
{
    VmcLiveSource source;
    assert(source.PushPacket(BundledFrame(10.0), 0.010) == 0);
    assert(source.PushPacket(BundledFrame(10.0 + 1.0 / 30.0), 0.043) == 1);
    assert(source.PushPacket(BundledFrame(10.0 + 2.0 / 30.0), 0.076) == 1);
    // A stream has no end marker; the last frame is the caller's to ask for.
    assert(source.Flush() == 1);

    assert(source.GetStats().framesAdmitted == 3);
    assert(source.GetStats().framesRefused == 0);
    // The bridge keeps no history of its own: what it delivered is what the
    // runtime's buffer holds, one for one.
    assert(source.GetIntake().GetBuffer().GetSize() == 3);

    // Provenance is on the source before a sender has named its model.
    assert(source.GetSourceMetadata().protocol == "vmc");
    assert(source.GetSourceMetadata().kind
           == motion::MotionSourceKind::LiveCapture);

    // The sender's clock and the consumer's share no origin, which is what
    // `AlignClock` exists to bridge — and after it the source answers on the
    // consumer's timeline, not on VMC's.
    assert(source.GetIntake().AlignClock(100.0));
    motion::PoseSampleResult head = source.Sample(100.0);
    assert(head.status == PoseSampleStatus::Sampled);
    assert(head.IsValid());
    assert(Near(head.pose->timestamp, 100.0));
    assert(head.pose->validRotations.count() == kRig.size());

    // Between two observed frames, which is interpolation the runtime does and
    // this adapter must not have its own copy of.
    assert(source.Sample(100.0 - 1.0 / 60.0).status
           == PoseSampleStatus::Sampled);
    // Before the oldest frame the boundary pose is held, never faded.
    assert(source.Sample(100.0 - 1.0).status == PoseSampleStatus::Held);

    double start = 0.0;
    double end = 0.0;
    assert(source.GetTimeRange(&start, &end));
    assert(Near(end, 100.0));
}

void
TestAGapIsResolvedByThePolicyAndNotByTheAdapter()
{
    // The same input under both missing-bone policies. The assembler reported
    // the gap and held nothing forward; what happens next is the runtime's, and
    // the proof is that the answer changes without the adapter changing.
    for (const motion::MissingBonePolicy policy :
         {motion::MissingBonePolicy::HoldLast,
          motion::MissingBonePolicy::LeaveUnbound}) {
        VmcLiveSourceConfig config;
        config.intake.missingBones = policy;
        VmcLiveSource source(config);

        source.PushPacket(BundledFrame(1.0), 0.0);
        source.PushPacket(BundledFrame(1.0 + 1.0 / 30.0,
                                       {HumanBone::Hips, HumanBone::Spine}),
                          0.033);
        source.Flush();

        assert(source.GetStats().framesAdmitted == 2);
        const motion::HumanoidPose& newest =
            source.GetIntake().GetBuffer().GetNewest();
        if (policy == motion::MissingBonePolicy::HoldLast) {
            assert(newest.validRotations.count() == kRig.size());
            assert(source.GetIntake().GetStats().bonesHeld == 2);
        } else {
            assert(newest.validRotations.count() == 2);
            assert(!newest.validRotations.test(
                static_cast<std::size_t>(HumanBone::Chest)));
        }
    }
}

void
TestAStaleBoneIsReportedAndNotUnbound()
{
    // A stale bone is a stronger claim than a missing one, and it is still not
    // this layer's to act on: unbinding it here would be a second missing-bone
    // policy, disagreeing with the configured one wherever the two horizons
    // differ. The diagnostic is what reaches an operator.
    VmcLiveSourceConfig config;
    config.frame.stalenessSeconds = 0.05;
    VmcLiveSource source(config);
    std::vector<Diagnostic> diagnostics;

    source.PushPacket(BundledFrame(1.0), 0.0, &diagnostics);
    for (int index = 1; index != 5; ++index) {
        source.PushPacket(BundledFrame(1.0 + index * 0.05,
                                       {HumanBone::Hips, HumanBone::Spine}),
                          0.0, &diagnostics);
    }
    source.Flush(&diagnostics);

    assert(CountCode(diagnostics, DiagnosticCode::StaleJoint) == 2);
    // Held, because the configured policy says so — the staleness horizon
    // informs the operator and does not overrule the intake.
    assert(source.GetIntake().GetBuffer().GetNewest().validRotations.count()
           == kRig.size());
}

// ---------------------------------------------------------------------------
// The one decision this layer takes
// ---------------------------------------------------------------------------

// Two frames on one clock, then two on a clock that began again. The assembler
// emits four; what the fourth costs is the policy's answer.
void
PushARestartingSession(VmcLiveSource* source)
{
    source->PushPacket(BundledFrame(30.0), 0.010);
    source->PushPacket(BundledFrame(30.0 + 1.0 / 30.0), 0.043);
    source->PushPacket(BundledFrame(0.016667), 0.900);
    source->PushPacket(BundledFrame(0.050000), 0.933);
    source->Flush();
}

void
TestARestartKeepsTheStreamAndIsLatchedForTheCaller()
{
    VmcLiveSource source;
    assert(source.GetRestartPolicy() == SessionRestartPolicy::Reset);
    PushARestartingSession(&source);

    assert(source.GetStats().framesDelivered == 4);
    assert(source.GetStats().framesAdmitted == 4);
    assert(source.GetStats().framesRefused == 0);
    assert(source.GetStats().sessionsReset == 1);
    // The history went with the session that ended: the buffer holds the new
    // session's two frames and not the old session's two.
    assert(source.GetIntake().GetBuffer().GetSize() == 2);
    assert(Near(source.GetIntake().GetBuffer().GetOldest().timestamp,
                0.016667));

    // Latched exactly once, because the consumer's clock offset was measured
    // against a clock that no longer exists and only the consumer knows what to
    // re-align it to.
    assert(source.ConsumeSessionRestart());
    assert(!source.ConsumeSessionRestart());

    assert(source.GetIntake().AlignClock(5.0));
    assert(source.Sample(5.0).status == PoseSampleStatus::Sampled);
}

void
TestRefusingARestartStopsTheStreamVisibly()
{
    // Set after construction rather than through the config, because the two
    // are separate paths to the same field and a policy that could only be
    // chosen once would be a different class.
    VmcLiveSource source;
    source.SetRestartPolicy(SessionRestartPolicy::Refuse);
    assert(source.GetRestartPolicy() == SessionRestartPolicy::Refuse);
    PushARestartingSession(&source);

    assert(source.GetStats().framesDelivered == 4);
    // The new session's frames arrive behind the head of a buffer nobody
    // cleared, and the intake refuses a frame that does not advance.
    assert(source.GetStats().framesAdmitted == 2);
    assert(source.GetStats().framesRefused == 2);
    assert(source.GetStats().sessionsReset == 0);
    assert(source.GetIntake().GetStats().framesRejectedOutOfOrder == 2);
    assert(source.GetIntake().GetBuffer().GetSize() == 2);
    assert(Near(source.GetIntake().GetBuffer().GetNewest().timestamp,
                30.0 + 1.0 / 30.0));

    // The latch does not depend on the policy: a consumer that chose to stall
    // still has to be able to find out why its source stopped advancing.
    assert(source.ConsumeSessionRestart());
    assert(!source.ConsumeSessionRestart());
}

// ---------------------------------------------------------------------------
// Provenance
// ---------------------------------------------------------------------------

void
TestProvenanceAppliesFromWhenTheSenderSentIt()
{
    VmcLiveSource source;
    source.PushPacket(BundledFrame(1.0), 0.0);
    source.PushPacket(BundledFrame(2.0), 0.0);
    assert(source.GetIntake().GetBuffer().GetSize() == 1);

    // The handshake arrives mid-session, which VMC permits and a sender that
    // restarts guarantees. It is not a frame: no bone, no root, no clock.
    assert(source.PushPacket(Handshake(), 0.0) == 0);
    source.PushPacket(BundledFrame(3.0), 0.0);
    source.Flush();
    assert(source.GetIntake().GetBuffer().GetSize() == 3);

    const motion::HumanoidPose& first =
        source.GetIntake().GetBuffer().GetOldest();
    const motion::HumanoidPose& last =
        source.GetIntake().GetBuffer().GetNewest();
    assert(first.source.has_value() && last.source.has_value());
    // Poses recorded before the sender named its model do not retroactively
    // learn it: re-stamping them would claim they were recorded knowing
    // something the session did not know yet.
    assert(first.source->sourceId.empty());
    assert(last.source->sourceId == "Example Avatar");
    assert(first.source->protocol == "vmc" && last.source->protocol == "vmc");
    // The model path names a file on the sender's machine and is not carried.
    assert(last.source->provider.empty());
    assert(source.GetSourceMetadata().sourceId == "Example Avatar");
}

// ---------------------------------------------------------------------------
// What a pose cannot carry
// ---------------------------------------------------------------------------

void
TestTheFrameDetailStaysReadableAfterTheHandOff()
{
    // The hips offset, the missing set, and the session flag reach a
    // `HumanoidPose` nowhere at all. Milestone B has to settle what the first of
    // them means with a real sender's session in front of it, so the hand-off
    // must not be where they stop being visible.
    VmcLiveSource source;

    VmcPacket opening;
    opening.messages.push_back(TimeMessage(1.0));
    opening.messages.push_back(RootMessage());
    VmcMessage hips = BoneMessage(HumanBone::Hips);
    hips.transform.position = {0.2f, 0.9f, 0.0f};
    opening.messages.push_back(hips);
    opening.messages.push_back(BoneMessage(HumanBone::Spine));
    assert(source.PushPacket(opening, 0.0) == 0);
    // Nothing was delivered, so the window is empty rather than showing the
    // previous push's frames.
    assert(source.GetFramesFromLastPush().empty());

    assert(source.PushPacket(BundledFrame(2.0, {HumanBone::Hips}), 0.0) == 1);
    assert(source.GetFramesFromLastPush().size() == 1);
    const vrmAdapterVmc::VmcFrame& first = source.GetFramesFromLastPush()[0];
    assert(first.hipsOffset.has_value());
    // Converted — the reflection through X is the skeleton map's — and still
    // uncomposed with the root, which is the open question itself.
    assert(std::abs((*first.hipsOffset)[0] + 0.2f) <= 1e-6f);
    assert(first.timestampFromSender);
    assert(!first.beginsNewSession);

    assert(source.Flush() == 1);
    const vrmAdapterVmc::VmcFrame& second = source.GetFramesFromLastPush()[0];
    assert(second.missing.count() == 1);
    assert(second.missing.test(static_cast<std::size_t>(HumanBone::Spine)));

    // Replaced by the next push rather than appended to, or a receive loop
    // would grow the window into the history this class does not keep.
    assert(source.PushPacket(BundledFrame(3.0), 0.0) == 0);
    assert(source.GetFramesFromLastPush().empty());
}

// ---------------------------------------------------------------------------
// Datagrams
// ---------------------------------------------------------------------------

void
TestEveryDiagnosticOfOneDatagramCarriesItsNumber()
{
    VmcLiveSource source;
    source.SetSource("127.0.0.1:39539");
    std::vector<Diagnostic> diagnostics;

    // 1: refused whole by the OSC layer, so the assembler never sees it — and a
    //    delivery it never sees is one its own serial cannot count.
    const std::vector<std::uint8_t> junk = {0xde, 0xad, 0xbe, 0xef};
    source.PushDatagram(junk, 0.001, &diagnostics);
    // 2 and 3: a bone and a clock, decoding cleanly and raising nothing.
    source.PushDatagram(
        BoneDatagram(VmcHumanBoneName(HumanBone::Hips), 0.0f, 0.0f, 0.0f, 1.0f),
        0.002, &diagnostics);
    source.PushDatagram(TimeDatagram(1.0f), 0.003, &diagnostics);
    // 4: refused by the VMC layer, which knows an address and not a session.
    source.PushDatagram(UnsupportedDatagram(), 0.004, &diagnostics);
    // 5: refused by the skeleton map and passed through the assembler, which
    //    stamps its own packet serial — 4 by now, because the OSC layer's
    //    refusal was never handed to it.
    source.PushDatagram(
        BoneDatagram(VmcHumanBoneName(HumanBone::Spine), 0.0f, 0.0f, 0.0f, 0.0f),
        0.005, &diagnostics);

    assert(diagnostics.size() == 3);
    assert(diagnostics[0].code == DiagnosticCode::PacketMalformed);
    assert(diagnostics[1].code == DiagnosticCode::UnsupportedMessage);
    assert(diagnostics[2].code == DiagnosticCode::PacketMalformed);
    for (const Diagnostic& diagnostic : diagnostics) {
        assert(diagnostic.source == "127.0.0.1:39539");
        assert(diagnostic.sequence.has_value());
    }
    // The datagram a reader would go looking for in a capture, on every line
    // whichever layer raised it — including the last, where the assembler's own
    // numbering said 4 and was overwritten.
    assert(*diagnostics[0].sequence == 1);
    assert(*diagnostics[1].sequence == 4);
    assert(*diagnostics[2].sequence == 5);
    assert(source.GetAssembler().GetStats().bonesMalformed == 1);
}

void
TestARefusedDatagramCostsItselfAndIsCounted()
{
    VmcLiveSource source;
    source.SetSource("127.0.0.1:39539");
    std::vector<Diagnostic> diagnostics;

    const std::vector<std::uint8_t> junk = {0xde, 0xad, 0xbe, 0xef};
    assert(source.PushDatagram(junk, 0.001, &diagnostics) == 0);
    assert(source.GetStats().datagramsRefused == 1);
    assert(source.GetStats().datagramsDecoded == 0);
    assert(CountCode(diagnostics, DiagnosticCode::PacketMalformed) == 1);
    // Stamped with the session, although the layer that raised it has no idea
    // which session it was reading.
    assert(diagnostics[0].source == "127.0.0.1:39539");

    // A refused datagram is not a refused session.
    source.PushDatagram(BoneDatagram(VmcHumanBoneName(HumanBone::Hips), 0.0f,
                                     0.0f, 0.0f, 1.0f),
                        0.002, &diagnostics);
    source.PushDatagram(TimeDatagram(1.0f), 0.003, &diagnostics);
    assert(source.Flush(&diagnostics) == 1);
    assert(source.GetStats().datagramsDecoded == 2);
    assert(source.GetStats().framesAdmitted == 1);

    // And it clears the evidence window rather than leaving the previous push's
    // frame in it. `GetFramesFromLastPush()` is a view on the delivery that just
    // happened, so a recording tool reading it after every push must not be
    // handed the frame before last as though it had just arrived. Found by the
    // review of the mocopi bridge, which took the same shape and had the same
    // gap; both are fixed with the same one line.
    assert(source.GetFramesFromLastPush().size() == 1);
    assert(source.PushDatagram(junk, 0.004, &diagnostics) == 0);
    assert(source.GetFramesFromLastPush().empty());
}

void
TestTheDatagramNeedNotOutliveThePush()
{
    // A receive loop with one reusable buffer invalidates every string view an
    // already-decoded packet holds, which is the hazard `VmcMessage.h` names
    // and no overload can refuse. Pushing through this class ends inside the
    // call, so the hazard ends here too — checked by doing exactly what a
    // receiver does: decode out of the buffer, then overwrite it.
    VmcLiveSource source;
    std::vector<std::uint8_t> buffer;

    // A quarter turn about Unity's −Z, which the basis change carries onto the
    // canonical +Z. The value is checked after the bytes are gone.
    const float half = 0.3826834324f;
    const float rest = 0.9238795325f;
    buffer = BoneDatagram(VmcHumanBoneName(HumanBone::LeftUpperArm), 0.0f, 0.0f,
                          -half, rest);
    source.PushDatagram(buffer, 0.001);
    buffer = TimeDatagram(2.0f);
    source.PushDatagram(buffer, 0.002);

    std::fill(buffer.begin(), buffer.end(), std::uint8_t{0xdd});
    buffer.clear();
    buffer.shrink_to_fit();

    assert(source.Flush() == 1);
    const motion::HumanoidPose& pose =
        source.GetIntake().GetBuffer().GetNewest();
    const std::size_t arm = static_cast<std::size_t>(HumanBone::LeftUpperArm);
    assert(pose.validRotations.test(arm));
    assert(Near(pose.timestamp, 2.0));
    const pxr::GfQuatf identity(1.0f, pxr::GfVec3f(0.0f));
    assert(std::abs(motion::AngleBetween(pose.localRotations[arm], identity)
                    - 0.7853981634f)
           <= 1e-4f);
    assert(pose.localRotations[arm].GetImaginary()[2] > 0.0f);
}

// ---------------------------------------------------------------------------
// The session
// ---------------------------------------------------------------------------

void
TestResetForgetsBothHalvesAndKeepsTheTally()
{
    VmcLiveSource source;
    source.PushPacket(Handshake(), 0.0);
    source.PushPacket(BundledFrame(30.0), 0.0);
    source.PushPacket(BundledFrame(30.033), 0.0);
    assert(source.GetIntake().GetBuffer().GetSize() == 1);
    assert(source.GetIntake().AlignClock(0.0));

    source.Reset();
    // The clock offset survives, because `LiveCaptureSource::Reset` keeps it
    // deliberately — so a caller replaying a second capture into this object
    // re-aligns it rather than inheriting the first capture's origin. Pinned
    // here because the sentence above is the only warning it gets.
    assert(!Near(source.GetIntake().GetClockOffset(), 0.0));
    assert(source.GetIntake().IsEmpty());
    assert(source.GetAssembler().GetObservedBones().none());
    // The sender is unknown again: after a reset nothing about it is still true.
    assert(source.GetSourceMetadata().sourceId.empty());
    assert(source.GetSourceMetadata().protocol == "vmc");
    // The open frame went with it, so the flush finds nothing.
    assert(source.Flush() == 0);

    // A clock that would have been a regression is now the start of a stream.
    source.PushPacket(BundledFrame(1.0), 0.0);
    source.PushPacket(BundledFrame(2.0), 0.0);
    source.Flush();
    assert(source.GetIntake().GetBuffer().GetSize() == 2);
    // Stats describe the session the caller is judging, not the stream's state.
    assert(source.GetStats().framesAdmitted == 3);
    source.ResetStats();
    assert(source.GetStats().framesAdmitted == 0);
    // Reset here is this layer's tally alone; the two halves keep their own.
    assert(source.GetIntake().GetStats().framesAccepted == 3);
}

void
TestTheSourceSurvivesHavingNowhereToReport()
{
    // Diagnostics are optional at every level below this one, and a receive loop
    // that keeps none still has to run.
    VmcLiveSource source;
    const std::vector<std::uint8_t> junk = {0x01, 0x02, 0x03};
    assert(source.PushDatagram(junk, 0.0) == 0);
    assert(source.PushPacket(BundledFrame(1.0), 0.0) == 0);
    assert(source.Flush() == 1);
    assert(source.GetStats().datagramsRefused == 1);
    assert(source.GetStats().framesAdmitted == 1);
}

// ---------------------------------------------------------------------------
// Corpus
// ---------------------------------------------------------------------------

// `framesAdmitted` is the *same* number the frame-assembler suite pins as frames
// emitted, and naming it again here is the cross-layer claim: under the default
// restart policy the intake admitted every frame the assembler produced, because
// the assembler emits strictly advancing frames within a session and that is
// exactly the ordering `LiveCaptureSource::Push` requires. `datagramsRefused` is
// the number no other layer holds — a datagram the OSC layer refused never
// reaches the assembler to be counted there.
struct Expected
{
    const char* file;
    std::size_t datagramsDecoded;
    std::size_t datagramsRefused;
    std::size_t framesAdmitted;
    std::size_t sessionsReset;
    // Poses still in the buffer at the end: the admitted count, less the ones a
    // restart dropped. It is named rather than derived because only the capture
    // knows how its session was split — and it is compared against the buffer's
    // capacity below, since a longer capture than any committed here loses its
    // oldest poses to eviction, which is the buffer working and not a fault.
    std::size_t buffered;
};

constexpr Expected kExpected[] = {
    {"arm-raise-30hz.vmcpackets", 117, 0, 5, 0, 5},
    {"extended-forms.vmcpackets", 2, 0, 1, 0, 1},
    {"malformed-forms.vmcpackets", 10, 0, 2, 0, 2},
    {"malformed-packets.vmcpackets", 2, 8, 0, 0, 0},
    {"mixed-traffic-30hz.vmcpackets", 13, 0, 3, 0, 3},
    {"neutral-standing-30hz.vmcpackets", 6, 0, 5, 0, 5},
    // Six admitted, two of them the old session's and dropped with it.
    {"sender-restart-30hz.vmcpackets", 10, 0, 6, 1, 2},
};

bool
Replay(const std::filesystem::path& path, VmcLiveSource* source,
       std::vector<Diagnostic>* diagnostics)
{
    vrmAdapterVmc::PacketCapture capture;
    vrmAdapterVmc::PacketCaptureError error;
    if (!vrmAdapterVmc::ReadPacketCaptureFile(path.string(), &capture,
                                              &error)) {
        std::fprintf(stderr, "%s:%zu: %s\n", path.filename().string().c_str(),
                     error.line, error.message.c_str());
        return false;
    }
    source->SetSource(capture.sourceId);
    for (const vrmAdapterVmc::RecordedDatagram& datagram : capture.datagrams) {
        source->PushDatagram(datagram.bytes, datagram.receiveTime,
                             diagnostics);
    }
    source->Flush(diagnostics);
    return true;
}

// The claim the restart capture exists for at this layer, made twice on the
// same bytes: what a sender restart costs is a policy and not an accident.
int
CheckTheRestartPolicyIsWhatCostsTheFrames(const std::filesystem::path& path)
{
    VmcLiveSourceConfig config;
    config.restart = SessionRestartPolicy::Refuse;
    VmcLiveSource refusing(config);
    if (!Replay(path, &refusing, nullptr)) {
        return 1;
    }
    if (refusing.GetStats().framesDelivered != 6
        || refusing.GetStats().framesAdmitted != 4
        || refusing.GetStats().framesRefused != 2
        || refusing.GetStats().sessionsReset != 0) {
        std::fprintf(stderr,
                     "sender-restart under Refuse: %llu delivered, %llu "
                     "admitted, %llu refused, %llu reset -- expected 6, 4, 2, "
                     "0\n",
                     static_cast<unsigned long long>(
                         refusing.GetStats().framesDelivered),
                     static_cast<unsigned long long>(
                         refusing.GetStats().framesAdmitted),
                     static_cast<unsigned long long>(
                         refusing.GetStats().framesRefused),
                     static_cast<unsigned long long>(
                         refusing.GetStats().sessionsReset));
        return 1;
    }
    // And the session is over rather than merely behind: the buffer's head is
    // still the last frame of the clock that ended.
    if (!refusing.ConsumeSessionRestart()
        || refusing.GetIntake().GetBuffer().GetNewest().timestamp < 30.0) {
        std::fprintf(stderr,
                     "sender-restart under Refuse: the stall is not visible to "
                     "a caller\n");
        return 1;
    }
    return 0;
}

int
CheckCorpus(const std::filesystem::path& directory)
{
    if (!std::filesystem::is_directory(directory)) {
        std::fprintf(stderr, "corpus directory not found: %s\n",
                     directory.string().c_str());
        return 1;
    }
    std::vector<std::filesystem::path> captures;
    for (const std::filesystem::directory_entry& file :
         std::filesystem::directory_iterator(directory)) {
        if (file.is_regular_file()
            && file.path().extension() == ".vmcpackets") {
            captures.push_back(file.path());
        }
    }
    std::sort(captures.begin(), captures.end());
    if (captures.empty()) {
        std::fprintf(stderr, "no .vmcpackets fixtures in %s\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    std::set<std::string> covered;

    for (const std::filesystem::path& path : captures) {
        const std::string name = path.filename().string();
        const Expected* entry = nullptr;
        for (const Expected& candidate : kExpected) {
            if (name == candidate.file) {
                entry = &candidate;
                break;
            }
        }
        if (!entry) {
            std::fprintf(stderr,
                         "%s: no expected delivery in this test -- add one, or "
                         "the capture is in the corpus and reaches the runtime "
                         "under nobody's eye\n",
                         name.c_str());
            ++failures;
            continue;
        }
        covered.insert(name);

        VmcLiveSource source;
        std::vector<Diagnostic> diagnostics;
        if (!Replay(path, &source, &diagnostics)) {
            ++failures;
            continue;
        }

        const vrmAdapterVmc::VmcLiveSourceStats& stats = source.GetStats();
        if (stats.datagramsDecoded != entry->datagramsDecoded
            || stats.datagramsRefused != entry->datagramsRefused
            || stats.framesAdmitted != entry->framesAdmitted
            || stats.sessionsReset != entry->sessionsReset) {
            std::fprintf(stderr,
                         "%s: %llu decoded, %llu refused, %llu admitted, %llu "
                         "reset -- expected %zu, %zu, %zu, %zu\n",
                         name.c_str(),
                         static_cast<unsigned long long>(stats.datagramsDecoded),
                         static_cast<unsigned long long>(stats.datagramsRefused),
                         static_cast<unsigned long long>(stats.framesAdmitted),
                         static_cast<unsigned long long>(stats.sessionsReset),
                         entry->datagramsDecoded, entry->datagramsRefused,
                         entry->framesAdmitted, entry->sessionsReset);
            ++failures;
            continue;
        }

        // The claim this pairing exists to make. Every frame the assembler
        // emitted was admitted, on every capture including the one that
        // restarts — so the two contracts meet rather than nearly meeting.
        if (stats.framesRefused != 0
            || stats.framesDelivered
                != source.GetAssembler().GetStats().framesEmitted) {
            std::fprintf(stderr,
                         "%s: the assembler emitted %llu frame(s) and the "
                         "intake was given %llu, refusing %llu\n",
                         name.c_str(),
                         static_cast<unsigned long long>(
                             source.GetAssembler().GetStats().framesEmitted),
                         static_cast<unsigned long long>(stats.framesDelivered),
                         static_cast<unsigned long long>(stats.framesRefused));
            ++failures;
        }

        // And the buffer is the delivery: a bridge that had grown a history of
        // its own would show up here as a count that no longer matches.
        const std::size_t expectedBuffered = std::min(
            entry->buffered, source.GetIntake().GetBuffer().GetCapacity());
        if (source.GetIntake().GetBuffer().GetSize() != expectedBuffered) {
            std::fprintf(stderr, "%s: %zu pose(s) buffered, expected %zu\n",
                         name.c_str(),
                         source.GetIntake().GetBuffer().GetSize(),
                         expectedBuffered);
            ++failures;
        }

        if (name == "neutral-standing-30hz.vmcpackets") {
            // Recorded on the sender's clock at 12.5 s and sampled on a
            // consumer's at zero, which is the whole reason the intake keeps an
            // offset — and the reason this capture was recorded with two
            // origins that disagree.
            source.GetIntake().AlignClock(0.0);
            const motion::PoseSampleResult head = source.Sample(0.0);
            const motion::PoseSampleResult between =
                source.Sample(-1.0 / 60.0);
            const motion::PoseSampleResult before = source.Sample(-1.0);
            if (head.status != PoseSampleStatus::Sampled
                || between.status != PoseSampleStatus::Sampled
                || before.status != PoseSampleStatus::Held
                || !head.IsValid()
                || head.pose->validRotations.count() != 22) {
                std::fprintf(stderr,
                             "%s: the recorded session does not sample as a "
                             "22-bone rig on the consumer's clock\n",
                             name.c_str());
                ++failures;
            }
        }

        if (name == "sender-restart-30hz.vmcpackets") {
            failures += CheckTheRestartPolicyIsWhatCostsTheFrames(path);
        }

        std::printf("%s: %llu pose(s) delivered\n", name.c_str(),
                    static_cast<unsigned long long>(stats.framesAdmitted));
    }

    for (const Expected& entry : kExpected) {
        if (covered.find(entry.file) == covered.end()) {
            std::fprintf(stderr, "%s: expected in this test, absent from %s\n",
                         entry.file, directory.string().c_str());
            ++failures;
        }
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("VMC live source: %zu capture(s) verified, every assembled "
                "frame admitted by the runtime\n",
                captures.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestFramesReachTheIntakeAndAreSampledByTheRuntime();
    TestAGapIsResolvedByThePolicyAndNotByTheAdapter();
    TestAStaleBoneIsReportedAndNotUnbound();
    TestARestartKeepsTheStreamAndIsLatchedForTheCaller();
    TestRefusingARestartStopsTheStreamVisibly();
    TestProvenanceAppliesFromWhenTheSenderSentIt();
    TestTheFrameDetailStaysReadableAfterTheHandOff();
    TestEveryDiagnosticOfOneDatagramCarriesItsNumber();
    TestARefusedDatagramCostsItselfAndIsCounted();
    TestTheDatagramNeedNotOutliveThePush();
    TestResetForgetsBothHalvesAndKeepsTheTally();
    TestTheSourceSurvivesHavingNowhereToReport();
    std::puts("vrmAdapterVmc live source tests passed");
    return 0;
}
