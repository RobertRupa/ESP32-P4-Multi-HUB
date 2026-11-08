# H2 Zigbee RCP Migration Audit

- Date: 2026-11-08
- Objective: move Zigbee radio ownership from the integrated C6 to an external H2 RCP while retaining the P4 application host and C6 ESP-Hosted Wi-Fi.
- Scope: Zigbee-only; Thread and Matter Bridge are not part of this migration step.
- Toolchain: ESP-IDF v5.5.5, RISC-V GCC 14.2.0.

## Implementation

- Added `IZigbeeCoordinator` and a common immutable snapshot in `firmware/p4/main/zigbee_coordinator.hpp`; the installed C6 bridge and P4 UART-RCP service implement the same API. P4 UI and HTTP consumers now depend on the port.
- Added `CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE`, default off. Its separate defaults profile selects the ESP-Zigbee UART Spinel coordinator. The profile defaults UART port 1 and leaves TX/RX GPIOs at `-1`.
- In H2 mode, the P4 radio service starts the ESP-Zigbee coordinator over UART and skips OpenThread/multipan initialization. `request_search()` is backed by a host-tested state policy: unavailable stack rejects; factory-new forms a network, then opens the audited 180-second join window after BDB success; an already formed network opens the window directly.
- Added `firmware/h2`, a standard UART Spinel RCP project with native 802.15.4 radio, UART0 TX=GPIO24/RX=GPIO23 per M5Stack Gateway H2 NCP documentation, vendor hook enabled, USB Serial/JTAG system console, and 230400 baud per M5Stack's Grove guidance for links that fail at 460800.
- Added a board-specific P4 test profile `sdkconfig.defaults.h2-jc4880p443c` using UART1, P4 TX=GPIO26/RX=GPIO27, and the local development Web profile. Generic H2 defaults remain unset. The user reports H2 connected through the Grove TX/RX header and confirms no MAX485 conflict.
- Added `firmware/c6/sdkconfig.defaults.h2-zigbee-wifi`; Wi-Fi remains enabled and C6 native Zigbee is off. CMake now includes C6 Zigbee source/dependencies only when that feature is enabled.
- Aligned P4 and C6 component manifests to the IDF 5.5.5 version already used by their lockfiles and builds.

## Build and test results

- Default P4/C6-backed profile: final `idf.py -B build build` passed after the search-policy extraction. The local generated P4 `sdkconfig` remains H2/Zigbee disabled.
- P4 H2-RCP profiles: the generic isolated profile builds with pins unset; the board-specific profile builds with UART1 TX GPIO26/RX GPIO27 at 230400 baud. The ESP-Zigbee `esp32p4` remote-radio library links successfully. Board-specific P4 image SHA-256: `70994874c30f758d92e62d7d75eccd240d77d9b2f055c44a0457aa83f0194177`.
- H2 RCP at 230400 baud: fresh IDF 5.5.5 build passed; image `/tmp/hub_h2_rcp_grove/hub_h2_zigbee_rcp.bin`, 340,272 bytes, SHA-256 `ce9969a581c9a4e1202f003c8e2d3dd7810d97c82ad35391e4b4d7ccc9a609f2`.
- C6 Wi-Fi-only profile: isolated build passed; resolved config has `CONFIG_ESP_HOSTED_CP_FEAT_WIFI=y` and `CONFIG_HUB_C6_ZIGBEE_COORD_ENABLE` unset. Image size was 1,095,072 bytes.
- Existing C6 Zigbee SDIO profile regression build passed with the conditional CMake source list.
- Host search-policy test passed 1/1 after form-then-join was added; the capability model, registry and web approval tests also passed 1/1 each. `git diff --check` passed.

## Hardware status and limitations

- The updated H2 RCP image was flashed to ESP32-H2 rev0.1 on `/dev/ttyACM1`; no full erase, separate flash read-back, or eFuse operation was run. USB Serial/JTAG logs confirmed `H2 radio RCP started; host owns Zigbee coordinator state`.
- A board-specific P4 image was encrypted-flashed to ESP32-P4 rev3.2 on `/dev/ttyACM0` with UART1 TX GPIO26/RX GPIO27 and 230400 baud. P4 booted, C6 ESP-Hosted 3.1.0 matched, Wi-Fi/DHCP and HTTPS started. P4 logged two `P-SpinelDrive-: Wait for response timeout` events; no Spinel response or RCP version was received. IDF then logged `Failed to reset co-processor`; the failure callback recorded that no hardware reset line is connected, and `SpinelDriver::ResetCoprocessor()` called `exit(1)`. P4 rebooted. No Zigbee network request was issued.
- A P4 test was repeated at 460800 baud before reducing to 230400; both rates timed out. The H2 logs show its RCP app started, but host/RCP communication still failed. The specific cause is unresolved; verify Grove TX/RX crossover at both connectors and confirm the exact Unit-versus-Module board variant. M5Stack's [Gateway H2 Zigbee Gateway guide](https://docs.m5stack.com/en/esp_idf/zigbee/unit_gateway_h2/zigbee_gateway) uses `ot_rcp`, documents H2 UART0 RX=GPIO23/TX=GPIO24, recommends 230400 when 460800 is unreliable over Grove, and configures host reset/boot pins as `-1` for the nominal test.
- Recovery: the default C6-backed P4 image was encrypted-flashed back to `/dev/ttyACM0`, without full erase or separate read-back. Boot returned to `p4-hosted>`; `c6_zigbee_status` reports `available=yes`, `stack_ready=yes`, `network_formed=no`, `search_pending=no`, `devices=0`, `last_error=0`, BDB `0x0100/SUCCESS`. Recovery image SHA-256: `baa2b3cc9157b33ee4bb823ed8b871b82e822eaf476bc3a06f905af67d4030b2`.
- C6 firmware and NVS were not changed. Current runtime is P4/C6-backed; H2 remains flashed with the 230400-baud standard RCP but is unused. No Zigbee network was formed and no device joined.

## Acceptance still required

1. Recheck the Grove TX/RX crossover at both connectors against M5Stack's H2 UART0 TX GPIO24/RX GPIO23 and P4 UART1 TX GPIO26/RX GPIO27 mappings.
2. Resolve why the M5Stack-documented `ot_rcp` image produces no Spinel response at either 460800 or 230400 baud; capture a successful RCP version handshake before network commissioning.
3. Implement a non-crashing RCP failure/recovery path compatible with M5Stack's nominal no-reset-wire setup; the current callback logs the timeout, but IDF's `SpinelDriver::ResetCoprocessor()` still exits the P4 application.
4. After handshake succeeds, disable C6 native Zigbee with an explicitly approved C6 Wi-Fi-only image, preserve C6 Zigbee NVS, then approve creation of a new H2 network, join one test device and check normal-reboot persistence.