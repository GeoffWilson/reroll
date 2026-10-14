/* Race setup (0x46a060), group U: creating the weapon bodies (homing
 * missiles, driller heads and parts, leaders and the snatch / wormhole
 * entity), the pickups and droppers, the split-screen viewports, the
 * primitive pools some effects draw from, a few lookup tables, and the
 * object-pool helpers the body allocator 0x460ba0 uses.
 *
 * Many of these fill PlayStation-style drawing primitives: a tag dword
 * (bits 24..31 = word count 0xc, low 24 bits = link) followed by a colour
 * dword whose top byte is the primitive code (0x8e/0x94/0x96/0x98/0x9a),
 * then per-vertex (x, y) words, (u, v) bytes, a clut word and a texture
 * page word. The (u, v, clut, page) values come from the texture records
 * at *0x6c1e14 (16 bytes each), indexed from the track's first record
 * *(u16 *)(*0x6c1eb0 + 0x2e). See build/groups/u_formats.txt.
 *
 * Still original (called by address): the allocator 0x4445e0, the body
 * allocator 0x460ba0, 0x465640 (body shape), 0x4611d0 (grid unlink),
 * 0x4612a0, the sprite allocator 0x47dcc0, 0x4270e0, 0x4291f0, 0x426200
 * (screen centre), 0x479ae0 (an empty function), 0x48f430 and vec
 * normalise (C, fixed.c). */
#include "../runtime.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"

#define g_race GP(0x4b1960)
#define g_config GP(0x4b2198)
#define g_world GP(0x6c1e30)
#define g_track_info GP(0x6c1eb0)
#define g_texture_records GP(0x6c1e14)
#define g_sin ((const int16_t *)0x4b24f8)
#define g_cos ((const int16_t *)0x4b2578)
#define g_pickup_seed (*(uint32_t *)0x4b11d8)

#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define S8(p, off) (*(int8_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define I32(p, off) (*(int32_t *)((p) + (off)))
#define PTR(p, off) GP((p) + (off))
#define PTR_SET(p, off, v) (U32(p, off) = (uint32_t)(uintptr_t)(v))

/* The track's model list: *(world + 0x18) indexed by the int16 at
 * *(world + 0x58) + `which`. */
#define WORLD_MODEL(which) (*(uint32_t *)(PTR(g_world, 0x18) + S16(PTR(g_world, 0x58), which) * 4))
/* The 16-byte texture record `n` after the track's first one. */
#define TEXTURE(n) (g_texture_records + ((uint32_t)U16(g_track_info, 0x2e) + (n)) * 0x10)

#define mem_alloc(n) GMEM(GCALL(0x4445e0, kr_malloc)(n))
#define body_new(model, kind, size, d) GCALL(0x460ba0, object_create)((uint8_t *)GMEM(model), kind, size, d)
#define body_shape_set(body, shape) GCALL(0x465640, body_init_from_template)(body, (const uint32_t *)(shape))
#define body_grid_unlink(body) GCALL(0x4611d0, object_grid_unlink)(body)
#define sprite_grid_unlink(body) GCALL(0x4612a0, object_grid_unlink2)(body)
#define prim_finish(prim, a) GCALL(0x4270e0, prim_flag2_set)(prim, a)
#define screen_centre_set(x, y) GCALL(0x426200, gte_screen_offset_set)((int32_t)(x), (int32_t)(y))
#define sprite_new(a, b, c, texture, size, f) GCALL(0x47dcc0, sprite_object_create)(a, b, c, texture, size, f)
#define snatch_spark_reset_at(spark) GCALL(0x48f430, snatch_spark_reset)((int32_t *)(spark))
#if defined(__i386__)
/* Empty functions the original passes arguments to (cdecl: unread). */
#define clock_setup(a, b, c, d, e) \
    (((void(GAMEAPI *)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))(uintptr_t)0x4291f0)(a, b, c, d, e))
#define screen_clear(rect, a, b, c) \
    (((int32_t(GAMEAPI *)(int16_t *, uint32_t, uint32_t, uint32_t))(uintptr_t)0x479ae0)(rect, a, b, c))
#else
#define clock_setup(a, b, c, d, e) ((void)(d), GCALL(0x4291f0, dx_error_ignore)())
#define screen_clear(rect, a, b, c) ((void)(rect), GCALL(0x479ae0, ps_wait_none)())
#endif

/* A primitive's tag: keep the link, set the word count. */
static void prim_tag(uint8_t *prim)
{
    U32(prim, 0) = (U32(prim, 0) & 0xffffff) | 0xc000000;
}

/* --- weapon bodies --- */

/* 0x483100: the 64 trail primitives of a projectile (+0x5dc, 0x78 each,
 * two per trail point) and the 32 trail points (+0x1b8, 0x20 each). The
 * last point is tinted (texture 0x3f); driller parts (+0x184 == 2) use
 * texture 0x58, the others cycle 0x47..0x4c. */
void GAMEAPI projectile_trail_init(uint8_t *body)
{
    uint8_t *prim = body + 0x5dc;
    uint32_t i, j;

    for (i = 0; i < 0x20; i++) {
        uint32_t texture;
        uint8_t r, g, b;
        uint8_t *point = body + 0x1bc + i * 0x20;

        if (i == 0x1f) {
            texture = 0x3f;
            r = 0xfe;
            g = 100;
            b = 200;
        } else {
            texture = I32(body, 0x184) == 2 ? 0x58 : (uint32_t)((int32_t)i % 6 + 0x47);
            r = g = b = 0xff;
        }
        for (j = 0; j < 2; j++) {
            uint32_t tag = U32(prim, 0);
            const uint8_t *t;

            U32(prim, 4) = 0;
            U8(prim, 7) = 0x9a;
            U32(prim, 0) = (tag & 0xffffff) | 0xc000000;
            t = TEXTURE(texture);
            U32(prim, 0x20) = U32(t, 0);
            U32(prim, 0x3c) = U32(t, 4);
            U16(prim, 0x58) = U16(t, 8);
            U16(prim, 0x74) = U16(t, 10);
            U8(prim, 5) = g;
            U8(prim, 4) = r;
            U8(prim, 6) = b;
            S16(point, -4) = 0;
            S16(point, -2) = 0;
            S16(point, 0) = 0;
            prim += 0x78;
            point += 8;
        }
    }
}

