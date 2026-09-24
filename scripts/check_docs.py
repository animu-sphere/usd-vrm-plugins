#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check the documentation against the manifests — contract values only.

The docs are hand-written on purpose; this does not generate them. It asserts
the handful of facts that silently rot when the workspace changes shape:

1. every bundle/library identity in the manifests appears in the root README's
   component table and in WORKSPACE.md's identity table;
2. no current-state document describes `usdVrm` as a bundle id or points at the
   pre-rename `plugins/usdVrm/` path (history is exempt — see HISTORY);
3. the schema contract version in the docs matches vrmSchema's manifest;
4. *(retired: the mocopi rig agreement left with the adapter and the profile;
   see the comment where it stood)*;
5. the OpenUSD pin agrees across the bundle manifests, the configure-time
   contract module, and the supported-configurations reference;
6. the roadmap and the release records agree about which versions are out:
   VERSION has a record and a changelog section, a `Next:` / `Then:` milestone
   is not an already-released version, a `Shipped:` one is, the roadmap status
   table agrees with those headings, and no document points at a retired
   roadmap filename;
7. every identity that installs a CMake package has a row in
   PACKAGE_CONTRACT.md, and every row that is not reserved names an identity
   the manifests declare;
8. every local markdown link resolves;
9. the root README stays an entry point: no release-version prose, no status
   column, no identity that has left this repository, no retired phase
   vocabulary, and no link into the archive
   (docs/contributing/documentation.md, "Root README");
10. front matter says what a document is: an archived document is
    `historical` and says so in a banner, a superseded one names a
    `canonical` replacement that exists and holds no parallel copy of what it
    replaced, and every `status` is one of the known values;
11. every roadmap document other than the index still holds incomplete work,
    and nothing that states current status (reference/) cites the archive.

Check 6 exists because the roadmap said "Next: v0.6.0 - the OpenExec
foundation" for two weeks after v0.6.0 shipped VMC input instead. Nothing was
wrong with any single document; the pair had drifted, and only a reader holding
both noticed.

