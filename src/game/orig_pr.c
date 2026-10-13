/* Group PR: the original's still-reachable functions in 0x47d400-0x47f1a2
 * (the warp / ice overlay and sprite drawing source file) and 0x4821d0 (the
 * cars' light trails, in the vector maths file).
 *
 *  - the warp overlay: its lookup tables (0x47d400: radius and angle bytes
 *    of a 128 x 128 grid, through the C runtime's sqrt / pow / sin / atan /
 *    _ftol), set-up (0x47d980: one of eight TWn.BMP pictures, a 256 x 256
 *    texture), the per-frame texture update (0x47d5a0, drawn by 0x47d6d0)
 *    and the ICE.BMP texture (0x47d7f0, drawn by 0x47d8f0), the two
 *    full-screen draws (0x47db20, 0x47db80);
 *  - the light sprite release (0x47dca0);
 *  - the sprites: per visible track section the objects on the grid
 *    (0x47dd70) drawn by type (0x47e1b0, 0x47e020, 0x47e220, 0x47e2a0,
 *    0x47e340), the billboard quad (0x47e5f0, corners rotated by 0x47e980),
 *    the colour words (0x47e550), the flat sprite (0x47e4c0), the streak
 *    (0x47ef30) with its view axis (0x47ee90), the glare sprites drawn last
 *    with a screen-area budget (0x47eaa0);
 *  - the tint near the markers (0x47ea00);
 *  - the cars' light trails (0x4821d0).
 *
 * Float steps are the original's x87 instructions in inline asm (fild,
 * fistp, fadd / fsub, the warp tables' x87 chain with its C runtime calls,
 * the glare budget's fmul pair). The transform / projection pointers
 * *0x6ccd78 / *0x6ccd7c / *0x6ccd80 (and 0x426cd0 / 0x426dc0) hand st(0)
 * from one to the next, so each pair is one asm block. Everything outside
 * the group is called by address. */
#include "../runtime.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S8(p, off) (*(int8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define GS16(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

#define g_race G32(0x4b1960)
#define g_config G32(0x4b2198)

typedef int32_t(GAMEAPI *pr_fn0)(void);
typedef int32_t(GAMEAPI *pr_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *pr_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *pr_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *pr_fn4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(GAMEAPI *pr_fn6)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f);

#define CALL0(a) (((pr_fn0)(a))())
#define CALL1(a, x) (((pr_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((pr_fn2)(a))(A(x), A(y)))
#define CALL3(a, x, y, z) (((pr_fn3)(a))(A(x), A(y), A(z)))
#define CALL4(a, x, y, z, w) (((pr_fn4)(a))(A(x), A(y), A(z), A(w)))
#define CALL6(a, x, y, z, w, v, u) (((pr_fn6)(a))(A(x), A(y), A(z), A(w), A(v), A(u)))

#define tex_lock_at(h, n, bits, pal, bpp, pitch) CALL6(0x428c30, h, n, bits, pal, bpp, pitch)
#define tex_unlock_at(h, n, pal) CALL3(0x428c60, h, n, pal)
#define tex_handle_new_at() CALL0(0x428c80)
#define tex_upload_at(h, w, ht, a, b, c) CALL6(0x428c90, h, w, ht, a, b, c)
#define bmp_load_at(name, out, flag) CALL3(0x477530, name, out, flag)
#define pixels_rgb_convert_at(src, n, dst, alpha) CALL4(0x41e1f0, src, n, dst, alpha)
#define mem_alloc_at(n) CALL1(0x476b80, n)
#define screen_tint_add_at(c, x) CALL2(0x42bfc0, c, x)
#define screen_flash_set_at(a, b, c, d, e, f) CALL6(0x42c040, a, b, c, d, e, f)
#define fade_tint_add_at(n) CALL1(0x42bf20, n)
#define grid_unlink2_at(o) CALL1(0x4612a0, o)
#define grid_record_at(i) ((uint8_t *)(uintptr_t)CALL1(0x4613e0, i))
#define fix_sqrt_table_at(n) CALL1(0x427960, n)
#define gte_matrix_save_at(out) CALL1(0x426480, out)
#define vec_length_at(v) CALL1(0x480cd0, v)
#define vec_div_at(v, d) CALL2(0x486e90, v, d)
#define vec_normalise_at(v) CALL1(0x480c70, v)
#define vec_dot_at(a, b) CALL2(0x40d950, a, b)

/* Every x87 register, for asm blocks that call out or use the FPU stack. */
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* This group's functions (also in build/groups/pr_protos.txt for physics.h). */
void GAMEAPI warp_tables_build(uint32_t power); /* 0x47d400 */
int32_t GAMEAPI warp_texture_update(int32_t fade); /* 0x47d5a0 */
void GAMEAPI warp_texture_render(int32_t du, int32_t dv, uint32_t bits, int32_t pitch, int32_t bpp); /* 0x47d6d0 */
void GAMEAPI ice_texture_load(void); /* 0x47d7f0 */
void GAMEAPI ice_texture_render(uint32_t bits, int32_t pitch, int32_t bpp); /* 0x47d8f0 */
int32_t GAMEAPI warp_init(void); /* 0x47d980 */
void GAMEAPI warp_overlay_draw(void); /* 0x47db20 */
void GAMEAPI ice_overlay_draw(void); /* 0x47db80 */
void GAMEAPI light_sprite_release(int32_t index); /* 0x47dca0 */
void GAMEAPI sprites_draw(const uint8_t *list); /* 0x47dd70 */
void GAMEAPI sprite_line_draw(uint8_t *s); /* 0x47e020 */
void GAMEAPI sprite_marker_draw(uint8_t *s); /* 0x47e1b0 */
void GAMEAPI light_sprite_draw(uint8_t *s); /* 0x47e220 */
void GAMEAPI sprite_puff_draw(uint8_t *s); /* 0x47e2a0 */
void GAMEAPI sprite_billboard_draw(uint8_t *s); /* 0x47e340 */
void GAMEAPI sprite_flat_insert(uint8_t *pkt); /* 0x47e4c0 */
void GAMEAPI sprite_colour_fill(uint32_t level, uint32_t code, uint32_t *rgb, uint8_t *alpha); /* 0x47e550 */
void GAMEAPI sprite_quad_build(const uint32_t *pos, int32_t size, uint8_t *pkt, uint32_t mode, uint32_t flags,
                               const uint8_t *sprite); /* 0x47e5f0 */
void GAMEAPI sprite_corners_rotate(uint32_t angle, int32_t w, int32_t h, uint32_t *a, uint32_t *b); /* 0x47e980 */
void GAMEAPI marker_proximity_tint(const uint8_t *body); /* 0x47ea00 */
void GAMEAPI sprites_glare_draw(void); /* 0x47eaa0 */
void GAMEAPI view_side_axis_update(void); /* 0x47ee90 */
void GAMEAPI sprite_streak_build(const int16_t *pos, int32_t scale, uint8_t *pkt, int32_t shift); /* 0x47ef30 */
void GAMEAPI car_light_trails_draw(void); /* 0x4821d0 */

/* ---- x87 helpers: the original's instructions ---- */

