/* The spin-around (0x40ef50): a timed twist that turns a car to face along
 * the track again, from the car's last cell (+0x11c) and the track's
 * direction there. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"
#include "../coverage.h"

#define car_spin_sparks GCALL(0x468f80, car_respawn_sparks) /* a ring of sparks; still the original */

#define g_tile_data ((const uint32_t *)GP(0x6c1e30)) /* [0]: tiles, [7]: packed normals */
#define g_race (GP(0x4b1960))
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_section_planes (GP(0x6bfb30))

#define SPIN_STATE 0x2284
#define SPIN_TIME 0x2288

/* 0x43d860: which way along the track is a to b, by the car slot's
 * section plane: 1 forwards, -1 back. */
int32_t GAMEAPI track_direction_sign(const int16_t *a, const int16_t *b, uint32_t car_index)
{
    int32_t dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    const int16_t *plane =
        (const int16_t *)(g_section_planes + g_car_slots[(uint16_t)car_index * CAR_SLOT_SIZE + 0x32] * 0x14);

    return plane[8] * dz + plane[7] * dy + plane[6] * dx >= 0 ? 1 : -1;
}

/* 0x43dd20: track sections where the direction along the track is fixed
 * or runs across the grid (tracks 0xb and 0xc): 0 normal, 1 across,
 * 2/3 other special cases, -1 reversed. */
int32_t GAMEAPI track_special_section(const uint8_t *car)
{
    const uint8_t *slot = g_car_slots + *(const uint16_t *)(car + 0x64e) * CAR_SLOT_SIZE;
    uint8_t track = g_race[0x579], section;
    int32_t tile = car[0x11c], row = *(const int16_t *)(car + 0x11e);

    if (track != 0xc && track != 0xb)
        return 0;
    section = slot[0x30];
    if (track == 0xc) {
        if (section == 6) {
            if (tile)
                return 0;
            if (row > 0x17)
                return -1;
            return (row >= 0x10) + 1;
        }
        if (section < 6 || section > 0xd || tile < 4)
            return 0;
        if (tile <= 5)
            return 1;
        if (tile != 0x18)
            return 0;
        if (row > 0x17)
            return -1;
        return row >= 0x13 ? 0 : 1;
    }
    section = slot[0x30];
    if (section == 4) {
        if (tile == 5) {
            if (row > 0x46)
                return -1;
            return row >= 0x3c ? 3 : 1;
        }
        if (tile == 4 || tile == 3)
            return 1;
        return tile == 0x10 ? -1 : 0;
    }
    if (section != 0xb)
        return 0;
    if (tile == 0xa || tile == 2)
        return -1;
    return tile == 8 || tile == 7 ? 1 : 0;
}

/* 0x407a50: the track's direction (dir, along the grid, 1 << 12 per
 * vertex unit) and up (the cell's surface normal) at the car's last
 * cell; 0 if it has none. */
int32_t GAMEAPI track_direction(int32_t *dir, int32_t *up, const uint8_t *car)
{
    uint32_t *normals = (uint32_t *)(uintptr_t)g_tile_data[7], key = U32(car, 0x11c), packed;
    const uint8_t *tile;
    const int16_t *a, *b;
    const uint8_t *cell;
    int32_t width, row, col, index, special, sign;

    if ((int32_t)key < 0)
        return 0;
    tile = GP((uintptr_t)g_tile_data[0] + (key & 0xff) * 4);
    width = I32(tile, 8);
    row = (int16_t)(key >> 16);
    col = (int8_t)(key >> 8);
    index = row * (width + 1) + col;
    b = (const int16_t *)(GP(tile + 0x20) + index * 8);
    cell = GP(tile + 0x50) + (index - row) * 8;
    special = track_special_section(car);
    if (special)
        COVER(COVER_TRACK_SPECIAL);
    a = b;
    if (special > 1) {
        /* across the grid */
        sign = special == 2 ? -1 : 1;
        if (col == width) {
            cell -= 8;
            b -= 4;
        } else {
            a = b + 4;
        }
    } else {
        sign = special;
        if (row == I32(tile, 0xc)) {
            cell -= width * 8;
            b -= (width + 1) * 4;
        } else {
            a = b + (width + 1) * 4;
        }
    }
    if (sign == 0)
        sign = track_direction_sign(a, b, *(const uint16_t *)(car + 0x64e));
    else
        sign = sign == 1 ? 0 : -1;
    dir[0] = (b[0] - a[0]) << 12;
    dir[1] = (b[1] - a[1]) << 12;
    dir[2] = (b[2] - a[2]) << 12;
    if (sign < 0) {
        dir[0] = -dir[0];
        dir[1] = -dir[1];
        dir[2] = -dir[2];
    }
    packed = normals[*(const uint16_t *)(cell + 2)];
    up[0] = (int32_t)((packed & 0xfff00fff) << 2) >> 14;
    up[2] = (int32_t)(packed << 22) >> 14;
    up[1] = (int32_t)((packed & 0xfffffc03) << 12) >> 14;
    return 1;
}

