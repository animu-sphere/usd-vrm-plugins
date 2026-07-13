# SPDX-License-Identifier: Apache-2.0
#
# Plain-CMake (non-OpenStrata) test wiring for the schema bundle. The
# authoritative verification path is `ost plugin test plugins/vrmSchema`; this
# CTest target exists so users building with bare CMake still get a schema
# discovery/contract smoke test. Mirrors plugins/usdVrm/tests/CMakeLists.txt.

# Resolve the OpenUSD install root from the pxr CMake package location.
set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "vrmSchema: Python3 interpreter not found; skipping schema tests")
    return()
endif()

set(_bundle_dir "${CMAKE_CURRENT_SOURCE_DIR}/..")

# The source-tree plugInfo.json is configured for this platform by the bundle
# CMakeLists (LibraryPath -> ../../../lib/, where the build stages the DLL), so
# the tests can point the registry straight at the committed resources dir.
set(_plug_resources "${_bundle_dir}/plugin/resources/vrmSchema")

set(_schema_env
    "PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib;PATH=path_list_prepend:${_bundle_dir}/lib;PYTHONPATH=path_list_prepend:${_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages;PXR_PLUGINPATH_NAME=path_list_prepend:${_plug_resources}")

add_test(
    NAME vrmschema_plugin
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vrmschema_plugin.py")

set_tests_properties(vrmschema_plugin PROPERTIES
    ENVIRONMENT_MODIFICATION "${_schema_env}")
