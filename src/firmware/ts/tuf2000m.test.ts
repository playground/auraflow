import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { crc16 } from './modbus';
import {
  parseFloat32,
  decodeFloatFromResponse,
  parseFloatResponse,
  parseU16Response,
  FlowRequest,
  REGISTERS,
  SLAVE_ID,
} from './tuf2000m';

/** Helper: append a valid Modbus CRC to a payload. */
function withCrc(bytes: number[]): Uint8Array {
  const buf = new Uint8Array(bytes.length + 2);
  buf.set(bytes);
  const crc = crc16(buf, bytes.length);
  buf[bytes.length]     =  crc        & 0xFF;
  buf[bytes.length + 1] = (crc  >> 8) & 0xFF;
  return buf;
}

describe('parseFloat32 — word-order semantics', () => {
  it('high-word-first: r0=0x3F80, r1=0x0000 → 1.0', () => {
    assert.equal(parseFloat32(0x3F80, 0x0000, 'high-word-first'), 1.0);
  });

  it('low-word-first (CDAB): r0=0x0000, r1=0x3F80 → 1.0', () => {
    assert.equal(parseFloat32(0x0000, 0x3F80, 'low-word-first'), 1.0);
  });

  it('high-word-first: r0=0xC2C9, r1=0x0000 → -100.5', () => {
    assert.equal(parseFloat32(0xC2C9, 0x0000, 'high-word-first'), -100.5);
  });

  it('low-word-first: r0=0x0000, r1=0xC2C9 → -100.5', () => {
    assert.equal(parseFloat32(0x0000, 0xC2C9, 'low-word-first'), -100.5);
  });

  it('low-word-first: r0=0x9999, r1=0x3DCC → ≈ 0.1', () => {
    // 0.1 in IEEE-754 single = 0x3DCCCCCD → bytes 3D CC CC CD
    // CDAB layout: response bytes [CC CD 3D CC] → r0=0xCCCD, r1=0x3DCC
    const v = parseFloat32(0xCCCD, 0x3DCC, 'low-word-first');
    assert.ok(Math.abs(v - 0.1) < 1e-7, `expected ≈0.1, got ${v}`);
  });

  it('default word order is low-word-first (CDAB)', () => {
    assert.equal(parseFloat32(0x0000, 0x3F80), 1.0);
  });
});

describe('decodeFloatFromResponse', () => {
  it('reads 1.0 from CDAB response bytes [00 00 3F 80]', () => {
    const payload = new Uint8Array([0x00, 0x00, 0x3F, 0x80]);
    assert.equal(decodeFloatFromResponse(payload, 'low-word-first'), 1.0);
  });

  it('reads 1.0 from ABCD response bytes [3F 80 00 00]', () => {
    const payload = new Uint8Array([0x3F, 0x80, 0x00, 0x00]);
    assert.equal(decodeFloatFromResponse(payload, 'high-word-first'), 1.0);
  });

  it('throws when payload is too short', () => {
    assert.throws(() => decodeFloatFromResponse(new Uint8Array([0x00, 0x00, 0x3F])));
  });
});

describe('parseFloatResponse — full Modbus framing', () => {
  it('extracts a CDAB-encoded 1.0 from a complete response frame', () => {
    // [slaveId=1][func=03][byteCount=4][reg0_hi=00][reg0_lo=00][reg1_hi=3F][reg1_lo=80][CRC...]
    const resp = withCrc([0x01, 0x03, 0x04, 0x00, 0x00, 0x3F, 0x80]);
    assert.equal(parseFloatResponse(resp, 'low-word-first'), 1.0);
  });

  it('respects ABCD when configured', () => {
    const resp = withCrc([0x01, 0x03, 0x04, 0x3F, 0x80, 0x00, 0x00]);
    assert.equal(parseFloatResponse(resp, 'high-word-first'), 1.0);
  });
});

describe('parseU16Response', () => {
  it('extracts a single u16 from a count=1 response', () => {
    // Signal quality of 87
    const resp = withCrc([0x01, 0x03, 0x02, 0x00, 0x57]);
    assert.equal(parseU16Response(resp), 87);
  });
});

describe('FlowRequest builders', () => {
  it('rateM3h builds a frame for slave 1, address 1, count 2', () => {
    const f = FlowRequest.rateM3h();
    assert.equal(f.length, 8);
    assert.deepEqual(Array.from(f.slice(0, 6)), [SLAVE_ID, 0x03, 0x00, 0x01, 0x00, 0x02]);
  });

  it('totalizerM3 builds a frame for the right register', () => {
    const f = FlowRequest.totalizerM3();
    assert.equal(f[2], (REGISTERS.totalizerM3.address >> 8) & 0xFF);
    assert.equal(f[3],  REGISTERS.totalizerM3.address       & 0xFF);
  });

  it('signalQuality requests one register', () => {
    const f = FlowRequest.signalQuality();
    assert.equal(f[5], 0x01);
  });
});
