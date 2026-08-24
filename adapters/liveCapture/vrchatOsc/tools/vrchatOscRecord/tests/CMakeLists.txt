# SPDX-License-Identifier: Apache-2.0

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "vrchatOscRecord: Python3 interpreter not found; skipping tests")
    return()
endif()

# Three names, split the way every other claim in this adapter is split: what can
# be checked without a socket, what needs one, and what needs a socket this
# runner may not have.
#
# None of the three takes a corpus argument, and that absence is the milestone
# rather than an omission. The sibling tools' equivalent suites read committed
# captures because there are some; this adapter's corpus is empty until a device
# session and a decoder exist (VRC-1, VRC-2), so these checks author their own
# bytes in Python -- which makes the reader check independent of the writer, and
# is what lets the loopback name assert VRC-0's done-condition end to end rather
# than through the C++ that produced it.
add_test(NAME vrchat_osc_record_inspect
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vrchat_osc_record.py"
            --tool "$<TARGET_FILE:vrchat_osc_record>"
            --mode inspect)

# Binds loopback sockets on OS-assigned ports, so a lane that forbids binds
# excludes this name and `vrchat_osc_record_ipv6` along with the adapter's three.
add_test(NAME vrchat_osc_record_loopback
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vrchat_osc_record.py"
            --tool "$<TARGET_FILE:vrchat_osc_record>"
            --mode loopback)

# The IPv6 warning, on its own name so that "could not check this here" is a word
# in the ctest summary rather than a line nobody sees -- the arrangement
# `vrmAdapterVrchatOsc_udpReceiverTruncation` uses in this adapter for the same
# reason. A runner without IPv6 loopback cannot bind `::1`.
#
# The claim is worth a name of its own besides. `UdpReceiver` deliberately does
# not refuse an IPv6 bind, because a socket inventing a restriction on itself
# from an *application's* configuration is the wrong layer for it. This tool is
# the right layer, and this test is what makes that split a fact.
add_test(NAME vrchat_osc_record_ipv6
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vrchat_osc_record.py"
            --tool "$<TARGET_FILE:vrchat_osc_record>"
            --mode ipv6)
set_tests_properties(vrchat_osc_record_ipv6 PROPERTIES SKIP_RETURN_CODE 77)

# There is no `_endToEnd` name here, where both sibling tools have one. Theirs
# drives a recorded session through `motion_capture` and `motion_retarget` onto a
# rig, which needs a decoded pose -- and this adapter produces none until VRC-5
# decides where the humanoid solve lives. Naming the absence rather than leaving
# the reader to notice it: the chain is missing a link on purpose, not a test.
