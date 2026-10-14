/* Per-car updates after the world step: the speedometer and lap clock
 * (0x438620), boost pads (0x47aa00), and placing the wheels' bodies
 * around the car for drawing (0x40de60). */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"

#define g_race GP(0x4b1960)
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_sin_cos ((const int16_t *)0x4a8200) /* 4096 pairs: sin, cos */
#define g_gte_matrix ((int16_t *)0x6ccbe0)
#define g_gte_short ((int16_t *)0x6ccc88) /* x, y, z, 4 bytes apart */
#define g_gte_vector_words ((uint32_t *)0x6ccc64)
#define g_gte_result ((const int32_t *)0x6cccc8)

#define CAR_WHEEL(car, w) ((car) + 0x324 + (w) * 0xc4)

/* 0x4270a0, 0x4270c0: 4096 to a turn, 4096 = 1.0 */
int32_t GAMEAPI fix_sin(uint32_t angle)
{
    return g_sin_cos[(angle & 0xfff) * 2];
}

int32_t GAMEAPI fix_cos(uint32_t angle)
{
    return g_sin_cos[(angle & 0xfff) * 2 + 1];
}

/* 0x426f80: a rotation matrix (int16, 4096 = 1.0) from three angles,
 * with a zero translation after it. */
void GAMEAPI matrix_from_angles(const int16_t *angles, int16_t *m)
{
    int32_t s0 = fix_sin((uint16_t)angles[0]), c0 = fix_cos((uint16_t)angles[0]);
    int32_t s1 = fix_sin((uint16_t)angles[1]), c1 = fix_cos((uint16_t)angles[1]);
    int32_t s2 = fix_sin((uint16_t)angles[2]), c2 = fix_cos((uint16_t)angles[2]);
    int32_t t = (s1 * s0) >> 12, u = (s1 * c0) >> 12;

    m[0] = (int16_t)((c2 * c1) >> 12);
    m[1] = (int16_t)(-(s2 * c1) >> 12);
    m[2] = (int16_t)s1;
    m[3] = (int16_t)((t * c2 + s2 * c0) >> 12);
    m[4] = (int16_t)((c2 * c0 - t * s2) >> 12);
    m[5] = (int16_t)(-(c1 * s0) >> 12);
    m[6] = (int16_t)((s2 * s0 - u * c2) >> 12);
    m[7] = (int16_t)((u * s2 + c2 * s0) >> 12);
    m[8] = (int16_t)((c1 * c0) >> 12);
    *(uint32_t *)(m + 10) = 0;
    *(uint32_t *)(m + 12) = 0;
    *(uint32_t *)(m + 14) = 0;
}

/* 0x427fc0: rotate the short vector at 0x6ccc88 by the GTE matrix, in
 * place. */
void GAMEAPI gte_rotate_short(void)
{
    const int16_t *m = g_gte_matrix;
    int32_t x = g_gte_short[0], y = g_gte_short[2], z = g_gte_short[4];

    g_gte_short[0] = (int16_t)((m[0] * x + m[1] * y + m[2] * z) >> 12);
    g_gte_short[2] = (int16_t)((m[3] * x + m[4] * y + m[5] * z) >> 12);
    g_gte_short[4] = (int16_t)((m[6] * x + m[7] * y + m[8] * z) >> 12);
}

/* 0x40e1a0: the wheel's bounce (+2) eases halfway back each step, plus
 * how far the suspension (+0x80) is from its rest when touching; an
 * eighth of it, negated and limited to 6, is the wheel's drawn drop
 * (+0x72), loaded as the GTE vector with +0x70..0x77. */
void GAMEAPI wheel_bounce(uint8_t *wheel, const uint8_t *car)
{
    int32_t bounce = *(int16_t *)(wheel + 2) / 2, drop;

    if (wheel[0] & 1) {
        int32_t travel = (I32(wheel, 0x80) - 0x1e0000) >> 13;

        if (car[0x63f])
            bounce += travel;
        else
            bounce -= travel;
    }
    *(int16_t *)(wheel + 2) = (int16_t)bounce;
    drop = -(bounce / 8);
    if (drop < -6)
        drop = -6;
    else if (drop > 6)
        drop = 6;
    *(int16_t *)(wheel + 0x72) = (int16_t)drop;
    g_gte_vector_words[0] = U32(wheel, 0x70);
    g_gte_vector_words[1] = U32(wheel, 0x74);
}

/* 0x40dff0: a wheel's body (+0x6c) turned by the steering and its spin
 * (+0x78), in the car's frame, and placed at its mount. */
