# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `vrmAdapterVrchatOsc` package, and nothing else.
#
# The second package the §1 defect was in. OSC-3 gave this adapter and its
# sibling a `PUBLIC osc::osc` and neither installed config gained a
# `find_dependency(osc)`; the fix landed in both, and the sibling's half was
# measured on 2026-08-29 (PACKAGE_CONTRACT.md §4.3). This is the other half,
# and it is the shorter one on purpose: this adapter requires only the two
# leaves and none of the motion layer, because it ships no semantic decoder
# yet. That makes it the package where the closure is small enough to read at a
# glance, and the one whose row a reviewer should expect to grow when the
# decoder arrives.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(vrmAdapterVrchatOscConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE vrmAdapterVrchatOsc TARGET vrmAdapterVrchatOsc::vrmAdapterVrchatOsc)
