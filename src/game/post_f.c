/* After the world step (sim_step 0x444120): attached points (0x442840)
 * and putting fallen bodies back on the track (0x468450), with the object
 * grid lists, the slot pool and the lap counting they use.
 *
 * Grid: each track cell has two lists of 16-bit link indices (cell +8 for
 * bodies, +0xa for particles and similar); an object holds its next/prev
 * links at +4/+6 and its cell (bit 0: off the track) at +0xc. Link index
 * i is the 0x80-byte record at pool + (i - 1) * 0x80. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"

#undef grid_link
#undef grid_unlink

#define g_race (GP(0x4b1960))
#define g_track (GP(0x6c1e30))
#define g_markers (GP(0x6c1eb0))
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_grid_pool (GP(0x6b9b90))
#define g_bodies ((uint32_t *)GP(0x6b9b80))
#define g_body_count (*(uint16_t *)0x6b9b84)
#define g_slots (GP(0x6ce740))  /* 0x48-byte collision slots */
#define g_slot_free (GP(0x6ce73c))
#define g_slot_used (GP(0x6ce744))
#define g_slot_used_last (*(int32_t *)0x6ce748)
#define g_slot_where (GP(0x6ce750))
#define g_slot_used_count (*(int32_t *)0x6ce754)
#define g_slot_free_last (*(int32_t *)0x6ce758)
#define g_models (GP(0x6bfac0))
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_misc_seed (*(uint32_t *)0x4b11d8)
#define g_sin ((const int16_t *)0x4b24f8)

#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define SLOT_OF_CAR(car) (g_car_slots + U16(car, 0x64e) * CAR_SLOT_SIZE)

/* Still original. */
#define sound_play(id, a, b, c, where) GCALL(0x471c90, sound_play_body)(id, a, b, c, (uint32_t)(uintptr_t)(where))
#define points_transform GCALL(0x426620, gte_rt_float) /* x87 */
#define effect_burst GCALL(0x42ad60, particles_burst)
#define effect_42b2d0 GCALL(0x42b2d0, light_flash_small)
#define effect_42b300 GCALL(0x42b300, light_flash)
#define effect_42b330 GCALL(0x42b330, blast_flash_set)
#define effect_42b370 GCALL(0x42b370, blast_glow_set)
#define effect_42b3b0 GCALL(0x42b3b0, smoke_cloud_start)
#define effect_42c760 GCALL(0x42c760, scorch_track)
#define effect_42c360 GCALL(0x42c360, shockwave_start)
#define effect_42c3c0 GCALL(0x42c3c0, blast_point_set)
#define effect_4624d0 GCALL(0x4624d0, pieces_spawn)
#define sound_release GCALL(0x471f40, sound_voice_stop)

/* --- the object grid --- */

/* 0x4613e0: link index to record (0 for none). */
uint8_t *GAMEAPI grid_record(uint32_t link)
{
    link &= 0xffff;
    if (link == 0)
        return 0;
    return (uint8_t *)(uintptr_t)((link + 0x1ffffff) * 0x80 + (uint32_t)(uintptr_t)g_grid_pool);
}

/* 0x4613c0: record to link index. */
uint32_t GAMEAPI grid_index(const uint8_t *record)
{
    if (!record)
        return 0;
    return ((uint32_t)(uintptr_t)record - (uint32_t)(uintptr_t)g_grid_pool + 0x80) >> 7;
}

static void unlink_from(uint8_t *object, uint32_t head)
{
    uint8_t *next, *prev;

    /* (all 16-bit stores: the links alias each other) */
    if (S16(object, 6) == 0) {
        U16((uint8_t *)(uintptr_t)(U32(object, 0xc) & ~1u), head) = U16(object, 4);
        next = grid_record(U16(object, 4));
        if (next)
            U16(next, 6) = 0;
        U16(object, 4) = 0;
        U16(object, 6) = 0;
        return;
    }
    prev = grid_record(U16(object, 6));
    next = grid_record(U16(object, 4));
    if (next)
        U16(next, 6) = U16(object, 6);
    U16(prev, 4) = U16(object, 4);
    U16(object, 4) = 0;
    U16(object, 6) = 0;
}

static void insert_at(uint8_t *object, uint32_t head)
{
    uint16_t *cell = (uint16_t *)(uintptr_t)((U32(object, 0xc) & ~1u) + head);
    uint16_t first = *cell;
    uint16_t self = (uint16_t)grid_index(object);

    U16(object, 4) = first;
    U16(object, 6) = 0;
    if (first != 0)
        U16(grid_record(first), 6) = self;
    *cell = self;
}

/* 0x4611d0 */
void GAMEAPI object_grid_unlink(uint8_t *object)
{
    unlink_from(object, 8);
}

/* 0x4612a0: the same on the cell's second list. */
void GAMEAPI object_grid_unlink2(uint8_t *object)
{
    unlink_from(object, 0xa);
}

/* 0x461170 */
void GAMEAPI object_grid_insert(uint8_t *object)
{
    insert_at(object, 8);
}

/* 0x461240 */
void GAMEAPI object_grid_insert2(uint8_t *object)
{
    insert_at(object, 0xa);
}

/* 0x460fb0: into the cell at the body's position. */
void GAMEAPI object_grid_link(uint8_t *object)
{
    int32_t off_track;
    uint8_t *cell = track_cell(I32(object, 0x34), I32(object, 0x38), I32(object, 0x3c), &off_track);

    U32(object, 0xc) = (uint32_t)(uintptr_t)cell + (off_track != 0);
    object_grid_insert(object);
}

/* 0x460ff0: into the second list of the cell at (x, y, z). */
void GAMEAPI object_grid_link2_at(uint8_t *object, int32_t x, int32_t y, int32_t z)
{
    int32_t off_track = z;
    uint8_t *cell = track_cell(x, y, z, &off_track);

    U32(object, 0xc) = (uint32_t)(uintptr_t)cell + (off_track != 0);
    object_grid_insert2(object);
}

/* --- attached points --- */

/* 0x442940: move the up-to-five points a body carries (model + 0x4c
 * offsets; +0x16a: kind << 12 | index) with it: 1 a 0x6c1b88 point, 2 a
 * grid object (relinked when it changes 256-unit block), 4 a marker. */
void GAMEAPI body_points_update(uint8_t *body)
{
    uint8_t *model = GP(body + 0x10);
    const uint32_t *offsets = (const uint32_t *)(model + U16(model, 0x4c));
    const uint16_t *points = (const uint16_t *)(body + 0x16a);
    int32_t *gte_translation = (int32_t *)0x6ccbf4;
    float *float_translation = (float *)0x6ccd0c;
    uint32_t i;

    gte_load_matrix(body + MATRIX);
    for (i = 0; i < 3; i++) {
        gte_translation[i] = I32(body, POSITION + i * 4) >> 16;
        float_translation[i] = (float)gte_translation[i];
    }
    for (i = 0; i < 5; i++, offsets += 2) {
        uint32_t point = points[i];
        int16_t x, y, z;

        if ((point & 0xf000) == 0)
            return;
        U32((uint8_t *)0x6ccc64, 0) = offsets[0];
        U32((uint8_t *)0x6ccc68, 0) = offsets[1];
        points_transform();
        x = (int16_t)I32((uint8_t *)0x6cccc8, 0);
        y = (int16_t)I32((uint8_t *)0x6ccccc, 0);
        z = (int16_t)I32((uint8_t *)0x6cccd0, 0);
        switch (point >> 12) {
        case 1: {
            uint8_t *p = (uint8_t *)0x6c1b88 + (point & 0xfff) * 8;
            S16(p, 0) = x;
            S16(p, 2) = y;
            S16(p, 4) = z;
            S16(p, 6) = 1;
            break;
        }
        case 2: {
            uint8_t *object = GP(0x5d6e00 + (point & 0xfff) * 4);
            uint32_t old = U32(object, 0x10);
            int32_t old_z = S16(object, 0x14);

            S16(object, 0x10) = x;
            S16(object, 0x12) = y;
            S16(object, 0x14) = z;
            if ((((uint32_t)(z ^ old_z)) | (U32(object, 0x10) ^ old)) & 0xff00ff00) {
                object_grid_unlink2(object);
                object_grid_link2_at(object, (int32_t)((uint32_t)S16(object, 0x10) << 16),
                                     (int32_t)((uint32_t)S16(object, 0x12) << 16),
                                     (int32_t)((uint32_t)S16(object, 0x14) << 16));
            }
            break;
        }
        case 4: {
            int16_t *p = (int16_t *)(GP(g_markers + 0x10c) + (point & 0xfff) * 0xc);
            p[0] = x;
            p[1] = y;
            p[2] = z;
            break;
        }
        }
    }
}

