# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _motion_bvh_version LIMIT_COUNT 1)
    string(STRIP "${_motion_bvh_version}" _motion_bvh_version)
else()
    set(_motion_bvh_version "0.7.0")
endif()

project(motionBvh
    VERSION ${_motion_bvh_version}
    DESCRIPTION "BVH syntax model and parser, with no producer semantics"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONBVH_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_motion_bvh_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_motion_bvh_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONBVH_BUILD_TESTS "Build motionBvh tests"
           ${_motion_bvh_tests_default})
endif()

# No OpenUSD, not even Gf, and that is a statement rather than an omission: this
# layer reads three numbers out of an OFFSET line and does not know the file's
# handedness, up axis, or unit, so a value typed as a geometric vector would be
# an assertion it is in no position to make (BvhDocument.h).
#
# The one declared edge (WORKSPACE.md §2) arrives below with the extractor, and
# it is to motionSource alone. It is not a route to OpenUSD by another name:
# motionSource links motionCore, whose Gf value types this library still names
# nowhere, and motionBvh_boundaries checks the source rule that says so. What the
# edge does cost is the *binary* half of that check, and tests/CMakeLists.txt
# states what is left of it and why.
if(NOT TARGET motionSource::motionSource)
    find_package(motionSource REQUIRED CONFIG)
endif()

add_library(motionBvh STATIC
    src/BvhDocument.cpp
    src/BvhExtract.cpp
    src/BvhParser.cpp
    src/Diagnostics.cpp)
add_library(motionBvh::motionBvh ALIAS motionBvh)
set_target_properties(motionBvh PROPERTIES
    EXPORT_NAME motionBvh
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionBvh PUBLIC cxx_std_17)
target_compile_definitions(motionBvh PUBLIC MOTIONBVH_STATIC)
target_include_directories(motionBvh
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(motionBvh PUBLIC motionSource::motionSource)

if(MSVC)
    # PUBLIC rather than PRIVATE, unlike before the extractor: BvhExtract.h is
    # an installed header citing the roadmap by its `§` sign, so every consumer
    # that includes it needs the flag too or MSVC warns C4819 on a non-UTF-8
    # code page. This is motionSource's answer, for motionSource's reason.
    target_compile_options(motionBvh PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS motionBvh
    EXPORT motionBvhTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/motionBvhConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/motionBvhConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionBvh")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/motionBvhConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT motionBvhTargets
    FILE motionBvhTargets.cmake
    NAMESPACE motionBvh::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionBvh")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/motionBvhConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/motionBvhConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionBvh")

if(MOTIONBVH_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
