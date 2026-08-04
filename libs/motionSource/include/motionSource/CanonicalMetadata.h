// SPDX-License-Identifier: Apache-2.0
//
// The one place this library crosses into canonical motion, and the only header
// here that includes `motionCore`.
//
// Everything else in `motionSource` is expressed in the source's own terms and
// links no OpenUSD value type of its own (SourceSkeleton.h says why). The
// declared `motionSource -> motionCore` edge (WORKSPACE.md §2) exists for this
// crossing and for the converter that follows it, and keeping the crossing in
// one file is what lets `motionSource_boundaries` check the claim rather than
// leave it as a habit.
#pragma once

#include "motionSource/SourceProvenance.h"
#include "motionSource/api.h"

#include "motionCore/Humanoid.h"

namespace motionSource
{

// The canonical metadata a clip converted from `provenance` carries.
//
// The mapping is narrowing on purpose, and three of its four decisions are worth
// stating because a later reader will otherwise re-litigate them:
//
// *`kind` is always `Clip`.* A recorded file is a clip by the time anything here
// sees it, whatever the sensors that produced it were doing. `LiveCapture` says
// values arrived over time from a running source, which is the property
// `motionRuntime`'s intake acts on, and a file has none of it.
//
// *`protocol` carries the format.* The field answers *how did these values
// arrive*, and for a recording that is the format it was read from. Leaving it
// empty would have made a converted clip indistinguishable from one with no
// stated origin; putting the format in `provider` would have overwritten the
// producer, which is the more specific fact.
//
// *`producerVersion` and `profileId` are dropped, and that is the narrowing.*
// Neither has a home on the canonical type and neither should get one: they
// cannot vary within a clip, and `MotionSourceMetadata` is carried per sample.
// They survive beside the motion — in the semantic clip's authored metadata,
// where this repository's other clip readers already record the file facts of
// their own sources — and a caller that needs them keeps the `SourceProvenance`
// it converted from.
MOTIONSOURCE_API motion::MotionSourceMetadata CanonicalMetadata(
    const SourceProvenance& provenance);

} // namespace motionSource
