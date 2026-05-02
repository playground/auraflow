/**
 * ESP-IDF HTTP server for the device's on-LAN status + config UI.
 * See http_config.h for the public API.
 *
 * Routes:
 *   GET  /        — HTML status page (auto-refreshes every 5s)
 *   GET  /edit    — HTML form preloaded from NVS for editing config
 *   GET  /diag    — JSON status (same data as /, scriptable)
 *   POST /config  — accept partial JSON, merge with current NVS config,
 *                   validate, save, esp_restart
 */
#ifdef ESP_PLATFORM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "http_config.h"
#include "nvs_config.h"
#include "ota.h"
#include "provisioning.h"

static const char *TAG = "http_config";

static httpd_handle_t        s_server = NULL;
static http_config_init_t    s_init   = { 0 };
static http_config_status_t  s_status = { 0 };
static bool                  s_started = false;

/* ── Helpers ──────────────────────────────────────────────────── */

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *body = cJSON_PrintUnformatted(root);
    if (body == NULL) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    free(body);
    return err;
}

/* Apply one optional string field from `root` into a fixed-size buffer.
 * If the key is absent, leaves dst unchanged (current value preserved). */
static void apply_str_field(const cJSON *root, const char *key,
                            char *dst, size_t dst_size)
{
    cJSON *item = cJSON_GetObjectItem(root, key);
    if (item == NULL) return;
    if (!cJSON_IsString(item) || item->valuestring == NULL) return;
    size_t len = strlen(item->valuestring);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, item->valuestring, len);
    dst[len] = '\0';
}

/* Convenience: render the boilerplate <head> + body open used by both
 * status and edit pages. */
static int render_html_head(char *buf, size_t size, const char *title,
                            int refresh_seconds)
{
    return snprintf(buf, size,
        "<!DOCTYPE html>\n"
        "<html lang=\"en\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "%s"
        "<title>%s</title>"
        "<style>"
        "body{font-family:-apple-system,BlinkMacSystemFont,sans-serif;"
        "max-width:560px;margin:1.5rem auto;padding:0 1rem;"
        "background:#0b0e13;color:#e2e8f0;line-height:1.5}"
        "h1{font-size:1.3rem;margin:0 0 .2rem}"
        ".sub{color:#94a3b8;font-size:.85rem;margin-bottom:1.4rem}"
        "table{width:100%%;border-collapse:collapse;margin-bottom:1rem}"
        "td{padding:.45rem .6rem;border-bottom:1px solid #2a3142;font-size:.95rem}"
        "td:first-child{color:#94a3b8;width:40%%}"
        "td:last-child{font-family:'SF Mono',Monaco,monospace;word-break:break-all}"
        "form{display:flex;flex-direction:column;gap:.5rem}"
        "label{display:flex;flex-direction:column;gap:.2rem;font-size:.85rem;color:#94a3b8}"
        "input,select{background:#0a0d12;border:1px solid #2a3142;color:#e2e8f0;"
        "padding:.45rem .6rem;border-radius:6px;font-size:.95rem;font-family:inherit}"
        "input:focus,select:focus{outline:none;border-color:#3b82f6}"
        ".row{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center}"
        "button{background:#3b82f6;color:#fff;border:none;padding:.6rem 1.1rem;"
        "border-radius:6px;font-size:.95rem;cursor:pointer;font-weight:600}"
        "button:hover{background:#2563eb}"
        "a{color:#3b82f6;text-decoration:none}"
        "a:hover{color:#60a5fa}"
        ".nav{margin-bottom:1rem;font-size:.85rem}"
        ".nav a{margin-right:1rem}"
        ".help{font-size:.78rem;color:#475569;margin-top:.2rem}"
        ".msg{font-size:.85rem;padding:.5rem .7rem;border-radius:6px;margin:.5rem 0}"
        ".msg.ok{background:rgba(74,222,128,.15);color:#4ade80}"
        ".msg.err{background:rgba(248,113,113,.15);color:#f87171}"
        "</style></head><body>"
        "<div class=\"nav\"><a href=\"/\">Status</a> · <a href=\"/edit\">Edit config</a> · <a href=\"/diag\">JSON</a></div>",
        refresh_seconds > 0
            ? "<meta http-equiv=\"refresh\" content=\"5\">"
            : "",
        title);
}

/* ── GET / — status page ──────────────────────────────────────── */

