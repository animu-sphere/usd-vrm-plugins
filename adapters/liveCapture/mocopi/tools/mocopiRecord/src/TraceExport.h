// SPDX-License-Identifier: Apache-2.0
//
// Where this adapter's half of the pipeline ends.
//
// `motion-capture-trace` is defined as "what an adapter delivered -- after
// protocol decode and coordinate conversion, before any intake policy"
// (motionRuntime/CaptureTrace.h), and that sentence describes a `MocopiFrame`
// exactly. So this file is a transcription rather than a conversion: a frame's
// pose is already a `motion::HumanoidPose`, already stamped with the sender's
// own clock, and nothing here computes a value that was not delivered.
//
// It exists so that no tool in the aggregate product has to link an adapter
// (WORKSPACE.md §2). `motion_capture` replays the file this writes exactly as it
// replays a generated fixture or a `.vrma`-derived one, and learns nothing about
// mocopi in the process -- which is what lets the release condition say
// **unchanged** about the two product tools a device session ends up passing
// through.
//
// ## The export reads a file, and a session still decodes nothing
//
// This is the one place this tool differs from `vmc_record`, and it is a
// decision rather than a stage of completion. `--export-trace` is accepted with
// `--inspect` alone: a recording writes datagrams, and a trace is derived from
// the file afterwards.
//
// Two things say so. `main.cpp`'s whole argument is that no decoder runs inside
// a recording -- the sibling states it as a rule about ordering and this tool
// keeps it by having no decoder in the process at all, and a live export would
// spend that property to save a command. And `Options.h` already records the
// consequence: there is no `--max-frames` here "and there cannot be", because
// this tool accumulates datagrams alone. A live export accumulates
// `sizeof(motion::HumanoidPose)` = 1320 bytes per frame beside the capture the
// datagram bound was sized for, which is the second bound in its own unit the
// sibling had to grow.
//
// What the restriction costs is one command, and what it buys is that an
// exported trace is a pure function of committed bytes: the same capture exports
// the same trace on any machine, with no device, which is the property the
// corpus tests already rest on and the one an operator's session most needs.
//
// ## One trace is one session
//
// A capture may hold more than one, because a source that is stopped and started
// puts its stream clock back and `MocopiFrameAssembler` reports that rather than
// repairing it. The two sessions are not one recording: their timestamps
// overlap, the second's rig is declared from scratch by the next skeleton
// packet, and `LiveCaptureSource` refuses a frame that does not advance on the
// newest it holds. So a trace written across a restart would replay as a session
// that stalls at the discontinuity -- the one thing this format promises never
// to be.
//
// Sessions are therefore collected separately and the caller says which one it
// wants. A capture with one session -- every capture the source did not restart
// during -- needs no such flag and is the ordinary case.
//
// ## What the trace cannot carry, and why that is not a loss
//
// A `MocopiFrame` knows things a trace has nowhere to put: which of the rig's
// bones this frame did not form, how many datagrams the transport lost before
// it, and how far the sender's two clocks have drifted apart. The capture keeps
// all of it, which is the verbatim record and the thing an operator keeps. A
// trace is the canonical half, and a canonical half that carried protocol
// residue would be a mocopi file with a neutral extension.
//
// The hips translation was the first entry on that list until 2026-08-23 and is
// no longer on it: `BodyPlacementPolicy::HipsOnly` makes it the pose's root, so
// it is canonical rather than residual and it crosses like any other canonical
// value. Nothing here decides that — a transcription that composed a root would
// be exactly the layer this file says it is not.
//
// One loss is worth naming for the release rather than for a reader, because it
// is a measurement this milestone owes: **a trace cannot carry the device**.
// `device` is the one header key `mocopi-packet-capture` has that the sibling
// format does not -- it is how a capture says what produced it -- and
// `motion-capture-trace` has exactly three provenance keys, none of them that.
// So the native path's claim to keep device state a relay drops holds as far as
// the capture and stops at the trace.
//
// ## The hips translation is measured on the way past, and said out loud
//
// The device sends the hips joint's absolute position in every frame -- the only
// translating joint on this rig -- and the collector measures how far it went,
// in the one place that has both the frames and the reason to care.
//
// It was added while §5.2 was open, when no layer on the live path was willing
// to call that translation root motion: it reached no `HumanoidPose` and
// therefore no trace, the bytes kept it, and nothing could read it back out. A
// cross-source session made the case concrete -- 1.17 m of walk arrived through
// the BVH path and nothing at all through this one, which is a fact about a
// *policy* and reads as a fact about the *device* unless something says
// otherwise at the point it happens.
//
// The record answered it on 2026-08-23 and this measurement is what survived
// unchanged, which is what the entry above predicted: the number that said what
// was being dropped now says what is being kept, and the line the tool prints
// changed its verb rather than being deleted. It is still a measurement and not
// a decision -- nothing here composes a `RootMotion`, the assembler does -- and
// keeping it is how an export from either side of the record stays comparable
// with the other.
#pragma once

