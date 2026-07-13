# SPDX-License-Identifier: Apache-2.0
#
# Installed-package consumer smoke: proves a third party (or a sibling bundle
# such as usdVrm) can resolve the *installed* vrmSchema package and link the
# typed API — the WORKSPACE.md §4 standalone-consumption contract. Configure
# with CMAKE_PREFIX_PATH pointing at a vrmSchema install prefix.
cmake_minimum_required(VERSION 3.22)
project(vrmSchemaInstalledConsumer LANGUAGES CXX)

find_package(vrmSchema REQUIRED CONFIG)

add_executable(vrmSchema-consumer main.cpp)
target_link_libraries(vrmSchema-consumer PRIVATE vrmSchema::vrmSchema)
target_compile_features(vrmSchema-consumer PRIVATE cxx_std_17)
if(MSVC)
    # pxr headers do not survive windows.h min/max macros (robin_hash.h C2238).
    target_compile_definitions(vrmSchema-consumer PRIVATE NOMINMAX)
    target_compile_options(vrmSchema-consumer PRIVATE /utf-8)
endif()
