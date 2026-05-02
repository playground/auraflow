/**
 * AuraFlow ESP32 firmware — orchestrator.
 *
 * Boot flow:
 *   1. Initialize NVS, load config.
 *   2. If unprovisioned → start the WebSerial provisioning listener and stop
 *      (the listener restarts the chip after a successful PROVISION:{...}).
 *   3. Else → init uplink, start wifi_mgr. The on_wifi_up callback fires the
 *      poll task once we have an IP.
 *
 * Poll task (one FreeRTOS task, runs forever):
 *   - Configures UART2 for Modbus RTU to the TUF-2000M (GPIO16 RX, 17 TX).
 *   - Each iteration: query flow rate / totalizer / signal quality; build
 *     a reading payload; uplink_push() — server response carries the next
 *     poll cadence (flowing vs idle).
 *   - Adaptive cadence is decided by comparing the just-read rate to a
 *     "flowing" threshold; flowing → flowing_poll_interval_ms, else idle.
 */
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "http_config.h"
#include "modbus.h"
#include "nvs_config.h"
#include "provisioning.h"
#include "tuf2000m.h"
#include "uplink.h"
#include "wifi_mgr.h"

#define FIRMWARE_VERSION         "0.2.0-c"

#define MODBUS_UART_NUM          UART_NUM_2
#define MODBUS_UART_BAUD         9600
#define MODBUS_GPIO_TX           17
#define MODBUS_GPIO_RX           16
#define MODBUS_RX_BUF            512
#define MODBUS_RESP_BUF          16
#define MODBUS_TIMEOUT_MS        500

/* "Currently flowing" threshold for cadence selection. Matches the
 * homehub default flowingThresholdM3h ≈ 0.1 L/min. Used only on-device
 * to pick the poll interval; the engine still runs server-side. */
#define FLOWING_THRESHOLD_M3H    0.006f

static const char *TAG = "auraflow";

static nvs_config_t          s_cfg;
static uplink_t              s_uplink;
static const char           *s_boot_reason = "unknown";
static char                  s_mac_str[18];
static int                   s_current_poll_ms = UPLINK_DEFAULT_IDLE_POLL_INTERVAL_MS;
/* Last cadence written to NVS — compared against each push's response to
 * skip the i32 writes when nothing changed. Initialized in app_main. */
static uplink_poll_config_t  s_last_persisted_pc = { 0 };

/* ── Modbus exchange over UART2 ──────────────────────────────── */

static void modbus_uart_init(void)
{
    const uart_config_t cfg = {
        .baud_rate  = MODBUS_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(MODBUS_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(MODBUS_UART_NUM,
                                 MODBUS_GPIO_TX, MODBUS_GPIO_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(MODBUS_UART_NUM, MODBUS_RX_BUF, 0, 0, NULL, 0));
}

/* Send `query` and read up to `out_size` bytes within `MODBUS_TIMEOUT_MS`.
 * Returns bytes actually read (0..out_size). The caller's parser handles
 * short reads via MODBUS_ERR_SHORT. */
static int modbus_exchange(const uint8_t *query, size_t query_len,
                           uint8_t *out, size_t out_size)
{
    uart_flush_input(MODBUS_UART_NUM);
    uart_write_bytes(MODBUS_UART_NUM, (const char *)query, query_len);
    int n = uart_read_bytes(MODBUS_UART_NUM, out, out_size,
                            pdMS_TO_TICKS(MODBUS_TIMEOUT_MS));
    return (n < 0) ? 0 : n;
}

/* ── Boot reason ─────────────────────────────────────────────── */

static const char *boot_reason_to_string(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON: return "power";
        case ESP_RST_SW:      return "software";
        case ESP_RST_PANIC:
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:     return "watchdog";
        default:              return "unknown";
    }
}

/* ── Diagnostics ─────────────────────────────────────────────── */

static void cache_mac_string(void)
{
    uint8_t mac[6] = { 0 };
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(s_mac_str, sizeof(s_mac_str),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        s_mac_str[0] = '\0';
    }
}

static bool current_rssi(int *out)
{
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) return false;
    *out = info.rssi;
    return true;
}

/* ── Poll task ───────────────────────────────────────────────── */

static bool read_flow_rate(float *out)
{
    uint8_t q[8];
    uint8_t r[MODBUS_RESP_BUF];
    modbus_build_read_holding(TUF2000M_SLAVE_ID,
                              TUF2000M_REG_FLOW_RATE_M3H.address,
                              TUF2000M_REG_FLOW_RATE_M3H.count, q);
    int n = modbus_exchange(q, sizeof(q), r, sizeof(r));
    if (n < 5) return false;
    return tuf2000m_parse_float_response(r, (size_t)n, s_cfg.word_order, out) == MODBUS_OK;
}

