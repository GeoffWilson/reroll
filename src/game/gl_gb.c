/* Race and car setup, group GB: the track loader 0x43fa60 and its helpers
 * (the backdrop record 0x404480, the backdrop packets 0x404550), the HUD
 * layout 0x4377c0, the effect pools 0x42a610 / 0x42a710, the car's wheel
 * set-up 0x40da70, the car hit callback 0x40eb60 and its helpers, the
 * projection set-up 0x426090, the car part shake 0x46bb50, the clock read
 * 0x428290 and the shock-wave fall-off 0x4050f0 / 0x405210.
 *
 * Floating point is x87 inline asm with the original's instructions, operand
 * order and memory widths, so the results are the original's bits under the
 * game's FPU control word (clang's C float arithmetic would use SSE).
 *
 * Still original (called by address): the archive loader 0x43b550-family,
 * the allocator 0x4445e0 / 0x444570 and pool 0x460af0 / 0x460b20, object
 * creation 0x460ba0, texture and colour set-up (0x428130, 0x428350,
 * 0x428390, 0x460aa0), packet helpers (0x4270e0, 0x427a70, 0x428070,
 * 0x404980), the vertex projection functions (0x426a20, 0x426be0, 0x426b00,
 * 0x427400, and the 3DNow! versions 0x4268c0 / 0x427250): they pass the
 * depth to each other on the x87 stack, which a C function can't do, and
 * they are drawing code. */
