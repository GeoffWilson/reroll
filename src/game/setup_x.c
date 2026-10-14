/* Race setup (0x46a060), group X: the parts of it that set up the AI, the
 * track's fixed lights, the effect pools, the sound listeners and the text
 * fonts' character maps.
 *
 * AI (0x401470 for each AI car): the first AI car of a race also fixes the
 * AI difficulty mode (0x4a714c, from the race mode), the faces of each body
 * the AI must drive round (0x403750), the difficulty band (0x47ff40), a
 * rival for each human player (0x411010) and which short racing lines stay
 * open (0x47ffd0); then the car's skill bits are set from its character's
 * table (0x40f3e0) and its first plan made (0x4105e0, aiplan.c).
 *
 * Lights (0x441b90): each of the track's fixed lights (0x20-byte records at
 * *0x6bfaec, count *0x6bfae8) gets a light record (0x40 bytes at 0x5d8000,
 * 0x4412e0) and is entered in the light grid of each section it touches
 * (0x441000).
 *
 * Pools: the 0x200 shrapnel pieces (0x462360), the tyre trails (0x481e50),
 * a car's glow sprites (0x490390), the countdown (0x46a940), the glint
 * count (0x4430f0).
 *
 * Sound (0x4730b0): one listener per view (0x4718d0), the engine and tune
 * state of each car reset.
 *
 * Text (0x478ff0, 0x42e0c0): the character maps of the three fonts and the
 * font table. Building the font textures themselves (0x4432d0, reading
 * FONT.BMP into Direct3D textures) is left original, as are the file
 * reading of ROOTNAME.TXT (0x465550), the arena allocator (0x476bc0,
 * 0x476c00) and everything owned by other groups, called by address. */
#include "../runtime.h"
#include "gamefn.h"
#include "physics.h"

#define g_race (GP(0x4b1960))
#define g_track (GP(0x6c1e30))
#define g_track_info (GP(0x6c1eb0))
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_ai_seed (*(uint32_t *)0x4a7148)
#define g_ai_difficulty_mode (*(int32_t *)0x4a714c)
#define g_difficulty (*(uint32_t *)0x6d0ed0)
#define g_ai_started (*(uint8_t *)0x4b5e4c)
#define g_rivals ((uint8_t *)0x6cf658)       /* per car: the player it races, or 0xff */
#define g_skill_table (*(uint32_t *)0x6cf654) /* a pointer */

#define I32(p, off) (*(int32_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define PTR(p, off) GP((p) + (off))
#define PTR_SET(p, off, v) (U32(p, off) = (uint32_t)(uintptr_t)(v))

/* Not ours. */
#define rejoin_plan GCALL(0x47fbb0, ai_rejoin_plan)
#define ai_plan GCALL(0x4105e0, ai_replan)
#define object_new GCALL(0x460ba0, object_create)
#define path_start GCALL(0x462e30, body_path_start)
#define pool_take GCALL(0x460af0, link_pool_take)
#define heap_take GCALL(0x4445e0, kr_malloc)
#define header_init GCALL(0x460b20, grid_link_init)
#define prim_flag GCALL(0x4270e0, prim_flag2_set)
/* flash_packet_init (0x42a9a0) takes the packet alone; the original
 * pushes a 1 as well, which it does not read. */
typedef void(GAMEAPI *glow_prim_init_fn)(uint8_t *prim, int32_t on);
#define glow_prim_init ((glow_prim_init_fn)(uintptr_t)0x42a9a0)
#define voice_slot_set GCALL(0x472130, voice_slot_reset)
#define fade_set GCALL(0x40afb0, race_end_fade_set)
#define font_load GCALL(0x4432d0, glyph_sheet_pack)
#define block_free GCALL(0x476b40, global_block_free)

/* --- AI --- */

/* 0x47ff40: the difficulty band of race setting +0x579: 0 (0-2), 1 (3-5),
 * 2 (6-9) or 3. */
uint32_t GAMEAPI ai_difficulty_band(void)
{
    uint8_t d = g_race[0x579];

    if (d <= 2)
        return 0;
    if (d <= 5)
        return 1;
    return 2 + (d > 9);
}

/* 0x47ffd0: short racing lines (fewer than 0x15 nodes) marked 1 at +4 are
 * opened (0) except in race modes 3 and 8. */
