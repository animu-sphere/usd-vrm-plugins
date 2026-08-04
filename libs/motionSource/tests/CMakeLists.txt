# SPDX-License-Identifier: Apache-2.0

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would print
# "verified" and return 0 without having checked anything. Undefine it for the
# test targets only. See libs/motionRuntime/tests/CMakeLists.txt, which hit this
# first.
set(_motion_source_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

# The source rig: its queries, its equality, and the invariants a hand-built
# skeleton can break.
add_executable(motionSource_skeleton_tests test_source_skeleton.cpp)
# The source animation: the angle vocabulary, timing, and the checks that keep a
# track index meaning a joint index.
add_executable(motionSource_animation_tests test_source_animation.cpp)
# Provenance, and the one narrowing crossing into canonical motion.
add_executable(motionSource_provenance_tests test_source_provenance.cpp)

set(_motion_source_tests
    motionSource_skeleton_tests
    motionSource_animation_tests
    motionSource_provenance_tests)

foreach(_motion_source_test ${_motion_source_tests})
    target_link_libraries(${_motion_source_test} PRIVATE
                          motionSource::motionSource)
    target_compile_options(${_motion_source_test}
                           PRIVATE ${_motion_source_assertions})
    if(MSVC)
        # The headers cite the contracts by their `§` sign, and MSVC reads
        # source in the *system* code page unless told otherwise -- so a
        # Japanese-Windows build warns C4819 on every include without this.
        target_compile_options(${_motion_source_test} PRIVATE /utf-8)
    endif()
endforeach()

add_test(NAME motionSource_skeleton COMMAND motionSource_skeleton_tests)
add_test(NAME motionSource_animation COMMAND motionSource_animation_tests)
add_test(NAME motionSource_provenance COMMAND motionSource_provenance_tests)

if(WIN32)
    if(TARGET gf)
        set(_motion_source_gf gf)
    else()
        set(_motion_source_gf pxr::gf)
    endif()
    # This library names no Gf type, but it links motionCore, which does -- so
    # the test executables can still import usd_gf.dll, which pulls usd_tf.dll ->
    # tbb.dll from bin/ plus python3xx.dll from the Python install. An activated
    # `ost` session has all three on PATH; a plain-CMake `ctest` run does not,
    # and the test exits 0xC0000135 (DLL not found) without them.
    set(_motion_source_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:motionSource>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_motion_source_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_motion_source_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_motion_source_python_dir
                               "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _motion_source_test_path
             "PATH=path_list_prepend:${_motion_source_python_dir}")
    endif()
    set_tests_properties(motionSource_skeleton motionSource_animation
                         motionSource_provenance PROPERTIES
        ENVIRONMENT_MODIFICATION "${_motion_source_test_path}")
endif()

if(Python3_Interpreter_FOUND)
    # Source and CMake only, and no binary argument, unlike every other boundary
    # check in this workspace: this library's one permitted dependency is
    # motionCore, whose Gf value types make an "imports no OpenUSD" claim false
    # by design. check_boundaries.py says so in place of pretending otherwise.
    add_test(NAME motionSource_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()
