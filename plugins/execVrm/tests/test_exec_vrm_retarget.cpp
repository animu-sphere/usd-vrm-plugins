// SPDX-License-Identifier: Apache-2.0
//
// `vrm.humanoidRetarget` and `vrm.computeBoundPose` through the built bundles,
// over retargeted_rig.usda, reached only through OpenExec's own lazy loading
// of the staged plugInfo.json files. Like every exec suite here it links
// neither plugin.
//
// What the correction suite could not reach, and so what this one measures:
//
//   * a value one bundle computes (`motion.sampleAnimation`, execMotion's)
//     reaching a computation of another (`vrm.computeBoundPose`), across
//     UsdSkel's own `skel:animationSource`, and from there a third computation
//     two relationships away from the clip;
//   * that the retarget is `vrmRetarget::PoseRetargeter` over the rig, the map,
//     the clip's rest and the root-motion options, and nothing more -- and that
//     the correction it computes for itself is, bit for bit, the one
//     `vrm.computeRestPoseCorrection` has already cached beside it;
//   * the four root-motion statements, against `ResolveRootTranslation`'s
//     definition;
//   * invalidation from the frame, the clip, the binding, the source and the
//     statements, and a driver's pose crossing both bundles as an override;
//   * every refusal, and a session with no execMotion at all
//     (`--without-exec-motion`, its own CTest entry).
//
// It links vrmRetarget for the result types and to build the expected values;
// the claim about the library is exactly that the node is one call of it.

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
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

#include <motionCore/Humanoid.h>
#include <vrmRetarget/HumanoidMap.h>
#include <vrmRetarget/PoseRetargeter.h>
#include <vrmRetarget/RestPose.h>
#include <vrmRetarget/RootMotionPolicy.h>
#include <vrmRetarget/TargetSkeleton.h>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
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
const TfToken kBoundPose("vrm.computeBoundPose");
const TfToken kRetarget("vrm.humanoidRetarget");
const TfToken kSampleAnimation("motion.sampleAnimation");

const TfToken kSourceRel("vrm:retarget:sourceSkeleton");
const TfToken kAnimationSourceRel("skel:animationSource");
const TfToken kRootMotion("vrm:retarget:rootMotion");
const TfToken kRootJoint("vrm:retarget:rootJoint");
const TfToken kTranslationScale("vrm:retarget:translationScale");
const TfToken kPreserveTargetHeight("vrm:retarget:preserveTargetHeight");
const TfToken kRotations("rotations");
const TfToken kRate("motion:timeCodesPerSecond");

const SdfPath kTargetPath("/Asset/skel/Skeleton");
const SdfPath kHumanoidPath("/Asset/rig/Humanoid");
const SdfPath kClipPath("/Clip/HumanoidSkeleton");
const SdfPath kClipAnimationPath("/Clip/Animation");
const SdfPath kPosedPath("/Posed/HumanoidSkeleton");
const SdfPath kPosedAnimationPath("/Posed/Animation");

// The value keys every request here asks for, in this order.
constexpr int kTargetKey = 0;
constexpr int kMapKey = 1;
constexpr int kCorrectionKey = 2;
constexpr int kSampledKey = 3;
constexpr int kBoundKey = 4;
constexpr int kRetargetKey = 5;

// The target rig's joint order (retargeted_rig.usda).
constexpr std::size_t kRootJointSlot = 0;
constexpr std::size_t kHipsJoint = 1;
constexpr std::size_t kSpineJoint = 2;
constexpr std::size_t kChestJoint = 3;
constexpr std::size_t kNeckJoint = 4;
constexpr std::size_t kHeadJoint = 5;
constexpr std::size_t kArmJoint = 6;
constexpr std::size_t kJointCount = 7;

const char* const kRootJointToken = "Root";

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

bool Near(const GfVec3f& a, const GfVec3f& b, float tolerance = 1e-6f)
{
    return std::abs(a[0] - b[0]) <= tolerance
        && std::abs(a[1] - b[1]) <= tolerance
        && std::abs(a[2] - b[2]) <= tolerance;
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
    UsdPrim clipAnimation;
    UsdPrim posed;
    UsdPrim posedAnimation;
};

// Opened directly and edited in memory, never saved -- each case opens its own.
Rig Open(const std::string& fixture)
{
    Rig rig;
    rig.stage = UsdStage::Open(fixture);
    assert(rig.stage && "the retargeted rig fixture did not open");
    rig.target = rig.stage->GetPrimAtPath(kTargetPath);
    rig.humanoid = rig.stage->GetPrimAtPath(kHumanoidPath);
    rig.clip = rig.stage->GetPrimAtPath(kClipPath);
    rig.clipAnimation = rig.stage->GetPrimAtPath(kClipAnimationPath);
    rig.posed = rig.stage->GetPrimAtPath(kPosedPath);
    rig.posedAnimation = rig.stage->GetPrimAtPath(kPosedAnimationPath);
    assert(rig.target && rig.humanoid && rig.clip && rig.clipAnimation &&
           rig.posed && rig.posedAnimation &&
           "the retargeted rig fixture is missing a prim");
    return rig;
}

