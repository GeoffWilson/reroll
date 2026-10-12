/* Group NA: the last original functions in 0x4010c0-0x4177b0.
 *
 *  - the CPU checks behind cpu_detect_amd (cpuid 0x80000001 / 0x80000000);
 *  - the sky: the dome of 32 x 3 quads and a fan of triangles round the
 *    view (0x4049b0, its vertex / quad / triangle helpers) and the sky
 *    quads list (0x404f50), with the sun flare;
 *  - blast and collapse helpers (0x401230, 0x405280, 0x4052b0, 0x405ed0,
 *    0x412bf0) and the event camera cut (0x40ae50);
 *  - the TV camera's swap-in and -out of the view's camera object
 *    (0x408270 / 0x40ad60) and the camera blend toward a fixed spot
 *    (0x40cbe0, 0x40cd00, 0x40cd90, 0x40cfa0);
 *  - the screenshot dump to \RollCage\Dumps\Dump%d.bmp (0x405c10);
 *  - the character animation bitmaps (CHARANIM, 0x4114b0-0x411740);
 *  - the float polygon clippers (0x417060-0x4177b0): a plane, and the
 *    near / far depths.
 *
 * Float code is plain C in the original's operation order; values the
 * original stores as floats (plane distances, the clip fraction) go
 * through memory here too, so they are rounded the same way. Everything
 * outside the group is called by address. */
#include "../runtime.h"
#include "physics.h"

#define STDCALL __attribute__((stdcall))

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define F32(p, off) (*(float *)((uint8_t *)(p) + (off)))
#define PTR(p, off) (*(uint8_t **)((uint8_t *)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define GF(a) (*(float *)(uintptr_t)(a))
#define GPTR(a) (*(uint8_t **)(uintptr_t)(a))

#define g_race GPTR(0x4b1960)
#define g_camera_flags G32(0x6cce40)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_gte_vector_lo G32(0x6ccc64)
#define g_gte_vector_hi G32(0x6ccc68)

typedef void(GAMEAPI *na_void_fn)(void);
typedef int32_t(GAMEAPI *na_int_fn)(void);
typedef int32_t(GAMEAPI *na_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *na_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *na_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *na_fn4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(GAMEAPI *na_fn6)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f);
typedef int32_t(GAMEAPI *na_fn8)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                 uint32_t g, uint32_t h);
typedef int32_t(STDCALL *na_api2)(uint32_t a, uint32_t b);

#define CALL0(a) (((na_int_fn)(a))())
#define CALL1(a, x) (((na_fn1)(a))((uint32_t)(x)))
#define CALL2(a, x, y) (((na_fn2)(a))((uint32_t)(x), (uint32_t)(y)))
#define CALL3(a, x, y, z) (((na_fn3)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z)))
#define CALL4(a, x, y, z, w) (((na_fn4)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z), (uint32_t)(w)))
#define PTRARG(p) ((uint32_t)(uintptr_t)(p))

#define grid_unlink(b) CALL1(0x4611d0, PTRARG(b))
#define grid_link(b) CALL1(0x460fb0, PTRARG(b))
#define crumble_start(o) CALL1(0x4427a0, PTRARG(o))
#define view_fov_set(w) CALL1(0x423f00, (w)) /* reads the low word only */
#define gte_project(out) CALL1(G32(0x6ccd78), PTRARG(out))
#define gte_finish() CALL0(G32(0x6ccd80))

/* A float's bits as the original's fstp leaves them, and back. */
static inline uint32_t fbits(float f)
{
    volatile float v = f;
    return *(volatile uint32_t *)&v;
}

static inline float bitsf(uint32_t u)
{
    volatile uint32_t v = u;
    return *(volatile float *)&v;
}

/* ---- CPU checks ---- */

static inline void na_cpuid(uint32_t leaf, uint32_t *a, uint32_t *d)
{
    uint32_t b, c;
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(b), "=c"(c), "=d"(*d) : "a"(leaf));
}

/* 0x4010c0: cpuid 0x80000001, edx bit 31 (3DNow!): 1 or 0. The original's
 * inline asm leaves ebx clobbered (sic); ours preserves it. */
int32_t GAMEAPI cpu_has_3dnow(void)
{
    uint32_t a, d;
    na_cpuid(0x80000001, &a, &d);
    return (d & 0x80000000) ? 1 : 0;
}

/* 0x4010f0: cpuid 0x80000000's eax (highest extended leaf). */
int32_t GAMEAPI cpu_ext_level(void)
{
    uint32_t a, d;
    na_cpuid(0x80000000, &a, &d);
    return (int32_t)a;
}

/* 0x401110: the 3DNow! flag cpu_detect_amd set (0x4b5e44). */
int32_t GAMEAPI cpu_3dnow_get(void)
{
    return GI32(0x4b5e44);
}

/* ---- blasts and collapses ---- */

/* 0x401230: a moving track object's update when it goes (kinds 0x1f..):
 * flags 0x2200 off and 0x1000 on, +0x148 = 0, body_retire, +0x184 = 2,
 * +0x188 = 0, collapse start 0x4427a0, pair_list_remove. Returns 0. */
int32_t GAMEAPI track_object_vanish(uint8_t *obj)
{
    U32(obj, 0x148) = 0;
    U32(obj, 0) = (U32(obj, 0) & ~0x2200u) | 0x1000;
    CALL1(0x419300, PTRARG(obj));
    U32(obj, 0x184) = 2;
    U16(obj, 0x188) = 0;
    crumble_start(obj);
    CALL1(0x478430, PTRARG(obj));
    return 0;
}

/* 0x4052b0: an object blown up: state +0x19a = 5, off the grid, collapse
 * start, every model part (count model+0x27) marked broken (+0x194 words:
 * bits 0x0a000000 off, 0x05000000 on); the blast entry's +9 = 1. */
void GAMEAPI blast_object_wreck(uint8_t *obj)
{
    uint8_t *entry = (uint8_t *)0x6d06e0 + U8(obj, 0x19b) * 16;
    int32_t i;

    U8(obj, 0x19a) = 5;
    grid_unlink(obj);
    crumble_start(obj);
    for (i = 0; i < (int32_t)U8(PTR(obj, 0x10), 0x27); i++)
        U32(PTR(obj, 0x194), i * 4) = (U32(PTR(obj, 0x194), i * 4) & 0xf5ffffffu) | 0x05000000;
    U8(entry, 9) = 1;
}