#include "../runtime.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define S8(p, off) (*(int8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define PTR(p, off) ((uint8_t *)(uintptr_t)U32(p, off)) /* 32-bit pointers in game memory */
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define GS16(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define GF(a) (*(float *)(uintptr_t)(a))
#define GD(a) (*(double *)(uintptr_t)(a))
#define A(p) ((uint32_t)(uintptr_t)(p))

/* Every x87 register, for asm blocks that use the FPU stack. */
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"
#define FTOL 0x497a80 /* C runtime _ftol: st(0) -> edx:eax, truncating */

#if !defined(__i386__)
/* x86-64: _ftol's steps on an x87 value (rounding set to chop around a
 * fistp qword; the low dword). The asm blocks below hand it st(0) stored
 * at full width (fstpt), where i386 calls _ftol itself. */
static inline int32_t x87_ftol(long double v)
{
    uint16_t cw, chop;
    int64_t q;

    __asm__ volatile("fnstcw %0" : "=m"(cw));
    chop = (uint16_t)(cw | 0xc00);
    __asm__ volatile("fldt %2\n\tfldcw %3\n\tfistpll %0\n\tfldcw %1"
                     : "=m"(q)
                     : "m"(cw), "m"(v), "m"(chop)
                     : "st", "memory");
    return (int32_t)q;
}
#endif

#define g_config GP(0x4b2198)
#define g_race GP(0x4b1960)
#define g_track_info GP(0x6c1eb0)
#define g_textures GP(0x6c1e14)
#define g_texture_count GI32(0x6c1e10)

/* ------------------------------------------------------------------ */
/* Shock waves (bodies_apply_forces' helpers). */

/* 0x405210: the shock wave's strength at an int16 point: (strength << 15) /
 * the largest squared axis distance (in 1/4 units) from the wave's centre
 * (16.16 at 0x6ccba0), minus 1. */
int32_t GAMEAPI shock_falloff(const int16_t *p)
{
    int32_t dx = (int32_t)(p[0] - (GI32(0x6ccba0) >> 16)) >> 2;
    int32_t dy = (int32_t)(p[1] - (GI32(0x6ccba4) >> 16)) >> 2;
    int32_t dz = (int32_t)(p[2] - (GI32(0x6ccba8) >> 16)) >> 2;
    int32_t y2 = (int32_t)((uint32_t)dy * (uint32_t)dy);
    int32_t z2 = (int32_t)((uint32_t)dz * (uint32_t)dz);
    int32_t m = (int32_t)((uint32_t)dx * (uint32_t)dx);

    if (y2 > m)
        m = y2;
    if (z2 > m)
        m = z2;
    if (m == 0)
        m = 1;
    return (int32_t)(G32(0x6ccbb0) << 15) / m - 1;
}

/* 0x4050f0: while a shock wave is live, weakens each 16-byte record at
 * 0x6d06e0 (strength byte +6) by the wave's fall-off at its position. */
void GAMEAPI shock_falloff_apply(void)
{
    uint32_t i;

    if (G32(0x6ccbac) == 0)
        return;
    for (i = 0; i < G32(0x6d06c0); i++) {
        uint8_t *rec = (uint8_t *)0x6d06e0 + i * 0x10;
        int32_t s = S8(rec, 6);
        if (s > 0) {
            s -= (int32_t)GCALL(0x405210, shock_falloff)(GMEM(rec));
            if (s <= 0)
                s = 0;
            U8(rec, 6) = (uint8_t)s;
        }
    }
}

/* ------------------------------------------------------------------ */
/* The clock. */

/* 0x428290: the game time in 1/2048 s units: ftol(time / 100.0 * 2048.0),
 * the time read as an unsigned 64-bit integer. */
int32_t GAMEAPI game_clock_read(void)
{
    uint32_t q[2];
    double d;
    int32_t r;

    q[0] = GCALL(0x43692a, game_time_get)();
    q[1] = 0;
#if defined(__i386__)
    __asm__ volatile("fildll %2\n\t"
                     "fstpl %1\n\t"
                     "fldl %1\n\t"
                     "fdivl %3\n\t"
                     "fmull %4\n\t"
                     "call *%5"
                     : "=a"(r), "=m"(d)
                     : "m"(*(const uint64_t *)q), "m"(GD(0x4a3368)), "m"(GD(0x4a3360)), "c"(FTOL)
                     : "edx", "memory", "cc", X87);
#else
    {
        long double t;

        __asm__ volatile("fildll %2\n\t"
                         "fstpl %1\n\t"
                         "fldl %1\n\t"
                         "fdivl %3\n\t"
                         "fmull %4\n\t"
                         "fstpt %0"
                         : "=m"(t), "=m"(d)
                         : "m"(*(const uint64_t *)q), "m"(GD(0x4a3368)), "m"(GD(0x4a3360))
                         : "memory", X87);
        r = x87_ftol(t);
    }
#endif
    return r;
}

/* ------------------------------------------------------------------ */
/* Car hits. */

/* 0x40fd40: starts the car's hit timer (+0x6b4 = 0x100, +0x6b6 = a random
 * length by the difficulty and the car's damage class) for a hard hit when
 * it isn't already running. */
void GAMEAPI car_hit_timer_start(uint8_t *car, int32_t force)
{
    uint32_t cls = (U32(car, 0x6e8) >> 9) & 7;
    int32_t extra;
    uint32_t seed;

    if (force < 0x14 || (U16(car, 0x6b4) & 0x3ff) != 0)
        return;
    if (GI32(0x4a714c) == 0)
        extra = 10;
    else if (GI32(0x4a714c) == 1)
        extra = 8;
    else
        extra = 4;
    seed = (G32(0x4a7148) * 0x41a7) & 0x7fffffff;
    G32(0x4a7148) = seed;
    U16(car, 0x6b4) = 0x100;
    U16(car, 0x6b6) =
        (uint16_t)((((uint32_t)G8(G32(0x6cf654) + cls) + 1) * (seed >> 15) >> 16) + (uint32_t)extra + 0x1e);
}

/* 0x410050: a human car's hit on an AI car (0x40feb0) when hard enough. */
void GAMEAPI car_hit_ai_hard(uint8_t *car, uint8_t *other, int32_t force)
{
    if (force < 0x14 || U8(other, 0x70c) == 8 || U8(car, 0x6b9) == 1)
        return;
    GCALL(0x40feb0, ai_grudge_add)(GMEM(car), GMEM(other), force);
}

/* 0x410090: a hit by an object kind 0xc / 0x11 on a non-human car. */
void GAMEAPI car_hit_ai_object(uint8_t *car, uint8_t *other, int32_t kind)
{
    if (other == 0 || U8(other, 0x70c) == 8)
        return;
    if (kind == 0xc)
        GCALL(0x40feb0, ai_grudge_add)(GMEM(car), GMEM(other), 0x96);
    else if (kind == 0x11)
        GCALL(0x40feb0, ai_grudge_add)(GMEM(car), GMEM(other), 0x19);
}

/* 0x4100e0: an AI car's hit on the human car (0x40fe00). */
void GAMEAPI car_hit_human(uint8_t *car, uint8_t *other, int32_t force)
{
    if (U8(other, 0x70c) == 8 && U8(other, 0x6b9) == 1)
        GCALL(0x40fe00, ai_grudge_reduce)(GMEM(other), GMEM(car), force);
}

/* 0x410110: a hit by an object kind 0xc / 0x11 on the human car. */
void GAMEAPI car_hit_human_object(uint8_t *car, uint8_t *other, int32_t kind)
{
    if (other == 0 || U8(other, 0x70c) != 8)
        return;
    if (kind == 0xc)
        GCALL(0x40fe00, ai_grudge_reduce)(GMEM(other), GMEM(car), 0x96);
    else if (kind == 0x11)
        GCALL(0x40fe00, ai_grudge_reduce)(GMEM(other), GMEM(car), 0x19);
}

static int32_t mul16(int32_t a, int32_t b)
{
    return (int32_t)(uint32_t)((uint64_t)((int64_t)a * b) >> 16);
}

/* 0x40eb60: the car's hit callback (car +0x148): the hit's force (from the
 * contact's normal speed, or the hit object's speed), the AI / human hit
 * reactions, the strongest hit this frame (+0x65f) and its direction, the
 * flip flag, the hit sound and sparks. Returns 1. */
int32_t GAMEAPI car_hit_callback(uint8_t *car, uint8_t *obj, uint8_t *hit)
{
    int32_t force, v[3];
    uint32_t sound;
    int sparks;

    if (obj == 0) {
        int32_t d = (int32_t)GCALL(0x40d950, vec_dot)(GMEM(hit + 0x38), GMEM(car + 0x40));
        force = ((d ^ (d >> 31)) - (d >> 31)) >> 14;
        if (force > 0x7f)
            force = 0x7f;
    } else {
        uint32_t kind = U32(obj, 0) & 0x1ff;
        if (kind == 0xc) {
            uint8_t *who = PTR(obj, 0x18c);
            if (U8(car, 0x70c) == 8) {
                GCALL(0x410090, car_hit_ai_object)(GMEM(car), GMEM(who), 0xc);
                return 1;
            }
            GCALL(0x410110, car_hit_human_object)(GMEM(car), GMEM(who), 0xc);
            return 1;
        }
        force = (int32_t)GCALL(0x427960, fix_sqrt_table)((uint32_t)mul16(I32(hit, 0x64), I32(hit, 0x64)) +
                                              (uint32_t)mul16(I32(hit, 0x60), I32(hit, 0x60)) +
                                              (uint32_t)mul16(I32(hit, 0x5c), I32(hit, 0x5c))) >>
                7;
        if (force > 0x7f)
            force = 0x7f;
        if (kind == 3) {
            if (U8(car, 0x70c) == 8)
                GCALL(0x410050, car_hit_ai_hard)(GMEM(car), GMEM(obj), force);
            else
                GCALL(0x4100e0, car_hit_human)(GMEM(car), GMEM(obj), force);
            if (U8(car, 0x70c) == 8 && U8(obj, 0x70c) != 8)
                GCALL(0x40fd40, car_hit_timer_start)(GMEM(car), force);
        } else if (kind == 0x11) {
            uint8_t *who = PTR(obj, 0x180);
            if (U8(car, 0x70c) == 8)
                GCALL(0x410090, car_hit_ai_object)(GMEM(car), GMEM(who), 0x11);
            else
                GCALL(0x410110, car_hit_human_object)(GMEM(car), GMEM(who), 0x11);
        }
    }
    if (force > (int32_t)U8(car, 0x65f)) {
        int32_t *dir = (int32_t *)(car + 0x670);
        int32_t len;
        U8(car, 0x65f) = (uint8_t)force;
        dir[0] = I32(car, 0x34) - (int32_t)((uint32_t)I32(hit, 0x2c) << 8);
        dir[1] = I32(car, 0x38) - (int32_t)((uint32_t)I32(hit, 0x30) << 8);
        dir[2] = I32(car, 0x3c) - (int32_t)((uint32_t)I32(hit, 0x34) << 8);
        GCALL(0x407e90, direction_fit)(GMEM(dir));
        len = (int32_t)GCALL(0x480cd0, vec_length)(GMEM(dir));
        if (len < 0x40) {
            dir[0] = I32(car, 0x664);
            dir[1] = I32(car, 0x668);
            dir[2] = I32(car, 0x66c);
        } else {
            GCALL(0x486e90, vec_div)(GMEM(dir), len);
        }
    }
    v[0] = I32(hit, 0x38) >> 8;
    v[1] = I32(hit, 0x3c) >> 8;
    v[2] = I32(hit, 0x40) >> 8;
    if (car != PTR(hit, 4)) {
        v[0] = -v[0];
        v[1] = -v[1];
        v[2] = -v[2];
    }
    if (force > 0x1f) {
        int32_t ux = S16(car, 0xc6), uy = S16(car, 0xcc), uz = S16(car, 0xd2);
        if (U8(car, 0x63f) == 0) {
            ux = -ux;
            uy = -uy;
            uz = -uz;
        }
        force -= mul16((int32_t)((uint32_t)uz * (uint32_t)v[2] + (uint32_t)uy * (uint32_t)v[1] +
                                 (uint32_t)ux * (uint32_t)v[0]) >>
                           4,
                       force);
        if (force > 0x1f)
            U8(car, 0x661) = 1;
    }
    sparks = 0;
    if (PTR(car, 0x6ac) == (uint8_t *)0xffffffff || *PTR(car, 0x6ac) == 0) {
        sound = 0x16;
    } else {
        sparks = 1;
        sound = 0x17;
    }
    if (S16(car, 0x658) > 3)
        GCALL(0x45be10, contact_particle)(GMEM(car), GMEM(hit + 0xc), sound);
    if (sparks && force >= 0x30)
        GCALL(0x46d760, blast_mark)(GMEM(car), GMEM(v), force);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Car set-up. */

/* 0x40da70: sets up the car's four wheel records (car +0x324, 0xc4 bytes
 * each): clears them, creates each wheel's object (0x460ba0 kind 0x8c0b),
 * places it (wheel_place 0x40dff0, the car's saved +0x634 for the front
 * pair), the wheel's offsets from the table at 0x4a7b40 and 0x4a7b60
 * (mirrored for the odd wheels), and its inertia terms from the car's wheel
 * vectors (+0x180, 0x1c apart). */
void GAMEAPI car_wheels_setup(uint8_t *car, uint32_t model, uint32_t side)
{
    uint32_t saved = U32(car, 0x634);
    uint8_t *w = car + 0x324;
    uint32_t i;

    gte_load_matrix(car + 0xc4);
    for (i = 0; i < 4; i++, w += 0xc4) {
        const int16_t *tab = (const int16_t *)0x4a7b40 + i * 4;
        const uint8_t *vec = car + 0x180 + i * 0x1c;
        int32_t v[3], d, r;
        uint32_t len;
        uint16_t t;
        uint8_t *obj;
        int k;

        U8(w, 0) = 0;
        for (k = 0x28; k <= 0x30; k += 4)
            U32(w, k) = 0;
        for (k = 0x1c; k <= 0x24; k += 4)
            U32(w, k) = 0;
        for (k = 4; k <= 0x18; k += 4)
            U32(w, k) = 0;
        for (k = 0x40; k <= 0x48; k += 4)
            U32(w, k) = 0;
        I32(w, 0x34) = (int32_t)((uint32_t)(int32_t)S16(car, 0xc8) << 4);
        I32(w, 0x38) = (int32_t)((uint32_t)(int32_t)S16(car, 0xce) << 4);
        I32(w, 0x3c) = (int32_t)((uint32_t)(int32_t)S16(car, 0xd4) << 4);
        U32(w, 0x5c) = 1000;
        U32(w, 0x58) = 0;
        U32(w, 0x78) = 0;
        U32(w, 0x6c) = A(GCALL(0x460ba0, object_create)(GMEM(model), 0x8c0b, 0x180, 0));
        GCALL(0x40dff0, wheel_place)(GMEM(w), GMEM(car), i < 2 ? saved : 0);
        U16(w, 0x70) = (uint16_t)tab[0];
        U16(w, 0x72) = (uint16_t)tab[1];
        U16(w, 0x74) = (uint16_t)tab[2];
        U16(w, 2) = 0;
        U32(w, 0x80) = 0x240000;
        U32(w, 0x7c) = 0;
        U8(w, 1) = 0;
        U16(w, 0x84) = G16(0x4a7b60);
        U16(w, 0x86) = G16(0x4a7b62);
        U16(w, 0x8c) = (uint16_t)-G16(0x4a7b60);
        U16(w, 0x8e) = G16(0x4a7b62);
        U16(w, 0x94) = 0;
        U8(w, 0x9c) = (uint8_t)side;
        U16(w, 0x96) = (uint16_t)-G16(0x4a7b62);
        U8(w, 0x9d) = (uint8_t)i;
        len = G32(0x4a7b64);
        if (i & 1) {
            t = U16(w, 0x84);
            U16(w, 0x84) = 0;
            U16(w, 0x94) = t;
            t = U16(w, 0x86);
            len = 0u - len;
            U16(w, 0x86) = U16(w, 0x96);
            U16(w, 0x96) = t;
        }
        U16(w, 0x88) = (uint16_t)len;
        U16(w, 0x90) = (uint16_t)len;
        U16(w, 0x98) = (uint16_t)len;
        obj = PTR(w, 0x6c);
        U32(obj, 0x164) = A(w);
        U32(w, 0xa8) &= ~0x8000u;
        U32(w, 0xb8) = 0xffffffff;
        v[0] = (int32_t)((uint32_t)(int32_t)S16(vec, 0) << 8);
        v[1] = (int32_t)((uint32_t)(int32_t)S16(vec, 2) << 8);
        v[2] = (int32_t)((uint32_t)(int32_t)S16(vec, 4) << 8);
        d = (int32_t)GCALL(0x40d950, vec_dot)(GMEM(v), GMEM(v));
        r = (int32_t)GCALL(0x40d9d0, fix_isqrt)(d);
        I32(w, 0xbc) = mul16(d, I32(car, 0xa8));
        I32(w, 0xc0) = (int32_t)GCALL(0x42df70, fix_div16)(0x10000, r);
    }
    U32(car, 0x634) = 0;
    U8(car, 0x63f) = 0;
}

/* 0x46bb50: an object's blast at one of its model's faces (0x14-byte
 * records at model +U16(+0x34)): the face's centre rotated by the object's
 * matrix (gte_rotate 0x428090), two explosion effects (0x42aa30) at it, below
 * and above by the face's height; the velocity is scaled down by 512 first.
 * The original's GTE vector word 0x6ccc6a gets a leftover stack halfword:
 * written 0 here. */
void GAMEAPI object_face_blast(uint8_t *obj, int32_t face, int32_t *vel)
{
    uint8_t *model = PTR(obj, 0x10);
    const uint8_t *f = model + U16(model, 0x34) + face * 0x14;
    int32_t pos[3];
    uint32_t w0, w1;

    vel[0] >>= 9;
    vel[1] >>= 9;
    vel[2] >>= 9;
    w0 = U32(f, 0);
    w1 = U32(f, 4);
    gte_load_matrix(obj + 0xc4);
    G32(0x6ccc64) = w0;
    G32(0x6ccc68) = w1 & 0xffff;
    GCALL(0x428090, gte_rotate)();
    pos[0] = (int32_t)((G32(0x6cccc8) << 16) + U32(obj, 0x34));
    pos[1] = (int32_t)((G32(0x6ccccc) << 16) + U32(obj, 0x38));
    pos[2] = (int32_t)((G32(0x6cccd0) << 16) + U32(obj, 0x3c));
    pos[1] -= (int32_t)(((U32(f, 0xc) >> 10) & 0x7ff) << 14);
    GCALL(0x42aa30, explosion_effects)(pos, vel, 4, 0x26, 0x11);
    pos[1] += (int32_t)(((U32(f, 0xc) >> 10) & 0x7ff) << 15);
    GCALL(0x42aa30, explosion_effects)(pos, vel, 4, 0x26, 0x11);
}

/* ------------------------------------------------------------------ */
/* Effect pools. */

/* 0x42a710: the smoke pool: config +0x68 (smoke amount) * 0x4a3384
 * records of 0x100 bytes (pool 0x460af0), each a packet header (0x460b20
 * kind 9); the effect sprites' texture words (0x6ccb9c, 0x6cca50, ..) from
 * the track's texture base, and 12 texture entries copied to the end of the
 * texture table with their blend bits (by 0x6ccd84 bit 2). */
void GAMEAPI smoke_pool_init(void)
{
    uint8_t *pool, *rec, *src, *dst;
    uint32_t i, n, base;

#if defined(__i386__)
    __asm__ volatile("flds %1\n\t"
                     "fmuls %2\n\t"
                     "call *%3"
                     : "=a"(n)
                     : "m"(GF(g_config + 0x68)), "m"(GF(0x4a3384)), "c"(FTOL)
                     : "edx", "memory", "cc", X87);
#else
    {
        long double t;

        __asm__ volatile("flds %1\n\t"
                         "fmuls %2\n\t"
                         "fstpt %0"
                         : "=m"(t)
                         : "m"(GF(g_config + 0x68)), "m"(GF(0x4a3384))
                         : "memory", X87);
        n = (uint32_t)x87_ftol(t);
    }
#endif
    G32(0x4ac728) = n;
    pool = (uint8_t *)(uintptr_t)GCALL(0x460af0, link_pool_take)(n << 8);
    G32(0x6ccb88) = A(pool);
    G32(0x6ccb5c) = A(pool);
    G32(0x6ccb84) = (G32(0x4ac728) << 8) + A(pool);
    for (i = 0, rec = pool; i < G32(0x4ac728); i++, rec += 0x100) {
        U8(rec, 0x94) = 0;
        U16(rec, 0x96) = 0;
        U8(rec, 0x17) = 0x9a;
        U32(rec, 0x10) = (U32(rec, 0x10) & 0xffffff) | 0xc000000;
        GCALL(0x460b20, grid_link_init)(GMEM(rec), 9);
    }
    base = U16(g_track_info, 0x2e);
    G16(0x6ccb9c) = (uint16_t)(base + 0x2e);
    G8(0x6ccb9e) = 0;
    G8(0x6ccb9f) = 0xc;
    G16(0x6cca50) = (uint16_t)(U16(g_track_info, 0x2e) + 0x2e);
    G8(0x6cca52) = 0;
    G8(0x6cca53) = 0xc;
    G8(0x6cc806) = 0;
    G8(0x6cc807) = 0xc;
    G16(0x6cc804) = (uint16_t)(U16(g_track_info, 0x2e) + 0x16);
    G8(0x6ccb52) = 0;
    G16(0x6ccb50) = (uint16_t)(U16(g_track_info, 0x2e) + 0x22);
    G8(0x6ccb53) = 6;
    G8(0x6ccb82) = 0;
    G8(0x6ccb83) = 0xc;
    G16(0x6ccb80) = (uint16_t)(U16(g_track_info, 0x2e) + 0x16);
    G8(0x6cc80a) = 0;
    G16(0x6cc808) = (uint16_t)(U16(g_track_info, 0x2e) + 0x3f);
    G8(0x6cc80b) = 0;
    G8(0x6ccb5a) = 0;
    G8(0x6ccb5b) = 1;
    G16(0x6ccb58) = (uint16_t)(U16(g_track_info, 0x2e) + 0x96);
    src = g_textures + ((uint32_t)U16(g_track_info, 0x2e) + 0x2e) * 0x10;
    dst = g_textures + (uint32_t)g_texture_count * 0x10;
    for (i = 0; i < 12; i++, src += 0x10, dst += 0x10) {
        U32(dst, 0) = U32(src, 0);
        U32(dst, 4) = U32(src, 4);
        U32(dst, 8) = U32(src, 8);
        U32(dst, 0xc) = U32(src, 0xc);
        U16(dst, 6) &= 0x3f;
        if (G8(0x6ccd84) & 2)
            U8(dst, 6) |= 0x80;
        else
            U8(dst, 6) |= 0x40;
    }
    G16(0x6cc940) = (uint16_t)g_texture_count;
    G8(0x6cc942) = 0;
    G8(0x6cc943) = 0xc;
    g_texture_count += 0xc;
}

/* 0x42a610: the race's effects: the smoke pool (0x42a710) and the other
 * effect pools (0x42a8f0, 0x42c2b0, 0x42a9e0, 0x42c1d0, 0x42c200), the smoke
 * records linked both ways, and the smoke amount (config +0x68) clamped to
 * 1..10 with 0x4ac724 = round(amount * 40). */
void GAMEAPI effects_init(void)
{
    uint8_t *rec, *prev;
    uint32_t i;
    uint16_t sw;
    int32_t r;
    float t;

    G32(0x6ccbac) = 0;
    G32(0x6cca54) = 0;
    GCALL(0x42a710, smoke_pool_init)();
    GCALL(0x42a8f0, smoke_clouds_init)();
    GCALL(0x42c2b0, shock_waves_init)();
    /* flashes_init reads no argument; the original pushes 1 */
    ((void(GAMEAPI *)(uint32_t))(uintptr_t)0x42a9e0)(1);
    GCALL(0x42c1d0, palette_ramp_init)();
    GCALL(0x42c200, flares_init)();
    G32(0x6ccb7c) = 0;
    G32(0x4be12c) = 0;
    G32(0x4be130) = 0;
    G32(0x4be128) = G32(0x6ccb5c);
    rec = (uint8_t *)(uintptr_t)G32(0x6ccb5c);
    prev = 0;
    for (i = 0; i < G32(0x4ac728); i++) {
        U32(rec, 0xa8) = A(prev);
        U32(rec, 0xac) = A(rec + 0x100);
        prev = rec;
        rec += 0x100;
    }
    U32((uint8_t *)(uintptr_t)G32(0x6ccb5c), (G32(0x4ac728) << 8) - 0x54) = 0;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %%ax"
                     : "=a"(sw)
                     : "m"(GF(g_config + 0x68)), "m"(GF(0x4a331c))
                     : "cc", X87);
    if (sw & 0x100)
        U32(g_config, 0x68) = 0x3f800000;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %%ax"
                     : "=a"(sw)
                     : "m"(GF(g_config + 0x68)), "m"(GF(0x4a3334))
                     : "cc", X87);
    if (!(sw & 0x4100))
        U32(g_config, 0x68) = 0x41200000;
    __asm__ volatile("flds %2\n\t"
                     "fmuls %3\n\t"
                     "fstps %1\n\t"
                     "flds %1\n\t"
                     "fistpl %0"
                     : "=m"(r), "=m"(t)
                     : "m"(GF(g_config + 0x68)), "m"(GF(0x4a3380))
                     : X87);
    GI32(0x4ac724) = r;
}

