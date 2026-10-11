/* Rigid-body physics: the world step (0x461410) and the code below it, as
 * far as it has been reimplemented.
 *
 * Every body (cars, wheels, debris, power-ups: pointer list at 0x6b9b80,
 * count in the low 16 bits of 0x6b9b84, also null-terminated) has
 *   +0x000  flags (low 9 bits: body type; 0x2000, 0x8000: see below)
 *   +0x00c  grid cell pointer (low bit is a flag)
 *   +0x034  position, 3 x 16.16
 *   +0x068  velocity, 3 x 16.16 per step
 *   +0x0c4  orientation, 3x3 int16, 4096 = 1.0
 *   +0x10c  collides at all (byte)
 *   +0x118  how far into the current step the body has been moved (0..dt)
 *   +0x148  collision callback (body, other, event) -> keep the contact
 *   +0x168  contact mode (signed byte; negative: no new contacts)
 *   +0x16a  trackside trigger (queued when the body moves)
 * Flag 0x8000 marks bodies that just move in a straight line (no rotation
 * or contacts); flag 0x2000 bodies get their forces from 0x462e80.
 *
 * A step of length dt (0x10000 = one 32 ms step) finds every contact due
 * within the step and queues it by time of impact (events.c). Contacts are
 * then resolved in time order: both bodies are moved up to the impact, the
 * impulse applied, their other queued contacts dropped and new ones searched
 * for along their new paths. Finally every body is moved to the end of the
 * step.
 *
 * Bodies are sorted into "groups" (12 bytes: body list, u16 count, cell); the
 * list of all bodies at 0x6b9b80 is itself laid out as a group, and
 * 0x6b9880 holds up to 0x40 groups of bodies that may touch (count at
 * 0x6b9b8c). 0x6b9850 is a scratch group for one or two bodies. */
#include "../runtime.h"
#include "physics.h"
#include "body.h"
#include "../coverage.h"
#include "events.h"
#include "fixed.h"

typedef void(GAMEAPI *void_fn)(void);
typedef void(GAMEAPI *int_fn)(int32_t);
typedef void(GAMEAPI *body_fn)(uint8_t *body);
typedef int32_t(GAMEAPI *callback_fn)(uint8_t *body, uint8_t *other, uint8_t *event);

#define forces_finish ((void_fn)0x4050f0)

#define g_body_group ((void *)0x6b9b80)
#define g_bodies (*(uint8_t ***)0x6b9b80)
#define g_body_count (*(uint32_t *)0x6b9b84 & 0xffff)
#define g_groups ((uint8_t *)0x6b9880)
#define g_group_count (*(int32_t *)0x6b9b8c)
#define GROUP_SIZE 12
#define g_pair_group ((void *)0x6b9850)
#define g_pair_bodies (*(uint8_t ***)0x6b9850)
#define g_pair_count (*(uint16_t *)0x6b9854)
#define g_pair_cell (*(uint32_t *)0x6b9858)
#define g_moved_triggers ((uint8_t **)0x6b8d00) /* bodies whose trigger fired */
#define g_moved_trigger_count (*(uint32_t *)0x4d11ec)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_race_data (*(uint8_t **)0x4b1960)
#define g_force_scratch_a (*(int32_t *)0x6ccbac)
#define g_force_scratch_b (*(int32_t *)0x6cca54)

#define g_slow_motion (*(int32_t *)0x5d9b20)          /* on? */
#define g_slow_motion_body (*(uint8_t **)0x5d9b24)    /* the body it does not slow */
#define g_slow_motion_scale (*(int32_t *)0x5d9b2c)    /* 16.16 */



/* 0x480490: quaternion to unit length, for components up to 180.0. */
void GAMEAPI quat_normalise_small(int32_t *q)
{
    int32_t x = q[1], y = q[2], z = q[3], w = q[0], length, inverse;

    length = (int32_t)fix_sqrt_table(
        (int32_t)((uint32_t)mul16(w, w) + (uint32_t)mul16(z, z) + (uint32_t)mul16(y, y) + (uint32_t)mul16(x, x)));
    if ((int32_t)((uint32_t)length << 8)) {
        inverse = fix_div16(0x10000, (int32_t)((uint32_t)length << 8));
        q[1] = mul16(x, inverse);
        q[2] = mul16(y, inverse);
        q[3] = mul16(z, inverse);
        q[0] = mul16(w, inverse);
    }
}

