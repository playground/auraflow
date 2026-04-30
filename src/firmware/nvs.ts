/**
 * NVS-backed device configuration. All keys live under the 'auraflow' domain.
 *
 * Set by provisioning (Phase 5 web flasher) or via the Moddable serial REPL
 * for development. Read at boot by main.ts.
 */

import Preference from 'preference';
import type { FloatWordOrder } from './tuf2000m';

const DOMAIN = 'auraflow';

export interface AuraflowConfig {
  wifiSsid:        string;
  wifiPassword:    string;
  homehubUrl:      string;
  internalApiKey:  string;
  sensorId:        string;
  wordOrder:       FloatWordOrder;
}

function read(key: string): string {
  const v = Preference.get(DOMAIN, key);
  return typeof v === 'string' ? v : '';
}

function write(key: string, value: string): void {
  Preference.set(DOMAIN, key, value);
}

export function loadConfig(): AuraflowConfig {
  const wordOrderRaw = read('wordOrder');
  const wordOrder: FloatWordOrder =
    wordOrderRaw === 'high-word-first' ? 'high-word-first' : 'low-word-first';

  return {
    wifiSsid:       read('wifiSsid'),
    wifiPassword:   read('wifiPassword'),
    homehubUrl:     read('homehubUrl'),
    internalApiKey: read('internalApiKey'),
    sensorId:       read('sensorId'),
    wordOrder,
  };
}

/** Bulk-set everything provisioning provides. */
export function saveConfig(cfg: Partial<AuraflowConfig>): void {
  if (cfg.wifiSsid       !== undefined) write('wifiSsid',       cfg.wifiSsid);
  if (cfg.wifiPassword   !== undefined) write('wifiPassword',   cfg.wifiPassword);
  if (cfg.homehubUrl     !== undefined) write('homehubUrl',     cfg.homehubUrl);
  if (cfg.internalApiKey !== undefined) write('internalApiKey', cfg.internalApiKey);
  if (cfg.sensorId       !== undefined) write('sensorId',       cfg.sensorId);
  if (cfg.wordOrder      !== undefined) write('wordOrder',      cfg.wordOrder);
}

export function isProvisioned(cfg: AuraflowConfig): boolean {
  return !!(cfg.wifiSsid && cfg.homehubUrl && cfg.internalApiKey && cfg.sensorId);
}
