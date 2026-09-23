// SPDX-License-Identifier: Apache-2.0
#include "vrmRig/ExpressionResolver.h"
#include "vrmRig/LookAtEvaluator.h"
#include "vrmRig/RequiredBones.h"

#include "pxr/base/gf/quatd.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace
{

constexpr float kEpsilon = 1e-4f;

bool
NearlyEqual(float a, float b)
{
    return std::fabs(a - b) <= kEpsilon;
}

bool
NearlyEqual(const pxr::GfVec3f& a, const pxr::GfVec3f& b)
{
    return NearlyEqual(a[0], b[0]) && NearlyEqual(a[1], b[1]) && NearlyEqual(a[2], b[2]);
}

bool
NearlyEqual(const pxr::GfVec4f& a, const pxr::GfVec4f& b)
{
    return NearlyEqual(a[0], b[0]) && NearlyEqual(a[1], b[1]) && NearlyEqual(a[2], b[2]) &&
           NearlyEqual(a[3], b[3]);
}

// Compares orientations, not representations: q and -q are the same rotation.
bool
SameOrientation(const pxr::GfQuatf& a, const pxr::GfQuatf& b)
{
    const pxr::GfQuatf na = a.GetNormalized();
    pxr::GfQuatf nb = b.GetNormalized();
    if (pxr::GfDot(na, nb) < 0.0f)
    {
        nb = pxr::GfQuatf(-nb.GetReal(), -nb.GetImaginary());
    }
    return NearlyEqual(na.GetReal(), nb.GetReal()) &&
           NearlyEqual(na.GetImaginary()[0], nb.GetImaginary()[0]) &&
           NearlyEqual(na.GetImaginary()[1], nb.GetImaginary()[1]) &&
           NearlyEqual(na.GetImaginary()[2], nb.GetImaginary()[2]);
}

pxr::GfQuatf
Rotation(const pxr::GfVec3f& axis, float degrees)
{
    const float radians = degrees * 3.14159265358979324f / 180.0f;
    return pxr::GfQuatf(std::cos(radians * 0.5f), axis.GetNormalized() * std::sin(radians * 0.5f));
}

const pxr::GfVec3f kAxisX(1.0f, 0.0f, 0.0f);
const pxr::GfVec3f kAxisY(0.0f, 1.0f, 0.0f);
const pxr::GfVec3f kAxisZ(0.0f, 0.0f, 1.0f);

// ---------------------------------------------------------------------------
// ExpressionResolve: a producer reports a name and a weight, the avatar carries
// the binds, and the join key is the verbatim name on both sides.
// ---------------------------------------------------------------------------

// An avatar with two expressions. `happy` drives two morph targets across two
// meshes plus a material colour -- the N-across-M shape the resolve exists for
// -- and `blink` drives one target of one mesh and is binary.
vrmRig::ExpressionRig
DesignExpressionRig()
{
    vrmRig::ExpressionRig rig;

    vrmRig::ExpressionDefinition happy;
    happy.name = "happy";
    happy.morphTargets.push_back({"/Asset/Meshes/Face/Smile", 1.0f});
    happy.morphTargets.push_back({"/Asset/Meshes/Brows/Raise", 0.5f});
    happy.materialColors.push_back(
        {"/Asset/Materials/Face", "color", pxr::GfVec4f(1.0f, 0.0f, 0.0f, 1.0f)});
    rig.Add(happy);

    vrmRig::ExpressionDefinition blink;
    blink.name = "blink";
    blink.isBinary = true;
    blink.morphTargets.push_back({"/Asset/Meshes/Face/EyeClose", 1.0f});
    rig.Add(blink);

    return rig;
}

openstrata::motion::MotionChannelSet
Weights(std::initializer_list<std::pair<const char*, float>> entries)
{
    openstrata::motion::MotionChannelSet weights;
    for (const auto& entry : entries)
    {
        weights.Set(entry.first, entry.second);
    }
    return weights;
}

void
TestExpressionRigDeclaresANameOnce()
{
    vrmRig::ExpressionRig rig = DesignExpressionRig();
    assert(rig.GetSize() == 2);

    // A second declaration of a declared name is refused rather than shadowing
    // the first -- the join key has to be unique or it is not a key.
    vrmRig::ExpressionDefinition duplicate;
    duplicate.name = "happy";
    duplicate.morphTargets.push_back({"/Asset/Meshes/Face/Other", 1.0f});
    assert(!rig.Add(duplicate));
    assert(rig.GetSize() == 2);
    assert(rig.Find("happy")->morphTargets[0].target == "/Asset/Meshes/Face/Smile");

    // A nameless expression cannot be joined on, so it is not a definition.
    vrmRig::ExpressionDefinition nameless;
    assert(!rig.Add(nameless));

    // Sorted by name, whatever order they arrived in.
    assert(rig.GetExpressions()[0].name == "blink");
    assert(rig.Find("relaxed") == nullptr);
}

void
TestOneWeightExpandsOntoEveryBind()
{
    const vrmRig::ExpressionResolver resolver(DesignExpressionRig());

    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", 0.5f}}), &diagnostics);

    assert(diagnostics.IsClean());
    // Two targets, sorted by target: Brows/Raise before Face/Smile.
    assert(resolved.morphTargets.size() == 2);
    assert(resolved.morphTargets[0].target == "/Asset/Meshes/Brows/Raise");
    // The bind's own 0.5 is the avatar's, and it multiplies the clip's.
    assert(NearlyEqual(resolved.morphTargets[0].weight, 0.25f));
    assert(resolved.morphTargets[1].target == "/Asset/Meshes/Face/Smile");
    assert(NearlyEqual(resolved.morphTargets[1].weight, 0.5f));

    // The colour is carried as (total weight, weighted target) so the material's
    // own base value never has to reach this library: Apply is the lerp.
    assert(resolved.materialColors.size() == 1);
    const vrmRig::ResolvedMaterialColor& color = resolved.materialColors[0];
    assert(color.material == "/Asset/Materials/Face");
    assert(color.colorType == "color");
    assert(NearlyEqual(color.totalWeight, 0.5f));
    const pxr::GfVec4f base(1.0f, 1.0f, 1.0f, 1.0f);
    assert(NearlyEqual(color.Apply(base), pxr::GfVec4f(1.0f, 0.5f, 0.5f, 1.0f)));
}

void
TestReportedZeroIsAuthoredAndUnreportedIsAbsent()
{
    const vrmRig::ExpressionResolver resolver(DesignExpressionRig());

    // A reported zero is a statement -- "this expression is off now" -- so its
    // targets are authored at zero. Dropping them would leave the previous
    // sample's weight standing on the rig.
    const vrmRig::ResolvedExpressions off = resolver.Resolve(Weights({{"happy", 0.0f}}));
    assert(off.morphTargets.size() == 2);
    assert(NearlyEqual(off.morphTargets[0].weight, 0.0f));
    assert(off.materialColors.size() == 1);
    assert(NearlyEqual(off.materialColors[0].totalWeight, 0.0f));
    // Apply with no weight is the material's own value, untouched.
    const pxr::GfVec4f base(0.25f, 0.5f, 0.75f, 1.0f);
    assert(NearlyEqual(off.materialColors[0].Apply(base), base));

    // `blink` was not reported, so its target is absent rather than zero: an
    // unreported name is not a zero weight, and this layer does not invent one
    // for the binds behind it either.
    for (const vrmRig::ResolvedMorphTarget& target : off.morphTargets)
    {
        assert(target.target != "/Asset/Meshes/Face/EyeClose");
    }

    // Nothing reported resolves to nothing at all.
    assert(resolver.Resolve(openstrata::motion::MotionChannelSet()).IsEmpty());
}

void
TestBinaryRoundsAndOutOfRangeIsClamped()
{
    const vrmRig::ExpressionResolver resolver(DesignExpressionRig());

    float weight = -1.0f;
    assert(resolver.ResolveWeight("blink", 0.4f, &weight));
    assert(NearlyEqual(weight, 0.0f));
    assert(resolver.ResolveWeight("blink", 0.5f, &weight));
    assert(NearlyEqual(weight, 1.0f));
    // "Does not resolve" is distinguishable from "resolves to zero", and the
    // sentinel differs from the reported weight so that an implementation
    // writing through the pointer before the early return fails here.
    weight = -7.0f;
    assert(!resolver.ResolveWeight("relaxed", 1.0f, &weight));
    assert(NearlyEqual(weight, -7.0f));

    // The clip reader carries a weight outside [0, 1] verbatim and leaves the
    // clamp to whoever applies it to a rig, which is here -- and the operator
    // is told which name it was.
    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", 1.5f}, {"blink", -0.2f}}), &diagnostics);
    assert(NearlyEqual(resolved.morphTargets[2].weight, 1.0f));
    assert(diagnostics.clampedNames.size() == 2);
    assert(diagnostics.clampedNames[0] == "blink");
    assert(diagnostics.unresolvedNames.empty());

    // A binary expression clamped to 0 still authors its target at 0.
    assert(resolved.morphTargets[1].target == "/Asset/Meshes/Face/EyeClose");
    assert(NearlyEqual(resolved.morphTargets[1].weight, 0.0f));

    // The rounding has to happen on the way to the binds and not only in
    // ResolveWeight: a partly-open eyelid is exactly what `isBinary` says this
    // rig cannot show, so 0.4 reaches the target as 0 and 0.6 as 1. Written
    // because the suite passed once with this line deleted from Resolve.
    const vrmRig::ResolvedExpressions ajar = resolver.Resolve(Weights({{"blink", 0.4f}}));
    assert(ajar.morphTargets.size() == 1);
    assert(ajar.morphTargets[0].target == "/Asset/Meshes/Face/EyeClose");
    assert(NearlyEqual(ajar.morphTargets[0].weight, 0.0f));
    assert(NearlyEqual(resolver.Resolve(Weights({{"blink", 0.6f}})).morphTargets[0].weight, 1.0f));

    // Turning the clamp off resolves what the producer actually said.
    vrmRig::ExpressionResolveOptions verbatim;
    verbatim.clampWeights = false;
    const vrmRig::ExpressionResolver unclamped(DesignExpressionRig(), verbatim);
    assert(NearlyEqual(unclamped.Resolve(Weights({{"happy", 1.5f}})).morphTargets[1].weight, 1.5f));
}

void
TestExpressionsAccumulateOnOneTarget()
{
    // Two expressions of the same rig driving one target is a rig that can sum
    // past 1, and the sum is carried through rather than corrected.
    vrmRig::ExpressionRig rig;
    vrmRig::ExpressionDefinition happy;
    happy.name = "happy";
    happy.morphTargets.push_back({"/Asset/Meshes/Face/Smile", 0.8f});
    happy.materialColors.push_back(
        {"/Asset/Materials/Face", "color", pxr::GfVec4f(1.0f, 0.0f, 0.0f, 1.0f)});
    rig.Add(happy);
    vrmRig::ExpressionDefinition aa;
    aa.name = "aa";
    aa.morphTargets.push_back({"/Asset/Meshes/Face/Smile", 0.8f});
    aa.materialColors.push_back(
        {"/Asset/Materials/Face", "color", pxr::GfVec4f(0.0f, 0.0f, 1.0f, 1.0f)});
    rig.Add(aa);

    const vrmRig::ExpressionResolver resolver(rig);
    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", 1.0f}, {"aa", 1.0f}}), &diagnostics);

    assert(resolved.morphTargets.size() == 1);
    assert(NearlyEqual(resolved.morphTargets[0].weight, 1.6f));
    assert(resolved.materialColors.size() == 1);
    assert(NearlyEqual(resolved.materialColors[0].totalWeight, 2.0f));
    // Two warnings, one per over-driven channel, and neither is an error: the
    // rig said it, so the operator hears it.
    assert(diagnostics.warnings.size() == 2);

    // Half of each stays inside 1 and says nothing.
    vrmRig::ExpressionDiagnostics quiet;
    const vrmRig::ResolvedExpressions half =
        resolver.Resolve(Weights({{"happy", 0.5f}, {"aa", 0.5f}}), &quiet);
    assert(quiet.IsClean());
    assert(NearlyEqual(half.morphTargets[0].weight, 0.8f));
    // The colour lerps toward both targets at once: base is pushed out entirely
    // and the two weighted targets are what is left.
    assert(NearlyEqual(half.materialColors[0].Apply(pxr::GfVec4f(1.0f)),
                       pxr::GfVec4f(0.5f, 0.0f, 0.5f, 1.0f)));
}

void
TestAnUnresolvedNameIsNamedOnceForAWholeClip()
{
    const vrmRig::ExpressionResolver resolver(DesignExpressionRig());

    openstrata::motion::MotionPose pose;
    pose.timestamp = 0.5;
    pose.channels.Set("happy", 0.25f);
    // A custom name this avatar does not declare. The clip is not wrong -- it
    // was authored against no avatar in particular -- but the loss is named.
    pose.channels.Set("照れ", 1.0f);

    vrmRig::ExpressionDiagnostics diagnostics;
    for (int sample = 0; sample < 3; ++sample)
    {
        const vrmRig::ResolvedExpressions resolved = resolver.Resolve(pose, &diagnostics);
        // The pose overload carries the sample's own time through.
        assert(NearlyEqual(static_cast<float>(resolved.timestamp), 0.5f));
        assert(resolved.morphTargets.size() == 2);
    }
    // Three samples, one line: diagnostics accumulate without repeating.
    assert(diagnostics.unresolvedNames.size() == 1);
    assert(diagnostics.unresolvedNames[0] == "照れ");
    assert(diagnostics.clampedNames.empty());
    assert(diagnostics.warnings.empty());
}

void
TestANonNumberIsNotAWeight()
{
    const vrmRig::ExpressionResolver resolver(DesignExpressionRig());
    const float notANumber = std::nanf("");

    // Every comparison against NaN is false, so a range test written as
    // `weight < 0 || weight > 1` calls it "already inside [0, 1]" and lets it
    // through to the binds, the totals and Apply() with nothing reported. It
    // clamps to 0 -- the only value that leaves the rig where it was -- and is
    // named beside the ordinary out-of-range weights.
    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", notANumber}}), &diagnostics);
    assert(resolved.morphTargets.size() == 2);
    assert(NearlyEqual(resolved.morphTargets[0].weight, 0.0f));
    assert(NearlyEqual(resolved.morphTargets[1].weight, 0.0f));
    assert(NearlyEqual(resolved.materialColors[0].totalWeight, 0.0f));
    assert(diagnostics.clampedNames.size() == 1);
    assert(diagnostics.clampedNames[0] == "happy");
    assert(!diagnostics.IsClean());

    float weight = -7.0f;
    assert(resolver.ResolveWeight("happy", notANumber, &weight));
    assert(NearlyEqual(weight, 0.0f));

    // An infinity is the same question with an answer the comparisons already
    // gave; it is here so the two cannot drift apart.
    vrmRig::ExpressionDiagnostics infinite;
    assert(NearlyEqual(
        resolver.Resolve(Weights({{"happy", HUGE_VALF}}), &infinite).morphTargets[1].weight, 1.0f));
    assert(infinite.clampedNames.size() == 1);

    // With clamping off the value reaches the binds, because that mode
    // resolves what the producer said -- but a resolve that carried a NaN into
    // an avatar must not read as a clean one.
    vrmRig::ExpressionResolveOptions verbatim;
    verbatim.clampWeights = false;
    const vrmRig::ExpressionResolver unclamped(DesignExpressionRig(), verbatim);
    vrmRig::ExpressionDiagnostics carried;
    const vrmRig::ResolvedExpressions raw =
        unclamped.Resolve(Weights({{"happy", notANumber}}), &carried);
    assert(std::isnan(raw.morphTargets[0].weight));
    // One warning for the expression, and one for each of the three channels
    // its NaN reached -- a count that says how far the value got.
    assert(carried.warnings.size() == 4);
    assert(!carried.IsClean());
}

void
TestABindWithNoIdentifierIsSkippedAndNamed()
{
    // Half a bind is not a bind: a morph target with no path, a colour with no
    // material, and a colour with no slot. The last one is the subtle one --
    // the slot is half the accumulator's key, so an empty one would merge two
    // binds of one material and hand back a colour nothing can map to a shader
    // input.
    vrmRig::ExpressionRig rig;
    vrmRig::ExpressionDefinition broken;
    broken.name = "happy";
    broken.morphTargets.push_back({"", 1.0f});
    broken.morphTargets.push_back({"/Asset/Meshes/Face/Smile", 1.0f});
    broken.materialColors.push_back({"", "color", pxr::GfVec4f(1.0f, 0.0f, 0.0f, 1.0f)});
    broken.materialColors.push_back(
        {"/Asset/Materials/Face", "", pxr::GfVec4f(1.0f, 0.0f, 0.0f, 1.0f)});
    broken.materialColors.push_back(
        {"/Asset/Materials/Face", "", pxr::GfVec4f(0.0f, 1.0f, 0.0f, 1.0f)});
    broken.materialColors.push_back(
        {"/Asset/Materials/Face", "emissionColor", pxr::GfVec4f(0.0f, 0.0f, 1.0f, 1.0f)});
    rig.Add(broken);

    const vrmRig::ExpressionResolver resolver(rig);
    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", 1.0f}}), &diagnostics);

    // The bind that could be resolved was, and only it.
    assert(resolved.morphTargets.size() == 1);
    assert(resolved.morphTargets[0].target == "/Asset/Meshes/Face/Smile");
    assert(resolved.materialColors.size() == 1);
    assert(resolved.materialColors[0].colorType == "emissionColor");
    // One line per shape, and the two slotless binds did not merge into a
    // single accumulator on their way to being refused.
    assert(diagnostics.warnings.size() == 3);
}

void
TestATargetDrivenBelowZeroIsReportedToo()
{
    // A negative bind weight is a rig this library does not validate, so a
    // fully-on expression can drive a target below 0. It extrapolates, exactly
    // as driving one past 1 does, and a report that named only the upper side
    // would leave this looking clean.
    vrmRig::ExpressionRig rig;
    vrmRig::ExpressionDefinition frown;
    frown.name = "sad";
    frown.morphTargets.push_back({"/Asset/Meshes/Face/Smile", -1.0f});
    rig.Add(frown);

    const vrmRig::ExpressionResolver resolver(rig);
    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"sad", 1.0f}}), &diagnostics);
    assert(NearlyEqual(resolved.morphTargets[0].weight, -1.0f));
    assert(diagnostics.warnings.size() == 1);
    assert(!diagnostics.IsClean());

    // The same in the other mode: a negative report with clamping off drives
    // every bind of the expression negative.
    vrmRig::ExpressionResolveOptions verbatim;
    verbatim.clampWeights = false;
    const vrmRig::ExpressionResolver unclamped(DesignExpressionRig(), verbatim);
    vrmRig::ExpressionDiagnostics carried;
    const vrmRig::ResolvedExpressions raw =
        unclamped.Resolve(Weights({{"happy", -0.5f}}), &carried);
    assert(NearlyEqual(raw.morphTargets[1].weight, -0.5f));
    assert(NearlyEqual(raw.materialColors[0].totalWeight, -0.5f));
    // Apply extrapolates past the material's own value rather than toward the
    // bind's target, which is the thing worth being told about.
    assert(NearlyEqual(raw.materialColors[0].Apply(pxr::GfVec4f(1.0f)),
                       pxr::GfVec4f(1.0f, 1.5f, 1.5f, 1.0f)));
    // Three channels outside the range -- two morph targets and one colour
    // slot -- one warning each, and nothing clamped.
    assert(carried.warnings.size() == 3);
    assert(carried.clampedNames.empty());
}