std::vector<ExecUsdValueKey> KeysFor(const Rig& rig)
{
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(rig.target, kTargetSkeleton);
    keys.emplace_back(rig.humanoid, kHumanoidMap);
    keys.emplace_back(rig.humanoid, kCorrection);
    keys.emplace_back(rig.clipAnimation, kSampleAnimation);
    keys.emplace_back(rig.clip, kBoundPose);
    keys.emplace_back(rig.humanoid, kRetarget);
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

vrmRetarget::RetargetedPose RetargetAt(const ExecUsdCacheView& view,
                                       int index = kRetargetKey)
{
    return ValueAt<vrmRetarget::RetargetedPose>(view, index, "retargeted pose");
}

motion::HumanoidPose PoseAt(const ExecUsdCacheView& view, int index)
{
    return ValueAt<motion::HumanoidPose>(view, index, "pose");
}

void AssertRefused(const ExecUsdCacheView& view, int index)
{
    assert(view.Get(index).IsEmpty() &&
           "a refusal came back carrying a value, which puts it back where a "
           "consumer cannot tell it from an answer");
}

// Arms a request -- its first compute, at the default time code, where the
// retarget refuses by design -- and moves the system to `frame`. The refusal
// posted while arming is the one TestTheRetargetComputes asserts; here it is
// cleared, so the case reads only what its own frame posts.
void ArmAt(ExecUsdSystem& system, ExecUsdRequest& request, double frame)
{
    {
        TfErrorMark mark;
        system.Compute(request);
        mark.Clear();
    }
    system.ChangeTime(UsdTimeCode(frame));
}

// ---------------------------------------------------------------------------
// The expected values, from the fixture's text
// ---------------------------------------------------------------------------

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
    rest.localTranslations[Slot(HumanBone::RightUpperArm)] =
        GfVec3f(-0.1f, 0.15f, 0);
    rest.SetParent(HumanBone::Spine, HumanBone::Hips);
    rest.SetParent(HumanBone::Chest, HumanBone::Spine);
    rest.SetParent(HumanBone::Neck, HumanBone::Chest);
    rest.SetParent(HumanBone::Head, HumanBone::Neck);
    rest.SetParent(HumanBone::LeftUpperArm, HumanBone::Chest);
    rest.SetParent(HumanBone::RightUpperArm, HumanBone::Chest);
    return rest;
}

// The target rig's rest, joint by joint.
GfQuatf TargetRestRotation(std::size_t joint)
{
    switch (joint) {
    case kHipsJoint: return About(kY, 90.0f);
    case kArmJoint: return About(kZ, 90.0f);
    default: return kIdentity;
    }
}

GfVec3f TargetRestTranslation(std::size_t joint)
{
    switch (joint) {
    case kHipsJoint: return GfVec3f(0, 1, 0);
    case kSpineJoint: return GfVec3f(0, 0.1f, 0);
    case kChestJoint: return GfVec3f(0, 0.15f, 0);
    case kNeckJoint: return GfVec3f(0, 0.2f, 0);
    case kHeadJoint: return GfVec3f(0, 0.1f, 0);
    case kArmJoint: return GfVec3f(0.1f, 0.15f, 0);
    default: return GfVec3f(0.0f);
    }
}

// Each mapped bone and the joint it drives.
struct Binding
{
    HumanBone bone;
    std::size_t joint;
};
const Binding kBindings[] = {
    {HumanBone::Hips, kHipsJoint},   {HumanBone::Spine, kSpineJoint},
    {HumanBone::Chest, kChestJoint}, {HumanBone::Neck, kNeckJoint},
    {HumanBone::Head, kHeadJoint},   {HumanBone::LeftUpperArm, kArmJoint}};

// The clip at frame 24, as its text states it.
const GfQuatf kSpineAt24 = About(kX, 30.0f);
const GfQuatf kArmAt24 = About(kZ, 90.0f);
const GfVec3f kHipsAt24(0.5f, 1.1f, 0.2f);
const GfVec3f kHipsRest(0, 1, 0);

