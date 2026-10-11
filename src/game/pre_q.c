/* Before the world step (sim_step 0x444120), group Q: 0x41ac50, the
 * track's animated pieces. Water meshes ripple (0x482df0) and the special
 * track objects listed at *0x6ce738 run: kind 0x14 a screen whose
 * texture coordinates and flicker colour (0x6ce734) animate, 0x1f a
 * burning wreck that throws blasts and shrinks, 0x20 a spray of particles.
 *
 * Also the effect spawners the blast dispatcher (0x42aa30) calls: particle
 * bursts (0x42ad60), lights (0x441d00 and the two wrappers), the flash /
 * glow / blast point globals, smoke clouds, shock waves, scorching the
 * track's vertex colours (0x42c760 with its row ranges 0x440c60) and
 * pieces (0x4624d0). 0x4411c0 marks mesh rows changed this frame.
 *
 * Still original: sound_play, the grid / sprite lists (0x4611d0,
 * 0x460ff0), particle_take 0x42ac30, particle_spawn 0x462690,
 * sprite_colour 0x42d300, explosion_effects 0x42aa30, cell_has_surface
 * 0x407f30 and track_cell 0x419420 (all called through their addresses,
 * so the hooks reach the C versions in the game). */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_seed (*(uint32_t *)0x4b11d8)
#define g_effect_seed (*(uint32_t *)0x4b11dc)
#define g_steps (*(uint32_t *)0x6c0a50)
#define g_sin ((const int16_t *)0x4b24f8)
#define g_cos ((const int16_t *)0x4b2578)
#define g_sections (*(uint8_t ***)0x6bfac0)
#define g_palette (*(const uint32_t **)0x6c1e20)

#define I32(p, off) (*(int32_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define PTR(p, off) (*(uint8_t **)((p) + (off)))
#define SHL(x, n) ((int32_t)((uint32_t)(int32_t)(x) << (n)))

/* The special track objects. */
#define g_specials_count (*(int32_t *)0x6ce6fc)
#define g_specials (*(uint8_t ***)0x6ce738)

/* The screen (object kind 0x14). */
#define g_screen_time (*(int32_t *)0x6ce700)
#define g_screen_state (*(uint32_t *)0x6ce704)
#define g_screen_model (*(uint8_t **)0x6ce708)
#define g_screen_polys (*(int16_t *)0x6ce70c)
#define g_screen_poly ((const int8_t *)0x6ce710)
#define g_screen_uv ((const uint8_t *)0x6ce714)
#define g_screen_colour (*(uint32_t *)0x6ce734)

/* Water meshes. */
#define g_waters_count (*(int32_t *)0x5d637c)
#define g_waters (*(uint8_t **)0x5d6378)

/* Mesh rows changed this frame. */
#define g_rows_count (*(uint32_t *)0x5d7fe4)
#define g_rows ((uint32_t *)0x5d71e0)

/* Lights. */
#define g_lights_count (*(uint32_t *)0x5d72e0)
#define g_lights ((uint8_t *)0x5d7300)

/* Pieces: a pool of 0x80 byte records with a free stack and a used list. */
#define g_pieces (*(uint8_t **)0x6b983c)
#define g_pieces_free (*(uint16_t **)0x6b9840)
#define g_pieces_used (*(uint16_t **)0x6b9844)
#define g_pieces_used_count (*(int32_t *)0x6b9848)
#define g_pieces_free_top (*(int32_t *)0x6b984c)

/* GTE scratch registers. */
#define g_gte_ir ((volatile int16_t *)0x6ccc88) /* three, 4 bytes apart */
#define g_gte_sqr ((volatile int32_t *)0x6cccc8)

