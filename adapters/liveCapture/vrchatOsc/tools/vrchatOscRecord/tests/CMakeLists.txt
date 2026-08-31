# SPDX-License-Identifier: Apache-2.0

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "vrchatOscRecord: Python3 interpreter not found; skipping tests")
    return()
endif()

set(_vrchat_osc_record_corpus "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/corpus")

# Four names, split the way every other claim in this adapter is split: what can
# be checked without a socket, what needs one, what needs a socket this runner
# may not have, and what needs bytes a decoder can decode.
#
# Three of the four take no corpus argument, and that is a choice rather than a
# consequence: the generated corpus arrived with VRC-2, and those checks still
# author their own bytes in Python. That is what makes the reader check
# independent of the writer, and what lets the loopback name assert VRC-0's
# done-condition end to end rather than through the C++ that produced it. A tool
# suite fed by this repository's own C++ writer would agree with it by
# construction.
#
# `vrchat_osc_record_export` is the exception and names its reason: an export
# decodes and solves, so its fixtures have to be things the decoder can decode.
# Inventing those in Python would be a second packet generator, silently
# disagreeing with `tools/generate_packets.py` at whatever point nobody checked.
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

add_test(NAME vrchat_osc_record_export
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vrchat_osc_record.py"
            --tool "$<TARGET_FILE:vrchat_osc_record>"
            --mode export
            --corpus "${_vrchat_osc_record_corpus}")

# The chain a VRChat OSC session takes to a rig, which exists only in a build
# tree that carries the product's tools as well as this adapter. A standalone
# configure of the adapter has neither target, and naming one that does not
# exist fails at generate time -- so the guard, exactly as both sibling adapters
# and tools/motionCapture use one.
#
# **This name did not exist until VRC-6, and the absence was stated here rather
# than left to be noticed**: a tracker source produces no pose, so until the
# humanoid solve had a home the chain was missing a link and not a test. It has
# one now (`libs/motionTracking`, VRC-5) and `vrchat_osc_record --export-trace`
# is what calls it, so the leg closes.
#
# It is a second driver rather than a share of a sibling's, and deliberately:
# WORKSPACE.md §2 forbids an adapter from knowing its siblings exist, and a
# shared end-to-end driver would be that knowledge in a test directory instead
# of a link line. There is also no `--vrm-avatar` half here, where both siblings
# have one -- see the test's own note on what a released avatar would add to a
# check that already asserts fourteen unobserved joints holding rest exactly.
if(TARGET motion_capture AND TARGET motion_retarget)
    add_test(NAME vrchat_osc_record_endToEnd
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test_vrchat_osc_end_to_end.py"
                --record-tool "$<TARGET_FILE:vrchat_osc_record>"
                --capture-tool "$<TARGET_FILE:motion_capture>"
                --retarget-tool "$<TARGET_FILE:motion_retarget>"
                --corpus "${_vrchat_osc_record_corpus}"
                --avatar "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vrchat_osc_avatar.usda")
else()
    message(STATUS
        "vrchatOscRecord: the product's motion tools are not in this build "
        "tree; the VRChat OSC->rig end-to-end test will be skipped")
endif()

if(WIN32)
    # The runtime the adapter's own tests need, for the reason both sibling
    # tools' suites give: the tool is statically linked against the adapter, but
    # motionCore, motionTracking and motionRuntime call exported Gf entry
    # points, so the process loads usd_gf.dll -> usd_tf.dll -> tbb.dll plus
    # python3xx.dll at start-up. An activated `ost` session has them on PATH; a
    # plain-CMake `ctest` does not, and the tool exits 0xC0000135 before main().
    #
    # **Every name here needs it now, including the three that predate the
    # export.** Until VRC-6 this tool's closure reached no OpenUSD at all, so
    # this block did not exist; linking motionTracking put usd_gf in front of
    # `--help`.
    if(TARGET gf)
        set(_vrchat_osc_record_gf gf)
    else()
        set(_vrchat_osc_record_gf pxr::gf)
    endif()
    set(_vrchat_osc_record_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrchat_osc_record_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrchat_osc_record_gf}>/../bin")
    get_filename_component(_vrchat_osc_record_python_dir
                           "${Python3_EXECUTABLE}" DIRECTORY)
    list(APPEND _vrchat_osc_record_test_path
         "PATH=path_list_prepend:${_vrchat_osc_record_python_dir}")
    set_tests_properties(vrchat_osc_record_inspect vrchat_osc_record_loopback
                         vrchat_osc_record_ipv6 vrchat_osc_record_export
                         PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vrchat_osc_record_test_path}")
endif()

if(TEST vrchat_osc_record_endToEnd)
    # This one needs more than the tools' own DLLs: it opens the baked stage
    # through `pxr` in Python, so the USD runtime has to be importable and not
    # merely loadable. Derived the way tools/motionCapture derives it -- pxr_DIR
    # points at the config package, three levels below the prefix holding bin/,
    # lib/ and the Python packages.
    set(_vrchat_osc_usd_root "")
    if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
        set(_vrchat_osc_usd_root "${pxr_DIR}")
    elseif(pxr_DIR)
        get_filename_component(_vrchat_osc_usd_root "${pxr_DIR}/../../.."
                               ABSOLUTE)
    endif()
    set_tests_properties(vrchat_osc_record_endToEnd PROPERTIES
        ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${_vrchat_osc_usd_root}/bin;PATH=path_list_prepend:${_vrchat_osc_usd_root}/lib;PYTHONPATH=path_list_prepend:${_vrchat_osc_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_vrchat_osc_usd_root}/lib/site-packages")
endif()
