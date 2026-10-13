/* Group PP: the original's still-reachable functions in 0x475030-0x4779f0
 * (the platform source file: memory, files, timer, window class, debug log,
 * bitmaps, and the car flame / track section drawing beside it).
 *
 *  - the car flames: the 5 x 8 vertex grid's heights and colours from the
 *    car's energy (0x475290), and its drawing (0x475580): the grid through
 *    the car's matrix into 32 quads (record 0x5f7400 + slot * 0x1088, 0x84
 *    a quad) on two lists by facing, linked into the ordering table;
 *  - memory: GlobalAlloc blocks with a table of 32 tracked ones (0x476ae0 /
 *    0x476b40), the 5 MB arena on one (0x476a50 / 0x476a90) allocated from
 *    the bottom (0x476b80) or the top (0x476bc0, released by 0x476c00);
 *    the family's whole state is 0x4d4acc-0x4d4b5b;
 *  - files: open for reading (retrying while shared, then by its base
 *    name), create, open for writing, close / read / write / size / seek to
 *    the end, existence by FindFirstFileA, _access, _chdir after unquoting,
 *    GetTempFileNameA, CreateDirectoryA;
 *  - the multimedia timer (0x476fb0 and its callback 0x4770f0, an address
 *    stored only as an immediate, and the empty callback 0x477120; 0x476f90,
 *    0x477090, 0x4770b0), the window class 0x477130, exit 0x476f80, the x87
 *    control word (0x477420 / 0x477440 / 0x477450);
 *  - the debug log (0x476ec0 printf, 0x4772e0 line to the debugger and the
 *    log file);
 *  - bitmaps: .BMP load (0x477530), 8-bit palette to 16-bit pixels
 *    (0x477460), row copy / palette conversion (0x477860);
 *  - the track: the visible sections list (0x477940) and one section's rows
 *    at a level of detail (0x4779f0): vertices through the GTE pair into a
 *    two-row ring, stitched to the neighbouring level's edge, quads split by
 *    facing into their 0x120-byte render records and the ordering table.
 *
 * Float steps are the original's x87 instructions (inline asm); the GTE
 * transform / projection pair *0x6ccd78 / *0x6ccd80 hands st(0) from one to
 * the other, so it is called from asm. Windows calls go through the import
 * table. Everything outside the group is called by address. */
#include "../runtime.h"
#include "physics.h"

#include <stdarg.h>

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))

#define STDCALL __attribute__((stdcall))
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

#define g_config G32(0x4b2198)
#define g_race G32(0x4b1960)

typedef int32_t(GAMEAPI *pp_fn0)(void);
typedef int32_t(GAMEAPI *pp_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *pp_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *pp_fn3)(uint32_t a, uint32_t b, uint32_t c);
#define CALL0(a) (((pp_fn0)(a))())
#define CALL1(a, x) (((pp_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((pp_fn2)(a))(A(x), A(y)))
#define CALL3(a, x, y, z) (((pp_fn3)(a))(A(x), A(y), A(z)))

/* Windows calls through the import table (stdcall). */
typedef int32_t(STDCALL *pp_api0)(void);
typedef int32_t(STDCALL *pp_api1)(uint32_t a);
typedef int32_t(STDCALL *pp_api2)(uint32_t a, uint32_t b);
typedef int32_t(STDCALL *pp_api4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(STDCALL *pp_api5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef int32_t(STDCALL *pp_api7)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                  uint32_t g);
#define IAT0(slot) ((*(pp_api0 *)(uintptr_t)(slot))())
#define IAT1(slot, a) ((*(pp_api1 *)(uintptr_t)(slot))(A(a)))
#define IAT2(slot, a, b) ((*(pp_api2 *)(uintptr_t)(slot))(A(a), A(b)))
#define IAT4(slot, a, b, c, d) ((*(pp_api4 *)(uintptr_t)(slot))(A(a), A(b), A(c), A(d)))
#define IAT5(slot, a, b, c, d, e) ((*(pp_api5 *)(uintptr_t)(slot))(A(a), A(b), A(c), A(d), A(e)))
#define IAT7(slot, a, b, c, d, e, f, g) \
    ((*(pp_api7 *)(uintptr_t)(slot))(A(a), A(b), A(c), A(d), A(e), A(f), A(g)))

#define pp_CreatePalette(p) IAT1(0x4a3050, p)
#define pp_GlobalFree(p) IAT1(0x4a3068, p)
#define pp_ReadFile(h, b, n, r, o) IAT5(0x4a3070, h, b, n, r, o)
#define pp_WriteFile(h, b, n, r, o) IAT5(0x4a3074, h, b, n, r, o)
#define pp_GetFileSize(h, hi) IAT2(0x4a3078, h, hi)
#define pp_GetTempFileNameA(p, s, u, o) IAT4(0x4a307c, p, s, u, o)
#define pp_FindClose(h) IAT1(0x4a3098, h)
#define pp_FindFirstFileA(p, d) IAT2(0x4a30a0, p, d)
#define pp_CloseHandle(h) IAT1(0x4a3100, h)
#define pp_SetFilePointer(h, d, hi, m) IAT4(0x4a3104, h, d, hi, m)
#define pp_CreateFileA(n, a, s, sa, c, f, t) IAT7(0x4a3108, n, a, s, sa, c, f, t)
#define pp_GetLastError() IAT0(0x4a310c)
#define pp_Sleep(ms) IAT1(0x4a3110, ms)
#define pp_GlobalAlloc(f, n) IAT2(0x4a3114, f, n)
#define pp_CreateDirectoryA(p, sa) IAT2(0x4a3140, p, sa)
#define pp_LoadIconA(i, n) IAT2(0x4a3270, i, n)
#define pp_RegisterClassExA(c) IAT1(0x4a3274, c)
#define pp_LoadCursorA(i, n) IAT2(0x4a3278, i, n)
#define pp_timeGetDevCaps(c, n) IAT2(0x4a32b0, c, n)
#define pp_timeEndPeriod(p) IAT1(0x4a32b8, p)
#define pp_timeSetEvent(d, r, cb, u, f) IAT5(0x4a32bc, d, r, cb, u, f)
#define pp_timeBeginPeriod(p) IAT1(0x4a32c0, p)
#define pp_timeKillEvent(id) IAT1(0x4a32c4, id)

/* Outside the group. */
#define mem_copy_at(d, s, n) CALL3(0x476f30, d, s, n)
#define mem_fill_at(d, c, n) CALL3(0x476f50, d, c, n)
#define file_open_any_at(name) ((uint32_t)CALL1(0x43b6c0, name))
#define file_read_any_at(f, b, n) CALL3(0x43b7f0, f, b, n)
#define file_close_any_at(f) CALL1(0x43b760, f)
#define debug_line_print_at(s) CALL1(0x428310, s)
#define vertex_project_codes_at(r) CALL1(0x4288c0, r)

/* This group's functions (also in build/groups/pp_protos.txt for physics.h). */
int32_t GAMEAPI dir_change_unquoted(const char *name); /* 0x475030 */
int32_t GAMEAPI path_access_ok(const char *name); /* 0x475070 */
void GAMEAPI car_flame_shape(int32_t car, uint32_t *colours); /* 0x475290 */
void GAMEAPI car_flame_draw(int32_t car, uint32_t ot_back, uint32_t ot_front); /* 0x475580 */
int32_t GAMEAPI arena_init(void); /* 0x476a50 */
int32_t GAMEAPI arena_shutdown(void); /* 0x476a90 */
uint32_t GAMEAPI global_block_alloc(uint32_t size, int32_t track); /* 0x476ae0 */
int32_t GAMEAPI global_block_free(uint32_t block); /* 0x476b40 */
uint32_t GAMEAPI arena_alloc_low(uint32_t size); /* 0x476b80 */
uint32_t GAMEAPI arena_alloc_high(uint32_t size); /* 0x476bc0 */
uint32_t GAMEAPI arena_free_high(uint32_t size); /* 0x476c00 */
uint32_t GAMEAPI os_file_open_read(const char *name); /* 0x476c30 */
uint32_t GAMEAPI os_file_create(const char *name); /* 0x476d10 */
uint32_t GAMEAPI os_file_open_rw(const char *name); /* 0x476d90 */
int32_t GAMEAPI os_handle_close(uint32_t h); /* 0x476e10 */
int32_t GAMEAPI os_handle_read(uint32_t h, void *buf, uint32_t n); /* 0x476e20 */
int32_t GAMEAPI os_handle_write(uint32_t h, const void *buf, uint32_t n); /* 0x476e50 */
uint32_t GAMEAPI os_handle_size(uint32_t h); /* 0x476e80 */
uint32_t GAMEAPI os_handle_seek_end(uint32_t h); /* 0x476ea0 */
int32_t GAMEAPI dbg_vlog_printf(const char *fmt, ...); /* 0x476ec0 */
int32_t GAMEAPI path_find_exists(const char *path); /* 0x476f00 */
void GAMEAPI exit_zero(void); /* 0x476f80 */
int32_t GAMEAPI mm_timer_start_default(uint32_t *period, uint32_t delay, uint32_t callback); /* 0x476f90 */
int32_t GAMEAPI mm_timer_start(uint32_t *period, uint32_t delay, uint32_t callback, uint32_t *id,
                               uint32_t own); /* 0x476fb0 */
int32_t GAMEAPI mm_timer_stop_default(uint32_t *period); /* 0x477090 */
int32_t GAMEAPI mm_timer_stop(uint32_t *period, uint32_t *id); /* 0x4770b0 */
void STDCALL mm_timer_tick(uint32_t id, uint32_t msg, uint32_t user, uint32_t r1, uint32_t r2); /* 0x4770f0 */
void STDCALL mm_timer_tick_none(uint32_t id, uint32_t msg, uint32_t user, uint32_t r1, uint32_t r2); /* 0x477120 */
uint32_t GAMEAPI window_class_register(uint32_t wndproc, uint32_t icon, uint32_t brush); /* 0x477130 */
int32_t GAMEAPI dbg_log_line(const char *text, const char *tag); /* 0x4772e0 */
void GAMEAPI fpu_init_single(void); /* 0x477420 */
void GAMEAPI fpu_cw_save(void); /* 0x477440 */
void GAMEAPI fpu_cw_restore(void); /* 0x477450 */
int32_t GAMEAPI image_palette_to_16(uint8_t *img); /* 0x477460 */
int32_t GAMEAPI image_bmp_load(const char *name, uint8_t *img, uint32_t flags); /* 0x477530 */
void GAMEAPI pixels_blit_convert(const uint8_t *src, uint8_t *dst, int32_t src_stride, int32_t dst_stride, int32_t bpp,
                                 int32_t width, int32_t height, const uint16_t *pal); /* 0x477860 */
int32_t GAMEAPI temp_file_name_get(const char *path, const char *prefix, char *out); /* 0x477910 */
int32_t GAMEAPI directory_create(const char *path); /* 0x477930 */
void GAMEAPI track_sections_draw(const uint32_t *list); /* 0x477940 */
void GAMEAPI track_section_draw(uint8_t *sec, uint32_t row, int32_t rows, int32_t lod, uint32_t flags); /* 0x4779f0 */

/* ---- x87 helpers: the original's instructions ---- */

/* fild word w; fmul dword k; fstp (the original's fstp / fld of the integer
 * as a float in between is exact for 16-bit values). */
static inline uint32_t x87_s16_mul(uint16_t w, uint32_t k)
{
    uint32_t r;
    __asm__ volatile("filds %1\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(w), "m"(k) : "st");
    return r;
}

/* fild dword; fstp dword: an integer to float, rounded by the control word. */
static inline uint32_t x87_from_int(uint32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fld a; fadd b; fmul k; fstp. */
static inline uint32_t x87_add_mul(uint32_t a, uint32_t b, uint32_t k)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfmuls %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(k) : "st");
    return r;
}

/* fld a; fsub b; fcomp 0.0 (0x4a3318); fnstsw: C0 (below zero or unordered). */
static inline int x87_diff_below(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfcomps 0x4a3318\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 1;
}

/* fld a; fcomp 0.0 (0x4a3318); fnstsw: the status word's high byte. */
static inline uint32_t x87_cmp_zero(uint32_t a)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps 0x4a3318\n\tfnstsw %0" : "=a"(sw) : "m"(a) : "st", "cc");
    return (sw >> 8) & 0xff;
}

