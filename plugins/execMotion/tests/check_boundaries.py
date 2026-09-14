#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Enforce execMotion's dependency boundary and the snapshot rule.

Motion policy §11.4 and WORKSPACE.md §2 forbid, inside an OpenExec
computation callback: socket or device I/O, file I/O and file watching, a wall
clock, a private thread pool, and mutable global state. A callback is a pure
function of the inputs exec resolves for it, and one that read a socket or a
clock would make cache reuse and invalidation unverifiable -- the one reason to
be on OpenExec at all. Until this check, both bundles obeyed that rule and
nothing would have noticed the first node that did not
(docs/roadmap/openexec-foundation.md §9).

Four halves, because each sees something the others cannot:

  source   the bundle's own code, for the rule's five categories by name, and
           for access to a stage, which a callback is never handed
  imports  the built library's imported symbols, for the same capabilities
           arriving through an inline header or a macro the source scan cannot
           read through
  links    the target's direct link libraries against an allow-list. The
           workspace libraries a bundle could reach a socket through are
           static, so no dependency list of the built binary ever names them;
           only CMake knows the edge exists
  schemas  the OpenExec schemas the bundle declares in plugInfo.json against
           the ones it registers computations for, and against the partition
           WORKSPACE.md §2 states -- a schema has exactly one declarer per
           session, and a second declarer loses its computations silently

execVrm's check imports the source and import rules from this file rather than
restating them: the rule is one rule for both bundles, and execVrm may reach
this bundle's tree (it requires execMotion; the reverse edge is forbidden).

Usage: check_boundaries.py <bundle-source-dir> <built-library> <link-libraries>

