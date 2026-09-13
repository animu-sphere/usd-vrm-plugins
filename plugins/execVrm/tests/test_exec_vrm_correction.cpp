// SPDX-License-Identifier: Apache-2.0
//
// `vrm.computeRestPoseCorrection` through the built bundle, over
// corrected_rig.usda, reached only through OpenExec's own lazy loading of the
// staged plugInfo.json. Like every exec suite here it does not link the plugin.
//
// What the humanoid suite could not reach, and so what this one measures:
//
//   * a computation reading one computation on its own prim and the SAME
//     computation (`vrm.computeTargetSkeleton`) on two other prims, through two
//     relationships -- the second a relationship no schema defines, on a prim
//     reached through an applied schema;
//   * that the node is `vrmRetarget::ComputeRestPoseCorrection` and nothing
//     more, compared with the library's answer over values written from the
//     fixture;
//   * invalidation from three rigs' worth of statements -- the source's rest,
//     a binding, the target's rest, and the source relationship's target --
//     and from none of them on a time change; and
//   * every refusal, one statement each, and the one relationship statement
//     the count cannot see.
//
// It links vrmRetarget for the result types and to build the expected values;
// the claim about the library is exactly that the node is one call of it.

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/diagnosticMgr.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/status.h"
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
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <motionCore/Humanoid.h>
#include <vrmRetarget/HumanoidMap.h>
#include <vrmRetarget/RestPose.h>
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

using motion::HumanBone;

const TfToken kTargetSkeleton("vrm.computeTargetSkeleton");
const TfToken kHumanoidMap("vrm.computeHumanoidMap");
const TfToken kCorrection("vrm.computeRestPoseCorrection");
const TfToken kSkeletonRel("vrm:skeleton");
const TfToken kSourceRel("vrm:retarget:sourceSkeleton");
const TfToken kRestTransforms("restTransforms");
const TfToken kJoints("joints");

const SdfPath kTargetPath("/Asset/skel/Skeleton");
const SdfPath kHumanoidPath("/Asset/rig/Humanoid");
const SdfPath kClipPath("/Clip/HumanoidSkeleton");
const SdfPath kPosedPath("/Posed/HumanoidSkeleton");

// The value keys every request here asks for, in this order.
constexpr int kTargetKey = 0;
constexpr int kSourceKey = 1;
constexpr int kMapKey = 2;
constexpr int kCorrectionKey = 3;

TfToken BoneAttribute(HumanBone bone)
{
    return TfToken("vrm:humanBones:" + std::string(motion::HumanBoneName(bone)));
}

std::size_t Slot(HumanBone bone)
{
    return static_cast<std::size_t>(bone);
}

// Orientation, not representation: q and -q are one rotation.
bool SameOrientation(const GfQuatf& a, const GfQuatf& b)
{
    const GfQuatf na = a.GetNormalized();
    const GfQuatf nb = b.GetNormalized();
    return std::abs(std::abs(GfDot(na, nb)) - 1.0f) <= 1e-6f;
}

// Two corrections that apply identically: the same bones corrected, each half
// the same orientation.
bool SameCorrection(const vrmRetarget::RestPoseCorrection& a,
                    const vrmRetarget::RestPoseCorrection& b)
{
    for (std::size_t slot = 0; slot < motion::HumanBoneCount; ++slot) {
        if (a.identity[slot] != b.identity[slot]
            || !SameOrientation(a.pre[slot], b.pre[slot])
            || !SameOrientation(a.post[slot], b.post[slot])) {
            std::fprintf(stderr, "the corrections differ at %s\n",
                         std::string(motion::HumanBoneName(
                                         static_cast<HumanBone>(slot)))
                             .c_str());
            return false;
        }
    }
    return true;
}

GfQuatf About(const GfVec3f& axis, float degrees)
{
    const float half = degrees * 3.14159265358979324f / 360.0f;
    return GfQuatf(std::cos(half), axis * std::sin(half));
}

