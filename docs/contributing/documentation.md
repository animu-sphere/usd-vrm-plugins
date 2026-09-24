# Documentation guidelines

Documentation is part of the implementation contract. A change is incomplete
if it changes a public boundary, the authored stage, implemented architecture
or delivery status without updating the page that owns it.

**One concept, one owning repository, one canonical document.** Everything
below follows from that.

## Category ownership

| Category | Put this here | Not this |
| --- | --- | --- |
| root `README.md` | What this repository is, what it owns and does not, a small conceptual diagram, a component responsibility table, links, a minimal build entry point, the licence | See [root README](#root-readme) |
| `docs/README.md` | Which document owns which subject | Content of its own |
| `design/` | Intended contracts, their rationale, the evidence they rest on, open questions | Claims that something is implemented |
| `architecture/` | Component identities, dependency edges, packaging — the binding structural contract | Rationale; plans |
| `reference/` | Facts about the current tree: capabilities, supported configurations | Plans; a sibling repository's status |
| `roadmap/` | Incomplete work this repository owns, and which release carries it | Completed work; rationale; a sibling's roadmap |
| `guides/` | How to accomplish a task, with commands that have been run | Commands nobody has run |
| `releases/` | One immutable record per released version | Work in progress |
| `reports/` | Dated measurements and observations: real runs, hardware sessions, protocol captures, OpenUSD release audits, build and delivery evidence | Current-state claims |
| `archive/` | Plans and documents that were once authoritative and no longer are | Anything a reader should act on |
| `contributing/` | How to maintain this repository | End-user tasks |

The same fact is not maintained independently in two categories.

## Cross-repository contracts

`usd-vrm-plugins`, `usd-motion-plugins` and `motion-connectors` each own a
set of subjects ([scope policy §13](../design/INTEGRATION_SCOPE_POLICY.md#13-place-among-the-motion-repositories)):

| Repository | Owns |
| --- | --- |
| `usd-motion-plugins` | What generic motion **is**: `MotionPose`, `MotionClip`, `MotionStream`, `HumanJoint`, root motion, sampling, filtering, recording, generic retargeting, generic motion files, the OpenUSD motion mapping, generic OpenExec evaluation |
| `motion-connectors` | How external motion **enters**: connector interfaces, `MotionFrame`, source state and timestamps, buffering, protocols and devices, source profiles and coordinate conversion, tracker observations |
| `usd-vrm-plugins` | How VRM **uses** motion: `.vrm` and `.vrma`, the VRM schemas, VRM humanoid semantics and required bones, expressions, look-at, resource resolution, VRM rig binding, `execVrm` |

> A repository may describe how it consumes a sibling repository's contract,
> but must not redefine that contract.
>
> Link to the owning repository instead of copying its API semantics,
> capability status, roadmap, or implementation state.

Good: "`motion_retarget` raises the caller's codes of `usd-motion-plugins`'
[RETARGETING_POLICY.md §7](https://github.com/animu-sphere/usd-motion-plugins/blob/main/docs/design/RETARGETING_POLICY.md#7-diagnostics)."
Bad: a table of what `MotionPose` contains, maintained here.

Link to a sibling's **canonical** document, never to one it has archived or
superseded. Cite a sibling's measurement where it was made, and do not restate
it as this repository's finding.

## Root README

The root README is an entry point. It follows the shared shape — **Scope**,
**Architecture**, **Components**, **Documentation**, **Build**, **License** —
and stays short. It does not carry:

- migration history, completed phases, or long explanations of moved
  components;
- release-by-release history, or version status prose ("v0.9.0 is …",
  "vX is in progress") — link to the capability matrix, the roadmap and the
  changelog instead;
- status columns in the component table (Shipped, Experimental, a version);
- detailed dependency graphs or contract definitions;
- a sibling repository's contracts or status;
- retired component names, except where compatibility requires one.

`scripts/check_docs.py` enforces the parts of this that a pattern can see.

## Reports and the archive

A **report** is dated, append-only evidence: what was measured, where and
when. It is never authoritative for current status, and it stays where it was
produced when ownership of the code it measured moves — **contracts move to
the current owner; evidence stays with its provenance.** A later finding gets
a new report and a one-line forward note on the old one. The only edit an
existing report receives is a link repair when a document it cites moves.

The **archive** holds intent that has been done or replaced: completed plans,
migration tracks, retired proposals, former roadmap documents. Every archived
document opens with a *Historical only* banner, and no current document cites
one as policy. A superseded **design** document is not archived; it stays at
its path as a short stub — status, former purpose, current owner, links to the
replacement — so an old citation still leads somewhere.

When a plan completes, its open remainder moves to [roadmap/](../roadmap/)
and the plan moves to [archive/](../archive/) in the same change.

## Metadata

A design document, a superseded stub and an archived document carry YAML
front matter:

```yaml
---
status: binding        # proposed | accepted | binding | superseded | rejected | historical
owner: usd-vrm-plugins # the repository that owns the subject
canonical: VRM_MOTION_POLICY.md   # superseded only: the replacement, relative
---
```

`check_docs.py` requires a superseded document to name a `canonical` that
exists, and an archived one to be `historical`.

## Naming

- Phase identifiers are always qualified by their sequence: Product P4,
  Workspace Phase 5. A sibling's sequence carries the sibling's name.
- Once a sequence completes, its vocabulary appears only in the archive,
  release records and reports.

## Language and form

- Repository documents are in English.
- Relative links for everything in the repository; code spans for commands,
  paths, targets, types and diagnostic codes.
- Keep each category index (`docs/README.md`, `roadmap/README.md`,
  `archive/README.md`, `releases/README.md`) in sync with its files.
- Never commit machine-local paths, or a capture, motion or model whose terms
  do not allow redistribution.

## Change checklist

1. Planned behaviour is not presented as implemented.
2. Every new page appears in its category index.
3. Relative links resolve (`python scripts/check_docs.py`).
4. Implementation changes update `architecture/` and `reference/`.
5. Completed work leaves `roadmap/`; a completed plan moves to `archive/`.
6. A sibling's contract is linked, not restated.
7. A change to a section a sibling repository cites is checked against the
   citation.
