---
status: superseded
owner: usd-motion-plugins
canonical: VRM_MOTION_POLICY.md
---

# Motion contract — superseded

**Status: superseded (2026-09-25).** Do not use this document to decide
current semantics.

**Former purpose.** The executable motion contract of this repository,
v0.3.0 through v0.9.0: `motionCore` values, `.vrma` reading, retarget
semantics and diagnostics, live capture, comparison, expressions, look-at,
recorded sources, the canonical basis, root and hips, tracker observations,
and the OpenExec driver contract.

**Current owners.** Every section has one owner now:

| Former section | Owner now |
| --- | --- |
| Scope; Coordinates and time; Provenance and exclusions (the `.vrma` reader) | this repository: [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md) §3 |
| `motionCore` value contract; Live-capture semantics; Comparison semantics; What the contract still owes | `usd-motion-plugins`: [MOTION_CONTRACT.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md) |
| Retarget semantics; Scale policy; Partial skeleton policy; Retarget diagnostics | `usd-motion-plugins`: [RETARGETING_POLICY.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/RETARGETING_POLICY.md); the VRM binding and required bones: [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md) §4 |
| `motion_retarget` exit codes | this repository: [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md) §7.1 |
| OpenExec driver contract; `VRM_OPENEXEC_*` codes | `usd-motion-plugins`: [EXEC_CONTRACT.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/EXEC_CONTRACT.md) §3–§4; parity with the bake: [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md) §7.3 |
| Expression semantics; Look-at semantics | the value: `usd-motion-plugins` [MOTION_CONTRACT.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md) §6; reading from `.vrma` and resolving on a rig: [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md) §3.3–§3.4, §5–§6 |
| Recorded-source provenance; The canonical basis; Recorded-source rest pose and the path rule; Root and hips | `usd-motion-plugins`: [MOTION_CONTRACT.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md) §3, §5.3, §7.1 |
| Tracker observations | `motion-connectors`: [CONNECTOR_CONTRACT.md](https://github.com/animu-sphere/motion-connectors/blob/main/docs/design/CONNECTOR_CONTRACT.md) §4; the boundary on the motion side: `usd-motion-plugins` MOTION_CONTRACT §11.1 |

The design triplet this document shipped beside moved to
[`tests/motion/fixtures/design_triplet/`](../../tests/motion/fixtures/design_triplet/).

**History.** The full text is in git history; the last revision is
[e98db79](https://github.com/animu-sphere/usd-vrm-plugins/blob/e98db79635e431b958c0cf64f088c822a9f73cf2/docs/design/MOTION_CONTRACT.md).
Its headings are the anchors older documents in this repository cite.
