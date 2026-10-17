# ESPHome for JBL PartyBox 1000

Control a stock JBL PartyBox 1000 from Home Assistant using an original ESP32
and ESPHome's encrypted native API. The ESP32 joins the speaker's Classic
Bluetooth TWS session and uses SPP for input selection and firmware queries.
No speaker modification, speaker firmware flash, MQTT broker or audio wiring
is required. Use one ESP32 per speaker.

**Beta.** Tested with speaker software `75/32/17/15/7`, ESPHome **2026.9.1**
and ESP-IDF **5.5.5**. Keep these versions pinned: the component hooks internal
Bluetooth stack functions. ESP32-WROOM-32/32E and ESP32-PICO-D4 support the
required Bluetooth Classic transport. ESP32-S3, C3 and C6 do not.

## Controls

| Entity | Support |
|---|---|
| Media player | Volume in 33 steps (0–32), application mute/unmute, guarded standby/wake |
| Source select | Bluetooth, USB, AUX, LINE IN |
| Bass select | Off, Level 1, Level 2 |
| Lighting select | Off, Pattern 1–4; visual names remain unmapped |
| Signal activity | Selected AUX/LINE IN/USB electrical signal; Bluetooth unknown |
| Diagnostics | Link status, field confidence, power estimate, firmware version, last command, heap and counters |

The player exposes no play/pause, track controls or metadata. Signal activity
does not establish audible music. Power is an estimate: `OFF` means requested
standby; neutral `IDLE` means power/link state is unknown. A Bluetooth connection
alone cannot establish whether the speaker is awake.

State confidence is `observed` after speaker readback, `assumed` after an accepted
command, or `unknown` after a gap. Mute readback is available in a startup
snapshot, with no demonstrated harmless on-demand getter. Native ESPHome cannot
make individual controls unavailable while its API remains connected, or clear
the player's individual volume/mute fields. Inspect confidence entities when
those retained values matter. See [protocol and state semantics](docs/protocol.md).

## Install

1. Use an original ESP32 with USB power. The examples default to **4 MB** flash;
   set `flash_size: 8MB` only for a board fitted with 8 MB. More flash adds no RAM.
2. Clone this repository and choose [the complete configuration](examples/partybox-1000.yaml)
   or [the minimal configuration](examples/minimal.yaml). Give each node a unique
   name. The default configuration includes all controls and diagnostics. The
   minimal example opts out of everything except the player and link status.
   Keep the version pins and radio settings.

   ```sh
   git clone https://github.com/CampusTech/esphome-partybox-1000.git
   cd esphome-partybox-1000
   ```

3. Create `examples/secrets.yaml` with the following private values:

   ```yaml
   wifi_ssid: "Your Wi-Fi"
   wifi_password: "Your Wi-Fi password"
   api_key: "BASE64_32_BYTE_KEY"
   setup_password: "A private recovery AP password"
   speaker_mac: "AA:BB:CC:DD:EE:FF"
   ```

   Use the speaker's **Classic Bluetooth** address, which may differ from its BLE
   address. Generate an API/OTA encryption key with
   `python3 -c 'import secrets, base64; print(base64.b64encode(secrets.token_bytes(32)).decode())'`.
   The recovery AP password must contain 8–63 characters. Do not commit secrets.
4. Install and flash from the repository root:

   ```sh
   python3.13 -m venv .venv
   .venv/bin/pip install -r requirements.txt
   .venv/bin/esphome run examples/partybox-1000.yaml
   ```

   First flash uses USB; later updates can use encrypted OTA. The recovery AP and
   captive portal allow Wi-Fi recovery. BLE provisioning/proxy cannot share this
   component's Bluetooth controller.
5. Turn the speaker on, keep other TWS receivers away, then hold its Bluetooth
   button for about five seconds to start TWS search. Wait for `TWS connected`
   and `SPP connected`. Add the ESPHome node to Home Assistant with its API key.

A physical TWS rejoin may be required after either device reboots or the speaker
fully powers off. **Connect Bluetooth** makes one selected-peer A2DP connection
attempt; it does not guarantee TWS rejoin. Standby/wake require a known power
estimate and a retained TWS link. They are never sent on boot or retried
automatically. A dark, fully powered-off speaker was not woken by this path.

For use outside this checkout, replace the local `external_components` source:

```yaml
external_components:
  - source: github://CampusTech/esphome-partybox-1000@main
    components: [partybox]
```

Pin a reviewed commit instead of `main` for a fixed deployment. All PartyBox
entities are created with built-in names, units and precision. Only the speaker
MAC is required under `partybox:`:

```yaml
partybox:
  mac_address: !secret speaker_mac
```

Add an entity section only to override its settings, or use `false` to omit it:

```yaml
partybox:
  mac_address: !secret speaker_mac
  media_player:
    name: Office player
  bass:
    icon: mdi:music
  last_word: false
```

Empty sections (`bass: {}` or `bass:`) keep the defaults. Existing configurations
with explicit names continue to work. To retain a deliberately smaller entity
set, opt out of unwanted entities as shown in [the minimal example](examples/minimal.yaml).
A separate source select is necessary because ESPHome's native media-player API
has no source-selection field.

## Validation and limits

With periodic post-connect Wi-Fi roaming disabled, one quiet LINE IN window
passed 17 minutes with SPP disabled; a separate music-playing window passed
10 minutes 45 seconds with SPP enabled. Both retained TWS and the native API
without reset or dropped protocol events. Home Assistant standby/wake was also
physically confirmed. These are bounded tests, not an overnight reliability claim.

Older speaker hardware, prolonged unattended operation, third-device Bluetooth
audio coexistence and speaker USB power retention in standby remain unverified.
Use an independent USB supply until retention is measured. See
[troubleshooting](docs/troubleshooting.md) for pairing, unknown state and link loss.

Run the checks from the repository root:

```sh
python3 tools/check.py
.venv/bin/python tools/check_config.py
.venv/bin/python tools/check_config.py --example full --compile
```

These run host tests, validate/generate both examples, then build the full example
and verify its linked hooks. Add `--flash 8MB` for that build variant. The
[protocol notes](docs/protocol.md#sdk-hooks-and-build-verification) explain why
compilation alone is insufficient. Archived firmware references are
listed in [reference/README.md](reference/README.md); they are not needed to use
the component. Project code is [MIT licensed](LICENSE).
