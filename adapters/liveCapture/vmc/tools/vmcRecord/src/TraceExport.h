// SPDX-License-Identifier: Apache-2.0
//
// Where this adapter's half of the pipeline ends.
//
// `motion-capture-trace` is defined as "what an adapter delivered -- after
// protocol decode and coordinate conversion, before any intake policy"
// (motionRuntime/CaptureTrace.h), and that sentence describes a `VmcFrame`
// exactly. So this file is a transcription rather than a conversion: a frame's
// pose is already a `motion::HumanoidPose`, already stamped, already rooted, and
// nothing here computes a value that was not delivered.
//
// It exists so that no tool in the aggregate product has to link an adapter
// (WORKSPACE.md §2). `motion_capture` replays the file this writes exactly as it
// replays a generated fixture or a `.vrma`-derived one, and learns nothing about
// VMC in the process -- which is what keeps a live session and a recorded clip
// on the same path instead of on two.
//
// ## One trace is one session
//
// A capture may hold more than one, because a sender that is stopped and started
// puts its clock back and `VmcFrameAssembler` reports that rather than repairing
// it. The two sessions are not one recording: their timestamps overlap, the
// second's rig is observed from scratch, and `LiveCaptureSource` refuses a frame
// that does not advance on the newest it holds. So a trace written across a
// restart would replay as a session that stalls at the discontinuity -- which is
// the one thing this format promises never to be, since replaying a trace is
// meant to be indistinguishable from the session that produced it.
//
// The alternatives were both worse. Splicing the second session onto the first
// manufactures continuity out of a discontinuity, which is the class of
// invention LiveSource.h refuses at the layer below. Writing one file and
// letting it stall hides a restart inside a file that looks complete.
//
// So sessions are collected separately and the caller says which one it wants.
// A capture with one session -- every capture a sender did not restart during --
// needs no such flag and is the ordinary case.
//
// ## What the trace does not carry, and why that is not a loss
//
// A `VmcFrame` knows things a trace has nowhere to put: which bones were missing
// and which of those were stale, the hips offset nobody has yet decided the
// meaning of, and how many samples were refused as duplicates inside one
// datagram. None of it is dropped -- it stays in the capture, which is the
// verbatim record and the thing an operator keeps. A trace is the canonical
// half, and a canonical half that carried protocol residue would be a VMC file
// with a neutral extension.
#pragma once

#include "vrmAdapterVmc/FrameAssembler.h"

#include "motionCore/Humanoid.h"

#include <cstddef>
#include <vector>

namespace vmcRecordTool
{

// Accumulates delivered frames into one animation per session, in the order the
// sessions occurred.
//
// It observes rather than decides: a frame reaches it whether or not the intake
// admitted it, because the intake's policy is a consumer's and this is the
// adapter's output. The one frame class it necessarily drops is the one the
// assembler never emitted -- a frame refused for a timestamp that did not
// advance is not something the adapter delivered.
class TraceCollector
{
public:
    // `frames` is a push's worth, as `GetFramesFromLastPush()` returns them.
    // `metadata` is the source's as of now, which is why it is passed on every
    // call rather than once: `/VMC/Ext/VRM` may arrive at any point in a
    // session, and a session ends up carrying what its sender had said by its
    // last frame. Stamping the whole capture with the metadata it ended on
    // would claim a session knew a model title that arrived after it.
    void Observe(const std::vector<vrmAdapterVmc::VmcFrame>& frames,
                 const motion::MotionSourceMetadata& metadata);

    // Finalises every session: the time range from its own first and last
    // sample, and a frame rate measured from them. Idempotent.
    //
    // The rate is measured because VMC declares none -- a sender sends when it
    // sends. The formula is `CaptureTrace.h`'s own derivation for a trace whose
    // header omits `frameRate`, so a file this writes reads back as the same
    // animation rather than as one whose rate was re-derived slightly
    // differently.
    void Close();

    // Valid after `Close`. Sessions that produced no frame are not among them.
    const std::vector<motion::HumanoidAnimation>& GetSessions() const noexcept
    {
        return _sessions;
    }

private:
    std::vector<motion::HumanoidAnimation> _sessions;
    bool _closed = false;
};

} // namespace vmcRecordTool
