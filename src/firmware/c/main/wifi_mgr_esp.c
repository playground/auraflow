/**
 * ESP-IDF Wi-Fi state machine — connects, handles disconnect with
 * exponential backoff, applies anti-flap throttle when reconnects spike.
 *
 * Pure helpers (history, backoff math) live in wifi_mgr.c.
 */
#ifdef ESP_PLATFORM

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "wifi_mgr.h"

static const char *TAG = "wifi_mgr";

static wifi_mgr_history_t        s_history;
static int                        s_backoff_ms = WIFI_MGR_INITIAL_BACKOFF_MS;
static const wifi_mgr_config_t   *s_cfg        = NULL;
static TimerHandle_t              s_reconnect_timer = NULL;

static int64_t now_ms(void)
{
    /* esp_timer_get_time returns microseconds since boot. */
    return esp_timer_get_time() / 1000;
}

/* Forward declaration. */
static void schedule_reconnect_locked(int delay_ms);

static void reconnect_timer_cb(TimerHandle_t timer)
{
    (void)timer;

    if (wifi_mgr_history_should_throttle(&s_history, now_ms())) {
        ESP_LOGW(TAG,
                 "anti-flap: %d+ connects in last hour, sleeping %d ms",
                 WIFI_MGR_FLAP_THRESHOLD, WIFI_MGR_THROTTLE_MS);
        schedule_reconnect_locked(WIFI_MGR_THROTTLE_MS);
        return;
    }

    ESP_LOGI(TAG, "attempting reconnect");
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect returned %s — backing off",
                 esp_err_to_name(err));
        int delay = s_backoff_ms;
        s_backoff_ms = wifi_mgr_next_backoff_ms(s_backoff_ms);
        schedule_reconnect_locked(delay);
    }
}

static void schedule_reconnect_locked(int delay_ms)
{
    if (s_reconnect_timer == NULL) return;
    if (delay_ms < 1) delay_ms = 1;
    xTimerChangePeriod(s_reconnect_timer, pdMS_TO_TICKS(delay_ms), 0);
    xTimerStart(s_reconnect_timer, 0);
}

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == WIFI_EVENT) {
        switch (id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started — connecting");
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "disconnected");
                if (s_cfg && s_cfg->on_down) s_cfg->on_down();
                {
                    int delay = s_backoff_ms;
                    s_backoff_ms = wifi_mgr_next_backoff_ms(s_backoff_ms);
                    schedule_reconnect_locked(delay);
                }
                break;
            default:
                break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "got IP");
        wifi_mgr_history_record(&s_history, now_ms());
        s_backoff_ms = WIFI_MGR_INITIAL_BACKOFF_MS;
        if (s_cfg && s_cfg->on_up) s_cfg->on_up();
    }
}

void wifi_mgr_start(const wifi_mgr_config_t *cfg)
{
    if (cfg == NULL || cfg->ssid == NULL) {
        ESP_LOGE(TAG, "invalid config (NULL ssid)");
        return;
    }

    s_cfg = cfg;
    wifi_mgr_history_init(&s_history);
    s_backoff_ms = WIFI_MGR_INITIAL_BACKOFF_MS;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &event_handler, NULL));

    wifi_config_t sta_cfg = { 0 };
    strncpy((char *)sta_cfg.sta.ssid, cfg->ssid, sizeof(sta_cfg.sta.ssid) - 1);
    if (cfg->password != NULL) {
        strncpy((char *)sta_cfg.sta.password, cfg->password,
                sizeof(sta_cfg.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

    /* One-shot timer reused for both backoff retries and throttle pauses. */
    s_reconnect_timer = xTimerCreate(
        "wifi-reconnect",
        pdMS_TO_TICKS(WIFI_MGR_INITIAL_BACKOFF_MS),
        pdFALSE,                /* one-shot */
        NULL,
        reconnect_timer_cb);

    ESP_ERROR_CHECK(esp_wifi_start());
    /* WIFI_EVENT_STA_START fires the first connect from the event handler. */
}

#endif  /* ESP_PLATFORM */
