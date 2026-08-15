// SPDX-License-Identifier: Apache-2.0
//
// Whether a datagram is a frame, and whether the stream it belongs to is still
// the same stream.
//
// This is the layer that *decides* (roadmap/adapters-mocopi-vmc-ardy.md §6).
// Everything below it converts: bytes into chunks, chunks into the two packet
// kinds, joint ids into canonical bones. Nothing below has been allowed to
// answer a question about a *session* — `MotionPacket.h` refuses a bone and
// never a frame, and `SkeletonMap.h` maps a frame and says so when a bone's
// path did not arrive — and every one of those deferrals named this file.
//
// It decides four things and no others: whether a frame can be interpreted at
// all, whether it is complete, whether it orders against the frame before it,
// and whether the source restarted. It does not hold a missing bone forward,
// smooth, interpolate, or derive a velocity — those are `LiveCaptureSource`'s
// intake policies and `motionRuntime`'s arithmetic, and an adapter that grew a
// second copy would be a second motion runtime (§2).
//
// ## One datagram is one frame, and that is measured
//
// The sibling adapter's assembler exists because VMC makes no promise that one
// datagram is one frame: its header spends four screens on where a frame begins,
// because two real sender shapes disagree about which end of the frame the clock
// arrives at. **None of that applies here.** Every one of the 7964 measured frame
// datagrams was one `fram` carrying the whole rig — 1605 bytes, 27 bone records,
// one `fnum`, one `time` (MotionPacket.h) — and no session grew a second shape.
//
// So this class holds no open frame, accumulates nothing across datagrams, and
// has **no `Flush()`**. A reader arriving from `vrmAdapterVmc::VmcFrameAssembler`
// will look for one; its absence is the measurement, not an omission. There is
// never a frame in flight to lose at the end of a stream, and a caller that
// stops reading has lost nothing this class was holding.
//
// The name is kept anyway. It assembles a *session* out of datagrams rather
// than a frame out of messages, and the questions it answers are the ones the
// plan put under frame assembly.
//
// ## The rig is declared, not learned
//
// The sibling learns its rig from the stream — it has to, because VMC names
// bones with strings and a sender that solves no fingers must not be reported as
// sending an incomplete frame forty times a second. That is why it carries a
// staleness horizon: "this bone stopped arriving" and "this rig never had this
// bone" are the same observation there, told apart only by time.
//
// Here a skeleton packet *states* the rig, about every 3.5 s, and
// `MakeSkeletonMap` refuses one that disagrees with the measured column. So the
// two cases are distinct from the first frame: a bone in `SkeletonMap::present`
// that a frame did not carry is missing, full stop, and there is **no staleness
// horizon here and no `VRM_MOCOPI_STALE_JOINT` in the frozen set** to report one
// with. That absence is a consequence of a measurement rather than a gap.
//
// A frame that arrives before any skeleton packet cannot be interpreted at all:
// a `bnid` is a position in a rig nothing has declared yet (SkeletonMap.h). Such
// frames are refused, and reported **once per episode** rather than sixty times
// a second — the same discipline `VRM_MOCOPI_DEVICE_UNAVAILABLE` uses one layer
// down, and for the same reason. It is an ordinary way for a session to begin:
// a capture started mid-stream waits up to 3.5 s for the rig, and an operator
// who is told once that it is waiting has learned something, where one told
// sixty times a second has lost the log.
//
// ## Two clocks, a counter, and the three things they tell apart
//
// `time` is binary32 seconds from the start of the stream, `uttm` is binary64
// Unix seconds, and `fnum` counts the device's frames since its *application*
// started. The three disagree in ways that are the whole point:
//
//     stream restart   time -> 0.0     fnum keeps rising    (measured, x4)
//     app restart      time -> 0.0     fnum -> small        (corpus)
//     duplicate        time unchanged  fnum unchanged       (corpus)
//     transport loss   time skips      fnum skips by the same count
//
// `time` is the timestamp a frame carries, because it starts at exactly 0.0 in
// all five measured sessions and a clip wants an origin. `uttm` is carried
// beside it rather than instead of it: it is an absolute instant, and the
// **difference between the two is constant within a session**, which is a drift
// check MotionPacket.h notes a caller "did not have to build". This class
// builds it — `sessionEpoch` is fixed at the first accepted frame and
// `clockDrift` is each frame's deviation from it. Measured at under 2 µs over
// 33 s, so anything larger is a fact about the sender rather than about float.
//
// ### Why a restart needs both signals
//
// Neither clock alone separates the four rows above, and this is the finding
// this layer paid for rather than inherited:
//
// **The counter does not reset on a stream restart.** `fnum` began at 1928,
// 4495, 10231 and 21414 in four sessions whose `time` all began at 0.0
// (MotionPacket.h). So a rule reading only `fnum` sees a stream restart as an
// ordinary advance.
//
// **The clock's backwards jump is not always large.** A stream that restarts
// 0.1 s in goes backwards by 0.1 s, which is inside any jitter threshold worth
// having. The sibling's rule — backwards by more than a second is a restart,
// anything closer is a regression — would read that as a regression and refuse
// the new session's every frame until its clock passed the old one's. The corpus
// records exactly this case, and it is the one place where **the native path
// detects something the relay provably cannot**: a VMC sender's clock going back
// 0.1 s carries no second signal to disambiguate it, and `fnum` here does.
//
// So the rule is stated over both, and each row above is reached by one branch:
//
//     fnum unchanged                     -> TIMESTAMP_INVALID, refused
//     time backwards & fnum backwards    -> SOURCE_RESTARTED, accepted
//     time backwards by > the threshold  -> SOURCE_RESTARTED, accepted
//     time not advancing otherwise       -> TIMESTAMP_INVALID, refused
//     otherwise                          -> accepted
//
// A counter that goes backwards while the clock *advances* is not a restart: the
// clock is what orders frames, and the only thing that could produce that pair is
// a `fnum` wrap, which at 60 Hz is 2.3 years away.
//
// **A restart is reported, not repaired** — the sibling's rule and the same
// reasoning. The new session's clock is emitted verbatim and nothing is offset
// to keep timestamps rising. Downstream, `LiveCaptureSource` refuses a frame
// that goes backwards, so a caller that ignores `beginsNewSession` sees the new
// session rejected as stale — which is the correct outcome for a caller that has
// not decided what a restart means to it. Deciding it here would be inventing
// continuity out of a discontinuity.
//
// ### What a restart drops, and what that costs
//
// Everything the old stream established goes, **the rig included** — and the
// honest version of that argument is narrower than it first looks. Joint
// *identity* would in fact survive: `MakeSkeletonMap` refuses any rig that
// disagrees with the measured parent column, so every map this adapter will ever
// hold carries the same id → bone table, and reading a new session's ids against
// an old session's map could not put a rotation on the wrong bone.
//
// What would not survive is the **rest pose**. A skeleton packet is a body
// measurement, a stream restart is exactly when a recalibration happens, and the
// rest pose is what a retarget corrects against and what a frame's translations
// are compared against. Carrying the old one forward would attach a new body's
// motion to a previous body's proportions, silently, for as long as it took the
// next skeleton packet to arrive.
//
// The cost of dropping it is real and bounded: the device repeats the rest table
// about every 3.5 s, so a restarted session is dark for at most that long, and
// every frame in the gap is refused and counted as `framesRefusedNoRig` with one
// diagnostic to say so. Bounded, reported, and recoverable beats silent and
// plausible, which is the trade this whole path is ordered by.
//
// ## A gap is transport loss, and it is counted rather than refused
//
// An `fnum` gap is **transport loss and not a protocol event**, measured rather
// than assumed: two sessions lost 14 and 8 datagrams to Wi-Fi and every gap's
// `time` delta was exactly the missing count times 1/60 s, so the sender's clock
// never skipped (MotionPacket.h). Nothing here refuses one, and the frame that
// arrives after a gap is an ordinary frame.
//
// It is **counted and not diagnosed**, and that is a deliberate reading of the
// frozen set rather than an oversight. None of the nine codes describes it: it is
// not a malformed packet, not an incomplete frame, not an invalid timestamp, and
// not a restart. Adding a tenth is a contract change and not a commit
// (Diagnostics.h), and a code invented for a phenomenon the measurement calls
// ordinary would be the wrong way to earn one. `MocopiFrameStats::framesLost` is
// where an operator reads it, next to the frames that did arrive.
//
// ## Incomplete, and what this layer will not decide about it
//
// A frame that formed fewer bones than the session's rig carries is emitted with
// `VRM_MOCOPI_FRAME_INCOMPLETE` and its `missing` set filled. It is **not**
// refused, and the line is worth stating because this is the code the two layers
// below deferred to and a reader may expect it to be a rejection.
//
// Whether nineteen of twenty-two bones is a usable frame is a *policy* question,
// and `LiveCaptureSource` already owns the answer in `MissingBonePolicy` — hold
// the last value, or leave the joint unbound. An adapter that dropped the frame
// would be answering it here, one layer too low, and would take the choice away
// from the only component that knows what the session is for. So this layer
// reports precisely what was missing and passes the frame on, which is the
// sibling's arrangement and the same argument.
//
// The one genuine refusal is a frame that formed **no** canonical bone at all.
// That is not a sparse pose but an absence of one, and a caller that accepted it
// would be publishing the previous frame's values as this frame's.
//
// ## What is carried beside the pose, and why it is not in it
//
// **The hips translation.** `FrameMapping::hipsPosition` is the body's
// placement — absolute, metres, and the only translation the rig sends
// (SkeletonMap.h). It is carried on `MocopiFrame` and deliberately **not**
// written into `pose.root`. Whether the body's placement is root motion is the
// open record this release exists to close
// ([§5.2](../../../../../docs/roadmap/adapters-mocopi-vmc-ardy.md)), and an
// assembler that filled in a `RootMotion` would have answered it silently, in
// the layer with the least standing to. `pose.root` is therefore left absent,
// and a tool that decides the question composes it.
//
// **Tracking state.** `VRM_MOCOPI_TRACKING_LOST` is in the frozen set and is
// **not raised anywhere in this adapter**. The measured grammar carries no
// per-joint confidence and no state field, so there is nothing to decode into
// one; it lives in `sndf/ipad` or `fram/tmcd`, both unidentified, or this
// application version does not send it. `pose.confidence` is left absent for the
// same reason — an array of 1.0 would be a claim the device never made. The code
// stays frozen and unraised rather than being repurposed for something adjacent,
// which is what a frozen set is for.
//
// ## The codes this layer raises
//
// `VRM_MOCOPI_FRAME_INCOMPLETE` — a frame short of the rig's bones, and a frame
// that arrived with no rig declared yet. `VRM_MOCOPI_TIMESTAMP_INVALID` — a
// duplicate delivery, or a clock that does not advance and does not restart.
// `VRM_MOCOPI_SOURCE_RESTARTED` — the two restart shapes above. Three, bringing
// the set to nine of nine paid for by a real caller.
//
// Not `VRM_MOCOPI_UNSUPPORTED_JOINT`: that is a property of the rig, raised once
// when the map is built, and `MakeSkeletonMap`'s diagnostics pass through this
// class unchanged rather than being re-raised per frame.
#pragma once

