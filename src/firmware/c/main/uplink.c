/**
 * Pure helpers for uplink — JSON request body serializer, poll-config
 * default merging, ring buffer init. The HTTP path + cJSON response
 * decoding live in uplink_esp.c.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "uplink.h"

const uplink_poll_config_t UPLINK_DEFAULT_POLL_CONFIG = {
    .poll_interval_ms         = UPLINK_DEFAULT_POLL_INTERVAL_MS,
    .flowing_poll_interval_ms = UPLINK_DEFAULT_FLOWING_POLL_INTERVAL_MS,
    .idle_poll_interval_ms    = UPLINK_DEFAULT_IDLE_POLL_INTERVAL_MS,
    .config_version           = UPLINK_DEFAULT_CONFIG_VERSION,
};

/* Helper: append `chunk` to buf at *len. Returns false on overflow. */
static bool append(char *buf, size_t buf_size, int *len, const char *fmt, ...)
{
    int remaining = (int)buf_size - *len;
    if (remaining <= 0) return false;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + *len, (size_t)remaining, fmt, args);
    va_end(args);

    if (n < 0 || n >= remaining) return false;
    *len += n;
    return true;
}

int uplink_build_request_json(char *buf, size_t buf_size,
                              const uplink_config_t *cfg,
                              const uplink_reading_t *reading,
                              int64_t now_ms)
{
    if (buf == NULL || buf_size == 0 || cfg == NULL || reading == NULL) {
        return -1;
    }

    int64_t t_offset_ms = now_ms - reading->enqueued_at_ms;
    if (t_offset_ms < 0) t_offset_ms = 0;

    int len = 0;

    /* Required: sensor_id + rate_m3h + tOffsetMs */
    if (!append(buf, buf_size, &len,
                "{\"sensorId\":\"%s\",\"rateM3h\":%g,\"tOffsetMs\":%lld",
                cfg->sensor_id, (double)reading->rate_m3h, (long long)t_offset_ms)) {
        return -1;
    }

    if (reading->has_total_m3) {
        if (!append(buf, buf_size, &len, ",\"totalM3\":%g", (double)reading->total_m3))
            return -1;
    }
    if (reading->has_signal_quality) {
        if (!append(buf, buf_size, &len, ",\"signalQuality\":%d", reading->signal_quality))
            return -1;
    }
    if (reading->has_rssi) {
        if (!append(buf, buf_size, &len, ",\"rssi\":%d", reading->rssi))
            return -1;
    }
    if (reading->has_uptime_sec) {
        if (!append(buf, buf_size, &len, ",\"uptimeSec\":%lld",
                    (long long)reading->uptime_sec))
            return -1;
    }
    if (reading->firmware_version[0] != '\0') {
        if (!append(buf, buf_size, &len, ",\"firmwareVersion\":\"%s\"",
                    reading->firmware_version))
            return -1;
    }
    if (reading->mac[0] != '\0') {
        if (!append(buf, buf_size, &len, ",\"mac\":\"%s\"", reading->mac))
            return -1;
    }
    if (reading->boot_reason[0] != '\0') {
        if (!append(buf, buf_size, &len, ",\"bootReason\":\"%s\"", reading->boot_reason))
            return -1;
    }
    /* Always emitted — server uses it to decide whether to insert into
     * flow_readings or treat the POST as a diagnostic-only heartbeat. */
    if (!append(buf, buf_size, &len, ",\"meterReachable\":%s",
                reading->meter_reachable ? "true" : "false")) {
        return -1;
    }

    if (!append(buf, buf_size, &len, "}")) return -1;
    return len;
}

void uplink_apply_poll_defaults(uplink_poll_config_t *pc)
{
    if (pc == NULL) return;
    if (pc->poll_interval_ms         <= 0) pc->poll_interval_ms         = UPLINK_DEFAULT_POLL_INTERVAL_MS;
    if (pc->flowing_poll_interval_ms <= 0) pc->flowing_poll_interval_ms = UPLINK_DEFAULT_FLOWING_POLL_INTERVAL_MS;
    if (pc->idle_poll_interval_ms    <= 0) pc->idle_poll_interval_ms    = UPLINK_DEFAULT_IDLE_POLL_INTERVAL_MS;
    if (pc->config_version           <= 0) pc->config_version           = UPLINK_DEFAULT_CONFIG_VERSION;
}

void uplink_init(uplink_t *u, const uplink_config_t *cfg)
{
    if (u == NULL || cfg == NULL) return;
    memset(u, 0, sizeof(*u));
    u->cfg              = *cfg;
    u->last_poll_config = UPLINK_DEFAULT_POLL_CONFIG;
    ring_buffer_init(&u->buffer, u->buffer_storage,
                     sizeof(uplink_reading_t), UPLINK_BUFFER_CAPACITY);
}

size_t uplink_pending(const uplink_t *u)
{
    return (u == NULL) ? 0 : ring_buffer_length(&u->buffer);
}