void GAMEAPI ai_lines_open_short(void)
{
    uint32_t i;

    if (g_race[0x551] == 3 || g_race[0x551] == 8)
        return;
    for (i = 1; i < g_track[0x46]; i++) {
        uint8_t *line = PTR(g_track, 0x60) + i * 0xc;

        if (U16(line, 0) <= 0x14 && line[4] == 1)
            line[4] = 0;
    }
}

/* 0x403750: for each body the AI drives round (12-byte records at 0x6d0a80:
 * body, count, list), the faces (0x14 bytes from model +0x34, model +0x27 of
 * them) with a vertex within 0x18 of the body's highest; the faces go in one
 * shared list of at most 0x3c (0x4b5ea8, count 0x4b5fa4). Faces of kind 4
 * (+0x194 table) are left out. */
void GAMEAPI ai_bodies_tall_faces(void)
{
    uint32_t n = *(uint32_t *)0x4b5f98;
    uint32_t *count = (uint32_t *)0x4b5fa4;
    uint32_t *list = (uint32_t *)0x4b5ea8; /* pointers */
    uint8_t *rec = (uint8_t *)0x6d0a80;

    *count = 0;
    for (; n; n--, rec += 0xc) {
        uint8_t *body = PTR(rec, 0), *edges = PTR(body, 0x28), *model = PTR(body, 0x10);
        uint8_t *verts = PTR(body, 0x24), *face = model + U16(model, 0x34);
        uint32_t k = body[0x10d], found = 0, f;
        int32_t top = -0xffffff;

        PTR_SET(rec, 8, &list[*count]);
        if (k) {
            const int16_t *y = (const int16_t *)(verts + 2);

            for (; k; k--, y += 4)
                if (*y > top)
                    top = *y;
            if (top >= 0) {
                for (f = 0; f < model[0x27]; f++, face += 0x14) {
                    uint32_t a, b;

                    if ((U32(PTR(body, 0x194), f * 4) & 0xf000000) == 0x4000000)
                        continue;
                    a = U32(face, 8) & 0xff;
                    b = U32(face, 8) >> 8 & 0xff;
                    for (; a < b; a++) {
                        uint32_t e = U32(edges, a * 8);
                        int j;

                        for (j = 0; j < 4; j++, e = (uint32_t)((int32_t)e >> 8))
                            if ((e & 0xff) && S16(verts, (e & 0xff) * 8 - 6) > top - 0x18)
                                goto tall;
                    }
                    continue;
                tall:
                    if (*count < 0x3c) {
                        list[*count] = (uint32_t)(uintptr_t)face;
                        found++;
                        (*count)++;
                    }
                }
            }
        }
        U32(rec, 4) = found;
    }
}

static uint32_t ai_random(void)
{
    g_ai_seed = g_ai_seed * 16807 & 0x7fffffff;
    return g_ai_seed;
}

/* 0x411010: with one or two human players, two AI cars (not of kind 6) are
 * picked at random as rivals: the first races player one, the second the
 * last player (0x6cf658, by AI car; 0xff for none). */
void GAMEAPI ai_pick_rivals(void)
{
    uint8_t players[8], ai[8], second;
    uint32_t n = g_race[0x54d], np = 0, na = 0, i, pick;

    U32(g_rivals, 0) = 0xffffffff;
    U16(g_rivals, 4) = 0xffff;
    for (i = 0; i < n; i++) {
        if (g_race[i] == 8) {
            if (g_race[i + 0x1c] != 6)
                ai[na++] = (uint8_t)i;
        } else
            players[np++] = (uint8_t)i;
    }
    if (np == 0 || np > 2 || na == 0)
        return;
    second = np == 1 ? players[0] : players[1];
    pick = (ai_random() >> 15) * na >> 16;
    g_rivals[ai[pick]] = players[0];
    if (na <= 1)
        return;
    na--;
    for (; pick < na; pick++)
        ai[pick] = ai[pick + 1];
    pick = (ai_random() >> 15) * na >> 16;
    g_rivals[ai[pick]] = second;
}

/* A 3-bit field at `shift` lowered by `by`, not below 0. */
static uint32_t field_lower(uint32_t x, int shift, uint32_t by)
{
    int32_t v = (int32_t)((x >> shift & 7) - by);

    return v < 0 ? x & ~(7u << shift) : (x & ~(7u << shift)) | ((uint32_t)v & 7) << shift;
}

