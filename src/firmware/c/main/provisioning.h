/**
 * First-boot provisioning over UART0.
 *
 * Listens for a single-line input of the form:
 *
 *   PROVISION:{"wifiSsid":"...","wifiPassword":"...","homehubUrl":"...",
 *              "internalApiKey":"...","sensorId":"...","wordOrder":"low-word-first"}\n
 *
 * On success: writes NVS, traces "OK\n", and reboots into normal operation.
 * On failure: traces "ERR:<reason>\n" and keeps listening.
 *
 * Periodic "READY:auraflow-provision-v1\n" heartbeats let the web flasher
 * detect that we're in provisioning mode.
 *
 * Pure helpers (strip_prefix, is_valid_word_order, validate_struct) are
 * host-testable. The full pipeline + UART task lives in provisioning_esp.c
 * and is only built into the firmware.
 *
 * C port of src/firmware/ts/provisioning.ts.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "nvs_config.h"

typedef enum {
    PROVISION_OK = 0,
    PROVISION_ERR_NO_PREFIX,         /* line didn't start with "PROVISION:" */
    PROVISION_ERR_INVALID_JSON,      /* cJSON couldn't parse the body */
    PROVISION_ERR_NOT_OBJECT,        /* JSON parsed but isn't an object */
    PROVISION_ERR_MISSING_FIELD,     /* required field absent */
    PROVISION_ERR_EMPTY_FIELD,       /* required field present but empty */
    PROVISION_ERR_BAD_WORD_ORDER,    /* wordOrder not one of the two allowed values */
} provision_status_t;

typedef struct {
    provision_status_t status;
    /* For MISSING/EMPTY/BAD_WORD_ORDER: the JSON key at fault. */
    char field_name[32];
} provision_result_t;

/* ── Pure helpers ─────────────────────────────────────────────── */

/**
 * Skip leading whitespace and the literal "PROVISION:" prefix.
 * Returns a pointer into the input string (no allocation), or NULL if
 * the prefix is missing.
 */
const char *provisioning_strip_prefix(const char *line);

/**
 * True if `s` is "low-word-first", "high-word-first", or NULL/empty
 * (treated as default = LOW). False otherwise.
 */
bool provisioning_is_valid_word_order(const char *s);

/**
 * Validate that the required nvs_config_t fields are non-empty.
 * wifi_password is NOT required (open networks). Returns the offending
 * field in `field_name` on EMPTY_FIELD.
 *
 * (Note: this only checks emptiness — it doesn't know about MISSING vs
 * EMPTY since both look identical in the pre-zeroed struct.)
 */
provision_result_t provisioning_validate_struct(const nvs_config_t *cfg);

/* ── ESP-IDF I/O ──────────────────────────────────────────────── */
#ifdef ESP_PLATFORM

/**
 * Parse a single PROVISION:{...} line and populate *out on success.
 * Combines strip_prefix + cJSON parse + per-field extraction + validation.
 */
provision_result_t provisioning_parse_line(const char *line, nvs_config_t *out);

/**
 * Start the UART0 listener task. Sends READY heartbeats every 5s,
 * processes incoming PROVISION:{...} lines, writes NVS on success,
 * and calls esp_restart() to boot into normal mode.
 */
void provisioning_start_listener(void);

#endif  /* ESP_PLATFORM */