/* The fields every projectile starts with cleared. */
static void projectile_clear(uint8_t *body)
{
    U32(body, 0x108) |= 1;
    I32(body, 0x190) = 0;
    I32(body, 0x194) = 0;
    I32(body, 0x198) = 0;
    I32(body, 0x19c) = 0;
    I32(body, 0x1a0) = 0;
    I32(body, 0x1a4) = 0;
    I32(body, 0x1a8) = 0;
    I32(body, 0x180) = 0;
    I32(body, 0x188) = 0;
}

/* 0x486a40: the snatch texture's page and the (u, v) corners of its
 * picture. */
void GAMEAPI snatch_texture_corners(uint16_t *page, uint16_t *top_left, uint16_t *top_right, uint16_t *bottom_left,
                                    uint16_t *bottom_right)
{
    *page = *(uint16_t *)0x4b24a0;
    *top_left = 0x8000;
    *top_right = 0x807f;
    *bottom_left = 0xff00;
    *bottom_right = 0xff7f;
}

/* 0x48f780: the snatch's primitives: 60 textured quads (+0x6ac, 0x84
 * each, grey) and a ring of 12 triangles (+0x259c, 0x60 each) round the
 * centre of its texture. */
void GAMEAPI snatch_prims_init(uint8_t *snatch)
{
    const uint8_t *t = TEXTURE(0x5c);
    uint8_t *prim = snatch + 0x6ac;
    union {
        uint8_t b[0x28];
        uint16_t w[0x14];
    } f; /* the original's frame from +0x10 */
    uint8_t half_u, half_v, mid_u, mid_v, *ring_u = f.b + 0x10, *ring_v = f.b + 0x1c;
    uint32_t i, a;

    for (i = 0; i < 0x3c; i++, prim += 0x84) {
        U32(prim, 0x20) = U32(t, 0);
        U32(prim, 0x40) = U32(t, 4);
        U16(prim, 0x60) = U16(t, 8);
        U16(prim, 0x80) = U16(t, 10);
        U32(prim, 4) = 0x80808080;
        U32(prim, 0x24) = 0x80808080;
        U32(prim, 0x44) = 0x80808080;
        U32(prim, 0x64) = 0x80808080;
        prim_tag(prim);
        U8(prim, 7) = 0x8e;
    }
    /* the page at f+0xa, the corners' (u, v) at f+2, f+0, f+4, f+0xc */
    snatch_texture_corners(&f.w[5], &f.w[1], &f.w[0], &f.w[2], &f.w[6]);
    f.b[0xc] = (uint8_t)(f.b[0] - f.b[2]);
    half_u = (uint8_t)(f.b[0xc] >> 1);
    half_v = (uint8_t)((uint8_t)(f.b[5] - f.b[3]) >> 1);
    f.b[4] = (uint8_t)(f.b[5] - f.b[3]);
    mid_u = (uint8_t)(half_u + f.b[2]);
    mid_v = (uint8_t)(half_v + f.b[3]);
    for (i = 0, a = 0; a < 0xc00; i++, a += 0x100) {
        uint32_t k = a / 12;

        ring_u[i] = (uint8_t)((uint8_t)((int32_t)g_sin[k] * (int32_t)half_u >> 14) + mid_u);
        ring_v[i] = (uint8_t)((uint8_t)(-((int32_t)g_cos[k] * (int32_t)half_v) >> 14) + mid_v);
    }
    prim = snatch + 0x259c;
    for (i = 1; i - 1 < 0xc; i++, prim += 0x60) {
        uint32_t next = i < 0xc ? i : 0;

        U32(prim, 4) = 0xffffffff;
        U16(prim, 0x3e) = f.w[5];
        U8(prim, 0x20) = ring_u[i - 1];
        U8(prim, 0x21) = ring_v[i - 1];
        U8(prim, 0x3c) = ring_u[next];
        U8(prim, 0x3d) = ring_v[next];
        U8(prim, 0x59) = mid_v;
        U8(prim, 0x58) = mid_u;
        prim_tag(prim);
        U8(prim, 7) = 0x96;
    }
}

/* 0x48ec30: the snatch's sprites: 5 beams (+0x1e0, texture 0x3f), 10
 * sparks (+0x274, texture 100, with their records at +0x29c) and 32 trail
 * dots (+0x1f4, texture 0x2e), then its primitives. */
void GAMEAPI snatch_sprites_create(uint8_t *snatch)
{
    uint8_t *slot = snatch + 0x1e0; /* pointers */
    uint8_t *spark = snatch + 0x29c;
    uint32_t texture = (uint32_t)U16(g_track_info, 0x2e) + 0x3f, i;

    for (i = 0; i < 5; i++) {
        uint8_t *s = sprite_new(0, 0, 0, texture, 0x30, 0x80);

        U32(s, 0) = 0xd;
        U8(s, 0x108) = 0;
        U8(s, 0x109) = 0;
        U8(s, 0x10a) = 0;
        sprite_grid_unlink(s);
        PTR_SET(slot, 0, s);
        slot += 4;
    }
    texture = (uint32_t)U16(g_track_info, 0x2e) + 100;
    slot = snatch + 0x274;
    for (i = 0; i < 10; i++, spark += 0x10) {
        uint8_t *s = sprite_new(0, 0, 0, texture, 0x20, 0x80);

        U32(s, 0) = 0xd;
        U8(s, 0x108) = 0;
        U8(s, 0x109) = 0;
        U8(s, 0x10a) = 0;
        sprite_grid_unlink(s);
        PTR_SET(slot, 0, s);
        slot += 4;
        snatch_spark_reset_at(spark);
    }
    texture = (uint32_t)U16(g_track_info, 0x2e) + 0x2e;
    slot = snatch + 0x1f4;
    for (i = 0; i < 0x20; i++) {
        uint8_t *s = sprite_new(0, 0, 0, texture, 0x10, 0x80);

        U32(s, 0) = 0xd;
        U8(s, 0x108) = 0;
        U8(s, 0x109) = 0;
        U8(s, 0x10a) = 0;
        sprite_grid_unlink(s);
        PTR_SET(slot, 0, s);
        slot += 4;
    }
    snatch_prims_init(snatch);
}

