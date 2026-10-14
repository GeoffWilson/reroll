/* Weapons, pickups and their effects, once per step after the world step
 * (0x47ab40, called by sim_step):
 *
 *  - pickups (0x4671f0): the weapon pickups spin, a car without a full
 *    weapon slot that drives through one collects it, and the pickup
 *    respawns from the drop queue (0x425d70) after falling into place;
 *  - the projectiles (0x458710): the 24 weapon objects at 0x6b9ce0, by kind
 *    (+0x184): 0 leader (follows the racing lines to the leading car),
 *    1 homing missile, 2 driller;
 *  - the freezer ray along the track (0x43aba0), the time warp (0x479740)
 *    and the targeting of homing missiles (0x478470);
 *  - per car: the firing buttons (0x47b0e0 -> 0x467900, one function per
 *    weapon kind), the turbo boost timers (+0x690..+0x69c) and the shield
 *    ripple (0x46d950).
 *
 * Sound, network messages, the debug log and the time-warp palette are
 * still the original's, called by address. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define S8(p, off) (*(int8_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define PTR(p, off) GP((p) + (off))
#define PTR_SET(p, off, v) (*(uint32_t *)((p) + (off)) = (uint32_t)(uintptr_t)(v))
/* the "none" pointer (-1) some pointer fields hold */
#define NO_PTR ((uint8_t *)(uintptr_t)0xffffffffu)

#define g_race GP(0x4b1960)
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_seed (*(uint32_t *)0x4b11dc)
#define g_fx_seed (*(uint32_t *)0x4b11d8)
#define g_ai_seed (*(uint32_t *)0x4a7148)
#define g_ai_difficulty_mode (*(int32_t *)0x4a714c)
#define g_track GP(0x6c1e30)
#define g_markers GP(0x6c1eb0)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define CAR_COUNT (g_race[0x54d])
#define SLOT(i) (g_car_slots + (i) * CAR_SLOT_SIZE)
#define SLOT_CAR(slot) PTR(slot, 0)

/* sine and cosine (4096 = 1), 256 steps, interleaved in 16-byte rows */
#define g_sin_table ((const int16_t *)0x4b24f8)
#define g_cos_table ((const int16_t *)0x4b2578)

/* Pickups */
#define g_pickups(i) GP(GP(0x6b8ce0) + (i) * 4)
#define g_pickup_count (*(int32_t *)0x6b8ce4)
#define g_pickup_spots GP(0x6bfb28)  /* 0xc each: xyz, +6/+7 shape, +8, +10 */
#define g_pickup_shapes GP(0x6bfb34)
#define g_droppers ((uint8_t *)0x6cd420) /* pointers */
#define g_drop_queue ((int8_t *)0x6cd438)
#define g_drop_count (*(int16_t *)0x6cd450)
#define g_beams ((uint8_t *)0x6c8100) /* 4 x 0x18: +0 life, +8 from, +0x10 to */
#define g_new_shapes GP(0x6b9b80)
#define g_new_shape_count (*(uint16_t *)0x6b9b84)

/* Time warp */
#define g_warp_state (*(int32_t *)0x5d9b20)
#define g_warp_car GP(0x5d9b24)
#define g_warp_car_set(v) (*(uint32_t *)0x5d9b24 = (uint32_t)(uintptr_t)(v))
#define g_warp_time (*(int32_t *)0x5d9b28)
#define g_warp_scale (*(int32_t *)0x5d9b2c)
#define g_warp_palette_on (*(uint32_t *)0x4d57f0)

/* Shield ripple mesh */
#define g_ripple_mesh GP(0x4d44a4)
#define g_ripple_links GP(0x4d44a0)
#define g_ripple_height ((int16_t *)0x4d4420)
#define g_ripples ((uint8_t *)0x6002e0) /* 6 x 0x84 */
#define g_render_flags (*(uint8_t *)0x6ccd84)


static uint32_t random_step(uint32_t *seed)
{
    *seed = *seed * 16807 & 0x7fffffff;
    return *seed;
}

/* ((seed >> 15) * 0x7fff) >> 16: 0 .. 0x7ffe */
static uint32_t random15(uint32_t *seed)
{
    return (random_step(seed) >> 15) * 0x7fff >> 16;
}

/* ---- Small helpers ---------------------------------------------------- */

/* 0x42b8d0 */
void GAMEAPI debris_spawn(const int32_t *at, const int32_t *velocity, int32_t kind)
{
    GCALL(0x42b700, spark_spawn)(at, velocity, kind, 1);
}

/* 0x4906a0 */
void GAMEAPI car_hit_timer_tick(uint8_t *car)
{
    if (U8(car, 0x689))
        U8(car, 0x689)--;
}

/* 0x47d7e0 */
void GAMEAPI warp_palette_off(void)
{
    g_warp_palette_on = 0;
}

/* 0x467cc0: mark the weapon slot holding kind | 0x40 (else the second) as
 * spent (0x20). */
void GAMEAPI weapon_slot_spent(uint8_t *car, uint32_t kind)
{
    if ((int32_t)S8(car, 0x67c) == (int32_t)(kind | 0x40))
        U8(car, 0x67c) |= 0x20;
    else
        U8(car, 0x67d) |= 0x20;
}

/* 0x45ca20 */
uint32_t GAMEAPI slot_car_index(const uint8_t *slot)
{
    return U16(SLOT_CAR(slot), 0x64e);
}

/* 0x460f80: the body with this network id, or 0. */
uint8_t *GAMEAPI body_by_id(uint32_t id)
{
    uint32_t i = (uint16_t)id;
    uint8_t *body;

    if ((int32_t)i >= *(int32_t *)0x4d1088)
        return 0;
    body = GP(0x4d0880 + i * 4);
    if (U16(body, 0x10e) != i)
        return 0;
    return body;
}

/* 0x46e150 */
void GAMEAPI ripple_stop(uint8_t *ripple)
{
    ripple[0] = 0;
}

/* 0x46e0e0: a free shield ripple for the slot's car, or -1. */
uint8_t *GAMEAPI ripple_alloc(const uint8_t *slot)
{
    uint8_t *ripple = g_ripples;
    uint32_t i;

    for (i = 0; i < 6; i++, ripple += 0x84) {
        if (ripple[0] == 0) {
            PTR_SET(ripple, 0x64, SLOT_CAR(slot));
            PTR_SET(SLOT_CAR(slot), 0x6ac, ripple);
            return ripple;
        }
    }
    return NO_PTR;
}

/* 0x426040: queue a pickup spot to drop a new pickup. */
void GAMEAPI drop_queue_push(uint32_t spot)
{
    g_drop_queue[g_drop_count] = (int8_t)spot;
    g_drop_count++;
}

/* 0x45c010: add a body to the list of new collision shapes. */
void GAMEAPI shape_list_add(uint8_t *body)
{
    uint32_t *entry = (uint32_t *)(g_new_shapes + (g_new_shape_count & 0xffff) * 4);

    entry[0] = (uint32_t)(uintptr_t)body;
    entry[1] = 0;
    g_new_shape_count++;
    U32(body, 0x130) = 0;
}

/* 0x462e30: start a body on a scripted path (path index, then the drop's
 * spot). */
void GAMEAPI body_path_start(uint8_t *body, int32_t path, uint8_t spot)
{
    U32(body, 0x134) = (uint32_t)(uintptr_t)g_pickup_shapes + *(uint16_t *)(g_pickup_shapes + path * 2);
    U16(body, 0x138) = 0;
    U16(body, 0x13a) = 0;
    U16(body, 0x13c) = 0;
    U8(body, 0x13e) = 0;
    U8(body, 0x13f) = spot;
    U32(body, 0) |= 0x2000;
}

/* 0x4344e0: a free light beam, set up; its index, or -1. */
int32_t GAMEAPI beam_add(const int16_t *from, const int16_t *to, int16_t life)
{
    int32_t i;
    uint8_t *beam = g_beams;

    for (i = 0; i < 4; i++, beam += 0x18) {
        if (S16(beam, 0) == 0) {
            S16(beam, 0) = life;
            S16(beam, 8) = from[0];
            S16(beam, 0xa) = from[1];
            S16(beam, 0xc) = from[2];
            S16(beam, 0x10) = to[0];
            S16(beam, 0x12) = to[1];
            S16(beam, 0x14) = to[2];
            return i;
        }
    }
    return -1;
}

/* 0x434540 */
void GAMEAPI beam_move(const int16_t *from, const int16_t *to, int32_t index)
{
    uint8_t *beam;

    if (index < 0)
        return;
    beam = g_beams + index * 0x18;
    if (from) {
        S16(beam, 8) = from[0];
        S16(beam, 0xa) = from[1];
        S16(beam, 0xc) = from[2];
    }
    if (to) {
        S16(beam, 0x10) = to[0];
        S16(beam, 0x12) = to[1];
        S16(beam, 0x14) = to[2];
    }
}

/* 0x4345a0 */
void GAMEAPI beam_remove(int32_t index)
{
    if (index >= 0)
        S16(g_beams + index * 0x18, 0) = 0;
}

/* ---- Pickups ---------------------------------------------------------- */

/* 0x4682b0: the dropper has delivered: its pickup appears at the spot,
 * with a burst of debris. */
void GAMEAPI pickup_drop(uint8_t *dropper)
{
    int32_t spot = S16(dropper, 0x186);
    uint8_t *pickup = g_pickups(spot);
    const uint8_t *at = g_pickup_spots + spot * 0xc;
    int32_t pos[3], v[3];
    const int16_t *row;

    I32(pickup, 0x34) = (int32_t)((uint32_t)S16(at, 0) << 16);
    I32(pickup, 0x38) = (int32_t)((uint32_t)S16(at, 2) << 16);
    I32(pickup, 0x3c) = (int32_t)((uint32_t)S16(at, 4) << 16);
    U32(pickup, 0x11c) = 0;
    U16(pickup, 0x18c) = U8(at, 10);
    U16(pickup, 0x18e) = U16(at, 8);
    grid_link(pickup);
    U16(pickup, 0x182) = 0x83;
    U16(pickup, 0x180) = 0xc;
    GCALL(0x471c90, sound_play_body)(8, 6, 0x400, 12000, (uint32_t)(uintptr_t)VEC(pickup, 0x34));
    pos[0] = I32(pickup, 0x34);
    pos[1] = I32(pickup, 0x38);
    pos[2] = I32(pickup, 0x3c);
    for (row = (const int16_t *)0x4b26f8; (int32_t)(uintptr_t)row > 0x4b24f8; row -= 0x10) {
        uint32_t r = random_step(&g_fx_seed) >> 16;

        v[0] = (int32_t)(r & 0xf) * (row[0x40] >> 8);
        v[2] = (int32_t)(r >> 8 & 0xf) * (row[0] >> 8);
        v[1] = (int32_t)((uint32_t)(-8 - (int32_t)(r >> 4 & 0xf)) << 7);
        debris_spawn(pos, v, 0);
    }
}

/* 0x425d70: the pickup droppers (+0x180: 0 idle, 2 waiting to drop, 1
 * falling with a light beam to the spot, 3 leaving). */
void GAMEAPI pickup_droppers_update(void)
{
    uint8_t *next = g_droppers;
    uint32_t count;

    for (count = g_markers[0xf7]; count; count--, next += 4) {
        uint8_t *d = GP(next);

        switch (I32(d, 0x180)) {
        case 1: {
            int16_t t = S16(d, 0x184), from[3], to[3];
            const int16_t *spot;

            if (t <= 0)
                break;
            if (t == 10) {
                spot = (const int16_t *)(g_pickup_spots + S16(d, 0x186) * 0xc);
                from[0] = (int16_t)(I32(d, 0x34) >> 16);
                from[1] = (int16_t)(I32(d, 0x38) >> 16);
                from[2] = (int16_t)(I32(d, 0x3c) >> 16);
                to[0] = spot[0];
                to[1] = spot[1];
                to[2] = spot[2];
                S16(d, 0x188) = (int16_t)beam_add(from, to, 0xc);
            } else if (t < 10 && S16(d, 0x188) >= 0) {
                spot = (const int16_t *)(g_pickup_spots + S16(d, 0x186) * 0xc);
                from[0] = (int16_t)(I32(d, 0x34) >> 16);
                from[1] = (int16_t)(I32(d, 0x38) >> 16);
                from[2] = (int16_t)(I32(d, 0x3c) >> 16);
                to[0] = spot[0];
                to[1] = spot[1];
                to[2] = spot[2];
                beam_move(from, to, S16(d, 0x188));
            }
            S16(d, 0x184)--;
            if (S16(d, 0x184) == 0)
                pickup_drop(d);
            break;
        }
        case 2:
            if (S16(d, 0x184) > 0) {
                S16(d, 0x184)--;
            } else {
                const uint8_t *spot = g_pickup_spots + S16(d, 0x186) * 0xc;
                const int16_t *start;

                shape_list_add(d);
                body_path_start(d, U8(spot, 6), U8(spot, 7));
                start = (const int16_t *)(PTR(g_markers, 0xa8) + U8(d, 0x13f) * 8);
                I32(d, 0x34) = (int32_t)((uint32_t)start[0] << 16);
                I32(d, 0x38) = (int32_t)((uint32_t)start[1] << 16);
                I32(d, 0x3c) = (int32_t)((uint32_t)start[2] << 16);
                I32(d, 0x38) = (int32_t)((uint32_t)start[1] << 16) - 0x900000;
                grid_link(d);
                I32(d, 0x180) = 1;
                S16(d, 0x184) = 0;
            }
            break;
        case 3:
            grid_unlink(d);
            I32(d, 0x180) = 0;
            GCALL(0x45c040, body_list_remove)(d);
            break;
        default: {
            int32_t n = g_drop_count;
            uint32_t i;

            if (n == 0)
                break;
            I32(d, 0x180) = 2;
            S16(d, 0x186) = g_drop_queue[0];
            S16(d, 0x184) = (int16_t)(random15(&g_seed) & 0x1f);
            for (i = 0; i < (uint32_t)(n - 1); i++)
                g_drop_queue[i] = g_drop_queue[i + 1];
            g_drop_count--;
            break;
        }
        }
    }
}

/* 0x4671f0: the pickups (+0x182: 0x80 | 1 waiting, 2 taken, 3 appearing;
 * +0x180 their timer) spin together; a car with an empty weapon slot
 * close enough collects a waiting one. */
void GAMEAPI pickups_update(void)
{
    int16_t angles[3], m[16];
    int32_t i, k;

    if (g_pickup_count < 1)
        return;
    angles[0] = (int16_t)((g_race_ticks & 0x7f) << 5);
    angles[1] = (int16_t)((g_race_ticks & 0x1f) << 7);
    angles[2] = (int16_t)((g_race_ticks & 0x3f) << 6);
    matrix_from_angles(angles, m);
    for (i = 0; i < g_pickup_count; i++) {
        uint8_t *p = g_pickups(i);
        int32_t state = S16(p, 0x182);
        uint32_t cars;
        uint8_t *slot;
        int32_t px, py, pz;

        if (!(state & 0x80))
            continue;
        state &= ~0x80;
        if (state == 2) {
            if (S16(p, 0x180) > 0 && --S16(p, 0x180) == 0) {
                drop_queue_push((uint32_t)i);
                U16(p, 0x182) = 0x84;
            }
            continue;
        }
        if (state == 3) {
            if (S16(p, 0x180) > 0 && --S16(p, 0x180) == 0)
                U16(p, 0x182) = 0x81;
        } else if (state != 1) {
            continue;
        }
        for (k = 0; k < 9; k++)
            S16(p, 0xc4 + k * 2) = m[k];
        py = I32(p, 0x38) >> 16;
        px = I32(p, 0x34) >> 16;
        pz = I32(p, 0x3c) >> 16;
        slot = g_car_slots;
        for (cars = CAR_COUNT; (int32_t)cars > 0; cars--, slot += CAR_SLOT_SIZE) {
            uint8_t *car = SLOT_CAR(slot);
            int32_t dx, dy, dz, k;

            if (S8(car, 0x67c) != -1 && S8(car, 0x67d) != -1)
                continue;
            dx = px - (I32(car, 0x34) >> 16);
            if (abs32(dx) > 0x100)
                continue;
            dz = pz - (I32(car, 0x3c) >> 16);
            if (abs32(dz) > 0x100)
                continue;
            dy = py - (I32(car, 0x38) >> 16);
            if (abs32(dy) > 0x100)
                continue;
            if ((uint32_t)(dy * dy + dz * dz + dx * dx) >= 0x1440) {
                k = I32(car, 0x68) >> 17;
                dx += k;
                dy += k;
                dz += k;
                if ((uint32_t)(dy * dy + dz * dz + dx * dx) >= 0x1440)
                    continue;
            }
            GCALL(0x471d50, sound_play_flagged)(9, 6, 0x800, 14000, (uint32_t)(uintptr_t)VEC(car, 0x34),
                                                U8(car, 0x6a4));
            grid_unlink(p);
            U16(p, 0x180) = 100;
            U16(p, 0x182) = 0x82;
            GCALL(0x467490, slot_lap_weapon)(slot);
            break;
        }
    }
    pickup_droppers_update();
}

