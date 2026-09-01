// SPDX-License-Identifier: Apache-2.0
#include "vrmRetarget/ExpressionResolver.h"

#include <algorithm>
#include <map>
#include <utility>

namespace vrmRetarget
{

namespace
{

float
ClampUnit(float weight)
{
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

} // namespace

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
        if (_options.clampWeights && (weight < 0.0f || weight > 1.0f)) {
            weight = ClampUnit(weight);
            if (diagnostics) {
                RecordName(diagnostics->clampedNames, reported.name);
            }
        }
        if (definition->isBinary) {
            weight = weight >= _options.binaryThreshold ? 1.0f : 0.0f;
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
        if (diagnostics && target.weight > 1.0f) {
            RecordWarning(diagnostics->warnings,
                          "morph target '" + target.target
                              + "' is driven past a total weight of 1; the "
                                "sum is carried through rather than clamped");
        }
        result.morphTargets.push_back(std::move(target));
    }

    result.materialColors.reserve(colors.size());
    for (const auto& entry : colors) {
        if (diagnostics && entry.second.totalWeight > 1.0f) {
            RecordWarning(diagnostics->warnings,
                          "material colour '" + entry.second.material + "'."
                              + entry.second.colorType
                              + " is driven past a total weight of 1; Apply "
                                "extrapolates rather than clamping");
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
