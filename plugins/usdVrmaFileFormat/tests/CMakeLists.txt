# SPDX-License-Identifier: Apache-2.0

set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "usdVrmaFileFormat: Python3 interpreter not found; skipping tests")
    return()
endif()

set(_bundle_dir "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(_lib_dir "${_bundle_dir}/lib")
set(_vrm_container_dir "$<TARGET_FILE_DIR:vrmContainer::vrmContainer>")
set(_motion_core_dir "$<TARGET_FILE_DIR:motionCore::motionCore>")
set(USDVRMA_TEST_LIBRARYPATH
    "${_lib_dir}/lib${PLUGIN_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(_plug_resources "${CMAKE_CURRENT_BINARY_DIR}/resources/usdVrmaFileFormat")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/plugInfo.in.json"
    "${_plug_resources}/plugInfo.json"
    @ONLY)

set(_test_env
    "PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib;PATH=path_list_prepend:${_lib_dir};PATH=path_list_prepend:${_vrm_container_dir};PATH=path_list_prepend:${_motion_core_dir};PYTHONPATH=path_list_prepend:${_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages;PXR_PLUGINPATH_NAME=path_list_prepend:${_plug_resources}")

add_test(
    NAME usdvrma_python_smoke
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/test_usdvrma_plugin.py")
set_tests_properties(usdvrma_python_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${_test_env}")

get_filename_component(_repo_root "${_bundle_dir}/../.." ABSOLUTE)
add_test(
    NAME usdvrma_fixture_deterministic
    COMMAND ${Python3_EXECUTABLE}
            "${_bundle_dir}/tools/generate_fixtures.py" --check)
set_tests_properties(usdvrma_fixture_deterministic PROPERTIES
    ENVIRONMENT_MODIFICATION "${_test_env}")
