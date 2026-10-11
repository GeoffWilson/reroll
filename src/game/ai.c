/* The computer drivers' steering (0x425ad0, after the AI's thinking each
 * step): follow the racing line, and drive by pressing the same buttons a
 * player would (car +0x700: 0x40 accelerate, 0x80 brake, 0x10 handbrake,
 * 0x2000/0x8000 steer), which car_controls_read then ramps.
 *
 * Racing lines: *(0x6c1e30)+0x60 is an array of 0xc-byte lines (+0 node
 * count, +2 reversed, +4 looped, +8 nodes); nodes are 0x10 bytes (+0
 * int16 x, y, z in whole units, +7 speed / 2). The car's line and node
 * are +0x6b0 and +0x6b2. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_lines (*(uint8_t **)(*(uint8_t **)0x6c1e30 + 0x60))
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274

#define LINE(i) (g_lines + (uint16_t)(i) * 0xc)
#define NODE(line, i) (*(uint8_t **)((line) + 8) + (uint16_t)(i) * 0x10)
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define BUTTONS 0x700

/* 0x47f290..0x47f310: press or release a button. */
void GAMEAPI ai_press_accelerate(uint8_t *car) { U32(car, BUTTONS) |= 0x40; }
void GAMEAPI ai_release_accelerate(uint8_t *car) { U32(car, BUTTONS) &= ~0x40u; }
void GAMEAPI ai_press_brake(uint8_t *car) { U32(car, BUTTONS) |= 0x80; }
void GAMEAPI ai_press_handbrake(uint8_t *car) { U32(car, BUTTONS) |= 0x10; }
void GAMEAPI ai_press_steer_down(uint8_t *car) { U32(car, BUTTONS) |= 0x8000; }
void GAMEAPI ai_press_steer_up(uint8_t *car) { U32(car, BUTTONS) |= 0x2000; }

/* 0x47f390: 1 if bit 27 is clear (a cheap sign test for values well
 * inside +-2^27). */
uint32_t GAMEAPI bit27_clear(uint32_t v)
{
    return ~v >> 27 & 1;
}

/* 0x47f400, 0x47f3d0: of an int16 vector. */
int32_t GAMEAPI svec_length_sq(const int16_t *v)
{
    return v[2] * v[2] + v[1] * v[1] + v[0] * v[0];
}

uint32_t GAMEAPI svec_length(const int16_t *v)
{
    return fix_sqrt_table(v[2] * v[2] + v[1] * v[1] + v[0] * v[0]);
}

/* 0x47f470, 0x47f4e0: the next and previous node along a line (in its
 * driving direction), wrapping if it loops; 0 at an end. */
int32_t GAMEAPI line_next(uint32_t node, const uint8_t *line, uint16_t *out)
{
    uint16_t n = (uint16_t)node, last = (uint16_t)(*(const uint16_t *)line - 1);

    if (!line[2]) {
        if (n != last) {
            *out = (uint16_t)(n + 1);
            return 1;
        }
        if (line[4]) {
            *out = 0;
            return 1;
        }
    } else {
        if (n) {
            *out = (uint16_t)(n - 1);
            return 1;
        }
        if (line[4]) {
            *out = last;
            return 1;
        }
    }
    return 0;
}

int32_t GAMEAPI line_prev(uint32_t node, const uint8_t *line, uint16_t *out)
{
    uint16_t n = (uint16_t)node, last = (uint16_t)(*(const uint16_t *)line - 1);

    if (!line[2]) {
        if (n) {
            *out = (uint16_t)(n - 1);
            return 1;
        }
        if (line[4]) {
            *out = last;
            return 1;
        }
    } else {
        if (n != last) {
            *out = (uint16_t)(n + 1);
            return 1;
        }
        if (line[4]) {
            *out = 0;
            return 1;
        }
    }
    return 0;
}