/* A 3-bit field at `shift` raised by `by`, not above 6. */
static uint32_t field_raise(uint32_t x, int shift, uint32_t by)
{
    uint32_t v = (x >> shift & 7) + by;

    return v <= 6 ? (x & ~(7u << shift)) | (v & 7) << shift : (x & ~(7u << shift)) | 6u << shift;
}

/* 0x40f3e0: a car's AI skills (3-bit fields of +0x6e4 and +0x6e8) from its
 * character's table (0x4a7b88, 8 bytes per character, race +0x1c by car),
 * eased in difficulty mode 0 and toughened in mode 2. */
void GAMEAPI ai_skill_init(uint8_t *car)
{
    const uint32_t *skills = (const uint32_t *)(uintptr_t)(0x4a7b88 + g_race[U16(car, 0x64e) + 0x1c] * 8);
    uint32_t x;

    g_skill_table = g_ai_difficulty_mode ? 0x4a7c60 : 0x4a7c58;
    U32(car, 0x6e4) = (U32(car, 0x6e4) & ~0x3fffffffu) | (skills[0] & 0x3fffffff);
    U32(car, 0x6e8) = (U32(car, 0x6e8) & ~0xffffffu) | (skills[1] & 0xffffff);
    if (g_ai_difficulty_mode == 0) {
        x = U32(car, 0x6e4);
        x = field_lower(x, 3, 3);
        x = field_lower(x, 6, 2);
        x = field_lower(x, 9, 2);
        x = field_lower(x, 18, 2);
        x = field_lower(x, 21, 1);
        x = field_lower(x, 24, 3);
        x = field_lower(x, 27, 2);
        U32(car, 0x6e4) = x;
        x = U32(car, 0x6e8);
        x = field_lower(x, 0, 3);
        x = field_lower(x, 3, 2);
        x = field_lower(x, 6, 2);
        x = field_lower(x, 12, 3);
        x = field_raise(x, 18, 3);
        x = field_raise(x, 21, g_difficulty == 0 ? 3 : g_difficulty == 1 ? 1 : 0);
        U32(car, 0x6e8) = x;
    }
    if (g_ai_difficulty_mode != 2)
        return;
    x = U32(car, 0x6e8);
    if ((x & 0xe00) < 0x600)
        U32(car, 0x6e8) = (((x & 0xfffffe00) + 0x400) & 0xe00) | (x & ~0xe00u);
    else if ((x & 0xe00) < 0xe00)
        U32(car, 0x6e8) = (((x & 0xfffffe00) + 0x200) & 0xe00) | (x & ~0xe00u);
    x = U32(car, 0x6e4);
    U32(car, 0x6e4) = (x & 0x38) <= 0x18 ? (x & ~8u) | 0x30 : x | 0x38;
}

/* 0x401470: an AI car at the start of a race (slot pointer). */
void GAMEAPI ai_car_race_start(uint8_t *slot)
{
    uint8_t *car = PTR(slot, 0), *ai = car + 0x6b0;
    uint32_t i;

    for (i = 0; i < 0x12; i++)
        U32(ai, i * 4) = 0;
    if (!g_ai_started) {
        uint8_t *race = g_race;

        g_ai_seed = U32(race, 0x618);
        if (race[0x58c] == 1)
            g_ai_difficulty_mode = 2;
        else
            switch (race[0x551]) {
            case 2:
            case 4:
            case 6:
            case 7:
                g_ai_difficulty_mode = 2;
                break;
            default:
                g_ai_difficulty_mode = race[0x585];
            }
        ai_bodies_tall_faces();
        g_difficulty = ai_difficulty_band();
        ai_pick_rivals();
        ai_lines_open_short();
        g_ai_started = 1;
    }
    if (g_track[0x46] > 1)
        rejoin_plan(slot);
    U16(ai, 4) = 0x40;
    U16(ai, 6) = 0;
    ai[8] = 0;
    ai[9] = 0;
    ai[10] = 0;
    ai[11] = 0;
    U32(ai, 0xc) = 0;
    U32(ai, 0x10) = 0;
    U32(ai, 0x14) = 0;
    ai[0x18] = 0;
    ai[0x19] = car[0x64e];
    U16(ai, 0x3c) = 0;
    U16(ai, 0x3e) = 0;
    U16(ai, 0x40) = 0;
    U16(ai, 0x42) = 0;
    U16(ai, 0x44) = 0;
    U16(ai, 0x46) = 0;
    ai[0x1b] = 0;
    ai[0x33] = g_rivals[U16(car, 0x64e)];
    U32(ai, 0x1c) = 0;
    U32(ai, 0x20) = 0;
    U16(ai, 0x24) = 0;
    U16(ai, 0x26) = 0;
    ai[0x28] = 0;
    ai[0x29] = 0;
    ai_skill_init(car);
    ai_plan(car, 1);
}

