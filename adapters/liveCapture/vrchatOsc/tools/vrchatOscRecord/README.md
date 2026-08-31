# vrchat_osc_record

Records a live VRChat OSC session to a `vrchat-osc-packet-capture` file, and says
what arrived. `--inspect` reports on a recorded capture with no socket at all,
adds the address inventory the recording path deliberately does not, and — with
an operator's assignment — turns the recording into a canonical
`motion-capture-trace` the product replays.

```text
vrchat_osc_record --output session.vrchatoscpackets --sender mocopi-app-2.7.2 \
                  --device mocopi-6sensor-iphone16 --source-id neutral-standing-01
vrchat_osc_record --inspect session.vrchatoscpackets
vrchat_osc_record --inspect session.vrchatoscpackets \
                  --export-trace session.trace \
                  --assign "1=hips 2=leftFoot 3=rightFoot head=head"
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

## Where a session leaves this adapter: `--export-trace`

```text
session.vrchatoscpackets → vrchat_osc_record --inspect --export-trace → session.trace
                         → motion_capture → motion_retarget → a rig
```

The trace is the hand-off, and it is a *file* rather than a call: no tool in the
aggregate product links an adapter, so `motion_capture` replays what this writes
exactly as it replays a `.vrma`-derived clip and learns nothing about VRChat OSC —
or about trackers — in the process (WORKSPACE.md §2).

**It runs against a file, not against a socket**, and the refusal at the prompt
says so. A recording here runs no decoder at all, which is what makes the capture
the evidence: the file is worth the same whatever the decoder later turns out to
be wrong about, and a trace exported from committed bytes is the same trace on
any machine.

### `--assign` is required and will never have a default

A tracker index is not a body role. `/tracking/trackers/1` is whatever strap the
operator put there, and nothing in this repository is entitled to guess which —
so the statement is a flag, it is mandatory, and a near miss is refused rather
than resolved:

```text
--assign "1=hips 2=leftFoot 3=rightFoot head=head"
```

Pairs separate on whitespace or commas, `#` runs to end of line, and the eleven
region names are `head chest hips leftElbow leftHand leftKnee leftFoot
rightElbow rightHand rightKnee rightFoot`. `leftFoot` works and `LeftFoot` does
not, which is the rule `motion_bvh_convert` applies to a profile id and is worth
more here: a parser that resolved `lfoot` would be the first line of the
automatic assignment this milestone deliberately does not build.

`--unplaced` answers the observed tracker no statement places — `refuse` (the
default) stops, because it will still be unplaced next frame; `ignore` is the
operator saying they know; `hold` waits for a rig that is still coming up.

### What the solve does, and where it stops

An observed **orientation** becomes the local rotation of the bone its region
names. An observed **position** is consumed in exactly one place — the hips,
where it becomes root motion — and every other one is reported *unused*. A joint
nobody observed stays at **rest**.

That last sentence is the release's claim rather than an omission: consuming a
hand's position is IK, IK needs limb lengths, and limb lengths belong to a target
rig this layer does not have and may not acquire. So this path delivers tracker
**input** reaching the canonical layer, not tracker-driven full-body motion. The
knees and the elbows are the visible edge of it — they are bound, they reach no
bone, and they are listed under `unsolved` so the operator is told which straps
drive nothing.

The report says all of it per region, and it is printed whether or not a trace
was written — a session that solved nothing is the one an operator needs it for,
because it is what tells a misspelled tracker identity from a strap nobody wore:

```text
solve: 12 of 12 frame(s)
  placed: head 12, hips 12, leftFoot 12, rightFoot 12
  unsolved: none
  withoutRotation: none
  positionsUnused: head 12, leftFoot 12, rightFoot 12
  stated but absent: none
  observed but unplaced: none
```

`--no-root-motion` turns the hips *position* off for a session whose hips
position is not trusted. The hips rotation is authored either way: a body that
turned turned whatever the translation is worth.

### One trace is one session, for a different reason than the siblings'

A capture the sender restarted during holds two, and exporting it is refused
until `--source-session` names one. Both sibling tools refuse the same thing
because their sender's own clock goes back to zero on a restart, so the two
halves overlap in time.

**That is not true here and the test asserts that it is not.** This wire carries
no sender clock — its tracker addresses are three floats and no timestamp — so
the only clock there is belongs to the receiver, and it runs forward across a
restart. What is refused instead is a continuity of *space*: the two halves are
two senders, VRChat's tracking space is established by a calibration the
receiving application performs, and a new session is a new calibration nothing
here can relate to the old one.

## Tests

Five CTest names, none of which needs a VRChat client or a sender application:
`vrchat_osc_record_inspect` (no socket), `vrchat_osc_record_loopback` (loopback
sockets), `vrchat_osc_record_ipv6` (skips where the host has no IPv6 loopback),
`vrchat_osc_record_export` (the solve and the trace, against the committed
corpus) and `vrchat_osc_record_endToEnd` (a session onto a rig, where the build
tree also carries the product's tools).

The loopback name is where VRC-0's done-condition is asserted end to end: Python
sends a series of deliberately awkward payloads — empty, unaligned, spanning the
printable range and out of it — the tool records them, and an **independent**
capture parser in the test reads the file back and compares byte for byte. A
writer and a reader that agreed with each other and with nothing else would fail
it.

The end-to-end name is the one that says a session *arrived*, and it says it as a
partition rather than as a count: on the `rig-motion` fixture, four joints move
and fourteen hold their rest pose **exactly** — not within a tolerance, because
nothing authored them and the retarget computes the same product at every sample.
Four of those fourteen sit between a driven hip and a driven foot, which is where
a solve that had quietly started estimating would show up first.
