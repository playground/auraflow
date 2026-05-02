/**
 * NVS-backed device configuration.
 *
 * Holds the values written by provisioning (Wi-Fi creds, HomeHub URL,
 * internal API key, sensor ID, Modbus word order). Loaded once at boot,
 * saved by the WebSerial provisioning protocol when a PROVISION:{...}
 * line is accepted.
 *
 * Pure helpers (parse_word_order, is_provisioned, word_order_to_string)
 * are testable on the host. The actual NVS I/O lives in nvs_config_esp.c
 * and is only built/declared on ESP_PLATFORM.
 *
 * C port of src/firmware/ts/nvs.ts.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "tuf2000m.h"

#define NVS_CONFIG_SSID_MAX_LEN          32
#define NVS_CONFIG_PASSWORD_MAX_LEN      64
#define NVS_CONFIG_URL_MAX_LEN           128
#define NVS_CONFIG_KEY_MAX_LEN           64
#define NVS_CONFIG_SENSOR_ID_MAX_LEN     64
#define NVS_CONFIG_IP_STR_MAX_LEN        15   /* "255.255.255.255" */

typedef struct {
    char                  wifi_ssid[NVS_CONFIG_SSID_MAX_LEN + 1];
    char                  wifi_password[NVS_CONFIG_PASSWORD_MAX_LEN + 1];
    char                  homehub_url[NVS_CONFIG_URL_MAX_LEN + 1];
    char                  internal_api_key[NVS_CONFIG_KEY_MAX_LEN + 1];
    char                  sensor_id[NVS_CONFIG_SENSOR_ID_MAX_LEN + 1];
    tuf2000m_word_order_t word_order;

    /* Optional static IP. Empty strings → use DHCP. All three must be set
     * together (validated by provisioning_validate_struct). */
    char                  static_ip[NVS_CONFIG_IP_STR_MAX_LEN + 1];
    char                  static_gateway[NVS_CONFIG_IP_STR_MAX_LEN + 1];
    char                  static_netmask[NVS_CONFIG_IP_STR_MAX_LEN + 1];
} nvs_config_t;

/* ── Pure helpers ─────────────────────────────────────────────── */

/** True if all required fields are non-empty (wifi_password is optional — open networks). */
bool nvs_config_is_provisioned(const nvs_config_t *cfg);

/** True if the three static-IP fields are all populated (caller wants static IP). */
bool nvs_config_uses_static_ip(const nvs_config_t *cfg);

/** Parse "low-word-first" / "high-word-first"; defaults to LOW on anything else. */
tuf2000m_word_order_t nvs_config_parse_word_order(const char *s);

/** Inverse of parse_word_order. Returns a static string, never NULL. */
const char *nvs_config_word_order_to_string(tuf2000m_word_order_t wo);

/* ── ESP-IDF NVS I/O ───────────────────────────────────────────── */
#ifdef ESP_PLATFORM

/**
 * Initialize NVS (idempotent). Erases + recreates if the partition is
 * incompatible (NEW_VERSION_FOUND / NO_FREE_PAGES). Call once at boot.
 */
bool nvs_config_init(void);

/**
 * Load all keys into *out. Missing keys → empty strings; missing
 * wordOrder → TUF2000M_DEFAULT_WORD_ORDER.
 */
bool nvs_config_load(nvs_config_t *out);

/** Save the populated string fields + word_order. Empty fields are not written. */
bool nvs_config_save(const nvs_config_t *cfg);

/** Erase the entire AuraFlow NVS namespace. */
bool nvs_config_reset(void);

/* ── Cached poll cadence ──────────────────────────────────────────
 * The cadence is HomeHub-authoritative — every push response carries
 * pollIntervalMs / flowingPollIntervalMs / idlePollIntervalMs and
 * uplink applies them at runtime. We mirror those three values to NVS
 * so that after a reboot we resume at the last-known cadence instead
 * of falling back to firmware defaults until HomeHub responds again.
 *
 * Stored under separate keys (not the main config blob) because they
 * change orders of magnitude more often. */

/** Load cached cadence into the three out params. Returns true only
 *  if all three keys are present — partial reads leave outs untouched. */
bool nvs_config_poll_load(int *poll_ms, int *flowing_ms, int *idle_ms);

/** Persist the cadence triple. Cheap; caller should compare-and-skip
 *  to avoid rewriting unchanged values on every push. */
bool nvs_config_poll_save(int poll_ms, int flowing_ms, int idle_ms);

#endif  /* ESP_PLATFORM */
