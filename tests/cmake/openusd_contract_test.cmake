# SPDX-License-Identifier: Apache-2.0
#
# Drives cmake/UsdVrmOpenUsd.cmake against fixture OpenUSD installs and checks
# that it accepts exactly one of them.
#
# The workspace's OpenUSD contract is only as good as its refusals, and every
# runtime we build against satisfies it -- so on a normal build the pin and the
# OpenExec probe are code that never fires. This is what fires them.
#
# Run by CTest (see the root CMakeLists.txt), as:
#   cmake -DFIXTURE_DIR=... -DWORK_DIR=... -DPROBE_GENERATOR=... -P <this>
cmake_minimum_required(VERSION 3.22)

foreach(_required FIXTURE_DIR WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

set(_failures 0)

# name / pxr version / omitted imported targets / omitted headers / expectation
# The expectation is either ACCEPT or a regex the configure error must match.
set(_cases
    "supported_runtime@2608@@@ACCEPT"
    "openusd_too_old@2505@@@Unsupported OpenUSD: found 25\\.05"
    "openusd_too_new@2611@@@Unsupported OpenUSD: found 26\\.11"
    "openexec_library_missing@2608@execIr@@execIr \\(no imported CMake target\\)"
    "openexec_headers_missing@2608@@usdExecImaging@usdExecImaging \\(headers absent")

foreach(_case IN LISTS _cases)
    string(REPLACE "@" ";" _fields "${_case}")
    list(GET _fields 0 _name)
    list(GET _fields 1 _version)
    list(GET _fields 2 _missing_targets)
    list(GET _fields 3 _missing_headers)
    list(GET _fields 4 _expect)

    set(_build "${WORK_DIR}/${_name}")
    # Each case gets a clean tree: a cached PXR_INCLUDE_DIRS or a header left
    # behind by an earlier case would silently satisfy the next one.
    file(REMOVE_RECURSE "${_build}")

    set(_generator_args)
    if(PROBE_GENERATOR)
        list(APPEND _generator_args "-G" "${PROBE_GENERATOR}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${FIXTURE_DIR}" -B "${_build}"
                ${_generator_args}
                "-DPROBE_PXR_VERSION=${_version}"
                "-DPROBE_MISSING_TARGETS=${_missing_targets}"
                "-DPROBE_MISSING_HEADERS=${_missing_headers}"
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE _stderr
        RESULT_VARIABLE _rc)
    set(_output "${_stdout}${_stderr}")

    if(_expect STREQUAL "ACCEPT")
        if(NOT _rc EQUAL 0)
            message(SEND_ERROR
                "[${_name}] expected the contract to accept this OpenUSD, but "
                "configure failed (rc=${_rc}):\n${_output}")
            math(EXPR _failures "${_failures} + 1")
        elseif(NOT _output MATCHES "FIXTURE ACCEPTED release=26\\.08")
            message(SEND_ERROR
                "[${_name}] configure succeeded but the contract module did "
                "not report an accepted 26.08:\n${_output}")
            math(EXPR _failures "${_failures} + 1")
        else()
            message(STATUS "[${_name}] accepted, as expected")
        endif()
    else()
        if(_rc EQUAL 0)
            message(SEND_ERROR
                "[${_name}] expected the contract to REJECT this OpenUSD, but "
                "configure succeeded:\n${_output}")
            math(EXPR _failures "${_failures} + 1")
        elseif(NOT _output MATCHES "${_expect}")
            message(SEND_ERROR
                "[${_name}] rejected, but not for the stated reason. Expected "
                "a message matching '${_expect}':\n${_output}")
            math(EXPR _failures "${_failures} + 1")
        else()
            message(STATUS "[${_name}] rejected, as expected")
        endif()
    endif()
endforeach()

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} OpenUSD contract case(s) failed")
endif()
message(STATUS "OpenUSD contract: all cases behaved as specified")
