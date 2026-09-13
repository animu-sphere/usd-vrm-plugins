// SPDX-License-Identifier: Apache-2.0
//
// `vrm.computeJointLocalTransforms` through the built bundles, over
// retargeted_rig.usda -- the retarget suite's fixture, since this node is the
// retarget in another shape. Like every exec suite here it links neither
// plugin, and it needs execMotion in the session for the retarget's pose.
//
// What the retarget suite could not say, and so what this one measures:
//
//   * that the node is the retarget's answer in the shape a UsdSkelAnimation
//     states at one time code -- the rig's tokens, the arrays unchanged, one
//     identity scale per joint -- and nothing else;
//   * that shape, authored the way motion_retarget authors it, is what UsdSkel
//     resolves: its joint-local transforms are UsdSkel's own composition of
//     the node's value, and without `scales`, or with arrays that do not pair
//     with `joints`, they are the rig's rest;
//   * what the identity scale costs on a rig whose rest is scaled -- the
//     fixture's arm, rested at scale 2 -- which is P1-2's question;
//   * a driver's override of the retarget reaching it, which is the one route
//     to a pose that does not pair with the rig;
//   * invalidation, and every refusal.
//
// It links vrmRetarget for the result types and OpenUSD's usdSkel for the
// resolution the node's value is compared against.

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/base/tf/diagnosticMgr.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/span.h"
#include "pxr/base/tf/status.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/tf/warning.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/types.h"
#include "pxr/base/vt/value.h"

#include "pxr/exec/exec/request.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"
#include "pxr/exec/execUsd/valueOverride.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/valueTypeName.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdSkel/animation.h"
#include "pxr/usd/usdSkel/bindingAPI.h"
#include "pxr/usd/usdSkel/cache.h"
#include "pxr/usd/usdSkel/skeleton.h"
#include "pxr/usd/usdSkel/skeletonQuery.h"
#include "pxr/usd/usdSkel/utils.h"

#include <vrmRetarget/PoseRetargeter.h>
#include <vrmRetarget/TargetSkeleton.h>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

const TfToken kTargetSkeleton("vrm.computeTargetSkeleton");
const TfToken kRetarget("vrm.humanoidRetarget");
const TfToken kJointTransforms("vrm.computeJointLocalTransforms");

const TfToken kSkeletonRel("vrm:skeleton");
const TfToken kRootMotion("vrm:retarget:rootMotion");
const TfToken kRotations("rotations");
const TfToken kRestTransforms("restTransforms");

const SdfPath kTargetPath("/Asset/skel/Skeleton");
const SdfPath kHumanoidPath("/Asset/rig/Humanoid");
const SdfPath kClipAnimationPath("/Clip/Animation");
const SdfPath kBakedPath("/Asset/Baked");
const SdfPath kUnscaledPath("/Asset/Unscaled");
const SdfPath kShortPath("/Asset/Short");

// The value keys every request here asks for, in this order.
constexpr int kTargetKey = 0;
constexpr int kRetargetKey = 1;
constexpr int kSampleKey = 2;

// The target rig's joint order (retargeted_rig.usda).
constexpr std::size_t kRootJointSlot = 0;
constexpr std::size_t kSpineJoint = 2;
constexpr std::size_t kHeadJoint = 5;
constexpr std::size_t kArmJoint = 6;
constexpr std::size_t kJointCount = 7;

// `motion:timeCodesPerSecond` on the fixture's clip: where motion_retarget
// places a sample, `timestamp * timeCodesPerSecond`.
constexpr double kClipRate = 24.0;

GfQuatf About(const GfVec3f& axis, float degrees)
{
    const float half = degrees * 3.14159265358979324f / 360.0f;
    return GfQuatf(std::cos(half), axis * std::sin(half));
}

const GfVec3f kY(0, 1, 0);

