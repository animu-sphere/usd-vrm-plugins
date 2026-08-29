# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `vrmAdapterVmc` package, and nothing else.
#
# This is the second fixture and the first one with edges. `osc` came first
# because its required-package set is empty, so a failure there could only be
# its own config file (roadmap/packaging-hardening.md PKG-1); this package is
# the one the defect was actually in. OSC-3 put `osc::osc` on this adapter's
# installed interface link line and its config gained no `find_dependency(osc)`,
# so the package named a target no consumer could resolve while all 17 lanes
# were green (PACKAGE_CONTRACT.md §1). Nothing in this repository had ever made
# the call below, which is why nothing caught it.
#
# Five packages must resolve before this one links -- `pxr`, `motionCore`,
# `motionRuntime`, `liveTransport` and `osc` (PACKAGE_CONTRACT.md §4.3) -- and
# the consumer names none of them. That is the contract this fixture is for: a
# consumer writes one `find_package` and one target, and the config file is
# responsible for everything behind it. It is also what makes this the first
# fixture the `no-dependency` mutation can be run against, which `osc` refused
# by name because it had nothing to strip.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(vrmAdapterVmcConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE vrmAdapterVmc TARGET vrmAdapterVmc::vrmAdapterVmc)
