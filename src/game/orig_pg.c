/* Group PG: the original's still-reachable functions in 0x438df0-0x442060
 * (the rest of the HUD source file, the archive / CPU helpers next to it,
 * the leader's hit callback and the light / lens flare source file).
 *
 *  - the HUD: speedo digits (0x438df0), lap and place (0x438e60), the four
 *    time readouts (0x438fb0), the warning sprite (0x439050), the two weapon
 *    icons (0x4390c0), the last-lap banner (0x439270 / 0x4392e0), the
 *    markers over the other cars (0x439400), the fade state and brightness
 *    (0x439d50 / 0x439d70), the split-screen layout (0x439f20), the name
 *    texts (0x43a000), a car's screen point (0x43a3e0) and the gap bars
 *    (0x43a4a0);
 *  - archive_load_by_name (0x43b660), the GenuineIntel check (0x43c370) and
 *    the "no" feature check (0x43c3e0);
 *  - the leader missile's hit callback (0x43f040);
 *  - the light slots: remove a light's cells (0x4410f0), new slot (0x441220),
 *    free (0x441370), move (0x4413a0);
 *  - the lens flares: queue (0x441d80), the quad (0x441de0), its texture and
 *    colour (0x441fa0) and the per-frame draw (0x442060).
 *
 * Calls outside the group go by address. Float steps are the original's x87
 * instructions in inline asm (memory operands, the same intermediates); the
 * GTE transform / projection pointers (0x6ccd78 / 0x6ccd80) hand a value
 * from one to the other on the x87 stack, so each pair is one asm block;
 * _ftol (0x497a80) is called by address with its operand in st(0). */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

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
#define A(x) ((uint32_t)(uintptr_t)(x))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))

#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

#define g_race G32(0x4b1960)
#define g_track G32(0x6c1eb0)
#define g_tex_table G32(0x6c1e14)

/* Outside the group. */
#define hud_sprite_at(n, a, f) A(GCALL(0x4387d0, hud_sprite_set)(n, a, f))
#define vec_length_at(v) GCALL(0x480cd0, vec_length)(v)
#define vec_div_at(v, d) GCALL(0x486e90, vec_div)(v, d)
#define text_free_at(t) GCALL(0x479280, text_free)((int32_t)(t))
#define text_create_at(s, a, x, y, flags, b, c) \
    GCALL(0x479060, text_create)((const uint8_t *)(s), a, x, y, flags, b, c)
#define driver_name_at(buf, n) GCALL(0x4484a0, driver_name)(buf, n)
#define message_at(a, b, c) GCALL(0x4600a0, net_kill_message)(a, b, c)
#define track_cell_at(x, y, z, out) A(GCALL(0x419420, track_cell)(x, y, z, out))
#define cell_row_ranges_at(cell, out, level) GCALL(0x440c60, cell_row_ranges)((const uint32_t *)GMEM(cell), out, level)
#define light_set_at(n, pos, a, b, rows, c, d) \
    GCALL(0x4412e0, light_set)(n, pos, (const uint32_t *)GMEM(a), b, rows, c, (uint8_t *)GMEM(d))
#define light_cells_add_at(n, rows) GCALL(0x441000, light_cells_add)(n, rows)
#define mesh_rows_touch_at(v, s) GCALL(0x4411c0, mesh_rows_touch)(v, s)

/* This group's functions (also in build/groups/pg_protos.txt for physics.h). */
void GAMEAPI hud_speed_digits(int32_t v); /* 0x438df0 */
void GAMEAPI hud_lap_place(uint8_t *slot); /* 0x438e60 */
void GAMEAPI hud_time_digits(uint8_t *slot); /* 0x438fb0 */
int32_t GAMEAPI hud_warning_sprite(uint8_t *slot); /* 0x439050 */
void GAMEAPI hud_weapon_icons(uint8_t *slot); /* 0x4390c0 */
void GAMEAPI hud_lap_banner(uint8_t *slot, int32_t hide); /* 0x439270 */
void GAMEAPI hud_banner_set(int32_t final); /* 0x4392e0 */
void GAMEAPI hud_car_markers(uint8_t *slot); /* 0x439400 */
void GAMEAPI hud_fade_set(uint32_t n, uint32_t a, uint32_t b); /* 0x439d50 */
void GAMEAPI hud_fade_apply(uint32_t n, uint32_t level, int32_t tint); /* 0x439d70 */
void GAMEAPI hud_split_layout(uint8_t *slot, int32_t mode); /* 0x439f20 */
void GAMEAPI hud_name_texts(uint8_t *slot, uint32_t a); /* 0x43a000 */
int32_t GAMEAPI car_screen_point(uint8_t *slot, int32_t *x, int32_t *y); /* 0x43a3e0 */
void GAMEAPI hud_gap_bars(uint32_t level, uint8_t *slot); /* 0x43a4a0 */
int32_t GAMEAPI archive_load_by_name(const char *name); /* 0x43b660 */
int32_t GAMEAPI cpu_is_intel(void); /* 0x43c370 */
int32_t GAMEAPI cpu_feature_none(void); /* 0x43c3e0 */
int32_t GAMEAPI leader_hit(uint8_t *body, uint8_t *hit, uint32_t a); /* 0x43f040 */
void GAMEAPI light_cells_remove(uint32_t n); /* 0x4410f0 */
uint32_t GAMEAPI light_slot_new(int32_t *pos, uint32_t a, uint32_t b); /* 0x441220 */
void GAMEAPI light_slot_free(uint32_t n); /* 0x441370 */
void GAMEAPI light_reposition(uint32_t n, const int32_t *pos); /* 0x4413a0 */
void GAMEAPI flare_add(const int32_t *pos, uint32_t kind, uint32_t colour); /* 0x441d80 */
int32_t GAMEAPI flare_quad(int32_t x, int32_t y, int32_t z, uint32_t scale, uint32_t size, uint8_t *p, int32_t kind); /* 0x441de0 */
void GAMEAPI flare_quad_colour(uint8_t *p, uint32_t tex, uint32_t alpha, uint32_t rgb); /* 0x441fa0 */
void GAMEAPI flares_draw(uint8_t *view); /* 0x442060 */

/* ===================================================================== */
/* x87 helpers: the original's instruction sequences. */

