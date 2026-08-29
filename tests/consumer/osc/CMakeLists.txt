# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `osc` package, and nothing else.
#
# `osc` is first because its edge set is empty (WORKSPACE.md §2): no workspace
# dependency, no platform library, no `find_dependency` in its config. So a
# failure here can only be this package's own config file, which is what makes
# it the right place to find out whether the fixture and the driver work at all
# before either is trusted with a package that has edges
# (roadmap/packaging-hardening.md PKG-1).
#
# Two lines below carry the whole contract, and both are easy to lose:
#
#   * `find_package(osc CONFIG REQUIRED)` -- the call no lane in this repository
#     had ever made before this fixture existed, because a composed workspace
#     build and `ost library build` both define `osc::osc` as an alias in the
#     same CMake project and never open a config file (PACKAGE_CONTRACT.md §1);
#   * `osc::osc` -- the exported name, not the in-tree alias. They are kept
#     identical on purpose (PACKAGE_CONTRACT.md §2) so this line is the same one
#     a workspace member writes; that is a promise, and this is where it is
#     tested rather than assumed.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it, so the
# accident PACKAGE_CONTRACT.md §5 criterion 5 warns about -- a fixture that
# quietly resolves against the build tree it was run from -- cannot happen by
# forgetting to check for it.
cmake_minimum_required(VERSION 3.24)

project(oscConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE osc TARGET osc::osc)
