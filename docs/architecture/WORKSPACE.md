# Workspace contract

This document is the binding contract for splitting `usdVrm` into an
OpenStrata plugin workspace. It fixes bundle identities, dependency
directions, artifact naming, and the invariants every migration PR must
preserve. Structural changes that contradict this document require changing
this document first, in its own PR.

Status: contract adopted; Phase 0 baseline frozen; Phase 1 `vrmSchema` split,
Phase 2 `vrmContainer` extraction, Phase 3 `usdVrmPackageResolver` split, and
Phase 4 `usdVrm` → `usdVrmFileFormat` rename landed (see §8). `usdVrm` is no
longer a bundle id; it names the aggregate product only (§1).

The motion layer (`.vrma` import, retargeting, the OpenExec runtime) was added
to this contract on 2026-07-18 from
[design/MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md).
Workspace Phase 6a (`motionCore`) and Phase 7 (`usdVrmaFileFormat`) land in
v0.3.0; the remaining motion identities are reserved. Workspace Phase 5 emits
the aggregate product archive, but its standalone packaging-closure P0 remains
open. The implementation contract for the shipped motion foundation is
[MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md).

The three input-adapter identities (`vrmAdapterMocopi`, `vrmAdapterVmc`,
`vrmAdapterArdy`) and their dependency directions were added to this contract on
2026-07-28, ahead of any adapter code, from
[roadmap/adapters-mocopi-vmc-ardy.md](../roadmap/adapters-mocopi-vmc-ardy.md).
On 2026-07-29 §2 gained two more rules from the same direction: an OpenExec
computation performs no I/O, and `ExecIr` is optional rather than foundational.
Also on 2026-07-29, and before the first adapter directory existed, §1 and §5
corrected those three identities from *bundle* to *plain library plus CLI tool* —
the kind they had to be all along, for the reason stated under §1's identity
table.

`liveTransport` was added on 2026-08-24, ahead of the code it now holds, from
[roadmap/osc-and-vrchat-trackers.md](../roadmap/osc-and-vrchat-trackers.md) §3.2
and §10. It exists to hold code two shipped adapters maintained separately, and
it is the first library here that is neither a member of the aggregate product
nor an adapter — so it needed a row in §1, edges in §2, and an exclusion in §5
before the extraction that filled it could be reviewed. Nothing moved into it in
the same change that named it: that is §6's second invariant, and the roadmap
plan restates it as a rule of its own. **The extraction landed on 2026-08-24, in
its own three changes**, and every claim the contract made about it ahead of time
held: the edge set is empty in fact as well as in prose, the diagnostic split
survived contact with two code enums that disagree about their own default, and
the exclusion's second clause is what kept the library out.

`vrmAdapterVrchatOsc` was added on 2026-08-24, ahead of its first directory, from
the same plan's §5 and §9. It is the **fourth** input-adapter identity, and it
needed a row for a reason the other three did not: those were named together in
2026-07-28's change, so a fourth scaffold landing without one would be the first
adapter whose identity the tree asserted and this document did not. §2 needed
nothing — `adapters/*` is already the rule, and this adapter needs no edge that
rule does not already permit — so what the row adds is the one claim §2 cannot
make: **it is not a pose source**, and the humanoid solve is deliberately outside
it.

It declares **one** of the three edges §2 allows, and that is a fact about the
adapter rather than about the permission. `motionCore` and `motionRuntime` are
what an adapter takes when it produces canonical values, and its first milestone
produces none — it is a scaffold and a recorder, with no decoder — so declaring
them would have claimed a dependency the library does not have. They arrive with
the code that produces a pose. A permission is not a requirement, and this is the
first row here where the two are visibly different.
§5 gains its artifact name on the terms every adapter artifact already has.

`osc` was added on 2026-08-29, ahead of the decoder it now holds, from the same
plan's §3.1, §4 and §10. It is the second library here that is neither a member
of the aggregate product nor an adapter, and it arrives on the schedule §2's own
note below set for it rather than beside `liveTransport`: a shared decoder needed
a **second consumer** first, because the only evidence that a surface is neutral
is a caller that never says `VMC`. That caller was measured before this row was
written — an address inventory of a VRChat session, decoding through
`vrmAdapterVmc`'s decoder without moving it, needed five VMC tokens and every one
of them was the *name*: the include path and four namespace qualifications. No
VMC address literal, no bone, no `VmcMessage`, no `SkeletonMap`. What it did emit
was `VRM_VMC_PACKET_MALFORMED`, in a report about a session VMC has nothing to do
with, which is §8's open question arriving as a measurement rather than as a
prediction.

So this row is narrower than `liveTransport`'s in the one way that matters. That
library was named before anyone knew what would fit through the seam; this one is
named after a caller went through it, so §4's ownership list is a description
rather than an intention.

## 1. Bundles and libraries

Shipped through Workspace Phase 7:

| Identity | Kind | Role |
| --- | --- | --- |
| `vrmSchema` | plugin bundle (`usd-schema`) | VRM schema APIs (`VrmHumanoidAPI`, `VrmExpressionAPI`, `VrmLookAtAPI`, `VrmSpringBoneAPI`, `VrmColliderAPI`, `VrmConstraintAPI`), schema tokens, `schema.usda` + generated sources, schema contract version |
| `usdVrmFileFormat` | plugin bundle (`usd-fileformat`) | `.vrm` `SdfFileFormat`, VRM 0.x / 1.0 detection, glTF/VRM parsing, canonical model (private), USD authoring (geometry, materials, skeleton, animation, schema application), import diagnostics |
| `usdVrmPackageResolver` | plugin bundle (`usd-package-resolver`) | `avatar.vrm[images/...]` package path resolution, embedded resource byte access, malformed/truncated/out-of-range rejection |
| `vrmContainer` | plain CMake library (`libs/`) | GLB header/chunk parsing, buffer-view access, byte-range validation, immutable byte views — shared by file format and resolver |
| `vrmCore` | plain CMake library (deferred) | canonical model, only if a second consumer beyond the importer appears |
| `usdVrm` | aggregate product name | retired as a bundle id; names the aggregate package composed of the bundles above |

Motion layer (Workspace Phase 6–8; motion policy §2, §14):

