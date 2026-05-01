/**
 * Pure helpers for wifi_mgr — connect-history tracking + backoff math.
 * Host-safe; the esp_wifi state machine lives in wifi_mgr_esp.c.
 */
#include <string.h>

#include "wifi_mgr.h"

void wifi_mgr_history_init(wifi_mgr_history_t *h)
{
    if (h == NULL) return;
    memset(h, 0, sizeof(*h));
}

void wifi_mgr_history_record(wifi_mgr_history_t *h, int64_t now_ms)
{
    if (h == NULL) return;
    if (h->count == WIFI_MGR_FLAP_THRESHOLD) {
        /* At capacity — shift oldest off, append new at the tail. */
        memmove(h->times, h->times + 1,
                (WIFI_MGR_FLAP_THRESHOLD - 1) * sizeof(h->times[0]));
        h->times[WIFI_MGR_FLAP_THRESHOLD - 1] = now_ms;
    } else {
        h->times[h->count++] = now_ms;
    }
}

bool wifi_mgr_history_should_throttle(const wifi_mgr_history_t *h, int64_t now_ms)
{
    if (h == NULL) return false;
    int64_t cutoff = now_ms - WIFI_MGR_FLAP_WINDOW_MS;
    size_t recent = 0;
    for (size_t i = 0; i < h->count; i++) {
        if (h->times[i] > cutoff) recent++;
    }
    return recent >= WIFI_MGR_FLAP_THRESHOLD;
}

int wifi_mgr_next_backoff_ms(int current_ms)
{
    if (current_ms <= 0) return WIFI_MGR_INITIAL_BACKOFF_MS;
    long doubled = (long)current_ms * 2L;
    if (doubled > WIFI_MGR_MAX_BACKOFF_MS) return WIFI_MGR_MAX_BACKOFF_MS;
    return (int)doubled;
}
