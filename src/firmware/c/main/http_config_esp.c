/**
 * ESP-IDF HTTP server for the device's on-LAN status page.
 * See http_config.h for the API.
 */
#ifdef ESP_PLATFORM

#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "http_config.h"

static const char *TAG = "http_config";

static httpd_handle_t        s_server = NULL;
static http_config_init_t    s_init   = { 0 };
static http_config_status_t  s_status = { 0 };
static bool                  s_started = false;

/* ── Status page renderer ─────────────────────────────────────── */

/* Keep the HTML small enough to fit comfortably in a stack buffer.
 * Mobile-friendly; dark theme to match the dashboard. */
static int render_status_html(char *buf, size_t buf_size)
{
    const int64_t uptime_sec = esp_timer_get_time() / 1000000LL;

    /* Snapshot the dynamic struct once — fields are independent ints/floats
     * and torn reads don't corrupt anything visible on the page. */
    const http_config_status_t s = s_status;

    char rate_str[32]    = "—";
    char signal_str[16]  = "—";
    char rssi_str[16]    = "—";

    if (s.has_rate)            snprintf(rate_str,   sizeof(rate_str),   "%.4f m³/h", (double)s.rate_m3h);
    if (s.has_signal_quality)  snprintf(signal_str, sizeof(signal_str), "%d", s.signal_quality);
    if (s.has_rssi)            snprintf(rssi_str,   sizeof(rssi_str),   "%d dBm", s.rssi);

    return snprintf(buf, buf_size,
        "<!DOCTYPE html>\n"
        "<html lang=\"en\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<meta http-equiv=\"refresh\" content=\"5\">"
        "<title>AuraFlow %s</title>"
        "<style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
        "max-width:560px;margin:2rem auto;padding:0 1rem;"
        "background:#0b0e13;color:#e2e8f0;line-height:1.5}"
        "h1{font-size:1.3rem;margin:0 0 .2rem}"
        ".sub{color:#94a3b8;font-size:.85rem;margin-bottom:1.4rem}"
        "table{width:100%%;border-collapse:collapse}"
        "td{padding:.45rem .6rem;border-bottom:1px solid #2a3142;font-size:.95rem}"
        "td:first-child{color:#94a3b8;width:45%%}"
        "td:last-child{font-family:'SF Mono',Monaco,monospace}"
        ".foot{margin-top:1.5rem;color:#475569;font-size:.78rem;text-align:center}"
        "</style></head><body>"
        "<h1>AuraFlow</h1>"
        "<div class=\"sub\">Sensor <code>%s</code> · auto-refreshing every 5s</div>"
        "<table>"
        "<tr><td>HomeHub</td><td>%s</td></tr>"
        "<tr><td>Firmware</td><td>%s</td></tr>"
        "<tr><td>Uptime</td><td>%lld s</td></tr>"
        "<tr><td>Last flow rate</td><td>%s</td></tr>"
        "<tr><td>Signal quality</td><td>%s</td></tr>"
        "<tr><td>Wi-Fi RSSI</td><td>%s</td></tr>"
        "<tr><td>Pending uploads</td><td>%zu</td></tr>"
        "</table>"
        "<div class=\"foot\">GET /diag and POST /config coming next</div>"
        "</body></html>\n",
        s_init.sensor_id,
        s_init.sensor_id,
        s_init.homehub_url,
        s_init.firmware_version,
        (long long)uptime_sec,
        rate_str,
        signal_str,
        rssi_str,
        s.pending_uploads);
}

/* ── HTTP handlers ─────────────────────────────────────────────── */

static esp_err_t handle_get_root(httpd_req_t *req)
{
    char buf[2048];
    int n = render_status_html(buf, sizeof(buf));
    if (n < 0 || n >= (int)sizeof(buf)) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

/* ── Public API ────────────────────────────────────────────────── */

void http_config_start(const http_config_init_t *init)
{
    if (s_started) return;
    if (init != NULL) s_init = *init;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return;
    }

    httpd_uri_t root = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = handle_get_root,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_server, &root);

    s_started = true;
    ESP_LOGI(TAG, "HTTP server up — visit http://<device-ip>/ to see status");
}

void http_config_update_status(const http_config_status_t *status)
{
    if (status != NULL) {
        s_status = *status;
    }
}

#endif  /* ESP_PLATFORM */
