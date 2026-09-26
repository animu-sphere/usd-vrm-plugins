#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Hold the CMake build graph to the workspace's dependency boundary.

`ost graph validate` checks the topology the descriptors declare. This checks
what the build actually does, statically, with no configure and no build, so it
runs on every lane and in a few seconds:

* **The repository boundary.** usd-motion-plugins is reached as an installed
  package and never as a source tree: no `add_subdirectory()` out of this
  repository, no `FetchContent` or `ExternalProject`, no `include()` of another
  repository's file.
* **Nothing left behind** (MIG-5). No identity WORKSPACE.md §9.1 sent to
  usd-motion-plugins or motion-connectors comes back, under the name it had
  here or the one it has there: no member directory, no target or target
  prefix (`motionCore_tests`), no file under `adapters/` or `profiles/motion/`,
  and no source that opens one of their namespaces. A second `motionRetarget`
  in this tree is the copy MIG-1..MIG-4 deleted, coming back.
* **The root orchestrates.** The root CMakeLists.txt resolves no consumed
  package: each member resolves what it links (cmake/UsdVrmConsumedPackage.cmake),
  so the root does not make every member's configure require every package.
* **Each member's edges are allowed.** `ALLOWED` below is WORKSPACE.md §2 for
  consumed packages: the VRM importer links no motion package at all, the
  `.vrma` importer does not know a target rig (no `vrmRig`, no
  `motionRetarget`), `vrmRig` builds on `motionCore` alone.
* **Each member's edges are real.** What a member resolves, what it links and
  what its sources include are one set. A package resolved and not linked is a
  configure-time requirement for nothing; linked and not included is the edge
  `execVrm` carried on `motionRecording` for a release after its last use; and
  included but not linked is borrowed from another member's link line, which
  holds in the composed build and fails the standalone one.
* **The descriptor says the same.** A member's `requires.libraries` names
  exactly the consumed packages its CMake resolves, so `ost` materializes what
  the build uses and nothing else.

With `--motion-include DIR` (repeatable), it also reads an installed
usd-motion-plugins `include/` tree and fails on any code in it that knows VRM:
a VRM header, a `Vrm*`/`UsdVrm*` type, a `vrmRig`/`vrmSchema` name. The
plain-CMake lane passes the prefix it just installed.

    python scripts/check_cmake_boundaries.py
    python scripts/check_cmake_boundaries.py --motion-include <prefix>/include
    python scripts/check_cmake_boundaries.py --selftest

Exit 0 when every rule holds, 1 on a violation, 2 when the tree is not one this
script can read (a member it has no row for is that, on purpose: a new member
is placed in `ALLOWED` by a person, not by a default).
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# usd-motion-plugins' library identities -- every package that repository
# installs a `<name>Config.cmake` for -- and its bundle. None may be built here.
CONSUMED = frozenset({
    "motionCore", "motionSampling", "motionRecording", "motionRetarget",
    "motionUsd", "motionSource", "motionBvh",
})

MOTION_PLUGINS = "usd-motion-plugins"
CONNECTORS = "motion-connectors"

# Every identity WORKSPACE.md §9.1 sends to another repository, under every
# name it has had -- the one it had here and the one it has there -- with the
# repository that owns it now (MIG-5: nothing left behind). None may be a
# member, a target or a target's prefix here: `motionCore_tests` is a copy of
# motionCore's suite as surely as `add_library(motionCore)` is a copy of it.
DEPARTED: dict[str, str] = {
    **{name: MOTION_PLUGINS for name in CONSUMED},
    **{name: MOTION_PLUGINS for name in (
        # The bundle, the library `motionRuntime` became two of, the tools,
        # and the two identities reserved there that never existed here.
        "execMotion", "motionRuntime", "motionCapture", "motion_capture",
        "motion_record", "motion_bvh_inspect", "motion_bvh_convert",
        "motion_convert", "motionFbx", "usdBvhFileFormat",
        # What stayed of it is `vrmRig`, so the old name back is the generic
        # half back.
        "vrmRetarget")},
    **{name: CONNECTORS for name in (
        "liveTransport", "osc", "motionTracking",
        "vrmAdapterVmc", "vrmAdapterMocopi", "vrmAdapterVrchatOsc",
        "vrmAdapterArdy", "vmc_record", "mocopi_record", "vrchat_osc_record",
        "motionConnectorTransport", "motionConnectorOsc",
        "motionConnectorTracking", "motionConnectorVmc",
        "motionConnectorMocopi", "motionConnectorVrchatOsc")},
}
# Trees that held nothing but departed code. Any file in one is a copy.
DEPARTED_TREES: dict[str, str] = {
    "adapters": CONNECTORS,
    "profiles/motion": MOTION_PLUGINS,
}

