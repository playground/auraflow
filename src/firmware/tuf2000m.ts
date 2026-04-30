/**
 * TUF-2000M ultrasonic flow meter — pure Modbus encode/decode helpers.
 *
 * The meter returns Float32 values across two consecutive holding registers.
 * Bytes WITHIN each register are always big-endian (Modbus standard). The
 * order of the two registers depends on the meter's M88 ("data byte order")
 * setting:
 *
 *   M88 = ABCD ("high-word-first"): register at the lower address holds
 *     the HIGH half of the float (this is the "natural" big-endian layout).
 *   M88 = CDAB ("low-word-first"): register at the lower address holds
 *     the LOW half of the float (the two halves are word-swapped).
 *
 * AuraFlow defaults to CDAB. **Verify on your unit by reading a known flow
 * rate; if the value comes back nonsensical (NaN, denormal, off by 10⁹),
 * flip the word order in NVS.**
 *
 * Register addresses below are typical for TUF-2000M / TDS-100 family
 * firmware at time of writing — confirm against the datasheet that ships
 * with your unit; some firmware revisions shift them.
 */

import {
  buildReadHoldingRegisters,
  parseReadHoldingRegistersResponse,
} from './modbus';

export const SLAVE_ID = 1;

export const REGISTERS = {
  flowRateM3h:    { address: 1,  count: 2 },   // Float32 m³/h
  totalizerM3:    { address: 9,  count: 2 },   // Float32 m³
  velocityMs:     { address: 5,  count: 2 },   // Float32 m/s (diagnostic)
  signalQuality:  { address: 96, count: 1 },   // 0..99 (some firmware uses 92 — verify)
} as const;

export type FloatWordOrder = 'high-word-first' | 'low-word-first';

/** AuraFlow's default until verified otherwise on the actual unit. */
export const DEFAULT_WORD_ORDER: FloatWordOrder = 'low-word-first';

/**
 * Reconstruct a Float32 from two 16-bit Modbus registers.
 *
 * @param r0 Register at the LOWER Modbus address (first in response).
 * @param r1 Register at the HIGHER address (second in response).
 * @param wordOrder See file-level docs.
 */
export function parseFloat32(
  r0:        number,
  r1:        number,
  wordOrder: FloatWordOrder = DEFAULT_WORD_ORDER,
): number {
  const buf  = new ArrayBuffer(4);
  const view = new DataView(buf);
  const high = wordOrder === 'high-word-first' ? r0 : r1;
  const low  = wordOrder === 'high-word-first' ? r1 : r0;
  view.setUint16(0, high, false);
  view.setUint16(2, low,  false);
  return view.getFloat32(0, false);
}

/** Read two registers from a Modbus response payload as a Float32. */
export function decodeFloatFromResponse(
  data:      Uint8Array,
  wordOrder: FloatWordOrder = DEFAULT_WORD_ORDER,
): number {
  if (data.length < 4) {
    throw new Error(`Float32 needs 4 bytes; got ${data.length}`);
  }
  const r0 = (data[0] << 8) | data[1];
  const r1 = (data[2] << 8) | data[3];
  return parseFloat32(r0, r1, wordOrder);
}

/** Read a single 16-bit register from a Modbus response payload. */
export function decodeU16FromResponse(data: Uint8Array): number {
  if (data.length < 2) {
    throw new Error(`U16 needs 2 bytes; got ${data.length}`);
  }
  return (data[0] << 8) | data[1];
}

/** Build a request frame for a named TUF-2000M register. */
export function buildRequest(reg: { address: number; count: number }): Uint8Array {
  return buildReadHoldingRegisters(SLAVE_ID, reg.address, reg.count);
}

/** Parse a complete TUF-2000M response and extract a Float32. */
export function parseFloatResponse(
  resp:      Uint8Array,
  wordOrder: FloatWordOrder = DEFAULT_WORD_ORDER,
): number {
  const parsed = parseReadHoldingRegistersResponse(resp, SLAVE_ID);
  return decodeFloatFromResponse(parsed.data, wordOrder);
}

/** Parse a complete TUF-2000M response and extract a Uint16 value. */
export function parseU16Response(resp: Uint8Array): number {
  const parsed = parseReadHoldingRegistersResponse(resp, SLAVE_ID);
  return decodeU16FromResponse(parsed.data);
}

/** Convenience wrappers for the common registers. */
export const FlowRequest = {
  rateM3h:       (): Uint8Array => buildRequest(REGISTERS.flowRateM3h),
  totalizerM3:   (): Uint8Array => buildRequest(REGISTERS.totalizerM3),
  signalQuality: (): Uint8Array => buildRequest(REGISTERS.signalQuality),
};
