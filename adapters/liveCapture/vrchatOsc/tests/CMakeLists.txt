# SPDX-License-Identifier: Apache-2.0

# NDEBUG compiles `assert()` away, and the root CMakeLists defaults
# single-config builds to Release -- so a suite built the normal way would print
# "passed" and return 0 without having checked anything. Undefine it for the test
# targets only: the library under test stays Release, the checks stay live.
set(_vrm_adapter_vrchat_osc_assertions
    $<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>)

add_executable(vrmAdapterVrchatOsc_tests test_vrm_adapter_vrchat_osc.cpp)
target_link_libraries(vrmAdapterVrchatOsc_tests
    PRIVATE vrmAdapterVrchatOsc::vrmAdapterVrchatOsc)
target_compile_options(vrmAdapterVrchatOsc_tests
    PRIVATE ${_vrm_adapter_vrchat_osc_assertions})
add_test(NAME vrmAdapterVrchatOsc_unit COMMAND vrmAdapterVrchatOsc_tests)

# The recorded packet format as this adapter sees it -- which is the magic, and
# the four functions that carry it. The reader and the writer have their own
# tests in liveTransport; what is checked here is the type tag, and the case that
# earns the binary is a sibling's capture being refused at line 1.
add_executable(vrmAdapterVrchatOsc_packetCapture_tests test_packet_capture.cpp)
target_link_libraries(vrmAdapterVrchatOsc_packetCapture_tests
    PRIVATE vrmAdapterVrchatOsc::vrmAdapterVrchatOsc)
target_compile_options(vrmAdapterVrchatOsc_packetCapture_tests
    PRIVATE ${_vrm_adapter_vrchat_osc_assertions})
add_test(NAME vrmAdapterVrchatOsc_packetCapture
         COMMAND vrmAdapterVrchatOsc_packetCapture_tests)

# The address inventory: what a session contains, counted from bytes. The first
# suite here that decodes anything, and the second consumer `libs/osc` was
# extracted for (osc-and-vrchat-trackers.md §3.1). It opens no socket and reads
# no file -- every datagram it inventories is built byte by byte in the test,
# because a fixture written by this repository's own encoder would let the
# inventory agree with itself.
add_executable(vrmAdapterVrchatOsc_addressInventory_tests
    test_address_inventory.cpp)
target_link_libraries(vrmAdapterVrchatOsc_addressInventory_tests
    PRIVATE vrmAdapterVrchatOsc::vrmAdapterVrchatOsc)
target_compile_options(vrmAdapterVrchatOsc_addressInventory_tests
    PRIVATE ${_vrm_adapter_vrchat_osc_assertions})
add_test(NAME vrmAdapterVrchatOsc_addressInventory
         COMMAND vrmAdapterVrchatOsc_addressInventory_tests)

# The tracker layer: what an address means. The first suite here that gives a
# byte a meaning, and the one whose first test is the milestone — an identity
# type that holds `1` and `head`, because they occupy the same path position and
# a decoder reading that segment as an integer drops the head silently.
#
# It opens no socket and reads no file in its unit mode; every datagram is built
# byte by byte, so a fixture written by this repository's own encoder cannot let
# the decoder agree with itself.
add_executable(vrmAdapterVrchatOsc_trackerMessage_tests
    test_tracker_message.cpp)
target_link_libraries(vrmAdapterVrchatOsc_trackerMessage_tests
    PRIVATE vrmAdapterVrchatOsc::vrmAdapterVrchatOsc)
target_compile_options(vrmAdapterVrchatOsc_trackerMessage_tests
    PRIVATE ${_vrm_adapter_vrchat_osc_assertions})
add_test(NAME vrmAdapterVrchatOsc_trackerMessage
         COMMAND vrmAdapterVrchatOsc_trackerMessage_tests)

# The basis: which way this sender's numbers point (VRC-3). Its constants are
# readings of the 2026-08-30 session and its rotation checks are physical — a
# direction is rotated and compared against where a body that did that has to
# end up — because a component-against-component check agrees with a mirrored
# conversion in every axis-aligned pose, which is the failure this milestone
# exists to avoid.
#
# It is the first binary here that links OpenUSD's value-type layer, which is
# what the PATH modification further down is for.
add_executable(vrmAdapterVrchatOsc_trackingSpace_tests test_tracking_space.cpp)
target_link_libraries(vrmAdapterVrchatOsc_trackingSpace_tests
    PRIVATE vrmAdapterVrchatOsc::vrmAdapterVrchatOsc)
