/* Race setup (0x46a060), group T: the car setup (0x40e670) and the pools and
 * per-race state set up next to it: bomblets and collapsing buildings
 * (0x46ba00), smoke, shock waves and flashes (the 0x42a610 tree), skid marks
 * (0x46eab0), car targets, text lists, debris and the in-game messages.
 *
 * Many records here are draw packets in the PlayStation style the PC port
 * kept: dword 0 is a link (low 24 bits) and a size in words (high byte),
 * byte 7 a packet kind, then colours, positions and texture words.
 *
 * Still original: allocation (0x4445e0, 0x460af0), object creation and
 * placement (0x460ba0, 0x460b20, 0x465640, 0x40da70), packet set-up
 * (0x4270e0, 0x427a40, 0x46e450, 0x47bc80), the message file reader
 * (0x42e6d0), and callees other groups own. */
#include "../runtime.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"

#define g_race GP(0x4b1960)
#define g_config GP(0x4b2198)
#define g_track GP(0x6c1e30)
#define g_track_info GP(0x6c1eb0)
#define g_textures GP(0x6c1e14) /* 16-byte texture entries */
#define g_palette GP(0x6c1e20)

#define I32(p, off) (*(int32_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define S8(p, off) (*(int8_t *)((p) + (off)))
#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define PTR(p, off) GP((p) + (off))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define SET_PTR(p, off, v) (U32(p, off) = (uint32_t)(uintptr_t)(v))

/* A texture entry of the track's set: index from the track info's base. */
#define TEXTURE(i) (g_textures + ((uint32_t)U16(g_track_info, 0x2e) + (i)) * 0x10)
/* The model for a track model slot (+0x58 slot table, +0x18 models). */
#define TRACK_MODEL(slot) GP(PTR(g_track, 0x18) + S16(PTR(g_track, 0x58), (slot) * 2) * 4)

/* A packet's link word: keeps the link, sets the size in words. */
#define PACKET_SIZE(p, words) (U32(p, 0) = (U32(p, 0) & 0xffffffu) | (uint32_t)(words) << 24)

/* Still original. */
#define level_alloc(size) ((uint8_t *)GMEM(GCALL(0x4445e0, kr_malloc)(size)))
#define object_new GCALL(0x460ba0, object_create)
#define object_place(object, where) GCALL(0x465640, body_init_from_template)(object, (const uint32_t *)(where))
#define grid_unlink_object GCALL(0x4611d0, object_grid_unlink)
#define body_spheres(body, count, table, zero) \
    GCALL(0x419150, body_spheres_set)(body, count, (const uint8_t *)(table), zero)
#define quat_facing_dir GCALL(0x40ee60, quat_facing)
#define car_model_set(car, model, index) \
    GCALL(0x40da70, car_wheels_setup)(car, (uint32_t)(uintptr_t)(model), (uint32_t)(index))
#define slot_digits GCALL(0x439910, slot_time_digits)
#define packet_init GCALL(0x4270e0, prim_flag2_set)
#define marks_packet_init(p, n) GCALL(0x427a40, ot_clear_forward)((uint32_t *)(p), n)
#define trail_init(p, tex) GCALL(0x46e450, car_trail_texture_set)(p, (uint32_t)(tex))
#define glow_packets_init GCALL(0x47bc80, glow_texture_cycle)
#define replay_check GCALL(0x41c190, replay_checksum)
#define message_read(buf, size, section, index) GCALL(0x42e6d0, text_line_get)((uint8_t *)(buf), size, section, index)
#define pieces_alloc GCALL(0x460af0, link_pool_take)
#define piece_init GCALL(0x460b20, grid_link_init)

#define CAR_KIND_AI 8

/* Ours, called through their hooks so that their in-game checks see these
 * calls (the hooks send them straight back to C). */
#define slot_race_reset_hooked GCALL(0x43d280, slot_race_reset)
#define car_shadow_init_hooked GCALL(0x46c220, car_shadow_init)
#define replay_start_hooked GCALL(0x41b8f0, replay_start)
#define car_trails_init_hooked GCALL(0x421dd0, car_trails_init)
#define glow_pick_vertices_hooked GCALL(0x47b280, glow_pick_vertices)

void GAMEAPI car_glow_init(uint8_t *glow, uint8_t *model, int32_t kind);

/* 0x405eb0: the collapsing-building queue is empty. */
void GAMEAPI crumble_queue_clear(void)
{
    *(uint8_t *)0x6d04a2 = 0;
    *(uint8_t *)0x6d04a1 = 0;
    *(uint8_t *)0x6d04a0 = 0;
}

/* 0x40de00: the twin car (+0x260, the second view's copy) gets the car's
 * four wheel records (0xc4 bytes from +0x324) and its own wheel objects. */
void GAMEAPI car_twin_wheels(uint8_t *slot, uint8_t *wheel_model)
{
    uint8_t *car = PTR(slot, 0), *twin = PTR(slot, 0x260);
    int32_t i, k;

    for (i = 0; i < 4; i++) {
        uint8_t *to = twin + 0x324 + i * 0xc4;
        const uint8_t *from = car + 0x324 + i * 0xc4;

        for (k = 0; k < 0x31; k++)
            U32(to, k * 4) = U32(from, k * 4);
        SET_PTR(to, 0x6c, object_new(wheel_model, 0xc00, 0x180, 0));
    }
}

/* 0x40eae0: the car type's handling (0x4a7910, 16 bytes per type): extra
 * rear drive, grip bonus, spin where drive falls off, weight class; with
 * race +0x587 set, players use 0x4a7980 when `special`. Players are a
 * weight class heavier. */
void GAMEAPI car_stats_set(uint8_t *car, int32_t index, int32_t kind, int32_t special)
{
    const uint32_t *stats;
    uint32_t on = U8(g_race, 0x587);

    if (on && U8(g_race, index) == CAR_KIND_AI)
        on = 0;
    if (on && special)
        stats = (const uint32_t *)0x4a7980;
    else
        stats = (const uint32_t *)(uintptr_t)(0x4a7910 + (uint32_t)kind * 0x10);
    U32(car, 0x2274) = stats[0];
    U32(car, 0x2278) = stats[1];
    U32(car, 0x227c) = stats[2];
    U32(car, 0x2280) = stats[3];
    if (U8(g_race, index) != CAR_KIND_AI)
        U32(car, 0x2280) = stats[3] + 1;
}

