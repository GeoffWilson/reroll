/* Group WA: the bodies of the original functions our wrappers in
 * src/widescreen.c still ran through trampolines (Phase 1's last step,
 * docs/native/plan64.md). The wrappers stay hooked at the addresses and now
 * call these instead:
 *
 *  - 0x417ed0 frustum_body: the frustum side planes 0x4b7fa8.. and the
 *    "view leaves the screen" flag 0x4b7fec (x87, atan / sin / cos of the
 *    C runtime by address);
 *  - 0x41f2d0 viewport_body: the projection centre, the D3D viewport
 *    (0x41f210) and the clip rectangle 0x6ccd64..;
 *  - 0x459b20 objects_body: the object pass over the cells' chains 0x5d7040
 *    (cars collected into 0x6ce640, the rest drawn lit, triggers kept);
 *  - 0x46a9e0 countdown_body: the countdown numeral's quad;
 *  - 0x47a030 visibility_body: the track's visibility list manager;
 *  - 0x485140 flake_spawn_body: (re)spawns screen flake / lens drop i;
 *  - 0x490430 sparkle_body: the respawn / snatch sparkle squares.
 *
 * Float steps are the original's x87 instructions (memory operands, the
 * same single / double precision stores in between). Everything outside
 * the group is called by address. */
#include "../runtime.h"
#include "physics.h"

