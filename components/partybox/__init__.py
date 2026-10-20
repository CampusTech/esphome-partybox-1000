"""JBL PartyBox 1000 Classic Bluetooth controller, pinned to ESP-IDF 5.5.5."""

from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    binary_sensor,
    button,
    esp32,
    light,
    media_player,
    select,
    sensor,
    text,
    text_sensor,
)
from esphome.const import CONF_DISABLED_BY_DEFAULT, CONF_ID, CONF_NAME
from esphome.const import __version__ as ESPHOME_VERSION
from esphome.core import CORE

DEPENDENCIES = ["esp32"]
AUTO_LOAD = [
    "media_player",
    "select",
    "button",
    "sensor",
    "binary_sensor",
    "text_sensor",
    "text",
]
CONFLICTS_WITH = [
    "esp32_ble",
    "esp32_ble_tracker",
    "bluetooth_proxy",
    "esp32_improv",
    "partybox_bt_probe",
]
ns = cg.esphome_ns.namespace("partybox")
PartyBox = ns.class_("PartyBox", cg.Component)
Player = ns.class_("Player", media_player.MediaPlayer)
Choice = ns.class_("Choice", select.Select)
Action = ns.class_("Action", button.Button)
TargetAddress = ns.class_("TargetAddress", text.Text, cg.Component)
PairingChoice = ns.class_("PairingChoice", select.Select)

CHOICES = {
    "source": (0, ["Bluetooth", "USB", "AUX", "LINE IN"]),
    "bass": (1, ["Off", "Level 1", "Level 2"]),
    "lighting": (2, ["Off", "Pattern 1", "Pattern 2", "Pattern 3", "Pattern 4"]),
}
ACTIONS = {
    "connect": 0,
    "standby": 1,
    "wake": 2,
    "query_version": 3,
    "pair": 4,
    "disconnect": 5,
}
SENSORS = [
    "volume_steps",
    "free_heap",
    "minimum_heap",
    "largest_free_block",
    "received_words",
    "dropped_events",
    "received_audio_bytes",
    "queued_commands",
    "snapshot_age",
]
BINARIES = [
    "tws_connected",
    "spp_connected",
    "signal_activity",
    "smart_dj",
    "voice_cancel",
    "aux_signal",
    "line_in_signal",
    "usb_signal",
    "bluetooth_signal",
]
TEXTS = [
    "volume_confidence",
    "mute_confidence",
    "source_confidence",
    "power_confidence",
    "bass_confidence",
    "lighting_confidence",
    "last_command",
    "firmware_version",
    "last_word",
    "power_estimate",
    "activity_confidence",
]

DEFAULT_NAMES = {
    "speaker_address": "Speaker Bluetooth address",
    "pairing_candidate": "Pairing candidate",
    "pairing_results": "Pairing results",
    "pair": "Pair speaker",
    "disconnect": "Disconnect Bluetooth",
    "media_player": "Player",
    "source": "Source",
    "bass": "Bass boost",
    "lighting": "Lighting",
    "connect": "Connect Bluetooth",
    "standby": "Standby",
    "wake": "Wake",
    "query_version": "Query firmware version",
    "volume_steps": "Volume steps",
    "free_heap": "Free heap",
    "minimum_heap": "Minimum heap",
    "largest_free_block": "Largest free block",
    "received_words": "Received TWS words",
    "dropped_events": "Dropped events",
    "received_audio_bytes": "Received audio bytes",
    "queued_commands": "Queued commands",
    "snapshot_age": "Snapshot age",
    "tws_connected": "TWS connected",
    "spp_connected": "SPP connected",
    "signal_activity": "Selected input signal",
    "smart_dj": "Smart DJ reported",
    "voice_cancel": "Voice cancel reported",
    "aux_signal": "AUX reported signal",
    "line_in_signal": "LINE IN reported signal",
    "usb_signal": "USB reported signal",
    "bluetooth_signal": "Bluetooth reported signal",
    "volume_confidence": "Volume confidence",
    "mute_confidence": "Mute confidence",
    "source_confidence": "Source confidence",
    "power_confidence": "Power confidence",
    "bass_confidence": "Bass confidence",
    "lighting_confidence": "Lighting confidence",
    "last_command": "Last command",
    "firmware_version": "Firmware version",
    "last_word": "Last TWS word",
    "power_estimate": "Power estimate",
    "activity_confidence": "Activity confidence",
}
SENSOR_UNITS = {
    "free_heap": "B",
    "minimum_heap": "B",
    "largest_free_block": "B",
    "snapshot_age": "s",
}

HIDDEN_BY_DEFAULT = (
    set(SENSORS)
    | (set(TEXTS) - {"firmware_version"})
    | {
        "query_version",
        "smart_dj",
        "voice_cancel",
    }
)


def entity_schema(key, schema):
    """Apply names before ESPHome validates entity visibility; false omits it."""

    def with_default_name(value):
        if value is None or value is True:
            value = {}
        if not isinstance(value, dict):
            raise cv.Invalid("Expected entity options or false to omit this entity")
        return {
            CONF_NAME: DEFAULT_NAMES[key],
            CONF_DISABLED_BY_DEFAULT: key in HIDDEN_BY_DEFAULT,
            **value,
        }

    return cv.Any(cv.boolean_false, cv.All(with_default_name, schema))


