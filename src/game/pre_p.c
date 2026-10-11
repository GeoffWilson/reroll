/* Before the world step (sim_step 0x444120), group P: the race order
 * (0x43d350) and what a lap or the finish leads to.
 *
 * Each step every car's nearest racing line node is refound (0x47ae40), its
 * checkpoints are followed (0x43d8e0, post_f.c) and the cars are ordered
 * by lap and lap distance with one bubble pass from the back, so places
 * change one at a time; a car that has finished (+0x259 bit 0) keeps its
 * place.
 *
 * Reached from the lap logic (post_f.c, by address): lap split times and
 * records (0x43d750, BCD times 0x439c70), the results table (0x439a70), the
 * weapon given for a lap (0x467490, 0x467880), handing a finished player's
 * car to the AI (0x47fbe0), the replay / ghost recorder (0x41ba20, its
 * checksum 0x41c190, 0x41c160), the end of race fade (0x40afb0, 0x472130,
 * 0x472290) and a few network state getters.
 *
 * Still original: network messages (0x45e800 and the 0x48c... transport,
 * 0x45fc40 / 0x45fca0, 0x4600a0 with its sprintf), the force feedback stop
 * 0x489f70 (DirectInput) and everything owned by other groups, called by
 * address. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_track (*(uint8_t **)0x6c1e30)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_sim_ticks (*(uint32_t *)0x6c0a50)
#define g_misc_seed (*(uint32_t *)0x4b11dc)
#define g_results_seed (*(uint32_t *)0x4b11d8)
#define g_net_flags (*(uint32_t *)0x5d622c)
#define g_replay_off (*(int32_t *)0x6b8d60)

#define I32(p, off) (*(int32_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define PTR(p, off) (*(uint8_t **)((p) + (off)))
#define SHL(x, n) ((int32_t)((uint32_t)(int32_t)(x) << (n)))

/* Not ours. */
typedef int32_t(GAMEAPI *checkpoints_fn)(uint8_t *slot, uint32_t probe);
typedef void(GAMEAPI *slot_fn)(uint8_t *slot);
typedef uint32_t(GAMEAPI *clock_fn)(void);
typedef void(GAMEAPI *digits_fn)(int32_t all, uint8_t *slot);
typedef int32_t(GAMEAPI *div_fn)(int32_t a, int32_t b);
typedef void(GAMEAPI *award_send_fn)(uint32_t car, uint32_t which, uint32_t kind);
#define checkpoints_follow ((checkpoints_fn)0x43d8e0)
#define rejoin_plan ((slot_fn)0x47fbb0)
#define clock_now ((clock_fn)0x4351c0)
#define time_digits_show ((digits_fn)0x439910)
#define div16 ((div_fn)0x42df70)
#define award_send ((award_send_fn)0x45e800)

/* --- small getters --- */

/* 0x45eed0, 0x45ef30, 0x45ef80: network state bits. */
uint32_t GAMEAPI net_state_host(void)
{
    return g_net_flags >> 7 & 1;
}

uint32_t GAMEAPI net_state_bit5(void)
{
    return g_net_flags >> 5 & 1;
}

uint32_t GAMEAPI net_state_game(void)
{
    return g_net_flags & 1;
}

/* 0x45ca10: a car's index. */
uint32_t GAMEAPI car_index_of(const uint8_t *car)
{
    return U16(car, 0x64e);
}

/* 0x48ce80 */
uint32_t GAMEAPI net_session_kind(void)
{
    return *(uint32_t *)0x5d5358;
}

/* 0x48cf20: the local player's car, 0 when unset (-1). Read twice, as the
 * original does. */
int32_t GAMEAPI net_local_slot(void)
{
    if (*(volatile int32_t *)0x5d535c == -1)
        return 0;
    return *(volatile int32_t *)0x5d535c;
}

/* 0x40a9a0 */
void GAMEAPI race_end_signal(void)
{
    *(uint16_t *)0x6cf6d2 = (uint16_t)(*(uint32_t *)0x6cf6d4 + 1);
}

/* 0x472290: one of four voice settings records. */
void GAMEAPI voice_slot_set(int32_t i, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    if (i < 0 || i >= 4)
        return;
    ((uint32_t *)0x4d4600)[i] = a;
    ((uint32_t *)0x4d4610)[i] = b;
    ((uint32_t *)0x4d4620)[i] = c;
    ((uint32_t *)0x4d4630)[i] = d;
}

