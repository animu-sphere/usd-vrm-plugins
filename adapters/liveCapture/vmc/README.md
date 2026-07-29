# vrmAdapterVmc

The VMC Protocol input adapter: OSC-over-UDP datagrams from any sender
application, in; canonical humanoid motion, out.

```text
UDP datagram → OSC decode → VMC message decode → frame assembly
             → VRM bone mapping → HumanoidPose → LiveCaptureSource
```

**Status: scaffold.** The build, the manifest, the boundary check, and the
frozen diagnostic codes exist. No decoder does yet — see
[the plan](../../../docs/roadmap/adapters-mocopi-vmc-ardy.md) §5 for the
implementation order and Milestone A for what "done" means.

## What this is, structurally

A plain static CMake library with an `openstrata.library.yaml`, exactly like
`motionRuntime` and `vrmRetarget` — **not** a plugin bundle. It registers
nothing with OpenUSD and ships no `plugInfo.json`, because
[WORKSPACE.md §2](../../../docs/architecture/WORKSPACE.md) keeps it away from
`vrmSchema`, from every file-format bundle, and from OpenExec. It has exactly
two dependencies, and they are the two its manifest declares:

```text
vrmAdapterVmc -> motionCore, motionRuntime
```

`tests/check_boundaries.py` is what makes that a fact rather than an intention.
It fails on a plugin manifest anywhere in the tree, on a stage/registration/exec
API in `include/` or `src/`, on a mention of a sibling adapter or a plugin
bundle, on a `target_link_libraries` naming anything but the two permitted
libraries, and on a binary whose imports leave the OpenUSD value-type layer.

That last one inspects the **test executable**, not the adapter's own archive.
A static `.lib`/`.a` records no imports at all — `dumpbin /dependents` on one
prints a section summary and nothing else — so a check pointed at the library
would be a gate that cannot fail. Pointed at the linked executable it has real
teeth: run against `motion_retarget` it reports `usd_sdf`, `usd_usd`, and
`usd_usdSkel`, which is exactly the class of import this boundary exists to
refuse.

## What it is not allowed to do

Target-skeleton discovery, retargeting, rest-pose correction, its own
interpolation, its own smoothing, `UsdSkelAnimation` authoring, stage authoring,
or a dependency on a sibling adapter. Every one of those already exists once in
this repository; a second copy inside an adapter is a forked pipeline that stays
invisible until two inputs disagree about the same avatar.

The adapter maps a VMC bone name to a `motion::HumanBone`, and stops. It never
resolves a joint index in a target skeleton — that is `vrmRetarget`'s job, one
layer down the pipeline and behind a `VrmHumanoidAPI` mapping.

One permission is easy to misread in the other direction: this adapter's future
**CLI**, under `tools/`, *may* drive `vrmRetarget` and author a stage, exactly as
`motion_retarget` does. The library may not. That is why the boundary check
scans `include/` and `src/` only.

## Transport arrives last

The implementation order is deliberately backwards from the tempting one:
recorded-packet decoder → semantic mapping → frame assembly → live-source bridge
→ thin UDP receiver. Building the receiver first would make every subsequent
test require a live sender; building it last keeps the whole adapter verifiable
in CI from committed fixtures, with no hardware and no socket.

## Diagnostics

Eight codes, frozen in `include/vrmAdapterVmc/Diagnostics.h` before the first
decoder exists so that the set describes the protocol rather than whichever bug
was chased last:

```text
VRM_VMC_PACKET_MALFORMED        VRM_VMC_UNSUPPORTED_MESSAGE
VRM_VMC_TIMESTAMP_REGRESSION    VRM_VMC_DUPLICATE_BONE
VRM_VMC_INCOMPLETE_FRAME        VRM_VMC_SOURCE_RESTARTED
VRM_VMC_SOCKET_BIND_FAILED      VRM_VMC_STALE_JOINT
```

`VRM_MOTION_*` is the canonical layer's namespace, not this one's: a reader can
tell a decode failure from a motion-contract violation without knowing which
adapter produced it. Exactly one code is non-recoverable — a receiver that never
bound has nothing to recover into.

## Building and testing

Composed with the rest of the workspace:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<usd-install>
cmake --build build --config Release
ctest --test-dir build -R vrmAdapterVmc
```

Or through the runtime `ost` resolves for the workspace:

```sh
ost build && ost test
```

Standalone — this directory is its own CMake project, resolving `motionCore` and
`motionRuntime` as installed packages rather than in-tree targets:

```sh
cmake -S adapters/liveCapture/vmc -B build/vmc \
      -DCMAKE_PREFIX_PATH="<usd-install>;<workspace-prefix>"
cmake --build build/vmc
```

`ost plugin build` is not the standalone route here: it takes a *bundle*
directory and refuses anything without an `openstrata.plugin.yaml`, which an
adapter does not have and must not grow.
