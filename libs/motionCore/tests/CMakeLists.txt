# SPDX-License-Identifier: Apache-2.0

add_executable(motionCore_tests test_humanoid.cpp)
add_executable(motionCore_compare_tests test_compare.cpp)
# These suites check with `assert()`, which NDEBUG compiles away -- and Release
# is the default build type. See libs/motionRuntime/tests/CMakeLists.txt for the
# full rationale.
foreach(_motion_core_test motionCore_tests motionCore_compare_tests)
    target_link_libraries(${_motion_core_test} PRIVATE motionCore::motionCore)
    target_compile_options(${_motion_core_test}
        PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
endforeach()
add_test(NAME motionCore_unit COMMAND motionCore_tests)
add_test(NAME motionCore_compare COMMAND motionCore_compare_tests)
if(WIN32)
    set_tests_properties(motionCore_unit motionCore_compare PROPERTIES
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