/* fld a; fchs; fstp. */
static inline uint32_t x87_neg(uint32_t a)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfchs\n\tfstps %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld a; fistp dword: rounded by the control word. */
static inline int32_t x87_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fild word v; fmul 0.5 (0x4a3338); fadd / fsubr dword m; fstp m: the edge
 * nudge, m + v / 2 or m - v / 2. */
static inline void x87_nudge(uint32_t *m, uint16_t v, int sub)
{
    if (sub)
        __asm__ volatile("filds %1\n\tfmuls 0x4a3338\n\tfsubrs %0\n\tfstps %0" : "+m"(*m) : "m"(v) : "st");
    else
        __asm__ volatile("filds %1\n\tfmuls 0x4a3338\n\tfadds %0\n\tfstps %0" : "+m"(*m) : "m"(v) : "st");
}

/* The transform *0x6ccd78 (vertex record out, the GTE vector 0x6ccc64 in),
 * its argument popped, then the projection *0x6ccd80, which takes the
 * transform's st(0). */
static inline void pp_transform_project(void *out)
{
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tcall *0x6ccd80"
                     :
                     : "S"(out)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
}

/* The same with the transform's argument still pushed during the projection. */
static inline void pp_transform_project_pushed(void *out)
{
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\tcall *0x6ccd80\n\taddl $4, %%esp"
                     :
                     : "S"(out)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
}

/* 0x4278f0(a, b, c): the facing value of three projected vertex records,
 * returned in st(0), stored as a single (fstp dword). */
typedef float(GAMEAPI *pp_facing_fn)(uint32_t a, uint32_t b, uint32_t c);

static uint32_t pp_facing(uint32_t a, uint32_t b, uint32_t c)
{
    float f = ((pp_facing_fn)0x4278f0)(a, b, c);
    uint32_t r;

    memcpy(&r, &f, 4);
    return r;
}

static void pp_copy6(uint32_t dst, uint32_t src)
{
    int i;

    for (i = 0; i < 6; i++)
        U32(dst, 4 * i) = U32(src, 4 * i);
}

static uint32_t pp_strlen(const char *s)
{
    uint32_t n = 0;

    while (s[n])
        n++;
    return n;
}

/* An ordering-table depth (z >> 5) squeezed into 1..0x1ff: halved above
 * 0x100, halved again above 0x180. */
static int32_t pp_depth_clamp(int32_t d)
{
    if (d > 0x100) {
        d = d / 2 + 0x80;
        if (d > 0x180) {
            d = d / 2 + 0xc0;
            if (d > 0x1ff)
                d = 0x1ff;
        }
    } else if (d < 1) {
        d = 1;
    }
    return d;
}

/* The Park-Miller step of the seed 0x4b11d8 (x * 16807 mod 2^31, by mask). */
static inline uint32_t pp_rand_step(uint32_t s)
{
    return (s * 16807u) & 0x7fffffff;
}

/* ===== 0x475030: _chdir (0x498b3f) to `name` with its quotes removed
 * (0x474ed0 into a 0x104-byte buffer); 0 for a null name. ===== */
int32_t GAMEAPI dir_change_unquoted(const char *name)
{
    char buf[0x104];

    if (!name)
        return 0;
    CALL2(0x474ed0, buf, name);
    return CALL1(0x498b3f, buf);
}

/* ===== 0x475070: whether _access(name, 0) (0x498bc5) succeeds. ===== */
int32_t GAMEAPI path_access_ok(const char *name)
{
    return CALL2(0x498bc5, name, 0) != -1;
}

