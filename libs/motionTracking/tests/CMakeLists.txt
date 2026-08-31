# SPDX-License-Identifier: Apache-2.0
#
# Two executables, one per half of the library, and the split is the same one
# the boundary check enforces: the vocabulary and the assignment name no bone
# and no value type, the solve names both. A single suite would have made that
# invisible, and the assignment half's fixtures are decisions where the solve
# half's are compositions.

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

add_executable(motionTracking_trackerSolve_tests test_tracker_solve.cpp)
target_link_libraries(motionTracking_trackerSolve_tests
    PRIVATE motionTracking::motionTracking)
target_compile_options(motionTracking_trackerSolve_tests
    PRIVATE ${_motiontracking_assertions})
if(MSVC)
    target_compile_options(motionTracking_trackerSolve_tests PRIVATE /utf-8)
endif()
add_test(NAME motionTracking_trackerSolve
    COMMAND motionTracking_trackerSolve_tests)

set(_motiontracking_tests
    motionTracking_trackerAssignment motionTracking_trackerSolve)

if(WIN32)
    if(TARGET gf)
        set(_motiontracking_gf gf)
    else()
        set(_motiontracking_gf pxr::gf)
    endif()
    # Until VRC-5 this file said there was nothing for a loader to find, and
    # that was true: the library linked no OpenUSD. The solve does -- it calls
    # exported Gf entry points through `motionCore`, so both executables load
    # usd_gf.dll at run time, which pulls usd_tf.dll -> tbb.dll from bin/ plus
    # python3xx.dll from the Python install. An activated `ost` session has all
    # three on PATH; a plain-CMake `ctest` run does not, and the test exits
    # 0xC0000135 (DLL not found) without them.
    #
    # Both suites take it, not only the solve's: they link one static library
    # and the linker is what decides which objects come with it.
    set(_motiontracking_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_motiontracking_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_motiontracking_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_motiontracking_python_dir
                               "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _motiontracking_test_path
             "PATH=path_list_prepend:${_motiontracking_python_dir}")
    endif()
    set_tests_properties(${_motiontracking_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "${_motiontracking_test_path}")
endif()

if(Python3_Interpreter_FOUND)
    # The solve suite is the binary argument, because it is the one whose
    # imports are worth a question: it links the same static library the other
    # does, and it is the executable that actually pulls the Gf entry points in.
    add_test(NAME motionTracking_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:motionTracking_trackerSolve_tests>")
endif()
