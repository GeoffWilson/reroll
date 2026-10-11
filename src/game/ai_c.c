/* The AI's weapons (0x401780, from the AI's thinking every step): each of
 * the car's two weapon slots (+0x67c, +0x67d: weapon type 0..7, -1 none)
 * has a countdown (+0x6d4, +0x6d6) restarted at a random delay when the
 * slot is empty, and once it is running each type has its own "fire now?"
 * test; firing presses the slot's fire button (+0x700 bits 4 and 8), the
 * same button a player would.
 *
 * Car slots (0x6bfb80, 0x274 each): +0 car, +0x2c lap, +0x2d leader,
 * +0x34 distance round the lap (16 bits a lap), +0x64 the line being
 * followed (+5 its index), +0x68 the node on it. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_ai_seed (*(uint32_t *)0x4a7148)
#define g_ai_difficulty_mode (*(int32_t *)0x4a714c)
#define g_difficulty (*(uint32_t *)0x6d0ed0)
#define g_lines (*(uint8_t **)(*(uint8_t **)0x6c1e30 + 0x60))
#define g_time_warp (*(int32_t *)0x5d9b20)
#define g_no_leader_missiles (*(uint8_t *)0x6b9d42)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274

#define LINE(i) (g_lines + (uint16_t)(i) * 0xc)
#define NODE(line, i) (*(uint8_t **)((line) + 8) + (uint16_t)(i) * 0x10)
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define SLOT(i) (g_car_slots + (i) * CAR_SLOT_SIZE)
#define SLOT_CAR(slot) (*(uint8_t **)(slot))
#define SLOT_AT(slot) ((int32_t)((uint32_t)(slot)[0x2c] << 16) + I32(slot, 0x34))
#define MY_INDEX(car) (*(const uint16_t *)((car) + 0x64e))
#define BUTTONS 0x700

typedef int32_t(GAMEAPI *slot_fn)(uint8_t *slot);
#define slot_on_pad ((slot_fn)0x480030) /* group A */

static uint32_t ai_random(void)
{
    g_ai_seed = g_ai_seed * 16807 & 0x7fffffff;
    return g_ai_seed;
}

/* A gap round the lap folded into -0x8000..0x8000 (half a lap). */
static int32_t gap_fold(int32_t gap)
{
    if (gap > 0x8000)
        gap += (int32_t)(-((uint32_t)(gap + 0x7fff) >> 16) << 16);
    if (gap < -0x8000)
        gap += (int32_t)((0x7fffu - (uint32_t)gap) >> 16 << 16);
    return gap;
}

/* 0x47f330, 0x47f350, 0x47f370: press rear fire, or a slot's fire
 * button unless it is held (+0x6fc). */
void GAMEAPI ai_press_rear_fire(uint8_t *car) { U32(car, BUTTONS) |= 0x4000; }

void GAMEAPI ai_press_fire_a(uint8_t *car)
{
    if (!(car[0x6fc] & 4))
        U32(car, BUTTONS) |= 4;
}

void GAMEAPI ai_press_fire_b(uint8_t *car)
{
    if (!(car[0x6fc] & 8))
        U32(car, BUTTONS) |= 8;
}

/* 0x47f550: the node `count` nodes ahead along the line; 0 if the line
 * ends first. */
int32_t GAMEAPI line_node_ahead(uint32_t node, const uint8_t *line, uint32_t count, uint16_t *out)
{
    uint16_t n = (uint16_t)node;
    uint8_t i;

    for (i = 0; i < (uint8_t)count; i++)
        if (!line_next(n, line, &n))
            return 0;
    *out = n;
    return 1;
}

/* 0x402e50: does the car's line have a speed limit (node +7 not 0xff) at
 * its node or within the next 9? */
int32_t GAMEAPI ai_corner_ahead(const uint8_t *car)
{
    const uint8_t *line = LINE(*(const uint16_t *)(car + 0x6b0));
    uint16_t node = *(const uint16_t *)(car + 0x6b2);
    int32_t i;

    if (NODE(line, node)[7] != 0xff)
        return 1;
    for (i = 0; i < 9; i++) {
        if (!line_next(node, line, &node))
            return 0;
        if (NODE(line, node)[7] != 0xff)
            return 1;
    }
    return 0;
}

/* 0x402750: where a scenery object (entry: +0 its shape, +4 the object)
 * is: the shape's offset turned by the object's yaw (+0x199) plus its
 * position, in whole units. */
