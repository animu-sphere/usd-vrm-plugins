# SPDX-License-Identifier: Apache-2.0
#
# vrmAdapterVmc — the generic real-time input leaf.
#
# A plain static library, not a plugin bundle: it registers nothing with
# OpenUSD and ships no plugInfo.json (WORKSPACE.md §1). It links motionCore,
# motionRuntime and liveTransport and nothing else; the CLI that may retarget
# and author a stage is a separate tool under tools/.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../VERSION"
         _vrm_adapter_vmc_version LIMIT_COUNT 1)
    string(STRIP "${_vrm_adapter_vmc_version}" _vrm_adapter_vmc_version)
else()
    set(_vrm_adapter_vmc_version "0.7.0")
endif()

project(vrmAdapterVmc
    VERSION ${_vrm_adapter_vmc_version}
    DESCRIPTION "VMC Protocol input adapter for the canonical motion pipeline"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED VRMADAPTERVMC_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_vrm_adapter_vmc_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_vrm_adapter_vmc_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(VRMADAPTERVMC_BUILD_TESTS "Build vrmAdapterVmc tests"
           ${_vrm_adapter_vmc_tests_default})
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
# The OSC wire format, which this adapter held alone until a second consumer
# proved the surface neutral (WORKSPACE.md §2, osc-and-vrchat-trackers.md §3.1).
# A sibling of `liveTransport` rather than a layer of it: §2 forbids an edge
# between those two in both directions, so this adapter is the place they meet.
if(NOT TARGET osc::osc)
    find_package(osc REQUIRED CONFIG)
endif()

# PacketCapture.cpp is gone rather than shrunk: the format's reader and writer
# are liveTransport's now, and what this adapter contributes to them is one
# string in a header. Diagnostics.cpp, UdpReceiver.cpp and OscPacket.cpp are
# what is left of three files that moved -- a code table, an event-to-code map,
# and a refusal-to-code map, which are the three things a shared library may not
# hold.
add_library(vrmAdapterVmc STATIC
    src/Diagnostics.cpp
    src/FrameAssembler.cpp
    src/LiveSource.cpp
    src/OscPacket.cpp
    src/SkeletonMap.cpp
    src/UdpReceiver.cpp
    src/VmcMessage.cpp)
add_library(vrmAdapterVmc::vrmAdapterVmc ALIAS vrmAdapterVmc)
set_target_properties(vrmAdapterVmc PROPERTIES
    EXPORT_NAME vrmAdapterVmc
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(vrmAdapterVmc PUBLIC cxx_std_17)
target_compile_definitions(vrmAdapterVmc PUBLIC VRMADAPTERVMC_STATIC)
target_include_directories(vrmAdapterVmc
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(vrmAdapterVmc
    PUBLIC
        motionCore::motionCore
        motionRuntime::motionRuntime
        liveTransport::liveTransport
        osc::osc)

# No `ws2_32` and no `Threads::Threads` here any more, and their absence is the
# clearest single measurement of what OSC-2 moved. Motion policy §8.2 still puts
# the socket inside the adapter *layer*; this adapter no longer opens one itself,
# so the platform's transport and threading primitives travel with
# liveTransport's exported target instead. `tests/check_boundaries.py` allows
# them by name either way, so a future direct link still has to be argued for.

if(MSVC)
    # PUBLIC for the same reason motionRuntime uses it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(vrmAdapterVmc PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS vrmAdapterVmc
    EXPORT vrmAdapterVmcTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vrmAdapterVmcConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterVmcConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmAdapterVmc")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterVmcConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT vrmAdapterVmcTargets
    FILE vrmAdapterVmcTargets.cmake
    NAMESPACE vrmAdapterVmc::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmAdapterVmc")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterVmcConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterVmcConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmAdapterVmc")

if(VRMADAPTERVMC_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()

# The adapter's CLI, which ships in the same artifact as the library
# (WORKSPACE.md §5). Added after the library and its tests: the tool links the
# target above, and adding it before would name a target that does not exist
# yet. It is where WORKSPACE.md §2's adapter-tool permissions begin -- a tool
# may drive `vrmRetarget` and author a stage where the library may not -- which
# is also why `tests/check_boundaries.py` scans include/ and src/ only.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/vmcRecord/CMakeLists.txt")
    add_subdirectory(tools/vmcRecord)
endif()
