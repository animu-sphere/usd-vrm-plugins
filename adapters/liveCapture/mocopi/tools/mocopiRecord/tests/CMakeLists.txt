# SPDX-License-Identifier: Apache-2.0

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "mocopiRecord: Python3 interpreter not found; skipping tests")
    return()
endif()

# Three names, split the way every other claim in this adapter is split: what can
# be checked without a socket, what needs one, and what needs a socket this
# runner may not have.
#
# No corpus argument, and that absence is the point rather than an omission. The
# sibling's tests read committed captures because its protocol is published and
# its corpus could be written; there is no mocopi corpus yet, and obtaining the
# first one is what this tool exists for. So the fixtures these tests need are
# authored in Python beside them — which makes the reader check independent of
# the writer, exactly as the sibling's Python capture *parser* is.
add_test(NAME mocopi_record_inspect
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_mocopi_record.py"
            --tool "$<TARGET_FILE:mocopi_record>"
            --mode inspect)

add_test(NAME mocopi_record_loopback
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_mocopi_record.py"
            --tool "$<TARGET_FILE:mocopi_record>"
            --mode loopback)

# The IPv6 warning, on its own name so that "could not check this here" is a word
# in the ctest summary rather than a line nobody sees — the arrangement
# `vrmAdapterMocopi_udpReceiverTruncation` established in this adapter for
# exactly the same reason. A runner without IPv6 loopback cannot bind `::1`, and
# inside the loopback suite that would have been invisible: ctest hides a passing
# test's output, so a lane that checked the claim and a lane that quietly declined
# to would print the same line.
#
# The claim is worth a name of its own besides. The vendor documents IPv4 only, so
# an IPv6 bind is a session that can never receive anything — and `UdpReceiver`
# deliberately does not refuse one, because a socket inventing a restriction on
# itself from a *product's* documentation is the wrong layer for it. This tool is
# the right layer, and this test is what makes that split a fact.
add_test(NAME mocopi_record_ipv6
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_mocopi_record.py"
            --tool "$<TARGET_FILE:mocopi_record>"
            --mode ipv6)
set_tests_properties(mocopi_record_ipv6 PROPERTIES SKIP_RETURN_CODE 77)

if(WIN32)
    # The same runtime the adapter's own tests need, for the same reason: the
    # tool is statically linked against the adapter, but motionCore and
    # motionRuntime call exported Gf entry points, so the process loads
    # usd_gf.dll -> usd_tf.dll -> tbb.dll plus python3xx.dll at start-up. An
    # activated `ost` session has them on PATH; a plain-CMake `ctest` does not,
    # and the tool exits 0xC0000135 before main().
    if(TARGET gf)
        set(_mocopi_record_gf gf)
    else()
        set(_mocopi_record_gf pxr::gf)
    endif()
    set(_mocopi_record_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_mocopi_record_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_mocopi_record_gf}>/../bin")
    get_filename_component(_mocopi_record_python_dir "${Python3_EXECUTABLE}"
                           DIRECTORY)
    list(APPEND _mocopi_record_test_path
         "PATH=path_list_prepend:${_mocopi_record_python_dir}")
    set_tests_properties(mocopi_record_inspect mocopi_record_loopback
                         mocopi_record_ipv6 PROPERTIES
        ENVIRONMENT_MODIFICATION "${_mocopi_record_test_path}")
endif()
