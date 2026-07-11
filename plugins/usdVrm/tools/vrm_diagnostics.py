#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Canonical VRM import/validation diagnostic taxonomy.

Every diagnostic the toolchain reports carries a stable code (``VRMxxx``) with a
fixed severity. This module is the single source of truth for that catalog:

* the C++ importer tags each message with a ``[VRMxxx] `` prefix (see
  ``src/model/VrmDiagnostics.h``) — the code list here mirrors that header;
* the stage validator (``validate_vrm.py``) raises the ``VRM2xx``/``VRM3xx``
  structural codes it owns;
* the compatibility report (``vrm_report.py``) resolves every code's severity
  from this table and rolls the counts up.

Severity ladder (most to least severe):

    FATAL   the stage is unusable / could not be read
    ERROR   a contract violation: downstream tools will misbehave
    WARNING fidelity loss or an unmapped feature; the import still loads
    INFO    a note about a deliberate, lossless-but-not-typed mapping

Keep this catalog and ``docs/DIAGNOSTICS.md`` in sync with the C++ header.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from enum import IntEnum
from typing import Iterable


class Severity(IntEnum):
    """Diagnostic severity, ordered so ``max()`` picks the worst."""

    INFO = 10
    WARNING = 20
    ERROR = 30
    FATAL = 40

    @property
    def label(self) -> str:
        return self.name


@dataclass(frozen=True)
class DiagnosticSpec:
    """A registered diagnostic code: its severity, source and short title."""

    code: str
    severity: Severity
    source: str  # "import" (C++ importer) or "validate" (stage validator)
    title: str


def _spec(code: str, severity: Severity, source: str, title: str) -> DiagnosticSpec:
    return DiagnosticSpec(code=code, severity=severity, source=source, title=title)


# --- Import-time codes (emitted by the C++ importer) -------------------------
# These MUST match src/model/VrmDiagnostics.h. The message text lives in C++;
# only the code + severity + a human title are catalogued here.
_IMPORT_SPECS = [
    _spec("VRM001", Severity.WARNING, "import",
          "No VRM extension; imported as plain glTF"),
    _spec("VRM002", Severity.ERROR, "import",
          "VRM extension JSON could not be parsed"),
    _spec("VRM003", Severity.FATAL, "import",
          "Container could not be read; stage fails to open"),
    _spec("VRM101", Severity.WARNING, "import",
          "Embedded image format unsupported; texture skipped"),
    _spec("VRM102", Severity.WARNING, "import",
          "Data-URI image unsupported; texture skipped"),
    _spec("VRM103", Severity.WARNING, "import",
          "Texture uses a UV set other than TEXCOORD_0"),
    _spec("VRM110", Severity.WARNING, "import",
          "Conflicting inverse bind matrices across skins"),
    _spec("VRM111", Severity.WARNING, "import",
          "Skin joint index out of range; clamped to root"),
    _spec("VRM120", Severity.WARNING, "import",
          "Primitive is not a triangle list; skipped"),
    _spec("VRM121", Severity.WARNING, "import",
          "Primitive has no POSITION; skipped"),
    _spec("VRM140", Severity.WARNING, "import",
          "Humanoid bone could not be mapped to a joint"),
    _spec("VRM141", Severity.WARNING, "import",
          "Duplicate humanoid bone; first mapping kept"),
    _spec("VRM150", Severity.INFO, "import",
          "VRM 0.x materialValues expression preserved raw only"),
    _spec("VRM151", Severity.WARNING, "import",
          "Expression morph target index out of range; bind skipped"),
    _spec("VRM160", Severity.WARNING, "import",
          "CUBICSPLINE animation approximated as linear"),
    _spec("VRM170", Severity.WARNING, "import",
          "Node constraint has no valid source; skipped"),
    _spec("VRM190", Severity.WARNING, "import",
          "Spring collider-group index out of range; dropped"),
    _spec("VRM180", Severity.WARNING, "import",
          "Morph targets present but no skeleton; blend shapes skipped"),
    _spec("VRM181", Severity.WARNING, "import",
          "Humanoid bones present but no skeleton imported"),
]

