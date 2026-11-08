# Hardware knowledge base: Guition JC4880P443C_I_W (4.3P4)

## Confidence levels

**Vendor/board schematic** = use after matching PCB revision. **Community-verified** = reported on actual boards, recheck on ours. **Proposed** = not yet wired. No pin on the expansion header is confirmed as a safe free GPIO solely by this file.

| Block | Reported characteristics | Status |
|---|---|---|
| Host | ESP32-P4; P4 silicon revision may affect toolchain | verify on device |
| Network | onboard ESP32-C6 Wi-Fi/BT via ESP-Hosted SDIO | reported |
| RAM/flash | 32 MB PSRAM, 16 MB flash | reported; probe |
| LCD | 4.3 inch 480x800 IPS, ST7701S MIPI-DSI, 2 lanes | community-verified |
| Touch | GT911 capacitive I2C | community-verified |
| Storage | microSD over SDMMC 4-bit | reported |
| Audio | ES8311 codec, NS4150 amp, microphone | reported |
| I/O | RS485/MAX485, UART, USB HS/FS, expansion header | reported |
| Power | USB 5V, 3.3V regulator, IP5306 Li-ion circuitry | reported; measure |

## Board GPIO reservation — cross-check vendor schematic before use

| Function | P4 GPIO or hardware | Reservation |
|---|---|---|
| LCD reset | GPIO5 | RESERVED |
| LCD backlight | GPIO23 | RESERVED, backlight enable reported active-high |
| MIPI-DSI | on-chip DSI; DSI PHY via LDO channel 3 @ 2.5V | RESERVED |
| Touch I2C SDA/SCL | GPIO7 / GPIO8 | RESERVED, shared with audio |
| Touch reset | GPIO3 in one community BSP; other projects differ | UNVERIFIED, RESERVED |
| ESP-Hosted SDIO CLK/CMD | GPIO18 / GPIO19 | RESERVED |
| ESP-Hosted SDIO D0-D3 | GPIO14/15/16/17 | RESERVED |
| C6 reset | GPIO54 | RESERVED |
| SDMMC CLK/CMD | GPIO43 / GPIO44 | RESERVED |
| SDMMC D0-D3 | GPIO39/40/41/42 | RESERVED |
| SD supply | LDO channel 4 @ 3.3V | RESERVED |
| ES8311 I2S | MCLK13, BCLK12, LRCK10, DOUT9, DIN48 | RESERVED |
| Amplifier enable | GPIO11 | RESERVED |
| UART0 console | TX GPIO37, RX GPIO38 | RESERVED (may be temporarily reassigned after assessment) |
| MAX485 UART | GPIO26 TX, GPIO27 RX | RESERVED |
| BOOT button | GPIO35 reported | RESERVED |

**Do not infer H2 pin choices from these entries.** Expansion header may include C6-specific pins that are **not** P4 GPIOs. Obtain vendor connector-to-net pin table, check shared signals, and document continuity before powering H2.

## User-reported connector map (2026-11-07)

The following pin labels and functions were supplied by the user. They have not been independently checked against the matching PCB revision or verified by continuity measurement.

| Connector | Pin | User-reported label/function | Status |
|---|---:|---|---|
| J5, H2 serial | 1 | 5 V | UNVERIFIED |
| J5, H2 serial | 2 | P4 IO26 | UNVERIFIED; shared UART1/RS485 net |
| J5, H2 serial | 3 | P4 IO27 | UNVERIFIED; shared UART1/RS485 net |
| J5, H2 serial | 4 | GND | UNVERIFIED |
| J4, RS485 | 1 | 5 V | UNVERIFIED |
| J4, RS485 | 2 | RS485 A / RX1 | UNVERIFIED; A is a differential bus terminal, not a UART logic pin |
| J4, RS485 | 3 | RS485 B / TX1 | UNVERIFIED; B is a differential bus terminal, not a UART logic pin |
| J4, RS485 | 4 | GND | UNVERIFIED |
| CN2, C6 | 1 | VIN | UNVERIFIED |
| CN2, C6 | 2 | TX0_BOOT | UNVERIFIED; boot-strapping interaction unknown |
| CN2, C6 | 3 | RX0 | UNVERIFIED |
| CN2, C6 | 4 | GND | UNVERIFIED |

