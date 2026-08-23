# usdVrmaFileFormat

`usdVrmaFileFormat` imports a VRM Animation (`.vrma`) GLB with the official
`VRMC_vrm_animation` 1.0 extension into an avatar-independent USD stage:

```text
/Animation
├─ HumanoidSkeleton  UsdSkelSkeleton using VRM semantic joint paths
├─ BodyAnimation     UsdSkelAnimation, bound to HumanoidSkeleton
└─ Expressions       one prim per expression the clip declares
```

Version 0.4.0 implements rotation tracks and the hips translation track from
the first glTF animation. It preserves the source extension JSON as provenance,
uses 30 time codes per second, and keeps the clip independent of any target
VRM. Look-at animation, scale animation, retargeting, live capture, and
OpenExec are intentionally outside this bundle.

## Expressions

VRMA declares an expression under `expressions.preset` or `expressions.custom`
and points it at a node whose **translation X component** carries the weight.
Each declared expression becomes one prim under `/Animation/Expressions`:

```text
def Scope "happy"
{
    uniform token vrm:expressionName = "happy"   # verbatim, the join key
    uniform token vrm:expressionType = "preset"
    float vrm:expressionWeight.timeSamples = { 0: 0, 15: 0.5, 30: 1 }
}
```

Three things this deliberately does *not* do:

- **It expands nothing.** A VRM expression drives N morph targets across M
  meshes plus material colours, and which ones is the *avatar's* property. A
  clip bound to no avatar cannot know them, so no `blendShapes` /
  `blendShapeWeights` binding is authored and `ExpressionResolve` stays a
  consumer step ([motion policy](../../docs/design/MOTION_ARCHITECTURE_POLICY.md)
  §4.3).
- **It does not clamp.** The specification clamps a weight to `[0, 1]`; a file
  that said `1.5` is carried verbatim with a `VRMA109` warning, because
  correcting it here would hide the authoring tool from whoever reads the clip.
- **It does not invent a zero, and it does not drop a stated one.** A clip can
  say three different things about an expression, and they are authored
  differently. A channel drives the node → time samples. No channel, but the
  node states a transform → one default value, because glTF leaves an
  un-animated node at its own TRS and VRMA reads the weight out of that
  translation. No channel and no transform → **no `vrm:expressionWeight` at
  all**, because an unreported weight is not a weight of zero
  ([MOTION_CONTRACT.md](../../docs/design/MOTION_CONTRACT.md#expression-semantics-v070)).
  What separates the last two is what the file wrote, never whether the number
  happens to be zero.

Expression key times join the same union every other channel is evaluated at,
so an expression that keys off the body's beats adds instants the body is
sampled at too, rather than being resampled onto the body's timeline.

The attributes are namespaced rather than a typed schema: whether the VRMA
animation schemas belong in `vrmSchema` is still open
([backlog](../../docs/roadmap/backlog.md), motion-layer open questions), and a
`VrmAnimationExpressionAPI` can later be applied to exactly these prims with
these attribute names without moving anything.

**The prim name is not yet a join key.** The layout mirrors the importer's
`/Asset/rig/Expressions/<name>`, but the importer sanitizes names through its
own private table and this bundle cannot link it, so a name outside ASCII lands
on a different prim name on each side. `vrm:expressionName` is the key that
survives that — and the avatar side does not author it yet, which is the first
thing `ExpressionResolve` has to fix.

`BodyAnimation` does author a constant identity `scales`. Scale is not
animated, but `UsdSkelAnimation.scales` has no schema fallback and UsdSkel
fetches translations, rotations and scales as a unit — a clip that omits the
attribute binds cleanly and then resolves to the skeleton's rest pose. The
array exists only so the clip evaluates.

The bundle depends only on `vrmContainer` for GLB validation and `motionCore`
for semantic motion values. It never links the VRM avatar importer or its
private canonical model.
