/* Group PK: the original's still-reachable functions in 0x458760-0x45bbf0
 * (the end of the weapons source file and the model drawing / lighting
 * source file).
 *
 *  - the incoming-missile level (0x458760): the bit length of the squared
 *    distance to the first live weapon object locked on a body;
 *  - lighting: the light colour matrix from three light records (0x459cb0
 *    by packed ids into 0x5d8000, 0x45b200 by pointers), a light's
 *    direction and strength at a body (0x45ba30), the light direction
 *    matrix in the body's frame and the body's tint (0x45b400, with the
 *    tint fade 0x45b390 / 0x45b9f0);
 *  - the model's GTE set-up (0x459e80: camera * body matrix, the body's
 *    position in camera space), its depth (0x45a2a0), the draw-with-light
 *    wrapper (0x45a260);
 *  - the model packet builders: flat-lit faces (0x45a430: a light colour
 *    per face from its packed normal) and smooth (0x45ab20: a colour per
 *    vertex normal), into the body's own packets or, by level of detail,
 *    the shared packet area 0x6c1e18 .. 0x6c1e24 with textures from
 *    0x6bfb58; the texture corner rotation 0x45a2f0;
 *  - the kind-5 track object: its trigger (0x45bbf0) and its hit callback
 *    (0x45bb70, not in the group's list: Ghidra has no function there, but
 *    0x45bbf0 installs it at +0x148).
 *
 * Float steps are the original's x87 instructions in inline asm: the GTE
 * matrix load's int16 * scale (fild / fstp / fld / fmul / fstp), int ->
 * float (fild / fstp), the depth compare (fcomp / fnstsw) and its fistp,
 * the facing value (0x4278f0, returned in st(0) and stored with fstp) and
 * its fchs. The transform / projection pair *0x6ccd78 / *0x6ccd80 hands
 * st(0) from one to the other, so each pair is one asm block. Everything
 * outside the group, and the group's functions where the original calls
 * them, is called by address. */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

/* x87 asm entries in src/game/orig_pc.c, called by address with the
 * prototypes their callers use: 0x426620 gte_rt_float (no arguments),
 * 0x4290b0 vec_normalise_x87 (in, out; returns 0), and through 0x6cce3c
 * the light colour (0x427400, 3DNow! 0x427250; no arguments). */
typedef void(GAMEAPI *pk_void_fn)(void);
typedef int32_t(GAMEAPI *pk_normalise_fn)(int32_t *in, int32_t *out);

#define light_matrix_load_at(m) GCALL(0x428940, light_matrix_load)(m)
#define gte_rotate_short_at() GCALL(0x427fc0, gte_rotate_short)()
#define gte_rt_float_at() ((pk_void_fn)(uintptr_t)0x426620)()
#define gte_square_ir_at() GCALL(0x4282d0, gte_square_ir)()
#define vec_normalise_at(in, out) ((pk_normalise_fn)(uintptr_t)0x4290b0)(in, out)
#define palette_get_at() GCALL(0x486350, weather_palette_get)()
#define quat_facing_at(q, v) GCALL(0x40ee60, quat_facing)((int32_t *)(q), v)
#define body_sparks_at(b, n) GCALL(0x45bd30, body_sparks)(b, n)
#define contact_particle_at(b, p, k) GCALL(0x45be10, contact_particle)(b, p, k)
#define explosion_effects_at(p, v, a, b, c) GCALL(0x42aa30, explosion_effects)(p, v, a, b, c)
#define gte_light_colour_at() ((pk_void_fn)(uintptr_t)G32(0x6cce3c))()
/* this group's own, called by address as the original does */
#define model_camera_matrix_set_at(b) GCALL(0x459e80, model_camera_matrix_set)(b)
#define model_view_depth_at(b) GCALL(0x45a2a0, model_view_depth)(b)
#define model_lights_set_at(b, l) GCALL(0x45b400, model_lights_set)(b, (const uint32_t *)PTR(l))
#define model_faces_draw_at(b, d) GCALL(0x45a430, model_faces_draw)(b, d)
#define model_faces_draw_smooth_at(b, d) GCALL(0x45ab20, model_faces_draw_smooth)(b, d)
#define prim_uv_rotate_at(p, k) GCALL(0x45a2f0, prim_uv_rotate)(p, k)
#define light_direction_at_at(p, l, o) GCALL(0x45ba30, light_direction_at)((const int32_t *)(p), (const int32_t *)PTR(l), o)
#define body_tint_approach_at(b, p) GCALL(0x45b390, body_tint_approach)(b, (uint8_t *)(p))
#define tint_byte_approach_at(a, b) ((uint8_t)GCALL(0x45b9f0, tint_byte_approach)(a, b))

/* This group's functions (also in build/groups/pk_protos.txt for physics.h). */
int32_t GAMEAPI missile_lock_level(const uint32_t *who); /* 0x458760 */
void GAMEAPI light_colours_from_ids(uint32_t ids, uint32_t *out); /* 0x459cb0 */
void GAMEAPI model_camera_matrix_set(uint8_t *body); /* 0x459e80 */
void GAMEAPI model_draw_lit(uint8_t *body, uint32_t lights); /* 0x45a260 */
int32_t GAMEAPI model_view_depth(uint8_t *body); /* 0x45a2a0 */
void GAMEAPI prim_uv_rotate(uint8_t *prim, int32_t k); /* 0x45a2f0 */
void GAMEAPI model_faces_draw(uint8_t *body, int32_t depth); /* 0x45a430 */
void GAMEAPI model_faces_draw_smooth(uint8_t *body, int32_t depth); /* 0x45ab20 */
void GAMEAPI light_colours_set(const uint32_t *lights); /* 0x45b200 */
void GAMEAPI body_tint_approach(uint8_t *body, uint8_t *tint); /* 0x45b390 */
void GAMEAPI model_lights_set(uint8_t *body, const uint32_t *lights); /* 0x45b400 */
uint32_t GAMEAPI tint_byte_approach(uint32_t cur, uint32_t want); /* 0x45b9f0 */
void GAMEAPI light_direction_at(const int32_t *pos, const int32_t *light, int32_t *out); /* 0x45ba30 */
int32_t GAMEAPI k5_object_hit(uint8_t *self, uint8_t *obj, uint8_t *hit); /* 0x45bb70 */
int32_t GAMEAPI k5_object_trigger(uint8_t *body); /* 0x45bbf0 */