/* 0x47f5b0, 0x47f5e0: a body's forward and up axes (16.16). */
void GAMEAPI body_forward(int32_t *out, const uint8_t *body)
{
    out[0] = (int32_t)((uint32_t)S16(body, MATRIX + 4) << 4);
    out[1] = (int32_t)((uint32_t)S16(body, MATRIX + 10) << 4);
    out[2] = (int32_t)((uint32_t)S16(body, MATRIX + 16) << 4);
}

void GAMEAPI body_up(int32_t *out, const uint8_t *body)
{
    out[0] = (int32_t)((uint32_t)S16(body, MATRIX + 2) << 4);
    out[1] = (int32_t)((uint32_t)S16(body, MATRIX + 8) << 4);
    out[2] = (int32_t)((uint32_t)S16(body, MATRIX + 14) << 4);
}

/* 0x47f640: how far b is turned from a about n: (a x b) along n's
 * largest axis, over that axis of n. 0 if n is tiny. */
int32_t GAMEAPI vec_turn_about(const int32_t *a, const int32_t *b, const int32_t *n)
{
    int32_t ax = abs32(n[0]), ay = abs32(n[1]), az = abs32(n[2]), c[3], num, den;

    vec_cross(a, b, c);
    if (ax > ay) {
        if (ax > az) {
            num = c[0];
            den = n[0];
        } else {
            num = c[2];
            den = n[2];
        }
    } else if (ay > az) {
        num = c[1];
        den = n[1];
    } else {
        num = c[2];
        den = n[2];
    }
    if (abs32(den) < 0x400)
        return 0;
    return fix_div16(num, den);
}

/* 0x47f6e0: is pos within r of the node on every axis? */
int32_t GAMEAPI node_near(uint32_t node, const uint8_t *line, const int32_t *pos, int32_t r)
{
    const int16_t *p = (const int16_t *)NODE(line, node);
    int32_t dx = (int32_t)((uint32_t)p[0] << 16) - pos[0];
    int32_t dy = (int32_t)((uint32_t)p[1] << 16) - pos[1];
    int32_t dz = (int32_t)((uint32_t)p[2] << 16) - pos[2];

    return abs32(dx) <= r && abs32(dy) <= r && abs32(dz) <= r;
}

/* 0x47f760: has pos (nearly) passed the node, along the line there? */
int32_t GAMEAPI node_passed(uint32_t node, const uint8_t *line, const int32_t *pos)
{
    const int16_t *p = (const int16_t *)NODE(line, node), *q;
    int32_t ax = (int32_t)((uint32_t)p[0] << 16), ay = (int32_t)((uint32_t)p[1] << 16);
    int32_t az = (int32_t)((uint32_t)p[2] << 16), d[3], t[3] = {0, 0, 0};
    uint16_t other;

    d[0] = ax - pos[0];
    d[1] = ay - pos[1];
    d[2] = az - pos[2];
    if (line_prev(node, line, &other)) {
        q = (const int16_t *)NODE(line, other);
        t[0] = ax - (int32_t)((uint32_t)q[0] << 16);
        t[1] = ay - (int32_t)((uint32_t)q[1] << 16);
        t[2] = az - (int32_t)((uint32_t)q[2] << 16);
    } else if (line_next(node, line, &other)) {
        q = (const int16_t *)NODE(line, other);
        t[0] = (int32_t)((uint32_t)q[0] << 16) - ax;
        t[1] = (int32_t)((uint32_t)q[1] << 16) - ay;
        t[2] = (int32_t)((uint32_t)q[2] << 16) - az;
    }
    vec_set_length(t, 0x10000);
    return vec_dot(d, t) < 5000;
}

/* 0x47f880: move *node to the line's node nearest ahead of pos (at most
 * 50 steps), setting *at to it: forwards while the nodes are near or
 * passed, else back while the previous ones are not. */
