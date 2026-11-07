# Development and validation guide

This repository contains a specification, **not a buildable firmware project** yet.

## Workstation

Prefer Ubuntu Linux, ESP-IDF official toolchain and documented Python/USB permissions. Compile P4 and H2 separately; do not attempt to flash the H2 with an esp32p4 target.

```bash
git clone https://github.com/RobertRupa/ESP32-P4-Multi-HUB.git
cd ESP32-P4-Multi-HUB
git status --short
# Following commands apply ONLY after firmware/p4 and firmware/h2 exist:
# source ~/esp/esp-idf/export.sh
# cd firmware/p4 && idf.py set-target esp32p4 && idf.py build
# cd ../h2 && idf.py set-target esp32h2 && idf.py build
```

Do not install floating ESP-IDF/esp-matter dependencies or overwrite pre-existing SDK directories. Future `tools/bootstrap.sh` must check pinned versions and sha, report failed prerequisites and never flash by default.

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
5. Identify P4 UART on expansion connector and two safe GPIOs for H2 reset/boot (no guessed physical pin numbers).
6. Flash H2 `ot_rcp` with verified UART mapping and compatible `OPENTHREAD_NCP_VENDOR_HOOK` if required; capture handshake and radio version.
7. Compile/port minimal Zigbee/Matter examples for P4 before declaring support.

## CI strategy

- Markdown link/static lint, source formatting, SPDX/license scanning.
- Host-native tests for capability normalization, command state machine and registry migrations with mocked Zigbee/Matter adapters.
- P4/H2 compile matrix only when real firmware components and pinned toolchain exist.
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