/* 0x472130: put a voice settings record back to its defaults (the low byte
 * of `which` is used; the flag is cleared even past the four). */
void GAMEAPI voice_slot_reset(uint32_t which)
{
    which &= 0xff;
    ((uint32_t *)0x4d45b0)[which] = 0;
    switch (which) {
    case 0:
        voice_slot_set(0, *(uint32_t *)0x4d458c, *(uint32_t *)0x4d4570, *(uint32_t *)0x4d4560, 0);
        break;
    case 1:
        voice_slot_set(1, *(uint32_t *)0x4d4588, *(uint32_t *)0x4d457c, *(uint32_t *)0x4d456c, 0);
        break;
    case 2:
        voice_slot_set(2, *(uint32_t *)0x4d4584, *(uint32_t *)0x4d4578, *(uint32_t *)0x4d4568, 0);
        break;
    case 3:
        voice_slot_set(3, *(uint32_t *)0x4d4580, *(uint32_t *)0x4d4574, *(uint32_t *)0x4d4564, 0);
        break;
    }
}

/* 0x40afb0: the end of race fade flag (bit 2 of 0x6cce40); turning it off
 * resets the four voice settings. Off is ignored in camera mode 0xe. */
void GAMEAPI race_end_fade_set(uint32_t on)
{
    on &= 1;
    if (!on && *(int32_t *)0x6cf6a0 == 0xe)
        return;
    *(uint32_t *)0x6cce40 = on << 2 | (*(uint32_t *)0x6cce40 & ~4u);
    if (!on) {
        voice_slot_reset(0);
        voice_slot_reset(1);
        voice_slot_reset(2);
        voice_slot_reset(3);
    }
}

/* --- race order --- */

/* 0x47ae40: the racing line node nearest the car among the slot's node, its
 * neighbours and the nodes their links lead to, or the node of the car's
 * track cell if that is nearer. Links into a line that does not loop are
 * range checked against the slot's own line's node count (sic). */
void GAMEAPI slot_nearest_line_node(uint8_t *slot)
{
    uint8_t *line = PTR(slot, 0x64), *car, *link1 = 0, *link2 = 0, *best_line = 0, *cand, *lines, *node;
    uint32_t start, flags, v;
    int32_t x, y, z, best = 0x7fffffff, i, ci, idx1 = 0, idx2 = 0, best_idx = 0, count = 0, k, j, d;

    if (!line)
        return;
    car = PTR(slot, 0);
    x = I32(car, 0x34) >> 16;
    y = I32(car, 0x38) >> 16;
    z = I32(car, 0x3c) >> 16;
    start = U32(slot, 0x68);
    for (k = 0; k < 3; k++) {
        i = (int32_t)(k == 1 ? start + 1 : k == 2 ? start - 1 : start);
        if (line[4]) {
            if (i < 0)
                i += U16(line, 0);
            count = U16(line, 0);
            if (i >= count)
                i -= count;
        } else {
            if (i < 0)
                continue;
            count = U16(line, 0);
            if (i >= count)
                continue;
        }
        flags = U32(PTR(line, 8) + (uint32_t)i * 16, 0xc);
        if (flags & 0x1f) {
            link1 = PTR(g_track, 0x60) + (flags & 0x1f) * 0xc;
            idx1 = (int32_t)(flags >> 5 & 0x1ff);
        } else {
            link1 = 0;
        }
        if (flags & 0x7c000) {
            idx2 = (int32_t)(flags >> 19 & 0x1ff);
            link2 = PTR(g_track, 0x60) + (flags >> 14 & 0x1f) * 0xc;
        } else {
            link2 = 0;
        }
        for (j = 0; j < 3; j++) {
            if (j == 1) {
                ci = idx1;
                cand = link1;
            } else if (j == 2) {
                ci = idx2;
                cand = link2;
            } else {
                ci = i;
                cand = line;
            }
            if (!cand)
                continue;
            if (cand[4]) {
                if (ci < 0)
                    ci += U16(cand, 0);
                if (ci >= U16(cand, 0))
                    ci -= U16(cand, 0);
            } else if (ci < 0 || ci >= count) {
                continue;
            }
            node = PTR(cand, 8) + (uint32_t)ci * 16;
            d = (S16(node, 4) - z) * (S16(node, 4) - z) + (S16(node, 2) - y) * (S16(node, 2) - y) +
                (S16(node, 0) - x) * (S16(node, 0) - x);
            if (d < best) {
                best_line = cand;
                best_idx = ci;
                best = d;
            }
        }
    }
    lines = PTR(g_track, 0x60);
    v = U32(PTR(g_track, 0x7c), (*(uint32_t *)((uint32_t)PTR(car, 0xc) & ~1u) >> 22) * 4);
    cand = lines + (v >> 16) * 0xc;
    ci = (int32_t)(v & 0xffff);
    node = PTR(cand, 8) + (uint32_t)ci * 16;
    d = (S16(node, 4) - z) * (S16(node, 4) - z) + (S16(node, 2) - y) * (S16(node, 2) - y) +
        (S16(node, 0) - x) * (S16(node, 0) - x);
    if (d > best) {
        cand = best_line;
        ci = best_idx;
    }
    PTR(slot, 0x64) = cand;
    I32(slot, 0x68) = ci;
}

