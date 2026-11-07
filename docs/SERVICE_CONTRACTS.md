# Service contracts and component boundaries

This is a contract design, **not a compiled API**. Use `docs/ENGINEERING.md` for normative coding practices.

## Contract catalog (proposed)

| Port / service | Responsible for | Key operations | Owner |
|---|---|---|---|
| `IRcpTransport` | H2 byte/framing transport and version handshake | `start`, `send`, `subscribe_frame`, `reset` | RCP task |
| `IZigbeeCoordinator` | Zigbee network lifecycle | `start`, `permit_join(until)`, `leave`, `send_zcl` | Zigbee task |
| `IDeviceReader` | Immutable snapshots and lookup | `get(device_id)`, `list(page)` | Registry |
| `IDeviceCommander` | Authorized command acceptance | `submit(request)` => accepted or error | Application |
| `IEventPublisher` | Typed bounded event delivery | `publish(event)` => delivered/queue full | Event bus |
| `ICapabilityTranslator` | Zigbee ZCL <-> domain values | `translate_report`, `encode_command` | Zigbee adapter |
| `IMatterBridgePort` | Matter endpoint/fabric adapter | `upsert_endpoint`, `set_reachable`, `report_attribute` | Matter task |
| `IStateStore` | Atomic versioned persistent records | `load`, `commit`, `migrate` | Storage task |
| `INetworkStatus` | IPv6/mDNS/Wi-Fi readiness | `snapshot`, `subscribe` | Network service |

## Explicit behavior requirements

- `start` is idempotent or returns a defined conflict. All services declare allowed lifecycle states.
- Each API declares task affinity and whether it blocks. Radio and Matter callbacks only enqueue bounded records.
- `submit` means the request was validated and enqueued, *not* that physical Zigbee device acted.
- Completion events carry command correlation IDs and success/failure reasons. Repeated commands require an explicit dedupe policy.
- `get`/ `list` return copies or immutable snapshots, never references to mutation-owned state that may expire.
- Cross-module payloads must not hold SDK-owned transient pointers. Own or copy necessary fields.
- `IStateStore` follows crash-safe commit semantics; no unbounded synchronous writes in UI or radio tasks.
- `IMatterBridgePort` mapping uses persistent stable IDs; unsupported capability must return `Unsupported`, never silently misrepresent state.
- Services indicate `Unavailable` during lost Wi-Fi/H2 instead of inventing success.
- An authentication principal/context is required for privileged command and commissioning operations.

## Dependency injection example (conceptual)

```cpp
// Domain/application headers may not include ESP-IDF/Matter/LVGL.
class DeviceCommandService final {
public:
    DeviceCommandService(IDeviceReader& devices,
                         IZigbeeCommandPort& zigbee,
                         IEventPublisher& events) noexcept;
    ErrorCode submit(const CommandRequest& request) noexcept;
private:
    IDeviceReader& devices_;
    IZigbeeCommandPort& zigbee_;
    IEventPublisher& events_;
};

// Composition root, inside firmware/p4; concrete types are from adapters.
// DeviceCommandService commands{registry, zigbee_adapter, event_bus};
// UiController ui{devices_reader, commands, event_bus};
```

The `IZigbeeCommandPort` name above is a future separated command port to be declared in the domain. No production code should copy this sketch without defining the full types and implementing tests.

## Contract change policy

When a contract changes, update its header-level semantics, tests, downstream mock and adapters in the same PR. Breaking changes need migration/version strategy. Keep the architecture decision record updated for boundary changes.

## Test doubles

Native fakes implement the same error and timeout behavior as actual adapters; add contract tests run against fake and hardware adapters when hardware is present. Never fake a green hardware test.
