// SPDX-License-Identifier: Apache-2.0
//
// The rig this bundle's computations produce, over plain values.
//
// The same seam execMotion keeps, for the same reason: it takes **plain values**
// -- joint tokens, rest matrices, the joint token each human bone names -- never
// a `VdfContext` and never a stage. Marshalling exec's inputs into these
// arguments is the registration TU's job, so a computation stays a thin wrapper
// over `vrmRetarget` (motion policy §11.4), and the seam is testable with no
// stage, no system and no request -- a failure in the mechanism cannot be
// mistaken for a failure in the value.
#pragma once

#include <motionCore/Humanoid.h>
#include <vrmRetarget/HumanoidMap.h>
#include <vrmRetarget/PoseRetargeter.h>
#include <vrmRetarget/RestPose.h>
#include <vrmRetarget/RootMotionPolicy.h>
#include <vrmRetarget/TargetSkeleton.h>

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/tf/token.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace execvrm {

/// The attribute `VrmHumanoidAPI` names each canonical bone's joint under,
/// `vrm:humanBones:<bone>`, indexed by `motion::HumanBone`.
///
/// Built once from `motion::HumanBoneName` rather than spelled out: the
/// vocabulary is motionCore's, and the schema's is asserted to be the same
/// fifty-five names by `execVrm_humanoid`, which reads the schema's own prim
/// definition -- the one comparison that catches either side growing a bone the
/// other does not have. A computation declares one input per entry, so a name
/// the schema did not define would be an input that silently never has a value
/// (the root-motion report, section 3).
const std::array<pxr::TfToken, motion::HumanBoneCount>& HumanBoneAttributeNames();

/// What a `UsdSkelSkeleton` states about its rest pose, as plain values.
struct SkeletonRest
{
    /// `joints`, in the skeleton's own order. That order is the one every index
    /// in a `vrmRetarget::HumanoidMap` counts into.
    std::vector<std::string> joints;

    /// `restTransforms`, one per joint, local to the parent.
    std::vector<pxr::GfMatrix4d> restTransforms;
};

/// Why a skeleton was refused. The seam decides; the registration TU names the
/// computation and reports.
///
/// Both refusals are shaped by one measured property of 26.08, and it is the
/// reason neither is a plain "absent" check: **an attribute the schema defines
/// and the stage does not give a value -- unauthored, or value-blocked --
/// reaches a callback as ONE element of the type's fallback, not as nothing.**
/// The executor posts a `TF_WARN` ("No value set for output") and fills the
/// input with Sdf's default for the type (`vdf/parallelExecutorEngineBase.h`,
/// `exec/typeRegistry.cpp`): an empty token for `joints`, an identity matrix
/// for `restTransforms`. An *authored* empty array arrives as zero elements. So
/// what "absent" looks like here is a value, and it is recognised by what the
/// value is rather than by its absence.
enum class SkeletonRefusal
{
    /// `restTransforms` does not pair one-to-one with a `joints` that names at
    /// least one joint. An unauthored or blocked `restTransforms` on a skeleton
    /// of two joints or more lands here, as one fallback matrix against several
    /// joints.
    RestTransformCount,

    /// A `joints` entry is the empty token, which names no joint path and is
    /// what an unauthored or blocked `joints` reaches a computation as. UsdSkel
    /// has no skeleton with an empty joint either.
    EmptyJointToken,
};

struct SkeletonOutcome
{
    std::optional<vrmRetarget::TargetSkeleton> skeleton;
    SkeletonRefusal refusal = SkeletonRefusal::RestTransformCount;
};

/// The target rig `rest` states, or a refusal.
///
/// Each joint's rest transform is decomposed into the rotation and translation
/// `vrmRetarget::TargetJoint` carries, **scale and shear dropped** -- the motion
/// contract ignores scale channels and a retargeted clip never authors one --
/// and each joint's parent is derived from its token by
/// `TargetSkeleton::ResolveParentsFromTokens`, the library's own "a/b/c" rule.
///
/// **This node is the plan's sixth boundary finding, and of the sampler's
/// kind.** Building a `TargetSkeleton` from a skeleton's rest transforms exists
/// in this repository -- in `tools/motionRetarget`'s `StageIo.cpp`
/// (`ReadSkeletonRest` and `DecomposeRest`), in a *tool*, where a bundle cannot
/// call it -- and the half that is plain arithmetic, matrix to rotation and
/// translation, has no `vrmRetarget` entry point. So the decomposition is here,
/// matched to the tool's line for line, and the ask for
/// [boundary consolidation](../../../docs/roadmap/boundary-consolidation.md) is
/// a `TargetSkeleton` built from tokens and rest matrices, beside the class,
/// so the rule has one implementation again.
///
/// **Two refusals, and the first is where this node and the tool disagree.** A
/// skeleton whose `restTransforms` do not pair with its `joints` is refused.
/// The tool warns and assumes an identity rest pose instead, which is a set of
/// rest rotations no consumer can tell from measured ones -- the one kind of
/// answer both exec bundles refuse. UsdSkel itself cannot compute a rest pose
/// for such a skeleton either. P0-6 parity inherits the difference, as a
/// missing-field semantics category rather than a numerical one. The second is
/// an empty joint token (`SkeletonRefusal::EmptyJointToken`).
///
/// A skeleton that authors **no joints** -- `joints = []` -- is an answer, not a
/// refusal, **whatever its `restTransforms` say**: an empty `TargetSkeleton`
/// says exactly that, it is distinguishable from every skeleton that has a
/// joint, and with no joint for a rest transform to belong to, no rest
/// transform can become a number. So `joints = []` beside an unauthored
/// `restTransforms` -- one fallback matrix, as exec delivers it -- is the empty
/// skeleton too, rather than a count mismatch the stage never stated. What an
/// empty skeleton cannot do is resolve a binding, which is the humanoid map's to
/// refuse. A skeleton that authors no `joints` *at all* is refused instead,
/// because exec hands that over as one empty token (`EmptyJointToken`).
///
/// **One case the fallback decides, and it cannot be refused from here**: a
/// skeleton of exactly one joint whose `restTransforms` is unauthored reaches
/// this function as one joint and one identity matrix, which is also what an
/// authored identity rest looks like. It is answered as identity. The offline
/// tool answers it the same way, with a warning, so it is not a P0-6
/// divergence -- but it is the rest pose a consumer cannot tell from a
/// measured one, on the one rig shape where nothing here can see it.
///
/// A rest pose that is not in parent-before-child order is also an answer. The
/// skeleton carries it faithfully and `TargetSkeleton::IsTopologicallyOrdered`
/// reports it, which is what the retargeter already reads it through.
SkeletonOutcome TargetSkeletonFromRest(const SkeletonRest& rest);

/// What a `VrmHumanoidAPI` prim states, as plain values.
///
/// The skeleton arrives through `vrm:skeleton`, a relationship, as a fan-in --
/// and 26.08 drops two kinds of target from a fan-in without a word: one that
/// provides no `vrm.computeTargetSkeleton` (not a skeleton, or not a prim at
/// all) is skipped while the network compiles, and one whose computation
/// refused is skipped by the read iterator (the blending report, section 2).
/// `skeletonTargetCount` is the relationship read a second time, for exec's
/// builtin `computePath`, which is how the two are told apart -- the same
/// closure `motion.blendPoses` needs.
struct HumanoidInputs
{
    /// How many objects `vrm:skeleton` reaches.
    ///
    /// Objects, not authored targets: a target path with no prim behind it
    /// provides no `computePath` either, so it is missing from this count and
    /// from `skeletons` alike. Beside one real skeleton it is therefore
    /// invisible, and the map is answered -- measured and pinned in
    /// `execVrm_humanoid`. A blend catches the same drop by counting its
    /// weights against it; a humanoid states nothing else to count.
    std::size_t skeletonTargetCount = 0;

    /// The skeletons that came back from those objects.
    std::vector<vrmRetarget::TargetSkeleton> skeletons;

    /// The joint token each canonical bone names, in vocabulary order.
    ///
    /// Through exec this is **every bone of the vocabulary**, not only the ones
    /// the prim authors: each `vrm:humanBones:*` attribute is defined by the
    /// applied schema, and an unauthored or blocked one reaches the callback as
    /// the empty token -- the fallback, with a `TF_WARN` beside it (see
    /// `SkeletonRefusal`). So an unauthored bone and an authored empty token
    /// are one value by the time they arrive here, and both bind nothing.
    std::vector<std::pair<motion::HumanBone, std::string>> bindings;
};

/// Why a humanoid map was refused.
enum class MapRefusal
{
    /// `vrm:skeleton` reaches nothing. The offline tool falls back to the first
    /// skeleton on the stage; a computation has no stage to search, and a
    /// search would be the heuristic the schema's relationship exists to
    /// replace.
    NoSkeleton,

    /// `vrm:skeleton` reaches more than one object. A humanoid's indices count
    /// into exactly one rig.
    SeveralSkeletons,

    /// The one object `vrm:skeleton` reaches answered no skeleton: it is not a
    /// `UsdSkelSkeleton`, or its skeleton refused.
    SkeletonUnanswered,

    /// A bone names a joint token the skeleton does not contain.
    UnknownJoint,

    /// Two bones name the same joint.
    DuplicateJoint,
};

struct MapOutcome
{
    std::optional<vrmRetarget::HumanoidMap> map;
    MapRefusal refusal = MapRefusal::NoSkeleton;

    /// For `UnknownJoint` and `DuplicateJoint`: the bones concerned, in
    /// vocabulary order, and the token each named -- so the report can say
    /// which statement the humanoid made that this layer cannot honour.
    std::vector<std::pair<motion::HumanBone, std::string>> offending;
};

/// The humanoid map `inputs` state, or a refusal.
///
/// **The whole node, and it is a wrapper**: `HumanoidMap::SetJointToken` for
/// each binding, against the one skeleton `vrm:skeleton` reaches. Resolution is
/// exact on the full joint path -- the library's rule, and the reason the map
/// never guesses from a joint's name.
///
/// Every refusal is somewhere the library *would* answer, and its answer would
/// be a map no consumer could tell from one the humanoid meant:
///
///   * a binding to a joint the skeleton does not contain is **refused**, where
///     the library leaves the bone unmapped and the offline tool warns and goes
///     on. A map without the bone is indistinguishable from a humanoid that
///     never named it, and "never default a value the scene stated that this
///     layer cannot honour" is both exec bundles' rule;
///   * two bones naming one joint are **refused**, where the library keeps both
///     and the retargeter lets the later one win (`FindDuplicateJointIndices`
///     calls it "always a mapping bug"). The offline tool reports a warning and
///     bakes it.
///
/// Both are P0-6 divergences of the missing-field kind, and both are cases the
/// importer never authors: it names joints off the skeleton it wrote.
///
/// **An empty token binds nothing and is not refused**, and here that is forced
/// rather than chosen: exec delivers an unauthored bone as the empty token, so
/// refusing one would refuse every humanoid that does not bind all fifty-five
/// bones. It is also the offline tool's reading, and it is sound on its own
/// terms -- an empty token cannot be a misspelled joint, because it names none.
/// The cost is that a humanoid cannot *state* an empty binding that means
/// anything else; nothing in the schema asks it to.
///
/// What is **not** refused, because the value answers it: a humanoid missing a
/// bone VRM 1.0 requires. `HumanoidMap::FindMissingRequiredBones` states the
/// gap, the library says a rig missing one "can still be retargeted onto", and
/// the retargeter already reports it. A humanoid stating no bone at all is the
/// empty map, for the same reason.
MapOutcome HumanoidMapFor(const HumanoidInputs& inputs);

/// Why a source skeleton could not be read as a clip's rest pose.
enum class SourceRestRefusal
{
    /// No joint's leaf is a bone of the vocabulary. The skeleton is not a
    /// semantic one, and the rest pose read off it would be the default -- every
    /// bone at identity -- which is numbers nobody can tell from a measured
    /// rest.
    NoHumanBone,

    /// Two joints' leaves name the same bone. The offline tool keeps the later
    /// one without a word; which of the two rests the clip meant is exactly
    /// what cannot be known from here.
    DuplicateBone,
};

struct SourceRestOutcome
{
    std::optional<vrmRetarget::SourceRestPose> rest;
    SourceRestRefusal refusal = SourceRestRefusal::NoHumanBone;

    /// For `DuplicateBone`: every joint token that named a bone some other
    /// joint also named, each beside that bone.
    std::vector<std::pair<motion::HumanBone, std::string>> offending;
};

/// The clip's rest pose, per human bone, as the source skeleton states it.
///
/// **The source is a semantic skeleton, and on that side a joint's leaf is its
/// bone.** That is the motion contract's statement about a semantic clip, not a
/// heuristic: `usdVrmaFileFormat` authors a skeleton whose joint leaves are the
/// vocabulary's names, and `execMotion`'s `motion.sampleAnimation` reads the
/// animation's joints the same way (`BoneForJointPath`). The *target* rig is
/// never read by name -- its bones are the humanoid's bindings.
///
/// Read the way `tools/motionRetarget`'s `ReadClip` reads it, line for line: for
/// each joint whose leaf is a bone, its decomposed rest rotation and translation
/// fill that bone's slot, and its semantic parent is the bone named by the
/// **leaf of its parent path** -- the path, not the joint the path resolves to,
/// so a parent path absent from the skeleton still parents the bone when its
/// leaf is one. A joint whose leaf is no bone contributes nothing, and a bone
/// whose parent path's leaf is no bone is a root. So a non-bone joint *between*
/// two bones, or above the hips, drops its rest rotation from the chain; that
/// is `SourceRestPose`'s shape (one slot per bone and no other), shared with the
/// tool, not a divergence P0-6 has to explain.
///
/// **This is the seventh boundary finding, and the sampler's kind a third
/// time**: the reading exists only in the tool, beside the decomposition
/// `vrm.computeTargetSkeleton` already copies from it. The rest rotations and
/// translations arrive here already decomposed -- the source skeleton is read
/// through `vrm.computeTargetSkeleton`, so the two rigs of a retarget are
/// decomposed by one implementation -- and only the bone assignment is this
/// function's.
SourceRestOutcome SourceRestFromSkeleton(
    const vrmRetarget::TargetSkeleton& skeleton);

/// What `vrm.computeRestPoseCorrection` reads, as plain values.
///
/// Three things arrive, over two relationships and one link on the prim:
///
///   * the humanoid's own `vrm.computeHumanoidMap`, which is null when the map
///     refused;
///   * the target skeleton, across `vrm:skeleton` -- the rig the map's indices
///     count into, read again rather than trusted, because a map carries no
///     skeleton;
///   * the source skeleton, across `vrm:retarget:sourceSkeleton`, counted twice
///     for the reason `vrm:skeleton` is.
struct CorrectionInputs
{
    /// Null when `vrm.computeHumanoidMap` answered nothing.
    const vrmRetarget::HumanoidMap* map = nullptr;

    /// What came back across `vrm:skeleton`. When the map answered, the map
    /// already counted this relationship and it reached exactly one skeleton.
    std::vector<vrmRetarget::TargetSkeleton> targets;

    /// How many objects `vrm:retarget:sourceSkeleton` reaches, and the
    /// skeletons that came back from them.
    std::size_t sourceTargetCount = 0;
    std::vector<vrmRetarget::TargetSkeleton> sources;
};

/// Why a correction was refused.
enum class CorrectionRefusal
{
    /// The humanoid's map refused, or the skeleton it counts into did not come
    /// back. The map's own error says which; this one says it propagated.
    RigUnanswered,

    /// `vrm:retarget:sourceSkeleton` reaches nothing -- unauthored, or naming
    /// no prim. **Not defaulted**, although the library has a default rest (all
    /// identity, which is `usdVrmaFileFormat`'s): a path to nothing arrives as
    /// nothing, exactly as an unauthored relationship does, so a default here
    /// would hand a misspelled source the identity rest with no word said.
    NoSource,

    /// It reaches more than one object. A correction is between two rigs.
    SeveralSources,

    /// The one object it reaches answered no `vrm.computeTargetSkeleton`: it is
    /// not a `UsdSkelSkeleton`, or its skeleton refused.
    SourceUnanswered,

    /// The source skeleton answered and is not readable as a rest pose
    /// (`SourceRestRefusal`, carried in `sourceRefusal`).
    SourceRest,
};

struct CorrectionOutcome
{
    std::optional<vrmRetarget::RestPoseCorrection> correction;
    CorrectionRefusal refusal = CorrectionRefusal::RigUnanswered;

    /// For `CorrectionRefusal::SourceRest`.
    SourceRestRefusal sourceRefusal = SourceRestRefusal::NoHumanBone;
    std::vector<std::pair<motion::HumanBone, std::string>> offending;
};

/// The correction carrying a rest-relative rotation from the clip's rig onto
/// the humanoid's, or a refusal.
///
/// **The node is one library call**: `vrmRetarget::ComputeRestPoseCorrection`
/// over the source rest, the target skeleton and the map -- asserted as that in
/// `execVrm_rig`, where the node's correction is compared with the library's
/// over the same values. Every mapped bone gets a correction and every unmapped
/// one stays identity, which is the library's rule.
///
/// No input moves with time: all three are rig statements, so the correction
/// is computed once per rig edit and never per frame -- which is the reason it
/// is a node of its own rather than a step of the retarget.
CorrectionOutcome RestPoseCorrectionFor(const CorrectionInputs& inputs);

/// What `vrm.computeBoundPose` reads: the pose `motion.sampleAnimation` answers
/// on whatever the skeleton's `skel:animationSource` targets, counted twice
/// for the reason `vrm:skeleton` is.
///
/// The computation is on the *skeleton* because that is the prim a
/// `vrm:retarget:sourceSkeleton` names, and it is the one prim from which both
/// halves of a clip are reachable: its rest directly, its animation through
/// UsdSkel's own binding. So one relationship on the humanoid says which clip
/// drives it, and the rest a correction reads and the pose a retarget reads
/// cannot come from two different clips.
struct BoundPoseInputs
{
    /// How many objects `skel:animationSource` reaches.
    std::size_t animationTargetCount = 0;

    /// The poses that came back from them. A target that provides no
    /// `motion.sampleAnimation` -- not a `UsdSkelAnimation`, or `execMotion`
    /// not in the session -- is dropped while the network compiles, and one
    /// whose sampler refused is dropped by the read iterator; the count above
    /// is what tells either from a skeleton that names nothing.
    std::vector<motion::HumanoidPose> poses;

    /// What the nearest ancestor with `SkelBindingAPI` applied binds, through
    /// its own `vrm.computeBindingPose`; null when there is no such ancestor,
    /// or it binds nothing, or its binding refused (it says why itself).
    ///
    /// UsdSkel's binding is inherited: `skel:animationSource` binds "Skeleton
    /// primitives at or beneath the location at which this property is
    /// defined", and `UsdSkelBindingAPI::GetInheritedAnimationSource` walks up
    /// from the skeleton to the first prim that has the API applied and authors
    /// the relationship. So a clip bound on its SkelRoot is bound. The walk is
    /// exec's `NamespaceAncestor` over a computation registered on the applied
    /// `UsdSkelBindingAPI`, which finds exactly the prims UsdSkel's `HasAPI`
    /// check admits.
    const motion::HumanoidPose* inherited = nullptr;
};

/// Why a bound pose was refused.
enum class BoundPoseRefusal
{
    /// `skel:animationSource` reaches nothing -- unauthored, or naming no prim,
    /// which arrive as one value (the correction report, section 3) -- and no
    /// ancestor binds an animation either. **Not answered as an empty pose**,
    /// although one exists: an empty pose is what a clip naming no bone samples
    /// to, and retargets to the rig's rest, so a misspelled binding would put
    /// the avatar at rest with no word said.
    NoAnimation,

    /// It reaches more than one object. UsdSkel binds one animation.
    SeveralAnimations,

    /// The one object it reaches answered no `motion.sampleAnimation`: it is
    /// not a `UsdSkelAnimation`, `execMotion` is not in the session, or the
    /// sampler refused (its own error says which of the last).
    AnimationUnanswered,
};

struct BoundPoseOutcome
{
    std::optional<motion::HumanoidPose> pose;
    BoundPoseRefusal refusal = BoundPoseRefusal::NoAnimation;
};

/// The pose of the animation the skeleton is bound to, or a refusal.
///
/// **UsdSkel's resolution order**: the prim's own `skel:animationSource` when it
/// reaches anything, and only then what an ancestor binds -- an own binding
/// shadows an inherited one, as `GetInheritedAnimationSource` stops at the
/// first prim that authors one.
///
/// **One case it cannot follow.** UsdSkel stops at an authored relationship
/// that targets nothing, or nothing valid, because that is an explicit
/// *unbinding*. Exec hands an authored empty relationship, and one whose target
/// names no prim, to a callback exactly as it hands an unauthored one (the
/// correction report, section 3), so here the walk goes on and the ancestor's
/// animation is answered. Pinned in `execVrm_retarget`, and a P0-6 row.
///
/// **A forward, and nothing more.** The pose is `execMotion`'s, sampled at the
/// frame the system evaluates and stamped in seconds there; this node exists
/// because an exec input traverses one relationship, and a retarget needs two
/// (the humanoid's `vrm:retarget:sourceSkeleton`, then the skeleton's
/// `skel:animationSource`). It is the first computation in this bundle whose
/// value another bundle computes, and it declares no `computeTime` -- the
/// sampler's time dependence reaches it across the link.
BoundPoseOutcome BoundPoseFor(const BoundPoseInputs& inputs);

/// What a humanoid states about where a clip's root motion lands, as plain
/// values: the four `vrm:retarget:*` attributes below.
///
/// **Absent means the prim has no such attribute at all**, and only that. None
/// of the four is a schema property, and an attribute the prim does not have
/// reaches a callback as no value. But one the prim *declares* with no value --
/// or authors and blocks -- reaches it as one element of the type's fallback,
/// beside an executor warning, exactly as a schema attribute does: the humanoid
/// report's section 4 is about an attribute's *existence*, not about its
/// schema (the retarget report, section 4). So a declared, valueless
/// `vrm:retarget:rootMotion` arrives as the empty token and is refused as no
/// mode; a valueless `rootJoint` is the empty token and names no joint; a
/// valueless `preserveTargetHeight` is `false`, the library's own default.
///
/// **And a valueless `translationScale` arrives as 0**, the one statement here
/// whose fallback is a believable number: a scale of zero pins the receiver at
/// its rest. It cannot be refused from here without refusing an authored zero,
/// which `motion_retarget --translation-scale 0` accepts, so it is answered and
/// pinned -- the one-joint skeleton's situation (`TargetSkeletonFromRest`), on
/// a statement rather than on a rig.
///
/// The vocabulary is `motion_retarget`'s own command line, attribute for flag,
/// so a stage and an invocation that mean the same retarget say it in the same
/// words -- which is what P0-6 parity compares.
struct RootMotionStatements
{
    /// `vrm:retarget:rootMotion`: `hips`, `root` or `ignore` (`--root-motion`).
    std::optional<std::string> mode;

    /// `vrm:retarget:rootJoint`: the target joint that receives the root under
    /// `root`, by its full joint path (`--root-joint`).
    std::optional<std::string> rootJoint;

    /// `vrm:retarget:translationScale` (`--translation-scale`).
    std::optional<float> translationScale;

    /// `vrm:retarget:preserveTargetHeight` (`--preserve-target-height`).
    std::optional<bool> preserveTargetHeight;
};

/// Why a humanoid's root-motion statements were refused.
enum class RootMotionRefusal
{
    /// `vrm:retarget:rootMotion` names no mode -- the empty token included,
    /// which is what a declared attribute with no value arrives as. A
    /// misspelled `ignore` given the library's default `hips` would move a
    /// body its author asked to keep in place.
    UnknownMode,

    /// `root`, with no `vrm:retarget:rootJoint` to receive it. The library
    /// degrades that to `ignore` with a warning; `motion_retarget` refuses it
    /// before it starts, and so does this.
    NoRootJoint,

    /// `vrm:retarget:rootJoint` names no joint of the target skeleton. Refused
    /// by `motion_retarget` too.
    UnknownRootJoint,

    /// `vrm:retarget:translationScale` is not a finite number.
    TranslationScale,
};

struct RootMotionOutcome
{
    std::optional<vrmRetarget::RootMotionOptions> options;
    RootMotionRefusal refusal = RootMotionRefusal::UnknownMode;
};

/// The root-motion options `statements` state against `target`, or a refusal.
///
/// Each absent statement keeps `vrmRetarget::RootMotionOptions`' own default --
/// `hips`, a scale of 1, the source's height -- because an absent value there
/// selects the library's documented behaviour, which is both exec bundles'
/// rule. Each stated one the layer cannot honour is refused, the rule's other
/// half. A `vrm:retarget:rootJoint` stated beside a mode other than `root` is
/// not read, as `--root-joint` is not.
RootMotionOutcome RootMotionOptionsFor(const RootMotionStatements& statements,
                                       const vrmRetarget::TargetSkeleton& target);

/// What `vrm.humanoidRetarget` reads, as plain values.
///
/// The correction node's three inputs, a fourth across the same source
/// relationship, the humanoid's own root-motion statements, and whether the
/// system names an instant:
///
///   * the humanoid's `vrm.computeHumanoidMap`, null when it refused;
///   * the target skeleton, across `vrm:skeleton`;
///   * the source skeleton across `vrm:retarget:sourceSkeleton`, counted, and
///     the pose its `vrm.computeBoundPose` forwards, across the same one;
///   * the four `vrm:retarget:*` statements (`RootMotionStatements`);
///   * `computeTime`'s time code, read only for whether it is the default one.
///
/// It does **not** read `vrm.computeRestPoseCorrection`, and that is the
/// finding rather than an omission: `vrmRetarget::PoseRetargeter` computes its
/// own correction in its constructor and accepts none, so a wrapper has nowhere
/// to hand the cached one (the retarget report, section 2).
struct RetargetInputs
{
    const vrmRetarget::HumanoidMap* map = nullptr;
    std::vector<vrmRetarget::TargetSkeleton> targets;

    std::size_t sourceTargetCount = 0;
    std::vector<vrmRetarget::TargetSkeleton> sources;
    std::vector<motion::HumanoidPose> poses;

    RootMotionStatements rootMotion;

    /// False when the system is at the default time code, which names no
    /// instant (`RetargetRefusal::NoInstant`).
    bool hasInstant = true;
};

/// Why a retarget was refused.
enum class RetargetRefusal
{
    /// The humanoid's map refused, or the skeleton it counts into did not come
    /// back (`CorrectionRefusal::RigUnanswered`'s case).
    RigUnanswered,

    /// The root-motion statements were refused (`rootMotionRefusal`).
    RootMotion,

    /// `vrm:retarget:sourceSkeleton` reaches nothing, several objects, or one
    /// that answered no skeleton -- the correction's three refusals, for the
    /// correction's reasons.
    NoSource,
    SeveralSources,
    SourceUnanswered,

    /// The source skeleton is not readable as a rest pose (`sourceRefusal`).
    SourceRest,

    /// The source skeleton answered no `vrm.computeBoundPose`: its own error
    /// says why.
    PoseUnanswered,

    /// The system is at the default time code -- what every request is armed
    /// at until `ChangeTime` (the filtering report, section 4). There the
    /// sampler answers a clip that authors only time samples with an **empty
    /// pose** stamped 0.0, which is harmless while it stays a pose: nobody
    /// takes a pose naming no bone for a frame. Retargeted, it is the rig's
    /// whole rest pose, every joint filled in, stamped 0.0 -- a believable
    /// answer for an instant nobody named, so this is where the refusal has to
    /// be. `motion.interpolatePose` refuses the default time code for the same
    /// reason one bundle over. Checked after every statement, so a request
    /// armed there still reports what the stage gets wrong.
    NoInstant,
};

struct RetargetOutcome
{
    std::optional<vrmRetarget::RetargetedPose> pose;
    RetargetRefusal refusal = RetargetRefusal::RigUnanswered;

    RootMotionRefusal rootMotionRefusal = RootMotionRefusal::UnknownMode;
    SourceRestRefusal sourceRefusal = SourceRestRefusal::NoHumanBone;
    std::vector<std::pair<motion::HumanBone, std::string>> offending;
};

/// One sample of the clip, expanded into the target rig's joint order, or a
/// refusal.
///
/// **The node is one library call**: `vrmRetarget::PoseRetargeter` over the
/// target rig, the map, the clip's rest and the root-motion options, asked
/// for the one pose the clip's skeleton is bound to -- what `motion_retarget`
/// does per sample, with the same four arguments. A joint the clip does not
/// drive stays at its rest, and a bone the clip drives that the rig does not
/// map is dropped: both are the library's rules, and both are visible to a
/// consumer from the pose and the map, which one request can ask for side by
/// side.
///
/// **And it recomputes the correction on every evaluation.** Constructing the
/// retargeter is where `ComputeRestPoseCorrection` runs, and this node is
/// recomputed on every frame the clip moves -- so the value
/// `vrm.computeRestPoseCorrection` caches per rig edit is computed again, per
/// frame, beside it. That is the eighth boundary finding, and the first whose
/// cost is a repeated computation rather than a copy: the ask is a retargeter
/// (or a free per-pose function beside it) that takes the correction as an
/// input.
RetargetOutcome HumanoidRetargetFor(const RetargetInputs& inputs);

/// What `vrm.computeJointLocalTransforms` reads, as plain values: the
/// humanoid's own `vrm.humanoidRetarget`, and the rig across `vrm:skeleton`
/// for the joint tokens a retargeted pose is ordered by and does not carry.
struct JointTransformsInputs
{
    /// Null when `vrm.humanoidRetarget` answered nothing -- it refused, or a
    /// driver's override of it was dropped.
    const vrmRetarget::RetargetedPose* pose = nullptr;

    /// What came back across `vrm:skeleton`. Not counted a second time: the
    /// map already refuses unless exactly one skeleton comes back, and a
    /// retarget that answered read that map. A driver's override of the
    /// retarget skips the map, which is how this can be other than one.
    std::vector<vrmRetarget::TargetSkeleton> targets;
};

/// Why an animation sample was refused.
enum class JointTransformsRefusal
{
    /// `vrm.humanoidRetarget` answered nothing. Its own error says why.
    PoseUnanswered,

    /// `vrm:skeleton` did not bring back exactly one skeleton. Reachable only
    /// under a driver's override of the retarget, which skips the map that
    /// would otherwise have refused first.
    RigUnanswered,

    /// The pose does not have one rotation and one translation per joint of
    /// the rig. Never a retarget's own answer -- `PoseRetargeter` sizes both
    /// arrays from the rig -- so this is a driver's override that was not
    /// retargeted onto this rig. Refused rather than padded or cut: a sample
    /// whose arrays do not pair with its `joints` is an animation UsdSkel
    /// maps onto no joint at all.
    JointCount,
};

struct JointTransformsOutcome
{
    std::optional<vrmRetarget::JointLocalTransforms> sample;
    JointTransformsRefusal refusal = JointTransformsRefusal::PoseUnanswered;

    /// For `JointCount`: the rig's joints, and the pose's two array sizes.
    std::size_t joints = 0;
    std::size_t rotations = 0;
    std::size_t translations = 0;
};

/// The retargeted pose as one `UsdSkelAnimation` sample -- the rig's joint
/// tokens, the pose's translations and rotations, and one identity scale per
/// joint -- or a refusal.
///
/// **Not a second retarget.** The retarget already answers in the rig's joint
/// order and in joint-local terms, so the arrays pass through bit for bit and
/// the timestamp with them. What this adds is what a bake adds: the tokens the
/// arrays are ordered by, which a `RetargetedPose` does not carry, and the
/// scales, which UsdSkel needs beside the other two before it resolves any
/// joint at all (`vrmRetarget::JointLocalTransforms`).
///
/// **Every scale is (1, 1, 1)**, which is `motion_retarget`'s rule and the
/// plan's P1-2: a retargeted clip never animates scale. That includes a joint
/// whose *rest* transform is scaled -- `vrm.computeTargetSkeleton` drops rest
/// scale, and so does the tool -- so a rig with a scaled rest does not keep its
/// scale under this sample. UsdSkel takes an animated joint's local transform
/// from the animation whole, rest scale included, and the sample states 1.
/// Measured in `execVrm_joint_transforms` on the fixture's arm, rested at
/// scale 2; the two implementations agree, so it is P1-2's question and not a
/// P0-6 row.
///
/// **The ninth boundary finding, and the smallest.** The bake's shape -- the
/// rig's tokens beside the arrays, identity scales -- is stated in one place
/// offline, `tools/motionRetarget`'s `WriteAnimation`, as two lines of a tool,
/// and `vrmRetarget` had no type for it. The library gained the type, for the
/// registry; the rule stays two lines here and two lines in the tool, and the
/// ask is that the tool author from the library's value, so P1-2's "OpenExec
/// and offline behave identically" is one statement.
JointTransformsOutcome JointLocalTransformsFor(
    const JointTransformsInputs& inputs);

} // namespace execvrm
