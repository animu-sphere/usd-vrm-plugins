# Current

The next milestone and active carry-over work. **Shipped work is not repeated
here** — it lives in the [delivery history](../reports/delivery-history.md) and
the per-version [release records](../releases/). A milestone's own detail lives
in its track document; this file carries the boundary, the completion
conditions, and what is still open.

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## Shipped: v0.8.0 — installed-package consumer lane, shared OSC foundation and VRChat OSC Trackers input, awaiting its tag 🚧

The milestone's work is **done** and its record is
[releases/v0.8.0.md](../releases/v0.8.0.md), which is where the boundary, what
shipped, and the conditions it deliberately did not close now live. The
milestones and their evidence are [packaging-hardening.md](packaging-hardening.md)
§4 (PKG-0 through PKG-5) and [osc-and-vrchat-trackers.md](osc-and-vrchat-trackers.md)
§9 (OSC-0 through OSC-3, VRC-0 through VRC-7) — every one ✅.

One completion condition is **open**, and one was met on the second of the two
branches it was written with. The open one is a redistributable recorded
session — open on the bytes and not on the code, since the three CTest names
that read the corpus would pick one up with no change. The branched one is the
solve: this release claims tracker *input* rather than tracker-driven motion,
which is the outcome the plan wrote down in advance rather than a shortfall
found afterwards. Both are in the release record's known limitations, with the
operator evidence that would close them below.

What is left is the tag.

### Before the tag

The v0.7.0 preparation cost five wrong documents because a member count was
written from a workstation and the lane ran a different `ost`
([report 35](../reports/ost/35-2026-08-24-v0.22.2-release-artifact-membership.md) §6).
This release adds a second class of the same risk — packaging claims — so the
checks are listed rather than remembered:

- [x] the installed-package consumer lane is green on all three OS, and its
      result is what the release's packaging claims cite *(2026-08-31, run
      `33397904470` on the tree that became `main`: `consume-windows`,
      `consume-linux`, `consume-macos` and `criterion 6 — three platforms agree`,
      all five jobs green)*;
- [x] every bundle, library and adapter manifest agrees with
      [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md), and no row
      still says `unmeasured` *(`check_docs.py` green over 4 bundles and 12
      libraries — which is where the three adapters are counted — and §4 carries
      no `unmeasured` cell; §5 says so in prose as well)*;
- [x] the aggregate product's closure is measured against
      `[workspace].release_members` on the **pinned** `ost`, not the
      workstation's — check `bootstrap.ost.version` in `openstrata.ci.yaml`
      against `ost --version` before writing any count *(pin and workstation
      are both **0.22.8**, so the v0.7.0 divergence does not exist here.
      Measured rather than derived: `ost plugin package --workspace` reports
      `4 package(s) … plus 6 tool package(s)` — ten member archives — and
      `Aggregate release members: vrmSchema, usdVrmFileFormat,
      usdVrmPackageResolver, usdVrmaFileFormat, motion_bvh, motion_capture,
      motion_retarget`, exactly the seven of `release_members` with the three
      adapter CLIs subtracted. `AGGREGATE_MEMBERSHIP_MISMATCH` did not fire, and
      every archive is named `-0.8.0-`)*;
- [x] each adapter's standalone package closure is measured, including
      `vrmAdapterMocopi`'s raw `ws2_32` on a POSIX host (PKG-5) *(the lane runs
      all twelve contract rows — the three adapters among them — on all three
      OS; on `macos-15` and `ubuntu-24.04` `vrmAdapterMocopi` links
      `Threads::Threads` and **no** `ws2_32`, which is the absence Windows
      structurally cannot see)*;
- [x] `liveTransport` and `osc` artifact contents are recorded — 9 files and 7
      as of 2026-08-29, and a change in either is a change in what the excluded
      side ships *(re-counted from the artifact manifests: still **9** and
      **7**, and no header entered or left either. The bytes did change —
      `PacketCapture.h` grew a per-record peer at VRC-4 and `OscPacket.h` grew
      the `t` argument — which is a change in content and not in membership,
      and the distinction is the one this row is asking about)*;
