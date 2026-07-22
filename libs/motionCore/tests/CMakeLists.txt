# SPDX-License-Identifier: Apache-2.0

add_executable(motionCore_tests test_humanoid.cpp)
target_link_libraries(motionCore_tests PRIVATE motionCore::motionCore)
add_test(NAME motionCore_unit COMMAND motionCore_tests)
if(WIN32)
    set_tests_properties(motionCore_unit PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:motionCore>")
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME motionCore_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:motionCore>")
endif()
