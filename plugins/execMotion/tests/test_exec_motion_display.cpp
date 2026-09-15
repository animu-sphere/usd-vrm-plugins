// SPDX-License-Identifier: Apache-2.0
//
// The OpenExec plan's P0-7: a clip's root motion reaching Hydra through
// `usdExecImaging`, on the one prim type 26.08 lets it reach -- a
// `UsdGeomXformable`.
//
// Nothing in this bundle is registered for `UsdGeomXformable`, and nothing may
// be: `execGeom` declares it, and a second declarer loses every computation it
// registers there. What this bundle registers instead is an **attribute
// expression** for `motion:root:transform` on a clip. An Xformable that connects
// its `xformOp:transform` to that attribute is placed by `execGeom`'s own
// `computeLocalToWorldTransform`, because `computeValue` follows one connection
// to an attribute of the same type -- and `usdExecImaging`'s Xformable adapter
// publishes exactly that computation as the prim's `xform` matrix.
//
// The plan's "done when", each asserted here:
//
//   * changing time recomputes;
//   * changing the motion input recomputes;
//   * an unrelated material change does **not** recompute the motion network;
//   * the stage uses `xformOp:transform` only, asserted before any drawn value
//     is trusted -- and in both directions, which the audit named only one of.
//
// The fifth, "from packaged plugins, not a build tree", is not claimed here:
// the suite loads the bundle the way every suite in this directory does,
// through `PXR_PLUGINPATH_NAME` and the staged plugInfo.json.
//
// Beside those, four things that are measurements rather than requirements:
// a refusal draws exactly where `ignore` does, three broken routes draw the
// prop's authored value without an error, the precondition breaks in a second
// direction the audit did not name, and a driver's root orientation -- which no
// clip states -- reaches `execGeom` as a turn about the root's own origin
// (docs/reports/openusd/26.08-openexec-display.md).
//
// The display half is `usdExecImaging`'s stage scene index driven directly:
// the same object `UsdImagingGLEngine` merges over the USD scene index when
// `USDIMAGINGGL_ENGINE_ENABLE_EXEC_SCENE_INDEX` is set, with the same three
// calls, and an observer standing where Hydra would. No GL context is needed,
// so this runs on every lane.
//
// Like every suite here but `execMotion_pose`, this executable does not link
// the plugin.

#include "pxr/pxr.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/errorMark.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/value.h"

#include "pxr/exec/exec/request.h"
#include "pxr/exec/execGeom/tokens.h"
#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"
#include "pxr/exec/execUsd/valueOverride.h"

#include "pxr/imaging/hd/dataSourceLocator.h"
#include "pxr/imaging/hd/sceneIndex.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/imaging/hd/xformSchema.h"
#include "pxr/usdImaging/usdExecImaging/stageSceneIndexFactory.h"
#include "pxr/usdImaging/usdExecImaging/stageSceneIndexInterface.h"

#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/xformOp.h"

#include <motionCore/Humanoid.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

const TfToken kSampleAnimation("motion.sampleAnimation");
const TfToken kRootTransform("motion:root:transform");
const TfToken kRootIntake("motion:root:intake");
const TfToken kTranslations("translations");
const TfToken kXformOpTransform("xformOp:transform");
const TfToken kDiffuseColor("inputs:diffuseColor");

const SdfPath kClip("/Clip");
const SdfPath kWorld("/World");
const SdfPath kProp("/World/Prop");
const SdfPath kMarker("/World/Prop/Marker");
const SdfPath kSurface("/Looks/Paint/Surface");

// Where /World puts everything under it, and where the fixture's prop says it
// is when nothing computes it -- the value a renderer without exec draws.
const GfVec3d kParent(10.0, 0.0, 0.0);
const GfVec3d kAuthoredProp(0.0, 0.0, -5.0);

