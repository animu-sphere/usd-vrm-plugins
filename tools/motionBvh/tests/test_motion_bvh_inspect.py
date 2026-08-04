#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""What `motion_bvh_inspect` reports, checked against an independent reading.

The tool's claim is that it prints the file's own words back. A test that asked
the C++ parser what a file says and then checked the tool agreed would be one
implementation agreeing with itself, so every number this test expects comes
from one of two places the tool never touches: the corpus manifest, whose
measured fields are produced by a separate Python scanner, and a reading of the
`.bvh` text done here.

The three claims:

* **the summary matches the file** -- joints, channels, frames and frame time,
  for every committed fixture, against the manifest;
* **a refused file is refused** -- the malformed half exits 1 and names the
  diagnostic the manifest expects, with no partial report on stdout;
* **the row map is right** -- the channel map, the hierarchy's column numbers
  and a printed frame all agree with the row read out of the text here, which is
  what makes `channelOffset` checkable from outside.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import subprocess
import sys

TOLERANCE = 1e-6


class Failures:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def check(self, condition: bool, message: str) -> bool:
        if not condition:
            self.messages.append(message)
        return condition

    def report(self) -> int:
        if not self.messages:
            return 0
        for message in self.messages:
            print(f"FAIL: {message}", file=sys.stderr)
        return 1


