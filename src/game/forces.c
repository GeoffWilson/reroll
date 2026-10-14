/* Forces on one body per step: body_forces (0x466520) and what it calls.
 *
 * Forces and torques are accumulated in the body (+0x74 force, +0x80
 * torque), then added to momentum and angular momentum (0x465bd0), and the
 * velocity follows from the momentum with a speed cap (0x465890):
 *   explosions  0x466b50 and the damping
 *               near the last one (0x466e40)
 *   gravity     mass * g (0x4acc50) for flag-0x1000 bodies, slowed in slow
 *               motion; cars (flags exactly 0x1c03) get drag (0x40e460) and
 *               gravity plus downforce along their own up axis (0x40e4b0)
 *   friction    at resting contacts (0x465d50): sliding friction against the
 *               contact velocity, damping of spin about the normal and a
 *               rolling drag, scaled by the friction coefficient (+0xb4) and
 *               the number of parts touching. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "gamefn.h"
#include "physics.h"
#include "../coverage.h"

typedef int32_t(GAMEAPI *normalise_fn)(const int32_t *v, int32_t *out);
#define vec_normalise_float ((normalise_fn)0x4290b0) /* x87, still the original */
#define blast_direction_adjust GCALL(0x407e90, direction_fit) /* still the original */
#define car_blast_reaction GCALL(0x46d760, blast_mark) /* still the original */

#define g_blast_centre ((int32_t *)0x6ccba0)
#define g_blast_strength (*(int32_t *)0x6ccbb0)

#define g_contact_slots GP(0x6ce740) /* per body part, 0x48 bytes */
#define g_gravity (*(int32_t *)0x4acc50)
#define g_explosion_pending (*(int32_t *)0x6ccbac)
#define g_explosion_damping (*(int32_t *)0x6cca54)
#define g_explosion_centre ((int32_t *)0x6ccb90)
#define g_slow_motion (*(int32_t *)0x5d9b20)
#define g_slow_motion_body GP(0x5d9b24)
#define g_slow_motion_scale (*(int32_t *)0x5d9b2c)

#define FORCE 0x074
#define TORQUE 0x080
#define FRICTION 0x0b4

/* Shake effect state (0x42abe0's globals) */
#define g_shake_on (*(int16_t *)0x6ccb60)
#define g_shake_phase (*(int16_t *)0x6ccb62)
#define g_shake_length (*(int32_t *)0x6ccb64)
#define g_shake_kind (*(int32_t *)0x6ccb68)
#define g_shake_origin ((int32_t *)0x6ccb6c)

static int slowed(uint8_t *body)
{
    uint32_t type = U32(body, FLAGS) & 0x1ff;

    return g_slow_motion && body != g_slow_motion_body && (type == 3 || type == 7 || type == 0x15);
}

static uint8_t *contact_slot(uint8_t *body)
{
    return g_contact_slots + body[(int8_t)body[RIGIDITY] + 0x140] * 0x48;
}

/* 0x42abe0: starts a screen shake at `origin`, unless in slow motion. */
void GAMEAPI shake_start(const int32_t *origin, int32_t kind)
{
    if (g_slow_motion)
        return;
    g_shake_on = 1;
    g_shake_kind = kind;
    g_shake_phase = 0;
    g_shake_length = 0x1f;
    g_shake_origin[0] = origin[0];
    g_shake_origin[1] = origin[1];
    g_shake_origin[2] = origin[2];
}

/* 0x466e40: bodies near the last explosion (0x6ccb90) lose speed; cars
 * whose +0x6ac entry is set are spared. */
