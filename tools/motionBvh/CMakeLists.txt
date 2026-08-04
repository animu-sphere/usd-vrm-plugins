# SPDX-License-Identifier: Apache-2.0
#
# The BVH path's CLIs. `motion_bvh_inspect` is here; `motion_bvh_convert` joins
# it in this same directory (WORKSPACE.md §1), which is why the project is named
# after the layer rather than after the first executable in it.
#
# This tool names no OpenUSD: it prints what a file says, and the values it
# prints have no basis, no unit and no axis for a Gf type to imply. What it can
# no longer claim is that OpenUSD is absent from its *configure* and its
# artifact. `find_package(motionBvh)` now resolves motionSource and, through it,
# pxr -- so a standalone configure of this directory needs OpenUSD on the prefix
# path even though nothing here references it, and on macOS the linker records
# those dylibs on the executable as well. Both are the cost of the declared
# motionBvh -> motionSource edge and are stated in the README rather than
# discovered by whoever follows its build recipe.
#
# `motion_bvh_convert` will link OpenUSD's stage libraries when it lands,
# because authoring a clip is what it is for -- the two tools sharing a
# directory does not make them share a link line.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../VERSION"
         _motion_bvh_tools_version LIMIT_COUNT 1)
    string(STRIP "${_motion_bvh_tools_version}" _motion_bvh_tools_version)
else()
    set(_motion_bvh_tools_version "0.6.0")
endif()

project(motionBvhTools
    VERSION ${_motion_bvh_tools_version}
    DESCRIPTION "BVH inspection CLI"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED MOTIONBVHTOOLS_BUILD_TESTS)
    if(DEFINED USDVRM_BUILD_TESTS)
        set(_motion_bvh_tools_tests_default "${USDVRM_BUILD_TESTS}")
    else()
        set(_motion_bvh_tools_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(MOTIONBVHTOOLS_BUILD_TESTS "Build motion_bvh_inspect tests"
           ${_motion_bvh_tools_tests_default})
endif()

# A standalone configure of this directory resolves the library as an installed
# package; the composed build already has the in-tree target.
if(NOT TARGET motionBvh::motionBvh)
    find_package(motionBvh REQUIRED CONFIG)
endif()

add_executable(motion_bvh_inspect
    src/Options.cpp
    src/Report.cpp
    src/main.cpp)

target_compile_features(motion_bvh_inspect PRIVATE cxx_std_17)
target_link_libraries(motion_bvh_inspect PRIVATE motionBvh::motionBvh)

if(MSVC)
    # The headers cite roadmap sections by their `§` sign, and MSVC reads source
    # in the *system* code page unless told otherwise -- so a Japanese-Windows
    # build warns C4819 on every one of them without this.
    target_compile_options(motion_bvh_inspect PRIVATE /utf-8)
endif()
# No NOMINMAX / _USE_MATH_DEFINES pair here, unlike every other tool in this
# workspace: those exist for windows.h meeting OpenUSD's numeric_limits<> uses,
# which arrive through motionCore. motionCore is now on this link line
# transitively, but no header this tool includes reaches those uses -- it
# includes the syntax half only, and BvhExtract.h is where motionSource starts.

# Staged into the member root's bin/, the way `motion_capture` and `vmc_record`
# are: `ost plugin package` reads the directories openstrata.tool.yaml declares
# rather than a build tree, so a tool that only lands in build/ is refused at
# packaging with PRECONDITION_FAILED (report 33).
set_target_properties(motion_bvh_inspect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(motion_bvh_inspect PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

include(GNUInstallDirs)
install(TARGETS motion_bvh_inspect
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(MOTIONBVHTOOLS_BUILD_TESTS)
    enable_testing()
    if(NOT Python3_Interpreter_FOUND)
        find_package(Python3 COMPONENTS Interpreter QUIET)
    endif()
    add_subdirectory(tests)
endif()