/* --- the track's fixed lights --- */

#define g_lights ((uint8_t *)0x5d8000) /* 0x40 bytes each */
#define g_light_grid(section) ((uint32_t *)GP(0x5d7de0 + (section) * 4)) /* by section: rows of 8 light keys */

/* 0x441000: a light's cells (up to 4 words: section, first and last row
 * <<4 in bits 8..19 and 20..31 (low 4 bits: column)) are stored at +0x14 and
 * the light's key put in the first free of the 8 places of each row. */
void GAMEAPI light_cells_add(uint32_t index, const uint32_t *cells)
{
    uint8_t *light = g_lights + index * 0x40;
    uint32_t k;

    U32(light, 0x14) = 0;
    U32(light, 0x18) = 0;
    U32(light, 0x1c) = 0;
    U32(light, 0x20) = 0;
    for (k = 0; k < 4; k++, cells++) {
        uint32_t w = *cells, key, first, last;

        if (!w)
            return;
        U32(light, 0x14 + k * 4) = w;
        w = *cells;
        key = (w >> 8 & 0x7ff) | index << 22 | (((w >> 20 & 0xfff) + 1) << 11 & 0x3ff800);
        first = (w >> 8 & 0xfff) >> 4;
        last = (w >> 20 & 0xfff) >> 4;
        if (first <= last) {
            uint32_t *row = g_light_grid(w & 0xff) + first * 8;
            uint32_t rows = last - first + 1;

            do {
                uint32_t j;

                for (j = 0; j < 8; j++)
                    if (row[j] == 0) {
                        row[j] = key;
                        break;
                    }
                row += 8;
            } while (--rows);
        }
    }
}

/* 0x4412e0: light record `index` at `pos` with `colour` (r, g, b, 0x3c),
 * `radius`, the cells it lights and its shape (pointer, 0, count); a light
 * on an object moves the object there. */
void GAMEAPI light_set(uint32_t index, const int32_t *pos, const uint32_t *colour, uint32_t radius,
                       const uint32_t *cells, const uint32_t *shape, uint8_t *object)
{
    uint8_t *light = g_lights + index * 0x40;

    U16(light, 0x24) = 1;
    U16(light, 0x26) = 0;
    I32(light, 0) = pos[0];
    I32(light, 4) = pos[1];
    I32(light, 8) = pos[2];
    U32(light, 0x28) = shape[0];
    U16(light, 0x2c) = ((const uint16_t *)shape)[2];
    U16(light, 0x2e) = ((const uint16_t *)shape)[3];
    PTR_SET(light, 0x30, object);
    if (object) {
        I32(object, 0x34) = pos[0];
        I32(object, 0x38) = pos[1];
        I32(object, 0x3c) = pos[2];
    }
    U32(light, 0xc) = *colour;
    U16(light, 0x10) = (uint16_t)radius;
    U16(light, 0x12) = 2;
    light_cells_add(index, cells);
}

/* 0x441b90: the track's fixed lights (0x20 bytes each: r, g, b, -, -, -,
 * x, y, z (int16), radius, shape, path, path spot, cells at +0x10). A
 * light with a path is carried by a new object following it. */