/* 0x41aa40: the track's special objects list (screens, wrecks...) is empty. */
void GAMEAPI specials_reset(void)
{
    G32(0x6ce738) = (uint32_t)(uintptr_t)level_alloc(0x40);
    *(uint32_t *)0x6ce6fc = 0;
    *(uint32_t *)0x6ce704 = 0;
}

/* 0x41b8f0: start the car's replay record (+0x650). A recorded race's
 * record (race +0x12a0, 0x1e884 each, state 8 at +0x1e880) is used if there
 * is one, else the car itself; mode 4 plays it back, else it records. */
void GAMEAPI replay_start(uint8_t *car, int32_t mode)
{
    uint8_t *rec = car, *channel;
    int32_t i;

    for (i = 0; i < 6; i++)
        if (I32(g_race, 0x1fb20 + i * 0x1e884) == 8) {
            rec = g_race + i * 0x1e884 + 0x12a0;
            break;
        }
    SET_PTR(car, 0x650, rec);
    SET_PTR(rec, 0x4e28, rec + 8);
    U16(rec, 0x4e2c) = 0;
    U16(rec, 0x4e2e) = 0;
    U16(rec, 4) = U16(car, 0x64e);
    for (i = 0; i < 3; i++) {
        channel = rec + (i == 1 ? 0x14c24 : i == 2 ? 0x19a50 : 0x4e30);
        if (mode == 4) {
            U16(channel, 0xa) = 0;
        } else {
            U16(channel, 8) = 0;
            I32(channel, 4) = (i == 0 ? 65000 : 20000) - 0x10;
        }
        U8(channel, 0) = 0;
        U8(channel, 1) = 0;
        U8(channel, 2) = 0;
    }
    if (mode == 4) {
        SET_PTR(rec, 0x4e28, rec + 8);
        U16(rec, 0x1e87c) = U16(rec, 2);
        replay_check(rec, 1);
        U32(rec, 0x1e880) = 5;
    } else {
        U16(rec, 0) = 0;
        U16(rec, 2) = 0;
        U32(rec, 0x1e880) = 2;
    }
}

/* 0x421dd0: the car's four trail packets (+0x2290, 0x78 apart) get the
 * trail texture (track texture 0x96). */
void GAMEAPI car_trails_init(uint8_t *car)
{
    const uint8_t *t;
    uint8_t *p = car + 0x22cc;
    int32_t i;

    U32(car, 0x228c) = 0;
    t = TEXTURE(0x96);
    for (i = 0; i < 4; i++, p += 0x78) {
        U32(p, -0x1c) = U32(t, 0);
        U32(p, 0) = U32(t, 4);
        U16(p, 0x1c) = U16(t, 8);
        U16(p, 0x38) = U16(t, 0xa);
    }
}

/* 0x421e30: the car's three skid-mark packets (+0x1408, 0x38 apart). */
void GAMEAPI car_marks_init(uint8_t *car)
{
    uint8_t *p = car + 0x1408;
    int32_t i;

    SET_PTR(car, 0x14b0, p);
    for (i = 0; i < 3; i++, p += 0x38)
        marks_packet_init(p, 0xe);
}

/* 0x4279d0: square root from the table at 0x4ac204, for the flare sizes:
 * the same as fix_sqrt_table (0x427960) but without its >> 12. */
uint32_t GAMEAPI fix_sqrt_b(int32_t x)
{
    int32_t even, scale, v;

    *(uint32_t *)0x6cccdc = (uint32_t)x;
    count_leading_zeros();
    if (*(int32_t *)0x6ccce0 == 32)
        return 0;
    even = *(int32_t *)0x6ccce0 & ~1;
    scale = (0x13 - even) >> 1;
    if (even - 0x18 >= 0)
        v = (int32_t)((uint32_t)x << ((even - 0x18) & 31));
    else
        v = x >> ((0x18 - even) & 31);
    v = *(const uint16_t *)(uintptr_t)(0x4ac204 + (uint32_t)(v - 0x40) * 2);
    if ((int8_t)scale < 0)
        return (uint32_t)v >> ((0u - (uint32_t)scale) & 31);
    return (uint32_t)v << (scale & 31);
}

/* 0x427ad0: a packet's texture-page word (+4). */
uint32_t GAMEAPI packet_tpage_set(uint8_t *packet, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t v = (a & 0xff00) | (b & 0xff0000) | (c & 0x3f) | 0xb4000000u;

    U32(packet, 4) = v;
    return v;
}

/* 0x427b00: the blend bits of a texture page (callers pass four values). */
uint32_t GAMEAPI packet_blend_bits(uint32_t blend)
{
    return blend & 0x3f;
}

/* Packets of 0x78 bytes in `count` rows of `per_row` pairs; the second of
 * each pair is `second` bytes on. Smoke (0x42a8f0) and shock waves
 * (0x42c2b0). */
static void pair_packets_init(uint32_t pool, uint32_t size, uint32_t row, int32_t per_row, uint32_t first,
                              uint32_t second)
{
    uint32_t r;
    int32_t i;

    G32(pool) = (uint32_t)(uintptr_t)level_alloc(size);
    for (r = 0; (int32_t)r < (int32_t)size; r += row) {
        uint32_t at = r;

        U32(GP(pool) + r, 0xc) = 0;
        for (i = 0; i < per_row; i++, at += 0x78) {
            uint8_t *p = GP(pool) + at + first;

            U8(GP(pool) + at, first + 7) = 0x98;
            PACKET_SIZE(p, 0xc);
            packet_init(p, 1);
            p = GP(pool) + at + second;
            U8(GP(pool) + at, second + 7) = 0x98;
            PACKET_SIZE(p, 0xc);
            packet_init(p, 1);
        }
    }
}

/* 0x42a8f0: the smoke clouds (*0x6ccb54: 5 rows of 32 packet pairs). */
void GAMEAPI smoke_clouds_init(void)
{
    pair_packets_init(0x6ccb54, 0x96dc, 0x1e2c, 0x20, 0x2c, 0xf2c);
}

