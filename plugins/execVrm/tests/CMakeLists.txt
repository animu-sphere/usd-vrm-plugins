# SPDX-License-Identifier: Apache-2.0

# NDEBUG is undefined for test targets specifically, as in every suite in this
# workspace: the checks are `assert()`, and a Release suite would otherwise
# print "passed" having checked nothing.
set(_exec_vrm_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

# ---------------------------------------------------------------------------
# The seam, with no stage and no exec
# ---------------------------------------------------------------------------
# ExecVrmRig.cpp is compiled into the test rather than linked out of the
# plugin, for execMotion's reason: nothing outside the bundle calls it, and a
# bundle that exports its internals is one other things start depending on.
add_executable(execVrm_rig_tests
    test_exec_vrm_rig.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/ExecVrmRig.cpp")
target_include_directories(execVrm_rig_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    ${PXR_INCLUDE_DIRS})
target_link_libraries(execVrm_rig_tests PRIVATE vrmRetarget::vrmRetarget)
# TfToken, for the attribute names the seam builds.
foreach(_t IN ITEMS tf gf)
    if(TARGET ${_t})
        target_link_libraries(execVrm_rig_tests PRIVATE ${_t})
    elseif(TARGET pxr::${_t})
        target_link_libraries(execVrm_rig_tests PRIVATE pxr::${_t})
    endif()
endforeach()
target_compile_options(execVrm_rig_tests PRIVATE ${_exec_vrm_assertions})
if(MSVC)
    target_compile_definitions(execVrm_rig_tests
        PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_compile_options(execVrm_rig_tests PRIVATE /utf-8)
endif()
add_test(NAME execVrm_rig COMMAND execVrm_rig_tests)

# ---------------------------------------------------------------------------
# The humanoid, through the built bundle
# ---------------------------------------------------------------------------
# Links OpenUSD and vrmRetarget -- the second for the two result types, read
# through their accessors -- but NOT the plugin: discovery goes through
# PXR_PLUGINPATH_NAME and the staged plugInfo.json, the path a packaged bundle
# takes.
add_executable(execVrm_humanoid_tests test_exec_vrm_humanoid.cpp)
target_include_directories(execVrm_humanoid_tests PRIVATE ${PXR_INCLUDE_DIRS})
target_link_libraries(execVrm_humanoid_tests PRIVATE vrmRetarget::vrmRetarget)
target_compile_options(execVrm_humanoid_tests PRIVATE ${_exec_vrm_assertions})
if(MSVC)
    target_compile_definitions(execVrm_humanoid_tests
        PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_compile_options(execVrm_humanoid_tests PRIVATE /bigobj /utf-8)
endif()

set(_exec_vrm_test_targets)
foreach(_t IN ITEMS arch tf gf vt plug sdf usd usdSkel
                    vdf ef esf esfUsd exec execUsd)
    if(TARGET ${_t})
        list(APPEND _exec_vrm_test_targets ${_t})
    elseif(TARGET pxr::${_t})
        list(APPEND _exec_vrm_test_targets pxr::${_t})
    endif()
endforeach()
target_link_libraries(execVrm_humanoid_tests
    PRIVATE ${_exec_vrm_test_targets})

set(_exec_vrm_fixture "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/humanoid_rig.usda")
add_test(NAME execVrm_humanoid
    COMMAND execVrm_humanoid_tests "${_exec_vrm_fixture}")

# The same executable, in a session that registers no vrmSchema. The bundle
# links nothing of that bundle and still cannot compute on the humanoid
# without it, because exec resolves `UsdVrmHumanoidAPI` by type name when it
# reads this bundle's Exec block -- so this is the one requirement
# `requires.bundles` states, measured by leaving it unmet.
add_test(NAME execVrm_humanoid_without_schema
    COMMAND execVrm_humanoid_tests "${_exec_vrm_fixture}" --without-schema)

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
set(_plug_resources "${_bundle_dir}/plugin/resources/execVrm")
# The schema bundle this one requires, from its sibling directory -- the same
# path usdVrmFileFormat's suites take to it. Its library goes on PATH as well,
# although nothing here loads it, so a plugin that one day did would not fail
# for a reason this file hides.
set(_vrm_schema_bundle "${_bundle_dir}/../vrmSchema")
set(_vrm_schema_resources "${_vrm_schema_bundle}/plugin/resources/vrmSchema")

set(_exec_vrm_path_env
    "PATH=path_list_prepend:${_usd_root}/bin"
    "PATH=path_list_prepend:${_usd_root}/lib"
    "PATH=path_list_prepend:${_bundle_dir}/lib")

set_tests_properties(execVrm_humanoid PROPERTIES
    ENVIRONMENT_MODIFICATION
        "${_exec_vrm_path_env};PATH=path_list_prepend:${_vrm_schema_bundle}/lib;PXR_PLUGINPATH_NAME=path_list_prepend:${_plug_resources};PXR_PLUGINPATH_NAME=path_list_prepend:${_vrm_schema_resources}")

# `set`, not `path_list_prepend`: an activated ost session may already carry
# vrmSchema on PXR_PLUGINPATH_NAME, and a prepend would leave it there -- which
# the suite itself refuses to run under, since it would measure nothing.
set_tests_properties(execVrm_humanoid_without_schema PROPERTIES
    ENVIRONMENT_MODIFICATION
        "${_exec_vrm_path_env};PXR_PLUGINPATH_NAME=set:${_plug_resources}")

set_tests_properties(execVrm_rig PROPERTIES
    ENVIRONMENT_MODIFICATION "${_exec_vrm_path_env}")

# Motion Phase E's label, as execMotion's suites carry.
set_tests_properties(execVrm_rig execVrm_humanoid
                     execVrm_humanoid_without_schema
    PROPERTIES LABELS "motion.openexec")