# --- Validation-time codes (emitted by validate_vrm.py) ----------------------
# VRM2xx stage structure, VRM3xx bindings/targets. These have no C++ analogue.
_VALIDATE_SPECS = [
    _spec("VRM200", Severity.FATAL, "validate",
          "Stage has no default prim"),
    _spec("VRM201", Severity.ERROR, "validate",
          "Default prim is not /Asset"),
    _spec("VRM202", Severity.WARNING, "validate",
          "/Asset kind is not 'component'"),
    _spec("VRM203", Severity.ERROR, "validate",
          "Stage up-axis is not Y"),
    _spec("VRM204", Severity.WARNING, "validate",
          "Stage metersPerUnit is not 1.0"),
    _spec("VRM205", Severity.ERROR, "validate",
          "SkelRoot/skeleton presence mismatch on /Asset"),
    _spec("VRM210", Severity.ERROR, "validate",
          "Skinned mesh has no skeleton binding"),
    _spec("VRM211", Severity.ERROR, "validate",
          "skel:skeleton target does not resolve to a Skeleton"),
    _spec("VRM212", Severity.ERROR, "validate",
          "Joint index out of range for the bound skeleton"),
    _spec("VRM213", Severity.ERROR, "validate",
          "Skinned mesh is missing joint indices/weights"),
    _spec("VRM214", Severity.ERROR, "validate",
          "Skeleton topology is not parent-before-child"),
    _spec("VRM220", Severity.INFO, "validate",
          "Mesh has no material binding"),
    _spec("VRM221", Severity.ERROR, "validate",
          "Material binding target does not exist"),
    _spec("VRM222", Severity.ERROR, "validate",
          "Texture asset does not resolve"),
    _spec("VRM230", Severity.ERROR, "validate",
          "Humanoid prim does not apply VrmHumanoidAPI"),
    _spec("VRM231", Severity.ERROR, "validate",
          "Humanoid vrm:skeleton relationship is missing/broken"),
    _spec("VRM232", Severity.ERROR, "validate",
          "Humanoid bone value is not a joint on the skeleton"),
    _spec("VRM240", Severity.ERROR, "validate",
          "Expression morph-target relationship is broken"),
    _spec("VRM241", Severity.ERROR, "validate",
          "Expression material-color target does not exist"),
    _spec("VRM242", Severity.ERROR, "validate",
          "Expression prim does not apply VrmExpressionAPI"),
    _spec("VRM243", Severity.ERROR, "validate",
          "Expression morph-target arrays are not parallel"),
    _spec("VRM244", Severity.ERROR, "validate",
          "Expression material-color arrays are not parallel"),
    _spec("VRM245", Severity.ERROR, "validate",
          "LookAt prim does not apply VrmLookAtAPI"),
    _spec("VRM246", Severity.ERROR, "validate",
          "LookAt skeleton relationship is missing/broken"),
    _spec("VRM247", Severity.ERROR, "validate",
          "LookAt eye joint value is not a skeleton joint"),
    _spec("VRM250", Severity.ERROR, "validate",
          "Spring-bone joint path is not on the skeleton"),
    _spec("VRM251", Severity.ERROR, "validate",
          "Spring-bone collider-group target does not exist"),
    _spec("VRM252", Severity.ERROR, "validate",
          "Spring-bone prim does not apply VrmSpringBoneAPI"),
    _spec("VRM253", Severity.ERROR, "validate",
          "Spring-bone parameter arrays are not parallel"),
    _spec("VRM254", Severity.ERROR, "validate",
          "Collider prim does not apply VrmColliderAPI"),
    _spec("VRM255", Severity.ERROR, "validate",
          "Collider shape token is invalid"),
    _spec("VRM260", Severity.INFO, "validate",
          "Lossless raw VRM extension block is absent"),
    _spec("VRM262", Severity.ERROR, "validate",
          "Constraint prim does not apply VrmConstraintAPI"),
    _spec("VRM263", Severity.ERROR, "validate",
          "Constraint type token is invalid"),
    _spec("VRM264", Severity.ERROR, "validate",
          "Constraint joint value is not a skeleton joint"),
    _spec("VRM270", Severity.WARNING, "validate",
          "Schema contract version is absent"),
    _spec("VRM271", Severity.ERROR, "validate",
          "Schema contract version is unsupported"),
]

CATALOG: dict[str, DiagnosticSpec] = {
    spec.code: spec for spec in (_IMPORT_SPECS + _VALIDATE_SPECS)
}

_CODE_RE = re.compile(r"^\[(VRM\d+)\]\s*(.*)$", re.DOTALL)


@dataclass
class Diagnostic:
    """A single reported diagnostic instance."""

    code: str
    severity: Severity
    message: str
    source: str
    prim_path: str = ""

    @property
    def title(self) -> str:
        spec = CATALOG.get(self.code)
        return spec.title if spec else ""

    def to_dict(self) -> dict:
        d = {
            "code": self.code,
            "severity": self.severity.label,
            "source": self.source,
            "message": self.message,
        }
        if self.prim_path:
            d["primPath"] = self.prim_path
        if self.title:
            d["title"] = self.title
        return d


def severity_of(code: str, default: Severity = Severity.WARNING) -> Severity:
    """Severity for a registered code, or ``default`` for an unknown one."""

    spec = CATALOG.get(code)
    return spec.severity if spec else default


def parse_coded_message(message: str) -> tuple[str, str]:
    """Split a ``[VRMxxx] body`` string into ``(code, body)``.

    A message with no recognizable prefix returns ``("", message)`` — the
    importer has always emitted plain text and older stages may still carry it.
    """

    match = _CODE_RE.match(message.strip())
    if not match:
        return "", message.strip()
    return match.group(1), match.group(2).strip()


def make_import_diagnostic(message: str) -> Diagnostic:
    """Build a Diagnostic from a stage ``vrm:warnings`` entry.

    The importer prefixes each warning with its code; severity is resolved from
    the catalog. Un-coded legacy text degrades to a WARNING with an empty code.
    """

    code, body = parse_coded_message(message)
    return Diagnostic(
        code=code,
        severity=severity_of(code) if code else Severity.WARNING,
        message=body,
        source="import",
    )


def make(code: str, message: str, prim_path: str = "") -> Diagnostic:
    """Build a validator-side Diagnostic for a registered code."""

    spec = CATALOG.get(code)
    if spec is None:
        raise KeyError(f"unknown diagnostic code: {code!r}")
    return Diagnostic(
        code=code,
        severity=spec.severity,
        message=message,
        source=spec.source,
        prim_path=prim_path,
    )


def severity_counts(diagnostics: Iterable[Diagnostic]) -> dict[str, int]:
    """Count diagnostics per severity label, always including every level."""

    counts = {sev.label: 0 for sev in Severity}
    for diag in diagnostics:
        counts[diag.severity.label] += 1
    return counts


def worst_severity(diagnostics: Iterable[Diagnostic]) -> Severity | None:
    """The most severe severity in the set, or None if empty."""

    severities = [d.severity for d in diagnostics]
    return max(severities) if severities else None
