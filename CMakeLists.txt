# SPDX-License-Identifier: Apache-2.0
#
# Top-level project for usd-vrm-plugins.
#
# This repo is dual-mode by design:
#   * OpenStrata-native: `ost build` / `ost test` drive this file, and
#     `ost plugin build|test plugins/usdVrmFileFormat` drives one bundle's
#     CMakeLists.txt directly against the activated runtime's toolchain.
#   * Plain CMake: anyone without OpenStrata can configure THIS file with an
#     OpenUSD 26.08 install and a `cmake --install` of usd-motion-plugins on
#     CMAKE_PREFIX_PATH, and build and test every member. Use the
#     CMakePresets.json here, or:
#       cmake -S . -B build "-DCMAKE_PREFIX_PATH=<usd-install>;<motion-install>"
#       cmake --build build --config Release
#       ctest --test-dir build -C Release
#     .github/workflows/plain-cmake.yml runs exactly that, with no `ost`.
#
# This file orchestrates and nothing else: the project and its version, the
# OpenUSD contract, the member list, and the tests that span members. A
# dependency belongs to the member that links it (cmake/UsdVrmConsumedPackage.cmake).
#
# Each member under libs/, plugins/ and tools/ is a self-contained CMake project
# (so `ost plugin build` / `ost library build` can target it standalone) that is
# also add_subdirectory()-able from here.
cmake_minimum_required(VERSION 3.22)

# Single source of truth for the release version: the repo-root VERSION file.
# Every other version string (CHANGELOG, release tag, ost bundle manifest) mirrors
# it; nothing hand-duplicates the number here. See docs/SUPPORTED_CONFIGURATIONS.md.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _vrm_project_version LIMIT_COUNT 1)
string(STRIP "${_vrm_project_version}" _vrm_project_version)

