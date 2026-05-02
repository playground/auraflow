/**
 * Consumer-facing first-boot provisioning over Wi-Fi.
 *
 * When the device is unprovisioned, this module:
 *   1. Starts soft-AP mode with SSID "AuraFlow-Setup-XXXX" (XXXX = last
 *      4 of the base MAC), open auth.
 *   2. Spawns a tiny UDP/53 listener that answers every DNS query with
 *      the AP's IP — so iOS/Android phones detect a captive portal as
 *      soon as they join.
 *   3. Starts the HTTP server in portal mode (see http_config_start_portal)
 *      which serves a mobile-friendly form at GET / and accepts
 *      POST /config to save NVS + reboot.
 *
 * Coexists with the UART0 PROVISION listener — whichever surface lands
 * a valid config first wins; the device reboots and ignores both
 * surfaces on next boot.
 */
#pragma once

#ifdef ESP_PLATFORM

/** Bring up AP + DNS hijack + portal HTTP server. Idempotent. */
void captive_portal_start(void);

#endif  /* ESP_PLATFORM */