Run: python scripts/check_docs.py   (exit 1 on any failure)
"""
from __future__ import annotations

import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]

# Historical records. They describe the world as it was and must not be
# rewritten to match the world as it is (see docs/reports/README.md).
HISTORY = (
    "docs/reports/",     # dated dogfooding evidence + delivery log
    "docs/releases/",    # immutable per-version release records
    "docs/archive/",     # superseded plans, kept for traceability
    "CHANGELOG.md",      # released sections are history
)


def is_history(rel: str) -> bool:
    rel = rel.replace("\\", "/")
    return any(rel.startswith(h) or rel == h for h in HISTORY)


# Generated trees that contain COPIES of authored files. Nothing here is
# authored, so nothing here is checked: a copy's relative links resolve against
# its original's directory and are broken by construction wherever it was
# staged. `.strata/` began carrying one when openstrata.toml grew
# `[[workspace.install_data]]` -- packaging staged `profiles/motion/` whole, and
# its README.md linked five files by repository-relative path. The profiles left
# with MIG-3; `.strata/` stays generated either way.
GENERATED = ("build/", "scratch/", ".strata/", "dist/", ".ost-ci/")


def is_generated(rel: str) -> bool:
    return rel.replace("\\", "/").startswith(GENERATED)


def read(rel: str) -> str:
    return (REPO_ROOT / rel).read_text(encoding="utf-8")


def scalar(text: str, key: str) -> str | None:
    """Pull `  key: value` out of a manifest without a YAML dependency."""
    m = re.search(rf"^\s*{re.escape(key)}:\s*(.+?)\s*$", text, re.M)
    return m.group(1).strip().strip("'\"") if m else None


def discover() -> tuple[dict[str, str], dict[str, str]]:
    """Return ({bundle id: manifest path}, {library id: descriptor path})."""
    bundles, libraries = {}, {}
    for m in sorted(REPO_ROOT.glob("plugins/*/openstrata.plugin.yaml")):
        name = scalar(m.read_text(encoding="utf-8"), "name")
        if name:
            bundles[name] = str(m.relative_to(REPO_ROOT)).replace("\\", "/")
    # Adapters are plain libraries too (WORKSPACE.md §1), one directory deeper:
    # adapters/<group>/<name>/. Globbing them here is what makes an adapter
    # identity subject to the same README/WORKSPACE inventory rule as every
    # other one.
    descriptors = sorted(REPO_ROOT.glob("libs/*/openstrata.library.yaml"))
    descriptors += sorted(REPO_ROOT.glob("adapters/*/*/openstrata.library.yaml"))
    for m in descriptors:
        lid = scalar(m.read_text(encoding="utf-8"), "id")
        if lid:
            libraries[lid] = str(m.relative_to(REPO_ROOT)).replace("\\", "/")
    return bundles, libraries


def check_inventory(failures: list[str]) -> None:
    bundles, libraries = discover()
    if not bundles:
        failures.append("no plugin manifests found under plugins/*/ — "
                        "has the layout moved?")
        return

    readme = read("README.md")
    workspace = read("docs/architecture/WORKSPACE.md")
    for ident, manifest in {**bundles, **libraries}.items():
        if not re.search(rf"`{re.escape(ident)}`", readme):
            failures.append(
                f"README.md does not mention `{ident}` (declared in {manifest})")
        if not re.search(rf"`{re.escape(ident)}`", workspace):
            failures.append(
                f"docs/architecture/WORKSPACE.md does not mention `{ident}` "
                f"(declared in {manifest})")

    # usdVrm is the aggregate product name, never a bundle id.
    if "usdVrm" in bundles:
        failures.append(
            "plugins/*/openstrata.plugin.yaml declares a bundle named `usdVrm`; "
            "the aggregate product name must not be a bundle id "
            "(docs/architecture/WORKSPACE.md §1)")


def check_no_stale_paths(failures: list[str]) -> None:
    """The pre-rename bundle path must not survive in current-state docs."""
    stale = re.compile(r"plugins/usdVrm(?![A-Za-z])")
    for p in sorted(REPO_ROOT.glob("**/*.md")):
        rel = str(p.relative_to(REPO_ROOT)).replace("\\", "/")
        if is_generated(rel) or is_history(rel):
            continue
        for i, line in enumerate(p.read_text(encoding="utf-8",
                                             errors="replace").splitlines(), 1):
            if stale.search(line):
                failures.append(
                    f"{rel}:{i} references the pre-rename path `plugins/usdVrm` "
                    f"(now plugins/usdVrmFileFormat)")


def check_schema_contract(failures: list[str]) -> None:
    manifest = read("plugins/vrmSchema/openstrata.plugin.yaml")
    contract = scalar(manifest, "contract")
    if not contract:
        failures.append("plugins/vrmSchema/openstrata.plugin.yaml declares no "
                        "schema.contract")
        return
    doc = read("plugins/vrmSchema/docs/SCHEMA_CONTRACT.md")
    if not re.search(rf"\bv?{re.escape(contract)}\b", doc.split("\n")[0]):
        failures.append(
            f"SCHEMA_CONTRACT.md's title does not name contract version "
            f"{contract} (from the vrmSchema manifest): {doc.splitlines()[0]!r}")
    ffmt = read("plugins/usdVrmFileFormat/openstrata.plugin.yaml")
    pinned = scalar(ffmt, "contract")
    if pinned != contract:
        failures.append(
            f"usdVrmFileFormat pins schema contract {pinned!r} but vrmSchema "
            f"provides {contract!r}")


# --- one rig, described in three places --------------------------------------
#
# The mocopi rig agreement -- the live adapter's joint table, the recorded
# profile, the committed BVH export -- has no leg left here. The adapter left
# with MIG-4 for `motion-connectors`, and the profile and the export with MIG-3
# for `usd-motion-plugins`, whose `workspace_motion_profiles` holds every
# shipped profile against the export it describes (a joint neither mapped nor
# ignored fails it), which is what this check did.


def check_openusd_pin(failures: list[str]) -> None:
    """One OpenUSD version, said the same way in all four places.

    Since v0.6.0 the version is a pin rather than a range, and it is asserted
    by two independent mechanisms — `ost` reads the manifests, a plain CMake
    build reads cmake/UsdVrmOpenUsd.cmake. Nothing makes them agree, and a
    half-bumped pair fails only on whichever build path the next person takes.
    """
    module = read("cmake/UsdVrmOpenUsd.cmake")
    m = re.search(r'set\(USDVRM_OPENUSD_REQUIRED_RELEASE\s+"([^"]+)"\)', module)
    if not m:
        failures.append("cmake/UsdVrmOpenUsd.cmake declares no "
                        "USDVRM_OPENUSD_REQUIRED_RELEASE")
        return
    release = m.group(1)

    packed = re.search(
        r"set\(USDVRM_OPENUSD_REQUIRED_PXR_VERSION\s+(\d+)\)", module)
    if not packed:
        failures.append("cmake/UsdVrmOpenUsd.cmake declares no "
                        "USDVRM_OPENUSD_REQUIRED_PXR_VERSION")
    elif packed.group(1) != release.replace(".", ""):
        # 26.08 -> 2608. OpenUSD's own packed form; they cannot disagree.
        failures.append(
            f"cmake/UsdVrmOpenUsd.cmake pins release {release!r} but "
            f"PXR_VERSION {packed.group(1)!r}")

    expected = f"=={release}"
    for name, manifest in sorted(discover()[0].items()):
        declared = scalar(read(manifest), "openusd")
        if declared != expected:
            failures.append(
                f"{manifest} declares openusd {declared!r}; the workspace pin "
                f"is {expected!r} (cmake/UsdVrmOpenUsd.cmake). Bundle {name}.")

    # The reference's OpenUSD table is the row a reader trusts, so require the
    # pin *there* rather than anywhere in the prose -- the prose also names the
    # retired range, on purpose, to say that it is retired.
    doc = "docs/reference/SUPPORTED_CONFIGURATIONS.md"
    rows = [ln for ln in read(doc).splitlines()
            if ln.startswith("|") and expected in ln]
    if not rows:
        failures.append(
            f"{doc} has no table row stating the OpenUSD pin {expected!r}")

    # The README's badge states the pin to every visitor before they open a
    # single document, so it is a mirror like the manifests and drifts like
    # one. Both halves: shields.io renders the label, and the alt text is what
    # a reader without images sees.
    readme = read("README.md")
    for pattern, what in ((r"badge/OpenUSD-([0-9.]+)-", "badge"),
                          (r"!\[OpenUSD ([0-9.]+)\]", "badge alt text")):
        found = re.search(pattern, readme)
        if not found:
            failures.append(f"README.md has no OpenUSD {what}")
        elif found.group(1) != release:
            failures.append(
                f"README.md's OpenUSD {what} states {found.group(1)!r}; the "
                f"workspace pin is {release!r} (cmake/UsdVrmOpenUsd.cmake)")


# --- roadmap / release drift -------------------------------------------------

# Roadmap documents that were renamed when their target version moved. The name
# must survive nowhere except in the two documents whose job is to record the
# rename; anywhere else it is a reader following a path that no longer exists.
RETIRED_DOC_NAMES = {
    "openexec-v0.6.0-v0.7.0.md": (
        "docs/archive/motion-split/openexec-foundation.md",
        "docs/archive/motion-split/roadmap-orderings.md",
    ),
}

VERSION_RE = re.compile(r"v(\d+\.\d+\.\d+)")
# "## Next: v0.7.0 - ..." / "## Then: ..." / "## Shipped: ..."
MILESTONE_HEADING = re.compile(
    r"^##\s+(Next|Then|Shipped):\s*v(\d+\.\d+\.\d+)\b", re.M)


def released_versions() -> set[str]:
    """Versions with a release record. A record exists only once cut."""
    return {p.stem.lstrip("v")
            for p in REPO_ROOT.glob("docs/releases/v*.md")}


def check_release_records(failures: list[str]) -> None:
    """VERSION, the release records, and the changelog name the same release."""
    version = read("VERSION").strip()
    released = released_versions()
    if version not in released:
        failures.append(
            f"VERSION is {version!r} but docs/releases/v{version}.md does not "
            f"exist. A released version needs a record (docs/releases/README.md)")
    if not re.search(rf"^##\s*\[{re.escape(version)}\]", read("CHANGELOG.md"), re.M):
        failures.append(
            f"CHANGELOG.md has no `## [{version}]` section, but VERSION says "
            f"{version!r}")


def check_roadmap_status(failures: list[str]) -> None:
    """The roadmap's next milestone is not a version that already shipped."""
    released = released_versions()
    current = read("docs/roadmap/current.md")

    headings = MILESTONE_HEADING.findall(current)
    if not headings:
        failures.append(
            "docs/roadmap/current.md has no `## Next: vX.Y.Z` heading; the "
            "next milestone is what this document is for")
        return

    planned = [v for kind, v in headings if kind in ("Next", "Then")]
    for kind, ver in headings:
        if kind in ("Next", "Then") and ver in released:
            failures.append(
                f"docs/roadmap/current.md says `{kind}: v{ver}` but "
                f"docs/releases/v{ver}.md exists — that version has shipped")
        if kind == "Shipped" and ver not in released:
            failures.append(
                f"docs/roadmap/current.md says `Shipped: v{ver}` with no "
                f"docs/releases/v{ver}.md to back it")

    # The status table is the single source for a track's target version, so
    # the Next milestone has to be one of the versions it lists as unshipped.
    table = read("docs/roadmap/README.md")
    targets = set(VERSION_RE.findall(table))
    unmet = [v for v in planned if v not in targets]
    if unmet:
        failures.append(
            "docs/roadmap/README.md's status table does not name "
            + ", ".join(f"v{v}" for v in unmet)
            + ", which docs/roadmap/current.md is planning. One table decides "
              "which release a track lands in")