/* 0x43d350: once per step, the race order (slot +0x2d, 1 first). Not in
 * modes 3 and 8 or on tracks without lap logic. The places must be 1..n
 * (the original's stack table is indexed by them). */
void GAMEAPI race_positions_update(void)
{
    uint8_t *race = g_race, *slot, *by_place[257] = {0}, **order = by_place + 1, *t;
    uint32_t n = race[0x54d], i;
    int32_t key[256], p, kt;

    if (!g_track[0x44] || race[0x551] == 3 || race[0x551] == 8)
        return;
    for (i = 0, slot = g_car_slots; i < n; i++, slot += CAR_SLOT_SIZE)
        slot_nearest_line_node(slot);
    for (i = 0, slot = g_car_slots; i < n; i++, slot += CAR_SLOT_SIZE) {
        checkpoints_follow(slot, 0);
        by_place[slot[0x2d]] = slot;
    }
    for (i = 0; i < n; i++)
        key[i] = SHL(order[i][0x2c], 16) + I32(order[i], 0x34);
    for (p = (int32_t)n - 2; p >= 0; p--) {
        if (order[p][0x259] & 1)
            continue;
        if (key[p + 1] > key[p]) {
            t = order[p + 1];
            order[p + 1] = order[p];
            order[p] = t;
            kt = key[p];
            key[p] = key[p + 1];
            key[p + 1] = kt;
        }
    }
    for (p = (int32_t)n - 1; p >= 0; p--)
        order[p][0x2d] = (uint8_t)(p + 1);
}

/* --- lap and race times --- */

/* 0x439c70: a lap or race time (>> 11 a count of 1/100 s) as six digits
 * packed one per nibble for the time displays; -1 and anything past the
 * range show as 0x595999. */
uint32_t GAMEAPI race_time_digits(int32_t t)
{
    int32_t c = t >> 11, d;
    uint32_t r = 0, i;

    if (t == -1 || t >= 0x4650000)
        return 0x595999;
    for (i = 0; i < 6; i++) {
        r <<= 4;
        switch (i) {
        case 0:
            d = c / 6000 % 10;
            break;
        case 1:
            d = c / 600 % 10;
            break;
        case 2:
            d = c / 100 % 6;
            break;
        case 3:
            d = c / 10 % 10;
            break;
        case 4:
            d = c % 10;
            break;
        default:
            d = (int32_t)(((uint32_t)(t >> 4) * 10 >> 7) % 10);
            break;
        }
        r |= (uint32_t)d;
    }
    return r;
}

/* 0x43d750: a lap ended: the best lap (+0x40, -1 none) and the last lap
 * (+0x48) from the lap time +0x3c, the race clock (+0x5c), the shown
 * digits, and the records: in time trial (mode 1) the car's five best laps
 * (race +0x30 + car * 0xdc, sorted), otherwise the lap's split. */
