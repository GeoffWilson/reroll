/* After the world step (sim_step 0x444120): the falling rock that drops
 * near a car (0x458010), the cars' engine glow (0x47b8f0) and the
 * shrapnel pieces flying off wrecks and explosions (0x462840).
 *
 * The chase/replay camera director the step runs next (0x408360, once per
 * viewport counted by 0x40d040) is in gl_ga.c. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"

#define g_race GP(0x4b1960)
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_seed (*(uint32_t *)0x4b11dc)
#define g_glow_seed (*(uint32_t *)0x4b2430)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define SLOT_CAR(i) GP(g_car_slots + (i) * CAR_SLOT_SIZE)
#define RACE_CARS(race) ((race)[0x54d])

#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define I16(p, off) (*(int16_t *)((p) + (off)))

#define explosion_shake GCALL(0x42aa30, explosion_effects)     /* group F */
#define rock_land GCALL(0x458320, pickup_fallen)             /* group F */
#define body_place GCALL(0x419150, body_spheres_set)          /* group F */
#define loop_sound GCALL(0x471e60, sound_moving_flagged)     /* sound */
#define glow_light GCALL(0x441d00, light_add)     /* not ours */
#define grid_link_fn GCALL(0x460fb0, object_grid_link)
#define body_update_velocity_fn GCALL(0x465890, body_update_velocity)
#define grid_unlink_piece GCALL(0x4612a0, object_grid_unlink2)
#define track_cell_fn GCALL(0x419420, track_cell)

static uint32_t next_seed(uint32_t *seed)
{
    *seed = *seed * 16807 & 0x7fffffff;
    return *seed;
}

/* ---- the falling rock ---- */

/* One rock (0x6b9d50: body, active count, sound slot), dropped from high
 * above a car. */
#define g_rock_index (*(uint32_t *)0x6b9d44)
#define g_rock_last_drop (*(uint32_t *)0x6b9d48)
#define ROCK(i) ((uint8_t *)0x6b9d50 + (i) * 0xc)
#define g_rock_target (*(uint32_t *)0x4d074c)
#define g_sound_limit (*(int32_t *)0x4b1c8c)
#define SURFACE(i) GP(GP(0x6bfac0) + (i) * 4) /* the surface table: pointers */
#define g_cell_info GP(0x6bfb40)

/* 0x458180: drop the rock above the slot whose position is `at`, offset
 * by up to half a unit at random, unless it is already falling or the spot
 * is off the track or over a surface flagged 8. */
void GAMEAPI rock_drop(int32_t *at)
{
    uint32_t index = g_rock_index;
    uint8_t *rock = ROCK(index);
    uint8_t *body;
    uint32_t r;
    int32_t x, y, z, off_track = (int32_t)(uintptr_t)at; /* the original reuses the argument's slot */
    uint8_t *cell;

    if (U32(rock, 4) != 0)
        return;
    g_rock_index = index + 1 >= 1 ? 0 : index + 1;
    body = GP(rock);
    I32(body, POSITION + 4) = at[1];
    r = next_seed(&g_seed);
    I32(body, 0x40) = 0;
    I32(body, 0x48) = 0;
    I32(body, 0x44) = 0x800000;
    r = (r >> 15) * 0x7fff >> 16;
    y = I32(body, POSITION + 4) - 0x18000000;
    x = (int32_t)((uint32_t)*(int16_t *)((uint8_t *)at + 0x94) << 16) + at[0] +
        (int32_t)(((r & 0xff) - 0x80) << 16);
    z = (int32_t)(((r & 0xff00) - 0x8000) << 8) + (int32_t)((uint32_t)*(int16_t *)((uint8_t *)at + 0xa0) << 16) +
        at[2];
    I32(body, POSITION + 4) = y;
    I32(body, POSITION) = x;
    I32(body, POSITION + 8) = z;
    cell = track_cell_fn(x, y, z, &off_track);
    if (off_track != 0)
        return;
    if (SURFACE(g_cell_info[2 + (U32(cell, 4) >> 16 & 0x3fff) * 6] & 0x7f)[4] & 8)
        return;
    body_update_velocity_fn(GP(rock) + POSITION);
    grid_link_fn(GP(rock));
    body = GP(rock);
    U32(rock, 4) = 1;
    body_place(body, 1, (const void *)0x4b0b48, 0);
    if ((int32_t)rock[8] >= g_sound_limit) {
        body = GP(rock);
        rock[8] = (uint8_t)loop_sound(1, 0x1e, 9, 0x800, 16000, (uint32_t)(uintptr_t)(body + POSITION),
                             (uint32_t)(uintptr_t)(body + VELOCITY), 0);
    }
}

/* 0x458010: while the rock falls, shake and mark the last three trail
 * records, and land it after 200 steps or once it touches; and some time
 * after the last drop, now and then drop it on a car (alternating between
 * the two players in a split-screen race). */
