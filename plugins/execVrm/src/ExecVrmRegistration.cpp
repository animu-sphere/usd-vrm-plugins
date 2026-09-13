// SPDX-License-Identifier: Apache-2.0
//
// execVrm's OpenExec registration: the rig value types, and the computations
// this bundle registers.
//
// The registrations live here and nothing else does. Everything a computation
// decides is in ExecVrmRig.cpp, over plain values; this file marshals exec's
// inputs into those calls and hands the results back -- execMotion's split,
// for execMotion's reason (motion policy §11.4).
//
// # Which schemas, and why these three
//
// 26.08 lets exactly one plugin declare a schema, and a second declarer loses
// every computation it registered there to a coding error at metadata read and
// a "computation not found" much later
// (docs/reports/openusd/26.08-openexec-mechanism.md §2). So the two exec bundles
// partition them (WORKSPACE.md §2): `execMotion` has `UsdSkelAnimation`, and
// this bundle has `UsdSkelSkeleton`, `UsdSkelBindingAPI` and the `Vrm*API`
// applied schemas. It declares only the three it registers on today;
// `UsdSkelBindingAPI` is there because UsdSkel's animation binding is
// inherited from an ancestor with that API applied (vrm.computeBindingPose).
//
// `VrmHumanoidAPI` is an *applied* schema, and that is what lets this bundle
// compute on the importer's humanoid prim at all: the prim is a `UsdGeomScope`,
// whose typed schema `execGeom` declares. A computation registered on an API
// schema resolves on any prim that has it applied, whatever the prim's type
// (the mechanism report §3, measured on a throwaway probe and exercised for
// real here).
//
// The schema is named by its **TfType**, `UsdVrmHumanoidAPI`, and exec resolves
// that name with `TfType::FindByName` when it reads this plugin's `plugInfo.json`
// -- so the name has to be declared by the time it is read, which is vrmSchema's
// plugInfo being registered, not vrmSchema's library being linked. This bundle
// links neither vrmSchema nor its generated class; it needs the bundle present
// in the session, which is what `requires.bundles` says (execVrm_humanoid runs
// once without it to measure what its absence looks like).
//
// # How a computation here refuses
//
// execMotion's way, for execMotion's reason: a `TF_RUNTIME_ERROR` naming the
// computation, and **no value at all** (`VdfContext::SetEmptyOutput`). An empty
// `TargetSkeleton` and an empty `HumanoidMap` are both legitimate answers -- a
// skeleton with no joints, a humanoid stating no bone -- so neither can stand
// for a refusal (docs/reports/openusd/26.08-openexec-root-motion.md §6).

#include "ExecVrmRig.h"

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"

#include "pxr/exec/ef/time.h"
#include "pxr/exec/exec/builtinComputations.h"
#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/exec/typeRegistry.h"
#include "pxr/exec/vdf/context.h"
#include "pxr/exec/vdf/readIterator.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/timeCode.h"

#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    // The plan's naming: `<layer>.<verb>`, and the layer is `vrm` because what
    // these computations apply is VRM semantics -- which is also what keeps
    // them apart from execMotion's `motion.*` on a stage that loads both.
    ((computeTargetSkeleton, "vrm.computeTargetSkeleton"))
    ((computeHumanoidMap, "vrm.computeHumanoidMap"))
    ((computeRestPoseCorrection, "vrm.computeRestPoseCorrection"))
    ((computeBoundPose, "vrm.computeBoundPose"))
    ((computeBindingPose, "vrm.computeBindingPose"))
    ((humanoidRetarget, "vrm.humanoidRetarget"))
    ((computeJointLocalTransforms, "vrm.computeJointLocalTransforms"))
    // execMotion's sampler, read by name across `skel:animationSource`. The one
    // computation this bundle reads that another bundle registers, which is
    // why `execMotion` is in `requires.bundles`.
    ((sampleAnimation, "motion.sampleAnimation"))
    // UsdSkelBindingAPI's relationship from a skeleton to its animation, and
    // the two names its fan-in arrives under.
    ((animationSource, "skel:animationSource"))
    ((animationSourcePaths, "skel:animationSource:paths"))
    ((animationSourcePoses, "skel:animationSource:poses"))
    // What the nearest ancestor with SkelBindingAPI applied binds: UsdSkel's
    // binding is inherited, so a clip bound on its SkelRoot is bound.
    ((inheritedPose, "skel:animationSource:inherited"))
    // UsdSkelSkeleton's own attributes, both `uniform`, each declared with its
    // ELEMENT type and read through an iterator (the migration audit §4).
    (joints)
    (restTransforms)
    // VrmHumanoidAPI's relationship to the skeleton its bones name joints of.
    ((skeleton, "vrm:skeleton"))
    // The two names the relationship's fan-in arrives under: it is traversed
    // twice, once for the skeletons and once for the paths that count them.
    ((skeletonPaths, "vrm:skeleton:paths"))
    ((skeletons, "vrm:skeleton:skeletons"))
    // The skeleton a clip was authored against, which is where the clip's rest
    // pose is. Not a vrmSchema property: a convention of this bundle, the way
    // `motion:timeCodesPerSecond` is execMotion's, and nothing authors it yet
    // (docs/roadmap/openexec-foundation.md §9).
    ((sourceSkeleton, "vrm:retarget:sourceSkeleton"))
    ((sourceSkeletonPaths, "vrm:retarget:sourceSkeleton:paths"))
    ((sourceSkeletons, "vrm:retarget:sourceSkeleton:skeletons"))
    ((sourcePoses, "vrm:retarget:sourceSkeleton:poses"))
    // Where the clip's root motion lands: `motion_retarget`'s four flags, as
    // attributes on the humanoid. Like the source relationship they are a
    // convention of this bundle and no schema's; one the prim does not have
    // keeps the library's default (ExecVrmRig.h, RootMotionStatements, for the
    // one the prim declares with no value).
    ((rootMotion, "vrm:retarget:rootMotion"))
    ((rootJoint, "vrm:retarget:rootJoint"))
    ((translationScale, "vrm:retarget:translationScale"))
    ((preserveTargetHeight, "vrm:retarget:preserveTargetHeight"))
);