// The hips at the fixture's two keys and halfway between them. Floats in the
// clip, so compared with a float's tolerance once they are doubles.
const GfVec3d kHipsAt0(0.0, 0.0, 0.0);
const GfVec3d kHipsAt50(0.0, 0.5, 1.0);
const GfVec3d kHipsAt100(0.0, 1.0, 2.0);
constexpr double kTolerance = 1e-6;

// A placement with no rotation or scale in it, at `translation`. Every
// transform this fixture produces is one: the clip states no root orientation,
// and nothing on the stage rotates.
bool
IsTranslation(const GfMatrix4d& m, const GfVec3d& translation)
{
    GfMatrix4d expected(1.0);
    expected.SetTranslateOnly(translation);
    for (int r = 0; r < 4; ++r)
    {
        for (int c = 0; c < 4; ++c)
        {
            if (std::abs(m[r][c] - expected[r][c]) > kTolerance)
            {
                return false;
            }
        }
    }
    return true;
}

std::string
Describe(const GfMatrix4d& m)
{
    const GfVec3d t = m.ExtractTranslation();
    char buffer[128];
    std::snprintf(buffer, sizeof buffer, "(%g, %g, %g)", t[0], t[1], t[2]);
    return buffer;
}

// ---------------------------------------------------------------------------
// The precondition
// ---------------------------------------------------------------------------
//
// `execGeom` reads `xformOp:transform` and nothing else: not `xformOpOrder`,
// not any other op. So an Xformable draws the same through exec and through
// `UsdGeomXformable` only when the two agree about what its local transform
// is, and that holds in exactly two shapes -- no ops at all and no
// `xformOp:transform`, or an order that is `[xformOp:transform]` and nothing
// more. The audit named one way out of those shapes (another op, which exec
// ignores); `TestThePreconditionIsTwoSided` measures the second (a transform
// the order does not list, which exec reads anyway).
//
// Returns the Xformables that break it, so a caller can assert on a stage that
// should pass and on one that should not.
std::vector<SdfPath>
TransformOnlyViolations(const UsdStageRefPtr& stage, std::size_t* checked)
{
    std::vector<SdfPath> violations;
    std::size_t count = 0;
    for (const UsdPrim& prim : stage->Traverse())
    {
        const UsdGeomXformable xformable(prim);
        if (!xformable)
        {
            continue;
        }
        ++count;

        bool resets = false;
        const std::vector<UsdGeomXformOp> ops = xformable.GetOrderedXformOps(&resets);
        const UsdAttribute transform = prim.GetAttribute(kXformOpTransform);
        const bool statesTransform =
            transform && (transform.HasAuthoredValue() || transform.HasAuthoredConnections());

        const bool noOps = ops.empty() && !statesTransform;
        const bool transformOnly = ops.size() == 1 &&
                                   ops[0].GetOpType() == UsdGeomXformOp::TypeTransform &&
                                   ops[0].GetName() == kXformOpTransform && !ops[0].IsInverseOp();
        if (resets || !(noOps || transformOnly))
        {
            violations.push_back(prim.GetPath());
        }
    }
    if (checked)
    {
        *checked = count;
    }
    return violations;
}

void
TestTheFixtureStatesTransformOnly(const UsdStageRefPtr& stage)
{
    std::size_t checked = 0;
    const std::vector<SdfPath> violations = TransformOnlyViolations(stage, &checked);
    for (const SdfPath& path : violations)
    {
        std::fprintf(stderr, "  not transform-only: %s\n", path.GetText());
    }
    assert(violations.empty() &&
           "an Xformable on the display stage states a transform exec and "
           "UsdGeom would read differently; no drawn value below means anything");
    // World, Prop and Marker. A traversal that found none would pass the check
    // above by checking nothing.
    assert(checked == 3);
    std::puts("execMotion_display: the fixture is transform-only (3 Xformables)");
}

