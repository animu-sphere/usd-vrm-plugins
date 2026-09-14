#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Hold check_boundaries.py's tables to cases, on any host.

The boundary check passes against both exec bundles, and a pass says nothing
about whether it would fail. Its first review found two tables that could not:
`_Fiopen` listed by a name MSVCP140 never exports, and a `static` detector that
took `static const T* last` for a constant and `std::function<void()>` for a
function. Both were found by building probe libraries by hand. This file keeps
those probes, and one of each kind the check is meant to report, as text and
as symbol lists -- so the POSIX tables are held to libstdc++'s and libc++'s
spellings on a Windows workstation too, which no single lane's built library
could do.

What the check is documented not to find is pinned here as not found. A change
that starts finding one is then an edit to this file, not a silent widening.
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import check_boundaries as rules  # noqa: E402

_failures: list[str] = []


def _expect(condition: bool, message: str) -> None:
    if not condition:
        _failures.append(message)


# ---------------------------------------------------------------------------
# `static`
# ---------------------------------------------------------------------------

MUTABLE_STATICS = [
    "static int calls = 0;",
    "static Pose cache;",
    "static Pose cache{};",
    "static int table[4];",
    # Review's two: the pointer is reassigned freely, and the parenthesis is the
    # template's.
    "static const Pose* last = nullptr;",
    "static std::function<void()> hook;",
    "static std::vector<const Pose*> seen;",
    "static Pose& shared = Get();",
    "inline static int count = 0;",
    "constinit static int count = 0;",
]

CONST_STATICS = [
    "static const int k = 1;",
    "static constexpr int k = 1;",
    # Review's two false reports: the qualifier before `static`, and `const`
    # after the type.
    "constexpr static int k = 1;",
    "static Pose const kRest{};",
    "static const Pose* const kNone = nullptr;",
    "static inline const int k = 2;",
    "static const Pose& kRef = Get();",
    "static const std::array<TfToken, 3> names = [] { return Make(); }();",
    "static bool Helper(int x) { return x > 0; }",
    "static Pose Make();",
    "static_assert(sizeof(int) == 4);",
    "int y = static_cast<int>(x);",
]

# Documented as not found (check_boundaries.py, above `_STATIC`).
UNFOUND_STATE = [
    "static Pose cache(1);",
    "namespace { int counter = 0; }",
    "int counter = 0;",
]

for text in MUTABLE_STATICS:
    _expect(rules.mutable_statics(text) != [],
            f"a mutable static was not reported: {text}")
for text in CONST_STATICS:
    _expect(rules.mutable_statics(text) == [],
            f"a const static or a function was reported: {text} -> "
            f"{rules.mutable_statics(text)}")
for text in UNFOUND_STATE:
    _expect(rules.purity_text_errors(text, "case") == [],
            f"a shape documented as not found is now found; update the "
            f"check's note and this list: {text}")


# ---------------------------------------------------------------------------
# Names and headers
# ---------------------------------------------------------------------------

def _categories(text: str) -> set[str]:
    return {e.split(" is forbidden")[0] for e in rules.purity_text_errors(text, "case")}


REPORTED_TEXT = {
    "using namespace std;\nvoid f() { ifstream in(path); }": "file I/O or file watching",
    "std::wofstream out;": "file I/O or file watching",
    "std::basic_filebuf<char> buffer;": "file I/O or file watching",
    "auto n = std::chrono::steady_clock::now();": "a wall clock",
    "std::thread([] {}).join();": "a private thread pool",
    "auto v = std::getenv(\"X\");": "mutable global state",
    "thread_local int n;": "mutable global state",
    "#include <chrono>\n": "a wall clock",
    "#include <sys/socket.h>\n": "socket or device I/O",
    "UsdStageRefPtr stage;": "stage access",
}
for text, category in REPORTED_TEXT.items():
    _expect(category in _categories(text),
            f"'{category}' was not reported for: {text!r}")

# Prose about the rule, and a message that says a forbidden word, are not code.
QUIET_TEXT = [
    "// a callback never opens a socket or reads the time()\nint x = 0;",
    "/* std::thread, std::chrono */ int y = 1;",
    "TF_CODING_ERROR(\"no time() and no socket( here\");",
    "std::string_view name;",
]
for text in QUIET_TEXT:
    _expect(rules.purity_text_errors(text, "case") == [],
            f"prose or a string was reported: {text!r} -> "
            f"{rules.purity_text_errors(text, 'case')}")


# ---------------------------------------------------------------------------
# Imports, as each platform's tool prints them
# ---------------------------------------------------------------------------

def _import_categories(symbols: list[tuple[str, str]], windows: bool) -> set[str]:
    return {e.split(" is forbidden")[0]
            for e in rules.forbidden_imports(symbols, windows, "case")}


