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

# motionCore has only Gf value-type dependencies. Add it after pxr so the
# composed plain-CMake build never asks pxrConfig.cmake to define its imported
# targets a second time; standalone builds still resolve pxr themselves.
# motionRuntime and vrmRetarget stack on top of it in dependency order
# (WORKSPACE.md §2): motionRuntime -> motionCore, vrmRetarget -> both.
add_subdirectory("libs/motionCore")
add_subdirectory("libs/motionRuntime")
add_subdirectory("libs/vrmRetarget")

# The recorded-file half's middle layer: motionSource -> motionCore and nothing
# else (WORKSPACE.md §2). It is added after motionCore for the same reason
# motionRuntime is, and it sits beside motionRuntime rather than above it -- live
# input and recorded files meet at motionCore and nowhere earlier
# (motion policy §8.3).
add_subdirectory("libs/motionSource")

# The recorded-file half's reader, above the layer it hands its extraction to.
# It used to be added before the OpenUSD resolution above, and that position was
# a statement rather than a convenience: the syntax layer reads three numbers off
# an OFFSET line and has no value type to borrow from Gf. It moved here when the
# extractor took the declared motionBvh -> motionSource edge (WORKSPACE.md §2),
# which is the arrow this line's position now says out loud. It still names no
# OpenUSD of its own; what it needs configured first is motionSource.
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
            "PATH=path_list_prepend:${_e2e_usd_root}/bin;PATH=path_list_prepend:${_e2e_usd_root}/lib;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/site-packages")

    # The same chain onto a real VRM, which is the release condition's own
    # wording (roadmap/current.md) and not a second opinion about the tools. It
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
                "PATH=path_list_prepend:${_e2e_usd_root}/bin;PATH=path_list_prepend:${_e2e_usd_root}/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:UsdVrmFileFormat>;PATH=path_list_prepend:${_vrm_schema_bundle}/lib;PATH=path_list_prepend:$<TARGET_FILE_DIR:vrmContainer::vrmContainer>;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/python;PYTHONPATH=path_list_prepend:${_e2e_usd_root}/lib/site-packages;PXR_PLUGINPATH_NAME=path_list_prepend:${_vrm_plug_resources};PXR_PLUGINPATH_NAME=path_list_prepend:${_vrm_schema_bundle}/plugin/resources/vrmSchema")
    else()
        message(STATUS
            "usd-vrm-plugins: usdVrmFileFormat or the VRM spec sample is not in "
            "this build tree; skipping the real-avatar bake")
    endif()
endif()

# The live half's shared leaf, before the adapters that link it and after
# everything else — which is the whole of its position argument. It reaches no
# workspace library at all (WORKSPACE.md §2 gives it an empty edge set), so it
# could sit anywhere above the adapters; it sits here, immediately above them,
# because the only targets that may name it are the ones added next. Nothing in
# the aggregate product links a transport, and a reader scanning this file for
# where that stops being true finds the boundary in one place.
#
# No OpenUSD, not even Gf: it holds a socket, a text file format, and a
# diagnostic line, and names no value type.
add_subdirectory("libs/liveTransport")

# Input adapters (WORKSPACE.md §1, §2). Each adapters/<group>/<name>/ carrying
# both an openstrata.library.yaml and a CMakeLists.txt is added here — plain
# libraries, not plugin bundles: an adapter registers nothing with OpenUSD.
#
# Added LAST, and that is the point. Every adapter is a leaf of the core, of the
# OpenExec bundles, and of every other adapter, so nothing configured above may
# name an adapter target; at this position CMake would fail on the unknown
# target rather than quietly accept the inverted edge. CONFIGURE_DEPENDS
# re-globs when an adapter is added or removed.
file(GLOB _adapters LIST_DIRECTORIES true CONFIGURE_DEPENDS RELATIVE
     "${CMAKE_CURRENT_SOURCE_DIR}/adapters"
     "${CMAKE_CURRENT_SOURCE_DIR}/adapters/*/*")
foreach(_adapter ${_adapters})
    set(_adapterDir "${CMAKE_CURRENT_SOURCE_DIR}/adapters/${_adapter}")
    if(IS_DIRECTORY "${_adapterDir}"
       AND EXISTS "${_adapterDir}/openstrata.library.yaml"
       AND EXISTS "${_adapterDir}/CMakeLists.txt")
        message(STATUS "OpenStrata: adding input adapter 'adapters/${_adapter}'")
        add_subdirectory("adapters/${_adapter}")
    endif()
endforeach()

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
