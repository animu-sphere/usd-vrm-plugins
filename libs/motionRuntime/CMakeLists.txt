# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _motion_runtime_version LIMIT_COUNT 1)
    string(STRIP "${_motion_runtime_version}" _motion_runtime_version)
else()
    set(_motion_runtime_version "0.7.0")
endif()

project(motionRuntime
    VERSION ${_motion_runtime_version}
    DESCRIPTION "OpenExec-independent humanoid motion runtime"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONRUNTIME_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_motion_runtime_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_motion_runtime_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONRUNTIME_BUILD_TESTS "Build motionRuntime tests"
           ${_motion_runtime_tests_default})
endif()

# Like motionCore, motionRuntime uses only OpenUSD's Gf value types. It has no
# USD stage, Sdf, plug, or file-format dependency (WORKSPACE.md §2).
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UsdVrmOpenUsd.cmake")

# In the composed workspace build motionCore is already an in-tree target; a
# standalone `ost plugin build`-style configure resolves its installed package.
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()

add_library(motionRuntime STATIC
    src/Blend.cpp
    src/CaptureTrace.cpp
    src/Filter.cpp
    src/Interpolation.cpp
    src/LiveCaptureSource.cpp
    src/MotionSource.cpp
    src/PoseBuffer.cpp
    src/Recorder.cpp
    src/Resample.cpp
    src/ReplaySender.cpp)
add_library(motionRuntime::motionRuntime ALIAS motionRuntime)
set_target_properties(motionRuntime PROPERTIES
    EXPORT_NAME motionRuntime
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionRuntime PUBLIC cxx_std_17)
target_compile_definitions(motionRuntime PUBLIC MOTIONRUNTIME_STATIC)
target_include_directories(motionRuntime
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(motionRuntime PUBLIC motionCore::motionCore)

if(MSVC)
    # PUBLIC, not PRIVATE: the installed headers carry UTF-8 (the §-references
    # into WORKSPACE.md), so every consumer that includes them needs the flag
    # too or MSVC warns C4819 on a non-UTF-8 code page.
    target_compile_options(motionRuntime PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS motionRuntime
    EXPORT motionRuntimeTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/motionRuntimeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/motionRuntimeConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionRuntime")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/motionRuntimeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT motionRuntimeTargets
    FILE motionRuntimeTargets.cmake
    NAMESPACE motionRuntime::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionRuntime")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/motionRuntimeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/motionRuntimeConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionRuntime")

if(MOTIONRUNTIME_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
