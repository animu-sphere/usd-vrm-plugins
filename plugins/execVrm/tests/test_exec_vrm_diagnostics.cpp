// SPDX-License-Identifier: Apache-2.0
//
// `vrm.computeRigDiagnostics` and `vrm.computeRetargetDiagnostics` through the
// built bundles, over retargeted_rig.usda -- the retarget suite's fixture,
// because what these nodes answer is what that retarget reported. Like every
// exec suite here it links neither plugin, and it needs execMotion in the
// session for the retarget's pose.
//
// What the seam suite cannot say, and so what this one measures:
//
//   * that both nodes are the library's reports over the values exec itself
//     computed -- `DiagnoseRig` over the rig and the map, then what
//     `PoseRetargeter::Retarget` reports for the bound pose -- and that a
//     diagnostic formats into the very line `motion_retarget` prints, which is
//     what P0-6's harness compares;
//   * that the rig's report moves with no frame and needs no clip, so it is
//     answered at the default time code and on a humanoid naming no source,
//     where the sample's is refused;
//   * that a statement or a map this layer cannot honour refuses both, and
//     that a duplicate binding -- a code the tool reports and bakes -- never
//     comes back through this bundle at all;
//   * invalidation from a binding, a key, a statement and the rig's order;
//   * a driver's pose diagnosed like the clip's, and a driver's retarget not
//     diagnosed at all.
//
// It links vrmRetarget for the result types and to build the expected values.

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
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
#include "pxr/exec/execUsd/valueOverride.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/valueTypeName.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include <motionCore/MotionPose.h>
#include <vrmRetarget/Diagnostics.h>
#include <vrmRetarget/HumanoidMap.h>
#include <vrmRetarget/PoseRetargeter.h>
#include <vrmRetarget/TargetSkeleton.h>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

using openstrata::motion::HumanJoint;
using vrmRetarget::RetargetDiagnosticCode;

const TfToken kTargetSkeleton("vrm.computeTargetSkeleton");
const TfToken kHumanoidMap("vrm.computeHumanoidMap");
const TfToken kBoundPose("vrm.computeBoundPose");
const TfToken kRetarget("vrm.humanoidRetarget");
const TfToken kRigDiagnostics("vrm.computeRigDiagnostics");
const TfToken kRetargetDiagnostics("vrm.computeRetargetDiagnostics");

const TfToken kSourceRel("vrm:retarget:sourceSkeleton");
const TfToken kRootMotion("vrm:retarget:rootMotion");
const TfToken kRotations("rotations");
const TfToken kJoints("joints");
const TfToken kRestTransforms("restTransforms");

const SdfPath kTargetPath("/Asset/skel/Skeleton");
const SdfPath kHumanoidPath("/Asset/rig/Humanoid");
const SdfPath kClipPath("/Clip/HumanoidSkeleton");
const SdfPath kClipAnimationPath("/Clip/Animation");

// The value keys every request here asks for, in this order.
constexpr int kTargetKey = 0;
constexpr int kMapKey = 1;
constexpr int kBoundKey = 2;
constexpr int kRetargetKey = 3;
constexpr int kRigKey = 4;
constexpr int kSampleKey = 5;

// The target rig's joints the order case swaps (retargeted_rig.usda).
constexpr std::size_t kNeckJoint = 4;
constexpr std::size_t kHeadJoint = 5;
const char* const kHeadToken =
    "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest/J_Bip_C_Neck/J_Bip_C_Head";
const char* const kChestToken = "Root/J_Bip_C_Hips/J_Bip_C_Spine/J_Bip_C_Chest";

TfToken
BoneAttribute(HumanJoint bone)
{
    return TfToken("vrm:humanBones:" + std::string(openstrata::motion::HumanJointName(bone)));
}

GfQuatf
About(const GfVec3f& axis, float degrees)
{
    const float half = degrees * 3.14159265358979324f / 360.0f;
    return GfQuatf(std::cos(half), axis * std::sin(half));
}

// Whether an error in `mark` says `what`. When none does, every error posted is
// printed, so a red run says what was reported instead.
bool
MarkNames(const TfErrorMark& mark, const std::string& what)
{
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it)
    {
        if (it->GetCommentary().find(what) != std::string::npos)
        {
            return true;
        }
    }
    std::fprintf(stderr, "no error said \"%s\"; posted:\n", what.c_str());
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it)
    {
        std::fprintf(stderr, "  %s\n", it->GetCommentary().c_str());
    }
    return false;
}

