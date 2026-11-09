# Project memory

## Supported system targets

Maintain only these two firmware compositions:

1. **P4 + C6:** P4 application and UI; C6 provides ESP-Hosted Wi-Fi and owns native Zigbee over SDIO Peer Data. Its build profile is retained; end-to-end commissioning is not currently claimed as validated.
2. **P4 + C6 + H2:** P4 hosts Zigbee through the external H2 standard UART Spinel RCP; C6 provides Wi-Fi only. The C6 Wi-Fi-only image is build-validated but has not been installed.

C6 multipan and Thread/Zigbee multipan are removed as product targets. Zigbee devices never join Thread. Matter Bridge and Home Assistant discovery are not implemented or validated.

## Current hardware state (2026-11-08)

- P4 revision 3.2 runs the encrypted board-specific H2-host profile on UART1 TX/RX GPIO26/27 at 230400 baud. Flash Encryption and 32 MB PSRAM passed boot checks; C6 ESP-Hosted 3.1.0, SDIO and Wi-Fi/DHCP started.
- H2 remains on the standard UART0 Spinel RCP image: TX GPIO24, RX GPIO23. No H2 reset/boot wire is present. P4 received Spinel reset successfully; BDB reports `ready=yes`, `joined=yes` for the existing persisted Zigbee network.
- Read-only status reports two persisted addresses. `0x1a02` interview succeeded with On/Off and Level Control clusters; `0x8362` interview still times out with `ESP_ERR_TIMEOUT` (263). No new network or device join was performed.
- The latest P4 H2 firmware is flashed. User-approved tests sent Level targets `93` with both `MoveToLevelWithOnOff` and standard `MoveToLevel` (`ExecuteIfOff=1`); both received ZCL Default Response success. A separately approved On command also received success and readback reported `OnOff=true`. Level CurrentLevel remained `3` after each test. After the subsequent P4-only reboot, read-only state reported CurrentLevel `3` and On/Off `false`. No additional writes were sent. Automatic On/Off and Level reads are serialized per endpoint.
- C6 and H2 firmware/NVS are not to be changed without explicit target-image and port selection. The C6 Wi-Fi-only image is not installed; do not assume C6 native Zigbee is disabled in the active H2 composition.

## Hardware and security constraints

- UART1 GPIO26/27 shares board nets with MAX485. The user reports removing the zero-ohm RX coupling resistor and confirms the UART crossover/logic for this test setup. Do not enable RS485 concurrently.
- The local Web UI is HTTP-only on port 80 by ADR-0002. Basic credentials/session cookies are plaintext in transit; use only on a trusted isolated LAN.
- The generated workstation `sdkconfig` enables development credentials `admin/admin`; never ship that configuration. P4 images on this unit require encrypted flashing. Do not erase NVS or program eFuses without explicit approval.
- Never record Wi-Fi credentials, one-time Web UI passwords, Zigbee network keys, install codes, Matter secrets, or private serial logs.

## Build and verification

- ESP-IDF v5.5.5, RISC-V GCC 14.2.0; component versions are pinned by project manifests/lockfiles.
- P4+C6 and P4+C6+H2 P4 profiles build. C6 native-Zigbee and C6 Wi-Fi-only profiles build. H2 standard RCP builds.
- Host tests cover device registry, capabilities, approval, search policy, known-device persistence, On/Off readback, and Level `CurrentLevel` state validation.
- H2 profile build/flash evidence and current device-interview results are in [the H2 migration audit](audits/2026-11-08-h2-zigbee-migration.md). HMI and C6 OTA evidence remain in their corresponding audits.