<link-libraries> is the target's LINK_LIBRARIES property joined with "|".
"""

from __future__ import annotations

import json
import os
import pathlib
import re
import shutil
import subprocess
import sys


# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------

def find_dumpbin() -> str | None:
    tool = shutil.which("dumpbin")
    if tool:
        return tool
    roots = [
        pathlib.Path(os.environ.get("ProgramFiles", r"C:\\Program Files")),
        pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\\Program Files (x86)")),
    ]
    for root in roots:
        # The release year is a wildcard rather than "2022", and the reason is a
        # measurement: this machine's VS 2022 was replaced by VS 18 in place on
        # 2026-08-25, leaving an empty `2022/` beside a populated `18/`. Every
        # boundary check in the tree then reported "dumpbin was not found" and
        # failed -- nine red names for an editor upgrade, none of them about a
        # boundary. A locator that names one release of a tool it only needs
        # `/dependents` from is a version pin with no reason to exist.
        matches = sorted(root.glob(
            "Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"),
            reverse=True)
        if matches:
            return str(matches[0])
    return None


def _run(command: list[str]) -> str:
    return subprocess.run(
        command, check=True, text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE).stdout


def binary_dependencies(library: pathlib.Path) -> str:
    """The shared libraries the built binary names, as the platform tool says.

    dumpbin and otool both print the inspected file's own path first. It is
    removed, so a bundle's own name, or a checkout directory's, cannot match a
    pattern meant for what the bundle depends on.
    """
    if sys.platform == "win32":
        tool = find_dumpbin()
        if not tool:
            raise RuntimeError("dumpbin was not found")
        text = _run([tool, "/nologo", "/dependents", str(library)])
    elif sys.platform == "darwin":
        tool = shutil.which("otool")
        if not tool:
            raise RuntimeError("otool was not found")
        text = _run([tool, "-L", str(library)])
    else:
        tool = shutil.which("readelf")
        if not tool:
            raise RuntimeError("readelf was not found")
        text = _run([tool, "-d", str(library)])
    return text.replace(str(library), "")


def imported_symbols(library: pathlib.Path) -> list[tuple[str, str]]:
    """Every symbol the built binary imports, as (provider, name).

    The provider is the DLL on Windows and empty elsewhere, where an ELF or
    Mach-O undefined symbol does not say which library will satisfy it. Names
    are demangled on POSIX so one pattern serves libstdc++ and libc++; MSVC's
    decorated names are matched as they are.
    """
    symbols: list[tuple[str, str]] = []
    if sys.platform == "win32":
        tool = find_dumpbin()
        if not tool:
            raise RuntimeError("dumpbin was not found")
        provider = ""
        for line in _run([tool, "/nologo", "/imports", str(library)]).splitlines():
            dll = re.match(r"^\s{4}(\S+\.dll)\s*$", line, re.IGNORECASE)
            if dll:
                provider = dll.group(1)
                continue
            # "<hint> <name>" -- exactly one token after the hint, which is what
            # separates an import from "0 time date stamp" and the table headers.
            entry = re.match(r"^\s+[0-9A-Fa-f]+\s+(\S+)\s*$", line)
            if entry and provider:
                symbols.append((provider, entry.group(1)))
        return symbols

    tool = shutil.which("nm")
    if not tool:
        raise RuntimeError("nm was not found")
    if sys.platform == "darwin":
        # Xcode's nm is llvm-nm and demangles; the older cctools nm has no -C,
        # and c++filt does the same job after it.
        try:
            listing = _run([tool, "-u", "-C", str(library)])
        except subprocess.CalledProcessError:
            listing = _run([tool, "-u", str(library)])
            demangler = shutil.which("c++filt")
            if demangler:
                listing = subprocess.run(
                    [demangler], input=listing, check=True, text=True,
                    encoding="utf-8", errors="replace",
                    stdout=subprocess.PIPE).stdout
        for line in listing.splitlines():
            name = line.strip()
            # Mach-O prefixes a C symbol with one underscore; demangling removes
            # it from a C++ one and leaves it on a C one.
            if re.fullmatch(r"_[A-Za-z_]\w*", name):
                name = name[1:]
            if name:
                symbols.append(("", name))
        return symbols
    for line in _run([tool, "-D", "--undefined-only", "-C", str(library)]).splitlines():
        entry = re.match(r"^\s*U\s+(.+?)\s*$", line)
        if entry:
            # A versioned reference reads `time@GLIBC_2.2.5`.
            symbols.append(("", re.sub(r"@.*$", "", entry.group(1))))
    return symbols


# ---------------------------------------------------------------------------
# Source text
# ---------------------------------------------------------------------------

_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
_LINE_COMMENT = re.compile(r"//[^\n]*")
_STRING = re.compile(r'"(?:\\.|[^"\\\n])*"')
_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def code_only(text: str) -> str:
    """Strip C++ comments before scanning.

    These sources document the boundary in situ, so the prose names the very
    things the code may not do. Scanning the comments too would make an
    accurate explanation indistinguishable from a violation.
    """
    return _LINE_COMMENT.sub("", _BLOCK_COMMENT.sub("", text))


def source_files(source: pathlib.Path) -> list[pathlib.Path]:
    suffixes = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}
    return sorted(p for p in (source / "src").rglob("*")
                  if p.is_file() and p.suffix.lower() in suffixes)


# ---------------------------------------------------------------------------
# The snapshot rule, by source (motion policy §11.4)
# ---------------------------------------------------------------------------
# Each category is matched twice: by the header that brings the capability in,
# and by the name that uses it -- a header can arrive transitively, and a name
# can be declared by hand. String literals are removed before the names are
# matched, so an error message that says "time" is not a clock.

PURITY_INCLUDES: list[tuple[str, re.Pattern[str]]] = [
    ("socket or device I/O", re.compile(
        r"^(?:winsock2?\.h|ws2tcpip\.h|sys/socket\.h|netinet/.+|arpa/inet\.h|"
        r"netdb\.h|(?:boost/)?asio(?:/.+|\.hpp)?|curl/.+)$", re.IGNORECASE)),
    ("file I/O or file watching", re.compile(
        r"^(?:fstream|filesystem|sys/inotify\.h|sys/event\.h|CoreServices/.+|"
        r"pxr/base/arch/fileSystem\.h|pxr/base/tf/fileUtils\.h)$")),
    ("stage access", re.compile(
        r"^pxr/usd/(?:usd/(?:stage|prim|attribute|relationship|editTarget)|"
        r"sdf/(?:layer|abstractData|fileFormat))\.h$")),
    ("a wall clock", re.compile(
        r"^(?:chrono|ctime|time\.h|sys/time\.h|pxr/base/arch/timing\.h|"
        r"pxr/base/tf/stopwatch\.h)$")),
    ("a private thread pool", re.compile(
        r"^(?:thread|future|pthread\.h|(?:oneapi/)?tbb/.+|"
        r"pxr/base/work/(?:dispatcher|detachedTask|threadLimits)\.h)$")),
    ("mutable global state", re.compile(
        r"^(?:atomic|mutex|shared_mutex|condition_variable|semaphore|latch|"
        r"barrier|random)$")),
]

PURITY_NAMES: list[tuple[str, re.Pattern[str]]] = [
    ("socket or device I/O", re.compile(
        r"\b(?:WSAStartup|WSASocket\w*|recvfrom|recvmsg|sendto|getaddrinfo|"
        r"curl_\w+)\b|\bsocket\s*\(|\basio::")),
    ("file I/O or file watching", re.compile(
        r"\b(?:fopen|_wfopen|fopen_s|_wfopen_s|_fsopen|_wfsopen|freopen|"
        r"ReadDirectoryChanges\w*|FindFirstChangeNotification\w*|CreateFile[AW2]?|"
        r"inotify_\w+|kqueue|FSEventStream\w*|ArchOpenFile|TfReadDir|TfIsFile|"
        r"TfIsDir|TfPathExists)\b|\bstd::filesystem\b|"
        # Unqualified as well: `using namespace std;` makes `ifstream` a stream.
        r"\b(?:basic_|w)?[io]?fstream\b|\b(?:basic_|w)?filebuf\b")),
    ("stage access", re.compile(
        r"\b(?:UsdStage\w*|UsdPrim|UsdAttribute|UsdRelationship|UsdEditTarget|"
        r"SdfLayer\w*|SdfAbstractData\w*)\b")),
    ("a wall clock", re.compile(
        r"\bstd::chrono\b|\b(?:steady_clock|system_clock|high_resolution_clock|"
        r"file_clock|utc_clock)\b|\b(?:clock_gettime|gettimeofday|"
        r"QueryPerformanceCounter|GetTickCount(?:64)?|GetSystemTime\w*|"
        r"GetLocalTime|timeGetTime|mach_absolute_time|timespec_get|"
        r"ArchGetTickTime|ArchGetStartTickTime|TfStopwatch)\b|"
        r"\btime\s*\(\s*(?:nullptr|NULL|0)?\s*\)|\bclock\s*\(\s*\)")),
    ("a private thread pool", re.compile(
        r"\bstd::(?:thread|jthread|async|this_thread)\b|"
        r"\b(?:pthread_create|CreateThread|_beginthread(?:ex)?|"
        r"CreateThreadpool\w*|SubmitThreadpoolWork|QueueUserWorkItem|"
        r"WorkDispatcher|WorkIsolatingDispatcher|WorkRunDetachedTask|"
        r"WorkSetConcurrencyLimit\w*|WorkSetMaximumConcurrencyLimit)\b|"
        r"\btbb::(?:task_arena|task_group|task_scheduler_init|global_control)\b")),
    ("mutable global state", re.compile(
        r"\bthread_local\b|\bstd::(?:atomic\w*|mutex|recursive_mutex|"
        r"shared_mutex|timed_mutex|condition_variable\w*|call_once|once_flag|"
        r"random_device|mt19937\w*|default_random_engine)\b|"
        r"\b(?:getenv|_wgetenv|getenv_s|_dupenv_s|secure_getenv|TfGetenv\w*|"
        r"ArchGetEnv|TfGetEnvSetting|TF_DEFINE_ENV_SETTING)\b|"
        r"\b(?:rand|srand)\s*\(")),
]

# A `static` object is state unless the object itself is const. The qualifiers
# before `static` count (`constexpr static int k`), a template's arguments do
# not (`std::vector<const T*>` is a mutable vector), and past a pointer only a
# `const` after the last `*` makes the pointer const: `static const T* last` is
# reassigned as freely as a `T*` is.
#
# What this scan does NOT find, because telling it apart takes parsing C++:
#   - a direct-initialised `static T t(1);`, which reads like a function
#     declaration. A `(` outside a template's arguments is taken for one;
#   - a variable at namespace scope, named or unnamed, declared with no
#     `static` at all;
#   - state reached through a `mutable` member of a const object.
# The import half does not find them either. Review is what holds these three.
_STATIC = re.compile(
    r"(?P<pre>(?:\b(?:constexpr|const|inline|constinit)\s+)*)\bstatic\b")


def _strip_templates(text: str) -> str:
    previous = None
    while previous != text:
        previous = text
        text = re.sub(r"<[^<>]*>", " ", text)
    return text


def _declarator(body: str, start: int) -> tuple[str, str]:
    """The text after `static` up to what ends it, outside template arguments."""
    depth = 0
    for index in range(start, len(body)):
        char = body[index]
        if char == "<":
            depth += 1
        elif char == ">" and depth:
            depth -= 1
        elif depth == 0 and char in ";{}=[(":
            return body[start:index], char
    return body[start:], ""


def _is_const_object(declaration: str) -> bool:
    tokens = re.findall(r"\w+|[*&]", _strip_templates(declaration))
    if "constexpr" in tokens:
        return True
    if "*" in tokens:
        last_star = len(tokens) - 1 - tokens[::-1].index("*")
        return "const" in tokens[last_star + 1:]
    if "&" in tokens:
        return "const" in tokens[:tokens.index("&")]
    return "const" in tokens


def mutable_statics(body: str) -> list[str]:
    found = []
    for match in _STATIC.finditer(body):
        declarator, end = _declarator(body, match.end())
        if end in ("(", "}", ""):
            continue
        declaration = f"{match.group('pre')}static{declarator}"
        if not _is_const_object(declaration):
            found.append(" ".join(declaration.split()))
    return found


def purity_text_errors(text: str, where: str) -> list[str]:
    """The snapshot rule over one translation unit's text."""
    errors: list[str] = []
    code = code_only(text)
    for include in _INCLUDE.findall(code):
        for category, pattern in PURITY_INCLUDES:
            if pattern.match(include):
                errors.append(
                    f"{category} is forbidden in a computation: "
                    f"#include <{include}> in {where}")
    body = _STRING.sub('""', _INCLUDE.sub("", code))
    for category, pattern in PURITY_NAMES:
        for match in sorted({m.group(0) for m in pattern.finditer(body)}):
            errors.append(
                f"{category} is forbidden in a computation: "
                f"'{match.strip()}' in {where}")
    for declaration in mutable_statics(body):
        errors.append(
            f"mutable global state is forbidden in a computation: "
            f"'{declaration}' in {where} (make it const, or pass it in)")
    return errors


