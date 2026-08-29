# SPDX-License-Identifier: Apache-2.0
#
# A consumer of the installed `vrmRetarget` package, and nothing else.
#
# The top of the motion stack, and the first package here whose edges are not
# all visible from its public headers. `pxr` and the value contract arrive in
# the include list; the runtime layer between them does not, because nothing in
# this package's interface names a type from it -- it is a link-time edge, used
# inside the implementation. A config that declared only what the headers show
# would compile a consumer and fail it at the link, which is the same defect
# PACKAGE_CONTRACT.md §1 describes arriving one stage later (§4.2).
#
# That is the property this fixture is here to check, and it is why criterion 4
# is a *build* rather than a compile: the fixture has to reach a call whose
# archive member carries the invisible edge, or the link would never be asked
# the question.
#
# Nothing here names a path into the workspace source tree, and the driver
# copies this directory outside the repository before configuring it
# (PACKAGE_CONTRACT.md §5 criterion 5).
cmake_minimum_required(VERSION 3.24)

project(vrmRetargetConsumer LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../ConsumerCriteria.cmake")

consumer_criteria(PACKAGE vrmRetarget TARGET vrmRetarget::vrmRetarget)
