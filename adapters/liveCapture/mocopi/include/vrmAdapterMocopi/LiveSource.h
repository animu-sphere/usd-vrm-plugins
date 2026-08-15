// SPDX-License-Identifier: Apache-2.0
//
// Where the adapter ends and the motion runtime begins.
//
// This is the last layer of the native path and deliberately the thinnest.
// Every layer below it converts or decides something about the protocol; this
// one hands what they produced to `motion::LiveCaptureSource` and answers the
// runtime's `IMotionSource` questions by forwarding them:
//
//     datagram -> chunks -> packet -> frame -> [ MocopiLiveSource ]
//                                           -> LiveCaptureSource -> pose
//
// What it must *not* grow is the reason it is written down at all. Buffering,
// interpolation, smoothing, confidence gating, missing-bone resolution and
// root-motion intake all exist exactly once, in `motionRuntime`, and an adapter
// that grew a second copy would have forked the pipeline rather than extended
// it (roadmap/adapters-mocopi-vmc-ardy.md §2, §6). So this class keeps no
// history of its own: the frames of the push it is in the middle of, the
// provenance it has already told the intake about, and a latch for the one
// event a consumer cannot reconstruct from the poses it receives.
//
// ## The hand-off
//
// The assembler reports; the intake decides. A frame arrives here carrying
// exactly the bones it formed, with `missing` beside it as a *report*, and it is
// passed on exactly that way — `MissingBonePolicy` then holds the bone or leaves
// it unbound, per the caller's configuration. Nothing here fills a gap in. That
// division is the sibling's and the argument is the same: a second missing-bone
// policy inside an adapter would disagree with the configured one invisibly.
//
// ## Three places this differs from `vrmAdapterVmc::VmcLiveSource`
//
// A reader arriving from the sibling should be told what is missing before
// looking for it. Each of the three is a consequence of a measurement rather
// than a simplification.
//
// **There is no `Flush()`.** The sibling has one because a VMC frame is
// assembled out of messages and one can be open when a stream ends. Here one
// datagram is one frame — 7964 measured frame datagrams, one shape, the whole
// rig in each (MotionPacket.h) — so `MocopiFrameAssembler` holds no open frame
// and has no `Flush()` for this class to forward. A replayed capture loses
// nothing by ending.
//
// **Provenance is fixed at construction and never changes.** The sibling
// compares the assembler's metadata against its own on every frame, because
// `/VMC/Ext/VRM` can arrive mid-session and teach it the model's title. This
// protocol carries no such handshake: the metadata is the protocol name and the
// kind, `sourceId` and `provider` stay empty for the reasons
// `MocopiFrameAssembler::GetSourceMetadata` gives, and there is nothing for a
// per-frame comparison to find. So the intake is told once. That absence is
// worth naming rather than leaving as a missing line: it is not that the
// handshake is unimplemented, it is that this protocol has none.
//
// **A restart costs the rig as well as the history**, and this is the one that
// changes what a consumer sees. Both adapters drop the intake's buffer on a
// restart. Only this one also loses the *rig*: a skeleton packet is a body
// measurement and a restart is exactly when a recalibration happens, so
// `MocopiFrameAssembler` forgets it rather than attaching a new body's motion to
// a previous body's proportions (FrameAssembler.h). The device repeats the table
// about every 3.5 s, so the new session produces no frame at all until it does.
//
// ## What that gap is, and why nothing here fills it
//
// Between the restart and the new session's first frame the intake still holds
// the old session's poses, and this class goes on serving them. That is
// deliberate, and the alternative was considered rather than overlooked: the
// buffer could be dropped the instant the assembler's restart count moved,
// which would make `Sample` report `Unavailable` for the gap instead of `Held`.
//
// It is not done because held-versus-unavailable during a dropout is
// `PoseBuffer`'s question and it already has an answer. A consumer sees the last
// pose held, `peakLagSeconds` growing, and `maxExtrapolationSeconds` bounding
// what is invented — which is what every other dropout on this path looks like,
// and a restart is not entitled to a third state that only this adapter can
// produce. The gap is reported where a gap is reported: the assembler counts
// every refused frame in `framesRefusedNoRig` and says once per episode why.
//
// ## The one decision this layer takes
//
// A restart is where the two halves would otherwise deadlock. The assembler
// reports it and refuses to repair it — the new session's clock comes out
// verbatim, and on this protocol that is not a perturbation but a return to
// exactly 0.0, measured in all five sessions (MotionPacket.h). To
// `LiveCaptureSource` that is a frame arriving tens of seconds behind the newest
// it holds, which it refuses, forever. That is the correct outcome for a caller
// that has not decided what a restart means to it, and this class is where a
// caller decides:
//
//     Reset    drop the intake's history and admit the new session
//     Refuse   hand the frame on unchanged and let the intake refuse it
//
// `Reset` is the default because the alternative is a stream that dies the first
// time an operator restarts their source, and because it is what the layer below
// already does with everything it learned. `Refuse` is not a degenerate setting:
// a consumer that would rather see a stall than lose its history can have one,
// visibly.
//
// The third option — offsetting the new session's timestamps to keep the stream
// continuous — is not offered anywhere in this adapter. It manufactures
// continuity out of a discontinuity, which is the class of invention §2 forbids.
//
// A restart also invalidates something `LiveCaptureSource::Reset` deliberately
// keeps: the clock offset, measured against a clock that no longer exists. Only
// the consumer knows the evaluation time to re-align to, so the restart is
// latched here and handed back rather than repaired:
//
//     if (source.ConsumeSessionRestart()) {
//         source.GetIntake().AlignClock(now);
//     }
//
// **The latch fires when the new session's first pose exists, not when the
// restart was detected**, and on this protocol those are up to 3.5 s apart where
// on the sibling they are the same instant. That ordering is required rather
// than incidental: `AlignClock` pins the *newest buffered frame* to the
// evaluation time, so aligning at detection would pin the dead session's head
// and aligning on an emptied buffer does nothing at all. `MocopiFrame` carries
// `beginsNewSession` on the frame the new session actually emits — the assembler
// holds the flag across every frame it refuses for having no rig — so acting on
// the frame is both the correct instant and the only one this class can see.
//
// A consumer that ignores the latch sees a source that has fallen the length of
// the old session behind, which is a visible fault rather than a silent one.
//
// ## What a pose cannot carry, and where it is still readable
//
// A frame knows things a `HumanoidPose` has nowhere to put: the hips
// translation, which bones the rig declared and this frame did not form, whether
// it began a session, how many datagrams the transport lost before it, and how
// far the sender's two clocks have drifted apart. The statistics structs carry
// the aggregates and the per-frame detail would otherwise stop here — which
// would close the evidence path v0.7.0 needs, since whether the body's placement
// is root motion is the record this release exists to write
// (roadmap §5.2, FrameAssembler.h). So `GetFramesFromLastPush()` opens a window
// on exactly what was just delivered, and a recording tool does not have to
// drive the assembler separately to look through it.
//
// **The hips translation reaches no `RootMotion` here either**, and this class
// is the last one that could have quietly composed it. It does not, for the
// reason the assembler does not: the question is open, this layer is in no
// better position to answer it than the one below, and a bridge that filled in a
// `RootMotion` would have closed the release's own record by writing code. One
// consequence is worth stating so it is not read as a defect — `RootMotionIntake`
// is inert for this adapter, whatever it is configured to, because no pose that
// passes through here carries root motion for it to pass through, ignore, or
// derive a velocity from.
//
// ## One thread
//
// Nothing here is thread-safe, and neither is what it wraps: `PoseBuffer` holds
// a deque, `LiveCaptureSource::Sample` writes its own statistics as it answers,
// and `motionRuntime` contains no mutex or atomic at all. So a push and a sample
// may not run concurrently, and neither may two samples.
//
// That is not solved here, and the shape of the solution is already settled one
// layer down: the hand-off between a receive thread and a consumer's is a
// bounded queue of **raw datagrams**, before any decoder, so this class and all
// of `motionRuntime` stay on one thread exactly as their tests are written
// (motion policy §11.4, and `vrmAdapterVmc::DatagramQueue` for the sibling's).
// What this class must *not* do meanwhile is grow a private lock: a mutex here
// would make it safe against itself and leave every `GetIntake()` caller racing
// on the same buffer, which is a worse fault for looking like a fixed one.
//
// ## The datagram's lifetime stops here
//
// `MotionPacket::provenance.formatType` and every `UnreadChunk` point into the
// caller's bytes — the hazard `MotionPacket.h` names, and the reason its rvalue
// overload is deleted. Pushing a datagram through this class ends inside the
// call: a joint has become a `motion::HumanBone`, a rotation has been reordered
// into a `GfQuatf`, and a diagnostic owns its subject and its detail. So a
// receiver may hand this API the buffer it is about to overwrite, and that is
// the shape a receiver should have.
#pragma once