/* 0x42a9a0: a full-screen flash packet. */
void GAMEAPI flash_packet_init(uint8_t *p)
{
    U32(p, 8) = 0x40;
    U16(p, 0x3e) = 0xffff;
    U8(p, 7) = 0x9a;
    PACKET_SIZE(p, 0xc);
    U32(p, 0x60) = 0x4000a3d7;
    U32(p, 0x44) = 0x4000a3d7;
    U32(p, 0x28) = 0x4000a3d7;
    U32(p, 0xc) = 0x4000a3d7;
}

/* 0x42a9e0: the four flash packets, and no screen shake. */
void GAMEAPI flashes_init(void)
{
    flash_packet_init((uint8_t *)0x6cc960);
    flash_packet_init((uint8_t *)0x6cc9d8);
    flash_packet_init((uint8_t *)0x6cca60);
    flash_packet_init((uint8_t *)0x6ccad8);
    *(uint16_t *)0x6ccb60 = 0;
}

/* 0x42c1d0: palette entries 0x3f0..0x3ff are a grey ramp (8..0x80, 0x3c). */
void GAMEAPI palette_ramp_init(void)
{
    uint32_t i;
    uint8_t c = 8;

    for (i = 0xfc0; i < 0x1000; i += 4, c += 8) {
        uint8_t *e = g_palette + i;

        e[0] = c;
        e[1] = c;
        e[2] = c;
        e[3] = 0x3c;
    }
}

/* 0x42c200: the flares (*0x4be1a0, 128 packets of 0x78, two halves) and
 * their size table (0x4be158: 20 sizes). */
void GAMEAPI flares_init(void)
{
    int32_t at, x;
    int16_t *size;

    *(uint32_t *)0x4be1a8 = 0;
    G32(0x4be1a0) = (uint32_t)(uintptr_t)level_alloc(0x3c00);
    G32(0x4be1a4) = (uint32_t)(uintptr_t)(GP(0x4be1a0) + 0x1e00);
    for (at = 0; at < 0x3c00; at += 0x78) {
        U32(GP(0x4be1a0) + at, 4) = 0x80808080;
        U16(GP(0x4be1a0) + at, 0x3e) = (uint16_t)packet_blend_bits(2);
    }
    for (x = 0, size = (int16_t *)0x4be158; (uint32_t)(uintptr_t)size < 0x4be180; x += 0x200000, size++) {
        int32_t s = (int32_t)fix_sqrt_b(x / 0x2800) * 0x2800;

        *size = (int16_t)((s + (int32_t)((uint32_t)(s >> 31) & 0xfff)) >> 12);
    }
}

/* 0x42c2b0: the shock waves (*0x6cc800: 3 rows of 16 packet pairs). */
void GAMEAPI shock_waves_init(void)
{
    pair_packets_init(0x6cc800, 0x2d3c, 0xf14, 0x10, 0x14, 0x794);
}

/* 0x434380: the position markers: *0x4be374 (128 packets of 0x78, 4 views
 * of 16 pairs, textures 0x14/0x15 alternating) and *0x4be37c (4 x 17
 * records of 0x10); each view's state at 0x6c8100 (0x18 apart). */
void GAMEAPI markers_init(void)
{
    uint8_t *p, *q, *view = (uint8_t *)0x6c8104;
    int32_t v, i, k;

    G32(0x4be374) = (uint32_t)(uintptr_t)level_alloc(0x3c00);
    G32(0x4be37c) = (uint32_t)(uintptr_t)level_alloc(0x440);
    p = GP(0x4be374);
    q = GP(0x4be37c);
    for (v = 0; v < 4; v++, view += 0x18) {
        U16(view, 2) = 0xff;
        U16(view, -4) = 0;
        U16(view, -2) = 0;
        U16(view, 0) = 0;
        for (i = 0; i < 0x10; i++) {
            for (k = 0; k < 2; k++, p += 0x78) {
                const uint8_t *t;
                uint16_t w;

                U8(p, 7) = 0x98;
                PACKET_SIZE(p, 0xc);
                t = TEXTURE(0x14 + (i & 1));
                U32(p, 0x20) = U32(t, 0);
                U32(p, 0x3c) = U32(t, 4);
                U16(p, 0x58) = U16(t, 8);
                U16(p, 0x74) = U16(t, 0xa);
                w = U16(p, 0x3c); /* the two texture coordinates swap */
                U16(p, 0x3c) = U16(p, 0x58);
                U16(p, 0x58) = w;
                packet_init(p, 1);
                U8(p, 4) = 0x7f;
                U8(p, 5) = 0x7f;
                U8(p, 6) = 0x7f;
            }
            U16(q, 0) = 0;
            U16(q, 2) = 0;
            U16(q, 4) = 0;
            U16(q, 8) = 0;
            U16(q, 0xa) = 0;
            U16(q, 0xc) = 0;
            q += 0x10;
        }
        U16(q, 0) = 0;
        U16(q, 2) = 0;
        U16(q, 4) = 0;
        U16(q, 8) = 0;
        U16(q, 0xa) = 0;
        U16(q, 0xc) = 0;
        q += 0x10;
    }
}

/* 0x47ad30: the racing-line node nearest the car, over every line but the
 * first (track +0x60, 0xc each: +0 node count, +8 nodes of 0x10 with int16
 * x, y, z): slot +0x64 the line, +0x68 the node. */
void GAMEAPI slot_find_line(uint8_t *slot)
{
    const uint8_t *car = PTR(slot, 0), *line, *best = 0;
    int32_t x = I32(car, 0x34) >> 16, y = I32(car, 0x38) >> 16, z = I32(car, 0x3c) >> 16;
    int32_t lines = U8(g_track, 0x46) - 1, best_node = 0, best_d = 0x7fffffff, n, j;

    if (lines <= 0) {
        SET_PTR(slot, 0x64, 0);
        I32(slot, 0x68) = 0;
        return;
    }
    for (line = PTR(g_track, 0x60) + 0xc; lines; lines--, line += 0xc) {
        const uint8_t *node = PTR(line, 8);

        n = U16(line, 0);
        for (j = 0; j < n; j++, node += 0x10) {
            uint32_t dx = (uint32_t)(S16(node, 0) - x), dy = (uint32_t)(S16(node, 2) - y);
            uint32_t dz = (uint32_t)(S16(node, 4) - z);
            int32_t d = (int32_t)(dz * dz + dy * dy + dx * dx);

            if (d < best_d) {
                best_node = j;
                best = line;
                best_d = d;
            }
        }
    }
    SET_PTR(slot, 0x64, (uint8_t *)best);
    I32(slot, 0x68) = best_node;
}

