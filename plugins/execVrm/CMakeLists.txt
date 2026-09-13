# SPDX-License-Identifier: Apache-2.0
#
# execVrm — the VRM OpenExec bundle (Workspace Phase 8, Motion Phase E): VRM
# semantics applied to a target rig, driven by the schema contract only. The
# boundary, five registered value types and six computations. On
# `UsdSkelSkeleton`: `vrm.computeTargetSkeleton`, which is the rig as
# `vrmRetarget` reads it, and `vrm.computeBoundPose`, which forwards the pose
# of the animation the skeleton is bound to. On the applied
# `UsdSkelBindingAPI`: `vrm.computeBindingPose`, which is that binding
# inherited from an ancestor, as UsdSkel inherits it. On the applied
# `VrmHumanoidAPI`:
# `vrm.computeHumanoidMap`, which is which joint each human bone drives,
# `vrm.computeRestPoseCorrection`, which carries a clip's rest onto that rig,
# and `vrm.humanoidRetarget`, which is one sample of the clip on it -- four of
# P0-5's five nodes, and two it needed (docs/roadmap/openexec-foundation.md).
#
# Self-contained so `ost plugin build plugins/execVrm` can configure it
# directly, and also add_subdirectory()-able from the repo-root CMakeLists.txt
# for the plain-CMake workflow — the same shape as every sibling bundle.
cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" _exec_vrm_version LIMIT_COUNT 1)
    string(STRIP "${_exec_vrm_version}" _exec_vrm_version)
else()
    set(_exec_vrm_version "0.8.0")  # mirrors openstrata.plugin.yaml
endif()

project(ExecVrmPlugin
    VERSION ${_exec_vrm_version}
    DESCRIPTION "OpenExec computations applying VRM semantics to a target rig"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_property(_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _isMultiConfig AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Spelled USDVRM_BUILD_TESTS, like every sibling (see execMotion's CMakeLists
# for the bug a bundle-private spelling once caused).
if(NOT DEFINED USDVRM_BUILD_TESTS)
    option(USDVRM_BUILD_TESTS "Build usdVrm plugin tests" ${PROJECT_IS_TOP_LEVEL})
endif()

if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
# The OpenUSD pin and the OpenExec capability probe, as in execMotion.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UsdVrmOpenUsd.cmake")

# The retarget core, and through it motionCore and motionRuntime: every node
# here is a thin wrapper over a `vrmRetarget` call, and the values that cross
# the boundary are `vrmRetarget`'s own (WORKSPACE.md §2, motion policy §11.4).
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore CONFIG REQUIRED)
endif()
if(NOT TARGET motionRuntime::motionRuntime)
    find_package(motionRuntime CONFIG REQUIRED)
endif()
if(NOT TARGET vrmRetarget::vrmRetarget)
    find_package(vrmRetarget CONFIG REQUIRED)
endif()

# vrmSchema is NOT linked. The one thing this bundle needs of it is the
# `UsdVrmHumanoidAPI` type name being declared when exec reads this plugin's
# Exec block -- a plugInfo registration, not a symbol -- and the attribute
# names it reads are the schema's, asserted against the schema's own prim
# definition by execVrm_humanoid rather than taken from UsdVrmTokens. So the
# edge is `requires.bundles` in the manifest, and linking would add a runtime
# DLL dependency for nothing.

set(PLUGIN_NAME ExecVrm)
add_library(${PLUGIN_NAME} SHARED
    src/ExecVrmRig.cpp
    src/ExecVrmRegistration.cpp)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${PXR_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PRIVATE
    motionCore::motionCore
    motionRuntime::motionRuntime
    vrmRetarget::vrmRetarget)

if(MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_compile_options(${PLUGIN_NAME} PRIVATE /bigobj /utf-8)
endif()

# The exec half named explicitly, as in execMotion: usd_ms does not carry the
# exec libraries, and `esf`/`esfUsd`/`ef` are here because the public exec
# headers include them -- the same list cmake/UsdVrmOpenUsd.cmake probes for.
set(_exec_vrm_targets)
foreach(_t IN ITEMS arch tf gf vt plug sdf usd usdSkel
                    vdf ef esf esfUsd exec execUsd)
    if(TARGET ${_t})
        list(APPEND _exec_vrm_targets ${_t})
    elseif(TARGET pxr::${_t})
        list(APPEND _exec_vrm_targets pxr::${_t})
    else()
        message(FATAL_ERROR
            "execVrm needs the OpenUSD target '${_t}', which this install "
            "does not define. cmake/UsdVrmOpenUsd.cmake should have refused "
            "this runtime already; if it did not, its probe list and this one "
            "have drifted apart.")
    endif()
endforeach()
target_link_libraries(${PLUGIN_NAME} PRIVATE ${_exec_vrm_targets})

set(OPENSTRATA_PLUGIN_LIBRARY_PREFIX "lib")
set_target_properties(${PLUGIN_NAME} PROPERTIES
    PREFIX "${OPENSTRATA_PLUGIN_LIBRARY_PREFIX}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(${PLUGIN_NAME} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/lib"
        LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/lib")
endforeach()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/execVrm/plugInfo.json.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/execVrm/plugInfo.json"
    @ONLY)

if(USDVRM_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
