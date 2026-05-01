/**
 * Host tests for tuf2000m.c — mirrors src/firmware/ts/tuf2000m.test.ts.
 */
#include <math.h>
#include <string.h>

#include "modbus.h"
#include "tuf2000m.h"
#include "test_helpers.h"

/* ── parse_float32 — word-order semantics ──────────────────────── */

static void test_high_word_first_1_0(void)
{
    /* IEEE-754 1.0 = 0x3F800000 → bytes 3F 80 00 00.
     * In ABCD: low-address register holds the high half (0x3F80),
     * high-address register holds the low half (0x0000). */
    float v = tuf2000m_parse_float32(0x3F80, 0x0000, TUF2000M_HIGH_WORD_FIRST);
    ASSERT_FLOAT_NEAR(v, 1.0f, 0.0);
}

static void test_low_word_first_1_0(void)
{
    /* In CDAB: registers swapped from the natural layout. */
    float v = tuf2000m_parse_float32(0x0000, 0x3F80, TUF2000M_LOW_WORD_FIRST);
    ASSERT_FLOAT_NEAR(v, 1.0f, 0.0);
}

static void test_high_word_first_negative(void)
{
    /* -100.5 = 0xC2C90000 */
    float v = tuf2000m_parse_float32(0xC2C9, 0x0000, TUF2000M_HIGH_WORD_FIRST);
    ASSERT_FLOAT_NEAR(v, -100.5f, 0.0);
}

static void test_low_word_first_negative(void)
{
    float v = tuf2000m_parse_float32(0x0000, 0xC2C9, TUF2000M_LOW_WORD_FIRST);
    ASSERT_FLOAT_NEAR(v, -100.5f, 0.0);
}

static void test_low_word_first_approx_point_one(void)
{
    /* 0.1 in IEEE-754 single = 0x3DCCCCCD → bytes 3D CC CC CD.
     * CDAB layout: response bytes [CC CD 3D CC] → r0=0xCCCD, r1=0x3DCC */
    float v = tuf2000m_parse_float32(0xCCCD, 0x3DCC, TUF2000M_LOW_WORD_FIRST);
    ASSERT_FLOAT_NEAR(v, 0.1f, 1e-7);
}

static void test_default_word_order_is_low_word_first(void)
{
    float v = tuf2000m_parse_float32(0x0000, 0x3F80, TUF2000M_DEFAULT_WORD_ORDER);
    ASSERT_FLOAT_NEAR(v, 1.0f, 0.0);
}

/* ── decode_float / decode_u16 ─────────────────────────────────── */

static void test_decode_float_cdab_one(void)
{
    const uint8_t payload[] = { 0x00, 0x00, 0x3F, 0x80 };
    float v = 0.0f;
    bool ok = tuf2000m_decode_float(payload, sizeof(payload), TUF2000M_LOW_WORD_FIRST, &v);
    ASSERT_TRUE(ok);
    ASSERT_FLOAT_NEAR(v, 1.0f, 0.0);
}

static void test_decode_float_abcd_one(void)
{
    const uint8_t payload[] = { 0x3F, 0x80, 0x00, 0x00 };
    float v = 0.0f;
    bool ok = tuf2000m_decode_float(payload, sizeof(payload), TUF2000M_HIGH_WORD_FIRST, &v);
    ASSERT_TRUE(ok);
    ASSERT_FLOAT_NEAR(v, 1.0f, 0.0);
}

static void test_decode_float_too_short(void)
{
    const uint8_t payload[] = { 0x00, 0x00, 0x3F };
    float v = 0.0f;
    bool ok = tuf2000m_decode_float(payload, sizeof(payload), TUF2000M_LOW_WORD_FIRST, &v);
    ASSERT_TRUE(!ok);
}

static void test_decode_u16_basic(void)
{
    const uint8_t payload[] = { 0x00, 0x57 };  /* signal quality 87 */
    uint16_t v = 0;
    bool ok = tuf2000m_decode_u16(payload, sizeof(payload), &v);
    ASSERT_TRUE(ok);
    ASSERT_EQ(v, 87u);
}

/* ── full Modbus framing → response parsing ────────────────────── */