void GAMEAPI track_lights_init(void)
{
    uint8_t *rec = GP(0x6bfaec);
    uint32_t i;

    for (i = 1; i <= *(uint32_t *)0x6bfae8; i++, rec += 0x20) {
        uint8_t colour[4];
        int32_t pos[3];
        uint32_t shape[2];
        uint8_t *object = 0, *shapes;
        uint8_t kind;

        colour[0] = rec[0];
        colour[1] = rec[1];
        colour[2] = rec[2];
        colour[3] = 0x3c;
        kind = rec[0xd];
        if (rec[0xe]) {
            uint32_t spot = rec[0xf];
            const int16_t *at = (const int16_t *)(PTR(g_track_info, 0xa8) + spot * 8);

            object = object_new(0, 0xa00f, 0x180, 0);
            path_start(object, rec[0xe], spot);
            pos[0] = (int32_t)((uint32_t)(int32_t)at[0] << 16);
            pos[1] = (int32_t)((uint32_t)(int32_t)at[1] << 16);
            pos[2] = (int32_t)((uint32_t)(int32_t)at[2] << 16);
        } else {
            pos[0] = (int32_t)((uint32_t)(int32_t)S16(rec, 6) << 16);
            pos[1] = (int32_t)((uint32_t)(int32_t)S16(rec, 8) << 16);
            pos[2] = (int32_t)((uint32_t)(int32_t)S16(rec, 10) << 16);
        }
        if (kind) {
            uint16_t first;

            shapes = GP(0x6bfb2c);
            first = U16(shapes, kind * 2);
            shape[0] = (uint32_t)(uintptr_t)(shapes + first);
            shape[1] = (uint32_t)(uint16_t)(U16(shapes, kind * 2 + 2) - first) >> 1 << 16;
        } else {
            shape[0] = 0;
            shape[1] = 0;
        }
        light_set(i, pos, (const uint32_t *)colour, rec[0xc], (const uint32_t *)(rec + 0x10), shape, object);
    }
    *(uint32_t *)0x5d7fe8 = *(uint32_t *)0x6bfae8 + 1;
    *(uint32_t *)0x5d7fe0 = *(uint32_t *)0x6bfae8 + 1;
}

/* --- effect pools --- */

/* 0x462360: the 0x200 shrapnel pieces (0x80 bytes, two 0x3c-byte
 * primitives each from +0x10), the free stack 0..0x1ff and the live list. */
void GAMEAPI pieces_init(void)
{
    uint8_t *piece = pool_take(0x10000);
    int32_t n, i;

    *(uint32_t *)0x6b983c = (uint32_t)(uintptr_t)piece;
    for (n = 0x200; n; n--, piece += 0x80) {
        uint8_t *prim = piece + 0x10;
        int k;

        header_init(piece, 5);
        U16(piece, 0x6a) = 0;
        for (k = 0; k < 2; k++, prim += 0x3c) {
            U32(prim, 4) = 0x40404040;
            U32(prim, 0x20) = 0x40404040;
            U32(prim, 0x14) = 0;
            U32(prim, 0x10) = 0;
            U32(prim, 0x30) = 0;
            U32(prim, 0x2c) = 0;
            U32(prim, 0) = (U32(prim, 0) & 0xffffff) | 0xc000000;
            prim[7] = 0xac;
            prim_flag(prim, 1);
        }
    }
    *(uint32_t *)0x6b9840 = heap_take(0x400);
    *(uint32_t *)0x6b9844 = heap_take(0x400);
    for (i = 0; i < 0x200; i++)
        (((int16_t *)GP(0x6b9840)))[i] = (int16_t)i;
    *(uint32_t *)0x6b9848 = 0;
    *(uint32_t *)0x6b984c = 0x1ff;
}

/* 0x481e50: each car's two tyre trails (0x6d4 bytes from +0x14cc: header,
 * then 12 primitives of 0x84 from +0xa4). */
void GAMEAPI tyre_trails_init(void)
{
    uint8_t *slot = g_car_slots;
    int32_t i;

    for (i = 0; i < g_race[0x54d]; i++, slot += CAR_SLOT_SIZE) {
        uint32_t off;

        for (off = 0; off < 0xda8; off += 0x6d4) {
            uint8_t *trail = PTR(slot, 0) + 0x14cc + off, *prim = trail + 0xa4;
            int k;

            U32(trail, 0) = 0;
            U32(trail, 4) = 0;
            U16(trail, 8) = 0;
            trail[10] = 0;
            trail[11] = 0;
            for (k = 0; k < 12; k++, prim += 0x84) {
                U32(prim, 0) = (U32(prim, 0) & 0xffffff) | 0xc000000;
                prim[7] = 0x8c;
                prim_flag(prim, 1);
            }
        }
    }
}

/* 0x490390: car `index`'s glow (0x690 bytes at 0x5cf860, pointed to from
 * +0x68c): a header primitive and 13 sprites of 0x78 bytes taking their
 * texture from the track's texture 0x40 + info +0x2e. */
