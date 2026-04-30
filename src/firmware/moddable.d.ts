/**
 * Ambient declarations for Moddable SDK runtime APIs we use.
 *
 * These let TypeScript compile firmware code that imports Moddable
 * modules (which aren't on npm) without complaining. The actual runtime
 * behavior comes from Moddable when the firmware is built with mcconfig.
 *
 * Pure modules (modbus.ts, tuf2000m.ts, ring-buffer.ts) avoid these
 * imports so they can also run under Node for unit testing.
 */

declare module 'timer' {
  const Timer: {
    repeat(callback: () => void, intervalMs: number): unknown;
    set(callback: () => void, delayMs: number): unknown;
    clear(handle: unknown): void;
  };
  export default Timer;
}

declare module 'embedded:io/serial' {
  export interface SerialOptions {
    receive:    number;
    transmit:   number;
    baud:       number;
    port?:      number;
    onReadable?: (this: Serial, count: number) => void;
  }
  export default class Serial {
    constructor(opts: SerialOptions);
    read():  ArrayBuffer;
    write(buf: ArrayBuffer | Uint8Array | number[]): void;
    close(): void;
  }
}

declare module 'wifi' {
  export interface WiFiOptions {
    ssid:       string;
    password?:  string;
  }
  export default class WiFi {
    constructor(opts: WiFiOptions, callback: (msg: string, code?: number) => void);
    static gotIP(): unknown;
    static disconnected(): unknown;
    static connected(): unknown;
    close(): void;
  }
}

declare module 'preference' {
  const Preference: {
    get(domain: string, key: string): unknown;
    set(domain: string, key: string, value: string | number | boolean): void;
    delete(domain: string, key: string): void;
    keys(domain: string): string[];
  };
  export default Preference;
}

declare module 'sntp' {
  export default class SNTP {
    constructor(opts: { host: string }, callback: (msg: string, value?: number) => void);
  }
}

declare const trace: (msg: string) => void;

/** Moddable's fetch (subset). Available in newer SDK builds. */
declare function fetch(input: string, init?: {
  method?:   string;
  headers?:  Record<string, string>;
  body?:     string;
}): Promise<{
  ok:        boolean;
  status:    number;
  statusText: string;
  text():    Promise<string>;
  json():    Promise<unknown>;
}>;
