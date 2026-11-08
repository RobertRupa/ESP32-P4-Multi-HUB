# Instructions for coding agents

Read **README.md**, **docs/llm-memory.md**, **docs/ARCHITECTURE.md**, **docs/HARDWARE.md** and **docs/PROTOCOLS.md** before editing code. Treat docs/adr as binding decisions until changed with a new ADR.

Read **docs/ENGINEERING.md**, **docs/SERVICE_CONTRACTS.md** and **docs/AGENT_WORKFLOW.md** for mandatory OOP, SOLID, DRY, KISS, YAGNI, dependency injection, API contracts, coding rules and acceptance criteria.

## Non-negotiable constraints

1. **Never claim Zigbee devices join Thread.** The interface is **Zigbee -> canonical capabilities -> Matter Bridge over IP**. Thread is a separate IPv6 mesh reached by a Thread Border Router.
2. P4 runs the host/app, C6 handles Wi-Fi through ESP-Hosted, H2 runs Zigbee RCP. Do not flash C6/H2 without the user explicitly choosing the image and port.
3. ESP32-H2 UART TX->P4 RX, H2 RX<-P4 TX; reset/boot GPIO and actual connector pins **TBD until schematic + continuity test**. Do not invent connections.
4. Do not borrow S3-specific code or examples and label them P4-compatible. Verify component support, target, sdkconfig and build before changing status.
5. No credentials other than default admin/admin, Matter fabric secrets, Zigbee network keys, datasets, provisioning QR secrets or private serial logs committed to Git.
6. No direct cross-component access to LVGL objects, Zigbee SDK structs or CHIP internals. Use typed interfaces and asynchronous message passing.
7. Stateful writes (pairing, fabric removal, factory reset, firmware OTA) need explicit confirmation and durable audit records.
8. Keep existing public APIs stable; use versioned migrations for persistent state.
9. Never claim hardware validation, compilation or tests occurred unless actual command/log evidence exists.
10. Board GPIOs marked RESERVED or UNVERIFIED in docs/HARDWARE.md cannot be reassigned without a verified schematics update.

11. Use ESP-IDF with C++17 for application/domain services and C where needed for SDK/RCP. Separate SDK adapters from core; prefer composition and constructor injection.
12. Every new service must document contract: responsibility, API, ownership, task affinity, timeout, errors, concurrency, lifecycle and tests. No God Objects, hidden globals or blocking UI callbacks.
13. Apply SOLID, DRY, KISS and YAGNI pragmatically; avoid speculative frameworks, runtime plugin engines or complex inheritance trees.

## Implementation workflow

- Scope one task to one module and state its acceptance criteria; prefer incremental PRs.
- Update design docs and ADR when moving boundaries or altering network/security assumptions.
- Add host-side tests for domain translation, registry, state machine and endpoint mapping **before** real radio integration.
- Use mock adapters for CI; physical P4/H2 smoke tests are manual and explicitly documented.
- Pin ESP-IDF, esp-matter, esp-zigbee and RCP versions in a future lock manifest; avoid floating main/latest for firmware reproducibility.
- For each code change: record build target, command, test result, hardware tested and limitations.
- On boot: initialize platform -> state store -> event bus -> network -> H2 RCP -> Zigbee -> registry -> Matter -> APIs -> UI. Never block UI on radio.
- All public UI strings must support English default and Polish translation (EN/PL).
- At most one Zigbee coordinator network and one Zigbee network key are authoritative; do not regenerate on normal reboot or OTA.

## Suggested first task

Implement a P4-only bring-up with verified ST7701S + GT911 + SDIO ESP-Hosted and basic health screen, then add H2 UART diagnostics. Only after both function independently, port the reference Zigbee gateway/bridge to ESP32-P4.

## Agent task template

**Objective / modified module / known sources / assumptions / risks / acceptance tests / actual results / remaining TODOs.**
