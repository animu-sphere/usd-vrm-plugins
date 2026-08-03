// SPDX-License-Identifier: Apache-2.0
//
// Frame assembly: where a frame begins, and what it means that a bone is not in
// it.
//
// The unit tests build `VmcPacket` values directly, for the reason every layer
// below does the same: this layer's input *is* a decoded packet, and going
// through the wire again would re-test four decoders and hide which of the five
// refused something. They are also the only place a phenomenon no committed
// capture contains can be stated — a sender that emits no clock, a frame
// boundary that carries nothing, a bone gone missing for a second.
//
// Corpus mode runs all five layers over every committed capture, and it is the
// one that matters most here. Every layer below this one could be checked
// against a single datagram; this one cannot, because its subject is the
// *arrangement* of the datagrams rather than their contents. Its central claim
// is the one the corpus was recorded to make: the bundled sender and the
// unbundled sender, which put their clock at opposite ends of a frame, produce
// the same five frames at the same 30 Hz cadence.
#include "vrmAdapterVmc/FrameAssembler.h"

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/OscPacket.h"
#include "vrmAdapterVmc/PacketCapture.h"
#include "vrmAdapterVmc/SkeletonMap.h"
#include "vrmAdapterVmc/VmcMessage.h"

#include "motionCore/Compare.h"
#include "motionCore/Humanoid.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using motion::HumanBone;
using motion::HumanBoneCount;
using vrmAdapterVmc::Diagnostic;
using vrmAdapterVmc::DiagnosticCode;
using vrmAdapterVmc::DiagnosticSeverity;
using vrmAdapterVmc::VmcFrame;
using vrmAdapterVmc::VmcFrameAssembler;
using vrmAdapterVmc::VmcFrameConfig;
using vrmAdapterVmc::VmcHumanBoneName;
using vrmAdapterVmc::VmcMessage;
using vrmAdapterVmc::VmcMessageKind;
using vrmAdapterVmc::VmcPacket;

constexpr std::array<float, 4> kUnityIdentity = {0.0f, 0.0f, 0.0f, 1.0f};

VmcMessage
TimeMessage(double seconds)
{
    VmcMessage message;
    message.kind = VmcMessageKind::Time;
    message.seconds = seconds;
    return message;
}

VmcMessage
BoneMessage(HumanBone bone, const std::array<float, 3>& position = {})
{
    VmcMessage message;
    message.kind = VmcMessageKind::BoneTransform;
    // Into the static table the map itself reads from, so the view outlives
    // every packet built here -- a `VmcMessage::name` never owns its bytes.
    message.name = VmcHumanBoneName(bone);
    message.transform.position = position;
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

// A short rig, so a test states which bones it is talking about rather than
// looping over fifty-five.
constexpr std::array<HumanBone, 4> kRig = {HumanBone::Hips, HumanBone::Spine,
                                           HumanBone::Chest, HumanBone::Head};

// The bundled sender's shape: the clock opens the frame, the root and the bones
// follow, and the whole frame is one datagram.
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
OneMessage(const VmcMessage& message)
{
    VmcPacket packet;
    packet.messages.push_back(message);
    return packet;
}

// `name` must outlive the packet: like every other message built here, the view
// does not own its bytes. Callers pass string literals.
VmcMessage
BlendMessage(std::string_view name, float value)
{
    VmcMessage message;
    message.kind = VmcMessageKind::BlendValue;
    message.name = name;
    message.value = value;
    return message;
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

const Diagnostic*
FirstOfCode(const std::vector<Diagnostic>& diagnostics, DiagnosticCode code)
{
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            return &diagnostic;
        }
    }
    return nullptr;
}

bool
Near(double a, double b)
{
    return std::abs(a - b) <= 1e-9;
}

// ---------------------------------------------------------------------------
// Frame boundaries
// ---------------------------------------------------------------------------

void
TestABundledSenderIsClosedByTheNextClock()
{
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    assert(assembler.Push(BundledFrame(12.5), 0.010, &frames, &diagnostics)
           == 0);
    // Nothing is emitted until the sender proves the frame is over: a frame
    // held open is a frame that can still gain a bone.
    assert(frames.empty());

    assert(assembler.Push(BundledFrame(12.533), 0.043, &frames, &diagnostics)
           == 1);
    assert(frames.size() == 1);
    assert(Near(frames[0].pose.timestamp, 12.5));
    assert(frames[0].timestampFromSender);
    assert(frames[0].pose.validRotations.count() == kRig.size());
    assert(frames[0].pose.root.hasPosition);

    // A stream has no end marker, so the last frame is the caller's to ask for.
    assert(assembler.Flush(&frames, &diagnostics) == 1);
    assert(frames.size() == 2);
    assert(Near(frames[1].pose.timestamp, 12.533));
    assert(diagnostics.empty());
    assert(assembler.GetStats().framesEmitted == 2);
}

void
TestAnUnbundledSenderIsClosedByARepeat()
{
    // The opposite convention, and the reason no rule about `/VMC/Ext/T` alone
    // can work: here the clock arrives *after* the bones it belongs to, so a
    // decoder that treated it as an opening marker would stamp every frame with
    // the following frame's time.
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    for (int index = 0; index != 2; ++index) {
        const double seconds = 20.0 + index / 30.0;
        assert(assembler.Push(OneMessage(RootMessage()), 0.0, &frames,
                              &diagnostics)
               == (index == 0 ? 0u : 1u));
        for (const HumanBone bone : kRig) {
            assert(assembler.Push(OneMessage(BoneMessage(bone)), 0.0, &frames,
                                  &diagnostics)
                   == 0);
        }
        assert(assembler.Push(OneMessage(TimeMessage(seconds)), 0.0, &frames,
                              &diagnostics)
               == 0);
    }
    assert(assembler.Flush(&frames, &diagnostics) == 1);

    assert(frames.size() == 2);
    assert(Near(frames[0].pose.timestamp, 20.0));
    assert(Near(frames[1].pose.timestamp, 20.0 + 1.0 / 30.0));
    assert(frames[0].pose.validRotations.count() == kRig.size());
    assert(frames[1].pose.validRotations.count() == kRig.size());
    assert(diagnostics.empty());
}

