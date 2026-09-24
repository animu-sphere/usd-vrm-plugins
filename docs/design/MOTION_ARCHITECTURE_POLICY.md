---
status: superseded
owner: usd-motion-plugins
canonical: VRM_MOTION_POLICY.md
---

# Motion architecture policy — superseded

**Status: superseded (2026-09-25).** Do not use this document to decide
current architecture, ownership or scope.

**Former purpose.** The policy for everything below the importer in this
repository: `.vrma` import, a vendor-neutral motion core, retargeting, live
and recorded input, generation, and the OpenExec runtime, sequenced as
Motion Phase A–H.

**Current owners.** The motion architecture moved out of this repository in
the motion split (2026-09-19..24), and each part now has one owner:

| Former section | Owner now |
| --- | --- |
| §2–§4 — VRM and VRMA plugins, composition, VRMA authoring | this repository: [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md) §1–§3 |
| §5, §7, §9, §12 — motion core, constraints, source metadata, live evaluation | `usd-motion-plugins`: [DESIGN_POLICY.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/DESIGN_POLICY.md) and [MOTION_CONTRACT.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/MOTION_CONTRACT.md) |
| §6 — motion source and generator interfaces | `usd-motion-plugins`: [DESIGN_POLICY.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/DESIGN_POLICY.md) |
| §8.1, §8.2, §8.4 — adapters, product names, generation adapters | `motion-connectors`: [DESIGN_POLICY.md](https://github.com/animu-sphere/motion-connectors/blob/main/docs/design/DESIGN_POLICY.md) and [CONNECTOR_CONTRACT.md](https://github.com/animu-sphere/motion-connectors/blob/main/docs/design/CONNECTOR_CONTRACT.md) |
| §8.3 — recorded files (BVH) | `usd-motion-plugins`: [DESIGN_POLICY.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/DESIGN_POLICY.md) §26–§27 |
| §10 — retarget core | generic: `usd-motion-plugins` [RETARGETING_POLICY.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/RETARGETING_POLICY.md); VRM binding, expressions, look-at: [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md) §4–§6 |
| §11.1, §11.4 — `execMotion`, no I/O in a computation | `usd-motion-plugins`: [EXEC_CONTRACT.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/EXEC_CONTRACT.md) |
| §11.2, §11.5 — `execVrm`, `ExecIr` | this repository: [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md) §7–§8 |
| §13 — motion plans on the stage | `usd-motion-plugins`: [USD_MAPPING.md](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/USD_MAPPING.md) |
| §14–§15 — layout and dependency edges | [architecture/WORKSPACE.md](../architecture/WORKSPACE.md) |
| §16 — Motion Phase A–H | retired; the history is in [archive/motion-split/](../archive/motion-split/) and the [release records](../releases/) |
| §18 — key decisions | 1–5, 19, 20: [VRM_MOTION_POLICY.md](VRM_MOTION_POLICY.md) §9; the rest: `usd-motion-plugins`' design policy |

**History.** The full text is in git history; the last revision is
[e98db79](https://github.com/animu-sphere/usd-vrm-plugins/blob/e98db79635e431b958c0cf64f088c822a9f73cf2/docs/design/MOTION_ARCHITECTURE_POLICY.md).
Its section numbers are the ones older documents in this repository cite.
