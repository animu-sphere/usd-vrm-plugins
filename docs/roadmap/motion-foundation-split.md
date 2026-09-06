# Motion foundation split — `motionCore` + `motionRuntime` as their own repository

**Status:** ⬜ not started, and **conditional** — the first milestone is a
measurement that can end the track · **Target:** after boundary consolidation ·
**Policy:** [design/INTEGRATION_SCOPE_POLICY.md](../design/INTEGRATION_SCOPE_POLICY.md) §10 ·
**Added:** 2026-09-06

`motionCore` and `motionRuntime` are vendor-neutral by construction: a pose, an
animation, root motion, constraints, a timestamped buffer, interpolation and
blending, with no VRM vocabulary, no OpenUSD dependency, no network and no
product name. That is the definition of a component that could live somewhere
else. It is not, on its own, a reason to move it.

**Scope decided 2026-09-06: the motion foundation only.** `vrmRetarget` stays —
it carries VRM in its name, its `ExpressionResolver` and its `LookAtEvaluator`.
`motionSource`, `motionBvh` and `motionTracking` stay for now (§7).
`liveTransport` and `osc` stay; they are small shared leaves whose likelier
answer is replacement by an existing library, not promotion to a project.

## 1. The gate this track opens with

[Scope policy §10](../design/INTEGRATION_SCOPE_POLICY.md) sets four conditions,
and the 2026-09-06 order deliberately schedules this track **before** the work
that would satisfy the first one. That is not an oversight to fix silently — it
is the reason the first milestone is a measurement:

| Condition | Where it stands on 2026-09-06 |
| --- | --- |
| Two or more consumers outside VRM | **One, after Motion Phase E.** `execMotion` is vendor-neutral by specification and names no VRM. There is no second. |
| An API carrying no VRM vocabulary | **Believed true, never checked as a claim.** The boundary checks assert what may not be *depended on*, not what the public headers *say*. |
| A need for independent versioning | **Not demonstrated.** Nothing outside this workspace pins a motion version. |
| A release cadence that has diverged | **No.** Every motion release to date is a plugin release. |

So the honest statement of this track is: **one condition is met after OpenExec,
one is measurable now, and two are not met.** MFS-0 measures them. If the answer
is still "no external consumer", the track stops at MFS-2 — which is the half
that pays for itself either way.

## 2. Reversible preparation, then a one-way door

The work divides cleanly, and only the last part is irreversible.

**Reversible (MFS-1, MFS-2).** Make the foundation *separable*: its public API
free of VRM vocabulary as a checked property, its own version, its own package,
its own test suite, and — the real test — consumed from inside this workspace
through `find_package` as though it were external. Every one of those is an
improvement to the current repository whether or not anything moves, which is
why they come first and why an inconclusive gate does not waste them.

**Irreversible (MFS-3).** Moving the history to another repository, and turning
an in-tree edge into a pinned external dependency. This costs a second release
contract, a second CI configuration, a version pin to keep current, and the loss
of one-PR changes across the boundary. It buys nothing until someone outside
VRM is actually consuming it, which is exactly what MFS-0 is for.

## 3. MFS-0 — measure the four conditions ⬜

- ⬜ **Name the consumers.** `execMotion` after Motion Phase E is one. Write
  down what the second would be, concretely, or record that there is none.
- ⬜ **Check the API claim mechanically.** Every public header of `motionCore`
  and `motionRuntime` scanned for VRM vocabulary, VRM-specific semantics, and
  types that only make sense to an avatar pipeline. This is a new boundary check
  and not a review pass — the existing ones look at edges, not at names.
- ⬜ **State the cadence.** Has any motion change ever wanted a release the
  plugins did not?

Outcome is one of: proceed to MFS-1; proceed to MFS-1 and MFS-2 only, and
re-gate later; or close the track with the measurement recorded.

## 4. MFS-1 — no VRM vocabulary in the public API ⬜

Whatever MFS-0 finds, fix it here. Two kinds of finding are expected and they
are not the same problem:

- **A name.** Cheap, mechanical, and the reason to do it before anything depends
  on the header from another repository.
- **A concept.** A type that is only meaningful because a VRM rig is downstream
  is a boundary defect that a rename would hide. It goes back to
  [the boundary track](boundary-consolidation.md) rather than being renamed
  here.

## 5. MFS-2 — consumed as if external, in place ⬜

- ⬜ The foundation installs a package and the workspace consumes it through
  `find_package`, with a contract row like every other package
  ([PACKAGE_CONTRACT.md](../architecture/PACKAGE_CONTRACT.md)).
- ⬜ Its own version, moving on its own contract rather than with the product's
  tag.
- ⬜ Its test suite runs against the installed package, not the source tree —
  layer 1 and part of layer 4 of
  [scope policy §9](../design/INTEGRATION_SCOPE_POLICY.md).
- ⬜ A consumer fixture outside the workspace, on the shape
  [the packaging track](packaging-hardening.md) built for the twelve packages.

At the end of MFS-2 the component is *portable*. Nothing has moved.

## 6. MFS-3 — the move, if the gate opened ⬜

Only reached if MFS-0 found a real second consumer.

- ⬜ Repository name. The candidates are `open-motion`, `humanoid-motion`,
  `motion-foundation`; picking one is the last decision, not the first.
- ⬜ History preserved for both libraries, and this workspace's edges become a
  pinned dependency with a stated version policy.
- ⬜ **Where the interop tests live is the hard part.** The cross-boundary layer
  (source → canonical → retarget → USD) spans both repositories by definition,
  and a test that spans two repositories runs in neither by default. The
  proposal is that it stays here — this repository is the integrator, and the
  foundation's own suite covers it standalone.
- ⬜ The motion corpus stays with the consumer that validates against it, which
  is here.

## 7. Open questions

- **Does `motionSource` follow?** It is format-neutral and VRM-free, and its
  `motionCore` edge is four files. It follows only if corpus tooling grows
  non-VRM users, which is a measurement the NPZ/AMASS track produces rather than
  one this track can make.
- **Does `motionTracking` follow?** Same shape, same answer, one consumer fewer.
- **Does `vrmRetarget` have a generic half?** A pose retargeter between two
  skeletons is generic; a humanoid map, the VRM root-motion policy, and the two
  resolves are not. Re-separating them is a real question and it is **not** this
  track's — it is a WORKSPACE.md change, and doing it as part of a repository
  move would hide a boundary decision inside a migration.
- **Who owns the canonical contract document?** [MOTION_CONTRACT.md](../design/MOTION_CONTRACT.md)
  describes types that would live elsewhere while
  [MOTION_ARCHITECTURE_POLICY.md](../design/MOTION_ARCHITECTURE_POLICY.md)
  describes an architecture that would not. They split along the same line the
  code does, and the split is not free: four documents cite the policy by
  section number.

## 8. Done when

Either the gate closes with a recorded measurement, or:

- The foundation builds, tests, packages and versions independently.
- This workspace consumes it as an external dependency and nothing downstream of
  `motionCore` changed to allow it.
- A consumer that has never heard of VRM builds against it.