/* 0x48d880: the snatch / wormhole body (weapon kind 7), at *0x5d1fc0. */
void GAMEAPI snatch_create(void)
{
    uint8_t *body = body_new(0, 0x816, 0x2a3c, 0);

    I32(body, 0x184) = 7;
    U32(body, 0x148) = 0x48dc20;
    body_shape_set(body, (const void *)0x4b0b68);
    body_grid_unlink(body);
    I32(body, 0x198) = 0;
    I32(body, 0x19c) = 0;
    U32(body, 0x108) |= 1;
    I32(body, 0x1a0) = 0;
    I32(body, 0x1a4) = 0;
    I32(body, 0x180) = 0;
    U8(body, 0x2a37) = 0;
    I32(body, 0x188) = 0;
    I32(body, 0x190) = 0;
    PTR_SET((uint8_t *)0x5d1fc0, 0, body);
    snatch_sprites_create(body);
    U8(body, 0x2a36) = 0xff;
}

/* 0x4583e0: the weapon bodies: 4 homing missiles (0x6b9ce0), 6 drillers
 * (0x6b9cc0) with 3 parts each (0x6b9cf0, the parent's +0x184..+0x18c),
 * 2 leaders (0x6b9d38) and the snatch; the live counts 0x6b9d40..43 are
 * cleared. */
void GAMEAPI weapon_bodies_create(void)
{
    uint32_t homer_model = WORLD_MODEL(0xa), part_model = WORLD_MODEL(0xa), leader_model = WORLD_MODEL(8);
    uint8_t *out = (uint8_t *)0x6b9ce0, *parts = (uint8_t *)0x6b9cf0, *driller; /* pointers */
    int32_t i;

    for (i = 0; (uint32_t)(uintptr_t)out < 0x6b9cf0; i++) {
        uint8_t *body = body_new(homer_model, 0x1c0c, 0x23ec, 0);

        I32(body, 0x184) = 1;
        U8(body, 0x23dd) = 0x7f;
        PTR_SET(out, 0, body);
        out += 4;
        U32(body, 0x148) = 0x4596f0;
        body_shape_set(body, (const void *)0x4b0b68);
        body_grid_unlink(body);
        projectile_trail_init(body);
        I32(body, 0x23e0) = i;
        projectile_clear(body);
    }
    for (driller = (uint8_t *)0x6b9cc0; (uint32_t)(uintptr_t)driller < 0x6b9cd8; driller += 4) {
        uint8_t *head = body_new(0, 0x811, 0x1b8, 0);
        uint8_t *link;

        PTR_SET(driller, 0, head);
        body_shape_set(head, (const void *)0x4b0b68);
        body_grid_unlink(head);
        U32(head, 0x148) = 0x424990;
        U32(head, 0x108) |= 1;
        I32(head, 0x1ac) = 0x7f;
        link = head + 0x184;
        for (i = 0; i < 3; i++) {
            uint8_t *part = body_new(part_model, 0x8c0c, 0x23ec, 0);

            PTR_SET(parts, 0, part);
            U32(part, 0x148) = 0;
            U8(part, 0x23dd) = 0x7f;
            body_shape_set(part, (const void *)0x4b0b68);
            body_grid_unlink(part);
            I32(part, 0x190) = 0;
            U32(part, 0x108) |= 1;
            I32(part, 0x194) = 0;
            I32(part, 0x198) = 0;
            I32(part, 0x19c) = 0;
            I32(part, 0x1a0) = 0;
            I32(part, 0x1a4) = 0;
            I32(part, 0x1a8) = 0;
            I32(part, 0x180) = 0;
            I32(part, 0x188) = 0;
            I32(part, 0x23e0) = i;
            I32(part, 0x184) = 2;
            projectile_trail_init(part);
            PTR_SET(link, 0, part);
            link += 4;
            parts += 4;
            PTR_SET(part, 0x18c, head);
            U32(part, 0x148) = 0;
        }
        I32(head, 0x190) = -1;
        I32(head, 0x194) = 0;
    }
    out = (uint8_t *)0x6b9d38;
    for (i = 0; (uint32_t)(uintptr_t)out < 0x6b9d40; i++) {
        uint8_t *body = body_new(leader_model, 0x1c0c, 0x240c, 0);

        I32(body, 0x184) = 0;
        PTR_SET(out, 0, body);
        out += 4;
        U32(body, 0x148) = 0x43f040;
        projectile_trail_init(body);
        body_shape_set(body, (const void *)0x4b0b68);
        body_grid_unlink(body);
        I32(body, 0x23e0) = i;
        U8(body, 0x23dd) = 0x7f;
        projectile_clear(body);
    }
    snatch_create();
    *(uint8_t *)0x6b9d40 = 0;
    *(uint8_t *)0x6b9d41 = 0;
    *(uint8_t *)0x6b9d42 = 0;
    *(uint8_t *)0x6b9d43 = 0;
}

/* --- pickups and droppers --- */

/* One textured quad of a pickup (0x78 bytes), from texture record `n`:
 * the second and third corners' (u, v) are swapped. */
static void pickup_quad(uint8_t *prim, uint32_t n)
{
    const uint8_t *t;
    uint32_t uv1, uv2;
    uint8_t swap;

    U8(prim, 7) = 0x98;
    prim_tag(prim);
    t = TEXTURE(n);
    U32(prim, 0x20) = U32(t, 0);
    uv1 = U32(t, 4);
    U32(prim, 0x3c) = uv1;
    uv2 = U32(t, 8);
    U16(prim, 0x58) = (uint16_t)uv2;
    U16(prim, 0x74) = U16(t, 10);
    U8(prim, 0x58) = (uint8_t)uv1;
    U8(prim, 0x3c) = (uint8_t)uv2;
    swap = U8(prim, 0x3d);
    U8(prim, 0x3d) = U8(prim, 0x59);
    U8(prim, 0x59) = swap;
    prim_finish(prim, 1);
    U8(prim, 4) = 0x7f;
    U8(prim, 5) = 0x7f;
    U8(prim, 6) = 0x7f;
}

/* 0x468150: a pickup's quads (2 from texture 0x40 at +0x190, 6 from 0x3f
 * at +0x280) and a random spin phase (+0x552). */
void GAMEAPI pickup_prims_init(uint8_t *pickup)
{
    uint8_t *prim;
    int32_t i;

    for (i = 0, prim = pickup + 0x190; i < 2; i++, prim += 0x78)
        pickup_quad(prim, 0x40);
    for (i = 0, prim = pickup + 0x280; i < 6; i++, prim += 0x78)
        pickup_quad(prim, 0x3f);
    g_pickup_seed = g_pickup_seed * 16807 & 0x7fffffff;
    U16(pickup, 0x552) = *(uint8_t *)0x4b11da;
}

