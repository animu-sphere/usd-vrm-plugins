# SPDX-License-Identifier: Apache-2.0

add_executable(vrmRetarget_tests test_vrm_retarget.cpp)
target_link_libraries(vrmRetarget_tests PRIVATE vrmRetarget::vrmRetarget)
add_test(NAME vrmRetarget_unit COMMAND vrmRetarget_tests)

if(WIN32)
    if(TARGET gf)
        set(_vrm_retarget_gf gf)
    else()
        set(_vrm_retarget_gf pxr::gf)
    endif()
    # Static library, but exported Gf entry points still pull usd_gf.dll and its
    # own dependencies in at run time; see the note in
    # libs/motionRuntime/tests/CMakeLists.txt.
    set(_vrm_retarget_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmRetarget>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_retarget_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_retarget_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_vrm_retarget_python_dir
                               "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _vrm_retarget_test_path
             "PATH=path_list_prepend:${_vrm_retarget_python_dir}")
    endif()
    set_tests_properties(vrmRetarget_unit PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vrm_retarget_test_path}")
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME vrmRetarget_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:vrmRetarget>")
endif()
