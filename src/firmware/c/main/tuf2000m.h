/**
 * TUF-2000M ultrasonic flow meter — pure Modbus encode/decode helpers.
 *
 * The meter returns Float32 values across two consecutive holding registers.
 * Bytes WITHIN each register are always big-endian (Modbus standard). The
 * order of the two registers depends on the meter's M88 ("data byte order")
 * setting:
 *
 *   M88 = ABCD (TUF2000M_HIGH_WORD_FIRST):
 *       register at the lower address holds the HIGH half of the float
 *       (the "natural" big-endian layout).
 *
 *   M88 = CDAB (TUF2000M_LOW_WORD_FIRST):
 *       register at the lower address holds the LOW half of the float
 *       (the two halves are word-swapped). AuraFlow's default.
 *
 * **Verify on your unit by reading a known flow rate; if the value comes
 * back nonsensical (NaN, denormal, off by 10⁹), flip the word order
 * in NVS.**
 *
 * C port of src/firmware/ts/tuf2000m.ts.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "modbus.h"

#define TUF2000M_SLAVE_ID  1

typedef struct {
    uint16_t address;
    uint16_t count;
} tuf2000m_register_t;

extern const tuf2000m_register_t TUF2000M_REG_FLOW_RATE_M3H;     /* {1,  2} m³/h Float32 */
extern const tuf2000m_register_t TUF2000M_REG_TOTALIZER_M3;      /* {9,  2} m³ Float32 */
extern const tuf2000m_register_t TUF2000M_REG_VELOCITY_MS;       /* {5,  2} m/s Float32 (diagnostic) */
extern const tuf2000m_register_t TUF2000M_REG_SIGNAL_QUALITY;    /* {96, 1} 0..99 u16 */

typedef enum {
    TUF2000M_LOW_WORD_FIRST  = 0,    /* CDAB (default) */
    TUF2000M_HIGH_WORD_FIRST = 1,    /* ABCD */
} tuf2000m_word_order_t;

#define TUF2000M_DEFAULT_WORD_ORDER  TUF2000M_LOW_WORD_FIRST

/**
 * Reconstruct a Float32 from two 16-bit Modbus registers.
 *
 * @param r0     Register at the LOWER Modbus address (first in response).
 * @param r1     Register at the HIGHER address (second in response).
 * @param order  See file-level docs.
 */
float tuf2000m_parse_float32(uint16_t r0, uint16_t r1, tuf2000m_word_order_t order);

/**
 * Decode a Float32 starting at register index 0 of a Modbus response payload.
 * @return false if data_len < 4.
 */
bool tuf2000m_decode_float(const uint8_t *data, size_t data_len,
                           tuf2000m_word_order_t order, float *out);

/**
 * Decode a single u16 at register index 0 of a Modbus response payload.
 * @return false if data_len < 2.
 */
bool tuf2000m_decode_u16(const uint8_t *data, size_t data_len, uint16_t *out);

/** Build a Modbus read-holding-registers request frame for a named register. */
void tuf2000m_build_request(tuf2000m_register_t reg, uint8_t out[8]);

/** Parse a complete TUF-2000M response and extract a Float32. */
modbus_status_t tuf2000m_parse_float_response(const uint8_t *resp, size_t resp_len,
                                              tuf2000m_word_order_t order,
                                              float *out);

/** Parse a complete TUF-2000M response and extract a u16. */
modbus_status_t tuf2000m_parse_u16_response(const uint8_t *resp, size_t resp_len,
                                            uint16_t *out);
