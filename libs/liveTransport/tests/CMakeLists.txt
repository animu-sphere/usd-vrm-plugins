# SPDX-License-Identifier: Apache-2.0
#
# Three executables, and the small number is deliberate.
#
# The socket, the capture format and the two adapters' diagnostic tables are
# already covered exhaustively by the suites that travelled with them — those
# tests did not move, they still name their own adapter's behaviour, and they
# now exercise this library through it. Copying them here would make a third
# copy of the very thing OSC-2 removed two of.
#
# What is here is what could not exist before: the internal header's two
# decisions, which no adapter could hold without diverging from its sibling, and
# the vehicle carrying a code set that belongs to neither adapter.
#
# The third arrived later and by the same rule. The `p` line -- which peer sent
# a record -- was added to the format after the extraction, and it is transport
# identity rather than protocol: no adapter's suite owns it, and a decoder may
# not read it. Its test names a magic that is nobody's.

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would print
# "verified" and return 0 without having checked anything. Undefine it for the
# test targets only: the library under test stays Release, the checks stay live.
# See libs/motionRuntime/tests/CMakeLists.txt, which hit this first.
set(_live_transport_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

# The debt OSC-1 recorded: the poll timeout mapping and the wake-up predicate,
# neither of which a socket-owning suite can produce on demand.
add_executable(liveTransport_pollTimeout_tests test_poll_timeout.cpp)
# src/ is on this target's include path rather than the library's PUBLIC one:
# PollTimeout.h is internal and is not installed, and a consumer that could
# include it would make it an API.
target_include_directories(liveTransport_pollTimeout_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# The diagnostic vehicle, against a code set this library has never seen.
add_executable(liveTransport_diagnostics_tests test_diagnostics.cpp)

# The capture format's one line that belongs to no adapter, and the
# compatibility claim two corpora rest on: a capture whose records name no
# peer is written exactly as it was before the line existed.
add_executable(liveTransport_packetCapture_tests test_packet_capture.cpp)

foreach(_live_transport_test liveTransport_pollTimeout_tests
                             liveTransport_diagnostics_tests
                             liveTransport_packetCapture_tests)
    target_link_libraries(${_live_transport_test}
        PRIVATE liveTransport::liveTransport)
    target_compile_options(${_live_transport_test}
        PRIVATE ${_live_transport_assertions})
    if(MSVC)
        # The headers cite roadmap sections by their `§` sign, and MSVC reads
        # source in the *system* code page unless told otherwise -- so a
        # Japanese-Windows build warns C4819 on every include without this.
        target_compile_options(${_live_transport_test} PRIVATE /utf-8)
    endif()
endforeach()

add_test(NAME liveTransport_pollTimeout
         COMMAND liveTransport_pollTimeout_tests)
add_test(NAME liveTransport_diagnostics
         COMMAND liveTransport_diagnostics_tests)
add_test(NAME liveTransport_packetCapture
         COMMAND liveTransport_packetCapture_tests)

# No ENVIRONMENT_MODIFICATION for the runtime's bin/ on any of the three, and
# here that is a statement rather than an oversight: this library links no
# OpenUSD on any platform, so there is nothing for a loader to find. The boundary check below
# asserts exactly that against the built binary.

if(Python3_Interpreter_FOUND)
    # Source *and* binary, unlike motionBvh's check. There the binary half had
    # become a measurement of the linker, because a declared edge reached
    # motionCore's Gf value types and three toolchains disagreed about whether
    # to record the dylibs. Here the edge set is empty, so the binary half has
    # no allowlist and no platform variance to survive: no `usd_*` import may
    # appear, on any of the three.
    add_test(NAME liveTransport_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:liveTransport_diagnostics_tests>")
endif()
