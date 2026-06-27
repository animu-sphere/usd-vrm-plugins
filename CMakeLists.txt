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

project(UsdVrmPlugins
    VERSION 0.1.0
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

# Resolve OpenUSD once for the whole project; bundles reuse the result.
find_package(pxr REQUIRED CONFIG)

add_subdirectory(plugins/usdVrm)

install(FILES
    LICENSE
    README.md
    DESTINATION "share/doc/usd-vrm-plugins"
    OPTIONAL)
