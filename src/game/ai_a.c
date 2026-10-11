/* The AI's thinking, top level (0x4015e0, once per AI car before the
 * driving): clear the buttons, re-plan every 120 ticks, then either run
 * the current special state (+0x6b4 low bits, with its counter +0x6b6)
 * or the normal checks (route, stuck, bursts, then 0x412290 and
 * 0x401780 on one step in six). Bit 0x40 of +0x6b5 tells ai_drive to
 * follow the line this step.
 *
 * Special states: 1/2 swerve, 0x20 rejoin a line, 0x40 race start, 0x80
 * unstick, 0x100 coast, 0x200 full throttle. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_ai_seed (*(uint32_t *)0x4a7148)
#define g_ai_difficulty_mode (*(int32_t *)0x4a714c)
#define g_markers_tick (*(uint32_t *)0x4b5e50)
#define g_start_waiting (*(int32_t *)0x4bde7c)
#define g_fire_odds_shift ((const int32_t *)0x4a7c38)
#define g_start_odds ((const uint8_t *)0x4a7bc0)
#define g_markers (*(uint8_t **)0x6c1eb0) /* +0xf6 count, +0xf8 8-byte entries */
#define g_lines (*(uint8_t **)(*(uint8_t **)0x6c1e30 + 0x60))
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274

#define LINE(i) (g_lines + (uint16_t)(i) * 0xc)
#define NODE(line, i) (*(uint8_t **)((line) + 8) + (uint16_t)(i) * 0x10)
#define SLOT_OF(car) (g_car_slots + *(const uint16_t *)((car) + 0x64e) * CAR_SLOT_SIZE)
#define SLOT_PLAN_LINE(slot) ((*(uint8_t **)((slot) + 0x64))[5])
#define SLOT_PLAN_NODE(slot) (*(uint16_t *)((slot) + 0x68))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define STATE 0x6b4
#define STATE_TIME 0x6b6

/* Still original, owned elsewhere. */
typedef void(GAMEAPI *car_fn)(uint8_t *car);
typedef void(GAMEAPI *void_fn)(void);
typedef int32_t(GAMEAPI *line_steps_fn)(uint32_t node, const uint8_t *line, uint32_t steps, uint16_t *out);
typedef void(GAMEAPI *queue_push_fn)(uint32_t line, uint32_t node);
typedef void(GAMEAPI *queue_pop_fn)(uint16_t *line, uint16_t *node);
typedef int32_t(GAMEAPI *queue_empty_fn)(void);
typedef int32_t(GAMEAPI *contains_fn)(uint32_t value, const uint16_t *list, uint32_t count);
#define ai_fire_front ((car_fn)0x47f350)
#define ai_fire_rear ((car_fn)0x47f370)
#define line_steps_ahead ((line_steps_fn)0x47f550)
#define ai_avoid ((car_fn)0x412290)
#define ai_weapons ((car_fn)0x401780)
#define route_queue_reset ((void_fn)0x47f1b0)
#define route_queue_push ((queue_push_fn)0x47f1c0)
#define route_queue_pop ((queue_pop_fn)0x47f220)
#define route_queue_empty ((queue_empty_fn)0x47f270)
#define list_contains ((contains_fn)0x47f430)

static uint32_t ai_random(void)
{
    g_ai_seed = g_ai_seed * 16807 & 0x7fffffff;
    return g_ai_seed;
}

/* 0x4255a0 */
void GAMEAPI ai_clear_buttons(uint8_t *car)
{
    U32(car, 0x700) = 0;
}

/* 0x47fbb0: put the slot's car back on its planned line and node. */
void GAMEAPI ai_rejoin_plan(const uint8_t *slot)
{
    uint8_t *car = *(uint8_t **)slot;

    U16(car, 0x6b0) = SLOT_PLAN_LINE(slot);
    U16(car, 0x6b2) = SLOT_PLAN_NODE(slot);
}

