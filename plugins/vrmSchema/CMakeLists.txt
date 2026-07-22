# SPDX-License-Identifier: Apache-2.0
#
# vrmSchema — the typed VRM API schemas (VrmHumanoidAPI, VrmExpressionAPI,
# VrmLookAtAPI, VrmSpringBoneAPI, VrmColliderAPI, VrmConstraintAPI), split out
# of the usdVrm file-format bundle per docs/architecture/WORKSPACE.md §1.
#
# Self-contained so `ost plugin build plugins/vrmSchema` can configure it
# directly, and also add_subdirectory()-able from the repo-root CMakeLists.txt
# for the plain-CMake (non-OpenStrata) workflow. Consumers resolve it as an
# installed package: find_package(vrmSchema CONFIG REQUIRED) and link
# vrmSchema::vrmSchema.
cmake_minimum_required(VERSION 3.22)

# Version comes from the repo-root VERSION file (single source of truth). When
# this bundle is built standalone from an export that does not carry the repo
# root, fall back to the bundle-manifest version so the configure still works.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" _vrm_schema_version LIMIT_COUNT 1)
    string(STRIP "${_vrm_schema_version}" _vrm_schema_version)
else()
    set(_vrm_schema_version "0.3.0")  # mirrors openstrata.plugin.yaml
endif()

project(VrmSchema
    VERSION ${_vrm_schema_version}
    DESCRIPTION "Typed VRM API schemas for OpenUSD"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenStrataPlugin.cmake")
openstrata_default_build_type()

# The repo root defines this option for the plain-CMake multi-bundle build.
if(NOT DEFINED USDVRM_BUILD_TESTS)
    option(USDVRM_BUILD_TESTS "Build usdVrm plugin tests" ${PROJECT_IS_TOP_LEVEL})
endif()

# Resolved from the runtime OpenStrata activates, or from CMAKE_PREFIX_PATH in
# the plain-CMake flow. Idempotent when the root project already found it.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()

set(PLUGIN_NAME vrmSchema)

# Committed usdGenSchema output (regenerate with tools/generate_schema.py when
# schema/schema.usda changes). Headers live under src/vrmSchema/ so in-tree
# consumers use the same #include <vrmSchema/...> spelling as the installed
# package.
set(VRMSCHEMA_COMMITTED_SCHEMA_SOURCES
    src/vrmSchema/tokens.cpp
    src/vrmSchema/vrmColliderAPI.cpp
    src/vrmSchema/vrmConstraintAPI.cpp
    src/vrmSchema/vrmExpressionAPI.cpp
    src/vrmSchema/vrmHumanoidAPI.cpp
    src/vrmSchema/vrmLookAtAPI.cpp
    src/vrmSchema/vrmSpringBoneAPI.cpp)

add_library(${PLUGIN_NAME} SHARED)
add_library(vrmSchema::vrmSchema ALIAS ${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES EXPORT_NAME vrmSchema)

target_compile_features(${PLUGIN_NAME} PUBLIC cxx_std_17)
target_include_directories(${PLUGIN_NAME}
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/vrmSchema"
        ${PXR_INCLUDE_DIRS})

# The generated USDVRM_API export macro resolves to ARCH_EXPORT only when
# USDVRM_EXPORTS is defined for the library that compiles it (otherwise
# ARCH_IMPORT — which is exactly what consumers such as usdVrm want).
target_compile_definitions(${PLUGIN_NAME} PRIVATE USDVRM_EXPORTS)

# ost 0.6+ writes this fragment for compiled schemas. It regenerates schema C++
# from schema/schema.usda in the resolved runtime and adds those sources to
# this library. Plain CMake, and older ost builds, fall back to the committed
# generated sources.
if(DEFINED OPENSTRATA_SCHEMA_SOURCES_FILE AND EXISTS "${OPENSTRATA_SCHEMA_SOURCES_FILE}")
    include("${OPENSTRATA_SCHEMA_SOURCES_FILE}")
else()
    target_sources(${PLUGIN_NAME} PRIVATE ${VRMSCHEMA_COMMITTED_SCHEMA_SOURCES})
endif()

if(MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE _USE_MATH_DEFINES)
    target_compile_options(${PLUGIN_NAME} PRIVATE /bigobj)
endif()

# Public because the installed generated headers include OpenUSD headers and
# downstream C++ consumers use the same ABI-bearing types.
openstrata_link_openusd(
    TARGET ${PLUGIN_NAME}
    VISIBILITY PUBLIC
    COMPONENTS arch tf gf vt sdf usd)

# Stages the shared library into the bundle's lib/ (so plugInfo.json's
# LibraryPath = ../../../lib/<lib> resolves) and refreshes the committed
# plugInfo.json for the target platform.
openstrata_configure_plugin(
    TARGET ${PLUGIN_NAME}
    PLUG_INFO_INPUT
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/vrmSchema/plugInfo.json.in"
    PLUG_INFO_OUTPUT
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/vrmSchema/plugInfo.json")

# ---------------------------------------------------------------------------
# Install & CMake package export: the installed tree is the contract consumers
# build against (find_package(vrmSchema CONFIG REQUIRED)); sibling bundles must
# never reach into this source tree (WORKSPACE.md §4).
# ---------------------------------------------------------------------------
openstrata_install_plugin_bundle(
    TARGET ${PLUGIN_NAME}
    EXPORT_SET vrmSchemaTargets
    RESOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/vrmSchema/plugInfo.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/vrmSchema/generatedSchema.usda"
    RESOURCE_DESTINATION "plugin/resources/vrmSchema"
    MANIFESTS
        "${CMAKE_CURRENT_SOURCE_DIR}/openstrata.plugin.yaml"
    FIXTURE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

install(FILES
    src/vrmSchema/api.h
    src/vrmSchema/tokens.h
    src/vrmSchema/vrmColliderAPI.h
    src/vrmSchema/vrmConstraintAPI.h
    src/vrmSchema/vrmExpressionAPI.h
    src/vrmSchema/vrmHumanoidAPI.h
    src/vrmSchema/vrmLookAtAPI.h
    src/vrmSchema/vrmSpringBoneAPI.h
    DESTINATION "include/vrmSchema")

include(CMakePackageConfigHelpers)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vrmSchemaConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmSchemaConfig.cmake"
    INSTALL_DESTINATION "lib/cmake/vrmSchema")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vrmSchemaConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(EXPORT vrmSchemaTargets
    FILE vrmSchemaTargets.cmake
    NAMESPACE vrmSchema::
    DESTINATION "lib/cmake/vrmSchema")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vrmSchemaConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vrmSchemaConfigVersion.cmake"
    DESTINATION "lib/cmake/vrmSchema")

if(USDVRM_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