static esp_err_t handle_get_root(httpd_req_t *req)
{
    char buf[3072];
    int len = 0;
    int n;

    n = render_html_head(buf, sizeof(buf), "AuraFlow", 5);
    if (n < 0) return httpd_resp_send_500(req);
    len = n;

    const int64_t uptime_sec = esp_timer_get_time() / 1000000LL;
    const http_config_status_t s = s_status;

    char rate_str[32]    = "—";
    char signal_str[16]  = "—";
    char rssi_str[16]    = "—";
    if (s.has_rate)            snprintf(rate_str,   sizeof(rate_str),   "%.4f m³/h", (double)s.rate_m3h);
    if (s.has_signal_quality)  snprintf(signal_str, sizeof(signal_str), "%d", s.signal_quality);
    if (s.has_rssi)            snprintf(rssi_str,   sizeof(rssi_str),   "%d dBm", s.rssi);

    n = snprintf(buf + len, sizeof(buf) - len,
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
        "</body></html>\n",
        s_init.sensor_id,
        s_init.homehub_url,
        s_init.firmware_version,
        (long long)uptime_sec,
        rate_str, signal_str, rssi_str,
        s.pending_uploads);
    if (n < 0 || (size_t)n >= sizeof(buf) - len) return httpd_resp_send_500(req);
    len += n;

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, len);
}

/* ── GET /diag — JSON status ──────────────────────────────────── */

static esp_err_t handle_get_diag(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return httpd_resp_send_500(req);

    const int64_t uptime_sec = esp_timer_get_time() / 1000000LL;
    const http_config_status_t s = s_status;

    cJSON_AddStringToObject(root, "sensorId",        s_init.sensor_id);
    cJSON_AddStringToObject(root, "homehubUrl",      s_init.homehub_url);
    cJSON_AddStringToObject(root, "firmwareVersion", s_init.firmware_version);
    cJSON_AddNumberToObject(root, "uptimeSec",       (double)uptime_sec);
    cJSON_AddNumberToObject(root, "pendingUploads",  (double)s.pending_uploads);

    if (s.has_rate)            cJSON_AddNumberToObject(root, "rateM3h",       (double)s.rate_m3h);
    if (s.has_signal_quality)  cJSON_AddNumberToObject(root, "signalQuality", (double)s.signal_quality);
    if (s.has_rssi)            cJSON_AddNumberToObject(root, "rssi",          (double)s.rssi);

    esp_err_t err = send_json(req, root);
    cJSON_Delete(root);
    return err;
}

/* ── GET /edit — HTML form preloaded from NVS ─────────────────── */

