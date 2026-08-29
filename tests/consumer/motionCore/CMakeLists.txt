# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `motionCore` package, and nothing else.
#
# The first fixture whose package's only required package is one this workspace
# does not produce. `pxr` arrives through the driver's `--extra-prefix`, the way
# OpenUSD arrives for anyone else, and the scratch prefix holds this package
# alone -- so the question this asks that the four before it could not is
# whether a config can resolve an edge it did not install
# (PACKAGE_CONTRACT.md §4.2).
#
# That edge is also the one whose spelling is not `<name>::<name>`: `pxr` brings
# `arch`, `gf` and `tf`, and there is no `pxr::pxr` anywhere. A closure walk that
# expected the workspace's own naming promise to hold for an external package
# would have found nothing to check here.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(motionCoreConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE motionCore TARGET motionCore::motionCore)
