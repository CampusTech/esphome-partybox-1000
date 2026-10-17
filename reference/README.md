# Original firmware and service references

These original files are available directly from this repository without a paid download service. They are optional research references: using the ESPHome integration requires no speaker firmware modifications or JBL firmware download.

JBL/Harman firmware and service documents retain their original rights; the project's MIT license covers the authored code and documentation. The archive contains original files, without custom speaker firmware or generated patches.

## Downloads

Links open the original file bytes. [manifest.json](manifest.json) records each file's size, SHA-256, model, format and provenance. Each payload is below 100 MB; the archive contains no duplicate ZIP copies.

| Model | File | Bytes | Description |
|---|---|---:|---|
| PartyBox 1000 | [V63 FULL](partybox-1000/PB1000_FW_V63_FULL.bin?raw=1) | 33,554,432 | Original official update package; main application plus component/data slots |
| PartyBox 1000 | [V64 FULL](partybox-1000/PB1000_FW_V64_FULL.bin?raw=1) | 33,554,432 | Original supplied/community full update package |
| PartyBox 1000 | [V75 MAIN](partybox-1000/PB1000_CR_202101070V75.bin?raw=1) | 2,385,920 | Main application only; **not** a full component or MCU package |
| PartyBox 100 | [Working-unit comparison dump](partybox-100/jbl_partybox100_good.bin?raw=1) | 8,388,608 | Different model; version unknown, uploader's working-unit claim unverified |
| PartyBox 1000 | [V63 update instructions](manuals/partybox-1000-v63-update-instructions.pdf?raw=1) | 88,289 | Original PDF from the official V63 ZIP |
| PartyBox 1000 | [V64 update instructions](manuals/partybox-1000-v64-update-instructions.pdf?raw=1) | 128,163 | October 2019 release/update PDF from the community V64 ZIP |
| PartyBox 1000 | [Service manual v1.1](manuals/partybox-1000-service-v1.1.pdf?raw=1) | 15,711,524 | February 2020 manual with original-generation parts, disassembly and schematics |
| PartyBox 1000 | [Service manual v1.2](manuals/partybox-1000-service-v1.2.pdf?raw=1) | 10,789,900 | December 2021 manual, schematics and hardware-revision bulletin |
| PartyBox 1000 | [Update and commands bulletin](manuals/partybox-1000-update-and-commands-bulletin.pdf?raw=1) | 252,305 | Portuguese manufacturer bulletin, revision 01 |
| PartyBox 1000 | [G1/G2 parts bulletin](manuals/partybox-1000-g1-g2-parts-bulletin.pdf?raw=1) | 152,980 | Portuguese manufacturer bulletin on board/firmware generations |

## Provenance and compatibility

V63 was extracted from [JBL's firmware ZIP](https://support.jbl.com/on/demandware.static/-/Sites-masterCatalog_Harman/default/dw105186d8/pdfs/Firmware_Update_PartyBox1000_V63_September2019.zip). Its binary and update PDF match that local source archive byte-for-byte. The complete [v1.2 service manual](https://harman-cdn.servify.tech/vendor-partner-docs/Partner-Brand_Collateral_19_2022-07-02.JBL%C2%A0PartyBox%C2%A01000%C2%A0-%C2%A0Service%C2%A0Manual%C2%A0v1.2.pdf) came from a manufacturer-hosted document URL.

V64 also matches the copy inside the supplied `jbl.zip` and its nested `Software+upgrade+V64.zip`. That archive is associated with a [community discussion](https://www.reddit.com/r/JBL/comments/17hz0cd/jbl_partybox_1000_broken/k7ec732/); it was not independently downloaded again to establish identity with the discussion's current download. The v1.1 service manual, V75, the two Portuguese bulletins, and the PartyBox 100 archive were user-supplied. No verified acquisition URL is available for those particular bytes. Local archive hashes and member names are retained in the manifest; hashes establish identity, not publisher authenticity.

The v1.1 cover identifies February 2020 and its revision list dates the update to February 10, 2020: corrected position numbers in the exploded view and spare-parts list. It has 73 pages. The 74-page v1.2 revision list dates its update to December 31, 2021: an added service-information page and updated spare-parts list. Keep v1.1 for the earlier parts reference; use v1.2 for the later board-generation guidance. The original v1.2 PDF retains a stale embedded document title saying "Service Manual v1.1"; its printed cover and revision list identify v1.2. The archived bytes are unchanged.

The service documents distinguish original G1 hardware/main firmware through V64 from revised G2 hardware/main firmware V75 and later. Main and ADC boards must be a matched generation. These files do not certify old/new hardware compatibility, and the integration does not update the speaker.

The examined V64 bundled control-MCU code reports revision 31. An attended speaker reported main 75 / MCU 32 / other fields 17, 15, 07. The short V75 file contains no MCU-32 update, BLE image or sound banks; a matching main version does not prove equality with the installed firmware. The PartyBox 100 dump is solely a comparison target and is not a PartyBox 1000 update. No cross-model, cross-generation or raw-dump flashing procedure is provided here.

## Findings useful to controller development

- Classic Bluetooth TWS discovery uses `HMPB` manufacturer data in EIR, peer class filtering and native master/slave role gates. Advertising matching bytes alone does not establish a working TWS session. The examined stock master actively searches using LIAC `9E8B00`.
- Native TWS snapshots carry application state, including application mute. Factory SPP DAC-output mute is separate. No harmless stock operation was found that reliably requests a fresh snapshot or reads that factory mute state on demand; command acknowledgments are not state readback. Sending snapshot kind `04` applies state to a receiver rather than querying it.
- Ordinary prompt candidates are effect-bank assets 6–11. Offline analysis identified a possible data-only quieter/silent prompt-bank update while preserving asset layout. It remains unflashed and acoustically unverified; V75 MAIN provides no installed G2 sound-bank copy. Large extracted audio assets and generated patches are intentionally omitted.
- The PartyBox 100 comparison contains two internally consistent MLC3730-format images. Two complete segments totaling 430,592 bytes match the PartyBox 1000 V75 image, supporting shared-platform research without establishing compatible commands, DSP routing or flashing.

These findings summarize bounded file analysis and prior attended observations. They do not turn modeled updater behavior into a verified recovery or installation method.