/* ------------------------------------------------------------------ */
/* Projection set-up. */

/* 0x426090: clears the GTE registers (0x6ccbe0) and the projection state
 * (0x6ccd00), the screen set-up 0x4283d0(150, 400, 507), the depth limits,
 * the field of view scale 0x6cd41c = max(width * 0.6 / 640, 0.6) and
 * 0x6cd418 = 1.5 times it, the float matrices' 1.0 fourth columns, and the
 * projection functions (the 3DNow! ones when 0x401110 finds 3DNow!). */
void GAMEAPI projection_init(void)
{
    uint16_t sw;
    uint32_t f;

    GCALL(0x476f50, mem_fill)(GMEM(0x6ccbe0), 0, 0x104);
    GCALL(0x476f50, mem_fill)(GMEM(0x6ccd00), 0, 0x720);
    GCALL(0x4283d0, depth_range_set)(0x96, 400, 0x1fb);
    G32(0x6ccd88) = 0x200;
    G32(0x6ccd04) = 0x40000000;
    G32(0x6ccd08) = 0x477fff00;
    G32(0x6ccdc0) = 0x3f800000;
    G32(0x6cce58) = 0xffffffff;
    __asm__ volatile("fildl %2\n\t"
                     "fstps %1\n\t"
                     "flds %1\n\t"
                     "fmuls %3\n\t"
                     "fdivs %4\n\t"
                     "fcomps %3\n\t"
                     "fnstsw %%ax"
                     : "=a"(sw), "=m"(f)
                     : "m"(G32(0x4acc38)), "m"(GF(0x4a335c)), "m"(GF(0x4a3358))
                     : "cc", X87);
    if (!(sw & 0x4100))
        __asm__ volatile("fildl %2\n\t"
                         "fstps %1\n\t"
                         "flds %1\n\t"
                         "fmuls %3\n\t"
                         "fdivs %4\n\t"
                         "fstps %0"
                         : "=m"(GF(0x6cd41c)), "+m"(f)
                         : "m"(G32(0x4acc38)), "m"(GF(0x4a335c)), "m"(GF(0x4a3358))
                         : X87);
    else
        G32(0x6cd41c) = 0x3f19999a;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfstps %0"
                     : "=m"(GF(0x6cd418))
                     : "m"(GF(0x6cd41c)), "m"(GF(0x4a3354))
                     : X87);
    G32(0x6ccd28) = 0x3f800000;
    G32(0x6ccd38) = 0x3f800000;
    G32(0x6ccd48) = 0x3f800000;
    G32(0x6ccddc) = 0x3f800000;
    G32(0x6ccdec) = 0x3f800000;
    G32(0x6ccdfc) = 0x3f800000;
    G32(0x6cce0c) = 0x3f800000;
    G32(0x6cce1c) = 0x3f800000;
    G32(0x6cce2c) = 0x3f800000;
    if (GCALL(0x401110, cpu_3dnow_get)() != 0) {
        G32(0x6ccd80) = 0x4291f0;
        G32(0x6ccd78) = 0x4268c0;
        G32(0x6ccd7c) = 0x4268c0;
        G32(0x6cce3c) = 0x427250;
        return;
    }
    G32(0x6ccd78) = 0x426a20;
    G32(0x6ccd7c) = 0x426be0;
    G32(0x6ccd80) = 0x426b00;
    G32(0x6cce3c) = 0x427400;
}

