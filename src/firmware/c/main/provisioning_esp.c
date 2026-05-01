/**
 * ESP-IDF UART listener + cJSON parsing for the PROVISION:{...} protocol.
 * Pure helpers (prefix stripping, struct validation) live in provisioning.c.
 */
#ifdef ESP_PLATFORM

#include <string.h>

#include "cJSON.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_config.h"
#include "provisioning.h"

#define PROVISION_UART          UART_NUM_0
#define PROVISION_BAUD          115200    /* must match ESP-IDF's default console baud
                                            (CONFIG_ESP_CONSOLE_UART_BAUDRATE) so printf
                                            and the host monitor stay readable */
#define PROVISION_RX_BUF        2048
#define PROVISION_LINE_MAX      1024
#define PROVISION_HEARTBEAT_MS  5000

static const char *TAG = "provision";

/* ── parse_line: strip prefix, parse JSON, copy fields, validate ── */

static void copy_field_if_present(cJSON *root, const char *key,
                                  char *dst, size_t dst_size)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item == NULL || !cJSON_IsString(item) || item->valuestring == NULL) {
        return;   /* leave dst as the caller's zeroed value */
    }
    size_t copy_len = strlen(item->valuestring);
    if (copy_len >= dst_size) copy_len = dst_size - 1;
    memcpy(dst, item->valuestring, copy_len);
    dst[copy_len] = '\0';
}

provision_result_t provisioning_parse_line(const char *line, nvs_config_t *out)
{
    provision_result_t r;
    r.status       = PROVISION_OK;
    r.field_name[0] = '\0';

    const char *json = provisioning_strip_prefix(line);
    if (json == NULL) {
        r.status = PROVISION_ERR_NO_PREFIX;
        return r;
    }

    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        r.status = PROVISION_ERR_INVALID_JSON;
        return r;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        r.status = PROVISION_ERR_NOT_OBJECT;
        return r;
    }

    /* Zero the output and copy each known string field. */
    memset(out, 0, sizeof(*out));
    out->word_order = TUF2000M_DEFAULT_WORD_ORDER;

    copy_field_if_present(root, "wifiSsid",       out->wifi_ssid,        sizeof(out->wifi_ssid));
    copy_field_if_present(root, "wifiPassword",   out->wifi_password,    sizeof(out->wifi_password));
    copy_field_if_present(root, "homehubUrl",     out->homehub_url,      sizeof(out->homehub_url));
    copy_field_if_present(root, "internalApiKey", out->internal_api_key, sizeof(out->internal_api_key));
    copy_field_if_present(root, "sensorId",       out->sensor_id,        sizeof(out->sensor_id));
    copy_field_if_present(root, "staticIp",       out->static_ip,        sizeof(out->static_ip));
    copy_field_if_present(root, "staticGateway",  out->static_gateway,   sizeof(out->static_gateway));
    copy_field_if_present(root, "staticNetmask",  out->static_netmask,   sizeof(out->static_netmask));

    /* wordOrder is optional but, if present, must be valid. */
    cJSON *wo = cJSON_GetObjectItem(root, "wordOrder");
    if (wo != NULL) {
        const char *wo_str = cJSON_IsString(wo) ? wo->valuestring : NULL;
        if (!provisioning_is_valid_word_order(wo_str)) {
            cJSON_Delete(root);
            r.status = PROVISION_ERR_BAD_WORD_ORDER;
            strncpy(r.field_name, "wordOrder", sizeof(r.field_name) - 1);
            r.field_name[sizeof(r.field_name) - 1] = '\0';
            return r;
        }
        out->word_order = nvs_config_parse_word_order(wo_str);
    }

    cJSON_Delete(root);

    /* Final structural validation (required fields populated). */
    return provisioning_validate_struct(out);
}

/* ── Listener task ───────────────────────────────────────────── */

static void emit_error(const provision_result_t *r)
{
    const char *reason = "unknown";
    switch (r->status) {
        case PROVISION_OK:                /* shouldn't happen here */ return;
        case PROVISION_ERR_NO_PREFIX:     reason = "missing PROVISION: prefix"; break;
        case PROVISION_ERR_INVALID_JSON:  reason = "invalid JSON"; break;
        case PROVISION_ERR_NOT_OBJECT:    reason = "expected JSON object"; break;
        case PROVISION_ERR_MISSING_FIELD: reason = "missing field"; break;
        case PROVISION_ERR_EMPTY_FIELD:   reason = "missing or empty field"; break;
        case PROVISION_ERR_BAD_WORD_ORDER:
            reason = "wordOrder must be low-word-first or high-word-first"; break;
    }
    if (r->field_name[0] != '\0') {
        printf("ERR:%s: %s\n", reason, r->field_name);
    } else {
        printf("ERR:%s\n", reason);
    }
}

static void handle_line(const char *line)
{
    /* Ignore noise — only act on lines with our prefix. */
    if (provisioning_strip_prefix(line) == NULL) return;

    nvs_config_t cfg;
    provision_result_t r = provisioning_parse_line(line, &cfg);
    if (r.status != PROVISION_OK) {
        emit_error(&r);
        return;
    }

    if (!nvs_config_save(&cfg)) {
        printf("ERR:save failed\n");
        return;
    }

    printf("OK\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(250));   /* let the OK byte get flushed */
    esp_restart();
}

static void listener_task(void *arg)
{
    (void)arg;

    /* Configure UART0 for input. The console (printf) already writes here;
     * full-duplex hardware lets us read concurrently. */
    const uart_config_t uart_cfg = {
        .baud_rate  = PROVISION_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(PROVISION_UART, &uart_cfg);
    uart_driver_install(PROVISION_UART, PROVISION_RX_BUF, 0, 0, NULL, 0);

    char    line[PROVISION_LINE_MAX];
    size_t  line_len = 0;
    int64_t last_heartbeat_ms = 0;

    ESP_LOGI(TAG, "listener started — emitting READY heartbeats every %d ms",
             PROVISION_HEARTBEAT_MS);

    while (1) {
        /* Heartbeat. */
        int64_t now = esp_log_timestamp();
        if (now - last_heartbeat_ms >= PROVISION_HEARTBEAT_MS) {
            printf("READY:auraflow-provision-v1\n");
            fflush(stdout);
            last_heartbeat_ms = now;
        }

        /* Read up to 64 bytes with a short timeout — keeps the heartbeat ticking. */
        uint8_t chunk[64];
        int n = uart_read_bytes(PROVISION_UART, chunk, sizeof(chunk),
                                pdMS_TO_TICKS(100));
        if (n <= 0) continue;

        for (int i = 0; i < n; i++) {
            uint8_t b = chunk[i];
            if (b == '\n') {
                line[line_len] = '\0';
                if (line_len > 0) handle_line(line);
                line_len = 0;
            } else if (b != '\r') {
                if (line_len < sizeof(line) - 1) {
                    line[line_len++] = (char)b;
                } else {
                    /* Overflow — drop the line and reset. */
                    line_len = 0;
                }
            }
        }
    }
}

void provisioning_start_listener(void)
{
    xTaskCreate(listener_task, "provision", 4096, NULL, 5, NULL);
}

#endif  /* ESP_PLATFORM */
