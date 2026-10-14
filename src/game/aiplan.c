/* The AI's re-planning (0x4105e0, every 120 ticks per AI car, staggered
 * by car): a skill level (+0x6e1, 0 easiest .. 4), the nearest player
 * (+0x6ca, gap +0x6cc), and behaviour flags in +0x6dc rolled from the
 * car's odds (3-bit fields of +0x6e4 / +0x6e8, out of 8) on the AI's own
 * random generator (0x4a7148).
 *
 * The flag functions take the record at car +0x6b0 (so +0x2c is the
 * flags, +0x30 aggression, +0x31 skill, +0x34/+0x38 the odds). */
#include "../runtime.h"
#include "body.h"
#include "physics.h"
#include "gamefn.h"

#define g_race GP(0x4b1960)
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_ai_seed (*(uint32_t *)0x4a7148)
#define g_ai_difficulty_mode (*(int32_t *)0x4a714c)
#define g_difficulty (*(uint32_t *)0x6d0ed0)
#define g_brake_odds ((const uint16_t *)0x4a7bc8)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274

#define SLOT_OF(car) (g_car_slots + *(const uint16_t *)((car) + 0x64e) * CAR_SLOT_SIZE)
#define SLOT_CAR(slot) GP(slot)
#define SLOT_LAP(slot) ((slot)[0x2c])
#define SLOT_DISTANCE(slot) I32(slot, 0x34)
#define AI_FLAGS(rec) U32(rec, 0x2c)

static uint32_t ai_random(void)
{
    g_ai_seed = g_ai_seed * 16807 & 0x7fffffff;
    return g_ai_seed;
}

static void set_flag(uint8_t *rec, uint32_t bit, uint32_t on)
{
    AI_FLAGS(rec) = (on & 1) * bit | (AI_FLAGS(rec) & ~bit);
}

/* 0x410670: true with odds/8 (low byte; the rest is the random value). */
uint32_t GAMEAPI ai_chance(uint32_t odds)
{
    uint32_t r = ai_random() >> 15;

    return (r & 0xffffff00) | ((uint8_t)((r * 7) >> 16) < (uint8_t)odds);
}

/* 0x429800: the nearest player car (kind 10): the closest behind this
 * car if any are, else the closest ahead (gap = this car - that one,
 * laps in the high word). */
void GAMEAPI ai_nearest_player(uint8_t *car)
{
    uint32_t me = *(uint16_t *)(car + 0x64e), i, count = g_race[0x54d];
    const uint8_t *slot = g_car_slots + me * CAR_SLOT_SIZE;
    int32_t at = (int32_t)((uint32_t)SLOT_LAP(slot) << 16) + SLOT_DISTANCE(slot), best = 0x70000000;
    uint32_t nearest = 0xffffffff;

    for (i = 0; i < count; i++) {
        const uint8_t *other = g_car_slots + i * CAR_SLOT_SIZE;
        int32_t gap;

        if (i == me || SLOT_CAR(other)[0x70c] != 10)
            continue;
        gap = at - (int32_t)((uint32_t)SLOT_LAP(other) << 16) - SLOT_DISTANCE(other);
        if (best > 0 ? gap < best : gap <= 0 && gap > best) {
            best = gap;
            nearest = i;
        }
    }
    car[0x6ca] = (uint8_t)nearest;
    I32(car, 0x6cc) = best;
}

static int32_t gap_to(const uint8_t *car, int32_t index)
{
    const uint8_t *slot = SLOT_OF(car), *other = g_car_slots + index * CAR_SLOT_SIZE;

    return (int32_t)((uint32_t)(SLOT_LAP(other) - SLOT_LAP(slot)) << 16) - SLOT_DISTANCE(slot) +
           SLOT_DISTANCE(other);
}

/* 0x410df0: the skill level: fixed by the AI mode, or the difficulty
 * setting; raised when the car it paces for (+0x6e3) is well ahead. */
