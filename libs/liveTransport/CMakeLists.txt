# SPDX-License-Identifier: Apache-2.0
#
# liveTransport — the live half's shared leaf.
#
# A plain static library, not a plugin bundle: it registers nothing with OpenUSD
# and ships no plugInfo.json (WORKSPACE.md §1). Its edge set is **empty** — not
# short, none — and that is a measurement rather than an intention: the six
# files it was extracted from include their own headers and the standard library
# and nothing else. `find_package` therefore appears nowhere below, and its
# arrival would be a contract change (WORKSPACE.md §2).
#
# It is outside the aggregate product, on the *adapter* side of §5's split
# though it carries no product name. §5's reader test is producer-neutral **and**
# opens nothing; this library satisfies the first clause exactly as motionBvh
# does and is still out, because the product would acquire I/O.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _live_transport_version LIMIT_COUNT 1)
    string(STRIP "${_live_transport_version}" _live_transport_version)
else()
    set(_live_transport_version "0.7.0")
endif()

project(liveTransport
    VERSION ${_live_transport_version}
    DESCRIPTION "Shared UDP receiver, packet-capture format and diagnostic vehicle for live adapters"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED LIVETRANSPORT_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_live_transport_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_live_transport_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(LIVETRANSPORT_BUILD_TESTS "Build liveTransport tests"
           ${_live_transport_tests_default})
endif()

add_library(liveTransport STATIC
    src/Diagnostics.cpp
    src/PacketCapture.cpp
    src/UdpReceiver.cpp)
add_library(liveTransport::liveTransport ALIAS liveTransport)
set_target_properties(liveTransport PROPERTIES
    EXPORT_NAME liveTransport
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(liveTransport PUBLIC cxx_std_17)
target_compile_definitions(liveTransport PUBLIC LIVETRANSPORT_STATIC)
target_include_directories(liveTransport
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        # src/ is on the private include path for PollTimeout.h, which is this
        # library's one internal header and the reason OSC-2 could pay a debt
        # OSC-1 could not: an adapter cannot hold one without diverging from its
        # sibling, and a library can. It is deliberately not installed.
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

# The platform's own transport and threading primitives, which are not a
# dependency direction: WORKSPACE.md §2 constrains which *workspace* libraries
# this leaf may reach, and its answer there is "none". `tests/check_boundaries.py`
# allows exactly these two names and nothing else, so the gate still fails on a
# workspace edge.
#
# Threads arrives with `DatagramQueue`, whose mutex is the whole of this
# library's answer to motion policy §11.4 (UdpReceiver.h). libstdc++ needs the
# link even for a std::mutex that is never contended.
if(WIN32)
    target_link_libraries(liveTransport PUBLIC ws2_32)
else()
    # Ask for `-pthread` rather than a bare `-lpthread`: on GCC the flag also
    # affects code generation, and a target that only gets the library links but
    # is not built for threads.
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(liveTransport PUBLIC Threads::Threads)
endif()

if(MSVC)
    # PUBLIC for the same reason the adapters use it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(liveTransport PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS liveTransport
    EXPORT liveTransportTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/liveTransportConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/liveTransportConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/liveTransport")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/liveTransportConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT liveTransportTargets
    FILE liveTransportTargets.cmake
    NAMESPACE liveTransport::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/liveTransport")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/liveTransportConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/liveTransportConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/liveTransport")

if(LIVETRANSPORT_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