// ---------------------------------------------------------------------------
// The retarget the fixture states
// ---------------------------------------------------------------------------
void TestTheRetargetComputes(const std::string& fixture)
{
    Warnings warnings;
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

    // ---- the default time code, which every request is armed at -----------
    // The sampler answers the keyed clip with an EMPTY pose there, stamped
    // 0.0 -- harmless while it stays a pose, and forwarded unchanged. The
    // retarget refuses: retargeted, that pose is the rig's whole rest at 0
    // seconds, an answer for an instant nobody named.
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        const motion::HumanoidPose empty = PoseAt(view, kSampledKey);
        assert(empty.validRotations.none() && !empty.root.hasPosition &&
               empty.timestamp == 0.0 &&
               "the sampler no longer answers an empty pose at the default "
               "time code; revisit the retarget's refusal");
        assert(PoseAt(view, kBoundKey) == empty);
        AssertRefused(view, kRetargetKey);
        assert(MarkNames(mark, "vrm.humanoidRetarget: the system is at the "
                               "default time code"));
        mark.Clear();
    }

    system.ChangeTime(UsdTimeCode(24.0));
    assert(timeReported.count(kSampledKey) && timeReported.count(kBoundKey) &&
           timeReported.count(kRetargetKey) &&
           "a frame change did not reach the retarget across two links");
    assert(!timeReported.count(kTargetKey) && !timeReported.count(kMapKey) &&
           !timeReported.count(kCorrectionKey) &&
           "a frame change reported a rig statement, which reads no time");

    vrmRetarget::TargetSkeleton target;
    vrmRetarget::HumanoidMap map;
    vrmRetarget::RestPoseCorrection correction;
    motion::HumanoidPose sampled;
    vrmRetarget::RetargetedPose retargeted;
    {
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        target = ValueAt<vrmRetarget::TargetSkeleton>(view, kTargetKey,
                                                      "target skeleton");
        map = ValueAt<vrmRetarget::HumanoidMap>(view, kMapKey, "humanoid map");
        correction = ValueAt<vrmRetarget::RestPoseCorrection>(
            view, kCorrectionKey, "rest-pose correction");
        sampled = PoseAt(view, kSampledKey);
        // The pose crossed from one bundle to the other unchanged.
        assert(PoseAt(view, kBoundKey) == sampled &&
               "the bound pose is not the sampler's pose");
        retargeted = RetargetAt(view);
        assert(mark.IsClean() && "the fixture's retarget posted an error");
    }

    // ---- the wrapper claim --------------------------------------------------
    // The node IS PoseRetargeter over the rig, the map, the clip's rest and the
    // library's default options, asked for the sampler's pose: bit for bit.
    assert(retargeted ==
               vrmRetarget::PoseRetargeter(target, map, ClipRest())
                   .Retarget(sampled) &&
           "the node's pose is not the library's over the same values");

    // ---- and the cost: the correction it recomputes is the cached one -------
    // Every mapped joint's rotation is the cached correction applied to the
    // clip's, exactly -- so the value vrm.computeRestPoseCorrection holds is
    // the one this node computed again, for this frame.
    for (const Binding& b : kBindings) {
        assert(retargeted.rotations[b.joint] ==
                   correction.Apply(b.bone, sampled.localRotations[Slot(b.bone)]) &&
               "the retarget's correction is not the cached one");
    }

    // ---- what it means, from the definition --------------------------------
    assert(retargeted.timestamp == 1.0 &&
           "frame 24 at 24 per second is not one second");
    assert(retargeted.rotations.size() == kJointCount &&
           retargeted.translations.size() == kJointCount);

    // The spine keeps its world-space delta under the turned hips.
    {
        const GfQuatf parent = About(kY, 90.0f);
        const GfQuatf delta =
            (parent * retargeted.rotations[kSpineJoint]) * parent.GetInverse();
        assert(SameOrientation(delta, kSpineAt24) &&
               "the spine's world delta did not survive the retarget");
    }
    // So does the arm, whose own rest is turned too.
    {
        const GfQuatf parent = About(kY, 90.0f);
        const GfQuatf rest = TargetRestRotation(kArmJoint);
        const GfQuatf delta =
            (parent * retargeted.rotations[kArmJoint] * rest.GetInverse())
            * parent.GetInverse();
        assert(SameOrientation(delta, kArmAt24) &&
               "the arm's world delta did not survive the retarget");
    }
    // A bone at its clip rest lands on the rig's rest.
    for (const std::size_t joint : {kHipsJoint, kChestJoint, kNeckJoint,
                                    kHeadJoint}) {
        assert(SameOrientation(retargeted.rotations[joint],
                               TargetRestRotation(joint)));
    }
    // The joint no bone binds stays at rest, and the right upper arm the clip
    // drives -- which the humanoid does not bind -- moved nothing.
    assert(retargeted.rotations[kRootJointSlot] == kIdentity);
    assert(retargeted.translations[kRootJointSlot] == GfVec3f(0.0f));
    assert(sampled.validRotations.test(Slot(HumanBone::RightUpperArm)) &&
           !map.IsMapped(HumanBone::RightUpperArm) &&
           "the fixture no longer drives a bone the rig does not bind");

    // Root motion onto the hips, by the library's default: the clip's delta
    // from its rest, added to the rig's rest.
    assert(Near(retargeted.translations[kHipsJoint],
                TargetRestTranslation(kHipsJoint) + (kHipsAt24 - kHipsRest)) &&
           "the hips did not carry the clip's root motion");
    for (const std::size_t joint : {kSpineJoint, kChestJoint, kNeckJoint,
                                    kHeadJoint, kArmJoint}) {
        assert(retargeted.translations[joint] == TargetRestTranslation(joint));
    }

    // ---- recompute with nothing changed ------------------------------------
    timeReported.clear();
    {
        ExecUsdCacheView view = system.Compute(request);
        assert(RetargetAt(view) == retargeted);
    }

    // ---- between two keys: still the library's, over USD's sample ----------
    system.ChangeTime(UsdTimeCode(12.0));
    {
        ExecUsdCacheView view = system.Compute(request);
        const motion::HumanoidPose between = PoseAt(view, kSampledKey);
        assert(between.timestamp == 0.5);
        assert(RetargetAt(view) ==
               vrmRetarget::PoseRetargeter(target, map, ClipRest())
                   .Retarget(between));
    }
    std::printf("execVrm retarget: the node is PoseRetargeter over the sampler's "
                "pose, its correction is the cached one, and a frame change "
                "reaches it across two links\n");
}

