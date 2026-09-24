---
status: historical
owner: usd-vrm-plugins
---

> **Historical only — archived 2026-09-25.** How this repository ordered its
> motion tracks, and why the order changed, as the roadmap index stated it on
> the day the tracks were archived. Do not use it to determine current
> ownership or roadmap: start from [docs/README.md](../../README.md). See
> [the archive index](../README.md).

# Roadmap orderings, 2026-08-03 to 2026-09-19

## Three sequences, deliberately separate

This repository tracks three independent sequences. They are never abbreviated
to a bare "Phase 4" or "Phase A" — always qualified:

| Sequence | Notation | What it tracks | Source of truth |
| --- | --- | --- | --- |
| Product roadmap | **Product P0–P6** | What the plugins do for users: docs, release, canonical contract, runtime verification, runtime layer, MToon, round-trip. | [design/DESIGN_POLICY.md](../../design/DESIGN_POLICY.md) §17 |
| Workspace migration | **Workspace Phase 0–8** | Where the code lives: baseline, schema split, container extraction, resolver split, rename, packaging, motion libraries, `usdVrmaFileFormat`, `execMotion`/`execVrm`. | [architecture/WORKSPACE.md](../../architecture/WORKSPACE.md) §8 |
| Motion runtime | **Motion Phase A–H** | How motion works: contract freeze, `.vrma` import, offline retarget, live capture, OpenExec, generation, expression/look-at, advanced. | [design/MOTION_ARCHITECTURE_POLICY.md](../../design/MOTION_ARCHITECTURE_POLICY.md) §16 |

How they meet:

- The **workspace** sequence answers *where does the code live* — it establishes
  a boundary, manifest, and packaging, and nothing more.
- The **motion** sequence answers *how does motion work* — it fills those
  boundaries with behavior.
- The **product** sequence answers *what does the user get*. Product P4 is now
  the umbrella for the runtime layer and delegates its detail to Motion Phase
  A–H rather than enumerating nodes itself.

So Workspace Phase 8 creates the `execVrm` bundle; Motion Phase E implements its
nodes; Product P4 is "done" when a user can drive an avatar from a clip, a live
capture, or a generator without changing the importer.

A phase is not a release. Workspace Phase 8 and Motion Phase E land together,
but *which* release they land in is a scheduling decision that has now changed
twice — which is why the version lives in the status table below and the phase
sequences carry none. From 2026-08-29 that pair carried no version at all, to
take a number when the release before it was cut. v0.8.0 was cut on 2026-09-01,
and the pair is **v0.9.0**.

An earlier draft used "Phase A–E" and an importer-specific "Phase 1–4"; both are
retired. The importer build-out those numbers tracked is complete and recorded
in the [delivery history](../../reports/delivery-history.md). **The new Motion Phase
A–H is unrelated to that retired A–E** and always carries the "Motion" qualifier.

## Status at a glance

**This table is the single source of truth for which release a track lands in.**
Every other document names a track and defers the version here; where a document
must repeat one, `scripts/check_docs.py` checks it against this table.

| Track | Status | Target |
| --- | --- | --- |
| VMC input | Shipped | v0.6.0 |
| mocopi live input | Shipped | v0.7.0 |
| generic BVH recorded-motion ingestion | Shipped | v0.7.0 |
| installed-package consumer lane + package contract | Shipped | v0.8.0 |
| shared OSC foundation + VRChat OSC Trackers input | Shipped | v0.8.0 |
| OpenExec foundation | Shipped | v0.9.0 |
| boundary consolidation, the canonical producer contract included | Planned | after the OpenExec foundation |
| motion migration to `usd-motion-plugins` and `motion-connectors`, boundary consolidation folded in | Planned | after the OpenExec foundation |
| NPZ / AMASS recorded sources | Moved | `usd-motion-plugins`, with `motionSource` |
| ARDY generation adapter | Moved | `motion-connectors` |
| `ExecIr` invertible VRM humanoid rig | Planned | after the OpenExec foundation, unscheduled |

