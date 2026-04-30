/**
 * Modbus RTU framing helpers — pure logic, no I/O.
 *
 * Function code 0x03 (read holding registers) is the only one we use.
 * Frame layout:
 *   [slaveId][0x03][addrHi][addrLo][countHi][countLo][crcLo][crcHi]
 * Response layout:
 *   [slaveId][0x03][byteCount][data...][crcLo][crcHi]
 */

export const FUNC_READ_HOLDING_REGISTERS = 0x03;

/** Modbus CRC-16 (polynomial 0xA001, init 0xFFFF). Returns a 16-bit value. */
export function crc16(bytes: Uint8Array, length = bytes.length): number {
  let crc = 0xFFFF;
  for (let i = 0; i < length; i++) {
    crc ^= bytes[i];
    for (let bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) {
        crc = (crc >>> 1) ^ 0xA001;
      } else {
        crc >>>= 1;
      }
    }
  }
  return crc & 0xFFFF;
}

/** Build a "read holding registers" query frame with CRC appended. */
export function buildReadHoldingRegisters(
  slaveId:        number,
  startAddress:   number,
  registerCount:  number,
): Uint8Array {
  const frame = new Uint8Array(8);
  frame[0] = slaveId & 0xFF;
  frame[1] = FUNC_READ_HOLDING_REGISTERS;
  frame[2] = (startAddress  >> 8) & 0xFF;
  frame[3] =  startAddress        & 0xFF;
  frame[4] = (registerCount >> 8) & 0xFF;
  frame[5] =  registerCount       & 0xFF;
  const crc = crc16(frame, 6);
  frame[6] =  crc        & 0xFF;   // CRC low byte first
  frame[7] = (crc  >> 8) & 0xFF;
  return frame;
}

export interface ParsedResponse {
  slaveId:    number;
  functionId: number;
  /** Raw register payload (2 bytes per register, big-endian within each). */
  data:       Uint8Array;
}

export class ModbusError extends Error {
  constructor(message: string, public readonly code: 'SHORT' | 'CRC' | 'EXCEPTION' | 'UNEXPECTED') {
    super(message);
  }
}

/**
 * Parse a response frame. Throws ModbusError on framing/CRC errors.
 * Caller is responsible for further interpreting `data`.
 */
export function parseReadHoldingRegistersResponse(
  resp:           Uint8Array,
  expectedSlaveId: number,
): ParsedResponse {
  if (resp.length < 5) {
    throw new ModbusError(`Response too short (${resp.length} bytes)`, 'SHORT');
  }

  const slaveId    = resp[0];
  const functionId = resp[1];

  // Exception response: function code has high bit set; one byte exception code follows.
  if (functionId & 0x80) {
    throw new ModbusError(`Modbus exception code ${resp[2]}`, 'EXCEPTION');
  }

  if (functionId !== FUNC_READ_HOLDING_REGISTERS) {
    throw new ModbusError(`Unexpected function code 0x${functionId.toString(16)}`, 'UNEXPECTED');
  }

  if (slaveId !== expectedSlaveId) {
    throw new ModbusError(`Slave id mismatch (got ${slaveId}, expected ${expectedSlaveId})`, 'UNEXPECTED');
  }

  const byteCount = resp[2];
  const expectedTotal = 3 + byteCount + 2;
  if (resp.length < expectedTotal) {
    throw new ModbusError(`Response truncated (got ${resp.length}, expected ${expectedTotal})`, 'SHORT');
  }

  // Validate CRC.
  const crcLo = resp[expectedTotal - 2];
  const crcHi = resp[expectedTotal - 1];
  const reported = (crcHi << 8) | crcLo;
  const computed = crc16(resp, expectedTotal - 2);
  if (reported !== computed) {
    throw new ModbusError(`CRC mismatch (reported 0x${reported.toString(16)}, computed 0x${computed.toString(16)})`, 'CRC');
  }

  return {
    slaveId,
    functionId,
    data: resp.slice(3, 3 + byteCount),
  };
}
