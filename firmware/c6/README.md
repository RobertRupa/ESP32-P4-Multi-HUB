# ESP32-C6 ESP-Hosted co-processor target

This is a build-only ESP-IDF target for the board's integrated ESP32-C6. It uses
Espressif ESP-Hosted 3.1.0 in co-processor role with Wi-Fi over the default SDIO
transport and RPC v2. The P4 host must use a matching ESP-Hosted version and
transport configuration before the two images can communicate.

## Build

Use ESP-IDF v5.5 or later, then run:

```bash
idf.py set-target esp32c6
idf.py build
```

Run these commands from `firmware/c6`. The component manager resolves the
exact `espressif/esp_hosted` version declared in `main/idf_component.yml`.

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

Built with ESP-IDF v5.5.5 and RISC-V GCC 14.2.0. The build succeeds and
produces the C6 co-processor image. ESP-Hosted 3.1.0 reports three Kconfig
warnings for an optional TinyUSB network-mode choice when TinyUSB dependencies
are skipped. The generated image has not been flashed or tested against the P4.