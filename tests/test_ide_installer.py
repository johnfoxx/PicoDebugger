#!/usr/bin/env python3
"""Verify the local installation using in-memory archive responses."""

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile


REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("installer", REPO / "scripts/install-ide-tools.py")
INSTALLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALLER)


def archive(member):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as file:
        file.writestr(member, "#!/bin/sh\nexit 0\n")
    return buffer.getvalue()


class LocalToolInstallation(unittest.TestCase):
    def test_verified_archive_registers_executable_and_can_be_reused(self):
        payload = archive("bin/fixture")
        digest = hashlib.sha256(payload).hexdigest()
        package = self.package(digest)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.object(INSTALLER, "request", return_value=io.BytesIO(payload)) as fetch:
                INSTALLER.install(root, package, "linux-x86_64", "git")
                INSTALLER.install(root, package, "linux-x86_64", "git")
            fetch.assert_called_once()
            registered = json.loads((root / "registered/fixture.json").read_text())
            target = Path(registered["clangd"])
            self.assertTrue(target.is_relative_to(root))
            self.assertEqual(target.read_bytes(), b"#!/bin/sh\nexit 0\n")

    def test_wrong_digest_and_traversal_do_not_register_a_package(self):
        for payload, digest in [(archive("bin/fixture"), "0" * 64), (archive("../outside"), None)]:
            with self.subTest(digest=digest), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                package = self.package(digest or hashlib.sha256(payload).hexdigest())
                with patch.object(INSTALLER, "request", return_value=io.BytesIO(payload)), self.assertRaises(ValueError):
                    INSTALLER.install(root, package, "linux-x86_64", "git")
                self.assertFalse((root / "registered/fixture.json").exists())
                self.assertFalse((root / "fixture/outside").exists())

    def test_shipped_catalog(self):
        catalog = json.loads((REPO / "config/dependencies.json").read_text())
        self.assertEqual(catalog["schema"], 1)
        provided = {name for pkg in catalog["packages"] for name in pkg["provides"]}
        self.assertTrue({"gcc", "gdb", "sdk", "clangd", "cmake", "openocd"}.issubset(provided))
        for pkg in catalog["packages"]:
            if pkg.get("repository"):
                INSTALLER.https(pkg["repository"])
                self.assertRegex(pkg["commit"], r"^[a-f0-9]{40}$")
            for asset in pkg.get("platforms", {}).values():
                INSTALLER.https(asset["url"])
                if "sha256" in asset:
                    self.assertRegex(asset["sha256"], r"^[a-f0-9]{64}$")
                else:
                    INSTALLER.https(asset["checksumUrl"])

    @staticmethod
    def package(digest):
        return {"id": "fixture", "name": "Fixture", "version": "1",
                "platforms": {"linux-x86_64": {"url": "https://fixture.invalid/fixture.zip", "sha256": digest}},
                "provides": {"clangd": "bin/fixture"}}


if __name__ == "__main__":
    unittest.main()
