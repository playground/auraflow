/**
 * Wi-Fi connection manager. Auto-reconnect with exponential backoff +
 * anti-flap throttling — if the device cycles up/down too quickly (5+
 * connects within an hour) we sleep 5 minutes before retrying instead
 * of busy-looping on a misconfigured network.
 *
 * Pure helpers (history tracking, backoff math) are host-testable.
 * The esp_wifi event-driven state machine lives in wifi_mgr_esp.c.
 *
 * C port of src/firmware/ts/wifi.ts.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WIFI_MGR_FLAP_WINDOW_MS      (60 * 60 * 1000)    /* 1 hour */
#define WIFI_MGR_FLAP_THRESHOLD      5                   /* connects/hour */
#define WIFI_MGR_THROTTLE_MS         (5 * 60 * 1000)     /* sleep when throttling */
#define WIFI_MGR_INITIAL_BACKOFF_MS  1000
#define WIFI_MGR_MAX_BACKOFF_MS      60000

/* ── Pure helpers ─────────────────────────────────────────────── */

/** Recent-connect history. Fixed-size — only the last FLAP_THRESHOLD entries matter. */
typedef struct {
    int64_t  times[WIFI_MGR_FLAP_THRESHOLD];   /* ms since some epoch (caller's choice) */
    size_t   count;                            /* 0..FLAP_THRESHOLD */
} wifi_mgr_history_t;

/** Initialize an empty history. */
void wifi_mgr_history_init(wifi_mgr_history_t *h);

/**
 * Append a new connect at `now_ms`. If at capacity, the oldest entry
 * is evicted (FIFO). Designed so should_throttle just needs to count
 * recent entries, not prune.
 */
void wifi_mgr_history_record(wifi_mgr_history_t *h, int64_t now_ms);

/**
 * True if the count of entries within the FLAP_WINDOW_MS preceding
 * `now_ms` is ≥ FLAP_THRESHOLD. Caller should pause reconnect attempts
 * for at least WIFI_MGR_THROTTLE_MS when true.
 */
bool wifi_mgr_history_should_throttle(const wifi_mgr_history_t *h, int64_t now_ms);

/**
 * Compute the next backoff delay given the current one, doubling it
 * with a cap at MAX_BACKOFF_MS. Caller starts at INITIAL_BACKOFF_MS.
 */
int wifi_mgr_next_backoff_ms(int current_ms);

/* ── ESP-IDF Wi-Fi state machine ──────────────────────────────── */
#ifdef ESP_PLATFORM

typedef void (*wifi_mgr_callback_t)(void);

typedef struct {
    const char         *ssid;
    const char         *password;          /* may be empty for open networks */
    wifi_mgr_callback_t on_up;             /* called once after IP_EVENT_STA_GOT_IP */
    wifi_mgr_callback_t on_down;           /* called on WIFI_EVENT_STA_DISCONNECTED */
} wifi_mgr_config_t;

/**
 * Configure esp_netif + esp_wifi, register event handlers, and start
 * connecting. Caller's callbacks fire from the default event-loop task.
 *
 * Internally tracks connect history and applies exponential backoff
 * on disconnect; if 5+ connects happen within an hour, throttles to
 * one attempt per 5 minutes until the rate normalizes.
 */
void wifi_mgr_start(const wifi_mgr_config_t *cfg);

#endif  /* ESP_PLATFORM */