// ---------------------------------------------------------------------------
// The override fields: the one mechanism VRM 1.0 gives for two co-active
// expressions that drive the same vertices. Their morph offsets sum, so a
// `happy` that raises the cheek while `blink` closes the lid drives the lid
// roughly twice as far as shut -- and nothing in the weights looks wrong,
// because the collision is geometric.
// ---------------------------------------------------------------------------

// A rig with one expression in each of the three categories plus a custom one,
// so a rule that reached the wrong set has somewhere to show it. `happy` is the
// one that arbitrates; every other expression declares nothing.
vrmRig::ExpressionRig
DesignOverrideRig(vrmRig::ExpressionOverride blink, vrmRig::ExpressionOverride lookAt,
                  vrmRig::ExpressionOverride mouth, bool binaryBlink = false)
{
    vrmRig::ExpressionRig rig;

    vrmRig::ExpressionDefinition happy;
    happy.name = "happy";
    happy.overrideBlink = blink;
    happy.overrideLookAt = lookAt;
    happy.overrideMouth = mouth;
    happy.morphTargets.push_back({"/Asset/Meshes/Face/Smile", 1.0f});
    rig.Add(happy);

    vrmRig::ExpressionDefinition eyes;
    eyes.name = "blink";
    eyes.isBinary = binaryBlink;
    eyes.morphTargets.push_back({"/Asset/Meshes/Face/EyeClose", 1.0f});
    eyes.materialColors.push_back(
        {"/Asset/Materials/Face", "color", pxr::GfVec4f(0.0f, 0.0f, 1.0f, 1.0f)});
    rig.Add(eyes);

    vrmRig::ExpressionDefinition vowel;
    vowel.name = "aa";
    vowel.morphTargets.push_back({"/Asset/Meshes/Face/MouthOpen", 1.0f});
    rig.Add(vowel);

    vrmRig::ExpressionDefinition gaze;
    gaze.name = "lookLeft";
    gaze.morphTargets.push_back({"/Asset/Meshes/Face/EyeLeft", 1.0f});
    rig.Add(gaze);

    // Custom, and named to look like a blink on purpose: the categories are
    // sets of preset names, and a rig's own vocabulary is not one of them.
    vrmRig::ExpressionDefinition custom;
    custom.name = "wink";
    custom.morphTargets.push_back({"/Asset/Meshes/Face/Wink", 1.0f});
    rig.Add(custom);

    return rig;
}

