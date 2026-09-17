// SPDX-License-Identifier: Apache-2.0
//
// What this executable is, and what it loaded.
//
// Two questions a user of the installed product cannot answer from outside the
// process. `--build-info` says which build answered: the version, the commit,
// the compiler and the OpenUSD it was built against. `--load-report` says which
// files answered: every plugin the registry loaded and every module the process
// mapped, by the path the loader resolved. The second is the one that settles a
// Windows DLL-discovery question -- "which vrmContainer.dll did it load, and
// from where?" -- without a debugger, and it is what the release lane's
// artifact-only smoke reads to prove the tool loads nothing from a build tree
// (the OpenExec plan's P0-3).
#pragma once

#include <string>

namespace motionRetargetTool
{

// "motion_retarget <version>".
std::string VersionLine();

// One JSON object, stable keys, no timestamp: the same build prints the same
// bytes, so the stamp cannot break packaging reproducibility.
std::string BuildInfoJson();

// Writes {"tool", "version", "loaded_plugins": {name: path},
// "loaded_modules": [path, ...]} to `path`. Modules are sorted and unique.
// Returns false, with `error` set, when the file cannot be written.
bool WriteLoadReport(const std::string& path, std::string* error);

} // namespace motionRetargetTool
