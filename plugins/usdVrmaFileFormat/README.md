# usdVrmaFileFormat

`usdVrmaFileFormat` imports a VRM Animation (`.vrma`) GLB with the official
`VRMC_vrm_animation` 1.0 extension into an avatar-independent USD stage:

```text
/Animation
├─ HumanoidSkeleton  UsdSkelSkeleton using VRM semantic joint paths
└─ BodyAnimation     UsdSkelAnimation, bound to HumanoidSkeleton
```

Version 0.4.0 implements rotation tracks and the hips translation track from
the first glTF animation. It preserves the source extension JSON as provenance,
uses 30 time codes per second, and keeps the clip independent of any target
VRM. Expression and look-at animation, scale animation, retargeting, live
capture, and OpenExec are intentionally outside this bundle.

`BodyAnimation` does author a constant identity `scales`. Scale is not
animated, but `UsdSkelAnimation.scales` has no schema fallback and UsdSkel
fetches translations, rotations and scales as a unit — a clip that omits the
attribute binds cleanly and then resolves to the skeleton's rest pose. The
array exists only so the clip evaluates.

The bundle depends only on `vrmContainer` for GLB validation and `motionCore`
for semantic motion values. It never links the VRM avatar importer or its
private canonical model.
