/* Cars' wheels and suspension, run from bodies_prepare (0x4621b0 calls
 * 0x466710 first, then 0x465740 for every body).
 *
 * Each car has four wheel blocks of 0xc4 bytes from +0x324 (front left,
 * front right, rear left, rear right; +0 bit 0: touching the ground) and
 * four ground probe records of 0x4c bytes from +0x1f0. Per car and step:
 *   0x40e220  steering: wheel headings from the steering angle (+0x634)
 *   0x476200  ground probes (probes.c)
 *   0x475e80  suspension springs into momentum and angular momentum
 *   0x40d190  drive, brakes and tyre grip (drive.c)
 *   0x486a80  (all cars) wheel forces into momentum (rolling.c)
 *   0x466840  a parked car stops completely
 *   0x466950  a car rolling on its side in the air is slowed
 *   0x4669e0  in the air, a twist towards upright (or upside down)
 *   0x40ef50  spin-around (spin.c) */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "../coverage.h"

typedef void(GAMEAPI *car_fn)(uint8_t *car);

#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_race_data (*(uint8_t **)0x4b1960)
#define g_sin_table ((const int32_t *)0x4b277c)

#define WHEELS 0x324 /* 4 x 0xc4 */
#define WHEEL_SIZE 0xc4
#define PROBES 0x1f0 /* 4 x 0x4c */
#define PROBE_SIZE 0x4c

/* GTE register file (see fixed.c) */
#define g_gte_vector ((int16_t *)0x6ccc64)
#define g_gte_result ((const int32_t *)0x6cccc8)

static int wheel_touching(uint8_t *car, int wheel)
{
    return car[WHEELS + wheel * WHEEL_SIZE] & 1;
}

/* 0x480b10: table sine of an angle in 16.16 radians (0x6487f = 2 pi). */
int32_t GAMEAPI sin_table(int32_t angle)
{
    int32_t a = abs32(angle);

    if (a > 0x6487f)
        a -= 0x6487f;
    if (a < 0x19221)
        return g_sin_table[a >> 9];
    if (a < 0x32440)
        return -g_sin_table[(0x3243f - a) >> 9];
    if (a > 0x3243f && a < 0x4b660)
        return -g_sin_table[(a - 0x3243f) >> 9];
    if (a > 0x4b65f && a < 0x64880)
        return g_sin_table[(0x6487f - a) >> 9];
    return 0;
}

/* 0x40e220: front wheels point along the steering angle (mirrored when the
 * car is upside down, +0x63f), rear wheels along the car's forward axis. */
void GAMEAPI car_steering(uint8_t *car)
{
    int32_t steer = I32(car, 0x634), forward[3], heading[3], i;
    int16_t c, s;

    forward[0] = *(int16_t *)(car + 0xc8) << 4;
    forward[1] = *(int16_t *)(car + 0xce) << 4;
    forward[2] = *(int16_t *)(car + 0xd4) << 4;
    if (car[0x63f] == 1)
        steer = -steer;
    c = (int16_t)(sin_table(steer - 0x19220) >> 4);
    s = (int16_t)(sin_table(steer) >> 4);
    gte_load_matrix(car + MATRIX);
    g_gte_vector[0] = c;
    g_gte_vector[1] = 0;
    g_gte_vector[2] = s;
    /* g_gte_vector[3]: the original copies an uninitialised stack word */
    gte_rotate();
    heading[0] = (int32_t)((uint32_t)g_gte_result[0] << 4);
    heading[1] = (int32_t)((uint32_t)g_gte_result[1] << 4);
    heading[2] = (int32_t)((uint32_t)g_gte_result[2] << 4);
    vec_normalise(heading);
    vec_normalise(forward);
    for (i = 0; i < 3; i++) {
        I32(car, WHEELS + 2 * WHEEL_SIZE + 0x34 + i * 4) = forward[i];
        I32(car, WHEELS + 3 * WHEEL_SIZE + 0x34 + i * 4) = forward[i];
        I32(car, WHEELS + 1 * WHEEL_SIZE + 0x34 + i * 4) = heading[i];
        I32(car, WHEELS + 0 * WHEEL_SIZE + 0x34 + i * 4) = heading[i];
    }
}

/* 0x475e80: suspension. For each wheel on the ground, a spring-damper
 * along the contact normal from the compression (rest length 0x240000) and
 * the speed along the normal, pushing only, at most 40000; the forces and
 * their torques go into the car's momentum. The wheel block gets the
 * contact's slip velocity, position, normal and load. */
