// SPDX-License-Identifier: Apache-2.0
//
// execVrm through the built bundle: `vrm.computeTargetSkeleton` on a
// `UsdSkelSkeleton` and `vrm.computeHumanoidMap` on a prim with `VrmHumanoidAPI`
// applied, reached only through OpenExec's own lazy loading of the staged
// plugInfo.json -- the path a packaged bundle takes. Like every execMotion suite
// it does not link the plugin.
//
// What no execMotion suite could reach, and so what this one measures:
//
//   * a computation registered on an **applied API schema from this
//     workspace's own schema bundle**, resolving on a prim whose typed schema
//     (`UsdGeomScope`) another plugin declares;
//   * that the vocabulary the node declares its fifty-five inputs from and the
//     schema's own `vrm:humanBones:*` properties are the same names;
//   * a relationship fan-in **across schemas**: a computation on the API
//     schema reading one registered on a typed schema, on another prim, and
//     invalidation following it -- an edit of the skeleton, of a binding, and
//     of the relationship's target; and
//   * with `--without-schema`, run under an environment that registers no
//     vrmSchema: what the bundle's one runtime requirement on that bundle
//     looks like when it is not met.
//
// It links vrmRetarget for the two result types and reads them through their
// accessors only; every expected value is written from the fixture.

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/diagnosticMgr.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/status.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/tf/warning.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/value.h"

#include "pxr/exec/exec/request.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primDefinition.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/schemaRegistry.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <motionCore/Humanoid.h>
#include <vrmRetarget/HumanoidMap.h>
#include <vrmRetarget/TargetSkeleton.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace {

using motion::HumanBone;

const TfToken kTargetSkeleton("vrm.computeTargetSkeleton");
const TfToken kHumanoidMap("vrm.computeHumanoidMap");
const TfToken kSkeletonRel("vrm:skeleton");
const TfToken kRestTransforms("restTransforms");
const TfToken kJoints("joints");

// The value keys every request here asks for, in this order.
constexpr int kSkeletonKey = 0;
constexpr int kMapKey = 1;

const char* const kRoot = "Root";
const char* const kHips = "Root/J_Bip_C_Hips";
const char* const kUpperArm =
    "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest/J_Bip_L_UpperArm";

TfToken BoneAttribute(HumanBone bone)
{
    return TfToken("vrm:humanBones:" + std::string(motion::HumanBoneName(bone)));
}

bool NearlyEqual(float a, float b)
{
    return std::abs(a - b) <= 1e-6f;
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

// The warnings posted while one is alive. 26.08 posts a `TF_WARN` for every
// input node that computes no value and then fills that input with the type's
// fallback -- which is how an unauthored schema attribute reaches a callback --
// and a warning is not an error, so a `TfErrorMark` cannot see it. Counting
// them is what turns "the fallback path ran" from an inference into a
// measurement. Warnings are posted from exec's worker threads, hence the lock.
//
// With a delegate installed the diagnostic manager hands everything to it
// instead of printing, so errors and statuses are printed here rather than
// swallowed: an error outside any mark should still be seen.
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

    // The warnings seen since the last call that contain `what`; every
    // warning seen is forgotten.
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
    UsdPrim skeleton;
    UsdPrim humanoid;
};

// Opened directly and edited in memory, never saved -- each case opens its own,
// so each differs from the fixture by the one statement it authors.
Rig Open(const std::string& fixture)
{
    Rig rig;
    rig.stage = UsdStage::Open(fixture);
    assert(rig.stage && "the rig fixture did not open");
    rig.skeleton = rig.stage->GetPrimAtPath(SdfPath("/Asset/skel/Skeleton"));
    rig.humanoid = rig.stage->GetPrimAtPath(SdfPath("/Asset/rig/Humanoid"));
    assert(rig.skeleton && rig.humanoid &&
           "the rig fixture is missing its skeleton or its humanoid");
    return rig;
}

std::vector<ExecUsdValueKey> KeysFor(const Rig& rig)
{
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(rig.skeleton, kTargetSkeleton);
    keys.emplace_back(rig.humanoid, kHumanoidMap);
    return keys;
}

