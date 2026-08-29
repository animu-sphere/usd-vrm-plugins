# USD VRM Plugins documentation

Documentation is organized by responsibility: each category answers one class of
question. This mirrors the layout used across `open-strata` and `hydra-merlin`,
so the three repositories read the same way.

When a summary disagrees with the implementation, the implementation wins and
the summary is a bug. When a summary disagrees with
[architecture/WORKSPACE.md](architecture/WORKSPACE.md) about *structure*, the
contract wins — structural changes go there first, in their own PR.

| Category | Answers | Start here |
| --- | --- | --- |
| [architecture/](architecture/) | How the workspace is structured: bundle identities, dependency directions, artifact naming — and what each installed package promises a consumer. | [WORKSPACE.md](architecture/WORKSPACE.md) · [PACKAGE_CONTRACT.md](architecture/PACKAGE_CONTRACT.md) |
| [guides/](guides/) | How to accomplish a task. | [INSTALL.md](guides/INSTALL.md) · [VIEWING_MOTION.md](guides/VIEWING_MOTION.md) |
| [reference/](reference/) | Factual contracts: what is supported, on what. | [CAPABILITY_MATRIX.md](reference/CAPABILITY_MATRIX.md) |
| [roadmap/](roadmap/) | What is planned next (only incomplete work). | [README.md](roadmap/README.md) |
| [releases/](releases/) | Immutable per-version release records. | [README.md](releases/README.md) |
| [design/](design/) | Why significant decisions were made. | [DESIGN_POLICY.md](design/DESIGN_POLICY.md) · [MATERIAL_ARCHITECTURE_POLICY.md](design/MATERIAL_ARCHITECTURE_POLICY.md) |
| [reports/](reports/) | Evidence from real runs: the `ost` dogfooding series, OpenUSD release audits, and the delivery log. | [README.md](reports/README.md) |
| [contributing/](contributing/) | Contributor procedures. | [RELEASE_NOTES_TEMPLATE.md](contributing/RELEASE_NOTES_TEMPLATE.md) |

## Canonical documents

- [design/DESIGN_POLICY.md](design/DESIGN_POLICY.md) is the long-form design &
  development policy for the **importer** — the source of truth for **Product
  P0–P6** and for the import / evaluation / simulation boundary.
- [design/MOTION_ARCHITECTURE_POLICY.md](design/MOTION_ARCHITECTURE_POLICY.md)
  is the policy for everything **below** the importer — `.vrma` import, the
  vendor-neutral motion core, retargeting, and the OpenExec runtime. Source of
  truth for **Motion Phase A–H**. It extends DESIGN_POLICY §10 and restructures
  §17-P4; where the two overlap, the motion policy wins.
- [design/MATERIAL_ARCHITECTURE_POLICY.md](design/MATERIAL_ARCHITECTURE_POLICY.md)
  is the policy for the **look** side — the `/Asset/mtl` hierarchy, the split
  between VRM source semantics and render realizations, and the schema surface
  that carries MToon. It extends DESIGN_POLICY §9 and restructures §17-P5; where
  the two overlap, the material policy wins. It adds **no** phase sequence: its
  three steps are the internal order of Product P5.
- [architecture/WORKSPACE.md](architecture/WORKSPACE.md) is the binding workspace
  contract — the source of truth for bundle identities, dependency directions,
  artifact naming, and **Workspace Phase 0–8**.
- [architecture/PACKAGE_CONTRACT.md](architecture/PACKAGE_CONTRACT.md) is the
  binding **distribution** contract — per package, the name a consumer writes in
  `find_package`, the target it links, the header root, the packages that must
  resolve first, and whether standalone installability has been *measured* or
  only reviewed. It answers "what do I write to consume this", which WORKSPACE.md
  §5 was repeatedly asked and never stated; §5 keeps naming and aggregate
  membership. Added 2026-08-29.

Five milestone plans sit in [roadmap/](roadmap/) rather than here, because they
are plans and not policy: the [packaging hardening lane](roadmap/packaging-hardening.md),
the [live input adapters](roadmap/adapters-mocopi-vmc-ardy.md),
the [recorded motion sources](roadmap/recorded-motion-sources.md), the
[shared OSC foundation and VRChat OSC Trackers input](roadmap/osc-and-vrchat-trackers.md),
and the [OpenExec foundation](roadmap/openexec-foundation.md). All five defer
every structural claim to WORKSPACE.md and every motion claim to the motion
policy, and **none states its own release version** — that is the
[roadmap status table](roadmap/README.md#status-at-a-glance), which exists
because two of them traded places once already, and has since re-ordered twice
more.

The three input tracks split the layer twice, and each split is a difference in
what the code argues about rather than an organisational preference. **Live
versus recorded**: one argues about packets and restarts, the other about a
hierarchy and a rest pose. **Pose versus tracker**, within live: VMC and mocopi
transport humanoid bone transforms, where VRChat OSC carries numbered tracker
observations that are pre-IK, and a tracker index is not a body role. All three
complete to a retargeted `UsdSkelAnimation` with no OpenExec dependency, and all
three converge at `motionCore` and nowhere earlier
([motion policy §8.3](design/MOTION_ARCHITECTURE_POLICY.md)).

The OSC track is also the one that owns *sharing*. It carries the extraction of
the OSC decoder and of the transport code the first two live adapters already
duplicate, which is a structural change and therefore reaches
[WORKSPACE.md](architecture/WORKSPACE.md) §1 and §2 before it reaches any
adapter.

The packaging track is the odd one out and is deliberately first. It adds no
input, no format and no node: it checks that the boundaries the other four
created can be consumed from **outside** this repository, which nothing here has
ever done — a composed build resolves every target in-tree and never opens a
package config file, so a package can name an unresolvable target and CI stays
green. It did, on 2026-08-29.

The OpenExec track attaches to that finished pipeline afterwards, and since
2026-08-03 it is also *scheduled* afterwards, so its parity comparison runs on
sessions recorded from a real device rather than on generated fixtures. That is a
one-way relationship: the input tracks supply evidence, and nothing in them reads
back. Since 2026-08-29 it is scheduled behind the *producer* tracks as well, for
the same reason one step further out: a compute layer over contracts that are
still moving evaluates a boundary twice instead of once.

The three phase systems are separate and always qualified; see
[roadmap/README.md](roadmap/README.md#three-sequences-deliberately-separate).

## Per-bundle documentation

Bundle-specific docs live with their bundle, not here:

| Bundle | Docs |
| --- | --- |
| `vrmSchema` | [README](../plugins/vrmSchema/README.md) · [SCHEMA_CONTRACT.md](../plugins/vrmSchema/docs/SCHEMA_CONTRACT.md) |
| `usdVrmFileFormat` | [README](../plugins/usdVrmFileFormat/README.md) · [DIAGNOSTICS.md](../plugins/usdVrmFileFormat/docs/DIAGNOSTICS.md) · [CORPUS.md](../plugins/usdVrmFileFormat/tests/corpus/CORPUS.md) |
| `usdVrmPackageResolver` | [README](../plugins/usdVrmPackageResolver/README.md) |
| `vrmContainer` | [README](../libs/vrmContainer/README.md) |
