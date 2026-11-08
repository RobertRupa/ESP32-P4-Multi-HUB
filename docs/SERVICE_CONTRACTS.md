# Service contracts and component boundaries

This catalog distinguishes proposed contracts from the implemented P4 Zigbee coordinator port. Use `docs/ENGINEERING.md` for normative coding practices.

## Contract catalog (proposed)

| Port / service | Responsible for | Key operations | Owner |
|---|---|---|---|
| `IRcpTransport` | H2 byte/framing transport and version handshake | `start`, `send`, `subscribe_frame`, `reset` | RCP task |
| `IZigbeeCoordinator` | Zigbee coordinator lifecycle and immutable status | `start`, `request_network_start`, `request_pairing_window`, `request_search`, `read_snapshot` | P4 owner task |
| `IDeviceReader` | Immutable snapshots and lookup | `get(device_id)`, `list(page)` | Registry |
| `IDeviceCommander` | Authorized command acceptance | `submit(request)` => accepted or error | Application |
| `IEventPublisher` | Typed bounded event delivery | `publish(event)` => delivered/queue full | Event bus |
| `ICapabilityTranslator` | Zigbee ZCL <-> domain values | `translate_report`, `encode_command` | Zigbee adapter |
| `IMatterBridgePort` | Matter endpoint/fabric adapter | `upsert_endpoint`, `set_reachable`, `report_attribute` | Matter task |
| `IStateStore` | Atomic versioned persistent records | `load`, `commit`, `migrate` | Storage task |
| `INetworkStatus` | IPv6/mDNS/Wi-Fi readiness | `snapshot`, `subscribe` | Network service |

## Wi-Fi Provisioning

`firmware/p4/main/wifi_provisioning.hpp` owns the STA provisioning contract over ESP-Hosted. Start once after the default event loop, netif and ESP-Hosted are ready. A dedicated `wifi_provision` task owns remote Wi-Fi calls; the LVGL task submits nonblocking scan/connect commands through a depth-4 queue and consumes latest-state snapshots through a depth-1 overwrite mailbox. Scans are capped at 16 APs. `connect` copies a maximum 32-byte SSID and 63-byte password, retries a failed association at most three times with bounded delays, and reports connected/failed status plus IPv4 without exposing the password. After DHCP success, credentials are committed to a dedicated `wifi_cfg` NVS partition whose XTS keys live in `wifi_keys`. Startup fails closed unless Flash Encryption is active; no plaintext fallback is permitted. Queue-full returns `ESP_ERR_TIMEOUT`; invalid credential lengths return `ESP_ERR_INVALID_SIZE`; initialization/transport failures are surfaced as a failed state. Callback context only copies bounded events to a queue and never calls LVGL or blocks on network I/O.

## Wi-Fi RSSI snapshot

While connected, the `wifi_provision` owner task reads ESP-Hosted RSSI on connection and every 5 seconds. `ProvisioningUpdate` exposes only dBm and a validity flag; UI/HTTP must not include SSID or BSSID. Failed samples set `rssi_valid=false`.

## mDNS hostname and Web services

After the HTTPS Web UI starts, the P4 initializes mDNS with hostname and instance name `MultiHub` and advertises `_http._tcp` on port 80 (redirect only) and `_https._tcp` on port 443. The intended browser address is `https://MultiHub.local/`; DHCP/IP changes do not require changing the advertised hostname. If mDNS initialization or service registration fails, the HTTPS/IP web server remains available and logs the mDNS error. Hardware verification on 2026-11-08 resolved `MultiHub.local` to `192.168.69.17` and Avahi discovered both services.

## System diagnostics snapshot

`firmware/p4/main/system_diagnostics.hpp` exposes a stateless, read-only snapshot shared by the touchscreen and HTTP diagnostics. It reports Flash Encryption state and free PSRAM/internal heap in whole KiB; it performs no allocation, persistence, network I/O or secret access. It may be called from task context and returns instantaneous values. UI/HTTP consumers must not treat a successful encryption flag as Secure Boot or production-security validation.

## HTTPS Web UI security and Zigbee approval