const GfQuatf kIdentity(1.0f, GfVec3f(0.0f));
const GfVec3f kX(1, 0, 0);
const GfVec3f kY(0, 1, 0);
const GfVec3f kZ(0, 0, 1);

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
// the executor warns once per bone per compute of it, which would bury the
// output. Errors and statuses are printed rather than swallowed.
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
    UsdPrim clip;
    UsdPrim posed;
};

// Opened directly and edited in memory, never saved -- each case opens its own.
Rig Open(const std::string& fixture)
{
    Rig rig;
    rig.stage = UsdStage::Open(fixture);
    assert(rig.stage && "the corrected rig fixture did not open");
    rig.target = rig.stage->GetPrimAtPath(kTargetPath);
    rig.humanoid = rig.stage->GetPrimAtPath(kHumanoidPath);
    rig.clip = rig.stage->GetPrimAtPath(kClipPath);
    rig.posed = rig.stage->GetPrimAtPath(kPosedPath);
    assert(rig.target && rig.humanoid && rig.clip && rig.posed &&
           "the corrected rig fixture is missing a prim");
    return rig;
}

std::vector<ExecUsdValueKey> KeysFor(const Rig& rig)
{
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(rig.target, kTargetSkeleton);
    keys.emplace_back(rig.clip, kTargetSkeleton);
    keys.emplace_back(rig.humanoid, kHumanoidMap);
    keys.emplace_back(rig.humanoid, kCorrection);
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

vrmRetarget::RestPoseCorrection CorrectionAt(const ExecUsdCacheView& view,
                                             int index = kCorrectionKey)
{
    return ValueAt<vrmRetarget::RestPoseCorrection>(
        view, index, "rest-pose correction");
}

void AssertRefused(const ExecUsdCacheView& view, int index)
{
    assert(view.Get(index).IsEmpty() &&
           "a refusal came back carrying a value, which puts it back where a "
           "consumer cannot tell it from an answer");
}

// ---------------------------------------------------------------------------
// The expected values, from the fixture's text
// ---------------------------------------------------------------------------
// Written from the fixture rather than read back through the bundle, so an
// expected value never shares a defect with the value it checks -- except the
// one comparison whose claim is that the node IS the library call, which
// feeds the library the same rigs.

// /Clip/HumanoidSkeleton: identity rests, the semantic chain its paths state.
vrmRetarget::SourceRestPose ClipRest()
{
    vrmRetarget::SourceRestPose rest;
    rest.localTranslations[Slot(HumanBone::Hips)] = GfVec3f(0, 1, 0);
    rest.localTranslations[Slot(HumanBone::Spine)] = GfVec3f(0, 0.1f, 0);
    rest.localTranslations[Slot(HumanBone::Chest)] = GfVec3f(0, 0.15f, 0);
    rest.localTranslations[Slot(HumanBone::Neck)] = GfVec3f(0, 0.2f, 0);
    rest.localTranslations[Slot(HumanBone::Head)] = GfVec3f(0, 0.1f, 0);
    rest.localTranslations[Slot(HumanBone::LeftUpperArm)] =
        GfVec3f(0.1f, 0.15f, 0);
    rest.SetParent(HumanBone::Spine, HumanBone::Hips);
    rest.SetParent(HumanBone::Chest, HumanBone::Spine);
    rest.SetParent(HumanBone::Neck, HumanBone::Chest);
    rest.SetParent(HumanBone::Head, HumanBone::Neck);
    rest.SetParent(HumanBone::LeftUpperArm, HumanBone::Chest);
    return rest;
}

// /Posed/HumanoidSkeleton: the arm turned 90 degrees about -Z, the hips a root
// of the semantic chain because `Reference` is no bone -- and Reference's own
// rest, turned about +X, is in no slot.
vrmRetarget::SourceRestPose PosedRest()
{
    vrmRetarget::SourceRestPose rest;
    rest.localTranslations[Slot(HumanBone::Hips)] = GfVec3f(0, 0.9f, 0);
    rest.localTranslations[Slot(HumanBone::Spine)] = GfVec3f(0, 0.1f, 0);
    rest.localTranslations[Slot(HumanBone::Chest)] = GfVec3f(0, 0.15f, 0);
    rest.localRotations[Slot(HumanBone::LeftUpperArm)] = About(kZ, -90.0f);
    rest.localTranslations[Slot(HumanBone::LeftUpperArm)] =
        GfVec3f(0.1f, 0.15f, 0);
    rest.SetParent(HumanBone::Spine, HumanBone::Hips);
    rest.SetParent(HumanBone::Chest, HumanBone::Spine);
    rest.SetParent(HumanBone::LeftUpperArm, HumanBone::Chest);
    return rest;
}

// The target rest rotations the fixture states, by bone.
GfQuatf TargetRest(HumanBone bone)
{
    switch (bone) {
    case HumanBone::Hips: return About(kY, 90.0f);
    case HumanBone::LeftUpperArm: return About(kZ, 90.0f);
    default: return kIdentity;
    }
}

const HumanBone kMapped[] = {HumanBone::Hips,  HumanBone::Spine,
                             HumanBone::Chest, HumanBone::Neck,
                             HumanBone::Head,  HumanBone::LeftUpperArm};

// ---------------------------------------------------------------------------
// The correction the fixture states
// ---------------------------------------------------------------------------
void TestTheCorrectionComputes(const std::string& fixture)
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
    assert(request.IsValid());

    vrmRetarget::TargetSkeleton target;
    vrmRetarget::HumanoidMap map;
    vrmRetarget::RestPoseCorrection correction;
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        target = ValueAt<vrmRetarget::TargetSkeleton>(view, kTargetKey,
                                                      "target skeleton");
        ValueAt<vrmRetarget::TargetSkeleton>(view, kSourceKey,
                                             "source skeleton");
        map = ValueAt<vrmRetarget::HumanoidMap>(view, kMapKey, "humanoid map");
        correction = CorrectionAt(view);
        assert(mark.IsClean() && "the fixture's correction posted an error");
    }

    // ---- the wrapper claim --------------------------------------------------
    // The node IS ComputeRestPoseCorrection over the clip's rest, the target
    // rig and the map: the library's answer over the same rigs, bit for bit.
    assert(correction ==
               vrmRetarget::ComputeRestPoseCorrection(ClipRest(), target, map) &&
           "the node's correction is not the library's over the same rigs");

    // ---- and what it means, from the definition -----------------------------
    // The source rests at identity, so a sample AT the source rest has to land
    // on each mapped joint's own rest -- including the spine, which is not
    // turned itself and still needs a correction because its parent is.
    for (const HumanBone bone : kMapped) {
        assert(SameOrientation(correction.Apply(bone, kIdentity),
                               TargetRest(bone)) &&
               "a sample at the clip's rest did not land on the rig's rest");
    }
    // Every bone below the turned hips is corrected, although only the arm is
    // turned itself: the correction reads the accumulated chain.
    assert(!correction.identity[Slot(HumanBone::Hips)]);
    assert(!correction.identity[Slot(HumanBone::Spine)] &&
           "the spine was left uncorrected under a turned hips");
    assert(!correction.identity[Slot(HumanBone::Head)]);
    assert(!correction.identity[Slot(HumanBone::LeftUpperArm)]);
    // An unmapped bone stays identity: nothing to correct onto.
    assert(correction.identity[Slot(HumanBone::RightUpperArm)]);

    // A rotation away from the rest survives as the same world-space delta.
    // Spine: source parent rest identity, target parent rest the hips' 90 Y.
    {
        const GfQuatf animated = About(kX, 30.0f);
        const GfQuatf retargeted =
            correction.Apply(HumanBone::Spine, animated);
        const GfQuatf parent = About(kY, 90.0f);
        const GfQuatf targetDelta =
            (parent * retargeted) * parent.GetInverse();
        assert(SameOrientation(targetDelta, animated) &&
               "the spine's world delta did not survive the correction");
    }

    // The source skeleton raised no fallback warning: it authors both arrays.
    for (const std::string& warning : warnings.Take("No value set for output")) {
        assert(warning.find("/Clip/") == std::string::npos &&
               "the source skeleton took the fallback path");
    }

    // ---- recompute with nothing changed, and time --------------------------
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(valueReported.empty());
        assert(CorrectionAt(view) == correction);
    }
    system.ChangeTime(UsdTimeCode(24.0));
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(timeReported.empty() &&
               "ChangeTime reported a correction, which reads no time");
        assert(CorrectionAt(view) == correction);
    }
    std::printf("execVrm correction: the node is ComputeRestPoseCorrection over "
                "the clip's rest, lands the clip's rest on the rig's, and "
                "does not move with time\n");
}