# The member directory `motion_retarget` lives in shares the library's name.
DEPARTED_DIR_EXCEPTIONS = {("tools", "motionRetarget")}

# The namespaces departed code was written in, here and there. Code here uses
# them (`openstrata::motion::MotionPose`) and never opens one: a definition
# inside one is an implementation of the shared packages, which is theirs.
_DEPARTED_NAMESPACE = re.compile(
    r"\bnamespace\s+(openstrata|motion|motionRuntime|motionSource|motionBvh|"
    r"motionTracking|vrmRetarget|liveTransport|osc|vrmAdapter\w*)\b"
    r"(?:\s*::\s*\w+)*\s*\{")

# The consumed packages each member may link (WORKSPACE.md §2). A member missing
# from this table is an error: its row is a decision, not a default.
ALLOWED: dict[str, frozenset[str]] = {
    "libs/vrmContainer": frozenset(),
    "libs/vrmRig": frozenset({"motionCore"}),
    "plugins/vrmSchema": frozenset(),
    "plugins/usdVrmFileFormat": frozenset(),
    "plugins/usdVrmPackageResolver": frozenset(),
    "plugins/usdVrmaFileFormat": frozenset({"motionCore", "motionUsd"}),
    "plugins/execVrm": frozenset({"motionCore", "motionRetarget"}),
    "tools/motionRetarget": frozenset(
        {"motionCore", "motionRetarget", "motionSampling", "motionUsd"}),
    "tests/parity": frozenset({"motionCore", "motionRetarget", "motionSampling"}),
    "tools/vrmExport": frozenset(),
    "plugins/vrmImaging": frozenset(),
}

# Workspace libraries a member may not reach, and why. The importers read a
# file without knowing which avatar it will drive, so VRM rig semantics are not
# theirs. `vrm_export` reaches a `.vrm` through the plugin registry and links
# OpenUSD alone (VRM_EXPORT_POLICY.md §4), so what the importer authors reaches
# it as data and never as code.
FORBIDDEN_WORKSPACE: dict[str, tuple[frozenset[str], str]] = {
    "plugins/usdVrmFileFormat": (
        frozenset({"vrmRig"}),
        "this member reads its input without knowing the target rig"),
    "plugins/usdVrmaFileFormat": (
        frozenset({"vrmRig"}),
        "this member reads its input without knowing the target rig"),
    "tools/vrmExport": (
        frozenset({"vrmRig", "vrmContainer", "vrmSchema"}),
        "vrm_export opens a .vrm through the plugin registry and links no VRM identity"),
    # The stage is the boundary (VRM_IMAGING_POLICY.md §12): what the importer
    # authors reaches the adapter as schema data, never as code.
    "plugins/vrmImaging": (
        frozenset({"vrmRig", "vrmContainer", "vrmSchema"}),
        "vrmImaging reads the composed stage's schemas and links no VRM identity"),
}

MEMBER_GLOBS = ("libs/*/CMakeLists.txt", "plugins/*/CMakeLists.txt",
                "tools/*/CMakeLists.txt")
EXTRA_MEMBERS = ("tests/parity",)
DESCRIPTOR_GLOBS = ("openstrata.plugin.yaml", "openstrata.library.yaml",
                    "openstrata.tool.yaml")
