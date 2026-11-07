# ESP32-P4 Multi-HUB

**Status:** host-testable domain components started; no working gateway firmware yet.

A modular, local-first embedded home automation gateway built around **Guition JC4880P443C_I_W** (ESP32-P4 + ESP32-C6), 4.3-inch touch display and an **external ESP32-H2 acting as a Zigbee 802.15.4 RCP**.

## Product goal

Pair and manage Zigbee devices locally, represent supported capabilities as **Matter bridged endpoints** on the IP network, and allow Matter controllers reachable via Wi-Fi/Ethernet/**Thread Border Router** to operate them. Add a touchscreen UI, local API, optional MQTT, automation and diagnostics without requiring cloud services.

**Important protocol distinction:** Zigbee devices are **not** turned into native Thread nodes. This hub is a Matter bridge advertised on the IP LAN. Thread Matter controllers/devices can interact with its Matter endpoints **through an existing Thread Border Router and working IPv6/mDNS routing**. The ESP32-H2 is dedicated to Zigbee; the same H2 must not be assumed to simultaneously serve a separate Thread RCP. A standalone Thread Border Router is a **future hardware/module option**, not part of MVP.

## Hardware roles

- **ESP32-P4** — application processor, display/UI, device registry, Zigbee host stack, Matter Bridge, local services.
- **Onboard ESP32-C6** — Wi-Fi/BLE network coprocessor using ESP-Hosted over SDIO. Preserve its stock integration unless explicitly migrating.
- **External ESP32-H2** — dedicated Zigbee radio running Espressif `ot_rcp` compatible with Zigbee gateway/bridge reference examples over UART; supports reset/boot control.
- **Network** — Wi-Fi via C6, with optional Ethernet in future; Matter over IP, not an 802.15.4 Thread interface in MVP.

## Docs index

1. [Product requirements](docs/PRODUCT.md)
2. [Hardware knowledge and pin reservation](docs/HARDWARE.md)
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
  p4/                  # ESP-IDF application, application composition root
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

This tree is a **target design**, not a claim that the directories exist or compile. Start with the hardware verification milestone before adding protocol stacks.

## Technical references

- [Espressif ESP-Matter Zigbee bridge reference](https://github.com/espressif/esp-matter/tree/main/examples/bridge_apps/zigbee_bridge)
- [Espressif Zigbee gateway reference](https://github.com/espressif/esp-zigbee-sdk/tree/main/examples/zigbee_gateway)
- [ESP32-P4 Matter documentation](https://docs.espressif.com/projects/esp-matter/en/latest/esp32p4/)
- [Guition community hardware field notes](https://github.com/ultramcu/guition-jc4880p443c-i-w)
- [Independent board BSP notes](https://github.com/ultramcu/guition-jc4880p4-bsp)

External references are inputs for validation; pinout and SDK support must be tested on the specific board revision.