**Re-ordered 2026-09-17 — the motion architecture was decided outside this
repository.** The `usd-motion-plugins` design policy places generic motion in
`usd-motion-plugins` and device and protocol input in `motion-connectors`, with
this repository depending on both and keeping VRM and VRMA. Three rows change.
The conditional split becomes an unconditional **motion migration**, and
boundary consolidation is folded into it: its producer contract becomes
evidence handed to the shared core, its reference pipeline the migration's
cross-repository test, its adapter distribution decision `motion-connectors`',
and its remaining items stay here beside the move
([current.md](../../roadmap/current.md)). NPZ / AMASS and the ARDY adapter leave with the
code they would have extended. The OpenExec foundation keeps its place and its
version: it finishes here first, because its findings are the API defects the
move fixes on arrival. Destinations are
[WORKSPACE.md §9](../../architecture/WORKSPACE.md#9-destinations-under-the-motion-architecture); the plan is
[motion-foundation-split.md](motion-foundation-split.md). The 2026-09-06 note
below is the record of the order it replaces.

**Re-ordered 2026-09-06 — the owner's call, and it inverts the 2026-08-29
pair.** The direction it comes from is
[design/INTEGRATION_SCOPE_POLICY.md](../../design/INTEGRATION_SCOPE_POLICY.md),
adopted the same day, which states for the first time how far this repository
goes: an integration workspace joining VRM assets and humanoid motion to
OpenUSD, and never a motion engine, a capture SDK, a generative model or a
network stack. The order that follows from it is **OpenExec → boundary
consolidation → repository split → additional sources and generators**, and
three things about it are worth stating precisely rather than leaving to be
re-derived.

**OpenExec moved to the front, reversing the argument that put it behind the
producer tracks.** The 2026-08-29 order said a compute layer over moving
contracts evaluates a boundary twice. The counter-argument is the packaging
track's, one layer out: every exec node is specified as a *thin wrapper* over
`motionRuntime` or `vrmRetarget`, so the foundation is the first consumer of
those libraries that is not the tool that grew up beside them — and a node that
cannot be written as a wrapper is a finding about the library API, produced by
an implementation instead of predicted by a review. What that costs is stated
in [the boundary track](boundary-consolidation.md) §1: no new source shape
informs Motion Phase E's node set, and the producer contract is written after
`execVrm` exists rather than before it.

**The canonical producer contract is no longer its own row.** It is BND-0 of
[the boundary track](boundary-consolidation.md), where it stops being the thing
NPZ had to answer on its way in and becomes one item in a track whose whole
subject is boundaries.

**The repository split is scheduled ahead of its own preconditions, on purpose.**
[Scope policy §10](../../design/INTEGRATION_SCOPE_POLICY.md) requires two non-VRM
consumers before a component leaves, and after Motion Phase E there is exactly
one (`execMotion`, vendor-neutral by specification). So the track opens with a
measurement that can end it, and its reversible half — the foundation packaged
and consumed as though it were external, in place — is work this repository
wants either way ([the split track](motion-foundation-split.md) §2).

**Re-ordered 2026-08-29, and the numbering moved with it.** The near-term plan
of that date put **packaging hardening first** — before the third adapter's
decoder, before NPZ/AMASS, and before OpenExec — on a measured argument: the
boundaries five phases of splitting created are only real if a consumer outside
this repository can resolve them, and on 2026-08-29 two installed packages named
a target no consumer could resolve while all 17 lanes were green
([the track](../packaging/packaging-hardening.md) §1). Two consequences for this table.
**The OSC track's release is now v0.8.0 rather than v0.7.5**: packaging
hardening and the tracker path ship together, and a point release between them
would have split one boundary across two tags. **The OpenExec foundation loses
its version** and sits behind the recorded-source and producer-contract tracks;
it is the compute layer over a canonical pipeline, and the plan's order puts
every producer contract in front of it. The `v0.7.5` label survives only where
the re-ordering itself is recorded, which is here.

**Re-ordered 2026-08-03.** The OpenExec foundation was scoped as v0.6.0 and
v0.6.0 shipped VMC input instead. Rather than renumber the plan by one, the
sequence was rebuilt around what each release can actually prove: OpenExec parity
is only worth as much as its input, so the release that records real device and
sender sessions comes first, and OpenExec then re-evaluates a pipeline that has
already met real hardware. The file `openexec-v0.6.0-v0.7.0.md` was renamed
[openexec-foundation.md](openexec-foundation.md) in the same change — a filename
carrying a version number is drift waiting to be re-litigated.

Where things stand, as of 2026-09-19:

- **Every Workspace phase has code.** `motionCore`, `motionRuntime`,
  `vrmRetarget` and `usdVrmaFileFormat` implement Motion Phases A–D;
  `motionSource` / `motionBvh` are the recorded-file half beside them; the three
  `vrmAdapter*` leaves sit on the shared `liveTransport` and `osc` floor; and
  Workspace Phase 8's `execMotion` / `execVrm` exist, evaluate a humanoid, and
  agree with the offline bake bit for bit. Every lane is pinned to OpenUSD
  26.08, and no other OpenUSD will configure.
- **v0.8.0 shipped on 2026-09-01**: the installed-package consumer lane, which
  resolves all twelve packages from outside the workspace on all three OS, and
  the VRChat OSC tracker input over the shared OSC foundation
  ([release record](../../releases/v0.8.0.md)). What it shipped and did not close
  is in that record's known limitations; the part with work left is in
  [current.md](../../roadmap/current.md#carried-out-of-v080).
- **v0.9.0 shipped on 2026-09-17**: the
  [OpenExec foundation](openexec-foundation.md), with OpenExec and the offline
  bake agreeing on 414 598 values, every one `==`
  ([release record](../../releases/v0.9.0.md)).
- **Current priority: the [motion migration](motion-foundation-split.md).**
  MIG-0..MIG-4 are done on this side (2026-09-19..24): every generic motion
  identity and every live input left, and this repository consumes what it
  still uses by digest. What is left is MIG-5's release-artifact proof and
  its cross-repository test ([current.md](../../roadmap/current.md)). Carried beside it: operator evidence for both input halves, the
  Workspace Phase 5 packaging P0, and the build machine's RPATH in packaged
  POSIX binaries ([current.md](../../roadmap/current.md#carried-out-of-v090)).
- **The recorded half's second format family left with the layer it extends**:
  it is no longer this repository's. NPZ / AMASS enters through `motionSource`,
  which is `usd-motion-plugins`' since MIG-3, and whether it is one identity
  (`motionNpz`) or two (`motionNpz` + `motionAmass`) is measured there
  ([the recorded track](recorded-motion-sources.md) §13).
- The display slice is **re-scoped** (2026-07-29). OpenUSD 26.08 resolves exec
  prim adapters from a hard-coded list, so a skinned VRM avatar cannot be
  displayed through the exec scene index at all. The foundation proves the
  mechanism on an exec-computed `UsdGeomXformable`; realtime skinned display is
  a later milestone and a release condition for nothing on this table.
