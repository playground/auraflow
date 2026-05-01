/**
 * Host tests for ring_buffer.c — mirrors src/firmware/ts/ring-buffer.test.ts.
 */
#include <stdint.h>
#include <string.h>

#include "ring_buffer.h"
#include "test_helpers.h"

/* ── init validation ───────────────────────────────────────────── */

static void test_init_rejects_invalid_args(void)
{
    ring_buffer_t rb;
    int storage[4];
    ASSERT_TRUE(!ring_buffer_init(&rb, NULL, sizeof(int), 4));
    ASSERT_TRUE(!ring_buffer_init(&rb, storage, 0, 4));
    ASSERT_TRUE(!ring_buffer_init(&rb, storage, sizeof(int), 0));
    ASSERT_TRUE( ring_buffer_init(&rb, storage, sizeof(int), 4));
}

/* ── empty state ───────────────────────────────────────────────── */

static void test_starts_empty(void)
{
    ring_buffer_t rb;
    int storage[3];
    ring_buffer_init(&rb, storage, sizeof(int), 3);

    ASSERT_EQ(ring_buffer_length(&rb), 0u);
    ASSERT_TRUE(ring_buffer_is_empty(&rb));
    ASSERT_TRUE(!ring_buffer_is_full(&rb));

    int out;
    ASSERT_TRUE(!ring_buffer_shift(&rb, &out));
    ASSERT_TRUE(!ring_buffer_peek(&rb, &out));
}

/* ── FIFO order under capacity ─────────────────────────────────── */

static void test_fifo_order(void)
{
    ring_buffer_t rb;
    int storage[3];
    ring_buffer_init(&rb, storage, sizeof(int), 3);

    int a = 10, b = 20, c = 30;
    ring_buffer_push(&rb, &a);
    ring_buffer_push(&rb, &b);
    ring_buffer_push(&rb, &c);

    ASSERT_EQ(ring_buffer_length(&rb), 3u);
    ASSERT_TRUE(ring_buffer_is_full(&rb));

    int out;
    ASSERT_TRUE(ring_buffer_shift(&rb, &out)); ASSERT_EQ(out, 10);
    ASSERT_TRUE(ring_buffer_shift(&rb, &out)); ASSERT_EQ(out, 20);
    ASSERT_TRUE(ring_buffer_shift(&rb, &out)); ASSERT_EQ(out, 30);
    ASSERT_TRUE(!ring_buffer_shift(&rb, &out));
}

/* ── overflow drops the oldest ─────────────────────────────────── */

static void test_overflow_drops_oldest(void)
{
    ring_buffer_t rb;
    int storage[2];
    ring_buffer_init(&rb, storage, sizeof(int), 2);

    int a = 10, b = 20, c = 30;
    ring_buffer_push(&rb, &a);
    ring_buffer_push(&rb, &b);
    ring_buffer_push(&rb, &c);  /* drops `a` */

    ASSERT_EQ(ring_buffer_length(&rb), 2u);

    int out;
    ASSERT_TRUE(ring_buffer_shift(&rb, &out)); ASSERT_EQ(out, 20);
    ASSERT_TRUE(ring_buffer_shift(&rb, &out)); ASSERT_EQ(out, 30);
}

static void test_repeated_overflow_keeps_advancing(void)
{
    ring_buffer_t rb;
    int storage[2];
    ring_buffer_init(&rb, storage, sizeof(int), 2);

    for (int i = 1; i <= 10; i++) {
        ring_buffer_push(&rb, &i);
    }
    /* Buffer should now hold the last two pushed: 9, 10 */

    int out;
    ring_buffer_shift(&rb, &out); ASSERT_EQ(out, 9);
    ring_buffer_shift(&rb, &out); ASSERT_EQ(out, 10);
    ASSERT_TRUE(ring_buffer_is_empty(&rb));
}

/* ── multi-field elements preserved byte-for-byte ──────────────── */

typedef struct {
    int     value;
    int64_t enqueued_at_ms;
    float   rate;
} test_item_t;

static void test_multi_field_elements(void)
{
    ring_buffer_t rb;
    test_item_t storage[3];
    ring_buffer_init(&rb, storage, sizeof(test_item_t), 3);

    test_item_t a = { .value = 1, .enqueued_at_ms = 100, .rate = 0.5f };
    test_item_t b = { .value = 2, .enqueued_at_ms = 200, .rate = 1.5f };
    ring_buffer_push(&rb, &a);
    ring_buffer_push(&rb, &b);

    test_item_t out;
    ASSERT_TRUE(ring_buffer_shift(&rb, &out));
    ASSERT_EQ(out.value, 1);
    ASSERT_EQ((long)out.enqueued_at_ms, 100L);
    ASSERT_FLOAT_NEAR(out.rate, 0.5, 0.0);

    ASSERT_TRUE(ring_buffer_shift(&rb, &out));
    ASSERT_EQ(out.value, 2);
    ASSERT_EQ((long)out.enqueued_at_ms, 200L);
    ASSERT_FLOAT_NEAR(out.rate, 1.5, 0.0);
}