def purity_source_errors(source: pathlib.Path) -> list[str]:
    files = source_files(source)
    if not files:
        return [f"no source files under {source / 'src'}: nothing was scanned"]
    errors: list[str] = []
    for path in files:
        errors += purity_text_errors(path.read_text(encoding="utf-8"), str(path))
    return errors


# ---------------------------------------------------------------------------
# The snapshot rule, by imported symbol
# ---------------------------------------------------------------------------
# What is NOT here is as measured as what is. MSVC links a CRT stub into every
# DLL, and on 2026-09-14 libExecMotion.dll imported GetSystemTimeAsFileTime,
# QueryPerformanceCounter and GetCurrentThreadId (the security cookie's seed)
# and the SRW-lock and condition-variable calls (a function-local static's
# guard) from KERNEL32 with no clock and no thread in its source. So KERNEL32's
# clock is not a check; the C++ runtime's is, since `std::chrono`'s clocks
# reach `_Query_perf_counter` and `_Xtime_get_ticks` in MSVCP140 and nothing
# the stub uses does. On POSIX a socket, a thread and a clock all live in libc
# or libSystem, so the check is by symbol there too, and never by library.
#
# MSVCP140 exports most of what matters here as C names, and one of them
# decorated: `_Fiopen`, which every `std::basic_filebuf::open` reaches, exists
# only as `?_Fiopen@std@@...` (three overloads). A pattern for the plain name
# never matches it, and a `std::ifstream` passed this half until it was
# matched decorated.