void GAMEAPI line_advance(const uint8_t *line, uint16_t *node, int32_t *at, const int32_t *pos)
{
    uint16_t start = *node, other;
    int32_t steps = 0;

    if (!node_near(start, line, pos, 0x140000) && !node_passed(start, line, pos)) {
        for (;;) {
            if (++steps >= 0x32) {
                *at = (int32_t)(uintptr_t)NODE(line, start);
                *node = start;
                return;
            }
            if (!line_prev(*node, line, &other) || node_near(other, line, pos, 0x140000) ||
                node_passed(other, line, pos))
                return;
            *at = (int32_t)(uintptr_t)NODE(line, other);
            *node = other;
        }
    }
    for (;;) {
        if (++steps >= 0x32) {
            *at = (int32_t)(uintptr_t)NODE(line, start);
            *node = start;
            return;
        }
        if (!line_next(*node, line, &other))
            return;
        *at = (int32_t)(uintptr_t)NODE(line, other);
        *node = other;
        if (!node_near(other, line, pos, 0x140000) && !node_passed(other, line, pos))
            return;
    }
}

/* 0x47fdc0: the line's unit direction at a node. */
void GAMEAPI line_direction(int32_t *out, uint32_t line_index, uint32_t node)
{
    const uint8_t *line = LINE(line_index);
    const int16_t *p = (const int16_t *)NODE(line, node), *q;
    int32_t ax = (int32_t)((uint32_t)p[0] << 16), ay = (int32_t)((uint32_t)p[1] << 16);
    int32_t az = (int32_t)((uint32_t)p[2] << 16);
    uint16_t other;

    if (line_next(node, line, &other)) {
        q = (const int16_t *)NODE(line, other);
        out[0] = (int32_t)((uint32_t)q[0] << 16) - ax;
        out[1] = (int32_t)((uint32_t)q[1] << 16) - ay;
        out[2] = (int32_t)((uint32_t)q[2] << 16) - az;
        vec_set_length(out, 0x10000);
        return;
    }
    if (line_prev(node, line, &other)) {
        q = (const int16_t *)NODE(line, other);
        out[0] = ax - (int32_t)((uint32_t)q[0] << 16);
        out[1] = ay - (int32_t)((uint32_t)q[1] << 16);
        out[2] = az - (int32_t)((uint32_t)q[2] << 16);
        vec_set_length(out, 0x10000);
        return;
    }
    out[0] = 0x10000;
    out[1] = 0;
    out[2] = 0;
}

/* 0x47fd10: the direction of the line a car slot is heading for (its
 * plan: slot +0x64, byte 5 the line, slot +0x68 the node); if that runs
 * the other way to the car's current line, the current line's direction
 * at the car instead. */
void GAMEAPI ai_line_direction(int32_t *out, const uint8_t *slot)
{
    const uint8_t *car = *(uint8_t **)slot;
    uint16_t current = *(const uint16_t *)(car + 0x6b0), target = (*(uint8_t **)(slot + 0x64))[5];

    if (current != target && LINE(current)[2] != LINE(target)[2]) {
        uint16_t node = *(const uint16_t *)(car + 0x6b2);
        int32_t at;

        line_advance(LINE(current), &node, &at, VEC(car, POSITION));
        line_direction(out, current, node);
        return;
    }
    line_direction(out, target, *(const uint16_t *)(slot + 0x68));
}

/* 0x425240: the surface kind under the first touching wheel. */
int32_t GAMEAPI car_surface_kind(const uint8_t *car)
{
    int32_t w;

    for (w = 0; w < 4; w++)
        if (car[0x324 + w * 0xc4] & 1) {
            const uint8_t *surface = *(uint8_t **)(car + 0x198 + w * 0x1c);

            return surface ? (int32_t)(U32(surface, 0xc) >> 8 & 0x1f) : 0;
        }
    return 0;
}

/* 0x4252a0: how much to steer (+-0xa000) for a turn of `turn` (limit
 * gives full lock), countering a fast spin on grippy surfaces for the
 * harder drivers; full lock towards the turn if it is behind. */
