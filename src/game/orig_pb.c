/* Group PB: the original's 0x420be0-0x422f50 still-reachable functions
 * (two source files: the end of the Direct3D back end, and the car drawing).
 *
 *  - the Direct3D back end's packet drawer 0x420be0 (one packet: render
 *    state 0x420a30, vertices straight to the batch 0x420a90 or through the
 *    clipper 0x4180c0 / 0x418170 and 0x417af0 / 0x417ce0 / 0x418290, batch
 *    flushes 0x420720 / 0x4206a0 / 0x420bb0), its ordering table walk
 *    DrawOTag 0x421860, the lost-surface restore 0x4218e0 and the strip
 *    vertex repeat 0x421800;
 *  - the cars: per-frame drawing 0x421ef0 (bodies, wheels, reflections,
 *    shadows and effects), the packet ring 0x422080 / 0x422100, the ride
 *    offset 0x422140, the model drawer with its level of detail 0x4226d0
 *    (transform, face culling, packets into buckets linked into the
 *    ordering table), the water reflections 0x422c80..0x422eb0 and the
 *    shadow 0x422f50.
 *
 * Calls outside the group go by address; COM calls are the typed methods
 * of com.h. The back end's register-argument helpers (0x420a30 ebx;
 * 0x420a90 eax / edx / ebx; 0x4180c0 eax / ecx / edx / ebx) are called with
 * inline asm. The GTE helpers behind the pointers 0x6ccd78 (transform) and
 * 0x6ccd80 (projection) pass a value on the x87 stack from one to the other;
 * nothing between those calls here uses the FPU. Float code is the
 * original's x87 instruction sequence in inline asm (memory operands), so
 * results match under any precision control word. */
#include "../runtime.h"
#include "com.h"
#include "physics.h"
#include "gamefn.h"

#define A(p) ((uint32_t)(uintptr_t)(p))
#define U8(a, off) (*(uint8_t *)(uintptr_t)((uint32_t)(a) + (uint32_t)(off)))
#define S16(a, off) (*(int16_t *)(uintptr_t)((uint32_t)(a) + (uint32_t)(off)))
#define U16(a, off) (*(uint16_t *)(uintptr_t)((uint32_t)(a) + (uint32_t)(off)))
#define U32(a, off) (*(uint32_t *)(uintptr_t)((uint32_t)(a) + (uint32_t)(off)))
#define I32(a, off) (*(int32_t *)(uintptr_t)((uint32_t)(a) + (uint32_t)(off)))
#define G8(a) U8(a, 0)
#define G32(a) U32(a, 0)
#define GI32(a) I32(a, 0)

#define GM(a) GMEM((uint32_t)(a))

/* The GTE helpers through their pointers: transform V0 (0x6ccc64 / 0x6ccc68)
 * into a 0x18-byte record (leaves a value on the x87 stack), project the
 * last record (takes it), and the lighting / normal step. */
#if defined(__i386__)
/* typed as returning an int so the transform's st(0) stays for the projection */
#define GTE_TRANSFORM(rec) ((int32_t(GAMEAPI *)(uint32_t))(uintptr_t)G32(0x6ccd78))((uint32_t)(rec))
#define GTE_PROJECT() ((int32_t(GAMEAPI *)(void))(uintptr_t)G32(0x6ccd80))()
#else
/* x86-64: the transform returns its st(0) as a float and the projection
 * takes it (subunit_transform_c / wide_project_c); kept here in between */
static float pb_gte_h;
#define GTE_TRANSFORM(rec) (pb_gte_h = ((float(GAMEAPI *)(uint8_t *))(uintptr_t)G32(0x6ccd78))((uint8_t *)GM(rec)))
#define GTE_PROJECT() ((void(GAMEAPI *)(float))(uintptr_t)G32(0x6ccd80))(pb_gte_h)
#endif
#define GTE_LIGHT() ((void(GAMEAPI *)(void))(uintptr_t)G32(0x6cce3c))()

#define g_race G32(0x4b1960)
#define CAR_RECORD(i) (0x6bfb80u + (uint32_t)(i) * 0x274u) /* the race's car records: +0 car, +0x260 reflection, +0x264 */
#define VIEW0 0x6cd4a0u
#define g_view G32(0x6ce60c)
#define g_list_count G32(0x6cd258)

#define X87_CLOBBER "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* ---- x87: the original's instruction sequences ---- */