void GAMEAPI scenery_position(const int32_t *entry, int16_t *out)
{
    const int32_t *shape = (const int32_t *)(uintptr_t)entry[0];
    const uint8_t *object = (const uint8_t *)(uintptr_t)entry[1];
    int32_t cos_yaw = *(const int16_t *)(0x4b2578 + object[0x199] * 2) >> 6;
    int32_t sin_yaw = *(const int16_t *)(0x4b24f8 + object[0x199] * 2) >> 6;
    int32_t x, z, a, b, c, d;

    out[0] = (int16_t)shape[0];
    out[1] = (int16_t)((uint32_t)shape[0] >> 16);
    out[2] = (int16_t)shape[1];
    x = out[0];
    z = out[2];
    a = fix_mul8(x, cos_yaw);
    b = fix_mul8(z, sin_yaw);
    c = fix_mul8(z, cos_yaw);
    d = fix_mul8(-x, sin_yaw);
    out[2] = (int16_t)(d + c);
    out[0] = (int16_t)((I32(object, 0x34) >> 16) + a + b);
    out[1] = (int16_t)(out[1] + (I32(object, 0x38) >> 16));
    out[2] = (int16_t)(out[2] + (I32(object, 0x3c) >> 16));
}

/* 0x401c70 (type 6): fire once 50 steps have passed, or at once if
 * nothing is locked on (+0x6ac). */
int32_t GAMEAPI ai_weapon6_ready(const uint8_t *car, int32_t time)
{
    return I32(car, 0x6ac) == -1 || (int16_t)time >= 0x32;
}

/* 0x401c90 (type 5): fire unless one is already running (0x5d9b20). */
int32_t GAMEAPI ai_weapon5_ready(const uint8_t *car)
{
    (void)car;
    return g_time_warp == 0;
}

/* 0x401ca0 (type 0): aimed at the race leader (slot +0x2d), or the cars
 * in +0x6cb; not when this car leads, nor near the end of a close race,
 * nor at a leader just behind going slower. For easy settings, only
 * sometimes at a player. */
int32_t GAMEAPI ai_weapon0_ready(const uint8_t *car, int32_t last_laps)
{
    uint32_t me = MY_INDEX(car), i, count;
    const uint8_t *slot = SLOT(me);
    int32_t my_speed = S16(car, 0x658), leader_speed = 0, leader_at = 0, gap, laps = g_race[0x586];
    int32_t my_lap = slot[0x2c];
    int8_t leader = -1;
    uint8_t mask = car[0x6cb];
    uint32_t bit;

    if (slot[0x2d] == 1)
        return 0;
    if (I32(g_race, 0x57c) >= 6)
        return 1;
    count = g_race[0x54d];
    for (i = 0; i < count; i++) {
        const uint8_t *other = SLOT(i);

        if (other[0x2d] == 1) {
            leader = (int8_t)i;
            leader_at = SLOT_AT(other);
            leader_speed = S16(SLOT_CAR(other), 0x658);
            break;
        }
    }
    gap = leader_at - SLOT_AT(slot);
    bit = (1u << ((int32_t)leader & 31)) & mask;
    if (mask) {
        if (!bit)
            return 0;
    } else {
        if ((uint8_t)last_laps == 1 && I32(car, 0x6d0) < 10000 && (my_lap == laps - 2 || my_lap == laps - 1))
            return 0;
        if (g_no_leader_missiles)
            return 0;
    }
    if (gap <= 100 && my_speed >= leader_speed)
        return 0;
    if (g_ai_difficulty_mode == 0 && SLOT_CAR(SLOT(leader))[0x70c] != 8) {
        int32_t shift = g_difficulty == 0 ? 10 : g_difficulty == 1 ? 9 : g_difficulty <= 3 ? 8 : 0;

        return !((ai_random() >> 15 << shift) & 0xffff0000);
    }
    return 1;
}

/* 0x401ea0 (type 3): dropped for a car close by on the same stretch
 * (same tile, within 14 rows, moving): one just ahead (300..3000) fires
 * it forwards, one just behind (-3000..-300, harder drivers only) fires
 * it backwards (rear fire, +0x6b5 bit 0x80). A player has to have been
 * near for a while (+0x6ec per car, past the odds threshold). */