int32_t GAMEAPI ai_steer_amount(int32_t turn, int32_t ahead, int32_t limit, const uint8_t *car)
{
    int32_t spin = car[0x6e1] > 1 ? I32(car, ANGULAR_MOMENTUM + 4) : 0, counter = 0, s;

    if (ahead < 0)
        return turn <= 0 ? -0xa000 : 0xa000;
    if (abs32(spin) > 0x3d0900)
        counter = mul16(-(spin >> 6), (int32_t)((uint32_t)(car_surface_kind(car) + 0x10) << 11));
    if (car[0x63f])
        counter = -counter;
    if (turn > limit)
        s = 0xa000;
    else if (turn < -limit)
        s = -0xa000;
    else
        s = mul16(turn, 0x1d081);
    s += counter;
    if (s > 0xa000)
        return 0xa000;
    if (s < -0xa000)
        return -0xa000;
    return s;
}

/* 0x425170: how the car's travel lines up with its heading (*ahead,
 * 16.16; also +0x6b8) and its sideways turn (*side); straight ahead if
 * barely moving. */
void GAMEAPI car_heading(uint8_t *car, int32_t *ahead, int32_t *side)
{
    int32_t v[3], forward[3], up[3], length;

    v[0] = I32(car, VELOCITY) >> 4;
    v[1] = I32(car, VELOCITY + 4) >> 4;
    v[2] = I32(car, VELOCITY + 8) >> 4;
    length = vec_length(v);
    if (length > 0x800) {
        vec_div(v, length);
        body_forward(forward, car);
        body_up(up, car);
        *ahead = vec_dot(forward, v);
        *side = vec_turn_about(forward, v, up);
    } else {
        *ahead = 0x10000;
        *side = 0;
    }
    car[0x6b8] = (uint8_t)(*ahead >> 8);
}

/* 0x4255b0: steer towards `want` (flipped when upside down) unless both
 * it and the current steering are small. */
void GAMEAPI ai_steer_press(int32_t want, uint8_t *car)
{
    int32_t steering = I32(car, 0x634);

    if (car[0x63f])
        want = -want;
    if (abs32(want) < 0x477 && abs32(steering) < 0x477)
        return;
    if (want > steering)
        ai_press_steer_up(car);
    else if (want < steering)
        ai_press_steer_down(car);
}

/* 0x425380: throttle and brake for the node's speed (target, in shown
 * speed units), by how well the car faces the line and its surface. */
void GAMEAPI ai_speed_control(uint8_t *car, uint32_t target)
{
    uint8_t *slot = g_car_slots + *(uint16_t *)(car + 0x64e) * CAR_SLOT_SIZE;
    int16_t speed = S16(car, 0x658);
    int32_t kind = car_surface_kind(car), skill = car[0x6e1], backing = 0, slow, crawl, facing, limit;
    int32_t forward[3], dir[3];

    if (kind <= 10) {
        slow = 200;
        crawl = 100;
    } else if (I32(car, 0x2278) < -5000 || (I32(car, 0x2278) < 1000 && g_race[0x553])) {
        slow = 0x28;
        crawl = skill > 1 ? 40 : 100;
    } else {
        crawl = 100;
        slow = 100;
    }
    body_forward(forward, car);
    ai_line_direction(dir, slot);
    facing = vec_dot(forward, dir);
    if (facing < 45000) {
        if (vec_dot(forward, VEC(car, VELOCITY)) < 0) {
            if (skill >= 2)
                ai_press_brake(car);
            ai_press_accelerate(car);
            backing = 1;
        } else if (facing < 0x8000) {
            if (skill <= 1) {
                if (speed < crawl)
                    ai_press_accelerate(car);
            } else {
                ai_press_brake(car);
                if (speed < crawl)
                    ai_press_accelerate(car);
                else
                    ai_press_handbrake(car);
            }
        } else if (speed < (int16_t)slow) {
            ai_press_accelerate(car);
        }
    } else if ((uint16_t)target == 0x1fe || (U32(car, 0x6dc) & 0x200)) {
        ai_press_accelerate(car);
    } else if (speed < (int32_t)(target & 0xffff)) {
        ai_press_accelerate(car);
    } else if (speed > (int32_t)(target & 0xffff) + 100 && abs32(I32(car, 0x634)) < 0x7d00) {
        ai_press_brake(car);
    }
    limit = kind >= 0x14 ? 2000000 : kind >= 10 ? 3000000 : 4000000;
    if (!backing && speed > (int16_t)slow && abs32(I32(car, ANGULAR_MOMENTUM + 4)) > limit)
        ai_release_accelerate(car);
}

