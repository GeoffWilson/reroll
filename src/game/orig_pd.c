/* Group PD: the original's still-reachable functions in 0x42be20-0x42f6a0
 * (the end of the effects source file and the front end's text strings).
 *
 *  - screen tints: the camera-shake flash (0x42be20), the fade tint
 *    (0x42bf20), the tint accumulator (0x42bfc0), the flash quad's corner
 *    colours (0x42c040) and the full-screen tint quad (0x42c0a0);
 *  - the blast flare ring (0x42c420) and the shock wave rings (0x42c950),
 *    packets linked into the ordering tables;
 *  - the text file: the font table (0x42e050), the string buffers
 *    (0x42e2a0), the section reader (0x42e3d0, 0x42e4b0, 0x42e620,
 *    0x42e690) and the line reader (0x42e6d0);
 *  - the string objects (128 records of 0x14c bytes at *0x4be310, glyph
 *    corners at *0x4be304, packets at *0x4be308 / *0x4be30c): build
 *    (0x42f6a0, 0x42e810, 0x42e8f0), on / off / flags (0x42ea00, 0x42ea40,
 *    0x42eab0, 0x42f0b0), size and limits (0x42eae0, 0x42ebc0, 0x42ed10,
 *    0x42f030, 0x42f070), placement (0x42ed40, 0x42eed0), colours (0x42f0f0,
 *    0x42f2b0), clipping (0x42f2d0) and drawing (0x42f3c0).
 *
 * Float steps are the original's x87 instructions in inline asm (memory
 * operands, the same intermediates); integer logic is C. Several functions
 * hand the address of one of their own stack arguments to a callee that
 * writes it (as the original does): those parameters are written through
 * that address here too. Everything outside the group is called by
 * address. */
#include "../runtime.h"
#include "physics.h"

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

typedef int32_t(GAMEAPI *pd_fn0)(void);
typedef int32_t(GAMEAPI *pd_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *pd_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *pd_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *pd_fn7)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                 uint32_t g);

#define CALL0(a) (((pd_fn0)(a))())
#define CALL1(a, x) (((pd_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((pd_fn2)(a))(A(x), A(y)))
#define CALL3(a, x, y, z) (((pd_fn3)(a))(A(x), A(y), A(z)))
#define CALL7(a, x, y, z, w, v, u, t) (((pd_fn7)(a))(A(x), A(y), A(z), A(w), A(v), A(u), A(t)))

/* This group's functions (also in build/groups/pd_protos.txt for physics.h). */
void GAMEAPI shake_flash_tint(uint32_t car); /* 0x42be20 */
void GAMEAPI fade_tint_add(int32_t n); /* 0x42bf20 */
uint32_t GAMEAPI screen_tint_add(uint32_t colour); /* 0x42bfc0 */
void GAMEAPI screen_flash_set(int32_t prio, int32_t c0, int32_t c1, int32_t c2, int32_t c3, int32_t c4); /* 0x42c040 */
void GAMEAPI screen_tint_draw(uint32_t arg); /* 0x42c0a0 */
void GAMEAPI blast_flare_draw(void); /* 0x42c420 */
void GAMEAPI shock_waves_draw(void); /* 0x42c950 */
uint32_t GAMEAPI font_by_id(uint32_t id); /* 0x42e050 */
int32_t GAMEAPI text_buffers_alloc(void); /* 0x42e2a0 */
int32_t GAMEAPI text_entry_copy(uint8_t *dst, int32_t max, const char *name); /* 0x42e3d0 */
uint32_t GAMEAPI text_section_find(const char *name); /* 0x42e4b0 */
uint32_t GAMEAPI text_bytes_find(const uint8_t *pat, const uint8_t *text, uint32_t len, uint32_t n); /* 0x42e620 */
uint32_t GAMEAPI text_bytes_differ(const uint8_t *a, const uint8_t *b, uint32_t n); /* 0x42e690 */
int32_t GAMEAPI text_line_get(uint8_t *dst, int32_t max, const char *name, int32_t line); /* 0x42e6d0 */
int32_t GAMEAPI string_printf(uint32_t font, uint32_t colours, uint32_t flags, uint32_t spacing, const char *fmt,
                              ...); /* 0x42e810 */
int32_t GAMEAPI string_width_printf(uint32_t font, uint32_t colours, uint32_t flags, uint32_t spacing,
                                    const char *fmt, ...); /* 0x42e8f0 */
uint32_t GAMEAPI string_active(int32_t i); /* 0x42ea00 */
void GAMEAPI string_on(int32_t i, int32_t x, int32_t y, int32_t z, uint32_t flags); /* 0x42ea40 */
void GAMEAPI string_off(int32_t i); /* 0x42eab0 */
int32_t GAMEAPI string_line_range(int32_t i, int32_t line, uint32_t *start, uint32_t *end, int32_t *mark); /* 0x42eae0 */
int32_t GAMEAPI string_limits(int32_t i, int32_t line, int32_t *minx, int32_t *miny, int32_t *maxx,
                              int32_t *maxy); /* 0x42ebc0 */
int32_t GAMEAPI string_limits_all(int32_t i, int32_t *minx, int32_t *miny, int32_t *maxx, int32_t *maxy); /* 0x42ed10 */
int32_t GAMEAPI string_set_position(int32_t i, int32_t x, int32_t y, int32_t xmode, int32_t ymode); /* 0x42ed40 */
int32_t GAMEAPI string_set_position_j(int32_t i, int32_t x, int32_t mode); /* 0x42eed0 */
uint32_t GAMEAPI string_get_width(int32_t i); /* 0x42f030 */
uint32_t GAMEAPI string_get_height(int32_t i); /* 0x42f070 */
uint32_t GAMEAPI string_has_marks(int32_t i); /* 0x42f0b0 */
int32_t GAMEAPI string_colour_range(int32_t i, int32_t line, int32_t which, int32_t sel,
                                    const uint8_t *colour); /* 0x42f0f0 */
int32_t GAMEAPI string_colour_set(int32_t i, int32_t sel, const uint8_t *colour); /* 0x42f2b0 */
void GAMEAPI string_clip_x(int32_t i, int32_t n, int32_t mode); /* 0x42f2d0 */
int32_t GAMEAPI strings_draw(uint32_t ot, uint32_t unused, uint32_t which); /* 0x42f3c0 */
int32_t GAMEAPI string_build(uint32_t font_id, const uint8_t *s, uint32_t colours, uint32_t flags, uint32_t spacing,
                             uint32_t va); /* 0x42f6a0 */

#define g_race G32(0x4b1960)
#define g_strings G32(0x4be310) /* 128 string records, 0x14c bytes */
#define g_glyphs G32(0x4be304)  /* 2048 glyphs x 4 corners (s16 x, y, flag, -) */
#define g_glyph_count G16(0x4be2fc)
#define REC(i) (g_strings + (uint32_t)(i) * 0x14c)

#define crt_malloc(n) ((uint32_t)CALL1(0x4982c2, n))
#define crt_strncpy(d, s, n) CALL3(0x498350, d, s, n)
#define debug_log(fmt, s) CALL2(0x496340, fmt, s)
#define sin_table(a) CALL1(0x480b10, a)
#define cos_table(a) CALL1(0x480bc0, a)
#define screen_idle(a) CALL1(0x479ae0, a)
#define shake_tint_net(p) CALL1(0x485050, p)
#define dword_store(p, v) CALL2(0x4290a0, p, v)
#define ot_link_chain(ot, a, b) CALL3(0x429080, ot, a, b)
#define matrix_from_angles(a, m) CALL2(0x426f80, a, m)
#define matrix_identity(m) CALL1(0x4264f0, m)
#define call_text_slot_free() CALL0(0x42fd10)
#define call_text_tab_next(x) ((uint32_t)CALL1(0x42fd60, x))
#define call_number_parse(s, v) CALL2(0x42fcd0, s, v)
#define call_number_digits(buf, v) CALL2(0x42fc40, buf, v)
#define call_glyph_place(f, c, col, y, x, h, fl) CALL7(0x42fd90, f, c, col, y, x, h, fl)

