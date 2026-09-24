# motion_retarget test fixtures

Every fixture here is hand-authored except one.

## `recorded_session_clip.usda`

This is a motion stage exactly as `usd-motion-plugins`' published recorder
wrote it, byte for byte. `motion_retarget_design_triplet` bakes it onto
`tests/motion/fixtures/design_triplet/avatar.usda`. The bake is what shows that a live
session still reaches a VRM rig through this tool unchanged. Until MIG-4, that
claim was made by `motion_capture_replay`, which ran `tools/motionCapture` and
then this tool. The recorder has since left the repository
([motion-foundation-split.md §6](../../../../docs/archive/motion-split/motion-foundation-split.md#6-mig-4--recording-and-live-input-)),
so what is committed is its output. `ost` has no way for this workspace to
consume another repository's tool.

| | |
| --- | --- |
| Tool | `motion_record` 0.5.0, `usd-motion-plugins` release `v0.5.0` |
| Archive | `motion_record-0.5.0-cy2026-windows-x86_64-py313-usd.tar.zst`, `sha256:24d6e280b3fa1323722cb115dfd0422f2e2c963cdad173fa448da52e875e5b5b` |
| Input | `walk-clean-30hz.trace` from that repository's `libs/motionRecording/tests/corpus` |
| Command | `motion_record --trace walk-clean-30hz.trace --output recorded_session_clip.usda` |

The command was run from the trace's own directory, so the stage's
`source:trace` records a file name and not a machine's path. Regenerate the
file the same way when the recorder's stage shape changes, and update this table
in the same change.
