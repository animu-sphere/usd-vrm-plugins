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

One producer is not the milestone: BVH-0's release condition is **two**, because
every assumption a single export makes — joint names, unit, axes, root policy,
hierarchy shape — is indistinguishable from a property of the format until
something disagrees with it.

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
| `workspace_motion_profiles` | each profile describes the recorded file that names it: the root, every mapped and ignored joint, no joint left neither, and the hierarchy the mapping implies |

The second reads both the profile and the recorded file itself
([`scripts/check_motion_profiles.py`](../../scripts/check_motion_profiles.py)),
rather than calling into the libraries that will: a profile checked with the same
parser the pipeline uses would be two implementations agreeing with each other.
It is also the one check that may hold a reader's file and a profile at once,
which is why it is a script and not a library test.
