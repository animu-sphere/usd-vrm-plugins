# osc

`osc` decodes the **OSC 1.0 wire format** and does nothing with it: packets and
bundles, bundle flattening into wire order, addresses, type tags, arguments, and
a refusal that names the byte and the address it refused at.

That is the whole of its job. It knows no address *semantics* — `/VMC/...`,
`/tracking/...` and `/avatar/...` are all just addresses here — no bone name, no
tracker role, no coordinate convention, no product name
([WORKSPACE.md §2](../../docs/architecture/WORKSPACE.md)).

```text
OSC wire format       = this library
OSC address semantics = the adapter
```

## Why it exists, and why it did not exist sooner

The decoder was written inside `vrmAdapterVmc` and stayed there through two
releases, because it had one consumer. A library extracted on the strength of
one caller is a library shaped like that caller — the only evidence that a
surface is protocol-neutral is a second caller that never says `VMC`
([the OSC track §3.1](../../docs/roadmap/osc-and-vrchat-trackers.md)).

That caller was written first and measured. An address inventory of a VRChat OSC
session, decoding through the VMC-owned decoder without moving it, needed **five
VMC tokens** — one include path and four namespace qualifications — plus the
export macro on its compile line. No VMC address literal, no bone name, no
message type, no skeleton map. The plan had predicted exactly three couplings:
the namespace, the export macro, and the diagnostic code. The measurement found
exactly those three, and the move removes all three.

## A refusal carries no code

`DecodeOscPacket` fills an `OscDecodeError` with a **subject** — the offending
address, where one had been read — and a **detail** ending in the byte it went
wrong at. There is no code in it, because a diagnostic code set is frozen per
protocol before its decoder exists, which makes it an adapter's property and
never a shared leaf's.

There is no neutral event *enum* either, where the transport leaf beside this
one has `TransportEvent`. The difference is real rather than stylistic: that
library's receiver raises two events a caller must tell apart, and this one
makes a single distinction — a datagram is decodable OSC or it is not. Three
invented neutral names would have been mapped straight back onto one adapter
code by every caller, and believed by the next reader
([the OSC track §8](../../docs/roadmap/osc-and-vrchat-trackers.md)).

## The edge set is empty, and emptier than the transport leaf's

Not short — none, `liveTransport` included: a decoder that reads no socket needs
nothing a transport owns, and the two are siblings rather than a stack. It also
links no *platform* library, which `liveTransport` does — no socket, no
threading primitive — so `osc_boundaries` has an allowlist with no exception in
it.

That check reads `tests/` as well as `include/` and `src/`, which is the one
place it is stricter than its sibling's. A decoder's payloads all need *some*
address, and the shortest path is to paste one off a real session: the suite
that moved here did exactly that, and every sample address was replaced on the
way — at identical length, so the byte offsets it asserts are the same numbers
they were.

## Tests

`osc_oscPacket` is the suite that OSC-0 froze beside the decoder before anything
moved, minus the corpus half that reads an adapter's capture format over an
adapter's fixtures. That half stayed with those fixtures and still runs, so the
same decoder is still checked against recorded bytes as well as against
hand-built ones.

`osc_boundaries` checks the leaf boundary in source and against a built binary.
