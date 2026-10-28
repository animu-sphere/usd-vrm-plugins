# Spring-bone reference trajectories

`cases.json` is the shared, generated-rig input for the reference runners and
the C++ comparison. It is original test data with no avatar or motion assets.
The checked CSVs contain world tail positions and absolute local quaternions
for every simulated joint on every one of 32 ticks. Each tick translates the
placement and rotates the nonsimulated root around +Z; the initial pose is
identity. Parameters and frame motion are stated once in the JSON.

| Reference | Cases | Joint samples |
| --- | --- | --- |
| three-vrm springbone 3.4.5, Three.js 0.170.0 | Both versions: chains, VRM 0.x branching, sphere collisions, VRM 1.0 capsule collisions, moving/rotating centers | 672 |
| UniVRM v0.128.1 | VRM 0.x: chain, branching, sphere collision, moving/rotating center | 416 |

The runners execute actual upstream code, downloaded into a separate work
directory. `provenance.json` records versions and SHA-256 hashes of all six
UniVRM source files. The reference sources are **not committed**.

`generate_three.cjs` calls the published `VRMSpringBoneJoint` implementation.
It refreshes the complete scene's world matrices before each joint, including
the child whose position the joint uses to measure its length. The 3.4.5
manager's partial traversal can leave that child's world position stale after
an ancestor moves, changing a rigid bone's measured length. These fixtures
measure the joint solver with current transforms, not parity with that manager
traversal. The private `_currentTail` is read only to capture the solver output.

`GenerateUniVRM.cs` compiles the unmodified `SpringBoneSystem`, its joint
init/state/settings, scene info and sphere-collider code. `UnityMathShim.cs`
supplies only rigid transforms, quaternion/vector arithmetic and no-op gizmos;
the upstream code performs traversal, virtual-tail construction, integration,
collisions and state updates. This is a unit-scale algorithm comparison,
**not a Unity player measurement**. The first-child and virtual-tail cases
have identity leaf rotations, where three-vrm and UniVRM's tail rules agree.
The unit suite separately covers the API's behavior on invalid inputs and
deterministic degenerate collisions.

Regeneration on Windows needs Node/npm, Python and a Roslyn C# compiler with
.NET Framework. Pass the installed compiler path explicitly:

```powershell
python libs/vrmRig/tests/springbone/regenerate.py --work-dir build/springbone-references --csc <Roslyn-csc.exe>
```

Normal CTest runs use only the checked data and C++ executable; neither Node,
C# nor a network connection is needed. Comparison tolerances are `1e-4 m` for
tail positions and `1e-4` for sign-aligned unit quaternion component distance.
Tests check row coverage as well as error so missing frames or joints fail.

Reference owners:
[three-vrm Joint](https://github.com/pixiv/three-vrm/blob/v3.4.5/packages/three-vrm-springbone/src/VRMSpringBoneJoint.ts),
[three-vrm loader](https://github.com/pixiv/three-vrm/blob/v3.4.5/packages/three-vrm-springbone/src/VRMSpringBoneLoaderPlugin.ts),
[UniVRM logic](https://github.com/vrm-c/UniVRM/tree/v0.128.1/Assets/VRM/Runtime/SpringBone/Logic).