bool NearMatrix(const GfMatrix4d& a, const GfMatrix4d& b, double tolerance)
{
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            if (std::abs(a[row][column] - b[row][column]) > tolerance) {
                return false;
            }
        }
    }
    return true;
}

double RowLength(const GfMatrix4d& m, int row)
{
    return GfVec3d(m[row][0], m[row][1], m[row][2]).GetLength();
}

// Whether an error in `mark` says `what`. When none does, every error posted is
// printed, so a red run says what was reported instead.
bool MarkNames(const TfErrorMark& mark, const std::string& what)
{
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it) {
        if (it->GetCommentary().find(what) != std::string::npos) {
            return true;
        }
    }
    std::fprintf(stderr, "no error said \"%s\"; posted:\n", what.c_str());
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it) {
        std::fprintf(stderr, "  %s\n", it->GetCommentary().c_str());
    }
    return false;
}

// The humanoid suite's warning collector: the map leaves 49 bones unbound and
// the executor warns once per bone per compute of it.
class Warnings : public TfDiagnosticMgr::Delegate
{
public:
    Warnings() { TfDiagnosticMgr::GetInstance().AddDelegate(this); }
    ~Warnings() override { TfDiagnosticMgr::GetInstance().RemoveDelegate(this); }

    void IssueError(const TfError& error) override
    {
        std::fprintf(stderr, "error: %s\n", error.GetCommentary().c_str());
    }
    void IssueFatalError(const TfCallContext&, const std::string& message) override
    {
        std::fprintf(stderr, "fatal: %s\n", message.c_str());
    }
    void IssueStatus(const TfStatus& status) override
    {
        std::fprintf(stderr, "status: %s\n", status.GetCommentary().c_str());
    }
    void IssueWarning(const TfWarning& warning) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _seen.push_back(warning.GetCommentary());
    }

    std::vector<std::string> Take(const std::string& what)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::vector<std::string> matching;
        for (const std::string& seen : _seen) {
            if (seen.find(what) != std::string::npos) {
                matching.push_back(seen);
            }
        }
        _seen.clear();
        return matching;
    }

private:
    std::mutex _mutex;
    std::vector<std::string> _seen;
};

struct Rig
{
    UsdStageRefPtr stage;
    UsdPrim target;
    UsdPrim humanoid;
    UsdPrim clipAnimation;
};

// Opened directly and edited in memory, never saved -- each case opens its own.
Rig Open(const std::string& fixture)
{
    Rig rig;
    rig.stage = UsdStage::Open(fixture);
    assert(rig.stage && "the retargeted rig fixture did not open");
    rig.target = rig.stage->GetPrimAtPath(kTargetPath);
    rig.humanoid = rig.stage->GetPrimAtPath(kHumanoidPath);
    rig.clipAnimation = rig.stage->GetPrimAtPath(kClipAnimationPath);
    assert(rig.target && rig.humanoid && rig.clipAnimation &&
           "the retargeted rig fixture is missing a prim");
    return rig;
}

std::vector<ExecUsdValueKey> KeysFor(const Rig& rig)
{
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(rig.target, kTargetSkeleton);
    keys.emplace_back(rig.humanoid, kRetarget);
    keys.emplace_back(rig.humanoid, kJointTransforms);
    return keys;
}

template <class T>
T ValueAt(const ExecUsdCacheView& view, int index, const char* what)
{
    const VtValue value = view.Get(index);
    if (value.IsEmpty() || !value.IsHolding<T>()) {
        std::fprintf(stderr, "no %s came back\n", what);
        assert(false && "a value this case needs did not come back");
    }
    return value.UncheckedGet<T>();
}

vrmRetarget::JointLocalTransforms SampleAt(const ExecUsdCacheView& view)
{
    return ValueAt<vrmRetarget::JointLocalTransforms>(view, kSampleKey,
                                                      "joint transforms");
}

