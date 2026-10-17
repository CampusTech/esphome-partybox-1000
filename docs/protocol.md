# Protocol and state model

This describes the implemented control surface and the wire formats needed to
understand or reproduce it. Hexadecimal bytes/words have no `0x` prefix below.
The component uses stock speaker firmware; it does not upload firmware to the JBL.

## Transports and session entry

Home Assistant talks to ESPHome over the encrypted native API. The ESP32 owns
the Classic Bluetooth controller and presents an A2DP sink plus AVRCP and SPP
profiles to one configured speaker. Received SBC buffers are counted and freed;
the ESP32 neither plays them nor sends keepalive audio to the speaker.

TWS admission needs more than an ordinary AVRCP connection. The implementation
uses the local name `PartyBox-ESPHome`, limited discoverability, Class of Device
fields `major=4`, `minor=0A`, `service=1A0` (base class `340428`), and Classic EIR
manufacturer bytes `48 4D 50 42 0D 10` (`HMPB` followed by source/flag bytes).
These are Classic inquiry data, not BLE advertisements. Limited discovery sets
the class bit `002000`, so controller readback is `342428`. The ESP-IDF EIR API
adds the AD type/length wrapper. Authentication accepts only the configured peer.

The speaker initiates TWS discovery. The first valid proprietary TWS word marks
a usable control session; an A2DP/AVRCP connection alone does not. SPP then
connects automatically to the configured peer on channel 1. The explicit Connect
action is a single A2DP attempt, not a complete TWS pairing procedure.

## AVRCP TWS envelope

Ordinary words use vendor-dependent AVRCP, panel subunit (`type=9`, `id=0`),
company ID `001958`, control command type `00` and this nine-byte vendor payload:

```text
19 00 00 05 00 W3 W2 W1 W0
```

`19` is the PDU ID; the 32-bit word is big-endian. Its kind is `word & 1F`.
Within the numerical word, `B0` is the least-significant byte, `B1` is bits 8–15,
`B2` bits 16–23 and `B3` bits 24–31. Thus `01020012` is lighting kind `12`,
pattern 2, secondary flag 1.

The normal response is AVRCP accepted (`09`) with payload `19 00 00 00` and the
original transaction label. Rejected/not-implemented responses (`0A`/`08`) are
not accepted. An ACK contains no state word and does not establish physical effect.

The exact numerical word `00000005` has a stock-encoder exception: company
`0000C4`, status command type `01`, payload `F5 00 00 01 00`. The response is
stable (`ctype=0C`, payload `F5 00 00 01 0F`). Its trailing `0F` is a literal,
not dynamic bass, power or role state. Ordinary source words ending in kind `05`
still use company `001958`/PDU `19`. The component answers the exception when
received; it does not treat it as a state query.

## Implemented controls and reports

| Field | Setter | Readback and limitations |
|---|---|---|
| Volume | TWS `B2=0..32`, kind `0A` | Snapshot or kind `0A`/`05`; native 0–1 volume rounds to one of 33 steps |
| Application mute | TWS `B2=0/1`, kind `0E` | Snapshot bit 8; no reliable live echo/on-demand query demonstrated |
| Source | SPP command `0F` below | TWS snapshot/kind `05`; SPP ACK alone gives assumed state |
| Bass | TWS `B2=0..2`, kind `0F` | Snapshot or kind `0F` |
| Lighting | TWS `B2=0..4`, `B3=0/1`, kind `12` | Snapshot or kind `12`; preserve last observed secondary flag |
| Standby/wake | TWS `B2=1/2`, kind `13` | Transition request/ACK, not a completed-power getter |
| Firmware | SPP command `08` below | Five returned version bytes; not automatic compatibility certification |
| Signal activity | Read only, kind `14` | Selected-input electrical detector; source/freshness guards apply |
| Smart DJ / voice cancel | Read only | Smart DJ snapshot bit 11 / kind `11`; voice cancel kind `10` |

Lighting writes are refused until the secondary flag is known. Its independent
meaning and the visual names of patterns 1–4 remain unresolved. Application mute
is distinct from the factory SPP DAC-mute command; this component uses TWS mute.

General TWS kind `05` is a peer-source report, not an established arbitrary local
input setter or callable getter. Kind `07` can select Bluetooth during role
negotiation but has other side effects; this component uses SPP for all input
selection. Play/pause/next/previous kinds are not implemented as player controls.

The wider decoded dispatcher includes clock/role messages (`01..03`, `06..09`),
track triggers (`0B..0D`) and the state kinds above. Role/session gates can accept
a word without applying it. Unrecognized kinds and flag bits must not be assigned
invented meanings. Kinds `00` and `15..1F` are unhandled in the inspected dispatcher.

## Connected-state snapshot

Kind `04` packs several fields into one word:

| Bits | Meaning |
|---|---|
| 0–4 | Kind `04` |
| 5 | Voice-cancellation flag in stock snapshot layout; not currently published from snapshots |
| 6 | Unnamed flag |
| 7 | Activity-related flag; full meaning unresolved |
| 8 | Application mute |
| 9–10 | Bass level 0–2 |
| 11 | Smart DJ |
| 12–14 | Lighting pattern 0–4 |
| 15 | Secondary lighting flag |
| 16–23 | Music volume 0–32 |
| 24–31 | Source |

