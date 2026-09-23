# Capture traces this workspace still replays

Three `motion-capture-trace` fixtures, kept here because `motion_capture`'s own
replay suite reads them and nothing else in this repository writes one any more.

The corpus they came from is `usd-motion-plugins`'
[`motionRecording`](https://github.com/animu-sphere/usd-motion-plugins/blob/main/libs/motionRecording/tests/corpus),
and the format is documented on that package's
[`CaptureTrace.h`](https://github.com/animu-sphere/usd-motion-plugins/blob/main/libs/motionRecording/include/motionRecording/CaptureTrace.h):
line-oriented text, one `t` block per frame, deterministic to six decimals.
These three files are copies at the bytes the corpus had when the library left
(MIG-2, 2026-09-21) and are **inputs to a consumer's test**, not a corpus: a
fixture that pins the format belongs with the reader, and the reader is a
package now.

| Fixture | What the replay suite asks it |
| --- | --- |
| `walk-clean-30hz.trace` | that a lagged delivery still resolves to the same clip |
| `walk-dropout-30hz.trace` | the two missing-joint policies, over a gap a producer really left |
| `walk-jitter-30hz.trace` | that `--normalize` is idempotent on a committed trace |

**Two format versions, on purpose.** `walk-jitter-30hz.trace` is **version 4**,
because the suite requires it to be what the consumed writer emits -- canonical
form is a property of the writer, and the writer moved. The other two stay at
**version 3**, the version the corpus had when it left, and that is the second
thing this directory proves: the consumed reader still accepts a trace written
before `sequence` and `sourceTime` existed.

A third reason they are here rather than fetched: the replay is the one test
that proves this repository can still consume a live session it no longer
records, and a test that needs a network to run proves it less often.