// The resolved weight of one target of the sample, by path. -1 means the target
// is absent, which is a different answer from a weight of zero.
float
WeightOf(const vrmRig::ResolvedExpressions& resolved, const std::string& target)
{
    for (const vrmRig::ResolvedMorphTarget& entry : resolved.morphTargets)
    {
        if (entry.target == target)
        {
            return entry.weight;
        }
    }
    return -1.0f;
}

void
TestABlockingOverrideTakesTheWholeCategory()
{
    // `happy` blocks the mouth and arbitrates nothing else.
    const vrmRig::ExpressionResolver resolver(DesignOverrideRig(
        vrmRig::ExpressionOverride::None, vrmRig::ExpressionOverride::None,
        vrmRig::ExpressionOverride::Block));

    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved = resolver.Resolve(
        Weights(
            {{"happy", 0.2f}, {"blink", 1.0f}, {"aa", 1.0f}, {"lookLeft", 1.0f}, {"wink", 1.0f}}),
        &diagnostics);

    // Block is a switch, not a steep blend: 0.2 of `happy` takes all of `aa`.
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/MouthOpen"), 0.0f));
    // Everything else is untouched, including the eyes -- `overrideMouth` names
    // one category and not "the other expressions".
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/EyeClose"), 1.0f));
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/EyeLeft"), 1.0f));
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/Wink"), 1.0f));
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/Smile"), 0.2f));

    // The avatar's own rule being obeyed is not a defect, so the resolve still
    // reads as clean -- and the suppression is named anyway, with the
    // expression that did it, because a producer whose mouth track went flat
    // has nothing to find in the weights.
    assert(diagnostics.IsClean());
    assert(diagnostics.suppressedNames.size() == 1);
    assert(diagnostics.suppressedNames[0] == "aa (by happy)");
}

void
TestABlendingOverrideLeavesTheRestOfTheCategory()
{
    const vrmRig::ExpressionResolver resolver(DesignOverrideRig(
        vrmRig::ExpressionOverride::Blend, vrmRig::ExpressionOverride::None,
        vrmRig::ExpressionOverride::None));

    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", 0.4f}, {"blink", 1.0f}}), &diagnostics);

    // 40% of the way to a full override leaves 60% of the blink standing --
    // which is the whole difference from `block`, where the same 0.4 would have
    // taken all of it.
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/EyeClose"), 0.6f));
    // The suppressed weight is the one every bind gets, colours included.
    assert(resolved.materialColors.size() == 1);
    assert(NearlyEqual(resolved.materialColors[0].totalWeight, 0.6f));
    assert(diagnostics.suppressedNames.size() == 1);
    assert(diagnostics.suppressedNames[0] == "blink (by happy)");

    // At zero weight it arbitrates nothing, and says nothing either.
    vrmRig::ExpressionDiagnostics quiet;
    assert(
        NearlyEqual(WeightOf(resolver.Resolve(Weights({{"happy", 0.0f}, {"blink", 1.0f}}), &quiet),
                             "/Asset/Meshes/Face/EyeClose"),
                    1.0f));
    assert(quiet.suppressedNames.empty());
}

