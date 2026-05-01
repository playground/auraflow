/**
 * HTTP uplink to HomeHub.
 *
 * Posts each reading to `${HOMEHUB_URL}/internal/sensors/flow/readings` with
 * `X-Internal-Key`. On any failure, buffers the reading in a ring (~5 min
 * capacity) and retries on the next push, oldest first. Each buffered
 * reading carries its enqueue time so the server can reconstruct the
 * actual recordedAt via `tOffsetMs`.
 *
 * Server response carries the desired poll cadence — the orchestrator
 * applies it on the next loop iteration. No HTTP server needed on-device.
 */

import { RingBuffer } from './ring-buffer';

export interface UplinkConfig {
  homehubUrl:      string;          // e.g. "http://192.168.1.10:3000"
  internalApiKey:  string;
  sensorId:        string;
  bufferCapacity?: number;
  /** Absolute request timeout. */
  timeoutMs?:      number;
}

export interface ReadingPayload {
  rateM3h:           number;
  totalM3?:          number | null;
  signalQuality?:    number | null;
  rssi?:             number | null;
  uptimeSec?:        number | null;
  firmwareVersion?:  string | null;
  mac?:              string | null;
  bootReason?:       'power' | 'software' | 'watchdog' | 'unknown' | null;
}

export interface PollConfig {
  pollIntervalMs:        number;
  flowingPollIntervalMs: number;
  idlePollIntervalMs:    number;
  configVersion:         number;
}

const DEFAULT_POLL: PollConfig = {
  pollIntervalMs:        5000,
  flowingPollIntervalMs: 5000,
  idlePollIntervalMs:    30000,
  configVersion:         1,
};

export class Uplink {
  private buffer: RingBuffer<ReadingPayload>;
  private url:    string;

  constructor(private cfg: UplinkConfig) {
    this.buffer = new RingBuffer<ReadingPayload>(cfg.bufferCapacity ?? 60);
    this.url    = `${cfg.homehubUrl.replace(/\/$/, '')}/internal/sensors/flow/readings`;
  }

  /**
   * Enqueue + attempt to flush. Returns the latest poll config from the
   * server, or DEFAULT_POLL if the request didn't succeed.
   */
  async push(payload: ReadingPayload, now = Date.now()): Promise<PollConfig> {
    this.buffer.push(payload, now);
    return this.flush(now);
  }

  /** Drain buffer to server, oldest first. Stops on first failure (re-buffers). */
  async flush(now = Date.now()): Promise<PollConfig> {
    let lastConfig: PollConfig = DEFAULT_POLL;

    while (!this.buffer.isEmpty()) {
      const item = this.buffer.peek()!;
      const tOffsetMs = Math.max(0, now - item.enqueuedAt);

      try {
        lastConfig = await this.postOnce({
          ...item.value,
          // Append the tOffsetMs so the server can reconstruct recordedAt.
          // (Cast: tOffsetMs is a wire-only field, not part of ReadingPayload.)
        }, tOffsetMs);
        // Success — pop from the buffer and continue with next item.
        this.buffer.shift();
      } catch (err) {
        // Network failure — leave the item at the head of the buffer for next push.
        // Don't throw; the orchestrator should keep polling.
        if (typeof trace === 'function') {
          trace(`uplink: post failed, buffered (${this.buffer.length} pending): ${(err as Error).message}\n`);
        }
        return lastConfig;
      }
    }

    return lastConfig;
  }

  get pendingCount(): number {
    return this.buffer.length;
  }

  private async postOnce(payload: ReadingPayload, tOffsetMs: number): Promise<PollConfig> {
    const body = JSON.stringify({
      sensorId:         this.cfg.sensorId,
      rateM3h:          payload.rateM3h,
      totalM3:          payload.totalM3 ?? undefined,
      signalQuality:    payload.signalQuality ?? undefined,
      rssi:             payload.rssi ?? undefined,
      uptimeSec:        payload.uptimeSec ?? undefined,
      firmwareVersion:  payload.firmwareVersion ?? undefined,
      mac:              payload.mac ?? undefined,
      bootReason:       payload.bootReason ?? undefined,
      tOffsetMs,
    });

    const res = await fetch(this.url, {
      method: 'POST',
      headers: {
        'Content-Type':   'application/json',
        'X-Internal-Key': this.cfg.internalApiKey,
      },
      body,
    });

    if (!res.ok) {
      throw new Error(`HTTP ${res.status} ${res.statusText}`);
    }

    try {
      const json = await res.json() as Partial<PollConfig>;
      return {
        pollIntervalMs:        json.pollIntervalMs        ?? DEFAULT_POLL.pollIntervalMs,
        flowingPollIntervalMs: json.flowingPollIntervalMs ?? DEFAULT_POLL.flowingPollIntervalMs,
        idlePollIntervalMs:    json.idlePollIntervalMs    ?? DEFAULT_POLL.idlePollIntervalMs,
        configVersion:         json.configVersion         ?? DEFAULT_POLL.configVersion,
      };
    } catch {
      return DEFAULT_POLL;
    }
  }
}