void GAMEAPI slot_lap_record(uint8_t *slot)
{
    uint32_t t = U32(slot, 0x3c), digits, *best, car, e, m;

    if (I32(slot, 0x40) == -1 || t < U32(slot, 0x40))
        U32(slot, 0x40) = t;
    U32(slot, 0x48) = t;
    U32(slot, 0x5c) = clock_now();
    time_digits_show(1, slot);
    if (!slot[0x2c] || g_race[0x58c])
        return;
    if (g_race[0x551] == 1) {
        car = U16(PTR(slot, 0), 0x64e);
        digits = race_time_digits(I32(slot, 0x48));
        best = (uint32_t *)(g_race + car * 0xdc + 0x30);
        for (e = 0; e < 5; e++) {
            if (digits < best[e]) {
                for (m = 4; m > e; m--)
                    best[m] = best[m - 1];
                best[e] = digits;
                return;
            }
        }
        return;
    }
    digits = race_time_digits(I32(slot, 0x48));
    U32(g_race, ((uint32_t)U16(PTR(slot, 0), 0x64e) * 0x37 + slot[0x2c]) * 4 + 0x2c) = digits;
}

/* 0x439a70: the results table. At the end (`final` 1, or race state 7) the
 * cars still racing get a race time projected from their progress (+0x44),
 * and a car placed behind another with a smaller time gets a random bit
 * more. Then every car's three times (race +0x24 + car * 0xdc) and place. */
void GAMEAPI race_results_update(uint32_t final)
{
    uint8_t *slot, *by_place[257] = {0}, *cur, *prev;
    uint32_t i, k, laps, v;
    int32_t e, q;
    int64_t product;

    if (final == 1 || I32(g_race, 0x57c) == 7) {
        laps = g_race[0x586];
        for (i = 0, slot = g_car_slots; i < g_race[0x54d]; i++, slot += CAR_SLOT_SIZE) {
            if (slot[0x2c] > laps)
                continue;
            e = SHL(slot[0x2c] + 0xffff, 16) + I32(slot, 0x34);
            if (e == 0) {
                v = 0xfffffff;
            } else {
                q = div16(SHL(laps, 16), e);
                product = (int64_t)q * I32(slot, 0x44);
                v = (uint32_t)product >> 16 | (uint32_t)((uint64_t)product >> 32) << 16;
            }
            U32(slot, 0x44) = v;
        }
        for (i = g_race[0x54d], slot = g_car_slots; i; i--, slot += CAR_SLOT_SIZE)
            by_place[slot[0x2d]] = slot;
        for (k = 1; k < g_race[0x54d]; k++) {
            cur = by_place[k + 1];
            prev = by_place[k];
            if (U32(cur, 0x44) < U32(prev, 0x44)) {
                g_results_seed = g_results_seed * 16807 & 0x7fffffff;
                U32(cur, 0x44) += g_results_seed >> 16 & 0xfff;
            }
        }
    }
    for (i = 0, slot = g_car_slots; i < g_race[0x54d]; i++, slot += CAR_SLOT_SIZE) {
        for (k = 0; k < 3; k++) {
            v = U32(slot, 0x3c + k * 4);
            if (k == 2 && (g_race[0x551] == 1 || I32(g_race, 0x57c) > 7))
                continue;
            U32(g_race, (9 + 0x37 * i + k) * 4) = race_time_digits((int32_t)v);
        }
        g_race[0x10 + i] = slot[0x2d];
    }
}

/* --- finishing --- */

/* 0x47fbe0: a player's car crossed the line: the AI drives it from now on,
 * with skill levels looked up from its control record. */
