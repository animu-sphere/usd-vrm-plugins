// SPDX-License-Identifier: Apache-2.0
//
// vrmImaging Step I5 (VRM_IMAGING_POLICY.md §17.3): a `.vrm` imported by
// usdVrmFileFormat reaches a Hydra consumer through vrmImaging, and what it
// reads there is what the source file says.
//
//     .vrm -> usdVrmFileFormat -> Vrm*API -> vrmImaging -> Hydra data
//
// The expectation is not this suite's: `tests/imaging/test_import_imaging.py`
// writes it from the source GLB's JSON with `material_oracle.py`, the
// importer suite's independent restatement of the canonical mapping, and hands
// it here with the `.vrm`. This suite opens the `.vrm` into UsdImaging's stage
// scene index -- the importer, vrmSchema and vrmImaging registered, nothing
// linked -- and reads every value from the scene index, never from the stage.
//
// What is measured, per Material prim:
//
//   * the set: the prims carrying a `vrm` contribution are exactly the
//     source's materials, and each has exactly the groups its schemas give;
//   * `vrm/material` and `vrm/mtoon`: exactly the fields the source maps to,
//     each equal to the source's value or specification default;
//   * `vrm/textureInfo`: exactly the source's roles, each field the source
//     states equal to it, and `file` resolving -- through the session's
//     resolver, as a renderer would -- to the bytes of the source's image.

#include "imaging_test_support.h"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/js/json.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/imaging/hd/sceneIndexPrimView.h"
#include "pxr/usd/ar/asset.h"
#include "pxr/usd/ar/resolvedPath.h"
#include "pxr/usd/ar/resolver.h"
#include "pxr/usd/sdf/assetPath.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace vrm_imaging_test;

namespace {

int failures = 0;

// Every mismatch is reported, not just the first, so a run names all of them.
bool
Check(bool condition, const std::string& what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        ++failures;
    }
    return condition;
}

HdDataSourceLocator
Nested(HdDataSourceLocator locator, const std::string& name)
{
    for (const std::string& element : TfStringSplit(name, ":")) {
        locator = locator.Append(TfToken(element));
    }
    return locator;
}

std::set<std::string>
Names(const HdContainerDataSourceHandle& prim, const HdDataSourceLocator& at)
{
    std::set<std::string> names;
    if (const auto container = HdContainerDataSource::Cast(
            HdContainerDataSource::Get(prim, at))) {
        for (const TfToken& name : container->GetNames()) {
            names.insert(name.GetString());
        }
    }
    return names;
}

std::set<std::string>
Keys(const JsObject& object)
{
    std::set<std::string> keys;
    for (const auto& entry : object) {
        keys.insert(entry.first);
    }
    return keys;
}

std::string
Join(const std::set<std::string>& names)
{
    return "{" + TfStringJoin(names.begin(), names.end(), ", ") + "}";
}

bool
Number(const JsValue& value, double* out)
{
    if (value.IsReal()) {
        *out = value.GetReal();
    } else if (value.IsInt()) {
        *out = static_cast<double>(value.GetInt64());
    } else {
        return false;
    }
    return true;
}

// The oracle's tolerance: relative and absolute 1e-6.
bool
Close(double a, double b)
{
    return std::fabs(a - b) <=
           std::max(1e-6, 1e-6 * std::max(std::fabs(a), std::fabs(b)));
}

