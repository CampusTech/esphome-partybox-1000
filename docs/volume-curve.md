# Firmware volume curve

**V63, V64 and V75 request identical master-gain coefficients for levels 0–32.**
These are relative electrical gains, not room loudness, amplifier wattage or
sound-pressure levels. Equal gains across different speakers do not establish
equal loudness.

The HA player currently exposes `volume_level = level / 32`. Its percentage is
an index into the following curve, not a linear amplitude percentage. For
example, 50% selects level 16, approximately −17 dB relative to level 32.

| Level /32 | HA % | Nominal gain, dB relative to maximum |
|---:|---:|---:|
| 0 | 0 | Zero gain (−∞) |
| 1 | 3.125 | −65 |
| 2 | 6.25 | −60 |
| 3 | 9.375 | −55 |
| 4 | 12.5 | −50 |
| 5 | 15.625 | −46 |
| 6 | 18.75 | −42 |
| 7 | 21.875 | −38 |
| 8 | 25 | −35 |
| 9 | 28.125 | −32 |
| 10 | 31.25 | −29 |
| 11 | 34.375 | −27 |
| 12 | 37.5 | −25 |
| 13 | 40.625 | −23 |
| 14 | 43.75 | −21 |
| 15 | 46.875 | −19 |
| 16 | 50 | −17 |
| 17 | 53.125 | −15 |
| 18 | 56.25 | −14 |
| 19 | 59.375 | −13 |
| 20 | 62.5 | −12 |
| 21 | 65.625 | −11 |
| 22 | 68.75 | −10 |
| 23 | 71.875 | −9 |
| 24 | 75 | −8 |
| 25 | 78.125 | −7 |
| 26 | 81.25 | −6 |
| 27 | 84.375 | −5 |
| 28 | 87.5 | −4 |
| 29 | 90.625 | −3 |
| 30 | 93.75 | −2 |
| 31 | 96.875 | −1 |
| 32 | 100 | 0 |

The nominal figures come from the table's attenuation indices. The
[machine-readable results](data/volume-curve.json) also contain the precise
gain calculated as `20 × log10(coefficient / coefficient_at_level_32)`.
Coefficient rounding differs from the nominal values by less than 0.003 dB.
Level zero writes coefficient zero; its gain is represented by `null` in JSON,
since negative infinity is not a JSON number. Zero volume is independent of the
speaker's mute flag and power state.

## Verification

The [offline mapper](../tools/map_volume_curve.py) checks each image against the
repository's reference manifest, locates `AK7738_SetMasterVolume`, resolves its
literal tables and transfer calls, and executes the original ARM setter in
Unicorn. All 396 cases passed: three releases × 33 levels × four modeled input
selections (USB, LINE IN, AUX and Bluetooth).

For every case, the captured requests were `80 00 4A <three coefficient bytes>`
followed by `A5 00 00`. The results independently cross-check those bytes against
the original index and coefficient tables, and verify the stored level and
normal function return.

| Release | Setter file offset | Setter runtime address |
|---|---|---|
| V63 | `0x6CED0` | `0x1F3AD0` |
| V64 | `0x6CF18` | `0x1F3B18` |
| V75 | `0x6CF24` | `0x1F3B24` |

The gain-index table is at runtime `0x34F6B4`; the coefficient table is at
`0x34F4B8` in each examined image. Maximum coefficient is `0x200000`. Treating
these as linear amplitude coefficients gives the relative dB values above;
this interpretation has not been verified by electrical measurement.

The emulator supplies ready/source RAM, models successful hardware ACKs, stubs
the transfer/logger/clock calls and disables UI notification. It captures
requested DSP writes; it does not execute the DSP, Bluetooth transport, real
scheduler or analog circuitry. Source-independent results establish this master
stage's mapping, not identical end-to-end gain for every input.

Reproduce from the repository root in a separate analysis environment:

```sh
python3 -m venv .local/volume-analysis
.local/volume-analysis/bin/pip install capstone==5.0.7 unicorn==2.1.4
.local/volume-analysis/bin/python tools/map_volume_curve.py --output /tmp/partybox-volume-curve.json
```

No speaker connection is needed. The tool neither changes firmware images nor
sends commands to hardware.

## Using the curve with other zones

This curve lets a controller apply comparable **gain changes** to a PartyBox
and a DSP zone expressed in dB. For example, reducing a PartyBox from level 16
to 13 requests approximately −6 dB; a Tesira zone can receive the same −6 dB
change relative to its calibrated setting. It does not mean setting both
devices to −17 dB produces the same sound level.

A combined controller still needs a reference gain and maximum level for each
zone. Apply a common master attenuation plus each zone's calibration/trim, then
select the nearest PartyBox step or set the DSP's dB value. Its attainable
precision varies: PartyBox steps are 5 dB near the bottom and 1 dB near the top.
The source player's gain is another upstream stage and must be included once
in that design.

Input sensitivity/trims, bass/EQ, active limiters, amplifier gain, room coverage
and speaker sensitivity remain outside this table. Prompt playback may
temporarily override music gain. This investigation makes no changes to HA's
existing player percentages, mixer behavior, volume limits or mute states.