/* 0x47ff00: fire whichever weapon slot holds kind 4; 1 if one did. */
int32_t GAMEAPI ai_fire_kind4(uint8_t *car)
{
    if (car[0x67c] == 4) {
        ai_fire_front(car);
        return 1;
    }
    if (car[0x67d] == 4) {
        ai_fire_rear(car);
        return 1;
    }
    return 0;
}

/* 0x480030: may this slot's AI attack? Always in mode 0, never on its
 * last lap in mode 1, only after lap 2 of long races in mode 2. */
int32_t GAMEAPI ai_may_attack(const uint8_t *slot)
{
    if (g_race[0x58c] == 1 || (slot[0x259] & 1) || g_ai_difficulty_mode == 0)
        return 1;
    if (g_ai_difficulty_mode == 1)
        return slot[0x2c] != g_race[0x586];
    if (g_ai_difficulty_mode == 2)
        return slot[0x2c] <= 2 && g_race[0x586] > 3;
    return 1;
}

/* 0x40f900: now and then (odds by +0x6e2) a burst of full throttle. */
void GAMEAPI ai_maybe_full_throttle(uint8_t *car)
{
    if (g_ai_difficulty_mode == 2 || g_race_ticks < 0x17a || S16(car, 0x658) < 200)
        return;
    if (!(((ai_random() >> 15) << (car[0x6e2] & 31)) >> 16)) {
        U16(car, STATE) = 0x200;
        U16(car, STATE_TIME) = 0;
    }
}

/* 0x410d70: fire a weapon at random (odds by +0x6e8 bits 6..8). */
void GAMEAPI ai_maybe_fire(uint8_t *car)
{
    uint32_t shift = (uint32_t)g_fire_odds_shift[U32(car, 0x6e8) >> 6 & 7];
    uint32_t roll = ((ai_random() >> 15) << (shift & 31)) >> 16;

    if (roll == 0) {
        if (car[0x67c])
            ai_fire_front(car);
    } else if (roll == 1 && car[0x67d]) {
        ai_fire_rear(car);
    }
}

/* 0x40f980: a random reaction time at the start, in steps, by the
 * car's start skill (+0x6e4 bits 3..5). */
uint32_t GAMEAPI ai_start_delay(const uint8_t *car)
{
    uint32_t a, b;

    switch (U32(car, 0x6e4) >> 3 & 7) {
    case 0:
        a = ai_random();
        b = ai_random() >> 15;
        return a >> 28 ? (uint8_t)((b * 21) >> 16) : (uint8_t)(((b << 4) >> 16) + 0x55);
    case 1:
        a = ai_random();
        b = ai_random() >> 15;
        return a >> 28 ? (uint8_t)(((b * 31) >> 16) + 10) : (uint8_t)(((b * 21) >> 16) + 0x4b);
    case 2:
        a = ai_random();
        b = ai_random() >> 15;
        return a >> 28 ? (uint8_t)(((b * 31) >> 16) + 0x14) : (uint8_t)(((b * 21) >> 16) + 0x46);
    case 3:
        a = ai_random();
        b = ai_random() >> 15;
        return a >> 29 ? (uint8_t)(((b * 31) >> 16) + 0x1e) : (uint8_t)(((b * 23) >> 16) + 0x44);
    case 4:
        return (uint8_t)((((ai_random() >> 15) * 46) >> 16) + 0x28);
    case 5:
        return (uint8_t)((((ai_random() >> 15) * 31) >> 16) + 0x32);
    case 6:
        return (uint8_t)((((ai_random() >> 15) << 4) >> 16) + 0x3c);
    default:
        return (uint8_t)((((ai_random() >> 15) * 7) >> 16) + 0x42);
    }
}

