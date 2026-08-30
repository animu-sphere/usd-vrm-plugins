// SPDX-License-Identifier: Apache-2.0
#include "ProfileLocator.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <system_error>

#if defined(_WIN32)
#    include <windows.h>
#elif defined(__APPLE__)
#    include <mach-o/dyld.h>
#else
#    include <unistd.h>
#endif

namespace motionBvhTool
{
namespace
{

#if defined(_WIN32)
constexpr char kPathListSeparator = ';';
#else
constexpr char kPathListSeparator = ':';
#endif

// Where this executable is, or an empty path when the platform will not say.
//
// `argv[0]` is deliberately not consulted: it is whatever the caller wrote, and
// on every platform here a tool launched through `PATH` gets a bare name. The
// installed profile directory is derived from this, so a wrong answer would
// mean a conversion silently reading a different producer's profile — which is
// the one failure this whole path is shaped to prevent.
std::filesystem::path
ExecutableDirectory()
{
    std::error_code code;
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = ::GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size()) {
            buffer.resize(written);
            break;
        }
        // Truncated. The API reports the buffer size rather than the needed
        // one, so grow and ask again instead of trusting a path that fits.
        buffer.resize(buffer.size() * 2);
    }
    const std::filesystem::path self(buffer);
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    ::_NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (::_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    const std::filesystem::path self(buffer.data());
#else
    const std::filesystem::path self =
        std::filesystem::read_symlink("/proc/self/exe", code);
    if (code) {
        return {};
    }
#endif
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(self, code);
    const std::filesystem::path& executable = code ? self : resolved;
    return executable.parent_path();
}

void
AppendPathList(const char* value,
               std::vector<std::filesystem::path>* directories)
{
    if (value == nullptr) {
        return;
    }
    const std::string list(value);
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t end = list.find(kPathListSeparator, start);
        const std::string entry = list.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (!entry.empty()) {
            directories->emplace_back(entry);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
}

} // namespace

bool
ProfileRequestIsPath(const std::string& request)
{
    if (request.find('/') != std::string::npos
        || request.find('\\') != std::string::npos) {
        return true;
    }
    const std::filesystem::path candidate(request);
    const std::string extension = candidate.extension().string();
    return extension == ".yaml" || extension == ".yml";
}

std::vector<std::filesystem::path>
ProfileSearchPath(const std::vector<std::string>& extraDirs)
{
    std::vector<std::filesystem::path> directories;
    directories.reserve(extraDirs.size() + 4);
    for (const std::string& directory : extraDirs) {
        directories.emplace_back(directory);
    }
#if defined(_MSC_VER)
    // getenv is deprecated-by-warning under MSVC; the documented replacement
    // allocates, and this reads one variable once at startup.
    std::size_t length = 0;
    char* value = nullptr;
    if (::_dupenv_s(&value, &length, "USDVRM_MOTION_PROFILE_PATH") == 0) {
        AppendPathList(value, &directories);
        std::free(value);
    }
#else
    AppendPathList(std::getenv("USDVRM_MOTION_PROFILE_PATH"), &directories);
#endif

    const std::filesystem::path executableDir = ExecutableDirectory();
    if (!executableDir.empty()) {
        // <prefix>/bin/<exe> -> <prefix>/share/... : a `cmake --install`
        // prefix, and a member archive unpacked on its own.
        directories.push_back(executableDir.parent_path() / "share"
                              / "usd-vrm-plugins" / "profiles" / "motion");
        // <prefix>/tools/<member>/bin/<exe> -> <prefix>/share/... : an
        // installed product. The two installed layouts agree about where the
        // data is relative to the prefix and disagree about how deep the tool
        // sits inside it, so each needs its own rule.
        const std::filesystem::path prefixFromToolMember =
            executableDir.parent_path().parent_path().parent_path();
        directories.push_back(prefixFromToolMember / "share"
                              / "usd-vrm-plugins" / "profiles" / "motion");
        // tools/<member>/bin/<exe> -> the repository root's profiles/motion.
        directories.push_back(prefixFromToolMember / "profiles" / "motion");
    }
    return directories;
}

bool
ResolveProfilePath(const std::string& request,
                   const std::vector<std::string>& extraDirs,
                   std::filesystem::path* path, std::string* error)
{
    if (ProfileRequestIsPath(request)) {
        std::error_code code;
        if (!std::filesystem::is_regular_file(request, code)) {
            *error = "no profile file at '" + request + "'";
            return false;
        }
        *path = std::filesystem::path(request);
        return true;
    }

    const std::vector<std::filesystem::path> directories =
        ProfileSearchPath(extraDirs);
    const std::string fileName = request + ".yaml";
    for (const std::filesystem::path& directory : directories) {
        std::error_code code;
        const std::filesystem::path candidate = directory / fileName;
        if (std::filesystem::is_regular_file(candidate, code)) {
            *path = candidate;
            return true;
        }
    }

    *error = "no profile '" + request + "' was found. Looked for '" + fileName
        + "' in:";
    for (const std::filesystem::path& directory : directories) {
        *error += "\n  " + directory.string();
    }
    if (directories.empty()) {
        *error += "\n  (nowhere: pass --profile-dir, set "
                  "USDVRM_MOTION_PROFILE_PATH, or name a file)";
    }
    return false;
}

} // namespace motionBvhTool
