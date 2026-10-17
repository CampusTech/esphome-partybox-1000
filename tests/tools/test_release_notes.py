"""Release tags must agree with the package and select the correct changelog."""

import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "release_notes", Path(__file__).resolve().parents[2] / "tools/release_notes.py"
)
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseNotesTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        (self.root / "packages").mkdir()
        (self.root / "packages/partybox-1000.yaml").write_text(
            "substitutions:\n  partybox_component_source: github://CampusTech/esphome-partybox-1000@v0.1.0-beta.1\n"
            "esphome:\n  project:\n    version: 0.1.0-beta.1\n"
            "esp32:\n  framework:\n    version: 5.5.5\n"
        )
        (self.root / "CHANGELOG.md").write_text(
            "# Changelog\n\n## 0.1.0-beta.1\n\nFirst beta.\n\nContributors: Robbie.\n\n"
            "## 0.0.1\n\nOlder content.\n"
        )

    def test_notes_select_one_version_and_keep_contributors(self):
        notes = release.release_notes(self.root, "v0.1.0-beta.1")
        self.assertIn("First beta.", notes)
        self.assertIn("Contributors: Robbie.", notes)
        self.assertNotIn("Older content", notes)
        self.assertIn("/blob/v0.1.0-beta.1/README.md", notes)

    def test_rejects_tag_version_mismatch(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            release.release_notes(self.root, "v0.2.0-beta.1")

    def test_rejects_component_source_pointing_to_another_version(self):
        p = self.root / "packages/partybox-1000.yaml"
        p.write_text(p.read_text().replace("@v0.1.0-beta.1", "@main"))
        with self.assertRaisesRegex(ValueError, "component source"):
            release.release_notes(self.root, "v0.1.0-beta.1")

    def test_rejects_missing_changelog_entry(self):
        (self.root / "CHANGELOG.md").write_text("# Changelog\n")
        with self.assertRaisesRegex(ValueError, "changelog"):
            release.release_notes(self.root, "v0.1.0-beta.1")


if __name__ == "__main__":
    unittest.main()