vrmRetarget::TargetSkeleton SkeletonAt(const ExecUsdCacheView& view)
{
    const VtValue value = view.Get(kSkeletonKey);
    assert(!value.IsEmpty() &&
           "no skeleton came back -- if the plugInfo is unstaged this is what "
           "it looks like, not a load error");
    assert(value.IsHolding<vrmRetarget::TargetSkeleton>() &&
           "vrm.computeTargetSkeleton did not return a TargetSkeleton");
    return value.UncheckedGet<vrmRetarget::TargetSkeleton>();
}

vrmRetarget::HumanoidMap MapAt(const ExecUsdCacheView& view)
{
    const VtValue value = view.Get(kMapKey);
    assert(!value.IsEmpty() && "no humanoid map came back");
    assert(value.IsHolding<vrmRetarget::HumanoidMap>() &&
           "vrm.computeHumanoidMap did not return a HumanoidMap");
    return value.UncheckedGet<vrmRetarget::HumanoidMap>();
}

// A refusal, which in this bundle is **no value at all**.
void AssertRefused(const ExecUsdCacheView& view, int index)
{
    assert(view.Get(index).IsEmpty() &&
           "a refusal came back carrying a value, which puts it back where a "
           "consumer cannot tell it from an answer");
}

// ---------------------------------------------------------------------------
// The vocabulary is the schema's
// ---------------------------------------------------------------------------
// The node declares one input per bone of motionCore's vocabulary, spelled
// `vrm:humanBones:<name>`. An input declared for an attribute the schema does
// not define never carries a value and nothing reports it, so the two lists
// are compared here, both ways, off the schema's own prim definition.
void TestTheSchemaDefinesEveryBoneOfTheVocabulary()
{
    const UsdPrimDefinition* const definition =
        UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(
            TfToken("VrmHumanoidAPI"));
    assert(definition &&
           "VrmHumanoidAPI is not a registered applied schema -- vrmSchema's "
           "plugInfo is not on the plugin path");

    std::set<TfToken> schemaBones;
    for (const TfToken& name : definition->GetPropertyNames()) {
        if (TfStringStartsWith(name.GetString(), "vrm:humanBones:")) {
            schemaBones.insert(name);
        }
    }

    std::set<TfToken> vocabulary;
    for (std::size_t slot = 0; slot < motion::HumanBoneCount; ++slot) {
        vocabulary.insert(BoneAttribute(static_cast<HumanBone>(slot)));
    }

    for (const TfToken& bone : vocabulary) {
        if (!schemaBones.count(bone)) {
            std::fprintf(stderr, "the schema defines no %s\n", bone.GetText());
        }
    }
    for (const TfToken& bone : schemaBones) {
        if (!vocabulary.count(bone)) {
            std::fprintf(stderr, "the vocabulary has no %s\n", bone.GetText());
        }
    }
    assert(schemaBones == vocabulary &&
           "motionCore's bone vocabulary and VrmHumanoidAPI's properties "
           "disagree, so an input is declared that never carries a value");
    std::printf("execVrm humanoid: the schema's %zu bones are the vocabulary's "
                "%zu\n",
                schemaBones.size(), vocabulary.size());
}

