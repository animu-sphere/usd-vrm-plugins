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
producer: Studio Custom

coordinates:
  handedness: right
  upAxis: +Y
  forwardAxis: +Z
  translationUnit: centimeters

root:
  joint: Reference
  translation: absolute-position
  rotation: body-orientation

restPose: rest-offsets
unmappedJoints: report

joints:
  Hips:    { bone: hips, required: true }
  Spine:   { bone: spine, required: true }
  Spine1:  { bone: chest }
  LeftArm: { bone: leftUpperArm, required: true }

ignoredJoints: [Reference]
```

Every **value** above is one the contract defines by name
([`SourceProfile.h`](../../libs/motionSource/include/motionSource/SourceProfile.h),
2026-08-05) and every **key** is one a reader now reads
([`SourceProfileFile.h`](../../libs/motionSource/include/motionSource/SourceProfileFile.h),
2026-08-05) — this is the file, not a sketch of one. Four names changed on the
way here, each because the sketch was stating an intention where a converter
needs a fact. An axis carries its sign, because "+Z forward" and "-Z forward"
are both real and an unsigned axis makes a rig that faces backwards look like
one that does not. `root-motion` became `absolute-position` versus
`rest-relative`, which differ in where their zero is — a distinction the intent
spelling could not carry and the first real export made concrete
([§9](#9-milestones)). A joint map's right-hand side grew a `required` flag,
because "expected joint names" and "required joints" were two lines of this
section's own list with one place to put them. And `units` became
`translationUnit`, singular and saying which: a profile states no angle unit at
all, because a format says whether its angles are degrees or radians and a
producer does not get to disagree with its own format about that.

Three properties of the reader are decisions rather than mechanics, and each is
this section's own rule made checkable. **An unknown key is refused**, because a
misspelled `requred:` a permissive reader dropped would unbind a joint the
profile called mandatory and report nothing — the near-miss failure §3.1
forbids, arriving through a typo. **Nothing is assumed**: every convention has an
`unspecified` the reader refuses where it is written, so "there is no default
profile" holds inside a file as well as between files. And **a profile that is
read is already valid** — parsing ends in `ValidateSourceProfile`, so no
half-built profile reaches a caller who would have to re-prove it.

**Why the reader is written rather than borrowed**, since the file is YAML and
libraries for it exist. Two reasons and one consequence. A profile needs an
unknown key to be an *error*, which a document parser hands back as a key like
any other — so the strict half would be written either way, and it is the half
where the risk is. And YAML's implicit typing is actively wrong for this data: a
joint named `on`, `y`, `no` or `null` is a *writer's word*, and a reader that
turned it into a boolean would rename a joint nobody renamed. Against that,
`motionSource` links exactly one thing ([WORKSPACE.md §2](../architecture/WORKSPACE.md)),
and spending that on a configuration file would be a contract change.

The consequence is that the subset must not disagree with YAML *silently*. So
`scripts/check_motion_profiles.py` reads every shipped profile a second time with
a real YAML implementation, where one is installed, and compares the two
readings. The claim is not that this reader accepts everything YAML does — it
refuses anchors, tags, block scalars and nested flow forms, and says so — but
that a file it *accepts* means to it what it means to YAML. A refusal is a bad
file's worst outcome; a difference of interpretation nobody is told about is the
failure a hand-written reader can produce and a refusal cannot.

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

Two things follow from that in the contract, and both were choices. **Every
convention has an `Unspecified` and validation refuses it**, so a profile nobody
finished is a refusal rather than a silent set of answers — a default-constructed
profile is invalid by construction, which is "there is no default profile" said
where it can be checked. And **the match returns facts, never a score**: which
bones bound, which required ones did not, which joints nothing maps, which names
are ambiguous. A confidence is the detector's arithmetic over those counts,
because a weighting fitted to the exports on hand today would be a producer's
answer reaching the format-neutral layer through a float instead of through an
`if`.

The `24/24` above is the one number that arithmetic gets wrong, which is worth
stating because it is the only figure this section actually prints. A required
mapping whose name the rig repeats binds nothing *and* is not missing — it is
ambiguous — so required-count less missing-count reports it as matched. The
match carries `BoundRequiredCount()` for that reason, and a test pins the two
apart rather than leaving the first detector to discover it.

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
  file says". The profile id stayed `null` until a profile existed — naming one
  that has not been written would be the schema-from-one-file failure arriving
  through the manifest instead of through the code — and **it is now filled**
  (2026-08-05), together with `expectedMappedBones`. Those two fields are a
  claim rather than a measurement, so the scanner carries them through untouched
  and `scripts/check_motion_profiles.py` is what checks them: the profile's root
  against the file's, every mapped and ignored joint against the joints it
  carries, and the hierarchy the mapping implies against the one it has.
- **A row with no bytes is checked, not merely recorded** (2026-08-05, with the
  second producer). The split above says a non-redistributable recording leaves
  a manifest and no file, and the obvious reading of that is that the profile
  describing it goes unchecked — which would leave the second profile, the one
  this milestone exists for, verified by nothing. So the hierarchy left the
  hand-written `observations` and became a **measured** field: joint names with
  their parents, written by the scanner out of the bytes, and what
  `check_motion_profiles.py` holds a profile against when the file is absent.
  Everything that check claims survives — the root, every mapped and ignored
  joint, nothing left neither, the chain the mapping implies. What does not is
  one link, that the row is a faithful reading of those bytes, and that is held
  by the SHA-256 and re-derived by anyone who runs `scripts/fetch_corpus.py`.
  The fixture pair under `tests/motion/fixtures/profile-check/` keeps the absent
  path honest: one profile that describes an invented rig and one that misplaces
  a single bone inside it, the second of which must fail.

**Nothing fetches in CI, and that is a decision rather than an omission.** A
build lane that reached a third party's server would make a PR's colour depend
on someone else's uptime, and it would automate an acknowledgement the corpus
policy deliberately asks a person to make (`--accept-license`). The cost — that
CI never re-measures those bytes — is what the `hierarchy` field above is spent
to make small.

## 9. Milestones

| Milestone | Contents | State |
| --- | --- | --- |
| **BVH-0** — contract and fixtures | real samples from mocopi and a second producer; joints, hierarchy, channels, unit, axis measured; the `motionSource` model and profile schema settled; the diagnostic set frozen | 🚧 |
| **BVH-1** — syntax | `BvhDocument`, the parser, `motion_bvh_inspect`, malformed fixtures, deterministic tests | ✅ |
| **BVH-2** — semantics | the `motionSource` API, the profile API, the mocopi profile, the second producer's, basis and unit conversion, source rest pose, root policy, `HumanoidAnimation`, the semantic clip writer | ✅ |
| ↳ how BVH-2 closed | the **second producer's profile** landed 2026-08-05 and cost two contract changes on the way, which is the milestone paying for itself: the first export had made "the root joint is the hips" and "the offsets are the rest" look like properties of the format. The semantic clip writer landed the same day with `motion_bvh_convert`, as a third repeated shape rather than shared code and with the condition that would change that written down ([§10](#10-contract-changes-this-plan-requires)), along with the value model, the profile contract and file, the extractor and the converter | ✅ |
| **BVH-3** — end to end | `motion_bvh_convert`, the **unchanged** `motion_retarget`, the target VRM bake, artifact-only smoke, the recorded corpus | 🚧 |
| ↳ what remains of BVH-3 | the **artifact-only smoke**, which is **blocked and diagnosed** rather than merely unwritten: a packaged product carries no profiles, so a converter unpacked from one refuses every file it is given ([§10](#10-contract-changes-this-plan-requires)). The tool, the unchanged retargeter and the bake landed 2026-08-05 ([§12](#12-pr-splitting) items 10 and 11), and the **target VRM** landed 2026-08-11 — the fixture bake stays beside it rather than being replaced, because a rig shaped so a broken rest-pose correction cannot pass it and a rig somebody shipped are not the same test | 🚧 |
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
what it already believed.

**The second producer arrived 2026-08-05**: two exports from the
[Bandai Namco Research Motiondataset](https://github.com/BandaiNamcoResearchInc/Bandai-Namco-Research-Motiondataset)
(1 and 2), CC BY-NC 4.0 and therefore **not committed** — two rows in the same
manifest, no bytes, fetched on demand ([§8](#8-corpus)). One walk and one
standing wave, because the pair is what separates a property of the export from
a property of a recording.

| Measured | Value |
| --- | --- |
| joints · channels · rows · frame time | 22 · 132 · 31 and 19 · 0.0333333 s (30 Hz) |
| channel declaration | identical on all 22 joints: `Xposition Yposition Zposition Zrotation Xrotation Yrotation` — ZXY, the one convention it shares with the first |
| root | **two joints**. `joint_Root` translates and never rotates (157.634 of Z in the walk, exactly zero in the wave); `Hips`, its only child, carries the body's orientation and a translation of its own |
| position channels | the 20 joints below `Hips` restate their own `OFFSET` every frame, as the first producer's do; `Hips` does not |
| `OFFSET`s | bone-local, +X down each bone — `Spine` (15.7357, 0, 0), `LowerLeg_L` (39.0811, 0, 0). Composed at identity they send the spine and both legs the same way and make no figure at all |
| `Hips` `OFFSET` | per file, not per rig: (0.0408292, 93.9915, 0.0639531) in one, (4.5925, 91.4895, **-427.239**) in the other, every other offset agreeing to the digit |
| unit · up axis | centimetres, +Y — hip height 92.1273 over an 80.07 leg chain |
| `End Site`s | five, and the two files **disagree**: 10 or ±5 along X in one, (0, 0, 0) in the other |
| humanoid gaps | `Hips`/`Spine`/`Chest`/`Neck`/`Head` with both shoulders on `Chest` — no `upperChest` to bind |

**The schema did not survive it unchanged**, which is the milestone working
rather than failing. Three rows above are contract questions, and they are
listed in [§10](#10-contract-changes-this-plan-requires) rather than answered
here: a profile states one `root.joint` where this export splits the answer
across two; `restPose: rest-offsets` describes no pose this file has; and the
converter reads root motion from joint index 0, which here is a static reference
node. The two rows that are *not* problems are worth naming too. A rig with no
`upperChest` needs nothing new at all — a bone no source joint exists for is
simply absent from a profile's map, and a clip's consumers ask for a bone's
nearest *present* ancestor rather than assuming every slot is filled — and a
per-file `Hips` `OFFSET` is exactly why the corpus keeps a second file from the
same rig.

One more difference surfaced only when the profile was written and the export
was actually converted, which is worth recording as the reason to convert
rather than only to measure. This producer restates each joint's `OFFSET` in
its position channels the way the first one does, but **not bit-exactly**: the
values carry solver noise at the 1e-5 to 1e-7 scale. Recorded-value identity is
`operator==` by contract, so the converter reports 18 joints as translation it
could not carry where the first producer reports none — and the two files from
this one dataset disagree with each other (18 and 16), which is the clearest
available statement that the number is about float noise rather than about
motion. Nothing is lost; the report is a false alarm, and changing it means
deciding what "restating rest geometry" tolerates, which is a contract question
and not a profile one. It is left standing and written down.

Note what this producer does **not** give: a documented export. Its README
states the frame rate, the contents and the styles, and says nothing about the
skeleton, the unit, the axes or the root — the same silence as the first. The
release condition's word "documented" is satisfied by the dataset being
published, versioned and hash-pinnable, not by the format declaring anything.

**The first profile is written from it** (2026-08-05,
[`profiles/motion/mocopi-mobile-bvh-default-v1.yaml`](../../profiles/motion/mocopi-mobile-bvh-default-v1.yaml)),
and the split between what the file settles and what the profile decides is the
point of writing it here rather than in the corpus:

| The file settles | The profile decides |
| --- | --- |
| the basis, the unit, the root's name and that its samples are absolute positions | that the root *is* the hips, since it sits at hip height and parents both legs and the spine |
| that `torso_7` parents the neck and both shoulders | that `torso_7` is therefore `upperChest` |
| that there are seven `torso_*` segments and the canonical humanoid has three | which two of the remaining six are `spine` and `chest` — placed at 21% and 46% of the root-to-`torso_7` rise, which is a judgement the geometry narrows and does not make |
| that no joint declares a rest rotation | that the rest pose is `rest-offsets` |
| that the export is a fixed 27-joint skeleton | that every mapped joint is `required` and unmapped ones are `refuse`d, with the five it deliberately ignores named |

Two of those rows are worth carrying into the converter. The four unmapped spine
segments are **not** motion thrown away — a joint between two mapped ones is on
the path between them, so a converter that composes that path keeps the chain's
total orientation whatever the profile chose, and what the choice changes is how
the bend is *distributed*. And that composition is not written yet, which makes
it a converter question this profile has now put a name to
([§10](#10-contract-changes-this-plan-requires)).

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
- ✅ **The semantic clip has one writer, three callers, and stays a repeated
  shape** (2026-08-05, with the converter). `motion_capture`,
  `usdVrmaFileFormat` and now `motion_bvh_convert` all author the
  avatar-independent clip. The decision is *not* to share the code yet, and the
  reason is that the writers differ in the field that matters most:
  `motion_capture` authors **identity** rest transforms because a capture stream
  reports rotations relative to the humanoid rest and never the rest itself,
  while this one authors a **real** rest pose because a recorded file states one
  and a profile says how to read it ([§4](#4-rest-pose-and-who-corrects-it)).
  Sharing them today means parameterising over exactly that difference, and
  [§12](#12-pr-splitting)'s rule is that one PR never introduces a boundary and
  a large feature together. What *would* change the answer is a fourth caller,
  or a third needing neither variant — at that point the difference is a
  parameter and the shape is a function. In the meantime the risk is the two
  drifting on the parts that are not a choice, so those are pinned by test
  rather than by intention: the joint set is in `HumanBone` order, the time
  codes are frames, and `scales` is authored. That last one is the scar —
  `UsdSkel` fetches translations, rotations and scales as a unit and `scales`
  has no schema fallback, so a clip missing it does not animate without scale,
  it silently resolves to the rest pose.
- ✅ **`SourceProvenance` versus `MotionSourceMetadata`** — a **neighbour**, with
  a one-way narrowing derivation (2026-08-04, with the model). Two independent
  arguments give the same answer: the canonical type rides on every pose and is
  serialised with it, so a per-clip fact has no business on it; and everything in
  `SourceProvenance` is known *before* any motion exists, which the canonical
  type describes. `CanonicalMetadata` maps producer → `provider`, format →
  `protocol`, and always `kind = Clip`, dropping the producer version and the
  profile id — the narrowing is pinned by a test rather than by a sentence.
  Semantics: [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#recorded-source-provenance-v070).
- ✅ **The quaternion rotation form has no producer, and the converter decided
  what that costs** (2026-08-05, with the converter). `SourceJointTrack` carries
  angles-with-an-order *or* quaternions, because composing three angles needs the
  handedness a profile supplies and decomposing a quaternion would invent an
  order the writer never declared — so neither converts into the other before a
  profile is in hand. Of the two honest answers, the **first** was taken: a
  quaternion track is refused as `UnsupportedRotationForm`, naming the joint and
  saying that no reader writes this form yet. The second — a synthetic fixture,
  said in the corpus to be synthetic — stays open and is what a reader producing
  quaternions arrives with. What settled it is that the refused path costs one
  branch and the implemented one would cost a code path tested only against a
  value this repository invented.
- ✅ **The canonical basis had a forward axis nobody had written down**
  (2026-08-05, with the converter). Three of the four canonical conventions were
  in [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md) already; the forward axis
  was not, and a converter that has to map a profile's `forwardAxis` onto
  canonical cannot proceed without it. It is recorded as **+Z**, which is a
  reading of the tree rather than a new decision — the VMC adapter's existing
  conversion leaves a +Z-forward sender's forward untouched, and the avatars this
  is retargeted onto face +Z by specification. Two things now depend on it in
  code: `CanonicalBasis`, whose determinant is the handedness question, and the
  rule that angles are composed by the right-hand rule *always*, leaving the
  mirror to that determinant. Handling handedness in both places is the failure
  the contract section spells out, because it is correct in every axis-aligned
  test pose.
- ✅ **The six semantic diagnostics are raised by the caller, from typed refusals
  the profile API returns** (2026-08-05, with the profile contract). The frozen
  set ([§6](#6-diagnostics)) lives in the reader, named for the format it reads,
  and its semantic half is raised "where a document meets a profile" — which is
  `motionSource`, the one library forbidden to know that reader exists
  ([WORKSPACE.md §2](../architecture/WORKSPACE.md)). Of the three answers that
  were open, the third won: `MatchSourceProfile` returns a
  `SourceProfileRefusal` naming the *event* in terms no format supplies —
  `root-joint-mismatch`, `ambiguous-joint-name`, `required-joint-missing`,
  `hierarchy-mismatch`, `unmapped-joint-refused` — and the caller holding both a
  reader and a profile maps it onto that reader's codes. The two rejected
  answers are worth keeping: plain text alone (which is right for a structural
  invariant, and is what
  [`SourceSkeleton.h`](../../libs/motionSource/include/motionSource/SourceSkeleton.h)
  still does) would make that caller parse prose to pick a code, and a second
  `VRM_MOTION_SOURCE_*` namespace would give one event two spellings and
  duplicate a set whose whole value is being frozen and closed. The mapping is
  not one-to-one and does not need to be — an ambiguous joint name has no code
  of its own and is a profile mismatch — which is exactly why the refusal names
  the event rather than the code.
- ✅ **A mapped bone's local rotation is the composition of the path above it,
  and the converter implements it** (2026-08-05). A profile maps a rig's joints
  onto the canonical humanoid, which has fewer of them: the first real one leaves
  four spine segments and a neck segment mapped to nothing
  ([§9](#9-milestones)). Those are not joints to drop — each sits *between* two
  mapped ones, so a converter that took a mapped joint's local rotation verbatim
  would lose every rotation above it and place the arms and head wrong, which
  reads as a subtly misassembled body rather than as a failure. The rule is one
  sentence — a bound bone's local rotation is the composition of the source local
  rotations from just below its nearest bound ancestor down to it — and it
  belongs to the converter rather than to a profile, because a profile that could
  state it would be stating an algorithm. Written here before the converter
  existed, it arrived as a decision already taken rather than as a bug in a
  retarget nobody could localise. Two things landed with it that the sentence did
  not say: the **rest pose is built by the same walk**, so one composition serves
  both and cannot disagree with itself, and *which* bones absorbed a chain is
  reported (`ConversionReport::composedBones`) because a cross-source comparison
  will want to know. Over the one real export it is four: `spine`, `chest`,
  `upperChest` and `head`.
- ✅ **A root is two joints in the second producer's export, and the answer is
  the path rule again** (raised, decided and implemented 2026-08-05).
  `joint_Root` carries the locomotion and never rotates; `Hips`, its only child,
  carries the body's orientation and a translation of its own
  ([§9](#9-milestones)). A profile states one `root.joint`, and the converter
  read root translation and rotation from joint index 0 — so naming `joint_Root`
  lost the orientation and naming `Hips` was not expressible. The decision, in
  [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#recorded-source-rest-pose-and-the-path-rule-v070):
  **both root policies describe the composition of the source path from the
  rig's root down to the joint bound to `hips`.** It costs no new vocabulary,
  the path is always defined because `ValidateSourceProfile` already refuses a
  profile that does not bind `hips` and one that binds them optionally, and a
  rig whose root *is* its hips has a path of one joint and reads identically —
  which is what makes it safe to state after a producer had already shipped.
  `root.joint` keeps naming the rig's root, because that field is what a profile
  is *matched* by and matching is a question about shape.
- ✅ **A first-frame rest was half a rest, and the second producer is the export
  that shows it** (raised, decided and implemented 2026-08-05). Its
  `OFFSET`s are bone-local — every one along +X down its own bone — so composing
  them at identity puts the spine and both legs the same way and makes no figure
  at all; the rest is not recoverable from the rest translations, and
  `rest-offsets` is not available to it. `FirstFrame` is, and it was
  taking rotations from frame 0 and translations from the `OFFSET`s: one rest
  built out of two poses, invisible for any producer whose offsets are its rest
  and wrong for the only kind of producer that picks the setting. The decision:
  **under `first-frame`, a joint's rest translation is its first sampled
  translation where it has one.** This producer's `Hips` `OFFSET` is capture
  bookkeeping — 427 cm of Z in one file, near zero in another from the same rig
  — so the old reading put an artefact of the capture volume into a rest pose,
  and `vrmRetarget` would have subtracted it from every frame. What the setting
  *claims* is narrowed in the same change: not that the writer's first frame is
  neutral, but that the source states no rest and the profile elects frame 0 to
  measure motion away from. The limitation that follows is written down rather
  than discovered — two clips from one dataset are each anchored to their own
  first frame.
- ✅ **A first-frame rest does not survive contact with a real avatar, and the
  second producer's rig has a real one** (raised, decided and implemented
  2026-08-05, by baking onto a VRM). `first-frame` maps the source's frame 0
  onto the target's rest, so the avatar never leaves its own rest pose's
  neighbourhood: a T-posed avatar walked with a clip whose arms hang at its
  sides holds its arms straight out for the whole clip, and the leg frame 0
  caught mid-stride carries that bend forever while the other looks right. Both
  were visible on the first bake, and the asymmetry is what identified the
  cause: 122.5° at one knee against 162.4° at the other, so the 40° between
  them became a permanent offset on one leg and nothing on the other.
  The fix is not a better default — it is that **this producer's rest exists**.
  Its own paper says the capture was retargeted to the proportions of a
  published character model, and the six bone lengths that claim implies agree
  with the export's own offsets to five significant figures, so the rig is that
  rig and its neutral is a T-pose. `restPose: t-pose` says exactly that, and the
  rest is built from canonical T-pose directions and the rig's *own* offsets —
  no number from that character model enters this repository, which matters
  because its licence forbids redistribution outright.
  [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md#recorded-source-rest-pose-and-the-path-rule-v070)
  carries the one weakness: offsets pin a bone's direction and not its roll, so
  the roll is taken from frame 0 and only the aim from the canonical pose.
- ⬜ **A released avatar's humanoid is incomplete, and what a bone it cannot
  bind costs is measured but not decided** (raised 2026-08-11, by baking onto a
  real VRM). VRM 1.0 makes `upperChest` optional; `Seed-san.vrm` leaves it out;
  the mocopi profile maps a source joint *to* it. `PoseRetargeter` reports the
  bone as unmapped and drops its rotation, and the measurement is exact rather
  than approximate: every bone below it reproduces the source **computed as if
  the source's `upperChest` had never moved** — 11.2° at worst on the recorded
  session, identical for the neck, the head and both arms, and agreeing with
  that prediction to 6e-7 degrees. So the rotation is dropped *whole*, not
  smeared, which is the one thing that had to be established before the question
  could be asked properly.

  This is the mirror of the path rule, and it does **not** have the same answer
  by symmetry. On the source side an unmapped joint always sits between two
  mapped ones, so composing it downward is the only reading. Here there are two:
  **hoist** the rotation to the nearest bound ancestor (`chest`), which keeps
  the chain rigid and pivots it too low; or **push** it onto each nearest bound
  descendant (`neck`, both shoulders), which pivots too high and leaves the
  missing joint's own offset unrotated. Which is less wrong is a fact about
  rigs, and one avatar cannot supply it — a second released model with no
  `upperChest` is what this needs, in the way BVH-0 needed a second producer.
  Until then the drop is pinned by `workspace_real_avatar_bake` as a
  characterisation, so a later rule has to change that test before it changes
  the behaviour, and `motion_retarget` names the bone on stderr rather than
  losing it in silence.
- 🚧 **Profiles need a packaging answer.** They are data that must reach an
  artifact-only smoke test, so `share/usd-vrm-plugins/profiles/motion/` is named
  in WORKSPACE.md §5. **The plain-CMake half is done and measured**
  (2026-08-05): the root project installs `profiles/motion/*.yaml` to exactly
  that destination, verified by installing to a scratch prefix. The `ost` half is
  not — 0.21.0 has no notion of a data-only member, and whether a packaged
  product carries these files is untested. This is the same shape as the adapter
  packaging gap ([report 34](../reports/ost/34-2026-07-29-v0.21.0-adapter-library-discovery-gap.md)).
- ✅ **A binary import check cannot survive this chain, and that is measured**
  (2026-08-05, with the converter). `motionBvh` refused any OpenUSD library in a
  built artifact's imports, which was the strongest form its "no OpenUSD" claim
  could take. Once the extractor took the edge to `motionSource` — and through it
  to `motionCore`'s `Gf` value types — what that check reports became the
  *linker's* answer rather than the library's: MSVC pulls only the archive
  members that resolve a symbol, GNU ld with `--as-needed` drops the resulting
  unused entries, and Apple's ld64 records every library on the link line whether
  or not one is used. All three are right about their own artifact, so one source
  tree produces two answers. **It cost a red macOS lane to establish**, on a
  claim verified on Windows and generalised in the same change — the same failure
  shape as the quaternion-precision one, and the reason this entry exists rather
  than a quieter fix. The check is removed rather than narrowed on both
  `motionBvh` and `motion_bvh_inspect`; the source rule, which forbids every
  OpenUSD name in every file of the library, is platform independent and carries
  the claim. Two consequences are stated where somebody meets them rather than
  discovered: a standalone configure of `tools/motionBvh` now needs OpenUSD on
  the prefix path, because `find_package(motionBvh)` resolves `motionSource` and
  through it `pxr`; and on macOS `motion_bvh_inspect` records those dylibs.
- 🚧 **The workspace graph gate reaches both libraries, measured rather than
  assumed.** `ost plugin test --workspace --graph-only` reported `5 libraries, 7
  library edge(s)` with `motionBvh` committed and `6 libraries, 8 library
  edge(s), valid` with `motionSource` beside it (`ost` 0.21.0, 2026-08-04) — one
  more library and one more edge, so the new descriptor is discovered and its
  declared `motionCore` edge is validated rather than assumed. With
  `profiles/motion/` committed the gate reports **the same** `6 libraries, 8
  library edge(s), valid` (2026-08-05): a directory of data is discovered as
  nothing and perturbs no edge, which is the right answer and now a measured one.
  What it does **not** cover is whether those files reach an artifact — that is
  the packaging item above, and this gate is as silent about it as it is about
  every tool.
- ✅ **`tools/motionBvh/` is one member carrying two executables, and `ost`
  0.21.0 packages it.** Every workspace tool before it was one directory, one
  executable, and an id equal to that executable's name; this member is
  `motion_bvh` with both `motion_bvh_inspect` and `motion_bvh_convert` inside
  it. `ost plugin package --workspace --product` reports
  `== motion_bvh 0.6.0 (tool) ==`, `build: matched (ost-managed)`, and a
  product of **7 exact members** (4 bundles + 3 tools); the archive carries
  `bin/motion_bvh_inspect.exe`, so an id that is not an executable name costs
  nothing. Measured 2026-08-04, deliberately *before* release preparation —
  deferring it would have made release prep the place a new member shape is
  first tried, and the shape held: re-measured 2026-08-05 with the second
  executable actually present, the archive carries **both** and the member
  count does not move.
- ⬜ **A packaged product carries no profiles, and that blocks the artifact-only
  smoke** (measured 2026-08-05). The same archive is exactly
  `bin/motion_bvh_inspect.exe`, `bin/motion_bvh_convert.exe` and
  `openstrata.tool.yaml` — no `share/usd-vrm-plugins/profiles/motion/` anywhere
  in the product. A tool member packages the `directories:` it declares, and
  `ost` 0.21.0 has no notion of a data-only member, so a data directory outside
  the member has no way in. The consequence is the specific one
  [WORKSPACE.md §5](../architecture/WORKSPACE.md) put the profiles beside the
  tools to prevent: a converter unpacked from a product finds nothing on its
  executable-relative search path and refuses every file it is given. A plain
  `cmake --install` is unaffected and does place them. This is BVH-3's remaining
  blocker and a v0.7.0 carry-over — it is an `ost` ask, not something a
  `--profile-dir` flag closes, because "works if you pass a flag naming a
  directory the artifact does not contain" is not an artifact-only smoke.
- The graph gate has nothing to say about any of that, and that is by design
  rather than a discovery gap like the adapter's: `--graph-only --json` names no
  tool at all, not even `motion_capture` or `motion_retarget`. Only packaging
  walks a tool descriptor.
- **The packaging order is load-bearing and easy to get backwards.** A root
  `ost build` rebuilds every bundle's library and invalidates the per-bundle
  managed-build provenance, so packaging straight after one fails closed with
  `PLUGIN_PACKAGE_OUTPUT_MISMATCH` on a digest that is not actually wrong. The
  order is `ost build` → `ost plugin build <bundle>` for each → `ost plugin
  package`.

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
4. ✅ `motionSource` skeleton and animation model — the source rig, the source
   animation, provenance, the single declared crossing into canonical motion,
   and a boundary check that fails on a producer name, a format name, a `Gf`
   type anywhere, or a canonical type outside that one crossing (2026-08-04)
5. ✅ the source profile contract — the vocabulary a profile states by name, the
   profile value and its invariants, the typed refusals that settle §10's
   diagnostic question, and matching a profile against a rig: a hierarchy
   embedding rather than a name lookup, returning facts a detector reports and
   never a score (2026-08-05)
6. ✅ the profile file and the first producer described by one — the keys and
   the small language they are written in, an unknown key refused rather than
   dropped, the shipped-profile check that keeps a file in `profiles/motion/`
   from being unloadable and unnoticed, and the check that holds a recorded file
   and a profile at once because neither library may (2026-08-05). One producer,
   not two: the export measured in BVH-0 is the only one anybody here has seen,
   and a second profile written from a file nobody has read would be the failure
   this plan is shaped around, wearing the shape of progress
7. ✅ the second producer's profile
   ([`bandai-namco-research-bvh-motiondataset-v1.yaml`](../../profiles/motion/bandai-namco-research-bvh-motiondataset-v1.yaml),
   2026-08-05) — written from two exports, one per half of the dataset, whose
   bytes this repository does not carry. It arrived third of three rather than
   first, and deliberately: its corpus rows landed with item 12, the two
   contract changes they raised landed next, and only then the file that depends
   on both. A profile written against a root the contract could not state would
   have been the feature arriving before the boundary. Its basis is measured
   rather than assumed — composing the rig forward from its own channels puts
   the head 47 units above the hips and every toe within 9 of zero, and the
   left shoulder at
   +X, which settles a forward axis that no `End Site` in this export could
   (they hold ±5 in one file and zeros in the other)
8. a DCC interoperability profile (optional)
9. ✅ BVH → canonical animation conversion, in two commits because it is two
   layers and the boundary between them is the point. **The extractor**
   (`motionBvh`) turns a document into `motionSource` values and is what took the
   declared edge — a channel set becomes a track, and nothing about a unit, an
   axis, a handedness or a bone travels with it; three shapes BVH permits and the
   value model does not are refused rather than reinterpreted. **The converter**
   (`motionSource`) is the change of basis as one signed permutation whose
   determinant is the handedness question, the intrinsic Euler composition, the
   path rule and the rest pose it also builds, the two root policies, and the
   four ways a conversion refuses. A test that holds a reader and a profile at
   once — which neither library may — drives the real export the whole way and
   checks every number against the `.bvh` text rather than against the converter
   (2026-08-05)
10. ✅ `motion_bvh_convert` — the composition point, and the first program
    anywhere that holds a reader and a profile at once. It resolves a profile
    **id** to a file (there is no default and no fallback, so a missing
    `--profile` is `VRM_BVH_PROFILE_REQUIRED` and stops the run), maps the
    typed `SourceProfileRefusal` onto the frozen semantic codes — which is where
    §10 said that mapping would live — and authors the clip with the rest pose
    the converter built. Exit status splits on *whose input was wrong*: 1 the
    recording, 2 the command or something it named. Its boundary is not the
    inspect tool's, and the check now says so per target: `--crossing` grants
    the stage and the humanoid vocabulary and still refuses `vrmRetarget` and
    `vrmSchema`, because the target avatar is the one thing this layer never
    binds to (2026-08-05)
11. ✅ the retarget end-to-end test — the recorded export through
    `motion_bvh_convert` and then an **unchanged** `motion_retarget` onto a
    target rig, checked through a `UsdSkelSkeletonQuery`. The fixture avatar is
    built so that a broken bake cannot pass it: its joints are named as a DCC
    names them so nothing binds by coincidence, its proportions differ, and its
    arms rest 45° down where the recorded rig's rest is straight — which makes
    the rest-pose correction a real rotation for four joints and identity
    elsewhere. The claim checked is the invariant the correction exists to
    hold, that a bone's world rotation *away from its own rest* is the same on
    both rigs; forcing the rest to identity fails it on exactly those four arms
    by exactly 45°. Also: the set of bones that move is preserved (this export
    never rotates its toes), root motion arrives as a displacement from the
    source's rest over the target's own hips height, and both tools are
    deterministic (2026-08-05)
12. 🚧 the recorded corpus and its manifest — the split, the manifest shape, the
    two checks over it, and the **first** producer export landed 2026-08-04,
    ahead of this position because a real file arrived and measuring it is
    BVH-0's whole content. The **second** producer's landed 2026-08-05: two
    Bandai Namco Research Motiondataset exports as rows with no bytes, the
    hierarchy promoted from prose to a measured field so a profile can be
    checked against a row rather than a file, one fetcher over both corpora
    where there were nearly two, and the fixture pair that keeps the absent-file
    path from passing everything ([§8](#8-corpus)). What still belongs here is
    everything that needs a profile to describe it — which is now blocked on two
    contract questions the export raised rather than on finding a producer
    ([§10](#10-contract-changes-this-plan-requires))

Every one of them checks: standalone build · dependency direction · no reverse
dependency · **no producer name in library code** · deterministic fixture tests ·
diagnostic stability · clean install.
