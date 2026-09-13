# SPDX-License-Identifier: Apache-2.0
#
# The OpenExec plan's P0-6: `motion_retarget` against `execMotion` + `execVrm`,
# on the same avatar and clip (docs/roadmap/openexec-foundation.md).
#
# Registered from the root rather than from either exec bundle or the tool,
# for the reason workspace_bvh_end_to_end is: what is tested is a composition --
# two bundles, a tool, the BVH converter that produces the recorded input, and
# usdVrmFileFormat for a real avatar -- and only the composed build guarantees
# all of them. The root CMakeLists.txt adds this directory after every tool, so
# each target named below either exists or the guard there skipped it.

# NDEBUG undefined, as in every suite here. The harness reports through its
# exit code rather than `assert()`, but a Release build silently dropping a
# check is the one failure a parity run must never have.
set(_parity_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(exec_parity exec_parity.cpp)
target_include_directories(exec_parity PRIVATE ${PXR_INCLUDE_DIRS})
# vrmRetarget for the result type, motionCore for the bone vocabulary and the
# contract's comparison. Neither exec bundle: they are found at run time, the
# way a packaged bundle is.
target_link_libraries(exec_parity PRIVATE
    vrmRetarget::vrmRetarget motionCore::motionCore)
set(_parity_pxr_targets)
foreach(_t IN ITEMS arch tf gf js vt plug sdf usd usdGeom usdSkel
                    vdf ef esf esfUsd exec execUsd)
    if(TARGET ${_t})
        list(APPEND _parity_pxr_targets ${_t})
    elseif(TARGET pxr::${_t})
        list(APPEND _parity_pxr_targets pxr::${_t})
    endif()
endforeach()
target_link_libraries(exec_parity PRIVATE ${_parity_pxr_targets})
# Not linked, and still what the harness runs: building it builds them.
add_dependencies(exec_parity ExecMotion ExecVrm)
target_compile_options(exec_parity PRIVATE ${_parity_assertions})
if(MSVC)
    target_compile_definitions(exec_parity PRIVATE NOMINMAX _USE_MATH_DEFINES)
    target_compile_options(exec_parity PRIVATE /bigobj /utf-8)
endif()

if(NOT USDVRM_TEST_PYTHON)
    message(STATUS "parity: no Python interpreter; the harness is built and "
                   "no parity case is registered")
    return()
endif()

# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------
set(_usd_root "")
if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
    set(_usd_root "${pxr_DIR}")
elseif(pxr_DIR)
    get_filename_component(_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
endif()

get_filename_component(_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(_plugins "${_repo_root}/plugins")

# Both exec bundles and the schema bundle from their source directories, the
# path their own suites take; usdVrmFileFormat through the plugInfo.json its
# tests configure with an absolute LibraryPath, the path the real-avatar bake
# takes. The environment reaches the tools as well as the harness, because the
# driver runs them as its subprocesses and a `.vrm` avatar has to open in all
# three.
set(_parity_env
    "PATH=path_list_prepend:${_usd_root}/bin"
    "PATH=path_list_prepend:${_usd_root}/lib"
    "PATH=path_list_prepend:${_plugins}/execMotion/lib"
    "PATH=path_list_prepend:${_plugins}/execVrm/lib"
    "PATH=path_list_prepend:${_plugins}/vrmSchema/lib"
    "PXR_PLUGINPATH_NAME=path_list_prepend:${_plugins}/execMotion/plugin/resources/execMotion"
    "PXR_PLUGINPATH_NAME=path_list_prepend:${_plugins}/execVrm/plugin/resources/execVrm"
    "PXR_PLUGINPATH_NAME=path_list_prepend:${_plugins}/vrmSchema/plugin/resources/vrmSchema")

set(_real_avatar
    "${_plugins}/usdVrmFileFormat/tests/corpus/spec-samples/vrm1/seed-san/Seed-san.vrm")
set(_have_real_avatar OFF)
if(TARGET UsdVrmFileFormat AND EXISTS "${_real_avatar}")
    set(_have_real_avatar ON)
    list(APPEND _parity_env
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmFileFormat>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer::vrmContainer>"
        "PXR_PLUGINPATH_NAME=path_list_prepend:${CMAKE_BINARY_DIR}/plugins/usdVrmFileFormat/tests/resources/usdVrmFileFormat")
endif()

# usdVrmaFileFormat the same way, for the one `.vrma` clip: the VRMA reader's own
# fixture of the design triplet's walk, which is the plan's "VRMA semantic
# clip" reaching exec through the reader rather than as hand-authored USD.
set(_vrma_walk "${_plugins}/usdVrmaFileFormat/tests/fixtures/canonical_walk.vrma")
set(_have_vrma OFF)
if(TARGET UsdVrmaFileFormat AND EXISTS "${_vrma_walk}")
    set(_have_vrma ON)
    list(APPEND _parity_env
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmaFileFormat>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer::vrmContainer>"
        "PXR_PLUGINPATH_NAME=path_list_prepend:${CMAKE_BINARY_DIR}/plugins/usdVrmaFileFormat/tests/resources/usdVrmaFileFormat")
endif()

set(_parity_driver
    "${USDVRM_TEST_PYTHON}" "${CMAKE_CURRENT_SOURCE_DIR}/test_exec_parity.py"
    --parity "$<TARGET_FILE:exec_parity>"
    --convert "$<TARGET_FILE:motion_bvh_convert>"
    --retarget "$<TARGET_FILE:motion_retarget>"
    --corpus "${_repo_root}/libs/motionBvh/tests/corpus"
    --profiles "${_repo_root}/profiles/motion"
    --fixtures "${_repo_root}/tests/motion/fixtures"
    --design-fixtures "${_repo_root}/docs/design/fixtures/motion"
    --design-map
        "${_repo_root}/tools/motionRetarget/tests/fixtures/design_avatar_humanoid_map.json"
    --parity-fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# One CTest name per case, so a red run names the input rather than "parity".
set(_parity_cases recorded_fixture recorded_root_motion generated_thirty_fps)
if(_have_real_avatar)
    list(APPEND _parity_cases recorded_real_avatar)
else()
    message(STATUS "parity: usdVrmFileFormat or Seed-san.vrm is not in this "
                   "build tree; skipping the real-avatar parity case")
endif()
if(_have_vrma)
    list(APPEND _parity_cases vrma_walk)
else()
    message(STATUS "parity: usdVrmaFileFormat or its walk fixture is not in "
                   "this build tree; skipping the VRMA parity case")
endif()

foreach(_case IN LISTS _parity_cases)
    add_test(NAME workspace_exec_parity_${_case}
        COMMAND ${_parity_driver} --case ${_case}
                --real-avatar "${_real_avatar}"
                --vrma-walk "${_vrma_walk}")
    # The plan's P0-2 labels: every case is OpenExec, and the three over the
    # recorded mocopi export are the real corpus as well.
    set(_labels "motion.openexec")
    if(_case MATCHES "^recorded_")
        list(APPEND _labels "motion.real-corpus")
    endif()
    set_tests_properties(workspace_exec_parity_${_case} PROPERTIES
        ENVIRONMENT_MODIFICATION "${_parity_env}"
        LABELS "${_labels}")
endforeach()