static esp_err_t handle_get_edit(httpd_req_t *req)
{
    nvs_config_t cfg;
    if (!nvs_config_load(&cfg)) {
        ESP_LOGW(TAG, "/edit: nvs_config_load failed; showing empty form");
        memset(&cfg, 0, sizeof(cfg));
    }

    char buf[4096];
    int len = 0;
    int n;

    n = render_html_head(buf, sizeof(buf), "AuraFlow — edit config", 0);
    if (n < 0) return httpd_resp_send_500(req);
    len = n;

    const char *wo_low_sel  = (cfg.word_order == TUF2000M_LOW_WORD_FIRST)  ? "selected" : "";
    const char *wo_high_sel = (cfg.word_order == TUF2000M_HIGH_WORD_FIRST) ? "selected" : "";

    n = snprintf(buf + len, sizeof(buf) - len,
        "<h1>Edit config</h1>"
        "<div class=\"sub\">Saving will reboot the device. "
        "Leave password / API key blank to keep the existing value.</div>"
        "<form id=\"f\" onsubmit=\"return submitConfig(event)\">"
        "<label>Sensor ID<input name=\"sensorId\" value=\"%s\" required></label>"
        "<label>Wi-Fi SSID<input name=\"wifiSsid\" value=\"%s\" required></label>"
        "<label>Wi-Fi password<input name=\"wifiPassword\" type=\"password\" placeholder=\"(unchanged)\"></label>"
        "<label>HomeHub URL<input name=\"homehubUrl\" value=\"%s\" required></label>"
        "<label>Internal API key<input name=\"internalApiKey\" type=\"password\" placeholder=\"(unchanged)\"></label>"
        "<label>Modbus word order"
        "<select name=\"wordOrder\">"
        "<option value=\"low-word-first\" %s>CDAB (low-word-first) — most TUF-2000M units</option>"
        "<option value=\"high-word-first\" %s>ABCD (high-word-first)</option>"
        "</select></label>"
        "<details style=\"margin:.5rem 0\">"
        "<summary style=\"cursor:pointer;color:#94a3b8;font-size:.85rem\">"
        "Advanced — static IP (leave all blank for DHCP)"
        "</summary>"
        "<div style=\"display:flex;flex-direction:column;gap:.5rem;margin-top:.5rem\">"
        "<label>Static IP<input name=\"staticIp\" value=\"%s\" placeholder=\"e.g. 192.168.1.42\"></label>"
        "<label>Gateway<input name=\"staticGateway\" value=\"%s\" placeholder=\"e.g. 192.168.1.1\"></label>"
        "<label>Netmask<input name=\"staticNetmask\" value=\"%s\" placeholder=\"e.g. 255.255.255.0\"></label>"
        "<div class=\"help\">All three or none — partial config is rejected.</div>"
        "</div></details>"
        "<div class=\"row\">"
        "<button type=\"submit\">Save &amp; reboot</button>"
        "<span id=\"msg\"></span>"
        "</div>"
        "</form>"
        "<script>"
        "async function submitConfig(ev){"
        "ev.preventDefault();"
        "const f=ev.target,fd=new FormData(f),body={};"
        "for(const[k,v]of fd.entries()){if(v!==null&&v!==undefined&&v!=='')body[k]=v;}"
        "const m=document.getElementById('msg');m.className='';m.textContent='Saving…';"
        "try{"
        "const r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
        "const j=await r.json();"
        "if(r.ok){m.className='msg ok';m.textContent='Saved. Rebooting…';}"
        "else{m.className='msg err';m.textContent='Error: '+(j.error||r.statusText);}"
        "}catch(e){m.className='msg err';m.textContent='Network error: '+e.message;}"
        "return false;"
        "}"
        "</script>"
        "</body></html>\n",
        cfg.sensor_id,
        cfg.wifi_ssid,
        cfg.homehub_url,
        wo_low_sel, wo_high_sel,
        cfg.static_ip, cfg.static_gateway, cfg.static_netmask);
    if (n < 0 || (size_t)n >= sizeof(buf) - len) return httpd_resp_send_500(req);
    len += n;

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, len);
}

/* ── POST /config — merge + validate + save + restart ─────────── */

static esp_err_t send_error(httpd_req_t *req, int code, const char *err, const char *field)
{
    cJSON *root = cJSON_CreateObject();
    if (root != NULL) {
        cJSON_AddStringToObject(root, "error", err);
        if (field != NULL) cJSON_AddStringToObject(root, "field", field);
        char *body = cJSON_PrintUnformatted(root);
        if (body != NULL) {
            httpd_resp_set_status(req, code == 400 ? "400 Bad Request" : "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
            free(body);
            cJSON_Delete(root);
            return ESP_OK;
        }
        cJSON_Delete(root);
    }
    return code == 400 ? httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, err)
                       : httpd_resp_send_500(req);
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500));   /* let the response flush + the form's "Saved…" render */
    ESP_LOGW(TAG, "rebooting after /config save");
    esp_restart();
}