/* ---- Turbo boost, time warp, shield ----------------------------------- */

/* 0x47aa60: start a turbo boost (+0x690 1, +0x692 its time, +0x69c the
 * target speed boost, one up from the current +0x698, 10..12); a second
 * one while one runs (if it has run no longer than `wait` + 1) gives the
 * flash; `flash` > 0 starts it at time 14 with a flash of 20 - 2 * flash.
 * 0 if a boost is already well under way. */
int32_t GAMEAPI boost_start(uint8_t *slot, int32_t wait, int32_t flash)
{
    uint8_t *car = SLOT_CAR(slot);
    int32_t again = 0, level;

    if (S16(car, 0x690) != 0) {
        if ((int32_t)S16(car, 0x692) > wait + 1)
            return 0;
        again = 1;
    }
    level = I32(car, 0x698) >> 16;
    S16(car, 0x690) = 1;
    S16(car, 0x692) = (int16_t)wait;
    if (level < 9) {
        level = 10;
    } else {
        level++;
        if (level > 12)
            level = 12;
    }
    if (again)
        GCALL(0x47a860, car_kick_forward)(car, 0x3c);
    if (flash) {
        S16(car, 0x692) = 0xe;
        U8(car, 0x697) = U8(SLOT_CAR(slot), 0x6f9);
        GCALL(0x47a860, car_kick_forward)(car, 0x14 - flash * 2);
    }
    I32(car, 0x69c) = (int32_t)((uint32_t)level << 16);
    GCALL(0x471d50, sound_play_flagged)(0x16, 6, 0x4b0, 15000, (uint32_t)(uintptr_t)VEC(car, 0x34), U8(car, 0x6a4));
    return 1;
}

/* 0x479890: start the time warp (everyone but the slot's car slowed); if
 * another car's warp is running it ends that one instead (2). `delay`
 * under 10 fades in over 10 - delay steps. */
int32_t GAMEAPI time_warp_start(const uint8_t *slot, int32_t delay)
{
    if (g_warp_state != 0 && g_warp_state != 4) {
        if (g_warp_car != SLOT_CAR(slot)) {
            g_warp_state = 4;
            g_warp_time = 0x14;
            return 2;
        }
        return 0;
    }
    g_warp_car_set(SLOT_CAR(slot));
    if (delay < 10) {
        g_warp_state = 2;
        g_warp_scale = delay * -0x1333 + 0x10000;
        g_warp_time = 10 - delay;
        return 1;
    }
    g_warp_state = 1;
    g_warp_scale = 0x4002;
    g_warp_time = 0x6e - delay;
    return 1;
}

/* 0x424cb0: after a time warp, some AI cars (one in two, or one in four
 * in AI mode 1; none in mode 2) other than the warper go full throttle. */
void GAMEAPI warp_end_ai_burst(const uint8_t *car)
{
    uint32_t me = U16(car, 0x64e), i;
    uint8_t *slot = g_car_slots;

    if (g_ai_difficulty_mode == 2)
        return;
    for (i = 0; (int32_t)i < (int32_t)CAR_COUNT; i++, slot += CAR_SLOT_SIZE) {
        uint8_t *other;

        if (i == me)
            continue;
        other = SLOT_CAR(slot);
        if (other[0x70c] != 8)
            continue;
        if ((random_step(&g_ai_seed) >> 15 << (g_ai_difficulty_mode == 1 ? 2 : 1)) & 0xffff0000)
            continue;
        U16(other, 0x6b4) = 0x200;
        U16(other, 0x6b6) = 0;
    }
}

/* 0x479740: the time warp (state 2 fading in, 1 running, 4 fading out). */
void GAMEAPI time_warp_update(void)
{
    warp_palette_off();
    switch (g_warp_state) {
    case 1:
        g_warp_time--;
        if (g_warp_time == 0x19)
            weapon_slot_spent(g_warp_car, 5);
        GCALL(0x47d5a0, warp_texture_update)(0xff);
        if (g_warp_time == 0) {
            g_warp_state = 4;
            g_warp_time = 0x14;
        }
        break;
    case 2:
        g_warp_scale -= 0x1333;
        g_warp_time--;
        GCALL(0x47d5a0, warp_texture_update)((int32_t)((uint32_t)(0x9f6 - g_warp_time * 0xff) / 10));
        if (g_warp_time == 0) {
            g_warp_state = 1;
            g_warp_time = 100;
        }
        break;
    case 4:
        g_warp_scale += 0x999;
        g_warp_time--;
        GCALL(0x47d5a0, warp_texture_update)((int32_t)((uint32_t)(g_warp_time * 0xff) / 0x14));
        if (g_warp_time == 0) {
            GCALL(0x467c90, car_clear_item)(g_warp_car, 5);
            g_warp_state = 0;
            g_warp_scale = 0x10000;
            g_warp_time = 0;
            warp_end_ai_burst(g_warp_car);
        }
        break;
    }
}

/* 0x46d6e0: raise the shield (a ripple record at car +0x6ac). 0 if none
 * is free in a network game. */
int32_t GAMEAPI shield_start(const uint8_t *slot)
{
    uint8_t *ripple = PTR(SLOT_CAR(slot), 0x6ac), was;

    if (ripple == NO_PTR)
        ripple = ripple_alloc(slot);
    if (GCALL(0x45ef80, net_state_game)() == 1 && GCALL(0x45eed0, net_state_host)() == 1 && ripple == NO_PTR)
        return 0;
    was = ripple[0];
    ripple[1] = 10;
    ripple[3] = 0;
    if (was != 1)
        ripple[0] = 1;
    ripple[2] = GCALL(0x471d50, sound_play_flagged)(
        0x17, 7, 0x800, 0x3fff, (uint32_t)(uintptr_t)VEC(SLOT_CAR(slot), 0x34), U8(SLOT_CAR(slot), 0x6a4));
    return 1;
}

/* 0x46d950: the shield ripple: a spring mesh over the car (heights +4,
 * speeds +0x34, one byte per vertex; links from 0x4d44a0) and its
 * vertex colours (+0x70). +1 is the strength left, +3 a hold timer. */
void GAMEAPI shield_ripple_update(const uint8_t *slot)
{
    uint8_t *r = PTR(SLOT_CAR(slot), 0x6ac);
    uint32_t n = U16(g_ripple_mesh, 4), i;
    int8_t *height = (int8_t *)(r + 4), *speed = (int8_t *)(r + 0x34);
    int32_t strength = (int8_t)r[1], fade;
    uint32_t *colour;

    if ((int8_t)r[3] > 0) {
        r[3]--;
    } else if (strength == 0) {
        ripple_stop(r);
        return;
    }
    if ((int32_t)n > 0) {
        const uint8_t *links = g_ripple_links;

        for (i = 0; i < n; i++, links += 8) {
            int32_t h = height[i], pull = 0, v, k;

            for (k = 0; k < links[0]; k++)
                pull += height[links[1 + k]] - h;
            v = (pull >> 3) + speed[i];
            h += v;
            if (h < 0)
                h = 0;
            v *= 15;
            speed[i] = (int8_t)(v < 0 ? -(-v >> 4) : v >> 4);
            g_ripple_height[i] = (int16_t)h;
        }
    }
    fade = strength < 5 ? 0xff - (int32_t)((uint32_t)strength << 6) : 0;
    colour = (uint32_t *)GP(r + 0x70);
    for (i = 0; (int32_t)i < (int32_t)n; i++, colour++) {
        int32_t h = g_ripple_height[i], s, red, green, blue, top;
        uint32_t c, rnd;

        height[i] = (int8_t)h;
        if (h < 0)
            h = -h;
        h <<= 3;
        if (h > 0xff)
            h = 0xff;
        c = (uint32_t)h | ((uint32_t)h << 8 | (uint32_t)h) << 8;
        if ((int32_t)c > 0xff)
            c = 0xff;
        s = speed[i];
        if (s < 0)
            s = -s;
        s <<= 3;
        c |= ((uint32_t)s & ~1u) << 7 | (uint32_t)s;
        rnd = random_step(&g_fx_seed) >> 16;
        if (!(rnd & 1)) {
            int32_t x = (int32_t)(rnd & 0xff);

            if (fade > 0) {
                x -= fade;
                if (x < 0)
                    x = 0;
            }
            c |= (uint32_t)x << 17 | (rnd >> 8 & 0x7f);
        }
        red = (int32_t)(c & 0xff);
        green = (int32_t)c >> 8 & 0xff;
        blue = (int32_t)c >> 16 & 0xff;
        top = red > green ? red : green;
        if (top <= blue)
            top = blue;
        if (top >= 0xff)
            top = 0xff;
        s = 0xff - top;
        c = ((c & 0xff0000) + ((uint32_t)s << 16)) | (((uint32_t)s << 8) + (c & 0xff00)) | (uint32_t)(red + s);
        if (g_render_flags & 1) {
            *colour = c;
        } else {
            uint32_t b = (c >> 16 & 0xff) * (uint32_t)top >> 8 & 0xff;
            uint32_t g = (c >> 8 & 0xff) * (uint32_t)top >> 8 & 0xff;
            uint32_t rr = (c & 0xff) * (uint32_t)top >> 8 & 0xff;

            *colour = b << 16 | g << 8 | rr;
        }
        ((uint8_t *)colour)[3] = (uint8_t)top;
    }
}

/* ---- Per car ---------------------------------------------------------- */

/* 0x40eee0: the jump button: if the car has a wheel on the ground and
 * isn't too fast, start a hop (+0x2284). Not in some race modes. */
void GAMEAPI car_hop_start(uint8_t *car)
{
    int32_t i;

    if (g_race[0x585] || g_race[0x551] == 3 || g_race[0x551] == 8)
        return;
    if (I32(car, 0x2284) != 0 || I32(car, 0x654) > 0x200000)
        return;
    for (i = 0; i < 4; i++)
        if (U8(car, 0x324 + i * 0xc4) & 1)
            break;
    if (i == 4)
        return;
    I32(car, 0x2284) = 1;
    I32(car, 0x2288) = 0;
}

int32_t GAMEAPI weapon_fire(uint8_t *slot, int32_t which, int32_t local, uint8_t *unused, uint8_t *data);

/* 0x47b0e0: fire buttons (front 4, rear 8 at +0x704) and jump (0x20). */
void GAMEAPI car_fire_buttons(uint8_t *slot)
{
    uint32_t pressed = U32(SLOT_CAR(slot), 0x704), held = U32(SLOT_CAR(slot), 0x700);

    if (pressed & 4)
        weapon_fire(slot, 0, 1, 0, 0);
    if (pressed & 8)
        weapon_fire(slot, 1, 1, 0, 0);
    if (held & 0x20)
        car_hop_start(SLOT_CAR(slot));
}

/* 0x42a410: a player passing close to a kind-4 marker clears its flag. */
void GAMEAPI player_markers_pass(const uint8_t *slot)
{
    uint32_t count = g_markers[0xf6];
    const uint8_t *car = SLOT_CAR(slot);
    uint8_t *entry;

    if (car[0x70c] != 10 || (int32_t)(count - 1) < 0)
        return;
    entry = PTR(g_markers, 0xf8) + count * 8 - 8;
    for (; count; count--, entry -= 8) {
        const uint8_t *node;
        int16_t d[3];

        if (entry[4] != 4 || entry[5] != 1)
            continue;
        node = PTR(PTR(g_track, 0x60), U16(entry, 0) * 0xc + 8) + U16(entry, 2) * 0x10;
        d[0] = (int16_t)((I32(car, 0x34) - (int32_t)((uint32_t)S16(node, 0) << 16)) >> 16);
        d[1] = (int16_t)((I32(car, 0x38) - (int32_t)((uint32_t)S16(node, 2) << 16)) >> 16);
        d[2] = (int16_t)((I32(car, 0x3c) - (int32_t)((uint32_t)S16(node, 4) << 16)) >> 16);
        if (svec_length_sq(d) < 1000000)
            entry[5] = 0;
    }
}


/* ---- Projectiles ------------------------------------------------------ */

#define g_projectiles(i) GP(0x6b9ce0 + (i) * 4) /* 24 */
#define g_leader_count (*(uint8_t *)0x6b9d42)
#define g_homer_count (*(uint8_t *)0x6b9d40)
#define g_driller_count (*(uint8_t *)0x6b9d41)
#define g_sound_voices (*(int32_t *)0x4b1c8c)
#define g_gte_vector_words ((uint32_t *)0x6ccc64)
#define g_gte_result ((const int32_t *)0x6cccc8)
#define g_particle_cycle (*(uint8_t *)0x6cca52)
#define g_lines (PTR(g_track, 0x60))


/* 0x42b650: a puff of smoke at a point. */
void GAMEAPI smoke_puff(const int32_t *at)
{
    uint8_t *p = GCALL(0x42ac30, particle_take)();
    uint32_t n;

    U32(p, 0) = 8;
    U8(p, 0x95) = 0x30;
    U16(p, 0x96) = 0x1e;
    U16(p, 0xa2) = 0;
    I32(p, 0x88) = at[0];
    I32(p, 0x8c) = at[1];
    I32(p, 0x90) = at[2];
    U16(p, 0x9c) = 0;
    U16(p, 0x9e) = 0;
    U16(p, 0xa0) = 0;
    GCALL(0x42cd40, particle_fade)(p, 0x1e, 1);
    GCALL(0x460ff0, object_grid_link2_at)(p, I32(p, 0x88), I32(p, 0x8c), I32(p, 0x90));
    U8(p, 0x94) = 1;
    U32(p, 0xa4) = *(uint32_t *)0x6cca50;
    n = g_particle_cycle + 1u;
    g_particle_cycle = n < 0xc ? (uint8_t)n : 0;
}

/* 0x458a20: the projectile's exhaust trail: a ring of 32 quads (+0x1b8,
 * 0x20 each) behind it; `mode` 0 rewrites the last one, 1 adds one, 2 adds
 * one and moves the trail with the projectile. */
void GAMEAPI trail_update(uint8_t *obj, int32_t mode)
{
    int32_t slot = (int32_t)S16(obj, 0x23de) + (int32_t)S8(obj, 0x23dc), kind;
    int16_t centre[3], a[3], b[3], size, *quad;
    int32_t px, py, pz, i, n;

    if (mode == 0)
        slot--;
    slot %= 32;
    gte_load_matrix(obj + 0xc4);
    quad = (int16_t *)(obj + 0x1b8 + slot * 0x20);
    px = I32(obj, 0x34) >> 16;
    py = I32(obj, 0x38) >> 16;
    pz = I32(obj, 0x3c) >> 16;
    g_gte_vector_words[0] = 0;
    *(int16_t *)(g_gte_vector_words + 1) = (int16_t)((I32(obj, 0x184) != 0 ? 0x14 : 0) - 0x28);
    gte_rotate();
    centre[0] = (int16_t)g_gte_result[0];
    centre[1] = (int16_t)g_gte_result[1];
    centre[2] = (int16_t)g_gte_result[2];
    a[0] = 0;
    a[2] = 0;
    b[1] = 0;
    b[2] = 0;
    kind = I32(obj, 0x184);
    if (kind != 2) {
        if (mode != 2) {
            size = 0x18;
        } else {
            n = 0x18 - (int32_t)((uint32_t)I32(obj, 0x180) << 2);
            size = (int16_t)(n < 1 ? 1 : n);
        }
    } else {
        if (mode != 2) {
            size = 0xc;
        } else {
            n = 0xc - (int32_t)((uint32_t)I32(obj, 0x180) << 1);
            size = (int16_t)(n < 1 ? 1 : n);
        }
    }
    b[0] = size;
    a[1] = (int16_t)-size;
    if (kind == 0) {
        int32_t s = g_sin_table[(g_race_ticks & 0x1f) * 8] >> 6, c = g_cos_table[(g_race_ticks & 0x1f) * 8] >> 6;
        int32_t ay = a[1], bx = b[0], t1, t2, t3;

        t1 = fix_mul8(0, c);
        t2 = fix_mul8(ay, s);
        t3 = fix_mul8(ay, c);
        a[0] = (int16_t)(t1 + t2);
        a[1] = (int16_t)(fix_mul8(0, s) + t3);
        t1 = fix_mul8(bx, c);
        t2 = fix_mul8(0, s);
        t3 = fix_mul8(0, c);
        b[0] = (int16_t)(t1 + t2);
        b[1] = (int16_t)(fix_mul8(-bx, s) + t3);
    }
    ((int16_t *)g_gte_vector_words)[0] = a[0];
    ((int16_t *)g_gte_vector_words)[1] = a[1];
    ((int16_t *)g_gte_vector_words)[2] = a[2];
    gte_rotate();
    a[0] = (int16_t)g_gte_result[0];
    a[1] = (int16_t)g_gte_result[1];
    a[2] = (int16_t)g_gte_result[2];
    ((int16_t *)g_gte_vector_words)[0] = b[0];
    ((int16_t *)g_gte_vector_words)[1] = b[1];
    ((int16_t *)g_gte_vector_words)[2] = b[2];
    gte_rotate();
    b[0] = (int16_t)g_gte_result[0];
    b[1] = (int16_t)g_gte_result[1];
    b[2] = (int16_t)g_gte_result[2];
    if (I32(obj, 0x184) == 1 || I32(obj, 0x184) == 0) {
        uint8_t angle = (uint8_t)(U8(obj, 0x5b8) + 0x14);

        U8(obj, 0x5b8) = angle;
        U8(obj, 0x5b9 + slot) = (uint8_t)(g_sin_table[angle] >> 9);
    }
    px += centre[0];
    py += centre[1];
    pz += centre[2];
    quad[0] = (int16_t)(a[0] + px);
    quad[1] = (int16_t)(a[1] + py);
    quad[2] = (int16_t)(a[2] + pz);
    quad[4] = (int16_t)(b[0] + px);
    quad[5] = (int16_t)(b[1] + py);
    quad[6] = (int16_t)(b[2] + pz);
    quad[8] = (int16_t)(px - a[0]);
    quad[9] = (int16_t)(py - a[1]);
    quad[10] = (int16_t)(pz - a[2]);
    quad[12] = (int16_t)(px - b[0]);
    quad[13] = (int16_t)(py - b[1]);
    quad[14] = (int16_t)(pz - b[2]);
    if (mode != 0) {
        if (S16(obj, 0x23de) < 0x20) {
            S16(obj, 0x23de)++;
        } else if (++U8(obj, 0x23dc) == 0x20) {
            U8(obj, 0x23dc) = 0;
        }
    }
    if (mode == 2) {
        int16_t dx = (int16_t)((I32(obj, 0x68) >> 16) + (I32(obj, 0x1a0) >> 12));
        int16_t dy = (int16_t)((I32(obj, 0x6c) >> 16) + (I32(obj, 0x1a4) >> 12));
        int16_t dz = (int16_t)((I32(obj, 0x70) >> 16) + (I32(obj, 0x1a8) >> 12));
        int16_t *v = (int16_t *)(obj + 0x1b8 + (int32_t)S8(obj, 0x23dc) * 0x20);

        for (n = S16(obj, 0x23de) - 1; n > 0; n--) {
            for (i = 0; i < 4; i++, v += 4) {
                v[0] = (int16_t)(v[0] - dx);
                v[1] = (int16_t)(v[1] - dy);
                v[2] = (int16_t)(v[2] - dz);
            }
        }
    }
}