void GAMEAPI rock_update(void)
{
    uint8_t *race = g_race;
    int32_t at[3], zero[3];
    uint32_t record;
    int i;

    if (race[0x554] == 0)
        return;
    if (U32(ROCK(0), 4) != 0) {
        uint8_t *body = GP(ROCK(0));

        at[0] = I32(body, POSITION);
        at[1] = I32(body, POSITION + 4);
        at[2] = I32(body, POSITION + 8);
        zero[0] = zero[1] = zero[2] = 0;
        explosion_shake(at, zero, 0, 8, 0);
        record = *(uint32_t *)0x6ccb88;
        for (i = 3; i != 0; i--) {
            record -= 0x100;
            if (record < *(uint32_t *)0x6ccb5c)
                record = *(uint32_t *)0x6ccb84 - 0x100;
            *(uint16_t *)(uintptr_t)(record + 0x96) = 0x10;
        }
        U32(ROCK(0), 4) += 1;
        if (U32(ROCK(0), 4) > 200)
            rock_land(GP(ROCK(0)));
        if (GP(ROCK(0))[0xc] & 1)
            rock_land(GP(ROCK(0)));
    }
    if (g_rock_last_drop + 900 < g_race_ticks) {
        uint32_t r = next_seed(&g_seed);

        if ((int32_t)((r >> 15) * 0x7fff >> 16) < 400) {
            if (g_race[0x552] != 0)
                g_rock_target ^= 1;
            rock_drop((int32_t *)(SLOT_CAR(g_rock_target) + POSITION));
        }
    }
}

/* ---- engine glow ---- */

/* 0x467cf0: hue (degrees, 360 = 0), saturation and value (0..0x400) to
 * red, green and blue 0..31. */
void GAMEAPI hsv_to_rgb(int32_t hue, int32_t saturation, int32_t value, int32_t *red, int32_t *green,
                        int32_t *blue)
{
    int32_t r = 0, g = 0, b = 0;

    if (saturation == 0) {
        r = g = b = value;
    } else {
        int32_t sector, fraction, p, q, t;

        if (hue == 360)
            hue = 0;
        sector = (int32_t)((uint32_t)(hue / 60) << 10);
        fraction = (int32_t)((uint32_t)hue << 10) / 60 - sector;
        p = (0x400 - saturation) * value / 0x400;
        q = (0x400 - fraction * saturation / 0x400) * value / 0x400;
        t = (0x400 - (0x400 - fraction) * saturation / 0x400) * value / 0x400;
        switch (sector) {
        case 0: r = value; g = t; b = p; break;
        case 0x400: r = q; g = value; b = p; break;
        case 0x800: r = p; g = value; b = t; break;
        case 0xc00: r = p; g = q; b = value; break;
        case 0x1000: r = t; g = p; b = value; break;
        case 0x1400: r = value; g = p; b = q; break;
        }
    }
    *red = (int32_t)((uint32_t)r + 1) / 33;
    *green = (int32_t)((uint32_t)g + 1) / 33;
    *blue = (int32_t)((uint32_t)b + 1) / 33;
}

/* 0x47bae0: the glow's levels: +0x2e8 follows the engine load (+0x5c8,
 * +0x504) up quickly and down 8 a step, +0x2ea jumps to full while the
 * boost (+0x690, +0x692) runs, and +0x2de/+0x2dd flash on the boost
 * button (+0x704 bit 0x40). */
void GAMEAPI glow_levels(const uint8_t *car, uint8_t *glow)
{
    int32_t level = I16(glow, 0x2e8), boost = I16(glow, 0x2ea);
    int8_t flash = (int8_t)glow[0x2de];
    int32_t load = ((I32(car, 0x5c8) >> 8) + (I32(car, 0x504) >> 8)) >> 6;

    if (load > level) {
        level += (load - level) >> 1;
        if (level > 0xff)
            level = 0xff;
    } else if (level != 0) {
        level -= 8;
        if (level < 0)
            level = 0;
    }
    I16(glow, 0x2e8) = (int16_t)level;
    if (I16(car, 0x690) != 0) {
        if (I16(car, 0x692) >= 0xf)
            boost = 0xff;
    } else if (boost != 0) {
        boost -= 8;
        if (boost < 0)
            boost = 0;
    }
    I16(glow, 0x2ea) = (int16_t)boost;
    if (car[0x704] & 0x40)
        flash = 10;
    else if (flash != 0)
        flash--;
    glow[0x2de] = (uint8_t)flash;
    if (flash == 0)
        glow[0x2dd] = 0;
    else if (flash >= 8)
        glow[0x2dd] = 0x7f;
    else
        glow[0x2dd] = (uint8_t)(flash * 0x80 / 8);
}

/* 0x47bbd0: push the glow model's vertices (indices at +0x2e0, half
 * strength where +0x2e4 is 1) out by the level. */
void GAMEAPI glow_vertices(uint8_t *glow, int32_t level, int32_t flash)
{
    uint8_t *verts = GP(GP(glow + 4) + 0x24);
    int32_t amount, i;

    level += (int32_t)((uint32_t)(flash + 0x10) << 9);
    if (level > 0x10000)
        level = 0x10000;
    if ((int8_t)glow[0x2dc] <= 0)
        return;
    amount = (int32_t)((uint32_t)level << 5) >> 16;
    for (i = 0; i < (int8_t)glow[0x2dc]; i++) {
        int32_t push = glow[0x2e4 + i] == 1 ? amount / 8 : amount;

        *(int16_t *)(verts + 4 + glow[0x2e0 + i] * 8) = (int16_t)-(int16_t)(push + I16(glow, 2));
    }
}

/* 0x47b9a0: one car's glow: levels, flicker counter, vertices and colour
 * (hue by +0x2ea, saturation by distance from the middle, value by level). */