// ---------------------------------------------------------------------------
// Invalidation, from each of the three rigs' statements
// ---------------------------------------------------------------------------
void TestInvalidationFollowsBothRelationships(const std::string& fixture)
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

    vrmRetarget::RestPoseCorrection correction =
        CorrectionAt(system.Compute(request));

    // ---- a source rest TRANSLATION moves: reported, and unchanged ----------
    // A correction is rotations only, so moving where the clip's head sits
    // changes nothing in it -- and it is reported anyway, because it reads the
    // skeleton that moved. The humanoid report's "invalidation follows the
    // dependency, not the value", one node on: whatever consumes a correction
    // is recomputed for every rest edit of the clip's rig.
    reported.clear();
    {
        UsdAttribute rest = rig.clip.GetAttribute(kRestTransforms);
        VtArray<GfMatrix4d> matrices;
        assert(rest.Get(&matrices));
        matrices[4].SetTranslateOnly(GfVec3d(0, 0.12, 0));
        assert(rest.Set(matrices));
    }
    assert(reported.count(kCorrectionKey) &&
           "a source rest translation did not reach the correction");
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(CorrectionAt(view) == correction &&
               "a rest translation changed a correction, which is rotations "
               "only");
    }

    // ---- the source's rest moves: the correction, and not the map ----------
    reported.clear();
    {
        UsdAttribute rest = rig.clip.GetAttribute(kRestTransforms);
        VtArray<GfMatrix4d> matrices;
        assert(rest.Get(&matrices));
        // The clip's spine, turned 90 degrees about +X.
        matrices[1] = GfMatrix4d(1, 0, 0, 0,
                                 0, 0, 1, 0,
                                 0, -1, 0, 0,
                                 0, 0.1, 0, 1);
        assert(rest.Set(matrices));
    }
    assert(reported.count(kSourceKey) && reported.count(kCorrectionKey) &&
           "a source rest edit did not reach the correction");
    assert(!reported.count(kMapKey) && !reported.count(kTargetKey) &&
           "a source rest edit reported the target rig");
    {
        ExecUsdCacheView view = system.Compute(request);
        const vrmRetarget::RestPoseCorrection turned = CorrectionAt(view);
        assert(turned != correction);
        // The clip's spine rest is now 90 X: a sample at it lands on the
        // rig's spine rest, which is identity.
        assert(SameOrientation(
            turned.Apply(HumanBone::Spine, About(kX, 90.0f)), kIdentity));
        correction = turned;
    }

    // ---- a binding moves: the map and the correction -----------------------
    reported.clear();
    rig.humanoid.GetAttribute(BoneAttribute(HumanBone::Head)).Block();
    assert(reported.count(kMapKey) && reported.count(kCorrectionKey) &&
           "unbinding a bone did not reach the correction");
    assert(!reported.count(kSourceKey) && !reported.count(kTargetKey));
    {
        ExecUsdCacheView view = system.Compute(request);
        const vrmRetarget::RestPoseCorrection unbound = CorrectionAt(view);
        assert(unbound.identity[Slot(HumanBone::Head)] &&
               "an unbound head kept its correction");
        correction = unbound;
    }

    // ---- the target's rest moves: all three downstream of it ---------------
    reported.clear();
    {
        UsdAttribute rest = rig.target.GetAttribute(kRestTransforms);
        VtArray<GfMatrix4d> matrices;
        assert(rest.Get(&matrices));
        matrices[1] = GfMatrix4d(1.0).SetTranslateOnly(GfVec3d(0, 1, 0));
        assert(rest.Set(matrices));
    }
    assert(reported.count(kTargetKey) && reported.count(kMapKey) &&
           reported.count(kCorrectionKey));
    assert(!reported.count(kSourceKey));
    {
        ExecUsdCacheView view = system.Compute(request);
        const vrmRetarget::RestPoseCorrection untwisted = CorrectionAt(view);
        // The hips are no longer turned, so they need no correction from an
        // identity clip hips.
        assert(untwisted.identity[Slot(HumanBone::Hips)]);
        correction = untwisted;
    }

    // ---- the source relationship moves: the correction follows it ----------
    // /Posed's rest, with no request rebuilt. Its answer is the library's over
    // the posed rest and the rig as it now stands.
    reported.clear();
    assert(rig.humanoid.GetRelationship(kSourceRel).SetTargets({kPosedPath}));
    assert(reported.count(kCorrectionKey) &&
           "retargeting vrm:retarget:sourceSkeleton did not reach the "
           "correction");
    assert(!reported.count(kMapKey));
    {
        ExecUsdCacheView view = system.Compute(request);
        const vrmRetarget::RestPoseCorrection posed = CorrectionAt(view);
        const vrmRetarget::TargetSkeleton target =
            ValueAt<vrmRetarget::TargetSkeleton>(view, kTargetKey,
                                                 "target skeleton");
        const vrmRetarget::HumanoidMap map =
            ValueAt<vrmRetarget::HumanoidMap>(view, kMapKey, "humanoid map");
        // As orientations, not bit for bit: the posed arm's rest is written
        // here as About(Z, -90) and decomposes off the matrix as its negation
        // (GfMatrix4d::ExtractRotationQuat picks the sign by branch).
        assert(SameCorrection(
            posed,
            vrmRetarget::ComputeRestPoseCorrection(PosedRest(), target, map)));
        // The posed arm rests at -90 Z and the rig's at +90 Z: a sample at the
        // clip's arm rest lands on the rig's.
        assert(SameOrientation(
            posed.Apply(HumanBone::LeftUpperArm, About(kZ, -90.0f)),
            About(kZ, 90.0f)));
    }
    std::printf("execVrm correction: the source's rest, a binding, the "
                "target's rest and the source relationship each reach it, "
                "with no request rebuilt\n");
}

