# SPDX-License-Identifier: Apache-2.0
#
# Top-level project for usd-vrm-plugins.
#
# This repo is dual-mode by design:
#   * OpenStrata-native: `ost plugin build|test plugins/usdVrmFileFormat` drives the bundle
#     CMakeLists.txt directly against the activated runtime's toolchain.
#   * Plain CMake: anyone without OpenStrata can configure THIS file, point
#     CMAKE_PREFIX_PATH at an OpenUSD install, and build every bundle. Use the
#     CMakePresets.json here, or:
#       cmake -S . -B build -DCMAKE_PREFIX_PATH=<usd-install>
#       cmake --build build --config Release
#
# Each plugin under plugins/<name> is a self-contained CMake project (so `ost
# plugin build` can target it standalone) that is also add_subdirectory()-able
# from here.
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

    # Each shipped producer profile against the recorded file it claims to
    # describe. Registered here rather than in either library because it is the
    # one check that holds a reader's file and a profile at once, which neither
    # of them may: motionSource is forbidden to know a reader exists, motionBvh
    # to know a producer does (WORKSPACE.md §2). A caller is what may hold both.
    # `motion_bvh_convert` is now such a caller and this script stays anyway:
    # the tool checks that a profile and a file *convert*, and this checks that
    # a shipped profile still describes the file it claims to, which is a
    # question about the data and is answerable with no build at all.
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/profiles/motion")
        add_test(NAME workspace_motion_profiles
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_motion_profiles.py")
        # Held against a producer's recorded export, which the OpenExec plan's
        # P0-2 label names. The two runs below read an invented rig instead.
        set_property(TEST workspace_motion_profiles
            APPEND PROPERTY LABELS "motion.real-corpus")

        # The same script over an invented rig with no bytes anywhere. A
        # recording this repository may not redistribute leaves a manifest row
        # and its measured hierarchy, and the profile that names it is held
        # against that instead of against a file -- a path the real corpus does
        # not exercise while every row it can check has its bytes committed.
        #
        # The pair is the test. Passing alone would be satisfied by a check that
        # had quietly stopped looking, so the second run is a profile that gets
        # exactly one thing wrong -- a bone mapped outside the chain it belongs
        # to, every name and count still agreeing -- and it must fail. That is
        # the near-miss the check exists for, arriving through the path with no
        # file behind it.
        set(_motion_profile_fixture
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/fixtures/profile-check")
        if(EXISTS "${_motion_profile_fixture}")
            add_test(NAME workspace_motion_profiles_absent
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_motion_profiles.py"
                    --profiles "${_motion_profile_fixture}/profiles"
                    --corpus "${_motion_profile_fixture}/corpus")
            add_test(NAME workspace_motion_profiles_absent_misplaced
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_motion_profiles.py"
                    --profiles "${_motion_profile_fixture}/profiles-misplaced"
                    --corpus "${_motion_profile_fixture}/corpus")
            set_tests_properties(workspace_motion_profiles_absent_misplaced
                PROPERTIES WILL_FAIL TRUE)
        endif()
    endif()

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

# The generic motion libraries are consumed, not built (MIG-1..MIG-3).
# `motionCore`, `motionSampling`, `motionRecording`, `motionSource` and
# `motionBvh` are published `usd-motion-plugins` artifacts, pinned by digest per
# target in each descriptor that names one; `ost` materializes them and puts
# their prefixes on CMAKE_PREFIX_PATH before this file is configured, so every
# `find_package` below resolves against a package rather than a sibling
# directory. A plain-CMake build resolves them from CMAKE_PREFIX_PATH the same
# way it resolves OpenUSD.
#
# Resolved once here, after pxr, so the composed build never asks any of their
# configs to define OpenUSD's imported targets a second time. Each member still
# guards its own `find_package` with `if(NOT TARGET ...)`, which is what keeps a
# standalone configure of that member working.
foreach(_consumed IN ITEMS motionCore motionSampling motionRecording)
    if(NOT TARGET ${_consumed}::${_consumed})
        find_package(${_consumed} REQUIRED CONFIG)
    endif()
endforeach()

# What is still built here from this layer: the VRM retarget, which stacks on
# the consumed core (WORKSPACE.md §2), and the recorded-file pair.
add_subdirectory("libs/vrmRetarget")

# The recorded-file half stays for now, and the reason is a toolchain fact
# rather than a design one: `ost` 0.23.2 materializes an external library
# artifact declared by a library or a plugin descriptor and not one declared
# only by a tool, and `motion_bvh` is the only consumer these two have left
# here. They leave with that tool (MIG-3's consuming half), not before.
#
# motionSource -> motionCore and nothing else (WORKSPACE.md §2); motionBvh ->
# motionSource, which is the edge that decides this order. Neither names
# OpenUSD of its own.
add_subdirectory("libs/motionSource")
add_subdirectory("libs/motionBvh")

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

# The motion layer's CLI tools. Both are executables, not bundles: they have no
# plugInfo.json and register nothing with OpenUSD -- they open stages the
# plugins above already know how to read. Added last so their end-to-end tests
# can rely on those bundles existing in the same build tree.
#
# Order matters for the tests, not the build. The Phase D end-to-end test drives
# motion_capture and then motion_retarget, so motionRetarget is added FIRST and
# motionCapture's test wiring picks it up with `if(TARGET motion_retarget)`.
# A standalone `ost plugin build tools/motionCapture` has no such target, and
# referencing one that does not exist fails at generate time rather than at run
# time -- so the guard, not a bare $<TARGET_FILE:>, is what keeps both
# configurations green.
add_subdirectory("tools/motionRetarget")    # Motion Phase C — offline retarget
add_subdirectory("tools/motionCapture")     # Motion Phase D — live capture

# The BVH path's CLIs. `motion_bvh_inspect` is the exception to the paragraph
# above: it opens no stage and links no OpenUSD, because the syntax layer under
# it has no value type to borrow from Gf. It could have been added beside
# libs/motionBvh for that reason, and is not — a tool is a tool, and
# `motion_bvh_convert` sits in the same directory with a stage in its hands.
add_subdirectory("tools/motionBvh")         # BVH-1/3 — syntax, and conversion

# BVH-3's evaluation point: a recorded file onto a target rig, through
# `motion_bvh_convert` and then an **unchanged** `motion_retarget`.
#
# Registered here rather than in either tool's own tests for the reason the
# profile check above is: it is the composition that is being tested, and it
# needs two executables that only the composed build guarantees. It is also the
# one test that would catch the recorded path needing a special case in the
# retargeter — which is the claim that makes stopping at the avatar-independent
# clip worth anything (roadmap/recorded-motion-sources.md §5, §7).
if(USDVRM_BUILD_TESTS AND USDVRM_TEST_PYTHON
   AND TARGET motion_bvh_convert AND TARGET motion_retarget
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/fixtures"
   AND EXISTS
       "${CMAKE_CURRENT_SOURCE_DIR}/libs/motionBvh/tests/corpus/recorded/redistributable")
    set(_e2e_usd_root "")
    if(pxr_DIR AND EXISTS "${pxr_DIR}/bin")
        set(_e2e_usd_root "${pxr_DIR}")
    elseif(pxr_DIR)
        get_filename_component(_e2e_usd_root "${pxr_DIR}/../../.." ABSOLUTE)
    endif()
    add_test(NAME workspace_bvh_end_to_end
        COMMAND "${USDVRM_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/test_bvh_end_to_end.py"
            --convert "$<TARGET_FILE:motion_bvh_convert>"
            --retarget "$<TARGET_FILE:motion_retarget>"
            --corpus "${CMAKE_CURRENT_SOURCE_DIR}/libs/motionBvh/tests/corpus"
            --profiles "${CMAKE_CURRENT_SOURCE_DIR}/profiles/motion"
            --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/fixtures")
    set_tests_properties(workspace_bvh_end_to_end PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_e2e_usd_root}/bin;PATH=path_list_prepend:${_e2e_usd_root}/lib;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/site-packages"
        # The OpenExec plan's P0-2 labels: a composition of two tools, over a
        # producer's recorded export.
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
                --convert "$<TARGET_FILE:motion_bvh_convert>"
                --retarget "$<TARGET_FILE:motion_retarget>"
                --avatar "${_real_avatar}"
                --corpus "${CMAKE_CURRENT_SOURCE_DIR}/libs/motionBvh/tests/corpus"
                --profiles "${CMAKE_CURRENT_SOURCE_DIR}/profiles/motion")
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

# The OpenExec plan's P0-6: a `motion_retarget` bake against `execMotion` +
# `execVrm` evaluating the same avatar and clip. Here for the same reason as the
# two tests above -- it is a composition of both exec bundles, the retarget
# tool, and the BVH converter that turns the recorded file into the clip -- and
# after them, because it needs every one of those targets.
if(USDVRM_BUILD_TESTS AND TARGET ExecMotion AND TARGET ExecVrm
   AND TARGET motion_retarget AND TARGET motion_bvh_convert)
    add_subdirectory("tests/parity")
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
# **Four executables, where there were seven.** The three adapter recorders
# left with MIG-4, and with them the three recorded captures this test opened
# from a non-ASCII directory. What that costs is stated rather than absorbed:
# the claim "every executable this workspace ships opens a non-ASCII path" is
# now over four, and the recorders' half of it is `motion-connectors`' to make.
if(USDVRM_BUILD_TESTS AND USDVRM_TEST_PYTHON
   AND TARGET motion_bvh_inspect AND TARGET motion_bvh_convert
   AND TARGET motion_retarget AND TARGET motion_capture
   AND TARGET UsdVrmFileFormat AND TARGET UsdVrmaFileFormat
   AND TARGET UsdVrmPackageResolver AND EXISTS "${_real_avatar}")
    set(_unicode_plugins "${CMAKE_CURRENT_SOURCE_DIR}/plugins")
    set(_unicode_resources "${CMAKE_CURRENT_BINARY_DIR}/plugins")
    get_filename_component(_unicode_python_dir "${USDVRM_TEST_PYTHON}"
                           DIRECTORY)
    add_test(NAME workspace_unicode_paths
        COMMAND "${USDVRM_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/paths/test_unicode_paths.py"
            --bvh-inspect "$<TARGET_FILE:motion_bvh_inspect>"
            --bvh-convert "$<TARGET_FILE:motion_bvh_convert>"
            --retarget "$<TARGET_FILE:motion_retarget>"
            --capture "$<TARGET_FILE:motion_capture>"
            --avatar "${_real_avatar}"
            --vrma "${_unicode_plugins}/usdVrmaFileFormat/tests/fixtures/canonical_walk.vrma"
            --bvh-corpus "${CMAKE_CURRENT_SOURCE_DIR}/libs/motionBvh/tests/corpus"
            --profiles "${CMAKE_CURRENT_SOURCE_DIR}/profiles/motion"
            --trace "${CMAKE_CURRENT_SOURCE_DIR}/tests/motion/fixtures/traces/walk-clean-30hz.trace")
    # The three file-format bundles as their own suites load them, and the
    # Python runtime the remaining tools load through Gf.
    set_tests_properties(workspace_unicode_paths PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${_e2e_usd_root}/bin;PATH=path_list_prepend:${_e2e_usd_root}/lib;PATH=path_list_prepend:${_unicode_python_dir};PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmFileFormat>;PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmaFileFormat>;PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmPackageResolver>;PATH=path_list_prepend:${_unicode_plugins}/vrmSchema/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer::vrmContainer>;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/site-packages;PXR_PLUGINPATH_NAME=path_list_prepend:${_unicode_resources}/usdVrmFileFormat/tests/resources/usdVrmFileFormat;PXR_PLUGINPATH_NAME=path_list_prepend:${_unicode_resources}/usdVrmaFileFormat/tests/resources/usdVrmaFileFormat;PXR_PLUGINPATH_NAME=path_list_prepend:${_unicode_resources}/usdVrmPackageResolver/tests/resources/usdVrmPackageResolver;PXR_PLUGINPATH_NAME=path_list_prepend:${_unicode_plugins}/vrmSchema/plugin/resources/vrmSchema"
        # A composition of every product tool and both importers, over the
        # recorded mocopi export.
        LABELS "motion.integration;motion.real-corpus")
else()
    message(STATUS
        "usd-vrm-plugins: a tool or a file-format bundle "
        "is not in this build tree; skipping workspace_unicode_paths")
endif()

# The motion migration's MIG-0 vocabulary check: every VRM name the headers
# leaving for usd-motion-plugins still spell, against the ledger that says how
# each is disposed of on arrival (WORKSPACE.md §9.3, §9.5). Source only, so it
# needs no target of this tree. The three near-miss ledgers each get one thing
# wrong, and each must fail with its own message -- a pass regex rather than
# WILL_FAIL, so a crash or a missing file cannot stand in for the right refusal.
if(USDVRM_BUILD_TESTS AND USDVRM_TEST_PYTHON)
    set(_vocabulary_fixture
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/boundary/fixtures/motion-vocabulary")
    add_test(NAME workspace_motion_vocabulary
        COMMAND "${USDVRM_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_motion_vocabulary.py")
    add_test(NAME workspace_motion_vocabulary_fixture
        COMMAND "${USDVRM_TEST_PYTHON}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_motion_vocabulary.py"
            --root "${_vocabulary_fixture}"
            --ledger "${_vocabulary_fixture}/complete.json")
    foreach(_case IN ITEMS unclassified stale anchor)
        add_test(NAME workspace_motion_vocabulary_${_case}
            COMMAND "${USDVRM_TEST_PYTHON}"
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_motion_vocabulary.py"
                --root "${_vocabulary_fixture}"
                --ledger "${_vocabulary_fixture}/${_case}.json")
    endforeach()
    set_tests_properties(workspace_motion_vocabulary_unclassified PROPERTIES
        PASS_REGULAR_EXPRESSION "unclassified: identifier `VrmBindingHint` in include/Pose.h")
    set_tests_properties(workspace_motion_vocabulary_stale PROPERTIES
        PASS_REGULAR_EXPRESSION "stale: ledger lists identifier `HumanoidAnimation`")
    set_tests_properties(workspace_motion_vocabulary_anchor PROPERTIES
        PASS_REGULAR_EXPRESSION "missing anchor: finding 1 is about `FindMissingRequiredBones`")
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
endif()

install(FILES
    LICENSE
    README.md
    DESTINATION "share/doc/usd-vrm-plugins"
    OPTIONAL)

# The producer profiles, at the destination WORKSPACE.md §5 names. This is the
# plain-CMake half of "profiles are data that has to reach an artifact" and only
# that half: `ost` 0.21.0 has no notion of a data-only member.
#
# That half is now **measured rather than unverified** (2026-08-05, with
# `motion_bvh_convert`): `ost plugin package --workspace --product` produces a
# 7-member product whose `motion_bvh` archive carries `bin/motion_bvh_inspect.exe`,
# `bin/motion_bvh_convert.exe` and `openstrata.tool.yaml` — and no profile at
# all, because `directories:` is what a tool member packages and a data
# directory outside the member has no way in. So the converter in a packaged
# product finds no profile on its executable-relative search path and refuses
# every file it is given, which is exactly the outcome WORKSPACE.md §5 put the
# profiles beside the tools to prevent. It blocks BVH-3's artifact-only smoke
# and is a v0.7.0 carry-over; a plain `cmake --install` is unaffected and is
# what this rule serves.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/profiles/motion/"
    DESTINATION "share/usd-vrm-plugins/profiles/motion"
    FILES_MATCHING PATTERN "*.yaml")