void GAMEAPI car_glow(uint8_t *car)
{
    uint8_t *glow = car + 0x1108;
    int32_t level, hue, saturation, value, red, green, blue;
    uint8_t flicker;

    glow_levels(car, glow);
    level = (int32_t)((uint32_t)I16(glow, 0x2e8) << 8);
    flicker = glow[0x2f5];
    glow[0x2f4] += 4;
    if (flicker != 0) {
        glow[0x2f5] = flicker + 1;
        if (flicker > 0x23)
            glow[0x2f5] = 0;
    }
    if ((next_seed(&g_glow_seed) >> 15 & 0x3ff) == 0xa0)
        glow[0x2f5] = 1;
    glow_vertices(glow, level, (int8_t)glow[0x2dd]);
    hue = I16(glow, 0x2ea) < 0x80 ? I16(glow, 0x2ec) : I16(glow, 0x2ee);
    saturation = 0x80 - I16(glow, 0x2ea);
    if (saturation < 0)
        saturation = -saturation;
    saturation = saturation * 0x35f >> 7;
    value = (level + 0x1800) >> 6;
    if (value > 0x3ff)
        value = 0x3ff;
    hsv_to_rgb(hue, saturation, value, &red, &green, &blue);
    glow[0x2f0] = (uint8_t)(red << 3);
    glow[0x2f1] = (uint8_t)(green << 3);
    glow[0x2f2] = (uint8_t)(blue << 3);
}

#define g_glow_lights_added (*(uint32_t *)0x4d4b98)

/* 0x47b8f0: every car's glow; in race mode 4, add each car's glow light
 * once. */
void GAMEAPI cars_glow(void)
{
    uint32_t i;

    for (i = 0; i < RACE_CARS(g_race); i++)
        car_glow(SLOT_CAR(i));
    if (g_glow_lights_added == 0 && I32(g_race, 0x57c) == 4) {
        for (i = 0; i < RACE_CARS(g_race); i++) {
            int32_t car = (int32_t)(uintptr_t)SLOT_CAR(i);

            glow_light((const int32_t *)(SLOT_CAR(i) + POSITION), (const uint32_t *)(SLOT_CAR(i) + 0x13f8), 3, 8, 1,
                       (const uint32_t *)&car);
        }
        g_glow_lights_added = 1;
    }
}

/* ---- shrapnel ---- */

/* Pieces (0x80 bytes at 0x6b983c), a free stack (0x6b9840, top 0x6b984c)
 * and the live list (0x6b9844, count 0x6b9848). */
#define g_pieces GP(0x6b983c)
#define g_piece_free ((uint16_t *)GP(0x6b9840))
#define g_piece_live ((uint16_t *)GP(0x6b9844))
#define g_piece_count (*(uint32_t *)0x6b9848)
#define g_piece_free_top (*(int32_t *)0x6b984c)

/* Depth cueing as the PlayStation GTE's DPCS does it: colour (0x6ccc88,
 * 0x6ccc8c, 0x6ccc90, 4.12) moved towards the far colour (0x6ccc34..3c)
 * by 0x6ccc84/4096; code byte from 0x6ccc7f. */
#define g_dq (*(int16_t *)0x6ccc84)
#define g_rgbc (*(uint32_t *)0x6ccc7c)
#define g_ir ((int16_t *)0x6ccc88) /* at 0, 4, 8 bytes */
#define g_far ((int32_t *)0x6ccc34)
#define g_rgb_fifo ((uint8_t *)0x6cccb4)
#define g_mac ((int32_t *)0x6cccc8)

/* 0x462490: free piece `piece`, the live list's entry `index`. */
void GAMEAPI piece_free(uint16_t piece, uint32_t index)
{
    g_piece_free_top++;
    g_piece_free[g_piece_free_top] = piece;
    g_piece_live[index] = g_piece_live[g_piece_count - 1];
    g_piece_count--;
}

/* 0x4629b0: move a piece: position (+0x4c, 16.16) by velocity (+0x58,
 * 18.14) with gravity 0x8000 on y; whole parts to +0x64. */
void GAMEAPI piece_move(uint8_t *piece)
{
    int32_t x = I32(piece, 0x4c), y = I32(piece, 0x50), z = I32(piece, 0x54);
    int32_t vy = I32(piece, 0x5c);

    I32(piece, 0x4c) = (int32_t)((uint32_t)I32(piece, 0x58) + (uint32_t)x);
    I16(piece, 0x64) = (int16_t)((I32(piece, 0x58) >> 14) + (int32_t)((uint32_t)x >> 16));
    I32(piece, 0x50) = (int32_t)((uint32_t)vy + (uint32_t)y);
    I32(piece, 0x5c) = (int32_t)((uint32_t)vy + 0x8000);
    I16(piece, 0x66) = (int16_t)((vy >> 14) + (int32_t)((uint32_t)y >> 16) + 1);
    I32(piece, 0x54) = (int32_t)((uint32_t)I32(piece, 0x60) + (uint32_t)z);
    I16(piece, 0x68) = (int16_t)((I32(piece, 0x60) >> 14) + (int32_t)((uint32_t)z >> 16));
}

static int64_t clamp64(int64_t v, int64_t low, int64_t high)
{
    return v < low ? low : v > high ? high : v;
}

/* 0x427590 */
void GAMEAPI gte_depth_cue(void)
{
    int64_t near_weight = 0x1000 - (int32_t)g_dq, far_weight = g_dq;
    int64_t sum[3];
    uint8_t code = (uint8_t)(g_rgbc >> 24);
    int i;

    for (i = 0; i < 3; i++) {
        int32_t colour = (int32_t)((uint32_t)g_ir[i * 2] << 8), far = (int32_t)((uint32_t)g_far[i] << 8);

        sum[i] = ((int64_t)colour * near_weight >> 12) + ((int64_t)far * far_weight >> 12);
    }
    for (i = 0; i < 3; i++) {
        int64_t mac = sum[i] >> 12;

        g_mac[i] = (int32_t)mac;
        g_ir[i * 2] = (int16_t)clamp64(mac, -0x8000, 0x7fff);
    }
    g_rgb_fifo[3] = g_rgb_fifo[7] = g_rgb_fifo[11] = code;
    for (i = 0; i < 3; i++)
        g_rgb_fifo[i] = g_rgb_fifo[4 + i] = g_rgb_fifo[8 + i] = (uint8_t)(clamp64(sum[i], 0, 0xff000) >> 12);
}

