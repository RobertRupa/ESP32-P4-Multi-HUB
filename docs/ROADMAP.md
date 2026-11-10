# Roadmap with acceptance gates

Only two product targets are maintained: **P4+C6** (C6 Wi-Fi plus native Zigbee) and **P4+C6+H2** (C6 Wi-Fi, H2 Zigbee RCP, P4 Zigbee host). The active P4 image is the H2 profile. Spinel reset and BDB readiness on a persisted, joined network are verified; interview succeeds for `0x1a02` and still times out for `0x8362`. The C6 Wi-Fi-only image required by the H2 target builds but has not been flashed. No new network or device join is claimed. HTTP-only Web UI remains limited to a trusted LAN. Thread, Matter Bridge and Home Assistant discovery are not implemented or validated. See the [HMI/Wi-Fi audit](audits/2026-11-07-p4-hmi-wifi.md), [C6 OTA audit](audits/2026-11-07-c6-host-ota.md) and [H2 migration audit](audits/2026-11-08-h2-zigbee-migration.md).

| Stage | Deliverable | Pass/fail acceptance |
|---|---|---|
| M0 | Reproducible toolchain / board-reference match and silicon revision | Record PCB markings and P4 revision; reconcile schematic revision; produce evidence-backed connector/net table; pin ESP-IDF and reference source revisions; no build warnings hidden |
| M1 | P4 BSP + LCD ST7701S / GT911 + C6 Wi-Fi | 10-minute touch/UI/Wi-Fi soak, clean cold boot and reconnection |
| M2 | H2 RCP standalone and UART transport | Spinel version/reset handshake and BDB readiness pass; unplug recovery and reset-line validation remain open |
| M3 | Zigbee coordinator | Existing joined network, `0x1a02` interview and CurrentLevel readback pass; both Level commands get ZCL success but reported level stays 3; new-device join, dimmer actuation and reboot persistence remain open |
| M4 | Canonical registry + device capability adapters | host-side unit tests, stable identities, online/offline state and persistence migrations |
| M5 | Matter bridge | actual P4 compile, Matter commissioning, dynamic endpoints, on/off+sensor reports in a real controller |
| M6 | Thread interoperability (external TBR) | same bridge accessible from Matter controller with configured Thread network/TBR and IPv6 LAN |
| M7 | Production UX + recovery + security | English/Polish UI, OTA rollback, reset scopes, safe credential storage, RF coexistence test |
| M8 | Extensions | MQTT, local API, rule engine, Modbus/BLE via feature flags |

**H2 migration progress (2026-11-08):** P4/H2 Spinel reset and BDB device-start status succeed at UART1 GPIO26/27 and 230400 baud. The existing network reports joined; short address `0x1a02` interviews successfully. User-approved Level target `93` via both command variants returns Default Response success but leaves reported CurrentLevel at `3`, even with On/Off true. Level actuation is not working on this dimmer; `0x8362` interview still times out.

## Critical go/no-go gates

1. **ESP32-P4 Zigbee host / esp-matter bridge compatibility:** inspect component target support and compile a minimal bridged endpoint sample before complex UI work.
2. **Board revision / pin availability:** investigate dedicated UART connectors and safe reset/boot controls using the vendor schematic and physical continuity checks. UART0 is the console/programming path; UART1 is shared with RS485. JP1 is not planned for H2. Do not assign pins until shared nets and the actual PCB revision are verified; preserve display, SDIO, I2S, SD and RS485.
3. **H2 coexistence:** verify practical Wi-Fi + Zigbee range and packet error under peak display/network load.
4. **Resource budget:** benchmark heap fragmentation, DMA memory, display buffers, Matter dynamic endpoints, and stress reconnect.
5. **SDMMC coexistence:** ESP-Hosted already owns SDMMC; verify slot mapping and init/deinit ownership before initializing the onboard microSD. ESP-IDF host/slot initialization is not thread-safe, so do not mount or probe the card concurrently until ownership is established.

## Current sequencing constraint

The two supported profiles must never run two authoritative Zigbee coordinators. In P4+C6+H2, pair the H2 host image with the C6 Wi-Fi-only image; preserve Zigbee NVS and do not implicitly migrate/reset network credentials. C6 firmware remains untouched until its exact Wi-Fi-only image and port are explicitly selected.

## Suggested implementation PR sequence

- PR01 docs (this architecture foundation)
- PR02 toolchain manifest, CMake, P4/C6 BSP and hardware probes
- PR04 canonical model + mock adapter + tests
- PR07 LVGL interface/commissioning/diagnostics
- PR08 robust persistence, OTA, security, integration tests
- PR03 H2 RCP build instructions and UART/boot wiring (confirmed actual pins; final hardware stage)
- PR05 Zigbee coordinator adapter, real network smoke test
- PR06 Matter bridge adapter with 1 stable bridged endpoint

## Test matrix

Board cold/warm boot; full power loss; H2 disconnected/reconnected; C6 Wi-Fi dropout; bridge commissioned/uncommissioned; Zigbee join timeout; rogue Zigbee frames; repeated Zigbee join/remove; NVS writes interrupted; microSD missing; heap stress; Zigbee/Wi-Fi channel collisions; Thread TBR absent/present.