// A scene-index value as the oracle writes one: a number, a bool, a string,
// or an array of numbers for a vector.
std::vector<double>
Components(const VtValue& value, bool* isVector)
{
    *isVector = true;
    if (value.IsHolding<GfVec2f>()) {
        const GfVec2f v = value.UncheckedGet<GfVec2f>();
        return {v[0], v[1]};
    }
    if (value.IsHolding<GfVec3f>()) {
        const GfVec3f v = value.UncheckedGet<GfVec3f>();
        return {v[0], v[1], v[2]};
    }
    if (value.IsHolding<GfVec4f>()) {
        const GfVec4f v = value.UncheckedGet<GfVec4f>();
        return {v[0], v[1], v[2], v[3]};
    }
    *isVector = false;
    if (value.IsHolding<float>()) {
        return {value.UncheckedGet<float>()};
    }
    if (value.IsHolding<double>()) {
        return {value.UncheckedGet<double>()};
    }
    if (value.IsHolding<int>()) {
        return {static_cast<double>(value.UncheckedGet<int>())};
    }
    return {};
}

bool
Matches(const VtValue& got, const JsValue& want)
{
    if (want.IsBool()) {
        return got.IsHolding<bool>() && got.UncheckedGet<bool>() == want.GetBool();
    }
    if (want.IsString()) {
        if (got.IsHolding<TfToken>()) {
            return got.UncheckedGet<TfToken>().GetString() == want.GetString();
        }
        return got.IsHolding<std::string>() &&
               got.UncheckedGet<std::string>() == want.GetString();
    }
    bool isVector = false;
    const std::vector<double> components = Components(got, &isVector);
    double number = 0.0;
    if (Number(want, &number)) {
        return !isVector && components.size() == 1 && Close(components[0], number);
    }
    if (want.IsArray()) {
        const JsArray& array = want.GetJsArray();
        if (!isVector || components.size() != array.size()) {
            return false;
        }
        for (size_t i = 0; i < array.size(); ++i) {
            if (!Number(array[i], &number) || !Close(components[i], number)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

std::string
Describe(const VtValue& value)
{
    return value.IsEmpty() ? std::string("<absent>") : TfStringify(value);
}

std::string
Hex(const char* data, size_t size)
{
    static const char* const kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        const unsigned char byte = static_cast<unsigned char>(data[i]);
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0xf]);
    }
    return out;
}

// The image a role's `file` names, read as a renderer would read it: the
// asset path the scene index hands out, resolved and opened by the session's
// resolver. Empty when it does not resolve or open.
std::string
ImageHex(const HdContainerDataSourceHandle& prim,
         const HdDataSourceLocator& file, std::string* assetPath)
{
    const auto source = HdTypedSampledDataSource<SdfAssetPath>::Cast(
        HdContainerDataSource::Get(prim, file));
    if (!source) {
        return std::string();
    }
    const SdfAssetPath path = source->GetTypedValue(0.0f);
    *assetPath = path.GetAssetPath();
    if (path.GetResolvedPath().empty()) {
        return std::string();
    }
    const std::shared_ptr<ArAsset> asset =
        ArGetResolver().OpenAsset(ArResolvedPath(path.GetResolvedPath()));
    if (!asset) {
        return std::string();
    }
    const std::shared_ptr<const char> buffer = asset->GetBuffer();
    return buffer ? Hex(buffer.get(), asset->GetSize()) : std::string();
}

void
CheckFields(const HdContainerDataSourceHandle& prim, const std::string& where,
            const HdDataSourceLocator& at, const JsObject& want, bool exact)
{
    const std::set<std::string> got = Names(prim, at);
    if (exact) {
        Check(got == Keys(want), where + ": fields " + Join(got) +
                                     ", the source maps " + Join(Keys(want)));
    }
    for (const auto& [name, value] : want) {
        if (name == "bytes") {
            continue;
        }
        const HdDataSourceLocator locator = Nested(at, name);
        const auto sampled = HdSampledDataSource::Cast(
            HdContainerDataSource::Get(prim, locator));
        const VtValue have = sampled ? sampled->GetValue(0.0f) : VtValue();
        Check(Matches(have, value),
              where + ": " + locator.GetString() + " = " + Describe(have) +
                  ", the source says " + JsWriteToString(value));
    }
}

size_t
CheckMaterial(const HdContainerDataSourceHandle& prim, const std::string& path,
              const JsObject& want)
{
    size_t values = 0;
    std::set<std::string> groups;
    for (const JsValue& group : want.at("groups").GetJsArray()) {
        groups.insert(group.GetString());
    }
    const std::set<std::string> got = Names(prim, HdDataSourceLocator(kVrm));
    Check(got == groups,
          path + ": vrm groups " + Join(got) + ", the source gives " + Join(groups));

    for (const auto& [group, fields] : want.at("fields").GetJsObject()) {
        CheckFields(prim, path, HdDataSourceLocator(kVrm, TfToken(group)),
                    fields.GetJsObject(), /*exact=*/true);
        values += fields.GetJsObject().size();
    }

    const JsObject& textures = want.at("textures").GetJsObject();
    const HdDataSourceLocator textureInfo(kVrm, TfToken("textureInfo"));
    if (!textures.empty()) {
        const std::set<std::string> roles = Names(prim, textureInfo);
        Check(roles == Keys(textures), path + ": texture roles " + Join(roles) +
                                           ", the source has " + Join(Keys(textures)));
    }
    for (const auto& [role, fields] : textures) {
        const HdDataSourceLocator at = textureInfo.Append(TfToken(role));
        // A field the source does not state -- a transform it never gave --
        // reads as the schema's fallback, which the texture_info suite holds;
        // here only what the source says is compared.
        CheckFields(prim, path, at, fields.GetJsObject(), /*exact=*/false);
        values += fields.GetJsObject().size();

        std::string assetPath;
        const std::string bytes =
            ImageHex(prim, at.Append(TfToken("file")), &assetPath);
        Check(!bytes.empty(), path + ": " + role + "'s file " +
                                  (assetPath.empty() ? std::string("is absent")
                                                     : "@" + assetPath + "@") +
                                  " did not resolve and open");
        Check(bytes.empty() ||
                  bytes == fields.GetJsObject().at("bytes").GetString(),
              path + ": " + role + "'s file @" + assetPath +
                  "@ is not the source's image");
    }
    return values;
}

} // namespace