// ---------------------------------------------------------------------------
// The four root-motion statements
// ---------------------------------------------------------------------------
void TestTheRootMotionStatementsAreTheToolsFlags(const std::string& fixture)
{
    struct Case
    {
        const char* what;
        const char* mode;
        const char* rootJoint;
        float scale;
        bool preserve;
        GfVec3f hips;
        GfVec3f root;
    };
    const GfVec3f delta = kHipsAt24 - kHipsRest;
    const Case cases[] = {
        {"ignore", "ignore", nullptr, 1.0f, false, kHipsRest, GfVec3f(0.0f)},
        {"hips, stated", "hips", nullptr, 1.0f, false, kHipsRest + delta,
         GfVec3f(0.0f)},
        {"root onto Root", "root", kRootJointToken, 1.0f, false, kHipsRest,
         delta},
        {"a scale of 2", nullptr, nullptr, 2.0f, false, kHipsRest + delta * 2.0f,
         GfVec3f(0.0f)},
        {"the target's height", nullptr, nullptr, 1.0f, true,
         kHipsRest + GfVec3f(delta[0], 0.0f, delta[2]), GfVec3f(0.0f)},
        // A root joint stated beside a mode that does not read it is not read,
        // as `--root-joint` is not.
        {"a root joint under hips", "hips", "NoSuchJoint", 1.0f, false,
         kHipsRest + delta, GfVec3f(0.0f)},
    };

    for (const Case& c : cases) {
        const Rig rig = Open(fixture);
        UsdPrim humanoid = rig.humanoid;
        if (c.mode) {
            assert(humanoid
                       .CreateAttribute(kRootMotion, SdfValueTypeNames->Token)
                       .Set(TfToken(c.mode)));
        }
        if (c.rootJoint) {
            assert(humanoid
                       .CreateAttribute(kRootJoint, SdfValueTypeNames->Token)
                       .Set(TfToken(c.rootJoint)));
        }
        if (c.scale != 1.0f) {
            assert(humanoid
                       .CreateAttribute(kTranslationScale,
                                        SdfValueTypeNames->Float)
                       .Set(c.scale));
        }
        if (c.preserve) {
            assert(humanoid
                       .CreateAttribute(kPreserveTargetHeight,
                                        SdfValueTypeNames->Bool)
                       .Set(true));
        }

        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        ArmAt(system, request, 24.0);
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        const vrmRetarget::RetargetedPose pose = RetargetAt(view);
        assert(mark.IsClean());
        if (!Near(pose.translations[kHipsJoint], c.hips) ||
            !Near(pose.translations[kRootJointSlot], c.root)) {
            std::fprintf(stderr,
                         "%s: hips (%g %g %g), root (%g %g %g)\n", c.what,
                         pose.translations[kHipsJoint][0],
                         pose.translations[kHipsJoint][1],
                         pose.translations[kHipsJoint][2],
                         pose.translations[kRootJointSlot][0],
                         pose.translations[kRootJointSlot][1],
                         pose.translations[kRootJointSlot][2]);
            assert(false && "a root-motion statement landed somewhere else");
        }
    }
    std::printf("execVrm retarget: ignore, hips, root, a scale and the target's "
                "height each land the root where ResolveRootTranslation says\n");
}

