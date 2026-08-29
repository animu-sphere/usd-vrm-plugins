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
| [openexec-foundation.md](openexec-foundation.md) | The OpenExec direction: the OpenUSD 26.08 exact pin, the `execMotion` / `execVrm` foundation, and the `ExecIr` invertible rig. Kept separate because it is a plan, not a status list. Renamed from `openexec-v0.6.0-v0.7.0.md` on 2026-08-03, when its target moved. |

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
sequences carry none. As of 2026-08-29 that pair carries no version at all: it
follows the producer tracks, and it takes a number when the release before it is
cut.

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
| installed-package consumer lane + package contract | Next | v0.8.0 |
| shared OSC foundation + VRChat OSC Trackers input | Next | v0.8.0 |
| NPZ / AMASS recorded sources | Planned | after v0.8.0 |
| canonical motion producer contract | Planned | after v0.8.0 |
| OpenExec foundation | Planned | after the two above |
| `ExecIr` invertible VRM humanoid rig | Planned | after the OpenExec foundation |

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
the re-ordering itself is recorded — here, the OSC bullet below, and
[the current milestone](current.md)'s opening note.

**Re-ordered 2026-08-03.** The OpenExec foundation was scoped as v0.6.0 and
v0.6.0 shipped VMC input instead. Rather than renumber the plan by one, the
sequence was rebuilt around what each release can actually prove: OpenExec parity
is only worth as much as its input, so the release that records real device and
sender sessions comes first, and OpenExec then re-evaluates a pipeline that has
already met real hardware. The file `openexec-v0.6.0-v0.7.0.md` was renamed
[openexec-foundation.md](openexec-foundation.md) in the same change — a filename
carrying a version number is drift waiting to be re-litigated.

- The workspace covers **Workspace Phase 6b and 7**: `motionCore`,
  `motionRuntime`, `vrmRetarget`, and `usdVrmaFileFormat` implement Motion
  Phases A–D; `vrmAdapterVmc` and `vrmAdapterMocopi` are the vendor leaves over
  them, and `motionSource` / `motionBvh` are the recorded-file half beside them.
  Only Workspace Phase 8 (`execMotion` / `execVrm`) is unbuilt.
- **v0.7.0 is prepared** (mocopi live input and generic BVH ingestion), after
  v0.6.0's VMC input. Every lane is pinned to OpenUSD 26.08 — and since v0.6.0,
  no other OpenUSD will configure at all.
- **The motion layer has CI.** `ost` 0.21.0's `kind: workspace` cells build the
  root tree and run its whole CTest suite on all three OS; the v0.5.0
  hand-written lane is deleted. They picked the adapter's tests up — including
  the two that bind a socket — with no CI edit.
- **Input has two halves, and v0.7.0 built both.** Live input is
  [the adapter track](adapters-mocopi-vmc-ardy.md); recorded files are
  [the BVH track](recorded-motion-sources.md), which is a generic pipeline with
  producer semantics in data rather than a capture product's importer. They meet
  at `motionCore` and nowhere earlier
  ([motion policy §8.3](../design/MOTION_ARCHITECTURE_POLICY.md)). One physical
  session observed both ways agrees to a median **0.084°** per bone
  ([report 01](../reports/motion/01-2026-08-15-mocopi-cross-source.md)); what
  v0.7.0 did **not** close is operator evidence — a VMC relay, a device recovery
  take, a redistributable capture, and an artifact-only run that needs the
  profiles to reach an artifact first.
- **Distribution is the near-term priority, and it is the one boundary this
  workspace has never checked from outside**
  ([the packaging track](packaging-hardening.md)). Every split since Workspace
  Phase 1 is enforced inside the tree — the graph gate, the boundary checks, the
  workspace cells — and a composed build resolves every target in-tree without
  ever opening a config file. So an installed package can name a target no
  consumer can resolve and stay green, which it did: `osc::osc` went `PUBLIC` on
  two adapters on 2026-08-29 and neither package config gained a
  `find_dependency(osc)`. The fix that landed is per-adapter; the general one is
  a consumer that is not us, and its contract is
  [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md).
- **The live half gains a third leaf in v0.8.0, and a shared floor under all
  three** ([the OSC track](osc-and-vrchat-trackers.md)). It was deliberately not
  in v0.7.0: that release's remaining items are evidence an operator produces,
  and a third adapter would have reopened a code milestone underneath them. It
  now shares a release with the packaging lane rather than preceding it as
  v0.7.5 — the same argument that kept it out of v0.7.0 makes a point release
  between two halves of one boundary the wrong shape. The duplication it resolves is **measured, not anticipated** — the
  two existing adapters carried one packet-capture implementation twice, six
  lines apart across 800, and one UDP receiver twice carrying four copied
  defects that were found and fixed in one copy on 2026-08-11 and stayed in the
  other until OSC-1 closed them on 2026-08-24. **The shared floor landed the
  same day**: `libs/liveTransport` holds the receiver, the capture format and
  the diagnostic vehicle once, and both adapters build against it (OSC-2). **The
  second half landed on 2026-08-29**: `libs/osc` holds the wire format once, and
  it waited for a second consumer rather than a schedule — an address inventory
  written in `vrmAdapterVrchatOsc` decoded real bytes through the VMC-owned
  decoder first, and needed five VMC tokens of which every one was the name
  (OSC-3). What remains on this track is the adapter itself: a recorded session
  to inventory, then the tracker decode, the tracking space, the frame policy
  and the solve boundary.
- **The recorded half gains a second format family after v0.8.0**, and the
  boundary is already built for it: NPZ / AMASS enters through `motionSource`
  exactly as BVH does, and a reader is allowed format syntax and storage
  interpretation and nothing else — no VRM target rig, no rest pose, no retarget
  policy, no stage authoring. Whether that is one identity (`motionNpz`) or two
  (`motionNpz` + `motionAmass`) is a **measurement, not a preference**: a few
  files of the real corpus decide whether the AMASS contract is absorbable at a
  format-neutral boundary. [The recorded track](recorded-motion-sources.md) §13.
- Current priorities: **installed-package consumer verification**, **real device
  evidence** across both input halves, closing the remaining **Workspace Phase
  5** packaging P0, and widening runtime verification. The
  [OpenExec foundation](openexec-foundation.md) follows the producer tracks and
  blocks none of them.
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
