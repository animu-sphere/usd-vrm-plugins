# SPDX-License-Identifier: Apache-2.0

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would print
# "verified" and return 0 without having checked anything. Undefine it for the
# test targets only. See libs/motionRuntime/tests/CMakeLists.txt, which hit this
# first.
set(_motion_bvh_assertions $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

# The frozen diagnostic set: its strings, its two halves, its formatting.
add_executable(motionBvh_diagnostics_tests test_diagnostics.cpp)
# The syntax model, with no parser anywhere near it.
add_executable(motionBvh_document_tests test_bvh_document.cpp)
# The parser, over hand-built text and over the committed corpus.
add_executable(motionBvh_parser_tests test_bvh_parser.cpp)

foreach(_motion_bvh_test motionBvh_diagnostics_tests motionBvh_document_tests
                         motionBvh_parser_tests)
    target_link_libraries(${_motion_bvh_test} PRIVATE motionBvh::motionBvh)
    target_compile_options(${_motion_bvh_test} PRIVATE ${_motion_bvh_assertions})
    if(MSVC)
        # The headers cite roadmap sections by their `§` sign, and MSVC reads
        # source in the *system* code page unless told otherwise -- so a
        # Japanese-Windows build warns C4819 on every include without this.
        target_compile_options(${_motion_bvh_test} PRIVATE /utf-8)
    endif()
endforeach()

add_test(NAME motionBvh_diagnostics COMMAND motionBvh_diagnostics_tests)
add_test(NAME motionBvh_document COMMAND motionBvh_document_tests)
add_test(NAME motionBvh_parser COMMAND motionBvh_parser_tests)

# No ENVIRONMENT_MODIFICATION for the runtime's bin/ here, unlike every other
# suite in this workspace: motionBvh links no OpenUSD, so these executables load
# nothing a plain `ctest` cannot find.

# The same parser over every committed fixture, against a table of what each
# file must produce or refuse. The binary takes the directory as an argument
# rather than referencing a target from another directory, so a standalone
# configure of this library still generates.
set(_motion_bvh_corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus/generated")
if(EXISTS "${_motion_bvh_corpus}")
    add_test(NAME motionBvh_corpus
        COMMAND motionBvh_parser_tests "${_motion_bvh_corpus}" generated)
endif()

# The other half: real producer exports, with their own expectation table. The
# generated fixtures are shapes this repository wrote and can therefore only
# confirm what it already believed; this one is 1.2 MB of somebody else's
# software, and it is the only fixture here that can be surprising.
set(_motion_bvh_recorded
    "${CMAKE_CURRENT_SOURCE_DIR}/corpus/recorded/redistributable")
if(EXISTS "${_motion_bvh_recorded}")
    add_test(NAME motionBvh_recordedCorpus
        COMMAND motionBvh_parser_tests "${_motion_bvh_recorded}" recorded)
endif()

if(Python3_Interpreter_FOUND)
    # The *test executable*, not $<TARGET_FILE:motionBvh>. This library is a
    # static archive and an archive records no imports at all, so inspecting it
    # is a check that cannot fail. The executable is the first artifact in which
    # the library's real transitive imports exist -- and for this library that
    # closure is meant to be empty of OpenUSD entirely.
    add_test(NAME motionBvh_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:motionBvh_parser_tests>")

    # motionBvh_corpus proves the fixtures say what the C++ parser reads out of
    # them; this measures the same files with an independent Python scanner and
    # checks the manifest against both. Without it the corpus would be pinned by
    # one implementation agreeing with itself.
    if(EXISTS "${_motion_bvh_corpus}")
        add_test(NAME motionBvh_corpusManifest
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_corpus.py"
                --check --corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
    endif()

    # The recorded half's counts are a measurement of someone else's exporter,
    # so the same scanner re-derives them and the hash pins the bytes those
    # measurements were taken from. A recorded file whose hash moved is a
    # different capture wearing the same name.
    if(EXISTS "${_motion_bvh_recorded}")
        add_test(NAME motionBvh_recordedManifest
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../tools/check_corpus.py"
                --check --recorded
                --corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
    endif()
endif()