/* 0x405280: a class-2 object hit: its blast entry's timer bytes +6 / +7
 * cleared, then blown up (0x4052b0). Returns 1. */
int32_t GAMEAPI blast_object_hit(uint8_t *obj)
{
    uint8_t *entry = (uint8_t *)0x6d06e0 + U8(obj, 0x19b) * 16;
    U8(entry, 6) = 0;
    U8(entry, 7) = 0;
    blast_object_wreck(obj);
    return 1;
}

/* 0x40ae50: an event camera cut (kind < 0x11) unless in race mode 2, the
 * kind is 10 / 15 with fewer than 2 players (0x4b65f4) or the last cut is
 * under 45 steps old (0x6cf6d2): event point 0x6cf6b0, kind 0x6cf6a0,
 * length 0x6cf6d4; kind 15 watches from the nearest spot of any kind within
 * 0x5dc (zoom by 0x40add0, object 0x6cf7e0), else from `from` or the
 * nearest spot. */
void GAMEAPI camera_event_cut(uint8_t *from, int32_t *point, uint32_t kind, int16_t length)
{
    uint8_t *spot;

    if (U8(g_race, 0x58c) == 2 || kind > 0x10)
        return;
    if ((kind == 10 || kind == 0xf) && GI32(0x4b65f4) < 2)
        return;
    if ((int16_t)G16(0x6cf6d2) < 0x2d)
        return;
    G32(0x6cf6b0) = (uint32_t)point[0];
    G32(0x6cf6b4) = (uint32_t)point[1];
    G32(0x6cf6a0) = kind;
    G32(0x6cf6b8) = (uint32_t)point[2];
    G16(0x6cf6d4) = (uint16_t)length;
    G16(0x6cf6d2) = 0;
    if (kind == 0xf) {
        CALL4(0x40a570, PTRARG(point), 0xffffffffu, PTRARG(&spot), 0x5dc);
        G32(0x6cf6a4) = U32(spot, 4);
        G32(0x6cf6a8) = U32(spot, 8);
        G32(0x6cf7e0) = PTRARG(from);
        G32(0x6cf6ac) = U32(spot, 0xc);
        G32(0x6cf6c8) = (uint32_t)CALL4(0x40add0, 0x6cf6a4, PTRARG(point), 700, 4);
        G16(0x6cf6d6) = 0;
        return;
    }
    if (from == 0) {
        CALL4(0x40a570, PTRARG(point), 0xffffffffu, PTRARG(&spot), 0);
        G32(0x6cf6a4) = U32(spot, 4);
        G32(0x6cf6a8) = U32(spot, 8);
        G32(0x6cf6ac) = U32(spot, 0xc);
    } else {
        G32(0x6cf6a4) = U32(from, 0);
        G32(0x6cf6a8) = U32(from, 4);
        G32(0x6cf6ac) = U32(from, 8);
    }
    G32(0x6cf6c8) = 0x200;
    G16(0x6cf6d6) = 0;
}

/* 0x405ed0: queue an object and its chain (+0x19c) for collapsing: queue
 * of 64 {object, next index} at 0x6d04c0 (head 0x6d04a0, count 0x6d04a2),
 * the last entry's next 0xff; then an event cut (kind 15, 100 steps) to
 * the last object. Returns 0. */
int32_t GAMEAPI crumble_queue_add(uint8_t *obj)
{
    uint32_t slot;
    int32_t pos[3];

    for (;;) {
        uint8_t head = G8(0x6d04a0);
        uint8_t *next;
        slot = head;
        G8(0x6d04a2) = (uint8_t)(G8(0x6d04a2) + 1);
        G32(0x6d04c0 + slot * 8) = PTRARG(obj);
        next = PTR(obj, 0x19c);
        head++;
        G8(0x6d04a0) = head;
        if (head >= 0x40) {
            head = 0;
            G8(0x6d04a0) = 0;
        }
        if (next == 0)
            break;
        G32(0x6d04c4 + slot * 8) = head;
        obj = PTR(obj, 0x19c);
    }
    G32(0x6d04c4 + slot * 8) = 0xff;
    pos[0] = I32(obj, 0x34);
    pos[1] = I32(obj, 0x38);
    pos[2] = I32(obj, 0x3c);
    camera_event_cut(obj, pos, 0xf, 100);
    return 0;
}

/* 0x412bf0: an object part (index `part` of +0x194) of kind 4 hit by a
 * body: if the root object (+0x1b0 chain) still stands (+0x198 parts left)
 * and the hit is hard enough (part bits 28..31 <= body +0xa8 (x 64 for body
 * kinds 0xc / 0x11, else x 3/4) * |point| >> 16), the part breaks (blast
 * 0x46bb50, +8 bit), the root loses one part; kinds 0xc / 0x11 break every
 * kind-4 part. A root with none left is queued to collapse. Returns 1 if
 * the part broke. */
int32_t GAMEAPI object_part_hit(uint8_t *obj, uint8_t *body, int32_t part, int32_t *point)
{
    uint32_t *entry = (uint32_t *)(PTR(obj, 0x194) + part * 4);
    uint8_t *root = obj;
    int32_t size, scale, all, x, y, z;
    uint32_t kind, cur;

    if ((*entry & 0x0f000000) != 0x04000000)
        return 0;
    while (PTR(root, 0x1b0))
        root = PTR(root, 0x1b0);
    if (U8(root, 0x198) == 0)
        return 0;
    x = point[0] >> 16;
    y = point[1] >> 16;
    z = point[2] >> 16;
    size = CALL1(0x427960, (uint32_t)(x * x + y * y + z * z));
    scale = I32(body, 0xa8);
    kind = U32(body, 0) & 0x1ff;
    if (kind == 0xc || kind == 0x11) {
        scale = (int32_t)((uint32_t)scale << 6);
        all = 1;
    } else {
        all = 0;
        scale = (scale >> 2) + (scale >> 1);
    }
    scale = (int32_t)(((int64_t)scale * size) >> 16);
    cur = *entry;
    if ((int32_t)((cur >> 28) - (uint32_t)scale) > 0)
        return 0;
    *entry = cur & 0x0fffffff;
    entry = (uint32_t *)(PTR(obj, 0x194) + part * 4);
    *entry = (*entry & 0xf5ffffffu) | 0x05000000;
    U16(obj, 8) |= (uint16_t)(1u << (part & 31));
    CALL3(0x46bb50, PTRARG(obj), part, PTRARG(point));
    root = obj;
    while (PTR(root, 0x1b0))
        root = PTR(root, 0x1b0);
    U8(root, 0x198) = (uint8_t)(U8(root, 0x198) - 1);
    if (all) {
        int32_t n = U8(PTR(root, 0x10), 0x27), i;
        for (i = 0; i < n; i++) {
            uint32_t *e = (uint32_t *)(PTR(root, 0x194) + i * 4);
            uint32_t v = *e;
            if ((v & 0x0f000000) == 0x04000000) {
                *e = (v & 0xf5ffffffu) | 0x05000000;
                CALL3(0x46bb50, PTRARG(root), i, PTRARG(point));
            }
        }
        U8(root, 0x198) = 0;
    }
    if (U8(root, 0x198) == 0)
        crumble_queue_add(root);
    return 1;
}

