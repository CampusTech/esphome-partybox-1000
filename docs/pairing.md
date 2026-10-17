# Pairing the bridge

Use one original ESP32 per PartyBox. ESP32-S3/C3/C6 cannot provide the required Bluetooth Classic transport.

1. Configure the speaker's **Bluetooth Classic MAC address**, which may differ from its BLE address. Use an address reported by a Classic Bluetooth device inspector, not an unidentified BLE advertisement.
2. Flash your personal configuration over USB. Wait for the ESP32 to join Wi-Fi. If it cannot, join its `PartyBox 1000 Setup` network using your recovery password and open the captive portal to correct Wi-Fi settings.
3. Turn the intended JBL on and keep other speakers out of TWS search. Hold **only Bluetooth** on the JBL for about five seconds. A preliminary short press is unnecessary.
4. Wait for **TWS connected** and **SPP connected** in Home Assistant. A chime may accompany TWS admission; the link entities are a better check. Ordinary Bluetooth audio pairing alone does not establish TWS.
5. Play music on the selected input at a comfortable level. Check one volume change, mute/unmute and input selection. Test Standby/Wake while watching the power indicator.

After a full power-off, factory reset or ESP32 reboot, you may need to repeat the five-second Bluetooth hold. **Connect Bluetooth** attempts an ordinary A2DP connection; it cannot force the JBL into TWS search. Do not factory-reset a speaker as the first troubleshooting step.

A retained TWS session can request orange-light standby and wake. Dark, fully powered-off speakers are not remotely wakeable through this integration. Power state is an estimate, and command acknowledgements are not proof of a physical effect.

Low-level diagnostics start disabled in HA. Enable them on the ESPHome device page when collecting a report, or set `disabled_by_default: false` on the corresponding entity in YAML. Existing HA entity enable/disable choices can survive firmware updates.

See [troubleshooting](troubleshooting.md) for missing links, unknown state and recovery. Fresh-device provisioning and prolonged unattended operation still need hardware validation; the beta's passing builds do not replace those tests.