void
TestTheStrongestOverrideWinsAndTheyDoNotStack()
{
    // Two expressions suppressing one category do not suppress it twice: the
    // rate is the largest any of them asked for. Multiplying them would leave
    // 0.5 * 0.2 = 0.1 of the blink, which is a face neither expression asked
    // for and the number this test exists to refuse.
    vrmRig::ExpressionRig rig = DesignOverrideRig(vrmRig::ExpressionOverride::Blend,
                                                       vrmRig::ExpressionOverride::None,
                                                       vrmRig::ExpressionOverride::None);
    vrmRig::ExpressionDefinition relaxed;
    relaxed.name = "relaxed";
    relaxed.overrideBlink = vrmRig::ExpressionOverride::Blend;
    relaxed.morphTargets.push_back({"/Asset/Meshes/Face/Relax", 1.0f});
    rig.Add(relaxed);

    const vrmRig::ExpressionResolver resolver(std::move(rig));
    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved = resolver.Resolve(
        Weights({{"happy", 0.5f}, {"relaxed", 0.8f}, {"blink", 1.0f}}), &diagnostics);

    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/EyeClose"), 0.2f));
    // And the report names the one that decided it, not both.
    assert(diagnostics.suppressedNames.size() == 1);
    assert(diagnostics.suppressedNames[0] == "blink (by relaxed)");
}

void
TestAnExpressionThatIsOffOverridesNothing()
{
    // `block` reads "while this expression is on", so a reported zero is not a
    // block -- and neither is a binary expression reported below its threshold,
    // which is off however the file spelled the number. Reading the raw report
    // instead would let an expression contributing nothing to the face take the
    // whole blink with it.
    const vrmRig::ExpressionResolver resolver(DesignOverrideRig(
        vrmRig::ExpressionOverride::Block, vrmRig::ExpressionOverride::None,
        vrmRig::ExpressionOverride::None));
    assert(NearlyEqual(WeightOf(resolver.Resolve(Weights({{"happy", 0.0f}, {"blink", 1.0f}})),
                                "/Asset/Meshes/Face/EyeClose"),
                       1.0f));

    // The same rig with a binary `happy`.
    vrmRig::ExpressionRig source = DesignOverrideRig(vrmRig::ExpressionOverride::Block,
                                                          vrmRig::ExpressionOverride::None,
                                                          vrmRig::ExpressionOverride::None);
    vrmRig::ExpressionRig rebuilt;
    for (const vrmRig::ExpressionDefinition& definition : source.GetExpressions())
    {
        vrmRig::ExpressionDefinition copy = definition;
        if (copy.name == "happy")
        {
            copy.isBinary = true;
        }
        rebuilt.Add(std::move(copy));
    }
    const vrmRig::ExpressionResolver binary(std::move(rebuilt));
    // 0.4 rounds to off, so the block it declares is not in force here.
    assert(NearlyEqual(WeightOf(binary.Resolve(Weights({{"happy", 0.4f}, {"blink", 1.0f}})),
                                "/Asset/Meshes/Face/EyeClose"),
                       1.0f));
    // At 0.6 it is on -- fully on, since it is binary -- and the block bites.
    assert(NearlyEqual(WeightOf(binary.Resolve(Weights({{"happy", 0.6f}, {"blink", 1.0f}})),
                                "/Asset/Meshes/Face/EyeClose"),
                       0.0f));
}

void
TestABinaryEyelidIsShutOrOpenUnderABlend()
{
    // `isBinary` says this rig has no half-shut eyelid, so a partial
    // suppression either leaves the blink standing or turns it off. The
    // rounding is re-applied after the attenuation and not only before it,
    // which is the line this test measures: without it the eyelid would land on
    // 0.6 and 0.3, values the flag says the rig cannot show.
    const vrmRig::ExpressionResolver resolver(DesignOverrideRig(
        vrmRig::ExpressionOverride::Blend, vrmRig::ExpressionOverride::None,
        vrmRig::ExpressionOverride::None,
        /*binaryBlink=*/true));

    assert(NearlyEqual(WeightOf(resolver.Resolve(Weights({{"happy", 0.4f}, {"blink", 1.0f}})),
                                "/Asset/Meshes/Face/EyeClose"),
                       1.0f));
    assert(NearlyEqual(WeightOf(resolver.Resolve(Weights({{"happy", 0.7f}, {"blink", 1.0f}})),
                                "/Asset/Meshes/Face/EyeClose"),
                       0.0f));
}

void
TestAnOverrideOfItsOwnCategoryIsReported()
{
    // A rig that declares `overrideBlink` on `blink` itself suppresses its own
    // blink. It is followed rather than exempted -- an override cannot mean one
    // thing for `happy` and another for `blink` without becoming a rule an
    // operator can no longer predict from the file -- and it is reported,
    // because it is far more likely to be a slip than an intent.
    vrmRig::ExpressionRig rig;
    vrmRig::ExpressionDefinition eyes;
    eyes.name = "blink";
    eyes.overrideBlink = vrmRig::ExpressionOverride::Block;
    eyes.morphTargets.push_back({"/Asset/Meshes/Face/EyeClose", 1.0f});
    rig.Add(eyes);

    const vrmRig::ExpressionResolver resolver(std::move(rig));
    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"blink", 1.0f}}), &diagnostics);
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/EyeClose"), 0.0f));
    assert(diagnostics.warnings.size() == 1);
    assert(!diagnostics.IsClean());
}

void
TestTheOverrideVocabularyIsThreeTokensAndThreeSets()
{
    bool recognized = false;
    assert(vrmRig::ParseExpressionOverride("block", &recognized) ==
           vrmRig::ExpressionOverride::Block);
    assert(recognized);
    assert(vrmRig::ParseExpressionOverride("blend", &recognized) ==
           vrmRig::ExpressionOverride::Blend);
    assert(recognized);
    // An absent value and an explicit "none" are the same statement.
    assert(vrmRig::ParseExpressionOverride("none", &recognized) ==
           vrmRig::ExpressionOverride::None);
    assert(recognized);
    assert(vrmRig::ParseExpressionOverride("", &recognized) ==
           vrmRig::ExpressionOverride::None);
    assert(recognized);
    // A token this layer does not know is not an arbitration it can perform,
    // and guessing which one was meant would suppress a face on a spelling.
    assert(vrmRig::ParseExpressionOverride("Block", &recognized) ==
           vrmRig::ExpressionOverride::None);
    assert(!recognized);
    assert(std::string(vrmRig::ExpressionOverrideToken(
               vrmRig::ExpressionOverride::Blend)) == "blend");

    // The categories are the preset sets, in the VRM 1.0 spelling a VRM 0.x rig
    // also arrives in -- the importer migrates `blink_l` to `blinkLeft` and `a`
    // to `aa` on the way through.
    using vrmRig::ExpressionCategory;
    assert(vrmRig::ExpressionCategoryOf("blinkLeft") == ExpressionCategory::Blink);
    assert(vrmRig::ExpressionCategoryOf("lookDown") == ExpressionCategory::LookAt);
    assert(vrmRig::ExpressionCategoryOf("oh") == ExpressionCategory::Mouth);
    // `happy` arbitrates the categories and is in none of them; a custom name
    // is in none either, whatever it is called.
    assert(vrmRig::ExpressionCategoryOf("happy") == ExpressionCategory::None);
    assert(vrmRig::ExpressionCategoryOf("wink") == ExpressionCategory::None);
    assert(vrmRig::ExpressionCategoryOf("Blink") == ExpressionCategory::None);
}

