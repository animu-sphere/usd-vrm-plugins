#!/usr/bin/env python3
"""Regression cases for documentation lifecycle and publication claims."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import check_docs as docs


class DocumentationChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        patcher = patch.object(docs, "REPO_ROOT", self.root)
        patcher.start()
        self.addCleanup(patcher.stop)

    def write(self, path, text):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8")

    def roadmap(self, owner="usd-vrm-plugins"):
        self.write("docs/roadmap/README.md", "| Track | Status | Priority | Owner | Target |\n"
                   "| --- | --- | --- | --- | --- |\n"
                   f"| Closure | active | P0 | {owner} | v0.11.0 candidate |\n")
        self.write("docs/roadmap/current.md", "# Current\nOwner: usd-vrm-plugins\n"
                   "Status: active\n[targets](README.md#status-at-a-glance)\n")
        self.write("docs/README.md", "\n".join(
            f"| Subject | [owner]({p}) |" for p in (
                "architecture/WORKSPACE.md", "architecture/PACKAGE_CONTRACT.md",
                "roadmap/README.md", "ideas/README.md", "contributing/release-candidate.md")))

    def test_no_old_milestone_heading_required(self):
        self.roadmap()
        failures = []
        docs.check_roadmap_status(failures)
        self.assertEqual([], failures)

    def test_proposed_record_is_not_a_release(self):
        self.roadmap()
        self.write("docs/releases/v0.11.0.md", "---\nstatus: proposed\n---\n# Unpublished\n")
        failures = []
        docs.check_roadmap_status(failures)
        self.assertEqual([], failures)
        self.write("docs/releases/v0.11.0.md", "# Published record\n")
        docs.check_roadmap_status(failures)
        self.assertTrue(any("targets released v0.11.0" in f for f in failures))

    def test_foreign_owner_is_dependency_not_local_track(self):
        self.roadmap(owner="hydra-toon")
        failures = []
        docs.check_roadmap_status(failures)
        self.assertTrue(any("local owner" in f for f in failures))

    def test_completed_task_and_parallel_stub_checklist_fail(self):
        self.roadmap()
        self.write("docs/roadmap/backlog.md", "Owner: usd-vrm-plugins\nbacklog\n- [x] done\n")
        self.write("docs/roadmap/old.md", "---\nstatus: superseded\n---\n- ⬜ retained task\n")
        failures = []
        docs.check_roadmap_ownership(failures)
        self.assertTrue(any("completed tasks" in f for f in failures))
        self.assertTrue(any("compatibility stub" in f for f in failures))

    def test_stub_without_tasks_is_allowed(self):
        self.write("docs/roadmap/old.md", "---\nstatus: superseded\n---\n[policy](../design/policy.md)\n")
        failures = []
        docs.check_roadmap_ownership(failures)
        self.assertEqual([], failures)

    def test_links_validate_anchors_and_legacy_aliases(self):
        self.write("source.md", "[ok](target.md#api-name)\n[legacy](target.md#old)\n"
                   "[missing](target.md#missing)\n[duplicate](target.md#api-name-1)\n")
        self.write("target.md", "# `API` name\n# `API` name\n<a id=\"old\"></a>\n")
        failures = []
        docs.check_links(failures)
        self.assertEqual(["source.md: broken anchor -> target.md#missing"], failures)

    def test_package_version_drift_fails_independently_of_schema(self):
        self.write("VERSION", "0.10.0\n")
        self.write("CHANGELOG.md", "## [0.10.0]\n")
        self.write("openstrata.toml", '[project]\nversion = "0.10.0"\n')
        self.write("docs/releases/v0.10.0.md", "# Released\n")
        self.write("plugins/vrmSchema/openstrata.plugin.yaml",
                   "plugin:\n  version: 0.10.0\nschema:\n  contract: 2\n")
        failures = []
        docs.check_release_records(failures)
        self.assertEqual([], failures)
        self.write("tools/export/openstrata.tool.yaml", "tool:\n  version: 0.9.0\n")
        docs.check_release_records(failures)
        self.assertTrue(any("does not match VERSION" in f for f in failures))


if __name__ == "__main__":
    unittest.main()
