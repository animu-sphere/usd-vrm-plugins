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
    set(_vrm_adapter_mocopi_version "0.8.0")
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
# The live half's shared leaf: the socket, the packet-capture format and the
# diagnostic vehicle, which this adapter and its sibling had written twice
# (WORKSPACE.md §2, osc-and-vrchat-trackers.md §3.2). Not a sibling edge and not
# a route to one -- it is a leaf under libs/ with an empty edge set of its own,
# and it is outside the aggregate product for the same reason an adapter is.
if(NOT TARGET liveTransport::liveTransport)
    find_package(liveTransport REQUIRED CONFIG)
endif()

# PacketCapture.cpp is gone rather than shrunk: the format's reader and writer
# are liveTransport's now, and what this adapter contributes to them is one
# string in a header. Diagnostics.cpp and UdpReceiver.cpp are what is left of
# two files that moved -- a code table and an event-to-code map, which are the
# two things a shared library may not hold.
add_library(vrmAdapterMocopi STATIC
    src/Diagnostics.cpp
    src/FrameAssembler.cpp
    src/LiveSource.cpp
    src/MotionPacket.cpp
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
# The three edges WORKSPACE.md §2 permits.
target_link_libraries(vrmAdapterMocopi
    PUBLIC
        motionCore::motionCore
        motionRuntime::motionRuntime
        liveTransport::liveTransport)

# No `ws2_32` here any more, and its absence is the clearest single measurement
# of what OSC-2 moved. Motion policy §8.2 still puts the socket inside the
# adapter *layer*; this adapter no longer opens one itself, so the platform's
# transport arrives through liveTransport's exported target instead.
# `tests/check_boundaries.py` keeps allowing it by name, because the permission
# is the contract's and not that file's to withdraw.
#
# `Threads::Threads` now arrives the same way, and that is a change in what this
# adapter *reserves* rather than in what it uses. The previous revision of this
# comment declined to ask for it because `DatagramQueue` is the sibling's and no
# source file here holds a mutex; that is still true, and the queue is still
# opt-in, but a shared library cannot offer one class its threading primitive and
# not another. Nothing here constructs a queue, so nothing here pays for one.

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
#
# The option is VRC-6's, and it is here for symmetry rather than for a need of
# this adapter's: `scripts/check_package_consumer.py` turns every package's CLI
# off while installing that package, because a consumer's prefix holds exactly
# the declared closure and a CLI's edges are the tool's rather than the
# library's. An option only one of three adapters defined would be a rule with
# an invisible exception.
option(VRMADAPTERMOCOPI_BUILD_TOOL
       "Build mocopi_record, the adapter's CLI" ON)
if(VRMADAPTERMOCOPI_BUILD_TOOL)
    add_subdirectory(tools/mocopiRecord)
endif()