static esp_err_t handle_post_config(httpd_req_t *req)
{
    if (req->content_len > 4096) {
        return send_error(req, 400, "request body too large", NULL);
    }

    char body[2048];
    int total = 0;
    while (total < req->content_len && total < (int)sizeof(body) - 1) {
        int chunk = httpd_req_recv(req, body + total, sizeof(body) - 1 - total);
        if (chunk <= 0) {
            if (chunk == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return send_error(req, 400, "failed to read body", NULL);
        }
        total += chunk;
    }
    body[total] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return send_error(req, 400, "invalid JSON", NULL);
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return send_error(req, 400, "expected JSON object", NULL);
    }

    /* Start from current NVS state so partial updates work — caller can
     * submit just {homehubUrl: "..."} and everything else is preserved. */
    nvs_config_t cfg;
    if (!nvs_config_load(&cfg)) {
        cJSON_Delete(root);
        return send_error(req, 500, "nvs_config_load failed", NULL);
    }

    apply_str_field(root, "wifiSsid",       cfg.wifi_ssid,        sizeof(cfg.wifi_ssid));
    apply_str_field(root, "wifiPassword",   cfg.wifi_password,    sizeof(cfg.wifi_password));
    apply_str_field(root, "homehubUrl",     cfg.homehub_url,      sizeof(cfg.homehub_url));
    apply_str_field(root, "internalApiKey", cfg.internal_api_key, sizeof(cfg.internal_api_key));
    apply_str_field(root, "sensorId",       cfg.sensor_id,        sizeof(cfg.sensor_id));
    apply_str_field(root, "staticIp",       cfg.static_ip,        sizeof(cfg.static_ip));
    apply_str_field(root, "staticGateway",  cfg.static_gateway,   sizeof(cfg.static_gateway));
    apply_str_field(root, "staticNetmask",  cfg.static_netmask,   sizeof(cfg.static_netmask));

    cJSON *wo = cJSON_GetObjectItem(root, "wordOrder");
    if (wo != NULL && cJSON_IsString(wo) && wo->valuestring != NULL) {
        if (!provisioning_is_valid_word_order(wo->valuestring)) {
            cJSON_Delete(root);
            return send_error(req, 400, "wordOrder must be low-word-first or high-word-first",
                              "wordOrder");
        }
        cfg.word_order = nvs_config_parse_word_order(wo->valuestring);
    }

    cJSON_Delete(root);

    /* Validate the merged result. Re-uses the provisioning validator —
     * required fields populated, static IP all-or-nothing, etc. */
    provision_result_t r = provisioning_validate_struct(&cfg);
    if (r.status != PROVISION_OK) {
        return send_error(req, 400, "validation failed", r.field_name);
    }

    if (!nvs_config_save(&cfg)) {
        return send_error(req, 500, "nvs_config_save failed", NULL);
    }

    /* Reply OK first, then restart the device after a short delay so the
     * client sees a clean response. */
    cJSON *resp = cJSON_CreateObject();
    if (resp != NULL) {
        cJSON_AddBoolToObject(resp, "ok", true);
        cJSON_AddStringToObject(resp, "message", "Saved. Rebooting in 1.5s.");
        send_json(req, resp);
        cJSON_Delete(resp);
    }

    xTaskCreate(restart_task, "config_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* ── POST /ota — kick off OTA from a URL ──────────────────────── */

static esp_err_t handle_post_ota(httpd_req_t *req)
{
    if (ota_in_progress()) {
        httpd_resp_set_status(req, "409 Conflict");
        return send_error(req, 409, "ota already in progress", NULL);
    }
    if (req->content_len > 1024) {
        return send_error(req, 400, "request body too large", NULL);
    }

    char body[1024];
    int total = 0;
    while (total < req->content_len && total < (int)sizeof(body) - 1) {
        int chunk = httpd_req_recv(req, body + total, sizeof(body) - 1 - total);
        if (chunk <= 0) {
            if (chunk == HTTPD_SOCK_ERR_TIMEOUT) continue;
            return send_error(req, 400, "failed to read body", NULL);
        }
        total += chunk;
    }
    body[total] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (root == NULL || !cJSON_IsObject(root)) {
        if (root != NULL) cJSON_Delete(root);
        return send_error(req, 400, "invalid JSON", NULL);
    }

    cJSON *url_item = cJSON_GetObjectItem(root, "url");
    if (!cJSON_IsString(url_item) || url_item->valuestring == NULL
        || url_item->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return send_error(req, 400, "url is required", "url");
    }

    /* Reject obviously bogus schemes; esp_https_ota will catch the rest. */
    const char *url = url_item->valuestring;
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        cJSON_Delete(root);
        return send_error(req, 400, "url must be http:// or https://", "url");
    }

    ota_start(url);
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    if (resp != NULL) {
        cJSON_AddBoolToObject(resp, "started", true);
        cJSON_AddStringToObject(resp, "message", "OTA started — watch logs; device will reboot on success.");
        send_json(req, resp);
        cJSON_Delete(resp);
    }
    return ESP_OK;
}

/* ── Captive-portal handlers ──────────────────────────────────── */

/* Suggest a default sensorId derived from the soft-AP MAC so users
 * can just hit Save without typing one. We grab the soft-AP MAC for
 * symmetry with the AP SSID suffix. */
static void default_sensor_id(char *out, size_t out_size)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(out, out_size, "auraflow-%02x%02x", mac[4], mac[5]);
}

/* GET / on the portal — mobile-friendly form. Same field names as the
 * PROVISION JSON so /config can validate it identically. */
