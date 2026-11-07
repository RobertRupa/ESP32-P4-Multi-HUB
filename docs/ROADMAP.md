# Roadmap with acceptance gates

Everything listed below is PLANNED, not implemented.

| Stage | Deliverable | Pass/fail acceptance |
|---|---|---|
| M0 | Reproducible toolchain / verified board schematic and silicon revision | Pin table with evidence; pinned ESP-IDF and reference source revisions; no build warnings hidden |
| M1 | P4 BSP + LCD ST7701S / GT911 + C6 Wi-Fi | 10-minute touch/UI/Wi-Fi soak, clean cold boot and reconnection |
| M2 | H2 RCP standalone and UART transport | firmware version handshake, reset/boot control, serial integrity and recovery after unplug |
| M3 | Zigbee coordinator | P4 SDK builds with Zigbee host for P4; pair/unpair a real bulb; persistence after power cycle |
| M4 | Canonical registry + device capability adapters | host-side unit tests, stable identities, online/offline state and persistence migrations |
| M5 | Matter bridge | actual P4 compile, Matter commissioning, dynamic endpoints, on/off+sensor reports in a real controller |
| M6 | Thread interoperability (external TBR) | same bridge accessible from Matter controller with configured Thread network/TBR and IPv6 LAN |
| M7 | Production UX + recovery + security | English/Polish UI, OTA rollback, reset scopes, safe credential storage, RF coexistence test |
| M8 | Extensions | MQTT, local API, rule engine, Modbus/BLE via feature flags |

## Critical go/no-go gates

1. **ESP32-P4 Zigbee host / esp-matter bridge compatibility:** inspect component target support and compile a minimal bridged endpoint sample before complex UI work.
2. **Board revision / pin availability:** find safe P4 UART TX/RX and 2 GPIO reset/boot on exposed connector; preserve display, SDIO, I2S, SD, RS485.
3. **H2 coexistence:** verify practical Wi-Fi + Zigbee range and packet error under peak display/network load.
4. **Resource budget:** benchmark heap fragmentation, DMA memory, display buffers, Matter dynamic endpoints, and stress reconnect.

## Suggested implementation PR sequence

- PR01 docs (this architecture foundation)
- PR02 toolchain manifest, CMake, BSP and hardware probes
- PR03 H2 RCP build instructions and UART/boot wiring (confirmed actual pins)
- PR04 canonical model + mock adapter + tests
- PR05 Zigbee coordinator adapter, real network smoke test
- PR06 Matter bridge adapter with 1 stable bridged endpoint
- PR07 LVGL interface/commissioning/diagnostics
- PR08 robust persistence, OTA, security, integration tests

## Test matrix

Board cold/warm boot; full power loss; H2 disconnected/reconnected; C6 Wi-Fi dropout; bridge commissioned/uncommissioned; Zigbee join timeout; rogue Zigbee frames; repeated Zigbee join/remove; NVS writes interrupted; microSD missing; heap stress; Zigbee/Wi-Fi channel collisions; Thread TBR absent/present.
