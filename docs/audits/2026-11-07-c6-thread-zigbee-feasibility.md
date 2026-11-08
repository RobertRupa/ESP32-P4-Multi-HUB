# C6 Thread/Zigbee Feasibility Audit

## Scope

- Build-only investigation of the user's requested ESP32-C6 Wi-Fi + Thread Border Router + Zigbee coordinator path.
- The initial feasibility probes were build-only. A later authorized IDF 6.2 Wi-Fi-only P4 image was flashed; that update is recorded in the [P4 flash audit](2026-11-07-p4-esp-hosted-flash.md). No multipan-enabled P4 image, C6 image or H2 image was flashed.
- Initial builds used ESP-IDF v5.5.5 and temporary `/tmp` build/config paths. A second isolated branch used ESP-IDF master commit `0e6b480f858c69862ffb6001d96e9eef74599a6e` (reported as v6.2.0) with GCC 16.1.0. All build outputs/configs remained under `/tmp`; generated artifacts in upstream example folders were removed afterward.

## Results

- The stock ESP-IDF `examples/openthread/ot_br` builds for ESP32-C6 (app image `0x1c73f0`). This is a standalone C6 Thread BR using the C6's native Wi-Fi and 802.15.4 radios. It is not the current ESP-Hosted co-processor image and would replace that role unless a separate host/backhaul design is added. Espressif documents that Wi-Fi and 802.15.4 share one RF path on C6, with a significant performance impact; it recommends separate radios for a Wi-Fi-based BR/gateway.
- The ESP-Hosted 3.1.0 P4 Thread-BR host and C6 CP examples produced build artifacts, but the pinned ESP-IDF v5.5.5 does not define `OPENTHREAD_RADIO_SPINEL_CUSTOM` or `OPENTHREAD_RCP_CUSTOM`. The build logs warn that these required symbols are unknown. In the C6 config, `ESP_HOSTED_CP_FEAT_OPENTHREAD_READY` is not enabled and auto-init is off; therefore the produced CP image is not verified as a usable hosted RCP. The P4 host likewise lacks a validated hosted Spinel transport. Its unpinned example manifest also resolved `esp_wifi_remote` 1.6.5 rather than the product's pinned 1.3.1, so that artifact is not a product-compatible build.
- The pinned ESP-IDF v5.5.5 tree has no `examples/openthread/ot_zb_multipan`. Current upstream ESP-IDF has an OpenThread+Zigbee multipan example, but it requires a separate multipan RCP with special build options, both stacks on a compatible host, and a shared 802.15.4 channel. That upstream example does not establish support for this board's P4 host or the existing ESP-Hosted C6 image.

## Newer IDF Multipan Build-Only Probe

- The exact IDF master commit above includes `examples/openthread/ot_zb_multipan`; the sample README lists ESP32, C3, C5, C6 and S3 hosts, but not P4. It requires a separate RCP over UART. The RCP can receive on only one 802.15.4 channel at a time, so Zigbee and Thread must share a channel.
- A temporary P4 host copy, augmented only with ESP-Hosted Wi-Fi dependencies (`esp_hosted` 3.1.0 and `esp_wifi_remote` 1.6.5), built successfully with Zigbee 2.0.5 and OpenThread multipan enabled. Image: 1,873,840 bytes; SHA-256 `bdab93d5f8d48963b7b3147579504f9287e6b2b849e052fbd7026b2280d2d3d0`.
- A temporary C6 CP copy combining the existing ESP-Hosted Wi-Fi feature with the standard UART multipan RCP startup also built. The effective config enabled ESP-Hosted Wi-Fi, `CONFIG_OPENTHREAD_RCP_UART`, `CONFIG_OPENTHREAD_MULTIPAN_RCP_ENABLE`, `CONFIG_IEEE802154_MULTI_PAN_ENABLE`, and software coexistence. Image: 1,309,168 bytes; SHA-256 `81f121f94760782958606afe471b40fea30585f8d688629c6c58b3fdb87c2ee4`.
- These are compile-only probes from temporary copies, not product firmware. No image was flashed. The P4 example UART defaults include GPIO5, which is already reserved for LCD reset in the board HMI; the actual C6-to-P4 UART connector/net mapping is unverified. The combined C6 uses one RF path for Wi-Fi and 802.15.4, and coexistence/performance was not measured.
- The sample provides Zigbee coordinator and Thread Border Router stacks, not a Matter Bridge. Home Assistant endpoint discovery still needs the P4 Matter bridge, commissioning and integration tests. UniFi Network would see the hub as an IP client, not each IP-less Zigbee/Thread endpoint.

