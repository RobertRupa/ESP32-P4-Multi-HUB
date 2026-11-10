# JC4880P443C_I_W — vendor hardware reference

> Source-derived engineering notes for the **JC4880P443C_I_W / JC4880P443C_I_W_Y** board. These are documented board facts, **not measurements on the project device**. Match board revision and PCB markings before applying pin assignments. The external ESP32-H2 Zigbee RCP remains a proposed addition, not part of this schematic.

## Source inventory and provenance

1. `JC4880P443C_I_W Specifications-EN-V1.0.pdf` — Jingcai/JCZN, 7 pages; product specifications, connector legend (p. 5), mechanical drawings (pp. 6–7).
2. `JC4880P443_V1.0.pdf` — one-page multi-block board schematic; title block references `JC4880P443_V0.2.SchDoc`, dated 2025-09-27. Revision/name mismatch must be checked against actual PCB.
3. `JC4880P443C_I_W_Y.dwg` — mechanical CAD drawing; compare mechanically with W vs W_Y enclosure variant before machining.
4. `Getting started JC4880P443C_I_W.pdf` — 12-page Arduino introduction. Its introduction names **JC1060P470**, so treat it as shared/generic vendor instructions rather than guaranteed board-specific settings.
5. `OV02C10_CSP_DS_2.01_Waching.pdf` — OmniVision OV02C10 sensor datasheet v2.01 (July 2022), marked **Confidential for Waching Only**. **Do not redistribute the original PDF or its internal figures** in this public repository. Only short factual integration notes are included.
6. `JC4880P443C_I_W.zip` — vendor archive containing Arduino/LVGL and ESP-IDF sample trees, documentation and diagrams. Vendor download: [JC4880P443C_I_W.zip](https://pan.jczn1688.com/directlink/1/HMI%20display/JC4880P443C_I_W.zip). Archive examples are references, not CI-tested project code.

Source hierarchy: matching revision schematic + physical validation > vendor example for that revision > vendor marketing specification > community notes. Keep contradictions explicit.

## Board and display specification (vendor)

| Property | Documented value | Source/qualification |
|---|---|---|
| Board | JC4880P443C_I_W (no shell); JC4880P443C_I_W_Y (with shell) | spec p. 4 |
| Application CPU | ESP32-P4 (vendor lists up to 360 MHz for module) | spec p. 3; verify installed silicon |
| Radio | separate ESP32-C6 for Wi-Fi/Bluetooth via ESP-Hosted | schematic/module, getting-started p. 2; P4 itself has no wireless radio |
| Memory | 32 MB PSRAM; 16 MB flash | spec p. 3; probe physically |
| Display | 4.3-inch IPS, 480×800, 24-bit color, ST7701S, MIPI DSI | spec pp. 3–4; schematic DSI connector |
| Touch | capacitive, GT911 per vendor LVGL example | example-derived; scan address and reset GPIO on target |
| Supply/current | 5 V, approximately 320 mA | spec p. 4; not a worst-case budget |
| Module outline | 117.01 × 69.41 mm; active area 93.60 × 56.16 mm | spec pp. 4, 7 |
| Temperatures | operating −20 to +70 °C; storage −30 to +80 °C | spec p. 4 |
| Storage | TF/microSD card socket | spec p. 3, schematic |
| Interfaces | full-speed and high-speed USB-C, camera CSI, RS485, UART0, UART1, I2C, microphone, speaker and battery connector | spec p. 5 |
| Audio codec | ES8311; NS4150 amplifier in board schematic | schematic |
| Battery circuitry | IP5306 visible in schematic | battery supply/charger behavior and connector polarity require review |

The specification p. 6 depicts a front portrait form factor; p. 7 shows the mechanical outline and mounting details. **Drawing orientation does not dictate LVGL screen rotation.**

## Connector map — vendor legend (spec p. 5)

| Legend # | Function |
|---|---|
| 1 | Microphone |
| 2 | MX1.25 2-pin speaker |
| 3 | MX1.25 2-pin lithium battery |
| 4 | Full-speed USB Type-C |
| 5 | High-speed USB Type-C |
| 6 | 2×13-pin 2.54 mm expansion header |
| 7 | MX1.25 4-pin UART0 |
| 8 | Camera interface |
| 9 | MX1.25 4-pin UART1 |
| 10 | MX1.25 4-pin RS485 |
| 11 | HS1.0 2-pin I2C |
| 12 | ESP32-P4 + ESP32-C6 core board |

### Expansion header JP1 (2×13 pins): schematic net names

The following is a best-effort transcription of the **schematic's labeled JP1 nets**; JP1 pins 3–4 remain unresolved from extracted schematic text. It does not certify which P4 GPIOs are free or which board-side pin is physically pin 1. Confirm pin-1 marker and continuity on the real PCB.

| Odd pin | Net | Even pin | Net |
|---:|---|---:|---|
| 1 | VCC3V3 | 2 | VCC5V |
| 3 | Not resolved from extracted schematic | 4 | Not resolved from extracted schematic |
| 5 | GND | 6 | GND |
| 7 | GPIO52 | 8 | GPIO33 |
| 9 | GPIO51 | 10 | GPIO31 |
| 11 | GPIO50 | 12 | GPIO30 |
| 13 | GPIO49 | 14 | GPIO29 |
| 15 | GPIO35 | 16 | GND |
| 17 | GPIO34 | 18 | VCC3V3 |
| 19 | GPIO32 | 20 | C6_U0RXD |
| 21 | GPIO28 | 22 | C6_U0TXD |
| 23 | ES_I2C_SDA | 24 | C6_IO9 |
| 25 | ES_I2C_SCL | 26 | C6_CHIP_PU |

**Caution:** JP1 mixes P4 signals, onboard C6 control/UART, power rails and GND. Many apparent GPIOs are multiplexed with board features such as Ethernet RMII, SD/MMC and boot straps. GPIO49–52 may be used by the optional Ethernet RMII path. Do **not** select H2 reset/UART or drive external voltage on these nets until a full shared-net/boot check.

## Board-level signal inventory (schematic + vendor samples)

| Peripheral | Board nets / P4 GPIO | Implementation note |
|---|---|---|
| Touch and audio control I2C | ES_I2C_SDA = GPIO7; ES_I2C_SCL = GPIO8 | shared bus; GT911 example uses 7/8 |
| LCD backlight | LCD_PWM (reported GPIO23) | confirm polarity and PWM timer before enabling |
| LCD reset | reported GPIO5 | verify ST7701S initialization sequence on actual revision |
| I2S ES8311 | MCLK GPIO13, SCLK GPIO12, LRCK GPIO10, DSDIN GPIO9, ASDOUT/ES7210_SDOUT GPIO48 | verify driver direction and bus sharing |
| Speaker amplifier | PA_CTRL = GPIO11 | amplifier-enable line; do not use as spare GPIO |
| UART0 | UART0_TXD GPIO37, UART0_RXD GPIO38 | console/programming path |
| UART1 | TX1 GPIO26, RX1 GPIO27 | also connects to RS485 path; shared resources |
| RS485 | MAX485, TX1/RX1 and separate driver-enable logic | not independently available UART by default |
| MicroSD | SD_CLK GPIO43; SD_CMD GPIO44; SD_DATA0–3 GPIO39–42; SD supply control | preserve slot and power rail |
| Host–C6 link | dedicated SDIO/ESP-Hosted signals plus C6 reset/boot | do not repurpose without tracing |
| MIPI DSI | dedicated differential clock/data pairs + LCD FPC | not parallel RGB GPIO |
| MIPI CSI | 2 data pairs + clock pair + CSI control lines | interface available; sensor/module compatibility to verify |

The net inventory comes from the board schematic; the *GPIO23 backlight*, *GPIO5 LCD reset* and several host–C6 assignments were also documented as community-derived in `HARDWARE.md`, so keep them provisional until verified.

### Camera FPC: high-level signals

The schematic's 15-pin camera connector `FPC2` exposes `CSI_A_DATA0_P/N`, `CSI_A_DATA1_P/N`, `CSI_A_CLK_P/N`, `CSI_IO0`, `CSI_IO1`, `ES_I2C_SCL/SDA`, 3.3 V and GND. Its presence does **not** prove the OV02C10 module uses the same physical pinout or proper on-module voltage regulators. Confirm flex pitch, orientation, VDD routing and lens-module wiring before connecting a camera.

## OV02C10 — sensor integration facts (not board-camera proof)

The supplied OmniVision datasheet describes a **1/7.25-inch**, 2 MP (1920×1080) rolling-shutter sensor, up to 60 fps in documented modes, RAW8/RAW10 MIPI output with 1 or 2 lanes, and SCCB configuration with 16-bit register indices and 8-bit data. Nominal *sensor silicon* rails are AVDD **2.8 V**, DVDD **1.2 V**, and DOVDD **1.8 V**, with a **6–27 MHz** input clock. These are **not** the camera connector's safe externally supplied voltages.

SCCB SID=0 uses the 8-bit write ID 0x6C (**7-bit address 0x36**); SID=1 uses write ID 0x20 (**7-bit address 0x10**). The bus address depends on SID/module wiring and must be probed. The datasheet recommends stable power rails before releasing active-low `XSHUTDN`. All MIPI lane, reset, timing and driver selections require a module-level wiring check.

**Security/IP:** the supplied sensor datasheet carries an explicit redistribution restriction. Keep the PDF out of Git, public ZIP bundles, assets and AI-generated image reproductions. Record only these integration facts and a private reference to the document.

## Software reference and first bring-up

The getting-started guide recommends Arduino IDE ≥2.3.4, Espressif's ESP32 boards package, `ESP32P4 Dev Module`, and `esptool`. It shows LVGL/library samples; exact demo versions and `lv_conf.h` must match the selected vendor source. The guide's Wi-Fi demonstration uses the C6 coprocessor, since ESP32-P4 has no built-in Wi-Fi/BLE.

For production firmware prefer **pinned ESP-IDF** and independent ESP32-P4 host + ESP32-C6 ESP-Hosted radio images. The vendor archive includes ESP-IDF 5.5.4/6.0.2 sample material, but it is not a declaration that the hub builds with those SDKs.

Recommended hardware validation order:

1. Photograph PCB revision and pin-1 markings; record P4/C6 versions and flash/PSRAM probes.
2. Identify which Type-C connector is FS versus HS; record exact esptool port and boot/recovery behavior.
3. Verify 5 V rails, 3.3 V rails, battery connector polarity and optional battery operation.
4. Bring up ST7701S over DSI and control backlight; confirm GT911 on I2C and touch axis mapping.
5. Validate microSD, ES8311 microphone/speaker, UART1/MAX485, and host-to-C6 Wi-Fi.
6. Test camera module power/pinout and CSI acquisition separately; do not assume a 1080p60 end-to-end pipeline.
7. Measure JP1 pad functions and shared consumers before assigning UART/GPIO to external H2 Zigbee RCP.

## Project-specific constraints

- ESP32-H2 remains a **separate Zigbee RCP**; no Thread radio/network is implied by the camera/display documentation.
- The board's **RS485 UART1**, **UART0 console**, **C6-connected JP1 pins**, **Ethernet RMII candidates** and **I2C used by display/audio** are not automatically available for the H2.
- New BSP code belongs in `components/board_4_3p4` when the source tree is implemented; peripheral abstractions must not leak into Matter/Zigbee domain code.
- The firmware support, memory consumption, camera sensor model *actually shipped*, and connector mapping must be validated on the physical board. This file is a documentation reference, not a passed test report.

## Supplier image registry

The 12 original user-provided AliExpress CDN graphics are referenced and embedded remotely in [IMAGE_SOURCES.md](IMAGE_SOURCES.md). These files have **not** been downloaded into Git or validated as pinout evidence.
