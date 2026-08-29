// SPDX-License-Identifier: Apache-2.0
//
// The two things every corpus pass in this adapter does before it can make a
// claim: find the captures, and replay one datagram.
//
// Six binaries read `tests/corpus/`, and each had written the scan out for
// itself. That is six copies of four lines, and they had already drifted into
// two behaviours: `std::filesystem::directory_iterator` **throws** on a path
// that is not a directory, nothing in a test file catches it, so the process
// calls `std::terminate` — on Windows an abort with exit `0xC0000409` and no
// message at all, where each of those files has a "no captures in ..." line it
// plainly meant to print. Two copies checked first and four did not, which is
// reachable by a mistyped argument and by a corpus present at configure time and
// absent at test time in a relocated or packaged build tree.
//
// The replay is the other half and the one that could go quietly wrong rather
// than loudly. `test_udp_receiver.cpp` replays every capture twice — once from
// the file and once through a real socket — and asserts the two agree, which is
// how it says the receiver changed nothing about the bytes. Two copies of the
// push sequence are two pipelines; if they drift, that test compares one against
// the other and reports it as a statement about the socket. `test_live_source.cpp`
// held a third copy of the same sequence.
//
// So the sequence is stated once, and the **buffer discipline is not a parameter
// but a property of the call**: `PushDatagram` takes a mutable buffer and
// poisons it the moment the push returns, before anything reads what the push
// produced. A decoder that retained a `string_view` into the caller's bytes
// survives the shape where the next receive happens to overwrite them and
// produces garbage under this one. No caller can opt out of that by writing its
// loop slightly differently, which is what the three copies had each done.
//
// What is deliberately **not** here is anything that decides. Every tolerance,
// every expected count and every assertion lives in the file making the claim —
// the same rule `fixtures.h` states, and for the same reason: two tests sharing
// an expected number agree with each other rather than with the layer. What each
// binary keeps out of a push is its own business too, which is why this returns
// what one datagram produced and stores nothing.
//
// The sibling adapter's tests have this shape and this duplication. The types
// below are mocopi's, so the header is not shareable as it stands; the
// arrangement is, and a VMC copy of it is the same change one namespace over.
#pragma once

#include "vrmAdapterMocopi/Diagnostics.h"
#include "vrmAdapterMocopi/FrameAssembler.h"
#include "vrmAdapterMocopi/LiveSource.h"

#include "motionCore/Humanoid.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vrmAdapterMocopiTests
{

// Every `.mocopipackets` file in `directory`, sorted, or false with a line on
// `stderr` saying which of the two refusals it was. The sort is what makes a
// corpus pass report the same order on every platform; `is_regular_file` is what
// keeps a directory named like a capture — or a dangling symlink — from being
// read as one, since the corpus README invites an operator to drop a recording
// in here by hand.
inline bool
CollectCaptures(const std::filesystem::path& directory,
                std::vector<std::filesystem::path>* out)
{
    // Checked rather than assumed, and with the non-throwing overload: this is
    // the crash described in the header, and any unrecognised argument to any of
    // the six binaries reaches this line.
    std::error_code failed;
    if (!std::filesystem::is_directory(directory, failed)) {
        std::fprintf(stderr, "not a corpus directory: %s\n",
                     directory.string().c_str());
        return false;
    }

    out->clear();
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()
            && entry.path().extension() == ".mocopipackets") {
            out->push_back(entry.path());
        }
    }
    std::sort(out->begin(), out->end());

    if (out->empty()) {
        std::fprintf(stderr, "no captures in %s\n", directory.string().c_str());
        return false;
    }
    return true;
}

// What one datagram produced. The frames are copies rather than references
// because the next push replaces them, and a caller comparing two replays holds
// both.
struct PushedDatagram
{
    std::size_t admitted = 0;
    std::vector<vrmAdapterMocopi::MocopiFrame> frames;
    bool restartLatched = false;
    // The pose a consumer sampled, present exactly when a frame was admitted and
    // the buffer had something to answer with.
    std::optional<motion::HumanoidPose> sampled;
};

// One datagram through the whole bridge: push, poison, latch the restart, sample
// at the delivered frame's stored timestamp.
//
// `bytes` is filled with `0xcd` before anything reads what the push produced, so
// the claim that a datagram need not outlive the call is checked by the poses a
// caller compares rather than by an assertion about pointers. It is a pointer
// rather than a value for that reason alone.
inline PushedDatagram
PushDatagram(vrmAdapterMocopi::MocopiLiveSource* source,
             std::vector<std::uint8_t>* bytes, double receiveTime,
             std::vector<vrmAdapterMocopi::Diagnostic>* diagnostics)
{
    PushedDatagram out;
    out.admitted = source->PushDatagram(*bytes, receiveTime, diagnostics);
    std::fill(bytes->begin(), bytes->end(), std::uint8_t{0xcd});

    out.restartLatched = source->ConsumeSessionRestart();
    out.frames = source->GetFramesFromLastPush();
    if (out.admitted == 0 || out.frames.empty()) {
        return out;
    }

    // `GetFramesFromLastPush()` is a vector because the sibling adapter can emit
    // several frames from one push. This protocol cannot — one datagram is one
    // frame, measured — and the sampling below relies on it: it attributes the
    // sampled pose to `frames.back()`, which is only the admitted frame while
    // there is exactly one. Asserted rather than assumed, so an assembler that
    // ever emitted two would fail loudly here instead of silently dropping the
    // earlier frame out of a caller's comparison.
    assert(out.frames.size() == 1);

    // Sampled at the pose's **stored** timestamp rather than at one recomputed
    // from the frame rate. `time` is binary32 on the wire, so a recomputed
    // instant falls *between* two stored ones and the buffer interpolates —
    // which would compare one interpolation against another and measure the
    // arithmetic rather than the layer under test (MOTION_CONTRACT.md).
    const motion::PoseSampleResult result =
        source->Sample(out.frames.back().pose.timestamp);
    if (result.pose) {
        out.sampled = *result.pose;
    }
    return out;
}

// The three tallies every replay reads at the end, from the three objects that
// keep them. Read together because they are only meaningful together: the
// bridge's, the assembler's, and the intake's.
struct ReplayStats
{
    vrmAdapterMocopi::MocopiLiveSourceStats source;
    vrmAdapterMocopi::MocopiFrameStats frame;
    motion::LiveCaptureStats intake;
};

inline ReplayStats
ReadStats(const vrmAdapterMocopi::MocopiLiveSource& source)
{
    ReplayStats out;
    out.source = source.GetStats();
    out.frame = source.GetAssembler().GetStats();
    out.intake = source.GetIntake().GetStats();
    return out;
}

} // namespace vrmAdapterMocopiTests