void
TestARepeatInsideOneDatagramIsADuplicate()
{
    // The same repetition means two different things depending on how it was
    // delivered, and this is the only place that distinction is visible: one
    // datagram is one indivisible send, so a bone twice inside it is a sender
    // repeating itself rather than a frame boundary.
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    VmcPacket packet = BundledFrame(1.0);
    packet.messages.push_back(BoneMessage(HumanBone::Chest));
    packet.messages.push_back(RootMessage());

    assert(assembler.Push(packet, 0.0, &frames, &diagnostics) == 0);
    assert(assembler.Flush(&frames, &diagnostics) == 1);

    assert(frames.size() == 1);
    assert(frames[0].duplicateBones == 2);
    assert(frames[0].pose.validRotations.count() == kRig.size());
    assert(CountCode(diagnostics, DiagnosticCode::DuplicateBone) == 2);
    assert(diagnostics[0].subject == VmcHumanBoneName(HumanBone::Chest));
    assert(diagnostics[0].severity == DiagnosticSeverity::Warning);
    assert(diagnostics[0].recoverable);
    assert(diagnostics[1].subject == "/VMC/Ext/Root/Pos");
    assert(assembler.GetStats().bonesDuplicated == 2);
}

void
TestASecondClockEndsTheFrameWhereverItArrived()
{
    // Two clocks in one datagram is the one case the duplicate rule above does
    // *not* cover: a repeated bone is genuinely ambiguous and a repeated clock
    // is not, so this splits rather than being reported as a repeat.
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    VmcPacket packet = BundledFrame(3.0);
    packet.messages.push_back(TimeMessage(4.0));
    packet.messages.push_back(BoneMessage(HumanBone::Head));

    assert(assembler.Push(packet, 0.0, &frames, &diagnostics) == 1);
    assert(assembler.Flush(&frames, &diagnostics) == 1);
    assert(frames.size() == 2);
    assert(Near(frames[0].pose.timestamp, 3.0));
    assert(Near(frames[1].pose.timestamp, 4.0));
    assert(frames[1].pose.validRotations.count() == 1);
}

void
TestAFrameBoundaryCarryingNothingIsRefused()
{
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    assert(assembler.Push(OneMessage(TimeMessage(1.0)), 0.0, &frames,
                          &diagnostics)
           == 0);
    // A clock with no content behind it: the second one closes a frame that
    // never had anything in it, and an empty pose downstream is
    // indistinguishable from a rig that reports nothing.
    assert(assembler.Push(OneMessage(TimeMessage(2.0)), 0.0, &frames,
                          &diagnostics)
           == 0);
    assert(frames.empty());
    assert(assembler.GetStats().framesRefusedEmpty == 1);
    assert(CountCode(diagnostics, DiagnosticCode::IncompleteFrame) == 1);
}

void
TestABlendValueIsAssembledLikeABone()
{
    // Both halves of the repeat rule, on a blend shape rather than a bone,
    // because the rule is the same rule and a second convention for expressions
    // is the thing this layer is written to avoid.
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    // Twice inside one datagram: a sender repeating itself.
    VmcPacket bundled = BundledFrame(1.0);
    bundled.messages.push_back(BlendMessage("Joy", 0.5f));
    bundled.messages.push_back(BlendMessage("A", 0.0f));
    bundled.messages.push_back(BlendMessage("Joy", 0.9f));
    assert(assembler.Push(bundled, 0.0, &frames, &diagnostics) == 0);
    assert(frames.empty());
    assert(assembler.GetStats().expressionsDuplicated == 1);
    assert(assembler.GetStats().expressionsAccepted == 2);
    assert(CountCode(diagnostics, DiagnosticCode::DuplicateBone) == 1);
    assert(diagnostics[0].subject == "Joy");

    // The same name in a *later* datagram: the sender has moved on, so it ends
    // the frame exactly as a repeated bone does.
    assert(assembler.Push(OneMessage(BlendMessage("Joy", 0.1f)), 0.033, &frames,
                          &diagnostics)
           == 1);
    assert(frames.size() == 1);

    // The first value stood, and the reported zero is a value: a reader that
    // dropped it would find one name here instead of two.
    const motion::ExpressionWeights& weights = frames[0].pose.expressions;
    assert(weights.entries.size() == 2);
    assert(weights.Find("Joy") != nullptr && *weights.Find("Joy") == 0.5f);
    assert(weights.Find("A") != nullptr && *weights.Find("A") == 0.0f);
    assert(weights.Find("Blink") == nullptr);
}

void
TestAFrameCarryingOnlyExpressionsIsEmitted()
{
    // No committed capture contains a sender that sends a face and no body, so
    // this is unit-tested rather than pinned by a fixture -- the same choice the
    // receive-clock fallback below is given, and for the same reason: inventing
    // the capture would be a claim about a sender nobody has recorded.
    //
    // What is being pinned is that expressions are content. The emptiness rule
    // predates a pose being able to hold them, and had it been left alone these
    // weights would have been assembled and then discarded at the boundary.
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    VmcPacket packet;
    packet.messages.push_back(TimeMessage(5.0));
    packet.messages.push_back(BlendMessage("Blink", 1.0f));
    assert(assembler.Push(packet, 0.0, &frames, &diagnostics) == 0);
    assert(assembler.Flush(&frames, &diagnostics) == 1);

    assert(frames.size() == 1);
    assert(Near(frames[0].pose.timestamp, 5.0));
    assert(!frames[0].pose.validRotations.any());
    assert(frames[0].pose.expressions.entries.size() == 1);
    assert(assembler.GetStats().framesRefusedEmpty == 0);
}

// ---------------------------------------------------------------------------
// Clocks
// ---------------------------------------------------------------------------

