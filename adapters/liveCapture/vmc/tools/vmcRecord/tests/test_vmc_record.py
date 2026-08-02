#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""`vmc_record` end to end, in the two halves the adapter splits everything into.

**inspect** decodes every committed capture through the tool's own CLI and
compares the report against counts this file states. Those counts are not
independent of the corpus tests -- they are the same numbers, read out through
one more layer -- and that is the point: a report is a claim about a session, so
a report that drifts from what the decoder does is a defect no C++ test sees.

**loopback** sends one capture at a real socket and records it back, then makes
the claim the tool exists for:

    the datagrams that came out of the socket are byte-identical to the ones
    that went in, and the recorded file reports the same motion as the file it
    was replayed from

That is `vrmAdapterVmc_loopbackCorpus`'s claim raised to the CLI: the library
test compares poses, and this one compares the artifact an operator actually
keeps. It is a separate CTest name for the same reason the library's socket
tests are -- a runner that forbids binding excludes it and loses nothing else.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import socket
import subprocess
import sys
import threading
import time

# What each committed capture decodes to, through the report. Derived from the
# corpus the same way every other corpus test's expectations are: from what the
# generator wrote, not from a run of the tool that is being tested.
#
# `datagrams` and `frames` are the two the other corpus tests already pin, so a
# disagreement here is a disagreement with them.
EXPECTED = {
    "arm-raise-30hz": {"datagrams": 117, "frames": 5},
    "extended-forms": {"datagrams": 2, "frames": 1},
    "malformed-forms": {"datagrams": 10, "frames": 2},
    "malformed-packets": {"datagrams": 10, "frames": 0},
    "mixed-traffic-30hz": {"datagrams": 13, "frames": 3},
    "neutral-standing-30hz": {"datagrams": 6, "frames": 5},
    # Six, not five: the restart is admitted as a new session under the default
    # policy, which is the difference `vrmAdapterVmc_liveSourceCorpus` records
    # as six frames against four.
    "sender-restart-30hz": {"datagrams": 10, "frames": 6},
}
# 168 and 22 -- the same totals the corpus tests one layer down are written
# against, which is what makes this a check on the report rather than a second
# opinion about the decoder.
TOTAL_DATAGRAMS = 168
TOTAL_FRAMES = 22

# The report lines a replayed session and a recorded one must agree on: what the
# bytes decoded to. Everything else -- when they arrived, from where, over which
# socket -- is what the wire is allowed to have changed.
MOTION_LABELS = ("decoded", "frames", "cadence", "bones", "clock", "intake",
                 "hips offset", "root")


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_capture(path: pathlib.Path) -> tuple[dict[str, str], list[bytes]]:
    """Header fields and payloads, per datagram and never concatenated."""
    header: dict[str, str] = {}
    payloads: list[bytearray] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or stripped.startswith("!"):
            continue
        tokens = stripped.split()
        if tokens[0] == "d":
            payloads.append(bytearray())
        elif not payloads:
            header[tokens[0]] = " ".join(tokens[1:])
        else:
            payloads[-1] += bytes.fromhex(
                stripped.split("|")[0].replace(" ", ""))
    return header, [bytes(payload) for payload in payloads]


def report_lines(text: str) -> dict[str, str]:
    """The report by label. Continuation lines join the label above them."""
    lines: dict[str, str] = {}
    label = ""
    for line in text.splitlines():
        if line.startswith(" ") and label:
            lines[label] += " " + line.strip()
            continue
        head, separator, rest = line.partition(":")
        if not separator:
            continue
        # `diagnostics` repeats, once per code; keep every occurrence.
        label = head.strip()
        lines[label] = (lines.get(label, "") + " " + rest.strip()).strip()
    return lines


