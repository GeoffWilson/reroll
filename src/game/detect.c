/* Collision detection for the world step: when, before the step's end
 * time, does each body next hit the track or another body? Each find
 * becomes an event in the queue (0x6b9860) for physics_world_step.
 *
 * A body's contact spheres are its slots (0x48 bytes at 0x6ce740, indices
 * at body+0x140: the body's own sphere at [rigidity], its parts below):
 *   +0x00 local offset (int16 x, y, z), +0x08 position and +0x14
 *   velocity (>> 8), +0x20 contact point, +0x2c normal, +0x38 surface,
 *   +0x3c depth, +0x40 radius, +0x44 flags.
 * A group (0x6b9b80, 0x6b9880.., 0x6b9850) is a list of bodies and a u16
 * count. */
#include "../runtime.h"
#include "body.h"
#include "events.h"
#include "fixed.h"
#include "physics.h"
#include "../coverage.h"

#define SLOT_SIZE 0x48
#define g_slots (*(uint8_t **)0x6ce740)
#define g_slow_motion (*(int32_t *)0x5d9b20)
#define g_slow_motion_body (*(uint8_t **)0x5d9b24)
#define g_slow_motion_scale (*(int32_t *)0x5d9b2c)
#define g_parity (*(uint32_t *)0x6c0a50)
#define g_pair_group ((struct group *)0x6b9850)
#define g_gte_vector_words ((uint32_t *)0x6ccc64)
#define g_gte_result ((const int32_t *)0x6cccc8)

/* body offsets used here */
#define HIT_SLOTS 0x110  /* bit per slot that hit, bit 28: searched, 31: special */
#define SLOT_TIME 0x114  /* time the slots were last placed at */
#define HIT_TIME 0x124   /* time of the first hit (relative) */
#define HIT_KIND 0x128   /* 0 none, 1 hit, 2 overlapping, 8 resting, 0x10 lost */
#define TRACK_CONTACT_COUNT 0x15c

struct group {
    uint8_t **bodies;
    uint16_t count;
};


static uint8_t *slot_of(const uint8_t *body, int i)
{
    return g_slots + body[0x140 + i] * SLOT_SIZE;
}

static uint32_t kind_of(const uint8_t *body)
{
    return U32(body, FLAGS) & 0x1ff;
}

/* 0x41a880: place a part's slot (its local offset turned by the body's
 * matrix, already in the GTE) dt (16.16 of a step) on from the body's
 * position, moving with the body's velocity plus its spin. */
void GAMEAPI slot_follow(uint8_t *body, uint8_t *slot, int32_t dt, int32_t slow)
{
    int32_t vx = I32(body, VELOCITY), vy = I32(body, VELOCITY + 4), vz = I32(body, VELOCITY + 8);
    int32_t ox = 0, oy = 0, oz = 0;

    if ((int8_t)body[RIGIDITY] > 1) {
        int32_t wx, wy, wz;

        g_gte_vector_words[0] = U32(slot, 0);
        g_gte_vector_words[1] = U32(slot, 4);
        gte_rotate();
        wz = I32(body, ANGULAR_VELOCITY + 8) >> 8;
        wy = I32(body, ANGULAR_VELOCITY + 4) >> 8;
        wx = I32(body, ANGULAR_VELOCITY) >> 8;
        ox = g_gte_result[0];
        oy = g_gte_result[1];
        oz = g_gte_result[2];
        vx += oz * wy - oy * wz;
        vy += ox * wz - oz * wx;
        vz += oy * wx - ox * wy;
    }
    if (slow) {
        vx = mul16(slow, vx);
        vy = mul16(slow, vy);
        vz = mul16(slow, vz);
    }
    if (dt > 0) {
        ox += mul16(vx, dt) >> 8;
        oy += mul16(vy, dt) >> 8;
        oz += mul16(vz, dt) >> 8;
    }
    I32(slot, 0x08) = ox + (I32(body, POSITION) >> 8);
    I32(slot, 0x0c) = oy + (I32(body, POSITION + 4) >> 8);
    I32(slot, 0x10) = oz + (I32(body, POSITION + 8) >> 8);
    I32(slot, 0x14) = vx >> 8;
    I32(slot, 0x18) = vy >> 8;
    I32(slot, 0x1c) = vz >> 8;
}