/* 0x425ce0: the droppers (*(0x6c1eb0)+0xf7 of them, at 0x6cd420) and an
 * empty drop queue (0x6cd438, 6 entries; count 0x6cd450). */
void GAMEAPI droppers_create(void)
{
    uint32_t count = U8(g_track_info, 0xf7), model = WORLD_MODEL(0x46);
    uint8_t *out = (uint8_t *)0x6cd420; /* pointers */
    int32_t i;

    for (; count; count--) {
        uint8_t *body = body_new(model, 0x8e1a, 0x18c, 0);

        PTR_SET(out, 0, body);
        out += 4;
        body_shape_set(body, (const void *)0x4a8120);
        body_grid_unlink(body);
        I32(body, 0x180) = 0;
    }
    for (i = 0; i < 6; i++)
        ((int32_t *)0x6cd438)[i] = -1;
    *(uint16_t *)0x6cd450 = 0;
}

/* 0x467120: the pickups (*(world)+0x36 of them, listed at *0x6b8ce0,
 * count 0x6b8ce4), only in race modes that have them (race +0xa86 set, or
 * mode +0x551 3 or 8); then the droppers. */
void GAMEAPI pickups_create(void)
{
    uint32_t count = U8(g_world, 0x36), model, n;
    uint8_t *race = g_race, *out; /* pointers */

    if (!U8(race, 0xa86) && U8(race, 0x551) != 3 && U8(race, 0x551) != 8) {
        *(uint32_t *)0x6b8ce4 = 0;
        return;
    }
    model = WORLD_MODEL(0xc);
    out = mem_alloc(count * 4);
    PTR_SET((uint8_t *)0x6b8ce0, 0, out);
    for (n = count; (int32_t)n > 0; n--) {
        uint8_t *body = body_new(model, 0x8c0a, 0x554, 0);

        PTR_SET(out, 0, body);
        out += 4;
        body_shape_set(body, (const void *)0x4b16c0);
        body_grid_unlink(body);
        U16(body, 0x182) = 0x82;
        U16(body, 0x180) = 1;
        I32(body, 0x11c) = 0;
        pickup_prims_init(body);
    }
    *(uint32_t *)0x6b8ce4 = count;
    droppers_create();
}

/* --- viewports --- */

/* 0x428b50: the border round each split-screen view: one pixel when the
 * screen is split. */
void GAMEAPI viewport_border(int32_t *x, int32_t *y)
{
    *x = 0;
    *y = 0;
    if (U8(g_race, 0x552)) {
        *x = *x + 1;
        *y = *y + 1;
    }
}

/* 0x45ed40: the order of the players' views: in a network game (0x5d622c
 * bit 3) the local player first, then the other active players' cars,
 * then unused car numbers; otherwise 0..3. Players: 6 of 0x66c at
 * 0x5d2cd0 (+0 active, +0x18 car); the local one is 0x5d535c. */
void GAMEAPI viewport_player_order(int32_t *out)
{
    int32_t n = 0, i, j, k;

    if (!(*(uint8_t *)0x5d622c & 8)) {
        for (i = 0; i < 4; i++)
            *out++ = i;
        return;
    }
#define PLAYER_ACTIVE(i) (*(int32_t *)GMEM(0x5d2cd0 + (i) * 0x66c))
#define PLAYER_CAR(i) (*(int32_t *)GMEM(0x5d2ce8 + (i) * 0x66c))
    *out = PLAYER_CAR(*(int32_t *)0x5d535c);
    for (i = 0; i < 6; i++) {
        if (i != *(int32_t *)0x5d535c && PLAYER_ACTIVE(i)) {
            *++out = PLAYER_CAR(i);
            if (n++ > 1)
                break;
        }
    }
    if (n >= 3)
        return;
    for (j = 0; j < 6; j++) {
        for (k = 0; k < 6; k++)
            if (k == PLAYER_CAR(j) && PLAYER_ACTIVE(k))
                break;
        if (k == 6) {
            *++out = j;
            if (n++ > 1)
                return;
        }
    }
#undef PLAYER_ACTIVE
#undef PLAYER_CAR
}

/* One view rectangle (car slot +8: x, y, width, height). */
static void view_set(int32_t slot, int32_t x, int32_t y, uint32_t w, int32_t h)
{
    int32_t *v = (int32_t *)GMEM(0x6bfb88 + slot * 0x274);

    v[0] = x;
    v[1] = y;
    v[2] = (int32_t)w;
    v[3] = h;
}

/* 0x423360: lay out the split-screen views by the view mode (race +0x552:
 * 0 one, 1 two above each other, 2 two side by side, 3 three, 4 four) for
 * the cars `a`..`d`, with the inner rectangles (slot +0x18) inside the
 * border, the screen centre, the background colours and the clear. */
