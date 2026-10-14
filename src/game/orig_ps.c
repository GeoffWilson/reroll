/* Group PS: the original's still-reachable functions in 0x483210-0x4867a0
 * (the projectile trails and the weather's drawing, the screen flakes, the
 * weather's light level and palette, the animated "snatch" texture).
 *
 *  - projectile trails: all of them (0x483210) and one (0x483240), drawn
 *    as two ribbons of quads through the transform pointers, with the head
 *    flare (0x441d80) or sprite (0x47e5f0);
 *  - the homing lock marker: two rings of four triangles round the screen
 *    position of a target (0x483d20, 0x483a80);
 *  - the weather: per view (0x484980) the rain drops (0x484a10) or snow
 *    flakes (0x484c20) of an emitter as sprites; the screen-space flakes
 *    and lens drops: update (0x4857f0, the spawner 0x485140 is hooked in
 *    src/widescreen.c), draw (0x485d00), free slot (0x485680);
 *  - the weather's brightness (0x486240), the lower of level / limit
 *    (0x486270), the sky offset (0x486310), the dimmed palette (0x486350),
 *    the network tint value (0x485050);
 *  - the snatch texture: set-up (0x486780), the plasma and its offset
 *    table (0x4867a0), the palette (0x486450), the per-frame animation
 *    (0x486540) and its copy into the locked surface (0x486600).
 *
 * Float steps are the original's x87 instructions in inline asm (memory
 * operands, the same single-precision stores in between). The transform /
 * projection pointers (*0x6ccd78 / *0x6ccd7c, then *0x6ccd80) hand st(0)
 * from one to the other, so each pair is one asm block. The plasma
 * (0x4867a0), a chain of x87 steps and C runtime calls passing doubles on
 * the stack, is the original's instruction sequence in assembly. Everything
 * outside the group is called by address. */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define GS16(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))
#define PTR(x) ((uint8_t *)(uintptr_t)(x))

/* 0x4266d0: gte_rtps_float, an x87 asm entry in src/game/orig_pc.c (no arguments). */
typedef void(GAMEAPI *ps_void_fn)(void);

#define flare_add(pos, kind, colour) GCALL(0x441d80, flare_add)(pos, kind, colour)
#define sprite_quad(pos, size, pkt, mode, flags, s) \
    GCALL(0x47e5f0, sprite_quad_build)((const uint32_t *)(pos), size, pkt, mode, flags, s)
#define gte_rtps() ((ps_void_fn)(uintptr_t)0x4266d0)()
#define net_slot() GCALL(0x48cf20, net_local_slot)()
#define flake_spawn(i) GCALL(0x485140, flake_spawn_body)(i)
#define fixed_sin(a) GCALL(0x4270a0, fix_sin)(a)
#define surface_lock(t, a, b, c, d, e) GCALL(0x428c30, texture_surface_lock)(t, a, A(b), A(c), A(d), A(e))
#define surface_unlock(t, a, b) GCALL(0x428c60, texture_surface_unlock)(t, a, A(b))
#define pixels_convert(s, n, d, a) GCALL(0x41e1f0, pixels_rgb_convert)(PTR(s), n, (uint16_t *)PTR(d), a)

#if !defined(__i386__)
/* x86-64: the transform pointers *0x6ccd78 / *0x6ccd7c (0x426a20 / 0x426be0,
 * src/subunit.c) return what the original leaves in st(0) and the
 * projection *0x6ccd80 (0x426b00, src/widescreen.c) takes it. */
typedef float(GAMEAPI *ps_transform_fn)(uint8_t *rec);
typedef void(GAMEAPI *ps_project_fn)(float h);
#define PS_TRANSFORM(slot, rec) ((ps_transform_fn)(uintptr_t)G32(slot))((uint8_t *)(rec))
#define PS_PROJECT(h) ((ps_project_fn)(uintptr_t)G32(0x6ccd80))(h)
#endif

#define g_race G32(0x4b1960)
#define g_flakes G32(0x4d5898) /* 32 records of 0xa0 */
#define FLAKE(i) (g_flakes + (uint32_t)(i) * 0xa0)

/* This group's functions (also in build/groups/ps_protos.txt for physics.h). */
void GAMEAPI projectile_trails_draw(void); /* 0x483210 */
void GAMEAPI projectile_trail_draw(uint8_t *p); /* 0x483240 */
void GAMEAPI lock_marker_quads(uint8_t *p, int32_t sx, int32_t sy); /* 0x483a80 */
void GAMEAPI lock_marker_draw(uint8_t *rec); /* 0x483d20 */
void GAMEAPI weather_draw(uint8_t *slot); /* 0x484980 */
void GAMEAPI rain_drops_draw(int32_t i); /* 0x484a10 */
void GAMEAPI snow_drops_draw(int32_t i); /* 0x484c20 */
int32_t GAMEAPI net_tint_level_get(int32_t *out); /* 0x485050 */
int32_t GAMEAPI flake_free_find(void); /* 0x485680 */
void GAMEAPI screen_flakes_update(int32_t outdoors); /* 0x4857f0 */
void GAMEAPI screen_flakes_draw(void); /* 0x485d00 */
int32_t GAMEAPI weather_brightness(void); /* 0x486240 */
int32_t GAMEAPI weather_level_lower(void); /* 0x486270 */
int32_t GAMEAPI weather_sky_offset(void); /* 0x486310 */
uint32_t GAMEAPI weather_palette_get(void); /* 0x486350 */
int32_t GAMEAPI snatch_palette_build(void); /* 0x486450 */
int32_t GAMEAPI snatch_texture_animate(void); /* 0x486540 */
void GAMEAPI snatch_texture_blit(int32_t step, uint32_t dst, uint32_t pitch, uint32_t bpp); /* 0x486600 */
int32_t GAMEAPI snatch_texture_setup(void); /* 0x486780 */
void snatch_plasma_build(void); /* 0x4867a0: asm */

/* Every x87 register, for asm blocks that call out or use the FPU stack. */
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* ---- x87 helpers: the original's instructions ---- */

