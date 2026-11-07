# Security, storage, updates and operations

## Secrets and identity

- Separate Zigbee trust center/network key and PAN identity, Matter fabric credentials and certificates, Wi-Fi credentials, API auth and device registry.
- Use appropriate encrypted storage/protection mechanisms available on the exact P4 hardware and IDF revision; do not assert flash encryption/secure boot provisioned before validation.
- Never print Zigbee/Matter keys or raw provisioning QR payloads to normal logs. Use short-lived pairing windows, limit web/API exposure to trusted LAN with authentication.
- Document production Matter attestation and compliance separately; example development credentials are **not** production credentials.
- Validate mDNS, IPv6 and Matter multicast behavior on routed/VLAN networks; not just DHCP IPv4.

## Persistence plan

- NVS for small configuration/state/secret handles and stable endpoint mappings; optionally dedicated encrypted partition after hardware validation.
- SD card for optional large diagnostic history, backup and media; hub **must boot and pair without SD card**.
- Schema version fields; versioned migration with power-cut tests.
- Persist Zigbee coordinator identity/network data and Matter endpoint allocation so routine reboot/OTA doesn't force re-pairing.
- Rate-limit sensor persistence and batch writes to protect flash endurance.

## OTA and reset scopes

- P4 application dual-slot OTA + rollback, subject to validated partition table/flash budget.
- H2 RCP OTA/reflash is separate, with compatibility handshake and rollback/recovery design; never simultaneously change host and RCP without a safe staged plan.
- C6 ESP-Hosted firmware must be independently versioned, preserved and updated only when tested.
- Matter fabric removal != Zigbee factory reset. Provide UI confirmation and maintenance backup/export policy.
- Implement safe mode after crash-loop; local diagnostics must remain available.

## Telemetry and incident handling

- Structured ring buffer logs with timestamps, component, severity, error code, correlation ID. Redact EUI-64/MAC/user identifiers where not necessary.
- Monitor free internal RAM, PSRAM, queue drops, UART frame errors, RCP firmware versions, Zigbee neighbor quality, Matter endpoint count, Wi-Fi RSSI and mDNS status.
- Counter-based watchdog recovery; bounded restarts, non-destructive defaults.
- Do not transmit personal telemetry to external services by default.
