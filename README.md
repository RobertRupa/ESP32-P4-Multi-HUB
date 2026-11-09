# ESP32-P4 Multi-HUB

**Status (2026-11-08):** The active P4 image uses the H2 UART-RCP coordinator profile (UART1 GPIO26/27, 230400 baud). Spinel reset succeeds, BDB reports the existing Zigbee network joined, and `0x1a02` interviews with On/Off and Level Control clusters. Read-only `CurrentLevel` returns `3`. User-approved Level targets of `93` using both ZCL command variants returned Default Response success, including with On/Off confirmed true, but `CurrentLevel` remained `3`; the dimmer is not applying the requested level. Saved device `0x8362` still times out during ZDO interview. No new network or device join was performed. C6 and H2 firmware/NVS were not modified. Thread and Matter Bridge remain unimplemented/unvalidated.

A modular, local-first embedded home automation gateway built around **Guition JC4880P443C_I_W** (ESP32-P4 + ESP32-C6), 4.3-inch touch display and an **external ESP32-H2 acting as a Zigbee 802.15.4 RCP**.

## Product goal

Pair and manage Zigbee devices locally, represent supported capabilities as **Matter bridged endpoints** on the IP network, and allow Matter controllers reachable via Wi-Fi/Ethernet/**Thread Border Router** to operate them. Add a touchscreen UI, local API, optional MQTT, automation and diagnostics without requiring cloud services.

**Important protocol distinction:** Zigbee devices are **not** turned into native Thread nodes. Thread is separate from the two supported Zigbee compositions below. No Matter Bridge or Home Assistant discovery has been validated.

## Supported system targets

| Target | Coordinator | C6 role | Status |
|---|---|---|---|
| P4 + C6 | C6 native Zigbee over ESP-Hosted Peer Data/SDIO | Wi-Fi and Zigbee | Build profile retained; end-to-end commissioning is not currently accepted as validated. |
| P4 + C6 + H2 | P4 Zigbee host over H2 UART Spinel | Wi-Fi only | P4/H2 handshake and existing-network readiness verified; one device interview works, one times out. The C6 Wi-Fi-only image builds but has not been installed. |

No C6 multipan or Thread/Zigbee multipan product target is maintained.

## Hardware roles

- **ESP32-P4** — application processor, display/UI, registry and local services; Zigbee host in the H2 target.
- **Onboard ESP32-C6** — Wi-Fi over ESP-Hosted SDIO. It also owns native Zigbee only in the P4+C6 target. For P4+C6+H2, use the separate Wi-Fi-only C6 profile; it is build-validated but not flashed.
- **External ESP32-H2** — Zigbee RCP for the P4+C6+H2 target. Standard UART Spinel reset and existing-network readiness are hardware-verified.
- **Network** — Wi-Fi via C6, with optional Ethernet in future; Matter over IP, not an 802.15.4 Thread interface in MVP.

## Docs index

1. [Product requirements](docs/PRODUCT.md)
2. [Hardware knowledge and pin reservation](docs/HARDWARE.md), [vendor board reference](docs/BOARD_VENDOR_REFERENCE.md), and [supplier image source registry](docs/IMAGE_SOURCES.md)
3. [System architecture and contracts](docs/ARCHITECTURE.md)
4. [Protocols: Zigbee, Matter, Thread](docs/PROTOCOLS.md)
5. [Roadmap and acceptance criteria](docs/ROADMAP.md)
6. [Engineering standards, frameworks and OOP](docs/ENGINEERING.md)
7. [Service contracts and dependency injection](docs/SERVICE_CONTRACTS.md)
8. [Agent implementation workflow](docs/AGENT_WORKFLOW.md)
9. [Setup and validation](docs/DEVELOPMENT.md)
10. [Security, storage and updates](docs/OPERATIONS.md)
11. [Architecture decisions](docs/adr/0001-matter-bridge-over-ip.md), [HTTP-only Web UI](docs/adr/0002-http-only-webgui.md)
12. [LLM agent working instructions](AGENTS.md) and [context summary](docs/llm-memory.md)

## Planned project layout

```text
firmware/
  p4/                  # ESP-IDF host application and ESP-Hosted driver
  c6/                  # build-only ESP-Hosted Wi-Fi co-processor target
  h2/                  # pinned ot_rcp build/config
components/
  board_4_3p4/         # board support, pins, power, display, touch, audio
  platform_services/   # time, storage, events, observability, network
  radio_rcp/           # UART, framing, reset, update, health
  zigbee_adapter/      # Zigbee stack + interview, bindings, reports
  device_registry/     # canonical persistent model
  capability_model/    # neutral types/units and translation rules
  matter_bridge/       # bridged endpoints, fabric commissioning
  mqtt_adapter/        # optional integration
  automation_engine/   # optional rules
  ui_lvgl/             # presentation only
  api_local/           # optional REST/WebSocket
tests/
tools/
docs/
```

The component tree remains a **target design**, not a claim that all directories exist or compile. The current two target profiles and their actual build/hardware evidence are recorded in [the H2 migration audit](docs/audits/2026-11-08-h2-zigbee-migration.md).

## Technical references

- [Espressif ESP-Matter Zigbee bridge reference](https://github.com/espressif/esp-matter/tree/main/examples/bridge_apps/zigbee_bridge)
- [Espressif ESP-Hosted-MCU v3.1.0](https://github.com/espressif/esp-hosted-mcu/tree/235aca95b52e3501b55eeb610b0275d288edc8c2/examples/wifi/sta/cp)
- [Espressif Zigbee gateway reference](https://github.com/espressif/esp-zigbee-sdk/tree/main/examples/zigbee_gateway)
- [ESP32-P4 Matter documentation](https://docs.espressif.com/projects/esp-matter/en/latest/esp32p4/)
- [Guition community hardware field notes](https://github.com/ultramcu/guition-jc4880p443c-i-w)
- [Independent board BSP notes](https://github.com/ultramcu/guition-jc4880p4-bsp)

External references are inputs for validation; pinout and SDK support must be tested on the specific board revision.

## Board manufacturer's reference

The supplied JC4880P443C_I_W hardware documentation is summarized in [Vendor board and connector reference](docs/BOARD_VENDOR_REFERENCE.md): vendor specs, 2×13 JP1 net names, USB/RS485/UART/I2C, LCD/DSI, camera/CSI, OV02C10 integration notes and bring-up requirements. The [supplier image source registry](docs/IMAGE_SOURCES.md) records externally hosted listing images; they are unverified and are not pinout evidence. Vendor documentation is a reference baseline, not a hardware test report; match the schematic to the physical board before assigning pins. Confidential third-party sensor datasheets are not distributed in this repository.