def check_retired_doc_names(failures: list[str]) -> None:
    """A renamed roadmap document leaves no mentions behind it."""
    for name, allowed in RETIRED_DOC_NAMES.items():
        for p in sorted(REPO_ROOT.glob("**/*.md")):
            rel = str(p.relative_to(REPO_ROOT)).replace("\\", "/")
            if is_generated(rel) or rel in allowed:
                continue
            if is_history(rel):
                continue  # history keeps its own words; links are checked below
            for i, line in enumerate(
                    p.read_text(encoding="utf-8",
                                errors="replace").splitlines(), 1):
                if name in line:
                    failures.append(
                        f"{rel}:{i} names the retired roadmap document "
                        f"`{name}` (allowed only in {', '.join(allowed)})")


def check_component_status(failures: list[str]) -> None:
    """A version in the README's component table is a version that shipped."""
    released = released_versions()
    for i, line in enumerate(read("README.md").splitlines(), 1):
        if not line.startswith("|"):
            continue
        for ver in VERSION_RE.findall(line):
            if ver not in released:
                failures.append(
                    f"README.md:{i} gives a component the status v{ver}, which "
                    f"has no docs/releases/v{ver}.md. Planned work belongs in "
                    f"the roadmap, not in the component table")


CONTRACT = "docs/architecture/PACKAGE_CONTRACT.md"