static esp_err_t handle_portal_root(httpd_req_t *req)
{
    char default_id[32];
    default_sensor_id(default_id, sizeof(default_id));

    char buf[4096];
    int len = 0;
    int n;

    n = render_html_head(buf, sizeof(buf), "AuraFlow — set up", 0);
    if (n < 0) return httpd_resp_send_500(req);
    len = n;

    /* Drop the nav bar — it links to /edit and /diag which don't exist
     * in portal mode. We render the head, then immediately overwrite
     * the trailing <div class="nav">…</div> with the portal body. */
    const char *nav_open = "<div class=\"nav\">";
    char *nav_pos = strstr(buf, nav_open);
    if (nav_pos != NULL) {
        len = (int)(nav_pos - buf);
    }

    n = snprintf(buf + len, sizeof(buf) - len,
        "<h1>Welcome to AuraFlow</h1>"
        "<div class=\"sub\">Tell the device how to reach your home Wi-Fi and HomeHub.</div>"
        "<form id=\"f\" onsubmit=\"return submitConfig(event)\">"
        "<label>Wi-Fi network"
        "<select name=\"wifiSsid\" id=\"ssid\" required>"
        "<option value=\"\" disabled selected>Scanning…</option>"
        "</select>"
        "<div class=\"help\"><a href=\"#\" onclick=\"loadScan(event)\">Rescan</a> "
        "or <a href=\"#\" onclick=\"toggleManual(event)\">enter manually</a></div>"
        "</label>"
        "<label id=\"manualLabel\" style=\"display:none\">Wi-Fi network (manual)"
        "<input name=\"wifiSsidManual\" placeholder=\"e.g. MyHome-5G\">"
        "</label>"
        "<label>Wi-Fi password<input name=\"wifiPassword\" type=\"password\"></label>"
        "<label>HomeHub URL<input name=\"homehubUrl\" placeholder=\"http://homehub.local:3300\" required></label>"
        "<label>Internal API key<input name=\"internalApiKey\" type=\"password\" required></label>"
        "<label>Sensor ID<input name=\"sensorId\" value=\"%s\" required></label>"
        "<input type=\"hidden\" name=\"wordOrder\" value=\"low-word-first\">"
        "<div class=\"row\">"
        "<button type=\"submit\">Save &amp; connect</button>"
        "<span id=\"msg\"></span>"
        "</div>"
        "</form>"
        "<script>"
        "async function loadScan(ev){if(ev)ev.preventDefault();"
        "const sel=document.getElementById('ssid');"
        "sel.innerHTML='<option value=\"\" disabled selected>Scanning…</option>';"
        "try{const r=await fetch('/scan');const j=await r.json();"
        "sel.innerHTML='<option value=\"\" disabled selected>Pick a network</option>';"
        "for(const s of(j.networks||[])){"
        "const o=document.createElement('option');o.value=s.ssid;"
        "o.textContent=s.ssid+' ('+s.rssi+' dBm)';sel.appendChild(o);}"
        "}catch(e){sel.innerHTML='<option value=\"\" disabled selected>Scan failed</option>';}}"
        "function toggleManual(ev){if(ev)ev.preventDefault();"
        "const m=document.getElementById('manualLabel');"
        "m.style.display=m.style.display==='none'?'flex':'none';}"
        "async function submitConfig(ev){ev.preventDefault();"
        "const f=ev.target,fd=new FormData(f),body={};"
        "for(const[k,v]of fd.entries()){if(v!==null&&v!==undefined&&v!=='')body[k]=v;}"
        "if(body.wifiSsidManual){body.wifiSsid=body.wifiSsidManual;delete body.wifiSsidManual;}"
        "const m=document.getElementById('msg');m.className='';m.textContent='Saving…';"
        "try{const r=await fetch('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
        "const j=await r.json();"
        "if(r.ok){m.className='msg ok';m.textContent='Saved. Device is rebooting and joining your Wi-Fi.';}"
        "else{m.className='msg err';m.textContent='Error: '+(j.error||r.statusText);}"
        "}catch(e){m.className='msg err';m.textContent='Network error: '+e.message;}"
        "return false;}"
        "loadScan();"
        "</script>"
        "</body></html>\n",
        default_id);
    if (n < 0 || (size_t)n >= sizeof(buf) - len) return httpd_resp_send_500(req);
    len += n;

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, len);
}

