/* World updates after the physics step (sim_step, group J): the weather
 * (rain or snow round the camera, thunder and lightning; 0x484080), timed
 * explosions (0x405140), the sprite effects' movement (0x42b990), the
 * cars' shadows (0x46c300), debris (0x47c210), the tyre trails
 * (0x481f20) and the cars' animated texture frames (0x4751b0).
 *
 * Left original: the weather's screen-space flakes (0x4857f0, 0x485140,
 * x87 floats), sound (0x471c90, 0x471fc0) and other groups' functions,
 * called by address. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_options (*(uint8_t **)0x4b2198)
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define SLOT_CAR(i) (*(uint8_t **)(g_car_slots + (i) * CAR_SLOT_SIZE))
#define CAR_COUNT (g_race[0x54d])
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))

typedef void(GAMEAPI *void_fn)(void);
typedef void(GAMEAPI *ptr_fn)(uint8_t *p);
typedef void(GAMEAPI *sound_fn)(int32_t sound, int32_t channel, int32_t pitch, int32_t volume, int32_t pan);
typedef void(GAMEAPI *volume_fn)(int32_t channel, int32_t left, int32_t right);
typedef void(GAMEAPI *flakes_fn)(int32_t outdoors);
typedef void(GAMEAPI *effect_fn)(int32_t *position, int32_t *velocity, int32_t kind, int32_t colour, int32_t size);
typedef void(GAMEAPI *fade_fn)(uint8_t *sprite, int32_t life, int32_t white);
#define sound_play ((sound_fn)0x471c90)
#define sound_set_volume ((volume_fn)0x471fc0)
#define weather_flakes ((flakes_fn)0x4857f0) /* x87; left original */
#define effect_spawn ((effect_fn)0x42aa30)   /* group F */
#define scenery_break ((ptr_fn)0x4052b0)     /* group F */
#define sprite_fade ((fade_fn)0x42cd40)      /* group F */
#define grid_remove ((ptr_fn)0x4612a0)       /* group F */

/* ---- Weather ----
 *
 * The race's weather (race +0x553: 0 none, 1 rain, 2 snow) has up to six
 * emitters (0x5d6360, ended by -1). Each holds a box (int16 min +0, max
 * +8) round a body (+0x10) that is moved to each camera in turn, and 400
 * drops (+0x14, 8 bytes: int16 x, y, z, alive) carried along by +0xc98
 * (x, y) and +0xc9c (z) each step; a drop leaving the box dies, and a box
 * that moves fills the new space with fresh drops (+0xc94 the next one to
 * reuse, +0xca2 a density shift). Indoors (the body's track section has
 * flag 8) the drops are cleared and the emitter's sound (+0xca1) muted. */

#define g_weather_seed (*(uint32_t *)0x4b2c50)
#define g_weather_level (*(int32_t *)0x4b2c60)
#define g_weather_level_min (*(int32_t *)0x4b2c58)
#define g_weather_level_max (*(int32_t *)0x4b2c54)
#define g_weather_changed (*(int32_t *)0x4b2c64)
#define g_weather_level_last (*(int32_t *)0x4d58e8)
#define g_weather_level_timer (*(int32_t *)0x4d58ec)
#define g_weather_trend (*(int32_t *)0x5d6338)
#define g_weather_emitters ((uint8_t **)0x5d6360)
#define g_thunder_wait (*(int32_t *)0x4d5900)
#define g_lightning_wait (*(int32_t *)0x4d5904)
#define g_lightning (*(int32_t *)0x4d5908)
#define g_flash (*(int32_t *)0x4d58f4)
#define g_flash_last (*(int32_t *)0x4d58f8)
#define g_cameras ((uint8_t *)0x6cf6e0) /* 0x150 each: position, velocity +0x34, matrix +0x90 */
#define g_camera_count (*(int32_t *)0x5d6248)
#define g_view_flags (*(uint8_t *)0x6cce40)
#define g_player_slot (*(int32_t *)0x6c0a40)
#define g_chase_view ((uint8_t *)0x5d6260)
#define g_surfaces (*(uint8_t **)0x6bfb40)
#define g_section_info (*(uint8_t ***)*(uint8_t **)0x6c1e30)

static uint32_t weather_random(void)
{
    g_weather_seed = g_weather_seed * 16807 & 0x7fffffff;
    return g_weather_seed;
}

/* 0x486290: the rain level drifts one step every 30 steps towards its
 * limit, in the direction of 0x5d6338 (0: lighter, 1: heavier). */
void GAMEAPI weather_level_step(void)
{
    g_weather_level_last = g_weather_level;
    if (g_weather_trend == 0) {
        if (g_weather_level > g_weather_level_min && ++g_weather_level_timer > 0x1e) {
            g_weather_level_timer = 0;
            g_weather_level--;
            g_weather_changed = 1;
        }
    } else if (g_weather_trend == 1 && g_weather_level < g_weather_level_max && --g_weather_level_timer < 0) {
        g_weather_level++;
        g_weather_level_timer = 0x1e;
        g_weather_changed = 1;
    }
}

/* 0x40d090: a view from just above the car (0xd8 bytes as a body from
 * +0x34: position, velocity +0x34, matrix +0x90) looking along its
 * forward axis. */
