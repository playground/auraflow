import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import {
  crc16,
  buildReadHoldingRegisters,
  parseReadHoldingRegistersResponse,
  ModbusError,
} from './modbus';

describe('crc16', () => {
  it('returns 0xFFFF for an empty input', () => {
    assert.equal(crc16(new Uint8Array(0)), 0xFFFF);
  });

  it('matches the canonical Modbus example for 01 03 00 00 00 0A', () => {
    // From the Modbus spec / well-known test vector.
    const frame = new Uint8Array([0x01, 0x03, 0x00, 0x00, 0x00, 0x0A]);
    const crc   = crc16(frame);
    // Expected CRC = 0xCDC5 (low 0xC5, high 0xCD).
    assert.equal(crc, 0xCDC5);
  });
});

describe('buildReadHoldingRegisters', () => {
  it('produces the correct frame for slave 1, addr 0x0001, count 2', () => {
    const frame = buildReadHoldingRegisters(0x01, 0x0001, 0x0002);
    assert.equal(frame.length, 8);
    assert.deepEqual(Array.from(frame.slice(0, 6)), [0x01, 0x03, 0x00, 0x01, 0x00, 0x02]);
    // Modbus wire-bytes: low CRC byte first. Vector matches notes.txt.
    assert.equal(frame[6], 0x95);
    assert.equal(frame[7], 0xCB);
  });

  it('CRC round-trips: a valid built frame parses back', () => {
    const frame = buildReadHoldingRegisters(0x05, 0x0009, 0x0002);
    // Reconstruct as if it were a response with the same slave, then add a real payload.
    // Just validate the request-frame CRC is what crc16 computes over the first 6.
    const expected = crc16(frame, 6);
    const reported = (frame[7] << 8) | frame[6];
    assert.equal(reported, expected);
  });
});

describe('parseReadHoldingRegistersResponse', () => {
  function withCrc(payload: number[]): Uint8Array {
    const buf = new Uint8Array(payload.length + 2);
    buf.set(payload);
    const crc = crc16(buf, payload.length);
    buf[payload.length]     =  crc        & 0xFF;
    buf[payload.length + 1] = (crc  >> 8) & 0xFF;
    return buf;
  }

  it('parses a well-formed 4-byte (2-register) response', () => {
    const resp = withCrc([0x01, 0x03, 0x04, 0x12, 0x34, 0x56, 0x78]);
    const out  = parseReadHoldingRegistersResponse(resp, 0x01);
    assert.equal(out.slaveId, 0x01);
    assert.equal(out.functionId, 0x03);
    assert.deepEqual(Array.from(out.data), [0x12, 0x34, 0x56, 0x78]);
  });

  it('throws SHORT on a truncated response', () => {
    const bad = new Uint8Array([0x01, 0x03, 0x04, 0x12, 0x34]);
    assert.throws(() => parseReadHoldingRegistersResponse(bad, 0x01), (err: unknown) => {
      return err instanceof ModbusError && err.code === 'SHORT';
    });
  });

  it('throws CRC on a corrupted frame', () => {
    const resp = withCrc([0x01, 0x03, 0x04, 0x12, 0x34, 0x56, 0x78]);
    resp[3] ^= 0xFF;   // flip a byte in the payload
    assert.throws(() => parseReadHoldingRegistersResponse(resp, 0x01), (err: unknown) => {
      return err instanceof ModbusError && err.code === 'CRC';
    });
  });

  it('throws EXCEPTION when the device returns an exception code', () => {
    // Function code 0x83 = exception for 0x03; exception code 0x02 = illegal data address.
    const resp = withCrc([0x01, 0x83, 0x02]);
    assert.throws(() => parseReadHoldingRegistersResponse(resp, 0x01), (err: unknown) => {
      return err instanceof ModbusError && err.code === 'EXCEPTION';
    });
  });

  it('throws UNEXPECTED on a slave id mismatch', () => {
    const resp = withCrc([0x05, 0x03, 0x02, 0x12, 0x34]);
    assert.throws(() => parseReadHoldingRegistersResponse(resp, 0x01), (err: unknown) => {
      return err instanceof ModbusError && err.code === 'UNEXPECTED';
    });
  });
});