/* 0x4257c0: v (the way to the node) as a unit vector; once close to or
 * past the node, move on to the next one and aim at that. */
void GAMEAPI ai_aim_at_node(uint8_t *car, int32_t *v, uint32_t line_index, uint32_t node)
{
    const uint8_t *line = LINE(line_index);
    int32_t length;
    uint16_t next;

    if (abs32(v[0]) <= 0xb00000 && abs32(v[1]) <= 0xb00000 && abs32(v[2]) <= 0xb00000) {
        length = vec_length(v);
    } else {
        int16_t s[3] = {(int16_t)(v[0] >> 16), (int16_t)(v[1] >> 16), (int16_t)(v[2] >> 16)};

        length = (int32_t)(svec_length(s) << 16);
    }
    if (length <= 0x140000 || node_passed(node, line, VEC(car, POSITION))) {
        if (line_next(node, line, &next)) {
            const int16_t *p = (const int16_t *)NODE(line, next);

            *(uint16_t *)(car + 0x6b2) = next;
            v[0] = (int32_t)((uint32_t)p[0] << 16) - I32(car, POSITION);
            v[1] = (int32_t)((uint32_t)p[1] << 16) - I32(car, POSITION + 4);
            v[2] = (int32_t)((uint32_t)p[2] << 16) - I32(car, POSITION + 8);
            vec_set_length(v, 0x10000);
            return;
        }
    }
    if (length > 0)
        vec_div(v, length);
}

/* 0x425940: steer towards target (a direction); with check, a car
 * sliding (and moving) steers by its travel instead when that turns the
 * same way harder, or it is mostly going straight. reverse: steering
 * while backing up. */
void GAMEAPI ai_steer_to(uint8_t *car, const int32_t *target, int32_t check, int32_t reverse)
{
    int32_t forward[3], up[3], along[3], flat[3], turn, ahead, steer, heading, side;

    body_forward(forward, car);
    body_up(up, car);
    vec_scale(vec_dot(target, up), up, along);
    flat[0] = target[0] - along[0];
    flat[1] = target[1] - along[1];
    flat[2] = target[2] - along[2];
    turn = vec_turn_about(forward, flat, up);
    ahead = vec_dot(forward, flat);
    if (reverse) {
        turn = -turn;
        ahead = -ahead;
    }
    steer = ai_steer_amount(turn, ahead, 0x582e, car);
    if (reverse)
        steer = -steer;
    car_heading(car, &heading, &side);
    if (reverse) {
        side = -side;
        heading = -heading;
    }
    if (check && heading < 0xfa00 && S16(car, 0x658) > 100) {
        int32_t slide = ai_steer_amount(side, heading, 0x582e, car);

        if (reverse)
            steer = -steer;
        if (bit27_clear((uint32_t)steer) != bit27_clear((uint32_t)slide) || abs32(steer) <= abs32(slide))
            if (bit27_clear((uint32_t)turn) == bit27_clear((uint32_t)side) || ahead >= 50000)
                steer = slide;
    }
    ai_steer_press(steer, car);
}

/* 0x411140: ease off to let a car behind catch up: the car it waits for
 * (+0x6e3, or else the first in the +0x6cb mask, less so), by how far
 * ahead this car is. Not for the hardest drivers (+0x6e1 == 4). */
