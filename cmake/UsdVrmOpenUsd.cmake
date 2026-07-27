# SPDX-License-Identifier: Apache-2.0
#
# UsdVrmOpenUsd.cmake — the workspace's OpenUSD contract, enforced in one place.
#
# Two checks, both fatal, both run once per CMake project:
#
#   1. OpenUSD is 26.08 and nothing else. The `>=25.05,<27.0` tolerated range
#      v0.1.0-v0.5.0 declared is retired in v0.6.0 (the OpenExec plan §1, §4.1).
#   2. The runtime carries OpenExec. From v0.6.0 OpenExec is a first-class
#      execution basis, not an optional experiment, so a runtime without it is
#      rejected at configure time rather than at link time inside execMotion.
#
# Every entry point that resolves OpenUSD includes this immediately after its
# `find_package(pxr ...)`: the root project, each bundle under plugins/, each
# OpenUSD-dependent library under libs/, and each tool under tools/. A bundle
# built standalone by `ost plugin build` never composes the root project, so
# the pin has to travel with the find_package call, not with the root.
#
# WHY NOT `find_package(pxr 26.08 EXACT ...)`: OpenUSD installs no
# pxrConfigVersion.cmake, so any version argument makes find_package fail with
# "no config version file" no matter which OpenUSD is present. pxrConfig.cmake
# does set the version variables directly, and those are what this tests.
#
# Sets, for callers that report build metadata:
#   USDVRM_OPENUSD_RELEASE       - "26.08"
#   USDVRM_OPENEXEC_AVAILABLE    - TRUE (configure fails otherwise)
#   USDVRM_OPENEXEC_COMPONENTS   - the exec libraries found, as a list
#
# The two USDVRM_OPENUSD_REQUIRED_* values below are the pin itself, and they
# have a consumer outside CMake: scripts/check_docs.py greps them by name to
# check that the bundle manifests and the supported-configurations reference
# say the same thing. Renaming one fails that check rather than silencing it.
include_guard(GLOBAL)

# The single supported point. `PXR_VERSION` is OpenUSD's own packed form: 2608
# is 26.08. The display form is built from MINOR/PATCH because OpenUSD's
# PXR_MAJOR_VERSION is 0 -- "0.26.8", the value pxrConfig.cmake publishes, is
# not what anyone calls this release.
set(USDVRM_OPENUSD_REQUIRED_PXR_VERSION 2608)
set(USDVRM_OPENUSD_REQUIRED_RELEASE "26.08")

if(NOT pxr_FOUND)
    message(FATAL_ERROR
        "UsdVrmOpenUsd.cmake was included before OpenUSD was resolved. "
        "Include it after find_package(pxr REQUIRED CONFIG).")
endif()

# ---------------------------------------------------------------------------
# 1. Exact version
# ---------------------------------------------------------------------------
if(NOT DEFINED PXR_VERSION)
    message(FATAL_ERROR
        "This OpenUSD install publishes no PXR_VERSION, so its version cannot "
        "be verified. usd-vrm-plugins ${PROJECT_VERSION} requires OpenUSD "
        "${USDVRM_OPENUSD_REQUIRED_RELEASE} exactly.\n"
        "  pxrConfig.cmake: ${pxr_DIR}")
endif()

if(DEFINED PXR_MINOR_VERSION AND DEFINED PXR_PATCH_VERSION)
    # 26 + 8 -> "26.08"; OpenUSD zero-pads the month in every name it uses.
    string(REGEX REPLACE "^([0-9])$" "0\\1" _usdvrm_patch "${PXR_PATCH_VERSION}")
    set(USDVRM_OPENUSD_RELEASE "${PXR_MINOR_VERSION}.${_usdvrm_patch}")
    unset(_usdvrm_patch)
else()
    set(USDVRM_OPENUSD_RELEASE "${PXR_VERSION}")
endif()

if(NOT PXR_VERSION EQUAL USDVRM_OPENUSD_REQUIRED_PXR_VERSION)
    message(FATAL_ERROR
        "Unsupported OpenUSD: found ${USDVRM_OPENUSD_RELEASE} "
        "(PXR_VERSION ${PXR_VERSION}), require "
        "${USDVRM_OPENUSD_REQUIRED_RELEASE} "
        "(PXR_VERSION ${USDVRM_OPENUSD_REQUIRED_PXR_VERSION}) exactly.\n"
        "  pxrConfig.cmake: ${pxr_DIR}\n"
        "v0.6.0 pins OpenUSD to one version: OpenUSD guarantees no ABI "
        "stability across releases, and the execMotion/execVrm bundles build "
        "on OpenExec, whose API is not stable either. See "
        "docs/reference/SUPPORTED_CONFIGURATIONS.md.")
