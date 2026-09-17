"""Failure-inject the real release publisher without making network requests."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
ROOT = Path(sys.argv[1]).resolve()
sys.argv = sys.argv[:1]
spec = importlib.util.spec_from_file_location("release", ROOT / "packaging/Release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class FakeGitHub:
    def __init__(self, failure=None, existing=False):
        self.failure = failure
        self.calls = []
        self.draft = existing
        self.public = False
        self.uploaded = []
        self.assets = []

    def call(self, *args, optional=False):
        self.calls.append(args)
        command = args[1]
        if command == "view":
            return json.dumps({"isDraft": True, "body": "Existing notes", "assets": self.assets}) if self.draft else None
        if command == "create":
            assert "--draft" in args and "--verify-tag" in args
            self.draft = True
        if command == "upload":
            assert self.draft and not self.public
            if self.failure == "upload":
                raise RuntimeError("Injected partial upload failure")
            self.uploaded = [Path(a) for a in args[3:] if a != "--clobber"]
        if command == "download":
            if self.failure == "download":
                raise RuntimeError("Injected verification download failure")
            directory = Path(args[args.index("--dir") + 1])
            directory.mkdir()
            for file in self.uploaded:
                shutil.copyfile(file, directory / file.name)
            if self.failure == "corrupt":
                (directory / self.uploaded[0].name).write_text("corrupted")
            if self.failure == "missing":
                (directory / self.uploaded[0].name).unlink()
        if command == "edit" and "--draft=false" in args:
            assert self.draft and self.uploaded
            self.public = True
            self.draft = False
        return ""


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        scratch_parent = os.environ.get("TMPDIR") or (tempfile.gettempdir() if os.name == "nt" else "/var/tmp")
        self.temp = tempfile.TemporaryDirectory(prefix="cmr-release-test-", dir=scratch_parent)
        self.addCleanup(self.temp.cleanup)
        # Production runs on Linux; make its nested verification scratch portable in tests.
        self.old_tmpdir = os.environ.get("TMPDIR")
        os.environ["TMPDIR"] = self.temp.name
        self.addCleanup(self.restore_tmpdir)
        self.root = Path(self.temp.name)
        self.artifacts = self.root / "artifacts"
        self.artifacts.mkdir()
        for name in release.expected_assets("3.1.1", False, True):
            (self.artifacts / name).write_text(name)
        self.upload = self.root / "upload"
        release.prepare(self.artifacts, self.upload, "3.1.1", False, True)
        self.needs = {job: {"result": "success", "outputs": {}}
                      for job in (*release.BUILD_JOBS, "release_metadata", "checksums")}
        self.needs["release_metadata"]["outputs"]["game_version"] = "3.1.1"
        self.needs["build-macos"]["outputs"]["signed"] = "false"

    def restore_tmpdir(self):
        if self.old_tmpdir is None:
            os.environ.pop("TMPDIR", None)
        else:
            os.environ["TMPDIR"] = self.old_tmpdir

    def test_failed_matrix_or_checksum_never_contacts_github(self):
        for job in self.needs:
            for result in ("failure", "cancelled", "skipped"):
                with self.subTest(job=job, result=result):
                    needs = copy.deepcopy(self.needs)
                    needs[job]["result"] = result
                    github = FakeGitHub()
                    with self.assertRaises(RuntimeError):
                        release.publish(self.upload, "v3.1.1", needs, github)
                    self.assertEqual(github.calls, [])
                    self.assertFalse(github.public)

    def test_upload_and_verification_failures_remain_private(self):
        for failure in ("upload", "download", "corrupt", "missing"):
            with self.subTest(failure=failure):
                github = FakeGitHub(failure)
                with self.assertRaises(RuntimeError):
                    release.publish(self.upload, "v3.1.1", self.needs, github)
                self.assertTrue(github.draft)
                self.assertFalse(github.public)

    def test_complete_release_publishes_last_and_can_resume_draft(self):
        for existing in (False, True):
            github = FakeGitHub(existing=existing)
            release.publish(self.upload, "v3.1.1", self.needs, github)
            self.assertTrue(github.public)
            self.assertEqual(github.calls[-1], ("release", "edit", "v3.1.1", "--draft=false", "--verify-tag"))

    def test_missing_artifact_and_invalid_checksum(self):
        next(self.artifacts.iterdir()).unlink()
        with self.assertRaises(RuntimeError):
            release.prepare(self.artifacts, self.root / "incomplete", "3.1.1", False, True)
        (self.upload / "SHA256SUMS.txt").write_text("wrong")
        github = FakeGitHub()
        with self.assertRaises(RuntimeError):
            release.publish(self.upload, "v3.1.1", self.needs, github)
        self.assertEqual(github.calls, [])

    def test_obsolete_draft_asset_rejected_before_mutation_and_retryable(self):
        github = FakeGitHub(existing=True)
        valid_asset = next(self.upload.iterdir()).name
        github.assets = [{"name": valid_asset}, {"name": "obsolete-build.zip"}]
        with self.assertRaisesRegex(RuntimeError, "Remove these assets from the draft and retry"):
            release.publish(self.upload, "v3.1.1", self.needs, github)
        self.assertEqual([call[1] for call in github.calls], ["view"])
        self.assertTrue(github.draft)
        self.assertFalse(github.public)
        github.assets.pop()
        release.publish(self.upload, "v3.1.1", self.needs, github)
        self.assertTrue(github.public)

    def test_workflow_gate_covers_every_build(self):
        workflow = (ROOT / ".github/workflows/ReleaseBuilds.yml").read_text()
        self.assertNotIn("types: [published]", workflow)
        publication = workflow.split("  publish-release:\n", 1)[1]
        needs = publication.split("    needs:\n", 1)[1].split("    if:", 1)[0]
        self.assertEqual({line.strip()[2:] for line in needs.splitlines()}, set(self.needs))
        self.assertIn("success() && github.event_name == 'push' && github.ref_type == 'tag'", publication)
        self.assertIn("RELEASE_NEEDS: ${{ toJSON(needs) }}", publication)
        self.assertIn("run: python3 packaging/Release.py publish", publication)
        self.assertNotIn("contents: write", workflow.split("  publish-release:", 1)[0])


unittest.main()
