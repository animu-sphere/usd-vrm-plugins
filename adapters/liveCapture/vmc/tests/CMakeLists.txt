# SPDX-License-Identifier: Apache-2.0

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would
# print "passed" and return 0 without having checked anything. Undefine it for
# the test target only: the library under test stays Release, the checks stay
# live. See libs/motionRuntime/tests/CMakeLists.txt, which hit this first.
set(_vrm_adapter_vmc_assertions
    $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(vrmAdapterVmc_tests test_vrm_adapter_vmc.cpp)
target_link_libraries(vrmAdapterVmc_tests PRIVATE vrmAdapterVmc::vrmAdapterVmc)
target_compile_options(vrmAdapterVmc_tests
    PRIVATE ${_vrm_adapter_vmc_assertions})
add_test(NAME vrmAdapterVmc_unit COMMAND vrmAdapterVmc_tests)

if(WIN32)
    if(TARGET gf)
        set(_vrm_adapter_vmc_gf gf)
    else()
        set(_vrm_adapter_vmc_gf pxr::gf)
    endif()
    # The adapter is a static library, but motionCore and motionRuntime call
    # exported Gf entry points, so the test executable loads usd_gf.dll at run
    # time -- which pulls usd_tf.dll -> tbb.dll from bin/ plus python3xx.dll. An
    # activated `ost` session has all three on PATH; a plain-CMake `ctest` run
    # does not, and the test exits 0xC0000135 without them.
    set(_vrm_adapter_vmc_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_adapter_vmc_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_adapter_vmc_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_vrm_adapter_vmc_python_dir
                               "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _vrm_adapter_vmc_test_path
             "PATH=path_list_prepend:${_vrm_adapter_vmc_python_dir}")
    endif()
    set_tests_properties(vrmAdapterVmc_unit PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vrm_adapter_vmc_test_path}")
endif()

if(Python3_Interpreter_FOUND)
    # The *test executable*, not $<TARGET_FILE:vrmAdapterVmc>. The adapter is a
    # static library and an archive records no imports at all, so inspecting it
    # is a check that cannot fail. This executable is the first artifact in
    # which the adapter's real transitive imports exist, and its link closure is
    # exactly the one the boundary is about: the adapter, motionCore, and
    # motionRuntime.
    add_test(NAME vrmAdapterVmc_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:vrmAdapterVmc_tests>")
endif()