// ---------------------------------------------------------------------------
// Invalidation
// ---------------------------------------------------------------------------
void TestInvalidationReachesTheRetarget(const std::string& fixture)
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
    vrmRetarget::RetargetedPose pose = RetargetAt(system.Compute(request));

    // ---- a root-motion statement: the retarget and nothing else ------------
    reported.clear();
    {
        UsdPrim humanoid = rig.humanoid;
        assert(humanoid.CreateAttribute(kRootMotion, SdfValueTypeNames->Token)
                   .Set(TfToken("ignore")));
    }
    assert(reported == std::set<int>({kRetargetKey}) &&
           "a root-motion statement reported more than the retarget");
    {
        const vrmRetarget::RetargetedPose ignored =
            RetargetAt(system.Compute(request));
        assert(ignored.translations[kHipsJoint] == kHipsRest);
        assert(ignored.rotations == pose.rotations);
        pose = ignored;
    }

    // ---- a key of the clip: sampler, bound pose, retarget -------------------
    reported.clear();
    {
        UsdAttribute rotations = rig.clipAnimation.GetAttribute(kRotations);
        VtArray<GfQuatf> values;
        assert(rotations.Get(&values, UsdTimeCode(24.0)));
        values[4] = About(kY, 45.0f);  // the head
        assert(rotations.Set(values, UsdTimeCode(24.0)));
    }
    assert(reported.count(kSampledKey) && reported.count(kBoundKey) &&
           reported.count(kRetargetKey) &&
           "a key of the clip did not reach the retarget");
    assert(!reported.count(kCorrectionKey) && !reported.count(kMapKey) &&
           "a key of the clip reported a rig statement");
    {
        const vrmRetarget::RetargetedPose turned =
            RetargetAt(system.Compute(request));
        const GfQuatf parent = About(kY, 90.0f);
        assert(SameOrientation(
            (parent * turned.rotations[kHeadJoint]) * parent.GetInverse(),
            About(kY, 45.0f)));
        pose = turned;
    }

    // ---- the binding: the clip skeleton bound to the posed animation --------
    reported.clear();
    assert(rig.clip.GetRelationship(kAnimationSourceRel)
               .SetTargets({kPosedAnimationPath}));
    assert(reported.count(kBoundKey) && reported.count(kRetargetKey) &&
           "rebinding the clip skeleton did not reach the retarget");
    assert(!reported.count(kSampledKey) && !reported.count(kCorrectionKey));
    {
        ExecUsdCacheView view = system.Compute(request);
        // The posed animation names its bones under `Reference`, and the
        // sampler reads them by leaf.
        const motion::HumanoidPose bound = PoseAt(view, kBoundKey);
        assert(SameOrientation(
            bound.localRotations[Slot(HumanBone::LeftUpperArm)],
            About(kZ, -90.0f)));
        pose = RetargetAt(view);
    }

    // ---- the source: rest and motion both follow it -------------------------
    // /Posed's skeleton, bound to the animation that holds it at its rest. A
    // retarget from a clip AT its rest lands every mapped joint on the rig's
    // rest -- the arm's -90 Z onto its +90 Z -- which needs the posed rest and
    // the posed motion to arrive together, through the one relationship.
    reported.clear();
    assert(rig.humanoid.GetRelationship(kSourceRel).SetTargets({kPosedPath}));
    assert(reported.count(kCorrectionKey) && reported.count(kRetargetKey) &&
           "retargeting vrm:retarget:sourceSkeleton did not reach the "
           "retarget");
    assert(!reported.count(kMapKey));
    {
        const vrmRetarget::RetargetedPose posed =
            RetargetAt(system.Compute(request));
        for (const Binding& b : kBindings) {
            assert(SameOrientation(posed.rotations[b.joint],
                                   TargetRestRotation(b.joint)) &&
                   "a clip at its rest did not land on the rig's rest");
        }
        // Ignore, stated above: the hips keep the rig's rest translation.
        assert(posed.translations[kHipsJoint] == kHipsRest);
    }
    std::printf("execVrm retarget: a statement, a key, the binding and the "
                "source each reach it, with no request rebuilt\n");
}