// ---------------------------------------------------------------------------
// Through exec alone
// ---------------------------------------------------------------------------
//
// The dataflow before any imaging: `execGeom`'s computation, on prims this
// bundle registers nothing for, answering the clip's root motion.
void
TestExecPlacesTheProp(const UsdStageRefPtr& stage)
{
    const UsdPrim clip = stage->GetPrimAtPath(kClip);
    const UsdAttribute rootTransform = clip.GetAttribute(kRootTransform);
    assert(rootTransform && "the fixture's clip does not declare "
                            "motion:root:transform");

    ExecUsdSystem system(stage);

    std::set<int> timeReported;
    int valueInvalidations = 0;
    std::set<int> valueReported;

    const TfToken& localToWorld = ExecGeomXformableTokens->computeLocalToWorldTransform;
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(stage->GetPrimAtPath(kProp), localToWorld);   // 0
    keys.emplace_back(stage->GetPrimAtPath(kMarker), localToWorld); // 1
    keys.emplace_back(stage->GetPrimAtPath(kWorld), localToWorld);  // 2
    keys.emplace_back(rootTransform);                               // 3
    ExecUsdRequest request = system.BuildRequest(
        std::move(keys),
        [&](const ExecRequestIndexSet& indices, const EfTimeInterval&)
        {
            ++valueInvalidations;
            valueReported.insert(indices.begin(), indices.end());
        },
        [&](const ExecRequestIndexSet& indices)
        { timeReported.insert(indices.begin(), indices.end()); });
    assert(request.IsValid());

    // An unstaged bundle does NOT show up here. Without the expression the
    // attribute's computed value is its resolved value, and a declared,
    // valueless matrix4d resolves to the type's fallback -- the identity,
    // beside a "No value set" executor warning (the display report section
    // 2). That is where the root is at the default time code and at frame 0,
    // so the first frame that can tell is 50; `AssertBundleRegistered` in
    // main is what names the cause before any of this runs.
    auto matrixAt = [](const ExecUsdCacheView& view, int index)
    {
        const VtValue value = view.Get(index);
        assert(value.IsHolding<GfMatrix4d>() &&
               "no matrix came back from execGeom or the attribute");
        return value.UncheckedGet<GfMatrix4d>();
    };

    // ---- the default time code: armed, and placed at the parent -----------
    //
    // A clip that authors only time samples resolves to nothing there (the
    // sampling report section 4), so the pose has no root, the root motion
    // states nothing, and an unstated root is the identity. The prop sits at
    // its parent -- which is also where `ignore` and a refusal put it, below.
    {
        const ExecUsdCacheView view = system.Compute(request);
        const GfMatrix4d prop = matrixAt(view, 0);
        assert(IsTranslation(prop, kParent) &&
               "at the default time code the prop should be at its parent");
        assert(IsTranslation(matrixAt(view, 3), GfVec3d(0.0)));
    }

    // ---- three frames ------------------------------------------------------
    const std::pair<double, GfVec3d> frames[] = {
        {0.0, kHipsAt0}, {50.0, kHipsAt50}, {100.0, kHipsAt100}};
    for (const auto& [frame, hips] : frames)
    {
        timeReported.clear();
        system.ChangeTime(UsdTimeCode(frame));
        const ExecUsdCacheView view = system.Compute(request);

        const GfMatrix4d prop = matrixAt(view, 0);
        const GfMatrix4d marker = matrixAt(view, 1);
        const GfMatrix4d world = matrixAt(view, 2);
        const GfMatrix4d root = matrixAt(view, 3);
        std::printf("  frame %g: root %s, prop %s, marker %s, world %s\n", frame,
                    Describe(root).c_str(), Describe(prop).c_str(), Describe(marker).c_str(),
                    Describe(world).c_str());

        // The attribute alone: the root motion as a matrix, no parent in it.
        // An identity here at frame 50 or 100 is the expression not running.
        assert(IsTranslation(root, hips) &&
               "motion:root:transform is not the clip's root -- an identity "
               "here is the attribute's valueless fallback, which is what it "
               "computes when no attribute expression is registered for it");
        // The prop: that, composed under /World by execGeom.
        assert(IsTranslation(prop, hips + kParent) &&
               "execGeom did not place the prop by the clip's root");
        // The marker states no transform of its own, so it inherits.
        assert(IsTranslation(marker, hips + kParent));
        // And /World, which reads nothing of the clip, never moves.
        assert(IsTranslation(world, kParent));
    }

    // Time dependence reaches the prim this bundle registered nothing for, and
    // its child, and stops at a prim the clip does not reach. The last frame
    // change is the one measured.
    assert(timeReported.count(0) && timeReported.count(1) && timeReported.count(3) &&
           "a frame change did not reach execGeom's computation through the "
           "connection");
    assert(!timeReported.count(2) && "/World reads nothing time dependent and was reported anyway");

    // ---- an unrelated material edit invalidates nothing --------------------
    //
    // Invalidation runs downstream from what changed, so a leaf that is not
    // reported is a network with no node reported upstream of it either: the
    // material edit reached no part of the motion network.
    const int before = valueInvalidations;
    valueReported.clear();
    stage->GetPrimAtPath(kSurface).GetAttribute(kDiffuseColor).Set(GfVec3f(0.1f, 0.8f, 0.1f));
    {
        const ExecUsdCacheView view = system.Compute(request);
        assert(IsTranslation(matrixAt(view, 0), kHipsAt100 + kParent));
    }
    assert(valueInvalidations == before && valueReported.empty() &&
           "a material edit invalidated a transform");

    // ---- the motion input: one edit of the clip reaches the prop -----------
    stage->GetPrimAtPath(kClip)
        .GetAttribute(kTranslations)
        .Set(VtVec3fArray{GfVec3f(3, 0, 0), GfVec3f(0, 0.1f, 0), GfVec3f(0, 0.2f, 0),
                          GfVec3f(0, 0.3f, 0), GfVec3f(9, 9, 9)},
             UsdTimeCode(100.0));
    {
        const ExecUsdCacheView view = system.Compute(request);
        assert(IsTranslation(matrixAt(view, 0), GfVec3d(13.0, 0.0, 0.0)) &&
               "an edit of the clip did not reach the prop");
    }
    assert(valueReported.count(0) && valueReported.count(1) && valueReported.count(3) &&
           !valueReported.count(2));

    // ---- an orientation, which no clip states ------------------------------
    //
    // A `UsdSkelAnimation` read into a canonical pose carries the hips position
    // and no root orientation -- here and in `motion_retarget`'s reader alike
    // -- so the fixture can only ever translate the prop. A live source does
    // state one, and it reaches the graph the way every driver's value does:
    // as an override. A quarter turn about +Y at (1, 0, 0) must turn the prop
    // about the root's own origin and then carry it there, under /World.
    {
        motion::HumanoidPose pose;
        pose.timestamp = 2.0;
        pose.root.worldPosition = GfVec3f(1.0f, 0.0f, 0.0f);
        pose.root.hasPosition = true;
        const float half = static_cast<float>(M_SQRT1_2);
        pose.root.worldOrientation = GfQuatf(half, 0.0f, half, 0.0f);
        pose.root.hasOrientation = true;

        ExecUsdValueOverrideVector overrides;
        overrides.push_back(
            ExecUsdValueOverride{ExecUsdValueKey(clip, kSampleAnimation), VtValue(pose)});
        const ExecUsdCacheView view = system.ComputeWithOverrides(request, std::move(overrides));
        const GfMatrix4d prop = matrixAt(view, 0);

        // A point a metre along the prop's +X lands a metre along -Z of the
        // root, and the root is 1 m along +X of /World, which is 10 m along +X.
        const GfVec3d moved = prop.Transform(GfVec3d(1.0, 0.0, 0.0));
        std::printf("  a driver's root turned about +Y at (1, 0, 0): prop "
                    "origin %s, its +X at (%g, %g, %g)\n",
                    Describe(prop).c_str(), moved[0], moved[1], moved[2]);
        assert(std::abs(moved[0] - 11.0) <= kTolerance && std::abs(moved[1]) <= kTolerance &&
               std::abs(moved[2] + 1.0) <= kTolerance &&
               "a root orientation did not reach execGeom as a turn about "
               "the root's own origin");
        assert(IsTranslation(matrixAt(view, 2), kParent));
    }

    std::puts("execMotion_display: exec places the prop by the clip's root");
}

