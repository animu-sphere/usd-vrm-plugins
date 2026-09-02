// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pxr/pxr.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <motionCore/Humanoid.h>

#include <optional>
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
// `HumanoidPose::expressions` is what a given instant reported.
//
// A clip can say one of three things about an expression it declares, and they
// are not the same statement (MOTION_CONTRACT.md, "Expression semantics"):
// a channel drives the node, or the node states a translation and no channel
// touches it -- a constant weight, since glTF leaves an un-animated node at its
// own TRS -- or the node states no translation at all, which is a weight the
// clip never gave and not a weight of zero.
struct VrmaExpression {
    std::string name;
    bool isPreset = true;

    // Whether any glTF channel drives this expression's node.
    bool isAnimated = false;

    // The weight the node states for the whole clip, when nothing animates it.
    // Empty when a channel drives the node, and empty when the node authored no
    // translation for the reader to read a weight out of.
    std::optional<float> constantWeight;
};

// The look-at block a clip declares, when it declares one.
//
// VRMA points look-at at a node whose position is the point the character
// watches, and states the offset from the source rig's head bone to where that
// gaze starts. The two halves travel differently and that is the decision here:
// the target is a value of an *instant*, so it rides on the poses beside the
// expression weights, while the offset is a measurement of the rig the clip was
// authored on -- it cannot vary within a clip, and paying for it per sample
// would buy nothing (MOTION_CONTRACT.md, "Look-at semantics").
//
// A clip says one of the same three things a declared expression can say: a
// channel drives the node, or the node states a position nothing animates --
// one target for the whole clip -- or it states none, which is a gaze the file
// never gave and not a gaze at the origin.
struct VrmaLookAt {
    bool present = false;

    // Source-rig measurement, in the clip's own space. Zero when the file
    // stated none, with a warning: an absent offset reads as a gaze starting at
    // the head bone itself, which is a claim worth making visible.
    GfVec3f offsetFromHeadBone = GfVec3f(0.0f);

    // Whether any glTF channel drives the look-at node.
    bool isAnimated = false;

    // The target the node states for the whole clip, when nothing animates it.
    // Empty when a channel drives the node, and empty when the node authored no
    // transform for a position to be read out of.
    std::optional<GfVec3f> constantTarget;
};

struct VrmaCanonicalDocument {
    std::string specVersion;
    std::string clipName;
    std::string rawExtensionJson;
    std::vector<VrmaJoint> joints;
    std::vector<VrmaExpression> expressions;
    VrmaLookAt lookAt;
    motion::HumanoidAnimation animation;
    std::vector<std::string> warnings;
};

PXR_NAMESPACE_CLOSE_SCOPE
