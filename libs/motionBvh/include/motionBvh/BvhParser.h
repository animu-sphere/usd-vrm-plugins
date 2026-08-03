// SPDX-License-Identifier: Apache-2.0
//
// BVH text in, `BvhDocument` out. No semantics anywhere in it.
//
// Four rules are decisions rather than details, and each one is a failure this
// parser is shaped to avoid.
//
// **A file parses entirely or not at all.** The first refusal ends the parse,
// `document` is left untouched, and `diagnostic` names what was refused and
// where. A half-read document is worse than a refused one: the layer above
// cannot tell which half it got, and the producer gets blamed for a file it
// wrote whole. This is the OSC decoder's rule, for the same reason
// (`vrmAdapterVmc/OscPacket.h`).
//
// **A frame is a line.** The `MOTION` section is read line by line rather than
// as a flat stream of numbers, because a flat stream cannot tell a short row
// from a missing frame — it only notices at the end of the file, and reports
// the last frame as broken when the first one was. Reading lines puts
// `VRM_BVH_FRAME_WIDTH_MISMATCH` on the row that is actually short. Blank lines
// are skipped: writers pad, and padding is not data.
//
// **Keywords are case-insensitive, joint names are verbatim.** Writers disagree
// about `Frames:` versus `FRAMES:` and about `Xrotation` versus `XROTATION`,
// and a case difference in a keyword is not a difference in meaning. A joint
// name is the file's own word, is compared byte for byte by every profile above,
// and is never touched.
//
// **A declared count is not an allocation.** `Frames: 2000000000` allocates
// nothing here: rows are appended as they are read, so a file's memory cost
// follows the bytes it actually carries. The limits below refuse the
// pathological shapes outright.
#pragma once

#include "motionBvh/BvhDocument.h"
#include "motionBvh/Diagnostics.h"
#include "motionBvh/api.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace motionBvh
{

// Refusals of the pathological case rather than limits anyone will meet: a
// hierarchy 64 deep is already twice a full-hand humanoid's, and a motion
// section of a million frames is nine hours at 30 Hz. A caller with a genuine
// reason can raise any of them; a test lowers them to check the refusal without
// committing a pathological fixture.
struct BvhParseLimits
{
    std::size_t maxHierarchyDepth = 64;
    std::size_t maxJoints = 4096;
    std::size_t maxFrames = 1000000;
};

struct BvhParseOptions
{
    // Named in every diagnostic this parse raises: a path, a fixture name, or
    // empty when the text came from nowhere nameable. `ParseBvhFile` fills it
    // with the path when it is left empty.
    std::string source;
    BvhParseLimits limits;
};

// Parses `text`. On failure `document` is untouched and `diagnostic`, when
// given, carries the code and the 1-based line the refusal is about.
//
// A leading UTF-8 byte-order mark is skipped: Windows exporters write one, it
// is not part of the `HIERARCHY` keyword, and refusing a file for it would be
// refusing a file nothing is wrong with.
MOTIONBVH_API bool ParseBvhText(std::string_view text, BvhDocument* document,
                                Diagnostic* diagnostic = nullptr,
                                const BvhParseOptions& options = {});

// Reads the file and parses it. A file that cannot be opened or read is
// `VRM_BVH_PARSE_FAILED` with the reason in `detail`: this layer has no code of
// its own for I/O, and inventing one would widen the frozen set for a failure
// that is not about BVH.
MOTIONBVH_API bool ParseBvhFile(const std::filesystem::path& path,
                                BvhDocument* document,
                                Diagnostic* diagnostic = nullptr,
                                const BvhParseOptions& options = {});

} // namespace motionBvh
