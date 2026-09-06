# SPDX-License-Identifier: Apache-2.0
#
# execMotion — the vendor-neutral OpenExec motion bundle (Workspace Phase 8,
# Motion Phase E). The boundary, one registered value type and four computations:
# `motion.identityPose`, which is the mechanism held at its weakest possible
# value, `motion.sampleAnimation`, which reads a clip at the evaluated frame,
# and `motion.filterPose` with the `motion.priorPose` seam an override enters
# through. `extractRootMotion`, `interpolatePose` and `blendPoses` follow
# (docs/roadmap/openexec-foundation.md, P0-4).
#
# Self-contained so `ost plugin build plugins/execMotion` can configure it
# directly, and also add_subdirectory()-able from the repo-root CMakeLists.txt
# for the plain-CMake workflow — the same shape as every sibling bundle.
cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" _exec_motion_version LIMIT_COUNT 1)
    string(STRIP "${_exec_motion_version}" _exec_motion_version)
else()
    set(_exec_motion_version "0.8.0")  # mirrors openstrata.plugin.yaml
endif()

project(ExecMotionPlugin
    VERSION ${_exec_motion_version}
    DESCRIPTION "OpenExec computations over canonical motion"
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

# Spelled USDVRM_BUILD_TESTS, like every sibling: the root defines it for all
# bundles, and a bundle-private spelling is how usdVrmaFileFormat once shipped
# a workspace lane that built it on three platforms and tested it on none.
if(NOT DEFINED USDVRM_BUILD_TESTS)
    option(USDVRM_BUILD_TESTS "Build usdVrm plugin tests" ${PROJECT_IS_TOP_LEVEL})
endif()

if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
# The OpenUSD pin and the OpenExec capability probe. This bundle is the first
# one that cannot link without the second half, so a runtime the probe refuses
# would otherwise fail here as an unresolved symbol.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UsdVrmOpenUsd.cmake")

if(NOT TARGET motionCore::motionCore)
    find_package(motionCore CONFIG REQUIRED)
endif()

# The vendor-neutral runtime, from `motion.filterPose` onward: the node is one
# step of `motion::PoseFilter` and the algorithm stays in the library
# (WORKSPACE.md §2, motion policy §11.4). `motion.identityPose` and
# `motion.sampleAnimation` need none of it -- an exec input arrives already
# resolved at the evaluated frame, so the sampler had nothing to wrap.
if(NOT TARGET motionRuntime::motionRuntime)
    find_package(motionRuntime CONFIG REQUIRED)
endif()

set(PLUGIN_NAME ExecMotion)
add_library(${PLUGIN_NAME} SHARED
    src/ExecMotionPose.cpp
    src/ExecMotionRegistration.cpp)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${PXR_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PRIVATE
    motionCore::motionCore
    motionRuntime::motionRuntime)

if(MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_compile_options(${PLUGIN_NAME} PRIVATE /bigobj /utf-8)
endif()

# The exec half is named explicitly rather than taken from usd_ms: the monolithic
# target does not carry the exec libraries, and `esf`/`esfUsd`/`ef` are here
# because the public exec headers include them transitively — the same list
# cmake/UsdVrmOpenUsd.cmake probes for.
set(_exec_motion_targets)
foreach(_t IN ITEMS arch tf gf vt plug sdf usd usdSkel
                    vdf ef esf esfUsd exec execUsd)
    if(TARGET ${_t})
        list(APPEND _exec_motion_targets ${_t})
    elseif(TARGET pxr::${_t})
        list(APPEND _exec_motion_targets pxr::${_t})
    else()
        message(FATAL_ERROR
            "execMotion needs the OpenUSD target '${_t}', which this install "
            "does not define. cmake/UsdVrmOpenUsd.cmake should have refused "
            "this runtime already; if it did not, its probe list and this one "
            "have drifted apart.")
    endif()
endforeach()
target_link_libraries(${PLUGIN_NAME} PRIVATE ${_exec_motion_targets})

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
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/execMotion/plugInfo.json.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/execMotion/plugInfo.json"
    @ONLY)

if(USDVRM_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