_WINDOWS_FORBIDDEN_PROVIDERS: list[tuple[str, re.Pattern[str]]] = [
    ("socket or device I/O", re.compile(
        r"^(?:ws2_32|wsock32|mswsock|winhttp|wininet|iphlpapi)\.dll$",
        re.IGNORECASE)),
    ("a wall clock", re.compile(r"^winmm\.dll$", re.IGNORECASE)),
    # The Concurrency Runtime is what `std::async` and PPL run on, and vcomp is
    # OpenMP's: each is a scheduler of its own beside OpenExec's.
    ("a private thread pool", re.compile(
        r"^(?:concrt140|vcomp140d?)\.dll$", re.IGNORECASE)),
]

_WINDOWS_FORBIDDEN_SYMBOLS: list[tuple[str, re.Pattern[str]]] = [
    ("a wall clock", re.compile(
        r"^(?:_Query_perf_counter|_Query_perf_frequency|_Xtime_get_ticks|"
        r"_time32|_time64|time|clock|_ftime\w*|timespec_get|_timespec\d*_get|"
        r"GetTickCount(?:64)?|GetSystemTimePreciseAsFileTime|GetLocalTime|"
        r"GetSystemTime|timeGetTime)$")),
    ("a private thread pool", re.compile(
        r"^(?:_Thrd_\w+|_beginthread(?:ex)?|CreateThread|CreateRemoteThread\w*|"
        r"CreateThreadpool\w*|SubmitThreadpoolWork|TrySubmitThreadpoolCallback|"
        r"QueueUserWorkItem|Sleep|SleepEx)$")),
    ("mutable global state", re.compile(
        r"^(?:_Mtx_\w+|_Cnd_\w+|getenv|_wgetenv|getenv_s|_wgetenv_s|_dupenv_s|"
        r"_wdupenv_s|GetEnvironmentVariable\w*)$")),
    ("file I/O or file watching", re.compile(
        r"^(?:__std_fs_\w+|fopen|_wfopen|fopen_s|_wfopen_s|_fsopen|"
        r"_wfsopen|freopen|CreateFile[AW2]?|OpenFile|ReadDirectoryChanges\w*|"
        r"FindFirstChangeNotification\w*)$|^\?_Fiopen@std@@")),
]

