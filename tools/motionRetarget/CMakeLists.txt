# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _motion_retarget_version LIMIT_COUNT 1)
    string(STRIP "${_motion_retarget_version}" _motion_retarget_version)
else()
    set(_motion_retarget_version "0.8.0")
endif()

project(motionRetargetTool
    VERSION ${_motion_retarget_version}
    DESCRIPTION "Offline VRM motion retargeting CLI"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONRETARGET_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_motion_retarget_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_motion_retarget_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONRETARGET_BUILD_TESTS "Build motion_retarget tests"
           ${_motion_retarget_tests_default})
endif()

# This is the one place in the motion layer that opens a stage. The libraries
# below it stay value-only (WORKSPACE.md §2); the tool reads the rig and the
# clip off stages, hands plain values to vrmRetarget, and authors the result.
# It reads the humanoid mapping as `vrm:humanBones:*` attributes rather than
# through VrmHumanoidAPI, so it needs no link against the vrmSchema bundle.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UsdVrmOpenUsd.cmake")
if(NOT TARGET vrmRetarget::vrmRetarget)
    find_package(vrmRetarget REQUIRED CONFIG)
endif()
# By name rather than through vrmRetarget's PUBLIC re-export, for the reason
# `motion_bvh` states about `motionSource`: the tool resamples the clip itself,
# before either half of the bake expands it, so it has a real edge on
# `motionRuntime` and declaring it leaves nothing to be inferred from a
# transitive dependency that could stop being transitive.
if(NOT TARGET motionRuntime::motionRuntime)
    find_package(motionRuntime REQUIRED CONFIG)
endif()

add_executable(motion_retarget
    src/Options.cpp
    src/StageIo.cpp
    src/main.cpp)

target_compile_features(motion_retarget PRIVATE cxx_std_17)
target_link_libraries(motion_retarget
    PRIVATE
        vrmRetarget::vrmRetarget
        motionRuntime::motionRuntime)

foreach(_usd_lib usd usdGeom usdSkel sdf tf gf vt js arch)
    if(TARGET ${_usd_lib})
        target_link_libraries(motion_retarget PRIVATE ${_usd_lib})
    elseif(TARGET pxr::${_usd_lib})
        target_link_libraries(motion_retarget PRIVATE pxr::${_usd_lib})
    else()
        message(FATAL_ERROR
                "motion_retarget requires the OpenUSD '${_usd_lib}' target.")
    endif()
endforeach()

if(MSVC)
    target_compile_options(motion_retarget PRIVATE /utf-8)
    # Same pair the plugin bundles set: without NOMINMAX, windows.h's min/max
    # macros break OpenUSD's own numeric_limits<>::max() uses in tf's robin map.
    target_compile_definitions(motion_retarget PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

# Stage the executable into the member root's bin/, the way each bundle stages
# its shared library into lib/. `ost plugin package --workspace` packages a
# workspace tool out of the directories openstrata.tool.yaml declares, not out
# of the build tree, so a tool that only lands in build/ is refused at packaging
# with PRECONDITION_FAILED.
set_target_properties(motion_retarget PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(motion_retarget PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

include(GNUInstallDirs)
install(TARGETS motion_retarget RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(MOTIONRETARGET_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
