# P4 ESP-Hosted Host Flash Audit

## Authorization and target

- Date: 2026-11-07
- Operation: initial ESP-Hosted host bring-up image on ESP32-P4
- User authorization: user selected the P4 ESP-Hosted host image and `/dev/ttyACM0` in this session
- Target: ESP32-P4, revision 3.2 (reported by esptool)
- Port: `/dev/ttyACM0`
- Read-only flash probe: 16 MB detected with `python -m esptool --chip esp32p4 -p /dev/ttyACM0 flash_id`
- Device MAC: intentionally not recorded

## Image

- Path: `firmware/p4/build/hub_p4_esp_hosted_host.bin`
- SHA-256: `35e0d56dec7c927cb427adfd04eb4b5c65f856063b94c4a9eab23bfac88fd3a7`
- Size: 556032 bytes
- Build target: `esp32p4`
- ESP-IDF: v5.5.5, commit `b774170ff46c393eeb5e495ea37936038d3f4f4f`
- Command: `idf.py -p /dev/ttyACM0 flash` from `firmware/p4`

## Scope and limitations

- The flash operation writes the P4 bootloader, partition table and application. It does not issue `erase-flash`.
- The integrated C6 and external H2 are not flash targets in this operation.
- The flashed host image uses a 2 MB flash header and a 1 MB application partition, although the read-only probe detected 16 MB. The image booted, but runtime warns that detected flash is larger than the image header. Rebuild with the verified 16 MB setting before using the remaining capacity or preparing a production image.
- The C6 co-processor image is a separate build target and was not flashed. The existing C6 firmware reports version 2.3.2; the P4 host is 3.1.0 and logs a major-version mismatch with a recommendation to update the coprocessor. No C6 image was selected or written.
- P4 SDIO/reset settings from the supplied board reference established a runtime SDIO link on this unit. No separate continuity/voltage measurements were recorded.

## Result

- Flash command result: success. Esptool verified the bootloader, application and partition table hashes; no `erase-flash` was issued.
- Boot/runtime result: P4 revision 3.2 booted the ESP-IDF v5.5.5 host application. Runtime reported 16 MB detected flash versus the configured 2 MB image header.
- C6 handshake result: SDIO initialization succeeded at 5 MHz; C6 chip ID `0x0d` was recognized, WLAN/HCI over SDIO and RPC v2 were reported. C6 firmware 2.3.2 is major-version-incompatible with host ESP-Hosted 3.1.0. No Wi-Fi AP association was tested.
- The first flashed image's `esp_hosted_get_cp_info()` log showed empty target/chip fields for this older CP firmware; the init event provided chip ID evidence. The second flashed image logs CP firmware version and treats absent target metadata as unavailable.
- Updated, not-flashed candidate: `firmware/p4/build/hub_p4_esp_hosted_host.bin`, SHA-256 `f325fb9571833a3d343aff90cc908e74f90b3b7d94b00230ac32ecacc3ac2405`.
- Follow-up: rebuild with corrected P4 flash-size configuration; select and explicitly authorize a matching C6 image/port before any C6 update; retest host/CP version compatibility and Wi-Fi association. Do not treat this smoke test as gateway acceptance.

## Follow-up P4 image update

- Authorization: user requested flashing and continuing the update; target remains the explicitly selected P4 and `/dev/ttyACM0`.
- Image: `firmware/p4/build/hub_p4_esp_hosted_host.bin`
- SHA-256: `50037fc7a8b82736a2335165ef0d74d9d817c584fc806483a94c1ab74bd2a0e1`
- Configuration: 16 MB flash header, existing single 1 MB factory application partition.
- Command: `idf.py -p /dev/ttyACM0 flash` from `firmware/p4`; no `erase-flash`.
- Result: success. Esptool verified the bootloader, application and partition table hashes; no `erase-flash` was issued.
- Runtime: boot log reports ESP32-P4 revision 3.2 and 16 MB flash. SDIO initializes at 5 MHz and discovers C6 2.3.2 with WLAN/HCI over SDIO and RPC v2. The host still reports a major-version mismatch; Wi-Fi association was not tested.

## IDF 6.2 Wi-Fi-only P4 update

