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

```bash
ctest --test-dir <build> -R motion_retarget --output-on-failure
```
