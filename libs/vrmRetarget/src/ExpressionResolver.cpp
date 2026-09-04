// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/ExpressionResolver.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace vrmRetarget
{

namespace
{

// A value that is not a number is not a weight, and 0 is the only value that
// leaves the rig where it was -- so it is what a NaN clamps to. Written as an
// explicit test rather than left to the comparisons below, because every
// comparison against NaN is false: `weight < 0 || weight > 1` says a NaN is
// already inside [0, 1], and it would reach the binds, the totals and Apply()
// with the clamp reporting nothing.
bool
IsOutsideUnitRange(float weight)
{
    return !(weight >= 0.0f && weight <= 1.0f);
}

float
ClampUnit(float weight)
{
    if (std::isnan(weight)) {
        return 0.0f;
    }
    if (weight < 0.0f) {
        return 0.0f;
    }
    if (weight > 1.0f) {
        return 1.0f;
    }
    return weight;
}

// Diagnostics accumulate across a whole clip, so a name a thousand samples
// reported is one line and not a thousand.
void
RecordName(std::vector<std::string>& names, const std::string& name)
{
    const auto it = std::lower_bound(names.begin(), names.end(), name);
    if (it == names.end() || *it != name) {
        names.insert(it, name);
    }
}

// Warnings keep first-appearance order rather than being sorted, so they read
// as the sequence the resolve ran into. They carry no per-sample numbers, which
// is what lets the same warning from a later sample be recognized as the same.
void
RecordWarning(std::vector<std::string>& warnings, std::string warning)
{
    if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
        warnings.push_back(std::move(warning));
    }
}

// The override a definition declares for one category. Written as a lookup
// rather than three parallel code paths, so a category added to the
// specification is one table entry and not a fourth copy of the resolve.
ExpressionOverride
OverrideFor(const ExpressionDefinition& definition, ExpressionCategory category)
{
    switch (category) {
    case ExpressionCategory::Blink:
        return definition.overrideBlink;
    case ExpressionCategory::LookAt:
        return definition.overrideLookAt;
    case ExpressionCategory::Mouth:
        return definition.overrideMouth;
    case ExpressionCategory::None:
        break;
    }
    return ExpressionOverride::None;
}

const char*
CategoryName(ExpressionCategory category)
{
    switch (category) {
    case ExpressionCategory::Blink:
        return "blink";
    case ExpressionCategory::LookAt:
        return "lookAt";
    case ExpressionCategory::Mouth:
        return "mouth";
    case ExpressionCategory::None:
        break;
    }
    return "none";
}

// How far `mode` at `weight` suppresses the category it names. `block` is a
// switch -- any weight above zero suppresses completely -- and `blend` hands
// its own weight over, so the two agree at 1 and differ everywhere else.
float
OverrideRate(ExpressionOverride mode, float weight)
{
    switch (mode) {
    case ExpressionOverride::Block:
        return weight > 0.0f ? 1.0f : 0.0f;
    case ExpressionOverride::Blend:
        return weight;
    case ExpressionOverride::None:
        break;
    }
    return 0.0f;
}

// What one category's arbitration came to for this sample, and which expression
// decided it. The rate is the largest any expression asked for -- two
// expressions both suppressing the mouth do not suppress it twice -- and the
// name is carried so the report can say who, which is the whole difference
// between "your blink went flat" and "happy took your blink".
struct CategoryOverride
{
    float rate = 0.0f;
    std::string source;
};

} // namespace

