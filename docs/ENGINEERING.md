# Engineering standards and framework selection

**Status:** normative design guidance for new code, not an assertion that implementations exist.

## Recommended stack

| Layer | Choice | Why / boundary |
|---|---|---|
| Firmware host | **ESP-IDF** (version pinned after P4 compatibility test), **C++17** application code, C where required | Official ESP32-P4 HAL/drivers, FreeRTOS, NVS, OTA, network, component builds; avoid Arduino as the foundation |
| Zigbee transport | ESP-IDF UART driver + RCP host adapter; external ESP32-H2 flashed with version-pinned `ot_rcp` | Zigbee stack and compatibility gate must be verified for P4; keep RCP protocol behind interface |
| Zigbee coordinator | Espressif **esp-zigbee-sdk** if target support verified | Only the Zigbee adapter imports ZCL/ZDO and Espressif types |
| Matter | **esp-matter / connectedhomeip** on P4 if supported in a pinned revision | Owns operational Matter data, fabrics, endpoints; adapter translates canonical capabilities |
| HMI | **LVGL 9.x** when board BSP/UI compatibility verified, ESP-IDF `esp_lcd`/MIPI DSI/touch drivers | One UI task; adapters communicate through queues, no direct domain access to widgets |
| Networking | ESP-Hosted C6, esp_netif, lwIP, mDNS, esp_event | IPv6/mDNS required for Matter; keep network stack in infrastructure |
| Embedded scheduling | FreeRTOS | Ownership-specific tasks, finite queues, timeouts, task watchdog |
| Persistence | NVS for small config/mappings, wear-aware writes; FATFS/microSD optional for logs | Schema-versioned, crash recovery, no SD dependency for essential operation |
| Diagnostics | ESP_LOGx + structured error codes, heap/task/watchdog metrics | Redact keys and commissioning secrets |
| Native tests | **Unity** via ESP-IDF for firmware, **GoogleTest + CMake/CTest** for pure host-side C++ domain | Mock ports; no hardware or network needed for domain tests |
| Static checks | **clang-format**, **clang-tidy** (where practical), compiler warnings, **cppcheck** optional, GitHub Actions CI | Exact tool versions and settings committed |
| Dependency management | ESP-IDF component manager + pinned `idf_component.yml`, committed lockfiles when created | Reproducible builds; avoid downloading from moving default branches |

Avoid introducing heavyweight dynamic languages, web frameworks or a general-purpose plugin runtime on P4. Optional WebGUI should use ESP-IDF `esp_http_server` with REST/WebSocket and a separately built static frontend only if a measured use case justifies it. H2 firmware is a distinct C-oriented target; do not port the whole object model to RCP.

## Architectural style: modular monolith, ports and adapters

- **Hexagonal (ports-and-adapters) + Clean Architecture**: core domain depends on interfaces and value types, not vendor SDK, UI or transport. Dependencies always point inward.
- **OOP with composition** as default for long-lived services (registry, coordinator, Matter mapper); prefer small focused classes with encapsulated invariants. No inheritance hierarchy of device subclasses or global singletons.
- **Service contracts** are header-level interfaces plus behavior: preconditions, result/error semantics, idempotency, timeout, ownership, thread context and versioning. An interface is not just virtual methods.
- **Dependency injection** occurs in the P4 composition root, explicitly assembling concrete adapters. Prefer constructor injection or an explicit `init(dependencies)`; no hidden service locator. Use static lifetime storage where practical.
- **Dependency inversion**: core declares ports; `zigbee_adapter`, `matter_bridge`, `ui_lvgl`, `storage_nvs` implement them. External SDK types never leak across the boundary.
- **CQRS-lite**: distinguish queries returning immutable snapshots from commands creating asynchronous work; avoid overengineered event sourcing.
- **Event-driven**: typed, versioned messages on bounded queues; separate event notification from authoritative registry state.

### Dependency direction

```text
app/composition_root
  |-> infrastructure (esp_idf, uart, NVS, ESP-Hosted)
  |-> adapters       (zigbee, matter, lvgl, optional mqtt/http)
  '--> application   (use-cases, services, commands, event dispatch)
           '--> domain (DeviceId, Capability, Result, service ports)
```

The domain must compile and run tests without any ESP-IDF, LVGL, FreeRTOS, ESP Zigbee or Matter headers.

## SOLID applied pragmatically

- **S — Single Responsibility:** `ZigbeeCoordinator` controls Zigbee lifecycle, `DeviceRegistry` owns identity/state, `MatterEndpointMapper` maps to Matter. Avoid one `HubManager` doing everything.
- **O — Open/Closed:** add a mapping strategy/adapter for a new device capability without modifying all clients. Do not build speculative plugin abstractions for nonexistent use cases.
- **L — Liskov:** test doubles and implementations obey the same service semantics, including errors, timeouts and queue-full conditions.
- **I — Interface Segregation:** `IDeviceReader`, `IDeviceCommander`, `IEventPublisher` rather than a monolithic `IHubService`.
- **D — Dependency Inversion:** application depends on ports; hardware/SDK adapters satisfy them.

## DRY, KISS, YAGNI and cohesion

- **DRY:** one canonical capability schema, one mapping policy per protocol, one device identity model, one error taxonomy. Do not duplicate Zigbee-to-Matter conversion in UI or MQTT.
- **KISS:** prefer explicit control flow, bounded fixed-size structures and simple components; add architecture only if it resolves a real change/test boundary.
- **YAGNI:** do not implement scripting engines, distributed brokers, dynamic plugins, a generic ORM or a framework within a framework before proven requirements.
- **High cohesion / low coupling:** feature-specific modules own their data; public headers expose minimal types.
- **Law of Demeter:** no chaining through internals of other services; call the owning service's contract.
- **Tell, don't ask** where useful: validate and perform command in the owner, don't expose mutable internals.
- **Composition over inheritance** and explicit error handling over exception propagation.