/* 0x459770: steer a homing missile at its target (+0x190, else the
 * point +0x194): momentum * (6 or 7) / 8 plus 40 * the unit direction;
 * within 10 units of the point it stops seeking (state 0x20). */
void GAMEAPI homer_steer(uint8_t *obj)
{
    const uint8_t *target = PTR(obj, 0x190);
    int32_t d[3], k;

    if (target) {
        I32(obj, 0x194) = I32(target, 0x34);
        I32(obj, 0x198) = I32(target, 0x38);
        I32(obj, 0x19c) = I32(target, 0x3c);
        if ((U32(target, 0) & 0x1ff) == 3)
            I32(obj, 0x198) += (int32_t)0xffe00000;
    }
    d[0] = I32(obj, 0x194) - I32(obj, 0x34);
    d[1] = I32(obj, 0x198) - I32(obj, 0x38);
    d[2] = I32(obj, 0x19c) - I32(obj, 0x3c);
    k = vec_normalise_shifted(d, d) >> 1;
    d[0] = (int32_t)((uint32_t)d[0] << 5);
    d[0] += d[0] >> 2;
    d[1] = (int32_t)((uint32_t)d[1] << 5);
    d[1] += d[1] >> 2;
    d[2] = (int32_t)((uint32_t)d[2] << 5);
    d[2] += d[2] >> 2;
    if (k < 5) {
        if (I32(obj, 0x188) == 2) {
            I32(obj, 0x180) = 0x10;
            I32(obj, 0x188) = 0x20;
        }
        k = 1;
    } else {
        k = 2;
    }
    k = (k + 5) << 13;
    I32(obj, 0x40) = mul16(I32(obj, 0x40), k);
    I32(obj, 0x44) = mul16(I32(obj, 0x44), k);
    I32(obj, 0x48) = mul16(I32(obj, 0x48), k) + d[2];
    I32(obj, 0x40) += d[0];
    I32(obj, 0x44) += d[1];
    I32(obj, 0x1a0) = I32(obj, 0x40);
    I32(obj, 0x1a4) = I32(obj, 0x44);
    I32(obj, 0x1a8) = I32(obj, 0x48);
}

/* 0x4598f0: a homing missile's launch: the push +0x1a0 fades in over the
 * countdown +0x180, turning by +0x1ac. */
void GAMEAPI homer_launch(uint8_t *obj)
{
    int32_t n = I32(obj, 0x180) - 1;
    int32_t x = I32(obj, 0x1a0), y = I32(obj, 0x1a4), z = I32(obj, 0x1a8);

    if (n > 0) {
        I32(obj, 0x40) += x >> (n & 31);
        I32(obj, 0x44) += y >> (n & 31);
        I32(obj, 0x48) += z >> (n & 31);
    } else {
        I32(obj, 0x40) += (int32_t)((uint32_t)x << (-n & 31));
        I32(obj, 0x44) += (int32_t)((uint32_t)y << (-n & 31));
        I32(obj, 0x48) += (int32_t)((uint32_t)z << (-n & 31));
    }
    if (I32(obj, 0x180) != 0) {
        I32(obj, 0x1a0) = S16(obj, 0x1ac) + x;
        I32(obj, 0x1a4) = S16(obj, 0x1ae) + y;
        I32(obj, 0x1a8) = S16(obj, 0x1b0) + z;
        GCALL(0x458f50, matrix_from_forward)(VEC(obj, 0x1a0), (int16_t *)(obj + 0xc4));
    }
}

/* 0x459610: a homing missile ends: explosion (unless mode > 2), then it
 * leaves the world and its trail fades (state 0x10). */
void GAMEAPI homer_end(uint8_t *obj, int32_t mode)
{
    int32_t at[3], push[3], size;

    trail_update(obj, 1);
    if (mode == 0)
        size = I32(obj, 0x184) == 0 ? 0xd1a6b : 0x51a6b;
    else if (mode == 1)
        size = 0x50a28;
    else if (mode == 2)
        size = 0x51a6b;
    else
        size = 0;
    if (size) {
        at[0] = I32(obj, 0x34);
        at[1] = I32(obj, 0x38) + 0x280000;
        at[2] = I32(obj, 0x3c);
        push[0] = 0;
        push[1] = (int32_t)0xffc00000;
        push[2] = 0;
        GCALL(0x42aa30, explosion_effects)(at, push, 3, size, 0xb);
    }
    grid_unlink(obj);
    GCALL(0x419300, body_retire)(obj);
    I32(obj, 0x180) = 0;
    I32(obj, 0x188) = 0x10;
    if ((int32_t)U8(obj, 0x23dd) < g_sound_voices) {
        GCALL(0x471f40, sound_voice_stop)(U8(obj, 0x23dd));
        U8(obj, 0x23dd) = 0x7f;
    }
}

/* 0x458810: a homing missile (+0x188: 1 launching, 2 seeking, 0x20
 * arriving, 4 / 8 exploding, 0x10 trail fading). */
void GAMEAPI homer_update(uint8_t *obj)
{
    uint8_t *owner;

    switch (I32(obj, 0x188)) {
    case 2:
    case 0x20:
        if (I32(obj, 0x188) == 2) {
            if (++I32(obj, 0x180) == 0x96)
                I32(obj, 0x188) = 8;
        } else if (--I32(obj, 0x180) == 0) {
            I32(obj, 0x188) = 8;
        }
        homer_steer(obj);
        GCALL(0x458f50, matrix_from_forward)(VEC(obj, 0x1a0), (int16_t *)(obj + 0xc4));
        trail_update(obj, (g_race_ticks & 1) == 0);
        return;
    case 4:
        homer_end(obj, 0);
        return;
    case 8:
        homer_end(obj, 1);
        return;
    case 0x10:
        if (!(g_race_ticks & 1)) {
            S16(obj, 0x23de)--;
            U8(obj, 0x23dc)++;
        }
        if (U8(obj, 0x23dc) == 0x20)
            U8(obj, 0x23dc) = 0;
        if (S16(obj, 0x23de) < 2) {
            I32(obj, 0x188) = 0;
            g_homer_count--;
        }
        return;
    default:
        I32(obj, 0x180)--;
        trail_update(obj, 2);
        homer_launch(obj);
        if (I32(obj, 0x180) != 0)
            return;
        owner = PTR(obj, 0x18c);
        U8(obj, 0x23dd) =
            GCALL(0x471e60, sound_moving_flagged)(1, 0x1e, 9, 0x800, 0x3fff, (uint32_t)(uintptr_t)VEC(obj, 0x34),
                                                  (uint32_t)(uintptr_t)VEC(obj, 0x68), U8(owner, 0x6a4));
        I32(obj, 0x188) = 2;
        smoke_puff(VEC(obj, 0x34));
        U32(obj, 0) &= ~0x1000u;
        owner = PTR(obj, 0x18c);
        GCALL(0x471d50, sound_play_flagged)(0x1b, 6, 0x800, 13000, (uint32_t)(uintptr_t)VEC(owner, 0x34),
                                            U8(owner, 0x6a4));
        owner = PTR(obj, 0x18c);
        if (S16(owner, 0x710) != 0)
            GCALL(0x41b4d0, pad_effect_request)(owner + 0x6f8, 200, 1, 0x32, 0, 10);
        return;
    }
}

/* ---- Leader (follows the racing lines to the car in front) ------------ */

/* +0x23fc / +0x2400: the line and node it last passed; +0x2404 / +0x2408
 * the next; +0x2402 its direction along the line (+1 / -1); +0x23ec the
 * line groups still to cross to reach the leader's (0xff ends). */
#define LEADER_LINE 0x23fc
#define LEADER_NODE 0x2400
#define LEADER_STEP 0x2402
#define LEADER_NEXT_LINE 0x2404
#define LEADER_NEXT_NODE 0x2408
#define LEADER_ROUTE 0x23ec

static const uint8_t *line_node(const uint8_t *line, int32_t node)
{
    return PTR(line, 8) + node * 0x10;
}