void GAMEAPI body_explosion_damping(uint8_t *body)
{
    const char *state;
    int32_t dy = (int32_t)(U32(body, POSITION + 4) - (uint32_t)g_explosion_centre[1]) >> 18;
    int32_t dx = (int32_t)(U32(body, POSITION) - (uint32_t)g_explosion_centre[0]) >> 18;
    int32_t dz = (int32_t)(U32(body, POSITION + 8) - (uint32_t)g_explosion_centre[2]) >> 18;
    int32_t d2 = dz * dz + dy * dy + dx * dx, k;

    if (d2 >= 0x1000)
        return;
    state = (const char *)GP(body + 0x6ac);
    if ((U32(body, FLAGS) & 0x1ff) == 3 && state != (const char *)(uintptr_t)0xffffffffu && *state)
        return;
    k = d2 * 8 + 0x8000;
    I32(body, VELOCITY) = mul16(k, I32(body, VELOCITY));
    I32(body, VELOCITY + 4) = mul16(k, I32(body, VELOCITY + 4));
    I32(body, VELOCITY + 8) = mul16(k, I32(body, VELOCITY + 8));
    I32(body, MOMENTUM) = mul16(k, I32(body, MOMENTUM));
    I32(body, MOMENTUM + 4) = mul16(k, I32(body, MOMENTUM + 4));
    I32(body, MOMENTUM + 8) = mul16(k, I32(body, MOMENTUM + 8));
    shake_start(g_explosion_centre, 2);
}

/* 0x40e460: speed drag on cars, coefficient +0x14b8. */
void GAMEAPI car_drag(uint8_t *car)
{
    int32_t k = *(int16_t *)(car + 0x14b8);

    I32(car, FORCE) -= (I32(car, VELOCITY) >> 16) * k;
    I32(car, FORCE + 4) -= (I32(car, VELOCITY + 4) >> 16) * k;
    I32(car, FORCE + 8) -= (I32(car, VELOCITY + 8) >> 16) * k;
}

/* 0x40e4b0: gravity, and the downforce that keeps cars on walls and
 * ceilings: while a wheel touches (+0x324 + n*0xc4, bit 0), or for 6 steps
 * after the last one left, a push along the car's up axis (towards it or
 * away, by +0x63f, kept in +0x644) replaces gravity's pull, growing with
 * the speed (+0x654) from 0x200000 to 0x300000 and capped at g. */
void GAMEAPI car_gravity(uint8_t *car)
{
    int32_t g = g_gravity, speed, push[3], length, wheel;
    uint32_t flip;

    if (g_slow_motion && car != g_slow_motion_body)
        g = mul16(g_slow_motion_scale, g);
    I32(car, FORCE + 4) += g;
    for (wheel = 0; wheel < 4 && !(car[0x324 + wheel * 0xc4] & 1); wheel++)
        ;
    if (wheel == 4) {
        if (car[0x645] >= 6)
            return;
        car[0x645]++;
        flip = car[0x644];
    } else {
        car[0x645] = 0;
        car[0x644] = car[0x63f];
        flip = car[0x63f];
    }
    speed = I32(car, 0x654);
    if (speed < 0x200000)
        return;
    if (speed > 0x300000)
        speed = 0x300000;
    speed = (speed - 0x200000) >> 2;
    if (flip) {
        push[0] = (int32_t)((uint32_t)-*(int16_t *)(car + 0xc6) << 4);
        push[1] = (int32_t)((uint32_t)-*(int16_t *)(car + 0xcc) << 4);
        push[2] = (int32_t)((uint32_t)-*(int16_t *)(car + 0xd2) << 4);
    } else {
        push[0] = *(int16_t *)(car + 0xc6) << 4;
        push[1] = *(int16_t *)(car + 0xcc) << 4;
        push[2] = *(int16_t *)(car + 0xd2) << 4;
    }
    if (g_slow_motion && car != g_slow_motion_body)
        speed = mul16(g_slow_motion_scale, speed);
    vec_scale(speed, push, push);
    push[1] += g;
    I32(car, FORCE + 4) -= g;
    length = vec_length(push);
    if (length > g_gravity)
        vec_scale(fix_div16(g_gravity, length), push, push);
    I32(car, FORCE + 4) += push[1];
    I32(car, FORCE) += push[0];
    I32(car, FORCE + 8) += push[2];
}

/* 0x419340: how many of the body's parts touched the ground (contact slot
 * flag 0x100, cleared here); if any did, the main slot's +0x3c becomes the
 * distance from the body to its contact point. */
