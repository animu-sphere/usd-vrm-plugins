# SPDX-License-Identifier: Apache-2.0
#
# vmc_record — the VMC adapter's CLI.
#
# Added from the adapter's own CMakeLists rather than from the repository root,
# because the tool ships *with* the adapter: WORKSPACE.md §5 names one artifact
# carrying the library together with its CLI. That also means a standalone
# configure of `adapters/liveCapture/vmc` builds both, which is the
# configuration the artifact would be built from.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../VERSION"
         _vmc_record_version LIMIT_COUNT 1)
    string(STRIP "${_vmc_record_version}" _vmc_record_version)
else()
    set(_vmc_record_version "0.5.0")
endif()

project(vmcRecordTool
    VERSION ${_vmc_record_version}
    DESCRIPTION "VMC session recorder"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED VMCRECORD_BUILD_TESTS)
    if(DEFINED VRMADAPTERVMC_BUILD_TESTS)
        set(_vmc_record_tests_default "${VRMADAPTERVMC_BUILD_TESTS}")
    else()
        set(_vmc_record_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(VMCRECORD_BUILD_TESTS "Build vmc_record tests"
           ${_vmc_record_tests_default})
endif()

# A standalone configure of this directory resolves the adapter as an installed
# package; the composed build already has the in-tree target.
if(NOT TARGET vrmAdapterVmc::vrmAdapterVmc)
    find_package(vrmAdapterVmc REQUIRED CONFIG)
endif()

add_executable(vmc_record
    src/Options.cpp
    src/SessionReport.cpp
    src/main.cpp)

target_compile_features(vmc_record PRIVATE cxx_std_17)
# The adapter, and deliberately nothing else. §2 would permit this target to
# link `vrmRetarget` and OpenUSD's stage libraries the way `motion_retarget`
# does; it does not need them, and a link line is the cheapest place to keep
# that true.
target_link_libraries(vmc_record PRIVATE vrmAdapterVmc::vrmAdapterVmc)

if(MSVC)
    target_compile_options(vmc_record PRIVATE /utf-8)
    # The pair every target here sets: without NOMINMAX, windows.h's min/max
    # macros break OpenUSD's own numeric_limits<>::max() uses, which arrive
    # through motionCore's Gf value types.
    target_compile_definitions(vmc_record PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

# Staged into the tool's own bin/, the way `motion_capture` and
# `motion_retarget` are: `ost plugin package` reads the directories
# openstrata.tool.yaml declares rather than a build tree, so a tool that only
# lands in build/ is refused at packaging with PRECONDITION_FAILED.
set_target_properties(vmc_record PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(vmc_record PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

include(GNUInstallDirs)
install(TARGETS vmc_record RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(VMCRECORD_BUILD_TESTS)
    enable_testing()
    if(NOT Python3_Interpreter_FOUND)
        find_package(Python3 COMPONENTS Interpreter QUIET)
    endif()
    add_subdirectory(tests)
endif()
