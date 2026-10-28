// SPDX-License-Identifier: Apache-2.0
//
// Installed-package consumer smoke: links the typed API from the installed
// vrmSchema package exactly the way the usdVrm importer does. Success is
// compiling, linking, and applying one schema on an in-memory stage.
#include <vrmSchema/tokens.h>
#include <vrmSchema/vrmHumanoidAPI.h>
#include <vrmSchema/vrmSpringBoneAPI.h>

#include <pxr/usd/usd/stage.h>
#include <pxr/base/vt/array.h>

#include <cstdio>

PXR_NAMESPACE_USING_DIRECTIVE

int
main()
{
    UsdStageRefPtr stage = UsdStage::CreateInMemory();
    UsdPrim prim = stage->DefinePrim(SdfPath("/Rig"));
    UsdVrmHumanoidAPI api = UsdVrmHumanoidAPI::Apply(prim);
    if (!api)
    {
        std::fprintf(stderr, "UsdVrmHumanoidAPI::Apply failed\n");
        return 1;
    }
    if (api.GetVrmHumanBonesHipsAttr().GetName() != UsdVrmTokens->vrmHumanBonesHips)
    {
        std::fprintf(stderr, "unexpected builtin attribute name\n");
        return 1;
    }
    UsdVrmSpringBoneAPI spring = UsdVrmSpringBoneAPI::Apply(stage->DefinePrim(SdfPath("/Spring")));
    VtTokenArray roots{TfToken("hips/hair")};
    spring.CreateVrmRootJointsAttr(VtValue(roots));
    VtTokenArray actual;
    if (!spring.GetVrmRootJointsAttr().Get(&actual) || actual != roots ||
        spring.GetVrmJointsAttr().HasAuthoredValueOpinion())
    {
        std::fprintf(stderr, "spring root declaration did not round-trip\n");
        return 1;
    }
    std::printf("vrmSchema installed-package consumer: OK\n");
    return 0;
}