/* -(-v >> 16) for negatives: integer part, rounded towards zero */
static int32_t integer_part(int32_t v)
{
    return v < 0 ? (int32_t)(0u - ((0u - (uint32_t)v) >> 16)) : (int32_t)((uint32_t)v >> 16);
}

/* 0x480580: the same for large components, on their integer parts. */
void GAMEAPI quat_normalise_large(int32_t *q)
{
    uint32_t x = (uint32_t)integer_part(q[1]), y = (uint32_t)integer_part(q[2]);
    uint32_t z = (uint32_t)integer_part(q[3]), w = (uint32_t)integer_part(q[0]);
    int32_t length = (int32_t)fix_sqrt_table((int32_t)(w * w + z * z + y * y + x * x)), inverse;

    if (length) {
        if (length > 0x7ff8)
            length = 0x7ff8;
        inverse = fix_div16(0x10000, (int32_t)((uint32_t)length << 16));
        q[1] = mul16(q[1], inverse);
        q[2] = mul16(q[2], inverse);
        q[3] = mul16(q[3], inverse);
        q[0] = mul16(q[0], inverse);
    }
}

/* 0x480430 */
void GAMEAPI quat_normalise(int32_t *q)
{
    if (abs32(q[1]) <= 0xb40000 && abs32(q[2]) <= 0xb40000 && abs32(q[3]) <= 0xb40000 &&
        abs32(q[0]) <= 0xb40000)
        quat_normalise_small(q);
    else
        quat_normalise_large(q);
}

/* 0x480780: unit quaternion (16.16) to a 3x3 int16 rotation matrix (4096 = 1.0). */
void GAMEAPI quat_to_matrix(const int32_t *q, int16_t *m)
{
    uint32_t xx = (uint32_t)mul16(q[1], q[1]), yy = (uint32_t)mul16(q[2], q[2]);
    uint32_t zz = (uint32_t)mul16(q[3], q[3]);
    uint32_t wx = (uint32_t)mul16(q[1], q[0]), wy = (uint32_t)mul16(q[2], q[0]), wz = (uint32_t)mul16(q[3], q[0]);
    uint32_t xy = (uint32_t)mul16(q[1], q[2]) * 2, xz = (uint32_t)mul16(q[1], q[3]) * 2;
    uint32_t yz = (uint32_t)mul16(q[2], q[3]) * 2, t = 0x10000 - zz * 2;

    m[0] = (int16_t)((int32_t)(t - yy * 2) >> 4);
    m[1] = (int16_t)((int32_t)(xy - wz * 2) >> 4);
    m[2] = (int16_t)((int32_t)(wy * 2 + xz) >> 4);
    m[3] = (int16_t)((int32_t)(xy + wz * 2) >> 4);
    m[4] = (int16_t)((int32_t)(t - xx * 2) >> 4);
    m[5] = (int16_t)((int32_t)(yz - wx * 2) >> 4);
    m[6] = (int16_t)((int32_t)(xz - wy * 2) >> 4);
    m[7] = (int16_t)((int32_t)(yz + wx * 2) >> 4);
    m[8] = (int16_t)((int32_t)(0x10000 - yy * 2 - xx * 2) >> 4);
}

/* 0x465a60: moves a body on by dt (slowed down for some body types while
 * the slow-motion power-up is on), turns it by its spin unless its
 * orientation is fixed (+0x108 bit 0), and queues its trigger. */
void GAMEAPI body_integrate(uint8_t *body, int32_t dt)
{
    uint32_t type = U32(body, FLAGS) & 0x1ff;

    if (g_slow_motion && body != g_slow_motion_body && (type == 3 || type == 7 || type == 0x15)) {
        COVER(COVER_SLOW_MOTION);
        dt = mul16(g_slow_motion_scale, dt);
    }
    I32(body, POSITION) += mul16(dt, I32(body, VELOCITY));
    I32(body, POSITION + 4) += mul16(dt, I32(body, VELOCITY + 4));
    I32(body, POSITION + 8) += mul16(dt, I32(body, VELOCITY + 8));
    if (!(body[0x108] & 1)) {
        int32_t *q = (int32_t *)(body + ORIENTATION);
        q[0] += mul16(dt, I32(body, SPIN));
        q[1] += mul16(dt, I32(body, SPIN + 4));
        q[2] += mul16(dt, I32(body, SPIN + 8));
        q[3] += mul16(dt, I32(body, SPIN + 12));
        quat_normalise(q);
        quat_to_matrix(q, (int16_t *)(body + MATRIX));
    }
    if (*(int16_t *)(body + 0x16a))
        g_moved_triggers[g_moved_trigger_count++] = body;
}

