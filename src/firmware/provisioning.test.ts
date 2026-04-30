import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { parseProvisionLine } from './provisioning';

const VALID_INPUT = 'PROVISION:' + JSON.stringify({
  wifiSsid:       'HouseNet',
  wifiPassword:   'secret123',
  homehubUrl:     'http://192.168.1.10:3000',
  internalApiKey: 'abcdef',
  sensorId:       'auraflow-mainline-01',
});

describe('parseProvisionLine — happy path', () => {
  it('parses a valid line with all required fields', () => {
    const r = parseProvisionLine(VALID_INPUT);
    assert.equal(r.ok, true);
    if (r.ok) {
      assert.equal(r.config.wifiSsid,    'HouseNet');
      assert.equal(r.config.sensorId,    'auraflow-mainline-01');
      assert.equal(r.config.wordOrder,   'low-word-first');   // default
    }
  });

  it('honors explicit wordOrder = high-word-first', () => {
    const input = 'PROVISION:' + JSON.stringify({
      wifiSsid: 'x', wifiPassword: 'x', homehubUrl: 'x',
      internalApiKey: 'x', sensorId: 'x', wordOrder: 'high-word-first',
    });
    const r = parseProvisionLine(input);
    assert.equal(r.ok, true);
    if (r.ok) assert.equal(r.config.wordOrder, 'high-word-first');
  });

  it('tolerates trailing CR/LF and whitespace', () => {
    const r = parseProvisionLine(`  ${VALID_INPUT}\r\n`);
    assert.equal(r.ok, true);
  });
});

describe('parseProvisionLine — rejection cases', () => {
  it('rejects line without PROVISION: prefix', () => {
    const r = parseProvisionLine('{"wifiSsid":"x"}');
    assert.equal(r.ok, false);
    if (!r.ok) assert.match(r.error, /prefix/);
  });

  it('rejects malformed JSON', () => {
    const r = parseProvisionLine('PROVISION:{not-json}');
    assert.equal(r.ok, false);
    if (!r.ok) assert.match(r.error, /JSON/);
  });

  it('rejects non-object JSON (array)', () => {
    const r = parseProvisionLine('PROVISION:[1,2,3]');
    assert.equal(r.ok, false);
    if (!r.ok) assert.match(r.error, /object/);
  });

  it('rejects missing required field', () => {
    const r = parseProvisionLine('PROVISION:' + JSON.stringify({
      wifiSsid: 'x', wifiPassword: 'x', homehubUrl: 'x', internalApiKey: 'x',
      // sensorId missing
    }));
    assert.equal(r.ok, false);
    if (!r.ok) assert.match(r.error, /sensorId/);
  });

  it('rejects empty-string required field', () => {
    const r = parseProvisionLine('PROVISION:' + JSON.stringify({
      wifiSsid: '', wifiPassword: 'x', homehubUrl: 'x',
      internalApiKey: 'x', sensorId: 'x',
    }));
    assert.equal(r.ok, false);
    if (!r.ok) assert.match(r.error, /wifiSsid/);
  });

  it('rejects unrecognized wordOrder', () => {
    const r = parseProvisionLine('PROVISION:' + JSON.stringify({
      wifiSsid: 'x', wifiPassword: 'x', homehubUrl: 'x',
      internalApiKey: 'x', sensorId: 'x', wordOrder: 'middle-word-first',
    }));
    assert.equal(r.ok, false);
    if (!r.ok) assert.match(r.error, /wordOrder/);
  });
});
