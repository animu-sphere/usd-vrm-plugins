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
# Extraction: a channel set into a track, and the shapes BVH permits that the
# neutral value model does not.
add_executable(motionBvh_extract_tests test_bvh_extract.cpp)
# The whole recorded path over a real export, which is a *caller* holding a
# reader and a profile at once -- something neither library may do and a test
# may. Registered below only where both the corpus half and the profiles are
# present.
add_executable(motionBvh_conversion_tests test_bvh_conversion.cpp)

foreach(_motion_bvh_test motionBvh_diagnostics_tests motionBvh_document_tests
                         motionBvh_parser_tests motionBvh_extract_tests
                         motionBvh_conversion_tests)
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
add_test(NAME motionBvh_extract COMMAND motionBvh_extract_tests)

# Still no ENVIRONMENT_MODIFICATION for the runtime's bin/, unlike every other
# suite in this workspace, and with the extractor in the tree that is now a
# measurement rather than a restatement. motionBvh links motionSource, which
# links motionCore, whose Gf value types live in a DLL -- but nothing on the
# extraction path names one, so no object that needs Gf is pulled out of those
# archives and these executables still load nothing a plain `ctest` cannot find.
# A future `motionSource` header that put a Gf type on the extraction path would
# make this line wrong, and the failure would be a 0xC0000135 here rather than a
# review comment.

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

# The recorded export driven the whole way to canonical motion, against the
# shipped profile written from it. It needs both halves and is skipped rather
# than failed without them: the profiles are package data, the export is a
# corpus file, and a library test that failed on their absence would fail a
# standalone configure for a reason that is not about this library.
set(_motion_bvh_profiles "${CMAKE_CURRENT_SOURCE_DIR}/../../../profiles/motion")
if(EXISTS "${_motion_bvh_recorded}" AND EXISTS "${_motion_bvh_profiles}")
    add_test(NAME motionBvh_recordedConversion
        COMMAND motionBvh_conversion_tests
                "${_motion_bvh_recorded}" "${_motion_bvh_profiles}")
    # Unlike every other test here, this one reaches canonical motion and
    # therefore Gf -- so it needs what the runtime's DLLs need, the way
    # libs/motionSource/tests does. The paragraph above about loading nothing is
    # about the other four executables.
    if(WIN32)
        if(TARGET gf)
            set(_motion_bvh_gf gf)
        else()
            set(_motion_bvh_gf pxr::gf)
        endif()
        set(_motion_bvh_conversion_path
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_motion_bvh_gf}>"
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_motion_bvh_gf}>/../bin")
        if(Python3_EXECUTABLE)
            get_filename_component(_motion_bvh_python_dir
                                   "${Python3_EXECUTABLE}" DIRECTORY)
            list(APPEND _motion_bvh_conversion_path
                 "PATH=path_list_prepend:${_motion_bvh_python_dir}")
        endif()
        set_tests_properties(motionBvh_recordedConversion PROPERTIES
            ENVIRONMENT_MODIFICATION "${_motion_bvh_conversion_path}")
    endif()
endif()

if(Python3_Interpreter_FOUND)
    # The *test executable*, not $<TARGET_FILE:motionBvh>. This library is a
    # static archive and an archive records no imports at all, so inspecting it
    # is a check that cannot fail. The executable is the first artifact in which
    # the library's real transitive imports exist.
    #
    # It is `motionBvh_parser_tests` deliberately, and since the extractor
    # landed that is a narrower claim than it reads as. An archive contributes
    # only the objects something references, so this executable's closure is the
    # syntax half's -- the half that is meant to be empty of OpenUSD entirely,
    # and the half whose emptiness is a property worth a check. The library as a
    # whole no longer has that property and cannot: its declared edge reaches
    # motionCore's Gf value types, and in a monolithic OpenUSD build no import
    # listing could tell an allowed dependency from a forbidden one anyway
    # (libs/motionSource/tests/check_boundaries.py says so at length). What
    # covers the rest is the source rule, which forbids every OpenUSD name in
    # every file here including the extractor's.
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
