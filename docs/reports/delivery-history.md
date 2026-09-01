# Delivery history

The granular delivery log for everything landed so far: the importer build-out,
the schema contract, the package resolver, the workspace split, and the
reliability/release tooling. It is **historical evidence**, reorganized out of
the original `ROADMAP.md` so the [roadmap](../roadmap/) holds only incomplete
work.

[v0.1.0](../releases/v0.1.0.md) shipped §A, §B, §C, and §F as a single `usdVrm`
bundle. The workspace split (§D, §E) landed in
[v0.2.0](../releases/v0.2.0.md) and the negative corpus (§G) in
[v0.3.0](../releases/v0.3.0.md); everything logged here is released as of
[v0.8.0](../releases/v0.8.0.md).

This is not a description of current behavior — see [architecture/](../architecture/)
and [reference/](../reference/) for that — nor of planned work, which is in the
[roadmap](../roadmap/). Per-version summaries are in [releases/](../releases/).

Legend: ✅ done · 🚧 in progress

---

## A. Importer capabilities

Shipped in `usdVrmFileFormat` (the bundle was named `usdVrm` for most of this
work; it was renamed in workspace Phase 4, §E).

- ✅ **Read + canonicalize.** `.vrm` GLB read, VRM 0.x / 1.0 detection, and all
  version differences absorbed into `VrmCanonicalDocument` before any USD is
  authored.
- ✅ **Geometry / materials.** Meshes (points/normals/UV/indices), non-skinned
  node transforms preserved, `UsdPreviewSurface` materials with the full texture
  set (base color, metallic-roughness, normal, emissive, occlusion; wrap modes,
  `KHR_texture_transform`), and MToon metadata (`vrm:mtoon:raw`, shader tagged
  MToon).
- ✅ **Skeleton / skinning.** One `UsdSkelSkeleton` unified across all glTF
  skins, topologically ordered, bind from **inverse bind matrices**;
  `UsdSkelBindingAPI`.
- ✅ **Animation.** glTF skeletal animation → `UsdSkelAnimation` (joint TRS
  clips). Morph-weight animation is not authored.
- ✅ **Front-direction bake.** VRM 0.x −Z front baked into the data
  (mesh/normals/blendshapes/skeleton/clips), so every avatar shares one canonical
  +Z rest pose and a single shared clip library drives all of them. Provenance
  kept in `customData` (`vrm:sourceFrontAxis`, `vrm:frontAxisNormalized`).
  *(design policy §7)*
- ✅ **Lossless preservation + diagnostics.** `vrm:meta` / `specVersion` /
  `vrm:rawExtension` and import warnings (`/Asset.customData.vrm:warnings`).

## B. Schema contract (contract v1)

- ✅ **Every `/Asset/rig/*` control prim is typed:** `VrmHumanoidAPI`,
  `VrmExpressionAPI`, `VrmLookAtAPI`, `VrmSpringBoneAPI`, `VrmColliderAPI`,
  `VrmConstraintAPI`. Raw VRM blocks are still preserved in `customData` as the
  lossless fallback. *(design policy §5)*
- ✅ **Schema specification + versioning policy**
  ([`SCHEMA_CONTRACT.md`](../../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md));
  stages and reports carry `schemaContractVersion = 1`.
- ✅ **Raw-extension ↔ typed-API correspondence table**, schema-compatibility
  tests, and public validator-rule docs.