## IDF 6.2 Product-Port Build Status

- The user approved an isolated IDF 6.2 product source port, initially build-only. Later, only the Wi-Fi-only default P4 image was explicitly authorized and flashed; see the [P4 flash audit](2026-11-07-p4-esp-hosted-flash.md). The repository P4/C6 component manifests now require IDF `6.2.0`; the exact build checkout remains master commit `0e6b480f858c69862ffb6001d96e9eef74599a6e` (reported v6.2.0, GCC 16.1.0), not a repository-pinned SDK checkout.
- The P4 source now includes an opt-in `MultipanService`, LCD Thread/Zigbee status and confirmation-gated Zigbee network formation / bounded join-window actions. The local HTTP diagnostics route remains read-only and exposes status/countdown fields only; it does not return EUI-64 values, network credentials or keys. Audit records are committed before network formation and permit-join changes.
- Both the P4 default profile (`CONFIG_HUB_MULTIPAN_ENABLE=n`) and the active multipan profile compiled successfully after integration. The active compile used a temporary overlay assigning P4 GPIO26/27 only to resolve UART configuration for compilation. Those nets are user-reported, shared with UART1/RS485, UNVERIFIED and RESERVED; this overlay is not a wiring recommendation and must not be flashed or used on hardware.
- A combined C6 ESP-Hosted Wi-Fi CP + UART multipan RCP image had previously compiled with UART0, console disabled and coexistence enabled. It was not flashed. CN2's user-reported UART0/BOOT pins are unverified and remain a service/programming connector, not approval to use or flash the port.
- The current `firmware/c6` product profile builds when defaults are loaded in this order: `sdkconfig.defaults`, `sdkconfig.defaults.esp32c6`, `sdkconfig.defaults.multipan`. An initial probe omitted the base defaults and therefore was not the product CP/Wi-Fi configuration; its duplicate-symbol linker failure was not reproduced with the correct profile.
- Current product image: `/tmp/hub_c6_product_idf62_20261107/build_multipan_product/hub_c6_esp_hosted_coprocessor.bin`, 1,312,720 bytes, SHA-256 `69f6e8c7d6e6cf003986148fbcead6462654a9de4fbb74dbf355a999614410f4`. It enables ESP-Hosted Wi-Fi CP, UART multipan RCP/coexistence, and disables the C6 console. The user authorized and the P4-hosted OTA transferred this exact image; the updater audit verified it after reboot. The configured OTA slot is 0x1C0000 bytes, but the integrated C6 flash capacity was not independently probed. No RF/interoperability test was performed.
- The running P4 product profile is Wi-Fi-only even though the C6 multipan image is now installed. `GET /api/status` reports `multipan_state=disabled`; runtime RCP traffic, P4/C6 UART electrical compatibility, RF coexistence, Thread IPv6 routing, Zigbee pairing and device discovery were not tested.
- Thread service startup can resume a stored operational dataset, but the product does not yet provision a Thread dataset or commissioner. The current Zigbee announcement counter is a bounded in-RAM diagnostic, not a persistent device registry. The Matter Bridge and Home Assistant discovery are not implemented.

## Transport Option Check

