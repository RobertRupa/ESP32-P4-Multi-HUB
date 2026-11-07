# LLM project memory (authoritative orientation)

**Repository:** RobertRupa/ESP32-P4-Multi-HUB. **Phase:** initial host-testable domain component, 2026-11-07. The repository started as documentation only; no firmware or verified pin map existed at inception.

**Board:** Guition JC4880P443C_I_W (JC-ESP32P4-M3); ESP32-P4 application SoC, onboard ESP32-C6 Wi-Fi/BT through SDIO ESP-Hosted; 4.3-inch 480x800 ST7701S MIPI-DSI, GT911 touch, likely 16 MB flash and 32 MB PSRAM, microSD, ES8311 audio, RS485, USB, battery supply; vendor samples/schematic are primary board references. Exact PCB variant and P4 silicon revision require hardware confirmation.

**Architecture:** External ESP32-H2 as *Zigbee* RCP via UART. P4 hosts coordinator and IP Matter Bridge. Zigbee devices appear as bridged Matter endpoints. Matter over IP can be reached by controllers on Wi-Fi/Ethernet and from Thread ecosystems through a separate Thread Border Router. Not a Thread Border Router and not native Thread devices; H2 is NOT assumed dual Zigbee + Thread.

**Primary risks:** ESP32-P4 support for Espressif Zigbee gateway and esp-matter Zigbee bridge examples cannot be assumed from ESP32-S3 demo; Wi-Fi C6 ESP-Hosted integration; P4 chip revision; 802.15.4 coexistence with C6 2.4GHz; UART pin allocation; PSRAM/flash use; Matter endpoint resource ceiling and persistence.

**Source of truth order:** verified board schematic and hardware tests > pinned SDK/compiler evidence > upstream documentation > community field notes > this planning document. Explicitly distinguish measured, documented, inferred and TODO.

**Code structure:** BSP, network/platform, RCP transport, Zigbee adapter, canonical capability/device registry, Matter adapter, optional MQTT/REST/rules, LVGL UI. Typed events; adapters communicate through domain interfaces and queues.

**Implementation status:** `components/capability_model` defines typed values and validation for the initial capability set. `components/device_registry` provides caller-bounded, in-memory device identity and reachability management. Both host CMake/CTest suites pass (1/1 each). Registry persistence/migrations, protocol adapters, P4/H2 firmware builds and hardware validation remain unimplemented.

**Workflow:** Read AGENTS.md and docs/{PRODUCT,HARDWARE,ARCHITECTURE,PROTOCOLS,ROADMAP,DEVELOPMENT,OPERATIONS}. Do not mark features implemented before builds, tests and P4 physical validation. Preserve user firmware and NVRAM. English default, Polish optional.

**Next verified milestone:** board revision/schematic check, screen/touch/Wi-Fi bring-up, UART wiring and H2 RCP handshake, offline registry unit tests. Then validate actual ESP32-P4 Zigbee/Matter build support.

**External references:** Espressif esp-matter/examples/bridge_apps/zigbee_bridge; esp-zigbee-sdk/examples/zigbee_gateway; ESP-IDF ot_rcp; Guition vendor JC4880P443C_I_W zip; ultramcu guition-jc4880p443c-i-w (community).

**Engineering standard:** ESP-IDF with portable C++17 domain/application, LVGL9 UI, ESP-Hosted/C6, FreeRTOS, Zigbee/Matter adapters. Mandatory: ports-and-adapters, SOLID, DRY, KISS, YAGNI, OOP composition over inheritance, constructor DI, immutable snapshots, typed bounded events, explicit service contracts, host-side mock tests. See docs/{ENGINEERING,SERVICE_CONTRACTS,AGENT_WORKFLOW}. No claim of verified framework/SDK support until P4 build tests.

**New vendor reference (2026-11-07):** [BOARD_VENDOR_REFERENCE.md](BOARD_VENDOR_REFERENCE.md) documents supplied JC4880P443C_I_W/Y specification, board schematic, generic Arduino getting-started guide, vendor example ZIP and OV02C10 integration facts. Board supports 5 V input (~320 mA typical), 117.01×69.41 mm module, ST7701S DSI, capacitive touch, two USB-C ports (FS/HS), CSI, microSD, ES8311 + speaker, MAX485/RS485, two UARTs, I2C and 2×13 JP1. JP1 contains P4 GPIO, C6 UART/control and supply nets: **not a bank of 26 free P4 GPIOs**. Vendor's Arduino/LVGL GT911 example uses SDA GPIO7 and SCL GPIO8; shared I2C bus. OV02C10 is only a supplied sensor reference, not proof of exact populated camera module. Its datasheet has confidentiality restrictions: do not redistribute. No hardware validation has occurred.
