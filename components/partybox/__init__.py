"""JBL PartyBox 1000 Classic Bluetooth controller, pinned to ESP-IDF 5.5.5."""

from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    binary_sensor,
    button,
    esp32,
    media_player,
    select,
    sensor,
    text_sensor,
)
from esphome.const import CONF_ID
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

CHOICES = {
    "source": (0, ["Bluetooth", "USB", "AUX", "LINE IN"]),
    "bass": (1, ["Off", "Level 1", "Level 2"]),
    "lighting": (2, ["Off", "Pattern 1", "Pattern 2", "Pattern 3", "Pattern 4"]),
}
ACTIONS = {"connect": 0, "standby": 1, "wake": 2, "query_version": 3}
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

schema = {
    cv.GenerateID(): cv.declare_id(PartyBox),
    cv.Required("mac_address"): cv.mac_address,
    cv.Optional("spp_enabled", default=True): cv.boolean,
    cv.Optional("media_player"): media_player.media_player_schema(Player),
}
for key in CHOICES:
    schema[cv.Optional(key)] = select.select_schema(Choice)
for key in ACTIONS:
    schema[cv.Optional(key)] = button.button_schema(
        Action,
        entity_category="diagnostic"
        if key in ("connect", "query_version")
        else cv.UNDEFINED,
    )
for key in SENSORS:
    schema[cv.Optional(key)] = sensor.sensor_schema(entity_category="diagnostic")
for key in BINARIES:
    schema[cv.Optional(key)] = binary_sensor.binary_sensor_schema(
        entity_category="diagnostic" if key.endswith("connected") else cv.UNDEFINED
    )
for key in TEXTS:
    schema[cv.Optional(key)] = text_sensor.text_sensor_schema(
        entity_category="diagnostic"
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
    cg.add(var.set_peer_address(config["mac_address"].as_hex))
    cg.add(var.set_spp_enabled(config["spp_enabled"]))
    if "media_player" in config:
        child = await media_player.new_media_player(config["media_player"], var)
        cg.add(var.set_player(child))
    for key, (role, options) in CHOICES.items():
        if key in config:
            child = await select.new_select(config[key], var, role, options=options)
            cg.add(var.set_choice(role, child))
    for key, role in ACTIONS.items():
        if key in config:
            await button.new_button(config[key], var, role)
    for names, module, method in [
        (SENSORS, sensor, "set_sensor"),
        (BINARIES, binary_sensor, "set_binary"),
        (TEXTS, text_sensor, "set_text"),
    ]:
        for role, key in enumerate(names):
            if key in config:
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