/* ------------------------------------------------------------------ */
/* The track's sky. */

/* 0x404480: the sky record (0x6d0a40) and its height / 4 (0x6d0a7c, and as
 * a float at 0x4b5fac, converted as an unsigned 64-bit integer, sic). */
void GAMEAPI sky_set(uint8_t *sky)
{
    uint32_t q[2];
    int32_t h = S16(sky, 4);

    G32(0x6d0a40) = A(sky);
    h = (int32_t)((uint32_t)h + ((uint32_t)(h >> 31) & 3)) >> 2;
    q[0] = (uint32_t)h;
    q[1] = 0;
    GI32(0x6d0a7c) = h;
    __asm__ volatile("fildll %1\n\tfstps %0" : "=m"(GF(0x4b5fac)) : "m"(*(const uint64_t *)q) : X87);
}

/* Copies a sky quad's 4 vertices (3 int16 each, 8 bytes apart). */
static void sky_quad_copy(uint8_t *dst, const uint8_t *src)
{
    int k;

    for (k = 0; k < 4; k++) {
        U16(dst, k * 8) = U16(src, k * 8);
        U16(dst, k * 8 + 2) = U16(src, k * 8 + 2);
        U16(dst, k * 8 + 4) = U16(src, k * 8 + 4);
    }
}

/* A sky packet's texture words from texture entry tex. */
static void sky_packet_texture(uint8_t *pk, const uint8_t *tex)
{
    U32(pk, 0x20) = U32(tex, 0);
    U32(pk, 0x3c) = U32(tex, 4);
    U16(pk, 0x58) = U16(tex, 8);
    U16(pk, 0x74) = U16(tex, 0xa);
    U32(pk, 0) = (U32(pk, 0) & 0xffffff) | 0xc000000;
    U8(pk, 7) = 0x98;
}

/* 0x404550: the sky's packets from the track archive's sky quads (0x22-byte
 * records at +I32(+0x1c)): 0x6d0a78 backdrop quads (vertices to *0x6d0a3c,
 * packets *0x4b5fc8, all with the lowest texture number among them), then
 * 0x6d0a00 cloud quads (halved or dropped by config +0x33; vertices to
 * *0x6d0a38, packets *0x4b5fcc, flagged by 0x4270e0), and the sky's lowest
 * point against the horizon (0x404980) as a float at 0x4b5fe0. */
void GAMEAPI sky_packets_build(uint8_t *arc)
{
    uint8_t *rec = arc + I32(arc, 0x1c);
    uint8_t *dst, *pk;
    const uint8_t *tex;
    uint32_t lowest = 0xffff, i, saved, n;
    int32_t horizon, m, out;
    uint8_t mode;

    if (G32(0x6d0a78) != 0) {
        const uint8_t *r = rec + 0x20;
        uint32_t left = G32(0x6d0a78), m16 = 0xffff;
        do {
            if ((int32_t)U16(r, 0) < (int32_t)m16)
                m16 = U16(r, 0);
            r += 0x22;
        } while (--left != 0);
        lowest = m16;
    }
    dst = (uint8_t *)(uintptr_t)G32(0x6d0a3c);
    for (i = 0; i < G32(0x6d0a78); i++) {
        pk = (uint8_t *)(uintptr_t)G32(0x4b5fc8) + i * 0x78;
        sky_quad_copy(dst, rec);
        tex = g_textures + lowest * 0x10;
        U32(pk, 4) = 0xffffffff;
        rec += 0x22;
        dst += 0x20;
        sky_packet_texture(pk, tex);
    }
    mode = U8(g_config, 0x33);
    if (mode == 0)
        G32(0x6d0a00) = 0;
    else if (mode == 1)
        G32(0x6d0a00) >>= 1;
    dst = (uint8_t *)(uintptr_t)G32(0x6d0a38);
    for (i = 0; i < G32(0x6d0a00); i++) {
        pk = (uint8_t *)(uintptr_t)G32(0x4b5fcc) + i * 0x78;
        sky_quad_copy(dst, rec);
        tex = g_textures + (uint32_t)U16(rec, 0x20) * 0x10;
        U32(pk, 4) = 0x30303030;
        sky_packet_texture(pk, tex);
        GCALL(0x4270e0, prim_flag2_set)(GMEM(pk), 1);
        dst += 0x20;
        rec += 0x22;
    }
    saved = G32(0x6cd400);
    dst = (uint8_t *)(uintptr_t)G32(0x6d0a38);
    horizon = (int32_t)0xfffff448;
    G32(0x6cd400) = 0x40;
    GCALL(0x404980, dome_scale)(GMEM(&horizon));
    G32(0x6cd400) = saved;
    m = 0;
    n = G32(0x6d0a00);
    if (n != 0) {
        const uint8_t *v = dst + 2;
        do {
            int k;
            for (k = 0; k < 4; k++, v += 8)
                if (S16(v, 0) < m)
                    m = S16(v, 0);
        } while (--n != 0);
    }
    out = (int16_t)horizon;
    if (out >= m - 0x200) {
        int32_t q = (int32_t)((uint32_t)(m - 0x200) << 8) / out;
        out = (int16_t)((uint32_t)(out * q) >> 8);
    }
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(GF(0x4b5fe0)) : "m"(out) : X87);
}

/* ------------------------------------------------------------------ */
/* The HUD layout. */

/* The HUD items' texture numbers (from the track's texture base). */
static const uint8_t hud_textures[0x43] = {
    0x7f, 0x7f, 0x71, 0x71, 0x7c, 0x7c, 0x7c, 0x7c, 0x7e, 0x7b, 0x7b, 0x79, 0x7a, 0x7d, 0x7d, 0x7d, 0x7d,
    0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d,
    0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x7e, 0x7e, 0x7e, 0x81, 0x70, 0x82,
    0x70, 0x83, 0x97, 0x97, 0x97, 0x97, 0x97, 0x97, 0xa3, 0x7d, 0x7d, 0x7d, 0x7d, 0x7d, 0x5b, 0x60};

#define HW(a) G16(a) /* HUD layout words */

/* (float)n via fild dword / fstp dword: the float's bits. */
static uint32_t int_to_float_bits(int32_t n)
{
    uint32_t r;

    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(n) : X87);
    return r;
}

/* A HUD packet's far corner: w and h (unsigned, loaded as 64-bit integers)
 * as floats, added to the near corner (+0x10 / +0x14, the second vertex
 * +0x48 / +0x30). */
