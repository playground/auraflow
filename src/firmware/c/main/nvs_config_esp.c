/**
 * ESP-IDF NVS I/O for nvs_config. Only built into the firmware (the host
 * test build filters out *_esp.c). Pure helpers are in nvs_config.c.
 */
#ifdef ESP_PLATFORM

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "nvs_config.h"

#define NVS_NS  "auraflow"

static const char *TAG = "nvs_config";

bool nvs_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, recreating...");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

/* Reads a string into a fixed-size buffer; missing keys → empty string. */
static void read_str_or_empty(nvs_handle_t handle, const char *key,
                              char *out, size_t out_size)
{
    size_t length = out_size;
    esp_err_t err = nvs_get_str(handle, key, out, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND || err != ESP_OK) {
        out[0] = '\0';
    }
}

bool nvs_config_load(nvs_config_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    out->word_order = TUF2000M_DEFAULT_WORD_ORDER;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return true;   /* no namespace yet — fresh device */
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(ro) failed: %s", esp_err_to_name(err));
        return false;
    }

    read_str_or_empty(handle, "wifiSsid",       out->wifi_ssid,        sizeof(out->wifi_ssid));
    read_str_or_empty(handle, "wifiPassword",   out->wifi_password,    sizeof(out->wifi_password));
    read_str_or_empty(handle, "homehubUrl",     out->homehub_url,      sizeof(out->homehub_url));
    read_str_or_empty(handle, "internalApiKey", out->internal_api_key, sizeof(out->internal_api_key));
    read_str_or_empty(handle, "sensorId",       out->sensor_id,        sizeof(out->sensor_id));
    read_str_or_empty(handle, "staticIp",       out->static_ip,        sizeof(out->static_ip));
    read_str_or_empty(handle, "staticGateway",  out->static_gateway,   sizeof(out->static_gateway));
    read_str_or_empty(handle, "staticNetmask",  out->static_netmask,   sizeof(out->static_netmask));

    char wo[32] = {0};
    size_t wo_len = sizeof(wo);
    if (nvs_get_str(handle, "wordOrder", wo, &wo_len) == ESP_OK) {
        out->word_order = nvs_config_parse_word_order(wo);
    }

    nvs_close(handle);
    return true;
}

bool nvs_config_save(const nvs_config_t *cfg)
{
    if (cfg == NULL) return false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(rw) failed: %s", esp_err_to_name(err));
        return false;
    }

    bool ok = true;
    if (cfg->wifi_ssid[0])        ok &= (nvs_set_str(handle, "wifiSsid",       cfg->wifi_ssid)        == ESP_OK);
    if (cfg->wifi_password[0])    ok &= (nvs_set_str(handle, "wifiPassword",   cfg->wifi_password)    == ESP_OK);
    if (cfg->homehub_url[0])      ok &= (nvs_set_str(handle, "homehubUrl",     cfg->homehub_url)      == ESP_OK);
    if (cfg->internal_api_key[0]) ok &= (nvs_set_str(handle, "internalApiKey", cfg->internal_api_key) == ESP_OK);
    if (cfg->sensor_id[0])        ok &= (nvs_set_str(handle, "sensorId",       cfg->sensor_id)        == ESP_OK);
    /* Static IP — write the empty string when clearing, otherwise the
     * old NVS value would persist after a re-provision that wants DHCP. */
    ok &= (nvs_set_str(handle, "staticIp",       cfg->static_ip)        == ESP_OK);
    ok &= (nvs_set_str(handle, "staticGateway",  cfg->static_gateway)   == ESP_OK);
    ok &= (nvs_set_str(handle, "staticNetmask",  cfg->static_netmask)   == ESP_OK);
    ok &= (nvs_set_str(handle, "wordOrder",
                       nvs_config_word_order_to_string(cfg->word_order)) == ESP_OK);

    if (ok) ok &= (nvs_commit(handle) == ESP_OK);
    nvs_close(handle);
    return ok;
}

bool nvs_config_poll_load(int *poll_ms, int *flowing_ms, int *idle_ms)
{
    if (poll_ms == NULL || flowing_ms == NULL || idle_ms == NULL) return false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    int32_t p = 0, f = 0, i = 0;
    bool ok = (nvs_get_i32(handle, "pollMs",        &p) == ESP_OK)
           && (nvs_get_i32(handle, "flowingPollMs", &f) == ESP_OK)
           && (nvs_get_i32(handle, "idlePollMs",    &i) == ESP_OK);
    nvs_close(handle);

    if (ok) {
        *poll_ms    = p;
        *flowing_ms = f;
        *idle_ms    = i;
    }
    return ok;
}

bool nvs_config_poll_save(int poll_ms, int flowing_ms, int idle_ms)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "poll_save: nvs_open(rw) failed: %s", esp_err_to_name(err));
        return false;
    }

    bool ok = (nvs_set_i32(handle, "pollMs",        poll_ms)    == ESP_OK)
           && (nvs_set_i32(handle, "flowingPollMs", flowing_ms) == ESP_OK)
           && (nvs_set_i32(handle, "idlePollMs",    idle_ms)    == ESP_OK);
    if (ok) ok = (nvs_commit(handle) == ESP_OK);
    nvs_close(handle);
    return ok;
}

bool nvs_config_reset(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;   /* already empty */
    if (err != ESP_OK) return false;
    bool ok = (nvs_erase_all(handle) == ESP_OK)
           && (nvs_commit(handle)    == ESP_OK);
    nvs_close(handle);
    return ok;
}

#endif  /* ESP_PLATFORM */
