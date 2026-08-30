# Recorded sessions

Real traffic, from a real sender. The split below is about redistribution and
never about worth: a session that may be published lives in
`redistributable/`, and one that may not leaves a manifest in `manifests/` and
no bytes anywhere.

The minimum this directory owes VRC-1 is one real mocopi `VRChat (OSC)` session
covering the same takes the native corpus covers — neutral standing, head turn,
arm raise, walk with root motion, restart. **That session was recorded on
2026-08-30** and survives as
[`manifests/2026-08-30-mocopi-vrchat-osc.json`](manifests/2026-08-30-mocopi-vrchat-osc.json)
and [report 02](../../../../../../docs/reports/motion/02-2026-08-30-vrchat-osc-address-inventory.md).
Its bytes are not here and will not be, on the argument the mocopi corpus
already makes: a tracker stream is a real person's motion in a real room.

**This paragraph used to require the same physical take as the native UDP
recording, and that condition is not producible on this sender.** Two product
behaviours were measured in that session and they compose: the transfer format is
exclusive, so the native wire and `VRChat (OSC)` cannot run at once; and this
application records no BVH while it is sending OSC, so two takes cannot be
chained through a common file export either — which is exactly how
[report 01](../../../../../../docs/reports/motion/01-2026-08-15-mocopi-cross-source.md)
compared the native wire against a BVH export of one take. The VRChat OSC path
therefore cannot share a physical take with any other observation of the same
motion.

What replaces it is a smaller claim, stated rather than hedged: the takes here
carry the **same labelled sequences** as the native corpus, performed separately.
A comparison across them is between two performances of one sequence, and every
conclusion drawn from it has to survive that — which rules out the per-sample
timing agreement report 01 was able to measure, and leaves what each path
carries and drops, which is what
[§11](../../../../../../docs/roadmap/osc-and-vrchat-trackers.md#11-the-fourth-observation-of-one-session)
exists to write down.