/* ===== 0x475290: the flame of car `car` (the cars' table 0x6bfb80, 0x274
 * apart), when the option +0x30 is on: strength (energy +0x654 - 0x3a0000)
 * / 0x640000 in 0..0xa0; for each of 5 columns a height 0x46 - (6i)^2 / 16
 * (pulled in at the tip by the strength), jittered by the seed 0x4b11d8
 * (columns 1..4), into the 8 rings' vertex y (0x5f72c4 + 8i + 0x28k), and
 * a colour (0x8e, g, r) from the seed and strength into colours[i + 5k]. ===== */
void GAMEAPI car_flame_shape(int32_t car, uint32_t *colours)
{
    uint32_t p = G32(0x6bfb80 + (uint32_t)car * 0x274), s, colour;
    int32_t v, strength, h, r, g, i, k;

    if (!U8(g_config, 0x30))
        return;
    v = (int32_t)((U32(p, 0x654) - 0x3a0000) << 8);
    v = (int32_t)(((int64_t)v * 0x51eb851f) >> 32) >> 19;
    v += (int32_t)((uint32_t)v >> 31);
    strength = v >= 0xa0 ? 0xa0 : v > 0 ? v : 0;
    for (i = 0; i < 5; i++) {
        h = (i * 6) * (i * 6) / 16;
        if (i == 4)
            h += 24 * ((strength * 4) >> 8);
        else if (i == 3)
            h += 4 * ((strength * 4) >> 8);
        h = 0x46 - h;
        s = G32(0x4b11d8);
        if (i) {
            s = pp_rand_step(s);
            h += (int32_t)((((s >> 15) << 2) >> 16) * (uint32_t)i);
        }
        s = pp_rand_step(s);
        G32(0x4b11d8) = s;
        r = 0xff - (int32_t)((uint32_t)(i * 3 << 6) / 5);
        r = r * (int32_t)((((s >> 15) << 5) >> 16) + (uint32_t)strength) >> 8;
        if (r >= 0xff)
            r = 0xff;
        g = r * r >> 8;
        if (g >= 0xff)
            g = 0xff;
        colour = (((uint32_t)g | 0xff8e0000) << 8) | (uint32_t)r;
        for (k = 0; k < 8; k++) {
            U16(0x5f72c4 + 8 * i + 0x28 * k, 0) = (uint16_t)h;
            colours[i + 5 * k] = colour;
        }
    }
}

/* Loads the GTE's float rotation (0x6ccd1c..) from the int16 matrix at
 * 0x6ccbe0 (scaled by 1/4096, 0x4a3310). */
static void pp_float_matrix_load(void)
{
    static const uint16_t to[9] = {0xd1c, 0xd20, 0xd24, 0xd2c, 0xd30, 0xd34, 0xd3c, 0xd40, 0xd44};
    int i;

    for (i = 0; i < 9; i++)
        G32(0x6cc000 + to[i]) = x87_s16_mul(G16(0x6ccbe0 + 2 * i), G32(0x4a3310));
}

/* Loads the GTE's float translation (0x6ccd0c..) from 0x6ccbf4... */
static void pp_float_translation_load(void)
{
    G32(0x6ccd0c) = x87_from_int(G32(0x6ccbf4));
    G32(0x6ccd10) = x87_from_int(G32(0x6ccbf8));
    G32(0x6ccd14) = x87_from_int(G32(0x6ccbfc));
}

/* ===== 0x475580: draws car `car`'s flame (option +0x30, not in the 0x6ccd84
 * bit 2 views, the car's flame slot +0x265 not -1): its shape (0x475290),
 * the car's matrix from its direction +0x68 (0x407e90) and up +0xc6/cc/d2
 * (0x407220, transposed by 0x428be0) times the view matrix 0x6ce5d0
 * (0x427fc0, column by column), at the car's centre +0x34 + half +0x14bc;
 * the 5 x 8 grid 0x5f72c0 through the GTE pair into stack vertices; each
 * of the 4 x 8 quads (record 0x5f7400 + slot * 0x1088, 0x84 each, rows
 * 0x210 apart) not wholly clipped gets its corners and colours and goes on
 * the front list (facing >= 0, sign flipped by 0x6cce94) or the back list;
 * the lists are linked in front of the ordering table entry: the car's
 * depth bucket (0x6cdd80, +0x14b4 >> 5 squeezed) with 0x6cce84, else
 * ot_front / ot_back. ===== */
void GAMEAPI car_flame_draw(int32_t car, uint32_t ot_back, uint32_t ot_front)
{
    uint32_t rec_car = 0x6bfb80 + (uint32_t)car * 0x274;
    uint32_t p = G32(rec_car), inv = G32(0x6cce94);
    uint32_t colours[40], verts[40 * 6], dir[3], up[3], src, dst, row_c, row_c1, next_c, next_c1, pk, ent;
    uint32_t front_head = 0, front_tail = 0, back_head = 0, back_tail = 0, f, below;
    uint16_t mat[9], m2[10];
    int32_t pos[3], slot, c, k, n, depth;
    uint32_t rec;

    if (!U8(g_config, 0x30))
        return;
    if (G8(0x6ccd84) & 4)
        return;
    slot = (int8_t)U8(rec_car, 0x265);
    if (slot == -1)
        return;
    rec = 0x5f7400 + (uint32_t)slot * 0x1088;
    car_flame_shape(car, colours);
    up[0] = (uint32_t)((int32_t)S16(p, 0xc6) << 4);
    up[1] = (uint32_t)((int32_t)S16(p, 0xcc) << 4);
    up[2] = (uint32_t)((int32_t)S16(p, 0xd2) << 4);
    dir[0] = U32(p, 0x68);
    dir[1] = U32(p, 0x6c);
    dir[2] = U32(p, 0x70);
    CALL1(0x407e90, dir);
    if (!CALL3(0x407220, dir, up, mat))
        return;
    CALL2(0x428be0, mat, mat);
    for (n = 0; n < 5; n++)
        G32(0x6ccbe0 + 4 * n) = G32(0x6ce5d0 + 4 * n);
    pp_float_matrix_load();
    /* The car matrix's columns through the view matrix (0x427fc0). */
    for (n = 0; n < 3; n++) {
        G16(0x6ccc88) = mat[n];
        G16(0x6ccc8c) = mat[3 + n];
        G16(0x6ccc90) = mat[6 + n];
        CALL0(0x427fc0);
        m2[n] = G16(0x6ccc88);
        m2[3 + n] = G16(0x6ccc8c);
        m2[6 + n] = G16(0x6ccc90);
    }
    m2[9] = 0; /* an unwritten stack word in the original (its 0x6ccbf2 copy) */
    for (n = 0; n < 3; n++)
        G32(0x6ccbf4 + 4 * n) = G32(0x6ce5e4 + 4 * n);
    pp_float_translation_load();
    pos[0] = (I32(p, 0x14bc) >> 1) + I32(p, 0x34);
    pos[1] = (I32(p, 0x14c0) >> 1) + I32(p, 0x38);
    pos[2] = (I32(p, 0x14c4) >> 1) + I32(p, 0x3c);
    G16(0x6ccc64) = (uint16_t)((uint32_t)pos[0] >> 16);
    G16(0x6ccc68) = (uint16_t)((uint32_t)pos[2] >> 16);
    G16(0x6ccc66) = (uint16_t)((uint32_t)pos[1] >> 16);
    CALL0(0x426620);
    for (n = 0; n < 3; n++)
        G32(0x6ccbf4 + 4 * n) = G32(0x6cccc8 + 4 * n);
    pp_float_translation_load();
    for (n = 0; n < 5; n++)
        G32(0x6ccbe0 + 4 * n) = (uint32_t)m2[2 * n] | (uint32_t)m2[2 * n + 1] << 16;
    pp_float_matrix_load();
    /* The grid: column c, ring k at 0x5f72c0 + 8c + 0x28k -> vertex k * 5 + c. */
    for (c = 0; c < 5; c++) {
        src = 0x5f72c4 + 8 * (uint32_t)c;
        dst = A(verts) + 0x18 * (uint32_t)c;
        for (k = 0; k < 8; k++) {
            G32(0x6ccc64) = U32(src, -4);
            G32(0x6ccc68) = U32(src, 0);
            pp_transform_project(PTR(dst));
            src += 0x28;
            dst += 0x78;
        }
    }
    for (c = 0; c < 4; c++) {
        for (k = 0; k < 8; k++) {
            pk = rec + 0x84 * (uint32_t)c + 0x210 * (uint32_t)k;
            n = c + 5 * ((k + 1) & 7);
            row_c = A(verts) + 0x18 * (uint32_t)(c + 5 * k);
            row_c1 = row_c + 0x18;
            next_c = A(verts) + 0x18 * (uint32_t)n;
            next_c1 = next_c + 0x18;
            f = pp_facing(next_c, row_c1, next_c1);
            if (inv)
                f = x87_neg(f);
            if (U32(row_c, 0) & (U32(row_c1, 0) & U32(next_c1, 0) & U32(next_c, 0)))
                continue;
            below = x87_cmp_zero(f) & 1;
            pp_copy6(pk + 8, row_c);
            pp_copy6(pk + 0x28, next_c);
            pp_copy6(pk + 0x48, row_c1);
            pp_copy6(pk + 0x68, next_c1);
            U32(pk, 4) = colours[c + 5 * k];
            U32(pk, 0x24) = colours[n];
            U32(pk, 0x44) = colours[c + 1 + 5 * k];
            U32(pk, 0x64) = colours[n + 1];
            if (!below) {
                if (!front_tail)
                    front_head = pk;
                else
                    U32(front_tail, 0) = pk;
                front_tail = pk;
            } else {
                if (!back_tail)
                    back_head = pk;
                else
                    U32(back_tail, 0) = pk;
                back_tail = pk;
            }
        }
    }
    if (G32(0x6cce84)) {
        depth = pp_depth_clamp(I32(p, 0x14b4) >> 5);
        if (front_tail) {
            ent = G32(0x6cdd80 + 4 * (uint32_t)depth) & 0x7fffffff;
            U32(front_tail, 0) = U32(ent, 0);
            U32(ent, 0) = front_head;
        }
        if (back_tail) {
            ent = G32(0x6cdd80 + 4 * (uint32_t)depth) & 0x7fffffff;
            U32(back_tail, 0) = U32(ent, 0);
            U32(ent, 0) = back_head;
        }
    } else {
        if (front_tail) {
            ent = ot_front & 0x7fffffff;
            U32(front_tail, 0) = U32(ent, 0);
            U32(ent, 0) = front_head;
        }
        if (back_tail) {
            ent = ot_back & 0x7fffffff;
            U32(back_tail, 0) = U32(ent, 0);
            U32(ent, 0) = back_head;
        }
    }
}

