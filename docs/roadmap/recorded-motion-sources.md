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

**The second reader is now named, and it is NPZ / AMASS** ([§13](#13-the-next-format-family--npz--amass),
added 2026-08-29). It is the first real test of the claim this document's second
sentence makes, and it is scheduled after v0.8.0 — the format-neutral layer was
built for a second reader and has never had one.

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

The **custom YAML profile** is the one on that list whose subject is not this
repository's own work, so it is checked where a user actually stands rather than
in the library: `motion_bvh_convert_clip` writes a profile nobody here ships for
the generated four-joint rig no shipped profile matches, names it **by path with
no search directory**, and requires the clip to record the user's own id and
producer — then requires the same profile to be refused against a rig it does not
describe, so what passed is a match and not a path being trusted (2026-08-15).

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
| **BVH-0** — contract and fixtures | real samples from mocopi and a second producer; joints, hierarchy, channels, unit, axis measured; the `motionSource` model and profile schema settled; the diagnostic set frozen | ✅ |
| **BVH-1** — syntax | `BvhDocument`, the parser, `motion_bvh_inspect`, malformed fixtures, deterministic tests | ✅ |
| **BVH-2** — semantics | the `motionSource` API, the profile API, two producers' profiles, basis and unit conversion, source rest pose, root policy, `HumanoidAnimation`, the semantic clip writer | ✅ |
| **BVH-3** — end to end | `motion_bvh_convert`, the **unchanged** `motion_retarget`, the target VRM bake, the recorded corpus | 🚧 — only the **artifact-only smoke** is left, and it is unblocked ([§10](#10-contract-changes-this-plan-requires)) |
| **BVH-4** — cross-source | the same motion through UDP and BVH, compared at the canonical layer; the VMC relay added where available; a decision record | 🚧 — two paths of three compared ([report 01](../reports/motion/01-2026-08-15-mocopi-cross-source.md)); the relay is [current.md](current.md#carried-out-of-v070--evidence-an-operator-produces)'s |

The measurements below are what BVH-0 bought, and they are kept because the two
shipped profiles were written from them: a profile schema written from one
producer's file makes that producer's export the schema.

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

**What this track has already asked for is in the contracts, not repeated here.**
[WORKSPACE.md](../architecture/WORKSPACE.md) §1, §2 and §5 carry the
`motionSource` / `motionBvh` / CLI identities, the chain and its reversals, the
aggregate membership, and the profile destination; a producer profile being data
in which product names are permitted; and `tools/motionBvh/` as one member with
two executables. [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md) carries
`SourceProvenance` beside `MotionSourceMetadata`, the quaternion rotation form,
the canonical forward axis that nobody had written down until this track needed
it, the six semantic diagnostics being the caller's to raise from typed
refusals, a mapped bone's local rotation being the composition of the path above
it, a root that is two joints in one producer's export, and the rest-pose rules
that a first-frame rest failed twice — once against a second producer and once
against a real avatar. Each landed in its contract before the code that depends
on it, and the measurement behind each is in [§9](#9-milestones) or the report it
cites.

Still open:

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
- ✅ **The profiles reach a packaged product and the artifact-only smoke runs it**
  *(2026-08-30, `scripts/artifact_only_bvh_smoke.py`)*. Both halves of the
  staging closed first — the root project installs `profiles/motion/*.yaml` to
  `share/usd-vrm-plugins/profiles/motion/` (2026-08-05, verified against a
  scratch prefix), and `ost` 0.22.3's `[[workspace.install_data]]` gives the
  mapping a product-level owner, with the aggregate reporting `data_files: 3`
  ([report 36](../reports/ost/36-2026-08-25-v0.22.3-canonical-runtimes-and-release-membership.md) §4).
  Through v0.7.0 neither did: a `motion_bvh` archive was its two executables and
  its descriptor, so a converter unpacked from a product found nothing on its
  executable-relative search path and refused every file it was given — the
  specific consequence [WORKSPACE.md §5](../architecture/WORKSPACE.md) put the
  profiles beside the tools to prevent, and an `ost` ask rather than something a
  `--profile-dir` flag closes, because "works if you pass a flag naming a
  directory the artifact does not contain" is not an artifact-only smoke.

  **The smoke then failed for a second reason, which was ours.** The data
  arrived byte-identically and the converter still refused: an installed product
  puts a tool member at `<prefix>/tools/<member>/bin/`, and the locator's
  installed-prefix rule assumed `<prefix>/bin/`. So this entry closes on two
  fixes in different layers, and the sequence is the point — the packaging ask
  was real, closing it was not sufficient, and only running the thing
  distinguished the two. The passing run converts the committed 17-second mocopi
  export from the prefix alone: 853 frames at 50 Hz, 22 of 27 joints bound.

Two measured facts worth keeping, because both are easy to assume the other way:

- **The graph gate reaches both libraries and says nothing about data or tools.**
  `--graph-only` reported one more library and one more edge as each of
  `motionBvh` and `motionSource` was committed (`ost` 0.21.0, 2026-08-04/05), so
  a declared edge is validated rather than assumed — and with `profiles/motion/`
  committed it reports **the same** counts, because a directory of data is
  discovered as nothing. `--graph-only --json` names no tool at all, not even
  `motion_capture`; only packaging walks a tool descriptor. So whether these
  files reach an artifact is the item above's, not the gate's.
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

## 13. The next format family — NPZ / AMASS

*Added 2026-08-29, from the near-term plan. Not started, and deliberately not
designed yet.*

BVH proved the layering. The next recorded format family is NPZ / AMASS, and it
enters through the boundary that already exists rather than beside it:

```text
NPZ / AMASS
    │
 format reader          syntax and storage interpretation, and nothing else
    │
SourceSkeleton
SourceAnimation
SourceProvenance
    │
SourceProfile          producer semantics, declarative, data not code
    │
CanonicalConversion
    │
motion::HumanoidAnimation
```

### 13.1 What a reader is allowed to decide

The same list §2 gives `motionBvh`, restated because a new format is where it
gets tested: array layout, dtype, shape, key naming, chunking, and whatever a
container says about its own contents. Nothing else. **A reader does not
decide** the VRM target rig, the target rest pose, the retarget policy, USD
stage authoring, an OpenExec graph, or anything a vendor runtime would supply.

That list is what makes this a §13 rather than a new plan. If it holds, NPZ is a
reader and a profile; if it does not, the finding is more interesting than the
format.

### 13.2 One identity or two, decided by measurement

```text
motionNpz              — if a format-neutral boundary absorbs the AMASS contract
motionNpz + motionAmass — if it does not
```

`.npz` is a container (a zip of `.npy` arrays), and AMASS is a *convention* over
one — body model, shape parameters, pose parameters in an axis-angle
parameterisation, a frame rate, a gender field. Those are not the same kind of
thing, which is why the split is plausible; whether it is *necessary* depends on
how much of the AMASS convention survives being expressed as a `SourceProfile`
rather than as reader code.

**Measure a few real files first, then name the identity.** This is the same
rule that put `libs/osc`'s extraction after its second consumer, and the same
one that made the BVH corpus require two producers from the start: a boundary
settled before the measurement is a boundary shaped like whichever file happened
to arrive first. The BVH half already paid for this lesson in the other
direction — two producers disagreed about what a root joint is, which is exactly
what one producer could not have shown.

### 13.3 What has to be answered before any code

- Does a `SourceSkeleton` describe an AMASS body model at all, or does the model
  belong in a profile as a named rest-pose convention?
- Is the pose parameterisation a *storage* question (the reader's) or a
  *producer semantic* (the profile's)? BVH's answer was Euler-order-in-the-file
  → reader, basis-and-handedness → profile; AMASS's axis-angle rotations need
  the same line drawn explicitly rather than by analogy.
- Do shape parameters cross into canonical motion at all? A canonical
  `HumanoidAnimation` is avatar-independent, so a per-subject body shape is
  either provenance or it is out.
- What is the corpus, and what is redistributable? AMASS datasets carry research
  licences that differ per sub-dataset — the same gate the VRM corpus hit, and
  the [corpus policy](current.md#standing-corpus-policy--recorded-evidence-is-not-the-generated-corpus)
  already has the shape for it: a manifest with no bytes.

### 13.4 Non-goals, added to §11

- an SMPL / SMPL-X body model implementation, or any mesh deformation from shape
  parameters — this layer produces humanoid *motion*;
- a Python dependency at build or run time, for a format whose ecosystem is
  Python's;
- treating AMASS as the definition of the boundary, which is the failure mode
  the two-producer rule exists to prevent.
