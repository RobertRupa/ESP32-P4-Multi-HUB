# ESP32-H2 Zigbee RCP

This target builds a dedicated IEEE 802.15.4 radio RCP for the P4-hosted ESP-Zigbee coordinator. It does not form a Zigbee network by itself and does not provide a Thread network. The Zigbee coordinator state and commissioning policy belong to P4.

## Build

Use the repository's pinned ESP-IDF v5.5.5 environment:

```sh
idf.py set-target esp32h2
idf.py build
```

The RCP uses standard UART Spinel at 230400 baud on H2 UART0 with the SoC's default UART pins (GPIO24 TX, GPIO23 RX). This reduced rate follows M5Stack's Gateway H2 Grove guidance for links that do not work reliably at 460800 baud. System logs use USB Serial/JTAG, leaving UART0 dedicated to Spinel. The P4-side UART uses a separate UART instance and requires explicit pin configuration. Do not infer H2 reset/boot wiring from this firmware.

## Temporary UART diagnostic

The opt-in diagnostic profile bypasses RCP startup and sends source-tagged,
sequence-numbered PING frames on UART0 at 230400 8N1. It reports local TX/RX
loopback, P4 PINGs and P4 ACKs over USB Serial/JTAG. A matching ACK is expected
within 250 ms. Use a TX/RX short only for an isolated H2 loopback test; remove it
when H2 remains connected to P4, or it can tie the crossed P4 TX/RX paths
together. The normal RCP profile remains the default. The shared frame format
and host test are in `firmware/common` and `tests/uart_diagnostic_protocol`.

Build in an isolated directory from this project directory:

```sh
source ~/esp/esp-idf/export.sh
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.uart-diagnostic" \
	idf.py -B build-uart-diagnostic \
	-D SDKCONFIG="$PWD/build-uart-diagnostic/sdkconfig" set-target esp32h2
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.uart-diagnostic" \
	idf.py -B build-uart-diagnostic \
	-D SDKCONFIG="$PWD/build-uart-diagnostic/sdkconfig" build
```

Do not flash until the exact H2 port and temporary image are explicitly selected.
The diagnostic image is not a Zigbee RCP and must be replaced with the normal
RCP image after the byte-level test.

## Limitations

- The P4 build currently compiles a separate ESP-Zigbee UART-RCP coordinator profile, but no host/RCP handshake has been verified.
- Board documentation reserves P4 GPIO26/27 for the shared UART1/MAX485 nets. Confirm the matching schematic revision and transceiver isolation before enabling the P4 profile.
- No Zigbee network formation, end-device join, persistence, or RF coexistence is claimed by a successful build.