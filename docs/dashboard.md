# Expandable speaker mixer

[The dashboard example](../examples/dashboard.yaml) uses
[Mixer Card](https://github.com/wrodie/mixer-card) and
[Auto Entities](https://github.com/thomasloven/lovelace-auto-entities), available
through HACS. Install both frontend cards before importing the dashboard.

The leftmost **All speakers** fader sets every member to the same volume. Its
readout follows the average of members with available volume values. Individual
faders retain independent volume and mute controls. Commands are sent when a
fader is released, rather than continuously while dragging.

The speaker button beneath **All speakers** mutes the whole group; pressing it
again unmutes every member. If only some speakers are muted, it first mutes all.
It leaves each speaker's volume level intact. Its indicator follows the mute
values reported by the component, including their existing confidence limits.

The **All speakers** selectors beneath the mixer apply bass boost or lighting to
every member. They show the shared setting when all members agree, or **Mixed**
when settings differ or a member's value is unknown. Choose a concrete setting
to synchronize the group; choosing Mixed sends no command. Individual speaker
selectors remain available below.

## Setup

1. In **Settings → Devices & services → Helpers → Create helper → Group →
   Media player group**, create **Office PartyBoxes**. Use the original ESPHome
   `Player` entities, ordered as you want their faders to appear. Leave members
   visible. The example expects `media_player.office_partyboxes`.
2. Add [the master helpers](../examples/dashboard-master-volume.yaml) to Home
   Assistant's `configuration.yaml`, merging its `template` list with any existing
   one. Reload Template entities after checking the configuration. Alternatively,
   create **Template → Number**, **Template → Switch** and two **Template → Select**
   helpers with the same settings. The expected entities are
   `number.office_partyboxes_volume`, `switch.office_partyboxes_audio` (switch on
   means audio enabled), `select.office_partyboxes_bass_boost` and
   `select.office_partyboxes_lighting`. The number
   helper is needed because the native
   media-player group forwards volume commands but does not expose volume
   readback.
3. Create an empty dashboard, open its **Raw configuration editor**, and paste
   [dashboard.yaml](../examples/dashboard.yaml). This file is a Lovelace
   dashboard, not an ESPHome configuration.

## Adding speakers

Add each new ESPHome `Player` to the **Office PartyBoxes** group helper. Its
fader and controls then appear automatically; the master helper uses the same
membership list. Reorder group members to reorder the dashboard. No dashboard
edit or Home Assistant restart is needed for membership changes.

The controls template expects the component's default entity naming pattern:
`media_player.<node>_player`, `select.<node>_source`,
`select.<node>_bass_boost`, `select.<node>_lighting`,
`button.<node>_wake`, `button.<node>_standby`, and the corresponding
`binary_sensor.<node>_tws_connected`, `_spp_connected`, and
`_selected_input_signal` entities. If you renamed entity IDs independently,
adjust the template's suffixes or add an explicit mapping. Display names can be
changed freely.

On smaller screens, swipe the mixer horizontally to reach additional faders.
Speaker controls use one, two or three columns depending on screen width.
**Input signal** reports selected-input electrical activity, not audible music;
unknown values and the component's existing state-confidence limits still apply.
