// SPDX-License-Identifier: Apache-2.0
//
// Where a frame begins, and what it means that a bone is not in it.
//
// VMC makes no promise that one datagram is one frame, so this layer exists to
// answer a question the protocol leaves open
// (roadmap/adapters-mocopi-vmc-ardy.md §5.2). Everything below it converts:
// bytes into OSC, OSC into VMC messages, VMC messages into canonical values.
// This is the first layer that *decides* — which samples belong together, which
// frame is too old to accept, and when a stream has become a different stream.
//
// It decides those and nothing else. It does not hold a missing bone forward,
// smooth anything, interpolate anything, or derive a velocity: those are
// `LiveCaptureSource`'s intake policies and `motionRuntime`'s arithmetic, and an
// adapter that grew a second copy would be a second motion runtime (§2). What
// comes out is a `motion::HumanoidPose` carrying exactly the bones the frame
// carried, plus the report of what was missing — so the policy layer can apply
// its own answer instead of inheriting one baked in here.
//
// ## Frame boundaries
//
// The corpus already contains two sender shapes that disagree about where the
// clock goes, which is why no single rule about `/VMC/Ext/T` can work:
//
//     bundled     | T(12.500) root Hips … UpperChest | T(12.533) root Hips …
//     unbundled   | root Hips … RightToes T(20.000)  | root Hips … T(20.033)
//
// In the first the clock *opens* the frame; in the second it *closes* it. A
// decoder that picked either convention would produce one frame per two on the
// other sender, off by half a frame, and every rotation in it would still be
// individually correct — which is the kind of defect a per-message test cannot
// see. So the boundary is drawn by two rules that hold for both shapes:
//
// **A second clock ends the frame.** The first `/VMC/Ext/T` a frame sees is its
// timestamp, whether it arrived before the bones or after them. A second one
// cannot describe the same frame, so it closes the open frame and opens the next
// carrying that value.
//
// **A repeat ends the frame — unless it arrived in the same datagram.** A bone
// or a root the open frame already carries means the sender has moved on. That
// is what closes an unbundled frame, whose clock arrives mid-stream and stays.
// Inside *one* datagram the same repeat is read as `VRM_VMC_DUPLICATE_BONE`
// instead: a datagram is one indivisible delivery, and a sender that emitted a
// bone twice in a single bundle is far likelier to have repeated itself than to
// have packed two frames into one send. The clock rule above does not make that
// exception, because the two cases are not symmetric — a repeated bone is
// genuinely ambiguous, and a repeated clock is not.
//
// A stream has no end marker, so `Flush()` closes whatever is still open. A
// caller that forgets it loses the last frame of every capture.
//
// ### What the second rule assumes, and when it is wrong
//
// The repeat rule holds exactly while a sender emits the same bones every frame.
// A bone the open frame does not already carry is added to it, whether or not
// the frame has met its clock — because "content after the clock belongs to the
// next frame" is true of the unbundled sender and false of the bundled one,
// which is the same asymmetry that made the clock's position unusable in the
// first place. So an unbundled sender whose *first* message of a new frame is a
// bone the previous frame lacked hands that bone to the previous frame:
//
//     sent      A B C T | D A B C T
//     assembled { A B C D }   { A B C }
//
// This is a limit of what a boundary can be inferred from, not a bug with a
// fix: the rule that would repair it breaks the other sender. It is narrow in
// practice — a Unity sender walks `HumanBodyBones` in a fixed order, so a bone
// lost mid-chain and recovered still sorts behind one that repeats first, and
// only a *leading* bone (`Hips`) going away and coming back reorders anything.
// It is written down because §9.2 puts `tracking-loss-and-recovery` in the
// corpus Milestone B has to record, and that is the capture in which this would
// first be seen. What settles it is a sender that varies its set — until one
// exists, inventing a third rule for it would be guessing.
//
// ## Clocks
//
// The sender's clock and the receiver's share no origin (tests/corpus/README.md),
// and this layer never converts one into the other. A frame is stamped with the
// sender's `/VMC/Ext/T` when the sender sent one, and with the receive time of
// its first message when it did not — `timestampFromSender` says which. That
// fallback is the one value here the sender did not supply, and it is preferred
// to refusing the frame because a sender that emits no clock still emits usable
// motion, and arrival order is a real monotone clock. It is just not the
// sender's, so a session that switches between the two mid-stream is reported
// rather than spliced.
//
// ## What a backwards clock means
//
// Three different things, told apart by one comparison against the last
// *accepted* frame — and the sender-restart capture records all three:
//
//     equal or slightly earlier  ->  VRM_VMC_TIMESTAMP_REGRESSION, refused
//     earlier by more than the   ->  VRM_VMC_SOURCE_RESTARTED, accepted as the
//       restart threshold             first frame of a new session
//     later                      ->  accepted
//
// A duplicate delivery lands in the first: the same bytes arriving twice produce
// the same timestamp twice, and a stream that emitted the same instant twice has
// not moved. Refusing it is what keeps a duplicated datagram from becoming a
// duplicated pose.
//
// A restart is *reported*, not repaired. The assembler resets what it learned
// from the old session and emits the sender's new clock verbatim; it does not
// offset the stream to keep timestamps rising. Downstream, `LiveCaptureSource`
// refuses a frame that goes backwards, so a caller that ignores
// `beginsNewSession` will see the new session rejected as stale — which is the
// correct outcome for a caller that has not decided what a restart means to it.
// Deciding that here would be inventing continuity out of a discontinuity, which
// is exactly the class of invention §2 forbids an adapter.
//
// ## Missing, and missing for too long
//
// A bone the session has seen and this frame did not carry is *missing*: the
// frame reports it and is still emitted, because whether a gap becomes a held
// pose or an unbound joint is `MissingBonePolicy`'s answer and not this layer's.
// A bone missing for longer than the staleness horizon is *stale*, which is a
// stronger claim — the value a downstream holder is still showing is no longer
// being reported as current — and it is raised once, when the horizon is
// crossed, rather than on every frame of a 30 Hz stream that would otherwise
// bury the session in it.
//
// Both are measured against what the session has actually observed rather than
// against the full 55-bone humanoid. A sender that solves no fingers is not
// sending an incomplete frame forty times a second; it is sending a complete
// frame from a rig with no fingers, and the difference is the whole reason
// `HumanoidPose::validRotations` exists.
//
// ## What this layer drops
//
// Blend-shape values and their apply are seen and deliberately not carried:
// `motionCore` has no expression sample yet, and inventing a place to put one is
// a contract change rather than an adapter's decision (§11, Motion Phase G).
// They are counted, so a session that is mostly expression traffic says so.
#pragma once

