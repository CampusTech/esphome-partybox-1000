#!/usr/bin/env python3
"""Verify all three hooks and the SDK version in a linked ESP32 firmware ELF."""

import argparse
import json
import re
import struct
import subprocess
from pathlib import Path

HOOKS = (
    ("bta_av_action", 0x20, "bta_av_rc_msg"),
    ("bta_av_action", 0x0C, "bta_av_rc_vendor_cmd"),
    ("bta_av_nsm_act", 0x24, "bta_av_rc_closed"),
)
REQUIRED_FLAGS = (
    "CONFIG_BT_CLASSIC_ENABLED",
    "CONFIG_BT_SPP_ENABLED",
    "CONFIG_BT_A2DP_USE_EXTERNAL_CODEC",
    "CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY",
    "CONFIG_ESP_COEX_SW_COEXIST_ENABLE",
)


def load_segments(data):
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Expected little-endian ELF32")
    if struct.unpack_from("<H", data, 18)[0] != 94:
        raise ValueError("Expected Xtensa ELF")
    start = struct.unpack_from("<I", data, 28)[0]
    stride, count = struct.unpack_from("<HH", data, 42)
    if stride < 32 or not count or start + stride * count > len(data):
        raise ValueError("Invalid ELF program header bounds")
    segments = []
    for index in range(count):
        kind, offset, address, _, size, memory_size, _, _ = struct.unpack_from(
            "<8I", data, start + index * stride
        )
        if kind != 1:
            continue
        if (
            size > memory_size
            or offset + size > len(data)
            or address + memory_size > 2**32
        ):
            raise ValueError("Invalid ELF load segment bounds")
        segments.append((offset, address, size))
    return segments


def verify_image(data, symbols, config):
    segments = load_segments(data)

    def linked_bytes(address, size):
        matches = [
            data[offset + address - base : offset + address - base + size]
            for offset, base, length in segments
            if base <= address and address + size <= base + length
        ]
        if len(matches) != 1:
            raise ValueError(f"Expected one file-backed ELF segment at {address:#x}")
        return matches[0]

    def symbol(name):
        if name not in symbols:
            raise ValueError(f"Missing linked symbol: {name}")
        return symbols[name]

    hooks = []
    for table, slot, name in HOOKS:
        address = symbol(table) + slot
        value = struct.unpack("<I", linked_bytes(address, 4))[0]
        if value != symbol("__wrap_" + name):
            raise ValueError(f"{table}+{slot:#x} bypasses {name} wrapper")
        if value == symbol(name):
            raise ValueError(f"{name} original fallback must remain distinct")
        hooks.append(
            {
                "symbol": name,
                "slot_address": hex(address),
                "wrapper_address": hex(value),
            }
        )

    # esp_app_desc_t is the public IDF application descriptor. Read the actual
    # linked IDF version field; a path or an output label cannot establish it.
    descriptor = linked_bytes(symbol("esp_app_desc"), 144)
    if struct.unpack_from("<I", descriptor)[0] != 0xABCD5432:
        raise ValueError("Invalid ESP application descriptor")
    sdk = descriptor[112:144].split(b"\0", 1)[0]
    if sdk != b"v5.5.5":
        raise ValueError(f"Unexpected linked SDK version: {sdk!r}; requires v5.5.5")
    defines = dict(re.findall(r"^#define\s+(\w+)\s+([^\n]+)$", config, re.MULTILINE))
    for flag in REQUIRED_FLAGS:
        if defines.get(flag) != "1":
            raise ValueError(f"Missing {flag}")
    if defines.get("CONFIG_BT_BLE_ENABLED") == "1":
        raise ValueError("BLE must not own this stack")
    return {
        "sdk": "ESP-IDF 5.5.5",
        "hooks": hooks,
        "sdk_flags_verified": list(REQUIRED_FLAGS),
        "ble_disabled": True,
        "verification_scope": "Linked hooks, application SDK version and SDK flags; physical tests separate",
    }


def find_nm(build):
    metadata = json.loads((build / "project_description.json").read_text())
    compiler = Path(metadata["c_compiler"])
    if not compiler.name.endswith("gcc"):
        raise ValueError("Cannot derive matching nm from IDF C compiler")
    candidate = compiler.with_name(compiler.name[:-3] + "nm")
    if not candidate.is_file():
        raise ValueError(f"Matching Xtensa nm not found: {candidate}")
    return str(candidate)


def verify_build(build, nm=None):
    image = build / "firmware.elf"
    data = image.read_bytes()
    load_segments(data)  # Reject malformed input before invoking the toolchain.
    output = subprocess.check_output(
        [nm or find_nm(build), "-a", str(image)], text=True, timeout=30
    )
    symbols = {}
    for line in output.splitlines():
        columns = line.split()
        if len(columns) == 3 and columns[1] != "U":
            try:
                symbols[columns[2]] = int(columns[0], 16)
            except ValueError:
                continue
    return verify_image(data, symbols, (build / "config/sdkconfig.h").read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "build", type=Path, help="ESP-IDF build directory containing firmware.elf"
    )
    parser.add_argument(
        "--nm",
        help="Matching Xtensa nm; defaults to project_description.json toolchain",
    )
    args = parser.parse_args()
    try:
        print(json.dumps(verify_build(args.build, args.nm), indent=2))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Build verification failed: {error}\n")


if __name__ == "__main__":
    main()
