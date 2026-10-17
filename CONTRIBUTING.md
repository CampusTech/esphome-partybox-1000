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

## Releasing

Update the project version and component-source tag together in
`packages/partybox-1000.yaml`, the versioned README examples, and `CHANGELOG.md`.
Include merged PRs and contributor attribution in the changelog. Run
`python tools/release_notes.py vVERSION` and the normal checks before pushing the
annotated version tag. The tag workflow repeats the host/build matrix and
publishes only after it passes, appending GitHub's generated merged-PR and
contributor notes. Alpha/beta/RC tags are marked prereleases.

Personal firmware exports contain deployment credentials. Do not upload them to
GitHub Releases, issues or Actions artifacts. The public release provides the
versioned source/package and the private firmware build/export command.