# A package row: seven cells, the first one backticked. Matching on shape rather
# than on the header text keeps a future column rename out of this check, the
# same way scripts/check_package_consumer.py reads the table.
CONTRACT_ROW = re.compile(r"^\|(?P<cells>.+)\|\s*$")
DASH = ("-", "\u2014", "")


def contract_rows() -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    for line in read(CONTRACT).splitlines():
        m = CONTRACT_ROW.match(line.strip())
        if not m:
            continue
        cells = [c.strip() for c in m.group("cells").split("|")]
        if len(cells) != 7 or not cells[0].startswith("`"):
            continue
        name = cells[0].strip("`")
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9]*", name):
            continue
        rows[name] = {"target": cells[1].strip("`"), "product": cells[5]}
    return rows


def contract_configs() -> dict[str, str]:
    """{identity: path} for every installed CMake package template.

    Discovered on the same rule `discover()` uses, because a table of paths here
    would be the second place the workspace's shape is written down -- and this
    check exists precisely because a second place drifts."""
    configs: dict[str, str] = {}
    for pattern in ("libs/*/cmake/*Config.cmake.in",
                    "adapters/*/*/cmake/*Config.cmake.in",
                    "plugins/*/cmake/*Config.cmake.in"):
        for path in sorted(REPO_ROOT.glob(pattern)):
            name = path.name[: -len("Config.cmake.in")]
            configs[name] = str(path.relative_to(REPO_ROOT)).replace("\\", "/")
    return configs


