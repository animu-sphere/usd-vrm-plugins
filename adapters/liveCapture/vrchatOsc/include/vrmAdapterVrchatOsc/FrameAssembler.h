// SPDX-License-Identifier: Apache-2.0
//
// Where a frame begins on a wire that has no clock, and what it means that a
// tracker is not in it.
//
// Everything below this layer converts: bytes into OSC, OSC into tracker
// messages, three floats into a canonical position or orientation. This is the
// first layer in this adapter that *decides* — which observations belong
// together, which tracker has stopped reporting, and when a stream has become a
// different stream (roadmap/osc-and-vrchat-trackers.md §9, VRC-4).
//
// It decides those and nothing else. **No body role, no calibration and no
// avatar joint is named in this file**, under any outcome: which tracker is on
// which body region is a generic assignment policy outside this adapter
// (VRC-4a), and what any of it means for a joint is the solve's
// ([§5](../../../../../docs/roadmap/osc-and-vrchat-trackers.md#5-a-tracker-source-is-not-a-pose-source)).
// What comes out is a `TrackerFrame` — observations with their identities
// intact — and not a pose.
//
// ## Frame boundaries, from a measurement rather than a convention
//
// VMC leaves the boundary open and marks it with a clock message; this wire has
// no clock at all. What it has instead was measured over 44 918 datagrams
// ([report 02](../../../../../docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md) §2, §3):
//
//     the cycle    head/rotation → head/position → 1/rotation → 1/position
//                  → 2/rotation → 2/position → 3/rotation → 3/position
//     the burst    all eight datagrams inside a median 0.053 ms
//     the interval ~17 ms between bursts, ~33.5 ms when a frame is lost whole
//
// **A frame on this wire is a burst, not a spread**, and the two facts that
// makes available are the two rules below. They are stated separately because
// they fail differently, and on the recorded sender they always agree — which
// is itself a fixture rather than an assumption.
//
// **A repeat closes the frame.** A tracker and channel the open frame already
// carries means the sender has come round the cycle again. This rule needs no
// timing at all and it survives loss: a frame that lost `/tracking/trackers/1/
// rotation` — which is 96 % of the single-address loss on this wire — is closed
// by the next `head/rotation` exactly as an intact one is, and comes out one
// sample short rather than merged with its successor. Inside *one* datagram the
// same repeat is a duplicate instead, for the reason the sibling gives: a
// datagram is one indivisible delivery, and a sender that put a tracker in a
// bundle twice repeated itself rather than packing two frames into one send.
//
// **A gap closes the frame.** A datagram arriving more than
// `frameWindowSeconds` after the first message of the open frame cannot belong
// to the same burst. The default is 5 ms, which is ninety times the measured
// burst and a third of the shortest interval between bursts, so on this sender
// the two rules close the same frames — and a sender that *spreads* its frame
// instead of bursting it is a sender this default is wrong for, which is why it
// is configuration and not a constant.
//
// A stream has no end marker, so `Flush()` closes whatever is still open. A
// caller that forgets it loses the last frame of every capture.
//
// ### What neither rule can do
//
// Neither can tell a *lost* datagram from one that was never sent. The corpus
// records that as a finding rather than a defect: `tracker-dropout` and a frame
// missing one address are the same absence at the decode layer, and this layer
// separates them only by how long the absence lasts (`missing` versus `stale`
// below). A sender that stopped and a network that dropped remain
// indistinguishable from the receiving end, which report 02 §3 measured and
// declined to attribute.
//
// ## A sample is half a sample until the frame says otherwise
//
// Position and rotation arrive on separate addresses, so a `TrackerMessage`
// fills one channel of one tracker and no message can fill both
// (TrackerMessage.h). This is the layer that owns the window in which two
// messages are one observation, and therefore the only layer that can say a
// tracker reported **half** of itself: `VRM_VRCHAT_OSC_TRACKER_PARTIAL` is
// raised here and nowhere below.
//
// A partial sample is **emitted, not repaired**. `hasPosition` and `hasRotation`
// say which halves are real, and the absent half is left at its default rather
// than held forward from the previous frame — because a defaulted rotation of
// identity is bit-for-bit the rotation a tracker at rest reports, and a held one
// is indistinguishable from a fresh one. Whether a gap becomes a held value is
// the consumer's policy, exactly as `MissingBonePolicy` is one adapter over, and
// answering it here would take the decision away from the layer that has the
// context to make it.
//
// This is not a rare shape. Single-address loss runs at 0.6–4.3 % of one
// address's frames on the recorded sender, which is a partial tracker about once
// a second (report 02 §3.1).
//
// ## The head is a tracker whose name is not a number
//
// `head` occupies the path position an index occupies, and the corpus carries a
// capture with no head at all. So it is a sample like any other — it is not
// promoted, not required, and not moved to the front of the frame — and
// `headReference` is an *index into* `samples` rather than a copy of one. A
// frame carrying no head is complete; a consumer that needs one says so itself.
//
// ## Missing, stale, silent, and restarted — four different absences
//
// | absence | what it means | how it is reported |
// | --- | --- | --- |
// | missing | a tracker the session has seen did not arrive in this frame | `TrackerFrame::missing` |
// | stale | it has not arrived for `stalenessSeconds` | `TrackerFrame::stale`, once per crossing |
// | silent | *nothing* has arrived for `sourceTimeoutSeconds` | `VRM_VRCHAT_OSC_SOURCE_TIMEOUT` |
// | restarted | a second session began | `VRM_VRCHAT_OSC_SOURCE_RESTARTED` |
//
// Missing and stale are measured against the trackers this session has actually
// observed, never against the eight the surface defines: a three-point setup is
// not sending an incomplete frame fifty-eight times a second, it is a complete
// frame from a rig with three trackers. Neither has a diagnostic code, and that
// is deliberate — the ten codes were frozen before this directory existed and an
// eleventh is a contract change (Diagnostics.h), so a per-tracker absence is
// reported as *data on the frame* where a consumer can apply its own policy.
//
// ### A silence is not a restart, and this is the line the format was widened for
//
// The measured restart on this wire is a **new ephemeral source port** and
// nothing else — no session identifier, no rest table, no handshake — with a
// 4.85 s dark window around it (report 02 §4). Until 2026-08-30 a capture could
// not carry that identity at all, so a replayed restart was indistinguishable
// from a pause; `liveTransport`'s `p` line is what changed, and `Push` takes the
// peer for exactly this reason.
//
// So the policy is split along what is actually observable:
//
// * a peer that differs from the session's peer **is** a restart. The assembler
//   raises `SOURCE_RESTARTED`, drops what it learned from the old session, and
//   marks the next frame `beginsNewSession`;
// * a silence longer than `sourceTimeoutSeconds` is **`SOURCE_TIMEOUT` and
//   nothing more**, however long it lasts. A stream that goes quiet for a minute
//   and comes back on the same port is one source that paused.
//
// **A caller that supplies no peer therefore never sees a restart**, and that is
// the honest outcome rather than a limitation to work around: guessing a restart
// from silence would make every fixture written before the format could say
// report a session boundary that nothing observed. The corpus carries both
// halves — one capture whose peer changes across the gap and one whose does not.
//
// A restart is reported and **not repaired**: the assembler forgets the old
// session's trackers and clock rather than splicing the two streams together.
// Deciding what continuity means across a recalibration is the consumer's, and
// inventing it here is the class of invention §2 forbids an adapter.
//
// ## Calibration discontinuity, which only simultaneity can distinguish
//
// VRChat's tracking space is the *player's*, established by a calibration the
// receiving application performs. When it is redone, every tracker moves at
// once, into a space whose relationship to the old one this layer cannot
// compute. A single tracker jumping is not that — it is a tracking glitch, and
// treating the two the same would make a lost sensor look like a new room.
//
// So the rule is simultaneity rather than size: when **every** observed tracker
// that had a position in the previous frame moves further than
// `calibrationJumpMeters` between consecutive frames, the frame is marked
// `followsDiscontinuity` and `VRM_VRCHAT_OSC_CALIBRATION_REQUIRED` is raised.
// Two trackers are required for the check to run at all, because with one there
// is no simultaneity to observe and the rule would be a speed limit wearing a
// calibration's name.
//
// **No recorded session contains a recalibration**, so the default threshold is
// a stated policy and not a measurement — the corpus fixture that produces one
// is marked `unobserved` for that reason. What makes the default safe rather
// than arbitrary is the arithmetic around it: at 58 Hz, 0.5 m between frames is
// 29 m/s, and a body does not do that in a room. Nothing is refused either way;
// the frame is emitted and the flag is what a consumer reads.
#pragma once