/* ===== 0x476a50: the arena: the block table (32 entries at 0x4d4acc)
 * cleared, a 5 MB untracked block (0x476ae0) as base 0x4d4b50, bottom
 * 0x4d4b58 and top 0x4d4b54 = base + 5 MB; whether it was allocated. ===== */
int32_t GAMEAPI arena_init(void)
{
    uint32_t p;
    int i;

    for (i = 0; i < 32; i++)
        G32(0x4d4acc + 4 * i) = 0;
    p = global_block_alloc(0x500000, 0);
    G32(0x4d4b58) = p;
    G32(0x4d4b50) = p;
    G32(0x4d4b54) = p + 0x500000;
    return p != 0;
}

/* ===== 0x476a90: frees the arena and every tracked block; 1. ===== */
int32_t GAMEAPI arena_shutdown(void)
{
    int i;

    if (G32(0x4d4b50))
        global_block_free(G32(0x4d4b50));
    G32(0x4d4b50) = 0;
    for (i = 0; i < 32; i++)
        if (G32(0x4d4acc + 4 * i))
            global_block_free(G32(0x4d4acc + 4 * i));
    return 1;
}

/* ===== 0x476ae0: GlobalAlloc(GPTR, size), entered in the first free
 * table slot when `track` (none free: not entered); the total 0x4d4b4c
 * grows by size; the block or 0. ===== */
uint32_t GAMEAPI global_block_alloc(uint32_t size, int32_t track)
{
    uint32_t p = (uint32_t)pp_GlobalAlloc(0x40, size);
    int i;

    if (!p)
        return 0;
    if (track) {
        for (i = 0; i < 32; i++)
            if (!G32(0x4d4acc + 4 * i)) {
                G32(0x4d4acc + 4 * i) = p;
                break;
            }
    }
    G32(0x4d4b4c) += size;
    return p;
}

/* ===== 0x476b40: GlobalFree(block) and its table slot cleared; 0. ===== */
int32_t GAMEAPI global_block_free(uint32_t block)
{
    int i;

    if (!block)
        return 0;
    pp_GlobalFree(block);
    for (i = 0; i < 32; i++)
        if (G32(0x4d4acc + 4 * i) == block) {
            G32(0x4d4acc + 4 * i) = 0;
            break;
        }
    return 0;
}

/* ===== 0x476b80: size rounded to 16 from the arena's bottom, zeroed
 * (0x476f50); 0 when it would pass the top. ===== */
uint32_t GAMEAPI arena_alloc_low(uint32_t size)
{
    uint32_t n = (size + 15) & ~15u, base = G32(0x4d4b58);

    if (base + n > G32(0x4d4b54))
        return 0;
    mem_fill_at(base, 0, n);
    G32(0x4d4b58) += n;
    return G32(0x4d4b58) - n;
}

/* ===== 0x476bc0: size rounded to 16 from the arena's top, zeroed; 0 when
 * it would pass the bottom. ===== */
uint32_t GAMEAPI arena_alloc_high(uint32_t size)
{
    uint32_t n = (size + 15) & ~15u, top = G32(0x4d4b54) - n;

    if (top < G32(0x4d4b58))
        return 0;
    G32(0x4d4b54) = top;
    mem_fill_at(top, 0, n);
    return G32(0x4d4b54);
}

/* ===== 0x476c00: gives size (rounded to 16) back to the top, at most up
 * to the arena's end; returns the end. ===== */
uint32_t GAMEAPI arena_free_high(uint32_t size)
{
    uint32_t top = G32(0x4d4b54) + ((size + 15) & ~15u), end = G32(0x4d4b50) + 0x500000;

    G32(0x4d4b54) = top;
    if (top > end)
        G32(0x4d4b54) = end;
    return end;
}

/* CreateFileA(name, access, share, 0, disposition, attributes, 0), retried
 * every 500 ms while the error is a sharing violation; -1 if it failed. */
static uint32_t pp_create_retry(const char *name, uint32_t access, uint32_t share, uint32_t disposition,
                                uint32_t attributes)
{
    uint32_t h = (uint32_t)pp_CreateFileA(name, access, share, 0, disposition, attributes, 0);

    while (h == 0xffffffff) {
        if (pp_GetLastError() != 0x21)
            break;
        pp_Sleep(500);
        h = (uint32_t)pp_CreateFileA(name, access, share, 0, disposition, attributes, 0);
    }
    return h;
}

/* ===== 0x476c30: opens `name` for reading (shared read, sequential scan),
 * retrying while shared; failing that, by its name after the last '\'; the
 * handle or 0. ===== */
uint32_t GAMEAPI os_file_open_read(const char *name)
{
    uint32_t h = pp_create_retry(name, 0x80000000, 1, 3, 0x8000080);
    int32_t n;

    if (h != 0xffffffff)
        return h;
    n = (int32_t)pp_strlen(name) - 1;
    while (n >= 0 && name[n] != '\\')
        n--;
    h = pp_create_retry(name + n + 1, 0x80000000, 1, 3, 0x8000080);
    return h != 0xffffffff ? h : 0;
}

/* ===== 0x476d10: creates `name` (read / write, CREATE_ALWAYS), retrying
 * while shared; the handle or 0. ===== */
uint32_t GAMEAPI os_file_create(const char *name)
{
    uint32_t h = pp_create_retry(name, 0xc0000000, 0, 2, 0x80);

    return h != 0xffffffff ? h : 0;
}

