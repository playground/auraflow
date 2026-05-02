/**
 * HTTP uplink to HomeHub.
 *
 * POSTs each reading to {homehub_url}/internal/sensors/flow/readings
 * with `X-Internal-Key`. On any failure the reading is buffered in a
 * ring (~5 min @ 5 s cadence) and retried on the next push, oldest first.
 *
 * Server responses carry the desired poll cadence — uplink_push() returns
 * the latest received poll config so the orchestrator can adapt timing
 * without an on-device control plane.
 *
 * Pure helpers (JSON serializer, default poll config, t_offset_ms math)
 * are host-testable. The HTTP path + cJSON response parsing live in
 * uplink_esp.c.
 *
 * C port of src/firmware/ts/uplink.ts.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ring_buffer.h"

#define UPLINK_DEFAULT_POLL_INTERVAL_MS          5000
#define UPLINK_DEFAULT_FLOWING_POLL_INTERVAL_MS  5000
#define UPLINK_DEFAULT_IDLE_POLL_INTERVAL_MS     30000
#define UPLINK_DEFAULT_CONFIG_VERSION            1

#define UPLINK_BUFFER_CAPACITY                   60      /* ~5 min @ 5 s cadence */
#define UPLINK_REQUEST_BODY_MAX                  768
#define UPLINK_RESPONSE_BODY_MAX                 384

/* ── Configuration ────────────────────────────────────────────── */

typedef struct {
    char homehub_url[129];
    char internal_api_key[65];
    char sensor_id[65];
    int  timeout_ms;          /* per-request timeout; 0 → use 5000 default */
} uplink_config_t;

/* ── Reading payload ──────────────────────────────────────────── */

/* Optional fields use boolean flags rather than sentinel values to
 * keep the JSON serializer's "include this key?" check unambiguous. */
typedef struct {
    float    rate_m3h;                    /* required */

    bool     has_total_m3;
    float    total_m3;

    bool     has_signal_quality;
    int      signal_quality;

    bool     has_rssi;
    int      rssi;

    bool     has_uptime_sec;
    int64_t  uptime_sec;

    /* False when the Modbus read of the meter failed (e.g. meter not yet
     * wired, RS485 fault, sensor offline). Set rate_m3h = 0 in that case
     * — server skips the readings INSERT and the leak engine, but still
     * refreshes diagnostics so we see the device is alive and OTA-able. */
    bool     meter_reachable;

    char     firmware_version[16];        /* empty → omit */
    char     mac[18];                     /* empty → omit */
    char     boot_reason[16];             /* empty → omit; "power"/"software"/"watchdog"/"unknown" */

    int64_t  enqueued_at_ms;              /* set by uplink_push; used to compute tOffsetMs at flush */
} uplink_reading_t;

/* ── Poll config ──────────────────────────────────────────────── */

typedef struct {
    int poll_interval_ms;
    int flowing_poll_interval_ms;
    int idle_poll_interval_ms;
    int config_version;
} uplink_poll_config_t;

extern const uplink_poll_config_t UPLINK_DEFAULT_POLL_CONFIG;

/* ── Pure helpers ─────────────────────────────────────────────── */

/**
 * Serialize a request body for one reading. Includes sensor_id, rate_m3h,
 * tOffsetMs (computed from `now_ms - reading->enqueued_at_ms`, clamped to 0)
 * and any optional fields where their `has_*` flag is set / string is non-empty.
 *
 * @return number of bytes written (excluding NUL), or -1 on overflow.
 */
int uplink_build_request_json(char *buf, size_t buf_size,
                              const uplink_config_t *cfg,
                              const uplink_reading_t *reading,
                              int64_t now_ms);

/**
 * Apply UPLINK_DEFAULT_POLL_CONFIG to any field of *pc that is <= 0.
 * Used after JSON-decoding the server response to fill in missing fields.
 */
void uplink_apply_poll_defaults(uplink_poll_config_t *pc);

/* ── Buffered uplink (state machine) ──────────────────────────── */

typedef struct {
    uplink_config_t       cfg;
    ring_buffer_t         buffer;
    uplink_reading_t      buffer_storage[UPLINK_BUFFER_CAPACITY];
    uplink_poll_config_t  last_poll_config;
} uplink_t;

void uplink_init(uplink_t *u, const uplink_config_t *cfg);

/**
 * Number of readings currently buffered (not yet acknowledged by HomeHub).
 * Useful for diagnostics. Always 0 right after a successful flush.
 */
size_t uplink_pending(const uplink_t *u);

/* ── ESP-IDF I/O ──────────────────────────────────────────────── */
#ifdef ESP_PLATFORM

/**
 * Enqueue a reading at `now_ms`, then flush as much as possible.
 * On every successful POST the response's poll config replaces
 * u->last_poll_config. Returns the most recent successfully-received
 * poll config (or the previously-cached one if all attempts failed).
 *
 * Never throws / never blocks the caller longer than `timeout_ms` per
 * pending reading. Failed reads stay queued for the next push.
 */
uplink_poll_config_t uplink_push(uplink_t *u,
                                 const uplink_reading_t *reading,
                                 int64_t now_ms);

/** Drain the buffer to HomeHub without enqueueing a new reading. */
uplink_poll_config_t uplink_flush(uplink_t *u, int64_t now_ms);

#endif  /* ESP_PLATFORM */
