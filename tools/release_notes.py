#!/usr/bin/env python3
"""Check a release tag against the package and print its changelog entry."""

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def release_notes(root, tag):
    if not re.fullmatch(r"v\d+\.\d+\.\d+(?:-(?:alpha|beta|rc)\.\d+)?", tag):
        raise ValueError("Expected a version tag such as v0.1.0-beta.1")
    version = tag[1:]
    package = (root / "packages/partybox-1000.yaml").read_text()
    project = re.search(
        r"^esphome:\n(.*?)(?=^\S|\Z)", package, re.MULTILINE | re.DOTALL
    )
    declared = re.findall(
        r"^    version: (\S+)[ \t]*$", project[1] if project else "", re.MULTILINE
    )
    if declared != [version]:
        raise ValueError(f"Release tag {tag} does not match package version {declared}")
    if (
        f"partybox_component_source: github://CampusTech/esphome-partybox-1000@{tag}\n"
        not in package
    ):
        raise ValueError("Package component source must be pinned to the release tag")
    changelog = (root / "CHANGELOG.md").read_text()
    entry = re.search(
        rf"^## {re.escape(version)}\n(.*?)(?=^## |\Z)",
        changelog,
        re.MULTILINE | re.DOTALL,
    )
    if not entry or not entry[1].strip():
        raise ValueError(f"Missing changelog entry for {version}")
    return entry[1].strip() + (
        f"\n\n[Install this version](https://github.com/CampusTech/esphome-partybox-1000/blob/{tag}/README.md#install)"
        f" · [Pairing guide](https://github.com/CampusTech/esphome-partybox-1000/blob/{tag}/docs/pairing.md)\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag")
    args = parser.parse_args()
    try:
        print(release_notes(ROOT, args.tag), end="")
    except (OSError, ValueError) as error:
        parser.exit(1, f"Release check failed: {error}\n")


if __name__ == "__main__":
    main()
