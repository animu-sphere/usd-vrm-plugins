# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _vrm_retarget_version LIMIT_COUNT 1)
    string(STRIP "${_vrm_retarget_version}" _vrm_retarget_version)
else()
    set(_vrm_retarget_version "0.4.0")
endif()

project(vrmRetarget
    VERSION ${_vrm_retarget_version}
    DESCRIPTION "Offline humanoid retargeting onto a VRM target rig"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED VRMRETARGET_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_vrm_retarget_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_vrm_retarget_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(VRMRETARGET_BUILD_TESTS "Build vrmRetarget tests"
           ${_vrm_retarget_tests_default})
endif()

# vrmRetarget is a plain library like motionCore and motionRuntime: OpenUSD Gf
# value types only, no stage, no plugin registration, and — the load-bearing
# one — no OpenExec (WORKSPACE.md §2). The retarget core is complete and
# testable before any exec node exists.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()

if(NOT TARGET motionCore::motionCore)
    find_package(motionCore REQUIRED CONFIG)
endif()
if(NOT TARGET motionRuntime::motionRuntime)
    find_package(motionRuntime REQUIRED CONFIG)
endif()

add_library(vrmRetarget STATIC
    src/HumanoidMap.cpp
    src/PoseRetargeter.cpp
    src/RestPose.cpp
    src/RootMotionPolicy.cpp
    src/TargetSkeleton.cpp)
add_library(vrmRetarget::vrmRetarget ALIAS vrmRetarget)
set_target_properties(vrmRetarget PROPERTIES
    EXPORT_NAME vrmRetarget
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(vrmRetarget PUBLIC cxx_std_17)
target_compile_definitions(vrmRetarget PUBLIC VRMRETARGET_STATIC)
target_include_directories(vrmRetarget
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(vrmRetarget
    PUBLIC
        motionCore::motionCore
        motionRuntime::motionRuntime)

if(MSVC)
    # PUBLIC for the same reason as motionRuntime: the installed headers are
    # UTF-8, so consumers need /utf-8 to include them without C4819.
    target_compile_options(vrmRetarget PUBLIC /utf-8)
endif()

include(GNUInstallDirs)
install(TARGETS vrmRetarget
    EXPORT vrmRetargetTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vrmRetargetConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmRetargetConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmRetarget")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vrmRetargetConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT vrmRetargetTargets
    FILE vrmRetargetTargets.cmake
    NAMESPACE vrmRetarget::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmRetarget")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vrmRetargetConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmRetargetConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vrmRetarget")

if(VRMRETARGET_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
