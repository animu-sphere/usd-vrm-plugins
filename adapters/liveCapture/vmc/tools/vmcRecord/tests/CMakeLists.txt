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