/* 0x43d280: a slot's race state at the start: place = index + 1, no
 * checkpoints or laps, the race's lap times (race +0x24 + index * 0xdc)
 * empty, the nearest racing line, the track's first checkpoints. */
void GAMEAPI slot_race_reset(uint8_t *slot)
{
    uint16_t index = U16(PTR(slot, 0), 0x64e);
    int32_t i;

    U8(slot, 0x2c) = 0;
    U8(slot, 0x2e) = 0;
    U8(slot, 0x2d) = (uint8_t)(index + 1);
    I32(slot, 0x3c) = -1;
    I32(slot, 0x40) = -1;
    I32(slot, 0x44) = -1;
    I32(slot, 0x48) = -1;
    if (!U8(g_race, 0x58c))
        for (i = 0; i < 0xd; i++)
            U32(g_race, (uint32_t)index * 0xdc + 0x24 + i * 4) = 0x595999;
    I32(slot, 0x5c) = -1;
    I32(slot, 0x60) = -1;
    I32(slot, 0x34) = 0;
    U8(slot, 0x2f) = 0;
    U8(PTR(slot, 0), 0x661) = 1;
    U8(PTR(slot, 0), 0x65d) = 0;
    slot_find_line(slot);
    U8(slot, 0x30) = 0;
    U8(slot, 0x31) = (uint8_t)(U8(g_track, 0x44) - 1);
    U8(slot, 0x32) = (uint8_t)*(uint32_t *)PTR(g_track, 0x90);
    U8(slot, 0x33) = (uint8_t)((uint32_t *)PTR(g_track, 0x90))[U8(slot, 0x31)];
}

/* 0x441d70: no lights queued. */
void GAMEAPI lights_reset(void)
{
    *(uint32_t *)0x4beb88 = 0;
}

/* 0x449cd0: whether player `i` (0..3) is switched on in the configuration
 * (+0x1a2 of each 0x90-byte player record). */
int32_t GAMEAPI config_player_on(int32_t i)
{
    if (i < 0 || i >= 4)
        return 0;
    return U8(g_config, i * 0x90 + 0x1a2) != 0;
}

/* 0x465270: the in-game messages (the file's PC_IN_GAME_MESSAGES section)
 * are packed into race +0xb20..+0x12a0, with pointers at 0x4d1098 (43
 * read, the rest pointing at a " " after them). */
void GAMEAPI messages_load(void)
{
    uint8_t *to = g_race + 0xb20;
    uint32_t table = 0x4d1098; /* game pointers */
    int32_t i = 0;
    char text[0x100];

    for (; table < 0x4d1144; table += 4, i++) {
        uint32_t n = 0, k;

        if (message_read(text, 0x100, (const char *)0x4b1678, i) == -1)
            *(uint16_t *)text = *(const uint16_t *)0x4a8064;
        while (text[n])
            n++;
        n++;
        if (to + n < g_race + 0x12a0) {
            for (k = 0; k < n; k++)
                to[k] = (uint8_t)text[k];
            G32(table) = (uint32_t)(uintptr_t)to;
            to += n;
        } else {
            G32(table) = (uint32_t)(uintptr_t)(g_race + 0xb20);
        }
    }
    to[0] = ' ';
    to[1] = 0;
    for (; i < 0x40; i++)
        G32(0x4d1098 + i * 4) = (uint32_t)(uintptr_t)to;
}

/* 0x4698f0: in race mode 4, car 0 gets a line-up of the six car models
 * (+0x2470: the objects, their glows from +0x18, 0x300 apart, the kind at
 * +0x1218) and the four name panels (*0x6b8cd8, 0x298 each, copied to
 * *0x6b8cdc). */
void GAMEAPI lineup_setup(uint8_t *car, uint8_t kind, const uint16_t *models, const uint16_t *glow_models)
{
    uint8_t *set, *panel;
    int32_t x0, top, k;
    uint32_t i;

    I32(car, 0x2470) = -1;
    if (U8(g_race, 0x551) != 4 || U16(car, 0x64e) != 0)
        return;
    set = level_alloc(0x121c);
    SET_PTR(car, 0x2470, set);
    G32(0x4b1948) = (uint32_t)(uintptr_t)(car);
    U8(set, 0x1218) = kind;
    for (i = 0; i < 6; i++) {
        uint8_t *glow_model = TRACK_MODEL(glow_models[i]);

        SET_PTR(set, i * 4, object_new(TRACK_MODEL(models[i]), 0x1c03, 0x180, 0));
        car_glow_init(set + 0x18 + i * 0x300, glow_model, i);
    }
    x0 = *(int16_t *)0x6c7df0;
    top = (int32_t)((uint32_t)(*(int32_t *)0x4acc38 * 256 / 400 * 25) >> 8) + *(int16_t *)0x6c7df2;
    G32(0x6b8cd8) = (uint32_t)(uintptr_t)level_alloc(0x14c0);
    G32(0x6b8cdc) = (uint32_t)(uintptr_t)(GP(0x6b8cd8) + 0xa60);
    for (k = 0; k < 4; k++) {
        const uint8_t *t = TEXTURE(0x99 + k);
        uint8_t *sub;
        uint32_t j;

        panel = GP(0x6b8cd8) + k * 0x298;
        U16(panel, 0x294) = 0;
        U16(panel, 0x296) = 0x1000;
        U32(panel, 0xc) = U32(t, 0);
        U16(panel, 0x10) = (uint16_t)(U8(t, 0xa) - U8(t, 0) + 1);
        U32(panel, 4) = 0;
        U8(panel, 7) = 0x46;
        U16(panel, 0x12) = (uint16_t)(U8(t, 0xb) - U8(t, 1) + 1);
        U16(panel, 8) = (uint16_t)x0;
        U16(panel, 0xa) = (uint16_t)(k + top + k * 32);
        PACKET_SIZE(panel, 4);
        U16(panel, 0xe) = U16(t, 6);
        for (j = 0, sub = panel; j < 0x10; j++, sub += 0x28) {
            const uint8_t *letter = TEXTURE(0x84);
            uint16_t px, py;

            U32(sub, 0x20) = U32(letter, 0);
            U32(sub, 0x28) = U32(letter, 4);
            U16(sub, 0x30) = U16(letter, 8);
            U16(sub, 0x38) = U16(letter, 0xa);
            px = (uint16_t)(j * 10 + U16(panel, 8));
            U16(sub, 0x1c) = px;
            I32(sub, 0x18) = 0;
            py = (uint16_t)(U16(panel, 0xa) + 0x12);
            U16(sub, 0x1e) = py;
            U16(sub, 0x2c) = px;
            U16(sub, 0x26) = py;
            U16(sub, 0x24) = (uint16_t)(px + 8);
            U16(sub, 0x2e) = (uint16_t)(py + 8);
            U16(sub, 0x36) = (uint16_t)(py + 8);
            U16(sub, 0x34) = (uint16_t)(px + 8);
            U8(sub, 0x1b) = 0x2e;
            PACKET_SIZE(sub + 0x14, 9);
        }
    }
    {
        uint32_t *from = (uint32_t *)GP(0x6b8cd8), *to = (uint32_t *)GP(0x6b8cdc);
        int32_t n = (int32_t)((uint32_t)(uintptr_t)to - (uint32_t)(uintptr_t)from) >> 2;

        for (; n; n--)
            *to++ = *from++;
    }
}

