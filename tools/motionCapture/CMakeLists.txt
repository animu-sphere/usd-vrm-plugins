# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _motion_capture_version LIMIT_COUNT 1)
    string(STRIP "${_motion_capture_version}" _motion_capture_version)
else()
    set(_motion_capture_version "0.5.0")
endif()

project(motionCaptureTool
    VERSION ${_motion_capture_version}
    DESCRIPTION "Live-capture replay CLI"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONCAPTURE_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_motion_capture_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_motion_capture_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONCAPTURE_BUILD_TESTS "Build motion_capture tests"
           ${_motion_capture_tests_default})
endif()

# Like motion_retarget, this tool is the only stage-aware part of its own path:
# motionRuntime stays value-only (WORKSPACE.md §2). It does not link vrmRetarget
# -- it authors the avatar-independent semantic clip and stops there, so the
# retarget step is the existing tool, unchanged.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UsdVrmOpenUsd.cmake")
if(NOT TARGET motionRuntime::motionRuntime)
    find_package(motionRuntime REQUIRED CONFIG)
endif()

add_executable(motion_capture
    src/ClipWriter.cpp
    src/Options.cpp
    src/main.cpp)

target_compile_features(motion_capture PRIVATE cxx_std_17)
target_link_libraries(motion_capture PRIVATE motionRuntime::motionRuntime)

foreach(_usd_lib usd usdGeom usdSkel sdf tf gf vt arch)
    if(TARGET ${_usd_lib})
        target_link_libraries(motion_capture PRIVATE ${_usd_lib})
    elseif(TARGET pxr::${_usd_lib})
        target_link_libraries(motion_capture PRIVATE pxr::${_usd_lib})
    else()
        message(FATAL_ERROR
                "motion_capture requires the OpenUSD '${_usd_lib}' target.")
    endif()
endforeach()

if(MSVC)
    target_compile_options(motion_capture PRIVATE /utf-8)
    # Same pair the plugin bundles set: without NOMINMAX, windows.h's min/max
    # macros break OpenUSD's own numeric_limits<>::max() uses in tf's robin map.
    target_compile_definitions(motion_capture PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

# Stage the executable into the member root's bin/, the way each bundle stages
# its shared library into lib/. `ost plugin package --workspace` packages a
# workspace tool out of the directories openstrata.tool.yaml declares, not out
# of the build tree, so a tool that only lands in build/ is refused at packaging
# with PRECONDITION_FAILED.
set_target_properties(motion_capture PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(motion_capture PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

include(GNUInstallDirs)
install(TARGETS motion_capture RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(MOTIONCAPTURE_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