- The ESP-Hosted OpenThread RCP feature supports Spinel over the existing ESP-Hosted bus (including SDIO) when both P4 host and C6 CP enable the custom/hosted OpenThread transport. This is a documented no-extra-wire path for OpenThread/Thread-only; it is not the Zigbee coordinator path.
- ESP-IDF 6.2's `esp_radio_spinel_multipan_init()` API documents and enforces `RADIO_MODE_UART_RCP`; it returns `ESP_ERR_NOT_SUPPORTED` for other radio modes. The `ot_zb_multipan` example likewise uses one UART RCP shared by OpenThread and Zigbee. Therefore the current supported Zigbee+Thread multipan candidate requires a distinct C6 UART0-to-P4-UART connection in addition to the existing SDIO Wi-Fi link. No build or flash variant can remove that transport requirement without changing the stack/backend architecture.
- The user-reported P4 GPIO16/17/18 mapping conflicts with the active SDIO bus (D2/D3/CLK); GPIO10 is also assigned to ES8311 I2S LRCK. The vendor board reference routes C6 UART0/boot signals to JP1 pins 20/22/24/26; those are connector nets, not P4 GPIO identifiers. Until a non-conflicting P4 UART mapping is verified, no P4 multipan host image can be safely enabled and Zigbee discovery cannot start.
- No Zigbee scan was attempted after the C6 OTA. The P4 runtime continues to report `multipan_state=disabled`; the successful C6 firmware transfer only validates OTA, not UART RCP traffic or Zigbee operation.

## SDIO-Native C6 Zigbee Coordinator Probe

- A separate opt-in architecture was prototyped to avoid UART: C6 runs the native Zigbee coordinator and native 802.15.4 radio alongside ESP-Hosted Wi-Fi CP; P4 and C6 exchange bounded Peer Data messages over SDIO. This is not the installed UART multipan RCP image and does not provide Thread.
- C6 test image: `/tmp/hub_c6_sdio_zigbee_probe/build_zigbee_sdio/hub_c6_esp_hosted_coprocessor.bin`, 1,418,944 bytes, SHA-256 `7b374d75dd42f1417050e55c048f53eb30de6487a5a754695981808bd033daec`. It enables native Zigbee, Wi-Fi/802.15.4 coexistence, ESP-Hosted Peer Data, a five-second status heartbeat and disables the C6 UART console. BDB setup follows the upstream commissioning pattern and formation errors are reported. The matching P4 host builds; both images remain unflashed for this architecture.
- `c6_zigbee_search CONFIRM-C6-ZIGBEE-SEARCH` is intended to commit an audit record, send a command over SDIO, then form/join the C6 Zigbee network and open permit-join for 180 seconds. Status reports state and a bounded in-RAM count, not device identifiers. This route requires end-to-end flashing, a real Zigbee end device, coexistence, reboot and join-timeout testing before it can be considered functional.

The accepted UART multipan design and the SDIO-native C6 Zigbee prototype are alternative architectures; only the latter avoids a P4-C6 UART link. It moves coordinator ownership to C6 and is not yet a production decision.

## Discovery Semantics

- Zigbee end devices join a Zigbee coordinator; they do not join a Thread network. A Matter Bridge can expose supported Zigbee devices to Home Assistant over IP after the P4 Matter bridge is implemented and commissioned.
- Native Thread devices require a Thread Border Router and a Matter controller/integration. A TBR is not a Zigbee coordinator.
- UniFi Network can list this hub as an IP/Wi-Fi client. It does not list IP-less Zigbee or Thread endpoints as separate network clients. Home Assistant is the appropriate UI for bridged Matter endpoints.

## Decision / Blockers

- The source and running P4 image target IDF 6.2.0. The exact SDK checkout and dependencies must be pinned before a reproducible release. Read-only `/api/c6/status` and serial `c6_status` are deployed and verified. The installed C6 image is the UART multipan RCP and its P4 multipan host remains disabled. The alternative C6-native Zigbee + SDIO Peer Data images build but are not flashed; no Zigbee scan has run.
- A C6-only standalone Thread BR build is possible, but it does not preserve the P4's current Wi-Fi coprocessor contract and does not provide Zigbee.
- The UART-RCP path requires verified non-conflicting UART nets and an enabled P4 multipan host. The SDIO-native C6 coordinator path avoids those UART pins but still requires flashing its exact P4/C6 test images, a real end-device join, coexistence/soak testing and recovery validation. Neither path provides Matter Bridge/HA discovery yet. H2 remains unflashed.
- H2 remains deferred as requested; do not use example UART pins as board assignments.