/* 0x424d80: state 0x200: 20 steps of throttle only. */
void GAMEAPI ai_state_full_throttle(uint8_t *car)
{
    if (U16(car, STATE_TIME) == 0x14) {
        U16(car, STATE) = 0;
        U16(car, STATE_TIME) = 0;
        return;
    }
    U16(car, STATE_TIME)++;
    U32(car, 0x700) = (U16(car, 0x700) & 0x5fbf) | 0x40;
}

/* 0x424dd0: state 0x100: count down; throttle (and skip the normal
 * thinking) while 30 or more remain. */
int32_t GAMEAPI ai_state_coast(uint8_t *car)
{
    uint16_t left = U16(car, STATE_TIME);

    if (!left) {
        U16(car, STATE) = 0;
        return 0;
    }
    left--;
    U16(car, STATE_TIME) = left;
    if (left < 0x1e)
        return 0;
    ai_press_accelerate(car);
    return 1;
}

/* 0x424e10: state 0x40, the race start: during the countdown (phases
 * 1..3) the AI revs at random (no more than one car at a time, odds by
 * +0x6e4) then waits its reaction time from 2 ticks before the go;
 * phase 4 counts the rest down then drives. */
void GAMEAPI ai_state_start(uint8_t *car)
{
    int32_t until;

    switch (I32(g_race, 0x57c)) {
    case 1:
    case 2:
    case 3:
        until = 0xe4 - (int32_t)g_race_ticks;
        if (until < 0x46) {
            if (until == 0x44) {
                U16(car, STATE_TIME) = (uint16_t)ai_start_delay(car);
                return;
            }
            if (until > 0x44)
                return;
            if (U16(car, STATE_TIME)) {
                U16(car, STATE_TIME)--;
                return;
            }
            ai_press_accelerate(car);
            return;
        }
        if (U16(car, STATE_TIME)) {
            if (U16(car, STATE_TIME) == 1)
                g_start_waiting--;
            U16(car, STATE_TIME)--;
            ai_press_accelerate(car);
            return;
        }
        if (g_start_waiting >= 2)
            return;
        if (((ai_random() >> 15) * g_start_odds[U32(car, 0x6e4) & 7]) >> 16)
            return;
        {
            uint32_t rev = (((ai_random() >> 15) << 4) >> 16) + 0xf;

            U16(car, STATE_TIME) = (uint16_t)rev;
            if ((int32_t)(rev & 0xffff) > until - 0x46)
                U16(car, STATE_TIME) = (uint16_t)(until - 0x46);
        }
        if (U16(car, STATE_TIME))
            g_start_waiting++;
        return;
    case 4:
        if (U16(car, STATE_TIME)) {
            U16(car, STATE_TIME)--;
            return;
        }
        ai_press_accelerate(car);
        U16(car, STATE) = 0;
        return;
    default:
        ai_press_accelerate(car);
        U16(car, STATE) = 0;
        U16(car, STATE_TIME) = 0;
        return;
    }
}

/* 0x424fe0: state 0x20: head for the car's line at its kind-4 marker
 * (or node 0); give up after 120 steps and rejoin the plan. Once lined
 * up facing along the line but with the node behind, fire a kind-4
 * weapon and drop the state. */
void GAMEAPI ai_state_rejoin(uint8_t *car)
{
    uint16_t line_index = U16(car, 0x6b0), node = 0;
    const uint8_t *line = LINE(line_index), *entry;
    int32_t i = g_markers[0xf6], forward[3], dir[3], to[3], along, behind;
    const int16_t *p;

    entry = *(uint8_t **)(g_markers + 0xf8) + i * 8 - 8;
    U16(car, STATE_TIME)++;
    if (U16(car, STATE_TIME) > 0x78) {
        ai_rejoin_plan(SLOT_OF(car));
        U16(car, STATE) = 0;
        U16(car, STATE_TIME) = 0;
        return;
    }
    for (i--; i >= 0; i--, entry -= 8)
        if (entry[4] == 4 && U16(entry, 0) == line_index)
            break;
    if (i >= 0)
        node = U16(entry, 2);
    else
        U16(car, STATE) = 0;
    body_forward(forward, car);
    line_direction(dir, line_index, node);
    p = (const int16_t *)NODE(line, node);
    to[0] = (int32_t)((uint32_t)p[0] << 16) - I32(car, POSITION);
    to[1] = (int32_t)((uint32_t)p[1] << 16) - I32(car, POSITION + 4);
    to[2] = (int32_t)((uint32_t)p[2] << 16) - I32(car, POSITION + 8);
    vec_set_length(to, 0x10000);
    along = vec_dot(dir, forward);
    behind = vec_dot(dir, to);
    if (along > 0xf746 && behind < -0xf746) {
        ai_fire_kind4(car);
        U16(car, STATE) = 0;
    }
    car[0x6b5] |= 0x40;
}

