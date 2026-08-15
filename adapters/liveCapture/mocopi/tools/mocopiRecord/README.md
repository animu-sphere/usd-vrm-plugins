# mocopi_record

The mocopi adapter's CLI, and the tool that obtains the bytes every layer above
the transport will be built from.

```text
UDP datagram → mocopi-packet-capture file
             → a report about the envelope, and nothing about the contents
```

**It decodes nothing, and that is the design rather than a stage of
completion.** The vendor documents the transport — UDP, port 12351 by default,
IPv4 only, unencrypted — and states nothing about the packet structure, so there
is no specification to write a corpus from and exactly one way to obtain one
without guessing: receive it. The receiver landed before this tool for that
reason ([`UdpReceiver.h`](../../include/vrmAdapterMocopi/UdpReceiver.h)); this is
the consumer it was waiting for, and
[the plan](../../../../../docs/roadmap/adapters-mocopi-vmc-ardy.md) names it in
Milestone D as the next code after it.

Every other recorder in this repository turns a session into a file so a decoder
can be *tested*. This one turns a session into a file so a decoder can be
*written*.

## The report says what a socket can see

The sibling tool has five layers to gather from and answers four questions —
is anything arriving, does it decode, does it become motion, what went wrong.
Two of those have no answer here, and the discipline this tool needs is that it
must not invent them. A field read at the wrong offset produces plausible
numbers, and a guess that reached a committed fixture's provenance is a guess
that survives longest and is questioned least
([BVH-0](../../../../../docs/roadmap/recorded-motion-sources.md#9-milestones)).

So three questions, and every number in the answers is a property of the
datagram *envelope*:

The counts below are a real session's. The endpoints are not, and neither is the
tail of the prefix: a capture's `peer` is somebody's LAN address and the header
carries an eight-byte field of unknown meaning that may well identify a device, so
this sample stops at the end of the first chunk, which is protocol constants
throughout.

```text
listen:      0.0.0.0:12351, receive buffer 65536 bytes
received:    4529 datagram(s), 7273365 byte(s) over 69.7354 s
peers:       1 (192.168.0.20:52001)
arrival:     64.9311 Hz mean, interval 2e-06-0.19366 s, over 69.7354 s of receive clock
lengths:     2 distinct in 4529 datagram(s)
             4509 of 1605 byte(s), 20 of 1821
prefix:      43 byte(s) common to every datagram:
             23 00 00 00 68 65 61 64 12 00 00 00 66 74 79 70  |#...head....ftyp|
             73 6f 6e 79 20 6d 6f 74 69 6f 6e 20 66 6f 72 6d  |sony motion form|
             61 74 01 00 00 00 76 72 73 6e 01                 |at....vrsn.|
diagnostics: none
stopped:     --duration elapsed
```

Two of those lines are the first sentences about this protocol anything in this
repository has been able to say, and both stay on the right side of the line
because neither reads a field:

- **`lengths`** is a census. It is also the check a corpus curator actually
  needs — a capture whose every datagram is the same length recorded one kind of
  packet, and a decoder built from it would meet the second kind for the first
  time in production. Whether a session *should* hold more than one shape is a
  question this tool refuses to answer, because it does not know and neither
  does anything else here yet. No threshold is attached to the measurement.

  It earned itself on the first real session: `2 distinct` is how the second
  packet kind was noticed at all, and following that up showed the larger one
  arriving roughly every 3.5 seconds rather than once at the start — which is the
  difference between "a capture must begin before the device does" and "any
  capture longer than about four seconds will do".
- **`prefix`** is the leading bytes every datagram shares, which is how a reviewer
  sees a container's magic without a decoder ring. It is laid out the way the
  capture format lays out a datagram — sixteen bytes a line, an ASCII gutter
  rendered by the format's own function — because the tags are what a reader
  recognises and hex alone would hide them in plain sight. A prefix of zero bytes
  is as informative as a long one and is printed as such: it says the shapes
  differ from the very first byte.

  The 80-byte cap on that block is the one number here set from evidence rather
  than taste. It was 32, on the reasoning that a container's magic is not longer
  than that; the first real session shared **77** bytes and diverged at byte 77
  exactly, so the old cap hid 45 of them and cut the line off immediately before
  the one offset that mattered. A cap below the shared header of the protocol it
  is describing reports the cap instead of the protocol.

`arrival` is the **receive** clock and the line says so. The sibling reports a
cadence on the sender's own clock, which describes a source's frame rate; this
describes the source, the network, and this process's scheduling at once. A
reader who mistakes one for the other will go looking for jitter in a device
that never had any.

## Two modes

`--inspect` reads a recorded capture and prints the same block, opening no
socket. It is what makes this tool testable with no device and no sender, and it
is the answer to "is this fixture still what I thought it was" for a capture
recorded months ago — which is why it prints the capture's own `provenance` line
and a live session does not. On a live run the operator typed those values a
moment ago.

## Where a session leaves this adapter: `--export-trace`

```text
session.mocopipackets → mocopi_record --inspect --export-trace → session.trace
                      → motion_capture → motion_retarget → an avatar
```

A `motion-capture-trace` is what an adapter *delivered* — after protocol decode
and coordinate conversion, before any intake policy — and the product's own
`motion_capture` replays one knowing nothing about mocopi. That file is the only
thing that passes between this adapter and the product, which is what
[WORKSPACE.md §2](../../../../../docs/architecture/WORKSPACE.md) requires and
what lets the release condition say **unchanged** about both tools downstream.

**It goes with `--inspect`, and only there.** The sibling tool exports from a
live session too; this one refuses to, and the refusal is the tool's own design
rather than a missing feature:

- a recording here runs **no decoder at all**, which is the property everything
  above is built on. A live export would spend it to save a command.
- this tool accumulates datagrams and nothing else. A live export accumulates a
  1320-byte pose per frame beside the capture the datagram bound was sized for,
  which is the second bound in a second unit the sibling had to grow.
- an exported trace is then a pure function of committed bytes: the same capture
  exports the same trace on any machine, with no device.

**One trace is one session.** A capture the source restarted during holds two,
their stream clocks overlap, and splicing them would invent a continuity the
device denies — so the export is refused until `--source-session` names a half.
A capture that restarts and never recovers needs no flag: its new session
declares no rig, so no frame of it was ever delivered.

The declared `frameRate` is **measured from the samples**, not this device's
nominal 60 Hz. A capture the transport thinned exports at the rate it actually
holds, because a file that declared 60 would be denying its own contents.

What a trace cannot carry, and the capture keeps: the hips translation, which
bones the rig declared and a frame did not form, the transport's losses, the two
clocks' drift — and the **device**. `mocopi-packet-capture` has a `device`
header key and `motion-capture-trace` has three provenance keys, none of them
that. So the native path's claim to keep device state a relay drops holds as far
as the capture and stops at the trace. `--sender` and `--source-id` do cross,
as the trace's `provider` and `sourceId`: the adapter itself leaves both empty
because nothing on this wire may be published, and the operator's own words are
not a guess.

## A device that is not there yet is the ordinary state

`--silence-timeout` is this tool's own flag, and it is why
`VRM_MOCOPI_DEVICE_UNAVAILABLE` has no default threshold one layer down: how long
a device may reasonably take to start is a property of the session, and a session
is what a command line states. A phone being strapped on and a phone switched
off produce the same nothing.

It **reports and keeps listening**. `--idle-timeout` is the flag that stops. A
recorder that ended on a silence report would lose the recording the operator was
waiting to start.

That also settles what reaches stderr mid-session, where this tool differs from
its sibling deliberately. The sibling prints only the diagnostics a session
cannot continue past, because a 30 Hz sender missing one bone would write a
thousand recoverable lines a minute over the progress line. This layer raises two
codes in total — one fatal, one the receiver rate-limits to once per silence
episode — so there is no flood to protect against, and the recoverable one is the
most useful thing an operator waiting on a device can be told.

## Two warnings that arrive before the first datagram

Both are the vendor's statements rather than this tool's inferences, and both are
knowable at the bind:

- **loopback only.** The vendor documents `localhost` as an unsupported
  destination, so a loopback-bound receiver will hear nothing from a device
  however right everything else is.
- **an IPv6 endpoint.** The product sends to IPv4 only.

`UdpReceiver` deliberately refuses neither: a socket inventing a restriction on
itself out of a *product's* documentation is the wrong layer for it. This tool is
the right layer, and an operator who learns after ten quiet minutes that nothing
could ever have arrived has lost the ten minutes.

## Recording a session

```sh
# What is actually arriving, without writing anything.
mocopi_record --dry-run --silence-timeout 5 --duration 20

# Thirty seconds of it, recorded, with the provenance a fixture needs.
mocopi_record --output session.mocopipackets \
              --sender mocopi-app-1.2.3 \
              --device xperia-5-iv \
              --source-id mocopi-neutral-standing \
              --silence-timeout 5 --duration 30

# And what a committed capture holds, months later, with no device present.
mocopi_record --inspect session.mocopipackets

# The same session as canonical motion, on its way to the product's tools.
mocopi_record --inspect session.mocopipackets --export-trace session.trace
```

**Provenance values carry no spaces**, and that is enforced at the prompt. A
capture header holds one whitespace-delimited token per key, so `--sender "mocopi
1.2.3"` would produce a file this adapter's own reader refuses — and finding that
out *after* a session against a device is finding it out too late, since the
session cannot be re-recorded. Join the words with `-`.

**A capture written without `--sender` and `--source-id` warns**, because the
corpus check refuses a committed fixture that carries neither, and the person who
can still supply them is the one who just ran the session — half an hour later the
answer is a guess, and a guessed provenance is worse than an absent one. A missing
`--device` warns separately: it is not required by that check, but it is the one
header key this format has that the sibling's does not, and a capture that cannot
say what produced it cannot support the native path's claim to keep device state a
relay drops. Warnings and not refusals — the first session against a new device is
a legitimately exploratory recording.

Two more exits worth knowing before a long session:

- A session that received nothing writes **no file** and exits 1. The format has
  no datagram-less form — `WritePacketCapture` would emit a header and stop, and
  `ReadPacketCapture` refuses the result — so the alternative is handing an
  operator an artifact this adapter's own reader rejects, which they would
  discover at the point they tried to use it.
- A session the **socket** cut short writes what it had and exits 1. The file is
  still there and still valid; the exit is how a script tells a complete
  recording from a truncated one, because the capture cannot say afterwards which
  it was and `stopped: the socket failed` is prose on stdout.

## What may be committed, and what may not

A capture recorded off a phone holds somebody's motion, and
[§9.2](../../../../../docs/roadmap/adapters-mocopi-vmc-ardy.md#92-corpus) keeps
recorded evidence apart from the generated corpus for exactly that reason. The
vendor's `BVH Sender` is the path that needs no device: pointed at a `.bvh` this
repository wrote
([`libs/motionBvh/tests/corpus/generated/`](../../../../../libs/motionBvh/tests/corpus/generated/)),
it yields a capture whose *encoding* is the vendor's and whose *content* is ours.
That one belongs in `generated/` with its provenance saying what produced it.

The device is then needed for what only a device can give — tracking loss, sensor
state, reconnection, and the cross-source comparison of §9.6 — rather than for
the decoder.

## Tests

Five CTest names, split the way every claim in this adapter is split:

| name | needs | what it checks |
| --- | --- | --- |
| `mocopi_record_inspect` | nothing | five captures authored in Python, every report line, six malformed captures refused with a line number, the CLI's refusals, and the four `--listen` forms that must keep working |
| `mocopi_record_loopback` | a socket | bytes recorded verbatim, the stop reasons, the two-peer warning, six peers counted as six while four are named, and the silence report firing once with the session continuing through it |
| `mocopi_record_ipv6` | IPv6 loopback | the socket takes the address and the tool warns; `Skipped` where the runner has no `::1` |
| `mocopi_record_export` | the corpus | the frames, the operator's provenance, the measured rate, the two refusals a restarted capture earns, and a capture that decodes into no frame writing nothing |
| `mocopi_record_endToEnd` | the product's tools | the whole chain onto a rig, resolved through a `UsdSkelSkeletonQuery` — and onto `Seed-san.vrm` where the tree has the importer |

The envelope fixtures are authored beside the tests rather than read from a
corpus, and that is the point: this tool has no opinion about what a datagram
means, so a check of its report should not either. Authoring them in Python also
makes the reader independent of the writer under test. `--export-trace` is the
exception and says why: an export decodes, so its fixtures have to be things the
decoder can decode.

**What `mocopi_record_endToEnd` asks, and why it is not the sibling's
question.** No committed mocopi capture *moves* — every one is a held pose,
because they were generated to pin a decode path — so the test bakes two
sessions onto one rig and requires the joints that differ between them to be the
joints the sessions differ by. Both upper arms rotate in `arms-lowered-60hz`, so
a set of joint names cannot tell a side swap from a correct map; the sides are
therefore checked by sign as well, which is `SkeletonMap.h`'s measurement
carried to the end of the chain. A capture that moves needs a device, and the
day one is committed this test says so by failing.

## What it does not do

No decoding, no joint mapping, no coordinate conversion, no frame assembly, no
retargeting, no stage authoring, and no filtering of its own input. The last one
is the receiver's rule finished: a recorder that dropped a datagram it did not
like would make a corpus a description of what the recorder let through rather
than of what a source sent.
