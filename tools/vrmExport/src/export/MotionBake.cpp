// SPDX-License-Identifier: Apache-2.0
#include "MotionBake.h"

#include "pxr/base/arch/fileSystem.h"
#include "pxr/base/arch/systemInfo.h"
#include "pxr/base/tf/fileUtils.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/stringUtils.h"

#include <cerrno>
#include <cstdlib>
#include <vector>

#if defined(ARCH_OS_WINDOWS)
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace vrmExport
{
namespace
{
std::string
FindBakeTool()
{
#if defined(ARCH_OS_WINDOWS)
    const std::string name = "motion_retarget.exe";
    const char* separator = ";";
#else
    const std::string name = "motion_retarget";
    const char* separator = ":";
#endif
    const std::string bin = TfGetPathName(ArchGetExecutablePath());
    // Plain cmake --install and OpenStrata's installed product, respectively.
    std::vector<std::string> directories = {bin,
                                            TfStringCatPaths(bin, "../../motion_retarget/bin")};
    if (const char* path = std::getenv("PATH"))
    {
        for (const std::string& dir : TfStringSplit(path, separator))
        {
            if (!dir.empty())
                directories.push_back(dir);
        }
    }
    for (const std::string& dir : directories)
    {
        const std::string candidate = TfStringCatPaths(dir, name);
        if (TfIsFile(candidate))
            return TfAbsPath(candidate);
    }
    return {};
}

#if defined(ARCH_OS_WINDOWS)
// _wspawnv passes a command line to the CRT. Quote each argument using the
// CRT's backslash-before-quote rule, including trailing backslashes.
std::wstring
QuoteArgument(const std::string& argument)
{
    const std::wstring wide = ArchWindowsUtf8ToUtf16(argument);
    std::wstring quoted = L"\"";
    std::size_t slashes = 0;
    for (wchar_t ch : wide)
    {
        if (ch == L'\\')
            ++slashes;
        else
        {
            quoted.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            quoted += ch;
            slashes = 0;
        }
    }
    quoted.append(slashes * 2, L'\\');
    return quoted + L'"';
}
#endif
} // namespace

bool
BakeMotion(const std::string& avatar, const std::string& motion, const std::string& output,
           std::string* error)
{
    const std::string tool = FindBakeTool();
    if (tool.empty())
    {
        *error = "--motion needs motion_retarget; install the complete product or place "
                 "motion_retarget beside vrm_export or on PATH";
        return false;
    }
    const std::vector<std::string> arguments = {tool,          "--avatar",        TfAbsPath(avatar),
                                                "--animation", TfAbsPath(motion), "--output",
                                                output,        "--quiet"};
    int exitCode = -1;
#if defined(ARCH_OS_WINDOWS)
    std::vector<std::wstring> quoted;
    for (const std::string& argument : arguments)
        quoted.push_back(QuoteArgument(argument));
    std::vector<const wchar_t*> argv;
    for (const std::wstring& argument : quoted)
        argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    const intptr_t status = _wspawnv(_P_WAIT, ArchWindowsUtf8ToUtf16(tool).c_str(), argv.data());
    if (status != -1)
        exitCode = static_cast<int>(status);
#else
    std::vector<char*> argv;
    for (const std::string& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t child;
    const int spawned = posix_spawn(&child, tool.c_str(), nullptr, nullptr, argv.data(), environ);
    if (spawned == 0)
    {
        int status;
        pid_t waited;
        do
        {
            waited = waitpid(child, &status, 0);
        } while (waited == -1 && errno == EINTR);
        if (waited == child && WIFEXITED(status))
            exitCode = WEXITSTATUS(status);
    }
#endif
    if (exitCode != 0)
    {
        *error = "motion_retarget could not bake '" + motion + "' onto '" + avatar + "' (exit " +
                 std::to_string(exitCode) +
                 "); see its refusal above and check the clip and humanoid rig";
        return false;
    }
    return true;
}
} // namespace vrmExport
