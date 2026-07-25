# SPDX-License-Identifier: Apache-2.0

add_executable(motionRuntime_tests test_motion_runtime.cpp)
target_link_libraries(motionRuntime_tests PRIVATE motionRuntime::motionRuntime)
add_test(NAME motionRuntime_unit COMMAND motionRuntime_tests)

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
    set_tests_properties(motionRuntime_unit PROPERTIES
        ENVIRONMENT_MODIFICATION "${_motion_runtime_test_path}")
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME motionRuntime_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:motionRuntime>")
endif()