TF_REGISTRY_FUNCTION(ExecTypeRegistry)
{
    // `vrmRetarget`'s own values, crossing unchanged -- the only shape under
    // which a node stays a wrapper. None is a `VtArray`, and all five are
    // equality comparable since this bundle asked for it: the exact
    // `operator==` motionCore's aggregates answered in v0.6.0 and
    // motionRuntime's `PoseSampleResult` for `motion.interpolatePose`, asked of
    // `vrmRetarget` for the first time. The last, `JointLocalTransforms`, is
    // the one type the library gained whole for this bundle rather than an
    // equality on a type it already had.
    ExecTypeRegistry::RegisterType(vrmRetarget::TargetSkeleton{});
    ExecTypeRegistry::RegisterType(vrmRetarget::HumanoidMap{});
    ExecTypeRegistry::RegisterType(vrmRetarget::RestPoseCorrection{});
    ExecTypeRegistry::RegisterType(vrmRetarget::RetargetedPose{});
    ExecTypeRegistry::RegisterType(vrmRetarget::JointLocalTransforms{});

    // execMotion's pose, registered here as well. `TargetedObjects<T>` checks
    // that `T` is registered when THIS bundle's computations are registered,
    // which is before anything has loaded execMotion's library -- so a bundle
    // that reads a value type has to register it itself. 26.08 allows several
    // plugins to register one type, provided the fallbacks are equal
    // (`VdfExecutionTypeRegistry::_Define`), and both are
    // `motion::HumanoidPose{}`.
    ExecTypeRegistry::RegisterType(motion::HumanoidPose{});
}

namespace {

std::string
_Quoted(const std::vector<SdfPath> &paths)
{
    std::string named;
    for (const SdfPath &path : paths) {
        named += named.empty() ? "<" : ", <";
        named += path.GetString();
        named += ">";
    }
    return named;
}

// The body of both binding computations: `vrm.computeBoundPose` on a skeleton
// and `vrm.computeBindingPose` on a prim with SkelBindingAPI applied. Each
// reads its own `skel:animationSource`, twice, and what the nearest ancestor
// with the API binds; the seam decides (ExecVrmRig.h, BoundPoseFor).
//
// Only the skeleton's node reports a prim that binds nothing. An ancestor that
// binds nothing is the ordinary case -- most SkelBindingAPI prims author only
// `skel:skeleton` -- and it answers no value silently, which the skeleton below
// it reads as "no ancestor binds one". An ancestor whose binding IS broken says
// so itself, as the skeleton's node does.
void
_ForwardBoundPose(const VdfContext &ctx, const char *computation,
                  bool reportUnbound)
{
    execvrm::BoundPoseInputs inputs;

    std::vector<SdfPath> targets;
    for (VdfReadIterator<SdfPath> path(ctx, _tokens->animationSourcePaths);
         !path.IsAtEnd(); ++path) {
        targets.push_back(*path);
    }
    inputs.animationTargetCount = targets.size();
    for (VdfReadIterator<motion::HumanoidPose> pose(
             ctx, _tokens->animationSourcePoses);
         !pose.IsAtEnd(); ++pose) {
        inputs.poses.push_back(*pose);
    }
    inputs.inherited =
        ctx.GetInputValuePtr<motion::HumanoidPose>(_tokens->inheritedPose);

    execvrm::BoundPoseOutcome outcome = execvrm::BoundPoseFor(inputs);
    if (outcome.pose) {
        ctx.SetOutput(std::move(*outcome.pose));
        return;
    }

    switch (outcome.refusal) {
    case execvrm::BoundPoseRefusal::NoAnimation:
        if (reportUnbound) {
            TF_RUNTIME_ERROR(
                "%s: 'skel:animationSource' reaches nothing on the stage, and "
                "no ancestor with SkelBindingAPI applied binds an animation "
                "either, so the skeleton is bound to no animation -- and an "
                "empty pose is not answered, because it would put the rig at "
                "rest for a misspelled binding; no pose was forwarded",
                computation);
        }
        break;
    case execvrm::BoundPoseRefusal::SeveralAnimations:
        TF_RUNTIME_ERROR(
            "%s: 'skel:animationSource' reaches %zu objects (%s), and a "
            "skeleton is bound to one animation; no pose was forwarded",
            computation, inputs.animationTargetCount,
            _Quoted(targets).c_str());
        break;
    case execvrm::BoundPoseRefusal::AnimationUnanswered:
        TF_RUNTIME_ERROR(
            "%s: 'skel:animationSource' targets %s, which answered no "
            "motion.sampleAnimation -- it is not a UsdSkelAnimation, "
            "execMotion is not in the session, or its sampler refused; no "
            "pose was forwarded",
            computation, _Quoted(targets).c_str());
        break;
    }
    ctx.SetEmptyOutput();
}

// Why a source skeleton is not a clip's rest, in the words both nodes that
// read one use -- so the retarget states the reason itself rather than
// pointing at the correction, which a request need not ask for.
std::string
_SourceRestReason(
    const std::string &named, execvrm::SourceRestRefusal refusal,
    const std::vector<std::pair<motion::HumanBone, std::string>> &offending)
{
    if (refusal == execvrm::SourceRestRefusal::NoHumanBone) {
        return "the source skeleton " + named
            + " names no human bone -- no joint's leaf is a bone of the "
              "vocabulary, so it is not a semantic clip's skeleton";
    }
    std::string bones;
    for (const auto &[bone, token] : offending) {
        bones += bones.empty() ? "" : ", ";
        bones += std::string(motion::HumanBoneName(bone));
        bones += " at '";
        bones += token;
        bones += "'";
    }
    return "the source skeleton " + named
        + " names one bone at more than one joint (" + bones
        + "), and which rest the clip meant cannot be known";
}

} // namespace