vrmRetarget::RetargetedPose RetargetAt(const ExecUsdCacheView& view)
{
    return ValueAt<vrmRetarget::RetargetedPose>(view, kRetargetKey,
                                                "retargeted pose");
}

void AssertRefused(const ExecUsdCacheView& view, int index)
{
    assert(view.Get(index).IsEmpty() &&
           "a refusal came back carrying a value, which puts it back where a "
           "consumer cannot tell it from an answer");
}

// Arms a request -- its first compute, at the default time code, where the
// retarget, and so this node, refuses by design -- and moves the system to
// `frame`.
void ArmAt(ExecUsdSystem& system, ExecUsdRequest& request, double frame)
{
    {
        TfErrorMark mark;
        system.Compute(request);
        mark.Clear();
    }
    system.ChangeTime(UsdTimeCode(frame));
}

// The node's value, authored the way motion_retarget's WriteAnimation authors
// a sample: `joints` and `scales` once, the two arrays at
// `timestamp * timeCodesPerSecond`, and the rig bound to it through an applied
// SkelBindingAPI on the skeleton. `withScales` false leaves `scales`
// unauthored, which is the defect the value's shape exists to prevent.
void AuthorAsTheToolDoes(const Rig& rig, const SdfPath& path,
                         const vrmRetarget::JointLocalTransforms& sample,
                         bool withScales)
{
    const UsdSkelAnimation animation =
        UsdSkelAnimation::Define(rig.stage, path);
    VtTokenArray joints;
    for (const std::string& joint : sample.joints) {
        joints.push_back(TfToken(joint));
    }
    assert(animation.CreateJointsAttr().Set(joints));
    if (withScales) {
        assert(animation.CreateScalesAttr().Set(
            VtVec3hArray(sample.scales.begin(), sample.scales.end())));
    }
    const UsdTimeCode time(sample.timestamp * kClipRate);
    assert(animation.CreateRotationsAttr().Set(
        VtQuatfArray(sample.rotations.begin(), sample.rotations.end()), time));
    assert(animation.CreateTranslationsAttr().Set(
        VtVec3fArray(sample.translations.begin(), sample.translations.end()),
        time));

    const UsdSkelBindingAPI binding = UsdSkelBindingAPI::Apply(rig.target);
    assert(binding);
    assert(binding.CreateAnimationSourceRel().SetTargets({path}));
}

// What UsdSkel resolves for the target rig at `frame`: its joint-local
// transforms, through a fresh cache so no earlier binding is remembered.
VtMatrix4dArray ResolvedLocals(const Rig& rig, double frame)
{
    UsdSkelCache cache;
    const UsdSkelSkeletonQuery query =
        cache.GetSkelQuery(UsdSkelSkeleton(rig.target));
    assert(query && "UsdSkel has no query for the target rig");
    VtMatrix4dArray locals;
    assert(query.ComputeJointLocalTransforms(&locals, UsdTimeCode(frame)));
    return locals;
}

VtMatrix4dArray RestTransforms(const Rig& rig)
{
    VtMatrix4dArray rest;
    assert(rig.target.GetAttribute(kRestTransforms).Get(&rest));
    return rest;
}

// What UsdSkel's own composition makes of a sample's components.
VtMatrix4dArray Composed(const vrmRetarget::JointLocalTransforms& sample)
{
    VtMatrix4dArray composed(sample.joints.size());
    assert(UsdSkelMakeTransforms(
        TfSpan<const GfVec3f>(sample.translations.data(),
                              sample.translations.size()),
        TfSpan<const GfQuatf>(sample.rotations.data(), sample.rotations.size()),
        TfSpan<const GfVec3h>(sample.scales.data(), sample.scales.size()),
        TfSpan<GfMatrix4d>(composed.data(), composed.size())));
    return composed;
}

