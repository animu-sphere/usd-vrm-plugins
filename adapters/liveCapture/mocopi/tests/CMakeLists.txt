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

set(_vrm_adapter_mocopi_tests
    vrmAdapterMocopi_unit vrmAdapterMocopi_packetCapture
    vrmAdapterMocopi_udpReceiver vrmAdapterMocopi_udpReceiverTruncation)

# Every committed capture parses and re-emits byte-identically, or every
# decoder test downstream of it compares against a fixture that has drifted from
# the writer. The same binary takes the corpus directory as an argument rather
# than referencing a target from another directory, so a standalone configure of
# this adapter still generates.
#
# There is no corpus yet and this block is guarded rather than absent, which is
# the deliberate half: the decoder cannot be written from a format nobody here
# has measured, so the first recorded session arrives before the first decoder
# does -- and when it does it becomes a checked fixture by being committed,
# rather than by somebody also remembering to add a test.
#
# What has changed since that was written is that the recording is now possible
# end to end: the receiver below turns a source aimed at this port into
# datagrams, and `tools/mocopiRecord` writes them to one of these files --
# `mocopi_record_loopback` checks that what comes off the socket is byte for byte
# what the capture keeps. What is still missing is a source to aim, the vendor's
# `BVH Sender` or a device, which is an operator's action and not a commit's.
set(_vrm_adapter_mocopi_corpus "${CMAKE_CURRENT_SOURCE_DIR}/corpus")
if(EXISTS "${_vrm_adapter_mocopi_corpus}")
    add_test(NAME vrmAdapterMocopi_corpus
        COMMAND vrmAdapterMocopi_packetCapture_tests
                "${_vrm_adapter_mocopi_corpus}")
    list(APPEND _vrm_adapter_mocopi_tests vrmAdapterMocopi_corpus)
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
endif()