SOURCE_SUFFIXES = {".h", ".hh", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx"}
# Build products and materialized artifacts live inside member directories;
# none of it is source.
SKIP_PARTS = {"build", ".strata", "__pycache__", "lib", "bin", "dist",
              "third_party", "corpus"}

_CMAKE_BRACKET_COMMENT = re.compile(r"#\[(=*)\[.*?\]\1\]", re.DOTALL)
_CPP_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
_CPP_LINE_COMMENT = re.compile(r"//[^\n]*")


def cmake_code(text: str) -> str:
    """CMake with comments removed; a `#` inside a quoted argument is kept."""
    text = _CMAKE_BRACKET_COMMENT.sub("", text)
    lines = []
    for line in text.splitlines():
        in_quote = False
        escaped = False
        cut = len(line)
        for i, ch in enumerate(line):
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif ch == '"':
                in_quote = not in_quote
            elif ch == "#" and not in_quote:
                cut = i
                break
        lines.append(line[:cut])
    return "\n".join(lines)


def cpp_code(text: str) -> str:
    return _CPP_LINE_COMMENT.sub("", _CPP_BLOCK_COMMENT.sub("", text))


def _files(root: Path, keep) -> list[Path]:
    out = []
    for path in root.rglob("*"):
        rel = path.relative_to(root).parts
        if any(part in SKIP_PARTS for part in rel[:-1]):
            continue
        if path.is_file() and keep(path):
            out.append(path)
    return sorted(out)


def cmake_files(root: Path) -> list[Path]:
    return _files(root, lambda p: p.name == "CMakeLists.txt" or p.suffix == ".cmake")


def source_files(root: Path) -> list[Path]:
    return _files(root, lambda p: p.suffix.lower() in SOURCE_SUFFIXES)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


_CALL = r"\b{name}\s*\(([^)]*)\)"


def call_args(code: str, name: str) -> list[list[str]]:
    return [a.split() for a in re.findall(_CALL.format(name=name), code, re.IGNORECASE)]


def _foreach_items(code: str) -> dict[str, set[str]]:
    """`foreach(var IN ITEMS a b)` / `foreach(var a b)`: what `${var}` can be."""
    items: dict[str, set[str]] = {}
    for args in call_args(code, "foreach"):
        if len(args) < 2:
            continue
        var, rest = args[0], args[1:]
        if rest[:2] == ["IN", "ITEMS"]:
            rest = rest[2:]
        elif rest[0] in ("IN", "RANGE"):
            continue
        items.setdefault(var, set()).update(r.strip('"') for r in rest)
    return items


def resolved_packages(code: str) -> set[str]:
    found = set()
    loops = _foreach_items(code)
    for args in call_args(code, "usdvrm_consume_package"):
        found.update(a.strip('"') for a in args)
    for args in call_args(code, "find_package"):
        if not args:
            continue
        name = args[0].strip('"')
        var = re.fullmatch(r"\$\{(\w+)\}", name)
        if var:
            # A package named through a loop variable is every item the loop
            # can hand it -- which is how the root once resolved all five.
            found.update(loops.get(var.group(1), set()))
        else:
            found.add(name)
    return found


def linked_packages(code: str) -> set[str]:
    return {m for m in re.findall(r"\b(\w+)::\1\b", code)}


def included_packages(text: str) -> set[str]:
    return set(re.findall(r"#\s*include\s*[<\"](\w+)/", cpp_code(text)))


def descriptor_libraries(path: Path) -> set[str]:
    """`requires.libraries[].id` of one descriptor, without a YAML parser."""
    ids: set[str] = set()
    section = None
    for line in read(path).splitlines():
        stripped = line.split("#", 1)[0].rstrip()
        if not stripped.strip():
            continue
        key = stripped.strip()
        if key.endswith(":") and not key.startswith("-") and " " not in key:
            if key in ("libraries:", "bundles:", "tools:", "capabilities:"):
                section = key[:-1]
                continue
            if len(line) - len(line.lstrip()) <= 2:
                section = None
        if section == "libraries" and key.startswith("- id:"):
            ids.add(key.split(":", 1)[1].strip())
    return ids


class Report:
    def __init__(self) -> None:
        self.errors: list[str] = []
        self.setup: list[str] = []

    def error(self, msg: str) -> None:
        self.errors.append(msg)


def members(root: Path) -> list[str]:
    found = []
    for pattern in MEMBER_GLOBS:
        for cml in root.glob(pattern):
            found.append(cml.parent.relative_to(root).as_posix())
    for extra in EXTRA_MEMBERS:
        if (root / extra / "CMakeLists.txt").exists():
            found.append(extra)
    return sorted(found)


# This repository's own CMake: the root file and the trees it adds. Anything
# else under the checkout -- a build tree, or the usd-motion-plugins checkout the
# plain-CMake lane builds its packages from -- is not this repository's build.
SOURCE_ROOTS = ("cmake", "libs", "plugins", "tools", "tests")


def repository_cmake_files(root: Path) -> list[Path]:
    files = [root / "CMakeLists.txt"] if (root / "CMakeLists.txt").exists() else []
    for name in SOURCE_ROOTS:
        if (root / name).is_dir():
            files += cmake_files(root / name)
    return files


def check_repository_boundary(root: Path, report: Report) -> None:
    for path in repository_cmake_files(root):
        rel = path.relative_to(root).as_posix()
        code = cmake_code(read(path))
        for word in ("FetchContent_Declare", "FetchContent_MakeAvailable",
                     "FetchContent_Populate", "ExternalProject_Add"):
            if re.search(rf"\b{word}\s*\(", code, re.IGNORECASE):
                report.error(
                    f"{rel}: {word}() -- a dependency is an installed package "
                    f"found on CMAKE_PREFIX_PATH, never a fetched source tree")
        for args in call_args(code, "add_subdirectory"):
            if not args:
                continue
            target = args[0].strip('"')
            if "${" in target and not target.startswith("${CMAKE_CURRENT_SOURCE_DIR}"):
                # The root's bundle loop: "plugins/${_bundle}". Resolved below.
                target = target.split("${", 1)[0]
            target = target.replace("${CMAKE_CURRENT_SOURCE_DIR}", ".")
            resolved = (path.parent / target).resolve()
            if not resolved.is_relative_to(root.resolve()):
                report.error(
                    f"{rel}: add_subdirectory({args[0]}) leaves the repository; "
                    f"another repository is consumed as its install, not its source")
        for args in call_args(code, "include"):
            if args and "usd-motion-plugins" in args[0]:
                report.error(f"{rel}: include({args[0]}) reads another repository's file")
        for args in call_args(code, "add_library") + call_args(code, "add_executable"):
            if not args:
                continue
            name = departed_prefix(args[0])
            if name:
                report.error(
                    f"{rel}: builds '{args[0]}', under an identity {DEPARTED[name]} "
                    f"publishes ('{name}') -- one library, one repository")
    for kind in ("libs", "plugins", "tools"):
        if not (root / kind).is_dir():
            continue
        for child in sorted((root / kind).iterdir()):
            if (kind, child.name) in DEPARTED_DIR_EXCEPTIONS:
                continue
            if child.is_dir() and child.name in DEPARTED and _files(child, lambda p: True):
                report.error(
                    f"{kind}/{child.name}: a member under {DEPARTED[child.name]}' "
                    f"identity '{child.name}'")
    for tree, owner in DEPARTED_TREES.items():
        if (root / tree).is_dir() and _files(root / tree, lambda p: True):
            report.error(f"{tree}/: a tree that left for {owner}, back with files in it")
    for name in SOURCE_ROOTS:
        if not (root / name).is_dir():
            continue
        for path in source_files(root / name):
            code = cpp_code(read(path))
            for m in _DEPARTED_NAMESPACE.finditer(code):
                n = code.count("\n", 0, m.start()) + 1
                report.error(
                    f"{path.relative_to(root).as_posix()}:{n}: opens namespace "
                    f"'{m.group(1)}' -- code there is the shared packages', "
                    f"used here and never written")


def departed_prefix(target: str) -> str | None:
    """The departed identity a target is named for: itself, or `<id>_...`."""
    target = target.strip('"')
    for name in DEPARTED:
        if target == name or target.startswith(name + "_"):
            return name
    return None


def check_root(root: Path, report: Report) -> None:
    cml = root / "CMakeLists.txt"
    if not cml.exists():
        report.setup.append("no root CMakeLists.txt")
        return
    resolved = resolved_packages(cmake_code(read(cml))) & CONSUMED
    for pkg in sorted(resolved):
        report.error(
            f"CMakeLists.txt: the root resolves '{pkg}'; the member that links it "
            f"resolves it (cmake/UsdVrmConsumedPackage.cmake)")


def check_member(root: Path, member: str, report: Report) -> None:
    base = root / member
    if member not in ALLOWED:
        report.setup.append(
            f"{member}: no row in check_cmake_boundaries.py's ALLOWED -- a new "
            f"member's consumed packages are placed by hand (WORKSPACE.md §2)")
        return
    allowed = ALLOWED[member]

    code = "\n".join(cmake_code(read(p)) for p in cmake_files(base))
    resolved = resolved_packages(code) & CONSUMED
    linked = linked_packages(code) & CONSUMED
    sources = [read(p) for p in source_files(base)]
    included_all: set[str] = set()
    for text in sources:
        included_all |= included_packages(text)
    included = included_all & CONSUMED

    for pkg in sorted((resolved | linked | included) - allowed):
        report.error(
            f"{member}: reaches '{pkg}', which WORKSPACE.md section 2 does not "
            f"allow it (allowed: {', '.join(sorted(allowed)) or 'no consumed package'})")
    for pkg in sorted(resolved - linked):
        report.error(f"{member}: resolves '{pkg}' and links nothing from it")
    for pkg in sorted(linked - resolved):
        report.error(
            f"{member}: links '{pkg}' without resolving it -- the composed build "
            f"lends it, and a standalone configure of {member} fails")
    for pkg in sorted(linked - included):
        report.error(f"{member}: links '{pkg}' and no source includes a {pkg}/ header")
    for pkg in sorted(included - linked):
        report.error(
            f"{member}: includes {pkg}/ headers without linking '{pkg}' -- the "
            f"edge is borrowed from another target's link line")

    forbidden, reason = FORBIDDEN_WORKSPACE.get(member, (frozenset(), ""))
    reached = (resolved_packages(code) | linked_packages(code) | included_all)
    for name in sorted(forbidden & reached):
        report.error(
            f"{member}: reaches the workspace library '{name}' -- {reason}")

    for descriptor in DESCRIPTOR_GLOBS:
        path = base / descriptor
        if path.exists():
            declared = descriptor_libraries(path) & CONSUMED
            for pkg in sorted(declared - resolved):
                report.error(
                    f"{member}/{descriptor}: pins '{pkg}', which its CMake never "
                    f"resolves -- `ost` materializes a package nothing uses")
            for pkg in sorted(resolved - declared):
                report.error(
                    f"{member}/{descriptor}: its CMake resolves '{pkg}', which "
                    f"requires.libraries does not declare")


_VRM_KNOWLEDGE = re.compile(
    r"#\s*include\s*[<\"](?:vrm\w*|usdVrm\w*|execVrm)/|"
    r"\bUsdVrm\w*|\bVrm[A-Z]\w*|\bvrm(?:Rig|Schema|Container)\b")


def check_motion_include(include_root: Path, report: Report) -> int:
    """Read one include/ tree's consumed-package headers; answer how many.

    Only the `<package>/` directories of consumed packages are read. A target's
    INTERFACE_INCLUDE_DIRECTORIES, read through `$<TARGET_PROPERTY>`, is
    transitive, so the root build hands over OpenUSD's and Python's include
    directories beside the package's own -- not ours to hold to this rule.
    """
    if not include_root.is_dir():
        report.setup.append(f"--motion-include {include_root}: not a directory")
        return 0
    headers = [p for p in include_root.rglob("*")
               if p.is_file() and p.suffix.lower() in SOURCE_SUFFIXES
               and p.relative_to(include_root).parts[0] in CONSUMED]
    for path in headers:
        for n, line in enumerate(cpp_code(read(path)).splitlines(), 1):
            if _VRM_KNOWLEDGE.search(line):
                report.error(
                    f"{path}:{n}: a consumed usd-motion-plugins header knows VRM "
                    f"({line.strip()})")
    return len(headers)


def run(root: Path, motion_includes: list[Path]) -> Report:
    report = Report()
    check_repository_boundary(root, report)
    check_root(root, report)
    for member in members(root):
        check_member(root, member, report)
    if motion_includes:
        check_motion_includes(motion_includes, report)
    return report


def check_motion_includes(include_roots: list[Path], report: Report) -> None:
    if not sum(check_motion_include(d, report) for d in include_roots):
        report.setup.append(
            "--motion-include: no header of any consumed package under "
            f"{', '.join(str(d) for d in include_roots)}, so nothing was checked")


# ---------------------------------------------------------------------------
# Self-test: every rule above, made to fire by a tree built for it. A rule that
# can no longer fail is found here rather than by the next edge it lets in.
# ---------------------------------------------------------------------------

_CONSUME = 'include("${{CMAKE_CURRENT_SOURCE_DIR}}/../../cmake/UsdVrmConsumedPackage.cmake")\nusdvrm_consume_package({pkgs})\n'


def _write(root: Path, rel: str, text: str) -> None:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _good_tree(root: Path) -> None:
    _write(root, "CMakeLists.txt",
           'add_subdirectory("libs/vrmRig")\n'
           '# find_package(motionRecording REQUIRED CONFIG) in a comment is fine\n')
    for member in ALLOWED:
        _write(root, f"{member}/CMakeLists.txt", "project(x)\n")
    _write(root, "libs/vrmRig/CMakeLists.txt",
           _CONSUME.format(pkgs="motionCore") +
           "target_link_libraries(vrmRig PUBLIC motionCore::motionCore)\n")
    _write(root, "libs/vrmRig/include/vrmRig/A.h", "#include <motionCore/MotionPose.h>\n")
    _write(root, "libs/vrmRig/openstrata.library.yaml",
           "requires:\n  libraries:\n    - id: motionCore\n      version: x\n")
    _write(root, "plugins/usdVrmaFileFormat/CMakeLists.txt",
           _CONSUME.format(pkgs="motionCore") +
           "target_link_libraries(P PRIVATE vrmContainer::vrmContainer motionCore::motionCore)\n")
    _write(root, "plugins/usdVrmaFileFormat/src/D.h",
           "// #include <vrmRig/RequiredBones.h> is only a comment\n"
           "#include <motionCore/MotionPose.h>\n")
    _write(root, "plugins/usdVrmaFileFormat/openstrata.plugin.yaml",
           "requires:\n  libraries:\n    - id: motionCore\n  bundles:\n"
           "    - id: motionRecording\n")


CASES: list[tuple[str, dict[str, str], str]] = [
    ("the good tree passes", {}, ""),
    ("root resolves a consumed package",
     {"CMakeLists.txt": "find_package(motionRecording REQUIRED CONFIG)\n"},
     "the root resolves 'motionRecording'"),
    ("root resolves the stack through a loop",
     {"CMakeLists.txt":
      "foreach(_c IN ITEMS motionCore motionUsd)\n"
      "    if(NOT TARGET ${_c}::${_c})\n"
      "        find_package(${_c} REQUIRED CONFIG)\n"
      "    endif()\nendforeach()\n"},
     "the root resolves 'motionUsd'"),
    ("a fetched source tree",
     {"libs/vrmRig/cmake/x.cmake": "FetchContent_Declare(m GIT_REPOSITORY u)\n"},
     "FetchContent_Declare()"),
    ("a sibling source tree",
     {"CMakeLists.txt": 'add_subdirectory("../usd-motion-plugins" m)\n'},
     "leaves the repository"),
    ("a copied identity",
     {"libs/motionCore/CMakeLists.txt": "add_library(motionCore STATIC a.cpp)\n"},
     "builds 'motionCore'"),
    ("a connector library back under its old name",
     {"libs/liveTransport/CMakeLists.txt": "add_library(liveTransport STATIC a.cpp)\n"},
     "an identity motion-connectors publishes ('liveTransport')"),
    ("a departed member directory, whatever it builds",
     {"libs/motionRuntime/README.md": "kept for reference\n"},
     "libs/motionRuntime: a member under usd-motion-plugins' identity"),
    ("a departed suite under a member that stays",
     {"plugins/execVrm/tests/CMakeLists.txt":
      "add_executable(execMotion_blend_tests t.cpp)\n"},
     "builds 'execMotion_blend_tests'"),
    ("a departed tool",
     {"tools/motionRetarget/tests/CMakeLists.txt":
      "add_executable(mocopi_record m.cpp)\n"},
     "an identity motion-connectors publishes ('mocopi_record')"),
    ("a file under adapters/",
     {"adapters/vmc/src/Packet.cpp": "int x;\n"},
     "adapters/: a tree that left for motion-connectors"),
    ("a producer profile",
     {"profiles/motion/mocopi.json": "{}\n"},
     "profiles/motion/: a tree that left for usd-motion-plugins"),
    ("a source that writes the shared core",
     {"libs/vrmRig/src/Slerp.cpp": "namespace openstrata::motion\n{\nint f();\n}\n"},
     "libs/vrmRig/src/Slerp.cpp:1: opens namespace 'openstrata'"),
    ("a source that writes the old core",
     {"tests/parity/src/pose.h": "// ok\nnamespace motion {\nstruct P;\n}\n"},
     "tests/parity/src/pose.h:2: opens namespace 'motion'"),
    ("using the shared namespaces is not writing them",
     {"libs/vrmRig/src/Use.cpp":
      "namespace om = openstrata::motion;\nusing namespace openstrata::motion;\n"
      "// namespace motion { in a comment\nnamespace vrmRig { int motion_capture; }\n"},
     ""),
    ("a name that only begins like a departed one",
     {"tools/motionRetarget/CMakeLists.txt":
      "add_executable(motion_retarget m.cpp)\nadd_executable(oscillator o.cpp)\n"},
     ""),
    ("the VRM importer links a motion package",
     {"plugins/usdVrmFileFormat/CMakeLists.txt":
      _CONSUME.format(pkgs="motionRecording") +
      "target_link_libraries(P PRIVATE motionRecording::motionRecording)\n",
      "plugins/usdVrmFileFormat/src/a.cpp": "#include <motionRecording/CaptureTrace.h>\n"},
     "plugins/usdVrmFileFormat: reaches 'motionRecording'"),
    ("the .vrma importer knows the target rig",
     {"plugins/usdVrmaFileFormat/src/r.cpp": "#include <vrmRig/RequiredBones.h>\n"},
     "reaches the workspace library 'vrmRig'"),
    ("vrm_export links the GLB reader",
     {"tools/vrmExport/src/export/Glb.cpp": "#include <vrmContainer/GlbContainer.h>\n"},
     "tools/vrmExport: reaches the workspace library 'vrmContainer' -- vrm_export opens"),
    ("the .vrma importer reaches the retarget",
     {"plugins/usdVrmaFileFormat/src/r.cpp": "#include <motionRetarget/RetargetMap.h>\n"},
     "plugins/usdVrmaFileFormat: reaches 'motionRetarget'"),
    ("a linked package no source includes",
     {"plugins/execVrm/CMakeLists.txt":
      _CONSUME.format(pkgs="motionCore") +
      "target_link_libraries(E PRIVATE motionCore::motionCore)\n"},
     "links 'motionCore' and no source includes"),
    ("a resolved package nothing links",
     {"libs/vrmRig/CMakeLists.txt":
      _CONSUME.format(pkgs="motionCore") + "add_library(vrmRig STATIC a.cpp)\n"},
     "resolves 'motionCore' and links nothing"),
    ("a link borrowed from another member",
     {"libs/vrmRig/CMakeLists.txt":
      "target_link_libraries(vrmRig PUBLIC motionCore::motionCore)\n"},
     "links 'motionCore' without resolving it"),
    ("an include borrowed from another link line",
     {"libs/vrmRig/CMakeLists.txt": "add_library(vrmRig STATIC a.cpp)\n",
      "libs/vrmRig/openstrata.library.yaml": "requires:\n  libraries: []\n"},
     "includes motionCore/ headers without linking"),
    ("a pin the build never resolves",
     {"libs/vrmRig/openstrata.library.yaml":
      "requires:\n  libraries:\n    - id: motionCore\n    - id: motionSampling\n"},
     "pins 'motionSampling'"),
    ("a resolved package the descriptor omits",
     {"libs/vrmRig/openstrata.library.yaml": "requires:\n  libraries: []\n"},
     "requires.libraries does not declare"),
    ("a dependency checkout beside the source is not ours",
     {"_deps/usd-motion-plugins/libs/motionCore/CMakeLists.txt":
      "add_library(motionCore STATIC a.cpp)\n"},
     ""),
    ("a member with no row",
     {"plugins/newBundle/CMakeLists.txt": "project(n)\n"},
     "SETUP plugins/newBundle: no row"),
]


def selftest() -> int:
    failures = 0
    for name, overrides, expect in CASES:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            _good_tree(root)
            for rel, text in overrides.items():
                _write(root, rel, text)
            report = run(root, [])
            lines = report.errors + [f"SETUP {s}" for s in report.setup]
            if not expect:
                ok = not lines
            else:
                ok = any(expect in line for line in lines)
            if not ok:
                failures += 1
                wanted = repr(expect) if expect else "no finding"
                print(f"FAIL {name}: expected {wanted}, got:")
                for line in lines or ["(nothing)"]:
                    print(f"    {line}")
            else:
                print(f"ok   {name}")

    with tempfile.TemporaryDirectory() as tmp:
        inc = Path(tmp)
        _write(inc, "motionRetarget/R.h",
               "// the VRM 1.0 humanoid is where the list comes from\n"
               "struct R { int a; };\n")
        clean = run_include_only(inc)
        _write(inc, "motionRetarget/Bad.h", "#include <vrmSchema/humanoidAPI.h>\n")
        dirty = run_include_only(inc)
        if clean or not any("knows VRM" in e for e in dirty):
            failures += 1
            print(f"FAIL motion headers: clean={clean} dirty={dirty}")
        else:
            print("ok   a consumed header that knows VRM")

    print(f"{len(CASES) + 1 - failures}/{len(CASES) + 1} self-test cases passed")
    return 1 if failures else 0


def run_include_only(include_root: Path) -> list[str]:
    report = Report()
    with tempfile.TemporaryDirectory() as foreign:
        # Beside a directory holding no consumed package, as the root build
        # hands them over.
        check_motion_includes([include_root, Path(foreign)], report)
    return report.errors + report.setup


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", type=Path, default=REPO_ROOT)
    # `extend` with nargs="+": CTest may hand a `;`-list over already split
    # into several arguments, or as one; both arrive here as a flat list.
    parser.add_argument("--motion-include", action="extend", nargs="+", default=[],
                        help="an installed usd-motion-plugins include/ directory")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    if args.selftest:
        return selftest()

    # CMake hands a target's include directories over as one `;`-list, the
    # same directory as often as not twice.
    includes: list[Path] = []
    for value in args.motion_include:
        for part in str(value).split(";"):
            if part and Path(part) not in includes:
                includes.append(Path(part))
    args.motion_include = includes

    report = run(args.root.resolve(), args.motion_include)
    for line in report.setup:
        print(f"SETUP: {line}")
    for line in report.errors:
        print(f"error: {line}")
    if report.setup:
        return 2
    if report.errors:
        print(f"{len(report.errors)} CMake dependency boundary violation(s)")
        return 1
    checked = len(members(args.root.resolve()))
    extra = (f", and the consumed headers under {len(args.motion_include)} include dir(s)"
             if args.motion_include else "")
    print(f"CMake dependency boundary holds: {checked} members{extra}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