int32_t GAMEAPI ai_weapon3_ready(uint8_t *car, int32_t last_lap, int32_t time)
{
    uint32_t me = MY_INDEX(car), i, count;
    const uint8_t *slot = SLOT(me);
    uint8_t skill = car[0x6e1], laps = g_race[0x586], my_lap = slot[0x2c];
    uint16_t threshold = *(const uint16_t *)(0x4a7c68 + (U32(car, 0x6e8) >> 12 & 7) * 2);
    const uint16_t *seen = (const uint16_t *)(car + 0x6ec);
    int32_t forward[3], dir[3], at, ahead_near = 0, behind_near = 0, fire_ahead = 0, fire_behind = 0;

    body_forward(forward, car);
    line_direction(dir, (*(uint8_t **)(slot + 0x64))[5], *(const uint16_t *)(slot + 0x68));
    if (vec_dot(dir, forward) < 0xd1b4)
        return 0;
    if (I32(g_race, 0x57c) >= 6 && (int16_t)time > 100)
        return 1;
    at = SLOT_AT(slot);
    count = g_race[0x54d];
    for (i = 0; i < count; i++) {
        const uint8_t *other_slot = SLOT(i), *other;
        int32_t gap;

        if (i == me)
            continue;
        gap = gap_fold((int32_t)((uint32_t)other_slot[0x2c] << 16) - at + I32(other_slot, 0x34));
        other = SLOT_CAR(other_slot);
        if (gap > 300 && gap < 3000) {
            if (car[0x11c] != other[0x11c] || abs32(S16(car, 0x11e) - S16(other, 0x11e)) >= 0xe ||
                S16(other, 0x658) <= 100)
                continue;
            ahead_near = 1;
            if (other[0x70c] == 8)
                continue;
            if (!skill)
                return 0;
            if (seen[i] > threshold)
                fire_ahead = 1;
        } else if (gap < -300 && gap > -3000 && skill > 2) {
            if (car[0x11c] != other[0x11c] || abs32(S16(car, 0x11e) - S16(other, 0x11e)) >= 0xe ||
                S16(other, 0x658) <= 100)
                continue;
            behind_near = 1;
            if (other[0x70c] == 8)
                continue;
            if (seen[i] > threshold)
                fire_behind = 1;
        }
    }
    if (count) {
        if (fire_ahead)
            return 1;
        if (fire_behind)
            goto rear;
    }
    if (car[0x6cb])
        return 0;
    if ((uint8_t)last_lap == 1 && I32(car, 0x6d0) < 10000 && my_lap == laps - 1)
        return 0;
    if (ahead_near)
        return 1;
    if (!behind_near)
        return 0;
rear:
    ai_press_rear_fire(car);
    car[0x6b5] |= 0x80;
    return 1;
}

/* 0x402190 (type 7): at a moving car 100..6000 ahead within 6 tiles,
 * straight ahead if over 4000 away; easy drivers only sometimes at
 * players. Limited to the +0x6cb cars, and on the last lap only to them. */
int32_t GAMEAPI ai_weapon7_ready(uint8_t *car, int32_t last_lap, int32_t time)
{
    uint32_t me = MY_INDEX(car), i, mask = car[0x6cb];
    const uint8_t *slot = SLOT(me);
    uint8_t skill = car[0x6e1], tile;
    int32_t forward[3], dir[3], at, only_mask = 0;

    if ((uint8_t)last_lap == 1 && I32(car, 0x6d0) < 10000 && slot[0x2c] == (uint32_t)g_race[0x586] - 1) {
        if (!mask)
            return 0;
        only_mask = 1;
    }
    body_forward(forward, car);
    line_direction(dir, (*(uint8_t **)(slot + 0x64))[5], *(const uint16_t *)(slot + 0x68));
    if (vec_dot(dir, forward) < 0xd1b4)
        return 0;
    if (I32(g_race, 0x57c) >= 6 && (int16_t)time > 100)
        return 1;
    at = SLOT_AT(slot);
    tile = car[0x11c];
    for (i = 0; i < g_race[0x54d]; i++) {
        const uint8_t *other_slot = SLOT(i), *other;
        int32_t gap;

        if (i == me)
            continue;
        gap = (int32_t)((uint32_t)other_slot[0x2c] << 16) - at + I32(other_slot, 0x34);
        if (gap > 0x10000)
            gap += (int32_t)(-((uint32_t)(gap - 1) >> 16) << 16);
        if (gap <= 100 || gap > 6000)
            continue;
        other = SLOT_CAR(other_slot);
        if (S16(other, 0x658) < 0x32)
            continue;
        if (skill <= 2 && other[0x70c] != 8) {
            int32_t shift = g_difficulty == 0 ? 9 : g_difficulty == 1 ? 8 : g_difficulty <= 3 ? 7 : 0;

            if (ai_random() >> 15 << shift >> 16)
                continue;
        }
        if (abs32((int32_t)tile - other[0x11c]) > 6)
            continue;
        if (gap >= 4000) {
            int32_t d[3];

            d[0] = I32(other, POSITION) - I32(car, POSITION);
            d[1] = I32(other, POSITION + 4) - I32(car, POSITION + 4);
            d[2] = I32(other, POSITION + 8) - I32(car, POSITION + 8);
            vec_set_length(d, 0x10000);
            if (vec_dot(d, dir) <= 0xf03c)
                continue;
        }
        if (mask & 1u << (i & 31))
            return 1;
        if (!only_mask && !mask)
            return 1;
    }
    return 0;
}