- ✅ **Two design-policy divergences reconciled** — contract v1 freezes the
  shipped shapes: spring bones stay under `/Asset/rig/SecondaryMotion/*` (not
  policy §4's `/Asset/physics/*`), and humanoid stays per-bone
  `vrm:humanBones:<bone>` token attributes (not policy §5's
  `token[] vrm:humanBoneNames` + relationship sketch).

Schema sources and generation moved to the standalone `vrmSchema` bundle in
workspace Phase 1 (§E); `usdVrmFileFormat` now depends on schema contract
version 1 rather than compiling the types itself.

## C. Package resolution

*Goal, met: hand an import to another machine and textures still resolve, with
no temp-dir dependency (design policy §8).*

- ✅ **`ArPackageResolver` for `.vrm` containers.** Embedded PNG/JPEG textures
  are authored as `avatar.vrm[images/<hash>.<ext>]` and served directly from the
  source container, removing the OS-temp extraction dependency for normal
  imports.
- ✅ **Malformed / truncated / out-of-range byte requests rejected.**
- ✅ **Texture export bundle** + portable asset-path policy + a `package`
  command (`tools/package_vrm.py`: package-local `textures/` + relative USD
  asset paths).
- ✅ **Asset inventory** output (`package_report.json`).

Split into the standalone `usdVrmPackageResolver` bundle in workspace Phase 3
(§E), with package-path semantics unchanged.

## D. Shared container library

- ✅ **`vrmContainer`** extracted as a plain CMake shared library: GLB
  header/chunk parsing, buffer-view access, byte-range validation, immutable
  byte views.
- ✅ **Deliberately not a plugin bundle** — no `plugInfo.json`, no
  registration, no OpenUSD types in its public API, enforced by a repo check.
- ✅ Consumed by both `usdVrmFileFormat` and `usdVrmPackageResolver`, each with
  a binary link check (`dumpbin`/`nm`) proving it imports `vrmContainer` and not
  the other bundles' libraries.

## E. Workspace migration (Workspace Phases 0–4)

Contract: [`architecture/WORKSPACE.md`](../architecture/WORKSPACE.md). Every
phase gate compared authored stages against the Phase 0 baseline; a phase that
changed a baseline artifact was a regression by definition.

| Workspace Phase | Deliverable | Landed as |
| --- | --- | --- |
| 0 | Baseline snapshots + regression criteria | `tests/baseline/` |
| 1 | `vrmSchema` bundle split | `plugins/vrmSchema` |
| 2 | `vrmContainer` extraction | `libs/vrmContainer` |
| 3 | `usdVrmPackageResolver` bundle split | `plugins/usdVrmPackageResolver` |
| 4 | `usdVrmFileFormat` purification / rename | `plugins/usdVrmFileFormat` |

- ✅ **Baseline freeze** — per-fixture USDA snapshots, schema contract snapshot,
  plugin discovery results, public symbol lists, clean-install results, and the
  diagnostics code table, generated and verified by `tools/baseline_freeze.py`.
- ✅ **Dependency direction enforced** — `ost plugin test --workspace` validates
  the bundle graph declared via `requires.bundles` / `requires.libraries` with
  stable `WORKSPACE_*` issue codes before any bundle's verification.
- ✅ **`usdVrm` retired as a bundle id** — it names the aggregate product only.

Workspace Phases 5 (packaging) and 6 (`execVrm`) are **not** done; they are in
the [roadmap](../roadmap/).

## F. Reliability and release tooling

- ✅ **Standalone stage validator** — `tools/validate_vrm.py`: a validation
  contract over the imported stage (default prim, `/Asset`,
  skinned-mesh→skeleton binding, parent-before-child joint order, `JOINTS_0`
  range, material/texture/humanoid/expression/spring targets, raw↔typed source
  mapping). Runs over an already-imported stage and never re-reads the `.vrm`,
  keeping it separate from the importer (design policy §10, §12). Emits typed
  diagnostics; non-zero exit on ERROR/FATAL.
- ✅ **Error/warning code taxonomy** (Fatal/Error/Warning/Info) replacing
  free-text warnings. Stable `VRMxxx` codes, canonical severity catalog in
  `tools/vrm_diagnostics.py`, reference table in
  [`DIAGNOSTICS.md`](../../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md).
- ✅ **Compatibility report** (`tools/vrm_report.py`) — human-readable + machine
  JSON, merging coded import-time diagnostics with the validator's findings plus
  a feature-presence matrix.
- ✅ **Versioning + changelog + release-note template.** Repo-root `VERSION` is
  the single source (CMake reads it); notes render from the tagged version's
  changelog section and fail if that section is not finalized.
- ✅ **Release workflow** (`.github/workflows/release.yml`, hand-authored): tag
  `vX.Y.Z` → three OS bundles (lean + split debug symbols) from digest-pinned
  runtimes, source archive, `SHA256SUMS`, draft GitHub release. Gates on
  tag == `VERSION`, digest-reproducible packaging, packaged-artifact
  verification (`ost plugin test --from-package`), and the clean-install smoke on
  all three OS. Each bundle carries a `buildInfo.json` stamp (commit / toolchain
  / OpenUSD / build type / schema contract version).
- ✅ **Install guide + clean-environment install test.** `clean_install_smoke.py`
  packages, extracts to a fresh dir outside the repo, and asserts
  discovery/open/validate/texture-resolution against the packaged artifacts.
- ✅ **OS axis in CI** — the PR lane runs `windows-2022`, `macos-15`, and
  `ubuntu-24.04`, each build → `ost plugin test` → package on hosted runners
  against digest-pinned cy2026 runtimes.
- ✅ **Coordinate/precision unit tests** locking `util/TransformUtil`
  conversions.

## G. Test corpus foundation

- ✅ **Corpus reorganized** into `spec-samples/` (vendored) · `vroid/` (fetched,
  git-ignored) · `conformance/` · `generated/`, with a machine-readable
  `manifest.json` (provenance + SHA-256 + roles + feature tags + expected
  diagnostics) driving `test_usdvrm_corpus.py`. Selection policy in
  [`CORPUS.md`](../../plugins/usdVrmFileFormat/tests/corpus/CORPUS.md).
- ✅ **Vendored avatars:** Seed-san (VirtualCast) and VRM1 Constraint Twist
  (pixiv), both VRM 1.0 with `allowRedistribution: true`.
- ✅ **Negative corpus:** `generated/malformed/` holds nine deliberately-broken,
  license-clean `.vrm` authored by `tools/generate_negative.py`, each pinning one
  importer diagnostic via `negative-manifest.json` + `test_usdvrm_negative.py`.
  This added coded diagnostics `VRM003` (container unreadable, FATAL), `VRM111`
  (skin joint index out of range), `VRM141` (duplicate humanoid bone), `VRM151`
  (expression morph index out of range), and `VRM190` (spring collider-group
  index out of range) — the importer's prior silent sanitizations now emit a
  stable code.

- ✅ **Motion corpus** (v0.5.0): six recorded capture traces under
  [`libs/motionRuntime/tests/corpus/`](../../libs/motionRuntime/tests/corpus/README.md),
  pinning a clean session, a limb dropout, confidence collapse, irregular
  arrival, a legless rig, and a reported root velocity. Synthetic **by
  necessity** — generated by closed-form maths in `tools/generate_traces.py`,
  because a corpus recorded from a commercial capture SDK would inherit the same
  redistribution gate the VRM corpus hit and CI could not run it. Two tests keep
  it honest: `motionRuntime_corpus` re-emits each committed trace through the
  C++ writer and compares bytes, `motionRuntime_traceGen` re-runs the generator
  and compares against what is committed.

Remaining corpus axes (VRM 0.x, VRoid, `.vrma` clips with expected output, real
capture data, KTX2, multi-skin) are in the
[roadmap](../roadmap/backlog.md).

## H. Motion layer (Motion Phases A–D)

Shipped across v0.3.0–v0.5.0; the fixed contract is
[MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md).

- ✅ **Motion Phase A — the contract** (v0.3.0). `motionCore`: vendor-neutral
  `HumanoidPose` / `HumanoidAnimation` / `RootMotion` / `MotionConstraintSet` and
  source metadata, plus the hand-authored design triplet under
  `docs/design/fixtures/motion/`.
- ✅ **Motion Phase B — `.vrma` import** (v0.3.0). `usdVrmaFileFormat` authors an
  avatar-independent canonical semantic skeleton; it never resolves or binds to
  a target VRM.
- ✅ **Motion Phase C — offline retarget** (v0.4.0). `motionRuntime` +
  `vrmRetarget` + the `motion_retarget` CLI. The design triplet became
  executable, and the layer reached its first end-to-end evaluation point.
- ✅ **Motion Phase D — live capture** (v0.5.0). `IMotionSource` / `ClipSource` /
  `LiveCaptureSource`, the `motion-capture-trace` format, `ReplaySender`,
  `CaptureRecorder`, and the `motion_capture` CLI. A captured session is baked
  onto an avatar by the **unchanged** Phase C tool — the milestone's claim, kept
  falsifiable by an end-to-end test that resolves the result through a
  `UsdSkelSkeletonQuery`.
- ✅ **One humanoid taxonomy** (v0.5.0). `HumanBoneParent`,
  `NearestPresentAncestor` and `HumanBoneJointPath` moved into `motionCore`; the
  `.vrma` reader's private copy of the VRM hierarchy is gone.
- 🚧 **A CI lane was written but not wired** (v0.5.0). `ost ci generate` emits
  one job per bundle cell, so plain libraries and CLI executables have none.
  `.github/workflows/motion-ci.yml` builds the whole workspace with plain CMake;
  its bootstrap, runtime pull and dependency-graph gate work on all three OS,
  but configuring against the runtime does not, so it ships disabled and the
  layer's coverage is unchanged. Diagnosis and cost in
  [report 32](ost/32-2026-07-26-v0.20.0-motion-layer-ci-workaround.md).

Motion Phases E–H (OpenExec evaluation, generation, expression/look-at,
IK/foot-locking) are in the [roadmap](../roadmap/).

## I. VMC input adapter (v0.6.0)

- ✅ **`vrmAdapterVmc`**: a plain static library that keeps VMC Protocol at an
  input leaf. It receives OSC-over-UDP datagrams, decodes OSC and VMC messages,
  maps Unity bone names and axes into `motion::HumanBone`, assembles frames, and
  pushes canonical poses into the existing `LiveCaptureSource`; it neither
  retargets nor authors a stage.
- ✅ **Recorded-packet corpus and live socket coverage**: `vmc-packet-capture`
  v1 preserves datagrams before decoding, and generated fixtures exercise the
  OSC, message, skeleton-map, frame-assembly, bridge, and UDP layers. The
  loopback test proves the socket path produces the same motion as replaying
  the captured bytes.
- ✅ **`vmc_record` CLI**: records a bounded live session or inspects a capture,
  retaining bytes before decode and reporting transport, decoding, frame, and
  intake results together. It reports hips-offset and root movement without
  assigning semantics the adapter cannot establish.

## J. mocopi live input (v0.7.0)

- ✅ **`vrmAdapterMocopi`**: a plain static library carrying a capture
  product's own UDP grammar from socket to canonical pose — bounded receiver,
  packet-capture format, decoder, joint map and basis change, frame assembly,
  and the `LiveCaptureSource` bridge. Built **receiver-first**, because the wire
  format has no published specification and there was nothing to write a corpus
  from; the inversion is the finding rather than a shortcut.
- ✅ **Nine committed captures and a loopback corpus**: the captures decode,
  map onto canonical bones and become sampled poses with no socket anywhere, and
  `vrmAdapterMocopi_loopbackCorpus` replays all nine **through a bound socket**
  — 54 datagrams — requiring frames, poses, diagnostics and all three tallies
  to be identical to the file path's, with no clock exemption. One byte dropped
  from every datagram turns all nine red.
- ✅ **`mocopi_record` CLI**: records a bounded live session or inspects a
  capture, and `--export-trace` (from `--inspect` only, so a recording still
  runs no decoder) writes a canonical `motion-capture-trace` that
  `motion_capture` and `motion_retarget` consume unmodified onto `Seed-san.vrm`.
- ✅ **Body placement**: `BodyPlacementPolicy` (default `HipsOnly`) composes
  `RootMotion::worldPosition` / `worldOrientation` from a rig whose only
  translating joint is the hips, which is the motion contract's §5.2 record.
  Before it the live path dropped 4.81 m of hips travel the recorded path
  carried. `None` keeps the old shape reachable.
- ✅ **Device evidence, no bytes**: five sessions off a mocopi app 2.7.2 rig
  (2026-08-15) survive as `tests/corpus/recorded/manifest.json` — hashes,
  measured statistics, and the diagnostics each raised. The first decoded with
  zero diagnostics; a real restart was dark for 233 frames = 3.8833 s; tracking
  loss proved unproducible on this product, so `VRM_MOCOPI_TRACKING_LOST` stays
  frozen and unraised.

## K. Generic BVH recorded-motion ingestion (v0.7.0)

- ✅ **`motionBvh`**: a generic BVH parser with a frozen diagnostic set and a
  format-shape corpus. It carries no producer semantics and no default profile,
  pinned by boundary tests rather than by review.
- ✅ **`motionSource`**: the format-neutral model above it — source rig,
  source animation in the source's own angle order and unit, provenance, and one
  declared crossing into canonical humanoid motion — plus the producer-profile
  contract: the vocabulary a profile states by name, its invariants, and the
  typed refusals.
- ✅ **`motion_bvh_inspect` and `motion_bvh_convert`**: the first links no
  OpenUSD, because the layer under it has no value type to borrow from Gf. The
  second refuses every file until a profile is named, and its output reaches a
  target VRM through **unchanged** `motion_retarget`.
- ✅ **Two shipped producer profiles and a user-defined third**: Sony mocopi's
  mobile export and Bandai Namco Research's Motiondataset — written from two
  measured exports, because read alone either file states something about itself
  that is not true of the export. The two producers disagree about what a root
  joint is, which is why the condition was two rather than one. The third is a
  profile this repository does not ship, named by path with no search directory,
  required to match and then required to be refused against a rig it does not
  describe.
- ✅ **Cross-source comparison**: one physical session recorded as UDP *and* as
  the app's BVH export, driven to a canonical clip on each side, agreeing to a
  median 0.084° per bone with the residual shown to be timing rather than value
  ([report 01](motion/01-2026-08-15-mocopi-cross-source.md)). What each path
  cannot carry is written down there, five measured entries.

## L. Installed-package consumer lane (v0.8.0)

- ✅ **[PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md)**: the binding
  distribution contract, derived from the CMake sources — per package, the
  `find_package` name, the exported target, the header root, the packages that
  must resolve first, the platform libraries on its link line, aggregate
  membership, and whether standalone installability was **measured** or only
  reviewed. Twelve packages take a contract; three plugin bundles export no
  target and install no config by design.
- ✅ **A consumer that is not us**: `scripts/check_package_consumer.py` installs
  one package and its required packages into a scratch prefix holding nothing
  else, copies the fixture **outside** this repository, and configures, builds
  and runs it. **Twelve of twelve pass and no config file failed** — which the
  plan predicted would not happen, so the finding is that the compliance was
  already there and is now *measured* rather than reviewed. Backed by
  **forty-eight mutations** of the installed prefix: 41 caught, 5 refused before
  install because masking makes them inert on any host, 2 inconclusive because
  `liveTransport`'s one edge is conditional and unreached on Windows.
- ✅ **The lane, on every pull request, on all three OS**
  ([`package-consumer.yml`](../../.github/workflows/package-consumer.yml)):
  twelve packages × three platforms from a prefix holding no build tree, plus
  criterion 6 — every workspace target in every closure present on all three or
  on none, with `ws2_32`/`Threads::Threads` the one documented difference, in
  both directions. It copies no pin: `scripts/ci_pins.py` reads runners,
  digests, host packages and Python out of `openstrata.ci.yaml` through `ost ci
  matrix`, and `--expect 3` is a check rather than a formality.
- ✅ **It found a defect in the runtime, not in a package**: a pulled runtime's
  CMake package carries the producing machine's Python paths, in
  `pxrConfig.cmake`'s guarded variables and again in sixteen imported targets'
  `INTERFACE_INCLUDE_DIRECTORIES`, the second of which no `-D` can override
  ([ost report 37](ost/37-2026-08-30-v0.22.6-runtime-python-paths-from-the-producer.md)).
  The four packages that passed everywhere are exactly the four whose closure
  never reaches `pxr`.
- ✅ **PKG-5 closed by measurement**: on `macos-15` and `ubuntu-24.04`
  `vrmAdapterMocopi`'s consumer links `Threads::Threads` and **no** `ws2_32` —
  the absence a Windows run structurally cannot see, and the half of
  [#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113) open since
  the receiver grew a platform link.
- ✅ **`scripts/check_docs.py` refuses a `*Config.cmake.in` with no contract
  row**, and a row naming a package that does not exist — added *before* the CI
  cell, while the document it checks was five days old.

## M. Shared OSC foundation and VRChat OSC Trackers input (v0.8.0)

- ✅ **`libs/liveTransport`**: the UDP receiver, bounded queue, packet-capture
  format and diagnostic vehicle, once instead of once per adapter. 1485 lines
  left `vrmAdapterVmc` and 337 arrived; 1781 left `vrmAdapterMocopi` and 359
  arrived, and every committed capture in both corpora still round-trips byte
  for byte. Four `UdpReceiver` defects were merged into `vrmAdapterVmc` first,
  in a change that moved no file — two of them shipped untested on purpose,
  because a `-1` and an `INT_MAX` poll timeout differ only after 24.8 days and a
  `POLLERR` wake-up is not producible from a suite that owns only its sockets.
- ✅ **`libs/osc`**: packets, bundles and their flattening, addresses, type tags,
  arguments, and a refusal naming the byte and the address it refused at, with
  no address *semantics*. It moved on measured evidence: an inventory written in
  `vrmAdapterVrchatOsc` decoding real bytes through the VMC-owned decoder needed
  **five VMC tokens**, every one of them the name — exactly the three couplings
  the plan predicted and nothing else. `OscPacket`'s public behaviour was frozen
  by seven characterisation tests, checked by six mutations, before any source
  moved.
- ✅ **`vrmAdapterVrchatOsc` and `vrchat_osc_record`**, the third live adapter,
  with a frozen ten-code diagnostic set. No adapter imports a sibling, verified
  by injection in every direction.
- ✅ **The protocol was inventoried before it was decoded**: six captures,
  44 918 datagrams — eight addresses (three numbered trackers and a named
  `head`), every one `,fff`, one message per datagram, no bundles, an
  eight-datagram cycle inside a median 0.053 ms, ~58 Hz emitted and ~39 Hz
  delivered ([report 02](motion/02-2026-08-30-vrchat-osc-address-inventory.md)).
  The inventory carrying no list of expected addresses is what made `head`
  arrive as a row instead of four absences.
- ✅ **Tracking space measured against a recorded rest pose**
  ([report 03](motion/03-2026-08-30-vrchat-osc-tracking-space.md)): metres,
  +Y up, +Z forward, left-handed, so the conversion is VRM 1.0's reflection
  through X. Handedness came from a labelled take, not from the numbers. The
  Euler order is measured to three of six and the residual is quoted — median
  0.21°, 12.33° at worst, against 25.7° before the test.
- ✅ **Frame assembly is stated policy with a fixture each**: two boundary rules,
  because a repeat needs no clock and a gap catches a frame no repeat would
  close; a peer that changes is a restart, a silence of any length is a timeout,
  and a caller with no peer never sees a restart. `session-restart` and
  `silent-gap` are the same 4.8452 s gap told apart by identity alone — testable
  only because the capture format grew a per-record peer the same day.
- ✅ **Twelve generated fixtures**, one the session's own shape, five `derived`
  and six `unobserved`, each with its reason.
- ✅ **`libs/motionTracking`**: which tracker is which body region, without
  saying which bone — in neither the adapter nor `motionCore`. Assigned
  observations reach a `HumanoidPose` by direct assignment, and a position the
  solve does not consume is **reported rather than dropped**, because consuming
  one is IK.
- ✅ **A capture reaches a rig through unchanged tools**: `vrchat_osc_record
  --inspect --export-trace --assign` writes the canonical trace, and
  `vrchat_osc_record_endToEnd` drives it through unchanged `motion_capture` and
  `motion_retarget`. The claim is a partition rather than a count — four joints
  move and fourteen hold their rest pose *exactly*, four of the fourteen sitting
  between a driven hip and a driven foot, which is where a solve that had begun
  estimating would show first.
- ✅ **Cross-source evidence, three paths and two performances**
  ([report 04](motion/04-2026-08-31-cross-source-carry-drop.md)). One physical
  session is **not producible on this sender** — its transfer format is
  exclusive and it records no BVH while sending OSC — so five labelled sequences
  were performed on both dates with the two 2026-08-15 paths kept as the
  control. Eight difference categories, and the one predicted to matter produced
  the largest difference and it was a **defect**: a frame whose hips tracker
  sent a position and no rotation localised its children against identity,
  snapping head and both feet **33.6°** — the hips' own orientation to five
  figures — on 16 of 777 frames of a stand-still take. Fixed; the worst
  single-frame step is now 2.46°. Report 01's one open row closes: all three
  paths now carry the body's travel.