int32_t GAMEAPI body_ground_contacts(uint8_t *body)
{
    uint8_t *slots = g_contact_slots, *slot;
    int32_t parts = (int8_t)body[RIGIDITY], touching = 0, i, dx, dy, dz, d2;

    if (!parts)
        return 0;
    for (i = parts - 1; i >= 0; i--) {
        uint8_t *s = slots + body[0x140 + i] * 0x48;
        if (*(uint16_t *)(s + 0x44) & 0x100)
            touching++;
        *(uint16_t *)(s + 0x44) = 0;
    }
    if (touching > 0) {
        slot = slots + body[parts + 0x140] * 0x48;
        dx = (I32(slot, 0x20) - (I32(body, POSITION) >> 8)) >> 8;
        dy = (I32(slot, 0x24) - (I32(body, POSITION + 4) >> 8)) >> 8;
        dz = (I32(slot, 0x28) - (I32(body, POSITION + 8) >> 8)) >> 8;
        d2 = fix_mul8(dz, dz) + fix_mul8(dy, dy) + fix_mul8(dx, dx);
        I32(slot, 0x3c) = d2;
        I32(slot, 0x3c) = (int32_t)(fix_sqrt_table(d2) << 12);
    }
    return touching;
}

/* 0x466120: the accumulated force's part along the contact normal (and
 * its size in *size). */
void GAMEAPI contact_normal_force(uint8_t *body, uint8_t *contact, int32_t *out, int32_t *size)
{
    const int32_t *n = VEC(contact, CONTACT_NORMAL);
    int32_t d = vec_dot(VEC(body, FORCE), n);

    *size = d;
    out[0] = mul16(d, n[0]);
    out[1] = mul16(*size, n[1]);
    out[2] = mul16(*size, n[2]);
}

/* 0x466240 */
void GAMEAPI body_add_force_torque(uint8_t *body, const int32_t *force, const int32_t *torque)
{
    I32(body, FORCE + 4) += force[1];
    I32(body, FORCE) += force[0];
    I32(body, FORCE + 8) += force[2];
    I32(body, TORQUE + 4) += torque[1];
    I32(body, TORQUE + 8) += torque[2];
    I32(body, TORQUE) += torque[0];
}

/* -(-v >> 16) for negatives: integer part, rounded towards zero */
static int32_t integer_part(int32_t v)
{
    return v < 0 ? (int32_t)(0u - ((0u - (uint32_t)v) >> 16)) : (int32_t)((uint32_t)v >> 16);
}

/* 0x480360: sets the vector's length (components up to 180.0). */
void GAMEAPI vec_set_length_small(int32_t *v, int32_t length)
{
    int32_t x = v[0], y = v[1], z = v[2], s, scale;
    uint32_t sum = (uint32_t)mul16(z, z) + (uint32_t)mul16(y, y) + (uint32_t)mul16(x, x);

    s = (int32_t)fix_sqrt_table(abs32((int32_t)sum));
    if ((int32_t)((uint32_t)s << 8)) {
        scale = mul16(fix_div16(0x10000, (int32_t)((uint32_t)s << 8)), length);
        v[0] = mul16(x, scale);
        v[1] = mul16(y, scale);
        v[2] = mul16(z, scale);
    }
}

/* 0x480270: the same for large components, on their integer parts. */
void GAMEAPI vec_set_length_large(int32_t *v, int32_t length)
{
    uint32_t x = (uint32_t)integer_part(v[0]), y = (uint32_t)integer_part(v[1]), z = (uint32_t)integer_part(v[2]);
    int32_t s = (int32_t)fix_sqrt_table(abs32((int32_t)(z * z + y * y + x * x))), scale;

    if (s) {
        if (s > 0x7fff)
            s = 0x7fff;
        scale = mul16(fix_div16(0x10000, (int32_t)((uint32_t)s << 16)), length);
        v[0] = mul16(v[0], scale);
        v[1] = mul16(v[1], scale);
        v[2] = mul16(v[2], scale);
    }
}

/* 0x480220 */
void GAMEAPI vec_set_length(int32_t *v, int32_t length)
{
    if (abs32(v[0]) <= 0xb40000 && abs32(v[1]) <= 0xb40000 && abs32(v[2]) <= 0xb40000)
        vec_set_length_small(v, length);
    else
        vec_set_length_large(v, length);
}