/* 0x46ba00: the bomblets (*0x600664, 36 x 8 bytes, their objects at
 * 0x600680: 4 big, 8 middle, 24 small, models 3/2/1) and the collapsing
 * buildings (*0x6d04a4, 4 x 0xa8, each with a 0x800-byte buffer at +0x8c). */
void GAMEAPI bomblets_setup(void)
{
    uint32_t object = 0x600680; /* game pointers */
    uint8_t *buffers, *o;
    int32_t i;

    crumble_queue_clear();
    *(uint32_t *)0x4d3dd8 = 0;
    *(uint32_t *)0x4d3ddc = 0;
    G32(0x600664) = (uint32_t)(uintptr_t)level_alloc(0x120);
    G32(0x6d04a4) = (uint32_t)(uintptr_t)level_alloc(0x2a0);
    buffers = level_alloc(0x2000);
    for (i = 0; object < 0x600710; object += 4, i++) {
        ((uint8_t *)0x600640)[i] = 0;
        if (object < 0x600690) {
            o = object_new(TRACK_MODEL(3), 0x1c07, 0x180, 0);
            G32(object) = (uint32_t)(uintptr_t)o;
            object_place(o, (const void *)0x4b1b38);
        } else if (object < 0x6006b0) {
            o = object_new(TRACK_MODEL(2), 0x1c07, 0x180, 0);
            G32(object) = (uint32_t)(uintptr_t)o;
            object_place(o, (const void *)0x4b1a60);
        } else {
            o = object_new(TRACK_MODEL(1), 0x1c07, 0x180, 0);
            G32(object) = (uint32_t)(uintptr_t)o;
            object_place(o, (const void *)0x4b1988);
        }
        grid_unlink_object(GP(object));
    }
    for (i = 0; i < 0x48; i++)
        U32(GP(0x600664), i * 4) = 0;
    for (i = 0; i < 0xa8; i++)
        U32(GP(0x6d04a4), i * 4) = 0;
    for (i = 0; i < 0x2a0; i += 0xa8, buffers += 0x800)
        SET_PTR(GP(0x6d04a4), i + 0x8c, buffers);
}

/* 0x46c220: the car's shadow (car +0x720): added to the shadow list
 * (0x4d3de4, count 0x4d3dfc) with its two packets (texture 0x41). */
void GAMEAPI car_shadow_init(uint8_t *shadow)
{
    const uint8_t *t = TEXTURE(0x41);
    uint8_t *p = shadow + 0x60, *page = shadow + 0x97c;
    int32_t i, n;

    U8(shadow, 6) = 0;
    U32(shadow, 0x128) = 0;
    U32(shadow, 0x970) = 0;
    U32(shadow, 0x974) = 0xffff0001;
    U32(shadow, 0x978) = 0;
    n = *(int32_t *)0x4d3dfc; /* read once: the seventh entry is the count */
    G32(0x4d3de4 + n * 4) = (uint32_t)(uintptr_t)shadow;
    *(int32_t *)0x4d3dfc = n + 1;
    for (i = 0; i < 2; i++, p += 0x84, page += 8) {
        U32(p, -0x20) = U32(t, 0);
        U32(p, 0) = U32(t, 4);
        U16(p, 0x20) = U16(t, 8);
        U16(p, 2) = (uint16_t)((U16(p, 2) & 0xffbf) | 0x80);
        U16(p, 0x40) = U16(t, 0xa);
        PACKET_SIZE(p - 0x40, 0xc);
        U8(p, -0x39) = 0x8e;
        packet_tpage_set(page, 1, 1, packet_blend_bits(2) & 0xffff);
    }
}

/* 0x46e110: the car takes its animated-texture record (0x6002e0, 0x84 each)
 * when its index is 0..5. */
uint8_t *GAMEAPI car_texture_anim_attach(uint8_t *slot)
{
    uint8_t *car = PTR(slot, 0), *rec;
    uint16_t index = U16(car, 0x64e);

    rec = (uint8_t *)0x6002e0 + (uint32_t)index * 0x84;
    if (index >= 6)
        return (uint8_t *)-1;
    SET_PTR(rec, 0x64, car);
    SET_PTR(PTR(slot, 0), 0x6ac, rec);
    return rec;
}

/* 0x46e3e0: the car's tyre trails (+0x10ac: 0x78 packets of 0x9c, then a
 * second half at +0x2490). */
void GAMEAPI car_trail_init(uint8_t *car)
{
    uint8_t *buffer = level_alloc(0x4920), *p;
    int32_t i;

    SET_PTR(car, 0x10ac, buffer);
    SET_PTR(car, 0x10b0, buffer + 0x2490);
    U8(car, 0x1104) = 0;
    for (i = 0, p = buffer + 0xc; i < 0x78; i++, p += 0x9c) {
        U8(p, 7) = 0x8e;
        PACKET_SIZE(p, 0xc);
    }
    trail_init(car + 0x10ac, 0x56);
    U8(car, 0x1105) = 1;
}

/* 0x46eab0: the skid marks (*0x6002d4: 300 records of 0x124 with two
 * packets each, 150 at configuration +0x2c != 2) and their colours. */