| Identity | Kind | Role |
| --- | --- | --- |
| `usdVrmaFileFormat` | plugin bundle (`usd-fileformat`, v0.3.0) | `.vrma` `SdfFileFormat`, glTF/GLB animation parsing, canonical semantic `HumanoidSkeleton`, `UsdSkelAnimation` + provenance. Avatar-independent: it never resolves, binds to, or retargets onto a target VRM. |
| `execMotion` | plugin bundle (reserved) | Vendor-neutral OpenExec motion nodes: clip sample, pose buffer, resample, filter, blend, apply-constraints, generate, record |
| `execVrm` | plugin bundle (reserved) | VRM semantics applied to a target rig: humanoid retarget, root-motion resolve, expression, look-at, avatar apply — driven by the schema contract only |
| `motionCore` | plain static CMake library (v0.3.0) | `motion::HumanoidPose`, `HumanoidAnimation`, `RootMotion`, `MotionConstraintSet`, source metadata. No USD stage authoring, no vendor SDK, no network. |
| `motionRuntime` | plain static CMake library (v0.4.0) | Timestamped pose buffer, interpolation/extrapolation, resample, filter, blend — the OpenExec-independent runtime |
| `vrmRetarget` | plain static CMake library (v0.4.0) | Humanoid map, rest pose, pose retargeter, root-motion policy. **Completed before OpenExec** (motion policy §18.12). Expression resolution stays with Motion Phase G. |
| `motion_retarget` | CLI executable (`tools/motionRetarget`, v0.4.0) | Reads the target rig and the semantic clip off stages, drives `vrmRetarget` over plain values, authors the retargeted `UsdSkelAnimation` and its `skel:animationSource` binding. Not a bundle — it registers nothing with OpenUSD. |
| `motion_capture` | CLI executable (`tools/motionCapture`, v0.5.0) | Replays a recorded capture trace through `LiveCaptureSource` and authors the avatar-independent semantic clip — the same shape `usdVrmaFileFormat` produces, so `motion_retarget` consumes it unchanged. Does **not** link `vrmRetarget`: it stops at the clip. Not a bundle. **It gains no adapter source, and that is the settled answer rather than a deferral** — a live session reaches it as a trace written by the adapter's own tool, so this row is the same after the first adapter as before it (§2). |
| `liveTransport` | plain static CMake library (`libs/liveTransport/`) | The live half's shared leaf: the UDP receiver, the optional datagram queue, the packet-capture file format, and the diagnostic **vehicle** — the struct, its severity and recoverability defaults, and its formatted form — that every live adapter raises through. It knows no protocol: no OSC, no vendor grammar, no address literal, no product name. It holds no diagnostic **code** either; a code enum is frozen per adapter and stays there (§2). Its edge set is **empty**, and that is a measurement rather than an intention — the six files it is extracted from include their own headers and the standard library and nothing else (measured 2026-08-24). Outside the aggregate product, on the *adapter* side of §5's split though it carries no product name. |
| `osc` | plain static CMake library (`libs/osc/`) | The OSC 1.0 wire format, and nothing that uses it: packet and bundle decoding, bundle flattening, address extraction, type-tag validation, argument access, and a refusal that names the byte and the address it refused at. It knows no address *semantics* — `/VMC/...`, `/tracking/...` and `/avatar/...` are all just addresses here — no bone name, no tracker role, no coordinate convention and no product name. Like `liveTransport` it holds no diagnostic **code**, and unlike `liveTransport` it holds no enum in place of one either: the decoder makes exactly one distinction — decodable or not — so its refusal is a typed value carrying a subject and a detail, and the caller that knows which adapter it is supplies the code (§2). Its edge set is **empty**, `liveTransport` included: a decoder that never reads a socket needs nothing a transport owns, and the two are siblings rather than a stack. Outside the aggregate product, for a reason §5 had not needed until this row. |
| `vrmAdapterVmc` | optional plain static CMake library (reserved, `adapters/liveCapture/vmc/`) | The generic real-time input: OSC-over-UDP decode, frame assembly, VRM humanoid bone names → canonical semantics. One adapter serves every sender application, including capture products relayed through it. **First adapter implemented.** |
| `vrmAdapterMocopi` | optional plain static CMake library (`adapters/liveCapture/mocopi/`, v0.7.0) | **Live UDP only.** Decodes one capture product's native packets into canonical humanoid semantics and pushes them at `LiveCaptureSource`. Direct path: keeps the SDK-specific confidence and device diagnostics a protocol relay drops. Does **not** wrap `vrmAdapterVmc`, and does **not** read that product's recorded files — a recording is a file format, and file formats are `motionBvh`'s (below). |
| `vrmAdapterVrchatOsc` | optional plain static CMake library (`adapters/liveCapture/vrchatOsc/`) | **A tracker source, not a pose source.** Decodes the VRChat OSC Trackers surface — numbered tracker observations, which are pre-IK — off OSC over UDP, and stops at a tracker frame: a tracker index is not a body role, so turning one into humanoid semantics is a separate and generic boundary rather than this adapter's ([the OSC track](../roadmap/osc-and-vrchat-trackers.md) §5). The third live adapter, and the one whose arrival turned the transport ring into a library rather than a third copy of it. A sibling of the other two and never a stack: it holds no VMC decoder and reaches OSC through a shared one when there is one. |
| `vrmAdapterArdy` | optional plain static CMake library (reserved, `adapters/generators/ardy/`) | One generator behind the vendor-neutral `IMotionGenerator` contract, producing canonical humanoid motion that `vrmRetarget` maps onto a target rig. |
| `vmc_record` | CLI executable (`adapters/liveCapture/vmc/tools/vmcRecord/`, v0.5.0) | Records a live VMC session to a `vmc-packet-capture` file and reports what it decoded to; `--inspect` reports on a recorded capture with no socket; `--export-trace` writes what the adapter delivered as a `motion-capture-trace`, which is the whole of this adapter's hand-off to the product's tools (§2). Links `vrmAdapterVmc` and nothing else: it neither retargets nor authors a stage, though §2 would permit both. |

Each adapter may also carry one CLI, declared beside it as an
`openstrata.tool.yaml` workspace tool in the way `motion_retarget` and
`motion_capture` are. Those executables are named when they are written, not
reserved here — `vmc_record` is the first, added with the VMC adapter's CLI.

Recorded motion sources — the file half of the input layer (motion policy §8.3):

| Identity | Kind | Role |
| --- | --- | --- |
| `motionSource` | plain static CMake library (`libs/motionSource/`) | The **format-neutral** intermediate: `SourceSkeleton`, `SourceAnimation`, `SourceProvenance`, the `SourceProfile` contract, the reader for the profile *file*, and the converter from those plus a profile to `motion::HumanoidAnimation`. Knows no file format and no producer — a profile file is data this layer reads, not a motion format it parses. **All of it is implemented**, the converter last. The `motionCore` edge below is carried by four files and no others: `CanonicalMetadata`, which derives canonical provenance, `SourceProfile`, whose joint map has a `HumanBone` on its right-hand side, `SourceProfileFile`, which reads that side out of a file, and `CanonicalConversion`, which is the crossing rather than a corner of one. `CanonicalConversion` is also the only file here permitted a `Gf` type: everywhere else a value in a basis this layer does not know is still not a geometric vector, and the converter is the one file that *does* know the basis. Stage, `Sdf` and plugin APIs stay forbidden in all four — authoring is a caller's. |
| `motionBvh` | plain static CMake library (`libs/motionBvh/`) | BVH **syntax** only — `HIERARCHY`, `ROOT`/`JOINT`, `OFFSET`, `CHANNELS`, `End Site`, `MOTION`, frame time, channel values in declaration order — plus the extractor that turns a `BvhDocument` into `motionSource` values. Decides no semantics: not which joint is which `HumanBone`, not the unit, not the axes, not what a root translation means. **Both halves are implemented**, and the extractor is what took the declared edge below. It still names no OpenUSD of its own — `motionSource` is its one link, and `motionCore`'s `Gf` value types arrive behind it and are named nowhere here. |
| `motion_bvh_inspect` | CLI executable (`tools/motionBvh/`, v0.6.0) | Reports what a BVH file contains, and optionally which profiles are candidates for it, with the reasons. Links `motionBvh` and nothing else. **The reporting half is implemented**; candidate profiles arrive with the profile contract, because a detector written before it would settle the profile schema on whichever file was inspected first. |
| `motion_bvh_convert` | CLI executable (`tools/motionBvh/`, v0.6.0) | BVH + an explicitly named profile → the avatar-independent semantic clip `motion_retarget` already consumes. Links `motionBvh` and `motionSource`, and authors a stage. Never binds to a target avatar. **Implemented.** It is the first program anywhere that holds a reader and a profile at once, which is why the six *semantic* diagnostics are raised here and nowhere lower: `MatchSourceProfile` returns a typed refusal naming the event, and this is the caller that maps it onto the reader's frozen codes. There is no default profile — a missing `--profile` is `VRM_BVH_PROFILE_REQUIRED` and stops the run — and a profile **id** is resolved to a file relative to the executable, so a packaged artifact finds the profiles shipped beside it. Its boundary check is a different one from `motion_bvh_inspect`'s and runs per target: it may author a stage and speak the humanoid vocabulary, and it still may not name `vrmRetarget` or `vrmSchema`. |
| motion source profiles | package data (`profiles/motion/*.yaml`) | One declarative file per producer *and export preset*: joint map, coordinate basis, unit, root and rest-pose policy, required/optional joints, provenance label. Data, never code — see below. |
| `motionFbx` | plain static CMake library (deferred) | A second reader behind the same `motionSource` boundary, if and when a consumer needs FBX. Named here so the boundary is designed for two readers rather than retrofitted for the second. |
| `usdBvhFileFormat` | plugin bundle (deferred) | A thin `SdfFileFormat` over `motionBvh`, only if reading `.bvh` directly off a stage is wanted. It would re-implement no parsing and no conversion. |