static void hud_packet_size(uint8_t *pk, uint32_t w, uint32_t h)
{
    uint32_t qw[2] = {w, 0}, qh[2] = {h, 0};
    float t;

    __asm__ volatile("fildll %5\n\t"
                     "fstps %4\n\t"
                     "flds %4\n\t"
                     "fadds 0x10(%7)\n\t"
                     "fstps %0\n\t"
                     "flds %4\n\t"
                     "fadds 0x48(%7)\n\t"
                     "fstps %1\n\t"
                     "fildll %6\n\t"
                     "fstps %4\n\t"
                     "flds %4\n\t"
                     "fadds 0x14(%7)\n\t"
                     "fstps %2\n\t"
                     "flds %4\n\t"
                     "fadds 0x30(%7)\n\t"
                     "fstps %3"
                     : "=m"(GF(pk + 0x2c)), "=m"(GF(pk + 0x64)), "=m"(GF(pk + 0x4c)), "=m"(GF(pk + 0x68)), "=m"(t)
                     : "m"(*(const uint64_t *)qw), "m"(*(const uint64_t *)qh), "r"(pk)
                     : "memory", X87);
}

/* 0x4377c0: lays out the HUD for the screen size (race +0xa80 / +0xa82,
 * scaled by width * 256 / 0x4acff0, which is 0x226 in split screen): the
 * item positions (int16 pairs from 0x6c7dc8), the lap time digit rows
 * (0x6c7dfc, 4 rows of 8), the items shown (0x6c7f5a, by the race mode and
 * kind), the two textured sprite packets per item at 0x6c3ec0, the split
 * screen player markers (0x6c7fac) and the HUD state reset (0x427a70,
 * 0x4387d0, 0x439d70, 0x478ff0). */
void GAMEAPI hud_layout(void)
{
    uint8_t *race = g_race;
    uint32_t W = U16(race, 0xa80), H = U16(race, 0xa82), u, k, i;
    uint8_t *pk;

#define T(n) (((uint32_t)(n) * u) >> 8)
    if (U8(race, 0x552) != 0)
        G32(0x4acff0) = 0x226;
    for (i = 0; i < 0x43; i++) {
        HW(0x6c7ed4 + i * 2) = hud_textures[i];
        G8(0x6c7f5a + i) = 1;
    }
    if (G8(0x6ccd84) & 4) {
        G8(0x6c7f5a) = 0;
        G8(0x6c7f5b) = 0;
    }
    u = (uint32_t)((int32_t)(G32(0x4acc38) << 8) / GI32(0x4acff0));
    HW(0x6c7dc8) = (uint16_t)T(13);
    HW(0x6c7dca) = (uint16_t)(W - T(38));
    HW(0x6c7dcc) = (uint16_t)T(40);
    HW(0x6c7dd4) = (uint16_t)T(40);
    HW(0x6c7dd2) = (uint16_t)(W - T(38));
    HW(0x6c7e8c) = (uint16_t)(T(13) - T(25));
    HW(0x6c7dce) = (uint16_t)(W - T(38));
    HW(0x6c7dd6) = (uint16_t)(W - T(38));
    HW(0x6c7e8e) = (uint16_t)(W - T(38) - T(3));
    HW(0x6c7dec) = (uint16_t)T(8);
    HW(0x6c7dee) = (uint16_t)T(8);
    HW(0x6c7df2) = (uint16_t)(T(8) + T(10));
    HW(0x6c7dd8) = (uint16_t)(T(8) + T(25));
    HW(0x6c7dda) = (uint16_t)(T(8) - T(4));
    HW(0x6c7dde) = (uint16_t)(T(8) - T(4));
    HW(0x6c7ddc) = (uint16_t)(T(8) + T(25) + T(8));
    HW(0x6c7dd0) = (uint16_t)T(13);
    HW(0x6c7e90) = (uint16_t)(HW(0x6c7ddc) + T(9));
    HW(0x6c7e92) = (uint16_t)(T(8) - T(4) + T(3));
    HW(0x6c7de2) = (uint16_t)(T(8) - T(4));
    HW(0x6c7de6) = (uint16_t)(T(8) - T(4));
    HW(0x6c7de0) = (uint16_t)(T(13) + HW(0x6c7e90));
    HW(0x6c7de4) = (uint16_t)(HW(0x6c7de0) + T(8));
    HW(0x6c7df0) = (uint16_t)T(8);
    HW(0x6c7df6) = (uint16_t)T(8);
    HW(0x6c7df4) = (uint16_t)(H - T(78));
    HW(0x6c7df8) = (uint16_t)(H - T(78));
    HW(0x6c7dfa) = (uint16_t)(T(8) + T(10));
    HW(0x6c7de8) = (uint16_t)(T(34) + HW(0x6c7df4));
    HW(0x6c7dea) = (uint16_t)(T(22) + T(8));
    HW(0x6c7e98) = (uint16_t)(HW(0x6c7de8) + T(14));
    HW(0x6c7e9a) = (uint16_t)(T(22) + T(8) + T(9));
    HW(0x6c7f9e) = (uint16_t)(H - T(60));
    HW(0x6c7fa0) = (uint16_t)(W - T(32));
    HW(0x6c7e94) = (uint16_t)(H - T(60) - T(22));
    HW(0x6c7e96) = (uint16_t)(W - T(32) - T(9));
    HW(0x6c7e7c) = (uint16_t)(H - T(60) - T(14));
    HW(0x6c7e7e) = (uint16_t)(W - T(32) - T(4));
    HW(0x6c7e82) = (uint16_t)(W - T(32) - T(4));
    HW(0x6c7e86) = (uint16_t)(W - T(32) - T(4));
    HW(0x6c7e80) = (uint16_t)(T(14) + HW(0x6c7e7c));
    HW(0x6c7e84) = (uint16_t)(T(28) + HW(0x6c7e7c));
    HW(0x6c7e88) = (uint16_t)(T(42) + HW(0x6c7e7c));
    HW(0x6c7e8a) = (uint16_t)(T(14) + HW(0x6c7e7e));
    HW(0x6c7ecc) = (uint16_t)((H >> 1) - T(32));
    HW(0x6c7ed0) = (uint16_t)((H >> 1) - T(32));
    HW(0x6c7ece) = (uint16_t)(W >> 1);
    HW(0x6c7ed2) = (uint16_t)((W >> 1) - T(68));
    if (U8(race, 0x552) == 2) {
        HW(0x6c7df0) = HW(0x6c7dec);
        HW(0x6c7df4) = HW(0x6c7dec);
        HW(0x6c7df8) = HW(0x6c7dec);
        HW(0x6c7df2) = (uint16_t)(T(30) + HW(0x6c7dee));
        HW(0x6c7df6) = (uint16_t)(HW(0x6c7df2) - T(10));
        HW(0x6c7dfa) = (uint16_t)(HW(0x6c7df6) - T(10));
        HW(0x6c7de8) = (uint16_t)(H - T(50));
        HW(0x6c7dea) = (uint16_t)T(8);
        HW(0x6c7e98) = (uint16_t)(HW(0x6c7de8) + T(14));
        HW(0x6c7e9a) = (uint16_t)(T(8) + T(9));
    }
    /* the lap time digit rows */
    for (k = 0; k < 4; k++) {
        uint32_t x, y;
        int j;
        if (k == 1) {
            x = T(30) + (uint32_t)(int32_t)GS16(0x6c7df4);
            y = HW(0x6c7df6);
        } else if (k == 2) {
            x = T(30) + (uint32_t)(int32_t)GS16(0x6c7df8);
            y = HW(0x6c7dfa);
        } else if (k == 3) {
            x = (uint32_t)(int32_t)GS16(0x6c7ed0) + T(14);
            y = (uint32_t)GS16(0x6c7ed2) + T(25);
        } else {
            x = T(30) + (uint32_t)(int32_t)GS16(0x6c7df0);
            y = HW(0x6c7df2);
        }
        for (j = -1; j < 7; j++) {
            if (j == 0 || j == 3 || j == 6)
                x += T(7);
            else if (j >= 0)
                x += T(5);
            x -= 2;
            HW(0x6c7dfc + k * 0x20 + (j + 1) * 4) = (uint16_t)x;
            HW(0x6c7dfe + k * 0x20 + (j + 1) * 4) = (uint16_t)y;
        }
    }
    for (i = 0; i < 6; i++) {
        G8(0x6c7f8f + i) = 0;
        G8(0x6c7f95 + i) = 0;
    }
    race = g_race;
    if (U8(race, 0xa86) == 0 && U8(race, 0x551) != 3 && U8(race, 0x551) != 8) {
        G8(0x6c7f5a) = 0;
        G8(0x6c7f5b) = 0;
        G8(0x6c7f5c) = 0;
        G8(0x6c7f5d) = 0;
        G8(0x6c7f8b) = 0;
    }
    switch (U8(race, 0x551)) {
    case 1:
        G8(0x6c7f62) = 0;
        G8(0x6c7f8e) = 0;
        /* fall through */
    case 4:
        G8(0x6c7f63) = 0;
        G8(0x6c7f5e) = 0;
        G8(0x6c7f60) = 0;
        G8(0x6c7f5f) = 0;
        G8(0x6c7f61) = 0;
        G8(0x6c7f8c) = 0;
        break;
    case 3:
    case 8:
        G8(0x6c7f62) = 0;
        G8(0x6c7f8e) = 0;
        G8(0x6c7f8c) = 0;
        G8(0x6c7f63) = 0;
        G8(0x6c7f64) = 0;
        G8(0x6c7f65) = 0;
        G8(0x6c7f66) = 0;
        for (i = 0; i < 8; i++)
            G32(0x6c7f67 + i * 4) = 0;
        HW(0x6c7de0) = (uint16_t)(HW(0x6c7dd8) + T(8));
        HW(0x6c7de2) = HW(0x6c7dda);
        G8(0x6c7f5f) = 0;
        G8(0x6c7f61) = 0;
        break;
    }
    GCALL(0x427a70, ot_clear_reverse)(GMEM(0x6ce5f0), 2);
    GCALL(0x427a70, ot_clear_reverse)(GMEM(0x6ce5f8), 2);
    G32(0x6ce608) = 0x6ce5f0;
    {
        uint32_t t = U16(g_textures, ((uint32_t)(int32_t)GS16(0x6c7ed4) + U16(g_track_info, 0x2e)) * 0x10 + 2);
        HW(0x6c7dc0) = (uint16_t)((t & 0x3f) << 4);
        HW(0x6c7dc2) = (uint16_t)(t >> 6);
    }
    HW(0x6c7dc4) = 0x10;
    HW(0x6c7dc6) = 1;
    HW(0x6c7da0) = 0;
    /* the items' sprite packets, two per item */
    pk = (uint8_t *)0x6c3ec0;
    for (i = 0; i < 0x43; i++) {
        int32_t idx = GS16(0x6c7ed4 + i * 2);
        const uint8_t *tex = g_textures + ((uint32_t)U16(g_track_info, 0x2e) + (uint32_t)idx) * 0x10;
        int32_t w = (int32_t)U8(tex, 0xa) - (int32_t)U8(tex, 0) + 1;
        int32_t h = (int32_t)U8(tex, 0xb) - (int32_t)U8(tex, 1) + 1;
        int n;

        if (idx == 0x7c || idx == 0x7d)
            w = 0xb;
        else if (idx == 0x83)
            w = (w + (int32_t)((uint32_t)(w >> 31) & 3)) >> 2;
        for (n = 0; n < 2; n++, pk += 0x78) {
            uint32_t bits, sw, sh;
            uint8_t a, b;

            U32(pk, 0x20) = U32(tex, 0);
            U32(pk, 0x3c) = U32(tex, 4);
            bits = int_to_float_bits(GS16(0x6c7dc8 + i * 4));
            U32(pk, 0x48) = bits;
            U32(pk, 0x10) = bits;
            bits = int_to_float_bits(GS16(0x6c7dca + i * 4));
            U32(pk, 0x30) = bits;
            U32(pk, 0x14) = bits;
            u = (uint32_t)((int32_t)(G32(0x4acc38) << 8) / GI32(0x4acff0));
            if (idx >= 0x70 && (idx <= 0x71 || idx == 0x7f)) {
                sw = (((uint32_t)w * 200) >> 9) * u >> 8;
                sh = (((uint32_t)h * 200) >> 9) * u >> 8;
            } else {
                sw = (uint32_t)(w >> 1) * u >> 8;
                sh = (uint32_t)(h >> 1) * u >> 8;
            }
            hud_packet_size(pk, sw, sh);
            U8(pk, 0x3d) = U8(pk, 0x21);
            U8(pk, 0x58) = U8(pk, 0x20);
            a = (uint8_t)(w + U8(pk, 0x20) - 1);
            U8(pk, 0x74) = a;
            U8(pk, 0x3c) = a;
            a = (uint8_t)(h + U8(pk, 0x21) - 1);
            U8(pk, 0x75) = a;
            U8(pk, 0x59) = a;
            if (i == 0x31) {
                a = U8(pk, 0x20);
                U8(pk, 0x20) = U8(pk, 0x3c);
                b = U8(pk, 0x74);
                U8(pk, 0x3c) = a;
                a = U8(pk, 0x58);
                U8(pk, 0x58) = b;
                U8(pk, 0x74) = a;
            }
            U32(pk, 8) = 0x40;
            U32(pk, 0x60) = 0x4000a3d7;
            U32(pk, 0x44) = 0x4000a3d7;
            U32(pk, 0x28) = 0x4000a3d7;
            U32(pk, 0xc) = 0x4000a3d7;
            U8(pk, 7) = 0x9a;
            U32(pk, 0) = (U32(pk, 0) & 0xffffff) | 0xc000000;
        }
    }
    if (U8(g_race, 0x552) != 0) {
        const uint8_t *tex = g_textures + ((uint32_t)U16(g_track_info, 0x2e) + 0x85) * 0x10;
        for (i = 0; i < 2; i++) {
            uint8_t *m = (uint8_t *)0x6c7fb8 + i * 0x1c;
            race = g_race;
            U32(m, -0xc) = (U32(m, -0xc) & 0xffffff) | 0x4000000;
            U8(m, -5) = 0x66;
            U32(m, 0) = U32(tex, 0);
            U8(m, 0) = (uint8_t)(U8(race, 0x1c + i) * 6 + U8(tex, 0));
            if (U8(race, 0x552) == 2)
                U8(m, 1) = U8(tex, 1);
            else
                U8(m, 1) = (uint8_t)(U8(tex, 1) + 6);
            U16(m, 4) = 6;
            U16(m, 6) = 6;
            GCALL(0x428070, list_push)(GMEM(m - 0x14), GMEM(m - 0xc));
        }
    }
    GCALL(0x4387d0, hud_sprite_set)(2, 0, -1);
    GCALL(0x4387d0, hud_sprite_set)(3, 0, -1);
    for (i = 0x6c7fe1; i < 0x6c7ff9; i += 4) {
        G8(i - 1) = 0;
        G8(i) = 0;
        G8(i + 1) = 0;
    }
    GCALL(0x439d70, hud_fade_apply)(0, 0, 0);
    GCALL(0x478ff0, text_reset)();
    if (U8(g_race, 0x552) != 0) {
        uint32_t v = G32(0x6c7fb0) & 0xff000000;
        G32(0x6c7fb0) = v;
        G32(0x6c7fcc) = v;
    }
#undef T
}

