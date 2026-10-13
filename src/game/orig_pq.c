/* Group PQ: the original's still-reachable functions in 0x478df0-0x47c730
 * (the view-drawing source file: what draw_view 0x479be0 and the track
 * visibility manager 0x47a030 call, and the glow / debris helpers next to
 * them).
 *
 *  - per-view effects called by draw_view: the texture animations
 *    (0x478df0), the screen messages' glyph packets (0x4792a0), the time
 *    warp's sound and view wobble (0x479940 / 0x479a80), the surface tint
 *    (0x479b30), the three depth-marker packets (0x479f60), the engine glow
 *    textures (0x47bd20 / 0x47bc80) and the empty PlayStation wait 0x479ae0;
 *  - the track: the visible runs of cells per level of detail (0x47a0d0,
 *    splitting each run with 0x47a1d0 and the projected-quad test 0x47a440),
 *    the texture words of the runs that came into view (0x47a500), the
 *    per-row light flags (0x47c420) and the lit vertex colours (0x47c730);
 *  - the engine glow's packets (0x47b470), the debris burst (0x47be70) and
 *    the object that breaks into debris (0x47c110).
 *
 * Float steps are the original's x87 instructions in inline asm (fild /
 * fstp / fadd / fcomp / fistp), so the control word rounds as it does there;
 * 0x47a440 hands st(0) from the transform pointer *0x6ccd78 to the
 * projection *0x6ccd80 and is the original's instruction sequence in asm.
 * Integer logic is C. Everything outside the group is called by address. */
#include "../runtime.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S8(p, off) (*(int8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define GS16(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

