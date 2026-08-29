# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `motionSource` package, and nothing else.
#
# The recorded-file half of the motion layer, and the same two-edge shape as the
# runtime beside it: one workspace package installed into the scratch prefix,
# one external package that arrives however the host supplies it
# (PACKAGE_CONTRACT.md §4.2). What makes it worth its own fixture rather than a
# copy of that one is the package *above* it -- `motionBvh` declares this and
# nothing under it, so this row is the one whose config the next fixture's whole
# closure depends on.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(motionSourceConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE motionSource TARGET motionSource::motionSource)
