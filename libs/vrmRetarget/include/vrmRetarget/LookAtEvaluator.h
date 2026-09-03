// SPDX-License-Identifier: Apache-2.0
//
// Turns the place a clip looks at into the way one rig's eyes look at it.
//
// A producer reports a target *point* (motionCore's `HumanoidPose::lookAtTarget`)
// because a direction is only meaningful next to a head, and where the head is
// -- and how far the eyes sit from it -- belongs to an avatar rather than to a
// clip. This is the layer that has the avatar, so this is where a point becomes
// either a pair of eye rotations or a set of named expression weights, which is
// exactly the division `ExpressionResolver` is under.
//
// Like the rest of vrmRetarget this takes plain values: the caller reads the
// avatar's `/Asset/rig/LookAt` prim off the stage -- its `vrm:type`, its eye
// joint tokens and the range-map curves the importer preserved verbatim under
// `vrm:lookAt:raw` -- and hands them in. `ParseLookAtRangeMaps` below is the
// one place that reads that raw JSON, and it reads a string rather than a
// stage, because a VRM 0.x rig and a VRM 1.0 rig state the same four curves in
// two different shapes and neither caller should have to know both.
//
// An expression-type rig resolves to `motion::ExpressionWeights` on purpose:
// that is precisely the value `ExpressionResolver` consumes, so a caller pipes
// this into that and the gaze reaches the same binds the face already does,
// with no second path into a rig.
#pragma once