def inspect(tool: str, *arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [tool, *arguments], text=True, encoding="utf-8", errors="replace",
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def summary_fields(stdout: str) -> dict[str, str]:
    """The `key: value` block the report always opens with."""
    fields: dict[str, str] = {}
    for line in stdout.splitlines():
        if not line or line[0].isspace():
            break
        head, separator, tail = line.partition(":")
        if not separator:
            break
        fields[head.strip()] = tail.strip()
    return fields


def block(stdout: str, heading: str) -> list[str]:
    """The indented lines under the first heading starting with `heading`."""
    lines = stdout.splitlines()
    for index, line in enumerate(lines):
        if line.startswith(heading):
            body = []
            for follower in lines[index + 1:]:
                if not follower.strip():
                    break
                body.append(follower)
            return body
    return []


def read_rows(path: pathlib.Path) -> tuple[list[list[float]], float]:
    """Read the MOTION section here, without asking the tool anything.

    Deliberately tolerant in the two ways the corpus varies and the meaning does
    not: keywords in any case, and blank lines between rows.
    """
    lines = path.read_text(encoding="utf-8").splitlines()
    start = next(i for i, line in enumerate(lines)
                 if line.strip().lower() == "motion")
    frame_time = 0.0
    rows: list[list[float]] = []
    for line in lines[start + 1:]:
        stripped = line.strip()
        if not stripped:
            continue
        lowered = stripped.lower()
        if lowered.startswith("frames"):
            continue
        if lowered.startswith("frame time"):
            frame_time = float(stripped.split(":", 1)[1])
            continue
        rows.append([float(token) for token in stripped.split()])
    return rows, frame_time


def frame_values(stdout: str) -> tuple[list[str], list[float]]:
    """The channel names and values of a printed frame, in row order."""
    names: list[str] = []
    values: list[float] = []
    for line in block(stdout, "frame "):
        for token in line.split():
            name, separator, value = token.partition("=")
            if separator:
                names.append(name)
                values.append(float(value))
    return names, values


def check_summaries(tool: str, corpus: pathlib.Path,
                    failures: Failures) -> None:
    """Every fixture, against the manifest that measured it independently."""
    manifest = json.loads(
        (corpus / "manifest.json").read_text(encoding="utf-8"))
    generated = corpus / "generated"

    for fixture in manifest["fixtures"]:
        path = generated / fixture["file"]
        name = fixture["file"]
        result = inspect(tool, str(path))

        if fixture["status"] == "refused":
            failures.check(
                result.returncode == 1,
                f"{name}: expected exit 1, got {result.returncode}")
            failures.check(
                f"[{fixture['diagnostic']}]" in result.stderr,
                f"{name}: expected {fixture['diagnostic']}, got "
                f"{result.stderr.strip()!r}")
            # A file read whole or not at all: a refusal that had printed half a
            # summary would read as a fact about the file.
            failures.check(
                result.stdout == "",
                f"{name}: refused, but printed a report: {result.stdout!r}")
            continue

        if not failures.check(
                result.returncode == 0,
                f"{name}: expected exit 0, got {result.returncode}: "
                f"{result.stderr.strip()!r}"):
            continue

        fields = summary_fields(result.stdout)
        failures.check(fields.get("source") == str(path),
                       f"{name}: source is {fields.get('source')!r}")
        for key, field in (("joints", "joints"), ("channels", "channels"),
                           ("frames", "frames")):
            reported = fields.get(field, "").split(" ")[0]
            failures.check(
                reported == str(fixture[key]),
                f"{name}: {field} is {reported!r}, manifest says "
                f"{fixture[key]}")
        frame_time = float(fields.get("frameTime", "nan").split(" ")[0])
        failures.check(
            math.isclose(frame_time, fixture["frameTime"], rel_tol=TOLERANCE,
                         abs_tol=TOLERANCE),
            f"{name}: frameTime is {frame_time}, manifest says "
            f"{fixture['frameTime']}")


def check_hierarchy(tool: str, generated: pathlib.Path,
                    failures: Failures) -> None:
    """Declaration order, depth, the End Site terminators, and the columns."""
    path = generated / "valid-nested-joints.bvh"
    result = inspect(tool, str(path), "--hierarchy")
    if not failures.check(result.returncode == 0,
                          f"--hierarchy exited {result.returncode}"):
        return

    lines = block(result.stdout, "hierarchy")
    joints = [line for line in lines if "[" in line.split()[0]]
    ends = [line for line in lines if line.strip().startswith("end site")]
    failures.check(len(joints) == 4, f"expected 4 joints, got {len(joints)}")
    failures.check(len(ends) == 2, f"expected 2 End Sites, got {len(ends)}")

    # Depth-first declaration order, indented by depth: Hips, Spine, Head, then
    # back out to LeftUpLeg. A parser that walked levels instead would put
    # LeftUpLeg third and the indentation would not come back out.
    indents = [len(line) - len(line.lstrip()) for line in joints]
    names = [line.split()[1] for line in joints]
    failures.check(names == ["Hips", "Spine", "Head", "LeftUpLeg"],
                   f"declaration order is {names}")
    failures.check(indents == [2, 4, 6, 4], f"indentation is {indents}")
    failures.check(
        summary_fields(result.stdout).get("depth") == "3 level(s)",
        f"depth is {summary_fields(result.stdout).get('depth')!r}")

    # Columns follow declaration order across the whole hierarchy, which is the
    # only thing that maps a row's numbers back to joints.
    columns = [int(line.split("column=")[1]) for line in joints]
    failures.check(columns == [0, 6, 9, 12], f"columns are {columns}")

    # `CHANNELS 0` is a joint the file animates nothing about. It still appears.
    static = inspect(tool, str(generated / "valid-static-joint.bvh"),
                     "--hierarchy")
    failures.check("channels=0" in static.stdout,
                   "a joint with CHANNELS 0 lost its line")


def check_row_map(tool: str, generated: pathlib.Path,
                  failures: Failures) -> None:
    """The channel map, a printed frame, and the file's own row must agree."""
    path = generated / "valid-nested-joints.bvh"
    rows, frame_time = read_rows(path)

    result = inspect(tool, str(path), "--channel-map", "--frame", "2")
    if not failures.check(result.returncode == 0,
                          f"--channel-map exited {result.returncode}"):
        return

    mapped = block(result.stdout, "channel map")
    failures.check(len(mapped) == len(rows[0]),
                   f"channel map has {len(mapped)} entries for "
                   f"{len(rows[0])} values")
    failures.check(
        [int(line.split()[0]) for line in mapped] == list(range(len(mapped))),
        "channel map columns are not 0..n-1 in order")
    failures.check(mapped[0].split()[1:] == ["[0]", "Hips.Xposition"],
                   f"column 0 is {mapped[0].strip()!r}")
    failures.check(mapped[-1].split()[1:] == ["[3]", "LeftUpLeg.Yrotation"],
                   f"the last column is {mapped[-1].strip()!r}")

    # The frame block, read back and compared value by value with the row this
    # test read out of the text itself.
    names, values = frame_values(result.stdout)
    failures.check(len(values) == len(rows[2]),
                   f"frame 2 printed {len(values)} values for "
                   f"{len(rows[2])} in the file")
    for index, (printed, actual) in enumerate(zip(values, rows[2])):
        failures.check(
            math.isclose(printed, actual, rel_tol=TOLERANCE, abs_tol=TOLERANCE),
            f"frame 2 column {index} printed {printed}, file says {actual}")

    # And the channel each value was printed under is the channel the map put
    # in that column -- the two blocks are derived from the same declaration
    # order, and this is what would catch them drifting apart.
    failures.check(
        names == [line.split(".")[-1] for line in mapped],
        "the frame's channel names do not follow the channel map")

    header = next(line for line in result.stdout.splitlines()
                  if line.startswith("frame "))
    failures.check(f"t={2 * frame_time:.9g}" in header,
                   f"frame 2's timestamp is wrong: {header!r}")

    # Out of range is a wrong command rather than a bad file, and the file's own
    # frame count is what makes it wrong -- so it is reported after the parse.
    beyond = inspect(tool, str(path), "--frame", "3")
    failures.check(beyond.returncode == 2,
                   f"--frame 3 of 3 exited {beyond.returncode}")


def check_ranges(tool: str, generated: pathlib.Path,
                 failures: Failures) -> None:
    """Per-column smallest and largest, against the same independent reading."""
    path = generated / "valid-nested-joints.bvh"
    rows, _ = read_rows(path)

    result = inspect(tool, str(path), "--ranges")
    if not failures.check(result.returncode == 0,
                          f"--ranges exited {result.returncode}"):
        return

    lines = block(result.stdout, "channel ranges")
    failures.check(len(lines) == len(rows[0]),
                   f"ranges cover {len(lines)} of {len(rows[0])} columns")
    for line in lines:
        column = int(line.split()[0])
        smallest = float(line.split("min=")[1].split()[0])
        largest = float(line.split("max=")[1].split()[0])
        values = [row[column] for row in rows]
        failures.check(
            math.isclose(smallest, min(values), rel_tol=TOLERANCE,
                         abs_tol=TOLERANCE)
            and math.isclose(largest, max(values), rel_tol=TOLERANCE,
                             abs_tol=TOLERANCE),
            f"column {column}: reported {smallest}..{largest}, file has "
            f"{min(values)}..{max(values)}")

    # A hierarchy with no motion has nothing to measure, and says so rather than
    # printing a table of zeroes.
    empty = inspect(tool, str(generated / "valid-empty-motion.bvh"), "--ranges")
    failures.check(empty.returncode == 0,
                   f"an empty MOTION section exited {empty.returncode}")
    failures.check("nothing to measure" in empty.stdout,
                   "an empty MOTION section printed a range table")


def check_repeated_names(tool: str, generated: pathlib.Path,
                         failures: Failures) -> None:
    """A repeated joint name is surfaced, because a profile matches by name."""
    repeated = inspect(tool, str(generated / "valid-duplicate-joint-names.bvh"))
    fields = summary_fields(repeated.stdout)
    failures.check(fields.get("repeated") == "Bone (2)",
                   f"repeated names reported as {fields.get('repeated')!r}")

    # And a file whose names are distinct does not carry the line at all.
    distinct = inspect(tool, str(generated / "valid-nested-joints.bvh"))
    failures.check("repeated" not in summary_fields(distinct.stdout),
                   "a file with distinct joint names reported repetitions")


def check_limits(tool: str, generated: pathlib.Path,
                 failures: Failures) -> None:
    """The parser's limits are the caller's, and the caller is this tool.

    Lowering one is how the refusal is checked without committing a fixture with
    a million frames in it (`BvhParser.h`).
    """
    path = str(generated / "valid-nested-joints.bvh")
    for flag, value in (("--max-depth", "2"), ("--max-joints", "2"),
                        ("--max-frames", "2")):
        refused = inspect(tool, path, flag, value)
        failures.check(refused.returncode == 1,
                       f"{flag} {value} exited {refused.returncode}")
        failures.check("[VRM_BVH_PARSE_FAILED]" in refused.stderr,
                       f"{flag} {value} refused with {refused.stderr.strip()!r}")
        # Raised past what the file needs, the same file reads.
        accepted = inspect(tool, path, flag, "4096")
        failures.check(accepted.returncode == 0,
                       f"{flag} 4096 exited {accepted.returncode}")


def check_command_errors(tool: str, generated: pathlib.Path,
                         failures: Failures) -> None:
    """Exit 2 is a wrong command; exit 1 is a refused file. They never swap."""
    for arguments, code in (
            ([], 2),
            (["--hierarchy"], 2),
            ([str(generated / "valid-minimal-root.bvh"), "--hierarchi"], 2),
            ([str(generated / "valid-minimal-root.bvh"), "--frame"], 2),
            ([str(generated / "valid-minimal-root.bvh"), "--frame", "-1"], 2),
            ([str(generated / "valid-minimal-root.bvh"),
              str(generated / "valid-static-joint.bvh")], 2),
            ([str(generated / "no-such-file.bvh")], 1)):
        result = inspect(tool, *arguments)
        failures.check(
            result.returncode == code,
            f"{arguments} exited {result.returncode}, expected {code}")

    missing = inspect(tool, str(generated / "no-such-file.bvh"))
    failures.check("[VRM_BVH_PARSE_FAILED]" in missing.stderr,
                   f"a missing file reported {missing.stderr.strip()!r}")

    helped = inspect(tool, "--help")
    failures.check(helped.returncode == 0,
                   f"--help exited {helped.returncode}")
    failures.check("motion_bvh_inspect" in helped.stdout,
                   "--help printed nothing useful")


def check_determinism(tool: str, generated: pathlib.Path,
                      failures: Failures) -> None:
    """Two runs over one file are byte-identical, or no golden test can exist."""
    path = str(generated / "valid-nested-joints.bvh")
    first = inspect(tool, path, "--all", "--frame", "1")
    second = inspect(tool, path, "--all", "--frame", "1")
    failures.check(first.stdout == second.stdout,
                   "two runs over the same file printed different reports")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", required=True)
    parser.add_argument("--corpus", required=True, type=pathlib.Path)
    arguments = parser.parse_args()

    corpus = arguments.corpus
    generated = corpus / "generated"
    failures = Failures()

    check_summaries(arguments.tool, corpus, failures)
    check_hierarchy(arguments.tool, generated, failures)
    check_row_map(arguments.tool, generated, failures)
    check_ranges(arguments.tool, generated, failures)
    check_repeated_names(arguments.tool, generated, failures)
    check_limits(arguments.tool, generated, failures)
    check_command_errors(arguments.tool, generated, failures)
    check_determinism(arguments.tool, generated, failures)

    if failures.report() != 0:
        return 1
    print("motion_bvh_inspect: every fixture reported as the manifest and the "
          "file text say")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
