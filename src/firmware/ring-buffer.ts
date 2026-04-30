/**
 * Bounded FIFO ring buffer. Drops the oldest entry when full.
 *
 * Used by the uplink module to hold readings while the network is down.
 * Pure logic — no I/O, fully testable.
 */

export interface BufferedItem<T> {
  value:    T;
  /** ms since epoch — captured by `push()` so flush can compute tOffsetMs. */
  enqueuedAt: number;
}

export class RingBuffer<T> {
  private buf:   (BufferedItem<T> | null)[];
  private head = 0;     // index of oldest item
  private size = 0;     // count of valid items

  constructor(public readonly capacity: number) {
    if (capacity <= 0) throw new Error('capacity must be > 0');
    this.buf = new Array(capacity).fill(null);
  }

  push(value: T, now = Date.now()): void {
    const slot = (this.head + this.size) % this.capacity;
    this.buf[slot] = { value, enqueuedAt: now };
    if (this.size < this.capacity) {
      this.size++;
    } else {
      // Full — overwrite oldest by advancing head.
      this.head = (this.head + 1) % this.capacity;
    }
  }

  /** Pop oldest; returns undefined if empty. */
  shift(): BufferedItem<T> | undefined {
    if (this.size === 0) return undefined;
    const item = this.buf[this.head]!;
    this.buf[this.head] = null;
    this.head = (this.head + 1) % this.capacity;
    this.size--;
    return item;
  }

  /** Peek without removing. */
  peek(): BufferedItem<T> | undefined {
    return this.size === 0 ? undefined : this.buf[this.head] ?? undefined;
  }

  get length(): number {
    return this.size;
  }

  isEmpty(): boolean {
    return this.size === 0;
  }

  isFull(): boolean {
    return this.size === this.capacity;
  }

  clear(): void {
    this.buf.fill(null);
    this.head = 0;
    this.size = 0;
  }

  /** Drain all items in FIFO order. Does not clear timestamps. */
  drain(): BufferedItem<T>[] {
    const out: BufferedItem<T>[] = [];
    while (this.size > 0) {
      const item = this.shift();
      if (item) out.push(item);
    }
    return out;
  }
}