void GAMEAPI car_suspension(uint8_t *car)
{
    int32_t force_sum[3] = {0, 0, 0}, torque_sum[3] = {0, 0, 0};
    int wheel;

    for (wheel = 0; wheel < 4; wheel++) {
        uint8_t *probe = car + PROBES + 8 + wheel * PROBE_SIZE; /* edi in the original */
        uint8_t *block = car + WHEELS + 0x14 + wheel * WHEEL_SIZE;
        int32_t n[3], f[3], t[3], d[3], compression, v, filtered, spring, damping, k, i;

        if (!(block[-0x14] & 1))
            continue;
        n[0] = (int32_t)((uint32_t)I32(probe, -8) << 4);
        n[1] = (int32_t)((uint32_t)I32(probe, -4) << 4);
        n[2] = (int32_t)((uint32_t)I32(probe, 0) << 4);
        compression = 0x240000 - I32(probe, 0x40);
        v = -vec_dot(VEC(probe, 4), VEC(probe, 0x34));
        filtered = fix_div16(v - mul16(compression, -0x2aab), 0x532c);
        spring = mul16(mul16(compression, 0xf29b) + mul16(filtered, 0x51b8), -0x241e);
        damping = mul16(-compression, 0x1a8d) + mul16(filtered, 0x4ed2);
        k = (mul16(damping, 0xd8b3) - v + spring) >> 2;
        if (k > 0)
            k = 0;
        else if (k < -40000)
            k = -40000;
        f[0] = mul16(n[0], k);
        f[1] = mul16(n[1], k);
        f[2] = mul16(n[2], k);
        force_sum[0] += f[0];
        force_sum[1] += f[1];
        force_sum[2] += f[2];
        vec_cross(VEC(probe, 0x10), f, t);
        torque_sum[0] += t[0];
        torque_sum[1] += t[1];
        torque_sum[2] += t[2];
        d[0] = I32(probe, 0x1c) - mul16(n[0], v);
        d[1] = I32(probe, 0x20) - mul16(n[1], v);
        d[2] = I32(probe, 0x24) - mul16(n[2], v);
        k = vec_dot(n, d);
        I32(block, -4) = d[0] - mul16(k, n[0]);
        I32(block, 0) = d[1] - mul16(k, n[1]);
        I32(block, 4) = d[2] - mul16(k, n[2]);
        for (i = 0; i < 3; i++) {
            I32(block, 8 + i * 4) = I32(probe, 0x28 + i * 4);
            I32(block, 0x14 + i * 4) = I32(probe, 0x10 + i * 4);
        }
        k = vec_dot(VEC(probe, 0x34), f);
        I32(block, -0x10) = mul16(k, f[0]);
        I32(block, -0xc) = mul16(k, f[1]);
        I32(block, -8) = mul16(k, f[2]);
        for (i = 0; i < 3; i++)
            I32(block, 0x2c + i * 4) = I32(probe, 0x34 + i * 4);
        I32(block, 0x6c) = I32(probe, 0x40);
    }
    I32(car, MOMENTUM) += force_sum[0];
    I32(car, MOMENTUM + 8) += force_sum[2];
    I32(car, ANGULAR_MOMENTUM + 4) += torque_sum[1];
    I32(car, ANGULAR_MOMENTUM) += torque_sum[0];
    I32(car, MOMENTUM + 4) += force_sum[1];
    I32(car, ANGULAR_MOMENTUM + 8) += torque_sum[2];
}

static int32_t abs_sum(uint8_t *body, int offset)
{
    return (int32_t)((uint32_t)abs32(I32(body, offset + 8)) + (uint32_t)abs32(I32(body, offset + 4)) +
                     (uint32_t)abs32(I32(body, offset)));
}

/* 0x466840: a car with no throttle (+0x640) and all wheels down that is
 * barely moving is stopped dead, wheel spin included. +0x63e (set while
 * braking hard?) allows more movement. */
void GAMEAPI car_settle(uint8_t *car)
{
    int32_t move_limit = 8000, turn_limit = 8000;
    int wheel;

    if (I32(car, 0x640))
        return;
    for (wheel = 0; wheel < 4; wheel++)
        if (!car[WHEELS + wheel * WHEEL_SIZE])
            return;
    if (car[0x63e]) {
        move_limit = 38000;
        turn_limit = 48000;
    }
    if (abs_sum(car, MOMENTUM) > move_limit)
        return;
    if ((int32_t)((uint32_t)abs32(I32(car, 0x4c)) + (uint32_t)abs32(I32(car, 0x54)) +
                  (uint32_t)abs32(I32(car, 0x50))) > turn_limit)
        return;
    COVER(COVER_CAR_SETTLED);
    U32(car, 0x4c) = U32(car, 0x50) = U32(car, 0x54) = 0;
    U32(car, 0x40) = U32(car, 0x44) = U32(car, 0x48) = 0;
    for (wheel = 0; wheel < 4; wheel++) {
        int32_t *spin = VEC(car, 0x37c + wheel * WHEEL_SIZE);
        spin[8] -= spin[0];
        spin[0] = 0;
    }
}

