# Development and validation guide

This repository contains host-testable domain components and an installed P4/C6 runtime. A board-specific P4 H2 host and H2 RCP were flashed on the test board; the RCP app starts, but the UART Spinel handshake times out. P4 is restored to the default C6-backed profile. No H2 Zigbee commissioning has been validated.

## Workstation

Prefer Ubuntu Linux, ESP-IDF official toolchain and documented Python/USB permissions. Compile P4 and H2 separately; do not attempt to flash the H2 with an esp32p4 target.

```bash
git clone https://github.com/RobertRupa/ESP32-P4-Multi-HUB.git
cd ESP32-P4-Multi-HUB
git status --short
# P4 compile probe (no flashing):
# source ~/esp/esp-idf/export.sh
# cd firmware/p4 && idf.py set-target esp32p4 && idf.py build
# C6 ESP-Hosted co-processor target (no flashing):
# cd firmware/c6 && idf.py set-target esp32c6 && idf.py build
# H2 standard UART Spinel RCP (build only; no flash):
# cd ../h2 && idf.py -B /tmp/hub-h2 -D SDKCONFIG=/tmp/hub-h2/sdkconfig set-target esp32h2
# idf.py -B /tmp/hub-h2 -D SDKCONFIG=/tmp/hub-h2/sdkconfig build
# P4 generic H2 Zigbee coordinator profile (build only; pins remain unset):
# cd ../p4 && idf.py -B /tmp/hub-p4-h2 -D SDKCONFIG=/tmp/hub-p4-h2/sdkconfig \
#   -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.h2-zigbee" build
# Board-specific Gateway H2 test profile (UART1 GPIO26/27; Spinel handshake currently times out):
# cd ../p4 && idf.py -B /tmp/hub-p4-h2-board -D SDKCONFIG=/tmp/hub-p4-h2-board/sdkconfig \
#   -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.h2-zigbee;sdkconfig.defaults.h2-jc4880p443c" build
# C6 Wi-Fi-only profile for H2 Zigbee ownership (build only):
# cd ../c6 && idf.py -B /tmp/hub-c6-h2-wifi -D SDKCONFIG=/tmp/hub-c6-h2-wifi/sdkconfig \
#   -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.h2-zigbee-wifi" build
```

For local P4 Web UI testing, `CONFIG_HUB_WEB_DEVELOPMENT_MODE=y` is set in the workstation's generated `firmware/p4/sdkconfig`: it provisions `admin/admin`, disables the forced password rotation, and uses 10,000 PBKDF2 iterations. The option's Kconfig default and shared defaults files keep it disabled for production builds. Do not commit generated `sdkconfig` or ship a development-mode image.

The P4 ESP-Hosted host target was built locally with ESP-IDF v5.5.5 at commit `b774170ff46c393eeb5e495ea37936038d3f4f4f` and the RISC-V GCC 14.2.0 toolchain. Its lockfile pins `espressif/esp_hosted` 3.1.0 and `espressif/esp_wifi_remote` 1.3.1. The generated configuration selects ESP32-C6, SDIO Slot 1, 4-bit mode, a 5 MHz initial clock, and the board-reference mapping (P4 CLK 18, CMD 19, D0-D3 14-17, reset 54). P4 revision 3.2 on `/dev/ttyACM0` now runs ESP-Hosted 3.1.0 with the matching integrated C6 firmware; the custom partition table preserves the staged OTA region. See the [P4 flash audit](audits/2026-11-07-p4-esp-hosted-flash.md) and [C6 OTA audit](audits/2026-11-07-c6-host-ota.md).

The C6 co-processor target was built locally with the same ESP-IDF v5.5.5 and GCC 14.2.0. It pins `espressif/esp_hosted` 3.1.0, targets `esp32c6`, enables Wi-Fi in co-processor role and uses the component's SDIO transport default. The 1.1 MB C6 image was staged in P4 flash, transferred over SDIO and accepted by the C6 OTA endpoint. After P4 restart, C6 reported ESP-Hosted 3.1.0 matching the host. The touchscreen scan/connect flow works on the Guition board; GT911 touch responds, DHCP succeeds, credentials save in the dedicated encrypted `wifi_cfg` NVS partition, and the station reconnects after reboot. The password is not included in source, docs or audit. The build still emits three upstream Kconfig warnings for the optional TinyUSB network-mode choice when TinyUSB dependencies are skipped. See the [C6 OTA audit](audits/2026-11-07-c6-host-ota.md) and [HMI/Wi-Fi audit](audits/2026-11-07-p4-hmi-wifi.md).

Do not install floating ESP-IDF/esp-matter dependencies or overwrite pre-existing SDK directories. Future `tools/bootstrap.sh` must check pinned versions and sha, report failed prerequisites and never flash by default.

For board-source provenance, use [HARDWARE.md](HARDWARE.md) and the [vendor board reference](BOARD_VENDOR_REFERENCE.md). The reference records vendor-document facts, not measurements on this unit; listing images in [IMAGE_SOURCES.md](IMAGE_SOURCES.md) are unverified and must not be used as pinout evidence. Do not redistribute the confidential OV02C10 datasheet.

## Source checkout policy

- Lock ESP-IDF, ESP-Matter, ESP Zigbee SDK and H2 RCP revision IDs/tags in one manifest before first build.
- Maintain separate sdkconfig.defaults for host and radio; never commit generated `sdkconfig` with machine secrets.
- Prefer native `idf_component.yml` and component manager; preserve licenses and attribution.
- Use SDK binaries and source builds compatible with board P4 chip revision, not assumptions copied from S3.

## First bring-up checklist

1. Inspect vendor PCB revision/schematic, record P4 chip revision, `esptool flash_id`, PSRAM test and USB port functions.
2. Verify ST7701S reset/backlight/DSI LDO sequence and GT911 touch addresses/pins.
3. Verify C6 link over SDIO, DHCP, IPv6 and mDNS; test reboot and repeated reconnection.
4. Validate microSD power LDO4 and SDMMC separate from C6 SDIO.
5. Record the user-confirmed H2 Grove wiring, including TX/RX crossover to P4 UART1 GPIO26/27 and H2 UART0 GPIO24/23. MAX485 shares P4 UART1 nets; keep the board-specific profile separate from shared defaults.
6. Resolve the current Spinel timeout with physical serial checks or a compatible RCP/host version; verify the IDF reset-failure behavior before attempting commissioning again.
7. Test a new Zigbee network and one real end-device on H2; do not claim P4/Zigbee support based only on target-library compilation.

## CI strategy

- Markdown link/static lint, source formatting, SPDX/license scanning.
- Host-native tests for capability normalization, command state machine and registry migrations with mocked Zigbee/Matter adapters.
- P4 C6-default, P4 H2-RCP, H2 RCP, C6 Wi-Fi-only and C6 Zigbee opt-in compile profiles use pinned ESP-IDF/component dependencies; keep build evidence separate from hardware claims.
- Physical radio/matter interoperability tests marked **manual/hardware-in-loop**; CI must never imply they passed.

## Debug evidence template

```text
Date, board revision, P4 silicon revision:
ESP-IDF/ESP-Matter/Zigbee SDK commit hashes:
Firmware image hashes (P4/C6/H2):
UART wiring and voltage measurements:
Exact build command and exit code:
Runtime log snippet with all secrets removed:
Reproducer / expected / actual / conclusion:
```
