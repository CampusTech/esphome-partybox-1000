# Changelog

## Unreleased

- Optional RGB health indicator with connection status, local command-write pulses and failure flashes.
- RGB-enabled target compilation and runtime tests for optional light support and startup failures.

## 0.1.0-beta.1

First public beta. Tested with PartyBox 1000 software `75/32/17/15/7`, ESPHome 2026.9.1 and ESP-IDF 5.5.5.

- Native Home Assistant media player: 33-step volume, application mute/unmute and guarded standby/wake.
- Source selection, bass and lighting controls, selected-input signal activity and state-confidence diagnostics.
- Reusable ESPHome package with tested framework pins and radio settings; entity names, units and precision have built-in defaults.
- Low-level diagnostics disabled by default in HA, with YAML overrides and per-entity opt-outs.
- Portable protocol/runtime tests, 4/8 MB build validation and verification of all three linked Bluetooth stack hooks.
- Original firmware/service reference archive and concise protocol and pairing documentation.
- Private USB/OTA firmware export with native-hook verification and SHA-256 checksums. Speaker address and credentials remain specific to each build.

Physical TWS rejoin may be required after a reboot or full power-off. Power is estimated; mute readback comes from startup snapshots. Older speaker hardware, overnight reliability, third-device Bluetooth audio coexistence and USB-power retention across standby remain unverified.

Contributors: [Robbie Trencheny (@robbiet480)](https://github.com/robbiet480). This initial release was developed directly on the repository; there are no merged pull requests to enumerate.
