# SPDX-License-Identifier: Apache-2.0
#
# One executable, holding the suite that arrived with the decoder.
#
# It is not a new suite. OSC-0 froze this decoder's public behaviour in tests
# written beside it in `vrmAdapterVmc`, before anything moved, precisely so that
# the move could be checked against them — so the whole of that freeze travels
# here, minus the half that reads an adapter's capture format over an adapter's
# fixtures. That half stayed where those fixtures live, and it still runs.

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would print
# "passed" and return 0 without having checked anything. Undefine it for the
# test target only: the library under test stays Release, the checks stay live.
# See libs/motionRuntime/tests/CMakeLists.txt, which hit this first.
set(_osc_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(osc_oscPacket_tests test_osc_packet.cpp)
target_link_libraries(osc_oscPacket_tests PRIVATE osc::osc)
target_compile_options(osc_oscPacket_tests PRIVATE ${_osc_assertions})
if(MSVC)
    # The headers cite roadmap sections by their `§` sign, and MSVC reads source
    # in the *system* code page unless told otherwise -- so a Japanese-Windows
    # build warns C4819 on every include without this.
    target_compile_options(osc_oscPacket_tests PRIVATE /utf-8)
endif()
add_test(NAME osc_oscPacket COMMAND osc_oscPacket_tests)

# No ENVIRONMENT_MODIFICATION for the runtime's bin/, and here that is a
# statement rather than an oversight: this library links no OpenUSD and no
# platform library on any of the three, so there is nothing for a loader to
# find. The boundary check below asserts exactly that against the built binary.

if(Python3_Interpreter_FOUND)
    add_test(NAME osc_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:osc_oscPacket_tests>")
endif()
