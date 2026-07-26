# SPDX-License-Identifier: Apache-2.0

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "motionCapture: Python3 interpreter not found; skipping tests")
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

# The tools are subprocesses of the Python test, so the DLL/PYTHONPATH setup has
# to cover both: pxr for the test itself, and the USD runtime libraries for the
# executables it launches.
set(_test_env
    "PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:motion_capture>;PYTHONPATH=path_list_prepend:${_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages")

set(_capture_test_arguments
    --tool "$<TARGET_FILE:motion_capture>"
    --corpus "${_repo_root}/libs/motionRuntime/tests/corpus")

# The full Phase D chain -- capture a session, then bake it onto a real avatar
# with the UNCHANGED Phase C tool -- only exists in a build tree that carries
# both. A standalone configure of this directory has no motion_retarget target,
# and naming one that does not exist fails at generate time, so the chain is
# added conditionally rather than unconditionally with a genex.
if(TARGET motion_retarget)
    list(APPEND _capture_test_arguments
        --retarget-tool "$<TARGET_FILE:motion_retarget>"
        --avatar "${_repo_root}/docs/design/fixtures/motion/avatar.usda"
        --humanoid-map
            "${_repo_root}/tools/motionRetarget/tests/fixtures/design_avatar_humanoid_map.json")
else()
    message(STATUS
        "motionCapture: motion_retarget is not in this build tree; the "
        "capture->retarget end-to-end leg will be skipped")
endif()

add_test(
    NAME motion_capture_replay
    COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_capture.py"
            ${_capture_test_arguments})
set_tests_properties(motion_capture_replay PROPERTIES
    ENVIRONMENT_MODIFICATION "${_test_env}")
