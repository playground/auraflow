/**
 * Host tests for modbus.c — mirrors src/firmware/ts/modbus.test.ts.
 * Compiled with clang, runs in a few ms, no ESP32 needed.
 */
#include <string.h>
#include "modbus.h"
#include "test_helpers.h"

/* ── crc16 ──────────────────────────────────────────────────────── */

static void test_crc16_empty(void)
{
    ASSERT_EQ(modbus_crc16(NULL, 0), 0xFFFFu);
}

static void test_crc16_canonical_vector(void)
{
    /* Modbus spec test vector: 01 03 00 00 00 0A → CRC 0xCDC5 */
    const uint8_t frame[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A };
    ASSERT_EQ(modbus_crc16(frame, sizeof(frame)), 0xCDC5u);
}

/* ── build_read_holding ────────────────────────────────────────── */

static void test_build_frame_for_slave1_addr1_count2(void)
{
    uint8_t buf[8];
    modbus_build_read_holding(0x01, 0x0001, 0x0002, buf);

    ASSERT_EQ(buf[0], 0x01u);
    ASSERT_EQ(buf[1], 0x03u);
    ASSERT_EQ(buf[2], 0x00u);
    ASSERT_EQ(buf[3], 0x01u);
    ASSERT_EQ(buf[4], 0x00u);
    ASSERT_EQ(buf[5], 0x02u);
    /* Modbus wire-bytes: low CRC byte first. Vector matches notes.txt. */
    ASSERT_EQ(buf[6], 0x95u);
    ASSERT_EQ(buf[7], 0xCBu);
}

static void test_build_frame_crc_round_trips(void)
{
    uint8_t buf[8];
    modbus_build_read_holding(0x05, 0x0009, 0x0002, buf);
    uint16_t expected = modbus_crc16(buf, 6);
    uint16_t reported = (uint16_t)((uint16_t)buf[7] << 8) | (uint16_t)buf[6];
    ASSERT_EQ(reported, expected);
}

/* ── parse_read_holding ────────────────────────────────────────── */

static size_t with_crc(uint8_t *out, const uint8_t *payload, size_t plen)
{
    memcpy(out, payload, plen);
    uint16_t crc = modbus_crc16(out, plen);
    out[plen]     = (uint8_t)(crc & 0xFFu);
    out[plen + 1] = (uint8_t)((crc >> 8) & 0xFFu);
    return plen + 2;
}

static void test_parse_well_formed_response(void)
{
    /* 2-register response: byteCount=4, payload 12 34 56 78 */
    const uint8_t payload[] = { 0x01, 0x03, 0x04, 0x12, 0x34, 0x56, 0x78 };
    uint8_t resp[16];
    size_t  resp_len = with_crc(resp, payload, sizeof(payload));

    const uint8_t *data;
    size_t         data_len;
    modbus_status_t s = modbus_parse_read_holding(resp, resp_len, 0x01, &data, &data_len);

    ASSERT_EQ(s,        MODBUS_OK);
    ASSERT_EQ(data_len, 4u);
    ASSERT_EQ(data[0],  0x12u);
    ASSERT_EQ(data[1],  0x34u);
    ASSERT_EQ(data[2],  0x56u);
    ASSERT_EQ(data[3],  0x78u);
}

static void test_parse_truncated_response(void)
{
    const uint8_t bad[] = { 0x01, 0x03, 0x04, 0x12, 0x34 };  /* claims 4 data bytes, only has 2 */
    const uint8_t *data;
    size_t         data_len;
    modbus_status_t s = modbus_parse_read_holding(bad, sizeof(bad), 0x01, &data, &data_len);
    ASSERT_EQ(s, MODBUS_ERR_SHORT);
}

static void test_parse_corrupted_payload_fails_crc(void)
{
    const uint8_t payload[] = { 0x01, 0x03, 0x04, 0x12, 0x34, 0x56, 0x78 };
    uint8_t resp[16];
    size_t  resp_len = with_crc(resp, payload, sizeof(payload));
    resp[3] ^= 0xFFu;  /* flip a byte in the data — CRC still valid for the original */

    const uint8_t *data;
    size_t         data_len;
    modbus_status_t s = modbus_parse_read_holding(resp, resp_len, 0x01, &data, &data_len);
    ASSERT_EQ(s, MODBUS_ERR_CRC);
}

static void test_parse_exception_response(void)
{
    /* 0x83 = exception for 0x03; exception code 0x02 = illegal data address */
    const uint8_t payload[] = { 0x01, 0x83, 0x02 };
    uint8_t resp[16];
    size_t  resp_len = with_crc(resp, payload, sizeof(payload));

    const uint8_t *data;
    size_t         data_len;
    modbus_status_t s = modbus_parse_read_holding(resp, resp_len, 0x01, &data, &data_len);
    ASSERT_EQ(s, MODBUS_ERR_EXCEPTION);
}

static void test_parse_slave_id_mismatch(void)
{
    const uint8_t payload[] = { 0x05, 0x03, 0x02, 0x12, 0x34 };
    uint8_t resp[16];
    size_t  resp_len = with_crc(resp, payload, sizeof(payload));

    const uint8_t *data;
    size_t         data_len;
    modbus_status_t s = modbus_parse_read_holding(resp, resp_len, 0x01, &data, &data_len);
    ASSERT_EQ(s, MODBUS_ERR_UNEXPECTED);
}

int main(void)
{
    RUN(test_crc16_empty);
    RUN(test_crc16_canonical_vector);
    RUN(test_build_frame_for_slave1_addr1_count2);
    RUN(test_build_frame_crc_round_trips);
    RUN(test_parse_well_formed_response);
    RUN(test_parse_truncated_response);
    RUN(test_parse_corrupted_payload_fails_crc);
    RUN(test_parse_exception_response);
    RUN(test_parse_slave_id_mismatch);
    TEST_SUMMARY_AND_EXIT();
}
