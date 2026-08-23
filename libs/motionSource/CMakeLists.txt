# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _motion_source_version LIMIT_COUNT 1)
    string(STRIP "${_motion_source_version}" _motion_source_version)
else()
    set(_motion_source_version "0.7.0")
endif()

project(motionSource
    VERSION ${_motion_source_version}
    DESCRIPTION "Format-neutral recorded-motion source model"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONSOURCE_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_motion_source_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_motion_source_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONSOURCE_BUILD_TESTS "Build motionSource tests"
           ${_motion_source_tests_default})
endif()

# The declared motionSource -> motionCore edge (WORKSPACE.md §2), and the only
# OpenUSD this library sees. It arrives through motionCore rather than directly.
# Four files cross into canonical motion -- CanonicalMetadata, SourceProfile for
# the right-hand side of a joint map, SourceProfileFile, which reads that side
# out of a file, and CanonicalConversion, which is the crossing rather than a
# corner of one -- and motionSource_boundaries carries the list, so a fifth joins
# it in review rather than quietly. Everywhere else a value in a basis this layer
# does not know is still not a geometric vector (SourceSkeleton.h), and the check
# is what keeps that true now that one file here does name a Gf type.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UsdVrmOpenUsd.cmake")

# In the composed workspace build motionCore is already an in-tree target; a
# standalone `ost plugin build`-style configure resolves its installed package.
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()

add_library(motionSource STATIC
    src/CanonicalConversion.cpp
    src/CanonicalMetadata.cpp
    src/SourceAnimation.cpp
    src/SourceProfile.cpp
    src/SourceProfileFile.cpp
    src/SourceProvenance.cpp
    src/SourceSkeleton.cpp)
add_library(motionSource::motionSource ALIAS motionSource)
set_target_properties(motionSource PROPERTIES
    EXPORT_NAME motionSource
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionSource PUBLIC cxx_std_17)
target_compile_definitions(motionSource PUBLIC MOTIONSOURCE_STATIC)
target_include_directories(motionSource
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(motionSource PUBLIC motionCore::motionCore)

if(MSVC)
    # PUBLIC, not PRIVATE: the installed headers carry UTF-8 (the §-references
    # into the contracts), so every consumer that includes them needs the flag
    # too or MSVC warns C4819 on a non-UTF-8 code page.
    target_compile_options(motionSource PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS motionSource
    EXPORT motionSourceTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/motionSourceConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/motionSourceConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionSource")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/motionSourceConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT motionSourceTargets
    FILE motionSourceTargets.cmake
    NAMESPACE motionSource::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionSource")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/motionSourceConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/motionSourceConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionSource")

if(MOTIONSOURCE_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
