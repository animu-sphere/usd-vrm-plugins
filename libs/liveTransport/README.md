# liveTransport

`liveTransport` holds the four things every live input adapter needs and none of
them can own: a **UDP receiver**, an opt-in **datagram queue**, the
**packet-capture file format**, and the **diagnostic vehicle** an adapter
reports through.

That is the whole of its job. It knows no protocol — no OSC, no vendor grammar,
no address literal, no product name — and it holds no diagnostic **code**
([WORKSPACE.md §2](../../docs/architecture/WORKSPACE.md)).

```text
OSC / a vendor grammar / a tracker surface  = the adapter
a socket, a capture file, a diagnostic line = this library
```

## Why it exists, in one measurement

Two adapters wrote this code twice. Normalised for their vendor identifier and
stripped of comments, `PacketCapture.cpp` differed by **5 lines out of 366** and
`PacketCapture.h` by **1 out of 44**; `UdpReceiver.cpp` differed by 161 and
`UdpReceiver.h` by 49, and that gap was **four defects** the younger copy had
fixed and the older one still carried
([osc-and-vrchat-trackers.md §2](../../docs/roadmap/osc-and-vrchat-trackers.md)).
Both receivers said so in their own preambles, and both named the same trigger
for turning the repetition into a library: a **third** recorder. A third live
adapter is what made that arrive.

The four defects were merged into both adapters **before** this library existed,
in their own change with their own tests, so that a file move never carried a
fix inside it. What is here is the merged behaviour, unchanged.

## The edge set is empty

Not short — none. It links no workspace library, and that is a measurement
rather than an aspiration: the six files it was extracted from include their own
headers and the standard library and nothing else. `liveTransport_boundaries`
checks it both ways on every build, in source and against a built binary, and
the binary half has no allowlist because nothing here can drag an OpenUSD
library in.

It is **outside the aggregate product**, on the adapter side of
[§5](../../docs/architecture/WORKSPACE.md)'s split though it carries no product
name. That section's reader test is *producer-neutral **and** opens nothing*;
this library satisfies the first clause exactly as `motionBvh` does and is still
out, because the product would acquire I/O — and no tool in the product opens a
transport, which is what makes every clip in this repository reproducible by
construction.

## It reports events, not codes

A diagnostic code set is frozen per adapter, before its decoder exists, so that
the set describes a protocol's failure modes rather than whichever bug was
chased last. That freeze is per protocol by construction, so a shared receiver
cannot name a code. It reports what it **observed** — `TransportEvent::BindFailed`,
`TransportEvent::Silence` — and the adapter that knows its own vocabulary maps
the event onto its own frozen code. This is the shape `MatchSourceProfile`
already uses one layer over.

That also settles a difference the two copies carried. The mocopi receiver had a
silence timeout and the VMC one had no equivalent, and the reason was never that
silence matters less to a relay: the VMC adapter's frozen code set has no code
for it. Here the capability is unconditional and the code stays the adapter's
problem — an adapter with nothing to map `Silence` onto leaves
`silenceTimeoutSeconds` at 0, which is off, and behaves exactly as it did.

## What did not come along

`DatagramQueue` is here but **opt-in**, and that is the contract rather than a
convenience. The failure mode of an extraction is that everything one caller
needed becomes everything every caller gets. A queue exists for a consumer that
cannot poll often enough to keep a kernel receive buffer from overflowing; one
adapter met that case and the other wrote down that it had not. A caller that
polls inside its own tick never constructs one and pays for neither the mutex
nor the second copy of every datagram.

`FrameAssembler` and `SkeletonMap` are **not** here and must not arrive. The
census puts each pair further apart than either copy is long, because assembling
a frame *is* the protocol. A shared frame assembler is how three protocols
acquire one protocol's frame policy.

## The one internal header

`src/PollTimeout.h` holds the poll timeout mapping and the wake-up predicate,
and it exists because a library can hold an internal header where an adapter
cannot. Two of the four merged fixes shipped without a test — a timeout of `-1`
and one of `INT_MAX` differ only after 24.8 days, and a `POLLERR` wake-up is not
producible on three platforms from a suite that owns only its own sockets — and
writing a unit test inside one adapter would have meant giving it a surface its
sibling did not have, which is divergence in the step whose purpose was
convergence. `tests/test_poll_timeout.cpp` is that test.

## Layout

```text
include/liveTransport/Diagnostics.h    the vehicle, the severity scale, the line
include/liveTransport/PacketCapture.h  the recorded-datagram file format
include/liveTransport/UdpReceiver.h    the socket, and the opt-in queue
src/PollTimeout.h                      internal; not installed
```
