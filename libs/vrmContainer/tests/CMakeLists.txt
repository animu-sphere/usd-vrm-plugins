# SPDX-License-Identifier: Apache-2.0

add_executable(vrmContainer_tests test_glb_container.cpp)
target_link_libraries(vrmContainer_tests PRIVATE vrmContainer::vrmContainer)
add_test(NAME vrmContainer_unit COMMAND vrmContainer_tests)
if(WIN32)
    set_tests_properties(vrmContainer_unit PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer>")
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME vrmContainer_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:vrmContainer>")
endif()
