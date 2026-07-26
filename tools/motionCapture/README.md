# motion_capture — Motion Phase D replay tool

Replays a recorded capture session into an avatar-independent semantic humanoid
clip. It is the motion layer's live half, made reproducible: the trace is pushed
frame by frame into a generic `LiveCaptureSource`, evaluated on a fixed tick,
and recorded back into a clip **in exactly the form `usdVrmaFileFormat`
produces** — so `motion_retarget` bakes a live session onto an avatar with no
changes at all.

Like `motion_retarget`, this is an executable, not a bundle: it registers
nothing with OpenUSD and has no `plugInfo.json`.

## The loop

```cpp
sender.Advance(now - deliveryLag);      // what has arrived by now
recorder.Record(source.Sample(now));    // what the consumer sees now
```

That is the whole of "live capture" as this project defines it. A real adapter
replaces the first line with a decoded packet and nothing else changes
([motion policy §8.2](../../docs/design/MOTION_ARCHITECTURE_POLICY.md)) — which
is why replaying a recording is a faithful test rather than a mock. Nothing in
`motionRuntime` or in this tool reads a wall clock or opens a transport.

## Use

```sh
# Replay a session into a semantic clip.
motion_capture --trace session.trace --output clip.usda --report

# Then bake it onto an avatar with the unchanged Phase C tool.
motion_retarget --avatar avatar.usda --animation clip.usda \
                --output baked.usda --humanoid-map map.json
```

### Simulating a transport that falls behind

`--delivery-lag` is the knob that makes a replay behave like a real session:
frames arrive that many seconds after the tick they belong to, so the consumer
starts holding and extrapolating exactly as it would when the network is slow.

```sh
motion_capture --trace session.trace --output clip.usda \
               --delivery-lag 0.1 --extrapolation 0.05 --report
```

`--report` prints how many frames were accepted or refused and why, how much of
the humanoid was observed, how many bones were gated or held, and how many ticks
were sampled, held, extrapolated, or unanswerable. Those numbers are also
written into the clip's `capture:*` customData, so a baked result can be traced
back to the session and the intake settings that produced it.

### Intake policy

| Flag | Effect |
| --- | --- |
| `--confidence-floor F` | Bones reporting below `F` are treated as missing. Frames carrying no confidence at all are never gated. |
| `--missing-bones hold\|unbound` | A missing bone keeps its last observed rotation, or is left for the target rig's rest pose. |
| `--root-motion derive\|passthrough\|ignore` | `derive` fills in a linear velocity from consecutive frames when the source reports none, which is what lets extrapolation hide a late frame. |
| `--smoothing HZ` | Frame-rate-independent exponential smoothing on intake. |

`--normalize out.trace` rewrites a trace in canonical form and exits — the way
to bring a hand-written fixture into the shape `motionRuntime`'s writer emits.

## What it authors

A `/Capture` scope holding a `UsdSkelSkeleton` over semantic humanoid joint
paths (`hips/spine/chest/...`) and a `UsdSkelAnimation` bound to it. Two details
are deliberate:

- **`scales` is always authored.** UsdSkel fetches translations, rotations and
  scales as a unit and `scales` has no schema fallback, so a clip without it
  binds cleanly and then holds every joint at rest — the v0.4.0 regression
  ([#64](https://github.com/animu-sphere/usd-vrm-plugins/issues/64)).
- **Rest transforms are identity, except the hips.** A capture stream reports
  rotations relative to the humanoid rest, never the rest itself, so identity
  rests make the retargeter's rest-pose correction a no-op — the honest reading.
  The hips rest translation is seeded with the session's first observed root
  position, so root motion arrives downstream as a delta from where the capture
  started rather than as an absolute height
  ([MOTION_CONTRACT.md](../../docs/design/MOTION_CONTRACT.md)).

A bone the session never observed is **absent** from the clip rather than
authored at rest: a joint that is present and unmoving means something different
downstream from a joint that was never captured.

## Tests

```sh
ctest -R motion_capture_replay
```

The test replays the corpus, checks the clip's shape and provenance, proves the
two missing-bone policies actually differ, drives a lagged session, and then
bakes the result onto `docs/design/fixtures/motion/avatar.usda` with
`motion_retarget` and resolves it through a `UsdSkelSkeletonQuery`. That last
step is the milestone's claim made falsifiable: if the live path produced
anything the offline path could not consume, it fails.

## Not in scope

No product-specific adapter ships here. Protocol decode and coordinate
conversion belong under `adapters/`, the only place product names are permitted
(WORKSPACE.md §1, motion policy §8.1). Validating against a real capture rig
remains open work.