#include "vrmRetarget/api.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace vrmRetarget
{

// One key of a VRM 0.x look-at curve, in the normalized space the file states
// it in: `time` is the input angle over its range and `value` is the output
// over its own, both conventionally in [0, 1].
struct LookAtCurveKey
{
    float time = 0.0f;
    float value = 0.0f;
    float inTangent = 0.0f;
    float outTangent = 0.0f;
};

// One of the four angle-to-output curves a VRM look-at rig states.
//
// VRM 1.0 spells it as a range map -- `inputMaxValue` degrees of gaze map
// linearly onto `outputScale` -- and VRM 0.x spells the same thing as
// `xRange` / `yRange` plus an editable curve. They are one value here, with the
// 0.x curve empty for a 1.0 rig, because a consumer that had to branch on the
// source version would be carrying the importer's job into the retarget layer.
//
// What `outputScale` *means* is the rig's type, not this struct's: degrees of
// eye rotation for a `bone` rig, an expression weight for an `expression` one.
struct LookAtRangeMap
{
    // The gaze angle, in degrees, at which the output reaches its full scale.
    // Input past it is clamped rather than extrapolated -- the eye stops.
    float inputMaxValue = 90.0f;

    // The output at (and past) `inputMaxValue`.
    float outputScale = 10.0f;

    // VRM 0.x's curve, empty for the linear map VRM 1.0 states. Evaluated as a
    // cubic Hermite between consecutive keys, and the 0.x default of
    // `[0,0,0,1, 1,1,1,0]` is that basis reduced to `t` -- so a 0.x rig that
    // never touched its curves and a 1.0 rig are the same map, not two.
    std::vector<LookAtCurveKey> curve;

    // The output for `inputDegrees`, which is a magnitude: the caller has
    // already chosen which of the four maps the sign of the angle selects.
    // A map whose `inputMaxValue` is not positive maps everything to 0 and is
    // reported by the evaluator rather than dividing by it here.
    VRMRETARGET_API float Map(float inputDegrees) const;
};

enum class LookAtType
{
    // `vrm:type == "bone"`: the eye joints carry the gaze.
    Bone,
    // `vrm:type == "expression"`: four named expressions carry it instead, and
    // the eye joints are not rotated.
    Expression,
};

// One avatar's look-at configuration, in the value form this library resolves
// against.
struct LookAtRig
{
    LookAtType type = LookAtType::Bone;

    // The caller's identifiers for the eye joints -- in practice the
    // `vrm:leftEye` / `vrm:rightEye` joint path tokens, which are tokens of the
    // skeleton's `joints` array and not prim paths. Opaque here, and carried
    // back out on the rotations so the caller can author them without a second
    // lookup. Empty for a rig that names no such eye, which an
    // `expression`-type rig legitimately is.
    std::string leftEyeJoint;
    std::string rightEyeJoint;

    // Where this avatar's gaze starts, relative to its head bone and in the
    // head's own space. VRM 1.0 states it; VRM 0.x has no equivalent inside the
    // look-at block, so a 0.x rig arrives with none and the fallback below
    // applies.
    std::optional<pxr::GfVec3f> offsetFromHeadBone;

    // Horizontal is split per eye because two eyes converge: the eye on the
    // side the gaze goes to rotates *outward*, away from the nose, and the
    // other rotates *inward*. Vertical is not split, because both eyes rise and
    // fall together.
    LookAtRangeMap horizontalInner;
    LookAtRangeMap horizontalOuter;
    LookAtRangeMap verticalDown;
    LookAtRangeMap verticalUp;
};

// Fills `rig`'s four range maps -- and, for a VRM 1.0 rig, its type and its
// `offsetFromHeadBone` -- from the JSON the importer preserved verbatim under
// `/Asset/rig/LookAt`'s `vrm:lookAt:raw` custom data.
//
// Both source shapes are accepted and produce the same value:
//
//   VRM 1.0   {"type", "offsetFromHeadBone", "rangeMapHorizontalInner": {
//              "inputMaxValue", "outputScale"}, ...}
//   VRM 0.x   {"lookAtTypeName", "lookAtHorizontalInner": {
//              "curve", "xRange", "yRange"}, ...}
//
// The block also states the rig's type, and this sets it -- 0.x's "BlendShape"
// included, which is the rig 1.0 calls `expression`. `vrm:type` is authored as
// its own attribute as well, so a caller that has both should parse first and
// apply the attribute over the result: the attribute is what the importer
// normalized and the raw block is what the source file happened to say.
// Returns false, having changed nothing, when the string is not an object --
// which includes the empty string a rig with no preserved curves carries. A map
// the JSON does not mention keeps the value it already had, so an incomplete
// block leaves the defaults standing rather than zeroing a curve to nothing.
VRMRETARGET_API bool ParseLookAtRangeMaps(const std::string& rawJson,
                                          LookAtRig* rig,
                                          std::vector<std::string>* warnings
                                              = nullptr);

// Where the target avatar's head is for one sample, in the same space as the
// clip's target point. The caller computes it: on a bake that is the
// retargeted skeleton's own head transform at that time.
struct LookAtHead
{
    // The head joint's orientation in that space. The offset from the head bone
    // is stated in the head's own space, so it is this that places the eyes.
    pxr::GfQuatf orientation = pxr::GfQuatf(1.0f);

    pxr::GfVec3f position = pxr::GfVec3f(0.0f);
};

// One sample's look-at question.
struct LookAtInput
{
    double timestamp = 0.0;

    // Optional for the reason `HumanoidPose::lookAtTarget` is: the origin is a
    // place a producer can legitimately look at, so "reported no target" cannot
    // be spelled as a value of the target.
    std::optional<pxr::GfVec3f> target;

    LookAtHead head;
};

// One eye's resolved rotation.
struct LookAtEyeRotation
{
    // The joint identifier the rig carried, handed straight back.
    std::string joint;

    // The rotation to apply to that eye joint, in the head's space -- which for
    // a VRM rig is the eye joint's own parent space, since the eye bones are
    // children of the head and rest looking forward. Composed as
    // `yaw about +Y` then `pitch about +X` negated, so an identity range map
    // reproduces the aim direction it was given.
    pxr::GfQuatf rotation = pxr::GfQuatf(1.0f);
};

// What one sample's target becomes on this rig.
struct ResolvedLookAt
{
    double timestamp = 0.0;

    // False when the sample named no target, or named one this rig cannot turn
    // into a direction. Both vectors are then empty, and that is the same rule
    // an unreported expression name is under: a gaze the clip never gave is not
    // a gaze straight ahead, so nothing is authored rather than a forward one.
    bool hasGaze = false;

    // The aim in the head's own space, before any range map: `yaw` positive
    // toward the character's own left (+X in the VRM/glTF basis, where the
    // avatar faces +Z), `pitch` positive up. Carried out because it is the
    // measurement an operator judging a retarget actually reads, and because it
    // is the one value both rig types share.
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;

    // Bone rigs. Left before right, and an eye the rig did not name is absent
    // rather than present with an identity rotation.
    std::vector<LookAtEyeRotation> eyeRotations;

    // Expression rigs, as the four VRM preset names `lookLeft`, `lookRight`,
    // `lookUp` and `lookDown`.
    //
    // All four are always reported when there is a gaze, including the two the
    // sample drives to 0. That is the rule `ExpressionResolver` states for a
    // reported zero, and it is load-bearing here for the same reason: a gaze
    // that swings left after a sample that looked right has to *say* that
    // `lookRight` is now 0, or the previous sample's weight stands on the rig.
    motion::ExpressionWeights expressions;
};

struct LookAtDiagnostics
{
    // First-appearance order, each distinct warning once however many samples
    // ran into it -- a clip is thousands of samples and a rig defect is one
    // fact about the rig, not one per frame.
    std::vector<std::string> warnings;

    // How the clip's gaze track divided. A clip whose every sample lands in
    // `samplesWithoutTarget` produced no gaze at all, which reads as a clean
    // resolve in the warnings alone.
    std::size_t samplesEvaluated = 0;
    std::size_t samplesWithoutTarget = 0;

    bool IsClean() const { return warnings.empty(); }
};

struct LookAtEvaluateOptions
{
    // The `vrm:lookAtOffsetFromHeadBone` the *source clip* stated, which is a
    // measurement of the rig the clip was authored on rather than of this
    // avatar. Consulted only when the avatar states none of its own -- a VRM
    // 0.x rig, or a stage authored before the importer preserved one -- and
    // reported when it is, because approximating this avatar's eye height with
    // another rig's is a substitution an operator should see rather than a
    // default.
    std::optional<pxr::GfVec3f> clipOffsetFromHeadBone;

    // Clamp an expression-type rig's resolved weights into [0, 1]. A VRM 1.0
    // rig states an `outputScale` that is already a weight, but a VRM 0.x
    // `BlendShape`-type rig states a `yRange` this layer cannot distinguish
    // from the degrees a `Bone` rig states there -- the two are the same key in
    // the same shape. So a weight outside the range is clamped and *named*,
    // rather than being rescaled by a factor guessed from the rig's type.
    bool clampExpressionWeights = true;

    // Treat a gaze shorter than this as no direction at all. A target at the
    // eye's own position has no direction to normalize, and a target a
    // micrometre away has one that is numerically meaningless; both are the
    // same defect and are answered the same way, with `hasGaze` false.
    float minimumGazeDistance = 1e-5f;
};

// Evaluates a clip's gaze against one avatar's look-at configuration.
class VRMRETARGET_API LookAtEvaluator
{
public:
    explicit LookAtEvaluator(LookAtRig rig,
                             LookAtEvaluateOptions options
                                 = LookAtEvaluateOptions());

    const LookAtRig& GetRig() const noexcept { return _rig; }
    const LookAtEvaluateOptions& GetOptions() const noexcept
    {
        return _options;
    }

    // The gaze angles alone, with no range map applied: the direction from this
    // avatar's eyes to the target, in the head's own space. Returns false --
    // leaving both outputs untouched -- when the sample named no target or the
    // target is not a direction. Split out because it is the half that has no
    // rig in it, so a caller comparing two rigs against one clip measures the
    // aim once.
    bool Aim(const LookAtInput& input, float* yawDegrees, float* pitchDegrees,
             LookAtDiagnostics* diagnostics = nullptr) const;

    // Resolves one sample. `diagnostics` may be null; when it is not it
    // accumulates across calls, so a whole clip's report is one object.
    ResolvedLookAt Evaluate(const LookAtInput& input,
                            LookAtDiagnostics* diagnostics = nullptr) const;

    // The same, taking the target and the timestamp off a pose -- the call a
    // consumer walking a retargeted clip actually makes, with `head` the head
    // transform that clip's own body produced at that sample.
    ResolvedLookAt Evaluate(const motion::HumanoidPose& pose,
                            const LookAtHead& head,
                            LookAtDiagnostics* diagnostics = nullptr) const;

private:
    LookAtRig _rig;
    LookAtEvaluateOptions _options;
};

} // namespace vrmRetarget
