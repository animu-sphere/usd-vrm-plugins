# SPDX-License-Identifier: Apache-2.0
#
# The installed vrmSchema package for C++ consumers. `ost plugin package` stages a
# bundle's libraries and USD resources but no headers, so the vrmSchema bundle
# cannot be built against. This member packages the bundle's whole install tree.
# It adds no source: plugins/vrmSchema remains the owner.
cmake_minimum_required(VERSION 3.22)
project(vrmSchemaSdk LANGUAGES CXX)
set(USDVRM_BUILD_TESTS OFF CACHE BOOL "The bundle's own lanes run its tests")
add_subdirectory(../../plugins/vrmSchema vrmSchema)
