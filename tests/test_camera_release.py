"""Pruebas stdlib: no Arduino, red, credenciales reales ni puerto serie."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

MODULE = Path(__file__).resolve().parents[1] / "scripts" / "camera_release.py"
SPEC = importlib.util.spec_from_file_location("camera_release", MODULE)
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
HEAD = "a" * 40
SOURCE = release.SKETCH + "/sd-recovery-firmware.ino"
CONTENT = b'// synthetic source\n#include "deployment_config.h"\n'


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.repo = self.base / "repo"
        self.repo.mkdir(mode=0o700)
        source = self.repo / SOURCE
        source.parent.mkdir(parents=True, mode=0o700)
        source.write_bytes(CONTENT)
        self.private = self.base / "private"
        self.private.mkdir(mode=0o700)
        self.config = self.private / "config.h"
        self.config.write_bytes(b"// synthetic-private-test-value\n")
        os.chmod(self.config, 0o600)
        self.output = self.private / "release"
        self.calls = []
        self.runner = mock.patch.object(release, "run", side_effect=self.fake_run)
        self.runner.start()
        self.addCleanup(self.runner.stop)

    def fake_run(self, args, cwd):
        self.calls.append(args)
        if args[0] == "git":
            cmd = args[1]
            if cmd == "rev-parse":
                return str(self.repo).encode() if args[2] == "--show-toplevel" else HEAD.encode()
            if cmd == "status":
                return b""
            if cmd == "ls-remote":
                return (HEAD + "\trefs/heads/main\n").encode()
            if cmd == "ls-tree":
                return b"100644 blob " + b"b" * 40 + b"\t" + SOURCE.encode() + b"\0"
            if cmd == "show":
                return CONTENT
        if args[1] == "version":
            return json.dumps({"VersionString": release.CLI_VERSION}).encode()
        if args[1:3] == ["core", "list"]:
            return json.dumps({"platforms": [{"id": "esp32:esp32", "installed_version": release.CORE_VERSION}]}).encode()
        if args[1] == "compile":
            staging = Path(args[-1])
            self.assertNotEqual(staging, self.repo / release.SKETCH)
            self.assertEqual((staging / "deployment_config.h").read_bytes(), self.config.read_bytes())
            self.assertEqual((staging / "sd-recovery-firmware.ino").read_bytes(), CONTENT)
            self.staging = staging
            output = Path(args[args.index("--build-path") + 1])
            (output / "camera.ino.bin").write_bytes(b"synthetic-application")
            (output / "camera.ino.bootloader.bin").write_bytes(b"synthetic-bootloader")
            (output / "camera.ino.partitions.bin").write_bytes(b"synthetic-partitions")
            return b""
        self.fail("Unexpected command: " + repr(args))

    def build(self):
        return release.build_release(self.repo, self.config, self.output)

    def manifest(self):
        return json.loads((self.output / "manifest.json").read_text())

    def tamper(self, mutate):
        manifest = self.manifest()
        mutate(manifest)
        (self.output / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaises(release.ReleaseError):
            release.verify_release(self.output, self.repo)

    def test_build_and_verify_all_bins_without_secrets(self):
        manifest = self.build()
        self.assertEqual(manifest, release.verify_release(self.output, self.repo))
        self.assertEqual(len(manifest["artifacts"]), 3)
        self.assertEqual(manifest["sources"], {SOURCE: release.digest(CONTENT)})
        self.assertNotIn(self.config.read_text().strip(), json.dumps(manifest))
        self.assertNotIn(release.digest(self.config.read_bytes()), json.dumps(manifest))
        self.assertNotIn(str(self.config), json.dumps(manifest))
        self.assertFalse(self.staging.exists())
        self.assertFalse((self.repo / release.SKETCH / "deployment_config.h").exists())
        self.assertFalse(any("upload" in cmd or "flash" in cmd for cmd in self.calls))
        if os.name == "posix":
            self.assertEqual(self.output.stat().st_mode & 0o777, 0o700)
            self.assertEqual((self.output / "build").stat().st_mode & 0o777, 0o700)
            self.assertEqual((self.output / "manifest.json").stat().st_mode & 0o777, 0o600)

    def test_existing_output_never_overwritten(self):
        self.output.mkdir(mode=0o700)
        sentinel = self.output / "keep"
        sentinel.write_bytes(b"untouched")
        with self.assertRaises(release.ReleaseError):
            self.build()
        self.assertEqual(sentinel.read_bytes(), b"untouched")
        self.assertFalse(any(cmd[1] == "compile" for cmd in self.calls))

    def test_repo_private_paths_rejected(self):
        for config, output in ((self.repo / SOURCE, self.output), (self.config, self.repo / "release")):
            with self.subTest(config=config, output=output), self.assertRaises(release.ReleaseError):
                release.build_release(self.repo, config, output)

    def test_dirty_tree_including_untracked_rejected(self):
        original = self.fake_run
        with mock.patch.object(release, "run", side_effect=lambda a, c: b"?? extra\n" if a[1] == "status" else original(a, c)):
            with self.assertRaises(release.ReleaseError):
                self.build()
        self.assertFalse(self.output.exists())

    def test_remote_missing_unreachable_or_different_rejected(self):
        for remote in (b"", ("c" * 40 + "\trefs/heads/main\n").encode(), None):
            original = self.fake_run
            def command(args, cwd):
                if args[1] == "ls-remote":
                    if remote is None:
                        raise release.ReleaseError("offline")
                    return remote
                return original(args, cwd)
            with self.subTest(remote=remote), mock.patch.object(release, "run", side_effect=command):
                with self.assertRaises(release.ReleaseError):
                    self.build()
            self.assertFalse(self.output.exists())

    def test_wrong_toolchain_versions_rejected(self):
        original = self.fake_run
        for cmd in ("version", "core"):
            def command(args, cwd):
                if args[0] == "arduino-cli" and args[1] == cmd:
                    return b'{"VersionString":"0.0.0","platforms":[]}'
                return original(args, cwd)
            with self.subTest(cmd=cmd), mock.patch.object(release, "run", side_effect=command):
                with self.assertRaises(release.ReleaseError):
                    self.build()
            self.assertFalse(self.output.exists())

    def test_worktree_source_differs_from_git_rejected(self):
        (self.repo / SOURCE).write_bytes(b"changed despite clean status")
        with self.assertRaises(release.ReleaseError):
            self.build()

    def test_tracked_deployment_config_rejected_without_reading_secret(self):
        original = self.fake_run
        def command(args, cwd):
            if args[1] == "ls-tree":
                return b"100644 blob " + b"b" * 40 + b"\t" + (release.SKETCH + "/deployment_config.h").encode() + b"\0"
            return original(args, cwd)
        with mock.patch.object(release, "run", side_effect=command), self.assertRaises(release.ReleaseError):
            self.build()
        self.assertFalse(any(cmd[1] == "show" for cmd in self.calls))

    def test_symlink_source_git_mode_rejected(self):
        original = self.fake_run
        def command(args, cwd):
            result = original(args, cwd)
            return result.replace(b"100644", b"120000") if args[1] == "ls-tree" else result
        with mock.patch.object(release, "run", side_effect=command), self.assertRaises(release.ReleaseError):
            self.build()

    def test_compiler_failure_removes_private_staging_and_omits_manifest(self):
        original = self.fake_run
        def command(args, cwd):
            if args[1] == "compile":
                self.staging = Path(args[-1])
                raise release.ReleaseError("compiler failed")
            return original(args, cwd)
        with mock.patch.object(release, "run", side_effect=command), self.assertRaises(release.ReleaseError):
            self.build()
        self.assertFalse(self.staging.exists())
        self.assertFalse((self.output / "manifest.json").exists())

    def test_manifest_identity_and_schema_tampering(self):
        self.build()
        original = (self.output / "manifest.json").read_bytes()
        for key, value in (("schema_version", True), ("schema_version", 2), ("commit", "b" * 40),
                           ("fqbn", "other"), ("core_version", "other"), ("cli_version", "other"),
                           ("sketch", "../other"), ("config", "forbidden"),
                           ("repository", "other"), ("created_at", "invalid"),
                           ("created_at", "2026-99-99T00:00:00+00:00")):
            with self.subTest(key=key, value=value):
                (self.output / "manifest.json").write_bytes(original)
                self.tamper(lambda m: m.update({key: value}))

    def test_manifest_source_hash_or_missing_source_rejected(self):
        self.build()
        self.tamper(lambda m: m["sources"].update({SOURCE: "0" * 64}))
        self.tamper(lambda m: m.update(sources={}))

    def test_artifact_traversal_absolute_and_windows_paths_rejected(self):
        self.build()
        entry = next(iter(self.manifest()["artifacts"].values()))
        for path in ("../outside.bin", "/tmp/out.bin", "build/../out.bin", "build//out.bin",
                     "build/./out.bin", "C:/out.bin", "build\\out.bin", "build/out.bin:stream", "other/out.bin"):
            with self.subTest(path=path):
                self.tamper(lambda m: m.update(artifacts={path: entry}))

    def test_artifact_data_hash_size_and_inventory_tampering(self):
        self.build()
        original = (self.output / "manifest.json").read_bytes()
        for field, value in (("sha256", "0" * 64), ("sha256", "invalid"), ("size", 1), ("size", True)):
            (self.output / "manifest.json").write_bytes(original)
            with self.subTest(field=field, value=value):
                self.tamper(lambda m: next(iter(m["artifacts"].values())).update({field: value}))
        (self.output / "manifest.json").write_bytes(original)
        self.tamper(lambda m: m["artifacts"].pop(next(iter(m["artifacts"]))))
        (self.output / "manifest.json").write_bytes(original)
        (self.output / "build" / "extra.bin").write_bytes(b"extra")
        with self.assertRaises(release.ReleaseError):
            release.verify_release(self.output, self.repo)

    def test_modified_or_missing_artifact_rejected(self):
        self.build()
        artifact = self.output / next(iter(self.manifest()["artifacts"]))
        artifact.write_bytes(b"tampered")
        with self.assertRaises(release.ReleaseError):
            release.verify_release(self.output, self.repo)
        artifact.unlink()
        with self.assertRaises(release.ReleaseError):
            release.verify_release(self.output, self.repo)

    def test_duplicate_json_keys_rejected(self):
        self.build()
        path = self.output / "manifest.json"
        content = path.read_text()
        path.write_text('{"schema_version":1,' + content[1:])
        with self.assertRaises(release.ReleaseError):
            release.verify_release(self.output, self.repo)

    @unittest.skipUnless(os.name == "posix", "Permisos y symlinks Ubuntu/POSIX")
    def test_private_permissions_and_artifact_symlinks_rejected(self):
        os.chmod(self.config, 0o644)
        with self.assertRaises(release.ReleaseError):
            self.build()
        os.chmod(self.config, 0o600)
        self.build()
        artifact = self.output / next(iter(self.manifest()["artifacts"]))
        artifact.unlink()
        artifact.symlink_to(self.config)
        with self.assertRaises(release.ReleaseError):
            release.verify_release(self.output, self.repo)


class RunnerTests(unittest.TestCase):
    def test_run_discards_failed_tool_output(self):
        result = subprocess.CompletedProcess([], 1, b"synthetic private stdout", b"synthetic private stderr")
        with mock.patch.object(release.subprocess, "run", return_value=result):
            with self.assertRaises(release.ReleaseError) as error:
                release.run(["arduino-cli", "compile"], Path.cwd())
        self.assertNotIn("private", str(error.exception))

    def test_run_uses_argument_list_no_shell(self):
        result = subprocess.CompletedProcess([], 0, b"ok", b"")
        with mock.patch.object(release.subprocess, "run", return_value=result) as command:
            self.assertEqual(release.run(["git", "status"], Path.cwd()), b"ok")
        self.assertNotIn("shell", command.call_args.kwargs)


if __name__ == "__main__":
    unittest.main()
