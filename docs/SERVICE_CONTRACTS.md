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

The P4 Web UI and API run over HTTP only on port 80 and advertise `_http._tcp` using hostname and instance name `MultiHub`. The intended browser address is `http://MultiHub.local/`; direct IPv4 access is also supported. No HTTPS listener or `_https._tcp` service is started. If mDNS initialization or service registration fails, the HTTP server remains available by IP and logs the mDNS error. Historical HTTPS validation predates ADR-0002.

## System diagnostics snapshot

`firmware/p4/main/system_diagnostics.hpp` exposes a stateless, read-only snapshot shared by the touchscreen and HTTP diagnostics. It reports Flash Encryption state and free PSRAM/internal heap in whole KiB; it performs no allocation, persistence, network I/O or secret access. It may be called from task context and returns instantaneous values. UI/HTTP consumers must not treat a successful encryption flag as Secure Boot or production-security validation.

## HTTP Web UI security and Zigbee approval

`firmware/p4/main/web_security.hpp` owns local Web UI credentials and session state. It refuses to start unless Flash Encryption is active. The production profile creates a cryptographically random one-time password for username `admin`, prints it once to the P4 serial log, stores only a salted PBKDF2-HMAC-SHA256 hash in NVS, and requires a password change of at least 12 characters before accepting Zigbee commands. Production uses 100,000 PBKDF2 iterations. The explicit local `CONFIG_HUB_WEB_DEVELOPMENT_MODE` profile uses `admin/admin`, 10,000 iterations and no forced rotation; it performs a one-time NVS migration recorded by a count and uptime marker. Kconfig defaults the profile off; never ship a build with it enabled. Failed login attempts are rate-limited. Successful Basic authentication issues a random 128-bit session cookie (`HttpOnly`, `SameSite=Strict`) for 10 minutes. Because the Web UI is HTTP-only, both Basic credentials and the session cookie are plaintext in transit; these browser controls do not provide transport confidentiality or integrity. Use only on a trusted, isolated LAN; do not expose port 80 to the Internet, guest networks, or shared/untrusted LANs. Flash Encryption protects stored data only. The session is not persisted across device reboot. Password changes and logout revoke it. The former TLS identity stored in NVS is no longer initialized or served and is not erased by this change.

If the one-time password was lost, the USB Serial/JTAG console command `web_password_reset CONFIRM-WEB-PASSWORD-RESET` creates and prints a new random one-time password, stores only its salted hash, sets `pw_change=1`, increments the durable `pw_reset_count`/`pw_reset_time` audit fields, and revokes existing Web UI sessions. It does not erase NVS or reset Zigbee state. The exact confirmation token is required; the password itself is never accepted as a command argument or written to source/audit files.

Authenticated, same-origin HTTP POST requests may create one `WebZigbeeApproval` request at a time. The Web UI presents an explicit browser confirmation before submitting a state-changing action; an accepted request enters `approved` directly, so no additional touchscreen approval is required. Requests expire after 60 seconds, and request IDs reject stale or duplicate decisions. The HTTP task never calls radio APIs; `WifiWizard` claims the approved request in its owner task and submits the existing nonblocking coordinator command. Durable audit is still committed before any radio operation; `completed` means queued, not that BDB formed a network. The `scan_nearby` action uses the audited legacy `request_search()` command: on a factory-new coordinator it requests formation and, after BDB success, opens a 180-second join window; on an already formed network it opens the join window directly. Rejection, expiry, an unavailable coordinator, an unformed network for pairing, and queue/audit failures do not open the join window. `GET /api/zigbee/status` provides the small status snapshot used for periodic polling; full C6 OTA diagnostics and staged-image hashing require authenticated HTTP and run only on initial load and the dedicated C6 diagnostics refresh. The nearby-device section shows the count of Zigbee announcements; individual identities and interview data are not yet available in the Peer Data contract.

## C6 diagnostics

`hub::read_c6_diagnostics()` in `firmware/p4/main/c6_ota.hpp` provides a read-only snapshot of the ESP-Hosted CP version, target/chip type, `c6_fw` staging partition/image validation and full staged-image SHA-256, and the C6 OTA audit state/counter/error. It returns `ESP_ERR_INVALID_ARG` for a null output pointer; unavailable subqueries are reported in the corresponding snapshot fields. The authenticated HTTP `GET /api/c6/status` and P4 USB Serial/JTAG console command `c6_status` use this same snapshot. The HTTP route does not return a MAC address, Wi-Fi credentials or network key. The diagnostic read may issue synchronous ESP-Hosted queries and hash the staged image in bounded 512-byte chunks, but does not write flash or NVS. The page requests this expensive snapshot on initial authenticated load and through a dedicated C6 diagnostics refresh; the ordinary dashboard refresh never hashes the staged image.

