# usd-vrm-plugins — VRM / VRMA / motion runtime architecture policy

> English translation of the project's forward motion-architecture policy. This
> is the canonical policy document for everything below the importer: `.vrma`
> import, the vendor-neutral motion core, retargeting, and the OpenExec runtime
> layer. [The roadmap](../roadmap/) tracks live status against it as **Motion
> Phase A–H** (§16).
>
> Section numbers are stable and match the source document, so the roadmap and
> the bundle docs can cite them (e.g. "motion policy §10"). It is a companion
> to [DESIGN_POLICY.md](DESIGN_POLICY.md), not a replacement: that document
> remains canonical for the importer, the schema contract, and Product P0–P6.
> Where the two overlap — runtime evaluation, OpenExec, Mocopi — **this document
> wins**, and DESIGN_POLICY §10 and §17-P4 carry forward-notes saying so.

## Naming deviations from the source

The source policy was written before the workspace split landed. Three names are
adapted here to the identities fixed by
[architecture/WORKSPACE.md](../architecture/WORKSPACE.md), which is binding for
structure:

| Source policy | Used here | Why |
| --- | --- | --- |
| a `usdVrm` bundle | `usdVrmFileFormat` | `usdVrm` was retired as a bundle id in Workspace Phase 4; it names the aggregate product only. |
| a `usdVrma` bundle | `usdVrmaFileFormat` | Symmetry with its sibling, and the same reason as above. |
| top-level `fixtures/` | per-bundle `tests/` + `tests/corpus/` | The repo already fixes fixture location per bundle; a second top-level convention would fragment it. |

Everything else — `motionCore`, `motionRuntime`, `vrmRetarget`, `execMotion`,
`execVrm`, `adapters/` — is adopted verbatim.

---

## 1. Purpose

Build a structure in which the following integrate loosely:

- `.vrm` avatar import into OpenUSD
- `.vrma` animation import into OpenUSD
- retargeting onto the VRM humanoid
- dynamic evaluation via OpenExec
- motion-capture input
- generative-AI motion synthesis
- offline bake and realtime playback
- future input methods, generators, and physics systems

The centre of the design is a **vendor-neutral Motion Core** — not Mocopi, not
ARDY, not any specific product or research implementation.

---

## 2. Base policy

### 2.1 VRM and VRMA are separate file-format plugins

`.vrm` and `.vrma` both sit on glTF/GLB, but they play different roles in USD.

| Input | Meaning | USD product |
| --- | --- | --- |
| `.vrm` | character asset | mesh, material, skeleton, humanoid, expression, look-at, spring-bone metadata |
| `.vrma` | reusable motion | humanoid animation, expression animation, look-at animation |
| runtime / exec | application + evaluation | retarget, blend, root motion, expression, look-at, simulation |

Recommended structure:

```text
plugins/
├─ usdVrmFileFormat/
├─ usdVrmaFileFormat/
├─ vrmSchema/
├─ execMotion/
└─ execVrm/
```

`.vrma` reading is **not** added to `usdVrmFileFormat`. It is an independent
bundle with a symmetric structure.

```text
.vrm
→ VrmDocumentReader
→ VrmCanonicalDocument
→ UsdVrmAuthorer
→ Asset USD
```

```text
.vrma
→ VrmaDocumentReader
→ HumanoidAnimation
→ UsdVrmaAuthorer
→ Motion USD
```

---

## 3. USD composition policy

### 3.1 Naive subLayer composition of VRM and VRMA is not the base form

Composition like this cannot express the application relationship between
character and motion:

```usda
#usda 1.0
(
    subLayers = [
        @avatar.vrm@,
        @walk.vrma@
    ]
)
```

The VRM's `/Asset` and the VRMA's `/Animation` merely appear on the same stage.
All of the following stay unresolved:

- which skeleton the VRMA applies to
- the correspondence between humanoid semantics and real joint paths
- the difference between source and target rest pose
- how root motion is handled
- expression target resolution
- look-at target resolution

And when several VRMAs share a prim path, subLayer strength makes conflicts
likely.

### 3.2 VRM and VRMA are placed by reference

Recommended structure:

```text
shot.usda
├─ /World/Character
│  └─ references avatar.vrm
├─ /World/Motions/Walk
│  └─ references walk.vrma
└─ /World/Bindings/CharacterWalk
   └─ holds target, source motion, retarget policy
```

Example:

