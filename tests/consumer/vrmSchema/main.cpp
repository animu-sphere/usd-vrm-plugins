// SPDX-License-Identifier: Apache-2.0
//
// Includes one public header of the installed `vrmSchema` package and calls
// into it. The include proves the package installed its header root; the call
// proves it installed something to link, and that the shared object behind the
// import library is there to load.
//
// `vrmHumanoidAPI.h` is a generated typed schema, so it pulls this package's
// whole edge set in with it -- the OpenUSD headers a schema class is built from
// arrive through it, and this package's own token header does too. A config
// that resolved the target and left `pxr` unresolved compiles no further than
// the first `#include`.
//
// The call needs no stage on purpose. A typed schema's attribute names are a
// static property of the class, so asking for them exercises the library's own
// registration path without opening a layer, and a packaging fixture that
// opened one would be measuring OpenUSD's plugin discovery instead of this
// package's CMake contract. That other contract -- the plugin registers and a
// stage opens -- is a different one, and `scripts/clean_install_smoke.py` gates
// it from packaged artifacts (PACKAGE_CONTRACT.md §4.1).
//
// This is deliberately not a test of the schemas. `plugins/vrmSchema/tests/`
// owns the applied-API round trips and the generated-schema comparison;
// duplicating any of it here would make a packaging failure look like a schema
// failure the first time this fixture went red.
#include <vrmSchema/vrmHumanoidAPI.h>

#include <cstdio>

int
main()
{
    // The attribute names the generated class declares, inherited ones
    // included. A schema that shipped a header and an empty library would
    // return an empty vector here rather than fail to link, which is why the
    // count is checked and not merely the call.
    const pxr::TfTokenVector& names =
        pxr::UsdVrmHumanoidAPI::GetSchemaAttributeNames(true);
    if (names.empty()) {
        std::fprintf(stderr, "consumer: the installed package declares no "
                             "humanoid attributes\n");
        return 1;
    }

    // One token by name, from this package's own generated token table: the
    // hips are the bone every VRM humanoid must define, so a table that came
    // back without them would be a different build behind a matching header.
    const pxr::TfToken& hips = pxr::UsdVrmTokens->vrmHumanBonesHips;
    if (hips.IsEmpty()) {
        std::fprintf(stderr, "consumer: the installed token table has no "
                             "hips\n");
        return 1;
    }

    std::fprintf(stdout, "consumer: read %zu attribute name(s) and the token "
                         "%s through the installed package\n",
                 names.size(), hips.GetText());
    return 0;
}
