// SPDX-License-Identifier: Apache-2.0
//
// Turning `--profile <id>` into a file on disk.
//
// The plan writes the command with an **id** rather than a path
// (roadmap/recorded-motion-sources.md §3.1), and an id is not a location, so
// something has to say where profiles live. That is this file, and it is a
// tool's job rather than a library's for the reason the whole layer split
// exists: `motionSource` reads a profile it is handed and is forbidden to know
// which producers ship one, so a library that searched a directory would be a
// library with an opinion about who exists.
//
// Three decisions.
//
// **An id is looked up; a path is used.** A request naming a directory
// separator, or ending in `.yaml` or `.yml`, is a file and is opened as given.
// Anything else is an id and becomes `<dir>/<id>.yaml` in each search
// directory. The two cannot be confused by accident — a profile id is
// `<producer>-<format>-<preset>-v<N>` and carries neither — and keeping one
// flag for both is what lets a profile under review and a shipped one be named
// the same way.
//
// **The id a file states must be the id that was asked for.** When the request
// was an id, the loaded profile's `id` is compared against it and a
// disagreement is refused. A profile file renamed on disk would otherwise let
// a conversion record a profile id it never read, and "which profile was used"
// is the one reproducible fact a conversion is supposed to carry. A request
// that was a path skips the check: naming a file is naming it, and a file under
// review has every reason not to be called after its id yet.
//
// **The installed location is found relative to the executable, not relative to
// the working directory.** WORKSPACE.md §5 ships the profiles as package data
// beside the tools precisely so that an artifact-only smoke test can pass —
// "a converter with no profile available refuses every file it is given" — and
// a search rooted at the caller's cwd would make that depend on where the smoke
// test happened to be run from. The search order, first hit wins:
//
//   1. every `--profile-dir`, in the order given
//   2. `USDVRM_MOTION_PROFILE_PATH`, a list in the platform's PATH separator
//   3. `<exe>/../share/usd-vrm-plugins/profiles/motion` — an install prefix
//   4. `<exe>/../../../profiles/motion` — this repository, whose tools stage
//      their executables in `tools/<member>/bin/`
//
// The fourth is a convenience and is stated rather than hidden: a build tree
// that found no profile would send whoever ran it looking for a packaging bug
// that is not there.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace motionBvhTool
{

// Whether `request` names a file rather than an id. See the header note.
bool ProfileRequestIsPath(const std::string& request);

// Every directory that would be searched for an id, in order. Directories that
// do not exist are kept: a refusal that lists only the paths that happened to
// be there tells whoever reads it nothing about where the tool looked.
std::vector<std::filesystem::path> ProfileSearchPath(
    const std::vector<std::string>& extraDirs);

// Resolves `request` to a file. On failure `error` says what was looked for and
// where — the whole search path, one directory per line — because "profile not
// found" with no list is the single least actionable thing this tool could say.
bool ResolveProfilePath(const std::string& request,
                        const std::vector<std::string>& extraDirs,
                        std::filesystem::path* path, std::string* error);

} // namespace motionBvhTool