void GAMEAPI car_view(uint8_t **slot, int32_t *out)
{
    uint8_t *car = *slot;
    int32_t dir[3], up[3], i;

    for (i = 0; i < 0x36; i++)
        out[i] = I32(car, 0x34 + i * 4);
    out[1] -= 0x70000;
    car = *slot;
    dir[0] = -S16(car, 0xc8);
    dir[1] = -S16(car, 0xce);
    dir[2] = -S16(car, 0xd4);
    if (car[0x63f] == 1) {
        up[0] = S16(car, 0xc6);
        up[1] = S16(car, 0xcc);
        up[2] = S16(car, 0xd2);
    } else {
        up[0] = -S16(car, 0xc6);
        up[1] = -S16(car, 0xcc);
        up[2] = -S16(car, 0xd2);
    }
    matrix_look(dir, up, (int16_t *)(out + 0x24));
    out[0x2b] = 0;
    out[0x2a] = 0;
    out[0x29] = 0;
    car = *slot;
    out[0xd] = I32(car, 0x68);
    out[0xe] = I32(car, 0x6c);
    out[0xf] = I32(car, 0x70);
}

#define g_saved_position ((int32_t *)0x4d58c8)
#define g_saved_velocity ((int32_t *)0x4d58d8)
#define g_saved_matrix ((uint32_t *)0x4d58a0)
#define g_saved_section (*(uint32_t *)0x4d58c0)

/* 0x484120: put the emitter's body at camera `index` (restore = 0,
 * remembering where it was) or back (restore = 1). The second last
 * camera is the chase view from the player's car, the last the first
 * camera; only the last two unless split screen (0x6cce40 bit 2). */
void GAMEAPI weather_emitter_place(uint8_t *emitter, int32_t restore, int32_t index)
{
    uint8_t *body = *(uint8_t **)(emitter + 0x10), *view;
    int32_t i, off_track;

    if (!(g_view_flags & 4) && index < g_camera_count - 2)
        return;
    if (restore) {
        for (i = 0; i < 3; i++) {
            I32(body, 0x34 + i * 4) = g_saved_position[i];
            I32(body, 0x68 + i * 4) = g_saved_velocity[i];
        }
        for (i = 0; i < 8; i++)
            U32(body, 0xc4 + i * 4) = g_saved_matrix[i];
        U32(body, 0x11c) = g_saved_section;
        return;
    }
    for (i = 0; i < 3; i++) {
        g_saved_position[i] = I32(body, 0x34 + i * 4);
        g_saved_velocity[i] = I32(body, 0x68 + i * 4);
    }
    for (i = 0; i < 8; i++)
        g_saved_matrix[i] = U32(body, 0xc4 + i * 4);
    g_saved_section = U32(body, 0x11c);
    view = g_cameras + index * 0x150;
    if (index == g_camera_count - 1)
        view = g_cameras;
    if (index == g_camera_count - 2) {
        car_view((uint8_t **)(g_car_slots + g_player_slot * CAR_SLOT_SIZE), (int32_t *)g_chase_view);
        view = g_chase_view;
    }
    for (i = 0; i < 3; i++) {
        I32(body, 0x34 + i * 4) = I32(view, i * 4);
        I32(body, 0x68 + i * 4) = I32(view, 0x34 + i * 4);
    }
    for (i = 0; i < 8; i++)
        U32(body, 0xc4 + i * 4) = U32(view, 0x90 + i * 4);
    view = track_cell(I32(view, 0), I32(view, 4), I32(view, 8), &off_track);
    body[0x11c] = g_surfaces[2 + ((U32(view, 4) >> 16) & 0x3fff) * 6] & 0x7f;
}

static int32_t section_indoors(const uint8_t *body)
{
    return I32(body, 0x11c) >= 0 && (*(g_section_info[body[0x11c]] + 4) & 8);
}

/* 0x4850f0: 1 if there is weather and the body is indoors. */
int32_t GAMEAPI weather_indoors(const uint8_t *body)
{
    return g_race[0x553] && body && section_indoors(body);
}

/* 0x4848c0: the box round the emitter's body: 0x400 either side, 0x300
 * up and down, pushed ahead along its matrix's third row. */
void GAMEAPI weather_box(int16_t *min, int16_t *max, const uint8_t *emitter)
{
    const uint8_t *body = *(uint8_t *const *)(emitter + 0x10);
    int16_t dx, dy, dz;

    min[0] = (int16_t)((I32(body, 0x34) >> 16) - 0x400);
    min[1] = (int16_t)((I32(body, 0x38) >> 16) - 0x300);
    min[2] = (int16_t)((I32(body, 0x3c) >> 16) - 0x400);
    max[0] = (int16_t)((I32(body, 0x34) >> 16) + 0x400);
    max[1] = (int16_t)((I32(body, 0x38) >> 16) + 0x300);
    max[2] = (int16_t)((I32(body, 0x3c) >> 16) + 0x400);
    dx = (int16_t)((S16(body, 0xd0) << 10) >> 12);
    dy = (int16_t)(((S16(body, 0xd2) * 0x300) >> 12) - 0x240);
    dz = (int16_t)((S16(body, 0xd4) << 10) >> 12);
    max[0] += dx;
    max[1] += dy;
    max[2] += dz;
    min[2] += dz;
    min[0] += dx;
    min[1] += dy;
}

/* 0x4846c0: whether the emitter is indoors (no drops), muting its sound. */
void GAMEAPI weather_emitter_indoors(int32_t indoors, uint8_t *emitter)
{
    int8_t channel = (int8_t)emitter[0xca1];

    emitter[0xca0] = (uint8_t)indoors;
    if (channel != 0x7f) {
        int32_t volume = indoors ? 0 : 0x1000;

        sound_set_volume(channel, volume, volume);
    }
}

/* 0x4846f0: fresh drops in the box from min to max (each side under
 * 0x1000): one per 2^19 of its volume / 64, and one more by chance. */
