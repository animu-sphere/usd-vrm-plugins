# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `motionBvh` package, and nothing else.
#
# The one row in the table whose required-package cell is not enough to install
# it. PACKAGE_CONTRACT.md §3 rule 3 forbids a config from reaching past its own
# declared edges, so this package names the layer directly beneath it and
# nothing further -- and installing that cell literally leaves *that* package's
# own configure with nothing to resolve. The driver walks the rows depth-first
# for exactly this row (roadmap/packaging-hardening.md PKG-1); every other
# package in the table happens to be listed in an order where the difference
# does not show.
#
# So this fixture is the one that measures a transitive closure rather than a
# declared edge: three workspace packages and one external one have to resolve
# before its target links, and the consumer names none of them.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(motionBvhConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE motionBvh TARGET motionBvh::motionBvh)
