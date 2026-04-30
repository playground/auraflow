/**
 * AuraFlow firmware orchestrator.
 *
 * Boot sequence:
 *   1. Load NVS config. If unprovisioned → trace and idle (Phase 5 will
 *      replace this with a captive-portal AP).
 *   2. Start Wi-Fi with auto-reconnect.
 *   3. Open UART2 for Modbus RTU to the TUF-2000M.
 *   4. Begin polling loop: read flow → uplink → apply server-pushed cadence.
 */

import Timer from 'timer';
import Serial from 'embedded:io/serial';
import { loadConfig, isProvisioned } from './nvs';
import { WiFiManager } from './wifi';
import { Uplink, type ReadingPayload, type PollConfig } from './uplink';
import {
  FlowRequest,
  parseFloatResponse,
  parseU16Response,
  type FloatWordOrder,
} from './tuf2000m';
import { ModbusError } from './modbus';

const FIRMWARE_VERSION = '0.1.0';
const FLOWING_THRESHOLD_M3H = 0.006;   // matches HomeHub default; cadence switches around this

// ── Boot ──────────────────────────────────────────────────────
const cfg = loadConfig();
if (!isProvisioned(cfg)) {
  trace('auraflow: NVS not provisioned. Run provisioning script over serial.\n');
  // Phase 5: launch AP captive portal here.
} else {
  startMain(cfg);
}

// ──────────────────────────────────────────────────────────────

interface RuntimeState {
  uplink:        Uplink;
  serial?:       Serial;
  wordOrder:     FloatWordOrder;
  pollIntervalMs: number;
  flowingMs:     number;
  idleMs:        number;
  pollHandle?:   unknown;
  rxBuffer:      number[];
  rxResolver?:   (bytes: Uint8Array) => void;
}

function startMain(c: ReturnType<typeof loadConfig>): void {
  const uplink = new Uplink({
    homehubUrl:     c.homehubUrl,
    internalApiKey: c.internalApiKey,
    sensorId:       c.sensorId,
  });

  const state: RuntimeState = {
    uplink,
    wordOrder:      c.wordOrder,
    pollIntervalMs: 30_000,
    flowingMs:      5_000,
    idleMs:         30_000,
    rxBuffer:       [],
  };

  new WiFiManager({
    ssid:     c.wifiSsid,
    password: c.wifiPassword,
    onUp:     () => onWiFiUp(state),
    onDown:   () => onWiFiDown(state),
  }).start();
}

function onWiFiUp(state: RuntimeState): void {
  trace('auraflow: wifi up — opening serial + starting poll loop\n');
  state.serial = new Serial({
    receive:    16,
    transmit:   17,
    baud:       9600,
    port:       2,
    onReadable() {
      try {
        const buf = new Uint8Array(this.read());
        for (let i = 0; i < buf.length; i++) state.rxBuffer.push(buf[i]);
        if (state.rxResolver && state.rxBuffer.length >= 5) {
          const out = new Uint8Array(state.rxBuffer);
          state.rxBuffer = [];
          const r = state.rxResolver;
          state.rxResolver = undefined;
          r(out);
        }
      } catch (e) {
        trace(`serial read err: ${(e as Error).message}\n`);
      }
    },
  });
  schedulePoll(state);
}

function onWiFiDown(state: RuntimeState): void {
  trace('auraflow: wifi down — pausing poll loop (uplink will buffer)\n');
  if (state.pollHandle) Timer.clear(state.pollHandle);
  state.pollHandle = undefined;
}

function schedulePoll(state: RuntimeState): void {
  if (state.pollHandle) Timer.clear(state.pollHandle);
  state.pollHandle = Timer.repeat(() => pollOnce(state).catch((e) =>
    trace(`pollOnce err: ${(e as Error).message}\n`),
  ), state.pollIntervalMs);
}

async function pollOnce(state: RuntimeState): Promise<void> {
  if (!state.serial) return;

  let rateM3h = 0;
  let totalM3: number | null = null;
  let signalQuality: number | null = null;

  try {
    rateM3h = parseFloatResponse(await modbusExchange(state, FlowRequest.rateM3h()), state.wordOrder);
  } catch (err) {
    trace(`modbus rate err: ${err instanceof ModbusError ? err.code : (err as Error).message}\n`);
    return;
  }

  try {
    totalM3 = parseFloatResponse(await modbusExchange(state, FlowRequest.totalizerM3()), state.wordOrder);
  } catch { /* totalizer is optional — keep going */ }

  try {
    signalQuality = parseU16Response(await modbusExchange(state, FlowRequest.signalQuality()));
  } catch { /* signal quality is optional */ }

  const payload: ReadingPayload = {
    rateM3h,
    totalM3,
    signalQuality,
    firmwareVersion: FIRMWARE_VERSION,
    uptimeSec:       Math.floor(Date.now() / 1000),    // approximate; ESP32 has no RTC
  };

  let pollCfg: PollConfig | undefined;
  try {
    pollCfg = await state.uplink.push(payload);
  } catch (err) {
    trace(`uplink err: ${(err as Error).message}\n`);
  }

  if (pollCfg) {
    state.flowingMs = pollCfg.flowingPollIntervalMs;
    state.idleMs    = pollCfg.idlePollIntervalMs;
    const desired   = rateM3h > FLOWING_THRESHOLD_M3H ? state.flowingMs : state.idleMs;
    if (desired !== state.pollIntervalMs) {
      state.pollIntervalMs = desired;
      schedulePoll(state);
    }
  }
}

/** Send a Modbus query and await the response, with a 1.5 s timeout. */
function modbusExchange(state: RuntimeState, query: Uint8Array): Promise<Uint8Array> {
  if (!state.serial) return Promise.reject(new Error('serial not open'));
  return new Promise<Uint8Array>((resolve, reject) => {
    state.rxBuffer  = [];
    state.rxResolver = resolve;
    state.serial!.write(query);

    const timeout = Timer.set(() => {
      if (state.rxResolver === resolve) {
        state.rxResolver = undefined;
        reject(new Error('modbus timeout'));
      }
    }, 1500);

    // Cleanup the timeout once resolution happens.
    const wrap = state.rxResolver!;
    state.rxResolver = (bytes) => {
      Timer.clear(timeout);
      wrap(bytes);
    };
  });
}