/* fild dword v; fstp dword. */
static inline uint32_t pg_i2f(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild qword {v, 0}; fstp dword: an unsigned 32-bit value to float. */
static inline uint32_t pg_u2f(uint32_t v)
{
    uint32_t q[2] = {v, 0};
    uint32_t r;
    __asm__ volatile("fildll %1\n\tfstps %0" : "=m"(r) : "m"(*(uint64_t *)q) : "st");
    return r;
}

/* fld a; fadd b; fstp. */
static inline uint32_t pg_add(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fadd b; fadd c; fstp. */
static inline uint32_t pg_add2(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfadds %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fsub b; fstp. */
static inline uint32_t pg_sub(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fchs; fstp. */
static inline uint32_t pg_chs(uint32_t a)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfchs\n\tfstps %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld a; fsub b; fdiv c; fstp. */
static inline uint32_t pg_sub_div(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfdivs %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fmul b; fmul c; fstp. */
static inline uint32_t pg_mul2(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfmuls %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fistp dword: rounded by the control word. */
static inline int32_t pg_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld a; fcomp b; fnstsw ax: the status word's high byte (C0 = 1, C3 = 0x40). */
static inline uint32_t pg_cmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 0xff;
}

/* fld a; call _ftol. */
static inline int32_t pg_ftol(uint32_t a)
{
#if defined(__i386__)
    int32_t r;
    __asm__ volatile("flds %1\n\tmovl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(a)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
#else
    long double v;
    __asm__ volatile("flds %1\n\tfstpt %0" : "=m"(v) : "m"(a) : "st");
    return (int32_t)(int64_t)v; /* _ftol truncates to 64 bits; eax is the low half */
#endif
}

/* fld a; fadd b; call _ftol. */
static inline int32_t pg_ftol_add(uint32_t a, uint32_t b)
{
#if defined(__i386__)
    int32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tmovl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(a), "m"(b)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
#else
    long double v;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstpt %0" : "=m"(v) : "m"(a), "m"(b) : "st");
    return (int32_t)(int64_t)v;
#endif
}

/* fld a; fmul c; fdiv d; fsubr c; call _ftol: c - a * c / d. */
static inline int32_t pg_ftol_fade(uint32_t a, uint32_t c, uint32_t d)
{
#if defined(__i386__)
    int32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfdivs %3\n\tfsubrs %2\n\tmovl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(a), "m"(c), "m"(d)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
#else
    long double v;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfdivs %3\n\tfsubrs %2\n\tfstpt %0"
                     : "=m"(v)
                     : "m"(a), "m"(c), "m"(d)
                     : "st");
    return (int32_t)(int64_t)v;
#endif
}

/* The transform pointer *0x6ccd78 (record out) then the projection
 * *0x6ccd80, which takes the transform's st(0). */
static inline void pg_transform_project(uint32_t *out)
{
#if defined(__i386__)
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tcall *0x6ccd80"
                     :
                     : "S"(out)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    /* x86-64: the transform returns its st(0) as a float, the projection
     * takes it (subunit_transform_c / wide_project_c) */
    float h = ((float(GAMEAPI *)(uint8_t *))(uintptr_t)G32(0x6ccd78))((uint8_t *)out);

    ((void(GAMEAPI *)(float))(uintptr_t)G32(0x6ccd80))(h);
#endif
}

/* (screen width << 8) / reference width (0x4acff0), signed. */
static inline int32_t pg_screen_scale(void)
{
    return (int32_t)(G32(0x4acc38) << 8) / GI32(0x4acff0);
}

/* A texture record (16 bytes at *0x6c1e14) into a sprite / quad's four
 * texture words: +0 -> d0, +4 -> d1, +8 -> w2, +0xa -> w3. */
static inline uint32_t pg_tex(uint32_t index)
{
    return g_tex_table + (index << 4);
}

/* The OT link: *p = *bucket, *bucket = p (flag bit 31 cleared). */
static inline void pg_link(uint8_t *p, uint32_t bucket)
{
    bucket &= 0x7fffffff;
    U32(p, 0) = G32(bucket);
    G32(bucket) = A(p);
}

/* ===================================================================== */
/* The HUD. */

/* 0x438df0: the speedo's three digits (sprites 0x2d..0x2f). */
void GAMEAPI hud_speed_digits(int32_t v)
{
    hud_sprite_at(0x2d, (v / 100) % 10, 0);
    hud_sprite_at(0x2e, (v / 10) % 10, 0);
    hud_sprite_at(0x2f, v % 10, 0);
}

/* 0x438e60: lap / laps digits (sprites 4..7) and the place with its suffix
 * (8, 0x34; flashing for the leader with tick bit 3), or in race modes 3
 * and 8 the car's two-digit count from race + 8 + car (sprites 4, 6). */
void GAMEAPI hud_lap_place(uint8_t *slot)
{
    uint32_t place = U8(slot, 0x2d);
    uint8_t mode = U8(g_race, 0x551);
    int32_t v, d, f;

    if (mode == 8 || mode == 3) {
        v = U8(g_race, U16(U32(slot, 0), 0x64e) + 8);
        hud_sprite_at(4, (v / 10) % 10, 0);
        hud_sprite_at(6, v % 10, 0);
        return;
    }
    v = U8(slot, 0x2c);
    if (v > (int32_t)U8(g_race, 0x586))
        v = U8(g_race, 0x586);
    d = v / 10;
    hud_sprite_at(4, d, 0);
    hud_sprite_at(5, v - d * 10, 0);
    v = U8(g_race, 0x586);
    d = v / 10;
    hud_sprite_at(6, d, 0);
    hud_sprite_at(7, v - d * 10, 0);
    if (U8(g_race, 0x551) == 1)
        return;
    f = 0;
    if (place == 1 && (G8(0x6c0a50) & 8))
        f = -1;
    hud_sprite_at(8, place, f);
    v = (int32_t)place - 1;
    if (v > 3)
        v = 3;
    hud_sprite_at(0x34, v, f);
}

/* 0x438fb0: the four time readouts (slot +0x4c.. packed nibbles) as 8
 * digit sprites each from 0xd / 0x15 / 0x1d / 0x25; the fourth flashes
 * while the car's counter +0x65c runs (and hides at 0); the flag stays
 * set for nothing after it, as the original. */
void GAMEAPI hud_time_digits(uint8_t *slot)
{
    static const int32_t bases[4] = {0xd, 0x15, 0x1d, 0x25};
    int32_t f = 0, i, k;

    for (i = 0; i < 4; i++) {
        int32_t v = I32(slot, 0x4c + 4 * i);
        if (i == 3) {
            uint8_t *body = PTR(U32(slot, 0));
            uint8_t c = U8(body, 0x65c);
            if (c == 0) {
                f = -1;
            } else {
                if (c & 8)
                    f = -1;
                U8(body, 0x65c) = (uint8_t)(c - 1);
            }
        }
        for (k = 0; k < 8; k++) {
            hud_sprite_at(bases[i] + k, v & 0xf, f);
            v >>= 4;
        }
    }
}

/* 0x439050: sprite 0x41 (shown when slot +0x2f > 0x17, half bright unless
 * its bit 3), grey level from the HUD fade 0x6c7fdc * 4; returns shown. */
int32_t GAMEAPI hud_warning_sprite(uint8_t *slot)
{
    uint32_t s = (G32(0x6c7fdc) & 0xff) * 4;
    int32_t r = 0;
    uint8_t b;
    uint8_t *p;

    if (s >= 0xff)
        s = 0xff;
    b = U8(slot, 0x2f);
    if (b > 0x17) {
        r = 1;
        if (!(b & 8))
            s >>= 1;
    }
    p = PTR(hud_sprite_at(0x41, 0, r - 1));
    {
        uint32_t keep = U32(p, 4) & 0xff000000;
        U16(p, 0x22) = (uint16_t)s;
        U32(p, 4) = ((s << 8 | s) << 8) | keep | s;
    }
    return r;
}

/* 0x4390c0: the two weapon icons (sprites 2, 3) from the car's +0x67c /
 * +0x67d (negative: none, the quad collapses): texture from the table
 * 0x4acff4 + the sprite's base 0x6c7ed4, width from the texture, grey from
 * the HUD fade * 2, halved on alternate ticks when flagged (bit 6; bit 5
 * picks the faster flash). */
void GAMEAPI hud_weapon_icons(uint8_t *slot)
{
    uint8_t *body = PTR(U32(slot, 0));
    int32_t i;

    for (i = 0; i < 2; i++) {
        int32_t c = S8(body, 0x67c + i);
        uint32_t n = (uint32_t)(i != 0) + 2;
        uint8_t *rec = PTR(0x6c3ec0 + n * 0xf0);
        uint8_t *t;
        uint32_t u, keep, a;

        if (c < 0) {
            uint32_t x = U32(rec, 0x14), y = U32(rec, 0x30);
            U32(rec, 0x4c) = x;
            U32(rec, 0x68) = y;
            continue;
        }
        t = PTR(pg_tex((uint32_t)(GS16(0x6c7ed4 + 2 * n) + G8(0x4acff4 + (c & 0x9f)) + U16(g_track, 0x2e))));
        U32(rec, 0x20) = U32(t, 0);
        U32(rec, 0x3c) = U32(t, 4);
        U16(rec, 0x58) = U16(t, 8);
        U16(rec, 0x74) = U16(t, 0xa);
        u = (uint32_t)(((int32_t)((U8(t, 0xb) - U8(t, 1) + 1) * 200) >> 9) * pg_screen_scale()) >> 8;
        U32(rec, 0x4c) = pg_add(pg_u2f(u), U32(rec, 0x14));
        u = (uint32_t)(((int32_t)((U8(t, 0xb) - U8(t, 1) + 1) * 200) >> 9) * pg_screen_scale()) >> 8;
        keep = U32(rec, 4) & 0xff000000;
        U32(rec, 0x68) = pg_add(pg_u2f(u), U32(rec, 0x30));
        a = (G32(0x6c7fdc) & 0xff) * 2;
        if (a >= 0xff)
            a = 0xff;
        if (c & 0x40) {
            uint32_t tick = G32(0x6c0a50);
            tick = (c & 0x20) ? tick >> 1 : tick >> 3;
            if (tick & 1)
                a >>= 1;
        }
        U16(rec, 0x22) = (uint16_t)a;
        U32(rec, 4) = ((a << 8 | a) << 8 | a) | keep;
    }
}

/* 0x439270: the lap banner sprite 0x42 while the car's counter +0x65d runs
 * (shown on its bit 3, hidden when the argument says so); on the last lap
 * the "final lap" frame (+0x38) and texture, else the plain one. */
void GAMEAPI hud_lap_banner(uint8_t *slot, int32_t hide)
{
    uint8_t *body = PTR(U32(slot, 0));
    int32_t v = -1;
    uint8_t c = U8(body, 0x65d);

    if (c != 0) {
        if ((c & 8) == 8)
            v = 0;
        U8(body, 0x65d) = (uint8_t)(c - 1);
    }
    if (hide != 0)
        v = -1;
    if (U8(slot, 0x2c) == U8(g_race, 0x586)) {
        if (v != -1) {
            v += 0x38;
            hud_banner_set(1);
        }
    } else if (v != -1) {
        hud_banner_set(0);
    }
    hud_sprite_at(0x42, 0, v);
}

/* 0x4392e0: the banner sprite's texture (track base + 0x98 final lap, else
 * + 0x60) and its square quad at (0x6c7ed0, 0x6c7ed2), 65 units scaled. */
void GAMEAPI hud_banner_set(int32_t final)
{
    uint32_t idx = U16(g_track, 0x2e) + (final == 1 ? 0x98 : 0x60);
    uint8_t *t = PTR(pg_tex(idx));
    uint32_t f;

    G32(0x6c7cc0) = U32(t, 0);
    G32(0x6c7cdc) = U32(t, 4);
    G16(0x6c7cf8) = U16(t, 8);
    G16(0x6c7d14) = U16(t, 0xa);
    G32(0x6c7ce8) = pg_i2f(GS16(0x6c7ed0));
    G32(0x6c7cb0) = G32(0x6c7ce8);
    G32(0x6c7cd0) = pg_i2f(GS16(0x6c7ed2));
    G32(0x6c7cb4) = G32(0x6c7cd0);
    f = pg_u2f((uint32_t)(pg_screen_scale() * 65) >> 8);
    G32(0x6c7ccc) = pg_add(f, G32(0x6c7cb0));
    G32(0x6c7d04) = pg_add(f, G32(0x6c7ce8));
    G32(0x6c7cec) = pg_add(f, G32(0x6c7cb4));
    G32(0x6c7d08) = pg_add(f, G32(0x6c7cd0));
}

/* 0x439400: per other car: a marker over it when on screen (flag
 * 0x6c7f95 + i, sprite record 0x6c764e - 0x3e + i * 0xf0, position words
 * 0x6c7eb4 + 4i), and in race mode 8 a direction arrow toward it (flag
 * 0x6c7f8f + i, words 0x6c7e9c + 4i, sprite 0x35 + i) from the view car's
 * direction (+0xc8 / +0xd4) and the gap, both unit vectors >> 4 dotted /
 * crossed; returns early (flags left) if either vector is shorter than 32.
 * Flags past the car count up to 6 are cleared. */
void GAMEAPI hud_car_markers(uint8_t *slot)
{
    uint8_t *body = PTR(U32(slot, 0));
    int32_t i = 0;

    if (U8(g_race, 0x54d) != 0) {
        for (i = 0; i < (int32_t)U8(g_race, 0x54d); i++) {
            uint8_t *s = PTR(0x6bfb80 + i * 0x274);
            uint8_t *rec = PTR(0x6c764e + i * 0xf0);
            uint8_t *w = PTR(0x6c7eb6 + 4 * i);
            uint8_t *other;
            int32_t x, y;

            G8(0x6c7f95 + i) = 0;
            G8(0x6c7f8f + i) = 0;
            if (s == slot)
                continue;
            other = PTR(U32(s, 0));
            if (car_screen_point(s, &x, &y)) {
                uint8_t *t;
                uint32_t f, u;
                int32_t k;
                uint16_t w3;

                G8(0x6c7f95 + i) = 1;
                t = PTR(pg_tex(U16(g_track, 0x2e) + 0xa3));
                U32(rec, -0x1e) = U32(t, 0);
                U32(rec, -2) = U32(t, 4);
                U16(rec, 0x1a) = U16(t, 8);
                w3 = U16(t, 0xa);
                x -= (int32_t)(U8(rec, -2) - U8(rec, -0x1e) + 1) >> 2;
                U16(rec, 0x36) = w3;
                y -= GI32(0x6cd410) * 5 >> 8;
                S16(w, -2) = (int16_t)x;
                S16(w, 0) = (int16_t)y;
                f = pg_i2f(S16(w, -2));
                U32(rec, 0xa) = f;
                U32(rec, -0x2e) = f;
                f = pg_i2f(S16(w, 0));
                U32(rec, -0xe) = f;
                U32(rec, -0x2a) = f;
                k = pg_screen_scale();
                u = (uint32_t)(((int32_t)(U8(rec, -2) - U8(rec, -0x1e) + 1) >> 2) * k) >> 8;
                f = pg_u2f(u);
                u = (uint32_t)(((int32_t)(U8(rec, 0x1b) - U8(rec, -0x1d) + 1) >> 2) * k) >> 8;
                U32(rec, -0x12) = pg_add(f, U32(rec, -0x2e));
                U32(rec, 0x26) = pg_add(f, U32(rec, 0xa));
                f = pg_u2f(u);
                U32(rec, 0xe) = pg_add(f, U32(rec, -0x2a));
                U32(rec, 0x2a) = pg_add(f, U32(rec, -0xe));
            }
            if (U8(g_race, 0x551) == 8) {
                int32_t a[3], b[3], len, lc, ni, cross, dot, k, p, q;
                uint32_t m, xs, ds, f, u;

                G8(0x6c7f8f + i) = 1;
                a[0] = S16(body, 0xc8);
                a[1] = 0;
                a[2] = S16(body, 0xd4);
                len = vec_length_at(a);
                if (len < 0x20)
                    return;
                vec_div_at(a, len);
                lc = a[2];
                b[0] = (I32(other, 0x34) - I32(body, 0x34)) >> 8;
                b[1] = 0;
                b[2] = (I32(other, 0x3c) - I32(body, 0x3c)) >> 8;
                ni = -a[0];
                len = vec_length_at(b);
                if (len < 0x20)
                    return;
                vec_div_at(b, len);
                lc >>= 4;
                a[0] >>= 4;
                b[2] >>= 4;
                a[2] >>= 4;
                b[0] >>= 4;
                ni >>= 4;
                cross = (b[2] * ni + b[0] * lc) >> 10;
                dot = (b[2] * a[2] + b[0] * a[0]) >> 10;
                k = pg_screen_scale();
                m = (uint32_t)(k * 56) >> 8;
                xs = U8(g_race, 0x58b) ? (uint32_t)-(int32_t)(m * (uint32_t)cross) : m * (uint32_t)cross;
                xs >>= 16;
                ds = (uint32_t)-(int32_t)(m * (uint32_t)dot) >> 16;
                p = GS16(0x6c7dd4) + (int32_t)xs +
                    (int32_t)((uint32_t)(((int32_t)(((G32(0x6c41cc) & 0xff) - (G32(0x6c41b0) & 0xff) + 1) * 200) >>
                                          10) * k) >> 8);
                q = (int32_t)((uint32_t)(((int32_t)((G8(0x6c41e9) - G8(0x6c41b1) + 1) * 200) >> 10) * k) >> 8) +
                    (int32_t)ds + GS16(0x6c7dd6);
                p -= (int32_t)((uint32_t)(((int32_t)(U8(rec, -0x5a2) - U8(rec, -0x5be) + 1) >> 2) * k) >> 8);
                S16(w, -0x1a) = (int16_t)p;
                q -= (int32_t)((uint32_t)(((int32_t)(U8(rec, -0x585) - U8(rec, -0x5bd) + 1) >> 2) * k) >> 8);
                S16(w, -0x18) = (int16_t)q;
                f = pg_i2f(S16(w, -0x1a));
                U32(rec, -0x596) = f;
                U32(rec, -0x5ce) = f;
                f = pg_i2f(S16(w, -0x18));
                U32(rec, -0x5ae) = f;
                U32(rec, -0x5ca) = f;
                u = (uint32_t)(((int32_t)(U8(rec, -0x5a2) - U8(rec, -0x5be) + 1) >> 1) * k) >> 8;
                f = pg_u2f(u);
                u = (uint32_t)(((int32_t)(U8(rec, -0x585) - U8(rec, -0x5bd) + 1) >> 1) * k) >> 8;
                U32(rec, -0x5b2) = pg_add(f, U32(rec, -0x5ce));
                U32(rec, -0x57a) = pg_add(f, U32(rec, -0x596));
                f = pg_u2f(u);
                U32(rec, -0x592) = pg_add(f, U32(rec, -0x5ca));
                U32(rec, -0x576) = pg_add(f, U32(rec, -0x5ae));
            }
        }
        if (i > 5)
            return;
    }
    for (; i < 6; i++) {
        G8(0x6c7f95 + i) = 0;
        G8(0x6c7f8f + i) = 0;
    }
}

/* 0x439d50: HUD fade state n (4 bytes at 0x6c7fe0): a + 1, countdown b. */
void GAMEAPI hud_fade_set(uint32_t n, uint32_t a, uint32_t b)
{
    G8(0x6c7fe0 + n * 4) = (uint8_t)(a + 1);
    G8(0x6c7fe1 + n * 4) = (uint8_t)b;
}

/* 0x439d70: HUD brightness (0..0xff, full when 0x6ccd84 bit 2) into every
 * sprite's colour (0x43 records of 0xf0 at 0x6c3ec0, +4 and the +0x22
 * shade; 2, 3 and 0x41 kept): grey, tinted by tint 1 / 2 for the digit and
 * gauge sprites, red only for 0x15..0x1c, blue for 0x31 / 0x33; the fade
 * state n counts down; 0x469d60 in race mode 4; the two split-screen
 * boxes (0x6c7fb0, 0x6c7fcc) at level * 4 (max 0xa0). */
void GAMEAPI hud_fade_apply(uint32_t n, uint32_t level, int32_t tint)
{
    uint32_t p = level, mask, rgb, all, red, blue, tinted, k;
    uint8_t b;

    if ((G8(0x6ccd84) & 4) || p > 0xff)
        p = 0xff;
    mask = G32(0x6c3ec4) & 0xff000000;
    G8(0x6c7fdc) = (uint8_t)p;
    rgb = ((p << 8 | p) << 8) | p;
    red = mask | p;
    blue = mask | (p << 16);
    all = mask | rgb;
    if (tint == 1)
        tinted = all & 0xff0000ff;
    else if (tint == 2)
        tinted = all & 0xff00ff00;
    else
        tinted = all;
    b = G8(0x6c7fe1 + n * 4);
    if (b > 1) {
        G8(0x6c7fe1 + n * 4) = (uint8_t)(b - 1);
    } else if (b == 1) {
        G8(0x6c7fe1 + n * 4) = 0;
        G8(0x6c7fe0 + n * 4) = 1;
    } else {
        G8(0x6c7fe0 + n * 4) = 0;
    }
    for (k = 0; k < 0x43; k++) {
        uint32_t v;

        if (k == 2 || k == 3 || k == 0x41)
            continue;
        if ((k >= 4 && k <= 0x14) || (k >= 0x1d && k <= 0x24) || k == 0x32 || k == 0x34)
            v = tinted;
        else if (k >= 0x15 && k <= 0x1c)
            v = red;
        else if (k == 0x31 || k == 0x33)
            v = blue;
        else
            v = all;
        G32(0x6c3ec4 + k * 0xf0) = v;
        G16(0x6c3ee2 + k * 0xf0) = (uint16_t)(v & 0xff);
    }
    if (U8(g_race, 0x551) == 4)
        GCALL(0x469d60, meters_colour_set)(all);
    if (U8(g_race, 0x552) != 0) {
        uint32_t q = p * 4, keep;
        if (q > 0xa0)
            q = 0xa0;
        keep = G32(0x6c7fb0) & 0xff000000;
        G32(0x6c7fb0) = ((q << 8 | q) << 8) | keep | q;
        G32(0x6c7fcc) = ((q << 8 | q) << 8) | keep | q;
    }
}

/* 0x439f20: split-screen race (mode 2) layout: the view's position bar
 * marker (box 0x6c7fa4 for player 0, 0x6c7fc0 for player 1: +0x10 x, +0x12
 * y) along the screen by the car's progress +0x34 (0 before the first lap);
 * mode 1 lays it out horizontally. */
void GAMEAPI hud_split_layout(uint8_t *slot, int32_t mode)
{
    uint32_t k = U32(slot, 0x34);
    uint32_t h = U16(g_race, 0xa82), w = U16(g_race, 0xa80);
    uint8_t *box;
    uint32_t x, d;

    if (U8(slot, 0x2c) < 1)
        k = 0;
    if (U16(U32(slot, 0), 0x64e) == 0) {
        box = PTR(0x6c7fa4);
        if (mode == 1) {
            U16(box, 0x10) = (uint16_t)(((h - 0x10) * k >> 16) + 5);
            U16(box, 0x12) = (uint16_t)(w - 10);
            return;
        }
        x = h - 10;
        d = w - 0x10;
    } else {
        box = PTR(0x6c7fc0);
        if (mode == 1) {
            U16(box, 0x12) = 3;
            U16(box, 0x10) = (uint16_t)(((h - 0x10) * k >> 16) + 5);
            return;
        }
        x = 3;
        d = w - 10;
    }
    d = d * k >> 16;
    U16(box, 0x10) = (uint16_t)x;
    U16(box, 0x12) = (uint16_t)(w - d - 5);
}

/* 0x43a000: the name texts: frees the old ones (0x4beb08 list, 0x4beaf0
 * over cars; -1 = none), then unless hidden (0x6cce40 & 0x12, the car past
 * the last lap, race modes 0 / 1 / 4 / 9): names from 0x45f650 (else the
 * driver name 0x4484a0); mode 8: a name over each other car on screen;
 * otherwise by place, a column at x 0x14 down the middle (unless race
 * +0x552) and one over each other car on screen. a goes to text_create. */
void GAMEAPI hud_name_texts(uint8_t *slot, uint32_t a)
{
    uint32_t count = 6;
    uint32_t lists[12]; /* the names, then 0x45f650's second list: as the
                         * original's stack, a seventh or eighth car's name
                         * is read from the second list (sic) */
    uint32_t *names = lists, *info = lists + 6;
    char buf[0x20];
    int32_t x, y, i, j;
    uint8_t mode;

    for (i = 0; i < 6; i++) {
        if (GI32(0x4beb08 + 4 * i) != -1) {
            text_free_at(G32(0x4beb08 + 4 * i));
            GI32(0x4beb08 + 4 * i) = -1;
        }
        if (GI32(0x4beaf0 + 4 * i) != -1) {
            text_free_at(G32(0x4beaf0 + 4 * i));
            GI32(0x4beaf0 + 4 * i) = -1;
        }
    }
    if (G8(0x6cce40) & 0x12)
        return;
    if (U8(slot, 0x2c) > U8(g_race, 0x586))
        return;
    mode = U8(g_race, 0x551);
    if (mode == 1 || mode == 0 || mode == 9 || mode == 4)
        return;
    GCALL(0x45f650, net_player_list)((int32_t *)&count, info, names);
    if (U8(g_race, 0x551) == 8) {
        for (i = 0; i < (int32_t)U8(g_race, 0x54d); i++) {
            uint8_t *s = PTR(0x6bfb80 + i * 0x274);
            const char *name;
            int32_t r;

            if (s == slot || !car_screen_point(s, &x, &y))
                continue;
            y -= GI32(0x6cd410) * 22 >> 8;
            name = (const char *)(uintptr_t)names[i];
            if (name != NULL && name[0] != 0) {
                r = text_create_at(name, 0, x, y, 0x302, a, 0);
            } else {
                driver_name_at(buf, U8(g_race, i + 0x1c));
                r = text_create_at(buf, 0, x, y, 0x302, a, 0);
            }
            GI32(0x4beaf0 + 4 * i) = r;
        }
        return;
    }
    {
        int32_t n = U8(g_race, 0x54d);
        int32_t row = -(int32_t)((uint32_t)n >> 1) * 18;

        if (n <= 0)
            return;
        for (j = 0; j < n; j = j + 1, n = U8(g_race, 0x54d)) {
            const char *name;
            uint8_t *s;

            for (i = 0; i < n; i++)
                if ((int32_t)G8(0x6bfbad + i * 0x274) - 1 == j)
                    break;
            if (i >= n)
                continue;
            name = (const char *)(uintptr_t)names[i];
            if (name == NULL || name[0] == 0) {
                driver_name_at(buf, U8(g_race, i + 0x1c));
                name = buf;
            }
            if (U8(g_race, 0x552) == 0)
                GI32(0x4beb08 + 4 * j) = text_create_at(name, 0, 0x14,
                                                        (GI32(0x6cd410) * row >> 8) + (U16(g_race, 0xa80) >> 1),
                                                        0x300, a, 0);
            s = PTR(0x6bfb80 + i * 0x274);
            if (s != slot && car_screen_point(s, &x, &y)) {
                y -= GI32(0x6cd410) * 22 >> 8;
                GI32(0x4beaf0 + 4 * j) = text_create_at(name, 0, x, y, 0x302, a, 0);
            }
            row += 0x12;
        }
    }
}

/* 0x43a3e0: a car's screen point: its position >> 16 (y - 16) into the GTE
 * vector, transform + project; on screen when the depth (+4) is 10..3500,
 * then x / y (+8 / +0xc) less the screen offset (0x6ccd54 / 58), rounded.
 * The vector's pad word (0x6ccc6a) gets 0 here, leftover stack in the
 * original (the transforms read three words). */
int32_t GAMEAPI car_screen_point(uint8_t *slot, int32_t *x, int32_t *y)
{
    uint8_t *body = PTR(U32(slot, 0));
    uint32_t rec[8];
    uint16_t vx = (uint16_t)(I32(body, 0x34) >> 16);
    uint16_t vy = (uint16_t)((I32(body, 0x38) >> 16) - 0x10);
    uint16_t vz = (uint16_t)(I32(body, 0x3c) >> 16);

    G32(0x6ccc64) = (uint32_t)vy << 16 | vx;
    G32(0x6ccc68) = vz;
    pg_transform_project(rec);
    if (pg_cmp(rec[1], G32(0x4a3334)) & 1)
        return 0;
    if (!(pg_cmp(rec[1], G32(0x4a339c)) & 0x41))
        return 0;
    *x = pg_fist(pg_sub(rec[2], G32(0x6ccd54)));
    *y = pg_fist(pg_sub(rec[3], G32(0x6ccd58)));
    return 1;
}

/* 0x43a4a0: the gap bars (race modes other than 0 / 1 / 4 / 8 / 9, one
 * view, not 0x6ccd84 bit 2): per place a 0x84-byte quad (code 0x8c / 0x8e)
 * in the packet pool 0x4be4c0, x by the gap to the leader (lap << 16 +
 * progress, both capped at laps + 1), linked in the frame's OT (*0x6ce608
 * + 4); a second quad unless the gap is at the clamp. For the first view's
 * slot, a car more than a lap behind the leader gets message 0xd once
 * (+0x270). level is the alpha. The leader's progress is leftover stack in
 * the original when no car is placed first; 0 here. */
void GAMEAPI hud_gap_bars(uint32_t level, uint8_t *slot)
{
    uint8_t *p = PTR(0x4be4c0);
    uint8_t mode = U8(g_race, 0x551);
    uint32_t c1, c2, c3;
    int32_t lead = 0, row, j, i, n;

    if (mode == 8 || mode == 0 || mode == 9 || mode == 1 || mode == 4)
        return;
    if (U8(g_race, 0x552) != 0 || (G8(0x6ccd84) & 4))
        return;
    if (level > 0xff)
        level = 0xff;
    c1 = (((level << 8) - level) << 8) & 0xffff0000;
    c3 = (level << 15) & 0xffff0000;
    c2 = ((level << 7) >> 8) | c3;
    n = U8(g_race, 0x54d);
    for (i = 0; i < n; i++) {
        if (G8(0x6bfbad + i * 0x274) == 1) {
            int32_t lim;
            lead = (int32_t)(G8(0x6bfbac + i * 0x274) << 16) + GI32(0x6bfbb4 + i * 0x274);
            lim = (int32_t)(U8(g_race, 0x586) + 1) << 16;
            if (lead >= lim)
                lead = lim;
            break;
        }
    }
    row = -(int32_t)((uint32_t)n >> 1) * 18;
    if (n <= 0)
        return;
    for (j = 0; j < n; j = j + 1, n = U8(g_race, 0x54d)) {
        int32_t m, lim, d, q;
        uint32_t f3, f2, yf, y2;
        uint8_t *s;

        for (i = 0; i < n; i++)
            if ((int32_t)G8(0x6bfbad + i * 0x274) - 1 == j)
                break;
        if (i >= n)
            continue;
        s = PTR(0x6bfb80 + i * 0x274);
        m = (int32_t)(U8(s, 0x2c) << 16) + I32(s, 0x34);
        lim = (int32_t)(U8(g_race, 0x586) + 1) << 16;
        if (m >= lim)
            m = lim;
        d = (lead - m) >> 8;
        if (slot == PTR(0x6bfb80) && d > 0x100) {
            if (U32(s, 0x270) == 0)
                message_at(i, 0, 0xd);
            U32(s, 0x270) = 1;
        } else {
            U32(s, 0x270) = 0;
        }
        if (d > 0x100)
            d = 0x100;
        else if (d < -0x100)
            d = -0x100;
        q = (d * 160 >> 8) * GI32(0x6cd40c) >> 9;
        f3 = pg_add(G32(0x6ccd54), G32(0x4a3348));
        f2 = pg_add(pg_i2f(q), f3);
        U32(p, 4) = c1;
        U32(p, 0x44) = c1;
        U32(p, 0x24) = c2;
        U32(p, 0x50) = f3;
        U32(p, 0x10) = f3;
        U32(p, 0x64) = c2;
        U16(p, 0x42) = 0x3f;
        U32(p, 0x6c) = 0x40200000;
        U32(p, 0x4c) = 0x40200000;
        U32(p, 0x70) = f2;
        U32(p, 0x30) = f2;
        U32(p, 0x2c) = 0x40200000;
        U32(p, 0xc) = 0x40200000;
        U32(p, 8) = 0x40;
        yf = pg_add(pg_i2f((int32_t)(U16(g_race, 0xa80) >> 1) + (GI32(0x6cd410) * row >> 8)), G32(0x6ccd58));
        U32(p, 0) = (U32(p, 0) & 0xffffff) | 0xc000000;
        U32(p, 0x34) = yf;
        U32(p, 0x14) = yf;
        y2 = pg_add(pg_i2f((int32_t)(G32(0x6cd410) << 4) >> 8), yf);
        U8(p, 7) = 0x8c;
        U32(p, 0x74) = y2;
        U32(p, 0x54) = y2;
        if (!(G8(0x6ccd84) & 4))
            U8(p, 7) = 0x8e;
        pg_link(p, U32(G32(0x6ce608), 4));
        p += 0x84;
        if (d < 0x100) {
            uint32_t xf;

            U16(p, 0x42) = 0x3f;
            U32(p, 0x6c) = 0x40200000;
            U32(p, 0x4c) = 0x40200000;
            U32(p, 0x2c) = 0x40200000;
            U32(p, 0xc) = 0x40200000;
            U32(p, 8) = 0x40;
            U32(p, 4) = c3;
            U32(p, 0x24) = c3;
            U32(p, 0x44) = c3;
            U32(p, 0x64) = c3;
            {
                uint32_t v = U32(p, -0x54);
                uint32_t tag = U32(p, 0);
                U32(p, 0x50) = v;
                U32(p, 0x10) = v;
                v = U32(p, -0x70);
                U32(p, 0x34) = v;
                U32(p, 0x14) = v;
                v = U32(p, -0x30);
                U32(p, 0x74) = v;
                U32(p, 0x54) = v;
                U32(p, 0) = (tag & 0xffffff) | 0xc000000;
            }
            U8(p, 7) = 0x8c;
            xf = pg_add2(pg_i2f(GI32(0x6cd40c) * 160 >> 9), G32(0x6ccd54), G32(0x4a3348));
            U32(p, 0x70) = xf;
            U32(p, 0x30) = xf;
            if (!(G8(0x6ccd84) & 4))
                U8(p, 7) = 0x8e;
            pg_link(p, U32(G32(0x6ce608), 4));
            U16(p, 0x22) = (uint16_t)(level >> 1);
            p += 0x84;
        }
        row += 0x12;
    }
}

/* ===================================================================== */
/* Archive, CPU, leader. */

/* 0x43b660: load an archive entry by name with the arena allocator hooks
 * (0x4b4a84 / 0x4b4a88 = 0x4445e0 / 0x444570 around 0x43b630), or from
 * disk (0x465340) when the name isn't in the archive. Returns the load's
 * result. */
int32_t GAMEAPI archive_load_by_name(const char *name)
{
    int32_t idx = (int32_t)GCALL(0x43b550, archive_find_by_name)(A(name));
    uint32_t alloc, release;
    int32_t r;

    if ((int16_t)idx == -1)
        return (int32_t)GCALL(0x465340, file_load_whole)(A(name));
    alloc = G32(0x4b4a84);
    release = G32(0x4b4a88);
    G32(0x4b4a84) = 0x4445e0;
    G32(0x4b4a88) = 0x444570;
    r = (int32_t)GCALL(0x43b630, archive_load_by_index)((uint32_t)idx);
    G32(0x4b4a88) = release;
    G32(0x4b4a84) = alloc;
    return r;
}

/* 0x43c370: 1 if cpuid 0's vendor string is "GenuineIntel" (0x4af428). */
int32_t GAMEAPI cpu_is_intel(void)
{
    uint32_t v[4], a = 0, b, c, d;
    const uint8_t *p = (const uint8_t *)v, *q = PTR(0x4af428);

    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    v[0] = b;
    v[1] = d;
    v[2] = c;
    v[3] = 0;
    for (;;) {
        if (*p != *q)
            return 0;
        if (*p == 0)
            return 1;
        p++;
        q++;
    }
}

/* 0x43c3e0: a feature check that always says no. */
int32_t GAMEAPI cpu_feature_none(void)
{
    return 0;
}

/* 0x43f040: the leader missile's hit callback: a hit car takes the blast
 * (0x4599c0); a car hit by another car's leader (body +0x18c the firer,
 * the victim not in state 8 at +0x70c) gets message 0xb; the missile ends
 * (0x459610). Returns 0. */
int32_t GAMEAPI leader_hit(uint8_t *body, uint8_t *hit, uint32_t a)
{
    if (hit != NULL) {
        uint8_t *o;
        GCALL(0x4599c0, car_blast_hit)(hit, (const uint8_t *)GMEM(a));
        if ((U32(hit, 0) & 0x1ff) == 3 && (o = PTR(U32(body, 0x18c))) != NULL && U8(hit, 0x70c) != 8)
            message_at(U16(hit, 0x64e), U16(o, 0x64e), 0xb);
    }
    GCALL(0x459610, homer_end)(body, 0);
    return 0;
}

/* ===================================================================== */
/* Light slots (64 bytes at 0x5d8000, 0x47 of them; count 0x5d7fe0). */

/* 0x4410f0: take light n out of its cells: up to 4 cell spans at +0x14
 * (section | first row << 12 | last row << 24, 0 ends); in each row of 8
 * entries (section table 0x5d7de0) the first entry for n (>> 22) is
 * cleared and the row marked (0x4411c0). */
void GAMEAPI light_cells_remove(uint32_t n)
{
    uint32_t k;

    for (k = 0; k < 4; k++) {
        uint32_t rec = G32(0x5d8014 + (n << 6) + 4 * k);
        uint32_t lo, first, last, base;

        if (rec == 0)
            return;
        lo = rec & 0xff;
        last = rec >> 24;
        first = (rec >> 12) & 0xff;
        base = G32(0x5d7de0 + lo * 4);
        if (first <= last) {
            uint32_t row = base + first * 32;
            uint32_t rows = last - first + 1;
            do {
                uint32_t c;
                for (c = 0; c < 8; c++) {
                    uint32_t v = U32(row, 4 * c);
                    if (v != 0 && (v >> 22) == n) {
                        mesh_rows_touch_at(U32(row, 4 * c), lo);
                        U32(row, 4 * c) = 0;
                        break;
                    }
                }
                row += 32;
            } while (--rows != 0);
        }
    }
}

/* 0x441220: a new light slot from the search start 0x5d7fe8 (first with
 * +0x24 == 0; 0 if none) at pos, set up by 0x4412e0 with the rows of its
 * cell (0x440c60, level b & 0xffff); raises the count. Returns the slot. */
uint32_t GAMEAPI light_slot_new(int32_t *pos, uint32_t a, uint32_t b)
{
    uint32_t idx = G32(0x5d7fe8);
    uint32_t p = 0x5d8000 + (idx << 6);
    uint32_t rows[4];
    uint32_t zero[2];
    int32_t out;
    uint32_t cell;

    if (p < 0x5d91c0) {
        uint32_t q = p + 0x24;
        while (G16(q) != 0) {
            p += 0x40;
            q += 0x40;
            idx++;
            if ((int32_t)q >= 0x5d91e4)
                break;
        }
    }
    if (p == 0x5d91c0)
        return 0;
    cell = track_cell_at(pos[0], pos[1], pos[2], &out);
    cell_row_ranges_at(cell, rows, b & 0xffff);
    zero[0] = 0;
    zero[1] = 0;
    light_set_at(idx, pos, a, b, rows, zero, 0);
    if (idx >= G32(0x5d7fe0))
        G32(0x5d7fe0) = G32(0x5d7fe0) + 1;
    return idx;
}

/* 0x441370: free light slot n (cells out, +0x24 = 0, count down if last). */
void GAMEAPI light_slot_free(uint32_t n)
{
    light_cells_remove(n);
    G16(0x5d8024 + (n << 6)) = 0;
    if (G32(0x5d7fe0) == n + 1)
        G32(0x5d7fe0) = G32(0x5d7fe0) - 1;
}

/* 0x4413a0: move light n to pos; re-file its cells when a coordinate's top
 * byte changes; +0x12 = 2. */
void GAMEAPI light_reposition(uint32_t n, const int32_t *pos)
{
    uint32_t rec = 0x5d8000 + (n << 6);

    if (((U32(rec, 0) ^ (uint32_t)pos[0]) | (U32(rec, 4) ^ (uint32_t)pos[1]) | (U32(rec, 8) ^ (uint32_t)pos[2])) &
        0xff000000) {
        uint32_t rows[4];
        int32_t out;
        uint32_t cell;

        light_cells_remove(n);
        cell = track_cell_at(pos[0], pos[1], pos[2], &out);
        cell_row_ranges_at(cell, rows, U16(rec, 0x10));
        light_cells_add_at(n, rows);
    }
    U32(rec, 0) = (uint32_t)pos[0];
    U32(rec, 4) = (uint32_t)pos[1];
    U32(rec, 8) = (uint32_t)pos[2];
    U16(rec, 0x12) = 2;
}

/* ===================================================================== */
/* Lens flares (10 records of 0x4c0 at 0x4beb90, count 0x4beb88). */

/* 0x441d80: queue a flare: kind, position >> 16, colour. */
void GAMEAPI flare_add(const int32_t *pos, uint32_t kind, uint32_t colour)
{
    int32_t c = GI32(0x4beb88);

    if (c < 10) {
        uint32_t r = 0x4beb90 + (uint32_t)c * 0x4c0;
        U32(r, 0) = kind;
        U16(r, 4) = (uint16_t)(pos[0] >> 16);
        U16(r, 6) = (uint16_t)(pos[1] >> 16);
        U16(r, 8) = (uint16_t)(pos[2] >> 16);
        U32(r, 0xc) = colour;
        GI32(0x4beb88) = c + 1;
    }
}

/* 0x441de0: a flare quad (0x78 bytes, 4 vertices of 0x1c) of half size
 * 2 * size * scale / 256 (scale 0xff: size) round (x, y); z 3.0, or z - 10
 * for kind 2; linked in the OT: kind 1 the front bucket (0x6cd6e8 /
 * 0x6cdd00), kind 2 by depth ((z - 10) >> 5, halved past 0x100 / 0x180,
 * max 0x1ff; nothing at <= 0), else bucket 1. Returns 1. */
int32_t GAMEAPI flare_quad(int32_t x, int32_t y, int32_t z, uint32_t scale, uint32_t size, uint8_t *p,
                           int32_t kind)
{
    uint32_t s, zf;
    int32_t t, d;

    s = scale == 0xff ? size : scale * size >> 8;
    if (s < 2)
        return 1;
    t = (int32_t)(s << 1);
    U32(p, 0x10) = pg_i2f(x + t);
    U32(p, 0x2c) = pg_i2f(x + t);
    U32(p, 0x48) = pg_i2f(x - t);
    U32(p, 0x64) = pg_i2f(x - t);
    U32(p, 0x14) = pg_i2f(y - t);
    U32(p, 0x30) = pg_i2f(y + t);
    U32(p, 0x4c) = pg_i2f(y - t);
    U32(p, 0x68) = pg_i2f(y + t);
    z -= 10;
    zf = kind == 2 ? pg_i2f(z) : 0x40400000;
    U32(p, 0x60) = zf;
    U32(p, 0x44) = zf;
    U32(p, 0x28) = zf;
    U32(p, 0xc) = zf;
    U32(p, 8) = 0x40;
    if (kind == 1) {
        pg_link(p, G32(0x6cce84) ? G32(0x6cd6e8) : G32(0x6cdd00));
        return 1;
    }
    if (kind == 2) {
        d = z >> 5;
        if (d > 0x100) {
            d = d / 2 + 0x80;
            if (d > 0x180) {
                d = d / 2 + 0xc0;
                if (d > 0x1ff)
                    d = 0x1ff;
            }
        }
        if (d <= 0)
            return 1;
        pg_link(p, G32(0x6cce84) ? G32(0x6cdd80 + 4 * d) : G32(0x6cd510 + 4 * d));
        return 1;
    }
    pg_link(p, G32(0x6cce84) ? G32(0x6cdd84) : G32(0x6cd514));
    return 1;
}

/* 0x441fa0: a flare quad's texture (track base + tex) and colour code
 * 0x9a: rgb as given with alpha in +0x22 when 0x6ccd84 bit 0, else each
 * channel * alpha >> 8 (each product's upper bits carry into the next
 * channel's multiplicand, as the original's byte register juggling). */
void GAMEAPI flare_quad_colour(uint8_t *p, uint32_t tex, uint32_t alpha, uint32_t rgb)
{
    uint8_t *t = PTR(pg_tex(U16(g_track, 0x2e) + tex));
    uint32_t r1, r2, r3;

    U32(p, 0x20) = U32(t, 0);
    U32(p, 0x3c) = U32(t, 4);
    U16(p, 0x58) = U16(t, 8);
    U16(p, 0x74) = U16(t, 0xa);
    if (G8(0x6ccd84) & 1) {
        U32(p, 4) = (rgb & 0xffffff) | 0x9a000000;
        U8(p, 0x22) = (uint8_t)alpha;
        return;
    }
    r1 = ((rgb >> 16) & 0xff) * alpha >> 8;
    r2 = ((r1 & 0xffffff00) | ((rgb >> 8) & 0xff)) * alpha >> 8;
    r3 = ((r2 & 0xffffff00) | (rgb & 0xff)) * alpha >> 8;
    U32(p, 4) = 0x9a000000 | (r1 & 0xff) << 16 | (r2 & 0xff) << 8 | (r3 & 0xff);
    U8(p, 0x22) = (uint8_t)alpha;
}

/* 0x442060: draw the queued flares (race +0x57c < 7), then empty the
 * queue. Size scale 0x6cd40c * 0.5 * 255 >> 8. Kinds 0 / 1: a glow (size
 * 0x20 / 0x40) faded by depth (255 - z * 255 / 12000) when in front and
 * z <= 6000. Kind 2, the sun (not with 0x6ccd84 bit 2): when on screen, a
 * glow faded by the light level (0x4b2c58..0x4b2c60) and by its distance
 * from the screen centre, and unless clipped 9 streak quads along the line
 * through the centre (tables 0x4af520.., 0x4af54c.., 0x4af570 / 584).
 * Kind 4: a headlight's 5 quads along the view car's left-right axis
 * (0x407e90, 0x407220, 0x426e90, 0x426e10), z <= 6000 (fade over 8000),
 * dimmed by how far each is off centre (tables 0x4af4e4 / 4f8 / 50c). */
void GAMEAPI flares_draw(uint8_t *view)
{
    int32_t scale, n;

    scale = pg_fist(pg_mul2(pg_u2f(G32(0x6cd40c)), G32(0x4a3338), G32(0x4a332c))) >> 8;
    if (I32(g_race, 0x57c) >= 7)
        return;
    for (n = 0; n < GI32(0x4beb88); n++) {
        uint8_t *r = PTR(0x4beb90 + (uint32_t)n * 0x4c0);
        uint8_t *pkt = r + 0x10;
        uint32_t rec[8];
        uint32_t kind = U32(r, 0);

        if (kind > 4)
            continue;
        if (kind == 0 || kind == 1) {
            int32_t fade, size = 0x20;
            G32(0x6ccc64) = U32(r, 4);
            G32(0x6ccc68) = U32(r, 8);
            pg_transform_project(rec);
            if (rec[0] != 0)
                continue;
            if (!(pg_cmp(rec[1], G32(0x4a33ac)) & 0x41))
                continue;
            fade = pg_ftol_fade(rec[1], G32(0x4a332c), G32(0x4a33a8)) * scale >> 8;
            if (kind == 1)
                size = 0x40;
            flare_quad_colour(pkt, 0x3f, 0x80, U32(r, 0xc));
            {
                int32_t y = pg_ftol(rec[3]);
                int32_t x = pg_ftol(rec[2]);
                flare_quad(x, y, 0, (uint32_t)fade, (uint32_t)size, pkt, 0);
            }
        } else if (kind == 2) {
            int32_t level, i, a, alpha;
            uint32_t X[10], Y[10], dx, dy, b, sh;

            level = 0x10000 / (0x100 - GI32(0x4b2c58));
            level = level * (GI32(0x4b2c60) - GI32(0x4b2c58)) >> 8;
            if (G8(0x6ccd84) & 4)
                continue;
            G32(0x6ccc64) = U32(r, 4);
            G32(0x6ccc68) = U32(r, 8);
            pg_transform_project(rec);
            if (U8(rec, 0) & 0x10)
                continue;
            X[0] = pg_sub(rec[2], G32(0x6ccd5c));
            Y[0] = pg_sub(rec[3], G32(0x6ccd60));
            X[9] = pg_chs(X[0]);
            Y[9] = pg_chs(Y[0]);
            dx = pg_sub_div(X[9], X[0], G32(0x4a33a4));
            dy = pg_sub_div(Y[9], Y[0], G32(0x4a33a4));
            for (i = 0; i < 4; i++) {
                X[i + 1] = pg_add(dx, X[i]);
                X[8 - i] = pg_sub(X[9 - i], dx);
                Y[i + 1] = pg_add(dy, Y[i]);
                Y[8 - i] = pg_sub(Y[9 - i], dy);
            }
            sh = (uint32_t)(U16(g_race, 0xa82) & ~1u) << 4;
            a = pg_ftol(X[0]);
            a = (a ^ (a >> 31)) - (a >> 31);
            b = ((uint32_t)a << 12) / sh;
            if (b >= 0xff)
                b = 0xff;
            b = ((0xff - b) * G32(0x4af584) >> 8) * (uint32_t)level >> 8;
            {
                uint32_t e = G32(0x4af570) * (uint32_t)level;
                alpha = (int32_t)(e & ~0x7fu) < 0x7f80 ? (int32_t)e >> 7 : 0xff;
            }
            flare_quad_colour(pkt, G32(0x4af548), (uint32_t)alpha, U32(r, 0xc));
            {
                int32_t y = pg_ftol_add(G32(0x6ccd60), Y[0]);
                int32_t x = pg_ftol_add(G32(0x6ccd5c), X[0]);
                flare_quad(x, y, 0, (uint32_t)scale, G32(0x4af520), pkt, 1);
            }
            if (rec[0] != 0)
                continue;
            for (i = 0; i < 9; i++) {
                uint8_t *q = pkt + 0x78 * (i + 1);
                int32_t y, x;
                flare_quad_colour(q, G32(0x4af54c + 4 * i), b, U32(r, 0xc));
                y = pg_ftol_add(G32(0x6ccd60), Y[i + 1]);
                x = pg_ftol_add(G32(0x6ccd5c), X[i + 1]);
                flare_quad(x, y, 0, (uint32_t)scale, G32(0x4af524 + 4 * i), q, 0);
            }
        } else if (kind == 4) {
            int32_t fade, i;
            int32_t dir[3], up[3], out[3];
            int16_t v[4];
            uint8_t mat[0x20];
            uint32_t X[5], Y[5], dx, dy;

            G32(0x6ccc64) = U32(r, 4);
            G32(0x6ccc68) = U32(r, 8);
            pg_transform_project(rec);
            if (!(pg_cmp(rec[1], G32(0x4a33ac)) & 0x41))
                continue;
            fade = pg_ftol_fade(rec[1], G32(0x4a332c), G32(0x4a33a0)) * scale >> 8;
            if (rec[0] != 0)
                continue;
            up[0] = 0;
            up[1] = (int32_t)0xfffff000;
            up[2] = 0;
            v[0] = (int16_t)(S16(view, 0xc8) >> 6);
            v[1] = (int16_t)((S16(view, 0xce) >> 6) - 0x14);
            v[2] = (int16_t)(S16(view, 0xd4) >> 6);
            v[3] = 0;
            dir[0] = (int32_t)((uint32_t)S16(r, 4) << 16) - I32(view, 0x34);
            dir[1] = (int32_t)((uint32_t)S16(r, 6) << 16) - I32(view, 0x38);
            dir[2] = (int32_t)((uint32_t)S16(r, 8) << 16) - I32(view, 0x3c);
            GCALL(0x407e90, direction_fit)(dir);
            GCALL(0x407220, matrix_look)(dir, up, (int16_t *)mat);
            GCALL(0x426e90, matrix_mul_into)(view + 0xc4, mat);
            GCALL(0x426e10, matrix_rotate_vec)(mat, v, out);
            X[0] = pg_i2f(out[0]);
            X[4] = pg_chs(X[0]);
            Y[0] = pg_i2f(out[1]);
            Y[4] = pg_chs(Y[0]);
            dx = pg_sub_div(X[4], X[0], G32(0x4a3344));
            dy = pg_sub_div(Y[4], Y[0], G32(0x4a3344));
            X[1] = pg_add(dx, X[0]);
            Y[1] = pg_add(dy, Y[0]);
            X[3] = pg_sub(X[4], dx);
            Y[3] = pg_sub(Y[4], dy);
            X[2] = 0;
            Y[2] = 0;
            for (i = 0; i < 5; i++) {
                uint8_t *q = pkt + 0x78 * i;
                int32_t ay, ax, m, x, y, z;

                ay = pg_ftol(Y[i]);
                ay = (ay ^ (ay >> 31)) - (ay >> 31);
                ax = pg_ftol(X[i]);
                ax = (ax ^ (ax >> 31)) - (ax >> 31);
                m = ax > ay ? ax : ay;
                if (m > 0x7f)
                    m = 0x7f;
                else
                    m = ax > ay ? ax : ay;
                m = (int32_t)((uint32_t)m << 8) >> 7;
                flare_quad_colour(q, G32(0x4af4f8 + 4 * i), (uint32_t)((0xff - m) * GI32(0x4af50c + 4 * i)) >> 8,
                                  U32(r, 0xc));
                z = pg_ftol(rec[1]);
                y = pg_ftol_add(rec[3], Y[i]);
                x = pg_ftol_add(rec[2], X[i]);
                flare_quad(x, y, z, (uint32_t)fade, G32(0x4af4e4 + 4 * i), q, 2);
            }
        }
    }
    GI32(0x4beb88) = 0;
}
