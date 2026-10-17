# Troubleshooting

## No TWS connection

Confirm an original ESP32, ESPHome 2026.9.1 and ESP-IDF 5.5.5. S3/C3/C6 cannot
provide Bluetooth Classic. Remove BLE tracker/proxy, BLE provisioning and other
Bluetooth-owning components. Use the speaker's Classic address, not its BLE address.

After `Ready for selected speaker's TWS search`, turn the JBL on and hold its
Bluetooth button for about five seconds. Keep other TWS receivers away. The ESP
accepts only the configured speaker, but the speaker may discover another receiver.
An A2DP/AVRCP connection alone is insufficient: wait for a valid TWS word and the
TWS-connected diagnostic. SPP follows automatically.

After either device restarts or the speaker fully powers off, a physical TWS
search may be needed again. Connect Bluetooth is one A2DP attempt; it can fail or
connect without completing TWS admission. It is not a guaranteed recovery button.
EIR/class controller readback in logs describes the ESP's advertised identity,
not proof that the JBL discovered or accepted it.

## Controls stay visible but do nothing

Check TWS/SPP connection, field confidence, power estimate and Last command.
Native ESPHome keeps individual controls visible while the node's API is online;
the component refuses requests whose prerequisites are missing.

- Volume, mute and bass need TWS plus a known awake estimate.
- Lighting also needs an observed secondary lighting flag from a snapshot/report.
- Source and version queries need TWS and usable SPP.
- Standby/wake need a known opposite power estimate and an idle command queue.

`accepted: assumed until reported` means an acknowledgement, not independent
confirmation of effect. A newer report takes precedence. Timeouts are not retried;
check the physical speaker before repeating a power action.

## Player says IDLE, or volume/mute looks stale

IDLE is neutral bridge status when power or TWS state is unknown. It does not
mean that analog music stopped. OFF means requested standby, not verified loss
of power. Use the power/confidence diagnostics alongside the player.

The native API cannot clear individual media-player volume/mute fields. Their
last values remain visible when confidence becomes unknown. Selects and activity
can instead be invalidated. Mute has startup snapshot readback, but no demonstrated
harmless on-demand getter. A control-only reconnect may not produce a new snapshot.

## Source or activity is unknown

Source selection uses SPP; readback comes from TWS. An SPP source ACK establishes
only an assumed selection. Activity needs observed source attribution and a fresh
mask; it is intentionally invalidated by source commands. It may remain unknown
until a new physical source report/snapshot. Bluetooth activity is always unknown.
This sensor detects input signal, including possible noise, rather than audible
output, music metadata or cable presence.

## Speaker will not wake

The verified remote pair is TWS-requested orange-light standby followed by TWS
wake while the control link survives. A dark/full-off speaker disconnects and was
not woken through this path. Restore physical power and rejoin TWS if needed.
Link presence does not establish awake state, and no unconditional power setter
or completed-power getter is available.

## Link drops or the ESP restarts

Start with the example's stationary-radio settings: periodic post-connect Wi-Fi
roaming disabled, Wi-Fi on core 1 and Bluetooth on core 0. Initial Wi-Fi discovery
and reconnection remain available. Keep the pinned toolchain and test one change
at a time. Earlier builds suffered idle ACL timeouts; the current settings passed
bounded quiet/music windows, with no overnight stability claim.

Use a stable independent USB supply. The speaker USB port's power retention in
standby is unverified. Set flash size to the fitted hardware; 8 MB flash does not
increase Bluetooth heap. Following OTA, confirm the running version and uptime:
an early reset can cause ESPHome to roll back to the previous image.

Inspect dropped protocol events, free/minimum heap, largest free block, snapshot
age, link flags and Last command. Overflow deliberately invalidates state and
closes the session. If recovery reports that its close failed, reboot the bridge
and perform a physical TWS rejoin. `spp_enabled: false` provides a startup-only
comparison that disables source/version commands; it is not a proven link-loss fix.

For a bug report, include board/flash size, speaker version bytes, pinned tool
versions, sanitized YAML and logs covering boot, join and failure. Preserve TWS
words, ACK/transaction labels, ACL reasons, reset reasons and counter changes.
RSSI delta is relative to the controller's golden receive-power range, not dBm.
Do not include Wi-Fi/API/OTA secrets or Bluetooth keys. A growing SBC byte counter
does not prove audible output; missing optional log lines do not prove no event.
