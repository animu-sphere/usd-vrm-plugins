# Generated fixtures

Protocol shapes, written by
[`tools/generate_packets.py`](../../../tools/generate_packets.py), committed, and
runnable in CI with no hardware, no device and no VRChat client. Re-checked by
`vrmAdapterVrchatOsc_packetGen`, so a hand-edited fixture cannot stay green
while the generator no longer reproduces it.

**They are written from a measurement, not from the specification.** VRChat's
tracking surface is published, which made writing these from the document
tempting and is exactly the reason not to: a specification says what a *receiver*
must accept, and this corpus is evidence about what a *sender* sends. The
shapes come from
[report 02](../../../../../../docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md)
— eight addresses, every one `,fff`, one message per datagram, no bundles,
rotation before position with the head leading a fixed eight-datagram cycle,
~58 Hz emitted with about a third of the frames lost whole, and a single-address
loss falling 96 % on `/tracking/trackers/1/rotation`.

| Capture | What it pins | Observed |
| --- | --- | :-: |
| `three-trackers-58hz` | ordinary traffic: three numbered trackers, a named head, a frame lost whole and an address lost on its own | yes |
| `one-tracker` | values, exactly: every number is representable in binary32, so *nothing is converted on the way through* is an equality rather than a tolerance | yes |
| `eight-trackers` | the whole numbered surface, 1–8 | **no** |
| `head-absent` | a session with no head reference is well-formed | yes |
| `position-only` | a position message is complete on its own | yes |
| `rotation-only` | a rotation is three floats, which is what makes it Euler-shaped | yes |
| `tracker-dropout` | an identity that stops mid-session, indistinguishable at this layer from a lost packet | yes |
| `duplicate-and-reordered` | the send order reversed, and one address sent twice with different values | **no** |
| `bundled-frame` | a whole frame in one OSC bundle, and the `bundled` flag a decoder forwards | **no** |
| `mixed-traffic` | VRChat's wider surface beside tracker data, plus four identities this adapter cannot read | **no** |
| `malformed-packets` | nine ways a datagram is not OSC, refused whole, and one that is fine | **no** |
| `malformed-forms` | valid OSC at known addresses with arguments that are not those addresses' | **no** |

**"Observed" is a field in [the manifest](manifest.json), not a footnote.** A
capture marked `false` carries a shape the recorded session never emitted, and
each says why in its `pins`: the numbered surface because refusing 4–8 would
call a legal address a protocol violation, a bundle because "no sender bundles"
must not quietly become "this cannot read one", and malformed traffic because
port 9000 is well known and anything on the network may send to it. A corpus
that cannot say which of its shapes were seen is one a later reader has to guess
about.

## What these do not describe

No real person's motion. The tracker values are invented, name **no body
region** — which tracker is on which part of a body is an assignment policy
outside this adapter — and assert no unit and no basis, because what space the
numbers are in is VRC-3's to establish against a recorded rest pose rather than
from the documentation.

Nor a second sender: the only VRChat OSC sender measured to date is one mobile
application in its `VRChat (OSC)` transfer format. A VRChat client itself has
never been recorded here and is never a test dependency.

## Adding one

1. Add a builder to `tools/generate_packets.py` and register it in `CAPTURES`.
2. Add an entry to [`manifest.json`](manifest.json) with its `file`, its `pins`,
   its `tags` and its `observed` — the measured fields are filled in for you.
3. Add a row to `kExpected` in
   [`tests/test_tracker_message.cpp`](../../test_tracker_message.cpp). A capture
   with no expectation **fails** that test rather than being skipped, which is
   what stops a fixture being added to the corpus and decoded by nobody.
4. Run `python adapters/liveCapture/vrchatOsc/tools/generate_packets.py`.
