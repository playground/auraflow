#include <string.h>

#include "tuf2000m.h"

const tuf2000m_register_t TUF2000M_REG_FLOW_RATE_M3H  = { .address = 1,  .count = 2 };
const tuf2000m_register_t TUF2000M_REG_TOTALIZER_M3   = { .address = 9,  .count = 2 };
const tuf2000m_register_t TUF2000M_REG_VELOCITY_MS    = { .address = 5,  .count = 2 };
const tuf2000m_register_t TUF2000M_REG_SIGNAL_QUALITY = { .address = 96, .count = 1 };

float tuf2000m_parse_float32(uint16_t r0, uint16_t r1, tuf2000m_word_order_t order)
{
    /* In CDAB (LOW_WORD_FIRST), the register at the lower address holds the
     * float's LOW half — so to reconstruct the IEEE-754 layout (high-half
     * bytes first) we feed r1 in as `high`. In ABCD it's the opposite. */
    uint16_t high = (order == TUF2000M_HIGH_WORD_FIRST) ? r0 : r1;
    uint16_t low  = (order == TUF2000M_HIGH_WORD_FIRST) ? r1 : r0;

    /* Build the IEEE-754 bit pattern as a uint32 in the natural
     * "high bytes first" order, then memcpy to float. memcpy avoids
     * strict-aliasing UB; modern compilers fold it to a single load. */
    uint32_t bits = ((uint32_t)((high >> 8) & 0xFFu) << 24)
                  | ((uint32_t)( high       & 0xFFu) << 16)
                  | ((uint32_t)((low  >> 8) & 0xFFu) <<  8)
                  | ((uint32_t)( low        & 0xFFu));

    float result;
    memcpy(&result, &bits, sizeof(float));
    return result;
}

bool tuf2000m_decode_float(const uint8_t *data, size_t data_len,
                           tuf2000m_word_order_t order, float *out)
{
    if (data_len < 4 || out == NULL) return false;
    uint16_t r0 = (uint16_t)((uint16_t)data[0] << 8) | (uint16_t)data[1];
    uint16_t r1 = (uint16_t)((uint16_t)data[2] << 8) | (uint16_t)data[3];
    *out = tuf2000m_parse_float32(r0, r1, order);
    return true;
}

bool tuf2000m_decode_u16(const uint8_t *data, size_t data_len, uint16_t *out)
{
    if (data_len < 2 || out == NULL) return false;
    *out = (uint16_t)((uint16_t)data[0] << 8) | (uint16_t)data[1];
    return true;
}

void tuf2000m_build_request(tuf2000m_register_t reg, uint8_t out[8])
{
    modbus_build_read_holding(TUF2000M_SLAVE_ID, reg.address, reg.count, out);
}

modbus_status_t tuf2000m_parse_float_response(const uint8_t *resp, size_t resp_len,
                                              tuf2000m_word_order_t order,
                                              float *out)
{
    const uint8_t *data;
    size_t         data_len;
    modbus_status_t s = modbus_parse_read_holding(resp, resp_len, TUF2000M_SLAVE_ID,
                                                  &data, &data_len);
    if (s != MODBUS_OK) return s;
    if (!tuf2000m_decode_float(data, data_len, order, out)) {
        return MODBUS_ERR_SHORT;
    }
    return MODBUS_OK;
}

modbus_status_t tuf2000m_parse_u16_response(const uint8_t *resp, size_t resp_len,
                                            uint16_t *out)
{
    const uint8_t *data;
    size_t         data_len;
    modbus_status_t s = modbus_parse_read_holding(resp, resp_len, TUF2000M_SLAVE_ID,
                                                  &data, &data_len);
    if (s != MODBUS_OK) return s;
    if (!tuf2000m_decode_u16(data, data_len, out)) {
        return MODBUS_ERR_SHORT;
    }
    return MODBUS_OK;
}
