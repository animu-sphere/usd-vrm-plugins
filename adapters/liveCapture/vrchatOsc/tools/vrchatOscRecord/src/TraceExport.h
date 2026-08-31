// SPDX-License-Identifier: Apache-2.0
//
// Where this adapter's half of the pipeline ends — and the one place in the
// repository where a tracker frame, an operator's statement and a canonical
// pose are all in scope at once.
//
// `motion-capture-trace` is defined as "what an adapter delivered -- after
// protocol decode and coordinate conversion, before any intake policy"
// (motionRuntime/CaptureTrace.h). For the two pose sources that sentence
// describes a frame exactly, and their exports are transcriptions. **Here it
// does not**, and the difference is the whole of VRC-5: a `TrackerFrame` is a
// handful of observations of places on a body, and a `HumanoidPose` is a rig's
// joints. Something has to turn the first into the second, and this file is
// where that something is *called* rather than where it lives:
//
//     TrackerFrame -> TrackerObservation[] -> AssignTrackers -> SolveTrackerPose
//
// Every step but the first belongs to `libs/motionTracking`. This file converts
// a frame's samples into that library's observation type, hands them over, and
// keeps what comes back. The permission is `adapters/*/tools/* ->
// motionTracking` ([WORKSPACE.md §2](../../../../../docs/architecture/WORKSPACE.md)),
// and it is a *tool's* permission for a reason this file is the demonstration
// of: the assignment is an operator's statement about a rig, so an adapter
// library that resolved it would have invented a calibration and hidden it
// inside a decoder.
//
// It exists so that no tool in the aggregate product has to link an adapter
// (§2). `motion_capture` replays the file this writes exactly as it replays a
// generated fixture or a `.vrma`-derived one, and learns nothing about VRChat
// OSC — or about trackers — in the process.
//
// ## The conversion is one struct onto another, and it is deliberately dull
//
// `TrackerSample` and `TrackerObservation` carry the same four things: an
// identity, a position, a rotation and a flag under each. They are two types
// because they are owned by two layers that must not know about each other, not
// because they disagree — and this file is the only place that says so. The one
// field that does not cross is `TrackerSample::index`, which is a property of
// the *address* (a decimal segment) and means nothing to a body; a solve that
// read it would be reading one wire's numbering convention.
//
// **`receiveTime` does not cross either, and its absence is the contract's.** An
// observation carries no clock, because a frame's time is the frame's and the
// layer that assembled one is the layer that knows it (TrackerObservation.h). So
// `SolveTrackerPose` is handed `TrackerFrame::receiveTime` and the per-sample
// times are dropped here rather than averaged into something no sender sent.
//
// ## One trace is one session, and the reason is not the sibling tools' reason
//
// A capture may hold more than one, because a sender restarted during it — on
// this wire a restart is a new ephemeral source port and nothing else, which is
// what `TrackerFrame::beginsNewSession` reports. Both siblings refuse to splice
// two sessions into one trace because **their clocks overlap**: a sender that is
// stopped and started puts its own timestamps back to zero, so the second
// session's first sample is earlier than the first session's last.
//
// **That is not true here, and it was measured rather than assumed.** VRChat's
// tracker addresses carry three floats and no timestamp, so the only clock this
// path has is the *receiver's* — which is monotonic across a restart. The
// `session-restart` fixture's two halves are 4.8452 s apart in the right
// direction, and a splice would produce a file that reads as one continuous
// recording with a long still stretch in the middle.
//
// So the refusal stands and its justification changes, which is worth stating
// because the weaker reason is the more common one: the two halves are two
// *senders*. The second is a different peer, its rig is observed from scratch,
// and nothing relates the calibration either side of the gap — VRChat's tracking
// space is established by a calibration the receiving application performs, and
// a new session is a new one. A trace that ran them together would assert a
// continuity of *space*, which no clock can supply and this layer cannot check.
//
// The alternatives were the siblings' alternatives and they fail for the same
// reasons: splicing invents continuity, and writing one file that goes still at
// the discontinuity hides a restart inside a file that looks complete. So
// sessions are collected separately and the caller says which one it wants.
//
// ## A refused frame is not a gap in a trace, and this is where that is decided
//
// A solve refuses for four reasons and only one of them is about *this* frame's
// numbers. The other three are about the assignment — a spec that does not
// validate, a spec applied to the wrong array, a binding set nothing placed —
// and under those **every** frame refuses, so a trace built from them would be
// empty and the report is what says why.
//
// A frame that refuses is not written and no placeholder is written for it. That
// is the same rule the sibling collectors follow for a frame the assembler never
// emitted, and it holds here for a stronger reason: a pose interpolated across a
// refusal would be this layer inventing motion, and the layer that may do that
// is the intake policy in `motionRuntime`, downstream, where a consumer can see
// it happening.
//
// ## What the trace does not carry, and why that is not a loss
//
// A `TrackerFrame` knows things a trace has nowhere to put: which trackers were
// missing and which of those were stale, whether the frame follows a calibration
// discontinuity, and how many messages were refused as repeats inside one
// datagram. None of it is dropped — it stays in the capture, which is the
// verbatim record and the thing an operator keeps. A trace is the canonical
// half, and a canonical half that carried protocol residue would be a VRChat OSC
// file with a neutral extension.
//
// `SolveReport` is where the rest of it goes: what the solve placed, what it
// could not, and every position it was handed and did not consume. That is not
// trace content — it is the operator's answer to "did my rig reach the avatar",
// and it is printed rather than serialised.
#pragma once