void GAMEAPI slot_glow_init(uint8_t *car, int32_t index)
{
    uint8_t *glow = (uint8_t *)0x5cf860 + index * 0x690, *texture, *sprite;
    uint32_t a, b;
    uint16_t c, d;
    int k;

    PTR_SET(car, 0x68c, glow);
    glow_prim_init(glow, 1);
    texture = GP(0x6c1e14) + (U16(g_track_info, 0x2e) + 0x40) * 0x10;
    a = U32(texture, 0);
    b = U32(texture, 4);
    c = U16(texture, 8);
    d = U16(texture, 10);
    for (k = 0, sprite = glow + 0x78; k < 13; k++, sprite += 0x78) {
        U32(sprite, 0x20) = a;
        U32(sprite, 0x3c) = b;
        U16(sprite, 0x58) = c;
        U16(sprite, 0x74) = d;
        U32(sprite, 0) = (U32(sprite, 0) & 0xffffff) | 0xc000000;
        sprite[7] = 0x9a;
    }
}

/* 0x46a940: the race countdown (0x4d3da8 = 3, its tick count 0x4d3dac) and
 * its two sprites (0x78 bytes at 0x4d3cb8). */
void GAMEAPI countdown_init(void)
{
    uint8_t *sprite;

    for (sprite = (uint8_t *)0x4d3cb8; sprite < (uint8_t *)0x4d3da8; sprite += 0x78) {
        U32(sprite, 4) = 0xffffffff;
        U32(sprite, 0) = (U32(sprite, 0) & 0xffffff) | 0xc000000;
        sprite[7] = 0x98;
        U32(sprite, 0x60) = 0x40400000;
        U32(sprite, 0x44) = 0x40400000;
        U32(sprite, 0x28) = 0x40400000;
        U32(sprite, 0xc) = 0x40400000;
        U32(sprite, 8) = 0x40;
        sprite[7] |= 2;
    }
    *(uint32_t *)0x4d3dac = 99999;
    *(uint32_t *)0x4d3da8 = 3;
}

/* 0x4430f0: no glints. */
void GAMEAPI glints_clear(void)
{
    *(uint32_t *)0x4c1b18 = 0;
}

/* --- sound --- */

/* 0x428060: the sound device is always there. */
uint32_t GAMEAPI sound_device_ready(void)
{
    return 1;
}

/* 0x471a70: 0 for output `mode` 1 (0xf8 if the device were missing), 1 for
 * mode 0; otherwise (mode - 1) without its low byte. */
uint32_t GAMEAPI sound_output_mode(uint32_t mode)
{
    uint32_t r;

    if ((mode & 0xff) == 0)
        return sound_device_ready();
    r = (mode & 0xff) - 1;
    if (r == 0) {
        r = sound_device_ready();
        if (r == 0)
            return 0xf8;
    }
    return r & 0xffffff00;
}

/* 0x4718d0: the listeners (position, velocity and camera matrix of up to
 * four views) and their voice slots. */
void GAMEAPI sound_listeners_set(uint32_t pos0, uint32_t vel0, uint32_t mat0, uint32_t pos1, uint32_t vel1,
                                 uint32_t mat1, uint32_t pos2, uint32_t vel2, uint32_t mat2, uint32_t pos3,
                                 uint32_t vel3, uint32_t mat3)
{
    *(uint32_t *)0x4d458c = pos0;
    *(uint32_t *)0x4d4570 = vel0;
    *(uint32_t *)0x4d4560 = mat0;
    *(uint32_t *)0x4d4588 = pos1;
    *(uint32_t *)0x4d457c = vel1;
    *(uint32_t *)0x4d456c = mat1;
    *(uint32_t *)0x4d4584 = pos2;
    *(uint32_t *)0x4d4578 = vel2;
    *(uint32_t *)0x4d4568 = mat2;
    *(uint32_t *)0x4d4580 = pos3;
    *(uint32_t *)0x4d4574 = vel3;
    *(uint32_t *)0x4d4564 = mat3;
    voice_slot_set(0);
    voice_slot_set(1);
    voice_slot_set(2);
    voice_slot_set(3);
}

/* 0x4730b0: a listener for each view (race +0x552: 1 or 2 views listen at
 * cars 0 and 1, 3 at 0-2, 4 at 0-3; otherwise car 0 alone), and every car's
 * engine (0x5ff1e0, 0x18 bytes) and tune (0x5ff280, 0x10 bytes) state
 * reset. */
