# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _motion_core_version LIMIT_COUNT 1)
    string(STRIP "${_motion_core_version}" _motion_core_version)
else()
    set(_motion_core_version "0.3.0")
endif()

project(motionCore
    VERSION ${_motion_core_version}
    DESCRIPTION "Vendor-neutral humanoid motion value types"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONCORE_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_motion_core_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_motion_core_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONCORE_BUILD_TESTS "Build motionCore tests"
           ${_motion_core_tests_default})
endif()

# motionCore deliberately uses OpenUSD's small Gf value types in its public
# contract, but has no USD stage, Sdf, plug, or file-format dependency.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()

add_library(motionCore STATIC
    src/Humanoid.cpp)
add_library(motionCore::motionCore ALIAS motionCore)
set_target_properties(motionCore PROPERTIES
    EXPORT_NAME motionCore
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(motionCore PUBLIC cxx_std_17)
target_compile_definitions(motionCore PUBLIC MOTIONCORE_STATIC)
target_include_directories(motionCore
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

if(TARGET gf)
    target_link_libraries(motionCore PUBLIC gf)
elseif(TARGET pxr::gf)
    target_link_libraries(motionCore PUBLIC pxr::gf)
else()
    message(FATAL_ERROR "motionCore requires the OpenUSD gf CMake target.")
endif()

if(MSVC)
    target_compile_options(motionCore PRIVATE /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS motionCore
    EXPORT motionCoreTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/motionCoreConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/motionCoreConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionCore")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/motionCoreConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT motionCoreTargets
    FILE motionCoreTargets.cmake
    NAMESPACE motionCore::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionCore")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/motionCoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/motionCoreConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/motionCore")

if(MOTIONCORE_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
