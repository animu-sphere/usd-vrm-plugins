# SPDX-License-Identifier: Apache-2.0
"""Regression cases for missing evidence and same-count membership drift."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import check_release_closure as closure


class ReleaseClosureTests(unittest.TestCase):
    def setUp(self):
        self.inv = closure.inventory(closure.ROOT)
        self.steps = {key: {"outcome": "success", "conclusion": "success"}
                      for key in closure.GATES}
        self.package = {"release_members": list(self.inv["members"]),
                        "packages": [{"name": name, "member": kind}
                                     for name, kind in self.inv["members"].items()],
                        "product": {"members": len(self.inv["members"])}}

    def test_complete_evidence(self):
        report = closure.checklist(self.inv, self.steps, self.package)
        self.assertTrue(report["passed"])
        self.assertEqual(report["checks"][-1]["status"], "notApplicable")

    def test_every_gate_is_required(self):
        for key in closure.GATES:
            for state in (None, "skipped", "cancelled", "failure"):
                with self.subTest(key=key, state=state):
                    steps = copy.deepcopy(self.steps)
                    if state is None:
                        del steps[key]
                    else:
                        steps[key] = {"outcome": state, "conclusion": state}
                    self.assertFalse(closure.checklist(self.inv, steps, self.package)["passed"])

    def test_continue_on_error_cannot_hide_failure(self):
        self.steps["closure_consumers"] = {"outcome": "failure", "conclusion": "success"}
        self.assertFalse(closure.checklist(self.inv, self.steps, self.package)["passed"])

    def test_same_count_different_packaged_identity(self):
        self.package["packages"][0]["name"] = "unshipped_bundle"
        self.assertFalse(closure.checklist(self.inv, self.steps, self.package)["passed"])

    def test_duplicate_and_wrong_kind(self):
        for mutate in ("duplicate", "kind", "declaration", "count"):
            with self.subTest(mutate=mutate):
                data = copy.deepcopy(self.package)
                if mutate == "duplicate":
                    data["packages"].append(data["packages"][0])
                elif mutate == "kind":
                    data["packages"][0]["member"] = "library"
                elif mutate == "declaration":
                    data["release_members"][0] = "unshipped_bundle"
                else:
                    data["product"]["members"] -= 1
                self.assertFalse(closure.checklist(self.inv, self.steps, data)["passed"])

    def test_new_installed_data_requires_probe(self):
        self.inv["installData"] = [{"source": "data", "destination": "share/data"}]
        self.assertFalse(closure.checklist(self.inv, self.steps, self.package)["passed"])

    def test_manifest_cannot_hide_tool_by_excluding_it(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "tools/example").mkdir(parents=True)
            (root / "tools/example/openstrata.tool.yaml").write_text("tool:\n  id: example\n")
            (root / "openstrata.toml").write_text(
                '[workspace]\nmembers = ["tools/example"]\n'
                'release_members = []\nrelease_exclude = ["example"]\n')
            with self.assertRaises(ValueError):
                closure.inventory(root)

    def test_failed_job_without_package_still_writes_report(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "closure.json"
            result = subprocess.run(
                [sys.executable, str(closure.ROOT / "scripts/check_release_closure.py"),
                 "--package-json", str(Path(directory) / "missing.json"),
                 "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            report = json.loads(output.read_text())
            self.assertFalse(report["passed"])
            self.assertTrue(all(c["status"] == "failed" for c in report["checks"][:-1]))
            self.assertIn("packageError", report)


if __name__ == "__main__":
    unittest.main()