#include "vrmAdapterMocopi/Diagnostics.h"
#include "vrmAdapterMocopi/MotionPacket.h"
#include "vrmAdapterMocopi/SkeletonMap.h"
#include "vrmAdapterMocopi/api.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/vec3f.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vrmAdapterMocopi
{

struct MocopiFrameConfig
{
    // A stream clock going backwards by more than this is a restart on its own,
    // without help from the counter. One second is far longer than any jitter a
    // sender's clock produces and far shorter than the gap the measured restarts
    // leave — `time` returns to exactly 0.0 from wherever the stream had reached,
    // tens of seconds in.
    //
    // It is deliberately **not** the only restart signal, which is the difference
    // from the sibling's identically-named field: a stream that restarts a tenth
    // of a second in goes backwards by less than any usable threshold, and the
    // counter is what catches it. See the header.
    //
    // Zero disables the clock half of the rule rather than making every backwards
    // frame a restart. The counter half still fires, so a session that restarts
    // its application is still detected; one that restarts only its stream is
    // then read as a regression and refused until its clock passes the old one's.
    // That is the safe direction — a stream wrongly held to one clock stalls
    // visibly, where one wrongly split into sessions discards its own history.
    double restartBackwardsSeconds = 1.0;
};

// One assembled frame: the canonical pose, plus everything a pose has nowhere
// to record.
struct MocopiFrame
{
    // Stamped with `time` — the sender's stream clock, seconds from the start of
    // the stream — and carrying exactly the bones this frame formed.
    //
    // `confidence` and `contacts` are absent because the grammar carries
    // neither, `expressions` because this protocol has none, `root` because the
    // hips translation is reported below instead of being read as root motion,
    // and `source` because the session's metadata belongs on the source once
    // rather than on sixty poses a second (see `GetSourceMetadata`).
    motion::HumanoidPose pose;

    // The device's frame counter, verbatim. Not an index into anything: it counts
    // the application's frames rather than the stream's, so the first frame of a
    // session is whatever the application had reached.
    std::uint32_t frameNumber = 0;

    // `uttm`, the sender's absolute clock, and this frame's deviation from the
    // constant offset the session began with. Measured under 2 µs over 33 s, so a
    // larger value is the sender's fact rather than float's. Zero on the first
    // frame of a session by construction — it is the frame that fixes the offset.
    double senderUnixSeconds = 0.0;
    double clockDrift = 0.0;

    // Whether the source restarted immediately before this frame, making it the
    // first of a new session. Reported, never repaired — see the header.
    bool beginsNewSession = false;

    // Datagrams the transport lost between the previous accepted frame and this
    // one: the `fnum` delta less one. Zero across a restart, where the counter's
    // difference means nothing.
    std::uint32_t lostFrames = 0;

    // Bones the session's rig declares that this frame did not form, because a
    // joint on their path did not arrive. Measured against `SkeletonMap::present`
    // rather than against a rig learned from the stream, and against the full
    // canonical humanoid least of all: a rig that ends at the wrists is not
    // sending an incomplete frame sixty times a second.
    std::bitset<motion::HumanBoneCount> missing;

    // The body's placement: the hips joint's own translation, absolute, in
    // metres. Absent when the hips record did not arrive. Deliberately not
    // composed into `pose.root` — see the header.
    std::optional<pxr::GfVec3f> hipsPosition;

    // Carried up from `FrameMapping` so a caller reading frames never has to
    // hold a second structure to learn what the mapping dropped. Records no bone
    // came from, and non-hips joints whose translation left its rest offset —
    // zero in every measured session, which is the point of counting it.
    std::size_t unusedJoints = 0;
    std::size_t droppedTranslations = 0;
};

// What an operator needs to judge a session without a debugger, in the same
// spirit as `LiveCaptureStats` — which counts what the *intake* did with these
// frames, and so cannot say anything about the ones that never became frames.
struct MocopiFrameStats
{
    std::uint64_t framesEmitted = 0;
    // Emitted, and short of at least one bone the rig declares.
    std::uint64_t framesIncomplete = 0;

    // Refused because the clock did not advance: a duplicate delivery, or a
    // backwards step that no restart explains.
    std::uint64_t framesRefusedOutOfOrder = 0;
    // Refused because no skeleton packet had arrived yet, so a `bnid` named a
    // position in a rig nothing had declared. Ordinary at the start of a session.
    std::uint64_t framesRefusedNoRig = 0;
    // Refused because the frame formed no canonical bone at all. An empty
    // mapping is an absence of a pose rather than a sparse one.
    std::uint64_t framesRefusedEmpty = 0;

    std::uint64_t sessionRestarts = 0;
    // Datagrams the transport lost, summed over the session's gaps. Not a
    // diagnostic anywhere: the measurement calls a gap ordinary, and the frozen
    // set has no code for it — see the header.
    std::uint64_t framesLost = 0;

    // Skeleton packets that built a rig, and those `MakeSkeletonMap` refused.
    // A refused one leaves the session's existing map standing: a rig is a
    // session-long thing worth keeping when a packet fails to replace it.
    std::uint64_t skeletonsAccepted = 0;
    std::uint64_t skeletonsRefused = 0;

    // Summed over emitted frames.
    std::uint64_t unusedJoints = 0;
    std::uint64_t droppedTranslations = 0;
};

// Feed it decoded packets in arrival order; take frames out.
//
// The unit is a decoded `MotionPacket` rather than a datagram, which is the line
// `SkeletonMap` already draws: this layer's questions are about fields, and a
// class that took bytes would be re-deciding what `DecodeMotionPacket` decided
// and could not be tested against a rig the decoder has never seen.
class VRMADAPTERMOCOPI_API MocopiFrameAssembler
{
public:
    explicit MocopiFrameAssembler(const MocopiFrameConfig& config = {});

    const MocopiFrameConfig& GetConfig() const noexcept { return _config; }

    // The endpoint or fixture name stamped on every diagnostic this assembler
    // raises, so a session replayed from a capture reports the same `source` a
    // live one would.
    void SetSource(std::string source);
    const std::string& GetSource() const noexcept { return _source; }

    // Accepts one decoded packet of either kind.
    //
    // A skeleton packet replaces the session's rig and never emits a frame. A
    // frame packet emits at most one frame, which is why this returns `bool`
    // where the sibling returns a count: there, one datagram can close several
    // frames; here one datagram *is* a frame, and a count would imply a
    // multiplicity the protocol does not have.
    //
    // `receiveTime` is the receiver's clock — the `d` record's time on a
    // replayed capture. It is used for nothing but diagnostics: unlike VMC,
    // every frame here carries the sender's own clock, so there is no receive
    // fallback to be reported and no `timestampFromSender` flag to carry.
    //
    // Frames are appended to `frames` and diagnostics to `diagnostics`; neither
    // is ever cleared, so a caller can accumulate a datagram's worth or a
    // session's.
    bool Push(const MotionPacket& packet, double receiveTime,
              std::vector<MocopiFrame>* frames,
              std::vector<Diagnostic>* diagnostics = nullptr);

    // Canonical provenance: protocol "mocopi", kind `LiveCapture`.
    //
    // `sourceId` and `provider` stay **empty**, and that is a decision rather
    // than a to-do. The only per-session identifier this protocol carries is
    // `sndf/ipad`, which is unidentified and treated as possibly
    // device-identifying — it stays out of committed fixtures and out of
    // anything this project publishes (MotionPacket.h), and provenance is
    // something published. The format magic does name a vendor, but it is a
    // constant of the protocol rather than a fact about the session, and
    // `protocol` already says which protocol this is.
    const motion::MotionSourceMetadata& GetSourceMetadata() const noexcept
    {
        return _metadata;
    }

    // The session's rig, or nullptr before a skeleton packet has declared one.
    // A caller needs it to interpret `MocopiFrame::missing`, and it carries the
    // device's own rest pose — which a relay cannot supply at all.
    const SkeletonMap* GetSkeletonMap() const noexcept
    {
        return _hasMap ? &_map : nullptr;
    }

    const MocopiFrameStats& GetStats() const noexcept { return _stats; }
    void ResetStats() noexcept { _stats = MocopiFrameStats(); }

    // Drops the rig, the clock history and the counter — everything a restart
    // invalidates. Stats survive, because they describe the session the caller
    // is judging rather than the stream's state.
    void Reset();

private:
    void _Report(std::vector<Diagnostic>* diagnostics, DiagnosticCode code,
                 std::string_view subject, std::optional<double> timestamp,
                 std::string detail);

    // Everything the old stream taught, dropped. The rig above all: a restarted
    // application may be sending a different one.
    void _ForgetSession();

    bool _PushSkeleton(const MotionSkeleton& skeleton,
                       std::vector<Diagnostic>* diagnostics);
    bool _PushFrame(const MotionFrame& frame, std::vector<MocopiFrame>* frames,
                    std::vector<Diagnostic>* diagnostics);

    MocopiFrameConfig _config;
    std::string _source;
    motion::MotionSourceMetadata _metadata;

    SkeletonMap _map;
    bool _hasMap = false;
    // Whether the "a frame arrived with no rig" refusal has been reported for
    // the current episode of it. Re-armed by the skeleton packet that ends the
    // episode, so a session that loses and regains its rig is told twice.
    bool _reportedNoRig = false;
    // A restart is detected on a frame this class then refuses for having no
    // rig, so `beginsNewSession` has to outlive that frame and land on the first
    // frame the new session actually emits.
    bool _pendingNewSession = false;

    std::uint64_t _packetSerial = 0;

    // The last accepted frame's clocks and counter. Absent before the session's
    // first accepted frame, and after a restart's reset.
    std::optional<double> _lastStreamSeconds;
    std::optional<std::uint32_t> _lastFrameNumber;
    // `uttm - time` at the first accepted frame of the session. Constant within
    // one, and the thing `MocopiFrame::clockDrift` is measured against.
    std::optional<double> _sessionEpoch;

    MocopiFrameStats _stats;
};

} // namespace vrmAdapterMocopi