_POSIX_FORBIDDEN_SYMBOLS: list[tuple[str, re.Pattern[str]]] = [
    ("socket or device I/O", re.compile(
        r"^(?:socket|connect|bind|accept4?|listen|recv|recvfrom|recvmsg|send|"
        r"sendto|sendmsg|getaddrinfo)$")),
    ("a wall clock", re.compile(
        r"^(?:clock_gettime|gettimeofday|time|clock)$|"
        r"std::(?:__1::)?chrono::(?:_V2::)?"
        r"(?:steady_clock|system_clock|high_resolution_clock)::now\b")),
    ("a private thread pool", re.compile(
        r"^(?:pthread_create|GOMP_\w+|__kmpc_\w+)$|std::(?:__1::)?thread::|"
        r"std::(?:__1::)?this_thread::")),
    ("mutable global state", re.compile(r"^(?:getenv|secure_getenv)$")),
    ("file I/O or file watching", re.compile(
        r"^(?:fopen|fopen64|freopen|freopen64|inotify_\w+|kqueue|kevent|"
        r"FSEventStream\w*)$|std::(?:__1::)?(?:__fs::)?filesystem::|"
        r"std::(?:__1::)?basic_filebuf<.*>::open\b")),
]


def forbidden_imports(symbols: list[tuple[str, str]], windows: bool,
                      label: str) -> list[str]:
    """The snapshot rule over an import list, as (provider, name) pairs.

    Separate from reading the list so the tables can be held to synthetic
    symbols of both platforms on any host (test_check_boundaries.py).
    """
    errors: list[str] = []
    if windows:
        for provider in sorted({p for p, _ in symbols}):
            for category, pattern in _WINDOWS_FORBIDDEN_PROVIDERS:
                if pattern.match(provider):
                    errors.append(
                        f"{category} is forbidden in a computation: "
                        f"{label} imports {provider}")
        table = _WINDOWS_FORBIDDEN_SYMBOLS
    else:
        table = _POSIX_FORBIDDEN_SYMBOLS
    for provider, name in symbols:
        for category, pattern in table:
            if pattern.search(name):
                where = f" from {provider}" if provider else ""
                errors.append(
                    f"{category} is forbidden in a computation: "
                    f"{label} imports {name}{where}")
    return errors