/* 0x442840: for each body queued this step (0x6b8d00, count 0x4d11ec). */
void GAMEAPI bodies_points_update(void)
{
    uint32_t i;

    for (i = 0; i < *(uint32_t *)0x4d11ec; i++)
        body_points_update(GP(0x6b8d00 + i * 4));
    *(uint32_t *)0x4d11ec = 0;
}

/* --- collision slot pool and body list --- */

/* 0x4190a0: take a slot from the free stack onto the used list; returns
 * it in eax (al). */
uint32_t GAMEAPI slot_take(void)
{
    uint32_t slot = g_slot_free[g_slot_free_last--];

    g_slot_used[++g_slot_used_last] = (uint8_t)slot;
    g_slot_where[slot] = (uint8_t)g_slot_used_last;
    g_slot_used_count++;
    return slot;
}

/* 0x4190f0: give a slot back (moving the last used one into its place). */
void GAMEAPI slot_give(uint32_t slot)
{
    uint32_t where = g_slot_where[slot];
    uint32_t last = g_slot_used[g_slot_used_last];

    g_slot_used[where] = (uint8_t)last;
    g_slot_where[last] = (uint8_t)where;
    g_slot_free[++g_slot_free_last] = (uint8_t)slot;
    g_slot_used_last--;
    g_slot_used_count--;
}

/* 0x45c040: drop a body from the body list and its group's list. */
void GAMEAPI body_list_remove(uint8_t *body)
{
    uint32_t *list = g_bodies, *last = g_bodies + g_body_count - 1;
    uint8_t *group;

    while (*list != (uint32_t)(uintptr_t)body)
        list++;
    *list = *last;
    *last = 0;
    g_body_count--;
    group = GP(body + 0x130);
    if (group) {
        uint32_t *entries = (uint32_t *)GP(group);
        uint32_t *end = entries + U16(group, 4) - 1;

        while (*entries != (uint32_t)(uintptr_t)body)
            entries++;
        *entries = *end;
        *end = 0;
        U16(group, 4)--;
    }
}

/* 0x419300: free a body's slots and take it out of the simulation. */
void GAMEAPI body_retire(uint8_t *body)
{
    int32_t i;

    for (i = (int8_t)body[0x10c]; i >= 0; i--)
        slot_give(body[0x140 + i]);
    body[0x10c] = 0;
    body_list_remove(body);
    U32(body, 0) |= 0x8000;
}

/* --- effects --- */

/* 0x42ac30: a particle from the free list, or the oldest one in use. */
uint8_t *GAMEAPI particle_take(void)
{
    uint8_t *p, *prev, *next;

    if (GP(0x4be128)) {
        p = GP(0x4be128);
        *(uint32_t *)0x4be128 = (uint32_t)(uintptr_t)GP(p + 0xac);
    } else {
        uint8_t *oldest = GP(0x4be130);

        p = oldest;
        if (U32(oldest, 0xb0)) {
            uint8_t *q = oldest;
            for (;;) {
                q = GP(q + 0xa8);
                if (!q) {
                    p = oldest;
                    break;
                }
                p = q;
                if (!U32(q, 0xb0))
                    break;
            }
        }
        prev = GP(p + 0xac);
        next = GP(p + 0xa8);
        if (prev)
            *(uint32_t *)(prev + 0xa8) = (uint32_t)(uintptr_t)next;
        else
            *(uint32_t *)0x4be130 = (uint32_t)(uintptr_t)next;
        if (next)
            *(uint32_t *)(next + 0xac) = (uint32_t)(uintptr_t)prev;
        else
            *(uint32_t *)0x4be12c = (uint32_t)(uintptr_t)prev;
    }
    *(uint32_t *)(p + 0xa8) = 0;
    *(uint32_t *)(p + 0xac) = (uint32_t)(uintptr_t)GP(0x4be12c);
    *(uint32_t *)0x4be12c = (uint32_t)(uintptr_t)p;
    if (GP(p + 0xac))
        *(uint32_t *)(GP(p + 0xac) + 0xa8) = (uint32_t)(uintptr_t)p;
    if (!GP(0x4be130))
        *(uint32_t *)0x4be130 = (uint32_t)(uintptr_t)p;
    if (p[0x94] == 1)
        object_grid_unlink2(p);
    U32(p, 0xb0) = 0;
    return p;
}

/* 0x42cd40: fade level (alpha, or the whole colour when `set`). */
void GAMEAPI particle_fade(uint8_t *p, int32_t level, int32_t set)
{
    uint32_t v = (uint32_t)(level * 6);

    if (v > 0xfe)
        v = 0xff;
    if (set) {
        U32(p, 0x98) = v << 24 | 0xffe0e0;
        return;
    }
    p[0x9b] = (uint8_t)v;
}

/* 0x42b700: a spark from `at` along `dir` (kind 0x17, or 0x1e if `alt`);
 * grey if `grey`, else a random colour. */
void GAMEAPI spark_spawn(const int32_t *at, const int32_t *dir, int32_t grey, int32_t alt)
{
    uint8_t *p = particle_take();
    uint32_t r, a, b;

    U32(p, 0) = alt ? 0x1e : 0x17;
    p[0x95] = 0x10;
    U16(p, 0x96) = 100;
    U16(p, 0xa2) = 0x8000;
    S16(p, 0x9c) = (int16_t)dir[0];
    S16(p, 0x9e) = (int16_t)dir[1];
    S16(p, 0xa0) = (int16_t)dir[2];
    I32(p, 0x88) = dir[0] * 0x100 + at[0];
    I32(p, 0x8c) = dir[1] * 0x100 + at[1];
    I32(p, 0x90) = dir[2] * 0x100 + at[2];
    particle_fade(p, 100, 1);
    object_grid_link2_at(p, I32(p, 0x88), I32(p, 0x8c), I32(p, 0x90));
    p[0x94] = 1;
    U32(p, 0xa4) = *(uint32_t *)0x6cc808;
    r = g_misc_seed = g_misc_seed * 16807 & 0x7fffffff;
    a = (r >> 15) * 0xff >> 16;
    if (grey == 0) {
        r = r * 16807 & 0x7fffffff;
        b = (r >> 15) * 0xff >> 8 & 0xffff00;
        r = g_misc_seed = r * 16807 & 0x7fffffff;
        U32(p, 0x98) = b | a | ((r >> 15) * 0xff & 0xffff0000) | 0xff000000;
        return;
    }
    U32(p, 0x98) = (a << 8 | a) << 8 | a | 0xff000000;
}