// ---------------------------------------------------------------------------
// Through usdExecImaging
// ---------------------------------------------------------------------------

// Stands where Hydra would: every notice the exec scene index sends.
class Observer : public HdSceneIndexObserver
{
  public:
    void
    PrimsAdded(const HdSceneIndexBase&, const AddedPrimEntries&) override
    {
        ++added;
    }
    void
    PrimsRemoved(const HdSceneIndexBase&, const RemovedPrimEntries&) override
    {
        ++removed;
    }
    void
    PrimsDirtied(const HdSceneIndexBase&, const DirtiedPrimEntries& entries) override
    {
        dirtied.insert(dirtied.end(), entries.begin(), entries.end());
    }
    void
    PrimsRenamed(const HdSceneIndexBase&, const RenamedPrimEntries&) override
    {
        ++renamed;
    }

    // Whether `path` was dirtied at its transform since the last Clear.
    bool
    TransformDirtied(const SdfPath& path) const
    {
        static const HdDataSourceLocator matrix(HdXformSchemaTokens->xform,
                                                HdXformSchemaTokens->matrix);
        for (const DirtiedPrimEntry& entry : dirtied)
        {
            if (entry.primPath == path && entry.dirtyLocators.Intersects(matrix))
            {
                return true;
            }
        }
        return false;
    }

