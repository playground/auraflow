/**
 * Pure-logic helpers for nvs_config. Host-safe; no ESP-IDF dependencies.
 * The NVS I/O lives in nvs_config_esp.c.
 */
#include <string.h>

#include "nvs_config.h"

bool nvs_config_is_provisioned(const nvs_config_t *cfg)
{
    if (cfg == NULL) return false;
    return cfg->wifi_ssid[0]        != '\0'
        && cfg->homehub_url[0]      != '\0'
        && cfg->internal_api_key[0] != '\0'
        && cfg->sensor_id[0]        != '\0';
}

tuf2000m_word_order_t nvs_config_parse_word_order(const char *s)
{
    if (s != NULL && strcmp(s, "high-word-first") == 0) {
        return TUF2000M_HIGH_WORD_FIRST;
    }
    return TUF2000M_LOW_WORD_FIRST;
}

const char *nvs_config_word_order_to_string(tuf2000m_word_order_t wo)
{
    return (wo == TUF2000M_HIGH_WORD_FIRST) ? "high-word-first" : "low-word-first";
}
