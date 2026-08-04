// SPDX-License-Identifier: Apache-2.0
#include "motionSource/CanonicalMetadata.h"

namespace motionSource
{

motion::MotionSourceMetadata
CanonicalMetadata(const SourceProvenance& provenance)
{
    motion::MotionSourceMetadata metadata;
    metadata.kind = motion::MotionSourceKind::Clip;
    metadata.provider = provenance.producer;
    metadata.protocol = provenance.format;
    metadata.sourceId = provenance.sourceId;
    return metadata;
}

} // namespace motionSource
