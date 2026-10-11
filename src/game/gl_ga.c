/* Group GA: the chase/replay camera director (0x408360, once per viewport
 * per step from sim_step 0x444120), its offset placement helper (0x40a720)
 * and the analogue controls response curve (0x428e90).
 *
 * These were left original for their x87 code. The floating point is
 * small: the int16 GTE matrix's float copy (0x6ccd1c, nine fild / fmul by
 * 1/4096), __ftol of the zoom float 0x6cce6c, and the curve table built
 * with the C runtime's pow. Each is done in inline x87 with the original's
 * instructions and operand widths (temporaries stored and reloaded as the
 * original does); the C runtime is called from asm. Everything else is
 * integer logic. Every callee is called by address.
 *
 * Viewport record (0x6cf6a0, 0x150 bytes each; see post_i.c): +0 shot kind,
 * +4 a fixed camera position (kind 0xf), +0x10/+0x14/+0x18 the kind 0xf
 * offset, +0x1c/+0x30 the chase flag and car, +0x20 a per-shot counter,
 * +0x24 zoom, +0x28 (kind 0xf zoom source), +0x2c/+0x2e orbit angle and
 * step, +0x32 time in the shot, +0x34 its length, +0x36 the car followed,
 * +0x38/+0x3c the event cut-away (index, kind), +0x3a the start delay,
 * +0x40 the camera body (0xd8 bytes; +0x34 its velocity, +0x90 its
 * matrix), +0x118/+0x11c tick stamps, +0x140 the kind 0xf node,
 * +0x148/+0x14c its flag and fade.
 *
 * Per viewport arrays (12 bytes each): 0x4b6080 the camera position,
 * 0x4b6050 its step (kind 0x10), 0x4b60b0 the up vector, 0x4b60f0 the
 * eased look-at point, 0x4b6130 the look direction, 0x4b6248 / 0x4b65c0 /
 * 0x4b6550 the kind 0xe start, eased point and eased up, 0x4b6580 the kind
 * 0xf anchor; 0x4b6298 (0x3c each) the kind 0xe Bezier points. */
#include "../runtime.h"
#include "body.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(p) + (off)))
#define PTR(p, off) (*(uint8_t **)((uint8_t *)(p) + (off)))
#define G32(a) (*(uint32_t *)(a))
#define GI32(a) (*(int32_t *)(a))

#define g_race (*(uint8_t **)0x4b1960)
#define g_config (*(uint8_t **)0x4b2198)
#define g_camera_seed (*(uint32_t *)0x4b11d8)
#define g_spot_count (*(int32_t *)0x4b65f4)
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_sin ((const int16_t *)0x4b24f8)

#define VIEW(i) ((uint8_t *)0x6cf6a0 + (i) * 0x150)
#define SLOT(i) ((uint8_t *)0x6bfb80 + (uint32_t)(i) * 0x274)
#define SLOT_CAR(i) PTR(SLOT(i), 0)
#define SLOT_CAMERA(i) PTR(SLOT(i), 0x28)
#define VIEW_VEC(base, view) ((int32_t *)((base) + (view) * 12))
#define VIEW_INT(base, view) ((int32_t *)((base) + (view) * 4))

#define find_trackside_at ((int32_t(GAMEAPI *)(uint16_t *, uint8_t **, int32_t))0x40b310)
#define check_events_at ((void(GAMEAPI *)(int32_t))0x407fa0)
#define choose_shot_at ((void(GAMEAPI *)(const void *, int32_t))0x40a9b0)
#define isqrt_at ((int32_t(GAMEAPI *)(int32_t))0x40d9d0)
#define line_distance_at ((int32_t(GAMEAPI *)(const int32_t *, const int32_t *, const int32_t *, int32_t *))0x40b180)
#define upside_down_at ((int32_t(GAMEAPI *)(const uint8_t *))0x408240)
#define probe_at ((int32_t(GAMEAPI *)(const uint8_t *, const uint8_t *))0x40a8f0)
#define direction_fit_at ((int32_t(GAMEAPI *)(int32_t *))0x407e90)
#define look_or_keep_at ((void(GAMEAPI *)(const void *, const void *, void *, int32_t))0x40a940)
#define spot_nearest_at ((void(GAMEAPI *)(const int32_t *, int32_t, uint8_t **, int32_t))0x40a570)
#define zoom_for_at ((int32_t(GAMEAPI *)(const int32_t *, const int32_t *, uint32_t, int32_t))0x40add0)
#define speed_zoom_at ((void(GAMEAPI *)(uint8_t *, uint8_t *))0x40b000)
#define factorials_init_at ((void(GAMEAPI *)(void))0x40c940)
#define points_scale_at ((void(GAMEAPI *)(uint8_t *, int32_t))0x40cad0)
#define bezier_at ((void(GAMEAPI *)(const int32_t *, int32_t, int32_t *, int32_t))0x40c9e0)
#define gte_rotate_at ((void(GAMEAPI *)(void))0x428090)
#define normalise_at ((void(GAMEAPI *)(int32_t *))0x480c70)
#define length_at ((int32_t(GAMEAPI *)(const int32_t *))0x480cd0)
#define vec_div_at ((void(GAMEAPI *)(int32_t *, int32_t))0x486e90)
#define side_axis_at ((void(GAMEAPI *)(int32_t *, const uint8_t *, int32_t))0x4081f0)
#define matrix_look_at ((int32_t(GAMEAPI *)(const int32_t *, const int32_t *, void *))0x407220)

