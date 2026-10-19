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

For source selection and bass sound modes inside a single HA media player, use
the optional [Home Assistant wrapper](docs/home-assistant.md).

## Pairing without a MAC address

The current checkout adds a **Pair speaker** button in HA. Put the intended JBL
into TWS search, then press it: a single discovery window finds compatible
PartyBox 1000 candidates and saves the only match. Multiple matches require a
choice. The saved target can also be edited in HA without reflashing.

Use [examples/pairing.yaml](examples/pairing.yaml) and the
[pairing guide](docs/pairing.md). This feature is not yet in the published beta
tag and still needs live discovery validation. Normal operation keeps active
scanning disabled. Changing targets restarts the bridge and requires TWS pairing.

## Optional RGB status LED

The supplied SDATEKIT ESP32-WROOM-32E schematic connects a WS2812B to
**GPIO16**. Add the
[LED package](packages/status-led-ws2812.yaml) alongside the base package;
[examples/rgb.yaml](examples/rgb.yaml) shows the local-checkout configuration.
This package currently requires the repository checkout; the tagged beta predates
RGB support. Boards without this LED need no changes. Other boards can override
`partybox_status_led_pin`.

| LED | Meaning |
|---|---|
| Dim green | Wi-Fi, an API state subscriber, TWS and enabled SPP are connected; standby is healthy too |
| Pulsing amber | Wi-Fi or API state subscriber missing |
| Pulsing blue | Required speaker transport missing |
| Brief cyan | A command was written to the Bluetooth transport |
| Three red flashes | A command was refused, rejected, failed or timed out |
| Steady red | PartyBox component failed |

The schematic also places a **0-ohm RGB link** in the data line. Our tested
SDATEKIT board shipped with that position empty, leaving the LED disconnected.
Check that link before troubleshooting GPIO settings; driver health cannot detect
an open connection or a different LED part.

Brightness defaults to 8%; override `partybox.status_brightness` if desired.
A transport write does not prove the speaker applied a command. The LED is
internal and does not add a Home Assistant light entity or Bluetooth traffic.

## Install

Use an original ESP32 with USB power, one per speaker. Install ESPHome **2026.9.1**;
the package supplies the tested framework and radio settings. Create a device
configuration in ESPHome with your own node name:

```yaml
substitutions:
  name: office-partybox
  friendly_name: Office PartyBox

packages:
  partybox: github://CampusTech/esphome-partybox-1000/packages/partybox-1000.yaml@v0.1.0-beta.1

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    password: !secret setup_password

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    encryption:
      key: !secret api_key

partybox:
  mac_address: !secret speaker_mac
```

Add these values to your private `secrets.yaml`:

```yaml
wifi_ssid: "Your Wi-Fi"
wifi_password: "Your Wi-Fi password"
setup_password: "A private recovery AP password"
api_key: "BASE64_32_BYTE_KEY"
speaker_mac: "AA:BB:CC:DD:EE:FF"
```

Use the speaker's **Classic Bluetooth** address, which may differ from its BLE
address. Generate the API/OTA key with
`python3 -c 'import secrets, base64; print(base64.b64encode(secrets.token_bytes(32)).decode())'`.
The recovery password must contain 8–63 characters. Do not commit secrets.

The package defaults to **4 MB** flash. For a board fitted with 8 MB, add
`partybox_flash_size: 8MB` under `substitutions`. Extra flash adds no RAM.
Use the ESPHome dashboard to install over USB, then add the discovered node to
Home Assistant with its API key. Later updates support encrypted OTA. The
recovery AP and captive portal allow Wi-Fi recovery; BLE provisioning/proxy cannot
share this Bluetooth controller.

Turn the JBL on and hold **only Bluetooth** for about five seconds to start TWS
search. Wait for `TWS connected` and `SPP connected`. Follow the short
[pairing guide](docs/pairing.md) for the first control test and reconnection.
A physical rejoin may be required after either device reboots or the speaker
fully powers off. **Connect Bluetooth** makes an ordinary A2DP attempt, not a
guaranteed TWS rejoin. Standby/wake require a known power estimate and a retained
TWS link; they are never sent on boot or retried automatically. A dark, fully
powered-off speaker was not woken by this path.

All entities have built-in names, units and precision. Add a section only to
override settings, or use `false` to omit an entity:

```yaml
partybox:
  mac_address: !secret speaker_mac
  media_player:
    name: Player override
  free_heap:
    disabled_by_default: false
  last_word: false
```

Everyday controls, link/activity status and the firmware version are visible by
default. Low-level counters, confidence fields, raw words and service diagnostics
start disabled in HA; enable them on the device page when needed. HA can retain
existing entity enable/disable choices across updates. Empty entity sections
keep defaults, and existing explicit names still work.

### Build personal firmware files

Firmware contains your speaker address and private credentials. To export verified
USB and OTA files, clone this release, install [requirements.txt](requirements.txt),
and configure [the local example](examples/partybox-1000.yaml) with your private
[secrets](examples/secrets.yaml.example):

```sh
git clone --branch v0.1.0-beta.1 https://github.com/CampusTech/esphome-partybox-1000.git
cd esphome-partybox-1000
python3.13 -m venv .venv
.venv/bin/pip install -r requirements.txt
# Create examples/secrets.yaml with your private values before building.
.venv/bin/python tools/build_firmware.py examples/partybox-1000.yaml --output dist/office-partybox
```

The export contains `firmware.factory.bin` (merged USB image, offset `0x0`),
`firmware.ota.bin` (OTA image), build verification metadata and `SHA256SUMS`.
Keep that directory private. It is ignored by Git. Both images must agree, and
all three linked Bluetooth hooks must pass verification before anything is
exported. Install directly with
`.venv/bin/esphome run examples/partybox-1000.yaml` if separate files are unnecessary.

[The minimal example](examples/minimal.yaml) keeps only the player and link status.
The local examples import the same package with a local component source; the
GitHub import above pins both the package and component to this release. See
[the changelog](CHANGELOG.md) for release notes and contributors.

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
