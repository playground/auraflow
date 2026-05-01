/**
 * First-boot provisioning over UART0.
 *
 * When the firmware boots unprovisioned, it opens UART0 and listens for
 * a single line of the form:
 *
 *   PROVISION:{"wifiSsid":"...","wifiPassword":"...","homehubUrl":"...",
 *              "internalApiKey":"...","sensorId":"...","wordOrder":"low-word-first"}\n
 *
 * On success it writes NVS, traces `OK\n`, and restarts. On a parse or
 * validation error it traces `ERR:<reason>\n` and keeps listening.
 *
 * Periodic `READY` heartbeats let the host (web flasher / serial tool)
 * detect that the firmware is in provisioning mode and which fields it
 * accepts.
 *
 * UART0 is shared with Moddable's trace output. Reads work fine alongside
 * trace writes (full-duplex hardware UART). xsbug debug builds may
 * collide with the binary debug protocol — provisioning over serial is
 * intended for release builds.
 *
 * The parser is exported separately so it can be unit-tested under Node
 * without any Moddable dependency.
 */

import type { AuraflowConfig } from './nvs';
import type { FloatWordOrder } from './tuf2000m';

export type ParseResult =
  | { ok: true;  config: AuraflowConfig }
  | { ok: false; error: string };

const REQUIRED_FIELDS: (keyof AuraflowConfig)[] = [
  'wifiSsid', 'wifiPassword', 'homehubUrl', 'internalApiKey', 'sensorId',
];

/**
 * Pure parser. Accepts a single line of input. Returns the parsed config
 * or a structured error reason. Whitespace is tolerated; the leading
 * `PROVISION:` prefix is required so that other ASCII/binary bytes on
 * the same UART (e.g. xsbug, trace output) don't get confused for
 * provisioning input.
 */
export function parseProvisionLine(line: string): ParseResult {
  const trimmed = line.replace(/[\r\n]+$/g, '').trim();
  const PREFIX  = 'PROVISION:';

  if (!trimmed.startsWith(PREFIX)) {
    return { ok: false, error: 'missing PROVISION: prefix' };
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(trimmed.slice(PREFIX.length));
  } catch {
    return { ok: false, error: 'invalid JSON' };
  }

  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
    return { ok: false, error: 'expected JSON object' };
  }
  const obj = parsed as Record<string, unknown>;

  // Validate required string fields.
  for (const k of REQUIRED_FIELDS) {
    const v = obj[k];
    if (typeof v !== 'string' || v.length === 0) {
      return { ok: false, error: `missing or empty field: ${k}` };
    }
  }

  // Optional wordOrder; default to low-word-first (CDAB).
  let wordOrder: FloatWordOrder = 'low-word-first';
  if (obj.wordOrder !== undefined) {
    if (obj.wordOrder !== 'low-word-first' && obj.wordOrder !== 'high-word-first') {
      return { ok: false, error: 'wordOrder must be low-word-first or high-word-first' };
    }
    wordOrder = obj.wordOrder;
  }

  return {
    ok: true,
    config: {
      wifiSsid:       obj.wifiSsid       as string,
      wifiPassword:   obj.wifiPassword   as string,
      homehubUrl:     obj.homehubUrl     as string,
      internalApiKey: obj.internalApiKey as string,
      sensorId:       obj.sensorId       as string,
      wordOrder,
    },
  };
}

/**
 * Moddable runtime entrypoint — starts the listener loop.
 * Imports of Moddable-specific modules are deferred so this file stays
 * loadable in Node for the unit tests above.
 */
export function startProvisioningListener(opts: {
  onSaved?: (config: AuraflowConfig) => void;
  heartbeatMs?: number;
} = {}): void {
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const { default: Serial } = require('embedded:io/serial');
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const { default: Timer  } = require('timer');
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const { saveConfig } = require('./nvs');

  const heartbeat = opts.heartbeatMs ?? 5000;
  let buffer: number[] = [];

  const serial = new Serial({
    receive:  3,            // GPIO3 = UART0 RX
    transmit: 1,            // GPIO1 = UART0 TX
    baud:     460800,       // matches Moddable's default trace baud
    port:     0,
    onReadable() {
      try {
        const chunk = new Uint8Array(this.read());
        for (let i = 0; i < chunk.length; i++) {
          const b = chunk[i];
          if (b === 0x0A /* \n */) {
            const line = String.fromCharCode(...buffer);
            buffer = [];
            handleLine(line);
          } else if (b !== 0x0D /* \r — ignore */) {
            buffer.push(b);
            if (buffer.length > 4096) buffer = []; // hard cap; runaway protection
          }
        }
      } catch (e) {
        trace(`provisioning: read err ${(e as Error).message}\n`);
      }
    },
  });

  function handleLine(line: string): void {
    if (!line.startsWith('PROVISION:')) return;   // ignore noise
    const result = parseProvisionLine(line);
    if (!result.ok) {
      trace(`ERR:${result.error}\n`);
      return;
    }
    try {
      saveConfig(result.config);
      trace('OK\n');
      opts.onSaved?.(result.config);
    } catch (e) {
      trace(`ERR:save failed: ${(e as Error).message}\n`);
      return;
    }
    // Brief delay so the OK byte makes it out before reset.
    Timer.set(() => {
      try {
        // eslint-disable-next-line @typescript-eslint/no-require-imports
        const esp = require('esp32');
        if (esp?.Restart) esp.Restart();
      } catch {
        trace('provisioning: Restart unavailable; cycle power.\n');
      }
    }, 250);
    serial.close();
  }

  // Heartbeat so the host can confirm provisioning mode.
  trace('READY:auraflow-provision-v1\n');
  Timer.repeat(() => trace('READY:auraflow-provision-v1\n'), heartbeat);
}
