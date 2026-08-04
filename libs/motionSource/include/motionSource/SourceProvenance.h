// SPDX-License-Identifier: Apache-2.0
//
// Where a recorded clip came from — the reader's and the profile's answers about
// the file itself, kept apart from the motion it carried.
//
// **This is a neighbour of `motion::MotionSourceMetadata`, not the same type and
// not a superset**, and settling that was owed before a converter set its first
// field (roadmap/recorded-motion-sources.md §10). Two things separate them, and
// either one alone would have been enough:
//
// *They travel differently.* `MotionSourceMetadata` rides on every pose and
// every canonical animation, is compared by `operator==`, and is written into
// the recorded-trace format — so a field added to it is a field every sample
// carries and every trace must round-trip. A file's producer version and the
// profile it was read under are facts about one conversion, stated once. Putting
// them on the canonical type would have paid the per-sample cost of both for a
// value that cannot vary within a clip.
//
// *They are answerable by different layers.* Everything here is known before any
// motion is: a reader supplies the format and the file's identity, a profile
// supplies the producer label, and a caller supplies which profile it named.
// `MotionSourceMetadata` describes motion that by then already exists.
//
// The relationship is one-way and narrowing: `CanonicalMetadata.h` derives the
// canonical value from this one, and nothing derives this one back. What the
// narrowing drops — the producer version and the profile id — is what a
// conversion records beside its output rather than inside the motion, so the
// derivation is where that loss is stated instead of being discovered later by
// whoever goes looking for a profile id in a pose.
//
// Every field is a label carried verbatim. Nothing here is ever a branch
// condition in this library: a producer name reaching an `if` is the failure the
// whole profile design exists to prevent (WORKSPACE.md §1).
#pragma once

#include "motionSource/api.h"

#include <string>

namespace motionSource
{

struct SourceProvenance
{
    // Who wrote the file, from the profile's provenance label. Never sniffed
    // from the file's contents: a file carries no reliable statement of its
    // writer, and concluding one from joint names is the specific failure
    // roadmap §3.1 forbids.
    std::string producer;

    // The writing application's version, when the file states one. Not part of a
    // profile id — a version that changes nothing about the output contract must
    // not fragment the profile set — so it is kept here and in a corpus
    // manifest, which is where a later reader goes looking for it.
    std::string producerVersion;

    // The profile this was read under, `<producer>-<format>-<preset>-v<N>`.
    // Empty means no profile was named, which is not a default and not a
    // fallback: it is the state a conversion refuses from.
    std::string profileId;

    // The reader's own label for the format it read. This library never compares
    // it to anything — it is provenance, and a library that switched on it would
    // know which readers exist, which is exactly the dependency WORKSPACE.md §2
    // says never reverses.
    std::string format;

    // Which file or capture this was: a path, a name, or a content hash, as the
    // caller identifies it. Two conversions of the same motion from two files
    // differ here and nowhere else.
    std::string sourceId;

    bool IsEmpty() const noexcept
    {
        return producer.empty() && producerVersion.empty() && profileId.empty()
               && format.empty() && sourceId.empty();
    }

    MOTIONSOURCE_API friend bool operator==(
        const SourceProvenance& lhs, const SourceProvenance& rhs) noexcept;
    MOTIONSOURCE_API friend bool operator!=(
        const SourceProvenance& lhs, const SourceProvenance& rhs) noexcept;
};

} // namespace motionSource
