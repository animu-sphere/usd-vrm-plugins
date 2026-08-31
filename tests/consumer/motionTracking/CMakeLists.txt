# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `motionTracking` package, and nothing else.
#
# Its edge set is one package (WORKSPACE.md §2): the solve links `motionCore`,
# which brings OpenUSD's Gf value types with it, and there is no platform
# library and no second workspace edge. So a failure here is this package's own
# config file or the one `find_dependency` line in it — which is exactly the
# defect class this lane exists for, and the reason the fixture below now
# reaches a header that would not compile without the edge resolving.
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
