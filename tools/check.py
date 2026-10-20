#!/usr/bin/env python3
"""Run portable release checks (Python standard library and a C/C++ compiler)."""

import ast
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
SKIP_DIRECTORIES = {
    ".git",
    ".local",
    ".venv",
    "venv",
    "__pycache__",
    ".esphome",
    "node_modules",
}


def source_files(root, suffix):
    for directory, subdirectories, filenames in os.walk(root):
        subdirectories[:] = [
            name for name in subdirectories if name not in SKIP_DIRECTORIES
        ]
        for name in filenames:
            if name.endswith(suffix):
                yield Path(directory) / name


def check_links(root):
    for document in source_files(root, ".md"):
        for raw in re.findall(r"!?\[[^\]]*\]\(([^)]+)\)", document.read_text()):
            target = raw.split(maxsplit=1)[0].strip("<>")
            parsed = urlsplit(target)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            path = (
                root / unquote(parsed.path).lstrip("/")
                if parsed.path.startswith("/")
                else document.parent / unquote(parsed.path)
            )
            if not path.exists():
                raise ValueError(
                    f"Broken local link in {document.relative_to(root)}: {target}"
                )


def check_manifest(root):
    reference = root / "reference"
    manifest_path = reference / "manifest.json"
    if not manifest_path.exists():
        return
    manifest = json.loads(manifest_path.read_text())
    if (
        manifest.get("schema_version"),
        manifest.get("path_base"),
        manifest.get("hash_algorithm"),
    ) != (1, "reference/", "sha256"):
        raise ValueError("Unsupported reference manifest schema")
    for entry in manifest["files"]:
        path = (reference / entry["path"]).resolve()
        if not path.is_relative_to(reference.resolve()):
            raise ValueError(f"Reference path escapes reference/: {entry['path']}")
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        if (
            path.stat().st_size != entry["size"]
            or digest.hexdigest() != entry["sha256"]
        ):
            raise ValueError(f"Reference hash mismatch: {entry['path']}")


def main():
    try:
        for source in source_files(ROOT, ".py"):
            ast.parse(source.read_text(), filename=str(source))
        check_links(ROOT)
        check_manifest(ROOT)
        subprocess.run(
            [sys.executable, "-m", "unittest", "discover", "-s", "tests/tools", "-v"],
            cwd=ROOT,
            check=True,
        )
        compiler = (
            os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
        )
        if not compiler:
            raise ValueError("Install a C++17 compiler (clang++ or g++)")
        with tempfile.TemporaryDirectory(prefix="partybox-core-") as folder:
            binary = Path(folder) / "core-test"
            subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "components/partybox"),
                    str(ROOT / "tests/partybox/core_test.cpp"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
        with tempfile.TemporaryDirectory(prefix="partybox-indicator-") as folder:
            indicator = Path(folder) / "indicator-test"
            subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "components/partybox"),
                    str(ROOT / "tests/partybox/indicator_test.cpp"),
                    "-o",
                    str(indicator),
                ],
                check=True,
            )
            subprocess.run([str(indicator)], check=True)
        for name in ("target", "pairing"):
            with tempfile.TemporaryDirectory(prefix="partybox-target-") as folder:
                binary = Path(folder) / name
                subprocess.run(
                    [
                        compiler,
                        "-std=c++17",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-I",
                        str(ROOT / "components/partybox"),
                        str(ROOT / f"tests/partybox/{name}_test.cpp"),
                        "-o",
                        str(binary),
                    ],
                    check=True,
                )
                subprocess.run([str(binary)], check=True)
        for name in ("test_headers.py", "test_hook.py", "test_runtime.py"):
            subprocess.run(
                [sys.executable, str(ROOT / "tests/partybox" / name)], check=True
            )
    except (
        OSError,
        ValueError,
        SyntaxError,
        KeyError,
        subprocess.SubprocessError,
    ) as error:
        print(f"Release checks failed: {error}", file=sys.stderr)
        return 1
    print(
        "PASS: source syntax, local links, reference hashes, verifier tests and production host regressions"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