`firmware/p4/main/web_security.hpp` owns local Web UI credentials and the HTTPS identity. It refuses to start unless Flash Encryption is active. The production profile creates a cryptographically random one-time password for username `admin`, prints it once to the P4 serial log, stores only a salted PBKDF2-HMAC-SHA256 hash in NVS, and requires a password change of at least 12 characters before accepting Zigbee commands. Production uses 100,000 PBKDF2 iterations. The explicit local `CONFIG_HUB_WEB_DEVELOPMENT_MODE` profile uses `admin/admin`, 10,000 iterations and no forced rotation; it performs a one-time NVS migration recorded by a count and uptime marker. Kconfig defaults the profile off; never ship a build with it enabled. A production image refuses to start the Web UI while the development marker remains, until an explicit production password reset removes it. Failed login attempts are rate-limited. Successful Basic authentication issues a single random 128-bit session cookie (`Secure`, `HttpOnly`, `SameSite=Strict`) for 10 minutes; the browser restores the session from this cookie after a page reload, and polling uses the cookie instead of repeating PBKDF2. The session is not persisted across device reboot. The login form permits only one in-flight verification. Password changes and logout revoke the session. The per-device self-signed ECDSA certificate and private key are generated at first start and stored in NVS protected by Flash Encryption; its SHA-256 fingerprint is logged so the user can verify the browser certificate warning. HTTP on port 80 only redirects the page to HTTPS; application APIs and Zigbee writes are HTTPS-only and same-origin.

If the one-time password was lost, the USB Serial/JTAG console command `web_password_reset CONFIRM-WEB-PASSWORD-RESET` creates and prints a new random one-time password, stores only its salted hash, sets `pw_change=1`, increments the durable `pw_reset_count`/`pw_reset_time` audit fields, and revokes existing Web UI sessions. It does not erase NVS or reset Zigbee state. The exact confirmation token is required; the password itself is never accepted as a command argument or written to source/audit files.

Authenticated POST requests may only create one pending `WebZigbeeApproval` request at a time. The request expires after 60 seconds; approve/reject operations include its request ID, and stale or duplicate decisions are rejected. The HTTP task never calls radio APIs. An administrator may explicitly approve or reject in the Web UI or on the touchscreen. The `scan_nearby` action uses the audited legacy `request_search()` command: on a factory-new coordinator it requests formation and, after BDB success, opens a 180-second join window; on an already formed network it opens the join window directly. The HTTP handler changes only the approval state; `WifiWizard` claims an approved request in the UI task and submits the existing nonblocking C6 bridge command. The P4 NVS audit must commit before Peer Data transmission and the C6 audit before the Zigbee operation; `completed` means queued, not that BDB formed a network. Rejection, expiry, an unavailable coordinator, an unformed network for pairing, and queue/audit failures do not open the join window. `GET /api/zigbee/status` provides the small status snapshot used for periodic polling; full C6 OTA diagnostics and staged-image hashing require authenticated HTTPS and run only on initial load and the dedicated C6 diagnostics refresh. The nearby-device section shows the count of Zigbee announcements; individual identities and interview data are not yet available in the Peer Data contract.

## C6 diagnostics

`hub::read_c6_diagnostics()` in `firmware/p4/main/c6_ota.hpp` provides a read-only snapshot of the ESP-Hosted CP version, target/chip type, `c6_fw` staging partition/image validation and full staged-image SHA-256, and the C6 OTA audit state/counter/error. It returns `ESP_ERR_INVALID_ARG` for a null output pointer; unavailable subqueries are reported in the corresponding snapshot fields. The authenticated HTTPS `GET /api/c6/status` and P4 USB Serial/JTAG console command `c6_status` use this same snapshot. Port 80 registers only the HTTPS root redirect. Neither interface returns a MAC address, Wi-Fi credentials or network key. The diagnostic read may issue synchronous ESP-Hosted queries and hash the staged image in bounded 512-byte chunks, but does not write flash or NVS. The page requests this expensive snapshot on initial authenticated load and through a dedicated C6 diagnostics refresh; the ordinary dashboard refresh never hashes the staged image.

