"""Personal firmware exports contain verified images and no configuration files."""

import hashlib
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[2] / "tools"
with patch.object(sys, "path", [str(TOOLS), *sys.path]):
    spec = importlib.util.spec_from_file_location(
        "build_firmware", TOOLS / "build_firmware.py"
    )
    exporter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(exporter)


class FirmwareExportTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.app = b"personalized-app"
        (self.build / "firmware.ota.bin").write_bytes(self.app)
        (self.build / "firmware.factory.bin").write_bytes(b"\xff" * 0x10000 + self.app)
        (self.build / "secrets.yaml").write_text("password: never-export-me")
        self.output = self.root / "export"

    def export(self):
        return exporter.export_firmware(self.build, self.output, "0.1.0-beta.1", "4MB")

    def test_exports_only_images_and_public_build_metadata_with_checksums(self):
        with patch.object(exporter, "verify_build", return_value={"hooks": [1, 2, 3]}):
            self.export()
        self.assertEqual(
            {p.name for p in self.output.iterdir()},
            {
                "firmware.factory.bin",
                "firmware.ota.bin",
                "build-info.json",
                "SHA256SUMS",
            },
        )
        info = json.loads((self.output / "build-info.json").read_text())
        self.assertTrue(info["personalized"])
        self.assertEqual(info["flash_size"], "4MB")
        self.assertNotIn(
            "never-export-me", (self.output / "build-info.json").read_text()
        )
        for line in (self.output / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ")
            self.assertEqual(
                digest, hashlib.sha256((self.output / name).read_bytes()).hexdigest()
            )

    def test_failed_hook_verification_exports_nothing(self):
        with patch.object(
            exporter, "verify_build", side_effect=ValueError("bypasses hook")
        ):
            with self.assertRaisesRegex(ValueError, "bypasses hook"):
                self.export()
        self.assertFalse(self.output.exists())

    def test_missing_image_exports_nothing(self):
        (self.build / "firmware.ota.bin").unlink()
        with patch.object(exporter, "verify_build", return_value={}):
            with self.assertRaises(OSError):
                self.export()
        self.assertFalse(self.output.exists())

    def test_mismatched_usb_and_ota_images_are_rejected(self):
        (self.build / "firmware.factory.bin").write_bytes(
            b"\xff" * 0x10000 + b"other-app"
        )
        with patch.object(exporter, "verify_build", return_value={}):
            with self.assertRaisesRegex(ValueError, "does not contain"):
                self.export()
        self.assertFalse(self.output.exists())

    def test_existing_destination_is_never_overwritten(self):
        self.output.mkdir()
        (self.output / "keep.txt").write_text("keep")
        with self.assertRaises(FileExistsError):
            self.export()
        self.assertEqual((self.output / "keep.txt").read_text(), "keep")


if __name__ == "__main__":
    unittest.main()