target_compile_options(vrmAdapterVrchatOsc_trackingSpace_tests
    PRIVATE ${_vrm_adapter_vrchat_osc_assertions})
add_test(NAME vrmAdapterVrchatOsc_trackingSpace
         COMMAND vrmAdapterVrchatOsc_trackingSpace_tests)

# The frame: which observations belong together, and what an absence means
# (VRC-4). Seven policies, each with a case and each with a fixture that
# produces it -- and the two that could not be tested from a file at all
# before 2026-08-30, because the only restart marker this wire has is a
# source port and a capture could not carry one.
add_executable(vrmAdapterVrchatOsc_frameAssembler test_frame_assembler.cpp)
target_link_libraries(vrmAdapterVrchatOsc_frameAssembler
    PRIVATE vrmAdapterVrchatOsc::vrmAdapterVrchatOsc)
target_compile_options(vrmAdapterVrchatOsc_frameAssembler
    PRIVATE ${_vrm_adapter_vrchat_osc_assertions})
add_test(NAME vrmAdapterVrchatOsc_frameAssembler
         COMMAND vrmAdapterVrchatOsc_frameAssembler)

# The socket, on its own CTest name, so a runner that forbids sockets excludes a
# name rather than a claim (adapter plan §9.5). This binary carries **three**
# such names: the two below and `vrmAdapterVrchatOsc_loopbackCorpus` further
# down, which binds a loopback socket of its own. A lane that forbids binds must
# exclude all three. It never touches port 9000: every bind here is loopback on
# an OS-assigned port, so a developer with VRChat running does not lose its port
# to the test suite.
add_executable(vrmAdapterVrchatOsc_udpReceiver_tests test_udp_receiver.cpp)
target_link_libraries(vrmAdapterVrchatOsc_udpReceiver_tests
    PRIVATE vrmAdapterVrchatOsc::vrmAdapterVrchatOsc)
target_compile_options(vrmAdapterVrchatOsc_udpReceiver_tests
    PRIVATE ${_vrm_adapter_vrchat_osc_assertions})
add_test(NAME vrmAdapterVrchatOsc_udpReceiver
         COMMAND vrmAdapterVrchatOsc_udpReceiver_tests)

# The over-long datagram case, on its own name so that "could not check this
# here" is a word in the ctest summary rather than a line nobody sees.
#
# It needs IPv6 to reach at all -- that is the only transport carrying more than
# the IPv4 payload maximum the capture format bounds itself by -- and a runner
# without IPv6 loopback cannot run it. With SKIP_RETURN_CODE the summary says
# `Skipped`, so "was this actually verified on POSIX" is answered by reading a
# log rather than by finding a machine. Both siblings carry the same pair of
# names, and macOS arm64 has skipped theirs since v0.7.0.
add_test(NAME vrmAdapterVrchatOsc_udpReceiverTruncation
         COMMAND vrmAdapterVrchatOsc_udpReceiver_tests truncation)
set_tests_properties(vrmAdapterVrchatOsc_udpReceiverTruncation PROPERTIES
    SKIP_RETURN_CODE 77)

# The corpus passes, registered only when a capture exists rather than when the
# directory does.
#
# The siblings guard on the directory, because theirs were created with fixtures
# in them. This corpus was created **empty** at VRC-0, with a README and no
# bytes, because the generated half needed a decoder to define what a shape is
# and the recorded half needed a device. **The generated half arrived with
# VRC-2** and these names came with it, which is what the glob is for: they
# appear on the commit that adds the first capture rather than being registered
# in advance and failing with "no fixtures" while a milestone is still open.
#
# The recorded half is still bytes-free and will stay that way — a tracker
# stream is a real person's motion in a real room — so everything below reads
# `generated/` in practice while naming the corpus root, and a redistributable
# session would be picked up without a CMake edit.
file(GLOB _vrm_adapter_vrchat_osc_captures CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/corpus/*.vrchatoscpackets"
     "${CMAKE_CURRENT_SOURCE_DIR}/corpus/*/*.vrchatoscpackets"
     "${CMAKE_CURRENT_SOURCE_DIR}/corpus/*/*/*.vrchatoscpackets")
