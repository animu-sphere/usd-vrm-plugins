# SPDX-License-Identifier: Apache-2.0
#
# UsdVrmExecMotion.cmake — where the consumed `execMotion` bundle is.
#
# `execMotion` is usd-motion-plugins' published OpenExec bundle, the one bundle
# a member here consumes from another repository. `execVrm` declares it in
# `requires.bundles` with a per-target archive-digest pin; this repository's
# own copy was deleted in MIG-2 once the parity rows passed against it.
#
# Nothing links it. What a suite needs is the bundle's directory, to put its
# `plugin/resources/execMotion` on PXR_PLUGINPATH_NAME and its `lib/` on PATH
# as it once put a sibling's. `ost` 0.23.5 materializes the pinned archive for
# the root build and writes its root into the toolchain as
# OPENSTRATA_EXTERNAL_BUNDLE_execMotion_ROOT (ost report 46). A build `ost`
# does not configure can name an extracted bundle with USDVRM_EXEC_MOTION_ROOT.
#
# Sets:
#   USDVRM_EXEC_MOTION_BUNDLE     - the bundle root, or empty
#   USDVRM_EXEC_MOTION_RESOURCES  - its plugInfo.json directory, or empty
#
# Empty means no suite that composes the bundle is registered, and every caller
# says so with a status message rather than registering a test that cannot
# load what it tests. `ost plugin build` of a single member configures without
# it; the root build, which is where those suites run, always has it.

# No include_guard: the result is ordinary variables, which a directory scope
# does not share with its parent, so every directory that needs them includes
# this file itself. Including it twice resolves the same answer twice.

set(USDVRM_EXEC_MOTION_ROOT "" CACHE PATH
    "An extracted execMotion bundle, for a build ost does not configure")

set(USDVRM_EXEC_MOTION_BUNDLE "")
set(USDVRM_EXEC_MOTION_RESOURCES "")
if(USDVRM_EXEC_MOTION_ROOT)
    set(_usdvrm_exec_motion_root "${USDVRM_EXEC_MOTION_ROOT}")
elseif(OPENSTRATA_EXTERNAL_BUNDLE_execMotion_ROOT)
    set(_usdvrm_exec_motion_root "${OPENSTRATA_EXTERNAL_BUNDLE_execMotion_ROOT}")
else()
    set(_usdvrm_exec_motion_root "")
endif()

if(_usdvrm_exec_motion_root)
    set(_usdvrm_exec_motion_plug
        "${_usdvrm_exec_motion_root}/plugin/resources/execMotion")
    if(EXISTS "${_usdvrm_exec_motion_plug}/plugInfo.json")
        set(USDVRM_EXEC_MOTION_BUNDLE "${_usdvrm_exec_motion_root}")
        set(USDVRM_EXEC_MOTION_RESOURCES "${_usdvrm_exec_motion_plug}")
    else()
        # A named root that holds no bundle is a mistake, not an absence.
        message(FATAL_ERROR
            "execMotion: '${_usdvrm_exec_motion_root}' has no "
            "plugin/resources/execMotion/plugInfo.json")
    endif()
endif()
