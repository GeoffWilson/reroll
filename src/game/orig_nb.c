/* Group NB: the original's 0x417950-0x41df10 still-reachable functions
 * (two source files: the Direct3D polygon clipper and the Direct3D set-up).
 *
 *  - the clipper's vertex pool 0x6cce98 (48-byte vertices, see nb_formats):
 *    vertex adds (0x4180c0, register arguments; 0x418170 for 2D), the
 *    restart 0x418260, the far-plane clip 0x417950, the screen-rectangle
 *    clip 0x418290 and the two clip drivers 0x417af0 / 0x417ce0 (frustum
 *    planes through 0x417060 / 0x417270, original);
 *  - the race screen colours 0x41b2f0, the replay files save 0x41c550;
 *  - the CPU checks 0x41c8c0 / 0x41c8f0 / 0x41c930 (pushfd, cpuid,
 *    GetSystemInfo through the import table);
 *  - the "Fatal" debug printf 0x41c970 / 0x41c9d0 / 0x41cd00 and its no-op
 *    hook 0x41cec0;
 *  - the Direct3D set-up: init 0x41ced0, the texture-coordinate tables
 *    0x41cfd0, QueryInterface / CreateDevice / EnumDevices / device pick /
 *    texture format pick, the z-buffer 0x41d980, the viewport 0x41df10,
 *    render states 0x41db00 and the small state setters, texture slot
 *    release / handle, shutdown 0x41d130.
 *
 * COM calls go through the object's vtable (stdcall), Windows calls through
 * the import table, everything outside the group by address. Float code is
 * the original's x87 instruction sequence in inline asm (memory operands
 * and register intermediates as the original), so results match under any
 * precision control word; integer logic is C. Register-to-register x87
 * operations are emitted as the original's bytes (AT&T operand order for
 * fsubp / fdivp is ambiguous). */
#include "../runtime.h"
#include "physics.h"

#define STDCALL __attribute__((stdcall))

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define P8(a) ((uint8_t *)(uintptr_t)(a))

#define g_config G32(0x4b2198)
#define g_race G32(0x4b1960)

typedef void(GAMEAPI *nb_void_fn)(void);
typedef int32_t(GAMEAPI *nb_int_fn)(void);
typedef int32_t(GAMEAPI *nb_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *nb_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *nb_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *nb_fn4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef float(GAMEAPI *nb_float3_fn)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(STDCALL *nb_com1)(uint32_t self);
typedef int32_t(STDCALL *nb_com2)(uint32_t self, uint32_t a);
typedef int32_t(STDCALL *nb_com3)(uint32_t self, uint32_t a, uint32_t b);
typedef int32_t(STDCALL *nb_com4)(uint32_t self, uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(STDCALL *nb_api1)(uint32_t a);

#define CALL0(a) ((nb_int_fn)(a))()
#define CALL1(a, x) ((nb_fn1)(a))((uint32_t)(x))
#define CALL2(a, x, y) ((nb_fn2)(a))((uint32_t)(x), (uint32_t)(y))
#define CALL3(a, x, y, z) ((nb_fn3)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z))
#define CALL4(a, x, y, z, w) ((nb_fn4)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z), (uint32_t)(w))

/* COM: method at byte offset `off` of the object's vtable. */
#define VT(obj, off) (*(void **)(uintptr_t)(G32(obj) + (off)))
#define COM1(obj, off) ((nb_com1)VT(obj, off))(obj)
#define COM2(obj, off, a) ((nb_com2)VT(obj, off))(obj, (uint32_t)(a))
#define COM3(obj, off, a, b) ((nb_com3)VT(obj, off))(obj, (uint32_t)(a), (uint32_t)(b))
#define COM4(obj, off, a, b, c) ((nb_com4)VT(obj, off))(obj, (uint32_t)(a), (uint32_t)(b), (uint32_t)(c))

#define mem_fill(p, v, n) CALL3(0x476f50, p, v, n)
#define mem_copy(d, s, n) CALL3(0x476f30, d, s, n)
#define block_alloc(n, f) ((uint32_t)CALL2(0x476ae0, n, f))
#define block_free(p) CALL1(0x476b40, p)
#define dx_error(hr) CALL1(0x4291f0, hr)
#define g_hresult G32(0x4d6f18)

/* ---- x87 helpers: the original's instructions ---- */
#define X87_FDIVP_ST1 ".byte 0xde, 0xf9\n\t" /* fdivp st(1), st: st1 / st0 */
#define X87_FADDP_ST1 ".byte 0xde, 0xc1\n\t" /* faddp st(1), st */
#define X87_FADD_ST0 ".byte 0xdc, 0xc0\n\t"  /* fadd st(0), st */

/* fld a; fcomp b; fnstsw ax: the status word's high byte (C0 = 1, C3 = 0x40). */
static inline uint32_t fcmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 0xff;
}

#define F_LESS(a, b) (fcmp(a, b) & 1)              /* below, or unordered */
#define F_GREATER(a, b) ((fcmp(a, b) & 0x41) == 0) /* above (ordered) */

/* fld a; fsub b; fld c; fsub d; fdivp; fstp: (a - b) / (c - d). */
static inline uint32_t x87_ratio(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tflds %3\n\tfsubs %4\n\t" X87_FDIVP_ST1 "fstps %0"
                     : "=m"(r)
                     : "m"(a), "m"(b), "m"(c), "m"(d)
                     : "st", "st(1)");
    return r;
}

/* fld n; fsub c; fmul t; fadd c; fstp: c + (n - c) * t. */
static inline uint32_t x87_lerp(uint32_t n, uint32_t c, uint32_t t)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfmuls %3\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(n), "m"(c), "m"(t)
                     : "st");
    return r;
}

/* fld s; fmul a; fld t; fmul b; faddp; fstp: s * a + t * b. */
static inline uint32_t x87_blend(uint32_t s, uint32_t a, uint32_t t, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tflds %3\n\tfmuls %4\n\t" X87_FADDP_ST1 "fstps %0"
                     : "=m"(r)
                     : "m"(s), "m"(a), "m"(t), "m"(b)
                     : "st", "st(1)");
    return r;
}

static inline uint32_t x87_sub(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

static inline uint32_t x87_add(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld v; fistp [0x6ce75c]; fild [0x6ce75c]; fstp: v rounded to an integer
 * by the current rounding mode (the scratch dword keeps the integer). */
static inline uint32_t x87_round(uint32_t v)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfistpl %2\n\tfildl %2\n\tfstps %0" : "=m"(r) : "m"(v), "m"(G32(0x6ce75c))
                     : "st", "memory");
    return r;
}