/* ---- the sky ---- */

/* 0x404980: an int16 scaled by 0x6cd400 * 440 / 65536, in place. */
void GAMEAPI dome_scale(int16_t *v)
{
    *v = (int16_t)((int32_t)((uint32_t)(*v * GI32(0x6cd400)) * 440u) >> 16);
}

/* 0x404e00: project an int16 point (GTE vector 0x6ccc64, 8 bytes) into a
 * 24-byte screen vertex (0x6ccd78 then 0x6ccd80); *flag = 2 if the
 * vertex's depth (+4, float) is beyond `far`, else 0. */
void GAMEAPI dome_vertex(const uint32_t *point, uint8_t *out, uint8_t *flag, float far)
{
    g_gte_vector_lo = point[0];
    g_gte_vector_hi = point[1];
    gte_project(out);
    *flag = 0;
    gte_finish();
    if (F32(out, 4) > far)
        *flag = 2;
}

static void copy_words(uint8_t *dst, const uint8_t *src, int n)
{
    int i;
    for (i = 0; i < n; i++)
        U32(dst, i * 4) = U32(src, i * 4);
}

/* Link a packet into an ordering-table list: the list word at `active`
 * (0x6cce84 set) or `other`, top bit masked off. */
static void packet_link(uint8_t *packet, uint32_t active, uint32_t other)
{
    uint32_t list = (G32(0x6cce84) ? G32(active) : G32(other)) & 0x7fffffff;
    U32(packet, 0) = G32(list);
    G32(list) = PTRARG(packet);
}

/* 0x404e50: a quad packet (0x78 bytes) from four screen vertices unless
 * their clip codes (+0) share a bit: vertices at +8, +0x24, +0x40, +0x5c,
 * colour word +4, linked on 0x6cd6f8 / 0x6cdd08. Arguments 6-9 (the
 * vertices' flags) are not read. */
void GAMEAPI dome_quad_add(uint8_t *packet, const uint8_t *a, const uint8_t *b, const uint8_t *c,
                           const uint8_t *d, uint32_t fa, uint32_t fb, uint32_t fc, uint32_t fd, uint32_t colour)
{
    (void)fa, (void)fb, (void)fc, (void)fd;
    if ((U32(c, 0) & U32(d, 0) & U32(a, 0) & U32(b, 0)) != 0)
        return;
    copy_words(packet + 8, a, 6);
    copy_words(packet + 0x24, b, 6);
    copy_words(packet + 0x40, c, 6);
    copy_words(packet + 0x5c, d, 6);
    U32(packet, 4) = colour;
    packet_link(packet, 0x6cd6f8, 0x6cdd08);
}

/* 0x404ee0: the triangle version (0x60 bytes: +8, +0x24, +0x40, colour
 * +4, lists 0x6cd700 / 0x6cdd0c). Arguments 5-7 are not read. */
void GAMEAPI dome_tri_add(uint8_t *packet, const uint8_t *a, const uint8_t *b, const uint8_t *c, uint32_t fa,
                          uint32_t fb, uint32_t fc, uint32_t colour)
{
    (void)fa, (void)fb, (void)fc;
    if ((U32(a, 0) & U32(b, 0) & U32(c, 0)) != 0)
        return;
    copy_words(packet + 8, a, 6);
    copy_words(packet + 0x24, b, 6);
    copy_words(packet + 0x40, c, 6);
    U32(packet, 4) = colour;
    packet_link(packet, 0x6cd700, 0x6cdd0c);
}

/* 0x4049b0: the sky dome round the view object (position +0x34, int16
 * parts): a brightness from 0x486240 tints the sky colour 0x4b5fd0 (quads,
 * code 0x98) and the fog colour (rules 0x6c1eb0 +0x30..0x32, set through
 * 0x428390); the sun flare (0x441d80) unless the rules (+4 bit 4), the
 * race (+0x553 & 3) or the view's cell (+4 bit 8) forbid it; then 3 rings
 * of 32 vertices (table *0x6d0a40: 4 int16 per vertex, the 4th the ring
 * spacing via 0x404980) and a centre, drawn as 32 triangles (packets
 * *0x4b5fbc, 0x60 each, code 0x94) and 64 quads (*0x4b5fb0, 0x78 each).
 * The GTE vector's pad word (0x6ccc6a) gets 0 here, leftover stack in the
 * original. */
