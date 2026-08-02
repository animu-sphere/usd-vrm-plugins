# SPDX-License-Identifier: Apache-2.0
#
# vrmAdapterVmc — the generic real-time input leaf.
#
# A plain static library, not a plugin bundle: it registers nothing with
# OpenUSD and ships no plugInfo.json (WORKSPACE.md §1). It links motionCore and
# motionRuntime and nothing else; the CLI that may retarget and author a stage
# is a separate tool under tools/, added when it is written.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../VERSION"
         _vrm_adapter_vmc_version LIMIT_COUNT 1)
    string(STRIP "${_vrm_adapter_vmc_version}" _vrm_adapter_vmc_version)
else()
    set(_vrm_adapter_vmc_version "0.5.0")
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

add_library(vrmAdapterVmc STATIC
    src/Diagnostics.cpp
    src/FrameAssembler.cpp
    src/LiveSource.cpp
    src/OscPacket.cpp
    src/PacketCapture.cpp
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
        motionRuntime::motionRuntime)

# The platform's own transport and threading primitives, which are not a
# dependency direction: WORKSPACE.md §2 constrains which *workspace* libraries an
# adapter may reach, and motion policy §8.2 puts the socket inside the adapter on
# purpose. `tests/check_boundaries.py` allows exactly these two names and nothing
# else, so the gate still fails on a workspace edge.
#
# Threads arrives with `DatagramQueue`, whose mutex is the whole of this
# adapter's answer to motion policy §11.4 (UdpReceiver.h). libstdc++ needs the
# link even for a std::mutex that is never contended.
if(WIN32)
    target_link_libraries(vrmAdapterVmc PUBLIC ws2_32)
else()
    # Ask for `-pthread` rather than a bare `-lpthread`: on GCC the flag also
    # affects code generation, and a target that only gets the library links but
    # is not built for threads.
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(vrmAdapterVmc PUBLIC Threads::Threads)
endif()

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