void GAMEAPI slot_finish_to_ai(uint8_t *slot)
{
    uint8_t *car = PTR(slot, 0);
    uint32_t k;

    car[0x70c] = 8;
    car[0x718] = 0;
    for (k = 0x20; k; k--)
        if (car[0x6f8] >= ((const uint8_t *)0x4a7e78)[k]) {
            car[0x718] = (uint8_t)k;
            break;
        }
    car[0x719] = 0;
    for (k = 0x1f; k; k--)
        if (car[0x6f9] >= ((const uint8_t *)0x4a7e9c)[k]) {
            car[0x719] = (uint8_t)k;
            break;
        }
    car[0x71a] = 0;
    for (k = 0xf; k; k--)
        if (car[0x6fa] >= ((const uint8_t *)0x4a7ebc)[k]) {
            car[0x71a] = (uint8_t)k;
            break;
        }
    slot_nearest_line_node(slot);
    rejoin_plan(slot);
    U16(car, 0x6b4) = 0;
    U16(car, 0x6b6) = 0;
    car[0x6b8] = 0;
    car[0x6b9] = 0;
    car[0x6ba] = 0;
    car[0x6bb] = 0;
    U32(car, 0x6bc) = 0;
    U32(car, 0x6c0) = 0;
    U32(car, 0x6c4) = 0;
    car[0x6c8] = 0;
    car[0x6c9] = (uint8_t)((U16(PTR(slot, 0), 0x64e) + g_sim_ticks + 1) % 6);
    *(uint8_t *)0x4b820c = 0;
    *(uint8_t *)0x4b8228 = 0;
    *(uint8_t *)0x4b820d = 0;
    *(uint8_t *)0x4b8229 = 0;
}

/* 0x467880: give the car weapon `kind` in its first free item slot (+0x67d,
 * then +0x67c; 0xff free) and tell the other players. In a network game
 * only the local car's own. */
void GAMEAPI car_award_weapon(uint8_t *car, uint32_t kind)
{
    if (net_state_host() && (int32_t)car_index_of(car) != net_local_slot())
        return;
    if (car[0x67d] == 0xff) {
        car[0x67d] = (uint8_t)kind;
        award_send(car_index_of(car), 0, kind);
        return;
    }
    if (car[0x67c] == 0xff) {
        car[0x67c] = (uint8_t)kind;
        award_send(car_index_of(car), 1, kind);
    }
}

static int32_t items_held(uint32_t kind)
{
    uint8_t *slot = g_car_slots, *car;
    uint32_t n = g_race[0x54d];
    int32_t count = 0;

    for (; n; n--, slot += CAR_SLOT_SIZE) {
        car = PTR(slot, 0);
        if (car[0x67c] == kind)
            count++;
        if (car[0x67d] == kind)
            count++;
    }
    return count;
}

/* 0x467490: the weapon a lap earns: a random draw (0x4b11dc) walks the
 * eight kinds with chances by race mode, place and gap; some kinds are
 * limited in how many may be held at once, and after nine refusals it is
 * kind 4. */
void GAMEAPI slot_lap_weapon(uint8_t *slot)
{
    uint8_t *car = PTR(slot, 0), *race, *other, *s, *c;
    int32_t place = slot[0x2d] - 1, r, d, i, count, self;
    uint32_t k = 0, tries = 0, chance, n;

    g_misc_seed = g_misc_seed * 16807 & 0x7fffffff;
    r = (int32_t)((g_misc_seed >> 15) * 0x7fff >> 16) % 100;
    for (;;) {
        race = g_race;
        switch (race[0x551]) {
        case 1:
        case 7:
            k = 4;
            chance = 100;
            break;
        case 2:
            other = (uint8_t *)(0x6bfdf4 - U16(PTR(slot, 0), 0x64e) * CAR_SLOT_SIZE);
            d = SHL(slot[0x2c], 16) - SHL(other[0x2c], 16) - I32(other, 0x34) + I32(slot, 0x34);
            if (d > 0x7fff)
                chance = ((const uint8_t *)0x4b17f8)[(int32_t)k];
            else if (d > 0)
                chance = ((const uint8_t *)0x4b17f8)[(int32_t)k + 8];
            else
                chance = ((const uint8_t *)0x4b17f8)[(int32_t)k + 8 * (2 + (d <= -0x7fff))];
            break;
        case 3:
        case 8:
            chance = ((const uint8_t *)0x4b1818)[(int32_t)k];
            break;
        case 5:
        case 6:
            if (PTR(slot, 0)[0x70c] == 8) {
                chance = ((const uint8_t *)0x4b17c8)[(int32_t)k + 8 * place];
            } else {
                uint32_t e = net_session_kind();

                chance = ((const uint8_t *)0x4b1820)[(int32_t)k + 8 * (place + 6 * (int32_t)e)];
            }
            break;
        default:
            if (PTR(slot, 0)[0x70c] == 8)
                chance = ((const uint8_t *)0x4b17c8)[(int32_t)k + 8 * place];
            else
                chance = ((const uint8_t *)0x4b1798)[(int32_t)k + 8 * place];
            break;
        }
        if (r < (int32_t)chance) {
            int ok = 0;

            race = g_race;
            n = race[0x54d];
            switch (k) {
            case 0:
                ok = *(uint8_t *)0x6b9d42 + items_held(0) < 2;
                break;
            case 1:
            case 4:
            case 5:
                ok = 1;
                break;
            case 2:
                count = *(uint8_t *)0x6b9d41;
                ok = -1;
                for (i = 0, s = g_car_slots; i < (int32_t)n; i++, s += CAR_SLOT_SIZE) {
                    c = PTR(s, 0);
                    if (c[0x67c] == 2 || c[0x67d] == 2) {
                        count++;
                        if ((race[0x551] == 3 || race[0x551] == 8) && s == slot) {
                            ok = 0;
                            break;
                        }
                    }
                }
                if (ok)
                    ok = count < 6;
                break;
            case 3:
                ok = *(uint32_t *)0x6c30a0 == 0 && items_held(3) == 0;
                break;
            case 6:
                count = 0;
                self = 0;
                for (i = (int32_t)n, s = g_car_slots; i > 0; i--, s += CAR_SLOT_SIZE) {
                    c = PTR(s, 0);
                    if (c[0x67c] == 6 || c[0x67d] == 6 || (I32(c, 0x6ac) != -1 && *PTR(c, 0x6ac) == 1)) {
                        count++;
                        if (s == slot)
                            self = 1;
                    }
                }
                ok = count < 2 && !self;
                break;
            case 7:
                ok = *(uint8_t *)0x6b9d43 + items_held(7) < 1;
                break;
            }
            if (ok) {
                car_award_weapon(car, k);
                return;
            }
            if (++tries > 8) {
                car_award_weapon(car, 4);
                return;
            }
        }
        r -= (int32_t)chance;
        if (r < 0)
            r = 0;
        k = (k + 1) & 7;
    }
}