- [x] the CHANGELOG names the **architecture** changes, not only the features:
      two shared libraries extracted, a third adapter, and every adapter's
      package config gaining a dependency it was missing *(the last three were
      there; the **entire packaging half was not** — PACKAGE_CONTRACT.md, the
      three scripts and the CI lane had landed with no `Added` entry at all,
      which is exactly the omission this row exists to catch, and it is now
      written)*;
- [x] the artifact-only BVH smoke is green on **Linux and macOS**, not only on
      the workstation that wrote it — it is a `release.yml` step, so a tag is
      the first time those two cells run it, and the `workflow_dispatch --ref`
      dry run is what turns that from a surprise into a measurement *(it did:
      `artifact-only BVH smoke: PASS` on both, in the dry run rather than at
      the tag — 853 frames at 50 Hz through 22 bound joints from the product
      alone, the negative half included)*;
- [x] `scripts/check_docs.py`, `check_motion_profiles.py` and `verify_corpus.py`
      are green, and `release.yml` is dry-run with `workflow_dispatch --ref`
      before the tag — a green PR lane proves nothing about it. **It proved
      nothing, exactly as written.** The first dry run went red on all three OS
      at `Stage the release artifacts`, in a jq expression that had never once
      executed: it consumes `.data.release_members`, which arrived in `ost`
      0.22.3 *after* v0.7.0 was tagged, and no `pull_request` event runs this
      workflow. Fixed, along with an `ost` pin two patch versions behind the CI
      contract beside correctly-mirrored runtime digests; `check_docs.py` now
      compares the lane's three behavioural pin sites. Second run green on all
      three OS, staging asserting `4 bundle(s), 3 product tool(s); release
      members: 4 + 3; product: 7`
      ([ost report 39](../reports/ost/39-2026-09-01-v0.22.8-release-lane-first-execution.md)).

### Carried out of v0.8.0

- ⬜ **Freeze the Linux and macOS symbol baselines.** `tests/baseline/symbols/`
  holds `windows-x86_64.txt` only, because until the workspace cells landed no
  lane ran the Phase 0 gate anywhere else. `--check` skips a platform with no
  committed file (it has nothing to regress against) and every other baseline
  artifact is verified on all three OS, so the gap is symbols alone. Closing it
  means running `tools/baseline_freeze.py --update` on a Linux and a macOS host
  and committing the result.
- ⬜ **There is no real-runtime compatibility lane any more.** The scheduled
  lane and its one cell, `usdvrmfileformat-support-windows-cy2026`, were removed
  on 2026-08-30: it targeted a self-hosted `usd-windows-real` runner that does
  not exist, so every weekly firing from 2026-07-27 onward was cancelled by
  GitHub after queueing, and it paired a 26.05-built plugin artifact with a
  26.08 runtime, which OpenUSD guarantees nothing about. Reinstating it needs
  all three of a real runner, a republished plugin artifact and a re-added
  `lane: scheduled` cell — not the cell alone, which is what was there
  ([report 38](../reports/ost/38-2026-08-30-v0.22.8-workspace-cell-verbs-and-orphaned-lanes.md)
  §5).
- ⬜ **Three bundle cells exist to reach a verb, not a bundle.** A
  `kind: workspace` cell takes `verify: graph|build|test`, so `ost plugin test
  --workspace` and `ost plugin package --workspace` — the two verbs `release.yml`
  runs by hand — cannot be spelled in `openstrata.ci.yaml` at all. The only
  generated construct that reaches them is the per-bundle cell, which is
  per-platform too, so pyramid and packaging coverage costs bundles × platforms
  jobs. Nine of this repository's twelve were measured redundant against the
  workspace suite and removed on 2026-08-30; the three that remain
  (`usdVrmFileFormat` on each platform) are the only lane that configures a
  bundle **standalone** and the only PR lane that runs `ost plugin package`.
  Closing this is upstream — `verify: pyramid` and `verify: package` — and would
  take the matrix to four cells
  ([report 38](../reports/ost/38-2026-08-30-v0.22.8-workspace-cell-verbs-and-orphaned-lanes.md)
  §2, the live v0.23.0 P2).
