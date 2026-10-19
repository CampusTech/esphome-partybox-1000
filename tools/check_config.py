#!/usr/bin/env python3
"""Validate/generate public examples in isolation; optionally compile and verify ELF.

Only public source and deterministic test credentials enter the temporary project.
No device upload or discovery command is invoked.
"""

import argparse
import importlib.metadata
import importlib.util
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from verify_build import verify_build

ROOT = Path(__file__).resolve().parents[1]
TEST_SECRETS = """# Public test values, never deployment credentials.
wifi_ssid: PartyBox-CI
wifi_password: public-test-password
setup_password: public-setup-password
api_key: AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=
speaker_mac: '02:00:00:00:00:01'
"""


def esphome(config, *arguments, success=True, expected_error=None, stream=False):
    if not success and not expected_error:
        raise ValueError("Negative configuration checks require an expected diagnostic")
    result = subprocess.run(
        [sys.executable, "-m", "esphome", *arguments, str(config)],
        text=True,
        stdout=None if stream else subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if success and result.returncode:
        raise RuntimeError(
            result.stdout
            or f"ESPHome {' '.join(arguments)} failed for {config.name} "
            f"(exit code {result.returncode}); see output above"
        )
    if not success and not result.returncode:
        raise RuntimeError(f"Unsupported configuration was accepted: {config.name}")
    if not success and expected_error not in result.stdout:
        raise RuntimeError(
            f"{config.name} failed without expected diagnostic {expected_error!r}:\n{result.stdout}"
        )
    return result.stdout or ""


def generated_entity_flags(generated, name):
    match = re.search(
        rf'App\.register_\w+\([^,]+, "{re.escape(name)}", [^,]+, (\d+)(?:UL)?\)',
        generated,
    )
    if not match:
        raise ValueError(f"Generated entity metadata was not found for {name}")
    return int(match[1])


def prepare(workspace):
    for name in ("components", "examples", "packages"):
        shutil.copytree(
            ROOT / name,
            workspace / name,
            dirs_exist_ok=True,
            ignore=shutil.ignore_patterns(".esphome", "__pycache__", "secrets.yaml"),
        )
    (workspace / "examples/secrets.yaml").write_text(TEST_SECRETS)


def run_checks(workspace, examples, flash, compile_target=False, negative=True):
    subprocess.run(
        [sys.executable, str(ROOT / "tests/partybox/test_config.py")], check=True
    )
    prepare(workspace)
    full = workspace / "examples/partybox-1000.yaml"
    full.write_text(
        full.read_text().replace(
            "partybox_flash_size: 4MB", f"partybox_flash_size: {flash}"
        )
    )
    for example in examples:
        config = (
            workspace
            / "examples"
            / {
                "minimal": "minimal.yaml",
                "rgb": "rgb.yaml",
                "full": "partybox-1000.yaml",
            }[example]
        )
        esphome(config, "config")
        esphome(config, "compile", "--only-generate")
        sources = list((workspace / "examples/.esphome/build").glob("*/src/main.cpp"))
        matching = [
            source for source in sources if "set_peer_address" in source.read_text()
        ]
        if not matching:
            raise ValueError("Generated PartyBox C++ was not found")
        source = max(matching, key=lambda item: item.stat().st_mtime_ns)
        generated = source.read_text()
        if "set_post_connect_roaming(false)" not in generated:
            raise ValueError("Generated Wi-Fi configuration must disable roaming")
        if "set_spp_enabled(true)" not in generated:
            raise ValueError("SPP must be enabled by default")
        if example == "full":
            if not generated_entity_flags(generated, "Free heap") & (1 << 25):
                raise ValueError("Low-level diagnostics must start disabled in HA")
            if generated_entity_flags(generated, "Player") & (1 << 25):
                raise ValueError("The media player must start enabled in HA")
        if example == "rgb":
            if "set_status_light(" not in generated or "set_pin(16)" not in generated:
                raise ValueError("RGB example must attach the GPIO16 status light")
        elif "set_status_light(" in generated:
            raise ValueError("RGB must remain opt-in")
        if compile_target:
            print(f"Compiling {example} ({flash})", flush=True)
            esphome(config, "compile", stream=True)
            build = source.parents[1] / "build"
            report = verify_build(build)
            print(
                f"PASS: {example} {flash}: {report['sdk']}, all {len(report['hooks'])} linked hooks"
            )
        if negative:
            for label, override, diagnostic in [
                (
                    "wrong-sdk",
                    "esp32:\n  framework:\n    version: 5.5.4\n",
                    "partybox requires the ESP-IDF framework at version 5.5.5",
                ),
                (
                    "wrong-chip",
                    "esp32:\n  board: esp32-s3-devkitc-1\n",
                    "This feature is only available on ESP32",
                ),
            ]:
                rejected = config.with_name(label + ".yaml")
                rejected.write_text(config.read_text() + "\n" + override)
                esphome(rejected, "config", success=False, expected_error=diagnostic)
        print(
            f"PASS: {example} {flash} schema, generated roaming/SPP settings and unsupported targets",
            flush=True,
        )

    base = (
        (workspace / "examples/partybox-1000.yaml")
        .read_text()
        .split("\npartybox:\n", 1)[0]
    )
    # Exercise omission of every supported entity, not only the example subset.
    spec = importlib.util.spec_from_file_location(
        "partybox_schema", ROOT / "components/partybox/__init__.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    cases = {
        "disabled": "".join(f"  {key}: false\n" for key in module.DEFAULT_NAMES),
        "overrides": (
            "  media_player:\n    name: Office player\n"
            "  bass:\n    icon: mdi:music\n"
            "  last_word: false\n"
            "  free_heap:\n    disabled_by_default: false\n"
        ),
    }
    for label, options in cases.items():
        config = workspace / "examples" / f"entities-{label}.yaml"
        config.write_text(
            base + "\npartybox:\n  mac_address: !secret speaker_mac\n" + options
        )
        esphome(config, "compile", "--only-generate")
        source = workspace / "examples/.esphome/build/partybox-1000/src/main.cpp"
        generated = source.read_text()
        if label == "disabled":
            if any(
                f"partybox::{kind}(" in generated
                for kind in ("Player", "Choice", "Action")
            ):
                raise ValueError("Disabled entity objects were generated")
            if any(
                f"->set_{kind}(" in generated
                for kind in ("player", "choice", "sensor", "binary", "text")
            ):
                raise ValueError("Disabled entities were attached to the controller")
        else:
            if generated_entity_flags(generated, "Free heap") & (1 << 25):
                raise ValueError("User diagnostic visibility override was ignored")
            if '"Office player"' not in generated or '"Bass boost"' not in generated:
                raise ValueError("Entity name/default overrides were not generated")
            if '"Last TWS word"' in generated:
                raise ValueError("Explicitly disabled last_word was generated")
        print(f"PASS: {label} entity code generation", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--example", choices=["all", "full", "minimal", "rgb"], default="all"
    )
    parser.add_argument("--flash", choices=["4MB", "8MB"], default="4MB")
    parser.add_argument(
        "--compile",
        action="store_true",
        help="Compile locally and verify linked ELF; never upload",
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        help="Preserve isolated generated project here (must not exist)",
    )
    args = parser.parse_args()
    if importlib.metadata.version("esphome") != "2026.9.1":
        parser.error("Install the pinned requirements.txt (ESPHome 2026.9.1)")
    examples = ["full", "minimal", "rgb"] if args.example == "all" else [args.example]
    try:
        if args.work_dir:
            args.work_dir.mkdir(parents=True, exist_ok=False)
            run_checks(args.work_dir.resolve(), examples, args.flash, args.compile)
        else:
            with tempfile.TemporaryDirectory(prefix="partybox-ci-") as folder:
                run_checks(Path(folder), examples, args.flash, args.compile)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Configuration check failed: {error}\n")


if __name__ == "__main__":
    main()