// ---------------------------------------------------------------------------
// A driver's pose, crossing both bundles
// ---------------------------------------------------------------------------
void TestADriversPoseReachesTheRetarget(const std::string& fixture)
{
    const Rig rig = Open(fixture);
    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    ArmAt(system, request, 24.0);
    ExecUsdCacheView plain = system.Compute(request);
    const vrmRetarget::TargetSkeleton target =
        ValueAt<vrmRetarget::TargetSkeleton>(plain, kTargetKey,
                                             "target skeleton");
    const vrmRetarget::HumanoidMap map =
        ValueAt<vrmRetarget::HumanoidMap>(plain, kMapKey, "humanoid map");
    const vrmRetarget::RetargetedPose unchanged = RetargetAt(plain);

    // A pose a live source would hand in: the head turned, the hips moved.
    motion::HumanoidPose held;
    held.timestamp = 1.0;
    held.localRotations[Slot(HumanBone::Head)] = About(kY, -60.0f);
    held.validRotations.set(Slot(HumanBone::Head));
    held.root.worldPosition = GfVec3f(-0.3f, 0.95f, 0.0f);
    held.root.hasPosition = true;

    for (const ExecUsdValueKey& key :
         {ExecUsdValueKey(rig.clipAnimation, kSampleAnimation),
          ExecUsdValueKey(rig.clip, kBoundPose)}) {
        std::vector<ExecUsdValueOverride> overrides;
        overrides.push_back(ExecUsdValueOverride{key, VtValue(held)});
        TfErrorMark mark;
        ExecUsdCacheView view =
            system.ComputeWithOverrides(request, std::move(overrides));
        assert(mark.IsClean());
        assert(PoseAt(view, kBoundKey) == held);
        assert(RetargetAt(view) ==
                   vrmRetarget::PoseRetargeter(target, map, ClipRest())
                       .Retarget(held) &&
               "an overridden pose did not reach the retarget");
    }

    // And it does not survive the call.
    assert(RetargetAt(system.Compute(request)) == unchanged);
    std::printf("execVrm retarget: a pose handed to execMotion's sampler or to "
                "the bound pose reaches the retarget\n");
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------
void TestTheRootMotionStatementsAreRefusedWhenUnhonourable(
    const std::string& fixture)
{
    struct Case
    {
        const char* what;
        const char* mode;
        const char* rootJoint;
        float scale;
        const char* message;
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Case cases[] = {
        {"a mode that is none of the three", "sideways", nullptr, 1.0f,
         "'vrm:retarget:rootMotion' is 'sideways'"},
        {"root with no joint", "root", nullptr, 1.0f,
         "'vrm:retarget:rootJoint' names no joint to receive it"},
        // A joint by its leaf, not its path: the tool resolves exactly, and so
        // does this.
        {"root onto a leaf", "root", "J_Bip_C_Hips", 1.0f,
         "'vrm:retarget:rootJoint' is 'J_Bip_C_Hips', which is not a joint"},
        {"a scale that is not a number", nullptr, nullptr, nan,
         "'vrm:retarget:translationScale' is not a finite number"},
    };

    for (const Case& c : cases) {
        const Rig rig = Open(fixture);
        UsdPrim humanoid = rig.humanoid;
        if (c.mode) {
            assert(humanoid
                       .CreateAttribute(kRootMotion, SdfValueTypeNames->Token)
                       .Set(TfToken(c.mode)));
        }
        if (c.rootJoint) {
            assert(humanoid
                       .CreateAttribute(kRootJoint, SdfValueTypeNames->Token)
                       .Set(TfToken(c.rootJoint)));
        }
        if (c.scale != 1.0f) {
            assert(humanoid
                       .CreateAttribute(kTranslationScale,
                                        SdfValueTypeNames->Float)
                       .Set(c.scale));
        }

        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kRetargetKey);
        // The correction reads none of it, and answers.
        ValueAt<vrmRetarget::RestPoseCorrection>(view, kCorrectionKey,
                                                 "rest-pose correction");
        if (!MarkNames(mark, c.message)) {
            std::fprintf(stderr, "%s: expected an error naming \"%s\"\n",
                         c.what, c.message);
            assert(false && "a root-motion statement was not refused");
        }
        mark.Clear();
    }
    std::printf("execVrm retarget: an unknown mode, root with no joint or an "
                "unknown one, and a scale that is no number are refused\n");
}

void TestAClipTheSkeletonCannotReachIsRefused(const std::string& fixture)
{
    struct Case
    {
        const char* what;
        bool clear;
        SdfPathVector targets;
        const char* message;
    };
    const Case cases[] = {
        {"no binding", true, {},
         "vrm.computeBoundPose: 'skel:animationSource' reaches nothing"},
        {"a binding to nothing", false, {SdfPath("/Clip/Nowhere")},
         "vrm.computeBoundPose: 'skel:animationSource' reaches nothing"},
        {"two animations", false, {kClipAnimationPath, kPosedAnimationPath},
         "'skel:animationSource' reaches 2 objects"},
        // Not an animation: the SkelRoot provides no motion.sampleAnimation
        // and is dropped from the fan-in while the network compiles.
        {"a binding to something that is not an animation", false,
         {SdfPath("/Clip")},
         "'skel:animationSource' targets </Clip>, which answered no "
         "motion.sampleAnimation"},
    };

    for (const Case& c : cases) {
        const Rig rig = Open(fixture);
        UsdRelationship binding = rig.clip.GetRelationship(kAnimationSourceRel);
        assert(binding);
        if (c.clear) {
            UsdPrim clip = rig.clip;
            assert(clip.RemoveProperty(kAnimationSourceRel));
        } else {
            assert(binding.SetTargets(c.targets));
        }

        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kBoundKey);
        AssertRefused(view, kRetargetKey);
        ValueAt<vrmRetarget::RestPoseCorrection>(view, kCorrectionKey,
                                                 "rest-pose correction");
        if (!MarkNames(mark, c.message) ||
            !MarkNames(mark, "vrm.humanoidRetarget: the source skeleton "
                             "</Clip/HumanoidSkeleton> answered no "
                             "vrm.computeBoundPose")) {
            std::fprintf(stderr, "%s: not refused as expected\n", c.what);
            assert(false && "a clip the skeleton cannot reach was answered");
        }
        mark.Clear();
    }
    std::printf("execVrm retarget: no binding, a binding to nothing, two, and "
                "one that is not an animation are each refused, twice\n");
}