// ---------------------------------------------------------------------------
// Refusals, one statement each
// ---------------------------------------------------------------------------
void TestTheSourceRelationshipIsCounted(const std::string& fixture)
{
    struct Case
    {
        const char* what;
        bool clear;
        SdfPathVector targets;
        const char* message;
    };
    const Case cases[] = {
        {"no source relationship", true, {},
         "'vrm:retarget:sourceSkeleton' reaches nothing on the stage"},
        // The reason absence is refused rather than defaulted: a path naming
        // no prim arrives exactly as no relationship does.
        {"a target naming nothing", false, {SdfPath("/Clip/Nowhere")},
         "'vrm:retarget:sourceSkeleton' reaches nothing on the stage"},
        {"two sources", false, {kClipPath, kPosedPath},
         "'vrm:retarget:sourceSkeleton' reaches 2 objects"},
        {"a source that is not a skeleton", false, {SdfPath("/Clip")},
         "'vrm:retarget:sourceSkeleton' targets </Clip>, which answered no"},
        // The case the second read of the relationship exists for: the
        // SkelRoot provides no vrm.computeTargetSkeleton and is dropped from
        // the fan-in while the network compiles, so the skeletons that come
        // back are one -- and only the count of paths says two were named.
        {"a skeleton and something that is not one", false,
         {kClipPath, SdfPath("/Clip")},
         "'vrm:retarget:sourceSkeleton' reaches 2 objects"},
        // The avatar's own skeleton, which is what naming the wrong rig looks
        // like: a skeleton, and not a semantic one -- VRoid's joint names,
        // none of them a bone. The target is never read by name; the source
        // is, and this is what a source that is not one gets.
        {"the target rig as its own source", false, {kTargetPath},
         "the source skeleton </Asset/skel/Skeleton> names no human bone"},
    };

    for (const Case& c : cases) {
        const Rig rig = Open(fixture);
        UsdRelationship source = rig.humanoid.GetRelationship(kSourceRel);
        assert(source);
        if (c.clear) {
            UsdPrim humanoid = rig.humanoid;
            assert(humanoid.RemoveProperty(kSourceRel));
        } else {
            assert(source.SetTargets(c.targets));
        }

        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kCorrectionKey);
        // The map does not read the source, and answers.
        ValueAt<vrmRetarget::HumanoidMap>(view, kMapKey, "humanoid map");
        if (!MarkNames(mark, c.message)) {
            std::fprintf(stderr, "%s: expected an error naming \"%s\"\n",
                         c.what, c.message);
            assert(false && "the source relationship was not refused as "
                            "expected");
        }
        mark.Clear();
    }
    std::printf("execVrm correction: no source, a path to nothing, two, one "
                "that is not a skeleton and one that is not semantic are each "
                "refused\n");
}