#include "vrmAdapterMocopi/FrameAssembler.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/vec3f.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace mocopiRecordTool
{

// What one session's hips did, in metres, for the export to report as the thing
// the trace does not carry.
//
// `path` is the distance actually travelled -- the sum of the steps between
// consecutive frames -- and `net` is how far the body ended up from where it
// started. Both, because they answer different questions and disagree in the
// way that matters: a session that walks out and back has a large path and a
// small net, and reporting only the second would call it stationary.
struct HipsMotion
{
    double pathMetres = 0.0;
    double netMetres = 0.0;
    // Frames whose hips record did not arrive, so the path is a sum over the
    // rest. Zero in every session measured off a device; not assumed to be.
    std::size_t framesWithoutHips = 0;
};

// Accumulates delivered frames into one animation per session, in the order the
// sessions occurred.
//
// It observes rather than decides: a frame reaches it whether or not the intake
// admitted it, because the intake's policy is a consumer's and this is the
// adapter's output. The frames it necessarily never sees are the ones the
// assembler never emitted -- a frame refused for arriving before its rig was
// declared, or for a clock that did not advance, is not something the adapter
// delivered.
class TraceCollector
{
public:
    // `frames` is a push's worth, as `GetFramesFromLastPush()` returns them.
    //
    // `metadata` is passed on every call for symmetry with the sibling and for
    // no other reason: this protocol has no `/VMC/Ext/VRM`-shaped handshake, so
    // a session's metadata is fixed before its first frame and cannot change
    // under a collector mid-session (LiveSource.h). A caller amending it -- the
    // operator's `--sender` and `--source-id`, which are the capture header's
    // own and are the only provenance this protocol will ever have -- amends it
    // once.
    void Observe(const std::vector<vrmAdapterMocopi::MocopiFrame>& frames,
                 const motion::MotionSourceMetadata& metadata);

    // How many frames are held, across every session.
    std::size_t GetFrameCount() const noexcept { return _frames; }

    // Finalises every session: the time range from its own first and last
    // sample, and a frame rate measured from them. Idempotent.
    //
    // The rate is **measured** even though this device's is exactly 60 Hz in
    // every session ever recorded off it (MotionPacket.h). Declaring 60 would
    // publish a constant the file cannot support: a capture bounded mid-stream,
    // one the transport thinned, or a device firmware that changes it would each
    // produce a trace whose declared rate disagreed with its own samples. The
    // formula is `CaptureTrace.h`'s own derivation for a trace whose header
    // omits `frameRate`, so a file this writes reads back as the same animation
    // rather than as one whose rate was re-derived slightly differently.
    void Close();

    // Valid after `Close`. Sessions that produced no frame are not among them.
    const std::vector<motion::HumanoidAnimation>& GetSessions() const noexcept
    {
        return _sessions;
    }

    // The hips motion of the session at the same index, which is the part of
    // that session the trace does not carry. Indices match `GetSessions()`
    // after `Close`, including the empty-session pruning.
    const std::vector<HipsMotion>& GetHipsMotion() const noexcept
    {
        return _hips;
    }

private:
    std::vector<motion::HumanoidAnimation> _sessions;
    std::vector<HipsMotion> _hips;
    // Per open session, carried alongside rather than inside `HipsMotion`: the
    // first and last positions seen, which is what `net` is measured from and
    // what the running `path` steps away from.
    std::vector<std::optional<pxr::GfVec3f>> _hipsFirst;
    std::vector<std::optional<pxr::GfVec3f>> _hipsLast;
    std::size_t _frames = 0;
    bool _closed = false;
};

} // namespace mocopiRecordTool
