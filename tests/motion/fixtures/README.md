# Motion test fixtures

Every fixture here is hand-authored except one.

## `mocopi-mobile-arm-raise-turn.usda`

This is a semantic clip exactly as `usd-motion-plugins`' published converter
wrote it, byte for byte. It is the recorded input of the tests that bake a real
capture onto a rig: `workspace_bvh_end_to_end`, `workspace_real_avatar_bake`,
`workspace_unicode_paths`, the three `workspace_exec_parity_recorded_*` cases
and both artifact-only release smokes. Until MIG-3 those tests ran
`motion_bvh_convert` over the BVH export first. The converter, the BVH reader,
the source layer and the producer profiles have since left this repository
([motion-foundation-split.md §5](../../../docs/roadmap/motion-foundation-split.md#5-mig-3--recorded-sources-)),
so what is committed is the converter's output. `ost` has no way for this
workspace to consume another repository's tool.

| | |
| --- | --- |
| Tool | `motion_convert` 0.5.0, `usd-motion-plugins` release `v0.5.0` |
| Archive | `motion_convert-0.5.0-cy2026-windows-x86_64-py313-usd.tar.zst`, `sha256:06324f0c52e3cd54bf622d0c24c8107d6c1b4cc740a48964cdcde395233bec10` |
| Input | `mocopi-mobile-arm-raise-turn.bvh` from that repository's `libs/motionBvh/tests/corpus/recorded/redistributable`, and the profile `mocopi-mobile-bvh-default-v1` from its `profiles/motion` |
| Command | `motion_convert mocopi-mobile-arm-raise-turn.bvh --profile mocopi-mobile-bvh-default-v1 --profile-dir <profiles/motion> --output mocopi-mobile-arm-raise-turn.usda` |

The command was run from the BVH's own directory, so the clip's
`source:sourceId` records a file name and not a machine's path. The profile is
named by id, so no path of it is recorded either. The export is the one its
capture's owner cleared for publication in this repository on 2026-08-04; the
clip is that capture, converted, and carries the same clearance.

Before this file replaced the in-tree converter, every test listed above was
run against it with the published executable in the converter's place, and all
of them passed unchanged. Regenerate the file the same way when the converter's
stage shape changes, and update this table in the same change.

## The rest

`humanoid_avatar.usda` and `humanoid_avatar_map.json` are the fixture rig
`workspace_bvh_end_to_end` and the parity cases bake onto. It is shaped so a
broken rest-pose correction cannot pass: its arms rest 45 degrees down where
the recorded rig's rest is straight.