void
TestAFrameWithNoSenderClockFallsBackToArrival()
{
    // No committed capture contains a sender that sends no clock, and the
    // fallback still has to be decided: refusing the frame would throw away
    // usable motion over a field VMC does not require per frame.
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    VmcPacket first = BundledFrame(0.0);
    first.messages.erase(first.messages.begin());
    VmcPacket second = first;

    assert(assembler.Push(first, 0.25, &frames, &diagnostics) == 0);
    assert(assembler.Push(second, 0.50, &frames, &diagnostics) == 1);
    assert(assembler.Flush(&frames, &diagnostics) == 1);

    assert(frames.size() == 2);
    assert(Near(frames[0].pose.timestamp, 0.25));
    assert(Near(frames[1].pose.timestamp, 0.50));
    // The flag is the whole point: the two clocks share no origin, so a reader
    // that mixes them has to be able to see that it is doing so.
    assert(!frames[0].timestampFromSender);
    assert(!frames[1].timestampFromSender);
}

void
TestAFrameThatDoesNotAdvanceIsRefused()
{
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    assembler.Push(BundledFrame(30.000), 0.010, &frames, &diagnostics);
    // A duplicated delivery arrives as the same instant twice. Equal is not
    // greater, and a stream that emitted the same instant twice has not moved.
    assembler.Push(BundledFrame(30.000), 0.011, &frames, &diagnostics);
    assembler.Push(BundledFrame(29.980), 0.045, &frames, &diagnostics);
    assembler.Push(BundledFrame(30.033), 0.078, &frames, &diagnostics);
    assembler.Flush(&frames, &diagnostics);

    assert(frames.size() == 2);
    assert(Near(frames[0].pose.timestamp, 30.000));
    assert(Near(frames[1].pose.timestamp, 30.033));
    assert(assembler.GetStats().framesRefusedOutOfOrder == 2);
    assert(CountCode(diagnostics, DiagnosticCode::TimestampRegression) == 2);
    assert(diagnostics[0].recoverable);
    // The bones were decoded and mapped before the frame they belonged to was
    // dropped, so the two counters disagree on purpose.
    assert(assembler.GetStats().bonesAccepted == 4 * 4);
    assert(assembler.GetStats().framesEmitted == 2);
}

void
TestABigBackwardsJumpIsARestartAndNotARegression()
{
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    assembler.Push(BundledFrame(30.000), 0.010, &frames, &diagnostics);
    assembler.Push(BundledFrame(0.016), 0.900, &frames, &diagnostics);
    assembler.Push(BundledFrame(0.050), 0.933, &frames, &diagnostics);
    assembler.Flush(&frames, &diagnostics);

    assert(frames.size() == 3);
    assert(!frames[0].beginsNewSession);
    assert(frames[1].beginsNewSession);
    assert(!frames[2].beginsNewSession);
    // Reported and not repaired: the sender's own clock comes out verbatim, and
    // what a restart means to a consumer is the consumer's to decide.
    assert(Near(frames[1].pose.timestamp, 0.016));
    assert(assembler.GetStats().sessionRestarts == 1);
    assert(assembler.GetStats().framesRefusedOutOfOrder == 0);
    assert(CountCode(diagnostics, DiagnosticCode::SourceRestarted) == 1);
    assert(CountCode(diagnostics, DiagnosticCode::TimestampRegression) == 0);

    // A zero threshold disables restart detection rather than making every
    // backwards frame a restart -- which is the reading the field's name
    // invites, and the opposite of what it does. The same stream then stalls on
    // the old clock instead of splitting into two sessions.
    VmcFrameConfig never;
    never.restartBackwardsSeconds = 0.0;
    VmcFrameAssembler held(never);
    std::vector<VmcFrame> stalled;
    held.Push(BundledFrame(30.000), 0.010, &stalled, nullptr);
    held.Push(BundledFrame(0.016), 0.900, &stalled, nullptr);
    held.Push(BundledFrame(0.050), 0.933, &stalled, nullptr);
    held.Flush(&stalled, nullptr);
    assert(stalled.size() == 1);
    assert(held.GetStats().sessionRestarts == 0);
    assert(held.GetStats().framesRefusedOutOfOrder == 2);
}

void
TestANewBoneJoinsTheFrameThatIsOpenEvenAfterItsClock()
{
    // A limit, recorded rather than desired. A bone the open frame does not
    // already carry is added to it whether or not the frame has met its clock,
    // so an unbundled sender whose *first* message of a new frame is a bone the
    // previous frame lacked hands that bone to the previous frame.
    //
    // The rule that would repair it -- content after the clock begins a new
    // frame -- is true of this sender and false of the bundled one, which is the
    // same asymmetry that made the clock's position unusable to begin with. So
    // this test exists to make the limit visible to whoever tries to fix it, and
    // to fail if the behaviour changes without the header changing with it.
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;

    for (const HumanBone bone : {HumanBone::Hips, HumanBone::Spine}) {
        assembler.Push(OneMessage(BoneMessage(bone)), 0.0, &frames);
    }
    assembler.Push(OneMessage(TimeMessage(1.0)), 0.0, &frames);
    // The next frame leads with a bone the first one never carried.
    assembler.Push(OneMessage(BoneMessage(HumanBone::Head)), 0.0, &frames);
    assembler.Push(OneMessage(BoneMessage(HumanBone::Hips)), 0.0, &frames);
    assembler.Push(OneMessage(TimeMessage(1.033)), 0.0, &frames);
    assembler.Flush(&frames, nullptr);

    assert(frames.size() == 2);
    assert(frames[0].pose.validRotations.count() == 3);
    assert(frames[0].pose.validRotations.test(
        static_cast<std::size_t>(HumanBone::Head)));
    assert(frames[1].pose.validRotations.count() == 1);

    // Narrow in practice for the reason the header gives: a Unity sender walks
    // `HumanBodyBones` in a fixed order, so a bone lost mid-chain and recovered
    // still sorts behind one that repeats first, and the frames come out whole.
    VmcFrameAssembler ordered;
    std::vector<VmcFrame> recovered;
    for (const HumanBone bone : {HumanBone::Hips, HumanBone::Spine,
                                 HumanBone::Chest}) {
        ordered.Push(OneMessage(BoneMessage(bone)), 0.0, &recovered);
    }
    ordered.Push(OneMessage(TimeMessage(1.0)), 0.0, &recovered);
    // `Chest` drops out for a frame and comes back in the next.
    for (const HumanBone bone : {HumanBone::Hips, HumanBone::Spine}) {
        ordered.Push(OneMessage(BoneMessage(bone)), 0.0, &recovered);
    }
    ordered.Push(OneMessage(TimeMessage(1.033)), 0.0, &recovered);
    for (const HumanBone bone : {HumanBone::Hips, HumanBone::Spine,
                                 HumanBone::Chest}) {
        ordered.Push(OneMessage(BoneMessage(bone)), 0.0, &recovered);
    }
    ordered.Push(OneMessage(TimeMessage(1.066)), 0.0, &recovered);
    ordered.Flush(&recovered, nullptr);

    assert(recovered.size() == 3);
    assert(recovered[0].pose.validRotations.count() == 3);
    assert(recovered[1].pose.validRotations.count() == 2);
    assert(recovered[1].missing.count() == 1);
    assert(recovered[2].pose.validRotations.count() == 3);
}

