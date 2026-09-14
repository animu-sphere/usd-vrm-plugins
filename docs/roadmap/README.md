# Roadmap

The roadmap holds only **incomplete** work. Shipped work lives in the
[delivery history](../reports/delivery-history.md) and, once `v0.1.0` is tagged,
in per-version [release records](../releases/). Design rationale lives in
[design/](../design/).

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

| Document | Contents |
| --- | --- |
| [current.md](current.md) | The next milestone and active carry-over work. |
| [backlog.md](backlog.md) | Ordered but unscheduled work: the milestone ladder beyond next, the motion layer, future phases, and cross-cutting open items. |
| [packaging-hardening.md](packaging-hardening.md) | The **distribution** direction, and the top-priority track: an installed-package consumer lane that configures each package from a clean prefix, outside this repository, naming no workspace target. Added 2026-08-29, the day a green CI shipped two packages naming a target no consumer could resolve. |
| [adapters-mocopi-vmc-ardy.md](adapters-mocopi-vmc-ardy.md) | The **live** input-adapter direction: a VMC Protocol adapter (shipped), then a direct capture-product adapter, then a generation adapter — completing end to end with no OpenExec dependency. The filename keeps the original triple; the order inside was reversed on 2026-07-29. |
| [recorded-motion-sources.md](recorded-motion-sources.md) | The **recorded-file** direction: a generic BVH pipeline over a format-neutral `motionSource` layer, with producer semantics in declarative profiles. Deliberately not any one capture product's importer. Added 2026-08-03. |
| [osc-and-vrchat-trackers.md](osc-and-vrchat-trackers.md) | The **third live input** and the sharing it forces: a VRChat OSC Trackers adapter over a protocol-neutral OSC decoder, plus the transport code the first two adapters already duplicate. A tracker source is not a pose source, and that difference is the reason it is its own plan. Added 2026-08-23. |
| [openexec-foundation.md](openexec-foundation.md) | The OpenExec direction: the OpenUSD 26.08 exact pin, the `execMotion` / `execVrm` foundation, and the `ExecIr` invertible rig. Kept separate because it is a plan, not a status list. Renamed from `openexec-v0.6.0-v0.7.0.md` on 2026-08-03, when its target moved. **Moved to the front of the queue on 2026-09-06** — see the re-order note below. |
| [boundary-consolidation.md](boundary-consolidation.md) | The **boundary** direction: state the agreements nine identities and four producer categories arrived at separately, as one set — the canonical producer contract, one reference pipeline for every source category, the adapter distribution decision, artifact closure as a release gate, and the invariants that can be checked rather than reviewed. Adds no format, adapter, node or package. Added 2026-09-06. |
| [motion-foundation-split.md](motion-foundation-split.md) | The **repository split** direction, and the only one that is conditional: `motionCore` + `motionRuntime` as their own repository, gated on a measurement of the four preconditions that can end the track. Its reversible half — a foundation consumed through `find_package` as though external — pays for itself whether or not anything moves. Added 2026-09-06. |

## Three sequences, deliberately separate

This repository tracks three independent sequences. They are never abbreviated
to a bare "Phase 4" or "Phase A" — always qualified:

| Sequence | Notation | What it tracks | Source of truth |
| --- | --- | --- | --- |
| Product roadmap | **Product P0–P6** | What the plugins do for users: docs, release, canonical contract, runtime verification, runtime layer, MToon, round-trip. | [design/DESIGN_POLICY.md](../design/DESIGN_POLICY.md) §17 |
| Workspace migration | **Workspace Phase 0–8** | Where the code lives: baseline, schema split, container extraction, resolver split, rename, packaging, motion libraries, `usdVrmaFileFormat`, `execMotion`/`execVrm`. | [architecture/WORKSPACE.md](../architecture/WORKSPACE.md) §8 |
| Motion runtime | **Motion Phase A–H** | How motion works: contract freeze, `.vrma` import, offline retarget, live capture, OpenExec, generation, expression/look-at, advanced. | [design/MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md) §16 |

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
in the [delivery history](../reports/delivery-history.md). **The new Motion Phase
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
| OpenExec foundation | In progress | v0.9.0 |
| boundary consolidation, the canonical producer contract included | Planned | after the OpenExec foundation |
| motion foundation repository split | Planned | after boundary consolidation, **and conditional on its own gate** |
| NPZ / AMASS recorded sources | Planned | after the split gate |
| ARDY generation adapter | Planned | with NPZ / AMASS |
| `ExecIr` invertible VRM humanoid rig | Planned | after the OpenExec foundation, unscheduled |

