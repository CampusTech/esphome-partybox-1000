"""Unsupported example cases must fail for the intended validation reason."""

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parents[2] / "tools"
with patch.object(sys, "path", [str(TOOLS), *sys.path]):
    spec = importlib.util.spec_from_file_location(
        "check_config", TOOLS / "check_config.py"
    )
    checks = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(checks)

SDK_ERROR = "partybox requires the ESP-IDF framework at version 5.5.5"


class RejectedConfigTests(unittest.TestCase):
    def test_unrelated_failure_cannot_satisfy_negative_case(self):
        result = subprocess.CompletedProcess([], 1, "YAML parse error: invalid syntax")
        with patch.object(checks.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(RuntimeError, "YAML parse error"):
                checks.esphome(
                    Path("wrong-sdk.yaml"),
                    "config",
                    success=False,
                    expected_error=SDK_ERROR,
                )

    def test_expected_validation_failure_is_accepted(self):
        result = subprocess.CompletedProcess([], 2, f"Failed config\n{SDK_ERROR}.\n")
        with patch.object(checks.subprocess, "run", return_value=result):
            self.assertEqual(
                checks.esphome(
                    Path("wrong-sdk.yaml"),
                    "config",
                    success=False,
                    expected_error=SDK_ERROR,
                ),
                result.stdout,
            )

    def test_accepted_unsupported_config_still_fails(self):
        result = subprocess.CompletedProcess([], 0, SDK_ERROR)
        with patch.object(checks.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(
                RuntimeError, "Unsupported configuration was accepted"
            ):
                checks.esphome(
                    Path("wrong-sdk.yaml"),
                    "config",
                    success=False,
                    expected_error=SDK_ERROR,
                )

    def test_negative_case_requires_an_expected_diagnostic(self):
        result = subprocess.CompletedProcess([], 1, "arbitrary failure")
        with patch.object(checks.subprocess, "run", return_value=result):
            with self.assertRaisesRegex(ValueError, "expected diagnostic"):
                checks.esphome(Path("wrong-sdk.yaml"), "config", success=False)


if __name__ == "__main__":
    unittest.main()