void GAMEAPI weather_drops_spawn(const int16_t *min, const int16_t *max, uint8_t *emitter)
{
    int32_t x0 = min[0], y0 = min[1], z0 = min[2];
    int32_t dx = max[0] - x0, dy = max[1] - y0, dz = max[2] - z0;
    int32_t volume, count, rest;
    uint8_t **next = (uint8_t **)(emitter + 0xc94), *drop;

    if ((dx & 0xfffff000) || (dy & 0xfffff000) || (dz & 0xfffff000))
        return;
    volume = (dz >> 2) * (dy >> 2) * (dx >> 2);
    count = volume >> 19;
    rest = (volume - (int32_t)((uint32_t)count << 19)) >> (emitter[0xca2] & 31);
    if ((int32_t)(weather_random() >> 15) < rest)
        count++;
    drop = *next;
    for (; count > 0; count--) {
        uint32_t a = weather_random(), b = weather_random(), c = weather_random();

        drop += 8;
        S16(drop, -8) = (int16_t)(((a >> 15) * (uint32_t)dx >> 16) + x0);
        S16(drop, -6) = (int16_t)(((b >> 15) * (uint32_t)dy >> 16) + y0);
        S16(drop, -4) = (int16_t)(((c >> 15) * (uint32_t)dz >> 16) + z0);
        S16(drop, -2) = 1;
        if (drop >= (uint8_t *)next)
            drop = emitter + 0x14;
    }
    *next = drop;
}

static int16_t *box3(int16_t *b, int16_t x, int16_t y, int16_t z)
{
    b[0] = x;
    b[1] = y;
    b[2] = z;
    return b;
}

/* 0x4842e0: move the emitter's drops, killing those outside its box,
 * then move the box with the body and fill the new space: all of it
 * after a jump of 0x400 or more in x or y, else the slabs uncovered in
 * x, y and z (as the original works them out) and a slab dy below the
 * box's top. */
void GAMEAPI weather_emitter_update(uint8_t *emitter)
{
    int16_t *box = (int16_t *)emitter;
    int32_t min_x = box[0], min_y = box[1], min_z = box[2];
    int32_t max_x = box[4], max_y = box[5], max_z = box[6];
    uint8_t *body = *(uint8_t **)(emitter + 0x10), *drop, *end = emitter + 0xc94;
    int32_t indoors = 0;
    int16_t lo[3], hi[3];

    if (emitter[0xca0]) {
        for (drop = emitter + 0x14; drop < end; drop += 8)
            S16(drop, 6) = 0;
    } else {
        uint32_t drift = U32(emitter, 0xc98);
        int32_t drift_z = S16(emitter, 0xc9c);

        for (drop = emitter + 0x14; drop < end; drop += 8) {
            int32_t x, y, z;

            if (!S16(drop, 6))
                continue;
            y = I32(drop, 0) >> 16;
            x = S16(drop, 0);
            z = S16(drop, 4);
            if (y > max_y || y < min_y || x > max_x || x < min_x || z > max_z || z < min_z) {
                S16(drop, 6) = 0;
            } else {
                U32(drop, 0) += drift;
                S16(drop, 4) = (int16_t)(z + drift_z);
            }
        }
    }
    if (section_indoors(body)) {
        weather_box(box, box + 4, emitter);
        indoors = 1;
    }
    weather_emitter_indoors(indoors, emitter);
    if (indoors)
        return;

    weather_box(lo, hi, emitter);
    {
        int32_t new_max_x = hi[0];
        int32_t far = 1;
        int32_t d;

        d = new_max_x - box[4];
        if (abs32(d) < 0x400)
            far = 0;
        d = hi[1] - box[5];
        if (abs32(d) >= 0x400 || far) {
            weather_drops_spawn(lo, hi, emitter);
        } else {
            /* three slabs, each the new box less the old one along x, y
             * and z in turn */
            int16_t a_lo[3], a_hi[3], b_lo[3], b_hi[3], c_lo[3], c_hi[3], d_lo[3], d_hi[3];
            int32_t z_grew, x_grew;

            box3(a_lo, lo[0], lo[1], lo[2]);
            box3(b_lo, lo[0], lo[1], lo[2]);
            box3(c_lo, lo[0], lo[1], lo[2]);
            box3(a_hi, hi[0], hi[1], hi[2]);
            box3(b_hi, hi[0], hi[1], hi[2]);
            box3(c_hi, hi[0], hi[1], hi[2]);
            if (hi[2] > box[6]) {
                c_lo[2] = box[6];
                z_grew = 1;
            } else {
                c_hi[2] = box[2];
                z_grew = 0;
            }
            if (hi[0] > box[4]) {
                a_lo[0] = box[4];
                x_grew = 1;
            } else {
                a_hi[0] = box[0];
                x_grew = 0;
            }
            if (hi[1] > box[5])
                b_lo[1] = box[5];
            else
                b_hi[1] = box[1];
            if (z_grew) {
                if (hi[2] > c_lo[2]) {
                    a_hi[2] = c_lo[2];
                    b_hi[2] = c_lo[2];
                }
            } else if (lo[2] < c_hi[2]) {
                a_lo[2] = c_hi[2];
                b_lo[2] = c_hi[2];
            }
            if (x_grew) {
                if (new_max_x > a_lo[0])
                    b_hi[0] = a_lo[0];
            } else if (lo[0] < new_max_x) {
                b_lo[0] = (int16_t)new_max_x;
            }
            weather_drops_spawn(a_lo, a_hi, emitter);
            weather_drops_spawn(b_lo, b_hi, emitter);
            weather_drops_spawn(c_lo, c_hi, emitter);
            box3(d_lo, lo[0], lo[1], lo[2]);
            box3(d_hi, hi[0], (int16_t)(S16(emitter, 0xc9a) + lo[1]), hi[2]);
            weather_drops_spawn(d_lo, d_hi, emitter);
        }
    }
    box[0] = lo[0];
    box[1] = lo[1];
    box[2] = lo[2];
    box[4] = hi[0];
    box[5] = hi[1];
    box[6] = hi[2];
}