/* 0x42b8b0 */
void GAMEAPI spark_spawn_plain(const int32_t *at, const int32_t *dir, int32_t grey)
{
    spark_spawn(at, dir, grey, 0);
}

/* 0x42aa30: an explosion's sound and effects, by the bits of `what`. */
void GAMEAPI explosion_effects(const int32_t *at, const int32_t *dir, int32_t size, uint32_t what, uint32_t sound)
{
    if (size != 0 && (uint8_t)sound != 0) {
        uint32_t i = *(uint32_t *)0x6ccb7c;
        int32_t *where = (int32_t *)(uintptr_t)(0x6cc820 + i * 0xc);

        *(uint32_t *)0x6ccb7c = i + 1;
        if (i + 1 > 0x17)
            *(uint32_t *)0x6ccb7c = 0;
        where[0] = at[0];
        where[1] = at[1];
        where[2] = at[2];
        sound_play(sound, 6, 0x400, 10000, where);
    }
    if (what & 4)
        effect_burst(at, dir, size, *(uint32_t *)0x6ccb9c, 4);
    if (what & 8)
        effect_burst(at, dir, size, *(uint32_t *)0x6cc804, 8);
    if (what & 0x200)
        effect_42b2d0(at, size);
    if (what & 0x400)
        effect_42b300(at, size);
    if (what & 0x10)
        effect_burst(at, dir, size, *(uint32_t *)0x6ccb50, 0x10);
    if (what & 1)
        effect_burst(at, dir, size, *(uint32_t *)0x6ccb80, 1);
    if (what & 0x800)
        effect_42b330(at, size);
    if (what & 0x20)
        effect_4624d0(at, dir, size);
    if (what & 0x80)
        effect_42b3b0(at, 1);
    if (what & 0x80000)
        effect_42b3b0(at, 2);
    if (what & 0x10000)
        shake_start(at, size);
    if (what & 0x40000)
        effect_42c760(at, size);
    if (what & 0x100)
        effect_42c360(at);
    if (what & 0x40)
        effect_42c3c0(at); /* (the original also pushes size) */
    if (what & 0x20000)
        effect_42b370(at, size);
    if (what & 0x1000)
        effect_burst(at, dir, size, *(uint32_t *)0x6ccb58, 0x4000);
}

/* --- fallen bodies --- */

/* 0x458320: a pickup body (type 0x21) that fell: remove it, with a
 * puff of smoke if it had been out a while. */
void GAMEAPI pickup_fallen(uint8_t *body)
{
    uint8_t *info = GP(body + 0x164);
    int32_t age = I32(info, 4);

    I32(info, 4) = 0;
    object_grid_unlink(body);
    body_retire(body);
    if (age > 9) {
        int32_t at[3] = {I32(body, 0x34), I32(body, 0x38) + 0x280000, I32(body, 0x3c)};
        int32_t dir[3] = {0, (int32_t)0xffc00000, 0};

        explosion_effects(at, dir, 8, 0x5126b, 0xb);
        *(uint32_t *)0x6b9d48 = g_race_ticks;
    }
    if ((int32_t)info[8] < *(int32_t *)0x4b1c8c) {
        sound_release(info[8]);
        info[8] = *(uint8_t *)0x4b1c8c;
    }
}

/* 0x46c160: a mine or similar (types 7, 0x15) that fell: remove it. */
void GAMEAPI dropped_fallen(uint8_t *item)
{
    uint8_t *body = GP(item);

    body_retire(body);
    object_grid_unlink(body);
    ((uint8_t *)0x600640)[item[5]] = 0;
    item[4] = 0;
    U16(body, 0x160) = 0;
}

/* 0x469810: mark cars as fallen (+0x128 = 0x20) when far above their
 * track's ceiling, or after 90 steps nearly stopped with a wheel off the
 * ground. */
void GAMEAPI cars_check_fallen(void)
{
    uint32_t n = g_race[0x54d];
    uint8_t *slot = g_car_slots;

    for (; n != 0; n--, slot += CAR_SLOT_SIZE) {
        uint8_t *car = GP(slot);
        int32_t ceiling = S16(GP(g_models + car[0x11c] * 4), 0x14) + 0x200;

        if (ceiling < I32(car, 0x38) >> 16) {
            U32(car, 0x128) = 0x20;
        } else if (S16(car, 0x658) < 2) {
            int w;

            slot[0x258]++;
            for (w = 0; w < 4; w++)
                if ((car[0x324 + w * 0xc4] & 1) == 0)
                    break;
            if (w == 4)
                slot[0x258] = 0;
            if (slot[0x258] <= 0x5a)
                continue;
            U32(GP(slot), 0x128) = 0x20;
        } else {
            slot[0x258] = 0;
            continue;
        }
        slot[0x258] = 0;
        U32(GP(slot), 0x110) &= 0x7fffffff;
    }
}

/* 0x40ee60: a quaternion turning about the vertical axis to face `dir`
 * (normalised in place). */
void GAMEAPI quat_facing(int32_t *q, int32_t *dir)
{
    int32_t x, z, s;

    vec_normalise(dir);
    x = dir[0];
    z = dir[2];
    s = (0x10000 - z) / 2;
    if (s < 0)
        s = 0;
    s = fix_isqrt(s);
    if (x < 0)
        s = -s;
    if (z < -0x10000)
        z = -0x10000;
    q[2] = s;
    q[0] = fix_isqrt((z + 0x10000) / 2);
    q[1] = 0;
    q[3] = 0;
}

/* 0x406e00: tell the car's camera to cut. */
void GAMEAPI car_camera_cut(const uint8_t *car)
{
    uint8_t *camera = GP(SLOT_OF_CAR(car) + 0x28);

    if ((uint32_t)(uintptr_t)camera != 0x12345678)
        U16(camera, 0x42) = 1;
}

/* 0x468f80: a ring of 16 sparks around a respawning car (`mode` 2: grey,
 * without the flash at +0x689). */
void GAMEAPI car_respawn_sparks(uint8_t *car, int32_t mode)
{
    int32_t x = I32(car, 0x34), y = I32(car, 0x38), z = I32(car, 0x3c);
    uint32_t i;

    if (mode == 2)
        mode = 0;
    else
        car[0x689] = 0x1f;
    for (i = 0; i < 16; i++) {
        uint32_t a = i * 16, b = a + 0x40;
        int32_t s = (int32_t)((uint32_t)g_sin[a] << 7), c = (int32_t)((uint32_t)g_sin[b] << 7);
        int32_t at[3], dir[3];

        at[0] = s + (s >> 1) + x;
        at[1] = y;
        at[2] = c + (c >> 1) + z;
        b &= 0xff;
        dir[0] = g_sin[b] >> 4;
        dir[1] = 0;
        dir[2] = g_sin[b + 0x40] >> 4;
        spark_spawn_plain(at, dir, mode);
    }
}

/* --- placing a fallen body --- */

#define g_planes (((uint32_t *)GP(0x6bfadc)))
#define DISTANCES ((uint16_t *)0x4d11f8)

/* respawn_blocked's track sweeps run with limit 0, where the sweep reads
 * out[6] before writing it: in the original, a stack slot last written by
 * 0x469040 (where it keeps the patch's first row) or, when the body's cell
 * has objects, by 0x41a0f0 (0, the caller's spent loop counter). Kept here
 * to give the same result. */
static int32_t sweep_stale;