The C6 updater remains separate from diagnostics. Standard updates use `c6_ota CONFIRM-C6-ESP-HOSTED-3.1.0` and reject duplicate/downgrade CP versions. Same-version exceptions are bound to exact test images: UART multipan RCP token `c6_ota CONFIRM-C6-MULTIPAN-69F6E8C7D6E6CF00` with SHA-256 `69f6e8c7d6e6cf003986148fbcead6462654a9de4fbb74dbf355a999614410f4`, and SDIO-native Zigbee coordinator diagnostic token `c6_ota CONFIRM-C6-ZIGBEE-SDIO-F1D30B9309055BD2` with SHA-256 `f1d30b9309055bd26553dffbc7a95509af56ece4270a487be4cefe25087ac79b`. The updater recomputes the full staged image hash before audit/transfer; only the exact corresponding image may use each token, and newer running versions are never downgraded. The protocol-v2 diagnostic image is installed and reboot-verified; network formation remains unresolved.

## C6-native Zigbee over ESP-Hosted Peer Data (experimental)

`firmware/c6/main/c6_zigbee_sdio.c` is an opt-in C6-native Zigbee Coordinator prototype. C6 owns ESP-Zigbee and its native 802.15.4 radio; ESP-Hosted continues to carry Wi-Fi and Peer Data RPC over the existing SDIO link. No UART RCP is used by this profile. `firmware/p4/main/c6_zigbee_bridge.hpp` consumes fixed-size versioned Peer Data snapshots and sends commands; it copies only coordinator state, a bounded device-announcement count, join countdown and error code. It does not expose EUI-64 values.

The P4 bridge exposes nonblocking `request_network_start()`, `request_pairing_window(seconds)` and legacy `request_search()` operations. Calls enqueue into a bounded depth-2 worker queue; queue-full returns `ESP_ERR_TIMEOUT`. The worker commits the action-specific audit record to P4 NVS before sending the Peer Data command, and the C6 command owner commits its own NVS audit before performing a Zigbee operation. Network creation is allowed only on a factory-new coordinator and leaves permit-join closed. Opening a 1..180-second join window requires an already formed network. The legacy `request_search()` remains a combined form-or-open operation for compatibility.

The C6 Zigbee owner logs the command type/duration, Peer Data queue overflow, audit and Zigbee-lock failures, commissioning preflight (`bdb_ready`, `factory_new`, `network_formed`, timeout state), and the raw plus mapped result of BDB initialization, formation, close and join-window calls. It logs each SDK signal type and raw BDB status; the timeout record includes the last signal/status and the commissioning status queried at the 120-second watchdog. Logs must not include network keys, install codes, EUI-64 values, or Thread datasets. `ESP_ERR_TIMEOUT` is the local watchdog or lock timeout, not a Zigbee SDK formation result; terminal formation status is logged separately from `IN_PROGRESS`.

The touchscreen Devices view uses two successive taps to confirm `request_network_start()`, `request_pairing_window(180)`, or the combined `request_search()` nearby scan. It reads the same immutable C6 Peer Data snapshot as diagnostics and does not call Zigbee SDK functions. USB Serial/JTAG commands `c6_zigbee_network_start CONFIRM-C6-ZIGBEE-NETWORK` and `c6_zigbee_pairing_window CONFIRM-C6-ZIGBEE-PAIRING` expose the separate operations; `c6_zigbee_search CONFIRM-C6-ZIGBEE-SEARCH` remains available for the combined form-or-open operation. Status is sent at startup, on changes and every five seconds. Status protocol v3 preserves the v2 fields and adds the raw BDB SDK result and last operation stage; the P4 parser remains compatible with v1 and v2. Flag bit `0x08` reports that the 120-second formation watchdog elapsed while BDB had not completed; the P4 displays this separately from `network_formed` and blocks another network-create attempt until a terminal BDB formation signal or coordinator reboot. `GET /api/c6/status` remains read-only and requires authenticated HTTPS. HTTPS Web UI writes require the configured development/production login profile and same-origin HTTPS; an explicit web approve/reject or touchscreen approve/reject decision is required before audited bridge operations are queued. The nearby-device section currently displays only announced-device count; individual identities and interview data are not yet available. This C6 SDIO profile does not use Thread, multipan RCP or UART.

