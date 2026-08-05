# SPDX-License-Identifier: Apache-2.0

if(NOT Python3_Interpreter_FOUND)
    message(STATUS
        "motionBvhTools: Python3 interpreter not found; skipping tests")
    return()
endif()

# The library's own corpus, driven through the CLI rather than through a second
# set of fixtures: the tool's claim is that it reports what the parser read, and
# a fixture only this tool ever sees could not check that.
get_filename_component(_motion_bvh_lib "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/motionBvh"
                       ABSOLUTE)

if(EXISTS "${_motion_bvh_lib}/tests/corpus/generated")
    add_test(NAME motion_bvh_inspect_report
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_bvh_inspect.py"
                --tool "$<TARGET_FILE:motion_bvh_inspect>"
                --corpus "${_motion_bvh_lib}/tests/corpus")
else()
    message(STATUS
        "motionBvhTools: the motionBvh corpus is not beside this tool; "
        "skipping the report test")
endif()

# No ENVIRONMENT_MODIFICATION, unlike every other tool's test in this workspace.
# This executable references nothing outside motionBvh's syntax half, so on
# Windows and Linux it records no OpenUSD import and a plain `ctest` finds
# everything it needs. On macOS it now records the dylibs anyway -- motionBvh's
# declared edge puts them on the link line and ld64 keeps every one of them --
# which costs nothing here because they sit on the runtime's own path. That
# difference is measured in libs/motionBvh/tests/check_boundaries.py, and it is
# why the check below no longer inspects a binary: it was reporting the linker.
if(EXISTS "${_motion_bvh_lib}/tests/check_boundaries.py")
    add_test(NAME motion_bvh_inspect_boundaries
        COMMAND "${Python3_EXECUTABLE}"
                "${_motion_bvh_lib}/tests/check_boundaries.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/.."
                --cmake-target motion_bvh_inspect)

    # The same check pointed at a target that does not exist, and it must FAIL.
    # `--cmake-target` filters which link lines are examined, so a misspelled
    # one examines none and the link check quietly stops existing while still
    # printing "boundary check passed". A check nobody has watched fail is a
    # check nobody knows still works.
    add_test(NAME motion_bvh_inspect_boundariesGuard
        COMMAND "${Python3_EXECUTABLE}"
                "${_motion_bvh_lib}/tests/check_boundaries.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/.."
                --cmake-target no_such_target)
    set_tests_properties(motion_bvh_inspect_boundariesGuard PROPERTIES
        WILL_FAIL TRUE)

    # The converter's boundary is a different one, and `--crossing` is where
    # that is said out loud. It may author a stage and speak the humanoid
    # vocabulary -- that is what a converter is -- and it may not name
    # vrmRetarget or vrmSchema, because the target avatar is the one thing this
    # layer never binds to (roadmap §4). Two executables in one directory means
    # two invocations, not one relaxed rule for both.
    add_test(NAME motion_bvh_convert_boundaries
        COMMAND "${Python3_EXECUTABLE}"
                "${_motion_bvh_lib}/tests/check_boundaries.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/.."
                --cmake-target motion_bvh_convert --crossing)
endif()

# The converter, over the same recorded export the library's own conversion test
# drives -- but checking the clip on disk rather than the values in memory,
# which is the half nothing else covers.
get_filename_component(_motion_bvh_profiles
                       "${CMAKE_CURRENT_SOURCE_DIR}/../../../profiles/motion"
                       ABSOLUTE)
if(EXISTS "${_motion_bvh_lib}/tests/corpus/recorded/redistributable"
   AND EXISTS "${_motion_bvh_profiles}")
    # Unlike the inspect tool's tests, this one needs the USD runtime twice
    # over: the executable it launches links the stage libraries, and the script
    # itself opens the result with pxr.
    set(_usd_root "")
    if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
        set(_usd_root "${pxr_DIR}")
    elseif(pxr_DIR)
        get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
    endif()

    add_test(NAME motion_bvh_convert_clip
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test_motion_bvh_convert.py"
                --tool "$<TARGET_FILE:motion_bvh_convert>"
                --corpus "${_motion_bvh_lib}/tests/corpus"
                --profiles "${_motion_bvh_profiles}")
    set_tests_properties(motion_bvh_convert_clip PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:motion_bvh_convert>;PYTHONPATH=path_list_prepend:${_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages")
else()
    message(STATUS
        "motionBvhTools: the recorded corpus or profiles/motion is not beside "
        "this tool; skipping the conversion test")
endif()