#include "vrmAdapterMocopi/Diagnostics.h"
#include "vrmAdapterMocopi/FrameAssembler.h"
#include "vrmAdapterMocopi/MotionPacket.h"
#include "vrmAdapterMocopi/api.h"

#include "motionRuntime/LiveCaptureSource.h"
#include "motionRuntime/MotionSource.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vrmAdapterMocopi
{

// What to do with the first frame of a new session. As above: the option this
// enumeration does not offer is the one that would splice the two.
enum class SessionRestartPolicy : std::uint8_t
{
    // Drop the intake's buffered history and admit the new session's first
    // frame. The stream continues; the history recorded before the restart is
    // gone, because it describes a stream that has ended.
    Reset,
    // Hand the frame on unchanged. The intake refuses it as a frame that does
    // not advance, and every frame after it too, so the session visibly stops.
    Refuse,
};

struct MocopiLiveSourceConfig
{
    MocopiFrameConfig frame;
    motion::LiveCaptureConfig intake;
    SessionRestartPolicy restart = SessionRestartPolicy::Reset;
};

// What this layer alone can count. Everything the assembler refused is in
// `MocopiFrameStats` and everything the intake refused is in `LiveCaptureStats`;
// repeating either here would give an operator two numbers that can disagree.
struct MocopiLiveSourceStats
{
    // Datagrams that decoded into a packet, and datagrams the decoder refused
    // whole. The second is the one number nothing else holds: a refused datagram
    // never reaches the assembler, so a session drowning in malformed traffic is
    // invisible in every other tally. A datagram whose *bone records* were
    // refused still counts as decoded — `MotionPacket::refusedBones` says so,
    // and saying it twice differently would be worse than not saying it here.
    std::uint64_t datagramsDecoded = 0;
    std::uint64_t datagramsRefused = 0;

    // Frames handed to the intake, and what it did with them. Under the default
    // restart policy `framesRefused` is expected to stay 0: the assembler emits
    // strictly advancing frames within a session, which is exactly the ordering
    // the intake requires.
    std::uint64_t framesDelivered = 0;
    std::uint64_t framesAdmitted = 0;
    std::uint64_t framesRefused = 0;

    // Times the restart policy reset the intake — zero under `Refuse`, whatever
    // the source did. How many restarts were *seen* is the assembler's
    // `sessionRestarts`, and on this protocol the two can differ for a third
    // reason the sibling does not have: a restart whose new session never
    // reaches a skeleton packet is seen and never acted on, because no frame of
    // it is ever emitted.
    std::uint64_t sessionsReset = 0;
};

// Owns the whole decode path and the intake it feeds, so a consumer holds one
// object and reads poses off it through `IMotionSource` like any clip.
//
// The configuration is not held here after construction, and that is a decision:
// the assembler and the intake each own theirs, `LiveCaptureSource::SetConfig`
// can change one of them mid-session, and a copy kept beside them would go stale
// the moment it did. Read them back through `GetAssembler().GetConfig()` and
// `GetIntake().GetConfig()`. The restart policy is the one setting that belongs
// to neither half, so it is the one this class keeps.
class VRMADAPTERMOCOPI_API MocopiLiveSource final : public motion::IMotionSource
{
public:
    explicit MocopiLiveSource(const MocopiLiveSourceConfig& config = {});

    SessionRestartPolicy GetRestartPolicy() const noexcept { return _restart; }
    void SetRestartPolicy(SessionRestartPolicy restart) noexcept
    {
        _restart = restart;
    }

    // The endpoint or fixture name every diagnostic this path raises is stamped
    // with. It is not provenance: this protocol carries no per-session
    // identifier that may be published, and an address is not one either
    // (`MocopiFrameAssembler::GetSourceMetadata`).
    void SetSource(std::string source);

    // Decodes one received datagram and delivers the frame it was, if it was
    // one.
    //
    // `receiveTime` is the receiver's clock. It reaches nothing but diagnostics:
    // every frame this protocol sends carries the sender's own clock, so there
    // is no receive fallback here and no `timestampFromSender` flag to carry —
    // which is the sibling's arrangement inverted, and a measurement rather than
    // an omission.
    //
    // Returns how many poses reached the intake's buffer — which is the number a
    // consumer cares about, and is *not* whether the assembler emitted a frame:
    // a frame the restart policy chose to refuse was emitted and not admitted.
    // `GetStats()` is where the two are told apart.
    //
    // Every diagnostic this call appends — the decoder's, the map's, and the
    // assembler's alike — is stamped with the session's source and with this
    // datagram's serial, counted from the first datagram this object was ever
    // given and including the ones no packet came out of. That numbering is this
    // layer's because it is the only one that knows what a datagram is: the
    // assembler is handed packets, so its own serial skips whatever the decoder
    // refused, and a list mixing the two would number the packet-level failures
    // differently from everything else. `PushPacket` leaves the assembler's
    // numbering alone, since a caller decoding for itself has no other.
    //
    // `bytes` need not outlive the call (see the header).
    std::size_t PushDatagram(const std::uint8_t* bytes, std::size_t size,
                             double receiveTime,
                             std::vector<Diagnostic>* diagnostics = nullptr);

    std::size_t PushDatagram(const std::vector<std::uint8_t>& datagram,
                             double receiveTime,
                             std::vector<Diagnostic>* diagnostics = nullptr)
    {
        return PushDatagram(datagram.data(), datagram.size(), receiveTime,
                            diagnostics);
    }

    // The same, for a caller that has already decoded — the corpus tests, and a
    // receiver that wants the decoder's diagnostics separated from the rest.
    std::size_t PushPacket(const MotionPacket& packet, double receiveTime,
                           std::vector<Diagnostic>* diagnostics = nullptr);

    // There is no `Flush()`, and its absence is a measurement — see the header.

    // Whether a session restart has reached a frame since this was last asked,
    // and clears the latch. True at most once per restart, whatever the policy
    // did with the frame — a consumer that chose `Refuse` still needs to know why
    // its source stopped advancing.
    bool ConsumeSessionRestart() noexcept;

    // IMotionSource, entirely by delegation. A pose sampled from here is a pose
    // the runtime produced; this class contributes no arithmetic to it.
    motion::PoseSampleResult Sample(double evaluationTime) override;
    motion::MotionSourceMetadata GetSourceMetadata() const override;
    bool GetTimeRange(double* startTime, double* endTime) const override;

    motion::LiveCaptureSource& GetIntake() noexcept { return _intake; }
    const motion::LiveCaptureSource& GetIntake() const noexcept
    {
        return _intake;
    }

    const MocopiFrameAssembler& GetAssembler() const noexcept
    {
        return _assembler;
    }

    // The session's rig, or nullptr before a skeleton packet has declared one —
    // and again after a restart until the next one arrives. A caller needs it to
    // interpret `MocopiFrame::missing`, and it carries the device's own rest
    // pose, which a relay cannot supply at all.
    const SkeletonMap* GetSkeletonMap() const noexcept
    {
        return _assembler.GetSkeletonMap();
    }

    // The frames the last push produced, in order, including any the restart
    // policy or the intake then refused. Valid until the next push, which reuses
    // the vector rather than allocating one per datagram. At most one element on
    // this protocol; it is a vector because the assembler appends to one, and a
    // shape that could only ever hold a single frame would have to be unwrapped
    // by every caller that also reads the sibling's.
    //
    // This is the window onto what a `HumanoidPose` cannot carry — see the
    // header. A caller that only wants poses never touches it; a recording tool
    // gathering the root/hips evidence v0.7.0 owes reads it after every push.
    const std::vector<MocopiFrame>& GetFramesFromLastPush() const noexcept
    {
        return _frames;
    }

    const MocopiLiveSourceStats& GetStats() const noexcept { return _stats; }

    // This layer's tally only. The assembler's and the intake's are reset
    // through their own objects, so a caller that wants one of them says so.
    void ResetStats() noexcept { _stats = MocopiLiveSourceStats(); }

    // A new session on the same object: both halves forget the stream, the rig
    // included. Stats survive, like everywhere else in this adapter — and so does
    // the intake's clock offset, which `LiveCaptureSource::Reset` keeps
    // deliberately. A caller replaying a second capture into the same object
    // re-aligns that offset exactly as it would after a restart; inheriting the
    // first capture's is the same fault the latch exists to make visible.
    void Reset();

private:
    // Hands `_frames` to the intake, applying the restart policy on the way.
    // Returns how many were admitted.
    std::size_t _Deliver();

    // Names the session and the datagram on every diagnostic a `PushDatagram`
    // appended. The decoder knows neither: it is reading bytes, and a caller
    // with one list must not have to tell which layer produced a line in order
    // to know what it is about.
    void _StampDatagram(std::vector<Diagnostic>* diagnostics,
                        std::size_t from) const;

    MocopiFrameAssembler _assembler;
    motion::LiveCaptureSource _intake;
    SessionRestartPolicy _restart;

    // Reused across pushes rather than allocated per datagram: at 60 Hz this is
    // called sixty times a second. It outlives the call only as the evidence
    // window `GetFramesFromLastPush()` opens.
    std::vector<MocopiFrame> _frames;

    // Received datagrams, refused ones included, so a diagnostic can name the
    // delivery it came from rather than the packet the assembler was handed.
    std::uint64_t _datagramSerial = 0;

    bool _restartPending = false;
    MocopiLiveSourceStats _stats;
};

} // namespace vrmAdapterMocopi
