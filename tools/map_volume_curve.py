#!/usr/bin/env python3
"""Recover and verify stock master-volume gains without contacting any hardware.

Optional analysis dependencies: capstone 5.0.7 and unicorn 2.1.4.
Runs the original ARM master setter with modeled DSP acknowledgements; captures
the requested coefficient writes. This does not emulate the DSP or acoustics.
"""

import argparse
import hashlib
import json
import math
import struct
from itertools import pairwise
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x186C00
SENTINEL = 0x43000000
IMAGES = {
    "V63": "PB1000_FW_V63_FULL.bin",
    "V64": "PB1000_FW_V64_FULL.bin",
    "V75": "PB1000_CR_202101070V75.bin",
}
SOURCES = {"USB": 0x04, "LINE IN": 0x09, "AUX": 0x0A, "Bluetooth": 0x0D}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def map_image(path, expected_hash):
    from capstone import CS_ARCH_ARM, CS_MODE_ARM, Cs, arm
    from unicorn import UC_ARCH_ARM, UC_HOOK_CODE, UC_MODE_ARM, Uc
    from unicorn.arm_const import (
        UC_ARM_REG_LR,
        UC_ARM_REG_PC,
        UC_ARM_REG_R0,
        UC_ARM_REG_R1,
        UC_ARM_REG_SP,
    )

    image = path.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    require(digest == expected_hash, f"Reference hash mismatch: {path.name}")
    marker = b"AK7738_SetMasterVolume\0"
    anchor = image.find(marker)
    require(
        anchor >= 0 and image.find(marker, anchor + 1) < 0, "Ambiguous setter marker"
    )
    setter = anchor - 0x174
    decoder = Cs(CS_ARCH_ARM, CS_MODE_ARM)
    decoder.detail = True

    def instruction(offset):
        return next(decoder.disasm(image[offset : offset + 4], offset))

    def literal(delta):
        at = setter + delta
        ins = instruction(at)
        require(ins.mnemonic == "ldr", f"Expected literal load at {at:#x}")
        require(ins.operands[1].mem.base == arm.ARM_REG_PC, "Not a PC-relative load")
        return struct.unpack_from("<I", image, at + 8 + ins.operands[1].mem.disp)[0]

    def branch(delta):
        ins = instruction(setter + delta)
        require(ins.mnemonic == "bl", "Unexpected master-setter call layout")
        return BASE + ins.operands[0].imm

    require(instruction(setter).mnemonic == "push", "Unexpected setter entry")
    ready, source_address = literal(0x08), literal(0x30)
    coefficient_table, index_table = literal(0x34), literal(0x3C)
    tx, ack, clock, logging = [branch(d) for d in (0xA4, 0xB0, 0x130, 0x148)]
    indices = list(struct.unpack_from("<33I", image, index_table - BASE))
    require(all(i < 128 for i in indices), "Unexpected coefficient-table index")
    coefficients = [
        struct.unpack_from("<I", image, coefficient_table - BASE + index * 4)[0]
        for index in indices
    ]
    require(
        coefficients[0] == 0 and coefficients[-1] == 0x200000,
        "Unexpected gain endpoints",
    )
    require(all(a <= b for a, b in pairwise(coefficients)), "Non-monotonic gains")
    cases = []

    for source_name, source in SOURCES.items():
        for level, coefficient in enumerate(coefficients):
            machine = Uc(UC_ARCH_ARM, UC_MODE_ARM)
            for address, size in (
                (0x180000, 0x300000),
                (0x40300000, 0x100000),
                (0x41000000, 0x10000),
                (SENTINEL, 0x1000),
            ):
                machine.mem_map(address, size)
            machine.mem_write(BASE, image[:0x246800])
            machine.mem_write(ready, b"\x01")
            machine.mem_write(source_address, bytes([source]))
            packets = []

            def hook(cpu, address, size, user, captured=packets):
                if address == SENTINEL:
                    cpu.emu_stop()
                    return
                if address == tx:
                    start, length = (
                        cpu.reg_read(UC_ARM_REG_R0),
                        cpu.reg_read(UC_ARM_REG_R1),
                    )
                    require(length in (3, 6), "Unexpected DSP transfer length")
                    captured.append(bytes(cpu.mem_read(start, length)).hex())
                elif address == ack:
                    cpu.reg_write(UC_ARM_REG_R0, 1)
                elif address == clock:
                    cpu.reg_write(UC_ARM_REG_R0, 1234)
                elif address != logging:
                    return
                cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

            machine.hook_add(UC_HOOK_CODE, hook)
            for register, value in (
                (UC_ARM_REG_R0, level),
                (UC_ARM_REG_R1, 0),
                (UC_ARM_REG_SP, 0x4100F000),
                (UC_ARM_REG_LR, SENTINEL),
            ):
                machine.reg_write(register, value)
            machine.emu_start(BASE + setter, SENTINEL + 4, count=10000)
            require(
                machine.reg_read(UC_ARM_REG_PC) == SENTINEL, "Setter did not return"
            )
            require(
                machine.mem_read(ready + 0x6A, 1)[0] == level, "Level state mismatch"
            )
            require(
                packets == [f"80004a{coefficient:06x}", "a50000"], "DSP writes mismatch"
            )
            cases.append({"source": source_name, "level": level, "packets": packets})

    return {
        "file": path.name,
        "sha256": digest,
        "setter_file_offset": hex(setter),
        "setter_runtime_address": hex(BASE + setter),
        "index_table_runtime_address": hex(index_table),
        "coefficient_table_runtime_address": hex(coefficient_table),
        "levels": [
            {
                "level": level,
                "ha_volume_percent": level * 100 / 32,
                "index": indices[level],
                "coefficient_hex": f"{coefficient:06x}",
                "gain_db_relative_to_max": round(
                    20 * math.log10(coefficient / coefficients[-1]), 6
                )
                if coefficient
                else None,
                "nominal_gain_db_relative_to_max": -indices[level]
                if coefficient
                else None,
            }
            for level, coefficient in enumerate(coefficients)
        ],
        "verified_setter_cases": cases,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="Save JSON instead of printing it")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "reference/manifest.json").read_text())
    hashes = {entry["path"]: entry["sha256"] for entry in manifest["files"]}
    versions = {
        version: map_image(
            ROOT / "reference/partybox-1000" / name,
            hashes[f"partybox-1000/{name}"],
        )
        for version, name in IMAGES.items()
    }
    curves = [
        [row["coefficient_hex"] for row in version["levels"]]
        for version in versions.values()
    ]
    require(
        all(curve == curves[0] for curve in curves), "Firmware volume curves differ"
    )
    result = {
        "scope": "Original ARM master setters; all 33 levels, four modeled input selections, three releases",
        "db_interpretation": "20*log10(coefficient/max_coefficient); assumes linear amplitude coefficients",
        "zero_level": "Zero coefficient; gain is negative infinity, represented by null",
        "limitations": [
            "Requested DSP writes were captured; the DSP itself and acoustic output were not emulated or measured.",
            "Hardware transfer and acknowledgements, diagnostic clock and logger were stubbed; notify_ui=0.",
            "Input trims, EQ/bass, amplifier gain, limiters, prompt overrides and speaker sensitivity are outside this curve.",
        ],
        "identical_across_versions": True,
        "verified_cases": sum(
            len(v["verified_setter_cases"]) for v in versions.values()
        ),
        "versions": versions,
    }
    output = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(output)
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
