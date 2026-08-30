// SPDX-License-Identifier: Apache-2.0
//
// The frame layer's seven policies, each as a case that fails without it
// (roadmap/osc-and-vrchat-trackers.md §9, VRC-4).
//
// The subject here is a *decision*, not a conversion, which changes what a test
// of it has to look like. Every suite below this one can check a value against
// an expected value; this one has to check that a boundary landed where the
// policy says it lands, and the way that goes wrong is subtle: every sample in
// a wrongly-cut frame is individually correct, and only the grouping is wrong.
// So the cases are built out of *sequences* — bursts, gaps, repeats, a peer
// changing — rather than out of single packets.
//
// Unit mode builds its packets rather than decoding them. That is deliberate:
// what a byte means is `test_tracker_message.cpp`'s subject and what an axis
// means is `test_tracking_space.cpp`'s, and a suite about frame boundaries that
// went through both would fail for three reasons at once. The corpus pass at
// the bottom is where this layer meets real bytes, and it reads the same
// fixtures those two read.
#include "vrmAdapterVrchatOsc/FrameAssembler.h"

#include "vrmAdapterVrchatOsc/PacketCapture.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using vrmAdapterVrchatOsc::Diagnostic;
using vrmAdapterVrchatOsc::DiagnosticCode;
using vrmAdapterVrchatOsc::TrackerChannel;
using vrmAdapterVrchatOsc::TrackerFrame;
using vrmAdapterVrchatOsc::TrackerFrameAssembler;
using vrmAdapterVrchatOsc::TrackerFrameConfig;
using vrmAdapterVrchatOsc::TrackerMessage;
using vrmAdapterVrchatOsc::TrackerPacket;
using vrmAdapterVrchatOsc::TrackerSample;

constexpr double kFrameSeconds = 0.017;
constexpr double kBurstSeconds = 0.000008;

TrackerMessage
Message(std::string_view identity, TrackerChannel channel, float x, float y,
        float z)
{
    TrackerMessage message;
    message.tracker.segment = identity;
    if (identity.size() == 1 && identity[0] >= '1' && identity[0] <= '8') {
        message.tracker.index =
            static_cast<std::uint8_t>(identity[0] - '0');
    }
    message.channel = channel;
    message.values = {{x, y, z}};
    return message;
}

// One datagram, which on this wire is one message: the recorded sender puts
// exactly one in every datagram and never bundles.
TrackerPacket
Datagram(const TrackerMessage& message)
{
    TrackerPacket packet;
    packet.messages.push_back(message);
    packet.messagesSeen = 1;
    return packet;
}

struct Session
{
    TrackerFrameAssembler assembler;
    std::vector<TrackerFrame> frames;
    std::vector<Diagnostic> diagnostics;

    explicit Session(const TrackerFrameConfig& config = {})
        : assembler(config)
    {
        assembler.SetSource("fixture");
    }

    void Send(std::string_view identity, TrackerChannel channel, double time,
              std::string_view peer = {}, float x = 0.0f, float y = 1.0f,
              float z = 0.0f)
    {
        assembler.Push(Datagram(Message(identity, channel, x, y, z)), time,
                       peer, &frames, &diagnostics);
    }

    // One turn of the measured cycle, as eight single-message datagrams inside
    // a burst.
    void Burst(double time, std::string_view peer = {},
               const std::vector<std::string_view>& identities = {"head", "1",
                                                                  "2", "3"},
               float shift = 0.0f)
    {
        int step = 0;
        for (std::string_view identity : identities) {
            Send(identity, TrackerChannel::Rotation, time + step++ * kBurstSeconds,
                 peer, 0.0f, 90.0f, 0.0f);
            Send(identity, TrackerChannel::Position, time + step++ * kBurstSeconds,
                 peer, shift, 1.5f, shift);
        }
    }

    std::size_t Count(DiagnosticCode code) const
    {
        return static_cast<std::size_t>(
            std::count_if(diagnostics.begin(), diagnostics.end(),
                          [code](const Diagnostic& diagnostic) {
                              return diagnostic.code == code;
                          }));
    }

