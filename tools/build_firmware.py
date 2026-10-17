#!/usr/bin/env python3
"""Compile a personal ESPHome configuration and export verified USB/OTA images."""

import argparse
import hashlib
import importlib.metadata
import json
import subprocess
import sys
import tempfile
from pathlib import Path

from verify_build import verify_build


def export_firmware(build, output, version, flash_size):
    """Export an allowlist of images atomically, after native-hook verification."""
    if output.exists():
        raise FileExistsError(f"Output directory already exists: {output}")
    report = verify_build(build)
    images = {
        name: (build / name).read_bytes()
        for name in (
            "firmware.factory.bin",
            "firmware.ota.bin",
        )
    }
    app = images["firmware.ota.bin"]
    if not app or images["firmware.factory.bin"][0x10000 : 0x10000 + len(app)] != app:
        raise ValueError("Factory image does not contain the OTA app at offset 0x10000")
    info = {
        "project": "campustech.partybox_1000",
        "version": version,
        "esphome": "2026.9.1",
        "flash_size": flash_size,
        "personalized": True,
        "usb_flash_offset": "0x0",
        "verification": report,
    }
    images["build-info.json"] = (json.dumps(info, indent=2) + "\n").encode()
    images["SHA256SUMS"] = "".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in images.items()
    ).encode()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(
        prefix=".partybox-export-", dir=output.parent
    ) as staging:
        stage = Path(staging) / "images"
        stage.mkdir(mode=0o700)
        for name, data in images.items():
            (stage / name).write_bytes(data)
            (stage / name).chmod(0o600)
        stage.rename(output)
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path, help="Personal ESPHome YAML configuration")
    parser.add_argument(
        "--output", type=Path, required=True, help="New private output directory"
    )
    args = parser.parse_args()
    try:
        if importlib.metadata.version("esphome") != "2026.9.1":
            raise ValueError("Install the pinned requirements.txt (ESPHome 2026.9.1)")
        config_path = args.config.resolve(strict=True)
        output = args.output.resolve()
        if output.exists():
            raise FileExistsError(f"Output directory already exists: {output}")
        from esphome.config import read_config
        from esphome.core import CORE

        CORE.reset()
        CORE.config_path = config_path
        config = read_config({})
        if config is None or "partybox" not in config:
            raise ValueError("A valid PartyBox ESPHome configuration is required")
        version = config["esphome"]["project"]["version"]
        flash = config["esp32"]["flash_size"]
        build = Path(CORE.build_path) / "build"
        subprocess.run(
            [sys.executable, "-m", "esphome", "compile", str(config_path)], check=True
        )
        export_firmware(build, output, version, flash)
        print(f"Verified personal USB/OTA firmware exported to {output}")
        print(
            "These images contain your credentials and speaker address; keep them private."
        )
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Firmware export failed: {error}\n")


if __name__ == "__main__":
    main()