void GAMEAPI skid_marks_init(void)
{
    const uint8_t *t = TEXTURE(0x8f);
    uint32_t i, n;
    int32_t k;

    *(uint32_t *)0x4d44bc = *(uint8_t *)0x6ccd84 & 1 ? 0x8ea0a0a0u : 0x8e000000u;
    *(uint32_t *)0x4d44b8 = *(uint8_t *)0x6ccd84 & 1 ? 0x8ea0a0a0u : 0x8e9f9f9fu;
    n = U8(g_config, 0x2c) != 2 ? 150 : 300;
    *(uint32_t *)0x4b1c68 = n;
    G32(0x6002d4) = (uint32_t)(uintptr_t)level_alloc(n * 0x124);
    G32(0x6002d8) = (uint32_t)(uintptr_t)(GP(0x6002d4));
    G32(0x6002d0) = (uint32_t)(uintptr_t)(GP(0x6002d4) + n * 0x124);
    for (i = 0; i < *(uint32_t *)0x4b1c68; i++) {
        uint8_t *rec = GP(0x6002d4) + i * 0x124, *p = rec + 0x5c;

        U32(rec, 8) &= 0xffff7fffu;
        for (k = 0; k < 2; k++, p += 0x84) {
            U32(p, -0x20) = U32(t, 0);
            U32(p, 0) = U32(t, 4);
            U16(p, 0x20) = U16(t, 8);
            U8(p, 2) |= 0x80;
            U16(p, 0x40) = U16(t, 0xa);
            U32(p, -0x3c) = 0xa0a0a0a0;
            U32(p, -0x1c) = 0xa0a0a0a0;
            U32(p, 4) = 0xa0a0a0a0;
            U32(p, 0x24) = 0xa0a0a0a0;
            PACKET_SIZE(p - 0x40, 0xc);
            U8(p, -0x39) = 0x8c;
        }
    }
}

/* 0x475d50: a body's four attached points (+0x180, 0x1c each, from
 * `points`: int16 position, int16 offset), `reach` at +0x320; the model's
 * radius (*0x6ce740, 0x48 per model, +0x40) grows to reach them. */
void GAMEAPI body_points_init(uint8_t *body, int32_t unused, const int16_t *points, int32_t reach)
{
    int16_t *p = (int16_t *)(body + 0x180);
    int32_t i, radius = 0;
    uint8_t *model;

    (void)unused;
    I32(body, 0x320) = reach;
    for (i = 0; i < 4; i++, p += 0xe, points += 0xe) {
        int32_t x, y, z, r;

        p[0] = points[0];
        p[1] = points[1];
        p[2] = points[2];
        p[4] = points[4];
        p[5] = points[5];
        p[6] = points[6];
        *(int32_t *)(p + 8) = i;
        x = (points[4] * reach >> 8) + p[0];
        y = (points[5] * reach >> 8) + p[1];
        z = (points[6] * reach >> 8) + p[2];
        x = fix_mul8(x, x);
        y = fix_mul8(y, y);
        z = fix_mul8(z, z);
        r = (int32_t)(fix_sqrt_table(z + y + x) << 4);
        if (r > radius)
            radius = r;
    }
    model = GP(0x6ce740) + (uint32_t)U8(body, S8(body, 0x10c) + 0x140) * 0x48;
    if (I32(model, 0x40) < radius)
        I32(model, 0x40) = radius;
}

/* 0x478340: no car targets (0x5f7220). */
void GAMEAPI car_targets_reset(void)
{
    *(uint32_t *)0x5f72a4 = 0;
    *(uint32_t *)0x5f72a0 = 0;
}

/* 0x478350: the slot has no homing target. */
void GAMEAPI slot_target_reset(uint8_t *slot)
{
    U32(slot, 0x8c) = 0;
    U16(slot, 0x92) = 0;
    U16(slot, 0x90) = 0;
}

/* 0x478400: add a target (body, kind) for homing weapons. */
void GAMEAPI car_target_add(uint8_t *body, uint32_t kind)
{
    int32_t n = *(int32_t *)0x5f72a0;

    G32(0x5f7220 + n * 8) = (uint32_t)(uintptr_t)body;
    ((uint32_t *)0x5f7224)[n * 2] = kind;
    *(int32_t *)0x5f72a0 = n + 1;
}

/* 0x479af0: the four text lists (0x5d9454..0x5d9460) are empty. */
void GAMEAPI text_lists_reset(void)
{
    *(uint32_t *)0x5d9460 = 0x5d9960;
    *(uint32_t *)0x5d9960 = 0;
    *(uint32_t *)0x5d97c0 = 0;
    *(uint32_t *)0x5d9620 = 0;
    *(uint32_t *)0x5d9480 = 0;
    *(uint32_t *)0x5d945c = 0x5d97c0;
    *(uint32_t *)0x5d9454 = 0x5d9620;
    *(uint32_t *)0x5d9458 = 0x5d9480;
}

/* 0x47ad00: the car is on no special track section. */
void GAMEAPI car_section_reset(uint8_t *slot)
{
    uint8_t *car = PTR(slot, 0);

    U8(car, 0x661) = 0;
    U16(car, 0x690) = 0;
    U16(car, 0x692) = 0xffff;
    U16(car, 0x694) = 0xffff;
}

/* 0x47b1e0: a glow model's vertices (+0x24, 8 bytes each, count +0x10d)
 * are moved by the car type's offset (0x4b2480) once per type (0x5d92a0). */
void GAMEAPI glow_model_offset(uint8_t *model, int32_t kind)
{
    uint32_t n = U8(model, 0x10d);
    int16_t *v = (int16_t *)PTR(model, 0x24);
    const int16_t *offset = (const int16_t *)GP(0x4b2480 + kind * 4);

    if (((uint8_t *)0x5d92a0)[kind])
        return;
    ((uint8_t *)0x5d92a0)[kind] = 1;
    for (; n; n--, v += 4) {
        v[0] = (int16_t)(v[0] + offset[0]);
        v[1] = (int16_t)(v[1] + offset[1]);
        v[2] = (int16_t)(v[2] + (int16_t)(offset[2] - 2));
    }
}

/* 0x47b240: the first face (model +0x28, 8 bytes, count at +0x10 -> +6)
 * whose 10-bit value is below -250, or 0. */