// ---------------------------------------------------------------------------
// The retarget, in an animation's shape
// ---------------------------------------------------------------------------
void TestTheSampleIsTheRetargetInAnAnimationsShape(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    ExecUsdSystem system(rig.stage);

    std::set<int> timeReported;
    ExecUsdRequest request = system.BuildRequest(
        KeysFor(rig),
        [](const ExecRequestIndexSet&, const EfTimeInterval&) {},
        [&](const ExecRequestIndexSet& indices) {
            timeReported.insert(indices.begin(), indices.end());
        });
    assert(request.IsValid());
    ArmAt(system, request, 24.0);

    // Time dependence crosses the link: the node declares no `computeTime`,
    // and the rig it reads the tokens off is reported to no frame change.
    assert(timeReported.count(kRetargetKey) && timeReported.count(kSampleKey) &&
           "a frame change did not reach the joint transforms");
    assert(!timeReported.count(kTargetKey));

    VtTokenArray authoredJoints;
    assert(rig.target.GetAttribute(TfToken("joints")).Get(&authoredJoints));

    for (const double frame : {24.0, 12.0}) {
        system.ChangeTime(UsdTimeCode(frame));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        const vrmRetarget::RetargetedPose pose = RetargetAt(view);
        const vrmRetarget::JointLocalTransforms sample = SampleAt(view);
        assert(mark.IsClean() && "the fixture's joint transforms posted an error");

        // Not a second retarget: the arrays and the timestamp, bit for bit.
        assert(sample.timestamp == pose.timestamp);
        assert(sample.timestamp == frame / kClipRate);
        assert(sample.rotations == pose.rotations);
        assert(sample.translations == pose.translations);

        // The two things a bake adds: the rig's own `joints`, verbatim and in
        // its order, and one identity scale per joint.
        assert(sample.joints.size() == authoredJoints.size() &&
               sample.joints.size() == kJointCount);
        for (std::size_t j = 0; j < kJointCount; ++j) {
            assert(sample.joints[j] == authoredJoints[j].GetString());
            assert(sample.scales[j] == GfVec3h(1.0f));
        }
    }
    std::printf("execVrm joint transforms: the retarget's arrays, with the "
                "rig's joints and identity scales, and a frame change reaches "
                "them across the link\n");
}

// ---------------------------------------------------------------------------
// Authored, it is what UsdSkel resolves
// ---------------------------------------------------------------------------
void TestAuthoredTheSampleIsWhatUsdSkelResolves(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    vrmRetarget::JointLocalTransforms sample;
    {
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        ArmAt(system, request, 24.0);
        sample = SampleAt(system.Compute(request));
    }
    const VtMatrix4dArray rest = RestTransforms(rig);

    // ---- with scales: UsdSkel's own composition of the node's value ---------
    AuthorAsTheToolDoes(rig, kBakedPath, sample, /* withScales = */ true);
    const VtMatrix4dArray locals = ResolvedLocals(rig, 24.0);
    const VtMatrix4dArray composed = Composed(sample);
    assert(locals.size() == kJointCount);
    for (std::size_t j = 0; j < kJointCount; ++j) {
        if (locals[j] != composed[j]) {
            std::fprintf(stderr, "joint %zu: UsdSkel resolved another matrix "
                                 "than it composes from the sample\n", j);
            assert(false);
        }
    }
    // And the clip moved the rig: at frame 24 the spine is turned 30 degrees
    // away from its rest.
    assert(!NearMatrix(locals[kSpineJoint], rest[kSpineJoint], 1e-3));

    // ---- without scales: the rig's rest, with no word said -------------------
    // The same arrays, the same binding, `scales` never authored. UsdSkel reads
    // the three as a unit, so the animation maps no joint and every local is
    // the rest -- the reason `scales` is part of the value rather than
    // something a writer is trusted to add.
    AuthorAsTheToolDoes(rig, kUnscaledPath, sample, /* withScales = */ false);
    const VtMatrix4dArray unscaled = ResolvedLocals(rig, 24.0);
    assert(unscaled == rest &&
           "an animation with no scales no longer resolves to the rest pose; "
           "revisit why the sample carries them");

    // ---- arrays that do not pair with `joints`: the rest again --------------
    // What the node would answer a driver's pose one joint short if it did not
    // refuse it: seven joints and scales, six rotations and translations.
    // UsdSkel maps no joint of it either, so the refusal is not strictness --
    // answered, the pose would bake to the rig standing still.
    vrmRetarget::JointLocalTransforms shortSample = sample;
    shortSample.rotations.pop_back();
    shortSample.translations.pop_back();
    AuthorAsTheToolDoes(rig, kShortPath, shortSample, /* withScales = */ true);
    assert(ResolvedLocals(rig, 24.0) == rest &&
           "UsdSkel now resolves arrays that do not pair with their joints; "
           "revisit the joint-count refusal");
    std::printf("execVrm joint transforms: authored as the tool authors them, "
                "UsdSkel resolves the sample's own composition; without "
                "scales, or one joint short, the rig's rest\n");
}

