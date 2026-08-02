// SPDX-License-Identifier: Apache-2.0
//
// Where the adapter ends and the motion runtime begins.
//
// This is the last layer of the VMC path and deliberately the thinnest. Every
// layer below it converts or decides something about the protocol; this one
// hands what they produced to `motion::LiveCaptureSource` and answers the
// runtime's `IMotionSource` questions by forwarding them:
//
//     datagram -> OSC -> VMC messages -> frame -> [ VmcLiveSource ]
//                                              -> LiveCaptureSource -> pose
//
// What it must *not* grow is the reason it is written down at all. Buffering,
// interpolation, smoothing, confidence gating, missing-bone resolution and
// root-motion intake all exist exactly once, in `motionRuntime`, and an adapter
// that grew a second copy would have forked the pipeline rather than extended
// it (roadmap/adapters-mocopi-vmc-ardy.md §2). So this class keeps no history
// of its own: one scratch vector the receive loop reuses, the provenance it has
// already told the intake about, and a latch for the one event a consumer
// cannot reconstruct from the poses it receives.
//
// ## The hand-off
//
// The assembler reports a gap; the intake decides what a gap means. A frame
// arrives here carrying exactly the bones the sender sent, with `missing` and
// `stale` beside it as a *report*, and it is passed on exactly that way —
// `MissingBonePolicy` then holds the bone or leaves it unbound, per the
// caller's configuration. Nothing here fills a gap in, and nothing here unbinds
// a bone the assembler called stale either: `VRM_VMC_STALE_JOINT` is what
// reaches an operator, because a second missing-bone policy inside the adapter
// would disagree with the configured one invisibly.
//
// ## The one decision this layer takes
//
// A sender restart is where the two halves would otherwise deadlock. The
// assembler reports a restart and refuses to repair it: the new session's clock
// comes out verbatim, which for `LiveCaptureSource` is a frame arriving behind
// the newest it holds — and it refuses those, forever. That is the correct
// outcome for a caller that has not decided what a restart means to it
// (FrameAssembler.h), and this class is where a caller decides:
//
//     Reset    drop the intake's history and admit the new session
//     Refuse   hand the frame on unchanged and let the intake refuse it
//
// `Reset` is the default because the alternative is a stream that dies the
// first time an operator restarts their sender application, and because it is
// what the layer below already does with everything it learned from the old
// session. `Refuse` is not a degenerate setting: a consumer that would rather
// see a stall than lose four seconds of history can have one, visibly.
//
// The third option — offsetting the new session's timestamps to keep the stream
// continuous — is not offered anywhere in this adapter. It manufactures
// continuity out of a discontinuity, which is the class of invention §2 forbids.
//
// A restart also invalidates something `LiveCaptureSource::Reset` deliberately
// keeps: the clock offset, which was measured against a clock that no longer
// exists. Only the consumer knows the evaluation time to re-align to, so the
// restart is latched here and handed back rather than repaired:
//
//     if (source.ConsumeSessionRestart()) {
//         source.GetIntake().AlignClock(now);
//     }
//
// A consumer that ignores the latch sees a source that has fallen tens of
// seconds behind, which is a visible fault rather than a silent one.
//
// One reading is inherited rather than chosen. A sender that stops emitting
// `/VMC/Ext/T` mid-session drops onto the receive clock, whose origin is not the
// sender's; the assembler compares the two as one number and so reads the switch
// as a restart, and this class acts on that reading. Nothing here tries to tell
// the two apart, because no capture records a sender doing it — and a rule
// written against a phenomenon nobody has observed is a guess with a test
// beside it.
//
// ## Provenance arrives when the sender sends it
//
// `/VMC/Ext/VRM` may arrive at any point in a session, and the intake stamps
// each pose with the metadata it holds *at the time that pose is pushed*. So a
// pose buffered before the handshake carries the bare `vmc` provenance and one
// after it carries the model's title. Re-stamping the buffered ones would claim
// they were recorded knowing something the session did not know yet, which is
// exactly the sort of small lie a provenance field exists to prevent.
//
// ## The datagram's lifetime stops here
//
// `VmcMessage::name` and every OSC `text` point into the caller's bytes, which
// is a hazard `VmcMessage.h` describes and no overload can refuse: a receive
// loop with one reusable buffer invalidates them on its next `recv`. Pushing a
// datagram through this class ends inside the call — a bone has become a
// `motion::HumanBone`, a title has been copied into a string, and a diagnostic
// owns its subject. So a receiver may hand this API a buffer it is about to
// overwrite, and that is the shape a receiver should have.
#pragma once

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/FrameAssembler.h"
#include "vrmAdapterVmc/VmcMessage.h"
#include "vrmAdapterVmc/api.h"

