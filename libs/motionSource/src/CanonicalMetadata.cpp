// SPDX-License-Identifier: Apache-2.0
#include "motionSource/CanonicalMetadata.h"

namespace motionSource
{

openstrata::motion::SourceMetadata
CanonicalMetadata(const SourceProvenance& provenance)
{
    openstrata::motion::SourceMetadata metadata;
    metadata.kind = openstrata::motion::MotionSourceKind::Clip;
    metadata.provider = provenance.producer;
    metadata.protocol = provenance.format;
    metadata.sourceId = provenance.sourceId;
    return metadata;
}

} // namespace motionSource
