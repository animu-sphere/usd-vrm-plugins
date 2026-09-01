# SPDX-License-Identifier: Apache-2.0
#
# osc — the OSC 1.0 wire format, and nothing that uses it.
#
# A plain static library, not a plugin bundle: it registers nothing with OpenUSD
# and ships no plugInfo.json (WORKSPACE.md §1). Its edge set is **empty**, and
# emptier than `liveTransport`'s in the one way a reader would not predict: it
# links no platform primitive either. A transport needs a socket and a mutex; a
# decoder is handed a byte range and returns messages, so the only thing below
# that is not this library's own sources is an interpreter to run the boundary
# check with. `tests/check_boundaries.py` enforces exactly that reading, with an
# allowlist that names `osc` and the CMake keywords and nothing else.
#
# It is outside the aggregate product, and §5 needed a third question to say
# why: an OSC decoder names no product and opens nothing, so both of that
# section's existing clauses pass. What keeps it out is that no member of the
# product links it or can — nothing in the product reads a datagram.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" _osc_version
         LIMIT_COUNT 1)
    string(STRIP "${_osc_version}" _osc_version)
else()
    set(_osc_version "0.8.0")
endif()

project(osc
    VERSION ${_osc_version}
    DESCRIPTION "OSC 1.0 packet decoding, shared by every OSC-speaking adapter"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED OSC_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_osc_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_osc_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(OSC_BUILD_TESTS "Build osc tests" ${_osc_tests_default})
endif()

add_library(osc STATIC src/OscPacket.cpp)
add_library(osc::osc ALIAS osc)
set_target_properties(osc PROPERTIES
    EXPORT_NAME osc
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(osc PUBLIC cxx_std_17)
target_compile_definitions(osc PUBLIC OSC_STATIC)
target_include_directories(osc
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

# No target_link_libraries at all, and the absence is the measurement. The
# transport leaf beside this one links `ws2_32` or `Threads::Threads`; a decoder
# links neither, so the boundary check's allowlist has no platform entry to make
# an exception for.

if(MSVC)
    # PUBLIC for the same reason the adapters use it: the installed headers
    # carry UTF-8 section references, and a consumer on a non-UTF-8 code page
    # warns C4819 without the flag.
    target_compile_options(osc PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS osc
    EXPORT oscTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/oscConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/oscConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osc")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/oscConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT oscTargets
    FILE oscTargets.cmake
    NAMESPACE osc::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osc")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/oscConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/oscConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/osc")

if(OSC_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