/* 0x45c300: adds a body to group `index` of `groups`. */
uint8_t *GAMEAPI group_add(uint8_t *body, uint32_t cell, uint8_t *groups, int32_t index)
{
    uint8_t *group = groups + index * GROUP_SIZE;
    uint16_t *count = (uint16_t *)(group + 4);

    (*(uint8_t ***)group)[*count] = body;
    *(uint8_t **)(body + 0x130) = group;
    (*count)++;
    U32(group, 8) = cell;
    return group;
}

/* 0x45c180: puts every moving, colliding body (flag 0x800, not 0x8000,
 * not type 4) that sits in an occupied grid cell into a group (0x6b9880)
 * of its own, marking it with +0x110 bit 24. The original also works out
 * an octant per body, but resets its octant slots for every body, so the
 * body always lands in a fresh group; only that outcome is reproduced. */
void GAMEAPI body_groups_build(void)
{
    uint32_t n = g_body_count, i;
    int32_t total = 0, k;
    uint8_t **bodies = g_bodies, *group = g_groups;

    for (k = g_group_count; k > 0; k--, group += GROUP_SIZE) {
        *(uint16_t *)(group + 4) = 0;
        U32(group, 8) = 0;
    }
    g_group_count = 0;
    group = g_groups;
    for (i = 0; i < n; i++) {
        uint8_t *b = bodies[i];
        uint32_t flags = U32(b, FLAGS), cell;
        uint32_t j;

        if ((flags & 0x8000) || !(flags & 0x800) || (flags & 0x1ff) == 4 || (U32(b, 0x110) & 0x1000000))
            continue;
        cell = U32(b, 0x0c) & ~1u;
        if (!*(uint16_t *)(uintptr_t)(cell + 8))
            continue;
        for (j = 0; j < g_body_count && g_bodies[j] != b; j++)
            ;
        if (j == g_body_count)
            continue; /* not in the list: the original adds nothing */
        group_add(b, cell, group, 0);
        U32(b, 0x110) |= 0x1000000;
        total++;
        group += GROUP_SIZE;
    }
    *(uint16_t *)(group + 4) = 0;
    U32(group, 8) = 0;
    g_group_count = total;
}

/* 0x45c0c0 */
void GAMEAPI bodies_begin_step(void)
{
    uint32_t n = g_body_count, i;
    uint8_t **bodies = g_bodies;

    for (i = 0; i < n; i++) {
        uint8_t *b = bodies[i];

        U32(b, TIME) = 0;
        U32(b, 0x128) = 0;
        U32(b, 0x114) = 0xffffffff;
        U32(b, 0x120) = 0x7fffffff;
        if (b[0x10c] && !(U32(b, FLAGS) & 0x8000)) {
            uint32_t type = U32(b, FLAGS) & 0x1ff;
            U32(b, 0x110) &= 0x80000000u;
            b[CONTACT_MODE] = type >= 3 && (type <= 4 || type == 0x1d) ? 3 : 1;
        }
    }
    n = g_race_data[0x54d];
    for (i = 0; i < n; i++) {
        uint8_t *car = *(uint8_t **)(g_car_slots + i * CAR_SLOT_SIZE);
        int32_t flash = car[0x65f] - 0x10;
        car[0x65f] = (uint8_t)(flash < 0 ? 0 : flash);
    }
}

/* 0x462160 */
void GAMEAPI bodies_apply_forces(void)
{
    uint32_t n = g_body_count, i;
    uint8_t **bodies = g_bodies;

    for (i = 0; i < n; i++) {
        if (U32(bodies[i], FLAGS) & 0x2000)
            body_script_step(bodies[i]);
        else
            body_forces(bodies[i]);
    }
    forces_finish();
    g_force_scratch_a = 0;
    g_force_scratch_b = 0;
}