void GAMEAPI sky_dome_draw(uint8_t *view)
{
    const int16_t *t = (const int16_t *)GPTR(0x6d0a40);
    uint8_t *tri = GPTR(0x4b5fbc);
    uint8_t *quad = GPTR(0x4b5fb0);
    uint32_t far = G32(0x4b5fac);
    int16_t x = (int16_t)(I32(view, 0x34) >> 16), y = (int16_t)(I32(view, 0x38) >> 16),
            z = (int16_t)(I32(view, 0x3c) >> 16);
    int16_t vec[4];
    uint8_t centre[24], ring_a[32][24], ring_b[32][24], ring_c[32][24];
    uint8_t centre_flag[4] = {0, 0, 0, 0}, flag_a[32], flag_b[32], flag_c[32];
    uint32_t sky_colour, tri_colour, sky = G32(0x4b5fd0);
    const uint8_t *rules;
    int32_t level, ok, i, j;
    uint32_t cell;

    vec[0] = x;
    vec[1] = (int16_t)(y - (int32_t)((uint32_t)t[3] << 2) - t[1]);
    vec[2] = z;
    vec[3] = 0;
    level = (CALL0(0x486240) >> 1) + 0x80;
    sky_colour = (uint32_t)(uint8_t)((int32_t)((sky & 0xff) * level) >> 8) |
                 (uint32_t)(uint8_t)((int32_t)(((sky >> 8) & 0xff) * level) >> 8) << 8 |
                 (uint32_t)(uint8_t)((int32_t)(((sky >> 16) & 0xff) * level) >> 8) << 16 | 0x98000000u;
    rules = GPTR(0x6c1eb0);
    CALL3(0x428390, (uint8_t)((int32_t)(U8(rules, 0x30) * level) >> 7),
          (uint8_t)((int32_t)(U8(rules, 0x31) * level) >> 7), (uint8_t)((int32_t)(U8(rules, 0x32) * level) >> 7));
    tri_colour = (sky_colour & 0xffffff) | 0x94000000u;
    dome_vertex((const uint32_t *)vec, centre, centre_flag, bitsf(far));

    ok = 1;
    if (U8(GPTR(0x6c1eb0), 4) & 4)
        ok = 0;
    if (U8(g_race, 0x553) & 3)
        ok = 0;
    cell = U32(view, 0x11c);
    if (!((int32_t)cell >= 0 && (U8(PTR(GPTR(GPTR(0x6c1e30)), (cell & 0xff) * 4), 4) & 8)) && ok) {
        int32_t sun[3];
        int16_t h;
        int32_t a = t[0x6b] * 5;
        sun[0] = (int32_t)((uint32_t)(t[0x68] + x) << 16);
        h = (int16_t)(y - a / 2 - t[0x69] + CALL0(0x486310));
        dome_scale(&h);
        sun[1] = (int32_t)((uint32_t)(uint16_t)h << 16);
        sun[2] = (int32_t)((uint32_t)(t[0x6a] + z) << 16);
        CALL3(0x441d80, PTRARG(sun), 2, 0x6fff);
    }

    for (i = 0; i < 32; i++) {
        const int16_t *p = t + 4 * i;
        int16_t step = p[3];
        dome_scale(&step);
        vec[0] = (int16_t)(p[0] + x);
        vec[1] = (int16_t)(y - p[1]);
        vec[2] = (int16_t)(p[2] + z);
        dome_vertex((const uint32_t *)vec, ring_a[i], &flag_a[i], bitsf(far));
        vec[1] = (int16_t)(vec[1] - step);
        dome_vertex((const uint32_t *)vec, ring_b[i], &flag_b[i], bitsf(far));
        vec[1] = (int16_t)(vec[1] - step);
        dome_vertex((const uint32_t *)vec, ring_c[i], &flag_c[i], bitsf(far));
    }

    for (j = 1; j <= 32; j++) {
        int32_t p = j - 1, n = j & 31;
        uint8_t *q = quad + 0xf0 * p;
        dome_tri_add(tri + 0x60 * p, centre, ring_c[p], ring_c[n], flag_c[n], flag_c[p], U32(centre_flag, 0),
                     tri_colour);
        dome_quad_add(q, ring_c[p], ring_c[n], ring_b[p], ring_b[n], flag_c[p], flag_c[n], flag_b[p], flag_b[n],
                      sky_colour);
        dome_quad_add(q + 0x78, ring_b[p], ring_b[n], ring_a[p], ring_a[n], flag_b[p], flag_b[n], flag_a[p],
                      flag_a[n], sky_colour);
    }
}

/* 0x404f50: the sky quads (count 0x6d0a00, int16 corner points 4 x 8
 * bytes each at *0x6d0a38, packets *0x4b5fc0 + 0x78 each) unless detail
 * flag 0x6ccd84 bit 2: projected and linked like 0x404e50, colour word
 * left as it is. */
void GAMEAPI sky_quads_draw(void)
{
    const uint32_t *src = (const uint32_t *)GPTR(0x6d0a38);
    uint8_t *packet;
    uint32_t count = G32(0x6d0a00);
    uint8_t v[4][24];
    int k;

    if ((G8(0x6ccd84) & 4) || count == 0)
        return;
    packet = GPTR(0x4b5fc0);
    do {
        for (k = 0; k < 4; k++) {
            g_gte_vector_lo = src[0];
            g_gte_vector_hi = src[1];
            gte_project(v[k]);
            gte_finish();
            src += 2;
        }
        if ((U32(v[3], 0) & U32(v[2], 0) & U32(v[1], 0) & U32(v[0], 0)) == 0) {
            copy_words(packet + 8, v[0], 6);
            copy_words(packet + 0x24, v[1], 6);
            copy_words(packet + 0x40, v[2], 6);
            copy_words(packet + 0x5c, v[3], 6);
            packet_link(packet, 0x6cd6f8, 0x6cdd08);
        }
        packet += 0x78;
    } while (--count != 0);
}

/* ---- screenshots ---- */

/* 0x405c10: the front buffer (0x4d5f4c, size 0x4acc38 x 0x4acc3c, 16-bit
 * pixels unpacked by the shift pairs 0x4d5f60 / 0x4d5f58 / 0x4d5f50) to
 * the first free \RollCage\Dumps\Dump%d.bmp, bottom row first. The info
 * header block from 0x476ae0 is never freed (sic); 0x4b5fe8 gets a header
 * too. Returns 0 if that allocation fails, else 1. */
