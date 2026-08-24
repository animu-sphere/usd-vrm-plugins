# SPDX-License-Identifier: Apache-2.0

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would
# print "passed" and return 0 without having checked anything. Undefine it for
# the test target only: the library under test stays Release, the checks stay
# live. See libs/motionRuntime/tests/CMakeLists.txt, which hit this first.
set(_vrm_adapter_vmc_assertions
    $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(vrmAdapterVmc_tests test_vrm_adapter_vmc.cpp)
target_link_libraries(vrmAdapterVmc_tests PRIVATE vrmAdapterVmc::vrmAdapterVmc)
target_compile_options(vrmAdapterVmc_tests
    PRIVATE ${_vrm_adapter_vmc_assertions})
add_test(NAME vrmAdapterVmc_unit COMMAND vrmAdapterVmc_tests)

# The recorded packet format, and the corpus recorded in it.
add_executable(vrmAdapterVmc_packetCapture_tests test_packet_capture.cpp)
target_link_libraries(vrmAdapterVmc_packetCapture_tests
    PRIVATE vrmAdapterVmc::vrmAdapterVmc)
target_compile_options(vrmAdapterVmc_packetCapture_tests
    PRIVATE ${_vrm_adapter_vmc_assertions})
add_test(NAME vrmAdapterVmc_packetCapture
         COMMAND vrmAdapterVmc_packetCapture_tests)

# The OSC layer, which knows nothing about VMC.
add_executable(vrmAdapterVmc_oscPacket_tests test_osc_packet.cpp)
target_link_libraries(vrmAdapterVmc_oscPacket_tests
    PRIVATE vrmAdapterVmc::vrmAdapterVmc)
target_compile_options(vrmAdapterVmc_oscPacket_tests
    PRIVATE ${_vrm_adapter_vmc_assertions})
add_test(NAME vrmAdapterVmc_oscPacket COMMAND vrmAdapterVmc_oscPacket_tests)

# The VMC layer, which knows addresses and argument forms and nothing about a
# humanoid.
add_executable(vrmAdapterVmc_vmcMessage_tests test_vmc_message.cpp)
target_link_libraries(vrmAdapterVmc_vmcMessage_tests
    PRIVATE vrmAdapterVmc::vrmAdapterVmc)
target_compile_options(vrmAdapterVmc_vmcMessage_tests
    PRIVATE ${_vrm_adapter_vmc_assertions})
add_test(NAME vrmAdapterVmc_vmcMessage COMMAND vrmAdapterVmc_vmcMessage_tests)

# The first layer that knows a humanoid exists: VMC's names and VMC's axes into
# canonical semantics.
add_executable(vrmAdapterVmc_skeletonMap_tests test_skeleton_map.cpp)
target_link_libraries(vrmAdapterVmc_skeletonMap_tests
    PRIVATE vrmAdapterVmc::vrmAdapterVmc)
target_compile_options(vrmAdapterVmc_skeletonMap_tests
    PRIVATE ${_vrm_adapter_vmc_assertions})
add_test(NAME vrmAdapterVmc_skeletonMap
         COMMAND vrmAdapterVmc_skeletonMap_tests)

# The first layer that decides rather than converts: where a frame begins, and
# what it means that a bone is not in it.
add_executable(vrmAdapterVmc_frameAssembler_tests test_frame_assembler.cpp)
target_link_libraries(vrmAdapterVmc_frameAssembler_tests
    PRIVATE vrmAdapterVmc::vrmAdapterVmc)
target_compile_options(vrmAdapterVmc_frameAssembler_tests
    PRIVATE ${_vrm_adapter_vmc_assertions})
add_test(NAME vrmAdapterVmc_frameAssembler
         COMMAND vrmAdapterVmc_frameAssembler_tests)

# The bridge into the runtime, which is the last layer that is still this
# adapter's: what it hands over is `motionRuntime`'s from there on.
add_executable(vrmAdapterVmc_liveSource_tests test_live_source.cpp)
target_link_libraries(vrmAdapterVmc_liveSource_tests
    PRIVATE vrmAdapterVmc::vrmAdapterVmc)
target_compile_options(vrmAdapterVmc_liveSource_tests
    PRIVATE ${_vrm_adapter_vmc_assertions})
add_test(NAME vrmAdapterVmc_liveSource COMMAND vrmAdapterVmc_liveSource_tests)

# The socket, and the queue that keeps it off the decoder's thread. This is the
# only test binary in the adapter that opens one, which is deliberate: every
# other claim here is made from committed bytes, so a runner that forbids
# sockets excludes these two names and loses no coverage of the decode path
# (roadmap §9.5, the `adapter-integration-loopback` lane).
add_executable(vrmAdapterVmc_udpReceiver_tests test_udp_receiver.cpp)
target_link_libraries(vrmAdapterVmc_udpReceiver_tests
    PRIVATE vrmAdapterVmc::vrmAdapterVmc)
target_compile_options(vrmAdapterVmc_udpReceiver_tests
    PRIVATE ${_vrm_adapter_vmc_assertions})
add_test(NAME vrmAdapterVmc_udpReceiver
         COMMAND vrmAdapterVmc_udpReceiver_tests)

# The over-long datagram case, on its own name so that "could not check this
# here" is a word in the ctest summary rather than a line nobody sees.
#
# It needs IPv6 to reach at all -- that is the only transport carrying more than
# the IPv4 payload maximum the capture format bounds itself by -- and a runner
# without IPv6 loopback cannot run it. Inside the suite above that would have
# been invisible: ctest hides a passing test's output, so a lane that checked
# the claim and a lane that quietly declined would print the same line. With
# SKIP_RETURN_CODE the summary says `Skipped`, and "was this actually verified
# on POSIX" is answered by reading the log rather than by re-running anything.
# Mirrors vrmAdapterMocopi_udpReceiverTruncation, deliberately.
add_test(NAME vrmAdapterVmc_udpReceiverTruncation
         COMMAND vrmAdapterVmc_udpReceiver_tests truncation)
set_tests_properties(vrmAdapterVmc_udpReceiverTruncation PROPERTIES
    SKIP_RETURN_CODE 77)

set(_vrm_adapter_vmc_tests
    vrmAdapterVmc_unit vrmAdapterVmc_packetCapture vrmAdapterVmc_oscPacket
    vrmAdapterVmc_vmcMessage vrmAdapterVmc_skeletonMap
    vrmAdapterVmc_frameAssembler vrmAdapterVmc_liveSource
    vrmAdapterVmc_udpReceiver vrmAdapterVmc_udpReceiverTruncation)

# Every committed capture parses and re-emits byte-identically, or every
# decoder test downstream of it compares against a fixture that has drifted from
# the writer. The same binary takes the corpus directory as an argument rather
# than referencing a target from another directory, so a standalone configure of
# this adapter still generates.
set(_vrm_adapter_vmc_corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
if(EXISTS "${_vrm_adapter_vmc_corpus}")
    add_test(NAME vrmAdapterVmc_corpus
        COMMAND vrmAdapterVmc_packetCapture_tests
                "${_vrm_adapter_vmc_corpus}")
    # The same corpus, one layer up: what the recorded datagrams decode to. The
    # counts it holds come from the generator's structure, so a fixture that
    # changed shape fails here as well as in the round-trip check.
    add_test(NAME vrmAdapterVmc_oscCorpus
        COMMAND vrmAdapterVmc_oscPacket_tests "${_vrm_adapter_vmc_corpus}")
    # And one layer further up: what those OSC messages mean. Both layers run
    # over the recorded bytes, so the composition is checked rather than assumed
    # from two green unit suites.
    add_test(NAME vrmAdapterVmc_vmcCorpus
        COMMAND vrmAdapterVmc_vmcMessage_tests "${_vrm_adapter_vmc_corpus}")
    # And the last layer, over the same bytes: every transform the VMC layer
    # decoded reaches canonical semantics, and the basis change is checked
    # against recorded values rather than hand-written ones.
    add_test(NAME vrmAdapterVmc_skeletonMapCorpus
        COMMAND vrmAdapterVmc_skeletonMap_tests "${_vrm_adapter_vmc_corpus}")
    # And the whole stack, over the same bytes: which of those canonical samples
    # belong to one frame. This is the only corpus test whose subject is the
    # *arrangement* of the recorded datagrams rather than their contents, so it
    # is the one the two sender shapes and the restart capture were recorded for.
    add_test(NAME vrmAdapterVmc_frameAssemblerCorpus
        COMMAND vrmAdapterVmc_frameAssembler_tests "${_vrm_adapter_vmc_corpus}")
    # The same bytes once more, all the way into the runtime: every capture
    # replays from datagrams into `LiveCaptureSource`, so what the corpus proves
    # about the decode path it also proves about the hand-off.
    add_test(NAME vrmAdapterVmc_liveSourceCorpus
        COMMAND vrmAdapterVmc_liveSource_tests "${_vrm_adapter_vmc_corpus}")
    # And once more over a real socket: the same bytes sent to a bound port and
    # read back off it, checked to produce the *same poses* the file path
    # produces. That equality is what says the receiver added nothing and lost
    # nothing -- a claim no amount of socket unit testing can make.
    add_test(NAME vrmAdapterVmc_loopbackCorpus
        COMMAND vrmAdapterVmc_udpReceiver_tests "${_vrm_adapter_vmc_corpus}")
    list(APPEND _vrm_adapter_vmc_tests
         vrmAdapterVmc_corpus vrmAdapterVmc_oscCorpus vrmAdapterVmc_vmcCorpus
         vrmAdapterVmc_skeletonMapCorpus vrmAdapterVmc_frameAssemblerCorpus
         vrmAdapterVmc_liveSourceCorpus vrmAdapterVmc_loopbackCorpus)
endif()

if(WIN32)
    if(TARGET gf)
        set(_vrm_adapter_vmc_gf gf)
    else()
        set(_vrm_adapter_vmc_gf pxr::gf)
    endif()
    # The adapter is a static library, but motionCore and motionRuntime call
    # exported Gf entry points, so the test executable loads usd_gf.dll at run
    # time -- which pulls usd_tf.dll -> tbb.dll from bin/ plus python3xx.dll. An
    # activated `ost` session has all three on PATH; a plain-CMake `ctest` run
    # does not, and the test exits 0xC0000135 without them.
    set(_vrm_adapter_vmc_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_adapter_vmc_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_adapter_vmc_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_vrm_adapter_vmc_python_dir
                               "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _vrm_adapter_vmc_test_path
             "PATH=path_list_prepend:${_vrm_adapter_vmc_python_dir}")
    endif()
    set_tests_properties(${_vrm_adapter_vmc_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vrm_adapter_vmc_test_path}")
endif()

if(Python3_Interpreter_FOUND)
    # The *test executable*, not $<TARGET_FILE:vrmAdapterVmc>. The adapter is a
    # static library and an archive records no imports at all, so inspecting it
    # is a check that cannot fail. This executable is the first artifact in
    # which the adapter's real transitive imports exist, and its link closure is
    # exactly the one the boundary is about: the adapter, motionCore, and
    # motionRuntime.
    add_test(NAME vrmAdapterVmc_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:vrmAdapterVmc_tests>")

    # vrmAdapterVmc_corpus proves the committed captures are canonical for the
    # C++ writer; this proves they still match the generator that authored
    # them. Without it a hand-edited fixture would stay green while the
    # generator no longer reproduces it, and the corpus would quietly become
    # unregenerable.
    if(EXISTS "${_vrm_adapter_vmc_corpus}")
        add_test(NAME vrmAdapterVmc_packetGen
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_packets.py"
                --check --output "${_vrm_adapter_vmc_corpus}")
    endif()
endif()
