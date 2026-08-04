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

# No ENVIRONMENT_MODIFICATION, unlike every other tool's test in this workspace:
# this executable links motionBvh and nothing else, so it loads no OpenUSD DLL
# and a plain `ctest` finds everything it needs. The check below is what keeps
# that true rather than merely currently so.
if(EXISTS "${_motion_bvh_lib}/tests/check_boundaries.py")
    add_test(NAME motion_bvh_inspect_boundaries
        COMMAND "${Python3_EXECUTABLE}"
                "${_motion_bvh_lib}/tests/check_boundaries.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/.."
                "$<TARGET_FILE:motion_bvh_inspect>"
                --cmake-target motion_bvh_inspect)
endif()
