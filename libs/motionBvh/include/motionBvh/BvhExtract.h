// SPDX-License-Identifier: Apache-2.0
//
// BVH shapes into neutral shapes — the one place a syntax model meets the
// format-neutral one (roadmap/recorded-motion-sources.md §2).
//
// `BvhDocument` says what the bytes carry. `motionSource` says what a recording
// is in terms no format supplies. This file is where the first becomes the
// second, and it is the change that realises the declared `motionBvh ->
// motionSource` edge (WORKSPACE.md §2) — an edge that never reverses, which is
// why everything below reads a document and writes source values and nothing
// here reads a source value back.
//
// It decides nothing a profile decides: not the unit, not the axes, not the
// handedness, not which joint carries which bone, and not what a root
// translation means. What it does decide is how a channel set becomes a track,
// and every one of those answers is a property of the **format** rather than of
// whoever wrote the file. That is the whole test for whether a rule belongs
// here: if a second producer could reasonably disagree with it, it is a
// profile's and not this file's.
//
// Six decisions, each a choice rather than a detail.
//
// **Position channels state a joint's local translation; they do not add to its
// `OFFSET`.** This is the format's convention and the recorded half of this
// library's corpus confirms it twice over: every non-root position column there
// is exactly its joint's `OFFSET` in every row, and the root's begin at the
// root's own `OFFSET`. Read additively, that rig would stand at twice its own
// height with every bone twice its own length, in a file nothing is wrong with.
//
// **A partial position channel set falls back to the `OFFSET`, component by
// component.** A joint declaring only `Xposition` animated one component and
// stated the other two in its hierarchy; substituting zero would move it onto
// its parent. This is the only place this file supplies a value the motion
// section did not carry, and it supplies one the file did state.
//
// **The Euler order is the *relative* order of the rotation channels, whatever
// sits between them.** `Xposition Zrotation Yposition Xrotation Yrotation`
// declares ZXY. The relative order is unambiguous and is the only statement a
// BVH file makes about its rotation order, so refusing an interleaved
// declaration would refuse a file that said exactly what it meant.
//
// **Rotation channels that cannot form an order are refused, and the code is
// `VRM_BVH_INVALID_ROTATION_ORDER`.** That code sits in the frozen set's
// semantic half (Diagnostics.h) and this is the one place outside the syntax
// layer that may raise it — which is not a hole in the boundary but the
// boundary being drawn one row further down than the parser: a joint declaring
// two rotation channels, or the same axis twice, cannot form an Euler order
// whoever wrote the file and whatever profile is later named for it. No profile
// is involved, so nothing here needs to know one exists.
// `motionBvh_boundaries` grants this file that single code by name, in review,
// the way `motionSource` grants a file its canonical crossing.
//
// **A joint with no rotation channels is not an error.** `CHANNELS 0` and a
// position-only joint are legal BVH, and a static prop exported beside a rig is
// the ordinary case — the resulting track simply carries no rotation, which is
// a thing `SourceJointTrack` can say.
//
// **The angle unit is stated here, and it is degrees.** A format says whether
// its angles are degrees or radians and a producer does not get to disagree with
// its own format about that, which is exactly why a profile carries a
// translation unit and no angle unit (SourceProfile.h). A rest *rotation* is
// left unset rather than set to identity, because BVH states none and the
// difference between "no rest rotation" and "an identity one" is one the
// converter acts on (SourceSkeleton.h).
#pragma once

#include "motionBvh/BvhDocument.h"
#include "motionBvh/Diagnostics.h"
#include "motionBvh/api.h"

#include "motionSource/SourceAnimation.h"
#include "motionSource/SourceSkeleton.h"

#include <string>
#include <string_view>

namespace motionBvh
{

// This reader's own label for the format it reads, carried into
// `SourceProvenance::format`. The layer above never compares it to anything —
// it is provenance, and a library that switched on it would know which readers
// exist (SourceProvenance.h).
MOTIONBVH_API std::string_view BvhFormatLabel() noexcept;

struct BvhExtractOptions
{
    // Which file or capture this was, reaching `SourceProvenance::sourceId`
    // unchanged: a path, a name, or a content hash, as the caller identifies it.
    // Also named in any diagnostic this extraction raises, so a refusal says
    // which input it is about.
    std::string sourceId;
};

// Turns `document` into the rig and the animation a profile is matched against.
//
// On failure both outputs are left untouched and `diagnostic`, when given,
// names what was refused. The parser's rule, for the parser's reason: a caller
// cannot tell which half of a half-extracted document it got.
//
// The producer and the profile id are deliberately **not** filled in. Both are a
// profile's answers and this layer has no profile; a reader that guessed either
// would be concluding a writer from a file that states none, which is the
// failure roadmap §3.1 forbids. The converter fills them, holding both.
MOTIONBVH_API bool ExtractBvhSource(const BvhDocument& document,
                                    motionSource::SourceSkeleton* skeleton,
                                    motionSource::SourceAnimation* animation,
                                    Diagnostic* diagnostic = nullptr,
                                    const BvhExtractOptions& options = {});

} // namespace motionBvh
