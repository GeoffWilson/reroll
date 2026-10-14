/* Group PF: the original's still-reachable functions in 0x433d90-0x4387d0
 * (the trails / beams effects source file, the platform file around WinMain
 * and the start of the HUD file).
 *
 *  - the trails (8 records of 0x18 at 0x6c8020): a new point every 5 race
 *    ticks (0x433d90), drawn as two rings of 16 transformed points joined
 *    into 0x68-byte quads (0x433f00);
 *  - the lightning beams (4 records of 0x18 at 0x6c8100): 17 jittered
 *    points along the beam, sparks, 0x78-byte quads (0x4345c0);
 *  - the platform layer: the window procedure (0x435ed1) and its
 *    WM_DESTROY / WM_COMMAND handlers (0x436034, 0x436542), window focus +
 *    input poll (0x4368f0), the clock (0x43692a, timeGetTime), the loading
 *    picture (0x436c2c), the display mode dialog (0x437171 and its
 *    helpers 0x436ed6 / 0x436f3b), the error dialog (0x4375c7);
 *  - the HUD: per-view update (0x4386f0), the race-kind extras (0x438760)
 *    and the sprite quad set-up all HUD pieces use (0x4387d0).
 *
 * Calls outside the group go by address; Windows calls by name
 * (winapi.h). The GTE transform / projection pointers (0x6ccd78 /
 * 0x6ccd80) hand a value from one to the other on the x87 stack, so each
 * pair (with the _ftol calls the beams make between them) is one asm
 * block. Float steps are the original's x87 instructions in inline asm. */
#include "../runtime.h"
#include "physics.h"
#include "winapi.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define GS16(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))

#define STDCALL __attribute__((stdcall))
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

#define g_config G32(0x4b2198)
#define g_race G32(0x4b1960)

