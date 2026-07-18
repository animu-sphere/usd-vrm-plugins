# SPDX-License-Identifier: Apache-2.0
#
# usdVrmPackageResolver — the ArPackageResolver that serves embedded resources
# out of .vrm / GLB containers (`avatar.vrm[images/<hash>.png]`), split out of
# the usdVrm file-format bundle per docs/architecture/WORKSPACE.md §1.
#
# Self-contained so `ost plugin build plugins/usdVrmPackageResolver` can
# configure it directly, and also add_subdirectory()-able from the repo-root
# CMakeLists.txt for the plain-CMake (non-OpenStrata) workflow. It must never
# link the file-format plugin (WORKSPACE.md §2); tests/check_boundaries.py
# enforces that on the built binary.
cmake_minimum_required(VERSION 3.22)

# Version comes from the repo-root VERSION file (single source of truth). When
# this bundle is built standalone from an export that does not carry the repo
# root, fall back to the bundle-manifest version so the configure still works.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" _vrm_resolver_version LIMIT_COUNT 1)
    string(STRIP "${_vrm_resolver_version}" _vrm_resolver_version)
else()
    set(_vrm_resolver_version "0.2.0")  # mirrors openstrata.plugin.yaml
endif()

project(UsdVrmPackageResolverPlugin
    VERSION ${_vrm_resolver_version}
    DESCRIPTION "OpenUSD package resolver for embedded VRM resources"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenStrataPlugin.cmake")
openstrata_default_build_type()

# The repo root defines this option for the plain-CMake multi-bundle build; it
# only reaches the option() below in a standalone bundle configure.
if(NOT DEFINED USDVRM_BUILD_TESTS)
    option(USDVRM_BUILD_TESTS "Build usdVrmPackageResolver plugin tests"
        ${PROJECT_IS_TOP_LEVEL})
endif()

# Resolved from the runtime OpenStrata activates, or from CMAKE_PREFIX_PATH in
# the plain-CMake flow. Idempotent when the root project already found it.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()

# cgltf, fetched at configure time (see cmake/Dependencies.cmake). Defines the
# cgltf::cgltf INTERFACE target. Used only to enumerate the glTF image table;
# all byte access goes through vrmContainer's checked views.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake")

# vrmContainer is the bundle's only workspace dependency (WORKSPACE.md §2). ost
# 0.16.0 executes the manifest's requires.libraries closure into the workspace
# prefix before configuring this consumer; a plain workspace build already has
# the in-tree alias; any other standalone caller supplies an installed package
# via CMAKE_PREFIX_PATH.
if(NOT TARGET vrmContainer::vrmContainer)
    find_package(vrmContainer CONFIG REQUIRED)
endif()

set(PLUGIN_NAME UsdVrmPackageResolver)

add_library(${PLUGIN_NAME} SHARED
    src/UsdVrmPackageResolver.cpp
    src/cgltfImpl.cpp)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${PXR_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PRIVATE
    cgltf::cgltf
    vrmContainer::vrmContainer)

if(MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_compile_options(${PLUGIN_NAME} PRIVATE /bigobj /utf-8)
endif()

# Only the Ar surface of OpenUSD: the resolver serves bytes for package paths
# and must not author stages (no usd/usdGeom/... components here).
openstrata_link_openusd(
    TARGET ${PLUGIN_NAME}
    COMPONENTS arch tf js plug vt ar)

# Stages the shared library into the bundle's lib/ (so plugInfo.json's
# LibraryPath = ../../../lib/<lib> resolves) and refreshes the committed
# plugInfo.json for the target platform.
openstrata_configure_plugin(
    TARGET ${PLUGIN_NAME}
    PLUG_INFO_INPUT
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/usdVrmPackageResolver/plugInfo.json.in"
    PLUG_INFO_OUTPUT
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/usdVrmPackageResolver/plugInfo.json")

openstrata_install_plugin_bundle(
    TARGET ${PLUGIN_NAME}
    RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/usdVrmPackageResolver/plugInfo.json"
    RESOURCE_DESTINATION "plugin/resources/usdVrmPackageResolver"
    MANIFESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/openstrata.plugin.yaml"
    FIXTURE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

if(USDVRM_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