// ---------------------------------------------------------------------------
// The rig the fixture states
// ---------------------------------------------------------------------------
void TestTheRigComputes(const std::string& fixture)
{
    Warnings warnings;
    const Rig rig = Open(fixture);
    ExecUsdSystem system(rig.stage);

    std::set<int> valueReported;
    std::set<int> timeReported;
    ExecUsdRequest request = system.BuildRequest(
        KeysFor(rig),
        [&](const ExecRequestIndexSet& indices, const EfTimeInterval&) {
            valueReported.insert(indices.begin(), indices.end());
        },
        [&](const ExecRequestIndexSet& indices) {
            timeReported.insert(indices.begin(), indices.end());
        });
    assert(request.IsValid() &&
           "a request over a typed schema and an applied one did not compile");

    vrmRetarget::TargetSkeleton skeleton;
    vrmRetarget::HumanoidMap map;
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        skeleton = SkeletonAt(view);
        map = MapAt(view);
        assert(mark.IsClean() && "the fixture's rig posted an error");
    }

    // ---- the skeleton --------------------------------------------------------
    const std::vector<vrmRetarget::TargetJoint>& joints = skeleton.GetJoints();
    assert(joints.size() == 7);
    assert(joints[0].token == kRoot && joints[1].token == kHips &&
           joints[6].token == kUpperArm);
    const int parents[] = {-1, 0, 1, 2, 3, 4, 3};
    for (std::size_t i = 0; i < joints.size(); ++i) {
        assert(joints[i].parent == parents[i]);
    }
    const float half = std::sqrt(0.5f);
    const GfQuatf arm = joints[6].restRotation;
    assert(NearlyEqual(arm.GetReal(), half) &&
           NearlyEqual(arm.GetImaginary()[0], 0.0f) &&
           NearlyEqual(arm.GetImaginary()[1], 0.0f) &&
           NearlyEqual(arm.GetImaginary()[2], half) &&
           "the arm's scaled rest transform did not come back as its rotation");
    assert(NearlyEqual(joints[6].restTranslation[0], 0.1f) &&
           NearlyEqual(joints[6].restTranslation[1], 0.15f));

    // ---- the map -------------------------------------------------------------
    // Six bones, each at the index its joint path has in the skeleton; the
    // custom `leftUpperArmTwist` names the hips joint and is not read, or this
    // would be a duplicate-joint refusal.
    assert(map.GetMappedCount() == 6 &&
           "the map does not hold exactly the six bones the humanoid states");
    assert(map.GetJointIndex(HumanBone::Hips) == 1);
    assert(map.GetJointIndex(HumanBone::Spine) == 2);
    assert(map.GetJointIndex(HumanBone::Chest) == 3);
    assert(map.GetJointIndex(HumanBone::Neck) == 4);
    assert(map.GetJointIndex(HumanBone::Head) == 5);
    assert(map.GetJointIndex(HumanBone::LeftUpperArm) == 6);
    assert(map.FindMissingRequiredBones().size() == 11);

    // ---- what the forty-nine unauthored bones cost -------------------------
    // Every `vrm:humanBones:*` attribute is defined by the applied schema, so
    // every one of the fifty-five has an input node, authored or not -- and the
    // forty-nine the fixture does not author compute no value, are filled with
    // the empty token, and post one warning each. That is how an unbound bone
    // reaches the callback as "" rather than as nothing, and it is measured
    // here so the day 26.08's executor stops doing it is a red test.
    {
        const std::vector<std::string> unset =
            warnings.Take("No value set for output");
        std::set<std::string> bonesWarned;
        for (const std::string& warning : unset) {
            assert(warning.find("/Asset/rig/Humanoid.vrm:humanBones:") !=
                       std::string::npos &&
                   "a warning named something other than an unauthored bone");
            bonesWarned.insert(warning);
        }
        std::printf("execVrm humanoid: the first compute warned %zu times, "
                    "about %zu distinct bone inputs\n",
                    unset.size(), bonesWarned.size());
        assert(unset.size() == motion::HumanBoneCount - 6 &&
               bonesWarned.size() == unset.size() &&
               "not exactly one warning per unauthored bone");
    }

    // ---- recompute with nothing changed ------------------------------------
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(valueReported.empty() &&
               "an unchanged stage reported an invalidation");
        assert(SkeletonAt(view) == skeleton && MapAt(view) == map);
        // Cached, so the input nodes did not run and nothing warned again.
        assert(warnings.Take("No value set for output").empty() &&
               "an unchanged recompute ran the unauthored bones' inputs again");
    }

    // ---- moving time reports nothing ---------------------------------------
    // Every input of both nodes is `uniform` and neither declares
    // `computeTime`. A rig reported here would be recomputed, with everything
    // downstream of it, on every frame a clip plays.
    system.ChangeTime(UsdTimeCode(24.0));
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(timeReported.empty() &&
               "ChangeTime reported a rig, which has no time-dependent input");
        assert(SkeletonAt(view) == skeleton && MapAt(view) == map);
    }
    std::printf("execVrm humanoid: the rig and its map compute on a Scope "
                "through the applied schema, and neither moves with time\n");
}