// ---------------------------------------------------------------------------
// Missing, and missing for too long
// ---------------------------------------------------------------------------

void
TestAMissingBoneIsReportedAndTheFrameIsKept()
{
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    assembler.Push(BundledFrame(1.0), 0.0, &frames, &diagnostics);
    assembler.Push(BundledFrame(1.033, {HumanBone::Hips, HumanBone::Spine}), 0.0,
                   &frames, &diagnostics);
    assembler.Flush(&frames, &diagnostics);

    assert(frames.size() == 2);
    // The frame survives: whether the gap becomes a held pose or an unbound
    // joint is `MissingBonePolicy`'s answer, one layer up.
    assert(frames[1].missing.count() == 2);
    assert(frames[1].missing.test(static_cast<std::size_t>(HumanBone::Chest)));
    assert(frames[1].missing.test(static_cast<std::size_t>(HumanBone::Head)));
    // And the assembler holds nothing forward itself, or the missing bones
    // would arrive downstream looking like fresh samples.
    assert(frames[1].pose.validRotations.count() == 2);
    assert(!frames[1].pose.validRotations.test(
        static_cast<std::size_t>(HumanBone::Chest)));
    assert(frames[1].stale.none());
    assert(assembler.GetStats().framesIncomplete == 1);
    assert(CountCode(diagnostics, DiagnosticCode::IncompleteFrame) == 1);

    // Measured against what the session has observed, never against the whole
    // fifty-five-bone humanoid: a rig that solves no fingers is complete.
    assert(assembler.GetObservedBones().count() == kRig.size());
    assert(!frames[0].missing.any());
}

void
TestAStaleBoneIsReportedOnceAndRecovers()
{
    // Deliberately between two frame gaps rather than on one. The frames below
    // step 0.05 s apart, so a horizon of 0.1 would put the third of them exactly
    // on the boundary -- and `1.0 + 2 * 0.05` is 1.1000000000000001, which makes
    // the answer a rounding coin flip rather than a claim about the assembler.
    VmcFrameConfig config;
    config.stalenessSeconds = 0.12;
    VmcFrameAssembler assembler(config);
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    assembler.Push(BundledFrame(1.0), 0.0, &frames, &diagnostics);
    for (int index = 1; index != 6; ++index) {
        assembler.Push(BundledFrame(1.0 + index * 0.05,
                                    {HumanBone::Hips, HumanBone::Spine}),
                       0.0, &frames, &diagnostics);
    }
    assembler.Push(BundledFrame(1.3), 0.0, &frames, &diagnostics);
    assembler.Flush(&frames, &diagnostics);

    assert(frames.size() == 7);
    // Missing from the first frame that drops it; stale only once the horizon
    // has actually been crossed.
    assert(frames[1].missing.count() == 2 && frames[1].stale.none());
    assert(frames[2].stale.none());
    assert(frames[3].stale.count() == 2);
    assert(frames[5].stale.count() == 2);
    // Two bones, one crossing each -- not one per bone per frame, which at 30 Hz
    // would bury the session in its own diagnostics.
    assert(assembler.GetStats().stalenessCrossings == 2);
    assert(CountCode(diagnostics, DiagnosticCode::StaleJoint) == 2);
    // Every frame that dropped a bone also reported itself incomplete, so the
    // stale one is found by its code rather than by its position: the two say
    // different things about the same gap and both are worth having.
    const Diagnostic* stale =
        FirstOfCode(diagnostics, DiagnosticCode::StaleJoint);
    assert(stale != nullptr);
    assert(stale->severity == DiagnosticSeverity::Warning);
    assert(stale->recoverable);
    // Named the way the sender wrote it, not the way VRM 1.0 spells it.
    assert(stale->subject == VmcHumanBoneName(HumanBone::Chest));
    assert(!motion::FindHumanBone(stale->subject));

    // The bones come back, and the horizon is armed again rather than spent.
    assert(frames[6].missing.none() && frames[6].stale.none());
    assembler.Push(BundledFrame(1.6, {HumanBone::Hips}), 0.0, &frames,
                   &diagnostics);
    assembler.Push(BundledFrame(1.9, {HumanBone::Hips}), 0.0, &frames,
                   &diagnostics);
    assembler.Flush(&frames, &diagnostics);
    assert(assembler.GetStats().stalenessCrossings == 5);
}

// ---------------------------------------------------------------------------
// The session
// ---------------------------------------------------------------------------

