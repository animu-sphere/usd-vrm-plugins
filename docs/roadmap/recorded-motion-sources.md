# Recorded motion sources — the BVH direction

The plan for reading recorded motion files: a generic BVH pipeline whose centre
is **not** any one capture product, and the format-neutral layer under it that a
second reader can be added to without redesigning anything above.

This document holds **boundaries, order, and completion conditions** only. Where
it touches structure it defers: the `motionSource` / `motionBvh` identities,
their dependency directions, the profile placement, and the aggregate-product
decision are settled in
[architecture/WORKSPACE.md](../architecture/WORKSPACE.md) §1, §2, §5, and motion
semantics in
[design/MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md)
§8.3, §14, §15. Items this plan needs from those contracts are listed in
[§10](#10-contract-changes-this-plan-requires) rather than asserted here.

The version this targets is in the
[roadmap status table](README.md#status-at-a-glance), not here.

Legend: 🚧 in progress · ⬜ not started · ⛔ blocked

## 1. Why this exists, and why it is not a mocopi importer

A capture product has two surfaces, and this repository had only started on one
of them:

```text
live:      sensors -> phone / PC app -> UDP  -> live adapter -> LiveCaptureSource
recorded:  sensors -> PC app         -> BVH  -> reader       -> HumanoidAnimation
```

They share a vendor and very little else. The live surface argues about packets,
arrival timestamps, restarts, and tracking loss; the recorded surface argues
about a hierarchy, channel declaration order, a frame time, and a rest pose. So
they are separate code, meeting at canonical motion and nowhere earlier
([motion policy §8.3](../design/MOTION_ARCHITECTURE_POLICY.md)).

**The recorded half is a BVH pipeline, not a mocopi one.** That is a decision
with a cost — it is more layers than reading one product's files needs — and it
buys one thing: a BVH file outlives the application that wrote it, and joint
names, units, axes, and root conventions are facts about the *writer* rather
than about the format. Bake mocopi's answers into the parser and the second
producer is not a new profile, it is a rewrite.

```text
BVH syntax                what the bytes say
    ↓
format-specific extraction  BVH shapes -> neutral shapes
    ↓
motionSource              source skeleton and animation, no format
    ↓
source profile            what one producer's export means
    ↓
canonical conversion      -> motion::HumanoidAnimation
```

The layer that earns its keep before it is used twice is `motionSource`. v0.7.0
implements one reader; the boundary is drawn for two, because retrofitting it
for the second means changing every signature above it.

## 2. What each layer is allowed to know

| Layer | Knows | Must not know |
| --- | --- | --- |
| `motionBvh` syntax | `HIERARCHY`, `ROOT`/`JOINT`, `OFFSET`, `CHANNELS`, `End Site`, `MOTION`, frame count, frame time, channel values, **channel declaration order** | which joint is a `HumanBone`, the unit, the up axis, handedness, what a root translation means, the rest pose's interpretation, any target |
| `motionBvh` extraction | how a BVH hierarchy and channel set become a `SourceSkeleton` / `SourceAnimation` | anything about a producer |
| `motionSource` | source joints, parents, rest locals, frames, frame time, provenance, and the profile contract | that BVH exists |
| profile (data) | one producer *and export preset*: joint map, basis, unit, root and rest policy, required/optional joints | how to run anything |
| converter | all of the above, plus how to reach canonical humanoid semantics | the target avatar |

The bottom-right cell is the one that decides whether this pipeline stays
composable. A converter that knew a target VRM's joint order would be a second
`vrmRetarget`, and the fork is invisible until two sources disagree about the
same avatar — the same argument the
[adapter plan §2](adapters-mocopi-vmc-ardy.md#2-what-an-adapter-is-allowed-to-be)
makes about live input.

## 3. The profile contract

A profile is named `<producer>-<format>-<preset>-v<N>`, and every part of that
is load-bearing:

```text
mocopi-pc-bvh-standard-v1
mocopi-pc-bvh-professional-v1
rokoko-studio-bvh-default-v1
rokoko-studio-bvh-humanik-v1
motionbuilder-bvh-humanik-v1
blender-bvh-native-v1
```

**A producer is not a profile.** One application's export presets can disagree
with each other, and two applications can agree — HumanIK appears above under two
different producers for exactly that reason. An application *version* is not in
the id either: it goes in the corpus manifest, because a version that changes
nothing about the output contract must not fragment the profile set. `v<N>` moves
only when a producer's output contract breaks.

What a profile carries: profile id · expected joint names · expected hierarchy
characteristics · source handedness · up axis · forward axis · translation unit ·
joint name → `HumanBone` · root joint · root translation policy · root rotation
policy · rest-pose interpretation · unmapped-joint policy · required vs optional
joints · provenance label.

A user-defined profile is a file with the same shape, and the restriction on it
is the interesting part:

```yaml
schemaVersion: 1
id: studio-custom-bvh

coordinates:
  handedness: right
  upAxis: Y
  forwardAxis: Z
  units: centimeters

root:
  joint: Hips
  translation: root-motion
  rotation: body-orientation

joints:
  Hips: hips
  Spine: spine
  Spine1: chest
  LeftArm: leftUpperArm
```

**Declarative only** — no arbitrary code, no expression language, no embedded
producer algorithm, and no target VRM path. A profile that could name an avatar
would make the converter avatar-aware through the back door, and one that could
run code would make "which profile was used" stop being a reproducible fact
about a conversion. A producer needing a genuine algorithm gets a profile
implementation in code, where it is reviewed.

### 3.1 Selection is explicit; detection is an aid

BVH carries no reliable statement of who wrote it, so **there is no default
profile and no automatic fallback**:

```bash
motion_bvh_convert capture.bvh \
  --profile mocopi-pc-bvh-standard-v1 \
  --output canonical.usda
```

Detection may exist, and reports candidates with confidence and *reasons*:

```text
candidate: mocopi-pc-bvh-standard-v1
confidence: 0.92
reasons:
  required joints matched: 24/24
  hierarchy matched
  root channel pattern matched
```

Three things it may never do: settle on a profile the confidence does not
support, conclude a producer from joint names alone, or continue past a profile
mismatch with a warning. The failure this forbids is specific — a near-miss
profile produces motion that is subtly misassembled rather than absent, which is
worse than a refusal because it looks like a result.

## 4. Rest pose, and who corrects it

BVH `OFFSET` values and the first frame's rotations are the *source* rest, and
they have no reason to match a target VRM's:

```text
source local motion + source rest pose
    ↓  converter
canonical semantic motion
    ↓  vrmRetarget + target rest pose
target joint animation
```

| Layer | Owns |
| --- | --- |
| `motionBvh` | hierarchy, offsets, channels — retained, not interpreted |
| profile | how the source rest is to be read |
| converter | building the source rest pose in canonical form |
| `vrmRetarget` | source rest → target rest correction |
| VRM schema / stage | target humanoid mapping |

A converter that applied target-rest correction would duplicate `vrmRetarget`,
which v0.4.0 already shipped and tested. This is the single most likely place for
that duplication to appear, because the correction feels like part of "reading
the file correctly".

## 5. Output — a semantic clip, never a direct bake

```text
BVH  →  HumanoidAnimation  →  canonical semantic USD clip  →  motion_retarget  →  target VRM
```

```bash
motion_bvh_inspect input.bvh

motion_bvh_convert input.bvh \
  --profile mocopi-pc-bvh-standard-v1 \
  --output canonical.usda

motion_retarget --motion canonical.usda --avatar avatar.vrm --output result.usda
```

Stopping at the avatar-independent clip is what makes the same motion reusable
across avatars, separates a parsing failure from a retarget failure, lets a BVH
result be compared against a UDP one at the canonical layer, keeps the converter
free of VRM schema details, and hands the OpenExec parity comparison another
recorded input. A one-shot convenience CLI may be added later as a **thin
wrapper** over these two, never as a second path.

## 6. Diagnostics

A separate namespace from live input, because a file syntax error and a dropped
packet are not the same class of event and a reader should not have to know which
adapter it is being compared against:

```text
VRM_BVH_PARSE_FAILED            VRM_BVH_PROFILE_REQUIRED
VRM_BVH_UNSUPPORTED_CHANNEL     VRM_BVH_PROFILE_MISMATCH
VRM_BVH_FRAME_WIDTH_MISMATCH    VRM_BVH_UNMAPPED_JOINT
VRM_BVH_INVALID_FRAME_TIME      VRM_BVH_REQUIRED_JOINT_MISSING
VRM_BVH_NON_FINITE_VALUE        VRM_BVH_INVALID_ROTATION_ORDER
                                VRM_BVH_INVALID_ROOT_POLICY
```

The set is frozen **before** the parser, the way the VMC adapter's eight were:
a code set written after the fact describes whichever failures were hit first,
not the format. The left column is syntax and the right is semantics, which is
also the layer split — a code that would have to sit in both is a sign the layer
boundary is in the wrong place.

## 7. Testing

**Parser** — minimal hierarchy · nested joints · several channel orders · a
6-channel root · 3-channel joints · `End Site` · empty motion · frame-count
mismatch · channel-count mismatch · malformed token · non-finite value · invalid
frame time · CRLF and LF · whitespace variation · deep-hierarchy and
large-frame-count limits.

**Profile** — the mocopi joint set, hierarchy, unit, axes and root policy · a
second producer's · a custom YAML profile · a missing required joint · a profile
mismatch · an unmapped optional joint · an ambiguous auto-detection.

**Converter** — Euler order from the declaration order · basis conversion · unit
conversion · rest-pose construction · root translation · root orientation ·
missing bone · unsupported joint · exact frame timing · deterministic output ·
non-finite rejection · canonical provenance.

**End to end** — BVH → `motionBvh` → semantic clip → **unchanged**
`motion_retarget` → a target VRM `UsdSkelAnimation`, checked through a
`UsdSkelSkeletonQuery`: the binding resolves, the expected bones move, root
motion follows the policy, the same input gives the same output, and it runs from
packaged artifacts with no build tree on the path.

**Cross-source** — the same or near-identical motion through mocopi UDP, mocopi
BVH, and a VMC relay, compared at the canonical layer on sample timing, bone
rotations, root translation, missing joints, provenance, metadata loss, and how
each represents tracking loss. Latency is a live-path measurement only. Motion
equivalence uses `NearlyEqual`; recorded-value identity uses `operator==`
([MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#comparison-semantics-v060)).

## 8. Corpus

The second producer is in the corpus from the start, not added once the first
works:

```text
mocopi-neutral.bvh                rokoko-neutral.bvh
mocopi-arm-raise.bvh              rokoko-arm-raise.bvh
mocopi-walk-root.bvh              custom-profile-minimal.bvh
motionbuilder-humanik-minimal.bvh
blender-roundtrip-minimal.bvh
```

Completing the parser and converter against mocopi alone would leave every
assumption it made — joint names, unit, axes, root policy, hierarchy shape —
indistinguishable from a property of the format, and each one is invisible until
something disagrees. The release condition is **two profiles: mocopi and one
independent mocap producer with a documented BVH export.** MotionBuilder and
Blender are DCC round-trip coverage and may follow.

The manifest carries: producer · producer version · profile id · frame count ·
frame time · joint count · channel count · coordinate convention · unit · root
policy · expected mapped bones · expected diagnostics · redistribution status ·
source hash. Redistributable files are committed; the rest leave a manifest and
no bytes, which is the split the
[adapter plan §9.2](adapters-mocopi-vmc-ardy.md#92-corpus) uses for recorded
sessions and the VRM corpus uses for models.

**The first file arrived 2026-08-04**, and two things about the shape above
turned out differently in practice:

- **The manifest is one file per corpus half, not a `manifests/` directory.**
  `recorded/manifest.json` describes every recorded file, and whether its bytes
  are committed beside it is a field rather than a location. A row that moved
  directory when its redistribution status changed would break every reference
  to it for a reason that has nothing to do with the file.
- **Half of what the manifest carries cannot be measured, and is labelled.**
  Producer, version and redistribution are provenance; coordinate convention,
  unit and root policy are *observations* — read out of a file that declares
  none of them. They sit under `observations` with that said in the data, so
  nothing downstream can mistake "what this file appears to be" for "what this
  file says". The profile id stays `null` until a profile exists: naming one
  that has not been written would be the schema-from-one-file failure arriving
  through the manifest instead of through the code.

## 9. Milestones

| Milestone | Contents | State |
| --- | --- | --- |
| **BVH-0** — contract and fixtures | real samples from mocopi and a second producer; joints, hierarchy, channels, unit, axis measured; the `motionSource` model and profile schema settled; the diagnostic set frozen | 🚧 |
| **BVH-1** — syntax | `BvhDocument`, the parser, `motion_bvh_inspect`, malformed fixtures, deterministic tests | ✅ |
| **BVH-2** — semantics | the `motionSource` API, the profile API, the mocopi profile, the second producer's, basis and unit conversion, source rest pose, root policy, `HumanoidAnimation`, the semantic clip writer | ⬜ |
| **BVH-3** — end to end | `motion_bvh_convert`, the **unchanged** `motion_retarget`, the target VRM bake, artifact-only smoke, the recorded corpus | ⬜ |
| **BVH-4** — cross-source | the same motion through UDP and BVH, compared at the canonical layer; the VMC relay added where available; a decision record | ⬜ |

BVH-0 is a measurement milestone, and skipping it is the failure mode this whole
plan is shaped around: writing the profile schema from one producer's file makes
that producer's export the schema.

**The first real sample landed 2026-08-04**: a 17-second session exported from
one vendor's phone application, committed at
`libs/motionBvh/tests/corpus/recorded/redistributable/` with everything the
manifest asks for measured from it. What it settles and what it does not are
worth separating:

| Measured | Value |
| --- | --- |
| joints · channels · rows · frame time | 27 · 162 · 853 · 0.02 s (50 Hz) |
| channel declaration | identical on all 27 joints: `Xposition Yposition Zposition Zrotation Xrotation Yrotation`, so the Euler order is ZXY throughout |
| position channels | the 78 non-root columns are exactly their joint's `OFFSET` in **all** 853 rows — rest geometry restated every frame, not translation |
| root translation | absolute position beginning at the root `OFFSET` (0, 95.9893, 0), not a delta and not zero-based |
| basis | +Y up, +Z forward (toe `End Site`s), +X the character's left — right-handed |
| unit | centimetres, from a hip height of 95.9893 over an 81.46 leg chain |
| `End Site`s | five, each 0.1 along one axis: direction markers, **not** bone lengths |
| root joint | named `root`; there is no `hips`, and seven `torso_*` segments are one spine chain |

That is one producer, and the milestone asks for two. It also proves the parser
against something it could be surprised by for the first time — the generated
fixtures are shapes this repository wrote, and a file it wrote can only confirm
what it already believed. What is still open is everything BVH-0 names beyond
one sample: the second producer, and therefore the `motionSource` model and the
profile schema, which are exactly the things that must not be written from one
file.

**BVH-1 started ahead of BVH-0, and that is not the shortcut it looks like.**
The syntax layer is the one part of this plan that owes nothing to a
measurement: `HIERARCHY`, `CHANNELS`, a row width and a frame time are the
format's, not a producer's, which is exactly why §2 puts them in a layer that is
forbidden to know a producer at all. What waits on real files is everything
BVH-0 actually names — the joint sets, units, axes and root conventions, and
therefore the `motionSource` model and the profile schema. `libs/motionBvh`
landed with its corpus named after format shapes rather than applications
([§8](#8-corpus)); the real export that arrived the same week went into a
separate half of that corpus, with its own manifest and its own expectation
table, so the syntax layer still has nothing a producer's answers could leak
into.

## 10. Contract changes this plan requires

Structural claims belong in the contracts, in their own change, before this plan
depends on them ([docs/README.md](../README.md)).

- ✅ **`motionSource`, `motionBvh`, and the two CLIs have identities and edges.**
  [WORKSPACE.md §1](../architecture/WORKSPACE.md) names them and states what each
  layer may know; §2 carries the chain and its reversals; §5 puts them **inside**
  the aggregate product — unlike an adapter — because the libraries carry no
  product name even though the data beside them does. Landed 2026-08-03 with this
  document.
- ✅ **A producer profile is data, and product names in data are permitted.**
  WORKSPACE.md §1 states the exception and its test: ship every profile and the
  libraries are byte-identical. Motion policy §8.3 carries the recorded-input
  path itself.
- ⬜ **The semantic clip has one writer, and now two callers.** `motion_capture`
  and `usdVrmaFileFormat` both author the avatar-independent clip;
  `motion_bvh_convert` will be the third. Whether that authoring is shared code
  or a repeated shape is currently undecided, and three callers is where it stops
  being ignorable.
- ⬜ **`SourceProvenance` versus `MotionSourceMetadata`.** `motionCore` already
  carries source metadata for live capture. Whether a recorded file's provenance
  is the same type, a superset, or a neighbour is a `motionCore` question, and it
  should be answered before the converter sets its first field rather than after.
- ⬜ **Profiles need a packaging answer.** They are data that must reach an
  artifact-only smoke test, so `share/usd-vrm-plugins/profiles/motion/` is named
  in WORKSPACE.md §5 — but `ost` 0.21.0 has no notion of a data-only member, and
  how the files get staged is unverified. This is the same shape as the adapter
  packaging gap ([report 34](../reports/ost/34-2026-07-29-v0.21.0-adapter-library-discovery-gap.md)).
- 🚧 **The workspace graph gate reaches `libs/motionBvh`, measured rather than
  assumed.** With the library committed, `ost plugin test --workspace
  --graph-only` reports `4 bundle(s), 1 bundle edge(s), 5 libraries, 7 library
  edge(s), valid` on `ost` 0.21.0 — one more library than before it, so the
  descriptor is discovered and its (currently empty) `requires` is validated.
  The `profiles/motion/` directory is still a new shape and still unconfirmed;
  it gets the same treatment when it lands, the way the adapter's discovery gap
  was found.
- 🚧 **`tools/motionBvh/` is one member carrying two executables, and the graph
  gate has nothing to say about it.** Adding the directory left `--graph-only`
  reporting the same `4 bundle(s), 5 libraries` — and `--graph-only --json`
  names no tool at all, not even `motion_capture` or `motion_retarget`, so this
  is the graph walking bundles and libraries by design rather than a discovery
  gap like the adapter's. What that leaves unverified is the *grouping*: every
  workspace tool so far has been one directory, one executable, and an id equal
  to that executable's name, where this member is `motion_bvh` with
  `motion_bvh_inspect` inside it and `motion_bvh_convert` to come. Only
  `ost plugin package --workspace --product` walks a tool descriptor, so the
  member count and the `tools/<id>/bin` layout are measured when a packaging run
  next happens rather than asserted here.

## 11. Non-goals

- mocopi-specific mapping inside the BVH parser, or a default profile anywhere;
- target VRM mapping inside the converter;
- retarget inside the mocopi live adapter;
- an `SdfFileFormat` bundle for `.bvh` in the first pass — a plain parser, a
  converter, a CLI and a corpus come first, and `usdBvhFileFormat` would be a
  wrapper over them if it is ever wanted;
- fully automatic identification of arbitrary BVH dialects;
- scripting inside a profile;
- an FBX reader, and Mixamo ingestion with it — Mixamo is an FBX candidate and is
  deliberately **not** a BVH producer profile;
- reverse-engineering a native USB device protocol;
- face and expression capture;
- spring-bone or physics evaluation.

## 12. PR splitting

One PR never introduces a boundary and a large feature together:

1. ✅ this document and the contract changes it names
2. ✅ `motionBvh` syntax model and parser — the frozen diagnostic set, the
   document model, the parser, the format-shape corpus and its manifest, and a
   boundary check that fails on a producer name, a semantic diagnostic, or an
   OpenUSD value type in the syntax layer (2026-08-04)
3. ✅ `motion_bvh_inspect` — the report, its sections, the CLI's own boundary
   check, and a test that drives it over the library's corpus and checks every
   number against the manifest and against the `.bvh` text rather than against
   the parser (2026-08-04)
4. `motionSource` skeleton and animation model
5. the source profile contract
6. the mocopi BVH profiles
7. the second producer's profile
8. a DCC interoperability profile (optional)
9. BVH → canonical animation conversion
10. `motion_bvh_convert`
11. the retarget end-to-end test
12. 🚧 the recorded corpus and its manifest — the split, the manifest shape, the
    two checks over it, and the **first** producer export landed 2026-08-04,
    ahead of this position because a real file arrived and measuring it is
    BVH-0's whole content. The second producer's, and everything that needs a
    profile to describe it, still belong here

Every one of them checks: standalone build · dependency direction · no reverse
dependency · **no producer name in library code** · deterministic fixture tests ·
diagnostic stability · clean install.
