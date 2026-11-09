# HTTP-only Web UI deployment audit

## Authorization and target

- Date: 2026-11-08
- User decision: permanently disable TLS for the local P4 Web UI
- Target: ESP32-P4 rev3.2 at `/dev/ttyACM0`
- Scope: P4 Web UI/API transport only. H2 RCP and C6 firmware were not changed.
- Architecture decision: [ADR-0002](../adr/0002-http-only-webgui.md)

## Build and flash

- Build directory: `/tmp/p4-h2-rcp-profile`
- Target/profile: ESP32-P4, H2 Zigbee RCP host enabled, UART1 GPIO26/27, SDIO 20 MHz
- Build: `source ~/esp/esp-idf/export.sh && idf.py -B /tmp/p4-h2-rcp-profile build`
- Result: build succeeded with ESP-IDF v5.5.5; generated image size 1,740,208 bytes.
- Flash: `idf.py -B /tmp/p4-h2-rcp-profile -p /dev/ttyACM0 encrypted-flash`
- Result: encrypted write succeeded for bootloader, application, and partition table. No `erase-flash`, eFuse, H2, or C6 operation was run; P4 NVS was not explicitly written or erased.

## Runtime verification

- Boot reported Flash Encryption enabled, 32 MB PSRAM test OK, and ESP-Hosted host/C6 versions matching at 3.1.0 over SDIO 4-bit at 20 MHz.
- H2 Spinel UART initialized and reset the co-processor. BDB initialized; the existing Zigbee network reported `factory_new=no`, `joined=yes`, `ready=yes`. Restored device `0x8362` Active Endpoints interview timed out (`raw=7`, `ESP_ERR_TIMEOUT`); no new network or device join is claimed.
- The P4 logged `HTTP Web UI listening on port 80; credentials and sessions are unencrypted` and advertised `MultiHub.local`.
- HTTP tests: `/` returned 200, `/favicon.ico` returned 204, `/api/status` returned 200 with `state=connected`, IPv4 `192.168.69.17`, RSSI `-80 dBm`, and `zigbee_network_formed=true`; unauthenticated `/api/auth/status` returned 401.
- HTTPS test: connection to port 443 was refused, as intended.
- `c6_status` returned C6 3.1.0 and OTA audit `verified`. The unauthenticated auth-status request returned 401; no password was entered by the test client and no authenticated write was attempted.
- An earlier HTTP-only image panicked in the `httpd` task with a stack-protection fault while returning 404 for `/favicon.ico`. The final image uses an 8 KiB HTTPD task stack and a 204 favicon handler. After this fix, root, favicon, status and auth requests completed without another panic during the captured run.
- Before deployment, logs contained repeated TLS handshake failures and ESP-Hosted RPC `msg_id=294` timeouts. In the approximately 136 seconds of final-image logs, neither TLS handshake errors nor `msg_id=294` timeouts appeared. ZDO Active Endpoints requests for `0x8362` still timed out (`raw=7`, `ESP_ERR_TIMEOUT`). This short observation does not prove TLS caused the earlier RPC failures or establish long-term Wi-Fi stability.

## Security and limitations

- HTTP Basic credentials and the session cookie are plaintext in transit. Flash Encryption protects NVS at rest only. Use this service only on a trusted isolated LAN; do not expose it through port forwarding, guest Wi-Fi or shared networks.
- Unauthenticated access remains rejected. Successful browser login and authenticated Zigbee writes were not exercised in this audit because user credentials were not used by the test client.
- Existing TLS identity data in NVS was not erased; the application no longer initializes or serves it.
