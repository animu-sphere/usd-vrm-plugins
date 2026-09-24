# SPDX-License-Identifier: Apache-2.0
#
# UsdVrmConsumedPackage.cmake — how a member resolves a package this repository
# consumes rather than builds.
#
# `motionCore`, `motionSampling`, `motionRecording`, `motionRetarget` and
# `motionUsd` are usd-motion-plugins' installed packages. This repository never
# reaches them as a source tree -- no add_subdirectory(), no FetchContent -- only
# as a prefix on CMAKE_PREFIX_PATH: `ost` materializes the digest-pinned artifact
# and puts it there, and a plain-CMake build names a `cmake --install` of that
# repository the same way it names an OpenUSD install
# (docs/reference/SUPPORTED_CONFIGURATIONS.md, "Plain CMake").
#
# The rule is that **the member that links a package resolves it**. The root
# project does not resolve any of them on a member's behalf: a root that finds
# the whole motion stack up front makes every member's configure require every
# package, which is how `motionRecording` stayed REQUIRED for a workspace in
# which nothing included one of its headers.
#
#   usdvrm_consume_package(<package> [<package>...])
#
# For each <package> whose `<package>::<package>` target is not visible yet, runs
# `find_package(<package> REQUIRED CONFIG)` and promotes every imported target
# that call created to IMPORTED_GLOBAL. So in the composed root build the first
# member to link a package resolves it and every later member -- and a
# workspace-wide test registered from the root -- sees that one definition,
# while a standalone configure of any single member resolves exactly what that
# member names and nothing else.
#
# A function, not a macro, on purpose: a package's config runs
# find_dependency(pxr) when OpenUSD is not resolved yet, and pxrConfig.cmake
# re-finds Python3 with only the Development components, which leaves
# `Python3_Interpreter_FOUND` FALSE in whatever scope it ran in (see the root
# CMakeLists.txt, USDVRM_TEST_PYTHON). Imported targets are directory-scoped, so
# they survive the function; the variables it would clobber do not escape it.
#
# scripts/check_cmake_boundaries.py reads the calls to this function as the
# member's package edges and holds them to docs/architecture/WORKSPACE.md §2.
include_guard(GLOBAL)

function(usdvrm_consume_package)
    foreach(_usdvrm_pkg IN LISTS ARGN)
        if(TARGET ${_usdvrm_pkg}::${_usdvrm_pkg})
            continue()
        endif()

        get_directory_property(_usdvrm_before IMPORTED_TARGETS)
        find_package(${_usdvrm_pkg} REQUIRED CONFIG)
        if(NOT TARGET ${_usdvrm_pkg}::${_usdvrm_pkg})
            message(FATAL_ERROR
                "${_usdvrm_pkg}Config.cmake (${${_usdvrm_pkg}_DIR}) was found but "
                "defines no ${_usdvrm_pkg}::${_usdvrm_pkg} target.")
        endif()

        get_directory_property(_usdvrm_created IMPORTED_TARGETS)
        if(_usdvrm_before)
            list(REMOVE_ITEM _usdvrm_created ${_usdvrm_before})
        endif()
        foreach(_usdvrm_target IN LISTS _usdvrm_created)
            get_target_property(_usdvrm_global ${_usdvrm_target} IMPORTED_GLOBAL)
            if(NOT _usdvrm_global)
                set_target_properties(${_usdvrm_target} PROPERTIES
                    IMPORTED_GLOBAL TRUE)
            endif()
        endforeach()
    endforeach()
endfunction()
