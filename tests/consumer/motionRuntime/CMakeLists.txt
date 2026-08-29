# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `motionRuntime` package, and nothing else.
#
# The first fixture whose package has both kinds of edge at once: one this
# workspace produces and installs into the scratch prefix, and one it does not
# and never will (PACKAGE_CONTRACT.md §4.2). Those resolve by different
# mechanisms -- a workspace edge through a config file this repository wrote,
# an external one through whatever the host supplies -- and a package whose
# config declared the first and forgot the second would configure on a
# workstation that happened to have OpenUSD somewhere and fail on a clean one.
#
# The consumer names neither of them, which is the contract this fixture is
# for: one `find_package`, one target, and the config file responsible for
# everything behind it.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(motionRuntimeConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE motionRuntime TARGET motionRuntime::motionRuntime)