def purity_import_errors(library: pathlib.Path) -> list[str]:
    try:
        symbols = imported_symbols(library)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        return [f"could not read the imports of {library}: {exc}"]
    if not symbols:
        # A binary imports its C++ runtime at the least, so an empty list is a
        # parser that stopped matching, not a pure bundle.
        return [f"no imported symbols read from {library}: nothing was checked"]
    return forbidden_imports(symbols, sys.platform == "win32", library.name)


# ---------------------------------------------------------------------------
# Links and schemas
# ---------------------------------------------------------------------------

# The OpenUSD half both bundles may link: the libraries cmake/UsdVrmOpenUsd.cmake
# probes for, plus the base and stage-value libraries under them. A new OpenUSD
# library is one line here, on purpose: imaging, for one, is the presentation
# layer's and not a computation's.
OPENUSD_ALLOWED = {"arch", "tf", "gf", "vt", "plug", "sdf", "usd", "usdSkel",
                   "vdf", "ef", "esf", "esfUsd", "exec", "execUsd"}


def link_errors(bundle: str, links: str, workspace_allowed: set[str]) -> list[str]:
    entries = [e for e in links.split("|") if e.strip()]
    if not entries:
        return [f"{bundle}: no link libraries were handed in, so no edge was checked"]
    errors = []
    for entry in entries:
        name = entry.strip()
        if name in workspace_allowed:
            continue
        if name.removeprefix("pxr::") in OPENUSD_ALLOWED:
            continue
        errors.append(
            f"{bundle} links '{name}', which WORKSPACE.md section 2 does not allow it "
            f"(workspace: {', '.join(sorted(workspace_allowed))}; OpenUSD: the "
            f"exec and stage-value libraries)")
    return errors


# WORKSPACE.md §2's partition. `=:` there is "the OpenExec schemas it declares",
# and a schema this table does not assign is one the contract has not placed:
# it goes into WORKSPACE.md first, then here.
def schema_owner(schema: str) -> str | None:
    if schema == "UsdSkelAnimation":
        return "execMotion"
    if schema in {"UsdSkelSkeleton", "UsdSkelBindingAPI"}:
        return "execVrm"
    if re.fullmatch(r"(?:Usd)?Vrm\w*API", schema):
        return "execVrm"
    return None