/* GET /scan — JSON list of nearby SSIDs. AP scan is blocking and
 * stalls the server task for ~1.5s; that's acceptable for a setup
 * flow that runs once. */
static esp_err_t handle_get_scan(httpd_req_t *req)
{
    wifi_scan_config_t cfg = { 0 };  /* all channels, active scan, defaults */
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan_start failed: %s", esp_err_to_name(err));
        return send_error(req, 500, "scan failed", NULL);
    }

    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found > 24) found = 24;        /* cap so we don't blow the JSON buffer */

    wifi_ap_record_t records[24];
    if (found > 0) {
        esp_wifi_scan_get_ap_records(&found, records);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();
    if (root == NULL || arr == NULL) {
        if (root) cJSON_Delete(root);
        if (arr)  cJSON_Delete(arr);
        return httpd_resp_send_500(req);
    }
    cJSON_AddItemToObject(root, "networks", arr);

    /* Dedup hidden / duplicate SSIDs — a phone seeing the same name 5
     * times with different RSSI (mesh) just confuses the user. */
    for (uint16_t i = 0; i < found; i++) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0') continue;
        bool dup = false;
        for (uint16_t j = 0; j < i; j++) {
            if (strcmp(ssid, (const char *)records[j].ssid) == 0) { dup = true; break; }
        }
        if (dup) continue;

        cJSON *net = cJSON_CreateObject();
        if (net == NULL) continue;
        cJSON_AddStringToObject(net, "ssid", ssid);
        cJSON_AddNumberToObject(net, "rssi", records[i].rssi);
        cJSON_AddItemToArray(arr, net);
    }

    esp_err_t r = send_json(req, root);
    cJSON_Delete(root);
    return r;
}

/* Catch-all 404 handler — iOS hits /hotspot-detect.html, Android hits
 * /generate_204, Windows hits /connecttest.txt; returning the portal
 * page on any of them makes the OS pop up the setup screen. */
static esp_err_t handle_portal_404(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    return handle_portal_root(req);
}

/* ── Public API ────────────────────────────────────────────────── */

void http_config_start(const http_config_init_t *init)
{
    if (s_started) return;
    if (init != NULL) s_init = *init;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;
    /* Default httpd task stack is 4 KiB; our /edit handler alone declares
     * a 4 KiB render buffer plus the cJSON + nvs_config_load frames it
     * sits on top of. 8 KiB gives comfortable headroom. */
    cfg.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = handle_get_root,   .user_ctx = NULL },
        { .uri = "/edit",   .method = HTTP_GET,  .handler = handle_get_edit,   .user_ctx = NULL },
        { .uri = "/diag",   .method = HTTP_GET,  .handler = handle_get_diag,   .user_ctx = NULL },
        { .uri = "/config", .method = HTTP_POST, .handler = handle_post_config,.user_ctx = NULL },
        { .uri = "/ota",    .method = HTTP_POST, .handler = handle_post_ota,   .user_ctx = NULL },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }

    s_started = true;
    ESP_LOGI(TAG, "HTTP server up — http://<device-ip>/  (status, /edit, /diag, /config)");
}

void http_config_start_portal(void)
{
    if (s_started) return;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 4;
    /* Same 8 KiB rationale as http_config_start — the portal HTML is
     * larger than the status page and we keep cJSON on the stack. */
    cfg.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start (portal) failed: %s", esp_err_to_name(err));
        return;
    }

    static const httpd_uri_t portal_routes[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = handle_portal_root, .user_ctx = NULL },
        { .uri = "/scan",   .method = HTTP_GET,  .handler = handle_get_scan,    .user_ctx = NULL },
        { .uri = "/config", .method = HTTP_POST, .handler = handle_post_config, .user_ctx = NULL },
    };
    for (size_t i = 0; i < sizeof(portal_routes) / sizeof(portal_routes[0]); i++) {
        httpd_register_uri_handler(s_server, &portal_routes[i]);
    }

    /* Captive-portal probes hit unknown URIs — route 404s to the form
     * so the phone OS shows it inline rather than "no internet". */
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, handle_portal_404);

    s_started = true;
    ESP_LOGI(TAG, "portal HTTP server up — http://192.168.4.1/");
}

void http_config_update_status(const http_config_status_t *status)
{
    if (status != NULL) {
        s_status = *status;
    }
}

#endif  /* ESP_PLATFORM */
