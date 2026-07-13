# SPDX-License-Identifier: Apache-2.0
#
# Top-level project for usd-vrm-plugins.
#
# This repo is dual-mode by design:
#   * OpenStrata-native: `ost plugin build|test plugins/usdVrm` drives the bundle
#     CMakeLists.txt directly against the activated runtime's toolchain.
#   * Plain CMake: anyone without OpenStrata can configure THIS file, point
#     CMAKE_PREFIX_PATH at an OpenUSD install, and build every bundle. Use the
#     CMakePresets.json here, or:
#       cmake -S . -B build -DCMAKE_PREFIX_PATH=<usd-install>
#       cmake --build build --config Release
#
# Each plugin under plugins/<name> is a self-contained CMake project (so `ost
# plugin build` can target it standalone) that is also add_subdirectory()-able
# from here.
cmake_minimum_required(VERSION 3.22)

# Single source of truth for the release version: the repo-root VERSION file.
# Every other version string (CHANGELOG, release tag, ost bundle manifest) mirrors
# it; nothing hand-duplicates the number here. See docs/SUPPORTED_CONFIGURATIONS.md.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _vrm_project_version LIMIT_COUNT 1)
string(STRIP "${_vrm_project_version}" _vrm_project_version)

project(UsdVrmPlugins
    VERSION ${_vrm_project_version}
    DESCRIPTION "OpenUSD plugins for VRM avatars"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators (Ninja) default to an empty build type. The OpenUSD
# installs we build against are Release-only, so default to Release to avoid
# pulling debug-only imported deps (e.g. tbb12_debug.lib).
get_property(_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _isMultiConfig AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(VRM_IS_TOP_LEVEL ON)
else()
    set(VRM_IS_TOP_LEVEL OFF)
endif()

option(USDVRM_BUILD_TESTS "Build usdVrm plugin tests" ${VRM_IS_TOP_LEVEL})

# Enable CTest at the top level so tests registered in bundle subdirectories are
# discoverable by `ctest` from this build tree.
if(USDVRM_BUILD_TESTS)
    enable_testing()
endif()

# Resolve OpenUSD once for the whole project; bundles reuse the result.
find_package(pxr REQUIRED CONFIG)

# Discover every plugin bundle under plugins/ rather than naming them one by one:
# an immediate plugins/<name>/ that carries both an openstrata.plugin.yaml (the
# bundle contract) and a CMakeLists.txt (so `ost plugin build` can target it
# standalone) is add_subdirectory()'d here. CONFIGURE_DEPENDS re-globs on the next
# build when a bundle is added/removed, so a second `ost plugin new plugins/<name>`
# is picked up without editing this file.
#
# Order: schema bundles first — they sit at the bottom of the workspace
# dependency graph (WORKSPACE.md §2: nothing may depend *from* a schema bundle,
# consumers link it via its in-tree alias target in this composed build), then
# everything else alphabetically.
file(GLOB _bundles LIST_DIRECTORIES true CONFIGURE_DEPENDS RELATIVE
     "${CMAKE_CURRENT_SOURCE_DIR}/plugins" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*")
set(_schema_bundles)
set(_other_bundles)
foreach(_bundle ${_bundles})
    set(_bundleDir "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${_bundle}")
    if(IS_DIRECTORY "${_bundleDir}"
       AND EXISTS "${_bundleDir}/openstrata.plugin.yaml"
       AND EXISTS "${_bundleDir}/CMakeLists.txt")
        file(READ "${_bundleDir}/openstrata.plugin.yaml" _manifest)
        if(_manifest MATCHES "kind:[ \t]*usd-schema")
            list(APPEND _schema_bundles "${_bundle}")
        else()
            list(APPEND _other_bundles "${_bundle}")
        endif()
    endif()
endforeach()
foreach(_bundle ${_schema_bundles} ${_other_bundles})
    message(STATUS "OpenStrata: adding plugin bundle 'plugins/${_bundle}'")
    add_subdirectory("plugins/${_bundle}")
endforeach()

install(FILES
    LICENSE
    README.md
    DESTINATION "share/doc/usd-vrm-plugins"
    OPTIONAL)
