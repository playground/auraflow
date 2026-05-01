/**
 * Host tests for wifi_mgr.c — anti-flap history + backoff math.
 * The esp_wifi state machine in wifi_mgr_esp.c is verified empirically.
 */
#include "test_helpers.h"
#include "wifi_mgr.h"

/* Use round-numbered pseudo-time for readability in vectors. */

/* ── history: empty + small ───────────────────────────────────── */

static void test_empty_history_not_throttled(void)
{
    wifi_mgr_history_t h;
    wifi_mgr_history_init(&h);
    ASSERT_TRUE(!wifi_mgr_history_should_throttle(&h, 0));
    ASSERT_EQ(h.count, 0u);
}

static void test_record_increments_count(void)
{
    wifi_mgr_history_t h;
    wifi_mgr_history_init(&h);
    wifi_mgr_history_record(&h, 1000);
    ASSERT_EQ(h.count, 1u);
    wifi_mgr_history_record(&h, 2000);
    ASSERT_EQ(h.count, 2u);
}

static void test_below_threshold_within_window_not_throttled(void)
{
    wifi_mgr_history_t h;
    wifi_mgr_history_init(&h);
    /* 4 connects, all in the last 5 minutes — below threshold of 5. */
    for (int i = 0; i < 4; i++) wifi_mgr_history_record(&h, 1000 * i);
    ASSERT_TRUE(!wifi_mgr_history_should_throttle(&h, 1000 * 4));
}

/* ── history: at and above threshold ──────────────────────────── */

static void test_at_threshold_within_window_throttles(void)
{
    wifi_mgr_history_t h;
    wifi_mgr_history_init(&h);
    /* 5 connects in 5 seconds. */
    for (int i = 0; i < 5; i++) wifi_mgr_history_record(&h, 1000 * i);
    /* `now` is shortly after the last connect — well within the hour window. */
    ASSERT_TRUE(wifi_mgr_history_should_throttle(&h, 6000));
}

static void test_count_caps_at_threshold(void)
{
    wifi_mgr_history_t h;
    wifi_mgr_history_init(&h);
    /* 10 connects but only the last 5 are retained. */
    for (int i = 0; i < 10; i++) wifi_mgr_history_record(&h, 1000 * i);
    ASSERT_EQ(h.count, (size_t)WIFI_MGR_FLAP_THRESHOLD);
    /* Oldest retained is the 6th (index 5) at t=5000. */
    ASSERT_EQ(h.times[0], 5000);
    ASSERT_EQ(h.times[4], 9000);
}

/* ── history: window aging ────────────────────────────────────── */

static void test_old_entries_outside_window_not_throttled(void)
{
    wifi_mgr_history_t h;
    wifi_mgr_history_init(&h);
    /* 5 connects all happened more than 1 hour before `now`. */
    int64_t base = 0;
    for (int i = 0; i < 5; i++) wifi_mgr_history_record(&h, base + 100 * i);

    int64_t now = base + WIFI_MGR_FLAP_WINDOW_MS + 1000;
    ASSERT_TRUE(!wifi_mgr_history_should_throttle(&h, now));
}

static void test_partial_aging(void)
{
    wifi_mgr_history_t h;
    wifi_mgr_history_init(&h);
    /* First 3 connects very long ago, last 2 just now. */
    int64_t old = 0;
    int64_t now = 10 * 60 * 1000;     /* 10 min */
    int64_t very_old = now - WIFI_MGR_FLAP_WINDOW_MS - 1000;

    wifi_mgr_history_record(&h, very_old);
    wifi_mgr_history_record(&h, very_old + 100);
    wifi_mgr_history_record(&h, very_old + 200);
    wifi_mgr_history_record(&h, now - 1000);
    wifi_mgr_history_record(&h, now);

    /* Only 2 entries fall within the window — below threshold. */
    ASSERT_TRUE(!wifi_mgr_history_should_throttle(&h, now));
    (void)old;
}

static void test_history_null_safe(void)
{
    ASSERT_TRUE(!wifi_mgr_history_should_throttle(NULL, 0));
    /* record(NULL) shouldn't crash either. */
    wifi_mgr_history_record(NULL, 0);
}

/* ── backoff math ─────────────────────────────────────────────── */

static void test_backoff_doubles(void)
{
    ASSERT_EQ(wifi_mgr_next_backoff_ms(1000),  2000);
    ASSERT_EQ(wifi_mgr_next_backoff_ms(2000),  4000);
    ASSERT_EQ(wifi_mgr_next_backoff_ms(4000),  8000);
    ASSERT_EQ(wifi_mgr_next_backoff_ms(8000),  16000);
    ASSERT_EQ(wifi_mgr_next_backoff_ms(16000), 32000);
}

static void test_backoff_caps_at_max(void)
{
    /* 32000 doubles to 64000, capped at WIFI_MGR_MAX_BACKOFF_MS = 60000. */
    ASSERT_EQ(wifi_mgr_next_backoff_ms(32000), WIFI_MGR_MAX_BACKOFF_MS);
    ASSERT_EQ(wifi_mgr_next_backoff_ms(60000), WIFI_MGR_MAX_BACKOFF_MS);
    ASSERT_EQ(wifi_mgr_next_backoff_ms(WIFI_MGR_MAX_BACKOFF_MS), WIFI_MGR_MAX_BACKOFF_MS);
}

static void test_backoff_zero_or_negative_returns_initial(void)
{
    ASSERT_EQ(wifi_mgr_next_backoff_ms(0),    WIFI_MGR_INITIAL_BACKOFF_MS);
    ASSERT_EQ(wifi_mgr_next_backoff_ms(-1),   WIFI_MGR_INITIAL_BACKOFF_MS);
    ASSERT_EQ(wifi_mgr_next_backoff_ms(-100), WIFI_MGR_INITIAL_BACKOFF_MS);
}

int main(void)
{
    RUN(test_empty_history_not_throttled);
    RUN(test_record_increments_count);
    RUN(test_below_threshold_within_window_not_throttled);
    RUN(test_at_threshold_within_window_throttles);
    RUN(test_count_caps_at_threshold);
    RUN(test_old_entries_outside_window_not_throttled);
    RUN(test_partial_aging);
    RUN(test_history_null_safe);
    RUN(test_backoff_doubles);
    RUN(test_backoff_caps_at_max);
    RUN(test_backoff_zero_or_negative_returns_initial);
    TEST_SUMMARY_AND_EXIT();
}