/* 0x425610: states 1/2: swerve (left or right by bit 0) towards the
 * line for up to 10 steps, braking at full lock; then 20 steps of normal
 * driving before the state ends. Only starts at speed and facing along
 * the line. 1 if it drove this step. */
int32_t GAMEAPI ai_state_swerve(uint8_t *car)
{
    uint16_t state = U16(car, STATE), line_index = U16(car, 0x6b0), node = U16(car, 0x6b2);
    uint16_t time = U16(car, STATE_TIME);
    const int16_t *p = (const int16_t *)NODE(LINE(line_index), node);
    int32_t d[3], speed;

    d[0] = (int32_t)((uint32_t)p[0] << 16) - I32(car, POSITION);
    d[1] = (int32_t)((uint32_t)p[1] << 16) - I32(car, POSITION + 4);
    d[2] = (int32_t)((uint32_t)p[2] << 16) - I32(car, POSITION + 8);
    if (time >= 0x1e) {
        U16(car, STATE) = 0;
        U16(car, STATE_TIME) = 0;
        return 0;
    }
    if (time >= 0xa) {
        U16(car, STATE_TIME)++;
        return 0;
    }
    if (time == 0) {
        int32_t dir[3], forward[3];

        if (S16(car, 0x658) < 200)
            return 0;
        ai_line_direction(dir, SLOT_OF(car));
        body_forward(forward, car);
        if (vec_dot(forward, dir) < 0xddb3)
            return 0;
    }
    if (state & 1) {
        ai_press_steer_down(car);
        if (I32(car, 0x634) < -30000) {
            ai_press_brake(car);
            U16(car, STATE_TIME)++;
        }
    } else {
        ai_press_steer_up(car);
        if (I32(car, 0x634) > 30000) {
            ai_press_brake(car);
            U16(car, STATE_TIME)++;
        }
    }
    ai_aim_at_node(car, d, line_index, node);
    speed = g_race[0x551] == 4 ? 0xfa : ((const uint8_t *)p)[7] * 2;
    ai_speed_control(car, (uint32_t)speed);
    return 1;
}

/* 0x42a510: once a tick: close kind-2 markers (+5 set) that a player
 * car on that line is within a node of, 5 nodes on. */
void GAMEAPI ai_markers_update(void)
{
    uint32_t count = g_markers[0xf6];
    uint8_t *entry = *(uint8_t **)(g_markers + 0xf8) + count * 8 - 8;
    uint16_t ahead = 0;

    if ((int32_t)(count - 1) < 0)
        return;
    for (; count; count--, entry -= 8) {
        uint16_t line_index;
        int32_t i, cars;

        if (entry[4] != 2 || entry[5] != 1)
            continue;
        line_index = U16(entry, 0);
        if (!line_steps_ahead(U16(entry, 2), LINE(line_index), 5, &ahead))
            continue;
        cars = g_race[0x54d];
        for (i = 0; i < cars; i++) {
            const uint8_t *slot = g_car_slots + i * CAR_SLOT_SIZE;

            if ((*(uint8_t **)slot)[0x70c] == 10 && SLOT_PLAN_LINE(slot) == line_index &&
                abs32((int32_t)SLOT_PLAN_NODE(slot) - ahead) < 2) {
                entry[5] = 0;
                break;
            }
        }
    }
}