/* --- replay / ghost recorder --- */

#define REPLAY_STATE(rec) U32(rec, 0x1e880)

static uint8_t *replay_channel(uint8_t *rec, int32_t k)
{
    return rec + (k == 1 ? 0x14c24 : k == 2 ? 0x19a50 : 0x4e30);
}

/* 0x41c190: the replay's checksum (+6): set it, or with `check` mark it
 * bad (0xfecd) when it does not match. */
void GAMEAPI replay_checksum(uint8_t *rec, int32_t check)
{
    uint32_t sum = 0x2524, v;
    int32_t k, n;
    const uint8_t *p;
    uint8_t d;

    for (k = 0; k < 4; k++) {
        if (k == 1) {
            n = U16(rec, 0x4e38);
            p = rec + 0x4e3c;
        } else if (k == 2) {
            n = U16(rec, 0x14c2c);
            p = rec + 0x14c30;
        } else if (k == 3) {
            n = U16(rec, 0x19a58);
            p = rec + 0x19a5c;
        } else {
            n = S16(rec, 0);
            p = rec + 8;
        }
        d = 0x71;
        if (n > 0) {
            for (; n; n--)
                d = (uint8_t)(~d + *p++);
            v = d;
        } else {
            v = ((uint32_t)n & 0xffff0000) | d;
        }
        sum ^= v << (2 * k);
    }
    if (!check)
        U16(rec, 6) = (uint16_t)sum;
    else if (U16(rec, 6) != (uint16_t)sum)
        U16(rec, 6) = 0xfecd;
}

/* The replay code of a button (0 for the unused ones). */
static uint32_t replay_button_code(uint32_t bit)
{
    switch (bit) {
    case 1:
        return 0xb;
    case 2:
        return 9;
    case 4:
        return 0xa;
    case 8:
        return 8;
    case 0x10:
        return 6;
    case 0x20:
        return 5;
    case 0x40:
        return 4;
    case 0x80:
        return 7;
    case 0x100:
        return 0xd;
    case 0x200:
        return 0xf;
    case 0x400:
        return 0xe;
    case 0x800:
        return 0xc;
    case 0x1000:
        return 2;
    case 0x2000:
        return 1;
    case 0x4000:
        return 3;
    }
    return 0;
}