void GAMEAPI ai_pace(uint8_t *car)
{
    uint8_t *slot = g_car_slots + *(uint16_t *)(car + 0x64e) * CAR_SLOT_SIZE, *other;
    int32_t index = (int8_t)car[0x6e3], from_mask = 0, gap, shift, ease, floor, limit, boost;

    if (car[0x6e1] == 4)
        return;
    if (index == -1) {
        uint32_t mask = car[0x6cb], count;

        if (slot[0x2c] == g_race[0x586] || !mask)
            return;
        count = g_race[0x54d];
        for (index = 0; (uint32_t)index < count; index++)
            if (mask >> index & 1)
                break;
        if ((uint32_t)index >= count)
            return;
        from_mask = 1;
    }
    other = g_car_slots + index * CAR_SLOT_SIZE;
    gap = (int32_t)((uint32_t)(other[0x2c] - slot[0x2c]) << 16) - I32(slot, 0x34) + I32(other, 0x34);
    if (gap > -800)
        return;
    if (gap > -1500) {
        shift = 0;
        ease = 10;
        floor = 350;
    } else if (gap > -3000) {
        shift = 0;
        ease = 20;
        floor = 330;
    } else if (gap > -8000) {
        shift = 1;
        ease = 50;
        floor = 300;
    } else if (gap > -16000) {
        shift = 1;
        ease = 100;
        floor = 250;
    } else {
        shift = 2;
        ease = 150;
        floor = 250;
    }
    if (from_mask) {
        ease *= 4;
        if (ease > 100)
            return;
        floor -= 100;
    }
    boost = (I32(car, 0x698) - 0x60000) >> (shift + 9);
    if (boost < 0)
        boost = 0;
    limit = S16(*(uint8_t **)other, 0x658) - ease + boost;
    if (limit < floor)
        limit = floor;
    if (S16(car, 0x658) > limit)
        ai_release_accelerate(car);
}

/* 0x425ad0: once the AI has thought this step (+0x6b5 bit 0x40): head
 * for a set point (+0x6b9 modes; mode 5 ends within 200 units) or the
 * next racing-line node (skipping one at speed when told to, +0x6dc
 * 0x200), at the node's speed (or 0xfa in race type 4). */
void GAMEAPI ai_drive(uint8_t *car)
{
    uint16_t line_index = *(uint16_t *)(car + 0x6b0), node = *(uint16_t *)(car + 0x6b2);
    int32_t d[3], speed;

    if (!(car[0x6b5] & 0x40))
        return;
    if (car[0x6b9]) {
        d[0] = I32(car, 0x6bc) - I32(car, POSITION);
        d[1] = I32(car, 0x6c0) - I32(car, POSITION + 4);
        d[2] = I32(car, 0x6c4) - I32(car, POSITION + 8);
        if (car[0x6b9] == 5) {
            int16_t s[3] = {(int16_t)(d[0] >> 16), (int16_t)(d[1] >> 16), (int16_t)(d[2] >> 16)};

            if (svec_length_sq(s) < 40000)
                car[0x6b9] = 0;
        }
    }
    if (!car[0x6b9]) {
        const int16_t *p;
        uint16_t next;

        if ((U32(car, 0x6dc) & 0x200) && S16(car, 0x658) > 300 && line_next(node, LINE(line_index), &next))
            node = next;
        *(uint16_t *)(car + 0x6b0) = line_index;
        *(uint16_t *)(car + 0x6b2) = node;
        p = (const int16_t *)NODE(LINE(line_index), node);
        d[0] = (int32_t)((uint32_t)p[0] << 16) - I32(car, POSITION);
        d[1] = (int32_t)((uint32_t)p[1] << 16) - I32(car, POSITION + 4);
        d[2] = (int32_t)((uint32_t)p[2] << 16) - I32(car, POSITION + 8);
        ai_aim_at_node(car, d, line_index, node);
        speed = g_race[0x551] == 4 ? 0xfa : ((const uint8_t *)p)[7] * 2;
    } else {
        speed = g_race[0x551] == 4 ? 0xfa : NODE(LINE(line_index), node)[7] * 2;
    }
    ai_speed_control(car, (uint32_t)speed);
    ai_steer_to(car, d, 1, 0);
    if (*(uint16_t *)(car + 0x6b4) & 0x8000) {
        U32(car, BUTTONS) &= ~0xa000u;
        *(uint16_t *)(car + 0x6b4) &= 0x7fff;
    }
    ai_pace(car);
}