// ---------------------------------------------------------------------------
// What the identity scale costs on a scaled rest
// ---------------------------------------------------------------------------
void TestARigsRestScaleIsNotKept(const std::string& fixture)
{
    // Frame 0 is the clip standing at its own rest, which the retarget lands
    // on the rig's rest at every joint: the case where the baked sample should
    // be the rig's rest, and is, but for one joint.
    const Rig rig = Open(fixture);
    vrmRetarget::JointLocalTransforms sample;
    {
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        ArmAt(system, request, 0.0);
        sample = SampleAt(system.Compute(request));
    }
    assert(sample.timestamp == 0.0);
    const VtMatrix4dArray rest = RestTransforms(rig);
    AuthorAsTheToolDoes(rig, kBakedPath, sample, /* withScales = */ true);
    const VtMatrix4dArray locals = ResolvedLocals(rig, 0.0);

    for (std::size_t j = 0; j < kJointCount; ++j) {
        if (j == kArmJoint) {
            continue;
        }
        assert(NearMatrix(locals[j], rest[j], 1e-6) &&
               "a joint at rest in the clip did not land on the rig's rest");
    }

    // The arm rests turned 90 degrees about +Z and scaled by 2. The sample
    // keeps the turn and the place and states a scale of 1, and UsdSkel takes
    // an animated joint's transform from the animation whole.
    const GfMatrix4d& restArm = rest[kArmJoint];
    const GfMatrix4d& bakedArm = locals[kArmJoint];
    for (int row = 0; row < 3; ++row) {
        assert(std::abs(RowLength(restArm, row) - 2.0) < 1e-9);
        assert(std::abs(RowLength(bakedArm, row) - 1.0) < 1e-6);
    }
    GfMatrix4d unscaledRest = restArm;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            unscaledRest[row][column] /= 2.0;
        }
    }
    assert(NearMatrix(bakedArm, unscaledRest, 1e-6) &&
           "the baked arm differs from its rest by more than the scale");
    std::printf("execVrm joint transforms: at the clip's rest every joint lands "
                "on the rig's rest but the arm, whose rest scale of 2 becomes "
                "1 (rows %.3f -> %.3f)\n",
                RowLength(restArm, 0), RowLength(bakedArm, 0));
}