static bool read_totalizer(float *out)
{
    uint8_t q[8];
    uint8_t r[MODBUS_RESP_BUF];
    modbus_build_read_holding(TUF2000M_SLAVE_ID,
                              TUF2000M_REG_TOTALIZER_M3.address,
                              TUF2000M_REG_TOTALIZER_M3.count, q);
    int n = modbus_exchange(q, sizeof(q), r, sizeof(r));
    if (n < 5) return false;
    return tuf2000m_parse_float_response(r, (size_t)n, s_cfg.word_order, out) == MODBUS_OK;
}

static bool read_signal_quality(int *out)
{
    uint8_t q[8];
    uint8_t r[MODBUS_RESP_BUF];
    modbus_build_read_holding(TUF2000M_SLAVE_ID,
                              TUF2000M_REG_SIGNAL_QUALITY.address,
                              TUF2000M_REG_SIGNAL_QUALITY.count, q);
    int n = modbus_exchange(q, sizeof(q), r, sizeof(r));
    if (n < 5) return false;
    uint16_t v;
    if (tuf2000m_parse_u16_response(r, (size_t)n, &v) != MODBUS_OK) return false;
    *out = (int)v;
    return true;
}

static void poll_task(void *arg)
{
    (void)arg;
    modbus_uart_init();
    cache_mac_string();

    ESP_LOGI(TAG, "poll task started; cadence will adapt to server config");

    while (1) {
        uplink_reading_t reading = { 0 };

        /* Try to read flow rate. On failure we still push — with rate=0
         * and meter_reachable=false — so HomeHub sees the device is
         * alive (last_seen, RSSI, fw version, source IP refresh) and
         * the OTA dispatch path stays unblocked even before the
         * TUF-2000M is wired. The server-side route skips the
         * flow_readings INSERT and the leak engine when the flag is
         * false; nothing else changes. */
        const bool meter_ok = read_flow_rate(&reading.rate_m3h);
        if (!meter_ok) {
            reading.rate_m3h = 0.0f;
            ESP_LOGW(TAG, "flow rate read failed; pushing diagnostic heartbeat");
        }
        reading.meter_reachable = meter_ok;

        /* Optional: totalizer + signal quality — only meaningful when
         * the meter answered. Skip the reads if it didn't. */
        if (meter_ok) {
            float t;
            if (read_totalizer(&t)) {
                reading.has_total_m3 = true;
                reading.total_m3     = t;
            }

            int sq;
            if (read_signal_quality(&sq)) {
                reading.has_signal_quality = true;
                reading.signal_quality     = sq;
            }
        }

        /* Diagnostics — cheap to include and surface real issues quickly. */
        int rssi;
        if (current_rssi(&rssi)) {
            reading.has_rssi = true;
            reading.rssi     = rssi;
        }
        reading.has_uptime_sec = true;
        reading.uptime_sec     = esp_timer_get_time() / 1000000LL;
        strncpy(reading.firmware_version, FIRMWARE_VERSION,
                sizeof(reading.firmware_version) - 1);
        strncpy(reading.mac, s_mac_str, sizeof(reading.mac) - 1);
        strncpy(reading.boot_reason, s_boot_reason,
                sizeof(reading.boot_reason) - 1);

        /* Push (and try to flush any backlog). Server response chooses next interval. */
        int64_t now_ms = esp_timer_get_time() / 1000;
        uplink_poll_config_t pc = uplink_push(&s_uplink, &reading, now_ms);

        bool flowing = reading.rate_m3h > FLOWING_THRESHOLD_M3H;
        s_current_poll_ms = flowing
            ? pc.flowing_poll_interval_ms
            : pc.idle_poll_interval_ms;

        /* Mirror the latest cadence to NVS so a reboot resumes here
         * instead of the firmware default. Skip when unchanged to avoid
         * gratuitous flash writes — uplink_push returns the cached
         * last_poll_config on offline pushes, so this naturally
         * idles when there's no network. */
        if (pc.poll_interval_ms         != s_last_persisted_pc.poll_interval_ms
         || pc.flowing_poll_interval_ms != s_last_persisted_pc.flowing_poll_interval_ms
         || pc.idle_poll_interval_ms    != s_last_persisted_pc.idle_poll_interval_ms) {
            if (nvs_config_poll_save(pc.poll_interval_ms,
                                     pc.flowing_poll_interval_ms,
                                     pc.idle_poll_interval_ms)) {
                s_last_persisted_pc = pc;
                ESP_LOGI(TAG, "poll cadence persisted: poll=%d flowing=%d idle=%d",
                         pc.poll_interval_ms,
                         pc.flowing_poll_interval_ms,
                         pc.idle_poll_interval_ms);
            }
        }

        ESP_LOGI(TAG,
                 "rate=%.4f m³/h%s sq=%s rssi=%s pending=%u next=%dms",
                 (double)reading.rate_m3h,
                 reading.has_total_m3        ? " (totalizer)" : "",
                 reading.has_signal_quality  ? "ok" : "—",
                 reading.has_rssi            ? "ok" : "—",
                 (unsigned)uplink_pending(&s_uplink),
                 s_current_poll_ms);

        /* Push a snapshot to the on-device status page. */
        const http_config_status_t hstatus = {
            .has_rate           = true,
            .rate_m3h           = reading.rate_m3h,
            .has_signal_quality = reading.has_signal_quality,
            .signal_quality     = reading.signal_quality,
            .has_rssi           = reading.has_rssi,
            .rssi               = reading.rssi,
            .pending_uploads    = uplink_pending(&s_uplink),
        };
        http_config_update_status(&hstatus);

        vTaskDelay(pdMS_TO_TICKS(s_current_poll_ms));
    }
}