/* Record one control channel (the car's +0x6f8 + k): runs of equal values,
 * small changes in one byte, others as run + value. */
static void replay_record_channel(uint8_t *rec, uint8_t *ch, uint32_t value)
{
    uint8_t *out;
    int32_t diff;

    ch[1] = ch[0];
    ch[0] = (uint8_t)value;
    diff = (int32_t)value - ch[1];
    if (diff == 0 && REPLAY_STATE(rec) != 3 && REPLAY_STATE(rec) != 7 && ch[2] != 0x7f) {
        ch[2]++;
    } else {
        out = ch + 0xc + U16(ch, 8);
        if (ch[2]) {
            if (diff)
                ch[2]--;
            out[0] = ch[2];
            out[1] = ch[1];
            out += 2;
            U16(ch, 8) += 2;
        }
        ch[2] = 0;
        if (diff) {
            if ((diff < 0 ? -diff : diff) < 0x40) {
                out[0] = (uint8_t)diff | 0x80;
                U16(ch, 8) += 1;
            } else {
                out[0] = ch[2];
                out[1] = ch[0];
                U16(ch, 8) += 2;
            }
        }
    }
    if (U16(ch, 8) > U32(ch, 4) && REPLAY_STATE(rec) == 2)
        REPLAY_STATE(rec) = 3;
}

/* Play one control channel back. */
static void replay_play_channel(uint8_t *ch)
{
    uint16_t at = U16(ch, 0xa);
    uint8_t c;

    ch[1] = ch[0];
    if (at == U16(ch, 8))
        return;
    if (ch[2]) {
        ch[2]--;
        return;
    }
    c = ch[0xc + at];
    if (c & 0x80) {
        ch[0] = (uint8_t)((int8_t)(uint8_t)(c << 1) >> 1) + ch[1];
        U16(ch, 0xa)++;
    } else {
        ch[2] = c;
        ch[0] = ch[0xc + at + 1];
        U16(ch, 0xa) += 2;
    }
}

/* 0x41ba20: once per step per car with a replay record (+0x650): states 2
 * (recording), 3 (full), 7 (closing) record the controls and buttons; 5
 * plays them back into the car (6 when done). Buttons are runs of a button
 * state, packed by how many buttons and how long. */