```usda
#usda 1.0

def Xform "World"
{
    def Xform "Character" (
        references = @avatar.vrm@</Asset>
    )
    {
    }

    def Scope "Motions"
    {
        def Scope "Walk" (
            references = @walk.vrma@</Animation>
        )
        {
        }
    }
}
```

### 3.3 A third binding / assembly layer

Base asset split:

```text
avatar.vrm              character original
walk.vrma               motion original
character_walk.usda     retarget result for one specific character
shot.usda               placement and binding
```

The VRMA original and the `UsdSkelAnimation` converted for a target skeleton
stay separate:

```text
/World/Motions/Walk
= the original, holding the VRMA's humanoid semantics

/World/Character/skel/Walk_Retargeted
= a derivative matched to the target skeleton's joint order and rest pose
```

---

## 4. VRMA USD authoring

### 4.1 Recommended prim structure

```text
/Animation
├─ HumanoidSkeleton
├─ BodyAnimation
├─ Expressions
├─ LookAt
└─ Metadata
```

Body motion uses standard USD as far as possible:

```text
body motion
→ UsdSkelAnimation
```

VRM-specific meaning is held in schemas or namespaced attributes:

```text
expression
→ VrmAnimationExpressionAPI (equivalent)

look-at
→ VrmAnimationLookAtAPI (equivalent)
```

### 4.2 HumanoidSkeleton is a canonical semantic skeleton

`HumanoidSkeleton.joints` does **not** carry the target VRM's real joint paths.
It carries the standard humanoid semantic ordering:

```text
hips
spine
chest
upperChest
neck
head
leftShoulder
leftUpperArm
leftLowerArm
leftHand
...
```

The VRMA file-format plugin is independent of any target avatar.

### 4.3 What the file-format plugin does not do

`usdVrmaFileFormat` does **not**:

- search for a target VRM
- bind directly to a target skeleton
- correct for proportion or rest-pose differences
- expand expressions into blend shapes
- apply look-at to eyes or neck
- simulate spring bones
- blend poses at runtime