def check_package_contract(failures: list[str]) -> None:
    """PACKAGE_CONTRACT.md §4 and the CMake sources describe the same packages.

    The document states, per package, what a consumer outside this workspace
    writes. A package that ships a config file and has no row is a promise
    nobody wrote down; a row naming a package that does not exist is a promise
    about nothing. Both are the class of drift check 6 catches between the
    roadmap and the release records, one directory over.

    A **reserved** row is exempt from the existence half by design: an identity
    arrives in WORKSPACE.md §1 first and reaches this document when it acquires
    an installed package, which for a reserved one is later or never
    (PACKAGE_CONTRACT.md §6)."""
    rows = contract_rows()
    if not rows:
        failures.append(f"{CONTRACT}: no package rows parsed out of §4 -- has "
                        f"the table's shape changed?")
        return
    bundles, libraries = discover()
    identities = {**bundles, **libraries}
    configs = contract_configs()

    for name, path in configs.items():
        if name not in rows:
            failures.append(
                f"{path} installs a CMake package `{name}` with no row in "
                f"{CONTRACT} §4. A package a consumer can find is a promise, "
                f"and this document is where the promise is written")
        elif "reserved" in (rows[name]["target"], rows[name]["product"]):
            # The exemption below is for an identity with no package yet. One
            # that installs a config has a package, so the row is out of date
            # rather than reserved -- and without this it would fall through
            # both halves of the check, which is the drift this exists to catch.
            failures.append(
                f"{CONTRACT} §4 marks `{name}` reserved, but {path} installs a "
                f"CMake package for it. A reserved identity is one with no "
                f"package yet (§6); fill the row in")
        elif rows[name]["target"] in DASH:
            failures.append(
                f"{CONTRACT} §4 says `{name}` exports no target, but {path} "
                f"installs a config for it")

    for name, row in rows.items():
        if "reserved" in (row["target"], row["product"]):
            continue
        if name not in identities:
            failures.append(
                f"{CONTRACT} §4 has a row for `{name}`, which no manifest "
                f"declares. Retire the row or add the identity to "
                f"WORKSPACE.md §1 first")
        if row["target"] not in DASH and name not in configs:
            failures.append(
                f"{CONTRACT} §4 says `{name}` exports `{row['target']}`, and "
                f"no *Config.cmake.in in the workspace installs a package "
                f"called `{name}`")


LINK = re.compile(r"\[[^\]]*\]\(([^)]+)\)")

# Code is not prose, and C++ reads as markdown more often than is comfortable:
# a lambda `+[](const VdfContext &ctx)` is exactly `[text](target)`. Strip code
# before looking for links, or every quoted callback becomes a broken link.
FENCE = re.compile(r"^```.*?^```", re.M | re.S)
INLINE_CODE = re.compile(r"`[^`\n]*`")


def prose_only(text: str) -> str:
    """Blank out fenced blocks and inline spans, keeping line numbers intact."""
    def blank(m: re.Match[str]) -> str:
        return re.sub(r"[^\n]", " ", m.group(0))
    return INLINE_CODE.sub(blank, FENCE.sub(blank, text))


def check_release_lane_ost_pin(failures: list[str]) -> None:
    """`release.yml` bootstraps the `ost` the CI contract pins.

    `.github/workflows/release.yml` is hand-authored -- the CI contract cannot
    express `ost plugin test --workspace` or `ost plugin package --workspace`,
    which are the two verbs a release turns on -- so `ost ci generate` never
    touches it and `ost ci validate` says nothing about it. On 2026-09-01 it was
    still bootstrapping 0.22.6 while `openstrata.ci.yaml` had moved to 0.22.8
    two days earlier, and nothing anywhere reported it: the runtime digests
    beside it *had* been mirrored, which is what makes a partial divergence
    harder to see than a neglected one. See ost report 39.
    """
    contract = read("openstrata.ci.yaml")
    m = re.search(r'^bootstrap:\s*$.*?^\s+version:\s*"([^"]+)"',
                  contract, re.M | re.S)
    if not m:
        failures.append("openstrata.ci.yaml declares no bootstrap.ost.version")
        return
    pinned = m.group(1)

    # Only the three sites that decide behaviour: the asset URL, the assertion
    # that checks what was installed, and the registry cache key. The header
    # comment names older versions on purpose — it is prose about this file's
    # history, and a check that read it would forbid the file from having one.
    lane = read(".github/workflows/release.yml")
    sites = {
        "the release asset URL":
            r"open-strata/releases/download/v(\d+\.\d+\.\d+)",
        "the post-install assertion":
            r'!=\s*"ost (\d+\.\d+\.\d+)"',
        "the registry cache key":
            r"key: ost-registry-(\d+\.\d+\.\d+)-",
    }
    for what, pattern in sites.items():
        found = set(re.findall(pattern, lane))
        if not found:
            failures.append(
                f".github/workflows/release.yml: {what} names no `ost` version. "
                f"It bootstraps one by hand, so every pin site must stay "
                f"readable or this check silently stops checking it")
            continue
        wrong = sorted(v for v in found if v != pinned)
        if wrong:
            failures.append(
                f".github/workflows/release.yml: {what} says ost "
                f"{', '.join(wrong)} but openstrata.ci.yaml pins {pinned}. "
                f"It is hand-authored, so regeneration will not fix it and no "
                f"PR lane will report it (ost report 39)")