uint32_t GAMEAPI glow_first_low_face(const uint8_t *glow)
{
    const uint8_t *object = PTR(glow, 4);
    const uint32_t *face = (const uint32_t *)(PTR(object, 0x28) + 4);
    uint32_t n = U16(PTR(object, 0x10), 6), i;

    for (i = 0; i < n; i++, face += 2)
        if ((int16_t)((int32_t)(*face << 22) >> 22) < -250)
            return i;
    return 0;
}

/* 0x47b280: the glow's vertices: those below the top (+0x2e0, count
 * +0x2dc; +0x2e4 flags those more than 3 above the bottom) and the faces
 * that use a bottom one (up to 8 bytes at +0x2f6). A random phase at
 * +0x2f4. The original keeps the lists on its stack: more than 8 picked
 * vertices, or 2 bottom ones, overrun its frame. */
void GAMEAPI glow_pick_vertices(uint8_t *glow)
{
    const uint8_t *object = PTR(glow, 4), *verts;
    uint32_t n = U8(object, 0x10d), i, picked = 0, low = 0, faces, j;
    int32_t top = -0x7fff, bottom = 0x7fff;
    uint8_t pick[256];
    uint32_t lows[256];
    uint8_t *out;
    uint32_t seed = *(uint32_t *)0x4b2430 * 16807u & 0x7fffffff;

    *(uint32_t *)0x4b2430 = seed;
    U8(glow, 0x2f5) = 0;
    U8(glow, 0x2f4) = (uint8_t)(seed >> 15);
    verts = PTR(object, 0x24);
    for (i = 0; i < n; i++) {
        int32_t z = S16(verts, i * 8 + 4);

        if (z > top)
            top = z;
        if (z < bottom)
            bottom = z;
    }
    bottom += 3;
    verts = PTR(PTR(glow, 4), 0x24);
    for (i = 0; i < n; i++) {
        int32_t z = S16(verts, i * 8 + 4);

        if (z < top) {
            U8(glow, 0x2e4 + picked) = z > bottom;
            pick[picked++] = (uint8_t)i;
        }
    }
    U8(glow, 0x2dc) = (uint8_t)picked;
    U16(glow, 2) = (uint16_t)((top < 0 ? 0u - (uint32_t)top : (uint32_t)top) + 0x10);
    for (i = 0; i < picked; i++)
        U8(glow, 0x2e0 + i) = pick[i];
    verts = PTR(PTR(glow, 4), 0x24);
    for (i = 0; i < picked; i++) {
        uint32_t v = U8(glow, 0x2e0 + i);

        if (S16(verts, v * 8 + 4) <= bottom)
            lows[low++] = v;
    }
    out = glow + 0x2f6;
    U32(glow, 0x2f6) = 0xffffffff;
    U32(glow, 0x2fa) = 0xffffffff;
    object = PTR(glow, 4);
    faces = U16(PTR(object, 0x10), 6);
    for (j = 0; j < faces; j++) {
        uint32_t corners = *(const uint32_t *)(PTR(object, 0x28) + j * 8), k, c;

        for (k = 0; k < 4; k++, corners >>= 8) {
            c = corners & 0xff;
            if (c) {
                uint32_t m;

                for (m = 0; (int32_t)m < (int32_t)low; m++)
                    if (c - 1 == lows[m])
                        break;
                if ((int32_t)m < (int32_t)low) {
                    *out++ = (uint8_t)j;
                    break;
                }
            }
        }
    }
}

/* 0x47bc60: the glow's packets start empty. */
void GAMEAPI glow_reset(uint8_t *glow)
{
    U16(glow, 0xa) = 0;
    U16(glow, 8) = 0;
    glow_packets_init(glow);
}

/* 0x47b160: an engine glow (car +0x1108, 0x300 bytes): its object (flags
 * 0x8013), the car type's offset and size (0x4b2434), the faces it lights. */
void GAMEAPI car_glow_init(uint8_t *glow, uint8_t *model, int32_t kind)
{
    uint8_t *object = object_new(model, 0x8013, 0x180, 0);

    SET_PTR(glow, 4, object);
    U8(glow, 0) = 0;
    U32(glow, 0x2f0) = 0;
    U16(glow, 0x2e8) = 0;
    U16(glow, 0x2ea) = 0;
    U8(glow, 0x2dd) = 0;
    U8(glow, 0x2de) = 0;
    U16(glow, 0x2ec) = ((const uint16_t *)0x4b2434)[kind];
    U16(glow, 0x2ee) = 0x18;
    glow_model_offset(object, kind);
    U8(glow, 1) = (uint8_t)glow_first_low_face(glow);
    glow_pick_vertices_hooked(glow);
    glow_reset(glow);
}

/* 0x47bd70: the debris pieces (*0x5d9290: 64 of 0x180, two packets each
 * with texture 0x4d). */
void GAMEAPI debris_init(void)
{
    const uint8_t *t = TEXTURE(0x4d);
    uint32_t t0 = U32(t, 0), t1 = U32(t, 4), t2 = U32(t, 8);
    uint8_t *p = pieces_alloc(0x6000);

    G32(0x5d9290) = (uint32_t)(uintptr_t)(p);
    for (*(uint32_t *)0x5d9288 = 0; *(uint32_t *)0x5d9288 < 0x40; (*(uint32_t *)0x5d9288)++, p += 0x180) {
        uint32_t a, b;

        piece_init(p, 0xe);
        a = U32(p, 0x30);
        U32(p, 0x34) = 0x80c08080;
        U32(p, 0xac) = 0x80c08080;
        b = U32(p, 0xa8);
        U32(p, 0x30) = (a & 0xffffff) | 0xc000000;
        U8(p, 0x2c) = 0;
        U8(p, 0x37) = 0x98;
        U32(p, 0x50) = t0;
        U32(p, 0x6c) = t1;
        U32(p, 0x88) = t2;
        U32(p, 0xa4) = t2 >> 16;
        U32(p, 0xa8) = (b & 0xffffff) | 0xc000000;
        U8(p, 0xaf) = 0x98;
        U32(p, 0xc8) = t0;
        U32(p, 0xe4) = t1;
        U32(p, 0x100) = t2;
        U32(p, 0x11c) = t2 >> 16;
    }
    *(uint32_t *)0x5d928c = 0;
}