int32_t GAMEAPI screenshot_dump(void)
{
    int32_t w = GI32(0x4acc38), h = GI32(0x4acc3c), size = w * h * 3 + 0x28, n, row;
    uint8_t *info = (uint8_t *)(uintptr_t)CALL2(0x476ae0, size, 0);
    uint8_t file_header[14], desc[0x6c], name[0x104], line[0x1800];
    uint32_t file;

    if (info == 0)
        return 0;
    G16(0x4b5fea) = (uint16_t)GI32(0x4acc38);
    G16(0x4b5fec) = (uint16_t)GI32(0x4acc38);
    G16(0x4b5fe8) = 0;
    G16(0x4b5fee) = (uint16_t)GI32(0x4acc3c);
    G16(0x4b5ff0) = 0;
    G8(0x4b5ff2) = 0x18;
    G8(0x4b5ff3) = 0;
    G32(0x4b5ff8) = 0;
    G32(0x4b6000) = 0;
    G32(0x4b6004) = 0;
    U16(file_header, 0) = 0x4d42;
    U32(file_header, 2) = (uint32_t)(size + 0xe);
    U16(file_header, 6) = 0;
    U16(file_header, 8) = 0;
    U32(file_header, 10) = 0x36;
    U32(info, 0) = 0x28;
    U32(info, 4) = G32(0x4acc38);
    U32(info, 8) = G32(0x4acc3c);
    U16(info, 0xe) = 0x18;
    U16(info, 0xc) = 1;
    U32(info, 0x10) = 0;
    U32(info, 0x14) = 0;
    U32(info, 0x18) = 0xb12;
    U32(info, 0x1c) = 0xb12;
    U32(info, 0x20) = 0;
    U32(info, 0x24) = 0;
    ((na_api2) * (void **)0x4a3140)(0x4a76d8, 0); /* CreateDirectoryA("\RollCage\Dumps", 0) */
    n = 1;
    do
        CALL3(0x4979f0, PTRARG(name), 0x4a76bc, n++); /* sprintf("\RollCage\Dumps\Dump%d.bmp") */
    while (CALL1(0x476f00, PTRARG(name)));
    file = (uint32_t)CALL1(0x476d10, PTRARG(name));
    if (file == 0)
        return 1;
    CALL3(0x476e50, file, PTRARG(file_header), 0xe);
    CALL3(0x476e50, file, PTRARG(info), 0x28);
    if (CALL3(0x4875a0, 0, G32(0x4d5f4c), PTRARG(desc))) {
        for (row = GI32(0x4acc3c) - 1; row >= 0; row--) {
            const uint16_t *src = (const uint16_t *)(PTR(desc, 0x24) + I32(desc, 0x10) * row);
            int32_t width = GI32(0x4acc38), k;
            if (width > 0) {
                uint32_t r = G32(0x4d5f60), g = G32(0x4d5f58), b = G32(0x4d5f50);
                uint8_t *o = line;
                for (k = width; k != 0; k--) {
                    uint32_t px = *src++;
                    *o++ = (uint8_t)((px >> ((r >> 8) & 31)) << (r & 31));
                    *o++ = (uint8_t)((px >> ((g >> 8) & 31)) << (g & 31));
                    *o++ = (uint8_t)((px >> ((b >> 8) & 31)) << (b & 31));
                }
            }
            CALL3(0x476e50, file, PTRARG(line), width * 3);
        }
        CALL1(0x487610, G32(0x4d5f4c));
    }
    CALL1(0x43b760, file);
    return 1;
}

/* ---- TV camera ---- */

/* 0x408270: the TV camera takes over a view: saves the field of view
 * (0x6cce6c, truncated, to 0x4b6244) and the view's camera object (+0x34,
 * 0xd8 bytes, to 0x4b616c), copies TV state 0x6cf6e0 + view * 0x150 over
 * it, camera flags from the TV record (+0x30, +0x1c) and bit 4, its field
 * of view (+0x24), the sound listener if flag 4. */
void GAMEAPI tv_camera_begin(int32_t view)
{
    uint8_t **camera = (uint8_t **)PTR(g_car_slots + view * CAR_SLOT_SIZE, 0x28);
    uint8_t *tv = (uint8_t *)0x6cf6a0 + view * 0x150;
    uint32_t flags;

    G32(0x4b6244) = (uint32_t)(int32_t)(int64_t)GF(0x6cce6c);
    copy_words((uint8_t *)0x4b616c, *camera + 0x34, 0x36);
    flags = g_camera_flags & 0xfffff83fu;
    G32(0x6cd408) = (uint32_t)view;
    flags |= (((U8(tv, 0x30) & 0xfu) << 1 | (U32(tv, 0x1c) & 1)) << 6) | 0x10;
    g_camera_flags = flags;
    view_fov_set(U16(tv, 0x24));
    grid_unlink(*camera);
    copy_words(*camera + 0x34, tv + 0x40, 0x36);
    if (g_camera_flags & 4) {
        uint8_t *body = *camera;
        CALL4(0x472050, view, PTRARG(body + 0x34), PTRARG(body + 0x68), PTRARG(body + 0xc4));
    }
    grid_link(*camera);
}

/* 0x40ad60: gives the view (0x6cd408) its camera object and field of view
 * back; camera flags 0x10 and 0x40 off. */
void GAMEAPI tv_camera_end(void)
{
    uint8_t **camera = (uint8_t **)PTR(g_car_slots + GI32(0x6cd408) * CAR_SLOT_SIZE, 0x28);

    grid_unlink(*camera);
    copy_words(*camera + 0x34, (const uint8_t *)0x4b616c, 0x36);
    grid_link(*camera);
    view_fov_set(G16(0x4b6244));
    g_camera_flags &= ~0x50u;
}

/* ---- camera blend ---- */

/* 0x40cfa0: out = a / 8 * k + b / 8 * (8 - k), per component (division
 * truncating). */
void GAMEAPI vec_lerp8(const int32_t *a, const int32_t *b, int32_t *out, int32_t k)
{
    int32_t m = 8 - k;
    out[0] = a[0] / 8 * k + b[0] / 8 * m;
    out[1] = b[1] / 8 * m + a[1] / 8 * k;
    out[2] = b[2] / 8 * m + a[2] / 8 * k;
}

/* 0x40cd90: moves the camera object (rec +0x28 -> *) k/8 (rec +0x5a) of
 * the way toward a spot 20 units along the car's side axis (+0xc6.., sign
 * by gravity flag +0x63f), turning it to look along the car's forward /
 * side / up axis (rec +0x50 / +0x4c / +0x4d flags); 0x407220 builds the
 * matrix. */
