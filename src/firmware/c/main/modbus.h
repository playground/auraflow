/**
 * Modbus RTU framing helpers — pure logic, no I/O.
 *
 * Function code 0x03 (read holding registers) is the only one we use.
 *
 * Frame layout (request):
 *   [slaveId][0x03][addrHi][addrLo][countHi][countLo][crcLo][crcHi]
 * Response:
 *   [slaveId][0x03][byteCount][data...][crcLo][crcHi]
 *
 * C port of src/firmware/ts/modbus.ts. See that file for the original
 * spec; behavior verified by both the TS Node tests and the C host
 * tests under ../test/.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define MODBUS_FUNC_READ_HOLDING_REGISTERS  0x03

typedef enum {
    MODBUS_OK = 0,
    MODBUS_ERR_SHORT,        /* response too short for declared byte_count */
    MODBUS_ERR_CRC,          /* trailing CRC didn't match recomputed value */
    MODBUS_ERR_EXCEPTION,    /* device returned a Modbus exception code */
    MODBUS_ERR_UNEXPECTED,   /* slave-id or function-code mismatch */
} modbus_status_t;

/** Modbus CRC-16 (poly 0xA001, init 0xFFFF). Pass `length` as 0 with NULL data is OK. */
uint16_t modbus_crc16(const uint8_t *data, size_t length);

/**
 * Build a "read holding registers" request frame with CRC appended.
 * `out` must point to at least 8 bytes.
 */
void modbus_build_read_holding(uint8_t slave_id,
                               uint16_t start_address,
                               uint16_t register_count,
                               uint8_t out[8]);

/**
 * Parse a complete read-holding-registers response.
 *
 * On MODBUS_OK, *data_out points into `resp` (no copy) and *data_len_out
 * holds the payload byte count (e.g. 4 bytes for two 16-bit registers).
 * On error, the out parameters are left unchanged.
 */
modbus_status_t modbus_parse_read_holding(const uint8_t *resp,
                                          size_t resp_len,
                                          uint8_t expected_slave,
                                          const uint8_t **data_out,
                                          size_t *data_len_out);