/* fild dword; fstp dword. */
static inline uint32_t x87_from_int(uint32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild word; fstp dword. */
static inline uint32_t x87_from_s16(uint16_t v)
{
    uint32_t r;
    __asm__ volatile("filds %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild qword; fstp dword. */
static inline uint32_t x87_from_u64(uint64_t v)
{
    uint32_t r;
    __asm__ volatile("fildll %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* ===================================================================== */
/* The clipper. Vertex pool 0x6cce98 (48 bytes each); polygon list 0x6cd258
 * (count, pointers); clipped list 0x6cd2ac (count, pointers). */
#define POOL 0x6cce98u
#define LIST 0x6cd258u
#define CLIPPED 0x6cd2acu

int32_t GAMEAPI clip_polygon_2d(void);

/* ===== 0x417950: clip a polygon list (count, pointers) to z < *0x6ccd08
 * (the far plane, compared as integers: positive floats). New vertices come
 * from the pool at *next (advanced); returns the output count. ===== */
int32_t GAMEAPI clip_far_z(uint32_t *in, uint32_t *out, int32_t *next)
{
    uint32_t limit = G32(0x6ccd08);
    int32_t idx = *next;
    uint32_t *cur = in + 1;
    int32_t left;
    uint32_t cur_z;
    int keep_cur;
    uint8_t *nv;

    out[0] = 0;
    left = (int32_t)in[0];
    cur_z = U32(in[1], 4);
    keep_cur = (int32_t)cur_z < (int32_t)limit;
    if (left <= 0) {
        *next = idx;
        return (int32_t)out[0];
    }
    nv = P8(POOL + (uint32_t)idx * 0x30);
    do {
        uint32_t nxt = left == 1 ? in[1] : cur[1];
        uint32_t nz;
        int keep_n;

        if (keep_cur) {
            out[out[0] + 1] = *cur;
            out[0] = out[0] + 1;
        }
        nz = U32(nxt, 4);
        keep_n = (int32_t)nz < (int32_t)limit;
        if (keep_cur != keep_n) {
            uint32_t t = x87_ratio(limit, cur_z, nz, cur_z);
            uint32_t c;

            out[out[0] + 1] = (uint32_t)(uintptr_t)nv;
            out[0] = out[0] + 1;
            c = *cur;
            U32(nv, 0x10) = x87_lerp(U32(nxt, 0x10), U32(c, 0x10), t);
            U32(nv, 0x14) = x87_lerp(U32(nxt, 0x14), U32(c, 0x14), t);
            U32(nv, 0x04) = x87_lerp(U32(nxt, 0x04), U32(c, 0x04), t);
            U32(nv, 0x18) = x87_lerp(U32(nxt, 0x18), U32(c, 0x18), t);
            U32(nv, 0x1c) = x87_lerp(U32(nxt, 0x1c), U32(c, 0x1c), t);
            U32(nv, 0x20) = U32(c, 0x20);
            U32(nv, 0x24) = U32(c, 0x24);
            U32(nv, 0x28) = U32(c, 0x28);
            U32(nv, 0x2c) = U32(c, 0x2c);
            idx++;
            nv += 0x30;
        }
        cur_z = nz;
        keep_cur = keep_n;
        cur++;
    } while (--left > 0);
    *next = idx;
    return (int32_t)out[0];
}

/* The cull test of the clip drivers: the facing value (0x4278f0, x87) of
 * three vertices, negated when *0x6cce94; true if negative. */
static int facing_away(uint32_t a, uint32_t b, uint32_t c)
{
    float f = ((nb_float3_fn)0x4278f0)(a, b, c);
    uint32_t bits;

    memcpy(&bits, &f, 4);
    if (G32(0x6cce94))
        bits ^= 0x80000000u;
    return (bits & 0x80000000u) != 0;
}

/* The two clip drivers: flags 0x40 (or *0x4b7fec) -> the 2D clip; 0x100 and
 * no clip codes on the first three vertices -> the list as it is; else the
 * planes of 0x4a7cf0 (masks 0x11, 0x12, 0x14, 0x18; plane records 0x4b7fa8,
 * 16 bytes) through `plane`, ping-ponging between two lists at `bufs`
 * (0x54 bytes apart), flag 0x20 -> `extra`, then `last` into 0x6cd2ac and
 * each clipped vertex projected (0x428900). Flag 0x80 culls back faces. */
static int32_t clip_drive(uint32_t flags, uint32_t plane, uint32_t bufs, uint32_t extra, uint32_t last)
{
    uint32_t cull, list = LIST;
    int32_t next;
    uint32_t pp = 0, i, n;

    if ((G32(0x4b7fec) | flags) & 0x40)
        return clip_polygon_2d();
    cull = flags & 0x80;
    if ((flags & 0x100) && (G32(0x6ccef8) | G32(0x6ccec8) | G32(0x6cce98)) == 0) {
        if (cull && facing_away(G32(0x6cd25c), G32(0x6cd260), G32(0x6cd264)))
            return 0;
        return (int32_t)LIST;
    }
    next = GI32(LIST);
    for (i = 0; i < 4; i++) {
        if (G32(0x4a7cf0 + i * 4) & flags) {
            uint32_t out = bufs + pp * 0x54;
            if (!CALL4(plane, list, 0x4b7fa8 + i * 0x10, out, &next))
                return 0;
            list = out;
            pp ^= 1;
        }
    }
    if (flags & 0x20) {
        uint32_t out = bufs + pp * 0x54;
        if (!CALL3(extra, list, out, &next))
            return 0;
        list = out;
    }
    if (!CALL3(last, list, CLIPPED, &next))
        return 0;
    n = G32(CLIPPED);
    for (i = 0; (int32_t)i < (int32_t)n; i++)
        CALL1(0x428900, G32(CLIPPED + 4 + i * 4));
    if (cull && facing_away(G32(0x6cd2b0), G32(0x6cd2b4), G32(0x6cd2b8)))
        return 0;
    return (int32_t)CLIPPED;
}

/* ===== 0x417af0: clip driver (planes 0x417060, lists 0x4b7f00, extra
 * 0x417600, last 0x417450). Returns the list to draw or 0. ===== */
int32_t GAMEAPI clip_polygon_a(uint32_t flags)
{
    return clip_drive(flags, 0x417060, 0x4b7f00, 0x417600, 0x417450);
}

/* ===== 0x417ce0: clip driver (planes 0x417270, lists 0x4b7e58, extra the
 * far clip 0x417950, last 0x4177b0). ===== */
int32_t GAMEAPI clip_polygon_b(uint32_t flags)
{
    return clip_drive(flags, 0x417270, 0x4b7e58, (uint32_t)(uintptr_t)clip_far_z, 0x4177b0);
}

/* ===== 0x4180c0: add a vertex (register arguments: eax packed colour
 * bytes, ecx / edx indices into the texture coordinate tables *0x6ce660 /
 * *0x6ce65c, ebx six dwords: code, z, x, y, +0x10, +0x14). The slot is the
 * pool + the 0x4a7d00 table's offset for the current count (sic: only four
 * entries). Each colour byte goes through a dword scratch (0x4b7ff0 + 4k)
 * whose low byte alone is written and which is loaded whole. ===== */
void GAMEAPI clip_vertex_add_c(uint32_t packed, uint32_t u, uint32_t v, const uint32_t *src)
{
    uint32_t tu, tv, n;
    uint8_t *vx;

    G8(0x4b7ff0) = (uint8_t)packed;
    tu = G32(G32(0x6ce660) + u * 4);
    tv = G32(G32(0x6ce65c) + v * 4);
    G8(0x4b7ff4) = (uint8_t)(packed >> 8);
    vx = P8(G32(0x4a7d00 + G32(LIST) * 4) + POOL);
    G8(0x4b7ff8) = (uint8_t)(packed >> 16);
    U32(vx, 0x18) = tu;
    U32(vx, 0x1c) = tv;
    U32(vx, 0x08) = src[2];
    U32(vx, 0x0c) = src[3];
    G8(0x4b7ffc) = (uint8_t)(packed >> 24);
    U32(vx, 0x04) = src[1];
    U32(vx, 0x10) = src[4];
    U32(vx, 0x14) = src[5];
    U32(vx, 0x00) = src[0];
    n = G32(LIST);
    G32(LIST + 4 + n * 4) = (uint32_t)(uintptr_t)vx;
    G32(LIST) = n + 1;
    U32(vx, 0x2c) = x87_from_int(G32(0x4b7ffc));
    U32(vx, 0x28) = x87_from_int(G32(0x4b7ff8));
    U32(vx, 0x24) = x87_from_int(G32(0x4b7ff4));
    U32(vx, 0x20) = x87_from_int(G32(0x4b7ff0));
}

/* The register-argument entry: callers keep ebx / esi / edi / ebp. */
__asm__(".text\n"
        ".globl " "clip_vertex_add" "\n"
        "clip_vertex_add:\n\t"
        "pushl %ebx\n\t"
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "pushl %eax\n\t"
        "call " "clip_vertex_add_c" "\n\t"
        "addl $16, %esp\n\t"
        "ret\n");

/* ===== 0x418170: add a 2D vertex: xy (two int16) + the screen offset
 * 0x6ccd54 / 0x6ccd58, z 3.0, code 0x40, texture indices (bytes 0 / 1),
 * colour (bytes 0..3 -> +0x20, +0x24, +0x28, +0x2c). ===== */
void GAMEAPI clip_vertex_add_2d(uint32_t xy, uint32_t uv, uint32_t rgba)
{
    uint8_t *vx = P8(POOL + G32(LIST) * 0x30);
    uint32_t t;

    t = x87_from_s16((uint16_t)xy);
    U32(vx, 0x08) = x87_add(G32(0x6ccd54), t);
    t = x87_from_s16((uint16_t)(xy >> 16));
    U32(vx, 0x04) = 0x40400000u;
    U32(vx, 0x00) = 0x40;
    U32(vx, 0x0c) = x87_add(G32(0x6ccd58), t);
    U32(vx, 0x18) = G32(G32(0x6ce660) + (uv & 0xff) * 4);
    U32(vx, 0x1c) = G32(G32(0x6ce65c) + ((uv >> 8) & 0xff) * 4);
    U32(vx, 0x2c) = x87_from_u64(rgba >> 24);
    U32(vx, 0x20) = x87_from_u64(rgba & 0xff);
    U32(vx, 0x24) = x87_from_u64((rgba >> 8) & 0xff);
    U32(vx, 0x28) = x87_from_u64((rgba >> 16) & 0xff);
    G32(LIST + 4 + G32(LIST) * 4) = (uint32_t)(uintptr_t)vx;
    G32(LIST) = G32(LIST) + 1;
}

/* ===== 0x418260: pool vertex 1 becomes vertex 0, count 1. ===== */
void GAMEAPI clip_vertices_restart(void)
{
    uint32_t i;

    for (i = 0; i < 12; i++)
        G32(POOL + i * 4) = G32(POOL + 0x30 + i * 4);
    G32(LIST) = 1;
}

/* One edge crossing of the 2D clip: the new vertex at `limit` on `axis`
 * (+8 x, +0xc y), the other axis and +0x18 / +0x1c blended, both axes
 * rounded to integers through 0x6ce75c; z and colour from a. */
static void clip_cross(const uint8_t *a, const uint8_t *b, uint8_t *nv, uint32_t limit, uint32_t axis)
{
    uint32_t other = axis == 8 ? 0xc : 8;
    uint32_t t, s, v;

    t = x87_ratio(limit, U32(a, axis), U32(b, axis), U32(a, axis));
    U32(nv, 0x04) = U32(a, 0x04);
    U32(nv, 0x20) = U32(a, 0x20);
    U32(nv, 0x24) = U32(a, 0x24);
    U32(nv, 0x28) = U32(a, 0x28);
    U32(nv, 0x2c) = U32(a, 0x2c);
    s = x87_sub(G32(0x4a331c), t);
    v = x87_blend(s, U32(a, other), t, U32(b, other));
    U32(nv, other) = x87_round(v);
    U32(nv, axis) = x87_round(limit);
    U32(nv, 0x18) = x87_blend(s, U32(a, 0x18), t, U32(b, 0x18));
    U32(nv, 0x1c) = x87_blend(s, U32(a, 0x1c), t, U32(b, 0x1c));
}

/* One pass of the 2D clip against the edge `bit`; codes of the new vertices
 * by `codes` (0: none, 'y': 4 / 8 by y, 'x': 1 / 2 by x). `out_n` may be
 * game memory (the last pass), so it is updated as the original does. */
static void clip_pass(const uint32_t *in, int32_t n, uint32_t *out, uint32_t *out_n, uint32_t bit, uint32_t limit,
                      uint32_t axis, int codes, uint8_t **pool)
{
    int32_t i;

    for (i = 0; i < n; i++) {
        const uint8_t *a = P8(in[i]);
        const uint8_t *b = P8(i < n - 1 ? in[i + 1] : in[0]);

        if (!(U8(a, 0) & bit)) {
            out[*out_n] = (uint32_t)(uintptr_t)a;
            *out_n = *out_n + 1;
            if (!(U8(b, 0) & bit))
                continue;
        } else if (U8(b, 0) & bit) {
            continue;
        }
        clip_cross(a, b, *pool, limit, axis);
        if (codes == 'y') {
            uint32_t c = 0;
            if (F_LESS(U32(*pool, 0xc), G32(0x6ccd6c)))
                c = 4;
            if (F_GREATER(U32(*pool, 0xc), G32(0x6ccd70)))
                c |= 8;
            U32(*pool, 0) = c;
        } else if (codes == 'x') {
            uint32_t c = 0;
            if (F_LESS(U32(*pool, 8), G32(0x6ccd64)))
                c = 1;
            if (F_GREATER(U32(*pool, 8), G32(0x6ccd68)))
                c |= 2;
            U32(*pool, 0) = c;
        }
        out[*out_n] = (uint32_t)(uintptr_t)*pool;
        *out_n = *out_n + 1;
        *pool += 0x30;
    }
}

/* ===== 0x418290: clip the polygon list to the screen rectangle (x
 * 0x6ccd64..0x6ccd68, y 0x6ccd6c..0x6ccd70). 0 if the first vertex is
 * outside the depth range (0x6ccd04..0x6ccd08) or all vertices share an
 * outside code; the list itself if all are inside; else four passes (new
 * vertices from the pool after the list's, positions rounded to integers)
 * into 0x6cd2ac. The last pass leaves its new vertices' codes unset. ===== */
int32_t GAMEAPI clip_polygon_2d(void)
{
    uint32_t v0 = G32(LIST + 4);
    uint32_t all = 0xffffffffu, any = 0;
    int32_t n, i;
    uint32_t a_list[64], b_list[64], a_n, b_n;
    uint8_t *pool;

    if (F_LESS(U32(v0, 4), G32(0x6ccd04)))
        return 0;
    if (F_GREATER(U32(v0, 4), G32(0x6ccd08)))
        return 0;
    n = GI32(LIST);
    if (n - 1 < 0)
        return 0;
    for (i = 0; i < n; i++) {
        uint8_t *v = P8(G32(LIST + 4 + i * 4));
        uint32_t c = 0;
        if (F_LESS(U32(v, 8), G32(0x6ccd64)))
            c = 1;
        if (F_GREATER(U32(v, 8), G32(0x6ccd68)))
            c |= 2;
        if (F_LESS(U32(v, 0xc), G32(0x6ccd6c)))
            c |= 4;
        if (F_GREATER(U32(v, 0xc), G32(0x6ccd70)))
            c |= 8;
        all &= c;
        any |= c;
        U32(v, 0) = c;
    }
    if (all != 0)
        return 0;
    if (any == 0)
        return (int32_t)LIST;
    n = GI32(LIST);
    pool = P8(POOL + (uint32_t)n * 0x30);
    a_n = 0;
    clip_pass((const uint32_t *)(uintptr_t)(LIST + 4), n, a_list, &a_n, 1, G32(0x6ccd64), 8, 'y', &pool);
    b_n = 0;
    clip_pass(a_list, (int32_t)a_n, b_list, &b_n, 2, G32(0x6ccd68), 8, 'y', &pool);
    a_n = 0;
    clip_pass(b_list, (int32_t)b_n, a_list, &a_n, 4, G32(0x6ccd6c), 0xc, 'x', &pool);
    G32(CLIPPED) = 0;
    clip_pass(a_list, (int32_t)a_n, (uint32_t *)(uintptr_t)(CLIPPED + 4), (uint32_t *)(uintptr_t)CLIPPED, 8,
              G32(0x6ccd70), 0xc, 0, &pool);
    return (int32_t)CLIPPED;
}

/* ===== 0x41b2f0: the race screen's colour (*0x6ce734, low 24 bits) into
 * the four screen polygons (indices 0x6ce710) of the model *0x6ce708 (+0x14
 * list when *0x6ce60c is 0x6cd4a0, else +0x18), all four colour words of
 * each, keeping their top bytes. Not in race modes 3 / 8 or with the screen
 * off (0x6ce704 == 0). ===== */
void GAMEAPI screen_colour_apply(void)
{
    uint8_t mode = U8(g_race, 0x551);
    uint32_t list, colour;
    int i;

    if (mode == 3 || mode == 8 || G32(0x6ce704) == 0)
        return;
    if (G32(0x6ce60c) == 0x6cd4a0)
        list = U32(G32(0x6ce708), 0x14);
    else
        list = U32(G32(0x6ce708), 0x18);
    colour = G32(0x6ce734) & 0xffffff;
    for (i = 0; i < 4; i++) {
        uint8_t *p = P8(list + (uint32_t)(int32_t)(int8_t)G8(0x6ce710 + i) * 0x84);
        uint32_t c = (U32(p, 4) & 0xff000000u) | colour;
        U32(p, 0x04) = c;
        U32(p, 0x24) = c;
        U32(p, 0x44) = c;
        U32(p, 0x64) = c;
    }
}

/* ---- string helpers (the original's inline repne scasb / rep movs) ---- */
static uint32_t nb_strlen(const uint8_t *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* strlen through a volatile pointer: the compiler may not assume s is
 * non-null from it (the original reads a null %s, after copying "(null)"). */
static uint32_t nb_strlen_v(const volatile uint8_t *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void nb_strcpy(uint8_t *d, const uint8_t *s)
{
    uint32_t n = nb_strlen(s) + 1, i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
}

static void nb_strcat(uint8_t *d, const uint8_t *s)
{
    nb_strcpy(d + nb_strlen(d), s);
}

/* ===== 0x41c550: save the race's replay cars: creates "..\TEMP"
 * (0x477930), deletes the old replay files (0x4743e0), then per car
 * (race +0x54d) a temporary name (0x477910: GetTempFileName in "..\TEMP\",
 * prefix "ARF"), its file part into the config's replay paths (+0xccc +
 * 0x20 i) and the car's 0x1e884-byte record (race +0x12a0) written to
 * "..\TEMP\<name>" (0x4654f0). The original's 0x40-byte path buffer is
 * larger here (a longer name would have run into its neighbour). ===== */
void GAMEAPI replay_files_save(void)
{
    uint32_t cfg = g_config;
    uint32_t paths[6];
    uint8_t name[0x200], path[0x240];
    uint32_t i, off;

    for (i = 0; i < 6; i++)
        paths[i] = cfg + 0xccc + i * 0x20;
    CALL1(0x477930, 0x4a7f8c);
    CALL1(0x4743e0, 0);
    if (U8(g_race, 0x54d) == 0)
        return;
    for (i = 0, off = 0;; i++, off += 0x1e884) {
        int32_t k;
        const uint8_t *file;

        CALL3(0x477910, 0x4a7f7c, 0x4a7f88, name);
        k = (int32_t)nb_strlen(name) - 1;
        while (k >= 0 && name[k] != '\\')
            k--;
        memcpy(path, P8(0x4a7f7c), 9);
        file = name + k + 1;
        nb_strcat(path, file);
        nb_strcpy(P8(paths[i]), file);
        CALL3(0x4654f0, path, g_race + 0x12a0 + off, 0x1e884);
        if (!((int32_t)(i + 1) < (int32_t)U8(g_race, 0x54d)))
            break;
    }
}

/* ===== 0x41c8c0: 1 if the CPU has cpuid (the EFLAGS ID bit can be
 * toggled; it is left toggled, as the original). ===== */
int32_t GAMEAPI cpuid_available(void)
{
    uint32_t before, after;

    __asm__ volatile("pushfl\n\tpopl %0\n\tmovl %0, %1\n\txorl $0x200000, %0\n\tpushl %0\n\tpopfl\n\t"
                     "pushfl\n\tpopl %0"
                     : "=&r"(after), "=&r"(before)
                     :
                     : "cc");
    return after != before;
}

/* ===== 0x41c8f0: cpuid leaf 1: MMX (edx bit 23) -> 0x4b8258, FPU (bit 0)
 * -> 0x4b825c. ===== */
void GAMEAPI cpu_features_read(void)
{
    uint32_t a = 1, b, c, d;

    G32(0x4b8258) = 0;
    G32(0x4b825c) = 0;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (d & 0x800000)
        G32(0x4b8258) = 1;
    if (d & 1)
        G32(0x4b825c) = 1;
}

/* ===== 0x41c930: GetSystemInfo (import 0x4a313c): an Intel 386 / 486
 * (processor type 386 / 486, architecture 0) -> 0; else reads the cpuid
 * features and returns 1. ===== */
int32_t GAMEAPI cpu_check(void)
{
    uint8_t info[0x24];
    uint32_t type;

    (*(nb_api1 *)0x4a313c)((uint32_t)(uintptr_t)info);
    type = U32(info, 0x18);
    if ((type == 0x182 || type == 0x1e6) && U16(info, 0) == 0)
        return 0;
    cpu_features_read();
    return 1;
}

/* ===== 0x41cec0: an empty debug hook (three arguments, none read). ===== */
void GAMEAPI debug_hook_nop(void)
{
}

/* ===== 0x41cd00: the Fatal printf's number: value in the conversion's base
 * ('d' 'i' signed decimal, 'u' decimal, 'o' octal, 'b' binary, 'x' / 'p'
 * / 'X' hex, lower case for 'x' only), at least `width` digits (a negative
 * width never ends, as the original), into dst. Returns dst. ===== */
uint8_t *GAMEAPI fatal_number(uint8_t *dst, uint32_t value, uint32_t conv, int32_t width)
{
    uint32_t base = 10;
    int is_signed = 1, neg = 0;
    uint8_t alpha = 'a', digit = 0;
    uint8_t buf[0x40];
    int32_t n = 0, k;
    uint32_t j;

    switch (conv & 0xff) {
    case 'X':
    case 'p': base = 16; is_signed = 0; alpha = 'A'; break;
    case 'b': base = 2; is_signed = 0; break;
    case 'd':
    case 'i': base = 10; break;
    case 'o': base = 8; is_signed = 0; break;
    case 'u': base = 10; is_signed = 0; break;
    case 'x': base = 16; is_signed = 0; alpha = 'a'; break;
    default: break;
    }
    if (value == 0 && width == 0) {
        dst[0] = '0';
        dst[1] = 0;
        return dst;
    }
    if (is_signed && base == 10 && (value & 0x80000000u)) {
        value = 0u - value;
        neg = 1;
    }
    for (;;) {
        uint32_t r;
        if (width == 0) {
            if (value == 0)
                break;
        } else if (width > 0 && n >= width && value == 0) {
            break;
        }
        r = value % base;
        value /= base;
        if (r <= 9)
            digit = (uint8_t)('0' + r);
        else if (r <= 0x24)
            digit = (uint8_t)(alpha + r - 10);
        buf[n++] = digit;
    }
    buf[n] = 0;
    if (neg)
        nb_strcat(buf, P8(0x4a8070));
    k = (int32_t)nb_strlen(buf) - 1;
    j = 0;
    while (k >= 0)
        dst[j++] = buf[k--];
    dst[j] = 0;
    return dst;
}

/* isdigit of the C runtime (inline _pctype test, or _isctype with a
 * multibyte code page). */
static int fatal_isdigit(uint8_t c)
{
    if (GI32(0x4b4d0c) > 1)
        return CALL2(0x498051, c, 4) != 0;
    return (U8(G32(0x4b4b00), (uint32_t)c * 2) & 4) != 0;
}

#define FATAL_BUF 0x4b8264u

/* ===== 0x41c9d0: the Fatal printf into 0x4b8264: %% %c %s (padded with
 * `width` spaces when shorter, sic) %d %i %u %o (0 prefix) %x %X (0x) %b
 * (% prefix) %p (0x, 8 digits); other conversions print an error (0x497f7a)
 * and exit (0x476f80). A null %s faults, as the original. Returns the
 * length (16 bits). ===== */
uint32_t GAMEAPI fatal_format(const uint8_t *fmt, const uint32_t *args)
{
    uint8_t *out = P8(FATAL_BUF);
    const uint32_t *ap = args - 1;
    uint8_t c = *fmt;

    if (c) {
        for (;;) {
            if (c != '%') {
                *out++ = c;
            } else {
                int32_t width = 0;
                uint32_t len;
                fmt++;
                if (fatal_isdigit(*fmt)) {
                    while (*fmt == '0')
                        fmt++;
                    width = CALL1(0x498046, fmt);
                    while (fatal_isdigit(*fmt))
                        fmt++;
                }
                switch (*fmt) {
                case '%':
                    out[0] = '%';
                    out++;
                    out[0] = 0;
                    break;
                case 's': {
                    const uint8_t *s = P8(*++ap);
                    if (s)
                        nb_strcpy(out, s);
                    else
                        memcpy(out, P8(0x4a8068), 7);
                    len = nb_strlen_v(s); /* a null s faults here, after "(null)" */
                    if (len < (uint32_t)width && width > 0) {
                        int32_t k;
                        for (k = width; k != 0; k--)
                            nb_strcat(out, P8(0x4a8064));
                    }
                    break;
                }
                case 'c':
                    out[0] = (uint8_t)*++ap;
                    out[1] = 0;
                    break;
                case 'b':
                    memcpy(out, P8(0x4a8060), 2);
                    while (*out)
                        out++;
                    fatal_number(out, *++ap, *fmt, width);
                    break;
                case 'o':
                    memcpy(out, P8(0x4a805c), 2);
                    while (*out)
                        out++;
                    fatal_number(out, *++ap, *fmt, width);
                    break;
                case 'd':
                case 'i':
                case 'u':
                    fatal_number(out, *++ap, *fmt, width);
                    break;
                case 'X':
                case 'x':
                    memcpy(out, P8(0x4a8058), 3);
                    while (*out)
                        out++;
                    fatal_number(out, *++ap, *fmt, width);
                    break;
                case 'p':
                    memcpy(out, P8(0x4a8058), 3);
                    while (*out)
                        out++;
                    fatal_number(out, *++ap, *fmt, 8);
                    break;
                default:
                    CALL2(0x497f7a, 0x4a802c, *fmt);
                    CALL0(0x476f80);
                    break;
                }
                while (*out)
                    out++;
            }
            c = *++fmt;
            if (!c)
                break;
        }
    }
    *out = 0;
    return nb_strlen(P8(FATAL_BUF)) & 0xffff;
}

/* ===== 0x41c970: Fatal(format, ...): formats into 0x4b8264 (the argument
 * pointer kept at 0x4b8260 meanwhile), "too long" (> 0x100) is itself
 * fatal, then the debug hook, the debug output 0x428310 and exit
 * (0x476f80, a tail jump in the original). ===== */
void GAMEAPI fatal_error(const uint8_t *fmt, ...)
{
    __builtin_va_list ap;
    uint32_t n;

    __builtin_va_start(ap, fmt);
    G32(0x4b8260) = (uint32_t)(uintptr_t)ap;
    n = fatal_format(fmt, (const uint32_t *)(void *)ap) & 0xffff;
    G32(0x4b8260) = 0;
    __builtin_va_end(ap);
    if (n > 0x100)
        fatal_error(P8(0x4a8004));
    debug_hook_nop();
    CALL1(0x428310, FATAL_BUF);
    CALL0(0x476f80);
}

/* ===================================================================== */
/* Direct3D set-up. Objects: DirectDraw 0x4d5f40 (primary surface 0x4d5f4c,
 * z-buffer 0x4d5fa0), Direct3D2 0x4b8380, device 0x4b8384, viewport
 * 0x4b8388; last HRESULT 0x4d6f18. */
#define g_d3d 0x4b8380u
#define g_device 0x4b8384u
#define g_viewport 0x4b8388u
#define g_ddraw 0x4d5f40u
#define RS(state, value) COM3(G32(g_device), 0x5c, state, value) /* SetRenderState */

/* ===== 0x41cfd0: the texture coordinate tables: three blocks (0x200,
 * 0x1800, 0x1130 bytes; old ones freed) and in the last, for n = 1 << i (i
 * 0..8), the table 0x4bdddc[i] of n + 1 floats k / (n - a) + a, a = 1 / n /
 * 2.1; the 256 one is both current tables (0x6ce660 / 0x6ce65c). ===== */
void GAMEAPI d3d_tables_alloc(void)
{
    uint32_t i;

    if (G32(0x4bcd7c))
        block_free(G32(0x4bcd7c));
    G32(0x4bcd7c) = block_alloc(0x200, 0);
    if (G32(0x4bcd80))
        block_free(G32(0x4bcd80));
    G32(0x4bcd80) = block_alloc(0x1800, 0);
    if (G32(0x4bde00))
        block_free(G32(0x4bde00));
    G32(0x4bde00) = block_alloc(0x1130, 0);
    for (i = 0; i < 9; i++) {
        uint32_t count = 1u << i, k;
        uint32_t f, a, b, kf, table;
        uint32_t one = G32(0x4a331c);

        table = (((1u << ((i - 1) & 31)) + 2) << 4) + G32(0x4bde00);
        G32(0x4bdddc + i * 4) = table;
        f = x87_from_int(count);
        __asm__ volatile("flds %1\n\tfdivs %2\n\tfdivs %3\n\tfstps %0"
                         : "=m"(a)
                         : "m"(one), "m"(f), "m"(G32(0x4a3328))
                         : "st");
        __asm__ volatile("flds %1\n\tfsubs %2\n\tfdivrs %3\n\tfstps %0" : "=m"(b) : "m"(f), "m"(a), "m"(one) : "st");
        for (k = 0; k <= count; k++) {
            kf = x87_from_int(k);
            __asm__ volatile("flds %1\n\tfmuls %2\n\tfadds %3\n\tfstps %0"
                             : "=m"(G32(G32(0x4bdddc + i * 4) + k * 4))
                             : "m"(kf), "m"(b), "m"(a)
                             : "st");
        }
    }
    G32(0x6ce660) = G32(0x4bddfc);
    G32(0x6ce65c) = G32(0x4bddfc);
}

/* ===== 0x41d100: DirectDraw QueryInterface for Direct3D2 (IID 0x4a3860)
 * into 0x4b8380; 1 on success. ===== */
int32_t GAMEAPI ddraw_query_d3d(void)
{
    g_hresult = (uint32_t)COM3(G32(g_ddraw), 0, 0x4a3860, g_d3d);
    return g_hresult == 0;
}

/* ===== 0x41d250: enumerate the Direct3D devices (callback 0x41d280);
 * returns 1. ===== */
int32_t GAMEAPI d3d_devices_enumerate(void)
{
    G32(0x4ba6f4) = 0;
    g_hresult = (uint32_t)COM3(G32(g_d3d), 0xc, 0x41d280, 0);
    return 1;
}

/* ===== 0x41d6d0: the first device with a hardware colour model (+0x314)
 * copied to 0x4b838c; none -> config +0x48 = 0xb, 0. ===== */
int32_t GAMEAPI d3d_device_pick(void)
{
    int32_t i, n = GI32(0x4ba6f4);

    for (i = 0; i < n; i++) {
        if (G32(0x4b89d8 + (uint32_t)i * 0x338) != 0) {
            mem_copy(0x4b838c, 0x4b86c4 + (uint32_t)i * 0x338, 0x338);
            return 1;
        }
    }
    U32(g_config, 0x48) = 0xb;
    return 0;
}

/* ===== 0x41d790: EnumTextureFormats callback (stdcall: surface
 * description, context): record n (count 0x4baffc) of the 0x94-byte
 * records at 0x4bb000 cleared, the 0x6c-byte description copied in; +0x6c
 * paletted, +0x90 its bits (8 for flag 0x20, 4 for flag 0x08 of the pixel
 * format flags +0x4c), else 0 / 0 and 0x487480(desc, record + 0x70) reads
 * the masks; count up; returns 1 (go on). No bound on the count (sic). ===== */
int32_t __attribute__((stdcall)) d3d_texture_format_cb(const uint8_t *desc, void *context)
{
    uint32_t rec = 0x4bb000 + G32(0x4baffc) * 0x94;
    uint32_t flags = *(const uint32_t *)(desc + 0x4c);

    (void)context;
    for (uint32_t k = 0; k < 0x25; k++)
        G32(rec + k * 4) = 0;
    for (uint32_t k = 0; k < 0x1b; k++)
        G32(rec + k * 4) = ((const uint32_t *)desc)[k];
    if (flags & 0x20) {
        G32(rec + 0x6c) = 1;
        G32(rec + 0x90) = 8;
    } else if (flags & 0x08) {
        G32(rec + 0x6c) = 1;
        G32(rec + 0x90) = 4;
    } else {
        G32(rec + 0x6c) = 0;
        G32(rec + 0x90) = 0;
        CALL2(0x487480, (uint32_t)desc, rec + 0x70);
    }
    G32(0x4baffc)++;
    return 1;
}

/* ===== 0x41d730: enumerate the texture formats (callback 0x41d790),
 * then the chosen one (0x41f060(0, 0)) of the 0x94-byte records at
 * 0x4bb000 copied to 0x4bcce8. ===== */
int32_t GAMEAPI d3d_texture_format_pick(void)
{
    uint32_t i;

    G32(0x4baffc) = 0;
    g_hresult = (uint32_t)COM3(G32(g_device), 0x24, 0x41d790, 0);
    if (g_hresult)
        return 0;
    i = (uint32_t)CALL2(0x41f060, 0, 0);
    for (uint32_t k = 0; k < 0x25; k++)
        G32(0x4bcce8 + k * 4) = G32(0x4bb000 + i * 0x94 + k * 4);
    return 1;
}

/* ===== 0x41d1f0: CreateDevice (guid 0x4b8690, the primary surface) into
 * 0x4b8384; then the texture formats when 0x4b86a4. ===== */
int32_t GAMEAPI d3d_device_create(void)
{
    g_hresult = (uint32_t)COM4(G32(g_d3d), 0x20, 0x4b8690, G32(0x4d5f4c), g_device);
    if (g_hresult) {
        dx_error(g_hresult);
        return 0;
    }
    G32(0x4baffc) = 0;
    if (G32(0x4b86a4) && !d3d_texture_format_pick())
        return 0;
    return 1;
}

/* ===== 0x41d890: release texture slot i (0x4ba6fc + 0x24 i: objects at
 * +8, +4, +0xc), +0 = 0, +0x18 = -1, one fewer in use (0x4ba6f8). ===== */
int32_t GAMEAPI texture_slot_release(int32_t i)
{
    uint32_t s = (uint32_t)i * 0x24;

    if (G32(s + 0x4ba704)) {
        COM1(G32(s + 0x4ba704), 8);
        G32(s + 0x4ba704) = 0;
    }
    if (G32(s + 0x4ba700)) {
        COM1(G32(s + 0x4ba700), 8);
        G32(s + 0x4ba700) = 0;
    }
    if (G32(s + 0x4ba708)) {
        COM1(G32(s + 0x4ba708), 8);
        G32(s + 0x4ba708) = 0;
    }
    G32(s + 0x4ba6fc) = 0;
    G32(s + 0x4ba714) = 0xffffffffu;
    if (GI32(0x4ba6f8) > 0)
        G32(0x4ba6f8) = G32(0x4ba6f8) - 1;
    return 1;
}

/* ===== 0x41d910: release all 64 texture slots (63 down to 0). ===== */
int32_t GAMEAPI texture_slots_release(void)
{
    int32_t i;

    for (i = 0x3f; i >= 0; i--)
        texture_slot_release(i);
    G32(0x6cce50) = 0;
    G32(0x6cce54) = 0;
    G32(0x4ba6f8) = 0;
    return 1;
}

/* ===== 0x41d940: texture slot i's handle (GetHandle of +0xc on the
 * device) into +0. ===== */
int32_t GAMEAPI texture_slot_handle(int32_t i)
{
    uint32_t s = (uint32_t)i * 0x24;

    g_hresult = (uint32_t)COM3(G32(s + 0x4ba708), 0xc, G32(g_device), s + 0x4ba6fc);
    if (g_hresult) {
        dx_error(g_hresult);
        return 0;
    }
    return 1;
}

/* ===== 0x41d980: (re)create the z-buffer (width, height) when 0x6cce7c:
 * the old one released, a surface of the device's deepest z depth
 * (0x4b8634 bits 0x100..0x800 -> 32, 24, 16, 8), in video memory when
 * 0x4b86a0 (else system memory), attached to the primary surface; its
 * description read back (0x487660) for the video-memory flag 0x4d5f9c.
 * Sets 0x6cce7c / 0x4d5f98 on success. CreateSurface failing returns 1
 * (sic). ===== */
int32_t GAMEAPI zbuffer_create(uint32_t width, uint32_t height)
{
    uint32_t desc[0x1b];
    uint32_t caps, i;

    if (G32(0x6cce7c) == 0) {
        G32(0x4d5f98) = 0;
        return 1;
    }
    G32(0x6cce7c) = 0;
    G32(0x4d5f98) = 0;
    if (G32(0x4d5fa0)) {
        COM1(G32(0x4d5fa0), 8);
        G32(0x4d5fa0) = 0;
    }
    if (G32(0x4b86a8) == 0)
        return 1;
    for (i = 0; i < 0x1b; i++)
        desc[i] = 0;
    desc[2] = height;
    desc[3] = width;
    desc[0] = 0x6c;
    desc[1] = 0x47;
    desc[0x1a] = (G32(0x4b86a0) ? 0x3800u : 0) + 0x20800u;
    caps = G32(0x4b8634);
    if (caps & 0x100)
        desc[6] = 0x20;
    else if (caps & 0x200)
        desc[6] = 0x18;
    else if (caps & 0x400)
        desc[6] = 0x10;
    else if (caps & 0x800)
        desc[6] = 8;
    else
        return 0;
    g_hresult = (uint32_t)COM4(G32(g_ddraw), 0x18, desc, 0x4d5fa0, 0);
    if (g_hresult)
        return 1;
    g_hresult = (uint32_t)COM2(G32(0x4d5f4c), 0xc, G32(0x4d5fa0));
    if (g_hresult)
        return 0;
    if (!CALL2(0x487660, desc, G32(0x4d5fa0)))
        return 0;
    G32(0x4d5f9c) = (desc[0x1a] >> 14) & 1;
    if (G32(0x4b86a0) != 0 && G32(0x4d5f9c) == 0)
        return 0;
    G32(0x6cce7c) = 1;
    G32(0x4d5f98) = 1;
    return 1;
}

/* ===== 0x41dd90: texture filtering (states 0x12 / 0x11): linear when
 * 0x6cce90, else nearest; 0x6ccdcc = 1. ===== */
void GAMEAPI d3d_filter_set(void)
{
    uint32_t mode = (G32(0x6cce90) != 0) + 1;

    if (G32(g_device)) {
        RS(0x12, mode);
        RS(0x11, mode);
        G32(0x6ccdcc) = 1;
    }
}

/* ===== 0x41ddd0: nearest filtering; returns the previous 0x6ccdcc. ===== */
int32_t GAMEAPI d3d_filter_nearest(void)
{
    uint32_t old = G32(0x6ccdcc);

    if (G32(g_device)) {
        RS(0x12, 1);
        RS(0x11, 1);
        G32(0x6ccdcc) = 0;
    }
    return (int32_t)old;
}

/* ===== 0x41de10: fog colour (r, g, b) -> 0x4bcdac, state 0x22. ===== */
void GAMEAPI d3d_fog_colour_set(uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t c = (((r << 8) | g) << 8) | b;

    G32(0x4bcdac) = c;
    RS(0x22, c);
}

/* ===== 0x41de40 / 0x41de60: fog on (state 0x1c = 0x4bcdc0) / off. ===== */
void GAMEAPI d3d_fog_on(void)
{
    RS(0x1c, G32(0x4bcdc0));
    G32(0x6cce4c) = 1;
}

void GAMEAPI d3d_fog_off(void)
{
    RS(0x1c, 0);
    G32(0x6cce4c) = 0;
}

/* ===== 0x41de80 / 0x41dec0: z-buffering on (with a z-buffer, 0x4d5f98:
 * enable, write, less-equal) / off. ===== */
void GAMEAPI d3d_zbuffer_on(void)
{
    if (G32(0x4d5f98)) {
        RS(7, 1);
        RS(0xe, 1);
        RS(0x17, 4);
    }
}

void GAMEAPI d3d_zbuffer_off(void)
{
    RS(7, 0);
    RS(0xe, 0);
}

/* ===== 0x41db00: the render states, then filtering and z-buffering;
 * 0x6cce68 = 0. ===== */
int32_t GAMEAPI d3d_render_states(void)
{
    if (G32(g_device) == 0)
        return 1;
    RS(8, G32(0x4bcda8));
    RS(9, G32(0x4bcda4));
    RS(0x1a, G32(0x6cce8c));
    RS(2, G32(0x6cce88));
    RS(0x16, G32(0x4bcdbc));
    RS(0x28, 0);
    RS(0x1d, 0);
    RS(0x1f, 1);
    RS(0x20, 0);
    RS(0xb, G32(0x4b86b4) ? 0 : 1);
    RS(0xc, 0xd);
    RS(0x10, 0);
    RS(1, 0);
    RS(4, 1);
    if (G8(0x4b86bc) & 2) {
        RS(0x15, 4);
    } else {
        RS(0x15, (G8(0x4b8618) & 2) ? 2 : 4);
        RS(0x13, 2);
        RS(0x14, 2);
    }
    RS(0x13, G32(0x4bcd8c));
    RS(0x14, G32(0x4bcd90));
    RS(3, 3);
    RS(5, 0);
    RS(6, 0);
    RS(0x17, 4);
    RS(0x1e, 0);
    RS(0x1c, G32(0x4bcdc0));
    RS(0x22, G32(0x4bcdac));
    RS(0x23, 0);
    RS(0x29, 0);
    RS(0xf, 0);
    RS(0x21, 0);
    RS(0x27, 0);
    RS(0x1b, 0);
    d3d_filter_set();
    if (G32(0x6cce7c))
        d3d_zbuffer_on();
    else
        d3d_zbuffer_off();
    G32(0x6cce68) = 0;
    return 1;
}

/* ===== 0x41dee0: render state defaults. ===== */
void GAMEAPI d3d_state_defaults(void)
{
    G32(0x4bcda4) = 2;
    G32(0x4bcda8) = 3;
    G32(0x4bcdac) = 0x808080;
    G32(0x4bcdb0) = 3;
    G32(0x4bcdbc) = 1;
}

/* ===== 0x41df10: the viewport (width, height): CreateViewport into
 * 0x4b8388, AddViewport, SetViewport (scale = width / 2 both ways, max x / y
 * = size / (2 scale), z 0..1), SetCurrentViewport. ===== */
int32_t GAMEAPI d3d_viewport_create(uint32_t width, uint32_t height)
{
    uint32_t vp[0xb], i, wf, hf, half, two = G32(0x4a3314);

    g_hresult = (uint32_t)COM3(G32(g_d3d), 0x18, g_viewport, 0);
    if (g_hresult)
        return 0;
    g_hresult = (uint32_t)COM2(G32(g_device), 0x18, G32(g_viewport));
    if (g_hresult)
        return 0;
    for (i = 0; i < 0xb; i++)
        vp[i] = 0;
    wf = x87_from_u64(width);
    vp[3] = width;
    vp[4] = height;
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfstps %0" : "=m"(half) : "m"(wf), "m"(two) : "st");
    vp[0] = 0x2c;
    vp[2] = 0;
    vp[1] = 0;
    vp[9] = 0;
    vp[10] = 0x3f800000u;
    vp[5] = half;
    vp[6] = half;
    __asm__ volatile("flds %1\n\tflds %2\n\t" X87_FADD_ST0 X87_FDIVP_ST1 "fstps %0"
                     : "=m"(vp[7])
                     : "m"(wf), "m"(vp[5])
                     : "st", "st(1)");
    hf = x87_from_u64(height);
    __asm__ volatile("flds %1\n\tflds %2\n\t" X87_FADD_ST0 X87_FDIVP_ST1 "fstps %0"
                     : "=m"(vp[8])
                     : "m"(hf), "m"(vp[6])
                     : "st", "st(1)");
    g_hresult = (uint32_t)COM2(G32(g_viewport), 0x14, vp);
    if (g_hresult)
        return 0;
    g_hresult = (uint32_t)COM2(G32(g_device), 0x34, G32(g_viewport));
    return g_hresult == 0;
}

/* ===== 0x41d130: Direct3D shutdown: 0x477450, 0x41e0d0(viewport), the
 * three table blocks freed, z-buffer, viewport, device and Direct3D
 * released. Returns 1. ===== */
int32_t GAMEAPI d3d_shutdown(void)
{
    CALL0(0x477450);
    CALL1(0x41e0d0, G32(g_viewport));
    if (G32(0x4bcd7c)) {
        block_free(G32(0x4bcd7c));
        G32(0x4bcd7c) = 0;
    }
    if (G32(0x4bcd80)) {
        block_free(G32(0x4bcd80));
        G32(0x4bcd80) = 0;
    }
    if (G32(0x4bde00)) {
        block_free(G32(0x4bde00));
        G32(0x4bde00) = 0;
    }
    if (G32(0x4d5fa0)) {
        COM1(G32(0x4d5fa0), 8);
        G32(0x4d5fa0) = 0;
        G32(0x4d5f98) = 0;
    }
    if (G32(g_viewport)) {
        COM1(G32(g_viewport), 8);
        G32(g_viewport) = 0;
    }
    if (G32(g_device)) {
        COM1(G32(g_device), 8);
        G32(g_device) = 0;
    }
    if (G32(g_d3d)) {
        COM1(G32(g_d3d), 8);
        G32(g_d3d) = 0;
    }
    return 1;
}

/* ===== 0x41ced0: Direct3D init (the first argument is not read; the
 * second asks for a z-buffer): clears 0x4b8380 (0x4a1c bytes) and the
 * texture slots, slot handles -1, 0x477440 / 0x477420, the tables,
 * QueryInterface, devices, pick, 0x41fda0, defaults, z-buffer, device,
 * viewport, states, 0x41ed00, 0x41f720, states again, 0x41f2d0(0, 0,
 * screen size). 0 at the first failure, else 1. ===== */
int32_t GAMEAPI d3d_init(uint32_t unused, uint32_t zbuffer)
{
    uint32_t a;

    (void)unused;
    mem_fill(0x4b8380, 0, 0x4a1c);
    mem_fill(0x4ba6fc, 0, 0x900);
    G32(0x6cce5c) = 0xffffffffu;
    for (a = 0x4ba714; a < 0x4bb014; a += 0x24)
        G32(a) = 0xffffffffu;
    CALL0(0x477440);
    CALL0(0x477420);
    d3d_tables_alloc();
    if (!ddraw_query_d3d())
        return 0;
    if (!d3d_devices_enumerate())
        return 0;
    if (!d3d_device_pick())
        return 0;
    CALL0(0x41fda0);
    d3d_state_defaults();
    if (zbuffer)
        zbuffer_create(G32(0x4d5f88), G32(0x4d5f8c));
    if (!d3d_device_create())
        return 0;
    if (!d3d_viewport_create(G32(0x4d5f88), G32(0x4d5f8c)))
        return 0;
    if (!d3d_render_states())
        return 0;
    if (!CALL0(0x41ed00))
        return 0;
    CALL0(0x41f720);
    if (!d3d_render_states())
        return 0;
    CALL4(0x41f2d0, 0, 0, G32(0x4acc38), G32(0x4acc3c));
    return 1;
}
