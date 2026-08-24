# Session manifests

What survives of a session whose bytes may not be committed: the measurements,
the hashes, and enough provenance for a later reader to tell whether a claim
still holds without the bytes.

One JSON file per session. `vrmAdapterMocopi`'s
[`tests/corpus/recorded/manifest.json`](../../../../../mocopi/tests/corpus/recorded/manifest.json)
is the shape to follow — format, recording tool and commit, measuring tool and
commit, session facts, and an explicit `redistributionAllowed`. Every numeric
field in one of these is a reading taken by a shipped tool, never a note taken
during the session.
