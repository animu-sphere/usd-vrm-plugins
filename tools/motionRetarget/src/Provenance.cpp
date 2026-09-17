// SPDX-License-Identifier: Apache-2.0
#include "Provenance.h"

#include "motionRetargetBuildInfo.h"

#include "pxr/base/js/json.h"
#include "pxr/base/js/value.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

#if defined(_WIN32)
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace motionRetargetTool
{
namespace
{

// The same enumeration `tests/parity/exec_parity` reports, so a load report
// from the tool and one from the harness read alike.
std::vector<std::string>
LoadedModules()
{
    std::set<std::string> paths;
#if defined(_WIN32)
    const HANDLE snapshot =
        CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W entry;
        entry.dwSize = sizeof(entry);
        for (BOOL more = Module32FirstW(snapshot, &entry); more;
             more = Module32NextW(snapshot, &entry))
        {
            const int size =
                WideCharToMultiByte(CP_UTF8, 0, entry.szExePath, -1, nullptr, 0, nullptr, nullptr);
            if (size > 1)
            {
                std::string path(static_cast<std::size_t>(size), '\0');
                const int written = WideCharToMultiByte(
                    CP_UTF8, 0, entry.szExePath, -1, path.data(), size, nullptr, nullptr);
                if (written > 1)
                {
                    path.resize(static_cast<std::size_t>(written - 1));
                    paths.insert(std::move(path));
                }
            }
        }
        CloseHandle(snapshot);
    }
#elif defined(__APPLE__)
    for (uint32_t i = 0, n = _dyld_image_count(); i < n; ++i)
    {
        if (const char* name = _dyld_get_image_name(i))
        {
            paths.insert(name);
        }
    }
#else
    std::ifstream maps("/proc/self/maps");
    for (std::string line; std::getline(maps, line);)
    {
        const std::size_t slash = line.find('/');
        if (slash != std::string::npos)
        {
            paths.insert(line.substr(slash));
        }
    }
#endif
    return {paths.begin(), paths.end()};
}

} // namespace

std::string
VersionLine()
{
    return std::string("motion_retarget ") + MOTION_RETARGET_VERSION;
}

std::string
BuildInfoJson()
{
    JsObject info;
    info["tool"] = JsValue("motion_retarget");
    info["version"] = JsValue(MOTION_RETARGET_VERSION);
    info["gitCommit"] = JsValue(MOTION_RETARGET_GIT_COMMIT);
    info["buildOs"] = JsValue(MOTION_RETARGET_BUILD_OS);
    info["compiler"] = JsValue(MOTION_RETARGET_COMPILER);
    info["buildType"] = JsValue(MOTION_RETARGET_BUILD_TYPE);
    info["openusdVersion"] = JsValue(MOTION_RETARGET_OPENUSD_RELEASE);
    info["pxrVersion"] = JsValue(MOTION_RETARGET_PXR_VERSION);
    return JsWriteToString(JsValue(info));
}

bool
WriteLoadReport(const std::string& path, std::string* error)
{
    JsObject plugins;
    for (const PlugPluginPtr& plugin : PlugRegistry::GetInstance().GetAllPlugins())
    {
        if (plugin && plugin->IsLoaded())
        {
            plugins[plugin->GetName()] = JsValue(plugin->GetPath());
        }
    }
    JsArray modules;
    for (const std::string& module : LoadedModules())
    {
        modules.push_back(JsValue(module));
    }
    JsObject report;
    report["tool"] = JsValue("motion_retarget");
    report["version"] = JsValue(MOTION_RETARGET_VERSION);
    report["loaded_plugins"] = JsValue(plugins);
    report["loaded_modules"] = JsValue(modules);

    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    if (!out)
    {
        *error = "cannot write the load report '" + path + "'";
        return false;
    }
    out << JsWriteToString(JsValue(report)) << "\n";
    return static_cast<bool>(out);
}

} // namespace motionRetargetTool
