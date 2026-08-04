// SPDX-License-Identifier: Apache-2.0
//
// What the file says, printed back in a fixed order.
//
// Three rules, and each is the tool's half of a boundary the library draws.
//
// **Nothing here derives a meaning.** An offset is printed as the three numbers
// it is, with no unit and no axis label; a rotation channel is printed under the
// name the file declared, in the order the file declared it. The one derived
// number in the whole report is the frame rate beside the frame time, it is
// marked `~` because it is a rounded reciprocal, and it is arithmetic on a
// value BVH defines in seconds rather than an interpretation of one.
//
// **Every block is deterministic.** Joints print in declaration order, columns
// in row order, and floats through one formatter — so two runs over the same
// file are byte-identical and a test can compare them. `%.7g` is the shortest
// form that round-trips a float, which keeps `0.0333333` from printing as
// `0.0333333015`.
//
// **A joint is identified by index, not by name.** BVH does not require joint
// names to be unique and real exports repeat them, so `[2] Hand` says which of
// the two `Hand`s a line is about where a bare `Hand` would not — and a profile
// author reading this report is exactly the person who has to see that
// ambiguity (`BvhDocument::FindJoints`).
#pragma once

#include "motionBvh/BvhDocument.h"

#include <cstddef>
#include <ostream>
#include <string>

namespace motionBvhTool
{

// The counts the file states, and the repeated joint names it does not.
// `source` is printed verbatim as the caller spelled it.
void PrintSummary(std::ostream& out, const motionBvh::BvhDocument& document,
                  const std::string& source);

// Joints in declaration order, indented by depth, each with its offset, its
// channels in declaration order, and the row column its first channel occupies.
void PrintHierarchy(std::ostream& out, const motionBvh::BvhDocument& document);

// Row column -> joint and channel. The inverse of the hierarchy block, and the
// one that answers "which number in this row is that".
void PrintChannelMap(std::ostream& out, const motionBvh::BvhDocument& document);

// One motion row, by joint. `frameIndex` must be in range; the caller checks it
// so the refusal can name the file's frame count.
void PrintFrame(std::ostream& out, const motionBvh::BvhDocument& document,
                std::size_t frameIndex);

// Per-column smallest and largest value across every frame. This is the block
// BVH-0 measures a real producer's export with: whether a root translation is
// in the tens or the hundredths is what separates one writer's unit from
// another's, and it is a measurement rather than a conclusion.
void PrintChannelRanges(std::ostream& out,
                        const motionBvh::BvhDocument& document);

} // namespace motionBvhTool