**Re-ordered 2026-09-06 — the owner's call, and it inverts the 2026-08-29
pair.** The direction it comes from is
[design/INTEGRATION_SCOPE_POLICY.md](../design/INTEGRATION_SCOPE_POLICY.md),
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
[Scope policy §10](../design/INTEGRATION_SCOPE_POLICY.md) requires two non-VRM
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
([the track](packaging-hardening.md) §1). Two consequences for this table.
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

Where things stand, as of 2026-09-15:

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
  ([release record](../releases/v0.8.0.md)). What it shipped and did not close
  is in that record's known limitations; the part with work left is in
  [current.md](current.md#carried-out-of-v080).
- **Current priority: v0.9.0, the [OpenExec foundation](openexec-foundation.md).**
  What remains is one packaging task (P0-3), the producer half of the
  statements the nodes read (P0-4), and three decisions (P0-6, P1-2, P1-3);
  [current.md](current.md) has the table. Carried beside it: operator evidence
  for both input halves, the Workspace Phase 5 packaging P0, and runtime
  verification. None of those blocks the foundation, and it blocks none of them.
- **The recorded half's second format family waits behind three tracks**, and
  its boundary is already built: NPZ / AMASS enters through `motionSource`
  exactly as BVH does. Whether that is one identity (`motionNpz`) or two
  (`motionNpz` + `motionAmass`) is a **measurement, not a preference**
  ([the recorded track](recorded-motion-sources.md) §13).
- The display slice is **re-scoped** (2026-07-29). OpenUSD 26.08 resolves exec
  prim adapters from a hard-coded list, so a skinned VRM avatar cannot be
  displayed through the exec scene index at all. The foundation proves the
  mechanism on an exec-computed `UsdGeomXformable`; realtime skinned display is
  a later milestone and a release condition for nothing on this table.

## Quality bar (applies to every phase)

- The importer authors data and never evaluates or simulates it.
- Authored stage semantics do not change without a schema contract bump.
- Every bundle builds standalone against installed packages, not just composed
  in the workspace tree. *(Stated since the first split and unenforced until
  now: a composed build never opens a config file, so this line was true of the
  bundles and unchecked for every library and adapter. The
  [packaging track](packaging-hardening.md) is what turns it into a lane, and
  [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md) records which
  packages have been measured rather than only reviewed.)*
- Dependency directions in [WORKSPACE.md](../architecture/WORKSPACE.md) §2 are
  enforced by CI, not convention. *(Met since the `ost` 0.21.0 adoption: the
  `workspace-graph-pr` cell runs `ost plugin test --workspace --graph-only` on
  every PR, before anything is built.)*
- Every documented command is one that has actually been run, and no document
  contradicts what CI does.
- **A package's dependency closure is explicit, not implied by a build that
  happens to work.** Every `PUBLIC`/`INTERFACE` workspace edge appears as a
  `find_dependency` in that package's config and as a row in
  [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md). *(Added
  2026-08-29, from the near-term plan's done criteria.)*
- **The line between public API and internal implementation can be stated for
  every identity**, not inferred from which header a consumer happened to
  include. The installed header root is that line, and it is a contract row
  rather than a convention.