- ⬜ **`ost` cannot tell us a workspace member ran no tests.** `ost test` reports
  one flat total, so `usdVrmaFileFormat` contributing **zero** CTest targets read
  as 100% passing on every lane, on three platforms, for months — the option-name
  bug fixed on 2026-08-30. The bug is ours and is closed; what stays open is that
  nothing would report the next one. Asked upstream as per-member attribution in
  `ost test --json`
  ([report 38](../reports/ost/38-2026-08-30-v0.22.8-workspace-cell-verbs-and-orphaned-lanes.md)
  §4).
- ⚠️ **`release.yml` stays hand-authored, and hand-mirrors what the contract now
  expresses.** Its X11 step, its `ost` pin and its runtime digests are copies of
  `openstrata.ci.yaml` values; regeneration never touches them and a green PR
  lane proves nothing about it. The `ost` release contract (`release:` in the
  matrix) is the eventual fix; adopting it is not scoped yet.
- ⬜ **An adapter artifact exists now, and no lane publishes one.** `ost` 0.22.3
  composes `requires.libraries` in the per-library verb, so
  `ost library package adapters/liveCapture/mocopi` produces
  `vrmAdapterMocopi-0.7.0-<target>.tar.zst` — the library and `mocopi_record.exe`
  together, the shape [WORKSPACE.md §5](../architecture/WORKSPACE.md) has named
  since before anything could emit it. **What is left is a decision, not a
  tool**: `release.yml` stages the aggregate's members and nothing produces an
  adapter artifact in CI, so whether a release carries them is open
  ([report 36](../reports/ost/36-2026-08-25-v0.22.3-canonical-runtimes-and-release-membership.md)
  §2, §3).
- ✅ **The profiles reach the product, and the smoke that proves it found a
  defect** *(2026-08-30: `scripts/artifact_only_bvh_smoke.py`)*. Both halves of
  the staging were already closed — the plain-CMake install (2026-08-05) and the
  packaged one (2026-08-25, `ost` 0.22.3's `[[workspace.install_data]]`,
  `data_files: 3`) — and this entry stayed open on the test rather than on a
  mechanism. **The first run of that test failed.** The profiles installed
  byte-identically to `share/usd-vrm-plugins/profiles/motion/` and
  `motion_bvh_convert` refused the capture anyway: `ost plugin product install`
  lands a tool member at `<prefix>/tools/<member>/bin/`, and the locator's
  installed-prefix rule was `<exe>/../share/…`, which is the layout of a *member
  archive* unpacked on its own. The fix is one more search-path rule in
  [ProfileLocator.cpp](../../tools/motionBvh/src/ProfileLocator.cpp); the run is
  853 frames at 50 Hz through 22 bound joints, from the artifact alone, and it
  ends by moving the installed profile aside and requiring the refusal to come
  back — so "it found *a* profile" cannot pass for "it found the one this
  product ships". Wired into `release.yml` beside the clean-install smoke, on
  all three of that lane's cells — which means it inherits that lane's standing
  caveat: **no PR event runs it**, so the only host it has been measured on is a
  Windows workstation, and the first Linux and macOS runs happen at a tag
  ([report 36](../reports/ost/36-2026-08-25-v0.22.3-canonical-runtimes-and-release-membership.md) §4).

### Carried out of v0.7.0 — evidence an operator produces

None of these closes by writing code, and each is stated with what it costs.

- ⬜ **A VMC relay session, compared at the canonical layer.** Two paths of
  three were compared on 2026-08-15 (median 0.084° per bone,
  [report 01](../reports/motion/01-2026-08-15-mocopi-cross-source.md)); a relay
  makes it three. Until one is recorded, the **VMC half of the root/hips
  decision stays open** — a VMC session retargets in place, and that cost is
  stated rather than hedged.
- ⬜ **A recovery a device can actually produce, or a decision that this
  product cannot.** The source *restart* is recorded from hardware — dark for
  233 frames = 3.8833 s, the refusal count and the new session's first timestamp
  agreeing exactly. Tracking loss was dropped as a take and the reason is the
  finding: removing a sensor puts the app into re-tracking, so the stream never
  carries a lost sensor. `VRM_MOCOPI_TRACKING_LOST` stays frozen and unraised.
