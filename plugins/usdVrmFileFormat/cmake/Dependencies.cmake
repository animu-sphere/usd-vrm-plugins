# SPDX-License-Identifier: Apache-2.0
#
# Third-party dependency resolution for the usdVrmFileFormat plugin.
#
# cgltf is the header-only GLB/glTF parser. The exact v1.15 source is vendored
# for air-gapped, reproducible configuration; both file-format bundles consume
# this same copy.
if(NOT CGLTF_SOURCE_DIR)
    set(CGLTF_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cgltf"
        CACHE PATH "Directory containing the vendored cgltf.h" FORCE)
endif()
if(NOT EXISTS "${CGLTF_SOURCE_DIR}/cgltf.h")
    message(FATAL_ERROR "CGLTF_SOURCE_DIR='${CGLTF_SOURCE_DIR}' has no cgltf.h")
endif()
set(_cgltf_include "${CGLTF_SOURCE_DIR}")
message(STATUS "usdVrmFileFormat: using vendored cgltf at ${_cgltf_include}")

# Expose cgltf as an INTERFACE target so consumers just link it.
if(NOT TARGET cgltf::cgltf)
    add_library(cgltf INTERFACE)
    add_library(cgltf::cgltf ALIAS cgltf)
    target_include_directories(cgltf INTERFACE "${_cgltf_include}")
endif()
