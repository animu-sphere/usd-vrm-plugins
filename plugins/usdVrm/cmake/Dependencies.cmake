# SPDX-License-Identifier: Apache-2.0
#
# Third-party dependency resolution for the usdVrm plugin.
#
# cgltf is the GLB/glTF container parser. It is header-only (single file) and is
# NOT vendored into the repository: it is fetched at configure time, pinned to a
# known tag, mirroring how the sibling usd-luma-plugins resolves nlohmann/json.
#
# Override points (for offline / air-gapped builds):
#   -DCGLTF_SOURCE_DIR=<dir containing cgltf.h>   use a local copy, skip fetch
#   -DFETCHCONTENT_SOURCE_DIR_CGLTF=<dir>          CMake's standard FC override

include(FetchContent)

set(CGLTF_GIT_TAG "v1.15" CACHE STRING "cgltf tag/commit to fetch")
set(CGLTF_SOURCE_DIR "" CACHE PATH "Local cgltf.h directory (skips FetchContent)")

if(CGLTF_SOURCE_DIR)
    if(NOT EXISTS "${CGLTF_SOURCE_DIR}/cgltf.h")
        message(FATAL_ERROR "CGLTF_SOURCE_DIR='${CGLTF_SOURCE_DIR}' has no cgltf.h")
    endif()
    set(_cgltf_include "${CGLTF_SOURCE_DIR}")
    message(STATUS "usdVrm: using local cgltf at ${_cgltf_include}")
else()
    message(STATUS "usdVrm: fetching cgltf ${CGLTF_GIT_TAG} via FetchContent")
    FetchContent_Declare(cgltf
        GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
        GIT_TAG ${CGLTF_GIT_TAG}
        GIT_SHALLOW TRUE
        # cgltf ships its own CMakeLists (test exes). Point SOURCE_SUBDIR at a
        # path that doesn't exist so MakeAvailable populates but does NOT
        # add_subdirectory() it — we only want the header.
        SOURCE_SUBDIR do-not-configure)
    FetchContent_MakeAvailable(cgltf)
    set(_cgltf_include "${cgltf_SOURCE_DIR}")
endif()

# Expose cgltf as an INTERFACE target so consumers just link it.
if(NOT TARGET cgltf::cgltf)
    add_library(cgltf INTERFACE)
    add_library(cgltf::cgltf ALIAS cgltf)
    target_include_directories(cgltf INTERFACE "${_cgltf_include}")
endif()
