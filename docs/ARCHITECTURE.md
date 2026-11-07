# Modular architecture

## Ports-and-adapters / event-driven monolith

ESP32-P4 is one firmware image, not a microservices host. Modular ESP-IDF components expose stable C/C++ interfaces and message queues. Only adapters depend on Espressif SDK internals. The domain contains no LVGL or CHIP/ZCL headers.

```text
[C6 ESP-Hosted Wi-Fi] ----> [Network/IP, IPv6, mDNS] ----> [Matter bridge adapter]
                                   |                          ^
                                   v                          |
                          [API / MQTT adapters]       [Endpoint mapper]
                                   ^                          ^
                                   |                          |
[H2 Zigbee RCP] -> [UART/RCP] -> [Zigbee adapter] -> [Capability model / Registry]
                                                       |       |
                                                    [Event bus]|
                                                       |       |
                                                  [LVGL UI] [Rules]
                                                       |
                                                 [Storage / NVS]
```

## Bounded contexts

| Component | Owns | Must not own |
|---|---|---|
| board_4_3p4 | pin definitions, clocks, display, touch, storage power rails | Zigbee/Matter policy |
| platform_services | network, NVS, time, telemetry, event bus, secure storage | device translations |
| radio_rcp | UART framing, H2 reset/boot, image compatibility, watchdog | user devices |
| zigbee_adapter | coordinator lifecycle, network formation, ZDO/ZCL, reporting, command status | Matter endpoints |
| device_registry | stable device identity, lifecycle, capability metadata, availability | any transport handle |
| capability_model | typed attributes, command semantics, valid units/ranges | raw SDK structures |
| matter_bridge | bridge aggregator/endpoints, commissioning, fabric security, notifications | Zigbee network key |
| mqtt_adapter | optional topic/discovery and command mapping | authoritative state |
| ui_lvgl | rendering and input dispatch, i18n | blocking Zigbee commands |
| automation_engine | optional event predicates/actions and permissions | direct hardware drivers |

## Core canonical identifiers

- **DeviceId:** stable internal UUID/128-bit ID; separate from Zigbee short address (which can change).
- **Zigbee identity:** coordinator network identity + IEEE EUI-64; endpoint and cluster identify subresources.
- **CapabilityId:** DeviceId + component endpoint + semantic capability key.
- **Matter mapping:** persistent association DeviceId -> bridged endpoint ID, compatible device type, attribute bindings, mapping schema version.
- **Attribute:** type, value, unit, timestamp, freshness, source, quality/reachable state; map command/measurement paths explicitly.
- **Command:** command ID (idempotency correlation), target, desired value, deadline, caller context; result accepted/pending/succeeded/failed/timed_out.
- Avoid using Zigbee NWK16 address or Matter endpoint ID as the only persistent device key.

## Event contract (illustrative, version 1)

```json
{
  "schema": 1,
  "event_id": "uuid",
  "type": "device.attribute.changed",
  "device_id": "stable-device-uuid",
  "capability": "switch.on",
  "value": true,
  "source": "zigbee",
  "timestamp_ms": 0,
  "quality": "reported",
  "correlation_id": null
}
```

Specify no allocation-heavy JSON on the hot path: runtime should use bounded typed records; JSON is only wire/debug format.

## Threading model and failure isolation

- Radio processing runs in dedicated tasks; UART RX task forwards framed packets to Zigbee adapter.
- Zigbee, Matter and networking SDK callback handlers must not block on I/O, UI or flash writes.
- Single owner for state changes: registry actor processes ordered device events; subscribers receive immutable snapshots/deltas.
- LVGL runs on its UI task; other tasks post UI messages, never call LVGL objects directly.
- Bounded queues, priorities, backpressure, watchdog and failure counters. Overflow policy: drop noncritical telemetry first, never silently drop security/reset commands.
- Idempotent replay on reboot; commissioning/migration are state machines with durable transitions.
- Design for PSRAM with internal DMA-capable memory reserved for display/SDIO/radio; allocation budgets measured during integration.

## Matter endpoint lifecycle

- Stable endpoint mapping persisted before activation and reconciled on boot.
- Only capabilities with verified mapping to Matter standard device types are published; unsupported vendor features remain local/MQTT.
- Reachable=FALSE after configured timeout, not fabricated online state.
- Command acknowledged only according to defined semantics; publish confirmed Zigbee reports rather than assuming actuation.
- Device remove/factory reset/fabric removal are separate operations; fabric removal must not erase Zigbee network without user request.
- Define max bridged device/endpoint count after heap and certification tests, not from theoretical PSRAM.

## Configuration boundaries

`CONFIG_HUB_ZIGBEE=y`, `CONFIG_HUB_MATTER=y`, `CONFIG_HUB_MQTT=n`, `CONFIG_HUB_WEB=n`, `CONFIG_HUB_RULES=n` are proposed Kconfig module toggles, **not yet implemented**. Use compile-time feature flags and runtime capability discovery.

## Engineering governance

Follow [Engineering standards](ENGINEERING.md), [Service contracts](SERVICE_CONTRACTS.md), and [Agent workflow](AGENT_WORKFLOW.md). The core is a portable C++17 domain; ESP-IDF/FreeRTOS, Zigbee, Matter and LVGL remain infrastructure/adapters. Dependencies are constructor-injected in the application composition root; no service locator. Contract tests verify module boundaries and failure semantics.