/* 0x41a620: place all of a body's slots at time t. */
void GAMEAPI body_slots_at(uint8_t *body, int32_t t)
{
    int32_t parts = (int8_t)body[RIGIDITY], dt = t - I32(body, TIME), slow = 0, i;
    uint8_t *slot;

    gte_load_matrix(body + MATRIX);
    slot = slot_of(body, parts);
    I32(slot, 0x08) = I32(body, POSITION) >> 8;
    I32(slot, 0x0c) = I32(body, POSITION + 4) >> 8;
    I32(slot, 0x10) = I32(body, POSITION + 8) >> 8;
    I32(slot, 0x14) = I32(body, VELOCITY) >> 8;
    I32(slot, 0x18) = I32(body, VELOCITY + 4) >> 8;
    I32(slot, 0x1c) = I32(body, VELOCITY + 8) >> 8;
    if (g_slow_motion && body != g_slow_motion_body && dt != 0 &&
        (kind_of(body) == 3 || kind_of(body) == 7 || kind_of(body) == 0x15)) {
        slow = g_slow_motion_scale;
        dt = mul16(slow, dt);
    }
    for (i = parts - 1; i >= 0; i--) {
        slot = slot_of(body, i);
        slot_follow(body, slot, dt, slow);
        if (t == 0 && I32(body, TIME) == 0)
            I32(slot, 0x38) = -1;
    }
    I32(body, SLOT_TIME) = t;
}

/* 0x418e80: bodies that never collide with each other: a weapon (kinds
 * 0xc, 0x11) and its owner (kind 3), two weapons of one owner, kind 4
 * with anything, and kind 0x16 with its two cars or another 0x16. */
int32_t GAMEAPI bodies_related(const uint8_t *a, const uint8_t *b)
{
    uint32_t kb = kind_of(b);

    switch (kind_of(a)) {
    case 3:
        if (kb == 0xc)
            return U32(b, 0x18c) == (uint32_t)a;
        if (kb == 0x11)
            return U32(b, 0x180) == (uint32_t)a;
        return 0;
    case 4:
        return 1;
    case 0xc:
        if (kb == 0xc)
            return U32(a, 0x18c) == U32(b, 0x18c);
        if (kb == 0x11)
            return U32(a, 0x18c) == U32(b, 0x180);
        return 0;
    case 0x11:
        if (kb == 0xc)
            return U32(a, 0x180) == U32(b, 0x18c);
        if (kb == 0x11)
            return U32(a, 0x180) == U32(b, 0x180);
        return 0;
    case 0x16:
        if (kb == 3)
            return U32(a, 0x194) == (uint32_t)b || U32(a, 0x198) == (uint32_t)b;
        return kb == 0x16;
    }
    return 0;
}

/* 0x461f40: queue a collision event at time + t (b is 0 for the track).
 * The type's bits 8..11 become the surface when bit 6 is set. */
void GAMEAPI event_add(uint8_t *a, uint8_t *b, const uint8_t *contact, uint32_t *type, int16_t surface,
                       int32_t *velocity, int32_t t)
{
    uint32_t raw = *type, handle;
    uint32_t new_surface = (raw & 0x40) ? raw & 0xf00 : 0;
    uint8_t *event;

    *type = raw & 0xff;
    handle = event_queue_push(g_event_queue, (uint32_t)((I32(a, TIME) + t) * 2 + (b == 0)));
    event = g_event_queue->events + (handle >> 20) * EVENT_SIZE;
    U32(event, 0x04) = (uint32_t)a;
    U32(event, 0x08) = (uint32_t)b;
    memcpy(event + 0x38, contact + 0x2c, 12);
    memcpy(event + 0x2c, contact + 0x20, 12);
    U32(event, 0x48) = U32(contact, 0x3c);
    U32(event, 0x54) = *type;
    *(int16_t *)(event + 0x58) = surface;
    if (velocity) {
        velocity[0] = (int32_t)((uint32_t)velocity[0] << 8);
        velocity[1] = (int32_t)((uint32_t)velocity[1] << 8);
        velocity[2] = (int32_t)((uint32_t)velocity[2] << 8);
        memcpy(event + 0x5c, velocity, 12);
    }
    U32(event, 0x44) = U32(contact, 0x38);
    if (b) {
        U32(event, 0) = 0;
        if (U32(b, HIT_SLOTS) & 0x80000000)
            U32(b, HIT_SLOTS) = 0;
    } else {
        U32(event, 0) = 1;
    }
    if (new_surface)
        *(uint16_t *)(event + 0x5a) = (uint16_t)new_surface;
}