void GAMEAPI ai_set_skill(uint8_t *car)
{
    int32_t leader = (int8_t)car[0x6e3], gap;

    if (g_ai_difficulty_mode == 2) {
        car[0x6e1] = 4;
        return;
    }
    if (g_ai_difficulty_mode == 1) {
        int32_t limit = g_race[0x1c + *(uint16_t *)(car + 0x64e)] == 6 ? -8000 : -16000;

        car[0x6e1] = (int8_t)car[0x6ca] != -1 && I32(car, 0x6cc) < limit ? 4 : 3;
    }
    if (g_ai_difficulty_mode == 0)
        car[0x6e1] = g_difficulty == 0 ? 0 : g_difficulty == 1 ? 1 : 2;
    if (leader == -1)
        return;
    gap = gap_to(car, leader);
    if (gap > 32000)
        car[0x6e1] += 2;
    else if (gap > 16000)
        car[0x6e1]++;
    if (car[0x6e1] > 4)
        car[0x6e1] = 4;
}

/* 0x410910: flag 1, not in the first 350 ticks. */
void GAMEAPI ai_roll_flag1(uint8_t *rec)
{
    uint32_t odds = U32(rec, 0x34) >> 6 & 7;

    if (g_race_ticks < 0x15e) {
        AI_FLAGS(rec) &= ~1u;
        return;
    }
    set_flag(rec, 1, ai_chance(odds));
}

/* 0x410960: flag 2, likelier on the last lap (more so past halfway). */
void GAMEAPI ai_roll_flag2(uint8_t *car)
{
    const uint8_t *slot = SLOT_OF(car);
    uint8_t odds = (uint8_t)(U32(car, 0x6e4) >> 9 & 7);

    if (g_race_ticks < 0x15e) {
        U32(car, 0x6dc) &= ~2u;
        return;
    }
    if (odds) {
        uint8_t extra = 0;

        if (SLOT_LAP(slot) == g_race[0x586])
            extra = (uint8_t)((SLOT_DISTANCE(slot) >= 0x8000) + 1);
        odds = (uint8_t)(odds + extra);
        if (odds > 7)
            odds = 7;
    }
    U32(car, 0x6dc) = (ai_chance(odds) & 1) << 1 | (U32(car, 0x6dc) & ~2u);
}

/* 0x410a20, 0x410ab0: odds shifted by how far behind (up) or ahead
 * (down) the nearest player is. */
static int8_t odds_by_player_gap(const uint8_t *rec, uint8_t odds)
{
    int32_t gap = I32(rec, 0x1c);
    int8_t shift = 0, total;

    if ((int8_t)rec[0x1a] != -1 && odds) {
        if (gap < -0x8000)
            shift = 2;
        else if (gap < -0x4000)
            shift = 1;
        else if (gap > 0x8000)
            shift = -2;
        else if (gap > 0x4000)
            shift = -1;
    }
    total = (int8_t)(odds + shift);
    if (total > 7)
        return 7;
    if (total < 0)
        return 0;
    return total;
}

void GAMEAPI ai_roll_flag4(uint8_t *rec)
{
    set_flag(rec, 4, ai_chance((uint8_t)odds_by_player_gap(rec, (uint8_t)(U32(rec, 0x34) >> 15 & 7))));
}

void GAMEAPI ai_roll_flag8(uint8_t *rec)
{
    set_flag(rec, 8, ai_chance((uint8_t)odds_by_player_gap(rec, (uint8_t)(U32(rec, 0x34) >> 18 & 7))));
}

/* 0x410b40, 0x410b80, 0x410bd0 */
void GAMEAPI ai_roll_flag10(uint8_t *rec)
{
    set_flag(rec, 0x10, ai_chance(U32(rec, 0x34) >> 12 & 7));
}

void GAMEAPI ai_roll_flag20(uint8_t *rec)
{
    if (g_race_ticks < 0x15e) {
        AI_FLAGS(rec) |= 0x20;
        return;
    }
    set_flag(rec, 0x20, ai_chance(U32(rec, 0x34) >> 21 & 7));
}