/* 0x462840: every live piece counts down its life (+8): at 3 it leaves
 * the grid, at 0 it is freed; otherwise it moves, and coloured pieces
 * (+0x6c) fade both colours (+0x14, +0x30) over their last 11 steps. */
void GAMEAPI pieces_update(void)
{
    uint32_t count = g_piece_count, i;
    uint16_t *live = g_piece_live;

    g_dq = 500;
    g_rgbc = *(uint32_t *)0x4b11e0;
    g_far[0] = g_far[1] = g_far[2] = 0;
    if (count == 0)
        return;
    i = 0;
    do {
        uint8_t *piece = g_pieces + (uint32_t)*live * 0x80;
        uint16_t life = U16(piece, 8);

        U16(piece, 8) = life - 1;
        if (life <= 3) {
            if (life == 3) {
                grid_unlink_piece(g_pieces + (uint32_t)*live * 0x80);
            } else if (life == 0) {
                piece_free(*live, i);
                live--;
                i--;
                count--;
            }
        } else {
            piece_move(piece);
            if (piece[0x6c] != 0 && life <= 0xb) {
                int c;

                for (c = 0x14; c <= 0x30; c += 0x1c) {
                    g_ir[0] = (int16_t)(piece[c] << 4);
                    g_ir[2] = (int16_t)(U32(piece, c) >> 4 & 0xff0);
                    g_ir[4] = (int16_t)(U32(piece, c) >> 12 & 0xff0);
                    gte_depth_cue();
                    U32(piece, c) = *(uint32_t *)0x6cccbc;
                }
            }
        }
        i++;
        live++;
    } while (i < count);
}

/* ---- helpers of the chase/replay camera director (0x408360) ----
 *
 * Each viewport (0x6cf6a0, 0x150 bytes) has a shot kind (+0), a car it
 * follows (+0x36), a time in the shot (+0x32) and its length (+0x34), and
 * can cut away to an event (+0x3c: 1/3 a pickup or explosion from
 * 0x6b9ce0, 2 one from 0x6b9cc0, 4 the one at 0x5d1fc0; +0x38 which).
 * Trackside camera spots: 0x4b65f4 of them at 0x6cfbe0, 0x38 bytes. */
#define VIEW(i) ((uint8_t *)0x6cf6a0 + (i) * 0x150)
#define g_camera_seed (*(uint32_t *)0x4b11d8)
#define g_spot_count (*(int32_t *)0x4b65f4)
#define SPOTS ((uint8_t *)0x6cfbe0)
#define g_view_car ((uint32_t *)0x6c0a40)
#define g_factorial ((int32_t *)0x4b6388)
#define SLOT_AT(i) (g_car_slots + (int32_t)(i) * CAR_SLOT_SIZE)

#define count_leading_zeros_of_fn GCALL(0x429170, count_leading_zeros_of)
#define vec_length_fn GCALL(0x480cd0, vec_length)
#define vec_dot_fn GCALL(0x40d950, vec_dot)
#define fix_div16_fn GCALL(0x42df70, fix_div16)
#define vec_scale_fn GCALL(0x480090, vec_scale)
#define matrix_look_fn GCALL(0x407220, matrix_look)
#define body_probe GCALL(0x4687f0, respawn_try)    /* group F */
#define camera_fov_set GCALL(0x423f00, view_zoom_set)  /* x87 */
#define camera_sound GCALL(0x40afb0, race_end_fade_set)    /* not ours */
#define grid_unlink_fn GCALL(0x4611d0, object_grid_unlink)

/* 0x40d040: viewports for the split-screen mode (0x552). */
int32_t GAMEAPI view_count(void)
{
    switch (g_race[0x552]) {
    case 1:
    case 2:
        return 2;
    case 3:
        return 3;
    case 4:
        return 4;
    default:
        return 1;
    }
}

/* 0x407e90: scale a direction down until its components' sum fits in 23
 * bits (returning the shift), or nudge it if it is all but zero. */
int32_t GAMEAPI direction_fit(int32_t *v)
{
    int32_t shift = count_leading_zeros_of_fn((uint32_t)abs32(v[0]) + (uint32_t)abs32(v[2]) +
                                              (uint32_t)abs32(v[1]));
    int32_t x, y, z, length;

    if (shift < 9) {
        shift = 9 - shift;
        v[0] >>= shift & 31;
        v[1] >>= shift & 31;
        v[2] >>= shift & 31;
        return shift;
    }
    x = v[0] >> 18;
    y = v[1] >> 18;
    z = v[2] >> 18;
    length = x * x + y * y + z * z;
    if (abs32(length) < 1)
        v[2] -= 0x20000;
    return 0;
}

/* 0x407f30: whether the track cell at `at` has surface `surface` among
 * its list (0x6bfacc). */
int32_t GAMEAPI cell_has_surface(uint32_t surface, const int32_t *at)
{
    int32_t off_track;
    uint8_t *cell = track_cell_fn(at[0], at[1], at[2], &off_track);
    uint16_t *entry = (uint16_t *)(GP(0x6bfacc) + (U32(cell, 4) & 0xffff) * 2);

    for (;;) {
        uint16_t word = entry[1];

        if ((uint32_t)(word >> 1 & 0x7f) == surface)
            return 1;
        if (word & 1)
            return 0;
        entry = (uint16_t *)((uintptr_t)((uint8_t *)entry + (entry[0] >> 11) + 4) & ~(uintptr_t)1);
    }
}

