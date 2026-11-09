# ADR-0002: HTTP-only local Web UI

- **Status:** Accepted
- **Date:** 2026-11-08

## Context

The P4 local Web UI used a self-signed TLS certificate. Browser TLS alerts (`mbedtls_ssl_handshake` `-0x7780`) were observed while the P4 continued to report Wi-Fi connected and its HTTPS status API responded when certificate verification was bypassed. The product owner explicitly requested that Web UI TLS be disabled permanently.

## Decision

Serve the P4 Web UI and its APIs over HTTP only on port 80. Do not start an HTTPS listener, generate/validate a TLS identity during Web UI startup, or advertise `_https._tcp`. Keep password verification, rate limiting, one-time bootstrap/rotation, session expiry/revocation, same-origin checks, `HttpOnly`, `SameSite=Strict`, and Flash Encryption for stored credentials. The session cookie cannot use `Secure` on HTTP.

## Consequences

- HTTP Basic credentials, password changes, API requests, and session cookies are unencrypted and unauthenticated at the transport layer. A device on the same network can observe or modify them.
- Flash Encryption and password hashing protect data at rest; they do not protect network traffic.
- The Web UI must be used only on a trusted, isolated LAN. Do not expose port 80 through WAN port forwarding, guest Wi-Fi, or shared/untrusted VLANs. This development configuration is not suitable for production.
- If production or untrusted-network access is needed, restore TLS with a trusted certificate/CA or put the device behind an authenticated VPN/reverse proxy; do not treat HTTP as a secure replacement.
- This decision changes only the P4 Web UI transport. It does not change ESP-Hosted SDIO, Wi-Fi, Zigbee, Thread, or Matter protocol architecture.

## Alternatives

- Keep HTTPS and install/trust a certificate with the correct hostname/IP SAN.
- Use a VPN or trusted reverse proxy for remote access.

HTTP-only was selected by the user for this deployment despite its transport-security limitations.
