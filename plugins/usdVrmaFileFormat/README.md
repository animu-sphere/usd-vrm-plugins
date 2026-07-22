# usdVrmaFileFormat

`usdVrmaFileFormat` imports a VRM Animation (`.vrma`) GLB with the official
`VRMC_vrm_animation` 1.0 extension into an avatar-independent USD stage:

```text
/Animation
├─ HumanoidSkeleton  UsdSkelSkeleton using VRM semantic joint paths
└─ BodyAnimation     UsdSkelAnimation, bound to HumanoidSkeleton
```

Version 0.3.0 implements rotation tracks and the hips translation track from
the first glTF animation. It preserves the source extension JSON as provenance,
uses 30 time codes per second, and keeps the clip independent of any target
VRM. Expression and look-at animation, scales, retargeting, live capture, and
OpenExec are intentionally outside this bundle.

The bundle depends only on `vrmContainer` for GLB validation and `motionCore`
for semantic motion values. It never links the VRM avatar importer or its
private canonical model.
