# Agent implementation and review playbook

Use with [AGENTS.md](../AGENTS.md), [ENGINEERING.md](ENGINEERING.md), [SERVICE_CONTRACTS.md](SERVICE_CONTRACTS.md).

## Required order of operations

1. Read product/hardware/protocol constraints, current ADRs and affected service contracts.
2. Identify domain, port, adapter, application use-case and composition root change.
3. Specify invariants, error/timeout semantics, task ownership and memory budgets **before writing code**.
4. Write minimal API and deterministic tests (host-native whenever possible).
5. Implement only the selected module, with injected dependencies and bounded buffers.
6. Format, lint, build and run actual tests; disclose unavailable checks accurately.
7. Update docs, test matrix and PR notes; avoid changing unrelated modules.
8. Include code review for SOLID/DRY/KISS/YAGNI, race/lifetime, security and SDK target compatibility.

## Definition of a good service PR

- One coherent capability, no speculative giant abstraction.
- Interface and contract tests, implementation behind adapter.
- Explicit result/error and async completion where required.
- No direct UI widget, Zigbee SDK or Matter structures inside domain classes.
- Platform pin and task configuration in one place.
- A failure injection path, logs without secrets, restart recovery story.
- Test evidence and reproducible build command if actually run.

## Anti-patterns to reject

- `GodObject` / `HubManager` owning all radios, registry, UI, storage, Matter.
- Global `extern` mutable device lists or singletons wired at random.
- Deep device class inheritance (`Lamp : ZigbeeThing : MatterDevice : ...`).
- Blocking UART/network/NVS operations in LVGL callback.
- Passing ESP Zigbee, CHIP or LVGL types to domain service headers.
- `delay()`, unbounded loops and dynamic allocation per sensor report.
- Converting telemetry in separate UI/MQTT/Matter branches.
- Returning success when a command was merely enqueued.
- Changing Matter fabric, Zigbee PAN keys or storage schema implicitly on reboot.
- Retrying indefinitely with no backoff/error observability.

## Suggested modules-first implementation

Start with `domain/` pure C++ types and ports; `application/` services; test/mocks; then ESP-IDF `platform_services/`, `radio_rcp/`, `zigbee_adapter/`, `matter_bridge/`, `ui_lvgl/`. Keep the existing docs/ARCHITECTURE.md folder suggestions as conceptual until scaffolded.

## Agent report template

```text
Objective:
Scope and affected modules:
Interfaces and invariants:
SOLID/DRY/KISS trade-offs:
Memory, scheduling, security:
Build/test commands ACTUALLY executed + results:
Hardware validation (if any):
Changes to docs and ADRs:
Known limitations / follow-up:
```