- ⬜ **A labelled *rolled* take, which is the cheapest item on this list.**
  VRC-3 measured the VRChat OSC Euler order down to three compositions of six
  and stopped there, because no sample in the 2026-08-30 session rotates about
  two axes at once by enough to separate them — the second-largest component of
  any orientation across 44 918 messages is 25.2°, since nobody tilted. Twenty
  seconds settles it: a head tilted onto one shoulder and held, or a foot rolled
  onto its outer edge and held, with which side written down. Until then the
  conversion carries a residual of median 0.21° and 12.33° at worst
  ([report 03](../reports/motion/03-2026-08-30-vrchat-osc-tracking-space.md) §2.3).
- ⬜ **A redistributable mocopi capture.** The five device sessions survive as
  [`recorded/manifest.json`](../../adapters/liveCapture/mocopi/tests/corpus/recorded/manifest.json)
  with hashes, every measured statistic and no bytes — a session is a real
  person's motion and a skeleton packet is a body measurement of that person.
  Getting a publishable one needs the vendor's `BVH Sender`, not a device.
- 🚧 **Both paths running from release artifacts alone, profiles included.**
  **The recorded path is done** (2026-08-30): `motion_bvh_convert` converts a
  real mocopi export from an installed product prefix with nothing from this
  source tree on any search path, and the profiles it uses are the product's own
  — see the packaging entry above. What is left is the **live** path, and it is
  left for the reason it always was rather than a new one: `mocopi_record` is
  not in the aggregate — it left by declaration when the exclusion stopped being
  a version pin — so that run composes the product with the adapter's own
  artifact, which `ost library package` can now produce, and nothing has
  performed it.

### Still Motion Phase G

Expressions now travel from a sender *and from a clip* to a canonical pose and
back out of a trace — v0.7.0 closed the clip half. Reaching a **rig** is what
#88 is actually about, and both the face and the gaze now do:

- ✅ **`ExpressionResolve`** *(2026-09-01)*. A VRM expression binds N morph
  targets across M meshes plus material colours — it is *not* one blend shape —
  so expanding a named weight onto a rig needs the avatar, which is why
  `motionCore` carries the name verbatim and the clip reader authors a name and
  a number and resolves nothing. Both halves landed the same day. The **join
  key** first: the avatar side now authors `vrm:expressionName` too, additive
  within schema contract v1, so the two sides join on the verbatim name rather
  than on prim names neither can predict from the other — and that closed a
  defect on the way, the importer's name uniquifier having counted bases, so a
  source file naming a mesh `Body`, `Body` and `Body_2` imported five meshes as
  four prims, silently, because `Define` returns an existing prim instead of
  failing.

  Then the **resolve**: `vrmRetarget`'s `ExpressionResolver` turns one sample's
  weights into the blend-shape weights and material colours they mean on one
  particular rig. Plain values like the rest of that library — the caller reads
  the binds off the stage — so `execVrm`'s `Vrm.ExpressionResolve` will be a
  wrapper over it rather than a second implementation. Four rules are the
  resolve rather than plumbing: a reported zero is authored and an unreported
  name contributes nothing (an absent name is not a zero weight, one layer up
  from where `ExpressionWeights::Find` already says so); the `[0, 1]` clamp the
  `.vrma` reader deliberately withheld lands here, named per expression;
  `isBinary` rounds on the way to the binds and not only in the scalar query;
  and a material colour is carried as `(totalWeight, weightedTarget)` with an
  `Apply(base)` lerp, so the material's own value never has to reach a library
  that will not read a stage. **The binary rounding was measured rather than
  assumed** — the first suite passed with that line deleted from the resolve
  path, because only the scalar query covered it, which is the false green the
  test now closes.
- ✅ **`motion_retarget` authors the resolved weights** *(2026-09-01)*. The
  resolve above produced values nothing wrote; the bake tool now reads the
  avatar's expression binds off the stage, resolves the clip's named weights
  against them, and authors `blendShapes` plus `blendShapeWeights` on the same
  `SkelAnimation` it already binds to the rig. **No `skel:blendShapes` or
  `skel:blendShapeTargets` is authored, and that is the answer rather than the
  gap**: UsdSkel carries the weights on the animation and hands each skinned
  prim the subset its own binding names, so authoring one would copy a binding
  the referenced avatar already owns — the same reason the rig itself is
  referenced and never copied. What that join costs is a translation this layer
  has to perform: an expression binds a blend-shape *prim*, an animation names a
  *token*, and the token is the one the mesh binding it chose — so a blend shape
  no mesh binds resolves to a weight that cannot be authored at all, and is
  reported. Three further decisions are measured by the new
  `expressive_{avatar,clip}` fixtures rather than asserted: an expression key is
  a sample of the same performance, so a blink between two body keys adds that
  instant to the bake; a weight the clip never states holds rather than falling
  to zero, which a value block is the one way to reach and the test uses; and a
  material colour is resolved and **not** authored, because a colour slot is a
  material input and that layer owns the vocabulary.
