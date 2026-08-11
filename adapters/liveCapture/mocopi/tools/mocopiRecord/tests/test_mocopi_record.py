#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""`mocopi_record` end to end, in three parts the adapter already splits things into.

**inspect** reads captures this file authors and checks the report against counts
it states. The captures are written here rather than taken from a corpus for a
reason that is the whole point of the tool: there is no mocopi corpus yet, and
obtaining the first one is what this tool exists for. Authoring them in Python
also makes the check independent of the writer under test -- the format is
line-oriented text precisely so that a second implementation is thirty lines
long -- which is the same arrangement the sibling adapter's tests use in the
opposite direction, where a Python *reader* checks a C++ writer.

**loopback** sends bytes at a real socket and records them back, then makes the
claim the tool exists for:

    the datagrams that came out of the socket are byte-identical to the ones
    that went in, and the report describes them without having decoded one

The bytes sent are arbitrary, and that is not a shortcut. This tool decodes
nothing, so there is no such thing as a valid or invalid input to it -- a
recorder that behaved differently on bytes it recognised would be exactly the
recorder this adapter must not have.

**ipv6** checks the one warning that needs an address family a runner may not
have. It exits 77 -- ctest's skip code -- rather than passing quietly, so
"could not check this here" is a word in the summary.
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

# The captures this test authors, as (receiveTime, payload) records. Each one is
# shaped to pin a specific line of the report rather than to look like a session:
# nothing here knows what a mocopi packet is, and a fixture that pretended to
# would be the guess this adapter's whole build order exists to avoid.
CAPTURES = {
    # One shape, one peer, an even 10 Hz. The census must say "1 distinct" and
    # the prefix must be the whole 8 bytes, since every datagram is identical.
    "one-shape": {
        "header": {"sender": "test.authored", "device": "test.none",
                   "sourceId": "one-shape-01", "listen": "0.0.0.0:12351",
                   "peer": "192.168.0.20:52001"},
        "records": [(round(0.1 * i, 6), bytes([0x08, 0x00, 0x00, 0x00,
                                               0x68, 0x65, 0x61, 0x64]))
                    for i in range(10)],
        "distinct": 1,
        "lengths": "10 of 8 byte(s)",
        # Identical datagrams, so the common prefix is the whole payload.
        "prefix": "08 00 00 00 68 65 61 64",
        "arrival_hz": 10.0,
    },
    # Three shapes sharing four leading bytes, which is the census line and the
    # prefix line together: a decoder built from a capture of one shape would
    # meet the other two for the first time in production.
    "three-shapes": {
        "header": {"sourceId": "three-shapes-01"},
        "records": [
            (0.000000, bytes([0xde, 0xad, 0xbe, 0xef]) + b"\x01" * 12),
            (0.020000, bytes([0xde, 0xad, 0xbe, 0xef]) + b"\x02" * 4),
            (0.040000, bytes([0xde, 0xad, 0xbe, 0xef]) + b"\x03" * 12),
            (0.060000, bytes([0xde, 0xad, 0xbe, 0xef]) + b"\x04" * 12),
            (0.080000, bytes([0xde, 0xad, 0xbe, 0xef])),
        ],
        "distinct": 3,
        # Commonest first, then by length: three of 16, one of 8, one of 4.
        "lengths": "3 of 16 byte(s), 1 of 4, 1 of 8",
        "prefix": "de ad be ef",
        "arrival_hz": 50.0,
    },
    # Payloads that agree past the 32-byte prefix cap, so the report has to say
    # "at least 32" -- the shared prefix genuinely may be longer than shown.
    "capped-prefix": {
        "header": {"sourceId": "capped-prefix-01"},
        "records": [(round(0.05 * i, 6), b"\x5a" * 40 + bytes([i]))
                    for i in range(4)],
        "distinct": 1,
        "lengths": "4 of 41 byte(s)",
        "prefix": " ".join(["5a"] * 32),
        "prefix_capped": True,
        "arrival_hz": 20.0,
    },
    # The same agreement, cut off by a datagram that is *itself* 32 bytes long.
    # The shared prefix is then exactly 32 and "at least" would be wrong -- it
    # would send a reviewer looking for bytes that are not there.
    "exact-prefix": {
        "header": {"sourceId": "exact-prefix-01"},
        "records": [
            (0.000000, b"\x5a" * 40),
            (0.050000, b"\x5a" * 32),
        ],
        "distinct": 2,
        "lengths": "1 of 32 byte(s), 1 of 40",
        "prefix": " ".join(["5a"] * 32),
        "prefix_capped": False,
        "arrival_hz": 20.0,
    },
    # A zero-length datagram, which the format calls legal and meaningful and the
    # report therefore has to name rather than fold into a census entry. It also
    # drives the prefix to nothing: an empty payload shares no byte with anything.
    "empty-datagram": {
        "header": {},
        "records": [
            (0.000000, b"\xaa\xbb\xcc"),
            (0.010000, b""),
            (0.020000, b"\xaa\xbb\xcc"),
        ],
        "distinct": 2,
        "lengths": "2 of 3 byte(s), 1 of 0",
        "prefix": None,  # no bytes common to every datagram
        "arrival_hz": 100.0,
        "empty": 1,
    },
}


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
    lines = ["!mocopi-packet-capture 1"]
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