J5 GPIO26/27 matches the UART1/RS485 signals already marked RESERVED above. Do not connect a second UART peer or enable RS485 concurrently until the shared-net topology, transceiver enable state and electrical levels are confirmed. CN2 is a user-reported C6 service connector; its presence does not authorize flashing or changing C6 firmware.

## H2-to-P4 wiring update (2026-11-08)

The user clarified that this board has separate UART1 and MAX485 headers on the same UART1 signals, and H2 is connected through Grove using 5 V, GND, TX and RX only. The vendor schematic transcription identifies UART1 TX1 as P4 GPIO26 and RX1 as GPIO27, also shared with the MAX485 path. The user confirms TX/RX direction, continuity, 3.3 V logic and no MAX485 conflict for this test setup. A board-revision identifier and measurement log are not in the repository; the assignment is therefore limited to the separate, user-directed JC4880P443C_I_W test profile and is not promoted to shared defaults or a production pin reservation change. `firmware/p4/sdkconfig.defaults.h2-jc4880p443c` configures UART1, P4 TX GPIO26 and P4 RX GPIO27 for that test profile. M5Stack's [Gateway H2 Zigbee Gateway guide](https://docs.m5stack.com/en/esp_idf/zigbee/unit_gateway_h2/zigbee_gateway) documents H2 UART0 RX=GPIO23/TX=GPIO24 and recommends 230400 baud for Grove links that fail at 460800. Its nominal host example sets reset/boot to `-1`; this board also has no H2 EN/reset/boot wire on Grove. The current IDF timeout recovery path nevertheless exits the P4 after no Spinel response.

## User-reported C6 multipan wiring (2026-11-07)

The user reports the following C6-to-P4 connections and says RS485 is unused. These mappings have not been independently checked against the board revision or continuity-tested:

| C6 signal | User-reported P4 destination | Status |
|---|---|---|
| C6_IO9 | P4 package pin 18 | UNVERIFIED; GPIO/net not established |
| C6_U0TXD | P4 package pin 17; user also says GPIO17 | UNVERIFIED; GPIO17 is reserved for ESP-Hosted SDIO D3 |
| C6_U0RXD | P4 package pin 16; user also says GPIO16 | UNVERIFIED; GPIO16 is reserved for ESP-Hosted SDIO D2 |
| C6_IO2 | P4 package pin 10; user also says GPIO10 | UNVERIFIED; GPIO10 is reserved for ES8311 I2S LRCK |
| C6_CHIP_PU | P4 package pin 75 | UNVERIFIED; package pin-to-net mapping unknown, not a P4 GPIO number |

If the numbers 16/17/18/10 are intended as P4 GPIO numbers, they conflict with the live ESP-Hosted SDIO and I2S assignments above; GPIO18 is SDIO CLK. RS485 being unused does not resolve these SDIO/I2S conflicts. Do not enable the multipan host or attach these nets to the current P4 configuration until the package-pin/net/GPIO mapping is established and a compatible, non-conflicting UART assignment is verified. The current C6 multipan image has been transferred, but the P4 multipan host remains disabled and Zigbee search has not been run.

## External ESP32-H2 RCP interface (build profile; wiring record incomplete)

| P4 side | H2 side | Notes |
|---|---|---|
| allocated UART TX | H2 configurable UART RX | 3.3V logic, cross TX/RX |
| allocated UART RX | H2 configurable UART TX | dedicated, no console logs |
| allocated GPIO | H2 EN/RST | safe boot and recovery |
| allocated GPIO | H2 boot strap | firmware updates; avoid unsafe default strap |
| GND | GND | common ground |
| regulated 3.3V | H2 supply | confirm peak current/regulator margin, decoupling |
| optional GPIOs | PTA coexistence | assess RF collision / available GPIOs |

The in-repository H2 image uses UART0 (TX GPIO24, RX GPIO23) and standard Spinel. M5Stack's Gateway H2 guide warns that 460800 baud may fail on Grove wiring and recommends reducing to 230400; the latest H2 image and P4 test profile use 230400. The P4 generic profile leaves UART pins unset, while the board-specific test profile uses UART1 GPIO26/27. No reset/boot connection is assigned.

