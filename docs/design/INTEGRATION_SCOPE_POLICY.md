# Integration scope policy

**Status:** canonical · **Adopted:** 2026-09-06

What this repository is for, what it will not become, and the test a proposed
identity, dependency or artifact has to pass. It is the *scope* half of the
policy set: [DESIGN_POLICY.md](DESIGN_POLICY.md) says how the importer is built,
[MOTION_ARCHITECTURE_POLICY.md](MOTION_ARCHITECTURE_POLICY.md) says how motion
works, [architecture/WORKSPACE.md](../architecture/WORKSPACE.md) says where the
code lives and what may depend on what, and
[architecture/PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md) says what
each installed package promises. This document says **how far the repository
goes**, which none of them states and all of them assume.

It restates nothing those four already fix. Where it appears to overlap one, the
other wins and this is the summary; a structural claim goes to WORKSPACE.md
first, in its own PR.

## 1. The scope statement

> `usd-vrm-plugins` is the integration workspace that connects **VRM assets and
> humanoid motion to OpenUSD**. It does not become a motion engine, a capture
> SDK, a generative model, or a network protocol stack.

Three layers grew out of the original file-format plugin, and all three are in
scope:

1. **Format integration** — VRM and VRMA read as OpenUSD assets and motion.
2. **A vendor-neutral humanoid motion foundation** — canonical pose, animation,
   root motion, constraints, and the runtime over them.
3. **The layer that joins them** — retarget, VRM semantic resolution, and the
   OpenExec exposure of both.

The breadth is not the risk. The risk is re-coupling those three into one
implementation unit, one dependency graph and one release artifact, which is
what every boundary in WORKSPACE.md exists to prevent.

## 2. What this repository does not own

Out of scope for the core, permanently, unless a specific decision recorded in
this document reverses one:

- A vendor SDK, device management, or a capture application's UI.
- A general-purpose OSC or UDP framework. `libs/osc` is the OSC 1.0 wire format
  and `libs/liveTransport` is a receiver and a capture file; neither is a
  networking library with users of its own.
- A general-purpose IK engine, animation graph, or behaviour/state machine.
- A generative motion model, its training, or its inference infrastructure.
- Large motion corpus hosting.
- A general humanoid DCC toolchain, or a game runtime.

