// SPDX-License-Identifier: Apache-2.0
//
// Expands a named expression weight onto a target rig's binds.
//
// A VRM expression is not one blend shape: it drives N morph targets across M
// meshes plus a set of material colours, and the numbers that say by how much
// belong to the avatar rather than to the clip. So a producer reports a name
// and a weight (motionCore's ExpressionWeights), the avatar carries the binds,
// and this is where the two meet -- the consumer step the motion contract
// names, because it is the first layer that has the rig.
//
// Like the rest of vrmRetarget this takes plain values: the caller reads the
// avatar's /Asset/rig/Expressions prims off the stage and hands the binds in.
// A target is identified by whatever string the caller uses for it -- in
// practice the blend-shape or material prim path the expression's relationship
// pointed at -- and this library never resolves, opens or composes one.
#pragma once

#include "vrmRetarget/api.h"

#include "motionCore/Humanoid.h"

#include "pxr/base/gf/vec4f.h"

#include <cstddef>
#include <string>
#include <vector>

namespace vrmRetarget
{

// One morph-target bind of an expression: the target this expression drives,
// and how far it drives it when the expression is fully on.
struct MorphTargetBind
{
    // The caller's identifier for the blend shape -- the prim path the
    // avatar's `vrm:morphTargets` relationship targeted. Opaque here.
    std::string target;

    // The target's weight at expression weight 1, straight from
    // `vrm:morphTargetWeights`. Conventionally 1, and a VRM 0.x file that
    // spelled it 0..100 has already been normalized by the importer.
    float weight = 1.0f;
};

// One material-colour bind: a colour slot of a material, and the value that
// slot takes when the expression is fully on.
struct MaterialColorBind
{
    // The caller's identifier for the material -- the prim path the avatar's
    // `vrm:materialColorTargets` relationship targeted.
    std::string material;

    // The VRM colour slot: "color", "emissionColor", "shadeColor", "rimColor",
    // "outlineColor", "matcapColor". Carried verbatim, because the vocabulary
    // belongs to the material layer and not to this one.
    std::string colorType;

    // The slot's value at expression weight 1, from `vrm:materialColorValues`.
    pxr::GfVec4f targetValue = pxr::GfVec4f(1.0f);
};

// One expression of the target rig, as the avatar declared it.
struct ExpressionDefinition
{
    // The name exactly as the source VRM spelled it -- `vrm:expressionName`,
    // never the prim name. The two sides sanitize with private tables, so a
    // prim name is not a key: a Japanese name lands on a hashed fallback on the
    // importer's side and on a valid-identifier result on the clip's, and a
    // name that had to take a collision suffix diverges even in ASCII.
    std::string name;

    // `vrm:isBinary`. A binary expression has no intermediate state: its
    // resolved weight is 0 or 1 and never 0.4.
    bool isBinary = false;

    std::vector<MorphTargetBind> morphTargets;
    std::vector<MaterialColorBind> materialColors;
};

// The expressions a target rig declares, keyed by verbatim name.
//
// A name is declared once. Two prims answering to one name is not a rig this
// layer can resolve against -- it would bind whichever it reached first, which
// is a silent loss -- and it is the defect the importer's VRM152 diagnostic
// already refuses on the way in. `Add` reports it rather than deciding.
class VRMRETARGET_API ExpressionRig
{
public:
    ExpressionRig() = default;

    // Declares `definition`. Returns false -- and changes nothing -- when the
    // name is empty or already declared. An empty name cannot be joined on, so
    // accepting one would put a definition in the rig that nothing can reach.
    bool Add(ExpressionDefinition definition);

    // The definition for `name`, or null when the rig declares no such
    // expression. A pointer rather than a value, for the same reason
    // ExpressionWeights::Find answers with one: "not declared" and "declared
    // with no binds" are different rigs.
    const ExpressionDefinition* Find(const std::string& name) const noexcept;

    const std::vector<ExpressionDefinition>& GetExpressions() const noexcept
    {
        return _expressions;
    }
    std::size_t GetSize() const noexcept { return _expressions.size(); }
    bool IsEmpty() const noexcept { return _expressions.empty(); }

private:
    // Sorted by name, so a rig read from a stage in prim order and one read in
    // any other order are the same value.
    std::vector<ExpressionDefinition> _expressions;
};

// A blend-shape weight to author, accumulated over every expression that drives
// this target in one sample.
struct ResolvedMorphTarget
{
    std::string target;
    float weight = 0.0f;
};

// A material colour slot driven in one sample.
//
// The VRM rule for one bind is lerp(base, target, weight), and the base value
// is the material's -- which this library does not have and will not read. So
// the accumulation is carried in the form that does not need it:
//
//     final = base * (1 - totalWeight) + weightedTarget
//
// which is base + the sum of weight_i * (target_i - base), expanded. The caller
// supplies its own base through Apply().
struct ResolvedMaterialColor
{
    std::string material;
    std::string colorType;