void GAMEAPI camera_blend_apply(uint8_t **rec)
{
    uint8_t *car = rec[0], *cam = rec[10];
    int32_t look[3], side[3], spot[3], up[3], fwd[3], pos[3], new_up[3], new_look[3];
    uint8_t *body;
    int32_t k;

    look[0] = (int32_t)((uint32_t)S16(car, 0xc8) << 4);
    look[1] = (int32_t)((uint32_t)S16(car, 0xce) << 4);
    look[2] = (int32_t)((uint32_t)S16(car, 0xd4) << 4);
    side[0] = (int32_t)((uint32_t)S16(car, 0xc6) << 4);
    side[1] = (int32_t)((uint32_t)S16(car, 0xcc) << 4);
    side[2] = (int32_t)((uint32_t)S16(car, 0xd2) << 4);
    if (U8(car, 0x63f) == 0) {
        side[0] = -side[0];
        side[1] = -side[1];
        side[2] = -side[2];
    }
    spot[0] = I32(car, 0x34) + side[0] * 20;
    spot[1] = I32(car, 0x38) + side[1] * 20;
    spot[2] = I32(car, 0x3c) + side[2] * 20;
    if (U8(cam, 0x50) == 0) {
        look[0] = -look[0];
        look[1] = -look[1];
        look[2] = -look[2];
    } else if (U8(cam, 0x4c) == 0) {
        int32_t l0 = look[0];
        look[0] = -look[2];
        look[2] = l0;
    } else if (U8(cam, 0x4d) == 0) {
        int32_t l0 = look[0];
        look[0] = look[2];
        look[2] = -l0;
    }
    k = S16(cam, 0x5a);
    body = *(uint8_t **)cam;
    fwd[0] = (int32_t)((uint32_t)S16(body, 0xd0) << 4);
    fwd[1] = (int32_t)((uint32_t)S16(body, 0xd2) << 4);
    fwd[2] = (int32_t)((uint32_t)S16(body, 0xd4) << 4);
    up[0] = (int32_t)((uint32_t)-S16(body, 0xca) << 4);
    up[1] = (int32_t)((uint32_t)-S16(body, 0xcc) << 4);
    up[2] = (int32_t)((uint32_t)-S16(body, 0xce) << 4);
    vec_lerp8(spot, (const int32_t *)(*(uint8_t **)cam + 0x34), pos, k);
    vec_lerp8(side, up, new_up, k);
    vec_lerp8(look, fwd, new_look, k);
    body = *(uint8_t **)cam;
    I32(body, 0x34) = pos[0];
    I32(body, 0x38) = pos[1];
    I32(body, 0x3c) = pos[2];
    CALL3(0x407220, PTRARG(new_look), PTRARG(new_up), PTRARG(*(uint8_t **)cam + 0xc4));
    grid_unlink(*(uint8_t **)cam);
    grid_link(*(uint8_t **)cam);
}

/* 0x40cbe0: unless the blend is off (rec +0x28 -> +0x5a == 0) or camera
 * flags 0x10 / 2 are set: saves the camera object's position (0x4b60e0)
 * and matrix (0x4b6020) and flags bits 6..10 (0x4b6160, 0x4b63bc), blends
 * (0x40cd90); if the camera is then within 16 << 10 of the car, flags get
 * 0x40 and the car's +0x64e. */
void GAMEAPI camera_blend_begin(uint8_t **rec)
{
    uint8_t *cam = rec[10], *body, *car;
    int32_t d[3];
    uint32_t flags;

    if (S16(cam, 0x5a) == 0)
        return;
    if ((g_camera_flags & 0x10) || (g_camera_flags & 2))
        return;
    body = *(uint8_t **)cam;
    G32(0x4b60e0) = U32(body, 0x34);
    G32(0x4b60e4) = U32(body, 0x38);
    G32(0x4b60e8) = U32(body, 0x3c);
    copy_words((uint8_t *)0x4b6020, body + 0xc4, 8);
    camera_blend_apply(rec);
    flags = g_camera_flags;
    G32(0x4b65f8) = 1;
    G32(0x4b6160) = (flags >> 6) & 1;
    G32(0x4b63bc) = (flags >> 7) & 0xf;
    car = rec[0];
    d[0] = (I32(body, 0x34) - I32(car, 0x34)) >> 10;
    d[1] = (I32(body, 0x38) - I32(car, 0x38)) >> 10;
    d[2] = (I32(body, 0x3c) - I32(car, 0x3c)) >> 10;
    if (CALL2(0x40d950, PTRARG(d), PTRARG(d)) < 0x100) {
        g_camera_flags |= 0x40;
        g_camera_flags = (g_camera_flags & 0xfffff87fu) | (uint32_t)(U8(rec[0], 0x64e) & 0xf) << 7;
    }
}

/* 0x40cd00: undoes camera_blend_begin: position, matrix and flag bits
 * back, relinked. */
void GAMEAPI camera_blend_end(uint8_t **rec)
{
    uint8_t *body;

    if (G32(0x4b65f8) == 0)
        return;
    body = *(uint8_t **)rec[10];
    U32(body, 0x34) = G32(0x4b60e0);
    U32(body, 0x38) = G32(0x4b60e4);
    U32(body, 0x3c) = G32(0x4b60e8);
    copy_words(body + 0xc4, (const uint8_t *)0x4b6020, 8);
    grid_unlink(body);
    grid_link(body);
    g_camera_flags = (((G32(0x4b63bc) & 0xf) << 1 | (G32(0x4b6160) & 1)) << 6) | (g_camera_flags & 0xfffff83fu);
    G32(0x4b65f8) = 0;
}

/* ---- character animations ---- */

#define CHARANIM(c, f) ((uint8_t *)0x4b6608 + ((f) + (c)*24) * 0x24)

/* 0x4114b0: uploads character c's frame f (c <= 6, f <= 0x15) into the
 * animation texture (handle 0x4b7da8): palette locked (0x428c30), the
 * frame's RGBX palette (+0x10) stored as BGRX, converted (0x41e1f0) and
 * the pixels (+0x14, size +4 / +2 / +6) blitted (0x477860), unlocked
 * (0x428c60). The frame size goes to 0x4b6600 / 0x4b6604. 0 if out of
 * range, else 1. */
int32_t GAMEAPI charanim_upload(uint32_t c, uint32_t f)
{
    uint8_t *frame = CHARANIM(c, f);
    uint32_t pitch, bits, height;
    uint8_t palette[0x400], converted[0x200];
    int i;

    if (c > 6 || f > 0x15)
        return 0;
    if (((na_fn6)0x428c30)(G32(0x4b7da8), 1, PTRARG(&pitch), PTRARG(palette), PTRARG(&bits), PTRARG(&height))) {
        const uint8_t *src = PTR(frame, 0x10);
        for (i = 0; i < 0x100; i++) {
            palette[i * 4] = src[i * 4 + 2];
            palette[i * 4 + 1] = src[i * 4 + 1];
            palette[i * 4 + 2] = src[i * 4];
            palette[i * 4 + 3] = 0;
        }
        CALL4(0x41e1f0, PTRARG(palette), 0x100, PTRARG(converted), 1);
        ((na_fn8)0x477860)(U32(frame, 0x14), pitch, (uint32_t)S16(frame, 4), height, bits, (uint32_t)S16(frame, 2),
                           (uint32_t)S16(frame, 6), PTRARG(converted));
        CALL3(0x428c60, G32(0x4b7da8), 1, PTRARG(palette));
    }
    GI32(0x4b6600) = S16(frame, 2);
    GI32(0x4b6604) = S16(frame, 6);
    return 1;
}

