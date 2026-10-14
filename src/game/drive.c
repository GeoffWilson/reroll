/* Drive, brakes and tyre grip: car_drive (0x40d190) and what it calls,
 * once per car and step from cars_prepare (wheels.c).
 *
 * Per wheel block (car + 0x324 + n * 0xc4):
 *   +0x00 bit 0  on the ground
 *   +0x10        contact point velocity (from the suspension)
 *   +0x28        contact position (body frame)
 *   +0x34        heading (steering, wheels.c)
 *   +0x4c        tyre force for this step
 *   +0x58        wheel spin; +0x78 wheel angle (spin added each step)
 *   +0x7c        tyre slip speed
 *   +0x9c        car slot index
 * Car fields: +0x640 throttle, +0x63e brake (byte), +0x698 front and
 * +0x2274 extra rear drive, +0x227c spin where drive starts to fall off,
 * +0x14ba tyre/engine scale, +0x2278 grip bonus, +0x6a0 damage, +0x2284
 * forced low grip. Skid marks (0x46ecb0) and dust/sparks (0x42b4d0) are
 * still the original code. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"
#include "../coverage.h"

#define wheel_skid_marks GCALL(0x46ecb0, wheel_skid_marks)
#define wheel_sparks GCALL(0x42b4d0, wheel_sparks)

#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_race_data (GP(0x4b1960))
#define g_track_tiles ((const uint32_t *)GP(0x6bfac0))
#define g_surface_limit (*(uint32_t *)0x6bfb1c)
#define g_slow_motion (*(int32_t *)0x5d9b20)
#define g_slow_motion_body (GP(0x5d9b24))
#define g_slow_motion_scale (*(int32_t *)0x5d9b2c)

#define WHEELS 0x324
#define WHEEL_SIZE 0xc4
#define SPIN_OFFSET 0x58

/* 0x40d8a0: wheel spin from the road (`road`: the tyre force along the
 * heading) and the drive/brake torque; the spin may stop at zero but not
 * flip sign in one step. The wheel angle follows the spin. */
void GAMEAPI wheel_spin(uint8_t *wheel, int32_t road, int32_t torque)
{
    int32_t old = I32(wheel, SPIN_OFFSET);
    int32_t spin = mul16(mul16(road, -0x1b333) + torque, 0x22f5c) + old;
    uint8_t *car;

    if (old == 0)
        I32(wheel, SPIN_OFFSET) = spin;
    else
        I32(wheel, SPIN_OFFSET) = (int32_t)((uint32_t)(old ^ spin) >> 31) ? 0 : spin;
    spin = I32(wheel, SPIN_OFFSET);
    car = GP(g_car_slots + wheel[0x9c] * CAR_SLOT_SIZE);
    if (g_slow_motion && car != g_slow_motion_body)
        I32(wheel, 0x78) += mul16(g_slow_motion_scale, spin);
    else
        I32(wheel, 0x78) += spin;
}

/* x - x*n/32 with the product wrapping and the division rounding towards
 * zero, as imul / cdq / sar do */
static int32_t less_32ths(int32_t x, int32_t n)
{
    return x - (int32_t)((uint32_t)x * (uint32_t)n) / 32;
}

/* 0x40d760: tyre force from the contact velocity. Below 8 times the grip
 * the tyre holds (an eighth of the velocity cancelled); above, it slides
 * with a force of the grip. Surfaces 0..31 cut grip by n/32. Returns 1 if
 * the tyre is sliding fast (sparks). */
