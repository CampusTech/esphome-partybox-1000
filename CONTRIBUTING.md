# Contributing

Use Python 3.13 and the pinned ESPHome/ESP-IDF versions. Run the checks in the
[README](README.md) before submitting a pull request. Do not include `secrets.yaml`,
Wi-Fi credentials, Bluetooth pairing keys, personal captures or device builds
containing those values.

For protocol changes, include the exact message, the speaker's five-part software
version, selected input, and the observed effect. Distinguish a received status
report from an acknowledgement or an inferred state. Scrub device addresses and
network names from logs. A portable test should exercise the change; new SDK
support also needs a target build and verification of the three native hooks.

Hardware reports are welcome. Useful checks include older speaker revisions,
long connection runs, reconnecting after standby/full power-off, and USB power
retention. Report what happened rather than assuming a disconnect means the
speaker switched off.