void GAMEAPI ai_roll_flag40(uint8_t *rec)
{
    set_flag(rec, 0x40, ai_chance(U32(rec, 0x34) >> 27 & 7));
}

/* 0x410c10: flags 0x80 and 0x100, each with odds from a table. */
void GAMEAPI ai_roll_flags80(uint8_t *rec)
{
    uint16_t odds = g_brake_odds[rec[0x38] & 7];
    uint32_t a = ai_random(), b = ai_random();

    set_flag(rec, 0x80, (uint16_t)(a >> 15) < odds);
    set_flag(rec, 0x100, (uint16_t)(b >> 15) < odds);
}

/* 0x410cb0: flag 0x200 (cut ahead a node at speed): 1 in 4 for easy
 * drivers, 1 in 8 for skill 3, rarer on track type 6; never for 4. */
void GAMEAPI ai_roll_flag200(uint8_t *car)
{
    uint8_t skill = car[0x6e1];

    if (skill != 4) {
        int32_t shift = skill <= 2 ? 2 : skill == 3 ? 3 : 0xf;

        if (g_race[0x1c + *(uint16_t *)(car + 0x64e)] == 6)
            shift++;
        if (!(uint8_t)(((ai_random() >> 15) << shift) >> 16)) {
            U32(car, 0x6dc) |= 0x200;
            return;
        }
    }
    U32(car, 0x6dc) &= ~0x200u;
}

/* 0x410750: flag 0x400, less likely near a player (unless refreshing);
 * off in race mode 2 for cars without slot bit 0x259. */
void GAMEAPI ai_roll_flag400(uint8_t *car, int32_t refresh)
{
    uint32_t me = *(uint16_t *)(car + 0x64e);
    const uint8_t *slot = g_car_slots + me * CAR_SLOT_SIZE;
    uint8_t odds = (uint8_t)(U32(car, 0x6e8) >> 18 & 7), near = 0, less;

    if (g_race[0x585] == 2 && g_race[0x58c] != 1 && !(slot[0x259] & 1)) {
        U32(car, 0x6dc) &= ~0x400u;
        return;
    }
    if (!refresh) {
        int32_t at = (int32_t)((uint32_t)SLOT_LAP(slot) << 16) + SLOT_DISTANCE(slot);
        uint32_t i, count = g_race[0x54d];

        for (i = 0; i < count; i++) {
            const uint8_t *other = g_car_slots + i * CAR_SLOT_SIZE;
            int32_t gap;

            if (i == me || SLOT_CAR(other)[0x70c] == 8)
                continue;
            gap = (int32_t)((uint32_t)SLOT_LAP(other) << 16) - at + SLOT_DISTANCE(other);
            if (gap > 0x8000)
                gap += (int32_t)(-((uint32_t)(gap + 0x7fff) >> 16) << 16);
            if (gap < -0x8000)
                gap += (int32_t)((0x7fffu - (uint32_t)gap) >> 16 << 16);
            if (gap > 1000) {
                if (near < 1)
                    near = 1;
            } else if (gap > 200) {
                if (near < 2)
                    near = 2;
            } else if (gap > -3000) {
                if (near < 3)
                    near = 3;
            } else if (near < 1) {
                near = 1;
            }
        }
        less = near == 1 ? 5 : near == 2 ? 3 : 0;
        odds = odds > less ? (uint8_t)(odds - less) : 0;
    }
    U32(car, 0x6dc) = (ai_chance(odds) & 1) << 10 | (U32(car, 0x6dc) & ~0x400u);
}

/* 0x4106c0: flag 0x800, only with a player near enough ahead for the
 * skill. */