/* ------------------------------------------------------------------ */
/* The track loader. */

#define g_track_base G32(0x6c1e28) /* the loaded track file's address */
#define INFO_REL(off) (U32(g_track_info, off) += g_track_base)

static void str_copy(char *dst, const char *src)
{
    while ((*dst++ = *src++) != 0)
        ;
}

static void str_cat(char *dst, const char *src)
{
    while (*dst)
        dst++;
    str_copy(dst, src);
}

/* A light colour component doubled by the light scale (0x6ccdc0), clamped
 * to 255 and truncated (ftol). */
static uint8_t colour_scale(uint8_t v)
{
    int32_t n = v, r;
    uint32_t t, sel;
    uint16_t sw;

    __asm__ volatile("fildl %2\n\t"
                     "fstps %1\n\t"
                     "flds %1\n\t"
                     "fmuls %3\n\t"
                     "fadd %%st(0), %%st\n\t"
                     "fstps %1\n\t"
                     "flds %1\n\t"
                     "fcomps %4\n\t"
                     "fnstsw %%ax"
                     : "=a"(sw), "=m"(t)
                     : "m"(n), "m"(GF(0x6ccdc0)), "m"(GF(0x4a332c))
                     : "cc", X87);
    sel = (sw & 0x100) ? t : 0x437f0000;
#if defined(__i386__)
    __asm__ volatile("flds %1\n\tcall *%2" : "=a"(r) : "m"(sel), "c"(FTOL) : "edx", "memory", "cc", X87);
#else
    {
        long double t;

        __asm__ volatile("flds %1\n\tfstpt %0" : "=m"(t) : "m"(sel) : "memory", X87);
        r = x87_ftol(t);
    }
#endif
    return (uint8_t)r;
}

/* 0x43fa60: loads the track `name`: its file (name + ".BL6" / ".BLP" by
 * 0x429340, archive loader 0x43b660) becomes the track header 0x6c1eb0, its
 * offsets are made addresses, the light colours are scaled, the sky and the
 * horizon are set, the texture file (name + ".BT6" / ".BTP") is uploaded
 * (0x428130), the track record `track` (0x6bfac0) is filled in and the
 * track's objects, flares, lights and pick-ups are created from the texture
 * file's object records (0x38 bytes); returns 0 if the textures failed. */