> **A producer profile is the one place a product name may appear outside
> `adapters/`, because it is data and not a branch.** `profiles/motion/` holds
> files named for Mocopi, Rokoko Studio, MotionBuilder and Blender, which
> reads at first like the rule below being broken. It is not, and the distinction
> is worth stating precisely: the *rule* forbids product-conditional code in the
> core, and a profile is a declaration the code never has a name for. `motionBvh`
> and `motionSource` contain no producer identifier, no `if (producer == ...)`,
> and no default profile — a caller names one, or the conversion is refused
> (`VRM_BVH_PROFILE_REQUIRED`). Ship every profile file and the libraries are
> byte-identical; that is the test of whether this line has been crossed.
>
> The profile **id** carries producer, format, skeleton preset, and contract
> version — `<producer>-<format>-<preset>-v<N>` — because a producer is not a
> profile: one application's export presets can disagree with each other, and two
> applications can agree. Application versions belong in a corpus manifest; the
> contract version moves only when a producer's output contract breaks.

A profile file is declarative and stays that way: mappings, units, axes, root
policy, rest-pose policy, and required/optional joints. **No arbitrary code, no
expression language, no embedded producer-specific algorithm, and no target VRM
path** — a profile that could name an avatar would have made the converter
avatar-aware through the back door. A producer that genuinely needs an algorithm
gets a profile implementation in code, not a richer file format.

> **A profile refuses a rig in terms no format supplies, and the caller turns
> that into a diagnostic.** The semantic half of a reader's frozen diagnostic set
> is raised where a document meets a profile — which is `motionSource`, the one
> library forbidden to know that reader exists (§2). So `MatchSourceProfile`
> returns a typed `SourceProfileRefusal` naming the event, and whoever holds both
> a reader and a profile maps it onto that reader's codes. A `VRM_BVH_*` string
> in `motionSource` is the reversal however it got there, and a second
> `VRM_MOTION_SOURCE_*` namespace would give one event two spellings; the
> argument is in
> [recorded-motion-sources.md §10](../roadmap/recorded-motion-sources.md).

> **An adapter is a library, not a plugin bundle.** The three rows above read
> "optional bundle" until 2026-07-29, which no manifest could have expressed. An
> `openstrata.plugin.yaml` declares one of OpenUSD's plugin kinds and points at a
> `plugInfo.json`; an adapter has neither, because §2 keeps it away from
> `vrmSchema`, from every file-format bundle, and from OpenExec, leaving it
> nothing to register. Its entire output is `motionCore` values pushed at a
> `motionRuntime` source. So an adapter is a plain static CMake library carrying
> an `openstrata.library.yaml`, exactly as `motionRuntime` and `vrmRetarget` are
> — which is also the only form in which §2's adapter-library / adapter-tool
> split is expressible in a manifest rather than only in prose.
>
> §5 is unaffected in substance: the artifact name and the aggregate exclusion
> are the same rule under either reading, and under *neither* is `ost` 0.21.0
> able to emit one — `plugin package` takes a bundle directory or `--workspace`
> over bundles, with no per-library equivalent. That was equally true of the
> "bundle" wording, which could not have produced a valid manifest to package.
> What the correction buys is that an adapter's dependencies become *declarable*
> in the one form the workspace graph reads — `requires.libraries` — rather than
> living in a manifest `ost` would reject. Whether the graph gate then reaches
> them is a separate, measured question; §2 has the answer, and today it is
> "not yet".

`adapters/` is the only place product, SDK, protocol, or research-model names
are permitted. The three above are **siblings, not a stack**: no adapter may
depend on another, and there is deliberately no `adapters/common/` until two of
them are shown to duplicate code that carries no vendor semantics. Their plan,
including the implementation order and per-adapter acceptance criteria, is
[roadmap/adapters-mocopi-vmc-ardy.md](../roadmap/adapters-mocopi-vmc-ardy.md).