schema = {
    cv.GenerateID(): cv.declare_id(PartyBox),
    cv.Optional("mac_address"): cv.mac_address,
    cv.Optional("speaker_address", default={}): entity_schema(
        "speaker_address",
        text.text_schema(TargetAddress, mode="TEXT", entity_category="config").extend(
            cv.COMPONENT_SCHEMA
        ),
    ),
    cv.Optional("pairing_candidate", default={}): entity_schema(
        "pairing_candidate",
        select.select_schema(PairingChoice, entity_category="config"),
    ),
    cv.Optional("pairing_results", default={}): entity_schema(
        "pairing_results", text_sensor.text_sensor_schema(entity_category="diagnostic")
    ),
    cv.Optional("spp_enabled", default=True): cv.boolean,
    cv.Optional("status_light"): cv.use_id(light.LightState),
    cv.Optional("status_brightness", default="8%"): cv.percentage,
    cv.Optional("media_player", default={}): entity_schema(
        "media_player", media_player.media_player_schema(Player)
    ),
}
for key in CHOICES:
    schema[cv.Optional(key, default={})] = entity_schema(
        key, select.select_schema(Choice)
    )
for key in ACTIONS:
    schema[cv.Optional(key, default={})] = entity_schema(
        key,
        button.button_schema(
            Action,
            entity_category="diagnostic"
            if key in ("connect", "query_version", "disconnect")
            else cv.UNDEFINED,
        ),
    )
for key in SENSORS:
    schema[cv.Optional(key, default={})] = entity_schema(
        key,
        sensor.sensor_schema(
            entity_category="diagnostic",
            accuracy_decimals=0,
            unit_of_measurement=SENSOR_UNITS.get(key, cv.UNDEFINED),
        ),
    )
for key in BINARIES:
    schema[cv.Optional(key, default={})] = entity_schema(
        key,
        binary_sensor.binary_sensor_schema(
            entity_category="diagnostic" if key.endswith("connected") else cv.UNDEFINED
        ),
    )
for key in TEXTS:
    schema[cv.Optional(key, default={})] = entity_schema(
        key, text_sensor.text_sensor_schema(entity_category="diagnostic")
    )

CONFIG_SCHEMA = cv.All(
    cv.Schema(schema).extend(cv.COMPONENT_SCHEMA),
    esp32.only_on_variant(supported=["ESP32"]),
)


def validate_toolchain(config):
    """Reject SDK layouts that have not passed native hook verification."""
    if ESPHOME_VERSION != "2026.9.1":
        raise cv.Invalid(
            "partybox requires ESPHome 2026.9.1; install the pinned requirements.txt"
        )
    if CORE.target_framework != "esp-idf" or esp32.idf_version() != cv.Version(5, 5, 5):
        raise cv.Invalid("partybox requires the ESP-IDF framework at version 5.5.5")
    return config


FINAL_VALIDATE_SCHEMA = validate_toolchain


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if "mac_address" in config:
        cg.add(var.set_peer_address(config["mac_address"].as_hex))
    if config["speaker_address"] is not False:
        child = cg.new_Pvariable(config["speaker_address"][CONF_ID], var)
        await text.register_text(
            child, config["speaker_address"], min_length=0, max_length=17
        )
        await cg.register_component(child, config["speaker_address"])
        cg.add(var.set_target_text(child))
    if config["pairing_candidate"] is not False:
        child = await select.new_select(
            config["pairing_candidate"],
            var,
            options=["None"] + [f"Candidate {i}" for i in range(1, 9)],
        )
        cg.add(var.set_pairing_choice(child))
    if config["pairing_results"] is not False:
        child = await text_sensor.new_text_sensor(config["pairing_results"])
        cg.add(var.set_pairing_results(child))
    cg.add(var.set_spp_enabled(config["spp_enabled"]))
    if "status_light" in config:
        indicator = await cg.get_variable(config["status_light"])
        cg.add(var.set_status_light(indicator, config["status_brightness"]))
    if config["media_player"] is not False:
        child = await media_player.new_media_player(config["media_player"], var)
        cg.add(var.set_player(child))
    for key, (role, options) in CHOICES.items():
        if config[key] is not False:
            child = await select.new_select(config[key], var, role, options=options)
            cg.add(var.set_choice(role, child))
    for key, role in ACTIONS.items():
        if config[key] is not False:
            await button.new_button(config[key], var, role)
    for names, module, method in [
        (SENSORS, sensor, "set_sensor"),
        (BINARIES, binary_sensor, "set_binary"),
        (TEXTS, text_sensor, "set_text"),
    ]:
        for role, key in enumerate(names):
            if config[key] is not False:
                child = await getattr(
                    module, "new_" + module.__name__.rsplit(".", 1)[-1]
                )(config[key])
                cg.add(getattr(var, method)(role, child))
    esp32.add_idf_component(
        name="partybox_bluedroid",
        path=str(Path(__file__).parent / "partybox_bluedroid"),
    )
    esp32.request_bluetooth()
    esp32.request_software_coexistence()
    for name, value in {
        "CONFIG_BT_BLUEDROID_ENABLED": True,
        "CONFIG_BT_CLASSIC_ENABLED": True,
        "CONFIG_BT_BLE_ENABLED": False,
        "CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY": True,
        "CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN": 1,
        "CONFIG_BT_A2DP_ENABLE": True,
        "CONFIG_BT_A2DP_USE_EXTERNAL_CODEC": True,
        "CONFIG_BT_SPP_ENABLED": True,
        "CONFIG_BT_AVRCP_CT_COVER_ART_ENABLED": False,
    }.items():
        esp32.add_idf_sdkconfig_option(name, value)
    for symbol in ("bta_av_rc_msg", "bta_av_rc_vendor_cmd", "bta_av_rc_closed"):
        cg.add_build_flag("-Wl,--wrap=" + symbol)