/* 0x4081f0: a body's sideways axis (matrix column 1), or its negation. */
void GAMEAPI body_side_axis(int32_t *out, const uint8_t *body, int32_t positive)
{
    int32_t x = I16(body, 0xc6), y = I16(body, 0xcc), z = I16(body, 0xd2);

    if (positive) {
        out[0] = x;
        out[1] = y;
        out[2] = z;
    } else {
        out[0] = -x;
        out[1] = -y;
        out[2] = -z;
    }
}

/* 0x408240: whether the car is upside down for its gravity (+0x63f: 0
 * normal, 1 flipped). */
int32_t GAMEAPI car_is_upside_down(const uint8_t *car)
{
    int8_t flipped = (int8_t)car[0x63f];
    int16_t up = I16(car, 0xcc);

    if (flipped == 0 && up < 0)
        return 1;
    if (flipped == 1 && up > 0)
        return 1;
    return 0;
}

/* 0x40a6e0: the surface at a point. */
uint32_t GAMEAPI surface_at(const int32_t *at)
{
    int32_t off_track;
    uint8_t *cell = track_cell_fn(at[0], at[1], at[2], &off_track);

    return g_cell_info[2 + (U32(cell, 4) >> 16 & 0x3fff) * 6] & 0x7f;
}

/* 0x40a570: the trackside camera spot of kind `kind` (-1: any but kind 1)
 * on the same surface whose distance from `at` is closest to `range`/4;
 * if none (or with one spot or fewer), a spot above and behind `at`. */
void GAMEAPI camera_spot_nearest(const int32_t *at, int32_t kind, uint8_t **out, int32_t range)
{
    uint32_t best = 0xffffffff;
    int32_t best_index = -1, i;

    range = (range >> 2) * (range >> 2);
    if (g_spot_count > 1) {
        uint32_t surface = surface_at(at);

        for (i = 0; i < g_spot_count; i++) {
            uint8_t *spot = SPOTS + i * 0x38;
            int32_t dx, dy, dz;
            uint32_t distance;

            if (kind == -1 ? I32(spot, 0) == 1 : kind != I32(spot, 0))
                continue;
            dx = (at[0] - I32(spot, 4)) >> 18;
            dy = (at[1] - I32(spot, 8)) >> 18;
            dz = (at[2] - I32(spot, 0xc)) >> 18;
            distance = (uint32_t)abs32((int32_t)((uint32_t)abs32(dz * dz + dy * dy + dx * dx) - (uint32_t)range));
            if (distance < best && cell_has_surface(surface, (int32_t *)(spot + 4))) {
                best = distance;
                best_index = i;
            }
        }
        *out = SPOTS + best_index * 0x38;
        if (best_index != -1)
            return;
    }
    *(int32_t *)0x6cf660 = 0;
    *(int32_t *)0x6cf664 = at[0] - 0x320000;
    *(int32_t *)0x6cf66c = at[2] - 0x320000;
    *(int32_t *)0x6cf668 = at[1] - 0xc80000;
    *out = (uint8_t *)0x6cf660;
}

/* 0x40a8f0: try a camera body at `at` (a copy of `body` moved there)
 * against the world; the probe's result. */
int32_t GAMEAPI camera_probe(const uint8_t *body, const uint8_t *at)
{
    int32_t scratch = (int32_t)(uintptr_t)body; /* the original passes its argument's slot */

    memcpy((void *)0x4b63c0, body, 0x60 * 4);
    memcpy((void *)0x4b63f4, at, 0x36 * 4);
    return body_probe((uint8_t *)GMEM(0x4b63c0), (const int32_t *)at, 0, (int16_t *)GMEM(0x4b6278), 2, (uint32_t *)&scratch);
}

/* 0x40a940: aim the view's matrix from `from` to `to`, keeping the last
 * good one (+0x120) if that fails. */
void GAMEAPI camera_look_or_keep(const void *from, const void *to, uint32_t *matrix, int32_t view)
{
    uint32_t *kept = (uint32_t *)(VIEW(view) + 0x120);
    int i;

    if (matrix_look_fn((int32_t *)from, (const int32_t *)to, (int16_t *)matrix) != 0) {
        for (i = 0; i < 8; i++)
            kept[i] = matrix[i];
    } else {
        for (i = 0; i < 8; i++)
            matrix[i] = kept[i];
    }
}

static uint32_t camera_random(void)
{
    g_camera_seed = g_camera_seed * 16807 & 0x7fffffff;
    return g_camera_seed;
}

/* 0x40a9b0: pick the view's next shot (kind 1..10, 4 the default chase)
 * and which car it follows, re-rolling up to five times when the car is
 * crawling or (for kinds 2 and 3) crowded and upright. */