/* 0x46ad00: can a car on line `from` reach (target line, node) by the
 * lines' links (node +0xc: two 5-bit line and 9-bit node pairs)? A
 * breadth-first search with the shared queue. */
int32_t GAMEAPI route_reaches(uint32_t from, uint32_t target_line, uint32_t target_node)
{
    uint16_t seen[0x40], line_index = (uint16_t)target_line, node = (uint16_t)target_node, a, b, *next;
    const uint8_t *p = NODE(LINE(line_index), node);
    int32_t n = 1;

    if (line_index == (uint16_t)from)
        return 1;
    a = p[0xc] & 0x1f;
    if (a == (uint16_t)from)
        return 1;
    b = (uint16_t)(U32(p, 0xc) >> 14 & 0x1f);
    if (b == (uint16_t)from)
        return 1;
    memset(seen, 0, sizeof(seen));
    seen[0] = line_index;
    route_queue_reset();
    if (a) {
        seen[1] = a;
        n = 2;
        route_queue_push(a, U32(p, 0xc) >> 5 & 0x1ff);
    }
    if (b) {
        seen[n++] = b;
        route_queue_push(b, U32(p, 0xc) >> 19 & 0x1ff);
    }
    next = seen + n;
    while (!route_queue_empty()) {
        route_queue_pop(&line_index, &node);
        p = NODE(LINE(line_index), node);
        a = p[0xc] & 0x1f;
        if (a == (uint16_t)from)
            return 1;
        b = (uint16_t)(U32(p, 0xc) >> 14 & 0x1f);
        if (b == (uint16_t)from)
            return 1;
        if (a && !list_contains(a, seen, 0x40)) {
            *next++ = a;
            route_queue_push(a, U32(p, 0xc) >> 5 & 0x1ff);
        }
        if (b && !list_contains(b, seen, 0x40)) {
            *next++ = b;
            route_queue_push(b, U32(p, 0xc) >> 19 & 0x1ff);
        }
    }
    return 0;
}

/* 0x46ac80: if the car's line can't reach its plan for 60 checks, jump
 * back onto the plan. */
void GAMEAPI ai_check_route(uint8_t *car)
{
    const uint8_t *slot = SLOT_OF(car);

    if (route_reaches(U16(car, 0x6b0), SLOT_PLAN_LINE(slot), SLOT_PLAN_NODE(slot))) {
        car[0x6c8] = 0;
        return;
    }
    if (++car[0x6c8] > 0x3c)
        ai_rejoin_plan(slot);
}

/* 0x46af10: crawling (under 85) with the throttle held for 30 steps (60
 * on rough ground) starts unsticking (state 0x80). +0x6d8 counts steps
 * since, from 254 just after the start. */
void GAMEAPI ai_stuck_check(uint8_t *car)
{
    uint8_t limit = 0x1e;

    if (I32(g_race, 0x57c) == 4 && U32(g_race, 0x580) < 0x3c) {
        car[0x6d9] = 0;
        car[0x6d8] = 0xfe;
        return;
    }
    if (car_surface_kind(car) > 10)
        limit = 0x3c;
    if (S16(car, 0x658) < 0x55 && (car[0x6fc] & 0x40)) {
        if (++car[0x6d9] > limit) {
            U16(car, STATE) = 0x80;
            U16(car, STATE_TIME) = 0;
            car[0x6d9] = 0;
        }
    } else {
        car[0x6d9] = 0;
    }
    if (car[0x6d8] != 0xff)
        car[0x6d8]++;
}

/* 0x46afb0: state 0x80, unsticking: by how the car faces the planned
 * line, reverse out (handbrake when slow) or drive on, for a while
 * (shorter if it got stuck again soon after the last time). */
