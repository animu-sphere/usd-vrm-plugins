# SPDX-License-Identifier: Apache-2.0
#
# motionTracking — which tracker is which body region, and nothing below or
# above that.
#
# A plain static library, not a plugin bundle: it registers nothing with OpenUSD
# and ships no plugInfo.json (WORKSPACE.md §1). Its allowed edge set is empty,
# and it is the third such leaf here with a different reason for it —
# `liveTransport` is empty because it must not learn a protocol, `osc` because a
# decoder needs nothing a transport owns, and this one because assignment maps
# one vocabulary it owns onto another. `motionCore` is the edge it most looks
# like it should have and must not: a region that resolved to a `HumanBone`
# would make assignment a lookup and leave the solve nothing to do (§2, which
# forbids the alias by name).
#
# Unlike the two leaves above it, this library is on the **product** side of
# §5's split. Nothing in the product links it today — its only consumer is an
# adapter's CLI — and nothing has to change for one to.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _motiontracking_version LIMIT_COUNT 1)
    string(STRIP "${_motiontracking_version}" _motiontracking_version)
else()
    set(_motiontracking_version "0.7.0")
endif()

project(motionTracking
    VERSION ${_motiontracking_version}
    DESCRIPTION "Tracker-to-body-region assignment, generic and outside every adapter"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONTRACKING_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_motiontracking_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_motiontracking_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONTRACKING_BUILD_TESTS "Build motionTracking tests"
           ${_motiontracking_tests_default})
endif()

add_library(motionTracking STATIC
    src/TrackerAssignment.cpp
    src/TrackerRegion.cpp)
add_library(motionTracking::motionTracking ALIAS motionTracking)
set_target_properties(motionTracking PROPERTIES
    EXPORT_NAME motionTracking
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionTracking PUBLIC cxx_std_17)
target_compile_definitions(motionTracking PUBLIC MOTIONTRACKING_STATIC)
target_include_directories(motionTracking
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

# No target_link_libraries at all, and the absence is the measurement — not even
# a platform primitive. This library owns two vocabularies and a policy over
# them; it opens nothing, waits for nothing, and holds no geometric type.

if(MSVC)
    # PUBLIC for the same reason every leaf here uses it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(motionTracking PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS motionTracking
    EXPORT motionTrackingTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/motionTrackingConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/motionTrackingConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionTracking")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/motionTrackingConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT motionTrackingTargets
    FILE motionTrackingTargets.cmake
    NAMESPACE motionTracking::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionTracking")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/motionTrackingConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/motionTrackingConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionTracking")

if(MOTIONTRACKING_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