void GAMEAPI camera_choose_shot(const void *unused, uint32_t view)
{
    uint8_t *v = VIEW(view);
    uint8_t *race;
    int32_t old_kind = I32(v, 0), tries = 4;
    uint32_t kind = 4, car_index = 0, previous = 0;
    int retry;

    (void)unused;
    for (;;) {
        uint8_t *car;

        retry = 0;
        race = g_race;
        if (race[0x58c] != 2) {
            do
                kind = camera_random() >> 16 & 0xf;
            while (kind != 10 && (kind == U32(v, 0) || kind > 10));
        }
        if (I16(v, 0x3a) > 0) {
            car_index = g_view_car[view];
        } else if (race[0x58c] == 2) {
            int32_t best = 6;

            kind = *(uint32_t *)(uintptr_t)(0x4a7840 + (camera_random() >> 16 & 0xf) * 4);
            if (race[0x552] != 0) {
                car_index = view;
            } else if (I32(v, 0x144) != -1) {
                car_index = U32(v, 0x144);
            } else {
                int32_t count = race[0x54d], i;

                car_index = previous;
                for (i = 0; i < count; i++)
                    if (SLOT_CAR(i)[0x70c] == 10 && (int32_t)g_car_slots[i * CAR_SLOT_SIZE + 0x2d] <= best) {
                        best = g_car_slots[i * CAR_SLOT_SIZE + 0x2d];
                        car_index = i;
                    }
            }
        } else {
            do
                car_index = camera_random() >> 16 & 7;
            while (car_index >= race[0x54d]);
        }
        car = GP(SLOT_AT(car_index));
        if (kind != 4 && kind != 5 && vec_length_fn((int32_t *)(car + VELOCITY)) < 0x20000) {
            retry = 1;
            g_camera_seed *= 7;
        }
        if (kind == 2 || kind == 3) {
            int32_t count = g_race[0x54d], near = 0, i;

            for (i = 0; i < count; i++) {
                uint8_t *other;
                int32_t dx, dy, dz;

                if ((uint32_t)i == car_index)
                    continue;
                other = SLOT_CAR(i);
                dx = (I32(car, POSITION) - I32(other, POSITION)) >> 18;
                dy = (I32(car, POSITION + 4) - I32(other, POSITION + 4)) >> 18;
                dz = (I32(car, POSITION + 8) - I32(other, POSITION + 8)) >> 18;
                if ((uint32_t)abs32(dz * dz + dy * dy + dx * dx) < 10000)
                    near++;
            }
            if (count > 0 && near != 0 && car_is_upside_down(car) == 0)
                goto check_retry;
            g_camera_seed *= 7;
        } else {
        check_retry:
            if (retry != 1)
                break;
        }
        previous = car_index;
        if (tries-- < 0) {
            kind = 4;
            break;
        }
    }
    U32(v, 0) = kind;
    U16(v, 0x32) = 0;
    U16(v, 0x34) = 0x5a;
    U16(v, 0x36) = (uint16_t)car_index;
    U32(v, 0x148) = 0;
    U32(v, 0x14c) = 0;
    switch (U32(v, 0)) {
    case 1:
    case 7:
        U16(v, 0x34) = 0xd2;
        break;
    case 6:
        U16(v, 0x34) = 0x96;
        break;
    case 8:
        U16(v, 0x34) = 0x3c;
        break;
    case 9:
        U16(v, 0x34) = 0xb4;
        break;
    case 10:
        U16(v, 0x34) = 300;
        break;
    }
    if (old_kind == 0xe && g_race[0x58c] == 0)
        camera_sound(0);
}

/* 0x40add0: a zoom for the distance between two points: twice the
 * length (at least 0xa00) over `divisor`, at most `limit` * 256 and at
 * least 0x55. */
int32_t GAMEAPI camera_zoom_for(const int32_t *a, const int32_t *b, uint32_t divisor, int32_t limit)
{
    int32_t d[3];
    uint32_t length;
    int32_t zoom;

    d[0] = (a[0] - b[0]) >> 9;
    d[1] = (a[1] - b[1]) >> 9;
    d[2] = (a[2] - b[2]) >> 9;
    length = (uint32_t)vec_length_fn(d) << 1;
    if (length < 0xa00)
        length = 0xa00;
    zoom = (int32_t)(length / divisor);
    if (zoom >= (int32_t)((uint32_t)limit << 8))
        zoom = (int32_t)((uint32_t)limit << 8);
    if (zoom <= 0x55)
        zoom = 0x55;
    return zoom;
}

/* 0x40b000: the slot's camera (+0x28) pulled in by speed (+0x658): the
 * field of view narrows and the camera closes on `at`, eased by its
 * velocity; returns the new position in `at`. */
void GAMEAPI camera_speed_zoom(uint8_t *slot, int32_t *at)
{
    uint8_t *settings = GP(0x4b2198);
    int16_t speed = I16(GP(slot), 0x658);
    int32_t fov, k, ease, dx, dy, dz;
    int16_t pull;
    uint8_t *camera, *body;
    int32_t bx, by, bz;

    if (settings[0x95] != 0)
        fov = (speed >= 0x1c2 ? 0x1c2 : speed) * 6;
    else
        fov = (speed >= 0x1a9 ? 0x1a9 : speed) * 5;
    fov = 200 - (fov >> 4);
    camera_fov_set(fov);
    camera = GP(slot + 0x28);
    body = GP(camera + 4);
    bx = I32(body, 0x34);
    by = I32(body, 0x38);
    bz = I32(body, 0x3c);
    dx = at[0] - bx;
    dy = at[1] - by;
    dz = at[2] - bz;
    if (GP(0x4b2198)[0x95] != 0)
        pull = (int16_t)(200 - ((200 - (int16_t)fov) * 200 >> 8));
    else
        pull = (int16_t)(200 - ((200 - (int16_t)fov) * 0xf0 >> 8));
    k = (int32_t)((uint32_t)(int32_t)pull << 8) / 0x68;
    dx = (dx >> 8) * k;
    dy = (dy >> 8) * k;
    dz = (dz >> 8) * k;
    ease = 0x1e0 - k;
    if (ease > 0x1e0)
        ease = 0x1e0;
    if (ease > 0) {
        camera = GP(slot + 0x28);
        dx += (I32(camera, 8) >> 4) * ease;
        dy += (I32(camera, 0xc) >> 4) * ease;
        dz += (I32(camera, 0x10) >> 4) * ease;
    }
    at[0] = bx + dx;
    at[1] = by + dy;
    at[2] = bz + dz;
    grid_unlink_fn(GP(GP(slot + 0x28)));
    grid_link_fn(GP(GP(slot + 0x28)));
}

