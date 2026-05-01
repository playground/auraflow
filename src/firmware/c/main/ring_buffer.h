/**
 * Bounded FIFO ring buffer over caller-provided storage.
 *
 * The TS source wraps each value in a `{ value, enqueuedAt }` envelope to
 * record when it was pushed. In C we let the caller embed that timestamp
 * (or any other metadata) directly in their element struct — the ring
 * buffer just moves opaque bytes around. Used by uplink.c to hold readings
 * while the network is down.
 *
 * No dynamic allocation. Storage is always a caller-supplied array; the
 * library copies elements in/out by `element_size` bytes via memcpy.
 *
 * C port of src/firmware/ts/ring-buffer.ts.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void   *storage;       /* capacity * element_size bytes, caller-owned */
    size_t  element_size;  /* bytes per element */
    size_t  capacity;      /* max elements */
    size_t  head;          /* index of oldest element */
    size_t  count;         /* current populated entries */
} ring_buffer_t;

/**
 * Initialize a ring buffer over caller-provided `storage`. Returns false
 * if any argument is invalid (NULL storage, zero element_size, zero capacity).
 */
bool ring_buffer_init(ring_buffer_t *rb,
                      void *storage,
                      size_t element_size,
                      size_t capacity);

/**
 * Push an element. Copies `element_size` bytes from `*element` into the
 * tail. If full, drops the oldest entry (head advances).
 */
void ring_buffer_push(ring_buffer_t *rb, const void *element);

/**
 * Pop the oldest element into `*element_out`. Returns false on empty.
 * `element_out` may be NULL to discard the value.
 */
bool ring_buffer_shift(ring_buffer_t *rb, void *element_out);

/**
 * Read the oldest element into `*element_out` without removing.
 * Returns false on empty.
 */
bool ring_buffer_peek(const ring_buffer_t *rb, void *element_out);

/** Number of populated elements. */
size_t ring_buffer_length(const ring_buffer_t *rb);

bool ring_buffer_is_empty(const ring_buffer_t *rb);
bool ring_buffer_is_full(const ring_buffer_t *rb);

/** Discard all elements (no callbacks fired). */
void ring_buffer_clear(ring_buffer_t *rb);

/**
 * Drain up to `max_elements` into `out` in FIFO order. Returns the count
 * actually drained. `out` must have room for at least
 * `max_elements * element_size` bytes.
 */
size_t ring_buffer_drain(ring_buffer_t *rb, void *out, size_t max_elements);