def declared_schemas(plug_info: pathlib.Path) -> set[str]:
    data = json.loads(plug_info.read_text(encoding="utf-8"))
    declared: set[str] = set()
    for plugin in data.get("Plugins", []):
        declared |= set(plugin.get("Info", {}).get("Exec", {})
                        .get("Schemas", {}).keys())
    return declared


def schema_errors(bundle: str, source: pathlib.Path) -> tuple[set[str], list[str]]:
    plug_info = source / "plugin" / "resources" / bundle / "plugInfo.json.in"
    errors: list[str] = []
    try:
        declared = declared_schemas(plug_info)
    except (OSError, ValueError) as exc:
        return set(), [f"could not read {plug_info}: {exc}"]
    registered: set[str] = set()
    for path in source_files(source):
        registered |= set(re.findall(
            r"\bEXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA\s*\(\s*(\w+)\s*\)",
            code_only(path.read_text(encoding="utf-8"))))
    if not registered:
        errors.append(f"{bundle} registers computations for no schema: "
                      f"the registration scan matched nothing")
    for schema in sorted(registered - declared):
        errors.append(
            f"{bundle} registers computations for {schema} and its plugInfo.json "
            f"does not declare it: they would never be found")
    for schema in sorted(declared - registered):
        errors.append(
            f"{bundle} declares {schema} in plugInfo.json and registers nothing "
            f"for it: a schema has one declarer per session, so this takes it "
            f"from the bundle that does")
    for schema in sorted(declared):
        owner = schema_owner(schema)
        if owner is None:
            errors.append(
                f"{bundle} declares {schema}, which WORKSPACE.md section 2 assigns to "
                f"neither exec bundle")
        elif owner != bundle:
            errors.append(
                f"{bundle} declares {schema}, which WORKSPACE.md section 2 assigns to "
                f"{owner}: the second declarer loses every computation it "
                f"registered there")
    return declared, errors


# ---------------------------------------------------------------------------
# This bundle
# ---------------------------------------------------------------------------

def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    source = pathlib.Path(sys.argv[1]).resolve()
    library = pathlib.Path(sys.argv[2]).resolve()
    errors: list[str] = []

    errors += purity_source_errors(source)
    errors += purity_import_errors(library)
    errors += link_errors(
        "execMotion", sys.argv[3],
        {"motionCore::motionCore", "motionRuntime::motionRuntime"})
    errors += schema_errors("execMotion", source)[1]

    # Vendor-neutral by specification: nothing VRM-shaped, no live leaf, and
    # never execVrm, whose edge to this bundle is the one direction allowed.
    forbidden_neighbours = re.compile(
        r"\b(?:vrmSchema|vrmContainer|vrmRetarget|usdVrm\w*|UsdVrm\w*|execVrm|"
        r"ExecVrm\w*|cgltf|mocopi|vrchat|ardy|liveTransport|motionTracking|"
        r"vrmAdapter\w*)\b|\bosc::|\bosc/",
        re.IGNORECASE)
    for path in source_files(source):
        if forbidden_neighbours.search(code_only(path.read_text(encoding="utf-8"))):
            errors.append(f"forbidden dependency direction: {path}")

    try:
        dependencies = binary_dependencies(library)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        errors.append(f"could not inspect execMotion dependencies: {exc}")
        dependencies = ""
    if re.search(r"vrmSchema|vrmContainer|UsdVrm|ExecVrm|liveTransport|"
                 r"motionTracking|vrmAdapter|(?:^|[\s/\\])(?:lib)?osc[._]",
                 dependencies, re.IGNORECASE | re.MULTILINE):
        errors.append(
            "execMotion binary imports a VRM, live or sibling-bundle library")

    if errors:
        if hasattr(sys.stderr, "reconfigure"):
            sys.stderr.reconfigure(errors="replace")
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("execMotion boundary check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