WINDOWS_REPORTED = {
    # Every `std::basic_filebuf::open` reaches one of these three, and MSVCP140
    # exports no undecorated `_Fiopen` at all.
    ("MSVCP140.dll", "?_Fiopen@std@@YAPEAU_iobuf@@PEBDHH@Z"): "file I/O or file watching",
    ("MSVCP140.dll", "?_Fiopen@std@@YAPEAU_iobuf@@PEBGHH@Z"): "file I/O or file watching",
    ("MSVCP140.dll", "?_Fiopen@std@@YAPEAU_iobuf@@PEB_WHH@Z"): "file I/O or file watching",
    ("MSVCP140.dll", "__std_fs_open_handle"): "file I/O or file watching",
    ("MSVCP140.dll", "_Query_perf_counter"): "a wall clock",
    ("MSVCP140.dll", "_Xtime_get_ticks"): "a wall clock",
    ("MSVCP140.dll", "_Thrd_join"): "a private thread pool",
    ("api-ms-win-crt-runtime-l1-1-0.dll", "_beginthreadex"): "a private thread pool",
    ("api-ms-win-crt-environment-l1-1-0.dll", "getenv"): "mutable global state",
    ("WS2_32.dll", "recvfrom"): "socket or device I/O",
    ("concrt140.dll", "?_Schedule@_TaskCollectionBase@details@Concurrency@@AEAAXXZ"):
        "a private thread pool",
    ("VCOMP140.DLL", "_vcomp_fork"): "a private thread pool",
}
for symbol, category in WINDOWS_REPORTED.items():
    _expect(category in _import_categories([symbol], True),
            f"'{category}' was not reported for the Windows import {symbol}")

# What MSVC's CRT stub imports into every DLL, measured on both bundles with no
# clock and no thread in either source. Reporting any of it would fail every
# bundle for the compiler's sake.
WINDOWS_BASELINE = [
    ("KERNEL32.dll", name) for name in (
        "GetSystemTimeAsFileTime", "QueryPerformanceCounter",
        "GetCurrentThreadId", "GetCurrentProcessId", "InitializeSListHead",
        "AcquireSRWLockExclusive", "ReleaseSRWLockExclusive",
        "SleepConditionVariableSRW", "WakeAllConditionVariable",
        "DisableThreadLibraryCalls")
] + [
    ("MSVCP140.dll", "?_Xlength_error@std@@YAXPEBD@Z"),
    ("MSVCP140.dll", "?_Lock@?$basic_streambuf@DU?$char_traits@D@std@@@std@@UEAAXXZ"),
    ("VCRUNTIME140.dll", "__std_terminate"),
    ("usd_tf.dll", "?GetString@TfToken@pxrInternal_v0_26_8__pxrReserved__@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ"),
]
_expect(rules.forbidden_imports(WINDOWS_BASELINE, True, "case") == [],
        f"the CRT stub's own imports were reported: "
        f"{rules.forbidden_imports(WINDOWS_BASELINE, True, 'case')}")

POSIX_REPORTED = {
    "std::chrono::_V2::steady_clock::now()": "a wall clock",
    "std::chrono::_V2::system_clock::now()": "a wall clock",
    "std::__1::chrono::steady_clock::now()": "a wall clock",
    "clock_gettime": "a wall clock",
    "pthread_create": "a private thread pool",
    "GOMP_parallel": "a private thread pool",
    "std::thread::_M_start_thread(std::unique_ptr<std::thread::_State, "
    "std::default_delete<std::thread::_State> >, void (*)())": "a private thread pool",
    "std::basic_filebuf<char, std::char_traits<char> >::open(char const*, "
    "std::_Ios_Openmode)": "file I/O or file watching",
    "std::__1::basic_filebuf<char, std::__1::char_traits<char> >::open(char "
    "const*, unsigned int)": "file I/O or file watching",
    "std::filesystem::status(std::filesystem::__cxx11::path const&)":
        "file I/O or file watching",
    "fopen64": "file I/O or file watching",
    "socket": "socket or device I/O",
    "recvfrom": "socket or device I/O",
    "getenv": "mutable global state",
}
for name, category in POSIX_REPORTED.items():
    _expect(category in _import_categories([("", name)], False),
            f"'{category}' was not reported for the POSIX import {name}")

# Undefined symbols an ordinary C++ library has. `mach_absolute_time` is here on
# purpose: OpenUSD's inline tick timer reaches it on macOS, and the check leaves
# it out rather than fail a bundle for a header it includes.
POSIX_BASELINE = [("", name) for name in (
    "memcpy", "pthread_once", "__cxa_guard_acquire", "__stack_chk_fail",
    "operator new(unsigned long)", "mach_absolute_time",
    "std::__throw_length_error(char const*)",
    "std::__cxx11::basic_string<char, std::char_traits<char>, "
    "std::allocator<char> >::_M_create(unsigned long&, unsigned long)",
    "std::basic_streambuf<char, std::char_traits<char> >::xsgetn(char*, long)",
    "timezone_offset", "sendfile_count",
)]
_expect(rules.forbidden_imports(POSIX_BASELINE, False, "case") == [],
        f"an ordinary POSIX import was reported: "
        f"{rules.forbidden_imports(POSIX_BASELINE, False, 'case')}")


# ---------------------------------------------------------------------------
# The schema partition
# ---------------------------------------------------------------------------

for schema, owner in {"UsdSkelAnimation": "execMotion",
                      "UsdSkelSkeleton": "execVrm",
                      "UsdSkelBindingAPI": "execVrm",
                      "UsdVrmHumanoidAPI": "execVrm",
                      "UsdGeomXformable": None}.items():
    _expect(rules.schema_owner(schema) == owner,
            f"{schema} is assigned to {rules.schema_owner(schema)}, not {owner}")


if _failures:
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(errors="replace")
    print("\n".join(_failures), file=sys.stderr)
    sys.exit(1)
print("check_boundaries.py tables hold")
