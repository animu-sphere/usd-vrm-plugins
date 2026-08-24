# SPDX-License-Identifier: Apache-2.0
#
# vrmAdapterVrchatOsc — the tracker input leaf.
#
# A plain static library, not a plugin bundle: it registers nothing with OpenUSD
# and ships no plugInfo.json (WORKSPACE.md §1). The CLI that may retarget and
# author a stage is a separate tool under tools/.
#
# ## One link line, and what is not on it
#
# `liveTransport`, and nothing else. WORKSPACE.md §2 permits an adapter three
# edges; the other two — motionCore and motionRuntime — are what an adapter
# takes when it has canonical values to produce, and this milestone produces
# none: VRC-0 is the scaffold, the frozen code set and a recorder, with no
# semantic decoder anywhere in it (osc-and-vrchat-trackers.md §9, VRC-0).
#
# The consequence is worth stating because it is visible in every test below:
# this adapter's closure contains no OpenUSD at all, so nothing here needs Gf's
# DLL directory on PATH and the boundary check's allowlist of value-type
# libraries is a check that the closure stays empty rather than a check that it
# stays small. That changes when a decoder arrives, and the change will be
# legible as a link line growing rather than as a comment being edited.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../VERSION"
         _vrm_adapter_vrchat_osc_version LIMIT_COUNT 1)
    string(STRIP "${_vrm_adapter_vrchat_osc_version}"
           _vrm_adapter_vrchat_osc_version)
else()
    set(_vrm_adapter_vrchat_osc_version "0.7.0")
endif()

project(vrmAdapterVrchatOsc
    VERSION ${_vrm_adapter_vrchat_osc_version}
    DESCRIPTION "VRChat OSC tracker input adapter for the canonical motion pipeline"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED VRMADAPTERVRCHATOSC_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_vrm_adapter_vrchat_osc_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_vrm_adapter_vrchat_osc_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(VRMADAPTERVRCHATOSC_BUILD_TESTS "Build vrmAdapterVrchatOsc tests"
           ${_vrm_adapter_vrchat_osc_tests_default})
endif()

# In the composed workspace build this is already an in-tree target; a
# standalone configure of this directory resolves its installed package.
if(NOT TARGET liveTransport::liveTransport)
    find_package(liveTransport REQUIRED CONFIG)
endif()

# Two files, and both of them exist because a shared library may not hold what
# is in them: a code table, and the map from a transport event to one of its
# rows (WORKSPACE.md §2's diagnostic split). The capture format contributes a
# third thing of the same kind and it is one string, so it lives in a header.
add_library(vrmAdapterVrchatOsc STATIC
    src/Diagnostics.cpp
    src/UdpReceiver.cpp)
add_library(vrmAdapterVrchatOsc::vrmAdapterVrchatOsc ALIAS vrmAdapterVrchatOsc)
set_target_properties(vrmAdapterVrchatOsc PROPERTIES
    EXPORT_NAME vrmAdapterVrchatOsc
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(vrmAdapterVrchatOsc PUBLIC cxx_std_17)
target_compile_definitions(vrmAdapterVrchatOsc PUBLIC VRMADAPTERVRCHATOSC_STATIC)
target_include_directories(vrmAdapterVrchatOsc
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
# The one edge this library has. The platform's socket and its threading
# primitive arrive behind it, as they do for both siblings since OSC-2.
target_link_libraries(vrmAdapterVrchatOsc PUBLIC liveTransport::liveTransport)

if(MSVC)
    # PUBLIC for the same reason the siblings use it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(vrmAdapterVrchatOsc PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS vrmAdapterVrchatOsc
    EXPORT vrmAdapterVrchatOscTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vrmAdapterVrchatOscConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterVrchatOscConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmAdapterVrchatOsc")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterVrchatOscConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT vrmAdapterVrchatOscTargets
    FILE vrmAdapterVrchatOscTargets.cmake
    NAMESPACE vrmAdapterVrchatOsc::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmAdapterVrchatOsc")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterVrchatOscConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmAdapterVrchatOscConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmAdapterVrchatOsc")

if(VRMADAPTERVRCHATOSC_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()

# The adapter's CLI, which ships in the same artifact as the library
# (WORKSPACE.md §5) and is where §2's adapter-tool permissions
# begin. Added after the library target it links, and unconditionally: an
# `if(EXISTS ...)` around a file the build requires can only ever mask a rename
# or a bad checkout, and this repository has already paid once for a test that
# silently stopped registering.
add_subdirectory(tools/vrchatOscRecord)