void
TestASuppressedExpressionStillOverrides()
{
    // The arbitration is one pass over the weights the sample resolved to, so
    // an expression another override drives to zero still overrides its own
    // category. It is a boundary rather than an accident: cascading would make
    // the answer depend on the order the three categories are settled in, and
    // the rig below -- where `aa` blocks the blink and `happy` blocks the mouth
    // -- would then have two defensible answers and no reason to prefer either.
    vrmRig::ExpressionRig rig = DesignOverrideRig(vrmRig::ExpressionOverride::None,
                                                       vrmRig::ExpressionOverride::None,
                                                       vrmRig::ExpressionOverride::Block);
    vrmRig::ExpressionRig rebuilt;
    for (const vrmRig::ExpressionDefinition& definition : rig.GetExpressions())
    {
        vrmRig::ExpressionDefinition copy = definition;
        if (copy.name == "aa")
        {
            copy.overrideBlink = vrmRig::ExpressionOverride::Block;
        }
        rebuilt.Add(std::move(copy));
    }

    const vrmRig::ExpressionResolver resolver(std::move(rebuilt));
    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved =
        resolver.Resolve(Weights({{"happy", 1.0f}, {"aa", 1.0f}, {"blink", 1.0f}}), &diagnostics);

    // `happy` blocks the mouth, so `aa` resolves to nothing --
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/MouthOpen"), 0.0f));
    // -- and the blink `aa` blocks is off all the same.
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/EyeClose"), 0.0f));
    assert(diagnostics.suppressedNames.size() == 2);
    assert(diagnostics.suppressedNames[0] == "aa (by happy)");
    assert(diagnostics.suppressedNames[1] == "blink (by aa)");
}

void
TestAnOverrideRateNeverInvertsAWeight()
{
    // With clamping off a reported weight reaches the binds verbatim -- but a
    // rate is not a weight: it multiplies *another* expression's. Left
    // unbounded, a `happy` at 1.5 would drive the blink to 1 * (1 - 1.5) =
    // -0.5, which is not a suppression but an inversion, and it would surface
    // only as the generic "driven outside [0, 1]" warning about a target
    // nothing asked to move.
    vrmRig::ExpressionResolveOptions verbatim;
    verbatim.clampWeights = false;
    const vrmRig::ExpressionResolver resolver(
        DesignOverrideRig(vrmRig::ExpressionOverride::Blend,
                          vrmRig::ExpressionOverride::None,
                          vrmRig::ExpressionOverride::None),
        verbatim);

    // Past 1 the rate saturates: fully suppressed, never inverted.
    const vrmRig::ResolvedExpressions past =
        resolver.Resolve(Weights({{"happy", 1.5f}, {"blink", 1.0f}}));
    assert(NearlyEqual(WeightOf(past, "/Asset/Meshes/Face/EyeClose"), 0.0f));
    // `happy` itself still reaches its own binds verbatim, which is what that
    // mode is for -- the bound is on the rate and not on the weight.
    assert(NearlyEqual(WeightOf(past, "/Asset/Meshes/Face/Smile"), 1.5f));

    // Below 0 it suppresses nothing rather than amplifying.
    const vrmRig::ResolvedExpressions below =
        resolver.Resolve(Weights({{"happy", -0.5f}, {"blink", 1.0f}}));
    assert(NearlyEqual(WeightOf(below, "/Asset/Meshes/Face/EyeClose"), 1.0f));

    // And a weight that is not a number arbitrates nothing, for the same
    // reason it is a weight of zero: every comparison against it is false, so
    // an unguarded `1 - rate` would carry the NaN into the blink's binds.
    const vrmRig::ResolvedExpressions notANumber =
        resolver.Resolve(Weights({{"happy", std::nanf("")}, {"blink", 1.0f}}));
    assert(NearlyEqual(WeightOf(notANumber, "/Asset/Meshes/Face/EyeClose"), 1.0f));
}

void
TestAGazeExpressionIsSuppressedLikeAnyOther()
{
    // An expression-driven look-at reaches the resolve as `lookLeft` and the
    // other three, folded into the sample's own weights -- so `overrideLookAt`
    // arbitrates a rig's gaze through exactly the path it arbitrates its face,
    // and needs no second mechanism.
    const vrmRig::ExpressionResolver resolver(DesignOverrideRig(
        vrmRig::ExpressionOverride::None, vrmRig::ExpressionOverride::Blend,
        vrmRig::ExpressionOverride::None));

    vrmRig::ExpressionDiagnostics diagnostics;
    const vrmRig::ResolvedExpressions resolved = resolver.Resolve(
        Weights({{"happy", 0.25f}, {"lookLeft", 0.8f}, {"blink", 1.0f}}), &diagnostics);
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/EyeLeft"), 0.6f));
    assert(NearlyEqual(WeightOf(resolved, "/Asset/Meshes/Face/EyeClose"), 1.0f));
    assert(diagnostics.suppressedNames.size() == 1);
    assert(diagnostics.suppressedNames[0] == "lookLeft (by happy)");
}


// ---------------------------------------------------------------------------
// LookAtEvaluator
// ---------------------------------------------------------------------------

// A range map that is the identity over [0, 90] degrees, so a resolved eye
// rotation is the aim itself and the test measures the geometry rather than a
// curve on top of it.
vrmRig::LookAtRangeMap
IdentityMap()
{
    vrmRig::LookAtRangeMap map;
    map.inputMaxValue = 90.0f;
    map.outputScale = 90.0f;
    return map;
}

vrmRig::LookAtRig
IdentityBoneRig()
{
    vrmRig::LookAtRig rig;
    rig.type = vrmRig::LookAtType::Bone;
    rig.leftEyeJoint = "Root/Hips/Spine/Head/LeftEye";
    rig.rightEyeJoint = "Root/Hips/Spine/Head/RightEye";
    rig.horizontalInner = IdentityMap();
    rig.horizontalOuter = IdentityMap();
    rig.verticalDown = IdentityMap();
    rig.verticalUp = IdentityMap();
    // An explicit zero: this rig states that its eyes sit on the head joint,
    // which is a measurement rather than the absence of one. The tests that are
    // about the absence clear it.
    rig.offsetFromHeadBone = pxr::GfVec3f(0.0f);
    return rig;
}

// The same rig driven through the face instead. Every map is the identity onto
// a unit weight, so a gaze at the limit of a range is a weight of exactly 1.
vrmRig::LookAtRig
IdentityExpressionRig()
{
    vrmRig::LookAtRig rig = IdentityBoneRig();
    rig.type = vrmRig::LookAtType::Expression;
    rig.horizontalInner.outputScale = 1.0f;
    rig.horizontalOuter.outputScale = 1.0f;
    rig.verticalDown.outputScale = 1.0f;
    rig.verticalUp.outputScale = 1.0f;
    return rig;
}

vrmRig::LookAtInput
GazeAt(const pxr::GfVec3f& target)
{
    vrmRig::LookAtInput input;
    input.target = target;
    return input;
}

void
TestAnIdentityRangeMapReproducesTheAim()
{
    // The composition order is the claim: yaw about +Y, then pitch about +X
    // negated, because a positive right-handed rotation about +X takes the
    // forward axis down. Get either wrong and a rig whose maps do nothing --
    // 90 degrees of input onto 90 degrees of output -- still fails to point at
    // the thing it is aiming at. So this measures the round trip rather than
    // asserting the two angles.
    const vrmRig::LookAtEvaluator evaluator(IdentityBoneRig());
    vrmRig::LookAtDiagnostics diagnostics;
    const pxr::GfVec3f target(1.0f, 1.0f, 1.0f);
    const vrmRig::ResolvedLookAt resolved = evaluator.Evaluate(GazeAt(target), &diagnostics);

    assert(resolved.hasGaze);
    assert(NearlyEqual(resolved.yawDegrees, 45.0f));
    assert(NearlyEqual(resolved.pitchDegrees, 35.26439f));
    assert(resolved.eyeRotations.size() == 2);
    for (const vrmRig::LookAtEyeRotation& eye : resolved.eyeRotations)
    {
        assert(NearlyEqual(eye.rotation.Transform(kAxisZ), target.GetNormalized()));
    }
    // An absent target is the only thing this counts as a sample without one,
    // and there was none.
    assert(diagnostics.samplesEvaluated == 1);
    assert(diagnostics.samplesWithoutTarget == 0);

    // +X is the character's own left in the basis VRM inherits from glTF, so a
    // target on that side is a positive yaw. The mirror image is the same
    // magnitude with the other sign, which is what makes the inner/outer choice
    // below a choice about a side rather than about a formula.
    const vrmRig::ResolvedLookAt mirrored =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(-1.0f, 1.0f, 1.0f)));
    assert(NearlyEqual(mirrored.yawDegrees, -45.0f));
    assert(NearlyEqual(mirrored.pitchDegrees, 35.26439f));
}

