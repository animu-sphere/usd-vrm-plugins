# SPDX-License-Identifier: Apache-2.0

if(NOT Python3_Interpreter_FOUND)
    message(STATUS "vmcRecord: Python3 interpreter not found; skipping tests")
    return()
endif()

set(_vmc_record_corpus "${CMAKE_CURRENT_SOURCE_DIR}/../../../tests/corpus")

# Two tests, split the way every other socket claim in this adapter is split:
# what can be checked from committed bytes, and what needs a socket. The first
# runs anywhere; the second is a separate CTest name so a runner that forbids
# binding excludes it and loses no coverage of the report (roadmap §9.5).
add_test(NAME vmc_record_inspect
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vmc_record.py"
            --tool "$<TARGET_FILE:vmc_record>"
            --corpus "${_vmc_record_corpus}"
            --mode inspect)

add_test(NAME vmc_record_loopback
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vmc_record.py"
            --tool "$<TARGET_FILE:vmc_record>"
            --corpus "${_vmc_record_corpus}"
            --mode loopback)

# The chain a VMC session takes to a rig, which exists only in a build tree that
# carries the product's tools as well as this adapter. A standalone configure of
# the adapter has neither target, and naming one that does not exist fails at
# generate time -- so the guard, exactly as tools/motionCapture uses one for
# motion_retarget.
#
# The adapter is added last from the root CMakeLists (it is a leaf of
# everything), so both targets already exist by the time this runs. That
# ordering is what lets the end-to-end test live with the adapter rather than
# with the product, which is where WORKSPACE.md §2 wants it: the product must
# not know this adapter exists, in a test directory any more than in a link
# line.
if(TARGET motion_capture AND TARGET motion_retarget)
    # And onto a released VRM as well, where the same tree carries the importer
    # to open one with. That is the v0.7.0 condition's own wording -- a session
    # reaching a *real* VRM avatar -- and it is an addition rather than a
    # replacement: the fixture beside this file is shaped to be awkward on
    # purpose, and a shipped model is shaped by whoever shipped it.
    #
    # It adds no edge. This adapter links nothing new, `motion_retarget` takes
    # the same command line, and what changes is only that usdVrmFileFormat is
    # on the plugin registry path when the test runs.
    set(_vmc_vrm_avatar "")
    if(TARGET UsdVrmFileFormat)
        set(_vmc_vrm_avatar
            "${CMAKE_SOURCE_DIR}/plugins/usdVrmFileFormat/tests/corpus/spec-samples/vrm1/seed-san/Seed-san.vrm")
        if(NOT EXISTS "${_vmc_vrm_avatar}")
            set(_vmc_vrm_avatar "")
        endif()
    endif()
    set(_vmc_vrm_argument "")
    if(_vmc_vrm_avatar)
        set(_vmc_vrm_argument --vrm-avatar "${_vmc_vrm_avatar}")
    endif()

    add_test(NAME vmc_record_endToEnd
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/test_vmc_end_to_end.py"
                --record-tool "$<TARGET_FILE:vmc_record>"
                --capture-tool "$<TARGET_FILE:motion_capture>"
                --retarget-tool "$<TARGET_FILE:motion_retarget>"
                --corpus "${_vmc_record_corpus}"
                --avatar "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vmc_avatar.usda"
                ${_vmc_vrm_argument})
else()
    message(STATUS
        "vmcRecord: the product's motion tools are not in this build tree; the "
        "VMC->rig end-to-end test will be skipped")
endif()

if(WIN32)
    # The same runtime the adapter's own tests need, for the same reason: the
    # tool is statically linked against the adapter, but motionCore and
    # motionRuntime call exported Gf entry points, so the process loads
    # usd_gf.dll -> usd_tf.dll -> tbb.dll plus python3xx.dll at start-up. An
    # activated `ost` session has them on PATH; a plain-CMake `ctest` does not,
    # and the tool exits 0xC0000135 before main().
    if(TARGET gf)
        set(_vmc_record_gf gf)
    else()
        set(_vmc_record_gf pxr::gf)
    endif()
    set(_vmc_record_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vmc_record_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vmc_record_gf}>/../bin")
    get_filename_component(_vmc_record_python_dir "${Python3_EXECUTABLE}"
                           DIRECTORY)
    list(APPEND _vmc_record_test_path
         "PATH=path_list_prepend:${_vmc_record_python_dir}")
    set_tests_properties(vmc_record_inspect vmc_record_loopback PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vmc_record_test_path}")
endif()

if(TEST vmc_record_endToEnd)
    # This one needs more than the tools' own DLLs: it opens the baked stage
    # through `pxr` in Python, so the USD runtime has to be importable and not
    # merely loadable. Derived the way tools/motionCapture derives it -- pxr_DIR
    # points at the config package, three levels below the prefix holding bin/,
    # lib/ and the Python packages.
    set(_vmc_usd_root "")
    if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
        set(_vmc_usd_root "${pxr_DIR}")
    elseif(pxr_DIR)
        get_filename_component(_vmc_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
    endif()
    set(_vmc_e2e_env
        "PATH=path_list_prepend:${_vmc_usd_root}/bin;PATH=path_list_prepend:${_vmc_usd_root}/lib;PYTHONPATH=path_list_prepend:${_vmc_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_vmc_usd_root}/lib/site-packages")
    if(_vmc_vrm_avatar)
        # Mirrored from plugins/usdVrmFileFormat/tests/CMakeLists.txt, which is
        # the environment that bundle's own smoke tests run in on all three OS:
        # the build-tree plugInfo (absolute LibraryPath, so no working-directory
        # assumption), the plugin's own lib/, and the sibling vrmSchema bundle,
        # whose typed Vrm*API schemas the importer applies and whose library
        # libUsdVrmFileFormat links.
        set(_vmc_schema_bundle "${CMAKE_SOURCE_DIR}/plugins/vrmSchema")
        string(APPEND _vmc_e2e_env
            ";PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmFileFormat>"
            ";PATH=path_list_prepend:${_vmc_schema_bundle}/lib"
            ";PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer::vrmContainer>"
            ";PXR_PLUGINPATH_NAME=path_list_prepend:${CMAKE_BINARY_DIR}/plugins/usdVrmFileFormat/tests/resources/usdVrmFileFormat"
            ";PXR_PLUGINPATH_NAME=path_list_prepend:${_vmc_schema_bundle}/plugin/resources/vrmSchema")
    endif()
    set_tests_properties(vmc_record_endToEnd PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vmc_e2e_env}")
endif()
