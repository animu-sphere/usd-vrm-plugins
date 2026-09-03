# motion_retarget

Motion Phase C's bake tool: it takes a semantic humanoid clip and a target rig
and writes a `UsdSkelAnimation` in that rig's joint order, bound to it.

```bash
motion_retarget \
  --avatar avatar.vrm \
  --animation walk.vrma \
  --output character_walk.usda
```

`.vrm` and `.vrma` inputs need `usdVrmFileFormat` / `usdVrmaFileFormat`
registered; any layer OpenUSD can open works, so a plain `.usda` rig and clip
need no plugin at all.

[docs/guides/VIEWING_MOTION.md](../../docs/guides/VIEWING_MOTION.md) walks the
whole path end to end — registering the bundles, opening the result in
`usdview`, composing a whole motion pack into one stage, and checking that the
rig is actually driven rather than sitting at rest.

## The stage boundary lives here

This is the motion layer's **only** stage-aware component. `vrmRetarget`,
`motionRuntime`, and `motionCore` take and return plain values and never open a
stage ([WORKSPACE.md](../../docs/architecture/WORKSPACE.md) §2). The split is
what lets `execVrm` wrap the retarget core later instead of reimplementing it,
and what lets a future live-capture source reuse it with no stage at all.

It is an **executable, not a bundle**: no `openstrata.plugin.yaml`, no
`plugInfo.json`, nothing registered with OpenUSD.

## Options

| Flag | Meaning |
| --- | --- |
| `--avatar PATH` | Target rig. Required. |
| `--animation PATH` | Source clip. Required. |
| `--output PATH` | Layer to author. Overwrites an existing one. Required. |
| `--humanoid-map PATH` | JSON `{"hips": "Root/Pelvis", …}`. Required when the avatar carries no `VrmHumanoidAPI`; merged over it when it does. |
| `--skeleton PRIMPATH` | Target `UsdSkelSkeleton`, when auto-detection finds the wrong one. |
| `--clip-skeleton PRIMPATH` | Source semantic skeleton, likewise. |
| `--root-motion hips\|root\|ignore` | Where root motion lands. Default `hips`. |
| `--root-joint TOKEN` | Receiving joint under `--root-motion root`. |
| `--translation-scale N` | Scale on the root translation delta. |
| `--preserve-target-height` | Take only the horizontal delta; keep the rig's rest height. |
| `--resample HZ` | Resample onto a uniform timeline first. |
| `--animation-name NAME` | Prim name for the authored animation. Default `RetargetedAnimation`. |
| `--no-expressions` | Bake the body only; resolve no expression weights. |
| `--no-look-at` | Bake without a gaze; leave the eyes where the rig rests them. (`--no-expressions` also suppresses an *expression*-driven gaze.) |
| `--quiet` | Suppress diagnostics on stderr. |

## What it authors

The output layer **references** the avatar and adds two things: the animation,
and an `over` on the skeleton that binds it. The avatar keeps owning its own
rig — nothing is copied.

```usda
def "Avatar" (
    prepend references = @./avatar.usda@
)
{
    def SkelAnimation "RetargetedWalk" { … }

    over "TargetSkeleton"
    {
        rel skel:animationSource = </Avatar/RetargetedWalk>
    }
}
```

This requires the target skeleton to sit under the avatar's `defaultPrim`; the
tool says so and exits non-zero when it does not.

### The face, when both sides have one

A clip carries expression weights by name and an avatar carries the binds those
names mean — N morph targets across M meshes, plus material colours. When both
are present the bake resolves one against the other
([`vrmRetarget::ExpressionResolver`](../../libs/vrmRetarget/README.md)) and
authors the result as `blendShapes` and `blendShapeWeights` on the same
`SkelAnimation` the joints are on:

```usda
def SkelAnimation "RetargetedWalk"
{
    uniform token[] blendShapes = ["Face_Blink", "Face_Brow", "Face_Smile"]
    float[] blendShapeWeights.timeSamples = {
        0: [0, 0, 0],
        15: [1, 0.25, 0.5],
    }
    …
}
```

Nothing is authored on the meshes. UsdSkel carries blend-shape weights on the
animation the skeleton is bound to and hands each skinned prim the subset its
own `skel:blendShapes` names, so the avatar keeps owning its binds exactly as it
keeps owning its rig. Four consequences are worth knowing:

- **A blend shape is named by the token its mesh binds it under**, not by its
  prim path — that is the join UsdSkel performs. A blend shape *no* mesh binds
  cannot be reached from an animation at all, so a weight resolved onto one is
  reported and not authored.
- **An expression key is a sample.** Expressions live on the pose, so a blink
  keyed between two body keys adds that instant to the bake and the joints are
  read there too. `--resample` therefore resamples both halves at once.
- **A weight the clip never states holds.** A `.vrma` expression prim that
  declares a name and authors no weight is not a zero — nothing is authored for
  its targets — and a sample that says nothing (a USD value block) leaves the
  previous weight standing rather than dropping it to zero.
- **Material colours are resolved and not authored.** A colour slot is a
  material input and the material layer owns what an MToon or a
  `UsdPreviewSurface` calls it, so the bake reports how many slots the clip
  drives on this rig instead of writing them.

`--no-expressions` skips all of it and bakes the body alone.