void
TestTheOffsetPlacesTheGazeOrigin()
{
    // The gaze starts at the eyes, not at the head joint, and the offset that
    // says where they are is stated in the head's own space -- so it has to be
    // rotated by the head before it is added. A rig that added it in world
    // space would agree with this test at an identity head orientation and
    // disagree the moment the character turned, which is why the second half
    // turns the head.
    vrmRig::LookAtRig rig = IdentityBoneRig();
    rig.offsetFromHeadBone = pxr::GfVec3f(0.0f, 0.06f, 0.0f);
    const vrmRig::LookAtEvaluator evaluator(rig);

    // Straight ahead of the eyes is a level gaze...
    const vrmRig::ResolvedLookAt level =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.06f, 1.0f)));
    assert(level.hasGaze);
    assert(NearlyEqual(level.yawDegrees, 0.0f));
    assert(NearlyEqual(level.pitchDegrees, 0.0f));

    // ...and straight ahead of the *joint* is 6 cm below them.
    const vrmRig::ResolvedLookAt below =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.0f, 1.0f)));
    assert(NearlyEqual(below.pitchDegrees, -3.43363f));

    // With the head turned a quarter turn to the character's left, the eyes
    // move with it: the target that was level ahead is now off to the right by
    // exactly that quarter turn, and the offset -- which is along the head's
    // own up axis, unchanged by a yaw -- still puts the origin at the eyes.
    vrmRig::LookAtInput turned = GazeAt(pxr::GfVec3f(0.0f, 0.06f, 1.0f));
    turned.head.orientation = Rotation(kAxisY, 90.0f);
    const vrmRig::ResolvedLookAt aside = evaluator.Evaluate(turned);
    assert(NearlyEqual(aside.yawDegrees, -90.0f));
    assert(NearlyEqual(aside.pitchDegrees, 0.0f));

    // A head that has moved carries its eyes with it too.
    vrmRig::LookAtInput walked = GazeAt(pxr::GfVec3f(0.0f, 1.56f, 1.0f));
    walked.head.position = pxr::GfVec3f(0.0f, 1.5f, 0.0f);
    const vrmRig::ResolvedLookAt ahead = evaluator.Evaluate(walked);
    assert(NearlyEqual(ahead.pitchDegrees, 0.0f));
}

void
TestInnerAndOuterAreChosenBySide()
{
    // Two eyes converge: the one on the side the gaze goes to turns outward,
    // away from the nose, and the other turns inward. The two maps are given
    // different scales so a resolve that read one map for both eyes -- or read
    // them the other way round -- cannot pass.
    vrmRig::LookAtRig rig = IdentityBoneRig();
    rig.horizontalInner.outputScale = 5.0f;
    rig.horizontalOuter.outputScale = 10.0f;
    rig.verticalUp.outputScale = 0.0f;
    rig.verticalDown.outputScale = 0.0f;
    const vrmRig::LookAtEvaluator evaluator(rig);

    const vrmRig::ResolvedLookAt left =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 0.0f)));
    assert(NearlyEqual(left.yawDegrees, 90.0f));
    assert(left.eyeRotations[0].joint == rig.leftEyeJoint);
    assert(SameOrientation(left.eyeRotations[0].rotation, Rotation(kAxisY, 10.0f)));
    assert(SameOrientation(left.eyeRotations[1].rotation, Rotation(kAxisY, 5.0f)));

    const vrmRig::ResolvedLookAt right =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(-1.0f, 0.0f, 0.0f)));
    assert(NearlyEqual(right.yawDegrees, -90.0f));
    assert(SameOrientation(right.eyeRotations[0].rotation, Rotation(kAxisY, -5.0f)));
    assert(SameOrientation(right.eyeRotations[1].rotation, Rotation(kAxisY, -10.0f)));

    // Past the map's input range the eye stops rather than extrapolating: a
    // target behind the character's shoulder is 135 degrees of aim and still
    // ten degrees of eye.
    const vrmRig::ResolvedLookAt behind =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, -1.0f)));
    assert(NearlyEqual(behind.yawDegrees, 135.0f));
    assert(SameOrientation(behind.eyeRotations[0].rotation, Rotation(kAxisY, 10.0f)));
}

void
TestVerticalIsSharedAndSignedByDirection()
{
    // Up and down are two maps because a face is not symmetric about the
    // horizon -- an eye rolls further up than down -- but both eyes share them,
    // so this is the one place the two rotations agree.
    vrmRig::LookAtRig rig = IdentityBoneRig();
    rig.horizontalInner.outputScale = 0.0f;
    rig.horizontalOuter.outputScale = 0.0f;
    rig.verticalUp.outputScale = 12.0f;
    rig.verticalDown.outputScale = 6.0f;
    const vrmRig::LookAtEvaluator evaluator(rig);

    const vrmRig::ResolvedLookAt up =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 1.0f, 0.0f)));
    assert(NearlyEqual(up.pitchDegrees, 90.0f));
    assert(SameOrientation(up.eyeRotations[0].rotation, Rotation(kAxisX, -12.0f)));
    assert(SameOrientation(up.eyeRotations[1].rotation, Rotation(kAxisX, -12.0f)));

    const vrmRig::ResolvedLookAt down =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, -1.0f, 0.0f)));
    assert(NearlyEqual(down.pitchDegrees, -90.0f));
    assert(SameOrientation(down.eyeRotations[0].rotation, Rotation(kAxisX, 6.0f)));
}

void
TestAGazeNobodyNamedIsNotAGazeForward()
{
    // The rule an unreported expression name is under, one field over: a clip
    // that said nothing about where the character is looking did not say the
    // character is looking straight ahead. Nothing resolves, and it is not a
    // warning -- a clip with no look-at track is an ordinary clip.
    const vrmRig::LookAtEvaluator evaluator(IdentityBoneRig());
    vrmRig::LookAtDiagnostics diagnostics;
    const vrmRig::ResolvedLookAt resolved =
        evaluator.Evaluate(vrmRig::LookAtInput(), &diagnostics);

    assert(!resolved.hasGaze);
    assert(resolved.eyeRotations.empty());
    assert(resolved.expressions.IsEmpty());
    assert(NearlyEqual(resolved.yawDegrees, 0.0f));
    assert(diagnostics.samplesEvaluated == 1);
    assert(diagnostics.samplesWithoutTarget == 1);
    assert(diagnostics.IsClean());

    float yaw = 7.0f;
    float pitch = 7.0f;
    assert(!evaluator.Aim(vrmRig::LookAtInput(), &yaw, &pitch));
    // Untouched, so a caller cannot mistake a refusal for a level gaze.
    assert(NearlyEqual(yaw, 7.0f) && NearlyEqual(pitch, 7.0f));
}

void
TestATargetOnTheEyesNamesNoDirection()
{
    // A target at the eye origin has no direction to normalize, and one a
    // micrometre away has one that is numerically meaningless. Both are the
    // same defect and both answer "no gaze" rather than inventing forward --
    // but unlike an absent target, this one is a rig or a clip going wrong, so
    // it is reported.
    vrmRig::LookAtRig rig = IdentityBoneRig();
    rig.offsetFromHeadBone = pxr::GfVec3f(0.0f, 0.06f, 0.0f);
    const vrmRig::LookAtEvaluator evaluator(rig);
    vrmRig::LookAtDiagnostics diagnostics;

    const vrmRig::ResolvedLookAt resolved =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.06f, 0.0f)), &diagnostics);
    assert(!resolved.hasGaze);
    assert(diagnostics.samplesWithoutTarget == 1);
    assert(diagnostics.warnings.size() == 1);

    // A second sample with the same defect is the same fact about the clip, so
    // the report does not grow.
    evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.06f, 0.0f)), &diagnostics);
    assert(diagnostics.warnings.size() == 1);
    assert(diagnostics.samplesEvaluated == 2);
}

