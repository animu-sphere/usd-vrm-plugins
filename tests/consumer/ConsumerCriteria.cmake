# SPDX-License-Identifier: Apache-2.0
#
# The six acceptance criteria of PACKAGE_CONTRACT.md §5, in the only place they
# can be asked: a project that is not this workspace.
#
# This module is shared by every fixture under tests/consumer/, and sharing it
# is the point rather than a convenience. Twelve fixtures each writing their own
# `find_package` and their own `if(TARGET)` would be twelve chances for one of
# them to check less than the others and still print a pass -- and the failure
# mode this whole track exists for is a check that was never run, not a check
# that ran and said no.
#
# It names no workspace target. It takes the package under test as an argument,
# so the only workspace identity that appears in a fixture is the one the
# fixture is for, which is what makes criterion 5 mechanically checkable
# (scripts/check_package_consumer.py).
#
#     consumer_criteria(PACKAGE osc TARGET osc::osc)
#
# Every line it prints is parsed by the driver, so the spellings below are an
# interface:
#
#     consumer: criterion <n> MET <text>
#     consumer: criterion <n> NOT MET <text>      (always fatal)
#     consumer: closure <item>                    (one per link-closure entry)
#     consumer: platform <system> <processor>
#
# Criterion 4 belongs to the build rather than to the configure, and criteria 5
# and 6 cannot be answered from inside one configure at all: 5 is a property of
# the fixture's sources and 6 is a property of three platforms. The driver
# reports those three; this module reports 1, 2 and 3 and the evidence 6 needs.

# `IMPORTED_LOCATION` and friends are per-configuration, and an imported target
# from a config file carries the configurations it was built in. Resolving the
# archive on disk -- rather than trusting that the target exists -- is what
# makes criterion 2 more than a restatement of criterion 1: a config file whose
# targets file points at a path the package did not install defines a perfectly
# good target that no consumer can link.
function(_consumer_imported_files target out_var)
    set(_files "")
    get_target_property(_type ${target} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    get_target_property(_configs ${target} IMPORTED_CONFIGURATIONS)
    if(NOT _configs)
        set(_configs "")
    endif()
    foreach(_config IN LISTS _configs)
        get_target_property(_loc ${target} IMPORTED_LOCATION_${_config})
        if(_loc)
            list(APPEND _files "${_loc}")
        endif()
        get_target_property(_implib ${target} IMPORTED_IMPLIB_${_config})
        if(_implib)
            list(APPEND _files "${_implib}")
        endif()
    endforeach()
    get_target_property(_loc ${target} IMPORTED_LOCATION)
    if(_loc)
        list(APPEND _files "${_loc}")
    endif()
    set(${out_var} "${_files}" PARENT_SCOPE)
endfunction()

# Walks INTERFACE_LINK_LIBRARIES transitively. Every namespaced entry must be a
# defined target -- that is criterion 3, and it is exactly the check the OSC-3
# defect would have failed: `osc::osc` sat on vrmAdapterVmc's interface link
# line while nothing had resolved the package that defines it.
#
# The accumulated list is also the answer criterion 6 compares across the three
# platforms, so unnamespaced entries (`ws2_32`, `m`, an absolute path) are
# collected too. They cannot be checked here -- a raw library name is resolved
# by the linker, not by CMake -- but a platform that grew one is a difference a
# lane must see.
function(_consumer_walk_closure target out_var)
    set(_pending "${target}")
    set(_seen "")
    set(_closure "")
    while(_pending)
        list(POP_FRONT _pending _item)
        if(_item IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen "${_item}")

        # A generator expression is not resolvable at configure time. Recording
        # it verbatim is honest; pretending to have checked it is not.
        if(_item MATCHES "^\$<")
            list(APPEND _closure "${_item}")
            continue()
        endif()

        if(NOT _item STREQUAL "${target}")
            list(APPEND _closure "${_item}")
        endif()

        if(_item MATCHES "::")
            if(NOT TARGET ${_item})
                message(FATAL_ERROR
                    "consumer: criterion 3 NOT MET ${_item} is on the link "
                    "closure of ${target} and no package has defined it -- the "
                    "config that brought it in is missing a find_dependency "
                    "(PACKAGE_CONTRACT.md §3 rule 1)")
            endif()
        endif()

        if(TARGET ${_item})
            get_target_property(_next ${_item} INTERFACE_LINK_LIBRARIES)
            if(_next)
                list(APPEND _pending ${_next})
            endif()
        endif()
    endwhile()
    list(REMOVE_DUPLICATES _closure)
    list(SORT _closure)
    set(${out_var} "${_closure}" PARENT_SCOPE)
endfunction()

function(consumer_criteria)
    cmake_parse_arguments(ARG "" "PACKAGE;TARGET" "" ${ARGN})
    foreach(_required PACKAGE TARGET)
        if(NOT ARG_${_required})
            message(FATAL_ERROR "consumer_criteria: ${_required} is required")
        endif()
    endforeach()

    message(STATUS "consumer: platform ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")

    # 1. The config is found, and CONFIG mode is not negotiable: a MODULE-mode
    #    fallback would let a stray Findosc.cmake answer for the package.
    find_package(${ARG_PACKAGE} CONFIG REQUIRED)
    message(STATUS "consumer: criterion 1 MET find_package(${ARG_PACKAGE} CONFIG REQUIRED) "
                   "resolved ${${ARG_PACKAGE}_DIR}")

    # 2. The exported target exists and the file it names is on disk.
    if(NOT TARGET ${ARG_TARGET})
        message(FATAL_ERROR
            "consumer: criterion 2 NOT MET ${ARG_PACKAGE} was found and defines "
            "no ${ARG_TARGET}")
    endif()
    _consumer_imported_files(${ARG_TARGET} _files)
    foreach(_file IN LISTS _files)
        if(NOT EXISTS "${_file}")
            message(FATAL_ERROR
                "consumer: criterion 2 NOT MET ${ARG_TARGET} names ${_file}, "
                "which the prefix does not contain")
        endif()
    endforeach()
    if(_files)
        message(STATUS "consumer: criterion 2 MET ${ARG_TARGET} resolves to ${_files}")
    else()
        message(STATUS "consumer: criterion 2 MET ${ARG_TARGET} is an interface target")
    endif()

    # 3. Everything the interface drags in resolves too.
    _consumer_walk_closure(${ARG_TARGET} _closure)
    foreach(_item IN LISTS _closure)
        message(STATUS "consumer: closure ${_item}")
    endforeach()
    list(LENGTH _closure _closure_size)
    message(STATUS "consumer: criterion 3 MET ${_closure_size} transitive link "
                   "entries, every namespaced one defined")

    # 4. Building this is the criterion; the driver decides whether it did.
    #    `main.cpp` includes a public header and calls into it, so a package
    #    that installs a config and forgets its headers fails to compile rather
    #    than passing three configure-time criteria and shipping. A fixture that
    #    only linked would prove the config file and not the header install
    #    (packaging-hardening.md PKG-1); the driver checks that the include is
    #    there, because a `main.cpp` that stopped including one would still
    #    build.
    add_executable(consumer main.cpp)
    target_link_libraries(consumer PRIVATE ${ARG_TARGET})
endfunction()