Those belong to `vrmRetarget`, `execMotion`, `execVrm`, or a bake tool. This is
the same import/evaluation boundary the importer already holds
([DESIGN_POLICY §10](DESIGN_POLICY.md#10-runtime-semantics--openexec)), applied
to motion.

---

## 5. Motion Core

### 5.1 A vendor-neutral intermediate representation

Specific inputs — Mocopi, generative AI, VRMA — are never exposed in the core
API.

Recommended namespace:

```text
motion
```

Recommended types:

```cpp
motion::HumanoidPose
motion::HumanoidAnimation
motion::RootMotion
motion::MotionConstraintSet
motion::MotionGenerationRequest
motion::MotionGenerationResult
motion::SourceMetadata
```

"Canonical" is defined as a documented property, and may be omitted from public
type names.

### 5.2 HumanoidPose

```cpp
struct RootMotion {
    GfVec3f worldPosition;
    GfQuatf worldOrientation;

    GfVec3f linearVelocity;
    GfVec3f angularVelocity;

    bool hasPosition = false;
    bool hasOrientation = false;
    bool hasLinearVelocity = false;
    bool hasAngularVelocity = false;
};

struct HumanoidPose {
    double timestamp;

    RootMotion root;

    std::array<GfQuatf, HumanBoneCount> localRotations;
    std::bitset<HumanBoneCount> validRotations;

    std::optional<std::array<float, HumanBoneCount>> confidence;
    std::optional<ContactState> contacts;
};
```

Load-bearing points:

- separate root motion from body pose
- never conflate the hips local transform with the character world transform
- missing bones must be representable
- timestamps are seconds-based, not integer frames
- confidence, contact, and source provenance must be addable later

### 5.3 HumanoidAnimation

```cpp
struct HumanoidAnimation {
    std::vector<HumanoidPose> samples;
    double startTime;
    double endTime;
    double nominalFrameRate;
};
```

The VRMA reader produces a `HumanoidAnimation`. Live input produces
`HumanoidPose` incrementally, and a recorder accumulates it into a
`HumanoidAnimation` when needed.

---

## 6. Motion source and motion generator

### 6.1 Observation / playback

```cpp
class IMotionSource {
public:
    virtual ~IMotionSource() = default;

    virtual PoseSampleResult Sample(
        double evaluationTime) = 0;
};
```

Implementations:

```text
VrmaClipSource
RecordedClipSource
LiveCaptureSource
GeneratedClipSource
```

### 6.2 Generation

```cpp
class IMotionGenerator {
public:
    virtual ~IMotionGenerator() = default;

    virtual MotionGenerationResult Generate(
        const MotionGenerationRequest& request) = 0;
};
```

A generation result is a `HumanoidAnimation`, so downstream it becomes just
another `GeneratedClipSource`:

```text
IMotionGenerator
→ HumanoidAnimation
→ GeneratedClipSource
→ IMotionSource
```

From retarget onward, VRMA, motion capture, and generative AI need no
distinguishing.

---

## 7. Motion constraints

To accommodate generative and procedural motion, pose and constraint are
separate types.

```cpp
struct MotionConstraintSet {
    std::vector<RootWaypoint> rootWaypoints;
    std::vector<RootTrajectorySample> rootTrajectory;

    std::vector<FullBodyKeyframe> keyframes;
    std::vector<JointPositionConstraint> jointPositions;
    std::vector<JointRotationConstraint> jointRotations;

    std::optional<std::string> textPrompt;
};
```

Every constraint carries:

```text
target time
joint semantic
coordinate space
position / rotation
weight
hard / soft
valid interval
source provenance
```

Coordinate spaces to support:

```text
world
character
skeleton
joint local
```

Future-time constraints, sparse joint constraints, root waypoints, and full-body
keyframes are all permitted.

---

## 8. Where motion capture and generative AI sit

### 8.1 Product names are confined to adapters

Core code, shared schemas, the retarget API, and shared OpenExec nodes never
mention:

```text
Mocopi
ARDY
any specific SDK name
any specific research model name
```

Product names are permitted only in:

```text
adapters/
tests/integration/
examples/
provider metadata
optional plugin bundles
```

### 8.2 Motion-capture adapter

```text
motion capture system
→ protocol decode
→ coordinate conversion
→ LiveCaptureSource
→ HumanoidPose
```

Mocopi is one concrete adapter:

```text
adapters/liveCapture/mocopi/
```

### 8.3 Generative-AI adapter

```text
text / trajectory / sparse constraints / pose history
→ motion generator adapter
→ HumanoidAnimation
```

ARDY is one concrete adapter:

```text
adapters/generators/ardy/
```

The core deals only in general concepts:

```text
MotionGenerator
MotionConstraintSet
PoseHistory
GeneratedMotionSource
```

---

## 9. Source metadata

Input provenance is metadata — never a type distinction or a behavioral
contract.

```cpp
enum class MotionSourceKind {
    Clip,
    LiveCapture,
    Generated,
    Procedural,
    Simulated
};

struct MotionSourceMetadata {
    MotionSourceKind kind;
    std::string provider;
    std::string protocol;
    std::string sourceId;
};
```

Examples:

```text
kind = LiveCapture
provider = "sony.mocopi"
protocol = "udp"
```

```text
kind = Generated
provider = "nvidia.ardy"
```

`provider` is provenance. It is never a branch condition in core logic.

---

## 10. Retarget core

### 10.1 An OpenExec-independent library

Recommended structure:

```text
libs/vrmRetarget/
├─ HumanoidMap
├─ RestPose
├─ PoseRetargeter
├─ RootMotionPolicy
├─ ExpressionResolver
└─ RetargetResult
```

Dependencies:

```text
motionCore
    ↑
vrmRetarget
    ↑
bake tool / execVrm
```

### 10.2 Minimum functionality

Initial:

- humanoid semantic → target joint mapping
- source/target rest-pose difference correction
- joint rotation conversion
- hips / root translation
- expansion into the target skeleton's joint order
- bake to `UsdSkelAnimation`

Later:

- root-motion policy
- scale / body-proportion correction
- expression
- look-at
- motion blending
- contact-aware retarget
- IK / foot locking

### 10.3 Root-motion policy

Root motion is first-class data. Candidates:

```text
ignore
applyToCharacter
applyToSkeletonRoot
extractToSeparatePrim
inPlace
```

The same policy set must work for VRMA, live input, and generated motion alike.

---

## 11. The role of OpenExec

### 11.1 execMotion

An input- and processing-agnostic motion runtime.

```text
Motion.ClipSample
Motion.LivePoseReceive
Motion.PoseBuffer
Motion.Resample
Motion.Filter
Motion.Blend
Motion.ApplyConstraints
Motion.Generate
Motion.Record
```

### 11.2 execVrm

VRM semantics and application to a target rig.

```text
Vrm.HumanoidRetarget
Vrm.RootMotionResolve
Vrm.ExpressionResolve
Vrm.LookAtEvaluate
Vrm.AvatarApply
```

### 11.3 Adapter-specific nodes

Early implementations may put product-specific nodes in an optional bundle:

```text
Adapters.Mocopi.Receive
Adapters.Mocopi.Decode

Adapters.Ardy.Generate
```

But the shared runtime API stays limited to:

```text
Motion.LivePoseReceive
Motion.Generate
```

A provider-registry approach is a future option.

---

## 12. Separating live evaluation from USD authoring

### 12.1 Do not rewrite the USD stage every frame

Avoid:

```text
live packet
→ USD attribute Set
→ SdfChangeBlock
→ composition notification
```

Live:

```text
motion source
→ pose buffer
→ motion runtime
→ retarget
→ transient pose
→ renderer / deformer
```

Record / publish:

```text
pose buffer
→ resample
→ HumanoidAnimation
→ UsdSkelAnimation
→ USDA / VRMA
```

### 12.2 PoseBuffer

Live input arrives with jitter, so a timestamped buffer sits in front of
evaluation:

```text
network receive
→ timestamped pose buffer
→ interpolation / extrapolation
→ requested evaluation time
→ HumanoidPose
```

---

## 13. Motion plans on the USD stage

For generative workflows, the stage must be able to hold motion *intent* and
constraints, not only finished animation.

```text
/World/MotionPlans/WalkToChair
├─ prompt
├─ rootWaypoints
├─ rootTrajectory
├─ targetKeyframes
├─ jointConstraints
├─ targetAvatar
└─ generatedAnimation
```

Example:

```usda
def Scope "MotionPlan"
{
    uniform token motion:sourceKind = "generated"
    uniform string motion:provider = "example.generator"

    string motion:prompt = "walk to the chair and sit"

    point3f[] motion:rootWaypoints = [
        (0, 0, 0),
        (1, 0, 2),
        (2, 0, 3)
    ]

    double[] motion:rootWaypointTimes = [0, 1.5, 3.0]

    rel motion:targetAvatar = </World/Character>
    rel motion:generatedAnimation = </World/Animations/Generated>
}
```

Model-specific latent representations are never part of a shared USD schema.

---

## 14. Recommended repository layout

```text
libs/
├─ vrmContainer/
├─ motionCore/
├─ motionRuntime/
└─ vrmRetarget/

plugins/
├─ vrmSchema/
├─ usdVrmFileFormat/
├─ usdVrmaFileFormat/
├─ execMotion/
└─ execVrm/

adapters/
├─ liveCapture/
│  └─ mocopi/
└─ generators/
   └─ ardy/

tools/
├─ vrma_inspect
├─ motion_retarget
├─ motion_bake
└─ motion_record
```

Fixtures follow the existing per-bundle convention (`<bundle>/tests/`,
`<bundle>/tests/corpus/`) rather than a new top-level `fixtures/` tree — see the
deviations table above.

---

## 15. Dependency directions

```text
vrmContainer
    ↑
usdVrmFileFormat / usdVrmaFileFormat

motionCore
    ↑
motionRuntime
    ↑
vrmRetarget
    ↑
execMotion / execVrm
    ↑
optional adapters
```

Forbidden:

```text
motionCore        → vendor SDK
motionCore        → Mocopi
motionCore        → ARDY
vrmRetarget       → network protocol
usdVrmaFileFormat → live receiver
usdVrmFileFormat  → motion generator
execVrm           → GLB parser
```

Adapters depend on the core. The core never depends on an adapter.

These edges are normative and belong in
[architecture/WORKSPACE.md §2](../architecture/WORKSPACE.md), which is what CI
enforces via `ost plugin test --workspace`.

---

## 16. Implementation roadmap

> Live status is tracked in [the roadmap](../roadmap/) as **Motion Phase A–H** —
> always qualified, never a bare "Phase A". This is the third and last sequence
> in the repo, alongside Product P0–P6 and Workspace Phase 0–6; see
> [roadmap/README.md](../roadmap/README.md#three-sequences-deliberately-separate).

### Motion Phase A: freeze the contract

- hand-author the ideal VRMA→USDA conversion
- define `motion::HumanoidPose`
- define `motion::HumanoidAnimation`
- make `RootMotion` an independent type
- define `MotionConstraintSet`
- write down the source/target coordinate spaces

Deliverables:

```text
canonical_walk.usda
avatar.usda
expected_retargeted.usda
```

### Motion Phase B: minimal `usdVrmaFileFormat`

- `.vrma` file-format plugin
- GLB / glTF animation read
- humanoid rotation
- hips translation
- canonical `HumanoidSkeleton`
- `UsdSkelAnimation`
- time range
- provenance metadata

Not covered: expression, look-at, advanced retarget, live evaluation.

### Motion Phase C: offline retarget

- `vrmRetarget` library
- CLI bake tool
- read the target VRM's humanoid mapping
- rest-pose correction
- expansion to target joint order
- `UsdSkelAnimation` output
- `skel:animationSource` binding

```bash
motion_retarget \
  --avatar avatar.vrm \
  --animation walk.vrma \
  --output character_walk.usda
```

**This is the first end-to-end evaluation point.**

### Motion Phase D: live-capture adapter prototype

- generic `LiveCaptureSource`
- timestamped `PoseBuffer`
- reproducible tests using recorded samples / a replay sender
- feeds the same retarget core as VRMA
- evaluation of missing bones, confidence, root motion

Product-specific support ships as an optional adapter.

### Motion Phase E: `execMotion` / `execVrm`

- ClipSample
- PoseBuffer
- HumanoidRetarget
- RootMotionResolve
- AvatarApply
- runtime evaluation
- transient application of live poses

OpenExec nodes are thin wrappers over `motionRuntime` and `vrmRetarget`.

### Motion Phase F: generation adapter

- `IMotionGenerator`
- `MotionGenerationRequest`
- text intent
- root waypoints
- sparse joint constraints
- pose history
- clip-ification of generated animation
- optional generation adapter

Generator-specific models, services, and dependencies stay inside the adapter.

### Motion Phase G: expression / look-at / recording

- VRMA expression animation
- VRMA look-at animation
- `ExpressionResolve`
- `LookAtEvaluate`
- live recording
- `UsdSkelAnimation` bake
- VRMA export investigation

### Motion Phase H: advanced

- motion blending
- IK / foot locking
- contact handling
- latency compensation
- multi-performer synchronization
- simulation bridge
- physical tracking
- generated-motion cache
- publish pipeline

---

## 17. Initial acceptance criteria

### VRMA

- humanoid bone correspondence is correct
- quaternions and coordinate conversion are correct
- hips translation is correct
- the time range is correct
- it can be retargeted to a target skeleton
- a baked result plays back in a stock USD environment

### Live capture

- timestamp and evaluation time are separate
- missing bones do not break it
- the buffer absorbs packet jitter
- root motion is stable
- it uses the same retarget core as VRMA

### Generation

- pose and constraint are separate
- future-time constraints are expressible
- root trajectory is separate from body pose
- results are storable as a `HumanoidAnimation`
- swapping the generator does not change anything downstream

---

## 18. Key design decisions

1. `.vrm` and `.vrma` are separate file-format plugins.
2. VRM and VRMA compose by reference, not subLayer.
3. A third binding / assembly layer relates them.
4. The VRMA original and retargeted animation stay separate.
5. File-format plugins never evaluate, simulate, or retarget.
6. Motion Core depends on neither VRM nor any product name.
7. Root motion is separate from body pose.
8. Motion source and motion generator are different interfaces.
9. Pose and motion constraint are separate.
10. Mocopi, ARDY, and the like are optional adapters.
11. Product names are confined to provider metadata and adapter implementations.
12. **The OpenExec-independent retarget core is completed before OpenExec.**
13. Live playback never rewrites the USD stage per frame.
14. USD animation is authored only on bake / record / publish.
15. VRMA, live capture, and generative AI converge on one humanoid motion
    pipeline.

---

## 19. Final architecture

```text
VRMA File
    ↓
ClipMotionSource ───────────────────┐

Motion Capture Adapter              │
    ↓                               │
LiveCaptureSource ──────────────────┤

Generation Adapter                  │
    ↓                               │
MotionGenerator                     │
    ↓                               │
GeneratedClipSource ────────────────┘
                    ↓
             motion::HumanoidPose
             motion::HumanoidAnimation
                    ↓
        Buffer / Resample / Filter / Blend
                    ↓
              vrmRetarget
                    ↓
               execVrm
                    ↓
                VRM Avatar
```

USD asset composition:

```text
avatar.vrm
    ─reference─┐

walk.vrma
    ─reference─┤
               ├─ shot / assembly layer
retargeted.usda┤
    ─reference─┘
```

This keeps the VRM↔VRMA relationship intact while leaving room to add motion
capture, generative AI, procedural motion, and physical simulation to the same
execution substrate later.
