/* The collision event queue (0x434ee0..0x4350f8): a binary min-heap of
 * 32-bit keys, the time of impact in the low 20 bits and the index of a
 * 0x68-byte event record in the top 12. Free record indices are kept on a
 * stack, pre-shifted into the top 12 bits. The heap is 1-based. */
#include "../runtime.h"
#include "events.h"

#define TIME_MASK 0xfffffu

uint32_t GAMEAPI event_queue_clear(struct event_queue *q)
{
    int32_t i;

    q->count = 0;
    for (i = 0; i < q->capacity; i++)
        EVQ_FREE(q)[i] = (uint32_t)i << 20;
    q->free_top = q->capacity - 1;
    return (uint32_t)q->free_top;
}

/* 0x434f20: appends `key` and sifts it up; returns where it ended. */
uint32_t GAMEAPI event_heap_insert(struct event_queue *q, uint32_t key)
{
    uint32_t *heap = EVQ_HEAP(q), i = ++q->count;

    while (i > 1) {
        uint32_t parent = heap[i >> 1];
        if ((parent & TIME_MASK) <= (key & TIME_MASK))
            break;
        heap[i] = parent;
        i >>= 1;
    }
    heap[i] = key;
    return i;
}

/* 0x434f70: moves the key at `i` down past any earlier child. */
uint32_t GAMEAPI event_heap_sift_down(struct event_queue *q, uint32_t i)
{
    uint32_t *heap = EVQ_HEAP(q), n = q->count, key = heap[i];

    for (;;) {
        uint32_t best = i, best_key = key, child = 2 * i;

        if (child <= n && (heap[child] & TIME_MASK) < (best_key & TIME_MASK)) {
            best_key = heap[child];
            best = child;
        }
        child = 2 * i + 1;
        if (child <= n && (heap[child] & TIME_MASK) < (best_key & TIME_MASK)) {
            best_key = heap[child];
            best = child;
        }
        if (best == i)
            return i;
        heap[i] = best_key;
        heap[best] = key;
        i = best;
    }
}

/* 0x435000. The index is the low 16 bits of free_top, sign extended. */
uint32_t GAMEAPI event_free_pop(struct event_queue *q)
{
    int16_t top = (int16_t)q->free_top;

    q->free_top--;
    return EVQ_FREE(q)[top];
}

/* 0x435020; returns the free stack as the original leaves it in eax. */
uint32_t *GAMEAPI event_free_push(struct event_queue *q, uint32_t record)
{
    EVQ_FREE(q)[++q->free_top] = record;
    return EVQ_FREE(q);
}

uint32_t GAMEAPI event_queue_push(struct event_queue *q, uint32_t time)
{
    uint32_t key = (event_free_pop(q) & 0xfff00000u) | time;

    event_heap_insert(q, key);
    return key;
}

uint32_t GAMEAPI event_queue_pop(struct event_queue *q)
{
    uint32_t *heap = EVQ_HEAP(q), last = heap[q->count], first = heap[1];

    heap[1] = last;
    q->count--;
    event_heap_sift_down(q, 1);
    event_free_push(q, first);
    return first;
}

/* 0x4350a0: removes the key at heap position `position` (16 bits). */
uint32_t GAMEAPI event_queue_remove(struct event_queue *q, uint32_t position)
{
    uint32_t *heap = EVQ_HEAP(q), i = position & 0xffff, removed = heap[i];

    heap[i] = heap[q->count];
    q->count--;
    for (; i; i >>= 1)
        event_heap_sift_down(q, i);
    event_free_push(q, removed);
    return removed;
}
