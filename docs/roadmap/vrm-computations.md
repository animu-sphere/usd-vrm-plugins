# Standard VRM computations — scope review

Status: **backlog**. Owner: **usd-vrm-plugins**. Priority and proposed target
are in the [index](README.md#status-at-a-glance). These computations extend
`execVrm` over existing `vrmRig` values and do not require ExecIr.

| ID | Next action | Done when / evidence destination |
| --- | --- | --- |
| C01 | Review the expression/look-at scope, stage inputs, public result types and client owner. | An accepted design names the bounded implementation, parity fixtures and consumer; motion policy and architecture contract. |

Only after C01 is accepted, schedule the following implementation candidates.
They are not release gates for the current milestone.

## Expression computations

Former optional-track P1-1: `vrm.resolveExpressionWeights`,
`vrm.computeMorphTargetWeights`, `vrm.computeMaterialColorOverrides`,
`vrm.computeTextureTransformOverrides`. First determine which results the
existing resolver can actually supply; raw texture-transform binds are not
silently promoted to supported evaluation.

Acceptance: thin wrappers over the VRM resolver, equal to supported offline
bake results, immutable inputs, no continuous USD writes, targeted
invalidation and installed-artifact consumption. Evidence belongs in the
parity suite and a dated report.

## Look-at computations

Former optional-track P1-2: `vrm.computeLookAt` and `vrm.applyLookAtToPose`
over `vrmRig`'s evaluator, with explicit head/eye transforms, target point,
range maps and bone/expression mode.

Acceptance: supported VRM 0.x/1.0 results agree with the offline bake,
including bone and expression modes, cache reuse/invalidation and a packaged
consumer. Evidence belongs in the parity suite and a dated report.