The installed P4/C6 command payload remains version 1. C6 status protocol v3 adds four bytes of raw BDB result plus a stage byte; the P4 accepts v1/v2/v3. The 120-second watchdog timeout is reported separately from the raw SDK return and terminal BDB signal. The last observed network-create attempt reported `last_signal_type=0x0100`, `last_bdb_status=1` (`IN_PROGRESS`), `network_formed=no`, `devices=0`, `join_seconds=0`, `last_error=263` (`ESP_ERR_TIMEOUT`); the underlying BDB operation did not emit a terminal formation status. No device join is claimed, and RF coexistence/network persistence remain unverified. This profile does not use Thread, multipan RCP or UART.

## Shared Zigbee coordinator port and H2 RCP profile

`firmware/p4/main/zigbee_coordinator.hpp` now defines `IZigbeeCoordinator` and the immutable `ZigbeeCoordinatorSnapshot`. The installed `C6ZigbeeBridge` implements this port without changing its Peer Data protocol. In the opt-in `CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE` profile, `MultipanService` implements the same coordinator API but starts only ESP-Zigbee over the H2 UART RCP; it does not initialize OpenThread or the multipan radio. The P4 composition root selects exactly one backend at build time and passes it to the HTTP/UI consumers.

The contract is nonblocking at public call sites. Network creation and pairing-window requests enter a depth-2 owner queue and persist the action audit before changing Zigbee state; queue-full returns `ESP_ERR_TIMEOUT`, invalid duration returns `ESP_ERR_INVALID_ARG`, and unavailable/unstarted services return `ESP_ERR_INVALID_STATE`. `request_search()` uses a pure tested policy: reject until the stack is ready; on a factory-new coordinator, create a network and open the 180-second join window only after BDB formation succeeds; on an already formed network, open the window directly. Both stateful operations are audited before their Zigbee calls. `read_snapshot()` copies status under a nonblocking mutex; it exposes no IEEE address. Request acceptance means queued, not that BDB formation or device join succeeded.

The P4 H2 profile is Kconfig-disabled by default; its generic UART TX/RX GPIO defaults remain `-1`. The board-specific test profile uses P4 UART1 GPIO26/27 at 230400 baud. The H2 image uses native 802.15.4 and UART0 (TX GPIO24/RX GPIO23), with system logs routed to USB Serial/JTAG. It has been flashed and its app-start log observed. P4 UART Spinel times out and IDF exits after reset failure; no handshake or pairing test has passed. The P4 was restored to the C6-backed profile. The C6 Wi-Fi-only profile is build-only; C6 firmware/NVS remain unchanged.

## Multipan radio service (experimental, IDF 6.2 profile)

`firmware/p4/main/multipan_service.hpp` owns the P4 OpenThread Border Router and Zigbee coordinator lifecycle when `CONFIG_HUB_MULTIPAN_ENABLE=y`. The default profile leaves it disabled. One owner task (8 KiB stack, priority 5) owns status and commands; the Zigbee SDK task is separately created with an 8 KiB stack. `start()` returns before initialization completes. When enabled, startup waits up to 30 seconds for the existing Wi-Fi STA netif and credential-store readiness, then starts the UART multipan transport. UART pins must be explicitly configured and electrically verified; no production pins are assigned.

`request_network_start()` and `request_pairing_window(seconds)` are nonblocking queue submissions (command queue depth 2); queue-full returns `ESP_ERR_TIMEOUT`. They are accepted only in the enabled/running profile. Network formation is initiated only by an explicit LCD action, which requires a second confirmation tap. Pairing is exposed only after a Zigbee network is formed; the LCD confirms the action and currently opens a 60-second window. The owner commits an audit count and uptime to the `radio_audit` NVS namespace before either stateful Zigbee operation. A failed audit prevents the operation. Join windows are bounded to 1..180 seconds and permit-join remains closed otherwise.

