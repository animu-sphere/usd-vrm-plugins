# SPDX-License-Identifier: Apache-2.0

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "motionRetarget: Python3 interpreter not found; skipping tests")
    return()
endif()

# Same derivation the plugin bundles use: pxr_DIR points at the config package,
# three levels below the runtime prefix that holds bin/ and lib/.
set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()

get_filename_component(_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

# The tool is a subprocess of the Python test, so the DLL/PYTHONPATH setup has
# to cover both: pxr for the test itself, and the USD runtime libraries for the
# executable it launches.
set(_test_env
    "PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:motion_retarget>;PYTHONPATH=path_list_prepend:${_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages")

add_test(
    NAME motion_retarget_design_triplet
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_retarget.py"
            --tool "$<TARGET_FILE:motion_retarget>"
            --fixtures "${_repo_root}/docs/design/fixtures/motion"
            --tool-fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
            --humanoid-map
                "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/design_avatar_humanoid_map.json")
set_tests_properties(motion_retarget_design_triplet PROPERTIES
    ENVIRONMENT_MODIFICATION "${_test_env}")