/* fld a; fsub b; fstp */
static inline uint32_t pb_fsub(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fadd b; fstp */
static inline uint32_t pb_fadd(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fmul b; fstp */
static inline uint32_t pb_fmul(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fchs; fstp */
static inline uint32_t pb_fneg(uint32_t a)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfchs\n\tfstps %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld a; fmul k; fcomp b; fnstsw ax; test ah, 0x41: a * k > b (not below,
 * equal or unordered). */
static inline int pb_mul_greater(uint32_t a, uint32_t k, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfcomps %3\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(k), "m"(b) : "st", "cc");
    return (sw & 0x4100) == 0;
}

/* fld a; fistp dword (rounded by the control word) */
static inline uint32_t pb_fist(uint32_t a)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fild dword; fstp */
static inline uint32_t pb_fild(uint32_t a)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* The facing value of three vertex records (0x4278f0, x87), stored as a
 * single (fstp dword). */
static uint32_t pb_facing(uint32_t a, uint32_t b, uint32_t c)
{
    float f = ((float(GAMEAPI *)(const uint8_t *, const uint8_t *, const uint8_t *))(uintptr_t)0x4278f0)(GM(a), GM(b),
                                                                                                  GM(c));
    uint32_t bits;

    memcpy(&bits, &f, 4);
    return bits;
}

/* rep movsd */
static inline void pb_movsd(uint32_t dst, uint32_t src, uint32_t n)
{
#if defined(__i386__)
    __asm__ volatile("rep movsl" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
#else
    for (; n; n--, dst += 4, src += 4)
        G32(dst) = G32(src);
#endif
}

/* abs by cdq / xor / sub (INT_MIN stays negative) */
static inline int32_t pb_abs(int32_t v)
{
    uint32_t s = (uint32_t)(v >> 31);
    return (int32_t)(((uint32_t)v ^ s) - s);
}

/* The ordering table slot for a depth (0x421e60 and 0x422f50): halved
 * above 0x100 and 0x180, 1..0x1ff. */
static int32_t pb_ot_clamp(int32_t v)
{
    if (v > 0x100) {
        v = v / 2 + 0x80;
        if (v > 0x180) {
            v = v / 2 + 0xc0;
            if (v > 0x1ff)
                return 0x1ff;
        }
    }
    if (v < 1)
        v = 1;
    return v;
}

/* ---- the back end's register-argument helpers ---- */

/* 0x420a30: render state and texture page from ebx (bits 0..9 used). */
static void pb_state_select(uint32_t ebx)
{
#if defined(__i386__)
    uint32_t fn = 0x420a30, c, d;
    __asm__ volatile("call *%0" : "+a"(fn), "=c"(c), "=d"(d) : "b"(ebx) : "memory", "cc", X87_CLOBBER);
#else
    render_state_select_c(ebx);
#endif
}

/* 0x420a90: one vertex into the batch: eax colour, edx vertex record (+4 z,
 * +8 x, +0xc y), bl / bh texture indices. */
static void pb_batch_vertex(uint32_t colour, uint32_t vertex, uint32_t uv)
{
#if defined(__i386__)
    uint32_t a = colour, d = vertex, c;
    __asm__ volatile("movl $0x420a90, %%ecx\n\tcall *%%ecx" : "+a"(a), "+d"(d), "=&c"(c) : "b"(uv)
                     : "memory", "cc", X87_CLOBBER);
#else
    vertex_emit_indexed_c(colour, uv, (const uint8_t *)GM(vertex));
#endif
}

/* 0x4180c0: one vertex into the clipper: eax colour, ecx / edx texture
 * indices, ebx six dwords. */
static void pb_clip_vertex(uint32_t colour, uint32_t u, uint32_t v, uint32_t src)
{
#if defined(__i386__)
    uint32_t a = colour, c = u, d = v;
    __asm__ volatile("call *%[fn]" : "+a"(a), "+c"(c), "+d"(d) : "b"(src), [fn] "S"(0x4180c0u)
                     : "memory", "cc", X87_CLOBBER);
#else
    clip_vertex_add_c(colour, u, v, (const uint32_t *)GM(src));
#endif
}

#define clip_vertex_2d(xy, uv, rgba) GCALL(0x418170, clip_vertex_add_2d)(xy, uv, rgba)
#define clip_restart() GCALL(0x418260, clip_vertices_restart)()
#define clip_a(flags) GCALL(0x417af0, clip_polygon_a)(flags)
#define clip_b(flags) GCALL(0x417ce0, clip_polygon_b)(flags)
#define clip_2d() GCALL(0x418290, clip_polygon_2d)()
#define batch_list(list) GCALL(0x420bb0, vertex_list_draw_fan)((const int32_t *)GM(list))
#define batch_flush_quads() GCALL(0x420720, vertices_draw_quads)()
#define batch_flush_triangles() GCALL(0x4206a0, vertices_draw_list)()

/* The line colour: (r + g wrapped to a byte, + b) * 10 / 16 of the colour
 * halved. */
static uint8_t pb_line_grey(uint32_t c)
{
    uint32_t t = (c & 0xfefefe) >> 1;
    uint32_t s = (uint8_t)((t & 0xff) + ((t >> 8) & 0xff));

    s += t >> 16;
    return (uint8_t)((s * 10) >> 4);
}

/* ===== 0x420be0: draw one packet with the Direct3D back end. The code
 * byte +7 picks the kind; +7 is put back at the end (some kinds change it
 * meanwhile). Vertex records: +0 clip code, +4 z, +8 x, +0xc y. ===== */
void GAMEAPI d3d_packet_draw(uint32_t p)
{
    uint8_t saved = U8(p, 7);
    uint32_t flags, sel, rec[4], half, dx, dy, ox, oy, col;

    switch (saved >> 2) {
    case 0x23: /* 0x8c: textured gouraud quad */
        flags = U32(p, 8) | U32(p, 0x28) | U32(p, 0x48) | U32(p, 0x68);
        sel = (U32(p, 0x42) & 0xffff00ffu) | (uint32_t)U8(p, 7) << 8;
        U8(p, 7) = U8(p, 0x22);
        pb_state_select(sel);
        if (!flags) {
            pb_batch_vertex(U32(p, 4), p + 8, U32(p, 0x20));
            pb_batch_vertex(U32(p, 0x24), p + 0x28, U32(p, 0x40));
            pb_batch_vertex(U32(p, 0x44), p + 0x48, U32(p, 0x60));
            pb_batch_vertex(U32(p, 0x64), p + 0x68, U32(p, 0x80));
            batch_flush_quads();
            break;
        }
        flags |= 0x100;
        g_list_count = 0;
        pb_clip_vertex(U32(p, 4), U8(p, 0x20), U8(p, 0x21), p + 8);
        pb_clip_vertex(U32(p, 0x24), U8(p, 0x40), U8(p, 0x41), p + 0x28);
        pb_clip_vertex(U32(p, 0x44), U8(p, 0x60), U8(p, 0x61), p + 0x48);
        batch_list(clip_a(flags));
        clip_restart();
        pb_clip_vertex(U32(p, 0x64), U8(p, 0x80), U8(p, 0x81), p + 0x68);
        g_list_count += 1;
        batch_list(clip_a(flags));
        break;
    case 0x26: /* 0x98: textured flat quad */
        flags = U32(p, 8) | U32(p, 0x24) | U32(p, 0x40) | U32(p, 0x5c);
        sel = (U32(p, 0x3e) & 0xffff00ffu) | (uint32_t)U8(p, 7) << 8;
        U8(p, 7) = U8(p, 0x22);
        pb_state_select(sel);
        if (!flags) {
            pb_batch_vertex(U32(p, 4), p + 8, U32(p, 0x20));
            pb_batch_vertex(U32(p, 4), p + 0x24, U32(p, 0x3c));
            pb_batch_vertex(U32(p, 4), p + 0x40, U32(p, 0x58));
            pb_batch_vertex(U32(p, 4), p + 0x5c, U32(p, 0x74));
            batch_flush_quads();
            break;
        }
        flags |= 0x100;
        g_list_count = 0;
        pb_clip_vertex(U32(p, 4), U8(p, 0x20), U8(p, 0x21), p + 8);
        pb_clip_vertex(U32(p, 4), U8(p, 0x3c), U8(p, 0x3d), p + 0x24);
        pb_clip_vertex(U32(p, 4), U8(p, 0x58), U8(p, 0x59), p + 0x40);
        batch_list(clip_b(flags));
        clip_restart();
        pb_clip_vertex(U32(p, 4), U8(p, 0x74), U8(p, 0x75), p + 0x5c);
        g_list_count += 1;
        batch_list(clip_b(flags));
        break;
    case 0x2d: /* 0xb4: texture page only. The original's ebx keeps its
                * caller's upper half (0x420a30 uses bits 0..9): 0 here. */
        pb_state_select(U8(p, 4) | (uint32_t)U8(p, 7) << 8);
        break;
    case 0x11: /* 0x44: sprite with its own texture page */
        pb_state_select((U32(p, 0xe) & 0xffff00ffu) | (uint32_t)U8(p, 7) << 8);
        /* fall through */
    case 0x19: { /* 0x64: sprite: x, y +8 / +0xa, u, v +0xc / +0xd, w, h +0x10 / +0x12 */
        int32_t w = S16(p, 0x10), h = S16(p, 0x12), x, y;
        uint32_t xy0, xy1, xy2, xy3, u1, v1;

        g_list_count = 0;
        x = S16(p, 8);
        y = S16(p, 0xa);
        xy0 = ((uint32_t)x & 0xffff) | (uint32_t)y << 16;
        xy1 = ((uint32_t)(x + w) & 0xffff) | (uint32_t)y << 16;
        xy2 = ((uint32_t)(x + w) & 0xffff) | (uint32_t)(y + h) << 16;
        xy3 = ((uint32_t)x & 0xffff) | (uint32_t)(y + h) << 16;
        u1 = (uint32_t)U8(p, 0xc) + (uint32_t)w;
        if (u1 >= 0xff)
            u1 = 0xff;
        v1 = (uint32_t)U8(p, 0xd) + (uint32_t)h;
        if (v1 >= 0xff)
            v1 = 0xff;
        v1 <<= 8;
        clip_vertex_2d(xy0, U8(p, 0xc) | (uint32_t)U8(p, 0xd) << 8, U32(p, 4) | 0xff000000u);
        clip_vertex_2d(xy1, u1 | (uint32_t)U8(p, 0xd) << 8, U32(p, 4) | 0xff000000u);
        clip_vertex_2d(xy2, u1 | v1, U32(p, 4) | 0xff000000u);
        clip_vertex_2d(xy3, U8(p, 0xc) | v1, U32(p, 4) | 0xff000000u);
        batch_list(clip_2d());
        break;
    }
    case 0x2b: /* 0xac: line, drawn as a triangle as wide as *0x6cd41c */
        pb_state_select(0x3f | (uint32_t)U8(p, 7) << 8);
        U8(p, 7) = pb_line_grey(U32(p, 4));
        U8(p, 0x23) = pb_line_grey(U32(p, 0x20));
        if (U32(p, 8) | U32(p, 0x24))
            break;
        half = G32(0x6cd41c);
        dx = pb_fsub(U32(p, 0x2c), U32(p, 0x10));
        dy = pb_fsub(U32(p, 0x30), U32(p, 0x14));
        if (dx & 0x80000000u) {
            dx = pb_fneg(dx);
            if (dy & 0x80000000u) {
                dy = pb_fneg(dy);
                ox = half;
                oy = pb_fneg(half);
            } else {
                ox = half;
                oy = half;
            }
        } else if (dy & 0x80000000u) {
            dy = pb_fneg(dy);
            ox = pb_fneg(half);
            oy = pb_fneg(half);
        } else {
            ox = pb_fneg(half);
            oy = half;
        }
        if (pb_mul_greater(dy, G32(0x4a3340), dx))
            ox = pb_fmul(ox, G32(0x4a333c));
        if (pb_mul_greater(dx, G32(0x4a3340), dy))
            oy = pb_fmul(oy, G32(0x4a333c));
        pb_batch_vertex(U32(p, 4), p + 8, 0);
        rec[0] = 0; /* not read */
        rec[2] = pb_fadd(U32(p, 0x2c), ox);
        rec[3] = pb_fadd(U32(p, 0x30), oy);
        rec[1] = U32(p, 0x28);
        pb_batch_vertex(U32(p, 0x20), A(rec), 0);
        rec[2] = pb_fsub(U32(p, 0x2c), ox);
        rec[3] = pb_fsub(U32(p, 0x30), oy);
        pb_batch_vertex(U32(p, 0x20), A(rec), 0);
        batch_flush_triangles();
        break;
    case 0x2a: /* 0xa8: untextured quad, one colour */
        pb_state_select(0x3f | (uint32_t)U8(p, 7) << 8);
        flags = U32(p, 8) | U32(p, 0x20) | U32(p, 0x38) | U32(p, 0x50);
        col = U32(p, 4);
        U8(p, 7) = (uint8_t)(col | col >> 8 | col >> 16);
        if (!flags) {
            pb_batch_vertex(U32(p, 4), p + 8, 0);
            pb_batch_vertex(U32(p, 4), p + 0x20, 0);
            pb_batch_vertex(U32(p, 4), p + 0x38, 0);
            pb_batch_vertex(U32(p, 4), p + 0x50, 0);
            batch_flush_quads();
            break;
        }
        g_list_count = 0;
        pb_clip_vertex(U32(p, 4), 0, 0, p + 8);
        pb_clip_vertex(U32(p, 4), 0, 0, p + 0x20);
        pb_clip_vertex(U32(p, 4), 0, 0, p + 0x38);
        batch_list(clip_a(flags | 0x100));
        clip_restart();
        pb_clip_vertex(U32(p, 4), 0, 0, p + 0x50);
        g_list_count += 1;
        batch_list(clip_a(flags | 0x100));
        break;
    case 0x27: /* 0x9c: textured gouraud triangle */
        sel = (U32(p, 0x42) & 0xffff00ffu) | (uint32_t)U8(p, 7) << 8;
        pb_state_select(sel);
        flags = U32(p, 8) | U32(p, 0x28) | U32(p, 0x48);
        U8(p, 7) = U8(p, 0x22);
        if (!flags) {
            pb_batch_vertex(U32(p, 4), p + 8, U32(p, 0x20));
            pb_batch_vertex(U32(p, 0x24), p + 0x28, U32(p, 0x40));
            pb_batch_vertex(U32(p, 0x44), p + 0x48, U32(p, 0x60));
            batch_flush_triangles();
            break;
        }
        g_list_count = 0;
        pb_clip_vertex(U32(p, 4), U8(p, 0x20), U8(p, 0x21), p + 8);
        pb_clip_vertex(U32(p, 0x24), U8(p, 0x40), U8(p, 0x41), p + 0x28);
        pb_clip_vertex(U32(p, 0x44), U8(p, 0x60), U8(p, 0x61), p + 0x48);
        batch_list(clip_a(flags));
        break;
    case 0x25: /* 0x94: textured flat triangle */
        sel = (U32(p, 0x3e) & 0xffff00ffu) | (uint32_t)U8(p, 7) << 8;
        pb_state_select(sel);
        flags = U32(p, 8) | U32(p, 0x24) | U32(p, 0x40);
        U8(p, 7) = 0xff;
        if (!flags) {
            pb_batch_vertex(U32(p, 4), p + 8, U32(p, 0x20));
            pb_batch_vertex(U32(p, 4), p + 0x24, U32(p, 0x3c));
            pb_batch_vertex(U32(p, 4), p + 0x40, U32(p, 0x58));
            batch_flush_triangles();
            break;
        }
        g_list_count = 0;
        pb_clip_vertex(U32(p, 4), U8(p, 0x20), U8(p, 0x21), p + 8);
        pb_clip_vertex(U32(p, 4), U8(p, 0x3c), U8(p, 0x3d), p + 0x24);
        pb_clip_vertex(U32(p, 4), U8(p, 0x58), U8(p, 0x59), p + 0x40);
        batch_list(clip_b(flags));
        break;
    case 0x28: /* 0xa0: fog: +4 = 2 off; else 0x6cce4c = +4, fog colour alpha */
        if (U8(p, 4) == 2) {
            G32(0x6cce4c) = 0;
            G32(0x6ccdb0) = 0;
        } else {
            G32(0x6cce4c) = U8(p, 4);
            G32(0x6ccdb0) = G32(0x6cce4c) ? 0 : 0xff000000u;
        }
        break;
    case 0x2c: /* 0xb0: texture filtering on / nearest */
        if (U8(p, 4))
            GCALL(0x41dd90, d3d_filter_set)();
        else
            GCALL(0x41ddd0, d3d_filter_nearest)();
        break;
    case 0x0b: /* 0x2c: 2D textured quad: xy / uv pairs +8 / +0xc, +0x10 / +0x14, +0x20 / +0x24, +0x18 / +0x1c */
        pb_state_select((U32(p, 0x16) & 0xffff00ffu) | (uint32_t)U8(p, 7) << 8);
        g_list_count = 0;
        clip_vertex_2d(U32(p, 8), U32(p, 0xc), U32(p, 4) | 0xff000000u);
        clip_vertex_2d(U32(p, 0x10), U32(p, 0x14), U32(p, 4) | 0xff000000u);
        clip_vertex_2d(U32(p, 0x20), U32(p, 0x24), U32(p, 4) | 0xff000000u);
        clip_vertex_2d(U32(p, 0x18), U32(p, 0x1c), U32(p, 4) | 0xff000000u);
        batch_list(clip_2d());
        break;
    case 0x0c: /* 0x30: 2D gouraud triangle: colour / xy pairs +4 / +8, +0xc / +0x10, +0x14 / +0x18 */
        sel = 0x3f | (uint32_t)U8(p, 7) << 8;
        U8(p, 7) = 0xff;
        pb_state_select(sel);
        g_list_count = 0;
        clip_vertex_2d(U32(p, 8), 0, U32(p, 4));
        clip_vertex_2d(U32(p, 0x10), 0, U32(p, 0xc));
        clip_vertex_2d(U32(p, 0x18), 0, U32(p, 0x14));
        batch_list(clip_2d());
        break;
    default:
        break;
    }
    U8(p, 7) = saved;
}

/* ===== 0x421800: repeat strip vertices in the batch: the batch slot n gets
 * slot n-2 and slot n+2 gets slot n-1 (0x20 bytes each, slot offsets
 * 0x4a807c), then the count +1. ===== */
void GAMEAPI d3d_vertex_repeat(void)
{
    uint32_t n = G32(0x4bddd8);
    uint32_t base = G32(0x4bcd80);

    pb_movsd(base + G32(0x4a807c + n * 4), base + G32(0x4a807c + (n - 2) * 4), 8);
    base = G32(0x4bcd80);
    pb_movsd(base + G32(0x4a807c + (n + 2) * 4), base + G32(0x4a807c + (n - 1) * 4), 8);
    G32(0x4bddd8) += 1;
}

/* ===== 0x4218e0: restore lost surfaces (IsLost / Restore): the back
 * buffer, the z-buffer, then each texture slot's surface (+8; when that
 * fails, or when a slot has none and the last restore failed, 0x420150
 * reloads the slot). Returns 1. The original starts the "failed" flag from
 * the caller's ecx when there is no back buffer (leftover register): 0 here. ===== */
int32_t GAMEAPI d3d_surfaces_restore(void)
{
    uint32_t failed = 0, i, slot;
    int32_t hr;

    if (G32(0x4d5f4c)) {
        failed = 0;
        if ((uint32_t)IDirectDrawSurface_IsLost(G32(0x4d5f4c)) == 0x887601c2u) {
            hr = IDirectDrawSurface_Restore(G32(0x4d5f4c));
            GI32(0x4d6f18) = hr;
            if (hr)
                failed = 1;
        }
    }
    if (G32(0x4d5f98) && G32(0x4d5fa0)) {
        failed = 0;
        if ((uint32_t)IDirectDrawSurface_IsLost(G32(0x4d5fa0)) == 0x887601c2u) {
            hr = IDirectDrawSurface_Restore(G32(0x4d5fa0));
            GI32(0x4d6f18) = hr;
            if (hr)
                failed = 1;
        }
    }
    for (i = 0; i < 64; i++) {
        slot = 0x4ba6fc + i * 0x24;
        if (!U32(slot, 4))
            continue;
        if (U32(slot, 8)) {
            failed = 0;
            if ((uint32_t)IDirectDrawSurface_IsLost(U32(slot, 8)) != 0x887601c2u)
                continue;
            hr = IDirectDrawSurface_Restore(U32(slot, 8));
            GI32(0x4d6f18) = hr;
            if (!hr)
                continue;
            failed = 1;
        } else if (!failed) {
            continue;
        }
        GCALL(0x420150, texture_reload)(i);
    }
    return 1;
}

/* ===== 0x421860: DrawOTag(ot): restore surfaces, reset the fog and blend
 * state, then draw every packet of the table (a link with bit 31 set is a
 * tag only; -1 or a null link ends it). ===== */
void GAMEAPI d3d_draw_otag(uint32_t ot)
{
    uint32_t e;

    if (!d3d_surfaces_restore())
        return;
    G32(0x6ccdb8) = 0x3eaaaaab;
    G32(0x6ccdb4) = 0x3f2aaaaa;
    G32(0x6cce4c) = 0;
    G32(0x6ccdb0) = 0xff000000u;
    pb_state_select(0x3f);
    G32(0x6cce4c) = 0;
    e = U32(ot, 0);
    while (e != 0xffffffffu) {
        if (!(e & 0x80000000u))
            d3d_packet_draw(e);
        e &= 0x7fffffff;
        if (!e)
            return;
        e = U32(e, 0);
    }
}

/* ===== 0x421e60: link the car's current packet (+0x14b0, its link at
 * +0x34) into the view's ordering table (0x6cd510) by depth: ((z - 2 *
 * model +0x32) >> 5) + bias - 2, clamped; not when 0x6cce84. ===== */
void GAMEAPI car_ot_insert(uint32_t car, int32_t z, int32_t bias)
{
    uint32_t k = U16(U32(car, 0x10), 0x32), pk, slot;
    int32_t v = (int32_t)((uint32_t)z - (k << 1)) >> 5;

    v = pb_ot_clamp((int32_t)((uint32_t)v + (uint32_t)bias - 2));
    pk = U32(car, 0x14b0);
    if (!G32(0x6cce84)) {
        slot = G32(0x6cd510 + (uint32_t)v * 4) & 0x7fffffff;
        U32(pk, 0x34) = U32(slot, 0);
        U32(slot, 0) = pk | 0x80000000u;
    }
}

/* ===== 0x422100: the car's next packet block in its ring (+0x14b0,
 * 0x38 apart, +0x1408..+0x1478), cleared as a 14-entry tag list. ===== */
void GAMEAPI car_packets_next(uint32_t car)
{
    uint32_t p = U32(car, 0x14b0) + 0x38;

    U32(car, 0x14b0) = p;
    if (p > car + 0x1478)
        U32(car, 0x14b0) = car + 0x1408;
    GCALL(0x427a40, ot_clear_forward)((uint32_t *)GM(U32(car, 0x14b0)), 0xe);
}

/* ===== 0x422080: the next packet block of every car, and of every
 * reflection car (records +0x260) when the race has reflections (+0x54e). ===== */
void GAMEAPI cars_packets_next(void)
{
    uint32_t race = g_race, i;

    for (i = 0; i < U8(race, 0x54d); i++) {
        car_packets_next(G32(CAR_RECORD(i)));
        race = g_race;
    }
    if (U8(race, 0x54e)) {
        for (i = 0; i < U8(race, 0x54d); i++) {
            car_packets_next(U32(CAR_RECORD(i), 0x260));
            race = g_race;
        }
    }
}

/* ===== 0x422140: the body's ride offset +0x14c8 eases towards 24 (or -24
 * upside down, +0x63f) while a wheel touches (+0x324 bit 0, four wheels
 * 0xc4 apart), else 0: halved when more than 24 away, then one step; the
 * offset vector +0x14bc = matrix column (+0xc6, +0xcc, +0xd2) * offset * 8
 * unless 0x4d1198. ===== */
void GAMEAPI car_ride_offset_update(uint32_t car)
{
    int32_t target = 0, cur, d, v;
    uint32_t i;

    for (i = 0; i < 4; i++) {
        if (U8(car, 0x324 + i * 0xc4) & 1) {
            target = U8(car, 0x63f) ? -0x18 : 0x18;
            break;
        }
    }
    cur = I32(car, 0x14c8);
    d = (int32_t)((uint32_t)target - (uint32_t)cur);
    if (pb_abs(d) > 0x18)
        I32(car, 0x14c8) = (int32_t)((uint32_t)(d / 2) + (uint32_t)cur);
    v = I32(car, 0x14c8);
    if (v < target)
        I32(car, 0x14c8) = v + 1;
    v = I32(car, 0x14c8);
    if (v > target)
        I32(car, 0x14c8) = v - 1;
    v = I32(car, 0x14c8);
    if (!G32(0x4d1198)) {
        U32(car, 0x14bc) = ((uint32_t)(int32_t)S16(car, 0xc6) * (uint32_t)v) << 3;
        U32(car, 0x14c0) = ((uint32_t)(int32_t)S16(car, 0xcc) * (uint32_t)v) << 3;
        U32(car, 0x14c4) = ((uint32_t)(int32_t)S16(car, 0xd2) * (uint32_t)v) << 3;
    }
}

/* ===== 0x422650: the depth of a body (0x45a2a0) with its position
 * (+0x34) moved by an offset vector for the call. ===== */
int32_t GAMEAPI car_depth_offset(uint32_t car, uint32_t off)
{
    uint32_t oz = U32(off, 8), oy = U32(off, 4);
    uint32_t z = U32(car, 0x3c), x = U32(car, 0x34), y = U32(car, 0x38);
    int32_t r;

    U32(car, 0x38) = oy + y;
    U32(car, 0x34) = x + U32(off, 0);
    U32(car, 0x3c) = oz + z;
    r = GCALL(0x45a2a0, model_view_depth)((uint8_t *)GM(car));
    U32(car, 0x34) = x;
    U32(car, 0x38) = y;
    U32(car, 0x3c) = z;
    return r;
}

/* Link a bucket's packets (first .. last) at the head of an ordering table
 * slot: the view's table by the last packet's +0x42 when 0x6cce84, else `ot`. */
static void pb_bucket_link(uint32_t first, uint32_t last, uint32_t ot)
{
    uint32_t slot;

    if (!last)
        return;
    if (G32(0x6cce84))
        slot = G32(0x6cd518 + (U8(last, 0x42) & 0x3f) * 4) & 0x7fffffff;
    else
        slot = ot & 0x7fffffff;
    U32(last, 0) = U32(slot, 0);
    U32(slot, 0) = first;
}

/* ===== 0x4226d0: draw a model (car body, wheel, front-end model) at a level
 * of detail by depth (*0x6cd404 * 1500 / 256 and * 3750 / 256): its
 * vertices transformed (and lit through the normals +0x48), each face
 * (a | b << 8 | c << 16 | d << 24 vertex indices from 1, d 0 for a
 * triangle; second dword bit 31 -> code bit 1) culled by facing and clip
 * codes and copied into its 0x84-byte packet (code 0x8c quad / 0x9c
 * triangle), packets chained into buckets (each bucket's first packet
 * +0x82: 8 per face plus flags 1 path bit, 2 bucket end, 4 last), the
 * buckets linked into the table in the order the header's nibble lists
 * (+0x18 and +0x44, by path) give. Kind 3 (+0 & 0x1ff) near the camera
 * also records each drawn face's packet number from +0x10b4 (to +0x10fc).
 *
 * The original's vertex / depth arrays hold 179 / 180 entries and its
 * bucket table 15 (more would overrun them); index 0 and unwritten
 * buckets read leftover stack there: zero here. ===== */
void GAMEAPI model_draw_lod(uint32_t m, uint32_t ot, int32_t dist)
{
    uint32_t vtx[1 + 256][6];
    uint32_t dep[1 + 256];
    uint32_t tbl[256][2]; /* bucket k (from 1): first, last packet; [0]: the last face's d, c (sic) */
    uint32_t mirror = G32(0x6cce94);
    int32_t n = GI32(0x6cd404);
    int32_t lod_near = (int32_t)((uint32_t)n * 1500u) >> 8;
    int32_t lod_far = (int32_t)((uint32_t)n * 3750u) >> 8;
    uint32_t cursor = 0xffffffffu, limit = 0xffffffffu, first = 0xffffffffu;
    uint32_t last = 0, path = 0, record = 0, bucket = 1;
    uint32_t hdr, faces, pk, src, pts, count, k, order, facing = 0, *dp;
    int32_t left;

    memset(vtx, 0, sizeof vtx);
    memset(dep, 0, sizeof dep);
    memset(tbl, 0, sizeof tbl);
    if ((U32(m, 0) & 0x1ff) == 3) {
        record = 1;
        if (dist < lod_near) {
            cursor = m + 0x10b4;
            limit = cursor + 0x48;
        } else {
            record = 0;
        }
    }
    hdr = U32(m, 0x10);
    src = U32(m, 0x24);
    if (dist > lod_near) {
        if (dist > lod_far) {
            faces = U32(m, 0x30);
            hdr += U16(hdr, 0x1e);
            count = U8(m, 0x163);
            pk = g_view == VIEW0 ? U32(m, 0x14c) : U32(m, 0x150);
        } else {
            faces = U32(m, 0x2c);
            count = U8(m, 0x162);
            hdr += U16(hdr, 0x1c);
            pk = g_view == VIEW0 ? U32(m, 0x1c) : U32(m, 0x20);
        }
    } else {
        pk = g_view == VIEW0 ? U32(m, 0x14) : U32(m, 0x18);
        faces = U32(m, 0x28);
        count = U8(m, 0x10d);
    }

    /* count + 1 vertices; the normals +0x48 (int16 x, y, z, negated) lit
     * between them, each one's depth (0x6cccbc) recorded */
    pts = U32(hdr, 0x48);
    G32(0x6ccc64) = U32(src, 0);
    G32(0x6ccc68) = U32(src, 4);
    GTE_TRANSFORM(A(vtx[1]));
    src += 8;
    dp = &dep[1];
    for (k = 1; k <= count; k++) {
        GTE_PROJECT();
        G32(0x6ccc64) = (0u - ((uint32_t)(int32_t)S16(pts, 2) << 16)) | ((0u - U16(pts, 0)) & 0xffff);
        G32(0x6ccc68) = 0u - (uint32_t)(int32_t)S16(pts, 4);
        GTE_LIGHT();
        G32(0x6ccc64) = U32(src, 0);
        G32(0x6ccc68) = U32(src, 4);
        GTE_TRANSFORM(A(vtx[1 + k]));
        *dp++ = G32(0x6cccbc);
        src += 8;
        pts += 8;
    }
    GTE_PROJECT();
    G32(0x6ccc64) = (0u - ((uint32_t)(int32_t)S16(pts, 2) << 16)) | ((0u - U16(pts, 0)) & 0xffff);
    G32(0x6ccc68) = 0u - (uint32_t)(int32_t)S16(pts, 4);
    GTE_LIGHT();
    *dp = G32(0x6cccbc);

    if (record) {
        first = pk;
        U32(m, 0x1100) = pk;
    }
    for (;;) {
        uint8_t fl;

        left = U16(pk, 0x82);
        do {
            uint32_t f0 = U32(faces, 0), a, b, c, d, va, vb, vc, vd, w2, code;

            faces += 8;
            a = f0 & 0xff;
            b = (f0 >> 8) & 0xff;
            c = (f0 >> 16) & 0xff;
            d = f0 >> 24;
            tbl[0][1] = c;
            tbl[0][0] = d;
            va = A(vtx[a]);
            vb = A(vtx[b]);
            vc = A(vtx[c]);
            facing = pb_facing(va, vb, vc);
            if (mirror)
                facing ^= 0x80000000u;
            left -= 8;
            if ((int32_t)facing < 0) {
                vd = d ? A(vtx[d]) : vc;
                if (!(U32(va, 0) & U32(vd, 0) & U32(vc, 0) & U32(vb, 0))) {
                    w2 = U32(faces, -4);
                    pb_movsd(pk + 8, va, 6);
                    pb_movsd(pk + 0x28, vc, 6);
                    pb_movsd(pk + 0x48, vb, 6);
                    U32(pk, 4) = dep[a];
                    U32(pk, 0x44) = dep[b];
                    U32(pk, 0x24) = dep[c];
                    if (last)
                        U32(last, 0) = pk;
                    else
                        tbl[bucket][0] = pk;
                    last = pk;
                    code = 0x9c;
                    if (d) {
                        pb_movsd(pk + 0x68, vd, 6);
                        U32(pk, 0x64) = dep[d];
                        code = 0x8c;
                    }
                    U8(pk, 7) = (uint8_t)(((w2 >> 30) & 2) | code);
                    if (record) {
                        U8(cursor, 0) = (uint8_t)((int32_t)(pk - first) / 0x84);
                        cursor++;
                        if (cursor >= limit)
                            cursor--;
                    }
                }
            }
            pk += 0x84;
        } while (left > 0);
        fl = (uint8_t)left;
        if (fl & 1) {
            path <<= 1;
            if ((int32_t)facing < 0)
                path |= 4;
        }
        if (fl & 2) {
            tbl[bucket][1] = last;
            last = 0;
            bucket = (bucket + 1) & 0xff;
        }
        if (fl & 4)
            break;
    }

    order = U32(U32(hdr, 0x18) + path, 0);
    k = order & 0xf;
    do {
        pb_bucket_link(tbl[k][0], tbl[k][1], ot);
        order >>= 4;
        k = order & 0xf;
    } while (k);
    if (U32(hdr, 0x44)) {
        order = U32(U32(hdr, 0x44) + path, 0);
        for (k = order & 0xf; k; k = order & 0xf) {
            pb_bucket_link(tbl[k][0], tbl[k][1], ot);
            order >>= 4;
        }
    }
    if (record) {
        U32(m, 0x10fc) = cursor;
        U8(m, 0x1104) = 1;
    }
}

/* ===== 0x4226a0: a model's lighting (0x45b400 with the light vector), then
 * its drawing. ===== */
void GAMEAPI model_draw_at(uint32_t m, uint32_t light, uint32_t ot, int32_t dist)
{
    GCALL(0x45b400, model_lights_set)((uint8_t *)GM(m), (const uint32_t *)GM(light));
    model_draw_lod(m, ot, dist);
}

/* ===== 0x422540: transform the car's extra points (model +0x110c: +0x24
 * points, +0x10d count + 1) into +0x1114 (0x18 apart); +0x1108 = 1. ===== */
void GAMEAPI car_points_transform(uint32_t car)
{
    uint32_t mdl = U32(car, 0x110c), out = car + 0x1114, src, count;

    src = U32(mdl, 0x24);
    count = U8(mdl, 0x10d);
    G32(0x6ccc64) = U32(src, 0);
    G32(0x6ccc68) = U32(src, 4);
    GTE_TRANSFORM(out);
    src += 8;
    while (count--) {
        GTE_PROJECT();
        out += 0x18;
        G32(0x6ccc64) = U32(src, 0);
        G32(0x6ccc68) = U32(src, 4);
        GTE_TRANSFORM(out);
        src += 8;
    }
    GTE_PROJECT();
    U8(car, 0x1108) = 1;
}

/* ===== 0x4225c0: a wheel's facing: three points (+0x84.., 8 apart)
 * projected (0x4266d0), their screen xy into 0x6ccc94..0x6ccc9c, the
 * normal clip 0x4278a0; 1 when it faces away (negated when 0x6cce94). ===== */
int32_t GAMEAPI wheel_facing_away(uint32_t w)
{
    uint32_t xy[3], s = w + 0x88, k;
    int32_t v;

    for (k = 0; k < 3; k++, s += 8) {
        G32(0x6ccc64) = U32(s, -4);
        G32(0x6ccc68) = U32(s, 0);
        GCALL(0x4266d0, gte_rtps_float)();
        xy[k] = G32(0x6ccc9c);
    }
    G32(0x6ccc94) = xy[0];
    G32(0x6ccc9c) = xy[2];
    G32(0x6ccc98) = xy[1];
    GCALL(0x4278a0, gte_nclip)();
    v = GI32(0x6cccc4);
    if (G32(0x6cce94))
        v = (int32_t)(0u - (uint32_t)v);
    return v < 0;
}

/* ===== 0x4224a0: the car's four wheels (+0x324, 0xc4 apart, model +0x6c):
 * into the car's packet block +0x10 when farther than the body, else +0x14
 * facing away / +0x28. ===== */
void GAMEAPI car_wheels_draw(uint32_t car, uint32_t light)
{
    uint32_t p = U32(car, 0x14b0), w = car + 0x324, k, mdl, ot;
    int32_t body = I32(car, 0x14b4), d, away;

    for (k = 0; k < 4; k++, w += 0xc4) {
        mdl = U32(w, 0x6c);
        d = car_depth_offset(mdl, car + 0x14bc);
        away = wheel_facing_away(w);
        if (d > body)
            ot = p + 0x10;
        else
            ot = away ? p + 0x14 : p + 0x28;
        model_draw_at(mdl, light, ot, body);
    }
}

/* ===== 0x422440: draw a car: depth with the ride offset (+0x14b4); when in
 * front: body (packet block +0x1c), extra points, wheels, then the block
 * into the ordering table (bias: 0x28 for reflections). ===== */
void GAMEAPI car_model_draw(uint32_t car, uint32_t light, int32_t bias)
{
    int32_t d = car_depth_offset(car, car + 0x14bc);

    I32(car, 0x14b4) = d;
    if (d > 0) {
        model_draw_at(car, light, U32(car, 0x14b0) + 0x1c, d);
        car_points_transform(car);
        car_wheels_draw(car, light);
        car_ot_insert(car, d, bias);
    }
}

/* ===== 0x422f50: the car's shadow: two shadow points (model + model
 * +0x4c, kind byte +6 = 2) transformed and projected, a square of radius
 * (*0x6cce44 << 11 >> 8) / depth (1..0x80) on screen as a 0x98 packet
 * (+0x2290, +0xf0 for the second view, 0x78 apart; colour by damage /
 * +0x640, scaled by 0xdc unless 0x6ccd84 bit 0), linked into `ot` (or the
 * second view's table by depth when 0x6cce84). Stops at the first point
 * that is not a shadow point or not on screen. ===== */
void GAMEAPI car_shadow_draw(uint32_t car, uint32_t ot)
{
    uint32_t rec[6] = {0, 0, 0, 0, 0, 0};
    uint32_t hdr = U32(car, 0x10), pts, screen, pk, i, col, r, rr, xy, t, rf, slot;
    int32_t z;

    pts = U16(hdr, 0x4c) + hdr;
    screen = G32(0x6cd478);
    pk = car + 0x2290;
    if (g_view != VIEW0)
        pk += 0xf0;
    car_depth_offset(car, car + 0x14bc);
    for (i = 0; i < 2; i++, pts += 8, pk += 0x78) {
        if (U8(pts, 6) != 2)
            return;
        G32(0x6ccc64) = U32(pts, 0);
        G32(0x6ccc68) = U32(pts, 4);
        GTE_TRANSFORM(A(rec));
        if (U8(car, 0x63e) > 0x20)
            col = 0x9a0000ffu;
        else
            col = I32(car, 0x640) >= 0 ? 0x9a0000a0u : 0x9affffffu;
        if (G8(0x6ccd84) & 1) {
            U32(pk, 4) = col;
        } else {
            U32(pk, 4) = (col & 0xff000000u) | ((((col >> 16) & 0xff) * 0xdc) >> 8) << 16 |
                         ((((col >> 8) & 0xff) * 0xdc) >> 8) << 8 | (((col & 0xff) * 0xdc) >> 8);
        }
        U8(pk, 0x22) = 0xdc;
        GTE_PROJECT();
        rec[1] = pb_fsub(rec[1], G32(0x4a3348));
        z = (int32_t)pb_fist(rec[1]);
        if (z <= 0x20)
            continue;
        r = (uint32_t)((int32_t)(G32(0x6cce44) << 11) >> 8) / (uint32_t)z;
        if ((int32_t)r < 1)
            continue;
        if ((int32_t)r > 0x80)
            r = 0x80;
        xy = pb_fist(rec[3]) << 16;
        xy |= pb_fist(rec[2]);
        rr = r << 16 | r;
        if ((rr + xy) & 0x80008000u)
            return;
        t = xy - rr - screen;
        if ((int32_t)t > 0)
            return;
        if (!(t & 0x8000))
            return;
        rf = pb_fild(r);
        U32(pk, 8) = 0x40;
        t = pb_fsub(rec[2], rf);
        U32(pk, 0x48) = t;
        U32(pk, 0x10) = t;
        t = pb_fsub(rec[3], rf);
        U32(pk, 0x30) = t;
        U32(pk, 0x14) = t;
        t = pb_fadd(rf, rec[2]);
        U32(pk, 0x64) = t;
        U32(pk, 0x2c) = t;
        t = pb_fadd(rf, rec[3]);
        U32(pk, 0x68) = t;
        U32(pk, 0x4c) = t;
        U32(pk, 0x60) = rec[1];
        U32(pk, 0x44) = rec[1];
        U32(pk, 0x28) = rec[1];
        U32(pk, 0xc) = rec[1];
        if (G32(0x6cce84))
            slot = G32(0x6cdd80 + (uint32_t)pb_ot_clamp(z >> 5) * 4) & 0x7fffffff;
        else
            slot = ot & 0x7fffffff;
        U32(pk, 0) = U32(slot, 0);
        U32(slot, 0) = pk;
    }
}

/* ===== 0x422210: the cars' effects, each loop over the race's cars whose
 * depth (+0x14b4) is above -80: 0x46e4b0 (block +0x20); 0x47b470 (blocks
 * +0x24 / +0x18) -> +0x228c, also for a reflection car; the shadow into
 * +0x228c's table; 0x46dff0 (blocks +0x2c / +0xc) and 0x46c9d0 (block +8)
 * except the car 0x6cce40 bits 7..10 names when its bit 6 is set; 0x475580
 * (index, blocks +0xc / +0x2c) above -100. ===== */
void GAMEAPI cars_effects_draw(void)
{
    uint32_t race = g_race, i, car, rec, mdl, p, sel;

    for (i = 0; i < U8(race, 0x54d); i++) {
        car = G32(CAR_RECORD(i));
        if (I32(car, 0x14b4) > -80) {
            GCALL(0x46e4b0, car_trail_draw)((uint8_t *)GM(car), U32(car, 0x14b0) + 0x20);
            race = g_race;
        }
    }
    for (i = 0; i < U8(race, 0x54d); i++) {
        rec = CAR_RECORD(i);
        car = U32(rec, 0);
        if (I32(car, 0x14b4) > -80) {
            p = U32(car, 0x14b0);
            U32(car, 0x228c) = GCALL(0x47b470, car_glow_packets_draw)((uint8_t *)GM(car), p + 0x24, p + 0x18);
            if (U8(rec, 0x264)) {
                mdl = U32(rec, 0x260);
                p = U32(mdl, 0x14b0);
                U32(mdl, 0x228c) = GCALL(0x47b470, car_glow_packets_draw)((uint8_t *)GM(mdl), p + 0x24, p + 0x18);
            }
            race = g_race;
        }
    }
    for (i = 0; i < U8(race, 0x54d); i++) {
        car = G32(CAR_RECORD(i));
        if (I32(car, 0x14b4) > -80 && U32(car, 0x228c)) {
            car_shadow_draw(car, U32(car, 0x228c));
            race = g_race;
        }
    }
    for (i = 0; i < U8(race, 0x54d); i++) {
        sel = G32(0x6cce40);
        car = G32(CAR_RECORD(i));
        if ((sel & 0x40) && ((sel >> 7) & 0xf) == i)
            continue;
        if (I32(car, 0x14b4) > -80) {
            p = U32(car, 0x14b0);
            GCALL(0x46dff0, shield_packets_link)((const uint8_t *)GM(car), p + 0x2c, p + 0xc);
            race = g_race;
        }
    }
    for (i = 0; i < U8(race, 0x54d); i++) {
        car = G32(CAR_RECORD(i));
        if (I32(car, 0x14b4) > -100) {
            p = U32(car, 0x14b0);
            GCALL(0x475580, car_flame_draw)((int32_t)i, p + 0xc, p + 0x2c);
            race = g_race;
        }
    }
    for (i = 0; i < U8(race, 0x54d); i++) {
        sel = G32(0x6cce40);
        car = G32(CAR_RECORD(i));
        if ((sel & 0x40) && ((sel >> 7) & 0xf) == i)
            continue;
        if (I32(car, 0x14b4) > -80) {
            GCALL(0x46c9d0, shadow_add)(car, U32(car, 0x14b0) + 8);
            race = g_race;
        }
    }
}

/* ===== 0x421ef0: draw the cars for the frame: packet blocks advanced,
 * reflections updated, 0x4231f0; the six extra models (0x6ce640, light
 * index 0x6ce620; 0x459cb0 sets the light vector when the index changes),
 * then the race's cars with light 0, each flagged +0x646 (cleared), with
 * its reflection car (bias 0x28); then the effects. ===== */
void GAMEAPI cars_draw(void)
{
    uint32_t key = 0, light[3], i, car, idx, rec;

    G32(0x6ccc7c) = 0xffffff;
    GCALL(0x422080, cars_packets_next)();
    GCALL(0x422c80, cars_reflections_update)();
    GCALL(0x4231f0, cars_view_flags_update)();
    light[2] = 0;
    light[1] = 0;
    light[0] = 0;
    for (i = 0; i < 0x18; i += 4) {
        car = G32(0x6ce640 + i);
        idx = G32(0x6ce620 + i);
        if (!car)
            break;
        if (!U8(car, 0x646))
            continue;
        if (idx * 2 != key) {
            key = idx * 2;
            GCALL(0x459cb0, light_colours_from_ids)(idx, light);
        }
        GCALL(0x422140, car_ride_offset_update)(car);
        car_model_draw(car, A(light), 0);
        U8(car, 0x646) = 0;
        rec = CAR_RECORD(U16(car, 0x64e));
        if (U8(rec, 0x264))
            car_model_draw(U32(rec, 0x260), A(light), 0x28);
    }
    for (i = 0; i < U8(g_race, 0x54d); i++) {
        car = G32(CAR_RECORD(i));
        if (!U8(car, 0x646))
            continue;
        if (key) {
            key = 0;
            GCALL(0x459cb0, light_colours_from_ids)(0, light);
        }
        GCALL(0x422140, car_ride_offset_update)(car);
        car_model_draw(car, A(light), 0);
        U8(car, 0x646) = 0;
        rec = CAR_RECORD(U16(car, 0x64e));
        if (U8(rec, 0x264))
            car_model_draw(U32(rec, 0x260), A(light), 0x28);
    }
    cars_effects_draw();
}

/* ===== 0x422e30: copy a 3x3 int16 matrix (8 dwords) and mirror it in y:
 * elements 0, 4, 5, 6 negated. ===== */
void GAMEAPI matrix_mirror_y(uint32_t dst, uint32_t src)
{
    int16_t a, b;

    pb_movsd(dst, src, 8);
    a = S16(dst, 8);
    b = S16(dst, 0xa);
    S16(dst, 8) = (int16_t)-a;
    S16(dst, 0) = (int16_t)-S16(dst, 0);
    a = S16(dst, 0xc);
    S16(dst, 0xa) = (int16_t)-b;
    S16(dst, 6) = S16(dst, 6);
    S16(dst, 0xc) = (int16_t)-a;
}

/* ===== 0x422e80: copy a position and mirror its y about the height h:
 * y = 2h - y. ===== */
void GAMEAPI position_mirror_y(uint32_t dst, uint32_t src, int32_t h)
{
    U32(dst, 0) = U32(src, 0);
    U32(dst, 4) = U32(src, 4);
    U32(dst, 8) = U32(src, 8);
    U32(dst, 4) = ((uint32_t)h << 1) - U32(dst, 4);
}

/* ===== 0x422d30: build a car's reflection: matrix (+0xc4) and position
 * (+0x34) mirrored about the water height, the same for its four wheel
 * bodies (+0x390, 0xc4 apart), the ride offset negated, the 0x300 bytes
 * from +0x1108 copied (twice, sic) keeping the reflection's +0x110c. ===== */
void GAMEAPI car_reflection_build(uint32_t src, uint32_t dst, int32_t h)
{
    uint32_t delta, b, k, ws, wd, save;

    matrix_mirror_y(dst + 0xc4, src + 0xc4);
    position_mirror_y(dst + 0x34, src + 0x34, h);
    delta = dst - src;
    for (k = 0, b = src + 0x390; k < 4; k++, b += 0xc4) {
        ws = U32(b, 0) + 0x34;
        wd = U32(b + delta, 0) + 0x34;
        matrix_mirror_y(wd + 0x90, ws + 0x90);
        position_mirror_y(wd, ws, h);
    }
    U32(dst, 0x14bc) = 0u - U32(src, 0x14bc);
    U32(dst, 0x14c0) = 0u - U32(src, 0x14c0);
    U32(dst, 0x14c4) = 0u - U32(src, 0x14c4);
    save = U32(dst, 0x110c);
    pb_movsd(dst + 0x1108, src + 0x1108, 0xc0);
    pb_movsd(dst + 0x1108, src + 0x1108, 0xc0);
    U32(dst, 0x110c) = save;
}

/* ===== 0x422eb0: the water box (world *0x6c1eb0: +0xd4 boxes of int16 x,
 * y, z, r; +0xff count) holding the car's position (whole units): its y in
 * *out, 1; 0 if none. ===== */
int32_t GAMEAPI car_water_find(uint32_t car, uint32_t out)
{
    uint32_t w = G32(0x6c1eb0), b, end;
    int32_t x, y, z, r;

    b = U32(w, 0xd4);
    end = b + (uint32_t)U8(w, 0xff) * 8;
    x = I32(car, 0x34) >> 16;
    y = I32(car, 0x38) >> 16;
    z = I32(car, 0x3c) >> 16;
    for (; b < end; b += 8) {
        r = S16(b, 6);
        if (pb_abs(x - S16(b, 0)) > r)
            continue;
        if (pb_abs(z - S16(b, 4)) > r)
            continue;
        if (pb_abs(y - S16(b, 2)) > r)
            continue;
        I32(out, 0) = S16(b, 2);
        return 1;
    }
    return 0;
}

/* ===== 0x422c80: with reflections on (race +0x54e), each car over water
 * gets its reflection built (record +0x264 = 1), else +0x264 = 0. ===== */
void GAMEAPI cars_reflections_update(void)
{
    uint32_t race = g_race, i, rec, car, mdl;
    int32_t h = 0;

    if (U8(race, 0x54e)) {
        for (i = 0; i < U8(race, 0x54d); i++) {
            rec = CAR_RECORD(i);
            if (car_water_find(U32(rec, 0), A(&h))) {
                mdl = U32(rec, 0x260);
                car = U32(rec, 0);
                U8(rec, 0x264) = 1;
                car_reflection_build(car, mdl, (int32_t)((uint32_t)h << 16));
            } else {
                U8(rec, 0x264) = 0;
            }
            race = g_race;
        }
    } else {
        for (i = 0; i < U8(race, 0x54d); i++)
            U8(CAR_RECORD(i), 0x264) = 0;
    }
}
