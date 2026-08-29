# vrchat_osc_record

Records a live VRChat OSC session to a `vrchat-osc-packet-capture` file, and says
what arrived. `--inspect` reports on a recorded capture with no socket at all,
and adds the address inventory the recording path deliberately does not.

```text
vrchat_osc_record --output session.vrchatoscpackets --sender mocopi-app-2.7.2 \
                  --device mocopi-6sensor-iphone16 --source-id neutral-standing-01
vrchat_osc_record --inspect session.vrchatoscpackets
```

## Recording decodes nothing; reading a file does

The live report says what a socket can see — how much arrived, from whom, how
fast, in how many distinct lengths, and which leading bytes every datagram
shares. It does not say what any of it means, and that is the tool's design
rather than its current state.

The reason is worth stating because this protocol's receiving end is *published*
and the other recorder in this repository's protocol is not. A specification says
what a **receiver** must accept; what a **sender** sends is a measurement. Sony's
help pages list `VRChat (OSC)` as a mocopi transfer format and name VRChat's
port — that is a menu entry, and this repository does not infer a packet shape
from one.

So a report that grouped datagrams by the addresses a *document* predicted could
have been written on day one, and it would have been the first thing anybody read
off a real session with every number in it conditional on an assumption nobody
had tested.

**`--inspect` groups by address now, and it is not that report.** It decodes OSC's
grammar — where an address ends, what its type tags are — and counts what it
finds. It has no list of addresses it expects, so an address nobody predicted
appears as a row rather than as a zero, and one address sent with two different
type tag strings appears as two rows rather than as an average. That is VRC-1's
measurement, and what it still needs is an operator and a device.

It is on the file path and not on the socket path deliberately. A recorder's job
is to obtain bytes without having an opinion about them, so that the file is
worth the same whatever the decoder later turns out to be wrong about; reading
that file is a separate act, and it is repeatable.

What the live report says is enough to tell whether a session is worth keeping.
"3 distinct lengths, 96 of them 68 bytes" is a statement about the envelope, and
a capture whose every datagram is the same length recorded one kind of packet —
a decoder built from it would meet the second kind for the first time in
production.

The `prefix:` line earns its place twice on this wire. OSC addresses are ASCII
and lead the packet, so the ASCII gutter beside the hex reads as text — which is
how a reviewer sees what a sender is talking about without this tool having
parsed anything.

## Provenance is the operator's, and `--device` matters here

`--sender`, `--device` and `--source-id` become capture header values and are
never inferred from traffic. The corpus check refuses a committed fixture that
carries neither a sender nor a source id, so the tool warns at the write — the
operator who can still supply them is the one who just ran the session.

`--device` is not required and is asked for anyway, for a reason this wire has
and a native one does not: a VRChat OSC stream is **relayed**. `--sender` names
the application that re-expressed somebody else's tracking, so `--device` is the
only place the thing that was actually measured can appear — and without it a
capture cannot answer whether one physical session observed four ways agrees with
itself.

The format carries one whitespace-delimited token per header key, so a value with
a space in it is refused at the prompt rather than written into a file this
adapter's own reader would then refuse.

## Stopping

A session always has at least one stop condition: `--max-datagrams` defaults to a
million, and `--duration`, `--idle-timeout` and Ctrl-C are there when a session
wants a different one. The report names which of them ended the run, because a
capture cannot tell afterwards.

Two exit codes carry meaning a script can read. **2** is a mistake at the prompt,
before a socket was opened or a byte read. **1** is a session that ran and did not
complete — the socket failed, or nothing arrived at all.

A session that received nothing writes **no file** and exits 1. The format has no
datagram-less form: the writer would emit a header and stop, and this adapter's
reader refuses the result. Leaving that on disk would hand an operator an
artifact their own tools reject, and they would find out at the point they tried
to use it.

## Notes an operator gets before the first datagram

Both are knowable at the bind, and an operator who learns after ten minutes that
nothing could have arrived has lost the ten minutes.

- **Loopback only.** Reported as a fact, not a verdict: a sender on this machine
  is an ordinary arrangement for this protocol. (The mocopi recorder warns
  instead, because that vendor documents `localhost` as unsupported.)
- **An IPv6 endpoint**, warned about: a sender configured for VRChat is aimed at
  an IPv4 address. The receiver deliberately does not *refuse* one — a socket
  inventing a restriction on itself from an application's configuration is the
  wrong layer for it — so this tool is where the warning belongs, and
  `vrchat_osc_record_ipv6` is what makes that split a fact.

## Waiting for a sender that is not running yet

`--silence-timeout S` reports `VRM_VRCHAT_OSC_SOURCE_TIMEOUT` after S quiet
seconds, once per episode, and keeps listening. It has no default one layer down
because how long a sender may reasonably take to start is a property of the
session, and this tool is where a session is stated. It does not stop the
recording — `--idle-timeout` is the flag that does.

## Tests

Three CTest names, none of which needs a VRChat client, a sender application, or
a committed fixture: `vrchat_osc_record_inspect` (no socket),
`vrchat_osc_record_loopback` (loopback sockets), and `vrchat_osc_record_ipv6`
(skips where the host has no IPv6 loopback).

The loopback name is where VRC-0's done-condition is asserted end to end: Python
sends a series of deliberately awkward payloads — empty, unaligned, spanning the
printable range and out of it — the tool records them, and an **independent**
capture parser in the test reads the file back and compares byte for byte. A
writer and a reader that agreed with each other and with nothing else would fail
it.
