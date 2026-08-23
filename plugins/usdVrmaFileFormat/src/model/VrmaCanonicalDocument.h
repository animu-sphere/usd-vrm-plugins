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

// One expression the clip declares, in the order VRMA named it under
// `expressions.preset` and then `expressions.custom`.
//
// The declaration is separate from the weights on the poses because the two say
// different things: this is the set of expressions the clip is *about*, while
// `HumanoidPose::expressions` is what a given instant reported. A clip may
// declare an expression and animate nothing on it, and that is not a weight of
// zero (MOTION_CONTRACT.md, "Expression semantics").
struct VrmaExpression {
    std::string name;
    bool isPreset = true;

    // Whether any glTF channel drives this expression's node. False means the
    // clip declared the expression and reported no weight for it.
    bool isAnimated = false;
};

struct VrmaCanonicalDocument {
    std::string specVersion;
    std::string clipName;
    std::string rawExtensionJson;
    std::vector<VrmaJoint> joints;
    std::vector<VrmaExpression> expressions;
    motion::HumanoidAnimation animation;
    std::vector<std::string> warnings;
};

PXR_NAMESPACE_CLOSE_SCOPE
