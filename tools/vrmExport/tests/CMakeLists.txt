# SPDX-License-Identifier: Apache-2.0

# Same derivation the plugin bundles use: pxr_DIR points at the config package,
# three levels below the runtime prefix that holds bin/ and lib/.
set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()

get_filename_component(_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(_vrm_fixtures "${_repo_root}/plugins/usdVrmFileFormat/tests/fixtures")
set(_vrm_corpus "${_repo_root}/plugins/usdVrmFileFormat/tests/corpus")

# The export operation's unit tests. They run with NO VRM plugin registered --
# `unset:` clears whatever the session composed -- which is what makes the one
# `.vrm` case a test of the missing-plugin refusal.
add_executable(vrm_export_operation_tests test_vrm_export_operation.cpp)
target_link_libraries(vrm_export_operation_tests PRIVATE vrm_export_operation)
# assert() is the check, and NDEBUG compiles it away in Release, the default.
target_compile_options(vrm_export_operation_tests
    PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG /utf-8,-UNDEBUG>)
if(MSVC)
    target_compile_definitions(vrm_export_operation_tests PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()
set(_unit_fixture "")
if(EXISTS "${_vrm_fixtures}/minimal.vrm")
    set(_unit_fixture "${_vrm_fixtures}/minimal.vrm")
endif()
add_test(NAME vrm_export_operation
    COMMAND vrm_export_operation_tests ${_unit_fixture})
set_tests_properties(vrm_export_operation PROPERTIES
    ENVIRONMENT_MODIFICATION
        "PXR_PLUGINPATH_NAME=unset:;PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib")

# End to end, over the fixtures and the corpus. It needs the three VRM bundles
# in the same build tree -- the importer to open a `.vrm`, its schemas, and the
# resolver that serves the textures inside one -- so it registers only in the
# composed root build, which adds this tool after them.
if(NOT Python3_Interpreter_FOUND)
    message(STATUS "vrmExport: Python3 interpreter not found; skipping the end-to-end test")
elseif(NOT (TARGET UsdVrmFileFormat AND TARGET UsdVrmPackageResolver
            AND TARGET vrmContainer))
    message(STATUS "vrmExport: the VRM bundles are not in this build tree; "
                   "skipping the end-to-end test")
else()
    # The bundles as their own suites load them: plugInfo.json files configured
    # by each bundle's tests with an absolute LibraryPath, and vrmSchema's from
    # its source tree.
    set(_plugins_bin "${CMAKE_BINARY_DIR}/plugins")
    set(_fileformat_resources "${_plugins_bin}/usdVrmFileFormat/tests/resources/usdVrmFileFormat")
    set(_resolver_resources
        "${_plugins_bin}/usdVrmPackageResolver/tests/resources/usdVrmPackageResolver")
    set(_schema_resources "${_repo_root}/plugins/vrmSchema/plugin/resources/vrmSchema")

    set(_inputs)
    foreach(_fixture IN ITEMS
            minimal vrm0_minimal textures materials mtoon_vrm0 mtoon_vrm1
            multiskin_ibm expressions vrm0_expressions lookat springbone
            constraints animation vrm0_frontbake)
        list(APPEND _inputs "${_vrm_fixtures}/${_fixture}.vrm")
    endforeach()
    list(APPEND _inputs
        "${_vrm_corpus}/spec-samples/vrm1/seed-san/Seed-san.vrm"
        "${_vrm_corpus}/spec-samples/vrm1/constraint-twist/VRM1_Constraint_Twist_Sample.vrm")

    add_test(NAME vrm_export_end_to_end
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_vrm_export.py"
            --tool "$<TARGET_FILE:vrm_export>"
            --plugin "fileformat=${_fileformat_resources}"
            --plugin "resolver=${_resolver_resources}"
            --plugin "schema=${_schema_resources}"
            --textured "${_vrm_fixtures}/textures.vrm"
            ${_inputs})
    # The loader path covers every bundle in every environment the test builds;
    # which of them OpenUSD *registers* is PXR_PLUGINPATH_NAME's alone, and the
    # test sets that per case. vrmContainer is named by its in-tree target for
    # the DLL the importer and the resolver load, never linked: this tool links
    # no VRM identity (check_cmake_boundaries.py).
    set_tests_properties(vrm_export_end_to_end PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_usd_root}/bin;PATH=path_list_prepend:${_usd_root}/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmFileFormat>;PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmPackageResolver>;PATH=path_list_prepend:${_repo_root}/plugins/vrmSchema/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer>;PYTHONPATH=path_list_prepend:${_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_usd_root}/lib/site-packages")
endif()