/* 0x469040: gather the track polygons of one patch of cells (`patch`:
 * +0 row << 10... see below) near `point` into the body's contact list
 * (+0x154, count +0x15c), skipping ones already listed (by key and kind)
 * and, beyond `radius2` >> 6, ones facing away; and remember the nearest
 * grid corner (+0x120 distance << 6, +0x11c its key).
 *
 * patch +0: column << 10 | row; +2: model; +4: rows << 8 | columns << 12
 * | extra reach. A key is model | column << 8 | row << 16. */
void GAMEAPI patch_gather(uint8_t *slot, const uint16_t *patch, uint8_t *body, int32_t dt, const int16_t *point,
                          int32_t radius2)
{
    const uint32_t *planes = g_planes;
    uint32_t n = U16(body, 0x15c), n0 = n;
    uint32_t model_index = ((const uint8_t *)patch)[2] & 0x7f;
    uint32_t col0 = patch[0] >> 10, row0 = patch[0] & 0x3ff;
    uint32_t rows = patch[2] >> 8 & 0xf, cols = patch[2] >> 12;
    uint8_t *model = GP(g_models + model_index * 4);
    int32_t width = I32(model, 8), stride = width + 1, skip = width - (int32_t)cols;
    uint32_t index = (uint32_t)stride * row0 + col0;
    const int16_t *verts = (const int16_t *)(GP(model + 0x20) + index * 8);
    const uint8_t *info = GP(model + 0x50) + (index - row0) * 8;
    int32_t limit = (int32_t)((patch[2] & 0xff) * (patch[2] & 0xff) + (uint32_t)radius2) >> 6;
    int32_t px = point[0], py = point[1], pz = point[2];
    uint32_t base_key = model_index | col0 << 8 | row0 << 16, key = base_key, best_key = 0;
    int32_t best = 0x7fffffff, tested = 0;
    uint8_t *list = GP(body + 0x154), *out;
    uint32_t r, c;
    int side;

    sweep_stale = (int32_t)row0;

    /* squared distances (>> 6, capped) from the point to each grid corner */
    {
        const int16_t *v = verts;
        uint16_t *d = DISTANCES;

        for (r = 0; r <= rows; r++, v += skip * 4)
            for (c = 0; c <= cols; c++, v += 4) {
                int32_t dx = v[0] - px, dy = v[1] - py, dz = v[2] - pz;
                int32_t dist = (int32_t)((uint32_t)(dz * dz) + (uint32_t)(dy * dy) + (uint32_t)(dx * dx)) >> 6;

                *d++ = (uint16_t)((dist & 0xffff0000) ? 0xffff : dist);
            }
    }
    {
        const uint16_t *d = DISTANCES;

        for (r = 0; r < rows; r++) {
            uint32_t last_plane = 0xffffffff;

            key = (key & 0xffff00ff) | col0 << 8;
            for (c = 0; c < cols; c++) {
                uint32_t sel = 0, m = d[0];
                int tris, t;

                if (d[1] < m) {
                    m = d[1];
                    sel = 1;
                }
                if (d[cols + 1] < m) {
                    m = d[cols + 1];
                    sel = 2;
                }
                if (d[cols + 2] < m) {
                    m = d[cols + 2];
                    sel = 3;
                }
                if ((int32_t)m < best) {
                    best_key = (key & 0xffff00ff) | (uint32_t)(uint8_t)((sel & 1) + (key >> 8)) << 8;
                    best_key = (best_key & 0xffff) | ((best_key >> 16) + (sel >> 1)) << 16;
                    best = (int32_t)m;
                }
                if ((int32_t)m <= limit) {
                    tris = (((const uint8_t *)verts)[(width + 2) * 8 + 6] & 2) ? 2 : 1;
                    for (t = tris; t > 0; t--) {
                        uint32_t plane_index = U16(info, 2), vi, kind, plane;
                        uint8_t flags = ((const uint8_t *)verts)[(width + 2) * 8 + 6];
                        const int16_t *vertex;
                        int found = 0;

                        if ((flags & 2) == 0) {
                            vi = (sel & 1) + ((sel & 2) ? (uint32_t)stride : 0);
                            vertex = verts;
                            kind = 0;
                        } else if ((flags & 4) == 0) {
                            if (t == 2) {
                                vi = 1;
                                vertex = verts + 4;
                                kind = 5;
                            } else {
                                kind = 4;
                                vi = (uint32_t)stride;
                                plane_index++;
                                vertex = verts + stride * 4;
                            }
                        } else if (t == 2) {
                            vi = 0;
                            vertex = verts;
                            kind = 2;
                        } else {
                            kind = 3;
                            vi = (uint32_t)width + 2;
                            plane_index++;
                            vertex = verts + (width + 2) * 4;
                        }
                        plane = planes[plane_index];
                        if (plane != last_plane)
                            tested = plane_test(slot, vertex, plane, dt | (int32_t)0x80000000);
                        last_plane = plane;
                        if (tested != 1)
                            continue;
                        if (n0 != 0) {
                            const uint8_t *e = list;
                            uint32_t k;

                            for (k = n0; (int32_t)k > 0; k--, e += 0x10)
                                if (U32(e, 4) == key && kind == (e[0] & 0xfu)) {
                                    found = 1;
                                    break;
                                }
                            if (found)
                                continue;
                        }
                        if ((int32_t)m > radius2 >> 6) {
                            uint32_t a = (sel & 1) + ((sel & 2) ? (uint32_t)stride : 0), b, cc;
                            const int16_t *A = verts + a * 4, *B, *C;
                            int32_t dx = px - A[0], dy = py - A[1], dz = pz - A[2];


                            b = ((sel >> 1) - 1) & 1;
                            if (sel & 1)
                                b += (uint32_t)stride;
                            B = verts + b * 4;
                            if ((int32_t)((uint32_t)((B[2] - A[2]) * dz) + (uint32_t)((B[1] - A[1]) * dy) +
                                          (uint32_t)((B[0] - A[0]) * dx)) < 0) {
                                cc = sel >> 1;
                                if ((sel & 1) == 0)
                                    cc += (uint32_t)stride;
                                C = verts + cc * 4;
                                if ((int32_t)((uint32_t)((C[2] - A[2]) * dz) + (uint32_t)((C[1] - A[1]) * dy) +
                                              (uint32_t)((C[0] - A[0]) * dx)) < 0)
                                    continue;
                            }
                        }
                        out = list + n * 0x10;
                        n++;
                        out[3] = (uint8_t)width;
                        U32(out, 4) = key;
                        out[0] = (out[0] & 0xf0) | (uint8_t)(kind & 0xf);
                        U32(out, 8) = plane;
                        *(uint32_t *)(out + 0xc) = (uint32_t)(uintptr_t)verts;
                        out[1] = (uint8_t)vi;
                    }
                }
                key = (key & 0xffff00ff) | (uint32_t)(uint8_t)((key >> 8) + 1) << 8;
                verts += 4;
                d++;
                info += 8;
            }
            key = (key & 0xffff) | ((key >> 16) + 1) << 16;
            d++;
            info += skip * 8;
            verts += skip * 4 + 4;
        }
    }
    out = list + n * 0x10;
    if ((model[4] & 1) == 0) {
        for (side = 0; side < 2; side++) {
            const uint16_t *edges;
            uint32_t at, kind;
            const int16_t *vertex;

            if (side == 0 ? col0 != 0 : col0 + cols != (uint32_t)width)
                continue;
            edges = (const uint16_t *)(GP(model + 0x38) + row0 * 4);
            at = (row0 + 1) * (uint32_t)stride;
            key = base_key;
            if (side) {
                key = (key & 0xffff00ff) | (uint32_t)(uint8_t)width << 8;
                at--;
                kind = 7;
            } else {
                key &= 0xffff00ff;
                kind = 6;
            }
            vertex = (const int16_t *)(GP(model + 0x20) + at * 8);
            for (r = rows; (int32_t)r > 0; r--, edges += 2, vertex += stride * 4,
                key = (key & 0xffff) | ((key >> 16) + 1) << 16) {
                uint16_t e = side ? edges[1] : edges[0];
                int32_t dy;
                uint32_t plane;

                if (e == 0xffff)
                    continue;
                dy = point[1] - vertex[1];
                if (dy > 0 && (dy * dy) >> 6 > limit)
                    continue;
                plane = planes[e];
                if (plane_test(slot, vertex, plane, dt | (int32_t)0x80000000) == 0)
                    continue;
                n++;
                out[3] = (uint8_t)width;
                U32(out, 4) = key;
                U32(out, 8) = plane;
                out[0] = (out[0] & 0xf0) | (uint8_t)kind;
                *(uint32_t *)(out + 0xc) = (uint32_t)(uintptr_t)vertex;
                out[1] = 0;
                out += 0x10;
            }
        }
    }
    U16(body, 0x15c) = (uint16_t)n;
    if ((int32_t)((uint32_t)best << 6) < I32(body, 0x120)) {
        I32(body, 0x120) = (int32_t)((uint32_t)best << 6);
        U32(body, 0x11c) = best_key;
    }
}