/* Every x87 register, for asm blocks that call out. */
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* ---- x87 helpers: the original's instructions ---- */

/* fild dword; fstp dword: an integer to float, rounded by the control word. */
static inline uint32_t pk_fild(uint32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild word w; fstp; fld; fmul dword k; fstp (the fstp / fld of the integer
 * as a float in between is exact for 16-bit values). */
static inline uint32_t pk_s16_mul(uint16_t w, uint32_t k)
{
    uint32_t r;
    __asm__ volatile("filds %1\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(w), "m"(k) : "st");
    return r;
}

/* fld dword; fstp dword: a float copied through the FPU. */
static inline uint32_t pk_fcopy(uint32_t a)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfstps %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld; fchs; fstp. */
static inline uint32_t pk_fchs(uint32_t a)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfchs\n\tfstps %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld a; fcomp b; fnstsw ax: C0 (a below b, or unordered). */
static inline int pk_below(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 1;
}

/* fld a; fistp dword: a rounded by the control word. */
static inline int32_t pk_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* The transform *0x6ccd78 (vertex record out, the GTE vector 0x6ccc64 in)
 * then the projection *0x6ccd80, which takes the transform's st(0). */
static inline void pk_transform_project(void *rec)
{
#if defined(__i386__)
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tcall *0x6ccd80"
                     :
                     : "S"(rec)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    /* x86-64: the transform (*0x6ccd78: 0x426a20, the subunit wrapper's C)
     * returns what the original leaves in st(0); the projection (*0x6ccd80:
     * 0x426b00, src/widescreen.c) takes it as its argument. */
    float h = ((float(GAMEAPI *)(uint8_t *))(uintptr_t)G32(0x6ccd78))((uint8_t *)rec);

    ((void(GAMEAPI *)(float))(uintptr_t)G32(0x6ccd80))(h);
#endif
}

/* 0x4278f0(a, b, c): the facing value of three projected vertex records,
 * returned in st(0), stored as a single (fstp dword). */
typedef float(GAMEAPI *pk_facing_fn)(void *a, void *b, void *c);

static uint32_t pk_facing(void *a, void *b, void *c)
{
    float f = ((pk_facing_fn)(uintptr_t)0x4278f0)(a, b, c);
    uint32_t r;

    memcpy(&r, &f, 4);
    return r;
}

/* The GTE matrix (five dwords at 0x6ccbe0, int16 3x3 and a pad word) from
 * m, and its floats (each * the scale 0x4a3310, rows of four at
 * 0x6ccd1c). */
static void pk_gte_matrix_load(const void *m)
{
    static const uint32_t dst[9] = {0x6ccd1c, 0x6ccd20, 0x6ccd24, 0x6ccd2c, 0x6ccd30,
                                    0x6ccd34, 0x6ccd3c, 0x6ccd40, 0x6ccd44};
    const uint32_t *s = (const uint32_t *)m;
    int i;

    for (i = 0; i < 5; i++)
        G32(0x6ccbe0 + 4 * i) = s[i];
    for (i = 0; i < 9; i++)
        G32(dst[i]) = pk_s16_mul(G16(0x6ccbe0 + 2 * i), G32(0x4a3310));
}

/* The GTE translation (0x6ccbf4, ints and floats at 0x6ccd0c). */
static void pk_gte_translation_set(uint32_t x, uint32_t y, uint32_t z)
{
    G32(0x6ccbf4) = x;
    G32(0x6ccbf8) = y;
    G32(0x6ccbfc) = z;
    G32(0x6ccd0c) = pk_fild(G32(0x6ccbf4));
    G32(0x6ccd10) = pk_fild(G32(0x6ccbf8));
    G32(0x6ccd14) = pk_fild(G32(0x6ccbfc));
}

/* m (int16 3x3) = GTE matrix * the body's matrix (+0xc4), a column at a
 * time through the short vector 0x6ccc88 (0x427fc0). */
static void pk_body_matrix_rotate(const uint8_t *body, int16_t *m)
{
    int j;

    for (j = 0; j < 3; j++) {
        G16(0x6ccc88) = U16(body, 0xc4 + 2 * j);
        G16(0x6ccc8c) = U16(body, 0xca + 2 * j);
        G16(0x6ccc90) = U16(body, 0xd0 + 2 * j);
        gte_rotate_short_at();
        m[j] = (int16_t)G16(0x6ccc88);
        m[3 + j] = (int16_t)G16(0x6ccc8c);
        m[6 + j] = (int16_t)G16(0x6ccc90);
    }
}

/* The light colour (0x6ccc14 / 18 / 1c, floats 0x6cce30 / 34 / 38) from a
 * 0xbbggrr colour: each byte << 4 | 0xf. */
static void pk_light_colour_set(uint32_t v)
{
    G32(0x6ccc14) = (v & 0xff) << 4 | 0xf;
    G32(0x6ccc18) = ((v & 0xff00) | 0xf0) >> 4;
    G32(0x6ccc1c) = ((v & 0xff0000) | 0xf000) >> 12;
    G32(0x6cce38) = pk_fild(G32(0x6ccc1c));
    G32(0x6cce34) = pk_fild(G32(0x6ccc18));
    G32(0x6cce30) = pk_fild(G32(0x6ccc14));
}

/* ===================================================================== */
/* The weapons. */

/* 0x458760: the incoming-missile level for *who: the first of the 24
 * weapon objects (0x6b9ce0) that is live (+0x188 nonzero, none of 0x3c)
 * and locked on it (+0x190); none: 0. Otherwise the squared distance
 * (position differences >> 20, squared, summed, >> 8, made positive) goes
 * to 0x6cccdc and its leading zero count to 0x6ccce0; returns its bit
 * length - 1, at least 3. */
int32_t GAMEAPI missile_lock_level(const uint32_t *who)
{
    uint32_t body = who[0];
    const uint32_t *list = (const uint32_t *)PTR(0x6b9ce0);
    uint32_t w, f, mask, d;
    int32_t n, dx, dy, dz;

    for (n = 0x18;;) {
        w = *list;
        f = U32(w, 0x188);
        if (f != 0 && !(f & 0x3c) && U32(w, 0x190) == body)
            break;
        n--;
        list++;
        if (n <= 0)
            return 0;
    }
    dx = (I32(w, 0x34) - I32(body, 0x34)) >> 20;
    dy = (I32(w, 0x38) - I32(body, 0x38)) >> 20;
    dz = (I32(w, 0x3c) - I32(body, 0x3c)) >> 20;
    d = (uint32_t)((int32_t)((uint32_t)dz * (uint32_t)dz + (uint32_t)dy * (uint32_t)dy + (uint32_t)dx * (uint32_t)dx) >>
                   8);
    if (d & 0x80000000u)
        d = 0u - d;
    G32(0x6cccdc) = d;
    for (n = 0, mask = 0x80000000u; n < 0x20 && !(mask & d); n++)
        mask >>= 1;
    G32(0x6ccce0) = (uint32_t)n;
    return 0x1f - n >= 3 ? 0x1f - n : 3;
}

/* ===================================================================== */
/* Lighting. */

/* One light's column of the light colour matrix from its packed colour
 * (+0xc: red byte, green / blue 14-bit fields), each scaled to at most
 * 0xfff. */
static void pk_light_column(int16_t *m, int col, uint32_t c)
{
    uint32_t r = (c & 0xff) << 6;

    m[col] = 0xfff;
    if (r <= 0xfff)
        m[col] = (int16_t)r;
    m[3 + col] = (c & 0xfffc) > 0x3ffc ? 0xfff : (int16_t)((c >> 2) & 0x3fff);
    m[6 + col] = (c & 0xfffc00) > 0x3ffc00 ? 0xfff : (int16_t)((c >> 10) & 0x3fff);
}

/* 0x459cb0: the light colour matrix (0x428940) from three 10-bit light ids
 * packed in ids (records of 0x40 at 0x5d8000; id 0: none, its column 0);
 * out[0..2] = the three records (or 0). */
void GAMEAPI light_colours_from_ids(uint32_t ids, uint32_t *out)
{
    union {
        uint32_t d[5];
        int16_t w[10];
    } m;
    uint32_t a = 0, b = 0, c = 0;

    memset(&m, 0, sizeof m);
    if (ids & 0x3ff)
        a = 0x5d8000 + (ids & 0x3ff) * 0x40;
    if ((ids >> 10) & 0x3ff)
        b = 0x5d8000 + ((ids >> 10) & 0x3ff) * 0x40;
    if ((ids >> 20) & 0x3ff)
        c = 0x5d8000 + ((ids >> 20) & 0x3ff) * 0x40;
    if (a)
        pk_light_column(m.w, 0, U32(a, 0xc));
    if (b)
        pk_light_column(m.w, 1, U32(b, 0xc));
    if (c)
        pk_light_column(m.w, 2, U32(c, 0xc));
    light_matrix_load_at(m.d);
    out[0] = a;
    out[1] = b;
    out[2] = c;
}

/* 0x45b200: the light colour matrix (0x428940) from three light records
 * (lights[0..2], 0: none). */
void GAMEAPI light_colours_set(const uint32_t *lights)
{
    union {
        uint32_t d[5];
        int16_t w[10];
    } m;
    uint32_t a = lights[0], b = lights[1], c = lights[2];

    memset(&m, 0, sizeof m);
    if (a)
        pk_light_column(m.w, 0, U32(a, 0xc));
    if (b)
        pk_light_column(m.w, 1, U32(b, 0xc));
    if (c)
        pk_light_column(m.w, 2, U32(c, 0xc));
    light_matrix_load_at(m.d);
}

/* 0x45ba30: the light's direction at pos, scaled by its strength: out =
 * pos - light (high words); if any component's size is over 0x4000, or the
 * squared length (0x4282d0) >> light byte +0x10 is over 0x3ff, out = 0.
 * Otherwise the strength *0x4d4b9c[that] * 4 (at most 0x1fffc) times the
 * normalised direction (0x4290b0), / 0x8000 rounded toward zero. */
void GAMEAPI light_direction_at(const int32_t *pos, const int32_t *light, int32_t *out)
{
    int32_t x, y, z, e, k, w, i;
    uint32_t size;

    out[0] = (pos[0] >> 16) - (light[0] >> 16);
    out[1] = (pos[1] >> 16) - (light[1] >> 16);
    z = (pos[2] >> 16) - (light[2] >> 16);
    out[2] = z;
    x = out[0];
    y = out[1];
    size = (uint32_t)(((y ^ (y >> 31)) - (y >> 31)) | ((z ^ (z >> 31)) - (z >> 31)) | ((x ^ (x >> 31)) - (x >> 31)));
    if ((int32_t)size > 0x4000) {
        out[2] = 0;
        out[1] = 0;
        out[0] = 0;
        return;
    }
    G16(0x6ccc88) = (uint16_t)out[0];
    G16(0x6ccc8c) = (uint16_t)out[1];
    G16(0x6ccc90) = (uint16_t)out[2];
    gte_square_ir_at();
    e = (int32_t)(G32(0x6ccccc) + G32(0x6cccd0) + G32(0x6cccc8)) >> (U8(light, 0x10) & 31);
    if (e > 0x3ff) {
        out[2] = 0;
        out[1] = 0;
        out[0] = 0;
        return;
    }
    w = I32(G32(0x4d4b9c), e * 4);
    vec_normalise_at(out, out);
    k = (int32_t)((uint32_t)w * 4);
    if (k > 0x1fffc)
        k = 0x1fffc;
    for (i = 0; i < 3; i++) {
        int32_t p = (int32_t)((uint32_t)out[i] * (uint32_t)k);

        out[i] = (int32_t)((uint32_t)p + ((uint32_t)(p >> 31) & 0x7fff)) >> 15;
    }
}

/* 0x45b9f0: one tint byte moved toward want: want if within 24 of cur,
 * otherwise cur + 24 or cur - 24 (bytes, wrapping). */
uint32_t GAMEAPI tint_byte_approach(uint32_t cur, uint32_t want)
{
    uint32_t a = cur & 0xff, b = want & 0xff;
    int32_t d = (int32_t)(a - b);

    if (((d ^ (d >> 31)) - (d >> 31)) <= 0x18)
        return b;
    if (a < b)
        return (a + 0x18) & 0xff;
    return (a - 0x18) & 0xff;
}

/* 0x45b390: the body's tint (+0x12c, 0xbbggrr) moved toward tint[0..2]
 * (0x45b9f0 a byte at a time, tint updated in place), then stored; a tint
 * with top byte 0xff is replaced outright. */
void GAMEAPI body_tint_approach(uint8_t *body, uint8_t *tint)
{
    uint32_t cur = U32(body, 0x12c);

    if ((cur >> 24) == 0xff) {
        U32(body, 0x12c) = U32(tint, 0);
        return;
    }
    tint[0] = tint_byte_approach_at(cur, tint[0]);
    tint[1] = tint_byte_approach_at(cur >> 8, tint[1]);
    tint[2] = tint_byte_approach_at(cur >> 16, tint[2]);
    U32(body, 0x12c) = U32(tint, 0);
}

/* 0x45b400: the light direction matrix: rows = the three lights' direction
 * and strength at the body (0x45ba30; no light: 0), times the body's matrix
 * through the GTE (its matrix saved and restored), to 0x6ccc00 and floats
 * at 0x6ccdd0. A body with flag 0x800 (the track's own lighting) also gets
 * its light colour: from its track cell (+0x11c: map, column, row; the
 * cell's colour index into the palette 0x486350, 0x20202020 past 0x3f0)
 * faded by its tint (0x45b390), or 0x400 each for cell -1. */
void GAMEAPI model_lights_set(uint8_t *body, const uint32_t *lights)
{
    union {
        uint32_t d[5];
        int16_t w[10];
    } m;
    uint32_t pal = palette_get_at();
    uint32_t l0 = lights[0], l1 = lights[1], l2 = lights[2];
    uint32_t saved[5];
    int32_t v[3];
    int i;

    memset(&m, 0, sizeof m);
    if (l0) {
        light_direction_at_at(body + 0x34, l0, v);
        m.w[2] = (int16_t)v[2];
        m.w[0] = (int16_t)v[0];
        m.w[1] = (int16_t)v[1];
    }
    if (l1) {
        light_direction_at_at(body + 0x34, l1, v);
        m.w[4] = (int16_t)v[1];
        m.w[3] = (int16_t)v[0];
        m.w[5] = (int16_t)v[2];
    }
    if (l2) {
        light_direction_at_at(body + 0x34, l2, v);
        m.w[6] = (int16_t)v[0];
        m.w[7] = (int16_t)v[1];
        m.w[8] = (int16_t)v[2];
    }
    for (i = 0; i < 5; i++)
        saved[i] = G32(0x6ccbe0 + 4 * i);
    pk_gte_matrix_load(m.d);
    pk_body_matrix_rotate(body, m.w);
    pk_gte_matrix_load(saved);
    {
        static const uint32_t dst[9] = {0x6ccdd0, 0x6ccdd4, 0x6ccdd8, 0x6ccde0, 0x6ccde4,
                                        0x6ccde8, 0x6ccdf0, 0x6ccdf4, 0x6ccdf8};

        for (i = 0; i < 5; i++)
            G32(0x6ccc00 + 4 * i) = m.d[i];
        for (i = 0; i < 9; i++)
            G32(dst[i]) = pk_s16_mul(G16(0x6ccc00 + 2 * i), G32(0x4a3310));
    }
    if (U32(body, 0) & 0x800) {
        uint32_t cell = U32(body, 0x11c), map, idx, tint;

        if (cell == 0xffffffffu) {
            G32(0x6ccc14) = 0x400;
            G32(0x6ccc18) = 0x400;
            G32(0x6ccc1c) = 0x400;
            G32(0x6cce38) = pk_fild(G32(0x6ccc1c));
            G32(0x6cce34) = pk_fild(G32(0x6ccc18));
            G32(0x6cce30) = pk_fild(G32(0x6ccc14));
            return;
        }
        map = U32(G32(0x6bfac0), (cell & 0xff) * 4);
        idx = (uint32_t)((int32_t)(U32(map, 8) + 1) * (int32_t)(int16_t)(cell >> 16) + (int32_t)(int8_t)(cell >> 8));
        idx = U32(U32(map, 0x20), idx * 8 + 4) >> 22;
        tint = idx < 0x3f0 ? U32(pal, idx * 4) : 0x20202020u;
        tint &= 0xffffff;
        body_tint_approach_at(body, &tint);
        pk_light_colour_set(tint);
    }
}

/* ===================================================================== */
/* The model's set-up. */

/* 0x459e80: the GTE for drawing a body: matrix = camera (0x6ce5d0) * the
 * body's matrix, translation = the body's position (high words of +0x34 /
 * +0x38 / +0x3c) in camera space (0x426620 with the camera translation).
 * The original's local matrix pad word (into 0x6ccbf2) is an unwritten
 * stack word; 0 here. */
void GAMEAPI model_camera_matrix_set(uint8_t *body)
{
    union {
        uint32_t d[5];
        int16_t w[10];
    } m;

    memset(&m, 0, sizeof m);
    pk_gte_matrix_load(PTR(0x6ce5d0));
    pk_body_matrix_rotate(body, m.w);
    pk_gte_translation_set(G32(0x6ce5e4), G32(0x6ce5e8), G32(0x6ce5ec));
    G16(0x6ccc68) = U16(body, 0x3e);
    G16(0x6ccc64) = U16(body, 0x36);
    G16(0x6ccc66) = U16(body, 0x3a);
    gte_rt_float_at();
    pk_gte_translation_set(G32(0x6cccc8), G32(0x6ccccc), G32(0x6cccd0));
    pk_gte_matrix_load(m.d);
}

/* 0x45a2a0: the body's depth: the GTE set up (0x459e80), the model's
 * centre (+0x28 / +0x2c) transformed and projected; its z rounded. */
int32_t GAMEAPI model_view_depth(uint8_t *body)
{
    uint32_t rec[6];
    uint32_t model;

    model_camera_matrix_set_at(body);
    model = U32(body, 0x10);
    G32(0x6ccc64) = U32(model, 0x28);
    G32(0x6ccc68) = U32(model, 0x2c);
    pk_transform_project(rec);
    return pk_fist(rec[1]);
}

/* 0x45a260: draw a body's model lit: its depth (0x45a2a0); from -1000 on,
 * its lights (0x45b400) and faces (0x45a430). */
void GAMEAPI model_draw_lit(uint8_t *body, uint32_t lights)
{
    int32_t depth = model_view_depth_at(body);

    if (depth < -1000)
        return;
    model_lights_set_at(body, lights);
    model_faces_draw_at(body, depth);
}

/* 0x45a2f0: rotate / mirror a textured quad's four corner texture words
 * (+0x20, +0x40, +0x80, +0x60 in corner order) by k (1..7). */
void GAMEAPI prim_uv_rotate(uint8_t *prim, int32_t k)
{
    uint16_t t;

    switch (k) {
    case 1:
        t = U16(prim, 0x20);
        U16(prim, 0x20) = U16(prim, 0x40);
        U16(prim, 0x40) = U16(prim, 0x80);
        U16(prim, 0x80) = U16(prim, 0x60);
        U16(prim, 0x60) = t;
        break;
    case 2:
        t = U16(prim, 0x20);
        U16(prim, 0x20) = U16(prim, 0x80);
        U16(prim, 0x80) = t;
        t = U16(prim, 0x40);
        U16(prim, 0x40) = U16(prim, 0x60);
        U16(prim, 0x60) = t;
        break;
    case 3:
        t = U16(prim, 0x20);
        U16(prim, 0x20) = U16(prim, 0x60);
        U16(prim, 0x60) = U16(prim, 0x80);
        U16(prim, 0x80) = U16(prim, 0x40);
        U16(prim, 0x40) = t;
        break;
    case 4:
        t = U16(prim, 0x20);
        U16(prim, 0x20) = U16(prim, 0x60);
        U16(prim, 0x60) = t;
        t = U16(prim, 0x40);
        U16(prim, 0x40) = U16(prim, 0x80);
        U16(prim, 0x80) = t;
        break;
    case 5:
        t = U16(prim, 0x20);
        U16(prim, 0x20) = U16(prim, 0x80);
        U16(prim, 0x80) = t;
        break;
    case 6:
        t = U16(prim, 0x20);
        U16(prim, 0x20) = U16(prim, 0x40);
        U16(prim, 0x40) = t;
        t = U16(prim, 0x60);
        U16(prim, 0x60) = U16(prim, 0x80);
        U16(prim, 0x80) = t;
        break;
    case 7:
        t = U16(prim, 0x40);
        U16(prim, 0x40) = U16(prim, 0x60);
        U16(prim, 0x60) = t;
        break;
    default:
        break;
    }
}

/* ===================================================================== */
/* The model packet builders. */

/* What both builders share: the level of detail, the face list, the
 * packets, the texture list. */
struct pk_model {
    int32_t mode;             /* 0..2 by depth (shared packets, textured), -1 with flag 0x800 */
    uint8_t *model;           /* the model, at its level of detail */
    const uint32_t *faces;    /* 8 bytes a face: 4 vertex indices (0: none), the packed normal */
    uint32_t nverts;          /* vertices - 1 */
    uint8_t *prim;            /* the next packet (0x84 bytes) */
    const uint16_t *tex;      /* mode >= 0: texture ids, one a face */
    const uint16_t *runs;     /* mode >= 0: the face run lengths */
    uint32_t limit, texbase;
};

/* The level of detail: near 6000, far 15000 (* 0x6cd404 / 256, doubled with
 * option byte +0x31 == 1). Mode -1 (flag 0x800) draws into the body's own
 * packets (+0x14 / +0x1c / +0x14c, or +0x18 / +0x20 / +0x150 for the other
 * buffer); otherwise into the shared area from 0x6c1e18. */
static void pk_model_lod(struct pk_model *s, uint8_t *body, int32_t depth, int32_t near, int32_t far)
{
    uint8_t *model = PTR(U32(body, 0x10));
    uint32_t base_off;
    int other = G32(0x6ce60c) != 0x6cd4a0;

    if (U8(G32(0x4b2198), 0x31) == 1) {
        near = (int32_t)((uint32_t)near << 1);
        far = (int32_t)((uint32_t)far << 1);
    }
    base_off = U16(model, 0x1c);
    if (!(U32(body, 0) & 0x800))
        s->mode = depth > near ? (depth > far) + 1 : 0;
    else
        s->mode = -1;
    if (depth > near) {
        if (depth > far) {
            uint32_t o = U16(model, 0x1e);

            model += o ? o : base_off;
            s->faces = (const uint32_t *)PTR(U32(body, 0x30));
            s->nverts = U8(body, 0x163);
            s->prim = PTR(other ? U32(body, 0x150) : U32(body, 0x14c));
        } else {
            s->faces = (const uint32_t *)PTR(U32(body, 0x2c));
            s->nverts = U8(body, 0x162);
            model += base_off;
            s->prim = PTR(other ? U32(body, 0x20) : U32(body, 0x1c));
        }
    } else {
        s->prim = PTR(other ? U32(body, 0x18) : U32(body, 0x14));
        s->faces = (const uint32_t *)PTR(U32(body, 0x28));
        s->nverts = U8(body, 0x10d);
    }
    s->model = model;
    s->tex = 0;
    s->runs = 0;
    if (s->mode >= 0) {
        s->prim = PTR(G32(0x6c1e18));
        s->tex = (const uint16_t *)PTR(G32(0x6bfb58) + U16(body, 0x1a0 + s->mode * 2) * 2);
        s->runs = (const uint16_t *)PTR(U32(model, 0x10) + A(model));
    }
}

/* A shared packet's texture: the face's texture id t: its entry at
 * texbase + t / 4 (+2: 0xffff, or an animation index into 0x6bfb44
 * records of 0x18 whose +6 frame indexes 0x6bfb4c entries of 6 bytes, +4
 * zero: follow +0 once; the entry's +0 indexes 0x6c1e14 corner sets of
 * 0x10, the index's high byte a corner rotation for 0x45a2f0). */
static void pk_prim_texture(const struct pk_model *s, uint8_t *prim)
{
    uint16_t t = *s->tex;
    uint8_t *uv = PTR(s->texbase + (t >> 2));
    uint32_t rot = 0, x;
    uint16_t a = U16(uv, 2);

    if (a != 0xffff) {
        uint32_t k = U16(G32(0x6bfb44), (a & 0xff) * 0x18 + 6);
        uint8_t *e = PTR(G32(0x6bfb4c) + k * 6);

        if (S16(e, 4) == 0)
            e = PTR(G32(0x6bfb4c) + (uint32_t)(int32_t)S16(e, 0) * 6);
        rot = (uint32_t)a >> 8;
        uv = PTR((uint32_t)(int32_t)S16(e, 0) * 16 + G32(0x6c1e14));
    }
    U32(prim, 0x20) = U32(uv, 0);
    U32(prim, 0x40) = U32(uv, 4);
    x = U32(uv, 8);
    U16(prim, 0x60) = (uint16_t)x;
    U16(prim, 0x80) = (uint16_t)(x >> 16);
    if (rot)
        prim_uv_rotate_at(prim, rot);
}

/* A face's ordering table entry from its farthest corner (z: the largest
 * of a, b, c and d (if any), fcomp, kept on ties by the later; fistp) / 32,
 * compressed above 0x100 to at most 0x1ff, at least 1: 0x6cd510 (0x6cdd80
 * for semi-transparent faces, with 0x6cce84); with 0x6cce84 opaque faces go
 * by the packet's byte +0x42 & 0x3f into 0x6cd518 (8 bytes a slot). The
 * packet is linked at the head. */
static void pk_prim_link(uint8_t *prim, const uint8_t *a, const uint8_t *b, const uint8_t *c, const uint8_t *d,
                         int semi)
{
    uint32_t m, ent;
    int32_t z;

    m = pk_below(U32(b, 4), U32(a, 4)) ? pk_fcopy(U32(a, 4)) : pk_fcopy(U32(b, 4));
    if (!pk_below(U32(c, 4), m))
        m = pk_fcopy(U32(c, 4));
    if (d && !pk_below(U32(d, 4), m))
        m = pk_fcopy(U32(d, 4));
    z = pk_fist(m) >> 5;
    if (z > 0x100) {
        z = z / 2 + 0x80;
        if (z > 0x180) {
            z = z / 2 + 0xc0;
            if (z > 0x1ff)
                z = 0x1ff;
        }
    }
    if (z < 1)
        z = 1;
    if (semi)
        ent = G32(0x6cce84) ? G32(0x6cdd80 + z * 4) : G32(0x6cd510 + z * 4);
    else
        ent = G32(0x6cce84) ? G32(0x6cd518 + (U8(prim, 0x42) & 0x3f) * 8) : G32(0x6cd510 + z * 4);
    ent &= 0x7fffffff;
    U32(prim, 0) = G32(ent);
    G32(ent) = A(prim);
}

#define PK_VREC(buf, i) ((buf) + (i) * 0x18)

/* 0x45a430: build a model's packets with a light colour per face: the
 * vertices (body +0x24, 8 bytes each) transformed and projected into
 * 1-based records of 0x18; per face (8 bytes: indices a, b, c, d (0: a
 * triangle), the packed normal with bit 31 semi-transparent), in runs (the
 * packet's +0x82, or the model's run list with its texture list in shared
 * mode): the facing value (0x4278f0, negated with 0x6cce94; -0.1 and vertex
 * b marked 0x80 when one is behind the near plane), skipped when not
 * facing, with the body's flag word +8 bit 0, or when all corners share a
 * clip code (& 0x3f). Otherwise the normal's light colour (*0x6cce3c), the
 * corners copied, the texture (shared mode) and colour set, the packet
 * linked by depth. A run's low bits: 2 shifts the flag word, 4 ends the
 * model. Shared mode: the face colours come from the body's colour runs
 * (+0x180 + 4 * mode) through the palette (0x486350), and the shared area
 * ends at 0x6c1e24. A model with vertex normals (+0x48) goes to 0x45ab20.
 * Vertex index 0 (below the original's buffer: stack garbage) reads a zero
 * record here. */
void GAMEAPI model_faces_draw(uint8_t *body, int32_t depth)
{
    uint8_t vbuf[(0x100 + 1) * 0x18];
    struct pk_model s;
    int32_t mirror = GI32(0x6cce94);
    int32_t ccount = 0xffff, near, far, count;
    const uint16_t *cols = (const uint16_t *)PTR(0xffffffff);
    uint32_t pal, i, n;
    const uint32_t *verts, *face;
    uint16_t flag8;

    memset(vbuf, 0, 0x18);
    pal = palette_get_at();
    s.texbase = G32(0x6bfb5c);
    s.limit = G32(0x6c1e24);
    near = (int32_t)(G32(0x6cd404) * 6000u) >> 8;
    far = (int32_t)(G32(0x6cd404) * 15000u) >> 8;
    if (U32(U32(body, 0x10), 0x48) != 0) {
        model_faces_draw_smooth_at(body, depth);
        return;
    }
    pk_model_lod(&s, body, depth, near, far);
    if (s.mode >= 0) {
        ccount = 0;
        cols = (const uint16_t *)PTR(U32(body, 0x180 + 4 * s.mode));
    }
    verts = (const uint32_t *)PTR(U32(body, 0x24));
    flag8 = U16(body, 8);

    n = s.nverts;
    G32(0x6ccc64) = verts[0];
    G32(0x6ccc68) = verts[1];
    pk_transform_project(PK_VREC(vbuf, 1));
    for (i = 1; i <= n; i++) {
        G32(0x6ccc64) = verts[2 * i];
        G32(0x6ccc68) = verts[2 * i + 1];
        pk_transform_project(PK_VREC(vbuf, i + 1));
    }

    face = s.faces;
    for (;;) {
        count = s.mode < 0 ? U16(s.prim, 0x82) : *s.runs++;
        do {
            uint32_t w;

            if (--ccount <= 0) {
                uint16_t c = *cols++;

                ccount = c & 0xff;
                pk_light_colour_set(U32(pal, (c >> 8) * 4));
            }
            w = face[0];
            face += 2;
            count -= 8;
            if (w != 0) {
                uint32_t id = w >> 24, f, nrm, semi;
                uint8_t *a = PK_VREC(vbuf, w & 0xff), *b = PK_VREC(vbuf, (w >> 8) & 0xff);
                uint8_t *c = PK_VREC(vbuf, (w >> 16) & 0xff), *d;

                if ((U32(c, 0) | U32(b, 0) | U32(a, 0)) & 0x10) {
                    U32(b, 0) |= 0x80;
                    f = 0xbdcccccdu; /* -0.1 */
                } else {
                    f = pk_facing(a, b, c);
                    if (mirror)
                        f = pk_fchs(f);
                }
                if (flag8 & 1 || !(f & 0x80000000u))
                    goto next;
                d = id ? PK_VREC(vbuf, id) : c;
                if ((U32(c, 0) & U32(b, 0) & U32(a, 0) & U32(d, 0)) & 0x3f)
                    goto next;
                G32(0x6ccc7c) = 0xffffff;
                nrm = face[-1];
                semi = (nrm >> 6) & 0x2000000;
                G32(0x6ccc68) = 0u - (uint32_t)((int32_t)(nrm << 22) >> 18);
                G32(0x6ccc64) = ((0u - (uint32_t)((int32_t)(nrm << 2) >> 18)) & 0xffff) |
                                (0u - ((uint32_t)((int32_t)(nrm << 12) >> 2) & 0xffff0000u));
                gte_light_colour_at();
                if (s.mode >= 0 && A(s.prim) >= s.limit)
                    goto done;
                memcpy(s.prim + 8, a, 0x18);
                memcpy(s.prim + 0x28, c, 0x18);
                memcpy(s.prim + 0x48, b, 0x18);
                if (s.mode >= 0)
                    pk_prim_texture(&s, s.prim);
                {
                    uint32_t col = G32(0x6cccbc), code = 0x9c000000u;

                    if (id) {
                        code = 0x8c000000u;
                        memcpy(s.prim + 0x68, d, 0x18);
                    }
                    U16(s.prim, 0x22) = 0xff;
                    U32(s.prim, 4) = code | col | semi;
                    col |= 0xff000000u;
                    U32(s.prim, 0x24) = col;
                    U32(s.prim, 0x44) = col;
                    U32(s.prim, 0x64) = col;
                }
                pk_prim_link(s.prim, a, b, c, id ? d : 0, semi != 0);
                if (s.mode >= 0)
                    s.prim += 0x84;
            }
        next:
            if (s.mode >= 0)
                s.tex++;
            else
                s.prim += 0x84;
        } while (count > 0);
        if (count & 2)
            flag8 >>= 1;
        if (count & 4)
            break;
    }
done:
    if (s.mode >= 0)
        G32(0x6c1e18) = A(s.prim);
}

/* 0x45ab20: build a model's packets with a light colour per vertex (normals
 * at the model's +0x48, 8 bytes each: x, y, z negated into the GTE vector,
 * *0x6cce3c; all but the last with alpha 0xff, sic): as 0x45a430 without
 * the face colour runs, a quad facing if either half (a, b, c / b, d, c)
 * is, skipped when all corners share any flag bit (not just the clip
 * codes), the semi-transparent bit (normal bit 31) into byte +7 and the
 * corners' colours from their vertices. Nothing without normals. Vertex
 * index 0 (outside the original's frame) reads a zero record and colour
 * here. */
void GAMEAPI model_faces_draw_smooth(uint8_t *body, int32_t depth)
{
    uint8_t vbuf[(0x100 + 1) * 0x18];
    uint32_t col[0x100 + 1];
    struct pk_model s;
    int32_t mirror = GI32(0x6cce94);
    int32_t near, far, count;
    uint32_t i, n;
    const uint32_t *verts, *face;
    const uint8_t *nrm;
    uint16_t flag8;

    memset(vbuf, 0, 0x18);
    col[0] = 0;
    s.limit = G32(0x6c1e24);
    s.texbase = G32(0x6bfb5c);
    near = (int32_t)(G32(0x6cd404) * 6000u) >> 8;
    far = (int32_t)(G32(0x6cd404) * 15000u) >> 8;
    if (U32(U32(body, 0x10), 0x48) == 0)
        return;
    pk_model_lod(&s, body, depth, near, far);
    verts = (const uint32_t *)PTR(U32(body, 0x24));
    flag8 = U16(body, 8);
    nrm = PTR(U32(s.model, 0x48));

    n = s.nverts;
    G32(0x6ccc64) = verts[0];
    G32(0x6ccc68) = verts[1];
    pk_transform_project(PK_VREC(vbuf, 1));
    for (i = 0;; i++, nrm += 8) {
        G32(0x6ccc64) = (0u - ((uint32_t)(int32_t)S16(nrm, 2) << 16)) | ((0u - U16(nrm, 0)) & 0xffff);
        G32(0x6ccc68) = 0u - (uint32_t)(int32_t)S16(nrm, 4);
        gte_light_colour_at();
        if (i == n) {
            col[i + 1] = G32(0x6cccbc); /* sic: no alpha */
            break;
        }
        G32(0x6ccc64) = verts[2 * (i + 1)];
        G32(0x6ccc68) = verts[2 * (i + 1) + 1];
        pk_transform_project(PK_VREC(vbuf, i + 2));
        col[i + 1] = G32(0x6cccbc) | 0xff000000u;
    }

    face = s.faces;
    for (;;) {
        count = s.mode < 0 ? U16(s.prim, 0x82) : *s.runs++;
        do {
            uint32_t w = face[0];

            face += 2;
            count -= 8;
            if (w != 0) {
                uint32_t ia = w & 0xff, ib = (w >> 8) & 0xff, ic = (w >> 16) & 0xff, id = w >> 24;
                uint32_t f1, f2, semi;
                uint8_t *a = PK_VREC(vbuf, ia), *b = PK_VREC(vbuf, ib), *c = PK_VREC(vbuf, ic), *d;

                if ((U32(a, 0) | U32(b, 0) | U32(c, 0)) & 0x10) {
                    U32(b, 0) |= 0x80;
                    f1 = 0xbdcccccdu; /* -0.1 */
                } else {
                    f1 = pk_facing(a, b, c);
                    if (mirror)
                        f1 = pk_fchs(f1);
                }
                if (id) {
                    d = PK_VREC(vbuf, id);
                    if ((U32(b, 0) | U32(d, 0) | U32(c, 0)) & 0x10) {
                        U32(b, 0) |= 0x80;
                        f2 = 0xbdcccccdu;
                    } else {
                        f2 = pk_facing(b, d, c);
                        if (mirror)
                            f2 = pk_fchs(f2);
                    }
                } else {
                    d = c;
                    f2 = f1;
                }
                if (flag8 & 1 || !((f1 | f2) & 0x80000000u))
                    goto next;
                if (U32(c, 0) & U32(b, 0) & U32(a, 0) & U32(d, 0))
                    goto next;
                semi = (face[-1] >> 30) & 2;
                if (s.mode >= 0 && A(s.prim) >= s.limit)
                    goto done;
                memcpy(s.prim + 8, a, 0x18);
                memcpy(s.prim + 0x28, c, 0x18);
                memcpy(s.prim + 0x48, b, 0x18);
                if (s.mode >= 0)
                    pk_prim_texture(&s, s.prim);
                {
                    uint8_t code = 0x9c;

                    U32(s.prim, 4) = col[ia];
                    U32(s.prim, 0x44) = col[ib];
                    U16(s.prim, 0x22) = 0xff;
                    U32(s.prim, 0x24) = col[ic];
                    if (id) {
                        memcpy(s.prim + 0x68, d, 0x18);
                        U32(s.prim, 0x64) = col[id];
                        code = 0x8c;
                    }
                    U8(s.prim, 7) = (uint8_t)(semi | code);
                }
                pk_prim_link(s.prim, a, b, c, id ? d : 0, semi != 0);
                if (s.mode >= 0)
                    s.prim += 0x84;
            }
        next:
            if (s.mode >= 0)
                s.tex++;
            else
                s.prim += 0x84;
        } while (count > 0);
        if (count & 2)
            flag8 >>= 1;
        if (count & 4)
            break;
    }
done:
    if (s.mode >= 0)
        G32(0x6c1e18) = A(s.prim);
}

/* ===================================================================== */
/* The kind-5 track object (0x45bf10). */

/* 0x45bb70: its hit callback (+0x148, installed by 0x45bbf0): a hit on
 * the track (obj 0) at a speed of 0x30000 or more in any axis (hit +0x5c
 * .. +0x64) makes a contact particle (0x45be10, kind 0x16) at the hit
 * point (+0xc) one time in four (seed 0x4b11d8). Returns 1. */
int32_t GAMEAPI k5_object_hit(uint8_t *self, uint8_t *obj, uint8_t *hit)
{
    int32_t x, y, z;
    uint32_t s;

    if (obj != 0)
        return 1;
    z = I32(hit, 0x64);
    y = I32(hit, 0x60);
    x = I32(hit, 0x5c);
    if ((uint32_t)(((z ^ (z >> 31)) - (z >> 31)) | ((y ^ (y >> 31)) - (y >> 31)) | ((x ^ (x >> 31)) - (x >> 31))) <
        0x30000)
        return 1;
    s = (G32(0x4b11d8) * 0x41a7u) & 0x7fffffff;
    G32(0x4b11d8) = s;
    if (!(s & 0x30000))
        contact_particle_at(self, hit + 0xc, 0x16);
    return 1;
}

/* 0x45bbf0: trigger the object: when held (flag 0x2000) it is released
 * (flags 0x2000 / 0x200 off, 0x1000 on, +0x108 bit 0 off), faces its
 * matrix's third column (0x40ee60 into +0x58), gets a random spin (+0x80
 * .. +0x88, seed 0x4b11dc) and 16 sparks (0x45bd30); returns 0. Otherwise
 * an explosion effect (0x42aa30: 0x11229, 0xb) 0x8c0000 above it falling
 * at 0xffc00000, its hit callback set to 0x45bb70; returns 1. */
int32_t GAMEAPI k5_object_trigger(uint8_t *body)
{
    int32_t v[3];

    if (U32(body, 0) & 0x2000) {
        uint32_t s, r;

        U32(body, 0) = (U32(body, 0) & ~0x2200u) | 0x1000;
        U32(body, 0x108) &= ~1u;
        v[0] = S16(body, 0xc8);
        v[1] = S16(body, 0xce);
        v[2] = S16(body, 0xd4);
        quat_facing_at(body + 0x58, v);
        s = (G32(0x4b11dc) * 0x41a7u) & 0x7fffffff;
        G32(0x4b11dc) = s;
        r = ((s >> 15) * 0x7fff) >> 16;
        U32(body, 0x80) = ((r & 0xf) + 0xff8) << 20;
        U32(body, 0x84) = (((r >> 4) & 0xf) - 8) << 20;
        U32(body, 0x88) = (((r >> 8) & 0xf) - 8) << 20;
        body_sparks_at(body, 0x10);
        return 0;
    } else {
        int32_t pos[3], vel[3];

        pos[0] = I32(body, 0x34);
        pos[1] = (int32_t)(U32(body, 0x38) + 0x8c0000);
        pos[2] = I32(body, 0x3c);
        vel[0] = 0;
        vel[1] = (int32_t)0xffc00000u;
        vel[2] = 0;
        explosion_effects_at(pos, vel, 1, 0x11229, 0xb);
        U32(body, 0x148) = 0x45bb70;
        return 1;
    }
}
