# SPDX-License-Identifier: Apache-2.0
#
# vrmAdapterMocopi — the native live input leaf for one capture product.
#
# A plain static library, not a plugin bundle: it registers nothing with
# OpenUSD and ships no plugInfo.json (WORKSPACE.md §1). It links motionCore and
# motionRuntime and nothing else; the CLI that may retarget and author a stage
# is a separate tool under tools/, added when it is written.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../VERSION"
         _vrm_adapter_mocopi_version LIMIT_COUNT 1)
    string(STRIP "${_vrm_adapter_mocopi_version}" _vrm_adapter_mocopi_version)
else()
    set(_vrm_adapter_mocopi_version "0.6.0")
endif()

project(vrmAdapterMocopi
    VERSION ${_vrm_adapter_mocopi_version}
    DESCRIPTION "Native live capture input adapter for the canonical motion pipeline"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED VRMADAPTERMOCOPI_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_vrm_adapter_mocopi_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_vrm_adapter_mocopi_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(VRMADAPTERMOCOPI_BUILD_TESTS "Build vrmAdapterMocopi tests"
           ${_vrm_adapter_mocopi_tests_default})
endif()

# OpenUSD arrives only through motionCore's Gf value types. The pin travels with
# the find_package call rather than with the root project, because a standalone
# configure of this directory never composes the root (see cmake/UsdVrmOpenUsd.cmake).
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/UsdVrmOpenUsd.cmake")

# In the composed workspace build both are already in-tree targets; a standalone
# configure resolves their installed packages.
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()
if(NOT TARGET motionRuntime::motionRuntime)
    find_package(motionRuntime REQUIRED CONFIG)
endif()

add_library(vrmAdapterMocopi STATIC
    src/Diagnostics.cpp
    src/FrameAssembler.cpp
    src/MotionPacket.cpp
    src/PacketCapture.cpp
    src/PacketChunk.cpp
    src/SkeletonMap.cpp
    src/UdpReceiver.cpp)
add_library(vrmAdapterMocopi::vrmAdapterMocopi ALIAS vrmAdapterMocopi)
set_target_properties(vrmAdapterMocopi PROPERTIES
    EXPORT_NAME vrmAdapterMocopi
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(vrmAdapterMocopi PUBLIC cxx_std_17)
target_compile_definitions(vrmAdapterMocopi PUBLIC VRMADAPTERMOCOPI_STATIC)
target_include_directories(vrmAdapterMocopi
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
# The two edges WORKSPACE.md §2 permits.
target_link_libraries(vrmAdapterMocopi
    PUBLIC
        motionCore::motionCore
        motionRuntime::motionRuntime)

# The platform's own transport, which is not a dependency direction:
# WORKSPACE.md §2 constrains which *workspace* libraries an adapter may reach,
# and motion policy §8.2 puts the socket inside the adapter on purpose.
# `tests/check_boundaries.py` allows exactly this one extra name, so the gate
# still fails on a workspace edge.
#
# This is the change that argues for it, which is what the previous revision of
# this comment asked for — the receiver landed here rather than last because
# this protocol has no published specification, so a socket is the only way to
# obtain the bytes every layer above it would be built from (UdpReceiver.h).
#
# One name and not the sibling's two: `Threads` arrives there with a
# `DatagramQueue` this adapter deliberately does not have, and asking for a
# threading primitive no source file uses would reserve exactly what this
# allowlist exists to prevent.
if(WIN32)
    target_link_libraries(vrmAdapterMocopi PUBLIC ws2_32)
endif()

if(MSVC)
    # PUBLIC for the same reason motionRuntime uses it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(vrmAdapterMocopi PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS vrmAdapterMocopi
    EXPORT vrmAdapterMocopiTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vrmAdapterMocopiConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterMocopiConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmAdapterMocopi")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterMocopiConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT vrmAdapterMocopiTargets
    FILE vrmAdapterMocopiTargets.cmake
    NAMESPACE vrmAdapterMocopi::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmAdapterMocopi")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterMocopiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterMocopiConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmAdapterMocopi")

if(VRMADAPTERMOCOPI_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()

# The adapter's CLI, which ships in the same artifact as the library
# (WORKSPACE.md §5). Added after the library and its tests: the tool links the
# target above, and adding it before would name a target that does not exist
# yet. It is where WORKSPACE.md §2's adapter-tool permissions begin -- a tool may
# drive `vrmRetarget` and author a stage where the library may not -- which is
# also why `tests/check_boundaries.py` scans include/ and src/ only.
#
# Unconditional, unlike the sibling adapter's guarded call. That guard was
# written when the tool did not exist yet; once it does, an `if(EXISTS ...)`
# around a file the build requires can never be false in a correct tree, so its
# only reachable effect is to mask a rename or a bad checkout -- the configure
# would succeed, the tool would not be built, and ctest would report 100% with
# `mocopi_record_inspect`, `_loopback` and `_ipv6` simply absent. This repository
# has already paid for a test that silently stopped registering, which is why
# `ctest -N` counts are checked; a missing file here is a configure error.
add_subdirectory(tools/mocopiRecord)
