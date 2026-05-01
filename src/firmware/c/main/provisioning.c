/**
 * Pure-logic helpers for provisioning. Host-safe.
 * The cJSON + UART driver code lives in provisioning_esp.c.
 */
#include <string.h>

#include "provisioning.h"

const char *provisioning_strip_prefix(const char *line)
{
    if (line == NULL) return NULL;

    /* Skip any leading whitespace (incl. CR/LF if a previous line is buffered). */
    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') {
        line++;
    }

    static const char PREFIX[]   = "PROVISION:";
    static const size_t PREFIX_N = sizeof(PREFIX) - 1;

    if (strncmp(line, PREFIX, PREFIX_N) != 0) {
        return NULL;
    }
    return line + PREFIX_N;
}

bool provisioning_is_valid_word_order(const char *s)
{
    if (s == NULL || s[0] == '\0') {
        return true;   /* absent → default (low-word-first) */
    }
    return strcmp(s, "low-word-first") == 0
        || strcmp(s, "high-word-first") == 0;
}

provision_result_t provisioning_validate_struct(const nvs_config_t *cfg)
{
    provision_result_t r;
    r.status       = PROVISION_OK;
    r.field_name[0] = '\0';

    if (cfg == NULL) {
        r.status = PROVISION_ERR_EMPTY_FIELD;
        strncpy(r.field_name, "(null cfg)", sizeof(r.field_name) - 1);
        return r;
    }

    /* Order matches the TS REQUIRED_FIELDS list — first missing wins. */
    struct { const char *name; const char *value; } required[] = {
        { "wifiSsid",       cfg->wifi_ssid        },
        { "wifiPassword",   cfg->wifi_password    },
        { "homehubUrl",     cfg->homehub_url      },
        { "internalApiKey", cfg->internal_api_key },
        { "sensorId",       cfg->sensor_id        },
    };

    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        if (required[i].value[0] == '\0') {
            r.status = PROVISION_ERR_EMPTY_FIELD;
            strncpy(r.field_name, required[i].name, sizeof(r.field_name) - 1);
            r.field_name[sizeof(r.field_name) - 1] = '\0';
            return r;
        }
    }

    /* Static IP fields are optional, but ALL-OR-NOTHING. If any of the
     * three is set, all three must be set — anything else is a partial
     * config that would leave the device in a confusing half-state. */
    struct { const char *name; const char *value; } static_ip[] = {
        { "staticIp",      cfg->static_ip      },
        { "staticGateway", cfg->static_gateway },
        { "staticNetmask", cfg->static_netmask },
    };

    bool any_set = false;
    bool all_set = true;
    for (size_t i = 0; i < sizeof(static_ip) / sizeof(static_ip[0]); i++) {
        bool set = (static_ip[i].value[0] != '\0');
        any_set = any_set || set;
        all_set = all_set && set;
    }
    if (any_set && !all_set) {
        /* Name the first missing field so the error is actionable. */
        for (size_t i = 0; i < sizeof(static_ip) / sizeof(static_ip[0]); i++) {
            if (static_ip[i].value[0] == '\0') {
                r.status = PROVISION_ERR_EMPTY_FIELD;
                strncpy(r.field_name, static_ip[i].name, sizeof(r.field_name) - 1);
                r.field_name[sizeof(r.field_name) - 1] = '\0';
                return r;
            }
        }
    }

    return r;
}