> **That condition is met, and `adapters/common/` is still not the answer**
> (measured 2026-08-23,
> [the census](../roadmap/osc-and-vrchat-trackers.md#2-the-duplication-census)).
> With vendor identifiers erased and comments stripped, `vrmAdapterVmc` and
> `vrmAdapterMocopi` hold one packet-capture implementation twice — 6 changed
> lines across 800 — one UDP receiver twice, and one live-source bridge twice.
> The receiver pair is the one that has already cost something: the mocopi
> header records four defects the sibling has identically because they were
> copied, fixed all four on 2026-08-11, and noted that they remain in
> `vrmAdapterVmc` — where they still are. The sibling rule above is what forbids
> the obvious fix: a shared leaf between two leaves is the adapter → adapter
> edge wearing a hat.
>
> **They are new libraries, and each takes a different side of an
> existing split.** The live-source bridge holds poses and belongs beside
> `LiveCaptureSource` in `motionRuntime`. The transport ring — socket, capture
> file format, diagnostic vehicle — **cannot**, and the refusal is already
> executable rather than a matter of taste:
> `libs/motionRuntime/tests/check_boundaries.py` rejects `winsock`,
> `sys/socket.h`, `asio`, `curl` and `websocket` in that library's sources,
> because `motion_capture` is a member of the aggregate product and links it,
> and no tool in the product opens a transport. So it needs a leaf of its own that the product does not link, which
> takes the *adapter* side of §5's aggregate split even though it carries no
> product name — the exclusion is about what the product may depend on, not only
> about what a name says. An OSC decoder shared by two protocol adapters is a
> second such library on the same terms.
>
> **Both are now above, and they were named eleven weeks apart for a reason
> worth keeping.** `liveTransport` went in first because its extraction was the
> next change to be reviewed and a reviewer cannot check a move against a
> contract that does not name its destination. The OSC decoder waited, because
> it had one consumer and the evidence that a surface is neutral is a caller
> that never says `VMC` ([the OSC track](../roadmap/osc-and-vrchat-trackers.md)
> §3.1). Naming it then would have settled a boundary on the only caller there
> was, which is the failure the second-consumer rule exists to prevent. `osc`
> is in the table as of 2026-08-29, after that caller was written and measured,
> in the order the same document sets: measured first, reconciled second, moved
> third.

> **A runtime route is not a build edge.** A capture application may act as a
> VMC sender, so a user's data can travel `mocopi app → VMC packet →
> vrmAdapterVmc`. That is a path assembled at runtime and creates no dependency:
> `vrmAdapterMocopi` handles native input and links `motionCore` /
> `motionRuntime` only, exactly as `vrmAdapterVmc` does. The ordering above
> (VMC implemented first, the native adapter second so the two paths can be
> compared on the same motion) is the roadmap's; the identities and the sibling
> rule are this contract's, and they do not move with it — including when the
> roadmap re-ordered its releases on 2026-08-03.

> **Two unrelated things are called "adapter" in this repo.** The bundles above
> are *input* adapters: vendor and protocol leaves under `adapters/`. The
> `ExecIr` adapter named in
> [the OpenExec plan §3](../roadmap/openexec-foundation.md) is an internal
> insulation layer inside `execVrm`, confining a possibly-experimental OpenUSD
> dependency. Neither is in the other's dependency graph.

Shared code is never a plugin bundle: `vrmContainer` has no plugin
registration, no `plugInfo.json`, and no OpenUSD types in its public API. The
same rule binds `motionCore`, `motionRuntime`, `vrmRetarget`, and every adapter
library under `adapters/` — and `motionCore` additionally carries no OpenUSD
*stage* dependency, only value types (`GfVec3f`, `GfQuatf`).

Product names (`Mocopi`, `ARDY`, any SDK or research-model name) are forbidden
in every identity above except `adapters/`. They may otherwise appear only in
`tests/integration/`, `examples/`, and provider metadata strings — never as a
branch condition in core logic (motion policy §8.1, §9).

## 2. Dependency directions

Allowed:

```text
usdVrmFileFormat      -> vrmSchema
usdVrmFileFormat      -> vrmContainer
usdVrmPackageResolver -> vrmContainer

usdVrmaFileFormat     -> vrmContainer
usdVrmaFileFormat     -> motionCore
motionRuntime         -> motionCore
vrmRetarget           -> motionCore
vrmRetarget           -> motionRuntime
motion_retarget       -> vrmRetarget, motionRuntime, motionCore, OpenUSD stage
motion_capture        -> motionRuntime, motionCore, OpenUSD stage
execMotion            -> motionCore, motionRuntime
execVrm               -> vrmSchema
execVrm               -> motionCore, motionRuntime, vrmRetarget
adapters/*            -> motionCore, motionRuntime, liveTransport, osc
adapters/*/tools/*    -> vrmRetarget, OpenUSD stage authoring
liveTransport         -> nothing — its allowed edge set is empty, not short
osc                   -> nothing — the same, and `liveTransport` is in the
                         prohibitions below rather than absent from here

motionSource          -> motionCore
motionBvh             -> motionSource
motion_bvh_inspect    -> motionBvh
motion_bvh_convert    -> motionBvh, motionSource, motionCore, OpenUSD stage
```

The last two lines of the adapter block are not the same permission. An
**adapter library** converts a vendor or protocol input into canonical motion
values and stops there; an **adapter tool** (its CLI) may go on to retarget and
author a stage, exactly as `motion_retarget` and `motion_capture` do. The moment
retarget or USD authoring lives inside an adapter library, that adapter has
become a second motion pipeline.

**No product tool depends on an adapter, and the arrow that would have said so
is deliberately absent.** `motion_capture` is a member of the aggregate product
(§5) and every adapter is excluded from it, so an edge from the first to the
second would have carried a protocol decoder, its network code, and a product
name into the product artifact — and it would have done so once per adapter,
since `--source vmc` invites `--source mocopi` behind it. The hand-off is a file
instead. An adapter's tool writes what its adapter delivered as a
`motion-capture-trace`, which is that format's stated content — *what an adapter
delivered, after protocol decode and coordinate conversion, before any intake
policy* (`motionRuntime/CaptureTrace.h`) — and `motion_capture` replays it
through `LiveCaptureSource` exactly as it replays any other trace, unchanged and
knowing nothing about where it came from.

That keeps three properties that an in-process live source would each have cost.
No tool in the product opens a transport or reads a wall clock, which is what
makes every clip in this repository reproducible by construction. A session
becomes a clip in exactly one place, so there is no second path from a live
sender to an avatar for the two to disagree along. And the adapter stays
separately shippable, because nothing in the product links it.

The cost is stated rather than hidden: a live session is two commands, not one.
That is the shape this repository already had — `vmc_record` exists because an
operator keeps a session as a file — and the intermediate is a canonical trace
carrying no VMC vocabulary at all, so the second command is the same one a
`.vrma` clip or a generated fixture goes through.

**That rule binds libraries, not only tools, and the difference is about to
matter.** `motion_capture` links `motionRuntime`, so a transport placed *there*
puts a socket in the product's link closure — the same property, lost through
the library rather than through the `--source vmc` flag that was already
refused. `libs/motionRuntime/tests/check_boundaries.py` already enforces it by
refusing socket headers in that library. It is worth stating as a *contract*
rather than leaving it to the check, because the two adapters duplicate a UDP
receiver and a packet-capture format today (§1), and `motionRuntime` is the
first place a reader looks for their shared home — a reader who finds only the
check may read it as an oversight to be amended. The shared transport is
`liveTransport`, a leaf the product does not link; the shared *pose* bridge,
which holds no socket, is `motionRuntime`'s and is the one piece of that
duplication this rule permits to move there.

**`liveTransport`'s prohibitions are the same rule read from the other end, and
one of them is not about the product at all.** Two say what may not depend on
it, and they are what keep a socket out of the aggregate's link closure however
it is reached — through a tool, through `motionRuntime`, or through a reader.
The rest say what *it* may not depend on, and they exist because a shared leaf
fails by growing rather than by being misplaced: the first `motionCore` value in
it makes it a motion library, the first address literal makes it a protocol
decoder, and the first adapter's code enum makes one adapter's frozen
diagnostics into every adapter's. Its empty edge set is what makes all three
checkable at a glance rather than by argument — a library with no permitted edge
has no ambiguous one, and any edge at all is a contract change.

**`osc`'s prohibitions are `liveTransport`'s with one line that is not a copy,
and it is the `<->`.** Every other shared-leaf rule here is asymmetric, because
one side is a layer and the other is what may reach it. These two are the same
layer twice, and the day one of them acquires the other is the day the pair
stops being two libraries: a decoder that can open a socket has become a
receiver, and a receiver that can decode has become an adapter with no adapter
around it. Neither direction is more plausible than the other, so neither gets
to be the one nobody wrote down.

**And the enforcement runs the wrong way round here, which is worth knowing
before the green result is read as coverage.** `liveTransport` lives under
`libs/`, so the workspace graph discovers it and validates its (empty) edges,
while the adapters that link it are invisible to the same gate for the reason
below. The shared half of this extraction is gated and the consuming half is
not — so the binary link check each adapter already carries is what proves the
edge in the direction that matters, exactly as it does for the two core
libraries today.

The four `motionSource` / `motionBvh` lines are a chain and are meant to be read
as one: a **reader** knows a file format and no semantics, `motionSource` knows
semantics and no file format, and a **profile** supplies what neither can know
on its own. The arrow `motionBvh -> motionSource` never reverses — the day
`motionSource` gains a BVH-shaped field is the day a second reader cannot be
added without changing it, which is the entire reason the layer exists before a
second reader does.

Forbidden (non-exhaustive; anything not allowed above is forbidden):

```text
vrmSchema             -> any other bundle or library
usdVrmPackageResolver -> usdVrmFileFormat, vrmSchema
usdVrmFileFormat      -> usdVrmPackageResolver (link-time; resolver is a
                         runtime bundle dependency only)
usdVrmFileFormat      -> usdVrmaFileFormat, motion generator, any motion library
execVrm               -> usdVrmFileFormat private API, importer canonical model
execVrm               -> GLB parser (vrmContainer, cgltf), reparse of the
                         source .vrm / .vrma bytes, joint-name heuristics
execMotion/execVrm    -> socket or device I/O, file watching, a wall clock, a
                         private thread pool, or mutable global state inside a
                         computation callback (see below)

motionCore            -> any vendor SDK, any product-named code, any network
                         protocol, any OpenUSD stage authoring
motionRuntime         -> vrmSchema, any USD file-format bundle
vrmRetarget           -> network protocol, OpenExec
usdVrmaFileFormat     -> live receiver, generator, vrmRetarget, a target VRM
motionCore/motionRuntime/vrmRetarget -> adapters/*  (adapters depend on the
                         core; the core never depends on an adapter)
execMotion/execVrm    -> adapters/*  (same rule, one layer up: an OpenExec
                         node never reaches for a vendor input)
adapters/<a>          -> adapters/<b>  (adapters are siblings, never a stack)
adapters/*            -> vrmSchema, any USD file-format bundle, vrmRetarget
                         (the *library*; its tool may — see above)
adapters/*            -> OpenExec, ExecIr, or emitting ExecIr values

liveTransport         -> motionCore, motionRuntime, vrmRetarget, motionSource,
                         motionBvh, vrmContainer, vrmSchema, any USD
                         file-format bundle, OpenExec, ExecIr, adapters/*
liveTransport         -> a protocol grammar, an OSC or vendor address literal,
                         a product or SDK name, or any adapter's diagnostic code
motionCore/motionRuntime/vrmRetarget/motionSource/motionBvh -> liveTransport
execMotion/execVrm    -> liveTransport
motion_capture/motion_retarget/motion_bvh_inspect/motion_bvh_convert
                      -> liveTransport  (no member of the aggregate product
                         links a transport, §5)

osc                   -> motionCore, motionRuntime, vrmRetarget, motionSource,
                         motionBvh, vrmContainer, vrmSchema, any USD
                         file-format bundle, OpenExec, ExecIr, adapters/*
osc                   -> a VMC, VRChat or vendor address literal, a bone or
                         tracker name, a coordinate convention, a product or
                         SDK name, or any adapter's diagnostic code
osc                   <-> liveTransport  (both directions: a decoder that reads
                         no socket and a transport that knows no grammar are
                         siblings, and either edge would make one of them the
                         place the other's rules stop applying)
motionCore/motionRuntime/vrmRetarget/motionSource/motionBvh -> osc
execMotion/execVrm    -> osc

motionCore            -> ExecIr
vrmRetarget           -> ExecIr
usdVrmFileFormat      -> authoring ExecIr prims as a requirement of import

motionSource          -> motionBvh, motionFbx, or any other reader
motionCore            -> motionSource, motionBvh
motionRuntime         -> motionBvh, motionSource
motionBvh             -> motionFbx, and any future reader -> any other reader
motionBvh             -> vrmRetarget, vrmSchema, any USD file-format bundle
motionBvh/motionSource-> adapters/*  (and adapters/* -> motionBvh, motionSource:
                         live input and file input meet at canonical motion and
                         nowhere earlier)
motionBvh             -> a producer name in code, a default profile, or a
                         joint-name heuristic standing in for one
motionSource/motionBvh-> a target VRM joint index, a target rest pose, or any
                         retarget step (that is vrmRetarget's, once)
any cycle, including self-cycles
```

Five of these are the motion layer's load-bearing invariants, restated so a
reviewer can check them without opening the policy:

- **`vrmRetarget` does not depend on OpenExec.** The retarget core is finished
  and testable before any OpenExec node exists (motion policy §10.1, §18.12);
  `execMotion` / `execVrm` nodes are thin wrappers over it.
- **`usdVrmaFileFormat` is avatar-independent.** It authors a canonical semantic
  humanoid skeleton, never a target skeleton's joint order. Retarget is a
  separate, later step (motion policy §4.2, §4.3).
- **The dependency arrow points at the core, never at an adapter.** Every
  adapter is a leaf — of the core, of the OpenExec nodes, and of each other.
  This is what lets a capture product, a sender application, or a generation
  model be swapped without touching retarget, runtime, OpenExec, or the
  importer.
- **An OpenExec computation evaluates an immutable snapshot and performs no
  I/O.** Receiving is the adapter's job and buffering is `motionRuntime`'s; a
  callback that opened a socket or read a clock would make cache reuse and
  invalidation untestable, which is the whole reason to be on OpenExec at all
  ([OpenExec plan §5](../roadmap/openexec-foundation.md)).
- **`ExecIr` is optional and never a prerequisite.** It is confined to an
  adapter layer inside `execVrm`; the canonical motion contract is not derived
  from its representation, the importer never has to author its prims, and the
  offline pipeline stays whole with it absent
  ([OpenExec plan §7.0](../roadmap/openexec-foundation.md)).

Enforcement: `ost plugin test --workspace` (ost >= 0.15.0) validates the
bundle graph declared via `requires.bundles` before running any bundle's
verification, with stable `WORKSPACE_*` issue codes (dependency missing,
version mismatch, contract mismatch, direction forbidden, cycle) and exit 5
on violation. Bundle manifests are the source of truth for these edges.

Plain-library edges (`requires.libraries`) became executable in ost 0.16.0: a
plain library carries an `openstrata.library.yaml` descriptor
(`libs/vrmContainer/`) giving it a workspace identity and CMake package/target,
and the workspace graph validates the `bundle -> library` edges (missing,
duplicate, version-incompatible, cyclic) alongside the bundle edges. `ost plugin
build/test/run` build and install the library into the workspace prefix before
its consumers and materialize its loader directory into the session; `ost plugin
package` stages the closure under `runtime/libraries/` with a
`dependencies.json` record. `vrmContainer`'s no-registration / no-OpenUSD
boundary is still enforced by its own repo check, and each consumer adds a
binary link check (`dumpbin`/`nm`) proving it imports `vrmContainer` and does not
import the other bundles' libraries (`usdVrmPackageResolver` proves it links
neither `usdVrmFileFormat` nor `vrmSchema`).

Adapters declare through that same door (§1): an adapter library states
`adapters/* -> motionCore, motionRuntime` in its `openstrata.library.yaml`, and
**the graph gate walks those edges**. It did not always: under `ost` 0.21.0,
plain-library discovery was the project root's immediate subdirectories plus
`libs/`, so a descriptor at `adapters/<group>/<name>/` was invisible and the
reported library count did not move when one was added — an adapter's declared
edges were accurate documentation rather than an enforced gate
([report 34](../reports/ost/34-2026-07-29-v0.21.0-adapter-library-discovery-gap.md)).
0.22.x widened discovery, and this repository declares the member set outright:
`openstrata.toml`'s `[workspace].members` names all twenty descriptors, and a
descriptor no pattern covers is a hard error rather than a silent omission.
Measured on 0.22.3 — `4 bundle(s), 1 bundle edge(s), 10 libraries, 16 library
edge(s), valid`, where the ten are the seven under `libs/` and the three
adapters.

Two things carry the enforcement in the meantime, and both are required of every
adapter. The workspace CMake tree builds it, so a link against something it may
not have fails the build on all three OS. And it carries the same binary link
check its neighbours do, proving it imports the two core libraries and imports
no sibling adapter, no `vrmRetarget`, and no plugin bundle — which is what
covers the sibling rule and the prohibitions above the line in any case, since
nothing declares an edge it is forbidden to have.

## 3. Schema contract versioning

- `vrmSchema` carries two independent versions: `plugin.version` (semantic
  implementation version) and `schema.contract` (authored-data contract).
- Compatible implementation releases keep `schema.contract` unchanged. A
  breaking type/property/token change increments it and requires
  authored-data migration notes.
- Consumers select the contract explicitly in their manifest:

```yaml
requires:
  bundles:
    - id: vrmSchema
      version: ">=0.2,<0.3"
      contract: 1
```

- `execVrm` reads the schema contract from the stage only — never importer
  internals.

## 4. Workspace root responsibilities

The root owns composition, not implementation:

- bundle discovery and workspace-wide configuration
- integration tests (`tests/integration/`): schema+format, format+resolver,
  full composition, clean-install, aggregate packaging
- the CI matrix (`openstrata.ci.yaml`) and generated lanes
- aggregate packaging and compatibility reporting

The root must not own plugin C++ sources, schema sources, plugin
`plugInfo.json`, or bundle-specific third-party dependency setup. Bundles may
be composed with `add_subdirectory` in the workspace build, but every bundle
must also build standalone against installed packages
(`find_package(vrmSchema CONFIG REQUIRED)` etc.); sibling
`add_subdirectory(../otherBundle)` from inside a bundle is forbidden.

## 5. Artifact naming and versioning

> **What a consumer writes to use one of these is
> [PACKAGE_CONTRACT.md](PACKAGE_CONTRACT.md), not this section.** This section
> names artifacts and decides aggregate membership; it has never stated the
> package name, exported target, header root or required packages that a project
> outside this repository needs, and readers kept arriving here for them. That
> split was made on 2026-08-29, after two installed packages named an imported
> target no consumer could resolve while every lane was green — a defect neither
> document would have caught, because nothing in this workspace had ever opened a
> package config file. Nothing in this section changed with the split.

Per-bundle artifacts plus one aggregate:

```text
vrmSchema-<version>-<target>.tar.zst
usdVrmFileFormat-<version>-<target>.tar.zst
usdVrmPackageResolver-<version>-<target>.tar.zst
usdVrmaFileFormat-<version>-<target>.tar.zst
execMotion-<version>-<target>.tar.zst          (when it exists)
execVrm-<version>-<target>.tar.zst             (when it exists)
usd-vrm-plugins-<version>-<target>-plugin-product.tar.zst (aggregate)
```

Adapter artifacts are named `vrmAdapter<Name>-<version>-<target>.tar.zst`, carry
the adapter library together with its CLI tool, and are **never** part of the
aggregate:

```text
vrmAdapterMocopi-<version>-<target>.tar.zst    (when it exists)
vrmAdapterVmc-<version>-<target>.tar.zst       (when it exists)
vrmAdapterVrchatOsc-<version>-<target>.tar.zst (when it exists)
vrmAdapterArdy-<version>-<target>.tar.zst      (when it exists)
```

Those four were a naming rule for artifacts nothing could emit, and as of
2026-08-25 the first of them exists. `ost` 0.22.2 grew
`ost library build|test|package` but composed no `requires.libraries`, so it
configured a leaf and refused anything with an edge — and every adapter has at
least one: three for the two that produce canonical values, one for
`vrmAdapterVrchatOsc` while it has no decoder
([report 35](../reports/ost/35-2026-08-24-v0.22.2-release-artifact-membership.md) §2).
0.22.3 composes the closure, and
`ost library package adapters/liveCapture/mocopi` produces
`vrmAdapterMocopi-0.7.0-<target>.tar.zst`: 15 files, the adapter library and
`mocopi_record.exe` together, exactly the shape named above. `liveTransport`
packages too, at 9 files, which is what the prediction later in this section
asked to have checked.

**No lane publishes them.** `release.yml` builds and stages the aggregate's
seven members; producing an adapter artifact is a command someone runs, not
something CI emits, and whether a release should carry them is an open decision
rather than an omission ([report 36](../reports/ost/36-2026-08-25-v0.22.3-canonical-runtimes-and-release-membership.md) §3).

**The "never" above is a declaration as of 2026-08-25, and for one release it
was not.** `ost` 0.21.0 — what the release lane bootstrapped through v0.7.0 —
does not discover a tool descriptor under
`adapters/<group>/<name>/tools/<tool>/`, so the product had **7** members: the
four bundles and the three `tools/` CLIs. `ost` 0.22.x *does* discover them, and
the same command on such a workstation packaged **9** (§3 of the same report),
**10** once the third adapter grew a CLI. Nothing in `openstrata.tool.yaml` or
`openstrata.toml` could decline membership, so for that release the exclusion
this section states as a rule was in fact a property of a pinned version — one
that a pin bump would have turned into a published archive.

`ost` 0.22.3 closes it. `openstrata.toml` now carries a `[workspace]` table
whose `release_members` names the seven and whose `release_exclude` names the
three adapter CLIs, and packaging fails with `AGGREGATE_MEMBERSHIP_MISMATCH`
when the discovered set minus the exclusions is not exactly that list. Measured
on 0.22.3: ten member archives, a seven-member product
([report 36](../reports/ost/36-2026-08-25-v0.22.3-canonical-runtimes-and-release-membership.md)
§3).

**An adapter CLI's own archive exists and is not published.** Packaging emits
`mocopi_record-<version>-<target>.tar.zst` beside the seven, because every
discovered member is packaged whether or not it joins the aggregate. That is not
the adapter artifact named at the top of this section — that one carries the
adapter library *with* its CLI, and `ost` still cannot produce it — so
`release.yml` stages only the release members. What ships is unchanged from
v0.7.0; what changed is that a bare tool archive now exists locally and could be
mistaken for the artifact this section promises.

`release.yml`'s staging step keeps its own count against the tree beside `ost`'s
check, because the declaration is the thing a mistaken commit would edit: moving
`motion_bvh` into `release_exclude` satisfies `ost` and leaves the product a CLI
short.

`liveTransport` is excluded from the aggregate on the same terms and carries no
CLI, so its artifact is named for the library alone:

```text
liveTransport-<version>-<target>.tar.zst
osc-<version>-<target>.tar.zst
```

**It is excluded for what the product would link, not for what its name says** —
the distinction §1 states, made concrete here by the first identity that needs
it. `motionSource` and `motionBvh` are in the product because a producer-neutral
library is safe to ship there; `liveTransport` is producer-neutral too and is
still out, because `motion_capture` linking it would put a socket in the
aggregate's closure and end the property that makes every clip in this
repository reproducible by construction (§2). So a new library's side is
decided by both questions rather than either: *does it name a product* is what
keeps a reader in, *would the product acquire I/O* is what keeps a transport
out, and failing one is enough to be excluded.

**`osc` fails neither, and is out anyway — which is the third question this
section had not needed to ask.** An OSC decoder names no product, and it opens
nothing: it is handed a byte range and returns messages, so a `motion_capture`
that linked it would acquire no transport, no clock and no vendor. Both clauses
above pass. What decides it is the one they take for granted: **no member of the
aggregate product links it, and none can** — nothing in the product reads a
datagram, so the only callers an OSC decoder can have are adapters, which are
excluded by name. That is a weaker reason than `liveTransport`'s and it is
deliberately written as one. `liveTransport` is out because including it would
*break* a property; `osc` is out because including it would ship a library no
member reaches, and the day a product member has a reason to decode OSC this
paragraph is what has to be re-argued rather than quietly outgrown.

That prediction has been checked. It said `liveTransport` would be the first
artifact on this excluded side the toolchain could actually emit, because its
edge set is empty where an adapter's is not — and the premise stopped holding
before the prediction was tested: `ost` 0.22.3 composes an adapter's closure
too, so both sides package now. `ost library package libs/liveTransport`
produces a 9-file archive, and the adapter it was contrasted against produces a
15-file one. The prediction was right about the outcome and wrong about the
reason, which is the half worth recording.

`ost library package libs/osc` produces a **7-file** archive, measured
2026-08-29 — the smallest thing on this side, and smaller than the transport
leaf's nine for the reason its identity row gives: one source file, two headers,
and no platform dependency for a config file to re-find.

`motionSource` and `motionBvh` are **not** adapters and take the opposite
decision: they carry no product name in code, so they belong in the aggregate
product exactly as `motionCore` and `motionRuntime` do, and `motion_bvh_inspect`
/ `motion_bvh_convert` join `motion_retarget` and `motion_capture` as tool
members of it. The profile files ship as package data beside them —
`share/usd-vrm-plugins/profiles/motion/` — because a converter with no profile
available refuses every file it is given, which would make an artifact-only
smoke test of the BVH path impossible to pass.

**That last sentence is the requirement, and through v0.7.0 only `cmake
--install` met it.** A packaged product did not: `ost` packaged a tool member
out of the `directories:` its descriptor declared, had no notion of a data-only
member, and the measured `motion_bvh` archive was exactly its two executables
and its descriptor. Unpacked and run — a *member* archive, on its own, so the
executable sat at `<root>/bin/` — the converter refused a real capture and
named `<root>/share/usd-vrm-plugins/profiles/motion` as the first directory it
looked in, which read at the time as the layout being agreed and only the
staging being missing. The qualification is added in hindsight and the next
paragraph is why: that is one of two installed layouts, and the tool searched
the product's first only after 2026-08-30. Declaring
`directories: [bin, share]` did stage it, and was rejected: `directories:` names
subdirectories of the *member root*, so it would have put the layer's data inside
one tool's directory and the copy that shipped would have stopped being the file
`scripts/check_motion_profiles.py` validates
([recorded-motion-sources.md §10](../roadmap/recorded-motion-sources.md),
[report 35](../reports/ost/35-2026-08-24-v0.22.2-release-artifact-membership.md) §4).

`ost` 0.22.3 supplies the missing owner. `openstrata.toml` declares

```toml
[[workspace.install_data]]
source = "profiles/motion"
destination = "share/usd-vrm-plugins/profiles/motion"
```

and the aggregate carries the mapping as product-owned data: measured on
0.22.3, the product manifest reports `data_files: 3` and the archive stages the
directory verbatim, with the destination recorded in `openstrata.product.json`
rather than copied under any member root. The file that ships is the file
`scripts/check_motion_profiles.py` validates, which is what
`directories: [bin, share]` could not promise.

**The run happened on 2026-08-30 and it failed, which is why the distinction
above was worth keeping.** `scripts/artifact_only_bvh_smoke.py` packages the
product, verifies it, installs it to a prefix outside this repository, and
drives `motion_bvh_convert` there with no `--profile-dir` and no
`USDVRM_MOTION_PROFILE_PATH`. The profiles arrived exactly where this section
says — byte-identical to `profiles/motion/` — and the converter refused the
capture anyway, because `ost plugin product install` lands a tool member at
`<prefix>/tools/<member>/bin/` and the locator looked at
`<exe>/../share/usd-vrm-plugins/profiles/motion`, one directory too shallow
inside the product's own prefix.

The paragraph this replaces recorded that "the layout was agreed and only the
staging was missing", and the agreement was real but with a *different* layout:
it was measured on a member archive unpacked on its own, where the executable
does sit at `<root>/bin/`. Two installed layouts put the data in the same place
relative to the prefix and the tool at different depths inside it, so an
executable-relative rule serves one of them at a time. The locator now carries
both, the smoke passes — 853 frames at 50 Hz, 22 bound joints, from the artifact
alone — and it proves the profile it read was the installed one by moving that
file aside and requiring the refusal to come back.

**None of this changes the destination**, which is the part worth stating: the
contract in this section was right, `[[workspace.install_data]]` puts the files
there, and the defect was one reader of it. That is the argument for the smoke
rather than for more review — the search path was documented, the destination
was documented, the two were written from each other, and they still disagreed
([report 36](../reports/ost/36-2026-08-25-v0.22.3-canonical-runtimes-and-release-membership.md) §4).

That split is the one to check when a future reader arrives: a reader is in the
product if the *library* is producer-neutral, whatever the data beside it is
named, **and it opens nothing**. `vrmAdapterMocopi` stays out because the
library itself decodes one product's packets; `liveTransport` stays out on the
second clause with the first one satisfied, which is why the sentence now has
two.

The adapter exclusion keeps the aggregate free of product names (motion policy §8.1),
but it also keeps optional SDK, network, and model dependencies — and their
license terms — out of the core distribution, and leaves each adapter free to
take its own release and support cadence later. Adapter versions may track the
repository tag at first; the artifact boundary that makes independent
distribution possible exists from the first adapter, not retrofitted.

Initial release rules: bundle identities and artifacts are separate; the git
tag is shared; all bundle versions stay synchronized with the repository
version; no independent release cadence until there is real demand.
Debug-symbol sidecars keep the ost `plugin package` convention
(`*-debug.tar.zst`).

## 6. Migration invariants

Every migration PR must preserve all of these:

1. Authored stage semantics do not change: all fixture stages produce
   baseline-identical output (Phase 0 snapshots are the reference).
2. One plugin boundary moves per PR; structural moves and feature changes
   never share a PR.
3. Each split PR adds (and CI runs) the standalone build of the bundle it
   creates, resolving siblings as installed packages.
4. Manifest and CMake package export are updated in the same PR as the code
   move; the manifest stays the source of truth for bundle metadata.
5. Plugin registration moves are proven by discovery tests in the same PR
   (no silently dropped or duplicated `plugInfo.json` registrations).
6. Package-path semantics (`avatar.vrm[images/...]`) do not change in the
   resolver split.
7. The rename PR (`usdVrm` → `usdVrmFileFormat`) adds no functionality.

## 7. Stage baseline policy (Phase 0)

Before any code moves, the current behavior is frozen as committed baseline
evidence, and every subsequent phase gate compares against it:

- per-fixture USDA snapshots (stage topology, material bindings, skeleton
  topology, animation output)
- schema contract snapshot (types, properties, tokens)
- plugin discovery results and public C++/Python symbol lists
- clean-install smoke results and embedded-texture resolution
- diagnostics codes (the corpus manifest's expected-code table)

A migration PR that changes any baseline artifact is a regression by
definition, regardless of tests passing.

The frozen evidence lives in `tests/baseline/` (see its README for the
artifact inventory and regression criteria) and is generated and verified by
`tools/baseline_freeze.py`; run
`ost plugin run plugins/usdVrmFileFormat -- python tools/baseline_freeze.py --check`
as the gate in every migration PR.

## 8. Phase status

| Phase | Deliverable | Status |
| --- | --- | --- |
| 0 | baseline snapshots + regression criteria | done (`tests/baseline/`) |
| 1 | `vrmSchema` bundle split | done (`plugins/vrmSchema`) |
| 2 | `vrmContainer` extraction | done (`libs/vrmContainer`) |
| 3 | `usdVrmPackageResolver` bundle split | done (`plugins/usdVrmPackageResolver`) |
| 4 | `usdVrmFileFormat` purification/rename | done (`plugins/usdVrmFileFormat`) |
| 5 | workspace packaging (per-bundle + aggregate) | aggregate product done; standalone registration P0 remains open upstream |
| 6a | `motionCore` bootstrap | done (`libs/motionCore`) |
| 6b | `motionRuntime` + `vrmRetarget` bootstrap | done (`libs/motionRuntime`, `libs/vrmRetarget`) |
| 7 | `usdVrmaFileFormat` bundle bootstrap | done (`plugins/usdVrmaFileFormat`) |
| 8 | `execMotion` + `execVrm` bundle bootstrap | not started |

> **Phase 6 was renumbered on 2026-07-18.** It previously read "`execVrm`
> (LookAt first)" — a single phase covering the whole runtime layer. The motion
> policy splits that into three plain libraries and two bundles, so the runtime
> bootstrap is now Workspace Phase 8 and the LookAt-first ordering is retired
> (the retarget core comes first). Documents citing "Workspace Phase 6 =
> `execVrm`" predate this.

Each of Phases 6–8 establishes a boundary only: manifest, CMake package export,
standalone build, discovery test, packaging. Motion Phases A+B fill the shipped
6a/7 boundaries; later behavior belongs to Motion Phases C–H. The two
sequences are not the same milestone, exactly as Workspace Phase 6 and Product
P4 are not.

> **The ladder ends at 8 and does not grow with every new library.** It tracks
> the *migration* out of the single `usdVrm` bundle — §6's invariants are written
> for moving existing code — and that migration is finished but for Phase 5's
> packaging P0 and Phase 8's bootstrap. Greenfield libraries take their identity
> and edges from §1 and §2 and no phase number: `adapters/liveCapture/vmc`
> shipped that way in v0.6.0, and `motionSource`, `motionBvh` and the BVH tools
> arrive the same way. Renumbering the ladder for each of them would make
> "Workspace Phase 0–8" — a string five documents repeat — mean something
> different every release, for no gain in what anyone can check.

Scaffolds for new bundles start from the ost template catalog
(`ost plugin new usd-schema --template usd-schema-cpp`,
`ost plugin new usd-package-resolver`) rather than hand-rolled skeletons.

> **Gate status — closed (ost 0.21.0).** This document called for the §2
> dependency directions to be enforced by a required PR gate from Phase 1 on.
> They now are: the `workspace-graph-pr` cell in `openstrata.ci.yaml` runs
> `ost plugin test --workspace --graph-only`, which validates the graph and
> exits on that result alone — no build, no runtime, milliseconds. Three
> `verify: test` workspace cells then build the root tree and run its CTest
> suite on all three OS, which is what the libraries and CLIs never had.
>
> v0.5.0 had tried to do this by hand and could not finish; the history is
> below, and the adoption is
> [report 33](../reports/ost/33-2026-07-28-v0.21.0-workspace-ci-adoption.md).
>
> The gate is a real one, not a formality: pointing `motionRuntime` at
> `motionCore >=0.9,<1.0` fails it with
> `WORKSPACE_LIBRARY_DEPENDENCY_VERSION_MISMATCH` and a non-zero exit.
>
> **What the gate actually covers (measured on ost 0.20.0, Workspace Phase 6b).**
> `ost plugin test --workspace` reports `4 bundle(s), 1 bundle edge(s), 4
> libraries, 7 library edge(s)` — so it *does* discover plain libraries from
> their `openstrata.library.yaml` and validate their `requires.libraries`
> edges, not only the plugin bundles. It caught a real
> `WORKSPACE_LIBRARY_DEPENDENCY_VERSION_MISMATCH` while Phase 6b was landing
> (the new libraries declared `motionCore >=0.4,<0.5` before `VERSION` moved off
> `0.3.0`), which is precisely the class of break this gate exists for. That
> makes the missing CI wiring more costly than it looked, not less. What the
> gate does **not** do is compile or unit-test a plain library: `--workspace`
> tests bundles. Under ost 0.20.0 `ost ci generate` also emitted one job per
> *bundle* cell, so `motionRuntime`, `vrmRetarget`, and the CLIs got no
> generated cell at all — recorded as an ask in
> [report 28](../reports/ost/28-2026-07-26-v0.20.0-motion-layer-ci-gap.md) and
> answered by 0.21.0's `kind: workspace` cell, which builds and tests them
> through the root tree instead.
>
> **v0.5.0 tried to cover that by hand and did not finish.** `motion-ci.yml`
> built the whole workspace with plain CMake from the repo root — the only
> configuration in which `libs/` and `tools/` targets exist — but was blocked at
> configure time: `pxrConfig.cmake` resolved Python development components to
> the paths of the Python the runtime was *built* against, which exist on no
> hosted runner. It shipped disabled and was deleted when the contract grew the
> cell it had been standing in for. That attempt is why the ask was accepted:
> a repo should not have to hand-roll a lane to test a library it declares
> through `openstrata.library.yaml`, and when it tries, it runs into a second
> problem the contract cannot express.
