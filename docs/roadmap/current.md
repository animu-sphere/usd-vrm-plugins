# Current

The next milestone and active carry-over work. **Shipped work is not repeated
here** — it lives in the [delivery history](../reports/delivery-history.md) and
the per-version [release records](../releases/). A milestone's own detail lives
in its track document; this file carries the boundary, the completion
conditions, and what is still open.

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## Next: v0.8.0 — installed-package consumer lane, shared OSC foundation and VRChat OSC Trackers input ⬜

**Renumbered from v0.7.5 on 2026-08-29**, when packaging hardening became the
first item of the near-term plan. The two halves ship on one tag because they
are one boundary — a third adapter *and* the proof that every package this
workspace produces can be consumed from outside it — and tagging a release whose
own packages are unverified is the thing this release exists to stop. The
re-ordering is recorded in
[the roadmap's status table](README.md#status-at-a-glance).

**Release boundary, first half — distribution.** Every package this workspace
installs configures, resolves and links from a **clean prefix**, driven by a
consumer project that names no target from this source tree, on all three OS.
Planned in [packaging-hardening.md](packaging-hardening.md); the per-package
promise is [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md).

**Release boundary, second half — input.** One physical session reaches the
**unchanged** canonical motion and retarget pipeline through a **third**
independently modelled live surface — VRChat OSC tracking input — over a
protocol-neutral OSC decoder and a shared transport layer that no adapter
maintains a private copy of. Planned in
[osc-and-vrchat-trackers.md](osc-and-vrchat-trackers.md).

**Why distribution goes first.** Five phases of splitting checked every boundary
they created *from inside*, and neither a composed build nor `ost library build`
ever opens a package config file. On 2026-08-29 that gap produced a defect
rather than a hypothesis — two installed packages named a target no consumer
could resolve, with all 17 lanes green
([the track](packaging-hardening.md) §1). The fix that landed is per-adapter;
the general one is a consumer that is not us, and that is what this half builds.

**A tracker source is not a pose source.** VMC and mocopi carry humanoid bone
transforms; VRChat OSC carries numbered tracker observations, which are pre-IK,
and a tracker index is not a body role. The adapter therefore stops at a
`TrackerFrame` and the humanoid solve is a separate, generic boundary — the one
place this track could push a VRChat-shaped type into `motionCore`, which it
does not do under any outcome
([§5](osc-and-vrchat-trackers.md#5-a-tracker-source-is-not-a-pose-source)).

Not in this boundary: Avatar Parameters, OSC eye tracking, OSCQuery discovery,
an OSC sender or router, two-way VRChat client integration, realtime display,
and OpenExec. A VRChat client is never a test dependency — the generated corpus
and a recorded mocopi `VRChat (OSC)` session are the evidence, and every replay
test completes with nothing installed.

### Done when

**Packaging** — the milestones and their evidence are
[packaging-hardening.md](packaging-hardening.md) §4.

- [x] every distributable package's consumer contract is written down — package
      name, exported target, header root, required packages, platform
      dependencies, aggregate membership, and whether standalone installability
      has been **measured** or only reviewed *(PKG-0, 2026-08-29:
      [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md), derived from
      the CMake sources. Twelve packages take a `find_package` contract; three
      plugin bundles export no target and install no config **by design**)*;
- [x] one consumer fixture configures and links against one package from a
      clean prefix, and is shown to **fail against the pre-fix config** before it
      is trusted *(PKG-1, 2026-08-29: `osc` first, because its edge set is empty
      and a failure can only be the config file itself, then `vrmAdapterVmc`,
      because that is the package the defect was in)*;
- [x] the driver runs by hand on a workstation before any lane exists, and
      answers which prefix a consumer actually gets *(PKG-2, 2026-08-29:
      `scripts/check_package_consumer.py`. For a plain library `cmake --install`
      and an extracted `ost` package are the same seven files; `vrmSchema` is
      the one row where the two prefixes could still diverge, and it has not
      been run)*;
- [x] all twelve `find_package` packages pass, with each failure fixed in the
      **config** and never in the fixture *(PKG-3, 2026-08-30: twelve of twelve
      configure, build, link and **run** from a prefix holding their own
      transitive closure and nothing else, and **no config file failed** — the
      compliance is now measured rather than reviewed. What did fail was the
      harness, three times; the fixes and the rule PKG-4's lane inherits from
      them are in the track. Half of
      [#113](https://github.com/animu-sphere/usd-vrm-plugins/issues/113) closes
      with it)*;
- [x] a PR-gating cell on all three OS builds the consumers from a prefix that
      holds no build tree, and the three platforms agree about the package
      closure except where a documented difference says why not
      (`ws2_32` vs `Threads::Threads` is the one expected) *(PKG-4,
      2026-08-30: **green on all three OS**, twelve packages each —
      `.github/workflows/package-consumer.yml`, plus the three scripts it is
      thin over. It copies no pin: `ost ci matrix` is what says which runners
      and which runtime digests, because the other hand-authored workflow here
      copied all three and went stale. Criterion 6 needed a contract before it
      needed a code path, and PACKAGE_CONTRACT.md §5.1 is it — a workspace
      target agrees or it is a defect, a declared platform dependency is
      present exactly where its cell says and **absent elsewhere**, and what a
      `pxr` build puts on its own link line is not a promise made here. **The
      two runs it took to get green both found defects, and neither was in a
      package**: a pulled runtime's CMake package names the producing machine's
      Python paths, in two layers, and the second of them is not overridable by
      any `-D` —
      [report 37](../reports/ost/37-2026-08-30-v0.22.6-runtime-python-paths-from-the-producer.md),
      which carries the upstream P1 and one question it leaves open)*;
- [x] `scripts/check_docs.py` refuses a `*Config.cmake.in` with no row in
      PACKAGE_CONTRACT.md, and a row naming a package that does not exist
      *(2026-08-30: five ways to fail it, each made to fail before the check was
      believed. Landed **before** the CI cell rather than after it — the
      document it checks is five days old, so the drift has had no time to
      happen, which is the moment to add such a check)*.

**Foundation** — the milestones and their evidence are
[osc-and-vrchat-trackers.md](osc-and-vrchat-trackers.md) §9.

- [x] the existing OSC decoder's public behaviour is frozen by characterisation
      tests before any source moves *(OSC-0, 2026-08-24)*;
- [x] the four `UdpReceiver` defects are fixed in `vrmAdapterVmc`, ~~each with a
      test~~, in a change that moves no file *(OSC-1, 2026-08-24. **Two of the
      four ship untested and it is deliberate** — a `-1` and an `INT_MAX` poll
      timeout differ only after 24.8 days, and a `POLLERR` wake-up is not
      producible from a suite that owns only its own sockets, so a test there
      would pass against the defect. The seam belongs to the extracted library
      and OSC-2 carries the ask)*;
- [x] the transport ring — receiver, queue, capture format, diagnostic vehicle —
      lives once, in a leaf outside the aggregate product's link closure, and
      every committed capture in both existing corpora still reads unchanged
      *(OSC-2, 2026-08-24: `libs/liveTransport`)*;
- [x] two adapters decode OSC through one library that contains no VMC and no
      VRChat address literal, enforced by a boundary check rather than by
      review *(OSC-3, 2026-08-29: `libs/osc`. The evidence came before the
      contract row — an address inventory written in `vrmAdapterVrchatOsc`
      decoded real bytes through the VMC-owned decoder first and needed five VMC
      tokens, every one of them the name)*;
- [x] no adapter imports a sibling adapter, checked in the binary *(VRC-0,
      2026-08-25: with a third adapter the trio is symmetric, and it is verified
      by injection in every direction rather than by the green result)*.

**VRChat OSC**

- [x] a real mocopi `VRChat (OSC)` session is captured and its addresses, type
      tags and cadence are inventoried **before** a decoder is written *(VRC-0 /
      VRC-1, 2026-08-30: six captures, 44 918 datagrams,
      [report 02](../reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md).
      **Eight addresses — three numbered trackers and a named `head`**, every one
      `,fff`, one message per datagram, no bundles, in a fixed eight-datagram
      cycle inside a median 0.053 ms; ~58 Hz emitted and ~39 Hz delivered. The
      inventory carrying no list of expected addresses is what made `head`
      arrive as a row instead of four absences, and a decoder reading that path
      segment as an integer would have dropped it silently)*;
- [x] generated fixtures fix the protocol's shapes with no hardware *(VRC-2,
      2026-08-30: twelve captures, written from
      [report 02](../reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md)'s
      measurements rather than from VRChat's published surface, and replayed
      against counts derived from the generator's structure. **One of the twelve
      is the session's own shape**; the manifest marks five more `derived` — the
      session's addresses and ordering in an arrangement it did not send — and
      six `unobserved`, each with its reason, because a corpus that cannot say
      how far a recording stands behind each fixture is one a later reader has
      to guess about)*;
- [ ] recorded fixtures replay deterministically with no client — **open on the
      bytes, not on the code**: the 2026-08-30 session is manifest-only under
      [the corpus policy](#standing-corpus-policy--recorded-evidence-is-not-the-generated-corpus),
      and the three CTest names that read the corpus — `_corpus`,
      `_trackerCorpus` and `_loopbackCorpus` — would pick a redistributable
      session up with no change;
- [ ] tracker position and rotation reach the canonical tracking space, verified
      against a recorded rest pose rather than the documentation alone;
- [ ] partial sets, timeouts, restarts and calibration discontinuity are stated
      policy with a fixture each, not emergent behaviour;
- [x] unknown VRChat OSC traffic is recoverable *(VRC-2, 2026-08-30:
      `VRM_VRCHAT_OSC_UNSUPPORTED_ADDRESS` is info and recoverable, the message
      is dropped and the datagram is not, and the `mixed-traffic` fixture
      carries avatar parameters, an avatar change, eye tracking and input beside
      two clean frames. Held apart from `TRACKER_ID_INVALID`, which is what a
      sender's bad index raises — collapsing the two would make a defect
      indistinguishable from a part of the surface nobody has implemented)*;
- [ ] a real session reaches a VRM avatar through **unchanged** `motion_capture`
      and `motion_retarget`, or the solve boundary is documented as the stated
      stopping point and the release claims tracker *input* rather than
      tracker-driven motion.

**Cross-source**

- [ ] ~~the same physical session is observed on native UDP, VRChat OSC and the
      BVH export~~ — **not producible on this sender, measured 2026-08-30**: its
      transfer format is exclusive *and* it records no BVH while sending OSC, so
      this path cannot share a take with any other observation
      ([report 02](../reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md) §5).
      What replaces it: the same labelled sequences, performed separately, and
      compared at the canonical layer for what each path carries and drops — the
      per-sample timing agreement report 01 measured is what that costs, and no
      tolerance widening buys it back;
- [ ] every difference is classified — including a *solve difference*, which is
      a category no previous comparison could attribute — rather than absorbed
      by widening a tolerance;
- [ ] what each path cannot carry is written down from evidence.

### Before the tag

The v0.7.0 preparation cost five wrong documents because a member count was
written from a workstation and the lane ran a different `ost`
([report 35](../reports/ost/35-2026-08-24-v0.22.2-release-artifact-membership.md) §6).
This release adds a second class of the same risk — packaging claims — so the
checks are listed rather than remembered:

- [ ] the installed-package consumer lane is green on all three OS, and its
      result is what the release's packaging claims cite;
- [ ] every bundle, library and adapter manifest agrees with
      [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md), and no row
      still says `unmeasured`;
- [ ] the aggregate product's closure is measured against
      `[workspace].release_members` on the **pinned** `ost`, not the
      workstation's — check `bootstrap.ost.version` in `openstrata.ci.yaml`
      against `ost --version` before writing any count;
- [ ] each adapter's standalone package closure is measured, including
      `vrmAdapterMocopi`'s raw `ws2_32` on a POSIX host (PKG-5);
- [ ] `liveTransport` and `osc` artifact contents are recorded — 9 files and 7
      as of 2026-08-29, and a change in either is a change in what the excluded
      side ships;
- [ ] the CHANGELOG names the **architecture** changes, not only the features:
      two shared libraries extracted, a third adapter, and every adapter's
      package config gaining a dependency it was missing;
- [ ] the artifact-only BVH smoke is green on **Linux and macOS**, not only on
      the workstation that wrote it — it is a `release.yml` step, so a tag is
      the first time those two cells run it, and the `workflow_dispatch --ref`
      dry run below is what turns that from a surprise into a measurement;
- [ ] `scripts/check_docs.py`, `check_motion_profiles.py` and `verify_corpus.py`
      are green, and `release.yml` is dry-run with `workflow_dispatch --ref`
      before the tag — a green PR lane proves nothing about it.

### Carried into v0.8.0

- ⬜ **Freeze the Linux and macOS symbol baselines.** `tests/baseline/symbols/`
  holds `windows-x86_64.txt` only, because until the workspace cells landed no
  lane ran the Phase 0 gate anywhere else. `--check` skips a platform with no
  committed file (it has nothing to regress against) and every other baseline
  artifact is verified on all three OS, so the gap is symbols alone. Closing it
  means running `tools/baseline_freeze.py --update` on a Linux and a macOS host
  and committing the result.
- ⛔ **The scheduled lane's `plugin_artifact` is still a 26.05 build.**
  `usdvrmfileformat-support-windows-cy2026` pairs a 26.05-built plugin with a
  26.08 runtime. OpenUSD guarantees no ABI stability across versions, so that
  artifact must be republished before the lane's result means anything. It is
  also the reason `ost ci validate` exits non-zero on a workstation that holds
  the artifact (the evidence gate); hosted runners do not hold it, so the
  generated lanes stay green.
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
back out of a trace — v0.7.0 closed the clip half. They do not yet reach a
**rig**, which is what #88 is actually about:

- ⬜ **`ExpressionResolve`.** A VRM expression binds N morph targets across M
  meshes plus material colours — it is *not* one blend shape — so expanding a
  named weight onto a rig needs `VrmExpressionAPI`, which is why `motionCore`
  carries the name verbatim, the clip reader authors a name and a number, and
  neither resolves anything.
- ⬜ **`motion_retarget` authors no `blendShapeWeights`**, and no
  `skel:blendShapes` / `skel:blendShapeTargets` binding on its output.
- ⬜ **Look-at is untouched**, in every layer.

## Then: the recorded-source and producer-contract tracks ⬜

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
