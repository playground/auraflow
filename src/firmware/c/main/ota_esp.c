/**
 * ESP-IDF OTA driver. See ota.h.
 *
 * One inflight OTA at a time, gated by s_in_progress. The URL is
 * strdup'd onto the heap so the originating HTTP handler can return
 * immediately — the task takes ownership and frees on exit.
 */
#ifdef ESP_PLATFORM

#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ota.h"

static const char *TAG = "ota";

static volatile bool s_in_progress = false;

bool ota_in_progress(void)
{
    return s_in_progress;
}

static void ota_task(void *arg)
{
    char *url = (char *)arg;

    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "OTA starting — running slot=%s, fetching %s",
             running ? running->label : "?", url);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "OTA OK — restarting");
        free(url);
        s_in_progress = false;
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    }

    free(url);
    s_in_progress = false;
    vTaskDelete(NULL);
}

void ota_start(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        ESP_LOGW(TAG, "ota_start: empty URL");
        return;
    }
    if (s_in_progress) {
        ESP_LOGW(TAG, "ota_start: already in progress, ignoring");
        return;
    }

    char *copy = strdup(url);
    if (copy == NULL) {
        ESP_LOGE(TAG, "ota_start: strdup failed");
        return;
    }

    s_in_progress = true;
    /* 8 KiB task stack — esp_https_ota + http client need plenty.
     * Priority 5: same as poll_task; high enough to make progress, low
     * enough that watchdog and Wi-Fi event work continues. */
    BaseType_t ok = xTaskCreate(ota_task, "ota", 8192, copy, 5, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "ota_start: xTaskCreate failed");
        free(copy);
        s_in_progress = false;
    }
}

#endif  /* ESP_PLATFORM */
