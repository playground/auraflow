/**
 * Host tests for provisioning.c — covers the pure helpers
 * (strip_prefix, is_valid_word_order, validate_struct). The cJSON
 * parsing + UART listener live in provisioning_esp.c and are validated
 * empirically on hardware.
 */
#include <string.h>

#include "nvs_config.h"
#include "provisioning.h"
#include "tuf2000m.h"
#include "test_helpers.h"

/* ── strip_prefix ──────────────────────────────────────────────── */

static void test_strip_prefix_returns_body(void)
{
    const char *body = provisioning_strip_prefix("PROVISION:{}");
    ASSERT_TRUE(body != NULL);
    ASSERT_TRUE(strcmp(body, "{}") == 0);
}

static void test_strip_prefix_skips_leading_whitespace(void)
{
    const char *body = provisioning_strip_prefix("  \r\nPROVISION:{}");
    ASSERT_TRUE(body != NULL);
    ASSERT_TRUE(strcmp(body, "{}") == 0);
}

static void test_strip_prefix_returns_null_when_missing(void)
{
    ASSERT_TRUE(provisioning_strip_prefix("{}") == NULL);
    ASSERT_TRUE(provisioning_strip_prefix("HELLO:{}") == NULL);
}

static void test_strip_prefix_is_case_sensitive(void)
{
    /* The web flasher always sends uppercase PROVISION; tolerating other
     * cases would invite ambiguity. */
    ASSERT_TRUE(provisioning_strip_prefix("provision:{}") == NULL);
    ASSERT_TRUE(provisioning_strip_prefix("Provision:{}") == NULL);
}

static void test_strip_prefix_null_input_safe(void)
{
    ASSERT_TRUE(provisioning_strip_prefix(NULL) == NULL);
}

/* ── is_valid_word_order ──────────────────────────────────────── */

static void test_word_order_low_is_valid(void)
{
    ASSERT_TRUE(provisioning_is_valid_word_order("low-word-first"));
}

static void test_word_order_high_is_valid(void)
{
    ASSERT_TRUE(provisioning_is_valid_word_order("high-word-first"));
}

static void test_word_order_null_is_valid_default(void)
{
    /* Absent → default. */
    ASSERT_TRUE(provisioning_is_valid_word_order(NULL));
    ASSERT_TRUE(provisioning_is_valid_word_order(""));
}

static void test_word_order_garbage_rejected(void)
{
    ASSERT_TRUE(!provisioning_is_valid_word_order("middle-out"));
    ASSERT_TRUE(!provisioning_is_valid_word_order("LOW-WORD-FIRST"));
    ASSERT_TRUE(!provisioning_is_valid_word_order("low_word_first"));
}

/* ── validate_struct ───────────────────────────────────────────── */

static void populate_complete(nvs_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->wifi_ssid,        "HouseNet");
    strcpy(cfg->wifi_password,    "secret");
    strcpy(cfg->homehub_url,      "http://10.0.0.1:3000");
    strcpy(cfg->internal_api_key, "abcdef");
    strcpy(cfg->sensor_id,        "auraflow-mainline-01");
    cfg->word_order = TUF2000M_LOW_WORD_FIRST;
}

static void test_validate_complete_struct_passes(void)
{
    nvs_config_t cfg;
    populate_complete(&cfg);
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_OK);
}

static void test_validate_empty_wifi_ssid_fails(void)
{
    nvs_config_t cfg;
    populate_complete(&cfg);
    cfg.wifi_ssid[0] = '\0';
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_ERR_EMPTY_FIELD);
    ASSERT_TRUE(strcmp(r.field_name, "wifiSsid") == 0);
}

static void test_validate_empty_wifi_password_fails(void)
{
    /* Provisioning REQUIRES wifiPassword (matches TS REQUIRED_FIELDS),
     * even though nvs_config_is_provisioned tolerates empty for open
     * networks at boot time. */
    nvs_config_t cfg;
    populate_complete(&cfg);
    cfg.wifi_password[0] = '\0';
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_ERR_EMPTY_FIELD);
    ASSERT_TRUE(strcmp(r.field_name, "wifiPassword") == 0);
}

static void test_validate_empty_homehub_url_fails(void)
{
    nvs_config_t cfg;
    populate_complete(&cfg);
    cfg.homehub_url[0] = '\0';
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_ERR_EMPTY_FIELD);
    ASSERT_TRUE(strcmp(r.field_name, "homehubUrl") == 0);
}

static void test_validate_empty_api_key_fails(void)
{
    nvs_config_t cfg;
    populate_complete(&cfg);
    cfg.internal_api_key[0] = '\0';
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_ERR_EMPTY_FIELD);
    ASSERT_TRUE(strcmp(r.field_name, "internalApiKey") == 0);
}

static void test_validate_empty_sensor_id_fails(void)
{
    nvs_config_t cfg;
    populate_complete(&cfg);
    cfg.sensor_id[0] = '\0';
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_ERR_EMPTY_FIELD);
    ASSERT_TRUE(strcmp(r.field_name, "sensorId") == 0);
}

