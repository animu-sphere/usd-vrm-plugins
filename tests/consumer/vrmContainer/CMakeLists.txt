# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `vrmContainer` package, and nothing else.
#
# The first fixture for a `SHARED` package (PACKAGE_CONTRACT.md §4.2), and that
# is the whole reason this row is measured separately rather than assumed to
# behave like the static ones beside it. Every criterion up to and including the
# link is answered by an import library; the shared object itself is not opened
# until the consumer *runs*, so a package that installed a config, a header root
# and a `.lib` and forgot the `.dll` would meet criteria 1-4 and exit
# `0xC0000135` on the line after them. The driver runs the built consumer with
# the prefix's own `bin` and `lib` on the loader path and nothing else, so what
# this fixture asks that a static one cannot is: did the prefix ship the half a
# consumer needs at load time.
#
# Its required-package set is empty, like `osc`'s, so a failure here is this
# package's own config file. Unlike `osc` it is in the aggregate product, and it
# is the package `usdVrmFileFormat` links -- the shape that the driver's two
# prefix sources could still diverge on is a *bundle*'s, and this is the library
# under one.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(vrmContainerConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE vrmContainer TARGET vrmContainer::vrmContainer)
