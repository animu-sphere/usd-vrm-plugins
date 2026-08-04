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
| `motion_capture` | CLI executable (`tools/motionCapture`, v0.5.0) | Replays a recorded capture trace through `LiveCaptureSource` and authors the avatar-independent semantic clip — the same shape `usdVrmaFileFormat` produces, so `motion_retarget` consumes it unchanged. Does **not** link `vrmRetarget`: it stops at the clip. Not a bundle. *(Gains a live adapter source when the first adapter lands; this row is updated in that PR, not before.)* |
| `vrmAdapterVmc` | optional plain static CMake library (reserved, `adapters/liveCapture/vmc/`) | The generic real-time input: OSC-over-UDP decode, frame assembly, VRM humanoid bone names → canonical semantics. One adapter serves every sender application, including capture products relayed through it. **First adapter implemented.** |
| `vrmAdapterMocopi` | optional plain static CMake library (reserved, `adapters/liveCapture/mocopi/`) | **Live UDP only.** Decodes one capture product's native packets into canonical humanoid semantics and pushes them at `LiveCaptureSource`. Direct path: keeps the SDK-specific confidence and device diagnostics a protocol relay drops. Does **not** wrap `vrmAdapterVmc`, and does **not** read that product's recorded files — a recording is a file format, and file formats are `motionBvh`'s (below). |
| `vrmAdapterArdy` | optional plain static CMake library (reserved, `adapters/generators/ardy/`) | One generator behind the vendor-neutral `IMotionGenerator` contract, producing canonical humanoid motion that `vrmRetarget` maps onto a target rig. |
| `vmc_record` | CLI executable (`adapters/liveCapture/vmc/tools/vmcRecord/`, v0.5.0) | Records a live VMC session to a `vmc-packet-capture` file and reports what it decoded to; `--inspect` reports on a recorded capture with no socket. Links `vrmAdapterVmc` and nothing else: it neither retargets nor authors a stage, though §2 would permit both. |

Each adapter may also carry one CLI, declared beside it as an
`openstrata.tool.yaml` workspace tool in the way `motion_retarget` and
`motion_capture` are. Those executables are named when they are written, not
reserved here — `vmc_record` is the first, added with the VMC adapter's CLI.

Recorded motion sources — the file half of the input layer (motion policy §8.3):

| Identity | Kind | Role |
| --- | --- | --- |
| `motionSource` | plain static CMake library (reserved, `libs/motionSource/`) | The **format-neutral** intermediate: `SourceSkeleton`, `SourceAnimation`, `SourceProvenance`, the `MotionSourceProfile` contract, and the converter from those plus a profile to `motion::HumanoidAnimation`. Knows no file format and no producer. |
| `motionBvh` | plain static CMake library (`libs/motionBvh/`) | BVH **syntax** only — `HIERARCHY`, `ROOT`/`JOINT`, `OFFSET`, `CHANNELS`, `End Site`, `MOTION`, frame time, channel values in declaration order — plus the extractor that turns a `BvhDocument` into `motionSource` values. Decides no semantics: not which joint is which `HumanBone`, not the unit, not the axes, not what a root translation means. **The syntax half is implemented**; it links nothing at all, not even OpenUSD's value types, and the declared edge below arrives with the extractor. |
| `motion_bvh_inspect` | CLI executable (`tools/motionBvh/`, v0.6.0) | Reports what a BVH file contains, and optionally which profiles are candidates for it, with the reasons. Links `motionBvh` and nothing else. **The reporting half is implemented**; candidate profiles arrive with the profile contract, because a detector written before it would settle the profile schema on whichever file was inspected first. |
| `motion_bvh_convert` | CLI executable (reserved, `tools/motionBvh/`) | BVH + an explicitly named profile → the avatar-independent semantic clip `motion_retarget` already consumes. Links `motionBvh` and `motionSource`, and authors a stage. Never binds to a target avatar. |
| motion source profiles | package data (`profiles/motion/*.yaml`) | One declarative file per producer *and export preset*: joint map, coordinate basis, unit, root and rest-pose policy, required/optional joints, provenance label. Data, never code — see below. |
| `motionFbx` | plain static CMake library (deferred) | A second reader behind the same `motionSource` boundary, if and when a consumer needs FBX. Named here so the boundary is designed for two readers rather than retrofitted for the second. |
| `usdBvhFileFormat` | plugin bundle (deferred) | A thin `SdfFileFormat` over `motionBvh`, only if reading `.bvh` directly off a stage is wanted. It would re-implement no parsing and no conversion. |

> **A producer profile is the one place a product name may appear outside
> `adapters/`, because it is data and not a branch.** `profiles/motion/` will
> hold files named for Mocopi, Rokoko Studio, MotionBuilder and Blender, which
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
adapters/*            -> motionCore, motionRuntime
adapters/*/tools/*    -> vrmRetarget, OpenUSD stage authoring

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
`adapters/* -> motionCore, motionRuntime` in its `openstrata.library.yaml`.
**The graph gate does not yet walk those edges**, and the difference is
measured rather than assumed — `ost` 0.21.0 discovers plain libraries in the
project root's immediate subdirectories and under `libs/`, so a descriptor at
`adapters/<group>/<name>/` is invisible to it and the reported library count
does not move when one is added. An adapter's declared edges are therefore
accurate documentation and a standing `ost` ask, not an enforced gate, until
discovery widens. An adapter's CLI inherits that: adding `vmc_record` under
`adapters/liveCapture/vmc/tools/` left `ost plugin test --workspace
--graph-only` reporting the same 4 bundles and 4 libraries it reported before,
so its `openstrata.tool.yaml` is not walked either. Whether a per-adapter
package would find it is untested for the reason §5 gives — there is no
per-library packaging command to try it with.

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
vrmAdapterArdy-<version>-<target>.tar.zst      (when it exists)
```

Those three are a naming rule for when the artifacts exist, not a description of
what the release lane emits: `ost` 0.21.0 packages a plugin bundle or a
workspace of them, and has no per-library command, so an adapter reaches a
consumer through the workspace build until one arrives.

`motionSource` and `motionBvh` are **not** adapters and take the opposite
decision: they carry no product name in code, so they belong in the aggregate
product exactly as `motionCore` and `motionRuntime` do, and `motion_bvh_inspect`
/ `motion_bvh_convert` join `motion_retarget` and `motion_capture` as tool
members of it. The profile files ship as package data beside them —
`share/usd-vrm-plugins/profiles/motion/` — because a converter with no profile
available refuses every file it is given, which would make an artifact-only
smoke test of the BVH path impossible to pass.

That split is the one to check when a future reader arrives: a reader is in the
product if the *library* is producer-neutral, whatever the data beside it is
named. `vrmAdapterMocopi` stays out because the library itself decodes one
product's packets.

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
