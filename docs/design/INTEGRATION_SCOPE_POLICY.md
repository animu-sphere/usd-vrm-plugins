---
status: binding
owner: usd-vrm-plugins
---

# Integration scope policy

**Adopted:** 2026-09-06 · **Revised:** 2026-09-17, 2026-09-25

What this repository is for, what it will not become, and the test a proposed
identity, dependency or artifact has to pass. It is the *scope* half of the
policy set: [DESIGN_POLICY.md](DESIGN_POLICY.md) says how the importer is built,
[VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md) says how VRM and VRMA use motion,
[architecture/WORKSPACE.md](../architecture/WORKSPACE.md) says where the
code lives and what may depend on what, and
[architecture/PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md) says what
each installed package promises. This document says **how far the repository
goes**, which none of them states and all of them assume.

It restates nothing those four fix. Where it appears to overlap one, the other
wins; a structural claim goes to WORKSPACE.md first, in its own PR. Section
numbers are stable.

## 1. The scope statement

> `usd-vrm-plugins` connects **VRM assets, VRMA motion and VRM semantics to
> OpenUSD**, over the shared motion core of `usd-motion-plugins`. It does not
> become a motion engine, a capture SDK, a generative model, or a network
> protocol stack.

Two layers are in scope:

1. **Format integration** — `.vrm` and `.vrma` read as OpenUSD assets and
   motion, the schemas that carry VRM meaning, and resolution of resources
   inside a `.vrm`.
2. **VRM semantics applied to motion** — the humanoid binding from
   `VrmHumanoidAPI`, VRM 1.0's required bones, expression arbitration,
   look-at, the bake, and `execVrm`.

Generic motion — values, sampling, filtering, recording, retargeting, the
OpenUSD motion mapping and generic OpenExec nodes — is `usd-motion-plugins`',
and this repository consumes it as installed packages. Device and protocol
input is `motion-connectors`'. The split was completed in 2026-09; how it was
done is [archive/motion-split/](../archive/motion-split/).

## 2. What this repository does not own

Out of scope, permanently, unless a decision recorded in this document
reverses one:

- Generic motion semantics or processing (§1). A new generic capability is
  proposed in `usd-motion-plugins`.
- Device, protocol or network input, a vendor SDK, device management, or a
  capture application's UI. These are `motion-connectors`'.
- A general-purpose IK engine, animation graph, or behaviour/state machine.
- A generative motion model, its training, or its inference infrastructure.
- Large motion corpus hosting.
- A general humanoid DCC toolchain, a game runtime, or the update loop that
  composes avatars at run time (`usd-avatar-runtime`, `usd-stage-runner`).

