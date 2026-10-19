"""Validate entity defaults and overrides with the pinned ESPHome schemas."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from esphome import config_validation as cv
from esphome.core import CORE

from components.partybox import CONFIG_SCHEMA


class EntityConfigTests(unittest.TestCase):
    def setUp(self):
        CORE.reset()
        CORE.data["core"] = {"target_platform": "esp32"}
        CORE.data["esp32"] = {"variant": "ESP32"}
        CORE.friendly_name = "PartyBox 1000"

    def validate(self, **entities):
        return CONFIG_SCHEMA({"mac_address": "02:00:00:00:00:01", **entities})

    def test_mac_only_enables_public_entities_with_existing_names(self):
        config = self.validate()
        entities = [value for value in config.values() if isinstance(value, dict)]
        self.assertEqual(len(entities), 38)
        self.assertTrue(all("name" in entity for entity in entities))
        self.assertTrue(all(not entity.get("internal", False) for entity in entities))
        for key, name in {
            "media_player": "Player",
            "source": "Source",
            "bass": "Bass boost",
            "lighting": "Lighting",
            "connect": "Connect Bluetooth",
            "standby": "Standby",
            "wake": "Wake",
            "query_version": "Query firmware version",
            "tws_connected": "TWS connected",
            "spp_connected": "SPP connected",
            "signal_activity": "Selected input signal",
            "smart_dj": "Smart DJ reported",
            "voice_cancel": "Voice cancel reported",
            "last_word": "Last TWS word",
            "power_estimate": "Power estimate",
            "firmware_version": "Firmware version",
        }.items():
            with self.subTest(entity=key):
                self.assertIn(key, config)
                self.assertEqual(config[key]["name"], name)
                self.assertFalse(config[key].get("internal", False))

    def test_unconfigured_bridge_has_pairing_and_address_controls(self):
        config = CONFIG_SCHEMA({})
        self.assertNotIn("mac_address", config)
        self.assertEqual(config["pair"]["name"], "Pair speaker")
        self.assertEqual(config["speaker_address"]["name"], "Speaker Bluetooth address")
        self.assertEqual(config["speaker_address"]["mode"], "TEXT")
        self.assertEqual(config["speaker_address"]["entity_category"], "config")

    def test_names_and_other_entity_options_override_defaults(self):
        config = self.validate(
            media_player={"name": "Office player"},
            bass={"icon": "mdi:music"},
            volume_steps={"accuracy_decimals": 1},
            free_heap={"unit_of_measurement": "bytes"},
        )
        self.assertEqual(config["media_player"]["name"], "Office player")
        self.assertEqual(config["bass"]["name"], "Bass boost")
        self.assertEqual(config["bass"]["icon"], "mdi:music")
        self.assertEqual(config["volume_steps"]["accuracy_decimals"], 1)
        self.assertEqual(config["free_heap"]["unit_of_measurement"], "bytes")

    def test_empty_null_and_true_use_default_names(self):
        config = self.validate(media_player={}, source=None, bass=True)
        self.assertEqual(config["media_player"]["name"], "Player")
        self.assertEqual(config["source"]["name"], "Source")
        self.assertEqual(config["bass"]["name"], "Bass boost")

    def test_false_omits_each_entity_kind(self):
        config = self.validate(
            media_player=False,
            source=False,
            connect=False,
            volume_steps=False,
            tws_connected=False,
            volume_confidence=False,
        )
        for key in [
            "media_player",
            "source",
            "connect",
            "volume_steps",
            "tws_connected",
            "volume_confidence",
        ]:
            self.assertIs(config[key], False)
        self.assertEqual(config["bass"]["name"], "Bass boost")

    def test_sensor_units_precision_and_categories_are_defaults(self):
        config = self.validate()
        for key, unit in [
            ("free_heap", "B"),
            ("minimum_heap", "B"),
            ("largest_free_block", "B"),
            ("snapshot_age", "s"),
        ]:
            with self.subTest(entity=key):
                self.assertEqual(config[key]["unit_of_measurement"], unit)
                self.assertEqual(config[key]["accuracy_decimals"], 0)
        self.assertEqual(config["volume_steps"]["accuracy_decimals"], 0)
        self.assertEqual(config["tws_connected"]["entity_category"], "diagnostic")
        self.assertEqual(config["connect"]["entity_category"], "diagnostic")

    def test_low_level_entities_start_disabled_in_home_assistant(self):
        config = self.validate()
        for key in [
            "free_heap",
            "minimum_heap",
            "largest_free_block",
            "volume_steps",
            "received_words",
            "dropped_events",
            "received_audio_bytes",
            "queued_commands",
            "snapshot_age",
            "last_word",
            "last_command",
            "volume_confidence",
            "mute_confidence",
            "source_confidence",
            "power_confidence",
            "bass_confidence",
            "lighting_confidence",
            "activity_confidence",
            "power_estimate",
            "query_version",
            "smart_dj",
            "voice_cancel",
        ]:
            with self.subTest(entity=key):
                self.assertTrue(config[key]["disabled_by_default"])
        for key in [
            "media_player",
            "source",
            "bass",
            "lighting",
            "connect",
            "standby",
            "wake",
            "tws_connected",
            "spp_connected",
            "signal_activity",
            "firmware_version",
        ]:
            with self.subTest(entity=key):
                self.assertFalse(config[key]["disabled_by_default"])

    def test_hidden_defaults_can_be_overridden_without_renaming(self):
        config = self.validate(free_heap={"disabled_by_default": False})
        self.assertFalse(config["free_heap"]["disabled_by_default"])
        self.assertEqual(config["free_heap"]["name"], "Free heap")

    def test_status_light_is_optional_and_brightness_is_validated(self):
        default = self.validate()
        self.assertNotIn("status_light", default)
        self.assertEqual(default["status_brightness"], 0.08)
        self.setUp()
        config = self.validate(status_light="indicator", status_brightness="20%")
        self.assertEqual(str(config["status_light"]), "indicator")
        self.assertEqual(config["status_brightness"], 0.2)
        for brightness in ["-1%", "101%"]:
            self.setUp()
            with self.subTest(brightness=brightness), self.assertRaises(cv.Invalid):
                self.validate(status_brightness=brightness)

    def test_invalid_entity_options_still_fail(self):
        with self.assertRaises(cv.Invalid):
            self.validate(bass={"misspelled_name": "Bass"})


if __name__ == "__main__":
    unittest.main()