/* ===== 0x476d90: opens `name` for reading and writing (OPEN_EXISTING),
 * retrying while shared; the handle or 0. ===== */
uint32_t GAMEAPI os_file_open_rw(const char *name)
{
    uint32_t h = pp_create_retry(name, 0xc0000000, 0, 3, 0x80);

    return h != 0xffffffff ? h : 0;
}

/* ===== 0x476e10: CloseHandle unless 0. ===== */
int32_t GAMEAPI os_handle_close(uint32_t h)
{
    if (!h)
        return 0;
    return pp_CloseHandle(h);
}

/* ===== 0x476e20: ReadFile; whether it succeeded. ===== */
int32_t GAMEAPI os_handle_read(uint32_t h, void *buf, uint32_t n)
{
    return pp_ReadFile(h, buf, n, &n, 0) != 0;
}

/* ===== 0x476e50: WriteFile; whether all n bytes went. ===== */
int32_t GAMEAPI os_handle_write(uint32_t h, const void *buf, uint32_t n)
{
    uint32_t written;

    if (!pp_WriteFile(h, buf, n, &written, 0))
        return 0;
    return written == n;
}

/* ===== 0x476e80: GetFileSize, 0 for an error. ===== */
uint32_t GAMEAPI os_handle_size(uint32_t h)
{
    uint32_t n = (uint32_t)pp_GetFileSize(h, 0);

    return n == 0xffffffff ? 0 : n;
}

/* ===== 0x476ea0: SetFilePointer(h, 0, 0, FILE_END). ===== */
uint32_t GAMEAPI os_handle_seek_end(uint32_t h)
{
    return (uint32_t)pp_SetFilePointer(h, 0, 0, 2);
}

/* ===== 0x476ec0: printf to the debug log: vsprintf (0x498c0b) into 0x104
 * bytes, then the line with the tag 0x4b23f4 (0x4772e0); 1. ===== */
int32_t GAMEAPI dbg_vlog_printf(const char *fmt, ...)
{
    char buf[0x104];
    va_list ap;

    va_start(ap, fmt);
    CALL3(0x498c0b, buf, fmt, *(uint32_t *)&ap);
    va_end(ap);
    return dbg_log_line(buf, (const char *)0x4b23f4);
}

/* ===== 0x476f00: whether FindFirstFileA finds `path` (FindClose always). ===== */
int32_t GAMEAPI path_find_exists(const char *path)
{
    uint8_t data[0x140];
    uint32_t h = (uint32_t)pp_FindFirstFileA(path, data);

    pp_FindClose(h);
    return h != 0xffffffff;
}

/* ===== 0x476f80: exit(0) (0x498c89). ===== */
void GAMEAPI exit_zero(void)
{
    CALL1(0x498c89, 0);
}

/* ===== 0x476f90: the periodic timer on the shared id 0x4d4b5c, through
 * the counting callback (0x476fb0 with own = 1). ===== */
int32_t GAMEAPI mm_timer_start_default(uint32_t *period, uint32_t delay, uint32_t callback)
{
    return mm_timer_start(period, delay, callback, (uint32_t *)0x4d4b5c, 1);
}

/* ===== 0x476fb0: a periodic multimedia timer: the callback stored in
 * 0x4d48c8 (none: the empty one, 0x477120 in the original); the period is
 * max(5, minimum) unless that reaches the maximum (then the maximum), set by
 * timeBeginPeriod into *period (0 if refused); timeSetEvent(delay, period,
 * TIME_PERIODIC) calls `callback` directly unless `own` or none, else the
 * counting callback (0x4770f0 in the original); its id into *id (0: period
 * ended). 1 when running, else 0. ===== */
int32_t GAMEAPI mm_timer_start(uint32_t *period, uint32_t delay, uint32_t callback, uint32_t *id, uint32_t own)
{
    uint32_t caps[2], p, r;

    G32(0x4d48c8) = callback ? callback : A(mm_timer_tick_none);
    if (pp_timeGetDevCaps(caps, 8))
        return 0;
    p = caps[0] > 5 ? caps[0] : 5;
    if (p >= caps[1])
        p = caps[1];
    *period = p;
    if (pp_timeBeginPeriod(p)) {
        *period = 0;
        return 0;
    }
    if (!own && callback)
        r = (uint32_t)pp_timeSetEvent(delay, *period, callback, 0, 1);
    else
        r = (uint32_t)pp_timeSetEvent(delay, *period, A(mm_timer_tick), 0, 1);
    *id = r;
    if (!r) {
        pp_timeEndPeriod(*period);
        return 0;
    }
    return 1;
}

/* ===== 0x477090: stops the shared timer (0x4770b0 with 0x4d4b5c). ===== */
int32_t GAMEAPI mm_timer_stop_default(uint32_t *period)
{
    return mm_timer_stop(period, (uint32_t *)0x4d4b5c);
}

/* ===== 0x4770b0: timeKillEvent(*id) and timeEndPeriod(*period) for those
 * set, each cleared. ===== */
int32_t GAMEAPI mm_timer_stop(uint32_t *period, uint32_t *id)
{
    int32_t r = 0;

    if (*id) {
        pp_timeKillEvent(*id);
        *id = 0;
    }
    if (*period) {
        r = pp_timeEndPeriod(*period);
        *period = 0;
    }
    return r;
}

/* ===== 0x4770f0: the timer callback: counts ticks in 0x5f72a8 and passes
 * the call on to the callback 0x4d48c8 (stdcall). ===== */
void STDCALL mm_timer_tick(uint32_t id, uint32_t msg, uint32_t user, uint32_t r1, uint32_t r2)
{
    G32(0x5f72a8)++;
    (*(pp_api5 *)(uintptr_t)0x4d48c8)(id, msg, user, r1, r2);
}

/* ===== 0x477120: the empty timer callback. ===== */
void STDCALL mm_timer_tick_none(uint32_t id, uint32_t msg, uint32_t user, uint32_t r1, uint32_t r2)
{
    (void)id;
    (void)msg;
    (void)user;
    (void)r1;
    (void)r2;
}

/* ===== 0x477130: registers the window class 0x4acc18 (CS_HREDRAW |
 * CS_VREDRAW, the instance 0x6c7ffc, icon `icon`, the arrow cursor, the
 * background brush + 1 for a colour index up to 0x18); the atom. ===== */
uint32_t GAMEAPI window_class_register(uint32_t wndproc, uint32_t icon, uint32_t brush)
{
    uint32_t wc[12], inst = G32(0x6c7ffc);

    if (brush <= 0x18)
        brush++;
    wc[2] = wndproc;
    wc[0] = 0x30;
    wc[1] = 3;
    wc[3] = 0;
    wc[4] = 0;
    wc[5] = inst;
    wc[6] = (uint32_t)pp_LoadIconA(inst, icon & 0xffff);
    wc[7] = (uint32_t)pp_LoadCursorA(0, 0x7f00);
    wc[8] = brush;
    wc[9] = 0;
    wc[10] = 0x4acc18;
    wc[11] = 0;
    return (uint32_t)pp_RegisterClassExA(wc);
}

/* ===== 0x4772e0: a debug log line: tag (default 0x4acc24 when null or
 * empty) + 0x4b2410 + text to the debugger (0x428310), and with logging on
 * (0x4d4b68) appended to the file 0x4b2400 (tag, the 3-byte separator,
 * text, CR LF 0x4b23fc); 1. ===== */
int32_t GAMEAPI dbg_log_line(const char *text, const char *tag)
{
    char buf[0x200];
    uint32_t n = 0, i, h;

    if (!tag || !tag[0])
        tag = (const char *)0x4acc24;
    for (i = 0; tag[i]; i++)
        buf[n++] = tag[i];
    for (i = 0; ((const char *)0x4b2410)[i]; i++)
        buf[n++] = ((const char *)0x4b2410)[i];
    for (i = 0; text[i]; i++)
        buf[n++] = text[i];
    buf[n] = 0;
    debug_line_print_at(buf);
    if (G32(0x4d4b68)) {
        h = os_file_open_rw((const char *)0x4b2400);
        if (h) {
            os_handle_seek_end(h);
            os_handle_write(h, tag, pp_strlen(tag));
            os_handle_write(h, (const void *)0x4b2410, 3);
            os_handle_write(h, text, pp_strlen(text));
            os_handle_write(h, (const void *)0x4b23fc, 2);
            file_close_any_at(h);
        }
    }
    return 1;
}

