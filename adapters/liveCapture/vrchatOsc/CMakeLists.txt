# SPDX-License-Identifier: Apache-2.0
#
# vrmAdapterVrchatOsc — the tracker input leaf.
#
# A plain static library, not a plugin bundle: it registers nothing with OpenUSD
# and ships no plugInfo.json (WORKSPACE.md §1). The CLI that may retarget and
# author a stage is a separate tool under tools/.
#
# ## Three link lines, and the one that is still not here
#
# `liveTransport`, `osc` and — since VRC-3 — `motionCore`. WORKSPACE.md §2
# permits an adapter four edges, and the fourth, `motionRuntime`, is what an
# adapter takes when it produces a *pose*; this one still produces none. What
# VRC-3 added is one layer below that: a tracker observation in the canonical
# basis, which is a value the rest of this repository can read and therefore a
# `GfVec3f` and a `GfQuatf` (osc-and-vrchat-trackers.md §5, §9).
#
# **The edge is taken for the value types**, and no `motion::` type is named in
# this library yet. That is the contract's route rather than a convenience:
# §2 gives an adapter `motionCore` and does not give it `pxr`, so Gf arrives
# through the library whose contract already carries it, and the boundary check
# holds the allowlist that says which OpenUSD libraries may follow.
#
# **The closure grows here, exactly once, and the check is what says so.** VRC-0
# recorded that this adapter's binaries imported no OpenUSD at all and predicted
# that a decoder producing a canonical value would change it; VRC-2 arrived and
# it did not, because `osc` links nothing. VRC-3 is the one that does: the test
# executables now load usd_gf and its dependencies, which is why tests/ gained a
# PATH modification on Windows, and the boundary check's value-type allowlist
# stops being a check that the closure is *empty* and starts being the check it
# was written as.

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
if(NOT TARGET osc::osc)
    find_package(osc REQUIRED CONFIG)
endif()
# The value types, by the route WORKSPACE.md §2 gives an adapter: Gf arrives
# through motionCore and never through a direct `find_package(pxr)` here.
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()

# Four files. Two of them exist because a shared library may not hold what is
# in them: a code table, and the map from a transport event to one of its rows
# (WORKSPACE.md §2's diagnostic split). The capture format contributes a third
# thing of the same kind and it is one string, so it lives in a header.
#
# `AddressInventory.cpp` is the first file here that is neither of those: it is
# this adapter reading a session rather than describing itself, and it is what
# made `osc` a library — a decoder with one caller is a decoder shaped like that
# caller (osc-and-vrchat-trackers.md §3.1). It carries the same diagnostic split
# as the other two, in its one refusal path.
# `TrackerMessage.cpp` is the fourth, and it is the first file here that gives a
# byte a meaning: an address becomes a tracker identity and a channel, and three
# floats become three floats this adapter is willing to name (VRC-2). It is
# still not a pose — position and rotation arrive on separate addresses and the
# window they belong to is VRC-4's — so the link line below is unchanged by it.
# `TrackingSpace.cpp` is the fifth and the first to produce a value outside this
# adapter's own vocabulary: the sender's axes into the canonical ones, measured
# from a labelled session rather than read off VRChat's documentation (VRC-3).
# It is the file the motionCore edge below exists for.
add_library(vrmAdapterVrchatOsc STATIC
    src/AddressInventory.cpp
    src/Diagnostics.cpp
    src/TrackerMessage.cpp
    src/TrackingSpace.cpp
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
# The three edges this library has. The platform's socket and its threading
# primitive arrive behind the first, as they do for both siblings since OSC-2;
# nothing at all arrives behind the second; and OpenUSD's value-type layer
# arrives behind the third, which is the change VRC-3 makes to this binary's
# closure. All three are PUBLIC because the installed headers name types from
# each.
target_link_libraries(vrmAdapterVrchatOsc
    PUBLIC
        liveTransport::liveTransport
        osc::osc
        motionCore::motionCore)

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