/* 0x43f360: bits set in a 64-bit mask. */
int32_t GAMEAPI bit_count64(uint32_t lo, uint32_t hi)
{
    int32_t n = 0;

    for (; lo; lo >>= 1)
        n += lo & 1;
    for (; hi; hi >>= 1)
        n += hi & 1;
    return n;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* 0x43f0b0: aim at the race leader (the slot with +0x2d == 1). Same line
 * group as its plan: the plan's node (slot +0x68). Otherwise a depth-
 * first search, deepening one step at a time up to 7, over the groups'
 * neighbour masks (track +0x94, 64 bits each) for the shortest chain of
 * groups to the leader's, stored at +0x23ec; -1.
 *
 * The search keeps its state in one block laid out as the original's
 * stack frame (f = its esp), as its arrays run into each other: per depth
 * d, tried count f+0x24, group f+0x2c, neighbour count f+0x3c and the
 * neighbour mask f+0x4c (the start group's own bit sits at f+0x44, depth
 * -1); visited groups f+0x34. */
int32_t GAMEAPI leader_route(uint8_t *obj)
{
    uint8_t f[0x90];
    const uint8_t *table = PTR(g_track, 0x94), *slot;
    uint32_t n = CAR_COUNT, li, g0, gt, g, m, word;
    int32_t d, limit;

    for (li = 0; (int32_t)li < (int32_t)n; li++)
        if (U8(SLOT(li), 0x2d) == 1)
            break;
    slot = SLOT(li);
    PTR_SET(obj, 0x190, SLOT_CAR(slot));
    g0 = U8(PTR(obj, LEADER_LINE), 5);
    gt = U8(PTR(slot, 0x64), 5);
    if (g0 == gt)
        return I32(slot, 0x68);
    U16(obj, 0x240a) = (uint16_t)gt;
    wr32(f + 0x44, 0);
    wr32(f + 0x48, 0);
    wr32(f + 0x34, 0);
    wr32(f + 0x38, 0);
    limit = 0;
    f[0x3c] = 1;
    f[0x24] = 0;
    wr32(f + 0x44 + (g0 >> 5) * 4, 1u << (g0 & 31));
    g = g0;
    d = 0;
    for (;;) {
        /* descend from group g at depth d to the depth limit along each
         * first neighbour */
        f[0x2c + d] = (uint8_t)g;
        wr32(f + 0x4c + d * 8, rd32(table + g * 8));
        wr32(f + 0x50 + d * 8, rd32(table + g * 8 + 4));
        while (d < limit) {
            uint32_t lo = rd32(f + 0x4c + d * 8), hi = rd32(f + 0x50 + d * 8);

            d++;
            f[0x3c + d] = (uint8_t)bit_count64(lo, hi);
            g = 0;
            m = lo;
            while (!(m & 1)) {
                g++;
                m >>= 1;
                if (g == 32)
                    m = hi;
            }
            f[0x2c + d] = (uint8_t)g;
            wr32(f + 0x4c + d * 8, rd32(table + g * 8));
            wr32(f + 0x50 + d * 8, rd32(table + g * 8 + 4));
        }
        if (rd32(f + 0x4c + (gt >> 5) * 4 + d * 8) & 1u << (gt & 31)) {
            uint8_t *route = obj + LEADER_ROUTE;
            int32_t i;

            if (limit > 7)
                return -1;
            for (i = 0; i < limit; i++)
                route[i] = f[0x2d + i];
            route[limit] = (uint8_t)gt;
            route[limit + 1] = 0xff;
            return -1;
        }
        word = (g >> 5) * 4;
        wr32(f + 0x34 + word, rd32(f + 0x34 + word) | 1u << (g & 31));
        /* back up to the next untried neighbour */
        for (;;) {
            uint8_t tried = (uint8_t)(f[0x24 + d] + 1);

            f[0x24 + d] = tried;
            if (tried >= f[0x3c + d]) {
                if (--d < 0)
                    break;
                continue;
            }
            g = f[0x2c + d];
            m = rd32(f + 0x44 + ((g >> 5) + d * 2) * 4) >> (g & 31) & ~1u;
            do {
                g++;
                m >>= 1;
                if (g == 32)
                    m = rd32(f + 0x48 + d * 8);
            } while (!(m & 1));
            if (!(rd32(f + 0x34 + (g >> 5) * 4) & 1u << (g & 31)))
                break;
            f[0x2c + d] = (uint8_t)g;
        }
        if (d >= 0)
            continue;
        if (++limit > 7)
            return -1;
        for (d = 0; d <= limit; d++)
            f[0x24 + d] = 0;
        g = g0;
        d = 0;
    }
}

/* 0x43f9c0: from a position to a node, nudged across the line by the
 * node's 3-bit side offsets (+8, +0xc) times 7/8 or (mode != 1) 5/8. */
void GAMEAPI leader_node_offset(const uint8_t *node, const int32_t *pos, int32_t *out, int32_t mode)
{
    uint32_t a = U32(node, 8), b = U32(node, 0xc);
    int32_t k = mode == 1 ? 0xe0 : 0xa0;

    out[0] = S16(node, 0) - (pos[0] >> 16);
    out[1] = S16(node, 2) - (pos[1] >> 16);
    out[2] = S16(node, 4) - (pos[2] >> 16);
    out[0] += (int8_t)(uint8_t)(a >> 20 << 5) * k >> 8;
    out[1] += (int8_t)(uint8_t)(a >> 23 << 5) * k >> 8;
    out[2] += (int8_t)(uint8_t)(b >> 28 << 5) * k >> 8;
}

/* 0x43f790: steer: momentum * (drag) plus 24 * the unit direction;
 * through a node the drag depends on its speed and how far the line
 * bends; `seek` (homing in) halves it and quadruples the push. */
void GAMEAPI leader_push(uint8_t *obj, int32_t *dir, const uint8_t *node, int32_t seek)
{
    int32_t k = 0x10000, drag;

    vec_normalise_shifted(dir, dir);
    if (node) {
        int32_t speed = U8(node, 7) + 0x14, a[3], b[3], dot;
        const uint8_t *last = line_node(PTR(obj, LEADER_LINE), S16(obj, LEADER_NODE));

        if (speed > 0x100)
            speed = 0x100;
        else if (speed < 0xc8)
            speed = 0xc8;
        speed = (speed + 0x100) << 7;
        a[1] = (int16_t)(U32(node, 8) >> 23 << 13);
        a[0] = (int16_t)(U32(node, 8) >> 20 << 13);
        a[2] = (int16_t)(U32(node, 0xc) >> 28 << 13);
        b[0] = (int16_t)(U32(last, 8) >> 20 << 13);
        b[1] = (int16_t)(U32(last, 8) >> 23 << 13);
        b[2] = (int16_t)(U32(last, 0xc) >> 28 << 13);
        vec_normalise(a);
        vec_normalise(b);
        dot = (b[2] >> 8) * (a[2] >> 8) + (b[1] >> 8) * (a[1] >> 8) + (b[0] >> 8) * (a[0] >> 8);
        if (dot < 0)
            dot = -dot;
        if (dot < 0x3c18)
            dot = 0x4000;
        else if (dot > 0x10000)
            dot = 0x10000;
        k = mul16((dot + 0x10000) >> 1, speed);
    }
    if (seek == 0) {
        drag = (int32_t)((uint32_t)k * 7) >> 3;
    } else {
        drag = k >> 1;
        dir[0] = (int32_t)((uint32_t)dir[0] << 2);
        dir[1] = (int32_t)((uint32_t)dir[1] << 2);
        dir[2] = (int32_t)((uint32_t)dir[2] << 2);
    }
    I32(obj, 0x40) = mul16(I32(obj, 0x40), drag);
    I32(obj, 0x44) = mul16(I32(obj, 0x44), drag);
    I32(obj, 0x48) = mul16(I32(obj, 0x48), drag);
    I32(obj, 0x40) += dir[0] * 24;
    I32(obj, 0x48) += dir[2] * 24;
    I32(obj, 0x44) += dir[1] * 24;
    I32(obj, 0x1a0) = I32(obj, 0x40) >> 8;
    I32(obj, 0x1a4) = I32(obj, 0x44) >> 8;
    I32(obj, 0x1a8) = I32(obj, 0x48) >> 8;
    GCALL(0x458f50, matrix_from_forward)(VEC(obj, 0x1a0), (int16_t *)(obj + 0xc4));
}

/* 0x43f390: step along the lines: once nearer the next node than the last
 * one passed, move on (taking the link that leads into the next group of
 * the route), turning back at the ends of open lines. 1 if `check` and
 * the target is within 0x400. */
int32_t GAMEAPI leader_advance(uint8_t *obj, int32_t check)
{
    const uint8_t *target = PTR(obj, 0x190) + 0x34;
    int32_t px = I32(obj, 0x34) >> 16, py = I32(obj, 0x38) >> 16, pz = I32(obj, 0x3c) >> 16;
    int32_t turned = 0, r, step, dn, dc, dx, dy, dz;
    const uint8_t *next, *last, *old, *line, *node;
    uint8_t *route = obj + LEADER_ROUTE;
    uint32_t want, link;

    if (check) {
        dx = px - (I32(target, 0) >> 16);
        dy = py - (I32(target, 4) >> 16);
        dz = pz - (I32(target, 8) >> 16);
        if (dz * dz + dy * dy + dx * dx < 0x100000)
            return 1;
    }
    next = line_node(PTR(obj, LEADER_NEXT_LINE), S16(obj, LEADER_NEXT_NODE));
    dx = px - S16(next, 0);
    dy = py - S16(next, 2);
    dz = pz - S16(next, 4);
    dn = dz * dz + dy * dy + dx * dx;
    old = PTR(obj, LEADER_LINE);
    last = line_node(old, S16(obj, LEADER_NODE));
    dx = px - S16(last, 0);
    dy = py - S16(last, 2);
    dz = pz - S16(last, 4);
    dc = dz * dz + dy * dy + dx * dx;
    if (dn > dc)
        return 0;
    S16(obj, LEADER_NODE) = S16(obj, LEADER_NEXT_NODE);
    PTR_SET(obj, LEADER_LINE, PTR(obj, LEADER_NEXT_LINE));
    if (old != PTR(obj, LEADER_NEXT_LINE) && old[2] != PTR(obj, LEADER_NEXT_LINE)[2])
        S16(obj, LEADER_STEP) = (int16_t)-S16(obj, LEADER_STEP);
    r = leader_route(obj);
    want = route[0];
    if (r > -1) {
        if (check && U8(PTR(obj, LEADER_LINE), 4) == 0) {
            int32_t dir = r >= S16(obj, LEADER_NODE) ? 1 : -1;

            if (S16(obj, LEADER_STEP) != dir)
                turned = 1;
            S16(obj, LEADER_STEP) = (int16_t)dir;
        }
    } else {
        node = line_node(PTR(obj, LEADER_NEXT_LINE), S16(obj, LEADER_NEXT_NODE));
        link = U32(node, 0xc);
        if ((link & 0x1f) == want) {
            PTR_SET(obj, LEADER_NEXT_LINE, g_lines + (link & 0x1f) * 0xc);
            S16(obj, LEADER_NEXT_NODE) = (int16_t)(U32(node, 0xc) >> 5 & 0x1ff);
        } else if ((link >> 14 & 0x1f) == want) {
            PTR_SET(obj, LEADER_NEXT_LINE, g_lines + (link >> 14 & 0x1f) * 0xc);
            S16(obj, LEADER_NEXT_NODE) = (int16_t)(U32(node, 0xc) >> 19 & 0x1ff);
        } else {
            goto move;
        }
        while (route[0] != 0xff) {
            route[0] = route[1];
            route++;
        }
    }
move:
    line = PTR(obj, LEADER_NEXT_LINE);
    step = S16(obj, LEADER_STEP);
    S16(obj, LEADER_NEXT_NODE) += (int16_t)(line[2] != PTR(obj, LEADER_LINE)[2] ? -step : step);
    if (line[4] != 0) {
        int16_t at = S16(obj, LEADER_NEXT_NODE);

        if (at < 0)
            S16(obj, LEADER_NEXT_NODE) = (int16_t)(at + U16(line, 0));
        else if ((int32_t)at >= (int32_t)U16(line, 0))
            S16(obj, LEADER_NEXT_NODE) = 0;
        return 0;
    }
    if (turned) {
        S16(obj, LEADER_STEP) = (int16_t)-step;
        S16(obj, LEADER_NEXT_NODE) += (int16_t)(-step * 2);
    }
    if (S16(obj, LEADER_NEXT_NODE) <= 0) {
        S16(obj, LEADER_NEXT_NODE) = 0;
    } else if ((int32_t)S16(obj, LEADER_NEXT_NODE) >= (int32_t)U16(line, 0) - 1) {
        S16(obj, LEADER_NEXT_NODE) = (int16_t)(U16(line, 0) - 1);
    } else if (!turned) {
        return 0;
    }
    node = line_node(line, S16(obj, LEADER_NEXT_NODE));
    link = U32(node, 0xc);
    if (link & 0x7c000) {
        PTR_SET(obj, LEADER_NEXT_LINE, g_lines + (link >> 14 & 0x1f) * 0xc);
        S16(obj, LEADER_NEXT_NODE) = (int16_t)(U32(node, 0xc) >> 19 & 0x1ff);
    }
    if (U8(PTR(obj, LEADER_NEXT_LINE), 4) == 0 && (U32(node, 0xc) & 0x1f) != 0) {
        PTR_SET(obj, LEADER_NEXT_LINE, g_lines + (U32(node, 0xc) & 0x1f) * 0xc);
        S16(obj, LEADER_NEXT_NODE) = (int16_t)(U32(node, 0xc) >> 5 & 0x1ff);
    }
    if (PTR(obj, LEADER_NEXT_LINE)[5] != PTR(obj, LEADER_LINE)[5])
        return 0;
    step = S16(obj, LEADER_STEP);
    S16(obj, LEADER_NEXT_NODE) = (int16_t)(S16(obj, LEADER_NODE) + step);
    if (S16(obj, LEADER_NEXT_NODE) < 0) {
        S16(obj, LEADER_NEXT_NODE) = 1;
        S16(obj, LEADER_STEP) = (int16_t)-step;
        return 0;
    }
    if ((int32_t)S16(obj, LEADER_NEXT_NODE) >= (int32_t)U16(PTR(obj, LEADER_LINE), 0)) {
        S16(obj, LEADER_NEXT_NODE) = (int16_t)(U16(PTR(obj, LEADER_LINE), 0) - 2);
        S16(obj, LEADER_STEP) = (int16_t)-step;
    }
    return 0;
}

/* 0x43ebc0: the leader missile (+0x188: 1 launching, 2 following the
 * lines, 0x80 / 0x100 turning back along them, 0x40 homing in on the car
 * +0x190, 0x10 trail fading). */
void GAMEAPI leader_update(uint8_t *obj)
{
    uint8_t *target = PTR(obj, 0x190) + 0x34, *owner;
    int32_t even = (g_race_ticks & 1) == 0, d[3], dist, dot, k;
    const uint8_t *node;

    switch (I32(obj, 0x188)) {
    case 0x10:
        if (!(g_race_ticks & 1)) {
            S16(obj, 0x23de)--;
            U8(obj, 0x23dc)++;
        }
        if (U8(obj, 0x23dc) == 0x20)
            U8(obj, 0x23dc) = 0;
        if (S16(obj, 0x23de) < 2) {
            I32(obj, 0x188) = 0;
            g_leader_count--;
        }
        return;
    case 2:
        trail_update(obj, even);
        node = line_node(PTR(obj, LEADER_NEXT_LINE), S16(obj, LEADER_NEXT_NODE));
        leader_node_offset(node, VEC(obj, 0x34), d, 1);
        leader_push(obj, d, node, 0);
        if (leader_advance(obj, 1)) {
            I32(obj, 0x180) = 0;
            I32(obj, 0x188) = 0x80;
        }
        return;
    case 1:
        trail_update(obj, 2);
        if (--I32(obj, 0x180) != 0)
            return;
        owner = PTR(obj, 0x18c);
        U8(obj, 0x23dd) =
            GCALL(0x471e60, sound_moving_flagged)(1, 0x3c, 9, 0x400, 0x3fff, (uint32_t)(uintptr_t)VEC(obj, 0x34),
                                                  (uint32_t)(uintptr_t)VEC(obj, 0x68), U8(owner, 0x6a4));
        owner = PTR(obj, 0x18c);
        GCALL(0x471d50, sound_play_flagged)(0x1d, 6, 0x400, 15000, (uint32_t)(uintptr_t)VEC(owner, 0x34),
                                            U8(owner, 0x6a4));
        owner = PTR(obj, 0x18c);
        if (S16(owner, 0x710) != 0)
            GCALL(0x41b4d0, pad_effect_request)(owner + 0x6f8, 0xdc, 1, 0x3c, 0, 10);
        I32(obj, 0x188) = 2;
        smoke_puff(VEC(obj, 0x34));
        return;
    case 0x40:
        trail_update(obj, even);
        if (PTR(obj, 0x190) == PTR(obj, 0x18c))
            PTR_SET(obj, 0x18c, 0);
        d[0] = (I32(target, 0) - I32(obj, 0x34)) >> 16;
        d[1] = (I32(target, 4) - I32(obj, 0x38)) >> 16;
        d[2] = (I32(target, 8) - I32(obj, 0x3c)) >> 16;
        dist = (int32_t)fix_sqrt_table(d[2] * d[2] + d[1] * d[1] + d[0] * d[0]);
        dot = (I32(obj, 0x70) >> 16) * (I32(target, 0x3c) >> 16) + (I32(obj, 0x68) >> 16) * (I32(target, 0x34) >> 16) +
              (I32(obj, 0x6c) >> 16) * (I32(target, 0x38) >> 16);
        if (dot < -0x800)
            dot = -0x800;
        else if (dot > 0x800)
            dot = 0x800;
        if (g_warp_state == 0) {
            k = (dist - (dot >> 8)) >> 7;
            if (k < 0)
                k = 0;
            else if (k > 4)
                k = 4;
            d[0] += I32(target, 0x34) * k >> 16;
            d[1] += k * I32(target, 0x38) >> 16;
            d[2] += I32(target, 0x3c) * k >> 16;
        }
        dist += dot >> 3;
        if (dist < 1)
            dist = 1;
        leader_push(obj, d, 0, dist);
        return;
    case 0x80:
        trail_update(obj, even);
        node = line_node(PTR(obj, LEADER_NEXT_LINE), S16(obj, LEADER_NEXT_NODE));
        leader_node_offset(node, VEC(obj, 0x34), d, 1);
        leader_push(obj, d, node, 0);
        leader_advance(obj, 0);
        if (++I32(obj, 0x180) == 0x2d) {
            int32_t ax = (I32(obj, 0x34) - I32(target, 0)) >> 16;
            int32_t ay = (I32(obj, 0x38) - I32(target, 4)) >> 16;
            int32_t az = (I32(obj, 0x3c) - I32(target, 8)) >> 16;

            if ((I32(obj, 0x70) >> 16) * az + (I32(obj, 0x6c) >> 16) * ay + (I32(obj, 0x68) >> 16) * ax > 0 &&
                az * az + ax * ax + ay * ay > 0x100000) {
                I32(obj, 0x180) = 0;
                S16(obj, LEADER_STEP) = (int16_t)-S16(obj, LEADER_STEP);
                I32(obj, 0x188) = 0x100;
                return;
            }
            I32(obj, 0x180) = 0x23;
        }
        return;
    case 0x100:
        trail_update(obj, even);
        node = line_node(PTR(obj, LEADER_NEXT_LINE), S16(obj, LEADER_NEXT_NODE));
        leader_node_offset(node, VEC(obj, 0x34), d, 0);
        leader_push(obj, d, node, 0);
        if (leader_advance(obj, 1)) {
            I32(obj, 0x180) = 0;
            I32(obj, 0x188) = 0x40;
        }
        return;
    }
}

/* ---- Driller (three linked parts boring along the ground) ------------- */

/* 0x424b90: hold the driller to the ground: two sweeps down from it (the
 * second a step ahead) set its vertical momentum. */
void GAMEAPI driller_follow_ground(uint8_t *obj)
{
    int32_t out[10];
    uint8_t slot[0x48];
    int32_t a, b, hit = 0, i;
    uint32_t r;

    for (i = 0; i < 0x48; i++)
        slot[i] = 0;
    I32(slot, 8) = I32(obj, 0x34) >> 8;
    I32(slot, 0xc) = I32(obj, 0x38) >> 8;
    I32(slot, 0x10) = I32(obj, 0x3c) >> 8;
    I32(slot, 0x14) = 0;
    I32(slot, 0x18) = 0x5000;
    I32(slot, 0x1c) = 0;
    I32(slot, 0x40) = 0;
    r = GCALL(0x415ea0, slot_track_sweep)(obj, slot, out, 0x10000);
    if (out[7])
        hit = 1;
    a = (int32_t)r + ((int32_t)r >> 2) - 50000;
    I32(slot, 8) += I32(obj, 0x68) >> 9;
    I32(slot, 0x10) += I32(obj, 0x70) >> 9;
    r = GCALL(0x415ea0, slot_track_sweep)(obj, slot, out, 0x10000);
    if (out[7])
        hit |= 2;
    b = (int32_t)r + ((int32_t)r >> 2) - 50000;
    switch (hit) {
    case 0:
        I32(obj, 0x44) += 0x8000;
        break;
    case 1:
        I32(obj, 0x44) += a * 8;
        break;
    case 2:
        I32(obj, 0x44) += b * 8;
        break;
    default:
        I32(obj, 0x44) = (b * 2 - a) * 8;
        break;
    }
}

/* 0x424380: the driller's head, once per tick (+0x194: 1 launching, its
 * height +0x1aa from the table at 0x4a80f0; 2 boring on, wobbling). */
void GAMEAPI driller_head_update(uint8_t *part)
{
    uint8_t *obj = PTR(part, 0x18c), *owner;

    if (I32(obj, 0x190) == (int32_t)g_race_ticks)
        return;
    I32(obj, 0x190) = (int32_t)g_race_ticks;
    if (I32(obj, 0x194) == 1) {
        if (--I32(obj, 0x198) == 0) {
            owner = PTR(obj, 0x180);
            I32(obj, 0x1ac) = (uint8_t)GCALL(0x471e60, sound_moving_flagged)(
                1, 0x1e, 9, 0x800, 0x3fff, (uint32_t)(uintptr_t)VEC(obj, 0x34), (uint32_t)(uintptr_t)VEC(obj, 0x68),
                U8(owner, 0x6a4));
            owner = PTR(obj, 0x180);
            GCALL(0x471d50, sound_play_flagged)(0x1c, 6, 0x4b0, 16000, (uint32_t)(uintptr_t)VEC(owner, 0x34),
                                                U8(owner, 0x6a4));
            owner = PTR(obj, 0x180);
            if (S16(owner, 0x710) != 0)
                GCALL(0x41b4d0, pad_effect_request)(owner + 0x6f8, 0xff, 1, 0x40, 0, 10);
            I32(obj, 0x194) = 2;
        }
        S16(obj, 0x1aa) = ((const int16_t *)0x4a80f0)[I32(obj, 0x198)];
        driller_follow_ground(obj);
    } else if (I32(obj, 0x194) == 2) {
        int32_t x, z;

        I32(obj, 0x198)++;
        U16(obj, 0x1a8) = (uint8_t)(U8(obj, 0x1a8) + 0x10);
        if (S16(obj, 0x1aa) > 0x80) {
            S16(obj, 0x1aa) -= 10;
            if (S16(obj, 0x1aa) < 0x80)
                S16(obj, 0x1aa) = 0x80;
        }
        z = I32(obj, 0x48);
        x = I32(obj, 0x40);
        if ((abs32(z) | abs32(x)) > 0x1000) {
            x = (x >> 1) + (x >> 4) + (x >> 3) + (x >> 2);
            z = (z >> 1) + (z >> 4) + (z >> 3) + (z >> 2);
        }
        I32(obj, 0x40) = x + I32(obj, 0x19c);
        I32(obj, 0x48) = z + I32(obj, 0x1a4);
        driller_follow_ground(obj);
    }
}

/* 0x423ff0: one of the driller's three parts (+0x18c the driller; part
 * +0x23e0 is its place, 0..2, round the head). */
void GAMEAPI driller_update(uint8_t *part)
{
    uint8_t *obj = PTR(part, 0x18c);
    int32_t i, k;
    uint32_t angle;

    driller_head_update(part);
    I32(part, 0x180) = I32(obj, 0x198);
    I32(part, 0x188) = I32(obj, 0x194);
    switch ((uint32_t)I32(part, 0x188)) {
    case 0:
        return;
    case 1:
    case 2: {
        int32_t v[3];
        int16_t x;

        v[0] = I32(obj, 0x68) >> 16;
        v[1] = I32(obj, 0x6c) >> 16;
        v[2] = I32(obj, 0x70) >> 16;
        GCALL(0x458f50, matrix_from_forward)(v, (int16_t *)(obj + 0xc4));
        for (i = 0; i < 3; i++) {
            uint8_t *p = PTR(obj, 0x184 + i * 4);

            for (k = 0; k < 9; k++)
                S16(p, 0xc4 + k * 2) = S16(obj, 0xc4 + k * 2);
        }
        if (I32(part, 0x188) == 2) {
            angle = (U16(obj, 0x1a8) + 0x40 + U32(part, 0x23e0) * 0x55) & 0xff;
        } else if (I32(part, 0x23e0) == 1) {
            angle = 0x95;
        } else if (I32(part, 0x23e0) == 2) {
            angle = 0xef;
        } else {
            angle = 0x40;
        }
        gte_load_matrix(obj + 0xc4);
        ((int16_t *)g_gte_vector_words)[0] = g_cos_table[angle];
        ((int16_t *)g_gte_vector_words)[1] = g_sin_table[angle];
        ((int16_t *)g_gte_vector_words)[2] = 0;
        gte_rotate();
        x = (int16_t)g_gte_result[0];
        v[1] = (int16_t)g_gte_result[1];
        v[2] = (int16_t)g_gte_result[2];
        grid_unlink(part);
        k = S16(obj, 0x1aa);
        I32(part, 0x34) = x * k + I32(obj, 0x34);
        I32(part, 0x38) = v[1] * k + I32(obj, 0x38);
        I32(part, 0x3c) = v[2] * k + I32(obj, 0x3c);
        grid_link(part);
        trail_update(part, 1);
        return;
    }
    case 0x10:
        S16(part, 0x23de)--;
        if (++U8(part, 0x23dc) == 0x20)
            U8(part, 0x23dc) = 0;
        if (S16(part, 0x23de) < 2)
            I32(part, 0x188) = 0;
        if (I32(PTR(obj, 0x184), 0x188) == 0 && I32(PTR(obj, 0x188), 0x188) == 0 &&
            I32(PTR(obj, 0x18c), 0x188) == 0) {
            I32(obj, 0x194) = 0;
            g_driller_count--;
        }
        return;
    default:
        I32(part, 0x34) = I32(obj, 0x34);
        I32(part, 0x38) = I32(obj, 0x38);
        I32(part, 0x3c) = I32(obj, 0x3c);
        trail_update(part, 1);
        return;
    }
}

/* 0x458710: the 24 weapon objects in flight. */
void GAMEAPI projectiles_update(void)
{
    int32_t i;

    for (i = 0; i < 0x18; i++) {
        uint8_t *obj = g_projectiles(i);

        if (I32(obj, 0x188) == 0)
            continue;
        switch (I32(obj, 0x184)) {
        case 0:
            leader_update(obj);
            break;
        case 1:
            homer_update(obj);
            break;
        case 2:
            driller_update(obj);
            break;
        }
    }
}

/* ---- Firing ----------------------------------------------------------- */

#define g_shape_spheres GP(0x6ce740) /* 0x48 each */
#define g_leaders(i) GP(0x6b9d38 + (i) * 4)       /* 2 */
#define g_homers(i) GP(0x6b9ce0 + (i) * 4)        /* the first 4 projectiles */
#define g_drillers(i) GP(0x6b9cc0 + (i) * 4)      /* 6 */
#define g_wormholes(i) GP(0x5d1fc0 + (i) * 4)     /* 1 */
#define g_wormhole_count (*(uint8_t *)0x6b9d43)


/* 0x419150: give a body `count` collision spheres (0x14-byte records:
 * x, y, z, radius, flags) plus a bounding one, and queue it. */
void GAMEAPI body_spheres_set(uint8_t *body, uint32_t count, const uint8_t *src, int32_t unused)
{
    uint8_t *table = g_shape_spheres, *e;
    uint32_t i = 0, best = 0;
    int32_t index;

    (void)unused;
    body[0x10c] = (uint8_t)count;
    if (count == 0) {
        shape_list_add(body);
        return;
    }
    for (; (i & 0xffff) < count; i++, src += 0x14) {
        int32_t x, y, z;
        uint32_t r;

        index = (int32_t)GCALL(0x4190a0, slot_take)();
        body[0x140 + (i & 0xffff)] = (uint8_t)index;
        e = table + index * 0x48;
        S16(e, 0) = S16(src, 0);
        S16(e, 2) = S16(src, 4);
        S16(e, 4) = S16(src, 8);
        x = S16(e, 0);
        y = S16(e, 2);
        z = S16(e, 4);
        U32(e, 0x40) = U32(src, 0xc);
        U16(e, 0x44) = U16(src, 0x10);
        U32(e, 0x20) = 0;
        U32(e, 0x24) = 0;
        U32(e, 0x28) = 0;
        U32(e, 0x2c) = 0;
        U32(e, 0x30) = 0;
        U32(e, 0x34) = 0;
        U32(e, 0x38) = 0xffffffff;
        r = (uint32_t)fix_mul8(x, x);
        r = (uint32_t)fix_mul8(z, z) + (uint32_t)fix_mul8(y, y) + r;
        r = (fix_sqrt_table((int32_t)r) << 4) + U32(e, 0x40);
        if (r > best)
            best = r;
        U16(e, 0x46) = (uint16_t)i;
    }
    index = (int32_t)GCALL(0x4190a0, slot_take)();
    body[0x140 + (i & 0xffff)] = (uint8_t)index;
    e = table + index * 0x48;
    U16(e, 0) = 0;
    U16(e, 2) = 0;
    U16(e, 4) = 0;
    U32(e, 0x20) = 0;
    U32(e, 0x24) = 0;
    U32(e, 0x28) = 0;
    U32(e, 0x2c) = 0;
    U32(e, 0x30) = 0;
    U32(e, 0x34) = 0;
    if ((U32(body, 0) & 0x1ff) == 0x11)
        best = 0x5000;
    U32(e, 0x40) = best;
    U16(e, 0x44) = 0;
    U16(e, 0x46) = (uint16_t)count;
    shape_list_add(body);
    U32(body, 0) &= ~0x8000u;
}

/* The launch record (0x60 bytes) a weapon starts from, also what goes
 * over the network: the car's position (+0), matrix (+0xc), mass (+0x2c),
 * momentum (+0x30), upside down (+0x44), the weapon kind (+0x57); homing
 * missiles add their target (+0x48..+0x5f). */
static void launch_record(uint8_t *data, const uint8_t *car)
{
    int32_t i;

    I32(data, 0) = I32(car, 0x34);
    I32(data, 4) = I32(car, 0x38);
    I32(data, 8) = I32(car, 0x3c);
    for (i = 0; i < 8; i++)
        U32(data, 0xc + i * 4) = U32(car, 0xc4 + i * 4);
    U32(data, 0x2c) = U32(car, 0xac);
    I32(data, 0x30) = I32(car, 0x40);
    I32(data, 0x34) = I32(car, 0x44);
    I32(data, 0x38) = I32(car, 0x48);
    U8(data, 0x44) = U8(car, 0x63f);
}

/* the launch record's car mass and momentum scaled to the body's mass */
static void launch_momentum(uint8_t *body, const uint8_t *data)
{
    int32_t k = mul16(I32(body, 0xa8), I32(data, 0x2c));

    I32(body, 0x40) = mul16(I32(data, 0x30), k);
    I32(body, 0x44) = mul16(I32(data, 0x34), k);
    I32(body, 0x48) = mul16(I32(data, 0x38), k);
}

/* the record's car up vector (matrix column 1) * 16, or down if it was
 * upside down */
static void launch_up(const uint8_t *data, int32_t *up)
{
    up[0] = S16(data, 0xe) << 4;
    up[1] = S16(data, 0x14) << 4;
    up[2] = S16(data, 0x1a) << 4;
    if (U8(data, 0x44) == 0) {
        up[0] = -up[0];
        up[1] = -up[1];
        up[2] = -up[2];
    }
}

/* 0x43e880: fire a leader missile (weapon kind 0). */
int32_t GAMEAPI leader_fire(uint8_t *slot, int32_t local, int32_t unused, uint8_t *data, int32_t which)
{
    uint8_t *car = SLOT_CAR(slot), *obj, buffer[0x70];
    uint32_t self;
    int32_t i, up[3];

    (void)unused;
    if (local) {
        data = buffer;
        buffer[0x57] = 0;
    }
    if (!data || data[0x57] != 0) {
        GCALL(0x476ec0, dbg_vlog_printf)((const char *)0x4af494);
        return 0;
    }
    for (i = 0;; i++) {
        if (i == 2)
            return 0;
        obj = g_leaders(i);
        if (I32(obj, 0x188) == 0)
            break;
    }
    PTR_SET(obj, 0x18c, car);
    I32(obj, 0x188) = 1;
    GCALL(0x471d50, sound_play_flagged)(0x3b, 6, 0x400, 15000, (uint32_t)(uintptr_t)VEC(car, 0x34), U8(car, 0x6a4));
    I32(obj, 0x180) = 10;
    if (local)
        launch_record(data, car);
    I32(obj, 0x34) = I32(data, 0);
    I32(obj, 0x38) = I32(data, 4);
    I32(obj, 0x3c) = I32(data, 8);
    I32(obj, 0x1a0) = S16(data, 0x10) << 4;
    I32(obj, 0x1a4) = S16(data, 0x16) << 4;
    I32(obj, 0x1a8) = S16(data, 0x1c) << 4;
    GCALL(0x458f50, matrix_from_forward)(VEC(obj, 0x1a0), (int16_t *)(obj + 0xc4));
    launch_momentum(obj, data);
    launch_up(data, up);
    I32(obj, 0x44) += up[1];
    I32(obj, 0x40) += up[0];
    I32(obj, 0x48) += up[2];
    I32(obj, 0x34) += up[0] * 2;
    I32(obj, 0x38) += up[1] * 2;
    I32(obj, 0x3c) += up[2] * 2;
    U32(obj, 0) &= ~0x1000u;
    body_update_velocity(obj + 0x34);
    grid_link(obj);
    U8(obj, 0x23dc) = 0;
    U16(obj, 0x23de) = 0;
    U8(obj, 0x5b8) = 0;
    trail_update(obj, 2);
    body_spheres_set(obj, 1, (const uint8_t *)0x4af480, 0);
    U32(obj, 0x1b4) = 0;
    self = (uint32_t)(uintptr_t)obj;
    GCALL(0x441d00, light_add)(VEC(obj, 0x34), (uint32_t *)(obj + 0x1b4), 8, 5, 1, &self);
    PTR_SET(obj, LEADER_LINE, PTR(slot, 0x64));
    U16(obj, LEADER_NODE) = U16(slot, 0x68);
    PTR_SET(obj, 0x190, GP(0x6bfdf4));
    U16(obj, 0x240a) = 0xffff;
    leader_route(obj);
    PTR_SET(obj, LEADER_NEXT_LINE, PTR(obj, LEADER_LINE));
    U16(obj, LEADER_NEXT_NODE) = U16(obj, LEADER_NODE);
    S16(obj, LEADER_STEP) = PTR(obj, LEADER_NEXT_LINE)[2] ? -1 : 1;
    leader_advance(obj, 0);
    g_leader_count++;
    if (local)
        GCALL(0x45e6f0, net_pickup_send)((int32_t)slot_car_index(slot), which, 0, (uint32_t)(uintptr_t)data, 0x60);
    return 1;
}

/* 0x459050: fire a homing missile (kind 1) at the slot's target (+0x6c,
 * +0x78..: none, a car (2) or a scenery object (1)), with a random wobble
 * scaled to the distance. */
int32_t GAMEAPI homer_fire(uint8_t *slot, int32_t local, int32_t unused, uint8_t *data, int32_t which)
{
    uint8_t *car = SLOT_CAR(slot), *obj, buffer[0x70];
    uint32_t self;
    int32_t i, up[3], d, clz, n, wobble;

    (void)unused;
    if (local) {
        data = buffer;
        buffer[0x57] = 1;
    }
    if (!data || data[0x57] != 1) {
        GCALL(0x476ec0, dbg_vlog_printf)((const char *)0x4b0c54);
        return 0;
    }
    for (i = 0;; i++) {
        if (i == 4)
            return 0;
        obj = g_homers(i);
        if (I32(obj, 0x188) == 0)
            break;
    }
    PTR_SET(obj, 0x18c, car);
    I32(obj, 0x188) = 1;
    PTR_SET(obj, 0x190, 0);
    PTR_SET(obj, 0x23e4, 0);
    PTR_SET(obj, 0x23e8, 0);
    GCALL(0x471d50, sound_play_flagged)(0x1a, 6, 3000, 15000, (uint32_t)(uintptr_t)VEC(car, 0x34), U8(car, 0x6a4));
    I32(obj, 0x180) = 10;
    if (local) {
        launch_record(data, car);
        I32(data, 0x48) = I32(slot, 0x6c);
        I32(data, 0x4c) = I32(slot, 0x70);
        I32(data, 0x50) = I32(slot, 0x74);
        U16(data, 0x54) = U16(slot, 0x8c);
        U8(data, 0x56) = U8(slot, 0x7a);
        if (U8(slot, 0x7a) == 2) {
            U16(data, 0x5e) = U16(PTR(slot, 0x80), 0x10e);
        } else if (U8(data, 0x56) == 1) {
            U16(data, 0x5e) = U16(PTR(slot, 0x84), 0x10e);
            U16(data, 0x5c) = U8(PTR(slot, 0x80), 0xc) & 0xf;
        } else {
            U16(data, 0x5e) = 0xffff;
            U16(data, 0x5c) = 0xffff;
        }
    }
    I32(obj, 0x34) = I32(data, 0);
    I32(obj, 0x38) = I32(data, 4);
    I32(obj, 0x3c) = I32(data, 8);
    I32(obj, 0x1a0) = S16(data, 0x10) << 4;
    I32(obj, 0x1a4) = S16(data, 0x16) << 4;
    I32(obj, 0x1a8) = S16(data, 0x1c) << 4;
    GCALL(0x458f50, matrix_from_forward)(VEC(obj, 0x1a0), (int16_t *)(obj + 0xc4));
    launch_momentum(obj, data);
    launch_up(data, up);
    I32(obj, 0x40) += (up[0] >> 1) + up[0];
    I32(obj, 0x44) += (up[1] >> 1) + up[1];
    I32(obj, 0x48) += (up[2] >> 1) + up[2];
    U32(obj, 0) |= 0x1000;
    I32(obj, 0x34) += (int32_t)((uint32_t)up[0] << 4);
    I32(obj, 0x38) += (int32_t)((uint32_t)up[1] << 4);
    I32(obj, 0x3c) += (int32_t)((uint32_t)up[2] << 4);
    if (U16(data, 0x54) == 4 || U16(data, 0x54) == 2) {
        uint8_t *target;

        I32(obj, 0x194) = I32(data, 0x48);
        I32(obj, 0x198) = I32(data, 0x4c);
        I32(obj, 0x19c) = I32(data, 0x50);
        target = body_by_id(U16(data, 0x5e));
        if (U8(data, 0x56) == 2) {
            PTR_SET(obj, 0x190, target ? target : PTR(slot, 0x80));
        } else if (U8(data, 0x56) == 1) {
            if (target) {
                uint8_t *model = PTR(target, 0x10);

                PTR_SET(obj, 0x23e8, target);
                PTR_SET(obj, 0x23e4, model + U16(model, 0x34));
                PTR_SET(obj, 0x23e4, model + U16(model, 0x34) + U16(data, 0x5c) * 20);
            } else {
                PTR_SET(obj, 0x23e4, PTR(slot, 0x80));
                PTR_SET(obj, 0x23e8, PTR(slot, 0x84));
            }
        }
    } else {
        I32(obj, 0x194) = (int32_t)((uint32_t)I32(obj, 0x1a0) << 12) + I32(obj, 0x34);
        I32(obj, 0x198) = (int32_t)((uint32_t)I32(obj, 0x1a4) << 12) + I32(obj, 0x38);
        I32(obj, 0x19c) = (int32_t)((uint32_t)I32(obj, 0x1a8) << 12) + I32(obj, 0x3c);
    }
    d = (abs32(I32(obj, 0x19c) - I32(data, 8)) + abs32(I32(obj, 0x198) - I32(data, 4)) +
         abs32(I32(obj, 0x194) - I32(data, 0))) >> 16;
    if (d < 0)
        d = -d;
    *(int32_t *)0x6cccdc = d;
    for (clz = 0; clz < 32 && !((uint32_t)d & 0x80000000u >> clz); clz++)
        ;
    *(int32_t *)0x6ccce0 = clz;
    n = 0x1e - clz;
    if (n > 0xb)
        n = 0xb;
    else if (n < 0)
        n = 0;
    wobble = 2 << n;
    if (local) {
        S16(data, 0x3c) = (int16_t)((random15(&g_seed) & (uint32_t)(wobble * 2 - 1)) - (uint32_t)wobble);
        S16(data, 0x3e) = (int16_t)-(int32_t)(random15(&g_seed) & (uint32_t)(wobble - 1));
        S16(data, 0x40) = (int16_t)((random15(&g_seed) & (uint32_t)(wobble * 2 - 1)) - (uint32_t)wobble);
    }
    U32(obj, 0x1ac) = U32(data, 0x3c);
    U32(obj, 0x1b0) = U32(data, 0x40);
    body_update_velocity(obj + 0x34);
    grid_link(obj);
    U8(obj, 0x23dc) = 0;
    U16(obj, 0x23de) = 0;
    U8(obj, 0x5b8) = 0;
    trail_update(obj, 2);
    body_spheres_set(obj, 1, (const uint8_t *)0x4b0c40, 0);
    U32(obj, 0x1b4) = 0;
    self = (uint32_t)(uintptr_t)obj;
    GCALL(0x441d00, light_add)(VEC(obj, 0x34), (uint32_t *)(obj + 0x1b4), 8, 5, 1, &self);
    g_homer_count++;
    if (local)
        GCALL(0x45e6f0, net_pickup_send)((int32_t)slot_car_index(slot), which, 1, (uint32_t)(uintptr_t)data, 0x60);
    return 1;
}

/* 0x424550: fire a driller (kind 2): a head and three parts round it. */
int32_t GAMEAPI driller_fire(uint8_t *slot, int32_t local, int32_t unused, uint8_t *data, int32_t which)
{
    uint8_t *car = SLOT_CAR(slot), *obj, buffer[0x74], *first;
    uint32_t self;
    int32_t i, up[3];

    (void)unused;
    if (local) {
        data = buffer;
        buffer[0x57] = 2;
    }
    if (!data || data[0x57] != 2) {
        GCALL(0x476ec0, dbg_vlog_printf)((const char *)0x4a8104);
        return 0;
    }
    for (i = 0;; i++) {
        if (i == 6)
            return 0;
        obj = g_drillers(i);
        if (I32(obj, 0x194) == 0)
            break;
    }
    PTR_SET(obj, 0x180, car);
    I32(obj, 0x194) = 1;
    GCALL(0x471d50, sound_play_flagged)(0x1a, 6, 3000, 15000, (uint32_t)(uintptr_t)VEC(car, 0x34), U8(car, 0x6a4));
    U16(obj, 0x1a8) = 0;
    U16(obj, 0x1aa) = 0;
    I32(obj, 0x198) = 10;
    if (local)
        launch_record(data, car);
    I32(obj, 0x34) = I32(data, 0);
    I32(obj, 0x38) = I32(data, 4);
    I32(obj, 0x3c) = I32(data, 8);
    launch_momentum(obj, data);
    launch_up(data, up);
    I32(obj, 0x38) += (int32_t)((uint32_t)up[1] << 6);
    I32(obj, 0x34) += (int32_t)((uint32_t)up[0] << 6);
    I32(obj, 0x3c) += (int32_t)((uint32_t)up[2] << 6);
    I32(obj, 0x34) += (int32_t)((uint32_t)S16(data, 0x10) << 10);
    I32(obj, 0x38) += (int32_t)((uint32_t)S16(data, 0x16) << 10);
    I32(obj, 0x3c) += (int32_t)((uint32_t)S16(data, 0x1c) << 10);
    body_update_velocity(obj + 0x34);
    grid_link(obj);
    body_spheres_set(obj, 1, (const uint8_t *)0x4a80dc, 0);
    for (i = 0; i < 3; i++) {
        uint8_t *part = PTR(obj, 0x184 + i * 4);

        U8(part, 0x23dc) = 0;
        U16(part, 0x23de) = 0;
        I32(part, 0x188) = 1;
        I32(part, 0x180) = 10;
        if (i == 0) {
            I32(part, 0x1a0) = S16(data, 0x10) << 4;
            I32(part, 0x1a4) = S16(data, 0x16) << 4;
            I32(part, 0x1a8) = S16(data, 0x1c) << 4;
            GCALL(0x458f50, matrix_from_forward)(VEC(part, 0x1a0), (int16_t *)(part + 0xc4));
        } else {
            /* meant to copy the part before; the original reads this one
             * again */
            const uint8_t *from = PTR(obj, 0x184 + i * 4);
            int32_t k;

            for (k = 0; k < 5; k++)
                U32(part, 0xc4 + k * 4) = U32(from, 0xc4 + k * 4);
            I32(part, 0x1a0) = I32(from, 0x1a0);
            I32(part, 0x1a4) = I32(from, 0x1a4);
            I32(part, 0x1a8) = I32(from, 0x1a8);
        }
        U16(part, 0x1ac) = 0;
        U16(part, 0x1ae) = 0;
        U16(part, 0x1b0) = 0;
        I32(part, 0x34) = I32(obj, 0x34);
        I32(part, 0x38) = I32(obj, 0x38);
        I32(part, 0x3c) = I32(obj, 0x3c);
        grid_link(part);
        U32(part, 0x1b4) = 0;
        self = (uint32_t)(uintptr_t)part;
        GCALL(0x441d00, light_add)(VEC(part, 0x34), (uint32_t *)(part + 0x1b4), 8, 5, 1, &self);
        trail_update(part, 2);
    }
    first = PTR(obj, 0x184);
    for (i = 0; i < 5; i++)
        U32(obj, 0xc4 + i * 4) = U32(first, 0xc4 + i * 4);
    I32(obj, 0x19c) = I32(first, 0x1a0);
    I32(obj, 0x1a0) = I32(first, 0x1a4);
    I32(obj, 0x1a4) = I32(first, 0x1a8);
    I32(obj, 0x1b0) = 0;
    I32(obj, 0x1b4) = 0;
    g_driller_count++;
    if (local)
        GCALL(0x45e6f0, net_pickup_send)((int32_t)slot_car_index(slot), which, 2, (uint32_t)(uintptr_t)data, 0x60);
    return 1;
}

/* 0x48eda0: set up a new wormhole's rings. */
void GAMEAPI wormhole_init(uint8_t *obj)
{
    int32_t i, v;

    for (i = 0; i < 5; i++) {
        uint8_t *p = PTR(obj, 0x1e0 + i * 4);

        GCALL(0x460ff0, object_grid_link2_at)(p, (int32_t)((uint32_t)S16(p, 0x10) << 16),
                                              (int32_t)((uint32_t)S16(p, 0x12) << 16),
                                              (int32_t)((uint32_t)S16(p, 0x14) << 16));
    }
    for (i = 0; i < 0x20; i++) {
        uint8_t *p = PTR(obj, 0x1f4 + i * 4);

        GCALL(0x460ff0, object_grid_link2_at)(p, (int32_t)((uint32_t)S16(p, 0x10) << 16),
                                              (int32_t)((uint32_t)S16(p, 0x12) << 16),
                                              (int32_t)((uint32_t)S16(p, 0x14) << 16));
    }
    for (v = -0x600, i = 0; v < -0x484; v += 0x26, i++) {
        I32(obj, 0x29c + i * 0x10 + 8) = v;
        GCALL(0x48f430, snatch_spark_reset)((int32_t *)(obj + 0x29c + i * 0x10));
    }
    U32(obj, 0x33c) = 0;
    U8(obj, 0x340) = 0;
    U8(obj, 0x341) = 0;
    S16(obj, 0x342) = g_sin_table[0];
    U16(obj, 0x2a34) = 0;
    GCALL(0x48eef0, snatch_effects)(obj);
}

/* 0x48d920: open a wormhole (kind 7). */
int32_t GAMEAPI wormhole_fire(uint8_t *slot, int32_t local, int32_t unused, uint8_t *data, int32_t which)
{
    uint8_t *car = SLOT_CAR(slot), *obj, buffer[0x70];
    uint32_t self;

    (void)unused;
    if (local) {
        data = buffer;
        buffer[0x57] = 7;
    }
    if (!data || data[0x57] != 7) {
        GCALL(0x476ec0, dbg_vlog_printf)((const char *)0x4b2d44);
        return 0;
    }
    obj = g_wormholes(0);
    if (I32(obj, 0x188) != 0)
        return 0;
    PTR_SET(obj, 0x194, car);
    I32(obj, 0x188) = 1;
    I32(obj, 0x180) = 0;
    I32(obj, 0x18c) = 0x200000;
    GCALL(0x471d50, sound_play_flagged)(0x1f, 6, 0x400, 16000, (uint32_t)(uintptr_t)VEC(car, 0x34), U8(car, 0x6a4));
    if (local)
        launch_record(data, car);
    I32(obj, 0x34) = I32(data, 0);
    I32(obj, 0x38) = I32(data, 4);
    I32(obj, 0x3c) = I32(data, 8);
    launch_momentum(obj, data);
    I32(obj, 0x1ac) = S16(data, 0x10) << 4;
    I32(obj, 0x1b0) = S16(data, 0x16) << 4;
    I32(obj, 0x1b4) = S16(data, 0x1c) << 4;
    GCALL(0x458f50, matrix_from_forward)(VEC(obj, 0x1ac), (int16_t *)(obj + 0xc4));
    body_update_velocity(obj + 0x34);
    grid_link(obj);
    body_spheres_set(obj, 1, (const uint8_t *)0x4b2d20, 0);
    wormhole_init(obj);
    U32(obj, 0x2a38) = 0x7f3f3f;
    self = (uint32_t)(uintptr_t)obj;
    GCALL(0x441d00, light_add)(VEC(obj, 0x34), (uint32_t *)(obj + 0x2a38), 8, 7, 1, &self);
    g_wormhole_count++;
    if (local)
        GCALL(0x45e6f0, net_pickup_send)((int32_t)slot_car_index(slot), which, 7, (uint32_t)(uintptr_t)data, 0x60);
    return 1;
}

/* 0x410160: the freezer hits cars 300..2500 track units ahead (or behind,
 * direction 1) in the same section: an AI firing freezes a player, a
 * player an AI car. */
void GAMEAPI freezer_hit_cars(const uint8_t *slot, int32_t direction)
{
    const uint8_t *car = SLOT_CAR(slot);
    int8_t kind = (int8_t)car[0x70c];
    uint32_t me = U16(car, 0x64e), i;
    uint32_t lap = U8(slot, 0x2c);
    int32_t distance = I32(slot, 0x34);
    uint8_t section = U8(car, 0x11c);

    for (i = 0; i < CAR_COUNT; i++) {
        const uint8_t *other_slot = SLOT(i);
        uint8_t *other;
        int32_t gap;
        uint32_t ds;

        if (i == me)
            continue;
        other = SLOT_CAR(other_slot);
        if (kind == 8) {
            if (other[0x70c] == 8)
                continue;
        } else if (other[0x70c] != 8) {
            continue;
        }
        gap = (int32_t)((uint32_t)U8(other_slot, 0x2c) * 0x10000 - (lap * 0x10000 + (uint32_t)distance)) +
              I32(other_slot, 0x34);
        if (gap > 0x8000)
            gap += (int32_t)(((uint32_t)gap + 0x7fff) >> 16) * -0x10000;
        if (gap < -0x8000)
            gap += (int32_t)(0x7fffu - (uint32_t)gap & 0xffff0000u);
        if (direction == 1)
            gap = -gap;
        if (gap <= 300 || gap >= 0x9c4)
            continue;
        ds = (uint32_t)section - other[0x11c];
        if (abs32((int32_t)ds) >= 2)
            continue;
        if (kind == 8)
            GCALL(0x40fe00, ai_grudge_reduce)((uint8_t *)car, other, 100);
        else
            GCALL(0x40feb0, ai_grudge_add)(other, (uint8_t *)car, 100);
    }
}

/* Freezer ray state */
#define g_freeze_state (*(int32_t *)0x6c30a0)    /* 1 starting, 2 on, 3 shrinking, 4 off */
#define g_freeze_slot GP(0x6c30a4)
#define g_freeze_slot_set(v) (*(uint32_t *)0x6c30a4 = (uint32_t)(uintptr_t)(v))
#define g_freeze_time (*(int32_t *)0x6c30a8)
#define g_freeze_length (*(int32_t *)0x6c30ac)   /* in rows */
#define g_freeze_tail (*(int32_t *)0x6c30b0)
#define g_freeze_step (*(int32_t *)0x6c30b4)     /* +1 / -1 along the track */
#define g_freeze_at GP(0x6c30b8)                 /* the car's track position (+0x11c) */
#define g_freeze_at_set(v) (*(uint32_t *)0x6c30b8 = (uint32_t)(uintptr_t)(v))
#define g_freeze_rows ((int16_t *)0x6c30bc)      /* row counts, two buffers */
#define g_freeze_colour_map ((uint16_t *)GP(0x6c3080))
#define g_freeze_colours_free (*(uint16_t *)0x6c3e94)
#define g_freeze_beams ((int16_t *)0x6c3e96)     /* 2 */
#define g_freeze_changed (*(int32_t *)0x6c3e90)
#define g_freeze_sweep (*(int32_t *)0x6c3eac)
#define g_freeze_voice (*(uint8_t *)0x4beb24)

/* 0x43a910: fire the freezer (kind 3): a ray along the track from the
 * car, forwards or back (by the car's heading against the track and the
 * reverse button). */
int32_t GAMEAPI freezer_fire(uint8_t *slot, int32_t local, int32_t unused, uint8_t *data, int32_t which)
{
    uint8_t *car = SLOT_CAR(slot), *at, *section;
    int32_t direction, start, stride, i, d[3], fwd[3];
    const int16_t *v, *w;
    uint32_t buttons;

    (void)unused;
    if (GCALL(0x45ef80, net_state_game)() == 1 && GCALL(0x45eed0, net_state_host)() == 1 && g_freeze_state != 0)
        return 0;
    g_freeze_state = 1;
    g_freeze_time = 0;
    g_freeze_slot_set(slot);
    for (i = 0; i < 0x200; i++)
        ((uint32_t *)g_freeze_colour_map)[i] = 0xffffffff;
    g_freeze_colours_free = 0x3cf;
    g_freeze_rows[0] = 0;
    g_freeze_rows[1] = 0;
    g_freeze_beams[0] = -1;
    g_freeze_beams[1] = -1;
    g_freeze_sweep = 0;
    g_freeze_length = 4;
    g_freeze_tail = 4;
    at = car + 0x11c;
    g_freeze_at_set(at);
    section = GP(PTR(g_track, 0) + (at[0]) * 4);
    stride = I32(section, 8) + 1;
    v = (const int16_t *)(PTR(section, 0x20) + (S16(at, 2) * stride + (int8_t)at[1]) * 8);
    if (S16(at, 2) == 0) {
        w = v + stride * 4;
        d[0] = -(v[0] - w[0]);
        d[1] = -(v[1] - w[1]);
        d[2] = -(v[2] - w[2]);
    } else {
        w = v - stride * 4;
        d[0] = v[0] - w[0];
        d[1] = v[1] - w[1];
        d[2] = v[2] - w[2];
    }
    d[0] = (int32_t)((uint32_t)d[0] << 16);
    d[1] = (int32_t)((uint32_t)d[1] << 16);
    d[2] = (int32_t)((uint32_t)d[2] << 16);
    fwd[0] = S16(car, 0xc8) << 4;
    fwd[1] = S16(car, 0xce) << 4;
    fwd[2] = S16(car, 0xd4) << 4;
    direction = vec_dot(d, fwd) > 0 ? 1 : -1;
    buttons = U32(car, 0x700);
    if ((buttons & 0x4000) && !(buttons & 0xa000))
        direction = -direction;
    start = local ? direction : *(int32_t *)data;
    g_freeze_changed = 0;
    g_freeze_step = start;
    freezer_hit_cars(slot, start);
    GCALL(0x471d50, sound_play_flagged)(0x14, 6, 0x800, 13000, (uint32_t)(uintptr_t)VEC(car, 0x34), U8(car, 0x6a4));
    g_freeze_voice =
        GCALL(0x471d50, sound_play_flagged)(0x15, 9, 0x514, 15000, (uint32_t)(uintptr_t)VEC(car, 0x34), U8(car, 0x6a4));
    if (local)
        GCALL(0x45e6f0, net_pickup_send)((int32_t)slot_car_index(slot), which, 3, (uint32_t)(uintptr_t)&direction, 4);
    return 1;
}

/* 0x467900: fire the weapon in slot `which` (0 front, 1 rear: car +0x67c
 * + which, the kind; 0xff empty, | 0x40 once a lasting one is running);
 * `local` from this machine (else `data` is the network's launch record).
 * Shows the HUD message for players. */
int32_t GAMEAPI weapon_fire(uint8_t *slot, int32_t which, int32_t local, uint8_t *unused, uint8_t *data)
{
    uint8_t *car = SLOT_CAR(slot);
    int32_t kind = (int8_t)car[0x67c + which], fired = 0, lasting = 0, r;

    (void)unused;
    if (kind == -1)
        return 0;
    switch ((uint32_t)kind) {
    case 0:
        if (!(fired = leader_fire(slot, local, 0, data, which)))
            return 0;
        if (SLOT_CAR(slot)[0x70c] != 8)
            GCALL(0x4600a0, net_kill_message)(U16(SLOT_CAR(slot), 0x64e), 0, 2);
        break;
    case 1:
        if (!(fired = homer_fire(slot, local, 0, data, which)))
            return 0;
        if (SLOT_CAR(slot)[0x70c] != 8)
            GCALL(0x4600a0, net_kill_message)(U16(SLOT_CAR(slot), 0x64e), 0, 0);
        break;
    case 2:
        if (!(fired = driller_fire(slot, local, 0, data, which)))
            return 0;
        if (SLOT_CAR(slot)[0x70c] != 8)
            GCALL(0x4600a0, net_kill_message)(U16(SLOT_CAR(slot), 0x64e), 0, 1);
        break;
    case 3:
        fired = freezer_fire(slot, local, 0, data, which);
        lasting = 1;
        if (!fired)
            return 0;
        if (SLOT_CAR(slot)[0x70c] != 8)
            GCALL(0x4600a0, net_kill_message)(U16(SLOT_CAR(slot), 0x64e), 0, 3);
        break;
    case 4:
        fired = boost_start(slot, 0, 0);
        if (fired) {
            player_markers_pass(slot);
            U16(car, 0x694) = which == 0 ? 4 : 8;
        }
        if (local) {
            if (!fired)
                return 0;
            GCALL(0x45e6f0, net_pickup_send)((int32_t)slot_car_index(slot), which, 4, 0, 0);
        }
        if (!fired)
            return 0;
        if (SLOT_CAR(slot)[0x70c] != 8)
            GCALL(0x4600a0, net_kill_message)(U16(SLOT_CAR(slot), 0x64e), 0, 5);
        break;
    case 5:
        r = time_warp_start(slot, 0);
        if (r == 0) {
            fired = 0;
        } else {
            fired = 1;
            lasting = r == 1;
        }
        if (local) {
            if (!fired)
                return 0;
            GCALL(0x45e6f0, net_pickup_send)((int32_t)slot_car_index(slot), which, 5, 0, 0);
        }
        if (!fired)
            return 0;
        if (SLOT_CAR(slot)[0x70c] != 8)
            GCALL(0x4600a0, net_kill_message)(U16(SLOT_CAR(slot), 0x64e), 0, 6);
        break;
    case 6:
        fired = shield_start(slot);
        if (local) {
            if (!fired)
                return 0;
            GCALL(0x45e6f0, net_pickup_send)((int32_t)slot_car_index(slot), which, 6, 0, 0);
        }
        if (!fired)
            return 0;
        if (SLOT_CAR(slot)[0x70c] != 8)
            GCALL(0x4600a0, net_kill_message)(U16(SLOT_CAR(slot), 0x64e), 0, 4);
        break;
    case 7:
        fired = wormhole_fire(slot, local, 0, data, which);
        lasting = 1;
        if (!fired)
            return 0;
        if (SLOT_CAR(slot)[0x70c] != 8)
            GCALL(0x4600a0, net_kill_message)(U16(SLOT_CAR(slot), 0x64e), 0, 7);
        break;
    default:
        return 0;
    }
    if (lasting) {
        car[0x67c + which] |= 0x40;
        return fired;
    }
    car[0x67c + which] = 0xff;
    return fired;
}

/* ---- Freezer ray ------------------------------------------------------ */

#define g_sections(i) GP(GP(0x6bfac0) + (i) * 4)
#define g_section_links GP(0x6bfb40)
#define g_freeze_list(p) ((uint8_t *)0x6c30c0 + (p) * 0x50) /* rows covered, by tick parity */
#define g_freeze_changes ((uint8_t *)0x6c3160)
#define g_freeze_runs ((uint8_t *)0x6c31b0)
#define g_freeze_saved ((uint8_t *)0x6c31d0)              /* 0x88 each */
#define g_freeze_light ((int16_t *)0x6c3ea4)
#define g_freeze_beam_ends(i) GP(0x6c3e9c + (i) * 4)
#define g_freeze_beam_ends_set(i, v) (*(uint32_t *)GMEM(0x6c3e9c + (i) * 4) = (uint32_t)(uintptr_t)(v))
#define g_freeze_colours_used (*(uint32_t *)0x4b2c64)


/* 0x42b4d0: a spark thrown off a car (slowed by the time warp unless it
 * is the warper's). */
void GAMEAPI wheel_sparks(const uint8_t *car, const int32_t *at, const int32_t *add)
{
    uint8_t *p = GCALL(0x42ac30, particle_take)();
    int32_t vx, vy, vz;
    uint32_t n;

    U32(p, 0) = 8;
    U8(p, 0x95) = 0x18;
    U16(p, 0x96) = 0x14;
    U16(p, 0xa2) = 0;
    vx = I32(car, 0x68) >> 8;
    vy = I32(car, 0x6c) >> 8;
    vz = I32(car, 0x70) >> 8;
    if (g_warp_state != 0 && car != g_warp_car) {
        vx = mul16(g_warp_scale, vx);
        vy = mul16(g_warp_scale, vy);
        vz = mul16(g_warp_scale, vz);
    }
    I32(p, 0x88) = (int32_t)((uint32_t)vx << 8) + at[0];
    I32(p, 0x8c) = (int32_t)((uint32_t)vy << 8) + at[1];
    I32(p, 0x90) = (int32_t)((uint32_t)vz << 8) + at[2];
    S16(p, 0x9c) = (int16_t)(vx * 3 / 4 + (add[0] >> 9));
    S16(p, 0x9e) = (int16_t)(vy * 3 / 4 + (add[1] >> 9));
    S16(p, 0xa0) = (int16_t)(vz * 3 / 4 + (add[2] >> 9));
    GCALL(0x42cd40, particle_fade)(p, 0x14, 1);
    GCALL(0x460ff0, object_grid_link2_at)(p, I32(p, 0x88), I32(p, 0x8c), I32(p, 0x90));
    U8(p, 0x94) = 1;
    U32(p, 0xa4) = *(uint32_t *)0x6cca50;
    n = g_particle_cycle + 1u;
    g_particle_cycle = n < 0xc ? (uint8_t)n : 0;
}

/* 0x43b380: the section next to `section` at its start (dir -1) or end
 * row: one in the track cell there whose matching rows have both their
 * first two vertices within 100 of this section's. -1 if none. */
int32_t GAMEAPI freeze_next_section(uint32_t section, int32_t dir)
{
    const uint8_t *sec = g_sections(section);
    int32_t row = dir == -1 ? 0 : I32(sec, 0xc), count, off_track;
    const int16_t *v = (const int16_t *)(PTR(sec, 0x20) + ((I32(sec, 8) + 1) * row + (int8_t)g_freeze_at[1]) * 8);
    const uint8_t *cell = GCALL(0x419420, track_cell)((int32_t)((uint32_t)v[0] << 16), (int32_t)((uint32_t)v[1] << 16),
                                     (int32_t)((uint32_t)v[2] << 16), &off_track);
    const uint8_t *link = g_section_links + (U32(cell, 4) >> 16 & 0x3fff) * 6;

    for (count = (U16(link, 2) >> 9) + 1; count > 0; count--, link += 6) {
        uint32_t other = U8(link, 2) & 0x7f;
        const uint8_t *osec;
        int32_t orow, first, k, n;
        const int16_t *w;

        if (other == section)
            continue;
        osec = g_sections(other);
        orow = row == 0 ? I32(osec, 0xc) : 0;
        first = U16(link, 0) & 0x3ff;
        if (orow < first || orow > (int32_t)((U16(link, 4) >> 8 & 0xf) + first))
            continue;
        w = (const int16_t *)(PTR(osec, 0x20) + ((I32(osec, 8) + 1) * orow + (U16(link, 0) >> 10)) * 8);
        n = U16(link, 4) >> 12;
        for (k = 0; k < n; k++, w += 4) {
            if (abs32(w[0] - v[0]) <= 0x64 && abs32(w[1] - v[1]) <= 0x64 && abs32(w[2] - v[2]) <= 0x64 &&
                abs32(w[4] - v[4]) <= 0x64 && abs32(w[5] - v[5]) <= 0x64 && abs32(w[6] - v[6]) <= 0x64)
                return (int32_t)(U8(link, 2) & 0x7f);
        }
    }
    return -1;
}

/* 0x43b2a0: tint a vertex's colour (index in bits 6..15 of `colour`)
 * icy: each colour used gets a lighter copy at the top of the palette. */
void GAMEAPI freeze_vertex_colour(uint8_t *vertex, uint32_t colour)
{
    uint8_t *palette = PTR(g_track, 0x30), *c;
    uint16_t *map;
    uint32_t k, r, g, b;

    if (g_freeze_colours_free <= U16(g_track, 0x5c))
        return;
    map = g_freeze_colour_map;
    g_freeze_colours_used = 1;
    k = colour >> 6 & 0x3ff;
    if (map[k] == 0xffff) {
        map[k] = g_freeze_colours_free;
        c = palette + g_freeze_colours_free * 4;
        U32(c, 0) = U32(palette, k * 4);
        r = c[0] + 0x50u;
        if (r > 0xff)
            r = 0xff;
        g = c[1] + 0x50u;
        if (g > 0xff)
            g = 0xff;
        b = c[2] + 200u;
        if (b > 0xff)
            b = 0xff;
        c[0] = (uint8_t)r;
        c[1] = (uint8_t)g;
        c[2] = (uint8_t)b;
        g_freeze_colours_free--;
    }
    U16(vertex, 6) = (uint16_t)(map[k] << 6 | (colour & 0x3f));
}

/* 0x43b190: freeze a row of track vertices (saving their colours), or
 * thaw it if it is already frozen. `row`: int16 row, byte section. */
void GAMEAPI freeze_row_toggle(const uint8_t *row)
{
    int32_t n = *(int32_t *)0x6c3e90, j, i, count;
    uint8_t *rec = g_freeze_saved;
    const uint8_t *sec;
    uint8_t *v;

    for (j = 0; j < n; j++, rec += 0x88) {
        if (S16(rec, 0) == S16(row, 0) && rec[2] == row[2]) {
            const uint8_t *last;

            sec = g_sections((int8_t)row[2]);
            v = PTR(rec, 4);
            count = I32(sec, 8) + 1;
            for (i = 0; i < count; i++)
                U16(v, i * 8 + 6) = U16(rec, 8 + i * 2);
            n--;
            *(int32_t *)0x6c3e90 = n;
            last = g_freeze_saved + n * 0x88;
            U32(rec, 0) = U32(last, 0);
            U32(rec, 4) = U32(last, 4);
            for (i = 0; i < 0x40; i++)
                U16(rec, 8 + i * 2) = U16(last, 8 + i * 2);
            return;
        }
    }
    *(int32_t *)0x6c3e90 = n + 1;
    U32(rec, 0) = U32(row, 0);
    sec = g_sections((int8_t)row[2]);
    v = PTR(sec, 0x20) + (I32(sec, 8) + 1) * S16(row, 0) * 8;
    PTR_SET(rec, 4, v);
    count = I32(sec, 8) + 1;
    for (i = 0; i < count; i++, v += 8) {
        U16(rec, 8 + i * 2) = U16(v, 6);
        freeze_vertex_colour(v, U16(v, 6));
    }
}

/* 0x43aba0: the freezer ray (state 1 growing to 14 rows, 2 holding for
 * 200 steps, 3 shrinking from the car, 4 ending): the rows it covers this
 * step (two lists by tick parity) are compared with last step's; rows
 * entering or leaving are frozen / thawed, the changed spans marked for
 * redrawing; two light beams sweep its far end and sparks fly off it. */
void GAMEAPI freezer_update(void)
{
    uint32_t p = g_race_ticks & 1, section;
    int32_t old_count, step, i, n, m, k, row, stride;
    uint8_t *list, *e, *old, *changes, local[4];
    const uint8_t *first, *sec;
    const int16_t *v;
    uint32_t bits;

    switch (g_freeze_state) {
    case 1:
        g_freeze_length++;
        g_freeze_time++;
        if (g_freeze_length == 0xe) {
            g_freeze_time = 0;
            g_freeze_state = 2;
        }
        break;
    case 2:
        g_freeze_time++;
        if (g_freeze_voice != 0x7f) {
            int32_t volume = g_freeze_time > 0x96 ? (int32_t)0xffffd098 - (int32_t)((g_freeze_time & 0xff) << 8)
                                                  : 15000;

            GCALL(0x471fc0, voice_set_volume)(g_freeze_voice, (uint32_t)volume, (uint32_t)volume);
        }
        if (g_freeze_time == 0xaf)
            weapon_slot_spent(SLOT_CAR(g_freeze_slot), 3);
        if (g_freeze_time > 200) {
            g_freeze_time = 0;
            g_freeze_state = 3;
        }
        break;
    case 3:
        g_freeze_time++;
        if (++g_freeze_tail == g_freeze_length) {
            g_freeze_time = 0;
            g_freeze_state = 4;
            beam_remove(g_freeze_beams[0]);
            beam_remove(g_freeze_beams[1]);
        }
        break;
    case 4:
        GCALL(0x467c90, car_clear_item)(SLOT_CAR(g_freeze_slot), 3);
        g_freeze_state = 0;
        if (g_freeze_voice != 0x7f) {
            GCALL(0x471f40, sound_voice_stop)(g_freeze_voice);
            g_freeze_voice = 0x7f;
        }
        break;
    default:
        return;
    }

    /* the rows covered now */
    step = g_freeze_step;
    old_count = (uint16_t)g_freeze_rows[1 - p];
    section = g_freeze_at[0];
    row = S16(g_freeze_at, 2);
    list = g_freeze_list(p);
    e = list;
    n = 0;
    for (i = 0; i < g_freeze_length;) {
        const uint8_t *s;

        if (i >= g_freeze_tail) {
            e[2] = (uint8_t)section;
            S16(e, 0) = (int16_t)row;
            e[3] = 1;
            n++;
            e += 4;
        }
        s = g_sections(section);
        row += step;
        if (row < 0) {
            int32_t next = freeze_next_section(section, -1);

            if (next == -1)
                break;
            section = (uint32_t)next;
            row = I32(g_sections(section), 0xc);
        } else if (row > I32(s, 0xc)) {
            int32_t next = freeze_next_section(section, 1);

            if (next == -1)
                break;
            section = (uint32_t)next;
            row = 0;
        } else {
            i++;
        }
    }
    g_freeze_rows[p] = (int16_t)n;

    /* rows entering (kind 1) and leaving (kind 3) */
    changes = g_freeze_changes;
    m = 0;
    old = g_freeze_list(1 - p);
    for (i = 0; i < n; i++) {
        for (k = 0; k < old_count; k++)
            if (U32(old, k * 4) == U32(list, i * 4))
                break;
        if (k < old_count)
            continue;
        U32(changes, m * 4) = U32(list, i * 4);
        freeze_row_toggle(list + i * 4);
        m++;
    }
    for (i = 0; i < old_count; i++) {
        for (k = 0; k < n; k++)
            if (U32(list, k * 4) == U32(old, i * 4))
                break;
        if (k < n)
            continue;
        U32(changes, m * 4) = U32(old, i * 4);
        changes[m * 4 + 3] = 3;
        freeze_row_toggle(changes + m * 4);
        m++;
    }

    /* merge the changed rows into spans per section, to redraw */
    k = 0;
    for (i = 0; i < m; i++) {
        uint8_t *c = changes + i * 4, *run = g_freeze_runs;
        int32_t j;

        for (j = 0; j < k; j++, run += 4) {
            if ((int32_t)(int8_t)c[2] == (int32_t)run[3] &&
                (S16(c, 0) == S16(run, 0) - 1 || S16(c, 0) == run[2] + S16(run, 0))) {
                if (S16(c, 0) < S16(run, 0))
                    S16(run, 0)--;
                run[2]++;
                break;
            }
        }
        if (j < k)
            continue;
        run[3] = c[2];
        S16(run, 0) = S16(c, 0);
        run[2] = 1;
        k++;
    }
    bits = p;
    for (i = 0; i < k; i++) {
        const uint8_t *run = g_freeze_runs + i * 4;

        bits ^= ((uint32_t)S16(run, 0) ^ bits) & 0x7ff;
        bits ^= (((uint32_t)run[2] + (uint32_t)S16(run, 0)) << 11 ^ bits) & 0x3ff800;
        GCALL(0x4411c0, mesh_rows_touch)(bits, run[3]);
    }

    /* the light at the car's nose, beams to the ray's far end */
    {
        const uint8_t *car = SLOT_CAR(g_freeze_slot);

        g_freeze_light[0] = (int16_t)((I32(car, 0x34) >> 16) + (S16(car, 0xc8) >> 6));
        g_freeze_light[1] = (int16_t)((I32(car, 0x38) >> 16) + (S16(car, 0xce) >> 6));
        g_freeze_light[2] = (int16_t)((I32(car, 0x3c) >> 16) + (S16(car, 0xd4) >> 6));
    }
    if (n > 0) {
        e = list + (n - 1) * 4;
        first = list;
    } else {
        S16(local, 0) = S16(g_freeze_at, 2);
        local[2] = g_freeze_at[0];
        e = local;
        first = local;
    }
    sec = GP(PTR(g_track, 0) + ((int8_t)e[2]) * 4);
    stride = I32(sec, 8) + 1;
    if (g_race_ticks & 1)
        g_freeze_sweep++;
    if (g_freeze_sweep >= stride)
        g_freeze_sweep = 0;
    {
        uint8_t *verts = PTR(sec, 0x20) + S16(e, 0) * stride * 8;

        g_freeze_beam_ends_set(0, verts + g_freeze_sweep * 8);
        g_freeze_beam_ends_set(1, verts + (stride - g_freeze_sweep) * 8);
    }
    for (i = 0; i < 2; i++) {
        if (g_freeze_beams[i] == -1)
            g_freeze_beams[i] = (int16_t)beam_add(g_freeze_light, (const int16_t *)g_freeze_beam_ends(i), 0x3fff);
        else
            beam_move(g_freeze_light, (const int16_t *)g_freeze_beam_ends(i), g_freeze_beams[i]);
    }

    /* sparks off the near end */
    {
        int32_t zero[3] = {0, 0, 0}, at[3];

        sec = GP(PTR(g_track, 0) + ((int8_t)first[2]) * 4);
        stride = I32(sec, 8) + 1;
        v = (const int16_t *)(PTR(sec, 0x20) + S16(first, 0) * stride * 8);
        for (i = stride; i > 0; i--, v += 4) {
            uint32_t r = random_step(&g_fx_seed) >> 16;

            if (r & 7)
                continue;
            at[0] = (int32_t)(((r & 0xff) + v[0] - 0x80) << 16);
            at[1] = (int32_t)((uint32_t)(v[1] - 0x20) << 16);
            at[2] = (int32_t)(((r >> 8 & 0xff) + v[2] - 0x80) << 16);
            wheel_sparks(SLOT_CAR(g_freeze_slot), at, zero);
        }
    }
}

/* ---- Homing missile targeting ----------------------------------------- */

#define g_scenery_targets (*(int32_t *)0x5f72a4) /* entries at markers +0xec, 0x14 each */
#define g_car_targets ((uint8_t *)0x5f7220)       /* 8 each: body, type */
#define g_car_target_count (*(int32_t *)0x5f72a0)
#define g_gte_diag(i) (*(int16_t *)(0x6ccbe0 + (i) * 8))
#define g_gte_vector2(i) (*(int16_t *)(0x6ccc88 + (i) * 4))


/* 0x478bb0: does the segment from `p` towards `target` (direction n)
 * pass through one of the track polygons of this cell? */
int32_t GAMEAPI target_blocked(const uint8_t *cell, const int32_t *n, const int32_t *p, const int16_t *target)
{
    const uint8_t *track = g_track, *link;
    const uint32_t *normals = (const uint32_t *)GP(track + 0x1c);
    int32_t count;

    link = PTR(track, 0x80) + (U32(cell, 4) >> 16 & 0x3fff) * 6;
    for (count = (U16(link, 2) >> 9) + 1; (uint32_t)count > 0; link += 6) {
        const uint8_t *sec = GP(PTR(track, 0) + (link[2] & 0x7f) * 4), *plane;
        uint32_t w = U16(link, 0), row = w & 0x3ff, normal;
        int32_t idx = I32(sec, 8) * (int32_t)row + (int32_t)(w >> 10), dist;
        const int16_t *v;

        plane = PTR(sec, 0x50) + idx * 8;
        v = (const int16_t *)(PTR(sec, 0x20) + (idx + (int32_t)row) * 8);
        count--;
        dist = (p[0] - v[0]) * n[0] + (p[1] - v[1]) * n[1] + (p[2] - v[2]) * n[2];
        if (dist > 0)
            continue;
        dist = (target[1] - v[1]) * n[1] + (target[2] - v[2]) * n[2] + (target[0] - v[0]) * n[0];
        if (dist < 0)
            continue;
        normal = normals[U16(plane, 2)];
        g_gte_diag(2) = (int16_t)n[2];
        g_gte_diag(0) = (int16_t)n[0];
        g_gte_diag(1) = (int16_t)n[1];
        g_gte_vector2(0) = (int16_t)((int32_t)(normal << 2) >> 22);
        g_gte_vector2(1) = (int16_t)((int32_t)(normal << 12) >> 22);
        g_gte_vector2(2) = (int16_t)((int32_t)(normal << 22) >> 22);
        gte_outer_product();
        g_gte_vector2(0) = (int16_t)n[0];
        g_gte_diag(0) = (int16_t)(g_gte_result[0] >> 8);
        g_gte_diag(1) = (int16_t)(g_gte_result[1] >> 8);
        g_gte_diag(2) = (int16_t)(g_gte_result[2] >> 8);
        g_gte_vector2(1) = (int16_t)n[1];
        g_gte_vector2(2) = (int16_t)n[2];
        gte_outer_product();
        dist = (g_gte_result[2] >> 8) * (p[2] - v[2]) + (g_gte_result[1] >> 8) * (p[1] - v[1]) +
               (g_gte_result[0] >> 8) * (p[0] - v[0]);
        if (dist < 0)
            return 1;
    }
    return 0;
}

/* 0x4789b0: can the slot's car see `target` (in front, nearer the
 * nose's line than `best`, and no track in the way over four steps)?
 * Then `best` becomes this target's score. `sticky` (switching away from
 * the current target) needs 6000 more than the current one's. */
int32_t GAMEAPI target_visible(const uint8_t *slot, int32_t sticky, const int16_t *target, int32_t *best)
{
    const uint8_t *car = SLOT_CAR(slot);
    int32_t p[3], d[3], step[3], last[3] = {0, 0, 0}, dot, need, i, off_track;
    uint32_t home;

    p[0] = I32(car, 0x34) >> 16;
    p[1] = I32(car, 0x38) >> 16;
    p[2] = I32(car, 0x3c) >> 16;
    d[0] = target[0] - p[0];
    d[1] = target[1] - p[1];
    d[2] = target[2] - p[2];
    step[0] = d[0] >> 2;
    step[1] = d[1] >> 2;
    step[2] = d[2] >> 2;
    vec_normalise_shifted(d, d);
    d[0] >>= 4;
    d[1] >>= 4;
    d[2] >>= 4;
    dot = S16(car, 0xd4) * d[2] + S16(car, 0xce) * d[1] + S16(car, 0xc8) * d[0];
    if (dot < 0)
        return 0;
    need = *best;
    if (sticky == 1 && I32(slot, 0x88) + 6000 > need)
        need = I32(slot, 0x88) + 6000;
    if (dot < need)
        return 0;
    home = U32(car, 0xc) & ~1u;
    for (i = 0; i < 4; i++) {
        p[0] += step[0];
        p[2] += step[2];
        p[1] += step[1];
        if (((uint32_t)(p[0] - last[0]) & 0xffffff00) || ((uint32_t)(p[1] - last[1]) & 0xffffff00) ||
            ((uint32_t)(p[2] - last[2]) & 0xffffff00)) {
            uint8_t *cell;

            last[2] = p[2];
            last[1] = p[1];
            last[0] = p[0];
            cell = GCALL(0x419420, track_cell)((int32_t)((uint32_t)p[0] << 16), (int32_t)((uint32_t)p[1] << 16),
                                 (int32_t)((uint32_t)p[2] << 16), &off_track);
            if ((uint32_t)(uintptr_t)cell != home) {
                if (off_track)
                    return 0;
                if (target_blocked(cell, d, p, target))
                    return 0;
            }
        }
    }
    *best = dot;
    return 1;
}

/* 0x478530: a homing missile holder's target (+0x7a: 0 none, 1 scenery
 * item +0x80 on object +0x84, 2 car +0x80; +0x6c the point; +0x88 its
 * score): the best visible within 0x900 units, scenery first. */
void GAMEAPI homer_choose_target(uint8_t *slot)
{
    uint8_t *car = SLOT_CAR(slot), *item = 0, *object = 0, *body = 0;
    int32_t best = -1, kind = 1, i, point[3];
    int16_t at[3];

    if (U8(car, 0x67c) != 1 && U8(car, 0x67d) != 1) {
        I32(slot, 0x8c) = 0;
        U16(slot, 0x7a) = 0;
        return;
    }
    if (g_scenery_targets > 0) {
        const uint8_t *entry = PTR(g_markers, 0xec);

        for (i = g_scenery_targets; i; i--, entry += 0x14) {
            uint8_t *it = PTR(entry, 0), *obj = PTR(entry, 4);
            int32_t x, z, c, s, t1, t2, t3, t4, dx, dy, dz;

            if ((U32(PTR(obj, 0x194), (U32(it, 0xc) & 0xf) * 4) & 0xf000000) == 0x5000000)
                continue;
            if (I32(slot, 0x8c) == 2 && (U16(slot, 0x7a) != 1 || PTR(slot, 0x80) != it))
                continue;
            x = (int16_t)U32(it, 0);
            at[1] = (int16_t)(I32(it, 0) >> 16);
            z = (int16_t)U32(it, 4);
            c = g_cos_table[U8(obj, 0x199)] >> 6;
            s = g_sin_table[U8(obj, 0x199)] >> 6;
            t1 = fix_mul8(x, c);
            t2 = fix_mul8(z, s);
            t3 = fix_mul8(z, c);
            t4 = fix_mul8(-x, s);
            at[0] = (int16_t)((int16_t)(t2 + t1) + (int16_t)(I32(obj, 0x34) >> 16));
            at[1] = (int16_t)(at[1] + (int16_t)(I32(obj, 0x38) >> 16));
            at[2] = (int16_t)((I32(obj, 0x3c) >> 16) + (t4 + t3));
            dx = at[0] - (I32(car, 0x34) >> 16);
            dy = at[1] - (I32(car, 0x38) >> 16);
            dz = at[2] - (I32(car, 0x3c) >> 16);
            if (dz * dz + dy * dy + dx * dx > 0x900000)
                continue;
            if (target_visible(slot, U16(slot, 0x7a) != 0 && (U16(slot, 0x7a) != 1 || PTR(slot, 0x80) != it), at,
                               &best)) {
                object = obj;
                item = it;
                point[0] = (int32_t)((uint32_t)at[0] << 16);
                point[1] = (int32_t)((uint32_t)at[1] << 16);
                point[2] = (int32_t)((uint32_t)at[2] << 16);
            }
        }
    }
    if (g_car_target_count > 0) {
        const uint8_t *entry = g_car_targets;

        for (i = g_car_target_count; i; i--, entry += 8) {
            uint8_t *b = PTR(entry, 0);
            int32_t dx, dy, dz;

            if (I32(entry, 4) == 1 && (!(U32(car, 0x700) & 0x1000) || b == car))
                continue;
            if (I32(slot, 0x8c) == 2 && (U16(slot, 0x7a) != 2 || PTR(slot, 0x80) != b))
                continue;
            at[0] = (int16_t)(I32(b, 0x34) >> 16);
            at[1] = (int16_t)(I32(b, 0x38) >> 16);
            at[2] = (int16_t)(I32(b, 0x3c) >> 16);
            dx = at[0] - (I32(car, 0x34) >> 16);
            dy = at[1] - (I32(car, 0x38) >> 16);
            dz = at[2] - (I32(car, 0x3c) >> 16);
            if (dz * dz + dy * dy + dx * dx > 0x900000)
                continue;
            if (target_visible(slot, 0, at, &best)) {
                point[0] = (int32_t)((uint32_t)at[0] << 16);
                body = b;
                kind = 2;
                point[1] = (int32_t)((uint32_t)at[1] << 16);
                point[2] = (int32_t)((uint32_t)at[2] << 16);
            }
        }
    }
    if (best <= 0) {
        I32(slot, 0x8c) = 0;
        U16(slot, 0x7a) = 0;
        return;
    }
    if (kind == U16(slot, 0x7a) &&
        ((kind == 1 && PTR(slot, 0x80) == item && PTR(slot, 0x84) == object) ||
         (kind == 2 && PTR(slot, 0x80) == body))) {
        I32(slot, 0x88) = best;
        if (kind != 2)
            return;
    } else {
        U16(slot, 0x78) = 6;
        I32(slot, 0x8c) = 2;
        I32(slot, 0x88) = best;
        U16(slot, 0x7a) = (uint16_t)kind;
        if (kind == 2) {
            PTR_SET(slot, 0x80, body);
        } else {
            PTR_SET(slot, 0x80, item);
            PTR_SET(slot, 0x84, object);
        }
        U8(slot, 0x7c) = 0x52;
        U8(slot, 0x7d) = 0x28;
        U8(slot, 0x7e) = 0xff;
    }
    I32(slot, 0x6c) = point[0];
    I32(slot, 0x70) = point[1];
    I32(slot, 0x74) = point[2];
}

/* 0x478470: targeting for every player car, and one AI car per step in
 * turn; then the lock-on countdowns (+0x78) of the slots locked (+0x8c
 * 2) run out to 4. */
void GAMEAPI homers_choose_targets(void)
{
    uint32_t turn = g_race_ticks % CAR_COUNT, i;

    for (i = 0; (int32_t)i < (int32_t)CAR_COUNT; i++) {
        const uint8_t *car = SLOT_CAR(SLOT(i));

        if (car[0x70c] != 8 || U16(car, 0x64e) == turn)
            homer_choose_target(SLOT(i));
    }
    for (i = 0; (int32_t)i < (int32_t)CAR_COUNT; i++) {
        uint8_t *slot = SLOT(i);

        if (I32(slot, 0x8c) == 2 && --S16(slot, 0x78) == 0) {
            I32(slot, 0x8c) = 4;
            S16(slot, 0x78) = 0;
            U8(slot, 0x7c) = 0xff;
            U8(slot, 0x7d) = 0;
            U8(slot, 0x7e) = 0x52;
        }
    }
}

/* ---- Top level -------------------------------------------------------- */

/* 0x47ab40 */
void GAMEAPI weapons_update(void)
{
    int32_t limit = -1, i;

    pickups_update();
    projectiles_update();
    freezer_update();
    time_warp_update();
    homers_choose_targets();
    for (i = 0; i < (int32_t)CAR_COUNT; i++) {
        uint8_t *slot = SLOT(i), *car = SLOT_CAR(slot);
        int32_t target;

        car_hit_timer_tick(car);
        car_fire_buttons(slot);
        target = I32(car, 0x69c);
        if (I32(car, 0x698) < target)
            I32(car, 0x698) = target;
        else
            I32(car, 0x698) -= 0x400;
        if (I32(car, 0x698) > 0xd0000)
            I32(car, 0x698) = 0xd0000;
        if (target > 0xd0000)
            I32(car, 0x69c) = 0xd0000;
        if (S16(car, 0x690) == 0)
            continue;
        if (S16(car, 0x690) == 1) {
            limit = 0x5a;
            if (S16(car, 0x692) < 3 && !(U32(SLOT_CAR(slot), 0x700) & (uint32_t)(int32_t)S16(car, 0x694)))
                S16(car, 0x690) = 2;
        } else if (S16(car, 0x690) == 2) {
            limit = 0x96;
        }
        S16(car, 0x692)++;
        if (S16(car, 0x692) > limit) {
            S16(car, 0x690) = 0;
            S16(car, 0x692) = 0;
            I32(car, 0x69c) = 0x60000;
            break; /* (the original stops for the other cars here) */
        }
        if (S16(car, 0x692) == 0xe) {
            U8(car, 0x697) = U8(SLOT_CAR(slot), 0x6f9);
        } else if (S16(car, 0x692) == 0xf &&
                   (U8(car, 0x697) < U8(SLOT_CAR(slot), 0x6f9) || U8(SLOT_CAR(slot), 0x6f9) > 200)) {
            GCALL(0x47a860, car_kick_forward)(car, 0x14);
        }
    }
    for (i = 0; i < (int32_t)CAR_COUNT; i++) {
        uint8_t *slot = SLOT(i), *ripple = PTR(SLOT_CAR(slot), 0x6ac);

        if (ripple != NO_PTR && ripple[0] != 0)
            shield_ripple_update(slot);
    }
}
