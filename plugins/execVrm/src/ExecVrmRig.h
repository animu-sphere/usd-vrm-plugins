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

} // namespace execvrm
