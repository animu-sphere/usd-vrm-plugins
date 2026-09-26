# SPDX-License-Identifier: Apache-2.0
#
# vrmImaging -- the canonical VRM material schemas made visible to Hydra
# (docs/design/VRM_IMAGING_POLICY.md). A UsdImaging API-schema adapter per
# applied schema, each contributing data sources to the Hydra material prim and
# mapping a changed USD property to the locator it feeds. No renderer, no
# shader, no image loading: what a VRM material says, not how it is drawn.
#
# Phase I0: `VrmMToonAPI` alone, as the contract experiment the policy (§20,
# §25) asks for before any Hydra-facing name is frozen.
#
# Not an `ost` plugin bundle yet: `ost` 0.23.8 has no plugin kind for a
# UsdImaging adapter, so there is no descriptor and the root CMakeLists.txt adds
# this directory by name. It is built and tested by the root build and not
# packaged (policy §19, Phase I5).
cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" _vrm_imaging_version LIMIT_COUNT 1)
    string(STRIP "${_vrm_imaging_version}" _vrm_imaging_version)
else()
    set(_vrm_imaging_version "0.9.0")
endif()

project(VrmImagingPlugin
    VERSION ${_vrm_imaging_version}
    DESCRIPTION "UsdImaging adapters exposing canonical VRM material semantics to Hydra"
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

if(NOT DEFINED USDVRM_BUILD_TESTS)
    option(USDVRM_BUILD_TESTS "Build usdVrm plugin tests" ${PROJECT_IS_TOP_LEVEL})
endif()

if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UsdVrmOpenUsd.cmake")

# vrmSchema is NOT linked, for execVrm's reason: the adapter needs the schema
# registered, which is vrmSchema's plugInfo.json, and reads the attribute names
# from the registered definition rather than from UsdVrmTokens. Nothing of the
# importer is reachable either (policy §12): the stage is the boundary.
set(PLUGIN_NAME VrmImaging)
add_library(${PLUGIN_NAME} SHARED
    src/VrmMToonAPIAdapter.cpp)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${PXR_INCLUDE_DIRS})

if(MSVC)
    # OpenUSD's hd/dataSource.h uses the std::atomic_* shared_ptr overloads,
    # deprecated in C++20, which is what `ost`'s toolchain compiles.
    target_compile_definitions(${PLUGIN_NAME} PRIVATE NOMINMAX
        _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
    target_compile_options(${PLUGIN_NAME} PRIVATE /utf-8)
endif()

set(_vrm_imaging_targets)
foreach(_t IN ITEMS arch tf vt sdf usd hd usdImaging)
    if(TARGET ${_t})
        list(APPEND _vrm_imaging_targets ${_t})
    elseif(TARGET pxr::${_t})
        list(APPEND _vrm_imaging_targets pxr::${_t})
    else()
        message(FATAL_ERROR
            "vrmImaging needs the OpenUSD target '${_t}', which this install "
            "does not define. A runtime built without imaging cannot host a "
            "UsdImaging adapter.")
    endif()
endforeach()
target_link_libraries(${PLUGIN_NAME} PRIVATE ${_vrm_imaging_targets})

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
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/vrmImaging/plugInfo.json.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/vrmImaging/plugInfo.json"
    @ONLY)

if(USDVRM_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
