/**
 * ESP-IDF HTTP path for uplink — POST one buffered reading at a time
 * via esp_http_client, decode the response with cJSON, drive the buffer.
 */
#ifdef ESP_PLATFORM

#include <string.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "uplink.h"

static const char *TAG = "uplink";

/* ── HTTP capture: collect response body in a fixed buffer ─────── */

typedef struct {
    char  *buf;
    size_t size;       /* bytes filled */
    size_t capacity;   /* max bytes (excluding terminating NUL) */
} response_capture_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    response_capture_t *cap = (response_capture_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && cap != NULL && evt->data != NULL) {
        size_t avail = cap->capacity - cap->size;
        size_t copy  = ((size_t)evt->data_len < avail) ? (size_t)evt->data_len : avail;
        if (copy > 0) {
            memcpy(cap->buf + cap->size, evt->data, copy);
            cap->size += copy;
        }
    }
    return ESP_OK;
}

/* ── Response parser ──────────────────────────────────────────── */

static uplink_poll_config_t parse_response(const char *body)
{
    uplink_poll_config_t pc = { 0 };

    cJSON *root = cJSON_Parse(body);
    if (root != NULL && cJSON_IsObject(root)) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(root, "pollIntervalMs"))
                && cJSON_IsNumber(item)) {
            pc.poll_interval_ms = (int)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(root, "flowingPollIntervalMs"))
                && cJSON_IsNumber(item)) {
            pc.flowing_poll_interval_ms = (int)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(root, "idlePollIntervalMs"))
                && cJSON_IsNumber(item)) {
            pc.idle_poll_interval_ms = (int)item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(root, "configVersion"))
                && cJSON_IsNumber(item)) {
            pc.config_version = (int)item->valuedouble;
        }
    }
    cJSON_Delete(root);

    uplink_apply_poll_defaults(&pc);
    return pc;
}

/* ── POST one reading ─────────────────────────────────────────── */

/* Returns true on 2xx + parsed response. *out_pc is set on success. */
static bool post_one(const uplink_t *u,
                     const uplink_reading_t *reading,
                     int64_t now_ms,
                     uplink_poll_config_t *out_pc)
{
    char body[UPLINK_REQUEST_BODY_MAX];
    int  body_len = uplink_build_request_json(body, sizeof(body),
                                              &u->cfg, reading, now_ms);
    if (body_len < 0) {
        ESP_LOGE(TAG, "request body would overflow %d bytes", UPLINK_REQUEST_BODY_MAX);
        return false;
    }

    char url[256];
    int n = snprintf(url, sizeof(url),
                     "%s/internal/sensors/flow/readings", u->cfg.homehub_url);
    if (n < 0 || n >= (int)sizeof(url)) {
        ESP_LOGE(TAG, "homehub_url too long");
        return false;
    }

    char response[UPLINK_RESPONSE_BODY_MAX + 1];
    response_capture_t cap = {
        .buf      = response,
        .size     = 0,
        .capacity = UPLINK_RESPONSE_BODY_MAX,
    };

    esp_http_client_config_t hcfg = {
        .url           = url,
        .method        = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data     = &cap,
        .timeout_ms    = (u->cfg.timeout_ms > 0) ? u->cfg.timeout_ms : 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&hcfg);
    if (client == NULL) return false;

    esp_http_client_set_header(client, "Content-Type",   "application/json");
    esp_http_client_set_header(client, "X-Internal-Key", u->cfg.internal_api_key);
    esp_http_client_set_post_field(client, body, body_len);

    esp_err_t err    = esp_http_client_perform(client);
    int       status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "POST failed: %s", esp_err_to_name(err));
        return false;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "POST returned HTTP %d", status);
        return false;
    }

    response[cap.size] = '\0';
    *out_pc = parse_response(response);
    return true;
}

/* ── Public push / flush ──────────────────────────────────────── */

uplink_poll_config_t uplink_flush(uplink_t *u, int64_t now_ms)
{
    if (u == NULL) return UPLINK_DEFAULT_POLL_CONFIG;

    while (!ring_buffer_is_empty(&u->buffer)) {
        uplink_reading_t r;
        if (!ring_buffer_peek(&u->buffer, &r)) break;

        uplink_poll_config_t pc;
        if (!post_one(u, &r, now_ms, &pc)) {
            /* Failed — leave the reading in place and bail out. */
            return u->last_poll_config;
        }
        ring_buffer_shift(&u->buffer, NULL);
        u->last_poll_config = pc;
    }
    return u->last_poll_config;
}

uplink_poll_config_t uplink_push(uplink_t *u,
                                 const uplink_reading_t *reading,
                                 int64_t now_ms)
{
    if (u == NULL || reading == NULL) return UPLINK_DEFAULT_POLL_CONFIG;

    /* Stamp enqueue time; the serializer uses (now_ms - enqueued_at_ms)
     * to compute tOffsetMs at flush time. */
    uplink_reading_t stamped = *reading;
    stamped.enqueued_at_ms   = now_ms;

    ring_buffer_push(&u->buffer, &stamped);
    return uplink_flush(u, now_ms);
}

#endif  /* ESP_PLATFORM */