typedef int32_t(GAMEAPI *pq_fn0)(void);
typedef int32_t(GAMEAPI *pq_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *pq_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *pq_fn4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(GAMEAPI *pq_fn5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef float(GAMEAPI *pq_facing_fn)(uint32_t a, uint32_t b, uint32_t c);

#define CALL0(a) (((pq_fn0)(a))())
#define CALL1(a, x) (((pq_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((pq_fn2)(a))(A(x), A(y)))
#define CALL4(a, x, y, z, w) (((pq_fn4)(a))(A(x), A(y), A(z), A(w)))
#define CALL5(a, x, y, z, w, v) (((pq_fn5)(a))(A(x), A(y), A(z), A(w), A(v)))

#define pq_sound_play(s, k, v, p, o) ((uint8_t)CALL5(0x471c90, s, k, v, p, o))
#define pq_voice_stop(v) CALL1(0x471f40, v)
#define pq_voice_pitch(v, p) CALL2(0x471f80, v, p)
#define pq_surface_kind(body) ((uint32_t)CALL1(0x40d6f0, body))
#define pq_surface_flash() CALL0(0x47db80)
#define pq_tint_add(c) CALL1(0x42bfc0, c)
#define pq_draw_prim(p) CALL1(0x428460, p)
#define pq_grid_unlink2(o) CALL1(0x4612a0, o)
#define pq_grid_link2_at(o, x, y, z) CALL4(0x460ff0, o, x, y, z)
#define pq_vec_normalise(v) CALL1(0x480c70, v)
#define pq_effects(p, v, a, b, c) CALL5(0x42aa30, p, v, a, b, c)
#define pq_grid_unlink(o) CALL1(0x4611d0, o)
#define pq_triggers(o) CALL1(0x4427a0, o)
#define pq_palette() ((uint32_t)CALL0(0x486350))
#define pq_light_matrix_load(m) CALL1(0x428940, m)
#define pq_square_ir() CALL0(0x4282d0)
#define pq_light_colour() CALL0(0x427100)

/* This group's functions (also in build/groups/pq_protos.txt for physics.h). */
void GAMEAPI texture_anims_step(void); /* 0x478df0 */
void GAMEAPI text_messages_draw(uint32_t owner, int32_t mode); /* 0x4792a0 */
void GAMEAPI time_warp_view_effect(const uint32_t *slot); /* 0x479940 */
uint32_t GAMEAPI time_warp_wobble_step(uint8_t *phase, uint32_t add, uint32_t row, uint32_t col,
                                       uint32_t shift); /* 0x479a80 */
int32_t GAMEAPI ps_wait_none(void); /* 0x479ae0 */
void GAMEAPI view_surface_tint(uint8_t *slot); /* 0x479b30 */
void GAMEAPI view_depth_markers_link(void); /* 0x479f60 */
void GAMEAPI track_runs_visible(const uint8_t *cell, uint32_t *out); /* 0x47a0d0 */
void GAMEAPI track_run_split(uint32_t *out, uint32_t kind, uint32_t start, uint32_t n, uint32_t level); /* 0x47a1d0 */
int32_t track_quad_in_range(uint8_t *quad); /* 0x47a440: asm, cdecl */
void GAMEAPI track_runs_diff_texture(uint32_t *now, uint32_t *before); /* 0x47a500 */
uint32_t GAMEAPI car_glow_packets_draw(uint8_t *g, uint32_t ot_front, uint32_t ot_back); /* 0x47b470 */
void GAMEAPI glow_texture_cycle(uint8_t *g); /* 0x47bc80 */
void GAMEAPI cars_glow_texture_cycle(void); /* 0x47bd20 */
void GAMEAPI debris_burst(int32_t x, int32_t y, int32_t z, uint32_t count, uint32_t spread,
                          uint32_t colour); /* 0x47be70 */
int32_t GAMEAPI object_debris_break(uint8_t *obj); /* 0x47c110 */
void GAMEAPI track_runs_light_flags(uint32_t *list); /* 0x47c420 */
void GAMEAPI track_runs_light(uint32_t *list, int32_t mode); /* 0x47c730 */

/* ---- x87 helpers: the original's instructions ---- */

/* fild dword v; fstp; fld; fadd dword k; fstp: v as a single, plus k. */
static inline uint32_t pq_int_add(int32_t v, uint32_t k)
{
    uint32_t r, t;
    __asm__ volatile("fildl %2\n\tfstps %1\n\tflds %1\n\tfadds %3\n\tfstps %0"
                     : "=m"(r), "=m"(t)
                     : "m"(v), "m"(k)
                     : "st");
    return r;
}

/* fild dword; fstp dword: an integer to a single, rounded by the control word. */
static inline uint32_t pq_fild(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fld a; fcomp b; fnstsw ax: C0 (a below b, or unordered). */
static inline int pq_below(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 1;
}

/* fld a; fistp dword: a rounded by the control word. */
static inline int32_t pq_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* Links a packet into an ordering-table entry (top bit masked). */
static inline void pq_ot_link(uint32_t entry, uint8_t *pkt)
{
    uint8_t *ot = PTR(entry & 0x7fffffff);
    U32(pkt, 0) = U32(ot, 0);
    U32(ot, 0) = A(pkt);
}

/* ===================================================================== */

/* 0x478df0: once per frame (0x6c0a50 against 0x4d4b74), each animated
 * texture record (0x6bfb44, count 0x6bfb48, 0x18 bytes) counts down +0; at
 * zero it takes the frame +6 of the frame table 0x6bfb4c (6 bytes: u, v,
 * time), jumping back to frame +0 (signed) when that frame's time is 0, and
 * stores u / v at +0x10 / +0x12, the next frame at +6 and the time at +0. */
void GAMEAPI texture_anims_step(void)
{
    uint32_t frame = G32(0x6c0a50), i;
    uint8_t *a;

    if (G32(0x4d4b74) == frame)
        return;
    a = PTR(G32(0x6bfb44));
    G32(0x4d4b74) = frame;
    for (i = 0; i < G32(0x6bfb48); i++, a += 0x18) {
        uint32_t t = U32(a, 0) - 1;
        U32(a, 0) = t;
        if (t == 0) {
            uint32_t tab = G32(0x6bfb4c);
            uint32_t idx = U16(a, 6);
            uint8_t *f = PTR(tab + 6 * idx);
            int32_t time = S16(f, 4);
            if (time == 0) {
                idx = (uint32_t)(int32_t)S16(f, 0);
                f = PTR(tab + 6 * idx);
                time = S16(f, 4);
            }
            U16(a, 0x10) = U16(f, 0);
            U16(a, 0x12) = U16(f, 2);
            U16(a, 6) = (uint16_t)(idx + 1);
            I32(a, 0) = time;
        }
    }
}

/* 0x4792a0(owner, mode): the screen messages' glyphs as textured quads
 * (0x84-byte packets from 0x5d9b40, stopping when the next would pass
 * 0x5f698c). Mode 1: the menu list 0x6b95a4 (count 0x6b9824), drawn at once
 * (0x428460), grey 0x80 / the selected line 0x6b8d66 at 0x40 or 0xff
 * (0x6b8d68 bit 4); otherwise the 0x1c race messages 0x5f6944, linked into
 * the ordering table *(0x6ce608 + 4). Records (0x50): +0 owner (0 = all),
 * +4 / +6 x / y, +8 length, +9 flags (1 blink, 0x20 first half of 512
 * frames), +0xc colour scale (-1 none), +0xe size, +0x10 text. Font
 * *0x5f6940: +0x10 space width, +0x14 gap, +0x101c glyph index per
 * character (0xff none), glyphs of 16 bytes from +0x10 (+0x1e height,
 * +0x20 width, +0x22 height add, +0x24 clut, +0x26 / +0x28 u / v). */
void GAMEAPI text_messages_draw(uint32_t owner, int32_t mode)
{
    uint8_t *font = PTR(G32(0x5f6940));
    uint8_t *pkt = PTR(0x5d9b40);
    const int16_t *sel = (const int16_t *)(uintptr_t)0x6b9828;
    uint32_t dim = 0; /* sic: the original's first scaled colour keeps a stack leftover as its top byte */
    uint8_t *rec;
    int32_t count, i;

    if (mode == 1) {
        rec = PTR(0x6b95a4);
        count = GI32(0x6b9824);
    } else {
        rec = PTR(0x5f6944);
        count = 0x1c;
    }
    for (i = 0; i < count; i++, sel++, rec += 0x50) {
        uint8_t len = U8(rec, 8), fl;
        const uint8_t *ch;
        int32_t n, x, y;

        if (len == 0)
            continue;
        fl = U8(rec, 9);
        if ((fl & 1) && (G8(0x6c0a50) & 8))
            continue;
        if ((fl & 0x20) && (G32(0x6c0a50) & 0x1ff) > 0x8e)
            continue;
        if (U32(rec, 0) != 0 && U32(rec, 0) != owner)
            continue;
        ch = rec + 0x10;
        n = len;
        x = S16(rec, 4);
        y = S16(rec, 6);
        do {
            uint32_t c = *ch;
            if (c == 0x20) {
                x += ((S16(rec, 0xe) * I32(font, 0x10)) >> 8) * GI32(0x6cd410) >> 8;
            } else {
                uint8_t gi = U8(font, c + 0x101c);
                if (gi != 0xff) {
                    uint8_t *g = font + gi * 16;
                    int32_t sc = S16(rec, 0xe), k = GI32(0x6cd410);
                    uint32_t h = U16(g, 0x1e), w = U16(g, 0x20), f;
                    int32_t dw = ((int32_t)(w * (uint32_t)sc) >> 8) * k;
                    int32_t dh = ((int32_t)(h * (uint32_t)sc) >> 8) * k;
                    int32_t hh = (int32_t)((U16(g, 0x22) + h) * (uint32_t)sc) >> 8;
                    uint32_t cv, col;
                    uint8_t code;
                    int16_t s;

                    f = pq_int_add(x, G32(0x6ccd54));
                    U32(pkt, 8) = 0x40;
                    U32(pkt, 0x6c) = 0x40066666;
                    U32(pkt, 0x4c) = 0x40066666;
                    U32(pkt, 0x2c) = 0x40066666;
                    U32(pkt, 0xc) = 0x40066666;
                    U32(pkt, 0x50) = f;
                    U32(pkt, 0x10) = f;
                    k = GI32(0x6cd410);
                    f = pq_int_add((dh >> 8) + y, G32(0x6ccd58));
                    U32(pkt, 0x34) = f;
                    U32(pkt, 0x14) = f;
                    f = pq_int_add((dw >> 8) + x, G32(0x6ccd54));
                    U32(pkt, 0x70) = f;
                    U32(pkt, 0x30) = f;
                    f = pq_int_add((hh * k >> 8) + y, G32(0x6ccd58));
                    U32(pkt, 0x74) = f;
                    U32(pkt, 0x54) = f;
                    U16(pkt, 0x22) = 0xff;
                    U16(pkt, 0x42) = U16(g, 0x24) | 0x40;
                    U8(pkt, 0x60) = U8(g, 0x26);
                    U8(pkt, 0x20) = U8(g, 0x26);
                    U8(pkt, 0x41) = U8(g, 0x28);
                    U8(pkt, 0x21) = U8(g, 0x28);
                    U8(pkt, 0x80) = (uint8_t)(U8(g, 0x26) + U8(g, 0x20) - 1);
                    U8(pkt, 0x40) = (uint8_t)(U8(g, 0x26) + U8(g, 0x20) - 1);
                    U8(pkt, 0x81) = (uint8_t)(U8(g, 0x28) + U8(g, 0x22) - 1);
                    U8(pkt, 0x61) = (uint8_t)(U8(g, 0x28) + U8(g, 0x22) - 1);
                    x += (((int32_t)((uint32_t)S16(rec, 0xe) * U16(g, 0x20)) >> 8) * k >> 8) + I32(font, 0x14);

                    if (mode == 1 && *sel >= 0) {
                        if (*sel == GS16(0x6b8d66))
                            cv = (G8(0x6b8d68) & 0x10) ? 0xff : 0x40;
                        else
                            cv = 0x80;
                    } else {
                        cv = 0xff;
                    }
                    s = S16(rec, 0xc);
                    code = U8(pkt, 7);
                    col = cv | cv << 8 | cv << 16;
                    if (s != -1) {
                        uint32_t b = (uint8_t)((int32_t)(cv * (uint32_t)(int32_t)s) >> 8);
                        col = (dim & 0xff000000) | b << 16 | b << 8 | b;
                    }
                    U32(pkt, 4) = col;
                    U32(pkt, 0x24) = col;
                    dim = col >> 2 & 0x3f3f3f;
                    U32(pkt, 0x44) = dim;
                    U32(pkt, 0x64) = dim;
                    U8(pkt, 7) = code;
                    if (mode == 1)
                        pq_draw_prim(pkt);
                    else
                        pq_ot_link(U32(G32(0x6ce608), 4), pkt);
                    pkt += 0x84;
                    if ((int32_t)(A(pkt) + 0x4c) >= 0x5f698c)
                        return;
                }
            }
            ch++;
        } while (--n > 0);
    }
}

/* 0x479940(slot): the time warp's view. Off (0x5d9b20 = 0): its sound
 * (voice 0x4b241c) stopped. The warping car's own view (0x5d9b24 = slot's
 * car): the sound started, the wobble terms 0x6cd482 / 84 / 86 / 8c
 * cleared. Other views, once per frame: the four wobble phases stepped
 * (0x479a80), the sound started if needed and its pitch set from phase
 * 0x4d4b88 and the warp's strength 0x5d9b2c. */
void GAMEAPI time_warp_view_effect(const uint32_t *slot)
{
    uint8_t v;

    if (G32(0x5d9b20) == 0) {
        if (G8(0x4b241c) != 0x7f) {
            pq_voice_stop(G8(0x4b241c));
            G8(0x4b241c) = 0x7f;
        }
        return;
    }
    if (G32(0x5d9b24) == slot[0]) {
        if (G8(0x4b241c) == 0x7f)
            G8(0x4b241c) = pq_sound_play(0x2c, 9, 0x400, 0x3fff, 0);
        G16(0x6cd482) = 0;
        G16(0x6cd486) = 0;
        G16(0x6cd48c) = 0;
        G16(0x6cd484) = 0;
        return;
    }
    if (G32(0x4d4b8c) == G32(0x6c0a50))
        return;
    G32(0x4d4b8c) = G32(0x6c0a50);
    time_warp_wobble_step(PTR(0x4d4b88), 2, 0, 1, 3);
    time_warp_wobble_step(PTR(0x4b241d), 4, 1, 0, 4);
    time_warp_wobble_step(PTR(0x4b241e), 6, 2, 0, 3);
    time_warp_wobble_step(PTR(0x4b241f), 8, 0, 2, 2);
    v = G8(0x4b241c);
    if (v == 0x7f) {
        v = pq_sound_play(0x2c, 9, 0x400, 0x3fff, 0);
        G8(0x4b241c) = v;
        if (v == 0x7f)
            return;
    }
    /* the original pushes the pitch with leftover upper bits; 0x471f80 reads only its low word */
    pq_voice_pitch(v, (uint32_t)G8(0x4d4b88) - ((0x10000 - G32(0x5d9b2c)) >> 7) + 0x400);
}

/* 0x479a80(phase, add, row, col, shift): phase += add; the wobble term
 * 0x6cd480[row * 3 + col] (int16) = sine 0x4b24f8[phase] >> shift, scaled
 * by 0x10000 - 0x5d9b2c (16.16). */
uint32_t GAMEAPI time_warp_wobble_step(uint8_t *phase, uint32_t add, uint32_t row, uint32_t col, uint32_t shift)
{
    uint8_t b = (uint8_t)(*phase + (uint8_t)add);
    int32_t v;
    int64_t prod;
    uint32_t r;

    *phase = b;
    v = (int32_t)GS16(0x4b24f8 + 2 * b) >> (shift & 31);
    prod = (int64_t)v * (int64_t)(int32_t)(0x10000 - G32(0x5d9b2c));
    r = (uint32_t)((uint64_t)prod >> 16);
    G16(0x6cd480 + 2 * (3 * row + col)) = (uint16_t)r;
    return r;
}

/* 0x479ae0: an empty PlayStation-era wait; returns 0. */
int32_t GAMEAPI ps_wait_none(void)
{
    return 0;
}

/* 0x479b30(slot): the tint of driving on a special surface. The level
 * slot +0x256 rises by 8 (to 0xff) while the car's surface (0x40d6f0) is
 * one (kind kept at +0x257) and falls by 8 after; while above 0 the screen
 * is tinted (0x42bfc0) blue-ish for kind 1 (with 0x47db80) or red-ish. */
void GAMEAPI view_surface_tint(uint8_t *slot)
{
    uint32_t lvl = U8(slot, 0x256);
    uint32_t kind = pq_surface_kind(U32(slot, 0));
    uint32_t e, s;

    if (kind != 0) {
        lvl += 8;
        U8(slot, 0x257) = (uint8_t)kind;
        if ((int32_t)lvl > 0xff)
            lvl = 0xff;
    } else {
        if ((int32_t)lvl <= 0) {
            U8(slot, 0x256) = (uint8_t)lvl;
            return;
        }
        lvl -= 8;
        if ((int32_t)lvl < 0)
            lvl = 0;
        kind = U8(slot, 0x257);
        if (lvl == 0) {
            U8(slot, 0x256) = 0;
            return;
        }
    }
    if (kind == 1) {
        e = 0x40;
        s = 0xa0;
        pq_surface_flash();
    } else {
        e = 0xa0;
        s = 0x40;
    }
    pq_tint_add((((uint32_t)((int32_t)(s * lvl) >> 8) << 8 | (uint32_t)((int32_t)(lvl << 6) >> 8)) << 8) |
                (uint32_t)((int32_t)(e * lvl) >> 8));
    U8(slot, 0x256) = (uint8_t)lvl;
}

/* 0x479f60: the three depth-marker packets 0x5d9b00 / 08 / 10 (codes 0, 1,
 * 2; +7 = 0xa0) linked into the ordering table *0x6ce60c + 0x70 at the
 * near depth 0x6ccd98 - 20 (1..0x1ff), the far depth 0x6ccd9c + 20 (at
 * least 1, at most the last 0x6ccd94 capped 0x1ff) and the last. */
void GAMEAPI view_depth_markers_link(void)
{
    int32_t d, last;

    G8(0x5d9b04) = 0;
    G8(0x5d9b07) = 0xa0;
    G8(0x5d9b0f) = 0xa0;
    G8(0x5d9b17) = 0xa0;
    d = GI32(0x6ccd98) - 0x14;
    G8(0x5d9b0c) = 1;
    G8(0x5d9b14) = 2;
    if (d < 1)
        d = 1;
    else if (d > 0x1ff)
        d = 0x1ff;
    pq_ot_link(U32(G32(0x6ce60c), 0x70 + 4 * d), PTR(0x5d9b00));
    last = GI32(0x6ccd94);
    if (last > 0x1ff)
        last = 0x1ff;
    d = GI32(0x6ccd9c) + 0x14;
    if (d < 1)
        d = 1;
    if (d > last)
        d = last;
    pq_ot_link(U32(G32(0x6ce60c), 0x70 + 4 * d), PTR(0x5d9b08));
    pq_ot_link(U32(G32(0x6ce60c), 0x70 + 4 * last), PTR(0x5d9b10));
}

/* 0x47a0d0(cell, out): the visible runs of the track from the camera's
 * cell. out[0] = 0, then the cell's (+4 low word) visibility records at
 * 0x6bfacc (u16 start | count << 11, a byte kind << 1 | last, then count
 * bytes step << 2 | level - 1; records 2-aligned) give runs of `step`
 * strips at a level of detail; a run within the level's distance (700,
 * 1500, 2500, 7000) of the far plane 0x6ccd08 is split into its visible
 * parts (0x47a1d0) appended to out (8 bytes: kind, start, count, level). */
void GAMEAPI track_runs_visible(const uint8_t *cell, uint32_t *out)
{
    static const int32_t dist[5] = {0, 700, 1500, 2500, 7000};
    const uint8_t *r = PTR(G32(0x6bfacc) + 2 * (U32(cell, 4) & 0xffff));

    out[0] = 0;
    for (;;) {
        uint32_t w = U16(r, 0);
        uint32_t kind = (uint32_t)(U8(r, 2) >> 1) & 0x7f;
        uint32_t start = w & 0x7ff;
        uint32_t n = w >> 11;
        const uint8_t *b = r + 3;
        for (; n; n--, b++) {
            uint32_t step = *b >> 2, lvl = (uint32_t)(*b & 3) + 1;
            if (pq_below(pq_fild(dist[lvl]), G32(0x6ccd08)))
                track_run_split(out, kind, start, step, lvl);
            start += step << (lvl - 1);
        }
        if (U8(r, 2) & 1)
            break;
        r = PTR(((uint32_t)(U16(r, 0) >> 11) + A(r) + 4) & ~1u);
    }
}

/* 0x47a1d0(out, kind, start, n, level): splits the run of n + 1 strips
 * (0x1c-byte quads from *(0x6bfac0[kind]) + 0x4c, every 1 << (level - 1)
 * at level of detail `level`) into the parts whose quads are in front
 * (0x47a440), stepping 4 (1 at level 1) and halving like a binary search
 * for the edges; each part (with a strip of margin before it) is appended
 * to out as (kind, start, count, level), out[0] counting. */
void GAMEAPI track_run_split(uint32_t *out, uint32_t kind, uint32_t start, uint32_t n, uint32_t level)
{
    uint16_t *cur = (uint16_t *)(uintptr_t)(A(out) + 8 * out[0] + 4);
    uint32_t lvl1 = (level & 0xffff) - 1, sh = lvl1 & 31;
    uint8_t *rec = PTR(U32(G32(0x6bfac0) + 4 * (kind & 0xffff), 0));
    uint32_t p = U32(rec, 0x4c) + (start & 0xffff) * 0x1c;
    int32_t step0 = lvl1 == 0 ? 1 : 4, step = step0;
    int32_t nn = (int32_t)(n & 0xffff);
    int32_t i = -step, lastend = -1, probed = -1, runstart = 0;
    uint32_t state = 0, vis = 0;

    p -= ((uint32_t)step << sh) * 0x1c;
    if (i > nn)
        return;
    do {
        int first = 1;
        uint32_t v;

        step = step0;
        while (i <= nn) {
            int32_t j = step + i;
            if (j > nn) {
                step = step / 2;
            } else if (j < 0) {
                int32_t hlf = step / 2;
                step = (hlf ^ (hlf >> 31)) - (hlf >> 31);
            } else {
                int32_t hlf;
                i = j;
                p += ((uint32_t)step << sh) * 0x1c;
                vis = (uint32_t)track_quad_in_range(PTR(p));
                probed = i;
                if (first) {
                    if (state == vis)
                        goto next;
                    first = 0;
                }
                hlf = step / 2;
                step = (hlf ^ (hlf >> 31)) - (hlf >> 31);
                if (state != vis)
                    step = -step;
            }
        next:
            if (step == 0)
                break;
        }
        if (first && state == 0)
            return;
        v = vis;
        if (state == vis) {
            int32_t d = probed - i;
            v = vis == 0;
            vis = v;
            i += d;
            p += ((uint32_t)d << sh) * 0x1c;
        }
        if (state == 0) {
            int32_t t;
            if (v != 1)
                goto advance;
            state = 1;
            t = i - lastend - 1;
            if (t > 1)
                t = 1;
            runstart = i - t;
            if (i != nn)
                goto advance;
        } else if (state == 1) {
            if (v != 0 && i != nn)
                goto advance;
        } else {
            goto advance;
        }
        cur[0] = (uint16_t)kind;
        state = 0;
        cur[3] = (uint16_t)level;
        cur[1] = (uint16_t)(((uint32_t)runstart << sh) + start);
        cur[2] = (uint16_t)(i == nn ? i - runstart : i - runstart + 1);
        cur += 4;
        lastend = i;
        out[0]++;
    advance:
        i++;
        p += (1u << sh) * 0x1c;
    } while (i <= nn);
}

/* 0x47a440(quad): 1 when all four corners of a track quad (+4 x / y
 * dwords, +0x14 z words; +2 = minus half the near limit) project nearer
 * than the limit (transform *0x6ccd78 then projection *0x6ccd80, which
 * takes st(0)); 0 at the first that does not. The corners' clip codes are
 * and-ed into a mask that starts at 0 (sic), so the answer is never
 * affected by them; the projection runs a fifth time before the 1. */
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl track_quad_in_range\n"
        ".type track_quad_in_range,@function\n"
        "track_quad_in_range:\n"
        "    push ebp\n"
        "    mov ebp, esp\n"
        "    sub esp, 0x1c\n"
        "    mov eax, dword ptr [ebp + 0x8]\n"
        "    push ebx\n"
        "    push esi\n"
        "    push edi\n"
        "    lea esi, [eax + 0x4]\n"
        "    lea edi, [eax + 0x14]\n"
        "    movsx eax, word ptr [eax + 0x2]\n"
        "    neg eax\n"
        "    shl eax, 1\n"
        "    mov dword ptr [ebp + 0x8], eax\n"
        "    fild dword ptr [ebp + 0x8]\n"
        "    mov ecx, dword ptr [esi]\n"
        "    xor edx, edx\n"
        "    fstp dword ptr [ebp - 0x4]\n"
        "    mov dword ptr [0x6ccc64], ecx\n"
        "    mov dx, word ptr [edi]\n"
        "    lea eax, [ebp - 0x1c]\n"
        "    mov dword ptr [0x6ccc68], edx\n"
        "    push eax\n"
        "    call dword ptr [0x6ccd78]\n"
        "    add esp, 0x4\n"
        "    xor ebx, ebx\n"
        "    mov dword ptr [ebp + 0x8], 0x3\n"
        "1:\n"
        "    add esi, 0x4\n"
        "    add edi, 0x2\n"
        "    call dword ptr [0x6ccd80]\n"
        "    fld dword ptr [ebp - 0x18]\n"
        "    fcomp dword ptr [ebp - 0x4]\n"
        "    and ebx, dword ptr [ebp - 0x1c]\n"
        "    fnstsw ax\n"
        "    test ah, 0x1\n"
        "    jne 2f\n"
        "    mov eax, dword ptr [ebp + 0x8]\n"
        "    test eax, eax\n"
        "    je 3f\n"
        "    mov ecx, dword ptr [esi]\n"
        "    xor edx, edx\n"
        "    mov dword ptr [0x6ccc64], ecx\n"
        "    mov dx, word ptr [edi]\n"
        "    lea eax, [ebp - 0x1c]\n"
        "    mov dword ptr [0x6ccc68], edx\n"
        "    push eax\n"
        "    call dword ptr [0x6ccd78]\n"
        "    mov eax, dword ptr [ebp + 0x8]\n"
        "    add esp, 0x4\n"
        "    dec eax\n"
        "    mov dword ptr [ebp + 0x8], eax\n"
        "    jmp 1b\n"
        "2:\n"
        "    pop edi\n"
        "    pop esi\n"
        "    xor eax, eax\n"
        "    pop ebx\n"
        "    mov esp, ebp\n"
        "    pop ebp\n"
        "    ret\n"
        "3:\n"
        "    call dword ptr [0x6ccd80]\n"
        "    mov eax, dword ptr [ebp - 0x1c]\n"
        "    pop edi\n"
        "    and eax, ebx\n"
        "    pop esi\n"
        "    neg eax\n"
        "    sbb eax, eax\n"
        "    pop ebx\n"
        "    inc eax\n"
        "    mov esp, ebp\n"
        "    pop ebp\n"
        "    ret\n"
        "    .size track_quad_in_range, .-track_quad_in_range\n"
        ".att_syntax prefix\n");

/* 0x47a500(now, before): the runs visible now (8-byte entries: kind,
 * start, count, level) minus the parts that were visible before (same
 * kind and level), as (kind, level, start, end) pairs in a stack copy;
 * each new part goes to 0x5d92c4 (count 0x5d92c0: kind, start, strips,
 * level) and its quads' vertex packets (0x6bfac4 + index * 0x120, +0x84 in
 * the second buffer) get their texture words (+0x20 / +0x40 / +0x60 /
 * +0x80) from the texture table (*0x6c1e14 at level 1, *0x6c1eb0 above);
 * a texture word with the top bit also sets the second quad's uv, grey
 * colours and code 0x8c. */
void GAMEAPI track_runs_diff_texture(uint32_t *now, uint32_t *before)
{
    uint8_t buf[0x320];
    uint8_t *aa = buf, *bb = buf + 0x190, *app;
    uint32_t na = now[0], nb = before[0], cnt, k, kb;
    uint32_t verts, tex1, texn, off;
    uint8_t *outp;

    for (k = 0; k < na; k++) {
        const uint8_t *s = (const uint8_t *)(now + 1) + 8 * k;
        uint8_t *d = aa + 8 * k;
        U16(d, 0) = U16(s, 0);
        U16(d, 2) = U16(s, 6);
        U16(d, 4) = U16(s, 2);
        U16(d, 6) = (uint16_t)(((uint32_t)U16(s, 4) << ((U16(s, 6) - 1) & 31)) + U16(s, 2));
    }
    for (k = 0; k < nb; k++) {
        const uint8_t *s = (const uint8_t *)(before + 1) + 8 * k;
        uint8_t *d = bb + 8 * k;
        U16(d, 0) = U16(s, 0);
        U16(d, 2) = U16(s, 6);
        U16(d, 4) = U16(s, 2);
        U16(d, 6) = (uint16_t)(((uint32_t)U16(s, 4) << ((U16(s, 6) - 1) & 31)) + U16(s, 2));
    }
    cnt = na;
    app = aa + 8 * na;
    for (kb = 0; kb < nb; kb++) {
        const uint8_t *e = bb + 8 * kb;
        uint32_t key = U32(e, 0);
        uint16_t bs = U16(e, 4), be = U16(e, 6);
        uint32_t rem = cnt;
        uint8_t *ea = aa;

        if (rem == 0)
            continue;
        do {
            if (U32(ea, 0) == key) {
                uint16_t s = U16(ea, 4), en = U16(ea, 6);
                if (s != en && s < be && en > bs) {
                    if (bs <= s) {
                        if (be < en)
                            U16(ea, 4) = be;
                        else
                            U16(ea, 6) = s;
                    } else {
                        if (be < en) {
                            U32(app, 0) = U32(ea, 0);
                            U16(app, 4) = be;
                            U16(app, 6) = U16(ea, 6);
                            rem++;
                            cnt++;
                            app += 8;
                        }
                        U16(ea, 6) = bs;
                    }
                }
            }
            rem--;
            ea += 8;
        } while (rem != 0);
    }

    verts = G32(0x6bfac4);
    tex1 = G32(0x6c1e14);
    texn = G32(0x6c1eb0);
    G32(0x5d92c0) = 0;
    off = G32(0x6ce60c) != 0x6cd4a0 ? 0x84 : 0;
    outp = PTR(0x5d92c6);
    for (k = 0; k < cnt; k++) {
        const uint8_t *e = aa + 8 * k;
        uint32_t st = U16(e, 4), en = U16(e, 6), lvl, kind, sh, n, base, idx;
        int32_t stride, num;
        uint8_t *rec, *src;

        if (en == st)
            continue;
        G32(0x5d92c0)++;
        lvl = U16(e, 2);
        kind = U16(e, 0);
        sh = (lvl - 1) & 31;
        n = (en - st) >> sh;
        U16(outp, -2) = (uint16_t)kind;
        U16(outp, 4) = (uint16_t)lvl;
        U16(outp, 0) = (uint16_t)st;
        U16(outp, 2) = (uint16_t)n;
        rec = PTR(U32(G32(0x6bfac0) + 4 * kind, 0));
        if (lvl == 1) {
            base = U32(rec, 0x50);
            stride = I32(rec, 8);
        } else {
            base = U32(rec, 0x4c + 4 * lvl);
            stride = (int32_t)U16(U32(rec, 0x20 + 4 * lvl), 0) - 1;
        }
        idx = (st >> sh) * (uint32_t)stride;
        num = stride * (int32_t)n;
        src = PTR(base + 8 * idx);
        if (num > 0) {
            do {
                uint32_t t, tb, w;
                uint8_t *v, *tx;
                if (lvl == 1) {
                    t = U32(src, 4) << 4;
                    tb = tex1;
                } else {
                    t = U32(src, 4);
                    tb = texn;
                }
                tx = PTR(t + tb);
                w = U32(tx, 0);
                v = PTR(U16(src, 0) * 0x120 + off + verts);
                if (w & 0x80000000) {
                    const uint8_t *uv = tx + (lvl == 1 ? 0x10 : 0xc);
                    w &= 0x7fffffff;
                    U32(v, 0x140) = U32(uv, 0);
                    U32(v, 0x160) = U32(uv, 4);
                    U16(v, 0x180) = U16(uv, 8);
                    U16(v, 0x1a0) = U16(uv, 0xa);
                    U32(v, 0x124) = 0x8c808080;
                    U32(v, 0x144) = 0x808080;
                    U32(v, 0x164) = 0x808080;
                    U32(v, 0x184) = 0x808080;
                }
                {
                    uint32_t d4 = U32(tx, 4), d8 = U32(tx, 8);
                    U16(v, 0x60) = (uint16_t)d8;
                    U32(v, 0x20) = w;
                    U32(v, 0x40) = d4;
                    U16(v, 0x80) = (uint16_t)(d8 >> 16);
                }
                src += 8;
            } while (--num != 0);
        }
        outp += 8;
    }
}

/* 0x47b470(glow, ot_front, ot_back): the engine glow's packets when its
 * update flagged them (+0x1108, cleared). The colour: the glow's rgb
 * (+0x13f8..) plus a pulse (sine 0x4b24f8[+0x13fc] by the largest of them;
 * -0x96 while +0x13fd is odd), clamped, then brightened towards white by
 * twice its largest channel (alpha) and, unless 0x6ccd84 bit 0, scaled by
 * it. The faces of the glow model (*(+0x110c): +0x10 header, +0x28 faces,
 * packets +0x14 / +0x18 per buffer) from its vertex records +0x10fc
 * (0x18 bytes); faces off screen together are skipped; each goes to its
 * depth's ordering table entry (0x6cdd80, when 0x6cce84) or the front /
 * back one by the facing of face +0x1109 (0x4278f0, negated with 0x6cce94).
 * The faces listed at +0x13fe (to -1, at most 8) are dimmed by half.
 * Returns the ordering table entry chosen. */
uint32_t GAMEAPI car_glow_packets_draw(uint8_t *g, uint32_t ot_front, uint32_t ot_back)
{
    uint8_t *rec = PTR(U32(g, 0x110c));
    uint8_t *mdl = PTR(U32(rec, 0x10));
    uint32_t flip = G32(0x6cce94);
    uint32_t m, col, b0, b1, b2, a, big, inv, c0, c1, c2, C, ret, f0, pkts, fb;
    int32_t add, count, s, i;
    const uint8_t *faces;
    uint8_t *pk;
    float f;

    if (U8(g, 0x1108) == 0)
        return 0;
    U8(g, 0x1108) = 0;
    count = U16(mdl, 6);
    faces = PTR(U32(rec, 0x28));
    m = U8(g, 0x13f8) > U8(g, 0x13f9) ? U8(g, 0x13f8) : U8(g, 0x13f9);
    if ((int32_t)m <= (int32_t)U8(g, 0x13fa))
        m = U8(g, 0x13fa);
    add = (((int32_t)GS16(0x4b24f8 + 2 * U8(g, 0x13fc)) >> 9) * (int32_t)m) >> 8;
    if (U8(g, 0x13fd) != 0 && (U8(g, 0x13fd) & 1))
        add = -0x96;
    col = 0;
    for (s = 16; s >= 0; s -= 8) {
        int32_t c = (int32_t)((U32(g, 0x13f8) >> s) & 0xff) + add;
        if (c < 0)
            c = 0;
        else if (c > 0xff)
            c = 0xff;
        col = col << 8 | (uint32_t)c;
    }
    b0 = col & 0xff;
    b1 = col >> 8 & 0xff;
    b2 = col >> 16 & 0xff;
    big = b0 > b1 ? b0 : b1;
    if (!((int32_t)big > (int32_t)b2))
        big = b2;
    a = 2 * big >= 0xff ? 0xff : 2 * big;
    inv = (0xff - a) & 0xff;
    c0 = (b0 + inv) & 0xff;
    c1 = (b1 + inv) & 0xff;
    c2 = (b2 + inv) & 0xff;
    if (G8(0x6ccd84) & 1)
        C = a << 24 | c2 << 16 | c1 << 8 | c0;
    else
        C = a << 24 | (c2 * a >> 8) << 16 | (c1 * a >> 8) << 8 | (c0 * a >> 8);

    pkts = G32(0x6ce60c) == 0x6cd4a0 ? U32(rec, 0x14) : U32(rec, 0x18);
    ret = ot_front;
    f0 = U32(faces, U8(g, 0x1109) * 8);
    f = ((pq_facing_fn)0x4278f0)(A(g + 0x10fc + (f0 & 0xff) * 0x18), A(g + 0x10fc + (f0 >> 8 & 0xff) * 0x18),
                                 A(g + 0x10fc + (f0 >> 16 & 0xff) * 0x18));
    memcpy(&fb, &f, 4);
    if (flip)
        fb ^= 0x80000000;
    if (!pq_below(fb, G32(0x4a3318)))
        ret = ot_back;

    pk = PTR(pkts);
    for (; count > 0; count--, faces += 8, pk += 0x84) {
        uint32_t fw = U32(faces, 0), b3 = fw >> 24, l70[6], l58[6], l88[6], l40[6];
        uint8_t code;

        memcpy(l70, g + 0x10fc + (fw & 0xff) * 0x18, 24);
        memcpy(l58, g + 0x10fc + (fw >> 8 & 0xff) * 0x18, 24);
        memcpy(l88, g + 0x10fc + (fw >> 16 & 0xff) * 0x18, 24);
        memcpy(l40, g + 0x10fc + (b3 ? b3 : fw >> 16 & 0xff) * 0x18, 24);
        if ((l40[0] & l88[0] & l58[0] & l70[0]) != 0)
            continue;
        memcpy(pk + 8, l70, 24);
        memcpy(pk + 0x28, l58, 24);
        memcpy(pk + 0x48, l88, 24);
        code = 0x9e;
        if (b3) {
            memcpy(pk + 0x68, l40, 24);
            code = 0x8e;
        }
        U32(pk, 4) = C;
        U32(pk, 0x24) = C;
        U32(pk, 0x44) = C;
        U32(pk, 0x64) = C;
        U8(pk, 7) = code;
        U16(pk, 0x22) = (uint16_t)(C >> 24);
        {
            uint32_t o;
            if (G32(0x6cce84)) {
                int32_t z = pq_fist(U32(pk, 0xc)) >> 5;
                if (z > 0x100 && (z = z / 2 + 0x80) > 0x180 && (z = z / 2 + 0xc0) > 0x1ff) {
                    o = G32(0x6cdd80 + 4 * 0x1ff);
                } else {
                    if (z < 1)
                        z = 1;
                    o = G32(0x6cdd80 + 4 * z);
                }
            } else {
                o = ret;
            }
            pq_ot_link(o, pk);
        }
    }
    for (i = 0; i < 8; i++) {
        int32_t idx = S8(g, 0x13fe + i);
        uint8_t *q;
        if (idx == -1)
            break;
        q = PTR(pkts + idx * 0x84);
        U8(q, 4) >>= 1;
        U8(q, 5) >>= 1;
        U8(q, 6) >>= 1;
    }
    return ret;
}

/* 0x47bc80(glow): the glow texture's frame +0xa steps 0..4 and the texture
 * (0x6c1e14 entry 0x91 + frame + *(0x6c1eb0) +0x2e) goes into the uv /
 * clut / tpage words of the faces listed at +0x2f6 (to -1, at most 8) of
 * the current packet buffer (*(+4) +0x14 / +0x18). */
void GAMEAPI glow_texture_cycle(uint8_t *g)
{
    int32_t f = S16(g, 0xa) + 1, i;
    uint8_t *t, *p;

    if (f >= 5)
        f = 0;
    U16(g, 0xa) = (uint16_t)f;
    t = PTR((((uint32_t)U16(G32(0x6c1eb0), 0x2e) + (int16_t)f + 0x91) << 4) + G32(0x6c1e14));
    p = PTR(G32(0x6ce60c) == 0x6cd4a0 ? U32(U32(g, 4), 0x14) : U32(U32(g, 4), 0x18));
    for (i = 0; i < 8; i++) {
        int32_t idx = S8(g, 0x2f6 + i);
        uint8_t *q;
        if (idx == -1)
            return;
        q = p + idx * 0x84;
        U16(q, 0x42) = U16(t, 6);
        U16(q, 0x20) = U16(t, 0);
        U16(q, 0x40) = U16(t, 4);
        U16(q, 0x60) = U16(t, 8);
        U16(q, 0x80) = U16(t, 0xa);
    }
}

/* 0x47bd20: 0x47bc80 for every car's glow (slot car + 0x1108). */
void GAMEAPI cars_glow_texture_cycle(void)
{
    int32_t i;
    uint32_t s = 0x6bfb80;

    if (U8(G32(0x4b1960), 0x54d) == 0)
        return;
    for (i = 0; i < (int32_t)U8(G32(0x4b1960), 0x54d); i++, s += 0x274)
        glow_texture_cycle(PTR(G32(s) + 0x1108));
}

/* 0x47be70(x, y, z, count, spread, colour): `count` debris pieces from
 * the ring *0x5d9290 (64 of 0x180, next 0x5d928c; loop index 0x5d9288):
 * unlinked if live (+0x2c, 0x4612a0), colour +0x2d, position a random
 * offset (the 0x4b11d8 Park-Miller generator, 3 draws kept at 0x5d9294 /
 * 98 / 9c, folded to +-0x3fff) << spread / 4 from (x, y, z), direction
 * +0x1c normalised (0x480c70), +0x2c = 0xff, life +0x28 = spread * 5,
 * +0x2e = 10, linked at its position (0x460ff0). */
void GAMEAPI debris_burst(int32_t x, int32_t y, int32_t z, uint32_t count, uint32_t spread, uint32_t colour)
{
    uint32_t sp = spread & 0xff, sh = (sp >> 2) & 31, life = sp * 5;

#define PQ_D() PTR(G32(0x5d928c) * 0x180 + G32(0x5d9290))
    G32(0x5d9288) = 0;
    if (count == 0)
        return;
    do {
        uint8_t *d = PQ_D();
        uint32_t s, r1, r2, r3, k;

        if (U8(d, 0x2c) != 0)
            pq_grid_unlink2(d);
        U8(PQ_D(), 0x2d) = (uint8_t)colour;
        s = (G32(0x4b11d8) * 16807) & 0x7fffffff;
        r1 = s >> 16;
        G32(0x5d9294) = r1;
        s = (s * 16807) & 0x7fffffff;
        r2 = s >> 16;
        G32(0x5d9298) = r2;
        s = (s * 16807) & 0x7fffffff;
        G32(0x4b11d8) = s;
        r3 = s >> 16;
        G32(0x5d929c) = r3;
        if ((int32_t)r1 > 0x3fff) {
            r1 = 0u - r1;
            G32(0x5d9294) = r1;
        }
        if ((int32_t)r2 > 0x3fff) {
            r2 = 0u - r2;
            G32(0x5d9298) = r2;
        }
        if ((int32_t)r3 > 0x3fff) {
            r3 = 0u - r3;
            G32(0x5d929c) = r3;
        }
        U32(PQ_D(), 0x10) = (r1 << sh) + (uint32_t)x;
        U32(PQ_D(), 0x14) = (G32(0x5d9298) << sh) + (uint32_t)y;
        U32(PQ_D(), 0x18) = (G32(0x5d929c) << sh) + (uint32_t)z;
        U32(PQ_D(), 0x1c) = U32(PQ_D(), 0x10) - (uint32_t)x;
        U32(PQ_D(), 0x20) = U32(PQ_D(), 0x14) - (uint32_t)y;
        U32(PQ_D(), 0x24) = U32(PQ_D(), 0x18) - (uint32_t)z;
        pq_vec_normalise(PQ_D() + 0x1c);
        U8(PQ_D(), 0x2c) = 0xff;
        U32(PQ_D(), 0x28) = life;
        U16(PQ_D(), 0x2e) = 10;
        d = PQ_D();
        pq_grid_link2_at(d, U32(d, 0x10), U32(d, 0x14), U32(d, 0x18));
        k = G32(0x5d928c) + 1;
        G32(0x5d928c) = k;
        if (k == 0x40)
            G32(0x5d928c) = 0;
        G32(0x5d9288)++;
    } while (G32(0x5d9288) < count);
#undef PQ_D
}

/* 0x47c110(obj): a track object breaks: for each of its kind's (+0x190,
 * 0x6bfb14 records of 16, +6 count) pieces, the point (model +0x34
 * offset, 0x14 apart: x | y << 16, z) at the object's position +0x34; a
 * piece of material 2 (*(+0x194)[i] bits 24..27) bursts into debris
 * (0x47be70, byte 3 of 0x6bfb10[+0x18e + i]), and every piece gets the
 * effects 0x42aa30 (0x24, 0xe); then unlinked (0x4611d0), its triggers
 * (0x4427a0), state +0x19a = 5. Returns 1. */
int32_t GAMEAPI object_debris_break(uint8_t *obj)
{
    uint8_t *t = PTR(U16(obj, 0x190) * 16 + G32(0x6bfb14));
    uint8_t *m = PTR(U32(obj, 0x10));
    const uint8_t *pt = m + U16(m, 0x34);
    uint32_t i = 0;

    if (U8(t, 6) != 0) {
        do {
            uint32_t b = U8(G32(0x6bfb10) + 8 * (U16(obj, 0x18e) + i), 3);
            uint32_t w = U32(pt, 0);
            int32_t pos[3];
            pos[0] = (int32_t)((w << 16) + U32(obj, 0x34));
            pos[1] = (int32_t)((w & 0xffff0000) + U32(obj, 0x38));
            pos[2] = (int32_t)((U32(pt, 4) << 16) + U32(obj, 0x3c));
            if ((U32(U32(obj, 0x194), 4 * i) & 0xf000000) == 0x2000000)
                debris_burst(pos[0], pos[1], pos[2], b, b >> 1, b);
            pq_effects(pos, obj + 0x40, 1, 0x24, 0xe);
            i++;
            pt += 0x14;
        } while (i < U8(t, 6));
    }
    pq_grid_unlink(obj);
    pq_triggers(obj);
    U8(obj, 0x19a) = 5;
    return 1;
}

/* 0x47c420(runs): the light flags per strip of the visible runs. Strip
 * offsets 0x5d91c0[i] (count + 1 each, then the total); per strip at
 * 0x5d7540: up to three lights (10 bits each) from the strip's cell of
 * the run kind's light grid 0x5d7de0 (32 bytes per 16 strips, 8 words of
 * light << 22 | end << 11 | start), the top bit when one is moving (light
 * +0x12) or a dynamic light 0x5d71e0 (to 0x5d7fe4: kind | start << 7 |
 * end << 18) covers it; then the strips of the parts that came into view
 * (0x5d92c4, count 0x5d92c0) also get the top bit. */
void GAMEAPI track_runs_light_flags(uint32_t *list)
{
    const uint8_t *ent = (const uint8_t *)(list + 1);
    uint32_t acc = 0, i = 0, k, lim, newc;

    if (list[0] != 0) {
        do {
            G32(0x5d91c0 + 4 * i) = acc;
            acc += (uint32_t)U16(ent, 8 * i + 4) + 1;
            i++;
        } while (i < list[0]);
    }
    G32(0x5d91c0 + 4 * i) = acc;
    lim = 0x5d71e0 + 4 * G32(0x5d7fe4);
    for (k = 0; k < list[0]; k++) {
        const uint8_t *e = ent + 8 * k;
        uint32_t sh = (uint32_t)(U16(e, 6) - 1) & 31;
        uint32_t off = G32(0x5d91c0 + 4 * k);
        uint32_t kind = U16(e, 0), start = U16(e, 2);
        uint32_t tab = G32(0x5d7de0 + 4 * kind);
        uint32_t rows = G32(0x5d91c0 + 4 * k + 4) - off;
        uint32_t j = 0, *out;

        if (rows == 0)
            continue;
        out = (uint32_t *)(uintptr_t)(0x5d7540 + 4 * off);
        do {
            uint32_t pos = (j << sh) + start, w = 0, bit = 0, b;
            const uint32_t *q = (const uint32_t *)(uintptr_t)(tab + (pos >> 4) * 0x20);
            for (b = 0; b < 8; b++, q++) {
                uint32_t v = *q;
                if (v != 0 && pos < (v >> 11 & 0x7ff) && pos >= (v & 0x7ff)) {
                    uint32_t idx = v >> 22;
                    if (G16(0x5d8012 + idx * 0x40) != 0)
                        w |= 0x80000000;
                    w |= idx << (bit & 31);
                    bit += 10;
                    if (bit > 0x14)
                        break;
                }
            }
            if (!(w & 0x80000000)) {
                uint32_t c;
                for (c = 0x5d71e0; c < lim; c += 4) {
                    uint32_t v = G32(c);
                    if (v != 0 && (v & 0x7f) == kind && pos < (v >> 18 & 0x7ff) && pos >= (v >> 7 & 0x7ff)) {
                        w |= 0x80000000;
                        break;
                    }
                }
            }
            *out++ = w;
            j++;
        } while (j < rows);
    }

    newc = G32(0x5d92c0);
    for (k = 0; k < list[0]; k++) {
        const uint8_t *e = ent + 8 * k;
        uint32_t lvl1 = (uint32_t)U16(e, 6) - 1, sh = lvl1 & 31;
        uint32_t end = ((uint32_t)U16(e, 4) << sh) + U16(e, 2);
        uint32_t off = G32(0x5d91c0 + 4 * k);
        uint32_t st = U16(e, 2), kind = U16(e, 0), r;
        const uint8_t *q = PTR(0x5d92c6);

        for (r = 0; r < newc; r++, q += 8) {
            uint32_t r0, e2, lo, hi;
            if (U16(q, 4) != lvl1 + 1 || U16(q, -2) != kind)
                continue;
            r0 = U16(q, 0);
            if (r0 > end)
                continue;
            e2 = ((uint32_t)U16(q, 2) << sh) + r0;
            if (st > e2)
                continue;
            lo = r0 > st ? r0 : st;
            hi = e2 < end ? e2 : end;
            hi = ((hi - st) >> sh) + off;
            lo = ((lo - st) >> sh) + off;
            if (lo <= hi) {
                uint32_t x = hi - lo + 1, *p = (uint32_t *)(uintptr_t)(0x5d7540 + 4 * lo);
                do
                    *p++ |= 0x80000000;
                while (--x != 0);
            }
        }
    }
}

/* The point-light state of 0x47c730: flags (bit per light set), the 3x3
 * colour matrix handed to 0x428940 (rows r, g, b; a column per light) and
 * each light's x | y << 16, z and range shift. */
struct pq_lights {
    uint32_t flags;
    int16_t lm[9];
    uint32_t xy[3], z[3];
    uint8_t sh[3];
};

/* A light record 0x5d8000 + idx * 0x40 (x, y, z 16.16, +0xc colour,
 * +0x10 shift) into light j. */
static void pq_light_load(struct pq_lights *L, int j, uint32_t idx)
{
    const uint8_t *r = PTR(0x5d8000 + idx * 0x40);
    uint32_t c, v;

    L->xy[j] = (U32(r, 0) >> 16 & 0xffff) | (U32(r, 4) >> 16) << 16;
    L->z[j] = U32(r, 8) >> 16;
    L->sh[j] = U8(r, 0x10);
    c = U32(r, 0xc);
    v = (c & 0xff) << 5;
    L->lm[j] = (int16_t)(v >= 0xfff ? 0xfff : v);
    v = (c >> 8 & 0xff) << 5;
    L->lm[3 + j] = (int16_t)(v >= 0xfff ? 0xfff : v);
    v = (c >> 16 & 0xff) << 5;
    L->lm[6 + j] = (int16_t)(v >= 0xfff ? 0xfff : v);
}

/* The lights changed from `prev` to `w`: each changed one loaded or
 * switched off, then the colour matrix loaded (0x428940). */
static void pq_lights_update(struct pq_lights *L, uint32_t w, uint32_t prev)
{
    uint32_t ch = w ^ prev;

    if (ch & 0x3ff) {
        if ((w & 0x3ff) == 0) {
            L->flags &= 6;
        } else {
            pq_light_load(L, 0, w & 0x3ff);
            L->flags |= 1;
        }
    }
    if (ch & 0xffc00) {
        if ((w & 0xffc00) == 0) {
            L->flags &= 5;
        } else {
            pq_light_load(L, 1, w >> 10 & 0x3ff);
            L->flags |= 2;
        }
    }
    if (ch & 0x3ff00000) {
        if ((w & 0x3ff00000) == 0) {
            L->flags &= 3;
        } else {
            pq_light_load(L, 2, w >> 20 & 0x3ff);
            L->flags |= 4;
        }
    }
    pq_light_matrix_load(L->lm);
}

/* Light j's level at a vertex (x | y << 16, z): the GTE squares
 * (0x4282d0) of the 16-bit differences (y borrows from x, sic) summed and
 * shifted; past 0x3ff none, else the reciprocal table entry. */
static int32_t pq_light_level(const struct pq_lights *L, int j, uint32_t vxy, uint32_t vz, uint32_t tab)
{
    uint32_t d = vxy - L->xy[j];
    int32_t s;

    G16(0x6ccc88) = (uint16_t)d;
    G16(0x6ccc8c) = (uint16_t)(d >> 16);
    G16(0x6ccc90) = (uint16_t)(vz - L->z[j]);
    pq_square_ir();
    s = (GI32(0x6cccc8) + GI32(0x6ccccc) + GI32(0x6cccd0)) >> (L->sh[j] & 31);
    return s > 0x3ff ? 0 : GI32(tab + 4 * s);
}

/* A vertex colour: the palette entry of the vertex's colour index, lit by
 * the three light levels through the GTE light colour (0x427100) when any
 * is non-zero. */
static void pq_vertex_colour(uint32_t *out, uint32_t pal, uint32_t vz, int32_t i1, int32_t i2, int32_t i3)
{
    uint32_t c, r, g, b;

    *out = U32(pal, 4 * (vz >> 22));
    if ((i1 | i2 | i3) == 0)
        return;
    G16(0x6ccc90) = (uint16_t)i3;
    G16(0x6ccc88) = (uint16_t)i1;
    G16(0x6ccc8c) = (uint16_t)i2;
    c = *out;
    r = (c & 0xff) << 4 | 0xf;
    g = ((c & 0xff00) | 0xf0) >> 4;
    b = ((c & 0xff0000) | 0xf000) >> 12;
    G32(0x6ccc14) = r;
    G32(0x6ccc18) = g;
    G32(0x6ccc1c) = b;
    G32(0x6cce38) = pq_fild((int32_t)b);
    G32(0x6cce34) = pq_fild((int32_t)g);
    G32(0x6cce30) = pq_fild((int32_t)r);
    pq_light_colour();
    *out = G32(0x6cccbc);
}

/* 0x47c730(runs, buffer): the vertex colours of the visible runs. Per run
 * (kind, start, rows, level), per row of vertices (8 bytes: x | y << 16,
 * z | flags | colour << 22; +0x18 strides at level 1, else the level's
 * step list *(rec +0x20 + 4 * level)) whose strip flags (0x5d7540, from
 * 0x47c420) or a forced redraw (0x4b2c60 / 0x4d58e8, 0x4d58f4 / 0x4d58f8
 * differ) say so: each vertex's palette colour (0x486350) lit by the
 * strip's lights (0x5d8000), into a two-row ring; then the four corner
 * colours of the quads between this row and the previous (packets
 * *(*0x6c1e30 + 4) + index * 0x120, +0x84 for buffer 1; corner order by
 * the vertex's bit 18). 0x6ccc7c = 0x8cffffff. */
void GAMEAPI track_runs_light(uint32_t *list, int32_t mode)
{
    uint32_t ring[0xa0];
    uint16_t steps[0x60];
    struct pq_lights L;
    uint32_t n_ent = list[0], e_i;
    const uint8_t *ent = (const uint8_t *)(list + 1);
    uint32_t cur_lights = 0, prev_lights = 0, lit = 0;
    uint32_t pal, tab, pk_base, force;

    memset(&L, 0, sizeof(L)); /* sic: the original's matrix starts as stack leftovers (lights off: no effect) */
    pal = pq_palette();
    pk_base = U32(G32(0x6c1e30), 4);
    tab = G32(0x4d4b9c);
    force = (G32(0x4b2c60) != G32(0x4d58e8) || G32(0x4d58f4) != G32(0x4d58f8)) ? 1 : 0;
    if (mode == 1)
        pk_base += 0x84;
    G32(0x6ccc7c) = 0x8cffffff;
    for (e_i = 0; e_i < n_ent; e_i++, ent += 8) {
        uint32_t off = G32(0x5d91c0 + 4 * e_i);
        int32_t rows = U16(ent, 4);
        uint32_t start = U16(ent, 2), lvl = U16(ent, 6), kind = U16(ent, 0);
        uint8_t *rec = PTR(U32(U32(G32(0x6c1e30), 0), 4 * kind));
        const uint32_t *visp = (const uint32_t *)(uintptr_t)(0x5d7540 + 4 * off);
        uint32_t n = U32(rec, 8), np1 = n + 1, la4 = np1;
        uint32_t vp = U32(rec, 0x20) + 8 * (np1 * start);
        uint32_t *rp = ring, *ring_end;
        uint32_t cnt, cm1, pp, w;
        int32_t skip, l30, l90;

        if (lvl == 1) {
            uint32_t k;
            skip = 0;
            if ((int32_t)np1 > 0)
                for (k = 0; k < np1; k++)
                    steps[k] = 8;
            pp = U32(rec, 0x50) + 8 * (n * start);
            cnt = np1;
            cm1 = n;
        } else {
            uint32_t e8 = 8 * np1;
            const uint8_t *sl;
            int32_t nd;
            skip = (int32_t)((e8 << ((lvl - 1) & 31)) - e8);
            sl = PTR(U32(rec, 0x20 + 4 * lvl));
            cnt = U32(sl, 0) & 0xffff;
            cm1 = cnt - 1;
            nd = ((int32_t)cm1 >> 1) + 1;
            /* the original's step list is 0x54 bytes of its frame: real lists fit */
            if (nd != 0)
                memcpy(steps, sl + 4, (uint32_t)nd > sizeof(steps) / 4 ? sizeof(steps) : 4 * (uint32_t)nd);
            pp = U32(rec, 0x4c + 4 * lvl) + 8 * (uint32_t)(((int32_t)start >> ((lvl - 1) & 31)) * (int32_t)cm1);
        }
        l30 = (int32_t)cnt + 1;
        l90 = -(int32_t)cm1;
        ring_end = ring + 2 * cnt;
        if (lit)
            prev_lights = cur_lights;
        w = *visp++;
        lit = (w & 0x80000000) | force;
        if (!lit) {
            int32_t t = l30;
            vp += (uint32_t)skip + 8 * la4;
            l30 = l90;
            l90 = t;
        } else {
            int32_t i1 = 0, i2 = 0, i3 = 0;
            cur_lights = w;
            if (prev_lights != w)
                pq_lights_update(&L, w, prev_lights);
            if (cnt != 0) {
                uint32_t k = cnt;
                const uint16_t *sp = steps + cnt;
                do {
                    uint32_t vz = U32(vp, 4);
                    if (L.flags) {
                        uint32_t vxy = U32(vp, 0);
                        if (L.flags & 1)
                            i1 = pq_light_level(&L, 0, vxy, vz, tab);
                        if (L.flags & 2)
                            i2 = pq_light_level(&L, 1, vxy, vz, tab);
                        if (L.flags & 4)
                            i3 = pq_light_level(&L, 2, vxy, vz, tab);
                    }
                    pq_vertex_colour(rp, pal, vz, i1, i2, i3);
                    sp--;
                    rp++;
                    vp += *sp;
                } while (--k != 0);
            }
            vp += (uint32_t)skip;
        }
        if (rows <= 0)
            continue;
        do {
            uint32_t plit;
            int32_t i1 = 0, i2 = 0, i3 = 0;

            if (lit)
                prev_lights = cur_lights;
            w = *visp++;
            plit = lit;
            lit = (w & 0x80000000) | force;
            if ((plit | lit) == 0) {
                vp += (uint32_t)skip + 8 * la4;
                pp += 8 * cnt - 8;
                continue;
            }
            cur_lights = w;
            if (lit && prev_lights != w)
                pq_lights_update(&L, w, prev_lights);
            if (cnt != 0) {
                uint32_t rem = cnt;
                const uint16_t *sp = steps + cnt;
                do {
                    uint32_t vz = U32(vp, 4);
                    if (lit) {
                        if (L.flags) {
                            uint32_t vxy = U32(vp, 0);
                            if (L.flags & 1)
                                i1 = pq_light_level(&L, 0, vxy, vz, tab);
                            if (L.flags & 2)
                                i2 = pq_light_level(&L, 1, vxy, vz, tab);
                            if (L.flags & 4)
                                i3 = pq_light_level(&L, 2, vxy, vz, tab);
                        }
                        pq_vertex_colour(rp, pal, vz, i1, i2, i3);
                    }
                    if (rem != cnt) {
                        uint8_t *pk = PTR(U16(pp - 8, 0) * 0x120 + pk_base);
                        if (vz & 0x40000) {
                            if (plit) {
                                U32(pk, 4) = rp[-l30];
                                U32(pk, 0x24) = rp[-l30 + 1];
                            }
                            if (lit) {
                                U32(pk, 0x44) = rp[-1];
                                U32(pk, 0x64) = rp[0];
                            }
                        } else {
                            if (plit) {
                                U32(pk, 4) = rp[-l30 + 1];
                                U32(pk, 0x24) = rp[0];
                            }
                            if (lit) {
                                U32(pk, 0x44) = rp[-l30];
                                U32(pk, 0x64) = rp[-1];
                            }
                        }
                    }
                    pp += 8;
                    sp--;
                    vp += *sp;
                    rp++;
                } while (--rem != 0);
            }
            pp -= 8;
            {
                int32_t t = l30;
                l30 = l90;
                l90 = t;
            }
            if (rp >= ring_end)
                rp = ring;
            vp += (uint32_t)skip;
        } while (--rows != 0);
    }
}