int
main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <model.vrm> <expected.json>\n", argv[0]);
        return 2;
    }
    std::ifstream stream(argv[2]);
    if (!stream) {
        std::fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    JsParseError error;
    const JsValue expected = JsParseStream(stream, &error);
    if (!expected.IsObject()) {
        std::fprintf(stderr, "%s: not a JSON object (line %u: %s)\n", argv[2],
                     error.line, error.reason.c_str());
        return 2;
    }
    const JsObject& materials = expected.GetJsObject().at("materials").GetJsObject();

    Session session;
    Open(session, argv[1]);

    // Every prim the scene index gives a `vrm` contribution: exactly the
    // source's materials, and nothing else.
    std::set<std::string> contributed;
    for (const SdfPath& path :
         HdSceneIndexPrimView(session.sceneIndex, SdfPath::AbsoluteRootPath())) {
        if (HdContainerDataSource::Get(session.sceneIndex->GetPrim(path).dataSource,
                                       HdDataSourceLocator(kVrm))) {
            contributed.insert(path.GetString());
        }
    }
    Check(contributed == Keys(materials),
          "prims with a vrm contribution " + Join(contributed) +
              ", the source's materials " + Join(Keys(materials)));

    size_t values = 0;
    for (const auto& [path, want] : materials) {
        const HdContainerDataSourceHandle prim = PrimData(session, path.c_str());
        if (!Check(prim != nullptr, path + ": not in the scene index")) {
            continue;
        }
        values += CheckMaterial(prim, path, want.GetJsObject());
    }

    if (failures) {
        std::fprintf(stderr, "vrmImaging_import: %d mismatch(es)\n", failures);
        return 1;
    }
    std::printf("vrmImaging_import: %zu materials, %zu source values read "
                "through Hydra\n",
                materials.size(), values);
    std::printf("vrmImaging_import: passed\n");
    return 0;
}