void
TestTheModelBecomesProvenanceAndThePathDoesNot()
{
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;

    VmcPacket handshake;
    handshake.messages.push_back(ModelMessage());
    assert(assembler.Push(handshake, 0.0, &frames, nullptr) == 0);
    // A handshake is not a frame: it carries no bone, no root, and no clock.
    assert(frames.empty());

    const motion::MotionSourceMetadata& metadata =
        assembler.GetSourceMetadata();
    assert(metadata.kind == motion::MotionSourceKind::LiveCapture);
    assert(metadata.protocol == "vmc");
    assert(metadata.sourceId == "Example Avatar");
    // The path names a file on the sender's machine; this adapter may not
    // resolve it and has no reason to carry it.
    assert(metadata.provider.empty());

    // Set on the session once rather than on thirty poses a second.
    assembler.Push(BundledFrame(1.0), 0.0, &frames, nullptr);
    assembler.Flush(&frames, nullptr);
    assert(frames.size() == 1);
    assert(!frames[0].pose.source.has_value());
    // VMC measures neither, and a pose that claimed otherwise would be gated by
    // a confidence floor it never earned.
    assert(!frames[0].pose.confidence.has_value());
    assert(!frames[0].pose.contacts.has_value());
}

void
TestTheHipsOffsetIsReachableAndNotComposed()
{
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;

    VmcPacket packet;
    packet.messages.push_back(TimeMessage(1.0));
    packet.messages.push_back(RootMessage());
    packet.messages.push_back(BoneMessage(HumanBone::Hips, {0.2f, 0.9f, 0.0f}));
    packet.messages.push_back(BoneMessage(HumanBone::Spine, {0.0f, 0.1f, 0.0f}));
    assembler.Push(packet, 0.0, &frames, nullptr);
    assembler.Flush(&frames, nullptr);

    assert(frames.size() == 1);
    assert(frames[0].hipsOffset.has_value());
    // Converted -- the reflection through X is the skeleton map's -- and left
    // where the root can still be told from it.
    assert(std::abs((*frames[0].hipsOffset)[0] + 0.2f) <= 1e-6f);
    assert(std::abs((*frames[0].hipsOffset)[1] - 0.9f) <= 1e-6f);
    assert(frames[0].pose.root.worldPosition.GetLength() == 0.0f);
}

void
TestARefusedSampleCostsItselfAndNotTheFrame()
{
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    VmcPacket packet = BundledFrame(1.0);
    VmcMessage unknown = BoneMessage(HumanBone::Head);
    unknown.name = "LeftPinkyProximal";
    packet.messages.push_back(unknown);
    VmcMessage broken = BoneMessage(HumanBone::Neck);
    broken.transform.rotation = {0.0f, 0.0f, 0.0f, 0.0f};
    packet.messages.push_back(broken);

    assembler.Push(packet, 0.0, &frames, &diagnostics);
    assembler.Flush(&frames, &diagnostics);

    assert(frames.size() == 1);
    assert(frames[0].pose.validRotations.count() == kRig.size());
    assert(assembler.GetStats().bonesUnsupported == 1);
    assert(assembler.GetStats().bonesMalformed == 1);
    assert(CountCode(diagnostics, DiagnosticCode::UnsupportedMessage) == 1);
    assert(CountCode(diagnostics, DiagnosticCode::PacketMalformed) == 1);
    // The layer's own diagnostics are stamped with the session's source and the
    // packet it was reading; the ones it passes through from the skeleton map
    // are stamped the same way, or a caller could not tell where either came
    // from.
    for (const Diagnostic& diagnostic : diagnostics) {
        assert(diagnostic.sequence.has_value() && *diagnostic.sequence == 1);
    }
}

void
TestTheSourceIsStampedOnEveryDiagnostic()
{
    VmcFrameAssembler assembler;
    assembler.SetSource("127.0.0.1:39539");
    std::vector<VmcFrame> frames;
    std::vector<Diagnostic> diagnostics;

    assembler.Push(BundledFrame(2.0), 0.0, &frames, &diagnostics);
    assembler.Push(BundledFrame(1.0), 0.0, &frames, &diagnostics);
    assembler.Flush(&frames, &diagnostics);

    assert(!diagnostics.empty());
    for (const Diagnostic& diagnostic : diagnostics) {
        assert(diagnostic.source == "127.0.0.1:39539");
    }
    assert(vrmAdapterVmc::FormatDiagnostic(diagnostics[0])
               .find("source=127.0.0.1:39539")
           != std::string::npos);
}

void
TestTheAssemblerSurvivesHavingNowhereToReport()
{
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;

    // Diagnostics are optional at every level below this one, and a receive loop
    // that keeps none still has to run.
    assert(assembler.Push(BundledFrame(2.0), 0.0, &frames) == 0);
    assert(assembler.Push(BundledFrame(1.0), 0.0, &frames) == 1);
    // The count is of frames *emitted*, not of frames closed: this packet ends
    // the backwards one above, which is refused rather than delivered.
    assert(assembler.Push(BundledFrame(3.0), 0.0, nullptr, nullptr) == 0);
    assert(assembler.Flush(nullptr, nullptr) == 1);
    // Counted whether or not anyone asked for the frames themselves.
    assert(assembler.GetStats().framesEmitted == 2);
    assert(assembler.GetStats().framesRefusedOutOfOrder == 1);
    assert(frames.size() == 1);
}

void
TestResetForgetsTheStreamAndKeepsTheTally()
{
    VmcFrameAssembler assembler;
    std::vector<VmcFrame> frames;

    assembler.Push(BundledFrame(30.0), 0.0, &frames, nullptr);
    assembler.Push(BundledFrame(30.033), 0.0, &frames, nullptr);
    assert(assembler.GetObservedBones().count() == kRig.size());

    assembler.Reset();
    assert(assembler.GetObservedBones().none());
    assert(assembler.GetSourceMetadata().sourceId.empty());
    // The open frame goes with it, so the flush finds nothing.
    assert(assembler.Flush(&frames, nullptr) == 0);
    // A clock that would have been a regression is now the start of a stream.
    assembler.Push(BundledFrame(1.0), 0.0, &frames, nullptr);
    assembler.Push(BundledFrame(2.0), 0.0, &frames, nullptr);
    assembler.Flush(&frames, nullptr);
    assert(frames.size() == 3);
    assert(assembler.GetStats().framesRefusedOutOfOrder == 0);
    // Stats describe the session the caller is judging, not the stream's state.
    assert(assembler.GetStats().framesEmitted == 3);
    assembler.ResetStats();
    assert(assembler.GetStats().framesEmitted == 0);
}