/* ── Wi-Fi callbacks ─────────────────────────────────────────── */

static volatile bool s_poll_started = false;

static void on_wifi_up(void)
{
    ESP_LOGI(TAG, "Wi-Fi up");

    /* Start the on-device status page once we have an IP. Idempotent — safe
     * to be called again on reconnect. */
    http_config_init_t hcfg = { 0 };
    strncpy(hcfg.sensor_id,        s_cfg.sensor_id,        sizeof(hcfg.sensor_id) - 1);
    strncpy(hcfg.homehub_url,      s_cfg.homehub_url,      sizeof(hcfg.homehub_url) - 1);
    strncpy(hcfg.firmware_version, FIRMWARE_VERSION,       sizeof(hcfg.firmware_version) - 1);
    http_config_start(&hcfg);

    if (!s_poll_started) {
        xTaskCreate(poll_task, "auraflow_poll", 6144, NULL, 5, NULL);
        s_poll_started = true;
    }
}

static void on_wifi_down(void)
{
    ESP_LOGW(TAG, "Wi-Fi down — uplink will buffer until reconnect");
}

/* ── Boot ────────────────────────────────────────────────────── */

void app_main(void)
{
    s_boot_reason = boot_reason_to_string(esp_reset_reason());
    ESP_LOGI(TAG, "AuraFlow firmware %s starting (boot=%s)",
             FIRMWARE_VERSION, s_boot_reason);

    if (!nvs_config_init()) {
        ESP_LOGE(TAG, "NVS init failed; treating as unprovisioned");
    }
    if (!nvs_config_load(&s_cfg)) {
        ESP_LOGW(TAG, "NVS load failed; treating as unprovisioned");
    }

    if (!nvs_config_is_provisioned(&s_cfg)) {
        ESP_LOGW(TAG, "NVS not provisioned — listening on UART0 for PROVISION:{...}");
        provisioning_start_listener();
        return;
    }

    ESP_LOGI(TAG,
             "provisioned: sensorId=%s homehub=%s wordOrder=%s",
             s_cfg.sensor_id, s_cfg.homehub_url,
             nvs_config_word_order_to_string(s_cfg.word_order));

    uplink_config_t ucfg = { 0 };
    strncpy(ucfg.homehub_url,      s_cfg.homehub_url,      sizeof(ucfg.homehub_url)      - 1);
    strncpy(ucfg.internal_api_key, s_cfg.internal_api_key, sizeof(ucfg.internal_api_key) - 1);
    strncpy(ucfg.sensor_id,        s_cfg.sensor_id,        sizeof(ucfg.sensor_id)        - 1);
    uplink_init(&s_uplink, &ucfg);

    /* Restore last-known cadence from NVS so we don't run on firmware
     * defaults until HomeHub's first response. Falls back silently when
     * nothing has been cached yet (fresh device). */
    int p_ms = 0, f_ms = 0, i_ms = 0;
    if (nvs_config_poll_load(&p_ms, &f_ms, &i_ms)) {
        s_uplink.last_poll_config.poll_interval_ms         = p_ms;
        s_uplink.last_poll_config.flowing_poll_interval_ms = f_ms;
        s_uplink.last_poll_config.idle_poll_interval_ms    = i_ms;
        s_current_poll_ms       = i_ms;   /* assume idle until first reading */
        s_last_persisted_pc     = s_uplink.last_poll_config;
        ESP_LOGI(TAG, "poll cadence restored from NVS: poll=%d flowing=%d idle=%d",
                 p_ms, f_ms, i_ms);
    } else {
        ESP_LOGI(TAG, "no cached poll cadence — using firmware defaults");
    }

    const bool static_ip = nvs_config_uses_static_ip(&s_cfg);
    if (static_ip) {
        ESP_LOGI(TAG, "static IP requested: ip=%s gw=%s nm=%s",
                 s_cfg.static_ip, s_cfg.static_gateway, s_cfg.static_netmask);
    } else {
        ESP_LOGI(TAG, "no static IP configured — using DHCP");
    }

    wifi_mgr_config_t wcfg = {
        .ssid           = s_cfg.wifi_ssid,
        .password       = s_cfg.wifi_password,
        .static_ip      = static_ip ? s_cfg.static_ip      : NULL,
        .static_gateway = static_ip ? s_cfg.static_gateway : NULL,
        .static_netmask = static_ip ? s_cfg.static_netmask : NULL,
        .on_up          = on_wifi_up,
        .on_down        = on_wifi_down,
    };
    wifi_mgr_start(&wcfg);
}
