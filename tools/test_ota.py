"""Smoke tests for the OTA tooling."""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
import unittest

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools.ota import load_manifest
from tools.ota.package import package_build

OTA_ROOT = REPO_ROOT / "ota"


class ManifestTests(unittest.TestCase):
    def test_manifest_loads(self) -> None:
        manifest = load_manifest(OTA_ROOT / "manifest.json")
        self.assertGreaterEqual(manifest.schema_version, 1)
        self.assertIn("0.0.1", manifest.versions)
        self.assertEqual(manifest.product["name"], "TinyBMS Web Gateway")


class PackagingTests(unittest.TestCase):
    def test_package_build_updates_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            tmp_ota = tmp_path / "ota"
            shutil.copytree(OTA_ROOT, tmp_ota)

            binary = tmp_path / "firmware.bin"
            binary.write_bytes(b"temporary firmware")

            version = "9.9.9-test"
            manifest_path = tmp_ota / "manifest.json"
            result_path = package_build(
                manifest_path=manifest_path,
                binary_path=binary,
                version=version,
                project="TinyBMS Web Gateway",
                output_directory=tmp_ota / "versions",
                update_manifest=True,
            )

            self.assertTrue(result_path.exists(), "Version manifest was not created")
            manifest_data = json.loads(manifest_path.read_text(encoding="utf-8"))
            entries = {entry["id"]: entry for entry in manifest_data["versions"]}
            self.assertIn(version, entries)
            self.assertTrue((tmp_ota / entries[version]["manifest"]).exists())

    def test_package_cli_dry_run(self) -> None:
        binary = OTA_ROOT / "versions" / "0.0.1" / "TinyBMS_Web_Gateway-0.0.1.bin"
        result = subprocess.run(
            [
                sys.executable,
                "tools/ota/package.py",
                "--manifest",
                str(OTA_ROOT / "manifest.json"),
                "--binary",
                str(binary),
                "--version",
                "0.0.1",
                "--dry-run",
            ],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertIn("[dry-run]", result.stdout + result.stderr)


class DeployTests(unittest.TestCase):
    def test_deploy_cli_dry_run(self) -> None:
        subprocess.run(
            [
                sys.executable,
                "tools/ota/deploy.py",
                "--manifest",
                str(OTA_ROOT / "manifest.json"),
                "--version",
                "0.0.1",
                "--dry-run",
            ],
            cwd=REPO_ROOT,
            check=True,
        )


if __name__ == "__main__":
    unittest.main()