def check_links(failures: list[str]) -> None:
    for p in sorted(REPO_ROOT.glob("**/*.md")):
        rel = str(p.relative_to(REPO_ROOT)).replace("\\", "/")
        if is_generated(rel):
            continue
        for target in LINK.findall(prose_only(p.read_text(encoding="utf-8",
                                                          errors="replace"))):
            t = target.strip()
            if t.startswith(("http://", "https://", "mailto:", "#")):
                continue
            path = t.partition("#")[0]
            if path and not (p.parent / path).resolve().exists():
                failures.append(f"{rel}: broken link -> {t}")


# --- entry points, lifecycle, and ownership ----------------------------------

# Identities that left this repository. The root README names what this
# repository owns; one of these there tells a reader the tree still has it.
# `motionCore` and the other consumed packages are not listed: naming what the
# product consumes is allowed, describing it is not, and that half is review.
RETIRED_IDENTITIES = (
    "vrmRetarget", "motionRuntime", "motionSource", "motionBvh",
    "motionTracking", "liveTransport", "osc", "vrmAdapterVmc",
    "vrmAdapterMocopi", "vrmAdapterVrchatOsc", "motion_capture",
    "motion_bvh_inspect", "motion_bvh_convert", "vmc_record",
    "mocopi_record", "vrchat_osc_record",
)

# Sequences whose work finished or left with the code. They survive in the
# archive, the release records and the reports.
RETIRED_VOCABULARY = re.compile(r"Motion Phase [A-H]\b|\bMIG-\d\b|\bBND-\d\b")

STATUS_VALUES = {"proposed", "accepted", "binding", "superseded", "rejected",
                 "historical"}
FRONT_MATTER = re.compile(r"\A---\n(?P<body>.*?)\n---\n", re.S)
OPEN_ITEM = re.compile("[⬜\U0001f6a7⛔]")  # ⬜ 🚧 ⛔

# A superseded stub names its replacement and maps its old sections; anything
# much longer is a second copy of the contract it says it no longer holds.
SUPERSEDED_MAX_LINES = 60


def front_matter(text: str) -> dict[str, str]:
    m = FRONT_MATTER.match(text.replace("\r\n", "\n"))
    if not m:
        return {}
    fields = {}
    for line in m.group("body").splitlines():
        key, sep, value = line.partition(":")
        if sep:
            fields[key.strip()] = value.split("#", 1)[0].strip()
    return fields


def check_readme_entry_point(failures: list[str]) -> None:
    """The root README is an entry point, not a design document."""
    raw = read("README.md")
    for i, line in enumerate(prose_only(raw).splitlines(), 1):
        for ver in VERSION_RE.findall(line):
            failures.append(
                f"README.md:{i} states a release version (v{ver}). Version "
                f"status goes stale; link the capability matrix, the roadmap "
                f"or the changelog instead")
        vocab = RETIRED_VOCABULARY.search(line)
        if vocab:
            failures.append(
                f"README.md:{i} uses a retired phase vocabulary "
                f"({vocab.group(0)}); it belongs in the archive and the "
                f"release records")
        for target in LINK.findall(line):
            if "docs/archive/" in target:
                failures.append(
                    f"README.md:{i} links into the archive ({target}); the "
                    f"entry point names current documents only")
        if line.startswith("|") and re.search(
                r"\|\s*(Status|Since|Shipped|Planned)\s*\|", line):
            failures.append(
                f"README.md:{i} has a status column; current status belongs "
                f"in docs/reference/CAPABILITY_MATRIX.md")
    for name in RETIRED_IDENTITIES:
        if re.search(rf"`{re.escape(name)}`", raw):
            failures.append(
                f"README.md names `{name}`, which left this repository. Say "
                f"who owns it now in docs/, not in the entry point")