ExpressionCategory
ExpressionCategoryOf(const std::string& name)
{
    // The VRM 1.0 preset spelling, which is also what a VRM 0.x rig arrives
    // in: the importer migrates `presetName` on the way through, so `blink_l`
    // is `blinkLeft` and `a` is `aa` by the time a definition exists.
    static const std::map<std::string, ExpressionCategory> kCategories = {
        {"blink", ExpressionCategory::Blink},
        {"blinkLeft", ExpressionCategory::Blink},
        {"blinkRight", ExpressionCategory::Blink},
        {"lookUp", ExpressionCategory::LookAt},
        {"lookDown", ExpressionCategory::LookAt},
        {"lookLeft", ExpressionCategory::LookAt},
        {"lookRight", ExpressionCategory::LookAt},
        {"aa", ExpressionCategory::Mouth},
        {"ih", ExpressionCategory::Mouth},
        {"ou", ExpressionCategory::Mouth},
        {"ee", ExpressionCategory::Mouth},
        {"oh", ExpressionCategory::Mouth},
    };
    const auto it = kCategories.find(name);
    return it == kCategories.end() ? ExpressionCategory::None : it->second;
}

ExpressionOverride
ParseExpressionOverride(const std::string& token, bool* recognized)
{
    if (recognized) {
        *recognized = true;
    }
    // An absent value and an explicit "none" are the same statement, so the
    // empty string is recognized rather than reported: a stage authors these
    // only when the source file stated one.
    if (token.empty() || token == "none") {
        return ExpressionOverride::None;
    }
    if (token == "block") {
        return ExpressionOverride::Block;
    }
    if (token == "blend") {
        return ExpressionOverride::Blend;
    }
    if (recognized) {
        *recognized = false;
    }
    return ExpressionOverride::None;
}

const char*
ExpressionOverrideToken(ExpressionOverride mode)
{
    switch (mode) {
    case ExpressionOverride::Block:
        return "block";
    case ExpressionOverride::Blend:
        return "blend";
    case ExpressionOverride::None:
        break;
    }
    return "none";
}

pxr::GfVec4f
ResolvedMaterialColor::Apply(const pxr::GfVec4f& base) const
{
    return base * (1.0f - totalWeight) + weightedTarget;
}

bool
ExpressionRig::Add(ExpressionDefinition definition)
{
    if (definition.name.empty()) {
        return false;
    }
    const auto it = std::lower_bound(
        _expressions.begin(), _expressions.end(), definition.name,
        [](const ExpressionDefinition& expression, const std::string& name) {
            return expression.name < name;
        });
    if (it != _expressions.end() && it->name == definition.name) {
        return false;
    }
    _expressions.insert(it, std::move(definition));
    return true;
}

const ExpressionDefinition*
ExpressionRig::Find(const std::string& name) const noexcept
{
    const auto it = std::lower_bound(
        _expressions.begin(), _expressions.end(), name,
        [](const ExpressionDefinition& expression, const std::string& key) {
            return expression.name < key;
        });
    if (it == _expressions.end() || it->name != name) {
        return nullptr;
    }
    return &*it;
}

ExpressionResolver::ExpressionResolver(ExpressionRig rig,
                                       ExpressionResolveOptions options)
    : _rig(std::move(rig))
    , _options(options)
{
}

bool
ExpressionResolver::ResolveWeight(const std::string& name, float reported,
                                  float* resolved) const
{
    const ExpressionDefinition* definition = _rig.Find(name);
    if (!definition) {
        return false;
    }
    float weight = _options.clampWeights ? ClampUnit(reported) : reported;
    if (definition->isBinary) {
        weight = weight >= _options.binaryThreshold ? 1.0f : 0.0f;
    }
    if (resolved) {
        *resolved = weight;
    }
    return true;
}

