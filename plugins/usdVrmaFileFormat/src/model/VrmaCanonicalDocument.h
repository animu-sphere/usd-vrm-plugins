// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <motionCore/Humanoid.h>

#include <string>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

// Parser-independent VRMA representation. Unlike the VRM importer's private
// document, its motion values are the public motionCore contract.
struct VrmaJoint {
    motion::HumanBone bone;
    std::string path;
    GfMatrix4d restTransform = GfMatrix4d(1.0);
    GfVec3f restTranslation = GfVec3f(0.0f);
    GfQuatf restRotation = GfQuatf(1.0f, GfVec3f(0.0f));
};

struct VrmaCanonicalDocument {
    std::string specVersion;
    std::string clipName;
    std::string rawExtensionJson;
    std::vector<VrmaJoint> joints;
    motion::HumanoidAnimation animation;
    std::vector<std::string> warnings;
};

PXR_NAMESPACE_CLOSE_SCOPE
