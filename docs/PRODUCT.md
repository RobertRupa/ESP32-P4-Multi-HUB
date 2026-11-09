# Product specification

## Mission

Build an **autonomous, local-first touchscreen multi-protocol hub** centered on ESP32-P4. MVP commissions Zigbee 3.0 devices through ESP32-H2, stores identity/state, presents selected capabilities via a standards-conformant Matter Bridge on the IP LAN and controls them from the screen. No external cloud or Linux server required for core operation.

## Supported system targets

Maintain only these two firmware compositions:

1. **P4 + C6:** P4 application and UI, C6 Wi-Fi plus the native Zigbee coordinator over ESP-Hosted Peer Data/SDIO. This profile is retained as a build target; end-to-end pairing is not currently claimed as validated.
2. **P4 + C6 + H2:** P4 Zigbee host with the external H2 standard UART Spinel RCP; C6 provides Wi-Fi only. The P4/H2 handshake and existing-network BDB readiness are verified. The C6 Wi-Fi-only image builds but is not flashed, so this composition is not fully deployed until that exact C6 image is selected and installed.

Do not maintain C6 multipan, P4 Thread/Zigbee multipan, or other radio-ownership product profiles. Thread remains a separate network and is not required by either target.

## In scope (MVP)

- Deterministic boot and recovery; setup for C6 Wi-Fi with safe credential storage.
- One Zigbee coordinator; permit-join time-limited pairing, interview, basic ZCL attribute reporting, device removal, offline detection.
- Capability model for on/off, level, temperature, relative humidity, contact, battery, with clearly documented support boundaries.
- Matter bridge discovery and commissioning, bridged device endpoints with stable identities, proper reachable status and bidirectional commands.
- Touch UI: dashboard, devices, pairing timer, detail/control, network/Matter setup, radio diagnostics, log/status.
- System event bus, persistent registry/identity/mapping, migrations, reboot resilience and factory reset flows.
- English default / Polish translations.
- Offline after provisioning: local Zigbee + UI continue without WAN.

## Planned extension points

- MQTT Home Assistant discovery; local REST and WebSocket event stream.
- Rules/automation; schedules; scenes; backup/restore without exposing raw credentials.
- Additional adapters: BLE gateway, RS485/Modbus, wired sensors, other radios; plugins are **compile-time modules**, not dynamically loaded binaries.
- Separate Thread Border Router hardware via a **second radio** if autonomous Thread networking is later required.
- Ethernet transport if supported by actual board add-on, rather than assuming onboard RJ45.

## Out of scope for MVP

- Turning Zigbee devices into native Thread nodes; universal compatibility with all Zigbee manufacturer clusters.
- Dual-stack concurrent Thread/Zigbee on the same ESP32-H2.
- Trustworthy full compatibility with smart-home ecosystems without conformance validation.
- Matter controller/commissioner for onboarding native Thread devices: independent major feature, not a prerequisite for the Matter Bridge.
- Cloud account requirement.

## User workflows

1. Provision network -> initialize Zigbee coordinator once -> display local IP, radio health and Matter commissioning.
2. Start permit-join with timeout -> pair Zigbee device -> interview -> classify into capabilities -> add Matter bridged endpoint if supported -> show on UI.
3. Matter controller sends on/off -> ACL and command validation -> Zigbee command -> confirmation/report or timeout -> update authoritative state and Matter attribute.
4. Zigbee report -> normalize units + quality/timestamp -> persist debounce -> publish Matter attribute/UI/MQTT.
5. Network unavailable -> core Zigbee and UI work; Matter reports disconnected until IP returns; no data corruption.
6. Device removal -> retire endpoint and stable mapping according to Matter adapter rules, clear bindings with explicit user confirmation.

## Quality targets to verify, not guaranteed

- No secrets in logs; watchdog and bounded task queues; UI remains responsive during radio outage.
- Reboot persistence and recoverable OTA; endurance controls on NVS writes.
- Separate compatibility matrix per device/model/manufacturer and Zigbee cluster, including known unsupported behaviors.
- At least one end-to-end bulb, one temperature sensor and one contact sensor demonstrated on actual hardware before MVP acceptance.
