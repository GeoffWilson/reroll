/* Collision event queue (events.c). Addresses are the originals. */
#ifndef REROLL_EVENTS_H
#define REROLL_EVENTS_H

#include "../runtime.h"

/* The queue at 0x6b9860. */
struct event_queue {
    uint32_t count;    /* keys in the heap */
    int32_t capacity;  /* event records */
    uint8_t *events;   /* records, EVENT_SIZE bytes each */
    uint32_t *heap;    /* 1-based keys: time in bits 0..19, record in 20..31 */
    uint32_t *free;    /* free record indices, << 20 */
    int32_t free_top;  /* top of `free` */
};
#define EVENT_SIZE 0x68
#define g_event_queue ((struct event_queue *)0x6b9860)

uint32_t GAMEAPI event_queue_clear(struct event_queue *q);                  /* 0x434ee0 */
uint32_t GAMEAPI event_heap_insert(struct event_queue *q, uint32_t key);    /* 0x434f20 */
uint32_t GAMEAPI event_heap_sift_down(struct event_queue *q, uint32_t i);   /* 0x434f70 */
uint32_t GAMEAPI event_free_pop(struct event_queue *q);                     /* 0x435000 */
uint32_t *GAMEAPI event_free_push(struct event_queue *q, uint32_t record);  /* 0x435020 */
uint32_t GAMEAPI event_queue_push(struct event_queue *q, uint32_t time);    /* 0x435040 */
uint32_t GAMEAPI event_queue_pop(struct event_queue *q);                    /* 0x435070 */
uint32_t GAMEAPI event_queue_remove(struct event_queue *q, uint32_t position); /* 0x4350a0 */

#endif