/* ===== 0x477420: fninit, then single precision (control word bits 8-9
 * cleared). ===== */
void GAMEAPI fpu_init_single(void)
{
    uint16_t cw;

    __asm__ volatile("finit\n\tfnstcw %0" : "=m"(cw) : : X87);
    cw &= 0xfcff;
    __asm__ volatile("fldcw %0" : : "m"(cw));
}

/* ===== 0x477440: saves the x87 control word in 0x4d48c4. ===== */
void GAMEAPI fpu_cw_save(void)
{
    __asm__ volatile("fnstcw 0x4d48c4" : : : "memory");
}

/* ===== 0x477450: loads the x87 control word from 0x4d48c4. ===== */
void GAMEAPI fpu_cw_restore(void)
{
    __asm__ volatile("fldcw 0x4d48c4" : : : "memory");
}

/* ===== 0x477460: an 8-bit image's palette (+0x10, BGRx) as 16-bit pixels
 * into +0x18 (allocated when 0: from the arena with flag +0x20 bit 0, else
 * a block): each channel shifted right then left by the screen format's
 * shifts (green 0x4d5f58 / 59, red 0x4d5f50 / 51, blue 0x4d5f60 / 61).
 * 1 (also for other depths), 0 without memory. ===== */
int32_t GAMEAPI image_palette_to_16(uint8_t *img)
{
    uint32_t out, src, g, r, b;
    int32_t i;

    if (U8(img, 0xa) != 8)
        return 1;
    if (!U32(img, 0x18)) {
        if (U8(img, 0x20) & 1)
            U32(img, 0x18) = arena_alloc_low((uint32_t)((int32_t)S16(img, 8) * 2));
        else
            U32(img, 0x18) = global_block_alloc((uint32_t)((int32_t)S16(img, 8) * 2), 0);
    }
    out = U32(img, 0x18);
    if (!out)
        return 0;
    for (i = 0; i < S16(img, 8); i++) {
        src = U32(img, 0x10) + 4 * (uint32_t)i;
        g = (uint32_t)(uint8_t)(U8(src, 1) >> (G8(0x4d5f58) & 31)) << (G8(0x4d5f59) & 31);
        r = (uint32_t)(uint8_t)(U8(src, 2) >> (G8(0x4d5f50) & 31)) << (G8(0x4d5f51) & 31);
        b = (uint32_t)(uint8_t)(U8(src, 0) >> (G8(0x4d5f60) & 31)) << (G8(0x4d5f61) & 31);
        U16(out, 2 * i) = (uint16_t)(g | r | b);
    }
    return 1;
}

/* fild dword w; fstp; fld; fmul f; fadd 3.0 (0x4a345c); _ftol (0x497a80):
 * the bitmap's row bytes before rounding down to 4. */
