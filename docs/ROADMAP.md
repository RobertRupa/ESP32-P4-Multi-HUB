# Roadmap with acceptance gates

Roadmap stages below remain planned or incomplete until their acceptance gates pass. ESP-Hosted P4 host and onboard C6 report matching 3.1.0 over SDIO RPC v2; the P4 HTTPS Web UI, Wi-Fi and session-authenticated controls are installed. C6 status-v3 OTA is reboot-verified. The current recovered P4/C6 runtime reports BDB `0x0100/SUCCESS` at initialization but no network is formed and no device has joined. H2 RCP and a board-specific P4 H2 profile were flashed; Spinel timed out at 460800 and 230400 baud, and P4 was restored to the C6-backed image. H2 RCP app startup is observed, but no host/RCP handshake or H2 commissioning is verified. C6 Wi-Fi-only remains build-only and C6 firmware/NVS are unchanged. M0 remains incomplete pending board-revision/schematic reconciliation; M1 remains incomplete pending its 10-minute soak and full cold-boot acceptance. Thread, Matter and HA remain unverified. See the [P4 flash audit](audits/2026-11-07-p4-esp-hosted-flash.md), [HMI/Wi-Fi audit](audits/2026-11-07-p4-hmi-wifi.md), [C6 multipan feasibility audit](audits/2026-11-07-c6-thread-zigbee-feasibility.md), [C6 OTA audit](audits/2026-11-07-c6-host-ota.md) and [H2 migration audit](audits/2026-11-08-h2-zigbee-migration.md).
The installed product path still uses the P4 C6 Peer Data bridge, read-only Zigbee status, a two-step touchscreen network-creation/search flow and confirmation-gated commands. The H2 migration has compile-time P4/C6/H2 profiles and host policy tests, but does not yet replace the installed coordinator. RF coexistence, Thread dataset provisioning and Matter Bridge/HA discovery are not validated or implemented.

| Stage | Deliverable | Pass/fail acceptance |
|---|---|---|
| M0 | Reproducible toolchain / board-reference match and silicon revision | Record PCB markings and P4 revision; reconcile schematic revision; produce evidence-backed connector/net table; pin ESP-IDF and reference source revisions; no build warnings hidden |
| M1 | P4 BSP + LCD ST7701S / GT911 + C6 Wi-Fi | 10-minute touch/UI/Wi-Fi soak, clean cold boot and reconnection |
| M2 | H2 RCP standalone and UART transport | firmware version handshake, reset/boot control, serial integrity and recovery after unplug |
| M3 | Zigbee coordinator | P4 SDK builds with Zigbee host for P4; pair/unpair a real bulb; persistence after power cycle |
| M4 | Canonical registry + device capability adapters | host-side unit tests, stable identities, online/offline state and persistence migrations |
| M5 | Matter bridge | actual P4 compile, Matter commissioning, dynamic endpoints, on/off+sensor reports in a real controller |
| M6 | Thread interoperability (external TBR) | same bridge accessible from Matter controller with configured Thread network/TBR and IPv6 LAN |
| M7 | Production UX + recovery + security | English/Polish UI, OTA rollback, reset scopes, safe credential storage, RF coexistence test |
| M8 | Extensions | MQTT, local API, rule engine, Modbus/BLE via feature flags |

**H2 migration progress (2026-11-08):** the ESP-Zigbee `esp32p4` remote-radio library and H2 RCP build with IDF 5.5.5. The board-specific P4/H2 profile was tested at UART1 GPIO26/27 and 230400 baud; P4 received no Spinel response and was restored to the C6-backed image. These checks do not complete M2 or M3. No H2 coordinator formation, device join or persistence test has passed.

## Critical go/no-go gates

1. **ESP32-P4 Zigbee host / esp-matter bridge compatibility:** inspect component target support and compile a minimal bridged endpoint sample before complex UI work.
2. **Board revision / pin availability:** investigate dedicated UART connectors and safe reset/boot controls using the vendor schematic and physical continuity checks. UART0 is the console/programming path; UART1 is shared with RS485. JP1 is not planned for H2. Do not assign pins until shared nets and the actual PCB revision are verified; preserve display, SDIO, I2S, SD and RS485.
3. **H2 coexistence:** verify practical Wi-Fi + Zigbee range and packet error under peak display/network load.
4. **Resource budget:** benchmark heap fragmentation, DMA memory, display buffers, Matter dynamic endpoints, and stress reconnect.
5. **SDMMC coexistence:** ESP-Hosted already owns SDMMC; verify slot mapping and init/deinit ownership before initializing the onboard microSD. ESP-IDF host/slot initialization is not thread-safe, so do not mount or probe the card concurrently until ownership is established.
6. **C6 multipan candidate:** newer IDF build-only host/CP images compile, but product adoption requires a pinned toolchain/dependencies, verified P4 UART nets (do not reuse GPIO5 LCD reset), C6 OTA-slot capacity, Wi-Fi/802.15.4 coexistence measurements and a Matter Bridge interoperability path.

## Current sequencing constraint

Keep the installed C6-backed runtime as the rollback path while the H2 profile is developed. Do not make H2 the authoritative coordinator until the matching board schematic/net record, UART configuration, RCP handshake, approved network formation and a real end-device join are verified. Preserve C6 Zigbee NVS; do not run two authoritative coordinators or implicitly migrate/reset network credentials.

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
