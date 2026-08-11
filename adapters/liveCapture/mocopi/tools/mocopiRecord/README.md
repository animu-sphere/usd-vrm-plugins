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

```text
listen:      192.168.0.7:12351, receive buffer 65536 bytes
received:    118 datagram(s), 6844 byte(s) over 3.882 s
peers:       1 (192.168.0.20:52001)
arrival:     30.1 Hz mean, interval 0.031-0.041 s, over 3.882 s of receive clock
lengths:     3 distinct in 118 datagram(s)
             96 of 68 byte(s), 21 of 44, 1 of 27
prefix:      6 byte(s) common to every datagram:
             08 00 00 00 68 65
diagnostics: none
stopped:     --idle-timeout elapsed with nothing arriving
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
- **`prefix`** is the leading bytes every datagram shares, which is how a
  reviewer sees a container's magic without a decoder ring. A prefix of zero
  bytes is as informative as a long one and is printed as such: it says the
  shapes differ from the very first byte.

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
```

**Provenance values carry no spaces**, and that is enforced at the prompt. A
capture header holds one whitespace-delimited token per key, so `--sender "mocopi
1.2.3"` would produce a file this adapter's own reader refuses — and finding that
out *after* a session against a device is finding it out too late, since the
session cannot be re-recorded. Join the words with `-`.

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

Three CTest names, split the way every claim in this adapter is split:

| name | needs | what it checks |
| --- | --- | --- |
| `mocopi_record_inspect` | nothing | five captures authored in Python, every report line, six malformed captures refused with a line number, the CLI's refusals, and the four `--listen` forms that must keep working |
| `mocopi_record_loopback` | a socket | bytes recorded verbatim, the stop reasons, the two-peer warning, six peers counted as six while four are named, and the silence report firing once with the session continuing through it |
| `mocopi_record_ipv6` | IPv6 loopback | the socket takes the address and the tool warns; `Skipped` where the runner has no `::1` |

The fixtures are authored beside the tests rather than read from a corpus, and
that absence is the point: there is no mocopi corpus yet, and obtaining the first
one is what this tool is for. Authoring them in Python also makes the check
independent of the writer under test.

## What it does not do

No decoding, no joint mapping, no coordinate conversion, no frame assembly, no
retargeting, no stage authoring, and no filtering of its own input. The last one
is the receiver's rule finished: a recorder that dropped a datagram it did not
like would make a corpus a description of what the recorder let through rather
than of what a source sent.
