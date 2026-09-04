// SPDX-License-Identifier: Apache-2.0
//
// motion_retarget — Motion Phase C's bake tool.
//
// It is the composition point, not the algorithm: `vrmRetarget` does the
// retargeting over plain values, `StageIo` does everything that touches a
// stage, and this file wires the two together and reports what happened.
#include "Options.h"
#include "StageIo.h"

#include "vrmRetarget/ExpressionResolver.h"
#include "vrmRetarget/LookAtEvaluator.h"
#include "vrmRetarget/PoseRetargeter.h"

#include "motionRuntime/Resample.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace
{

void
ReportWarnings(const std::vector<std::string>& warnings, bool quiet)
{
    if (quiet) {
        return;
    }
    for (const std::string& warning : warnings) {
        std::cerr << "motion_retarget: warning: " << warning << "\n";
    }
}

std::string
JoinBones(const std::vector<motion::HumanBone>& bones)
{
    std::string joined;
    for (const motion::HumanBone bone : bones) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += std::string(motion::HumanBoneName(bone));
    }
    return joined;
}

std::string
JoinNames(const std::vector<std::string>& names)
{
    std::string joined;
    for (const std::string& name : names) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += "'" + name + "'";
    }
    return joined;
}

// The distinct material colour slots a clip drives on this rig.
//
// They are resolved and deliberately not authored: a colour slot is a material
// input, and the material layer owns what an MToon or a UsdPreviewSurface calls
// it. Reporting the count is what keeps that a stated boundary rather than a
// silent omission -- an operator whose clip turns a face red sees why it did
// not.
std::size_t
CountMaterialColors(
    const std::vector<vrmRetarget::ResolvedExpressions>& expressions)
{
    std::set<std::pair<std::string, std::string>> slots;
    for (const vrmRetarget::ResolvedExpressions& sample : expressions) {
        for (const vrmRetarget::ResolvedMaterialColor& color :
             sample.materialColors) {
            slots.emplace(color.material, color.colorType);
        }
    }
    return slots.size();
}

// Evaluates the clip's gaze against the avatar's own look-at configuration,
// one result per retargeted sample.
//
// The head transform is what makes this a step of its own rather than part of
// the retarget: a gaze needs to know where the head *is*, and that is only
// known once the body has been expanded onto this rig. So it reads the head out
// of the retargeted pose -- in the skeleton space the clip's target point is
// already in, since a clip's target and its hips translation are stated in the
// same space.
std::vector<vrmRetarget::ResolvedLookAt>
EvaluateGaze(const motionRetargetTool::Avatar& avatar,
             const motionRetargetTool::Clip& clip,
             const motion::HumanoidAnimation& source,
             const vrmRetarget::RetargetedAnimation& retargeted,
             vrmRetarget::LookAtDiagnostics* diagnostics)
{
    std::vector<vrmRetarget::ResolvedLookAt> gaze;
    const int head = avatar.map.GetJointIndex(motion::HumanBone::Head);
    if (head == vrmRetarget::HumanoidMap::kUnmapped) {
        // Without a head there is no place for the eyes to be, so there is
        // nothing to evaluate against -- and the retarget itself already
        // reports the head as a missing required bone.
        diagnostics->warnings.push_back(
            "the target rig maps no joint for 'head', so the clip's look-at "
            "target cannot be turned into a gaze");
        return gaze;
    }

    vrmRetarget::LookAtEvaluateOptions options;
    options.clipOffsetFromHeadBone = clip.lookAtOffsetFromHeadBone;
    const vrmRetarget::LookAtEvaluator evaluator(avatar.lookAtRig, options);

    const std::size_t count =
        std::min(source.samples.size(), retargeted.samples.size());
    gaze.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        vrmRetarget::LookAtHead where;
        if (!vrmRetarget::GetJointWorldTransform(avatar.skeleton,
                                                 retargeted.samples[i], head,
                                                 &where.orientation,
                                                 &where.position)) {
            diagnostics->warnings.push_back(
                "the target rig's head joint has no resolvable transform, so "
                "the clip's look-at target cannot be turned into a gaze");
            gaze.clear();
            return gaze;
        }
        gaze.push_back(evaluator.Evaluate(source.samples[i], where,
                                          diagnostics));
    }
    return gaze;
}