endif()

# ---------------------------------------------------------------------------
# 2. OpenExec capability probe
# ---------------------------------------------------------------------------
# 26.08 has no OpenExec build toggle -- build_usd.py ships these
# unconditionally -- so this is a detection check, not a build-option check. A
# runtime can still lack them: a slimmed export, a hand-built install with
# components stripped, or any OpenUSD that predates OpenExec. Each component is
# probed by both its imported target (does it link?) and one header (is the
# development half installed?), because ost stages those two halves separately
# and a runtime missing one of them fails much later and far less clearly.
set(_usdvrm_openexec_probe
    "exec"            "pxr/exec/exec/system.h"
    "execGeom"        "pxr/exec/execGeom/tokens.h"
    "execIr"          "pxr/exec/execIr/controller.h"
    "execUsd"         "pxr/exec/execUsd/system.h"
    "vdf"             "pxr/exec/vdf/api.h"
    "usdExecImaging"  "pxr/usdImaging/usdExecImaging/stageSceneIndexInterface.h")

set(USDVRM_OPENEXEC_COMPONENTS)
set(_usdvrm_openexec_missing)
list(LENGTH _usdvrm_openexec_probe _usdvrm_probe_length)
math(EXPR _usdvrm_probe_last "${_usdvrm_probe_length} - 1")
foreach(_i RANGE 0 ${_usdvrm_probe_last} 2)
    list(GET _usdvrm_openexec_probe ${_i} _component)
    math(EXPR _j "${_i} + 1")
    list(GET _usdvrm_openexec_probe ${_j} _header)

    # PXR_INCLUDE_DIRS is one directory in every install we ship against, but
    # it is documented as a list, so treat it as one.
    set(_usdvrm_header_found FALSE)
    foreach(_dir IN LISTS PXR_INCLUDE_DIRS)
        if(EXISTS "${_dir}/${_header}")
            set(_usdvrm_header_found TRUE)
            break()
        endif()
    endforeach()

    if(NOT TARGET ${_component})
        list(APPEND _usdvrm_openexec_missing
             "${_component} (no imported CMake target)")
    elseif(NOT _usdvrm_header_found)
        list(APPEND _usdvrm_openexec_missing
             "${_component} (headers absent: ${_header})")
    else()
        list(APPEND USDVRM_OPENEXEC_COMPONENTS "${_component}")
    endif()
endforeach()
# This module is included into the scope of every project in the workspace, so
# it leaves none of its working variables behind -- including the loop
# variables, which have names an unrelated loop in an includer could reuse.
unset(_usdvrm_openexec_probe)
unset(_usdvrm_probe_length)
unset(_usdvrm_probe_last)
unset(_usdvrm_header_found)
unset(_component)
unset(_header)
unset(_i)
unset(_j)
unset(_dir)

if(_usdvrm_openexec_missing)
    string(REPLACE ";" "\n  - " _usdvrm_openexec_missing_text
           "${_usdvrm_openexec_missing}")
    message(FATAL_ERROR
        "This OpenUSD ${USDVRM_OPENUSD_RELEASE} install has no usable "
        "OpenExec. Missing:\n  - ${_usdvrm_openexec_missing_text}\n"
        "  OpenUSD prefix: ${PXR_INCLUDE_DIRS}\n"
        "From v0.6.0 OpenExec is a first-class execution basis, not an "
        "optional experiment (the OpenExec plan §1). Build or pull an OpenUSD "
        "26.08 that installs the exec libraries and their headers.")
endif()
unset(_usdvrm_openexec_missing)

set(USDVRM_OPENEXEC_AVAILABLE TRUE)

# include_guard(GLOBAL) makes this the only time the line is printed per
# configure, however many members include the module.
string(REPLACE ";" " " _usdvrm_components_text "${USDVRM_OPENEXEC_COMPONENTS}")
message(STATUS
    "OpenUSD ${USDVRM_OPENUSD_RELEASE} (PXR_VERSION ${PXR_VERSION}), "
    "OpenExec: ${_usdvrm_components_text}")
unset(_usdvrm_components_text)
