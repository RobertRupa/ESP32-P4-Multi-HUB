# ESP32-P4 ESP-Hosted host target

This target compiles the P4 side of the integrated C6 Wi-Fi coprocessor link.
It uses the same ESP-Hosted 3.1.0 protocol as `firmware/c6`, plus
`esp_wifi_remote` 1.3.1 for ESP-IDF 5.5. Wi-Fi credentials are not stored in
the repository; this build-only smoke application initializes ESP-Hosted and
queries the connected coprocessor identity.

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

The build command does not flash either chip. A successful build does not prove
a C6 handshake; that requires the matching C6 image and a physical test with
the selected serial/recovery path.

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

Do not flash until the exact P4 port and temporary image are explicitly selected.
After testing, restore the normal C6-backed runtime image.

## Local build result

Built with ESP-IDF v5.5.5, RISC-V GCC 14.2.0, ESP-Hosted 3.1.0 and
`esp_wifi_remote` 1.3.1. The updated image was flashed to P4 revision 3.2 with a
16 MB flash header. Runtime discovered C6 2.3.2 over SDIO and reported the host
3.1.0 major-version mismatch. Wi-Fi association was not tested. Three upstream
optional TinyUSB Kconfig warnings remain. See [the flash audit](../../docs/audits/2026-11-07-p4-esp-hosted-flash.md).