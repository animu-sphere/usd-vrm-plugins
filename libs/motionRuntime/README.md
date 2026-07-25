# motionRuntime

The OpenExec-independent humanoid motion runtime: a bounded pose history,
interpolation, resampling, smoothing, and blending over the `motionCore` value
types.

`motionRuntime` is a **plain static CMake library**, not a plugin bundle. It has
no `plugInfo.json`, no `openstrata.plugin.yaml`, and no USD stage, `Sdf`, `Plug`,
or file-format dependency — only OpenUSD's `Gf` value types, inherited through
`motionCore`. See [WORKSPACE.md](../../docs/architecture/WORKSPACE.md) §1–2 for
the identity and dependency rules, enforced by
[`tests/check_boundaries.py`](tests/check_boundaries.py).

Workspace Phase: **6b** (bootstrapped in v0.4.0 alongside
[`vrmRetarget`](../vrmRetarget/)).

## What it provides

| Header | Contents |
| --- | --- |
| `motionRuntime/PoseBuffer.h` | `PoseBuffer` — bounded, strictly ordered pose history with bracketed sampling and capped position extrapolation |
| `motionRuntime/Interpolation.h` | `SlerpShortest`, `LerpRootMotion`, `LerpPose` |
| `motionRuntime/Resample.h` | `Resample`, `SampleAnimation` |
| `motionRuntime/Filter.h` | `PoseFilter` — frame-rate independent exponential smoothing |
| `motionRuntime/Blend.h` | `BlendPoses` (two-pose and weighted N-pose) |

## Two rules the whole library obeys

- **A missing sample is not a zero sample.** Every operation preserves
  `HumanoidPose::validRotations` and the `RootMotion` presence flags. Where one
  input carries a bone and the other does not, the value is *held*, never faded
  toward identity. A live source that drops a bone for a frame does not snap the
  avatar.
- **Orientations stay unit quaternions and take the short arc.** `q` and `-q`
  are the same rotation, so every interpolation picks the representative on the
  near hemisphere first. N-pose blending folds inputs in pairwise for the same
  reason — a component-wise weighted sum of quaternions is not a rotation.

## Building

It builds as part of the workspace root `CMakeLists.txt`. Standalone:

```bash
cmake -S libs/motionRuntime -B build/motionRuntime \
      -DCMAKE_PREFIX_PATH="<usd-install>;<motionCore-install>"
cmake --build build/motionRuntime
ctest --test-dir build/motionRuntime --output-on-failure
```
