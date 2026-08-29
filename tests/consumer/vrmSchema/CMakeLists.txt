# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `vrmSchema` package, and nothing else.
#
# The one plugin bundle in the `find_package` group. The three file-format and
# resolver bundles beside it export no target and install no config by design --
# nothing links them, OpenUSD discovers them through `plugInfo.json` -- and
# their consumer contract is plugin load, gated elsewhere
# (PACKAGE_CONTRACT.md §4.1). This one ships a CMake package because a bundle in
# this workspace links it, which is also what already made its `Standalone` cell
# read *measured*: the standalone bundle build in CI is that consumer.
#
# What that consumer is not is a project outside this workspace. It is a member
# of it, built from its tree, and criterion 5 is the property it cannot have.
# This fixture is the other sense of the word, and the shape it measures is the
# one PKG-2 left open: a bundle's install is not a plain library's, so this is
# the row where a `cmake --install` prefix and an extracted `ost` package could
# still diverge.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(vrmSchemaConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE vrmSchema TARGET vrmSchema::vrmSchema)