    // The sum of the resolved weights of every expression driving this slot.
    float totalWeight = 0.0f;

    // The sum of weight_i * target_i over those same expressions.
    pxr::GfVec4f weightedTarget = pxr::GfVec4f(0.0f);

    // The slot's value for this sample, given the material's authored value.
    // Deliberately not clamped: a rig whose binds drive a slot outside [0, 1]
    // -- past it, or below it through a negative bind weight -- extrapolates
    // here and is reported as a warning, rather than being quietly corrected
    // into a value no bind asked for.
    VRMRETARGET_API pxr::GfVec4f Apply(const pxr::GfVec4f& base) const;
};

// What one sample's expression weights become on this rig.
//
// Both vectors carry an entry for every target any *reported* expression
// touches, including one whose resolved weight is 0. A reported zero is a
// statement -- "this expression is off now" -- and dropping it would leave the
// previous sample's weight standing on the target.
//
// A target no reported expression touches gets no entry at all, which is the
// other half of the same rule: an unreported name is not a zero weight, so this
// layer does not invent one for the binds behind it either.
struct ResolvedExpressions
{
    double timestamp = 0.0;

    // Sorted -- by target, and by (material, colorType) -- for the reason
    // ExpressionWeights is sorted: two producers that reported the same weights
    // in a different order are the same motion, so they must resolve to the
    // same value.
    std::vector<ResolvedMorphTarget> morphTargets;
    std::vector<ResolvedMaterialColor> materialColors;

    bool IsEmpty() const noexcept
    {
        return morphTargets.empty() && materialColors.empty();
    }
};

// Diagnostics a caller should surface rather than swallow. Resolving a clip
// onto a rig that does not declare what it animates is legal and useful -- a
// clip is authored against no avatar in particular -- but doing it silently is
// how a whole expression track goes missing without a line of output.
struct ExpressionDiagnostics
{
    // Reported by the producer, declared by no expression of this rig. Sorted,
    // each name once however many samples reported it.
    std::vector<std::string> unresolvedNames;

    // Reported outside [0, 1] and clamped here. The clip reader carries such a
    // weight verbatim on purpose and leaves the clamp to whoever applies it to
    // a rig, which is this layer; the operator still gets told. A weight that
    // is not a number is clamped to 0 and named here too: NaN compares false
    // against every bound, so left to the comparisons it would pass for a
    // weight already inside the range and reach the binds unreported.
    std::vector<std::string> clampedNames;

    std::vector<std::string> warnings;

    bool IsClean() const
    {
        return unresolvedNames.empty() && clampedNames.empty()
            && warnings.empty();
    }
};

struct ExpressionResolveOptions
{
    // Clamp a reported weight into [0, 1] before applying it, NaN to 0. The
    // VRMA specification says a weight outside the range is clamped; the reader
    // carries it verbatim and this is the layer the specification meant.
    // Turning it off resolves what the producer actually said, which is a
    // diagnostic mode rather than a rendering one -- a non-finite weight then
    // reaches the binds, and says so in the warnings rather than passing for a
    // clean resolve.
    bool clampWeights = true;

    // A binary expression's resolved weight is 1 at or above this and 0 below
    // it. Consulted only for a definition whose `isBinary` is set.
    float binaryThreshold = 0.5f;
};

// Joins a producer's named weights to a target rig's binds.
class VRMRETARGET_API ExpressionResolver
{
public:
    explicit ExpressionResolver(
        ExpressionRig rig,
        ExpressionResolveOptions options = ExpressionResolveOptions());

    const ExpressionRig& GetRig() const noexcept { return _rig; }
    const ExpressionResolveOptions& GetOptions() const noexcept
    {
        return _options;
    }

    // Resolves one sample's weights. `diagnostics` may be null; when it is not
    // it accumulates across calls, so a whole clip's report is one object.
    ResolvedExpressions Resolve(const motion::ExpressionWeights& weights,
                                ExpressionDiagnostics* diagnostics
                                    = nullptr) const;

    // The same, taking the weights off a pose and carrying its timestamp
    // through -- expressions live on the pose, so this is the call a consumer
    // walking a clip actually makes.
    ResolvedExpressions Resolve(const motion::HumanoidPose& pose,
                                ExpressionDiagnostics* diagnostics
                                    = nullptr) const;

    // The weight this rig applies for `name` given a `reported` one: the clamp
    // and the binary rounding, with no binds expanded. Returns false, leaving
    // `resolved` untouched, when the rig declares no such expression -- so a
    // caller can tell "resolves to 0" from "does not resolve".
    bool ResolveWeight(const std::string& name, float reported,
                       float* resolved) const;

private:
    ExpressionRig _rig;
    ExpressionResolveOptions _options;
};

} // namespace vrmRetarget