/* 0x4024a0 (type 1): with flag 0x10, at a pickup-type target (slot +0x8c
 * == 4; +0x7a: 1 a point +0x6c, 2 an object +0x80) not already taken by
 * one of the 24 live pickups (0x6b9ce0) nor guarded by the track's
 * rules; fire if within 4000 and another car is near behind (or nearer
 * the target than this one, ahead). */
int32_t GAMEAPI ai_weapon1_ready(uint8_t *car, int32_t time)
{
    uint32_t me = MY_INDEX(car), kind;
    uint8_t *slot = SLOT(me);
    int32_t on_pad, dist, i;
    int16_t d[3];

    kind = *(const uint16_t *)(slot + 0x7a);
    if (!(car[0x6dc] & 0x10))
        return 0;
    if (I32(g_race, 0x57c) >= 6 && (int16_t)time > 0x32)
        return 1;
    if (I32(slot, 0x8c) != 4)
        return 0;
    for (i = 0; i < 0x18; i++) {
        const uint8_t *p = ((uint8_t **)0x6b9ce0)[i];

        if (I32(p, 0x184) != 1 || !I32(p, 0x188))
            continue;
        if (kind == 1) {
            if (I32(p, 0x194) == I32(slot, 0x6c) && I32(p, 0x198) == I32(slot, 0x70) &&
                I32(p, 0x19c) == I32(slot, 0x74))
                return 0;
        } else if (kind == 2 && I32(slot, 0x80) == I32(p, 0x190)) {
            return 0;
        }
    }
    on_pad = slot_on_pad(slot);
    if (kind == 1) {
        const uint8_t *rules = *(uint8_t **)0x6c1eb0;
        int32_t j;

        for (j = rules[0xf6] - 1; j >= 0; j--) {
            const uint8_t *e = *(uint8_t **)(rules + 0xf8) + j * 8;

            if (e[4] != 1 || U32(e, 0) != *(const uint16_t *)(*(uint8_t **)(slot + 0x84) + 0x192))
                continue;
            if (e[5] == 1 && on_pad == 1)
                return 0;
            if (car[0x6e0] >= e[6])
                return 0;
        }
    }
    d[0] = (int16_t)((uint32_t)(I32(slot, 0x6c) - I32(car, POSITION)) >> 16);
    d[1] = (int16_t)((uint32_t)(I32(slot, 0x70) - I32(car, POSITION + 4)) >> 16);
    d[2] = (int16_t)((uint32_t)(I32(slot, 0x74) - I32(car, POSITION + 8)) >> 16);
    dist = svec_length_sq(d);
    if (dist > 0xf42400)
        return 0;
    {
        int32_t at = SLOT_AT(slot), count = g_race[0x54d];

        for (i = 0; i < count; i++) {
            const uint8_t *other_slot = SLOT(i);
            int32_t gap;

            if ((uint32_t)i == me)
                continue;
            gap = gap_fold((int32_t)((uint32_t)other_slot[0x2c] << 16) - at + I32(other_slot, 0x34));
            if (abs32(gap) > 5000)
                continue;
            if (gap <= 0) {
                if (dist < 0x5f5e10)
                    return 1;
            } else {
                const uint8_t *other = SLOT_CAR(other_slot);

                d[0] = (int16_t)((uint32_t)(I32(other, POSITION) - I32(car, POSITION)) >> 16);
                d[1] = (int16_t)((uint32_t)(I32(other, POSITION + 4) - I32(car, POSITION + 4)) >> 16);
                d[2] = (int16_t)((uint32_t)(I32(other, POSITION + 8) - I32(car, POSITION + 8)) >> 16);
                if (svec_length_sq(d) < dist)
                    return 1;
            }
        }
    }
    return 0;
}

