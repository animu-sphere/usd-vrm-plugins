# Motion source profiles

One declarative file per producer **and export preset**: what that application's
recorded output means. Joint names, units, axes and root conventions are facts
about the *writer* rather than about a file format, so they live here as data and
the code that reads them never has a name for any of them
([WORKSPACE.md §1](../../docs/architecture/WORKSPACE.md),
[recorded-motion-sources.md §3](../../docs/roadmap/recorded-motion-sources.md)).

This is the one place a product name may appear outside `adapters/`. The rule it
looks like it breaks forbids product-conditional *code*; a profile is a
declaration, and the test of whether the line has been crossed is that shipping
every file in this directory leaves `motionSource` and `motionBvh`
byte-identical.

## What is here

| Profile | Written from |
| --- | --- |
| [`mocopi-mobile-bvh-default-v1.yaml`](mocopi-mobile-bvh-default-v1.yaml) | one 17-second export measured in [the recorded corpus](../../libs/motionBvh/tests/corpus/recorded/manifest.json) (2026-08-04) |
| [`bandai-namco-research-bvh-motiondataset-v1.yaml`](bandai-namco-research-bvh-motiondataset-v1.yaml) | two exports, one from each half of the same dataset, measured in the same corpus and **not committed** — their licence is non-commercial (2026-08-05) |

BVH-0's release condition is **two producers**, because every assumption a
single export makes — joint names, unit, axes, root policy, hierarchy shape — is
indistinguishable from a property of the format until something disagrees with
it. The second one disagreed about three of them, and two were contract changes
rather than profile ones: it splits the body's placement across a reference node
and a hips, and its offsets compose into no pose at all, so `rest-offsets` is
unavailable to it. Both are settled in
[MOTION_CONTRACT.md](../../docs/design/MOTION_CONTRACT.md#recorded-source-rest-pose-and-the-path-rule-v070).

The second profile also shows what a *second file from the same producer* is
for. Its two rows are one walk and one standing clip, and neither alone
describes the export: in the walk the reference node carries all the
locomotion and looks like the body's root, and in the standing clip the same
node is zero in all six channels and looks like a dead node.

## The file

The keys, the small language they are written in, and what a file that gets one
wrong is told are all in
[`SourceProfileFile.h`](../../libs/motionSource/include/motionSource/SourceProfileFile.h).
Two properties are worth knowing before writing one:

- **Nothing is assumed.** Every convention has an `unspecified` value that
  validation refuses, so a profile nobody finished is a refusal rather than a
  silent set of answers. There is no default profile and no automatic fallback —
  a caller names one, or the conversion is refused.
- **An unknown key is an error, not a warning.** A misspelled `requred:` that a
  permissive reader dropped would unbind a joint the profile called mandatory and
  report nothing, and a near-miss profile produces motion that is *misassembled*
  rather than absent — which is worse than a refusal because it looks like a
  result.

The file name is the profile's `id`, and the id is
`<producer>-<format>-<preset>-v<N>`. Every part is load-bearing: an application's
export presets can disagree with each other and two applications can agree, so a
producer is not a profile. An application *version* is not in the id — that goes
in the corpus manifest, because a version that changes nothing about the output
contract must not fragment the profile set. `v<N>` moves only when a producer's
output contract breaks.

A user-defined profile is a file of exactly this shape; nothing here is
privileged over one written elsewhere.

## What checks them

| Check | Claims |
| --- | --- |
| `motionSource_shippedProfiles` | every file here is one the library can read, and its id is its file name |
| `workspace_motion_profiles` | each profile describes the recorded file that names it: the root, every mapped and ignored joint, no joint left neither, and the hierarchy the mapping implies — and that a real YAML implementation reads the file the same way |

**Neither checks what a profile says**, and that is deliberate rather than a
gap. A test asserting that this rig's root maps to `hips` would be the library or
its suite learning a producer, which is the one thing the layer is built not to
do. What is checked is the shape of the claim: that the file loads, and that the
rig it describes is the rig in the corpus.

The second check reads both the profile and the recorded file itself
([`scripts/check_motion_profiles.py`](../../scripts/check_motion_profiles.py)),
rather than calling into the libraries that will: a profile checked with the same
parser the pipeline uses would be two implementations agreeing with each other.
It is also the one check that may hold a reader's file and a profile at once,
which is why it is a script and not a library test.

Its third reading is a real YAML implementation, where one is installed. The
library's reader is a stated subset written rather than borrowed — a profile
needs an unknown key to be an error, and YAML's implicit typing would read a
joint named `on`, `y` or `null` as something other than its name — so the claim
is not "everything YAML accepts, this accepts". It is that **a file this accepts
means to it what it means to YAML**: a refusal is a bad file's worst outcome, and
a silent difference of interpretation is the failure a hand-written reader can
produce that a refusal cannot.