- Authorization: user confirmed the exact P4 Wi-Fi-only image and `/dev/ttyACM0`. Multipan was disabled in the flashed image; C6 and H2 were not selected as flash targets.
- Image: `/tmp/hub_p4_product_idf62_20261107/build_wifi_only_p4/hub_p4_esp_hosted_host.bin`; SHA-256 `4ffc63b4c1a941413016467c846993bbcfd16d58ef6a869dd6faa2edd92d71f4`; 1,188,192 bytes. ESP-IDF commit `0e6b480f858c69862ffb6001d96e9eef74599a6e` (reported v6.2.0); `CONFIG_HUB_MULTIPAN_ENABLE=n`.
- The first `idf.py ... flash` wrote plaintext to this Flash-Encryption-enabled unit. Boot ROM then reported `invalid header` and watchdog resets. Recovery used the same image and port with ESP-IDF `encrypted-flash`, which supplied `--encrypt`; no `erase-flash` or eFuse programming command was run. The encrypted write command cannot verify flash contents directly, so boot and application status were checked afterward.
- The encrypted write targeted bootloader at `0x2000`, partition table at `0x11000`, and factory app at `0x20000`. The existing table still reports `nvs` at `0x12000`, `phy_init` at `0x18000`, `c6_fw` at `0x410000`, `wifi_keys` at `0x610000`, and `wifi_cfg` at `0x611000`. No C6 image was staged into `c6_fw`; no NVS partition was explicitly written or erased.
- Boot result: ESP32-P4 revision 3.2 launched IDF commit `0e6b480f`; Flash Encryption reported enabled; 32 MB PSRAM passed its memory test. ESP-Hosted negotiated RPC v2 and host/C6 versions `3.1.0` / `3.1.0` matched. Wi-Fi reconnected to `192.168.69.17` with credentials saved; read-only `/api/status` reported `connected`, `flash_encryption=true`, RSSI `-70 dBm`, free PSRAM `32012 KB`, and internal heap `247 KB`. Multipan status was `disabled` as intended.
- The staged `/tmp/hub-c6-staged.bin` is an ESP-IDF v5.5.5 C6 image targeting ESP-Hosted CP 3.1.0. C6 OTA was not run: the running C6 already reports 3.1.0 and the staged OTA command refuses duplicate/downgrade versions. No C6 or H2 firmware was flashed. The multipan-enabled P4/C6 images remain build-only; the test P4 UART pins are unverified and shared with RS485.
- HMI driver and GT911 initialization appeared in boot logs, but no post-update visual/touch acceptance or 10-minute soak was performed. Zigbee, Thread, Matter and Home Assistant behavior remain unverified.

## IDF 6.2 C6 diagnostics and hash-bound OTA guard

- Authorization: user confirmed the exact P4 image and `/dev/ttyACM0`; flashed with ESP-IDF `encrypted-flash`. No `erase-flash`, C6 OTA or H2 operation was run.
- Image: `/tmp/hub_p4_product_idf62_20261107/build_wifi_only_p4/hub_p4_esp_hosted_host.bin`; SHA-256 `cf3a0db3598c392057c8ea56da9b9f2d346c153f4306db1f0b5845f158f30f5a`; 1,194,848 bytes. IDF commit `0e6b480f858c69862ffb6001d96e9eef74599a6e`; `CONFIG_HUB_MULTIPAN_ENABLE=n`.
- The encrypted write updated bootloader (`0x2000`), partition table (`0x11000`) and factory app (`0x20000`). Esptool cannot read-back verify encrypted writes; boot logs and runtime were checked. NVS and `c6_fw` were not written or erased.
- Runtime: P4 rev3.2 booted with Flash Encryption enabled, 32 MB PSRAM test passed, ESP-Hosted host/C6 versions matched at 3.1.0, Wi-Fi reconnected to `192.168.69.17`, and `/api/status` reported `connected`, credentials saved, and multipan disabled.
- `GET /api/c6/status` reported CP 3.1.0, chip ID `0x0d`, and the existing valid staged image `371b9ed-dirty`, 1,095,088 bytes, SHA-256 `df2e588902b3bf8985b2aafd5165a9ae48f70dc048a282d385670d20904b390d`; OTA audit was `not_run`, attempts 0. Serial command `c6_status` printed the same version, stage hash and audit state.
- The P4 source contains a separate token-bound same-version exception for one C6 multipan candidate: `c6_ota CONFIRM-C6-MULTIPAN-69F6E8C7D6E6CF00`, bound to full SHA-256 `69f6e8c7d6e6cf003986148fbcead6462654a9de4fbb74dbf355a999614410f4`. The normal OTA path still rejects duplicate/downgrade versions, and all newer running versions are rejected. This P4 app does not stage or transfer an image automatically.
- The C6 multipan candidate was staged at P4 `c6_fw` (`0x410000`) over `/dev/ttyACM0`; the existing 2 MiB staging partition was backed up first to `/tmp/c6-fw-backup-20261107.bin` (SHA-256 `319f58f498cc8b12781648d572f395d0f3d689a7d5fc3e3d48899e7cc7d731f2`). Candidate read-back SHA matched `69f6e8c7d6e6cf003986148fbcead6462654a9de4fbb74dbf355a999614410f4`.
- User-confirmed command `c6_ota CONFIRM-C6-MULTIPAN-69F6E8C7D6E6CF00` validated the exact image hash, transferred 1,312,720 bytes over ESP-Hosted SDIO, completed OTA, and restarted P4. On reboot, the CP reported 3.1.0 matching the P4 host and the OTA audit transitioned to `verified` (attempts 1, last error 0). `/api/c6/status` and serial `c6_status` both reported the same staged image SHA and verified audit.
- C6 image update is successful as a firmware transfer, but no RF/interoperability test was performed. P4 `multipan_state` remains `disabled`; no Zigbee discovery or Thread routing is active. P4 GPIO26/27 and C6 UART0/CN2 connectivity are still unverified, and H2 was not flashed.