/* 0x484fd0: a thunder clap and the screen flash. */
void GAMEAPI weather_thunder_clap(void)
{
    uint32_t r = weather_random() >> 15 & 0xffff;

    sound_play(0x24, 7, (int32_t)(r & 0xff) * 2 + 0x400, (int32_t)(r >> 8) * 16 + 7000, 0);
    *(uint16_t *)0x6ccb60 = 1;
    *(uint16_t *)0x6ccb62 = 1;
    *(uint32_t *)0x6ccb64 = 0xc;
}

/* 0x484e20: now and then (1 in 64 steps, after any wait), distant thunder. */
void GAMEAPI weather_thunder(void)
{
    uint32_t r;

    if (g_thunder_wait) {
        g_thunder_wait--;
        return;
    }
    if ((weather_random() >> 15 & 0xffff) >= 0x400)
        return;
    r = weather_random() >> 15 & 0xffff;
    sound_play(r < 0x200 ? 0x25 : 0x26, 7, (int32_t)(r & 0xff) + 0x400, (int32_t)(r >> 8) * 16 + 0x2000, 0);
}

/* 0x484ef0: lightning: outdoors, 1 in 256 steps a clap starts a flash
 * of 8 to 20 steps that blinks every other two. */
void GAMEAPI weather_lightning(int32_t outdoors)
{
    if (g_lightning_wait) {
        g_lightning_wait--;
    } else if (outdoors) {
        uint32_t r = weather_random() >> 15 & 0xffff;

        if (r < 0x80) {
            weather_thunder_clap();
            switch (r & 3) {
            case 0:
            case 1:
                g_lightning = 0x14;
                break;
            case 2:
                g_lightning = 0x10;
                break;
            case 3:
                g_lightning = 8;
                break;
            }
        }
    }
    g_flash_last = g_flash;
    if (g_lightning) {
        int32_t flash = g_lightning & 2, last = g_flash;

        g_lightning--;
        g_flash = flash;
        if (last != flash)
            g_weather_changed = 1;
    }
}

/* 0x484080: the weather, once a step. */
void GAMEAPI weather_update(void)
{
    uint8_t *emitter = g_weather_emitters[0];
    int32_t outdoors = 1, i;

    weather_level_step();
    if (g_race[0x553] && emitter) {
        for (i = 0; i < 6; i++) {
            if (emitter == (uint8_t *)-1)
                break;
            weather_emitter_place(emitter, 0, i);
            weather_emitter_update(emitter);
            if (weather_indoors(*(uint8_t **)(emitter + 0x10)) == 1)
                outdoors = 0;
            weather_emitter_place(emitter, 1, i);
            emitter = g_weather_emitters[i + 1];
        }
        if (g_race[0x553] == 1) {
            weather_thunder();
            weather_lightning(outdoors);
        }
    }
    weather_flakes(outdoors);
}

/* ---- Timed explosions ----
 *
 * 0x6d06e0 (count 0x6d06c0), 0x10 bytes each: int16 position, +6 done,
 * +7 countdown, +8 size (0: small), +9 keep the scenery, +0xc scenery. */

/* 0x405140: count each down; the first to reach zero explodes (only one
 * a step). */
void GAMEAPI explosions_update(void)
{
    uint32_t count = *(uint32_t *)0x6d06c0, i;
    uint8_t *e = (uint8_t *)0x6d06e0;

    for (i = 0; i < count; i++, e += 0x10) {
        int32_t position[3], velocity[3];

        if (e[6])
            continue;
        if (e[7]) {
            e[7]--;
            continue;
        }
        position[0] = (int32_t)((uint32_t)S16(e, 0) << 16);
        position[1] = (int32_t)((uint32_t)S16(e, 2) << 16);
        position[2] = (int32_t)((uint32_t)S16(e, 4) << 16);
        velocity[0] = 0;
        velocity[2] = 0;
        if ((int8_t)e[8] > 0) {
            velocity[1] = (int32_t)0xffc00000;
            effect_spawn(position, velocity, (int8_t)e[8] - 1, 0x50b2c, 0xb);
        } else {
            velocity[1] = (int32_t)0xffe00000;
            effect_spawn(position, velocity, 3, 0x24, 0xa);
        }
        if (!e[9])
            scenery_break(*(uint8_t **)(e + 0xc));
        e[6] = 0xff;
        return;
    }
}

/* ---- Sprite effects ----
 *
 * 0x100 bytes each from *0x6ccb5c to *0x6ccb84: +0 kind, +0x88 position
 * (24.8), +0x94 in use, +0x95 size, +0x96 int16 life, +0x98 colour
 * (alpha +0x9b), +0x9c int16 velocity, +0xa2 behaviour flags, +0xa8 and
 * +0xac the in-use list (0x4be12c, 0x4be130) and free list (0x4be128). */

#define SPRITE_VEL(s, i) S16(s, 0x9c + (i) * 2)
#define SPRITE_POS(s, i) I32(s, 0x88 + (i) * 4)

static void sprite_move(uint8_t *s)
{
    int i;

    for (i = 0; i < 3; i++)
        SPRITE_POS(s, i) += SPRITE_VEL(s, i) * 0x100;
}

static uint8_t fade_alpha(uint8_t alpha, int32_t by)
{
    int32_t a = alpha - by;

    return a < 1 ? 0 : (uint8_t)a;
}

/* 0x42c400: a 20-step counter, running while non-zero. */
void GAMEAPI flash_timer_step(void)
{
    int32_t *t = (int32_t *)0x4be1a8;

    if (*t) {
        int32_t n = *t + 1;

        *t = n > 0x14 ? 0 : n;
    }
}