// ---------------------------------------------------------------------------
// A driver's retarget
// ---------------------------------------------------------------------------
void TestADriversRetargetReachesTheSample(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    ArmAt(system, request, 24.0);
    const vrmRetarget::JointLocalTransforms unchanged =
        SampleAt(system.Compute(request));

    const ExecUsdValueKey retargetKey(rig.humanoid, kRetarget);
    auto computeWith = [&](const vrmRetarget::RetargetedPose& pose) {
        std::vector<ExecUsdValueOverride> overrides;
        overrides.push_back(ExecUsdValueOverride{retargetKey, VtValue(pose)});
        return system.ComputeWithOverrides(request, std::move(overrides));
    };

    // A pose a driver retargeted itself -- filtered, blended, held -- reaches
    // the node as its own answer would.
    vrmRetarget::RetargetedPose held;
    held.timestamp = 0.5;
    held.rotations.assign(kJointCount, GfQuatf(1.0f));
    held.translations.assign(kJointCount, GfVec3f(0.0f));
    held.rotations[kHeadJoint] = About(kY, -60.0f);
    {
        TfErrorMark mark;
        ExecUsdCacheView view = computeWith(held);
        assert(mark.IsClean());
        const vrmRetarget::JointLocalTransforms sample = SampleAt(view);
        assert(sample.timestamp == 0.5);
        assert(sample.rotations == held.rotations &&
               sample.translations == held.translations);
        assert(sample.joints == unchanged.joints &&
               sample.scales == unchanged.scales);
    }

    // One not retargeted onto this rig is refused: the one route to a pose
    // whose arrays do not pair with the rig's joints.
    {
        vrmRetarget::RetargetedPose shortPose = held;
        shortPose.rotations.pop_back();
        shortPose.translations.pop_back();
        TfErrorMark mark;
        ExecUsdCacheView view = computeWith(shortPose);
        AssertRefused(view, kSampleKey);
        assert(MarkNames(mark, "vrm.computeJointLocalTransforms: the retargeted "
                               "pose has 6 rotations and 6 translations, and "
                               "the rig has 7 joints"));
        mark.Clear();
    }

    // Past the map, a rig that does not come back is this node's to refuse.
    // With `vrm:skeleton` naming nothing the map refuses, and the overridden
    // retarget still answers: what reaches the node is a pose with no rig.
    {
        assert(rig.humanoid.GetRelationship(kSkeletonRel).SetTargets({}));
        TfErrorMark mark;
        ExecUsdCacheView view = computeWith(held);
        AssertRefused(view, kSampleKey);
        assert(MarkNames(mark, "vrm.computeJointLocalTransforms: 'vrm:skeleton' "
                               "brought back 0 skeletons"));
        mark.Clear();
        assert(rig.humanoid.GetRelationship(kSkeletonRel)
                   .SetTargets({kTargetPath}));
    }

    // And no override survives its call.
    assert(SampleAt(system.Compute(request)) == unchanged);
    std::printf("execVrm joint transforms: a driver's retarget reaches it, one "
                "that does not pair with the rig is refused, and so is one "
                "with no rig\n");
}

// ---------------------------------------------------------------------------
// Refusals and invalidation
// ---------------------------------------------------------------------------
void TestARetargetThatRefusedIsRefused(const std::string& fixture)
{
    // The default time code, where the retarget refuses by design.
    {
        const Rig rig = Open(fixture);
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kRetargetKey);
        AssertRefused(view, kSampleKey);
        assert(MarkNames(mark, "vrm.humanoidRetarget: the system is at the "
                               "default time code"));
        assert(MarkNames(mark, "vrm.computeJointLocalTransforms: the "
                               "humanoid's vrm.humanoidRetarget answered "
                               "nothing"));
        mark.Clear();
    }
    // A statement the retarget cannot honour, at a real frame.
    {
        const Rig rig = Open(fixture);
        assert(rig.humanoid.CreateAttribute(kRootMotion, SdfValueTypeNames->Token)
                   .Set(TfToken("sideways")));
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        ArmAt(system, request, 24.0);
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kSampleKey);
        assert(MarkNames(mark, "'sideways', which is none of hips, root and "
                               "ignore"));
        assert(MarkNames(mark, "vrm.computeJointLocalTransforms: the "
                               "humanoid's vrm.humanoidRetarget answered "
                               "nothing"));
        mark.Clear();
    }
    std::printf("execVrm joint transforms: a retarget that refused leaves "
                "nothing to shape, and says so\n");
}

