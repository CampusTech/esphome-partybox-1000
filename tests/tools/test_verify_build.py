"""The verifier must inspect linked bytes, including every native hook and SDK."""

import importlib.util
import struct
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "verify_build", Path(__file__).resolve().parents[2] / "tools/verify_build.py"
)
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


HOOKS = [
    ("bta_av_action", 0x20, "bta_av_rc_msg"),
    ("bta_av_action", 0x0C, "bta_av_rc_vendor_cmd"),
    ("bta_av_nsm_act", 0x24, "bta_av_rc_closed"),
]
FLAGS = [
    "CONFIG_BT_CLASSIC_ENABLED",
    "CONFIG_BT_SPP_ENABLED",
    "CONFIG_BT_A2DP_USE_EXTERNAL_CODEC",
    "CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY",
    "CONFIG_ESP_COEX_SW_COEXIST_ENABLE",
]


def linked_fixture():
    data = bytearray(1024)
    data[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into(
        "<HHIIIIIHHHHHH", data, 16, 2, 94, 1, 0, 52, 0, 0, 52, 32, 1, 0, 0, 0
    )
    struct.pack_into("<8I", data, 52, 1, 128, 0x1000, 0x1000, 896, 896, 4, 4)
    symbols = {
        "bta_av_action": 0x1000,
        "bta_av_nsm_act": 0x1040,
        "esp_app_desc": 0x1100,
    }
    for index, (table, slot, symbol) in enumerate(HOOKS):
        symbols[symbol] = 0x2000 + index * 16
        symbols["__wrap_" + symbol] = 0x3000 + index * 16
        struct.pack_into(
            "<I",
            data,
            128 + symbols[table] + slot - 0x1000,
            symbols["__wrap_" + symbol],
        )
    struct.pack_into("<I", data, 384, 0xABCD5432)
    data[496:503] = b"v5.5.5\0"
    config = "".join(f"#define {name} 1\n" for name in FLAGS)
    return data, symbols, config


class VerifyBuildTests(unittest.TestCase):
    def test_accepts_three_linked_wrappers_and_binary_sdk(self):
        data, symbols, config = linked_fixture()
        result = verifier.verify_image(data, symbols, config)
        self.assertEqual(result["sdk"], "ESP-IDF 5.5.5")
        self.assertEqual(len(result["hooks"]), 3)

    def test_rejects_each_bypassed_hook(self):
        for table, slot, symbol in HOOKS:
            with self.subTest(symbol=symbol):
                data, symbols, config = linked_fixture()
                struct.pack_into(
                    "<I", data, 128 + symbols[table] + slot - 0x1000, symbols[symbol]
                )
                with self.assertRaisesRegex(ValueError, "bypasses"):
                    verifier.verify_image(data, symbols, config)

    def test_rejects_missing_hook(self):
        data, symbols, config = linked_fixture()
        del symbols["__wrap_bta_av_rc_closed"]
        with self.assertRaisesRegex(ValueError, "Missing linked symbol"):
            verifier.verify_image(data, symbols, config)

    def test_rejects_wrong_sdk_in_firmware(self):
        data, symbols, config = linked_fixture()
        data[496:503] = b"v5.5.4\0"
        with self.assertRaisesRegex(ValueError, "SDK version"):
            verifier.verify_image(data, symbols, config)

    def test_rejects_missing_sdk_flag(self):
        data, symbols, config = linked_fixture()
        with self.assertRaisesRegex(ValueError, "Missing CONFIG_BT_SPP_ENABLED"):
            verifier.verify_image(
                data, symbols, config.replace("#define CONFIG_BT_SPP_ENABLED 1\n", "")
            )

    def test_rejects_ble_enabled(self):
        data, symbols, config = linked_fixture()
        with self.assertRaisesRegex(ValueError, "BLE"):
            verifier.verify_image(
                data, symbols, config + "#define CONFIG_BT_BLE_ENABLED 1\n"
            )

    def test_rejects_malformed_elf_without_struct_errors(self):
        valid, symbols, config = linked_fixture()
        cases = [b"", b"\x7fELF\x01\x01", bytes(len(valid)), valid[:60]]
        for offset, fmt, value in [
            (18, "<H", 243),
            (28, "<I", 0xFFFFFFF0),
            (42, "<H", 1),
            (56, "<I", 0xFFFFFFF0),
            (68, "<I", 0xFFFFFFF0),
        ]:
            altered = bytearray(valid)
            struct.pack_into(fmt, altered, offset, value)
            cases.append(altered)
        for data in cases:
            with self.subTest(length=len(data)):
                with self.assertRaises(ValueError):
                    verifier.verify_image(data, symbols, config)


if __name__ == "__main__":
    unittest.main()