/* 0x40b180: how far `point` is from the line a -> b, if its foot lies
 * between them (`*along` gets the fraction, 16.16); -1 if not. */
int32_t GAMEAPI point_line_distance(const int32_t *point, const int32_t *a, const int32_t *b, int32_t *along)
{
    int32_t line[3], to[3];
    int32_t dot, length, t;

    line[0] = (b[0] - a[0]) >> 9;
    line[1] = (b[1] - a[1]) >> 9;
    line[2] = (b[2] - a[2]) >> 9;
    to[0] = (point[0] - a[0]) >> 9;
    to[1] = (point[1] - a[1]) >> 9;
    to[2] = (point[2] - a[2]) >> 9;
    dot = vec_dot_fn(line, to);
    length = vec_dot_fn(line, line);
    if (length < 0x10000)
        return -1;
    t = fix_div16_fn(dot, length);
    if (t <= 0 || t >= 0x20000)
        return -1;
    *along = t;
    line[0] = (int32_t)((uint32_t)line[0] << 9);
    line[1] = (int32_t)((uint32_t)line[1] << 9);
    line[2] = (int32_t)((uint32_t)line[2] << 9);
    vec_scale_fn(t, line, line);
    line[0] = ((int32_t)((uint32_t)line[0] - ((uint32_t)to[0] << 9))) >> 9;
    line[1] = ((int32_t)((uint32_t)line[1] - ((uint32_t)to[1] << 9))) >> 9;
    line[2] = ((int32_t)((uint32_t)line[2] - ((uint32_t)to[2] << 9))) >> 9;
    length = vec_length_fn(line);
    if (length > 0x7f0000)
        return -1;
    return (int32_t)((uint32_t)length << 9);
}

/* 0x40b4f0: the car nearest a point (-1 with no cars). */
void GAMEAPI nearest_car(const int32_t *at, int32_t *index)
{
    int32_t count = g_race[0x54d], best_index = -1, i;
    uint32_t best = 0xffffffff;

    if (count <= 0) {
        *index = -1;
        return;
    }
    for (i = 0; i < count; i++) {
        uint8_t *car = SLOT_CAR(i);
        int32_t dx = (at[0] - I32(car, POSITION)) >> 18;
        int32_t dy = (at[1] - I32(car, POSITION + 4)) >> 18;
        int32_t dz = (at[2] - I32(car, POSITION + 8)) >> 18;
        uint32_t distance = (uint32_t)abs32(dz * dz + dy * dy + dx * dx);

        if (distance < best) {
            best = distance;
            best_index = i;
        }
    }
    *index = best_index;
}

/* 0x40b310: a trackside spot (kind 1) a fast car is passing close to,
 * for a shot from it: the car to `*car_index`, the spot to `*spot`. */
int32_t GAMEAPI camera_find_trackside(uint16_t *car_index, uint8_t **spot, int32_t view)
{
    uint8_t *v = VIEW(view), *race;
    int32_t wanted = -1, i, nearest;

    if (I16(v, 0x32) < 0x3c || g_race_ticks < U32(v, 0x11c) + 0x21c || U32(v, 0) == 0x10 || U32(v, 0) == 0xf ||
        I16(v, 0x3a) > 0)
        return 0;
    race = g_race;
    if (race[0x552] != 0) {
        int32_t views = view_count();

        for (i = 0; i < views; i++)
            if (U32(VIEW(i), 0) == 0x10)
                return 0;
        race = g_race;
    }
    if (race[0x58c] == 2) {
        if (race[0x552] != 0) {
            wanted = view;
        } else if (I32(v, 0x144) != -1) {
            wanted = I32(v, 0x144);
        } else {
            int32_t count = race[0x54d], best = 6;

            for (i = 0; i < count; i++)
                if (SLOT_CAR(i)[0x70c] == 10 && (int32_t)g_car_slots[i * CAR_SLOT_SIZE + 0x2d] <= best) {
                    best = g_car_slots[i * CAR_SLOT_SIZE + 0x2d];
                    wanted = i;
                }
        }
    }
    for (i = 0; i < g_spot_count; i++) {
        uint8_t *s = SPOTS + i * 0x38;
        uint8_t *car;
        int32_t along, distance;

        if (I32(s, 0) != 1)
            continue;
        nearest_car((int32_t *)(s + 4), &nearest);
        if (wanted != -1 && wanted != nearest)
            continue;
        car = GP(SLOT_AT(nearest));
        if (vec_length_fn((int32_t *)(car + VELOCITY)) < 0x80000)
            continue;
        distance = point_line_distance((int32_t *)(car + POSITION), (int32_t *)(s + 4), (int32_t *)(s + 0x10), &along);
        if (distance == -1 || distance > 0x3e80000 || along > 0x4000)
            continue;
        *car_index = (uint16_t)nearest;
        *(uint32_t *)spot = (uint32_t)(uintptr_t)s; /* a game-memory slot (view +0x4b65b0..): 32 bits */
        return 1;
    }
    return 0;
}

/* 0x40c940: factorials 0! .. 12! (0x4b6388). */
void GAMEAPI factorials_init(void)
{
    int32_t f = 1, i;

    g_factorial[0] = 1;
    for (i = 1; i <= 12; i++) {
        f *= i;
        g_factorial[i] = f;
    }
}