set(_vrm_adapter_vrchat_osc_tests
    vrmAdapterVrchatOsc_unit vrmAdapterVrchatOsc_packetCapture
    vrmAdapterVrchatOsc_addressInventory vrmAdapterVrchatOsc_trackerMessage
    vrmAdapterVrchatOsc_trackingSpace vrmAdapterVrchatOsc_frameAssembler
    vrmAdapterVrchatOsc_udpReceiver vrmAdapterVrchatOsc_udpReceiverTruncation)

if(_vrm_adapter_vrchat_osc_captures)
    set(_vrm_adapter_vrchat_osc_corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
    # The format's reading: every committed capture parses and re-emits
    # byte-identically, so a fixture can never drift from the C++ writer.
    add_test(NAME vrmAdapterVrchatOsc_corpus
        COMMAND vrmAdapterVrchatOsc_packetCapture_tests
                "${_vrm_adapter_vrchat_osc_corpus}")
    # The same bytes, one layer up: what those datagrams mean. Its counts come
    # from the generator's structure rather than from a run, so a fixture that
    # changed shape fails here as well as in the round-trip check -- and a
    # capture with no expectation in that test fails too, which is what stops a
    # fixture being added to the corpus and decoded by nobody.
    add_test(NAME vrmAdapterVrchatOsc_trackerCorpus
        COMMAND vrmAdapterVrchatOsc_trackerMessage_tests
                "${_vrm_adapter_vrchat_osc_corpus}")
    # And the socket's reading of the same directory: every capture sent through
    # a real loopback socket comes back byte-identical and in order. The
    # siblings' equivalent pass compares poses, because they have decoders; this
    # one compares bytes, which is still the right claim for *this* name -- the
    # receiver's job is to add and lose nothing, and bytes are the strongest
    # statement of that.
    #
    # It **binds a socket**, so it belongs to the excluded set with the two
    # `_udpReceiver` names rather than beside the corpus name it is listed with.
    add_test(NAME vrmAdapterVrchatOsc_loopbackCorpus
        COMMAND vrmAdapterVrchatOsc_udpReceiver_tests
                "${_vrm_adapter_vrchat_osc_corpus}")
    # And the basis's reading of the same directory, which asserts one thing:
    # every message the decoder accepts, the conversion converts. It carries no
    # expected canonical values per capture and it deliberately cannot — the
    # generated corpus's numbers are this repository's own invention, so a
    # fixture can show that the conversion runs and only a labelled session can
    # show that it is right. What it does refuse is a conversion that returned
    # identity and zero for everything, which every loop in it would otherwise
    # pass.
    add_test(NAME vrmAdapterVrchatOsc_trackingSpaceCorpus
        COMMAND vrmAdapterVrchatOsc_trackingSpace_tests
                "${_vrm_adapter_vrchat_osc_corpus}")
    # And the frame layer's reading of the same directory: every capture is
    # assembled and its frame count, partial count and session boundaries are
    # compared against numbers derived from the generator's structure. A
    # capture with no expectation in that test fails it, which is what stops a
    # fixture being added to the corpus and assembled by nobody.
    add_test(NAME vrmAdapterVrchatOsc_frameCorpus
        COMMAND vrmAdapterVrchatOsc_frameAssembler
                "${_vrm_adapter_vrchat_osc_corpus}")
    list(APPEND _vrm_adapter_vrchat_osc_tests
         vrmAdapterVrchatOsc_corpus vrmAdapterVrchatOsc_trackerCorpus
         vrmAdapterVrchatOsc_trackingSpaceCorpus
         vrmAdapterVrchatOsc_frameCorpus
         vrmAdapterVrchatOsc_loopbackCorpus)
endif()

# OpenUSD's DLL directory, on PATH for every name above. **This block is what
# VRC-0 and VRC-2 both predicted and neither produced.** Its two previous
# versions recorded that nothing in these binaries loaded an OpenUSD library at
# all, because `liveTransport` opens a socket and `osc` links nothing; VRC-3
# links motionCore for the value types its conversion returns, so the test
# executables now call exported Gf entry points and load usd_gf.dll -- which
# pulls usd_tf.dll -> tbb.dll from bin/ plus python3xx.dll. An activated `ost`
# session has all three on PATH; a plain-CMake `ctest` run does not, and the
# test exits 0xC0000135 without them.
#
# It is applied to every test name rather than to the one binary that links Gf,
# for the reason both siblings do it that way: the set is what a lane runs, and
# a per-binary list is one new suite away from being wrong in a way that shows
# up as a launch failure rather than as a missing entry.
if(WIN32)
    if(TARGET gf)
        set(_vrm_adapter_vrchat_osc_gf gf)
    else()
        set(_vrm_adapter_vrchat_osc_gf pxr::gf)
    endif()
    set(_vrm_adapter_vrchat_osc_test_path
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_adapter_vrchat_osc_gf}>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:${_vrm_adapter_vrchat_osc_gf}>/../bin")
    if(Python3_EXECUTABLE)
        get_filename_component(_vrm_adapter_vrchat_osc_python_dir
                               "${Python3_EXECUTABLE}" DIRECTORY)
        list(APPEND _vrm_adapter_vrchat_osc_test_path
             "PATH=path_list_prepend:${_vrm_adapter_vrchat_osc_python_dir}")
    endif()
    set_tests_properties(${_vrm_adapter_vrchat_osc_tests} PROPERTIES
        ENVIRONMENT_MODIFICATION "${_vrm_adapter_vrchat_osc_test_path}")
endif()

if(Python3_Interpreter_FOUND)
    # A *test executable*, not $<TARGET_FILE:vrmAdapterVrchatOsc>. The adapter
    # is a static library and an archive records no imports at all, so inspecting
    # it would be a check that cannot fail.
    #
    # **Which executable is load-bearing, and VRC-3 is what proved it.** A static
    # archive contributes only the objects a binary references, so an exe's
    # imports are the closure of the *code it calls* rather than of the library.
    # `vrmAdapterVrchatOsc_tests` names nothing in `TrackingSpace.cpp`, so the
    # linker drops that object and the exe imports no OpenUSD at all --  measured
    # with `dumpbin /dependents`, after this file had already been changed to say
    # that the closure had grown. The check would have gone on passing whatever
    # `TrackingSpace.cpp` reached for.
    #
    # So the binary named here is the one that links the widest layer the adapter
    # touches, which is the conversion's suite. That is a rule and not a
    # preference: **a new file reaching a new layer must be reachable from this
    # binary, or the gate silently stops covering it.** The siblings do not have
    # this problem yet, because their unit suites call the code that links
    # motionCore; the day one of them grows a layer its main suite does not
    # reach, they inherit the same rule.
    #
    # `Python3_Interpreter_FOUND` is trustworthy *here* and is not at the root:
    # the adapter's own CMakeLists re-runs find_package(Python3 Interpreter) in
    # this scope. A test guarded on it in the wrong scope does not fail, it
    # silently never registers -- so if this entry ever disappears from
    # `ctest -N`, that is the first thing to check.
    add_test(NAME vrmAdapterVrchatOsc_boundaries
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/check_boundaries.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/.."
            "$<TARGET_FILE:vrmAdapterVrchatOsc_trackingSpace_tests>")

    # vrmAdapterVrchatOsc_corpus proves the committed captures are canonical for
    # the C++ writer; this proves they still match the generator that authored
    # them. Without it a hand-edited fixture would stay green while the generator
    # no longer reproduces it, and the corpus would quietly become
    # unregenerable. It also re-derives every measured field of the generated
    # half's manifest, so the manifest cannot stop describing the bytes.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/corpus/generated/manifest.json")
        add_test(NAME vrmAdapterVrchatOsc_packetGen
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/../tools/generate_packets.py"
                --check --output
                "${CMAKE_CURRENT_SOURCE_DIR}/corpus/generated")
    endif()
endif()