/* 0x415d00: the earliest time within dt that one of the body's parts hits
 * the track; records which parts (bit per part) and how. */
void GAMEAPI body_track_sweep(uint8_t *body, int32_t dt)
{
    int32_t best = dt, limit = dt, out[9], i;
    uint32_t parts = 0;

    i = (int8_t)body[RIGIDITY] - 1;
    if (i < 0) {
        I32(body, HIT_KIND) = 0;
        return;
    }
    for (; i >= 0; i--) {
        uint8_t *slot = slot_of(body, i);
        int32_t t;

        *(uint16_t *)(slot + 0x44) = 0;
        t = (int32_t)slot_track_sweep(body, slot, out, (uint32_t)limit);
        if (!out[7] || (limit < dt && (out[7] & 0x80000)))
            continue;
        if (t == best) {
            parts |= 1u << i;
        } else if (t < best) {
            best = t;
            parts = 1u << i;
        }
        memcpy(slot + 0x20, out, 24);
        I32(slot, 0x38) = out[8];
        I32(slot, 0x3c) = out[6];
        if (best < limit)
            limit = best + 1;
    }
    if (!parts) {
        I32(body, HIT_KIND) = 0;
        return;
    }
    U32(body, HIT_SLOTS) |= parts;
    I32(body, HIT_TIME) = best;
    if (best == limit) {
        if (kind_of(body) == 3 || kind_of(body) == 4) {
            U32(body, HIT_SLOTS) = 0;
            I32(body, HIT_KIND) = 0;
        } else {
            I32(body, HIT_KIND) = 8;
        }
        COVER(COVER_SWEEP_RESTING);
    } else {
        I32(body, HIT_KIND) = best ? 1 : 2;
        COVER(COVER_SWEEP_HIT);
    }
}

/* 0x415c90 */
void GAMEAPI bodies_track_sweep(uint8_t **bodies, int32_t count, int32_t t)
{
    for (; count > 0; count--, bodies++) {
        uint8_t *body = *bodies;

        U32(body, HIT_SLOTS) &= 0x80000000;
        I32(body, HIT_TIME) = t - I32(body, TIME);
        if ((U32(body, FLAGS) & 0x10000) && *(uint16_t *)(body + TRACK_CONTACT_COUNT))
            body_track_sweep(body, t - I32(body, TIME));
    }
}

/* 0x43bfe0: one track contact for the body from its parts that hit:
 * the average point and normal, the shallowest depth when overlapping.
 * Returns 1 if there is none (no parts, or the normals cancel). count
 * gets the number of resting parts (HIT_KIND 8), which only count if
 * resting is set. */
