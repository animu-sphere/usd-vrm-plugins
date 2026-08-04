// SPDX-License-Identifier: Apache-2.0
//
// Provenance, and the one crossing into canonical motion.
//
// The question this file answers in code is the one the plan asked to have
// settled before a converter set its first field: whether a recorded file's
// provenance is `motion::MotionSourceMetadata`, a superset of it, or a
// neighbour. It is a neighbour, and the derivation below is one-way and
// narrowing -- so the test that matters most here is the one asserting that two
// files differing only in what the canonical type does not carry produce the
// same canonical value.
#include "motionSource/CanonicalMetadata.h"
#include "motionSource/SourceProvenance.h"

#include <cassert>
#include <cstdio>

namespace
{

using motionSource::CanonicalMetadata;
using motionSource::SourceProvenance;

SourceProvenance
MakeProvenance()
{
    SourceProvenance provenance;
    provenance.producer = "example.studio";
    provenance.producerVersion = "3.2.1";
    provenance.profileId = "example-studio-recording-default-v1";
    provenance.format = "test-fixture";
    provenance.sourceId = "arm-raise-turn";
    return provenance;
}

void
TestEquality()
{
    const SourceProvenance provenance = MakeProvenance();
    SourceProvenance other = MakeProvenance();
    assert(provenance == other);

    other.producer = "other.studio";
    assert(provenance != other);

    other = MakeProvenance();
    other.producerVersion = "3.2.2";
    assert(provenance != other);

    other = MakeProvenance();
    other.profileId.clear();
    assert(provenance != other);

    other = MakeProvenance();
    other.format = "other";
    assert(provenance != other);

    // Two conversions of the same motion out of two files differ here and
    // nowhere else.
    other = MakeProvenance();
    other.sourceId = "walk";
    assert(provenance != other);

    assert(SourceProvenance{}.IsEmpty());
    assert(!provenance.IsEmpty());
}

void
TestCanonicalMapping()
{
    const motion::MotionSourceMetadata metadata =
        CanonicalMetadata(MakeProvenance());

    // A recorded file is a clip by the time anything here sees it. `LiveCapture`
    // says values arrived over time from a running source, which is the property
    // the runtime's intake acts on, and a file has none of it.
    assert(metadata.kind == motion::MotionSourceKind::Clip);
    assert(metadata.provider == "example.studio");
    // `protocol` answers *how did these values arrive*, and for a recording that
    // is the format it was read from.
    assert(metadata.protocol == "test-fixture");
    assert(metadata.sourceId == "arm-raise-turn");
}

// The narrowing, made a fact rather than a sentence in a header: the producer
// version and the profile id have no home on the canonical type, so two files
// that differ in only those two convert to the same canonical metadata. A later
// change that quietly widened the mapping -- packing a profile id into
// `sourceId`, say -- fails here.
void
TestNarrowingIsDeliberate()
{
    SourceProvenance first = MakeProvenance();
    SourceProvenance second = MakeProvenance();
    second.producerVersion = "9.9.9";
    second.profileId = "example-studio-recording-professional-v2";
    assert(first != second);

    assert(CanonicalMetadata(first) == CanonicalMetadata(second));
}

void
TestEmptyProvenance()
{
    const motion::MotionSourceMetadata metadata =
        CanonicalMetadata(SourceProvenance{});
    assert(metadata.kind == motion::MotionSourceKind::Clip);
    assert(metadata.provider.empty());
    assert(metadata.protocol.empty());
    assert(metadata.sourceId.empty());
}

} // namespace

int
main()
{
    TestEquality();
    TestCanonicalMapping();
    TestNarrowingIsDeliberate();
    TestEmptyProvenance();
    std::printf("motionSource provenance: verified\n");
    return 0;
}
