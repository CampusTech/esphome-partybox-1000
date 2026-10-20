"""Portable headers must compile without a caller's incidental includes."""

import os
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class StandaloneHeaderTests(unittest.TestCase):
    def test_portable_headers_are_self_contained(self):
        compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "Install a C++17 compiler")
        for header in ("core.h", "indicator.h", "target.h"):
            with self.subTest(header=header):
                result = subprocess.run(
                    [
                        compiler,
                        "-std=c++17",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-I",
                        str(ROOT / "components/partybox"),
                        "-x",
                        "c++",
                        "-fsyntax-only",
                        "-",
                    ],
                    input=f'#include "{header}"\n',
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
