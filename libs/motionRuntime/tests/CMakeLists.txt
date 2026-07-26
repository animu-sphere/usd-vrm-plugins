# SPDX-License-Identifier: Apache-2.0

# Every suite in this workspace checks with `assert()`, and NDEBUG compiles
# `assert()` away -- so a suite built Release prints "passed" and returns 0
# without having checked anything. That is not hypothetical: the root
# CMakeLists defaults single-config builds to Release, and motion-ci.yml passes
# -DCMAKE_BUILD_TYPE=Release outright, so it was the normal case.
#
# NDEBUG is therefore undefined for test targets specifically: the library under
# test stays Release, the checks inside the test stay live.
# target_compile_options lands after CMAKE_CXX_FLAGS_<CONFIG>, so the /U wins.
set(_motion_runtime_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(motionRuntime_tests test_motion_runtime.cpp)
target_link_libraries(motionRuntime_tests PRIVATE motionRuntime::motionRuntime)
target_compile_options(motionRuntime_tests
    PRIVATE ${_motion_runtime_assertions})
add_test(NAME motionRuntime_unit COMMAND motionRuntime_tests)

# Motion Phase D: live-capture intake, the recorded trace format, and replay.
add_executable(motionRuntime_liveCapture_tests test_live_capture.cpp)
target_link_libraries(motionRuntime_liveCapture_tests
    PRIVATE motionRuntime::motionRuntime)
target_compile_options(motionRuntime_liveCapture_tests
    PRIVATE ${_motion_runtime_assertions})
add_test(NAME motionRuntime_liveCapture
         COMMAND motionRuntime_liveCapture_tests)

set(_motion_runtime_tests motionRuntime_unit motionRuntime_liveCapture)

# The corpus is Motion Phase D's reproducibility contract: every committed
# trace must parse, and must survive a read/write round trip byte-identically,
# or every golden comparison downstream of it is meaningless. The same test
# binary takes the corpus directory as an argument -- deliberately, rather than
# referencing a target from another directory, because a standalone
# `ost plugin build libs/motionRuntime` has no such target and would fail at
# generate time.
set(_motion_runtime_corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
if(EXISTS "${_motion_runtime_corpus}")
    add_test(NAME motionRuntime_corpus
        COMMAND motionRuntime_liveCapture_tests "${_motion_runtime_corpus}")
    list(APPEND _motion_runtime_tests motionRuntime_corpus)
endif()

if(WIN32)
    if(TARGET gf)
        set(_motion_runtime_gf gf)
    else()
        set(_motion_runtime_gf pxr::gf)
    endif()
    # motionRuntime is a static library, but it calls exported Gf entry points
    # (GfSlerp, quaternion multiply), so the test executable loads usd_gf.dll at
    # run time. That DLL is in the runtime's lib/ and pulls in usd_tf.dll ->
    # tbb.dll from bin/ plus python3xx.dll from the Python install. An activated
    # `ost` session already has all three on PATH; a plain-CMake `ctest` run does
    # not, and the test exits 0xC0000135 (DLL not found) without them.
    set(_motion_runtime_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:motionRuntime>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_motion_runtime_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_motion_runtime_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_motion_runtime_python_dir
                               "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _motion_runtime_test_path
             "PATH=path_list_prepend:${_motion_runtime_python_dir}")
    endif()
    set_tests_properties(${_motion_runtime_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "${_motion_runtime_test_path}")
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME motionRuntime_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:motionRuntime>")

    # motionRuntime_corpus proves the committed traces are canonical for the
    # C++ writer; this proves they still match the generator that authored
    # them. Without it a hand-edited fixture would stay green while the
    # generator no longer reproduces it, and the corpus would quietly become
    # unregenerable.
    if(EXISTS "${_motion_runtime_corpus}")
        add_test(NAME motionRuntime_traceGen
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_traces.py"
                --check --output "${_motion_runtime_corpus}")
    endif()
endif()
