# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `vrmRig` package, and nothing else.
#
# What a VRM rig adds to a retarget: the expression resolve, the look-at and
# VRM 1.0's required bones. Its one edge beyond OpenUSD is the value contract,
# `motionCore`, published by usd-motion-plugins and resolved from a prefix the
# consumer is handed like OpenUSD. The retarget itself is that repository's
# `motionRetarget` and is not an edge of this package: the VRM half includes
# nothing from the generic half (WORKSPACE.md §9.5).
#
# Criterion 4 is still a *build* rather than a compile: the fixture reaches a
# call whose archive member carries the edge to the value contract, so a config
# that stopped declaring it would fail at the link rather than pass.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(vrmRigConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE vrmRig TARGET vrmRig::vrmRig)