void GAMEAPI ai_state_unstick(uint8_t *car)
{
    const uint8_t *slot = SLOT_OF(car);
    uint16_t limit = car_surface_kind(car) <= 10 ? 0x78 : 0x96, time;
    uint8_t drive_for, give_up;
    int32_t dir[3], forward[3], facing, moving;

    if (car[0x6d8] < limit) {
        drive_for = 0x3c;
        give_up = 0x78;
    } else {
        drive_for = 0x14;
        give_up = 0x3c;
    }
    time = U16(car, STATE_TIME);
    U16(car, STATE_TIME) = (uint16_t)(time + 1);
    if (time >= give_up)
        goto done;
    line_direction(dir, SLOT_PLAN_LINE(slot), SLOT_PLAN_NODE(slot));
    body_forward(forward, car);
    facing = vec_dot(dir, forward);
    moving = vec_dot(VEC(car, VELOCITY), forward);
    if (facing < 0x5780) {
        if (facing >= 0)
            ai_press_brake(car);
        if (moving > 0) {
            ai_press_handbrake(car);
            ai_steer_to(car, dir, 1, 0);
            return;
        }
        if (S16(car, 0x658) < 0x46)
            ai_press_handbrake(car);
        dir[0] = -dir[0];
        dir[1] = -dir[1];
        dir[2] = -dir[2];
        ai_steer_to(car, dir, 0, 1);
        return;
    }
    if (facing < 0xa474) {
        ai_press_brake(car);
        if (moving > 0) {
            ai_press_handbrake(car);
            ai_steer_to(car, dir, 1, 0);
            return;
        }
        if (S16(car, 0x658) < 0x1e)
            ai_press_handbrake(car);
        dir[0] = -dir[0];
        dir[1] = -dir[1];
        dir[2] = -dir[2];
        ai_steer_to(car, dir, 0, 1);
        return;
    }
    if (time >= drive_for)
        ai_press_accelerate(car);
    else
        ai_press_handbrake(car);
    if (moving >= 0) {
        ai_steer_to(car, dir, 1, 0);
        if (time < drive_for)
            return;
        goto done;
    }
    ai_press_brake(car);
    dir[0] = -dir[0];
    dir[1] = -dir[1];
    dir[2] = -dir[2];
    ai_steer_to(car, dir, 0, 1);
    return;
done:
    U16(car, STATE_TIME) = 0;
    U16(car, STATE) = 0;
    car[0x6d8] = 0;
}

/* 0x4015e0 */
void GAMEAPI ai_think(uint8_t *car)
{
    U16(car, STATE) &= 0xbfff;
    ai_clear_buttons(car);
    if (g_race_ticks % 0x78 == (uint32_t)U16(car, 0x64e) << 4)
        ai_replan(car, 0);
    ai_maybe_fire(car);
    if (g_markers_tick < g_race_ticks) {
        g_markers_tick = g_race_ticks;
        ai_markers_update();
    }
    if (car[0x6c9] >= 5)
        car[0x6c9] = 0;
    else
        car[0x6c9]++;
    switch (U16(car, STATE) & 0x3ff) {
    case 1:
    case 2:
        if (ai_state_swerve(car))
            return;
        break;
    case 0x20:
        ai_state_rejoin(car);
        return;
    case 0x40:
        ai_state_start(car);
        return;
    case 0x80:
        ai_state_unstick(car);
        return;
    case 0x100:
        if (ai_state_coast(car))
            return;
        break;
    case 0x200:
        ai_state_full_throttle(car);
        return;
    }
    ai_check_route(car);
    ai_stuck_check(car);
    ai_maybe_full_throttle(car);
    if (!car[0x6c9]) {
        ai_avoid(car);
        if (!car[0x6c9])
            ai_weapons(car);
    }
    car[0x6b5] |= 0x40;
}