#include <string.h>

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define GS16(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

typedef uint32_t(GAMEAPI *wa_fn1)(uint32_t a);
typedef uint32_t(GAMEAPI *wa_fn2)(uint32_t a, uint32_t b);
typedef uint32_t(GAMEAPI *wa_fn4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);

#define CALL1(a, x) (((wa_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((wa_fn2)(a))(A(x), A(y)))
#define CALL4(a, x, y, z, w) (((wa_fn4)(a))(A(x), A(y), A(z), A(w)))

#define d3d_viewport_set(x, y, w, h) CALL4(0x41f210, x, y, w, h)
#define screen_offset_get(px, py) CALL2(0x426240, px, py)
#define light_colours_from_ids(ids, out) CALL2(0x459cb0, ids, out)
#define model_draw_lit(o, lights) CALL2(0x45a260, o, lights)
#define grid_record(link) CALL1(0x4613e0, link)
#define track_runs_visible(cell, out) CALL2(0x47a0d0, cell, out)
#define track_runs_diff_texture(now, before) CALL2(0x47a500, now, before)
#define track_runs_light_flags(list) CALL1(0x47c420, list)
#define track_runs_light(list, mode) CALL2(0x47c730, list, mode)
#define CRT_ATAN 0x497c24u
#define CRT_SIN 0x497b74u
#define CRT_COS 0x497ac4u

#define g_race G32(0x4b1960)
#define g_flakes G32(0x4d5898) /* 32 records of 0xa0 */
#define g_view G32(0x6ce60c)
#define g_clip_left G32(0x6ccd64)
#define g_clip_top G32(0x6ccd6c)
#define K_TWO G32(0x4a3314)  /* 2.0f */
#define K_ZERO G32(0x4a3318) /* 0.0f */

/* This group's functions (also in build/groups/wa_protos.txt for physics.h). */
void GAMEAPI frustum_body(float x, float y, float w, float h, float focal); /* 0x417ed0 */
void GAMEAPI viewport_body(int32_t x, int32_t y, int32_t w, int32_t h); /* 0x41f2d0 */
void GAMEAPI objects_body(void); /* 0x459b20 */
void GAMEAPI countdown_body(void); /* 0x46a9e0 */
uint32_t *GAMEAPI visibility_body(uint8_t *cell); /* 0x47a030 */
void GAMEAPI flake_spawn_body(int32_t i); /* 0x485140 */
void GAMEAPI sparkle_body(uint8_t *camera); /* 0x490430 */

/* ---- x87 helpers: the original's instructions ---- */

/* fild dword v; fstp dword. */
static inline uint32_t wa_i2f(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fld a; fadd b; fstp. */
static inline uint32_t wa_add(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fdiv b; fstp. */
static inline uint32_t wa_div(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fdiv b; fmul c; fstp. */
static inline uint32_t wa_div_mul(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfmuls %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fdiv b; fstp qword. */
static inline uint64_t wa_div_d(uint32_t a, uint32_t b)
{
    uint64_t r;
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfstpl %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld dword a; fstp qword. */
static inline uint64_t wa_f2d(uint32_t a)
{
    uint64_t r;
    __asm__ volatile("flds %1\n\tfstpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld a; fchs; fstp. */
static inline uint32_t wa_neg(uint32_t a)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfchs\n\tfstps %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld a; fcomp b; fnstsw ax: ah (C0 = 1, C2 = 4, C3 = 0x40). */
static inline uint32_t wa_cmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return sw >> 8;
}

/* A C runtime function of one double (pushed as two words), its st(0)
 * result stored as a float. */
static inline uint32_t wa_crt_d1(uint32_t fn, uint64_t d)
{
    uint32_t r, lo = (uint32_t)d, hi = (uint32_t)(d >> 32);
    __asm__ volatile("pushl %[hi]\n\t"
                     "pushl %[lo]\n\t"
                     "call *%[fn]\n\t"
                     "addl $8,%%esp\n\t"
                     "fstps %[r]"
                     : [r] "=m"(r)
                     : [fn] "r"(fn), [lo] "r"(lo), [hi] "r"(hi)
                     : "eax", "ecx", "edx", "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)",
                       "st(6)", "st(7)");
    return r;
}

static inline uint32_t fbits(float f)
{
    uint32_t r;
    memcpy(&r, &f, 4);
    return r;
}

/* The game's random step (seed * 16807 mod 2^31), as at 0x4b2c50 and
 * 0x4b2d1c. */
static inline uint32_t wa_next(uint32_t s)
{
    return (s * 16807u) & 0x7fffffffu;
}

/* ===== 0x417ed0(x, y, w, h, H): the frustum side planes of a view.
 * 0x4b7fec = 0x40 unless the view lies within the screen; with
 * s = max(w, h) / H the half angles atan(2H / s / (w / s)) and
 * atan(2H / s / (h / s)) give the planes (sin, 0, cos, 0.001) /
 * (-sin, 0, cos, 0.001) at 0x4b7fa8 and (0, sin, cos, 0.001) /
 * (0, -sin, cos, 0.001) at 0x4b7fc8. */
void GAMEAPI frustum_body(float xf, float yf, float wf, float hf, float focalf)
{
    uint32_t x = fbits(xf), y = fbits(yf), w = fbits(wf), h = fbits(hf), focal = fbits(focalf);
    uint32_t right = wa_add(x, w), bottom = wa_add(y, h), m, s, ws, hs, d, a, sn, cs, nsn;
    uint64_t ad;
    uint32_t off = 0x40;

    if (!(wa_cmp(x, K_ZERO) & 1) && !(wa_cmp(y, K_ZERO) & 1) &&
        (wa_cmp(right, wa_i2f(GI32(0x4acc38))) & 0x41) &&
        (wa_cmp(bottom, wa_i2f(GI32(0x4acc3c))) & 0x41))
        off = 0;
    G32(0x4b7fec) = off;
    m = (wa_cmp(w, h) & 0x41) ? h : w;
    s = wa_div(m, focal);
    ws = wa_div(w, s);
    hs = wa_div(h, s);
    d = wa_div_mul(K_TWO, s, focal);

    a = wa_crt_d1(CRT_ATAN, wa_div_d(d, ws));
    ad = wa_f2d(a);
    sn = wa_crt_d1(CRT_SIN, ad);
    cs = wa_crt_d1(CRT_COS, ad);
    nsn = wa_neg(sn);
    G32(0x4b7fac) = sn;
    G32(0x4b7fb0) = 0;
    G32(0x4b7fb4) = cs;
    G32(0x4b7fa8) = 0x3a83126f; /* 0.001f */
    G32(0x4b7fbc) = nsn;
    G32(0x4b7fc0) = 0;
    G32(0x4b7fc4) = cs;
    G32(0x4b7fb8) = 0x3a83126f;

    a = wa_crt_d1(CRT_ATAN, wa_div_d(d, hs));
    ad = wa_f2d(a);
    sn = wa_crt_d1(CRT_SIN, ad);
    cs = wa_crt_d1(CRT_COS, ad);
    nsn = wa_neg(sn);
    G32(0x4b7fcc) = 0;
    G32(0x4b7fdc) = 0;
    G32(0x4b7fd0) = sn;
    G32(0x4b7fd4) = cs;
    G32(0x4b7fc8) = 0x3a83126f;
    G32(0x4b7fe0) = nsn;
    G32(0x4b7fe4) = cs;
    G32(0x4b7fd8) = 0x3a83126f;
}

/* ===== 0x41f2d0(x, y, w, h): a view's half size 0x6ccd4c / 0x6ccd50 and
 * centre 0x6ccd5c / 0x6ccd60, the D3D viewport (0x41f210) of the rectangle
 * clipped to the screen, and the clip rectangle 0x6ccd64 (left), 0x6ccd68
 * (right), 0x6ccd6c (top), 0x6ccd70 (bottom), with copies of left / top at
 * 0x6ccd54 / 0x6ccd58. */
void GAMEAPI viewport_body(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t cw = w, ch = h;

    G32(0x6ccd4c) = wa_div(wa_i2f(w), K_TWO);
    G32(0x6ccd50) = wa_div(wa_i2f(h), K_TWO);
    G32(0x6ccd5c) = wa_add(G32(0x6ccd4c), wa_i2f(x));
    G32(0x6ccd60) = wa_add(G32(0x6ccd50), wa_i2f(y));
    if (x < 0) {
        cw = w + x;
        x = 0;
    }
    if (y < 0) {
        ch = h + y;
        y = 0;
    }
    if (x + cw > GI32(0x4acc38))
        cw = GI32(0x4acc38) - x;
    if (y + ch > GI32(0x4acc3c))
        ch = GI32(0x4acc3c) - y;
    d3d_viewport_set(x, y, cw, ch);
    G32(0x6ccd64) = wa_i2f(x);
    G32(0x6ccd6c) = wa_i2f(y);
    G32(0x6ccd68) = wa_add(G32(0x6ccd64), wa_i2f(cw));
    G32(0x6ccd54) = G32(0x6ccd64);
    G32(0x6ccd58) = G32(0x6ccd6c);
    G32(0x6ccd70) = wa_add(G32(0x6ccd6c), wa_i2f(ch));
}

/* ===== 0x459b20: the objects of the cells the view's list reaches (the
 * chains 0x5d7040, 0 ending, light ids per chain 0x5d6ac0): every drawable
 * (0x400) object of each chain; cars (kind 3) are only collected into
 * 0x6ce640 (0 ending) with their chain's light ids in 0x6ce620, the others
 * are drawn lit (0x45a260, with the light records of the chain's ids from
 * 0x459cb0, fetched when the ids change), and those with a trigger
 * (+0x16a) are added to 0x6b9ba0 (count 0x6b9cb8, the count before at
 * 0x6b9b94) unless 0x4d1198. Sets the view's packet buffer (0x6c1e18 /
 * 0x6c1e24) first. */
void GAMEAPI objects_body(void)
{
    uint32_t lights[3] = {0, 0, 0};
    uint32_t count = G32(0x6b9cb8), cars = 0x6ce640, car_lights = 0x6ce620, light_ids = 0x5d6ac0;
    uint32_t chains = 0x5d7040, last = 0, limit;
    uint16_t link;

    G32(0x6b9b94) = count;
    if (g_view == 0x6cd4a0) {
        G32(0x6c1e18) = G32(0x4beb84);
        limit = G32(0x6c1e34);
    } else {
        G32(0x6c1e18) = G32(0x6c1e1c);
        limit = G32(0x6c1e38);
    }
    G32(0x6c1e24) = limit;
    link = G16(chains);
    if (!link) {
        G32(0x6b9cb8) = count;
        G32(cars) = 0;
        return;
    }
    do {
        uint32_t ids = G32(light_ids), slot, o;
        if (ids * 2 != last) {
            last = ids * 2;
            light_colours_from_ids(ids, lights);
        }
        o = grid_record(link);
        slot = 0x6b9ba0 + 4 * count;
        do {
            uint32_t kind = U32(o, 0);
            if (kind & 0x400) {
                if ((kind & 0x1ff) == 3) {
                    G32(cars) = o;
                    cars += 4;
                    G32(car_lights) = ids;
                    car_lights += 4;
                } else {
                    model_draw_lit(o, lights);
                    if (U16(o, 0x16a) && !G32(0x4d1198) && (int32_t)slot < 0x6b9cb8) {
                        G32(slot) = o;
                        count++;
                        slot += 4;
                    }
                }
            }
            o = grid_record(U16(o, 4));
        } while (o);
        light_ids += 4;
        chains += 2;
        link = G16(chains);
    } while (link);
    G32(0x6b9cb8) = count;
    G32(cars) = 0;
}

/* ===== 0x46a9e0: the countdown numeral (0x4d3da8, 5 and up: the "go"
 * frame) as a textured quad (packet 0x4d3cb8, or 0x4d3d30 for the second
 * view 0x6cdd10) round the screen offset (0x426240), its size from the
 * texture record and the race's width, zooming with the frame counter
 * 0x4d3dac, faded in (frames 0-3) and out (after 10); linked to the
 * front of the overlay table entry *0x6ce608 + 4. Not while the race state
 * (+0x57c) is 4 or more. */
void GAMEAPI countdown_body(void)
{
    uint32_t race = g_race, tex, pkt, colour, sx, sy, entry;
    int32_t half_w, half_h, n = GI32(0x4d3da8), scale, t, f, px = 0, py = 0;
    uint8_t step;

    scale = (int32_t)(((int64_t)(int32_t)(U16(race, 0xa82) * 255u) * 0x66666667) >> 32) >> 8;
    scale += (uint32_t)scale >> 31;
    scale = (int32_t)((uint32_t)scale << 6);
    if (n >= 5)
        n = 5;
    if (I32(race, 0x57c) >= 4)
        return;
    screen_offset_get(&px, &py);
    tex = ((U16(G32(0x6c1eb0), 0x2e) + 0x7eu) << 4) + G32(0x6c1e14);
    t = GI32(0x4d3dac);
    half_w = ((((int32_t)U8(tex, 4) - (int32_t)U8(tex, 0)) >> 1) * scale >> 9) / (t + 10);
    half_h = ((scale * 42) >> 9) / (t + 10);
    pkt = g_view == 0x6cdd10 ? 0x4d3d30 : 0x4d3cb8;
    U32(pkt, 0x20) = U32(tex, 0);
    U32(pkt, 0x3c) = U32(tex, 4);
    U16(pkt, 0x58) = U16(tex, 8);
    U16(pkt, 0x74) = U16(tex, 0xa);
    if (n >= 5) {
        U8(pkt, 0x20) += 0x1c;
        n -= 5;
    }
    step = (uint8_t)((uint8_t)(n + 1) * 42);
    U8(pkt, 0x21) += step;
    U8(pkt, 0x3d) += step;
    U8(pkt, 0x59) = (uint8_t)(U8(pkt, 0x3d) + 0x29);
    U8(pkt, 0x75) = (uint8_t)(U8(pkt, 0x3d) + 0x29);
    U8(pkt, 0x3c) += 0xe4;
    U8(pkt, 0x74) += 0xe4;
    sx = wa_add(wa_i2f(px - half_w), g_clip_left);
    U32(pkt, 0x10) = sx;
    U32(pkt, 0x48) = sx;
    sx = wa_add(wa_i2f(px + half_w), g_clip_left);
    U32(pkt, 0x2c) = sx;
    U32(pkt, 0x64) = sx;
    sy = wa_add(wa_i2f(py - half_w), g_clip_top); /* sic: the top uses the half width */
    U32(pkt, 0x14) = sy;
    U32(pkt, 0x30) = sy;
    sy = wa_add(wa_i2f(py + half_h), g_clip_top);
    U32(pkt, 0x4c) = sy;
    U32(pkt, 0x68) = sy;
    if (t < 4) {
        int32_t b = (int32_t)((uint32_t)t << 6);
        if (b >= 0xff)
            b = 0xff;
        f = (int32_t)((uint32_t)b * 255u) >> 8;
    } else if (t > 10) {
        int32_t b = (int32_t)((uint32_t)(t - 10) << 4), e;
        if (b >= 0xff)
            b = 0xff;
        e = 0xff - b;
        f = (int32_t)((uint32_t)e * 255u) >> 8;
    } else {
        f = 0xff;
    }
    colour = ((uint32_t)U8(pkt, 7) << 24) | 0xff8080u;
    if (G8(0x6ccd84) & 1) {
        U32(pkt, 4) = colour;
    } else {
        /* the shade's byte arithmetic: each byte * f >> 8 through eax,
         * whose upper bytes carry over */
        uint32_t ff = (uint32_t)f, hi, mid, lo, eax;
        eax = (colour >> 16) & 0xff;
        eax = eax * ff >> 8;
        hi = ((colour >> 24) << 24) | ((eax & 0xff) << 16);
        eax = (eax & ~0xffu) | ((colour >> 8) & 0xff);
        eax = eax * ff >> 8;
        mid = eax & 0xff;
        eax = (eax & ~0xffu) | (colour & 0xff);
        eax = eax * ff >> 8;
        lo = eax & 0xff;
        U32(pkt, 4) = hi | (mid << 8) | lo;
    }
    U8(pkt, 0x22) = (uint8_t)f;
    entry = U32(G32(0x6ce608), 4) & 0x7fffffffu;
    U32(pkt, 0) = U32(entry, 0);
    U32(entry, 0) = pkt;
}

/* ===== 0x47a030(cell): the view's visibility list. The view's current /
 * previous run lists (0x5d9454 / 0x5d9460 for the first view 0x6cd4a0,
 * else 0x5d9458 / 0x5d945c) are swapped, the cell's runs built into the
 * current one (0x47a0d0), compared with the previous (0x47a500), their
 * light flags (0x47c420) and light (0x47c730, 0 / 1 for the first /
 * second view) set; returns the current list. */
uint32_t *GAMEAPI visibility_body(uint8_t *cell)
{
    uint32_t now, before;

    if (g_view == 0x6cd4a0) {
        now = G32(0x5d9460);
        before = G32(0x5d9454);
        G32(0x5d9454) = now;
        G32(0x5d9460) = before;
    } else {
        now = G32(0x5d945c);
        before = G32(0x5d9458);
        G32(0x5d9458) = now;
        G32(0x5d945c) = before;
    }
    track_runs_visible(cell, now);
    track_runs_diff_texture(now, before);
    track_runs_light_flags(now);
    track_runs_light(now, g_view == 0x6cd4a0 ? 0 : 1);
    return (uint32_t *)(uintptr_t)now;
}

/* ===== 0x485140(i): (re)spawns screen flake i (records of 0xa0 at
 * *0x4d5898) for the race's weather (+0x553): live (+0), a random spot in
 * the 1024 x 1024 square centred on the race's screen (+0x7c / +0x80
 * floats), +0x88 / +0x98 cleared, then per kind: 2 (snow) life +0x98
 * 0x60.., size +0x84 24.., +0x9c / +0x94 random bytes, +0x8c / +0x90
 * cleared; 1 (rain) life 0x64.., size 10.., +0x9c cleared; 0 (lens drop)
 * up to 50 tries of a random angle (+0x9c, the sin / cos tables 0x4b24f8 /
 * 0x4b2578) and radius round (w / 2, 3h / 2) until on screen, then life
 * 0x64.., size 24.., +0x90 cleared. The random seed is 0x4b2c50. */
void GAMEAPI flake_spawn_body(int32_t i)
{
    uint32_t off = (uint32_t)i * 0xa0u, s, r, f;
    int32_t x, y;

    U32(g_flakes + off, 0x98) = 0;
    s = wa_next(G32(0x4b2c50));
    x = (int32_t)(((s >> 15) << 10) >> 16);
    s = wa_next(s);
    G32(0x4b2c50) = s;
    U32(g_flakes + off, 0) = 1;
    y = (int32_t)(((s >> 15) << 10) >> 16);
    x -= (int32_t)(0x400 - (uint32_t)U16(g_race, 0xa82)) >> 1;
    U32(g_flakes + off, 0x7c) = wa_i2f(x);
    y -= (int32_t)(0x400 - (uint32_t)U16(g_race, 0xa80)) >> 1;
    U32(g_flakes + off, 0x80) = wa_i2f(y);
    U32(g_flakes + off, 0x88) = 0;
    switch (U8(g_race, 0x553)) {
    case 2:
        s = wa_next(G32(0x4b2c50));
        r = s;
        s = wa_next(s);
        G32(0x4b2c50) = s;
        U32(g_flakes + off, 0x98) = ((((r >> 15) << 5) >> 16) + 0x60);
        U32(g_flakes + off, 0x84) = wa_i2f((int32_t)((((s >> 15) << 3) >> 16) + 0x18));
        s = wa_next(G32(0x4b2c50));
        G32(0x4b2c50) = s;
        U32(g_flakes + off, 0x9c) = ((s >> 15) * 255u) >> 16;
        U32(g_flakes + off, 0x8c) = 0;
        U32(g_flakes + off, 0x90) = 0;
        s = wa_next(G32(0x4b2c50));
        G32(0x4b2c50) = s;
        U32(g_flakes + off, 0x94) = ((s >> 15) * 255u) >> 16;
        return;
    case 1:
        s = wa_next(G32(0x4b2c50));
        r = s;
        s = wa_next(s);
        G32(0x4b2c50) = s;
        U32(g_flakes + off, 0x98) = ((((r >> 15) << 5) >> 16) + 0x64);
        U32(g_flakes + off, 0x84) = wa_i2f((int32_t)((((s >> 15) * 5u) >> 16) + 10));
        U32(g_flakes + off, 0x9c) = 0;
        return;
    case 0: {
        uint32_t h = U16(g_race, 0xa80), y_mid, tries = 50;
        y_mid = wa_i2f((int32_t)(h + h - (h >> 1)));
        do {
            int32_t radius, v;
            uint32_t angle, fl;
            s = wa_next(G32(0x4b2c50));
            G32(0x4b2c50) = s;
            h = U16(g_race, 0xa80);
            radius = (int32_t)(((((s >> 15) << 8) >> 16) + 0x80) * h) / (int32_t)(h >> 1);
            s = wa_next(s);
            G32(0x4b2c50) = s;
            U32(g_flakes + off, 0x9c) = (((s >> 15) & 0x3f) - 0x20) & 0xff;
            angle = U32(g_flakes + off, 0x9c);
            v = (int32_t)((uint32_t)(int32_t)GS16(0x4b24f8 + 2 * angle) * (uint32_t)radius) >> 14;
            U32(g_flakes + off, 0x7c) = wa_i2f(v + (int32_t)(U16(g_race, 0xa82) >> 1));
            angle = U32(g_flakes + off, 0x9c);
            v = (int32_t)(0u - (uint32_t)(int32_t)GS16(0x4b2578 + 2 * angle) * (uint32_t)radius) >> 14;
            U32(g_flakes + off, 0x80) = wa_add(wa_i2f(v), y_mid);
            fl = g_flakes + off;
            if (!(wa_cmp(U32(fl, 0x7c), K_ZERO) & 0x41) &&
                (wa_cmp(U32(fl, 0x7c), wa_i2f(U16(g_race, 0xa82))) & 1) &&
                !(wa_cmp(U32(fl, 0x80), K_ZERO) & 0x41) &&
                (wa_cmp(U32(fl, 0x80), wa_i2f(U16(g_race, 0xa80))) & 1))
                break;
        } while ((int32_t)--tries > 0);
        f = g_flakes + off;
        s = wa_next(G32(0x4b2c50));
        r = s;
        s = wa_next(s);
        G32(0x4b2c50) = s;
        U32(f, 0x98) = (((r >> 15) << 5) >> 16) + 0x64;
        U32(g_flakes + off, 0x84) = wa_i2f((int32_t)((((s >> 15) << 3) >> 16) + 0x18));
        U32(g_flakes + off, 0x90) = 0;
        return;
    }
    default:
        return;
    }
}

/* ===== 0x490430(camera): the respawn / snatch sparkle of the camera's
 * target car (camera + 4, kind 3) while its +0x689 is set (and not
 * 0x6cce40 & 0x10): the car's packet at +0x68c (tinted from the level) and
 * the 13 squares after it, random grey levels and positions on a 32-pixel
 * grid of the race's screen (seed 0x4b2d1c), each linked to the front of
 * the world list entry 1 (0x6cd514, or 0x6cdd84 when 0x6cce84). */
void GAMEAPI sparkle_body(uint8_t *camera)
{
    uint32_t car, level, pkt, q, list, code, wcell, hcell, k;
    int32_t c;

    if (G8(0x6cce40) & 0x10)
        return;
    car = U32(camera, 4);
    if ((U32(car, 0) & 0x1ff) != 3)
        return;
    level = U8(car, 0x689);
    if (!level)
        return;
    pkt = U32(car, 0x68c);
    q = pkt + 0x78;
    list = (G32(0x6cce84) ? G32(0x6cdd84) : G32(0x6cd514)) & 0x7fffffffu;
    U32(pkt, 0) = U32(list, 0);
    U32(list, 0) = pkt;
    c = (int32_t)(level * 5);
    U8(pkt, 4) = (uint8_t)(c >= 0xff ? 0xff : c);
    c = (int32_t)(level * 7);
    U8(pkt, 5) = (uint8_t)(c >= 0xff ? 0xff : c);
    U8(pkt, 6) = (uint8_t)(c >= 0xff ? 0xff : c);
    hcell = ((uint32_t)U16(g_race, 0xa80) >> 5) - 1;
    wcell = ((uint32_t)U16(g_race, 0xa82) >> 5) - 1;
    code = (uint32_t)U8(q, 7) << 24;
    for (k = 13; k; k--) {
        uint32_t s = wa_next(G32(0x4b2d1c)), r, grey, size;
        int32_t x0, x1, y0, y1;
        G32(0x4b2d1c) = s;
        r = s >> 15;
        grey = r & 0x7f;
        if (level < 16)
            grey = grey * level >> 4;
        U32(q, 4) = ((grey << 8 | grey) << 8 | grey) | code;
        x0 = (int32_t)(((r >> 4) & 0x1f) * wcell);
        y0 = (int32_t)(((r >> 8) & 0x1f) * hcell);
        size = ((r >> 12) & 0x1f) << 2;
        x1 = (int32_t)(size + (uint32_t)x0);
        y1 = (int32_t)((uint32_t)y0 + size);
        U32(q, 0x10) = wa_add(g_clip_left, wa_i2f(x0));
        U32(q, 0x2c) = wa_add(g_clip_left, wa_i2f(x1));
        U32(q, 0x48) = wa_add(g_clip_left, wa_i2f(x0));
        U32(q, 0x64) = wa_add(g_clip_left, wa_i2f(x1));
        U32(q, 0x14) = wa_add(wa_i2f(y0), g_clip_top);
        U32(q, 0x30) = wa_add(wa_i2f(y0), g_clip_top);
        U32(q, 0x4c) = wa_add(wa_i2f(y1), g_clip_top);
        U32(q, 0x60) = 0x40400000; /* 3.0f */
        U32(q, 0x44) = 0x40400000;
        U32(q, 0x28) = 0x40400000;
        U32(q, 0xc) = 0x40400000;
        U32(q, 8) = 0x40;
        U32(q, 0x68) = wa_add(wa_i2f(y1), g_clip_top);
        list = (G32(0x6cce84) ? G32(0x6cdd84) : G32(0x6cd514)) & 0x7fffffffu;
        U32(q, 0) = U32(list, 0);
        U32(list, 0) = q;
        q += 0x78;
    }
}
