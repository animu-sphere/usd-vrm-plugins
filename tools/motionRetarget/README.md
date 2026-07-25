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

## Diagnostics you should not ignore

Bones the clip drives but the rig does not bind, required VRM bones with no
binding, two bones resolving to one joint, and a skeleton whose joints are not
in parent-before-child order are all reported on stderr. Retargeting onto a
partial rig is legal and useful; doing it silently is not.

## Testing

`tests/test_motion_retarget.py` bakes the frozen design triplet
(`docs/design/fixtures/motion/`) and compares the result with
`expected_retargeted.usda` at the value level, then exercises
`--root-motion ignore`, `--resample`, re-baking over an existing output, and the
two documented failure modes.

```bash
ctest --test-dir <build> -R motion_retarget --output-on-failure
```