#include "vrmAdapterVmc/Diagnostics.h"
#include "vrmAdapterVmc/SkeletonMap.h"
#include "vrmAdapterVmc/VmcMessage.h"
#include "vrmAdapterVmc/api.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/vec3f.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vrmAdapterVmc
{

struct VmcFrameConfig
{
    // How far a bone may go unreported before the session stops calling its last
    // value current. Measured in frame timestamps rather than arrival times, so
    // it means the same thing on a replayed capture as on a live socket. Zero
    // disables the check.
    double stalenessSeconds = 0.5;

    // A frame earlier than the last accepted one by more than this is a restart;
    // anything closer is a regression. One second is far longer than any jitter
    // a sender's clock produces and far shorter than the gap a restart leaves,
    // so the two cases do not overlap in practice — and a stream where they do
    // is one whose clock cannot be trusted for either reading.
    //
    // Zero disables restart detection rather than making every backwards frame a
    // restart, which is the reading the sentence above would otherwise invite:
    // every frame that does not advance is then a regression, and a session that
    // restarts never recovers. That is the safe direction — a stream wrongly
    // held to one clock stalls visibly, where one wrongly split into sessions
    // discards its own history.
    double restartBackwardsSeconds = 1.0;
};

// One assembled frame: the canonical pose, plus the things a pose has nowhere to
// record.
struct VmcFrame
{
    // Stamped, rooted, and carrying exactly the bones this frame carried. Its
    // `confidence` and `contacts` are absent because VMC reports neither, and
    // `source` is absent because the session's metadata belongs on the source
    // once rather than on thirty poses a second (see `GetSourceMetadata`).
    motion::HumanoidPose pose;

    // Whether the sender's clock restarted immediately before this frame, making
    // it the first of a new session. See the header: the assembler reports this
    // and does not repair it.
    bool beginsNewSession = false;

    // False when the frame was stamped with the receive clock because the sender
    // sent no `/VMC/Ext/T` for it.
    bool timestampFromSender = false;

    // Bones the session has observed that this frame did not carry, and the
    // subset of those whose last report is now older than the staleness horizon.
    // `stale` is always a subset of `missing`.
    std::bitset<motion::HumanBoneCount> missing;
    std::bitset<motion::HumanBoneCount> stale;

    // The sender's hips offset, converted, and deliberately not composed with
    // `/VMC/Ext/Root/Pos`. A `HumanoidPose` carries rotations and one
    // `RootMotion` (MOTION_CONTRACT.md), so fifty-four of a frame's fifty-five
    // local positions are the sender's rig geometry and have nowhere canonical
    // to go; this one could be body translation instead, and whether it is
    // depends on what a real sender puts in each field. Until that is measured
    // it is reachable and unread — see SkeletonMap.h, which converted it rather
    // than taking the decision away from here.
    std::optional<pxr::GfVec3f> hipsOffset;

    // How many of this frame's samples were refused as a repeat inside one
    // datagram — the root included, which occupies the same one-per-frame slot
    // a bone does. The repeats are dropped; the first value stands.
    std::size_t duplicateBones = 0;
};

// What an operator needs to judge a session without a debugger, in the same
// spirit as `LiveCaptureStats` — which counts what the *intake* did with these
// frames, and so cannot say anything about the ones that never became frames.
struct VmcFrameStats
{
    std::uint64_t framesEmitted = 0;
    // Refused for a timestamp that did not advance. Their bones still count in
    // `bonesAccepted` below: they were decoded and mapped, and then the frame
    // they belonged to was dropped whole.
    std::uint64_t framesRefusedOutOfOrder = 0;
    // Closed carrying neither a bone nor a root. Never emitted; a stream that
    // produces these is one whose clock arrives without its content.
    std::uint64_t framesRefusedEmpty = 0;
    // Emitted, and missing at least one bone the session had observed.
    std::uint64_t framesIncomplete = 0;
    std::uint64_t sessionRestarts = 0;

    std::uint64_t bonesAccepted = 0;
    std::uint64_t bonesDuplicated = 0;
    // The two ways `MapVmcBoneTransform` refuses: a name outside the Unity
    // vocabulary, and a transform that is not one.
    std::uint64_t bonesUnsupported = 0;
    std::uint64_t bonesMalformed = 0;

    std::uint64_t rootsAccepted = 0;
    std::uint64_t rootsMalformed = 0;

    // Bones that crossed the staleness horizon, counted per crossing rather than
    // per frame.
    std::uint64_t stalenessCrossings = 0;

    // Availability, model, and blend-shape messages: none of them carry body
    // motion, so none of them open, close, or stamp a frame. The model's title
    // is still read out of one, as provenance.
    std::uint64_t messagesIgnored = 0;
};

// Feed it decoded packets in arrival order; take frames out.
//
// The unit is a packet rather than a message because the datagram boundary is
// load-bearing here and nowhere else: it is what tells a duplicated bone from a
// new frame. A caller that flattened its packets into a message stream would
// lose that distinction before this class could use it.
class VRMADAPTERVMC_API VmcFrameAssembler
{
public:
    explicit VmcFrameAssembler(const VmcFrameConfig& config = {});

    const VmcFrameConfig& GetConfig() const noexcept { return _config; }

    // The endpoint or fixture name stamped on every diagnostic this assembler
    // raises, so a session replayed from a capture reports the same `source` a
    // live one would.
    void SetSource(std::string source);
    const std::string& GetSource() const noexcept { return _source; }

    // Accepts one decoded datagram. `receiveTime` is the receiver's clock — the
    // `d` record's time on a replayed capture — and is used only to stamp a
    // frame whose sender sent no clock.
    //
    // Completed frames are appended to `frames` and diagnostics to
    // `diagnostics`; neither is ever cleared, so a caller can accumulate a
    // datagram's worth or a session's.
    //
    // Returns how many frames this packet *emitted*, which is how many were
    // appended — not how many it closed. A packet that ends a frame the clock
    // check then refuses closed one and returns zero, so the count is always the
    // growth of `frames` and never an announcement that a boundary was reached.
    // `GetStats()` is where a caller reads the difference.
    std::size_t Push(const VmcPacket& packet, double receiveTime,
                     std::vector<VmcFrame>* frames,
                     std::vector<Diagnostic>* diagnostics = nullptr);

    // Closes the frame still open at the end of a stream. Returns 1 when that
    // frame was emitted and 0 when there was none or it was refused.
    std::size_t Flush(std::vector<VmcFrame>* frames,
                      std::vector<Diagnostic>* diagnostics = nullptr);

    // What `/VMC/Ext/VRM` said, as canonical provenance: protocol "vmc", kind
    // `LiveCapture`, and the model's title as the source id. `provider` stays
    // empty because VMC has no field naming the sender application — the
    // application that filled the datagrams is a thing only its operator knows,
    // and guessing it from an avatar's title would be provenance that reads as
    // measured and is not.
    //
    // The model *path* is deliberately not carried. It names a file on the
    // sender's own machine, which this adapter may not resolve (§2) and has no
    // reason to propagate.
    const motion::MotionSourceMetadata& GetSourceMetadata() const noexcept
    {
        return _metadata;
    }

    // Bones this session has observed at least once. This is the rig the
    // completeness and staleness checks are measured against, and it is learned
    // from the stream rather than configured: a capture rig that solves no
    // fingers should not be reported as sending an incomplete frame forever.
    const std::bitset<motion::HumanBoneCount>& GetObservedBones() const noexcept
    {
        return _observed;
    }

    const VmcFrameStats& GetStats() const noexcept { return _stats; }
    void ResetStats() noexcept { _stats = VmcFrameStats(); }

    // Drops the open frame, the observed rig, the clock history, and the source
    // metadata — everything a restart invalidates. Stats survive, because they
    // describe the session the caller is judging rather than the stream's state.
    void Reset();

private:
    // The frame being accumulated. Separate from `VmcFrame` because what is
    // under construction and what is finished are not the same thing: this one
    // has no timestamp yet, and may never become a frame at all.
    struct OpenFrame
    {
        bool open = false;
        double receiveTime = 0.0;
        std::optional<double> senderTime;
        motion::HumanoidPose pose;
        std::optional<pxr::GfVec3f> hipsOffset;
        bool hasRoot = false;
        std::size_t duplicateBones = 0;
        // Which packet contributed each bone, so a repeat can be told from a
        // boundary. Only meaningful where `pose.validRotations` is set.
        std::array<std::uint64_t, motion::HumanBoneCount> bonePacket{};
        std::uint64_t rootPacket = 0;
    };

    void _Open(double receiveTime);
    // Closes the open frame, appending it when it is acceptable. Returns whether
    // a frame was emitted.
    bool _Close(std::vector<VmcFrame>* frames,
                std::vector<Diagnostic>* diagnostics);
    void _Report(std::vector<Diagnostic>* diagnostics, DiagnosticCode code,
                 std::string_view subject, std::optional<double> timestamp,
                 std::string detail);

    VmcFrameConfig _config;
    std::string _source;
    motion::MotionSourceMetadata _metadata;

    OpenFrame _frame;
    std::uint64_t _packetSerial = 0;

    std::bitset<motion::HumanBoneCount> _observed;
    // The timestamp of the last accepted frame that carried each observed bone.
    std::array<double, motion::HumanBoneCount> _lastSeen{};
    // Bones already reported stale, so the crossing is reported once rather than
    // on every frame until the bone comes back.
    std::bitset<motion::HumanBoneCount> _reportedStale;

    std::optional<double> _lastAccepted;
    VmcFrameStats _stats;
};

} // namespace vrmAdapterVmc