    void
    Clear()
    {
        dirtied.clear();
    }

    std::vector<DirtiedPrimEntry> dirtied;
    int added = 0;
    int removed = 0;
    int renamed = 0;
};

// The matrix the exec scene index hands Hydra for `path`: `HdXformSchema`'s
// `matrix`, which is what a renderer reads.
GfMatrix4d
Drawn(const UsdExecImagingStageSceneIndexInterfaceRefPtr& sceneIndex, const SdfPath& path)
{
    const HdSceneIndexPrim prim = sceneIndex->GetPrim(path);
    assert(prim.dataSource && "the exec scene index has no data for this prim");
    const HdXformSchema xform = HdXformSchema::GetFromParent(prim.dataSource);
    const HdMatrixDataSourceHandle matrix = xform.GetMatrix();
    assert(matrix && "the prim's data has no xform matrix");
    // Reset, always: the adapter publishes a local-to-WORLD matrix, so Hydra
    // must not compose it under the parent a second time.
    const HdBoolDataSourceHandle resets = xform.GetResetXformStack();
    assert(resets && resets->GetTypedValue(0.0f));
    return matrix->GetTypedValue(0.0f);
}

void
TestTheSceneIndexDrawsIt(const UsdStageRefPtr& stage)
{
    const UsdExecImagingStageSceneIndexInterfaceRefPtr sceneIndex =
        UsdExecImagingCreateStageSceneIndex();
    assert(sceneIndex && "null: this OpenUSD was built with PXR_BUILD_EXEC off, which the "
                         "capability probe should have refused at configure time");

    Observer observer;
    sceneIndex->AddObserver(HdSceneIndexObserverPtr(&observer));
    sceneIndex->SetStage(stage);

    // Only what the two adapters cover has data: the clip is a
    // UsdSkelAnimation, and it is placed by nothing -- it only places.
    assert(!sceneIndex->GetPrim(kClip).dataSource);
    assert(!sceneIndex->GetPrim(kSurface).dataSource);

    // ---- time --------------------------------------------------------------
    sceneIndex->SetTime(UsdTimeCode(0.0));
    assert(IsTranslation(Drawn(sceneIndex, kProp), kHipsAt0 + kParent));

    observer.Clear();
    sceneIndex->SetTime(UsdTimeCode(50.0));
    assert(observer.TransformDirtied(kProp) && "a frame change did not dirty the prop's transform");
    assert(observer.TransformDirtied(kMarker));
    assert(!observer.TransformDirtied(kWorld) &&
           "a frame change dirtied a transform that reads nothing of the clip");
    const GfMatrix4d at50 = Drawn(sceneIndex, kProp);
    std::printf("  drawn at frame 50: prop %s\n", Describe(at50).c_str());
    assert(IsTranslation(at50, kHipsAt50 + kParent) &&
           "Hydra was handed a different placement from exec's");
    assert(IsTranslation(Drawn(sceneIndex, kMarker), kHipsAt50 + kParent));
    assert(IsTranslation(Drawn(sceneIndex, kWorld), kParent));

    // ---- an unrelated material edit ---------------------------------------
    observer.Clear();
    stage->GetPrimAtPath(kSurface).GetAttribute(kDiffuseColor).Set(GfVec3f(0.1f, 0.1f, 0.8f));
    sceneIndex->ApplyPendingUpdates();
    assert(observer.dirtied.empty() && "a material edit dirtied a prim in the exec scene index");
    assert(IsTranslation(Drawn(sceneIndex, kProp), kHipsAt50 + kParent));

    // ---- the motion input --------------------------------------------------
    //
    // The scene index holds exec's invalidation until ApplyPendingUpdates --
    // that is what the engine's notice batching is for -- so nothing reaches
    // the observer at the edit itself.
    observer.Clear();
    const UsdPrim clip = stage->GetPrimAtPath(kClip);
    UsdAttribute intake = clip.CreateAttribute(kRootIntake, SdfValueTypeNames->Token);
    intake.Set(TfToken("ignore"));
    assert(observer.dirtied.empty() &&
           "the exec scene index sent a notice before ApplyPendingUpdates");
    sceneIndex->ApplyPendingUpdates();
    assert(observer.TransformDirtied(kProp) &&
           "an edit of the clip's policy did not dirty the prop");
    const GfMatrix4d ignored = Drawn(sceneIndex, kProp);
    assert(IsTranslation(ignored, kParent) && "`ignore` should leave the prop at its parent");

    // ---- a refusal draws exactly where `ignore` does -----------------------
    //
    // The bundle refuses a token naming no policy by setting no value, and
    // execGeom reads an absent local transform as the identity. So the picture
    // cannot tell a deliberate `ignore` from a misspelled one; only the TfError
    // can. Measured, because it is the display half of the rule every
    // refusal here was written to keep -- and the display half breaks it.
    observer.Clear();
    {
        TfErrorMark mark;
        intake.Set(TfToken("ignroe"));
        sceneIndex->ApplyPendingUpdates();
        const GfMatrix4d refused = Drawn(sceneIndex, kProp);
        assert(refused == ignored && "a refusal drew somewhere other than `ignore` -- the finding "
                                     "this block records has changed, and the report with it");
        bool named = false;
        for (TfErrorMark::Iterator it = mark.GetBegin(); it != mark.GetEnd(); ++it)
        {
            named =
                named || it->GetCommentary().find("motion.extractRootMotion") != std::string::npos;
        }
        assert(named && "the refusal posted no error naming the node, so "
                        "nothing at all distinguishes it from `ignore`");
        mark.Clear();
    }
    intake.Set(TfToken("passthrough"));
    sceneIndex->ApplyPendingUpdates();
    assert(IsTranslation(Drawn(sceneIndex, kProp), kHipsAt50 + kParent));

    // ---- the connection is the whole route, and it fails quietly ----------
    //
    // `computeValue` follows exactly one connection to a valid attribute of
    // the same type. Anything else and the prop's own authored value is what
    // exec answers -- drawn, with no diagnostic.
    UsdAttribute transform = stage->GetPrimAtPath(kProp).GetAttribute(kXformOpTransform);
    const SdfPathVector connected = {kClip.AppendProperty(kRootTransform)};
    const GfVec3d fallback = kAuthoredProp + kParent;

    struct Route
    {
        const char* what;
        SdfPathVector targets;
    };
    const Route routes[] = {
        {"a connection to an attribute the clip does not declare",
         {kClip.AppendProperty(TfToken("motion:root:transfrom"))}},
        {"two connections, both to the declared attribute's type",
         {kClip.AppendProperty(kRootTransform), kWorld.AppendProperty(kXformOpTransform)}},
        {"a connection to an attribute of another type",
         {kClip.AppendProperty(TfToken("motion:timeCodesPerSecond"))}},
    };
    for (const Route& route : routes)
    {
        observer.Clear();
        TfErrorMark mark;
        transform.SetConnections(route.targets);
        sceneIndex->ApplyPendingUpdates();
        const GfMatrix4d drawn = Drawn(sceneIndex, kProp);
        std::printf("  %s: drawn %s, %zu error(s)\n", route.what, Describe(drawn).c_str(),
                    static_cast<std::size_t>(std::distance(mark.GetBegin(), mark.GetEnd())));
        assert(observer.TransformDirtied(kProp) &&
               "re-routing the connection did not dirty the prop");
        assert(IsTranslation(drawn, fallback) &&
               "a broken route drew somewhere other than the prop's own "
               "authored value");
        assert(mark.IsClean() && "a broken route now posts an error -- better than 26.08, and "
                                 "this suite should say so");
        mark.Clear();
    }
    transform.SetConnections(connected);
    sceneIndex->ApplyPendingUpdates();
    assert(IsTranslation(Drawn(sceneIndex, kProp), kHipsAt50 + kParent) &&
           "restoring the one connection did not restore the placement");

    // Nothing was added, removed or renamed: the exec scene index only ever
    // dirties, since the USD scene index beneath it owns the prims.
    assert(observer.added == 0 && observer.removed == 0 && observer.renamed == 0);

    sceneIndex->RemoveObserver(HdSceneIndexObserverPtr(&observer));
    std::puts("execMotion_display: usdExecImaging draws the prop where exec "
              "places it");
}