/* fld a; fistp dword: a rounded by the control word. */
static inline int32_t pr_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fild dword; fstp dword. */
static inline uint32_t pr_fild(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild word; fstp dword. */
static inline uint32_t pr_filds(int16_t v)
{
    uint32_t r;
    __asm__ volatile("filds %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fld a; fadd b; fstp. */
static inline uint32_t pr_add(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fsub b; fstp. */
static inline uint32_t pr_sub(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fadd b; fadd c; fstp (one rounding to single at the end). */
static inline uint32_t pr_add_add(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfadds %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fadd b; fsub c; fstp. */
static inline uint32_t pr_add_sub(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfsubs %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* push rec; call *0x6ccd78 (transform); call *0x6ccd80 (projection, takes
 * the transform's st(0)); pop. */
static inline void pr_transform_project(void *rec)
{
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\tcall *0x6ccd80\n\taddl $4, %%esp"
                     :
                     : "S"(rec)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
}

/* push rec; call *0x6ccd7c (transform with depth); when its depth (+4) is
 * not negative, call *0x6ccd80. Returns the sign bit set (no projection). */
static inline int pr_transform_depth_project(void *rec)
{
    int neg;

    __asm__ volatile("pushl %1\n\t"
                     "call *0x6ccd7c\n\t"
                     "addl $4, %%esp\n\t"
                     "xorl %%eax, %%eax\n\t"
                     "testl $0x80000000, 4(%1)\n\t"
                     "jne 1f\n\t"
                     "call *0x6ccd80\n\t"
                     "xorl %%eax, %%eax\n\t"
                     "jmp 2f\n"
                     "1:\n\t"
                     "movl $1, %%eax\n"
                     "2:"
                     : "=a"(neg)
                     : "S"(rec)
                     : "ecx", "edx", "cc", "memory", X87);
    return neg;
}

/* The depth slot of a z (sar 5 done by the caller): halved twice above
 * 0x100 / 0x180, at most 0x1ff. */
static int32_t pr_depth_curve(int32_t z)
{
    if (z > 0x100) {
        z = z / 2 + 0x80;
        if (z > 0x180) {
            z = z / 2 + 0xc0;
            if (z > 0x1ff)
                z = 0x1ff;
        }
    }
    return z;
}

/* Link a packet into ordering-table slot z (0x6cdd80 when 0x6cce84 is set,
 * else 0x6cd510; entries' top bit masked). */
static void pr_ot_link(uint32_t z, uint32_t pkt)
{
    uint32_t e = (G32(0x6cce84) != 0 ? G32(0x6cdd80 + 4 * z) : G32(0x6cd510 + 4 * z)) & 0x7fffffff;

    G32(pkt) = G32(e);
    G32(e) = pkt;
}

/* The colour registers 0x6ccc34 / 38 / 3c from the bytes of a colour
 * word (<< 4), 0x6ccc60 from a level byte (<< 4). */
static void pr_colour_set(uint32_t c, uint8_t level)
{
    G32(0x6ccc34) = (c & 0xff) << 4;
    G32(0x6ccc38) = ((c >> 8) & 0xff) << 4;
    G32(0x6ccc3c) = ((c >> 16) & 0xff) << 4;
    G32(0x6ccc60) = (uint32_t)level << 4;
}

/* ===================================================================== */
/* The warp and ice overlays. */

/* One radius byte of the warp table (0x47d400's first loop): n = x * x +
 * y * y as a double to sqrt (0x4981d4), stored as a float, * 262144.0
 * (0x4a3478) to pow (0x497cc9) with the power, stored, * 256 (0x4a33c0)
 * = q, stored; q * 1.284 / 64 (0x4a3470 / 0x4a3468, doubles) to sin
 * (0x497b74), stored, * 32 (0x4a3464) + q, stored, through _ftol
 * (0x497a80). */
struct pr_radius {
    int32_t n;
    uint32_t nd[2];
    uint32_t power[2];
    uint32_t r, p, q, s, v;
};

static uint8_t pr_warp_radius(struct pr_radius *w)
{
    uint32_t r;

    __asm__ volatile("fildl 0(%1)\n\t"
                     "fstpl 4(%1)\n\t"
                     "pushl 8(%1)\n\t"
                     "pushl 4(%1)\n\t"
                     "movl $0x4981d4, %%eax\n\t"
                     "call *%%eax\n\t"
                     "addl $8, %%esp\n\t"
                     "fstps 20(%1)\n\t"
                     "flds 20(%1)\n\t"
                     "fmuls 0x4a3478\n\t"
                     "pushl 16(%1)\n\t"
                     "pushl 12(%1)\n\t"
                     "subl $8, %%esp\n\t"
                     "fstpl (%%esp)\n\t"
                     "movl $0x497cc9, %%eax\n\t"
                     "call *%%eax\n\t"
                     "addl $16, %%esp\n\t"
                     "fstps 24(%1)\n\t"
                     "flds 24(%1)\n\t"
                     "fmuls 0x4a33c0\n\t"
                     "fstps 28(%1)\n\t"
                     "flds 28(%1)\n\t"
                     "fmull 0x4a3470\n\t"
                     "fdivl 0x4a3468\n\t"
                     "subl $8, %%esp\n\t"
                     "fstpl (%%esp)\n\t"
                     "movl $0x497b74, %%eax\n\t"
                     "call *%%eax\n\t"
                     "addl $8, %%esp\n\t"
                     "fstps 32(%1)\n\t"
                     "flds 32(%1)\n\t"
                     "fmuls 0x4a3464\n\t"
                     "fadds 28(%1)\n\t"
                     "fstps 36(%1)\n\t"
                     "flds 36(%1)\n\t"
                     "movl $0x497a80, %%eax\n\t"
                     "call *%%eax"
                     : "=a"(r)
                     : "S"(w)
                     : "ecx", "edx", "cc", "memory", X87);
    return (uint8_t)r;
}

/* One angle byte (0x47d400's second loop): y / x (floats, 0.0001 for 0)
 * as a double to atan (0x497c24), stored, * 40.738 (0x4a3460), stored,
 * through _ftol. */
struct pr_angle {
    uint32_t x, y, s, t;
};

static uint8_t pr_warp_angle(struct pr_angle *w)
{
    uint32_t r;

    __asm__ volatile("flds 4(%1)\n\t"
                     "fdivs 0(%1)\n\t"
                     "subl $8, %%esp\n\t"
                     "fstpl (%%esp)\n\t"
                     "movl $0x497c24, %%eax\n\t"
                     "call *%%eax\n\t"
                     "addl $8, %%esp\n\t"
                     "fstps 8(%1)\n\t"
                     "flds 8(%1)\n\t"
                     "fmuls 0x4a3460\n\t"
                     "fstps 12(%1)\n\t"
                     "flds 12(%1)\n\t"
                     "movl $0x497a80, %%eax\n\t"
                     "call *%%eax"
                     : "=a"(r)
                     : "S"(w)
                     : "ecx", "edx", "cc", "memory", X87);
    return (uint8_t)r;
}

/* ===== 0x47d400: the warp lookup table *0x4d57ec, 128 x 128 byte pairs
 * (x, y from -64 to 63, entry (y * 128 + x) * 2 + 0x4080): the first byte
 * from the distance (0 counts as 1 in each coordinate) through the power
 * (a float argument), the second from the angle atan(y / x). ===== */
void GAMEAPI warp_tables_build(uint32_t power)
{
    struct pr_radius w;
    struct pr_angle a;
    int32_t x, y;

    __asm__ volatile("flds %1\n\tfstpl %0" : "=m"(w.power) : "m"(power) : "st");
    for (y = -0x40; y < 0x40; y++) {
        for (x = -0x40; x < 0x40; x++) {
            int32_t cx = x != 0 ? x : 1, cy = y != 0 ? y : 1;
            uint8_t b;

            w.n = cy * cy + cx * cx;
            b = pr_warp_radius(&w);
            G8(G32(0x4d57ec) + 2 * (uint32_t)(y * 0x80 + 0x2040 + x)) = b;
        }
    }
    for (y = -0x40; y < 0x40; y++) {
        for (x = -0x40; x < 0x40; x++) {
            uint8_t b;

            a.x = x != 0 ? pr_fild(x) : 0x38d1b717;
            a.y = y != 0 ? pr_fild(y) : 0x38d1b717;
            b = pr_warp_angle(&a);
            G8(G32(0x4d57ec) + 2 * (uint32_t)(y * 0x80 + x) + 0x4081) = b;
        }
    }
}

/* ===== 0x47d5a0: the warp texture's frame (config +0x29 on, handle
 * 0x4b24a0 not -1): locked (0x428c30; out bits, bpp, pitch), drawn at 8 /
 * 16 bits per pixel (0x47d6d0 with the scroll bytes 0x4d57f8 / 0x4d57f9),
 * the palette 0x4d4ba0 (64 entries) into the lock's buffer, unlocked; then
 * shown (0x4d57f0 = 1) with the tint 0x4d57f4 = grey (fade * 96) >> 8,
 * and the scroll bytes moved by 0x4b249c per frame since 0x4b24a4 (1 when
 * that was -1 or not 1..5 frames ago). Returns 1. The lock's outputs start
 * at 0 here (stack leftovers in the original if the lock fails without
 * writing them). ===== */
int32_t GAMEAPI warp_texture_update(int32_t fade)
{
    uint32_t pal[0x100];
    uint32_t bits = 0, bpp = 0, pitch = 0;
    uint32_t c;
    int32_t frames;
    uint8_t step;

    if (U8(g_config, 0x29) == 0 || G32(0x4b24a0) == 0xffffffff)
        return 1;
    if (tex_lock_at(G32(0x4b24a0), 1, &bits, pal, &bpp, &pitch) != 0) {
        if (bpp == 8 || bpp == 0x10)
            warp_texture_render(S8(0x4d57f8, 0), S8(0x4d57f9, 0), bits, (int32_t)pitch, (int32_t)bpp);
        memcpy(pal, (const void *)0x4d4ba0, 0x100);
        tex_unlock_at(G32(0x4b24a0), 1, pal);
    }
    G32(0x4d57f0) = 1;
    c = ((uint32_t)fade * 0x60) >> 8;
    G32(0x4d57f4) = (((c << 8) | c) << 8) | c;
    frames = GI32(0x4be3e8) - GI32(0x4b24a4);
    if (G32(0x4b24a4) == 0xffffffff || frames < 1 || frames > 5)
        frames = 1;
    G32(0x4b24a4) = G32(0x4be3e8);
    step = (uint8_t)(G32(0x4b249c) * (uint32_t)frames);
    G8(0x4d57f8) = (uint8_t)(G8(0x4d57f8) - step);
    G8(0x4d57f9) = (uint8_t)(G8(0x4d57f9) + step);
    return 1;
}

/* The warp picture's texel for a table entry pair (lo, hi): the 16-bit
 * index ((lo + du) << 8) + dv plus twice the same with its low byte
 * replaced by hi (hand asm in the original). */
static uint8_t pr_warp_texel(uint32_t lo, uint32_t hi, int32_t du, int32_t dv)
{
    uint32_t a = ((lo + (uint32_t)du) << 8) + (uint32_t)dv;
    uint32_t b = (((a & 0xffffff00u) | hi) << 1);
    uint16_t i = (uint16_t)((uint16_t)a + (uint16_t)b);

    return G8(G32(0x4d57c8 + 0x14) + i);
}

/* ===== 0x47d6d0 (hand asm in the original): the warp texture, 128 rows
 * of 128 texels from the table *0x4d57ec through the picture
 * *0x4d57dc, scrolled by du / dv: bytes at 8 bits per pixel, else words
 * through the palette 0x4d51c4. The original leaves 0x80 in eax (nothing
 * reads it). ===== */
void GAMEAPI warp_texture_render(int32_t du, int32_t dv, uint32_t bits, int32_t pitch, int32_t bpp)
{
    uint32_t t = G32(0x4d57ec), r, k;

    if (bpp == 8) {
        uint8_t *d = PTR(bits);

        for (r = 0; r < 0x80; r++) {
            for (k = 0; k < 0x80; k++) {
                *d++ = pr_warp_texel(U8(t, 0), U8(t, 1), du, dv);
                t += 2;
            }
            d += pitch - 0x80;
        }
        return;
    }
    {
        uint16_t *d = (uint16_t *)(uintptr_t)bits;

        for (r = 0; r < 0x80; r++) {
            for (k = 0; k < 0x80; k++) {
                *d++ = G16(0x4d51c4 + 2 * (uint32_t)pr_warp_texel(U8(t, 0), U8(t, 1), du, dv));
                t += 2;
            }
            d = (uint16_t *)((uint8_t *)d + pitch - 0x100);
        }
    }
}

/* ===== 0x47d7f0: ICE.BMP into 0x4d51a0; its 64-entry palette byte-swapped
 * into 0x4d54c4 and converted with the 64 before it (0x41e1f0: 128 entries
 * to 0x4d4fa0); the warp texture locked (0x428c30), the picture drawn in
 * its right half (0x47d8f0 at bits + 128 texels), the palette into the
 * lock buffer's second 256 bytes, unlocked. The lock's outputs start at 0
 * here (stack leftovers in the original if the lock fails without writing
 * them). ===== */
void GAMEAPI ice_texture_load(void)
{
    uint32_t pal[0x100];
    uint32_t bits = 0, bpp = 0, pitch = 0;
    uint32_t s, d;

    bmp_load_at(0x4b24a8, 0x4d51a0, 1);
    s = G32(0x4d51b0) + 1;
    for (d = 0x4d54c5; d < 0x4d55c5; d += 4) {
        G8(d - 1) = U8(s, 1);
        G8(d) = U8(s, 0);
        G8(d + 1) = U8(s, -1);
        s += 0x10;
    }
    pixels_rgb_convert_at(0x4d53c4, 0x80, 0x4d4fa0, 1);
    if (tex_lock_at(G32(0x4b24a0), 1, &bits, pal, &bpp, &pitch) == 0)
        return;
    if (bpp == 8)
        ice_texture_render(bits + 0x80, (int32_t)pitch, 8);
    else if (bpp == 0x10)
        ice_texture_render(bits + 0x100, (int32_t)pitch, 0x10);
    memcpy(&pal[0x40], (const void *)0x4d54c4, 0x100);
    tex_unlock_at(G32(0x4b24a0), 1, pal);
}

/* ===== 0x47d8f0 (hand asm in the original): ICE.BMP's 128 x 128 pixels
 * (*0x4d51b4), each (p >> 2) + 0x40: bytes at 8 bits per pixel, words
 * through 0x4d4fa0 at 16, nothing otherwise. ===== */
void GAMEAPI ice_texture_render(uint32_t bits, int32_t pitch, int32_t bpp)
{
    uint32_t s = G32(0x4d51b4), r, k;

    if (bpp == 8) {
        uint8_t *d = PTR(bits);

        for (r = 0; r < 0x80; r++) {
            for (k = 0; k < 0x80; k++)
                *d++ = (uint8_t)((G8(s++) >> 2) + 0x40);
            d += pitch - 0x80;
        }
    } else if (bpp == 0x10) {
        uint16_t *d = (uint16_t *)(uintptr_t)bits;

        for (r = 0; r < 0x80; r++) {
            for (k = 0; k < 0x80; k++)
                *d++ = G16(0x4d4fa0 + 2 * (uint32_t)(uint8_t)((G8(s++) >> 2) + 0x40));
            d = (uint16_t *)((uint8_t *)d + pitch - 0x100);
        }
    }
}

/* ===== 0x47d980: the warp overlay's set-up: one of eight pictures
 * (TW1..TW8.BMP, by the next random number of the seed 0x4b11dc) with its
 * power for the tables (0x47d400 into a 0x10004-byte block 0x476b80), a
 * 256 x 256 texture (0x428c80 / 0x428c90; -1 and returns 0 on failure);
 * the picture's pixels >> 2, its palette byte-swapped into 0x4d4ba0 and
 * converted (0x41e1f0 to 0x4d51c4), the first frame (0x47d5a0) and the ice
 * (0x47d7f0). Returns 1. ===== */
int32_t GAMEAPI warp_init(void)
{
    static const uint32_t names[8] = {0x4b24e8, 0x4b24e0, 0x4b24d8, 0x4b24d0,
                                      0x4b24c8, 0x4b24c0, 0x4b24b8, 0x4b24b0};
    uint32_t power[8] = {0x3d428f5c, 0x3d4ccccd, 0x3d4ccccd, 0x3d570a3d,
                         0x3d3851ec, 0x3d3851ec, 0x3d4ccccd, 0x3d3851ec};
    uint32_t seed, k, p, s, d;

    G32(0x4b249c) = 1;
    seed = (G32(0x4b11dc) * 0x41a7) & 0x7fffffff;
    G32(0x4b11dc) = seed;
    k = (((seed >> 15) * 0x7fff) >> 24) & 7;
    G32(0x4d57ec) = (uint32_t)mem_alloc_at(0x10004);
    warp_tables_build(power[k]);
    bmp_load_at(names[k], 0x4d57c8, 1);
    G32(0x4b24a0) = (uint32_t)tex_handle_new_at();
    if (tex_upload_at(G32(0x4b24a0), 0x100, 0x100, 0, 0, 1) == 0) {
        G32(0x4b24a0) = 0xffffffff;
        return 0;
    }
    p = G32(0x4d57dc);
    for (k = 0; k < 0x10000; k++) {
        G8(p) = (uint8_t)(G8(p) >> 2);
        p++;
    }
    s = G32(0x4d57d8) + 1;
    for (d = 0x4d4ba1; d < 0x4d4ca1; d += 4) {
        G8(d - 1) = U8(s, 1);
        G8(d) = U8(s, 0);
        G8(d + 1) = U8(s, -1);
        s += 0x10;
    }
    pixels_rgb_convert_at(0x4d4ba0, 0x80, 0x4d51c4, 1);
    G32(0x4d57f0) = 0;
    warp_texture_update(0);
    ice_texture_load();
    return 1;
}

/* ===== 0x47db20: the warp overlay, when config +0x29, 0x4d57f0 and the
 * texture are on: the tint 0x4d57f4 (0x42bfc0) and the full-screen
 * texture flash (0x42c040, priority 1, the handle's low word). ===== */
void GAMEAPI warp_overlay_draw(void)
{
    if (U8(g_config, 0x29) == 0 || G32(0x4d57f0) == 0 || G32(0x4b24a0) == 0xffffffff)
        return;
    screen_tint_add_at(G32(0x4d57f4), 0);
    screen_flash_set_at(1, G16(0x4b24a0), 0x101, 0x17f, 0x7f01, 0x7f7f);
}

/* ===== 0x47db80: the ice overlay (config +0x32, texture on): the flash
 * with the texture's right half (priority 3). ===== */
void GAMEAPI ice_overlay_draw(void)
{
    if (U8(g_config, 0x32) == 0 || G32(0x4b24a0) == 0xffffffff)
        return;
    screen_flash_set_at(3, G32(0x4b24a0), 0x181, 0x1ff, 0x7f81, 0x7fff);
}

/* ===== 0x47dca0: a light sprite (0x5d6e00[index]) off the grid's second
 * list (0x4612a0). ===== */
void GAMEAPI light_sprite_release(int32_t index)
{
    grid_unlink2_at(G32(0x5d6e00 + 4 * (uint32_t)index));
}

/* ===================================================================== */
/* The sprites. */

/* ===== 0x47dd70: the sprites of the visible track: the view axis
 * (0x47ee90); per list record (count, then 8 bytes: section, first, count,
 * step + 1; the slot index in 0x5d91c0 per record) every step-th entry of
 * the section's index table (*(0x6bfac0[section]) + 0x48) gives a range of
 * objects (0x6bfae4); each object's word +8 in the 12-byte table *0x6bfaf8:
 * high half a grid chain (0x4613e0) whose sprites are drawn by type (2:
 * 0x47e1b0, 5: 0x47e020, 0xd: 0x47e220, 0xe: 0x47e2a0, 0x17 / 0x1b / 0x1c
 * / 0x1e: 0x47e340; 8 / 9 / 0x22 kept for later in 0x5d6600, at most 300),
 * low half appended to 0x5d7040 (words) with the slot's 0x5d7540 value in
 * 0x5d6ac0. Then the counts (0x4d5804, the word list's 0) and the glare
 * sprites (0x47eaa0). ===== */
void GAMEAPI sprites_draw(const uint8_t *list)
{
    uint32_t nwords = 0, nlater = 0;
    uint32_t objects, n;
    const uint8_t *rec;
    uint32_t idx = 0x5d91c0;

    view_side_axis_update();
    objects = G32(0x6bfaf8);
    n = U32(list, 0);
    rec = list + 4;
    if (n != 0) {
        do {
            uint32_t sh = (uint32_t)(uint16_t)(U16(rec, 6) - 1) & 31;
            uint32_t step = 1u << sh;
            uint32_t start = U16(rec, 2);
            uint32_t end = (uint16_t)((uint16_t)((uint32_t)U16(rec, 4) << sh) + start);
            uint32_t slot = 0x5d7540 + 4 * G32(idx);
            uint32_t table = U32(U32(G32(0x6bfac0), 4 * (uint32_t)U16(rec, 0)), 0x48);
            uint32_t prev = U16(table, 2 * start);
            uint32_t obj = G32(0x6bfae4) + 2 * prev;
            uint32_t u = start;

            if (u < end) {
                uint32_t np = table + 2 * (u + step);

                do {
                    uint32_t next = G16(np);

                    if (prev < next) {
                        uint32_t count = next - prev;

                        do {
                            uint32_t v = U32(objects, 12 * (uint32_t)G16(obj) + 8);

                            if (v != 0) {
                                if (v & 0xffff0000) {
                                    uint8_t *o = grid_record_at(v >> 16);
                                    uint32_t later = 0x5d6600 + 4 * nlater;

                                    do {
                                        switch (U32(o, 0)) {
                                        case 2:
                                            sprite_marker_draw(o);
                                            break;
                                        case 5:
                                            sprite_line_draw(o);
                                            break;
                                        case 8:
                                        case 9:
                                        case 0x22:
                                            if ((int32_t)later < 0x5d6ab0) {
                                                G32(later) = A(o);
                                                nlater++;
                                                later += 4;
                                            }
                                            break;
                                        case 0xd:
                                            light_sprite_draw(o);
                                            break;
                                        case 0xe:
                                            sprite_puff_draw(o);
                                            break;
                                        case 0x17:
                                        case 0x1b:
                                        case 0x1c:
                                        case 0x1e:
                                            sprite_billboard_draw(o);
                                            break;
                                        default:
                                            break;
                                        }
                                        o = grid_record_at(U16(o, 4));
                                    } while (o != 0);
                                }
                                if (v & 0xffff) {
                                    G16(0x5d7040 + 2 * nwords) = (uint16_t)v;
                                    G32(0x5d6ac0 + 4 * nwords) = G32(slot);
                                    nwords++;
                                }
                            }
                            obj += 2;
                        } while (--count != 0);
                    }
                    np += 2 * step;
                    u += step;
                    slot += 4;
                    prev = next;
                } while (u < end);
            }
            idx += 4;
            rec += 8;
        } while (--n != 0);
    }
    G32(0x4d5804) = nlater;
    G16(0x5d7040 + 2 * nwords) = 0;
    sprites_glare_draw();
}

/* ===== 0x47e020 (type 5): the screen window 0x6ccd64..70 widened by
 * *0x6cd418, the two ends (vectors from the high halves of +0x4c..54, and
 * the words +0x64 / +0x68) transformed into the records +0x18 / +0x34,
 * the window put back (fadd / fsub, as the original); depth (+0x1c fistp)
 * >> 5 above 1 and not both off the same edge: the packet +0x10 into the
 * ordering table. The vector's last word is a stack leftover in the
 * original (0 here). ===== */
void GAMEAPI sprite_line_draw(uint8_t *s)
{
    uint32_t d = G32(0x6cd418);
    int32_t z;

    G32(0x6ccd64) = pr_add(G32(0x6ccd64), d);
    G32(0x6ccd68) = pr_sub(G32(0x6ccd68), d);
    G32(0x6ccd6c) = pr_add(G32(0x6ccd6c), d);
    G32(0x6ccd70) = pr_sub(G32(0x6ccd70), d);
    G32(0x6ccc64) = (uint32_t)(uint16_t)(I32(s, 0x4c) >> 16) | ((uint32_t)I32(s, 0x50) >> 16 << 16);
    G32(0x6ccc68) = (uint16_t)(I32(s, 0x54) >> 16);
    pr_transform_project(s + 0x18);
    G32(0x6ccc64) = U32(s, 0x64);
    G32(0x6ccc68) = U32(s, 0x68);
    pr_transform_project(s + 0x34);
    z = pr_fist(U32(s, 0x1c)) >> 5;
    G32(0x6ccd64) = pr_sub(G32(0x6ccd64), d);
    G32(0x6ccd68) = pr_add(G32(0x6ccd68), d);
    G32(0x6ccd6c) = pr_sub(G32(0x6ccd6c), d);
    G32(0x6ccd70) = pr_add(G32(0x6ccd70), d);
    if (z <= 1)
        return;
    z = pr_depth_curve(z);
    if ((U32(s, 0x18) & U32(s, 0x34)) != 0)
        return;
    pr_ot_link((uint32_t)z, A(s + 0x10));
}

/* ===== 0x47e1b0 (type 2): colour *0x4af4b8, level 0xff; the quad
 * (0x47e5f0) of size +0x16 << 10, mode 10, flags 0x80000001, with the
 * sprite for its fade. ===== */
void GAMEAPI sprite_marker_draw(uint8_t *s)
{
    pr_colour_set(G32(0x4af4b8), 0xff);
    sprite_quad_build((const uint32_t *)(s + 0x10), (int32_t)((uint32_t)U8(s, 0x16) << 10), s + 0x18, 10,
                      0x80000001, s);
}

/* ===== 0x47e220 (type 0xd, light): colour +0x108, level +0x10b; the
 * rotated quad (mode 0x10006) of size +0x16 << 8. ===== */
void GAMEAPI light_sprite_draw(uint8_t *s)
{
    pr_colour_set(U32(s, 0x108), U8(s, 0x10b));
    sprite_quad_build((const uint32_t *)(s + 0x10), (int32_t)((uint32_t)U8(s, 0x16) << 8), s + 0x18, 0x10006,
                      0x80000000, 0);
}

/* ===== 0x47e2a0 (type 0xe): colour 0x505050, level 0x50; the quad at the
 * high halves of +0x10..18 (the vector's fourth word is a stack leftover
 * in the original, 0 here), size +0x2d << 8, mode 2, packet +0x30. ===== */
void GAMEAPI sprite_puff_draw(uint8_t *s)
{
    uint32_t v[2];

    pr_colour_set(0x505050, 0x50);
    v[0] = (uint32_t)(uint16_t)(I32(s, 0x10) >> 16) | ((uint32_t)I32(s, 0x14) >> 16 << 16);
    v[1] = (uint16_t)(I32(s, 0x18) >> 16);
    sprite_quad_build(v, (int32_t)((uint32_t)U8(s, 0x2d) << 8), s + 0x30, 2, 0x80000000, 0);
}

/* ===== 0x47e340 (types 8, 9, 0x17, 0x1b, 0x1c, 0x1e, 0x22): colour +0x98,
 * level +0x9b; the texture record (*0x6c1e14) of frame +0xa4 + byte +0xa6
 * into the packet (+0x10: +0x20, +0x3c, +0x58, +0x74), the frame byte
 * stepped (up to +0xa7, then 0) when 0x6c0a50 bit 0 and not 0x4d1198;
 * type 8: the flat sprite (0x47e4c0); else at the high halves of
 * +0x88..90: +0xa2 = 0x10 a streak (0x47ef30), else the quad (mode 3, or 1
 * for 0x8000; size +0x95 << 8). The vector's fourth word is the high half
 * of the address s + 0x98 (the original's stack slot held it). ===== */
void GAMEAPI sprite_billboard_draw(uint8_t *s)
{
    uint32_t a4, tex, frame, mode;
    uint8_t *pkt = s + 0x10;
    uint16_t v[4];
    int16_t kind;

    pr_colour_set(U32(s, 0x98), U8(s, 0x9b));
    a4 = U32(s, 0xa4);
    frame = (a4 >> 16) & 0xff;
    tex = G32(0x6c1e14) + (((a4 & 0xffff) + frame) << 4);
    if ((G8(0x6c0a50) & 1) != 0 && G32(0x4d1198) == 0) {
        if (frame + 1 < (a4 >> 24))
            U8(s, 0xa6) = (uint8_t)(frame + 1);
        else
            U8(s, 0xa6) = 0;
    }
    U32(pkt, 0x20) = U32(tex, 0);
    U32(pkt, 0x3c) = U32(tex, 4);
    U16(pkt, 0x58) = U16(tex, 8);
    U16(pkt, 0x74) = U16(tex, 10);
    if (U32(s, 0) == 8) {
        sprite_flat_insert(pkt);
        return;
    }
    v[0] = (uint16_t)(I32(s, 0x88) >> 16);
    v[1] = (uint16_t)(I32(s, 0x8c) >> 16);
    v[2] = (uint16_t)(I32(s, 0x90) >> 16);
    v[3] = (uint16_t)((uint32_t)A(s + 0x98) >> 16);
    kind = S16(s, 0xa2);
    mode = kind == (int16_t)0x8000 ? 1 : 3;
    if (kind == 0x10) {
        sprite_streak_build((const int16_t *)v, 0x8000, pkt, 0);
        return;
    }
    sprite_quad_build((const uint32_t *)v, (int32_t)((uint32_t)U8(s, 0x95) << 8), pkt, mode, 0x80000000, 0);
}

/* ===== 0x47e4c0: a flat sprite packet: level 0xfff - (+0x76 >> 2)
 * (scaled by *0x6cd400 below 0x100; none beyond 4000, 0 below 0), code
 * 0x9a (0x47e550), linked at ordering-table slot +0x5a. ===== */
void GAMEAPI sprite_flat_insert(uint8_t *pkt)
{
    int32_t f = GI32(0x6cd400);
    int32_t v = U16(pkt, 0x76) >> 2;
    uint32_t slot = U16(pkt, 0x5a);

    if (f < 0x100)
        v = (f * v) >> 8;
    if (v < 0)
        v = 0;
    else if (v > 4000)
        return;
    sprite_colour_fill((uint32_t)(0xfff - v), 0x9a, (uint32_t *)(pkt + 4), pkt + 0x22);
    pr_ot_link(slot, A(pkt));
}

/* ===== 0x47e550: a packet's colour word (code byte on top): with 0x6ccd84
 * bit 0 the colour registers >> 4 and the alpha byte (level * 0x6ccc60) >>
 * 16 (>> 17 with bit 2); otherwise each register * ((level * 0x6ccc60) >>
 * 12) >> 16. ===== */
void GAMEAPI sprite_colour_fill(uint32_t level, uint32_t code, uint32_t *rgb, uint8_t *alpha)
{
    uint32_t c = code << 24;

    if (G32(0x6ccd84) & 1) {
        uint32_t a;

        c |= ((G32(0x6ccc3c) >> 4) & 0xff) << 16;
        c |= ((G32(0x6ccc38) >> 4) & 0xff) << 8;
        c |= (G32(0x6ccc34) >> 4) & 0xff;
        *rgb = c;
        a = (G32(0x6ccc60) * level) >> 16;
        if (G32(0x6ccd84) & 4)
            a >>= 1;
        *alpha = (uint8_t)a;
    } else {
        uint32_t m = (G32(0x6ccc60) * level) >> 12;

        c |= (((G32(0x6ccc3c) * m) >> 16) & 0xff) << 16;
        c |= (((G32(0x6ccc38) * m) >> 16) & 0xff) << 8;
        c |= ((G32(0x6ccc34) * m) >> 16) & 0xff;
        *rgb = c;
    }
}

/* ===== 0x47e5f0: a sprite quad packet (0x84 bytes) at a vector (two
 * words to the GTE 0x6ccc64), transformed with depth (*0x6ccd7c, record
 * z / x / y at +4 / +8 / +0xc) and projected; depth z (fistp) less 32 *
 * the low word of mode (+32) must stay above that; size (0x6cce44 * size
 * >> 8) / depth > 0 is the half width, the half height that >> or << the
 * flags' low word; the level 0xfff - depth / 4 (with a sprite: from its
 * +0x17 and the depth, folded above +0x17 * 2 + 0x44c), scaled by
 * 0x6cd404 above 0x100, at least 0x32, at most 0xfff. Corners on screen
 * (packed words against *0x6cd478) or none: axis-aligned, or for a mode
 * with a high word rotated by the screen position (0x47e980). Colour
 * (0x47e550, code 0x98 | flags >> 30), +8 = 0x40, the depths, linked at
 * the depth slot. ===== */
void GAMEAPI sprite_quad_build(const uint32_t *pos, int32_t size, uint8_t *pkt, uint32_t mode, uint32_t flags,
                               const uint8_t *sprite)
{
    uint32_t rec[11];
    int32_t zl = GI32(0x6cd478);
    uint32_t lo = mode & 0xffff, scaled, w, h, screen, zr;
    int32_t depth, level, sh;

    G32(0x6ccc64) = pos[0];
    G32(0x6ccc68) = pos[1];
    if (pr_transform_depth_project(rec))
        return;
    scaled = (G32(0x6cce44) * (uint32_t)size) >> 8;
    depth = pr_fist(rec[1]) - (int32_t)(lo << 5) + 0x20;
    if (depth <= (int32_t)(lo << 5))
        return;
    w = scaled / (uint32_t)depth;
    if (w == 0)
        return;
    screen = ((uint32_t)pr_fist(pr_sub(rec[3], G32(0x6ccd6c))) << 16) |
             (uint32_t)pr_fist(pr_sub(rec[2], G32(0x6ccd64)));
    if (sprite != 0) {
        int32_t b = (int32_t)U8(sprite, 0x17) << 1;
        int32_t lim = b + 0x44c;

        level = ((depth - 0x140) >> 2) + b + 0x352;
        if (level > lim)
            level = 2 * lim - (level >> 1);
    } else {
        level = 0xfff - (depth >> 2);
    }
    if (GI32(0x6cd404) > 0x100)
        level = (GI32(0x6cd404) * level) >> 8;
    if (level < 0x32)
        return;
    if (level > 0xfff)
        level = 0xfff;
    sh = (int16_t)flags;
    if (sh < 0)
        h = w << ((uint32_t)-sh & 31);
    else
        h = w >> ((uint32_t)sh & 31);
    if (mode & 0xffff0000) {
        int32_t q = 0x10000 / GI32(0x4acc38);
        uint32_t a, b, p0, p1, p2, p3, t;
        int32_t angle = (((int32_t)(int16_t)screen * q) >> 8) + (((int32_t)screen >> 16) * q >> 8);
        uint32_t fal, fbl, fah, fbh;

        sprite_corners_rotate((uint32_t)angle, (int32_t)w, (int32_t)h, &a, &b);
        p0 = screen - a;
        p1 = a + screen;
        p2 = screen - b;
        p3 = b + screen;
        if ((p3 & p2 & p1 & p0) & 0x80008000)
            return;
        t = (p3 - (uint32_t)zl) | (p2 - (uint32_t)zl) | (p1 - (uint32_t)zl) | (p0 - (uint32_t)zl);
        if ((int32_t)t > 0 || (t & 0x8000) == 0)
            return;
        fal = pr_filds((int16_t)a);
        fbl = pr_filds((int16_t)b);
        fah = pr_filds((int16_t)((int32_t)a >> 16));
        fbh = pr_filds((int16_t)((int32_t)b >> 16));
        U32(pkt, 0x10) = pr_sub(rec[2], fal);
        U32(pkt, 0x2c) = pr_add(fbl, rec[2]);
        U32(pkt, 0x48) = pr_sub(rec[2], fbl);
        U32(pkt, 0x64) = pr_add(fal, rec[2]);
        U32(pkt, 0x14) = pr_sub(rec[3], fah);
        U32(pkt, 0x30) = pr_add(fbh, rec[3]);
        U32(pkt, 0x4c) = pr_sub(rec[3], fbh);
        U32(pkt, 0x68) = pr_add(fah, rec[3]);
    } else {
        uint32_t e = (h << 16) | w, t, fw, fh;

        if ((e + screen) & 0x80008000)
            return;
        t = screen - e - (uint32_t)zl;
        if ((int32_t)t > 0 || (t & 0x8000) == 0)
            return;
        fw = pr_fild((int32_t)w);
        fh = pr_fild((int32_t)h);
        U32(pkt, 0x48) = U32(pkt, 0x10) = pr_sub(rec[2], fw);
        U32(pkt, 0x30) = U32(pkt, 0x14) = pr_sub(rec[3], fh);
        U32(pkt, 0x64) = U32(pkt, 0x2c) = pr_add(fw, rec[2]);
        U32(pkt, 0x68) = U32(pkt, 0x4c) = pr_add(fh, rec[3]);
    }
    zr = (uint32_t)pr_depth_curve(depth >> 5);
    sprite_colour_fill((uint32_t)level, (flags >> 30) | 0x98, (uint32_t *)(pkt + 4), pkt + 0x22);
    U32(pkt, 8) = 0x40;
    U32(pkt, 0x60) = U32(pkt, 0x44) = U32(pkt, 0x28) = U32(pkt, 0xc) = rec[1];
    pr_ot_link(zr, A(pkt));
}

/* ===== 0x47e980: the two corner offsets (packed y << 16 | x words) of a
 * w x h quad turned by angle (the sine table 0x4b24f8, 256 steps). ===== */
void GAMEAPI sprite_corners_rotate(uint32_t angle, int32_t w, int32_t h, uint32_t *a, uint32_t *b)
{
    int32_t c = GS16(0x4b24f8 + 2 * ((angle + 0x40) & 0xff));
    int32_t s = GS16(0x4b24f8 + 2 * (angle & 0xff));
    int32_t ch = c * h, sw = s * w, cw = c * w, sh = s * h;

    *a = ((uint32_t)((sw + ch) >> 14) << 16) + (uint32_t)((cw - sh) >> 14);
    *b = ((uint32_t)((ch - sw) >> 14) << 16) + (uint32_t)(-(sh + cw) >> 14);
}

/* ===== 0x47ea00: the squared distance (in units of 4.0, per axis >> 18)
 * from the body's position (+0x34) to the nearest of the 0x4d5800 markers
 * (0x5d6400, 12 bytes each), at most 0x10000: its square root (0x427960)
 * * 4 as a fade tint (0x42bf20). ===== */
void GAMEAPI marker_proximity_tint(const uint8_t *body)
{
    int32_t x = I32(body, 0x34), y = I32(body, 0x38), z = I32(body, 0x3c);
    int32_t best = 0x10000, n = GI32(0x4d5800);
    uint32_t m = 0x5d6408;

    if (n > 0) {
        do {
            int32_t d = (GI32(m - 8) - x) >> 18, e;

            d *= d;
            if (d <= best) {
                e = (GI32(m) - z) >> 18;
                d += e * e;
                if (d <= best) {
                    e = (GI32(m - 4) - y) >> 18;
                    d += e * e;
                    if (d < best)
                        best = d;
                }
            }
            m += 12;
        } while (--n != 0);
        if (best > 0x10000)
            return;
    }
    fade_tint_add_at((uint32_t)fix_sqrt_table_at(best) << 2);
}

/* ===== 0x47eaa0: the glare sprites kept by 0x47dd70 (0x5d6600, 0x4d5804):
 * each at the high halves of +0x88..90 through 0x426cd0 (depth, screen
 * position) / 0x426dc0, depth above 0x60, half size +0x95 * 0x6cce44 /
 * depth above 1 (capped by 0x6cd40c * 40 >> 8 for type 8, * 60 >> 8
 * otherwise), on screen: kept (compacted in place, 0 ended), the corners
 * as floats (+ the screen offset 0x6ccd5c - 0x6ccd4c, 0x6ccd60 - 0x6ccd50)
 * into the record, depth - 64 at +0x86 and as floats, its slot at +0x6a,
 * the clipped area summed. Over the budget (config +0x68 * 0.4 * 256,
 * 0x80..0x300, times 0x6cd40c * 75 * 32 >> 16) every so many are skipped;
 * the rest drawn (0x47e340). The vector's fourth word is a stack leftover
 * in the original (0 here); so is the screen position before the first
 * 0x426dc0 that writes it (it skips the write for a clipped point; 0 here,
 * kept across sprites as in the original). ===== */
void GAMEAPI sprites_glare_draw(void)
{
    int32_t zl = GI32(0x6cd478);
    uint32_t fx = pr_sub(G32(0x6ccd5c), G32(0x6ccd4c));
    uint32_t fy = pr_sub(G32(0x6ccd60), G32(0x6ccd50));
    int32_t xmax = U16(g_race, 0xa82), ymax = U16(g_race, 0xa80);
    int32_t n = GI32(0x4d5804), i, acc = 0, skip = 0;
    uint32_t area = 0, out = 0x5d6600;
    uint32_t scr = 0;

    if (n == 0)
        return;
    {
        uint32_t t = G32(0x6cd40c);
        int32_t cap8 = (int32_t)((t * 40) >> 8), cap = (int32_t)((t * 60) >> 8);

        if (n > 0) {
            uint32_t in = 0x5d6600;

            for (i = n; i != 0; i--, in += 4) {
                uint8_t *s = PTR(G32(in));
                int32_t z, sz;
                uint32_t br, tl, tr, bl, e;

                G32(0x6ccc64) = (uint32_t)(uint16_t)(I32(s, 0x88) >> 16) | ((uint32_t)I32(s, 0x8c) >> 16 << 16);
                G32(0x6ccc68) = (uint16_t)(I32(s, 0x90) >> 16);
                __asm__ volatile("pushl %1\n\t"
                                 "movl $0x426cd0, %%eax\n\t"
                                 "call *%%eax\n\t"
                                 "addl $4, %%esp\n\t"
                                 "movl %%eax, %%edi\n\t"
                                 "testl $0x80000000, %%eax\n\t"
                                 "jne 1f\n\t"
                                 "movl $0x426dc0, %%eax\n\t"
                                 "call *%%eax\n"
                                 "1:"
                                 : "=D"(z)
                                 : "S"(&scr)
                                 : "eax", "ecx", "edx", "cc", "memory", X87);
                if (z < 0 || z <= 0x60)
                    continue;
                sz = (int32_t)((uint32_t)U8(s, 0x95) * G32(0x6cce44)) / z;
                if (sz <= 1)
                    continue;
                if (U32(s, 0) == 8) {
                    if (sz > cap8)
                        sz = cap8;
                } else if (sz > cap) {
                    sz = cap;
                }
                e = (uint32_t)sz * 0x10001;
                br = e + scr;
                if (br & 0x80008000)
                    continue;
                tl = scr - e;
                if ((int32_t)(tl - (uint32_t)zl) > 0 || ((tl - (uint32_t)zl) & 0x8000) == 0)
                    continue;
                z -= 0x40;
                tr = scr - (((uint32_t)sz << 16) - (uint32_t)sz);
                bl = (((uint32_t)sz << 16) - (uint32_t)sz) + scr;
                G32(out) = A(s);
                out += 4;
                U32(s, 0x20) = pr_add(pr_filds((int16_t)tl), fx);
                U32(s, 0x3c) = pr_add(pr_filds((int16_t)tr), fx);
                U32(s, 0x58) = pr_add(pr_filds((int16_t)bl), fx);
                U32(s, 0x74) = pr_add(pr_filds((int16_t)br), fx);
                U32(s, 0x24) = pr_add(pr_filds((int16_t)((int32_t)tl >> 16)), fy);
                U32(s, 0x40) = pr_add(pr_filds((int16_t)((int32_t)tr >> 16)), fy);
                U32(s, 0x5c) = pr_add(pr_filds((int16_t)((int32_t)bl >> 16)), fy);
                U32(s, 0x78) = pr_add(pr_filds((int16_t)((int32_t)br >> 16)), fy);
                {
                    uint32_t fz = pr_fild(z);
                    int32_t x0, y0, x1, y1;

                    U16(s, 0x86) = (uint16_t)z;
                    U32(s, 0x70) = U32(s, 0x54) = U32(s, 0x38) = U32(s, 0x1c) = fz;
                    U32(s, 0x18) = 0x40;
                    U16(s, 0x6a) = (uint16_t)pr_depth_curve(z >> 5);
                    x0 = (int32_t)(tl << 16) > 0 ? (int32_t)(tl << 16) >> 16 : 0;
                    y0 = (int32_t)(tl & 0xffff0000) > 0 ? (int32_t)tl >> 16 : 0;
                    x1 = (int16_t)br;
                    if (x1 >= xmax)
                        x1 = xmax;
                    y1 = (int32_t)br >> 16;
                    if (y1 >= ymax)
                        y1 = ymax;
                    area += (uint32_t)((y1 - y0) * (x1 - x0));
                }
            }
        }
    }
    G32(out) = 0;
    if (area != 0) {
        uint32_t f, lim, b;

        __asm__ volatile("flds %1\n\tfmuls 0x4a347c\n\tfmuls 0x4a33c0\n\tfstps %0"
                         : "=m"(f)
                         : "m"(U32(g_config, 0x68))
                         : "st");
        b = (uint32_t)pr_fist(f);
        if (b < 0x80)
            b = 0x80;
        else if (b > 0x300)
            b = 0x300;
        lim = (((G32(0x6cd40c) * 75) << 13) >> 8) * b >> 8;
        if (area > lim)
            skip = (int32_t)(0xff - (lim << 8) / area);
    }
    if (skip > 0) {
        for (i = 0; i < n; i++) {
            uint32_t s = G32(0x5d6600 + 4 * (uint32_t)i);

            if (s == 0)
                return;
            acc += skip;
            if (acc >= 0x100)
                acc -= 0x100;
            else
                sprite_billboard_draw(PTR(s));
        }
    } else {
        for (i = 0; i < n; i++) {
            uint32_t s = G32(0x5d6600 + 4 * (uint32_t)i);

            if (s == 0)
                return;
            sprite_billboard_draw(PTR(s));
        }
    }
}

/* ===== 0x47ee90: the view's sideways axis for the streaks: the GTE
 * matrix's row 0 x / z words (0x426480) << 4 as a vector (y 0), its length
 * (0x480cd0) above 0x100 normalised (0x486e90) and 0x5d65e0 = 1, else 0;
 * the vector >> 4 as words at 0x5d6de0. ===== */
void GAMEAPI view_side_axis_update(void)
{
    int16_t m[16];
    int32_t v[3];
    int32_t len;

    gte_matrix_save_at(m);
    v[1] = 0;
    v[0] = (int32_t)m[0] << 4;
    v[2] = (int32_t)m[2] << 4;
    len = vec_length_at(v);
    if (len > 0x100) {
        vec_div_at(v, len);
        G32(0x5d65e0) = 1;
    } else {
        G32(0x5d65e0) = 0;
    }
    G16(0x5d6de0) = (uint16_t)(v[0] >> 4);
    G16(0x5d6de2) = (uint16_t)(v[1] >> 4);
    G16(0x5d6de4) = (uint16_t)(v[2] >> 4);
}

/* ===== 0x47ef30: a streak packet (when 0x5d65e0): the point and two more
 * (along the view axis 0x5d6de0 by scale >> 21, and up by scale >> or <<
 * shift >> 9) transformed (*0x6ccd7c / *0x6ccd80; none when the first's
 * depth is negative); depth (fistp) above 0xe0, level 0xfff - depth / 4
 * (scaled by 0x6cd404 above 0x100, at least 0x32, at most 0xfff); corners
 * from the three screen points, colour code 0x9a, depths, linked at the
 * depth slot. The vectors' fourth words are stack leftovers in the
 * original (0 here). ===== */
struct pr_streak {
    uint32_t r1[6], r2[6], r3[6];
    uint32_t p2[2], p3[2];
};

void GAMEAPI sprite_streak_build(const int16_t *pos, int32_t scale, uint8_t *pkt, int32_t shift)
{
    struct pr_streak w;
    int32_t up, depth, level, slot;
    uint32_t x1, y1, dx2, dy2, dx3, dy3, t;
    int neg;

    if (G32(0x5d65e0) == 0)
        return;
    up = shift < 0 ? (int32_t)((uint32_t)scale << ((uint32_t)-shift & 31)) : scale >> (shift & 31);
    up >>= 9;
    w.p2[0] = (uint32_t)(uint16_t)(pos[0] + (int16_t)((GS16(0x5d6de0) * scale) >> 21)) |
              (uint32_t)(uint16_t)(pos[1] + (int16_t)((GS16(0x5d6de2) * scale) >> 21)) << 16;
    w.p2[1] = (uint16_t)(pos[2] + (int16_t)((GS16(0x5d6de4) * scale) >> 21));
    w.p3[0] = (uint32_t)(uint16_t)pos[0] | (uint32_t)(uint16_t)(pos[1] - (int16_t)up) << 16;
    w.p3[1] = (uint16_t)pos[2];
    G32(0x6ccc64) = U32(pos, 0);
    G32(0x6ccc68) = U32(pos, 4);
    __asm__ volatile("pushl %%esi\n\t"
                     "call *0x6ccd7c\n\t"
                     "addl $4, %%esp\n\t"
                     "testl $0x80000000, 4(%%esi)\n\t"
                     "jne 1f\n\t"
                     "call *0x6ccd80\n\t"
                     "movl 72(%%esi), %%eax\n\t"
                     "movl %%eax, 0x6ccc64\n\t"
                     "movl 76(%%esi), %%eax\n\t"
                     "movl %%eax, 0x6ccc68\n\t"
                     "leal 24(%%esi), %%eax\n\t"
                     "pushl %%eax\n\t"
                     "call *0x6ccd7c\n\t"
                     "call *0x6ccd80\n\t"
                     "addl $4, %%esp\n\t"
                     "movl 80(%%esi), %%eax\n\t"
                     "movl %%eax, 0x6ccc64\n\t"
                     "movl 84(%%esi), %%eax\n\t"
                     "movl %%eax, 0x6ccc68\n\t"
                     "leal 48(%%esi), %%eax\n\t"
                     "pushl %%eax\n\t"
                     "call *0x6ccd7c\n\t"
                     "addl $4, %%esp\n\t"
                     "call *0x6ccd80\n\t"
                     "xorl %%eax, %%eax\n\t"
                     "jmp 2f\n"
                     "1:\n\t"
                     "movl $1, %%eax\n"
                     "2:"
                     : "=a"(neg)
                     : "S"(&w)
                     : "ecx", "edx", "cc", "memory", X87);
    if (neg)
        return;
    depth = pr_fist(w.r1[1]);
    if (depth <= 0xe0)
        return;
    level = 0xfff - (depth >> 2);
    if (GI32(0x6cd404) > 0x100)
        level = (GI32(0x6cd404) * level) >> 8;
    if (level < 0x32)
        return;
    if (level > 0xfff)
        level = 0xfff;
    x1 = w.r1[2];
    y1 = w.r1[3];
    dx2 = pr_sub(w.r2[2], x1);
    dy2 = pr_sub(w.r2[3], y1);
    dx3 = pr_sub(w.r3[2], x1);
    dy3 = pr_sub(w.r3[3], y1);
    t = pr_sub(x1, dx2);
    U32(pkt, 0x10) = pr_add(t, dx3);
    U32(pkt, 0x2c) = pr_add_add(dx3, dx2, x1);
    U32(pkt, 0x48) = pr_sub(t, dx3);
    U32(pkt, 0x64) = pr_add_sub(dx2, x1, dx3);
    t = pr_sub(y1, dy2);
    U32(pkt, 0x14) = pr_add(t, dy3);
    U32(pkt, 0x30) = pr_add_add(dy3, dy2, y1);
    U32(pkt, 0x4c) = pr_sub(t, dy3);
    U32(pkt, 0x68) = pr_add_sub(dy2, y1, dy3);
    slot = pr_depth_curve(depth >> 5);
    sprite_colour_fill((uint32_t)level, 0x9a, (uint32_t *)(pkt + 4), pkt + 0x22);
    U32(pkt, 8) = 0x40;
    U32(pkt, 0x60) = U32(pkt, 0x44) = U32(pkt, 0x28) = U32(pkt, 0xc) = w.r1[1];
    pr_ot_link((uint32_t)slot, A(pkt));
}

/* ===================================================================== */
/* The cars' light trails. */

/* A trail packet colour word scaled by an intensity (bytes 0..2 * s >> 8,
 * byte 3 kept), raw with 0x6ccd84 bit 0; the intensity byte at +off. */
static void pr_trail_colour(uint8_t *pk, uint32_t off, uint32_t col, uint32_t s)
{
    if (G8(0x6ccd84) & 1) {
        U32(pk, off) = col;
    } else {
        U32(pk, off) = (col & 0xff000000) | (((((col >> 16) & 0xff) * s) >> 8) & 0xff) << 16 |
                       (((((col >> 8) & 0xff) * s) >> 8) & 0xff) << 8 | ((((col & 0xff) * s) >> 8) & 0xff);
    }
}

/* ===== 0x4821d0: per car slot (race +0x54d) fast enough (speed +0x654
 * above 0x370000: level ((speed - 0x370000) << 7) / 0x1e0000, at least 2,
 * at most 150), moving (+0x68 >> 18) and heading along its velocity
 * (normalised, 0x480c70; dot with the forward column +0xc8 / +0xce / +0xd4
 * at least 0xea6, 0x40d950): the seed 0x4b11d8 stepped, then for both
 * trails (car +0x14cc + 0x6d4 * k, mirrored sideways for the second) of 3
 * or more points: per point back from the head (12-point ring at +0x14)
 * two vertices across the trail (the width table 0x4b2aac +- 14 except at
 * the end, turned by the angle byte +0xa less 20 per point) transformed
 * (*0x6ccd78 / *0x6ccd80) into two local record rings, packet colours
 * (+0xa8, 0x84 apart) fading with the phase byte +8 (less 40 per point);
 * then per pair of points not all off the same edge the four records into
 * the packets (+0xa4) with the texture record 0x47 + index % 6 (from
 * *0x6c1eb0 +0x2e, table *0x6c1e14), linked at the nearer depth's slot.
 * The vectors' fourth words are stack leftovers in the original (0
 * here). ===== */
void GAMEAPI car_light_trails_draw(void)
{
    uint32_t ra[12][6], rb[12][6];
    uint32_t slot = 0x6bfb80;
    int32_t ci = 0;

    if (U8(g_race, 0x54d) == 0)
        return;
    do {
        uint8_t *car = PTR(G32(slot));
        int32_t lv = (int32_t)((uint32_t)(I32(car, 0x654) - 0x370000) << 7) / 0x1e0000;
        int32_t fwd[3], vel[3], k;

        if (lv >= 0x96)
            lv = 0x96;
        else if (lv < 2)
            goto next;
        if (((I32(car, 0x68) >> 18) * 3) == 0)
            goto next;
        fwd[0] = S16(car, 0xc8);
        fwd[1] = S16(car, 0xce);
        fwd[2] = S16(car, 0xd4);
        vel[0] = I32(car, 0x68);
        vel[1] = I32(car, 0x6c);
        vel[2] = I32(car, 0x70);
        vec_normalise_at(vel);
        if (vec_dot_at(fwd, vel) < 0xea6)
            goto next;
        G32(0x4b11d8) = (G32(0x4b11d8) * 0x41a7) & 0x7fffffff;
        for (k = 0; k < 2; k++) {
            uint8_t *tr;
            int32_t cnt, step, head, c0, t, i, j, level, off;
            uint8_t phase;
            int8_t ang;
            int32_t sx, sy, sz, ux, uy, uz;
            uint32_t widths, pk;

            car = PTR(G32(slot));
            tr = car + 0x14cc + 0x6d4 * k;
            cnt = I32(tr, 4);
            if (cnt < 3)
                continue;
            step = (lv << 8) / (cnt - 1);
            head = I32(tr, 0);
            phase = U8(tr, 8);
            t = lv + GS16(0x4b24f8 + 2 * (uint32_t)phase);
            if ((int32_t)(t & 0xfffffe00) >= 0x1fe00)
                c0 = 0xff;
            else if ((t >> 9) <= 0)
                c0 = 0;
            else
                c0 = t >> 9;
            ang = S8(tr, 0xa);
            if (k == 0) {
                sx = S16(car, 0xc4);
                sy = S16(car, 0xca);
                sz = S16(car, 0xd0);
            } else {
                sx = (int16_t)-S16(car, 0xc4);
                sy = (int16_t)-S16(car, 0xca);
                sz = (int16_t)-S16(car, 0xd0);
            }
            ux = S16(car, 0xc6);
            uy = S16(car, 0xcc);
            uz = S16(car, 0xd2);
            off = 0;
            level = lv;
            widths = 0x4b2aac;
            pk = A(tr) + 0xa8;
            for (i = 0; i < I32(tr, 4); i++) {
                uint8_t *p;
                int32_t wa, wb, sa, sb, x16, y16, z16, c1, l2;
                int16_t sn = GS16(0x4b24f8 + 2 * (int32_t)ang), cs = GS16(0x4b2578 + 2 * (int32_t)ang);

                head--;
                if (head < 0)
                    head = 0xb;
                p = tr + 12 * head;
                x16 = I32(p, 0x14) >> 16;
                y16 = I32(p, 0x18) >> 16;
                z16 = I32(p, 0x1c) >> 16;
                wa = GI32(widths) + off;
                sa = (sn * wa) >> 12;
                sb = (cs * wa) >> 12;
                G32(0x6ccc64) = (uint32_t)(uint16_t)(x16 + ((ux * sb) >> 16) + ((sx * sa) >> 16)) |
                                (uint32_t)(uint16_t)(y16 + ((uy * sb) >> 16) + ((sy * sa) >> 16)) << 16;
                G32(0x6ccc68) = (uint16_t)(z16 + ((uz * sb) >> 16) + ((sz * sa) >> 16));
                pr_transform_project(ra[head]);
                l2 = (level * 0x100 - step) >> 8;
                phase = (uint8_t)(phase - 0x28);
                c1 = l2 + (GS16(0x4b24f8 + 2 * (uint32_t)phase) >> 9);
                if (c1 >= 0xff)
                    c1 = 0xff;
                else if (c1 <= 0)
                    c1 = 0;
                wb = GI32(widths) - off;
                sa = (sn * wb) >> 12;
                sb = (cs * wb) >> 12;
                G32(0x6ccc64) = (uint32_t)(uint16_t)(x16 + ((ux * sb) >> 16) + ((sx * sa) >> 16)) |
                                (uint32_t)(uint16_t)(y16 + ((uy * sb) >> 16) + ((sy * sa) >> 16)) << 16;
                G32(0x6ccc68) = (uint16_t)(z16 + ((uz * sb) >> 16) + ((sz * sa) >> 16));
                pr_transform_project(rb[head]);
                pr_trail_colour(PTR(pk), 0, 0x8effa0a0, (uint32_t)c0);
                U8(pk, 0x1e) = (uint8_t)c0;
                pr_trail_colour(PTR(pk), 0x20, 0xffa0a0, (uint32_t)c0);
                U8(pk, 0x23) = (uint8_t)c0;
                pr_trail_colour(PTR(pk), 0x40, 0xffa0a0, (uint32_t)c1);
                U8(pk, 0x43) = (uint8_t)c1;
                pr_trail_colour(PTR(pk), 0x60, 0xffa0a0, (uint32_t)c1);
                U8(pk, 0x63) = (uint8_t)c1;
                ang = (int8_t)(ang - 0x14);
                pk += 0x84;
                level = l2;
                off = i >= I32(tr, 4) - 3 ? 0 : 0xe;
                c0 = c1;
                widths += 4;
            }
            j = 0;
            head = I32(tr, 0);
            pk = A(tr) + 0xa4;
            while (j < I32(tr, 4) - 2) {
                int32_t pv, za, zb, z;
                uint32_t tex;

                head--;
                if (head < 0)
                    head = 0xb;
                pv = head - 1;
                if (pv < 0)
                    pv = 0xb;
                tex = G32(0x6c1e14) + 16 * ((uint32_t)U16(G32(0x6c1eb0), 0x2e) + 0x47 + (uint32_t)(head % 6));
                if ((rb[pv][0] & rb[head][0] & ra[pv][0] & ra[head][0]) == 0) {
                    memcpy(PTR(pk + 0x08), ra[head], 24);
                    memcpy(PTR(pk + 0x28), rb[head], 24);
                    memcpy(PTR(pk + 0x48), ra[pv], 24);
                    memcpy(PTR(pk + 0x68), rb[pv], 24);
                    U32(pk, 0x20) = U32(tex, 0);
                    U32(pk, 0x40) = U32(tex, 4);
                    U16(pk, 0x60) = U16(tex, 8);
                    U16(pk, 0x80) = U16(tex, 10);
                    za = pr_fist(rb[head][1]);
                    zb = pr_fist(rb[pv][1]);
                    z = za > zb ? pr_fist(rb[head][1]) : pr_fist(rb[pv][1]);
                    z >>= 5;
                    if (z > 0) {
                        z -= 3;
                        if (z < 1)
                            z = 1;
                        else
                            z = pr_depth_curve(z);
                        pr_ot_link((uint32_t)z, pk);
                    }
                }
                pk += 0x84;
                j++;
            }
        }
    next:
        slot += 0x274;
        ci++;
    } while (ci < (int32_t)U8(g_race, 0x54d));
}