The same list, as refusals, is [the backlog's non-goals](../roadmap/backlog.md#non-goals).

## 3. The test for a new identity

WORKSPACE.md §1 names the identities; this is the test a *proposed* one has to
pass. A new bundle or library needs at least one of:

1. **A different OpenUSD plugin registration boundary.** Only something OpenUSD
   discovers and registers is a plugin bundle; `usdVrmPackageResolver` is one
   because it registers an `ArPackageResolver`.
2. **A different dependency direction.** `vrmContainer` is a library because
   the importer and the resolver both read GLB bytes and neither may link the
   other.
3. **A standalone consumer or distribution that means something.** A package
   nobody would install on its own is a directory, not an identity.

And it must pass §1: an identity with no VRM vocabulary belongs in
`usd-motion-plugins` or `motion-connectors`. An extraction meeting none of the
three is deferred. `vrmCore` is the standing example: the importer's canonical
model gets its own library when a **second consumer** appears, exactly as
`vrmContainer` did. Abstractions are built from consumers, not predictions.

## 4. Canonical motion is the only confluence

Every producer terminates at `usd-motion-plugins`' `MotionPose` /
`MotionClip`, and nothing downstream of that point knows which producer it
was. That invariant is owned there
([its MOTION_CONTRACT.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md));
what it means here:

- **VRMA is not privileged.** It is the one producer this repository keeps. A
  `.vrma` clip, a converted BVH file and a recorded session reach a VRM rig
  through one `motion_retarget` argument list, and
  `workspace_reference_pipeline` holds that.
- **VRM semantics stay out of the generic types.** Expressions, look-at,
  spring bones and the humanoid schema binding are VRM vocabulary; a pose, a
  joint, a contact, a confidence and a timestamp are not. A clip's expression
  weights travel as names on the canonical pose and are resolved onto a rig
  only by `vrmRig` ([VRM_MOTION_POLICY.md §5](VRM_MOTION_POLICY.md#5-expressions-on-a-rig)).

## 5. OpenExec sits above the libraries, never under them

OpenExec exposes computation that libraries already own. The dependency runs
one way:

```text
consumed motion packages ← vrmRig ← execVrm
                                ↖ motion_retarget
```

and never a library → OpenExec. Every node is a thin wrapper over a library
call, so the same computation is reachable from a CLI, a unit test and an exec
graph with one implementation behind all three. A node that cannot be written
as a wrapper is a **finding about the library boundary**, not a licence to
implement the computation a second time.

I/O inside a computation is a permanent non-goal: no socket, no file watch, no
SDK callback, no wall clock. A computation evaluates an immutable snapshot
(`usd-motion-plugins`' design policy §21, checked here by
`execVrm_boundaries`).

## 6. Generators and constraints

Not this repository's. The generator interface and `MotionConstraintSet` are
`usd-motion-plugins`'; a generation adapter is `motion-connectors`'. A
generated clip reaches a VRM rig the way any clip does (§4).

## 7. The aggregate product

The aggregate product is what a default installation reproduces: the VRM and
VRMA bundles, the libraries they need, the product CLI, and the consumed
packages they resolve. Membership is declared, not implied by a build; the
contract is [PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md) and the
members are `[workspace].release_members` (WORKSPACE.md §5). No live or vendor
adapter is a member; whether and how those ship is `motion-connectors`'.

## 8. Release quality is artifact closure

A version number describes an intention; the artifact is the release. Each one
is expected to demonstrate, from the package and not from the source tree: the
workspace graph, a standalone bundle build, the aggregate product package, an
installed-consumer configure, an artifact-only smoke, and installed data
resolving.

"It works in the tree" is not a release claim, and the reason is measured: a
composed build resolves every target in-tree without opening a package config,
so on 2026-08-29 two installed packages named a target no consumer could
resolve while all seventeen lanes were green
([the packaging track](../archive/packaging/packaging-hardening.md) §1). One
checklist a release passes or fails is still open
([current.md](../roadmap/current.md#release-closure-and-checkable-invariants)).

## 9. Four test layers

Each layer answers a question the one below it cannot:

| Layer | Scope | Question |
| --- | --- | --- |
| 1 — unit | `vrmContainer`, `vrmRig` | Is the computation right, with no runtime and no I/O? |
| 2 — format contract | VRM, VRMA | Does a file mean what we say it means — malformed input refused, mapping, timestamps, root motion, missing bones, provenance? |
| 3 — cross-boundary | VRMA and the consumed producers → canonical → VRM retarget → `UsdSkelAnimation`; offline against `execVrm` | Do the boundaries compose, for every producer category, on both implementations? |
| 4 — installed artifact | the release package, off every source path | Does the thing we ship work? |

Layer 4 is the acceptance layer. Layers 1–3 are how a failure there becomes
attributable. Generic motion is unit-tested where it lives.

## 10. Repository split

Done. Generic motion moved to `usd-motion-plugins` and live input to
`motion-connectors` (2026-09-19..24); each move reproduced its parity
baseline before the copy here was deleted. What remains is that no copy comes
back: `workspace_cmake_boundaries` fails when a moved identity reappears under
any name it has had. The plan and the reasoning that preceded it are
[archive/motion-split/motion-foundation-split.md](../archive/motion-split/motion-foundation-split.md);
where each identity went is
[WORKSPACE.md §9](../architecture/WORKSPACE.md#9-destinations-under-the-motion-architecture).

A cross-repository test — VRMA → `MotionClip` → a target VRM — runs here,
because this repository is its natural integrator, until an integration
repository exists (`usd-motion-plugins`' design policy §30.6).

## 11. PR review checklist

The invariants above, in the form a reviewer can apply:

**Scope** — no generic motion capability, device input or protocol lands
here; a moved identity does not come back; nothing here becomes a dependency
of `usd-motion-plugins` or `motion-connectors`.

**Import** — the importer authors and never evaluates; the `.vrma` reader never
looks for a target avatar; a file format never retargets.

**USD** — a source asset and its derivative are separate; a composition
relationship is explicit; canonical semantics and target joint paths do not
mix.

**Packaging** — every `PUBLIC`/`INTERFACE` edge is a `find_dependency` row; an
installed data path never resolves into the source tree.

**OpenExec** — a node is a wrapper; a computation performs no I/O and evaluates
a snapshot.

**Documentation** — a sibling's contract is linked, never restated
([contributing/documentation.md](../contributing/documentation.md)).

## 12. What success looks like

- `avatar.vrm` produces a deterministic OpenUSD asset.
- `walk.vrma` produces an avatar-independent semantic clip.
- Canonical motion plus a VRM target goes through one retarget, whichever
  producer made the motion.
- A CLI and an OpenExec graph call the same computation library, and agree.
- The reference workflow runs from release artifacts, with no source checkout.

Growth is measured in **boundary stability**, not in feature count.

## 13. Place among the motion repositories

The `usd-motion-plugins` design policy fixes the ecosystem's dependency
direction, and this repository keeps it:

```text
motion-connectors ─→ usd-motion-plugins ←─ usd-vrm-plugins
                              ↑
                       usd-mmd-plugins
usd-avatar-runtime ─→ all of the above
```

| Question | Repository |
| --- | --- |
| Is it true of motion whatever the avatar format — a pose, a clip, sampling, retarget, recording, `UsdSkelAnimation` authoring, BVH? | `usd-motion-plugins` |
| Does it talk to a device, a service or a network protocol, or own reconnection and device lifecycle? | `motion-connectors` |
| Does it need the VRM specification — `.vrm`, `.vrma`, the humanoid schema binding, expressions, look-at, spring bones? | here |
| Does it schedule evaluation, or wire motion, avatar semantics, physics and application state together? | `usd-avatar-runtime` |

That table is the motion-plugins policy's §38, restated for the reviewer of a
change here.
