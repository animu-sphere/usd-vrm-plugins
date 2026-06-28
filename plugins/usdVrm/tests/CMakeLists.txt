# SPDX-License-Identifier: Apache-2.0
#
# Plain-CMake (non-OpenStrata) test wiring. The authoritative verification path
# is `ost plugin test plugins/usdVrm` (the L0-L6 pyramid); this CTest target
# exists so users building with bare CMake still get an end-to-end smoke test.
#
# It drives the freshly built plugin from Python, so it needs OpenUSD's bin/lib
# (and site-packages) plus the bundle's lib/ and plugInfo root on the right env
# vars — injected portably via ENVIRONMENT_MODIFICATION (CMake 3.22+).

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "usdVrm: Python3 interpreter not found; skipping smoke test")
    return()
endif()

# Resolve the OpenUSD install root from the pxr CMake package location.
set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()

set(_bundle_dir "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(_lib_dir "${_bundle_dir}/lib")

# The committed plugInfo.json hard-codes a Windows .dll LibraryPath (for the ost
# flow). For plain-CMake CTest — which must also work on Linux/macOS — generate a
# plugInfo with an absolute, correct-suffix LibraryPath into the build tree and
# point discovery there. CMake paths use forward slashes on every platform, so
# the JSON stays valid. (Cross-platform plugInfo for the ost flow is tracked
# separately in the roadmap.)
set(USDVRM_TEST_LIBRARYPATH
    "${_lib_dir}/lib${PLUGIN_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(_plug_resources "${CMAKE_CURRENT_BINARY_DIR}/resources/usdVrm")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.in.json"
    "${_plug_resources}/plugInfo.json"
    @ONLY)

add_test(
    NAME usdvrm_python_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/test_usdvrm_plugin.py")

set_tests_properties(usdvrm_python_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib;PATH=path_list_prepend:${_lib_dir};PYTHONPATH=path_list_prepend:${_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages;PXR_PLUGINPATH_NAME=path_list_prepend:${_plug_resources}")