For example, `090B12C4` and `090B13C4` differ only in application mute bit 8
at LINE IN, volume 11. Startup readback of that bit was physically correlated
with audible mute. A fresh complete connection can produce a snapshot, but a
control-only reconnect did not reliably do so. There is no demonstrated harmless
on-demand snapshot query. **Sending a kind `04` word applies supplied state;
it does not request current state.**

The tested TWS source values are Bluetooth `0D`, USB `0F`, AUX `06`, LINE IN `09`.
These differ from some firmware-internal source enums; do not substitute internal
enum values into TWS words. A valid snapshot initializes observed fields and an
awake estimate.
Each later report updates only its corresponding fields.

## Selected-input activity

Kind `14` carries an active-low silence mask in `B2`: bit 0 AUX, bit 1 LINE IN,
bit 2 USB, bit 3 Bluetooth. A cleared bit means signal present; a set bit means
silent. The component publishes AUX/LINE IN/USB activity only when the selected
source has fresh observed readback. Bluetooth activity remains unknown.

A source command invalidates activity immediately. An ACK can establish an
assumed source but cannot restore observed source attribution; activity remains
unknown until a fresh source report and activity mask arrive. Neither signal
presence nor an SBC byte counter proves audible output, playback metadata or
music recognition. A muted or zero-volume input may still carry electrical signal.

## SPP frames

Requests begin `BA`, replies `BB`; byte 1 is total frame length. The implemented
requests are:

```text
BA 07 0F 01 SS FF FF   Select source: SS=00 Bluetooth, 01 USB, 02 AUX, 03 LINE IN
BA 06 08 01 FF FF      Query firmware component versions
```

The parser admits only these reply shapes:

```text
BB 05 0A 9C 2D        Short command acknowledgement
BB 05 01 2D 46        Known short source-response shape; no selected-source value
BB 0A 08 VV VV VV VV VV FF FF   Five component-version bytes
```

Only the command acknowledgement resolves a pending source setter; the second
short shape is recognized without being treated as source readback. A version
reply resolves a pending version query and publishes the five decimal bytes
joined with `/`, for example `75/32/17/15/7`. Not all replies end `FF FF`.
An SPP write-completion callback is not a speaker acknowledgement.

## Confidence, power and invalidation

Every controlled field has a value, revision and quality:

- `observed`: a validated snapshot/report supplied the field.
- `assumed`: an accepted command supplied its expected value, with no newer
  report since the command was sent.
- `unknown`: no usable value, including after disconnect or protocol-event loss.

A newer report wins over an older command's ACK. Loss of TWS invalidates speaker
state and discards queued commands. Source selection also becomes unknown to
Home Assistant when usable SPP is lost; TWS source readback can still be used
internally for signal attribution. Firmware identification is cleared on SPP loss
and must be refreshed after reconnection.

Power kind `13` is a state-dependent key-synchronization request: `00010013`
requests standby and `00020013` requests wake. Incoming requests and successful
ACKs establish only an assumed estimate. Standby/wake are permitted only when
the current estimate is known and opposite to the target, with an idle command
queue. They are never automatically retried, replayed or sent at boot.

The tested TWS standby retains the control link and can be woken through it.
Physical dark/full-off disconnected the links and was not remotely woken.
Natural standby and prolonged sleep are separate validation cases. Connection
presence is not a power getter. The media player publishes `ON` for the awake
estimate, `OFF` for requested standby and neutral `IDLE` for unknown power/link
state; none establishes playing/paused audio. ESPHome's native API cannot clear
individual volume/mute fields or make one control unavailable while the node is
connected. Retained values need their confidence diagnostics.

## Transactions and recovery

There is one outstanding transaction across TWS/SPP, up to eight unsent commands,
at least 150 ms between sends and a three-second response timeout. Unsent volume
targets coalesce. TWS replies must match the selected peer, envelope, label and
native session. Timed-out labels are quarantined until TWS reconnect. An SPP
timeout poisons/closes that channel before further requests; setters are not
retried. SPP connection attempts back off to 60 seconds.

Callbacks use bounded queues. Protocol-event overflow invalidates state, fences
the failed native session, requests an actual AVRCP close and disconnects media.
Lifecycle mailboxes preserve connection changes if data events are dropped.
Optional logs have a separate lossy queue and cannot consume protocol capacity.
The dropped-events diagnostic counts protocol loss, not discarded optional logs.

## SDK hooks and build verification

ESP-IDF's public AVRCP path does not provide the required early proprietary
request handling; its stock vendor-command helper also hardcodes company `C4`.
Three linker wrappers implement the selected-peer receive/response path, dispatch
company `1958` commands on the SDK BTA task, and track native close/session changes:
`bta_av_rc_msg`, `bta_av_rc_vendor_cmd`, `bta_av_rc_closed`.

The wrappers include internal SDK types and assert their sizes/offsets. The code
requires ESP-IDF **5.5.5**, with ESPHome **2026.9.1** as the tested frontend.
Before any SDK upgrade, audit those layouts, protocol handling and actual linked
call sites. Host tests and successful compilation cannot prove linker wrapping
took effect. `python3 tools/verify_build.py PATH/TO/ESP-IDF/build` checks the linked
ELF, all three hook-table slots, linked SDK version and Classic Bluetooth SDK
settings. It locates the build's matching Xtensa tools automatically; `--nm` can
select an explicit matching executable. `tools/check_config.py --compile` runs
this verification after building, without uploading. Standard ESPHome BLE
ownership conflicts with this component; supported hardware is the original
Classic-capable ESP32 only.