typedef int32_t(GAMEAPI *pf_fn0)(void);
typedef int32_t(GAMEAPI *pf_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *pf_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *pf_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *pf_fn4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(GAMEAPI *pf_fn5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef int32_t(GAMEAPI *pf_fn6)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f);
#define CALL0(a) (((pf_fn0)(a))())
#define CALL1(a, x) (((pf_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((pf_fn2)(a))(A(x), A(y)))
#define CALL3(a, x, y, z) (((pf_fn3)(a))(A(x), A(y), A(z)))
#define CALL4(a, x, y, z, w) (((pf_fn4)(a))(A(x), A(y), A(z), A(w)))
#define CALL5(a, x, y, z, w, v) (((pf_fn5)(a))(A(x), A(y), A(z), A(w), A(v)))
#define CALL6(a, x, y, z, w, v, u) (((pf_fn6)(a))(A(x), A(y), A(z), A(w), A(v), A(u)))

/* Outside the group. */
#define vec_length_at(v) CALL1(0x480cd0, v)
#define fix_div16_at(a, b) CALL2(0x42df70, a, b)
#define vec_cross_at(a, b, out) CALL3(0x4806a0, a, b, out)
#define sin_table_at(a) CALL1(0x480b10, a)
#define particle_spawn_at(pos, vel, kind) CALL3(0x462690, pos, vel, kind)
#define text_entry_at(buf, size, sec, idx) CALL4(0x42e6d0, buf, size, sec, idx)
#define sprintf_at2(buf, fmt, a) CALL3(0x4979f0, buf, fmt, a)
#define language_set_at(l) CALL1(0x42e3c0, l)

/* This group's functions (also in build/groups/pf_protos.txt for physics.h). */
void GAMEAPI trail_point_add(const int32_t *pos, const int16_t *ahead, const int32_t *up); /* 0x433d90 */
void GAMEAPI trails_draw(void); /* 0x433f00 */
void GAMEAPI beams_draw(void); /* 0x4345c0 */
int32_t STDCALL window_proc(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam); /* 0x435ed1 */
void GAMEAPI window_destroy_quit(void); /* 0x436034 */
void GAMEAPI window_command(uint32_t hwnd, uint32_t id); /* 0x436542 */
void GAMEAPI input_focus_poll(void); /* 0x4368f0 */
uint32_t GAMEAPI game_time_get(void); /* 0x43692a */
void GAMEAPI loading_picture_draw(int32_t which); /* 0x436c2c */
void GAMEAPI mode_dialog_languages_fill(uint32_t dlg); /* 0x436ed6 */
void GAMEAPI mode_dialog_texts_set(uint32_t dlg); /* 0x436f3b */
int32_t STDCALL mode_dialog_proc(uint32_t dlg, uint32_t msg, uint32_t wparam, uint32_t lparam); /* 0x437171 */
int32_t STDCALL error_dialog_proc(uint32_t dlg, uint32_t msg, uint32_t wparam, uint32_t lparam); /* 0x4375c7 */
void GAMEAPI hud_update(uint32_t *slot); /* 0x4386f0 */
void GAMEAPI hud_update_extra(uint32_t *slot); /* 0x438760 */
uint8_t *GAMEAPI hud_sprite_set(int32_t n, int32_t a, int32_t frame); /* 0x4387d0 */

/* ===================================================================== */
/* x87 helpers. */

/* fld dword a; fcomp dword b; fnstsw: C0 (a below b, or unordered). */
static inline int pf_below(uint32_t a, uint32_t b)
{
    uint16_t sw;

    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 1;
}

/* fld dword v; call _ftol. */
static inline int32_t pf_ftol(uint32_t v)
{
    int32_t r;

    __asm__ volatile("flds %1\n\tmovl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(v)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
}

/* The OT link both effects use: bucket d of 0x6cdd80 (when 0x6cce84) or
 * 0x6cd510, flag bit cleared; the prim goes at the bucket's head. */
static inline void pf_ot_link(uint32_t prim, int32_t d)
{
    uint32_t bucket = (G32(0x6cce84) ? G32(0x6cdd80 + d * 4) : G32(0x6cd510 + d * 4)) & 0x7fffffff;

    G32(prim) = G32(bucket);
    G32(bucket) = prim;
}

/* The depth bucket from a depth already shifted: halved past 0x100 and
 * again past 0x180, 0x1ff at most, 1 at least. */
static inline int32_t pf_depth_bucket(int32_t d)
{
    if (d > 0x100) {
        d = d / 2 + 0x80;
        if (d > 0x180) {
            d = d / 2 + 0xc0;
            if (d > 0x1ff)
                return 0x1ff;
        }
    }
    if (d < 1)
        d = 1;
    return d;
}

static inline void pf_copy6(uint32_t dst, const uint8_t *src)
{
    int k;

    for (k = 0; k < 6; k++)
        U32(dst, k * 4) = U32(src, k * 4);
}

/* ===================================================================== */
/* The trails. */

/* ===== 0x433d90: a trail point, at most every 5 race ticks (0x6c0a50 vs
 * the last one 0x4be380): record *0x6c80fc (0x18 bytes at 0x6c8020)
 * gets pos >> 8 (+0 / +2 / +4, +6 = 0), the unit vector from pos to
 * ahead << 8 (+8, 14-bit) and its cross product with `up` (+0x10), then
 * +0xe = 1 (live) and the index moves on (mod 8). Nothing when the length
 * is under 0x2000 or over 0x4000000. (The original keeps the 16.16
 * reciprocal in its first argument's slot.) ===== */
void GAMEAPI trail_point_add(const int32_t *pos, const int16_t *ahead, const int32_t *up)
{
    uint32_t rec = 0x6c8020 + G32(0x6c80fc) * 0x18;
    int32_t now = GI32(0x6c0a50);
    int32_t d = now - GI32(0x4be380);
    int32_t v[3], out[3], len, k, i;

    d = (d ^ (d >> 31)) - (d >> 31);
    if (d < 5)
        return;
    GI32(0x4be380) = now;
    S16(rec, 0) = (int16_t)(pos[0] >> 8);
    S16(rec, 2) = (int16_t)(pos[1] >> 8);
    S16(rec, 4) = (int16_t)(pos[2] >> 8);
    U16(rec, 6) = 0;
    v[0] = (int32_t)((uint32_t)(int32_t)ahead[0] << 8) - pos[0];
    v[1] = (int32_t)((uint32_t)(int32_t)ahead[1] << 8) - pos[1];
    v[2] = (int32_t)((uint32_t)(int32_t)ahead[2] << 8) - pos[2];
    len = vec_length_at(v);
    if (len < 0x2000 || len > 0x4000000)
        return;
    k = fix_div16_at(0x10000, len);
    for (i = 0; i < 3; i++)
        v[i] = (int32_t)(uint32_t)(((int64_t)v[i] * k) >> 16);
    S16(rec, 8) = (int16_t)(v[0] >> 2);
    S16(rec, 0xa) = (int16_t)(v[1] >> 2);
    S16(rec, 0xc) = (int16_t)(v[2] >> 2);
    vec_cross_at(v, up, out);
    S16(rec, 0x10) = (int16_t)(out[0] >> 2);
    S16(rec, 0x12) = (int16_t)(out[1] >> 2);
    S16(rec, 0x14) = (int16_t)(out[2] >> 2);
    U16(rec, 0xe) = 1;
    G32(0x6c80fc) = G32(0x6c80fc) + 1;
    if (G32(0x6c80fc) >= 8)
        G32(0x6c80fc) = 0;
}

/* V0 = a (x / y dword, z word; the original's z high half is an unwritten
 * stack word: 0 here, the transforms read words), transform into out_a,
 * project; the same for b into out_b. */
static void pf_transform_pair(uint32_t a_xy, uint32_t a_z, uint8_t *out_a, uint32_t b_xy, uint32_t b_z, uint8_t *out_b)
{
    uint32_t blk[6] = {a_xy, a_z, A(out_a), b_xy, b_z, A(out_b)};

    __asm__ volatile("movl (%0), %%eax\n\t"
                     "movl %%eax, 0x6ccc64\n\t"
                     "movl 4(%0), %%eax\n\t"
                     "movl %%eax, 0x6ccc68\n\t"
                     "pushl 8(%0)\n\t"
                     "call *0x6ccd78\n\t"
                     "call *0x6ccd80\n\t"
                     "movl 12(%0), %%eax\n\t"
                     "movl %%eax, 0x6ccc64\n\t"
                     "movl 16(%0), %%eax\n\t"
                     "movl %%eax, 0x6ccc68\n\t"
                     "pushl 20(%0)\n\t"
                     "call *0x6ccd78\n\t"
                     "addl $8, %%esp\n\t"
                     "call *0x6ccd80"
                     :
                     : "S"(blk)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
}

/* ===== 0x433f00: the trails: a new point first when 0x6c8004 is the race
 * tick (0x433d90 on 0x6c80f0 / 0x6c80e0 / 0x6c8010). Per live record
 * (+0xe): age +6 up by one (dead past 0x1f); two rings (scale 4 * age and
 * 4 * age + 16) of 8 points from the table 0x4acb94 (pairs a, b: pos +- (a
 * * scale * dir_a + b * scale * dir_b) >> 24, 16-bit) through the
 * transform pointers; then 16 quads (0x68 bytes, buffer *0x4be378 + record
 * * 0x680, + 0x3400 for the second OT set) joining ring 0 and ring 1:
 * colour bytes +4 / +6 from the age, the four screen vertices at +8 / +0x20
 * / +0x38 / +0x50, linked at the farthest depth (_ftol >> 5, from 1) unless
 * all four share a clip bit. ===== */
void GAMEAPI trails_draw(void)
{
    uint8_t verts[32 * 0x18];
    int32_t r;

    if (G32(0x6c8004) == G32(0x6c0a50))
        trail_point_add((const int32_t *)PTR(0x6c80f0), (const int16_t *)PTR(0x6c80e0), (const int32_t *)PTR(0x6c8010));
    for (r = 0; r < 8; r++) {
        uint32_t rec = 0x6c8020 + r * 0x18;
        uint32_t age, base;
        int32_t ax, ay, az, bx, by, bz, row, k, i;
        uint8_t c_red, c_alpha;

        if (U16(rec, 0xe) == 0)
            continue;
        age = (uint32_t)U16(rec, 6) + 1;
        U16(rec, 6) = (uint16_t)age;
        if ((uint16_t)age > 0x1f) {
            U16(rec, 0xe) = 0;
            continue;
        }
        ax = (int32_t)((uint32_t)(int32_t)S16(rec, 8) << 2);
        ay = (int32_t)((uint32_t)(int32_t)S16(rec, 0xa) << 2);
        az = (int32_t)((uint32_t)(int32_t)S16(rec, 0xc) << 2);
        bx = (int32_t)((uint32_t)(int32_t)S16(rec, 0x10) << 2);
        by = (int32_t)((uint32_t)(int32_t)S16(rec, 0x12) << 2);
        bz = (int32_t)((uint32_t)(int32_t)S16(rec, 0x14) << 2);
        for (row = 0; row < 2; row++) {
            int32_t scale = (int32_t)(age * 4 + row * 0x10);

            for (k = 0; k < 8; k++) {
                int32_t c1 = GI32(0x4acb94 + k * 8) * scale;
                int32_t c2 = scale * GI32(0x4acb98 + k * 8);
                int32_t x1 = 0, y1 = 0, z1 = 0, x2 = 0, y2 = 0, z2 = 0;
                uint16_t px, py, pz, mx, my, mz;
                uint8_t *out = verts + (row * 16 + k) * 0x18;

                if (c1 != 0) {
                    int32_t c = c1 >> 8;

                    y1 = ((ay >> 8) * c) >> 16;
                    x1 = ((ax >> 8) * c) >> 16;
                    z1 = ((az >> 8) * c) >> 16;
                }
                if (c2 != 0) {
                    int32_t c = c2 >> 8;

                    x2 = ((bx >> 8) * c) >> 16;
                    y2 = ((by >> 8) * c) >> 16;
                    z2 = ((bz >> 8) * c) >> 16;
                }
                px = (uint16_t)(U16(rec, 0) + x2 + x1);
                py = (uint16_t)(U16(rec, 2) + y2 + y1);
                pz = (uint16_t)(U16(rec, 4) + z2 + z1);
                mx = (uint16_t)(U16(rec, 0) - x2 - x1);
                my = (uint16_t)(U16(rec, 2) - y2 - y1);
                mz = (uint16_t)(U16(rec, 4) - z2 - z1);
                pf_transform_pair(px | (uint32_t)py << 16, pz, out, mx | (uint32_t)my << 16, mz, out + 8 * 0x18);
            }
        }

        base = G32(0x4be378) + r * 0x680;
        if (G32(0x6ce60c) == 0x6cdd10)
            base += 0x3400;
        c_red = (uint8_t)(0x80 - (uint8_t)((uint8_t)age << 2));
        {
            int32_t e = (int32_t)age - 0x10;

            e = (e ^ (e >> 31)) - (e >> 31);
            c_alpha = (uint8_t)(0x40 - (uint8_t)((uint8_t)e << 2));
        }
        for (i = 1; i <= 16; i++) {
            uint32_t prim = base + (i - 1) * 0x68;
            int32_t nb = i < 16 ? i : 0, d;
            uint32_t t;

            U8(prim, 6) = c_red;
            U8(prim, 4) = c_alpha;
            t = U32(verts + (i - 1) * 0x18, 4);
            if (pf_below(t, U32(verts + nb * 0x18, 4)))
                t = U32(verts + nb * 0x18, 4);
            if (pf_below(t, U32(verts + (16 + i - 1) * 0x18, 4)))
                t = U32(verts + (16 + i - 1) * 0x18, 4);
            if (pf_below(t, U32(verts + (16 + nb) * 0x18, 4)))
                t = U32(verts + (16 + nb) * 0x18, 4);
            d = pf_ftol(t) >> 5;
            if (d < 1)
                continue;
            d = pf_depth_bucket(d + 0x10);
            pf_copy6(prim + 8, verts + (i - 1) * 0x18);
            pf_copy6(prim + 0x20, verts + nb * 0x18);
            pf_copy6(prim + 0x38, verts + (16 + i - 1) * 0x18);
            pf_copy6(prim + 0x50, verts + (16 + nb) * 0x18);
            if ((U32(prim, 0x38) & U32(prim, 8) & U32(prim, 0x50) & U32(prim, 0x20)) == 0)
                pf_ot_link(prim, d);
        }
    }
}

/* ===================================================================== */
/* The beams. */

/* rand (0x4b11d8): x * 16807 mod 2^31. */
static inline uint32_t pf_rand(void)
{
    uint32_t s = (G32(0x4b11d8) * 16807u) & 0x7fffffff;

    G32(0x4b11d8) = s;
    return s;
}

/* The beams' pair: V0 = a, transform into ra, (project after) V0 = b,
 * transform into rb, _ftol(ra depth) and _ftol(rb depth) around the
 * second projection, as the original orders them. */
static void pf_beam_pair(uint32_t a_xy, uint32_t a_z, uint8_t *ra, uint32_t b_xy, uint32_t b_z, uint8_t *rb,
                         int32_t *da, int32_t *db)
{
    uint32_t blk[8] = {a_xy, a_z, A(ra), b_xy, b_z, A(rb), 0, 0};

    __asm__ volatile("movl (%0), %%eax\n\t"
                     "movl %%eax, 0x6ccc64\n\t"
                     "movl 4(%0), %%eax\n\t"
                     "movl %%eax, 0x6ccc68\n\t"
                     "pushl 8(%0)\n\t"
                     "call *0x6ccd78\n\t"
                     "addl $4, %%esp\n\t"
                     "call *0x6ccd80\n\t"
                     "movl 12(%0), %%eax\n\t"
                     "movl %%eax, 0x6ccc64\n\t"
                     "movl 16(%0), %%eax\n\t"
                     "movl %%eax, 0x6ccc68\n\t"
                     "pushl 20(%0)\n\t"
                     "call *0x6ccd78\n\t"
                     "addl $4, %%esp\n\t"
                     "movl 8(%0), %%eax\n\t"
                     "flds 4(%%eax)\n\t"
                     "movl $0x497a80, %%eax\n\t"
                     "call *%%eax\n\t"
                     "movl %%eax, 24(%0)\n\t"
                     "call *0x6ccd80\n\t"
                     "movl 20(%0), %%eax\n\t"
                     "flds 4(%%eax)\n\t"
                     "movl $0x497a80, %%eax\n\t"
                     "call *%%eax\n\t"
                     "movl %%eax, 28(%0)"
                     :
                     : "S"(blk)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
    *da = (int32_t)blk[6];
    *db = (int32_t)blk[7];
}

/* ===== 0x4345c0: the lightning beams (4 records of 0x18 at 0x6c8100:
 * +0 timer, +2 phase, +4 shape bits, +6 brightness, +8 start, +0x10 end,
 * int16 x / y / z). Per live beam: timer down, phase up, brightness down
 * 0x40; one rand in 32 re-strikes (new phase and shape, brightness 0xff,
 * and from then on 14 shape points a frame instead of 1, sic: for the
 * following beams too). New shape points from the sine table (17-point
 * buffer *0x4be37c + beam * 0x110, 16 bytes each, scrolled by one when the
 * shape has bits), then 17 points from start to end ((end - start) << 12
 * per step), jittered by rand except at the ends, offset by the shape
 * point (half at the second and second to last), a spark (0x462690 kind
 * 0x10) one time in 1024 and always at the end; two transformed vertices
 * per point; quads (0xf0 apart from *0x4be374 + beam * 0xf00, + 0x78 for
 * the second OT set) at the nearer depth ((_ftol >> 5) - 8) unless all
 * four share a clip bit; colour from the brightness (0xffffff with byte
 * +0x22 when 0x6ccd84 bit 0). ===== */
void GAMEAPI beams_draw(void)
{
    uint8_t recs[17 * 0x30];
    int32_t count = 1;
    int32_t b;

    for (b = 0; b < 4; b++) {
        uint32_t beam = 0x6c8100 + b * 0x18;
        uint32_t pts, prim, q, r, lo;
        int32_t hi, s0, s1, s2, m0, m1, m2, k, i;
        int32_t a0, a1, a2, j0, j2; /* (the z jitter mask a1 * 2 - 1 goes unused: sic below) */
        int32_t acc_x, acc_y, acc_z, x0, y0, z0, dx, dy, dz;
        uint32_t mode, bright;

        if (U16(beam, 0) == 0)
            continue;
        U16(beam, 0)--;
        U16(beam, 2)++;
        U16(beam, 6) += 0xffc0;
        r = pf_rand();
        hi = (int32_t)(r >> 16);
        lo = (uint32_t)hi & 0x1f;
        hi >>= 5;
        if (lo > 0x18) {
            U16(beam, 2) = (uint16_t)(hi & 0x3ff);
            hi >>= 7;
            if (lo > 0x1e)
                count = 0xe;
            U16(beam, 6) = 0xff;
            U16(beam, 4) = (uint16_t)(hi & 0x3f);
        }
        mode = U16(beam, 4);
        bright = U8(beam, 6);
        s0 = (mode >> 2) & 3;
        s1 = (mode >> 4) & 3;
        s2 = mode & 3;
        pts = G32(0x4be37c) + b * 0x110;
        m2 = (0x10 << s2) - 1;
        m0 = (0x10 << s0) - 1;
        m1 = (0x10 << s1) - 1;
        for (k = 0; k < count; k++) {
            uint32_t p = (uint32_t)U16(beam, 2) + k;
            uint32_t w = pts + 0x10 + k * 0x10;

            S16(w, 0) = (int16_t)(sin_table_at((p & m2) << (15 - s2)) >> (s0 + 10));
            S16(w, 2) = (int16_t)(sin_table_at((p & m0) << (15 - s0)) >> (s1 + 10));
            S16(w, 4) = (int16_t)(sin_table_at((p & m1) << (15 - s1)) >> (s2 + 10));
            U16(w, 0xa) = U16(w, 2);
            U16(w, 8) = U16(w, 0);
            U16(w, 0xc) = U16(w, 4);
        }
        if (s0 | s2) {
            int32_t j;

            for (j = 15; j >= 2; j--) {
                uint32_t to = pts + j * 0x10, from = to - 0x10;

                U16(to, 2) = U16(from, 2);
                U16(to, 0) = U16(from, 0);
                U16(to, 4) = U16(from, 4);
                U16(to, 0xa) = U16(from, 0xa);
                U16(to, 8) = U16(from, 8);
                U16(to, 0xc) = U16(from, 0xc);
            }
        }

        prim = G32(0x4be374) + b * 0xf00;
        if (G32(0x6ce60c) == 0x6cdd10)
            prim += 0x78;
        acc_x = acc_y = acc_z = 0;
        x0 = S16(beam, 8);
        y0 = S16(beam, 0xa);
        z0 = S16(beam, 0xc);
        dx = (int32_t)((uint32_t)(S16(beam, 0x10) - x0) << 12);
        dy = (int32_t)((uint32_t)(S16(beam, 0x12) - y0) << 12);
        dz = (int32_t)((uint32_t)(S16(beam, 0x14) - z0) << 12);
        a2 = s2 * 4 + 0x10;
        j2 = a2 * 2 - 1;
        a1 = s1 * 4 + 0x10;
        a0 = s0 * 4 + 0x10;
        j0 = a0 * 2 - 1;
        q = prim + 0x40;
        for (i = 0; i < 17; i++) {
            uint32_t pt = pts + i * 0x10;
            uint8_t *rec1 = recs + i * 0x30, *rec2 = rec1 + 0x18;
            uint16_t v1x, v1y, v1z, v2x, v2y, v2z;
            int32_t d1, d2, d;

            v1x = v2x = (uint16_t)((acc_x >> 16) + x0);
            v1y = v2y = (uint16_t)((acc_y >> 16) + y0);
            v1z = v2z = (uint16_t)((acc_z >> 16) + z0);
            if (i != 0 && i != 16) {
                uint32_t s = pf_rand() >> 16;

                v1x += (uint16_t)((j2 & s) - a2);
                v1y += (uint16_t)(((s >> 8) & j0) - a0);
                v1z += (uint16_t)(-a1); /* sic: (rand >> 32) & mask */
            }
            if (i == 1 || i == 15) {
                v1x += (uint16_t)(S16(pt, 0) >> 1);
                v1y += (uint16_t)(S16(pt, 2) >> 1);
                v1z += (uint16_t)(S16(pt, 4) >> 1);
            } else if (i != 0 && i != 16) {
                v1x += U16(pt, 0);
                v1y += U16(pt, 2);
                v1z += U16(pt, 4);
            }
            if (i > 0) {
                uint32_t mask = i != 16 ? 0x3ff : 0;

                if (((pf_rand() >> 16) & mask) == 0) {
                    int32_t pos[3], vel[3];

                    pos[0] = (int32_t)((uint32_t)(int32_t)(int16_t)v1x << 16);
                    pos[2] = (int32_t)((uint32_t)(int32_t)(int16_t)v1z << 16);
                    pos[1] = (int32_t)((uint32_t)(int32_t)(int16_t)v1y << 16);
                    vel[0] = (int32_t)(((pf_rand() >> 16) & 0xff) - 0x80) << 12;
                    vel[1] = (int32_t)(((pf_rand() >> 16) & 0xff) - 0x80) << 12;
                    vel[2] = (int32_t)(((pf_rand() >> 16) & 0xff) - 0x80) << 12;
                    particle_spawn_at(pos, vel, 0x10);
                }
            }
            if (i != 0 && i != 16) {
                uint32_t s = pf_rand() >> 16;

                v2x += (uint16_t)((j2 & s) - a2);
                v2y += (uint16_t)(((s >> 8) & j0) - a0);
                v2z += (uint16_t)(-a1); /* sic */
            }
            if (i == 1 || i == 15) {
                v2x += (uint16_t)(S16(pt, 8) >> 1);
                v2y += (uint16_t)(S16(pt, 0xa) >> 1);
                v2z += (uint16_t)(S16(pt, 0xc) >> 1);
            } else if (i != 0 && i != 16) {
                v2x += U16(pt, 8);
                v2y += U16(pt, 0xa);
                v2z += U16(pt, 0xc);
            }
            pf_beam_pair(v1x | (uint32_t)v1y << 16, v1z, rec1, v2x | (uint32_t)v2y << 16, v2z, rec2, &d1, &d2);
            acc_x += dx;
            acc_y += dy;
            acc_z += dz;
            d = d2 < d1 ? d2 : d1;
            if (d < 1)
                continue;
            d = pf_depth_bucket((d >> 5) - 8);
            if (i <= 0)
                continue;
            pf_copy6(q - 0x38, rec1 - 0x30);
            pf_copy6(q - 0x1c, rec1);
            pf_copy6(q, rec2 - 0x30);
            pf_copy6(q + 0x1c, rec2);
            if ((U32(q, -0x1c) & U32(q, 0) & U32(q, -0x38) & U32(q, 0x1c)) != 0)
                continue;
            if (G8(0x6ccd84) & 1) {
                U32(q, -0x3c) = (uint32_t)U8(q, -0x39) << 24 | 0xffffff;
                U8(q, -0x1e) = (uint8_t)bright;
            } else {
                U32(q, -0x3c) = (uint32_t)U8(q, -0x39) << 24 | bright << 16 | bright << 8 | bright;
            }
            pf_ot_link(prim, d);
            prim += 0xf0;
            q += 0xf0;
        }
    }
}

/* ===================================================================== */
/* The platform layer. */

/* ===== 0x435ed1: the window procedure: WM_MOVE: the client origin to
 * screen coordinates (0x4d5f90, from 0, 0); WM_SYSCOMMAND: the screen
 * saver / size / move / maximise / minimise / key menu / close commands
 * swallowed; WM_SYSKEYDOWN / UP swallowed; WM_DESTROY 0x436034; WM_KEYDOWN
 * the cheat typing 0x4360e1; WM_COMMAND 0x436542; else DefWindowProcA. ===== */
int32_t STDCALL window_proc(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam)
{
    switch (msg) {
    case 0x104:
    case 0x105:
        return 0;
    case 2:
        window_destroy_quit();
        return 0;
    case 3:
        G32(0x4d5f94) = 0;
        G32(0x4d5f90) = G32(0x4d5f94);
        ClientToScreen(GMEM(hwnd), GMEM(0x4d5f90));
        return 0;
    case 0x100:
        CALL5(0x4360e1, hwnd, wparam, 1, (int32_t)(int16_t)lparam, lparam >> 16);
        return 0;
    case 0x111:
        window_command(hwnd, wparam & 0xffff);
        return 0;
    case 0x112:
        if (wparam == 0xf170 || wparam == 0xf140 || wparam == 0xf130 || wparam == 0xf040 || wparam == 0xf050 ||
            wparam == 0xf100 || wparam == 0xf090)
            return 0;
        break;
    }
    return DefWindowProcA(GMEM(hwnd), msg, wparam, lparam);
}

/* ===== 0x436034: WM_DESTROY: the boot data saved (0x474260), everything
 * shut down (0x435e2c(0)), 0x4be460 = 1, PostQuitMessage(0). ===== */
void GAMEAPI window_destroy_quit(void)
{
    CALL0(0x474260);
    CALL1(0x435e2c, 0);
    G32(0x4be460) = 1;
    PostQuitMessage(0);
}

/* ===== 0x436542: WM_COMMAND: id 0x9c41 destroys the window. (The window
 * procedure passes two more arguments, unread.) ===== */
void GAMEAPI window_command(uint32_t hwnd, uint32_t id)
{
    if (id == 0x9c41)
        DestroyWindow(GMEM(hwnd));
}

/* ===== 0x4368f0: the game window (0x6c8000) made active and focused if
 * it isn't, then the input poll 0x489600. ===== */
void GAMEAPI input_focus_poll(void)
{
    if (G32(0x6c8000) != 0 && (uint32_t)(uintptr_t)GetActiveWindow() != G32(0x6c8000)) {
        SetActiveWindow(GMEM(G32(0x6c8000)));
        SetFocus(GMEM(G32(0x6c8000)));
    }
    CALL0(0x489600);
}

/* ===== 0x43692a: the clock, timeGetTime(). ===== */
uint32_t GAMEAPI game_time_get(void)
{
    return timeGetTime();
}

/* ===== 0x436c2c: the loading picture: loaded once (0x4be490 = 0x477530
 * on the name 0x4ace70 % n: 1, 2 once something ran (0x4d4650 or
 * 0x4acc64 != -1), 3 + language (3 past 7) when `which` is 1, 8 with
 * config +0x96), cut into texture pages (0x4201f0, first page 0x4acc64),
 * then drawn over the screen (0x4203a0). ===== */
void GAMEAPI loading_picture_draw(int32_t which)
{
    if (G32(0x4be490) == 0) {
        char name[0x104];
        int32_t n = 1;

        if (G32(0x4d4650) == 0 && GI32(0x4acc64) == -1)
            n = 1;
        else
            n = 2;
        if (which == 1) {
            n = U8(g_config, 0x36) + 3;
            if (n > 7)
                n = 3;
        }
        if (U8(g_config, 0x96) != 0)
            n = 8;
        sprintf_at2(name, 0x4ace70, n);
        G32(0x4be490) = CALL3(0x477530, name, 0x4be398, 0);
        G32(0x4acc64) = CALL5(0x4201f0, G32(0x4be3ac), G32(0x4be3a8), (int32_t)GS16(0x4be39a), (int32_t)GS16(0x4be39e),
                              (int32_t)GS16(0x4be39c));
    }
    if (G32(0x4be490) != 0) {
        int32_t rect[4];

        SetRect(rect, 0, 0, G32(0x4acc38), G32(0x4acc3c));
        CALL6(0x4203a0, G32(0x4acc64), (int32_t)GS16(0x4be39a), (int32_t)GS16(0x4be39e), rect, 0, -1);
    }
}

/* ===== 0x436ed6: the mode dialog's language list (combo 0x3ea): the five
 * names at 0x4ace90 (0x14 apart), CB_ADDSTRING. ===== */
void GAMEAPI mode_dialog_languages_fill(uint32_t dlg)
{
    int32_t i;

    for (i = 0; i < 5; i++)
        SendDlgItemMessageA(GMEM(dlg), 0x3ea, 0x143, 0, 0x4ace90 + i * 0x14);
}

/* ===== 0x436f3b: the mode dialog's texts in the current language
 * (text_entry of 0x4acee8.. entries 2..8, WM_SETTEXT on items 0x405,
 * 0x407, 0x3f7, 0x406, 1, 3, 2). (The callers pass the language too,
 * unread.) ===== */
void GAMEAPI mode_dialog_texts_set(uint32_t dlg)
{
    static const uint32_t items[7] = {0x405, 0x407, 0x3f7, 0x406, 1, 3, 2};
    char text[0x100];
    int32_t i;

    for (i = 0; i < 7; i++) {
        text_entry_at(text, 0x100, 0x4acee8 + i * 0x18, i + 2);
        SendDlgItemMessageA(GMEM(dlg), items[i], 0xc, 0, (LPARAM)(uintptr_t)text);
    }
}

/* ===== 0x437171: the display mode dialog (DialogBoxParamA from
 * 0x4370b2). WM_INITDIALOG: language (0x4be4b8 = config +0x36) list and
 * texts, the device list (0x4883c0, selection 0x4be448, 0x487020), the
 * mode list (0x488470), the two check boxes (0x4acc58, 0x4be44c through
 * 0x4acc68), Cancel-like button 3 disabled unless 0x474b70. WM_COMMAND:
 * language / device selection changes; OK: language kept (texts reset when
 * it changed), check boxes read back, the result device | mode << 16
 * (config +0x48 = 0xb when 0x488d60 refuses the device); Cancel -1; 3: -2
 * (uninstall, texts reset). Returns 1 for handled messages and 0x200. ===== */
int32_t STDCALL mode_dialog_proc(uint32_t dlg, uint32_t msg, uint32_t wparam, uint32_t lparam)
{
    (void)lparam;
    if (msg == 0x110) {
        G32(0x4be4b8) = U8(g_config, 0x36);
        language_set_at(G32(0x4be4b8));
        mode_dialog_languages_fill(dlg);
        SendDlgItemMessageA(GMEM(dlg), 0x3ea, 0x14e, U8(g_config, 0x36), 0);
        mode_dialog_texts_set(dlg);
        CALL2(0x4883c0, dlg, 0x3e8);
        G32(0x4be448) = SendDlgItemMessageA(GMEM(dlg), 0x3e8, 0x147, 0, 0);
        CALL1(0x487020, G32(0x4be448));
        CALL4(0x488470, dlg, 0x3e9, G32(0x4be44c), G32(0x4acc58));
        SendDlgItemMessageA(GMEM(dlg), 0x3f7, 0xf1, G32(0x4acc68 + G32(0x4acc58) * 4), 0);
        SendDlgItemMessageA(GMEM(dlg), 0x406, 0xf1, G32(0x4acc68 + G32(0x4be44c) * 4), 0);
        if (CALL0(0x474b70) == 0)
            EnableWindow(GetDlgItem(GMEM(dlg), 3), 0);
        return 1;
    }
    if (msg == 0x111) {
        uint32_t id = wparam & 0xffff, code = wparam >> 16;

        if (id == 0x3ea && code == 1) {
            G32(0x4be4b8) = SendDlgItemMessageA(GMEM(dlg), 0x3ea, 0x147, 0, 0);
            language_set_at(G32(0x4be4b8));
            mode_dialog_texts_set(dlg);
            CALL4(0x488470, dlg, 0x3e9, G32(0x4be44c), G32(0x4acc58));
        }
        if (id == 0x3e8 && code == 1) {
            G32(0x4be448) = SendDlgItemMessageA(GMEM(dlg), 0x3e8, 0x147, 0, 0);
            CALL1(0x487020, G32(0x4be448));
            CALL4(0x488470, dlg, 0x3e9, G32(0x4be44c), G32(0x4acc58));
        }
        if (id == 1) {
            uint32_t mode;

            if (G32(0x4be4b8) != U8(g_config, 0x36)) {
                U8(g_config, 0x36) = (uint8_t)G32(0x4be4b8);
                CALL1(0x473c50, 1);
            }
            U8(g_config, 0x36) = (uint8_t)G32(0x4be4b8);
            language_set_at(U8(g_config, 0x36));
            G32(0x4acc58) = SendDlgItemMessageA(GMEM(dlg), 0x3f7, 0xf0, 0, 0) == 1 ? 1 : 0;
            G32(0x4be44c) = SendDlgItemMessageA(GMEM(dlg), 0x406, 0xf0, 0, 0) == 1 ? 1 : 0;
            G32(0x4be448) = SendDlgItemMessageA(GMEM(dlg), 0x3e8, 0x147, 0, 0);
            mode = (uint32_t)SendDlgItemMessageA(GMEM(dlg), 0x3e9, 0x147, 0, 0);
            if (CALL1(0x488d60, G32(0x4be448)) == 0)
                U32(g_config, 0x48) = 0xb;
            EndDialog(GMEM(dlg), mode << 16 | G32(0x4be448));
        }
        if (id == 2)
            EndDialog(GMEM(dlg), -1);
        if (id == 3) {
            U8(g_config, 0x36) = (uint8_t)G32(0x4be4b8);
            language_set_at(U8(g_config, 0x36));
            CALL1(0x473c50, 0);
            EndDialog(GMEM(dlg), -2);
        }
        return 1;
    }
    if (msg == 0x200)
        return 1;
    return 0;
}

/* ===== 0x4375c7: the error dialog (resource 0x83): strings entry 8; the
 * title text (0x4acf90 entry 0) on item 0x409 and the message on 0x408:
 * config +0x48 >= 0x80: the format 0x4acfa4 entry 1 with +0x48 / +0x4c;
 * else entry +0x48 + 2 of 0x4acfb8 (for 4, + 0x4acfe0 + 0x4acfcc entry
 * 10). OK / Cancel end it with -1. ===== */
int32_t STDCALL error_dialog_proc(uint32_t dlg, uint32_t msg, uint32_t wparam, uint32_t lparam)
{
    char message[0x104], extra[0x104], title[0x104];

    (void)lparam;
    if (msg == 0x110) {
        CALL1(0x42df90, 8);
        text_entry_at(title, 0x104, 0x4acf90, 0);
        if (I32(g_config, 0x48) >= 0x80) {
            text_entry_at(extra, 0x104, 0x4acfa4, 1);
            CALL4(0x4979f0, message, extra, U32(g_config, 0x48), U32(g_config, 0x4c));
        } else {
            text_entry_at(message, 0x104, 0x4acfb8, I32(g_config, 0x48) + 2);
            if (I32(g_config, 0x48) == 4) {
                text_entry_at(extra, 0x104, 0x4acfcc, 10);
                CALL2(0x498460, message, 0x4acfe0);
                CALL2(0x498460, message, extra);
            }
        }
        SendDlgItemMessageA(GMEM(dlg), 0x409, 0xc, 0, (LPARAM)(uintptr_t)title);
        SendDlgItemMessageA(GMEM(dlg), 0x408, 0xc, 0, (LPARAM)(uintptr_t)message);
        return 1;
    }
    if (msg == 0x111) {
        if ((wparam & 0xffff) == 1 || (wparam & 0xffff) == 2)
            EndDialog(GMEM(dlg), -1);
        return 1;
    }
    return 0;
}

/* ===================================================================== */
/* The HUD. */

/* ===== 0x4386f0: a view's HUD (slot: *slot = the car): speedo (car
 * +0x658), the HUD pieces 0x438e60 / 0x4390c0 / 0x439050 / 0x438fb0 and
 * 0x439270 (with 0x439050's result), 0x439f20 in race mode 2. (The
 * original leaves the pieces' arguments stacked: each reads its own.) ===== */
void GAMEAPI hud_update(uint32_t *slot)
{
    int32_t r;

    CALL1(0x438df0, (int32_t)S16(slot[0], 0x658));
    CALL1(0x438e60, slot);
    CALL1(0x4390c0, slot);
    r = CALL1(0x439050, slot);
    CALL1(0x438fb0, slot);
    CALL2(0x439270, slot, r);
    if (U8(g_race, 0x551) == 2)
        CALL2(0x439f20, slot, U8(g_race, 0x552));
}

/* ===== 0x438760: a view's HUD extras: 0x439400 in race modes 3 and 5..8,
 * 0x43a000 (frame counter byte 0x6c7fdc), 0x45cfe0 unless race +0x552. ===== */
void GAMEAPI hud_update_extra(uint32_t *slot)
{
    uint8_t mode = U8(g_race, 0x551);

    if (mode == 3 || (mode > 4 && mode <= 8))
        CALL1(0x439400, slot);
    CALL2(0x43a000, slot, G32(0x6c7fdc) & 0xff);
    if (U8(g_race, 0x552) == 0)
        CALL2(0x45cfe0, slot, G32(0x6c7fdc) & 0xff);
}

/* fild qword (v, 0); fstp dword t; fld t; fadd dword *src; fstp *dst. */
static inline void pf_fadd_size(uint32_t v, uint32_t src, uint32_t dst)
{
    uint64_t q = v;
    float t;

    __asm__ volatile("fildll %2\n\tfstps %1\n\tflds %1\n\tfadds %3\n\tfstps %0"
                     : "=m"(*(float *)PTR(dst)), "=m"(t)
                     : "m"(q), "m"(*(float *)PTR(src))
                     : "st");
}

/* ===== 0x4387d0: HUD sprite n's quad (0xf0-byte packets at 0x6c3ec0):
 * frame -1 only copies y0 / y1 into y2 / y3 (+0x4c, +0x68). Otherwise uv0
 * (+0x20) comes from texture record (*0x6c1eb0 +0x2e + frame + 0x6c7ed4[n])
 * of *0x6c1e14, the size from the old corners (+1 when a corner was 0xff)
 * or per kind: digits 4..7 (13 x 23, u + 14 * a), 8 / 45..47 (27 x 41,
 * second row from 5, v + 42 * a), 52 (25 wide, u + 26 * a), 65 / 66 (the
 * size scaled by 350 / 512, u + w * a), else (u + w * a, half size on
 * screen). The screen x1 / x3 / y2 / y3 = x0 / x2 / y0 / y1 + the size
 * scaled by width << 8 / 0x4acff0 (x87, through a float); the corners'
 * uv, 0xff when they wrap. Returns the packet. ===== */
uint8_t *GAMEAPI hud_sprite_set(int32_t n, int32_t a, int32_t frame)
{
    uint32_t p = 0x6c3ec0 + (uint32_t)n * 0xf0;
    uint32_t tex;
    int32_t dw, dh, w;
    uint8_t du = 0, dv = 0, u, v, ue, ve;

    if (frame == -1) {
        U32(p, 0x4c) = U32(p, 0x14);
        U32(p, 0x68) = U32(p, 0x30);
        return PTR(p);
    }
    tex = ((uint32_t)U16(G32(0x6c1eb0), 0x2e) + (uint32_t)(frame + GS16(0x6c7ed4 + n * 2))) * 16 + G32(0x6c1e14);
    dw = (int32_t)U8(p, 0x3c) - U8(p, 0x20);
    if (U8(p, 0x3c) == 0xff)
        dw++;
    dh = (int32_t)U8(p, 0x59) - U8(p, 0x21);
    if (U8(p, 0x59) == 0xff)
        dh++;
    U32(p, 0x20) = G32(tex);
    switch (n) {
    case 4:
    case 5:
    case 6:
    case 7:
        dv = 0;
        dh = 0x17;
        du = (uint8_t)(a * 14);
        w = (int32_t)(G32(0x4acc38) << 8) / GI32(0x4acff0);
        pf_fadd_size((uint32_t)(w * 7) >> 8, p + 0x10, p + 0x2c);
        pf_fadd_size((uint32_t)(w * 7) >> 8, p + 0x48, p + 0x64);
        pf_fadd_size((uint32_t)(w * 12) >> 8, p + 0x14, p + 0x4c);
        pf_fadd_size((uint32_t)(w * 12) >> 8, p + 0x30, p + 0x68);
        dw = 0xd;
        break;
    case 8:
    case 45:
    case 46:
    case 47:
        dw = 0x1b;
        if (a >= 5) {
            a -= 5;
            du = 0x1c;
        }
        dh = 0x29;
        w = (int32_t)(G32(0x4acc38) << 8) / GI32(0x4acff0);
        pf_fadd_size((uint32_t)(w * 14) >> 8, p + 0x10, p + 0x2c);
        pf_fadd_size((uint32_t)(w * 14) >> 8, p + 0x48, p + 0x64);
        pf_fadd_size((uint32_t)(w * 21) >> 8, p + 0x14, p + 0x4c);
        pf_fadd_size((uint32_t)(w * 21) >> 8, p + 0x30, p + 0x68);
        dv = (uint8_t)(a * 42);
        break;
    case 52: {
        int32_t h;

        du = (uint8_t)(a * 26);
        w = (int32_t)(G32(0x4acc38) << 8) / GI32(0x4acff0);
        pf_fadd_size((uint32_t)(w * 13) >> 8, p + 0x10, p + 0x2c);
        pf_fadd_size((uint32_t)(w * 13) >> 8, p + 0x48, p + 0x64);
        h = (dh + 1) >> 1;
        pf_fadd_size((uint32_t)(w * h) >> 8, p + 0x14, p + 0x4c);
        pf_fadd_size((uint32_t)(w * h) >> 8, p + 0x30, p + 0x68);
        dw = 0x19;
        break;
    }
    case 65:
    case 66: {
        int32_t sw = (int32_t)((uint32_t)(dw * 350)) >> 9;
        int32_t sh = (int32_t)((uint32_t)(dh * 350)) >> 9;

        du = (uint8_t)(dw * a);
        w = (int32_t)(G32(0x4acc38) << 8) / GI32(0x4acff0);
        pf_fadd_size((uint32_t)(w * sw) >> 8, p + 0x10, p + 0x2c);
        pf_fadd_size((uint32_t)(w * sw) >> 8, p + 0x48, p + 0x64);
        pf_fadd_size((uint32_t)(w * sh) >> 8, p + 0x14, p + 0x4c);
        pf_fadd_size((uint32_t)(w * sh) >> 8, p + 0x30, p + 0x68);
        break;
    }
    default:
        du = (uint8_t)(dw * a);
        w = (int32_t)(G32(0x4acc38) << 8) / GI32(0x4acff0);
        pf_fadd_size((uint32_t)(w * (dw >> 1)) >> 8, p + 0x10, p + 0x2c);
        pf_fadd_size((uint32_t)(w * (dw >> 1)) >> 8, p + 0x48, p + 0x64);
        pf_fadd_size((uint32_t)(w * (dh >> 1)) >> 8, p + 0x14, p + 0x4c);
        pf_fadd_size((uint32_t)(w * (dh >> 1)) >> 8, p + 0x30, p + 0x68);
        break;
    }
    u = (uint8_t)(U8(p, 0x20) + du);
    v = (uint8_t)(U8(p, 0x21) + dv);
    U8(p, 0x20) = u;
    U8(p, 0x21) = v;
    U8(p, 0x58) = u;
    U8(p, 0x3d) = v;
    ue = (uint8_t)(u + (uint8_t)dw);
    ve = (uint8_t)(v + (uint8_t)dh);
    U8(p, 0x74) = ue;
    U8(p, 0x3c) = ue;
    U8(p, 0x75) = ve;
    U8(p, 0x59) = ve;
    if (ue < U8(p, 0x20)) {
        U8(p, 0x74) = 0xff;
        U8(p, 0x3c) = 0xff;
    }
    if (ve < U8(p, 0x21)) {
        U8(p, 0x75) = 0xff;
        U8(p, 0x59) = 0xff;
    }
    return PTR(p);
}