void
TestTheClipsOffsetIsTheFallbackAndIsReported()
{
    // A VRM 0.x rig states no offsetFromHeadBone at all. The clip's is the only
    // measurement left, and using it assumes the two rigs' eyes sit at the same
    // height -- an assumption an operator should see, so it is a warning rather
    // than a default.
    vrmRig::LookAtRig unmeasuredRig = IdentityBoneRig();
    unmeasuredRig.offsetFromHeadBone.reset();

    vrmRig::LookAtEvaluateOptions options;
    options.clipOffsetFromHeadBone = pxr::GfVec3f(0.0f, 0.06f, 0.0f);
    const vrmRig::LookAtEvaluator borrowed(unmeasuredRig, options);
    vrmRig::LookAtDiagnostics diagnostics;
    const vrmRig::ResolvedLookAt resolved =
        borrowed.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.0f, 1.0f)), &diagnostics);
    assert(NearlyEqual(resolved.pitchDegrees, -3.43363f));
    assert(diagnostics.warnings.size() == 1);

    // The avatar's own offset wins when it has one, and then there is nothing
    // to report.
    vrmRig::LookAtRig own = IdentityBoneRig();
    own.offsetFromHeadBone = pxr::GfVec3f(0.0f, 0.12f, 0.0f);
    const vrmRig::LookAtEvaluator preferred(own, options);
    vrmRig::LookAtDiagnostics clean;
    const vrmRig::ResolvedLookAt higher =
        preferred.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.0f, 1.0f)), &clean);
    assert(NearlyEqual(higher.pitchDegrees, -6.84277f));
    assert(clean.IsClean());

    // Neither side stating one is a third case, and it is not silent either:
    // the gaze then starts at the head joint, which is inside the skull.
    const vrmRig::LookAtEvaluator bare(unmeasuredRig);
    vrmRig::LookAtDiagnostics unmeasured;
    bare.Evaluate(GazeAt(pxr::GfVec3f(0.0f, 0.0f, 1.0f)), &unmeasured);
    assert(unmeasured.warnings.size() == 1);
}

void
TestAnExpressionRigReportsAllFourNames()
{
    // One weight drives both eyes, so an expression rig has no inner eye and
    // the horizontal curve is the outer one. What matters more is that all four
    // names are reported every sample: a gaze that swings left after a sample
    // that looked right has to say `lookRight` is now 0, or the earlier weight
    // stands on the rig -- the same rule ExpressionResolver states for a
    // reported zero.
    const vrmRig::LookAtEvaluator evaluator(IdentityExpressionRig());

    vrmRig::LookAtDiagnostics diagnostics;
    const vrmRig::ResolvedLookAt left =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 0.0f)), &diagnostics);
    assert(left.hasGaze);
    // No eye is rotated: an expression rig drives its gaze through the face.
    assert(left.eyeRotations.empty());
    assert(left.expressions.entries.size() == 4);
    assert(NearlyEqual(*left.expressions.Find("lookLeft"), 1.0f));
    assert(NearlyEqual(*left.expressions.Find("lookRight"), 0.0f));
    assert(NearlyEqual(*left.expressions.Find("lookUp"), 0.0f));
    assert(NearlyEqual(*left.expressions.Find("lookDown"), 0.0f));

    const vrmRig::ResolvedLookAt down =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(0.0f, -1.0f, 0.0f)), &diagnostics);
    assert(NearlyEqual(*down.expressions.Find("lookDown"), 1.0f));
    assert(NearlyEqual(*down.expressions.Find("lookUp"), 0.0f));
    // A downward gaze is straight down, so its horizontal weights are both
    // zero -- and both are still stated.
    assert(NearlyEqual(*down.expressions.Find("lookLeft"), 0.0f));
    assert(NearlyEqual(*down.expressions.Find("lookRight"), 0.0f));
    assert(diagnostics.IsClean());

    // The value the expression half hands back is exactly what
    // ExpressionResolver consumes, so a gaze reaches the avatar's binds through
    // the path the face already uses rather than through a second one.
    vrmRig::ExpressionRig binds;
    vrmRig::ExpressionDefinition lookDown;
    lookDown.name = "lookDown";
    lookDown.morphTargets.push_back({"/Asset/Meshes/Face/EyesDown", 1.0f});
    binds.Add(lookDown);
    const vrmRig::ExpressionResolver resolver(binds);
    const vrmRig::ResolvedExpressions applied = resolver.Resolve(down.expressions);
    assert(applied.morphTargets.size() == 1);
    assert(NearlyEqual(applied.morphTargets[0].weight, 1.0f));
}

void
TestAnExpressionWeightOutsideTheRangeIsClampedAndNamed()
{
    // A VRM 0.x BlendShape rig states its output range in the same key a Bone
    // rig states degrees in, and nothing in the block distinguishes them. So a
    // weight of 10 is clamped rather than being rescaled by a factor guessed
    // from the rig's type, and the operator is told which name it happened to.
    vrmRig::LookAtRig rig = IdentityExpressionRig();
    // Both horizontal maps, so the only thing this sample can be told about is
    // the weight -- an expression rig that states a *different* inner map is a
    // separate report, and it is the case below.
    rig.horizontalInner.outputScale = 10.0f;
    rig.horizontalOuter.outputScale = 10.0f;
    const vrmRig::LookAtEvaluator evaluator(rig);

    vrmRig::LookAtDiagnostics diagnostics;
    const vrmRig::ResolvedLookAt clamped =
        evaluator.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 0.0f)), &diagnostics);
    assert(NearlyEqual(*clamped.expressions.Find("lookLeft"), 1.0f));
    assert(diagnostics.warnings.size() == 1);

    vrmRig::LookAtEvaluateOptions verbatim;
    verbatim.clampExpressionWeights = false;
    const vrmRig::LookAtEvaluator unclamped(rig, verbatim);
    vrmRig::LookAtDiagnostics carried;
    const vrmRig::ResolvedLookAt raw =
        unclamped.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 0.0f)), &carried);
    assert(NearlyEqual(*raw.expressions.Find("lookLeft"), 10.0f));
    assert(carried.warnings.size() == 1);
    assert(carried.warnings[0] != diagnostics.warnings[0]);
}

void
TestABoneRigWithHalfItsEyesDrivesTheOneItNamed()
{
    vrmRig::LookAtRig half = IdentityBoneRig();
    half.rightEyeJoint.clear();
    const vrmRig::LookAtEvaluator one(half);
    vrmRig::LookAtDiagnostics diagnostics;
    const vrmRig::ResolvedLookAt resolved =
        one.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 1.0f)), &diagnostics);
    assert(resolved.hasGaze);
    assert(resolved.eyeRotations.size() == 1);
    assert(resolved.eyeRotations[0].joint == half.leftEyeJoint);
    assert(diagnostics.warnings.size() == 1);

    // A bone rig naming neither eye resolves to nothing at all, and the aim is
    // still measured -- which is what lets a caller report the gaze it could
    // not apply.
    vrmRig::LookAtRig blind = IdentityBoneRig();
    blind.leftEyeJoint.clear();
    blind.rightEyeJoint.clear();
    const vrmRig::LookAtEvaluator none(blind);
    vrmRig::LookAtDiagnostics blindReport;
    const vrmRig::ResolvedLookAt aimed =
        none.Evaluate(GazeAt(pxr::GfVec3f(1.0f, 0.0f, 1.0f)), &blindReport);
    assert(aimed.hasGaze);
    assert(NearlyEqual(aimed.yawDegrees, 45.0f));
    assert(aimed.eyeRotations.empty());
    assert(blindReport.warnings.size() == 1);
}

void
TestThePoseOverloadCarriesTheSampleThrough()
{
    const vrmRig::LookAtEvaluator evaluator(IdentityBoneRig());
    openstrata::motion::MotionPose pose;
    pose.timestamp = 1.25;
    assert(!pose.lookAtTarget);

    vrmRig::LookAtHead head;
    head.position = pxr::GfVec3f(0.0f, 1.5f, 0.0f);
    const vrmRig::ResolvedLookAt silent = evaluator.Evaluate(pose, head);
    assert(!silent.hasGaze);
    assert(silent.timestamp == 1.25);

    pose.lookAtTarget = pxr::GfVec3f(0.0f, 2.5f, 1.0f);
    const vrmRig::ResolvedLookAt gazing = evaluator.Evaluate(pose, head);
    assert(gazing.hasGaze);
    assert(gazing.timestamp == 1.25);
    // One metre up and one metre ahead of a head that is itself 1.5 m up.
    assert(NearlyEqual(gazing.pitchDegrees, 45.0f));
}

