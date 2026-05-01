#include "modbus.h"

uint16_t modbus_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x0001u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

void modbus_build_read_holding(uint8_t slave_id,
                               uint16_t start_address,
                               uint16_t register_count,
                               uint8_t out[8])
{
    out[0] = slave_id;
    out[1] = MODBUS_FUNC_READ_HOLDING_REGISTERS;
    out[2] = (uint8_t)((start_address  >> 8) & 0xFFu);
    out[3] = (uint8_t)( start_address        & 0xFFu);
    out[4] = (uint8_t)((register_count >> 8) & 0xFFu);
    out[5] = (uint8_t)( register_count       & 0xFFu);
    uint16_t crc = modbus_crc16(out, 6);
    out[6] = (uint8_t)( crc        & 0xFFu);  /* CRC low byte first on the wire */
    out[7] = (uint8_t)((crc  >> 8) & 0xFFu);
}

modbus_status_t modbus_parse_read_holding(const uint8_t *resp,
                                          size_t resp_len,
                                          uint8_t expected_slave,
                                          const uint8_t **data_out,
                                          size_t *data_len_out)
{
    if (resp_len < 5) {
        return MODBUS_ERR_SHORT;
    }

    uint8_t slave    = resp[0];
    uint8_t func     = resp[1];

    /* Exception response: high bit of function code set; one byte exception code follows. */
    if (func & 0x80u) {
        return MODBUS_ERR_EXCEPTION;
    }
    if (func != MODBUS_FUNC_READ_HOLDING_REGISTERS) {
        return MODBUS_ERR_UNEXPECTED;
    }
    if (slave != expected_slave) {
        return MODBUS_ERR_UNEXPECTED;
    }

    uint8_t byte_count    = resp[2];
    size_t  expected_total = (size_t)3 + (size_t)byte_count + (size_t)2;
    if (resp_len < expected_total) {
        return MODBUS_ERR_SHORT;
    }

    /* Validate trailing CRC. */
    uint8_t  crc_lo  = resp[expected_total - 2];
    uint8_t  crc_hi  = resp[expected_total - 1];
    uint16_t reported = (uint16_t)((uint16_t)crc_hi << 8) | (uint16_t)crc_lo;
    uint16_t computed = modbus_crc16(resp, expected_total - 2);
    if (reported != computed) {
        return MODBUS_ERR_CRC;
    }

    *data_out     = &resp[3];
    *data_len_out = byte_count;
    return MODBUS_OK;
}