- ✅ **A clip's gaze reaches the canonical layer** *(2026-09-02)*. Look-at was
  untouched in every layer; the *reading* half now runs end to end, and the
  shape is the expression half's rather than a second design. VRMA points
  look-at at a node and the character watches where that node **is**, so what
  travels is a target **point**: `HumanoidPose::lookAtTarget`, optional because
  the origin is a place a producer can legitimately name, and `/Animation/LookAt`
  carrying `vrm:lookAtTarget` beside the `vrm:lookAtOffsetFromHeadBone` the
  source rig measured. Turning a point into a pair of eyes needs the avatar's
  own look-at configuration, so it stays `LookAtEvaluate`'s job — the same
  division `ExpressionResolve` is under.

  Three decisions are the read rather than plumbing, and each has a fixture.
  **A target is placed where the file put it**: a look-at node may be parented,
  so the ancestors' stated transforms are composed in — `gazing_head.vrma` puts
  the target under a node translated 1.5 m up, and reading the position in its
  own space would be a gaze at the floor. **A clip says one of three things**,
  and they are authored apart: a channel drives the node (time samples), the
  node states a transform nothing animates (one default), or it states none
  (**no attribute at all**, because a gaze the file never gave is not a gaze at
  the origin). And **the offset travels beside the clip, not on its samples**,
  because it is a measurement of the rig the clip was authored on; a file that
  omits it is warned about rather than quietly read as zero.

  The pose field obliged the **recorded-trace format** to grow a `lookat` line
  at version 3, and the committed corpus was regenerated. No live producer emits
  a gaze today; carrying it anyway is what stops a recorder from silently
  dropping a field and making a replay differ from the session it reproduces.
- ✅ **`LookAtEvaluate`, and a rig that looks there** *(2026-09-04)*. Both
  halves landed: `vrmRetarget`'s `LookAtEvaluator` turns a target point into one
  particular rig's answer, and `motion_retarget` authors it. Plain values like
  `ExpressionResolver`, so `execVrm`'s `Vrm.LookAtEvaluate` stays a wrapper.

  **The two rig types are not a spelling difference**, and that is the shape of
  the whole step. A `bone` rig answers with eye rotations, and the eye on the
  side the gaze goes to takes the *outer* range map while the other takes the
  *inner* one — the split exists because two eyes converge, and a resolve that
  read one map for both would look plausible on every symmetric rig. An
  `expression` rig answers with `motion::ExpressionWeights` for `lookLeft`,
  `lookRight`, `lookUp` and `lookDown`, which is deliberately the value
  `ExpressionResolve` already consumes: the gaze is folded into the sample's own
  weights *before* the expression resolve, so it reaches the avatar's binds
  through the one accumulator that already sums expressions rather than through
  a second path into the same blend shapes. One weight drives both eyes there,
  so the inner map is unreachable for that type and a rig that states a
  different one is told rather than quietly half-read.

  Four more decisions are measured rather than asserted. **A gaze starts at the
  eyes**: the origin is the head joint plus the rig's `offsetFromHeadBone`,
  rotated by the head — an offset added in world space agrees with every test
  until the character turns, which is why a turned head is one of them. **The
  two VRM spellings are one value**: 1.0's `inputMaxValue`/`outputScale` and
  0.x's `xRange`/`yRange` plus an editable Hermite curve parse into the same
  `LookAtRangeMap`, and the 0.x linear default reduces to the 1.0 map
  algebraically rather than approximately. **A gaze nobody named is not a gaze
  forward, and one the clip stops naming holds** — the eyes stay where the
  retarget put them until a first target arrives, and a sample that says nothing
  afterwards leaves the last gaze standing, which is the rule a blocked
  expression weight is already under and the one thing that keeps the two rig
  types' different authoring routes agreeing. A target sitting *on* the eye
  origin names no direction and is reported, because that one is a defect. And **the clip's own `offsetFromHeadBone` is a fallback,
  not the measurement**: it describes the rig the clip was authored on, so it is
  used only when the avatar states none and the substitution is warned about.

  Two things the tests had to earn rather than claim. The eye rotation's
  composition — yaw about +Y, then pitch about +X *negated*, since a positive
  right-handed rotation about +X takes the forward axis down — is checked by
  aiming an identity range map at a target and requiring the resulting rotation
  to point back at it, and it fails on either half being wrong; asserting the
  two angles instead would have agreed with a tool that had both conventions
  inverted. The same holds one layer up: the end-to-end fixtures give the four
  maps four *different* output scales (10 outer, 5 inner, 12 up, 6 down), so
  swapping inner for outer fails four assertions instead of none.

  What is **not** authored is `skel:blendShapes` on any mesh and any eye joint
  an expression rig does not have — the same answer the expression bake gave,
  for the same reason. `--no-look-at` bakes the body and leaves the eyes at
  rest, for a pipeline that aims them itself; `--no-expressions` does the same
  to an *expression*-driven gaze, because those four weights reach the stage as
  blend-shape weights and by no other route, and the run says so rather than
  counting a gaze it did not write. Everything the gaze displaces is named
  once: an eye bone the clip itself animates, and a gaze expression the clip
  also drives by name.