The C6 updater remains separate from diagnostics. Standard updates use `c6_ota CONFIRM-C6-ESP-HOSTED-3.1.0` and reject duplicate/downgrade CP versions. The target image hash is rechecked before staging/transfer; newer running versions are never downgraded. C6 OTA is not a Zigbee coordinator selection mechanism: the P4 build profile determines which coordinator backend is used.

## P4 + C6 Zigbee coordinator target

`firmware/c6/main/c6_zigbee_sdio.c` implements the P4+C6 target: C6 owns ESP-Zigbee and its native 802.15.4 radio, while ESP-Hosted carries Wi-Fi and Peer Data RPC over SDIO. No UART RCP is used by this target. `firmware/p4/main/c6_zigbee_bridge.hpp` consumes fixed-size versioned Peer Data snapshots and sends commands; it copies coordinator state and a bounded announcement count, not EUI-64 values.

The P4 bridge exposes nonblocking `request_network_start()`, `request_pairing_window(seconds)` and legacy `request_search()` operations. Calls enqueue into a bounded depth-2 worker queue; queue-full returns `ESP_ERR_TIMEOUT`. The worker commits the action-specific audit record to P4 NVS before sending the Peer Data command, and the C6 command owner commits its own NVS audit before performing a Zigbee operation. Network creation is allowed only on a factory-new coordinator and leaves permit-join closed. Opening a 1..180-second join window requires an already formed network. The legacy `request_search()` remains a combined form-or-open operation for compatibility.

The C6 Zigbee owner logs the command type/duration, Peer Data queue overflow, audit and Zigbee-lock failures, commissioning preflight (`bdb_ready`, `factory_new`, `network_formed`, timeout state), and the raw plus mapped result of BDB initialization, formation, close and join-window calls. It logs each SDK signal type and raw BDB status; the timeout record includes the last signal/status and the commissioning status queried at the 120-second watchdog. Logs must not include network keys, install codes, EUI-64 values, or Thread datasets. `ESP_ERR_TIMEOUT` is the local watchdog or lock timeout, not a Zigbee SDK formation result; terminal formation status is logged separately from `IN_PROGRESS`.

The touchscreen and Web UI submit audited, confirmation-gated network and pairing requests. The P4 consumes immutable C6 Peer Data snapshots and does not call the Zigbee SDK. This target currently exposes announcement counts only; short-address interview and per-device controls are available only in the H2 target.

The installed P4/C6 command payload remains version 1. C6 status protocol v3 adds four bytes of raw BDB result plus a stage byte; the P4 accepts v1/v2/v3. The 120-second watchdog timeout is reported separately from the raw SDK return and terminal BDB signal. The last observed network-create attempt reported `last_signal_type=0x0100`, `last_bdb_status=1` (`IN_PROGRESS`), `network_formed=no`, `devices=0`, `join_seconds=0`, `last_error=263` (`ESP_ERR_TIMEOUT`); the underlying BDB operation did not emit a terminal formation status. No device join is claimed, and RF coexistence/network persistence remain unverified. This profile does not use Thread or a UART radio link.

## P4 + C6 + H2 Zigbee RCP target

`firmware/p4/main/zigbee_coordinator.hpp` defines `IZigbeeCoordinator` and the immutable `ZigbeeCoordinatorSnapshot`. `C6ZigbeeBridge` implements the P4+C6 backend. Under `CONFIG_HUB_H2_ZIGBEE_RCP_ENABLE`, the P4 radio service starts ESP-Zigbee over the H2 UART RCP and skips C6 coordinator ownership. The P4 composition root selects exactly one backend at build time.

The contract is nonblocking at public call sites. Network creation and pairing-window requests enter a depth-2 owner queue and persist the action audit before changing Zigbee state; queue-full returns `ESP_ERR_TIMEOUT`, invalid duration returns `ESP_ERR_INVALID_ARG`, and unavailable/unstarted services return `ESP_ERR_INVALID_STATE`. On H2, `stack_ready` is published only after the BDB first-start/reboot signal succeeds. The SDK startup-skip signal queues BDB initialization; an approved create request then starts `EZB_BDB_MODE_NETWORK_FORMATION` only when BDB is ready and the coordinator is factory-new. Signal callbacks copy bounded event data to the owner queue; the owner task performs follow-up BDB calls under the Zigbee lock. Terminal formation status and raw SDK results/stages are retained in the immutable snapshot. A 120-second formation watchdog queries the current BDB status, reports `ESP_ERR_TIMEOUT`, clears pending state, and does not retry or open joining.

`request_search()` uses a pure tested policy: reject until the stack and BDB are ready; on a factory-new coordinator, create a network and open the 180-second join window only after BDB formation and network-close succeed; on an already formed network, open the window directly. Both stateful operations are audited before their Zigbee calls. `read_snapshot()` copies status under a nonblocking mutex; in the H2 profile it includes at most 16 announced short addresses, but no IEEE/EUI-64 values. C6 Peer Data remains count-only. Request acceptance means queued, not that BDB formation or device join succeeded.