void
TestBothVrmSpellingsParseToOneValue()
{
    // The 1.0 range map and the 0.x curve say the same thing in two shapes, and
    // the reader's job is that a consumer never learns which shape a rig came
    // from. The linear default is where the two have to agree exactly rather
    // than within a tolerance: 0.x's `[0,0,0,1, 1,1,1,0]` is the Hermite basis
    // over one unit segment with both tangents 1, which reduces to `t`.
    vrmRig::LookAtRig one;
    std::vector<std::string> warnings;
    assert(vrmRig::ParseLookAtRangeMaps(
        R"({"type":"bone","offsetFromHeadBone":[0.0,0.06,0.0],)"
        R"("rangeMapHorizontalInner":{"inputMaxValue":90,"outputScale":5.0},)"
        R"("rangeMapHorizontalOuter":{"inputMaxValue":90,"outputScale":10.0}})",
        &one, &warnings));
    assert(warnings.empty());
    assert(one.type == vrmRig::LookAtType::Bone);
    assert(one.offsetFromHeadBone &&
           NearlyEqual(*one.offsetFromHeadBone, pxr::GfVec3f(0.0f, 0.06f, 0.0f)));
    assert(NearlyEqual(one.horizontalInner.outputScale, 5.0f));
    assert(NearlyEqual(one.horizontalOuter.Map(45.0f), 5.0f));
    assert(one.horizontalOuter.curve.empty());
    // A map the block never mentioned keeps its default rather than collapsing
    // to zero: an incomplete block is not four broken curves.
    assert(NearlyEqual(one.verticalUp.inputMaxValue, 90.0f));

    vrmRig::LookAtRig zero;
    assert(
        vrmRig::ParseLookAtRangeMaps(R"({"lookAtTypeName":"BlendShape",)"
                                          R"("lookAtHorizontalOuter":{"curve":[0,0,0,1,1,1,1,0],)"
                                          R"("xRange":90,"yRange":1.0}})",
                                          &zero, &warnings));
    assert(warnings.empty());
    // "BlendShape" is the name 0.x gives the rig 1.0 calls `expression`, and
    // the rest of this library speaks the newer one.
    assert(zero.type == vrmRig::LookAtType::Expression);
    assert(zero.horizontalOuter.curve.size() == 2);
    assert(NearlyEqual(zero.horizontalOuter.Map(45.0f), 0.5f));

    vrmRig::LookAtRangeMap implicitlyLinear = zero.horizontalOuter;
    implicitlyLinear.curve.clear();
    for (const float degrees : {0.0f, 12.5f, 45.0f, 71.25f, 90.0f, 180.0f})
    {
        assert(NearlyEqual(zero.horizontalOuter.Map(degrees), implicitlyLinear.Map(degrees)));
    }

    // A curve that is not the linear default is read as the curve it is.
    vrmRig::LookAtRig curved;
    assert(vrmRig::ParseLookAtRangeMaps(R"({"lookAtVerticalUp":{"curve":[0,0,0,0,1,1,0,0],)"
                                             R"("xRange":90,"yRange":10}})",
                                             &curved, &warnings));
    // Flat tangents at both ends: the smoothstep, which is 0.5 at the middle
    // and visibly not `t` on either side of it.
    assert(NearlyEqual(curved.verticalUp.Map(45.0f), 5.0f));
    assert(NearlyEqual(curved.verticalUp.Map(22.5f), 1.5625f));
}

void
TestAnUnreadableLookAtBlockLeavesTheDefaultsStanding()
{
    vrmRig::LookAtRig rig;
    std::vector<std::string> warnings;
    // The empty string is what a rig with no preserved curves carries, and it
    // is not a defect -- there is nothing to warn about in a file that said
    // nothing.
    assert(!vrmRig::ParseLookAtRangeMaps("", &rig, &warnings));
    assert(warnings.empty());

    // A block that is not an object is a defect, and the defaults stand.
    assert(!vrmRig::ParseLookAtRangeMaps("[90, 10]", &rig, &warnings));
    assert(warnings.size() == 1);
    assert(NearlyEqual(rig.horizontalOuter.inputMaxValue, 90.0f));

    // An input range of zero would be a division by it. It maps everything to
    // nothing instead, and says so.
    warnings.clear();
    assert(vrmRig::ParseLookAtRangeMaps(
        R"({"rangeMapVerticalUp":{"inputMaxValue":0,"outputScale":10}})", &rig, &warnings));
    assert(warnings.size() == 1);
    assert(rig.verticalUp.Map(45.0f) == 0.0f);

    // A curve that is not a whole number of four-float keys is not a curve.
    // Reading the keys it does hold would silently rescale the rest of it, so
    // it falls back to linear and is named.
    warnings.clear();
    vrmRig::LookAtRig ragged;
    assert(vrmRig::ParseLookAtRangeMaps(
        R"({"lookAtVerticalDown":{"curve":[0,0,0,1,1],"xRange":90,)"
        R"("yRange":10}})",
        &ragged, &warnings));
    assert(warnings.size() == 1);
    assert(ragged.verticalDown.curve.empty());
    assert(NearlyEqual(ragged.verticalDown.Map(45.0f), 5.0f));
}

// ---------------------------------------------------------------------------
// The required bones: VRM 1.0's statement, which `motionRetarget` takes from its
// caller rather than holding one of its own.
// ---------------------------------------------------------------------------

void
TestTheRequiredBonesAreVrm10sSeventeenHipsFirst()
{
    using openstrata::motion::HumanJoint;
    const std::vector<HumanJoint>& required = vrmRig::GetRequiredBones();

    // Seventeen, each once. A duplicate would report a missing bone twice.
    assert(required.size() == 17);
    for (std::size_t i = 0; i < required.size(); ++i)
    {
        assert(std::count(required.begin(), required.end(), required[i]) == 1);
    }

    // Hips first, which is what keeps every report in this list's order: the
    // retarget prepends the hips under `Hips` root motion when a set lacks
    // them, and a set that starts with them is never reordered.
    assert(required.front() == HumanJoint::Hips);

    // What VRM 1.0 leaves optional is absent.
    for (const HumanJoint optional :
         {HumanJoint::UpperChest, HumanJoint::Jaw, HumanJoint::LeftEye, HumanJoint::RightEye,
          HumanJoint::LeftShoulder, HumanJoint::RightShoulder, HumanJoint::LeftToes,
          HumanJoint::RightToes, HumanJoint::LeftIndexProximal})
    {
        assert(std::find(required.begin(), required.end(), optional) == required.end());
    }

    // The same object every call, so a caller holding a reference holds the set.
    assert(&vrmRig::GetRequiredBones() == &required);
}

} // namespace

int
main()
{
    TestExpressionRigDeclaresANameOnce();
    TestOneWeightExpandsOntoEveryBind();
    TestReportedZeroIsAuthoredAndUnreportedIsAbsent();
    TestBinaryRoundsAndOutOfRangeIsClamped();
    TestExpressionsAccumulateOnOneTarget();
    TestAnUnresolvedNameIsNamedOnceForAWholeClip();
    TestANonNumberIsNotAWeight();
    TestABindWithNoIdentifierIsSkippedAndNamed();
    TestATargetDrivenBelowZeroIsReportedToo();
    TestABlockingOverrideTakesTheWholeCategory();
    TestABlendingOverrideLeavesTheRestOfTheCategory();
    TestTheStrongestOverrideWinsAndTheyDoNotStack();
    TestAnExpressionThatIsOffOverridesNothing();
    TestABinaryEyelidIsShutOrOpenUnderABlend();
    TestAnOverrideOfItsOwnCategoryIsReported();
    TestTheOverrideVocabularyIsThreeTokensAndThreeSets();
    TestASuppressedExpressionStillOverrides();
    TestAnOverrideRateNeverInvertsAWeight();
    TestAGazeExpressionIsSuppressedLikeAnyOther();
    TestAnIdentityRangeMapReproducesTheAim();
    TestTheOffsetPlacesTheGazeOrigin();
    TestInnerAndOuterAreChosenBySide();
    TestVerticalIsSharedAndSignedByDirection();
    TestAGazeNobodyNamedIsNotAGazeForward();
    TestATargetOnTheEyesNamesNoDirection();
    TestTheClipsOffsetIsTheFallbackAndIsReported();
    TestAnExpressionRigReportsAllFourNames();
    TestAnExpressionWeightOutsideTheRangeIsClampedAndNamed();
    TestABoneRigWithHalfItsEyesDrivesTheOneItNamed();
    TestThePoseOverloadCarriesTheSampleThrough();
    TestBothVrmSpellingsParseToOneValue();
    TestAnUnreadableLookAtBlockLeavesTheDefaultsStanding();
    TestTheRequiredBonesAreVrm10sSeventeenHipsFirst();
    std::puts("vrmRig unit tests passed");
    return 0;
}