#define CRT_FTOL 0x497a80
#define CRT_POW 0x497cc9

#define X87_CLOBBERS "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* __ftol of the zoom float 0x6cce6c (fld dword; call __ftol). */
static int32_t zoom_ftol(void)
{
    int32_t r;

    __asm__ volatile("flds %[z]\n\t"
                     "call *%[ftol]"
                     : "=a"(r)
                     : [z] "m"(*(const float *)0x6cce6c), [ftol] "S"(CRT_FTOL)
                     : "ecx", "edx", "memory", "cc", X87_CLOBBERS);
    return r;
}

/* The float copy (0x6ccd1c, rows 0x10 apart, * 1/4096 from 0x4a3310) of
 * the int16 GTE matrix at 0x6ccbe0: fild word, through a float temporary,
 * fmul dword, fstp dword; nine times. */
static void gte_float_matrix(void)
{
    float tmp;

#define GTE_ELEMENT(src, dst)                                                                                      \
    "filds " #src "\n\t"                                                                                           \
    "fstps %[t]\n\t"                                                                                               \
    "flds %[t]\n\t"                                                                                                \
    "fmuls 0x4a3310\n\t"                                                                                           \
    "fstps " #dst "\n\t"
    __asm__ volatile(GTE_ELEMENT(0x6ccbe0, 0x6ccd1c) GTE_ELEMENT(0x6ccbe2, 0x6ccd20) GTE_ELEMENT(0x6ccbe4, 0x6ccd24)
                         GTE_ELEMENT(0x6ccbe6, 0x6ccd2c) GTE_ELEMENT(0x6ccbe8, 0x6ccd30) GTE_ELEMENT(0x6ccbea, 0x6ccd34)
                             GTE_ELEMENT(0x6ccbec, 0x6ccd3c) GTE_ELEMENT(0x6ccbee, 0x6ccd40)
                                 GTE_ELEMENT(0x6ccbf0, 0x6ccd44)
                     : [t] "=m"(tmp)
                     :
                     : "memory", X87_CLOBBERS);
#undef GTE_ELEMENT
}

/* The GTE matrix (int16, 0x6ccbe0) from a body's matrix (+0xc4, 5 dwords)
 * and its float copy. */
static void gte_matrix_from(const uint8_t *body)
{
    int i;

    for (i = 0; i < 5; i++)
        G32(0x6ccbe0 + i * 4) = U32(body, 0xc4 + i * 4);
    gte_float_matrix();
}

/* The GTE vector (0x6ccc64, three int16 and a pad word). The original
 * builds it in a stack slot whose pad word is never written: it stores
 * leftover stack (0x6ccc6a is verifier scratch); here 0. */
static void gte_vector(uint16_t x, uint16_t y, uint16_t z)
{
    G32(0x6ccc64) = (uint32_t)x | (uint32_t)y << 16;
    G32(0x6ccc68) = z; /* sic: pad word is stack leftover in the original */
}

#define GTE_RESULT(i) GI32(0x6cccc8 + (i) * 4)

static int32_t shl16(int32_t v)
{
    return (int32_t)((uint32_t)v << 16);
}

static void copy_body(uint8_t *cam, const uint8_t *from)
{
    memcpy(cam, from, 0x36 * 4);
}

