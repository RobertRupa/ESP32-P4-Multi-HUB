# ESP32-P4 Multi-HUB

**Status:** The P4 was briefly flashed with the board-specific H2 host profile (UART1 GPIO26/27, 230400 baud), but P4 timed out waiting for Spinel and IDF exited during RCP reset recovery. The P4 was restored to the default C6-backed image; `p4-hosted>` returned, C6 ESP-Hosted 3.1.0 and Wi-Fi/DHCP started, and C6 Zigbee remains available but has no formed network or joined devices. The M5Stack Gateway H2 RCP image was flashed to the external H2 and logs that its RCP app started, but P4/H2 handshake did not succeed at either 460800 or 230400 baud. C6 firmware/NVS were not modified. The H2 host/RCP integration, Zigbee commissioning, Thread and Matter Bridge remain unvalidated.

A modular, local-first embedded home automation gateway built around **Guition JC4880P443C_I_W** (ESP32-P4 + ESP32-C6), 4.3-inch touch display and an **external ESP32-H2 acting as a Zigbee 802.15.4 RCP**.

## Product goal

Pair and manage Zigbee devices locally, represent supported capabilities as **Matter bridged endpoints** on the IP network, and allow Matter controllers reachable via Wi-Fi/Ethernet/**Thread Border Router** to operate them. Add a touchscreen UI, local API, optional MQTT, automation and diagnostics without requiring cloud services.

**Important protocol distinction:** Zigbee devices are **not** turned into native Thread nodes. The accepted product baseline remains a Matter bridge advertised on the IP LAN. The installed runtime still runs Zigbee natively on the C6 over ESP-Hosted Peer Data/SDIO and does not provide Thread. An opt-in P4 Zigbee-over-UART-RCP build profile now exists, but it does not activate in the default image. The H2 host/RCP handshake and Zigbee commissioning remain unverified. No Matter Bridge or Home Assistant discovery has been validated.

## Hardware roles

- **ESP32-P4** — application processor, display/UI, device registry, Zigbee host stack, Matter Bridge, local services.
- **Onboard ESP32-C6** — provides Wi-Fi over ESP-Hosted SDIO and currently runs the experimental native Zigbee coordinator; Zigbee shares its 2.4 GHz RF path with Wi-Fi. The opt-in migration profile builds it as Wi-Fi-only, but that image is not installed.
- **External ESP32-H2** — intended dedicated Zigbee RCP. `firmware/h2` now builds a standard UART Spinel RCP image; compatibility with the P4 host, wiring and runtime are not yet validated.
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
11. [Architecture decisions](docs/adr/0001-matter-bridge-over-ip.md)
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

The component tree remains a **target design**, not a claim that all directories exist or compile. The P4 host, C6 profiles and H2 RCP project have the build evidence documented in `docs/audits/2026-11-08-h2-zigbee-migration.md`; hardware integration is still pending.

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