/* sqrt of the sum of squares, via the table, as 0x465d50 does it */
static int32_t magnitude(int32_t x, int32_t y, int32_t z)
{
    uint32_t sum = (uint32_t)mul16(z, z) + (uint32_t)mul16(y, y) + (uint32_t)mul16(x, x);

    return (int32_t)fix_sqrt_table(abs32((int32_t)sum));
}

/* 0x465d50: friction at a resting contact. `normal_force`: the force into
 * the surface (0x466120), `slip`: the contact point's velocity along the
 * surface, `parts`: how many parts touch. Outputs a force and a torque. */
void GAMEAPI contact_friction(uint8_t *body, const int32_t *normal_force, const int32_t *slip, int32_t *force,
                              int32_t *torque, int32_t parts)
{
    uint8_t *slot = contact_slot(body);
    const int32_t *n = VEC(slot, CONTACT_NORMAL);
    int32_t ratio = 0, slip_speed, pressure, friction, spin, twist[3], along[3], across[3], roll[3];

    slip_speed = (int32_t)((uint32_t)magnitude(slip[0] >> 2, slip[1] >> 2, slip[2] >> 2) << 10);
    if (I32(slot, 0x3c))
        ratio = fix_div16(slip_speed, I32(slot, 0x3c));
    pressure = (int32_t)((uint32_t)magnitude(normal_force[0], normal_force[1], normal_force[2]) << 8);
    friction = (int32_t)((uint32_t)mul16(pressure, I32(body, FRICTION)) * (uint32_t)parts);
    force[0] = -slip[0];
    force[1] = -slip[1];
    force[2] = -slip[2];
    if (ratio < 0x10) {
        /* static friction: hold still */
        force[0] = force[1] = force[2] = 0;
        torque[0] = torque[1] = torque[2] = 0;
        if (abs32(I32(body, ANGULAR_MOMENTUM)) < 10000 && abs32(I32(body, ANGULAR_MOMENTUM + 4)) < 10000 &&
            abs32(I32(body, ANGULAR_MOMENTUM + 8)) < 10000) {
            U32(body, 0x4c) = U32(body, 0x50) = U32(body, 0x54) = 0;
            U32(body, 0xb8) = U32(body, 0xbc) = U32(body, 0xc0) = 0;
        }
        if (abs32(I32(body, MOMENTUM)) < 0x46 && abs32(I32(body, MOMENTUM + 4)) < 0x46 &&
            abs32(I32(body, MOMENTUM + 8)) < 0x46) {
            U32(body, 0x40) = U32(body, 0x44) = U32(body, 0x48) = 0;
            U32(body, 0x68) = U32(body, 0x6c) = U32(body, 0x70) = 0;
        }
    } else {
        if (friction > ratio)
            friction = ratio * 2;
        vec_set_length(force, friction);
        vec_cross(VEC(slot, CONTACT_POINT), force, torque);
    }
    /* damp spin about the normal */
    spin = -vec_dot(VEC(body, ANGULAR_VELOCITY), n);
    twist[0] = mul16(spin, n[0]);
    twist[1] = mul16(spin, n[1]);
    twist[2] = mul16(spin, n[2]);
    vec_set_length(twist, friction * 5);
    torque[0] += twist[0];
    torque[1] += twist[1];
    torque[2] += twist[2];
    /* rolling drag against the velocity along the surface */
    pressure = mul16(pressure, 0xed8);
    vec_split(n, VEC(body, VELOCITY), along, across);
    roll[0] = -across[0];
    roll[1] = -across[1];
    roll[2] = -across[2];
    vec_set_length(roll, (int32_t)((uint32_t)pressure * (uint32_t)parts));
    force[1] += roll[1];
    force[0] += roll[0];
    force[2] += roll[2];
}

/* 0x465bd0: adds force and torque (times dt) to momentum and angular
 * momentum, unless that would take a component past +-0x7d000000 in the
 * direction it already has; then clears them. */
void GAMEAPI body_apply_accumulated(uint8_t *body, int32_t dt)
{
    int i;

    for (i = 0; i < 6; i++) {
        int32_t *m = VEC(body, MOMENTUM) + i, f = I32(body, FORCE + i * 4);
        int32_t next = (int32_t)((uint32_t)mul16(dt, f) + (uint32_t)*m);

        if (abs32(next) < 0x7d000000 || (int32_t)((uint32_t)f ^ (uint32_t)*m) < 0)
            *m = next;
    }
    for (i = 0; i < 6; i++)
        I32(body, FORCE + i * 4) = 0;
}