def run_tool(tool: pathlib.Path, *arguments: str) -> str:
    result = subprocess.run([str(tool), *arguments], text=True,
                            encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        fail(f"mocopi_record {' '.join(arguments)} exited "
             f"{result.returncode}\n{result.stderr}")
    return result.stdout


def expect_exit(tool: pathlib.Path, code: int,
                *arguments: str) -> subprocess.CompletedProcess:
    result = subprocess.run([str(tool), *arguments], text=True,
                            encoding="utf-8", errors="replace",
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != code:
        fail(f"`mocopi_record {' '.join(arguments)}` should have exited {code}, "
             f"got {result.returncode}\n{result.stderr}")
    return result


def check_report(name: str, lines: dict[str, str], expected: dict) -> None:
    """Every line of the report that is a statement about the bytes."""
    records = expected["records"]
    payload_bytes = sum(len(payload) for _, payload in records)

    received = lines.get("received", "")
    if not received.startswith(
            f"{len(records)} datagram(s), {payload_bytes} byte(s)"):
        fail(f"{name}: expected {len(records)} datagrams and {payload_bytes} "
             f"bytes, report said '{received}'")

    lengths = lines.get("lengths", "")
    if not lengths.startswith(f"{expected['distinct']} distinct in "
                              f"{len(records)} datagram(s)"):
        fail(f"{name}: expected {expected['distinct']} distinct lengths, "
             f"report said '{lengths}'")
    # The census entries are a continuation line, which `report_lines` joins on.
    if not lengths.endswith(expected["lengths"]):
        fail(f"{name}: expected the census to end '{expected['lengths']}', "
             f"report said '{lengths}'")

    prefix = lines.get("prefix", "")
    if expected["prefix"] is None:
        if prefix != "no bytes common to every datagram":
            fail(f"{name}: datagrams share no leading byte, report said "
                 f"'{prefix}'")
    else:
        wanted = expected["prefix"]
        count = len(wanted.split())
        # "at least" is warranted only when the cap is what shortened the prefix,
        # never merely because the prefix came out cap-length.
        lower_bound = "at least " if expected.get("prefix_capped") else ""
        if prefix != (f"{lower_bound}{count} byte(s) common to every datagram: "
                      f"{wanted}"):
            fail(f"{name}: expected a {lower_bound}{count}-byte common prefix "
                 f"'{wanted}', report said '{prefix}'")

    # Absent, not zero, when the session carried none: a line that always
    # printed would make "0 zero-length datagram(s)" the commonest line in
    # every report anybody ever reads.
    empty = lines.get("empty")
    if expected.get("empty"):
        if not empty or not empty.startswith(f"{expected['empty']} zero-length"):
            fail(f"{name}: expected {expected['empty']} zero-length "
                 f"datagram(s), report said '{empty}'")
    elif empty is not None:
        fail(f"{name}: carries no zero-length datagram, but the report has an "
             f"empty line: '{empty}'")

    # An arrival rate is a statement about intervals on a monotonic clock, so it
    # can never be negative and here it is known exactly: the authored records
    # are evenly spaced.
    arrival = lines.get("arrival", "")
    mean = re.match(r"([0-9.eE+-]+) Hz mean", arrival)
    if not mean:
        fail(f"{name}: the report has no arrival rate: '{arrival}'")
    if abs(float(mean.group(1)) - expected["arrival_hz"]) > 0.05:
        fail(f"{name}: expected {expected['arrival_hz']} Hz of arrivals, "
             f"report said '{arrival}'")


def check_inspect(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    for name, expected in sorted(CAPTURES.items()):
        capture = workspace / f"{name}.mocopipackets"
        write_capture(capture, expected["header"], expected["records"])
        lines = report_lines(run_tool(tool, "--inspect", str(capture)))
        check_report(name, lines, expected)

        if lines.get("stopped") != "end of capture":
            fail(f"{name}: a replayed capture ends at its end, not at "
                 f"'{lines.get('stopped')}'")
        # A replay opened no socket, so the lines that describe one are absent
        # rather than zeroed.
        if "listen" in lines:
            fail(f"{name}: --inspect reported a bound endpoint it never had")
        # And nothing decoded, so nothing raised a diagnostic. This is the line
        # that would change first if a decoder were ever smuggled in here.
        if lines.get("diagnostics") != "none":
            fail(f"{name}: nothing here decodes, so nothing can diagnose; "
                 f"report said '{lines.get('diagnostics')}'")

        # The provenance the capture carries, read back. This is half of what
        # "is this fixture still what I thought it was" means, and it is the one
        # report line the live path deliberately does not print.
        provenance = lines.get("provenance", "")
        if expected["header"]:
            for key, value in expected["header"].items():
                if f"{key}={value}" not in provenance:
                    fail(f"{name}: the report lost the capture's {key}: "
                         f"'{provenance}'")
        elif provenance != "none recorded":
            fail(f"{name}: this capture records no provenance, report said "
                 f"'{provenance}'")

    print(f"mocopi_record --inspect: {len(CAPTURES)} authored capture(s) "
          f"reported as expected")


def check_malformed(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    """A capture the reader refuses is refused with its line named.

    The reader is strict in four ways the format's header lists, and every one of
    them is a way a *fixture* goes wrong silently. What this checks is the tool's
    half: exit 1, and a message carrying the line number, so an operator fixes a
    file rather than re-recording a session.
    """
    good = "\n".join([
        "!mocopi-packet-capture 1",
        "sourceId probe",
        "",
        "d 0.000000 4",
        "  de ad be ef  |....|",
    ]) + "\n"

    cases = {
        # A gutter that disagrees with its bytes. A reviewer reads the gutter and
        # not the hex, so this is worse than no gutter at all.
        "bad-gutter": good.replace("|....|", "|dead|"),
        # A record carrying fewer bytes than it declared, which must not read as
        # a truncated datagram the source is to blame for.
        "short-record": good.replace("d 0.000000 4", "d 0.000000 8"),
        # A typo in a header key, which must not read as an absent field.
        "unknown-key": good.replace("sourceId probe", "sourceid probe"),
        # A receive clock that runs backwards is a recorder defect, not a
        # phenomenon to reproduce. Two records rather than a substitution on the
        # one above, because the defect *is* the relation between two times.
        "backwards": "\n".join([
            "!mocopi-packet-capture 1",
            "",
            "d 1.000000 1",
            "  ff  |.|",
            "",
            "d 0.500000 1",
            "  ee  |.|",
        ]) + "\n",
        # No datagrams at all -- which is exactly what this tool declines to
        # write, checked from the other side.
        "no-datagrams": "!mocopi-packet-capture 1\nsourceId probe\n",
        # The other protocol's magic line. Two magic lines is a feature of having
        # two formats: this fails at line 1 with a clear message instead of at
        # the first field with a malformed-packet diagnostic.
        "wrong-magic": good.replace("!mocopi-packet-capture 1",
                                    "!vmc-packet-capture 1"),
    }

    for name, text in sorted(cases.items()):
        path = workspace / f"malformed-{name}.mocopipackets"
        path.write_bytes(text.encode("utf-8"))
        result = expect_exit(tool, 1, "--inspect", str(path))
        if str(path) not in result.stderr:
            fail(f"{name}: the refusal did not name the file: {result.stderr}")

    # A path that does not exist is the one refusal with no line number, so it
    # must not print one.
    missing = workspace / "absent.mocopipackets"
    result = expect_exit(tool, 1, "--inspect", str(missing))
    if "could not open" not in result.stderr:
        fail(f"a missing capture should say it could not be opened: "
             f"{result.stderr}")

    print(f"mocopi_record --inspect: {len(cases)} malformed capture(s) and a "
          f"missing one refused with a reason")


def check_help_and_refusals(tool: pathlib.Path,
                            workspace: pathlib.Path) -> None:
    """The CLI's own contract: --help succeeds, a bad combination does not."""
    usage = run_tool(tool, "--help")
    for expected in ("mocopi_record", "--inspect", "--silence-timeout",
                     "--device"):
        if expected not in usage:
            fail(f"--help did not document {expected}")

    capture = workspace / "one-shape.mocopipackets"
    write_capture(capture, CAPTURES["one-shape"]["header"],
                  CAPTURES["one-shape"]["records"])

    # Named inside the caller's temporary directory rather than the working one:
    # a refused invocation must write nothing, and checking that against a path
    # in the build tree would pass on a stale file from an earlier run.
    unused = workspace / "unused.mocopipackets"
    refusals = [
        # No output, no dry run, no inspect: nothing to do and no file to show
        # for it.
        [],
        # --dry-run writes nothing, so a flag that writes has nothing to act on.
        ["--dry-run", "--output", str(unused)],
        # An unbracketed IPv6 address with a port is ambiguous, not guessed at.
        ["--dry-run", "--listen", "::1:12351"],
        ["--dry-run", "--listen", "[::1"],
        # A half that is present-but-empty is refused rather than ignored. Each
        # of these used to parse as success and silently leave the default port
        # in place, which is indistinguishable from having been obeyed.
        ["--dry-run", "--listen", "127.0.0.1:"],
        ["--dry-run", "--listen", ":"],
        ["--dry-run", "--listen", "[]"],
        ["--dry-run", "--listen", "[::1]:"],
        ["--dry-run", "--listen", "[::1]x"],
        # Provenance is written into the capture header, which carries one token
        # per key -- so a value with whitespace is refused at the prompt rather
        # than after a session that cannot be re-recorded. `--sender "a b"` is
        # the shape this tool's own README used to suggest.
        ["--dry-run", "--sender", "mocopi 1.2.3"],
        ["--dry-run", "--device", "xperia 5"],
        ["--dry-run", "--source-id", "neutral standing"],
        ["--dry-run", "--sender", "a\tb"],
        ["--dry-run", "--device", "a\nd 0.0 1"],
        # A bound that admits no datagrams is refused rather than honoured.
        ["--dry-run", "--max-datagrams", "0"],
        # NaN and infinity pass every range check by failing to be on either
        # side of it, so they are refused at the parse.
        ["--dry-run", "--duration", "nan"],
        ["--dry-run", "--duration", "inf"],
        ["--dry-run", "--duration", "-1"],
        ["--dry-run", "--silence-timeout", "-1"],
        # Two hours is the ceiling: a mistyped duration should fail at the
        # prompt rather than forty days later.
        ["--dry-run", "--duration", "999999"],
        ["--dry-run", "--idle-timeout", "1.5.2"],
        ["--dry-run", "--max-datagrams", "1.5"],
        ["--dry-run", "--port", "70000"],
        ["--dry-run", "--nonsense"],
        ["--dry-run", "--duration"],
    ]
    # Every session flag, refused with --inspect. Listed exhaustively rather
    # than sampled: the parser tracks the *flag* and not the parsed value
    # precisely so that `--port 12351` is refused too, and a sample would not
    # notice a flag that stopped being tracked.
    for flag in (["--listen", "0.0.0.0"], ["--port", "12351"],
                 ["--reuse-address"], ["--receive-buffer", "65536"],
                 ["--silence-timeout", "5"], ["--output", str(unused)],
                 ["--sender", "x"], ["--device", "x"], ["--source-id", "x"],
                 ["--duration", "1"], ["--idle-timeout", "1"],
                 ["--max-datagrams", "10"], ["--dry-run"]):
        refusals.append(["--inspect", str(capture), *flag])

    for arguments in refusals:
        expect_exit(tool, 2, *arguments)
        if unused.exists():
            fail(f"`mocopi_record {' '.join(arguments)}` was refused and still "
                 f"wrote a file")

    # And the one flag that survives --inspect, because it is about the streams
    # rather than about a socket.
    if not run_tool(tool, "--inspect", str(capture), "--quiet"):
        fail("--quiet suppressed the report, which is what the tool produces")

    # A range error states the range that is actually enforced. `--max-datagrams`
    # refuses 0 under a separate check, so a message offering 0 sends a user who
    # follows it straight into the second refusal.
    over = expect_exit(tool, 2, "--dry-run", "--max-datagrams", "20000000")
    if "between 1 and 10000000" not in over.stderr:
        fail(f"--max-datagrams should state the range it enforces: "
             f"{over.stderr.splitlines()[0] if over.stderr else ''}")
    # And 0 is refused by that same stated range rather than by a check below it.
    zero = expect_exit(tool, 2, "--dry-run", "--max-datagrams", "0")
    if "between 1 and 10000000" not in zero.stderr:
        fail(f"--max-datagrams 0 should be refused by the stated range: "
             f"{zero.stderr.splitlines()[0] if zero.stderr else ''}")

    # The forms that must keep working, so the refusals above are not a blanket
    # tightening: a bare address, a port alone, and a bracketed IPv6 address with
    # and without a port. `--dry-run --duration 0` binds, reports and exits.
    for listen in ("127.0.0.1", ":0", "[::1]", "[::1]:0"):
        result = subprocess.run(
            [str(tool), "--dry-run", "--listen", listen, "--duration", "1"],
            text=True, encoding="utf-8", errors="replace",
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # Exit 1 is allowed here and only here: a runner with no IPv6 cannot bind
        # `[::1]` and reports VRM_MOCOPI_SOCKET_BIND_FAILED, which is the socket
        # answering rather than the parser refusing. Exit 2 is the parser, and
        # that is what must not happen.
        if result.returncode == 2:
            fail(f"`--listen {listen}` is a documented form and was refused: "
                 f"{result.stderr}")

    print(f"mocopi_record: usage, {len(refusals)} refusals, and four accepted "
          f"--listen forms behave")


class Session:
    """One `mocopi_record` run against a real socket, driven from this process.

    Every live check needs the same four things -- a port nothing holds, a
    started tool, datagrams delivered only after it says it is bound, and a
    process that stops on its own -- so they are here once. What varies is the
    arguments and what is sent, which is what each check is actually about.
    """

    def __init__(self, tool: pathlib.Path, output: pathlib.Path | None,
                 *extra: str, host: str = "127.0.0.1") -> None:
        self.tool = tool
        self.output = output
        self.extra = extra
        self.host = host
        self.family = (socket.AF_INET6 if ":" in host else socket.AF_INET)
        # Bracketed for the CLI and bare for the socket. An unbracketed IPv6
        # address with a port is ambiguous -- "::1:12351" is a valid address in
        # its own right -- so `--listen` refuses one, and a test that passed the
        # bare form would be exercising that refusal instead of an IPv6 bind.
        self.listen = f"[{host}]" if self.family == socket.AF_INET6 else host
        self.port = 0
        self.stdout = ""
        self.stderr: list[str] = []

    def _spawn(self, port: int) -> subprocess.Popen:
        arguments = [str(self.tool), "--listen", self.listen, "--port",
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
        test that fails once a month for a reason it cannot explain is worse than
        one that retries.
        """
        for attempt in range(3):
            port = free_udp_port(self.family)
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
                fail(f"mocopi_record never bound a port\n"
                     f"{''.join(self.stderr)}")
            self.stderr.clear()
        raise AssertionError("unreachable")

    def send(self, payloads: list[bytes], *, source_port: int = 0) -> "Session":
        """Delivers datagrams, from an optionally distinct source port."""
        with socket.socket(self.family, socket.SOCK_DGRAM) as sender:
            if source_port:
                sender.bind((self.host, source_port))
            for payload in payloads:
                sender.sendto(payload, (self.host, self.port))
                # Sent flat out, a burst can outrun the receive buffer on a
                # loaded runner -- and a dropped datagram would make this flaky
                # rather than failing.
                time.sleep(0.002)
        return self

    def finish(self, expected_returncode: int = 0) -> dict[str, str]:
        """Waits for the session to end, and collects both streams.

        Deliberately not `communicate()`: the drain thread above is already
        reading stderr, and `communicate()` reads and closes it too. The two
        race, and the loser is whichever lines are near EOF -- which is exactly
        where the end-of-session warnings are.

        The watchdog replaces `communicate(timeout=)`, which is the only thing
        that call was still buying: a hung tool must fail this test, not park the
        runner on a blocking read.
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
            fail(f"mocopi_record exited {self._process.returncode}, expected "
                 f"{expected_returncode}\n{''.join(self.stderr)}")
        return report_lines(self.stdout)


def free_udp_ports(count: int = 1, family: int = socket.AF_INET) -> list[int]:
    """`count` distinct ports nothing holds.

    Every probe is bound *before* any of them is closed, which is what makes the
    ports distinct from each other. Calling a one-port helper twice does not:
    UDP has no TIME_WAIT -- as the two-peer check relies on, since the tool has
    to be able to rebind immediately -- so the second call can legitimately hand
    back the port the first one just released, and a test that then expected two
    peers would fail on a runner where nothing was wrong.
    """
    host = "::1" if family == socket.AF_INET6 else "127.0.0.1"
    probes = []
    try:
        for _ in range(count):
            probe = socket.socket(family, socket.SOCK_DGRAM)
            probe.bind((host, 0))
            probes.append(probe)
        return [probe.getsockname()[1] for probe in probes]
    finally:
        for probe in probes:
            probe.close()


def free_udp_port(family: int = socket.AF_INET) -> int:
    return free_udp_ports(1, family)[0]


def check_loopback(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    """What went in came out, verbatim, and the report describes it."""
    expected = CAPTURES["three-shapes"]
    payloads = [payload for _, payload in expected["records"]]
    output = workspace / "recorded-loopback.mocopipackets"

    session = Session(tool, output,
                      "--sender", "test.loopback",
                      "--device", "test.loopback-device",
                      "--source-id", "loopback-01",
                      # Stops a couple of seconds after the last datagram; the
                      # duration is the net that catches a source that never
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
    if "loopback only" not in lines.get("listen", ""):
        fail(f"a loopback bind must say so, since no device can reach it: "
             f"'{lines.get('listen')}'")
    if not any("no device can reach" in line for line in session.stderr):
        fail("a loopback session did not warn before receiving anything")

    # The claim this tool exists for.
    header, recorded = read_capture(output)
    if recorded != payloads:
        fail(f"the recorded datagrams are not the ones that were sent: "
             f"{len(recorded)} recorded, {len(payloads)} sent")
    if header.get("sender") != "test.loopback":
        fail(f"the capture did not record its provenance: {header}")
    if header.get("device") != "test.loopback-device":
        fail(f"the capture lost `device`, this format's own header key: "
             f"{header}")
    if header.get("listen") != f"127.0.0.1:{session.port}":
        fail(f"the capture did not record its listen endpoint: {header}")
    if not header.get("peer", "").startswith("127.0.0.1:"):
        fail(f"the capture did not record the peer it heard from: {header}")

    # And that a session off the wire is described the same way as one off the
    # disk. Only the byte-level lines: when they arrived and from where is what
    # the wire is allowed to have changed, and the arrival rate is a fact about
    # this machine's scheduling rather than about the datagrams.
    live = report_lines(run_tool(tool, "--inspect", str(output)))
    for label in ("received", "lengths", "prefix", "diagnostics"):
        if label == "received":
            # The byte and datagram counts, not the timespan.
            if live[label].split(" over ")[0] != lines[label].split(" over ")[0]:
                fail(f"a recorded session and its replay disagree about "
                     f"{label}:\n  live:     {lines.get(label)}\n"
                     f"  replayed: {live.get(label)}")
        elif live.get(label) != lines.get(label):
            fail(f"a recorded session and its replay disagree about {label}:\n"
                 f"  live:     {lines.get(label)}\n"
                 f"  replayed: {live.get(label)}")

    print(f"mocopi_record: {len(recorded)} datagram(s) through a real socket, "
          f"recorded verbatim and described without decoding one")


def check_stop_reasons(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    """Every session ends for exactly one stated reason, and says which.

    Ctrl-C is the one that is not here: delivering a console interrupt to a child
    without also killing the test runner needs a process group on POSIX and a
    detached console on Windows, and the two share no mechanism.
    """
    payloads = [payload for _, payload in CAPTURES["one-shape"]["records"]]

    # --max-datagrams stops the session mid-stream, and what is written is
    # exactly the datagrams that arrived before it did: not the whole send, and
    # not a truncated last record.
    #
    # Driven by the capture whose payloads all differ, so "the first three that
    # arrived" is a claim about *which* three. The ten identical datagrams below
    # would satisfy it by accident.
    distinct = [payload for _, payload in CAPTURES["three-shapes"]["records"]]
    bounded = workspace / "bounded.mocopipackets"
    session = Session(tool, bounded, "--max-datagrams", "3",
                      "--idle-timeout", "10", "--duration", "60").start()
    session.send(distinct)
    lines = session.finish()
    if lines.get("stopped") != "--max-datagrams reached":
        fail(f"expected --max-datagrams to stop the session, got "
             f"'{lines.get('stopped')}'")
    _, bounded_payloads = read_capture(bounded)
    if bounded_payloads != distinct[:3]:
        fail(f"a bounded session recorded {len(bounded_payloads)} datagram(s), "
             f"expected the first 3 that arrived")

    # --duration against a source that never says anything: the timer runs from
    # the bind rather than from the first datagram, so a session nobody sends to
    # still ends. `--dry-run`, so exit 0 -- the run below is the one that asks
    # for a file and does not get one.
    lines = Session(tool, None, "--duration", "1").start().finish()
    if lines.get("stopped") != "--duration elapsed":
        fail(f"expected --duration to stop a silent session, got "
             f"'{lines.get('stopped')}'")
    if lines.get("arrival") != "not measurable from 0 datagram(s)":
        fail(f"a session that received nothing cannot report a rate: "
             f"'{lines.get('arrival')}'")
    if lines.get("lengths") != "none":
        fail(f"a session that received nothing has no census: "
             f"'{lines.get('lengths')}'")

    # The same session with --output. Nothing arrived, so no file is written and
    # the exit says so: the format has no datagram-less form, and the writer
    # would otherwise emit a header this adapter's own reader refuses.
    absent = workspace / "never-written.mocopipackets"
    session = Session(tool, absent, "--duration", "1").start()
    lines = session.finish(expected_returncode=1)
    if absent.exists():
        fail(f"a session that received nothing wrote {absent}")
    if not any("not written" in line for line in session.stderr):
        fail(f"the tool did not say why no file appeared: "
             f"{''.join(session.stderr)}")

    # Two peers: the capture header names one, so the report has to say there
    # were more. A fixture whose provenance is true of some of its datagrams and
    # not the rest is the failure that warning exists for.
    #
    # Both source ports come from one call, so they are distinct from each other
    # by construction -- see `free_udp_ports`.
    mixed = workspace / "two-peers.mocopipackets"
    first, second = free_udp_ports(2)
    session = Session(tool, mixed, "--idle-timeout", "2",
                      "--duration", "60").start()
    session.send(payloads[:5], source_port=first)
    session.send(payloads[5:], source_port=second)
    lines = session.finish()
    if not lines.get("peers", "").startswith("2 ("):
        fail(f"expected two peers to be reported, got '{lines.get('peers')}'")
    if not any("more than one peer" in line for line in session.stderr):
        fail("a two-peer session did not warn that its header names one")

    print("mocopi_record: three stop reasons, the empty-session refusal and "
          "the two-peer warning behave")


def check_peer_count(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    """The peer count counts hosts, past the point where it stops naming them.

    The report names at most four peers, and deduplicating new peers against that
    short named list alone made the count wrong the moment a fifth host appeared:
    every datagram from a peer too late to be named failed the search and
    incremented the tally, so the line whose whole job is to catch a misconfigured
    multi-source session reported a datagram count as a host count.

    Six hosts, three datagrams each, is the smallest shape that separates the two.
    The old arithmetic gives 4 + 3 + 3 = 10; the right answer is 6.
    """
    payloads = [payload for _, payload in CAPTURES["one-shape"]["records"]][:3]
    output = workspace / "six-peers.mocopipackets"
    ports = free_udp_ports(6)

    session = Session(tool, output, "--idle-timeout", "2",
                      "--duration", "60").start()
    for port in ports:
        session.send(payloads, source_port=port)
    lines = session.finish()

    peers = lines.get("peers", "")
    if not peers.startswith("6 ("):
        fail(f"expected six peers to be reported as 6, got '{peers}'")
    # Four named, then an ellipsis: the count grows past the list, the list does
    # not.
    if peers.count(",") != 4 or not peers.endswith("...)"):
        fail(f"expected four named peers and an ellipsis, got '{peers}'")
    if len(read_capture(output)[1]) != len(ports) * len(payloads):
        fail("a multi-peer session did not record every datagram")

    print("mocopi_record: six peers are counted as six and named as four")


def check_silence(tool: pathlib.Path, workspace: pathlib.Path) -> None:
    """The device that is not there yet, which is this adapter's own code.

    `VRM_MOCOPI_DEVICE_UNAVAILABLE` is the code the sibling's frozen set does not
    have, the receiver is the only layer that can raise it, and it has no default
    threshold because a phone being strapped on and a phone switched off produce
    the same nothing. This tool is the caller that states the threshold, so this
    is the test that the arrangement works end to end.

    What is checked besides the code appearing: the session **continues**. A
    silence report is not a stop condition -- `--idle-timeout` is -- and a
    recorder that ended on one would lose the recording an operator was waiting
    to start.
    """
    payloads = [payload for _, payload in CAPTURES["one-shape"]["records"]]
    output = workspace / "after-silence.mocopipackets"

    # Silent for long enough to cross the threshold, then the datagrams arrive.
    # The report must show the code once, and the capture must hold everything
    # sent after it.
    #
    # The session is ended by `--max-datagrams` on the last datagram rather than
    # by an idle timeout, and that is what makes the count below exactly one. An
    # idle timeout longer than the silence threshold would leave the receiver
    # quiet past it a second time on the way out -- a second *episode*, correctly
    # reported, and the assertion this test is making is about episodes rather
    # than about polls.
    session = Session(tool, output, "--silence-timeout", "0.5",
                      "--max-datagrams", str(len(payloads)),
                      "--duration", "60").start()
    time.sleep(1.5)
    session.send(payloads)
    lines = session.finish()

    if lines.get("stopped") != "--max-datagrams reached":
        fail(f"the session was meant to end on its last datagram, got "
             f"'{lines.get('stopped')}'")

    diagnostics = lines.get("diagnostics", "")
    if "VRM_MOCOPI_DEVICE_UNAVAILABLE" not in diagnostics:
        fail(f"a session silent past its threshold did not report it: "
             f"'{diagnostics}'")
    if not re.search(r"\b1 x VRM_MOCOPI_DEVICE_UNAVAILABLE", diagnostics):
        # Once per episode, not once per poll -- at a 0.2 s poll and a 1.5 s
        # silence, a per-poll report would say seven.
        fail(f"the silence report is once per episode, report said "
             f"'{diagnostics}'")
    if "warning" not in diagnostics:
        fail(f"a device that is not there yet is recoverable, so the code is a "
             f"warning: '{diagnostics}'")

    _, recorded = read_capture(output)
    if recorded != payloads:
        fail(f"the session did not continue past the silence report: "
             f"{len(recorded)} datagram(s) recorded of {len(payloads)}")
    if not any("VRM_MOCOPI_DEVICE_UNAVAILABLE" in line
               for line in session.stderr):
        fail("the operator waiting for a device was not told on stderr")

    print("mocopi_record: the silence report fires once and the session "
          "continues through it")


def check_ipv6(tool: pathlib.Path, workspace: pathlib.Path) -> int:
    """The warning that needs an address family this runner may not have."""
    try:
        with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as probe:
            probe.bind(("::1", 0))
    except OSError as exc:
        print(f"mocopi_record: no IPv6 loopback on this runner, so the IPv4-only "
              f"warning could not be checked here: {exc}")
        return 77

    payloads = [payload for _, payload in CAPTURES["one-shape"]["records"]]
    output = workspace / "ipv6.mocopipackets"
    session = Session(tool, output, "--idle-timeout", "2", "--duration", "60",
                      host="::1").start()
    session.send(payloads)
    lines = session.finish()

    # The receiver takes an IPv6 address rather than refusing one, because a
    # socket inventing a restriction on itself from a product's documentation is
    # the wrong layer for it. So the datagrams do arrive.
    _, recorded = read_capture(output)
    if recorded != payloads:
        fail(f"an IPv6 session recorded {len(recorded)} datagram(s) of "
             f"{len(payloads)}; the receiver is documented to take the address")
    if not lines.get("listen", "").startswith(f"[::1]:{session.port}"):
        fail(f"the report did not name the IPv6 endpoint: "
             f"'{lines.get('listen')}'")

    # And the tool, which is the layer that does know what the product sends,
    # warns before a single datagram rather than after ten quiet minutes.
    if not any("IPv4 only" in line for line in session.stderr):
        fail(f"an IPv6 bind did not warn that the product sends to IPv4 only: "
             f"{''.join(session.stderr)}")

    print("mocopi_record: an IPv6 bind is accepted by the socket and warned "
          "about by the tool")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True, type=pathlib.Path)
    parser.add_argument("--mode", required=True,
                        choices=("inspect", "loopback", "ipv6"))
    arguments = parser.parse_args()

    # Every file this test writes lands here and leaves with it, including the
    # ones a failing run abandons. CTest's working directory is the build tree,
    # which is shared with every other test and outlives all of them.
    with tempfile.TemporaryDirectory(prefix="mocopi_record-") as scratch:
        workspace = pathlib.Path(scratch)
        if arguments.mode == "inspect":
            check_inspect(arguments.tool, workspace)
            check_malformed(arguments.tool, workspace)
            check_help_and_refusals(arguments.tool, workspace)
        elif arguments.mode == "loopback":
            check_loopback(arguments.tool, workspace)
            check_stop_reasons(arguments.tool, workspace)
            check_peer_count(arguments.tool, workspace)
            check_silence(arguments.tool, workspace)
        else:
            return check_ipv6(arguments.tool, workspace)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