/* 0x42cd80: kind 8: slows to 7/8, rising, fading and growing. */
void GAMEAPI sprite_move_smoke(uint8_t *s, int32_t life)
{
    sprite_move(s);
    SPRITE_VEL(s, 0) = (int16_t)(SPRITE_VEL(s, 0) * 7 / 8);
    SPRITE_VEL(s, 1) = (int16_t)((int16_t)(SPRITE_VEL(s, 1) - 0x80) * 7 / 8);
    SPRITE_VEL(s, 2) = (int16_t)(SPRITE_VEL(s, 2) * 7 / 8);
    sprite_fade(s, life, 0);
    s[0x95] += 4;
}

/* 0x42ce70: slows by an eighth; fades over its last 16 (or 8) steps. */
void GAMEAPI sprite_move_drag(uint8_t *s, uint32_t life, int32_t slow_fade)
{
    uint32_t fade_from = slow_fade ? 8 : 0x10;
    int32_t by = slow_fade ? 0x10 : 8, i;

    sprite_move(s);
    for (i = 0; i < 3; i++)
        SPRITE_VEL(s, i) = (int16_t)(SPRITE_VEL(s, i) - (SPRITE_VEL(s, i) >> 3));
    if (life < fade_from)
        s[0x9b] = fade_alpha(s[0x9b], by);
}

/* 0x42ce50: kind 0x1b: as 0x42ce70, rising. */
void GAMEAPI sprite_move_rising(uint8_t *s, uint32_t life)
{
    sprite_move_drag(s, life, 0);
    SPRITE_VEL(s, 1) -= 0x80;
}

/* 0x42cf40: kind 0x22: grows (faster for sprites at addresses with bit
 * 10 set), drifts and falls; fades over its last 64 steps. */
void GAMEAPI sprite_move_spray(uint8_t *s, uint32_t life)
{
    uint8_t size = s[0x95];

    sprite_move(s);
    if (size < 0xfd) {
        s[0x95] = (uint8_t)(size + 1);
        if ((uintptr_t)s & 0x400)
            s[0x95] = (uint8_t)(size + 2);
    }
    SPRITE_VEL(s, 2) += 0xd;
    SPRITE_VEL(s, 1) -= 0x23;
    SPRITE_VEL(s, 0) += 0xd;
    if (life < 0x40)
        s[0x9b] = fade_alpha(s[0x9b], 4);
}

#define g_effect_seed (*(uint32_t *)0x4b11d8)
#define g_sin_steps ((const int16_t *)0x4b24f8)
#define g_cos_steps ((const int16_t *)0x4b2578)

/* 0x42d000: kind 0x17: turns its x/z velocity a random small angle and
 * rises at random; fades under 60 steps of life. */
void GAMEAPI sprite_move_swirl(uint8_t *s)
{
    int32_t i, vx, vz, c, sn;
    int16_t nx, nz;

    g_effect_seed = g_effect_seed * 16807 & 0x7fffffff;
    i = (int32_t)(g_effect_seed >> 15 >> 13) + 1;
    sn = g_sin_steps[i];
    c = g_cos_steps[i];
    vx = SPRITE_VEL(s, 0);
    vz = SPRITE_VEL(s, 2);
    nx = (int16_t)(((uint32_t)(vx * c) >> 14) + ((uint32_t)(vz * sn) >> 14));
    nz = (int16_t)(((uint32_t)(vz * c) >> 14) - ((uint32_t)(vx * sn) >> 14));
    SPRITE_VEL(s, 2) = nz;
    SPRITE_VEL(s, 0) = nx;
    SPRITE_POS(s, 0) += nx * 0x100;
    SPRITE_POS(s, 1) += SPRITE_VEL(s, 1) * 0x100;
    SPRITE_POS(s, 2) += nz * 0x100;
    g_effect_seed = g_effect_seed * 16807 & 0x7fffffff;
    SPRITE_VEL(s, 1) += (int16_t)(0x18 - (g_effect_seed >> 15 >> 10));
    if (S16(s, 0x96) < 0x3c)
        s[0x9b] -= 4;
}

/* 0x42d130: kind 0x1e: falls; darkens under 60 steps of life. */
void GAMEAPI sprite_move_falling(uint8_t *s)
{
    sprite_move(s);
    SPRITE_VEL(s, 1) += 0xb4;
    if (S16(s, 0x96) < 0x3c) {
        uint32_t colour = U32(s, 0x98);
        int32_t r = (int32_t)(colour & 0xff) - 4, g = (int32_t)(colour >> 8 & 0xff) - 4;
        int32_t b = (int32_t)(colour >> 16 & 0xff) - 4;

        if (r < 0)
            r = 0;
        if (g < 0)
            g = 0;
        if (b < 0)
            b = 0;
        U32(s, 0x98) = ((uint32_t)b << 8 | (uint32_t)g) << 8 | (uint32_t)r;
    }
}

/* 0x42d400: other kinds, by behaviour: 0x10 still, 1 rises, 0x4000
 * flies out with its life and drops, else slows to 3/4 (4 and 8 also
 * rise). */
void GAMEAPI sprite_move_basic(uint8_t *s, int32_t behaviour)
{
    int32_t i;

    if (behaviour == 0x10)
        return;
    if (behaviour == 1) {
        SPRITE_VEL(s, 1) -= 0x38;
        return;
    }
    if (behaviour == 0x4000) {
        int32_t life = S16(s, 0x96), left = 0x1e - life, y;

        SPRITE_POS(s, 0) += SPRITE_VEL(s, 0) * 4 * life;
        y = SPRITE_POS(s, 1) + SPRITE_VEL(s, 1) * 4 * life;
        SPRITE_POS(s, 2) += SPRITE_VEL(s, 2) * 4 * life;
        SPRITE_POS(s, 1) = y;
        SPRITE_POS(s, 1) = (int32_t)((uint32_t)(left < 1 ? 0 : left) << 14) + y;
        return;
    }
    for (i = 0; i < 3; i++) {
        SPRITE_POS(s, i) += SPRITE_VEL(s, i) * 0x100;
        SPRITE_VEL(s, i) = (int16_t)(SPRITE_VEL(s, i) * 3 / 4);
    }
    if (behaviour == 4)
        SPRITE_VEL(s, 1) -= 0x60;
    else if (behaviour == 8)
        SPRITE_VEL(s, 1) -= 0x80;
}