PXR_NAMESPACE_CLOSE_SCOPE

// ---------------------------------------------------------------------------
// vrm.computeTargetSkeleton -- the rig, as the retargeter reads it
// ---------------------------------------------------------------------------
//
// `vrmRetarget::TargetSkeleton` from the skeleton's `joints` and
// `restTransforms`. Both are `uniform`, and the node declares no `computeTime`,
// so it is reported to no time change -- which `execVrm_humanoid` asserts,
// because a rig that acquired a time dependency would be recomputed, with
// everything downstream of it, on every frame a clip plays.
//
// It is registered on `UsdSkelSkeleton`, a typed schema no shipped plugin
// declares; the bundle's other computations reach it across a relationship.
EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdSkelSkeleton)
{
    self.PrimComputation(_tokens->computeTargetSkeleton)
        .Callback<vrmRetarget::TargetSkeleton>(+[](const VdfContext &ctx) {
            execvrm::SkeletonRest rest;

            VdfReadIterator<TfToken> joint(ctx, _tokens->joints);
            rest.joints.reserve(joint.ComputeSize());
            for (; !joint.IsAtEnd(); ++joint) {
                rest.joints.push_back(joint->GetString());
            }

            // Neither input is `.Required()`, and neither could be made to
            // mean anything by it (the sampling report §2). What an unauthored
            // or blocked one looks like here is decided upstream of this
            // callback: both attributes are UsdSkelSkeleton's own, so each has
            // an input node whether or not the stage gives it a value, and a
            // node with no value is filled with ONE element of Sdf's fallback
            // for the type, beside a TF_WARN -- an empty token, an identity
            // matrix. The seam refuses by what those values are
            // (ExecVrmRig.h, SkeletonRefusal).
            VdfReadIterator<GfMatrix4d> matrix(ctx, _tokens->restTransforms);
            rest.restTransforms.reserve(matrix.ComputeSize());
            for (; !matrix.IsAtEnd(); ++matrix) {
                rest.restTransforms.push_back(*matrix);
            }

            execvrm::SkeletonOutcome outcome =
                execvrm::TargetSkeletonFromRest(rest);
            if (outcome.skeleton) {
                ctx.SetOutput(std::move(*outcome.skeleton));
                return;
            }

            switch (outcome.refusal) {
            case execvrm::SkeletonRefusal::RestTransformCount:
                // The offline tool assumes an identity rest pose here and
                // warns; a computation has no warning a consumer would see,
                // and identity rest rotations are numbers nobody can tell from
                // measured ones.
                TF_RUNTIME_ERROR(
                    "vrm.computeTargetSkeleton: the skeleton states %zu joints "
                    "and %zu 'restTransforms' (an unauthored or blocked array "
                    "arrives as one fallback matrix), and a rest pose is one "
                    "transform per joint; no skeleton was computed",
                    rest.joints.size(), rest.restTransforms.size());
                break;
            case execvrm::SkeletonRefusal::EmptyJointToken:
                TF_RUNTIME_ERROR(
                    "vrm.computeTargetSkeleton: a 'joints' entry is the empty "
                    "token, which names no joint -- and is what an unauthored "
                    "or blocked 'joints' arrives as; no skeleton was computed");
                break;
            }
            ctx.SetEmptyOutput();
        })
        .Inputs(
            AttributeValue<TfToken>(_tokens->joints),
            AttributeValue<GfMatrix4d>(_tokens->restTransforms));

    // -----------------------------------------------------------------------
    // vrm.computeBoundPose -- the pose of the animation this skeleton is bound to
    // -----------------------------------------------------------------------
    //
    // `motion.sampleAnimation` on whatever `skel:animationSource` targets,
    // forwarded -- the skeleton's own binding, or else the one the nearest
    // ancestor with SkelBindingAPI applied states (vrm.computeBindingPose,
    // below), which is UsdSkel's order. A retarget reaches a clip through ONE
    // relationship on the
    // humanoid, `vrm:retarget:sourceSkeleton`, because the skeleton is the one
    // prim both halves of a clip can be reached from -- and an exec input
    // traverses one relationship, so the second hop, UsdSkel's own binding, is
    // a computation on the skeleton. It is the first in this bundle whose value
    // another bundle computes: `motion.sampleAnimation` is execMotion's,
    // registered on `UsdSkelAnimation`, which this bundle may not declare.
    //
    // It declares no `computeTime`: the sampler's time dependence crosses the
    // link, as it does inside execMotion (the filtering report, section 3).
    //
    // Like every fan-in here the relationship is read twice, and here the
    // second read is also how this bundle notices that execMotion is not in the
    // session at all: an animation that provides no `motion.sampleAnimation` is
    // dropped from the fan-in while the network compiles, exactly as one that
    // is not an animation is.
    self.PrimComputation(_tokens->computeBoundPose)
        .Callback<motion::HumanoidPose>(+[](const VdfContext &ctx) {
            _ForwardBoundPose(ctx, "vrm.computeBoundPose",
                              /* reportUnbound = */ true);
        })
        .Inputs(
            Relationship(_tokens->animationSource)
                .TargetedObjects<SdfPath>(ExecBuiltinComputations->computePath)
                .InputName(_tokens->animationSourcePaths),
            Relationship(_tokens->animationSource)
                .TargetedObjects<motion::HumanoidPose>(_tokens->sampleAnimation)
                .InputName(_tokens->animationSourcePoses),
            NamespaceAncestor<motion::HumanoidPose>(_tokens->computeBindingPose)
                .InputName(_tokens->inheritedPose));
}

