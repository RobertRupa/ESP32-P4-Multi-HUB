# ESP32-H2 Zigbee RCP

This target builds a dedicated IEEE 802.15.4 radio RCP for the P4-hosted ESP-Zigbee coordinator. It does not form a Zigbee network by itself and does not provide a Thread network. The Zigbee coordinator state and commissioning policy belong to P4.

## Build

Use the repository's pinned ESP-IDF v5.5.5 environment:

```sh
idf.py set-target esp32h2
idf.py build
```

The RCP uses standard UART Spinel at 230400 baud on H2 UART0, explicitly routed to GPIO24 TX / GPIO23 RX. These are module TX0/RX0 pads 31/30 in the user-supplied H2-MINI-1 pin diagram. This reduced rate follows M5Stack's Gateway H2 Grove guidance for links that do not work reliably at 460800 baud. System logs use USB Serial/JTAG, leaving UART0 dedicated to Spinel. The P4-side UART uses a separate UART instance and explicit pin configuration. No H2 reset/boot wire is configured by this firmware.

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

## Hardware status

- The standard H2 RCP image is installed on `/dev/ttyACM1`; boot logs report the RCP application started.
- The P4 board profile on UART1 GPIO26/27 at 230400 baud successfully resets the RCP over Spinel and initializes BDB. BDB reports an existing joined network.
- No fresh network formation or device join is claimed. P4 interview succeeds for `0x1a02`; `0x8362` still times out. Level command readback is implemented in the P4 host, but physical actuation with this latest path is not yet verified.
- The Grove link has no H2 reset/boot wire. UART1 shares the board's MAX485 nets; do not enable RS485 concurrently.