// ---------------------------------------------------------------------------
// The precondition, measured in both directions
// ---------------------------------------------------------------------------
//
// Two prims that break it, one each way, on a stage of their own so the
// fixture stays one that passes. Each is compared through exec and through
// `UsdGeomXformable::ComputeLocalToWorldTransform`, which is what every
// renderer not running exec uses -- and the precondition check must flag both.
void
TestThePreconditionIsTwoSided()
{
    UsdStageRefPtr stage = UsdStage::CreateInMemory();
    UsdGeomXform parent = UsdGeomXform::Define(stage, SdfPath("/Parent"));
    GfMatrix4d parentMatrix(1.0);
    parentMatrix.SetTranslateOnly(kParent);
    parent.AddTransformOp().Set(parentMatrix);

    // Another op and no transform: the audit's direction. Exec ignores it.
    UsdGeomXform nudged = UsdGeomXform::Define(stage, SdfPath("/Parent/Nudged"));
    nudged.AddTranslateOp().Set(GfVec3d(0.0, 3.0, 0.0));

    // A transform the order does not list: the other direction. UsdGeom
    // ignores it, and exec reads `xformOp:transform` by name, so exec does not.
    UsdGeomXform unordered = UsdGeomXform::Define(stage, SdfPath("/Parent/Unordered"));
    GfMatrix4d unorderedMatrix(1.0);
    unorderedMatrix.SetTranslateOnly(GfVec3d(0.0, 0.0, 7.0));
    unordered.GetPrim()
        .CreateAttribute(kXformOpTransform, SdfValueTypeNames->Matrix4d)
        .Set(unorderedMatrix);

    std::size_t checked = 0;
    const std::vector<SdfPath> violations = TransformOnlyViolations(stage, &checked);
    assert(checked == 3);
    assert(violations.size() == 2 && violations[0] == SdfPath("/Parent/Nudged") &&
           violations[1] == SdfPath("/Parent/Unordered") &&
           "the precondition check missed a prim that draws differently");

    ExecUsdSystem system(stage);
    const TfToken& localToWorld = ExecGeomXformableTokens->computeLocalToWorldTransform;
    std::vector<ExecUsdValueKey> keys;
    keys.emplace_back(nudged.GetPrim(), localToWorld);
    keys.emplace_back(unordered.GetPrim(), localToWorld);
    ExecUsdRequest request = system.BuildRequest(std::move(keys));
    const ExecUsdCacheView view = system.Compute(request);
    const GfMatrix4d nudgedExec = view.Get(0).Get<GfMatrix4d>();
    const GfMatrix4d unorderedExec = view.Get(1).Get<GfMatrix4d>();

    const GfMatrix4d nudgedUsd = nudged.ComputeLocalToWorldTransform(UsdTimeCode::Default());
    const GfMatrix4d unorderedUsd = unordered.ComputeLocalToWorldTransform(UsdTimeCode::Default());
    std::printf("  xformOp:translate only:     exec %s, UsdGeom %s\n", Describe(nudgedExec).c_str(),
                Describe(nudgedUsd).c_str());
    std::printf("  xformOp:transform unlisted: exec %s, UsdGeom %s\n",
                Describe(unorderedExec).c_str(), Describe(unorderedUsd).c_str());

    assert(IsTranslation(nudgedExec, kParent) &&
           IsTranslation(nudgedUsd, kParent + GfVec3d(0.0, 3.0, 0.0)) &&
           "exec now reads ops other than xformOp:transform");
    assert(IsTranslation(unorderedExec, kParent + GfVec3d(0.0, 0.0, 7.0)) &&
           IsTranslation(unorderedUsd, kParent) &&
           "exec now honours xformOpOrder for xformOp:transform");

    std::puts("execMotion_display: the precondition is two-sided");
}