void GAMEAPI viewports_layout(int32_t a, int32_t b, int32_t c, int32_t d)
{
    uint32_t mode = U8(g_race, 0x552), w = 0, h = 0, w2, h2, half_h;
    int32_t bx, by, count, views[4], i;
    int16_t rect[4];

    *(uint16_t *)0x4bde74 = 0;
    switch (mode) {
    case 0:
        w = *(uint16_t *)0x6cd462;
        h = *(uint16_t *)0x6cd464;
        break;
    case 1:
        w = *(uint16_t *)0x6cd462;
        h = (uint32_t)*(uint16_t *)0x6cd464 >> 1;
        break;
    case 2:
        w = (uint32_t)*(uint16_t *)0x6cd462 >> 1;
        h = *(uint16_t *)0x6cd464;
        break;
    case 3:
    case 4:
        w = (uint32_t)*(uint16_t *)0x6cd462 >> 1;
        h = (uint32_t)*(uint16_t *)0x6cd464 >> 1;
        break;
    }
    *(uint16_t *)0x6cd468 = (uint16_t)(h >> 1);
    *(uint16_t *)0x6cd466 = (uint16_t)(w >> 1);
    screen_centre_set((w >> 1) & 0xffff, *(uint16_t *)0x6cd468);
    *(uint32_t *)0x6cd478 = (h << 16) + w;
    half_h = (uint32_t)*(uint16_t *)0x6cd464 >> 1;
    w2 = (uint32_t)*(uint16_t *)0x6cd462 >> 1;
    viewport_border(&bx, &by);
    w2 += (uint32_t)bx;
    w -= (uint32_t)bx * 2;
    half_h += (uint32_t)by;
    h2 = h - (uint32_t)by * 2;
    switch (mode) {
    case 1:
        count = 2;
        view_set(a, bx, by, w, (int32_t)h2);
        view_set(b, bx, (int32_t)half_h, w, (int32_t)h2);
        break;
    case 2:
        count = 2;
        view_set(a, bx, by, w, (int32_t)h2);
        view_set(b, (int32_t)w2, by, w, (int32_t)h2);
        break;
    case 3:
        count = 3;
        view_set(a, (int32_t)(((uint32_t)*(uint16_t *)0x6cd462 >> 1) - (w >> 1)), by, w, (int32_t)h2);
        view_set(b, bx, (int32_t)half_h, w, (int32_t)h2);
        view_set(c, (int32_t)w2, (int32_t)half_h, w, (int32_t)h2);
        break;
    case 4:
        count = 4;
        view_set(a, bx, by, w, (int32_t)h2);
        view_set(b, (int32_t)w2, by, w, (int32_t)h2);
        view_set(c, bx, (int32_t)half_h, w, (int32_t)h2);
        view_set(d, (int32_t)w2, (int32_t)half_h, w, (int32_t)h2);
        break;
    default:
        count = 1;
        view_set(a, bx, by, w, (int32_t)h2);
        break;
    }
    views[0] = a;
    views[1] = b;
    views[2] = c;
    views[3] = d;
    for (i = 0; i < count; i++) {
        int32_t *v = (int32_t *)GMEM(0x6bfb88 + views[i] * 0x274);

        v[4] = v[0];
        v[5] = v[1];
        v[6] = v[2];
        v[7] = v[3];
        v[4] -= bx;
        v[5] -= by;
        v[6] += bx * 2;
        v[7] += by * 2;
    }
    if (mode == 3) {
        int32_t *v = (int32_t *)GMEM(0x6bfb88 + a * 0x274);

        v[4] = 0;
        v[6] = *(uint16_t *)0x6cd462;
    }
    *(uint8_t *)0x6cd4b8 = *(uint8_t *)0x6cd46e;
    *(uint8_t *)0x6cdd28 = *(uint8_t *)0x6cd46e;
    *(uint8_t *)0x6cd4b6 = *(uint8_t *)0x6cd470;
    *(uint8_t *)0x6cdd26 = *(uint8_t *)0x6cd470;
    *(uint8_t *)0x6cd4b9 = (uint8_t)(U8(g_track_info, 0x30) << 1);
    *(uint8_t *)0x6cdd29 = *(uint8_t *)0x6cd4b9;
    *(uint8_t *)0x6cd4ba = (uint8_t)(U8(g_track_info, 0x31) << 1);
    *(uint8_t *)0x6cdd2a = *(uint8_t *)0x6cd4ba;
    *(uint8_t *)0x6cd4bb = (uint8_t)(U8(g_track_info, 0x32) << 1);
    *(uint8_t *)0x6cdd2b = *(uint8_t *)0x6cd4bb;
    rect[0] = 0;
    rect[1] = 0;
    rect[2] = 0x140;
    rect[3] = 0x100;
    screen_clear(rect, 0, 0, 0);
    rect[3] = 0xff;
    rect[1] = 0x100;
    screen_clear(rect, 0, 0, 0);
    U16(g_race, 0xa82) = (uint16_t)w;
    U16(g_race, 0xa80) = (uint16_t)h2;
}

/* 0x444080: the views for this race (cars in 0x6c0a40..4c). */
void GAMEAPI viewports_setup(void)
{
    viewport_player_order((int32_t *)0x6c0a40);
    viewports_layout(*(int32_t *)0x6c0a40, *(int32_t *)0x6c0a44, *(int32_t *)0x6c0a48, *(int32_t *)0x6c0a4c);
}

/* --- primitive pools and tables --- */

/* 0x4040b0: the (u, v) midpoint of two texture corners. */
void GAMEAPI uv_midpoint(uint8_t *out, const uint8_t *a, const uint8_t *b)
{
    out[0] = (uint8_t)(((int32_t)a[0] + b[0]) / 2);
    out[1] = (uint8_t)(((int32_t)a[1] + b[1]) / 2);
}

/* 0x4040f0: two primitive pools (0x1e00 bytes at 0x4b5fa8 / 0x4b5fb0..b4:
 * 16 groups of four 0x78-byte quads splitting the first 16 texture
 * records into quarters; 0xc00 bytes at 0x4b5fdc / 0x4b5fbc..b8: 32
 * 0x60-byte triangles round a circle of texture 0xa2), and the table at
 * *0x6d0a40 doubled. */
