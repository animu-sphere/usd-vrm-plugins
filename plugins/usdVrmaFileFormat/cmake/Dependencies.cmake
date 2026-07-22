# SPDX-License-Identifier: Apache-2.0
# cgltf is a header-only glTF parser used only inside the file-format binary.
# Keep the exact v1.15 source in-tree, rather than fetching it during CMake
# configuration. This keeps every OpenStrata lane hermetic and avoids a nested
# FetchContent build when CMake regenerates a bundle.
if(NOT CGLTF_SOURCE_DIR)
    set(CGLTF_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/cgltf"
        CACHE PATH "Directory containing the vendored cgltf.h" FORCE)
endif()

if(NOT EXISTS "${CGLTF_SOURCE_DIR}/cgltf.h")
    message(FATAL_ERROR "CGLTF_SOURCE_DIR='${CGLTF_SOURCE_DIR}' has no cgltf.h")
endif()
set(_cgltf_include "${CGLTF_SOURCE_DIR}")

if(NOT TARGET cgltf::cgltf)
    add_library(cgltf INTERFACE)
    add_library(cgltf::cgltf ALIAS cgltf)
    target_include_directories(cgltf INTERFACE "${_cgltf_include}")
endif()
