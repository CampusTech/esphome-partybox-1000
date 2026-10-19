# Sources and bass modes inside the media player

The pinned ESPHome native API does not carry media-player source or sound-mode
fields. The [Universal Media Player](https://www.home-assistant.io/integrations/universal/)
built into Home Assistant can combine the existing ESPHome player and selects.
No ESP32 firmware update, MQTT or custom HA integration is required.

1. Copy [the HA example](../examples/home-assistant.yaml) into Home Assistant's
   `configuration.yaml`, merging its `media_player` list with any existing list.
   This is a Home Assistant configuration, not an ESPHome configuration.
2. Replace every example entity ID with your actual ESPHome player, Source and
   Bass boost entity IDs. Give each speaker a different `unique_id`.
3. Check the HA configuration, then restart HA for the first installation.
   Once Universal is loaded, later edits can use the `universal.reload` action.
4. Use the new combined player in dashboards. Keep the original ESPHome player
   and selects enabled; they supply its state and accept its commands.

The source list follows the Source select: Bluetooth, USB, AUX and LINE IN.
Sound modes follow Bass boost: Off, Level 1 and Level 2. Changing either the
wrapper or the underlying select updates the same speaker setting. Volume,
mute, power and their existing confidence limitations come from the ESPHome
child. The wrapper does not add play/pause, track metadata or new readback.

The active child is fixed to the one ESPHome player so that volume and wake
features remain available when its state is standby/off. Its state template
mirrors the child's state, including unknown/unavailable values.