void GAMEAPI race_sounds_init(void)
{
    uint32_t p[4][3] = {{0}}, count = 1, i;
    uint8_t *race = g_race;

    switch (race[0x552]) {
    case 4:
        p[3][0] = (uint32_t)(uintptr_t)(GP(0x6c02dc) + 0x34);
        p[3][1] = (uint32_t)(uintptr_t)(GP(0x6c02dc) + 0x68);
        p[3][2] = (uint32_t)(uintptr_t)(GP(GP(0x6c0304)) + 0xc4);
        count = 2;
        /* fall through */
    case 3:
        p[2][0] = (uint32_t)(uintptr_t)(GP(0x6c0068) + 0x34);
        p[2][1] = (uint32_t)(uintptr_t)(GP(0x6c0068) + 0x68);
        p[2][2] = (uint32_t)(uintptr_t)(GP(GP(0x6c0090)) + 0xc4);
        count++;
        /* fall through */
    case 1:
    case 2:
        p[1][0] = (uint32_t)(uintptr_t)(GP(0x6bfdf4) + 0x34);
        p[1][1] = (uint32_t)(uintptr_t)(GP(0x6bfdf4) + 0x68);
        p[1][2] = (uint32_t)(uintptr_t)(GP(GP(0x6bfe1c)) + 0xc4);
        count++;
        break;
    }
    (void)count; /* passed as a 13th argument, which 0x4718d0 does not read */
    sound_listeners_set((uint32_t)(uintptr_t)(GP(0x6bfb80) + 0x34), (uint32_t)(uintptr_t)(GP(0x6bfb80) + 0x68),
                        (uint32_t)(uintptr_t)(GP(GP(0x6bfba8)) + 0xc4), p[1][0], p[1][1], p[1][2], p[2][0], p[2][1],
                        p[2][2], p[3][0], p[3][1], p[3][2]);
    race = g_race;
    for (i = 0; i < race[0x54d] * 6u; i++)
        ((uint32_t *)0x5ff1e0)[i] = 0;
    for (i = 0; i < race[0x54d]; i++) {
        uint8_t *engine = (uint8_t *)0x5ff1e0 + i * 0x18, *tune = (uint8_t *)0x5ff280 + i * 0x10;

        U16(engine, 0x16) = 1;
        U32(tune, 0) = 0xff;
        engine[7] = 0x7f;
        U32(tune, 4) = 0;
        engine[6] = 0x7f;
        U32(tune, 8) = 0x7f;
        engine[5] = 0x7f;
        U32(tune, 0xc) = 0;
        engine[4] = 0x7f;
    }
    sound_output_mode(1);
}

/* --- text --- */

/* 0x428070: push `item` on the list at `head`. */
int32_t GAMEAPI list_push(uint32_t *head, uint32_t *item)
{
    *item = *head;
    *head = (uint32_t)(uintptr_t)item;
    return 0;
}

#define FONT_A ((uint8_t *)0x6c8180) /* 0x1120 bytes: +0 kind, +4 width, glyphs from +0x1c, map +0x101c */
#define FONT_B ((uint8_t *)0x6c92a0)
#define FONT_C ((uint8_t *)0x6ca3c0) /* a copy of font A */
#define g_fonts ((uint32_t *)0x6cb4e0) /* pointers */
#define g_font_count (*(uint32_t *)0x4be314)

/* 0x42e0c0: the fonts' glyphs (font A only with `small_only` 0) and their
 * character maps: A-Z, a-z, 0-9 and the 50 characters of 0x4ac86c, in
 * that order; then the font table. */