/* 0x466950: a car on its side (|matrix[5]| large) spinning fast about y
 * with no wheel down for over 30 steps loses 1/32 of that spin per step. */
void GAMEAPI car_roll_damping(uint8_t *car)
{
    int32_t side = *(int16_t *)(car + 0xce) << 4, spin;
    int wheel;

    if (abs32(side) > 59999) {
        spin = I32(car, ANGULAR_MOMENTUM + 4);
        if (abs32(spin) < 8000000) {
            car[0x65e] = 0;
            return;
        }
        for (wheel = 0; wheel < 4; wheel++)
            if (wheel_touching(car, wheel))
                break;
        if (wheel == 4) {
            car[0x65e]++;
            if (car[0x65e] > 0x1e) {
                car[0x65e] = 0x1e;
                COVER(COVER_CAR_ROLL_DAMPED);
                I32(car, ANGULAR_MOMENTUM + 4) = spin - ((spin + (spin >> 31 & 0x1f)) >> 5);
            }
            return;
        }
    }
    car[0x65e] = 0;
}

/* 0x4669e0: in the air, turns the car's up axis towards world up (or down,
 * whichever is nearer). */
void GAMEAPI car_self_right(uint8_t *car)
{
    int32_t up[3], target[3], axis[3], length;
    int wheel;

    for (wheel = 0; wheel < 4; wheel++)
        if (wheel_touching(car, wheel))
            return;
    up[0] = *(int16_t *)(car + 0xc6) << 4;
    up[2] = *(int16_t *)(car + 0xd2) << 4;
    up[1] = *(int16_t *)(car + 0xcc) * 0x10;
    target[0] = 0;
    target[1] = up[1] >= 0 ? 0x10000 : -0x10000;
    target[2] = 0;
    vec_cross(up, target, axis);
    length = vec_length(axis);
    if (length > 0x200) {
        COVER(COVER_CAR_SELF_RIGHT);
        vec_div(axis, length);
        I32(car, 0x4c) = (I32(car, 0x4c) - (I32(car, 0x4c) >> 7)) + (axis[0] >> 1);
        I32(car, 0x50) = (I32(car, 0x50) - (I32(car, 0x50) >> 7)) + (axis[1] >> 1);
        I32(car, 0x54) = (I32(car, 0x54) - (I32(car, 0x54) >> 7)) + (axis[2] >> 1);
    }
}

/* 0x4800f0: out = a * b (3x3, 16.16). */
void GAMEAPI mat_mul(const int32_t *a, const int32_t *b, int32_t *out)
{
    int r, c, k;

    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++) {
            int32_t sum = 0;
            for (k = 0; k < 3; k++)
                sum += mul16(a[r * 3 + k], b[k * 3 + c]);
            out[r * 3 + c] = sum;
        }
}

/* 0x4801a0: out = diag(d) * transpose(m), m an int16 rotation matrix. */
void GAMEAPI mat_diag_mul_transpose(const int32_t *d, const int16_t *m, int32_t *out)
{
    int c, i;

    for (c = 0; c < 3; c++)
        for (i = 0; i < 3; i++)
            *out++ = mul16(d[c], m[c + 3 * i] << 4);
}

/* 0x465740 (called with body + 0x34): velocity from momentum, the world
 * inverse inertia M * D * M^T from the body-frame diagonal (+0x9c), then
 * angular velocity and spin. */
void GAMEAPI body_prepare(uint8_t *at_position)
{
    uint8_t *body = at_position - POSITION;
    int32_t m[9], half[9], i;

    body_update_velocity(at_position);
    for (i = 0; i < 9; i++)
        m[i] = *(int16_t *)(body + MATRIX + i * 2) << 4;
    mat_diag_mul_transpose(VEC(body, 0x9c), (const int16_t *)(body + MATRIX), half);
    mat_mul(m, half, VEC(body, INVERSE_INERTIA));
    body_update_spin(body);
}

/* 0x466710 */
void GAMEAPI cars_prepare(void)
{
    uint32_t i;

#define EACH_CAR(fn)                                                                                              \
    for (i = 0; i < g_race_data[0x54d]; i++)                                                                       \
    fn(*(uint8_t **)(g_car_slots + i * CAR_SLOT_SIZE))
    EACH_CAR(car_steering);
    EACH_CAR(car_ground_probes);
    EACH_CAR(car_suspension);
    EACH_CAR(car_drive);
#undef EACH_CAR
    cars_wheel_forces();
    for (i = 0; i < g_race_data[0x54d]; i++) {
        uint8_t *car = *(uint8_t **)(g_car_slots + i * CAR_SLOT_SIZE);
        car_settle(car);
        car_roll_damping(car);
        car_self_right(car);
        car_spin_around(car);
    }
}
