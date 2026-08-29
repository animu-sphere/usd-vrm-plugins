#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""`vrchat_osc_record`, from the outside.

Three modes, split the way every other claim in this adapter is split: what can
be checked without a socket, what needs one, and what needs a socket this runner
may not have.

**Every byte in this file is authored here, in Python, and none of it is claimed
to be a packet.** That restraint is the same one the C++ suites keep and it costs
more here, because this protocol's receiving end is published: a plausible OSC
message could be written from the specification, and the report would look far
more impressive with `/tracking/trackers/1/position` in its prefix line. It would
also be this repository's assumption about a sender, printed as though it were
evidence, in a test whose whole subject is a tool that must not have opinions
about payloads (osc-and-vrchat-trackers.md §6).

What *is* checked is the claim VRC-0 is asked for: the bytes a sender put on the
wire are the bytes in the capture file, in the order they arrived. The capture is
written by the tool and read back here by an independent parser, so a writer and
a reader that agreed with each other and with nothing else would fail this.
"""

from __future__ import annotations

import argparse
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

# CTest's convention for "this could not be checked here".
SKIP_EXIT_CODE = 77

MAGIC = "!vrchat-osc-packet-capture 1"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def gutter(payload: bytes) -> str:
    """The format's ASCII gutter: printable ASCII as itself, the rest as '.'."""
    return "".join(chr(b) if 0x20 <= b <= 0x7e else "." for b in payload)


def write_capture(path: pathlib.Path, header: dict[str, str],
                  records: list[tuple[float, bytes]]) -> None:
    """A second implementation of the writer, in the format's own terms.

    The gutter is emitted rather than omitted even though the reader treats it as
    optional, because the reader *verifies* one when it is there -- so writing it
    from an independent renderer is the only way this test checks that agreement
    rather than assuming it.
    """
    lines = [MAGIC]
    for key in ("sender", "device", "sourceId", "listen", "peer"):
        if header.get(key):
            lines.append(f"{key} {header[key]}")
    for receive_time, payload in records:
        lines.append("")
        lines.append(f"d {receive_time:.6f} {len(payload)}")
        for offset in range(0, len(payload), 16):
            chunk = payload[offset:offset + 16]
            hex_column = " ".join(f"{b:02x}" for b in chunk).ljust(16 * 3 - 1)
            lines.append(f"  {hex_column}  |{gutter(chunk)}|")
    # Written as bytes: the format is specified down to its line endings, so a
    # writer that let the platform translate them would be checking something
    # other than the format on Windows.
    path.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))


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


def report_prefix_bytes(text: str) -> bytes | None:
    """The `prefix:` block's bytes, read back out of the report.

    The block is laid out as the capture format lays out a datagram, so this
    reads it the way the capture parser reads a record rather than the way
    `report_lines` joins a continuation. Splitting at the *first* '|' is safe for
    the hex half; the gutter itself may contain '|' where a payload byte is 0x7c.
    """
    collected = bytearray()
    inside = False
    for line in text.splitlines():
        if line.startswith("prefix:"):
            inside = True
            continue
        if not inside:
            continue
        if not line.startswith(" "):
            break
        hex_half = line.split("|")[0].strip()
        if not hex_half:
            break
        collected += bytes.fromhex(hex_half)
    return bytes(collected) if inside else None


