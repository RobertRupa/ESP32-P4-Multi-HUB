# Protocol contract: Zigbee -> Matter with Thread interoperability

## Terminology

- **Zigbee**: 802.15.4-based mesh with ZCL attributes and a coordinator; devices belong to a Zigbee PAN.
- **Thread**: IPv6 mesh over 802.15.4 with a Thread Border Router (TBR) to the infrastructure LAN.
- **Matter**: IP application protocol that can run over Wi-Fi, Ethernet or Thread. A **Matter Bridge** exports representations of non-Matter Zigbee devices as bridged endpoints.
- **Matter controller**: commissions and controls Matter nodes; not identical to a TBR.

**MVP topology:** Zigbee device -> ESP32-H2 radio -> ESP32-P4 Zigbee host -> Matter Bridge over Wi-Fi (onboard C6) -> LAN. A Matter controller on the same IP network accesses bridged devices. Thread-side ecosystem uses a separately provisioned TBR. **The hub does not route Zigbee packets into Thread, does not act as a TBR, and does not need a Thread radio for Matter-over-Wi-Fi bridging.**

## ESP32-H2 responsibility

The H2 serves a dedicated Zigbee 802.15.4 RCP with UART framing, reset and boot/reflash lines. Radio firmware is version-pinned and paired with compatible host Zigbee stack. The H2 is not the Zigbee coordinator application by itself; Zigbee network coordinator logic lives in the host stack on P4.

**Feasibility gate:** Official Espressif Zigbee/Matter bridge reference describes an ESP32-S3 host + H2 RCP. The existence of ESP32-P4 Matter docs does **not** prove the whole Zigbee-Matter example compiles for ESP32-P4. Validate stack dependency target restrictions experimentally before promising implementation. If impossible, keep the ports-and-adapters design and use an external supported Zigbee coprocessor/gateway with a versioned host protocol while preserving the P4 UI/domain, or add supported intermediary MCU (explicit architectural change).

## Supported device capability candidates

| Zigbee concept | Canonical capability | Matter exposure | Validation |
|---|---|---|---|
| On/Off cluster | switch.on | On/Off device type / cluster mapping | planned |
| Level Control | light.level | Level Control, scaling 0..254 to Matter | planned |
| Temperature Measurement | sensor.temperature_c | Temperature Measurement (0.01 °C Zigbee values, normalized) | planned |
| Relative Humidity | sensor.rh_percent | Relative Humidity Measurement | planned |
| IAS Zone contact | binary.contact | suitable Matter contact sensor mapping | planned |
| Power Configuration | battery.percent / battery.voltage | only if compatible Matter semantics | planned |

Do not expose imaginary features or incorrect measurements. Manufacturer-specific clusters require compatibility plugins and test fixtures. Map reporting intervals and stale-value deadlines separately.

## Matter and Thread commissioning

1. Commission the Matter **bridge** into an authorized fabric over IP using the supported ESP-Matter flow.
2. Matter bridge creates an aggregator endpoint and a bounded set of dynamic bridged endpoints.
3. Zigbee pairing is a *separate* flow with permit-join timeout and H2 coordination; never treat a Matter fabric as a Zigbee network.
4. IPv6, multicast DNS service discovery and Matter fabric credentials must survive reboot.
5. Thread border routing, IPv6 reachability and client interoperability are tested in a lab; no direct commissioning of Zigbee gadgets into Thread.

## Security boundaries

- Zigbee trust-center/network keys and Matter fabrics are unrelated. Never share or derive one from the other.
- Only an authenticated controller can call Matter commands; local UI privileged actions need user confirmation.
- Zigbee permit-join defaults CLOSED and expires automatically.
- Separate `reset network`, `remove Zigbee device`, `remove Matter fabric` and `full factory reset`.

## Future options

- Separate dedicated 802.15.4 Thread radio and OpenThread Border Router stack, subject to P4 host support and network architecture review.
- Matter Controller capabilities and native Thread device commissioning via an existing TBR, after bridge MVP.
- Full multi-radio arbitration/coexistence if additional radios are added. Never assume H2 Zigbee+Thread simultaneity.