/* 0x42d300: the colour for a behaviour at a life: white with an alpha
 * from the fade table (0x4b25f6, read backwards) or a ramp. */
uint32_t GAMEAPI sprite_colour(uint32_t behaviour, uint32_t life)
{
    const int16_t *fade = (const int16_t *)0x4b25f6;

    if (behaviour < 9) {
        if (behaviour == 8) {
            life *= 2;
            if (life > 0x3f)
                life = 0x3f;
            return (uint32_t)(*(fade - life) * 0xff >> 14) << 24 | 0xffffff;
        }
        if (behaviour == 1) {
            if (life >= 0x20)
                return 0xffffffff;
            return (uint32_t)(*(fade - life) * 0xff >> 14) << 24 | 0xffffff;
        }
        if (behaviour != 4)
            return 0;
        life <<= 2;
        if (life > 0x80)
            life = 0x80;
    } else {
        if (behaviour == 0x4000) {
            life *= 6;
            if (life > 0x3f)
                life = 0x3f;
            return (uint32_t)(*(fade - life) * 0xff >> 14) << 24 | 0xc0ff;
        }
        if (behaviour != 0x10)
            return 0;
        life <<= 4;
        if (life > 200)
            life = 200;
    }
    return life << 24 | 0xffffff;
}

/* 0x42d560: behaviours 1 and 4 grow while young. */
void GAMEAPI sprite_grow(uint8_t *s, int32_t behaviour, uint32_t life)
{
    if (behaviour == 1) {
        if (life > 0x20 && s[0x95] < 0x80)
            s[0x95] += 0x20;
    } else if (behaviour == 4 && life > 0x10 && s[0x95] < 0x80) {
        s[0x95] += 4;
    }
}

/* 0x42ad00: take a sprite off the in-use list onto the free list. */
void GAMEAPI sprite_free(uint8_t *s)
{
    uint8_t *prev = *(uint8_t **)(s + 0xac), *next = *(uint8_t **)(s + 0xa8);

    if (prev)
        *(uint8_t **)(prev + 0xa8) = next;
    else
        *(uint8_t **)0x4be130 = next;
    if (next)
        *(uint8_t **)(next + 0xac) = prev;
    else
        *(uint8_t **)0x4be12c = prev;
    *(uint8_t **)(s + 0xa8) = 0;
    *(uint8_t **)(s + 0xac) = *(uint8_t **)0x4be128;
    *(uint8_t **)0x4be128 = s;
    s[0x94] = 0;
    grid_remove(s);
}

/* 0x42d1e0: move each sprite in use by its kind, freeing it at the end
 * of its life. */
void GAMEAPI sprites_move(void)
{
    uint8_t *end = *(uint8_t **)0x6ccb84, *s;

    for (s = *(uint8_t **)0x6ccb5c; s < end; s += 0x100) {
        uint32_t behaviour;
        int32_t life;

        if (!s[0x94])
            continue;
        behaviour = U16(s, 0xa2);
        life = S16(s, 0x96);
        switch (U32(s, 0)) {
        case 8:
            sprite_move_smoke(s, life);
            break;
        case 0x17:
            sprite_move_swirl(s);
            break;
        case 0x1b:
            sprite_move_rising(s, (uint32_t)life);
            break;
        case 0x1c:
            sprite_move_drag(s, (uint32_t)life, 1);
            break;
        case 0x1e:
            sprite_move_falling(s);
            break;
        case 0x22:
            sprite_move_spray(s, (uint32_t)life);
            break;
        default:
            sprite_move_basic(s, (int32_t)behaviour);
            U32(s, 0x98) = sprite_colour(behaviour, (uint32_t)life);
            sprite_grow(s, (int32_t)behaviour, (uint32_t)life);
            break;
        }
        if (life == 1)
            sprite_free(s);
        else
            S16(s, 0x96) = (int16_t)(life - 1);
    }
}

/* 0x42b990 */
void GAMEAPI sprites_update(void)
{
    sprites_move();
    flash_timer_step();
}

/* ---- Shadows ----
 *
 * Each car drops a sphere straight down from its shadow point (+0x14bc
 * from its position) onto the track; where it lands, the four corners
 * of the shadow (+0x720, 8 bytes each: int16 x, y, z, alpha at +7) are
 * laid on the plane under it, fading with height. +0x726 says whether
 * it is drawn, +0x1090 the smoothed ground normal. */

/* 0x46c520 */
void GAMEAPI shadow_probe(const uint8_t *car, uint8_t *slot)
{
    I32(slot, 0x08) = (I32(car, 0x14bc) + I32(car, 0x34)) >> 8;
    I32(slot, 0x0c) = (I32(car, 0x14c0) + I32(car, 0x38)) >> 8;
    I32(slot, 0x14) = 0;
    I32(slot, 0x10) = (I32(car, 0x14c4) + I32(car, 0x3c)) >> 8;
    I32(slot, 0x1c) = 0;
    I32(slot, 0x18) = 0x4000;
    I32(slot, 0x40) = 0;
    U16(slot, 0x44) = 0;
}

/* 0x46c450: on a hit, the offset from the car to it and the normal
 * eased a quarter towards the hit's. */