int32_t GAMEAPI tyre_grip(int32_t torque, uint8_t *wheel, const int32_t *velocity, int32_t *force, int32_t index,
                          int32_t grip, int32_t surface)
{
    int32_t speed, sliding = 0, scale;

    (void)index;
    force[0] = velocity[0];
    force[1] = velocity[1];
    force[2] = velocity[2];
    speed = vec_length(force);
    wheel_skid_marks(wheel, speed, surface);
    if (surface < 0)
        surface = 0;
    grip = less_32ths(grip, surface);
    if (speed < (int32_t)((uint32_t)grip * 8)) {
        COVER(COVER_TYRE_HOLD);
        force[0] = -(force[0] / 8);
        force[1] = -(force[1] / 8);
        force[2] = -(force[2] / 8);
    } else {
        COVER(COVER_TYRE_SLIDE);
        scale = fix_div16(-grip, speed);
        force[0] = mul16(force[0], scale);
        force[1] = mul16(force[1], scale);
        force[2] = mul16(force[2], scale);
        speed = less_32ths(speed, surface);
        if (speed >= 0x30000)
            sliding = 1;
    }
    I32(wheel, 0x7c) = speed;
    wheel_spin(wheel, vec_dot(force, VEC(wheel, 0x34)), torque);
    return sliding;
}

/* 0x40d6f0: kind of track surface under the car: 0 none, 1 or 2. */
int32_t GAMEAPI surface_kind(uint8_t *car)
{
    uint32_t where = U32(car, 0x11c), value;
    uint8_t *tile;

    if (where == 0xffffffff)
        return 0;
    tile = (uint8_t *)(uintptr_t)g_track_tiles[where & 0xff];
    value = U32(GP(tile + 0x20),
                4 + ((I32(tile, 8) + 1) * (int16_t)(where >> 16) + (int8_t)(where >> 8)) * 8) >> 22;
    if (value >= 0x3f0)
        return 0;
    if (value < (g_surface_limit & 0xffff))
        return 0;
    return value < 0x3d0 ? 1 : 2;
}

/* 0x40d490: grip for this step, and a drive boost shift while recovering
 * from a power-slide. +0x648 counts steps travelling well off the car's
 * heading while +0x64c (set by hard braking) runs; +0x64a then gives a
 * spell of extra grip. Scaled by the tyres (+0x14ba), damage (+0x6a0) and
 * the difficulty (race +0x553). */
void GAMEAPI grip_level(uint8_t *car, int32_t *grip, int32_t *shift)
{
    int16_t *sliding = (int16_t *)(car + 0x648), *recover = (int16_t *)(car + 0x64a);
    int16_t *brake_timer = (int16_t *)(car + 0x64c);
    int32_t v[3], speed, ratio, k, forward[3];

    if ((int32_t)((uint32_t)car[0x63e] << 8) > 0x8000)
        *brake_timer = 0x1e;
    if (*sliding == 0) {
        if (*brake_timer > 0)
            (*brake_timer)--;
        else
            *brake_timer = 0;
    }
    v[0] = I32(car, VELOCITY) >> 4;
    v[1] = I32(car, VELOCITY + 4) >> 4;
    v[2] = I32(car, VELOCITY + 8) >> 4;
    speed = vec_length(v);
    if (speed <= 0x8000) {
        ratio = 0x10000;
        goto straight;
    }
    forward[0] = *(int16_t *)(car + 0xc8) << 4;
    forward[1] = *(int16_t *)(car + 0xce) << 4;
    forward[2] = *(int16_t *)(car + 0xd4) << 4;
    ratio = fix_div16(vec_dot(forward, v), speed);
    if (ratio >= 0xec54)
        goto straight;
    if (*brake_timer) {
        (*sliding)++;
        *recover = 0;
        COVER(COVER_POWER_SLIDE);
        *grip = 0x3400;
        goto chosen;
    }
    goto pick;
straight:
    if (*recover == 0) {
        int32_t steps = *sliding * 8;
        *recover = (int16_t)(steps > 0x3c0 ? 0x3c0 : steps);
    }
    *sliding = 0;
pick:
    if (*recover) {
        COVER(COVER_SLIDE_RECOVERY);
        (*recover)--;
        *grip = 0x6000;
    } else {
        *grip = 0x4e00;
    }
chosen:
    *shift = ratio > 0xfa00 && *recover ? 3 : 0;
    k = *(int16_t *)(car + 0x14ba) << 4;
    k += (k * 3 - 0x3000) / 16;
    if (I32(g_race_data, 0x57c) >= 4 && I32(car, 0x6a0) > 0)
        k = mul16(k, 0x10000 - I32(car, 0x6a0));
    if (g_race_data[0x553] & 1)
        k = mul16(k, 0xd000);
    else if (g_race_data[0x553] & 2)
        k = mul16(k, 0xb000);
    *grip = mul16(*grip, k);
    *grip += I32(car, 0x2278);
    if (*grip < 0xc00)
        *grip = 0xc00;
    if (I32(car, 0x640) < 30000)
        *grip += *grip >> 2;
    if (I32(car, 0x2284) == 1)
        *grip = 0xc00;
}

