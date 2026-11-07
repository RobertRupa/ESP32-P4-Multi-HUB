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

## External ESP32-H2 RCP interface (proposed, not final wiring)

| P4 side | H2 side | Notes |
|---|---|---|
| allocated UART TX | H2 configurable UART RX | 3.3V logic, cross TX/RX |
| allocated UART RX | H2 configurable UART TX | dedicated, no console logs |
| allocated GPIO | H2 EN/RST | safe boot and recovery |
| allocated GPIO | H2 boot strap | firmware updates; avoid unsafe default strap |
| GND | GND | common ground |
| regulated 3.3V | H2 supply | confirm peak current/regulator margin, decoupling |
| optional GPIOs | PTA coexistence | assess RF collision / available GPIOs |

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
