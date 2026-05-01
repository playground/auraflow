/**
 * Host tests for uplink.c — JSON request body construction, default
 * poll config, init bookkeeping. The HTTP path + cJSON response
 * decoding live in uplink_esp.c and are validated empirically.
 */
#include <string.h>

#include "test_helpers.h"
#include "uplink.h"

/* ── Helpers ──────────────────────────────────────────────────── */

static void make_cfg(uplink_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->homehub_url,      "http://10.0.0.1:3000");
    strcpy(cfg->internal_api_key, "secret");
    strcpy(cfg->sensor_id,        "auraflow-mainline-01");
}

static void make_minimal_reading(uplink_reading_t *r)
{
    memset(r, 0, sizeof(*r));
    r->rate_m3h       = 0.42f;
    r->enqueued_at_ms = 1000;
}

/* ── Required-only body ───────────────────────────────────────── */

static void test_minimal_body_has_required_fields(void)
{
    uplink_config_t  cfg;
    uplink_reading_t r;
    char buf[512];
    make_cfg(&cfg);
    make_minimal_reading(&r);

    int n = uplink_build_request_json(buf, sizeof(buf), &cfg, &r, /*now*/ 1000);
    ASSERT_TRUE(n > 0);
    /* Required fields present. */
    ASSERT_TRUE(strstr(buf, "\"sensorId\":\"auraflow-mainline-01\"") != NULL);
    ASSERT_TRUE(strstr(buf, "\"rateM3h\":0.42")                     != NULL);
    ASSERT_TRUE(strstr(buf, "\"tOffsetMs\":0")                      != NULL);
    /* Optional fields absent. */
    ASSERT_TRUE(strstr(buf, "totalM3")           == NULL);
    ASSERT_TRUE(strstr(buf, "signalQuality")     == NULL);
    ASSERT_TRUE(strstr(buf, "rssi")              == NULL);
    ASSERT_TRUE(strstr(buf, "uptimeSec")         == NULL);
    ASSERT_TRUE(strstr(buf, "firmwareVersion")   == NULL);
    ASSERT_TRUE(strstr(buf, "mac")               == NULL);
    ASSERT_TRUE(strstr(buf, "bootReason")        == NULL);
    /* Ends with closing brace. */
    ASSERT_EQ(buf[n - 1], '}');
}

/* ── tOffsetMs ────────────────────────────────────────────────── */

static void test_t_offset_is_difference(void)
{
    uplink_config_t  cfg;
    uplink_reading_t r;
    char buf[512];
    make_cfg(&cfg);
    make_minimal_reading(&r);
    r.enqueued_at_ms = 5000;

    int n = uplink_build_request_json(buf, sizeof(buf), &cfg, &r, /*now*/ 8500);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "\"tOffsetMs\":3500") != NULL);
}

static void test_t_offset_clamps_to_zero_when_negative(void)
{
    uplink_config_t  cfg;
    uplink_reading_t r;
    char buf[512];
    make_cfg(&cfg);
    make_minimal_reading(&r);
    r.enqueued_at_ms = 9000;

    /* now BEFORE enqueue (clock skew) — tOffsetMs should clamp to 0. */
    int n = uplink_build_request_json(buf, sizeof(buf), &cfg, &r, /*now*/ 1000);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "\"tOffsetMs\":0") != NULL);
}

/* ── Optional fields included when present ─────────────────────── */

static void test_optional_total_m3_included(void)
{
    uplink_config_t  cfg;
    uplink_reading_t r;
    char buf[512];
    make_cfg(&cfg);
    make_minimal_reading(&r);
    r.has_total_m3 = true;
    r.total_m3     = 1234.567f;

    int n = uplink_build_request_json(buf, sizeof(buf), &cfg, &r, 1000);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "\"totalM3\":1234.5") != NULL);
}

static void test_optional_signal_quality_included(void)
{
    uplink_config_t  cfg;
    uplink_reading_t r;
    char buf[512];
    make_cfg(&cfg);
    make_minimal_reading(&r);
    r.has_signal_quality = true;
    r.signal_quality     = 87;

    int n = uplink_build_request_json(buf, sizeof(buf), &cfg, &r, 1000);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "\"signalQuality\":87") != NULL);
}

static void test_optional_rssi_included(void)
{
    uplink_config_t  cfg;
    uplink_reading_t r;
    char buf[512];
    make_cfg(&cfg);
    make_minimal_reading(&r);
    r.has_rssi = true;
    r.rssi     = -62;

    int n = uplink_build_request_json(buf, sizeof(buf), &cfg, &r, 1000);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "\"rssi\":-62") != NULL);
}

static void test_string_fields_included_when_non_empty(void)
{
    uplink_config_t  cfg;
    uplink_reading_t r;
    char buf[512];
    make_cfg(&cfg);
    make_minimal_reading(&r);
    strcpy(r.firmware_version, "0.2.0");
    strcpy(r.mac,               "AA:BB:CC:DD:EE:FF");
    strcpy(r.boot_reason,       "watchdog");

    int n = uplink_build_request_json(buf, sizeof(buf), &cfg, &r, 1000);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(strstr(buf, "\"firmwareVersion\":\"0.2.0\"")    != NULL);
    ASSERT_TRUE(strstr(buf, "\"mac\":\"AA:BB:CC:DD:EE:FF\"")    != NULL);
    ASSERT_TRUE(strstr(buf, "\"bootReason\":\"watchdog\"")      != NULL);
}