/* fild dword v; fstp dword. */
static inline uint32_t ps_i2f(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fld a; fadd b; fstp. */
static inline uint32_t ps_add(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fsub b; fstp. */
static inline uint32_t ps_sub(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fmul b; fstp. */
static inline uint32_t ps_mul(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fdiv b; fstp. */
static inline uint32_t ps_div(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fdiv b; fdivr c; fstp: c / (a / b). */
static inline uint32_t ps_div_divr(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfdivrs %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fmul b; fadd c; fstp: a * b + c. */
static inline uint32_t ps_mul_add(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfadds %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fsub b; fmul c; fadd d; fstp: (a - b) * c + d. */
static inline uint32_t ps_sub_mul_add(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfmuls %3\n\tfadds %4\n\tfstps %0"
                     : "=m"(r)
                     : "m"(a), "m"(b), "m"(c), "m"(d)
                     : "st");
    return r;
}

/* fld a; fsub b; fmul c; fsubr d; fstp: d - (a - b) * c. */
static inline uint32_t ps_sub_mul_subr(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfmuls %3\n\tfsubrs %4\n\tfstps %0"
                     : "=m"(r)
                     : "m"(a), "m"(b), "m"(c), "m"(d)
                     : "st");
    return r;
}

/* fld a; fmul k1; fmul s; fld a; fmul k2; faddp; fstp: a * k1 * s + a * k2. */
static inline uint32_t ps_blend(uint32_t a, uint32_t k1, uint32_t s, uint32_t k2)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfmuls %3\n\tflds %1\n\tfmuls %4\n\tfaddp %%st, %%st(1)\n\tfstps %0"
                     : "=m"(r)
                     : "m"(a), "m"(k1), "m"(s), "m"(k2)
                     : "st", "st(1)");
    return r;
}

/* fld a; fistp dword: a rounded by the control word. */
static inline int32_t ps_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld a; fcomp b; fnstsw ax: the status word's high byte (C0 = 1, C3 = 0x40). */
static inline uint32_t ps_cmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 0xff;
}

/* fld dword a; call _ftol (0x497a80, truncating). */
static inline int32_t ps_ftol(uint32_t a)
{
#if defined(__i386__)
    int32_t r;
    __asm__ volatile("flds %1\n\tmovl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(a)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
#else
    float f;

    memcpy(&f, &a, 4);
    return (int32_t)(int64_t)f; /* _ftol truncates to 64 bits; eax is the low half */
#endif
}

/* The transform *0x6ccd78 (record out, the GTE vector 0x6ccc64 in), its
 * argument popped, then the projection *0x6ccd80 (takes its st(0)). */
static inline void ps_transform_project(void *out)
{
#if defined(__i386__)
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tcall *0x6ccd80"
                     :
                     : "S"(out)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    float h = PS_TRANSFORM(0x6ccd78, out);

    PS_PROJECT(h);
#endif
}

/* The same with the transform's argument still pushed during the
 * projection. */
static inline void ps_transform_project_pushed(void *out)
{
#if defined(__i386__)
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\tcall *0x6ccd80\n\taddl $4, %%esp"
                     :
                     : "S"(out)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    ps_transform_project(out);
#endif
}

/* 16807 * seed mod 2^31 (the original's lea / shl chain, masked). */
static inline uint32_t ps_rng(uint32_t address)
{
    uint32_t r = (G32(address) * 16807u) & 0x7fffffffu;
    G32(address) = r;
    return r;
}

/* The depth bucket squeeze shared by the sprites: above 0x100 halved
 * (+0x80), above 0x180 halved again (+0xc0), at most 0x1ff. */
static inline int32_t ps_squeeze(int32_t b)
{
    if (b > 0x100) {
        b = b / 2 + 0x80;
        if (b > 0x180) {
            b = b / 2 + 0xc0;
            if (b > 0x1ff)
                b = 0x1ff;
        }
    }
    return b;
}

/* Link a packet at an ordering-table entry (pointer, high bit masked). */
static inline void ps_link(uint32_t entry, uint8_t *pk)
{
    entry &= 0x7fffffffu;
    U32(pk, 0) = G32(entry);
    G32(entry) = A(pk);
}

/* ===================================================================== */
/* Projectile trails. */

/* The four projected vertices of the trail's current ring. The transform
 * leaves x / y (+8 / +0xc) unset behind the near plane, and the original
 * copies such vertices into packets: its stack leftovers, which between the
 * projectiles 0x483210 draws are the previous projectile's ring. Kept here
 * across calls for the same effect (the first projectile of a frame sees
 * the last frame's ring instead of whatever the original's stack held). */
uint32_t ps_trail_ring[4][6];

/* ===== 0x483210: every live projectile (24 pointers at 0x6b9ce0, flags
 * +0x188 non-zero) drawn by 0x483240. ===== */
void GAMEAPI projectile_trails_draw(void)
{
    uint32_t a;

    for (a = 0x6b9ce0; a < 0x6b9d40; a += 4) {
        uint8_t *p = PTR(G32(a));

        if (U32(p, 0x188) != 0)
            projectile_trail_draw(p);
    }
}

/* ===== 0x483240: one projectile's trail. The 32 trail points (+0x1b8,
 * 0x20 each: four int16 x / y / z sub-points 8 apart) are a ring ending at
 * (+0x23dc (int8) + count +0x23de - 1) mod 32. With flags +0x188 & 0x1c3
 * the head (mean of sub-points 0 and 2) gets the trail's colour (+0x1b4:
 * flags 1 from the kind's table 0x4b2b3c / 0x4b2b10 / 0x4b2ae4 by +0x180,
 * else 0x70f040 / 0xf07040 / 0x60a0f0 for kind +0x184 0 / 2 / other) as
 * the GTE colour 0x6ccc34.. and a flare (0x441d80, kind 4) or, for kind 2,
 * a sprite (0x47e5f0 with +0x22ec). Then, unless the count is below 2,
 * each point from the head back: unless 0x4d1198, jitter (kind 1: the
 * point itself by RNG 0x4b11d8 masked by the age's spread; kind 2: only
 * what is drawn; others: the point sinks 2), all but with flags 1; the
 * head's point (kinds 0 / 1) drawn a quarter of the way from its middle;
 * the four sub-points projected (the nearest depth kept). From the second
 * point on, two quads (sub-points 0 / 2 and 1 / 3 of this and the previous
 * point: packets +0x4ec + i * 0xf0, 0x78 each), dropped when all four clip
 * codes share a bit; the first quad also when detail (*0x4b2198 +0x68) is
 * below 4.0 and the kind is 0; colour 0xf4dcc4 scaled by the alpha
 * (point's +0x5ba byte + age fades, 0..255) unless 0x6ccd84 bit 0; depth
 * bucket nearest >> 5 squeezed, 1..0x1ff, into 0x6cdd80 / 0x6cd510 (by
 * 0x6cce84). A clipped quad leaves its packet for the next (sic). The
 * original also has a near-plane interpolation block that can't run (its
 * flag is always 1 there); left out. The GTE vector's pad word (0x6ccc6a)
 * and the sprite vector's (+6) are stack leftovers in the original: 0.
 * ===== */
void GAMEAPI projectile_trail_draw(uint8_t *p)
{
    uint32_t prev[4][6];
    int32_t kind = I32(p, 0x184);
    int32_t n = S16(p, 0x23de);
    int32_t idx = (int8_t)U8(p, 0x23dc) + n - 1;
    uint32_t flags = U32(p, 0x188);
    int32_t vec[3] = {0, 0, 0};
    int32_t i, j, k, pidx;

    if (idx < 0)
        idx = 0;
    idx %= 32;
    if (flags & 0x1c3) {
        uint8_t *pt = p + 0x1b8 + idx * 0x20;
        int16_t mid[4];
        uint32_t colour;

        mid[0] = (int16_t)((S16(pt, 0x10) + S16(pt, 0)) >> 1);
        mid[1] = (int16_t)((S16(pt, 0x12) + S16(pt, 2)) >> 1);
        mid[2] = (int16_t)((S16(pt, 0x14) + S16(pt, 4)) >> 1);
        mid[3] = 0;
        if (flags == 1) {
            uint32_t t = kind == 0 ? 0x4b2b3c : kind == 2 ? 0x4b2b10 : 0x4b2ae4;

            colour = G32(t + U32(p, 0x180) * 4);
        } else {
            colour = kind == 0 ? 0x70f040 : kind == 2 ? 0xf07040 : 0x60a0f0;
        }
        U32(p, 0x1b4) = colour;
        G32(0x6ccc34) = (colour & 0xff) << 4;
        G32(0x6ccc38) = ((colour >> 8) & 0xff) << 4;
        G32(0x6ccc3c) = ((colour >> 16) & 0xff) << 4;
        G32(0x6ccc60) = 0xff0;
        if (kind == 2) {
            sprite_quad(mid, 0x3000, p + 0x22ec, 3, 0x80000000u, 0);
        } else {
            vec[0] = (int32_t)((uint32_t)mid[0] << 16);
            vec[1] = (int32_t)((uint32_t)mid[1] << 16);
            vec[2] = (int32_t)((uint32_t)mid[2] << 16);
            flare_add(vec, 4, (kind != 0 ? 0xff3fc13fu : 0) + 0xc0ffc0u);
        }
    }
    if (n < 2)
        return;
    pidx = idx;
    for (i = 0; i < n; i++) {
        int32_t m = n - i - 1, fade, grow, spread, mask, minz = 0x7fffffff;
        int16_t alpha_base;
        uint16_t tmp[3];
        uint8_t *pt;

        if (U8(p, 0x188) & 3)
            m += 0x20 - n;
        fade = m * 6 - 1;
        if (fade < 0)
            fade = 0;
        grow = m * 2 - 0x20;
        if (grow < 0)
            grow = 0;
        grow *= 2;
        spread = (0x20 - m) / 8;
        mask = spread * 2 - 1;
        if (mask < 0)
            mask = 0;
        pt = p + 0x1b8 + pidx * 0x20;
        pidx--;
        alpha_base = (int8_t)U8(p, 0x5ba + pidx);
        if (pidx < 0)
            pidx = 0x1f;
        tmp[0] = U16(pt, 0);
        tmp[1] = U16(pt, 2);
        tmp[2] = U16(pt, 4);
        for (j = 0; j < 4; j++) {
            uint8_t *sp = pt + j * 8;

            memcpy(prev[j], ps_trail_ring[j], 24);
            if (G32(0x4d1198) == 0) {
                if (kind == 1) {
                    if (U32(p, 0x188) != 1) {
                        uint32_t r = ps_rng(0x4b11d8) >> 16;

                        U16(sp, 0) += (uint16_t)((mask & r) - spread);
                        U16(sp, 2) += (uint16_t)(spread - (mask & (r >> 4)) - 2);
                        U16(sp, 4) += (uint16_t)((mask & (r >> 8)) - spread);
                    }
                } else if (kind == 2) {
                    if (U32(p, 0x188) != 1) {
                        uint32_t r = ps_rng(0x4b11d8) >> 16;

                        tmp[0] += (uint16_t)((r & 0xf) - 8);
                        tmp[1] += (uint16_t)(((r >> 4) & 0xf) - 8);
                        tmp[2] += (uint16_t)(((r >> 8) & 0xf) - 8);
                    }
                } else if (U32(p, 0x188) != 1) {
                    U16(sp, 2) -= 2;
                }
            }
            if (i == 0 && (kind == 0 || kind == 1)) {
                if (j == 0) {
                    vec[0] = ((S16(sp, 0x10) - S16(sp, 0)) >> 1) + S16(sp, 0);
                    vec[1] = ((S16(sp, 0x12) - S16(sp, 2)) >> 1) + S16(sp, 2);
                    vec[2] = ((S16(sp, 0x14) - S16(sp, 4)) >> 1) + S16(sp, 4);
                }
                tmp[0] = (uint16_t)(((S16(sp, 0) - vec[0]) >> 2) + vec[0]);
                tmp[1] = (uint16_t)(((S16(sp, 2) - vec[1]) >> 2) + vec[1]);
                tmp[2] = (uint16_t)(((S16(sp, 4) - vec[2]) >> 2) + vec[2]);
            }
            G32(0x6ccc64) = tmp[0] | (uint32_t)tmp[1] << 16;
            G32(0x6ccc68) = tmp[2];
            ps_transform_project(ps_trail_ring[j]);
            tmp[0] = U16(sp, 8);
            tmp[1] = U16(sp, 0xa);
            tmp[2] = U16(sp, 0xc);
            {
                int32_t z = ps_ftol(ps_trail_ring[j][1]);

                if (z < minz)
                    minz = z;
            }
        }
        if (i != 0) {
            uint8_t *pk = p + 0x4ec + i * 0xf0;
            int32_t b = ps_squeeze(minz >> 5);

            if (b < 1)
                b = 1;
            for (k = 0; k < 2; k++) {
                int32_t alpha;
                uint32_t tag;

                if ((ps_cmp(U32(G32(0x4b2198), 0x68), G32(0x4a3344)) & 1) && I32(p, 0x184) == 0 && k == 0) {
                    pk += 0x78;
                    continue;
                }
                if (prev[k][0] & prev[k + 2][0] & ps_trail_ring[k][0] & ps_trail_ring[k + 2][0])
                    continue;
                memcpy(pk + 8, prev[k], 24);
                memcpy(pk + 0x24, prev[k + 2], 24);
                memcpy(pk + 0x40, ps_trail_ring[k], 24);
                memcpy(pk + 0x5c, ps_trail_ring[k + 2], 24);
                alpha = alpha_base + fade + (grow >> 2);
                if (alpha < 0)
                    alpha = 0;
                if (alpha > 0xff)
                    alpha = 0xff;
                tag = (uint32_t)U8(pk, 7) << 24 | 0xf4dcc4;
                if (G8(0x6ccd84) & 1) {
                    U32(pk, 4) = tag;
                } else {
                    uint32_t a = (uint32_t)alpha;

                    U32(pk, 4) = (tag & 0xff000000u) | ((0xf4 * a) >> 8) << 16 | ((0xdc * a) >> 8) << 8 |
                                 ((0xc4 * a) >> 8);
                }
                U8(pk, 0x22) = (uint8_t)alpha;
                ps_link(G32((G32(0x6cce84) ? 0x6cdd80u : 0x6cd510u) + (uint32_t)b * 4), pk);
                pk += 0x78;
            }
        }
    }
}

/* ===================================================================== */
/* The homing lock marker. */

/* ===== 0x483a80: two rings of four triangles (packets of 0x1c from +0x28,
 * 0x70 per ring, code 0x32, length 6) round screen point (sx, sy): ring 0
 * turns by +0x24 (+4 a call), radius 0, colours 0xff / 0x80000020; ring 1
 * by +0x26 (-4 a call), radius (+0xc * 10 + 2) * scale >> 8, colours
 * 0xff0000 / 0x80200000; corner offsets from the tables 0x4b2b68 /
 * 0x4b2bc8 (scaled by *0x6cd40c >> 8) plus the radius times 0x4b2c28 /
 * 0x4b2c38, rotated by the sin / cos tables 0x4b24f8 / 0x4b2578 (>> 14);
 * linked at *(*0x6ce608 + 4). ===== */
void GAMEAPI lock_marker_quads(uint8_t *p, int32_t sx, int32_t sy)
{
    int32_t scale = GI32(0x6cd40c);
    uint32_t i, j;

    U16(p, 0x24) += 4;
    U16(p, 0x26) -= 4;
    for (i = 0; i < 2; i++) {
        uint8_t *pk = p + 0x28 + i * 0x70;
        uint32_t a, c0, c1;
        int32_t r, s, c;

        if (i == 0) {
            a = U8(p, 0x24);
            r = 0;
            c0 = 0xff;
            c1 = 0x80000020u;
        } else {
            a = U8(p, 0x26);
            c0 = 0xff0000;
            c1 = 0x80200000u;
            r = (int32_t)((uint32_t)U16(p, 0xc) * 10 + 2) * scale >> 8;
        }
        s = GS16(0x4b24f8 + a * 2);
        c = GS16(0x4b2578 + a * 2);
        for (j = 0; j < 4; j++, pk += 0x1c) {
            uint32_t e = i * 0x30 + j * 0xc;
            int32_t w = GI32(0x4b2c28 + j * 4) * r, w2 = GI32(0x4b2c38 + j * 4) * r;
            int32_t x0 = (GI32(0x4b2b68 + e) * scale >> 8) + w;
            int32_t x1 = (GI32(0x4b2b6c + e) * scale >> 8) + w;
            int32_t x2 = (GI32(0x4b2b70 + e) * scale >> 8) + w;
            int32_t y0 = (GI32(0x4b2bc8 + e) * scale >> 8) + w2;
            int32_t y1 = (GI32(0x4b2bcc + e) * scale >> 8) + w2;
            int32_t y2 = (GI32(0x4b2bd0 + e) * scale >> 8) + w2;

            S16(pk, 8) = (int16_t)((x0 * c >> 14) + sx + (y0 * s >> 14));
            S16(pk, 0x10) = (int16_t)((x1 * c >> 14) + sx + (y2 * s >> 14));
            S16(pk, 0x18) = (int16_t)((x2 * c >> 14) + sx + (y1 * s >> 14));
            S16(pk, 0xa) = (int16_t)(sy + (-(x0 * s) >> 14) + (y0 * c >> 14));
            S16(pk, 0x12) = (int16_t)(sy + (-(x1 * s) >> 14) + (y2 * c >> 14));
            S16(pk, 0x1a) = (int16_t)(sy + (-(x2 * s) >> 14) + (y1 * c >> 14));
            U32(pk, 0xc) = c1;
            U32(pk, 4) = c0;
            U32(pk, 0x14) = c1;
            U32(pk, 0) = (U32(pk, 0) & 0xffffff) | 0x06000000u;
            U8(pk, 7) = 0x32;
            ps_link(U32(G32(0x6ce608), 4), pk);
        }
    }
}

/* ===== 0x483d20: if +0x8c, the position +0x6c (16.16, high words)
 * projected (0x4266d0) and the marker drawn at the screen point 0x6ccc9c
 * (0x483a80 on +0x6c). The GTE vector's pad word (0x6ccc6a) is a stack
 * leftover in the original: 0 here. ===== */
void GAMEAPI lock_marker_draw(uint8_t *rec)
{
    uint32_t v;

    if (U32(rec, 0x8c) == 0)
        return;
    G32(0x6ccc64) = ((uint32_t)(I32(rec, 0x6c) >> 16) & 0xffff) | (uint32_t)(I32(rec, 0x70) >> 16) << 16;
    G32(0x6ccc68) = (uint32_t)(I32(rec, 0x74) >> 16) & 0xffff;
    gte_rtps();
    v = G32(0x6ccc9c);
    lock_marker_quads(rec + 0x6c, (int32_t)(v & 0xffff), (int32_t)(v >> 16));
}

/* ===================================================================== */
/* The weather's drawing. */

/* ===== 0x484980: per view (slot): the screen flakes (0x485d00) unless
 * render flag 2; with weather (race +0x553), the emitter: the slot's index
 * among the first count - 2 (count 0x5d6248; 0 if none), count - 2 with
 * render flag 2, count - 1 with flags 0x10 and 8; rain (kind bit 0,
 * 0x484a10) or snow (0x484c20). ===== */
void GAMEAPI weather_draw(uint8_t *slot)
{
    uint8_t rf = G8(0x6cce40), kind;
    int32_t count, last, i = 0;

    if (!(rf & 2)) {
        screen_flakes_draw();
        rf = G8(0x6cce40);
    }
    kind = U8(g_race, 0x553);
    if (kind == 0)
        return;
    count = GI32(0x5d6248);
    last = count - 2;
    if (last > 0) {
        uint32_t s = 0x6bfb80;

        while (A(slot) != s) {
            i++;
            s += 0x274;
            if (i >= last)
                break;
        }
    }
    if (rf & 2)
        i = last;
    if ((rf & 0x10) && (rf & 8))
        i = count - 1;
    if (kind & 1)
        rain_drops_draw(i);
    else
        snow_drops_draw(i);
}

/* ===== 0x484a10: emitter i's rain (the 400 drops from +0x14 + i * 8, sic)
 * as streaks: the view's clip bounds 0x6ccd64..70 widened by *0x6cd418;
 * each live drop (+6) projected at its position and again moved by the
 * emitter's step (+0xc98 words less the body's position +0x68 >> 16, the
 * x / y pair added as one dword); dropped if both clip codes share a bit or
 * the first depth (fistp) >> 5 is below 1; bucket squeezed, less 0x30, at
 * least 1, into *0x6ce60c + 0x70; sprites of 0x3c from *0x5d6340, at most
 * 0xa0 (then it returns with the bounds still widened, sic); bounds put
 * back. The second vector's pad word (0x6ccc6a) is a stack leftover in the
 * original: 0 here. ===== */
void GAMEAPI rain_drops_draw(int32_t i)
{
    uint8_t *e = PTR(G32(0x5d6360 + (uint32_t)i * 4));
    uint32_t ot = G32(0x6ce60c) + 0x70;
    uint8_t *drop = e + i * 8 + 0x14;
    uint32_t f = G32(0x6cd418);
    uint8_t *pos = PTR(U32(e, 0x10) + 0x68);
    uint8_t *spr = PTR(G32(0x5d6340));
    uint32_t drawn = 0, k, step;
    uint16_t dz;

    G32(0x6ccd64) = ps_add(G32(0x6ccd64), f);
    G32(0x6ccd68) = ps_sub(G32(0x6ccd68), f);
    G32(0x6ccd6c) = ps_add(G32(0x6ccd6c), f);
    G32(0x6ccd70) = ps_sub(G32(0x6ccd70), f);
    step = (uint16_t)(U16(e, 0xc98) - (uint16_t)(I32(pos, 0) >> 16)) |
           (uint32_t)(uint16_t)(U16(e, 0xc9a) - (uint16_t)(I32(pos, 4) >> 16)) << 16;
    dz = (uint16_t)(U16(e, 0xc9c) - (uint16_t)(I32(pos, 8) >> 16));
    for (k = 0; k < 400; k++, drop += 8) {
        uint32_t xy, z;
        int32_t zi, b;

        if (U16(drop, 6) == 0)
            continue;
        G32(0x6ccc64) = U32(drop, 0);
        G32(0x6ccc68) = U32(drop, 4);
        xy = U32(drop, 0) + step;
        z = (uint16_t)(U16(drop, 4) + dz);
        ps_transform_project_pushed(spr + 8);
        G32(0x6ccc64) = xy;
        G32(0x6ccc68) = z;
#if defined(__i386__)
        __asm__ volatile("pushl %1\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tflds -0x18(%1)\n\tfistpl %0\n\t"
                         "call *0x6ccd80"
                         : "=m"(zi)
                         : "S"(spr + 0x24)
                         : "eax", "ecx", "edx", "cc", "memory", X87);
#else
        {
            float h = PS_TRANSFORM(0x6ccd78, spr + 0x24);

            zi = ps_fist(U32(spr, 0xc));
            PS_PROJECT(h);
        }
#endif
        if (U32(spr, 8) & U32(spr, 0x24))
            continue;
        b = zi >> 5;
        if (b < 1)
            continue;
        b = ps_squeeze(b) - 0x30;
        if (b < 1)
            b = 1;
        ps_link(ot + (uint32_t)b * 4, spr);
        spr += 0x3c;
        if (++drawn >= 0xa0)
            return;
    }
    G32(0x6ccd64) = ps_sub(G32(0x6ccd64), f);
    G32(0x6ccd68) = ps_add(G32(0x6ccd68), f);
    G32(0x6ccd6c) = ps_sub(G32(0x6ccd6c), f);
    G32(0x6ccd70) = ps_add(G32(0x6ccd70), f);
}

/* ===== 0x484c20: emitter i's snow (the 400 drops from +0x14 + i * 8, sic)
 * as squares: each live drop through the depth transform *0x6ccd7c (the
 * projection only for z >= 0); dropped behind the near plane (code 0x10);
 * half size (0xc00 << *0x6cce48) / depth (unsigned, at most 12; a depth
 * of 0 faults as in the original), bucket depth >> 5 squeezed less 0x30
 * (at least 1); while sprites last (0xaf of 0x78 from *0x5d6244): the
 * screen point less the clip origin (0x6ccd64 / 0x6ccd6c, fistp) packed
 * with the size must fit *0x6cd478 (packed word tests); corners x / y +-
 * size, depth at the four z, +8 = 0x40; into *0x6ce60c + 0x70. ===== */
void GAMEAPI snow_drops_draw(int32_t i)
{
    uint8_t *e = PTR(G32(0x5d6360 + (uint32_t)i * 4));
    uint8_t *spr = PTR(G32(0x5d6244));
    uint32_t scale = 0xc00u << (G8(0x6cce48) & 31);
    uint8_t *drop = e + i * 8 + 0x14;
    uint32_t ot = G32(0x6ce60c) + 0x70;
    uint8_t *end = spr + 0x5208;
    uint32_t v[6];
    uint32_t k;

    for (k = 400; k != 0; k--, drop += 8) {
        uint32_t size, pos, sz2, t, ff;
        int32_t zi, b, ix, iy;

        if (U16(drop, 6) == 0)
            continue;
        G32(0x6ccc64) = U32(drop, 0);
        G32(0x6ccc68) = U32(drop, 4);
#if defined(__i386__)
        __asm__ volatile("pushl %0\n\tcall *0x6ccd7c\n\taddl $4, %%esp\n\ttestl $0x80000000, 4(%0)\n\tjnz 1f\n\t"
                         "call *0x6ccd80\n1:"
                         :
                         : "S"(v)
                         : "eax", "ecx", "edx", "cc", "memory", X87);
#else
        {
            float h = PS_TRANSFORM(0x6ccd7c, v);

            if (!(v[1] & 0x80000000u))
                PS_PROJECT(h);
        }
#endif
        if (v[1] & 0x80000000u)
            continue;
        if (v[0] & 0x10)
            continue;
        zi = ps_fist(v[1]);
        size = scale / (uint32_t)zi;
        b = ps_squeeze(zi >> 5) - 0x30;
        if (b < 1)
            b = 1;
        if (size > 12)
            size = 12;
        if (spr >= end)
            continue;
        ix = ps_fist(ps_sub(v[2], G32(0x6ccd64)));
        iy = ps_fist(ps_sub(v[3], G32(0x6ccd6c)));
        pos = (uint32_t)iy << 16 | (uint32_t)ix;
        sz2 = size << 16 | size;
        if ((sz2 + pos) & 0x80008000u)
            continue;
        t = pos - sz2 - G32(0x6cd478);
        if ((int32_t)t > 0)
            continue;
        if (!(t & 0x8000))
            continue;
        ff = ps_i2f((int32_t)size);
        U32(spr, 8) = 0x40;
        U32(spr, 0x48) = U32(spr, 0x10) = ps_sub(v[2], ff);
        U32(spr, 0x30) = U32(spr, 0x14) = ps_sub(v[3], ff);
        U32(spr, 0x64) = U32(spr, 0x2c) = ps_add(ff, v[2]);
        U32(spr, 0x68) = U32(spr, 0x4c) = ps_add(ff, v[3]);
        U32(spr, 0x60) = U32(spr, 0x44) = U32(spr, 0x28) = U32(spr, 0xc) = v[1];
        ps_link(ot + (uint32_t)b * 4, spr);
        spr += 0x78;
    }
}

/* ===== 0x485050: *out = 0x6ccb64 * 21 - 21 (returned too). ===== */
int32_t GAMEAPI net_tint_level_get(int32_t *out)
{
    int32_t v = GI32(0x6ccb64) * 21 - 0x15;

    *out = v;
    return v;
}

/* ===== 0x485680: the first free screen flake (+0 zero) of 32, or -1.
 * ===== */
int32_t GAMEAPI flake_free_find(void)
{
    uint32_t f = g_flakes;
    int32_t i;

    for (i = 0; i < 0x20; i++, f += 0xa0)
        if (U32(f, 0) == 0)
            return i;
    return -1;
}

/* ===== 0x4857f0: the screen flakes' step (single view only, race +0x552
 * zero). Speed = the local car's (0x48cf20 slot) +0x658 unless render flag
 * 4. Snow (2, 8 flakes): every 10 steps a new target drift (+0x8c, RNG
 * 0x4b2c50, 1..3) eased into +0x88, added to y (+0x80) and to the angle
 * (+0x9c, either way by +0x94 bit 0); life +0x98 -2, at 0 the flake ends
 * (+0) and re-spawns outdoors (0x485140). Rain (1, 32 drops): speed under
 * 100 counts as 100; size +0x84 shrinks by 0.1 * 1 / (speed / 100); fall
 * +0x88 += 16 (to 0x400), y += sin(fall) * 0.0002; x pushed from the
 * centre by speed * 3e-5 / 2, y towards 1.5 screen heights by a quarter of
 * it; at life 16 ends, re-spawns outdoors, size eased (0.3 * scale + 0.7).
 * Clear (0, 8 lens drops): the sun-glint state 0x4d590c (on 1 in 512 steps,
 * 30000 ticks of 0x6c7ff8), when on with 0x4d58f0 and speed above 250 a
 * drop 1 in 2048 into the first free slot; lives fade every 5 steps.
 * ===== */
void GAMEAPI screen_flakes_update(int32_t outdoors)
{
    uint32_t w = ps_i2f(GI32(0x4acc38));
    uint32_t half = ps_div(w, G32(0x4a3314));
    uint32_t speed, sway;
    int32_t i;

    if (U8(g_race, 0x552) != 0)
        return;
    if (!(G8(0x6cce40) & 4)) {
        uint32_t s = (uint32_t)net_slot();

        speed = ps_i2f(S16(G32(0x6bfb80 + s * 0x274), 0x658));
    } else {
        speed = 0;
    }
    sway = ps_mul(speed, G32(0x4a348c));
    switch (U8(g_race, 0x553)) {
    case 2:
        for (i = 0; i < 8; i++) {
            int32_t c = I32(FLAKE(i), 0x90), a, t;

            I32(FLAKE(i), 0x90) = c + 1;
            if (c >= 10) {
                uint32_t r = ps_rng(0x4b2c50);

                U32(FLAKE(i), 0x8c) = ((r >> 15) * 3) >> 16;
                I32(FLAKE(i), 0x8c) += 1;
                if (I32(FLAKE(i), 0x8c) > 3)
                    I32(FLAKE(i), 0x8c) = 3;
                I32(FLAKE(i), 0x90) = 0;
            }
            a = I32(FLAKE(i), 0x88);
            t = I32(FLAKE(i), 0x8c);
            if (a < t)
                I32(FLAKE(i), 0x88) = a + 1;
            else if (a > t)
                I32(FLAKE(i), 0x88) = a - 1;
            U32(FLAKE(i), 0x80) = ps_add(ps_i2f(I32(FLAKE(i), 0x88)), U32(FLAKE(i), 0x80));
            if (U8(FLAKE(i), 0x94) & 1)
                I32(FLAKE(i), 0x9c) += I32(FLAKE(i), 0x88);
            else
                I32(FLAKE(i), 0x9c) -= I32(FLAKE(i), 0x88);
            I32(FLAKE(i), 0x98) -= 2;
            if (I32(FLAKE(i), 0x98) <= 0) {
                U32(FLAKE(i), 0) = 0;
                if (outdoors)
                    flake_spawn(i);
            }
        }
        break;
    case 1: {
        int32_t h = GI32(0x4acc3c);
        uint32_t bottom = ps_i2f(h + (h >> 1)), sway2, fall;

        if (ps_cmp(speed, G32(0x4a3488)) & 0x41)
            speed = 0x42c80000u; /* 100.0 */
        speed = ps_div_divr(speed, G32(0x4a3488), G32(0x4a331c));
        sway = ps_mul(sway, G32(0x4a3338));
        sway2 = ps_mul(sway, G32(0x4a3338));
        fall = ps_mul(speed, G32(0x4a341c));
        for (i = 0; i < 0x20; i++) {
            int32_t a;
            uint32_t arg;

            I32(FLAKE(i), 0x98) -= 2;
            U32(FLAKE(i), 0x84) = ps_sub(U32(FLAKE(i), 0x84), fall);
            a = I32(FLAKE(i), 0x88) + 0x10;
            if (a >= 0x400)
                a = 0x400;
            I32(FLAKE(i), 0x88) = a;
            /* push ecx with cx = the word: the high half is the flakes'
             * base pointer's (left in ecx). */
            arg = (g_flakes & 0xffff0000u) | U16(FLAKE(i), 0x88);
            U32(FLAKE(i), 0x80) = ps_mul_add(ps_i2f(fixed_sin(arg)), G32(0x4a3484), U32(FLAKE(i), 0x80));
            U32(FLAKE(i), 0x7c) = ps_sub_mul_add(U32(FLAKE(i), 0x7c), half, sway, U32(FLAKE(i), 0x7c));
            U32(FLAKE(i), 0x80) = ps_sub_mul_subr(bottom, U32(FLAKE(i), 0x80), sway2, U32(FLAKE(i), 0x80));
            if (I32(FLAKE(i), 0x98) <= 0x10) {
                U32(FLAKE(i), 0) = 0;
                if (outdoors)
                    flake_spawn(i);
                U32(FLAKE(i), 0x84) = ps_blend(U32(FLAKE(i), 0x84), G32(0x4a3340), speed, G32(0x4a3480));
            }
        }
        break;
    }
    case 0: {
        uint32_t r = ps_rng(0x4b2c50);
        int32_t on = GI32(0x4d590c);

        if (on == 0) {
            if ((((r >> 15) << 10) & 0xffff0000u) == 0) {
                G32(0x4d590c) = 1;
                G32(0x4d5910) = G32(0x6c7ff8);
                on = 1;
            }
        } else if (on == 1) {
            if (G32(0x6c7ff8) > G32(0x4d5910) + 30000) {
                G32(0x4d590c) = 0;
                on = 0;
            }
        }
        if (G32(0x4d58f0) != 0 && on != 0 && !(ps_cmp(speed, G32(0x4a334c)) & 0x41)) {
            r = ps_rng(0x4b2c50);
            if ((((r >> 15) << 5) & 0xffff0000u) == 0) {
                int32_t k = flake_free_find();

                if (k != -1)
                    flake_spawn(k);
            }
        }
        for (i = 0; i < 8; i++) {
            int32_t c = I32(FLAKE(i), 0x90);

            I32(FLAKE(i), 0x90) = c + 1;
            if (c >= 5) {
                I32(FLAKE(i), 0x98) -= 1;
                I32(FLAKE(i), 0x90) = 0;
            }
            if (I32(FLAKE(i), 0x98) <= 0)
                U32(FLAKE(i), 0) = 0;
        }
        break;
    }
    default:
        break;
    }
}

/* ===== 0x485d00: the live screen flakes (8 for clear / snow, 32 for
 * rain; single view only) as quads (packet at +4, code 0x9a, depth 3.0,
 * grey +0x98) at the centre +0x7c / +0x80, scale *0x6cd40c / 512: snow a
 * square of half size +0x84 * scale (fistp) turned by +0x9c - 0x20 (sin /
 * cos tables 0x4b24f8 / 0x4b2578, >> 14); rain a square of half size +0x84
 * * scale; clear the lens shape (four corners 0x4b2c68 / 0x4b2c78) turned
 * by +0x9c, times the scale; other kinds keep their corners. Linked at
 * *0x6ce584. ===== */
void GAMEAPI screen_flakes_draw(void)
{
    uint32_t scale = ps_div(ps_i2f(GI32(0x6cd40c)), G32(0x4a33c4));
    uint8_t kind0 = U8(g_race, 0x553);
    int32_t n = 0x20, i;

    if (U8(g_race, 0x552) != 0)
        return;
    if (kind0 == 0 || kind0 == 2)
        n = 8;
    for (i = 0; i < n; i++) {
        uint32_t f = FLAKE(i), b;
        uint8_t *pk = PTR(f + 4);

        if (U32(f, 0) == 0)
            continue;
        switch (U8(g_race, 0x553)) {
        case 2: {
            int32_t s = ps_fist(ps_mul(scale, U32(f, 0x84)));
            uint32_t a = (U32(f, 0x9c) - 0x20) & 0xff;
            uint32_t sa = ps_i2f(-(GS16(0x4b24f8 + a * 2) * s) >> 14);
            uint32_t ca = ps_i2f((GS16(0x4b2578 + a * 2) * s) >> 14);

            U32(pk, 0x10) = ps_sub(U32(f, 0x7c), sa);
            U32(pk, 0x2c) = ps_add(ca, U32(f, 0x7c));
            U32(pk, 0x48) = ps_sub(U32(f, 0x7c), ca);
            U32(pk, 0x64) = ps_add(sa, U32(f, 0x7c));
            U32(pk, 0x14) = ps_sub(U32(f, 0x80), ca);
            U32(pk, 0x30) = ps_sub(U32(f, 0x80), sa);
            U32(pk, 0x4c) = ps_add(sa, U32(f, 0x80));
            U32(pk, 0x68) = ps_add(ca, U32(f, 0x80));
            break;
        }
        case 1: {
            uint32_t c = ps_mul(scale, U32(f, 0x84));

            U32(pk, 0x48) = U32(pk, 0x10) = ps_sub(U32(f, 0x7c), c);
            U32(pk, 0x30) = U32(pk, 0x14) = ps_sub(U32(f, 0x80), c);
            U32(pk, 0x64) = U32(pk, 0x2c) = ps_add(c, U32(f, 0x7c));
            U32(pk, 0x68) = U32(pk, 0x4c) = ps_add(c, U32(f, 0x80));
            break;
        }
        case 0: {
            uint32_t a = U32(f, 0x9c) & 0xff, k;
            int32_t sn = GS16(0x4b24f8 + a * 2), cs = GS16(0x4b2578 + a * 2);

            for (k = 0; k < 4; k++) {
                int32_t t1 = GI32(0x4b2c78 + k * 4), t0 = GI32(0x4b2c68 + k * 4);

                U32(pk, 0x10 + k * 0x1c) =
                    ps_mul_add(ps_i2f((-(t1 * sn) >> 14) + ((t0 * cs) >> 14)), scale, U32(f, 0x7c));
            }
            for (k = 0; k < 4; k++) {
                int32_t t1 = GI32(0x4b2c78 + k * 4), t0 = GI32(0x4b2c68 + k * 4);

                U32(pk, 0x14 + k * 0x1c) =
                    ps_mul_add(ps_i2f(((t1 * cs) >> 14) + ((t0 * sn) >> 14)), scale, U32(f, 0x80));
            }
            break;
        }
        default:
            break;
        }
        U32(pk, 0x60) = U32(pk, 0x44) = U32(pk, 0x28) = U32(pk, 0xc) = 0x40400000u; /* 3.0 */
        U32(pk, 8) = 0x40;
        b = U32(f, 0x98);
        U32(pk, 4) = ((b << 8 | b) << 8) | b;
        U32(pk, 0) = (U32(pk, 0) & 0xffffff) | 0x0c000000u;
        U8(pk, 7) = 0x9a;
        ps_link(G32(0x6ce584), pk);
    }
}

/* ===================================================================== */
/* The weather's light level and palette. */

/* ===== 0x486240: 0x100 - (0x100 - level 0x4b2c60) * 0x4b2c5c >> 8, at
 * least 0. ===== */
int32_t GAMEAPI weather_brightness(void)
{
    int32_t v = 0x100 - ((0x100 - GI32(0x4b2c60)) * GI32(0x4b2c5c) >> 8);

    return v < 0 ? 0 : v;
}

/* ===== 0x486270: the lower of 0x4b2c5c and 0x4b2c60. ===== */
int32_t GAMEAPI weather_level_lower(void)
{
    int32_t a = GI32(0x4b2c5c), c = GI32(0x4b2c60);

    return a < c ? a : c;
}

/* ===== 0x486310: ((0x100 - 0x4b2c60) * 30 + 0x4d58ec) * 210 >> 8. ===== */
int32_t GAMEAPI weather_sky_offset(void)
{
    int32_t e = GI32(0x4d58ec) + (0x100 - GI32(0x4b2c60)) * 30;

    return e * 210 >> 8;
}

/* ===== 0x486350: the palette to draw with: the track's (*0x6c1eb0 +0x4c)
 * without a weather buffer (0x4d58fc) or with weather detail 2 (0x5d6338);
 * else the buffer, rebuilt when 0x4b2c64 is set (and cleared): a lightning
 * flash (0x4d58f4) makes every colour ff ff f0, else each colour scaled by
 * the brightness (0x486240; the original's mul keeps the previous result's
 * high bits); the fourth bytes copied. ===== */
uint32_t GAMEAPI weather_palette_get(void)
{
    const uint8_t *src = PTR(U32(G32(0x6c1eb0), 0x4c));
    uint32_t b = (uint32_t)weather_brightness();
    uint8_t *dst = PTR(G32(0x4d58fc));
    uint32_t n, r;

    if (G32(0x4d58fc) == 0 || GI32(0x5d6338) == 2)
        return U32(G32(0x6c1eb0), 0x4c);
    if (G32(0x4b2c64) != 0) {
        if (G32(0x4d58f4) != 0) {
            for (n = 0x400; n != 0; n--) {
                dst[0] = 0xff;
                dst[1] = 0xff;
                dst[2] = 0xf0;
                dst[3] = src[3];
                src += 4;
                dst += 4;
            }
        } else {
            uint32_t eax = 0, k;

            for (n = 0x400; n != 0; n--) {
                for (k = 0; k < 3; k++) {
                    eax = (eax & 0xffffff00u) | *src++;
                    eax = (eax * b) >> 8;
                    *dst++ = (uint8_t)eax;
                }
                eax = (eax & 0xffffff00u) | *src++;
                *dst++ = (uint8_t)eax;
            }
        }
    }
    r = G32(0x4d58fc);
    G32(0x4b2c64) = 0;
    return r;
}

/* ===================================================================== */
/* The snatch texture (handle 0x4b24a0, 128 x 128, 8 or 16 bits). */

/* ===== 0x486450: its palette (0x4d5914, 256 RGBX), locked (0x428c30):
 * entries 255 down to 200 a blue ramp (r = g = i / 4, b = 2i), the next 71
 * on to white; converted to 16 bits into 0x4d5d14 (0x41e1f0) and
 * unlocked (0x428c60, returned). 0 if the lock fails. ===== */
int32_t GAMEAPI snatch_palette_build(void)
{
    uint32_t l0 = 0, l1 = 0, l2 = 0;
    uint8_t *p;
    int32_t s, d, n;

    if (surface_lock(G32(0x4b24a0), 1, &l2, 0x4d5914, &l1, &l0) == 0)
        return 0;
    p = PTR(0x4d5d10);
    s = 0;
    for (n = 0x38; n != 0; n--) {
        p[0] = (uint8_t)(s >> 2);
        p[1] = (uint8_t)(s >> 2);
        p[2] = (uint8_t)(2 * s < 0xff ? 2 * s : 0xff);
        s += 2;
        p -= 4;
    }
    d = 8 * s - 0x364;
    for (n = 0x47; n != 0; n--) {
        p[0] = (uint8_t)(d < 0xff ? d : 0xff);
        p[1] = (uint8_t)(d < 0xff ? d : 0xff);
        p[2] = (uint8_t)(2 * s < 0xff ? 2 * s : 0xff);
        s++;
        d += 8;
        p -= 4;
    }
    pixels_convert(0x4d5914, 0x100, 0x4d5d14, 1);
    return surface_unlock(G32(0x4b24a0), 1, 0x4d5914);
}

/* ===== 0x486540: per frame with the option (*0x4b2198 +0x28) and a
 * texture: steps since the last call (frame 0x4be3e8 less 0x4b2c88; 1
 * unless 1..5), the surface locked and, at 8 or 16 bits, the plasma copied
 * (0x486600; the original pushes a fifth argument, 2, it never reads);
 * unlocked; 0x4b2c88 = the frame. Returns 1. ===== */
int32_t GAMEAPI snatch_texture_animate(void)
{
    uint32_t tex, l0 = 0, l1 = 0, l2 = 0;
    int32_t t, step = 1;

    if (U8(G32(0x4b2198), 0x28) == 0)
        return 1;
    tex = G32(0x4b24a0);
    if (tex == 0xffffffffu)
        return 1;
    t = GI32(0x4b2c88);
    if (t != -1) {
        step = GI32(0x4be3e8) - t;
        if (step < 1 || step > 5)
            step = 1;
    }
    if (surface_lock(tex, 1, &l2, 0x4d5914, &l0, &l1) != 0) {
        if (l0 == 8 || l0 == 0x10)
            snatch_texture_blit(step, l2, l1, l0);
        surface_unlock(G32(0x4b24a0), 1, 0x4d5914);
    }
    G32(0x4b2c88) = G32(0x4be3e8);
    return 1;
}

/* ===== 0x486600: the plasma into the locked surface (rows 128..255 of
 * pitch `pitch`, sic: dst + pitch * 128): from the offset table
 * (*0x4d5f14 at word pair (0x4d5f20 * 4) & 0xffff) a start and a step (+4);
 * each of 128 rows (plasma *0x4d5f1c, 0x200 apart) 32 dwords, each the sum
 * of the plasma at the 16-bit offset and at offset + step, | 0x80808080
 * (offsets wrap at 16 bits); 8 bits stored as is, 16 bits through the
 * palette 0x4d5d14. Then 0x4d5f20 += 3 * step, wrapped below 0xe10.
 * ===== */
void GAMEAPI snatch_texture_blit(int32_t step, uint32_t dst, uint32_t pitch, uint32_t bpp)
{
    int32_t c;

    if (bpp == 8 || bpp == 0x10) {
        uint32_t gap = pitch - (bpp == 8 ? 0x80u : 0x100u);
        uint32_t out = dst + pitch * 0x80;
        uint32_t t = ((G32(0x4d5f20) << 2) & 0xffff) + G32(0x4d5f14);
        uint16_t si0 = U16(t, 0), bx = (uint16_t)(U16(t, 2) + 4);
        uint32_t src = G32(0x4d5f1c);
        uint32_t row, col;

        for (row = 0x80; row != 0; row--) {
            uint16_t si = si0;

            for (col = 0x20; col != 0; col--) {
                uint32_t v = U32(src, si);

                si = (uint16_t)(si + bx);
                v += U32(src, si);
                si = (uint16_t)(si - bx);
                v |= 0x80808080u;
                si = (uint16_t)(si + 4);
                if (bpp == 8) {
                    U32(out, 0) = v;
                    out += 4;
                } else {
                    U32(out, 0) = (uint32_t)G16(0x4d5d14 + ((v >> 8) & 0xff) * 2) << 16 |
                                  G16(0x4d5d14 + (v & 0xff) * 2);
                    U32(out, 4) = (uint32_t)G16(0x4d5d14 + (v >> 24) * 2) << 16 |
                                  G16(0x4d5d14 + ((v >> 16) & 0xff) * 2);
                    out += 8;
                }
            }
            src += 0x200;
            out += gap;
        }
    }
    c = GI32(0x4d5f20) + step + 2 * step;
    if (c >= 0xe10)
        c -= 0xe10;
    GI32(0x4d5f20) = c;
}

/* ===== 0x486780: with a snatch texture (0x4b24a0 not -1): the plasma
 * (0x4867a0), the palette (0x486450), the first frame (0x486540).
 * Returns 1. ===== */
int32_t GAMEAPI snatch_texture_setup(void)
{
    if (G32(0x4b24a0) != 0xffffffffu) {
        snatch_plasma_build();
        snatch_palette_build();
        snatch_texture_animate();
    }
    return 1;
}

/* ===== 0x4867a0: the plasma (0x1c800 bytes, 512 x 228, from the low
 * arena 0x476b80(0x75840) at 0x4d5f18 / 0x4d5f1c): each byte (255 - (sum
 * of sqrt / sin / cos terms of x and y * 10 + 64)) / 2, sic: two of its
 * terms load the low half of a double as a float; then the offset table
 * (*0x4d5f14 = +0x1c800): 3600 word pairs (x, y * 512) of a wobble. The
 * original's instruction sequence (an 8-aligned frame, doubles passed on
 * the stack to the C runtime's sqrt 0x4a1ea4, sin 0x497ac4, cos 0x497b74
 * and _ftol 0x497a80). ===== */
#if defined(__i386__)
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl snatch_plasma_build\n"
        ".type snatch_plasma_build,@function\n"
        "snatch_plasma_build:\n"
        "    push ebp\n" /* 4867a0 */
        "    mov ebp, esp\n" /* 4867a1 */
        "    and esp, -0x8\n" /* 4867a3 */
        "    sub esp, 0x20\n" /* 4867a6 */
        "    push ebx\n" /* 4867a9 */
        "    push ebp\n" /* 4867aa */
        "    push esi\n" /* 4867ab */
        "    push edi\n" /* 4867ac */
        "    push 0x75840\n" /* 4867ad */
        "    .att_syntax prefix\n"
        "    call 0x476b80\n" /* 4867b2 */
        "    .intel_syntax noprefix\n"
        "    add esp, 0x4\n" /* 4867b7 */
        "    mov dword ptr [0x4d5f18], eax\n" /* 4867ba */
        "    test eax, eax\n" /* 4867bf */
        "    je ps_plasma_486a2f\n" /* 4867c1 */
        "    lea ecx, [eax + 0x1c800]\n" /* 4867c7 */
        "    mov dword ptr [0x4d5f1c], eax\n" /* 4867cd */
        "    mov dword ptr [0x4d5f14], ecx\n" /* 4867d2 */
        "    mov esi, eax\n" /* 4867d8 */
        "    mov dword ptr [esp + 0x14], 0x0\n" /* 4867da */
        "ps_plasma_4867e2:\n"
        "    fld dword ptr [0x4a34e8]\n" /* 4867e2 */
        "    fsub dword ptr [esp + 0x14]\n" /* 4867e8 */
        "    mov dword ptr [esp + 0x10], 0x0\n" /* 4867ec */
        "    fstp qword ptr [esp + 0x18]\n" /* 4867f4 */
        "    fld dword ptr [esp + 0x14]\n" /* 4867f8 */
        "    fdiv dword ptr [0x4a34e4]\n" /* 4867fc */
        "    mov edi, dword ptr [esp + 0x1c]\n" /* 486802 */
        "    mov ebx, dword ptr [esp + 0x18]\n" /* 486806 */
        "    fstp qword ptr [esp + 0x28]\n" /* 48680a */
        "    fld dword ptr [esp + 0x14]\n" /* 48680e */
        "    fdiv dword ptr [0x4a34e0]\n" /* 486812 */
        "    mov ebp, dword ptr [esp + 0x2c]\n" /* 486818 */
        "    fstp qword ptr [esp + 0x20]\n" /* 48681c */
        "ps_plasma_486820:\n"
        "    fld dword ptr [0x4a33c0]\n" /* 486820 */
        "    fsub dword ptr [esp + 0x10]\n" /* 486826 */
        "    push edi\n" /* 48682a */
        "    push ebx\n" /* 48682b */
        "    sub esp, 0x8\n" /* 48682c */
        "    fstp qword ptr [esp]\n" /* 48682f */
        "    .att_syntax prefix\n"
        "    call 0x4a1ea4\n" /* 486832 */
        "    .intel_syntax noprefix\n"
        "    fdiv qword ptr [0x4a34d8]\n" /* 486837 */
        "    add esp, 0x8\n" /* 48683d */
        "    fstp qword ptr [esp]\n" /* 486840 */
        "    .att_syntax prefix\n"
        "    call 0x497ac4\n" /* 486843 */
        "    .intel_syntax noprefix\n"
        "    fstp qword ptr [esp + 0x20]\n" /* 486848 */
        "    fld dword ptr [esp + 0x18]\n" /* 48684c */
        "    fadd dword ptr [esp + 0x1c]\n" /* 486850 */
        "    fdiv dword ptr [0x4a34d0]\n" /* 486854 */
        "    fstp qword ptr [esp]\n" /* 48685a */
        "    .att_syntax prefix\n"
        "    call 0x497b74\n" /* 48685d */
        "    .intel_syntax noprefix\n"
        "    fadd qword ptr [esp + 0x20]\n" /* 486862 */
        "    fstp qword ptr [esp + 0x20]\n" /* 486866 */
        "    fld dword ptr [esp + 0x18]\n" /* 48686a */
        "    fdiv dword ptr [0x4a34cc]\n" /* 48686e */
        "    fstp qword ptr [esp]\n" /* 486874 */
        "    .att_syntax prefix\n"
        "    call 0x497b74\n" /* 486877 */
        "    .intel_syntax noprefix\n"
        "    fadd qword ptr [esp + 0x20]\n" /* 48687c */
        "    fstp qword ptr [esp + 0x20]\n" /* 486880 */
        "    fld dword ptr [esp + 0x18]\n" /* 486884 */
        "    fdiv dword ptr [0x4a34c8]\n" /* 486888 */
        "    fstp qword ptr [esp]\n" /* 48688e */
        "    .att_syntax prefix\n"
        "    call 0x497ac4\n" /* 486891 */
        "    .intel_syntax noprefix\n"
        "    fadd qword ptr [esp + 0x20]\n" /* 486896 */
        "    mov edx, dword ptr [esp + 0x2c]\n" /* 48689a */
        "    mov eax, dword ptr [esp + 0x28]\n" /* 48689e */
        "    push edx\n" /* 4868a2 */
        "    push eax\n" /* 4868a3 */
        "    fstp qword ptr [esp + 0x28]\n" /* 4868a4 */
        "    .att_syntax prefix\n"
        "    call 0x497ac4\n" /* 4868a8 */
        "    .intel_syntax noprefix\n"
        "    fadd qword ptr [esp + 0x28]\n" /* 4868ad */
        "    mov ecx, dword ptr [esp + 0x38]\n" /* 4868b1 */
        "    push ebp\n" /* 4868b5 */
        "    push ecx\n" /* 4868b6 */
        "    fstp qword ptr [esp + 0x30]\n" /* 4868b7 */
        "    .att_syntax prefix\n"
        "    call 0x497b74\n" /* 4868bb */
        "    .intel_syntax noprefix\n"
        "    fadd qword ptr [esp + 0x30]\n" /* 4868c0 */
        "    add esp, 0x18\n" /* 4868c4 */
        "    fmul qword ptr [0x4a34c0]\n" /* 4868c7 */
        "    fadd qword ptr [0x4a3468]\n" /* 4868cd */
        "    .att_syntax prefix\n"
        "    call 0x497a80\n" /* 4868d3 */
        "    .intel_syntax noprefix\n"
        "    fld dword ptr [esp + 0x10]\n" /* 4868d8 */
        "    fadd dword ptr [0x4a331c]\n" /* 4868dc */
        "    or dl, -0x1\n" /* 4868e2 */
        "    sub dl, al\n" /* 4868e5 */
        "    shr dl\n" /* 4868e7 */
        "    fstp dword ptr [esp + 0x10]\n" /* 4868e9 */
        "    fld dword ptr [esp + 0x10]\n" /* 4868ed */
        "    fcomp dword ptr [0x4a33c4]\n" /* 4868f1 */
        "    mov byte ptr [esi], dl\n" /* 4868f7 */
        "    inc esi\n" /* 4868f9 */
        "    fnstsw ax\n" /* 4868fa */
        "    test ah, 0x1\n" /* 4868fc */
        "    jne ps_plasma_486820\n" /* 4868ff */
        "    fld dword ptr [esp + 0x14]\n" /* 486905 */
        "    fadd dword ptr [0x4a331c]\n" /* 486909 */
        "    fstp dword ptr [esp + 0x14]\n" /* 48690f */
        "    fld dword ptr [esp + 0x14]\n" /* 486913 */
        "    fcomp dword ptr [0x4a34bc]\n" /* 486917 */
        "    fnstsw ax\n" /* 48691d */
        "    test ah, 0x1\n" /* 48691f */
        "    jne ps_plasma_4867e2\n" /* 486922 */
        "    mov esi, dword ptr [0x4d5f14]\n" /* 486928 */
        "    mov dword ptr [esp + 0x14], 0x0\n" /* 48692e */
        "ps_plasma_486936:\n"
        "    fld dword ptr [esp + 0x14]\n" /* 486936 */
        "    fdiv dword ptr [0x4a34b8]\n" /* 48693a */
        "    sub esp, 0x8\n" /* 486940 */
        "    fstp qword ptr [esp]\n" /* 486943 */
        "    .att_syntax prefix\n"
        "    call 0x497b74\n" /* 486946 */
        "    .intel_syntax noprefix\n"
        "    fmul qword ptr [0x4a34b0]\n" /* 48694b */
        "    fadd qword ptr [0x4a34a8]\n" /* 486951 */
        "    .att_syntax prefix\n"
        "    call 0x497a80\n" /* 486957 */
        "    .intel_syntax noprefix\n"
        "    shl eax, 0x9\n" /* 48695c */
        "    mov dword ptr [esp + 0x18], eax\n" /* 48695f */
        "    fild dword ptr [esp + 0x18]\n" /* 486963 */
        "    fstp qword ptr [esp + 0x30]\n" /* 486967 */
        "    fld dword ptr [esp + 0x1c]\n" /* 48696b */
        "    fdiv dword ptr [0x4a3464]\n" /* 48696f */
        "    fstp qword ptr [esp]\n" /* 486975 */
        "    .att_syntax prefix\n"
        "    call 0x497ac4\n" /* 486978 */
        "    .intel_syntax noprefix\n"
        "    fmul qword ptr [0x4a34a0]\n" /* 48697d */
        "    fadd qword ptr [esp + 0x30]\n" /* 486983 */
        "    fadd qword ptr [0x4a3498]\n" /* 486987 */
        "    .att_syntax prefix\n"
        "    call 0x497a80\n" /* 48698d */
        "    .intel_syntax noprefix\n"
        "    fld dword ptr [esp + 0x1c]\n" /* 486992 */
        "    fdiv dword ptr [0x4a3390]\n" /* 486996 */
        "    mov edi, eax\n" /* 48699c */
        "    mov dword ptr [esp + 0x20], edi\n" /* 48699e */
        "    fstp qword ptr [esp]\n" /* 4869a2 */
        "    .att_syntax prefix\n"
        "    call 0x497ac4\n" /* 4869a5 */
        "    .intel_syntax noprefix\n"
        "    fmul qword ptr [0x4a34b0]\n" /* 4869aa */
        "    fadd qword ptr [0x4a34a8]\n" /* 4869b0 */
        "    .att_syntax prefix\n"
        "    call 0x497a80\n" /* 4869b6 */
        "    .intel_syntax noprefix\n"
        "    shl eax, 0x9\n" /* 4869bb */
        "    mov dword ptr [esp + 0x18], eax\n" /* 4869be */
        "    fild dword ptr [esp + 0x18]\n" /* 4869c2 */
        "    fstp qword ptr [esp + 0x30]\n" /* 4869c6 */
        "    fld dword ptr [esp + 0x1c]\n" /* 4869ca */
        "    fdiv dword ptr [0x4a3494]\n" /* 4869ce */
        "    fstp qword ptr [esp]\n" /* 4869d4 */
        "    .att_syntax prefix\n"
        "    call 0x497b74\n" /* 4869d7 */
        "    .intel_syntax noprefix\n"
        "    fmul qword ptr [0x4a34a0]\n" /* 4869dc */
        "    add esp, 0x8\n" /* 4869e2 */
        "    fadd qword ptr [esp + 0x28]\n" /* 4869e5 */
        "    fadd qword ptr [0x4a3498]\n" /* 4869e9 */
        "    fild dword ptr [esp + 0x18]\n" /* 4869ef */
        "    fstp qword ptr [esp + 0x28]\n" /* 4869f3 */
        "    fsub qword ptr [esp + 0x28]\n" /* 4869f7 */
        "    .att_syntax prefix\n"
        "    call 0x497a80\n" /* 4869fb */
        "    .intel_syntax noprefix\n"
        "    fld dword ptr [esp + 0x14]\n" /* 486a00 */
        "    fadd dword ptr [0x4a331c]\n" /* 486a04 */
        "    mov word ptr [esi], di\n" /* 486a0a */
        "    add esi, 0x2\n" /* 486a0d */
        "    mov word ptr [esi], ax\n" /* 486a10 */
        "    add esi, 0x2\n" /* 486a13 */
        "    fstp dword ptr [esp + 0x14]\n" /* 486a16 */
        "    fld dword ptr [esp + 0x14]\n" /* 486a1a */
        "    fcomp dword ptr [0x4a3490]\n" /* 486a1e */
        "    fnstsw ax\n" /* 486a24 */
        "    test ah, 0x1\n" /* 486a26 */
        "    jne ps_plasma_486936\n" /* 486a29 */
        "ps_plasma_486a2f:\n"
        "    pop edi\n" /* 486a2f */
        "    pop esi\n" /* 486a30 */
        "    pop ebp\n" /* 486a31 */
        "    pop ebx\n" /* 486a32 */
        "    mov esp, ebp\n" /* 486a33 */
        "    pop ebp\n" /* 486a35 */
        "    ret\n" /* 486a36 */
        ".att_syntax prefix\n");
#else
/* x86-64: the same steps in C. The x87 runs at single precision, so each
 * fadd / fsub / fmul / fdiv result is rounded to float (stored as a double
 * where the original stores a qword); _hypot is the C runtime's C
 * (crt_hypot), cos 0x497ac4 / sin 0x497b74 are libm's, _ftol truncates. */
static float ps_f32(uint32_t a)
{
    float f;

    memcpy(&f, (const void *)(uintptr_t)a, 4);
    return f;
}

static double ps_f64(uint32_t a)
{
    double d;

    memcpy(&d, (const void *)(uintptr_t)a, 8);
    return d;
}

static int32_t ps_ftol_d(double v)
{
    return (int32_t)(int64_t)v;
}

void snatch_plasma_build(void)
{
    uint8_t *p = PTR(GCALL(0x476b80, arena_alloc_low)(0x75840)), *out;
    float x, y, t;

    G32(0x4d5f18) = A(p);
    if (!p)
        return;
    G32(0x4d5f1c) = A(p);
    G32(0x4d5f14) = A(p + 0x1c800);
    out = p;
    y = 0.0f;
    do {
        double d18 = (float)(ps_f32(0x4a34e8) - y);
        double d28 = (float)(y / ps_f32(0x4a34e4));
        double d20 = (float)(y / ps_f32(0x4a34e0));

        x = 0.0f;
        do {
            double acc, st;
            int32_t r;

            st = (float)(ps_f32(0x4a33c0) - x);
            st = GCALL(0x4a1ea4, crt_hypot)(st, d18);
            st = (float)(st / ps_f64(0x4a34d8));
            acc = __builtin_cos(st);
            acc = (float)(__builtin_sin((float)((float)(x + y) / ps_f32(0x4a34d0))) + acc);
            acc = (float)(__builtin_sin((float)(x / ps_f32(0x4a34cc))) + acc);
            acc = (float)(__builtin_cos((float)(x / ps_f32(0x4a34c8))) + acc);
            acc = (float)(__builtin_cos(d20) + acc);
            st = (float)(__builtin_sin(d28) + acc);
            st = (float)(st * ps_f64(0x4a34c0));
            st = (float)(st + ps_f64(0x4a3468));
            r = ps_ftol_d(st);
            x = x + ps_f32(0x4a331c);
            *out++ = (uint8_t)((uint8_t)(0xff - (uint8_t)r) >> 1);
        } while (x < ps_f32(0x4a33c4));
        y = y + ps_f32(0x4a331c);
    } while (y < ps_f32(0x4a34bc));
    out = PTR(G32(0x4d5f14));
    t = 0.0f;
    do {
        double st, base;
        int32_t a, b;

        st = (float)(__builtin_sin((float)(t / ps_f32(0x4a34b8))) * ps_f64(0x4a34b0));
        st = (float)(st + ps_f64(0x4a34a8));
        base = (double)(int32_t)((uint32_t)ps_ftol_d(st) << 9);
        st = (float)(__builtin_cos((float)(t / ps_f32(0x4a3464))) * ps_f64(0x4a34a0));
        st = (float)(st + base);
        st = (float)(st + ps_f64(0x4a3498));
        a = ps_ftol_d(st);
        st = (float)(__builtin_cos((float)(t / ps_f32(0x4a3390))) * ps_f64(0x4a34b0));
        st = (float)(st + ps_f64(0x4a34a8));
        base = (double)(int32_t)((uint32_t)ps_ftol_d(st) << 9);
        st = (float)(__builtin_sin((float)(t / ps_f32(0x4a3494))) * ps_f64(0x4a34a0));
        st = (float)(st + base);
        st = (float)(st + ps_f64(0x4a3498));
        st = (float)(st - (double)a);
        b = ps_ftol_d(st);
        t = t + ps_f32(0x4a331c);
        U16(out, 0) = (uint16_t)a;
        U16(out, 2) = (uint16_t)b;
        out += 4;
    } while (t < ps_f32(0x4a3490));
}
#endif
