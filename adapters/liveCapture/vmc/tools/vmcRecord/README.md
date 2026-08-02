# vmc_record

Record a live VMC session, and say what it was.

```sh
vmc_record --output session.vmcpackets --sender vseeface --source-id walk-01
vmc_record --inspect adapters/liveCapture/vmc/tests/corpus/arm-raise-30hz.vmcpackets
```

This is the VMC adapter's CLI, and the one part of it that meets a real sender.
Every layer beneath it is verifiable from committed bytes — which is the whole
point of the adapter's build order
([the plan](../../../../../docs/roadmap/adapters-mocopi-vmc-ardy.md) §5) and also
its limit: the corpus is *generated*, so it reproduces the protocol's shapes and
not what any real application emits. The rest of Milestone B is the same shape —
two sender applications validated, a capture device through a relay, a recorded
corpus — and none of it can be closed by writing more code. It is closed by an
operator pointing a sender at this port.

So the tool turns one such session into two things a repository can keep: a
capture file, and a statement of what was in it.

## The datagram reaches the file before the decoder sees it

```text
receive  ->  append to the capture  ->  decode  ->  report
```

That order is the one rule here. A recorder whose decoder could refuse a
datagram would record what this adapter *already understands*, and the sessions
worth recording are exactly the ones it might not: a sender emitting a form no
fixture pins, a device dropping a joint, a restart mid-frame.
[`UdpReceiver.h`](../../include/vrmAdapterVmc/UdpReceiver.h) makes the same
argument one layer down about filtering, and this is where it finishes — nothing
the decode path does or fails to do can change a byte of what was written.

The decode still runs in the same loop rather than afterwards, because an
operator with a sender application open needs to know *now* whether the session
is worth keeping. What it produces is a report, and a report is not a filter.

## Two modes, one report

`--inspect` decodes a recorded capture and prints the same block, opening no
socket. That is what makes this tool testable in CI over the committed corpus
with no sender at all, and it is also the answer to "is this fixture still what
I thought it was" for a capture recorded months ago.

```text
listen:      0.0.0.0:39539, receive buffer 65536 bytes
received:    117 datagram(s), 8180 byte(s) over 0.152133 s
peers:       1 (192.168.1.20:52002)
decoded:     117 datagram(s), 0 refused whole; 2 message(s) ignored
frames:      5 emitted, 0 incomplete; 0 refused out of order, 0 refused empty
cadence:     30 Hz mean, interval 0.0333328-0.0333347 s, over 0.133333 s of sender clock
bones:       21 of 55 observed; 105 accepted, 0 duplicated, 0 unsupported, 0 malformed
clock:       5 frame(s) stamped by the sender, 0 by the receiver; 0 restart(s)
intake:      5 frame(s) delivered, 5 admitted, 0 refused; 0 session reset(s)
hips offset: 5 frame(s), |offset| 0.9-0.9 m, moved at most 0 m from the first (constant: rig geometry, not translation)
             first (0, 0.9, 0)
root:        5 frame(s) with a position, moved at most 0 m from (0, 0, 0) (constant)
diagnostics: none
model:       "Example Avatar" - the sender's model title, and it is in the recorded bytes
stopped:     end of capture
```

The order is the order the questions are asked when a live session is not
working: is anything arriving, does it decode, does it become motion, what went
wrong. Each line is a tally some layer of the adapter already keeps —
`UdpReceiverStats` cannot see a bone and `VmcFrameStats` cannot see a datagram
that never decoded, so the report is the one place they are read together.

## The two lines that are not statistics

`hips offset` and `root` are the evidence the adapter deliberately does not have.
A `HumanoidPose` carries rotations and one `RootMotion`, so fifty-four of a
frame's fifty-five local positions are the sender's rig geometry — but the
fifty-fifth could be body translation instead, and whether it is depends on what
a real sender puts in each field
([`FrameAssembler.h`](../../include/vrmAdapterVmc/FrameAssembler.h)). A hips
offset that never moves is rig geometry; one that tracks the body is
translation. Neither this tool nor the adapter decides which — it reports the
numbers that decide it, which is why the line says how far the value moved and
not what it means.

The `model` line is the same kind of statement in the other direction: the
sender's model title is in the recorded bytes whatever this tool does with it,
so an operator deciding whether a capture can be committed is told it is in
there. It is never *used* — `--source-id` names a capture, and naming one after
the avatar it happened to see would put someone's title in a fixture's header as
well as its payload.

## Stopping

A recorder that only stops on Ctrl-C cannot be run from a script; one that
cannot be interrupted cannot be run by a person. Both exist, plus `--duration`,
`--idle-timeout`, and a `--max-datagrams` bound that is on by default — the
capture is held in memory until it is written, so the bound is a bound on
memory and is not optional.

Every session reports which of them ended it. A capture that stopped because a
flag said so and one that stopped because the socket failed are different
sessions, and the file cannot tell them apart afterwards.

## Recording a session

```sh
# What the sender is actually sending, without writing anything.
vmc_record --dry-run --idle-timeout 5

# Ten seconds of it, recorded.
vmc_record --output walk-01.vmcpackets --duration 10 \
           --sender vseeface --source-id walk-01
```

`--listen` defaults to every IPv4 interface and `--port` to 39539, VMC's
registered port. Two failures are worth knowing before they happen: a socket
bound to `127.0.0.1` cannot be reached from the machine running the sender
(the report says `loopback only`), and a second recorder on a port that is
already served takes some fraction of the traffic rather than failing — which is
why `--reuse-address` is off by default.

## Tests

```text
vmc_record_inspect    every committed capture, decoded through the CLI
vmc_record_loopback   one capture through a real socket and back
```

The second makes the claim the tool exists for: the datagrams that came out of
the socket are byte-identical to the ones that went in, and the recorded file
reports the same motion as the file it was replayed from. That is
`vrmAdapterVmc_loopbackCorpus`'s claim raised to the CLI — the library test
compares poses, this one compares the artifact an operator actually keeps. It is
a separate CTest name for the same reason the library's socket tests are: a
runner that forbids binding excludes it and loses nothing else.

## What it does not do

It does not retarget, author a stage, or open an avatar.
[WORKSPACE.md](../../../../../docs/architecture/WORKSPACE.md) §2 *permits* an
adapter tool to do all three — that permission is what separates a tool from its
library — and this one needs none of them. `motion_capture` is where a VMC
session becomes a clip (Milestone C), and a second path to an avatar from here
would be the fork the plan's §2 forbids.
