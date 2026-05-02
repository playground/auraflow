/**
 * Over-the-air firmware updates.
 *
 * The HTTP endpoint POST /ota {"url": "http://..."} hands a URL to
 * ota_start(), which spawns a task that:
 *   1. fetches the .bin via esp_https_ota (HTTP or HTTPS)
 *   2. writes it to the inactive OTA slot (ota_0 ↔ ota_1)
 *   3. flips otadata + esp_restart() on success
 *
 * Failures log and abort — the running image stays active. Rollback on
 * post-boot failure is not yet wired (CONFIG_BOOTLOADER_APP_ROLLBACK
 * + esp_ota_mark_app_valid_cancel_rollback) — add when we ship to
 * non-makers.
 *
 * The pure half is a no-op stub for now; everything meaningful is
 * ESP-IDF I/O.
 */
#pragma once

#include <stdbool.h>

#ifdef ESP_PLATFORM

/** Kick off an OTA from the given URL on a background task. Non-blocking;
 *  the device reboots ~10–60s later if the download succeeds. */
void ota_start(const char *url);

/** True while a previous ota_start() is still in flight. The HTTP handler
 *  uses this to reject overlapping requests with 409 Conflict. */
bool ota_in_progress(void);

#endif  /* ESP_PLATFORM */