/* --- lap counting --- */

/* Slot fields: +0x2c lap, +0x2e laps started, +0x2f progress counter,
 * +0x30/+0x31 next and previous checkpoint (index into track +0x90),
 * +0x32/+0x33 their checkpoint records, +0x34 position along the lap
 * (0..0xffff), +0x38 distance driven backwards, +0x3c/+0x48 lap times,
 * +0x259 bit 0 finished. Checkpoints (track +0x70, 0x14 bytes): +0 id,
 * +1 order, +2 lap position, +4 point, +0xc normal (64 = 1), +0x12 bit 0
 * timing line. */
#define CHECKPOINT(i) (GP(g_track + 0x70) + (uint32_t)(i) * 0x14)
#define CHECKPOINT_AT(slot, field) ((((uint32_t *)GP(g_track + 0x90)))[(slot)[field]] & 0xff)

#define slot_lap_time GCALL(0x43d750, slot_lap_record)
#define slot_lap_bonus GCALL(0x467490, slot_lap_weapon)
#define hud_message GCALL(0x4600a0, net_kill_message)
#define slot_finish_plan GCALL(0x47fbe0, slot_finish_to_ai)
#define voice_stop GCALL(0x489f70, ff_device_stop_all)
#define race_state_a GCALL(0x45ef80, net_state_game)
#define race_state_b GCALL(0x45eed0, net_state_host)
#define race_state_c GCALL(0x45ef30, net_state_bit5)
#define race_results GCALL(0x439a70, race_results_update)
#define race_records GCALL(0x45fc40, net_finish_send)
#define replay_mark GCALL(0x41c160, replay_finish_mark)
#define replay_record_play GCALL(0x41ba20, replay_step)
#define race_end_fade GCALL(0x40afb0, race_end_fade_set)
#define race_end GCALL(0x40a9a0, race_end_signal)

/* 0x43e000: on a timing line, end the current lap time. */
void GAMEAPI slot_lap_line(uint8_t *slot)
{
    uint8_t mark;

    if (slot[0x2e] != slot[0x2c])
        return;
    I32(slot, 0x48) = I32(slot, 0x3c);
    if (slot[0x30] != 0) {
        mark = 0x4f;
    } else {
        if (slot[0x2c] == 0)
            return;
        mark = 0xbf;
        I32(slot, 0x5c) = (int32_t)race_clock();
    }
    (GP(slot))[0x65c] = mark;
    slot_time_digits(1, slot);
}

/* 0x43d460: a lap completed (or the start line crossed). */
void GAMEAPI slot_lap_done(uint8_t *slot)
{
    uint8_t *race = g_race, *car;
    uint8_t lap = slot[0x2c];
    uint32_t i;

    I32(slot, 0x34) = 0;
    if (lap < slot[0x2e]) {
        slot[0x2c] = lap + 1;
        return;
    }
    if (race[0x58c] == 1) {
        slot[0x2c] = lap + 1;
        slot[0x2e]++;
        return;
    }
    car = GP(slot);
    if (car[0x661] == 0 && lap != 0) {
        car[0x65d] = 0x6f;
        if (g_race[0xa86]) {
            slot_lap_bonus(slot);
            slot_lap_bonus(slot);
        }
    }
    (GP(slot))[0x661] = 0;
    if (slot[0x2c])
        slot_lap_time(slot);
    lap = ++slot[0x2c];
    slot[0x2e]++;
    race = g_race;
    if (race[0x551] == 4) {
        uint8_t *other;

        if (lap <= 5)
            return;
        slot[0x2c] = 5;
        slot[0x2e] = 5;
        /* (sic: slot 1 - car, not the car's own) */
        other = (uint8_t *)(uintptr_t)(0x6bfdf4 - U16(GP(slot), 0x64e) * CAR_SLOT_SIZE);
        if (other[0x2c] > 1) {
            other[0x2c]--;
            other[0x2e]--;
        }
        return;
    }
    if (race[0x551] == 1) {
        slot[0x2c] = 1;
        slot[0x2e] = 1;
        return;
    }
    if (lap > race[0x586]) {
        slot[0x2c] = race[0x586] + 1;
        if (I32(g_race, 0x57c) == 4)
            I32(g_race, 0x57c) = 5;
        race = g_race;
        if ((slot[0x259] & 1) == 0) {
            car = GP(slot);
            if (car[0x70c] != 8) {
                if (race[0x58c] == 0)
                    slot[0x25a] = 1;
                if (g_track[0x46] > 1) {
                    if (car[0x70c] != 8)
                        hud_message(U16(car, 0x64e), 0, 8);
                    slot_finish_plan(slot);
                    if (slot[0x268]) {
                        voice_stop(S16(slot, 0x26c));
                        slot[0x268] = 0;
                    }
                }
            }
            slot[0x259] |= 1;
            race = g_race;
        }
        for (i = 0; i < race[0x54d]; i++) {
            uint8_t *s = g_car_slots + i * CAR_SLOT_SIZE;

            if ((GP(s))[0x70c] != 8 && (s[0x259] & 1) == 0)
                goto check_last;
        }
        if (I32(race, 0x57c) == 5) {
            I32(g_race, 0x57c) = 6;
            I32(g_race, 0x580) = 0;
            if (g_race[0x58c] == 2) {
                I32(g_race, 0x57c) = 7;
                I32(g_race, 0x580) = 0;
            }
            if (race_state_a() == 1 && race_state_b() == 1) {
                if (race_state_c())
                    race_results(1);
                race_records();
            }
            for (i = 0; i < g_race[0x54d]; i++) {
                uint8_t *s = g_car_slots + i * CAR_SLOT_SIZE;

                replay_mark(GP(s));
                replay_record_play(GP(s));
                replay_record_play(GP(s));
            }
            race_end_fade(1);
            race_end();
            race = g_race;
        }
    }
check_last:
    if (slot[0x2c] == race[0x586])
        (GP(slot))[0x65d] = 0x6f;
}