static inline int32_t pp_row_bytes(uint32_t w, uint32_t f)
{
    int32_t r;
    uint32_t t;

    __asm__ volatile("fildl %2\n\tfstps %1\n\tflds %1\n\tfmuls %3\n\tfadds 0x4a345c\n\t"
                     "movl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r), "=m"(t)
                     : "m"(w), "m"(f)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
}

/* ===== 0x477530: loads the .BMP `name` (0x43b6c0 / 0x43b7f0 / 0x43b760)
 * into the image record: +2 width, +4 row bytes (width * bits / 8 + 3,
 * down to 4), +6 height, +8 palette entries (biClrUsed, else 1 << bits),
 * +0xa bits, +0xc block (the 40-byte header, palette, pixels; from the
 * arena with flag 1, else a block), +0x10 palette (bits <= 8), +0x14
 * pixels (rows flipped to top-down), +0x18 = 0, +0x1c a GDI palette
 * (CreatePalette, up to 256 entries), +0x20 flags. 1, or 0 on failure (a
 * failed pixel read frees the pixel pointer, not the block: sic). ===== */
int32_t GAMEAPI image_bmp_load(const char *name, uint8_t *img, uint32_t flags)
{
    uint8_t fh[0xe], ih[0x28];
    uint32_t f, count, bits, scale, pal_bytes = 0, pixel_bytes, block, at, pal;
    int32_t stride, height, i;
    float tmp;
    uint16_t sw;

    U32(img, 0x10) = 0;
    U32(img, 0x14) = 0;
    U8(img, 0xb) = 0;
    U32(img, 0x1c) = 0;
    f = file_open_any_at(name);
    if (!f)
        return 0;
    if (!file_read_any_at(f, fh, 0xe)) {
        file_close_any_at(f);
        return 0;
    }
    if (!file_read_any_at(f, ih, 0x28)) {
        file_close_any_at(f);
        return 0;
    }
    count = U32(ih, 0x20);
    if (!count)
        count = 1u << (U8(ih, 0xe) & 31);
    U16(img, 8) = (uint16_t)count;
    bits = U16(ih, 0xe);
    /* bits / 8.0, compared with 1.0 (0x4a331c): a palette when <= or unordered. */
    __asm__ volatile("fildl %2\n\tfstps %1\n\tflds %1\n\tfdivs 0x4a3420\n\tfstps %1\n\t"
                     "flds %1\n\tfcomps 0x4a331c\n\tfnstsw %0"
                     : "=a"(sw), "=m"(tmp)
                     : "m"(bits)
                     : "st", "cc");
    memcpy(&scale, &tmp, 4);
    if ((sw >> 8) & 0x41)
        pal_bytes = (uint32_t)((int32_t)S16(img, 8) << 2);
    height = I32(ih, 8);
    U32(img, 0x20) = flags;
    U16(img, 2) = U16(ih, 4);
    U16(img, 6) = (uint16_t)height;
    U16(img, 4) = (uint16_t)(pp_row_bytes(U32(ih, 4), scale) & ~3);
    stride = S16(img, 4);
    pixel_bytes = (uint32_t)(stride * height);
    U8(img, 0xa) = U8(ih, 0xe);
    if (flags & 1)
        block = arena_alloc_low(pixel_bytes + pal_bytes + 0x28);
    else
        block = global_block_alloc(pixel_bytes + pal_bytes + 0x28, 0);
    if (!block) {
        file_close_any_at(f);
        return 0;
    }
    U32(img, 0xc) = block;
    memcpy(PTR(block), ih, 0x28);
    at = block + 0x28;
    if (pal_bytes) {
        U32(img, 0x10) = at;
        if (!file_read_any_at(f, at, pal_bytes)) {
            file_close_any_at(f);
            return 0;
        }
        at += pal_bytes;
    }
    U32(img, 0x14) = at;
    if (!file_read_any_at(f, at, pixel_bytes)) {
        if (!(flags & 1))
            global_block_free(at); /* sic: the pixels, not the block */
        file_close_any_at(f);
        return 0;
    }
    file_close_any_at(f);
    if (S16(img, 8) <= 0x100) {
        pal = global_block_alloc((uint32_t)(4 * (int32_t)S16(img, 8) + 8), 0);
        if (pal) {
            U16(pal, 0) = 0x300;
            U16(pal, 2) = U16(img, 8);
            for (i = 0; i < S16(img, 8); i++) {
                U8(pal, 4 + 4 * i) = U8(U32(img, 0x10), 4 * i + 2);
                U8(pal, 5 + 4 * i) = U8(U32(img, 0x10), 4 * i + 1);
                U8(pal, 6 + 4 * i) = U8(U32(img, 0x10), 4 * i);
                U8(pal, 7 + 4 * i) = 0;
            }
            U32(img, 0x1c) = (uint32_t)pp_CreatePalette(pal);
            global_block_free(pal);
        }
    }
    if (S16(img, 6) > 0) {
        /* Top-down: rows swapped from both ends, a dword at a time. */
        int32_t rows = S16(img, 6);
        uint32_t row_bytes = (uint32_t)(int32_t)S16(img, 4), top = at, bottom = at + (uint32_t)(rows - 1) * row_bytes;
        int32_t left = rows;

        do {
            uint32_t n = row_bytes >> 2, t;

            do {
                t = U32(bottom, 0);
                U32(bottom, 0) = U32(top, 0);
                U32(top, 0) = t;
                bottom += 4;
                top += 4;
            } while (--n);
            bottom -= 2 * row_bytes;
            left -= 2;
        } while (left > 1);
    }
    U32(img, 0x18) = 0;
    return 1;
}

/* ===== 0x477860: copies `height` rows of `width` pixels: 8 bits a pixel
 * row by row (0x476f30), 16 bits through the 256-entry palette `pal`;
 * other depths nothing. ===== */
void GAMEAPI pixels_blit_convert(const uint8_t *src, uint8_t *dst, int32_t src_stride, int32_t dst_stride, int32_t bpp,
                                 int32_t width, int32_t height, const uint16_t *pal)
{
    int32_t x;

    if (bpp == 8) {
        for (; height > 0; height--) {
            mem_copy_at(dst, src, width);
            src += src_stride;
            dst += dst_stride;
        }
    } else if (bpp == 16) {
        for (; height > 0; height--) {
            for (x = 0; x < width; x++)
                ((uint16_t *)dst)[x] = pal[src[x]];
            src += src_stride;
            dst += dst_stride;
        }
    }
}

/* ===== 0x477910: GetTempFileNameA(path, prefix, 0, out). ===== */
int32_t GAMEAPI temp_file_name_get(const char *path, const char *prefix, char *out)
{
    return pp_GetTempFileNameA(path, prefix, 0, out);
}

/* ===== 0x477930: CreateDirectoryA(path, 0). ===== */
int32_t GAMEAPI directory_create(const char *path)
{
    return pp_CreateDirectoryA(path, 0);
}

/* ===== 0x477940: the visible track pieces: list = {count, then 8-byte
 * entries (section index into 0x6bfac0's table, first row, rows, level)};
 * each drawn (0x4779f0) with flag 1 when the previous entry is the same
 * section at a coarser level, 2 when the next is. ===== */
void GAMEAPI track_sections_draw(const uint32_t *list)
{
    uint32_t count = list[0], i, flags;
    const uint16_t *e = (const uint16_t *)(list + 1);

    for (i = 0; i < count; i++, e += 4) {
        uint32_t sec = U32(G32(0x6bfac0), 4 * (uint32_t)e[0]);

        flags = 0;
        if (i > 0 && e[-1] > e[3] && e[-4] == e[0])
            flags = 1;
        if (i < count - 1 && e[7] > e[3] && e[4] == e[0])
            flags |= 2;
        track_section_draw(PTR(sec), e[1], e[2], e[3], flags);
    }
}

/* The edge nudge of a stitched vertex r between p and n (screen x / y at
 * +8 / +0xc): one unit (a half step) along the edge's direction, signs from
 * n - p (x: +1 unless below zero; y the same, carried through the packed
 * word), negated by 0x6cce94. */
static void pp_edge_nudge(uint32_t r, uint32_t p_x, uint32_t p_y, uint32_t n_x, uint32_t n_y, uint32_t inv)
{
    uint32_t c = x87_diff_below(n_x, p_x) ? 0x10000 : 0xffff0000;

    c = x87_diff_below(n_y, p_y) ? c - 1 : c + 1;
    x87_nudge(&U32(r, 8), (uint16_t)c, inv != 0);
    x87_nudge(&U32(r, 0xc), (uint16_t)(c >> 16), inv != 0);
}

/* ===== 0x4779f0: draws `rows` rows of a track section from `row` at level
 * of detail `lod`: vertices (8 bytes; GTE vector 0x6ccc64 / 0x6ccc68)
 * stepped by the level's deltas (lod 1: 8 each, +8 quads per row; else the
 * table +0x20 + 4 lod) into a two-row ring of 24-byte screen vertices on
 * the stack, through the GTE pair. With flag 1 (2), the first (last) row's
 * vertices between the coarser neighbour's points (skip table +0x28 / +0x24
 * + 4 lod) are moved to the middle of their neighbours and nudged. Each
 * quad (list +0x50 / +0x4c + 4 lod: record index, render records
 * *(*0x6c1e30 + 4) (+0x84 for the second view), 0x120 apiece) not wholly
 * clipped is split along its diagonal (bit 2 of the vertex's flags, lod 1;
 * else always), the halves facing away dropped (0x4278f0, near ones kept),
 * its corners copied in, type 0x8c, and linked into the ordering table at
 * its farthest depth >> 5 (0x6cd510, squeezed into 1..0x1ff) or by its
 * texture page (+0x42, 0x6cd518, with 0x6cce84); bit 0 of the flags (lod 1)
 * or of the level's nibble adds the overlay record after it (colours copied
 * and 0x50 nearer when its +0x20 bit 31 is set; track 12 puts it on page
 * 0x3f). ===== */
void GAMEAPI track_section_draw(uint8_t *sec, uint32_t row, int32_t rows, int32_t lod, uint32_t flags)
{
    /* The original's frame from ebp - 0x8c8 (ring, deltas, the stitch's two
     * vertices at ebp - 0xc4 / ebp - 0x6c), so an over-long row overruns
     * into the same places; 0x18 bytes spare below. */
    uint8_t frame[0x18 + 0x8c8];
#define FR(off) A(frame + 0x18 + 0x8c8 - (off))
    uint32_t ring = FR(0x8c8), ring_end, vptr = ring, deltas = FR(0x118), tmp = FR(0x6c), tmp2 = FR(0xc4);
    uint32_t recbase = U32(G32(0x6c1e30), 4), inv = G32(0x6cce94);
    int is12 = U8(g_race, 0x579) == 12;
    int32_t quads = I32(sec, 8), vpr = quads + 1, extra, l74, l50, l4c, col, depth, z;
    uint32_t vp0 = U32(sec, 0x20) + 8 * (uint32_t)(vpr * (int32_t)row), vp = vp0, qlist, skip, skip0;
    uint32_t nib_ptr = 0, nib = 0, stitched = 0, edge = 1, dp, cnt, qflags, cur, pk, pk2, ent, bucket, tex;
    uint32_t p0, p1, p2, p3, a, b, c, d, f1, f2, s1, s2;
    int32_t i;

    if (G32(0x6ce60c) != 0x6cd4a0)
        recbase += 0x84;
    if (lod == 1) {
        extra = 0;
        if (vpr > 0)
            for (i = 0; i < vpr; i++)
                U16(deltas, 2 * i) = 8;
        qlist = U32(sec, 0x50) + 8 * (uint32_t)(quads * (int32_t)row);
        skip = U32(sec, 0x28);
    } else {
        uint32_t e8 = (uint32_t)vpr << 3, tbl, n, idx, nibs;

        extra = (int32_t)((e8 << ((uint32_t)(lod - 1) & 31)) - e8);
        tbl = U32(sec, 0x20 + 4 * lod);
        vpr = (int32_t)(U32(tbl, 0) & 0xffff);
        quads = vpr - 1;
        n = (uint32_t)((quads >> 1) + 1);
        for (i = 0; i < (int32_t)n; i++)
            U32(deltas, 4 * i) = U32(tbl, 4 + 4 * i);
        idx = (uint32_t)(((int32_t)row >> ((uint32_t)(lod - 1) & 31)) * quads);
        qlist = U32(sec, 0x4c + 4 * lod) + 8 * idx;
        skip = U32(sec, 0x24 + 4 * lod);
        nibs = U32(sec, 0x34 + 4 * lod);
        nib = (uint32_t)(I32(nibs, 4 * (idx >> 3)) >> ((idx & 7) * 4));
        nib_ptr = nibs + 4 * (idx >> 3) + 4;
    }
    l74 = vpr;
    l50 = vpr + 1;
    l4c = -quads;
    ring_end = ring + 0x30 * (uint32_t)(int32_t)(int16_t)vpr;
    /* The first row (the previous row's far edge). */
    if (vpr != 0) {
        dp = deltas + 2 * (uint32_t)vpr;
        cnt = (uint32_t)vpr;
        do {
            G32(0x6ccc64) = U32(vp, 0);
            G32(0x6ccc68) = U32(vp, 4);
            pp_transform_project(PTR(vptr));
            vptr += 0x18;
            dp -= 2;
            vp += U16(dp, 0);
        } while (--cnt);
    }
    skip0 = skip;
    if (flags & 1) {
        uint32_t next = vp0;

        vp = vp0;
        vptr = ring;
        skip = skip0 + 2 * U16(skip0, 0) + 2;
        edge = next;
        if (vpr > 0) {
            dp = deltas + 2 * (uint32_t)vpr;
            cnt = (uint32_t)vpr;
            do {
                int mid = vp < next;

                if (!mid) {
                    next += U16(skip, 0);
                    skip -= 2;
                }
                dp -= 2;
                vp += U16(dp, 0);
                vptr += 0x18;
                if (mid) {
                    uint32_t r = vptr - 0x18, p = vptr - 0x30, n = vptr;

                    U32(r, 0x10) = x87_add_mul(U32(p, 0x10), U32(n, 0x10), G32(0x4a3338));
                    U32(r, 0x14) = x87_add_mul(U32(p, 0x14), U32(n, 0x14), G32(0x4a3338));
                    U32(r, 4) = x87_add_mul(U32(p, 4), U32(n, 4), G32(0x4a3338));
                    vertex_project_codes_at(r);
                    pp_edge_nudge(r, U32(p, 8), U32(p, 0xc), U32(n, 8), U32(n, 0xc), inv);
                }
            } while (--cnt);
            edge = next;
        }
    }
    if (rows <= 0)
        return;
    for (;;) {
        vp += (uint32_t)extra;
        if (rows == 1)
            edge = vp;
        col = vpr;
        if (col != 0) {
            dp = deltas + 2 * (uint32_t)vpr;
            do {
                cur = vp;
                G32(0x6ccc64) = U32(vp, 0);
                G32(0x6ccc68) = U32(vp, 4);
                qflags = U32(vp, 4) >> 16;
                pk = recbase + (uint32_t)U16(qlist - 8, 0) * 0x120;
                pp_transform_project(PTR(vptr));
                col--;
                dp -= 2;
                vp += U16(dp, 0);
                if (rows == 1 && (flags & 2)) {
                    int near;

                    if (!stitched) {
                        stitched = 1;
                        skip = skip0 + 2 * U16(skip0, 0) + 2;
                    }
                    near = cur < edge;
                    if (!near) {
                        edge += U16(skip, 0);
                        skip -= 2;
                    }
                    if (near && col != 0) {
                        pp_copy6(tmp2, vptr - 0x18);
                        G32(0x6ccc64) = U32(vp, 0);
                        G32(0x6ccc68) = U32(vp, 4);
                        pp_transform_project_pushed(PTR(tmp));
                        U32(vptr, 0x10) = x87_add_mul(U32(tmp, 0x10), U32(tmp2, 0x10), G32(0x4a3338));
                        U32(vptr, 0x14) = x87_add_mul(U32(tmp, 0x14), U32(tmp2, 0x14), G32(0x4a3338));
                        U32(vptr, 4) = x87_add_mul(U32(tmp, 4), U32(tmp2, 4), G32(0x4a3338));
                        vertex_project_codes_at(vptr);
                        pp_edge_nudge(vptr, U32(tmp, 8), U32(tmp, 0xc), U32(tmp2, 8), U32(tmp2, 0xc), inv);
                    }
                }
                if (col == quads)
                    goto next; /* a row's first vertex: no quad yet */
                if (lod != 1) {
                    qflags = nib | 4;
                    nib = (uint32_t)((int32_t)nib >> 4);
                    if (!nib) {
                        nib = U32(nib_ptr, 0);
                        nib_ptr += 4;
                    }
                }
                p0 = vptr - 0x18 * (uint32_t)(int32_t)(int16_t)l50;
                p1 = vptr - 0x18 * (uint32_t)(int32_t)(int16_t)l74;
                p2 = vptr - 0x18;
                p3 = vptr;
                if (U32(p3, 0) & (U32(p1, 0) & U32(p0, 0) & U32(p2, 0)))
                    goto next;
                if (qflags & 4) {
                    a = p0;
                    b = p1;
                    c = p2;
                    d = p3;
                } else {
                    a = p1;
                    b = p3;
                    c = p0;
                    d = p2;
                }
                if ((U32(b, 0) | U32(c, 0) | U32(a, 0)) & 0x10) {
                    U32(a, 0) |= 0x80;
                    f1 = 0x3f800000;
                } else {
                    f1 = pp_facing(a, b, c);
                    if (inv)
                        f1 = x87_neg(f1);
                }
                if (!(U8(a, 0) & 0x80) && !(U8(d, 0) & 0x10)) {
                    f2 = pp_facing(b, d, c);
                    if (inv)
                        f2 = x87_neg(f2);
                } else {
                    f2 = 0x3f800000;
                    U32(b, 0) |= 0x80;
                }
                s1 = x87_cmp_zero(f1);
                s2 = x87_cmp_zero(f2);
                if (s1 & 0x41) {
                    if (s2 & 0x41)
                        goto next;
                    a = b;
                } else if (s2 & 0x41) {
                    d = c;
                }
                z = I32(p3, 4);
                if (z < I32(p2, 4))
                    z = I32(p2, 4);
                if (z < I32(p1, 4))
                    z = I32(p1, 4);
                if (z < I32(p0, 4))
                    z = I32(p0, 4);
                depth = x87_fist((uint32_t)z) >> 5;
                if (depth < 0)
                    goto next;
                tex = U16(pk, 0x42);
                depth = pp_depth_clamp(depth);
                pp_copy6(pk + 8, a);
                pp_copy6(pk + 0x28, b);
                pp_copy6(pk + 0x48, c);
                pp_copy6(pk + 0x68, d);
                U8(pk, 7) = 0x8c;
                bucket = tex;
                if (qflags & 1) {
                    pk2 = pk + 0x120;
                    pp_copy6(pk2 + 8, a);
                    pp_copy6(pk2 + 0x28, b);
                    pp_copy6(pk2 + 0x48, c);
                    pp_copy6(pk2 + 0x68, d);
                    U8(pk2, 7) = 0x8c;
                    if (G32(0x6cce84))
                        ent = G32(0x6cd518 + 8 * (uint32_t)(U8(pk2, 0x42) & 0x3f));
                    else
                        ent = G32(0x6cd510 + 4 * (uint32_t)depth);
                    ent &= 0x7fffffff;
                    U32(pk2, 0) = U32(ent, 0);
                    U32(ent, 0) = pk2;
                    if (U32(pk2, 0x20) & 0x80000000) {
                        U32(pk2, 4) = U32(pk, 4);
                        U32(pk2, 0x24) = U32(pk, 0x24);
                        U32(pk2, 0x44) = U32(pk, 0x44);
                        U32(pk2, 0x64) = U32(pk, 0x64);
                        if (is12) {
                            U8(pk, 7) |= 1;
                            bucket = 0x3f;
                        }
                        U8(pk2, 7) |= 2;
                        depth += 0x50;
                        if (depth > 0x1ff)
                            depth = 0x1ff;
                    }
                }
                if (G32(0x6cce84))
                    ent = G32(0x6cd518 + 8 * (bucket & 0x3f));
                else
                    ent = G32(0x6cd510 + 4 * (uint32_t)depth);
                ent &= 0x7fffffff;
                U32(pk, 0) = U32(ent, 0);
                U32(ent, 0) = pk;
next:
                qlist += 8;
                vptr += 0x18;
            } while (col != 0);
        }
        l74 = -l74;
        {
            int32_t t = l50;

            l50 = l4c;
            l4c = t;
        }
        qlist -= 8;
        if (vptr >= ring_end)
            vptr = ring;
        if (--rows <= 0)
            break;
    }
#undef FR
}
