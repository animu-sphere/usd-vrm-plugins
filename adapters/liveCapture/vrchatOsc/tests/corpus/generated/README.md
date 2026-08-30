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

| Capture | What it pins | `observed` |
| --- | --- | :-: |
| `three-trackers-58hz` | ordinary traffic: three numbered trackers, a named head, a frame lost whole and an address lost on its own | session |
| `one-tracker` | values, exactly: every number is representable in binary32, so *nothing is converted on the way through* is an equality rather than a tolerance | derived |
| `eight-trackers` | the whole numbered surface, 1–8 | unobserved |
| `head-absent` | a session with no head reference is well-formed | derived |
| `position-only` | a position message is complete on its own | derived |
| `rotation-only` | a rotation is three floats, which is what makes it Euler-shaped | derived |
| `tracker-dropout` | an identity that stops mid-session, indistinguishable at this layer from a lost packet | derived |
| `duplicate-and-reordered` | the send order reversed, and one address sent twice with different values | unobserved |
| `bundled-frame` | a whole frame in one OSC bundle, and the `bundled` flag a decoder forwards | unobserved |
| `mixed-traffic` | VRChat's wider surface beside tracker data, plus four identities this adapter cannot read | unobserved |
| `malformed-packets` | nine ways a datagram is not OSC, refused whole, and one that is fine | unobserved |
| `malformed-forms` | valid OSC at known addresses with arguments that are not those addresses' | unobserved |

**`observed` is a field in [the manifest](manifest.json), not a footnote**, and
it has three values because a boolean was not honest enough:

- **`session`** — the recorded session carried this shape. **One capture does.**
- **`derived`** — every address, type tag and ordering in it is one the session
  carried, recombined into an arrangement it did not: one tracker alone, no
  head, a single channel sustained, a permanent dropout. Five are. The elements
  are evidence; the arrangement is not.
- **`unobserved`** — it carries something the session never sent at all. Six
  are, each with its reason in `pins`: the numbered surface, because refusing
  4–8 would call a legal address a protocol violation; a bundle, because "no
  sender bundles" must not quietly become "this cannot read one"; a reordered or
  duplicated frame, which that sender's 99.7 %-consistent cycle argues against
  rather than merely omits; VRChat's wider surface; and malformed traffic,
  because port 9000 is well known and anything on the network may send to it.

The ratio is the thing worth seeing: **this protocol's evidence here is one
measured arrangement**, and the other eleven fixtures are constructed from it or
from the specification. A corpus that cannot say how far a recording stands
behind each of its fixtures is one a later reader has to guess about.

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
   its `tags` and its `observed` (`session`, `derived` or `unobserved`) — the
   measured fields are filled in for you.
3. Add a row to `kExpected` in
   [`tests/test_tracker_message.cpp`](../../test_tracker_message.cpp). A capture
   with no expectation **fails** that test rather than being skipped, which is
   what stops a fixture being added to the corpus and decoded by nobody.
4. Run `python adapters/liveCapture/vrchatOsc/tools/generate_packets.py`.