// ---------------------------------------------------------------------------
// Invalidation across the relationship
// ---------------------------------------------------------------------------
void TestInvalidationFollowsTheRelationship(const std::string& fixture)
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

    vrmRetarget::TargetSkeleton skeleton;
    vrmRetarget::HumanoidMap map;
    {
        ExecUsdCacheView view = system.Compute(request);
        skeleton = SkeletonAt(view);
        map = MapAt(view);
    }

    // ---- a binding moves: the map is reported, the skeleton is not ---------
    reported.clear();
    assert(rig.humanoid.GetAttribute(BoneAttribute(HumanBone::Head))
               .Set(TfToken(kRoot)));
    assert(reported.count(kMapKey) &&
           "rebinding a bone did not reach vrm.computeHumanoidMap");
    assert(!reported.count(kSkeletonKey) &&
           "rebinding a bone reported the skeleton, which does not read it");
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(SkeletonAt(view) == skeleton);
        map = MapAt(view);
        assert(map.GetJointIndex(HumanBone::Head) == 0);
    }

    // ---- the skeleton's rest moves: both are reported ----------------------
    // The map reads the skeleton across `vrm:skeleton`, so an edit of the
    // skeleton reaches it -- and it is reported although its value comes back
    // unchanged: a rest transform moves no joint index. Invalidation follows
    // the dependency, not the value.
    reported.clear();
    {
        UsdAttribute rest = rig.skeleton.GetAttribute(kRestTransforms);
        VtArray<GfMatrix4d> matrices;
        assert(rest.Get(&matrices));
        matrices[1].SetTranslateOnly(GfVec3d(0.0, 1.5, 0.0));
        assert(rest.Set(matrices));
    }
    assert(reported.count(kSkeletonKey) &&
           "a rest transform edit did not reach vrm.computeTargetSkeleton");
    assert(reported.count(kMapKey) &&
           "a skeleton edit did not reach the map across vrm:skeleton");
    {
        ExecUsdCacheView view = system.Compute(request);
        const vrmRetarget::TargetSkeleton moved = SkeletonAt(view);
        assert(moved != skeleton);
        assert(NearlyEqual(moved.GetJoints()[1].restTranslation[1], 1.5f));
        assert(MapAt(view) == map &&
               "a rest transform moved a joint index");
    }

    // ---- the relationship's target moves: the map follows it ---------------
    // A second skeleton with the fixture's joints in the reverse order, so
    // every index the map holds changes and the only way to get them is from
    // the skeleton the relationship now targets. No request is rebuilt.
    {
        UsdPrim other = rig.stage->DefinePrim(SdfPath("/Asset/skel/Reversed"),
                                              TfToken("Skeleton"));
        VtArray<TfToken> tokens;
        assert(rig.skeleton.GetAttribute(kJoints).Get(&tokens));
        std::reverse(tokens.begin(), tokens.end());
        assert(other.CreateAttribute(kJoints, SdfValueTypeNames->TokenArray,
                                     /*custom=*/false, SdfVariabilityUniform)
                   .Set(tokens));
        assert(other.CreateAttribute(kRestTransforms,
                                     SdfValueTypeNames->Matrix4dArray,
                                     /*custom=*/false, SdfVariabilityUniform)
                   .Set(VtArray<GfMatrix4d>(tokens.size(), GfMatrix4d(1.0))));
    }
    reported.clear();
    assert(rig.humanoid.GetRelationship(kSkeletonRel)
               .SetTargets({SdfPath("/Asset/skel/Reversed")}));
    assert(reported.count(kMapKey) &&
           "retargeting vrm:skeleton did not reach the map");
    {
        ExecUsdCacheView view = system.Compute(request);
        const vrmRetarget::HumanoidMap reversed = MapAt(view);
        // Seven joints reversed: index i becomes 6 - i. Head was rebound to
        // Root above, and Root is last now.
        assert(reversed.GetJointIndex(HumanBone::Hips) == 5);
        assert(reversed.GetJointIndex(HumanBone::LeftUpperArm) == 0);
        assert(reversed.GetJointIndex(HumanBone::Head) == 6);
    }
    std::printf("execVrm humanoid: a binding, a rest transform and the "
                "relationship's target each reach the map, with no request "
                "rebuilt\n");
}