/* 0x402830 (type 2): aimed: at a car up to 6000 ahead and within 3000
 * units, where it will be (its line a few nodes on, at its speed, plus
 * its travel), if that is within the odds' cone ahead; else (no +0x6cb
 * cars, flag 0x10) at scenery or the 0x6d0e00 targets ahead on the
 * level. Easy settings only sometimes aim at players. */
int32_t GAMEAPI ai_weapon2_ready(uint8_t *car, int32_t time)
{
    uint32_t me = MY_INDEX(car), mask = car[0x6cb];
    uint8_t odds = (uint8_t)(U32(car, 0x6e8) >> 3 & 7);
    int32_t forward[3], at, i, blocked = 0;

    if (I32(g_race, 0x57c) >= 6 && (int16_t)time > 100)
        return 1;
    at = SLOT_AT(SLOT(me));
    body_forward(forward, car);
    for (i = 0; i < (int32_t)g_race[0x54d]; i++) {
        const uint8_t *other_slot = SLOT(i), *line = *(uint8_t **)(other_slot + 0x64), *other;
        int32_t threshold = *(const int32_t *)(0x4a7c18 + odds * 4), gap, dist, shift, aim[3], lead[3];
        uint32_t steps;
        uint16_t node;
        int16_t d[3];

        if ((uint32_t)i == me)
            continue;
        other = SLOT_CAR(other_slot);
        if (other[0x70c] != 8) {
            if (g_ai_difficulty_mode == 0)
                continue;
            if (g_ai_difficulty_mode == 1 && (ai_random() & 0x60000000))
                continue;
        }
        if (mask && !(mask & 1u << (i & 31)))
            continue;
        gap = gap_fold(SLOT_AT(other_slot) - at);
        if (gap < 0 || gap > 6000)
            continue;
        d[0] = (int16_t)((uint32_t)(I32(other, POSITION) - I32(car, POSITION)) >> 16);
        d[1] = (int16_t)((uint32_t)(I32(other, POSITION + 4) - I32(car, POSITION + 4)) >> 16);
        d[2] = (int16_t)((uint32_t)(I32(other, POSITION + 8) - I32(car, POSITION + 8)) >> 16);
        dist = svec_length_sq(d);
        if (dist > 0x895440)
            continue;
        if (dist < 1000000) {
            shift = 3;
            steps = 4;
        } else if (dist < 4000000) {
            shift = 4;
            steps = 6;
        } else {
            shift = 5;
            steps = 8;
        }
        if (!line_node_ahead(*(const uint16_t *)(other_slot + 0x68), line, steps, &node))
            continue;
        {
            const int16_t *p = (const int16_t *)NODE(line, node);

            lead[0] = (int32_t)((uint32_t)p[0] << 16) - I32(other, POSITION);
            lead[1] = (int32_t)((uint32_t)p[1] << 16) - I32(other, POSITION + 4);
            lead[2] = (int32_t)((uint32_t)p[2] << 16) - I32(other, POSITION + 8);
        }
        vec_set_length(lead, I32(other, 0x654));
        lead[0] = (int32_t)((uint32_t)lead[0] << shift);
        lead[1] = (int32_t)((uint32_t)lead[1] << shift);
        lead[2] = (int32_t)((uint32_t)lead[2] << shift);
        aim[0] = I32(other, POSITION) + (int32_t)((uint32_t)I32(other, VELOCITY) << shift) + lead[0] - I32(car, POSITION);
        aim[1] = I32(other, POSITION + 4) + (int32_t)((uint32_t)I32(other, VELOCITY + 4) << shift) + lead[1] -
                 I32(car, POSITION + 4);
        aim[2] = I32(other, POSITION + 8) + (int32_t)((uint32_t)I32(other, VELOCITY + 8) << shift) + lead[2] -
                 I32(car, POSITION + 8);
        vec_set_length(aim, 0x10000);
        if (vec_dot(aim, forward) > threshold)
            return 1;
    }
    if (mask || !(car[0x6dc] & 0x10))
        return 0;
    {
        int32_t on_pad = slot_on_pad(SLOT(me)), count = *(int32_t *)0x5f72a4;
        const int32_t *entry = *(int32_t **)(*(uint8_t **)0x6c1eb0 + 0xec);

        forward[1] = 0;
        vec_set_length(forward, 0x10000);
        for (i = 0; i < count; i++, entry += 5) {
            const uint8_t *rules = *(uint8_t **)0x6c1eb0;
            uint32_t k = rules[0xf6];
            const uint8_t *e = *(uint8_t **)(rules + 0xf8) + k * 8 - 8;
            int32_t threshold = *(const int32_t *)(0x4a7bf8 + odds * 4), v[3];
            const uint8_t *object = (const uint8_t *)(uintptr_t)entry[1];
            const uint8_t *shape = (const uint8_t *)(uintptr_t)entry[0];
            int16_t pos[3], rel[3];

            if ((U32(*(uint8_t **)(object + 0x194), (U32(shape, 0xc) & 0xf) * 4) & 0xf000000) == 0x5000000)
                continue;
            scenery_position(entry, pos);
            rel[0] = (int16_t)(pos[0] - (I32(car, POSITION) >> 16));
            rel[1] = (int16_t)(pos[1] - (I32(car, POSITION + 4) >> 16));
            rel[2] = (int16_t)(pos[2] - (I32(car, POSITION + 8) >> 16));
            if (svec_length_sq(rel) > 0x3d0900)
                continue;
            for (; k; k--, e -= 8)
                if (e[4] == 1 && U32(e, 0) == *(const uint16_t *)(object + 0x192) &&
                    ((e[5] == 1 && on_pad == 1) || car[0x6e0] >= e[6]))
                    blocked = 1;
            if (blocked == 1) {
                blocked = 0;
                continue;
            }
            v[0] = (int32_t)((uint32_t)rel[0] << 16);
            v[1] = 0;
            v[2] = (int32_t)((uint32_t)rel[2] << 16);
            vec_set_length(v, 0x10000);
            if (vec_dot(v, forward) > threshold)
                return 1;
        }
    }
    for (i = 0; i < (int32_t) * (uint8_t *)0x4b5e54; i++) {
        const uint8_t *target = *(uint8_t **)(*(uint8_t **)0x600664 + ((uint8_t *)0x6d0e00)[i] * 8);
        int32_t threshold = *(const int32_t *)(0x4a7bd8 + odds * 4), d[3];

        if (((int32_t *)0x6d0e40)[i] > 0x895440)
            continue;
        d[0] = I32(target, POSITION) - I32(car, POSITION);
        d[1] = I32(target, POSITION + 4) - I32(car, POSITION + 4);
        d[2] = I32(target, POSITION + 8) - I32(car, POSITION + 8);
        vec_set_length(d, 0x10000);
        if (vec_dot(d, forward) > threshold)
            return 1;
    }
    return 0;
}