**What is still Phase G** is neither of the two resolve steps: it is **live
recording** and the **VRMA export investigation**
([the backlog](backlog.md) carries both). Every item listed above is closed, so
this section stays only until those two find a version.

## Next: the recorded-source and producer-contract tracks ⬜

**No version yet, deliberately** — it takes one when v0.8.0 is cut. Two pieces
of work that belong together because the second is what stops the first from
being answered once per input:

- ⬜ **NPZ / AMASS through the existing `motionSource` boundary.** The recorded
  half gains a second format family, and the boundary is already built for it: a
  reader is allowed format syntax and storage interpretation, and never the VRM
  target rig, the target rest pose, the retarget policy, stage authoring, an
  OpenExec graph, or a vendor runtime. Whether that is one identity
  (`motionNpz`) or two (`motionNpz` + `motionAmass`) is settled by **measuring a
  few files of the real corpus first** — an AMASS-shaped contract that a
  format-neutral reader cannot absorb is the only thing that justifies the
  second identity, and deciding before the measurement is how a boundary ends up
  shaped like whichever file arrived first.
  [The recorded track](recorded-motion-sources.md) §13.
- ⬜ **The canonical producer contract, frozen before the inputs multiply.**
  Four categories now produce motion — recorded source, live pose source,
  tracker source, generated source — and each was designed on its own. What is
  unified is the **canonical value boundary**, not an I/O API:
  `SourceAnimation → HumanoidAnimation` for recorded, `timestamp +
  HumanoidPose` for live, `timestamp + TrackerFrame` for trackers, and
  `request/context → HumanoidAnimation or a pose stream` for generators. A
  generation product reaches the workspace behind a vendor-neutral
  `IMotionGenerator`, never as a fifth shape.
  [The backlog](backlog.md#canonical-motion-producer-contract) carries it.

Both are producer-side, and both are in front of OpenExec on purpose: a compute
layer over contracts that are still moving buys a second implementation of a
boundary rather than a second evaluation of one.

## After those: the OpenExec foundation (Workspace Phase 8 + Motion Phase E) ⬜

**Release boundary:** `execMotion` and `execVrm` bundles exist and evaluate a
humanoid through OpenExec, proven equal to the offline result on the same input.
Nodes are thin wrappers over `motionRuntime` and `vrmRetarget`, never a second
implementation, and each evaluates an immutable snapshot rather than a live
source. Planned in [openexec-foundation.md](openexec-foundation.md).

**Why it has moved twice, and why it now carries no version.** Parity is worth
what its input is worth. Scoping it as v0.6.0 would have proved that two
implementations agree about *generated* data; ordering the adapter releases
first made v0.7.0's recorded sessions the parity input. The 2026-08-29 re-order
applied the same argument twice more — the input is now also a package closure
no external consumer has ever resolved and a producer contract that four input
categories each answered separately — so the order is packaging → tracker path →
recorded corpus → producer contracts → OpenExec. **Nothing in the plan is
withdrawn**: the re-order changes when it starts, not what it is.

Not in this boundary: realtime skinned display, any `ExecIr` dependency, and
network I/O inside a computation, which is a permanent non-goal rather than a
deferral. Skinned display is bounded upstream — OpenUSD 26.08 resolves exec prim
adapters from a hard-coded list — so P0-7 ships an exec-computed
`UsdGeomXformable` instead and realtime skinned display becomes its own
milestone after the `ExecIr` track ([the plan](openexec-foundation.md) P0-7).

**Prerequisites already met.** One OpenUSD across three OS (all three 26.08
runtimes published and pinned, OpenExec included and built `--examples`); the
motion layer has CI (`ost` 0.21.0's `kind: workspace` cells, so what remains is
coverage rather than lane shape); and the `motionCore` aggregates can be
compared, which was P0-4's stated blocker.

### Still open

- ⬜ **Amend the OpenExec capability probe** with what the audit found: `esf`,
  `esfUsd` and `ef` go unprobed although the public exec headers require them,
  and `usdExecImaging` proves nothing because it is built whether or not
  `PXR_BUILD_EXEC` is on. The refusal is still correct today — the other five
  components are absent in that configuration — so this is precision, not a hole.
- ⬜ **`execMotion`, `execVrm`, parity, and the display slice** — P0-4 through
  P0-7 of the [plan](openexec-foundation.md#6-foundation-tasks). Mechanism before
  behavior: the first spike registers no real computation, so a failure is
  attributable.

## Standing: corpus policy — recorded evidence is not the generated corpus

The generated corpora stay. Real-session evidence goes beside them, never mixed
in, and the same shape serves both halves of the release:

```text
<adapter or library>/tests/corpus/
├─ generated/     protocol or format shapes, committed, CI-runnable, no hardware
└─ recorded/
   ├─ manifest.json      every recorded file, with or without its bytes
   └─ redistributable/   real sessions and files cleared for publication
```

Whether a file's bytes are committed is a **field**, not a location: a row that
changed directory when its redistribution status changed would break every
reference to it for a reason that has nothing to do with the file.

A capture or a file that cannot be redistributed leaves **no bytes** in the
repository. It leaves a manifest: hash, recording or exporting tool version,
sender or producer identity and version, device or relay identity, the measured
statistics, the expected diagnostics, expected frame and pose counts, the
validation date, and the redistribution status. A BVH manifest additionally
carries the profile id, frame time, joint and channel counts, coordinate
convention, unit, root policy, and the bones it is expected to map. That is
enough for a later reader to tell whether a claim still holds without the bytes,
and it is the same convention the VRM corpus already uses for models it cannot
ship.

Public CI runs the redistributable half. Hardware validation is an **opt-in
lane** that never gates a pull request — its output is a capture and a manifest,
not a green tick. A device is needed once per behavior, not once per run.

## Standing: product tracks with work still open

Only the open items are listed. What each track has already delivered is in the
[delivery history](../reports/delivery-history.md) and the
[release records](../releases/).

### Product P0 — documentation & implementation sync 🚧

*Goal: no contradiction between the docs and the code; a new user understands
the workspace layout, the output structure, and the import/runtime boundary.*
(design policy §15, §17-P0)

- 🚧 Describe `vrmSchema`, `usdVrmFileFormat`, `usdVrmPackageResolver`, and
  `usdVrmaFileFormat` as separate bundles; `vrmContainer`, `motionCore`,
  `motionRuntime`, `vrmRetarget`, `motionSource`, `motionBvh`, `liveTransport`,
  `osc` and the three `vrmAdapter*` leaves as plain libraries; `motion_retarget`,
  `motion_capture`, `motion_bvh_convert` and the `*_record` tools as CLIs; and
  `usdVrm` as the aggregate product name only.
- 🚧 Unify phase notation to **Product P0–P6**, **Workspace Phase 0–8**, and
  **Motion Phase A–H** — three sequences, never a bare "Phase N".
- 🚧 Align build / test / install examples with what CI actually runs.
- 🚧 Adopt the house documentation taxonomy shared with `open-strata` and
  `hydra-merlin`.
- ⬜ **Finish separating the workspace contract from its history.** The
  near-term plan of 2026-08-29 proposed splitting
  [WORKSPACE.md](../architecture/WORKSPACE.md) three ways — a slim contract, a
  `DEPENDENCY_RULES.md`, and a `PACKAGE_CONTRACT.md` — plus a `docs/decisions/`
  directory of ADRs. **Only the package half was taken** (2026-08-29): §5 now
  defers the consumer contract to
  [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md), and no claim in
  WORKSPACE.md moved. The other two are deliberately open, for one reason each.
  The dependency split is a change to a document five others cite by section
  number, so it is worth doing once, with the citations updated in the same PR.
  And `docs/decisions/` would be **this repository leaving the taxonomy it
  shares with `open-strata` and `hydra-merlin`** — a decision for all three
  repositories rather than for this one. Until then, rationale keeps landing
  where it does now: measurements in [reports/](../reports/), plans in this
  directory, and per-release records in [releases/](../releases/).

Done when: the component table matches the manifests, no document describes
`usdVrm` as a bundle id, every local link resolves, and a consistency check
guards all of it in CI.

### Product P1 — release stabilization 🚧

- ⛔ **A second OpenUSD version cell** (min vs latest) in the compatibility
  matrix. Today CI runs cy2026 / OpenUSD 26.08 only. **Blocked externally:**
  GHCR has no published min-version (e.g. OpenUSD 25.05 / cy2025) runtime
  artifact yet — this needs an open-strata runtime build + publish per OS, then
  a fourth cell in `openstrata.ci.yaml`. The OS axis already runs three cells.

### Product P3 — runtime verification 🚧

*Goal: builds and opens are continuously verified on all three OS; textured real
models resolve; schema registration succeeds.* (design policy §14, §17-P3)

The OS axis and the workspace graph gate are shipped. Remaining:

- ⬜ Explicit **UTF-8 / Unicode path** and **DLL dependency discovery** coverage
  on the Windows cell.
- ⬜ **Real VRM smoke test** (open + texture resolve) exercised in CI, not just
  fixtures.
- ⬜ **Verify the non-`ost` install path on Windows** *(carried from v0.2.0 /
  v0.3.0)*. The published bundles are only exercised through `ost`; a user
  composing them by hand against a plain OpenUSD environment is uncovered.
  `libUsdVrmFileFormat` links against `libvrmSchema` and `vrmContainer`, which
  are staged under `runtime/libraries/{lib,bin}` rather than beside the plugin —
  and Python 3.8+ dropped `PATH` from the DLL search for dynamically loaded
  modules, so the correct mechanism (`PATH` / `os.add_dll_directory` /
  co-location) is **unestablished**. [INSTALL.md](../guides/INSTALL.md) names the
  directories and the failure signature but deliberately prescribes no recipe.
  Closing this needs a non-`ost` install lane, not a docs edit.

## Workspace Phase 5 — per-bundle + aggregate packaging 🚧

**Status:** aggregate product shipped; the standalone dependency-registration P0
is blocked on `ost` · **Contract:**
[WORKSPACE.md](../architecture/WORKSPACE.md) §5

- ⛔ **A dependency bundle's USD registration half is never staged.** `ost`
  stages `libvrmSchema` + its CMake package into `runtime/libraries/`, but not
  `plugInfo.json` or `generatedSchema.usda` — so a packaged importer links
  against schemas it can no longer register, and a bare per-bundle
  `--from-package` fails at L3/L4. This is the **P0 upstream ask**, and it is
  why the release must ship all three VRM bundles. `--from-package --workspace`
  *does* compose and is green, but it works by putting the dependency's separate
  package on the path rather than by making any one package self-closed
  ([report 25](../reports/ost/25-2026-07-18-v0.18.0-from-package-workspace-correction.md)
  measures both).
- ⬜ **Retire the hand-rolled closure in `scripts/clean_install_smoke.py`.**
  It remains the release lane's packaged-artifact gate — it extracts outside the
  repo and drives textured avatars end to end, where the composed `ost` verb
  covers `minimal.vrm` per bundle. Needs the P0 above; the composed verb narrows
  but does not remove the need.