#include "motionCore/Humanoid.h"
#include "motionRuntime/LiveCaptureSource.h"
#include "motionRuntime/MotionSource.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vrmAdapterVmc
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

struct VmcLiveSourceConfig
{
    VmcFrameConfig frame;
    motion::LiveCaptureConfig intake;
    SessionRestartPolicy restart = SessionRestartPolicy::Reset;
};

// What this layer alone can count. Everything the assembler refused is in
// `VmcFrameStats` and everything the intake refused is in `LiveCaptureStats`;
// repeating either here would give an operator two numbers that can disagree.
struct VmcLiveSourceStats
{
    // Datagrams that decoded as OSC, and datagrams refused whole by that layer.
    // The second is the one number nothing else holds: a refused datagram never
    // reaches the assembler, so a session drowning in malformed traffic is
    // invisible in every other tally. A datagram whose *messages* were refused
    // one layer up still counts as decoded — the VMC layer's tallies say so, and
    // saying it twice differently would be worse than not saying it here.
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
    // the sender did. How many restarts were *seen* is the assembler's
    // `sessionRestarts`.
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
class VRMADAPTERVMC_API VmcLiveSource final : public motion::IMotionSource
{
public:
    explicit VmcLiveSource(const VmcLiveSourceConfig& config = {});

    SessionRestartPolicy GetRestartPolicy() const noexcept { return _restart; }
    void SetRestartPolicy(SessionRestartPolicy restart) noexcept
    {
        _restart = restart;
    }

    // The endpoint or fixture name every diagnostic this path raises is stamped
    // with. It is not provenance: `MotionSourceMetadata::sourceId` is what the
    // sender said its model was, and an address is not that.
    void SetSource(std::string source);

    // Decodes one received datagram and delivers whatever frames it completed.
    // `receiveTime` is the receiver's clock, used only to stamp a frame whose
    // sender sent no clock of its own.
    //
    // Returns how many poses reached the intake's buffer — which is the number a
    // consumer cares about, and is *not* the assembler's count of frames
    // emitted: a frame the restart policy chose to refuse was emitted and not
    // admitted. `GetStats()` is where the two are told apart.
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
    // receiver that wants the OSC diagnostics separated from the rest.
    std::size_t PushPacket(const VmcPacket& packet, double receiveTime,
                           std::vector<Diagnostic>* diagnostics = nullptr);

    // Delivers the frame still open at the end of a stream. A live session never
    // reaches this; a replayed capture that forgets it loses its last frame.
    std::size_t Flush(std::vector<Diagnostic>* diagnostics = nullptr);

    // Whether a session restart has happened since this was last asked, and
    // clears the latch. True at most once per restart, whatever the policy did
    // with the frame — a consumer that chose `Refuse` still needs to know why
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

    const VmcFrameAssembler& GetAssembler() const noexcept
    {
        return _assembler;
    }

    const VmcLiveSourceStats& GetStats() const noexcept { return _stats; }

    // This layer's tally only. The assembler's and the intake's are reset
    // through their own objects, so a caller that wants one of them says so.
    void ResetStats() noexcept { _stats = VmcLiveSourceStats(); }

    // A new session on the same object: both halves forget the stream, and the
    // provenance goes with it, because after this nothing is known about the
    // sender again. Stats survive, like everywhere else in this adapter.
    void Reset();

private:
    // Hands `_frames` to the intake, applying the restart policy on the way.
    // Returns how many were admitted.
    std::size_t _Deliver();

    // Names the session on the diagnostics the decode layers raised before the
    // assembler saw them. Neither of those layers knows which sender it is
    // reading, and a caller with one list must not have to tell which layer
    // produced a line in order to know what it is about.
    void _StampSource(std::vector<Diagnostic>* diagnostics,
                      std::size_t from) const;

    VmcFrameAssembler _assembler;
    motion::LiveCaptureSource _intake;
    SessionRestartPolicy _restart;

    // Reused across pushes rather than allocated per datagram: at 30 Hz with a
    // per-message sender this is called a hundred times a second, and the frames
    // are consumed before the call returns.
    std::vector<VmcFrame> _frames;

    // What the intake was last told, so the handshake is forwarded once rather
    // than on every frame that follows it.
    motion::MotionSourceMetadata _metadata;

    bool _restartPending = false;
    VmcLiveSourceStats _stats;
};

} // namespace vrmAdapterVmc