Flash ESP32-H2 with version-pinned ESP-IDF **ot_rcp** variant configured for Zigbee gateway requirements (upstream Matter example requires `OPENTHREAD_NCP_VENDOR_HOOK=y`). Although the binary is called OpenThread RCP, Zigbee host examples use its 802.15.4 radio abstraction. This is *not* a Thread network in this project.

## RF, bring-up and hazards

- Place H2 2.4 GHz antenna away from LCD flex, metal cases, noisy DC/DC and the onboard C6 antenna; check coexistence and channel planning.
- Measure 3.3V idle/startup/Tx peaks; use level-compatible cabling, common GND, short UART signal paths.
- P4 engineering sample revisions may need alternate build settings; do not apply third-party PlatformIO ES flags to ESP-IDF blindly.
- Display may need manual ST7701S init, MIPI DSI LDO before reset, and touch reset verification.
- Never flash C6 Wi-Fi co-processor assuming P4 images are interchangeable.
- Check real schematic and battery connector polarity before adding external battery.

## Primary references

- Vendor Guition zip: https://pan.jczn1688.com/directlink/1/HMI%20display/JC4880P443C_I_W.zip
- Community pinout and schematic: https://github.com/ultramcu/guition-jc4880p443c-i-w
- Board BSP field implementation: https://github.com/ultramcu/guition-jc4880p4-bsp
- Espressif host/radio sample: https://github.com/espressif/esp-matter/tree/main/examples/bridge_apps/zigbee_bridge

**Validation deliverable:** a versioned connector pin table (pin number, net label, ESP GPIO, electrical level, shared consumers, verified yes/no) with physical test evidence.

## Vendor documentation added (2026-11-07)

The supplied manufacturer PDFs and schematic are distilled in **[BOARD_VENDOR_REFERENCE.md](BOARD_VENDOR_REFERENCE.md)**, including the manufacturer's 12-connector legend, board dimensions and electrical ratings, JP1 2×13 net table (with unresolved positions explicitly marked), shared UART1/RS485 and I2C dependencies, CSI/DSI details, OV02C10 integration limits and a bring-up checklist. The new reference is **documentary evidence, not electrical validation**; retain RESERVED/UNVERIFIED assignments until tested.

The supplied camera OV02C10 datasheet is marked confidential and is **not committed**. Its factual electrical/interface summary appears in the reference; never apply the sensor's internal 1.2/1.8/2.8 V supply rails directly as a camera-module connector pinout.

## Physical probe (2026-11-07)

An esptool read-only probe identified ESP32-P4 revision 3.2 and 16 MB flash on `/dev/ttyACM0`. The initial P4 ESP-Hosted image booted and initialized the documented SDIO map (Slot 1, 4-bit, 5 MHz, P4 GPIO18/19/14-17, reset GPIO54); runtime discovered the onboard C6 and reported WLAN/HCI over SDIO and RPC v2. The existing C6 firmware identifies as ESP-Hosted 2.3.2, while the P4 host is 3.1.0; the log reports a major-version mismatch. No C6 image was flashed, and no Wi-Fi association was tested. The PCB revision and electrical continuity were not separately verified. Full command, image hash and results are in the [P4 flash audit](audits/2026-11-07-p4-esp-hosted-flash.md).

## HMI and Wi-Fi validation update (2026-11-07)

The P4 detects 32 MB HEX PSRAM at 200 MHz and passes the PSRAM memory test. ESP-Hosted 3.1.0 matches the onboard C6 over SDIO with aggregation negotiated. The touchscreen Wi-Fi wizard using ST7701 MIPI-DSI and GT911 I2C is visible and touch responds; its password keyboard is docked within the display area. A home STA connection obtained IPv4 via DHCP, was saved in encrypted `wifi_cfg` NVS, and automatically reconnected after reboot. The local read-only diagnostics page at `http://192.168.69.17/` reports Wi-Fi, encryption, PSRAM and heap status without showing SSID or password. Credentials are excluded from source and audit. Generic ST7701 returned LCD ID `FF FF FF`; the panel-specific initialization table was applied and visible output was confirmed. PCB revision and signal continuity remain unverified; extended RSSI/roaming/IPv6/mDNS testing remains pending. See [P4 HMI/Wi-Fi audit](audits/2026-11-07-p4-hmi-wifi.md).