static int32_t fix_mul16(int32_t a, int32_t b)
{
    return (int32_t)((int64_t)a * b >> 16);
}

/* 0x40c970: base ** power in 16.16, by the bits of `power` (masks at
 * 0x4a7880, stopping at the first clear one at 0x4a788c). */
int32_t GAMEAPI fix_pow(int32_t base, uint32_t power)
{
    int32_t result = 0x10000;
    int i;

    for (i = 0; i < 5; i++) {
        if ((power & ((uint16_t *)0x4a788c)[i]) == 0)
            return result;
        if (power & ((uint16_t *)0x4a7880)[i])
            result = fix_mul16(result, base);
        base = fix_mul16(base, base);
    }
    return result;
}

/* 0x40c9e0: the point at `t` (16.16) along the Bezier curve through
 * `count` control points (3 ints each). */
void GAMEAPI bezier_point(const int32_t *points, int32_t count, int32_t *out, int32_t t)
{
    int32_t i;

    out[0] = out[1] = out[2] = 0;
    for (i = 0; i < count; i++) {
        int32_t weight = fix_pow(t, i) * (g_factorial[count - 1] / g_factorial[i] / g_factorial[count - 1 - i]);

        if (weight != 0) {
            weight = fix_mul16(weight, fix_pow(0x10000 - t, count - 1 - i));
            if (weight != 0) {
                out[0] += points[i * 3] * weight;
                out[1] += points[i * 3 + 1] * weight;
                out[2] += points[i * 3 + 2] * weight;
            }
        }
    }
}

/* 0x40cad0: scale points (4 shorts each) for the screen mode (0x579):
 * all three by 0x4a7898, mirrored by 0x4a78e0, y by 0x4a78bc too. */
void GAMEAPI points_scale_for_mode(uint8_t *points, int32_t count)
{
    uint32_t mode = g_race[0x579];
    int32_t scale, y_scale;
    int16_t i;

    if (mode >= 0x11)
        mode = 0x10;
    y_scale = ((int16_t *)0x4a78bc)[mode];
    scale = ((int16_t *)0x4a7898)[mode];
    for (i = 0; i < count; i++) {
        int16_t *p = (int16_t *)(points + i * 8);

        p[0] = (int16_t)(p[0] * scale >> 8);
        p[1] = (int16_t)(p[1] * scale >> 8);
        p[2] = (int16_t)(p[2] * scale >> 8);
    }
    if (((int16_t *)0x4a78e0)[mode] != 0)
        for (i = 0; i < count; i++)
            *(int16_t *)(points + i * 8) = (int16_t)-*(int16_t *)(points + i * 8);
    for (i = 0; i < count; i++)
        *(int16_t *)(points + i * 8 + 2) = (int16_t)(*(int16_t *)(points + i * 8 + 2) * y_scale >> 8);
}

/* 0x407fa0: drop the view's event once it has finished, and (not in mode
 * 2, a while into a shot and 10 seconds since the last, with no view
 * already on one) cut to a new event: a flagged object in 0x6b9ce0, then
 * 0x6b9cc0, then 0x5d1fc0. */
void GAMEAPI camera_check_events(int32_t view)
{
    uint8_t *v = VIEW(view), *race;
    int32_t views, i;
    uint32_t last;

    if ((I32(v, 0x3c) == 1 || I32(v, 0x3c) == 3) && I32(GP(0x6b9ce0 + 4 * I16(v, 0x38)), 0x188) == 0)
        I32(v, 0x3c) = 0;
    if (I32(v, 0x3c) == 2 && I32(GP(0x6b9cc0 + 4 * I16(v, 0x38)), 0x194) == 0)
        I32(v, 0x3c) = 0;
    if (I32(v, 0x3c) == 4 && I32(GP(0x5d1fc0 + 4 * I16(v, 0x38)), 0x188) == 0)
        I32(v, 0x3c) = 0;
    views = view_count();
    race = g_race;
    if (race[0x552] != 0) {
        last = 3000;
        for (i = 0; i < views; i++) {
            if (U32(VIEW(i), 0x118) <= last)
                last = U32(VIEW(i), 0x118);
            if (I32(VIEW(i), 0x3c) != 0)
                return;
        }
    } else {
        if (I32(v, 0x3c) != 0)
            return;
        last = U32(v, 0x118);
    }
    if (race[0x58c] == 2 || I16(v, 0x32) < 0x3c || g_race_ticks < last + 600 || I16(v, 0x3a) > 0 ||
        U32(v, 0) == 0x10 || U32(v, 0) == 0xf)
        return;
    for (i = 0; i < 24; i++) {
        uint8_t *object = GP(0x6b9ce0 + 4 * i);

        if (object[0x188] & 1) {
            int32_t kind = I32(object, 0x184);

            if (kind == 1)
                I32(v, 0x3c) = 1;
            else if (kind == 0)
                I32(v, 0x3c) = 3;
            else
                continue;
            I16(v, 0x38) = (int16_t)i;
            U32(v, 0) = 0xb;
            U32(v, 0x20) = 0;
            break;
        }
    }
    if (I32(v, 0x3c) != 0)
        return;
    for (i = 0; i < 6; i++)
        if (GP(0x6b9cc0 + 4 * i)[0x194] & 1) {
            I32(v, 0x3c) = 2;
            I16(v, 0x38) = (int16_t)i;
            U32(v, 0) = 0xc;
            break;
        }
    if (I32(v, 0x3c) != 0)
        return;
    if (GP(0x5d1fc0)[0x188] & 1) {
        I32(v, 0x3c) = 4;
        I16(v, 0x38) = 0;
        U32(v, 0) = 0xd;
    }
}
