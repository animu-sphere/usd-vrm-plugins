#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Routing regressions run without OST, OpenUSD, network access or PyYAML."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts/ci" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


paths = load("changed_paths")
gate = load("required_gate")
generator = load("generate")


class Routing(unittest.TestCase):
    def test_only_known_prose_skips_builds(self):
        self.assertFalse(paths.requires_full(["README.md", "CHANGELOG.md", "docs/guides/BUILDING.md"]))
        self.assertFalse(paths.requires_full([f"docs/reports/{i}.md" for i in range(3500)]))
        self.assertFalse(paths.requires_full(["docs/日本語 の説明.md", "docs/a\nb.md"]))

    def test_unknown_and_executable_inputs_are_full(self):
        for path in ["openstrata.ci.yaml", "openstrata.toml", "CMakeLists.txt",
                     "cmake/x.cmake", "CMakePresets.json", "VERSION",
                     ".github/workflows/ci-routing.yml", "scripts/ci/generate.py",
                     "plugins/x.cpp", "libs/x.cpp", "tools/x.py", "tests/README.md",
                     "third_party/x", "deps.lock", "docs/fixture.json", "docs/example.py",
                     "docs/architecture/PACKAGE_CONTRACT.md",
                     "docs/contributing/RELEASE_NOTES_TEMPLATE.md", "unknown.md",
                     "docs/../code.md", "docs//guide.md", "docs\\guide.md"]:
            with self.subTest(path=path):
                self.assertTrue(paths.requires_full(["README.md", path]))
        self.assertTrue(paths.requires_full([]))

    def test_large_diff_cannot_hide_code(self):
        self.assertTrue(paths.requires_full(
            [f"docs/{i}.md" for i in range(3500)] + ["plugins/code.cpp"]))

    def test_git_deletions_renames_and_fork_head(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)

            def git(*args):
                return subprocess.check_output(["git", *args], cwd=repo, text=True).strip()

            def commit():
                git("add", "-A")
                git("-c", "user.name=CI Test", "-c", "user.email=ci@example.invalid",
                    "-c", "commit.gpgsign=false", "commit", "-qm", "fixture")
                return git("rev-parse", "HEAD")

            git("init", "-q")
            (repo / "docs").mkdir()
            (repo / "code.cpp").write_text("code\n")
            (repo / "docs/old.md").write_text("old\n")
            base = commit()
            git("checkout", "-qb", "fork-head")
            (repo / "code.cpp").rename(repo / "docs/renamed.md")
            (repo / "docs/old.md").unlink()
            head = commit()
            changed = paths.changed_paths(base, head, repo)
            self.assertEqual(set(changed), {"code.cpp", "docs/renamed.md", "docs/old.md"})
            self.assertTrue(paths.requires_full(changed))
            # Base advanced after the fork; unrelated changes are not the PR diff.
            git("checkout", "-q", "--detach", base)
            (repo / "base-only.cpp").write_text("base\n")
            advanced_base = commit()
            self.assertEqual(changed, paths.changed_paths(advanced_base, head, repo))
            self.assertEqual([], paths.changed_paths(head, head, repo))
            # A docs deletion and a synchronize commit still select docs-only.
            git("checkout", "-q", "--detach", base)
            (repo / "docs/old.md").unlink()
            docs_head = commit()
            self.assertFalse(paths.requires_full(paths.changed_paths(base, docs_head, repo)))
            (repo / "docs/new.md").write_text("new\n")
            synced = commit()
            self.assertFalse(paths.requires_full(paths.changed_paths(base, synced, repo)))

    def test_invalid_sha_fails(self):
        for sha in ["", "refs/heads/main", "--help", "a" * 39]:
            with self.subTest(sha=sha), self.assertRaises(ValueError):
                paths.changed_paths(sha, "b" * 40)

    def test_dispatch_forces_full_without_reading_a_diff(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "output"
            with mock.patch.dict(os.environ, {
                "GITHUB_EVENT_NAME": "workflow_dispatch", "GITHUB_OUTPUT": str(output)
            }, clear=True), mock.patch("sys.argv", ["changed_paths.py"]), \
                    mock.patch.object(paths, "changed_paths") as diff:
                paths.main()
                diff.assert_not_called()
            self.assertEqual("full=true\n", output.read_text())

    def test_pr_reads_event_shas_and_propagates_diff_errors(self):
        with tempfile.TemporaryDirectory() as tmp:
            event = Path(tmp) / "event.json"
            output = Path(tmp) / "output"
            event.write_text(json.dumps({"pull_request": {
                "base": {"sha": "a" * 40}, "head": {"sha": "b" * 40}}}))
            with mock.patch.dict(os.environ, {
                "GITHUB_EVENT_NAME": "pull_request", "GITHUB_EVENT_PATH": str(event),
                "GITHUB_OUTPUT": str(output)
            }, clear=True), mock.patch("sys.argv", ["changed_paths.py"]):
                with mock.patch.object(paths, "changed_paths", return_value=["README.md"]) as diff:
                    paths.main()
                    diff.assert_called_once_with("a" * 40, "b" * 40)
                    self.assertEqual("full=false\n", output.read_text())
                output.unlink()
                with mock.patch.object(paths, "changed_paths", side_effect=RuntimeError("missing git object")):
                    with self.assertRaises(RuntimeError):
                        paths.main()
                    self.assertFalse(output.exists())


class RequiredGate(unittest.TestCase):
    def needs(self, full):
        jobs = {name: {"result": "success"} for name in gate.LIGHT | gate.HEAVY}
        jobs["changes"]["outputs"] = {"full": str(full).lower()}
        if not full:
            for name in gate.HEAVY:
                jobs[name]["result"] = "skipped"
        return jobs

    def test_valid_routes(self):
        for full in (True, False):
            gate.validate(self.needs(full))

    def test_every_failure_cancellation_and_unexpected_skip_fails(self):
        for full in (True, False):
            for name in gate.LIGHT | gate.HEAVY:
                expected = self.needs(full)[name]["result"]
                for result in ("success", "failure", "cancelled", "skipped", None, "unknown"):
                    if result == expected:
                        continue
                    with self.subTest(full=full, job=name, result=result):
                        needs = self.needs(full)
                        needs[name]["result"] = result
                        with self.assertRaises(ValueError):
                            gate.validate(needs)

    def test_missing_job_and_output_fail(self):
        for name in gate.LIGHT | gate.HEAVY:
            needs = self.needs(True)
            del needs[name]
            with self.assertRaises(ValueError):
                gate.validate(needs)
        for full in (None, "", "TRUE", "unknown"):
            needs = self.needs(True)
            needs["changes"]["outputs"]["full"] = full
            with self.assertRaises(ValueError):
                gate.validate(needs)

    def test_workflow_needs_cover_every_caller(self):
        workflow = (ROOT / ".github/workflows/ci-routing.yml").read_text()
        import re
        job_names = set(re.findall(r"^  ([a-z-]+):$", workflow.split("\njobs:\n")[1], re.M))
        self.assertEqual(job_names - {"ci-required"}, gate.LIGHT | gate.HEAVY)
        listed = re.search(r"^    needs: \[(.*)\]$", workflow, re.M).group(1)
        self.assertEqual(set(listed.split(", ")), gate.LIGHT | gate.HEAVY)


class Generation(unittest.TestCase):
    def raw(self):
        return ("# Generated by `ost ci generate github` from openstrata.ci.yaml.\n"
                "# Regenerate after editing the matrix; do not edit the jobs by hand.\n"
                "name: ost source ci\non:\n  pull_request:\n\njobs:\n"
                "  pr:\n    if: github.event_name == 'pull_request'\n"
                "    steps:\n      - run: ost artifact verify pinned --require-provenance\n")

    def test_adapter_preserves_all_job_steps(self):
        raw = self.raw()
        rendered = generator.adapt(raw)
        self.assertIn("  workflow_call:\n  workflow_dispatch:\n", rendered)
        self.assertIn("github.event_name == 'workflow_dispatch'", rendered)
        self.assertEqual(raw.split("    steps:\n")[1], rendered.split("    steps:\n")[1])

    def test_adapter_rejects_unknown_generator_shapes(self):
        for old, new in [("  pull_request:", "  push:"), ("  pr:", "  mainline:"),
                         ("    if: github.event_name", "    if: other.event_name")]:
            with self.subTest(old=old), self.assertRaises(ValueError):
                generator.adapt(self.raw().replace(old, new))

    def test_drift_check_rejects_a_modified_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "generated.yml"
            output.write_text(generator.adapt(self.raw()))
            with mock.patch.object(generator, "OUTPUT", output), \
                    mock.patch.object(generator, "declared_bootstrap_version", return_value="0.23.13"), \
                    mock.patch("sys.argv", ["generate.py", "--check"]), \
                    mock.patch.object(generator.subprocess, "check_output", side_effect=[
                        "ost 0.23.13\n", self.raw(), "ost 0.23.13\n", self.raw()
                    ]):
                self.assertEqual(0, generator.main())
                output.write_text(output.read_text() + "# manual drift\n")
                with mock.patch("sys.stderr"):
                    self.assertEqual(1, generator.main())

    def test_generation_refuses_the_wrong_ost_version(self):
        with mock.patch("sys.argv", ["generate.py", "--check"]), \
                mock.patch.object(generator, "declared_bootstrap_version", return_value="0.23.13"), \
                mock.patch.object(generator.subprocess, "check_output", return_value="ost 0.23.14\n"):
            with self.assertRaisesRegex(ValueError, "generation requires ost 0.23.13"):
                generator.main()


if __name__ == "__main__":
    unittest.main()