def run_tool(tool: pathlib.Path, *arguments: str) -> str:
    result = subprocess.run([str(tool), *arguments], text=True,
                            encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        fail(f"vrchat_osc_record {' '.join(arguments)} exited "
             f"{result.returncode}\n{result.stderr}")
    return result.stdout


def expect_exit(tool: pathlib.Path, code: int,
                *arguments: str) -> subprocess.CompletedProcess:
    result = subprocess.run([str(tool), *arguments], text=True,
                            encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != code:
        fail(f"`vrchat_osc_record {' '.join(arguments)}` should have exited "
             f"{code}, got {result.returncode}\n{result.stderr}")
    return result


# ---------------------------------------------------------------------------
# Payloads that are not packets
# ---------------------------------------------------------------------------

def payload(size: int, seed: int) -> bytes:
    """A counting pattern. See the module docstring for why it is not a packet."""
    return bytes((seed + index) % 256 for index in range(size))


# ---------------------------------------------------------------------------
# OSC, written here rather than by the library under test
# ---------------------------------------------------------------------------

def osc_string(text: str) -> bytes:
    """NUL-terminated, padded to four -- always at least one NUL."""
    raw = text.encode("ascii") + b"\x00"
    return raw + b"\x00" * (-len(raw) % 4)


def osc_message(address: str, values: list[float]) -> bytes:
    body = b"".join(struct.pack(">f", value) for value in values)
    return osc_string(address) + osc_string("," + "f" * len(values)) + body


def osc_bundle(elements: list[bytes]) -> bytes:
    out = osc_string("#bundle") + struct.pack(">Q", 1)
    for element in elements:
        out += struct.pack(">I", len(element)) + element
    return out


def inventory_rows(text: str) -> dict[tuple[str, str], str]:
    """The `addresses:` block, one entry per address and type tag pair.

    Read off the raw text rather than through `report_lines`, which joins every
    indented line into one string. The rows are what this milestone produces, so
    they are compared individually.
    """
    rows: dict[tuple[str, str], str] = {}
    inside = False
    for line in text.splitlines():
        if line.startswith("addresses:"):
            inside = True
            continue
        if not inside:
            continue
        if not line.startswith(" "):
            break
        fields = line.strip().split()
        if not fields[0].startswith("/"):
            # A diagnostic line for a refused datagram, not a row.
            continue
        rows[(fields[0], fields[1])] = line.strip()
    return rows


# ---------------------------------------------------------------------------
# --inspect: no socket anywhere
# ---------------------------------------------------------------------------

def check_inspect(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    records = [
        (0.000000, payload(24, 0x41)),
        (0.020000, payload(24, 0x41)),
        (0.040000, payload(48, 0x41)),
    ]
    capture = workspace / "inspect.vrchatoscpackets"
    write_capture(capture, {
        "sender": "example.synthetic",
        "device": "example.synthetic",
        "sourceId": "inspect-01",
        "peer": "192.168.0.20:52001",
    }, records)

    text = run_tool(tool, "--inspect", str(capture))
    lines = report_lines(text)

    total = sum(len(p) for _, p in records)
    if not lines.get("received", "").startswith(
            f"{len(records)} datagram(s), {total} byte(s)"):
        fail(f"--inspect: received line was '{lines.get('received')}'")

    if not lines.get("lengths", "").startswith("2 distinct in 3 datagram(s)"):
        fail(f"--inspect: lengths line was '{lines.get('lengths')}'")
    # Commonest first, and the count is what is named rather than the length.
    if "2 of 24 byte(s)" not in lines.get("lengths", ""):
        fail(f"--inspect: census did not name the commonest length: "
             f"'{lines.get('lengths')}'")

    # The three payloads share their first 24 bytes and diverge after, because
    # the third is the same pattern continued. That is a property of what this
    # file wrote, which is the only kind of claim about payload bytes this suite
    # is allowed to make.
    shared = report_prefix_bytes(text)
    if shared != records[0][1]:
        fail(f"--inspect: prefix was {shared!r}, expected {records[0][1]!r}")

    provenance = lines.get("provenance", "")
    for expected in ("sender=example.synthetic", "device=example.synthetic",
                     "sourceId=inspect-01", "peer=192.168.0.20:52001"):
        if expected not in provenance:
            fail(f"--inspect: provenance line lacks {expected}: '{provenance}'")

    if lines.get("stopped") != "end of capture":
        fail(f"--inspect: stopped line was '{lines.get('stopped')}'")
    if lines.get("diagnostics") != "none":
        fail(f"--inspect: diagnostics line was '{lines.get('diagnostics')}'")
    # A file replay never bound anything, so the socket lines are absent rather
    # than zero -- see SessionReport.h.
    if "listen:" in text:
        fail("--inspect: printed a socket line for a session with no socket")

    # These payloads are counting patterns and not OSC, so the inventory has no
    # rows and three refusals. That is the case worth asserting first: the port
    # is a well-known one, anything on the network may send to it, and a
    # capture full of traffic this decoder refuses must read as a finding rather
    # than as a broken tool.
    if not lines.get("addresses", "").startswith(
            "0 (0 message(s), 0 bundled datagram(s), 3 refused)"):
        fail(f"--inspect: addresses line was '{lines.get('addresses')}'")
    if text.count("[VRM_VRCHAT_OSC_PACKET_MALFORMED]") != len(records):
        fail("--inspect: a refused datagram must say why, once each")


def check_inspect_inventory(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    """The address inventory, over OSC this file encoded itself.

    Every datagram here is built by `osc_message` and `osc_bundle` above rather
    than by anything the tool links, so a decoder that agreed with the
    repository's own encoder and with nothing else would fail this.

    The addresses are data. Two of them look like the VRChat tracker surface,
    one looks like a different part of it, and one is deliberately not in any
    document -- because the claim is that the inventory reports what a sender
    sent rather than what somebody expected.
    """
    frame = osc_bundle([
        osc_message("/tracking/trackers/1/position", [0.0, 1.0, 0.0]),
        osc_message("/tracking/trackers/1/rotation", [0.0, 90.0, 0.0]),
        osc_message("/avatar/parameters/VelocityY", [0.5]),
        osc_message("/an/address/nobody/documented", [1.0]),
    ])
    records = [
        (0.000000, frame),
        (0.100000, frame),
        # Unbundled, and a second type tag string for an address already seen.
        (0.200000, osc_message("/tracking/trackers/1/position", [2.0])),
        # And one datagram that is not OSC at all.
        (0.300000, b"\xde\xad\xbe\xef"),
    ]
    capture = workspace / "inventory.vrchatoscpackets"
    write_capture(capture, {
        "sender": "example.synthetic",
        "sourceId": "inventory-01",
        "peer": "192.168.0.20:52001",
    }, records)

    text = run_tool(tool, "--inspect", str(capture))
    lines = report_lines(text)

    if not lines.get("addresses", "").startswith(
            "5 (9 message(s), 2 bundled datagram(s), 1 refused)"):
        fail(f"--inspect: addresses line was '{lines.get('addresses')}'")

    rows = inventory_rows(text)
    expected = {
        ("/an/address/nobody/documented", ",f"): 2,
        ("/avatar/parameters/VelocityY", ",f"): 2,
        ("/tracking/trackers/1/position", ",f"): 1,
        ("/tracking/trackers/1/rotation", ",fff"): 2,
        ("/tracking/trackers/1/position", ",fff"): 2,
    }
    for key, messages in expected.items():
        if key not in rows:
            fail(f"--inspect: the inventory has no row for {key}: {sorted(rows)}")
        if f"{messages} message(s)" not in rows[key]:
            fail(f"--inspect: {key} reported '{rows[key]}', expected "
                 f"{messages} message(s)")

    # One address, two type tag strings, two rows. A table keyed on the address
    # alone would average them and hide the shape a decoder has to handle.
    if len([key for key in rows
            if key[0] == "/tracking/trackers/1/position"]) != 2:
        fail("--inspect: one address with two type tag strings must be two rows")

    # The rows are sorted, because an operator diffs one session's report
    # against the next's.
    if list(rows) != sorted(rows):
        fail(f"--inspect: the inventory is not sorted: {list(rows)}")

    # The refused datagram is named with this adapter's own code, not the shared
    # decoder's -- which has none.
    if text.count("[VRM_VRCHAT_OSC_PACKET_MALFORMED]") != 1:
        fail("--inspect: the refused datagram was not reported once")


def check_inspect_refuses_a_bad_capture(tool: pathlib.Path,
                                        workspace: pathlib.Path) -> None:
    # A capture that is a *sibling's*. It parses perfectly as a capture -- the
    # record grammar is one grammar -- and it is a recording of another
    # protocol, so refusing it at line 1 is what stops a fixture being blamed on
    # a source that never sent it.
    sibling = workspace / "sibling.vrchatoscpackets"
    sibling.write_bytes(b"!vmc-packet-capture 1\n\nd 0.000000 1\n  2f  |/|\n")
    result = expect_exit(tool, 1, "--inspect", str(sibling))
    if ":1:" not in result.stderr:
        fail(f"--inspect: a refusal must name the line, got '{result.stderr}'")

    # And a capture with no datagrams, which the reader refuses and this tool
    # therefore must never write.
    empty = workspace / "empty.vrchatoscpackets"
    empty.write_bytes((MAGIC + "\nsender example\n").encode("utf-8"))
    expect_exit(tool, 1, "--inspect", str(empty))


def check_help_and_refusals(tool: pathlib.Path,
                            workspace: pathlib.Path) -> None:
    usage = run_tool(tool, "--help")
    for expected in ("--inspect", "--output", "--dry-run", "--silence-timeout",
                     "9000"):
        if expected not in usage:
            fail(f"--help does not mention {expected}")

    capture = workspace / "refusals.vrchatoscpackets"
    write_capture(capture, {"sourceId": "refusals-01"},
                  [(0.0, payload(8, 0x30))])

    # Every one of these is a mistake the tool can name at the prompt, before a
    # socket is opened or a byte is read. Exit 2 throughout: 1 is a session that
    # ran and failed, and a script has to be able to tell those apart.
    expect_exit(tool, 2)
    expect_exit(tool, 2, "--not-a-flag")
    expect_exit(tool, 2, "--output")
    expect_exit(tool, 2, "--dry-run", "--output", str(workspace / "x"))
    expect_exit(tool, 2, "--output", str(workspace / "x"), "--sender",
                "two words")
    expect_exit(tool, 2, "--output", str(workspace / "x"), "--listen",
                "127.0.0.1:")
    expect_exit(tool, 2, "--output", str(workspace / "x"), "--listen",
                "::1:9000")
    expect_exit(tool, 2, "--output", str(workspace / "x"), "--duration", "nan")
    expect_exit(tool, 2, "--output", str(workspace / "x"), "--max-datagrams",
                "0")
    # --inspect opens no socket, so a flag about one has nothing to act on. The
    # list of exceptions is empty in this tool, where the siblings each have two
    # export flags -- nothing here decodes, so there is nothing to write out of a
    # capture.
    result = expect_exit(tool, 2, "--inspect", str(capture), "--port", "9000")
    if "--port" not in result.stderr:
        fail(f"--inspect refusal should name the flag, got '{result.stderr}'")


# ---------------------------------------------------------------------------
# The socket
# ---------------------------------------------------------------------------

class Session:
    """A running recorder, with its stderr drained so it cannot deadlock."""

    def __init__(self, tool: pathlib.Path, *arguments: str) -> None:
        self._process = subprocess.Popen(
            [str(tool), *arguments], text=True, encoding="utf-8",
            errors="replace", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.stderr: list[str] = []
        self.endpoint = ""
        self._listening = threading.Event()
        self._thread = threading.Thread(target=self._drain, daemon=True)
        self._thread.start()

    def _drain(self) -> None:
        assert self._process.stderr is not None
        for line in self._process.stderr:
            self.stderr.append(line.rstrip("\n"))
            if "listening on " in line and not self.endpoint:
                self.endpoint = line.rsplit("listening on ", 1)[1].strip()
                self._listening.set()

    def wait_until_listening(self, timeout: float = 10.0) -> str:
        if not self._listening.wait(timeout):
            self.kill()
            fail("the recorder never said where it was listening")
        return self.endpoint

    def finish(self, timeout: float = 30.0) -> tuple[int, str]:
        """Waits for the session to end, and collects both streams.

        Deliberately **not** `communicate()`: the drain thread above is already
        reading stderr, and `communicate()` reads and closes it too. The two race
        for the same bytes, and the loser is whichever lines are nearest EOF --
        which is exactly where the end-of-session warnings are, and where
        `check_silence` reads the diagnostic it asserts. The closed pipe also
        raises `ValueError` in the drain thread, on a daemon thread nobody is
        watching. The mocopi tool's suite carries the same note for the same
        reason; this one had the defect anyway, which is a fair argument that
        the note belongs in both files rather than one.

        **It did not reproduce here in eight runs with `communicate()` back**,
        and that is the reason to fix it by construction rather than to wait for
        a red run: a loser-takes-the-last-lines race is decided by scheduling, so
        a workstation that wins it every time says nothing about a loaded CI
        runner. What *is* checked is the consequence -- `check_loopback` asserts
        the write line the tool emits immediately before exiting, which is
        precisely the line this race would eat.

        The watchdog replaces `communicate(timeout=)`, which is the only thing
        that call was still buying: a hung tool must fail this test, not park the
        runner on a blocking read.
        """
        watchdog = threading.Timer(timeout, self._process.kill)
        watchdog.start()
        try:
            assert self._process.stdout is not None
            stdout = self._process.stdout.read()
            self._process.wait()
        finally:
            watchdog.cancel()
        # Only after stderr has reached EOF is `self.stderr` the whole session.
        self._thread.join(timeout=10.0)
        return self._process.returncode, stdout

    def kill(self) -> None:
        self._process.kill()
        self._process.wait(timeout=10.0)


def send_all(endpoint: str, payloads: list[bytes]) -> None:
    host, _, port = endpoint.rpartition(":")
    host = host.strip("[]")
    family = socket.AF_INET6 if ":" in host else socket.AF_INET
    with socket.socket(family, socket.SOCK_DGRAM) as sender:
        sender.connect((host, int(port)))
        for one in payloads:
            sender.send(one)
            # One at a time, with the smallest pause that keeps a kernel receive
            # buffer from being the thing under test. A burst large enough to
            # overflow it would fail this for a reason that is not about the
            # recorder.
            time.sleep(0.005)


def check_loopback(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    # The claim this milestone exists to make, end to end and through a real
    # process: what a sender put on the wire is what the file holds.
    sent = [
        payload(1, 0x00),
        payload(16, 0x20),
        payload(37, 0x7d),
        payload(256, 0x00),
        b"",
    ]
    output = workspace / "loopback.vrchatoscpackets"
    session = Session(tool, "--output", str(output),
                      "--listen", "127.0.0.1", "--port", "0",
                      "--sender", "example.synthetic",
                      "--device", "example.synthetic",
                      "--source-id", "loopback-01",
                      "--max-datagrams", str(len(sent)))
    endpoint = session.wait_until_listening()
    send_all(endpoint, sent)
    code, stdout = session.finish()
    if code != 0:
        fail(f"the recorder exited {code}\n" + "\n".join(session.stderr))

    header, payloads = read_capture(output)
    if payloads != sent:
        fail("the capture is not what was sent:\n"
             f"  sent     {[len(p) for p in sent]}\n"
             f"  captured {[len(p) for p in payloads]}")
    if header.get("sender") != "example.synthetic":
        fail(f"the capture header lost its sender: {header}")
    if header.get("sourceId") != "loopback-01":
        fail(f"the capture header lost its sourceId: {header}")
    if header.get("listen") != endpoint:
        fail(f"the capture header says listen={header.get('listen')}, "
             f"the tool said {endpoint}")
    if not header.get("peer", "").startswith("127.0.0.1:"):
        fail(f"the capture header lost its peer: {header}")

    # The last line the tool writes before it exits, asserted because it is the
    # one a stderr race loses. `finish()` reads stdout on this thread and lets
    # the drain thread reach EOF on its own for exactly this reason; a
    # `communicate()` here would eat whichever lines are nearest the end, and
    # every end-of-session warning this tool has is there.
    if not any("wrote 5 datagram(s) to " in line for line in session.stderr):
        fail("the end-of-session write line never reached stderr:\n"
             + "\n".join(session.stderr))

    lines = report_lines(stdout)
    if lines.get("stopped") != "--max-datagrams reached":
        fail(f"stopped line was '{lines.get('stopped')}'")
    if not lines.get("empty", "").startswith("1 zero-length datagram(s)"):
        fail(f"a zero-length datagram was not reported: '{lines.get('empty')}'")
    if "(loopback only" not in lines.get("listen", ""):
        fail(f"listen line did not say it was loopback: '{lines.get('listen')}'")

    # And the recorded file is what `--inspect` reads: the two halves of this
    # tool meet on one file, which is what makes a fixture recorded today
    # readable by a check run months later.
    #
    # The counts are compared and the *duration* is not, and the difference is a
    # property of the format rather than a looser assertion. A capture records
    # receive times to six decimal places, so a replay's duration is the file's
    # microsecond resolution and the live report's is the clock's -- measured
    # here as 0.020738 against 0.0207381. Everything this milestone claims to
    # preserve is in the counts; the last digit of a duration is not, and
    # asserting it would make this test fail on the format doing what it says.
    replay = report_lines(run_tool(tool, "--inspect", str(output)))
    counts = f"{len(sent)} datagram(s), {sum(len(p) for p in sent)} byte(s)"
    for name, line in (("live", lines.get("received", "")),
                       ("--inspect", replay.get("received", ""))):
        if not line.startswith(counts):
            fail(f"the {name} report said '{line}', expected it to start "
                 f"'{counts}'")
    if replay.get("lengths") != lines.get("lengths"):
        fail(f"--inspect disagrees with the live census: "
             f"'{replay.get('lengths')}' vs '{lines.get('lengths')}'")


def check_stop_reasons(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    # A session that received nothing writes no file and exits 1, because the
    # format has no datagram-less form and the alternative is handing an operator
    # an artifact this adapter's own reader refuses.
    output = workspace / "never.vrchatoscpackets"
    session = Session(tool, "--output", str(output),
                      "--listen", "127.0.0.1", "--port", "0",
                      "--idle-timeout", "0.3")
    session.wait_until_listening()
    code, stdout = session.finish()
    if code != 1:
        fail(f"a session that received nothing should exit 1, got {code}")
    if output.exists():
        fail("a session that received nothing wrote a file")
    lines = report_lines(stdout)
    if lines.get("stopped") != "--idle-timeout elapsed with nothing arriving":
        fail(f"stopped line was '{lines.get('stopped')}'")

    # --dry-run listens, reports, and writes nothing -- including when datagrams
    # do arrive, which is the case that tells it apart from the one above.
    #
    # "Wrote nothing" is checked against a snapshot of the whole directory taken
    # before the run, and that is the second attempt at this assertion. The first
    # looked for a file whose name began with `dry`, which could never exist:
    # the session is launched with no `--output`, so there is no path for the
    # tool to have written to and the check could not fail whatever the tool did.
    # A vacuous assertion is worse than none, because it reads as coverage.
    before = {entry.name for entry in workspace.iterdir()}
    session = Session(tool, "--dry-run",
                      "--listen", "127.0.0.1", "--port", "0",
                      "--duration", "0.4")
    endpoint = session.wait_until_listening()
    send_all(endpoint, [payload(12, 0x50)])
    code, stdout = session.finish()
    if code != 0:
        fail(f"--dry-run exited {code}\n" + "\n".join(session.stderr))
    lines = report_lines(stdout)
    if lines.get("stopped") != "--duration elapsed":
        fail(f"--dry-run stopped line was '{lines.get('stopped')}'")
    if not lines.get("received", "").startswith("1 datagram(s)"):
        fail(f"--dry-run did not report the datagram: '{lines.get('received')}'")
    written = {entry.name for entry in workspace.iterdir()} - before
    if written:
        fail(f"--dry-run wrote {sorted(written)}")


def check_silence(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    # `--silence-timeout` reports and keeps listening; `--idle-timeout` is what
    # stops. Both are on this session, and the stop reason is what says which of
    # them did what.
    output = workspace / "silence.vrchatoscpackets"
    session = Session(tool, "--output", str(output),
                      "--listen", "127.0.0.1", "--port", "0",
                      "--source-id", "silence-01",
                      "--silence-timeout", "0.1",
                      "--idle-timeout", "1.5")
    endpoint = session.wait_until_listening()
    # Long enough for the first silence episode to be reported before anything
    # arrives, which is the half of this code a sender that has not started
    # produces.
    time.sleep(0.5)
    send_all(endpoint, [payload(20, 0x60)])
    code, stdout = session.finish()
    if code != 0:
        fail(f"the recorder exited {code}\n" + "\n".join(session.stderr))

    if "VRM_VRCHAT_OSC_SOURCE_TIMEOUT" not in stdout:
        fail("a silent session did not report VRM_VRCHAT_OSC_SOURCE_TIMEOUT\n"
             + stdout)
    if "VRM_VRCHAT_OSC_SOURCE_TIMEOUT" not in "\n".join(session.stderr):
        fail("the silence diagnostic never reached stderr, where an operator "
             "waiting for a sender would read it")
    lines = report_lines(stdout)
    if "warning" not in lines.get("diagnostics", ""):
        fail(f"diagnostics line was '{lines.get('diagnostics')}'")


def check_bind_failure(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    # The one fatal code this adapter has. A port already served is refused
    # rather than silently shared, which matters more on this adapter than on
    # either sibling: its default port is the one VRChat itself binds.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as held:
        held.bind(("127.0.0.1", 0))
        port = held.getsockname()[1]
        result = expect_exit(tool, 1,
                             "--output", str(workspace / "taken"),
                             "--listen", "127.0.0.1", "--port", str(port),
                             "--idle-timeout", "0.2")
    if "VRM_VRCHAT_OSC_SOCKET_BIND_FAILED" not in result.stderr:
        fail("a refused bind did not report VRM_VRCHAT_OSC_SOCKET_BIND_FAILED\n"
             + result.stderr)
    if "error fatal" not in result.stderr:
        fail(f"the bind failure was not fatal: '{result.stderr}'")


# ---------------------------------------------------------------------------
# IPv6, which a runner may not have
# ---------------------------------------------------------------------------

def check_ipv6(tool: pathlib.Path, workspace: pathlib.Path) -> int:
    if not socket.has_ipv6:
        print("skipped: this Python has no IPv6")
        return SKIP_EXIT_CODE
    try:
        with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as probe:
            probe.bind(("::1", 0))
    except OSError:
        print("skipped: no IPv6 loopback on this host")
        return SKIP_EXIT_CODE

    # The receiver deliberately does not refuse an IPv6 bind: a socket inventing
    # a restriction on itself from an *application's* configuration is the wrong
    # layer for it. This tool is the right layer, and this test is what makes
    # that split a fact rather than a comment.
    session = Session(tool, "--dry-run", "--listen", "[::1]", "--port", "0",
                      "--duration", "0.3")
    session.wait_until_listening()
    code, _ = session.finish()
    if code != 0:
        fail(f"an IPv6 session exited {code}\n" + "\n".join(session.stderr))
    if not any("IPv6 endpoint" in line for line in session.stderr):
        fail("an IPv6 bind did not warn that a VRChat sender is aimed at IPv4\n"
             + "\n".join(session.stderr))
    print("an IPv6 bind is allowed and warned about")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True, type=pathlib.Path)
    parser.add_argument("--mode", required=True,
                        choices=("inspect", "loopback", "ipv6"))
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory() as directory:
        workspace = pathlib.Path(directory)
        if arguments.mode == "inspect":
            check_inspect(arguments.tool, workspace)
            check_inspect_inventory(arguments.tool, workspace)
            check_inspect_refuses_a_bad_capture(arguments.tool, workspace)
            check_help_and_refusals(arguments.tool, workspace)
            print("vrchat_osc_record --inspect checks passed")
            return 0
        if arguments.mode == "loopback":
            check_loopback(arguments.tool, workspace)
            check_stop_reasons(arguments.tool, workspace)
            check_silence(arguments.tool, workspace)
            check_bind_failure(arguments.tool, workspace)
            print("vrchat_osc_record loopback checks passed")
            return 0
        return check_ipv6(arguments.tool, workspace)


if __name__ == "__main__":
    raise SystemExit(main())