// ---------------------------------------------------------------------------
// Corpus
// ---------------------------------------------------------------------------

// `bones` and `roots` are the *same* numbers the skeleton-map suite pins, which
// are themselves the VMC layer's `kinds[BoneTransform]` and
// `kinds[RootTransform]`. Naming them a third time is the cross-layer claim: the
// assembler groups every sample the layer below produced and drops none of them
// on the floor, however many frames those samples turned out to be.
struct Expected
{
    const char* file;
    std::size_t frames;
    std::size_t refusedOutOfOrder;
    std::size_t incomplete;
    std::size_t restarts;
    std::size_t staleCrossings;
    std::size_t bones;
    std::size_t roots;
    // Blend-shape values that reached a frame. Zero for every capture that
    // sends none, which is most of them -- and the column exists so that a
    // capture which does send them cannot have them silently dropped again.
    std::size_t expressions;
};

constexpr Expected kExpected[] = {
    {"arm-raise-30hz.vmcpackets", 5, 0, 0, 0, 0, 105, 5, 0},
    // Its one `Blend/Val` carries a float where the name goes, so it is refused
    // a layer below this one and never reaches a frame.
    {"extended-forms.vmcpackets", 1, 0, 0, 0, 0, 21, 1, 0},
    {"malformed-forms.vmcpackets", 2, 0, 1, 0, 0, 41, 2, 0},
    {"malformed-packets.vmcpackets", 0, 0, 0, 0, 0, 0, 0, 0},
    // Three names in each of three frames: `Joy`, `Blink`, and `A` -- the last
    // of which is sent as 0.0, so this capture also records the difference
    // between a weight that is zero and a name that was never sent.
    {"mixed-traffic-30hz.vmcpackets", 3, 0, 0, 0, 0, 63, 3, 9},
    {"neutral-standing-30hz.vmcpackets", 5, 0, 0, 0, 0, 110, 5, 0},
    {"sender-restart-30hz.vmcpackets", 6, 2, 1, 1, 15, 153, 8, 0},
};

// Low enough that the one gap a capture actually records crosses it: the
// truncated frame in `sender-restart` leaves fifteen bones unreported for
// 0.0667 s, and the missing `Chest` in `malformed-forms` for 0.0333 s. A horizon
// between the two is what makes the corpus state the difference between a bone
// that is merely absent and one whose last value has stopped being current.
constexpr double kCorpusStaleness = 0.05;

bool
Replay(const std::filesystem::path& path, std::vector<VmcFrame>* frames,
       std::vector<Diagnostic>* diagnostics, VmcFrameAssembler* assembler)
{
    vrmAdapterVmc::PacketCapture capture;
    vrmAdapterVmc::PacketCaptureError error;
    if (!vrmAdapterVmc::ReadPacketCaptureFile(path.string(), &capture, &error)) {
        std::fprintf(stderr, "%s:%zu: %s\n", path.filename().string().c_str(),
                     error.line, error.message.c_str());
        return false;
    }
    assembler->SetSource(capture.sourceId);

    for (const vrmAdapterVmc::RecordedDatagram& datagram : capture.datagrams) {
        vrmAdapterVmc::OscPacket osc;
        if (!vrmAdapterVmc::DecodeOscPacket(datagram.bytes, &osc)) {
            continue;
        }
        vrmAdapterVmc::VmcPacket vmc;
        vrmAdapterVmc::DecodeVmcPacket(osc, &vmc);
        assembler->Push(vmc, datagram.receiveTime, frames, diagnostics);
    }
    assembler->Flush(frames, diagnostics);
    return true;
}

// The claim the corpus exists to make at this layer: two senders that put their
// clock at opposite ends of a frame produce the same frames at the same rate.
int
CheckTheTwoSenderShapesAgree(
    const std::vector<VmcFrame>& bundled,
    const std::vector<VmcFrame>& unbundled)
{
    if (bundled.size() != unbundled.size()) {
        std::fprintf(stderr,
                     "the bundled sender produced %zu frame(s) and the "
                     "unbundled one %zu -- the boundary rule reads one of the "
                     "two shapes wrong\n",
                     bundled.size(), unbundled.size());
        return 1;
    }
    int failures = 0;
    for (std::size_t index = 1; index != bundled.size(); ++index) {
        const double bundledStep =
            bundled[index].pose.timestamp - bundled[index - 1].pose.timestamp;
        const double unbundledStep = unbundled[index].pose.timestamp
            - unbundled[index - 1].pose.timestamp;
        // Six decimals is what the capture format records, so a cadence
        // recovered to better than that would be a claim about the fixture's
        // precision rather than about the assembler.
        if (std::abs(bundledStep - 1.0 / 30.0) > 1e-5
            || std::abs(unbundledStep - 1.0 / 30.0) > 1e-5) {
            std::fprintf(stderr,
                         "frame %zu steps %f s (bundled) and %f s (unbundled), "
                         "not the 30 Hz both were recorded at\n",
                         index, bundledStep, unbundledStep);
            ++failures;
        }
    }
    return failures;
}