void GAMEAPI quarter_prims_init(void)
{
    uint8_t *quads, *tris, *t, *q;
    const uint8_t *edge;
    uint32_t i, j, k;

    PTR_SET((uint8_t *)0x4b5fa8, 0, mem_alloc(0x1e00));
    PTR_SET((uint8_t *)0x4b5fdc, 0, mem_alloc(0xc00));
    t = TEXTURE(0);
    clock_setup(0x6d0a60, 1, 0, U16(t, 6), 0);
    clock_setup(0x6d0a6c, 1, 0, U16(t, 6), 0);
    clock_setup(0x6d0a20, 1, 1, U16(t, 6), 0);
    clock_setup(0x6d0a2c, 1, 1, U16(t, 6), 0);
    quads = GP(0x4b5fa8);
    tris = GP(0x4b5fdc);
    PTR_SET((uint8_t *)0x4b5fbc, 0, tris);
    PTR_SET((uint8_t *)0x4b5fb0, 0, quads);
    PTR_SET((uint8_t *)0x4b5fb4, 0, quads + 0x1e00);
    PTR_SET((uint8_t *)0x4b5fb8, 0, tris + 0xc00);
    for (i = 0, q = quads; i < 0x40; i++, q += 0x78) {
        uint32_t tag = U32(q, 0);

        U8(q, 7) = 0x98;
        U8(q, 4) = 0xff;
        U8(q, 5) = 0xff;
        U32(q, 0) = (tag & 0xffffff) | 0xc000000;
        U8(q, 6) = 0xff;
    }
    /* each texture record: (u, v) of its 4 corners at +0, +4, +8, +10,
     * clut +2, page +6 */
    q = GP(0x4b5fb0) + 0x12c;
    for (i = 0; i < 0x10; i++, t += 0x10, q += 0x1e0) {
        U32(q, -0x10c) = U32(t, 0);
        uv_midpoint(q - 0xf0, t, t + 4);
        uv_midpoint(q - 0xd4, t + 8, t);
        uv_midpoint(q - 0xb8, t + 10, t);
        U16(q, -0xee) = U16(t, 6);
        uv_midpoint(q - 0x1c, t, t + 4);
        U32(q, 0) = U32(t, 4);
        uv_midpoint(q + 0x1c, t, t + 10);
        uv_midpoint(q + 0x38, t + 4, t + 10);
        U16(q, -0x1a) = U16(t, 2);
        uv_midpoint(q - 0x94, t, t + 8);
        uv_midpoint(q - 0x78, t, t + 10);
        U16(q, -0x5c) = U16(t, 8);
        uv_midpoint(q - 0x40, t + 8, t + 10);
        U16(q, -0x76) = U16(t, 6);
        U16(q, -0x92) = U16(t, 2);
        uv_midpoint(q + 0x5c, t, t + 10);
        uv_midpoint(q + 0x78, t + 4, t + 10);
        uv_midpoint(q + 0x94, t + 8, t + 10);
        U16(q, 0xb0) = U16(t, 10);
        U16(q, 0x7a) = U16(t, 6);
        U16(q, 0x5e) = U16(t, 2);
    }
    for (i = 0, q = GP(0x4b5fdc); i < 0x20; i++, q += 0x60) {
        uint32_t tag = U32(q, 0);

        U8(q, 7) = 0x94;
        U8(q, 4) = 0xff;
        U8(q, 5) = 0xff;
        U32(q, 0) = (tag & 0xffffff) | 0xc000000;
        U8(q, 6) = 0xff;
    }
    *(uint8_t *)0x4b5fd0 = 0xff;
    *(uint8_t *)0x4b5fd1 = 0xff;
    *(uint8_t *)0x4b5fd2 = 0xff;
    for (i = 0; i < 0x100; i += 8) {
        U16(GP(0x6d0a40), i) = (uint16_t)(U16(GP(0x6d0a40), i) << 1);
        U16(GP(0x6d0a40), i + 4) = (uint16_t)(U16(GP(0x6d0a40), i + 4) << 1);
        U16(GP(0x6d0a40), i + 2) = (uint16_t)(U16(GP(0x6d0a40), i + 2) << 1);
        U16(GP(0x6d0a40), i + 6) = (uint16_t)(U16(GP(0x6d0a40), i + 6) << 1);
    }
    edge = TEXTURE(0xa2);
    q = GP(0x4b5fdc) + 0x20;
    for (k = 0, i = 0, j = 8; k < 0x20; k++, q += 0x60) {
        U8(q, 0) = 0x80;
        U16(q, 0x1e) = U16(edge, 6);
        U8(q, 1) = 0x80;
        U8(q, 0x1c) = (uint8_t)(((int32_t)g_sin[i] * 0xfe >> 15) + 0x80);
        U8(q, 0x1d) = (uint8_t)(((int32_t)g_cos[i] * 0xfe >> 15) + 0x80);
        U8(q, 0x38) = (uint8_t)(((int32_t)g_sin[j] * 0xfe >> 15) + 0x80);
        U8(q, 0x39) = (uint8_t)(((int32_t)g_cos[j] * 0xfe >> 15) + 0x80);
        i = j;
        j = (j + 8) & 0xff;
    }
}

/* 0x405050: clear the timed explosions (0x6d06e0, 0x10 each; +6 done
 * 0xff, +7 0; count 0x6d06c0). */
void GAMEAPI explosions_reset(void)
{
    uint8_t *p;

    for (p = (uint8_t *)0x6d06e6; (uint32_t)(uintptr_t)p < 0x6d0a06; p += 0x10) {
        p[1] = 0;
        p[0] = 0xff;
    }
    *(uint32_t *)0x6d06c0 = 0;
}

/* 0x442660: clear the flags of the attached points (0x6c1b80, 8 bytes
 * each: int16 x, y, z, flag; see body_points_update). */
void GAMEAPI attached_points_reset(void)
{
    uint16_t *p;

    for (p = (uint16_t *)0x6c1b86; (uint32_t)(uintptr_t)p < 0x6c1dbe; p += 4)
        *p = 0;
}

/* 0x46c210: no car shadows yet (count 0x4d3dfc). */
void GAMEAPI shadows_reset(void)
{
    *(uint32_t *)0x4d3dfc = 0;
}

/* 0x433d30: the 256 0x68-byte primitives at *0x4be378 (code 0xaa) and
 * the words at 0x6c802e (every 0x18 bytes) cleared. */
void GAMEAPI trail_prims_init(void)
{
    uint8_t *p = mem_alloc(0x6800);
    uint16_t *w;
    int32_t i;

    PTR_SET((uint8_t *)0x4be378, 0, p);
    *(uint32_t *)0x6c80fc = 0;
    for (w = (uint16_t *)0x6c802e; (uint32_t)(uintptr_t)w < 0x6c80ee; w += 0xc)
        *w = 0;
    for (i = 0; i < 0x100; i++, p += 0x68) {
        uint32_t tag = U32(p, 0);

        U32(p, 4) = 0;
        U8(p, 7) = 0xaa;
        U32(p, 0) = (tag & 0xffffff) | 0xc000000;
    }
}

/* 0x47c2c0: the 1024-entry table at *0x4d4b9c (made once): entries 0..31
 * count down from 0x7ffd, then 0x100000 / i - i - 3 (at least 0). */
void GAMEAPI reciprocal_table_init(void)
{
    int32_t i, n;

    if (GP(0x4d4b9c))
        return;
    PTR_SET((uint8_t *)0x4d4b9c, 0, mem_alloc(0x1000));
    for (i = 0x20; i < 0x400; i++) {
        n = 0x100000 / i - i - 3;
        if (n < 0)
            n = 0;
        ((int32_t *)GP(0x4d4b9c))[i] = n;
    }
    for (i = 0, n = 0x7ffd; i < 0x20; i++, n--)
        ((int32_t *)GP(0x4d4b9c))[i] = n;
}

/* 0x47c330: the per-model vertex buffers for the track's models
 * (*(world) with *(world)+0x10 of them; 0x20 bytes per 16 vertices, plus
 * one): offsets then pointers at 0x5d7de0, one block at *0x5d71d0, zeroed;
 * the light list (0x5d71e0, 0x5d72e0..) and the sprite slots at 0x5d8064
 * cleared. */