/* 0x40d190 */
void GAMEAPI car_drive(uint8_t *car)
{
    int32_t grip, shift, surface = 0, brake, i;

    grip_level(car, &grip, &shift);
    surface = surface_kind(car);
    brake = car[0x63e] << 8;
    for (i = 0; i < 4; i++) {
        uint8_t *wheel = car + WHEELS + i * WHEEL_SIZE;
        uint8_t *probe = GP(car + 0x198 + i * 0x1c);
        int32_t *spin = VEC(wheel, SPIN_OFFSET), drive, torque, rolling, total;

        if (abs32(*spin) > 3000000)
            *spin = *spin <= 0 ? -3000000 : 3000000;
        if (!(car[WHEELS + (i ^ 1) * WHEEL_SIZE] & 1)) {
            drive = 0;
        } else {
            drive = I32(car, 0x2274) + I32(car, 0x698);
            if (i <= 1)
                drive = I32(car, 0x698);
            if (abs32(*spin) >= I32(car, 0x227c))
                drive = abs32(fix_div16(drive, *spin));
            if (shift)
                drive += drive >> (shift & 31);
        }
        drive = mul16(drive, *(int16_t *)(car + 0x14ba) << 4);
        torque = mul16(I32(car, 0x640), drive);
        if (*spin == 0) {
            rolling = 0;
        } else {
            rolling = abs32(*spin / 256) + 0xd9a;
            if (i >= 2)
                rolling += brake * 4;
            else
                rolling += brake >> 2;
            if (*spin > 0)
                rolling = -rolling;
        }
        total = torque + rolling;
        I32(wheel, 0x7c) = 0;
        if (!(wheel[0] & 1)) {
            wheel_spin(wheel, 0, total);
            U32(wheel, 0xa8) &= ~0x8000u;
        } else {
            int32_t k = mul16(*spin, -0x1b333), velocity[3], force[3], kind;

            velocity[0] = I32(wheel, 0x10) + mul16(I32(wheel, 0x34), k);
            velocity[1] = I32(wheel, 0x14) + mul16(I32(wheel, 0x38), k);
            velocity[2] = I32(wheel, 0x18) + mul16(I32(wheel, 0x3c), k);
            I32(wheel, 0x60) = velocity[0];
            I32(wheel, 0x64) = velocity[1];
            I32(wheel, 0x68) = velocity[2];
            if (surface == 1)
                kind = 0x1a;
            else if (!probe)
                kind = -1;
            else
                kind = (U32(probe, 0xc) >> 8) & 0x1f;
            if (tyre_grip(total, wheel, velocity, force, i, grip, kind) && i >= 2 && probe) {
                uint32_t type = U32(probe, 0xc) & 0xff;
                if (type != 5 && type != 6 && type != 8 && type != 9) {
                    int32_t position[3];
                    position[0] = I32(wheel, 0x28) + I32(car, POSITION);
                    position[1] = I32(wheel, 0x2c) + I32(car, POSITION + 4);
                    position[2] = I32(wheel, 0x30) + I32(car, POSITION + 8);
                    COVER(COVER_TYRE_SPARKS);
                    wheel_sparks(car, position, velocity);
                }
            }
            I32(wheel, 0x4c) = force[0];
            I32(wheel, 0x50) = force[1];
            I32(wheel, 0x54) = force[2];
        }
    }
}