#include "vrmAdapterVrchatOsc/FrameAssembler.h"

#include "motionCore/Humanoid.h"
#include "motionTracking/TrackerAssignment.h"
#include "motionTracking/TrackerObservation.h"
#include "motionTracking/TrackerRegion.h"
#include "motionTracking/TrackerSolve.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace vrchatOscRecordTool
{

// What every solve in one export added up to.
//
// Counted per region rather than per frame, because the question an operator
// asks is about a device: "did the strap on my left foot reach a joint" is
// answered by a number against `leftFoot`, and a per-frame total answers it for
// nobody. The four vectors of `TrackerSolve` are tallied separately for the
// reason that struct keeps them separate — `unsolved` says a strap reached no
// bone and `positionsUnused` says a number nothing read, and a region can be in
// both.
struct SolveReport
{
    std::size_t framesObserved = 0;
    std::size_t framesSolved = 0;

    // Per refusal enumerator, including `None`, so the counts sum to
    // `framesObserved` and a reader can check that they do.
    std::array<std::size_t, motionTracking::TrackerSolveRefusalCount> refusals{};
    // The first detail seen under each refusal. One line rather than a count:
    // a 2000-frame session that refuses every frame refuses for one reason, and
    // 2000 copies of it would bury the report an operator ran this for.
    std::array<std::string, motionTracking::TrackerSolveRefusalCount> firstDetail;

    // Over solved frames only. A refused solve reports nothing about a region,
    // and folding its empty vectors in would make a session that refused
    // everything look like one whose straps were merely unused.
    std::array<std::size_t, motionTracking::TrackerRegionCount> placed{};
    std::array<std::size_t, motionTracking::TrackerRegionCount> unsolved{};
    std::array<std::size_t, motionTracking::TrackerRegionCount> withoutRotation{};
    std::array<std::size_t, motionTracking::TrackerRegionCount> positionsUnused{};

    // Stated regions no frame ever carried a tracker for, and observed trackers
    // no statement places — the assignment layer's two ways to miss, kept apart
    // here as they are there. Both are recorded from the *last* frame that
    // produced an assignment at all: they describe the rig rather than an
    // instant, and a per-frame tally of an absence would count one missing
    // device once per frame.
    std::vector<motionTracking::TrackerRegion> absent;
    std::vector<std::string> unplaced;
};

// How far the hips travelled in what the trace carries, which is the largest
// thing a trace holds beside the rotations and the one an operator can check
// against a session they remember performing.
//
// Printed for every export including the ones that stayed put: "0.02 m of hips
// path" is the useful answer for a still session, and a line that appeared only
// above some threshold would leave a reader unable to tell a still session from
// an unmeasured one. Borrowed wholesale from `mocopi_record`, which measured
// what it is worth.
struct HipsMotion
{
    double pathMetres = 0.0;
    double netMetres = 0.0;
    std::size_t framesWithoutRoot = 0;
};

// Accumulates solved frames into one animation per session, in the order the
// sessions occurred.
//
// It observes rather than decides: a frame reaches it whether or not any intake
// would admit it, because the intake's policy is a consumer's and this is the
// adapter's output.
class TraceCollector
{
public:
    // The statement and the solve's own configuration, both fixed for the whole
    // export. An assignment that changed mid-capture would be a second
    // calibration nobody stated, and the operator who could state one is not at
    // the prompt any more.
    TraceCollector(motionTracking::TrackerAssignmentSpec assignment,
                   motionTracking::TrackerSolveConfig solve);

    // `frames` is a push's worth, as `TrackerFrameAssembler::Push` appended
    // them. `metadata` is stamped on the session rather than on each pose,
    // which is where `HumanoidAnimation` carries it.
    void Observe(const std::vector<vrmAdapterVrchatOsc::TrackerFrame>& frames,
                 const motion::MotionSourceMetadata& metadata);

    // How many poses are held, across every session.
    std::size_t GetFrameCount() const noexcept { return _poses; }

    // Finalises every session: the time range from its own first and last
    // sample, and a frame rate measured from them. Idempotent.
    //
    // The rate is measured because this wire declares none — its tracker
    // addresses carry three floats and no timestamp, so arrival order is the
    // only clock there is. The formula is `CaptureTrace.h`'s own derivation for
    // a trace whose header omits `frameRate`, so a file this writes reads back
    // as the same animation rather than as one whose rate was re-derived
    // slightly differently.
    void Close();

    // Valid after `Close`. Sessions that produced no pose are not among them,
    // and `GetHipsMotion()` is indexed alongside.
    const std::vector<motion::HumanoidAnimation>& GetSessions() const noexcept
    {
        return _sessions;
    }
    const std::vector<HipsMotion>& GetHipsMotion() const noexcept
    {
        return _hips;
    }

    const SolveReport& GetReport() const noexcept { return _report; }

private:
    void _OpenSession();

    motionTracking::TrackerAssignmentSpec _assignment;
    motionTracking::TrackerSolveConfig _solve;

    std::vector<motion::HumanoidAnimation> _sessions;
    std::vector<HipsMotion> _hips;
    SolveReport _report;
    std::size_t _poses = 0;
    bool _closed = false;
};

// The names an operator reads back. Both are here rather than in the report
// printer because they are about what this export did, and `SessionReport` is
// about what a socket saw.
void PrintSolveReport(std::FILE* out, const SolveReport& report);

} // namespace vrchatOscRecordTool