def check_lifecycle(failures: list[str]) -> None:
    """Front matter and banners say what a document is."""
    for p in sorted(REPO_ROOT.glob("**/*.md")):
        rel = str(p.relative_to(REPO_ROOT)).replace("\\", "/")
        if is_generated(rel):
            continue
        text = p.read_text(encoding="utf-8", errors="replace")
        fm = front_matter(text)
        status = fm.get("status")
        if status and status not in STATUS_VALUES:
            failures.append(
                f"{rel}: front matter status {status!r} is not one of "
                f"{', '.join(sorted(STATUS_VALUES))}")
        archived = (rel.startswith("docs/archive/")
                    and rel != "docs/archive/README.md")
        if archived:
            if status != "historical":
                failures.append(
                    f"{rel}: an archived document carries `status: historical` "
                    f"in its front matter")
            if "Historical only" not in text[:1500]:
                failures.append(
                    f"{rel}: an archived document opens with a "
                    f"\"Historical only\" banner")
        elif status == "historical":
            failures.append(
                f"{rel}: `status: historical` outside docs/archive/ -- move "
                f"the document there, or give it its real status")
        if status == "superseded":
            canonical = fm.get("canonical")
            if not canonical:
                failures.append(f"{rel}: a superseded document names its "
                                f"replacement in `canonical:`")
            elif not (p.parent / canonical).resolve().exists():
                failures.append(f"{rel}: `canonical: {canonical}` does not "
                                f"exist")
            lines = len(text.splitlines())
            if lines > SUPERSEDED_MAX_LINES:
                failures.append(
                    f"{rel}: a superseded stub of {lines} lines (limit "
                    f"{SUPERSEDED_MAX_LINES}) must not keep a parallel copy "
                    f"of what it replaced")


def check_roadmap_ownership(failures: list[str]) -> None:
    """The roadmap holds incomplete work; current status cites no archive."""
    for p in sorted((REPO_ROOT / "docs" / "roadmap").glob("*.md")):
        if p.name == "README.md":
            continue
        if not OPEN_ITEM.search(p.read_text(encoding="utf-8",
                                            errors="replace")):
            failures.append(
                f"docs/roadmap/{p.name} has no open item. Completed work "
                f"leaves the roadmap: move the plan to docs/archive/")
    for p in sorted((REPO_ROOT / "docs" / "reference").glob("*.md")):
        text = prose_only(p.read_text(encoding="utf-8", errors="replace"))
        for target in LINK.findall(text):
            if "archive/" in target and not target.startswith("http"):
                failures.append(
                    f"docs/reference/{p.name} cites the archive ({target}); "
                    f"what is implemented now cannot rest on a superseded "
                    f"plan")


def main() -> int:
    # The findings quote doc prose, which is not ASCII. Don't let a legacy
    # console encoding (e.g. cp932) turn a real failure into a UnicodeEncodeError.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    failures: list[str] = []
    for check in (check_inventory, check_no_stale_paths,
                  check_schema_contract,
                  check_openusd_pin, check_release_lane_ost_pin,
                  check_release_records, check_roadmap_status,
                  check_retired_doc_names, check_component_status,
                  check_package_contract, check_links,
                  check_readme_entry_point, check_lifecycle,
                  check_roadmap_ownership):
        check(failures)

    if failures:
        print(f"FAIL: {len(failures)} documentation inconsistency(ies)\n")
        for f in failures:
            print(f"  - {f}")
        print("\nThe docs are hand-written; fix the prose, not this check — "
              "unless the manifests really did change shape.")
        return 1

    bundles, libraries = discover()
    print(f"OK: docs consistent with {len(bundles)} bundle(s) "
          f"({', '.join(sorted(bundles))}) and {len(libraries)} library(ies) "
          f"({', '.join(sorted(libraries))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