/* 0x4621b0 */
void GAMEAPI bodies_prepare(void)
{
    uint32_t n, i;
    uint8_t **bodies;

    cars_prepare();
    n = g_body_count;
    bodies = g_bodies;
    for (i = 0; i < n; i++)
        if (!(U32(bodies[i], FLAGS) & 0x8000))
            body_prepare(bodies[i] + POSITION);
}

/* 0x461310: moves `body` on by dt, plus push/128 if given, relinking it in
 * the grid if it crossed into another cell (top 8 bits of a coordinate). */
void GAMEAPI body_move(uint8_t *body, int32_t dt, const int32_t *push)
{
    int32_t x = I32(body, POSITION) >> 24, y = I32(body, POSITION + 4) >> 24, z = I32(body, POSITION + 8) >> 24;

    body_integrate(body, dt);
    if (push) {
        I32(body, POSITION) += push[0] >> 7;
        I32(body, POSITION + 4) += push[1] >> 7;
        I32(body, POSITION + 8) += push[2] >> 7;
    }
    x ^= I32(body, POSITION) >> 24;
    y ^= I32(body, POSITION + 4) >> 24;
    z ^= I32(body, POSITION + 8) >> 24;
    I32(body, TIME) += dt;
    if (x || y || z) {
        grid_unlink(body);
        grid_link(body);
    }
}

/* 0x4621f0: straight-line move for 0x8000 bodies; no grid relink. */
void GAMEAPI body_move_simple(uint8_t *body, int32_t dt)
{
    int32_t dx = (int32_t)(uint32_t)((uint64_t)((int64_t)dt * I32(body, VELOCITY)) >> 16);
    int32_t dy = (int32_t)(uint32_t)((uint64_t)((int64_t)dt * I32(body, VELOCITY + 4)) >> 16);
    int32_t dz = (int32_t)(uint32_t)((uint64_t)((int64_t)dt * I32(body, VELOCITY + 8)) >> 16);

    I32(body, POSITION) += dx;
    I32(body, POSITION + 4) += dy;
    I32(body, POSITION + 8) += dz;
    I32(body, TIME) += dt;
    if (*(int16_t *)(body + 0x16a))
        g_moved_triggers[g_moved_trigger_count++] = body;
}

/* 0x461ef0 */
void GAMEAPI bodies_advance_to(int32_t dt)
{
    uint8_t **p;

    for (p = g_bodies; *p; p++) {
        int32_t left = dt - I32(*p, TIME);
        if (left > 0) {
            if (U32(*p, FLAGS) & 0x8000)
                body_move_simple(*p, left);
            else
                body_move(*p, left, 0);
        }
    }
}

/* 0x462060: the earliest queued contact: its record in *event, and its time
 * (from the start of the step), or -1 if none is left. */
int32_t GAMEAPI event_next(uint8_t **event)
{
    uint32_t key;

    if (!g_event_queue->count)
        return -1;
    key = event_queue_pop(g_event_queue);
    *event = g_event_queue->events + (key >> 20) * EVENT_SIZE;
    return ((int32_t)key >> 1) & 0x7ffff;
}

/* 0x461510: asks both bodies' callbacks whether to keep the contact; a body
 * that declines is dropped from it. If only the second body is left it
 * becomes the first: the contact normal (+0x38) is flipped and *time made
 * relative to it. */
void GAMEAPI collision_order(uint8_t *event, int32_t *time)
{
    uint8_t *a = EVENT_BODY(event, EVENT_A), *b;
    int32_t offset = -I32(a, TIME); /* read before the null check, as the original */
    callback_fn callback;

    if (a && (callback = *(callback_fn *)(a + CALLBACK)) != 0 &&
        !callback(a, EVENT_BODY(event, EVENT_B), event))
        EVENT_BODY(event, EVENT_A) = 0;
    b = EVENT_BODY(event, EVENT_B);
    if (b) {
        offset += I32(b, TIME);
        callback = *(callback_fn *)(b + CALLBACK);
        if (callback && !callback(b, EVENT_BODY(event, EVENT_A), event))
            EVENT_BODY(event, EVENT_B) = 0;
    }
    if (!EVENT_BODY(event, EVENT_A) && EVENT_BODY(event, EVENT_B)) {
        EVENT_BODY(event, EVENT_A) = EVENT_BODY(event, EVENT_B);
        I32(event, 0x38) = -I32(event, 0x38);
        I32(event, 0x3c) = -I32(event, 0x3c);
        I32(event, 0x40) = -I32(event, 0x40);
        *time -= offset;
        EVENT_BODY(event, EVENT_B) = 0;
    }
}

