# SPDX-License-Identifier: Apache-2.0
#
# vrchat_osc_record — the VRChat OSC adapter's CLI.
#
# Added from the adapter's own CMakeLists rather than from the repository root,
# because the tool ships *with* the adapter: WORKSPACE.md §5 names one artifact
# carrying the library together with its CLI. That also means a standalone
# configure of `adapters/liveCapture/vrchatOsc` builds both, which is the
# configuration the artifact would be built from.

cmake_minimum_required(VERSION 3.22)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../VERSION")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../VERSION"
         _vrchat_osc_record_version LIMIT_COUNT 1)
    string(STRIP "${_vrchat_osc_record_version}" _vrchat_osc_record_version)
else()
    set(_vrchat_osc_record_version "0.7.0")
endif()

project(vrchatOscRecordTool
    VERSION ${_vrchat_osc_record_version}
    DESCRIPTION "VRChat OSC session recorder"
    LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED VRCHATOSCRECORD_BUILD_TESTS)
    if(DEFINED VRMADAPTERVRCHATOSC_BUILD_TESTS)
        set(_vrchat_osc_record_tests_default
            "${VRMADAPTERVRCHATOSC_BUILD_TESTS}")
    else()
        set(_vrchat_osc_record_tests_default "${PROJECT_IS_TOP_LEVEL}")
    endif()
    option(VRCHATOSCRECORD_BUILD_TESTS "Build vrchat_osc_record tests"
           ${_vrchat_osc_record_tests_default})
endif()

# A standalone configure of this directory resolves the adapter as an installed
# package; the composed build already has the in-tree target.
if(NOT TARGET vrmAdapterVrchatOsc::vrmAdapterVrchatOsc)
    find_package(vrmAdapterVrchatOsc REQUIRED CONFIG)
endif()

add_executable(vrchat_osc_record
    src/Options.cpp
    src/SessionReport.cpp
    src/main.cpp)

target_compile_features(vrchat_osc_record PRIVATE cxx_std_17)
# The adapter, and deliberately nothing else. §2 would permit this target to link
# `vrmRetarget` and OpenUSD's stage libraries the way `motion_retarget` does; it
# does not need them, and a link line is the cheapest place to keep that true.
target_link_libraries(vrchat_osc_record
    PRIVATE vrmAdapterVrchatOsc::vrmAdapterVrchatOsc)

if(MSVC)
    target_compile_options(vrchat_osc_record PRIVATE /utf-8)
    # The pair every target here sets. NOMINMAX is cheap insurance rather than a
    # present need in this tool -- nothing in its closure reaches OpenUSD today
    # -- and it costs one line against a windows.h macro breaking a
    # numeric_limits<>::max() the day something does.
    target_compile_definitions(vrchat_osc_record PRIVATE NOMINMAX _USE_MATH_DEFINES)
endif()

# Staged into the tool's own bin/, the way `motion_capture` and `motion_retarget`
# are: `ost plugin package` reads the directories openstrata.tool.yaml declares
# rather than a build tree, so a tool that only lands in build/ is refused at
# packaging with PRECONDITION_FAILED.
set_target_properties(vrchat_osc_record PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin")
foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(vrchat_osc_record PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_CURRENT_SOURCE_DIR}/bin")
endforeach()

include(GNUInstallDirs)
install(TARGETS vrchat_osc_record RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(VRCHATOSCRECORD_BUILD_TESTS)
    enable_testing()
    if(NOT Python3_Interpreter_FOUND)
        find_package(Python3 COMPONENTS Interpreter QUIET)
    endif()
    add_subdirectory(tests)
endif()