// ---------------------------------------------------------------------------
// Refusals, one statement each
// ---------------------------------------------------------------------------
void TestARestPoseThatDoesNotPairIsRefusedAndPropagates(
    const std::string& fixture)
{
    Warnings warnings;
    const Rig rig = Open(fixture);
    UsdAttribute rest = rig.skeleton.GetAttribute(kRestTransforms);
    assert(rest);
    rest.Block();

    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);
    AssertRefused(view, kSkeletonKey);
    AssertRefused(view, kMapKey);
    // ONE rest transform, not zero: a blocked array the schema defines reaches
    // the callback as one fallback element, and the executor says so.
    assert(MarkNames(mark, "vrm.computeTargetSkeleton: the skeleton states 7 "
                           "joints and 1 'restTransforms'") &&
           "a blocked restTransforms did not arrive as one fallback matrix");
    assert(warnings.Take("Skeleton.restTransforms").size() == 1 &&
           "the fallback path did not warn about the blocked restTransforms");
    // The skeleton's refusal vanishes from the map's fan-in, and the second
    // read of the relationship is what turns that into a refusal rather than
    // a map resolved against nothing.
    assert(MarkNames(mark, "vrm.computeHumanoidMap: 'vrm:skeleton' targets "
                           "</Asset/skel/Skeleton>, which answered no") &&
           "the map did not refuse a skeleton that refused");
    mark.Clear();
    std::printf("execVrm humanoid: a blocked rest pose is refused, and the "
                "map refuses in turn\n");
}

// ---------------------------------------------------------------------------
// What a skeleton that authors nothing arrives as
// ---------------------------------------------------------------------------
// Four skeletons beside the fixture's, each differing in what it authors:
//
//   * /Bare authors nothing -- so `joints` and `restTransforms` both reach the
//     callback as ONE fallback element, an empty token and an identity matrix,
//     and the counts pair. Only the empty token can say it, and it is refused.
//   * /Empty authors both as empty arrays, which is a value: zero elements, no
//     warning, and the empty skeleton as the answer.
//   * /Jointless authors `joints = []` and no rest pose, so it arrives as zero
//     joints and one fallback matrix -- and is the empty skeleton too, because
//     with no joint for a matrix to belong to, none becomes a number.
//   * /One authors one joint and no rest pose, and is answered with the
//     identity rest the fallback supplied -- pinned rather than fixed, because
//     it is indistinguishable here from an authored identity, and the offline
//     tool answers it identically.
void TestWhatAnUnauthoredSkeletonArrivesAs(const std::string& fixture)
{
    Warnings warnings;
    const Rig rig = Open(fixture);
    const TfToken skeletonType("Skeleton");
    UsdPrim bare =
        rig.stage->DefinePrim(SdfPath("/Asset/skel/Bare"), skeletonType);
    UsdPrim empty =
        rig.stage->DefinePrim(SdfPath("/Asset/skel/Empty"), skeletonType);
    UsdPrim jointless =
        rig.stage->DefinePrim(SdfPath("/Asset/skel/Jointless"), skeletonType);
    UsdPrim one = rig.stage->DefinePrim(SdfPath("/Asset/skel/One"), skeletonType);
    assert(bare && empty && jointless && one);
    assert(empty.GetAttribute(kJoints).Set(VtArray<TfToken>()));
    assert(empty.GetAttribute(kRestTransforms).Set(VtArray<GfMatrix4d>()));
    assert(jointless.GetAttribute(kJoints).Set(VtArray<TfToken>()));
    assert(one.GetAttribute(kJoints).Set(VtArray<TfToken>({TfToken(kRoot)})));

    ExecUsdSystem system(rig.stage);
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(bare, kTargetSkeleton);
    keys.emplace_back(empty, kTargetSkeleton);
    keys.emplace_back(jointless, kTargetSkeleton);
    keys.emplace_back(one, kTargetSkeleton);
    ExecUsdRequest request = system.BuildRequest(std::move(keys));

    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);

    // Every warning the compute posted, counted per skeleton: each one that
    // takes the fallback path says so once per attribute that took it.
    const std::vector<std::string> unset =
        warnings.Take("No value set for output");
    auto warnedFor = [&unset](const std::string& prim) {
        std::size_t count = 0;
        for (const std::string& warning : unset) {
            if (warning.find(prim + ".") != std::string::npos) {
                ++count;
            }
        }
        return count;
    };

    AssertRefused(view, 0);
    assert(MarkNames(mark, "a 'joints' entry is the empty token") &&
           "a skeleton that authors no joints was not refused for it");
    assert(warnedFor("/Asset/skel/Bare") == 2 &&
           "the bare skeleton's two attributes did not both take the "
           "fallback path");

    for (const int index : {1, 2}) {
        const VtValue value = view.Get(index);
        assert(value.IsHolding<vrmRetarget::TargetSkeleton>() &&
               value.UncheckedGet<vrmRetarget::TargetSkeleton>().IsEmpty() &&
               "a skeleton with no joints did not come back empty");
    }
    assert(warnedFor("/Asset/skel/Empty") == 0 &&
           "authored empty arrays took the fallback path");
    assert(warnedFor("/Asset/skel/Jointless") == 1 &&
           "the jointless skeleton's rest pose did not take the fallback "
           "path, so this case measures nothing");

    {
        const VtValue value = view.Get(3);
        assert(value.IsHolding<vrmRetarget::TargetSkeleton>() &&
               "a one-joint skeleton with no rest pose was refused, which "
               "would mean the fallback is no longer one identity matrix");
        const vrmRetarget::TargetSkeleton& skeleton =
            value.UncheckedGet<vrmRetarget::TargetSkeleton>();
        assert(skeleton.GetSize() == 1);
        assert(skeleton.GetJoints()[0].restRotation ==
                   GfQuatf(1.0f, GfVec3f(0.0f)) &&
               skeleton.GetJoints()[0].restTranslation == GfVec3f(0.0f));
    }
    assert(warnedFor("/Asset/skel/One") == 1);

    // Nothing but the bare skeleton's refusal was an error.
    std::size_t errors = 0;
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it) {
        ++errors;
    }
    assert(errors == 1);
    mark.Clear();
    std::printf("execVrm humanoid: an unauthored skeleton is one empty token "
                "and is refused; no joints is the empty skeleton, with or "
                "without a rest pose; one joint with no rest is the identity "
                "the fallback supplied\n");
}

