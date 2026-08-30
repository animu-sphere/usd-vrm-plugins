# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `motionTracking` package, and nothing else.
#
# Its edge set is empty (WORKSPACE.md §2) — no workspace dependency, no platform
# library, no `find_dependency` in its config — so this fixture is `osc`'s twin
# in shape, and a failure here can only be this package's own config file.
#
# The one thing worth saying that the sibling's comment does not: this package
# is on the **product** side of §5's split and no product member links it yet,
# so this fixture is currently the only thing in the repository that configures
# against `motionTrackingConfig.cmake` at all. That makes the criterion 5 rule
# below load-bearing rather than routine — a fixture that named a source path
# would be the only reader of this config and would be reading the wrong one.
cmake_minimum_required(VERSION 3.24)

project(motionTrackingConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE motionTracking TARGET motionTracking::motionTracking)