/* Still original. */
typedef void(GAMEAPI *body_fn)(uint8_t *body);
typedef void(GAMEAPI *place_fn)(uint8_t *body, int32_t x, int32_t y, int32_t z);
typedef uint8_t(GAMEAPI *sound_fn)(uint32_t sound, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(GAMEAPI *surface_fn)(uint32_t surface, const int32_t *at);
typedef void(GAMEAPI *spawn_fn)(const int32_t *pos, int32_t *vel, int32_t kind);
typedef uint8_t *(GAMEAPI *take_fn)(void);
typedef uint32_t(GAMEAPI *colour_fn)(uint32_t behaviour, uint32_t life);
typedef void(GAMEAPI *blast_fn)(const int32_t *at, const int32_t *dir, int32_t size, uint32_t what, uint32_t sound);
typedef uint8_t *(GAMEAPI *cell_fn)(int32_t x, int32_t y, int32_t z, int32_t *off_track);

#define grid_unlink_at ((body_fn)0x4611d0)
#define sprite_place ((place_fn)0x460ff0)
#define sound_play_at ((sound_fn)0x471c90)
#define surface_near ((surface_fn)0x407f30)
#define spawn_particle ((spawn_fn)0x462690)
#define take_particle ((take_fn)0x42ac30)
#define particle_colour ((colour_fn)0x42d300)
#define blast_effects ((blast_fn)0x42aa30)
#define cell_at ((cell_fn)0x419420)

static uint32_t seed_step(uint32_t *seed)
{
    *seed = *seed * 16807 & 0x7fffffff;
    return *seed;
}

/* 0x4411c0: note that rows of a mesh section changed (the row count in
 * bits 11..21 of `rows`, the section in the low byte of `section`);
 * skipped when the same as the last entry. */
void GAMEAPI mesh_rows_touch(uint32_t rows, uint32_t section)
{
    uint32_t n = g_rows_count, entry;

    if (n >= 0x40)
        return;
    entry = (0x40000000 | (section & 0x7f)) | (rows & 0x3fffff) << 7;
    if (n && g_rows[n - 1] == entry)
        return;
    g_rows[n] = entry;
    g_rows_count++;
}

/* 0x441d00: add a light (up to 16 a frame): position, colour, two sizes,
 * a kind and optionally three more words (the body it follows first). */
void GAMEAPI light_add(const int32_t *pos, const uint32_t *colour, uint32_t radius, uint32_t falloff, uint32_t kind,
                       const uint32_t *extra)
{
    uint8_t *light;
    int i;

    if (g_lights_count >= 0x10)
        return;
    light = g_lights + g_lights_count * 0x24;
    I32(light, 0) = pos[0];
    I32(light, 4) = pos[1];
    I32(light, 8) = pos[2];
    U32(light, 0xc) = *colour;
    U16(light, 0x12) = (uint16_t)falloff;
    U16(light, 0x10) = (uint16_t)radius;
    U32(light, 0x14) = kind;
    if (extra)
        for (i = 0; i < 3; i++)
            U32(light, 0x18 + i * 4) = extra[i];
    g_lights_count++;
}

/* 0x42b2d0: a small orange light. */
void GAMEAPI light_flash_small(const int32_t *pos, uint32_t colour)
{
    uint32_t radius = colour + 3;

    if ((uint16_t)radius < 5)
        radius = 5;
    light_add(pos, &colour, radius, 2, 0x26, 0);
}

/* 0x42b300: a light whose size grows with `size`. */
void GAMEAPI light_flash(const int32_t *pos, uint32_t size)
{
    light_add(pos, &size, (size >> 2) + 5, 3, (size * 8 + 0x2d0) & 0xffff, 0);
}

/* 0x42b330: the blast flash at a point. */
void GAMEAPI blast_flash_set(const int32_t *pos, int32_t size)
{
    *(int32_t *)0x6ccbac = 1;
    *(int32_t *)0x6ccba0 = pos[0];
    *(int32_t *)0x6ccba4 = pos[1];
    *(int32_t *)0x6ccba8 = pos[2];
    *(int32_t *)0x6ccbb0 = size * 4 + 4;
}

/* 0x42b370: the blast glow at a point. */
void GAMEAPI blast_glow_set(const int32_t *pos, int32_t size)
{
    *(int32_t *)0x6cca54 = 1;
    *(int32_t *)0x6ccb90 = pos[0];
    *(int32_t *)0x6ccb94 = pos[1];
    *(int32_t *)0x6ccb98 = pos[2];
    *(int32_t *)0x6ccb78 = size * 4 + 4;
}

/* 0x42b3b0: start a smoke cloud in the first free of five slots. */
void GAMEAPI smoke_cloud_start(const int32_t *pos, int32_t big)
{
    uint8_t *base = *(uint8_t **)0x6ccb54, *cloud;
    uint32_t i;

    for (i = 0; i < 5; i++)
        if (!I32(base, i * 0x1e2c + 0xc))
            break;
    if (i >= 5)
        return;
    cloud = base + i * 0x1e2c;
    I32(cloud, 0) = pos[0];
    I32(cloud, 4) = pos[1] - 0x140000;
    I32(cloud, 8) = pos[2];
    I32(cloud, 0xc) = 0x3c;
    I32(cloud, 0x10) = 0;
    I32(cloud, 0x20) = 0x10;
    if (big == 1) {
        I32(cloud, 0x24) = 0x3c;
        I32(cloud, 0xc) = 0x3c;
        I32(cloud, 0x28) = 0x40;
    } else {
        I32(cloud, 0x28) = 0x15;
        I32(cloud, 0x24) = 0x1e;
        I32(cloud, 0xc) = 0x1e;
    }
    switch (((seed_step(&g_seed) >> 15) * 0xff) >> 16 & 3) {
    case 0:
    case 1:
        I32(cloud, 0x18) = 0x3a;
        I32(cloud, 0x1c) = 5;
        I32(cloud, 0x14) = 0xffffff;
        break;
    default:
        I32(cloud, 0x18) = 0x86;
        I32(cloud, 0x1c) = 9;
        I32(cloud, 0x14) = 0xb0b0b0;
        break;
    }
}

/* 0x42c360: start a shock wave in the first free of three slots. */
void GAMEAPI shockwave_start(const int32_t *pos)
{
    uint8_t *base = *(uint8_t **)0x6cc800, *wave;
    uint32_t i;

    for (i = 0; i < 3; i++)
        if (!I32(base, i * 0xf14 + 0xc))
            break;
    if (i >= 3)
        return;
    wave = base + i * 0xf14;
    I32(wave, 0) = pos[0];
    I32(wave, 4) = pos[1];
    I32(wave, 8) = pos[2];
    I32(wave, 0xc) = 0x18;
    I32(wave, 0x10) = 0;
}

/* 0x42c3c0: the blast point, in whole units. */
void GAMEAPI blast_point_set(const int32_t *pos)
{
    *(int32_t *)0x4be1a8 = 1;
    *(int16_t *)0x4be180 = (int16_t)(pos[0] >> 16);
    *(int16_t *)0x4be182 = (int16_t)(pos[1] >> 16);
    *(int16_t *)0x4be184 = (int16_t)(pos[2] >> 16);
}

/* 0x462450: take a piece from the free stack onto the used list; -1 when
 * none is free. */
int32_t GAMEAPI piece_take(void)
{
    int32_t top = g_pieces_free_top;
    uint16_t piece;

    if (top < 0)
        return -1;
    piece = g_pieces_free[top];
    g_pieces_free_top = top - 1;
    g_pieces_used[g_pieces_used_count] = piece;
    g_pieces_used_count++;
    return piece;
}

/* 0x4624d0: throw size*3+12 pieces from `pos` along `vel` (spread). */
void GAMEAPI pieces_spawn(const int32_t *pos, const int32_t *vel, int32_t size)
{
    int32_t n = size * 3 + 0xc, i, y, z;
    uint8_t *piece;

    while (n > 0) {
        i = piece_take();
        if (i == -1)
            return;
        piece = g_pieces + SHL(i, 7);
        U16(piece, 0x6a) = 1;
        I32(piece, 0x4c) = pos[0];
        y = pos[1];
        I32(piece, 0x50) = y;
        z = pos[2];
        I32(piece, 0x54) = z;
        sprite_place(piece, I32(piece, 0x4c), y, z);
        U16(piece, 8) = (uint16_t)((((seed_step(&g_seed) >> 15) << 4) >> 16) + 0x27);
        I32(piece, 0x58) = SHL((int32_t)(((seed_step(&g_seed) >> 15) << 11) >> 16) - 0x400, 11) +
                           (int32_t)((uint32_t)(vel[0] >> 2) & 0xfffff800);
        I32(piece, 0x5c) = SHL((int32_t)(((seed_step(&g_seed) >> 15) << 11) >> 16) - 0x400, 11) +
                           (int32_t)((uint32_t)(vel[1] >> 2) & 0xfffff800);
        I32(piece, 0x60) = SHL((int32_t)(((seed_step(&g_seed) >> 15) << 11) >> 16) - 0x400, 11) +
                           (int32_t)((uint32_t)(vel[2] >> 2) & 0xfffff800);
        U32(piece, 0x30) = 0x2f6f6f;
        n--;
        S16(piece, 0x64) = (int16_t)(I32(piece, 0x4c) >> 16);
        S16(piece, 0x66) = (int16_t)(I32(piece, 0x50) >> 16);
        S16(piece, 0x68) = (int16_t)(I32(piece, 0x54) >> 16);
        U32(piece, 0x14) = 0xae00052f;
    }
}

/* 0x42ad60: a burst of particles (record kind 9) of behaviour `kind`
 * (1, 4, 8, 0x10 or 0x4000) from `pos`; `dir` aims kinds 4 and 8; the
 * count follows `size` and the detail level 0x4ac724. The top byte of
 * `owner` numbers them round. */
void GAMEAPI particles_burst(const int32_t *pos, const int32_t *dir, uint32_t size, uint32_t owner, uint32_t kind)
{
    uint32_t behaviour = kind & 0xffff, n, number = 0, round, r;
    int32_t life = 5;
    uint8_t *p;

    if (behaviour == 8)
        n = size + 3;
    else if (behaviour == 1 || behaviour == 0x10)
        n = (size >> 2) + 1;
    else if (behaviour == 4)
        n = (size >> 1) + 2;
    else if (behaviour == 0x4000)
        n = 0x41;
    else
        return;
    if (!n)
        return;
    n *= *(uint32_t *)0x4ac724;
    n = (n & 0xffffff00) > 0x100 ? n >> 8 : 1;
    round = owner >> 24;
    do {
        p = take_particle();
        U16(p, 0xa2) = (uint16_t)kind;
        I32(p, 0) = 9;
        r = (seed_step(&g_seed) >> 15) & 0xffff;
        I32(p, 0x88) = pos[0];
        I32(p, 0x8c) = pos[1];
        I32(p, 0x90) = pos[2];
        switch (behaviour) {
        case 4:
            U8(p, 0x95) = (uint8_t)(((r & 0x1f) + 8) << 1);
            life = (int16_t)(((int32_t)r >> 5 & 0x1f) + 0x30);
            S16(p, 0x96) = (int16_t)life;
            break;
        case 1:
            U8(p, 0x95) = (uint8_t)(((uint8_t)size + 3) << 1);
            life = (int16_t)((r & 0xf) + 0x30);
            S16(p, 0x96) = (int16_t)life;
            break;
        case 8:
            U8(p, 0x95) = (uint8_t)((r & 0x3f) + 0x8e);
            life = (int16_t)(((int32_t)r >> 5 & 0x1f) + 0x20);
            S16(p, 0x96) = (int16_t)life;
            break;
        case 0x10:
            I32(p, 0xb0) = 1;
            U8(p, 0x95) = (uint8_t)((r & 0x1f) + 0x10);
            life = (int16_t)((((int32_t)r >> 5 & 0x1f) + (int32_t)size * 8) * 4 + 0x348);
            S16(p, 0x96) = (int16_t)life;
            break;
        case 0x4000:
            life = 0x2d;
            S16(p, 0x96) = 0x2d;
            U8(p, 0x95) = (uint8_t)((r & 4) + 0xc);
            break;
        }
        sprite_place(p, I32(p, 0x88), I32(p, 0x8c), I32(p, 0x90));
        U8(p, 0x94) = 1;
        if ((uint16_t)(kind & 0xc)) {
            U16(p, 0x9c) = (uint16_t)((((seed_step(&g_seed) >> 15) << 14) >> 16) + (dir[0] >> 8) - 0x2000);
            U16(p, 0x9e) = (uint16_t)((((seed_step(&g_seed) >> 15) << 14) >> 16) + (dir[1] >> 8) - 0x2000);
            U16(p, 0xa0) = (uint16_t)((((seed_step(&g_seed) >> 15) << 14) >> 16) + (dir[2] >> 8) - 0x2000);
        }
        if ((uint16_t)kind == 0x10) {
            I32(p, 0x88) += SHL((int32_t)(((seed_step(&g_seed) >> 15) << 13) >> 16) - 0x2000, size & 31);
            I32(p, 0x90) += SHL((int32_t)(((seed_step(&g_seed) >> 15) << 13) >> 16) - 0x2000, size & 31);
        } else if ((uint16_t)kind == 1) {
            I32(p, 0x88) += SHL((int32_t)(((seed_step(&g_seed) >> 15) << 13) >> 16) - 0x2000, 10);
            I32(p, 0x90) += SHL((int32_t)(((seed_step(&g_seed) >> 15) << 13) >> 16) - 0x2000, 10);
            r = (((seed_step(&g_seed) >> 15) << 13) >> 16);
            U16(p, 0xa0) = 0;
            U16(p, 0x9e) = 0;
            I32(p, 0x8c) += SHL(-8 - (int32_t)r, 10);
            U16(p, 0x9c) = 0;
        } else if ((uint16_t)kind == 0x4000) {
            uint32_t lo, hi;

            r = (seed_step(&g_seed) >> 15) & 0xffff;
            hi = r >> 8;
            lo = r & 0xff;
            S16(p, 0x9c) = (int16_t)(g_cos[lo] * g_sin[hi] >> 14);
            S16(p, 0x9e) = (int16_t)(g_sin[lo] * g_sin[hi] >> 14);
            S16(p, 0xa0) = g_cos[hi];
        }
        U32(p, 0x98) = particle_colour(behaviour, (uint32_t)life);
        U32(p, 0xa4) = owner;
        U8(p, 0xa6) = (uint8_t)number;
        number++;
        if (number >= (round & 0xff))
            number = 0;
    } while (--n);
}

/* 0x4282d0: squares of the three GTE IR registers. */
void GAMEAPI gte_square_ir(void)
{
    g_gte_sqr[0] = g_gte_ir[0] * g_gte_ir[0];
    g_gte_sqr[1] = g_gte_ir[2] * g_gte_ir[2];
    g_gte_sqr[2] = g_gte_ir[4] * g_gte_ir[4];
}

/* Scale a row distance by 2^(s/2) (odd steps by about sqrt 2). */
static int32_t scale_half_steps(int32_t d, int32_t s)
{
    int32_t shift;

    if (s < 0) {
        uint32_t sign = (uint32_t)(s >> 31), a = ((uint32_t)s ^ sign) - sign;

        a -= (uint32_t)((int32_t)a >> 31);
        shift = (int32_t)a >> 1;
        d >>= shift & 31;
        if (s & 1) {
            int32_t m = (int32_t)((uint32_t)d * 0xb5);

            d = (int32_t)((uint32_t)m + ((uint32_t)(m >> 31) & 0xff)) >> 8;
        }
    } else {
        shift = s >> 1;
        d = SHL(d, shift & 31);
        if (s & 1) {
            int32_t m = (int32_t)((uint32_t)d * 0xb5);

            d = (int32_t)((uint32_t)m + ((uint32_t)(m >> 31) & 0x7f)) >> 7;
        }
    }
    return d;
}

/* 0x440c60: the up to four mesh row ranges (section in the low byte, rows
 * from bits 8..19 to bits 20..31) a track cell covers, widened or narrowed
 * by `level` (10 adds one row at the end). */
void GAMEAPI cell_row_ranges(const uint32_t *cell, uint32_t *out, int32_t level)
{
    uint32_t c0, c1, base, section, i;

    out[0] = out[1] = out[2] = out[3] = 0;
    c1 = cell[1];
    {
        const uint8_t *rec = *(const uint8_t **)0x6bfb40 + ((c1 >> 16) & 0x3fff) * 6;

        base = U16(rec, 0) & 0x3ff;
        section = rec[2] & 0x7f;
    }
    c0 = cell[0];
    if (c0 & 1) {
        uint32_t o = out[0], v, a, e;

        v = (base - (c0 >> 7 & 0x3f)) << 8;
        o = ((v ^ o) & 0xfff00) ^ o;
        out[0] = o;
        a = (cell[0] & 0x7e) + 2 * base;
        e = ((((uint32_t)0 - a) << 19 ^ o) & 0xfffff) ^ (a << 19);
        out[0] = ((e ^ section) & 0xff) ^ e;
    } else {
        const uint8_t *p = *(const uint8_t **)0x6bfacc + (c0 & 0x1fff) + 2 * (c1 & 0xffff);
        uint32_t *o = out, ax;

        for (i = 0; i < 4; i++, o++) {
            ax = U16(p, 0);
            if (ax & 1) {
                uint32_t d = *o, v, e;

                v = (base - (ax >> 7 & 0x3f)) << 8;
                v = ((v ^ d) & 0xfff00) ^ d;
                *o = v;
                e = (((uint32_t)(p[0] >> 1 & 0x3f) + base) << 20) ^ (v & 0xfffff);
                *o = ((e ^ section) & 0xff) ^ e;
                if (p[1] & 0x20)
                    break;
                p += 2;
            } else {
                uint32_t w = ax | (uint32_t)U16(p, 2) << 16, lo = w & 0xffe, t, u, v;

                t = (((w & 0xfffff000) + (lo << 11)) << 8) & 0xfff00;
                u = ((w & 0xff000) + (lo << 11)) << 8;
                v = (t ^ u) & ~0xffu;
                v ^= lo << 7; /* the original masks lo again with 0x1ffe */
                v |= w >> 20 & 0x7f;
                *o = v;
                if (w & 0x8000000)
                    break;
                p += 4;
            }
        }
    }
    if (level == 10) {
        for (i = 0; i < 4; i++) {
            uint32_t v = out[i], n, e;

            if (!v)
                return;
            n = U32(g_sections[v & 0xff], 0xc);
            e = ((v & 0xfff00000) + 0x100000) ^ (v & 0xfffff);
            out[i] = e;
            if (e >> 20 > n)
                out[i] = (n << 20) ^ (e & 0xfffff);
        }
    } else {
        int32_t s = level - 10;

        for (i = 0; i < 4; i++) {
            uint32_t v = out[i], lo, hi, mid;
            int32_t up, down, last, a, b;

            if (!v)
                return;
            lo = v >> 8 & 0xfff;
            hi = v >> 20;
            mid = (lo + hi) >> 1;
            up = (int32_t)(hi - mid);
            down = (int32_t)(mid - lo);
            last = I32(g_sections[v & 0xff], 0xc) - 1;
            if (hi < (uint32_t)last)
                up = scale_half_steps(up, s);
            if ((v & 0xfff00) > 0x100)
                down = scale_half_steps(down, s);
            a = (int32_t)mid - down - 1;
            b = up + (int32_t)mid + 1;
            if (a >= last + 1)
                a = last + 1;
            if (b >= last + 1)
                b = last + 1;
            if (a < 0)
                a = 0;
            if (b < 0)
                b = 0;
            out[i] = ((((uint32_t)a & 0xfff) | (uint32_t)b << 12) << 8) | (out[i] & 0xff);
        }
    }
}

/* 0x42c760: darken the track's vertex colours round a blast at `pos`
 * (`level` 6..10 sets the reach). */
void GAMEAPI scorch_track(const int32_t *pos, uint32_t level)
{
    const uint32_t *palette = g_palette;
    uint32_t centre = (uint16_t)(pos[0] >> 16) | (uint32_t)(uint16_t)(pos[1] >> 16) << 16;
    uint32_t centre_z = (uint16_t)(pos[2] >> 16), rows[4], tag = 0;
    int32_t off_track, k;
    uint8_t *cell;

    *(int32_t *)0x4b2c64 = 1;
    if (level < 6)
        level = 6;
    else if (level > 10)
        level = 10;
    cell = cell_at(pos[0], pos[1], pos[2], &off_track);
    cell_row_ranges((const uint32_t *)cell, rows, (int32_t)level);
    for (k = 0; k < 4; k++) {
        uint32_t v = rows[k], lo, hi, width;
        uint8_t *section, *vert, *end;

        if (!v)
            return;
        section = g_sections[v & 0xff];
        lo = v >> 8 & 0xfff;
        width = U32(section, 8) + 1;
        hi = (v >> 20) + 1;
        if ((int32_t)hi > I32(section, 0xc) + 1)
            hi--;
        tag = (hi & 0x7ff) << 11 | ((lo & 0x7ff) ^ (tag & 0xffc00000));
        mesh_rows_touch(tag, v & 0xff);
        vert = PTR(section, 0x20) + width * lo * 8;
        end = PTR(section, 0x20) + width * hi * 8;
        for (; vert < end; vert += 8) {
            uint32_t d = U32(vert, 0) - centre, sum, colour, total;
            int32_t dist;

            g_gte_ir[0] = (int16_t)d;
            g_gte_ir[2] = (int16_t)((int32_t)d >> 16);
            g_gte_ir[4] = (int16_t)(S16(vert, 4) - (int32_t)centre_z);
            gte_square_ir();
            sum = (uint32_t)g_gte_sqr[2] + (uint32_t)g_gte_sqr[1] + (uint32_t)g_gte_sqr[0];
            dist = (int32_t)sum >> (level & 31);
            if (dist > 0x3ff)
                continue;
            dist >>= 6;
            colour = palette[(uint32_t)(I32(vert, 4) >> 22) & 0x3ff];
            total = (colour >> 16 & 0xff) + (colour >> 8 & 0xff) + (colour & 0xff);
            if ((int32_t)total >= SHL(dist, 5))
                U32(vert, 4) = (uint32_t)SHL(dist + 0x3f0, 22) | (U32(vert, 4) & 0x3fffff);
        }
    }
}

/* 0x482df0: ripple the water meshes near a camera: each record (0x1c
 * bytes) moves its vertices' heights and colours along a sine wave and
 * its edge vertices sideways. */
void GAMEAPI water_ripples_update(void)
{
    int32_t n = g_waters_count;
    uint8_t *water = g_waters;
    uint32_t tag = 0;

    for (; n > 0; n--, water += 0x1c) {
        uint32_t old, new, cols, extra, row, col, u, v;
        int32_t ax, az;
        uint8_t *p;
        int16_t *q;

        if (!(*(uint8_t *)0x6cce40 & 4)) {
            uint32_t cars = g_race[0x54d];
            const uint32_t *camera = (const uint32_t *)0x6bfba8;

            if (!cars)
                continue;
            for (; (int32_t)cars > 0; cars--, camera = (const uint32_t *)((const uint8_t *)camera + 0x274))
                if (*camera != 0x12345678 &&
                    surface_near(U16(water, 0), (const int32_t *)(**(uint8_t ***)0x6bfba8 + 0x34)))
                    break;
            if ((int32_t)cars <= 0)
                continue;
        }
        p = PTR(water, 0xc);
        old = U16(water, 2);
        extra = U16(water, 0xa);
        new = old + 1;
        if (new >= 0x800)
            new = 0;
        U16(water, 2) = (uint16_t)new;
        u = old * 4;
        v = new * 4;
        for (row = 0; row < U16(water, 6); row++) {
            uint32_t shift = row ? 9 : 10, uu, diff;

            u += 4;
            v += 4;
            diff = v - u;
            uu = u;
            for (col = 0; col < U16(water, 4); col++, p += 8) {
                int32_t a = g_sin[uu & 0xff] >> ((((int32_t)uu >> 8) & 3) ? shift : shift - 1);
                uint32_t w = diff + uu;
                int32_t b = g_sin[w & 0xff] >> ((((int32_t)w >> 8) & 3) ? shift : shift - 1);

                S16(p, 2) = (int16_t)(b + (S16(p, 2) - a));
                if ((int32_t)col < (int32_t)U16(water, 4) - 1) {
                    U16(p, 6) = (uint16_t)(SHL((b >> (12 - shift)) + 0x3df, 6) | 6);
                    uu += 0x20;
                }
            }
            p += extra * 8;
        }
        ax = S16(water, 0x14);
        az = S16(water, 0x18);
        cols = U16(water, 4);
        q = (int16_t *)PTR(water, 0x10);
        u = old * 4 - 0x20;
        v = u + (new - old) * 4;
        for (row = 0; row < U16(water, 6); row++, u += 4, v += 4) {
            int32_t a = g_sin[u & 0xff] >> ((((int32_t)u >> 8) & 3) ? 10 : 9);
            int32_t b = g_sin[v & 0xff] >> ((((int32_t)v >> 8) & 3) ? 10 : 9);

            q[0] = (int16_t)(q[0] + ((b * ax >> 9) - (a * ax >> 9)));
            q[2] = (int16_t)(q[2] + ((b * az >> 9) - (a * az >> 9)));
            q = (int16_t *)((uint8_t *)q + (cols + extra) * 8);
        }
        tag = (uint32_t)(U16(water, 6) & 0x7ff) << 11 | (tag & 0xffc00000);
        mesh_rows_touch(tag, U16(water, 0));
    }
}

/* 0x41b000: the screen (object kind 0x14): its texture coordinates and
 * flicker colour 0x6ce734 run through states 0x6ce704 1..7. */
void GAMEAPI screen_update(uint8_t *object)
{
    uint8_t mode = g_race[0x551];
    int32_t t, list, i;
    uint32_t colour;

    (void)object;
    if (mode == 3 || mode == 8) {
        g_screen_state = 0;
        return;
    }
    switch (g_screen_state) {
    case 1:
        g_screen_colour = 0;
        return;
    case 2:
        for (list = 0; list < 2; list++) {
            uint8_t *polys = PTR(g_screen_model, list ? 0x18 : 0x14);

            for (i = 0; i < g_screen_polys; i++) {
                uint8_t *poly = polys + g_screen_poly[i] * 0x84;

                poly[0x20] = (uint8_t)(g_screen_uv[0] + 0x40);
                poly[0x40] = (uint8_t)(g_screen_uv[0] + 0x40);
                poly[0x60] = (uint8_t)(g_screen_uv[0] + 0x4f);
                poly[0x80] = (uint8_t)(g_screen_uv[0] + 0x4f);
                poly[0x21] = g_screen_uv[1];
                poly[0x41] = g_screen_uv[3];
                poly[0x61] = g_screen_uv[5];
                poly[0x81] = g_screen_uv[7];
            }
        }
        g_screen_colour = 0;
        g_screen_state = 3;
        /* fall through */
    case 3:
        if (g_steps % 25 == 0)
            sound_play_at(0x28, 6, 0x400, 10000, 0);
        return;
    case 4:
        t = g_screen_time * 2;
        if (t > 0x6e)
            t = 0x6e;
        g_screen_colour = (uint32_t)t;
        g_screen_time++;
        return;
    case 5:
        t = 200 - g_screen_time;
        if (t > 0x6e) {
            t = 0x6e;
        } else if (t < 0) {
            t = 0;
            g_screen_state = 6;
        }
        g_screen_colour = (uint32_t)SHL(t, 8);
        g_screen_time++;
        return;
    case 6:
        for (list = 0; list < 2; list++) {
            uint8_t *polys = PTR(g_screen_model, list ? 0x18 : 0x14);

            for (i = 0; i < g_screen_polys; i++) {
                uint8_t *poly = polys + g_screen_poly[i] * 0x84;
                const uint8_t *uv = g_screen_uv + i * 8;

                poly[0x20] = uv[0];
                poly[0x40] = uv[2];
                poly[0x60] = uv[4];
                poly[0x80] = uv[6];
                poly[0x21] = uv[1];
                poly[0x41] = uv[3];
                poly[0x61] = uv[5];
                poly[0x81] = uv[7];
            }
        }
        g_screen_state = 7;
        t = 0;
        colour = 0;
        break;
    case 7:
        t = g_screen_time;
        if (t < 0x80) {
            colour = (uint32_t)t | ((uint32_t)t << 8 | (uint32_t)t) << 8;
        } else {
            colour = 0x808080;
            if (!(seed_step(&g_seed) & 0x1ff0000))
                colour = 0x303030;
        }
        break;
    default:
        return;
    }
    t++;
    g_screen_colour = colour;
    g_screen_time = t;
    if (t > 0x400)
        g_screen_time = 0x200;
}

/* 0x41ada0: a burning wreck (object kind 0x1f, state +0x184): throws a
 * blast from the table at 0x4a7d40 / 0x4a7d20 each step and shrinks its
 * matrix for 0x60 steps (+0x188), then leaves the grid. */
void GAMEAPI wreck_burn_update(uint8_t *object)
{
    int32_t state = I32(object, 0x184), dir[3], pos[3], i;
    uint32_t r, what;

    if (state == 3) {
        grid_unlink_at(object);
        I32(object, 0x184) = 0;
        return;
    }
    if (state != 2)
        return;
    if (S16(object, 0x188) >= 0x60) {
        I32(object, 0x184) = 3;
        return;
    }
    pos[1] = I32(object, 0x38) + 0x280000;
    dir[0] = 0;
    dir[1] = (int32_t)0xffc00000;
    dir[2] = 0;
    r = ((seed_step(&g_effect_seed) >> 15) * 0x7fff) >> 16;
    pos[0] = I32(object, 0x34) + SHL((int32_t)(r & 0x3f) - 0x20, 17);
    pos[2] = I32(object, 0x3c) + SHL((int32_t)(r >> 8 & 0x3f) - 0x20, 17);
    what = *(const uint32_t *)(0x4a7d20 + *(const uint8_t *)(0x4a7d40 + S16(object, 0x188)) * 4);
    if (what)
        blast_effects(pos, dir, 9, what, 0xb);
    for (i = 0; i < 9; i++)
        S16(object, 0xc4 + i * 2) = (int16_t)fix_mul8(0xfd, S16(object, 0xc4 + i * 2));
    S16(object, 0x188)++;
}

/* 0x43e640: a spray (object kind 0x20, state +0x180): idles, builds up
 * (+0x186 particles a step) to five, holds, then dies away; throws that
 * many particles (kind 0x19) at a fixed point. */
void GAMEAPI spray_update(uint8_t *object)
{
    int32_t n;

    switch (I32(object, 0x180)) {
    case 1:
        S16(object, 0x184)++;
        if (!(U8(object, 0x184) & 0x3f)) {
            S16(object, 0x186)++;
            if (S16(object, 0x186) == 5) {
                I32(object, 0x180) = 2;
                U16(object, 0x184) = (uint16_t)((seed_step(&g_seed) >> 16 & 0x10) + 8);
            }
        }
        break;
    case 2:
        S16(object, 0x184)--;
        if (!S16(object, 0x184))
            I32(object, 0x180) = 3;
        break;
    case 3:
        S16(object, 0x184)++;
        if (!(U8(object, 0x184) & 1)) {
            S16(object, 0x186)--;
            if (!S16(object, 0x186)) {
                I32(object, 0x180) = 0;
                U16(object, 0x184) = 0;
            }
        }
        break;
    default: {
        uint32_t r = seed_step(&g_seed) >> 16;

        if (!(r & 0x1f)) {
            I32(object, 0x180) = 1;
            U16(object, 0x186) = 1;
        } else {
            U16(object, 0x186) = (r & 0xc0) ? 0 : 1;
        }
        break;
    }
    }
    n = S16(object, 0x186);
    for (; n > 0; n--) {
        int32_t pos[3], vel[3];
        uint32_t r1, r2, s = g_seed * 16807 & 0x7fffffff;

        r1 = s >> 16;
        pos[0] = SHL((int32_t)(r1 & 0x1f) + 0x221, 17);
        pos[1] = (int32_t)0xfd000000;
        pos[2] = SHL((int32_t)((int32_t)r1 >> 6 & 0x1f) + 0xef1, 17);
        g_seed = s;
        r2 = seed_step(&g_seed) >> 16;
        vel[0] = SHL((int32_t)(r2 & 0xf) - 7, 15);
        vel[2] = SHL((int32_t)((int32_t)r2 >> 10 & 0xf) - 7, 15);
        vel[1] = SHL(-0x18 - (int32_t)((int32_t)r2 >> 6 & 0xf), 15);
        spawn_particle(pos, vel, 0x19);
    }
}

/* 0x41ad50: run the special track objects. */
void GAMEAPI track_specials_update(void)
{
    int32_t n = g_specials_count;
    uint8_t **object = g_specials;

    for (; n > 0; n--, object++) {
        uint32_t kind = U32(*object, 0) & 0x1ff;

        if (kind == 0x14)
            screen_update(*object);
        else if (kind == 0x1f)
            wreck_burn_update(*object);
        else if (kind == 0x20)
            spray_update(*object);
    }
}

/* 0x41ac50: once per step before the world step. */
void GAMEAPI track_animation_update(void)
{
    water_ripples_update();
    track_specials_update();
}