// ---------------------------------------------------------------------------
// vrm.computeBindingPose -- what a SkelBindingAPI prim binds at or beneath it
// ---------------------------------------------------------------------------
//
// UsdSkel's binding is inherited: `skel:animationSource` binds "Skeleton
// primitives at or beneath the location at which this property is defined",
// and `GetInheritedAnimationSource` walks up from a skeleton to the first prim
// that has SkelBindingAPI applied and authors one. This computation is that
// walk, one prim per step: registered on the applied `UsdSkelBindingAPI`, it
// answers its own binding, or else its nearest such ancestor's through
// `NamespaceAncestor` -- which finds the nearest ancestor PROVIDING the
// computation, so exactly the prims UsdSkel's `HasAPI` check admits. A clip
// bound on its SkelRoot, the layout many UsdSkel producers write, reaches the
// skeleton's `vrm.computeBoundPose` this way.
//
// `UsdSkelBindingAPI` is declared by no shipped plugin, so this bundle declares
// it (WORKSPACE.md §2). It resolves on a SkelRoot, whose typed schema execGeom
// declares, as `VrmHumanoidAPI` does on a Scope.
EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdSkelBindingAPI)
{
    self.PrimComputation(_tokens->computeBindingPose)
        .Callback<motion::HumanoidPose>(+[](const VdfContext &ctx) {
            _ForwardBoundPose(ctx, "vrm.computeBindingPose",
                              /* reportUnbound = */ false);
        })
        .Inputs(
            Relationship(_tokens->animationSource)
                .TargetedObjects<SdfPath>(ExecBuiltinComputations->computePath)
                .InputName(_tokens->animationSourcePaths),
            Relationship(_tokens->animationSource)
                .TargetedObjects<motion::HumanoidPose>(_tokens->sampleAnimation)
                .InputName(_tokens->animationSourcePoses),
            NamespaceAncestor<motion::HumanoidPose>(_tokens->computeBindingPose)
                .InputName(_tokens->inheritedPose));
}

