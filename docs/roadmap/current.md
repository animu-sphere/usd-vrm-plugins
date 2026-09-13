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

- ✅ **The avatar's own arbitration, so two expressions stop both owning the
  eyelid** *(2026-09-04, closes #170)*. Both resolve steps above expand what a
  clip says onto a rig; this is the rig answering back. Expressions accumulate
  on the targets they bind, and two that bind *different* targets still fight
  when those targets displace the same vertices — measured on `AliciaSolid.vrm`,
  a `blink` inside a full-weight `happy` displaces 154 shared points, 146 of
  them the same way, and drives the lid **1.96x** past either expression alone.
  Nothing in the weights is out of range, because the collision is geometric,
  which is why no diagnostic and no clamp could have found it.

  VRM 1.0's per-expression `overrideBlink` / `overrideLookAt` / `overrideMouth`
  are the one mechanism the specification gives for it, and none of the three
  tokens existed anywhere in this repository — they survived as unread text
  inside `vrm:rawExtension`, on two corpus assets that state the rule. All three
  layers landed together: `VrmExpressionAPI` carries the tokens (additive within
  contract v1), the importer authors them where the file states one and reports
  a token outside the vocabulary as the new `VRM153`, and `ExpressionResolver`
  performs the arbitration — which is where it belongs, since an override is a
  statement one expression makes about *others* and so exists only for a whole
  sample, while the importer has a file.

  Four decisions are the resolve rather than plumbing, each measured. A
  **category is a set of preset names**, never one expression, and a custom name
  is in none of them. **The strongest override wins and they do not stack**: two
  blends at 0.5 and 0.8 leave 0.2 of the blink and not 0.1, a face neither asked
  for. **An expression the sample resolves to zero overrides nothing**, so the
  rate is read off the resolved weight and not the reported one — otherwise a
  binary expression reported at 0.4 blocks a blink while contributing nothing to
  the face itself. The arbitration is **one pass**, so an expression *another*
  override suppressed still overrides its own category: cascading would make the
  answer depend on the order the categories are settled in, and a pair that
  override each other's categories would have none at all.
  And **a binary expression is rounded again after a partial suppression**,
  because `isBinary` says the rig has no half-shut eyelid to land on. A
  suppression is named with the expression that caused it and is deliberately
  not a defect: the avatar asked for it.

**What is still Phase G** is neither of the two resolve steps: it is **live
recording** and the **VRMA export investigation**
([the backlog](backlog.md) carries both). Every item listed above is closed, so
this section stays only until those two find a version.

## Next: the OpenExec foundation (Workspace Phase 8 + Motion Phase E) 🚧

**No version yet, deliberately** — it takes one when v0.8.0 is cut.

**Release boundary:** `execMotion` and `execVrm` bundles exist and evaluate a
humanoid through OpenExec, proven equal to the offline result on the same input.
Nodes are thin wrappers over `motionRuntime` and `vrmRetarget`, never a second
implementation, and each evaluates an immutable snapshot rather than a live
source. Planned in [openexec-foundation.md](openexec-foundation.md).

**Why it has moved three times, and why it now leads.** Parity is worth what its
input is worth. Scoping it as v0.6.0 would have proved that two implementations
agree about *generated* data; ordering the adapter releases first made v0.7.0's
recorded sessions the parity input; the 2026-08-29 re-order put a package
closure and a producer contract in front of it as well.

**2026-09-06 inverts that last pair, and the argument is the packaging track's
one layer out.** Every node here is specified as a *thin wrapper* over a library
call, so the foundation is the first consumer of `motionRuntime` and
`vrmRetarget` that is not the tool that grew up beside them — and a boundary
nothing outside has consumed is a boundary nobody has measured. A node that
cannot be written as a wrapper is a finding about the library API, produced by
an implementation rather than predicted by a review, and
[the boundary track](boundary-consolidation.md) is scheduled immediately
afterwards to act on those findings.

**What the inversion costs, stated rather than hedged.** NPZ/AMASS no longer
precedes this, so no new source shape informs Motion Phase E's node set and the
parity input stays what v0.7.0 recorded. The canonical producer contract is now
written *after* `execVrm` exists, so a fifth crossing the exec layer needs will
be discovered as rework rather than avoided. Both were weighed against the
finding an implemented consumer produces, and **nothing in the plan is
withdrawn**: the re-order changes when each track starts, not what it is.

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

- ✅ **The OpenExec capability probe carries what the audit found**
  *(2026-09-06)*. Nine components rather than six: `ef`, `esf` and `esfUsd`
  added, because the public exec headers require them and a runtime without them
  fails at compile time inside a bundle rather than at configure time; and
  `usdIrImaging` in `usdExecImaging`'s place, because only the first is gated on
  `PXR_BUILD_EXEC`. The refusal was already correct — the other five components
  are absent in that configuration — so this was precision rather than a hole,
  and P0-1 is closed with it. Eleven `workspace_openusd_contract` cases are
  where the precision is checked, including the two that say what the contract
  does *not* require.
- ✅ **`execMotion` exists, and the mechanism is proven** *(2026-09-06,
  P0-4 step 1)*. `plugins/execMotion` registers
  `motion::HumanoidPose` as an execution value type and one `motion.identityPose`
  computation on `UsdSkelAnimation`, and `execMotion_mechanism` runs the whole of
  request-compile, compute, unchanged recompute, authored-value invalidation,
  time change and explicit invalidation against the built bundle — reaching it
  only through `plugInfo.json`, so hiding that file turns the test red the way a
  packaged bundle would. Mechanism before behaviour, as written: there is no
  algorithm in the bundle, so a wrong answer can only be a wrong mechanism.

  **Running it found four things reading could not**, and three of them change
  tasks that had not started
  ([the mechanism report](../reports/openusd/26.08-openexec-mechanism.md)). The
  load-bearing one: **an OpenExec schema has exactly one declarer per session**,
  so `execVrm` may not name `UsdSkelAnimation` — the migration audit told it to —
  and the two bundles now partition the schemas in
  [WORKSPACE.md §2](../architecture/WORKSPACE.md). A collision would have cost
  `execVrm` every computation it registered there, reported as a coding error at
  load and a missing computation much later. The one that changed this bundle:
  **a computation cannot learn the stage's `timeCodesPerSecond`**, so it cannot
  convert the frame it is handed into the second canonical motion is expressed
  in — this pose carries no timestamp at all rather than a guessed one, and
  `motion.sampleAnimation` has to be *given* a rate.
- ✅ **`motion.sampleAnimation`, and the rate a clip has to state**
  *(2026-09-06, P0-4)*. The first computation with an algorithm behind it: it
  reads the clip's `joints`, `rotations`, `translations` and
  `motion:timeCodesPerSecond` plus the builtin `computeTime`, and returns the
  pose the clip states at the frame the system is evaluating, stamped in
  seconds.

  **The rate enters the graph rather than being applied on the way out**, which
  is the signature question step 1 left open, and the reason is the *next* node.
  `motion.filterPose` wraps `motion::PoseFilter`, whose cutoff is frame-rate
  independent precisely because it derives each step's weight from the time
  elapsed between poses — so a pose stamped by the caller after it leaves exec
  reaches that filter as time zero. A clip that states no rate is therefore
  **refused rather than stamped**: an error and no value at all, because
  `timestamp` has no absent state and a guessed second is indistinguishable
  downstream from a measured one. The attribute duplicates the stage's own
  `timeCodesPerSecond` and is a shim for a gap in 26.08, not a format; nothing
  in the repository authors it yet, and the producer half is a contract ask
  ([the plan](openexec-foundation.md) §9).

  **Running it measured four more things**
  ([the sampling report](../reports/openusd/26.08-openexec-sampling.md)), and
  two of them are rules for every node still to come. `.Required()` **does not
  refuse a missing attribute** — the request compiles, `IsValid()` is true, and
  the callback runs with an input that has no value, which is the mechanism
  report's metadata finding on a second kind of input and makes it general: a
  node that needs something must refuse for itself, because nothing upstream
  will. And **between two keys the answer is USD's** — an exec input arrives
  already resolved at the evaluated frame, and 26.08 slerps a `quatf[]` — so
  this node is **not** a wrapper over `motion::SampleAnimation`, P0-6 has two
  samplers to compare rather than one implementation to check, and it has to
  compare them at the clip's own key times. Also measured: **a keyed attribute
  and the builtin `computeTime` are each enough alone to make a value key
  time-dependent** — the node is reported over a clip with no time sample
  anywhere, and, in a throwaway build with `computeTime` deleted, still reported
  over a keyed one — so a node that declares `computeTime` is recomputed on
  every frame change even when nothing it reads has moved, which makes it
  something the later nodes declare because they use it rather than out of
  habit; `motion.identityPose`, whose one input is `uniform`, goes on being
  reported to nothing. And the default time code — what a system evaluates at
  until `ChangeTime`, and again after an `InvalidateAll()` — resolves a clip
  that authors only time samples to **nothing**, so a caller that forgets the
  frame gets an empty pose rather than the first one.

  **This is the plan's first "not a wrapper" finding, and producing one is why
  the re-order put this track first.** Reading a `UsdSkelAnimation` into a
  canonical pose does exist here — in `tools/motionRetarget`'s `StageIo.cpp`, in
  a *tool*, where a bundle cannot call it — so the bundle keeps its own seam over
  plain values and the duplication is recorded rather than hidden, for
  [boundary consolidation](boundary-consolidation.md) to act on.
- ✅ **`motion.filterPose`, and where a recurrence lives when the graph has no
  history** *(2026-09-06, P0-4)*. One `motion::PoseFilter` step, the first node
  that wraps `motionRuntime`, and the first that needs a value from a frame it is
  not being evaluated at.

  **The state is passed in rather than kept**, and that is forced rather than
  preferred: a static in the callback is the mutable state exec's cache-safety
  contract and the motion policy both forbid, an authored "previous pose"
  attribute would put a derived value into the scene, and there is no input that
  can be evaluated at another time. So `motion.priorPose` is a computation whose
  ordinary value is the clip's own pose at the evaluated frame and whose purpose
  is to be replaced through `ExecUsdSystem::ComputeWithOverrides` — **exec does
  the step, the driver owns the sequence**, which is where the state already sits
  for a live source in `motionRuntime`'s pose buffer. Un-overridden the node *is*
  `motion.sampleAnimation`, with nothing special-casing it: zero elapsed time is
  a reseed and a pass-through in the library. A clip may state
  `motion:filter:cutoffHz`, `motion:filter:rootPosition` and
  `motion:filter:rootOrientation`, and an absent one keeps
  `PoseFilter::Options`' **own** default — the opposite of the rate's treatment
  one bullet up, because what an absent value costs differs: a missing rate is a
  second no consumer can tell from a measured one, a missing cutoff is the
  behaviour every other caller of the library already gets.

  **Four measurements**
  ([the filtering report](../reports/openusd/26.08-openexec-filtering.md)), three
  of which change what the remaining nodes may assume. **A computation reads
  another computation** on the same prim, the registered aggregate crossing that
  link unchanged — so the plan's chain is links rather than one node, and
  `Computation<T>()` is not a connection, which leaves 26.08's one-connection
  fallback to `blendPoses`. **Time dependence propagates across the link**: this
  node declares neither `computeTime` nor a keyed attribute and is still reported
  when the frame moves, which makes "declare `computeTime` only if you use it"
  free to follow rather than something every downstream node has to undo. **A
  request is armed by its first `Compute`** — a `ChangeTime` before one reaches
  no callback at all, which cost a red run and is the shape a naive event-driven
  driver would take. And an **override** reaches every dependent of the key it
  names, is visible as that key's own value, leaks into no sibling, and does not
  survive the call.

  **The second boundary finding, smaller than the sampler's and the same kind:**
  `motion::PoseFilter` has no stateless one-step entry point, so the bundle
  composes one from two `Apply` calls. The algorithm stays in the library — the
  node is a wrapper — but the idiom is a workaround for a missing signature, and
  it has a **measured cost P0-6 inherits**: a pose is not the whole of a filter's
  state, so a bone returning after a missing frame is passed through here (45.0°)
  where the streaming filter smooths it (23.8°). Nothing for a clip, whose
  `joints` are `uniform`; real for a live source. So
  `Step(prior, pose, options)` — **returning the state beside the result** — is
  the ask
  ([boundary consolidation](boundary-consolidation.md) §1). A **driver contract**
  joins the open list with it: compute once to arm a request, step the recurrence
  through overrides, neither discoverable from the computations themselves, and
  P0-6's parity harness is the first client that needs it written down.
- ✅ **`motion.extractRootMotion`, and the first answer that is not a pose**
  *(2026-09-06, P0-4)*. Where the body is at the evaluated frame, as a
  `motion::RootMotion`, under the intake policy the clip states in
  `motion:root:intake` — `motion::RootMotionIntake`'s own three, reached as a
  token. `ignore` **clears** the root rather than zeroing it, which is what the
  presence flags are for: a cleared root leaves a rig its own placement, a zeroed
  position puts the body at the origin. It reads `motion.sampleAnimation` rather
  than the filtered pose because that is the **library's** ordering —
  `LiveCaptureSource` conditions the root of the frame as it arrived and smooths
  afterwards.

  **Four measurements**
  ([the root-motion report](../reports/openusd/26.08-openexec-root-motion.md)),
  three of which change what the remaining nodes may assume. **A bundle
  registers more than one value type, and a computation may answer in a type
  other than the one it reads** — one request hands back a pose and a root
  motion side by side. **Time dependence follows the link and not the type**, so
  the change of result type costs nothing. **One override drives every node that
  depends on the key it names**: a single `motion.priorPose` substitution steps
  the filter *and* derives the velocity in one `ComputeWithOverrides`, which
  shortens the driver contract — one previous answer per prim, not one per node.
  And **an input the callback reads but `.Inputs()` does not declare is silent**:
  the bundle compiles, the request is valid, the callback runs, and the pointer
  is null. That is the third shape of 26.08's one property — a callback cannot
  tell *absent* from *not asked for* — and it makes a node's own refusal the only
  refusal there is.

  **A refusal sets no value at all, in every node here.** Review found this
  node's refusal returning a cleared `motion::RootMotion` — `ignore`'s own
  answer, bit for bit — so a misspelled `passthrough` got a deliberate
  `ignore`'s behaviour for anyone not reading `TfError`s. The fix is 26.08's
  documented channel: a void-returning callback and
  `VdfContext::SetEmptyOutput`, which reaches the caller as an empty value —
  the one shape no computation here produces as an answer. The earlier nodes
  moved to it, and **a refusal propagates**: a dependent handed no value refuses
  in turn, rather than filtering a pose nobody sampled
  ([the root-motion report](../reports/openusd/26.08-openexec-root-motion.md) §6).

  **The absent-input rule is now general**, because this attribute answers it
  both ways: defaulted when **absent**, refused when **stated and
  unrecognized**. Default where an absent value selects the library's documented
  behaviour; refuse where it would produce a number no consumer can tell from a
  measured one; never default a value the clip stated that this layer cannot
  honour, or a misspelled `ignore` gets the root motion it asked not to have.

  **The third boundary finding, and the first where the wrapper idiom was tried
  and ruled out rather than skipped.** The rule lives in
  `LiveCaptureSource::_Condition`, private to a capture *session*, and composing
  it the way `motion.filterPose` composes `PoseFilter` gives the **wrong answer
  in the ordinary case**: two poses at the same instant are a reseed for
  `PoseFilter` and a *refusal* for `Push`, and an un-overridden node evaluates
  exactly that case. So the derivation is three lines in the seam, asserted
  against its definition rather than against the library, and
  `ConditionRootMotion(prior, pose, intake)` is the ask
  ([boundary consolidation](boundary-consolidation.md) §1).
- ✅ **`motion.interpolatePose`, and the first input a driver hands in**
  *(2026-09-12, P0-4)*. The pose a **snapshot** states at the evaluated instant —
  a timestamped history entering as an override on a new key,
  `motion.poseHistory`, whose ordinary value is the clip's own pose as a history
  of one, so un-overridden the node is the sampler. It is
  `motion::ClipSource::Sample`, one library call and nothing else, and it answers
  the library's `motion::PoseSampleResult` **whole**: a hold is stamped at the
  requested instant exactly as a sample is, so the status is the only field that
  tells a stopped source from a live one. `motionRuntime` gained the exact
  `operator==` that registering the type needed.

  **Four measurements**
  ([the interpolation report](../reports/openusd/26.08-openexec-interpolation.md)).
  An override of a key whose type is a **whole history** reaches its dependent
  like a pose-typed one. **Two overrides of two keys in one call** each reach
  only their own dependents — the graph's previous answer fed back, the source's
  history handed in — so a driver holds one of each per prim. **A wrongly typed
  override is dropped, not refused**: a coding error naming the key, and the key's
  *ordinary* value computed in its place, so every dependent answers plausibly —
  and an empty `VtValue` takes the same path, so a driver cannot push an absence
  into a key. And **the first result type with an absent state of its own**: an
  empty history is the library's `Unavailable`, an answer rather than a refusal,
  and the node refuses only a history whose timestamps are not finite or
  decrease, and the **default time code** — no instant, and the time code every
  request is armed at, where a history sampled at a guessed 0.0 would answer a
  believable `Held`.

  **The fourth boundary finding, and the first where the wrapper works and the
  finding is its cost**: the status-carrying answer exists only on a source
  object that owns its animation, so every evaluation copies the history, and the
  free `motion::SampleAnimation` beneath it drops the status.
  `SampleClip(animation, t) -> PoseSampleResult` is the ask
  ([boundary consolidation](boundary-consolidation.md) §1).
- ✅ **`motion.blendPoses`, and the first node that reads several prims**
  *(2026-09-13, P0-4)*. A blend is a `UsdSkelAnimation` stating
  `motion:blend:sources`, a relationship to the clips, and
  `motion:blend:weights`, one per target. The node is `motion::BlendPoses` over
  each target's `motion.sampleAnimation`, weighted by position: one library call,
  and the last of P0-4's five nodes.

  **Four measurements**
  ([the blending report](../reports/openusd/26.08-openexec-blending.md)). A
  **relationship fan-in arrives in authored target order**: at first compile,
  after the targets are reordered on a live system, and in a fresh one. That
  corrects the plan's line that relationship fan-in has no deterministic order;
  that property is `IncomingConnections`'s. **Two kinds of source vanish from a
  fan-in without a word**: a target that is not a clip is skipped at compile, and
  a source that refused is skipped by the read iterator. With the node's check
  disabled, a blend whose second clip refused answered the first clip exactly.
  So the node counts its targets a second time through the builtin `computePath`
  and refuses a mismatch. **Invalidation crosses the relationship**, including an
  edit of its targets, with no request rebuilt. And **an override on one prim
  reaches a dependent on another**, which is how a driver's pose enters a blend
  and adds a driver-contract line: stamp it at the sources' instant.

  **The sources must share one instant**, compared exactly. Two clips at two
  rates are at two seconds on one frame, and the library would stamp the blend
  between them (0.625 s between 1.0 s and 0.5 s, measured). Absent weights are
  refused rather than blended evenly, because a callback cannot tell an absent
  array from an empty one.

  **The fifth boundary finding is the library's answer, not the call's cost.**
  Over nothing weighted `motion::BlendPoses` answers a pose stamped 0.0. It
  carries a NaN weight into NaN rotations, it interpolates its sources'
  timestamps as though they were samples in time, and its fold depends on order
  (4.247° between three sources and their reverse) without saying so. The ask is
  a blend that can say *nothing to blend* and states its preconditions
  ([boundary consolidation](boundary-consolidation.md) §1).
- ✅ **`execVrm` exists: the target rig and the humanoid map** *(2026-09-13,
  P0-5)*. `plugins/execVrm`, with `vrm.computeTargetSkeleton` on
  `UsdSkelSkeleton` and `vrm.computeHumanoidMap` on the applied
  `VrmHumanoidAPI` — `vrmRetarget`'s `TargetSkeleton` and `HumanoidMap`, which
  gained the exact `operator==` the registry requires. It joins the aggregate
  product, links nothing of `vrmSchema`, and requires it as a bundle.

  **Six measurements**
  ([the humanoid report](../reports/openusd/26.08-openexec-humanoid.md)), and the
  first reaches back into P0-4. **An attribute a schema defines and the stage
  gives no value reaches a callback as one element of the type's fallback**,
  beside an executor warning, not as nothing — so an unauthored `joints` is one
  joint named `""` and is refused, an unbound bone reads as the empty token, a
  one-joint skeleton with no rest pose is answered as identity, and a one-joint
  clip that keys nothing comes out of `motion.sampleAnimation` with a root at the
  origin nobody stated. **A computation on this workspace's own applied schema
  resolves on a prim `execGeom` types**, and not on one carrying the attributes
  without the schema. **The schema bundle is a runtime edge** exec resolves by
  type name. **Fifty-five inputs declared in a loop work**, against the schema's
  own 55 names. And **invalidation follows the dependency, not the value**.

  **The sixth boundary finding**, and P0-6's first table: a `TargetSkeleton` from
  rest transforms exists only in the tool, and five stage statements that
  `motion_retarget` tolerates this bundle refuses or cannot see — none of them
  reachable from what the importer authors.
- ✅ **`execVrm` carries a clip's rest onto the rig** *(2026-09-13, P0-5)*.
  `vrm.computeRestPoseCorrection` on the applied `VrmHumanoidAPI` is
  `vrmRetarget::ComputeRestPoseCorrection` over the humanoid's map, the target
  rig, and the skeleton a clip was authored against. That skeleton is reached
  across `vrm:retarget:sourceSkeleton`, a relationship no schema defines and
  nothing authors yet. Both rigs are read through `vrm.computeTargetSkeleton`.

  **Five measurements**
  ([the correction report](../reports/openusd/26.08-openexec-rest-correction.md)).
  The node equals the library's answer, bit for bit. One computation serves both
  rigs through two relationships. **A path to nothing arrives exactly as no
  relationship does**, so an absent source is refused rather than given the
  library's identity rest. Invalidation follows the dependency, not the value.
  The source is read by name and the target never is. **The seventh boundary
  finding**: the clip's rest is read off its skeleton only in the tool. And a
  question for the next node: `PoseRetargeter` computes its own correction and
  accepts none.
- ✅ **`execVrm` retargets one sample of a clip, across both bundles**
  *(2026-09-13, P0-5)*. `vrm.humanoidRetarget` on the applied `VrmHumanoidAPI`
  is `vrmRetarget::PoseRetargeter` over the rig, the map, the clip's rest and
  four `vrm:retarget:*` root-motion statements, which are `motion_retarget`'s
  flags word for word. Its pose is `execMotion`'s `motion.sampleAnimation` on
  the animation the clip's skeleton binds, forwarded by a fifth node,
  `vrm.computeBoundPose`, because an exec input makes one relationship hop and
  this needs two.

  **Six measurements**
  ([the retarget report](../reports/openusd/26.08-openexec-retarget.md)). The
  node is one library call, bit for bit. **A value crosses bundles unchanged,
  and exec says nothing when the other bundle is missing**, so `requires.bundles`
  names `execMotion` and only the bound pose's count notices its absence. **A
  bundle that reads a value type registers it too**: without that, a session
  that did not load `execMotion` first lost every `execVrm` computation, and the
  suite that loads both stayed green. **The fallback follows existence, not
  schema**: a declared, valueless `translationScale` is 0, pinned. **The default
  time code is refused**, because retargeting the sampler's empty pose there
  gives the rig's whole rest. And invalidation reaches the retarget from the
  frame, a key, the binding, the source and a statement.

  **The eighth boundary finding**: `PoseRetargeter` computes its correction in
  its constructor and takes none, so the node recomputes every frame what
  `vrm.computeRestPoseCorrection` caches, 17.7 µs of 21.2 µs on a full humanoid.
- ⬜ **The rest of `execVrm`, parity, and the display slice**: P0-5's
  `vrm.computeJointLocalTransforms`, then P0-6 and P0-7 of the
  [plan](openexec-foundation.md#6-foundation-tasks). What remains of P0-4 itself
  is the producer half of the rate and policies, the written driver contract,
  and packaged discovery — and now a decision on what a one-joint clip's
  fallback-filled root means.

## Then: boundary consolidation ⬜

**Boundary:** the agreements nine identities and four producer categories
arrived at separately are stated as one set, the ones that can be checked are
checked, and the three decisions the workspace has been carrying as open are
settled. Planned in [boundary-consolidation.md](boundary-consolidation.md); the
direction it serves is
[design/INTEGRATION_SCOPE_POLICY.md](../design/INTEGRATION_SCOPE_POLICY.md),
adopted 2026-09-06.

It adds no format, no adapter, no node and no package. Its five items:

- ⬜ **BND-0 — the canonical producer contract**, *moved here from the
  recorded-source milestone on 2026-09-06*. Four categories produce motion and
  each was designed alone: recorded sources, live pose sources, tracker sources,
  and generated sources. What is unified is the **canonical value boundary**,
  not an I/O API — `SourceAnimation → HumanoidAnimation` for recorded,
  `timestamp + HumanoidPose` for live, `timestamp + TrackerFrame` for trackers,
  and `request/context → HumanoidAnimation or a pose stream` for generators.
  Done when a fifth producer is added by *naming* a crossing.
- ⬜ **BND-1 — one reference pipeline, proved once for every category.** Every
  source reaches `UsdSkelAnimation` today along its own tested path, and no
  single test says the same thing happens to all of them. One integration test,
  three sources — a `.vrma` clip, a BVH export, a recorded live trace — through
  an identical downstream call sequence. A source needing a downstream branch
  has found a defect, which is the point of running them together. This is the
  test NPZ/AMASS later joins **without changing it**.
- ⬜ **BND-2 — settle the adapter distribution decision.** Open since v0.7.0:
  `ost library package` produces an adapter artifact and no lane publishes one,
  so "the adapters are optional artifacts" is a design statement with nothing
  behind it. Recommended answer — one version, separate artifact membership.
- ⬜ **BND-3 — artifact closure as the release gate**, as one checklist a
  release passes or does not, rather than seven lanes and some prose.
- ⬜ **BND-4 / BND-5 — make the invariants checkable, and finish separating the
  workspace contract from its history.** The `DEPENDENCY_RULES.md` split carried
  from Product P0 lands here: doing it alongside a repository split is one
  migration of the section-number citations instead of two.

## Then: the motion foundation repository split ⬜ — and it can end at its gate

**Boundary:** `motionCore` and `motionRuntime` build, test, package and version
independently, and this workspace consumes them as an external dependency.
**Scope decided 2026-09-06: the motion foundation only** — `vrmRetarget` stays,
and so do `motionSource`, `motionBvh`, `motionTracking`, `liveTransport` and
`osc`. Planned in [motion-foundation-split.md](motion-foundation-split.md).

**It is scheduled ahead of its own preconditions on purpose, so it opens with a
measurement that can close it.**
[Scope policy §10](../design/INTEGRATION_SCOPE_POLICY.md) requires two consumers
outside VRM before a component leaves; after Motion Phase E there is exactly one
— `execMotion`, vendor-neutral by specification — and two of the four conditions
are not met at all. MFS-0 measures them and the answer is allowed to be no.

The track divides at a one-way door, and only the last part is behind it.
**Reversible:** the public API checked free of VRM vocabulary as a property
rather than a belief, its own version, its own package, its own suite run
against the installed artifact, and the workspace consuming it through
`find_package` as though it were external. Every one of those improves this
repository whether or not anything moves, which is why an inconclusive gate
wastes none of it. **Irreversible:** moving the history, and turning an in-tree
edge into a pinned external dependency — a second release contract, a second CI
configuration, and the loss of one-PR changes across the boundary, bought only
when someone outside VRM is actually consuming it.

## After those: NPZ / AMASS recorded sources, and the ARDY generation adapter ⬜

Two producer additions, and they are last because they are the ones that *use* a
boundary rather than fix one. Both were ahead of OpenExec until 2026-09-06.

- ⬜ **NPZ / AMASS through the existing `motionSource` boundary.** The recorded
  half gains a second format family, and the boundary is already built for it: a
  reader is allowed format syntax and storage interpretation, and never the VRM
  target rig, the target rest pose, the retarget policy, stage authoring, an
  OpenExec graph, or a vendor runtime. **A container is not a format** — the same
  `.npz` means different things from AMASS, SMPL-X, a HumanML3D derivative or a
  custom dump, so what ships is a container reader plus an explicit profile, and
  the field layout never reaches a core API. Whether that is one identity
  (`motionNpz`) or two (`motionNpz` + `motionAmass`) is settled by **measuring a
  few files of the real corpus first**; deciding before the measurement is how a
  boundary ends up shaped like whichever file arrived first.
  [The recorded track](recorded-motion-sources.md) §13.
  **A file-format plugin is not part of this**: a CLI and a plain library
  reaching canonical motion is enough, and `.npz` becomes an `SdfFileFormat`
  only when composing one directly onto a stage has a use case.
- ⬜ **The ARDY generation adapter** (Motion Phase F), behind the vendor-neutral
  `IMotionGenerator` that BND-0 freezes. The generator implementation itself is
  never in this repository
  ([scope policy §2](../design/INTEGRATION_SCOPE_POLICY.md)). Done when a
  generated take and a `.vrma` clip go through the same code path from the
  retarget onwards.
  [The adapters track](adapters-mocopi-vmc-ardy.md) §7.

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
  **The dependency split is scheduled as BND-5** of
  [boundary consolidation](boundary-consolidation.md) since 2026-09-06: doing it
  in the track that also prepares a repository split is one migration of the
  section-number citations rather than two.

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