## Embedded C++ rules

- C++17 subset verified for actual ESP-IDF toolchain. **Disable or avoid C++ exceptions/RTTI if consistent with build configuration**; choose an explicit status/result type.
- **RAII** for mutex guards, lock scopes, queue ownership and resources where runtime support allows; never assume destructors run on abrupt watchdog reset.
- **No heap churn in radio or UI hot paths**. Prefer `std::array`, fixed-capacity buffers, preallocated queues and controlled object lifetimes. Profile internal DRAM vs PSRAM vs DMA-capable memory.
- `std::unique_ptr` may express ownership during initialization, but allocation is not automatically safe in real-time processing. Prefer references for non-owning dependencies and `std::span`-like views only if toolchain supports the chosen API (C++17 has no standard `std::span`).
- Avoid raw owning pointers; non-owning pointers may be nullable, references non-null. Document ownership/lifetime.
- No unbounded `String`, `std::vector`, map growth or recursive traversal on callbacks. Avoid dynamic polymorphism in hot loops when virtual dispatch or vtable costs matter; measure rather than ban it wholesale.
- Do not allow C++ exceptions to cross C callbacks; wrap ESP-IDF C API at the adapter boundary with explicit error conversion.
- `extern "C"` is only used for C ABI entry points/callbacks; initialize C++ objects in a deterministic composition root.
- Mark invariant inputs `const`, scoped enums, strong units (e.g., centi-degrees vs degrees), and no magic pin constants.
- Follow ESP-IDF error conventions and codify one domain `ErrorCode` mapping. Log errors with context, but redact sensitive bytes.

## Thread safety and service contracts

Every public service must state:

1. **Responsibility** and owner task.
2. **Inputs/outputs** with units and lifetime/ownership.
3. **Concurrency**: thread-safe or single-task-only; whether callbacks can run on an ESP-IDF event loop.
4. **Timing**: blocking/nonblocking, queue capacity, deadline, retry policy.
5. **State**: lifecycle and persistence semantics, invariant, idempotency and ordering.
6. **Errors**: invalid argument, unavailable, conflict, queue full, timeout, transport, unsupported and permission denied.
7. **Observability**: counters, structured event ID, correlation ID.
8. **Version compatibility** and test cases.

**Example contract, illustrative only (not implemented):**

```cpp
enum class ErrorCode : uint8_t {
    Ok, InvalidArgument, Unavailable, Conflict, QueueFull,
    Timeout, Unsupported, PermissionDenied, TransportFailure
};

struct CommandRequest {
    DeviceId device_id;
    CapabilityId capability_id;
    CapabilityValue requested_value;
    uint64_t correlation_id;
    uint32_t deadline_ms;
};

// Non-blocking; acceptance != Zigbee execution success.
// Must be called from task context, not ISR.
// Owner retains no references to caller-owned buffers.
class IDeviceCommander {
public:
    virtual ~IDeviceCommander() = default;
    virtual ErrorCode submit(const CommandRequest& request) noexcept = 0;
};
```

`DeviceId`, `CapabilityId`, and `CapabilityValue` are conceptual domain types to implement separately. Use a completion event to report confirmed/failed execution and preserve correlation ID. For ISR paths, use FreeRTOS `FromISR` APIs and a designated channel; never call arbitrary service methods from ISR.

## Coding conventions

- File/folder names `snake_case`; types `PascalCase`; functions/methods `snake_case`; local/member variables `snake_case` with trailing `_` for private fields; constants `kPascalCase`; `CONFIG_HUB_*` for Kconfig.
- Header `#pragma once` or include guards consistently; headers self-contained. Keep private SDK includes in `src/` and expose `include/` public headers with CMake `REQUIRES`/ `PRIV_REQUIRES`.
- Comment *why*, preconditions, constraints and concurrency rules rather than narrating trivial operations.
- Fail fast on invalid configuration; bounded retries and exponential backoff for recoverable I/O, no busy waits.
- No silent ignored `esp_err_t`; no `abort()` for ordinary link loss; recover or escalate explicitly.
- Allocate stack deliberately; check stack watermark, memory ownership, callback context, recursion and watchdog impact.
- Hardware pins/timeouts/task priorities are named config, not scattered literals.

## Testing and definition of done

Test pyramid: native unit tests -> ESP-IDF component tests -> integration with fake radio/Matter controller -> hardware-in-loop with actual P4/C6/H2 -> real multi-vendor Matter interoperability. Include command timeout, queue full, malformed reports, reboot, simulated power loss, endpoint-map migration, retries and stale state.

A change is done only when:
- Code follows this contract and architecture boundaries.
- New behavior is covered by a meaningful test; no tests are falsely claimed.
- Formatting/static analysis/build checks pass for the affected targets (or the limitation is documented).
- Docs and ADR updated if the public interface, topology or constraints changed.
- Security and memory impact reviewed, including secret redaction and resource caps.
- Actual results and reproducible commands included in PR description.

## Pull-request review checklist

- [ ] Are interfaces minimal and one service owns each state mutation?
- [ ] Are SDK dependencies hidden behind adapters?
- [ ] Are ownership, task affinity, timeouts, queues, error semantics documented?
- [ ] Is the solution simpler than a suitable alternative (KISS/YAGNI)?
- [ ] Is logic duplicated across UI/Matter/Zigbee (DRY)?
- [ ] Are responsibilities separated according to SOLID?
- [ ] Are heap, stack, storage writes and radio retry behavior bounded?
- [ ] Are unit and hardware tests reported with evidence?