int32_t GAMEAPI shadow_ground(uint8_t *car, int32_t *normal, int32_t *offset, int32_t time, const int32_t *hit)
{
    int32_t i;

    if (!hit[7] || time < 1)
        return 0;
    for (i = 0; i < 3; i++)
        offset[i] = (int32_t)((uint32_t)hit[i] << 8) - I32(car, 0x34 + i * 4);
    for (i = 0; i < 3; i++)
        I32(car, 0x1090 + i * 4) = (I32(car, 0x1090 + i * 4) * 3 + hit[3 + i]) / 4;
    normal[0] = I32(car, 0x1090);
    normal[2] = I32(car, 0x1098);
    normal[1] = I32(car, 0x1094);
    return 1;
}

/* 0x46c750: one corner: the car's side and forward axes (signs by the
 * corner's bits), dropped onto the plane; returns how far below the car
 * the corner is. */
int32_t GAMEAPI shadow_corner(const uint8_t *car, const int32_t *forward, const int32_t *side, int32_t *out,
                              const int32_t *n, int32_t plane, uint8_t corner)
{
    int32_t fx, fy, fz, sx, sy, sz, y;

    if (corner & 2) {
        fx = -forward[0];
        fy = -forward[1];
        fz = -forward[2];
    } else {
        fx = forward[0];
        fy = forward[1];
        fz = forward[2];
    }
    if (corner & 1) {
        sx = -side[0];
        sy = -side[1];
        sz = -side[2];
    } else {
        sx = side[0];
        sy = side[1];
        sz = side[2];
    }
    out[0] = sx + fx;
    out[2] = sz + fz;
    y = mul16(mul16(n[0], out[0]) + mul16(n[2], out[2]) + plane, n[1]);
    out[0] = (I32(car, 0x34) + out[0]) >> 16;
    out[1] = (I32(car, 0x38) + y) >> 16;
    out[2] = (I32(car, 0x3c) + out[2]) >> 16;
    return y - sy - fy;
}

/* 0x46c580: the four corners on the ground plane (normal n, the ground
 * at offset), pushed a little off it; none on steep ground. */
void GAMEAPI shadow_corners(uint8_t *car, int32_t *n, const int32_t *offset)
{
    int32_t plane, scale, lift_x, lift_y, lift_z, height, forward[3], side[3], corner[3], i;
    uint8_t *point = car + 0x720;

    if (abs32(n[1]) < 0x1000) {
        car[0x726] = 0;
        return;
    }
    plane = -vec_dot(n, offset);
    scale = *(int32_t *)0x6cce7c ? 4 : 6;
    lift_x = (n[0] * scale) >> 16;
    lift_y = (n[1] * scale) >> 16;
    lift_z = (n[2] * scale) >> 16;
    height = abs32(n[1]);
    n[1] = -fix_div16(0x10000, n[1]);
    forward[0] = S16(car, 0xc8) * 0x380;
    forward[1] = S16(car, 0xce) * 0x380;
    forward[2] = S16(car, 0xd4) * 0x380;
    side[0] = S16(car, 0xc4) * 0x380;
    side[1] = S16(car, 0xca) * 0x380;
    side[2] = S16(car, 0xd0) * 0x380;
    for (i = 0; i < 4; i++, point += 8) {
        int32_t below = shadow_corner(car, forward, side, corner, n, plane, (uint8_t)(3 - i)) >> 16;
        int32_t alpha;

        S16(point, 0) = (int16_t)(lift_x + corner[0]);
        S16(point, 2) = (int16_t)(lift_y + corner[1]);
        S16(point, 4) = (int16_t)(lift_z + corner[2]);
        if (below < 0)
            below = 0;
        else if (below > 0x40)
            below = 0x40;
        alpha = (uint8_t)(0x80 - (uint8_t)(below * 2)) * height;
        point[7] = (uint8_t)(alpha / 0x10000);
    }
}

/* 0x46c300 */
void GAMEAPI shadows_update(void)
{
    uint8_t slots[6][0x48];
    int32_t hits[6][10], normal[3], offset[3], i;
    uint32_t *times = (uint32_t *)0x4d3e00;

    for (i = 0; i < CAR_COUNT; i++)
        shadow_probe(SLOT_CAR(i), slots[i]);
    for (i = 0; i < CAR_COUNT; i++)
        times[i] = slot_track_sweep(SLOT_CAR(i), slots[i], hits[i], 0x1010000);
    for (i = 0; i < CAR_COUNT; i++) {
        uint8_t *car = SLOT_CAR(i);

        if (shadow_ground(car, normal, offset, (int32_t)times[i], hits[i])) {
            car[0x726] = 1;
            shadow_corners(car, normal, offset);
        } else {
            car[0x726] = 0;
        }
    }
}

/* ---- Debris ----
 *
 * 64 pieces of 0x180 bytes from *0x5d9290: +0x10 position, +0x1c
 * velocity, +0x28 speed (slowing by +0x2e, itself easing to 1), +0x2c
 * steps left. 0x5d9288 is the loop's index. */

/* 0x47c210 */
void GAMEAPI debris_update(void)
{
    uint8_t *d = *(uint8_t **)0x5d9290;
    uint32_t *index = (uint32_t *)0x5d9288;

    for (*index = 0; *index < 0x40; ++*index, d += 0x180) {
        int32_t speed;

        if (!d[0x2c])
            continue;
        if (!--d[0x2c])
            grid_remove(d);
        speed = I32(d, 0x28);
        I32(d, 0x10) += (I32(d, 0x1c) >> 1) * speed;
        I32(d, 0x14) += (I32(d, 0x20) >> 1) * speed;
        I32(d, 0x18) += (I32(d, 0x24) >> 1) * speed;
        if (speed > 8) {
            int16_t slow = S16(d, 0x2e);

            I32(d, 0x28) -= slow;
            if (slow > 1)
                S16(d, 0x2e) = (int16_t)(slow - 1);
        } else {
            I32(d, 0x28) = 4;
        }
        I32(d, 0x14) += 0x48000;
    }
}