Each of these reaches the workspace, when it must, as an **adapter**, a thin
integration library, or a dependency — never as a core feature. This is the
existing non-goal list in [the backlog](../roadmap/backlog.md#non-goals) stated
as a boundary rather than as a set of individual refusals.

## 3. The test for a new identity

WORKSPACE.md §1 names the identities; this is the test a *proposed* one has to
pass. A new bundle, library or adapter needs at least one of:

1. **A different OpenUSD plugin registration boundary.** Only something OpenUSD
   discovers and registers is a plugin bundle. Convenience is not a reason —
   `vrmAdapterVmc` is a library precisely because it registers nothing.
2. **A different dependency direction.** `libs/osc` and `libs/liveTransport` are
   siblings with empty edge sets, which is why they are two libraries and not
   one.
3. **A standalone consumer or distribution that means something.** A package
   nobody would install on its own is a directory, not an identity.

An extraction meeting none of the three is deferred. `vrmCore` is the standing
example: the importer's canonical model gets its own library when a **second
consumer** appears and not before, exactly as `vrmContainer` did when the
resolver became the second reader of GLB bytes (WORKSPACE.md §1). Abstractions
are built from consumers, not from predictions.

## 4. Canonical motion is the only confluence

Every producer terminates at `motion::HumanoidAnimation` / `HumanoidPose`, and
nothing downstream of that point knows which producer it was. This is the
invariant the motion layer is arranged around
([motion policy §5](MOTION_ARCHITECTURE_POLICY.md), §8.3), and it is what makes
a clip, a file, a live session and a generated take one pipeline rather than
four.

Two consequences that are easy to lose:

- **VRMA is not privileged.** It is the VRM ecosystem's standard motion
  container and one input among several. `.vrma`, `.bvh`, a recorded trace, a
  corpus array and a generated clip all reach the same type by the same rule:
  format syntax and storage interpretation in the reader, semantics in a
  profile or a specification, and nothing about a target rig anywhere below the
  retarget.
- **A container is not a format.** `.npz` is a storage container whose meaning
  depends on the corpus that wrote it, so a reader for one is a reader plus an
  explicit profile — never a generic "NPZ support" whose field layout leaks into
  a core API. [The recorded track](../roadmap/recorded-motion-sources.md) §13
  carries the measurement that decides its identity.

**A tracker observation is not a pose**, and the same rule applies one layer
out: a numbered device is pre-IK, so protocol decode, tracker identity, body
region assignment and the solve are four boundaries and not one adapter
([the OSC track](../roadmap/osc-and-vrchat-trackers.md) §5).

**VRM semantics stay out of the generic types.** Expressions, look-at, spring
bones and the humanoid schema binding are VRM vocabulary; a pose, a bone, a
contact, a confidence and a timestamp are not. The two are evaluated together
and are not the same abstraction — which is why a clip's expression weights
travel as names on the canonical pose and are expanded onto a rig only in
`vrmRetarget`.

## 5. OpenExec sits above the libraries, never under them

OpenExec exposes computation this workspace already owns to a USD execution
graph. The dependency runs one way:

```text
motionCore ← motionRuntime ← vrmRetarget ← execMotion / execVrm
```

and never `motionCore → OpenExec`. Every node is a thin wrapper over a library
call, so the same computation is reachable from a CLI, a unit test and an exec
graph with one implementation behind all three
([motion policy §11](MOTION_ARCHITECTURE_POLICY.md)). A node that cannot be
written as a wrapper is a **finding about the library boundary**, not a licence
to implement the computation a second time.

I/O inside a computation is a permanent non-goal, not a deferral: no socket, no
file watch, no SDK callback, no wall clock. Receiving belongs to an adapter,
buffering to `motionRuntime`, and a computation evaluates an immutable snapshot
(motion policy §11.4).

## 6. Generators and constraints

A generation product reaches the workspace behind a vendor-neutral
`IMotionGenerator` and produces canonical motion like any other producer
(`adapters/generators/`). Text-to-motion, trajectory-conditioned motion,
procedural locomotion and a cloud API are then the same downstream shape.

`MotionConstraintSet` is deliberately not a generator feature. A **pose** and a
**desired condition** are separate representations, and the second is what
retarget correction, IK, interactive editing and generation all need — so it is
specified once (motion policy §7) and consumed by whichever of them arrives
first.

## 7. The aggregate product and its optional adapters

The aggregate product is what a default installation reproduces: the VRM and
VRMA bundles, the libraries they need, and the product CLIs. **Live and vendor
adapters are optional artifacts**, and are excluded for reasons that are
properties of the adapter rather than of the release — hardware and SDK
dependencies, a different licence, a different CI requirement, different
platform availability, and a network surface the core does not have.

Membership is declared, not implied by a build; the contract is
[PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md) and the aggregate's
members are `[workspace].release_members` (WORKSPACE.md §5). What is still open
is whether a release *ships* the optional artifacts and under whose version —
[the boundary track](../roadmap/boundary-consolidation.md) owns that decision.

## 8. Release quality is artifact closure

A version number describes an intention; the artifact is the release. Each one
is expected to demonstrate, from the package and not from the source tree: the
workspace graph, a standalone bundle build, the aggregate product package, an
installed-consumer configure, an artifact-only smoke, installed data and
profiles resolving, and each adapter's own closure.

"It works in the tree" is not a release claim, and the reason is measured rather
than cautionary: a composed build resolves every target in-tree without ever
opening a package config, so on 2026-08-29 two installed packages named a target
no consumer could resolve while all seventeen lanes were green
([the packaging track](../roadmap/packaging-hardening.md) §1).

## 9. Four test layers

Each layer answers a question the one below it cannot:

| Layer | Scope | Question |
| --- | --- | --- |
| 1 — unit | `vrmContainer`, `motionCore`, `motionRuntime`, `motionSource`, `motionTracking`, `osc`, `liveTransport` | Is the computation right, with no runtime and no I/O? |
| 2 — format contract | VRM, VRMA, BVH, future corpus profiles | Does a file mean what we say it means — malformed input refused, mapping, timestamps, basis, root motion, missing bones, provenance? |
| 3 — cross-boundary | source → canonical → retarget → `UsdSkelAnimation` | Do the boundaries compose, for every producer category? |
| 4 — installed artifact | the release package, off every source path | Does the thing we ship work? |

Layer 4 is the acceptance layer. Layers 1–3 are how a failure there becomes
attributable.

## 10. Repository split

Splitting the repository is a **release and ownership** decision, not a source
boundary one — the source boundaries already exist and are enforced in-tree. A
component leaves only when all of these hold:

1. Two or more consumers outside VRM.
2. An API that carries no VRM vocabulary.
3. A need for independent versioning.
4. A release cadence that has actually diverged from the plugins'.

| Component | Split candidate | Note |
| --- | --- | --- |
| `vrmSchema`, `usdVrmFileFormat`, `usdVrmaFileFormat`, `usdVrmPackageResolver` | no | The product |
| `vrmContainer` | unlikely | A VRM/GLB-specific shared leaf |
| `motionCore`, `motionRuntime` | **yes — the standing candidate** | [The split track](../roadmap/motion-foundation-split.md) |
| `vrmRetarget` | maybe | Its generic half and its VRM resolves would have to be re-separated first |
| `motionSource`, `motionBvh` | later | Only if corpus tooling grows non-VRM users |
| `liveTransport`, `osc` | unlikely | Small shared leaves; replacing them with an existing library is the likelier answer |
| The `vrmAdapter*` leaves | no | Product extensions, already optional artifacts |
| A generator engine | n/a | Never in this repository at all |

## 11. PR review checklist

The invariants above, in the form a reviewer can apply:

**Import** — the importer authors and never evaluates; the `.vrma` reader never
looks for a target avatar; a file format never retargets.

**Motion** — no source-specific type in `motionCore`; root motion is not the
hips' local pose; a missing bone is representable; a timestamp is not an integer
frame index.

**Adapter** — no vendor or protocol branch in the core; no product name outside
`adapters/` except in a declarative profile file; a tracker observation is not
called a pose.

**USD** — a source asset and its derivative are separate; a composition
relationship is explicit; canonical semantics and target joint paths do not
mix.

**Packaging** — an optional adapter never joins the aggregate implicitly; every
`PUBLIC`/`INTERFACE` edge is a `find_dependency` row; an installed data path
never resolves into the source tree.

**OpenExec** — a node is a wrapper; a computation performs no I/O and evaluates
a snapshot.

## 12. What success looks like

Not the number of supported formats. These five:

- `avatar.vrm` produces a deterministic OpenUSD asset.
- A clip, a capture, a corpus file, a live trace and a generated take all
  converge on `HumanoidAnimation`.
- Canonical motion plus a VRM target goes through one retarget and runtime
  contract, whichever produced the motion.
- A CLI and an OpenExec graph call the same computation library.
- The reference workflow runs from release artifacts, with no source checkout.

Growth is measured in **boundary stability**, not in feature count. The
repository splits when the motion foundation has users who have never heard of
VRM — and not before, because until then the split costs a release contract and
buys nothing.