int32_t GAMEAPI body_track_contact(uint8_t *body, uint8_t *contact, int32_t *count, int32_t resting)
{
    int32_t parts = (int8_t)body[RIGIDITY], n = 0, i, inverse;
    uint32_t hits = U32(body, HIT_SLOTS);
    int32_t *point = (int32_t *)(contact + 0x20), *normal = (int32_t *)(contact + 0x2c);
    int32_t px, py, pz;

    *count = 0;
    if (U32(body, HIT_KIND) & 0x2000) {
        uint8_t *slot = slot_of(body, parts);

        memcpy(contact + 0x2c, slot + 0x2c, 12);
        memcpy(contact + 0x20, slot + 0x20, 12);
        *(uint16_t *)(contact + 0x44) = *(uint16_t *)(slot + 0x44);
        U32(contact, 0x3c) = U32(slot, 0x3c);
        U32(contact, 0x38) = U32(slot, 0x38);
        U32(body, HIT_KIND) &= ~0x2000u;
        COVER(COVER_CONTACT_SAVED);
        return 0;
    }
    if (!(hits & 0xffffff))
        return 1;
    memset(contact + 0x20, 0, 24);
    *(uint16_t *)(contact + 0x44) = 0;
    U32(contact, 0x3c) = 0;
    I32(contact, 0x38) = -1;
    for (i = 0; i < (int8_t)body[RIGIDITY]; i++, hits = (uint32_t)((int32_t)hits >> 1)) {
        if (hits & 1) {
            uint8_t *slot = slot_of(body, i);

            normal[1] += I32(slot, 0x30);
            n++;
            normal[0] += I32(slot, 0x2c);
            normal[2] += I32(slot, 0x34);
        }
    }
    if (!normal[0] && !normal[1] && !normal[2])
        return 1;
    if (n > 1)
        vec_normalise(normal);
    n = 0;
    hits = U32(body, HIT_SLOTS);
    for (i = 0; i < (int8_t)body[RIGIDITY]; i++, hits = (uint32_t)((int32_t)hits >> 1)) {
        uint8_t *slot = slot_of(body, i);

        *(uint16_t *)(slot + 0x44) &= 0xfeff;
        if (!(hits & 1))
            continue;
        U32(contact, 0x38) = U32(slot, 0x38);
        if (I32(body, HIT_KIND) == 8) {
            ++*count;
            if (!resting)
                continue;
        }
        point[1] += I32(slot, 0x24);
        point[2] += I32(slot, 0x28);
        point[0] += I32(slot, 0x20);
        *(uint16_t *)(contact + 0x44) &= *(uint16_t *)(slot + 0x44);
        if (I32(body, HIT_KIND) == 2) {
            int32_t d = vec_dot(normal, (const int32_t *)(slot + 0x2c));

            if (!d)
                return 1;
            d = fix_div16(I32(slot, 0x3c), d);
            if (d < I32(contact, 0x3c))
                I32(contact, 0x3c) = d;
        }
        slot[0x45] |= 1;
        n++;
    }
    /* average about the body's position, to keep the sums small */
    px = (I32(body, POSITION) >> 8) * n;
    py = (I32(body, POSITION + 4) >> 8) * n;
    pz = (I32(body, POSITION + 8) >> 8) * n;
    inverse = fix_div16(0x10000, (int32_t)((uint32_t)n << 16));
    point[0] -= px;
    point[1] -= py;
    point[2] -= pz;
    vec_scale(inverse, point, point);
    point[0] += I32(body, POSITION) >> 8;
    point[1] += I32(body, POSITION + 4) >> 8;
    point[2] += I32(body, POSITION + 8) >> 8;
    return 0;
}

/* 0x418c60: track and object contacts for a group's bodies up to time t;
 * returns the number of events queued. Kinds 7 and 0x15 are searched
 * every other body (by a counter that is never stored back). */