void GAMEAPI model_buffers_init(void)
{
    uint8_t *world, *models; /* pointers */
    uint32_t total = 0, i, *block;

    reciprocal_table_init();
    for (i = 0; i < 0x46; i++)
        *(uint16_t *)GMEM(0x5d8064 + i * 0x40) = 0;
    world = g_world;
    models = GP(world);
    i = 0;
    if (U32(world, 0x10)) {
        do {
            ((uint32_t *)0x5d7de0)[i & 0xffff] = total;
            total += ((U32(GP(models + (i & 0xffff) * 4), 0xc) >> 4) + 1) * 0x20;
            i++;
        } while ((i & 0xffff) < U32(world, 0x10));
    }
    ((uint32_t *)0x5d7de0)[i & 0xffff] = total;
    block = mem_alloc(total);
    PTR_SET((uint8_t *)0x5d71d0, 0, block);
    i = 0;
    if (U32(g_world, 0x10) + 1 != 0) {
        do {
            ((uint32_t *)0x5d7de0)[i & 0xffff] += (uint32_t)(uintptr_t)block;
            i++;
        } while ((i & 0xffff) < U32(g_world, 0x10) + 1);
    }
    for (i = 0; (i & 0xffff) < total >> 2; i++)
        *block++ = 0;
    *(uint32_t *)0x5d7fe0 = 0;
    for (i = 0; i < 0x40; i++)
        ((uint32_t *)0x5d71e0)[i] = 0;
    *(uint32_t *)0x5d7fe8 = 0;
    *(uint32_t *)0x5d7fe4 = 0;
    *(uint32_t *)0x5d72e0 = 0;
}

/* --- model normals --- */

/* 0x40f2a0: vertex normals of a model (`model` +4 vertex count, +6 face
 * count, +0xc faces of 8 bytes: four 1-based vertex numbers, then the
 * face normal packed 10:10:10 as x bits 20..29, y 10..19, z 0..9): the
 * normalised sum of the normals of the faces using each vertex, as int16
 * (x, y, z, pad) in `out`. */
void GAMEAPI model_vertex_normals(const uint8_t *model, int16_t *out)
{
    int32_t v;

    for (v = 0; v < (int32_t)U16(model, 4); v++) {
        const uint32_t *face = (const uint32_t *)GP(model + 0xc);
        int32_t vec[3] = {0, 0, 0}, x = 0, y = 0, z = 0, used = 0, faces = U16(model, 6), k;

        if (faces > 0) {
            for (; faces; faces--, face += 2) {
                uint32_t corners = face[0];

                for (k = 4; k; k--, corners >>= 8) {
                    if ((corners & 0xff) - 1 == (uint32_t)v) {
                        uint32_t n = face[1];

                        y += (int32_t)((uint32_t)((int32_t)(n << 12) >> 18) << 4);
                        x += (int32_t)((uint32_t)((int32_t)(n << 2) >> 18) << 4);
                        z += (int32_t)((uint32_t)((int32_t)(n << 22) >> 18) << 4);
                        used++;
                    }
                }
            }
            if (used > 0 && (x || y || z)) {
                vec[0] = x;
                vec[1] = y;
                vec[2] = z;
                vec_normalise(vec);
                x = vec[0];
                y = vec[1];
                z = vec[2];
            }
        }
        out[0] = (int16_t)(x >> 4);
        out[1] = (int16_t)(y >> 4);
        out[2] = (int16_t)(z >> 4);
        out += 4;
    }
}

/* 0x40f1e0: a model's vertex normals (+0x48), made once, together with
 * those of its two linked models (offsets +0x1c, +0x1e from it) in one
 * block. */
void GAMEAPI model_normals_make(uint8_t *model)
{
    uint8_t *first = 0, *second = 0;
    uint32_t count;
    int16_t *block;

    if (U32(model, 0x48))
        return;
    count = U16(model, 4);
    if (U16(model, 0x1c)) {
        first = model + U16(model, 0x1c);
        count += U16(first, 4);
    }
    if (U16(model, 0x1e)) {
        second = model + U16(model, 0x1e);
        count += U16(second, 4);
    }
    block = mem_alloc(count * 8);
    PTR_SET(model, 0x48, block);
    model_vertex_normals(model, block);
    if (first) {
        int16_t *at = block + U16(model, 4) * 4;

        PTR_SET(first, 0x48, at);
        model_vertex_normals(first, at);
    }
    if (second) {
        /* sic: reads the first link's count even when there is none */
        int16_t *at = block + ((uint32_t)*(volatile uint16_t *)((uintptr_t)first + 4) + U16(model, 4)) * 4;

        PTR_SET(second, 0x48, at);
        model_vertex_normals(second, at);
    }
}

/* --- pads --- */

/* 0x449bf0: the binding record (0x10 bytes) of control `n` (0..0x1a) of
 * pad set `set` (0..3), or 0. */
uint8_t *GAMEAPI binding_get(uint32_t set, int32_t n)
{
    uint8_t *base;

    switch (set) {
    case 0:
        base = (uint8_t *)0x6bf180;
        break;
    case 1:
        base = (uint8_t *)0x6bf340;
        break;
    case 2:
        base = (uint8_t *)0x6bf500;
        break;
    case 3:
        base = (uint8_t *)0x6bf6c0;
        break;
    default:
        return 0;
    }
    if (n < 0 || n >= 0x1b)
        return 0;
    return base + n * 0x10;
}

/* 0x449d80: control `n` of pad set `set` is bound to a button (+4 == 1). */
int32_t GAMEAPI binding_is_button(uint32_t set, int32_t n)
{
    uint8_t *b = binding_get(set, n);

    return b && I32(b, 4) == 1;
}

/* 0x449d00: the flags a player's pad set gives (when the player's config
 * byte +0x1a3 is set): `steer` for controls 0 or 1 bound to buttons,
 * `accel` for 4, `brake` for 2. */
uint32_t GAMEAPI pad_button_flags(int32_t player, uint32_t unused, uint32_t steer, uint32_t accel, uint32_t brake)
{
    uint32_t flags = 0;

    (void)unused;
    if (player < 0 || player >= 4 || !U8(g_config, player * 0x90 + 0x1a3))
        return 0;
    if (binding_is_button((uint32_t)player, 0) || binding_is_button((uint32_t)player, 1))
        flags = steer;
    if (binding_is_button((uint32_t)player, 4))
        flags |= accel;
    if (binding_is_button((uint32_t)player, 2))
        flags |= brake;
    return flags;
}

