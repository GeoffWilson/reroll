/* The wheels' push on each car (0x486a80, after every car's drive): the
 * force a touching wheel carries (wheel +0x4c) becomes momentum, and its
 * turning effect about the wheel's lever (+0x28) becomes angular momentum,
 * eased by how hard the car is to turn about that axis. Then each car's
 * wheel dust (0x4219e0, still the original: it spawns particles). */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

typedef void(GAMEAPI *car_fn)(uint8_t *car);
#define car_wheel_dust ((car_fn)0x4219e0)

#define g_race (*(uint8_t **)0x4b1960)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274

/* 0x486f00: scale v to length (16.16), first shifting it down to 10
 * significant bits; 0 if v is or becomes zero. */
int32_t GAMEAPI vec_set_length_shifted(int32_t *v, int32_t length)
{
    int32_t shift, k;

    if (!(v[0] | v[1] | v[2]))
        return 0;
    shift = 10 - count_leading_zeros_of((uint32_t)(abs32(v[0]) | abs32(v[1]) | abs32(v[2])));
    if (shift > 0) {
        v[0] >>= shift;
        v[1] >>= shift;
        v[2] >>= shift;
    }
    k = vec_length(v);
    if (!k)
        return 0;
    k = fix_div16(length, k);
    v[0] = mul16(v[0], k);
    v[1] = mul16(v[1], k);
    v[2] = mul16(v[2], k);
    return 1;
}

/* 0x486c10: a wheel's torque (lever x force) into torque; past a small
 * size, part of it (wheel +0xc0) is turned back into force, and the rest
 * is reduced by 1 + (wheel +0xbc) x how easily the car turns that way
 * (car inverse inertia, body+0xe4). */
void GAMEAPI wheel_force_torque(int32_t *force, int32_t *torque, const uint8_t *wheel, const uint8_t *car_position)
{
    int32_t length, extra[3], axis[3], turn[3], k;

    vec_cross(VEC(wheel, 0x28), force, torque);
    length = vec_length(torque);
    if (length <= 1000) {
        torque[0] = torque[1] = torque[2] = 0;
        return;
    }
    k = mul16(length, I32(wheel, 0xc0));
    vec_cross(torque, VEC(wheel, 0x28), extra);
    vec_set_length_shifted(extra, k);
    force[0] += extra[0];
    force[1] += extra[1];
    force[2] += extra[2];
    memcpy(axis, torque, 12);
    vec_div(axis, length);
    mat_mul_vec(VEC(car_position, 0xb0), axis, turn);
    k = fix_div16(0x10000, mul16(I32(wheel, 0xbc), vec_length(turn) >> 8) + 0x10000);
    torque[0] = mul16(torque[0], k);
    torque[1] = mul16(torque[1], k);
    torque[2] = mul16(torque[2], k);
}

/* 0x486a80 */
void GAMEAPI cars_wheel_forces(void)
{
    uint32_t i;

    for (i = 0; i < g_race[0x54d]; i++) {
        uint8_t *car = *(uint8_t **)(g_car_slots + i * CAR_SLOT_SIZE);
        int32_t push[3] = {0, 0, 0}, turn[3] = {0, 0, 0}, w;

        for (w = 0; w < 4; w++) {
            const uint8_t *wheel = car + 0x324 + w * 0xc4;
            int32_t force[3], torque[3];

            if (!(wheel[0] & 1))
                continue;
            force[1] = I32(wheel, 0x50);
            force[0] = I32(wheel, 0x4c);
            force[2] = I32(wheel, 0x54);
            wheel_force_torque(force, torque, wheel, car + POSITION);
            push[0] += force[0];
            push[1] += force[1];
            push[2] += force[2];
            turn[0] += torque[0];
            turn[1] += torque[1];
            turn[2] += torque[2];
        }
        I32(car, ANGULAR_MOMENTUM) += turn[0];
        I32(car, ANGULAR_MOMENTUM + 4) += turn[1];
        I32(car, MOMENTUM) += push[0];
        I32(car, ANGULAR_MOMENTUM + 8) += turn[2];
        I32(car, MOMENTUM + 4) += push[1];
        I32(car, MOMENTUM + 8) += push[2];
    }
    for (i = 0; i < g_race[0x54d]; i++)
        car_wheel_dust(*(uint8_t **)(g_car_slots + i * CAR_SLOT_SIZE));
}