// Writes the resolved eye rotations over the retargeted samples.
//
// An eye rotation is a joint rotation, so this is the same array the body was
// expanded into and the existing authoring step carries it to the stage
// unchanged. The composition is `gaze * rest`: the range maps state yaw and
// pitch about the head's own axes, which is the eye joint's parent space, so
// the gaze is applied there rather than inside the eye's rest frame. The two
// coincide for every VRM rig whose eye joints rest unrotated -- which is the
// usual shape, a glTF node with a translation and nothing else -- and a rig
// where they do not is told about rather than silently read one way.
std::size_t
ApplyEyeRotations(const motionRetargetTool::Avatar& avatar,
                  const motion::HumanoidAnimation& source,
                  const std::vector<vrmRetarget::ResolvedLookAt>& gaze,
                  vrmRetarget::RetargetedAnimation* retargeted,
                  std::vector<std::string>* warnings)
{
    std::map<std::string, int> jointIndex;
    for (const std::string& token : {avatar.lookAtRig.leftEyeJoint,
                                     avatar.lookAtRig.rightEyeJoint}) {
        if (token.empty()) {
            continue;
        }
        const int index = avatar.skeleton.FindJoint(token);
        if (index == vrmRetarget::TargetSkeleton::kNoParent) {
            continue;
        }
        jointIndex.emplace(token, index);
        const pxr::GfQuatf& rest =
            avatar.skeleton.GetJoints()[static_cast<std::size_t>(index)]
                .restRotation;
        // Orientation, not representation: the dot against identity is 1 for
        // an unrotated rest and -1 for the other spelling of the same one.
        if (std::fabs(pxr::GfDot(rest, pxr::GfQuatf(1.0f))) < 1.0f - 1e-5f) {
            warnings->push_back(
                "eye joint '" + token
                + "' does not rest unrotated, so its gaze is composed onto "
                  "that rest rotation in the head's axes rather than in the "
                  "eye's own");
        }

        // The clip may already be driving this joint. An eye is a human bone
        // like any other, so a rig that binds `leftEye` in its humanoid map
        // and a clip that animates it produce a rotation here that the gaze is
        // about to overwrite -- and losing a channel the clip explicitly
        // authored is exactly the silence the expression collision one branch
        // over is reported for. Say it once, naming the bone.
        for (std::size_t slot = 0; slot < motion::HumanBoneCount; ++slot) {
            const auto bone = static_cast<motion::HumanBone>(slot);
            if (avatar.map.GetJointIndex(bone) != index) {
                continue;
            }
            const bool driven = std::any_of(
                source.samples.begin(), source.samples.end(),
                [slot](const motion::HumanoidPose& pose) {
                    return pose.validRotations.test(slot);
                });
            if (driven) {
                warnings->push_back(
                    "the clip animates '" + std::string(motion::HumanBoneName(bone))
                    + "' and the avatar aims that joint with its look-at; the "
                      "gaze this rig resolves wins");
            }
        }
    }
    if (jointIndex.empty()) {
        return 0;
    }

    // The last gaze each eye was given, so a sample that says nothing leaves it
    // standing. That is the rule a blocked expression weight is already under
    // -- a USD value block means "no statement at this time", not "back to
    // neutral" -- and the two authoring routes have to agree about it, or the
    // same clip would freeze an expression-driven rig's eyes and snap a
    // bone-driven one's. Until the clip gives a first gaze the map is empty and
    // nothing is written, so the eye stays where the retarget put it.
    std::map<int, pxr::GfQuatf> held;
    std::set<int> driven;
    for (std::size_t i = 0; i < gaze.size() && i < retargeted->samples.size();
         ++i) {
        if (gaze[i].hasGaze) {
            for (const vrmRetarget::LookAtEyeRotation& eye :
                 gaze[i].eyeRotations) {
                const auto found = jointIndex.find(eye.joint);
                if (found == jointIndex.end()) {
                    continue;
                }
                const auto slot = static_cast<std::size_t>(found->second);
                const pxr::GfQuatf& rest =
                    avatar.skeleton.GetJoints()[slot].restRotation;
                held[found->second] = (eye.rotation * rest).GetNormalized();
            }
        }
        for (const auto& entry : held) {
            retargeted->samples[i]
                .rotations[static_cast<std::size_t>(entry.first)] = entry.second;
            driven.insert(entry.first);
        }
    }
    return driven.size();
}

} // namespace

