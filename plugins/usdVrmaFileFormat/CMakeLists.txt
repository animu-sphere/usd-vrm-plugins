# SPDX-License-Identifier: Apache-2.0
#
# usdVrmaFileFormat — an OpenUSD SdfFileFormat plugin that imports .vrma clips.
cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION" _vrma_plugin_version LIMIT_COUNT 1)
    string(STRIP "${_vrma_plugin_version}" _vrma_plugin_version)
else()
    set(_vrma_plugin_version "0.3.0")
endif()

project(UsdVrmaPlugin
    VERSION ${_vrma_plugin_version}
    DESCRIPTION "OpenUSD VRMA file-format plugin"
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

if(NOT DEFINED USDVRMA_BUILD_TESTS)
    option(USDVRMA_BUILD_TESTS "Build usdVrma file-format tests" ${PROJECT_IS_TOP_LEVEL})
endif()

if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake")

if(NOT TARGET vrmContainer::vrmContainer)
    find_package(vrmContainer CONFIG REQUIRED)
endif()
if(NOT TARGET motionCore::motionCore)
    find_package(motionCore CONFIG REQUIRED)
endif()

set(PLUGIN_NAME UsdVrmaFileFormat)
add_library(${PLUGIN_NAME} SHARED
    src/UsdVrmaFileFormat.cpp
    src/io/cgltfImpl.cpp
    src/io/CgltfVrmaDocumentReader.cpp
    src/usd/UsdVrmaAuthorer.cpp)

target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
target_include_directories(${PLUGIN_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    ${PXR_INCLUDE_DIRS})
target_link_libraries(${PLUGIN_NAME} PRIVATE
    cgltf::cgltf
    vrmContainer::vrmContainer
    motionCore::motionCore)

if(MSVC)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_compile_options(${PLUGIN_NAME} PRIVATE /bigobj /utf-8)
endif()

if(TARGET usd_ms)
    target_link_libraries(${PLUGIN_NAME} PRIVATE usd_ms)
else()
    set(_pxr_targets)
    foreach(_t IN ITEMS arch tf gf vt js ar plug sdf usd usdGeom usdSkel)
        if(TARGET ${_t})
            list(APPEND _pxr_targets ${_t})
        elseif(TARGET pxr::${_t})
            list(APPEND _pxr_targets pxr::${_t})
        endif()
    endforeach()
    if(_pxr_targets)
        target_link_libraries(${PLUGIN_NAME} PRIVATE ${_pxr_targets})
    elseif(PXR_LIBRARIES)
        target_link_libraries(${PLUGIN_NAME} PRIVATE ${PXR_LIBRARIES})
    else()
        message(FATAL_ERROR "Could not locate OpenUSD CMake targets or PXR_LIBRARIES.")
    endif()
endif()

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
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/usdVrmaFileFormat/plugInfo.json.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/plugin/resources/usdVrmaFileFormat/plugInfo.json"
    @ONLY)

if(USDVRMA_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