void TestABindingTheSkeletonCannotHonourIsRefused(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    // The leaf name, which is the thing a joint-name heuristic would try.
    assert(rig.humanoid.GetAttribute(BoneAttribute(HumanBone::Hips))
               .Set(TfToken("J_Bip_C_Hips")));

    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);
    SkeletonAt(view);
    AssertRefused(view, kMapKey);
    assert(MarkNames(mark, "binds hips -> 'J_Bip_C_Hips'"));
    mark.Clear();
    std::printf("execVrm humanoid: a binding to no joint is refused, by "
                "bone\n");
}

void TestTwoBonesOnOneJointAreRefused(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    assert(rig.humanoid.GetAttribute(BoneAttribute(HumanBone::Spine))
               .Set(TfToken(kHips)));

    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);
    AssertRefused(view, kMapKey);
    assert(MarkNames(mark, "hips -> 'Root/J_Bip_C_Hips', spine -> "
                           "'Root/J_Bip_C_Hips'"));
    mark.Clear();
    std::printf("execVrm humanoid: two bones on one joint are refused\n");
}

void TestTheSkeletonRelationshipIsCounted(const std::string& fixture)
{
    struct Case
    {
        const char* what;
        SdfPathVector targets;
        const char* message;
    };
    const Case cases[] = {
        {"no target", {}, "'vrm:skeleton' reaches nothing on the stage"},
        // A path naming no prim provides no `computePath` either, so it is
        // missing from both reads -- the same refusal as no target at all.
        {"a target naming nothing",
         {SdfPath("/Asset/skel/Nowhere")},
         "'vrm:skeleton' reaches nothing on the stage"},
        {"two targets",
         {SdfPath("/Asset/skel/Skeleton"), SdfPath("/Asset/skel")},
         "'vrm:skeleton' reaches 2 objects"},
        // A prim that is not a skeleton provides no vrm.computeTargetSkeleton,
        // so it is dropped from the fan-in while the network compiles, and
        // only the count of paths notices.
        {"a target that is not a skeleton",
         {SdfPath("/Asset/rig")},
         "'vrm:skeleton' targets </Asset/rig>, which answered no"},
    };

    for (const Case& c : cases) {
        const Rig rig = Open(fixture);
        assert(rig.humanoid.GetRelationship(kSkeletonRel).SetTargets(c.targets));

        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kMapKey);
        if (!MarkNames(mark, c.message)) {
            std::fprintf(stderr, "%s: expected an error naming \"%s\"\n",
                         c.what, c.message);
            assert(false && "the skeleton relationship was not refused as "
                            "expected");
        }
        mark.Clear();
    }
    std::printf("execVrm humanoid: no skeleton, two, and one that is not a "
                "skeleton are each refused\n");
}

