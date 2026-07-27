# SPDX-License-Identifier: Apache-2.0
#
# A fixture project that stands in for an OpenUSD install, so
# cmake/UsdVrmOpenUsd.cmake can be pointed at OpenUSD versions and OpenExec
# layouts we do not have. Every runtime this repo builds against is OpenUSD
# 26.08 with a complete OpenExec, so the *rejecting* half of the contract has
# no other way to be exercised.
#
# LANGUAGES NONE: nothing is compiled here. The module only reads version
# variables, imported-target existence, and header paths.
#
# Driven by ../openusd_contract_test.cmake, not configured by hand.
cmake_minimum_required(VERSION 3.22)
project(UsdVrmOpenUsdContractFixture LANGUAGES NONE)

if(NOT DEFINED PROBE_PXR_VERSION)
    message(FATAL_ERROR "PROBE_PXR_VERSION is required")
endif()

# Reproduce what pxrConfig.cmake publishes, including OpenUSD's major
# version of 0 -- the module derives its display string from MINOR/PATCH
# precisely because of that.
set(pxr_FOUND TRUE)
set(pxr_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixture-pxrConfig.cmake")
set(PXR_VERSION "${PROBE_PXR_VERSION}")
set(PXR_MAJOR_VERSION 0)
math(EXPR PXR_MINOR_VERSION "${PROBE_PXR_VERSION} / 100")
math(EXPR PXR_PATCH_VERSION "${PROBE_PXR_VERSION} % 100")

set(PXR_INCLUDE_DIRS "${CMAKE_CURRENT_BINARY_DIR}/include")

# The six components the module probes, with the header it looks for. Kept in
# step with UsdVrmOpenUsd.cmake by hand: this fixture is what would catch the
# module probing a header no OpenUSD ships.
set(_fixture_components
    "exec"            "pxr/exec/exec/system.h"
    "execGeom"        "pxr/exec/execGeom/tokens.h"
    "execIr"          "pxr/exec/execIr/controller.h"
    "execUsd"         "pxr/exec/execUsd/system.h"
    "vdf"             "pxr/exec/vdf/api.h"
    "usdExecImaging"  "pxr/usdImaging/usdExecImaging/stageSceneIndexInterface.h")

list(LENGTH _fixture_components _fixture_length)
math(EXPR _fixture_last "${_fixture_length} - 1")
foreach(_i RANGE 0 ${_fixture_last} 2)
    list(GET _fixture_components ${_i} _component)
    math(EXPR _j "${_i} + 1")
    list(GET _fixture_components ${_j} _header)

    if(NOT _component IN_LIST PROBE_MISSING_TARGETS)
        add_library(${_component} SHARED IMPORTED GLOBAL)
    endif()
    if(NOT _component IN_LIST PROBE_MISSING_HEADERS)
        file(WRITE "${PXR_INCLUDE_DIRS}/${_header}" "/* fixture */\n")
    endif()
endforeach()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/UsdVrmOpenUsd.cmake")

# Only reached when the contract accepted the fixture. The driver greps for
# this, so a configure that succeeds for the wrong reason (module not included,
# say) is still a failure.
message(STATUS "FIXTURE ACCEPTED release=${USDVRM_OPENUSD_RELEASE} "
               "components=${USDVRM_OPENEXEC_COMPONENTS}")