#include "vrmAdapterVrchatOsc/Diagnostics.h"
#include "vrmAdapterVrchatOsc/TrackerMessage.h"
#include "vrmAdapterVrchatOsc/TrackingSpace.h"
#include "vrmAdapterVrchatOsc/api.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vrmAdapterVrchatOsc
{

struct TrackerFrameConfig
{
    // How far apart two datagrams may be and still belong to one frame,
    // measured on the receive clock. The measured burst is 0.053 ms wide and
    // the measured interval between bursts is 17 ms, so anything between those
    // two separates them; 5 ms sits ninety times above the first and three
    // times below the second. Zero disables the rule and leaves the repeat rule
    // to close every frame, which is what a sender that spreads its frame over
    // a whole period needs.
    double frameWindowSeconds = 0.005;

    // How long a tracker the session has observed may go unreported before its
    // last value stops being called current. Measured in receive time, so a
    // replayed capture means what a live session means. Zero disables the
    // check; `missing` is unaffected either way.
    double stalenessSeconds = 0.5;

    // How long the *whole stream* may go quiet before `SOURCE_TIMEOUT`. The
    // measured restart's dark window is 4.85 s and the measured frame interval
    // is 17 ms, so one second is far above the loss this wire produces and far
    // below the gap a stopped sender leaves. Zero disables the report.
    double sourceTimeoutSeconds = 1.0;

    // How far every observed tracker must move, at once, between consecutive
    // frames for the move to be read as a recalibration rather than as motion.
    // Zero disables the check. See the header: no session has recorded one, so
    // this is a stated policy whose safety is arithmetic — 0.5 m in one frame
    // period at 58 Hz is 29 m/s.
    double calibrationJumpMeters = 0.5;
};

// One tracker's observation, in the canonical basis.
//
// The identity is **owned** here, where `TrackerId::segment` is a view into the
// datagram it was decoded from. A frame outlives its buffer by construction —
// a receive loop reuses one — so a sample that borrowed would be dangling by
// the time a caller read it.
struct TrackerSample
{
    // Verbatim, as the address spelled it: "1", "head". Ordering, grouping and
    // equality are the segment's throughout this adapter, so the head is never
    // a special case in any of them.
    std::string tracker;
    // Set when the segment is a decimal in [MinTrackerIndex, MaxTrackerIndex].
    // Kept beside the segment because neither implies the other and a table
    // keyed on the index alone would collapse `head` onto tracker 0.
    std::optional<std::uint8_t> index;

    // Canonical: metres, right-handed, +Y up, +Z forward (TrackingSpace.h).
    // Read only where the matching flag is set — an unset half is a default and
    // not an observation, and the default rotation is exactly what a tracker at
    // rest reports.
    pxr::GfVec3f position{0.0f, 0.0f, 0.0f};
    pxr::GfQuatf rotation = pxr::GfQuatf::GetIdentity();
    bool hasPosition = false;
    bool hasRotation = false;

    // The receive time of the first message that filled this sample. There is
    // no sender clock on this wire to prefer to it: VRChat's tracker addresses
    // carry three floats and no timestamp, so arrival order is the only clock
    // there is, and it is the receiver's.
    double receiveTime = 0.0;

    bool complete() const noexcept { return hasPosition && hasRotation; }
};

// One assembled frame: the observations, and the things an observation has
// nowhere to record.
struct TrackerFrame
{
    // In the order the frame first saw each tracker, which on the recorded
    // sender is the cycle's order with the head at its head. Not sorted, and
    // not reordered to put the head anywhere: the order a sender chose is
    // evidence, and `duplicate-and-reordered` is the fixture that says an
    // assembler may not depend on it.
    std::vector<TrackerSample> samples;

    // Index into `samples` of the tracker named `head`, when the frame carried
    // one. An index rather than a copy, so the head is one sample and not two —
    // see the header on why it is not promoted.
    std::optional<std::size_t> headReference;

    // The receive time of the first message of this frame.
    double receiveTime = 0.0;

    // The sender's endpoint, when the caller supplied one. Empty for a capture
    // written before the format could say who sent a datagram.
    std::string peer;

    // The sender restarted immediately before this frame: a different peer.
    // Reported, never repaired.
    bool beginsNewSession = false;

    // Every observed tracker moved further than the threshold at once. See the
    // header: simultaneity is what separates a recalibration from motion.
    bool followsDiscontinuity = false;

    // Trackers the session has observed that this frame did not carry, and the
    // subset whose last report is older than the staleness horizon. `stale` is
    // always a subset of `missing`. Identities, in the session's first-seen
    // order, because there is no fixed slot to index: the head has no number
    // and a numbered tracker's index is not a body role.
    std::vector<std::string> missing;
    std::vector<std::string> stale;

    // Samples carrying exactly one of the two channels.
    std::size_t partial = 0;
    // Messages refused as a repeat inside one datagram. The repeats are
    // dropped; the first value stands.
    std::size_t duplicates = 0;
};

// What an operator needs to judge a session without a debugger.
struct TrackerFrameStats
{
    std::uint64_t framesEmitted = 0;
    // Emitted while at least one observed tracker was absent.
    std::uint64_t framesIncomplete = 0;
    // Emitted carrying at least one half-filled sample.
    std::uint64_t framesPartial = 0;

    std::uint64_t samplesEmitted = 0;
    std::uint64_t positionsAccepted = 0;
    std::uint64_t rotationsAccepted = 0;
    // Repeats inside one datagram, and messages this layer refused: a
    // component that is not finite, or a channel outside the enum. Neither
    // can happen on the wire path -- the decoder refuses the first with the
    // same code and cannot produce the second -- and both are reachable from
    // a caller that built a `TrackerPacket` itself, which is a supported way
    // to drive this class.
    std::uint64_t messagesDuplicated = 0;
    std::uint64_t messagesRefused = 0;

    // Trackers that crossed the staleness horizon, counted per crossing rather
    // than per frame: a 58 Hz stream would otherwise bury a session in one
    // tracker's absence.
    std::uint64_t stalenessCrossings = 0;
    std::uint64_t sourceTimeouts = 0;
    std::uint64_t sessionRestarts = 0;
    std::uint64_t calibrationDiscontinuities = 0;

    // Frames closed by each rule, so the claim that the two agree on this
    // sender is a number a test can read rather than a sentence in a header.
    // A frame closed by both counts once, under the repeat.
    std::uint64_t framesClosedByRepeat = 0;
    std::uint64_t framesClosedByGap = 0;
    // Closed because the session ended under it: the peer changed.
    std::uint64_t framesClosedByRestart = 0;
    std::uint64_t framesClosedByFlush = 0;
};

// Feed it decoded packets in arrival order; take frames out.
//
// The unit is a packet rather than a message for the reason the sibling gives:
// the datagram boundary is what tells a duplicate from a new frame, and a
// caller that flattened its packets into a message stream would have thrown it
// away before this class could use it.
class VRMADAPTERVRCHATOSC_API TrackerFrameAssembler
{
public:
    explicit TrackerFrameAssembler(const TrackerFrameConfig& config = {});

    const TrackerFrameConfig& GetConfig() const noexcept { return _config; }

    // The endpoint or fixture name stamped on every diagnostic this assembler
    // raises, so a session replayed from a capture reports the same `source` a
    // live one would. This is the *listening* side; the peer below is the
    // sending side, and they are not the same string.
    void SetSource(std::string source);
    const std::string& GetSource() const noexcept { return _source; }

    // Accepts one decoded packet.
    //
    // `receiveTime` is the receiver's clock — the `d` record's time on a
    // replayed capture. `peer` is who sent it: `RecordedDatagram::peer` on a
    // replay, `ReceivedDatagram::peer` live, and **empty when the caller does
    // not know**, which is the case a capture written before the format could
    // say produces. A caller that passes empty throughout never sees a restart,
    // deliberately (see the header).
    //
    // Completed frames are appended to `frames` and diagnostics to
    // `diagnostics`; neither is ever cleared, so a caller can accumulate a
    // datagram's worth or a session's.
    //
    // Returns how many frames this packet emitted, which is how many were
    // appended — not how many it closed. A frame closed and then refused as
    // empty returns zero, and `GetStats()` is where the difference is read.
    std::size_t Push(const TrackerPacket& packet, double receiveTime,
                     std::string_view peer, std::vector<TrackerFrame>* frames,
                     std::vector<Diagnostic>* diagnostics = nullptr);

    // The peerless overload, for a caller that has no identity to give — a
    // fixture, or a capture from before the `p` line. Spelled as its own
    // function rather than as a defaulted argument so that "this session cannot
    // see a restart" is a decision at the call site.
    std::size_t Push(const TrackerPacket& packet, double receiveTime,
                     std::vector<TrackerFrame>* frames,
                     std::vector<Diagnostic>* diagnostics = nullptr)
    {
        return Push(packet, receiveTime, std::string_view(), frames,
                    diagnostics);
    }

    // Closes the frame still open at the end of a stream. Returns 1 when that
    // frame was emitted and 0 when there was none or it was refused.
    std::size_t Flush(std::vector<TrackerFrame>* frames,
                      std::vector<Diagnostic>* diagnostics = nullptr);

    // Trackers this session has observed at least once, in first-seen order.
    // This is the rig the completeness and staleness checks are measured
    // against, and it is learned from the stream rather than configured: a
    // three-point setup must not be reported as sending an incomplete frame
    // forever.
    const std::vector<std::string>& GetObservedTrackers() const noexcept
    {
        return _observed;
    }

    // The peer this session is currently attributed to, or empty when no caller
    // has named one.
    const std::string& GetPeer() const noexcept { return _peer; }

    const TrackerFrameStats& GetStats() const noexcept { return _stats; }
    void ResetStats() noexcept { _stats = TrackerFrameStats(); }

    // Drops the open frame, the observed trackers, the clock history and the
    // peer — everything a restart invalidates. Stats survive, because they
    // describe the session the caller is judging rather than the stream's
    // state.
    void Reset();

private:
    // What is under construction, which is not a `TrackerFrame`: this has no
    // completeness report yet and may never become a frame at all.
    struct OpenFrame
    {
        bool open = false;
        double receiveTime = 0.0;
        std::string peer;
        bool beginsNewSession = false;
        std::vector<TrackerSample> samples;
        std::size_t duplicates = 0;
        // Which packet contributed each channel of each sample, so a repeat
        // inside one datagram can be told from the next turn of the cycle.
        std::vector<std::array<std::uint64_t, TrackerChannelCount>> channelPacket;
        std::vector<std::array<bool, TrackerChannelCount>> channelSet;
    };

    // Opens a frame and consumes `_pendingNewSession`, which is the only
    // place that flag is read.
    void _Open(double receiveTime, std::string peer);
    bool _Close(std::vector<TrackerFrame>* frames,
                std::vector<Diagnostic>* diagnostics, const char* reason);
    void _Report(std::vector<Diagnostic>* diagnostics, DiagnosticCode code,
                 std::string_view subject, std::optional<double> timestamp,
                 std::string detail);
    // The index of `tracker` in the open frame, adding it when it is not there.
    std::size_t _SampleFor(const TrackerId& tracker, double receiveTime);

    TrackerFrameConfig _config;
    std::string _source;
    std::string _peer;

    OpenFrame _frame;
    std::uint64_t _packetSerial = 0;
    // A restart has been seen and the frame that begins the new session has
    // not opened yet. It is a member rather than a local of `Push` because
    // the datagram that carries the new peer need not carry a message this
    // layer accepts -- port 9000 is a well-known one and anything on the
    // network may send to it, so the first datagram of a new session can be
    // an avatar parameter. A local was lost in exactly that case, and the
    // session began on a frame that did not say so.
    bool _pendingNewSession = false;

    // First-seen order, and the last receive time each was reported at.
    std::vector<std::string> _observed;
    std::map<std::string, double> _lastSeen;
    // Trackers already reported stale, so a crossing is reported once rather
    // than on every frame until the tracker comes back.
    std::map<std::string, bool> _reportedStale;
    // The last emitted frame's positions, for the discontinuity check.
    std::map<std::string, pxr::GfVec3f> _lastPosition;

    std::optional<double> _lastArrival;
    TrackerFrameStats _stats;
};

} // namespace vrmAdapterVrchatOsc