void GAMEAPI wheel_place(uint8_t *wheel, const uint8_t *car, int32_t steering)
{
    uint8_t *body = GP(wheel + 0x6c);
    int32_t spin = I32(wheel, 0x78);
    int16_t angles[3], m[16];
    int col;

    if (car[0x63f] == 1) {
        steering = -steering;
        spin = -spin;
    }
    angles[0] = 0;
    angles[1] = (int16_t)((steering >> 6) + 0x400);
    angles[2] = (int16_t)((spin >> 12) * -3);
    matrix_from_angles(angles, m);
    for (col = 0; col < 3; col++) {
        g_gte_short[0] = m[col];
        g_gte_short[2] = m[col + 3];
        g_gte_short[4] = m[col + 6];
        gte_rotate_short();
        *(int16_t *)(body + MATRIX + col * 2) = g_gte_short[0];
        *(int16_t *)(body + MATRIX + 6 + col * 2) = g_gte_short[2];
        *(int16_t *)(body + MATRIX + 12 + col * 2) = g_gte_short[4];
    }
    wheel_bounce(wheel, car);
    gte_rotate();
    U32(body, 0xc) = U32(car, 0xc);
    U32(body, 0x11c) = U32(car, 0x11c);
    I32(body, POSITION) = (int32_t)((uint32_t)g_gte_result[0] << 16) + I32(car, POSITION);
    I32(body, POSITION + 4) = (int32_t)((uint32_t)g_gte_result[1] << 16) + I32(car, POSITION + 4);
    I32(body, POSITION + 8) = (int32_t)((uint32_t)g_gte_result[2] << 16) + I32(car, POSITION + 8);
}

/* 0x40de60: all four wheels; only the front pair steer. AI cars (kind 8)
 * draw the steering eased a quarter of the way each step (+0x638). */
void GAMEAPI car_wheels_place(uint8_t *car)
{
    int32_t steering = I32(car, 0x634), w;

    if (car[0x70c] == 8) {
        int32_t shown = I32(car, 0x638);

        shown += steering / 4 - shown / 4;
        I32(car, 0x638) = shown;
        steering = shown;
    }
    gte_load_matrix(car + MATRIX);
    for (w = 0; w < 4; w++)
        wheel_place(CAR_WHEEL(car, w), car, w < 2 ? steering : 0);
}

/* 0x439910: a car slot's four times (+0x3c..0x48, 1/2048 s units?) as
 * eight packed digits each (+0x4c..0x58), lowest nibble first:
 * minutes, minutes, ':', seconds, seconds, ':', hundredths, and a last
 * digit from the raw time. -1 is blank (0xb) with the separators kept.
 * Without all, the second and fourth are skipped. */
void GAMEAPI slot_time_digits(int32_t all, uint8_t *slot)
{
    int32_t i, d;

    for (i = 0; i < 4; i++) {
        int32_t raw = I32(slot, 0x3c + i * 4), t = raw >> 11;
        uint32_t packed = 0, digit = 0;

        if (!all && (i == 1 || i == 3))
            continue;
        for (d = 0; d < 8; d++) {
            if (raw == -1) {
                digit = d == 2 || d == 5 ? 10 : 11;
            } else {
                switch (d) {
                case 0: digit = (uint32_t)((t / 6000) % 10); break;
                case 1: digit = (uint32_t)((t / 600) % 10); break;
                case 2: case 5: digit = 10; break;
                case 3: digit = (uint32_t)((t / 100) % 6); break;
                case 4: digit = (uint32_t)((t / 10) % 10); break;
                case 6: digit = (uint32_t)(t % 10); break;
                case 7: digit = (((uint32_t)(raw >> 4) * 10) >> 7) % 10; break;
                }
            }
            packed = packed >> 4 | digit << 28;
        }
        U32(slot, 0x4c + i * 4) = packed;
    }
}

/* 0x4351c0: the race clock, 1/2048 s units?. */
uint32_t GAMEAPI race_clock(void)
{
    return (g_race_ticks << 11) / 3;
}

/* 0x438620: the car's speed (+0x654, 16.16 per step) and the shown
 * speed (+0x658, best +0x65a) once racing (phase 4+); and the lap and
 * race clocks (+0x3c, +0x44) for cars still racing. */
void GAMEAPI car_speed_and_clock(uint8_t *slot)
{
    uint8_t *car = GP(slot);
    int32_t phase = I32(g_race, 0x57c);
    int32_t vx = I32(car, VELOCITY) >> 8, vy = I32(car, VELOCITY + 4) >> 8, vz = I32(car, VELOCITY + 8) >> 8;
    int32_t speed = (int32_t)(fix_sqrt_table(vz * vz + vy * vy + vx * vx) << 8);

    I32(car, 0x654) = speed;
    if (I32(g_race, 0x57c) < 4) {
        *(int16_t *)(car + 0x658) = 0;
    } else {
        int16_t shown = (int16_t)((speed * 108 / 16) >> 16);

        *(int16_t *)(car + 0x658) = shown;
        if (shown > *(int16_t *)(car + 0x65a))
            *(int16_t *)(car + 0x65a) = shown;
    }
    if (phase >= 4 && slot[0x2c] <= g_race[0x586]) {
        uint32_t now = race_clock();

        I32(slot, 0x3c) = (int32_t)(now - U32(slot, 0x5c));
        I32(slot, 0x44) = (int32_t)(now - U32(slot, 0x60));
        slot_time_digits(0, slot);
    }
}

/* 0x47aa00: a wheel on a boost surface (its cell's kind 1) adds to the
 * car's boost (+0x698), less with flag 0x40 of +0x700; once a step. */
void GAMEAPI car_boost(uint8_t *slot)
{
    uint8_t *car = GP(slot);
    int32_t w;

    for (w = 0; w < 4; w++) {
        const uint8_t *surface = GP(car + 0x198 + w * 0x1c);

        if (surface && (U32(surface, 0xc) & 0xff) == 1) {
            I32(car, 0x698) += (U32(car, 0x700) & 0x40) ? 0x3000 : 0x5000;
            return;
        }
    }
}