void GAMEAPI fonts_init(int32_t small_only)
{
    uint8_t extra[0x32];
    uint32_t i, n;
    uint32_t base;

    for (i = 0; i < 0x32; i++)
        extra[i] = ((const uint8_t *)0x4ac86c)[i];
    g_font_count = 0;
    if (small_only == 0) {
        font_load(0x4ac860, 0x2e, FONT_A, 0, 0x32, 0x70, 1, 0);
        font_load(0x4ac860, 0x10, FONT_B, 0x36, 100, 0x70, 0, 1);
    } else
        font_load(0x4ac860, 0x10, FONT_B, 0x36, 100, 0x70, 0, 0);
    for (i = 0; i < 0x40; i++)
        U32(FONT_A + 0x101c, i * 4) = 0xffffffff;
    for (i = 0; i < 0x40; i++)
        U32(FONT_B + 0x101c, i * 4) = 0xffffffff;
    n = 0;
    for (i = 'A'; i <= 'Z'; i++, n++) {
        FONT_B[0x101c + i] = (uint8_t)n;
        FONT_A[0x101c + i] = (uint8_t)n;
    }
    for (i = 'a'; i <= 'z'; i++, n++) {
        FONT_B[0x101c + i] = (uint8_t)n;
        FONT_A[0x101c + i] = (uint8_t)n;
    }
    for (i = '0'; i <= '9'; i++, n++) {
        FONT_B[0x101c + i] = (uint8_t)n;
        FONT_A[0x101c + i] = (uint8_t)n;
    }
    for (i = 0; i < 0x32; i++, n++) {
        FONT_B[0x101c + extra[i]] = (uint8_t)n;
        FONT_A[0x101c + extra[i]] = (uint8_t)n;
    }
    U32(FONT_B, 4) = 0x100;
    U32(FONT_A, 4) = 0x100;
    U32(FONT_A, 0) = 3;
    U32(FONT_A, 0x18) = 0;
    U32(FONT_A, 0x10) = 0x14;
    U32(FONT_A, 0x14) = 0;
    U32(FONT_B, 0) = 1;
    for (i = 0; i < 0x447; i++)
        U32(FONT_C, i * 4) = U32(FONT_A, i * 4);
    U32(FONT_C, 0) = 2;
    *(uint32_t *)0x4ac848 = 2;
    base = g_font_count;
    U32(FONT_B, 0x18) = 1;
    g_fonts[base] = (uint32_t)(uintptr_t)FONT_B;
    U32(FONT_B, 0x14) = 1;
    *(uint32_t *)0x4ac84c = 1;
    U32(FONT_B, 0x10) = 5;
    U32(FONT_C, 4) = 0x80;
    *(uint32_t *)0x4ac844 = 3;
    g_font_count = base + 1;
    if (small_only == 0) {
        g_fonts[base + 1] = (uint32_t)(uintptr_t)FONT_C;
        g_fonts[base + 2] = (uint32_t)(uintptr_t)FONT_A;
        g_font_count = base + 3;
        return;
    }
    U32(FONT_B, 4) = 0x140;
}

/* 0x45cfd0: the text needs drawing again. */
void GAMEAPI text_redraw_set(void)
{
    *(uint32_t *)0x4d0774 = 1;
}

/* 0x478ff0: the text system for a race: font B only, its 0x380 glyph
 * sprites (0x84 bytes at 0x5d9b40) and 0x1c text lines (0x50 bytes at
 * 0x5f6940, font first) emptied. */
void GAMEAPI text_reset(void)
{
    uint8_t *sprite = (uint8_t *)0x5d9b40;
    uint32_t plain, i;

    fonts_init(1);
    *(uint32_t *)0x5f6940 = (uint32_t)(uintptr_t)FONT_B;
    plain = *(uint32_t *)0x6ccd84 & 4;
    for (i = 0; i < 0x380; i++, sprite += 0x84) {
        sprite[7] = 0x8c;
        U32(sprite, 0) = (U32(sprite, 0) & 0xffffff) | 0xc000000;
        if (plain == 0)
            sprite[7] |= 2;
    }
    for (i = 0; i < 0x1c; i++)
        ((uint8_t *)0x5f694c)[i * 0x50] = 0;
    text_redraw_set();
}

/* 0x477810: free an image's pixel (+0xc) and palette (+0x18) blocks unless
 * it does not own them (+0x20 bit 0). */
void GAMEAPI image_release(uint8_t *image)
{
    if (U32(image, 0xc) != 0 && !(image[0x20] & 1))
        block_free(U32(image, 0xc));
    if (U32(image, 0x18) != 0 && !(image[0x20] & 1))
        block_free(U32(image, 0x18));
    U32(image, 0xc) = 0;
    U32(image, 0x10) = 0;
    U32(image, 0x14) = 0;
    U32(image, 0x18) = 0;
}

/* --- race state --- */

/* 0x463930: the end of race fade starts off unless setting 0x6cce40 bit 2. */
void GAMEAPI race_fade_init(int32_t unused)
{
    (void)unused;
    fade_set(~*(uint32_t *)0x6cce40 >> 2 & 1);
}

/* 0x463990: the replay state (0x6b8d60) cleared. */
void GAMEAPI replay_state_clear(void)
{
    *(uint32_t *)0x6b8d60 = 0;
    *(uint16_t *)0x6b8d64 = 0;
    *(uint16_t *)0x6b8d66 = 0;
    *(uint32_t *)0x6b8d68 = 0;
}
