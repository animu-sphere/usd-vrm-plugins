# SPDX-License-Identifier: Apache-2.0

# NDEBUG is undefined for test targets specifically, as in every suite in this
# workspace: the checks are `assert()`, and a Release suite would otherwise
# print "passed" having checked nothing.
set(_vrm_imaging_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

# ---------------------------------------------------------------------------
# VrmMToonAPI through UsdImaging's stage scene index
# ---------------------------------------------------------------------------
# Links OpenUSD and NOT the plugin: discovery goes through
# PXR_PLUGINPATH_NAME and the staged plugInfo.json, the path a packaged plugin
# takes.
add_executable(vrmImaging_mtoon_tests test_vrm_imaging_mtoon.cpp)
target_include_directories(vrmImaging_mtoon_tests PRIVATE ${PXR_INCLUDE_DIRS})
set(_vrm_imaging_test_targets)
foreach(_t IN ITEMS arch tf gf vt plug sdf usd hd usdImaging)
    if(TARGET ${_t})
        list(APPEND _vrm_imaging_test_targets ${_t})
    elseif(TARGET pxr::${_t})
        list(APPEND _vrm_imaging_test_targets pxr::${_t})
    endif()
endforeach()
target_link_libraries(vrmImaging_mtoon_tests PRIVATE ${_vrm_imaging_test_targets})
target_compile_options(vrmImaging_mtoon_tests PRIVATE ${_vrm_imaging_assertions})
if(MSVC)
    # OpenUSD's hd/dataSource.h uses the std::atomic_* shared_ptr overloads,
    # deprecated in C++20, which is what `ost`'s toolchain compiles.
    target_compile_definitions(vrmImaging_mtoon_tests PRIVATE NOMINMAX
        _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)
    target_compile_options(vrmImaging_mtoon_tests PRIVATE /utf-8)
endif()
# The plugin is loaded at run time, so the test has to be built after it.
add_dependencies(vrmImaging_mtoon_tests ${PLUGIN_NAME})

set(_vrm_imaging_fixture
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mtoon_materials.usda")
add_test(NAME vrmImaging_mtoon
    COMMAND vrmImaging_mtoon_tests "${_vrm_imaging_fixture}")

# The same executable, in a session that registers no vrmSchema: the plugin
# links nothing of that bundle and still contributes nothing without it,
# because UsdImaging asks an API-schema adapter only about schemas a prim's
# definition includes.
add_test(NAME vrmImaging_mtoon_without_schema
    COMMAND vrmImaging_mtoon_tests "${_vrm_imaging_fixture}" --without-schema)

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------
set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()

set(_bundle_dir "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(_plug_resources "${_bundle_dir}/plugin/resources/vrmImaging")
# The schema bundle, from its sibling directory, as execVrm's suites reach it.
# Its library goes on PATH although nothing here loads it, so a plugin that one
# day did would not fail for a reason this file hides.
set(_vrm_schema_bundle "${_bundle_dir}/../vrmSchema")
set(_vrm_schema_resources "${_vrm_schema_bundle}/plugin/resources/vrmSchema")

set(_vrm_imaging_path_env
    "PATH=path_list_prepend:${_usd_root}/bin"
    "PATH=path_list_prepend:${_usd_root}/lib"
    "PATH=path_list_prepend:${_bundle_dir}/lib"
    "PATH=path_list_prepend:${_vrm_schema_bundle}/lib")

set_tests_properties(vrmImaging_mtoon PROPERTIES
    ENVIRONMENT_MODIFICATION
        "${_vrm_imaging_path_env};PXR_PLUGINPATH_NAME=path_list_prepend:${_plug_resources};PXR_PLUGINPATH_NAME=path_list_prepend:${_vrm_schema_resources}")

# `set`, not a prepend: an activated ost session may already carry vrmSchema on
# PXR_PLUGINPATH_NAME, and the suite refuses to run under it.
set_tests_properties(vrmImaging_mtoon_without_schema PROPERTIES
    ENVIRONMENT_MODIFICATION
        "${_vrm_imaging_path_env};PXR_PLUGINPATH_NAME=set:${_plug_resources}")

set_tests_properties(vrmImaging_mtoon vrmImaging_mtoon_without_schema
    PROPERTIES LABELS "material.imaging")