/* 0x4620a0: removes every queued contact involving `a` or `b`. */
int32_t GAMEAPI event_queue_drop_bodies(uint8_t *a, uint8_t *b)
{
    int32_t dropped = 0, i;

    if (!a && !b)
        return 0;
    for (i = (int32_t)g_event_queue->count; i > 0; i--) {
        uint8_t *event = g_event_queue->events + (g_event_queue->heap[i] >> 20) * EVENT_SIZE;
        int match = 0;

        if (a && (a == EVENT_BODY(event, EVENT_A) || a == EVENT_BODY(event, EVENT_B)))
            match = 1;
        if (b && (b == EVENT_BODY(event, EVENT_A) || b == EVENT_BODY(event, EVENT_B)))
            match = 1;
        if (match) {
            event_queue_remove(g_event_queue, (uint32_t)i);
            dropped++;
        }
    }
    return dropped;
}

/* 0x418960: contacts of one or two bodies (after a collision) with the
 * track and with every group; bodies in no-contact mode are left out. */
int32_t GAMEAPI collisions_find_for(uint8_t *a, uint8_t *b, int32_t dt)
{
    int32_t found, i;

    if (a && (int8_t)a[CONTACT_MODE] < 0)
        a = 0;
    if (b && (int8_t)b[CONTACT_MODE] < 0)
        b = 0;
    if (!a) {
        if (!b)
            return 0;
        a = b;
        b = 0;
    }
    g_pair_bodies[0] = a;
    g_pair_bodies[1] = b;
    g_pair_cell = U32(a, 0x0c) & ~1u;
    g_pair_count = (uint16_t)((b != 0) + 1);
    found = group_contacts(g_pair_group, dt);
    if ((U32(a, FLAGS) & 0x1ff) == 4)
        return found;
    for (i = 0; i < g_group_count; i++) {
        uint8_t *group = g_groups + i * GROUP_SIZE;
        if (*(uint16_t *)(group + 4))
            found += group_pair_contacts(g_pair_group, group, dt);
    }
    return found;
}

/* 0x4188c0: every body's track contacts, then contacts within each group of
 * more than one body and between every pair of groups. */
int32_t GAMEAPI collisions_find_all(int32_t dt)
{
    int32_t found = group_contacts(g_body_group, dt), left = g_group_count;
    uint8_t *group = g_groups;

    for (; left > 0; left--, group += GROUP_SIZE) {
        uint8_t *other;
        int32_t k;

        if (*(uint16_t *)(group + 4) > 1)
            found += group_pair_contacts(group, 0, dt);
        for (k = left - 1, other = group + GROUP_SIZE; k > 0; k--, other += GROUP_SIZE)
            found += group_pair_contacts(group, other, dt);
    }
    return found;
}

/* 0x461410 */
int32_t GAMEAPI physics_world_step(int32_t dt)
{
    int32_t pending, time;
    uint8_t *event, *a, *b;

    bodies_begin_step();
    bodies_apply_forces();
    track_contacts_find(dt);
    body_groups_build();
    bodies_prepare();
    event_queue_clear(g_event_queue);
    pending = collisions_find_all(dt);
    do {
        time = event_next(&event);
        if (time == -1)
            break;
        /* queued times are from the start of the step; make it relative to
         * where the first body already is */
        time -= I32(EVENT_BODY(event, EVENT_A), TIME);
        a = EVENT_BODY(event, EVENT_A);
        b = EVENT_BODY(event, EVENT_B);
        collision_order(event, &time);
        collision_resolve(event, time, dt);
        pending += -1 - event_queue_drop_bodies(a, b);
        a = EVENT_BODY(event, EVENT_A); /* resolving may have changed them */
        b = EVENT_BODY(event, EVENT_B);
        if (a || b)
            pending += collisions_find_for(a, b, dt);
    } while (time > -1);
    (void)pending; /* counted by the original too, never used */
    bodies_advance_to(dt);
    return 0;
}
