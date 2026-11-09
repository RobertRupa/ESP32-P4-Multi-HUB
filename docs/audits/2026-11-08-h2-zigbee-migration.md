# P4 + C6 + H2 Target Audit

- Date: 2026-11-08
- Scope: P4 Zigbee host over the external H2 UART Spinel RCP; C6 provides ESP-Hosted Wi-Fi only in the target profile.
- Toolchain: ESP-IDF v5.5.5, RISC-V GCC 14.2.0.

## Target profiles

1. **P4+C6:** C6 owns native Zigbee over ESP-Hosted Peer Data/SDIO. P4 and C6 builds pass; commissioning is not currently claimed as validated.
2. **P4+C6+H2:** P4 owns Zigbee; H2 is the standard UART Spinel RCP; C6 is Wi-Fi-only. P4/H2 runtime is verified. The matching C6 Wi-Fi-only profile builds but has not been installed.

C6 multipan and Thread/Zigbee multipan are not product targets. No image or runtime path claims Zigbee devices join Thread.

## Build and image

- P4 defaults: `sdkconfig.defaults`, `sdkconfig.defaults.esp32p4`, `sdkconfig.defaults.h2-zigbee`, `sdkconfig.defaults.h2-jc4880p443c`.
- Effective P4 config: `CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE=y`, UART1 TX/RX GPIO26/27, 230400 baud; multipan is not a selectable profile.
- Final P4 image: `/tmp/p4-h2-host-20261108/hub_p4_esp_hosted_host.bin`, 1,750,048 bytes, SHA-256 `dd6639eae1789e69c2835afa16f30cec98be12e4ef6202fa0b9524eb89d61f37`.
- P4 build command: `idf.py -B /tmp/p4-h2-host-20261108 -D SDKCONFIG=/tmp/p4-h2-host-20261108/sdkconfig build`; result: success. ESP-Hosted emits three upstream optional TinyUSB Kconfig warnings.
- The image was written only to P4 `/dev/ttyACM0` with `idf.py -B /tmp/p4-h2-host-20261108 -p /dev/ttyACM0 encrypted-flash`. The flash operation preserved NVS and did not erase the chip, program eFuses, or write C6/H2 flash.
- H2 remains on its previously installed standard RCP image at 230400 baud. C6 firmware/NVS are unchanged. The C6 Wi-Fi-only build disables native C6 Zigbee and Peer Data but has not been flashed.

## Hardware results

- P4 revision 3.2 boots with Flash Encryption enabled; 32 MB PSRAM passes its memory test. ESP-Hosted/C6 3.1.0 reports a match, SDIO initializes, and Wi-Fi receives `192.168.69.17`.
- H2 Spinel UART enables at 230400 baud; P4 logs `RESET_POWER_ON` and successful co-processor software reset. BDB initializes with `ESP_OK` and reports `factory_new=no`, `joined=yes`, `ready=yes` for the existing persisted network.
- Read-only `/api/zigbee/status` reports `network_formed=true`, `device_count=2`, and short addresses `0x8362`, `0x1a02`.
- Device `0x1a02` interview succeeds: endpoint 1 advertises On/Off `0x0006` and Level Control `0x0008`; endpoint 242 is Green Power. Device `0x8362` still times out during Active Endpoints interview (`ESP_ERR_TIMEOUT`, 263).
- Authenticated read-only `POST /api/zigbee/device` `operation=read_level` followed by GET reports `level_known=true`, `level_value=3` for `0x1a02` endpoint 1. This verifies CurrentLevel readback, not actuation.
- User-approved tests sent target `93` to `0x1a02` endpoint 1 using both Level cluster command `0x04` (`MoveToLevelWithOnOff`) and command `0x00` (`MoveToLevel` with `ExecuteIfOff=1`). Both received Default Response status `0x00`, but CurrentLevel remained `3`. After separate approval, one On command received Default Response `0x00` and readback reported `OnOff=true`; one Level 93 command while On again left CurrentLevel at `3`. No retries were sent. The final image serializes attribute reads per endpoint. After its P4-only reboot, read-only status reported CurrentLevel `3`; a subsequent On/Off read reported `false`. No write was sent after that reboot. The transport, coordinator, interview, command API, and ZCL Default Response path are verified; physical level change is not.
- No new Zigbee network was created and no join window was opened. The existing network and saved short addresses were preserved.

## Remaining limitations

- C6 Wi-Fi-only firmware is build-validated, not installed. Do not treat the H2 profile as a complete three-chip deployment until the exact C6 image is explicitly selected and installed.
- `0x8362` interview still times out; its clusters/capabilities are unknown.
- Both Level command variants are accepted by ZCL (`status=0x00`), but CurrentLevel remained 3 even after On/Off readback reported `true`.
- No Matter Bridge, Thread network, Home Assistant discovery, fresh Zigbee join, or RF coexistence test is claimed.