void TestInvalidationReachesTheSample(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    ExecUsdSystem system(rig.stage);

    std::set<int> reported;
    ExecUsdRequest request = system.BuildRequest(
        KeysFor(rig),
        [&](const ExecRequestIndexSet& indices, const EfTimeInterval&) {
            reported.insert(indices.begin(), indices.end());
        });
    assert(request.IsValid());
    ArmAt(system, request, 24.0);
    vrmRetarget::JointLocalTransforms sample = SampleAt(system.Compute(request));

    // ---- a root-motion statement: the retarget and this, not the rig --------
    reported.clear();
    assert(rig.humanoid.CreateAttribute(kRootMotion, SdfValueTypeNames->Token)
               .Set(TfToken("ignore")));
    assert(reported == std::set<int>({kRetargetKey, kSampleKey}) &&
           "a root-motion statement did not reach exactly the retarget and "
           "the joint transforms");
    {
        const vrmRetarget::JointLocalTransforms ignored =
            SampleAt(system.Compute(request));
        assert(ignored.translations != sample.translations);
        assert(ignored.rotations == sample.rotations);
        sample = ignored;
    }

    // ---- a key of the clip ---------------------------------------------------
    reported.clear();
    {
        UsdAttribute rotations = rig.clipAnimation.GetAttribute(kRotations);
        VtArray<GfQuatf> values;
        assert(rotations.Get(&values, UsdTimeCode(24.0)));
        values[4] = About(kY, 45.0f);  // the head
        assert(rotations.Set(values, UsdTimeCode(24.0)));
    }
    assert(reported.count(kRetargetKey) && reported.count(kSampleKey) &&
           !reported.count(kTargetKey));
    {
        const vrmRetarget::JointLocalTransforms turned =
            SampleAt(system.Compute(request));
        assert(turned.rotations[kHeadJoint] != sample.rotations[kHeadJoint]);
        sample = turned;
    }

    // ---- a rest of the rig: the rig, the retarget and this -------------------
    // The Root joint, which no bone binds: its rest moves and the retarget
    // carries it, since a joint the clip does not drive stays at rest.
    reported.clear();
    {
        UsdAttribute restTransforms = rig.target.GetAttribute(kRestTransforms);
        VtMatrix4dArray rest;
        assert(restTransforms.Get(&rest));
        rest[kRootJointSlot].SetTranslateOnly(GfVec3d(0, 0, 0.5));
        assert(restTransforms.Set(rest));
    }
    assert(reported.count(kTargetKey) && reported.count(kRetargetKey) &&
           reported.count(kSampleKey) &&
           "a rest edit of the rig did not reach the joint transforms");
    {
        const vrmRetarget::JointLocalTransforms moved =
            SampleAt(system.Compute(request));
        assert(moved.translations[kRootJointSlot] == GfVec3f(0, 0, 0.5f));
        assert(moved.joints == sample.joints);
    }
    std::printf("execVrm joint transforms: a statement, a key and a rest each "
                "reach it, with no request rebuilt\n");
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2 && "usage: execVrm_joint_transforms <retargeted_rig.usda>");
    const std::string fixture = argv[1];

    // The map leaves 49 bones unbound; see the humanoid suite.
    Warnings all;

    TestTheSampleIsTheRetargetInAnAnimationsShape(fixture);
    TestAuthoredTheSampleIsWhatUsdSkelResolves(fixture);
    TestARigsRestScaleIsNotKept(fixture);
    TestADriversRetargetReachesTheSample(fixture);
    TestARetargetThatRefusedIsRefused(fixture);
    TestInvalidationReachesTheSample(fixture);

    for (const std::string& warning : all.Take("")) {
        if (warning.find("No value set for output") == std::string::npos) {
            std::printf("  also warned: %s\n", warning.c_str());
        }
    }
    std::puts("execVrm joint transforms: all checks passed");
    return 0;
}
