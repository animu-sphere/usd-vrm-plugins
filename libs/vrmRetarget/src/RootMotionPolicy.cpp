// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/RootMotionPolicy.h"

namespace vrmRetarget
{

pxr::GfVec3f
ResolveRootTranslation(const RootMotionOptions& options,
                       const pxr::GfVec3f& sourceTranslation,
                       const pxr::GfVec3f& sourceRestTranslation,
                       const pxr::GfVec3f& targetRestTranslation)
{
    if (options.mode == RootMotionMode::Ignore) {
        return targetRestTranslation;
    }

    pxr::GfVec3f delta =
        (sourceTranslation - sourceRestTranslation) * options.translationScale;
    if (options.preserveTargetHeight) {
        delta[1] = 0.0f;
    }
    return targetRestTranslation + delta;
}

} // namespace vrmRetarget