/* ── peek doesn't consume ──────────────────────────────────────── */

static void test_peek(void)
{
    ring_buffer_t rb;
    int storage[3];
    ring_buffer_init(&rb, storage, sizeof(int), 3);

    int v = 42;
    ring_buffer_push(&rb, &v);

    int out;
    ASSERT_TRUE(ring_buffer_peek(&rb, &out));
    ASSERT_EQ(out, 42);
    ASSERT_EQ(ring_buffer_length(&rb), 1u);   /* unchanged */
    ASSERT_TRUE(ring_buffer_shift(&rb, &out));
    ASSERT_EQ(out, 42);
}

/* ── drain returns FIFO and empties ────────────────────────────── */

static void test_drain(void)
{
    ring_buffer_t rb;
    int storage[4];
    ring_buffer_init(&rb, storage, sizeof(int), 4);

    int a = 1, b = 2, c = 3;
    ring_buffer_push(&rb, &a);
    ring_buffer_push(&rb, &b);
    ring_buffer_push(&rb, &c);

    int drained[8];
    size_t n = ring_buffer_drain(&rb, drained, 8);
    ASSERT_EQ(n, 3u);
    ASSERT_EQ(drained[0], 1);
    ASSERT_EQ(drained[1], 2);
    ASSERT_EQ(drained[2], 3);
    ASSERT_EQ(ring_buffer_length(&rb), 0u);
}

static void test_drain_with_max_smaller_than_count(void)
{
    ring_buffer_t rb;
    int storage[4];
    ring_buffer_init(&rb, storage, sizeof(int), 4);

    for (int i = 1; i <= 3; i++) ring_buffer_push(&rb, &i);

    int drained[2];
    size_t n = ring_buffer_drain(&rb, drained, 2);
    ASSERT_EQ(n, 2u);
    ASSERT_EQ(drained[0], 1);
    ASSERT_EQ(drained[1], 2);
    /* Third item still in buffer */
    ASSERT_EQ(ring_buffer_length(&rb), 1u);
    int out;
    ring_buffer_shift(&rb, &out);
    ASSERT_EQ(out, 3);
}

/* ── clear empties without yielding ────────────────────────────── */

static void test_clear(void)
{
    ring_buffer_t rb;
    int storage[3];
    ring_buffer_init(&rb, storage, sizeof(int), 3);

    int a = 1, b = 2;
    ring_buffer_push(&rb, &a);
    ring_buffer_push(&rb, &b);
    ring_buffer_clear(&rb);

    ASSERT_EQ(ring_buffer_length(&rb), 0u);
    int out;
    ASSERT_TRUE(!ring_buffer_shift(&rb, &out));
}

/* ── is_full reflects capacity ─────────────────────────────────── */

static void test_is_full(void)
{
    ring_buffer_t rb;
    int storage[2];
    ring_buffer_init(&rb, storage, sizeof(int), 2);

    ASSERT_TRUE(!ring_buffer_is_full(&rb));
    int v = 1;
    ring_buffer_push(&rb, &v); ASSERT_TRUE(!ring_buffer_is_full(&rb));
    ring_buffer_push(&rb, &v); ASSERT_TRUE( ring_buffer_is_full(&rb));
}

/* ── shift with NULL out discards the value ────────────────────── */

static void test_shift_null_discards(void)
{
    ring_buffer_t rb;
    int storage[3];
    ring_buffer_init(&rb, storage, sizeof(int), 3);

    int a = 1, b = 2;
    ring_buffer_push(&rb, &a);
    ring_buffer_push(&rb, &b);

    ASSERT_TRUE(ring_buffer_shift(&rb, NULL));
    ASSERT_EQ(ring_buffer_length(&rb), 1u);
    int out;
    ASSERT_TRUE(ring_buffer_shift(&rb, &out));
    ASSERT_EQ(out, 2);
}

int main(void)
{
    RUN(test_init_rejects_invalid_args);
    RUN(test_starts_empty);
    RUN(test_fifo_order);
    RUN(test_overflow_drops_oldest);
    RUN(test_repeated_overflow_keeps_advancing);
    RUN(test_multi_field_elements);
    RUN(test_peek);
    RUN(test_drain);
    RUN(test_drain_with_max_smaller_than_count);
    RUN(test_clear);
    RUN(test_is_full);
    RUN(test_shift_null_discards);
    TEST_SUMMARY_AND_EXIT();
}