void TestASourceThatRefusedIsRefusedInTurn(const std::string& fixture)
{
    Warnings warnings;
    const Rig rig = Open(fixture);
    rig.clip.GetAttribute(kRestTransforms).Block();

    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);
    AssertRefused(view, kSourceKey);
    AssertRefused(view, kCorrectionKey);
    assert(MarkNames(mark, "vrm.computeTargetSkeleton: the skeleton states 6 "
                           "joints and 1 'restTransforms'"));
    // The source's refusal vanishes from the fan-in, and the second read of
    // the relationship is what turns that into a refusal rather than a
    // correction from nothing.
    assert(MarkNames(mark, "'vrm:retarget:sourceSkeleton' targets "
                           "</Clip/HumanoidSkeleton>, which answered no"));
    mark.Clear();
    std::printf("execVrm correction: a source skeleton that refused is "
                "refused in turn\n");
}

void TestASourceNamingOneBoneTwiceIsRefused(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    // A second hips under a reference joint, beside the first: two rests for
    // one bone, and the offline tool would keep the later without a word.
    {
        UsdAttribute joints = rig.clip.GetAttribute(kJoints);
        UsdAttribute rest = rig.clip.GetAttribute(kRestTransforms);
        VtArray<TfToken> tokens;
        VtArray<GfMatrix4d> matrices;
        assert(joints.Get(&tokens) && rest.Get(&matrices));
        tokens.push_back(TfToken("Reference"));
        tokens.push_back(TfToken("Reference/hips"));
        matrices.push_back(GfMatrix4d(1.0));
        matrices.push_back(GfMatrix4d(1.0));
        assert(joints.Set(tokens) && rest.Set(matrices));
    }

    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);
    ValueAt<vrmRetarget::TargetSkeleton>(view, kSourceKey, "source skeleton");
    AssertRefused(view, kCorrectionKey);
    assert(MarkNames(mark, "hips at 'hips', hips at 'Reference/hips'"));
    mark.Clear();
    std::printf("execVrm correction: a source naming one bone at two joints "
                "is refused, both named\n");
}

