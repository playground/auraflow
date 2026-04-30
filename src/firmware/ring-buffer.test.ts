import { describe, it } from 'node:test';
import assert from 'node:assert/strict';
import { RingBuffer } from './ring-buffer';

describe('RingBuffer', () => {
  it('rejects capacity <= 0', () => {
    assert.throws(() => new RingBuffer(0));
    assert.throws(() => new RingBuffer(-1));
  });

  it('starts empty', () => {
    const rb = new RingBuffer<number>(3);
    assert.equal(rb.length, 0);
    assert.equal(rb.isEmpty(), true);
    assert.equal(rb.shift(), undefined);
  });

  it('preserves FIFO order under capacity', () => {
    const rb = new RingBuffer<string>(3);
    rb.push('a', 1); rb.push('b', 2); rb.push('c', 3);
    assert.equal(rb.length, 3);
    assert.equal(rb.shift()!.value, 'a');
    assert.equal(rb.shift()!.value, 'b');
    assert.equal(rb.shift()!.value, 'c');
    assert.equal(rb.shift(), undefined);
  });

  it('drops the oldest when full', () => {
    const rb = new RingBuffer<string>(2);
    rb.push('a', 1); rb.push('b', 2);
    rb.push('c', 3);   // overflows — drops 'a'
    assert.equal(rb.length, 2);
    assert.equal(rb.shift()!.value, 'b');
    assert.equal(rb.shift()!.value, 'c');
  });

  it('keeps timestamps with each entry', () => {
    const rb = new RingBuffer<number>(3);
    rb.push(10, 1000); rb.push(20, 2000);
    const a = rb.shift()!;
    assert.equal(a.value, 10);
    assert.equal(a.enqueuedAt, 1000);
  });

  it('drain() returns FIFO and empties the buffer', () => {
    const rb = new RingBuffer<number>(4);
    rb.push(1); rb.push(2); rb.push(3);
    const out = rb.drain();
    assert.deepEqual(out.map((i) => i.value), [1, 2, 3]);
    assert.equal(rb.length, 0);
  });

  it('clear() empties without yielding items', () => {
    const rb = new RingBuffer<number>(2);
    rb.push(1); rb.push(2);
    rb.clear();
    assert.equal(rb.length, 0);
    assert.equal(rb.shift(), undefined);
  });

  it('isFull reflects capacity', () => {
    const rb = new RingBuffer<number>(2);
    assert.equal(rb.isFull(), false);
    rb.push(1); rb.push(2);
    assert.equal(rb.isFull(), true);
  });
});