/* ── Buffer overflow ──────────────────────────────────────────── */

static void test_overflow_returns_negative(void)
{
    uplink_config_t  cfg;
    uplink_reading_t r;
    char buf[16];   /* nowhere near enough */
    make_cfg(&cfg);
    make_minimal_reading(&r);

    int n = uplink_build_request_json(buf, sizeof(buf), &cfg, &r, 1000);
    ASSERT_EQ(n, -1);
}

static void test_null_inputs_return_negative(void)
{
    uplink_config_t  cfg;
    uplink_reading_t r;
    char buf[64];
    make_cfg(&cfg);
    make_minimal_reading(&r);

    ASSERT_EQ(uplink_build_request_json(NULL, sizeof(buf), &cfg, &r, 0), -1);
    ASSERT_EQ(uplink_build_request_json(buf, 0,            &cfg, &r, 0), -1);
    ASSERT_EQ(uplink_build_request_json(buf, sizeof(buf), NULL, &r, 0), -1);
    ASSERT_EQ(uplink_build_request_json(buf, sizeof(buf), &cfg, NULL, 0), -1);
}

/* ── Default poll config ──────────────────────────────────────── */

static void test_default_poll_config_constants(void)
{
    ASSERT_EQ(UPLINK_DEFAULT_POLL_CONFIG.poll_interval_ms,         5000);
    ASSERT_EQ(UPLINK_DEFAULT_POLL_CONFIG.flowing_poll_interval_ms, 5000);
    ASSERT_EQ(UPLINK_DEFAULT_POLL_CONFIG.idle_poll_interval_ms,    30000);
    ASSERT_EQ(UPLINK_DEFAULT_POLL_CONFIG.config_version,           1);
}

static void test_apply_defaults_fills_zero_fields(void)
{
    uplink_poll_config_t pc = { 0 };
    uplink_apply_poll_defaults(&pc);
    ASSERT_EQ(pc.poll_interval_ms,         UPLINK_DEFAULT_POLL_INTERVAL_MS);
    ASSERT_EQ(pc.flowing_poll_interval_ms, UPLINK_DEFAULT_FLOWING_POLL_INTERVAL_MS);
    ASSERT_EQ(pc.idle_poll_interval_ms,    UPLINK_DEFAULT_IDLE_POLL_INTERVAL_MS);
    ASSERT_EQ(pc.config_version,           UPLINK_DEFAULT_CONFIG_VERSION);
}

static void test_apply_defaults_preserves_set_fields(void)
{
    uplink_poll_config_t pc = {
        .poll_interval_ms         = 7777,
        .flowing_poll_interval_ms = 2000,
        .idle_poll_interval_ms    = 60000,
        .config_version           = 42,
    };
    uplink_apply_poll_defaults(&pc);
    ASSERT_EQ(pc.poll_interval_ms,         7777);
    ASSERT_EQ(pc.flowing_poll_interval_ms, 2000);
    ASSERT_EQ(pc.idle_poll_interval_ms,    60000);
    ASSERT_EQ(pc.config_version,           42);
}

static void test_apply_defaults_negative_treated_as_missing(void)
{
    uplink_poll_config_t pc = {
        .poll_interval_ms         = -1,
        .flowing_poll_interval_ms = 0,
        .idle_poll_interval_ms    = -100,
        .config_version           = 0,
    };
    uplink_apply_poll_defaults(&pc);
    ASSERT_EQ(pc.poll_interval_ms,         UPLINK_DEFAULT_POLL_INTERVAL_MS);
    ASSERT_EQ(pc.flowing_poll_interval_ms, UPLINK_DEFAULT_FLOWING_POLL_INTERVAL_MS);
    ASSERT_EQ(pc.idle_poll_interval_ms,    UPLINK_DEFAULT_IDLE_POLL_INTERVAL_MS);
    ASSERT_EQ(pc.config_version,           UPLINK_DEFAULT_CONFIG_VERSION);
}

/* ── uplink_init / pending ────────────────────────────────────── */

static void test_init_starts_with_empty_buffer(void)
{
    uplink_config_t cfg;
    uplink_t        u;
    make_cfg(&cfg);
    uplink_init(&u, &cfg);
    ASSERT_EQ(uplink_pending(&u), 0u);
    ASSERT_EQ(u.last_poll_config.poll_interval_ms, UPLINK_DEFAULT_POLL_INTERVAL_MS);
    ASSERT_TRUE(strcmp(u.cfg.sensor_id, "auraflow-mainline-01") == 0);
}

static void test_pending_null_safe(void)
{
    ASSERT_EQ(uplink_pending(NULL), 0u);
}

int main(void)
{
    RUN(test_minimal_body_has_required_fields);
    RUN(test_t_offset_is_difference);
    RUN(test_t_offset_clamps_to_zero_when_negative);
    RUN(test_optional_total_m3_included);
    RUN(test_optional_signal_quality_included);
    RUN(test_optional_rssi_included);
    RUN(test_string_fields_included_when_non_empty);
    RUN(test_overflow_returns_negative);
    RUN(test_null_inputs_return_negative);
    RUN(test_default_poll_config_constants);
    RUN(test_apply_defaults_fills_zero_fields);
    RUN(test_apply_defaults_preserves_set_fields);
    RUN(test_apply_defaults_negative_treated_as_missing);
    RUN(test_init_starts_with_empty_buffer);
    RUN(test_pending_null_safe);
    TEST_SUMMARY_AND_EXIT();
}