def run_tool(tool: pathlib.Path, *arguments: str) -> str:
    result = subprocess.run([str(tool), *arguments], text=True,
                            encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        fail(f"vmc_record {' '.join(arguments)} exited {result.returncode}\n"
             f"{result.stderr}")
    return result.stdout


def check_inspect(tool: pathlib.Path, corpus: pathlib.Path) -> None:
    seen = 0
    total_datagrams = 0
    total_frames = 0
    for name, expected in sorted(EXPECTED.items()):
        capture = corpus / f"{name}.vmcpackets"
        if not capture.exists():
            fail(f"{capture} is missing")
        lines = report_lines(run_tool(tool, "--inspect", str(capture)))

        received = lines.get("received", "")
        if not received.startswith(f"{expected['datagrams']} datagram(s)"):
            fail(f"{name}: expected {expected['datagrams']} datagrams, "
                 f"report said '{received}'")

        frames = lines.get("frames", "")
        if not frames.startswith(f"{expected['frames']} emitted"):
            fail(f"{name}: expected {expected['frames']} frames, report said "
                 f"'{frames}'")

        if lines.get("stopped") != "end of capture":
            fail(f"{name}: a replayed capture ends at its end, not at "
                 f"'{lines.get('stopped')}'")

        # A replay opened no socket, so the three lines that describe one are
        # absent rather than zeroed.
        if "listen" in lines:
            fail(f"{name}: --inspect reported a bound endpoint it never had")

        # A cadence is a statement about intervals inside one session, so its
        # span can never be negative -- not even on the capture whose second
        # session starts thirty seconds before its first one ended. It was,
        # once: the span had been measured from the first frame to the last.
        cadence = lines.get("cadence", "")
        span = re.search(r"over (\S+) s of sender clock", cadence)
        if span and float(span.group(1)) < 0.0:
            fail(f"{name}: the cadence line spans negative time: '{cadence}'")

        seen += 1
        total_datagrams += expected["datagrams"]
        total_frames += expected["frames"]

    # The whole corpus, so a capture added without a line here fails rather than
    # being silently unchecked.
    committed = sorted(path.stem for path in corpus.glob("*.vmcpackets"))
    if committed != sorted(EXPECTED):
        fail(f"the corpus holds {committed}, this test knows {sorted(EXPECTED)}")
    if (total_datagrams, total_frames) != (TOTAL_DATAGRAMS, TOTAL_FRAMES):
        fail(f"the per-capture counts sum to {total_datagrams} datagrams and "
             f"{total_frames} frames, where the corpus is {TOTAL_DATAGRAMS} "
             f"and {TOTAL_FRAMES}")
    print(f"vmc_record --inspect: {seen} capture(s), {total_datagrams} "
          f"datagram(s), {total_frames} frame(s) reported as expected")


def check_help_and_refusals(tool: pathlib.Path, corpus: pathlib.Path) -> None:
    """The CLI's own contract: --help succeeds, a bad combination does not."""
    usage = run_tool(tool, "--help")
    if "--inspect" not in usage or "vmc_record" not in usage:
        fail("--help did not print the usage")

    for arguments in (
        # No output, no dry run, no inspect: nothing to do and no file to show
        # for it.
        [],
        # --inspect opens no socket, so a flag about one is a mistake -- and it
        # is a mistake whether or not the value differs from the default, which
        # is why the parser tracks the flag and not the parsed setting.
        ["--inspect", str(corpus / "arm-raise-30hz.vmcpackets"),
         "--port", "39539"],
        ["--inspect", str(corpus / "arm-raise-30hz.vmcpackets"),
         "--listen", "0.0.0.0"],
        ["--inspect", str(corpus / "arm-raise-30hz.vmcpackets"),
         "--max-datagrams", "10"],
        ["--inspect", str(corpus / "arm-raise-30hz.vmcpackets"),
         "--output", "unused.vmcpackets"],
        # A dry run writes nothing.
        ["--dry-run", "--output", "unused.vmcpackets"],
        # An unbracketed IPv6 address with a port is ambiguous, not guessed at.
        ["--dry-run", "--listen", "::1:39539"],
        ["--dry-run", "--max-datagrams", "0"],
    ):
        result = subprocess.run([str(tool), *arguments], text=True,
                                encoding="utf-8", errors="replace",
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        if result.returncode != 2:
            fail(f"`vmc_record {' '.join(arguments)}` should have been refused "
                 f"with exit 2, got {result.returncode}")
        if pathlib.Path("unused.vmcpackets").exists():
            fail("a refused invocation wrote a file")
    print("vmc_record: usage and eight refusals behave")


def free_udp_port() -> int:
    """A port nothing holds. UDP has no TIME_WAIT, so rebinding is immediate."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def check_loopback(tool: pathlib.Path, corpus: pathlib.Path) -> None:
    source = corpus / "arm-raise-30hz.vmcpackets"
    _, payloads = read_capture(source)

    output = pathlib.Path("recorded-loopback.vmcpackets").absolute()
    if output.exists():
        output.unlink()

    port = free_udp_port()
    process = subprocess.Popen(
        [str(tool),
         "--listen", "127.0.0.1", "--port", str(port),
         "--output", str(output),
         "--sender", "test.loopback",
         "--source-id", "loopback-01",
         # Stops a second after the last datagram; the duration is the net that
         # catches a sender that never arrives, so the test cannot hang.
         "--idle-timeout", "2.0",
         "--duration", "60"],
        text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # Drained on a thread: the tool writes a progress line a second, and a full
    # stderr pipe would block it mid-session.
    stderr_lines: list[str] = []
    listening = threading.Event()

    def drain() -> None:
        assert process.stderr is not None
        for line in process.stderr:
            stderr_lines.append(line)
            if "listening on" in line:
                listening.set()

    reader = threading.Thread(target=drain, daemon=True)
    reader.start()

    # Nothing is sent until the socket says it is bound. Sending first would
    # lose the datagrams outright -- UDP has nowhere to hold them.
    if not listening.wait(timeout=30.0):
        process.kill()
        fail("vmc_record never reported a bound endpoint")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
        for payload in payloads:
            sender.sendto(payload, ("127.0.0.1", port))
            # The corpus is 117 datagrams inside 170 ms of session; sent flat
            # out they can outrun the receive buffer on a loaded runner, and a
            # dropped datagram would make this test flaky rather than failing.
            time.sleep(0.002)

    try:
        stdout, _ = process.communicate(timeout=60)
    except subprocess.TimeoutExpired:
        process.kill()
        fail("vmc_record did not stop on its idle timeout")
    reader.join(timeout=5.0)
    if process.returncode != 0:
        fail(f"vmc_record exited {process.returncode}\n"
             f"{''.join(stderr_lines)}")

    lines = report_lines(stdout)
    if lines.get("stopped") != "--idle-timeout elapsed with nothing arriving":
        fail(f"expected the idle timeout to stop the session, got "
             f"'{lines.get('stopped')}'")
    if not lines.get("listen", "").startswith(f"127.0.0.1:{port}"):
        fail(f"the report did not name the bound endpoint: "
             f"'{lines.get('listen')}'")

    header, recorded = read_capture(output)
    if recorded != payloads:
        fail(f"the recorded datagrams are not the ones that were sent: "
             f"{len(recorded)} recorded, {len(payloads)} sent")
    if header.get("sender") != "test.loopback":
        fail(f"the capture did not record its provenance: {header}")
    if header.get("listen") != f"127.0.0.1:{port}":
        fail(f"the capture did not record its listen endpoint: {header}")

    # And the claim this test exists for: what arrived over the wire reports the
    # same motion as what was read off the disk.
    live = report_lines(run_tool(tool, "--inspect", str(output)))
    replayed = report_lines(run_tool(tool, "--inspect", str(source)))
    for label in MOTION_LABELS:
        if live.get(label) != replayed.get(label):
            fail(f"a recorded session and its source disagree about {label}:\n"
                 f"  recorded: {live.get(label)}\n"
                 f"  source:   {replayed.get(label)}")

    output.unlink()
    print(f"vmc_record: {len(recorded)} datagram(s) through a real socket, "
          f"recorded verbatim and reporting the same motion")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True, type=pathlib.Path)
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    parser.add_argument("--mode", required=True,
                        choices=("inspect", "loopback"))
    arguments = parser.parse_args()

    if arguments.mode == "inspect":
        check_inspect(arguments.tool, arguments.corpus)
        check_help_and_refusals(arguments.tool, arguments.corpus)
    else:
        check_loopback(arguments.tool, arguments.corpus)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
