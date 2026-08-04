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
import tempfile
import threading
import time

# What each committed capture decodes to, through the report. Derived from the
# corpus the same way every other corpus test's expectations are: from what the
# generator wrote, not from a run of the tool that is being tested.
#
# `datagrams` and `frames` are the two the other corpus tests already pin, so a
# disagreement here is a disagreement with them.
#
# `expressions` is the report line, or None when the session carried no blend
# shapes and the line must therefore be absent: a sender that sends no face is
# not a sender that sent an empty one.
#
# `sessions` is what `--export-trace` must produce: one trace per session, with
# that many frames in each. They sum to `frames`, because a trace carries what
# the adapter delivered and the adapter delivers every frame it emitted -- a
# session boundary changes which file a frame lands in, never whether it is
# there at all.
EXPECTED = {
    "arm-raise-30hz": {"datagrams": 117, "frames": 5, "expressions": None,
                       "sessions": [5]},
    "extended-forms": {"datagrams": 2, "frames": 1, "expressions": None,
                       "sessions": [1]},
    "malformed-forms": {"datagrams": 10, "frames": 2, "expressions": None,
                        "sessions": [2]},
    # Nothing decoded into a frame, so there is no session and no trace: an
    # export here is refused rather than writing an empty recording.
    "malformed-packets": {"datagrams": 10, "frames": 0, "expressions": None,
                          "sessions": []},
    # Three names across three frames, one of them (`A`) sent as 0.0 every
    # time -- so a reader that dropped a zero weight would report two names
    # here. The name list is a continuation line, which `report_lines` joins
    # onto the label above it.
    "mixed-traffic-30hz": {"datagrams": 13, "frames": 3,
                           "expressions": "3 name(s); 9 accepted, "
                                          "0 duplicated A, Blink, Joy",
                           "sessions": [3]},
    "neutral-standing-30hz": {"datagrams": 6, "frames": 5, "expressions": None,
                              "sessions": [5]},
    # Six, not five: the restart is admitted as a new session under the default
    # policy, which is the difference `vrmAdapterVmc_liveSourceCorpus` records
    # as six frames against four.
    #
    # And the only capture that exports two traces. The two sessions' clocks
    # overlap, so a single spliced trace would stall on replay at the
    # discontinuity -- which is why an export from this one has to be told which
    # session it means.
    "sender-restart-30hz": {"datagrams": 10, "frames": 6, "expressions": None,
                            "sessions": [4, 2]},
}
# 168 and 22 -- the same totals the corpus tests one layer down are written
# against, which is what makes this a check on the report rather than a second
# opinion about the decoder.
TOTAL_DATAGRAMS = 168
TOTAL_FRAMES = 22

# The report lines a replayed session and a recorded one must agree on: what the
# bytes decoded to. Everything else -- when they arrived, from where, over which
# socket -- is what the wire is allowed to have changed.
MOTION_LABELS = ("decoded", "frames", "cadence", "bones", "expressions",
                 "clock", "intake", "hips offset", "root")


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

        # Absent, not empty, when the session carried no blend shapes.
        expressions = lines.get("expressions")
        if expected["expressions"] is None:
            if expressions is not None:
                fail(f"{name}: carries no blend shapes, but the report has an "
                     f"expressions line: '{expressions}'")
        elif expressions != expected["expressions"]:
            fail(f"{name}: expected expressions "
                 f"'{expected['expressions']}', report said '{expressions}'")

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


def read_trace(path: pathlib.Path) -> tuple[dict[str, str], int]:
    """The header, and how many frames a trace holds.

    Read here rather than through `motion_capture`, for the same reason the
    capture above is: this test is a check on what the tool wrote, and reading
    it back with the writer's own counterpart would only prove the two agree.
    The format is line-oriented text precisely so that a second reader is four
    lines long.
    """
    header: dict[str, str] = {}
    frames = 0
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#") or stripped.startswith("!"):
            continue
        key, _, rest = stripped.partition(" ")
        if key == "t":
            frames += 1
        elif frames == 0:
            # A header value is the rest of the line: a sender's model title has
            # spaces in it, which is the defect this format was carrying until
            # a real one was written through it.
            header[key] = rest.strip()
    return header, frames


