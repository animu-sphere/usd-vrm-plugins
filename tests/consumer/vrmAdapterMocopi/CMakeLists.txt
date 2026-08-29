# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `vrmAdapterMocopi` package, and nothing else.
#
# This is the package with a standing platform gap. Its standalone build had
# been unverified since the receiver added a platform link and the installed
# config was edited without the check being re-run
# ([#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113)) -- a
# measurement that was true about an older package, which is not the same claim
# as no measurement at all. This fixture is what replaces it.
#
# Four packages must resolve before its target links and the consumer names none
# of them (PACKAGE_CONTRACT.md §4.3). One of the four carries the platform link
# this package inherits rather than declares, so the closure recorded here is
# also where the imported half of #113 is answered. The raw-library half is not
# -- that needs a POSIX host, where the absence of the socket library is what
# there is to see (roadmap/packaging-hardening.md PKG-5).
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(vrmAdapterMocopiConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE vrmAdapterMocopi TARGET vrmAdapterMocopi::vrmAdapterMocopi)
