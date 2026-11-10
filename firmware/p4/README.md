# ESP32-P4 ESP-Hosted host target

This target provides the P4 host used by both supported compositions: P4+C6
and P4+C6+H2. The default profile selects the C6 Peer Data Zigbee bridge; the
board-specific H2 profile selects the P4 Zigbee host over UART Spinel while
retaining C6 ESP-Hosted Wi-Fi. Wi-Fi credentials are not stored in the
repository.

## Guition SDIO configuration

The host defaults use the board reference's documented P4 SDIO map: Slot 1,
four data lines, CLK GPIO18, CMD GPIO19, D0-D3 GPIO14-17, and C6 reset GPIO54.
The initial clock is 5 MHz for bring-up. These are schematic-derived project
settings, not a continuity test or a claim that the link was exercised on the
physical board. Confirm the PCB revision, shared nets and pull-ups before any
hardware run.

## Build

Use ESP-IDF v5.5 or later, then run from this directory:

```bash
idf.py set-target esp32p4
idf.py build
```

The default build is the P4+C6 target. For P4+C6+H2, see the isolated profile
commands in `docs/DEVELOPMENT.md`. Builds do not authorize flashing. The H2
board profile uses the verified test wiring UART1 GPIO26/27 at 230400 baud.

## Temporary H2 UART diagnostic

The opt-in application entry point skips project-owned Wi-Fi and Zigbee startup.
The ESP-Hosted component still auto-initializes the onboard C6 over SDIO in this
build; that path does not use UART1. The diagnostic sends source-tagged PING
frames every 500 ms over UART1 GPIO26 TX / GPIO27 RX at 230400 8N1. It prints
received bytes in hex, distinguishes P4's own local echo from H2 PING/ACK frames,
ACKs H2 PINGs, and reports periodic zero-byte RX counters over USB Serial/JTAG.
UART1 shares the board's MAX485 signals; the profile does not select RS-485
half-duplex mode or drive an external DE/~RE pin. Do not run the transceiver
concurrently. This diagnostic tests byte transport only, not the Spinel RCP
handshake.

Build in an isolated directory from this project directory:

```bash
source ~/esp/esp-idf/export.sh
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.uart-diagnostic" \
	idf.py -B build-uart-diagnostic \
	-D SDKCONFIG="$PWD/build-uart-diagnostic/sdkconfig" set-target esp32p4
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32p4;sdkconfig.defaults.uart-diagnostic" \
	idf.py -B build-uart-diagnostic \
	-D SDKCONFIG="$PWD/build-uart-diagnostic/sdkconfig" build
```

This temporary diagnostic profile is not a product target. Do not select it
for deployment; after a byte test, rebuild one of the two supported profiles.

## Local build result

The board-specific H2 profile builds with ESP-IDF v5.5.5 and has been
encrypted-flashed to P4 revision 3.2. Runtime verifies H2 Spinel reset and BDB
readiness on the existing network; C6 ESP-Hosted 3.1.0 and Wi-Fi/DHCP also
start. See the [H2 migration audit](../../docs/audits/2026-11-08-h2-zigbee-migration.md).