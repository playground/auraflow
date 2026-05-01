#include <stdint.h>
#include <string.h>

#include "ring_buffer.h"

/* Pointer to the i'th slot of the underlying byte storage. */
static inline void *slot_at(const ring_buffer_t *rb, size_t i)
{
    return (uint8_t *)rb->storage + (i * rb->element_size);
}

bool ring_buffer_init(ring_buffer_t *rb,
                      void *storage,
                      size_t element_size,
                      size_t capacity)
{
    if (rb == NULL || storage == NULL || element_size == 0 || capacity == 0) {
        return false;
    }
    rb->storage      = storage;
    rb->element_size = element_size;
    rb->capacity     = capacity;
    rb->head         = 0;
    rb->count        = 0;
    return true;
}

void ring_buffer_push(ring_buffer_t *rb, const void *element)
{
    /* Tail = (head + count) % capacity. */
    size_t tail = (rb->head + rb->count) % rb->capacity;
    memcpy(slot_at(rb, tail), element, rb->element_size);

    if (rb->count < rb->capacity) {
        rb->count++;
    } else {
        /* Full: overwrite oldest by advancing head. */
        rb->head = (rb->head + 1) % rb->capacity;
    }
}

bool ring_buffer_shift(ring_buffer_t *rb, void *element_out)
{
    if (rb->count == 0) return false;
    if (element_out != NULL) {
        memcpy(element_out, slot_at(rb, rb->head), rb->element_size);
    }
    rb->head = (rb->head + 1) % rb->capacity;
    rb->count--;
    return true;
}

bool ring_buffer_peek(const ring_buffer_t *rb, void *element_out)
{
    if (rb->count == 0) return false;
    memcpy(element_out, slot_at(rb, rb->head), rb->element_size);
    return true;
}

size_t ring_buffer_length(const ring_buffer_t *rb) { return rb->count; }
bool   ring_buffer_is_empty(const ring_buffer_t *rb) { return rb->count == 0; }
bool   ring_buffer_is_full (const ring_buffer_t *rb) { return rb->count == rb->capacity; }

void ring_buffer_clear(ring_buffer_t *rb)
{
    rb->head  = 0;
    rb->count = 0;
}

size_t ring_buffer_drain(ring_buffer_t *rb, void *out, size_t max_elements)
{
    size_t n = (rb->count < max_elements) ? rb->count : max_elements;
    for (size_t i = 0; i < n; i++) {
        uint8_t *dst = (uint8_t *)out + (i * rb->element_size);
        ring_buffer_shift(rb, dst);
    }
    return n;
}