void GAMEAPI ai_roll_flag800(uint8_t *rec)
{
    uint8_t odds = (uint8_t)(U32(rec, 0x38) >> 21 & 7);
    int32_t no_player = (int8_t)rec[0x1a] == -1, gap = I32(rec, 0x1c);

    if (rec[0x31] == 4) {
        if (no_player || gap < 0x4000) {
            AI_FLAGS(rec) &= ~0x800u;
            return;
        }
    } else if (rec[0x31] == 3) {
        if (no_player || gap < 0x800) {
            AI_FLAGS(rec) &= ~0x800u;
            return;
        }
    } else if (!no_player && gap < -0x1000) {
        AI_FLAGS(rec) &= ~0x800u;
        return;
    }
    set_flag(rec, 0x800, ai_chance(odds));
}

/* 0x410300: aggression (+0x30): 255 less a random amount whose spread
 * grows with the 3-bit level (+0x34 bits 24..26); level 7 is 128 less
 * up to 127. */
void GAMEAPI ai_roll_aggression(uint8_t *rec)
{
    uint32_t a, b, c;

    switch (U32(rec, 0x34) >> 24 & 7) {
    case 0:
        rec[0x30] = 0xff;
        break;
    case 1:
        a = ai_random();
        b = ai_random();
        rec[0x30] = (uint8_t)(0xff - (b >> 26) - (a >> 26));
        break;
    case 2:
        a = ai_random();
        b = ai_random();
        rec[0x30] = (uint8_t)(0xff - (b >> 26) - (a >> 25));
        break;
    case 3:
        a = ai_random();
        b = ai_random();
        rec[0x30] = (uint8_t)(0xff - (b >> 25) - (a >> 25));
        break;
    case 4:
        a = ai_random();
        b = ai_random();
        c = ai_random();
        rec[0x30] = (uint8_t)(0xff - (c >> 26) - (uint8_t)((a >> 25) + 0x1f + (b >> 25)));
        break;
    case 5:
        a = ai_random();
        b = ai_random();
        rec[0x30] = (uint8_t)(0xff - (b >> 25) - (uint8_t)((a >> 24) + 0x3f));
        break;
    case 6:
        a = ai_random();
        b = ai_random();
        rec[0x30] = (uint8_t)(0xff - (b >> 26) - (uint8_t)((a >> 24) + 0x5e));
        break;
    case 7:
        a = ai_random();
        rec[0x30] = (uint8_t)(0x80 - (a >> 24));
        break;
    }
}

/* 0x410f40: +0x6e2 (15 when refreshing or at skill 4), by the gap to the
 * nearest player; untouched in the first 378 ticks. */
void GAMEAPI ai_set_gap_level(uint8_t *car, int32_t refresh)
{
    int32_t gap = I32(car, 0x6cc);
    uint8_t skill = car[0x6e1];

    if (refresh || skill == 4) {
        car[0x6e2] = 0xf;
        return;
    }
    if (g_race_ticks < 0x17a)
        return;
    if ((int8_t)car[0x6ca] == -1) {
        car[0x6e2] = 7;
        return;
    }
    car[0x6e2] = gap > 0x4000 ? 5 : gap > 0x2000 ? 6 : gap > -0x8000 ? 7 : 8;
    if (skill == 2 || skill == 3)
        car[0x6e2]++;
    if (g_race[0x1c + *(uint16_t *)(car + 0x64e)] == 6)
        car[0x6e2]++;
}

/* 0x4105e0 */
void GAMEAPI ai_replan(uint8_t *car, int32_t refresh)
{
    uint8_t *rec = car + 0x6b0;

    if (refresh)
        car[0x6ca] = 0xff;
    else
        ai_nearest_player(car);
    ai_set_skill(car);
    ai_roll_flag1(rec);
    ai_roll_flag2(car);
    ai_roll_flag4(rec);
    ai_roll_flag8(rec);
    ai_roll_flag10(rec);
    ai_roll_flag20(rec);
    ai_roll_flag40(rec);
    ai_roll_flags80(rec);
    ai_roll_flag200(car);
    ai_roll_flag400(car, refresh);
    ai_roll_flag800(rec);
    ai_roll_aggression(rec);
    ai_set_gap_level(car, refresh);
}