    const TrackerSample* Find(const TrackerFrame& frame,
                              std::string_view identity) const
    {
        for (const TrackerSample& sample : frame.samples) {
            if (sample.tracker == identity) {
                return &sample;
            }
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// The two boundary rules
// ---------------------------------------------------------------------------

void
TestABurstIsOneFrameAndTheNextBurstEndsIt()
{
    Session session;
    session.Burst(0.000);
    // The frame is still open: nothing has said it ended, and on a stream
    // this is the truth rather than a deferral.
    assert(session.frames.empty());

    session.Burst(kFrameSeconds);
    // Closed the moment the next burst arrives, and a test that only
    // flushed would pass against an assembler with no boundary rule at all.
    //
    // **On this sender the gap is what gets there first**, and that is a
    // measured fact rather than a preference: the window is checked when a
    // datagram arrives, and 17 ms is past it before the repeated
    // `head/rotation` inside that datagram is ever read. The repeat rule is
    // what makes the same cut with no clock at all, which the next case
    // measures by disabling the window.
    assert(session.frames.size() == 1);
    assert(session.assembler.GetStats().framesClosedByGap == 1);
    assert(session.assembler.GetStats().framesClosedByRepeat == 0);

    assert(session.assembler.Flush(&session.frames, &session.diagnostics) == 1);
    assert(session.frames.size() == 2);
    assert(session.assembler.GetStats().framesClosedByFlush == 1);

    for (const TrackerFrame& frame : session.frames) {
        assert(frame.samples.size() == 4);
        assert(frame.partial == 0);
        assert(frame.duplicates == 0);
        assert(frame.missing.empty());
        // Four trackers, eight datagrams, and the frame is stamped with the
        // first of them rather than the last.
        assert(frame.receiveTime == frame.samples.front().receiveTime);
    }
    assert(session.frames[0].receiveTime == 0.0);
    assert(std::abs(session.frames[1].receiveTime - kFrameSeconds) < 1e-9);
}

void
TestTheGapRuleClosesWhatNoRepeatWould()
{
    // A sender whose next frame carries trackers the last one did not: no
    // address repeats, so the repeat rule never fires and only the window
    // separates the two. This is the case that earns the second rule.
    Session session;
    session.Send("1", TrackerChannel::Position, 0.000);
    session.Send("2", TrackerChannel::Position, 0.000008);
    assert(session.frames.empty());

    session.Send("3", TrackerChannel::Position, kFrameSeconds);
    assert(session.frames.size() == 1);
    assert(session.assembler.GetStats().framesClosedByGap == 1);
    assert(session.assembler.GetStats().framesClosedByRepeat == 0);
    assert(session.frames[0].samples.size() == 2);

    // And the claim the header makes about the recorded sender, measured
    // rather than asserted: the two rules cut the same stream the same way.
    // The window is turned off for the second run, so the repeat is the only
    // rule left -- and a repeat rule needs no clock, which is what makes it
    // the one that survives a sender whose timing is not this one's.
    TrackerFrameConfig noWindow;
    noWindow.frameWindowSeconds = 0.0;

    Session windowed;
    Session repeated{noWindow};
    for (int index = 0; index < 4; ++index) {
        windowed.Burst(index * kFrameSeconds);
        repeated.Burst(index * kFrameSeconds);
    }
    windowed.assembler.Flush(&windowed.frames, &windowed.diagnostics);
    repeated.assembler.Flush(&repeated.frames, &repeated.diagnostics);

    assert(windowed.assembler.GetStats().framesClosedByGap == 3);
    assert(windowed.assembler.GetStats().framesClosedByRepeat == 0);
    assert(repeated.assembler.GetStats().framesClosedByRepeat == 3);
    assert(repeated.assembler.GetStats().framesClosedByGap == 0);

    // Same frames, sample for sample and stamp for stamp.
    assert(windowed.frames.size() == repeated.frames.size());
    for (std::size_t index = 0; index < windowed.frames.size(); ++index) {
        const TrackerFrame& left = windowed.frames[index];
        const TrackerFrame& right = repeated.frames[index];
        assert(left.receiveTime == right.receiveTime);
        assert(left.samples.size() == right.samples.size());
        for (std::size_t sample = 0; sample < left.samples.size();
             ++sample) {
            assert(left.samples[sample].tracker
                   == right.samples[sample].tracker);
            assert(left.samples[sample].position
                   == right.samples[sample].position);
        }
    }
}

void
TestARepeatInsideOneDatagramIsADuplicate()
{
    // A bundle is one indivisible delivery, so a tracker appearing twice in it
    // is a sender repeating itself and not two frames in one send. The first
    // value stands, which is checked by value rather than by count -- keeping
    // the *last* would produce the same frame count and the wrong pose.
    TrackerPacket bundle;
    bundle.bundled = true;
    bundle.messages.push_back(
        Message("1", TrackerChannel::Position, 0.25f, 1.0f, 0.0f));
    bundle.messages.push_back(
        Message("1", TrackerChannel::Position, 9.75f, 1.0f, 0.0f));
    bundle.messagesSeen = 2;

    Session session;
    session.assembler.Push(bundle, 0.0, {}, &session.frames,
                           &session.diagnostics);
    session.assembler.Flush(&session.frames, &session.diagnostics);

    assert(session.frames.size() == 1);
    assert(session.frames[0].samples.size() == 1);
    assert(session.frames[0].duplicates == 1);
    assert(session.assembler.GetStats().messagesDuplicated == 1);
    // Mirrored through X by the canonical conversion, so the first value is
    // -0.25 and the second would have been -9.75.
    assert(std::abs(session.frames[0].samples[0].position[0] + 0.25f) < 1e-6f);

    // The same two messages in two *datagrams* are two frames, not a
    // duplicate. This is the pair that makes the rule a rule rather than a
    // spelling of "ignore repeats".
    Session split;
    split.Send("1", TrackerChannel::Position, 0.0, {}, 0.25f, 1.0f, 0.0f);
    split.Send("1", TrackerChannel::Position, 0.001, {}, 9.75f, 1.0f, 0.0f);
    split.assembler.Flush(&split.frames, &split.diagnostics);
    assert(split.frames.size() == 2);
    assert(split.frames[0].duplicates == 0);
}

// ---------------------------------------------------------------------------
// A sample is half a sample until the frame says otherwise
// ---------------------------------------------------------------------------

void
TestAPartialTrackerIsEmittedAndReported()
{
    // 96 % of the single-address loss on this wire falls on one address, at
    // 0.6-4.3 % of that address's frames -- so this is about once a second and
    // not an edge case (report 02 §3.1).
    Session session;
    session.Send("1", TrackerChannel::Position, 0.000);
    session.Send("2", TrackerChannel::Rotation, 0.000008);
    session.Send("2", TrackerChannel::Position, 0.000016);
    session.assembler.Flush(&session.frames, &session.diagnostics);

    assert(session.frames.size() == 1);
    const TrackerFrame& frame = session.frames[0];
    assert(frame.samples.size() == 2);
    assert(frame.partial == 1);
    assert(session.Count(DiagnosticCode::TrackerPartial) == 1);

    const TrackerSample* one = session.Find(frame, "1");
    assert(one && one->hasPosition && !one->hasRotation);
    // The absent half is a default and says so. It is *identity*, which is
    // exactly what a tracker at rest reports -- so a reader that ignored the
    // flag could not tell the invented value from a measured one, which is the
    // whole reason the flag exists.
    assert(one->rotation == pxr::GfQuatf::GetIdentity());
    assert(!one->complete());

    const TrackerSample* two = session.Find(frame, "2");
    assert(two && two->complete());

    // The refusal names the address family and the tracker, because that is
    // what this layer knows: it reports what the wire said, not a body part.
    const Diagnostic& partial = session.diagnostics.front();
    assert(partial.subject == "/tracking/trackers/1");
    assert(partial.severity == vrmAdapterVrchatOsc::DiagnosticSeverity::Warning);
    assert(partial.recoverable);
}

void
TestTheHeadIsATrackerWhoseNameIsNotANumber()
{
    Session session;
    session.Burst(0.000);
    session.assembler.Flush(&session.frames, &session.diagnostics);
    const TrackerFrame& frame = session.frames[0];

    assert(frame.headReference.has_value());
    // Not promoted and not moved: the head is where the sender put it, which
    // on this wire is first because the sender's cycle starts there and not
    // because this layer sorted it.
    assert(*frame.headReference == 0);
    assert(frame.samples[*frame.headReference].tracker == "head");
    assert(!frame.samples[*frame.headReference].index.has_value());
    assert(frame.samples[1].index == 1);

    // And a session with no head at all is complete. `head-absent` is the
    // fixture; this is the claim it pins.
    Session headless;
    headless.Burst(0.000, {}, {"1", "2", "3"});
    headless.assembler.Flush(&headless.frames, &headless.diagnostics);
    assert(headless.frames.size() == 1);
    assert(!headless.frames[0].headReference.has_value());
    assert(headless.frames[0].samples.size() == 3);
    assert(headless.frames[0].missing.empty());
}

// ---------------------------------------------------------------------------
// Four absences
// ---------------------------------------------------------------------------

void
TestMissingBecomesStaleOnceAndComesBack()
{
    Session session;
    // Five frames with everyone, then forty without tracker 3 -- which at
    // 17 ms a frame crosses the half-second horizon partway through.
    for (int index = 0; index < 5; ++index) {
        session.Burst(index * kFrameSeconds);
    }
    for (int index = 5; index < 45; ++index) {
        session.Burst(index * kFrameSeconds, {}, {"head", "1", "2"});
    }
    session.assembler.Flush(&session.frames, &session.diagnostics);

    assert(session.frames.size() == 45);
    assert(session.frames[4].missing.empty());
    // Missing from the first frame it is absent from, because absence is
    // measured against what the session has observed rather than against a
    // horizon.
    assert(session.frames[5].missing == std::vector<std::string>{"3"});
    assert(session.frames[5].stale.empty());
    // Stale only once the horizon is crossed: 0.5 s is 30 frames at 58 Hz.
    assert(session.frames.back().stale == std::vector<std::string>{"3"});
    // Reported once per crossing and not once per frame, or a 58 Hz stream
    // would bury a session in one tracker's absence.
    assert(session.assembler.GetStats().stalenessCrossings == 1);

    // It comes back, and goes away again: a second crossing, not a silence
    // swallowed by the first.
    for (int index = 45; index < 50; ++index) {
        session.Burst(index * kFrameSeconds);
    }
    for (int index = 50; index < 95; ++index) {
        session.Burst(index * kFrameSeconds, {}, {"head", "1", "2"});
    }
    session.assembler.Flush(&session.frames, &session.diagnostics);
    assert(session.assembler.GetStats().stalenessCrossings == 2);

    // And the rig is what the session saw, never the eight the surface
    // defines: a three-point setup is not sending an incomplete frame forever.
    assert(session.assembler.GetObservedTrackers().size() == 4);
}

void
TestASilenceIsNotARestart()
{
    // The pair this policy is made of, and the reason the capture format grew
    // a line: the same gap, told apart by identity alone.
    Session paused;
    paused.Burst(0.000, "192.168.1.8:51662");
    paused.Burst(kFrameSeconds, "192.168.1.8:51662");
    paused.Burst(5.000, "192.168.1.8:51662");
    paused.assembler.Flush(&paused.frames, &paused.diagnostics);

    assert(paused.frames.size() == 3);
    assert(paused.Count(DiagnosticCode::SourceTimeout) == 1);
    assert(paused.Count(DiagnosticCode::SourceRestarted) == 0);
    assert(paused.assembler.GetStats().sessionRestarts == 0);
    // Nothing was forgotten: the trackers on the far side of the silence are
    // the same session's, so none of them is reported as new and none of the
    // frames begins one.
    assert(!paused.frames[2].beginsNewSession);
    assert(paused.frames[2].missing.empty());

    Session restarted;
    restarted.Burst(0.000, "192.168.1.8:51662");
    restarted.Burst(kFrameSeconds, "192.168.1.8:51662");
    // The second session is a **different rig**, and it has to be: with the
    // same four trackers either side, an assembler that forgot nothing and one
    // that forgot everything produce the same frames, and the case would pass
    // against both. The mutation pass is what said so -- "the restart not
    // forgetting the old session's trackers" survived an earlier version of
    // this test unchanged.
    restarted.Burst(5.000, "192.168.1.8:50035", {"1", "2", "3"}, 4.0f);
    restarted.assembler.Flush(&restarted.frames, &restarted.diagnostics);

    assert(restarted.frames.size() == 3);
    // Both, in the order they happened: the silence, then the identity change.
    assert(restarted.Count(DiagnosticCode::SourceTimeout) == 1);
    assert(restarted.Count(DiagnosticCode::SourceRestarted) == 1);
    assert(restarted.diagnostics[0].code == DiagnosticCode::SourceTimeout);
    assert(restarted.diagnostics[1].code == DiagnosticCode::SourceRestarted);
    assert(restarted.frames[2].beginsNewSession);
    assert(restarted.frames[2].peer == "192.168.1.8:50035");
    assert(restarted.assembler.GetStats().sessionRestarts == 1);

    // Reported, not repaired. The old session's rig is gone rather than
    // carried across, so the new session's first frame is complete rather than
    // reporting a head that belonged to a session that ended.
    assert(restarted.frames[2].missing.empty());
    assert(restarted.assembler.GetObservedTrackers()
           == std::vector<std::string>({"1", "2", "3"}));
    // And the positions go with it: every tracker moved four metres across the
    // restart, which is a recalibration's shape exactly -- but the session that
    // was being compared against has ended, so there is nothing to compare and
    // no discontinuity to report. A restart that kept its old positions would
    // raise CALIBRATION_REQUIRED on every restart that moved.
    assert(!restarted.frames[2].followsDiscontinuity);
    assert(restarted.Count(DiagnosticCode::CalibrationRequired) == 0);
}

void
TestAPeerlessSessionNeverSeesARestart()
{
    // The deliberate outcome, stated as a test so it cannot be mistaken for an
    // oversight: a caller with no identity to give gets a timeout and never a
    // session boundary. Guessing one from silence would make every capture
    // written before the format could say report a restart nothing observed.
    Session session;
    session.Burst(0.000);
    session.Burst(30.000);
    session.assembler.Flush(&session.frames, &session.diagnostics);

    assert(session.frames.size() == 2);
    assert(session.Count(DiagnosticCode::SourceTimeout) == 1);
    assert(session.Count(DiagnosticCode::SourceRestarted) == 0);
    assert(!session.frames[1].beginsNewSession);
    assert(session.assembler.GetPeer().empty());
}

void
TestSimultaneityIsWhatMakesItACalibration()
{
    Session recalibrated;
    recalibrated.Burst(0.000);
    recalibrated.Burst(kFrameSeconds);
    recalibrated.Burst(2 * kFrameSeconds, {}, {"head", "1", "2", "3"}, 1.2f);
    recalibrated.assembler.Flush(&recalibrated.frames,
                                 &recalibrated.diagnostics);

    assert(recalibrated.frames.size() == 3);
    assert(!recalibrated.frames[1].followsDiscontinuity);
    assert(recalibrated.frames[2].followsDiscontinuity);
    assert(recalibrated.Count(DiagnosticCode::CalibrationRequired) == 1);
    // Nothing is refused: the frame is emitted and the flag is what a consumer
    // reads. A layer that dropped it would decide for the consumer what a
    // recalibration means to it.
    assert(recalibrated.frames[2].samples.size() == 4);

    // One tracker jumping is a tracking glitch and not a new room. Same
    // distance, same interval, one mover.
    Session glitch;
    glitch.Burst(0.000);
    glitch.Burst(kFrameSeconds);
    glitch.Send("head", TrackerChannel::Rotation, 2 * kFrameSeconds);
    glitch.Send("head", TrackerChannel::Position, 2 * kFrameSeconds + 0.000008,
                {}, 1.2f, 1.5f, 1.2f);
    for (std::string_view identity : {"1", "2", "3"}) {
        glitch.Send(identity, TrackerChannel::Rotation,
                    2 * kFrameSeconds + 0.000016);
        glitch.Send(identity, TrackerChannel::Position,
                    2 * kFrameSeconds + 0.000024);
    }
    glitch.assembler.Flush(&glitch.frames, &glitch.diagnostics);
    assert(glitch.frames.size() == 3);
    assert(!glitch.frames[2].followsDiscontinuity);
    assert(glitch.Count(DiagnosticCode::CalibrationRequired) == 0);

    // And one tracker is not a simultaneity at all, however far it moves --
    // with a single tracker the rule would be a speed limit wearing a
    // calibration's name.
    Session alone;
    alone.Send("1", TrackerChannel::Position, 0.000, {}, 0.0f, 1.0f, 0.0f);
    alone.Send("1", TrackerChannel::Position, kFrameSeconds, {}, 40.0f, 1.0f,
               0.0f);
    alone.assembler.Flush(&alone.frames, &alone.diagnostics);
    assert(alone.frames.size() == 2);
    assert(!alone.frames[1].followsDiscontinuity);
}

void
TestAFrameCarriesCanonicalValuesAndNotWireOnes()
{
    // The frame layer's samples are `TrackingSpace`'s output, not the wire's.
    // Checked on the axis the conversion mirrors, because that is the one a
    // frame assembler could silently pass through unconverted.
    Session session;
    session.Send("1", TrackerChannel::Position, 0.0, {}, 0.5f, 1.5f, 0.25f);
    session.assembler.Flush(&session.frames, &session.diagnostics);

    const TrackerSample& sample = session.frames[0].samples[0];
    assert(std::abs(sample.position[0] + 0.5f) < 1e-6f);
    assert(std::abs(sample.position[1] - 1.5f) < 1e-6f);
    assert(std::abs(sample.position[2] - 0.25f) < 1e-6f);
}

void
TestAnEmptyFrameIsNeverEmitted()
{
    // A packet of nothing but unsupported addresses decodes to a packet with
    // no messages, which must not become a frame: an emitted frame says a
    // tracker set was observed.
    Session session;
    TrackerPacket empty;
    empty.messagesSeen = 3;
    empty.unsupported = 3;
    assert(session.assembler.Push(empty, 0.0, {}, &session.frames,
                                  &session.diagnostics)
           == 0);
    assert(session.assembler.Flush(&session.frames, &session.diagnostics) == 0);
    assert(session.frames.empty());
    assert(session.assembler.GetStats().framesEmitted == 0);
}

void
TestARestartSurvivesADatagramThisLayerReadsNothingIn()
{
    // Port 9000 is a well-known one and anything on the network may send to
    // it, so the datagram that carries a new peer need not carry a message
    // this adapter accepts -- `mixed-traffic` is a whole fixture of that
    // shape. The session boundary has to survive it and reach the frame that
    // does open, or a caller reading `beginsNewSession` sees a stream that
    // restarted according to the diagnostics and never began a session
    // according to the frames.
    Session session;
    session.Burst(0.000, "192.168.1.8:51662");

    // The new peer's first datagram: an avatar parameter, which reaches this
    // layer as a packet with no messages in it.
    TrackerPacket foreign;
    foreign.messagesSeen = 1;
    foreign.unsupported = 1;
    session.assembler.Push(foreign, 5.000, "192.168.1.8:50035",
                           &session.frames, &session.diagnostics);
    session.Burst(5.017, "192.168.1.8:50035");
    session.assembler.Flush(&session.frames, &session.diagnostics);

    assert(session.assembler.GetStats().sessionRestarts == 1);
    assert(session.frames.size() == 2);
    assert(!session.frames[0].beginsNewSession);
    // The frame that actually opened the new session, one datagram later.
    assert(session.frames[1].beginsNewSession);
    assert(session.frames[1].peer == "192.168.1.8:50035");
}

void
TestACallerBuiltMessageWithNoChannelIsRefused()
{
    // `TrackerChannel::Count` is the enum's terminator rather than an
    // address, so no decoded message carries it -- but a `TrackerPacket` is
    // an aggregate and building one is a supported way to drive this class,
    // which the partial and empty cases above both do. The channel indexes
    // two fixed-width arrays, so without the guard this reads and then
    // writes past the end of both.
    TrackerMessage broken =
        Message("1", TrackerChannel::Position, 0.0f, 1.0f, 0.0f);
    broken.channel = TrackerChannel::Count;

    // The frame is opened first, and carrying *that tracker*, because that is
    // where the indexing happens: the duplicate-and-repeat scan reads
    // `channelSet[index][channel]` for every sample whose identity matches,
    // so the out-of-range read needs a sample to match against.
    Session session;
    session.Send("1", TrackerChannel::Position, 0.0, {}, 0.5f, 1.5f, 0.25f);
    session.assembler.Push(Datagram(broken), 0.000008, {}, &session.frames,
                           &session.diagnostics);
    session.assembler.Flush(&session.frames, &session.diagnostics);

    // Refused as a caller's mistake, which is what every caller-precondition
    // failure in this adapter raises, and the open frame is untouched.
    assert(session.frames.size() == 1);
    assert(session.frames[0].samples.size() == 1);
    assert(session.frames[0].samples[0].hasPosition);
    assert(!session.frames[0].samples[0].hasRotation);
    assert(session.Count(DiagnosticCode::PacketMalformed) == 1);
    assert(session.assembler.GetStats().messagesRefused == 1);

    // The *detail* is asserted, not just the code, and that is what makes this
    // a test of the guard rather than of the conversion: `MapTrackerPosition`
    // refuses a wrong channel too, with the same code and a different sentence
    // -- and it does so one statement *after* the scan that would already have
    // read out of range. Memory safety is not observable from a portable test;
    // which layer refused is.
    const Diagnostic& refusal = session.diagnostics.front();
    assert(refusal.detail == "a message carries no readable channel");
    assert(refusal.subject == "/tracking/trackers/1");
}

// ---------------------------------------------------------------------------
// Corpus mode: the same policies, against bytes
// ---------------------------------------------------------------------------

struct Expected
{
    const char* file;
    std::size_t frames;
    std::size_t samples;
    std::size_t partial;
    std::size_t restarts;
    std::size_t timeouts;
    std::size_t discontinuities;
    std::size_t staleCrossings;
};

// Derived from each generator function's structure rather than from a run: a
// capture of N frames over M trackers assembles to N frames of M samples unless
// the generator dropped something, and every departure below is one the
// fixture's docstring names.
constexpr Expected kExpected[] = {
    // Six frames, and the jump at frame 3 is one discontinuity: the frames
    // after it are all at the new origin, so nothing jumps again.
    {"calibration-jump.vrchatoscpackets", 6, 24, 0, 0, 0, 1, 0},
    // Three frames the generator wrote, and **four** frames out. The third
    // ends with `/tracking/trackers/1/position` sent a second time, 64 us
    // later, in a datagram of its own -- and a repeat in its own datagram is
    // the next turn of the cycle by this layer's rule, whatever the clock
    // says. So the second value is neither dropped as a duplicate nor
    // merged over the first: it opens a frame carrying one half-filled
    // sample, which is the one outcome that invents nothing. A keep-first
    // or keep-last policy would have had to choose a value to lose.
    {"duplicate-and-reordered.vrchatoscpackets", 4, 13, 1, 0, 0, 0, 0},
    // The whole numbered surface plus the head, two frames of nine.
    {"eight-trackers.vrchatoscpackets", 2, 18, 0, 0, 0, 0, 0},
    {"head-absent.vrchatoscpackets", 3, 9, 0, 0, 0, 0, 0},
    // Valid OSC whose arguments are not the addresses'. Two frames survive:
    // one clean, and the bundle whose `/tracking/trackers/1/rotation` is a
    // four-float quaternion. The partial is that bundle's tracker 1 -- a bad
    // message costs *that message* and not the seven that arrived with it,
    // which is what this fixture exists to say and what a partial sample
    // makes visible one layer up.
    {"malformed-forms.vrchatoscpackets", 2, 8, 1, 0, 0, 0, 0},
    // Nine undecodable datagrams and one good message.
    {"malformed-packets.vrchatoscpackets", 1, 1, 1, 0, 0, 0, 0},
    // Tracker frames beside everything else VRChat sends: two clean frames of
    // four, and twelve datagrams that reach this layer as nothing at all --
    // five foreign addresses, three tracker-shaped ones this adapter maps to
    // nothing, and four unreadable identities. A session made entirely of
    // those is a reading and not a fault, so none of them is a frame.
    {"mixed-traffic.vrchatoscpackets", 2, 8, 0, 0, 0, 0, 0},
    {"one-tracker.vrchatoscpackets", 3, 3, 0, 0, 0, 0, 0},
    // One channel for a whole session: every sample is partial, which is a
    // reading of the wire and not a fault -- a tracker's two halves are two
    // datagrams and either can be the only one there is.
    {"position-only.vrchatoscpackets", 3, 12, 12, 0, 0, 0, 0},
    {"rotation-only.vrchatoscpackets", 3, 12, 12, 0, 0, 0, 0},
    // The restart: two sessions of three frames, one timeout for the 4.85 s
    // dark window and one restart for the port that changed inside it.
    {"session-restart.vrchatoscpackets", 6, 24, 0, 1, 1, 0, 0},
    // The same silence with the same peer either side. One timeout, no
    // restart, and the tracker set survives -- which is the whole fixture.
    {"silent-gap.vrchatoscpackets", 6, 24, 0, 0, 1, 0, 0},
    // Seven frames of the eight the generator counts: one is lost whole, and
    // one arrives missing `/tracking/trackers/1/rotation`.
    {"three-trackers-58hz.vrchatoscpackets", 7, 28, 1, 0, 0, 0, 0},
    // A tracker that stops mid-session and never returns: six frames, the
    // first two with four trackers and the last four with three, so twenty
    // samples. **Nothing goes stale inside it**, and that is the finding
    // rather than a gap in the fixture -- the capture is 0.086 s long and
    // the horizon is half a second, so at this cadence a dropout is
    // indistinguishable from ordinary loss for thirty more frames than this
    // capture contains. What it does report is `missing`, from the first
    // frame the tracker is absent from.
    {"tracker-dropout.vrchatoscpackets", 6, 20, 0, 0, 0, 0, 0},
    // A whole frame in one OSC bundle, three times over.
    {"bundled-frame.vrchatoscpackets", 3, 12, 0, 0, 0, 0, 0},
};

int
CheckCorpus(const std::filesystem::path& directory)
{
    if (!std::filesystem::is_directory(directory)) {
        std::fprintf(stderr, "corpus directory not found: %s\n",
                     directory.string().c_str());
        return 1;
    }

    std::vector<std::filesystem::path> captures;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()
            && entry.path().extension() == ".vrchatoscpackets") {
            captures.push_back(entry.path());
        }
    }
    std::sort(captures.begin(), captures.end());
    if (captures.empty()) {
        std::fprintf(stderr, "no .vrchatoscpackets fixtures in %s\n",
                     directory.string().c_str());
        return 1;
    }

    int failures = 0;
    for (const std::filesystem::path& path : captures) {
        const std::string name = path.filename().string();
        const Expected* expected = nullptr;
        for (const Expected& candidate : kExpected) {
            if (name == candidate.file) {
                expected = &candidate;
                break;
            }
        }
        if (!expected) {
            std::fprintf(stderr,
                         "%s: no expected assembly in this test -- add one, or "
                         "the capture is in the corpus and assembled by "
                         "nobody\n",
                         name.c_str());
            ++failures;
            continue;
        }

        vrmAdapterVrchatOsc::PacketCapture capture;
        vrmAdapterVrchatOsc::PacketCaptureError error;
        if (!vrmAdapterVrchatOsc::ReadPacketCaptureFile(path.string(), &capture,
                                                        &error)) {
            std::fprintf(stderr, "%s:%zu: %s\n", name.c_str(), error.line,
                         error.message.c_str());
            ++failures;
            continue;
        }

        TrackerFrameAssembler assembler;
        assembler.SetSource(name);
        std::vector<TrackerFrame> frames;
        std::vector<Diagnostic> diagnostics;
        for (const vrmAdapterVrchatOsc::RecordedDatagram& datagram :
             capture.datagrams) {
            // The record's own peer, which is what makes a restart readable
            // from a file at all: this wire marks one with a source port and
            // with nothing else.
            const TrackerPacket packet =
                vrmAdapterVrchatOsc::DecodeTrackerDatagram(datagram.bytes);
            assembler.Push(packet, datagram.receiveTime, datagram.peer, &frames,
                           &diagnostics);
        }
        assembler.Flush(&frames, &diagnostics);

        std::size_t samples = 0;
        std::size_t partial = 0;
        for (const TrackerFrame& frame : frames) {
            samples += frame.samples.size();
            partial += frame.partial;
        }
        const auto& stats = assembler.GetStats();

        struct Check
        {
            const char* what;
            std::size_t actual;
            std::size_t claimed;
        };
        const Check checks[] = {
            {"frames", frames.size(), expected->frames},
            {"samples", samples, expected->samples},
            {"partial", partial, expected->partial},
            {"restarts", static_cast<std::size_t>(stats.sessionRestarts),
             expected->restarts},
            {"timeouts", static_cast<std::size_t>(stats.sourceTimeouts),
             expected->timeouts},
            {"discontinuities",
             static_cast<std::size_t>(stats.calibrationDiscontinuities),
             expected->discontinuities},
            {"stale crossings",
             static_cast<std::size_t>(stats.stalenessCrossings),
             expected->staleCrossings},
        };
        bool ok = true;
        for (const Check& check : checks) {
            if (check.actual != check.claimed) {
                std::fprintf(stderr, "%s: %s is %zu, expected %zu\n",
                             name.c_str(), check.what, check.actual,
                             check.claimed);
                ok = false;
            }
        }
        if (!ok) {
            ++failures;
            continue;
        }

        std::printf("%s: %zu frame(s), %zu sample(s), %zu partial, %llu "
                    "restart(s)\n",
                    name.c_str(), frames.size(), samples, partial,
                    static_cast<unsigned long long>(stats.sessionRestarts));
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d corpus capture(s) failed\n", failures);
        return 1;
    }
    std::printf("VRChat OSC frame corpus: %zu capture(s) assembled\n",
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

    TestABurstIsOneFrameAndTheNextBurstEndsIt();
    TestTheGapRuleClosesWhatNoRepeatWould();
    TestARepeatInsideOneDatagramIsADuplicate();
    TestAPartialTrackerIsEmittedAndReported();
    TestTheHeadIsATrackerWhoseNameIsNotANumber();
    TestMissingBecomesStaleOnceAndComesBack();
    TestASilenceIsNotARestart();
    TestARestartSurvivesADatagramThisLayerReadsNothingIn();
    TestAPeerlessSessionNeverSeesARestart();
    TestSimultaneityIsWhatMakesItACalibration();
    TestAFrameCarriesCanonicalValuesAndNotWireOnes();
    TestAnEmptyFrameIsNeverEmitted();
    TestACallerBuiltMessageWithNoChannelIsRefused();
    std::puts("vrmAdapterVrchatOsc frame assembler tests passed");
    return 0;
}