// The one cause the drawn values cannot name. Without the bundle, every
// transform in this suite is still a matrix -- the attribute's valueless
// fallback is the identity -- and the first assertion to fail is a placement
// at frame 50. So the registration is asserted first, by the plugin name the
// staged plugInfo.json declares.
void
AssertBundleRegistered()
{
    const PlugPluginPtr plugin = PlugRegistry::GetInstance().GetPluginWithName("ExecMotion");
    assert(plugin && "the ExecMotion plugin is not registered: PXR_PLUGINPATH_NAME does "
                     "not reach its staged plugInfo.json, and without it every "
                     "transform below falls back to the identity rather than failing");
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s displayed_clip.usda\n", argv[0]);
        return 2;
    }
    AssertBundleRegistered();

    // Each test opens its own stage: two of them edit the clip, and a later
    // test reading an earlier one's edits would be measuring an order.
    //
    // Every edit goes to the stage's own session layer, so the fixture's layer
    // -- shared between the three stages through the layer registry -- is never
    // touched and no test can see another's edits.
    auto open = [&]()
    {
        UsdStageRefPtr stage = UsdStage::Open(argv[1]);
        assert(stage && "could not open the display fixture");
        stage->SetEditTarget(stage->GetSessionLayer());
        return stage;
    };
    TestTheFixtureStatesTransformOnly(open());
    TestExecPlacesTheProp(open());
    TestTheSceneIndexDrawsIt(open());
    TestThePreconditionIsTwoSided();

    std::puts("execMotion_display: all passed");
    return 0;
}
