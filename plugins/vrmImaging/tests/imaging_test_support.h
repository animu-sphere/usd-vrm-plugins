// SPDX-License-Identifier: Apache-2.0
//
// What every vrmImaging suite does to stand in for a Hydra consumer: open a
// stage into UsdImaging's own stage scene index, read prims and data sources
// from it, and record the dirty notices it sends. Nothing here queries the
// stage for a value; the stage is only ever written, as an editor or an
// evaluator would write it.
#ifndef USD_VRM_IMAGING_TEST_SUPPORT_H
#define USD_VRM_IMAGING_TEST_SUPPORT_H

#include "pxr/pxr.h"

// First, before anything that reaches <windows.h>: hd's material network
// schema has a token named `interface`, which <objbase.h> defines as `struct`.
#include "pxr/imaging/hd/materialNetworkSchema.h"
#include "pxr/imaging/hd/materialSchema.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/vt/value.h"
#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceLocator.h"
#include "pxr/imaging/hd/sceneIndexObserver.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usdImaging/usdImaging/stageSceneIndex.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace vrm_imaging_test {

PXR_NAMESPACE_USING_DIRECTIVE

inline const TfToken kVrm("vrm");

inline HdDataSourceLocator
VrmLocator(const char* group, const char* field)
{
    return HdDataSourceLocator(kVrm, TfToken(group), TfToken(field));
}

// Every dirty notice, merged per prim, between two Clear()s.
class Recorder : public HdSceneIndexObserver
{
public:
    void PrimsAdded(const HdSceneIndexBase&, const AddedPrimEntries&) override {}
    void PrimsRemoved(const HdSceneIndexBase&, const RemovedPrimEntries&) override {}
    void PrimsRenamed(const HdSceneIndexBase&, const RenamedPrimEntries&) override {}
    void PrimsDirtied(const HdSceneIndexBase&,
                      const DirtiedPrimEntries& entries) override
    {
        for (const DirtiedPrimEntry& entry : entries) {
            dirtied[entry.primPath].insert(entry.dirtyLocators);
        }
    }

    HdDataSourceLocatorSet At(const char* path) const
    {
        const auto it = dirtied.find(SdfPath(path));
        return it == dirtied.end() ? HdDataSourceLocatorSet() : it->second;
    }

    void Clear() { dirtied.clear(); }

    std::map<SdfPath, HdDataSourceLocatorSet> dirtied;
};

struct Session
{
    UsdStageRefPtr stage;
    UsdImagingStageSceneIndexRefPtr sceneIndex;
    Recorder recorder;
};

inline void
Open(Session& session, const std::string& fixture)
{
    session.stage = UsdStage::Open(fixture);
    assert(session.stage && "the fixture does not open");
    session.sceneIndex = UsdImagingStageSceneIndex::New();
    session.sceneIndex->SetStage(session.stage);
    session.sceneIndex->SetTime(UsdTimeCode(0.0));
    session.sceneIndex->AddObserver(HdSceneIndexObserverPtr(&session.recorder));
}

inline HdContainerDataSourceHandle
PrimData(const Session& session, const char* path)
{
    return session.sceneIndex->GetPrim(SdfPath(path)).dataSource;
}

inline VtValue
ValueAt(const Session& session, const char* path,
        const HdDataSourceLocator& locator)
{
    const HdSampledDataSourceHandle sampled = HdSampledDataSource::Cast(
        HdContainerDataSource::Get(PrimData(session, path), locator));
    return sampled ? sampled->GetValue(0.0f) : VtValue();
}

inline float
FloatAt(const Session& session, const char* path,
        const HdDataSourceLocator& locator)
{
    const VtValue value = ValueAt(session, path, locator);
    assert(value.IsHolding<float>() && "the field is missing or not a float");
    return value.UncheckedGet<float>();
}

inline bool
Near(float a, float b)
{
    return std::fabs(a - b) < 1e-6f;
}

// The attribute an edit is made through: a direct stage write, as an
// expression evaluator or an editor would make it.
inline UsdAttribute
Attribute(const Session& session, const char* path, const char* name)
{
    const UsdAttribute attribute =
        session.stage->GetPrimAtPath(SdfPath(path)).GetAttribute(TfToken(name));
    assert(attribute && "the fixture lost an attribute this test edits");
    return attribute;
}

inline void
PrintLocators(const char* what, const HdDataSourceLocatorSet& locators)
{
    std::fprintf(stderr, "%s dirtied:\n", what);
    for (const HdDataSourceLocator& locator : locators) {
        std::fprintf(stderr, "  %s\n", locator.GetString().c_str());
    }
}

} // namespace vrm_imaging_test

#endif