`read_snapshot()` returns a mutex-protected copy with radio lifecycle, Thread role, TBR readiness, Zigbee network state, join-window countdown, and a count of at most 64 unique device-announcement EUI-64 values held in RAM. Device IDs/addresses and network secrets are not returned by UI or HTTP. The announcement count is not a persistent device registry and may omit devices if the bounded event queue overflows; drops are counted. The HTTP diagnostics API is read-only. Thread is resumed only when a stored operational dataset exists; Thread commissioning/dataset provisioning is not implemented. The supplied UniFi Network view detects Thread Border Routers but reports `Devices: 0` and “Observability only”; that establishes Border Router visibility, not access to its child/neighbor table. The current P4 profile has OpenThread/multipan disabled. The development `POST /api/thread/scan` runs an asynchronous `_meshcop._udp` DNS-SD query and `GET /api/thread/status` returns the number of Border Agent advertisements and query error only; it does not enumerate Thread members, child IDs or addresses. Enumerating actual Thread nodes requires an enabled compatible Thread host or a supported Border Router API. Zigbee device discovery does not create Matter endpoints; the Matter Bridge and Home Assistant commissioning remain separate work.

With `CONFIG_HUB_MULTIPAN_ENABLE=n`, the service publishes `disabled` without including OpenThread/Zigbee SDK headers in its public API. With the feature enabled but missing UART pins it publishes `needs_uart_configuration` without starting either radio. `ESP_ERR_NOT_SUPPORTED`, `ESP_ERR_INVALID_STATE`, `ESP_ERR_TIMEOUT`, `ESP_ERR_NO_MEM`, and SDK errors retain their ESP-IDF meanings. HTTP and LCD read immutable snapshots; they do not call radio SDK APIs from the UI task.

## Explicit behavior requirements

- `start` is idempotent or returns a defined conflict. All services declare allowed lifecycle states.
- Each API declares task affinity and whether it blocks. Radio and Matter callbacks only enqueue bounded records.
- `submit` means the request was validated and enqueued, *not* that physical Zigbee device acted.
- Completion events carry command correlation IDs and success/failure reasons. Repeated commands require an explicit dedupe policy.
- `get`/ `list` return copies or immutable snapshots, never references to mutation-owned state that may expire.
- Cross-module payloads must not hold SDK-owned transient pointers. Own or copy necessary fields.
- `IStateStore` follows crash-safe commit semantics; no unbounded synchronous writes in UI or radio tasks.
- `IMatterBridgePort` mapping uses persistent stable IDs; unsupported capability must return `Unsupported`, never silently misrepresent state.
- Services indicate `Unavailable` during lost Wi-Fi/H2 instead of inventing success.
- An authentication principal/context is required for privileged command and commissioning operations.

## Dependency injection example (conceptual)

```cpp
// Domain/application headers may not include ESP-IDF/Matter/LVGL.
class DeviceCommandService final {
public:
    DeviceCommandService(IDeviceReader& devices,
                         IZigbeeCommandPort& zigbee,
                         IEventPublisher& events) noexcept;
    ErrorCode submit(const CommandRequest& request) noexcept;
private:
    IDeviceReader& devices_;
    IZigbeeCommandPort& zigbee_;
    IEventPublisher& events_;
};

// Composition root, inside firmware/p4; concrete types are from adapters.
// DeviceCommandService commands{registry, zigbee_adapter, event_bus};
// UiController ui{devices_reader, commands, event_bus};
```

The `IZigbeeCommandPort` name above is a future separated command port to be declared in the domain. No production code should copy this sketch without defining the full types and implementing tests.

## Contract change policy

When a contract changes, update its header-level semantics, tests, downstream mock and adapters in the same PR. Breaking changes need migration/version strategy. Keep the architecture decision record updated for boundary changes.

## Test doubles

Native fakes implement the same error and timeout behavior as actual adapters; add contract tests run against fake and hardware adapters when hardware is present. Never fake a green hardware test.