### The gaze, which depends on the rig twice over

A clip names a *place* the character is looking at, not a direction, because a
direction is only meaningful next to a head and where the head sits belongs to
the avatar. So the bake evaluates the clip's `vrm:lookAtTarget` against the
avatar's own `/Asset/rig/LookAt`
([`vrmRetarget::LookAtEvaluator`](../../libs/vrmRetarget/README.md)) — the head
transform the body half just produced, plus the rig's `offsetFromHeadBone`, its
four range-map curves and its type.

**The type decides how the answer reaches the stage**, and the two are not a
spelling difference:

- a **`bone`** rig is aimed through its eye joints. The rotations are written
  into the very arrays the body was expanded into, so they reach the stage
  through the joint authoring that already exists. The eye on the side the gaze
  goes to takes the *outer* range map and the other takes the *inner* one,
  because two eyes converge. The eyes need not be in the humanoid map: a VRM
  names them through its look-at configuration, and that is where these come
  from.
- an **`expression`** rig is aimed through `lookLeft`, `lookRight`, `lookUp` and
  `lookDown`. Those are expressions of the rig exactly as `happy` is, so they
  are folded into the sample's own weights *before* the expression resolve and
  land in `blendShapeWeights` through the same accumulator — not through a
  second path into the same blend shapes. All four are stated every sample,
  including the two a given gaze drives to zero, or a swing to the left would
  leave the previous sample's `lookRight` standing.

Three more things worth knowing:

- **A gaze the clip never named is not a gaze forward, and one it stops naming
  holds.** Until the clip gives a first target the eyes stay where the retarget
  put them; after that, a sample that says nothing — a USD value block — leaves
  the last gaze standing rather than snapping back to rest, which is the rule a
  blocked expression weight is already under. The two rig types have to agree
  about it: an expression gaze reaches the stage as a fixed-width array that
  holds by construction and a bone gaze as a per-sample joint array that does
  not.
- **A channel the gaze overwrites is named.** An eye is a human bone like any
  other, so a rig that binds `leftEye` in its humanoid map and a clip that
  animates it produce a rotation the gaze replaces. The gaze wins — it is the
  value this rig's own curves produced — and the bone it displaced is reported,
  as is a clip that drives a gaze *expression* by name while also naming a
  target.
- **The clip's `vrm:lookAtOffsetFromHeadBone` is a fallback, not the
  measurement.** It describes the rig the clip was authored on, so it is used
  only when the avatar states none of its own, and the substitution is reported.
- **VRM 0.x and VRM 1.0 state the same four curves in two shapes**, and the
  reader turns both into one value, so nothing above this layer learns which
  version the rig came from.

`--no-look-at` skips all of it and leaves the eyes at rest. So does
`--no-expressions` **on an expression-driven rig**, and for a reason rather than
as a side effect: those four weights reach the stage as blend-shape weights and
by no other route, so the flag that refuses to author blend-shape weights
refuses them too. The run says so rather than silently counting a gaze it did
not write.

## Diagnostics you should not ignore

Bones the clip drives but the rig does not bind, required VRM bones with no
binding, two bones resolving to one joint, and a skeleton whose joints are not
in parent-before-child order are all reported on stderr. Retargeting onto a
partial rig is legal and useful; doing it silently is not.

The same holds for the face: expressions the clip animates and this avatar does
not declare, weights clamped from outside `[0, 1]`, blend shapes no mesh binds,
and material colour slots the bake resolves and does not write are each named
rather than counted. A clip is authored against no avatar in particular, so none
of them is an error — but a whole expression track going missing without a line
of output is how a bake looks correct and drives nothing.

## Testing

`tests/test_motion_retarget.py` bakes the frozen design triplet
(`docs/design/fixtures/motion/`) and compares the result with
`expected_retargeted.usda` at the value level, then exercises
`--root-motion ignore`, `--resample`, re-baking over an existing output, and the
two documented failure modes.

It then bakes `tests/fixtures/expressive_{avatar,clip}.usda` — the same
skeleton, with a face on it — and checks the expression half through UsdSkel's
own queries rather than the attributes just written: the animation's blend-shape
order, the weights it resolves, and what the face mesh's blend-shape mapper
makes of them in its own order. Each fixture expression carries one decision:
two morph targets at different bind weights, a binary eyelid, a name the clip
never weights, a material colour with no morph target, and a blend shape no mesh
binds.

Then `tests/fixtures/gazing_{avatar,clip}.usda` and
`gazing_expression_avatar.usda`: one clip baked onto two rigs that aim their
eyes differently. The four range maps are given four *different* output scales
(10 outer, 5 inner, 12 up, 6 down) so that reading one map for both eyes, or
swapping inner for outer, fails rather than merely looks plausible; and the
authored eye rotation is checked by turning the forward axis *with* it, so a
bake that inverted both of its conventions cannot pass by agreeing with a test
that reproduced them. Four more cases cover what the gaze *displaces* and what
suppresses it: a blocked target that must hold, an eye the clip itself animates,
a gaze expression the clip also drives by name — each reported exactly once,
however many samples ran into it — and `--no-expressions` on an
expression-driven rig.

```bash
ctest --test-dir <build> -R motion_retarget --output-on-failure
```