int32_t GAMEAPI track_load(const char *name, uint8_t *track)
{
    int32_t chain = 0, lights = 0, n, cnt, i, j;
    uint32_t v;
    uint8_t *packets, *objs, *arc2, *recs, *a38, *a3c, *list, *p;
    uint32_t local_4, local_8, local_10, local_c, local_14, local_18, light_base;
    const char *ext;

    if (GCALL(0x429340, textures_unusable_thunk)() != 0) {
        str_copy((char *)0x6c1dc0, name);
        str_cat((char *)0x6c1dc0, (const char *)0x4af4d4);
        str_copy((char *)0x6c1e60, name);
        ext = (const char *)0x4af4cc;
    } else {
        str_copy((char *)0x6c1dc0, name);
        str_cat((char *)0x6c1dc0, (const char *)0x4af4c4);
        str_copy((char *)0x6c1e60, name);
        ext = (const char *)0x4af4bc;
    }
    str_cat((char *)0x6c1e60, ext);
    v = GCALL(0x43b660, archive_load_by_name)(GMEM(0x6c1dc0));
    g_track_base = v;
    G32(0x6c1eb0) = v;
    GCALL(0x460aa0, object_pool_reset)((int32_t)S16(g_track_info, 0x11e));
    v = U32(g_track_info, 0x54);
    G32(0x4af4b8) = v;
    G8(0x6cd4b9) = (uint8_t)(U8(g_track_info, 0x30) << 1);
    G8(0x6cdd29) = G8(0x6cd4b9);
    G8(0x6cd4ba) = (uint8_t)(U8(g_track_info, 0x31) << 1);
    G8(0x6cdd2a) = G8(0x6cd4ba);
    G8(0x6cd4bb) = (uint8_t)(U8(g_track_info, 0x32) << 1);
    G8(0x6cdd2b) = G8(0x6cd4bb);
    GCALL(0x428350, ambient_colour_set)(v, (v >> 8) & 0xff, G8(0x4af4ba));
    GCALL(0x428390, clear_colour_store)((uint8_t)(U8(g_track_info, 0x30) << 1), (uint8_t)(U8(g_track_info, 0x31) << 1), (uint8_t)(U8(g_track_info, 0x32) << 1));
    n = I32(g_track_info, 8);
    packets = (uint8_t *)(uintptr_t)GCALL(0x4445e0, kr_malloc)((uint32_t)n * 0x120);
    if (G32(0x4beb84) == 0) {
        v = GCALL(0x4445e0, kr_malloc)(0x60ae0);
        G32(0x4beb84) = v;
        G32(0x6c1e1c) = v + 0x30570;
        G32(0x6c1e34) = v + 0x30570;
        G32(0x6c1e38) = v + 0x60ae0;
    }
    local_4 = U32(g_track_info, 0x88) + g_track_base;
    local_10 = U32(g_track_info, 0x8c) + g_track_base;
    local_c = U32(g_track_info, 0x98) + g_track_base;
    for (j = 0; j < (int32_t)U8(g_track_info, 0x79); j++)
        U32((uint8_t *)(uintptr_t)local_10, 8 + j * 0xc) += local_c;
    local_8 = U32(g_track_info, 0x1c) + g_track_base;
    INFO_REL(0x14);
    INFO_REL(0x7c);
    INFO_REL(0x80);
    INFO_REL(0x18);
    INFO_REL(0x34);
    INFO_REL(0x40);
    INFO_REL(0x3c);
    INFO_REL(0x4c);
    G32(0x6c1e20) = U32(g_track_info, 0x4c);
    INFO_REL(0x20);
    INFO_REL(0x24);
    local_14 = U32(g_track_info, 0x24);
    G32(0x6c1e30) = A(track);
    U32(track, 0x14) = local_14;
    for (i = 0; i < (int32_t)U16(g_track_info, 0x66); i++) {
        int c;
        for (c = 0; c < 3; c++) {
            p = PTR(g_track_info, 0x4c) + i * 4 + c;
            *p = colour_scale(*p);
        }
    }
    local_18 = U32(g_track_info, 0x68) + g_track_base;
    INFO_REL(0x6c);
    INFO_REL(0x70);
    INFO_REL(0xa8);
    INFO_REL(0xa4);
    INFO_REL(0x9c);
    INFO_REL(0x44);
    INFO_REL(0x74);
    INFO_REL(0xf0);
    INFO_REL(0xe4);
    INFO_REL(0xe8);
    INFO_REL(0xf8);
    INFO_REL(0x100);
    INFO_REL(0x118);
    INFO_REL(0xd4);
    INFO_REL(0x90);
    INFO_REL(0xc0);
    INFO_REL(0xec);
    INFO_REL(0x108);
    INFO_REL(0x10c);
    INFO_REL(0xd0);
    light_base = U32(g_track_info, 0xc4) + g_track_base;
    /* the models and their sub-models */
    for (j = 0; j < (int32_t)U16(g_track_info, 0x2c); j++) {
        uint8_t *slot = PTR(g_track_info, 0x18) + j * 4;
        uint8_t *m, *s;
        if (U32(slot, 0) == 0)
            continue;
        U32(slot, 0) += g_track_base;
        m = PTR(PTR(g_track_info, 0x18), j * 4);
        U32(m, 0x18) += A(m);
        U16(m, 0xa) = 0;
        U32(m, 0x40) += A(m);
        U32(m, 0xc) += A(m);
        if (U32(m, 0x44) != 0)
            U32(m, 0x44) += A(m);
        if (U16(m, 0x1c) != 0) {
            s = m + U16(m, 0x1c);
            U32(s, 0x18) += A(s);
            U32(s, 0xc) += A(s);
            if (U32(s, 0x44) != 0)
                U32(s, 0x44) += A(s);
        }
        if (U16(m, 0x1e) != 0) {
            s = m + U16(m, 0x1e);
            U32(s, 0xc) += A(s);
            U32(s, 0x18) += A(s);
            if (U32(s, 0x44) != 0)
                U32(s, 0x44) += A(s);
        }
    }
    /* the object kinds */
    objs = PTR(g_track_info, 0x14);
    for (j = 0; j < I32(g_track_info, 0x10); j++) {
        uint8_t *o;
        int k;
        U32(objs, j * 4) += g_track_base;
        o = PTR(objs, j * 4);
        U32(o, 0x50) += g_track_base;
        for (k = 0x28; k < 0x34; k += 4) {
            U32(PTR(objs, j * 4), k + 0x2c) += g_track_base;
            U32(PTR(objs, j * 4), k) += g_track_base;
            U32(PTR(objs, j * 4), k + 0x14) += g_track_base;
        }
        U32(PTR(objs, j * 4), 0x20) += g_track_base;
        U32(PTR(objs, j * 4), 0x48) += U32(g_track_info, 0x3c);
        U32(PTR(objs, j * 4), 0x38) += U32(g_track_info, 0x50) + g_track_base;
        o = PTR(objs, j * 4);
        U32(o, 0x4c) = light_base + U32(o, 0x4c) * 0x1c;
    }
    /* the 256 spot records */
    p = (uint8_t *)(uintptr_t)local_14;
    for (j = 0; j < 0x100; j++, p += 0x10)
        if (U8(p, 0) != 0xff || U8(p, 1) != 0)
            U32(p, 0xc) = U32(g_track_info, 0x6c) - 2 + U32(p, 0xc) * 2;
    INFO_REL(0x28);
    /* the track's packets, two quads per record */
    for (i = 0; i < n; i++) {
        uint8_t *b = packets + i * 0x120;
        int h;
        for (h = 0; h <= 0x84; h += 0x84) {
            uint8_t *q = b + h;
            U8(q, 7) = 0x8c;
            U8(q, 4) = 0xff;
            U32(q, 0) = (U32(q, 0) & 0xffffff) | 0xc000000;
            U8(q, 5) = 0xff;
            U8(q, 6) = 0xff;
        }
        for (h = 0x24; h <= 0x64; h += 0x20) {
            U8(b, h) = 0xff;
            U8(b, h + 1) = 0xff;
            U8(b, h + 2) = 0xff;
            U8(b, h + 0x84) = 0xff;
            U8(b, h + 0x85) = 0xff;
            U8(b, h + 0x86) = 0xff;
        }
    }
    INFO_REL(0x60);
    INFO_REL(0x84);
    /* sky_set reads one argument; the original pushes two */
    ((void(GAMEAPI *)(uint8_t *, uint32_t))(uintptr_t)0x404480)(GP(g_track_info + 0x60), U16(g_track_info, 0x64));
    GCALL(0x4044c0, track_tables_alloc)(U16(g_track_info, 0x5c), U16(g_track_info, 0x5e));
    arc2 = (uint8_t *)(uintptr_t)GCALL(0x43b660, archive_load_by_name)(GMEM(0x6c1e60));
    g_texture_count = I32(arc2, 0x10);
    G32(0x6c1e14) = U32(g_track_info, 0x110) + g_track_base;
    G32(0x6c1e40) = U32(g_track_info, 0x114) + g_track_base;
    recs = arc2 + U32(arc2, 0x28);
    a38 = arc2 + U32(arc2, 0x38);
    a3c = arc2 + U32(arc2, 0x3c);
    for (i = 0; i < n; i++) {
        uint8_t *b = packets + i * 0x120;
        const uint8_t *tex = g_textures;
        uint16_t page = (uint16_t)((uint8_t)(U8(arc2, 0x40 + i * 2) - 1) & 0x1f);
        int h;
        for (h = 0; h <= 0x84; h += 0x84) {
            uint8_t *q = b + h;
            U16(q, 0x42) = (uint16_t)(U16(tex, 6) | 0x40);
            U16(q, 0x82) = page;
            U16(q, 0x22) = (uint16_t)(U16(tex, 2) & 0x7fff);
            U8(q, 0x20) = U8(tex, 0);
            U8(q, 0x21) = U8(tex, 1);
            U8(q, 0x40) = U8(tex, 4);
            U8(q, 0x41) = U8(tex, 5);
            U8(q, 0x60) = U8(tex, 8);
            U8(q, 0x61) = U8(tex, 9);
            U8(q, 0x80) = U8(tex, 0xa);
            U8(q, 0x81) = U8(tex, 0xb);
        }
    }
    v = GCALL(0x429340, textures_unusable_thunk)() != 0 ? 2 : 0;
    if (GCALL(0x428130, track_textures_load)(U16(arc2, 0x34), U8(arc2, 0x32), U8(arc2, 0x33), (const uint8_t *)a38,
                                             A(a3c), A(arc2 + U32(arc2, 0x24)), v) == 0)
        return 0;
    GCALL(0x404550, sky_packets_build)(GMEM(arc2));
    cnt = I32(arc2, 0x14);
    U32(track, 4) = A(packets);
    U32(track, 0) = A(objs);
    U32(track, 8) = G32(0x6c1eb0);
    U8(track, 0x45) = U8(g_track_info, 4);
    U32(track, 0xc) = U32(g_track_info, 0x20);
    U32(track, 0x10) = U32(g_track_info, 0x10);
    U32(track, 0x18) = U32(g_track_info, 0x18);
    U32(track, 0x1c) = U32(g_track_info, 0x34);
    U32(track, 0x20) = U32(g_track_info, 0x38);
    U32(track, 0x24) = U32(g_track_info, 0x40);
    U32(track, 0x28) = U32(g_track_info, 0x48);
    U32(track, 0x4c) = U32(g_track_info, 0x70);
    U8(track, 0x47) = U8(g_track_info, 0x78);
    U32(track, 0x2c) = U32(g_track_info, 0x44);
    U32(track, 0x48) = U32(g_track_info, 0x74);
    U32(track, 0x30) = U32(g_track_info, 0x4c);
    U32(track, 0x50) = U32(g_track_info, 0x7c);
    U32(track, 0x54) = U32(g_track_info, 0x80);
    U32(track, 0x38) = local_18;
    U32(track, 0x58) = U32(g_track_info, 0x84);
    U32(track, 0x3c) = U32(g_track_info, 0x6c);
    U16(track, 0x5c) = U16(g_track_info, 0x66);
    U16(track, 0x5e) = U16(g_track_info, 0x7a);
    U32(track, 0x60) = local_10;
    U32(track, 0x64) = local_c;
    U32(track, 0x94) = U32(g_track_info, 0xc0);
    U8(track, 0x46) = U8(g_track_info, 0x79);
    U8(track, 0x36) = U8(g_track_info, 0x94);
    U32(track, 0x68) = U32(g_track_info, 0x90);
    U32(track, 0x78) = U32(g_track_info, 0xa8);
    U32(track, 0x6c) = U32(g_track_info, 0x9c);
    U32(track, 0x74) = U32(g_track_info, 0xa4);
    U8(track, 0x44) = U8(g_track_info, 0x95);
    U32(track, 0x70) = U32(g_track_info, 0xa0) + g_track_base;
    U32(track, 0x90) = U32(g_track_info, 0xbc) + g_track_base;
    U32(track, 0x7c) = U32(g_track_info, 0xac) + g_track_base;
    U32(track, 0x80) = U32(g_track_info, 0x28);
    U32(track, 0x84) = U32(g_track_info, 0xb0) + g_track_base;
    U32(track, 0x8c) = U32(g_track_info, 0xb8) + g_track_base;
    U32(track, 0x88) = U8(g_track_info, 0xb4);
    U32(track, 0x98) = U32(g_track_info, 0xcc) + g_track_base;
    U32(track, 0x9c) = U32(g_track_info, 0xd8) + g_track_base;
    list = (uint8_t *)(uintptr_t)GCALL(0x4445e0, kr_malloc)((uint32_t)cnt * 4);
    /* the objects */
    for (i = 0; i < cnt; i++) {
        uint8_t *rec = recs + i * 0x38;
        uint32_t kindw = U16(rec, 0);
        uint8_t *obj;
        int k;

        switch (kindw & 0x3ff) {
        case 0:
            obj = (uint8_t *)(uintptr_t)GCALL(0x460ba0, object_create)(GMEM(U32(PTR(g_track_info, 0x18), U8(rec, 3) * 4)), kindw, 0x1b4, (int32_t)A(rec + 0x1c));
            U8(obj, 0x169) = U8(rec, 0x26);
            for (k = 0; k < 3; k++)
                U32(obj, 0x180 + k * 4) = U16(rec, 0x20 + k * 2) + local_8;
            U32(obj, 0x1a8) = 3;
            U32(obj, 0x1ac) = 3;
            U16(obj, 0x1a0) = U16(rec, 0x28);
            U16(obj, 0x1a2) = U16(rec, 0x2a);
            U16(obj, 0x1a4) = U16(rec, 0x2c);
            U8(obj, 0x1a6) = U8(rec, 0x1c);
            U8(obj, 0x1a7) = U8(rec, 0x1d);
            U16(obj, 0x18c) = U16(rec, 0x1a);
            U16(obj, 0x18e) = U16(rec, 0x18);
            U8(obj, 0x199) = (uint8_t)(U16(rec, 0x10) >> 4);
            U16(obj, 0x190) = U16(rec, 6);
            U8(obj, 0x19a) = U8(rec, 0x1f);
            if (U16(rec, 6) == 0xffff) {
                U8(obj, 0x198) = 0;
            } else {
                U32(obj, 0x1b0) = 0;
                if (U8(rec, 0x16) == 0) {
                    U32(obj, 0x19c) = 0;
                } else {
                    U32(obj, 0x19c) = (uint32_t)chain;
                    U8((uint8_t *)(uintptr_t)chain, 0x198) = 0;
                    U32((uint8_t *)(uintptr_t)chain, 0x1b0) = A(obj);
                }
                chain = (int32_t)A(obj);
                U8(obj, 0x198) = U8(PTR(g_track_info, 0x80), (uint32_t)U16(rec, 6) * 0x10 + 7);
            }
            U16(obj, 0x192) = (uint16_t)i;
            U32(list, i * 4) = A(obj);
            U32(obj, 0x194) = local_4 + (uint32_t)U16(rec, 0x14) * 4;
            for (k = 0x34; k < 0x10c; k += 4)
                U32(obj, k) = 0;
            GCALL(0x480a80, quat_from_angles)((int32_t)S16(rec, 0xe), (int32_t)S16(rec, 0x10), (int32_t)S16(rec, 0x12), GMEM(obj + 0x58));
            GCALL(0x480780, quat_to_matrix)(GMEM(obj + 0x58), GMEM(obj + 0xc4));
            I32(obj, 0x34) = (int32_t)((uint32_t)(int32_t)S16(rec, 8) << 16);
            I32(obj, 0x38) = (int32_t)((uint32_t)(int32_t)S16(rec, 0xa) << 16);
            I32(obj, 0x3c) = (int32_t)((uint32_t)(int32_t)S16(rec, 0xc) << 16);
            GCALL(0x442680, object_lights_setup)(GMEM(obj), GMEM(rec), GMEM(&lights));
            GCALL(0x460fb0, object_grid_link)(GMEM(obj));
            if (U8(obj, 0x19a) == 2)
                GCALL(0x405070, track_blast_register)(GMEM(obj), GMEM(rec));
            GCALL(0x478370, object_lit_faces_register)(GMEM(obj));
            if (U8(obj, 0x19a) == 0)
                GCALL(0x403a20, track_static_add)(A(obj));
            break;
        case 2:
            GCALL(0x47dbc0, sprite_spawn_marker)(U16(rec, 8), U16(rec, 0xa), U16(rec, 0xc), U16(rec, 6), U8(rec, 4),
                                                 U8(rec, 5));
            break;
        case 0xd:
            GCALL(0x47dc30, light_sprite_spawn)(U16(rec, 8), U16(rec, 0xa), U16(rec, 0xc), U16(rec, 6), U8(rec, 4),
                                                U8(rec, 0x1c), (kindw & 0xff00) | U8(rec, 0x1d), U8(rec, 0x1e),
                                                (int32_t)lights);
            lights++;
            break;
        case 0x18:
            v = A(GCALL(0x41aa70, track_object_spawn)(GMEM(rec)));
            GCALL(0x442680, object_lights_setup)(GMEM(v), GMEM(rec), GMEM(&lights));
            GCALL(0x4039d0, object_triggers_register)(GMEM(rec), v);
            break;
        }
    }
    GCALL(0x478f80, track_models_mark_lit)(cnt, GMEM(arc2 + U32(arc2, 0x28)));
    for (j = 0; j < (int32_t)U16(g_track_info, 0x96); j++) {
        uint8_t *slot = PTR(track, 0x7c) + j * 4;
        v = U32(slot, 0);
        if (v < 0x10000)
            U32(slot, 0) = U32(list, v * 4);
    }
    GCALL(0x444570, kr_free)(A(list));
    GCALL(0x444570, kr_free)(A(arc2));
    G32(0x6c1e2c) = U16(g_track_info, 0xc8);
    G32(0x6c1e3c) = GCALL(0x4445e0, kr_malloc)((uint32_t)U16(g_track_info, 0xc8) << 5);
    {
        uint8_t *info = g_track_info;
        uint32_t lit = 0;
        p = PTR(info, 0xf0);
        G32(0x4b65f4) = 0;
        for (j = 0; j < (int32_t)U8(info, 0xb6); j++, p += 0x10) {
            uint8_t *e;
            if ((int32_t)lit >= 0x28 || U8(p, 6) == 1)
                continue;
            e = (uint8_t *)0x6cfbe0 + lit * 0x38;
            U32(e, 0) = U8(p, 6);
            I32(e, 4) = (int32_t)((uint32_t)(int32_t)S16(p, 0) << 16);
            I32(e, 8) = (int32_t)((uint32_t)(int32_t)S16(p, 2) << 16);
            I32(e, 0xc) = (int32_t)((uint32_t)(int32_t)S16(p, 4) << 16);
            I32(e, 0x10) = (int32_t)((uint32_t)(int32_t)S16(p, 8) << 16);
            I32(e, 0x14) = (int32_t)((uint32_t)(int32_t)S16(p, 0xa) << 16);
            I32(e, 0x18) = (int32_t)((uint32_t)(int32_t)S16(p, 0xc) << 16);
            lit++;
            G32(0x4b65f4) = lit;
        }
    }
    GCALL(0x40b5b0, track_tv_spots)();
    return 1;
}
