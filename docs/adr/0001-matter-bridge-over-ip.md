# ADR-0001: Zigbee through external H2, Matter bridge over IP

- **Status:** Accepted as design baseline; H2/P4 implementation in progress
- **Date:** 2026-11-07

## Context

Target board is ESP32-P4 + onboard ESP32-C6 network coprocessor. User selects external ESP32-H2 and wants Zigbee devices accessible within a Thread-connected smart-home ecosystem.

## Decision

Use H2 as dedicated 802.15.4 Zigbee RCP over UART; P4 hosts Zigbee coordinator and Matter Bridge; C6 supplies Wi-Fi IP connectivity. Zigbee devices are exposed as Matter **bridged endpoints**, not native Thread nodes. Existing Matter controllers and Thread networks reach the bridge through the infrastructure IPv6 network and an external Thread Border Router.

## Rationale

- Based on Espressif's Zigbee+Matter bridge example (reference uses S3+H2).
- Clear radio separation avoids assuming unsupported simultaneous H2 Zigbee/Thread networks.
- Capability adapter isolates Matter/Zigbee protocol changes and permits optional MQTT.
- No requirement for a second 802.15.4 radio until autonomous Thread border routing is truly necessary.

## Limitations

P4 build support for Zigbee host and Matter bridge, availability of safe P4 UART/GPIO and Wi-Fi coexistence are **must-pass feasibility gates**. Reference example for S3 does not establish P4 compatibility. A Thread controller/commissioner is not automatically implemented by this bridge.

## Alternatives rejected for MVP

- Zigbee-to-Thread packet tunneling: incompatible application/network semantics.
- Replacing onboard C6's ESP-Hosted image to dual-run Zigbee: risks Wi-Fi availability.
- Running Thread and Zigbee simultaneously on one H2 with no verified multiprotocol scheduler: not suitable as a baseline.
- Server-only Zigbee2MQTT: reduces autonomous gateway goal, though remains fallback when P4 SDK feasibility fails.

## Revisit triggers

Need a native Thread Border Router, Matter Controller, dedicated Ethernet, new protocol stacks, unsupported P4 host SDK or physical hardware revision constraints.

## Implementation note (2026-11-08)

The pinned ESP-Zigbee component provides an `esp32p4` remote-radio archive, and an opt-in P4 UART-RCP profile plus an H2 RCP project compile with ESP-IDF v5.5.5. A board-specific P4 UART1 GPIO26/27 profile and the H2 RCP were flashed on the test board; Spinel timed out at 460800 and 230400 baud, so P4 was restored to the C6-backed runtime. No H2 network or Zigbee pairing is verified. H2 reset/boot pins are not connected, and the IDF timeout recovery path exits the host. C6 remains the installed coordinator.
