# SPDX-License-Identifier: Apache-2.0

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would
# print "passed" and return 0 without having checked anything. Undefine it for
# the test target only: the library under test stays Release, the checks stay
# live. See libs/motionRuntime/tests/CMakeLists.txt, which hit this first.
set(_vrm_adapter_mocopi_assertions
    $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(vrmAdapterMocopi_tests test_vrm_adapter_mocopi.cpp)
target_link_libraries(vrmAdapterMocopi_tests
    PRIVATE vrmAdapterMocopi::vrmAdapterMocopi)
target_compile_options(vrmAdapterMocopi_tests
    PRIVATE ${_vrm_adapter_mocopi_assertions})
add_test(NAME vrmAdapterMocopi_unit COMMAND vrmAdapterMocopi_tests)

# The recorded packet format, and -- once one exists -- the corpus recorded in
# it.
add_executable(vrmAdapterMocopi_packetCapture_tests test_packet_capture.cpp)
target_link_libraries(vrmAdapterMocopi_packetCapture_tests
    PRIVATE vrmAdapterMocopi::vrmAdapterMocopi)
target_compile_options(vrmAdapterMocopi_packetCapture_tests
    PRIVATE ${_vrm_adapter_mocopi_assertions})
add_test(NAME vrmAdapterMocopi_packetCapture
         COMMAND vrmAdapterMocopi_packetCapture_tests)

# The socket, on its own CTest name. It is the only test in this adapter that
# binds one, so a runner that forbids sockets excludes a name rather than a
# claim (roadmap §9.5) -- and the sibling adapter's two socket tests have
# already run green in all three `kind: workspace` cells, so this is a measured
# arrangement rather than a hopeful one. It never touches port 12351: every bind
# here is loopback on an OS-assigned port, so a developer with a real source
# aimed at this machine does not lose it to the test suite.
add_executable(vrmAdapterMocopi_udpReceiver_tests test_udp_receiver.cpp)
target_link_libraries(vrmAdapterMocopi_udpReceiver_tests
    PRIVATE vrmAdapterMocopi::vrmAdapterMocopi)
target_compile_options(vrmAdapterMocopi_udpReceiver_tests
    PRIVATE ${_vrm_adapter_mocopi_assertions})
add_test(NAME vrmAdapterMocopi_udpReceiver
         COMMAND vrmAdapterMocopi_udpReceiver_tests)

# The over-long datagram case, on its own name so that "could not check this
# here" is a word in the ctest summary rather than a line nobody sees.
#
# It needs IPv6 to reach at all -- that is the only transport carrying more than
# the IPv4 payload maximum the capture format bounds itself by -- and a runner
# without IPv6 loopback cannot run it. Inside the suite above, that would have
# been invisible: ctest hides a passing test's output, so a lane that checked
# the claim and a lane that quietly declined to print the same line. With
# SKIP_RETURN_CODE the summary says `Skipped`, and the question "was this
# actually verified on POSIX" is answered by reading the log instead of by
# re-running anything on a machine somebody has to find.
add_test(NAME vrmAdapterMocopi_udpReceiverTruncation
         COMMAND vrmAdapterMocopi_udpReceiver_tests truncation)
set_tests_properties(vrmAdapterMocopi_udpReceiverTruncation PROPERTIES
    SKIP_RETURN_CODE 77)

# The container and the two packet kinds. Its own name rather than a case inside
# the suite above, because the two layers fail for different reasons: a capture
# that will not parse is a fixture problem, and a datagram that will not decode is
# a decoder problem, and a single red name would not say which.
add_executable(vrmAdapterMocopi_motionPacket_tests test_motion_packet.cpp)
target_link_libraries(vrmAdapterMocopi_motionPacket_tests
    PRIVATE vrmAdapterMocopi::vrmAdapterMocopi)
target_compile_options(vrmAdapterMocopi_motionPacket_tests
    PRIVATE ${_vrm_adapter_mocopi_assertions})
add_test(NAME vrmAdapterMocopi_motionPacket
         COMMAND vrmAdapterMocopi_motionPacket_tests)

# The first layer that knows a humanoid exists, on its own name for the same
# reason: a joint that maps to the wrong bone and a datagram that will not decode
# are different failures, and one red name would not say which.
add_executable(vrmAdapterMocopi_skeletonMap_tests test_skeleton_map.cpp)
target_link_libraries(vrmAdapterMocopi_skeletonMap_tests
    PRIVATE vrmAdapterMocopi::vrmAdapterMocopi)
target_compile_options(vrmAdapterMocopi_skeletonMap_tests
    PRIVATE ${_vrm_adapter_mocopi_assertions})
add_test(NAME vrmAdapterMocopi_skeletonMap
         COMMAND vrmAdapterMocopi_skeletonMap_tests)

# The layer that decides, on its own name for the same reason again: a session
# whose restart went undetected and a joint that mapped to the wrong bone are
# different failures, and one red name would not say which.
add_executable(vrmAdapterMocopi_frameAssembler_tests test_frame_assembler.cpp)
target_link_libraries(vrmAdapterMocopi_frameAssembler_tests
    PRIVATE vrmAdapterMocopi::vrmAdapterMocopi)
target_compile_options(vrmAdapterMocopi_frameAssembler_tests
    PRIVATE ${_vrm_adapter_mocopi_assertions})
add_test(NAME vrmAdapterMocopi_frameAssembler
         COMMAND vrmAdapterMocopi_frameAssembler_tests)

# The bridge into the runtime, on its own name for the same reason once more: a
# session whose restart was mishandled and a session whose restart went
# undetected are different failures one layer apart, and one red name would not
# say which. It is also the only name here that links the intake's behaviour into
# its claims, so a `motionRuntime` change that altered what `Push` accepts turns
# this red and leaves the four below it green -- which is the division working
# rather than a flaky boundary.
add_executable(vrmAdapterMocopi_liveSource_tests test_live_source.cpp)
target_link_libraries(vrmAdapterMocopi_liveSource_tests
    PRIVATE vrmAdapterMocopi::vrmAdapterMocopi)
target_compile_options(vrmAdapterMocopi_liveSource_tests
    PRIVATE ${_vrm_adapter_mocopi_assertions})
add_test(NAME vrmAdapterMocopi_liveSource
         COMMAND vrmAdapterMocopi_liveSource_tests)

set(_vrm_adapter_mocopi_tests
    vrmAdapterMocopi_unit vrmAdapterMocopi_packetCapture
    vrmAdapterMocopi_motionPacket vrmAdapterMocopi_skeletonMap
    vrmAdapterMocopi_frameAssembler vrmAdapterMocopi_liveSource
    vrmAdapterMocopi_udpReceiver vrmAdapterMocopi_udpReceiverTruncation)

# Every committed capture parses and re-emits byte-identically, or every
# decoder test downstream of it compares against a fixture that has drifted from
# the writer. The same binary takes the corpus directory as an argument rather
# than referencing a target from another directory, so a standalone configure of
# this adapter still generates.
#
# The corpus exists as of the decoder, and the guard stays: a standalone
# configure of a source tree without it must still generate.
#
# Five names run over the same directory and they check different things, one
# per layer that consumes the bytes -- which is the arrangement rather than an
# accumulation: a capture is asked a different question by each component, so a
# fixture is never merely parsed by the layer it was written for. `_corpus` is
# the format's: every capture parses and re-emits byte-identically, so a fixture
# can never drift from the C++ writer. `_motionPacketCorpus` is the decoder's:
# every capture decodes to what its `sourceId` claims it pins, or is refused with
# the code, per datagram, that it exists to be refused with. The three below it
# are named at their own `add_test` calls.
#
# Neither reads `manifest.json` — that is `_packetGen`'s, which compares the
# manifest's derived fields against the *generator*. So the manifest is pinned to
# the bytes and the bytes are pinned to the decoder, with no test asserting the
# manifest's prose against either; a `pins` string is a claim to a reviewer and
# not an assertion, which is why the assertions below are named after the
# phenomenon rather than reading it out of the JSON.
set(_vrm_adapter_mocopi_corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
if(EXISTS "${_vrm_adapter_mocopi_corpus}")
    add_test(NAME vrmAdapterMocopi_corpus
        COMMAND vrmAdapterMocopi_packetCapture_tests
                "${_vrm_adapter_mocopi_corpus}")
    add_test(NAME vrmAdapterMocopi_motionPacketCorpus
        COMMAND vrmAdapterMocopi_motionPacket_tests
                "${_vrm_adapter_mocopi_corpus}")
    # And a third reading of the same directory: what each capture becomes once
    # its joints carry canonical bones. This is where the fixtures are finally
    # asked which arm is which -- `arms-lowered-60hz` was written to be asked
    # that and could not be, one layer down, by a decoder that names no bone.
    add_test(NAME vrmAdapterMocopi_skeletonMapCorpus
        COMMAND vrmAdapterMocopi_skeletonMap_tests
                "${_vrm_adapter_mocopi_corpus}")
    # And a fourth reading, which is the one `frame-loss-60hz` was written for:
    # a gap, a duplicate delivery and a restart are sequence questions, and every
    # layer below sees one datagram at a time. Until this name existed the only
    # thing any test could say about that capture was that its frames map.
    add_test(NAME vrmAdapterMocopi_frameAssemblerCorpus
        COMMAND vrmAdapterMocopi_frameAssembler_tests
                "${_vrm_adapter_mocopi_corpus}")
    # And a fifth reading, which is the first one whose subject is a *pose a
    # consumer sampled* rather than something a layer produced. It replays every
    # capture through one reused buffer -- so the claim that a datagram need not
    # outlive the call is checked by the poses matching rather than by an
    # assertion about pointers -- and it is the only place the two restart
    # policies are run over the same bytes.
    add_test(NAME vrmAdapterMocopi_liveSourceCorpus
        COMMAND vrmAdapterMocopi_liveSource_tests
                "${_vrm_adapter_mocopi_corpus}")
    list(APPEND _vrm_adapter_mocopi_tests
         vrmAdapterMocopi_corpus vrmAdapterMocopi_motionPacketCorpus
         vrmAdapterMocopi_skeletonMapCorpus
         vrmAdapterMocopi_frameAssemblerCorpus
         vrmAdapterMocopi_liveSourceCorpus)
endif()

if(WIN32)
    if(TARGET gf)
        set(_vrm_adapter_mocopi_gf gf)
    else()
        set(_vrm_adapter_mocopi_gf pxr::gf)
    endif()
    # The adapter is a static library, but motionCore and motionRuntime call
    # exported Gf entry points, so the test executable loads usd_gf.dll at run
    # time -- which pulls usd_tf.dll -> tbb.dll from bin/ plus python3xx.dll. An
    # activated `ost` session has all three on PATH; a plain-CMake `ctest` run
    # does not, and the test exits 0xC0000135 without them.
    set(_vrm_adapter_mocopi_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_adapter_mocopi_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_adapter_mocopi_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_vrm_adapter_mocopi_python_dir
                               "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _vrm_adapter_mocopi_test_path
             "PATH=path_list_prepend:${_vrm_adapter_mocopi_python_dir}")
    endif()
    set_tests_properties(${_vrm_adapter_mocopi_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vrm_adapter_mocopi_test_path}")
endif()

if(Python3_Interpreter_FOUND)
    # The *test executable*, not $<TARGET_FILE:vrmAdapterMocopi>. The adapter is
    # a static library and an archive records no imports at all, so inspecting
    # it is a check that cannot fail. This executable is the first artifact in
    # which the adapter's real transitive imports exist, and its link closure is
    # exactly the one the boundary is about: the adapter, motionCore, and
    # motionRuntime.
    #
    # `Python3_Interpreter_FOUND` is trustworthy *here* and is not at the root:
    # the adapter's own CMakeLists re-runs find_package(Python3 Interpreter) in
    # this scope after pxrConfig.cmake has clobbered it with a Development-only
    # re-find. A test guarded on it above that line does not fail, it silently
    # never registers -- so if this entry ever disappears from `ctest -N`, that
    # is the first thing to check.
    add_test(NAME vrmAdapterMocopi_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:vrmAdapterMocopi_tests>")

    # vrmAdapterMocopi_corpus proves the committed captures are canonical for the
    # C++ writer; this proves they still match the generator that authored them.
    # Without it a hand-edited fixture would stay green while the generator no
    # longer reproduces it, and the corpus would quietly become unregenerable --
    # which matters more here than for the sibling adapter, because regenerating
    # is the only way this corpus can ever be extended: the measurement it was
    # written from is not committed.
    if(EXISTS "${_vrm_adapter_mocopi_corpus}")
        add_test(NAME vrmAdapterMocopi_packetGen
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_packets.py"
                --check --output "${_vrm_adapter_mocopi_corpus}")
    endif()
endif()
