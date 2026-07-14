# SPDX-License-Identifier: Apache-2.0
#
# Third-party dependency resolution for the usdVrmPackageResolver plugin.
#
# cgltf parses the glTF JSON so the resolver can enumerate the image table
# (buffer-view index + MIME type) of a .vrm container. It is header-only
# (single file) and is NOT vendored into the repository: it is fetched at
# configure time, pinned to a known tag. Byte access itself goes through
# vrmContainer's checked views, never through unchecked cgltf pointers.
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
    message(STATUS "usdVrmPackageResolver: using local cgltf at ${_cgltf_include}")
else()
    message(STATUS "usdVrmPackageResolver: fetching cgltf ${CGLTF_GIT_TAG} via FetchContent")
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