/* 0x4115e0: loads CHARANIM\<name>%04d.BMP (names 0x4a7cdc..0x4a7cac) for
 * character c, frame f + 1 into its record (0x477530); palette entries
 * with all of R, G, B <= 8 become 4, 4, 4. 0 if out of range or not
 * loaded, else 1 (frame size to 0x4b6600 / 0x4b6604). */
int32_t GAMEAPI charanim_load(uint32_t c, uint32_t f)
{
    static const uint32_t names[7] = {0x4a7cdc, 0x4a7cd4, 0x4a7ccc, 0x4a7cc4, 0x4a7cbc, 0x4a7cb4, 0x4a7cac};
    uint8_t *frame = CHARANIM(c, f);
    uint8_t name[0x104];
    int32_t i;

    if (c > 6 || f > 0x15)
        return 0;
    CALL4(0x4979f0, PTRARG(name), 0x4a7c98, names[c], f + 1);
    if (CALL3(0x477530, PTRARG(name), PTRARG(frame), 1) == 0)
        return 0;
    for (i = 0; i < 0x400; i += 4) {
        uint8_t *e = PTR(frame, 0x10) + i;
        if (e[2] <= 8 && e[1] <= 8 && e[0] <= 8) {
            e[2] = 4;
            PTR(frame, 0x10)[i + 1] = 4;
            PTR(frame, 0x10)[i] = 4;
        }
    }
    GI32(0x4b6600) = S16(frame, 2);
    GI32(0x4b6604) = S16(frame, 6);
    return 1;
}

/* 0x4116f0: creates the animation texture (0x428c80, 256 x 0x8c, handle
 * 0x4b7da8) and loads every frame of the 7 characters. Returns 1. */
int32_t GAMEAPI charanim_load_all(void)
{
    uint32_t handle = (uint32_t)CALL0(0x428c80);
    int32_t c, f;

    G32(0x4b7da8) = handle;
    ((na_fn6)0x428c90)(handle, 0x100, 0x8c, 0, 0, 0);
    for (c = 0; c < 7; c++)
        for (f = 0; f < 0x16; f++)
            charanim_load((uint32_t)c, (uint32_t)f);
    return 1;
}

/* 0x411740: the animation texture's handle, origin and far corner (width
 * - 1, (height + 0xff) << 8, both) as words. */
void GAMEAPI charanim_texture_info(uint16_t *handle, uint16_t *origin, int16_t *u, int16_t *v, uint16_t *uv)
{
    *handle = G16(0x4b7da8);
    *origin = 0;
    *u = (int16_t)(GI32(0x4b6600) - 1);
    *v = (int16_t)((GI32(0x4b6604) + 0xff) << 8);
    *uv = (uint16_t)(((GI32(0x4b6604) + 0xff) << 8) | (GI32(0x4b6600) - 1));
}

/* ---- polygon clipping ----
 * Polygons are {count, vertex pointers...}; vertices 0x30 bytes of floats
 * (+4 depth, +0x10 / +0x14 screen, +0x18 / +0x1c texture, +0x20..+0x2c
 * colour). New vertices come from the pool 0x6cce98 (index *next). Inside
 * tests compare the floats' bit patterns as signed integers, as the
 * original does. Each returns (and stores in out[0]) the output count. */

#define CLIP_POOL(i) ((uint8_t *)0x6cce98 + (i)*0x30)

static uint32_t plane_distance(const uint8_t *v, const float *n)
{
    return fbits(((GF(0x4b8000) + F32(v, 0x10) * n[0]) + F32(v, 0x14) * n[1]) + F32(v, 4) * n[2]);
}

/* The first five attributes along the edge cur -> nxt, then 0x20..0x28;
 * stored in the original's order. */
static void lerp_position(uint8_t *o, const uint8_t *nxt, const uint8_t *cur, float t)
{
    float a10 = (F32(nxt, 0x10) - F32(cur, 0x10)) * t + F32(cur, 0x10);
    float a14 = (F32(nxt, 0x14) - F32(cur, 0x14)) * t + F32(cur, 0x14);
    float a04 = (F32(nxt, 0x04) - F32(cur, 0x04)) * t + F32(cur, 0x04);
    float a18 = (F32(nxt, 0x18) - F32(cur, 0x18)) * t + F32(cur, 0x18);
    float a1c = (F32(nxt, 0x1c) - F32(cur, 0x1c)) * t + F32(cur, 0x1c);
    F32(o, 0x1c) = a1c;
    F32(o, 0x10) = a10;
    F32(o, 0x18) = a18;
    F32(o, 0x04) = a04;
    F32(o, 0x14) = a14;
}

static void lerp_colour(uint8_t *o, const uint8_t *nxt, const uint8_t *cur, float t)
{
    float a20 = (F32(nxt, 0x20) - F32(cur, 0x20)) * t + F32(cur, 0x20);
    float a24 = (F32(nxt, 0x24) - F32(cur, 0x24)) * t + F32(cur, 0x24);
    float a28 = (F32(nxt, 0x28) - F32(cur, 0x28)) * t + F32(cur, 0x28);
    F32(o, 0x28) = a28;
    F32(o, 0x20) = a20;
    F32(o, 0x24) = a24;
    F32(o, 0x2c) = (F32(nxt, 0x2c) - F32(cur, 0x2c)) * t + F32(cur, 0x2c);
}

static void copy_colour(uint8_t *o, const uint8_t *cur)
{
    U32(o, 0x20) = U32(cur, 0x20);
    U32(o, 0x24) = U32(cur, 0x24);
    U32(o, 0x28) = U32(cur, 0x28);
    U32(o, 0x2c) = U32(cur, 0x2c);
}

/* The plane clip of 0x417060 / 0x417270: inside where the distance
 * (0x4b8000 + n . (x, y, z), n = plane[1..3]) >= plane[0]. */