/* 0x465890 (called with body + 0x34): velocity from momentum, capped: if a
 * component passes 0x490000 and the speed is over 0x80 per step, the
 * velocity is scaled to that speed and the momentum to match. */
void GAMEAPI body_update_velocity(uint8_t *at_position)
{
    uint8_t *body = at_position - POSITION;
    int32_t inverse = I32(body, INVERSE_MASS), x, y, z, scale;
    uint32_t speed2;

    if (inverse == 0x10000) {
        I32(body, VELOCITY) = I32(body, MOMENTUM);
        I32(body, VELOCITY + 4) = I32(body, MOMENTUM + 4);
        I32(body, VELOCITY + 8) = I32(body, MOMENTUM + 8);
    } else {
        I32(body, VELOCITY) = mul16(I32(body, MOMENTUM), inverse);
        I32(body, VELOCITY + 4) = mul16(I32(body, MOMENTUM + 4), I32(body, INVERSE_MASS));
        I32(body, VELOCITY + 8) = mul16(I32(body, MOMENTUM + 8), I32(body, INVERSE_MASS));
    }
    x = I32(body, VELOCITY);
    y = I32(body, VELOCITY + 4);
    z = I32(body, VELOCITY + 8);
    if (abs32(x) <= 0x490000 && abs32(y) <= 0x490000 && abs32(z) <= 0x490000)
        return;
    speed2 = (uint32_t)mul16(z >> 8, z >> 8) + (uint32_t)mul16(y >> 8, y >> 8) + (uint32_t)mul16(x >> 8, x >> 8);
    if (speed2 <= 0x4000)
        return;
    scale = fix_div16(0x800000, (int32_t)((uint32_t)fix_isqrt((int32_t)(speed2 << 8)) << 4));
    x = mul16(scale, x);
    y = mul16(scale, y);
    z = mul16(scale, z);
    I32(body, VELOCITY) = x;
    I32(body, VELOCITY + 4) = y;
    I32(body, VELOCITY + 8) = z;
    I32(body, MOMENTUM) = mul16(I32(body, MASS), x);
    I32(body, MOMENTUM + 4) = mul16(I32(body, MASS), y);
    I32(body, MOMENTUM + 8) = mul16(I32(body, MASS), z);
}

static int32_t clamp_blast(int32_t v)
{
    if (v > 0x1800)
        v = 0x1800;
    return v;
}

/* 0x466b50: the push of an explosion (centre 0x6ccba0, strength
 * 0x6ccbb0) on a body within 256 units, falling off with the square of the
 * distance. Cars note the strongest blast (+0x65f) and its direction
 * (+0x670) and shake (+0x6a8/+0x6aa); a car with +0x6ac set reacts
 * through 0x46d760 instead. Kinds 7 and 0xc get half. */
