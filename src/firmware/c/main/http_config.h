/**
 * On-device HTTP server — minimum viable status page.
 *
 * Started after Wi-Fi gets an IP. Serves a single HTML page at GET /
 * showing static identity (sensor ID, homehub URL, firmware version)
 * plus runtime state (uptime, last reading rate, signal quality, RSSI,
 * pending upload count). The orchestrator pushes runtime updates via
 * http_config_update_status() each poll iteration.
 *
 * Future expansions on the same esp_http_server instance:
 *   GET /diag    — JSON version of the same data
 *   POST /config — change homehubUrl / static IP without re-provisioning
 *   plus the captive-portal handlers when Commit 2 lands (AP mode)
 *
 * Pure-side files would be tested on the host, but this module is
 * almost entirely ESP-IDF I/O — there isn't a meaningful pure subset
 * to extract yet.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef ESP_PLATFORM

typedef struct {
    char sensor_id[64];
    char homehub_url[128];
    char firmware_version[16];
} http_config_init_t;

typedef struct {
    bool   has_rate;
    float  rate_m3h;
    bool   has_signal_quality;
    int    signal_quality;
    bool   has_rssi;
    int    rssi;
    size_t pending_uploads;
} http_config_status_t;

/** Start the HTTP server in STA mode (status + edit + diag + ota + config).
 *  Idempotent — safe to call multiple times. */
void http_config_start(const http_config_init_t *init);

/** Start the HTTP server in captive-portal mode (mobile setup form +
 *  Wi-Fi scan + same POST /config + catch-all that returns the form so
 *  iOS/Android probes pop up the setup page). Used when unprovisioned;
 *  called from captive_portal_start. */
void http_config_start_portal(void);

/** Update the snapshot displayed on GET /. Cheap, called from poll_task. */
void http_config_update_status(const http_config_status_t *status);

#endif  /* ESP_PLATFORM */
