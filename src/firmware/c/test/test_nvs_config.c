/**
 * Host tests for nvs_config — covers the pure helpers
 * (is_provisioned, parse_word_order, word_order_to_string). The NVS
 * I/O paths in nvs_config_esp.c are validated empirically on hardware.
 */
#include <string.h>

#include "nvs_config.h"
#include "tuf2000m.h"
#include "test_helpers.h"

/* ── is_provisioned ─────────────────────────────────────────────── */

static void test_is_provisioned_when_all_required_set(void)
{
    nvs_config_t cfg = {0};
    strcpy(cfg.wifi_ssid,        "HouseNet");
    strcpy(cfg.wifi_password,    "secret");
    strcpy(cfg.homehub_url,      "http://10.0.0.1:3000");
    strcpy(cfg.internal_api_key, "abcdef");
    strcpy(cfg.sensor_id,        "auraflow-mainline-01");
    ASSERT_TRUE(nvs_config_is_provisioned(&cfg));
}

static void test_not_provisioned_missing_homehub_url(void)
{
    nvs_config_t cfg = {0};
    strcpy(cfg.wifi_ssid,        "x");
    strcpy(cfg.internal_api_key, "x");
    strcpy(cfg.sensor_id,        "x");
    ASSERT_TRUE(!nvs_config_is_provisioned(&cfg));
}

static void test_not_provisioned_missing_sensor_id(void)
{
    nvs_config_t cfg = {0};
    strcpy(cfg.wifi_ssid,        "x");
    strcpy(cfg.homehub_url,      "x");
    strcpy(cfg.internal_api_key, "x");
    ASSERT_TRUE(!nvs_config_is_provisioned(&cfg));
}

static void test_password_is_optional_for_open_networks(void)
{
    nvs_config_t cfg = {0};
    strcpy(cfg.wifi_ssid,        "OpenGuestNet");
    /* no password */
    strcpy(cfg.homehub_url,      "http://10.0.0.1:3000");
    strcpy(cfg.internal_api_key, "x");
    strcpy(cfg.sensor_id,        "x");
    ASSERT_TRUE(nvs_config_is_provisioned(&cfg));
}

static void test_is_provisioned_null_safe(void)
{
    ASSERT_TRUE(!nvs_config_is_provisioned(NULL));
}

/* ── parse_word_order ─────────────────────────────────────────── */

static void test_parse_high_word_first(void)
{
    ASSERT_EQ(nvs_config_parse_word_order("high-word-first"),
              TUF2000M_HIGH_WORD_FIRST);
}

static void test_parse_low_word_first(void)
{
    ASSERT_EQ(nvs_config_parse_word_order("low-word-first"),
              TUF2000M_LOW_WORD_FIRST);
}

static void test_parse_invalid_defaults_to_low(void)
{
    ASSERT_EQ(nvs_config_parse_word_order("middle-out"),    TUF2000M_LOW_WORD_FIRST);
    ASSERT_EQ(nvs_config_parse_word_order(""),              TUF2000M_LOW_WORD_FIRST);
    ASSERT_EQ(nvs_config_parse_word_order("HIGH-word-first"), TUF2000M_LOW_WORD_FIRST);
}

static void test_parse_null_defaults_to_low(void)
{
    ASSERT_EQ(nvs_config_parse_word_order(NULL), TUF2000M_LOW_WORD_FIRST);
}

/* ── word_order_to_string ─────────────────────────────────────── */

static void test_to_string_low(void)
{
    const char *s = nvs_config_word_order_to_string(TUF2000M_LOW_WORD_FIRST);
    ASSERT_TRUE(s != NULL);
    ASSERT_TRUE(strcmp(s, "low-word-first") == 0);
}

static void test_to_string_high(void)
{
    const char *s = nvs_config_word_order_to_string(TUF2000M_HIGH_WORD_FIRST);
    ASSERT_TRUE(s != NULL);
    ASSERT_TRUE(strcmp(s, "high-word-first") == 0);
}

static void test_word_order_round_trip(void)
{
    /* parse(to_string(x)) == x for both values */
    ASSERT_EQ(nvs_config_parse_word_order(
                  nvs_config_word_order_to_string(TUF2000M_LOW_WORD_FIRST)),
              TUF2000M_LOW_WORD_FIRST);
    ASSERT_EQ(nvs_config_parse_word_order(
                  nvs_config_word_order_to_string(TUF2000M_HIGH_WORD_FIRST)),
              TUF2000M_HIGH_WORD_FIRST);
}

int main(void)
{
    RUN(test_is_provisioned_when_all_required_set);
    RUN(test_not_provisioned_missing_homehub_url);
    RUN(test_not_provisioned_missing_sensor_id);
    RUN(test_password_is_optional_for_open_networks);
    RUN(test_is_provisioned_null_safe);
    RUN(test_parse_high_word_first);
    RUN(test_parse_low_word_first);
    RUN(test_parse_invalid_defaults_to_low);
    RUN(test_parse_null_defaults_to_low);
    RUN(test_to_string_low);
    RUN(test_to_string_high);
    RUN(test_word_order_round_trip);
    TEST_SUMMARY_AND_EXIT();
}