int
main(int argc, char** argv)
{
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    motionRetargetTool::Options options;
    bool showHelp = false;
    std::string error;
    if (!motionRetargetTool::ParseOptions(arguments, &options, &showHelp,
                                          &error)) {
        std::cerr << "motion_retarget: " << error << "\n\n"
                  << motionRetargetTool::GetUsage();
        return 2;
    }
    if (showHelp) {
        std::fputs(motionRetargetTool::GetUsage(), stdout);
        return 0;
    }

    std::map<std::string, std::string> extraMappings;
    if (!options.humanoidMapPath.empty()
        && !motionRetargetTool::ReadHumanoidMapFile(options.humanoidMapPath,
                                                    &extraMappings, &error)) {
        std::cerr << "motion_retarget: " << error << "\n";
        return 1;
    }

    motionRetargetTool::Avatar avatar;
    if (!motionRetargetTool::ReadAvatar(options.avatarPath,
                                        options.targetSkeletonPath,
                                        extraMappings, &avatar, &error)) {
        std::cerr << "motion_retarget: " << error << "\n";
        return 1;
    }
    ReportWarnings(avatar.warnings, options.quiet);

    motionRetargetTool::Clip clip;
    if (!motionRetargetTool::ReadClip(options.animationPath,
                                      options.clipSkeletonPath, &clip, &error)) {
        std::cerr << "motion_retarget: " << error << "\n";
        return 1;
    }
    ReportWarnings(clip.warnings, options.quiet);

    // Resample once, here, rather than inside the retargeter. The body and the
    // face are two expansions of the same samples, and what keeps them on one
    // timeline is that they expand the same list: a retargeter-side resample
    // would move the joints onto a uniform timeline while the expressions
    // stayed on the clip's key times, and the two would meet at neither.
    motion::HumanoidAnimation resampled;
    const motion::HumanoidAnimation* source = &clip.animation;
    if (options.resampleRate > 0.0) {
        resampled = motion::Resample(clip.animation, options.resampleRate);
        source = &resampled;
    }

    vrmRetarget::RetargetOptions retargetOptions;
    retargetOptions.rootMotion = options.rootMotion;
    if (retargetOptions.rootMotion.mode
        == vrmRetarget::RootMotionMode::RootJoint) {
        const int index = avatar.skeleton.FindJoint(options.rootJointToken);
        if (index < 0) {
            std::cerr << "motion_retarget: --root-joint '"
                      << options.rootJointToken
                      << "' is not a joint of the target skeleton\n";
            return 1;
        }
        retargetOptions.rootMotion.rootJointIndex = index;
    }

    const vrmRetarget::PoseRetargeter retargeter(avatar.skeleton, avatar.map,
                                                 clip.restPose,
                                                 retargetOptions);
    vrmRetarget::RetargetDiagnostics diagnostics;
    // Not const: a bone-driven look-at writes its eye rotations into these very
    // arrays, which is what lets the gaze reach the stage through the joint
    // authoring that already exists rather than through a second path.
    vrmRetarget::RetargetedAnimation retargeted =
        retargeter.Retarget(*source, &diagnostics);

    if (!options.quiet) {
        if (!diagnostics.missingRequiredBones.empty()) {
            std::cerr << "motion_retarget: warning: the target rig maps no "
                         "joint for required bones: "
                      << JoinBones(diagnostics.missingRequiredBones) << "\n";
        }
        ReportWarnings(diagnostics.warnings, options.quiet);
    }

    // The gaze, between the body and the face because it needs the first and
    // may feed the second: a bone-driven look-at writes eye rotations over the
    // retargeted joints, and an expression-driven one produces the very weights
    // the expression resolve below consumes.
    std::vector<vrmRetarget::ResolvedLookAt> gaze;
    vrmRetarget::LookAtDiagnostics lookAtDiagnostics;
    std::size_t eyeJointsDriven = 0;
    const bool gazeDrivesExpressions =
        avatar.lookAtRig.type == vrmRetarget::LookAtType::Expression;
    // An expression-driven gaze reaches the stage as blend-shape weights and by
    // no other route, so --no-expressions takes it along with the face. The
    // evaluation is skipped rather than performed and discarded, because the
    // summary counts what it evaluated: a run that reports samples gazing has
    // to have authored a gaze.
    const bool gazeHasSomewhereToGo =
        options.expressions || !gazeDrivesExpressions;
    if (options.lookAt && avatar.hasLookAt && gazeHasSomewhereToGo) {
        gaze = EvaluateGaze(avatar, clip, *source, retargeted,
                            &lookAtDiagnostics);
        if (!gazeDrivesExpressions) {
            eyeJointsDriven = ApplyEyeRotations(avatar, *source, gaze,
                                                &retargeted,
                                                &lookAtDiagnostics.warnings);
        }
    }
    if (!options.quiet) {
        if (clip.hasLookAtTrack && !avatar.hasLookAt) {
            std::cerr << "motion_retarget: warning: the clip names a look-at "
                         "target and the avatar declares no look-at "
                         "configuration, so no gaze was authored\n";
        }
        if (options.lookAt && avatar.hasLookAt && clip.hasLookAtTrack
            && !gazeHasSomewhereToGo) {
            std::cerr << "motion_retarget: warning: this avatar aims its eyes "
                         "with expressions, which --no-expressions does not "
                         "author, so the clip's look-at target was not baked "
                         "either\n";
        }
        if (options.lookAt && avatar.hasLookAt && !clip.hasLookAtTrack) {
            // Not a defect on either side: an avatar states how its eyes work
            // whether or not a given clip uses them.
            std::cerr << "motion_retarget: note: the avatar declares a look-at "
                         "configuration and the clip names no target\n";
        }
        ReportWarnings(lookAtDiagnostics.warnings, options.quiet);
    }

    // The face half of the same samples.
    //
    // A rig that declares no expression at all is resolved against anyway,
    // rather than short-circuited: it resolves nothing, and the point is that
    // it *says* so. That is the total-loss case -- every name the clip animates
    // going missing at once -- and it is the one a bake must not pass over in
    // silence, so the empty rig takes the same path as a rig missing one name.
    //
    // An expression-driven gaze joins in here rather than beside: `lookLeft` is
    // an expression of the rig like `happy` is, so folding the four gaze
    // weights into the sample's own before the resolve reaches the avatar's
    // binds through the one accumulator that already sums expressions -- and a
    // rig that binds a gaze expression and a face expression to the same morph
    // target gets the sum, which is the rule rather than a coincidence.
    std::vector<vrmRetarget::ResolvedExpressions> expressions;
    vrmRetarget::ExpressionDiagnostics expressionDiagnostics;
    if (options.expressions) {
        const vrmRetarget::ExpressionResolver resolver(avatar.expressionRig);
        expressions.reserve(source->samples.size());
        for (std::size_t i = 0; i < source->samples.size(); ++i) {
            const motion::HumanoidPose& pose = source->samples[i];
            if (!gazeDrivesExpressions || i >= gaze.size()
                || !gaze[i].hasGaze) {
                expressions.push_back(resolver.Resolve(pose,
                                                       &expressionDiagnostics));
                continue;
            }
            motion::ExpressionWeights weights = pose.expressions;
            for (const motion::ExpressionWeight& gazeWeight :
                 gaze[i].expressions.entries) {
                if (!weights.Set(gazeWeight.name, gazeWeight.weight)) {
                    // The clip drives a gaze expression by name *and* names a
                    // look-at target. Both are legitimate authoring, and one
                    // has to win; the gaze does, because it is the value this
                    // rig's own curves produced.
                    //
                    // Once per name, not once per sample: a collision is a
                    // fact about the clip, and a thousand-sample take would
                    // otherwise put four thousand identical lines on stderr.
                    // Every other warning path in this feature de-duplicates
                    // for the same reason.
                    const std::string collision =
                        "the clip animates expression '" + gazeWeight.name
                        + "' and also names a look-at target; the gaze this "
                          "rig resolves wins";
                    if (std::find(expressionDiagnostics.warnings.begin(),
                                  expressionDiagnostics.warnings.end(),
                                  collision)
                        == expressionDiagnostics.warnings.end()) {
                        expressionDiagnostics.warnings.push_back(collision);
                    }
                }
            }
            vrmRetarget::ResolvedExpressions resolved =
                resolver.Resolve(weights, &expressionDiagnostics);
            // The weights overload carries no timestamp, and the samples have
            // to stay on the clip's instants for the authoring step to line
            // them up with the joints.
            resolved.timestamp = pose.timestamp;
            expressions.push_back(std::move(resolved));
        }
    }

    if (!options.quiet) {
        if (!expressionDiagnostics.unresolvedNames.empty()) {
            std::cerr << "motion_retarget: warning: the clip animates "
                         "expressions the avatar does not declare: "
                      << JoinNames(expressionDiagnostics.unresolvedNames)
                      << "\n";
        }
        if (!expressionDiagnostics.clampedNames.empty()) {
            std::cerr << "motion_retarget: warning: expression weights "
                         "outside [0, 1] were clamped: "
                      << JoinNames(expressionDiagnostics.clampedNames) << "\n";
        }
        if (!expressionDiagnostics.suppressedNames.empty()) {
            // A note rather than a warning: this is the avatar's own
            // `overrideBlink` / `overrideLookAt` / `overrideMouth` rule being
            // obeyed, and it is the one thing the weights themselves cannot
            // show -- a producer whose blink track went flat would otherwise
            // have nowhere to look.
            std::cerr << "motion_retarget: note: the avatar's expression "
                         "overrides suppressed: "
                      << JoinNames(expressionDiagnostics.suppressedNames)
                      << "\n";
        }
        ReportWarnings(expressionDiagnostics.warnings, options.quiet);
        const std::size_t materialColors = CountMaterialColors(expressions);
        if (materialColors != 0) {
            std::cerr << "motion_retarget: warning: the clip drives "
                      << materialColors
                      << " material colour slot(s) of this rig; "
                         "motion_retarget authors blend-shape weights only, "
                         "so they are not written\n";
        }
    }

    motionRetargetTool::WriteResult written;
    if (!motionRetargetTool::WriteRetargetedAnimation(
            options.outputPath, avatar, clip, retargeted, expressions,
            options.animationName, &written, &error)) {
        std::cerr << "motion_retarget: " << error << "\n";
        return 1;
    }
    ReportWarnings(written.warnings, options.quiet);

    if (!options.quiet) {
        std::cout << "motion_retarget: wrote " << options.outputPath << " ("
                  << retargeted.samples.size() << " samples over "
                  << retargeted.joints.size() << " joints, "
                  << avatar.map.GetMappedCount() << " humanoid bones bound";
        if (written.blendShapesAuthored != 0) {
            std::cout << ", " << written.blendShapesAuthored
                      << " blend shapes driven";
        }
        if (eyeJointsDriven != 0) {
            std::cout << ", " << eyeJointsDriven << " eye joints aimed";
        }
        const std::size_t gazing =
            lookAtDiagnostics.samplesEvaluated
            - lookAtDiagnostics.samplesWithoutTarget;
        if (gazing != 0) {
            std::cout << ", " << gazing << " samples gazing";
        }
        std::cout << ")\n";
    }
    return 0;
}
