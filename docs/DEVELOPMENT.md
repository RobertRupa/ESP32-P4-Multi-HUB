# Development and validation guide

The repository supports two system targets only: **P4+C6** and **P4+C6+H2**. Use ESP-IDF v5.5.5 and isolated build directories under `/tmp` so builds do not overwrite tracked outputs.

## P4 + C6

P4 application with the C6 native Zigbee coordinator over ESP-Hosted Peer Data/SDIO:

```sh
source ~/esp/esp-idf/export.sh
cd firmware/p4
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32p4" \
  idf.py -B /tmp/hub-p4-c6 -D SDKCONFIG=/tmp/hub-p4-c6/sdkconfig set-target esp32p4
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32p4" \
  idf.py -B /tmp/hub-p4-c6 -D SDKCONFIG=/tmp/hub-p4-c6/sdkconfig build
cd ../c6
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6" \
  idf.py -B /tmp/hub-c6-zigbee -D SDKCONFIG=/tmp/hub-c6-zigbee/sdkconfig set-target esp32c6
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6" \
  idf.py -B /tmp/hub-c6-zigbee -D SDKCONFIG=/tmp/hub-c6-zigbee/sdkconfig build
```

## P4 + C6 + H2

P4 hosts Zigbee through the H2 UART Spinel RCP; C6 is Wi-Fi-only. The C6 Wi-Fi-only image is build-validated but not installed:

```sh
cd firmware/p4
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.h2-zigbee;sdkconfig.defaults.h2-jc4880p443c" \
  idf.py -B /tmp/hub-p4-h2 -D SDKCONFIG=/tmp/hub-p4-h2/sdkconfig set-target esp32p4
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.h2-zigbee;sdkconfig.defaults.h2-jc4880p443c" \
  idf.py -B /tmp/hub-p4-h2 -D SDKCONFIG=/tmp/hub-p4-h2/sdkconfig build
cd ../c6
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.h2-zigbee-wifi" \
  idf.py -B /tmp/hub-c6-wifi -D SDKCONFIG=/tmp/hub-c6-wifi/sdkconfig set-target esp32c6
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.h2-zigbee-wifi" \
  idf.py -B /tmp/hub-c6-wifi -D SDKCONFIG=/tmp/hub-c6-wifi/sdkconfig build
cd ../h2
idf.py -B /tmp/hub-h2 -D SDKCONFIG=/tmp/hub-h2/sdkconfig set-target esp32h2
idf.py -B /tmp/hub-h2 -D SDKCONFIG=/tmp/hub-h2/sdkconfig build
```

The board profile uses the verified test wiring P4 UART1 GPIO26/27 at 230400 baud; the H2 RCP uses UART0 TX GPIO24/RX GPIO23. No H2 reset/boot wire is present. P4/H2 Spinel reset and BDB readiness on an existing joined network are verified. Interview succeeds for `0x1a02`; `0x8362` still times out. No new network or device join is claimed.

## Validation and flashing

Host tests for the Level and On/Off state policies:

```sh
cmake -S tests/zigbee_level_state -B /tmp/hub-zigbee-level-state
cmake --build /tmp/hub-zigbee-level-state
ctest --test-dir /tmp/hub-zigbee-level-state --output-on-failure
cmake -S tests/zigbee_on_off_state -B /tmp/hub-zigbee-on-off-state
cmake --build /tmp/hub-zigbee-on-off-state
ctest --test-dir /tmp/hub-zigbee-on-off-state --output-on-failure
```

Build does not authorize flashing. Use encrypted P4 flashing on this board and preserve NVS. Do not flash C6 or H2 unless the exact image and port are explicitly selected. Never use `erase-flash` or program eFuses as routine setup.

The generated workstation `firmware/p4/sdkconfig` enables development credentials `admin/admin`; do not commit or ship that configuration. The Web UI is HTTP-only and must be used only on a trusted isolated LAN. Keep credentials and network secrets out of source, docs and logs.

The repository uses pinned ESP-IDF/component manifests and lockfiles. Do not install floating dependencies or overwrite an existing SDK. Hardware results are documented in the [H2 migration audit](audits/2026-11-08-h2-zigbee-migration.md), [HMI/Wi-Fi audit](audits/2026-11-07-p4-hmi-wifi.md), and [C6 OTA audit](audits/2026-11-07-c6-host-ota.md).