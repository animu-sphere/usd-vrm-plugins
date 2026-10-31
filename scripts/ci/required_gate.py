#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reject missing/failed/cancelled jobs and skips not selected by the router."""
from __future__ import annotations

import json
import os


LIGHT = {"changes", "docs"}
HEAVY = {"generated", "source", "plain", "consumer"}


def validate(needs: dict) -> None:
    if set(needs) != LIGHT | HEAVY:
        raise ValueError("required gate dependencies are missing or unexpected")
    full = needs["changes"].get("outputs", {}).get("full")
    if full not in {"true", "false"}:
        raise ValueError("changes did not emit a valid route")
    for name, job in needs.items():
        expected = "skipped" if name in HEAVY and full == "false" else "success"
        if job.get("result") != expected:
            raise ValueError(f"{name}: expected {expected}, got {job.get('result')!r}")


if __name__ == "__main__":
    validate(json.loads(os.environ["NEEDS_JSON"]))
    print("All selected CI jobs succeeded.")