static void copy3(int32_t *dst, const int32_t *src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

static uint32_t camera_random(void)
{
    g_camera_seed = g_camera_seed * 16807 & 0x7fffffff;
    return g_camera_seed;
}

/* The zoom for the chase-style shots: 0x138 with config +0x31 == 1, else
 * 0xd0. */
static int32_t shot_zoom(void)
{
    return g_config[0x31] == 1 ? 0x138 : 0xd0;
}

/* 0x40a720: place the camera body `out` at the car's position plus the
 * offset (ox, oy, oz) turned by the car's matrix (oy negated when `flip`),
 * looking along `dir` with the car's side axis (negated unless `flip`). */
void GAMEAPI camera_place_offset(uint8_t *car, int32_t ox, int32_t oy, int32_t oz, uint8_t *out, int32_t *dir,
                                 int32_t flip)
{
    int32_t axis[3];
    uint16_t y = (uint16_t)oy;

    side_axis_at(axis, car, flip);
    if (flip)
        y = (uint16_t)-oy;
    gte_matrix_from(car);
    gte_vector((uint16_t)ox, y, (uint16_t)oz);
    gte_rotate_at();
    copy_body(out, car + 0x34);
    I32(out, 0) += shl16(GTE_RESULT(0));
    I32(out, 4) += shl16(GTE_RESULT(1));
    I32(out, 8) += shl16(GTE_RESULT(2));
    matrix_look_at(dir, axis, out + 0x90);
}

/* The response curve table (0x4bdf04, 129 ints): pow(x * 0x4a3324, 2)
 * truncated, x from 25 by 0x4a3320. One entry, advancing x. */
static int32_t curve_entry(uint32_t *x_and_tmp)
{
    int32_t r;

    __asm__ volatile("flds (%[x])\n\t"
                     "fmuls 0x4a3324\n\t"
                     "pushl $0x40000000\n\t"
                     "pushl $0\n\t"
                     "subl $8, %%esp\n\t"
                     "fstps 4(%[x])\n\t"
                     "flds 4(%[x])\n\t"
                     "fstpl (%%esp)\n\t"
                     "call *%[pow]\n\t"
                     "addl $16, %%esp\n\t"
                     "call *%[ftol]\n\t"
                     "flds (%[x])\n\t"
                     "fadds 0x4a3320\n\t"
                     "fstps (%[x])"
                     : "=a"(r)
                     : [x] "D"(x_and_tmp), [pow] "b"(CRT_POW), [ftol] "S"(CRT_FTOL)
                     : "ecx", "edx", "memory", "cc", X87_CLOBBERS);
    return r;
}

/* 0x428e90: the analogue response curve: a stick value (0..255, 0x80
 * centre) mapped through the table 0x4bdf04 (built on first use, flag
 * 0x4be110) after a dead zone; with `digital` (low byte) the value itself
 * unless within 25 of 0. */
uint32_t GAMEAPI controls_curve(int32_t value, int32_t unused, int32_t digital)
{
    int16_t x = (int16_t)value;
    int32_t e, r;

    (void)unused;
    if (GI32(0x4be110) == 0) {
        uint32_t x_and_tmp[2] = {0x41c80000, 0}; /* 25.0f */
        int32_t *p;

        for (p = (int32_t *)0x4bdf04; p < (int32_t *)0x4be108; p++)
            *p = curve_entry(x_and_tmp);
        GI32(0x4bdf04) = 0;
        GI32(0x4be110) = 1;
    }
    if ((uint8_t)digital != 0) {
        if (abs32(x) < 25)
            return 0;
        return (uint16_t)value;
    }
    if (x < 0x80) {
        e = 0x80 - ((0x80 - x) * 300 >> 8);
        if (e <= 0)
            e = 0;
        r = 0x80 - GI32(0x4be104 - (int16_t)e * 4);
        if (r <= 0)
            r = 0;
        return (uint32_t)r;
    }
    e = ((x - 0x80) * 300 >> 8) + 0x80;
    if (e >= 0xff)
        e = 0xff;
    r = GI32(0x4bdd04 + (int16_t)e * 4) + 0x80;
    if (r >= 0xff)
        r = 0xff;
    return (uint32_t)r;
}

/* Kind 0x10's first step: fly from one end of the trackside spot's line to
 * the other (which end by bit 16 of the car's +0x654) over a time set by
 * the length; the step is capped near sqrt(300) << 18 per axis. */
static void flyby_start(int32_t view, uint8_t *v, uint8_t *car, int32_t **at_out, int32_t **step_out)
{
    int32_t pace = I32(car, 0x654), far[3], length, sq, a, k;
    uint32_t bit;
    uint8_t *spot = PTR(VIEW_INT(0x4b65b0, view), 0);
    int32_t *at = VIEW_VEC(0x4b6080, view), *step = VIEW_VEC(0x4b6050, view);

    if (pace < 0x140000)
        pace = 0x140000;
    bit = (uint32_t)pace & 0x10000;
    if (bit) {
        copy3(at, (int32_t *)(spot + 0x10));
        copy3(far, (int32_t *)(spot + 4));
        U16(v, 0x34) = (uint16_t)(I32(spot, 0x34) / pace);
    } else {
        copy3(at, (int32_t *)(spot + 4));
        copy3(far, (int32_t *)(spot + 0x10));
        U16(v, 0x34) = (uint16_t)((I32(spot, 0x34) / pace) * 220 >> 8);
    }
    length = S16(v, 0x34);
    step[0] = (far[0] - at[0]) / length;
    step[1] = (far[1] - at[1]) / length;
    step[2] = (far[2] - at[2]) / length;
    sq = (step[0] >> 18) * (step[0] >> 18) + (step[1] >> 18) * (step[1] >> 18) + (step[2] >> 18) * (step[2] >> 18);
    a = sq < 0 ? -sq : sq;
    if (a > 300) {
        int32_t root = isqrt_at(a);

        k = (int32_t)((uint32_t)isqrt_at(300) << 8) / root;
        step[0] = (step[0] >> 8) * k;
        step[1] = (step[1] >> 8) * k;
        step[2] = (step[2] >> 8) * k;
    }
    if (bit) {
        int32_t l = S16(v, 0x34), lead = l * 70 >> 8;

        U16(v, 0x34) = (uint16_t)(l * 140 >> 8);
        at[0] += step[0] * lead;
        at[1] += lead * step[1];
        at[2] += step[2] * lead;
    }
    *VIEW_INT(0x4b6040, view) = (pace & 0x30000) == 0;
    *at_out = at;
    *step_out = step;
}

/* Kind 0x10 once placed: look at the car, with the up vector leaning
 * towards the spot's far end if the flyby is flagged (0x4b6040). */
static void flyby_look(int32_t view, uint8_t *v, uint8_t *cam, const uint8_t *car)
{
    int32_t *dir = VIEW_VEC(0x4b6130, view), *up = VIEW_VEC(0x4b60b0, view);

    dir[0] = I32(car, 0x34) - I32(cam, 0);
    dir[1] = I32(car, 0x38) - I32(cam, 4);
    dir[2] = I32(car, 0x3c) - I32(cam, 8);
    direction_fit_at(dir);
    if (*VIEW_INT(0x4b6040, view) != 0) {
        uint8_t *spot = PTR(VIEW_INT(0x4b65b0, view), 0);
        int32_t d0 = dir[0], d1 = dir[1];
        int32_t ax = I32(spot, 0x10) - d0, cy = I32(spot, 0x14) - d1, cz = I32(spot, 0x18) - dir[2];
        int32_t t[3];

        if (ax >= 0x1000000)
            ax = 0x1000000;
        else if (ax <= -0x1000000)
            ax = -0x1000000;
        if (cz >= 0x1000000)
            cz = 0x1000000;
        else if (cz <= -0x1000000)
            cz = -0x1000000;
        t[0] = d0 - ax;
        t[1] = d1 - cy;
        t[2] = dir[2] - cz;
        direction_fit_at(t);
        normalise_at(t);
        t[0] >>= 2;
        t[2] >>= 2;
        if (S16(v, 0x32) == 0) {
            up[0] = t[0];
            up[1] = (int32_t)0xffff0000;
            up[2] = t[2];
        } else {
            up[0] = up[0] * 15 + t[0];
            up[1] = up[1] * 15 - 0x10000;
            up[2] = up[2] * 15 + t[2];
            normalise_at(up);
        }
    } else {
        copy3(up, (const int32_t *)0x4a7808);
    }
    look_or_keep_at(dir, up, cam + 0x90, view);
    G32(v + 0x11c) = g_race_ticks;
}

/* Kinds 0xb..0xd: from the trackside spot nearest the event object, look
 * at the object, easing by 1/2 (config +0x31 == 1, zoom 0x138) or 1/4
 * (zoom 0x7b). */
static void event_shot(int32_t view, uint8_t *v, uint8_t *cam, const uint8_t *car, const uint8_t *object)
{
    const int32_t *at = (const int32_t *)(object + 0x34);
    int32_t *pos = VIEW_VEC(0x4b6080, view), *look = VIEW_VEC(0x4b60f0, view), *dir = VIEW_VEC(0x4b6130, view);
    uint8_t *spot;
    int32_t shift;

    spot_nearest_at(at, -1, &spot, 0);
    copy3(pos, (int32_t *)(spot + 4));
    if (g_config[0x31] == 1) {
        I32(v, 0x24) = 0x138;
        shift = 1;
    } else {
        I32(v, 0x24) = 0x7b;
        shift = 2;
    }
    copy_body(cam, car + 0x34);
    copy3((int32_t *)cam, pos);
    I32(cam, 0x3c) = 0;
    I32(cam, 0x38) = 0;
    I32(cam, 0x34) = 0;
    if (S16(v, 0x32) == 0) {
        copy3(look, at);
    } else {
        look[0] = ((at[0] - look[0]) >> shift) + look[0];
        look[1] = ((I32(object, 0x38) - look[1]) >> shift) + look[1];
        look[2] = ((I32(object, 0x3c) - look[2]) >> shift) + look[2];
    }
    dir[0] = look[0] - I32(cam, 0);
    dir[1] = look[1] - I32(cam, 4);
    dir[2] = look[2] - I32(cam, 8);
    direction_fit_at(dir);
    look_or_keep_at(dir, (const void *)0x4a7808, cam + 0x90, view);
    U16(v, 0x32) = 0;
}

/* Direction from the camera body to a point, fitted. */
static int32_t *aim_at(int32_t view, const uint8_t *cam, int32_t x, int32_t y, int32_t z)
{
    int32_t *dir = VIEW_VEC(0x4b6130, view);

    dir[0] = x - I32(cam, 0);
    dir[1] = y - I32(cam, 4);
    dir[2] = z - I32(cam, 8);
    direction_fit_at(dir);
    return dir;
}

/* Kind 0xe's first step: the replay sweep's car, its Bezier control points
 * (0x4a7818, scaled for the screen mode by view 0) turned by its matrix. */
static void sweep_start(int32_t view, uint8_t **camera, uint8_t **car)
{
    uint16_t index = (uint16_t)G32(0x6c0a40 + view * 4);
    uint8_t *c;
    int32_t *start = VIEW_VEC(0x4b6248, view), *eased = VIEW_VEC(0x4b65c0, view), *up = VIEW_VEC(0x4b6550, view);
    int32_t *point = (int32_t *)(0x4b6298 + view * 0x3c);
    const uint32_t *control;

    U16(VIEW(view), 0x36) = index;
    *camera = SLOT_CAMERA(index);
    c = *car = SLOT_CAR(index);
    if (view == 0) {
        factorials_init_at();
        points_scale_at((uint8_t *)0x4a7818, 5);
    }
    copy3(start, (int32_t *)(c + 0x34));
    copy3(eased, (int32_t *)(c + 0x34));
    up[0] = 0;
    up[1] = (int32_t)0xffff0000;
    up[2] = 0;
    gte_matrix_from(c);
    for (control = (const uint32_t *)0x4a781c; (uintptr_t)control < 0x4a7844; control += 2, point += 3) {
        G32(0x6ccc64) = control[-1];
        G32(0x6ccc68) = control[0];
        gte_rotate_at();
        point[0] = GTE_RESULT(0);
        point[1] = GTE_RESULT(1);
        point[2] = GTE_RESULT(2);
    }
    *VIEW_INT(0x4b6120, view) = 0;
    *VIEW_INT(0x4b6540, view) = 0;
    *VIEW_INT(0x4b6010, view) = 0x1b4;
}

/* Kind 0xe each step: the camera rides the Bezier curve from where the
 * car was (after ten steps grounded the end points follow the car and its
 * camera's offset), looking at the car tilted by the eased up vector. */
static void sweep_step(int32_t view, uint8_t *v, uint8_t *cam, uint8_t *camera, uint8_t *car, int32_t zoom)
{
    int32_t *start = VIEW_VEC(0x4b6248, view), *eased = VIEW_VEC(0x4b65c0, view), *up = VIEW_VEC(0x4b6550, view);
    int32_t *grounded = VIEW_INT(0x4b6120, view), *t = VIEW_INT(0x4b6540, view);
    int32_t offset[3], *dir, length, shift, next;

    I32(v, 0x24) = zoom;
    copy_body(cam, PTR(camera, 0) + 0x34);
    if (S16(camera, 0x3c) == 4)
        ++*grounded;
    if (*grounded > 10) {
        int32_t tx = I32(car, 0x34) + I32(camera, 0x2c), ty = I32(car, 0x38) + I32(camera, 0x30);
        int32_t tz = I32(car, 0x3c) + I32(camera, 0x34), dx, dy, dz;
        uint8_t *p = (uint8_t *)(0x4b6298 + view * 0x3c);

        eased[0] = ((tx - eased[0]) >> 4) + eased[0];
        eased[1] = ((ty - eased[1]) >> 4) + eased[1];
        eased[2] = ((tz - eased[2]) >> 4) + eased[2];
        dx = (eased[0] - start[0]) >> 16;
        dy = (eased[1] - start[1]) >> 16;
        dz = (eased[2] - start[2]) >> 16;
        I32(p, 0x24) = dx;
        I32(p, 0x28) = dy;
        I32(p, 0x2c) = dz;
        I32(p, 0x30) = dx;
        I32(p, 0x34) = dy;
        I32(p, 0x38) = dz;
        up[0] = up[0] * 15 + I32(camera, 0x14);
        up[1] = up[1] * 15 + I32(camera, 0x18);
        up[2] = up[2] * 15 + I32(camera, 0x1c);
        normalise_at(up);
    }
    bezier_at((const int32_t *)(0x4b6298 + view * 0x3c), 5, offset, *t);
    I32(cam, 4) = start[1] + offset[1];
    I32(cam, 8) = start[2] + offset[2];
    I32(cam, 0) = start[0] + offset[0];
    dir = aim_at(view, cam, I32(car, 0x34), I32(car, 0x38), I32(car, 0x3c));
    length = length_at(dir);
    if (length >= 0x100)
        vec_div_at(dir, length);
    shift = g_race[0x552] != 0 ? 3 : 2;
    dir[0] += up[0] >> shift;
    dir[1] += up[1] >> shift;
    dir[2] += up[2] >> shift;
    look_or_keep_at(dir, up, cam + 0x90, view);
    speed_zoom_at(SLOT(U16(v, 0x36)), cam);
    next = *t + *VIEW_INT(0x4b6010, view);
    *t = next;
    if (next > 0x10000)
        *t = 0x10000;
}

/* Kind 0xf: a fixed camera (+4) watching a point offset (+0x10..+0x18,
 * turned by the node's matrix) from the node's anchor; +0x14c fades in
 * over 20 steps and out over the last 50..20. */
static void node_shot(int32_t view, uint8_t *v, uint8_t *cam, const uint8_t *car)
{
    uint8_t *node = PTR(v, 0x140);
    int32_t t, rx, ry, rz, *anchor;

    if (S16(v, 0x32) == 0) {
        int32_t sum = 0;

        while (PTR(node, 0x1b0))
            node = PTR(node, 0x1b0);
        PTR(v, 0x140) = node;
        do {
            uint8_t *p = PTR(node, 0x10);

            if (p)
                sum += U16(p, 0x22);
            node = PTR(node, 0x19c);
        } while (node);
        if (sum > 600)
            sum = 600;
        I32(v, 0x14) = -(sum >> 1);
        I32(v, 0x18) = 0;
        I32(v, 0x10) = 0;
        node = PTR(v, 0x140);
    }
    I32(v, 0x148) = 1;
    t = S16(v, 0x32);
    if (t < 0x14) {
        I32(v, 0x14c) = t * 500;
    } else {
        int32_t l = S16(v, 0x34);

        if (t < l - 0x32)
            I32(v, 0x14c) = 10000;
        else if (t > l - 0x14)
            I32(v, 0x14c) = 0;
        else
            I32(v, 0x14c) = (l - t - 0x14) * 333;
    }
    G32(v + 0x24) = (uint32_t)(I32(v, 0x28) * 104) >> 8;
    gte_matrix_from(node);
    gte_vector(U16(v, 0x10), U16(v, 0x14), U16(v, 0x18));
    gte_rotate_at();
    rx = shl16(GTE_RESULT(0));
    ry = shl16(GTE_RESULT(1));
    rz = shl16(GTE_RESULT(2));
    anchor = VIEW_VEC(0x4b6580, view);
    if (S16(v, 0x32) < 0x1e)
        copy3(anchor, (int32_t *)(node + 0x34));
    ry += anchor[1];
    rx += anchor[0];
    rz += anchor[2];
    copy_body(cam, car + 0x34);
    copy3((int32_t *)cam, (int32_t *)(v + 4));
    I32(cam, 0x3c) = 0;
    I32(cam, 0x38) = 0;
    I32(cam, 0x34) = 0;
    look_or_keep_at(aim_at(view, cam, rx, ry, rz), (const void *)0x4a7808, cam + 0x90, view);
}

/* Kinds 4 and 5: circle the car (angle +0x2c by +0x2e a step, radius
 * 0x1e0 table units) above or below it; kind 5 (refused when the car
 * is upside down) climbs instead. Returns whether the camera was placed
 * and fits (0x40a8f0). */
static int32_t orbit_shot(int32_t view, uint8_t *v, uint8_t *cam, uint8_t *car, int kind5)
{
    int32_t a = S16(v, 0x2c), s = g_sin[a & 0xff], c = g_sin[(a + 0x40) & 0xff];

    if (S16(v, 0x32) == 0)
        U16(v, 0x2e) = (camera_random() & 0x10000) ? (kind5 ? 2 : 1) : (kind5 ? 0xfffe : 0xffff);
    if (kind5) {
        if (upside_down_at(car))
            return 0;
        if (S16(v, 0x32) == 0)
            I32(v, 0x20) = -200;
        copy_body(cam, car + 0x34);
        I32(cam, 0) += s * 0x1e0;
        I32(cam, 4) += shl16(I32(v, 0x20));
        I32(cam, 8) += c * 0x1e0;
    } else {
        copy_body(cam, car + 0x34);
        I32(cam, 0) += s * 0x1e0;
        I32(cam, 8) += c * 0x1e0;
        if (upside_down_at(car))
            I32(cam, 4) += 0x260000;
        else
            I32(cam, 4) -= 0x260000;
    }
    look_or_keep_at(aim_at(view, cam, I32(car, 0x34), I32(car, 0x38), I32(car, 0x3c)), (const void *)0x4a7808,
                    cam + 0x90, view);
    U16(v, 0x2c) += U16(v, 0x2e);
    if (kind5)
        I32(v, 0x20) += 2;
    return probe_at(PTR(G32(0x6bfba8), 0), cam) != 0;
}

/* Kinds 2 and 3: looking straight down from above the car (refused, -1,
 * when it is upside down), half the time dropping from 0x780000 higher.
 * Returns whether the camera was placed and fits (0x40a8f0). */
static int32_t overhead_shot(int32_t view, uint8_t *v, uint8_t *cam, uint8_t *car, int kind3)
{
    int32_t *dir = VIEW_VEC(0x4b6130, view), *up = VIEW_VEC(0x4b60b0, view), drop;

    if (S16(v, 0x32) == 0) {
        uint32_t bit = camera_random() >> 16 & 1;

        I32(v, 0x20) = (int32_t)bit;
        if (bit)
            I32(v, 0x20) = 0x780000;
    }
    if (upside_down_at(car))
        return 0;
    copy_body(cam, car + 0x34);
    drop = I32(v, 0x20);
    if (drop) {
        drop -= 45000;
        I32(v, 0x20) = drop;
        I32(cam, 4) -= drop;
    } else {
        I32(cam, 4) += (int32_t)0xff880000;
    }
    dir[0] = 0;
    dir[1] = 0x1000;
    dir[2] = 0;
    if (kind3) {
        up[0] = S16(car, 0xc8);
        up[1] = 0;
        up[2] = S16(car, 0xd4);
    } else {
        up[0] = 0x1000;
        up[1] = 0;
        up[2] = 0x1000;
    }
    look_or_keep_at(dir, up, cam + 0x90, view);
    return probe_at(PTR(G32(0x6bfba8), 0), cam) != 0;
}

/* 0x408360: the chase/replay camera director, once per viewport per step.
 * Picks up trackside flybys (0x40b310) and event cut-aways, places the
 * viewport's camera body (+0x40) for the current shot kind, and when a shot
 * can't be placed picks another (0x40a9b0) and tries again (after five
 * tries, kind 9: the car's own chase camera). Ends the shot when its time
 * passes its length. */
void GAMEAPI camera_director(int32_t view)
{
    uint8_t *v = VIEW(view);
    uint8_t *cam = v + 0x40;
    int32_t event = I32(v, 0x3c); /* read once, before the event checks */
    int32_t zoom = zoom_ftol();
    uint32_t tries = 0;
    int32_t *pos = VIEW_VEC(0x4b6080, view);

    if (find_trackside_at((uint16_t *)(v + 0x36), (uint8_t **)VIEW_INT(0x4b65b0, view), view) != 0) {
        I32(v, 0) = 0x10;
        U16(v, 0x32) = 0;
    }
    I32(v, 0x24) = 0x68;
    I32(v, 0x1c) = 0;
    U16(v, 0x30) = 0;
    I32(v, 0x148) = 0;
    check_events_at(view);

    for (;;) {
        uint16_t *car_index = (uint16_t *)(v + 0x36);
        uint8_t *camera = SLOT_CAMERA(*car_index);
        uint8_t *car = SLOT_CAR(*car_index);
        uint32_t kind = U32(v, 0);
        int replaced = 0;

        if (kind == 0xb && event == 3) {
            uint8_t *object = *(uint8_t **)(0x6b9ce0 + S16(v, 0x38) * 4);
            uint8_t *owner = PTR(object, 0x190);

            if (I32(object, 0x188) == 0x10 && I32(v, 0x20) == 0)
                I32(v, 0x20) = 0x1e;
            if (I32(v, 0x20) == 1 || I32(v, 0x3c) == 0) {
                I32(v, 0) = 8;
                U16(v, 0x32) = 0;
                U16(v, 0x34) = 0x5a;
                *car_index = U16(owner, 0x64e);
                car = SLOT_CAR(*car_index);
                goto events_done;
            }
        }
        if (kind >= 0xb && kind <= 0xd) {
            int32_t e = I32(v, 0x3c);

            if (e == 4) {
                uint8_t *object = *(uint8_t **)(0x5d1fc0 + S16(v, 0x38) * 4);

                if (U8(object, 0x188) & 0x40) {
                    uint8_t *owner = PTR(object, 0x198);

                    I32(v, 0) = 9;
                    U16(v, 0x32) = 0;
                    U16(v, 0x34) = 0xd2;
                    *car_index = U16(owner, 0x64e);
                    car = SLOT_CAR(*car_index);
                }
            } else if (e == 0) {
                choose_shot_at(pos, view);
                car = SLOT_CAR(*car_index);
            } else {
                G32(v + 0x118) = g_race_ticks;
            }
        }
    events_done:
        if (I32(g_race, 0x57c) == 6 && S16(v, 0x3a) == -1) {
            U16(v, 0x3a) = 300;
            I32(v, 0) = 4;
            U16(v, 0x32) = 0;
            U16(v, 0x34) = 0x78;
            *car_index = (uint16_t)G32(0x6c0a40 + view * 4);
            car = SLOT_CAR(*car_index);
        }

        switch (U32(v, 0)) {
        case 0:
        case 9:
            copy_body(cam, PTR(camera, 0) + 0x34);
            goto done;

        case 1:
        case 7: {
            int32_t *dir = VIEW_VEC(0x4b6130, view);
            uint8_t *followed;

            speed_zoom_at(SLOT(*car_index), cam);
            I32(v, 0x24) = zoom_ftol();
            I32(v, 0x1c) = 1;
            U16(v, 0x30) = *car_index;
            copy3((int32_t *)(cam + 0x34), (int32_t *)(car + 0x68));
            if (U32(v, 0) == 1) {
                dir[0] = -S16(car, 0xc8);
                dir[1] = -S16(car, 0xce);
                dir[2] = -S16(car, 0xd4);
            } else {
                dir[0] = S16(car, 0xc8);
                dir[1] = S16(car, 0xce);
                dir[2] = S16(car, 0xd4);
            }
            followed = SLOT_CAR(*car_index);
            camera_place_offset(car, 0, -10, 0x14, cam, dir, followed[0x63f] == 1);
            goto done;
        }

        case 2:
        case 3:
            if (overhead_shot(view, v, cam, car, U32(v, 0) == 3))
                goto done;
            choose_shot_at(pos, view);
            break;

        case 4:
        case 5:
            if (orbit_shot(view, v, cam, car, U32(v, 0) == 5))
                goto done;
            choose_shot_at(pos, view);
            break;

        case 6: {
            int32_t *up = VIEW_VEC(0x4b60b0, view), *look = VIEW_VEC(0x4b60f0, view);
            int32_t x, y, z;
            uint8_t *target = car;

            I32(v, 0x24) = 0xd0;
            copy_body(cam, car + 0x34);
            x = I32(car, 0x34);
            z = I32(car, 0x3c) - 0xa00000;
            up[2] = 0;
            up[0] = 0;
            up[1] = (int32_t)0xfffff000;
            if (upside_down_at(car))
                y = I32(car, 0x38) + 0x640000;
            else
                y = I32(car, 0x38) - 0x640000;
            if (S16(v, 0x32) == 0) {
                look[0] = x;
                look[1] = y;
                look[2] = z;
            } else {
                look[0] = ((x - look[0]) >> 3) + look[0];
                look[1] = ((y - look[1]) >> 3) + look[1];
                look[2] = ((z - look[2]) >> 3) + look[2];
            }
            copy3((int32_t *)cam, look);
            if (probe_at(PTR(G32(0x6bfba8), 0), cam) == 0) {
                choose_shot_at(pos, view);
                replaced = 1;
                target = SLOT_CAR(*car_index);
            }
            look_or_keep_at(aim_at(view, cam, I32(target, 0x34), I32(target, 0x38), I32(target, 0x3c)), up,
                            cam + 0x90, view);
            if (replaced != 1)
                goto done;
            break;
        }

        case 8:
            if (S16(v, 0x32) == 0) {
                int32_t ry;

                gte_matrix_from(car);
                gte_vector(0, 0, 0xffb0);
                gte_rotate_at();
                ry = GTE_RESULT(1);
                pos[0] = shl16(GTE_RESULT(0)) + I32(car, 0x34);
                pos[2] = shl16(GTE_RESULT(2)) + I32(car, 0x3c);
                if (upside_down_at(car))
                    pos[1] = shl16(ry + 0x28) + I32(car, 0x38);
                else
                    pos[1] = shl16(ry - 0x28) + I32(car, 0x38);
                copy_body(cam, car + 0x34);
                copy3((int32_t *)cam, pos);
                if (probe_at(PTR(G32(0x6bfba8), 0), cam) == 0) {
                    choose_shot_at(pos, view);
                    replaced = 1;
                    car = SLOT_CAR(*car_index);
                }
            }
            I32(v, 0x24) = 0xd0;
            copy_body(cam, car + 0x34);
            copy3((int32_t *)cam, pos);
            I32(cam, 0x3c) = 0;
            I32(cam, 0x38) = 0;
            I32(cam, 0x34) = 0;
            look_or_keep_at(aim_at(view, cam, I32(car, 0x34), I32(car, 0x38), I32(car, 0x3c)),
                            (const void *)0x4a7808, cam + 0x90, view);
            if (replaced != 1)
                goto done;
            break;

        case 10: {
            int32_t old[3], shift = 2, *look = VIEW_VEC(0x4b60f0, view);
            uint8_t *spot;

            copy3(old, pos);
            if (S16(v, 0x32) == 0)
                I32(v, 0x20) = (int32_t)(camera_random() >> 15 & 0xffff);
            spot_nearest_at((int32_t *)(car + 0x34), -1, &spot, 0);
            copy3(pos, (int32_t *)(spot + 4));
            if (g_spot_count > 0 && (pos[0] != old[0] || pos[1] != old[1] || pos[2] != old[2])) {
                int16_t length = S16(v, 0x34);

                if (S16(v, 0x32) > length - 0x1e)
                    U16(v, 0x32) = (uint16_t)(length - 0x1e);
            }
            if ((U8(v, 0x20) & 1) && g_config[0x31] == 1) {
                I32(v, 0x24) = zoom_for_at(old, (int32_t *)(car + 0x34), 0xfa, 3);
                shift = 1;
            } else {
                I32(v, 0x24) = shot_zoom();
            }
            copy_body(cam, car + 0x34);
            copy3((int32_t *)cam, pos);
            I32(cam, 0x3c) = 0;
            I32(cam, 0x38) = 0;
            I32(cam, 0x34) = 0;
            if (S16(v, 0x32) == 0) {
                copy3(look, (int32_t *)(car + 0x34));
            } else {
                look[0] = ((I32(car, 0x34) - look[0]) >> shift) + look[0];
                look[1] = ((I32(car, 0x38) - look[1]) >> shift) + look[1];
                look[2] = ((I32(car, 0x3c) - look[2]) >> shift) + look[2];
            }
            look_or_keep_at(aim_at(view, cam, look[0], look[1], look[2]), (const void *)0x4a7808, cam + 0x90,
                            view);
            goto done;
        }

        case 0xb: {
            uint8_t *object = *(uint8_t **)(0x6b9ce0 + S16(v, 0x38) * 4);

            event_shot(view, v, cam, car, object);
            if (I32(v, 0x20) > 0)
                I32(v, 0x20)--;
            goto done;
        }

        case 0xc:
            event_shot(view, v, cam, car, *(uint8_t **)(0x6b9cc0 + S16(v, 0x38) * 4));
            goto done;

        case 0xd:
            event_shot(view, v, cam, car, *(uint8_t **)(0x5d1fc0 + S16(v, 0x38) * 4));
            goto done;

        case 0xe:
            if (S16(v, 0x32) == 0)
                sweep_start(view, &camera, &car);
            sweep_step(view, v, cam, camera, car, zoom);
            goto done;

        case 0xf:
            node_shot(view, v, cam, car);
            goto done;

        case 0x10: {
            int32_t *at, *step, along;
            uint8_t *spot;

            if (S16(v, 0x32) == 0) {
                car = SLOT_CAR(*car_index);
                flyby_start(view, v, car, &at, &step);
            } else {
                at = pos;
                step = VIEW_VEC(0x4b6050, view);
                at[0] += step[0];
                at[1] += step[1];
                at[2] += step[2];
            }
            I32(v, 0x24) = shot_zoom();
            copy_body(cam, car + 0x34);
            copy3((int32_t *)cam, at);
            copy3((int32_t *)(cam + 0x34), step);
            spot = PTR(VIEW_INT(0x4b65b0, view), 0);
            if (line_distance_at((int32_t *)(car + 0x34), (int32_t *)(spot + 4), (int32_t *)(spot + 0x10), &along) <=
                0x3e80000) {
                flyby_look(view, v, cam, car);
                goto done;
            }
            choose_shot_at(at, view);
            break;
        }

        default:
            goto done;
        }

        /* the shot could not be placed: a new one was chosen; try again */
        if (tries++ > 3) {
            I32(v, 0) = 9;
            U16(v, 0x32) = 0;
            U16(v, 0x34) = 0x5a;
            U16(v, 0x36) = (uint16_t)view;
        }
    }
done:
    if (S16(v, 0x3a) > 0)
        U16(v, 0x3a)--;
    {
        int16_t t = S16(v, 0x32);

        U16(v, 0x32) = (uint16_t)(t + 1);
        if (t > S16(v, 0x34))
            choose_shot_at(pos, view);
    }
}