static size_t with_crc(uint8_t *out, const uint8_t *payload, size_t plen)
{
    memcpy(out, payload, plen);
    uint16_t crc = modbus_crc16(out, plen);
    out[plen]     = (uint8_t)(crc & 0xFFu);
    out[plen + 1] = (uint8_t)((crc >> 8) & 0xFFu);
    return plen + 2;
}

static void test_parse_float_response_cdab_one(void)
{
    /* slaveId=1, func=0x03, byteCount=4, then CDAB bytes [00 00 3F 80] */
    const uint8_t payload[] = { 0x01, 0x03, 0x04, 0x00, 0x00, 0x3F, 0x80 };
    uint8_t resp[16];
    size_t  resp_len = with_crc(resp, payload, sizeof(payload));

    float v = 0.0f;
    modbus_status_t s = tuf2000m_parse_float_response(resp, resp_len,
                                                      TUF2000M_LOW_WORD_FIRST, &v);
    ASSERT_EQ(s, MODBUS_OK);
    ASSERT_FLOAT_NEAR(v, 1.0f, 0.0);
}

static void test_parse_float_response_abcd_one(void)
{
    const uint8_t payload[] = { 0x01, 0x03, 0x04, 0x3F, 0x80, 0x00, 0x00 };
    uint8_t resp[16];
    size_t  resp_len = with_crc(resp, payload, sizeof(payload));

    float v = 0.0f;
    modbus_status_t s = tuf2000m_parse_float_response(resp, resp_len,
                                                      TUF2000M_HIGH_WORD_FIRST, &v);
    ASSERT_EQ(s, MODBUS_OK);
    ASSERT_FLOAT_NEAR(v, 1.0f, 0.0);
}

static void test_parse_u16_response_basic(void)
{
    /* count=1 register response: byteCount=2, payload [00 57] = 87 */
    const uint8_t payload[] = { 0x01, 0x03, 0x02, 0x00, 0x57 };
    uint8_t resp[16];
    size_t  resp_len = with_crc(resp, payload, sizeof(payload));

    uint16_t v = 0;
    modbus_status_t s = tuf2000m_parse_u16_response(resp, resp_len, &v);
    ASSERT_EQ(s, MODBUS_OK);
    ASSERT_EQ(v, 87u);
}

/* ── request frame builders ────────────────────────────────────── */

static void test_build_request_flow_rate(void)
{
    uint8_t f[8];
    tuf2000m_build_request(TUF2000M_REG_FLOW_RATE_M3H, f);
    ASSERT_EQ(f[0], TUF2000M_SLAVE_ID);
    ASSERT_EQ(f[1], 0x03u);
    ASSERT_EQ(f[2], 0x00u);
    ASSERT_EQ(f[3], 0x01u);   /* register address 1 */
    ASSERT_EQ(f[4], 0x00u);
    ASSERT_EQ(f[5], 0x02u);   /* count 2 */
}

static void test_build_request_totalizer(void)
{
    uint8_t f[8];
    tuf2000m_build_request(TUF2000M_REG_TOTALIZER_M3, f);
    ASSERT_EQ(f[2], (uint8_t)((TUF2000M_REG_TOTALIZER_M3.address >> 8) & 0xFFu));
    ASSERT_EQ(f[3], (uint8_t)(TUF2000M_REG_TOTALIZER_M3.address & 0xFFu));
}

static void test_build_request_signal_quality_count_one(void)
{
    uint8_t f[8];
    tuf2000m_build_request(TUF2000M_REG_SIGNAL_QUALITY, f);
    ASSERT_EQ(f[5], 0x01u);   /* signal quality is a single u16 register */
}

int main(void)
{
    RUN(test_high_word_first_1_0);
    RUN(test_low_word_first_1_0);
    RUN(test_high_word_first_negative);
    RUN(test_low_word_first_negative);
    RUN(test_low_word_first_approx_point_one);
    RUN(test_default_word_order_is_low_word_first);
    RUN(test_decode_float_cdab_one);
    RUN(test_decode_float_abcd_one);
    RUN(test_decode_float_too_short);
    RUN(test_decode_u16_basic);
    RUN(test_parse_float_response_cdab_one);
    RUN(test_parse_float_response_abcd_one);
    RUN(test_parse_u16_response_basic);
    RUN(test_build_request_flow_rate);
    RUN(test_build_request_totalizer);
    RUN(test_build_request_signal_quality_count_one);
    TEST_SUMMARY_AND_EXIT();
}
