# SPDX-License-Identifier: Apache-2.0
#
# One executable: this library has one behaviour, and splitting it across two
# suites would hide that the vocabulary and the policy over it are checked
# together.

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would print
# "passed" and return 0 without having checked anything. Undefine it for the
# test target only: the library under test stays Release, the checks stay live.
# See libs/motionRuntime/tests/CMakeLists.txt, which hit this first.
set(_motiontracking_assertions
    $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(motionTracking_trackerAssignment_tests
    test_tracker_assignment.cpp)
target_link_libraries(motionTracking_trackerAssignment_tests
    PRIVATE motionTracking::motionTracking)
target_compile_options(motionTracking_trackerAssignment_tests
    PRIVATE ${_motiontracking_assertions})
if(MSVC)
    # The headers cite contract sections by their `§` sign, and MSVC reads
    # source in the *system* code page unless told otherwise -- so a
    # Japanese-Windows build warns C4819 on every include without this.
    target_compile_options(motionTracking_trackerAssignment_tests PRIVATE /utf-8)
endif()
add_test(NAME motionTracking_trackerAssignment
    COMMAND motionTracking_trackerAssignment_tests)

# No ENVIRONMENT_MODIFICATION for the runtime's bin/, and here that is a
# statement rather than an oversight: this library links no OpenUSD and no
# platform library on any of the three, so there is nothing for a loader to
# find. The boundary check below asserts exactly that against the built binary.

if(Python3_Interpreter_FOUND)
    add_test(NAME motionTracking_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:motionTracking_trackerAssignment_tests>")
endif()