// ---------------------------------------------------------------------------
// A target naming nothing is invisible beside one that names a skeleton
// ---------------------------------------------------------------------------
// The one relationship statement this node cannot refuse, pinned rather than
// fixed. A target path with no prim behind it provides neither
// `vrm.computeTargetSkeleton` nor `computePath`, so it is missing from BOTH
// reads of `vrm:skeleton` -- and beside a real skeleton, the relationship then
// reads as naming exactly one object. `motion.blendPoses` catches the same
// drop because its weights are a second statement to count against; a
// humanoid has none, and exec offers no read of a relationship's authored
// targets. So a humanoid naming a skeleton and a path to nothing is answered
// against the skeleton, in either order, with no error.
void TestADanglingSecondTargetIsInvisible(const std::string& fixture)
{
    const vrmRetarget::HumanoidMap expected = [&fixture] {
        const Rig rig = Open(fixture);
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        return MapAt(system.Compute(request));
    }();

    const SdfPath skeleton("/Asset/skel/Skeleton");
    const SdfPath nowhere("/Asset/skel/Nowhere");
    for (const SdfPathVector& targets :
         {SdfPathVector{skeleton, nowhere}, SdfPathVector{nowhere, skeleton}}) {
        const Rig rig = Open(fixture);
        assert(rig.humanoid.GetRelationship(kSkeletonRel).SetTargets(targets));

        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        const vrmRetarget::HumanoidMap map = MapAt(system.Compute(request));
        assert(mark.IsClean() &&
               "a dangling second target was noticed after all -- the "
               "limitation this pins is gone, and the node should now refuse");
        assert(map == expected);
    }
    std::printf("execVrm humanoid: a target naming nothing beside a skeleton "
                "is invisible to both reads, and the map is answered\n");
}

// ---------------------------------------------------------------------------
// Two ways to say nothing about a bone
// ---------------------------------------------------------------------------
void TestAnEmptyTokenAndABlockBindNothing(const std::string& fixture)
{
    for (const bool block : {false, true}) {
        const Rig rig = Open(fixture);
        UsdAttribute arm =
            rig.humanoid.GetAttribute(BoneAttribute(HumanBone::LeftUpperArm));
        assert(arm);
        if (block) {
            arm.Block();
        } else {
            assert(arm.Set(TfToken()));
        }

        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        const vrmRetarget::HumanoidMap map = MapAt(system.Compute(request));
        assert(mark.IsClean() &&
               "saying nothing about a bone was refused");
        assert(map.GetMappedCount() == 5 &&
               !map.IsMapped(HumanBone::LeftUpperArm));
    }
    std::printf("execVrm humanoid: an empty token and a value block both "
                "leave a bone unbound\n");
}