ResolvedExpressions
ExpressionResolver::Resolve(const motion::ExpressionWeights& weights,
                            ExpressionDiagnostics* diagnostics) const
{
    // Ordered containers, so the result is the rig's own order rather than the
    // order the producer happened to report its names in.
    std::map<std::string, float> morphTargets;
    std::map<std::pair<std::string, std::string>, ResolvedMaterialColor> colors;

    // The resolve is two passes over the sample rather than one, and the reason
    // is the arbitration: an expression's weight is not decided by its own
    // report alone, because another expression in the same sample may be
    // overriding the category it belongs to. So the first pass answers "what
    // did each reported name resolve to", the categories are settled between
    // them, and only then are the binds expanded.
    struct Contribution
    {
        const ExpressionDefinition* definition;
        float weight;
    };
    std::vector<Contribution> contributions;
    contributions.reserve(weights.entries.size());

    for (const motion::ExpressionWeight& reported : weights.entries) {
        const ExpressionDefinition* definition = _rig.Find(reported.name);
        if (!definition) {
            // The clip animates an expression this avatar does not declare.
            // That is not an error -- a clip is authored against no avatar in
            // particular -- but it is exactly the loss an operator has to be
            // able to see, so it is named rather than counted.
            if (diagnostics) {
                RecordName(diagnostics->unresolvedNames, reported.name);
            }
            continue;
        }

        float weight = reported.weight;
        if (IsOutsideUnitRange(weight)) {
            if (_options.clampWeights) {
                weight = ClampUnit(weight);
                if (diagnostics) {
                    RecordName(diagnostics->clampedNames, reported.name);
                }
            } else if (diagnostics && !std::isfinite(weight)) {
                // Verbatim mode resolves what the producer said, and a value
                // that is not finite is still what it said. It reaches the
                // binds -- but silently would make this mode indistinguishable
                // from a rig that resolved cleanly, so the report says so.
                RecordWarning(diagnostics->warnings,
                              "expression '" + reported.name
                                  + "' reported a weight that is not finite "
                                    "and clamping is off; it is carried into "
                                    "the binds unchanged");
            }
        }
        if (definition->isBinary) {
            weight = weight >= _options.binaryThreshold ? 1.0f : 0.0f;
        }
        contributions.push_back(Contribution{definition, weight});
    }

    // What each category came to for this sample. An override is read off the
    // *resolved* weight rather than the reported one, which is the one place
    // this differs from a naive reading of the specification and is deliberate:
    // a binary expression reported at 0.4 is off, and an expression that is off
    // cannot suppress anything. Reading the raw report would let it block a
    // blink while contributing nothing to the face itself.
    CategoryOverride overrides[3];
    const auto slotOf = [](ExpressionCategory category) {
        switch (category) {
        case ExpressionCategory::Blink:
            return 0;
        case ExpressionCategory::LookAt:
            return 1;
        case ExpressionCategory::Mouth:
            return 2;
        case ExpressionCategory::None:
            break;
        }
        return -1;
    };
    for (const Contribution& contribution : contributions) {
        for (const ExpressionCategory category :
             {ExpressionCategory::Blink, ExpressionCategory::LookAt,
              ExpressionCategory::Mouth}) {
            const float rate = OverrideRate(
                OverrideFor(*contribution.definition, category),
                contribution.weight);
            CategoryOverride& winner = overrides[slotOf(category)];
            if (rate > winner.rate) {
                winner.rate = rate;
                winner.source = contribution.definition->name;
            }
        }
    }

    for (const Contribution& contribution : contributions) {
        const ExpressionDefinition* definition = contribution.definition;
        float weight = contribution.weight;

        const ExpressionCategory category
            = ExpressionCategoryOf(definition->name);
        const int slot = slotOf(category);
        if (slot >= 0 && overrides[slot].rate > 0.0f) {
            // An expression that overrides the category it is itself in
            // suppresses itself, and is not exempted here. The rig said so --
            // and exempting it would make an override mean one thing for
            // `happy` and another for `blink`, which is a rule an operator
            // cannot predict from the file. Reported, because it is far more
            // likely to be an authoring slip than an intent.
            if (diagnostics
                && OverrideFor(*definition, category)
                    != ExpressionOverride::None
                && contribution.weight > 0.0f) {
                RecordWarning(diagnostics->warnings,
                              "expression '" + definition->name
                                  + "' overrides the '" + CategoryName(category)
                                  + "' expressions and is one of them, so it "
                                    "suppresses itself");
            }
            weight *= 1.0f - overrides[slot].rate;
            // A binary expression has no intermediate state, so a partial
            // suppression either leaves it standing or turns it off: a
            // half-shut eyelid is exactly what the flag says this rig cannot
            // show. The rounding is re-applied rather than skipped, which is
            // why it is here and not only above.
            if (definition->isBinary) {
                weight = weight >= _options.binaryThreshold ? 1.0f : 0.0f;
            }
            if (diagnostics && weight != contribution.weight) {
                RecordName(diagnostics->suppressedNames,
                           definition->name + " (by " + overrides[slot].source
                               + ")");
            }
        }

        for (const MorphTargetBind& bind : definition->morphTargets) {
            if (bind.target.empty()) {
                if (diagnostics) {
                    RecordWarning(diagnostics->warnings,
                                  "expression '" + definition->name
                                      + "' binds a morph target with no "
                                        "identifier; the bind is skipped");
                }
                continue;
            }
            // operator[] default-constructs the accumulator, which is what
            // gives a target touched at weight 0 its explicit zero entry.
            morphTargets[bind.target] += weight * bind.weight;
        }

        for (const MaterialColorBind& bind : definition->materialColors) {
            if (bind.material.empty()) {
                if (diagnostics) {
                    RecordWarning(diagnostics->warnings,
                                  "expression '" + definition->name
                                      + "' binds a material colour with no "
                                        "material; the bind is skipped");
                }
                continue;
            }
            if (bind.colorType.empty()) {
                // The slot is half the key. Accepting an empty one would merge
                // two binds of one material into a single accumulator and hand
                // the caller a colour it cannot map back to a shader input.
                if (diagnostics) {
                    RecordWarning(diagnostics->warnings,
                                  "expression '" + definition->name
                                      + "' binds a colour of material '"
                                      + bind.material
                                      + "' with no colour slot; the bind is "
                                        "skipped");
                }
                continue;
            }
            ResolvedMaterialColor& color
                = colors[std::make_pair(bind.material, bind.colorType)];
            color.material = bind.material;
            color.colorType = bind.colorType;
            color.totalWeight += weight;
            color.weightedTarget += bind.targetValue * weight;
        }
    }

    ResolvedExpressions result;
    result.morphTargets.reserve(morphTargets.size());
    for (const auto& entry : morphTargets) {
        ResolvedMorphTarget target;
        target.target = entry.first;
        target.weight = entry.second;
        // Both directions: a rig whose binds sum past 1, and -- through a
        // negative bind weight, or a negative report with clamping off -- one
        // that drives a target below 0. Neither is corrected, and a report that
        // named only the first would leave the other looking clean.
        if (diagnostics && IsOutsideUnitRange(target.weight)) {
            RecordWarning(diagnostics->warnings,
                          "morph target '" + target.target
                              + "' is driven outside [0, 1]; the sum is "
                                "carried through rather than clamped");
        }
        result.morphTargets.push_back(std::move(target));
    }

    result.materialColors.reserve(colors.size());
    for (const auto& entry : colors) {
        if (diagnostics && IsOutsideUnitRange(entry.second.totalWeight)) {
            RecordWarning(diagnostics->warnings,
                          "material colour '" + entry.second.material + "'."
                              + entry.second.colorType
                              + " is driven to a total weight outside [0, 1]; "
                                "Apply extrapolates rather than clamping");
        }
        result.materialColors.push_back(entry.second);
    }

    return result;
}

ResolvedExpressions
ExpressionResolver::Resolve(const motion::HumanoidPose& pose,
                            ExpressionDiagnostics* diagnostics) const
{
    ResolvedExpressions result = Resolve(pose.expressions, diagnostics);
    result.timestamp = pose.timestamp;
    return result;
}

} // namespace vrmRetarget