/* Unit vector from a to the node, if it is not zero. */
static void node_direction(int32_t *out, const int16_t *node, const int32_t *a)
{
    out[0] = (int32_t)((uint32_t)node[0] << 16) - a[0];
    out[1] = (int32_t)((uint32_t)node[1] << 16) - a[1];
    out[2] = (int32_t)((uint32_t)node[2] << 16) - a[2];
    if (out[0] || out[1] || out[2])
        vec_set_length(out, 0x10000);
}

/* 0x402ed0 (type 4): on a long straight: the car facing along the line,
 * and the next node, 3 on and 10 on all lined up with it (tighter for
 * harder drivers), with no corner speed ahead; not near the end of the
 * race or while +0x6b4 bit 0x20. */
int32_t GAMEAPI ai_weapon4_ready(uint8_t *car, int32_t last_laps)
{
    const uint8_t *slot = SLOT(MY_INDEX(car)), *line;
    uint8_t line_index = (*(uint8_t **)(slot + 0x64))[5], skill = car[0x6e1];
    uint16_t node = *(const uint16_t *)(slot + 0x68), next;
    uint32_t laps = g_race[0x586], my_lap = slot[0x2c];
    int32_t forward[3], dir[3], v[3], at[3], threshold;
    const int16_t *p;

    line = LINE(line_index);
    if ((uint8_t)last_laps == 1 && I32(car, 0x6d0) < 10000 &&
        (my_lap == laps - 1 || my_lap == laps - 2 || (my_lap == laps && I32(slot, 0x34) < 0x8000)))
        return 0;
    if (car[0x6b4] & 0x20)
        return 0;
    if (ai_corner_ahead(car))
        return 0;
    body_forward(forward, car);
    line_direction(dir, line_index, node);
    if (vec_dot(forward, dir) < (skill > 2 ? 0x12dc : 0) + 0xddb3)
        return 0;
    if (line_next(node, line, &next))
        node = next;
    p = (const int16_t *)NODE(line, node);
    at[0] = (int32_t)((uint32_t)p[0] << 16);
    at[1] = (int32_t)((uint32_t)p[1] << 16);
    at[2] = (int32_t)((uint32_t)p[2] << 16);
    node_direction(v, p, VEC(car, POSITION));
    if (vec_dot(v, dir) < (skill > 2 ? 0xb8d : 0) + 0xf08f)
        return 0;
    if (!line_node_ahead(node, line, 3, &next))
        return 0;
    node_direction(v, (const int16_t *)NODE(line, next), at);
    threshold = (skill > 2 ? 0xf43 : 0) + 0xe803;
    if (vec_dot(v, dir) < threshold)
        return 0;
    if (!line_node_ahead(node, line, 10, &next))
        return 0;
    node_direction(v, (const int16_t *)NODE(line, next), at);
    return vec_dot(v, dir) >= threshold;
}