/* 0x40e670: set up car `index` in its slot: the car object (models from
 * the car type's tables 0x4a7b68 / 0x4a7b78 and track model 8), its twin
 * when race +0x54e is set, start position (track +0x48), facing the first
 * checkpoint, spheres, attached points, handling, race state, shadow,
 * trails, glow, replay record. */
void GAMEAPI car_setup(uint8_t *slot, int32_t index)
{
    uint32_t kind = U8(g_race, index + 0x1c);
    uint8_t *model = TRACK_MODEL(((const uint16_t *)0x4a7b68)[kind]);
    uint8_t *glow_model = TRACK_MODEL(((const uint16_t *)0x4a7b78)[kind]);
    uint8_t *wheel_model = TRACK_MODEL(8);
    uint8_t *car = object_new(model, 0x1c03, 0x2474, 0), *start, *checkpoint;
    int32_t dir[3], quat[4];
    uint32_t mode;

    SET_PTR(slot, 0, car);
    if (!U8(g_race, 0x54e))
        I32(slot, 0x260) = -1;
    else
        SET_PTR(slot, 0x260, object_new(model, 0xc03, 0x2474, 0));
    start = PTR(g_track, 0x48) + (uint32_t)U8(g_race, index + 0x10) * 8;
    *(int32_t *)0x4a7a00 = (int32_t)((uint32_t)(int32_t)S16(start, 0) << 16);
    *(int32_t *)0x4a7a04 = (int32_t)((uint32_t)(int32_t)S16(start, 2) << 16);
    *(int32_t *)0x4a7a08 = (int32_t)((uint32_t)(int32_t)S16(start, 4) << 16);
    object_place(car, (const void *)0x4a7a00);
    checkpoint = PTR(g_track, 0x70) + (*(uint32_t *)PTR(g_track, 0x90) & 0xff) * 0x14;
    dir[0] = S16(checkpoint, 0xc);
    dir[1] = S16(checkpoint, 0xe);
    dir[2] = S16(checkpoint, 0x10);
    if (((S16(checkpoint, 8) - (I32(car, 0x3c) >> 16)) * dir[2] >> 6) +
            ((S16(checkpoint, 6) - (I32(car, 0x38) >> 16)) * dir[1] >> 6) +
            ((S16(checkpoint, 4) - (I32(car, 0x34) >> 16)) * dir[0] >> 6) < 0) {
        dir[0] = -dir[0];
        dir[1] = -dir[1];
        dir[2] = -dir[2];
    }
    quat_facing_dir(quat, dir);
    I32(car, 0x58) = quat[0];
    I32(car, 0x5c) = quat[1];
    I32(car, 0x60) = quat[2];
    I32(car, 0x64) = quat[3];
    I32(car, 0xb4) = 0;
    body_spheres(car, 5, (const void *)0x4a7ad8, 0);
    body_points_init(car, 4, (const int16_t *)0x4a7990, 0x2c00);
    car_model_set(car, wheel_model, index);
    I32(car, 0x634) = 0;
    I32(car, 0x638) = 0;
    U8(car, 0x63c) = 0;
    U8(car, 0x63d) = 0;
    U8(car, 0x63e) = 0;
    I32(car, 0x640) = 0;
    I32(car, 0x654) = 0;
    U16(car, 0x658) = 0;
    U16(car, 0x65a) = 0;
    U16(car, 0x648) = 0;
    U16(car, 0x64a) = 0;
    U16(car, 0x64c) = 0;
    U8(car, 0x67c) = 0xff;
    U8(car, 0x67d) = 0xff;
    U16(car, 0x64e) = (uint16_t)index;
    I32(car, 0x680) = 0;
    U8(car, 0x688) = 0;
    U8(car, 0x689) = 0;
    I32(car, 0x68c) = -1;
    I32(car, 0x6ac) = -1;
    slot_race_reset_hooked(slot);
    car_section_reset(slot);
    car_shadow_init_hooked(car + 0x720);
    I32(car, 0x698) = 0x60000;
    I32(car, 0x69c) = 0x60000;
    I32(car, 0x6a0) = 0;
    U16(car, 0x6a8) = 0;
    U16(car, 0x6aa) = 0;
    U32(car, 0x148) = 0x40eb60;
    car_trail_init(car);
    car_glow_init(car + 0x1108, glow_model, (int32_t)kind);
    car_marks_init(car);
    car_trails_init_hooked(car);
    slot_digits(1, slot);
    U16(car, 0x14b8) = 0x40;
    U16(car, 0x14ba) = 0x1000;
    slot_target_reset(slot);
    I32(car, 0x14c4) = 0;
    I32(car, 0x14c0) = 0;
    I32(car, 0x14bc) = 0;
    I32(car, 0x14c8) = 0;
    U8(car, 0x65e) = 0;
    U8(car, 0x65d) = 0;
    U8(car, 0x65c) = 0;
    car_stats_set(car, index, (int32_t)kind, 1);
    U8(slot, 0x258) = 0;
    U8(car, 0x63f) = 0;
    U8(car, 0x644) = 0;
    U8(car, 0x645) = 0;
    I32(car, 0x2284) = 0;
    I32(car, 0x2288) = 0;
    if (U8(g_race, U16(car, 0x64e)) == CAR_KIND_AI) {
        I32(car, 0x650) = 0;
    } else {
        mode = U8(g_race, 0x58c) == 2 ? 4 : 1;
        if (*(int32_t *)0x4be3f0 == 1)
            mode = 4;
        replay_start_hooked(car, (int32_t)mode);
    }
    I32(car, 0x66c) = 0;
    I32(car, 0x668) = 0;
    I32(car, 0x664) = 0;
    I32(car, 0x678) = 0;
    I32(car, 0x674) = 0;
    I32(car, 0x670) = 0;
    car_texture_anim_attach(slot);
    car_target_add(car, 1);
    U8(slot, 0x259) = 0;
    U8(slot, 0x25a) = 0;
    lineup_setup(car, (uint8_t)kind, (const uint16_t *)0x4a7b68, (const uint16_t *)0x4a7b78);
    if (U8(g_race, 0x54e)) {
        uint8_t *twin = PTR(slot, 0x260);

        car_twin_wheels(slot, wheel_model);
        car_glow_init(twin + 0x1108, glow_model, (int32_t)kind);
        car_marks_init(twin);
        car_trails_init_hooked(twin);
    }
}