/* 0x43d8e0: progress between the slot's checkpoints. With `probe` 1 only
 * says where the car is: 1 past the next one, 2 behind the previous one,
 * 3 between. Otherwise updates the lap position (+0x34) and moves on to
 * the next checkpoint (completing a lap at checkpoint 0) or back. */
int32_t GAMEAPI slot_checkpoints(uint8_t *slot, uint32_t probe)
{
    uint8_t *car = GP(slot), *next, *prev, *best = 0;
    uint32_t counter = slot[0x2f], wanted, next_index, prev_index, position;
    int32_t best_dist = 0x7fffffff, prev_dist, passed = 0, behind = 0;
    int32_t px, py, pz, d, dir[3], up[3], facing;

    if (probe == 0) {
        if (I32(car, 0x11c) < 0)
            return 0;
        wanted = car[0x11c];
    } else {
        wanted = 0xffffffff;
    }
    px = I32(car, 0x34) >> 16;
    py = I32(car, 0x38) >> 16;
    pz = I32(car, 0x3c) >> 16;
    next_index = CHECKPOINT_AT(slot, 0x30);
    next = CHECKPOINT(next_index);
    prev_index = CHECKPOINT_AT(slot, 0x31);
    prev = CHECKPOINT(prev_index);
    dir[1] = py - S16(next, 6);
    dir[2] = pz - S16(next, 8);
    d = (S16(next, 0xc) * (px - S16(next, 4)) >> 6) + (S16(next, 0xe) * dir[1] >> 6) +
        (S16(next, 0x10) * dir[2] >> 6);
    if (d >= 0 && (g_race[0x579] != 0xb || wanted == next[0])) {
        best_dist = 0;
        passed = 1;
        best = next;
    }
    if (abs32(d) < best_dist) {
        best_dist = abs32(d);
        slot[0x32] = (uint8_t)next_index;
        best = next;
    }
    d = (S16(prev, 0xc) * (px - S16(prev, 4)) >> 6) + (S16(prev, 0xe) * (py - S16(prev, 6)) >> 6) +
        (S16(prev, 0x10) * (pz - S16(prev, 8)) >> 6);
    if (d < 0) {
        d = 0;
        behind = 1;
    }
    prev_dist = d;
    slot[0x33] = (uint8_t)prev_index;
    if (probe == 1) {
        if (behind)
            return 2;
        return passed ? 1 : 3;
    }
    position = U16(prev, 2);
    {
        uint32_t at_best = U16(best, 2) < position ? 0x10000 : U16(best, 2);

        if (prev_dist + best_dist > 10)
            position = (uint32_t)fix_div16(mul16((int32_t)position, best_dist) + mul16((int32_t)at_best, prev_dist),
                                           prev_dist + best_dist);
    }
    if ((int32_t)position < 0)
        position = 0;
    else if ((int32_t)position > 0xffff)
        position = 0xffff;
    d = (int32_t)position - I32(slot, 0x34);
    if (d < -0x8000)
        d += 0x10000;
    else if (d > 0x7fff)
        d -= 0x10000;
    if (d < 0)
        I32(slot, 0x38) -= d;
    else if (d > 0)
        I32(slot, 0x38) = 0;
    dir[0] = 0; /* (the original's is unset when track_direction fails) */
    track_direction(dir, up, car);
    if (S16(GP(slot), 0x658) < 3)
        facing = mul16(dir[0], S16(car, 0xc8)) + mul16(dir[1], S16(car, 0xce)) + mul16(dir[2], S16(car, 0xd4));
    else
        facing = mul16(dir[0], I32(car, 0x68)) + mul16(dir[1], I32(car, 0x6c)) + mul16(dir[2], I32(car, 0x70));
    if (facing <= 0) {
        I32(slot, 0x38) = 0;
        counter = 0;
    } else {
        counter++;
        if ((int32_t)counter > 0xf0)
            counter -= 0x40;
    }
    slot[0x2f] = (uint8_t)counter;
    I32(slot, 0x34) = (int32_t)position;
    if (!passed) {
        if (behind == 1) {
            uint8_t back = slot[0x31];

            slot[0x30] = back;
            slot[0x32] = slot[0x33];
            if (back != 0) {
                slot[0x31] = back - 1;
                return 0;
            }
            slot[0x31] = g_track[0x44] - 1;
            if (slot[0x2c] != 0 && (slot[0x259] & 1) == 0) {
                I32(slot, 0x34) = 0xffff;
                slot[0x2c]--;
            }
        }
        return 0;
    }
    if (best[0x12] & 1)
        slot_lap_line(slot);
    slot[0x31] = slot[0x30];
    slot[0x33] = slot[0x32];
    slot[0x30]++;
    if (slot[0x30] >= g_track[0x44])
        slot[0x30] = 0;
    if (slot[0x31] == 0 && (slot[0x259] & 1) == 0)
        slot_lap_done(slot);
    return 0;
}

/* 0x43de60: find the slot's checkpoints afresh from the car's position
 * (after it was put back on the track). */
void GAMEAPI slot_checkpoints_reset(uint8_t *slot)
{
    uint8_t *car = GP(slot), *nearest = 0;
    int32_t px = I32(car, 0x34) >> 16, py = I32(car, 0x38) >> 16, pz = I32(car, 0x3c) >> 16;
    int32_t best = 0x7fffffff, i, rows = g_track[0x44];
    const uint32_t *row = ((uint32_t *)GP(g_track + 0x90));

    for (; rows > 0; rows--, row++) {
        uint32_t packed = *row;
        int k;

        for (k = 0; k < 4; k++, packed >>= 8) {
            uint32_t index = packed & 0xff;
            uint8_t *c;
            int32_t dx, dy, dz, d;

            if (index == 0xff)
                continue;
            c = CHECKPOINT(index);
            dx = px - S16(c, 4);
            dy = py - S16(c, 6);
            dz = pz - S16(c, 8);
            d = dx * dx + dy * dy + dz * dz;
            if (d < best) {
                best = d;
                nearest = c;
            }
        }
    }
    slot[0x30] = nearest[1];
    slot[0x31] = nearest[1];
    if (nearest[1])
        slot[0x31] = nearest[1] - 1;
    else
        slot[0x31] = g_track[0x44] - 1;
    for (i = 1; i <= (int32_t)g_track[0x44] + 10; i++) {
        int32_t where = slot_checkpoints(slot, 1);

        if (where == 1) {
            slot[0x31] = slot[0x30];
            slot[0x30]++;
            if (slot[0x30] >= g_track[0x44])
                slot[0x30] = 0;
        } else if (where == 2) {
            slot[0x30] = slot[0x31];
            if (slot[0x30])
                slot[0x31] = slot[0x30] - 1;
            else
                slot[0x31] = g_track[0x44] - 1;
        } else {
            break;
        }
    }
    slot[0x2f] = 0;
    slot_checkpoints(slot, 0);
}

/* --- putting a fallen body back --- */

#define g_respawn_last_node (*(uint32_t *)0x4d11f0) /* an int16_t * */
#define RESPAWN_POSITION ((int32_t *)0x4d1800)
#define RESPAWN_DIRECTION ((int32_t *)0x4d3c90)
#define TEST_BODY ((uint8_t *)0x4d1810)   /* scratch copy of the body */
#define TEST_SLOTS ((uint8_t *)0x4d1950)  /* its slots */
#define g_copy_to (*(uint32_t *)0x4d17f8) /* the copy loops' pointers, kept in game memory */
#define g_copy_from (*(uint32_t *)0x4d11f4)

