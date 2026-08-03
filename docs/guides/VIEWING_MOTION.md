# Playing a `.vrma` clip on a `.vrm` avatar

How to go from an avatar file and a motion file to an animated character in
`usdview`, and how to tell whether it is actually animating rather than merely
looking like it should.

Neither file format plays anything on its own. A `.vrm` composes a rig in its
rest pose; a `.vrma` composes a `UsdSkelAnimation` on the *canonical semantic*
humanoid skeleton, which is not the avatar's skeleton and never shares its joint
names. [`motion_retarget`](../../tools/motionRetarget/README.md) is what joins
them: it bakes the clip into the target rig's joint order and binds it.

## What you need

A built workspace and an adopted `cy2026` runtime — see the repo
[README](../../README.md#build-and-test). `ost build` puts the tool at
`tools/motionRetarget/bin/motion_retarget.exe` (`motion_retarget` elsewhere).

## 1. Bake the clip onto the rig

Reading `.vrm` and `.vrma` needs the plugin bundles registered, so the bake runs
inside a runtime session:

```sh
ost plugin run plugins/usdVrmFileFormat \
    --with plugins/vrmSchema \
    --with plugins/usdVrmPackageResolver \
    --with plugins/usdVrmaFileFormat \
    -- tools/motionRetarget/bin/motion_retarget \
       --avatar    avatar.vrm \
       --animation clip.vrma \
       --output    avatar_clip.usda
```

It reports what it did, and the numbers are worth reading:

```text
motion_retarget: wrote avatar_clip.usda (577 samples over 128 joints,
                 53 humanoid bones bound)
```

`53 humanoid bones bound` is the load-bearing one. A rig that mapped only a
handful still writes a file and still opens.

A VRM 0.x rig draws a warning naming bones the clip drives that it does not
have — `leftThumbMetacarpal`, `rightThumbMetacarpal` are VRM 1.0 additions, and
losing them is expected rather than a failure.

## 2. Open it

`view` is the exception to `ost`'s dependency composition: it loads only what
`--with` names, so every bundle must be spelled out.

```sh
ost plugin view plugins/usdVrmFileFormat avatar_clip.usda \
    --with plugins/vrmSchema \
    --with plugins/usdVrmPackageResolver \
    --with plugins/usdVrmaFileFormat
```

Press play. The frame range comes from the clip.

## 3. Several clips at once

The baked layer references the avatar rather than copying it, so referencing
*it* in turn composes the rig once per slot. Give each slot a translation and a
whole motion pack fits in one stage:

```usda
#usda 1.0
(
    defaultPrim = "MotionPack"
    startTimeCode = 0
    endTimeCode = 354
    timeCodesPerSecond = 30
    metersPerUnit = 1
    upAxis = "Y"
)

def Xform "MotionPack"
{
    def "VRMA_01" (
        prepend references = @./avatar_clip01.usda@
    )
    {
        double3 xformOp:translate = (-4.5, 0, 0)
        uniform token[] xformOpOrder = ["xformOp:translate"]
    }

    # ... one per clip, 1.5 apart
}
```

The slot prims are **typeless on purpose**. The referenced `defaultPrim` is a
`SkelRoot`; declaring `def Xform` locally would override that type and skinning
would stop resolving. `SkelRoot` is itself transformable, so the `xformOp`
applies either way — which is what makes the mistake quiet.

`metersPerUnit` is not decoration either. Stage metrics come from the root
layer alone and do **not** compose through a reference, so this layer has to
repeat what the baked ones declare. Omit it and the pack resolves to USD's
default of `0.01`, describing a row of 1.6 cm characters — and rendering them
perfectly, because the scale is self-consistent until something that declares
its units honestly joins the stage. `motion_retarget` carries the avatar's
metrics onto its own output for the same reason; before v0.7.0 it authored
neither ([#89](https://github.com/animu-sphere/usd-vrm-plugins/issues/89)).

Set `endTimeCode` to the longest clip; shorter ones hold their last pose.

## Checking that it animates

A bound animation and an ignored one look identical in the layer text and differ
only in what a consumer resolves. UsdSkel answers the question directly:

```python
from pxr import Usd, UsdSkel

stage = Usd.Stage.Open("avatar_clip.usda")
root = UsdSkel.Root.Find(stage.GetDefaultPrim())
cache = UsdSkel.Cache()
cache.Populate(root, Usd.TraverseInstanceProxies())

for binding in cache.ComputeSkelBindings(root, Usd.TraverseInstanceProxies()):
    query = cache.GetSkelQuery(binding.GetSkeleton())
    assert query.GetAnimQuery(), "nothing drives this skeleton"
    assert (list(query.ComputeJointLocalTransforms(0))
            != list(query.ComputeJointLocalTransforms(120))), "rest pose"
```

Both assertions exist because both failures are silent. UsdSkel resolves the
rest pose — a full-length, entirely valid array of transforms — whenever the
binding does not take, so a value check on the authored attributes passes while
the character stands still. Two ways to land there:

- **`skel:animationSource` without `SkelBindingAPI` applied.** The relationship
  has the right target and UsdSkel ignores it. This was a `motion_retarget` bug;
  it is fixed, and the regression case bakes onto a rig with the schema stripped
  because a rig that already has it cannot detect the difference.
- **A `SkelAnimation` with no `scales`.** UsdSkel fetches translations,
  rotations and scales as a unit, and `scales` has no schema fallback.

## Checking that it is the right size

```python
from pxr import Usd, UsdGeom

stage = Usd.Stage.Open("avatar_clip.usda")
assert UsdGeom.GetStageMetersPerUnit(stage) == 1.0
assert UsdGeom.GetStageUpAxis(stage) == UsdGeom.Tokens.y
```

A `.vrm` stage declares `metersPerUnit = 1` and `upAxis = "Y"`, and the bake
re-declares whatever the avatar resolved. A layer that declared neither would
report `0.01` and describe a 1.6 cm avatar while looking identical in
`usdview` — the same failure the multi-clip stage above can still be written
into by hand.
