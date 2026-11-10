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

The user clarified that this board has separate UART1 and MAX485 headers on the same UART1 signals, and H2 is connected through Grove using 5 V, GND, TX and RX only. The vendor schematic transcription identifies UART1 TX1 as P4 GPIO26 and RX1 as GPIO27, also shared with the MAX485 path. The user confirms TX/RX direction, continuity, 3.3 V logic and no MAX485 conflict for this test setup. A board-revision identifier and measurement log are not in the repository; the assignment is therefore limited to the separate, user-directed JC4880P443C_I_W test profile and is not promoted to shared defaults or a production pin reservation change. `firmware/p4/sdkconfig.defaults.h2-jc4880p443c` configures UART1, P4 TX GPIO26 and P4 RX GPIO27 for that test profile. M5Stack's [Gateway H2 Zigbee Gateway guide](https://docs.m5stack.com/en/esp_idf/zigbee/unit_gateway_h2/zigbee_gateway) documents H2 UART0 RX=GPIO23/TX=GPIO24 and recommends 230400 baud for Grove links that fail at 460800. Its nominal host example sets reset/boot to `-1`; this board's Grove connection has no H2 EN/reset/boot wire. In ESP-IDF 5.5.5 the UART Spinel `HardwareReset()` path reinitializes the P4 UART and invokes the RCP failure callback; it does not toggle H2 reset. A missing Spinel response can still cause `SpinelDriver::ResetCoprocessor()` to exit the P4, so the absent external reset line limits hard recovery but is not established as the cause of the initial timeout.

## P4 + H2 UART interface (test hardware)

| P4 side | H2 side | Notes |
|---|---|---|
| UART1 TX GPIO26 | UART0 RX GPIO23 | 3.3V logic, TX/RX crossed |
| UART1 RX GPIO27 | UART0 TX GPIO24 | H2 logs use USB Serial/JTAG |
| GND | GND | common ground |
| board 5V | H2 Grove VCC | User-reported wiring; confirm supply margin for the exact module |

The P4 H2-host profile uses 230400 baud. The existing connection has passed Spinel reset and BDB readiness checks. No H2 EN/reset or boot wire is connected. UART1 shares board signals with MAX485; the user removed the zero-ohm RX coupling resistor and reports no transceiver conflict for this test setup. Do not enable RS485 concurrently. This is the P4+C6+H2 target; Zigbee devices do not join Thread.


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

An esptool read-only probe identified ESP32-P4 revision 3.2 and 16 MB flash on `/dev/ttyACM0`. The initial P4 ESP-Hosted image booted and initialized the documented SDIO map; the onboard C6 has since been updated to ESP-Hosted 3.1.0. The PCB revision and electrical continuity remain separately unverified. Current P4/H2 build and hardware status are in the [H2 migration audit](audits/2026-11-08-h2-zigbee-migration.md).

## HMI and Wi-Fi validation update (2026-11-07)

The P4 detects 32 MB HEX PSRAM at 200 MHz and passes the PSRAM memory test. ESP-Hosted 3.1.0 matches the onboard C6 over SDIO with aggregation negotiated. The touchscreen Wi-Fi wizard using ST7701 MIPI-DSI and GT911 I2C is visible and touch responds; its password keyboard is docked within the display area. A home STA connection obtained IPv4 via DHCP, was saved in encrypted `wifi_cfg` NVS, and automatically reconnected after reboot. The local read-only diagnostics page at `http://192.168.69.17/` reports Wi-Fi, encryption, PSRAM and heap status without showing SSID or password. Credentials are excluded from source and audit. Generic ST7701 returned LCD ID `FF FF FF`; the panel-specific initialization table was applied and visible output was confirmed. PCB revision and signal continuity remain unverified; extended RSSI/roaming/IPv6/mDNS testing remains pending. See [P4 HMI/Wi-Fi audit](audits/2026-11-07-p4-hmi-wifi.md).