// ---------------------------------------------------------------------------
// The schema is the route, not the attributes
// ---------------------------------------------------------------------------
// The offline tool finds a humanoid by the attribute `vrm:humanBones:hips` and
// never asks whether the schema is applied. A computation is found through the
// schema, so a prim carrying every attribute and no `VrmHumanoidAPI` has no
// humanoid map at all -- the importer applies the schema, and a hand-authored
// stage that does not is the divergence.
void TestAnUnappliedHumanoidHasNoMap(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    UsdPrim bare = rig.stage->DefinePrim(SdfPath("/Asset/rig/Unapplied"),
                                         TfToken("Scope"));
    assert(bare.CreateAttribute(BoneAttribute(HumanBone::Hips),
                                SdfValueTypeNames->Token, /*custom=*/true,
                                SdfVariabilityUniform)
               .Set(TfToken(kHips)));
    assert(bare.CreateRelationship(kSkeletonRel, /*custom=*/true)
               .SetTargets({SdfPath("/Asset/skel/Skeleton")}));

    ExecUsdSystem system(rig.stage);
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(bare, kHumanoidMap);
    ExecUsdRequest request = system.BuildRequest(std::move(keys));
    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);
    assert(view.Get(0).IsEmpty() &&
           "a prim without VrmHumanoidAPI produced a humanoid map");
    assert(!mark.IsClean() && "a computation that does not exist was silent");
    mark.Clear();
    std::printf("execVrm humanoid: the attributes without the applied schema "
                "are not a humanoid\n");
}

// ---------------------------------------------------------------------------
// --without-schema: vrmSchema is not in the session
// ---------------------------------------------------------------------------
// Run by its own CTest entry, with PXR_PLUGINPATH_NAME *set* to this bundle
// alone. The bundle links nothing of vrmSchema; what it needs is the type name
// `UsdVrmHumanoidAPI` declared when exec reads its Exec block. So this is what
// a session that composes execVrm without the bundle it requires looks like.
void TestWithoutTheSchemaTheHumanoidIsNotFound(const std::string& fixture)
{
    assert(!UsdSchemaRegistry::GetInstance().FindAppliedAPIPrimDefinition(
               TfToken("VrmHumanoidAPI")) &&
           "vrmSchema is registered after all, so this run measures nothing");

    TfErrorMark mark;
    const Rig rig = Open(fixture);
    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    ExecUsdCacheView view = system.Compute(request);

    // The typed half does not need the schema bundle at all.
    const vrmRetarget::TargetSkeleton skeleton = SkeletonAt(view);
    assert(skeleton.GetSize() == 7);

    // The applied half is not found, and what says why is exec's own coding
    // error at metadata read -- nothing of ours runs.
    AssertRefused(view, kMapKey);
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it) {
        std::printf("  posted: %s\n", it->GetCommentary().c_str());
    }
    assert(MarkNames(mark, "UsdVrmHumanoidAPI") &&
           "nothing named the schema type exec could not resolve");
    mark.Clear();
    std::printf("execVrm humanoid (without vrmSchema): the skeleton computes "
                "and the humanoid map is not found\n");
}

} // namespace

int main(int argc, char** argv)
{
    assert((argc == 2 || argc == 3) &&
           "usage: execVrm_humanoid <humanoid_rig.usda> [--without-schema]");
    const std::string fixture = argv[1];

    if (argc == 3) {
        assert(std::string(argv[2]) == "--without-schema");
        TestWithoutTheSchemaTheHumanoidIsNotFound(fixture);
        std::puts("execVrm humanoid (without vrmSchema): all checks passed");
        return 0;
    }

    // Every case below computes a humanoid that leaves most bones unauthored,
    // and each of those posts the executor's "No value set" warning -- dozens
    // per case, measured once in TestTheRigComputes. Collected here so they do
    // not bury the output, and anything else that was warned is printed.
    Warnings all;

    TestTheSchemaDefinesEveryBoneOfTheVocabulary();
    TestTheRigComputes(fixture);
    TestInvalidationFollowsTheRelationship(fixture);
    TestARestPoseThatDoesNotPairIsRefusedAndPropagates(fixture);
    TestWhatAnUnauthoredSkeletonArrivesAs(fixture);
    TestABindingTheSkeletonCannotHonourIsRefused(fixture);
    TestTwoBonesOnOneJointAreRefused(fixture);
    TestTheSkeletonRelationshipIsCounted(fixture);
    TestADanglingSecondTargetIsInvisible(fixture);
    TestAnEmptyTokenAndABlockBindNothing(fixture);
    TestAnUnappliedHumanoidHasNoMap(fixture);

    for (const std::string& warning : all.Take("")) {
        if (warning.find("No value set for output") == std::string::npos) {
            std::printf("  also warned: %s\n", warning.c_str());
        }
    }
    std::puts("execVrm humanoid: all checks passed");
    return 0;
}