The H2 profile performs a bounded, asynchronous ZDO interview after a device announce or an authenticated `POST /api/zigbee/device` `operation=interview` request for a known short address. It requests Active Endpoints and then one Simple Descriptor at a time; callback data is copied into fixed-capacity owner events before the SDK releases it. The snapshot retains at most 16 devices, 4 endpoints per device, and 8 input plus 8 output clusters per endpoint. Each descriptor request times out after 10 seconds, records a per-device failure, and is not retried automatically. Authenticated `GET /api/zigbee/device?address=<short>` returns only the selected short-address record and never an IEEE/EUI-64 identifier. The dashboard renders the exact advertised cluster IDs; standard On/Off (`0x0006`) and Level Control (`0x0008`, target 0..254) controls appear only when present as input/server clusters. Every device write requires explicit browser confirmation; no touchscreen confirmation is required for Web UI actions. The owner revalidates the discovered endpoint/cluster, commits a durable NVS audit, then submits the ZCL command under the Zigbee lock. Acceptance means the command was accepted by the ZCL API, not confirmed physical actuation. The C6 backend retains its count-only snapshot and returns `ESP_ERR_NOT_SUPPORTED` for interview/command operations.

The H2 profile persists a versioned, CRC-checked list of at most 16 Zigbee short addresses in the P4 `nvs` partition under namespace `zigbee_known`, key `shorts_v1`. Serialization, NVS scratch buffers and the runtime registry use fixed-size arrays; no application heap allocation is used for device addresses or restoration. Only short network addresses are stored, never IEEE/EUI-64 addresses or network keys. New announcement addresses and authenticated manual interview addresses are committed by the owner task. After BDB reports an existing joined network, the owner restores the list and re-interviews one address at a time; failed/time-out interviews do not delete the persisted address. When BDB reports `factory_new`, only this key is replaced by a valid empty versioned blob; the NVS partition and unrelated credentials/audits are not erased. Invalid schema/CRC is logged and ignored rather than interpreted as addresses. The C6/count-only backend does not use this store.

The P4 USB Serial/JTAG console command `zigbee_interview <short-address>` requests a read-only interview through the coordinator owner queue and persists that short address. It validates `0x0001..0xFFFE`; it does not open permit-join, issue cluster commands, erase NVS, or reset the device. Interview still requires the device to respond to ZDO requests.

For an advertised On/Off server cluster, the H2 host reads attribute `0x0000` using ZCL Read Attributes and updates the immutable endpoint snapshot from bounded Read Attribute Response or Report Attribute events. Only a successful boolean value is considered known. The value becomes stale after 10 seconds; the authenticated Web UI requests a fresh read with `POST /api/zigbee/device` `operation=read_on_off` during its bounded polling and after an accepted On/Off command. While a read is pending, the page re-queries the capability snapshot every 500 ms; automatic Zigbee read requests remain rate-limited to at most one per second. The service uses its bounded owner queue and suppresses duplicate reads while one is pending. A pending read expires after 10 seconds and returns the UI to `Unknown`; it never defaults to Off. The UI renders one browser-confirmed switch for each interviewed endpoint with input cluster `0x0006`; it keeps the last reported state while a command is pending and only changes after readback/report. Green Power endpoint `0xF2` receives no On/Off control unless it independently advertises the server cluster. The C6/count-only backend does not expose this value and keeps the control unavailable.

For an advertised Level Control server cluster (`0x0008`), the H2 host reads `CurrentLevel` attribute `0x0000` and stores only successful unsigned-byte values `0..254`; `0xFF` is unknown. A read is requested after an accepted level command and through authenticated `POST /api/zigbee/device` `operation=read_level`. The value becomes stale after 10 seconds, duplicate reads are suppressed while pending, and a pending read times out after 10 seconds. The dashboard keeps the slider target separate from the reported value. ZCL API acceptance means the command was queued by the stack, not that the physical device acted; CurrentLevel readback is the device-state evidence.

The P4 H2 profile is Kconfig-disabled by default; its generic UART TX/RX GPIO defaults remain `-1`. The board-specific profile uses P4 UART1 GPIO26/27 at 230400 baud. The standard H2 RCP uses UART0 TX GPIO24/RX GPIO23, with logs on USB Serial/JTAG. On 2026-11-08 the P4 host logged `RESET_POWER_ON`, successful RCP software reset, `BDB ready=yes`, and `joined=yes`. The persisted network was not recreated. Read-only API status reports two saved short addresses: `0x1a02` interview succeeded; `0x8362` still times out. The C6 Wi-Fi-only image is build-validated but not flashed; C6/H2 firmware and NVS were unchanged during the P4 profile update.

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
