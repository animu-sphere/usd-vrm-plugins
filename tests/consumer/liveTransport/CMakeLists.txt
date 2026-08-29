# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `liveTransport` package, and nothing else.
#
# This is the row where the three platforms are *expected* to disagree, and it
# is the reason criterion 6 exists at all: the package carries `ws2_32` on
# Windows and `Threads::Threads` elsewhere (PACKAGE_CONTRACT.md §4.2), so the
# closure this fixture records is the one thing in the table a single host is
# structurally unable to check. What one host can do is record its half, which
# is what PKG-4's lane compares -- and what a Windows run alone cannot say is
# that the threading link is *absent* here, which is why the POSIX run of this
# same fixture is worth more than this one (roadmap/packaging-hardening.md
# PKG-5).
#
# It is also the package whose platform dependency is easiest to ship broken.
# `find_dependency(Threads)` sits inside `if(NOT WIN32)` in the installed
# config, so on Windows that line is never reached: a config that lost it would
# be green on every Windows lane and red the first time anyone configured on
# Linux. The driver evaluates no such condition and says so rather than
# guessing, which is why `--mutate no-dependency --dependency Threads` ends
# **inconclusive** here instead of accusing this fixture.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(liveTransportConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE liveTransport TARGET liveTransport::liveTransport)