void TestASamplerThatRefusedIsRefusedAcrossBothBundles(
    const std::string& fixture)
{
    const Rig rig = Open(fixture);
    rig.clipAnimation.GetAttribute(kRate).Block();

    ExecUsdSystem system(rig.stage);
    ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
    ArmAt(system, request, 24.0);
    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);
    AssertRefused(view, kSampledKey);
    AssertRefused(view, kBoundKey);
    AssertRefused(view, kRetargetKey);
    assert(MarkNames(mark, "motion.sampleAnimation: the clip states no usable "
                           "'motion:timeCodesPerSecond'"));
    assert(MarkNames(mark, "which answered no motion.sampleAnimation"));
    assert(MarkNames(mark, "answered no vrm.computeBoundPose"));
    mark.Clear();
    std::printf("execVrm retarget: a sampler that refused is refused by the "
                "bound pose and the retarget in turn\n");
}

void TestTheSourceAndTheRigAreRefusedAsTheCorrectionRefusesThem(
    const std::string& fixture)
{
    {
        const Rig rig = Open(fixture);
        UsdPrim humanoid = rig.humanoid;
        assert(humanoid.RemoveProperty(kSourceRel));
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kRetargetKey);
        assert(MarkNames(mark, "vrm.humanoidRetarget: "
                               "'vrm:retarget:sourceSkeleton' reaches nothing"));
        mark.Clear();
    }
    {
        const Rig rig = Open(fixture);
        assert(rig.humanoid.GetRelationship(kSourceRel).SetTargets(
            {kClipPath, kPosedPath}));
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kRetargetKey);
        assert(MarkNames(mark, "vrm.humanoidRetarget: "
                               "'vrm:retarget:sourceSkeleton' reaches 2"));
        mark.Clear();
    }
    {
        const Rig rig = Open(fixture);
        assert(rig.humanoid.GetRelationship(kSourceRel).SetTargets(
            {kTargetPath}));
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kRetargetKey);
        assert(MarkNames(mark, "vrm.humanoidRetarget: the source skeleton "
                               "</Asset/skel/Skeleton> is not a semantic"));
        mark.Clear();
    }
    {
        const Rig rig = Open(fixture);
        assert(rig.humanoid.GetAttribute(BoneAttribute(HumanBone::Hips))
                   .Set(TfToken("J_Bip_C_Hips")));
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kMapKey);
        AssertRefused(view, kRetargetKey);
        assert(MarkNames(mark, "vrm.humanoidRetarget: the humanoid's "
                               "vrm.computeHumanoidMap"));
        mark.Clear();
    }
    std::printf("execVrm retarget: no source, two, one that is not semantic, "
                "and a map that refused are each refused\n");
}

// ---------------------------------------------------------------------------
// A root-motion statement declared and given no value
// ---------------------------------------------------------------------------
// An attribute the prim does not have is no value, and keeps the library's
// default. One it DECLARES with no value -- or blocks -- is not: it arrives as
// the type's fallback beside an executor warning, schema property or not. So:
//
//   * a valueless mode is the empty token, and refused as naming no mode;
//   * a valueless scale is 0, a believable number, and answered: the root is
//     pinned at the rig's rest with nothing but the executor's warning to say
//     why. Pinned, because an authored zero is the same value and
//     `motion_retarget --translation-scale 0` accepts one.
void TestAStatementDeclaredWithNoValueIsTheFallback(const std::string& fixture)
{
    {
        const Rig rig = Open(fixture);
        UsdPrim humanoid = rig.humanoid;
        assert(humanoid.CreateAttribute(kRootMotion, SdfValueTypeNames->Token));
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        TfErrorMark mark;
        ExecUsdCacheView view = system.Compute(request);
        AssertRefused(view, kRetargetKey);
        assert(MarkNames(mark, "'vrm:retarget:rootMotion' is the empty token"));
        mark.Clear();
    }

    for (const bool block : {false, true}) {
        const Rig rig = Open(fixture);
        UsdPrim humanoid = rig.humanoid;
        UsdAttribute scale =
            humanoid.CreateAttribute(kTranslationScale, SdfValueTypeNames->Float);
        assert(scale);
        if (block) {
            assert(scale.Set(2.0f));
            scale.Block();
        }

        Warnings warnings;
        ExecUsdSystem system(rig.stage);
        ExecUsdRequest request = system.BuildRequest(KeysFor(rig));
        ArmAt(system, request, 24.0);
        TfErrorMark mark;
        const vrmRetarget::RetargetedPose pose =
            RetargetAt(system.Compute(request));
        assert(mark.IsClean());
        assert(pose.translations[kHipsJoint] == kHipsRest &&
               "a valueless scale is no longer the fallback 0 -- the pin is "
               "gone, and the node may now be able to default it instead");
        bool warned = false;
        for (const std::string& warning :
             warnings.Take("No value set for output")) {
            warned = warned || warning.find("vrm:retarget:translationScale")
                                   != std::string::npos;
        }
        assert(warned && "the executor did not warn about the fallback");
    }
    std::printf("execVrm retarget: a declared, valueless mode is the empty token "
                "and refused; a valueless or blocked scale is 0 and answered\n");
}

