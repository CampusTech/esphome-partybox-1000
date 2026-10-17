"""Release checks reject broken source links and altered reference files."""

import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "check", Path(__file__).resolve().parents[2] / "tools/check.py"
)
checks = importlib.util.module_from_spec(spec)
if spec.loader is not None:
    spec.loader.exec_module(checks)


class SourceCheckTests(unittest.TestCase):
    def test_relative_document_links_are_resolved_from_the_document(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "docs").mkdir()
            (root / "README.md").write_text(
                "[Guide](docs/guide.md#topic) [Web](https://example.com)"
            )
            (root / "docs/guide.md").write_text("[Home](../README.md)")
            checks.check_links(root)
            (root / "docs/guide.md").unlink()
            with self.assertRaisesRegex(ValueError, "Broken local link"):
                checks.check_links(root)

    def test_manifest_rejects_modified_file(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            reference = root / "reference"
            reference.mkdir()
            (reference / "capture.bin").write_bytes(b"original")
            (reference / "manifest.json").write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "path_base": "reference/",
                        "hash_algorithm": "sha256",
                        "files": [
                            {
                                "path": "capture.bin",
                                "size": 8,
                                "sha256": hashlib.sha256(b"original").hexdigest(),
                            }
                        ],
                    }
                )
            )
            checks.check_manifest(root)
            (reference / "capture.bin").write_bytes(b"modified")
            with self.assertRaisesRegex(ValueError, "Reference hash mismatch"):
                checks.check_manifest(root)

    def test_manifest_rejects_path_escape(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "reference").mkdir()
            (root / "outside").write_bytes(b"x")
            (root / "reference/manifest.json").write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "path_base": "reference/",
                        "hash_algorithm": "sha256",
                        "files": [
                            {
                                "path": "../outside",
                                "size": 1,
                                "sha256": hashlib.sha256(b"x").hexdigest(),
                            }
                        ],
                    }
                )
            )
            with self.assertRaisesRegex(ValueError, "escapes"):
                checks.check_manifest(root)


if __name__ == "__main__":
    unittest.main()