/* 0x489ff0: device `n`'s record (0x43c bytes at 0x4d75c0) has bit 8 set. */
uint32_t GAMEAPI device_flag(int32_t n)
{
    return *(uint32_t *)GMEM(0x4d75c0 + n * 0x43c) >> 8 & 1;
}

/* 0x489fa0: device `n` exists (n < *0x4d9554) and is not of type 4. */
int32_t GAMEAPI device_usable(int32_t n)
{
    if (n >= *(int32_t *)0x4d9554)
        return 0;
    return (*(uint32_t *)GMEM(0x4d75c4 + n * 0x43c) & 0xff00) != 0x400;
}

/* 0x428cc0: pad set `set` (< 4) has a device-type binding (+4 == 1 in
 * the 0x10-byte records of the tables at 0x4ac6fc, count *0x4ac70c[set])
 * for device *id whose flag is set. */
int32_t GAMEAPI pad_device_bound(int32_t set, const int16_t *id)
{
    const int32_t *rec;
    int32_t count, i;

    if (set > 3)
        return 0;
    rec = (const int32_t *)(GP(0x4ac6fc + set * 4) + 8);
    count = *(const int32_t *)GP(0x4ac70c + set * 4);
    for (i = 0; i < count; i++, rec += 4)
        if (rec[-1] == 1 && rec[0] == *id && device_flag(rec[0]))
            return 1;
    return 0;
}

/* --- the body allocator's helpers (0x460ba0) --- */

/* 0x460af0: take `size` bytes (rounded up to 0x80) from the link pool
 * *0x6b9b90 (used 0x4d087c). */
uint8_t *GAMEAPI link_pool_take(uint32_t size)
{
    uint32_t used = *(uint32_t *)0x4d087c;

    *(uint32_t *)0x4d087c = used + ((size + 0x7f) & ~0x7fu);
    return GP(0x6b9b90) + (used & ~3u);
}

/* 0x460b20: a grid link record: owner, no links. */
void GAMEAPI grid_link_init(uint8_t *link, uint32_t owner)
{
    U16(link, 4) = 0;
    U16(link, 6) = 0;
    U32(link, 0) = owner;
    U16(link, 8) = 0;
    U32(link, 0xc) = 0;
}

/* 0x460b40: a new body's header: 0x180 bytes of 0xff, then the defaults. */
void GAMEAPI body_header_init(uint8_t *body, uint32_t owner)
{
    uint32_t i;

    for (i = 0; i < 0x60; i++)
        ((uint32_t *)body)[i] = 0xffffffff;
    grid_link_init(body, owner);
    I32(body, 0x118) = 0;
    I32(body, 0x110) = 0;
    I32(body, 0x148) = 0;
    I32(body, 0x11c) = -1;
    U8(body, 0x12f) = 0xff;
    U32(body, 0x16a) = 0;
    U32(body, 0x16e) = 0;
    U16(body, 0x172) = 0;
}

/* 0x460dc0: a body's model instance: `inst` (+0 model data, +4 extra),
 * and the model's faces (`model` +0x10 .. +0x14, 0x28 each, from the
 * model) copied into 0x84-byte records at `faces`; counts instances in
 * `model` +0xa. */
void GAMEAPI model_instance_init(uint32_t *inst, uint8_t *model, uint8_t *faces, uint32_t extra)
{
    const uint8_t *face = model + I32(model, 0x10);
    uint32_t n;

    inst[0] = (uint32_t)(uintptr_t)faces;
    inst[1] = extra;
    n = (uint32_t)(I32(model, 0x14) - I32(model, 0x10)) * 2 / 0x28;
    if ((int32_t)n > 0) {
        for (; n; n--, face += 0x28, faces += 0x84) {
            U32(faces, 4) = U32(face, 4);
            U32(faces, 0x20) = U32(face, 0xc);
            U32(faces, 0x40) = U32(face, 0x14);
            U32(faces, 0x60) = U32(face, 0x1c);
            U32(faces, 0x80) = U32(face, 0x24);
            U16(faces, 0x82) = U16(face, 0x1e);
        }
    }
    U16(model, 0xa)++;
}

/* 0x460e40: carve the face records for a model and its two linked models
 * (`use_*`) out of `at`: two 0x84-byte blocks per face each (out pairs
 * set to -1 when unused); returns the end. */
uint8_t *GAMEAPI model_faces_carve(uint8_t *model, uint8_t *at, int32_t use0, int32_t use1, int32_t use2,
                                  uint8_t **first0, uint8_t **second0, uint8_t **first1, uint8_t **second1,
                                  uint8_t **first2, uint8_t **second2)
{
    uint32_t link1, link2, n;

    if (!model)
        return at;
    link1 = U16(model, 0x1c);
    link2 = U16(model, 0x1e);
    *first0 = (uint8_t *)-1;
    *second0 = (uint8_t *)-1;
    *first1 = (uint8_t *)-1;
    *second1 = (uint8_t *)-1;
    *first2 = (uint8_t *)-1;
    *second2 = (uint8_t *)-1;
    if (use0) {
        *first0 = at;
        n = (uint32_t)(I32(model, 0x14) - I32(model, 0x10)) / 0x28;
        *second0 = at + n * 0x84;
        at += n * 0x108;
    }
    if (use1) {
        *first1 = at;
        n = (uint32_t)(I32(model + link1, 0x14) - I32(model + link1, 0x10)) / 0x28;
        *second1 = at + n * 0x84;
        at += n * 0x108;
    }
    if (use2) {
        *first2 = at;
        n = (uint32_t)(I32(model + link2, 0x14) - I32(model + link2, 0x10)) / 0x28;
        *second2 = at + n * 0x84;
        at += n * 0x108;
    }
    return at;
}

/* 0x460f50: add a body to the body list (0x4d0880, up to 0x200; count
 * 0x4d1088), its index at +0x10e. */
void GAMEAPI body_list_add(uint8_t *body)
{
    int32_t n = *(int32_t *)0x4d1088;

    if (n >= 0x200)
        return;
    PTR_SET((uint8_t *)0x4d0880, n * 4, body);
    U16(body, 0x10e) = (uint16_t)n;
    *(int32_t *)0x4d1088 = n + 1;
}