/* 0x40ef50: state 1 lasts 26 steps (sparks at 22), state 2 41 more; while
 * either runs, a torque turns the car: in state 1 about the track's up,
 * then about it towards the track's direction. Momentum and angular
 * momentum lose an eighth a step. */
void GAMEAPI car_spin_around(uint8_t *car)
{
    int32_t dir[3], up[3], sum[3] = {0, 0, 0}, torque[3], scale, n = 0, w;

    switch (I32(car, SPIN_STATE)) {
    case 0:
        return;
    case 1:
        if (++I32(car, SPIN_TIME) > 0x19) {
            I32(car, SPIN_TIME) = 0;
            I32(car, SPIN_STATE) = 2;
        }
        if (I32(car, SPIN_TIME) == 0x16)
            car_spin_sparks(car, 2);
        break;
    case 2:
        if (++I32(car, SPIN_TIME) > 0x28) {
            I32(car, SPIN_TIME) = 0;
            I32(car, SPIN_STATE) = 0;
        }
        break;
    }
    if (!track_direction(dir, up, car))
        return;
    /* the touching wheels' ground normals, if more than one */
    for (w = 0; w < 4; w++) {
        const uint8_t *wheel = car + 0x324 + w * 0xc4;

        if (wheel[0] & 1) {
            sum[0] += I32(wheel, 0x40);
            sum[1] += I32(wheel, 0x44);
            sum[2] += I32(wheel, 0x48);
            n++;
        }
    }
    if (n > 1) {
        int32_t length = vec_length(sum);

        if (length > 0x8000) {
            vec_div(sum, length);
            memcpy(up, sum, 12);
        }
    }
    if (I32(car, SPIN_STATE) == 1) {
        scale = 0x400000;
    } else {
        int32_t forward[3], length, d;

        dir[0] >>= 6;
        dir[1] >>= 6;
        dir[2] >>= 6;
        length = vec_length(dir);
        if (length < 0x20)
            return;
        vec_div(dir, length);
        forward[0] = *(int16_t *)(car + MATRIX + 4) << 4;
        forward[1] = *(int16_t *)(car + MATRIX + 10) << 4;
        forward[2] = *(int16_t *)(car + MATRIX + 16) << 4;
        d = vec_dot(forward, dir);
        scale = (d + 0x11000) << 6;
        vec_cross(forward, dir, sum);
        if (vec_dot(sum, up) > 0)
            scale = -scale;
    }
    COVER(COVER_SPIN_AROUND);
    vec_scale(scale, up, torque);
    I32(car, ANGULAR_MOMENTUM) -= I32(car, ANGULAR_MOMENTUM) >> 3;
    I32(car, ANGULAR_MOMENTUM + 4) -= I32(car, ANGULAR_MOMENTUM + 4) >> 3;
    I32(car, ANGULAR_MOMENTUM + 8) -= I32(car, ANGULAR_MOMENTUM + 8) >> 3;
    I32(car, MOMENTUM) -= I32(car, MOMENTUM) >> 3;
    I32(car, MOMENTUM + 4) -= I32(car, MOMENTUM + 4) >> 3;
    I32(car, MOMENTUM + 8) -= I32(car, MOMENTUM + 8) >> 3;
    I32(car, 0x80) += torque[0];
    I32(car, 0x84) += torque[1];
    I32(car, 0x88) += torque[2];
}
