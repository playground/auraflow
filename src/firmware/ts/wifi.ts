/**
 * Wi-Fi connect with auto-reconnect and exponential backoff (capped at 60s).
 * Anti-flap: if we reconnect more than 5 times in a rolling hour, throttle.
 */

import WiFi from 'wifi';

const MAX_BACKOFF_MS  = 60_000;
const FLAP_WINDOW_MS  = 60 * 60 * 1000;
const FLAP_THRESHOLD  = 5;

export interface WiFiOpts {
  ssid:     string;
  password: string;
  onUp?:   () => void;
  onDown?: () => void;
}

export class WiFiManager {
  private wifi?:        WiFi;
  private backoff =     1000;
  private connectTimes: number[] = [];

  constructor(private opts: WiFiOpts) {}

  start(): void {
    this.connect();
  }

  private connect(): void {
    if (this.shouldThrottle()) {
      if (typeof trace === 'function') trace('wifi: anti-flap throttle, sleeping 5 min\n');
      // Schedule a delayed retry; can't import Timer at top-level cleanly so do it inline.
      // eslint-disable-next-line @typescript-eslint/no-require-imports
      const Timer = require('timer').default;
      Timer.set(() => this.connect(), 5 * 60 * 1000);
      return;
    }

    this.wifi = new WiFi(
      { ssid: this.opts.ssid, password: this.opts.password },
      (msg: string) => this.onEvent(msg),
    );
  }

  private onEvent(msg: string): void {
    if (typeof trace === 'function') trace(`wifi: ${msg}\n`);
    if (msg === 'gotIP') {
      this.connectTimes.push(Date.now());
      this.backoff = 1000;
      this.opts.onUp?.();
    } else if (msg === 'disconnected') {
      this.opts.onDown?.();
      this.scheduleReconnect();
    }
  }

  private scheduleReconnect(): void {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const Timer = require('timer').default;
    const delay = this.backoff;
    this.backoff = Math.min(this.backoff * 2, MAX_BACKOFF_MS);
    Timer.set(() => this.connect(), delay);
  }

  private shouldThrottle(): boolean {
    const cutoff = Date.now() - FLAP_WINDOW_MS;
    this.connectTimes = this.connectTimes.filter((t) => t > cutoff);
    return this.connectTimes.length >= FLAP_THRESHOLD;
  }
}