void TestAMapThatRefusedIsRefusedInTurn(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    assert(rig.humanoid.GetAttribute(BoneAttribute(HumanBone::Hips))
               .Set(TfToken("J_Bip_C_Hips")));

    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);
    AssertRefused(view, kMapKey);
    AssertRefused(view, kCorrectionKey);
    assert(MarkNames(mark, "vrm.computeHumanoidMap: the humanoid binds hips"));
    assert(MarkNames(mark, "vrm.computeRestPoseCorrection: the humanoid's "
                           "vrm.computeHumanoidMap"));
    mark.Clear();
    std::printf("execVrm correction: a map that refused is refused in turn\n");
}

// ---------------------------------------------------------------------------
// A target naming nothing is invisible beside one that names a skeleton
// ---------------------------------------------------------------------------
// The humanoid suite's pin, on the second relationship: a path with no prim
// behind it provides neither `computePath` nor `vrm.computeTargetSkeleton`, so
// beside a real source it is missing from both reads and the correction is
// answered against the real one, in either order. Alone it is refused (the
// count test above), which is why absence is refused at all.
void TestADanglingSecondSourceIsInvisible(const std::string& fixture)
{
    const vrmRetarget::RestPoseCorrection expected = [&fixture] {
        const Rig rig = Open(fixture);
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        return CorrectionAt(system.Compute(request));
    }();

    const SdfPath nowhere("/Clip/Nowhere");
    for (const SdfPathVector& targets :
         {SdfPathVector{kClipPath, nowhere}, SdfPathVector{nowhere, kClipPath}}) {
        const Rig rig = Open(fixture);
        assert(rig.humanoid.GetRelationship(kSourceRel).SetTargets(targets));

        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        const vrmRetarget::RestPoseCorrection correction =
            CorrectionAt(system.Compute(request));
        assert(mark.IsClean() &&
               "a dangling second source was noticed after all -- the "
               "limitation this pins is gone, and the node should now refuse");
        assert(correction == expected);
    }
    std::printf("execVrm correction: a source path naming nothing beside a "
                "real one is invisible, and the correction is answered\n");
}

