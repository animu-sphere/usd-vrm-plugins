# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _vrm_export_version LIMIT_COUNT 1)
    string(STRIP "${_vrm_export_version}" _vrm_export_version)
else()
    set(_vrm_export_version "0.9.0")
endif()

project(vrmExportTool
    VERSION ${_vrm_export_version}
    DESCRIPTION "Write an imported VRM as native USD / USDZ"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED VRMEXPORT_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_vrm_export_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_vrm_export_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(VRMEXPORT_BUILD_TESTS "Build vrm_export tests"
           ${_vrm_export_tests_default})
endif()

# OpenUSD and nothing else (docs/design/VRM_EXPORT_POLICY.md §4). A `.vrm` is
# reached through the plugin registry at run time, the way any OpenUSD client
# reaches one, so no VRM identity is linked: not the importer, not
# vrmContainer, not vrmSchema.
if(NOT pxr_FOUND)
    find_package(pxr REQUIRED CONFIG)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UsdVrmOpenUsd.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/UsdVrmUtf8CodePage.cmake")

set(_vrm_export_usd_libs usd usdUtils usdValidation sdf ar plug tf vt gf arch)
set(_vrm_export_usd_targets)
foreach(_usd_lib ${_vrm_export_usd_libs})
    if(TARGET ${_usd_lib})
        list(APPEND _vrm_export_usd_targets ${_usd_lib})
    elseif(TARGET pxr::${_usd_lib})
        list(APPEND _vrm_export_usd_targets pxr::${_usd_lib})
    else()
        message(FATAL_ERROR "vrm_export requires the OpenUSD '${_usd_lib}' target.")
    endif()
endforeach()

# The export operation, apart from the command line so its unit tests link it
# (policy §4). A target of the tool, not a library identity: it is not
# installed and exports no package.
add_library(vrm_export_operation STATIC
    src/Options.cpp
    src/export/VrmExport.cpp)
target_include_directories(vrm_export_operation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(vrm_export_operation PUBLIC cxx_std_17)
target_link_libraries(vrm_export_operation PUBLIC ${_vrm_export_usd_targets})
set_target_properties(vrm_export_operation PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(vrm_export src/main.cpp)
usdvrm_use_utf8_code_page(vrm_export)
target_link_libraries(vrm_export PRIVATE vrm_export_operation)
target_compile_definitions(vrm_export PRIVATE
    VRM_EXPORT_VERSION="${_vrm_export_version}")

if(MSVC)
    foreach(_target vrm_export_operation vrm_export)
        target_compile_options(${_target} PRIVATE /utf-8)
        # Same pair the plugin bundles set: without NOMINMAX, windows.h's
        # min/max macros break OpenUSD's own numeric_limits<>::max() uses.
        target_compile_definitions(${_target} PRIVATE NOMINMAX _USE_MATH_DEFINES)
    endforeach()
endif()

# Stage the executable into the member root's bin/, which openstrata.tool.yaml
# declares: `ost plugin package --workspace` packages a workspace tool out of
# those directories, not out of the build tree.
set_target_properties(vrm_export PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(vrm_export PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

include(GNUInstallDirs)
install(TARGETS vrm_export RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(VRMEXPORT_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    add_subdirectory(tests)
endif()
