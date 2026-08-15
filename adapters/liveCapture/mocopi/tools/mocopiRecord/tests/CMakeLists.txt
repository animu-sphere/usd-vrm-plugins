# SPDX-License-Identifier: Apache-2.0

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "mocopiRecord: Python3 interpreter not found; skipping tests")
    return()
endif()

# Four names, split the way every other claim in this adapter is split: what can
# be checked without a socket, what needs one, what needs a socket this runner
# may not have, and what needs bytes a decoder can decode.
#
# Three of the four take no corpus argument, and that absence is still the point
# rather than an omission. The sibling's tests read committed captures because
# its protocol is published and its corpus could be written; this adapter's
# corpus was *measured* off a device afterwards, and the tool that obtained it
# has no opinion about what a datagram means — so its envelope checks author
# their own bytes in Python, which makes the reader check independent of the
# writer exactly as the sibling's Python capture *parser* is.
#
# `mocopi_record_export` is the exception and names its reason: an export
# decodes, so its fixtures have to be things the decoder can decode. Inventing
# those in Python would be a second packet generator, silently disagreeing with
# `tools/generate_packets.py` at whatever point nobody checked.
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

add_test(NAME mocopi_record_export
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_mocopi_record.py"
            --tool "$<TARGET_FILE:mocopi_record>"
            --mode export
            --corpus "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/corpus")

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

# The chain a mocopi session takes to a rig, which exists only in a build tree
# that carries the product's tools as well as this adapter. A standalone
# configure of the adapter has neither target, and naming one that does not
# exist fails at generate time -- so the guard, exactly as the sibling adapter
# and tools/motionCapture use one.
#
# It is the same arrangement as `vmc_record_endToEnd` and it is deliberately a
# second test rather than a shared one: WORKSPACE.md §2 forbids an adapter from
# knowing its sibling exists, and a shared end-to-end driver would be that
# knowledge in a test directory instead of a link line.
if(TARGET motion_capture AND TARGET motion_retarget)
    # And onto a released VRM as well, where the same tree carries the importer
    # to open one with -- the v0.7.0 condition's own wording, a session reaching
    # a *real* VRM avatar. An addition rather than a replacement: the fixture
    # beside this file binds every bone this device sends, and a shipped model
    # does not, which is the finding that half exists to keep.
    set(_mocopi_vrm_avatar "")
    if(TARGET UsdVrmFileFormat)
        set(_mocopi_vrm_avatar
            "${CMAKE_SOURCE_DIR}/plugins/usdVrmFileFormat/tests/corpus/spec-samples/vrm1/seed-san/Seed-san.vrm")
        if(NOT EXISTS "${_mocopi_vrm_avatar}")
            set(_mocopi_vrm_avatar "")
        endif()
    endif()
    set(_mocopi_vrm_argument "")
    if(_mocopi_vrm_avatar)
        set(_mocopi_vrm_argument --vrm-avatar "${_mocopi_vrm_avatar}")
    endif()

    add_test(NAME mocopi_record_endToEnd
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test_mocopi_end_to_end.py"
                --record-tool "$<TARGET_FILE:mocopi_record>"
                --capture-tool "$<TARGET_FILE:motion_capture>"
                --retarget-tool "$<TARGET_FILE:motion_retarget>"
                --corpus "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/corpus"
                --avatar "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mocopi_avatar.usda"
                ${_mocopi_vrm_argument})
else()
    message(STATUS
        "mocopiRecord: the product's motion tools are not in this build tree; "
        "the mocopi->rig end-to-end test will be skipped")
endif()

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
                         mocopi_record_export mocopi_record_ipv6 PROPERTIES
        ENVIRONMENT_MODIFICATION "${_mocopi_record_test_path}")
endif()

if(TEST mocopi_record_endToEnd)
    # This one needs more than the tools' own DLLs: it opens the baked stage
    # through `pxr` in Python, so the USD runtime has to be importable and not
    # merely loadable. Derived the way tools/motionCapture derives it -- pxr_DIR
    # points at the config package, three levels below the prefix holding bin/,
    # lib/ and the Python packages.
    set(_mocopi_usd_root "")
    if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
        set(_mocopi_usd_root "${pxr_DIR}")
    elseif(pxr_DIR)
        get_filename_component(_mocopi_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
    endif()
    set(_mocopi_e2e_env
        "PATH=path_list_prepend:${_mocopi_usd_root}/bin;PATH=path_list_prepend:${_mocopi_usd_root}/lib;PYTHONPATH=path_list_prepend:${_mocopi_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_mocopi_usd_root}/lib/site-packages")
    if(_mocopi_vrm_avatar)
        # Mirrored from plugins/usdVrmFileFormat/tests/CMakeLists.txt, which is
        # the environment that bundle's own smoke tests run in on all three OS:
        # the build-tree plugInfo (absolute LibraryPath, so no working-directory
        # assumption), the plugin's own lib/, and the sibling vrmSchema bundle,
        # whose typed Vrm*API schemas the importer applies and whose library
        # libUsdVrmFileFormat links.
        set(_mocopi_schema_bundle "${CMAKE_SOURCE_DIR}/plugins/vrmSchema")
        string(APPEND _mocopi_e2e_env
            ";PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmFileFormat>"
            ";PATH=path_list_prepend:${_mocopi_schema_bundle}/lib"
            ";PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer::vrmContainer>"
            ";PXR_PLUGINPATH_NAME=path_list_prepend:${CMAKE_BINARY_DIR}/plugins/usdVrmFileFormat/tests/resources/usdVrmFileFormat"
            ";PXR_PLUGINPATH_NAME=path_list_prepend:${_mocopi_schema_bundle}/plugin/resources/vrmSchema")
    endif()
    set_tests_properties(mocopi_record_endToEnd PROPERTIES
        ENVIRONMENT_MODIFICATION "${_mocopi_e2e_env}")
endif()
