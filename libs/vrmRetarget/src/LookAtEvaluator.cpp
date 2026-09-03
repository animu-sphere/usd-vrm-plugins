// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/LookAtEvaluator.h"

#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vrmRetarget
{

namespace
{

constexpr float kPi = 3.14159265358979324f;
constexpr float kRadiansToDegrees = 180.0f / kPi;
constexpr float kDegreesToRadians = kPi / 180.0f;

// The VRM 1.0 expression presets a gaze drives, named from the character's own
// point of view: `lookLeft` is the character looking to *its* left.
constexpr const char* kLookLeft = "lookLeft";
constexpr const char* kLookRight = "lookRight";
constexpr const char* kLookUp = "lookUp";
constexpr const char* kLookDown = "lookDown";

// Warnings keep first-appearance order and each distinct one appears once, so a
// rig defect a thousand samples ran into reads as one fact about the rig. The
// same rule -- and the same reason -- as ExpressionResolver's report.
void
RecordWarning(LookAtDiagnostics* diagnostics, std::string warning)
{
    if (!diagnostics) {
        return;
    }
    std::vector<std::string>& warnings = diagnostics->warnings;
    if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
        warnings.push_back(std::move(warning));
    }
}

// A cubic Hermite through the stated keys. `t` is already inside [0, 1].
//
// An empty curve is the linear map, which is what VRM 1.0 states and what a
// VRM 0.x file's default `[0,0,0,1, 1,1,1,0]` states too: the Hermite basis over
// one unit segment with both tangents 1 reduces algebraically to `t`. So the two
// spellings are one map rather than two that happen to agree, and the reduction
// is what the parse test measures.
float
EvaluateCurve(const std::vector<LookAtCurveKey>& keys, float t)
{
    if (keys.empty()) {
        return t;
    }
    if (keys.size() == 1) {
        return keys.front().value;
    }
    if (t <= keys.front().time) {
        return keys.front().value;
    }
    if (t >= keys.back().time) {
        return keys.back().value;
    }
    std::size_t index = 0;
    while (index + 2 < keys.size() && t >= keys[index + 1].time) {
        ++index;
    }
    const LookAtCurveKey& a = keys[index];
    const LookAtCurveKey& b = keys[index + 1];
    const float span = b.time - a.time;
    if (!(span > 0.0f)) {
        // Two keys at one time state a step. Taking the later value is the only
        // reading that does not divide by the span.
        return b.value;
    }
    const float u = (t - a.time) / span;
    const float u2 = u * u;
    const float u3 = u2 * u;
    return (2.0f * u3 - 3.0f * u2 + 1.0f) * a.value
        + (u3 - 2.0f * u2 + u) * span * a.outTangent
        + (-2.0f * u3 + 3.0f * u2) * b.value
        + (u3 - u2) * span * b.inTangent;
}

pxr::GfQuatf
AxisRotation(const pxr::GfVec3f& axis, float degrees)
{
    const float radians = degrees * kDegreesToRadians;
    return pxr::GfQuatf(std::cos(radians * 0.5f),
                        axis * std::sin(radians * 0.5f));
}

// The eye's rotation for one resolved pair of angles, in the head's own space.
//
// Yaw about +Y then pitch about +X, and the pitch is negated because a positive
// right-handed rotation about +X takes the forward axis toward -Y: "looking up"
// is a negative rotation there. Composed in this order, an identity range map
// reproduces the aim direction it was given, which is what the round-trip test
// measures rather than asserts -- and it fails on either half of this being
// wrong, which is the only reason the two conventions above are trustworthy.
pxr::GfQuatf
EyeRotation(float yawDegrees, float pitchDegrees)
{
    return AxisRotation(pxr::GfVec3f(0.0f, 1.0f, 0.0f), yawDegrees)
        * AxisRotation(pxr::GfVec3f(1.0f, 0.0f, 0.0f), -pitchDegrees);
}

float
ClampUnit(float weight)
{
    if (std::isnan(weight)) {
        return 0.0f;
    }
    return std::min(1.0f, std::max(0.0f, weight));
}

bool
IsOutsideUnitRange(float weight)
{
    // Written as the negation for the reason ExpressionResolver states: every
    // comparison against NaN is false, so `w < 0 || w > 1` calls a NaN
    // in-range.
    return !(weight >= 0.0f && weight <= 1.0f);
}

bool
SameMap(const LookAtRangeMap& a, const LookAtRangeMap& b)
{
    if (a.inputMaxValue != b.inputMaxValue || a.outputScale != b.outputScale
        || a.curve.size() != b.curve.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.curve.size(); ++i) {
        if (a.curve[i].time != b.curve[i].time
            || a.curve[i].value != b.curve[i].value
            || a.curve[i].inTangent != b.curve[i].inTangent
            || a.curve[i].outTangent != b.curve[i].outTangent) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Raw-JSON reading
// ---------------------------------------------------------------------------

const pxr::JsValue*
Find(const pxr::JsObject& object, const char* key)
{
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

// A JSON number, whichever of the three ways it was written. A file is free to
// spell 90 as an integer, and a reader that only accepted a real would silently
// drop the range it states.
bool
AsFloat(const pxr::JsValue* value, float* out)
{
    if (!value) {
        return false;
    }
    if (value->IsReal()) {
        *out = static_cast<float>(value->GetReal());
        return true;
    }
    if (value->IsInt()) {
        *out = static_cast<float>(value->GetInt());
        return true;
    }
    if (value->IsUInt64()) {
        *out = static_cast<float>(value->GetUInt64());
        return true;
    }
    return false;
}

bool
AsVec3(const pxr::JsValue* value, pxr::GfVec3f* out)
{
    if (!value || !value->IsArray()) {
        return false;
    }
    const pxr::JsArray& array = value->GetJsArray();
    if (array.size() != 3) {
        return false;
    }
    pxr::GfVec3f parsed(0.0f);
    for (std::size_t i = 0; i < 3; ++i) {
        if (!AsFloat(&array[i], &parsed[i])) {
            return false;
        }
    }
    *out = parsed;
    return true;
}

// VRM 1.0: `{"inputMaxValue": <deg>, "outputScale": <deg or weight>}`.
void
ReadRangeMap1(const pxr::JsObject& block, const char* key, LookAtRangeMap* map,
              std::vector<std::string>* warnings)
{
    const pxr::JsValue* value = Find(block, key);
    if (!value || !value->IsObject()) {
        return;
    }
    const pxr::JsObject& object = value->GetJsObject();
    float number = 0.0f;
    if (AsFloat(Find(object, "inputMaxValue"), &number)) {
        map->inputMaxValue = number;
    }
    if (AsFloat(Find(object, "outputScale"), &number)) {
        map->outputScale = number;
    }
    // A 1.0 range map is linear by definition, so any curve a 0.x block left in
    // the same rig would be read against a range it was not authored for.
    map->curve.clear();
    if (!(map->inputMaxValue > 0.0f) && warnings) {
        warnings->push_back(std::string("look-at range map '") + key
                            + "' states an inputMaxValue that is not positive; "
                              "it maps every angle to zero");
    }
}

// VRM 0.x: `{"xRange": <deg>, "yRange": <deg or weight>, "curve": [t, v, in,
// out, ...]}`. The curve is Unity's serialized AnimationCurve -- four floats
// per key -- and the default `[0,0,0,1, 1,1,1,0]` is the linear map.
void
ReadRangeMap0(const pxr::JsObject& block, const char* key, LookAtRangeMap* map,
              std::vector<std::string>* warnings)
{
    const pxr::JsValue* value = Find(block, key);
    if (!value || !value->IsObject()) {
        return;
    }
    const pxr::JsObject& object = value->GetJsObject();
    float number = 0.0f;
    if (AsFloat(Find(object, "xRange"), &number)) {
        map->inputMaxValue = number;
    }
    if (AsFloat(Find(object, "yRange"), &number)) {
        map->outputScale = number;
    }
    map->curve.clear();
    if (const pxr::JsValue* curve = Find(object, "curve")) {
        if (curve->IsArray()) {
            const pxr::JsArray& array = curve->GetJsArray();
            if (array.size() % 4 != 0) {
                if (warnings) {
                    warnings->push_back(
                        std::string("look-at curve '") + key
                        + "' is not a whole number of four-float keys; it is "
                          "read as the linear map instead");
                }
            } else {
                bool complete = true;
                for (std::size_t i = 0; i + 3 < array.size(); i += 4) {
                    LookAtCurveKey point;
                    complete = AsFloat(&array[i], &point.time)
                        && AsFloat(&array[i + 1], &point.value)
                        && AsFloat(&array[i + 2], &point.inTangent)
                        && AsFloat(&array[i + 3], &point.outTangent);
                    if (!complete) {
                        break;
                    }
                    map->curve.push_back(point);
                }
                if (!complete) {
                    map->curve.clear();
                    if (warnings) {
                        warnings->push_back(
                            std::string("look-at curve '") + key
                            + "' holds a value that is not a number; it is "
                              "read as the linear map instead");
                    }
                }
            }
        }
    }
    if (!(map->inputMaxValue > 0.0f) && warnings) {
        warnings->push_back(std::string("look-at range map '") + key
                            + "' states an xRange that is not positive; it "
                              "maps every angle to zero");
    }
}

} // namespace

float
LookAtRangeMap::Map(float inputDegrees) const
{
    if (!(inputMaxValue > 0.0f)) {
        return 0.0f;
    }
    // The negation catches a NaN as well as a negative, and a magnitude is what
    // this takes: the sign of the angle has already chosen which map runs.
    float input = !(inputDegrees > 0.0f) ? 0.0f : inputDegrees;
    if (input > inputMaxValue) {
        input = inputMaxValue;
    }
    return EvaluateCurve(curve, input / inputMaxValue) * outputScale;
}

bool
ParseLookAtRangeMaps(const std::string& rawJson, LookAtRig* rig,
                     std::vector<std::string>* warnings)
{
    if (!rig || rawJson.empty()) {
        return false;
    }
    pxr::JsParseError error;
    const pxr::JsValue parsed = pxr::JsParseString(rawJson, &error);
    if (parsed.IsNull() || !parsed.IsObject()) {
        if (warnings) {
            warnings->push_back(
                "the avatar's preserved look-at block is not a JSON object; "
                "its range maps keep their defaults");
        }
        return false;
    }
    const pxr::JsObject& block = parsed.GetJsObject();

    // VRM 1.0 and VRM 0.x are told apart by the keys they use rather than by a
    // version this block does not carry. The importer preserves the 1.0
    // `lookAt` object whole and, for 0.x, only the `lookAt*` keys of
    // `firstPerson` -- so the two key families never appear together in a file
    // this reader is given, and a file that somehow held both would be read as
    // 1.0, which is the newer contract.
    const bool isVrm1 = Find(block, "rangeMapHorizontalInner")
        || Find(block, "rangeMapHorizontalOuter")
        || Find(block, "rangeMapVerticalDown") || Find(block, "rangeMapVerticalUp")
        || Find(block, "offsetFromHeadBone");

    if (isVrm1) {
        ReadRangeMap1(block, "rangeMapHorizontalInner", &rig->horizontalInner,
                      warnings);
        ReadRangeMap1(block, "rangeMapHorizontalOuter", &rig->horizontalOuter,
                      warnings);
        ReadRangeMap1(block, "rangeMapVerticalDown", &rig->verticalDown,
                      warnings);
        ReadRangeMap1(block, "rangeMapVerticalUp", &rig->verticalUp, warnings);

        pxr::GfVec3f offset(0.0f);
        if (AsVec3(Find(block, "offsetFromHeadBone"), &offset)) {
            rig->offsetFromHeadBone = offset;
        }
        if (const pxr::JsValue* type = Find(block, "type")) {
            if (type->IsString() && type->GetString() == "expression") {
                rig->type = LookAtType::Expression;
            } else if (type->IsString() && type->GetString() == "bone") {
                rig->type = LookAtType::Bone;
            }
        }
        return true;
    }

    ReadRangeMap0(block, "lookAtHorizontalInner", &rig->horizontalInner,
                  warnings);
    ReadRangeMap0(block, "lookAtHorizontalOuter", &rig->horizontalOuter,
                  warnings);
    ReadRangeMap0(block, "lookAtVerticalDown", &rig->verticalDown, warnings);
    ReadRangeMap0(block, "lookAtVerticalUp", &rig->verticalUp, warnings);
    if (const pxr::JsValue* type = Find(block, "lookAtTypeName")) {
        if (type->IsString()) {
            // 0.x spells the two types "Bone" and "BlendShape"; the second is
            // the same rig 1.0 calls `expression`, which is the name the rest of
            // this library uses.
            if (type->GetString() == "BlendShape") {
                rig->type = LookAtType::Expression;
            } else if (type->GetString() == "Bone") {
                rig->type = LookAtType::Bone;
            }
        }
    }
    // A 0.x look-at block states no offset from the head bone at all -- the
    // nearest thing, `firstPersonBoneOffset`, is a first-person camera
    // placement rather than an eye origin, and is not preserved. So a 0.x rig
    // arrives with none, deliberately, and the evaluator's fallback is what
    // answers for it.
    return true;
}

LookAtEvaluator::LookAtEvaluator(LookAtRig rig, LookAtEvaluateOptions options)
    : _rig(std::move(rig))
    , _options(options)
{
}

bool
LookAtEvaluator::Aim(const LookAtInput& input, float* yawDegrees,
                     float* pitchDegrees, LookAtDiagnostics* diagnostics) const
{
    if (!input.target) {
        return false;
    }

    pxr::GfVec3f offset(0.0f);
    if (_rig.offsetFromHeadBone) {
        offset = *_rig.offsetFromHeadBone;
    } else if (_options.clipOffsetFromHeadBone) {
        offset = *_options.clipOffsetFromHeadBone;
        RecordWarning(diagnostics,
                      "this avatar states no offsetFromHeadBone, so the gaze "
                      "starts at the offset the source clip measured on its "
                      "own rig; the two rigs' eye heights are assumed equal");
    } else {
        RecordWarning(diagnostics,
                      "neither this avatar nor the source clip states an "
                      "offsetFromHeadBone, so the gaze starts at the head "
                      "joint itself rather than at the eyes");
    }

    // Normalizing costs one square root a sample and buys the guarantee the
    // rest of this depends on: an orientation that is not unit-length rotates
    // and *scales*, so an un-normalized head would move the eye origin.
    const pxr::GfQuatf orientation = input.head.orientation.GetNormalized();
    const pxr::GfVec3f origin
        = input.head.position + orientation.Transform(offset);
    const pxr::GfVec3f local
        = orientation.GetInverse().Transform(*input.target - origin);

    const float length = local.GetLength();
    if (!(length >= _options.minimumGazeDistance)) {
        // A target at the eyes has no direction, and one a micrometre away has
        // a direction that is numerically meaningless. Both are answered as no
        // gaze rather than as a forward one, for the reason an absent target
        // is: this layer does not invent a direction nobody named.
        RecordWarning(diagnostics,
                      "a look-at target sits on the eye origin, so it names no "
                      "direction; those samples resolve to no gaze");
        return false;
    }

    if (yawDegrees) {
        // +X is the character's own left in the glTF basis VRM inherits, and
        // +Z is forward, so this is positive when the character looks left.
        *yawDegrees = std::atan2(local[0], local[2]) * kRadiansToDegrees;
    }
    if (pitchDegrees) {
        const float horizontal
            = std::sqrt(local[0] * local[0] + local[2] * local[2]);
        *pitchDegrees = std::atan2(local[1], horizontal) * kRadiansToDegrees;
    }
    return true;
}

ResolvedLookAt
LookAtEvaluator::Evaluate(const LookAtInput& input,
                          LookAtDiagnostics* diagnostics) const
{
    ResolvedLookAt result;
    result.timestamp = input.timestamp;
    if (diagnostics) {
        ++diagnostics->samplesEvaluated;
    }

    float yaw = 0.0f;
    float pitch = 0.0f;
    if (!Aim(input, &yaw, &pitch, diagnostics)) {
        if (diagnostics) {
            ++diagnostics->samplesWithoutTarget;
        }
        return result;
    }

    result.hasGaze = true;
    result.yawDegrees = yaw;
    result.pitchDegrees = pitch;

    const float horizontalMagnitude = std::fabs(yaw);
    const bool toTheLeft = yaw >= 0.0f;
    const bool upward = pitch >= 0.0f;
    const float vertical = upward ? _rig.verticalUp.Map(pitch)
                                  : -_rig.verticalDown.Map(-pitch);

    if (_rig.type == LookAtType::Bone) {
        const float inner = _rig.horizontalInner.Map(horizontalMagnitude);
        const float outer = _rig.horizontalOuter.Map(horizontalMagnitude);
        // The eye on the side the gaze goes to turns outward, away from the
        // nose; the other turns inward. That is the whole of what the split
        // between the two horizontal maps is for.
        const float left = toTheLeft ? outer : -inner;
        const float right = toTheLeft ? inner : -outer;

        if (!_rig.leftEyeJoint.empty()) {
            result.eyeRotations.push_back(
                {_rig.leftEyeJoint, EyeRotation(left, vertical)});
        }
        if (!_rig.rightEyeJoint.empty()) {
            result.eyeRotations.push_back(
                {_rig.rightEyeJoint, EyeRotation(right, vertical)});
        }
        if (result.eyeRotations.empty()) {
            RecordWarning(diagnostics,
                          "this avatar's look-at is bone-driven and names no "
                          "eye joint, so its gaze resolves to no rotation");
        } else if (_rig.leftEyeJoint.empty() || _rig.rightEyeJoint.empty()) {
            RecordWarning(diagnostics,
                          "this avatar's look-at names one eye joint and not "
                          "the other; only the named eye is driven");
        }
        return result;
    }

    // An expression drives both eyes with one weight, so there is no inner eye
    // to distinguish and the horizontal curve is the outer one. The inner map
    // is unreachable for this rig type, which is worth saying out loud when the
    // rig bothered to state a different one.
    if (!SameMap(_rig.horizontalInner, _rig.horizontalOuter)) {
        RecordWarning(diagnostics,
                      "this avatar's look-at is expression-driven and states a "
                      "horizontal inner map different from its outer one; one "
                      "weight drives both eyes, so only the outer map is read");
    }
    const float horizontal = _rig.horizontalOuter.Map(horizontalMagnitude);

    // All four names, every sample, including the two this gaze drives to zero
    // -- otherwise a swing to the left leaves the previous sample's `lookRight`
    // standing on the rig.
    const std::pair<const char*, float> weights[] = {
        {kLookLeft, toTheLeft ? horizontal : 0.0f},
        {kLookRight, toTheLeft ? 0.0f : horizontal},
        {kLookUp, upward ? vertical : 0.0f},
        {kLookDown, upward ? 0.0f : -vertical},
    };
    for (const auto& entry : weights) {
        float weight = entry.second;
        if (IsOutsideUnitRange(weight)) {
            if (_options.clampExpressionWeights) {
                weight = ClampUnit(weight);
                RecordWarning(
                    diagnostics,
                    std::string("look-at expression '") + entry.first
                        + "' resolves outside [0, 1] and is clamped; a VRM 0.x "
                          "BlendShape rig states its weight range in the same "
                          "key a Bone rig states degrees in");
            } else {
                RecordWarning(
                    diagnostics,
                    std::string("look-at expression '") + entry.first
                        + "' resolves outside [0, 1] and clamping is off; it "
                          "is carried to the binds unchanged");
            }
        }
        result.expressions.Set(entry.first, weight);
    }
    return result;
}

ResolvedLookAt
LookAtEvaluator::Evaluate(const motion::HumanoidPose& pose,
                          const LookAtHead& head,
                          LookAtDiagnostics* diagnostics) const
{
    LookAtInput input;
    input.timestamp = pose.timestamp;
    input.target = pose.lookAtTarget;
    input.head = head;
    return Evaluate(input, diagnostics);
}

} // namespace vrmRetarget
