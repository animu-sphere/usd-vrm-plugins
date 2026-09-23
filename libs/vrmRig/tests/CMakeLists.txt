# SPDX-License-Identifier: Apache-2.0

add_executable(vrmRig_tests test_vrm_rig.cpp)
target_link_libraries(vrmRig_tests PRIVATE vrmRig::vrmRig)
# This suite checks with `assert()`, which NDEBUG compiles away -- and Release is
# the default build type, so a Release build would pass every check vacuously.
target_compile_options(vrmRig_tests
    PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
add_test(NAME vrmRig_unit COMMAND vrmRig_tests)

if(WIN32)
    if(TARGET gf)
        set(_vrm_rig_gf gf)
    else()
        set(_vrm_rig_gf pxr::gf)
    endif()
    # Static library, but exported Gf entry points still pull usd_gf.dll and its
    # own dependencies in at run time, and the runtime's bin/ is not on PATH.
    set(_vrm_rig_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmRig>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_rig_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_rig_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_vrm_rig_python_dir
                               "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _vrm_rig_test_path
             "PATH=path_list_prepend:${_vrm_rig_python_dir}")
    endif()
    set_tests_properties(vrmRig_unit PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vrm_rig_test_path}")
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME vrmRig_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:vrmRig>")
endif()

# The OpenExec plan's P0-2 label, on every test this directory registers. The
# root suite's workspace_ctest_labels fails if this directory contributes none.
get_property(_vrm_rig_labelled DIRECTORY PROPERTY TESTS)
set_property(TEST ${_vrm_rig_labelled}
    APPEND PROPERTY LABELS "motion.retarget")