/* ---- Tyre trails ----
 *
 * Two trails a car (+0x14cc, 0x6d4 each: +0 next point, +4 points in
 * use, +8 int16 texture scroll, +0xa byte, +0x14 twelve points), from
 * a point beside each rear wheel's body (+0x518, 0xc4 apart). */

/* 0x481f20 */
void GAMEAPI tyre_trails_update(void)
{
    int32_t c, w;

    for (c = 0; c < CAR_COUNT; c++) {
        uint8_t *car = SLOT_CAR(c);
        uint32_t scroll;

        gte_load_matrix(car + 0xc4);
        g_effect_seed = g_effect_seed * 16807 & 0x7fffffff;
        scroll = (uint8_t)(((g_effect_seed >> 16) & 0x1f) + 0xf);
        for (w = 0; w < 2; w++) {
            uint8_t *trail = car + 0x14cc + w * 0x6d4;
            const uint8_t *wheel = *(uint8_t **)(car + 0x518 + w * 0xc4);
            const int32_t *r = (const int32_t *)0x6cccc8;
            int32_t *point, x, y, z;

            *(uint32_t *)0x6ccc68 = 0;
            *(uint16_t *)0x6ccc64 = (uint16_t)(w ? 9 : -8);
            *(uint16_t *)0x6ccc66 = (uint16_t)(S16(car, 0xcc) >= 0 ? -6 : 6);
            gte_rotate();
            x = (int32_t)((uint32_t)r[0] << 16) + I32(wheel, 0x34);
            y = (int32_t)((uint32_t)r[1] << 16) + I32(wheel, 0x38);
            z = (int32_t)((uint32_t)r[2] << 16) + I32(wheel, 0x3c);
            point = (int32_t *)(trail + 0x14) + I32(trail, 0) * 3;
            point[0] = x;
            point[1] = y;
            point[2] = z;
            if (++I32(trail, 0) >= 0xc)
                I32(trail, 0) = 0;
            if (I32(trail, 4) < 0xc)
                I32(trail, 4)++;
            S16(trail, 8) = (int16_t)(S16(trail, 8) + scroll);
            if (S16(trail, 8) >= 0x100)
                S16(trail, 8) = (int16_t)(S16(trail, 8) - 0x100);
            trail[0xa] += 0x14;
        }
    }
}

/* ---- Animated car textures ----
 *
 * With the option on (0x4b2198 +0x30), each car (whose +0x6ac record is
 * missing or off) steps a 9-frame animation every other step and copies
 * the frame's texture coordinates (0x6c1e14 entries 0x86 on from the
 * track's base +0x2e) into its 4 x 8 quads at 0x5f7440 (0x1088 a car),
 * spread down the texture. Cars at speed get an order number in the
 * slot's +0x265 (up to 6). */

/* 0x475430 */
void GAMEAPI car_texture_frames(int32_t car_index)
{
    uint8_t *base = (uint8_t *)0x5f7400 + car_index * 0x1088;
    uint32_t w0, w1, w2, w3, step, a = 0, b, *quad;
    uint8_t *entry;
    int32_t row, i;

    if (g_race_ticks & 1) {
        int32_t frame = I32(base, 0x1080) + 1;

        I32(base, 0x1080) = frame < 9 ? frame : 0;
    }
    entry = *(uint8_t **)0x6c1e14 +
            ((uint32_t)U16(*(uint8_t **)0x6c1eb0, 0x2e) + (uint32_t)I32(base, 0x1080) + 0x86) * 16;
    w0 = U32(entry, 0);
    w1 = U32(entry, 4);
    w3 = U16(entry, 0xa);
    w2 = U16(entry, 8);
    {
        uint32_t top = w0 >> 8 & 0xff, bottom = w2 >> 8 & 0xff;

        step = (top * 0xffff + bottom) << 16;
        b = step;
        quad = (uint32_t *)(base + 0x40);
        for (row = 0; row < 4; row++) {
            uint32_t va = ((a >> 18) + top) & 0xff, vb = ((b >> 18) + top) & 0xff;
            uint32_t *q = quad;

            w0 = (w0 & ~0xff00u) | va << 8;
            w1 = (w1 & ~0xff00u) | va << 8;
            w2 = (w2 & ~0xff00u) | vb << 8;
            w3 = (w3 & ~0xff00u) | vb << 8;
            for (i = 0; i < 8; i++, q += 0x84) {
                q[-8] = w0;
                q[8] = w2;
                q[0] = w1;
                q[0x10] = w3;
            }
            a += step;
            b += step;
            quad += 0x21;
        }
    }
}

/* 0x4751b0 */
void GAMEAPI car_textures_update(void)
{
    uint32_t order = 0, i;

    if (!g_options[0x30])
        return;
    for (i = 0; i < CAR_COUNT; i++)
        g_car_slots[i * CAR_SLOT_SIZE + 0x265] = 0xff;
    for (i = 0; i < CAR_COUNT; i++) {
        uint8_t *car = SLOT_CAR(i), *record = *(uint8_t **)(car + 0x6ac);
        int32_t speed;

        if (record != (uint8_t *)-1 && *record)
            continue;
        speed = (int32_t)((uint32_t)(I32(car, 0x654) - 0x3a0000) << 8) / 0x190000;
        if (speed > 1) {
            g_car_slots[i * CAR_SLOT_SIZE + 0x265] = (uint8_t)order;
            if (++order >= 6)
                return;
        }
        car_texture_frames((int32_t)i);
    }
}