void GAMEAPI body_explosion_force(uint8_t *body)
{
    int32_t x = I32(body, POSITION), y = I32(body, POSITION + 4), z = I32(body, POSITION + 8);
    int32_t dx = (x - g_blast_centre[0]) >> 18, dy = (y - g_blast_centre[1]) >> 18, dz = (z - g_blast_centre[2]) >> 18;
    int32_t d2 = dz * dz + dy * dy + dx * dx, fx, fy, fz, s;
    uint32_t kind;

    if (d2 > 0x10000)
        return;
    if (d2 < 0x100)
        d2 = 0x100;
    fx = (int32_t)((uint32_t)dx << 16) / d2;
    fy = (int32_t)((uint32_t)dy << 16) / d2;
    fz = (int32_t)((uint32_t)dz << 16) / d2;
    if (abs32(fz) + abs32(fy) + abs32(fx) < 0x20)
        return;
    s = g_blast_strength >> 2;
    fx = clamp_blast(s * fx);
    fy = clamp_blast(s * fy);
    fz = clamp_blast(s * fz);
    if (fx < -0x1800)
        fx = -0x1800;
    if (fy < -0x1800)
        fy = -0x1800;
    if (fz < -0x1800)
        fz = -0x1800;
    kind = U32(body, FLAGS) & 0x1ff;
    if (kind == 3) {
        const char *whole = (const char *)GP(body + 0x6ac);
        int32_t level;

        if (whole != (const char *)(uintptr_t)0xffffffffu && *whole) {
            int32_t direction[3] = {fx, fy, fz};

            COVER(COVER_BLAST_REACTION);
            vec_normalise_float(direction, direction);
            direction[0] >>= 4;
            direction[1] >>= 4;
            direction[2] >>= 4;
            car_blast_reaction(body, direction, 0x70);
            return;
        }
        COVER(COVER_BLAST_CAR);
        level = g_blast_strength * 4;
        if (level >= 0x7f)
            level = 0x7f;
        if (level > body[0x65f]) {
            int32_t *direction = VEC(body, 0x670), length;

            body[0x65f] = (uint8_t)level;
            direction[0] = x - g_blast_centre[0];
            direction[1] = y - g_blast_centre[1];
            direction[2] = z - g_blast_centre[2];
            blast_direction_adjust(direction);
            length = vec_length(direction);
            if (length < 0x40)
                memcpy(direction, body + 0x664, 12);
            else
                vec_div(direction, length);
        }
        *(uint16_t *)(body + 0x6a8) = 0x10;
        *(uint16_t *)(body + 0x6aa) = (uint16_t)(abs32(fz) + abs32(fy) + abs32(fx));
    } else if (kind == 7 || kind == 0xc) {
        COVER(COVER_BLAST_OTHER);
        fx >>= 1;
        fy >>= 1;
        fz >>= 1;
    }
    I32(body, FORCE) += fx << 8;
    I32(body, FORCE + 4) += fy << 8;
    I32(body, FORCE + 8) += fz << 8;
    I32(body, TORQUE) += fx << 7;
    I32(body, TORQUE + 4) += fy << 7;
    I32(body, TORQUE + 8) += fz << 7;
    U32(body, 0x110) &= 0x7fffffff;
}

/* 0x466520: all forces on one body for this step. */
void GAMEAPI body_forces(uint8_t *body)
{
    int32_t parts;

    if (g_explosion_pending == 1) {
        uint32_t flags = U32(body, FLAGS), type = flags & 0x1ff;
        if (flags & 0x20000)
            U32(body, FLAGS) = flags & ~0x20000u;
        else if (type != 0x11 && type != 0x15 && type != 0x19)
            body_explosion_force(body);
    }
    if (g_explosion_damping == 1) {
        uint32_t flags = U32(body, FLAGS), type = flags & 0x1ff;
        if (flags & 0x20000)
            U32(body, FLAGS) = flags & ~0x20000u;
        else if (type != 0x11 && type != 0x15 && type != 0x19)
            body_explosion_damping(body);
    }
    if (I32(body, VELOCITY) || I32(body, VELOCITY + 4) || I32(body, VELOCITY + 8))
        U32(body, 0x110) &= 0x7fffffffu; /* moving: no longer at rest */
    if (U32(body, FLAGS) & 0x1000) {
        if (U32(body, FLAGS) == 0x1c03) {
            car_drag(body);
            car_gravity(body);
        } else if (!(U32(body, 0x110) & 0x80000000u)) {
            int32_t weight = mul16(I32(body, MASS), g_gravity);
            if (slowed(body))
                weight = mul16(g_slow_motion_scale, weight);
            I32(body, FORCE + 4) += weight;
        }
    }
    parts = body_ground_contacts(body);
    if (parts && I32(body, FRICTION)) {
        uint8_t *slot = contact_slot(body);
        int32_t point_velocity[3], normal_force[3], size, along[3], across[3], force[3], torque[3];

        contact_point_velocity(body, slot, point_velocity);
        contact_normal_force(body, slot, normal_force, &size);
        vec_split(VEC(slot, CONTACT_NORMAL), point_velocity, along, across);
        contact_friction(body, normal_force, across, force, torque, parts);
        body_add_force_torque(body, force, torque);
    }
    body_apply_accumulated(body, 0x10000);
    body_update_velocity(body + POSITION);
}