// ---------------------------------------------------------------------------
// vrm.computeHumanoidMap -- which joint each human bone drives
// ---------------------------------------------------------------------------
//
// `vrmRetarget::HumanoidMap` from the `vrm:humanBones:<bone>` tokens the
// humanoid states, resolved against the skeleton `vrm:skeleton` targets. One
// library call per binding, and the refusals the seam documents.
//
// **Fifty-five inputs, declared in a loop.** `VrmHumanoidAPI` spells a binding
// as one attribute per bone rather than one array, so there is one input per
// bone of the vocabulary -- and `Inputs()` appends on every call, so the
// declaration can iterate the vocabulary instead of spelling it. The names are
// asserted to be the schema's own by `execVrm_humanoid`: an input declared for
// an attribute the schema does not define would simply never carry a value,
// and nothing would say so.
//
// **The skeleton arrives across a relationship and across schemas**: this node
// is registered on an applied API schema and reads a computation registered on
// a typed one, on another prim. Like `motion.blendPoses` it reads the
// relationship twice, because a target that answers nothing vanishes from the
// fan-in without a word.
//
// It declares no `computeTime`: every input is `uniform`.
EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdVrmHumanoidAPI)
{
    auto humanoidMap = self.PrimComputation(_tokens->computeHumanoidMap);
    humanoidMap.Callback<vrmRetarget::HumanoidMap>(+[](const VdfContext &ctx) {
        execvrm::HumanoidInputs inputs;

        std::vector<SdfPath> targets;
        for (VdfReadIterator<SdfPath> path(ctx, _tokens->skeletonPaths);
             !path.IsAtEnd(); ++path) {
            targets.push_back(*path);
        }
        inputs.skeletonTargetCount = targets.size();

        for (VdfReadIterator<vrmRetarget::TargetSkeleton> skeleton(
                 ctx, _tokens->skeletons);
             !skeleton.IsAtEnd(); ++skeleton) {
            inputs.skeletons.push_back(*skeleton);
        }

        const auto &names = execvrm::HumanBoneAttributeNames();
        for (std::size_t slot = 0; slot < names.size(); ++slot) {
            // Never null for a bone the applied schema defines -- which is all
            // fifty-five: an unauthored or blocked one arrives as the empty
            // token, Sdf's fallback, with a TF_WARN posted for it. Null only
            // for an input this registration forgot to declare, and then the
            // bone reads as unbound, which is what the empty token means too.
            if (const TfToken *const token =
                    ctx.GetInputValuePtr<TfToken>(names[slot])) {
                inputs.bindings.emplace_back(
                    static_cast<motion::HumanBone>(slot), token->GetString());
            }
        }

        execvrm::MapOutcome outcome = execvrm::HumanoidMapFor(inputs);
        if (outcome.map) {
            ctx.SetOutput(std::move(*outcome.map));
            return;
        }

        auto quoted = [](const std::vector<SdfPath> &paths) {
            std::string named;
            for (const SdfPath &path : paths) {
                named += named.empty() ? "<" : ", <";
                named += path.GetString();
                named += ">";
            }
            return named;
        };
        auto bones = [](const std::vector<
                         std::pair<motion::HumanBone, std::string>> &bound) {
            std::string named;
            for (const auto &[bone, token] : bound) {
                named += named.empty() ? "" : ", ";
                named += std::string(motion::HumanBoneName(bone));
                named += " -> '";
                named += token;
                named += "'";
            }
            return named;
        };

        switch (outcome.refusal) {
        case execvrm::MapRefusal::NoSkeleton:
            TF_RUNTIME_ERROR(
                "vrm.computeHumanoidMap: 'vrm:skeleton' reaches nothing on the "
                "stage, so there is no rig for the human bones to name joints "
                "of; no humanoid map was computed");
            break;
        case execvrm::MapRefusal::SeveralSkeletons:
            TF_RUNTIME_ERROR(
                "vrm.computeHumanoidMap: 'vrm:skeleton' reaches %zu objects "
                "(%s), and a humanoid's joint indices count into exactly one "
                "rig; no humanoid map was computed",
                inputs.skeletonTargetCount, quoted(targets).c_str());
            break;
        case execvrm::MapRefusal::SkeletonUnanswered:
            TF_RUNTIME_ERROR(
                "vrm.computeHumanoidMap: 'vrm:skeleton' targets %s, which "
                "answered no vrm.computeTargetSkeleton -- it is not a "
                "UsdSkelSkeleton, or its skeleton refused; no humanoid map was "
                "computed",
                quoted(targets).c_str());
            break;
        case execvrm::MapRefusal::UnknownJoint:
            TF_RUNTIME_ERROR(
                "vrm.computeHumanoidMap: the humanoid binds %s, and the "
                "skeleton %s contains no such joint -- a map without the bone "
                "would read as a humanoid that never named it; no humanoid map "
                "was computed",
                bones(outcome.offending).c_str(), quoted(targets).c_str());
            break;
        case execvrm::MapRefusal::DuplicateJoint:
            TF_RUNTIME_ERROR(
                "vrm.computeHumanoidMap: the humanoid binds %s, more than one "
                "bone to one joint, and a retarget would let one of them "
                "silently win; no humanoid map was computed",
                bones(outcome.offending).c_str());
            break;
        }
        ctx.SetEmptyOutput();
    });
    humanoidMap.Inputs(
        Relationship(_tokens->skeleton)
            .TargetedObjects<SdfPath>(ExecBuiltinComputations->computePath)
            .InputName(_tokens->skeletonPaths),
        Relationship(_tokens->skeleton)
            .TargetedObjects<vrmRetarget::TargetSkeleton>(
                _tokens->computeTargetSkeleton)
            .InputName(_tokens->skeletons));
    for (const TfToken &name : execvrm::HumanBoneAttributeNames()) {
        humanoidMap.Inputs(AttributeValue<TfToken>(name));
    }

    // -----------------------------------------------------------------------
    // vrm.computeRestPoseCorrection -- from the clip's rest onto this rig's
    // -----------------------------------------------------------------------
    //
    // `vrmRetarget::ComputeRestPoseCorrection` over three things, and the
    // first node here that reads a computation of its own prim, one across
    // `vrm:skeleton` and one across a second relationship -- the SAME
    // computation, `vrm.computeTargetSkeleton`, on two skeletons:
    //
    //   * the humanoid's `vrm.computeHumanoidMap`;
    //   * the target rig, across `vrm:skeleton`, read again because a map
    //     carries no skeleton -- the one its indices count into;
    //   * the source rig, across `vrm:retarget:sourceSkeleton`: the skeleton the
    //     clip was authored against, whose rest pose is the clip's. Reading it
    //     through `vrm.computeTargetSkeleton` means both rigs are decomposed by
    //     one implementation; the seam assigns the source's joints to bones.
    //
    // Why a relationship to the SKELETON rather than to the animation: a
    // `UsdSkelAnimation` states no rest, and the binding runs the other way --
    // the skeleton names its animation in `skel:animationSource` -- so the
    // skeleton is the one prim from which both halves of a clip are reachable.
    //
    // It declares no `computeTime`, and nothing it reads moves with time, so a
    // correction is computed once per rig edit and reported to no frame change
    // -- the reason it is a node of its own and not a step of the retarget.
    self.PrimComputation(_tokens->computeRestPoseCorrection)
        .Callback<vrmRetarget::RestPoseCorrection>(+[](const VdfContext &ctx) {
            execvrm::CorrectionInputs inputs;
            inputs.map = ctx.GetInputValuePtr<vrmRetarget::HumanoidMap>(
                _tokens->computeHumanoidMap);

            for (VdfReadIterator<vrmRetarget::TargetSkeleton> skeleton(
                     ctx, _tokens->skeletons);
                 !skeleton.IsAtEnd(); ++skeleton) {
                inputs.targets.push_back(*skeleton);
            }

            std::vector<SdfPath> sources;
            for (VdfReadIterator<SdfPath> path(ctx, _tokens->sourceSkeletonPaths);
                 !path.IsAtEnd(); ++path) {
                sources.push_back(*path);
            }
            inputs.sourceTargetCount = sources.size();
            for (VdfReadIterator<vrmRetarget::TargetSkeleton> skeleton(
                     ctx, _tokens->sourceSkeletons);
                 !skeleton.IsAtEnd(); ++skeleton) {
                inputs.sources.push_back(*skeleton);
            }

            execvrm::CorrectionOutcome outcome =
                execvrm::RestPoseCorrectionFor(inputs);
            if (outcome.correction) {
                ctx.SetOutput(std::move(*outcome.correction));
                return;
            }

            std::string named;
            for (const SdfPath &path : sources) {
                named += named.empty() ? "<" : ", <";
                named += path.GetString();
                named += ">";
            }

            switch (outcome.refusal) {
            case execvrm::CorrectionRefusal::RigUnanswered:
                // The map posted the reason; this says where it went.
                TF_RUNTIME_ERROR(
                    "vrm.computeRestPoseCorrection: the humanoid's "
                    "vrm.computeHumanoidMap, or the skeleton it counts into, "
                    "answered nothing; no correction was computed");
                break;
            case execvrm::CorrectionRefusal::NoSource:
                TF_RUNTIME_ERROR(
                    "vrm.computeRestPoseCorrection: "
                    "'vrm:retarget:sourceSkeleton' reaches nothing on the "
                    "stage, so there is no clip rest pose to correct from -- "
                    "and an identity rest is not assumed, because a target "
                    "naming no prim arrives exactly as no target does; no "
                    "correction was computed");
                break;
            case execvrm::CorrectionRefusal::SeveralSources:
                TF_RUNTIME_ERROR(
                    "vrm.computeRestPoseCorrection: "
                    "'vrm:retarget:sourceSkeleton' reaches %zu objects (%s), "
                    "and a correction is from exactly one rig; no correction "
                    "was computed",
                    inputs.sourceTargetCount, named.c_str());
                break;
            case execvrm::CorrectionRefusal::SourceUnanswered:
                TF_RUNTIME_ERROR(
                    "vrm.computeRestPoseCorrection: "
                    "'vrm:retarget:sourceSkeleton' targets %s, which answered "
                    "no vrm.computeTargetSkeleton -- it is not a "
                    "UsdSkelSkeleton, or its skeleton refused; no correction "
                    "was computed",
                    named.c_str());
                break;
            case execvrm::CorrectionRefusal::SourceRest:
                TF_RUNTIME_ERROR(
                    "vrm.computeRestPoseCorrection: %s; no correction was "
                    "computed",
                    _SourceRestReason(named, outcome.sourceRefusal,
                                      outcome.offending)
                        .c_str());
                break;
            }
            ctx.SetEmptyOutput();
        })
        .Inputs(
            Computation<vrmRetarget::HumanoidMap>(_tokens->computeHumanoidMap),
            Relationship(_tokens->skeleton)
                .TargetedObjects<vrmRetarget::TargetSkeleton>(
                    _tokens->computeTargetSkeleton)
                .InputName(_tokens->skeletons),
            Relationship(_tokens->sourceSkeleton)
                .TargetedObjects<SdfPath>(ExecBuiltinComputations->computePath)
                .InputName(_tokens->sourceSkeletonPaths),
            Relationship(_tokens->sourceSkeleton)
                .TargetedObjects<vrmRetarget::TargetSkeleton>(
                    _tokens->computeTargetSkeleton)
                .InputName(_tokens->sourceSkeletons));

    // -----------------------------------------------------------------------
    // vrm.humanoidRetarget -- one sample of the clip, on this rig
    // -----------------------------------------------------------------------
    //
    // `vrmRetarget::PoseRetargeter` over the rig, the map, the clip's rest and
    // the root-motion options, asked for the one pose the clip's skeleton is
    // bound to -- `motion_retarget`'s call, per sample. It reads:
    //
    //   * the humanoid's `vrm.computeHumanoidMap`, and the rig across
    //     `vrm:skeleton`, as the correction does;
    //   * across `vrm:retarget:sourceSkeleton`: the paths, the skeleton (for
    //     the clip's rest) and its `vrm.computeBoundPose` (for the pose) --
    //     one relationship, so the rest and the motion cannot come from two
    //     clips;
    //   * the four `vrm:retarget:*` root-motion statements.
    //
    // **Which pose**, the question `motion.blendPoses` left: the clip's own
    // sample, the pose `motion_retarget` retargets, because P0-6 compares the
    // two. A filtered pose or a blend reaches it only as a driver's override --
    // of `motion.sampleAnimation` on the clip, or of `vrm.computeBoundPose` on
    // its skeleton -- which the retarget suite measures crossing both bundles.
    //
    // It does NOT read `vrm.computeRestPoseCorrection`. `PoseRetargeter`
    // computes its own correction when it is constructed and takes none, so
    // this node recomputes, every frame, the value that node caches per rig
    // edit: the eighth boundary finding (ExecVrmRig.h, HumanoidRetargetFor).
    //
    // It reads `computeTime`, and not for the frame: the pose's own time
    // dependence already crosses two links to reach it, so declaring the
    // builtin costs no recompute it was not getting. It is read for the one
    // thing the pose cannot say -- whether the system names an instant at all
    // (ExecVrmRig.h, RetargetRefusal::NoInstant).
    auto retarget = self.PrimComputation(_tokens->humanoidRetarget);
    retarget.Callback<vrmRetarget::RetargetedPose>(+[](const VdfContext &ctx) {
        execvrm::RetargetInputs inputs;
        inputs.map = ctx.GetInputValuePtr<vrmRetarget::HumanoidMap>(
            _tokens->computeHumanoidMap);

        for (VdfReadIterator<vrmRetarget::TargetSkeleton> skeleton(
                 ctx, _tokens->skeletons);
             !skeleton.IsAtEnd(); ++skeleton) {
            inputs.targets.push_back(*skeleton);
        }

        std::vector<SdfPath> sources;
        for (VdfReadIterator<SdfPath> path(ctx, _tokens->sourceSkeletonPaths);
             !path.IsAtEnd(); ++path) {
            sources.push_back(*path);
        }
        inputs.sourceTargetCount = sources.size();
        for (VdfReadIterator<vrmRetarget::TargetSkeleton> skeleton(
                 ctx, _tokens->sourceSkeletons);
             !skeleton.IsAtEnd(); ++skeleton) {
            inputs.sources.push_back(*skeleton);
        }
        for (VdfReadIterator<motion::HumanoidPose> pose(
                 ctx, _tokens->sourcePoses);
             !pose.IsAtEnd(); ++pose) {
            inputs.poses.push_back(*pose);
        }

        // An attribute the prim does not have is null here and keeps the
        // library's default. One it declares with no value, or blocks, is NOT
        // null: it arrives as the type's fallback beside an executor warning,
        // schema property or not (ExecVrmRig.h, RootMotionStatements).
        if (const TfToken *const mode =
                ctx.GetInputValuePtr<TfToken>(_tokens->rootMotion)) {
            inputs.rootMotion.mode = mode->GetString();
        }
        if (const TfToken *const joint =
                ctx.GetInputValuePtr<TfToken>(_tokens->rootJoint)) {
            inputs.rootMotion.rootJoint = joint->GetString();
        }
        if (const float *const scale =
                ctx.GetInputValuePtr<float>(_tokens->translationScale)) {
            inputs.rootMotion.translationScale = *scale;
        }
        if (const bool *const preserve =
                ctx.GetInputValuePtr<bool>(_tokens->preserveTargetHeight)) {
            inputs.rootMotion.preserveTargetHeight = *preserve;
        }

        // `IsNumeric()` first: `GetValue()` is a coding error on the default
        // time code, which is what a request is armed at.
        inputs.hasInstant =
            ctx.GetInputValue<EfTime>(ExecBuiltinComputations->computeTime)
                .GetTimeCode()
                .IsNumeric();

        execvrm::RetargetOutcome outcome = execvrm::HumanoidRetargetFor(inputs);
        if (outcome.pose) {
            ctx.SetOutput(std::move(*outcome.pose));
            return;
        }

        std::string named;
        for (const SdfPath &path : sources) {
            named += named.empty() ? "<" : ", <";
            named += path.GetString();
            named += ">";
        }

        switch (outcome.refusal) {
        case execvrm::RetargetRefusal::RigUnanswered:
            TF_RUNTIME_ERROR(
                "vrm.humanoidRetarget: the humanoid's vrm.computeHumanoidMap, "
                "or the skeleton it counts into, answered nothing; no pose was "
                "retargeted");
            break;
        case execvrm::RetargetRefusal::RootMotion:
            switch (outcome.rootMotionRefusal) {
            case execvrm::RootMotionRefusal::UnknownMode:
                if (inputs.rootMotion.mode->empty()) {
                    TF_RUNTIME_ERROR(
                        "vrm.humanoidRetarget: 'vrm:retarget:rootMotion' is "
                        "the empty token, which names no mode -- and is what "
                        "the attribute arrives as when it is declared with no "
                        "value, or blocked; no pose was retargeted");
                } else {
                    TF_RUNTIME_ERROR(
                        "vrm.humanoidRetarget: 'vrm:retarget:rootMotion' is "
                        "'%s', which is none of hips, root and ignore; no pose "
                        "was retargeted",
                        inputs.rootMotion.mode->c_str());
                }
                break;
            case execvrm::RootMotionRefusal::NoRootJoint:
                TF_RUNTIME_ERROR(
                    "vrm.humanoidRetarget: 'vrm:retarget:rootMotion' is "
                    "'root' and 'vrm:retarget:rootJoint' names no joint to "
                    "receive it; no pose was retargeted");
                break;
            case execvrm::RootMotionRefusal::UnknownRootJoint:
                TF_RUNTIME_ERROR(
                    "vrm.humanoidRetarget: 'vrm:retarget:rootJoint' is '%s', "
                    "which is not a joint of the target skeleton; no pose was "
                    "retargeted",
                    inputs.rootMotion.rootJoint->c_str());
                break;
            case execvrm::RootMotionRefusal::TranslationScale:
                TF_RUNTIME_ERROR(
                    "vrm.humanoidRetarget: 'vrm:retarget:translationScale' is "
                    "not a finite number; no pose was retargeted");
                break;
            }
            break;
        case execvrm::RetargetRefusal::NoSource:
            TF_RUNTIME_ERROR(
                "vrm.humanoidRetarget: 'vrm:retarget:sourceSkeleton' reaches "
                "nothing on the stage, so there is no clip to retarget; no "
                "pose was retargeted");
            break;
        case execvrm::RetargetRefusal::SeveralSources:
            TF_RUNTIME_ERROR(
                "vrm.humanoidRetarget: 'vrm:retarget:sourceSkeleton' reaches "
                "%zu objects (%s), and a retarget is of exactly one clip; no "
                "pose was retargeted",
                inputs.sourceTargetCount, named.c_str());
            break;
        case execvrm::RetargetRefusal::SourceUnanswered:
            TF_RUNTIME_ERROR(
                "vrm.humanoidRetarget: 'vrm:retarget:sourceSkeleton' targets "
                "%s, which answered no vrm.computeTargetSkeleton -- it is not "
                "a UsdSkelSkeleton, or its skeleton refused; no pose was "
                "retargeted",
                named.c_str());
            break;
        case execvrm::RetargetRefusal::SourceRest:
            TF_RUNTIME_ERROR(
                "vrm.humanoidRetarget: %s; no pose was retargeted",
                _SourceRestReason(named, outcome.sourceRefusal,
                                  outcome.offending)
                    .c_str());
            break;
        case execvrm::RetargetRefusal::PoseUnanswered:
            // The bound pose posted the reason; this says where it went.
            TF_RUNTIME_ERROR(
                "vrm.humanoidRetarget: the source skeleton %s answered no "
                "vrm.computeBoundPose; no pose was retargeted",
                named.c_str());
            break;
        case execvrm::RetargetRefusal::NoInstant:
            TF_RUNTIME_ERROR(
                "vrm.humanoidRetarget: the system is at the default time code, "
                "which names no instant -- the clip's sampler answers an empty "
                "pose there, and retargeted it would be the rig's whole rest "
                "pose at 0 seconds; no pose was retargeted (call ChangeTime "
                "first)");
            break;
        }
        ctx.SetEmptyOutput();
    });
    retarget.Inputs(
        Computation<vrmRetarget::HumanoidMap>(_tokens->computeHumanoidMap),
        Relationship(_tokens->skeleton)
            .TargetedObjects<vrmRetarget::TargetSkeleton>(
                _tokens->computeTargetSkeleton)
            .InputName(_tokens->skeletons),
        Relationship(_tokens->sourceSkeleton)
            .TargetedObjects<SdfPath>(ExecBuiltinComputations->computePath)
            .InputName(_tokens->sourceSkeletonPaths),
        Relationship(_tokens->sourceSkeleton)
            .TargetedObjects<vrmRetarget::TargetSkeleton>(
                _tokens->computeTargetSkeleton)
            .InputName(_tokens->sourceSkeletons),
        Relationship(_tokens->sourceSkeleton)
            .TargetedObjects<motion::HumanoidPose>(_tokens->computeBoundPose)
            .InputName(_tokens->sourcePoses),
        AttributeValue<TfToken>(_tokens->rootMotion),
        AttributeValue<TfToken>(_tokens->rootJoint),
        AttributeValue<float>(_tokens->translationScale),
        AttributeValue<bool>(_tokens->preserveTargetHeight),
        Stage().Computation<EfTime>(ExecBuiltinComputations->computeTime));

    // -----------------------------------------------------------------------
    // vrm.computeJointLocalTransforms -- the retarget, as an animation sample
    // -----------------------------------------------------------------------
    //
    // The humanoid's own `vrm.humanoidRetarget` in the shape a
    // `UsdSkelAnimation` states at one time code: the rig's joint tokens, read
    // off `vrm.computeTargetSkeleton` across `vrm:skeleton` because a
    // retargeted pose does not carry them, the pose's translations and
    // rotations unchanged, and one identity scale per joint -- what
    // `motion_retarget` authors, and so what P0-6 compares against a bake.
    //
    // It declares no `computeTime`. The retarget's time dependence reaches it
    // across the link (the filtering report, section 3), and unlike the
    // retarget it has nothing to learn from the time code itself: a driver's
    // override of the retarget is a pose someone named, at whatever instant.
    //
    // The skeleton is read once, not counted: when the retarget answered, the
    // map it read had already refused anything but exactly one skeleton. Only a
    // driver's override of the retarget reaches this node past the map, and
    // then a skeleton that did not come back is refused like any other.
    self.PrimComputation(_tokens->computeJointLocalTransforms)
        .Callback<vrmRetarget::JointLocalTransforms>(+[](const VdfContext &ctx) {
            execvrm::JointTransformsInputs inputs;
            inputs.pose = ctx.GetInputValuePtr<vrmRetarget::RetargetedPose>(
                _tokens->humanoidRetarget);
            for (VdfReadIterator<vrmRetarget::TargetSkeleton> skeleton(
                     ctx, _tokens->skeletons);
                 !skeleton.IsAtEnd(); ++skeleton) {
                inputs.targets.push_back(*skeleton);
            }

            execvrm::JointTransformsOutcome outcome =
                execvrm::JointLocalTransformsFor(inputs);
            if (outcome.sample) {
                ctx.SetOutput(std::move(*outcome.sample));
                return;
            }

            switch (outcome.refusal) {
            case execvrm::JointTransformsRefusal::PoseUnanswered:
                // The retarget posted the reason; this says where it went.
                TF_RUNTIME_ERROR(
                    "vrm.computeJointLocalTransforms: the humanoid's "
                    "vrm.humanoidRetarget answered nothing; no joint "
                    "transforms were computed");
                break;
            case execvrm::JointTransformsRefusal::RigUnanswered:
                TF_RUNTIME_ERROR(
                    "vrm.computeJointLocalTransforms: 'vrm:skeleton' brought "
                    "back %zu skeletons, so there are no joint tokens to "
                    "order the pose by; no joint transforms were computed",
                    inputs.targets.size());
                break;
            case execvrm::JointTransformsRefusal::JointCount:
                TF_RUNTIME_ERROR(
                    "vrm.computeJointLocalTransforms: the retargeted pose has "
                    "%zu rotations and %zu translations, and the rig has %zu "
                    "joints -- a pose that was not retargeted onto this rig; "
                    "no joint transforms were computed",
                    outcome.rotations, outcome.translations, outcome.joints);
                break;
            }
            ctx.SetEmptyOutput();
        })
        .Inputs(
            Computation<vrmRetarget::RetargetedPose>(_tokens->humanoidRetarget),
            Relationship(_tokens->skeleton)
                .TargetedObjects<vrmRetarget::TargetSkeleton>(
                    _tokens->computeTargetSkeleton)
                .InputName(_tokens->skeletons));
}
