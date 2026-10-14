/* Physics body and contact layouts, shared by physics.c and collide.c. */
#ifndef REROLL_BODY_H
#define REROLL_BODY_H

#include "../runtime.h"

#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define I32(p, off) (*(int32_t *)((p) + (off)))
#define VEC(p, off) ((int32_t *)((p) + (off)))

/* Body fields (see physics.c for the overview). */
#define FLAGS 0x000            /* low 9 bits: type; 0x2000, 0x8000 */
#define POSITION 0x034         /* 3 x 16.16 */
#define MOMENTUM 0x040         /* velocity * mass */
#define ANGULAR_MOMENTUM 0x04c
#define ORIENTATION 0x058      /* quaternion w, x, y, z (16.16) */
#define VELOCITY 0x068         /* per step */
#define FORCE 0x074            /* summed for the step */
#define TORQUE 0x080
#define SPIN 0x08c             /* quaternion change per step */
#define MASS 0x0a8
#define INVERSE_MASS 0x0ac
#define RESTITUTION 0x0b0      /* bounce: impulse scale is 1 + this */
#define ANGULAR_VELOCITY 0x0b8 /* inverse inertia * angular momentum >> 8 */
#define MATRIX 0x0c4           /* 3x3 int16, 4096 = 1.0 */
#define INVERSE_INERTIA 0x0e4  /* 3x3 int32 16.16 */
#define RIGIDITY 0x10c         /* signed byte; above 1: contacts also turn it */
#define TIME 0x118
#define CALLBACK 0x148
#define CONTACT_MODE 0x168

/* Contact block (event + 0x0c, or one built on the stack): */
#define CONTACT_POINT 0x20  /* relative to the body once in its frame */
#define CONTACT_NORMAL 0x2c
#define CONTACT_DEPTH 0x3c

/* Event record (0x68 bytes): */
#define EVENT_KIND 0x00     /* 1: body against the track; else two bodies */
#define EVENT_A 4
#define EVENT_B 8
#define EVENT_CONTACT 0x0c
#define EVENT_TYPE 0x54     /* low byte */
#define EVENT_SURFACE 0x5a  /* int16, for type 0x40 */
#define EVENT_VELOCITY 0x5c /* relative velocity at the contact */
/* the bodies are 32-bit pointers in the event record */
#define EVENT_BODY(event, which) ((uint8_t *)(uintptr_t)*(uint32_t *)((event) + (which)))
#define EVENT_BODY_SET(event, which, body) (*(uint32_t *)((event) + (which)) = (uint32_t)(uintptr_t)(body))

typedef void(GAMEAPI *grid_fn)(uint8_t *body);
#define grid_unlink ((grid_fn)0x4611d0)
#define grid_link ((grid_fn)0x460fb0)

/* imul then shr eax,16 / shl edx,16: bits 16..47 of the product */
static inline int32_t mul16(int32_t a, int32_t b)
{
    return (int32_t)(uint32_t)((uint64_t)((int64_t)a * b) >> 16);
}

/* cdq; xor; sub: |v|, with |INT_MIN| = INT_MIN */
static inline int32_t abs32(int32_t v)
{
    return (int32_t)(((uint32_t)v ^ (uint32_t)(v >> 31)) - (uint32_t)(v >> 31));
}

#endif