// ---------------------------------------------------------------------------
// A one-joint source with no rest: the fallback, answered
// ---------------------------------------------------------------------------
// `vrm.computeTargetSkeleton`'s pinned case, one node on: a source skeleton of
// one joint whose `restTransforms` is unauthored reaches the correction as a
// hips at an identity rest -- indistinguishable from an authored one -- and is
// answered. The offline tool reads it identically.
void TestAOneJointSourceWithNoRestIsTheFallback(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    UsdPrim one = rig.stage->DefinePrim(SdfPath("/Clip/One"),
                                        TfToken("Skeleton"));
    assert(one.GetAttribute(kJoints).Set(VtArray<TfToken>({TfToken("hips")})));
    assert(rig.humanoid.GetRelationship(kSourceRel).SetTargets({one.GetPath()}));

    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);
    const vrmRetarget::RestPoseCorrection correction = CorrectionAt(view);
    assert(mark.IsClean());

    vrmRetarget::SourceRestPose identity;  // the library's own default
    assert(correction ==
           vrmRetarget::ComputeRestPoseCorrection(
               identity,
               ValueAt<vrmRetarget::TargetSkeleton>(view, kTargetKey,
                                                    "target skeleton"),
               ValueAt<vrmRetarget::HumanoidMap>(view, kMapKey,
                                                 "humanoid map")) &&
           "a one-joint source with no rest was not the identity rest");
    std::printf("execVrm correction: a one-joint source with no rest pose is "
                "the identity the fallback supplied\n");
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2 && "usage: execVrm_correction <corrected_rig.usda>");
    const std::string fixture = argv[1];

    // The map leaves 49 bones unbound; see the humanoid suite.
    Warnings all;

    TestTheCorrectionComputes(fixture);
    TestInvalidationFollowsBothRelationships(fixture);
    TestTheSourceRelationshipIsCounted(fixture);
    TestASourceThatRefusedIsRefusedInTurn(fixture);
    TestASourceNamingOneBoneTwiceIsRefused(fixture);
    TestAMapThatRefusedIsRefusedInTurn(fixture);
    TestADanglingSecondSourceIsInvisible(fixture);
    TestAOneJointSourceWithNoRestIsTheFallback(fixture);

    for (const std::string& warning : all.Take("")) {
        if (warning.find("No value set for output") == std::string::npos) {
            std::printf("  also warned: %s\n", warning.c_str());
        }
    }
    std::puts("execVrm correction: all checks passed");
    return 0;
}
