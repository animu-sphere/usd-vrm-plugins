# SPDX-License-Identifier: Apache-2.0
#
# motionTracking — which tracker is which body region, and nothing below or
# above that.
#
# A plain static library, not a plugin bundle: it registers nothing with OpenUSD
# and ships no plugInfo.json (WORKSPACE.md §1). Its allowed edge set was empty
# until VRC-5 and is now one line — `motionCore`, taken by the solve and by
# nothing else: a solve produces a `HumanoidPose`, which is that library's type.
#
# The two halves are held apart by the boundary check rather than by two CMake
# targets, and that is a deliberate choice. Splitting the assignment half into
# its own library would make the rule a link line and cost every consumer a
# second package, while the rule that actually matters — a `TrackerRegion` is
# never a `HumanBone` — has no link line to fail on anyway (§2, which forbids
# the alias by name). So the check reads the sources, per file.
#
# Unlike the two leaves above it, this library is on the **product** side of
# §5's split, and nothing links it at all yet: §2's permission is
# `adapters/*/tools/* -> motionTracking` and no CLI has taken it (VRC-6). Nothing
# has to change for a product tool to link it either.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _motiontracking_version LIMIT_COUNT 1)
    string(STRIP "${_motiontracking_version}" _motiontracking_version)
else()
    set(_motiontracking_version "0.8.0")
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

# The solve names OpenUSD's Gf value types and `motionCore`'s humanoid
# vocabulary; nothing here has a USD stage, Sdf, plug or file-format dependency,
# exactly as motionCore and motionRuntime do not.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UsdVrmOpenUsd.cmake")

# In the composed workspace build motionCore is already an in-tree target; a
# standalone configure resolves its installed package.
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()

add_library(motionTracking STATIC
    src/TrackerAssignment.cpp
    src/TrackerObservation.cpp
    src/TrackerRegion.cpp
    src/TrackerSolve.cpp)
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

# One link line, and it is the whole edge set. No platform primitive and no
# transport: this library opens nothing and waits for nothing, and what it
# gained at VRC-5 is a value type rather than an ability. `motionCore` carries
# the Gf target with it, so there is no second line for OpenUSD.
target_link_libraries(motionTracking PUBLIC motionCore::motionCore)

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