/* 0x468d80: would the test body hit anything where it is? Its slots
 * against the track and objects (all but the main one, or only the main
 * one if `main_only`), then other bodies by bounding sphere. */
int32_t GAMEAPI respawn_blocked(uint8_t *test, const uint8_t *body, int32_t main_only)
{
    uint8_t *slots = g_slots;
    const uint8_t *index;
    uint32_t count, i;
    int32_t out[9];
    uint32_t type;

    out[6] = sweep_stale;

    if (main_only) {
        count = 1;
        index = test + 0x140 + (int8_t)test[0x10c];
    } else {
        count = (uint32_t)((int8_t)test[0x10c] - 1);
        index = test + 0x140;
    }
    for (; count != 0; count--, index++) {
        uint8_t *s = slots + *index * 0x48;

        slot_track_sweep(test, s, out, 0);
        if (out[7])
            return 1;
        slot_object_sweep(test, s, out, 0);
        if (out[7])
            return 1;
    }
    type = U32(test, 0) & 0x1ff;
    if (type == 4 || type == 0x16 || type == 0xc || type == 0x11)
        return 0;
    {
        int32_t radius = I32(slots + test[0x140 + (int8_t)test[0x10c]] * 0x48, 0x40);
        uint32_t *list = g_bodies;

        for (i = g_body_count; i != 0; i--, list++) {
            const uint8_t *other = (const uint8_t *)(uintptr_t)*list;
            uint32_t t, dx, dy, dz, r;

            if (other == body)
                continue;
            t = U32(other, 0) & 0x1ff;
            if (t == 4 || t == 0x16 || t == 0xc || t == 0x11)
                continue;
            dx = (U32(test, 0x34) - U32(other, 0x34)) >> 8;
            dy = (U32(test, 0x38) - U32(other, 0x38)) >> 8;
            dz = (U32(test, 0x3c) - U32(other, 0x3c)) >> 8;
            if (dz + dy + dx > 0x20000)
                continue;
            r = (uint32_t)(I32(slots + other[0x140 + (int8_t)other[0x10c]] * 0x48, 0x40) + radius);
            if ((dz + dy) * dz + dx * dx < r * r) /* (sic) */
                return 1;
        }
    }
    return 0;
}

/* 0x4687f0: try the body at `position` facing `q` (matrix to `matrix`):
 * on a copy, with copies of its slots, gather the track there and test
 * for anything in the way. 1 if it fits; `*key` gets the nearest track
 * corner (or -1). */
int32_t GAMEAPI respawn_try(uint8_t *body, const int32_t *position, const int32_t *q, int16_t *matrix, int32_t mode,
                            uint32_t *key)
{
    uint8_t *slots = g_slots, *main_slot;
    int32_t words = (U32(body, 0) & 0x1ff) == 3 ? 0x91d : 0x60, i, n, result = 1, off_track;
    uint8_t *cell;
    uint32_t *to, *from;

    if (q)
        quat_to_matrix(q, matrix);
    to = (uint32_t *)TEST_BODY;
    from = (uint32_t *)body;
    for (; words > 0; words--)
        *to++ = *from++;
    if (q) {
        from = (uint32_t *)matrix;
        to = (uint32_t *)(TEST_BODY + MATRIX);
        for (i = 0; i < 4; i++)
            *to++ = *from++;
        *(uint16_t *)to = *(uint16_t *)from;
        from = (uint32_t *)q;
        to = (uint32_t *)(TEST_BODY + ORIENTATION);
        for (i = 0; i < 4; i++)
            *to++ = *from++;
    }
    g_copy_to = (uint32_t)(uintptr_t)to;
    g_copy_from = (uint32_t)(uintptr_t)from;
    I32(TEST_BODY, 0x120) = 0x7fffffff;
    n = *(int32_t *)0x4d3c84 = (int8_t)body[0x10c];
    for (i = 0; i <= n; i++) {
        uint32_t fresh, k;

        from = (uint32_t *)(slots + body[0x140 + i] * 0x48);
        g_copy_from = (uint32_t)(uintptr_t)from;
        fresh = slot_take();
        TEST_SLOTS[i] = (uint8_t)fresh;
        to = (uint32_t *)(slots + (fresh & 0xff) * 0x48);
        for (k = 0; k < 0x12; k++)
            *to++ = *from++;
        g_copy_to = (uint32_t)(uintptr_t)to;
        g_copy_from = (uint32_t)(uintptr_t)from;
        n = *(int32_t *)0x4d3c84;
    }
    I32(TEST_BODY, 0x34) = position[0];
    I32(TEST_BODY, 0x38) = position[1];
    I32(TEST_BODY, 0x3c) = position[2];
    body_slots_at(TEST_BODY, I32(TEST_BODY, 0x118));
    U32(TEST_BODY, 0x110) = 0;
    main_slot = slots + TEST_SLOTS[(int8_t)TEST_BODY[0x10c]] * 0x48;
    *(uint32_t *)(TEST_BODY + 0x154) = 0x4d13f8;
    *key = 0xffffffff;
    I32(main_slot, 0xc) = I32(TEST_BODY, 0x38) >> 8;
    I32(main_slot, 8) = I32(TEST_BODY, 0x34) >> 8;
    I32(main_slot, 0x10) = I32(TEST_BODY, 0x3c) >> 8;
    I32(main_slot, 0x14) = 0;
    I32(main_slot, 0x18) = 0;
    I32(main_slot, 0x1c) = 0;
    if (mode == 2)
        I32(main_slot, 0x40) = 8000;
    U16(TEST_BODY, 0x15c) = 0;
    U16(TEST_BODY, 0x15e) = 0;
    {
        int32_t x = I32(main_slot, 8), y = I32(main_slot, 0xc), z = I32(main_slot, 0x10);

        cell = track_cell((int32_t)((uint32_t)x << 8), (int32_t)((uint32_t)y << 8), (int32_t)((uint32_t)z << 8),
                          &off_track);
        *(uint32_t *)0x4d3c88 = (uint32_t)(uintptr_t)cell;
        if (off_track == 0) {
            int16_t point[3] = {(int16_t)(x >> 8), (int16_t)(y >> 8), (int16_t)(z >> 8)};
            int32_t r = I32(main_slot, 0x40) >> 8, count;
            uint32_t patch_index = U32(cell, 4) >> 16 & 0x3fff;
            const uint16_t *patch = (const uint16_t *)(GP(g_track + 0x80) + patch_index * 6);

            r = r * r;
            for (count = (patch[1] >> 9) + 1; count > 0; count--, patch += 3)
                patch_gather(main_slot, patch, TEST_BODY, 0, point, r);
            *(uint32_t *)0x4d1968 = (uint32_t)(uintptr_t)(GP(TEST_BODY + 0x154) + U16(TEST_BODY, 0x15c) * 0x10);
            if ((U32(TEST_BODY, 0) & 0x1ff) != 4) {
                body_object_contacts(TEST_BODY, slots + TEST_SLOTS[(int8_t)TEST_BODY[0x10c]] * 0x48, 0);
                if (*(uint32_t *)(uintptr_t)(U32(TEST_BODY, 0xc) & ~1u) >> 19 & 7)
                    sweep_stale = 0;
            }
            *key = U32(TEST_BODY, 0x11c);
        } else {
            result = 0;
        }
    }
    if (respawn_blocked(TEST_BODY, body, mode))
        result = 0;
    for (i = (int8_t)TEST_BODY[0x10c]; i >= 0; i--)
        slot_give(TEST_SLOTS[i]);
    return result;
}

