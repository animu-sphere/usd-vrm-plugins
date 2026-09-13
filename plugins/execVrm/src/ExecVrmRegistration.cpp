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
// # Which schemas, and why these two
//
// 26.08 lets exactly one plugin declare a schema, and a second declarer loses
// every computation it registered there to a coding error at metadata read and
// a "computation not found" much later
// (docs/reports/openusd/26.08-openexec-mechanism.md §2). So the two exec bundles
// partition them (WORKSPACE.md §2): `execMotion` has `UsdSkelAnimation`, and
// this bundle has `UsdSkelSkeleton` and the `Vrm*API` applied schemas. It
// declares only the two it registers on today.
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

#include "pxr/exec/exec/builtinComputations.h"
#include "pxr/exec/exec/registerSchema.h"
#include "pxr/exec/exec/typeRegistry.h"
#include "pxr/exec/vdf/context.h"
#include "pxr/exec/vdf/readIterator.h"

#include "pxr/usd/sdf/path.h"

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
);

TF_REGISTRY_FUNCTION(ExecTypeRegistry)
{
    // `vrmRetarget`'s own values, crossing unchanged -- the only shape under
    // which a node stays a wrapper. Neither is a `VtArray`, and both are
    // equality comparable since this bundle asked for it: the exact
    // `operator==` motionCore's aggregates answered in v0.6.0 and
    // motionRuntime's `PoseSampleResult` for `motion.interpolatePose`, asked of
    // `vrmRetarget` for the first time.
    ExecTypeRegistry::RegisterType(vrmRetarget::TargetSkeleton{});
    ExecTypeRegistry::RegisterType(vrmRetarget::HumanoidMap{});
    ExecTypeRegistry::RegisterType(vrmRetarget::RestPoseCorrection{});
}

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
                if (outcome.sourceRefusal ==
                    execvrm::SourceRestRefusal::NoHumanBone) {
                    TF_RUNTIME_ERROR(
                        "vrm.computeRestPoseCorrection: the source skeleton "
                        "%s names no human bone -- no joint's leaf is a bone "
                        "of the vocabulary, so it is not a semantic clip's "
                        "skeleton; no correction was computed",
                        named.c_str());
                } else {
                    std::string bones;
                    for (const auto &[bone, token] : outcome.offending) {
                        bones += bones.empty() ? "" : ", ";
                        bones += std::string(motion::HumanBoneName(bone));
                        bones += " at '";
                        bones += token;
                        bones += "'";
                    }
                    TF_RUNTIME_ERROR(
                        "vrm.computeRestPoseCorrection: the source skeleton "
                        "%s names one bone at more than one joint (%s), and "
                        "which rest the clip meant cannot be known; no "
                        "correction was computed",
                        named.c_str(), bones.c_str());
                }
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
}