project(UsdVrmPlugins
    VERSION ${_vrm_project_version}
    DESCRIPTION "OpenUSD plugins for VRM avatars"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators (Ninja) default to an empty build type. The OpenUSD
# installs we build against are Release-only, so default to Release to avoid
# pulling debug-only imported deps (e.g. tbb12_debug.lib).
get_property(_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _isMultiConfig AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(VRM_IS_TOP_LEVEL ON)
else()
    set(VRM_IS_TOP_LEVEL OFF)
endif()

option(USDVRM_BUILD_TESTS "Build usdVrm plugin tests" ${VRM_IS_TOP_LEVEL})

# Enable CTest at the top level so tests registered in bundle subdirectories are
# discoverable by `ctest` from this build tree.
if(USDVRM_BUILD_TESTS)
    enable_testing()

    # The OpenUSD contract's refusals (cmake/UsdVrmOpenUsd.cmake), against
    # fixture installs. Registered here rather than in a bundle because the
    # module is a workspace-wide contract, and because every real runtime
    # satisfies it -- nothing else in the tree ever takes the reject path.
    add_test(NAME workspace_openusd_contract
        COMMAND "${CMAKE_COMMAND}"
                "-DFIXTURE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/openusd-contract"
                "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/tests/openusd-contract"
                "-DPROBE_GENERATOR=${CMAKE_GENERATOR}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/openusd_contract_test.cmake")

    # The producer-profile checks (`workspace_motion_profiles` and its two
    # invented-rig runs) left with the profiles in MIG-3; they are
    # `usd-motion-plugins`' now, beside the data they check.
    find_package(Python3 COMPONENTS Interpreter QUIET)

    # Kept under a name of our own, because `Python3_Interpreter_FOUND` does not
    # survive this file. `find_package(pxr)` below runs pxrConfig.cmake in *this*
    # scope, and it re-finds Python3 asking only for the Development components
    # -- which leaves `Python3_Interpreter_FOUND` FALSE from that line onward
    # even though the interpreter above was found and is still perfectly good.
    # A later test guarded on that variable does not fail; it silently never
    # registers, which is the worst of the available outcomes and is exactly
    # what happened to workspace_bvh_end_to_end before this line existed.
    if(Python3_Interpreter_FOUND)
        set(USDVRM_TEST_PYTHON "${Python3_EXECUTABLE}")
    endif()
endif()

# vrmContainer has no OpenUSD dependency, so it is added before the single
# workspace-wide OpenUSD resolution below.
add_subdirectory("libs/vrmContainer")

# Resolve OpenUSD once for the whole project; bundles reuse the result.
find_package(pxr REQUIRED CONFIG)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UsdVrmOpenUsd.cmake")

# The generic motion libraries are consumed, not built (MIG-1..MIG-3), and this
# file resolves none of them. `motionCore`, `motionRetarget`, `motionSampling`
# and `motionUsd` are usd-motion-plugins' installed packages: `ost`
# materializes each digest-pinned artifact and puts its prefix on
# CMAKE_PREFIX_PATH, and a plain-CMake build names a `cmake --install` of that
# repository there, the same way it names OpenUSD. Never a sibling source tree.
#
# Each member resolves what it links, through usdvrm_consume_package()
# (cmake/UsdVrmConsumedPackage.cmake), so this build requires exactly the union
# of what its members use -- and a standalone configure of one member requires
# only that member's own. OpenUSD is resolved above first, so no package config
# is ever the one that defines OpenUSD's targets.
#
# Set for the members' workspace-wide tests, which need bundles a standalone
# member configure does not have. A variable rather than a target probe: a
# probe answers "which package happens to be visible here", which is not the
# question and stopped being the same answer when this file stopped resolving
# packages for its members.
set(USDVRM_COMPOSED_BUILD ON)

# What is still built here from this layer: what a VRM rig adds to the
# consumed retarget (WORKSPACE.md §2, §9.5).
add_subdirectory("libs/vrmRig")

# The recorded-file half left with MIG-3: `motionSource`, `motionBvh`, the BVH
# tools and the producer profiles are `usd-motion-plugins`' now, and nothing
# here reads a BVH file. What a converted recording does to a VRM rig is still
# this repository's claim, over the converter's committed output
# (tests/motion/fixtures/README.md).

# Discover every plugin bundle under plugins/ rather than naming them one by one:
# an immediate plugins/<name>/ that carries both an openstrata.plugin.yaml (the
# bundle contract) and a CMakeLists.txt (so `ost plugin build` can target it
# standalone) is add_subdirectory()'d here. CONFIGURE_DEPENDS re-globs on the next
# build when a bundle is added/removed, so a second `ost plugin new plugins/<name>`
# is picked up without editing this file.
#
# Order: schema bundles first — they sit at the bottom of the workspace
# dependency graph (WORKSPACE.md §2: nothing may depend *from* a schema bundle,
# consumers link it via its in-tree alias target in this composed build), then
# everything else alphabetically.
file(GLOB _bundles LIST_DIRECTORIES true CONFIGURE_DEPENDS RELATIVE
     "${CMAKE_CURRENT_SOURCE_DIR}/plugins" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*")
set(_schema_bundles)
set(_other_bundles)
foreach(_bundle ${_bundles})
    set(_bundleDir "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${_bundle}")
    if(IS_DIRECTORY "${_bundleDir}"
       AND EXISTS "${_bundleDir}/openstrata.plugin.yaml"
       AND EXISTS "${_bundleDir}/CMakeLists.txt")
        file(READ "${_bundleDir}/openstrata.plugin.yaml" _manifest)
        if(_manifest MATCHES "kind:[ \t]*usd-schema")
            list(APPEND _schema_bundles "${_bundle}")
        else()
            list(APPEND _other_bundles "${_bundle}")
        endif()
    endif()
endforeach()
foreach(_bundle ${_schema_bundles} ${_other_bundles})
    message(STATUS "OpenStrata: adding plugin bundle 'plugins/${_bundle}'")
    add_subdirectory("plugins/${_bundle}")
endforeach()

# The motion layer's CLI tools. They are executables, not bundles: they have no
# plugInfo.json and register nothing with OpenUSD -- they open stages the
# plugins above already know how to read. Added last so their end-to-end tests
# can rely on those bundles existing in the same build tree.
#
# The capture-replay CLI left with MIG-4: `tools/motionCapture` is
# `usd-motion-plugins`' `motion_record` now. What it authored still reaches an
# avatar here, as a committed clip that motionRetarget's suite bakes.
add_subdirectory("tools/motionRetarget")    # Motion Phase C — offline retarget

# BVH-3's evaluation point: a recorded file onto a target rig, through
# `motion_convert` and then an **unchanged** `motion_retarget`. The converter is
# `usd-motion-plugins`' since MIG-3, so the first arrow is its committed output
# (tests/motion/fixtures/README.md) and this build runs the second.
#
# Registered here rather than in the tool's own tests because it needs the
# fixture rig and, for the real avatar below, a plugin bundle in the same
# build. It is the one test that would catch the recorded path needing a special
# case in the retargeter — which is the claim that makes stopping at the
# avatar-independent clip worth anything (roadmap/recorded-motion-sources.md §5,
# §7).
if(USDVRM_BUILD_TESTS AND USDVRM_TEST_PYTHON
   AND TARGET motion_retarget
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/fixtures")
    set(_e2e_usd_root "")
    if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
        set(_e2e_usd_root "${pxr_DIR}")
    elseif(pxr_DIR)
        get_filename_component(_e2e_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
    endif()
    add_test(NAME workspace_bvh_end_to_end
        COMMAND "${USDVRM_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/test_bvh_end_to_end.py"
            --retarget "$<TARGET_FILE:motion_retarget>"
            --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/fixtures")
    set_tests_properties(workspace_bvh_end_to_end PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_e2e_usd_root}/bin;PATH=path_list_prepend:${_e2e_usd_root}/lib;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/site-packages"
        # The OpenExec plan's P0-2 labels: a composition of two tools, over a
        # producer's recorded export, converted.
        LABELS "motion.integration;motion.real-corpus")

    # The same chain onto a real VRM, which is v0.7.0's release condition's own
    # wording (roadmap/adapters-mocopi-vmc-ardy.md, Milestone C) and not a
    # second opinion about the tools. It
    # is registered separately rather than folded into the test above because the
    # two rigs answer different questions and neither substitutes: the fixture is
    # shaped so a broken rest-pose correction cannot pass it, and a released
    # avatar is shaped by whoever made it -- 128 joints where 77 carry hair and
    # luggage, two unbound joints between bound ones, and an optional human bone
    # it simply does not have.
    #
    # This is the one test in the tree that needs a plugin bundle AND the motion
    # tools in the same build, which is why it is here: `motion_retarget` opens a
    # `.vrm` only if usdVrmFileFormat is registered, and only the composed root
    # build guarantees both halves exist. Everything the plugin needs to load is
    # mirrored from plugins/usdVrmFileFormat/tests/CMakeLists.txt, which is the
    # same environment its own CI-proven smoke tests run in -- the importer
    # applies the typed Vrm*API schemas, so the sibling vrmSchema bundle's
    # resources are on the registry path and its library on the loader path.
    set(_real_avatar
        "${CMAKE_CURRENT_SOURCE_DIR}/plugins/usdVrmFileFormat/tests/corpus/spec-samples/vrm1/seed-san/Seed-san.vrm")
    if(TARGET UsdVrmFileFormat AND EXISTS "${_real_avatar}")
        # Configured by the bundle's own tests, which this file has already
        # added: an absolute LibraryPath, so the plugin loads with no assumption
        # about the working directory or the ost flow having run.
        set(_vrm_plug_resources
            "${CMAKE_CURRENT_BINARY_DIR}/plugins/usdVrmFileFormat/tests/resources/usdVrmFileFormat")
        set(_vrm_schema_bundle "${CMAKE_CURRENT_SOURCE_DIR}/plugins/vrmSchema")
        add_test(NAME workspace_real_avatar_bake
            COMMAND "${USDVRM_TEST_PYTHON}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/test_real_avatar.py"
                --retarget "$<TARGET_FILE:motion_retarget>"
                --avatar "${_real_avatar}"
                --clip "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/fixtures/mocopi-mobile-arm-raise-turn.usda")
        set_tests_properties(workspace_real_avatar_bake PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:${_e2e_usd_root}/bin;PATH=path_list_prepend:${_e2e_usd_root}/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmFileFormat>;PATH=path_list_prepend:${_vrm_schema_bundle}/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer::vrmContainer>;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/site-packages;PXR_PLUGINPATH_NAME=path_list_prepend:${_vrm_plug_resources};PXR_PLUGINPATH_NAME=path_list_prepend:${_vrm_schema_bundle}/plugin/resources/vrmSchema"
            LABELS "motion.integration;motion.real-corpus")
    else()
        message(STATUS
            "usd-vrm-plugins: usdVrmFileFormat or the VRM spec sample is not in "
            "this build tree; skipping the real-avatar bake")
    endif()
endif()

# The reference pipeline: a `.vrma`, a converted BVH export and a recorded
# session through one downstream invocation onto one VRM, asserted to be the
# same code rather than three plausible outputs (MIG-5's cross-repository test,
# boundary consolidation BND-1). Two of the three first arrows are
# usd-motion-plugins' tools, read as their committed output; the third is
# usdVrmaFileFormat, so this needs both importers and the tool in one build.
#
# `_local` is the same test over an avatar and `.vrma` files that cannot be
# committed, named by USDVRM_LOCAL_AVATAR and USDVRM_LOCAL_VRMA_DIR in the
# environment of the run; without them it reports Skipped.
if(USDVRM_BUILD_TESTS AND USDVRM_TEST_PYTHON
   AND TARGET motion_retarget
   AND TARGET UsdVrmFileFormat AND TARGET UsdVrmaFileFormat
   AND EXISTS "${_real_avatar}")
    set(_reference_plugins "${CMAKE_CURRENT_SOURCE_DIR}/plugins")
    set(_reference_resources "${CMAKE_CURRENT_BINARY_DIR}/plugins")
    set(_reference_common
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/test_reference_pipeline.py"
        --retarget "$<TARGET_FILE:motion_retarget>"
        --bvh-clip "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/fixtures/mocopi-mobile-arm-raise-turn.usda"
        --trace-clip "${CMAKE_CURRENT_SOURCE_DIR}/tools/motionRetarget/tests/fixtures/recorded_session_clip.usda")
    add_test(NAME workspace_reference_pipeline
        COMMAND "${USDVRM_TEST_PYTHON}" ${_reference_common}
            --avatar "${_real_avatar}"
            --vrma "${_reference_plugins}/usdVrmaFileFormat/tests/fixtures/canonical_walk.vrma")
    add_test(NAME workspace_reference_pipeline_local
        COMMAND "${USDVRM_TEST_PYTHON}" ${_reference_common} --local)
    set_tests_properties(workspace_reference_pipeline workspace_reference_pipeline_local
        PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_e2e_usd_root}/bin;PATH=path_list_prepend:${_e2e_usd_root}/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmFileFormat>;PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmaFileFormat>;PATH=path_list_prepend:${_reference_plugins}/vrmSchema/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer::vrmContainer>;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/site-packages;PXR_PLUGINPATH_NAME=path_list_prepend:${_reference_resources}/usdVrmFileFormat/tests/resources/usdVrmFileFormat;PXR_PLUGINPATH_NAME=path_list_prepend:${_reference_resources}/usdVrmaFileFormat/tests/resources/usdVrmaFileFormat;PXR_PLUGINPATH_NAME=path_list_prepend:${_reference_plugins}/vrmSchema/plugin/resources/vrmSchema"
        LABELS "motion.integration;motion.real-corpus")
    set_tests_properties(workspace_reference_pipeline_local PROPERTIES
        SKIP_RETURN_CODE 77)
else()
    message(STATUS
        "usd-vrm-plugins: a tool or a file-format bundle is not in this build "
        "tree; skipping workspace_reference_pipeline")
endif()

# The OpenExec plan's P0-6: a `motion_retarget` bake against `execMotion` +
# `execVrm` evaluating the same avatar and clip. Here for the same reason as the
# two tests above -- it is a composition of both exec bundles and the retarget
# tool over the converted recording -- and after them, because it needs every
# one of those targets. `execMotion` is not a target any more: it is
# usd-motion-plugins' published bundle, consumed by execVrm's descriptor and
# materialized for this build by `ost` (cmake/UsdVrmExecMotion.cmake). These
# are the rows MIG-2 re-ran against it before this repository's copy went.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UsdVrmExecMotion.cmake")
if(USDVRM_BUILD_TESTS AND USDVRM_EXEC_MOTION_BUNDLE AND TARGET ExecVrm
   AND TARGET motion_retarget)
    add_subdirectory("tests/parity")
elseif(USDVRM_BUILD_TESTS)
    message(STATUS
        "usd-vrm-plugins: no consumed execMotion or no execVrm in this build; "
        "skipping the OpenExec parity cases. A plain-CMake build names an "
        "extracted execMotion bundle with -DUSDVRM_EXEC_MOTION_ROOT=<dir>; "
        "without one, workspace_ctest_labels reports the labels these cases "
        "carry as missing")
endif()

# The live half left with MIG-4. `liveTransport`, `osc`, `motionTracking` and
# the three input adapters are `motion-connectors`' now, as
# `motionConnectorTransport`, `motionConnectorOsc`, `motionConnectorTracking`
# and the three `motionConnector*` connectors
# ([WORKSPACE.md §9.1](docs/architecture/WORKSPACE.md), rule 7: they left
# together). Nothing here links a transport, a wire format or a tracker
# assignment any more, and the `adapters/` tree and its glob are gone with
# them — a directory this file scanned for members it no longer has.

# Every executable and file format the workspace ships, handed paths no ANSI
# code page can spell and held to the same run from an ASCII directory. It is
# the OpenExec plan's P0-2 "Windows Unicode paths" row and Product P3's Unicode
# item.
#
# **One executable, where there were seven.** The three adapter recorders left
# with MIG-4, and with them the three recorded captures this test opened from a
# non-ASCII directory; `motion_capture` followed, as `usd-motion-plugins`'
# `motion_record`, whose own suite now makes the claim (usd-motion-plugins #23);
# and the two BVH tools left with MIG-3, their claims made by
# `motion_convert_clip` and `motion_bvh_inspect_report` there
# (usd-motion-plugins #25). What that costs is stated rather than absorbed: the
# claim "every executable this workspace ships opens a non-ASCII path" is now
# over `motion_retarget` alone, which is every executable it ships, and the
# rest of it is made where the executables went.
if(USDVRM_BUILD_TESTS AND USDVRM_TEST_PYTHON
   AND TARGET motion_retarget
   AND TARGET UsdVrmFileFormat AND TARGET UsdVrmaFileFormat
   AND TARGET UsdVrmPackageResolver AND EXISTS "${_real_avatar}")
    set(_unicode_plugins "${CMAKE_CURRENT_SOURCE_DIR}/plugins")
    set(_unicode_resources "${CMAKE_CURRENT_BINARY_DIR}/plugins")
    get_filename_component(_unicode_python_dir "${USDVRM_TEST_PYTHON}"
                           DIRECTORY)
    add_test(NAME workspace_unicode_paths
        COMMAND "${USDVRM_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/paths/test_unicode_paths.py"
            --retarget "$<TARGET_FILE:motion_retarget>"
            --avatar "${_real_avatar}"
            --vrma "${_unicode_plugins}/usdVrmaFileFormat/tests/fixtures/canonical_walk.vrma"
            --clip "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/fixtures/mocopi-mobile-arm-raise-turn.usda")
    # The three file-format bundles as their own suites load them, and the
    # Python runtime the remaining tools load through Gf.
    set_tests_properties(workspace_unicode_paths PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_e2e_usd_root}/bin;PATH=path_list_prepend:${_e2e_usd_root}/lib;PATH=path_list_prepend:${_unicode_python_dir};PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmFileFormat>;PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmaFileFormat>;PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmPackageResolver>;PATH=path_list_prepend:${_unicode_plugins}/vrmSchema/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer::vrmContainer>;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/site-packages;PXR_PLUGINPATH_NAME=path_list_prepend:${_unicode_resources}/usdVrmFileFormat/tests/resources/usdVrmFileFormat;PXR_PLUGINPATH_NAME=path_list_prepend:${_unicode_resources}/usdVrmaFileFormat/tests/resources/usdVrmaFileFormat;PXR_PLUGINPATH_NAME=path_list_prepend:${_unicode_resources}/usdVrmPackageResolver/tests/resources/usdVrmPackageResolver;PXR_PLUGINPATH_NAME=path_list_prepend:${_unicode_plugins}/vrmSchema/plugin/resources/vrmSchema"
        # A composition of every product tool and both importers, over the
        # recorded mocopi export, converted.
        LABELS "motion.integration;motion.real-corpus")
else()
    message(STATUS
        "usd-vrm-plugins: a tool or a file-format bundle "
        "is not in this build tree; skipping workspace_unicode_paths")
endif()

# The OpenExec plan's P0-2 labels, read back from CTest itself. `ost test`
# reports one flat total, so a member whose suite silently stopped registering
# reads as passing (usdVrmaFileFormat did, for months); this fails instead,
# naming the label and the directory that contributed nothing. Registered last
# and from here because it reads the whole tree's registrations, and it runs
# CTest in listing mode only, so it executes nothing it lists.
if(USDVRM_BUILD_TESTS AND USDVRM_TEST_PYTHON)
    add_test(NAME workspace_ctest_labels
        COMMAND "${USDVRM_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_ctest_labels.py"
            --ctest "${CMAKE_CTEST_COMMAND}"
            --build-dir "${CMAKE_CURRENT_BINARY_DIR}"
            --source-dir "${CMAKE_CURRENT_SOURCE_DIR}"
            --config "$<CONFIG>")
    # The check's rules against cases, so a rule that can no longer fail is
    # found on every lane rather than by the next silent suite.
    add_test(NAME workspace_ctest_labels_selftest
        COMMAND "${USDVRM_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_ctest_labels.py"
            --selftest)

    # The CMake dependency boundary (WORKSPACE.md §2) as the build states it:
    # what each member resolves, links and includes, against what it may; the
    # root resolving nothing on a member's behalf; no source tree of another
    # repository. And the consumed packages this build actually resolved,
    # read for any code that knows VRM -- whichever prefix they came from, the
    # artifact `ost` pulled or a plain `cmake --install`.
    set(_boundary_includes)
    foreach(_consumed IN ITEMS motionCore motionRetarget motionSampling motionUsd)
        if(TARGET ${_consumed}::${_consumed})
            list(APPEND _boundary_includes --motion-include
                 "$<TARGET_PROPERTY:${_consumed}::${_consumed},INTERFACE_INCLUDE_DIRECTORIES>")
        endif()
    endforeach()
    add_test(NAME workspace_cmake_boundaries
        COMMAND "${USDVRM_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_cmake_boundaries.py"
            ${_boundary_includes})
    add_test(NAME workspace_cmake_boundaries_selftest
        COMMAND "${USDVRM_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_cmake_boundaries.py"
            --selftest)
endif()

install(FILES
    LICENSE
    README.md
    DESTINATION "share/doc/usd-vrm-plugins"
    OPTIONAL)
