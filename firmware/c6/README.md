# ESP32-C6 ESP-Hosted co-processor target

This ESP-IDF target supplies the integrated C6 firmware used by the two system
compositions. P4+C6 enables the native C6 Zigbee coordinator over Peer Data.
P4+C6+H2 uses a separate Wi-Fi-only C6 profile and moves Zigbee ownership to
the external H2 RCP. Both use ESP-Hosted 3.1.0 over SDIO RPC v2.

## Build

Use ESP-IDF v5.5 or later, then run:

```bash
idf.py set-target esp32c6
idf.py build
```

Run these commands from `firmware/c6`. The component manager resolves the
exact `espressif/esp_hosted` version declared in `main/idf_component.yml`.

## P4+C6+H2 Wi-Fi-Only Profile

When Zigbee coordinator ownership is on the P4 through the external H2 RCP, use
an isolated C6 build directory. This profile retains ESP-Hosted Wi-Fi and
disables the C6-native Zigbee coordinator and Peer Data RPC;
it does not alter the active root `sdkconfig`.

```sh
source ~/esp/esp-idf/export.sh
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.h2-zigbee-wifi" \
	idf.py -B build-h2-zigbee-wifi \
	-D SDKCONFIG="$PWD/build-h2-zigbee-wifi/sdkconfig" set-target esp32c6
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.h2-zigbee-wifi" \
	idf.py -B build-h2-zigbee-wifi \
	-D SDKCONFIG="$PWD/build-h2-zigbee-wifi/sdkconfig" build
```

Verify the generated `sdkconfig` has `CONFIG_ESP_HOSTED_CP_FEAT_WIFI=y`,
`CONFIG_HUB_C6_ZIGBEE_COORD_ENABLE` unset,
`CONFIG_ESP_HOSTED_CP_FEAT_PEER_DATA` unset. A successful build is not flash
authorization; confirm the selected image and port before writing the
integrated C6. This profile builds successfully but has not been installed.

## Flashing restrictions

Building does not authorize flashing the integrated C6. The project currently
uses the upstream example's 4 MB flash and OTA partition layout. Verify the
physical C6 flash capacity, board revision, selected image and serial port
before any flash operation. Do not use `erase-flash` as part of routine setup.

The upstream ESP-Hosted-MCU source is [pinned to v3.1.0](https://github.com/espressif/esp-hosted-mcu/tree/235aca95b52e3501b55eeb610b0275d288edc8c2/examples/wifi/sta/cp).
Guition-specific SDIO wiring and host configuration must be checked against
the board reference; Function-EV-Board GPIO settings are not applicable by
assumption.

## Local build result

The P4+C6 native-Zigbee and P4+C6+H2 Wi-Fi-only C6 profiles both build with
ESP-IDF v5.5.5. The Wi-Fi-only image has not been flashed. ESP-Hosted 3.1.0
reports three upstream Kconfig warnings for an optional TinyUSB network-mode
choice when TinyUSB dependencies are skipped.