void GAMEAPI replay_step(uint8_t *car)
{
    uint8_t *rec = PTR(car, 0x650), *out;
    const uint16_t *codes = (const uint16_t *)0x4a7f5c;
    uint32_t buttons, bit, b, code[2] = {0, 0}, found, kinds, mode, size, b0, b1, b2, b3;
    int32_t run, k;

    if (g_replay_off != 0 || !rec)
        return;
    switch (REPLAY_STATE(rec)) {
    case 2:
    case 3:
    case 7:
        U16(rec, 0x4e2c)++;
        if (g_sim_ticks == 0)
            U16(rec, 0x4e2e) = U16(car, 0x700);
        for (k = 0; k < 3; k++)
            replay_record_channel(rec, replay_channel(rec, k), car[k == 1 ? 0x6f9 : k == 2 ? 0x6fa : 0x6f8]);
        buttons = U16(rec, 0x4e2e);
        out = PTR(rec, 0x4e28);
        if (buttons == U16(car, 0x700) && REPLAY_STATE(rec) != 3 && REPLAY_STATE(rec) != 7 &&
            U16(rec, 0x4e2c) != 0x2000)
            return;
        run = S16(rec, 0x4e2c) - 1;
        found = kinds = 0;
        for (bit = 0; bit < 16; bit++) {
            b = 1u << bit & buttons;
            if (b) {
                if (found < 2)
                    code[found] = replay_button_code(b);
                found++;
            }
            if (b & 0xf0f0)
                kinds++;
        }
        mode = 0;
        if (kinds == 1 && found == 1 && run < 0x10)
            mode = 1;
        else if (found == 1)
            mode = run < 0x200 ? 2 : 0;
        else if (found == 2)
            mode = kinds == 2 && run < 0x80 ? 3 : run < 0x20 ? 4 : 0;
        switch (mode) {
        case 1:
            out[0] = (uint8_t)(code[0] << 4) | (uint8_t)run;
            size = 1;
            break;
        case 2:
            out[0] = (uint8_t)((code[0] | 0xc0) << 1) | (uint8_t)(run >> 8);
            out[1] = (uint8_t)run;
            size = 2;
            break;
        case 3:
            out[0] = (uint8_t)((code[0] | 0xf0) << 2) | (uint8_t)(code[1] >> 1);
            out[1] = (uint8_t)(code[1] << 7) | (uint8_t)run;
            size = 2;
            break;
        case 4:
            out[0] = (uint8_t)((code[0] | 0xd0) << 1) | (uint8_t)(code[1] >> 3);
            out[1] = (uint8_t)(code[1] << 5) | (uint8_t)run;
            size = 2;
            break;
        default:
            out[0] = (uint8_t)(run >> 8) | 0xe0;
            out[1] = (uint8_t)run;
            out[2] = (uint8_t)(buttons >> 8);
            out[3] = (uint8_t)buttons;
            size = 4;
            break;
        }
        out = PTR(rec, 0x4e28) + size;
        S16(rec, 0) = (int16_t)(S16(rec, 0) + size);
        U16(rec, 0x4e2e) = U16(car, 0x700);
        U16(rec, 0x4e2c) = 1;
        PTR(rec, 0x4e28) = out;
        if (REPLAY_STATE(rec) == 2) {
            S16(rec, 2)++;
            if (S16(rec, 0) >= 0x4e14)
                REPLAY_STATE(rec) = 3;
        } else if (REPLAY_STATE(rec) == 3) {
            REPLAY_STATE(rec) = 7;
        } else {
            replay_checksum(rec, 0);
            REPLAY_STATE(rec) = 0;
        }
        return;
    case 5:
        if (U16(rec, 0x4e2c) == 0) {
            out = PTR(rec, 0x4e28);
            b0 = out[0];
            b1 = out[1];
            b2 = out[2];
            b3 = out[3];
            if (!(b0 & 0x80)) {
                buttons = codes[b0 >> 4];
                run = (int32_t)(b0 & 0xf);
                size = 1;
            } else if ((b0 >> 5 & 3) == 0) {
                buttons = codes[b0 >> 1 & 0xf];
                run = (int32_t)((b0 & 1) << 8 | b1);
                size = 2;
            } else if ((b0 >> 5 & 3) == 2) {
                buttons = (uint32_t)codes[b1 >> 7 | (b0 & 3) << 1] | codes[b0 >> 2 & 7];
                run = (int32_t)(b1 & 0x7f);
                size = 2;
            } else if ((b0 >> 5 & 3) == 1) {
                buttons = (uint32_t)codes[b1 >> 5 | (b0 & 1) << 3] | codes[b0 >> 1 & 0xf];
                run = (int32_t)(b1 & 0x1f);
                size = 2;
            } else {
                run = (int32_t)((b0 & 0x1f) << 8 | b1);
                buttons = b2 << 8 | b3;
                size = 4;
            }
            U32(car, 0x700) = buttons;
            out = PTR(rec, 0x4e28) + size;
            S16(rec, 0x1e87c)--;
            U16(rec, 0x4e2e) = (uint16_t)buttons;
            U16(rec, 0x4e2c) = (uint16_t)(run - 1);
            PTR(rec, 0x4e28) = out;
            if (S16(rec, 0x1e87c) < -1)
                REPLAY_STATE(rec) = 6;
        } else {
            U32(car, 0x700) = U16(rec, 0x4e2e);
            U16(rec, 0x4e2c)--;
        }
        for (k = 0; k < 3; k++)
            replay_play_channel(replay_channel(rec, k));
        car[0x6f8] = rec[0x4e30];
        car[0x6f9] = rec[0x14c24];
        car[0x6fa] = rec[0x19a50];
        return;
    case 6:
        REPLAY_STATE(rec) = 0;
        return;
    }
}

/* 0x41c160: the race is over: a recording replay closes (2 -> 3), a
 * playing one ends (5 -> 6). */
void GAMEAPI replay_finish_mark(uint8_t *car)
{
    uint8_t *rec = PTR(car, 0x650);

    if (REPLAY_STATE(rec) == 2)
        REPLAY_STATE(rec) = 3;
    else if (REPLAY_STATE(rec) == 5)
        REPLAY_STATE(rec) = 6;
}