// ---------------------------------------------------------------------------
// --without-exec-motion: execMotion is not in the session
// ---------------------------------------------------------------------------
// Run by its own CTest entry, with PXR_PLUGINPATH_NAME *set* to this bundle and
// vrmSchema alone. This bundle links nothing of execMotion; what it needs is
// `motion.sampleAnimation` registered in the session, which is the edge
// `requires.bundles` states. So this is what composing execVrm without the
// bundle it reads looks like.
void TestWithoutExecMotionThePoseIsNotFound(const std::string& fixture)
{
    assert(!PlugRegistry::GetInstance().GetPluginWithName("ExecMotion") &&
           "execMotion is registered after all, so this run measures nothing");

    const Rig rig = Open(fixture);
    ExecUsdSystem system(rig.stage);
    // No key on the animation: nothing in this session provides it.
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(rig.humanoid, kCorrection);
    keys.emplace_back(rig.clip, kBoundPose);
    keys.emplace_back(rig.humanoid, kRetarget);
    ExecUsdRequest request = system.BuildRequest(std::move(keys));

    // At the default time code: with no sampler in the session nothing here
    // depends on time, so a frame change would recompute nothing and post
    // nothing -- the first compute is the one that says why.
    TfErrorMark mark;
    ExecUsdCacheView view = system.Compute(request);
    // The rig half needs nothing of execMotion.
    ValueAt<vrmRetarget::RestPoseCorrection>(view, 0, "rest-pose correction");
    AssertRefused(view, 1);
    AssertRefused(view, 2);
    for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it) {
        std::printf("  posted: %s\n", it->GetCommentary().c_str());
    }
    // The animation is dropped from the fan-in as a prim providing no
    // computation is -- the second read of the relationship is the only thing
    // that notices.
    assert(MarkNames(mark, "'skel:animationSource' targets </Clip/Animation>, "
                           "which answered no motion.sampleAnimation"));
    mark.Clear();
    std::printf("execVrm retarget (without execMotion): the rig computes and "
                "the bound pose and the retarget are refused\n");
}

} // namespace

int main(int argc, char** argv)
{
    assert((argc == 2 || argc == 3) &&
           "usage: execVrm_retarget <retargeted_rig.usda> "
           "[--without-exec-motion]");
    const std::string fixture = argv[1];

    // The map leaves 49 bones unbound; see the humanoid suite.
    Warnings all;

    if (argc == 3) {
        assert(std::string(argv[2]) == "--without-exec-motion");
        TestWithoutExecMotionThePoseIsNotFound(fixture);
        std::puts("execVrm retarget (without execMotion): all checks passed");
        return 0;
    }

    TestTheRetargetComputes(fixture);
    TestTheRootMotionStatementsAreTheToolsFlags(fixture);
    TestInvalidationReachesTheRetarget(fixture);
    TestADriversPoseReachesTheRetarget(fixture);
    TestTheRootMotionStatementsAreRefusedWhenUnhonourable(fixture);
    TestAClipTheSkeletonCannotReachIsRefused(fixture);
    TestASamplerThatRefusedIsRefusedAcrossBothBundles(fixture);
    TestTheSourceAndTheRigAreRefusedAsTheCorrectionRefusesThem(fixture);
    TestAStatementDeclaredWithNoValueIsTheFallback(fixture);

    for (const std::string& warning : all.Take("")) {
        if (warning.find("No value set for output") == std::string::npos) {
            std::printf("  also warned: %s\n", warning.c_str());
        }
    }
    std::puts("execVrm retarget: all checks passed");
    return 0;
}
