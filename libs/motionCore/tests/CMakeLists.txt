# SPDX-License-Identifier: Apache-2.0

add_executable(motionCore_tests test_humanoid.cpp)
target_link_libraries(motionCore_tests PRIVATE motionCore::motionCore)
# This suite checks with `assert()`, which NDEBUG compiles away -- and Release is
# the default build type. See libs/motionRuntime/tests/CMakeLists.txt for the
# full rationale.
target_compile_options(motionCore_tests
    PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)
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