def check_trace_export(tool: pathlib.Path, corpus: pathlib.Path,
                       workspace: pathlib.Path) -> None:
    """The adapter's hand-off: what it delivered, as a canonical trace.

    This is the whole of the boundary WORKSPACE.md §2 keeps -- no tool in the
    product links this adapter, so a live session reaches `motion_capture` as a
    file. What is checked is that the file holds exactly the frames the report
    says were delivered, split into one trace per session.
    """
    exported = 0
    for name, expected in sorted(EXPECTED.items()):
        capture = corpus / f"{name}.vmcpackets"
        sessions = expected["sessions"]
        target = workspace / f"{name}.trace"

        if not sessions:
            # Nothing decoded into a frame. The export is refused with exit 1 --
            # not 2, because the arguments were fine and the recording was not
            # -- and no file appears.
            result = subprocess.run(
                [str(tool), "--inspect", str(capture), "--export-trace",
                 str(target)],
                text=True, encoding="utf-8", errors="replace",
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode != 1:
                fail(f"{name}: an export with no frames should exit 1, got "
                     f"{result.returncode}")
            if target.exists():
                fail(f"{name}: a refused export wrote {target}")
            continue

        if len(sessions) > 1:
            # More than one session and nobody said which: refused, because the
            # alternatives are discarding a recording or splicing two clocks.
            result = subprocess.run(
                [str(tool), "--inspect", str(capture), "--export-trace",
                 str(target)],
                text=True, encoding="utf-8", errors="replace",
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode != 1:
                fail(f"{name}: {len(sessions)} sessions and no "
                     f"--sender-session should exit 1, got "
                     f"{result.returncode}")
            if target.exists():
                fail(f"{name}: a refused export wrote {target}")
            if "--sender-session" not in result.stderr:
                fail(f"{name}: the refusal did not name the flag that resolves "
                     f"it: {result.stderr}")

        for index, frames in enumerate(sessions, start=1):
            path = workspace / f"{name}.{index}.trace"
            arguments = ["--inspect", str(capture), "--export-trace", str(path)]
            if len(sessions) > 1:
                arguments += ["--sender-session", str(index)]
            run_tool(tool, *arguments)

            header, written = read_trace(path)
            if written != frames:
                fail(f"{name} session {index}: expected {frames} frame(s) in "
                     f"the trace, found {written}")
            if header.get("protocol") != "vmc":
                fail(f"{name} session {index}: the trace does not record what "
                     f"produced it: {header}")
            exported += 1

        # A session past the last one is refused rather than clamped.
        result = subprocess.run(
            [str(tool), "--inspect", str(capture), "--export-trace",
             str(workspace / f"{name}.over.trace"), "--sender-session",
             str(len(sessions) + 1)],
            text=True, encoding="utf-8", errors="replace",
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode != 1:
            fail(f"{name}: --sender-session past the end should exit 1, got "
                 f"{result.returncode}")

    # The capture that carries a model title, checked by value: "Example Avatar"
    # has a space in it, and a trace whose header lost half of it would still
    # parse. That is the failure the format was carrying before a real sender's
    # provenance was written through it.
    header, _ = read_trace(workspace / "arm-raise-30hz.1.trace")
    if header.get("sourceId") != "Example Avatar":
        fail(f"the exported trace did not carry the sender's model title "
             f"whole: {header.get('sourceId')!r}")

    print(f"vmc_record --export-trace: {exported} session(s) written as "
          f"canonical traces")


def check_help_and_refusals(tool: pathlib.Path, corpus: pathlib.Path,
                            workspace: pathlib.Path) -> None:
    """The CLI's own contract: --help succeeds, a bad combination does not."""
    usage = run_tool(tool, "--help")
    if "--inspect" not in usage or "vmc_record" not in usage:
        fail("--help did not print the usage")

    # Named inside the caller's temporary directory rather than the working one:
    # a refused invocation must write nothing, and checking that against a path
    # in the build tree would pass on a stale file from an earlier run.
    unused = workspace / "unused.vmcpackets"
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
         "--output", str(unused)],
        # A dry run writes nothing.
        ["--dry-run", "--output", str(unused)],
        # An unbracketed IPv6 address with a port is ambiguous, not guessed at.
        ["--dry-run", "--listen", "::1:39539"],
        ["--dry-run", "--max-datagrams", "0"],
        # --sender-session says which session to export, so it needs
        # something to export; and a dry run writes nothing, trace included.
        ["--inspect", str(corpus / "arm-raise-30hz.vmcpackets"), "--sender-session",
         "1"],
        ["--dry-run", "--export-trace", str(unused)],
        ["--inspect", str(corpus / "arm-raise-30hz.vmcpackets"),
         "--export-trace", str(unused), "--sender-session", "0"],
    ):
        result = subprocess.run([str(tool), *arguments], text=True,
                                encoding="utf-8", errors="replace",
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        if result.returncode != 2:
            fail(f"`vmc_record {' '.join(arguments)}` should have been refused "
                 f"with exit 2, got {result.returncode}")
        if unused.exists():
            fail("a refused invocation wrote a file")
    print("vmc_record: usage and eleven refusals behave")


class Session:
    """One `vmc_record` run against a real socket, driven from this process.

    Every live check needs the same four things -- a port nothing holds, a
    started tool, datagrams delivered only after it says it is bound, and a
    process that stops on its own -- so they are here once. What varies is the
    arguments and what is sent, which is what each check is actually about.
    """

    def __init__(self, tool: pathlib.Path, output: pathlib.Path | None,
                 *extra: str) -> None:
        self.tool = tool
        self.output = output
        self.extra = extra
        self.port = 0
        self.stdout = ""
        self.stderr: list[str] = []

    def _spawn(self, port: int) -> subprocess.Popen:
        arguments = [str(self.tool), "--listen", "127.0.0.1", "--port",
                     str(port)]
        if self.output is not None:
            arguments += ["--output", str(self.output)]
        else:
            arguments.append("--dry-run")
        return subprocess.Popen([*arguments, *self.extra],
                                text=True, encoding="utf-8", errors="replace",
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def start(self) -> "Session":
        """Binds, retrying on a port taken between the probe and the bind.

        `free_udp_port` closes its probe before the tool binds, so the port can
        be claimed in between. That is unlikely and it is not impossible, and a
        test that fails once a month for a reason it cannot explain is worse
        than one that retries.
        """
        for attempt in range(3):
            port = free_udp_port()
            process = self._spawn(port)
            listening = threading.Event()

            def drain(process: subprocess.Popen = process) -> None:
                assert process.stderr is not None
                for line in process.stderr:
                    self.stderr.append(line)
                    if "listening on" in line:
                        listening.set()

            # Drained on a thread: the tool writes a progress line a second, and
            # a full stderr pipe would block it mid-session.
            self._reader = threading.Thread(target=drain, daemon=True)
            self._reader.start()

            # Nothing is sent until the socket says it is bound. Sending first
            # would lose the datagrams outright -- UDP has nowhere to hold them.
            if listening.wait(timeout=30.0):
                self.port = port
                self._process = process
                return self

            process.kill()
            process.wait(timeout=10.0)
            if attempt == 2:
                fail(f"vmc_record never bound a port\n{''.join(self.stderr)}")
            self.stderr.clear()
        raise AssertionError("unreachable")

    def send(self, payloads: list, *, source_port: int = 0) -> "Session":
        """Delivers datagrams, from an optionally distinct source port."""
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            if source_port:
                sender.bind(("127.0.0.1", source_port))
            for payload in payloads:
                sender.sendto(payload, ("127.0.0.1", self.port))
                # 117 datagrams inside 170 ms of session, sent flat out, can
                # outrun the receive buffer on a loaded runner -- and a dropped
                # datagram would make this flaky rather than failing.
                time.sleep(0.002)
        return self

    def finish(self, expected_returncode: int = 0) -> dict:
        """Waits for the session to end, and collects both streams.

        `expected_returncode` is not always 0: a session whose capture was
        written and whose trace export was refused exits 1, and that pair is a
        claim worth making rather than a failure to tolerate.

        Deliberately not `communicate()`: the drain thread above is already
        reading stderr, and `communicate()` reads and closes it too. The two
        race, and the loser is whichever lines are near EOF -- which is exactly
        where the end-of-session warnings are. That cost a green test claiming
        a warning had not been printed when it had.

        The watchdog replaces `communicate(timeout=)`, which is the only thing
        that call was still buying: a hung tool must fail this test, not park
        the runner on a blocking read.
        """
        watchdog = threading.Timer(90.0, self._process.kill)
        watchdog.start()
        try:
            assert self._process.stdout is not None
            self.stdout = self._process.stdout.read()
            self._process.wait()
        finally:
            watchdog.cancel()
        # Only after stderr has reached EOF is `self.stderr` the whole session.
        self._reader.join(timeout=10.0)
        if self._process.returncode != expected_returncode:
            fail(f"vmc_record exited {self._process.returncode}, expected "
                 f"{expected_returncode}\n{''.join(self.stderr)}")
        return report_lines(self.stdout)


def free_udp_port() -> int:
    """A port nothing holds. UDP has no TIME_WAIT, so rebinding is immediate."""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def check_loopback(tool: pathlib.Path, corpus: pathlib.Path,
                   workspace: pathlib.Path) -> None:
    """What went in came out, and reports the same motion as its source."""
    source = corpus / "arm-raise-30hz.vmcpackets"
    _, payloads = read_capture(source)
    output = workspace / "recorded-loopback.vmcpackets"
    live_trace = workspace / "recorded-loopback.trace"

    session = Session(tool, output,
                      "--sender", "test.loopback",
                      "--source-id", "loopback-01",
                      # Exported from the live loop, which is the half of
                      # --export-trace no other test reaches: `--inspect` runs a
                      # different function over a file that is already whole.
                      "--export-trace", str(live_trace),
                      # Stops a couple of seconds after the last datagram; the
                      # duration is the net that catches a sender that never
                      # arrives, so the test cannot hang.
                      "--idle-timeout", "2.0",
                      "--duration", "60").start()
    session.send(payloads)
    lines = session.finish()

    if lines.get("stopped") != "--idle-timeout elapsed with nothing arriving":
        fail(f"expected the idle timeout to stop the session, got "
             f"'{lines.get('stopped')}'")
    if not lines.get("listen", "").startswith(f"127.0.0.1:{session.port}"):
        fail(f"the report did not name the bound endpoint: "
             f"'{lines.get('listen')}'")

    header, recorded = read_capture(output)
    if recorded != payloads:
        fail(f"the recorded datagrams are not the ones that were sent: "
             f"{len(recorded)} recorded, {len(payloads)} sent")
    if header.get("sender") != "test.loopback":
        fail(f"the capture did not record its provenance: {header}")
    if header.get("listen") != f"127.0.0.1:{session.port}":
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

    # And the same claim about the *canonical* artifact, which is the one the
    # product consumes. A trace written from the live loop and one written from
    # the file the loop recorded must be byte-identical: the frames carry the
    # sender's own clock, so nothing in this file is a fact about the wire, and
    # a difference would mean the two code paths disagree about what the adapter
    # delivered. The exported header names the sender's model title, not the
    # capture's operator-supplied `--source-id`, so the recording tool's
    # provenance cannot leak into the canonical one.
    replayed_trace = workspace / "replayed-from-file.trace"
    run_tool(tool, "--inspect", str(output), "--export-trace",
             str(replayed_trace))
    if live_trace.read_bytes() != replayed_trace.read_bytes():
        fail("the trace exported from the live loop differs from the one "
             "exported from the capture it wrote")
    header, frames = read_trace(live_trace)
    if frames != EXPECTED["arm-raise-30hz"]["sessions"][0]:
        fail(f"the live export holds {frames} frame(s), expected "
             f"{EXPECTED['arm-raise-30hz']['sessions'][0]}")
    if header.get("sourceId") != "Example Avatar":
        fail(f"the live export did not carry the sender's model title: "
             f"{header.get('sourceId')!r}")

    print(f"vmc_record: {len(recorded)} datagram(s) through a real socket, "
          f"recorded verbatim, reporting the same motion and exporting the "
          f"same trace")


def check_stop_reasons(tool: pathlib.Path, corpus: pathlib.Path,
                       workspace: pathlib.Path) -> None:
    """Every session ends for exactly one stated reason, and says which.

    The report makes that claim, so each reason a test can reach is reached.
    Ctrl-C is the one that is not here: delivering a console interrupt to a
    child without also killing the test runner needs a process group on POSIX
    and a detached console on Windows, and the two share no mechanism.
    """
    _, payloads = read_capture(corpus / "arm-raise-30hz.vmcpackets")

    # --max-datagrams stops the session mid-stream, and what is written is
    # exactly the datagrams that arrived before it did: not the whole send, and
    # not a truncated last record.
    bounded = workspace / "bounded.vmcpackets"
    session = Session(tool, bounded, "--max-datagrams", "40",
                      "--idle-timeout", "10", "--duration", "60").start()
    session.send(payloads)
    lines = session.finish()
    if lines.get("stopped") != "--max-datagrams reached":
        fail(f"expected --max-datagrams to stop the session, got "
             f"'{lines.get('stopped')}'")
    _, bounded_payloads = read_capture(bounded)
    if bounded_payloads != payloads[:40]:
        fail(f"a bounded session recorded {len(bounded_payloads)} datagram(s), "
             f"expected the first 40 that arrived")

    # --max-frames stops on the second thing a session accumulates. It exists
    # because a datagram bound cannot stand in for a pose bound -- a bundled
    # sender emits one frame per datagram and a per-message one takes about
    # fifty -- so a session has to be able to end on either.
    #
    # Driven by the bundled capture, where a frame arrives whole in one
    # datagram. The per-message one is used below, for the opposite reason.
    _, bundled = read_capture(corpus / "neutral-standing-30hz.vmcpackets")
    bounded_frames = workspace / "bounded-frames.vmcpackets"
    bounded_trace = workspace / "bounded-frames.trace"
    session = Session(tool, bounded_frames, "--max-frames", "2",
                      "--export-trace", str(bounded_trace),
                      "--idle-timeout", "10", "--duration", "60").start()
    session.send(bundled)
    lines = session.finish()
    if lines.get("stopped") != "--max-frames reached":
        fail(f"expected --max-frames to stop the session, got "
             f"'{lines.get('stopped')}'")
    _, bounded_exported = read_trace(bounded_trace)
    # A bound is a stop condition, not a truncation: the frame the loop was in
    # the middle of is still flushed. So the trace holds at least the two the
    # bound stopped on, and fewer than the five the whole capture carries.
    if not 2 <= bounded_exported < EXPECTED["neutral-standing-30hz"]["frames"]:
        fail(f"a session bounded at 2 frames exported {bounded_exported}, "
             f"expected at least 2 and fewer than the capture's "
             f"{EXPECTED['neutral-standing-30hz']['frames']}")

    # A recording stopped in the *middle* of a frame, which only a per-message
    # sender can be. `/VMC/Ext/T` ends a frame, so the half-assembled one the
    # flush delivers has no sender clock and is stamped from the receive clock
    # instead -- an origin some twenty seconds from the sender's here, which the
    # assembler reads as a restart because it compares the two as one number.
    #
    # LiveSource.h says that switch is a phenomenon "no capture records a sender
    # doing", and it is right about senders. This tool's own stop conditions
    # reach the same shape from the other side, so the export refuses a
    # two-session recording exactly as it does for a real restart. Pinned here
    # rather than smoothed over: an operator who bounds a per-message session
    # will meet it, and a refusal that named no reason would be a mystery.
    truncated = workspace / "truncated-mid-frame.vmcpackets"
    truncated_trace = workspace / "truncated-mid-frame.trace"
    session = Session(tool, truncated, "--max-frames", "2",
                      "--export-trace", str(truncated_trace),
                      "--idle-timeout", "10", "--duration", "60")
    session.start().send(payloads)
    # Exit 1, not 0: the capture was written and the trace was refused.
    lines = session.finish(expected_returncode=1)
    if lines.get("stopped") != "--max-frames reached":
        fail(f"expected --max-frames to stop the session, got "
             f"'{lines.get('stopped')}'")
    if not truncated.exists():
        fail("the capture was not written when only the trace was refused")
    if truncated_trace.exists():
        fail("a refused export wrote a trace")
    if not any("--sender-session" in line for line in session.stderr):
        fail(f"the refusal did not name the flag that resolves it: "
             f"{''.join(session.stderr)}")

    # --duration against a sender that never says anything: the timer runs from
    # the bind rather than from the first datagram, so a session nobody sends to
    # still ends.
    lines = Session(tool, None, "--duration", "1").start().finish()
    if lines.get("stopped") != "--duration elapsed":
        fail(f"expected --duration to stop a silent session, got "
             f"'{lines.get('stopped')}'")

    # Two peers: the capture header names one, so the report has to say there
    # were more. A fixture whose provenance is true of some of its datagrams and
    # not the rest is the failure that warning exists for.
    mixed = workspace / "two-peers.vmcpackets"
    session = Session(tool, mixed, "--idle-timeout", "2",
                      "--duration", "60").start()
    session.send(payloads[:20], source_port=free_udp_port())
    session.send(payloads[20:40], source_port=free_udp_port())
    lines = session.finish()
    if not lines.get("peers", "").startswith("2 ("):
        fail(f"expected two peers to be reported, got '{lines.get('peers')}'")
    if not any("more than one peer" in line for line in session.stderr):
        fail("a two-peer session did not warn that its header names one")

    print("vmc_record: four stop reasons and the two-peer warning behave")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True, type=pathlib.Path)
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    parser.add_argument("--mode", required=True,
                        choices=("inspect", "loopback"))
    arguments = parser.parse_args()

    # Every file this test writes lands here and leaves with it, including the
    # ones a failing run abandons. CTest's working directory is the build tree,
    # which is shared with every other test and outlives all of them.
    with tempfile.TemporaryDirectory(prefix="vmc_record-") as scratch:
        workspace = pathlib.Path(scratch)
        if arguments.mode == "inspect":
            check_inspect(arguments.tool, arguments.corpus)
            check_trace_export(arguments.tool, arguments.corpus, workspace)
            check_help_and_refusals(arguments.tool, arguments.corpus, workspace)
        else:
            check_loopback(arguments.tool, arguments.corpus, workspace)
            check_stop_reasons(arguments.tool, arguments.corpus, workspace)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