static int32_t clip_by_plane(const uint32_t *in, const float *plane, uint32_t *out, int32_t *next, int flat)
{
    const uint32_t *list = in + 1, *e = in + 1;
    uint32_t *o = out + 1;
    uint8_t *pool = CLIP_POOL(*next);
    int32_t count = *next, n;
    int32_t limit = (int32_t)fbits(plane[0]);
    uint32_t dprev = plane_distance((const uint8_t *)(uintptr_t)list[0], plane + 1);
    int inside = (int32_t)dprev >= limit;

    for (n = (int32_t)in[0]; n > 0; n--, e++) {
        const uint8_t *nxt = (const uint8_t *)(uintptr_t)(n == 1 ? list[0] : e[1]);
        uint32_t d;
        int now;
        if (inside)
            *o++ = *e;
        d = plane_distance(nxt, plane + 1);
        now = (int32_t)d >= limit;
        if (inside != now) {
            const uint8_t *cur = (const uint8_t *)(uintptr_t)*e;
            float t = bitsf(fbits((plane[0] - bitsf(dprev)) / (bitsf(d) - bitsf(dprev))));
            *o++ = PTRARG(pool);
            lerp_position(pool, nxt, cur, t);
            if (flat)
                copy_colour(pool, cur);
            else
                lerp_colour(pool, nxt, cur, t);
            pool += 0x30;
            count++;
        }
        inside = now;
        dprev = d;
    }
    *next = count;
    out[0] = (uint32_t)((int32_t)((uint8_t *)o - (uint8_t *)out - 4) >> 2);
    return (int32_t)out[0];
}

/* 0x417060: clip against a plane, every attribute interpolated. */
int32_t GAMEAPI clip_plane(const uint32_t *in, const float *plane, uint32_t *out, int32_t *next)
{
    return clip_by_plane(in, plane, out, next, 0);
}

/* 0x417270: the same, new vertices taking the colour of the edge's start. */
int32_t GAMEAPI clip_plane_flat(const uint32_t *in, const float *plane, uint32_t *out, int32_t *next)
{
    return clip_by_plane(in, plane, out, next, 1);
}

/* The depth clips of 0x417450 / 0x417600: inside where depth (+4) >= limit
 * (near) or < limit (far). */
static int32_t clip_by_depth(const uint32_t *in, uint32_t *out, int32_t *next, uint32_t limit_bits, int far)
{
    const uint32_t *list = in + 1, *e = in + 1;
    uint32_t *o = out + 1;
    uint8_t *pool = CLIP_POOL(*next);
    int32_t count = *next, n;
    int32_t limit = (int32_t)limit_bits;
    uint32_t dprev = U32((const uint8_t *)(uintptr_t)list[0], 4);
    int inside = far ? (int32_t)dprev < limit : (int32_t)dprev >= limit;

    for (n = (int32_t)in[0]; n > 0; n--, e++) {
        const uint8_t *nxt = (const uint8_t *)(uintptr_t)(n == 1 ? list[0] : e[1]);
        uint32_t d = U32(nxt, 4);
        int now = far ? (int32_t)d < limit : (int32_t)d >= limit;
        if (inside)
            *o++ = *e;
        if (inside != now) {
            const uint8_t *cur = (const uint8_t *)(uintptr_t)*e;
            float t = bitsf(fbits((bitsf(limit_bits) - bitsf(dprev)) / (bitsf(d) - bitsf(dprev))));
            *o++ = PTRARG(pool);
            lerp_position(pool, nxt, cur, t);
            lerp_colour(pool, nxt, cur, t);
            pool += 0x30;
            count++;
        }
        inside = now;
        dprev = d;
    }
    *next = count;
    out[0] = (uint32_t)((int32_t)((uint8_t *)o - (uint8_t *)out - 4) >> 2);
    return (int32_t)out[0];
}

/* 0x417450: clip to depth >= the near limit 0x6ccd04. */
int32_t GAMEAPI clip_near(const uint32_t *in, uint32_t *out, int32_t *next)
{
    return clip_by_depth(in, out, next, G32(0x6ccd04), 0);
}

/* 0x417600: clip to depth < the far limit 0x6ccd08. */
int32_t GAMEAPI clip_far(const uint32_t *in, uint32_t *out, int32_t *next)
{
    return clip_by_depth(in, out, next, G32(0x6ccd08), 1);
}

/* 0x4177b0: the near clip again, attributes interpolated one at a time
 * (0x10, 0x14, 4, 0x18, 0x1c) and the colour copied from the edge's
 * start; out[0] counted in place; with no input vertices *next is kept. */
int32_t GAMEAPI clip_near_list(const uint32_t *in, uint32_t *out, int32_t *next)
{
    const uint32_t *list = in + 1, *e = in + 1;
    uint32_t limit_bits = G32(0x6ccd04);
    int32_t limit = (int32_t)limit_bits, count = *next, n;
    uint32_t dprev;
    uint8_t *pool;
    int inside;

    out[0] = 0;
    n = (int32_t)in[0];
    dprev = U32((const uint8_t *)(uintptr_t)list[0], 4);
    inside = (int32_t)dprev >= limit;
    if (n <= 0) {
        *next = count;
        return (int32_t)out[0];
    }
    pool = CLIP_POOL(count);
    for (; n > 0; n--, e++) {
        const uint8_t *nxt = (const uint8_t *)(uintptr_t)(n == 1 ? list[0] : e[1]);
        uint32_t d;
        int now;
        if (inside) {
            out[out[0] + 1] = *e;
            out[0]++;
        }
        d = U32(nxt, 4);
        now = (int32_t)d >= limit;
        if (inside != now) {
            const uint8_t *cur;
            float t = bitsf(fbits((bitsf(limit_bits) - bitsf(dprev)) / (bitsf(d) - bitsf(dprev))));
            out[out[0] + 1] = PTRARG(pool);
            out[0]++;
            dprev = d;
            inside = now;
            cur = (const uint8_t *)(uintptr_t)*e;
            F32(pool, 0x10) = (F32(nxt, 0x10) - F32(cur, 0x10)) * t + F32(cur, 0x10);
            F32(pool, 0x14) = (F32(nxt, 0x14) - F32(cur, 0x14)) * t + F32(cur, 0x14);
            F32(pool, 0x04) = (F32(nxt, 0x04) - F32(cur, 0x04)) * t + F32(cur, 0x04);
            F32(pool, 0x18) = (F32(nxt, 0x18) - F32(cur, 0x18)) * t + F32(cur, 0x18);
            F32(pool, 0x1c) = (F32(nxt, 0x1c) - F32(cur, 0x1c)) * t + F32(cur, 0x1c);
            copy_colour(pool, cur);
            count++;
            pool += 0x30;
        } else {
            dprev = d;
            inside = now;
        }
    }
    *next = count;
    return (int32_t)out[0];
}