int
CheckCorpus(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> captures;
    if (!std::filesystem::is_directory(directory)) {
        std::fprintf(stderr, "corpus directory not found: %s\n",
                     directory.string().c_str());
        return 1;
    }
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
    std::vector<VmcFrame> bundled;
    std::vector<VmcFrame> unbundled;

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
                         "%s: no expected assembly in this test -- add one, or "
                         "the capture is in the corpus and assembled by "
                         "nobody\n",
                         name.c_str());
            ++failures;
            continue;
        }
        covered.insert(name);

        VmcFrameConfig config;
        config.stalenessSeconds = kCorpusStaleness;
        VmcFrameAssembler assembler(config);
        std::vector<VmcFrame> frames;
        std::vector<Diagnostic> diagnostics;
        if (!Replay(path, &frames, &diagnostics, &assembler)) {
            ++failures;
            continue;
        }

        const vrmAdapterVmc::VmcFrameStats& stats = assembler.GetStats();
        if (frames.size() != entry->frames
            || stats.framesRefusedOutOfOrder != entry->refusedOutOfOrder
            || stats.framesIncomplete != entry->incomplete
            || stats.sessionRestarts != entry->restarts
            || stats.stalenessCrossings != entry->staleCrossings
            || stats.bonesAccepted != entry->bones
            || stats.rootsAccepted != entry->roots
            || stats.expressionsAccepted != entry->expressions) {
            std::fprintf(stderr,
                         "%s: %zu frame(s), %llu out of order, %llu "
                         "incomplete, %llu restart(s), %llu stale, %llu "
                         "bone(s), %llu root(s), %llu expression(s) -- "
                         "expected %zu, %zu, %zu, %zu, %zu, %zu, %zu, %zu\n",
                         name.c_str(), frames.size(),
                         static_cast<unsigned long long>(
                             stats.framesRefusedOutOfOrder),
                         static_cast<unsigned long long>(
                             stats.framesIncomplete),
                         static_cast<unsigned long long>(stats.sessionRestarts),
                         static_cast<unsigned long long>(
                             stats.stalenessCrossings),
                         static_cast<unsigned long long>(stats.bonesAccepted),
                         static_cast<unsigned long long>(stats.rootsAccepted),
                         static_cast<unsigned long long>(
                             stats.expressionsAccepted),
                         entry->frames, entry->refusedOutOfOrder,
                         entry->incomplete, entry->restarts,
                         entry->staleCrossings, entry->bones, entry->roots,
                         entry->expressions);
            ++failures;
            continue;
        }

        // Nothing here is refused for a duplicate: every repetition the corpus
        // records arrives in its own datagram and is therefore a frame boundary.
        // A committed capture that started duplicating inside a bundle would
        // change the frame count above, so this says which of the two rules is
        // doing the work.
        if (stats.bonesDuplicated != 0 || stats.expressionsDuplicated != 0) {
            std::fprintf(stderr,
                         "%s: %llu bone and %llu expression duplicate(s) "
                         "inside one datagram\n",
                         name.c_str(),
                         static_cast<unsigned long long>(
                             stats.bonesDuplicated),
                         static_cast<unsigned long long>(
                             stats.expressionsDuplicated));
            ++failures;
        }

        // Every capture that carries a frame carries a sender clock for it, so
        // the arrival-time fallback is never reached here -- it is unit-tested
        // instead, which is the honest place for a phenomenon no sender in this
        // corpus produces.
        for (const VmcFrame& frame : frames) {
            if (!frame.timestampFromSender) {
                std::fprintf(stderr,
                             "%s: a frame at %f was stamped from the receive "
                             "clock\n",
                             name.c_str(), frame.pose.timestamp);
                ++failures;
                break;
            }
        }

        // The one capture that sends blend shapes. The counts above say three
        // arrived per frame; this says which three and with what weights, which
        // is what a count cannot: `A` is sent as 0.0 every frame, and a reader
        // that treated a zero weight as "not sent" would land on two names here
        // while the count above still read nine.
        if (name == "mixed-traffic-30hz.vmcpackets") {
            for (std::size_t index = 0; index != frames.size(); ++index) {
                const motion::ExpressionWeights& weights =
                    frames[index].pose.expressions;
                const float* joy = weights.Find("Joy");
                const float* blink = weights.Find("Blink");
                const float* a = weights.Find("A");
                const float expected = 0.25f * static_cast<float>(index);
                if (weights.entries.size() != 3 || !joy || !blink || !a
                    || std::abs(*joy - expected) > 1e-6f
                    || std::abs(*blink - (1.0f - expected)) > 1e-6f
                    || *a != 0.0f) {
                    std::fprintf(stderr,
                                 "%s: frame %zu carried %zu expression(s), not "
                                 "Joy/Blink/A at the recorded weights\n",
                                 name.c_str(), index, weights.entries.size());
                    ++failures;
                    break;
                }
                // Sent under the sender's own spelling, not normalised to a VRM
                // preset name: the vocabulary is the model's.
                if (weights.entries[0].name != "A"
                    || weights.entries[1].name != "Blink"
                    || weights.entries[2].name != "Joy") {
                    std::fprintf(stderr,
                                 "%s: frame %zu did not carry the sender's own "
                                 "names in name order\n",
                                 name.c_str(), index);
                    ++failures;
                    break;
                }
            }
        }

        if (name == "neutral-standing-30hz.vmcpackets") {
            bundled = frames;
            // The pose survives assembly as the pose the layer below decoded:
            // every bone of the full torso rig, every rotation identity, the
            // root at the origin. A boundary rule that split a frame in two
            // would still satisfy the identity check one layer down.
            for (const VmcFrame& frame : frames) {
                if (frame.pose.validRotations.count() != 22
                    || !frame.pose.root.hasPosition
                    || frame.pose.root.worldPosition.GetLength() != 0.0f
                    || frame.missing.any()) {
                    std::fprintf(stderr,
                                 "%s: a frame carried %zu bone(s) and %zu "
                                 "missing, not the whole 22-bone rig\n",
                                 name.c_str(),
                                 frame.pose.validRotations.count(),
                                 frame.missing.count());
                    ++failures;
                    break;
                }
            }
        }

        // The unbundled sender's five frames are the arm going up, one step per
        // frame. The skeleton-map suite pinned that the five angles exist; this
        // pins that they landed one per frame and in the order they were sent,
        // which is the thing a frame boundary can get wrong while every
        // individual rotation stays correct.
        if (name == "arm-raise-30hz.vmcpackets") {
            unbundled = frames;
            const pxr::GfQuatf identity(1.0f, pxr::GfVec3f(0.0f));
            const std::size_t arm =
                static_cast<std::size_t>(HumanBone::LeftUpperArm);
            for (std::size_t index = 0; index != frames.size(); ++index) {
                const float expected = static_cast<float>(
                    15.0 * index * 3.14159265358979323846 / 180.0);
                if (!frames[index].pose.validRotations.test(arm)
                    || std::abs(motion::AngleBetween(
                                    frames[index].pose.localRotations[arm],
                                    identity)
                                - expected)
                        > 1e-4f) {
                    std::fprintf(stderr,
                                 "%s: frame %zu is not the arm at %f degrees\n",
                                 name.c_str(), index, 15.0 * index);
                    ++failures;
                    break;
                }
            }
        }

        if (name == "sender-restart-30hz.vmcpackets") {
            // The four phenomena this capture was recorded for, read off the
            // frames rather than off the counters above: the duplicate delivery
            // produced no second pose, the truncated frame is missing exactly
            // the bones it did not carry and every one of them is stale, and
            // the restart is flagged on the one frame whose clock went back.
            std::set<double> instants;
            for (const VmcFrame& frame : frames) {
                instants.insert(frame.pose.timestamp);
            }
            if (instants.size() != frames.size()) {
                std::fprintf(stderr,
                             "%s: %zu frame(s) at %zu distinct instant(s) -- a "
                             "duplicated datagram became a duplicated pose\n",
                             name.c_str(), frames.size(), instants.size());
                ++failures;
            }
            const VmcFrame& truncated = frames[3];
            if (truncated.pose.validRotations.count() != 6
                || truncated.missing.count() != 15
                || truncated.stale != truncated.missing) {
                std::fprintf(stderr,
                             "%s: the truncated frame carried %zu bone(s), %zu "
                             "missing, %zu stale -- expected 6, 15, 15\n",
                             name.c_str(),
                             truncated.pose.validRotations.count(),
                             truncated.missing.count(),
                             truncated.stale.count());
                ++failures;
            }
            if (!frames[4].beginsNewSession
                || frames[4].pose.timestamp >= frames[3].pose.timestamp
                || frames[4].missing.any()) {
                std::fprintf(stderr,
                             "%s: the frame after the restart is not flagged, "
                             "or did not begin a session of its own\n",
                             name.c_str());
                ++failures;
            }
            if (CountCode(diagnostics, DiagnosticCode::SourceRestarted) != 1
                || CountCode(diagnostics, DiagnosticCode::TimestampRegression)
                    != 2
                || CountCode(diagnostics, DiagnosticCode::StaleJoint) != 15) {
                std::fprintf(stderr,
                             "%s: %zu restart, %zu regression, %zu stale "
                             "diagnostic(s) -- expected 1, 2, 15\n",
                             name.c_str(),
                             CountCode(diagnostics,
                                       DiagnosticCode::SourceRestarted),
                             CountCode(diagnostics,
                                       DiagnosticCode::TimestampRegression),
                             CountCode(diagnostics, DiagnosticCode::StaleJoint));
                ++failures;
            }
        }

        // A missing bone that is *not* stale, which is the contrast the
        // staleness horizon exists to draw: the same `Chest` gap, a frame
        // apart, on the other side of it.
        if (name == "malformed-forms.vmcpackets") {
            if (frames[1].missing.count() != 1 || frames[1].stale.any()
                || !frames[1].missing.test(
                    static_cast<std::size_t>(HumanBone::Chest))) {
                std::fprintf(stderr,
                             "%s: the second frame is missing %zu bone(s) and "
                             "stale on %zu -- expected the Chest, and nothing "
                             "stale\n",
                             name.c_str(), frames[1].missing.count(),
                             frames[1].stale.count());
                ++failures;
            }
        }

        std::printf("%s: %zu frame(s) assembled\n", name.c_str(),
                    frames.size());
    }

    for (const Expected& entry : kExpected) {
        if (covered.find(entry.file) == covered.end()) {
            std::fprintf(stderr, "%s: expected in this test, absent from %s\n",
                         entry.file, directory.string().c_str());
            ++failures;
        }
    }

    if (bundled.empty() || unbundled.empty()) {
        std::fprintf(stderr,
                     "the corpus lost one of its two sender shapes; the "
                     "boundary rule is then pinned by neither\n");
        ++failures;
    } else {
        failures += CheckTheTwoSenderShapesAgree(bundled, unbundled);
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("VMC frame assembly: %zu capture(s) verified, both sender "
                "shapes at %zu frames and 30 Hz\n",
                captures.size(), bundled.size());
    return 0;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc > 1) {
        return CheckCorpus(std::filesystem::path(argv[1]));
    }

    TestABundledSenderIsClosedByTheNextClock();
    TestAnUnbundledSenderIsClosedByARepeat();
    TestARepeatInsideOneDatagramIsADuplicate();
    TestASecondClockEndsTheFrameWhereverItArrived();
    TestAFrameBoundaryCarryingNothingIsRefused();
    TestABlendValueIsAssembledLikeABone();
    TestAFrameCarryingOnlyExpressionsIsEmitted();
    TestAFrameWithNoSenderClockFallsBackToArrival();
    TestAFrameThatDoesNotAdvanceIsRefused();
    TestABigBackwardsJumpIsARestartAndNotARegression();
    TestANewBoneJoinsTheFrameThatIsOpenEvenAfterItsClock();
    TestAMissingBoneIsReportedAndTheFrameIsKept();
    TestAStaleBoneIsReportedOnceAndRecovers();
    TestTheModelBecomesProvenanceAndThePathDoesNot();
    TestTheHipsOffsetIsReachableAndNotComposed();
    TestARefusedSampleCostsItselfAndNotTheFrame();
    TestTheSourceIsStampedOnEveryDiagnostic();
    TestTheAssemblerSurvivesHavingNowhereToReport();
    TestResetForgetsTheStreamAndKeepsTheTally();
    std::puts("vrmAdapterVmc frame assembler tests passed");
    return 0;
}