/* 0x468bb0: put the body down at `position` with `orientation` and
 * `matrix`, at rest; for a car, also its plan (line, node), camera and
 * lap progress. */
void GAMEAPI respawn_place(uint8_t *body, const int32_t *position, const int32_t *orientation, const int16_t *matrix,
                           uint8_t *line, uint32_t node, uint32_t key)
{
    int i;

    object_grid_unlink(body);
    I32(body, 0x38) = position[1];
    I32(body, 0x34) = position[0];
    I32(body, 0x3c) = position[2];
    I32(body, 0x60) = orientation[2];
    I32(body, 0x5c) = orientation[1];
    I32(body, 0x64) = orientation[3];
    I32(body, 0x58) = orientation[0];
    for (i = 0; i < 9; i++)
        S16(body, MATRIX + i * 2) = matrix[i];
    for (i = 0; i < 3; i++) {
        I32(body, 0x68 + i * 4) = 0;
        I32(body, 0x40 + i * 4) = 0;
        I32(body, 0xb8 + i * 4) = 0;
    }
    for (i = 0; i < 6; i++)
        I32(body, 0x74 + i * 4) = 0;
    for (i = 0; i < 3; i++)
        I32(body, 0x4c + i * 4) = 0;
    U32(body, 0x11c) = key;
    if ((U32(body, 0) & 0x1ff) == 3) {
        uint8_t *slot = SLOT_OF_CAR(body);

        for (i = 0; i < 4; i++)
            I32(body, 0x37c + i * 0xc4) = 0;
        I32(body, 0x640) = 0;
        *(uint32_t *)(slot + 0x64) = (uint32_t)(uintptr_t)line;
        U32(slot, 0x68) = node;
        if ((GP(slot))[0x70c] == 8)
            ai_rejoin_plan(slot);
        if (g_race[0x551] == 3 || g_race[0x551] == 8) {
            uint8_t *left = g_race + U16(GP(slot), 0x64e) + 8;

            if (*left)
                (*left)--;
        } else {
            int32_t before = I32(slot, 0x34), d;

            slot_checkpoints_reset(slot);
            d = I32(slot, 0x34) - before;
            if (d > 0x7fff)
                d -= 0x10000;
            else if (d < -0x8000)
                d += 0x10000;
            d += before;
            if (d < 0) {
                uint8_t lap = slot[0x2c];

                if (lap != 0 && lap <= g_race[0x586])
                    slot[0x2c] = lap - 1;
            } else if (d > 0x10000) {
                slot_lap_line(slot);
                slot_lap_done(slot);
            }
        }
        car_camera_cut(body);
    }
    object_grid_link(body);
}

/* 0x468450: once per step, put bodies that fell off the track (+0x128 is
 * 0x20) back on the nearest racing line node (or the next nearest if
 * that is blocked); pickups and dropped items are removed instead. A
 * camera body (type 4) that fell ends the pass. */
void GAMEAPI fallen_bodies_update(void)
{
    uint32_t *list;
    int32_t count;
    /* (sic: the nearest nodes are not reset between bodies) */
    int16_t *best = 0, *second = 0;
    const uint8_t *best_line = 0, *second_line = 0;
    int32_t best_index = -1, second_index = -1;
    int32_t best_dist = 0x7fffffff, second_dist = 0x7fffffff;
    int32_t q[4];
    uint32_t key;

    cars_check_fallen();
    count = g_body_count;
    list = g_bodies;
    if (count == 0)
        return;
    do {
        uint8_t *body = (uint8_t *)(uintptr_t)*list;

        if (U32(body, 0x128) == 0x20) {
            uint32_t type = U32(body, 0) & 0x1ff;

            if (type == 4) {
                camera_far(GP(body + 0x164));
                return;
            }
            if (type == 7 || type == 0x15) {
                dropped_fallen(GP(body + 0x164));
                list--;
                count--;
            } else if (type == 0x21) {
                pickup_fallen(body);
                list--;
                count--;
            } else {
                int32_t bx = I32(body, 0x34) >> 16, by = I32(body, 0x38) >> 16, bz = I32(body, 0x3c) >> 16;
                const uint8_t *line = GP(g_track + 0x60);
                int32_t lines = g_track[0x46] - 1, step;
                int16_t *next;

                for (; lines > 0; lines--) {
                    int16_t *node;
                    int32_t k, nodes;

                    line += 0xc;
                    node = ((int16_t *)GP(line + 8));
                    nodes = U16(line, 0);
                    for (k = 0; k < nodes; k++, node += 8) {
                        int32_t dx, dy, dz, d;

                        if ((uint32_t)(uintptr_t)node == g_respawn_last_node)
                            continue;
                        dx = node[0] - bx;
                        dy = node[1] - by;
                        dz = node[2] - bz;
                        d = dz * dz + dy * dy + dx * dx;
                        if (d < best_dist) {
                            second_dist = best_dist;
                            second_line = best_line;
                            second_index = best_index;
                            second = best;
                            best = node;
                            best_line = line;
                            best_index = k;
                            best_dist = d;
                        } else if (d < second_dist) {
                            second_line = line;
                            second = node;
                            second_dist = d;
                            second_index = k;
                        }
                    }
                }
                g_respawn_last_node = (uint32_t)(uintptr_t)best;
                RESPAWN_POSITION[0] = (int32_t)((uint32_t)best[0] << 16);
                RESPAWN_POSITION[1] = (int32_t)((uint32_t)best[1] << 16);
                RESPAWN_POSITION[2] = (int32_t)((uint32_t)best[2] << 16);
                if (best_index < (int32_t)U16(best_line, 0) - 1) {
                    step = 1;
                    next = best + 8;
                } else {
                    step = -1;
                    next = best - 8;
                }
                if (best_line[2])
                    step = -step;
                RESPAWN_DIRECTION[0] = (int32_t)((uint32_t)(next[0] - best[0]) << 8);
                RESPAWN_DIRECTION[1] = (int32_t)((uint32_t)(next[1] - best[1]) << 8);
                RESPAWN_DIRECTION[2] = (int32_t)((uint32_t)(next[2] - best[2]) << 8);
                if (step < 0) {
                    RESPAWN_DIRECTION[0] = -RESPAWN_DIRECTION[0];
                    RESPAWN_DIRECTION[1] = -RESPAWN_DIRECTION[1];
                    RESPAWN_DIRECTION[2] = -RESPAWN_DIRECTION[2];
                }
                quat_facing(q, RESPAWN_DIRECTION);
                if (respawn_try(body, RESPAWN_POSITION, q, (int16_t *)(body + MATRIX), 1, &key)) {
                    respawn_place(body, RESPAWN_POSITION, q, (int16_t *)(body + MATRIX), (uint8_t *)best_line,
                                  (uint32_t)best_index, key);
                } else {
                    RESPAWN_POSITION[0] = (int32_t)((uint32_t)second[0] << 16);
                    RESPAWN_POSITION[1] = (int32_t)((uint32_t)second[1] << 16);
                    g_respawn_last_node = (uint32_t)(uintptr_t)second;
                    RESPAWN_POSITION[2] = (int32_t)((uint32_t)second[2] << 16);
                    respawn_place(body, RESPAWN_POSITION, q, (int16_t *)(body + MATRIX), (uint8_t *)second_line,
                                  (uint32_t)second_index, key);
                }
                if ((U32(body, 0) & 0x1ff) == 3)
                    car_respawn_sparks(body, 0);
                sound_play(0x2b, 6, 0x294, 15000, 0);
            }
        }
        count--;
        list++;
    } while (count > 0);
}