// The humanoid suite's warning collector: the map leaves 49 bones unbound and
// the executor warns once per bone per compute of it.
class Warnings : public TfDiagnosticMgr::Delegate
{
  public:
    Warnings()
    {
        TfDiagnosticMgr::GetInstance().AddDelegate(this);
    }
    ~Warnings() override
    {
        TfDiagnosticMgr::GetInstance().RemoveDelegate(this);
    }

    void
    IssueError(const TfError& error) override
    {
        std::fprintf(stderr, "error: %s\n", error.GetCommentary().c_str());
    }
    void
    IssueFatalError(const TfCallContext&, const std::string& message) override
    {
        std::fprintf(stderr, "fatal: %s\n", message.c_str());
    }
    void
    IssueStatus(const TfStatus& status) override
    {
        std::fprintf(stderr, "status: %s\n", status.GetCommentary().c_str());
    }
    void
    IssueWarning(const TfWarning& warning) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _seen.push_back(warning.GetCommentary());
    }

    std::vector<std::string>
    Take(const std::string& what)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::vector<std::string> matching;
        for (const std::string& seen : _seen)
        {
            if (seen.find(what) != std::string::npos)
            {
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
    UsdPrim clipAnimation;
};

// Opened directly and edited in memory, never saved -- each case opens its own.
Rig
Open(const std::string& fixture)
{
    Rig rig;
    rig.stage = UsdStage::Open(fixture);
    assert(rig.stage && "the retargeted rig fixture did not open");
    rig.target = rig.stage->GetPrimAtPath(kTargetPath);
    rig.humanoid = rig.stage->GetPrimAtPath(kHumanoidPath);
    rig.clip = rig.stage->GetPrimAtPath(kClipPath);
    rig.clipAnimation = rig.stage->GetPrimAtPath(kClipAnimationPath);
    assert(rig.target && rig.humanoid && rig.clip && rig.clipAnimation &&
           "the retargeted rig fixture is missing a prim");
    return rig;
}

std::vector<ExecUsdValueKey>
KeysFor(const Rig& rig)
{
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(rig.target, kTargetSkeleton);
    keys.emplace_back(rig.humanoid, kHumanoidMap);
    keys.emplace_back(rig.clip, kBoundPose);
    keys.emplace_back(rig.humanoid, kRetarget);
    keys.emplace_back(rig.humanoid, kRigDiagnostics);
    keys.emplace_back(rig.humanoid, kRetargetDiagnostics);
    return keys;
}

template <class T>
T
ValueAt(const ExecUsdCacheView& view, int index, const char* what)
{
    const VtValue value = view.Get(index);
    if (value.IsEmpty() || !value.IsHolding<T>())
    {
        std::fprintf(stderr, "no %s came back\n", what);
        assert(false && "a value this case needs did not come back");
    }
    return value.UncheckedGet<T>();
}

vrmRetarget::RetargetDiagnostics
RigAt(const ExecUsdCacheView& view)
{
    return ValueAt<vrmRetarget::RetargetDiagnostics>(view, kRigKey, "rig diagnostics");
}

vrmRetarget::RetargetDiagnostics
SampleAt(const ExecUsdCacheView& view)
{
    return ValueAt<vrmRetarget::RetargetDiagnostics>(view, kSampleKey, "retarget diagnostics");
}

void
AssertRefused(const ExecUsdCacheView& view, int index)
{
    assert(view.Get(index).IsEmpty() &&
           "a refusal came back carrying a value, which puts it back where a "
           "consumer cannot tell it from an answer");
}

// Arms a request -- its first compute, at the default time code, where the
// retarget, and so the sample's diagnostics, refuse by design -- and moves the
// system to `frame`.
void
ArmAt(ExecUsdSystem& system, ExecUsdRequest& request, double frame)
{
    {
        TfErrorMark mark;
        system.Compute(request);
        mark.Clear();
    }
    system.ChangeTime(UsdTimeCode(frame));
}

// The required bones the fixture's humanoid leaves unbound, in the
// vocabulary's order: what the rig says about every retarget onto it.
std::vector<std::string>
MissingRequired(const vrmRetarget::HumanoidMap& map)
{
    std::vector<std::string> missing;
    for (const HumanJoint bone : vrmRetarget::HumanoidMap::GetRequiredBones())
    {
        if (!map.IsMapped(bone))
        {
            missing.emplace_back(openstrata::motion::HumanJointName(bone));
        }
    }
    return missing;
}

// ---------------------------------------------------------------------------
// The library's reports, over what exec computed
// ---------------------------------------------------------------------------
void
TestTheNodesAreTheLibrarysReports(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    ExecUsdSystem system(rig.stage);

    std::set<int> timeReported;
    ExecUsdRequest request = system.BuildRequest(
        KeysFor(rig), [](const ExecRequestIndexSet&, const EfTimeInterval&) {},
        [&](const ExecRequestIndexSet& indices)
        { timeReported.insert(indices.begin(), indices.end()); });
    assert(request.IsValid());
    ArmAt(system, request, 24.0);

    // The rig's report moves with no frame: nothing it reads does. The
    // sample's does, across the pose it retargets.
    assert(!timeReported.count(kRigKey) && "a frame change reached the rig's diagnostics");
    assert(timeReported.count(kSampleKey) &&
           "a frame change did not reach the sample's diagnostics");

    for (const double frame : {24.0, 0.0})
    {
        system.ChangeTime(UsdTimeCode(frame));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        const auto target =
            ValueAt<vrmRetarget::TargetSkeleton>(view, kTargetKey, "target skeleton");
        const auto map = ValueAt<vrmRetarget::HumanoidMap>(view, kMapKey, "humanoid map");
        const auto pose = ValueAt<openstrata::motion::MotionPose>(view, kBoundKey, "bound pose");
        const vrmRetarget::RetargetDiagnostics rigReport = RigAt(view);
        const vrmRetarget::RetargetDiagnostics sampleReport = SampleAt(view);
        assert(mark.IsClean() && "the fixture's diagnostics posted an error");

        // The wrapper claim, over exec's own values. The clip's rest is left
        // at the library's default on purpose: no code depends on it, which
        // is why the correction is the one input this comparison can skip.
        assert(rigReport == vrmRetarget::DiagnoseRig(target, map) &&
               "the rig's diagnostics are not DiagnoseRig over exec's values");
        vrmRetarget::RetargetDiagnostics expected = rigReport;
        vrmRetarget::PoseRetargeter(target, map).Retarget(pose, &expected);
        assert(sampleReport == expected &&
               "the sample's diagnostics are not the rig's then the pose's");

        // And from the definition: the rig's required bones, then the one
        // bone the clip drives at every key and the humanoid does not bind.
        const std::vector<std::string> missing = MissingRequired(map);
        assert(missing.size() == 11);
        assert(rigReport.Subjects(RetargetDiagnosticCode::MissingRequiredBone) == missing);
        assert(rigReport.reported.size() == missing.size());
        assert(sampleReport.reported.size() == missing.size() + 1);
        const vrmRetarget::RetargetDiagnostic& unbound = sampleReport.reported.back();
        assert(unbound.code == RetargetDiagnosticCode::UnboundDrivenBone &&
               unbound.subject == "rightUpperArm");

        // What P0-6's harness compares, byte for byte: the offline tool prints
        // each diagnostic through this same formatter, so the line exec's
        // value formats into is the line the tool would print for it.
        assert(vrmRetarget::FormatRetargetDiagnostic(unbound) ==
               "[VRM_RETARGET_UNBOUND_DRIVEN_BONE] warning recoverable "
               "subject=rightUpperArm: the clip drives it and the target rig "
               "binds no joint for it");
    }
    std::printf("execVrm diagnostics: the rig's report is DiagnoseRig and the "
                "sample's is the rig's then the pose's, over exec's own values; "
                "only the second moves with the frame\n");
}

// ---------------------------------------------------------------------------
// A rig is diagnosed with no clip to retarget
// ---------------------------------------------------------------------------
void
TestTheRigIsDiagnosedWithNoClipToRetarget(const std::string& fixture)
{
    // At the default time code -- where every request is armed, and where the
    // retarget refuses rather than answer the rig's whole rest at 0 seconds.
    vrmRetarget::RetargetDiagnostics atAFrame;
    {
        const Rig rig = Open(fixture);
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        const vrmRetarget::RetargetDiagnostics armed = RigAt(view);
        AssertRefused(view, kSampleKey);
        assert(MarkNames(mark, "vrm.computeRetargetDiagnostics: the system is "
                               "at the default time code"));
        assert(MarkNames(mark, "no pose was retargeted, so nothing was "
                               "diagnosed (call ChangeTime first)"));
        mark.Clear();

        system.ChangeTime(UsdTimeCode(24.0));
        atAFrame = RigAt(system.Compute(request));
        assert(armed == atAFrame && !armed.IsClean());
    }
    // On a humanoid that names no clip at all.
    {
        const Rig rig = Open(fixture);
        UsdPrim humanoid = rig.humanoid;
        assert(humanoid.RemoveProperty(kSourceRel));
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        ArmAt(system, request, 24.0);
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        assert(RigAt(view) == atAFrame);
        AssertRefused(view, kSampleKey);
        assert(MarkNames(mark, "vrm.computeRetargetDiagnostics: "
                               "'vrm:retarget:sourceSkeleton' reaches nothing "
                               "on the stage, so there is no clip to "
                               "retarget; no pose was retargeted, so nothing "
                               "was diagnosed"));
        mark.Clear();
    }
    std::printf("execVrm diagnostics: the rig is diagnosed at the default time "
                "code and with no clip, where the sample's report is refused\n");
}

// ---------------------------------------------------------------------------
// What this layer cannot honour
// ---------------------------------------------------------------------------

// Whether any error in `mark` says `what`, without printing: for the claims
// that an error was NOT posted.
bool
Posted(const TfErrorMark& mark, const std::string& what)
{
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it)
    {
        if (it->GetCommentary().find(what) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

void
TestWhatTheRetargetCannotHonourRefusesBoth(const std::string& fixture)
{
    // Every case reads its errors off the ARMING compute, and that is a
    // finding rather than a convenience. A refusal is posted when its node
    // computes, and a node nothing invalidates computes once: the rig's report
    // reads nothing that moves with time, so it refuses at the compute that
    // arms the request -- at the default time code, the compute a driver
    // discards because the retarget refuses there by design -- and at every
    // frame after that only the absence of its value says so. The sample's
    // report, which moves with the frame, says it again at each one.

    // A statement: the retarget's words, under each node's own name.
    {
        const Rig rig = Open(fixture);
        assert(rig.humanoid.CreateAttribute(kRootMotion, SdfValueTypeNames->Token)
                   .Set(TfToken("sideways")));
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        {
            TfErrorMark mark;
            ExecUsdCacheView view = system.Compute(request);
            AssertRefused(view, kRigKey);
            assert(MarkNames(mark, "vrm.computeRigDiagnostics: "
                                   "'vrm:retarget:rootMotion' is 'sideways', "
                                   "which is none of hips, root and ignore; the "
                                   "rig was not diagnosed"));
            mark.Clear();
        }
        system.ChangeTime(UsdTimeCode(24.0));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kRigKey);
        AssertRefused(view, kSampleKey);
        assert(MarkNames(mark, "vrm.computeRetargetDiagnostics: "
                               "'vrm:retarget:rootMotion' is 'sideways'"));
        assert(!Posted(mark, "vrm.computeRigDiagnostics:") &&
               "the rig's report refused again at a frame; revisit the note "
               "about where its refusal is read");
        mark.Clear();
    }
    // Two bones on one joint. The offline tool reports that as
    // VRM_RETARGET_DUPLICATE_TARGET and bakes it, the later bone winning; the
    // humanoid map refuses it here (MapRefusal::DuplicateJoint), so the code
    // never comes back through this bundle -- the parity table's third row,
    // now as a code rather than a sentence.
    {
        const Rig rig = Open(fixture);
        assert(rig.humanoid
                   .CreateAttribute(BoneAttribute(HumanJoint::UpperChest), SdfValueTypeNames->Token,
                                    false, SdfVariabilityUniform)
                   .Set(TfToken(kChestToken)));
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        {
            TfErrorMark mark;
            ExecUsdCacheView view = system.Compute(request);
            AssertRefused(view, kMapKey);
            AssertRefused(view, kRigKey);
            assert(MarkNames(mark, "vrm.computeHumanoidMap: the humanoid binds "));
            assert(MarkNames(mark, "vrm.computeRigDiagnostics: the humanoid's "
                                   "vrm.computeHumanoidMap, or the skeleton it "
                                   "counts into, answered nothing; the rig was "
                                   "not diagnosed"));
            mark.Clear();
        }
        system.ChangeTime(UsdTimeCode(24.0));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kRigKey);
        AssertRefused(view, kSampleKey);
        assert(!Posted(mark, "vrm.computeHumanoidMap:") &&
               !Posted(mark, "vrm.computeRigDiagnostics:"));
        mark.Clear();
    }
    std::printf("execVrm diagnostics: a statement and a map this layer cannot "
                "honour refuse both reports, so a duplicate binding is never "
                "a code here -- and the rig's refusal is posted once, at the "
                "arming compute\n");
}

// ---------------------------------------------------------------------------
// Invalidation
// ---------------------------------------------------------------------------
void
TestInvalidationReachesTheReports(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    ExecUsdSystem system(rig.stage);

    std::set<int> reported;
    ExecUsdRequest request = system.BuildRequest(
        KeysFor(rig), [&](const ExecRequestIndexSet& indices, const EfTimeInterval&)
        { reported.insert(indices.begin(), indices.end()); });
    assert(request.IsValid());
    ArmAt(system, request, 24.0);
    const vrmRetarget::RetargetDiagnostics before = SampleAt(system.Compute(request));

    // ---- a key of the clip: the sample's report, never the rig's ------------
    reported.clear();
    {
        UsdAttribute rotations = rig.clipAnimation.GetAttribute(kRotations);
        VtArray<GfQuatf> values;
        assert(rotations.Get(&values, UsdTimeCode(24.0)));
        values[4] = About(GfVec3f(0, 1, 0), 45.0f); // the head
        assert(rotations.Set(values, UsdTimeCode(24.0)));
    }
    assert(reported.count(kSampleKey) && !reported.count(kRigKey) &&
           "a key of the clip did not reach exactly the sample's report");
    assert(SampleAt(system.Compute(request)) == before);

    // ---- a binding: both, and one bone becomes two codes ---------------------
    // The head unbound: the rig lacks a required bone, and the clip, which
    // drives the head at every key, now drives a bone the rig does not bind.
    reported.clear();
    assert(rig.humanoid.GetAttribute(BoneAttribute(HumanJoint::Head)).Set(TfToken()));
    assert(reported.count(kMapKey) && reported.count(kRigKey) && reported.count(kSampleKey));
    {
        ExecUsdCacheView view = system.Compute(request);
        const vrmRetarget::RetargetDiagnostics rigReport = RigAt(view);
        const vrmRetarget::RetargetDiagnostics sampleReport = SampleAt(view);
        assert(rigReport.Has(RetargetDiagnosticCode::MissingRequiredBone, "head"));
        assert(sampleReport.Has(RetargetDiagnosticCode::MissingRequiredBone, "head"));
        assert(sampleReport.Subjects(RetargetDiagnosticCode::UnboundDrivenBone) ==
               std::vector<std::string>({"head", "rightUpperArm"}));
    }

    // ---- a statement: both ----------------------------------------------------
    // Recomputed after, and not only for the value: exec reports a value that
    // is cached, so an edit landing on a report nobody recomputed since the
    // last one would be reported to nothing.
    const vrmRetarget::RetargetDiagnostics rigBefore = RigAt(system.Compute(request));
    reported.clear();
    assert(
        rig.humanoid.CreateAttribute(kRootMotion, SdfValueTypeNames->Token).Set(TfToken("ignore")));
    assert(reported.count(kRigKey) && reported.count(kSampleKey) && !reported.count(kMapKey));
    // The hips are bound, so which mode lands the root changes nothing a rig
    // can be told about.
    assert(RigAt(system.Compute(request)) == rigBefore);

    // ---- the rig's order: the rig, and so both -------------------------------
    // Head stated before its parent: a skeleton UsdSkel's order rule forbids,
    // carried faithfully by vrm.computeTargetSkeleton and named here.
    reported.clear();
    {
        UsdAttribute joints = rig.target.GetAttribute(kJoints);
        UsdAttribute rest = rig.target.GetAttribute(kRestTransforms);
        VtArray<TfToken> tokens;
        VtArray<GfMatrix4d> matrices;
        assert(joints.Get(&tokens) && rest.Get(&matrices));
        std::swap(tokens[kNeckJoint], tokens[kHeadJoint]);
        std::swap(matrices[kNeckJoint], matrices[kHeadJoint]);
        assert(joints.Set(tokens) && rest.Set(matrices));
    }
    assert(reported.count(kTargetKey) && reported.count(kRigKey) && reported.count(kSampleKey));
    {
        const vrmRetarget::RetargetDiagnostics rigReport = RigAt(system.Compute(request));
        assert(rigReport.Subjects(RetargetDiagnosticCode::InvalidHierarchy) ==
               std::vector<std::string>({kHeadToken}));
    }
    std::printf("execVrm diagnostics: a key reaches the sample's report only, "
                "and a binding, a statement and the rig's order reach both, "
                "with no request rebuilt\n");
}

// ---------------------------------------------------------------------------
// A driver's pose, and a driver's retarget
// ---------------------------------------------------------------------------
void
TestADriversPoseIsDiagnosedAndItsRetargetIsNot(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    ArmAt(system, request, 24.0);
    ExecUsdCacheView view = system.Compute(request);
    const vrmRetarget::RetargetDiagnostics unchanged = SampleAt(view);
    const openstrata::motion::MotionPose clipPose =
        ValueAt<openstrata::motion::MotionPose>(view, kBoundKey, "bound pose");

    // A pose a driver hands in -- a live source's, say -- that drives a bone
    // the clip never did: diagnosed like the clip's own. This is the case the
    // library's per-sample report exists for, since a clip's joints are
    // uniform and a live source's bones are not.
    {
        openstrata::motion::MotionPose live = clipPose;
        const auto slot = static_cast<std::size_t>(HumanJoint::LeftLowerArm);
        live.localRotations[slot] = About(GfVec3f(0, 1, 0), 20.0f);
        live.validRotations.set(slot);
        std::vector<ExecUsdValueOverride> overrides;
        overrides.push_back(
            ExecUsdValueOverride{ExecUsdValueKey(rig.clip, kBoundPose), VtValue(live)});
        TfErrorMark mark;
        const vrmRetarget::RetargetDiagnostics driven =
            SampleAt(system.ComputeWithOverrides(request, std::move(overrides)));
        assert(mark.IsClean());
        // One pose reports its bones in the vocabulary's order, where the left
        // forearm precedes the right upper arm.
        assert(driven.Subjects(RetargetDiagnosticCode::UnboundDrivenBone) ==
               std::vector<std::string>({"leftLowerArm", "rightUpperArm"}));
    }

    // A retarget a driver hands in is not diagnosed: the node retargets the
    // stage's sample again rather than reading the retarget's answer, so an
    // override of the retarget reaches the joint transforms and not this. The
    // cost of the eleventh boundary finding, stated rather than hidden.
    {
        vrmRetarget::RetargetedPose held =
            ValueAt<vrmRetarget::RetargetedPose>(view, kRetargetKey, "retargeted pose");
        held.timestamp = 0.5;
        std::vector<ExecUsdValueOverride> overrides;
        overrides.push_back(
            ExecUsdValueOverride{ExecUsdValueKey(rig.humanoid, kRetarget), VtValue(held)});
        TfErrorMark mark;
        ExecUsdCacheView overridden = system.ComputeWithOverrides(request, std::move(overrides));
        assert(mark.IsClean());
        assert(ValueAt<vrmRetarget::RetargetedPose>(overridden, kRetargetKey, "retargeted pose")
                   .timestamp == 0.5);
        assert(SampleAt(overridden) == unchanged &&
               "a driver's retarget reached the diagnostics of the stage's");
    }

    // And neither override survives its call.
    assert(SampleAt(system.Compute(request)) == unchanged);
    std::printf("execVrm diagnostics: a driver's pose is diagnosed like the "
                "clip's, and a driver's retarget is not diagnosed at all\n");
}

} // namespace

int
main(int argc, char** argv)
{
    assert(argc == 2 && "usage: execVrm_diagnostics <retargeted_rig.usda>");
    const std::string fixture = argv[1];

    // The map leaves 49 bones unbound; see the humanoid suite.
    Warnings all;

    TestTheNodesAreTheLibrarysReports(fixture);
    TestTheRigIsDiagnosedWithNoClipToRetarget(fixture);
    TestWhatTheRetargetCannotHonourRefusesBoth(fixture);
    TestInvalidationReachesTheReports(fixture);
    TestADriversPoseIsDiagnosedAndItsRetargetIsNot(fixture);

    for (const std::string& warning : all.Take(""))
    {
        if (warning.find("No value set for output") == std::string::npos)
        {
            std::printf("  also warned: %s\n", warning.c_str());
        }
    }
    std::puts("execVrm diagnostics: all checks passed");
    return 0;
}