/* A weapon type's fire test. */
static int32_t weapon_ready(uint8_t *car, int32_t type, int32_t flag, int32_t time)
{
    switch (type) {
    case 0: return ai_weapon0_ready(car, flag);
    case 1: return ai_weapon1_ready(car, time);
    case 2: return ai_weapon2_ready(car, time);
    case 3: return ai_weapon3_ready(car, flag, time);
    case 4: return ai_weapon4_ready(car, flag);
    case 5: return ai_weapon5_ready(car);
    case 6: return ai_weapon6_ready(car, time);
    default: return ai_weapon7_ready(car, flag, time);
    }
}

/* A slot's countdown: running while it holds a weapon; when empty and
 * not already counting, a random delay (shorter on harder AI modes). */
static void weapon_timer(uint8_t *car, int32_t type, int32_t offset)
{
    if (type != -1) {
        S16(car, offset)++;
        return;
    }
    if (S16(car, offset) < 0)
        return;
    if (g_ai_difficulty_mode == 0)
        S16(car, offset) = (int16_t)-((ai_random() >> 26) + 8);
    else if (g_ai_difficulty_mode == 1)
        S16(car, offset) = (int16_t)-((ai_random() >> 27) + 4);
    else
        S16(car, offset) = (int16_t)-((ai_random() >> 28) + 2);
}

/* 0x401780: count the slots down, then (after 350 ticks) try each loaded
 * slot; with the same type in both, only the one held longer fires. In
 * race phases 6+ (after the finish) it waits, then only slot A. */
void GAMEAPI ai_weapons(uint8_t *car)
{
    const uint8_t *slot = SLOT(MY_INDEX(car));
    int32_t flag_a = U32(car, 0x6dc) >> 7 & 1, flag_b = U32(car, 0x6dc) >> 8 & 1;
    int32_t a = (int8_t)car[0x67c], b = (int8_t)car[0x67d], use_a = 1, use_b = 1;

    if ((slot[0x259] & 1) && I32(g_race, 0x57c) != 6)
        return;
    if (I32(g_race, 0x57c) >= 6) {
        int32_t t = I32(g_race, 0x580);

        if (t < 0x96)
            return;
        if (t < 0x12c)
            use_b = 0;
    }
    weapon_timer(car, a, 0x6d4);
    weapon_timer(car, b, 0x6d6);
    if (g_race_ticks < 0x15e)
        return;
    if (S16(car, 0x6d4) < 0)
        use_a = 0;
    if (S16(car, 0x6d6) < 0)
        use_b = 0;
    if (a & ~0x1f)
        use_a = 0;
    if (b & ~0x1f)
        use_b = 0;
    if (!((a ^ b) & 0x1f)) {
        if (S16(car, 0x6d4) < S16(car, 0x6d6))
            goto slot_b;
        use_b = 0;
    }
    if (use_a && (uint32_t)a <= 7 && weapon_ready(car, a, flag_a, S16(car, 0x6d4)))
        ai_press_fire_a(car);
slot_b:
    if (use_b && (uint32_t)b <= 7 && weapon_ready(car, b, flag_b, S16(car, 0x6d6)))
        ai_press_fire_b(car);
}