static void test_validate_first_missing_field_wins(void)
{
    /* Empty wifi_ssid AND empty homehub_url — error names wifi_ssid (declared first). */
    nvs_config_t cfg;
    populate_complete(&cfg);
    cfg.wifi_ssid[0]   = '\0';
    cfg.homehub_url[0] = '\0';
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_ERR_EMPTY_FIELD);
    ASSERT_TRUE(strcmp(r.field_name, "wifiSsid") == 0);
}

static void test_validate_null_safe(void)
{
    provision_result_t r = provisioning_validate_struct(NULL);
    ASSERT_EQ(r.status, PROVISION_ERR_EMPTY_FIELD);
}

/* ── static IP: optional, but all-or-nothing ──────────────────── */

static void test_validate_no_static_ip_passes(void)
{
    nvs_config_t cfg;
    populate_complete(&cfg);
    /* All three static fields empty → DHCP, valid. */
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_OK);
}

static void test_validate_full_static_ip_passes(void)
{
    nvs_config_t cfg;
    populate_complete(&cfg);
    strcpy(cfg.static_ip,      "192.168.1.42");
    strcpy(cfg.static_gateway, "192.168.1.1");
    strcpy(cfg.static_netmask, "255.255.255.0");
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_OK);
}

static void test_validate_partial_static_ip_missing_gateway_fails(void)
{
    nvs_config_t cfg;
    populate_complete(&cfg);
    strcpy(cfg.static_ip,      "192.168.1.42");
    /* gateway empty */
    strcpy(cfg.static_netmask, "255.255.255.0");
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_ERR_EMPTY_FIELD);
    ASSERT_TRUE(strcmp(r.field_name, "staticGateway") == 0);
}

static void test_validate_partial_static_ip_missing_netmask_fails(void)
{
    nvs_config_t cfg;
    populate_complete(&cfg);
    strcpy(cfg.static_ip,      "192.168.1.42");
    strcpy(cfg.static_gateway, "192.168.1.1");
    /* netmask empty */
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_ERR_EMPTY_FIELD);
    ASSERT_TRUE(strcmp(r.field_name, "staticNetmask") == 0);
}

static void test_validate_only_gateway_set_fails(void)
{
    nvs_config_t cfg;
    populate_complete(&cfg);
    strcpy(cfg.static_gateway, "192.168.1.1");
    /* ip + netmask empty */
    provision_result_t r = provisioning_validate_struct(&cfg);
    ASSERT_EQ(r.status, PROVISION_ERR_EMPTY_FIELD);
    ASSERT_TRUE(strcmp(r.field_name, "staticIp") == 0);
}

/* ── nvs_config_uses_static_ip ───────────────────────────────── */

static void test_uses_static_ip_all_empty_false(void)
{
    nvs_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    ASSERT_TRUE(!nvs_config_uses_static_ip(&cfg));
}

static void test_uses_static_ip_all_set_true(void)
{
    nvs_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.static_ip,      "192.168.1.42");
    strcpy(cfg.static_gateway, "192.168.1.1");
    strcpy(cfg.static_netmask, "255.255.255.0");
    ASSERT_TRUE(nvs_config_uses_static_ip(&cfg));
}

static void test_uses_static_ip_partial_false(void)
{
    nvs_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.static_ip, "192.168.1.42");
    /* gateway, netmask empty */
    ASSERT_TRUE(!nvs_config_uses_static_ip(&cfg));
}

static void test_uses_static_ip_null_safe(void)
{
    ASSERT_TRUE(!nvs_config_uses_static_ip(NULL));
}

int main(void)
{
    RUN(test_strip_prefix_returns_body);
    RUN(test_strip_prefix_skips_leading_whitespace);
    RUN(test_strip_prefix_returns_null_when_missing);
    RUN(test_strip_prefix_is_case_sensitive);
    RUN(test_strip_prefix_null_input_safe);
    RUN(test_word_order_low_is_valid);
    RUN(test_word_order_high_is_valid);
    RUN(test_word_order_null_is_valid_default);
    RUN(test_word_order_garbage_rejected);
    RUN(test_validate_complete_struct_passes);
    RUN(test_validate_empty_wifi_ssid_fails);
    RUN(test_validate_empty_wifi_password_fails);
    RUN(test_validate_empty_homehub_url_fails);
    RUN(test_validate_empty_api_key_fails);
    RUN(test_validate_empty_sensor_id_fails);
    RUN(test_validate_first_missing_field_wins);
    RUN(test_validate_null_safe);
    RUN(test_validate_no_static_ip_passes);
    RUN(test_validate_full_static_ip_passes);
    RUN(test_validate_partial_static_ip_missing_gateway_fails);
    RUN(test_validate_partial_static_ip_missing_netmask_fails);
    RUN(test_validate_only_gateway_set_fails);
    RUN(test_uses_static_ip_all_empty_false);
    RUN(test_uses_static_ip_all_set_true);
    RUN(test_uses_static_ip_partial_false);
    RUN(test_uses_static_ip_null_safe);
    TEST_SUMMARY_AND_EXIT();
}
