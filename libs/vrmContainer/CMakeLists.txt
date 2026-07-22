# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _vrm_container_version LIMIT_COUNT 1)
    string(STRIP "${_vrm_container_version}" _vrm_container_version)
else()
    set(_vrm_container_version "0.3.0")
endif()

project(vrmContainer
    VERSION ${_vrm_container_version}
    DESCRIPTION "Memory-safe GLB container views for VRM plugins"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED VRMCONTAINER_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_vrm_container_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_vrm_container_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(VRMCONTAINER_BUILD_TESTS "Build vrmContainer tests"
           ${_vrm_container_tests_default})
endif()

add_library(vrmContainer SHARED
    src/GlbContainer.cpp)
add_library(vrmContainer::vrmContainer ALIAS vrmContainer)
set_target_properties(vrmContainer PROPERTIES
    EXPORT_NAME vrmContainer
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

target_compile_features(vrmContainer PUBLIC cxx_std_17)
target_compile_definitions(vrmContainer PRIVATE VRMCONTAINER_EXPORTS)
target_include_directories(vrmContainer
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

if(MSVC)
    target_compile_options(vrmContainer PRIVATE /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS vrmContainer
    EXPORT vrmContainerTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vrmContainerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmContainerConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmContainer")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vrmContainerConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT vrmContainerTargets
    FILE vrmContainerTargets.cmake
    NAMESPACE vrmContainer::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmContainer")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vrmContainerConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmContainerConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmContainer")

if(VRMCONTAINER_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_custom_command(TARGET vrmContainer POST_BUILD
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_boundaries.py"
                "${CMAKE_CURRENT_SOURCE_DIR}"
                "$<TARGET_FILE:vrmContainer>"
            COMMENT "Checking vrmContainer source and binary boundaries"
            VERBATIM)
    endif()
    add_subdirectory(tests)
endif()