/* Every x87 register, for asm blocks that call out or use the FPU stack. */
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* ---- x87 helpers: the original's instructions ---- */

/* fild dword; fstp dword: an integer to float, rounded by the control word. */
static inline uint32_t pd_from_int(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fld a; fadd b; fstp. */
static inline uint32_t pd_add(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fdiv b; fmul c; fstp: a / b * c. */
static inline uint32_t pd_div_mul(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfmuls %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fistp dword: a rounded by the control word. */
static inline int32_t pd_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld a; fcomp b; fnstsw ax: the status word's high byte. */
static inline uint32_t pd_cmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 0xff;
}

#define ABOVE(sw) (((sw) & 0x41) == 0) /* greater (ordered) */

/* fld dword a; call _ftol (0x497a80, truncating): the low dword. */
static inline int32_t pd_ftol(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tmovl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(a)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
}

/* The transform pointer *0x6ccd78 (record out) then the projection
 * *0x6ccd80, which takes the transform's st(0). */
static inline void pd_transform_project(uint32_t *out)
{
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tcall *0x6ccd80"
                     :
                     : "S"(out)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
}

static uint32_t pd_strlen(const uint8_t *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void pd_strcpy(uint8_t *d, const uint8_t *s)
{
    while ((*d++ = *s++) != 0)
        ;
}

/* The original's MSVC callers never read their pushed arguments back, but
 * the original writes some of them: kept, through a volatile store. */
#define ARG_STORE(p, v) (*(volatile int32_t *)(p) = (int32_t)(v))

/* ===================================================================== */
/* Screen tints. */

/* ===== 0x42be20: per frame while the camera shake (word 0x6ccb60) runs:
 * the count 0x6ccb64 down (shake off at 0); in network play with phase
 * word 0x6ccb62 == 1, 0x485050 writes the car argument's slot; else the
 * tint from the car's distance to the shake origin 0x6ccb6c (positions
 * >> 19, squared, times 12 - kind 0x6ccb68): nothing beyond 0x60000, else
 * (count * 9 << 15) / max(d, 0x6000) - 0x6c, doubled and clamped to 0..0xff
 * for red and green, 3/8 of it for blue, added (0x42bfc0). ===== */
void GAMEAPI shake_flash_tint(uint32_t car)
{
    int32_t t, dx, dy, dz, d, c, v;

    if (!G16(0x6ccb60))
        return;
    t = GI32(0x6ccb64) - 1;
    if (t <= 0) {
        G16(0x6ccb60) = 0;
        return;
    }
    GI32(0x6ccb64) = t;
    if (U8(g_race, 0x553) && G16(0x6ccb62) == 1) {
        shake_tint_net(&car);
        return;
    }
    dx = (int32_t)(U32(car, 0x34) - G32(0x6ccb6c)) >> 19;
    dy = (int32_t)(U32(car, 0x38) - G32(0x6ccb70)) >> 19;
    dz = (int32_t)(U32(car, 0x3c) - G32(0x6ccb74)) >> 19;
    d = (int32_t)((uint32_t)dz * (uint32_t)dz + (uint32_t)dy * (uint32_t)dy + (uint32_t)dx * (uint32_t)dx);
    d = (int32_t)((uint32_t)d * (uint32_t)(12 - GI32(0x6ccb68)));
    if (d > 0x60000)
        return;
    if (d < 0x6000)
        d = 0x6000;
    c = (int32_t)(((uint32_t)t * 9) << 15) / d - 0x6c;
    if (c < 0)
        c = 0;
    v = c * 2;
    if (v > 0xff)
        v = 0xff;
    screen_tint_add(((((uint32_t)(v * 3 / 8)) << 8 | (uint32_t)v) << 8) | (uint32_t)v);
}

/* ===== 0x42bf20: the fade colour (bytes of 0x6ccd90, doubled, clamped to
 * 0xff) scaled by 0x1000 - n * 8 (n < 0x200), added to the tint. ===== */
void GAMEAPI fade_tint_add(int32_t n)
{
    uint32_t c, r, g, b;
    int32_t t;

    if (n >= 0x200)
        return;
    t = 0x1000 - (int32_t)((uint32_t)n << 12) / 512;
    c = G32(0x6ccd90);
    r = (c & 0xff) << 1;
    if (r > 0xff)
        r = 0xff;
    g = ((c >> 8) & 0xff) << 1;
    if (g > 0xff)
        g = 0xff;
    b = (uint32_t)G8(0x6ccd92) << 1;
    if (b > 0xff)
        b = 0xff;
    r = (uint32_t)((int32_t)(r * (uint32_t)t) >> 12);
    g = (uint32_t)((int32_t)(g * (uint32_t)t) >> 12);
    b = (uint32_t)((int32_t)(b * (uint32_t)t) >> 12);
    screen_tint_add((((b << 8) | g) << 8) | r);
}

/* ===== 0x42bfc0: adds a colour to the frame's tint 0x4be134 (each byte
 * saturating at 0xff; the top byte cleared), flag 0x4be138 = 1. Returns
 * the new tint. ===== */
uint32_t GAMEAPI screen_tint_add(uint32_t colour)
{
    uint32_t old = G32(0x4be134), r, g, b;

    G32(0x4be138) = 1;
    r = (old & 0xff) + (colour & 0xff);
    if (r >= 0xff)
        r = 0xff;
    g = ((old >> 8) & 0xff) + ((colour >> 8) & 0xff);
    if (g >= 0xff)
        g = 0xff;
    b = G8(0x4be136) + ((colour >> 16) & 0xff);
    if (b >= 0xff)
        b = 0xff;
    G32(0x4be134) = (b << 8 | g) << 8 | r;
    return G32(0x4be134);
}

/* ===== 0x42c040: the flash quad's texture word (0x4ac72c) and corner
 * colours (words 0x4be122 / 0x4be120 / 0x4be11e / 0x4be11c), unless a
 * higher-priority (lower word) one is set (0x4ac72e). ===== */
void GAMEAPI screen_flash_set(int32_t prio, int32_t c0, int32_t c1, int32_t c2, int32_t c3, int32_t c4)
{
    if ((int16_t)prio > GS16(0x4ac72e))
        return;
    G16(0x4ac72c) = (uint16_t)c0;
    G16(0x4be122) = (uint16_t)c1;
    G16(0x4be120) = (uint16_t)c2;
    G16(0x4be11e) = (uint16_t)c3;
    G16(0x4be11c) = (uint16_t)c4;
    G16(0x4ac72e) = (uint16_t)prio;
}

/* ===== 0x42c0a0: unless render flags 0x6ccd84 & 4, the full-screen tint
 * quad: one of four packets (0x479ae0's answer, always 0 in retail, and
 * the first view 0x6cd4a0 or not) gets the tint 0x4be134 as its colour,
 * the flash words and the clip rectangle 0x6ccd64..0x6ccd70 as corners and
 * is linked into the current view's table (+0x74); the tint and its flag
 * are cleared, the flash priority reset to 0x63. Without a tint only the
 * flash words are reset. ===== */
void GAMEAPI screen_tint_draw(uint32_t arg)
{
    uint32_t view, p, ot;

    if (G8(0x6ccd84) & 4)
        return;
    if (screen_idle(arg)) {
        view = G32(0x6ce60c);
        p = view == 0x6cd4a0 ? 0x6cca60 : 0x6ccad8;
    } else {
        view = G32(0x6ce60c);
        p = view == 0x6cd4a0 ? 0x6cc960 : 0x6cc9d8;
    }
    if (!G32(0x4be138) || !G32(0x4be134)) {
        G16(0x4ac72c) = 0xffff;
        G16(0x4ac72e) = 0x63;
        return;
    }
    {
        uint32_t c = (U32(p, 4) & 0xff000000u) | G32(0x4be134);
        G16(0x4ac72e) = 0x63;
        U16(p, 0x20) = G16(0x4be122);
        U16(p, 0x3c) = G16(0x4be120);
        U16(p, 0x58) = G16(0x4be11e);
        U16(p, 0x74) = G16(0x4be11c);
        U32(p, 4) = c;
        U16(p, 0x3e) = G8(0x4ac72c) & 0x3f;
    }
    U32(p, 0x48) = G32(0x6ccd64);
    U32(p, 0x10) = G32(0x6ccd64);
    G16(0x4ac72c) = 0xffff;
    U32(p, 0x64) = G32(0x6ccd68);
    U32(p, 0x2c) = G32(0x6ccd68);
    U32(p, 0x30) = G32(0x6ccd6c);
    U32(p, 0x14) = G32(0x6ccd6c);
    U32(p, 0x68) = G32(0x6ccd70);
    U32(p, 0x4c) = G32(0x6ccd70);
    ot = U32(view, 0x74) & 0x7fffffff;
    U32(p, 0) = G32(ot);
    G32(ot) = p;
    G32(0x4be134) = 0;
    G32(0x4be138) = 0;
}

/* ===================================================================== */
/* Blast flare and shock waves. */

/* The ordering-table entry for a depth (z-buffered or not: 0x6cce84). */
static uint32_t pd_ot_entry(int32_t depth)
{
    uint32_t e = G32(0x6cce84) ? G32(0x6cdd80 + 4 * (uint32_t)depth) : G32(0x6cd510 + 4 * (uint32_t)depth);
    return e & 0x7fffffff;
}

/* ===== 0x42c420: the blast flare (count 0x4be1a8, 1..): the blast point
 * (0x4be180 words) through the transform; at depth z / 32 - 2 (>= 1,
 * compressed above 0x100 and 0x180, at most 0x1ff) a ring of 32 quads
 * (packets *0x4be1a0, other views *0x4be1a4, 0x78 bytes) between the radii
 * size(n) / d and size(min(n + 10, 19)) / d (word table 0x4be158), sine
 * table 0x4b24f8, palette entries 0x47.. (+ i % 6) of the track; colour
 * 0x9a / alpha (20 - n) / 10 * 96 after the tenth frame, else 0x60; with
 * render flags 0x6ccd84 & 1 white and the alpha in byte +0x22. Without
 * that flag the original's byte +0x22 is an unwritten stack byte: 0
 * here. ===== */
void GAMEAPI blast_flare_draw(void)
{
    uint32_t v[6] = {0, 0, 0, 0, 0, 0};
    uint32_t ring[32][4];
    int32_t n = GI32(0x4be1a8), d, r1, r2, k, i;
    uint32_t pk, alpha, colour;
    uint8_t extra;

    if (!n)
        return;
    G32(0x6ccc64) = G32(0x4be180);
    G32(0x6ccc68) = G32(0x4be184);
    pd_transform_project(v);
    pk = G32(0x6ce60c) == 0x6cd4a0 ? G32(0x4be1a0) : G32(0x4be1a4);
    d = (pd_fist(v[1]) >> 5) - 2;
    if (d < 1)
        return;
    if (d > 0x100) {
        d = d / 2 + 0x80;
        if (d > 0x180) {
            d = d / 2 + 0xc0;
            if (d > 0x1ff)
                d = 0x1ff;
        }
    }
    r1 = (int32_t)G16(0x4be156 + 2 * (uint32_t)n) / d;
    k = n + 10;
    if (k >= 0x14)
        k = 0x13;
    r2 = (int32_t)G16(0x4be158 + 2 * (uint32_t)k) / d;
    for (i = 0; i < 32; i++) {
        int32_t s = GS16(0x4b24f8 + 2 * ((8 * i) & 0xff));
        int32_t c = GS16(0x4b24f8 + 2 * ((8 * i + 0x40) & 0xff));
        ring[i][0] = pd_add(pd_from_int((int32_t)((uint32_t)s * (uint32_t)r1) >> 14), v[2]);
        ring[i][1] = pd_add(pd_from_int((int32_t)((uint32_t)s * (uint32_t)r2) >> 14), v[2]);
        ring[i][2] = pd_add(pd_from_int((int32_t)((uint32_t)c * (uint32_t)r1) >> 14), v[3]);
        ring[i][3] = pd_add(pd_from_int((int32_t)((uint32_t)c * (uint32_t)r2) >> 14), v[3]);
    }
    alpha = 0x60;
    if (GI32(0x4be1a8) > 10)
        alpha = (uint32_t)pd_fist(pd_div_mul(pd_from_int(20 - GI32(0x4be1a8)), pd_from_int(10), G32(0x4a3388)));
    alpha &= 0xff;
    if (G8(0x6ccd84) & 1) {
        colour = 0x9affffff;
        extra = (uint8_t)alpha;
    } else {
        colour = 0x9a000000u | alpha << 16 | alpha << 8 | alpha;
        extra = 0; /* the original's unwritten stack byte */
    }
    for (i = 0; i < 32; i++, pk += 0x78) {
        int32_t j = (i + 1) & 0x1f;
        uint32_t pal = G32(0x6c1e14) + (((uint32_t)U16(G32(0x6c1eb0), 0x2e) + (uint32_t)(i % 6) + 0x47) << 4);
        uint32_t e;

        U32(pk, 0x10) = ring[j][1];
        U32(pk, 0x14) = ring[j][3];
        U32(pk, 0xc) = v[1];
        U32(pk, 0x8) = 0x40;
        U32(pk, 0x2c) = ring[j][0];
        U32(pk, 0x30) = ring[j][2];
        U32(pk, 0x28) = v[1];
        U32(pk, 0x48) = ring[i][1];
        U32(pk, 0x24) = 0x40;
        U32(pk, 0x4c) = ring[i][3];
        U32(pk, 0x40) = 0x40;
        U32(pk, 0x44) = v[1];
        U32(pk, 0x64) = ring[i][0];
        U32(pk, 0x68) = ring[i][2];
        U32(pk, 0x60) = v[1];
        U32(pk, 0x5c) = 0x40;
        U16(pk, 0x20) = U16(pal, 0);
        U16(pk, 0x3c) = U16(pal, 4);
        U16(pk, 0x58) = U16(pal, 8);
        U16(pk, 0x74) = U16(pal, 0xa);
        U32(pk, 4) = colour;
        U8(pk, 0x22) = extra;
        U16(pk, 0x3e) = U16(pal, 6);
        e = pd_ot_entry(d);
        U32(pk, 0) = G32(e);
        G32(e) = pk;
    }
}

/* ===== 0x42c950: the shock waves: 3 rows (0xf14 bytes at *0x6cc800,
 * count +0xc): each live one counts down and, while above 0, is drawn as a
 * ring of 16 wall quads: radius sine(min((24 - n) * 64 / 20, 0x40)) / 128
 * around (+0, +8), top at (+4 >> 16) - n * n / 4 - 0x20 and 0x14 below,
 * corners through the transform; colour 0x9a / alpha (n * 21 under 12,
 * else 0xff), palette 0x3a + the row's frame (+0x10, 0..4); packets +0x14
 * (first view) or +0x794, 0x78 each, at depth (sum of the 4 depths) / 128
 * - 1 (compressed like the flare's; skipped when not in front, too far or
 * all corners clipped on one side). The GTE z words' high halves (0x6ccc6a)
 * come from unwritten stack words in the original: 0 here; the transform
 * records start zeroed. ===== */
void GAMEAPI shock_waves_draw(void)
{
    uint32_t rec[16][12];
    uint32_t off, alpha = 0;
    int32_t k;

    for (k = 0; k < 16; k++) {
        int32_t m;
        for (m = 0; m < 12; m++)
            rec[k][m] = 0;
    }
    for (off = 0; off < 0x2d3c; off += 0xf14) {
        uint32_t row = G32(0x6cc800) + off, base, cnt, pal, c0, c1, c2, ang;
        int32_t s = I32(row, 0xc), t, idx, rad, x, z, i;
        uint16_t y, y2;

        if (!s)
            continue;
        s--;
        I32(row, 0xc) = s;
        if (s <= 0)
            continue;
        alpha = s < 12 ? (uint32_t)(s * 21) : 0xff;
        t = 24 - s;
        idx = (int32_t)((uint32_t)t << 6) / 20;
        if (t > 20)
            idx = 0x40;
        rad = GS16(0x4b24f8 + 2 * (uint32_t)idx) / 128;
        x = I32(row, 0);
        z = I32(row, 8);
        y = (uint16_t)((I32(row, 4) >> 16) - ((int32_t)((uint32_t)s * (uint32_t)s) / 4 + 0x20));
        y2 = (uint16_t)(y + 0x14);
        for (k = 0, ang = 0; k < 16; k++, ang += 0x6487) {
            uint16_t X = (uint16_t)((int32_t)((uint32_t)sin_table(ang) * (uint32_t)rad + (uint32_t)x) >> 16);
            uint16_t Z = (uint16_t)((int32_t)((uint32_t)cos_table(ang) * (uint32_t)rad + (uint32_t)z) >> 16);

            G32(0x6ccc64) = X | (uint32_t)y << 16;
            G32(0x6ccc68) = Z;
            pd_transform_project(rec[k]);
            G32(0x6ccc64) = X | (uint32_t)y2 << 16;
            G32(0x6ccc68) = Z;
            pd_transform_project(rec[k] + 6);
        }
        base = G32(0x6ce60c) == 0x6cd4a0 ? row + 0x14 : row + 0x794;
        cnt = U32(row, 0x10) + 1;
        if (cnt >= 5)
            cnt = 0;
        U32(row, 0x10) = cnt;
        pal = G32(0x6c1e14) + (((uint32_t)U16(G32(0x6c1eb0), 0x2e) + cnt + 0x3a) << 4);
        c0 = U32(pal, 0);
        c1 = U32(pal, 4);
        c2 = U32(pal, 8);
        for (i = 0; i < 16; i++) {
            uint32_t pk = base + 0x78 * (uint32_t)i, f, e;
            int32_t j = i + 1 < 16 ? i + 1 : 0, d, m;

            f = pd_add(rec[i][1], rec[i][7]);
            f = pd_add(f, rec[j][1]);
            f = pd_add(f, rec[j][7]);
            if (!ABOVE(pd_cmp(f, G32(0x4a3318))))
                continue;
            d = (pd_ftol(f) >> 7) - 1;
            if (d > 0x100) {
                d = d / 2 + 0x80;
                if (d > 0x180) {
                    d = d / 2 + 0xc0;
                    if (d > 0x1ff)
                        continue;
                }
            }
            if (d < 1)
                d = 1;
            else if (d >= 0x1fc)
                continue;
            if (rec[j][6] & rec[i][6] & rec[j][0] & rec[i][0])
                continue;
            for (m = 0; m < 6; m++)
                U32(pk, 8 + 4 * m) = rec[i][m];
            U32(pk, 0x3c) = c1;
            for (m = 0; m < 6; m++)
                U32(pk, 0x24 + 4 * m) = rec[j][m];
            for (m = 0; m < 6; m++)
                U32(pk, 0x40 + 4 * m) = rec[i][6 + m];
            for (m = 0; m < 6; m++)
                U32(pk, 0x5c + 4 * m) = rec[j][6 + m];
            U32(pk, 0x20) = c0;
            U16(pk, 0x58) = (uint16_t)c2;
            U16(pk, 0x74) = (uint16_t)(c2 >> 16);
            U32(pk, 4) = ((((alpha | 0xffff9a00u) << 8) | alpha) << 8) | alpha;
            e = pd_ot_entry(d);
            U32(pk, 0) = G32(e);
            G32(e) = pk;
        }
    }
}

/* ===================================================================== */
/* The text file. */

/* ===== 0x42e050: the font with this id (first dword) among the *0x4be314
 * loaded ones (pointers at 0x6cb4e0), else 0. ===== */
uint32_t GAMEAPI font_by_id(uint32_t id)
{
    int32_t i, n = GI32(0x4be314);

    for (i = 0; i < n; i++)
        if (G32(G32(0x6cb4e0 + 4 * (uint32_t)i)) == id)
            return G32(0x6cb4e0 + 4 * (uint32_t)i);
    return 0;
}

/* ===== 0x42e2a0: the string buffers, allocated once (0x4be300): packets
 * 0x4be308 / 0x4be30c (0x42000 each), glyph corners 0x4be304 (0x10000),
 * records 0x4be310 (0xa600); -1 if one fails. Then the glyph count
 * 0x4be2fc and every record's flags word are cleared; 0. ===== */
int32_t GAMEAPI text_buffers_alloc(void)
{
    uint32_t off;

    if (!G32(0x4be300)) {
        uint32_t t;
        G32(0x4be300) = 1;
        G32(0x4be308) = crt_malloc(0x42000);
        G32(0x4be30c) = crt_malloc(0x42000);
        G32(0x4be304) = crt_malloc(0x10000);
        t = crt_malloc(0xa600);
        G32(0x4be310) = t;
        if (!G32(0x4be308) || !G32(0x4be30c) || !G32(0x4be304) || !t)
            return -1;
    }
    g_glyph_count = 0;
    for (off = 0; off < 0xa600; off += 0x14c)
        U16(g_strings, off) = 0;
    return 0;
}

/* ===== 0x42e3d0: a text file entry copied out (section `name`, this
 * language, 0x42e4b0): up to ']' with byte 1 dropped, a NUL (and a '~'
 * after it) as a line break 0xd unless only blanks and ']' follow; a tab
 * is logged. At most max - 1 characters (max -1: no limit; <= 0 or no
 * buffer: counted only), NUL-terminated. Returns the length, -1 if the
 * entry is missing. ===== */
int32_t GAMEAPI text_entry_copy(uint8_t *dst, int32_t max, const char *name)
{
    const uint8_t *s = 0;
    uint8_t *d;
    int32_t lim = max, n;

    if (name)
        s = (const uint8_t *)(uintptr_t)text_section_find(name);
    if (max != -1 && max <= 0) {
        d = 0;
        lim = 0;
    } else {
        d = dst;
        if (!d)
            lim = 0;
        else
            *d = 0;
    }
    if (!s) {
        n = -1;
        goto end;
    }
    n = 0;
    if (*s == ']')
        goto end;
    do {
        uint8_t c = *s;
        if (c == 1) {
            s++;
            continue;
        }
        if (c == 9)
            debug_log(0x4ac8a0, name); /* "tab used in string" */
        c = *s;
        if (c) {
            if (d && (lim == -1 || n < lim - 1))
                *d++ = c;
            s++;
        } else {
            const uint8_t *p;
            c = s[1];
            s++;
            if (c == '~')
                s++;
            p = s;
            while (*p <= 0x20)
                p++;
            if (*p == ']') {
                s = p;
                continue;
            }
            if (d && (lim == -1 || n < lim - 1))
                *d++ = 0xd;
        }
        n++;
    } while (*s != ']');
end:
    if (d && (lim == -1 || n > 0))
        *d = 0;
    return n;
}

/* ===== 0x42e4b0: where a section's text starts in the text file
 * (*0x4be2ec, size *0x4be2f0): "name" + ")" found (0x42e620), then the
 * language marker (word 0x4be2f4: 1 FRENCH, 2 GERMAN, 3 ITALIAN, 4
 * SPANISH, 5 JAPANESE, else ENGLISH) after it, then the next '[' and the
 * first byte above 0x1f after it. 0 if not found. A name over 0x7e
 * characters overruns the original's 0x80-byte buffer. ===== */
uint32_t GAMEAPI text_section_find(const char *name)
{
    uint8_t buf[0x100];
    uint32_t text = G32(0x4be2ec), size, pos, lang, n;

    if (!text)
        return 0;
    pd_strcpy(buf, (const uint8_t *)name);
    pd_strcpy(buf + pd_strlen(buf), (const uint8_t *)0x4ac8ec);
    switch ((G32(0x4be2f4) & 0xffff) - 1) {
    case 0:
        lang = 0x4ac8e4; /* FRENCH */
        break;
    case 1:
        lang = 0x4ac8dc; /* GERMAN */
        break;
    case 2:
        lang = 0x4ac8cc; /* ITALIAN */
        break;
    case 3:
        lang = 0x4ac8d4; /* SPANISH */
        break;
    case 4:
        lang = 0x4ac8c0; /* JAPANESE */
        break;
    default:
        lang = 0x4ac8b8; /* ENGLISH */
        break;
    }
    pos = text_bytes_find(buf, (const uint8_t *)(uintptr_t)text, pd_strlen(buf), G32(0x4be2f0));
    if (!pos || pos >= G32(0x4be2f0))
        return 0;
    do {
        n = pd_strlen((const uint8_t *)(uintptr_t)lang);
        if (!text_bytes_differ((const uint8_t *)(uintptr_t)lang, (const uint8_t *)(uintptr_t)(G32(0x4be2ec) + pos), n)) {
            text = G32(0x4be2ec);
            size = G32(0x4be2f0);
            if (pos < size) {
                do {
                    if (G8(text + pos) == '[') {
                        pos++;
                        if (pos < size) {
                            do {
                                if (G8(text + pos) > 0x1f)
                                    return text + pos;
                                pos++;
                            } while (pos < size);
                        }
                    }
                    pos++;
                } while (pos < size);
            }
        }
        pos++;
    } while (pos < G32(0x4be2f0));
    return 0;
}

/* ===== 0x42e620: the first offset i < n where the len bytes of pat occur
 * in text (0 also when not found). The original copies pat into a 64-byte
 * stack buffer first (overrun above 64 bytes); compared in place here. ===== */
uint32_t GAMEAPI text_bytes_find(const uint8_t *pat, const uint8_t *text, uint32_t len, uint32_t n)
{
    uint32_t i, k;

    for (i = 0; i < n; i++) {
        for (k = 0; k < len && pat[k] == text[i + k]; k++)
            ;
        if (k == len)
            return i;
    }
    return 0;
}

/* ===== 0x42e690: 1 if the first n bytes differ, else 0. ===== */
uint32_t GAMEAPI text_bytes_differ(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 1;
    return 0;
}

/* ===== 0x42e6d0: line `line` (0xd separated) of a text entry (0x42e3d0,
 * 0x800 bytes) copied out like 0x42e3d0 (max -1 no limit; shorter: strncpy
 * max - 1 and a NUL). Returns the line's length, -1 if the entry or line
 * is missing. ===== */
int32_t GAMEAPI text_line_get(uint8_t *dst, int32_t max, const char *name, int32_t line)
{
    uint8_t one[0x800], text[0x800];
    const uint8_t *p;
    uint8_t *o;
    int32_t lim = max, n;

    if (max != -1 && max <= 0) {
        dst = 0;
        lim = 0;
    } else if (!dst) {
        lim = 0;
    } else {
        *dst = 0;
    }
    if (text_entry_copy(text, 0x800, name) == -1)
        return -1;
    p = text;
    for (;;) {
        uint8_t c = *p;
        o = one;
        while (c && c != 0xd) {
            p++;
            *o++ = c;
            c = *p;
        }
        *o = 0;
        if (!line)
            break;
        c = *p;
        line--;
        if (!c)
            return -1;
        p++;
    }
    n = (int32_t)pd_strlen(one);
    if (!dst || !n)
        return n;
    if (lim == -1 || lim > n) {
        pd_strcpy(dst, one);
        return n;
    }
    if (lim <= 0)
        return n;
    crt_strncpy(dst, one, lim - 1);
    dst[lim - 1] = 0;
    return n;
}

/* The format with "]" appended (strncpy of 0xfe characters into a zeroed
 * buffer when it is 0xff or longer). */
static void pd_format_copy(uint8_t *buf, const char *fmt)
{
    if (pd_strlen((const uint8_t *)fmt) >= 0xff) {
        uint32_t i;
        for (i = 0; i < 0x100; i++)
            buf[i] = 0;
        crt_strncpy(buf, fmt, 0xfe);
    } else {
        pd_strcpy(buf, (const uint8_t *)fmt);
    }
    pd_strcpy(buf + pd_strlen(buf), (const uint8_t *)0x4ac8f0);
}

/* ===== 0x42e810: a string object from a format and its arguments
 * (0x42f6a0); returns its index or -1. ===== */
int32_t GAMEAPI string_printf(uint32_t font, uint32_t colours, uint32_t flags, uint32_t spacing, const char *fmt, ...)
{
    uint8_t buf[0x100];
    __builtin_va_list ap;
    int32_t r;

    pd_format_copy(buf, fmt);
    __builtin_va_start(ap, fmt);
    r = string_build(font, buf, colours, flags, spacing, (uint32_t)(uintptr_t)ap);
    __builtin_va_end(ap);
    return r;
}

/* ===== 0x42e8f0: the width of a formatted string: built (0x42f6a0),
 * measured (0x42f030), switched off (flags &= ~0x4000) and its glyphs given
 * back (word 0x4be2fc restored). ===== */
int32_t GAMEAPI string_width_printf(uint32_t font, uint32_t colours, uint32_t flags, uint32_t spacing,
                                    const char *fmt, ...)
{
    uint8_t buf[0x100];
    __builtin_va_list ap;
    uint16_t saved = g_glyph_count;
    int32_t idx, w;

    pd_format_copy(buf, fmt);
    __builtin_va_start(ap, fmt);
    idx = string_build(font, buf, colours, flags, spacing, (uint32_t)(uintptr_t)ap);
    __builtin_va_end(ap);
    w = (int32_t)string_get_width(idx);
    if (idx != -1)
        U16(REC(idx), 0) &= 0xbfff;
    g_glyph_count = saved;
    return w;
}

/* ===================================================================== */
/* String objects. Record (0x14c bytes): +0 flags (0x8000 shown, 0x4000 in
 * use, 0x2000 has '~>' marks), +2 first glyph, +4/+8/+0xc position, +0x14
 * matrix, +0x74 width, +0x76 height, +0x78 lines, +0x7a glyphs, +0x7c line
 * widths, +0xbc line starts (33 words), +0xfe mark glyph per line (-1
 * none), +0x140 glyph corners, +0x144 / +0x148 packets. */

/* ===== 0x42ea00: whether string i (0..0x7f) is in use (flag 0x4000). ===== */
uint32_t GAMEAPI string_active(int32_t i)
{
    if (i == -1 || i < 0 || i >= 0x80)
        return 0;
    return (U16(REC(i), 0) >> 14) & 1;
}

/* ===== 0x42ea40: a string shown at (x, y, z) with extra flags. ===== */
void GAMEAPI string_on(int32_t i, int32_t x, int32_t y, int32_t z, uint32_t flags)
{
    if (!string_active(i))
        return;
    I32(REC(i), 4) = (int16_t)x;
    I32(REC(i), 8) = (int16_t)y;
    I32(REC(i), 0xc) = (int16_t)z;
    U16(REC(i), 0) |= (uint16_t)(flags | 0x8000);
}

/* ===== 0x42eab0: a string hidden. ===== */
void GAMEAPI string_off(int32_t i)
{
    if (string_active(i))
        U16(REC(i), 0) &= 0x7fff;
}

/* ===== 0x42eae0: a line's glyph range [start, end) and mark glyph; line
 * -1: all glyphs, mark -1. -1 (start = end = -1) for no such line. ===== */
int32_t GAMEAPI string_line_range(int32_t i, int32_t line, uint32_t *start, uint32_t *end, int32_t *mark)
{
    uint32_t rec = REC(i), s;

    if (line == -1) {
        s = U16(rec, 2);
        *start = s;
        *end = U16(REC(i), 0x7a) + s;
        *mark = -1;
        return 0;
    }
    if (line < 0 || line >= (int32_t)U16(rec, 0x78)) {
        *start = 0xffffffff;
        *end = 0xffffffff;
        return -1;
    }
    *start = U16(rec, 0xbc + 2 * (uint32_t)line);
    *end = U16(REC(i), 0xbe + 2 * (uint32_t)line);
    *mark = S16(REC(i), 0xfe + 2 * (uint32_t)line);
    return 0;
}

/* ===== 0x42ebc0: the bounding box of a line's glyph corners (line -1:
 * all), offset by the string's position; any output may be 0. 0, or -1
 * (box 0..0) when the string or line is missing. The original stores the
 * start (then times 4) in the i argument's slot. ===== */
int32_t GAMEAPI string_limits(int32_t i, int32_t line, int32_t *minx, int32_t *miny, int32_t *maxx, int32_t *maxy)
{
    int32_t idx = i, x0 = 0, y0 = 0, x1 = 0, y1 = 0, r = -1;
    uint32_t end;
    int32_t mark;

    if (string_active(idx) && string_line_range(idx, line, (uint32_t *)&i, &end, &mark) != -1) {
        uint32_t g = g_glyphs;
        int32_t k = (int32_t)((uint32_t)i << 2), e = (int32_t)(end << 2);

        ARG_STORE(&i, k);
        x0 = x1 = S16(g, 8 * (uint32_t)k);
        y0 = y1 = S16(g, 8 * (uint32_t)k + 2);
        for (k++; k < e; k++) {
            int32_t x = S16(g, 8 * (uint32_t)k), y = S16(g, 8 * (uint32_t)k + 2);
            if (x < x0)
                x0 = x;
            if (x > x1)
                x1 = x;
            if (y < y0)
                y0 = y;
            if (y > y1)
                y1 = y;
        }
        r = 0;
    }
    if (minx)
        *minx = I32(REC(idx), 4) + x0;
    if (miny)
        *miny = I32(REC(idx), 8) + y0;
    if (maxx)
        *maxx = I32(REC(idx), 4) + x1;
    if (maxy)
        *maxy = I32(REC(idx), 8) + y1;
    return r;
}

/* ===== 0x42ed10: 0x42ebc0 for the whole string. ===== */
int32_t GAMEAPI string_limits_all(int32_t i, int32_t *minx, int32_t *miny, int32_t *maxx, int32_t *maxy)
{
    return string_limits(i, -1, minx, miny, maxx, maxy);
}

/* ===== 0x42ed40: places a string: the whole box vertically (ymode 0 top,
 * 1 centre, 2 bottom at y), each line horizontally (xmode 0 left, 1 centre,
 * 2 right at x), by moving its glyph corners. 0, -1 when missing. The
 * original keeps the box's bottom in the i slot, the y offset in the y
 * slot and the record offset in the ymode slot. ===== */
int32_t GAMEAPI string_set_position(int32_t i, int32_t x, int32_t y, int32_t xmode, int32_t ymode)
{
    int32_t idx = i, minx, miny, maxx, dy, line;
    uint32_t off;

    if (string_limits_all(idx, &minx, &miny, &maxx, &i) == -1)
        return -1;
    switch (ymode) {
    case 0:
        dy = y - miny;
        break;
    case 1:
        dy = y - (miny + i) / 2;
        break;
    case 2:
        dy = y - i;
        break;
    default:
        dy = 0;
        break;
    }
    ARG_STORE(&y, dy);
    off = (uint32_t)idx * 0x14c;
    ARG_STORE(&ymode, off);
    for (line = 0; line < (int32_t)U16(g_strings + off, 0x78); line++) {
        int32_t dx;
        uint32_t s, e;

        if (string_limits(idx, line, &minx, &miny, &maxx, &i) == -1)
            continue;
        switch (xmode) {
        case 0:
            dx = x - minx;
            break;
        case 1:
            dx = x - (minx + maxx) / 2;
            break;
        case 2:
            dx = x - maxx;
            break;
        default:
            dx = 0;
            break;
        }
        if (!dx && !dy)
            continue;
        e = (uint32_t)U16(g_strings + off, 0xbe + 2 * (uint32_t)line) << 2;
        s = (uint32_t)U16(g_strings + off, 0xbc + 2 * (uint32_t)line) << 2;
        for (; (int32_t)s < (int32_t)e; s++) {
            U16(g_glyphs, 8 * s) += (uint16_t)dx;
            U16(g_glyphs, 8 * s + 2) += (uint16_t)dy;
        }
    }
    return 0;
}

/* ===== 0x42eed0: moves each line's glyphs from its '~>' mark on so that
 * mode 0: the mark glyph's x (the entry at the mark index, sic), 1: the
 * middle of that and the line's right edge, 2: the right edge lands on x.
 * 0, -1 when the string is missing. The original keeps the line's end
 * (then times 4) in the i slot. ===== */
int32_t GAMEAPI string_set_position_j(int32_t i, int32_t x, int32_t mode)
{
    int32_t idx = i, line;
    uint32_t off, aoff;

    if (!string_active(idx))
        return -1;
    off = (uint32_t)idx * 0x14c;
    if (!U16(g_strings + off, 0x78))
        return 0;
    for (line = 0, aoff = off + 0xfe; line < (int32_t)U16(g_strings + off, 0x78); line++, aoff += 2) {
        uint32_t start, g;
        int32_t mark, minx, maxx, dx, k, e;

        if (U16(g_strings + aoff, 0) == 0xffff)
            continue;
        if (string_line_range(idx, line, &start, (uint32_t *)&i, &mark) == -1)
            continue;
        if (mark == -1 || mark >= i)
            continue;
        if (string_limits(idx, line, &minx, 0, &maxx, 0) == -1)
            continue;
        g = g_glyphs;
        switch (mode) {
        case 0:
            dx = x - S16(g, 8 * (uint32_t)mark);
            break;
        case 1:
            dx = x - (S16(g, 8 * (uint32_t)mark) + maxx) / 2;
            break;
        case 2:
            dx = x - maxx;
            break;
        default:
            continue;
        }
        if (!dx)
            continue;
        e = (int32_t)((uint32_t)i << 2);
        ARG_STORE(&i, e);
        for (k = (int32_t)((uint32_t)mark << 2); k < e; k++)
            U16(g_glyphs, 8 * (uint32_t)k) += (uint16_t)dx;
    }
    return 0;
}

/* ===== 0x42f030 / 0x42f070: a string's width / height (0 if not in
 * use). 0x42f0b0: whether it has '~>' marks (flag 0x2000). ===== */
uint32_t GAMEAPI string_get_width(int32_t i)
{
    if (!string_active(i))
        return 0;
    return U16(REC(i), 0x74);
}

uint32_t GAMEAPI string_get_height(int32_t i)
{
    if (!string_active(i))
        return 0;
    return U16(REC(i), 0x76);
}

uint32_t GAMEAPI string_has_marks(int32_t i)
{
    if (!string_active(i))
        return 0;
    return (U16(REC(i), 0) >> 13) & 1;
}

/* ===== 0x42f0f0: colours (bytes 0..2, 4..6, 8..10, 12..14: the four
 * corners) for glyphs of a line (-1: all): which -1 all of them, n >= 0
 * the n-th, n < 0 the (-n - 1)-th from the end; in packet set sel (0
 * *0x4be308, 1 *0x4be30c, -1 both). 0, -1 when missing. The original keeps
 * the start / end in the sel / i slots. ===== */
int32_t GAMEAPI string_colour_range(int32_t i, int32_t line, int32_t which, int32_t sel, const uint8_t *colour)
{
    int32_t idx = i, set, from, mark;

    if (sel == -1) {
        if (string_colour_range(i, line, which, 0, colour) == -1)
            return -1;
        return string_colour_range(i, line, which, 1, colour);
    }
    set = sel ? 1 : 0;
    if (!string_active(idx))
        return -1;
    if (string_line_range(idx, line, (uint32_t *)&sel, (uint32_t *)&i, &mark) == -1)
        return -1;
    if (which == -1) {
        from = sel;
    } else if (which >= 0) {
        from = sel + which;
        if (from < i)
            ARG_STORE(&i, from + 1);
    } else {
        int32_t to = i + which + 2;
        ARG_STORE(&i, to);
        from = sel;
        if (to > from)
            from = to - 1;
    }
    for (; from < i; from++) {
        uint32_t o = (uint32_t)from * 0x84;
        U8(G32(0x4be308 + 4 * set) + o, 4) = colour[0];
        U8(G32(0x4be308 + 4 * set) + o, 5) = colour[1];
        U8(G32(0x4be308 + 4 * set) + o, 6) = colour[2];
        U8(G32(0x4be308 + 4 * set) + o, 0x24) = colour[4];
        U8(G32(0x4be308 + 4 * set) + o, 0x25) = colour[5];
        U8(G32(0x4be308 + 4 * set) + o, 0x26) = colour[6];
        U8(G32(0x4be308 + 4 * set) + o, 0x44) = colour[8];
        U8(G32(0x4be308 + 4 * set) + o, 0x45) = colour[9];
        U8(G32(0x4be308 + 4 * set) + o, 0x46) = colour[10];
        U8(G32(0x4be308 + 4 * set) + o, 0x64) = colour[12];
        U8(G32(0x4be308 + 4 * set) + o, 0x65) = colour[13];
        U8(G32(0x4be308 + 4 * set) + o, 0x66) = colour[14];
    }
    return 0;
}

/* ===== 0x42f2b0: colours for a whole string. ===== */
int32_t GAMEAPI string_colour_set(int32_t i, int32_t sel, const uint8_t *colour)
{
    return string_colour_range(i, -1, -1, sel, colour);
}

/* ===== 0x42f2d0: hides the glyph corners (+4 = -1, else 0) outside an
 * n-wide window of the string's box: mode 1 from the left, 2 centred, 3
 * from the right. The original keeps the box's right edge in the i slot
 * and the glyph end in the n slot. ===== */
void GAMEAPI string_clip_x(int32_t i, int32_t n, int32_t mode)
{
    int32_t idx = i, minx, miny, maxy, lo, hi, mark, k, e;
    uint32_t start;

    if (n <= 0)
        return;
    if (string_limits(idx, -1, &minx, &miny, &i, &maxy) == -1)
        return;
    switch (mode) {
    case 1:
        lo = minx;
        hi = lo + n - 1;
        break;
    case 2:
        lo = (minx - n + i) / 2;
        hi = lo + n - 1;
        break;
    case 3:
        lo = i - n + 1;
        hi = i;
        break;
    default:
        return;
    }
    if (string_line_range(idx, -1, &start, (uint32_t *)&n, &mark) == -1)
        return;
    k = (int32_t)(start << 2);
    e = (int32_t)((uint32_t)n << 2);
    ARG_STORE(&n, e);
    for (; k < e; k++) {
        int32_t x = S16(g_glyphs, 8 * (uint32_t)k);
        U16(g_glyphs, 8 * (uint32_t)k + 4) = (x < lo || x > hi) ? 0xffff : 0;
    }
}

/* ===== 0x42f3c0: the shown strings' glyph packets (set `which`) placed
 * on screen (position + corner, scaled from 640 x 480 to the screen
 * 0x4acc38 / 0x4acc3c, depth 1000.0) and chained (0x4290a0); the chain is
 * linked into ordering table `ot` (0x429080), whose result is returned (0
 * without strings). Hidden glyphs (corner +4 = -1) are skipped. The
 * original keeps the packet field's offset in the `which` slot. ===== */
int32_t GAMEAPI strings_draw(uint32_t ot, uint32_t unused, uint32_t which)
{
    uint32_t first = 0, last = 0, r, field;

    (void)unused;
    field = (which ? 1u : 0u) * 4 + 0x144;
    ARG_STORE(&which, field);
    for (r = 0; r < 0x80; r++, field += 0x14c, ARG_STORE(&which, field)) {
        uint32_t rec = g_strings + r * 0x14c, g, pk, k;

        if ((U16(rec, 0) & 0xc000) != 0xc000)
            continue;
        if (!U16(rec, 0x7a))
            continue;
        g = U32(rec, 0x140);
        pk = U32(g_strings + field, 0);
        for (k = 0; k < U16(g_strings + r * 0x14c, 0x7a); k++, g += 0x20, pk += 0x84) {
            uint32_t j;

            if (U16(g, 4) == 0xffff)
                continue;
            for (j = 0; j < 4; j++) {
                int32_t sx = (int32_t)((uint32_t)(I32(g_strings + r * 0x14c, 4) + S16(g, 8 * j)) * G32(0x4acc38)) / 640;
                U32(pk, 0x10 + 0x20 * j) = pd_from_int(sx);
                if (j == 3) {
                    U32(pk, 0x6c) = 0x447a0000; /* 1000.0 */
                    U32(pk, 0x4c) = 0x447a0000;
                    U32(pk, 0x2c) = 0x447a0000;
                    U32(pk, 0xc) = 0x447a0000;
                }
                {
                    int32_t sy =
                        (int32_t)((uint32_t)(I32(g_strings + r * 0x14c, 8) + S16(g, 8 * j + 2)) * G32(0x4acc3c)) / 480;
                    U32(pk, 0x14 + 0x20 * j) = pd_from_int(sy);
                }
            }
            U32(pk, 0x68) = 0x40;
            U32(pk, 0x48) = 0x40;
            U32(pk, 0x28) = 0x40;
            U32(pk, 0x8) = 0x40;
            if (!first) {
                first = pk;
                last = pk;
            } else {
                dword_store(last, pk);
                last = pk;
            }
        }
    }
    if (first)
        return ot_link_chain(ot, first, last);
    return 0;
}

/* The original's locals of 0x42f6a0, at the same offsets (from its entry
 * stack pointer - 0x54), so that a number field narrower than its width
 * (a negative skip) reads the same neighbours. */
struct pd_build_frame {
    uint32_t x;      /* -0x54: pen x (0x42fd90 writes its low word) */
    int32_t y;       /* -0x50 */
    uint32_t line;   /* -0x4c */
    int32_t skip;    /* -0x48: number width, then leading digits skipped */
    int32_t height;  /* -0x44: line height (0x42fd90 writes its low word) */
    uint32_t arg;    /* -0x40: argument index */
    uint32_t maxw;   /* -0x3c */
    union {
        uint16_t angles[4];
        uint32_t sarg; /* -0x38: string argument */
    } u;
    uint32_t font;   /* -0x30 */
    uint32_t start;  /* -0x2c: glyph count at the line's start */
    uint32_t count0; /* -0x28 */
    int32_t idx;     /* -0x24 */
    uint8_t num[0x20]; /* -0x20 */
};

/* A '~>' mark: the line's mark glyph (first one only), flag 0x2000. */
static void pd_build_mark(uint32_t rec, const struct pd_build_frame *f, uint32_t *fl)
{
    uint32_t ln = f->line & 0xffff;
    if (U16(rec, 0xfe + 2 * ln) == 0xffff) {
        U16(rec, 0xfe + 2 * ln) = g_glyph_count;
        *fl |= 0x2000;
    }
}

/* ===== 0x42f6a0: builds string object `font_id` text (up to ']'): a free
 * record (0x42fd10, 0x4be2f8 the next search start), matrix from zero
 * angles, identities, marks -1; glyphs (0x42fd90) with colours `colours`;
 * ' ' moves by 8 (flags & 8: the font's +0x10), tab to the next stop
 * (0x42fd60), NUL or ']' after text ends a line (spacing, else the font
 * height); escapes ~> mark, ~Pn argument index, ~S string argument, ~D
 * number argument, ~0w number in w digits (zero-padded, 9 digits;
 * without w leading zeros dropped). At most 32 lines. Returns the index,
 * -1 without font or free record. A NUL that ends a line does not end the
 * text (sic). ===== */
int32_t GAMEAPI string_build(uint32_t font_id, const uint8_t *s, uint32_t colours, uint32_t flags, uint32_t spacing,
                             uint32_t va)
{
    struct pd_build_frame f;
    uint32_t rec, fl, i = 0, slot, k;
    int32_t n;

    {
        uint8_t *b = (uint8_t *)&f;
        for (k = 0; k < sizeof f; k++)
            b[k] = 0;
    }
    f.maxw = 0;
    f.count0 = G32(0x4be2fc);
    f.start = f.count0;
    f.font = font_by_id(font_id);
    if (!f.font)
        return -1;
    f.idx = call_text_slot_free();
    if (f.idx == -1)
        return -1;
    n = GI32(0x4be2f8) + 1;
    GI32(0x4be2f8) = n;
    if (n >= 0x80)
        GI32(0x4be2f8) = 0;
    rec = REC(f.idx);
    f.u.angles[0] = f.u.angles[1] = f.u.angles[2] = f.u.angles[3] = 0;
    matrix_from_angles(f.u.angles, rec + 0x14);
    matrix_identity(rec + 0x54);
    matrix_identity(rec + 0x34);
    for (k = 0; k < 16; k++)
        U32(rec, 0xfe + 4 * k) = 0xffffffff;
    fl = flags & 0xdfff;
    U16(rec, 2) = g_glyph_count;
    U32(rec, 0x140) = g_glyphs + ((uint32_t)g_glyph_count << 5);
    U32(rec, 0x144) = G32(0x4be308) + (uint32_t)g_glyph_count * 0x84;
    U32(rec, 0x148) = G32(0x4be30c) + (uint32_t)g_glyph_count * 0x84;
    for (;;) {
        uint8_t c;

        if ((f.line & 0xffff) >= 0x20)
            break;
        c = s[i];
        if (c <= 0x20) {
            /* 0x42fab1: two-byte characters above 0x7f, never reached */
        } else if (c == '~') {
            uint32_t e;
            i++;
            f.skip = 0;
            slot = i;
            e = (uint32_t)s[i] - 0x30;
            if (e <= 0x23) {
                switch (s[i]) {
                case '>':
                    pd_build_mark(rec, &f, &fl);
                    break;
                case 'P':
                    slot += (uint32_t)call_number_parse(s + i + 1, &f.arg);
                    break;
                case 'S': {
                    uint32_t m = 0;
                    const uint8_t *p;
                    f.u.sarg = U32(va + 4 * f.arg, 0);
                    p = (const uint8_t *)(uintptr_t)f.u.sarg;
                    if ((int32_t)pd_strlen(p) <= 0)
                        break;
                    do {
                        uint8_t ch = p[m];
                        if (ch == '~' && p[m + 1] == '>') {
                            pd_build_mark(rec, &f, &fl);
                            m++;
                        } else if (ch == ' ') {
                            f.x += (fl & 8) ? U32(f.font, 0x10) : 8;
                        } else if (ch == 9) {
                            f.x = call_text_tab_next(f.x);
                            p = (const uint8_t *)(uintptr_t)f.u.sarg;
                        } else {
                            call_glyph_place(font_id, ch, colours, f.y, &f.x, &f.height, fl);
                            p = (const uint8_t *)(uintptr_t)f.u.sarg;
                        }
                        m++;
                    } while ((int32_t)m < (int32_t)pd_strlen(p));
                    break;
                }
                case '0':
                    slot += (uint32_t)call_number_parse(s + i, &f.skip);
                    /* fall through */
                case 'D': {
                    int32_t sk, m;
                    call_number_digits(f.num, U32(va + 4 * f.arg, 0));
                    if (f.skip) {
                        f.skip = (int32_t)pd_strlen(f.num) - f.skip;
                    } else if ((int32_t)pd_strlen(f.num) > 0) {
                        int32_t z = 0;
                        sk = 0;
                        while (f.num[z] == '0') {
                            sk++;
                            z++;
                            if (z >= (int32_t)pd_strlen(f.num))
                                break;
                        }
                        f.skip = sk;
                    }
                    sk = f.skip;
                    for (m = 0; m < (int32_t)pd_strlen(f.num + sk); m++) {
                        call_glyph_place(font_id, f.num[sk + m], colours, f.y, &f.x, &f.height, fl);
                        sk = f.skip;
                    }
                    break;
                }
                default:
                    break;
                }
                i = slot;
            }
        } else if (c != ']') {
            call_glyph_place(font_id, c, colours, f.y, &f.x, &f.height, fl);
        }
        /* 0x42f877 */
        if (s[i] == ' ')
            f.x += (fl & 8) ? U32(f.font, 0x10) : 8;
        if (s[i] == 9)
            f.x = call_text_tab_next(f.x);
        c = s[i];
        if (!c || (c == ']' && (uint16_t)f.x)) {
            uint32_t ln = f.line & 0xffff;
            U16(rec, 0x7c + 2 * ln) = (uint16_t)f.x;
            U16(rec, 0xbc + 2 * ln) = (uint16_t)f.start;
            f.start = G32(0x4be2fc);
            if ((uint16_t)f.x > (uint16_t)f.maxw)
                f.maxw = f.x;
            if ((uint16_t)spacing)
                f.y += (int32_t)spacing;
            else
                f.y += f.height;
            f.x = 0;
            f.line++;
        }
        if (s[i] == ']')
            break;
        i++;
    }
    fl |= 0x4000;
    U16(rec, 0xbc + 2 * (f.line & 0xffff)) = g_glyph_count;
    U16(rec, 0x74) = (uint16_t)f.maxw;
    U16(rec, 0x76) = (uint16_t)f.y;
    U16(rec, 0x78) = (uint16_t)f.line;
    U16(rec, 0x7a) = (uint16_t)(G32(0x4be2fc) - f.count0);
    U16(rec, 0) = (uint16_t)fl;
    U32(rec, 4) = 0;
    U32(rec, 8) = 0;
    U32(rec, 0xc) = 0;
    return f.idx;
}