int32_t GAMEAPI group_contacts(void *list, int32_t t)
{
    struct group *group = list;
    uint8_t **bodies = group->bodies;
    uint32_t parity = g_parity;
    int32_t n, found = 0;

    for (n = group->count; n > 0; n--, bodies++) {
        uint8_t *body = *bodies;
        uint32_t flags = U32(body, FLAGS);

        if (!(flags & 0x800) || (flags & 0x8200))
            continue;
        if ((flags & 0x1ff) == 7 || (flags & 0x1ff) == 0x15)
            if (parity++ & 1)
                continue;
        if ((U32(body, HIT_SLOTS) & 0x90000000) || I32(body, TIME) == t)
            continue;
        U32(body, FLAGS) = flags | 0x10000;
        if (I32(body, SLOT_TIME) != I32(body, TIME))
            body_slots_at(body, I32(body, TIME));
        if (kind_of(body) == 0x11) {
            I32(slot_of(body, 0), 0x40) = 0x1900;
            I32(slot_of(body, 1), 0x40) = 0x5000;
        }
    }

    bodies_track_sweep(group->bodies, group->count, t);
    for (n = group->count, bodies = group->bodies; n > 0; n--, bodies++)
        if ((U32(*bodies, FLAGS) & 0x10000) && *(uint16_t *)(*bodies + 0x15e))
            body_object_hits(*bodies);

    for (n = group->count, bodies = group->bodies; n > 0; n--, bodies++) {
        uint8_t *body = *bodies;
        uint8_t contact[0x48];
        int32_t resting;

        if (!(U32(body, FLAGS) & 0x10000))
            continue;
        U32(body, FLAGS) &= ~0x10000u;
        if (U32(body, HIT_KIND)) {
            if (body_track_contact(body, contact, &resting, U32(body, HIT_KIND) == 8)) {
                U32(body, HIT_KIND) = 0x10;
                COVER(COVER_CONTACT_LOST);
                event_add(body, 0, contact, (uint32_t *)(body + HIT_KIND), 0, 0, 0);
            } else {
                event_add(body, 0, contact, (uint32_t *)(body + HIT_KIND), (int16_t)resting, 0,
                          I32(body, HIT_TIME));
            }
            found++;
        }
        U32(body, HIT_SLOTS) |= 0x10000000;
    }

    for (n = group->count, bodies = group->bodies; n > 0; n--, bodies++)
        if (kind_of(*bodies) == 0x11) {
            I32(slot_of(*bodies, 0), 0x40) = 32000;
            I32(slot_of(*bodies, 1), 0x40) = 32000;
        }
    return found;
}

static int far_apart(int32_t a, int32_t b)
{
    return abs32((a - b) >> 23) > 2;
}

/* 0x418a30: contacts between the bodies of a group (other 0) or between
 * two groups, each pair tested from the earlier body's time to t; returns
 * the number of events queued. */
int32_t GAMEAPI group_pair_contacts(void *list, void *other_list, int32_t t)
{
    struct group *group = list, *other = other_list;
    uint8_t **outer = group->bodies;
    int32_t left, found = 0;

    if (other) {
        const uint8_t *a = group->bodies[0], *b = other->bodies[0];

        if (far_apart(I32(a, POSITION), I32(b, POSITION)) ||
            far_apart(I32(a, POSITION + 4), I32(b, POSITION + 4)) ||
            far_apart(I32(a, POSITION + 8), I32(b, POSITION + 8)))
            return 0;
    }
    for (left = group->count; left > 0; left--, outer++) {
        uint8_t *a = *outer, **inner;
        int32_t time, dt, n;
        uint32_t hits;

        if (!a || I32(a, TIME) == t)
            continue;
        time = I32(a, TIME);
        hits = U32(a, HIT_SLOTS);
        body_slots_at(a, time);
        dt = t - time;
        if (dt <= 0)
            continue;
        if (other) {
            inner = other->bodies;
            n = other->count;
        } else {
            inner = outer + 1;
            n = left - 1;
        }
        for (; n > 0; n--, inner++) {
            uint8_t *b = *inner, contact[0x48];
            int32_t velocity[3], when;
            uint32_t type;

            if (!b || a == b || I32(b, TIME) == t || bodies_related(a, b) || bodies_related(b, a))
                continue;
            if (group == g_pair_group && a == g_pair_group->bodies[1] && b == g_pair_group->bodies[0])
                continue;
            if ((hits & 0x80000000) && (U32(b, HIT_SLOTS) & 0x80000000))
                continue;
            body_slots_at(b, time);
            when = body_pair_test(a, b, &type, contact, velocity, dt);
            type &= 0xff;
            if (!type)
                continue;
            I32(contact, 0x38) = -3;
            event_add(a, b, contact, &type, 0, velocity, when);
            COVER(COVER_PAIR_EVENT);
            found++;
        }
    }
    return found;
}
