/* Group PC: the original's still-reachable functions in 0x4231f0-0x42b9a0
 * (the view / GTE-emulation source file and its neighbours).
 *
 *  - the views: per-car on-screen flags (0x4231f0), the end-of-frame
 *    ordering-table flush (0x423c40), the view matrix and camera (0x423d10,
 *    0x423d70, 0x423df0), the zoom / level-of-detail set-up (0x423e10,
 *    0x423f00, 0x426260), the full-screen view (0x423f20), the viewport
 *    with its border (0x4284b0, 0x428770);
 *  - the GTE emulation's state: screen offset (0x426200 / 0x426240), the
 *    rotation matrix and translation (0x426330, 0x426480, 0x426530), the
 *    light matrix (0x428940), the colour of a lit point (0x4292b0);
 *  - the float transforms called through the pointers 0x6ccd78 / 0x6ccd7c /
 *    0x6ccd80 / 0x6cce3c (x87: 0x426620, 0x4266d0, 0x426cd0 + 0x426dc0,
 *    0x427400; 3DNow!: 0x4268c0, 0x427250), the integer light colour
 *    0x427100, the screen cross product 0x4278a0, the facing test 0x4278f0,
 *    the vertex projections 0x4288c0 / 0x428900;
 *  - 3x3 int16 matrix helpers (0x426e10, 0x426e90, 0x426f60, 0x428be0) and
 *    the x87 normalise 0x4290b0;
 *  - ordering tables (0x427a40, 0x427a70, 0x427ab0), the depth range and
 *    projection save / restore (0x4283d0, 0x428a80, 0x428b00, 0x428b90,
 *    0x428bd0), render flags 0x428a50, the clear colour 0x428390, the
 *    z-buffer restore 0x428270, the debug line print 0x428310;
 *  - thin wrappers and thunks (0x428240 / 50 / 60, 0x428c30 / 60 / 80 / 90,
 *    0x429340, 0x4290a0, the no-op 0x4291f0);
 *  - the smoke clouds' drawing (0x42b9a0).
 *
 * The functions that are all x87 / 3DNow! arithmetic, several of which hand
 * values to each other on the x87 stack (0x426cd0 leaves H / z in st(0) for
 * 0x426dc0; 0x4288c0 for 0x426b00; 0x428bd0 and 0x4278f0 return in st(0)),
 * are the original's instruction sequences in assembly (checked instruction
 * by instruction against the original), so they match under any control
 * word and keep their register / x87 stack conventions. Elsewhere float
 * steps are the original's x87 instructions in inline asm (memory operands,
 * register intermediates as the original); integer logic is C. Everything
 * outside the group is called by address. */
#include "../runtime.h"
#include "gamefn.h"

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

#define g_race G32(0x4b1960)
#define g_screen_width G32(0x4acc38)
#define g_screen_height G32(0x4acc3c)
#define g_render_flags G32(0x6cce40)

/* This group's functions (also in build/groups/pc_protos.txt for physics.h). */
void GAMEAPI cars_view_flags_update(void); /* 0x4231f0 */
void GAMEAPI ot_frame_flush(void); /* 0x423c40 */
void GAMEAPI view_matrix_store(const uint32_t *m); /* 0x423d10 */
void GAMEAPI view_camera_build(uint8_t *view, const uint32_t *matrix, const uint8_t *pos); /* 0x423d70 */
void GAMEAPI view_camera_load(const uint8_t *view); /* 0x423df0 */
void GAMEAPI view_lod_set(uint32_t zoom); /* 0x423e10 */
void GAMEAPI view_zoom_set(uint32_t zoom); /* 0x423f00 */
int32_t GAMEAPI view_full_screen(void); /* 0x423f20 */
void GAMEAPI gte_screen_offset_set(int32_t x, int32_t y); /* 0x426200 */
void GAMEAPI gte_screen_offset_get(int32_t *x, int32_t *y); /* 0x426240 */
void GAMEAPI projection_h_set(int32_t zoom); /* 0x426260 */
void GAMEAPI gte_matrix_load(const uint8_t *m); /* 0x426330 */
void GAMEAPI gte_matrix_save(uint8_t *out); /* 0x426480 */
void GAMEAPI gte_translation_load(const uint8_t *v); /* 0x426530 */
void GAMEAPI view_rect_saved_get(uint32_t *out); /* 0x426590 */
void GAMEAPI view_saved_get(uint32_t *out); /* 0x4265b0 */
void gte_rt_float(void); /* 0x426620: asm */
void gte_rtps_float(void); /* 0x4266d0: asm */
#if defined(__i386__)
void gte_rtps_3dnow(void); /* 0x4268c0: asm, 3DNow!, (record) through 0x6ccd78 / 0x6ccd7c */
void gte_rt_depth(void); /* 0x426cd0: asm, (record) returns z; leaves st(0) for gte_project_pending when z >= 0 */
void gte_project_pending(void); /* 0x426dc0: asm, takes st(0) from gte_rt_depth */
#else
/* x86-64: C bodies with the prototypes their callers use (nothing calls
 * them with registers / the x87 stack): what the original leaves in st(0)
 * is the result, or the argument of its consumer. */
float gte_rtps_3dnow(uint8_t *rec); /* 0x4268c0, through 0x6ccd78 / 0x6ccd7c */
int32_t gte_rt_depth_c(uint8_t *rec, float *st0); /* 0x426cd0: returns z; *st0 its st(0) when z >= 0 */
void gte_project_pending_c(float st0);              /* 0x426dc0 */
int32_t gte_rt_depth(uint8_t *rec, float *st0);   /* 0x426cd0's hook: gte_rt_depth_c */
void gte_project_pending(float st0);               /* 0x426dc0's hook: gte_project_pending_c */
#endif
void GAMEAPI matrix_rotate_vec(const uint8_t *m, const int16_t *v, int32_t *out); /* 0x426e10 */
void GAMEAPI matrix_mul_into(const uint8_t *a, uint8_t *b); /* 0x426e90 */
void GAMEAPI matrix_translation_set(uint8_t *m, const int32_t *v); /* 0x426f60 */
void GAMEAPI gte_light_colour(void); /* 0x427100 */
void gte_light_colour_3dnow(void); /* 0x427250: asm, 3DNow!, through 0x6cce3c */
void gte_light_colour_float(void); /* 0x427400: asm, through 0x6cce3c */
int32_t GAMEAPI gte_nclip(void); /* 0x4278a0 */
#if defined(__i386__)
void vec_facing_float(void); /* 0x4278f0: asm, (a, b, c) returns its float in st(0) */
#else
float vec_facing_float_c(const uint8_t *a, const uint8_t *b, const uint8_t *c); /* 0x4278f0 */
float vec_facing_float(const uint8_t *a, const uint8_t *b, const uint8_t *c);   /* its hook */
#endif
int32_t GAMEAPI ot_clear_forward(uint32_t *ot, uint32_t n); /* 0x427a40 */
int32_t GAMEAPI ot_clear_reverse(uint32_t *ot, uint32_t n); /* 0x427a70 */
uint32_t GAMEAPI ot_add_prim(uint32_t entry, uint32_t *prim); /* 0x427ab0 */
void GAMEAPI filter_set_thunk(void); /* 0x428240 */
void GAMEAPI fog_on_thunk(void); /* 0x428250 */
void GAMEAPI fog_off_thunk(void); /* 0x428260 */
int32_t GAMEAPI zbuffer_restore(void); /* 0x428270 */
int32_t GAMEAPI debug_line_print(char *s); /* 0x428310 */
int32_t GAMEAPI clear_colour_store(uint32_t r, uint32_t g, uint32_t b); /* 0x428390 */
int32_t GAMEAPI depth_range_set(int32_t near, int32_t far, uint32_t c); /* 0x4283d0 */
void GAMEAPI view_border_draw(const int16_t *r, const int16_t *in, uint32_t fill); /* 0x4284b0 */
void GAMEAPI view_viewport_apply(const uint8_t *view, int32_t border); /* 0x428770 */
#if defined(__i386__)
void vertex_project_codes(void); /* 0x4288c0: asm, (record); st(0) handed to 0x426b00 */
void vertex_project_float(void); /* 0x428900: asm, (record) */
#else
void vertex_project_codes(uint8_t *rec); /* 0x4288c0 */
void vertex_project_float(uint8_t *rec); /* 0x428900 */
#endif
void GAMEAPI light_matrix_load(const uint32_t *m); /* 0x428940 */
void GAMEAPI render_flags_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e); /* 0x428a50 */
void GAMEAPI projection_push(void); /* 0x428a80 */
void GAMEAPI projection_pop(void); /* 0x428b00 */
void GAMEAPI far_plane_set(uint32_t f); /* 0x428b90 */
#if defined(__i386__)
void far_plane_get(void); /* 0x428bd0: asm, returns the float in st(0) */
#else
float far_plane_get(void); /* 0x428bd0 */
#endif
void GAMEAPI matrix_transpose_s16(const uint8_t *src, int16_t *dst); /* 0x428be0 */
int32_t GAMEAPI texture_surface_lock(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f); /* 0x428c30 */
int32_t GAMEAPI texture_surface_unlock(uint32_t a, uint32_t b, uint32_t c); /* 0x428c60 */
int32_t GAMEAPI texture_handle_new(void); /* 0x428c80 */
int32_t GAMEAPI texture_surface_upload(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f); /* 0x428c90 */
void GAMEAPI dword_store(uint32_t *p, uint32_t v); /* 0x4290a0 */
#if defined(__i386__)
void vec_normalise_x87(void); /* 0x4290b0: asm, (in, out) returns 0 */
#else
int32_t vec_normalise_x87(const int32_t *in, int32_t *out); /* 0x4290b0 */
#endif
void GAMEAPI dx_error_ignore(void); /* 0x4291f0 */
void GAMEAPI light_colour_at(const uint8_t *pos, const uint32_t *rgb, uint32_t *out); /* 0x4292b0 */
int32_t GAMEAPI textures_unusable_thunk(void); /* 0x429340 */
void GAMEAPI smoke_clouds_draw(void); /* 0x42b9a0 */

#define draw_otag(ot) GCALL(0x428470, capture_draw_otag)((uint32_t)(ot))
#define draw_prim(p) GCALL(0x428460, capture_draw_prim)(A(p))
#define viewport_set(x, y, w, h) GCALL(0x41f2d0, viewport_body)((int32_t)(x), (int32_t)(y), (int32_t)(w), (int32_t)(h))
/* The frustum's arguments are floats (bit patterns here). */
#define projection_set(x, y, w, h, d) \
    GCALL(0x417ed0, frustum_body)(pc_float(x), pc_float(y), pc_float(w), pc_float(h), pc_float(d))

/* Every x87 register, for asm blocks that call out or use the FPU stack. */
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* ---- x87 helpers: the original's instructions ---- */

/* A float's bit pattern as a float. */
static inline float pc_float(uint32_t bits)
{
    float f;

    memcpy(&f, &bits, 4);
    return f;
}

#if !defined(__i386__)
/* x86-64: long double arithmetic is x87 arithmetic (rounded by the control
 * word, as the original's register intermediates). */
typedef long double pc_ld;

/* A float in game memory, widened (exact, as fld dword). */
#define PC_F(a) ((pc_ld)pc_float(G32(a)))
/* An int16 in game memory (fild word). */
#define PC_W(a) ((pc_ld)GS16(a))

/* fistp dword: rounded by the control word. */
static inline int32_t pc_fistp32(pc_ld v)
{
    int32_t r;
    __asm__ volatile("fistpl %0" : "=m"(r) : "t"(v) : "st");
    return r;
}

/* fistp word. */
static inline int16_t pc_fistp16(pc_ld v)
{
    int16_t r;
    __asm__ volatile("fistps %0" : "=m"(r) : "t"(v) : "st");
    return r;
}

/* fstp dword: rounded to a float by the control word. */
static inline uint32_t pc_fstp32(pc_ld v)
{
    uint32_t r;
    __asm__ volatile("fstps %0" : "=m"(r) : "t"(v) : "st");
    return r;
}

/* The C runtime's _ftol (0x497a80): st(0) truncated (control word's
 * rounding set to chop around a fistp qword), the low dword. */
static inline int32_t pc_ftol(pc_ld v)
{
    uint16_t cw, chop;
    int64_t r;

    __asm__ volatile("fnstcw %0" : "=m"(cw));
    chop = (uint16_t)(cw | 0xc00);
    __asm__ volatile("fldcw %1\n\tfistpll %0\n\tfldcw %2" : "=m"(r) : "m"(chop), "m"(cw), "t"(v) : "st");
    return (int32_t)r;
}
#endif

/* fild dword; fstp dword: an integer to float, rounded by the control word. */
static inline uint32_t x87_from_int(uint32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild word w; fmul dword k; fstp: w * k (the original's fstp / fld of the
 * integer as a float in between is exact for 16-bit values). */
static inline uint32_t x87_s16_mul(uint16_t w, uint32_t k)
{
    uint32_t r;
    __asm__ volatile("filds %1\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(w), "m"(k) : "st");
    return r;
}

/* fld a; fadd b; fstp. */
static inline uint32_t x87_add(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fdiv b; fmul c; fstp: a / b * c. */
static inline uint32_t x87_div_mul(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfmuls %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fmul b; fdiv c; fstp: a * b / c. */
static inline uint32_t x87_mul_div(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfdivs %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fistp dword: a rounded by the control word. */
static inline int32_t x87_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fld a; fcomp b; fnstsw ax: the status word's high byte (C0 = 1, C3 = 0x40). */
static inline uint32_t x87_cmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 0xff;
}

/* fld a; fadd / fsub t; fcomp b: the sum / difference kept in the register. */
static inline uint32_t x87_cmp_sum(uint32_t a, uint32_t t, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfcomps %3\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(t), "m"(b) : "st", "cc");
    return (sw >> 8) & 0xff;
}

static inline uint32_t x87_cmp_diff(uint32_t a, uint32_t t, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfcomps %3\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(t), "m"(b) : "st", "cc");
    return (sw >> 8) & 0xff;
}

#define BELOW(sw) ((sw) & 1)            /* less, or unordered */
#define ABOVE(sw) (((sw) & 0x41) == 0) /* greater (ordered) */

/* fld dword a; call _ftol (0x497a80, truncating): the low dword. */
static inline int32_t ftol_f(uint32_t a)
{
#if defined(__i386__)
    int32_t r;
    __asm__ volatile("flds %1\n\tmovl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(a)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
#else
    return pc_ftol((pc_ld)pc_float(a));
#endif
}

/* fld dword a; fmul qword k; call _ftol. */
static inline int32_t ftol_mul_d(uint32_t a, const void *k)
{
#if defined(__i386__)
    int32_t r;
    __asm__ volatile("flds %1\n\tfmull (%2)\n\tmovl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(a), "r"(k)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
#else
    double d;

    memcpy(&d, k, 8);
    return pc_ftol((pc_ld)pc_float(a) * (pc_ld)d);
#endif
}

/* fld a; fdiv b; fmul c; call _ftol. */
static inline int32_t ftol_div_mul(uint32_t a, uint32_t b, uint32_t c)
{
#if defined(__i386__)
    int32_t r;
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfmuls %3\n\tmovl $0x497a80, %%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(a), "m"(b), "m"(c)
                     : "ecx", "edx", "cc", "memory", X87);
    return r;
#else
    pc_ld v = (pc_ld)pc_float(a) / (pc_ld)pc_float(b);

    return pc_ftol(v * (pc_ld)pc_float(c));
#endif
}

/* The transform pointer *0x6ccd78 (out) then the projection *0x6ccd80,
 * which takes the transform's st(0); between them, *byte = 0 (if given). */
static inline void gte_transform_project(uint32_t *out, uint8_t *byte)
{
#if defined(__i386__)
    if (byte)
        __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tmovb $0, (%1)\n\tcall *0x6ccd80"
                         :
                         : "S"(out), "D"(byte)
                         : "eax", "ecx", "edx", "cc", "memory", X87);
    else
        __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tcall *0x6ccd80"
                         :
                         : "S"(out)
                         : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    /* x86-64: the transform (*0x6ccd78: 0x426a20 / 0x4268c0) returns what
     * the original leaves in st(0); the projection (*0x6ccd80: 0x426b00 /
     * 0x4291f0) takes it as its argument. */
    float h = ((float(GAMEAPI *)(uint8_t *))(uintptr_t)G32(0x6ccd78))((uint8_t *)out);

    if (byte)
        *byte = 0;
    ((void(GAMEAPI *)(float))(uintptr_t)G32(0x6ccd80))(h);
#endif
}

/* ===================================================================== */
/* The views. */

/* ===== 0x4231f0: per car, whether it shows in the current view: the
 * car's position (high words of +0x34 / +0x38 / +0x3c) through the
 * transform and projection pointers; car +0x14b4 = the depth rounded; car
 * +0x646 = 1 if it is in front (z >= -80), not the view's own car (render
 * flags 0x40: player (flags >> 7) & 15) and either inside the view, behind
 * the near plane / past the far plane (code 0x10 / 0x20) or with its
 * 250-unit radius at that depth overlapping the clip rectangle. The
 * original's GTE z word's high half (0x6ccc6a) comes from an unwritten
 * stack word; 0 here. ===== */
void GAMEAPI cars_view_flags_update(void)
{
    uint32_t own = 0, i;
    uint32_t v[6];

    if (g_render_flags & 0x40)
        own = G32(0x6bfb80 + ((g_render_flags >> 7) & 0xf) * 0x274);
    view_camera_load((const uint8_t *)0x6ce5d0);
    for (i = 0; i < G8(g_race + 0x54d); i++) {
        uint32_t car = G32(0x6bfb80 + i * 0x274);
        uint32_t t;

        G32(0x6ccc64) = (U32(car, 0x34) >> 16) | (U32(car, 0x38) & 0xffff0000u);
        G32(0x6ccc68) = U32(car, 0x3c) >> 16;
        gte_transform_project(v, &U8(car, 0x646));
        I32(car, 0x14b4) = x87_fist(v[1]);
        if (BELOW(x87_cmp(v[1], 0xc2a00000u))) /* -80.0 */
            continue;
        if (car == own)
            continue;
        if (v[0] != 0 && !(v[0] & 0x30)) {
            t = x87_div_mul(G32(0x6ccd00), v[1], G32(0x4a334c)); /* H / z * 250 */
            if (BELOW(x87_cmp_sum(v[2], t, G32(0x6ccd64))))
                continue;
            if (ABOVE(x87_cmp_diff(v[2], t, G32(0x6ccd68))))
                continue;
            if (BELOW(x87_cmp_sum(v[3], t, G32(0x6ccd6c))))
                continue;
            if (ABOVE(x87_cmp_diff(v[3], t, G32(0x6ccd70))))
                continue;
        }
        U8(car, 0x646) = 1;
    }
}

/* ===== 0x423c40: end of frame: fog on and the filter (thunks), the
 * ordering tables 0x6cdd0c and 0x6ce57c drawn, fog off, 0x6ce584 and the
 * current double-buffered table (*0x6ce608 + 4) drawn; *0x6ce608 toggled
 * between 0x6ce5f0 and 0x6ce5f8; the tables cleared (0x6cd510 / 0x6cdd80:
 * 0x200 entries; the new current one and 0x6ce580: 2). ===== */
void GAMEAPI ot_frame_flush(void)
{
    fog_on_thunk();
    filter_set_thunk();
    draw_otag(0x6cdd0c);
    draw_otag(0x6ce57c);
    fog_off_thunk();
    draw_otag(0x6ce584);
    draw_otag(G32(0x6ce608) + 4);
    G32(0x6ce608) = G32(0x6ce608) != 0x6ce5f0 ? 0x6ce5f0 : 0x6ce5f8;
    ot_clear_reverse((uint32_t *)0x6cd510, 0x200);
    ot_clear_reverse((uint32_t *)0x6cdd80, 0x200);
    ot_clear_reverse((uint32_t *)(uintptr_t)G32(0x6ce608), 2);
    ot_clear_reverse((uint32_t *)0x6ce580, 2);
}

/* ===== 0x423d10: the view matrix (9 x int16 + pad, 5 dwords) to
 * 0x6ce5a0. ===== */
void GAMEAPI view_matrix_store(const uint32_t *m)
{
    uint32_t i;

    for (i = 0; i < 5; i++)
        G32(0x6ce5a0 + i * 4) = m[i];
}

/* ===== 0x423d70: a camera transform: view = matrix (5 dwords) with no
 * translation, multiplied into by 0x6cd480 (matrix_mul_into); its
 * translation = the position's low words rotated by it. ===== */
void GAMEAPI view_camera_build(uint8_t *view, const uint32_t *matrix, const uint8_t *pos)
{
    int16_t p[3];
    int32_t t[3];
    uint32_t i;

    p[0] = S16(pos, 0);
    p[1] = S16(pos, 4);
    p[2] = S16(pos, 8);
    for (i = 0; i < 5; i++)
        U32(view, i * 4) = matrix[i];
    U32(view, 0x1c) = 0;
    U32(view, 0x18) = 0;
    U32(view, 0x14) = 0;
    matrix_mul_into((const uint8_t *)0x6cd480, view);
    matrix_rotate_vec(view, p, t);
    matrix_translation_set(view, t);
}

/* ===== 0x423df0: a view's matrix and translation into the GTE. ===== */
void GAMEAPI view_camera_load(const uint8_t *view)
{
    gte_matrix_load(view);
    gte_translation_load(view);
}

/* ===== 0x423e10: level-of-detail scale for a zoom (low word, signed):
 * *0x6cce44 = ftol(width / 320 * fix_div8(zoom << 8, 0x6800)); detail
 * level *0x6cce48 = {0,0,1,1,2,2,2,3,3}[*0x6cce44 >> 8] (0 outside);
 * zoom at least 0x68: *0x6cd400 = 0x6800 / zoom, *0x6cd404 = (zoom << 8) /
 * 104. ===== */
void GAMEAPI view_lod_set(uint32_t zoom)
{
    static const uint32_t level[9] = {0, 0, 1, 1, 2, 2, 2, 3, 3};
    int32_t z = (int16_t)zoom;
    int32_t r, e;

    G32(0x6cce44) = (uint32_t)GCALL(0x444490, fix_div8)((int32_t)((uint32_t)z << 8), 0x6800);
    r = ftol_div_mul(x87_from_int(g_screen_width), G32(0x4a3350), x87_from_int(G32(0x6cce44)));
    G32(0x6cce44) = (uint32_t)r;
    e = r >> 8;
    G32(0x6cce48) = (e >= 0 && e <= 8) ? level[e] : 0;
    if (z <= 0x68)
        z = 0x68;
    G32(0x6cd400) = (uint32_t)(0x6800 / z);
    G32(0x6cd404) = (uint32_t)((z << 8) / 104);
}

/* ===== 0x423f00: set the zoom: word 0x6cd46a, the projection distance
 * (0x426260) and the detail scale (0x423e10). ===== */
void GAMEAPI view_zoom_set(uint32_t zoom)
{
    G16(0x6cd46a) = (uint16_t)zoom;
    projection_h_set((int32_t)(int16_t)zoom);
    view_lod_set(zoom);
}

/* ===== 0x423f20: the whole screen as the view (pause / menus). Split
 * screen (race +0x552): the saved view 0x4bde98 copied to 0x4bde18 with
 * the rectangle (0, 0, w, h) (*0x6cd462 / *0x6cd464), its +0x18 byte
 * cleared, kept (0x426600) and the viewport set (0x4265d0); else the
 * viewport of the current view (*0x6ce60c + 0x5c). Returns h << 16 | w.
 * ===== */
int32_t GAMEAPI view_full_screen(void)
{
    uint32_t h = G32(0x6cd464) & 0xffff;
    uint32_t w = G16(0x6cd462);

    if (G8(g_race + 0x552)) {
        uint16_t buf[10]; /* the original's 20-byte local; the rectangle is its words 2..5 */

        view_rect_saved_get((uint32_t *)buf);
        view_saved_get((uint32_t *)0x4bde18);
        G16(0x4bde18) = 0;
        G16(0x4bde1a) = 0;
        G16(0x4bde1c) = (uint16_t)w;
        G16(0x4bde1e) = (uint16_t)h;
        G16(0x4bde20) = 0;
        G16(0x4bde22) = 0;
        G8(0x4bde30) = 0;
        buf[4] = (uint16_t)w;
        buf[5] = (uint16_t)h;
        buf[2] = 0;
        buf[3] = 0;
        GCALL(0x426600, view_keep)((const uint32_t *)GMEM(0x4bde18));
        GCALL(0x4265d0, viewport_apply)((int16_t *)&buf[2]);
    } else {
        GCALL(0x4265d0, viewport_apply)((int16_t *)GMEM(G32(0x6ce60c) + 0x5c));
    }
    return (int32_t)(h << 16 | w);
}

/* ===================================================================== */
/* The GTE emulation's state. */

/* ===== 0x426200: screen offset: 16.16 at 0x6ccc40 / 0x6ccc44, floats at
 * 0x6ccd4c / 0x6ccd50. ===== */
void GAMEAPI gte_screen_offset_set(int32_t x, int32_t y)
{
    G32(0x6ccc40) = (uint32_t)x << 16;
    G32(0x6ccc44) = (uint32_t)y << 16;
    G32(0x6ccd4c) = x87_from_int((uint32_t)x);
    G32(0x6ccd50) = x87_from_int((uint32_t)y);
}

/* ===== 0x426240: the screen offset's integer parts. ===== */
void GAMEAPI gte_screen_offset_get(int32_t *x, int32_t *y)
{
    *x = GI32(0x6ccc40) >> 16;
    *y = GI32(0x6ccc44) >> 16;
}

/* ===== 0x426260: projection distance for a zoom: *0x6cce6c = zoom,
 * H (*0x6ccd00) = width / 320 * zoom, word 0x6ccc48 = ftol(H); the
 * flare scale *0x6cd41c = width * 0.5 / 640, *0x6cd418 twice that; the
 * detail scale (0x423e10); the projection (0x417ed0) for the race's
 * screen size (+0xa82, +0xa80) at H. ===== */
void GAMEAPI projection_h_set(int32_t zoom)
{
    uint32_t w, a80, a82;

    G32(0x6cce6c) = x87_from_int((uint32_t)zoom);
    w = x87_from_int(g_screen_width);
    G32(0x6ccd00) = x87_div_mul(w, G32(0x4a3350), G32(0x6cce6c));
    G16(0x6ccc48) = (uint16_t)ftol_f(G32(0x6ccd00));
    w = x87_from_int(g_screen_width);
    G32(0x6cd41c) = x87_mul_div(w, G32(0x4a3338), G32(0x4a3358));
    G32(0x6cd418) = x87_add(G32(0x6cd41c), G32(0x6cd41c)); /* fld; fadd st, st: exact */
    view_lod_set((uint32_t)zoom);
    a80 = x87_from_int(U16(g_race, 0xa80));
    a82 = x87_from_int(U16(g_race, 0xa82));
    projection_set(0, 0, a82, a80, G32(0x6ccd00));
}

/* ===== 0x426330: rotation matrix (9 x int16) into the GTE (0x6ccbe0)
 * and as floats / 4096 into rows 0x6ccd1c / 0x6ccd2c / 0x6ccd3c. ===== */
void GAMEAPI gte_matrix_load(const uint8_t *m)
{
    static const uint16_t row[3] = {0xd1c, 0xd2c, 0xd3c};
    uint32_t i;

    for (i = 0; i < 9; i++)
        G16(0x6ccbe0 + i * 2) = U16(m, i * 2);
    for (i = 0; i < 9; i++)
        G32(0x6cc000 + row[i / 3] + (i % 3) * 4) = x87_s16_mul(G16(0x6ccbe0 + i * 2), G32(0x4a3310));
}

/* ===== 0x426480: the GTE rotation matrix out (9 words). ===== */
void GAMEAPI gte_matrix_save(uint8_t *out)
{
    uint32_t i;

    for (i = 0; i < 9; i++)
        U16(out, i * 2) = G16(0x6ccbe0 + i * 2);
}

/* ===== 0x426530: translation (+0x14 / +0x18 / +0x1c) into the GTE
 * (0x6ccbf4..) and as floats (0x6ccd0c..). ===== */
void GAMEAPI gte_translation_load(const uint8_t *v)
{
    G32(0x6ccbf4) = U32(v, 0x14);
    G32(0x6ccbf8) = U32(v, 0x18);
    G32(0x6ccbfc) = U32(v, 0x1c);
    G32(0x6ccd0c) = x87_from_int(G32(0x6ccbf4));
    G32(0x6ccd10) = x87_from_int(G32(0x6ccbf8));
    G32(0x6ccd14) = x87_from_int(G32(0x6ccbfc));
}

/* ===== 0x426590: the saved rectangle record (5 dwords at 0x4bde80). */
void GAMEAPI view_rect_saved_get(uint32_t *out)
{
    uint32_t i;

    for (i = 0; i < 5; i++)
        out[i] = G32(0x4bde80 + i * 4);
}

/* ===== 0x4265b0: the saved view (0x5c bytes at 0x4bde98). ===== */
void GAMEAPI view_saved_get(uint32_t *out)
{
    uint32_t i;

    for (i = 0; i < 0x17; i++)
        out[i] = G32(0x4bde98 + i * 4);
}

/* ===================================================================== */
/* Integer matrix helpers (int16 3x3 at +0, int32 translation at +0x14). */

/* ===== 0x426e10: out = (m * v) >> 12 (v: three int16). ===== */
void GAMEAPI matrix_rotate_vec(const uint8_t *m, const int16_t *v, int32_t *out)
{
    int32_t x = v[0], y = v[1], z = v[2];

    out[0] = (S16(m, 0) * x + S16(m, 2) * y + S16(m, 4) * z) >> 12;
    out[1] = (S16(m, 6) * x + S16(m, 8) * y + S16(m, 0xa) * z) >> 12;
    out[2] = (S16(m, 0xc) * x + S16(m, 0xe) * y + S16(m, 0x10) * z) >> 12;
}

/* ===== 0x426e90: b's rotation = (a * b) >> 12, its translation = a's.
 * The original copies 32 bytes of its 18-byte product, so b's pad word
 * +0x12 gets an unwritten stack word (0 here), as does the translation when
 * a is b. ===== */
void GAMEAPI matrix_mul_into(const uint8_t *a, uint8_t *b)
{
    int16_t r[16] = {0};
    uint32_t i, j, k;

    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++) {
            int32_t s = 0;
            for (k = 0; k < 3; k++)
                s += S16(a, (i * 3 + k) * 2) * S16(b, (k * 3 + j) * 2);
            r[i * 3 + j] = (int16_t)(s >> 12);
        }
    memcpy(b, r, 32); /* sic: 32 bytes, the last 14 stack garbage in the original */
    U32(b, 0x14) = U32(a, 0x14); /* a read after: garbage too when a == b */
    U32(b, 0x18) = U32(a, 0x18);
    U32(b, 0x1c) = U32(a, 0x1c);
}

/* ===== 0x426f60: translation = v. ===== */
void GAMEAPI matrix_translation_set(uint8_t *m, const int32_t *v)
{
    I32(m, 0x14) = v[0];
    I32(m, 0x18) = v[1];
    I32(m, 0x1c) = v[2];
}

/* ===== 0x428be0: dst = transpose(src) (3x3 int16). ===== */
void GAMEAPI matrix_transpose_s16(const uint8_t *src, int16_t *dst)
{
    int16_t m[16]; /* the original copies 32 bytes */
    uint32_t i, k;

    memcpy(m, src, 32);
    for (i = 0; i < 3; i++)
        for (k = 0; k < 3; k++)
            dst[i * 3 + k] = m[k * 3 + i];
}

/* ===================================================================== */
/* Lighting. */

/* ===== 0x427100: GTE-style light colour (integer): the light matrix
 * 0x6ccc20 times the normal 0x6ccc88 / 8c / 90 (int16 in dwords) plus the
 * back colour 0x6ccc14.. << 12, >> 8, times the colour bytes 0x6ccc7c..,
 * >> 16 saturated to 0xff (anything with bits above 0xff0000, negatives
 * too), into 0x6cccbc..bf (code byte copied). ===== */
void GAMEAPI gte_light_colour(void)
{
    int32_t x = GS16(0x6ccc88), y = GS16(0x6ccc8c), z = GS16(0x6ccc90);
    int32_t r = GS16(0x6ccc20) * x + GS16(0x6ccc22) * y + GS16(0x6ccc24) * z + (int32_t)(G32(0x6ccc14) << 12);
    int32_t g = GS16(0x6ccc26) * x + GS16(0x6ccc28) * y + GS16(0x6ccc2a) * z + (int32_t)(G32(0x6ccc18) << 12);
    int32_t b = GS16(0x6ccc2c) * x + GS16(0x6ccc2e) * y + GS16(0x6ccc30) * z + (int32_t)(G32(0x6ccc1c) << 12);
    uint32_t v;

    v = (G32(0x6ccc7c) & 0xff) * (uint32_t)(r >> 8);
    G8(0x6cccbc) = (v & 0xffff0000u) > 0xff0000u ? 0xff : (uint8_t)(v >> 16);
    v = G8(0x6ccc7d) * (uint32_t)(g >> 8);
    G8(0x6cccbd) = (v & 0xffff0000u) > 0xff0000u ? 0xff : (uint8_t)(v >> 16);
    v = G8(0x6ccc7e) * (uint32_t)(b >> 8);
    G8(0x6cccbe) = (v & 0xffff0000u) > 0xff0000u ? 0xff : (uint8_t)(v >> 16);
    G8(0x6cccbf) = G8(0x6ccc7f);
}

/* ===== 0x428940: light matrix (5 dwords: 9 x int16 + pad) to 0x6ccc20,
 * and as floats / 4096 into 0x6cce00 / 0x6cce10 / 0x6cce20 rows. ===== */
void GAMEAPI light_matrix_load(const uint32_t *m)
{
    static const uint16_t row[3] = {0xe00, 0xe10, 0xe20};
    uint32_t i;

    for (i = 0; i < 5; i++)
        G32(0x6ccc20 + i * 4) = m[i];
    for (i = 0; i < 9; i++)
        G32(0x6cc000 + row[i / 3] + (i % 3) * 4) = x87_s16_mul(G16(0x6ccc20 + i * 2), G32(0x4a3310));
}

/* ===== 0x4292b0: the lit colour at a point: back colour from *rgb (each
 * byte b: (b << 4) | 15 at 0x6ccc14 / 18 / 1c, also as floats at 0x6cce30
 * / 34 / 38), the point (dword +0, word +4) into the GTE vector, the
 * float light colour (0x427400), *out = the colour 0x6cccbc. ===== */
void GAMEAPI light_colour_at(const uint8_t *pos, const uint32_t *rgb, uint32_t *out)
{
    G32(0x6ccc14) = ((*rgb & 0xff) << 4) | 0xf;
    G32(0x6ccc18) = ((*rgb & 0xff00) | 0xf0) >> 4;
    G32(0x6ccc1c) = ((*rgb & 0xff0000) | 0xf000) >> 12;
    G32(0x6cce38) = x87_from_int(G32(0x6ccc1c));
    G32(0x6cce34) = x87_from_int(G32(0x6ccc18));
    G32(0x6cce30) = x87_from_int(G32(0x6ccc14));
    GI32(0x6ccc68) = S16(pos, 4);
    G32(0x6ccc64) = U32(pos, 0);
    gte_light_colour_float();
    *out = G32(0x6cccbc);
}

/* ===== 0x4278a0: the screen triangle's cross product (GTE NCLIP): points
 * 0x6ccc94 / 98 / 9c (int16 x, y) -> 0x6cccc4. ===== */
int32_t GAMEAPI gte_nclip(void)
{
    int32_t x0 = GS16(0x6ccc94), y0 = GS16(0x6ccc96);
    int32_t r = (GS16(0x6ccc98) - x0) * (GS16(0x6ccc9e) - y0) - (GS16(0x6ccc9c) - x0) * (GS16(0x6ccc9a) - y0);

    GI32(0x6cccc4) = r;
    return r;
}

/* ===================================================================== */
/* Ordering tables. */

/* ===== 0x427a40: forward-linked table: entry k -> &entry k+1 (| bit 31),
 * last = -1. Returns 0. n <= 1 runs away (sic). ===== */
int32_t GAMEAPI ot_clear_forward(uint32_t *ot, uint32_t n)
{
    uint32_t k = n - 1;

    do {
        *ot = (A(ot) + 4) | 0x80000000u;
        ot++;
    } while (--k);
    *ot = 0xffffffffu;
    return 0;
}

/* ===== 0x427a70: reverse-linked table: entry k -> &entry k-1 (| bit 31)
 * for k = n-1..1, entry 0 = -1. Returns 0. ===== */
int32_t GAMEAPI ot_clear_reverse(uint32_t *ot, uint32_t n)
{
    uint32_t k = n - 1;
    uint32_t *p = ot + k;

    do {
        *p = (A(p) - 4) | 0x80000000u;
        p--;
    } while (--k);
    *p = 0xffffffffu;
    return 0;
}

/* ===== 0x427ab0: link a packet at the head of a table entry (bit 31 of
 * the entry address ignored). Returns the entry address. ===== */
uint32_t GAMEAPI ot_add_prim(uint32_t entry, uint32_t *prim)
{
    uint32_t *head = (uint32_t *)(uintptr_t)(entry & 0x7fffffffu);

    *prim = *head;
    *head = A(prim);
    return A(head);
}

/* ===================================================================== */
/* Thunks and wrappers. */

/* ===== 0x428240 / 0x428250 / 0x428260: jumps to the filter / fog on /
 * fog off render state setters. ===== */
void GAMEAPI filter_set_thunk(void)
{
    GCALL(0x41dd90, d3d_filter_set)();
}

void GAMEAPI fog_on_thunk(void)
{
    GCALL(0x41de40, d3d_fog_on)();
}

void GAMEAPI fog_off_thunk(void)
{
    GCALL(0x41de60, d3d_fog_off)();
}

/* ===== 0x428c80: jump to 0x4205f0 (a new texture handle). ===== */
int32_t GAMEAPI texture_handle_new(void)
{
    return GCALL(0x4205f0, texture_page_next)();
}

/* ===== 0x429340: jump to 0x4219c0. ===== */
int32_t GAMEAPI textures_unusable_thunk(void)
{
    return (int32_t)GCALL(0x4219c0, flag_4bcd54_clear)();
}

/* ===== 0x428c30: 0x41fef0 with the same six arguments (texture surface
 * lock). ===== */
int32_t GAMEAPI texture_surface_lock(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    return GCALL(0x41fef0, texture_lock)((int32_t)a, b, (uint32_t *)GMEM(c), (uint32_t *)GMEM(d), (uint32_t *)GMEM(e),
                                         (uint32_t *)GMEM(f));
}

/* ===== 0x428c60: 0x420000 with the same three arguments (unlock). ===== */
int32_t GAMEAPI texture_surface_unlock(uint32_t a, uint32_t b, uint32_t c)
{
    return GCALL(0x420000, texture_unlock)((int32_t)a, b, (const uint32_t *)GMEM(c));
}

/* ===== 0x428c90: 0x41e710 with the same six arguments (upload). ===== */
int32_t GAMEAPI texture_surface_upload(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    return GCALL(0x41e710, texture_upload_body)((int32_t)a, (int32_t)b, (int32_t)c, d, e, f);
}

/* ===== 0x4290a0: *p = v. ===== */
void GAMEAPI dword_store(uint32_t *p, uint32_t v)
{
    *p = v;
}

/* ===== 0x4291f0: does nothing (the DirectX error report, and the
 * projection pointer *0x6ccd80 on the 3DNow! path). ===== */
void GAMEAPI dx_error_ignore(void)
{
}

/* ===================================================================== */
/* Render state. */

/* ===== 0x428270: z-buffer back on (0x41de80) if wanted (*0x6cce7c), with
 * 0x6cce80 / 0x6cce84 = 1. Returns 1, or 0 when not wanted. ===== */
int32_t GAMEAPI zbuffer_restore(void)
{
    if (G32(0x6cce7c) == 0)
        return 0;
    GCALL(0x41de80, d3d_zbuffer_on)();
    G32(0x6cce80) = 1;
    G32(0x6cce84) = 1;
    return 1;
}

/* ===== 0x428310: debug print of a line with its trailing "\n" / "\r\n"
 * cut (both tested on the original length), through 0x496340. ===== */
int32_t GAMEAPI debug_line_print(char *s)
{
    int32_t n = 0;

    while (s[n])
        n++;

    if (n >= 1 && s[n - 1] == '\n')
        s[n - 1] = 0;
    if (n >= 2 && s[n - 2] == '\r')
        s[n - 2] = 0;
#if defined(__i386__)
    /* A tail call: eax as the printf leaves it. */
    return ((int32_t(GAMEAPI *)(const char *))(uintptr_t)0x496340)(s);
#else
    GCALL(0x496340, debug_log_printf)(s);
    return 0;
#endif
}

/* ===== 0x428390: clear colour: 0x6ccd8c = b << 16 | g << 8 | r (bytes),
 * then 0x41ec40(r, g, b). ===== */
int32_t GAMEAPI clear_colour_store(uint32_t r, uint32_t g, uint32_t b)
{
    r &= 0xff;
    g &= 0xff;
    b &= 0xff;
    G32(0x6ccd8c) = ((b << 8 | g) << 8) | r;
#if defined(__i386__)
    /* A tail call: eax as the callee leaves it. */
    return ((int32_t(GAMEAPI *)(uint32_t, uint32_t, uint32_t))(uintptr_t)0x41ec40)(r, g, b);
#else
    GCALL(0x41ec40, background_colour_set)((int32_t)r, (int32_t)g, (int32_t)b);
    return 0;
#endif
}

/* ===== 0x4283d0: depth range: 0x6ccd98 = near, 0x6ccd9c = far, 0x6ccd94
 * = c; floats near * 32 (0x6ccda0), far * 32 (0x6ccda4); 0x6ccda8 = 1 /
 * ((far - near) / 255); 0x6ccdac = 1 / (far * 32 - near * 32 + 1) * 255.
 * Returns far - near. ===== */
int32_t GAMEAPI depth_range_set(int32_t near, int32_t far, uint32_t c)
{
    uint32_t d;

    G32(0x6ccd98) = (uint32_t)near;
    G32(0x6ccd9c) = (uint32_t)far;
    G32(0x6ccda0) = x87_from_int((uint32_t)near << 5);
    G32(0x6ccd94) = c;
    G32(0x6ccda4) = x87_from_int((uint32_t)far << 5);
    d = x87_from_int((uint32_t)(far - near));
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfdivrs %3\n\tfstps %0"
                     : "=m"(G32(0x6ccda8))
                     : "m"(d), "m"(G32(0x4a332c)), "m"(G32(0x4a331c))
                     : "st");
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfadds %3\n\tfdivrs %3\n\tfmuls %4\n\tfstps %0"
                     : "=m"(G32(0x6ccdac))
                     : "m"(G32(0x6ccda4)), "m"(G32(0x6ccda0)), "m"(G32(0x4a331c)), "m"(G32(0x4a332c))
                     : "st");
    return far - near;
}

/* ===== 0x428a50: render flags: 0x6cce90 = a, 0x6cce88 = b, 0x6cce8c =
 * c, z-buffer wanted 0x6cce7c = e (d unused). ===== */
void GAMEAPI render_flags_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)d;
    G32(0x6cce88) = b;
    G32(0x6cce8c) = c;
    G32(0x6cce90) = a;
    G32(0x6cce7c) = e;
}

/* ===== 0x428a80: save the projection (zoom 0x6cce6c, depth range
 * 0x6ccd98 / 9c / 94, far plane through 0x428bd0) at 0x4bdefc / 0x4be108
 * / 0x4bde94 / 0x4bdef4 / 0x4bdef8; render flags | 0x42, with the local
 * player slot (0x48cf20) in bits 7..10; facing sign 0x6cce94 = race
 * +0x58b ^ 1. ===== */
void GAMEAPI projection_push(void)
{
    uint32_t slot;

    G32(0x4bdefc) = G32(0x6cce6c);
    G32(0x4be108) = G32(0x6ccd98);
    G32(0x4bde94) = G32(0x6ccd9c);
    G32(0x4bdef4) = G32(0x6ccd94);
#if defined(__i386__)
    __asm__ volatile("call far_plane_get\n\tfstps %0" : "=m"(G32(0x4bdef8)) : : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    G32(0x4bdef8) = pc_fstp32((pc_ld)far_plane_get());
#endif
    g_render_flags |= 0x42;
    slot = (uint32_t)GCALL(0x48cf20, net_local_slot)();
    g_render_flags = ((slot & 0xf) << 7) | (g_render_flags & 0xfffff87fu);
    G32(0x6cce94) = G8(g_race + 0x58b) ^ 1;
}

/* ===== 0x428b00: restore it: depth range, zoom (ftol of the saved float,
 * 0x426260); render flags & ~0x42; facing sign = race +0x58b. The far
 * plane is not restored (sic). ===== */
void GAMEAPI projection_pop(void)
{
    depth_range_set(GI32(0x4be108), GI32(0x4bde94), G32(0x4bdef4));
    projection_h_set(ftol_f(G32(0x4bdefc)));
    g_render_flags &= ~0x42u;
    G32(0x6cce94) = G8(g_race + 0x58b);
}

/* ===== 0x428b90: far plane (float) at 0x6ccd08 and the depth range
 * (ftol(f * 0.5) >> 5, ftol(f) >> 5, 0x1fb). ===== */
void GAMEAPI far_plane_set(uint32_t f)
{
    int32_t a, b;

    G32(0x6ccd08) = f;
    a = ftol_f(f);
    b = ftol_mul_d(f, (const void *)0x4a3370);
    depth_range_set(b >> 5, a >> 5, 0x1fb);
}

/* ===================================================================== */
/* Viewports. */

/* One screen-space quad of the border: corners (x0, y0) .. (x1, y1). */
static void border_quad(uint8_t *p, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    U32(p, 0x10) = U32(p, 0x40) = x87_from_int((uint32_t)x0);
    U32(p, 0x14) = U32(p, 0x2c) = x87_from_int((uint32_t)y0);
    U32(p, 0x28) = U32(p, 0x58) = x87_from_int((uint32_t)x1);
    U32(p, 0x44) = U32(p, 0x5c) = x87_from_int((uint32_t)y1);
    draw_prim(p);
}

/* ===== 0x4284b0: full-screen projection and viewport (centre, 0x417ed0,
 * 0x41f2d0), then the frame between the outer rectangle r and the inner
 * one in (x, y, w, h words) as four black quads (packet code 0xa8, z 4.0)
 * through draw_prim; with fill set and in.h > 0, the inner rectangle in
 * the clear colour 0x6ccd8c at z 1.9. The original's packet is a stack
 * local whose unset fields hold leftovers (zero here), except the last
 * dword, the float scratch of the width conversion. ===== */
void GAMEAPI view_border_draw(const int16_t *r, const int16_t *in, uint32_t fill)
{
    uint8_t p[0x6c];
    uint32_t c, hf, wf;

    gte_screen_offset_set((int32_t)g_screen_width >> 1, (int32_t)g_screen_height >> 1);
    hf = x87_from_int(g_screen_height);
    wf = x87_from_int(g_screen_width);
    projection_set(0, 0, wf, hf, G32(0x6ccd00));
    viewport_set(0, 0, g_screen_width, g_screen_height);
    memset(p, 0, sizeof(p));
    U32(p, 0x68) = wf; /* the original's float scratch is the packet's last dword */
    U32(p, 4) = 0xa8000000u;
    U32(p, 0xc) = U32(p, 0x24) = U32(p, 0x3c) = U32(p, 0x54) = 0x40800000u; /* 4.0 */
    border_quad(p, r[0], r[1], r[2] + r[0], in[1]);
    border_quad(p, r[0], in[3] + in[1], r[2] + r[0], r[1] + r[3]);
    border_quad(p, r[0], r[1], in[0], r[1] + r[3]);
    border_quad(p, in[2] + in[0], r[1], r[2] + r[0], r[1] + r[3]);
    if (fill && in[3] > 0) {
        c = G32(0x6ccd8c);
        U8(p, 4) = (uint8_t)c;
        U8(p, 6) = (uint8_t)(c >> 16);
        U8(p, 5) = (uint8_t)(c >> 8);
        U32(p, 0xc) = U32(p, 0x24) = U32(p, 0x3c) = U32(p, 0x54) = 0x3ff33333u; /* 1.9 */
        border_quad(p, in[0], in[1], in[2] + in[0], in[3] + in[1]);
    }
}

/* ===== 0x428770: a view's viewport (x, y, w, h words at +0): with a
 * border, the frame round it (0x4284b0 with the rectangle grown by the
 * border, filled); the screen centre (w / 2, h / 2), projection
 * (0x417ed0) and viewport (0x41f2d0); with a border and the z-buffer
 * wanted, 0x41e190, 0x41e040(1), 0x41e170. ===== */
void GAMEAPI view_viewport_apply(const uint8_t *view, int32_t border)
{
    if (border) {
        uint8_t copy[0x5c];

        memcpy(copy, view, sizeof(copy));
        U16(copy, 4) += (uint16_t)(border * 2);
        U16(copy, 6) += (uint16_t)(border * 2);
        U16(copy, 0) -= (uint16_t)border;
        U16(copy, 2) -= (uint16_t)border;
        view_border_draw((const int16_t *)copy, (const int16_t *)view, 1);
    }
    gte_screen_offset_set(S16(view, 4) >> 1, S16(view, 6) >> 1);
    {
        uint32_t hf = x87_from_int((uint32_t)(int32_t)S16(view, 6));
        uint32_t wf = x87_from_int((uint32_t)(int32_t)S16(view, 4));
        projection_set(0, 0, wf, hf, G32(0x6ccd00));
    }
    viewport_set((int32_t)S16(view, 0), (int32_t)S16(view, 2), (int32_t)S16(view, 4), (int32_t)S16(view, 6));
    if (border && G32(0x6cce7c)) {
        GCALL(0x41e190, d3d_scene_end)();
        GCALL(0x41e040, viewport_clear)(1);
        GCALL(0x41e170, d3d_scene_begin)();
    }
}

/* ===================================================================== */
/* The smoke clouds (5 records of 0x1e2c at *0x6ccb54). */

/* ===== 0x42b9a0: each live cloud (+0xc frames left, decremented) as a
 * ring of 32 quads: inner / outer rings of radius (+0x24 - left + 2 / + 5)
 * * +0x28 round (+0, +4 >> 16, +8) through the transform pointers;
 * brightness 255 / +0x20 * left while left < +0x20; texture frame +0x10
 * cycling below +0x1c, from the track's texture table (*0x6c1eb0 +0x2e,
 * kind +0x18; kinds 0x86 / 0x91 take bytes 1 of the three words from
 * bytes 9, 0xb, 1 and byte 3 of the third from byte 5); per quad the four
 * z added (float, rounded each step), skipped unless > 0; depth bucket
 * ftol(sum) >> 7 - 1 squeezed above 0x100 / 0x180, dropped from 0x1fc
 * (or above 0x1ff), at least 1; dropped if all four clip codes share a
 * bit; packet (0x78 bytes from +0x2c, or +0xf2c for the second view):
 * colour (code 0x9a, scaled by the brightness unless *0x6ccd84 bit 0) and
 * alpha byte, four vertices with the texture words, linked into
 * 0x6cdd80 / 0x6cd510 (by *0x6cce84) at the bucket. The GTE z word's high
 * half (0x6ccc6a) is an unwritten stack word in the original: 0 here.
 * ===== */
void GAMEAPI smoke_clouds_draw(void)
{
    uint32_t ring[32][12]; /* per step: inner vertex (6 dwords), outer vertex */
    uint32_t off;

    /* The transforms leave x / y unset behind the near plane, and such
     * vertices are still copied; the original's are stack leftovers. */
    memset(ring, 0, sizeof(ring));

    for (off = 0; off < 0x96dc; off += 0x1e2c) {
        uint8_t *rec = (uint8_t *)(uintptr_t)(G32(0x6ccb54) + off);
        uint32_t left = U32(rec, 0xc), alpha, tag, frame, kind, t0, t1, t2, step;
        int32_t r1, r2, cx, cz;
        uint32_t y, angle = 0;
        uint8_t *pk, *tex;

        if (left == 0)
            continue;
        left--;
        U32(rec, 0xc) = left;
        if ((int32_t)left <= 0)
            continue;
        alpha = left < U32(rec, 0x20) ? 0xffu / U32(rec, 0x20) * left : 0xff;
        r1 = (int32_t)((U32(rec, 0x24) - left + 2) * U32(rec, 0x28));
        r2 = (int32_t)((U32(rec, 0x24) - left + 5) * U32(rec, 0x28));
        cz = I32(rec, 8);
        y = (uint32_t)(I32(rec, 4) >> 16) & 0xffff;
        cx = I32(rec, 0);
        for (step = 0; step < 32; step++) {
            int32_t s = GCALL(0x480b10, sin_table)((int32_t)angle);
            int32_t c = GCALL(0x480bc0, cos_table)((int32_t)angle);

            G32(0x6ccc64) = ((uint32_t)((s * r1 + cx) >> 16) & 0xffff) | y << 16;
            G32(0x6ccc68) = (uint32_t)((c * r1 + cz) >> 16) & 0xffff;
            gte_transform_project(&ring[step][0], 0);
            G32(0x6ccc64) = ((uint32_t)((s * r2 + cx) >> 16) & 0xffff) | y << 16;
            G32(0x6ccc68) = (uint32_t)((c * r2 + cz) >> 16) & 0xffff;
            gte_transform_project(&ring[step][6], 0);
            angle += 0x3243;
        }
        tag = (U32(rec, 0x14) & 0xffffff) | 0x9a000000u;
        pk = rec + (G32(0x6ce60c) == 0x6cd4a0 ? 0x2c : 0xf2c);
        frame = U32(rec, 0x10) + 1;
        if (frame >= U32(rec, 0x1c))
            frame = 0;
        U32(rec, 0x10) = frame;
        kind = U32(rec, 0x18);
        tex = (uint8_t *)(uintptr_t)(G32(0x6c1e14) + ((U16(G32(0x6c1eb0), 0x2e) + kind + frame) << 4));
        t0 = U32(tex, 0);
        t1 = U32(tex, 4);
        t2 = U32(tex, 8);
        if (kind == 0x86 || kind == 0x91) {
            t0 = (t0 & 0xffff00ffu) | (uint32_t)U8(tex, 9) << 8;
            t1 = (t1 & 0xffff00ffu) | (uint32_t)U8(tex, 0xb) << 8;
            t2 = (t2 & 0x00ff00ffu) | (uint32_t)U8(tex, 1) << 8 | (uint32_t)U8(tex, 5) << 24;
        }
        for (step = 1; step <= 32; step++, pk += 0x78) {
            uint32_t *a = ring[step - 1], *b = ring[step < 32 ? step : 0];
            uint32_t sum;
            int32_t depth;
            uint32_t entry;

            sum = x87_add(a[1], a[7]);
            sum = x87_add(sum, b[1]);
            sum = x87_add(sum, b[7]);
            if (!ABOVE(x87_cmp(sum, G32(0x4a3318))))
                continue;
            depth = (ftol_f(sum) >> 7) - 1;
            if (depth > 0x100) {
                depth = depth / 2 + 0x80;
                if (depth > 0x180) {
                    depth = depth / 2 + 0xc0;
                    if (depth > 0x1ff)
                        continue;
                }
            }
            if (depth < 1)
                depth = 1;
            else if (depth >= 0x1fc)
                continue;
            if (a[0] & a[6] & b[0] & b[6])
                continue;
            memcpy(pk + 8, a, 24);
            U32(pk, 0x20) = t0;
            memcpy(pk + 0x24, b, 24);
            memcpy(pk + 0x40, a + 6, 24);
            U16(pk, 0x58) = (uint16_t)t2;
            memcpy(pk + 0x5c, b + 6, 24);
            U32(pk, 0x3c) = t1;
            U16(pk, 0x74) = (uint16_t)(t2 >> 16);
            if (G8(0x6ccd84) & 1) {
                U32(pk, 4) = tag;
            } else {
                uint32_t k = alpha & 0xff;
                U32(pk, 4) = 0x9a000000u | ((((tag >> 16) & 0xff) * k) >> 8 & 0xff) << 16 |
                             ((((tag >> 8) & 0xff) * k) >> 8 & 0xff) << 8 | (((tag & 0xff) * k) >> 8 & 0xff);
            }
            U8(pk, 0x22) = (uint8_t)alpha;
            entry = G32((G32(0x6cce84) ? 0x6cdd80 : 0x6cd510) + (uint32_t)depth * 4) & 0x7fffffffu;
            U32(pk, 0) = G32(entry);
            G32(entry) = A(pk);
        }
    }
}

/* ===================================================================== */
/* The x87 / 3DNow! transforms: the original's instructions. */
#if defined(__i386__)
__asm__(".intel_syntax noprefix\n"
        ".text\n"

        /* 0x426620: float rotate-translate of the GTE vector 0x6ccc64 / 66 / 68
         * (int16) by the float matrix (rows 0x6ccd1c / 2c / 3c, translation
         * 0x6ccd0c / 10 / 14), z first, each rounded by the control word into
         * 0x6cccd0 (z), 0x6cccc8 (x), 0x6ccccc (y). */
        ".globl gte_rt_float\n"
        ".type gte_rt_float,@function\n"
        "gte_rt_float:\n"
        "    push ebp\n" /* 426620 */
        "    mov ebp, esp\n" /* 426621 */
        "    push ecx\n" /* 426623 */
        "    fild word ptr [0x6ccc64]\n" /* 426624 */
        "    fild word ptr [0x6ccc66]\n" /* 42662a */
        "    fild word ptr [0x6ccc68]\n" /* 426630 */
        "    fld dword ptr [0x6ccd14]\n" /* 426636 */
        "    fld dword ptr [0x6ccd3c]\n" /* 42663c */
        "    fmul st, st(4)\n" /* 426642 */
        "    fld dword ptr [0x6ccd40]\n" /* 426644 */
        "    fmul st, st(4)\n" /* 42664a */
        "    fxch st(1)\n" /* 42664c */
        "    faddp st(2), st\n" /* 42664e */
        "    fld dword ptr [0x6ccd44]\n" /* 426650 */
        "    fmul st, st(3)\n" /* 426656 */
        "    fxch st(1)\n" /* 426658 */
        "    faddp st(2), st\n" /* 42665a */
        "    faddp st(1), st\n" /* 42665c */
        "    fistp dword ptr [0x6cccd0]\n" /* 42665e */
        "    fld dword ptr [0x6ccd0c]\n" /* 426664 */
        "    fld dword ptr [0x6ccd1c]\n" /* 42666a */
        "    fmul st, st(4)\n" /* 426670 */
        "    fld dword ptr [0x6ccd20]\n" /* 426672 */
        "    fmul st, st(4)\n" /* 426678 */
        "    fxch st(1)\n" /* 42667a */
        "    faddp st(2), st\n" /* 42667c */
        "    fld dword ptr [0x6ccd24]\n" /* 42667e */
        "    fmul st, st(3)\n" /* 426684 */
        "    fxch st(1)\n" /* 426686 */
        "    faddp st(2), st\n" /* 426688 */
        "    faddp st(1), st\n" /* 42668a */
        "    fistp dword ptr [0x6cccc8]\n" /* 42668c */
        "    fld dword ptr [0x6ccd10]\n" /* 426692 */
        "    fld dword ptr [0x6ccd2c]\n" /* 426698 */
        "    fmul st, st(4)\n" /* 42669e */
        "    fld dword ptr [0x6ccd30]\n" /* 4266a0 */
        "    fmul st, st(4)\n" /* 4266a6 */
        "    fxch st(1)\n" /* 4266a8 */
        "    faddp st(2), st\n" /* 4266aa */
        "    fld dword ptr [0x6ccd34]\n" /* 4266ac */
        "    fmul st, st(3)\n" /* 4266b2 */
        "    fxch st(1)\n" /* 4266b4 */
        "    faddp st(2), st\n" /* 4266b6 */
        "    faddp st(1), st\n" /* 4266b8 */
        "    fistp dword ptr [0x6ccccc]\n" /* 4266ba */
        "    fcompp\n" /* 4266c0 */
        "    fstp dword ptr [ebp - 0x4]\n" /* 4266c2 */
        "    mov esp, ebp\n" /* 4266c5 */
        "    pop ebp\n" /* 4266c7 */
        "    ret\n" /* 4266c8 */


        /* 0x4266d0: the same, then the GTE-style screen point: x / y saturated to
         * int16 at 0x6ccc88 / 8c, z at 0x6ccc90 (and 1..0xffff at 0x6cccac / b0);
         * screen x / y = offset (0x6ccc42 / 46) + x * H / z (z > 2, else * H),
         * clamped to -1024..1023 (0x447fc000 is 1023.0) and stored as words 0x6ccc9c / 9e (first set
         * to x / y clamped to -0x400..0x3ff). */
        ".globl gte_rtps_float\n"
        ".type gte_rtps_float,@function\n"
        "gte_rtps_float:\n"
        "    push ebp\n" /* 4266d0 */
        "    mov ebp, esp\n" /* 4266d1 */
        "    sub esp, 0xc\n" /* 4266d3 */
        "    push ebx\n" /* 4266d6 */
        "    push esi\n" /* 4266d7 */
        "    push edi\n" /* 4266d8 */
        "    mov dword ptr [ebp - 0x8], 0xc4800000\n" /* 4266d9 */
        "    mov dword ptr [ebp - 0x4], 0x447fc000\n" /* 4266e0 */
        "    fild word ptr [0x6ccc64]\n" /* 4266e7 */
        "    fild word ptr [0x6ccc66]\n" /* 4266ed */
        "    fild word ptr [0x6ccc68]\n" /* 4266f3 */
        "    fld dword ptr [0x6ccd14]\n" /* 4266f9 */
        "    fld dword ptr [0x6ccd3c]\n" /* 4266ff */
        "    fmul st, st(4)\n" /* 426705 */
        "    fld dword ptr [0x6ccd40]\n" /* 426707 */
        "    fmul st, st(4)\n" /* 42670d */
        "    fxch st(1)\n" /* 42670f */
        "    faddp st(2), st\n" /* 426711 */
        "    fld dword ptr [0x6ccd44]\n" /* 426713 */
        "    fmul st, st(3)\n" /* 426719 */
        "    fxch st(1)\n" /* 42671b */
        "    faddp st(2), st\n" /* 42671d */
        "    faddp st(1), st\n" /* 42671f */
        "    fistp dword ptr [0x6cccd0]\n" /* 426721 */
        "    fld dword ptr [0x6ccd0c]\n" /* 426727 */
        "    fld dword ptr [0x6ccd1c]\n" /* 42672d */
        "    fmul st, st(4)\n" /* 426733 */
        "    fld dword ptr [0x6ccd20]\n" /* 426735 */
        "    fmul st, st(4)\n" /* 42673b */
        "    fxch st(1)\n" /* 42673d */
        "    faddp st(2), st\n" /* 42673f */
        "    fld dword ptr [0x6ccd24]\n" /* 426741 */
        "    fmul st, st(3)\n" /* 426747 */
        "    fxch st(1)\n" /* 426749 */
        "    faddp st(2), st\n" /* 42674b */
        "    faddp st(1), st\n" /* 42674d */
        "    fistp dword ptr [0x6cccc8]\n" /* 42674f */
        "    fld dword ptr [0x6ccd10]\n" /* 426755 */
        "    fld dword ptr [0x6ccd2c]\n" /* 42675b */
        "    fmul st, st(4)\n" /* 426761 */
        "    fld dword ptr [0x6ccd30]\n" /* 426763 */
        "    fmul st, st(4)\n" /* 426769 */
        "    fxch st(1)\n" /* 42676b */
        "    faddp st(2), st\n" /* 42676d */
        "    fld dword ptr [0x6ccd34]\n" /* 42676f */
        "    fmul st, st(3)\n" /* 426775 */
        "    fxch st(1)\n" /* 426777 */
        "    faddp st(2), st\n" /* 426779 */
        "    faddp st(1), st\n" /* 42677b */
        "    fistp dword ptr [0x6ccccc]\n" /* 42677d */
        "    fcompp\n" /* 426783 */
        "    fstp dword ptr [ebp - 0xc]\n" /* 426785 */
        "    fld dword ptr [0x6ccd00]\n" /* 426788 */
        "    cmp dword ptr [0x6cccd0], 0x2\n" /* 42678e */
        "    jle .Lgte_rtps_float_42679f\n" /* 426795 */
        "    fild dword ptr [0x6cccd0]\n" /* 426797 */
        "    fdivp st(1), st\n" /* 42679d */
        ".Lgte_rtps_float_42679f:\n"
        "    mov edx, dword ptr [0x6cccc8]\n" /* 42679f */
        "    mov eax, 0xffff8000\n" /* 4267a5 */
        "    mov ebx, 0x7fff\n" /* 4267aa */
        "    mov esi, 0x3ff\n" /* 4267af */
        "    mov edi, 0xfffffc00\n" /* 4267b4 */
        "    cmp edx, ebx\n" /* 4267b9 */
        "    jle .Lgte_rtps_float_4267bf\n" /* 4267bb */
        "    mov edx, ebx\n" /* 4267bd */
        ".Lgte_rtps_float_4267bf:\n"
        "    cmp edx, eax\n" /* 4267bf */
        "    jge .Lgte_rtps_float_4267c5\n" /* 4267c1 */
        "    mov edx, eax\n" /* 4267c3 */
        ".Lgte_rtps_float_4267c5:\n"
        "    mov dword ptr [0x6ccc88], edx\n" /* 4267c5 */
        "    cmp edx, esi\n" /* 4267cb */
        "    jle .Lgte_rtps_float_4267d1\n" /* 4267cd */
        "    mov edx, esi\n" /* 4267cf */
        ".Lgte_rtps_float_4267d1:\n"
        "    cmp edx, edi\n" /* 4267d1 */
        "    jge .Lgte_rtps_float_4267d7\n" /* 4267d3 */
        "    mov edx, edi\n" /* 4267d5 */
        ".Lgte_rtps_float_4267d7:\n"
        "    mov ecx, dword ptr [0x6ccccc]\n" /* 4267d7 */
        "    mov word ptr [0x6ccc9c], dx\n" /* 4267dd */
        "    cmp ecx, ebx\n" /* 4267e4 */
        "    jle .Lgte_rtps_float_4267ea\n" /* 4267e6 */
        "    mov ecx, ebx\n" /* 4267e8 */
        ".Lgte_rtps_float_4267ea:\n"
        "    cmp ecx, eax\n" /* 4267ea */
        "    jge .Lgte_rtps_float_4267f0\n" /* 4267ec */
        "    mov ecx, eax\n" /* 4267ee */
        ".Lgte_rtps_float_4267f0:\n"
        "    mov dword ptr [0x6ccc8c], ecx\n" /* 4267f0 */
        "    cmp ecx, esi\n" /* 4267f6 */
        "    jle .Lgte_rtps_float_4267fc\n" /* 4267f8 */
        "    mov ecx, esi\n" /* 4267fa */
        ".Lgte_rtps_float_4267fc:\n"
        "    cmp ecx, edi\n" /* 4267fc */
        "    jge .Lgte_rtps_float_426802\n" /* 4267fe */
        "    mov ecx, edi\n" /* 426800 */
        ".Lgte_rtps_float_426802:\n"
        "    mov esi, 0xffff\n" /* 426802 */
        "    mov edx, dword ptr [0x6cccd0]\n" /* 426807 */
        "    mov word ptr [0x6ccc9e], cx\n" /* 42680d */
        "    mov edi, 0x1\n" /* 426814 */
        "    cmp edx, ebx\n" /* 426819 */
        "    jle .Lgte_rtps_float_42681f\n" /* 42681b */
        "    mov edx, ebx\n" /* 42681d */
        ".Lgte_rtps_float_42681f:\n"
        "    cmp edx, eax\n" /* 42681f */
        "    jge .Lgte_rtps_float_426825\n" /* 426821 */
        "    mov edx, eax\n" /* 426823 */
        ".Lgte_rtps_float_426825:\n"
        "    mov dword ptr [0x6ccc90], edx\n" /* 426825 */
        "    cmp edx, esi\n" /* 42682b */
        "    jle .Lgte_rtps_float_426831\n" /* 42682d */
        "    mov edx, esi\n" /* 42682f */
        ".Lgte_rtps_float_426831:\n"
        "    cmp edx, edi\n" /* 426831 */
        "    jge .Lgte_rtps_float_426837\n" /* 426833 */
        "    mov edx, edi\n" /* 426835 */
        ".Lgte_rtps_float_426837:\n"
        "    mov dword ptr [0x6cccac], edx\n" /* 426837 */
        "    mov dword ptr [0x6cccb0], edx\n" /* 42683d */
        "    fild word ptr [0x6ccc42]\n" /* 426843 */
        "    fild dword ptr [0x6ccc88]\n" /* 426849 */
        "    fmul st, st(2)\n" /* 42684f */
        "    faddp st(1), st\n" /* 426851 */
        "    fcom dword ptr [ebp - 0x8]\n" /* 426853 */
        "    wait\n" /* 426856 */
        "    fnstsw ax\n" /* 426857 */
        "    test ah, 0x1\n" /* 426859 */
        "    je .Lgte_rtps_float_426863\n" /* 42685c */
        "    fcomp st(1)\n" /* 42685e */
        "    fld dword ptr [ebp - 0x8]\n" /* 426860 */
        ".Lgte_rtps_float_426863:\n"
        "    fcom dword ptr [ebp - 0x4]\n" /* 426863 */
        "    wait\n" /* 426866 */
        "    fnstsw ax\n" /* 426867 */
        "    test ah, 0x1\n" /* 426869 */
        "    jne .Lgte_rtps_float_426873\n" /* 42686c */
        "    fcomp st(1)\n" /* 42686e */
        "    fld dword ptr [ebp - 0x4]\n" /* 426870 */
        ".Lgte_rtps_float_426873:\n"
        "    fistp word ptr [0x6ccc9c]\n" /* 426873 */
        "    fild word ptr [0x6ccc46]\n" /* 426879 */
        "    fild dword ptr [0x6ccc8c]\n" /* 42687f */
        "    fmul st, st(2)\n" /* 426885 */
        "    faddp st(1), st\n" /* 426887 */
        "    fcom dword ptr [ebp - 0x8]\n" /* 426889 */
        "    wait\n" /* 42688c */
        "    fnstsw ax\n" /* 42688d */
        "    test ah, 0x1\n" /* 42688f */
        "    je .Lgte_rtps_float_426899\n" /* 426892 */
        "    fcomp st(1)\n" /* 426894 */
        "    fld dword ptr [ebp - 0x8]\n" /* 426896 */
        ".Lgte_rtps_float_426899:\n"
        "    fcom dword ptr [ebp - 0x4]\n" /* 426899 */
        "    wait\n" /* 42689c */
        "    fnstsw ax\n" /* 42689d */
        "    test ah, 0x1\n" /* 42689f */
        "    jne .Lgte_rtps_float_4268a9\n" /* 4268a2 */
        "    fcomp st(1)\n" /* 4268a4 */
        "    fld dword ptr [ebp - 0x4]\n" /* 4268a6 */
        ".Lgte_rtps_float_4268a9:\n"
        "    fistp word ptr [0x6ccc9e]\n" /* 4268a9 */
        "    fcomp st(1)\n" /* 4268af */
        "    pop edi\n" /* 4268b1 */
        "    pop esi\n" /* 4268b2 */
        "    pop ebx\n" /* 4268b3 */
        "    mov esp, ebp\n" /* 4268b4 */
        "    pop ebp\n" /* 4268b6 */
        "    ret\n" /* 4268b7 */


        /* 0x4268c0 (through 0x6ccd78 / 0x6ccd7c when 3DNow! is present): the
         * float transform of the GTE vector into a vertex record (code, z, x, y,
         * view x, view y): z, view x / y with pfmul / pfacc, codes 0x10 (z below
         * the near plane, cleared when not) / 0x20 (beyond the far plane), and
         * when in front the projection (pfrcp of z) with the screen codes 1/2/4/8. */
        ".globl gte_rtps_3dnow\n"
        ".type gte_rtps_3dnow,@function\n"
        "gte_rtps_3dnow:\n"
        "    push ebp\n" /* 4268c0 */
        "    mov ebp, esp\n" /* 4268c1 */
        "    sub esp, 0x10\n" /* 4268c3 */
        "    push ebx\n" /* 4268c6 */
        "    femms\n" /* 4268c7 */
        "    mov ebx, dword ptr [ebp + 0x8]\n" /* 4268c9 */
        "    movsx eax, word ptr [0x6ccc64]\n" /* 4268cc */
        "    movsx ecx, word ptr [0x6ccc66]\n" /* 4268d3 */
        "    movsx edx, word ptr [0x6ccc68]\n" /* 4268da */
        "    mov dword ptr [ebp - 0x10], eax\n" /* 4268e1 */
        "    mov dword ptr [ebp - 0xc], ecx\n" /* 4268e4 */
        "    mov dword ptr [ebp - 0x8], edx\n" /* 4268e7 */
        "    mov eax, dword ptr [0x6ccbf4]\n" /* 4268ea */
        "    movq mm6, qword ptr [ebp - 0x10]\n" /* 4268ef */
        "    lea ecx, [ebp - 0x8]\n" /* 4268f3 */
        "    mov dword ptr [ebp - 0x4], eax\n" /* 4268f9 */
        "    mov eax, dword ptr [0x6ccbf8]\n" /* 4268fc */
        "    pi2fd mm0, mm6\n" /* 426901 */
        "    pi2fd mm1, qword ptr [ecx]\n" /* 426905 */
        "    mov dword ptr [ebp - 0x4], eax\n" /* 426909 */
        "    mov eax, dword ptr [0x6ccbfc]\n" /* 42690c */
        "    movq mm2, mm0\n" /* 426911 */
        "    pi2fd mm3, qword ptr [ecx]\n" /* 426914 */
        "    mov dword ptr [ebp - 0x4], eax\n" /* 426918 */
        "    movq mm4, mm0\n" /* 42691b */
        "    pi2fd mm5, qword ptr [ecx]\n" /* 42691e */
        "    movq mm6, qword ptr [0x6ccd3c]\n" /* 426922 */
        "    movq mm7, qword ptr [0x6ccd44]\n" /* 426929 */
        "    pfmul mm4, mm6\n" /* 426930 */
        "    pfmul mm5, mm7\n" /* 426934 */
        "    movq mm6, qword ptr [0x6ccd1c]\n" /* 426938 */
        "    movq mm7, qword ptr [0x6ccd24]\n" /* 42693f */
        "    pfmul mm0, mm6\n" /* 426946 */
        "    pfadd mm4, mm5\n" /* 42694a */
        "    pfmul mm1, mm7\n" /* 42694e */
        "    pfacc mm4, mm4\n" /* 426952 */
        "    movq mm6, qword ptr [0x6ccd2c]\n" /* 426956 */
        "    movq mm7, qword ptr [0x6ccd34]\n" /* 42695d */
        "    pfmul mm2, mm6\n" /* 426964 */
        "    pfadd mm0, mm1\n" /* 426968 */
        "    pfmul mm3, mm7\n" /* 42696c */
        "    pfacc mm0, mm0\n" /* 426970 */
        "    movd dword ptr [ebx + 0x4], mm4\n" /* 426974 */
        "    pfadd mm2, mm3\n" /* 426978 */
        "    mov edx, 0x10\n" /* 42697c */
        "    pfacc mm2, mm2\n" /* 426981 */
        "    mov eax, dword ptr [ebx + 0x4]\n" /* 426985 */
        "    movd dword ptr [ebx + 0x10], mm0\n" /* 426988 */
        "    movd dword ptr [ebx + 0x14], mm2\n" /* 42698c */
        "    cmp eax, dword ptr [0x6ccd08]\n" /* 426990 */
        "    jl .Lgte_rtps_3dnow_42699b\n" /* 426996 */
        "    or edx, 0x20\n" /* 426998 */
        ".Lgte_rtps_3dnow_42699b:\n"
        "    cmp eax, dword ptr [0x6ccd04]\n" /* 42699b */
        "    jl .Lgte_rtps_3dnow_426a11\n" /* 4269a1 */
        "    movd mm0, eax\n" /* 4269a3 */
        "    pfrcp mm0, mm0\n" /* 4269a6 */
        "    movd mm1, dword ptr [0x6ccd00]\n" /* 4269aa */
        "    pfmul mm0, mm1\n" /* 4269b1 */
        "    and edx, -0x11\n" /* 4269b5 */
        "    movd dword ptr [ebp - 0x10], mm0\n" /* 4269b8 */
        "    movd dword ptr [ebp - 0xc], mm0\n" /* 4269bc */
        "    movq mm1, qword ptr [ebp - 0x10]\n" /* 4269c0 */
        "    movq mm0, qword ptr [ebx + 0x10]\n" /* 4269c4 */
        "    pfmul mm0, mm1\n" /* 4269c8 */
        "    movq mm1, qword ptr [0x6ccd5c]\n" /* 4269cc */
        "    pfadd mm0, mm1\n" /* 4269d3 */
        "    movq qword ptr [ebx + 0x8], mm0\n" /* 4269d7 */
        "    mov eax, dword ptr [ebx + 0x8]\n" /* 4269db */
        "    mov ecx, dword ptr [ebx + 0xc]\n" /* 4269de */
        "    cmp eax, dword ptr [0x6ccd64]\n" /* 4269e1 */
        "    jge .Lgte_rtps_3dnow_4269ee\n" /* 4269e7 */
        "    or edx, 0x1\n" /* 4269e9 */
        "    jmp .Lgte_rtps_3dnow_4269f9\n" /* 4269ec */
        ".Lgte_rtps_3dnow_4269ee:\n"
        "    cmp eax, dword ptr [0x6ccd68]\n" /* 4269ee */
        "    jl .Lgte_rtps_3dnow_4269f9\n" /* 4269f4 */
        "    or edx, 0x2\n" /* 4269f6 */
        ".Lgte_rtps_3dnow_4269f9:\n"
        "    cmp ecx, dword ptr [0x6ccd6c]\n" /* 4269f9 */
        "    jge .Lgte_rtps_3dnow_426a06\n" /* 4269ff */
        "    or edx, 0x4\n" /* 426a01 */
        "    jmp .Lgte_rtps_3dnow_426a11\n" /* 426a04 */
        ".Lgte_rtps_3dnow_426a06:\n"
        "    cmp ecx, dword ptr [0x6ccd70]\n" /* 426a06 */
        "    jl .Lgte_rtps_3dnow_426a11\n" /* 426a0c */
        "    or edx, 0x8\n" /* 426a0e */
        ".Lgte_rtps_3dnow_426a11:\n"
        "    mov dword ptr [ebx], edx\n" /* 426a11 */
        "    femms\n" /* 426a13 */
        "    pop ebx\n" /* 426a15 */
        "    mov esp, ebp\n" /* 426a16 */
        "    pop ebp\n" /* 426a18 */
        "    ret\n" /* 426a19 */


        /* 0x426cd0: the float transform's z into the record 0x6ccbc0 (code 0x10
         * first); returns z rounded. When z >= 0 also view x / y (+0x10 / +0x14),
         * code 0x20 past the far plane and, from the near plane, st(0) = H / z
         * (else z) left on the x87 stack for 0x426dc0, and *0x6ccd74 = out (the
         * argument). When z < 0 the stack is left empty. */
        ".globl gte_rt_depth\n"
        ".type gte_rt_depth,@function\n"
        "gte_rt_depth:\n"
        "    push ebp\n" /* 426cd0 */
        "    mov ebp, esp\n" /* 426cd1 */
        "    sub esp, 0x8\n" /* 426cd3 */
        "    push ebx\n" /* 426cd6 */
        "    mov ebx, 0x6ccbc0\n" /* 426cd7 */
        "    fild word ptr [0x6ccc64]\n" /* 426cdc */
        "    fild word ptr [0x6ccc66]\n" /* 426ce2 */
        "    fild word ptr [0x6ccc68]\n" /* 426ce8 */
        "    mov dword ptr [ebx], 0x10\n" /* 426cee */
        "    mov edx, dword ptr [ebp + 0x8]\n" /* 426cf4 */
        "    fld dword ptr [0x6ccd14]\n" /* 426cf7 */
        "    fld dword ptr [0x6ccd3c]\n" /* 426cfd */
        "    fmul st, st(4)\n" /* 426d03 */
        "    fld dword ptr [0x6ccd40]\n" /* 426d05 */
        "    fmul st, st(4)\n" /* 426d0b */
        "    fxch st(1)\n" /* 426d0d */
        "    faddp st(2), st\n" /* 426d0f */
        "    fld dword ptr [0x6ccd44]\n" /* 426d11 */
        "    fmul st, st(3)\n" /* 426d17 */
        "    fxch st(1)\n" /* 426d19 */
        "    faddp st(2), st\n" /* 426d1b */
        "    faddp st(1), st\n" /* 426d1d */
        "    fst dword ptr [ebx + 0x4]\n" /* 426d1f */
        "    fistp dword ptr [ebp - 0x4]\n" /* 426d22 */
        "    test dword ptr [ebp - 0x4], 0x80000000\n" /* 426d25 */
        "    je .Lgte_rt_depth_426d35\n" /* 426d2c */
        "    fcompp\n" /* 426d2e */
        "    fstp dword ptr [ebp - 0x8]\n" /* 426d30 */
        "    jmp .Lgte_rt_depth_426db2\n" /* 426d33 */
        ".Lgte_rt_depth_426d35:\n"
        "    fld dword ptr [0x6ccd0c]\n" /* 426d35 */
        "    fld dword ptr [0x6ccd1c]\n" /* 426d3b */
        "    fmul st, st(4)\n" /* 426d41 */
        "    fld dword ptr [0x6ccd20]\n" /* 426d43 */
        "    fmul st, st(4)\n" /* 426d49 */
        "    fxch st(1)\n" /* 426d4b */
        "    faddp st(2), st\n" /* 426d4d */
        "    fld dword ptr [0x6ccd24]\n" /* 426d4f */
        "    fmul st, st(3)\n" /* 426d55 */
        "    fxch st(1)\n" /* 426d57 */
        "    faddp st(2), st\n" /* 426d59 */
        "    faddp st(1), st\n" /* 426d5b */
        "    fstp dword ptr [ebx + 0x10]\n" /* 426d5d */
        "    fld dword ptr [0x6ccd10]\n" /* 426d60 */
        "    fld dword ptr [0x6ccd2c]\n" /* 426d66 */
        "    fmul st, st(4)\n" /* 426d6c */
        "    fld dword ptr [0x6ccd30]\n" /* 426d6e */
        "    fmul st, st(4)\n" /* 426d74 */
        "    fxch st(1)\n" /* 426d76 */
        "    faddp st(2), st\n" /* 426d78 */
        "    fld dword ptr [0x6ccd34]\n" /* 426d7a */
        "    fmul st, st(3)\n" /* 426d80 */
        "    fxch st(1)\n" /* 426d82 */
        "    faddp st(2), st\n" /* 426d84 */
        "    faddp st(1), st\n" /* 426d86 */
        "    fstp dword ptr [ebx + 0x14]\n" /* 426d88 */
        "    fcompp\n" /* 426d8b */
        "    fstp dword ptr [ebp - 0x8]\n" /* 426d8d */
        "    fld dword ptr [ebx + 0x4]\n" /* 426d90 */
        "    mov eax, dword ptr [ebx + 0x4]\n" /* 426d93 */
        "    cmp eax, dword ptr [0x6ccd08]\n" /* 426d96 */
        "    jl .Lgte_rt_depth_426da1\n" /* 426d9c */
        "    or dword ptr [ebx], 0x20\n" /* 426d9e */
        ".Lgte_rt_depth_426da1:\n"
        "    cmp eax, dword ptr [0x6ccd04]\n" /* 426da1 */
        "    jl .Lgte_rt_depth_426db2\n" /* 426da7 */
        "    fdivr dword ptr [0x6ccd00]\n" /* 426da9 */
        "    and dword ptr [ebx], -0x11\n" /* 426daf */
        ".Lgte_rt_depth_426db2:\n"
        "    mov dword ptr [0x6ccd74], edx\n" /* 426db2 */
        "    mov eax, dword ptr [ebp - 0x4]\n" /* 426db8 */
        "    pop ebx\n" /* 426dbb */
        "    mov esp, ebp\n" /* 426dbc */
        "    pop ebp\n" /* 426dbe */
        "    ret\n" /* 426dbf */


        /* 0x426dc0: completes 0x426cd0: unless code 0x10, the screen point
         * (view x / y * st(0) + the offset 0x6ccd4c / 50) as two words at
         * *0x6ccd74; pops st(0) either way. */
        ".globl gte_project_pending\n"
        ".type gte_project_pending,@function\n"
        "gte_project_pending:\n"
        "    push ebp\n" /* 426dc0 */
        "    mov ebp, esp\n" /* 426dc1 */
        "    push ecx\n" /* 426dc3 */
        "    push ebx\n" /* 426dc4 */
        "    mov ebx, 0x6ccbc0\n" /* 426dc5 */
        "    mov edx, dword ptr [0x6ccd74]\n" /* 426dca */
        "    test dword ptr [ebx], 0x10\n" /* 426dd0 */
        "    jne .Lgte_project_pending_426df9\n" /* 426dd6 */
        "    fld dword ptr [ebx + 0x10]\n" /* 426dd8 */
        "    fmul st, st(1)\n" /* 426ddb */
        "    fld dword ptr [ebx + 0x14]\n" /* 426ddd */
        "    fmul st, st(2)\n" /* 426de0 */
        "    fxch st(1)\n" /* 426de2 */
        "    fadd dword ptr [0x6ccd4c]\n" /* 426de4 */
        "    fxch st(1)\n" /* 426dea */
        "    fadd dword ptr [0x6ccd50]\n" /* 426dec */
        "    fxch st(1)\n" /* 426df2 */
        "    fistp word ptr [edx]\n" /* 426df4 */
        "    fistp word ptr [edx + 0x2]\n" /* 426df6 */
        ".Lgte_project_pending_426df9:\n"
        "    fstp dword ptr [ebp - 0x4]\n" /* 426df9 */
        "    pop ebx\n" /* 426dfc */
        "    mov esp, ebp\n" /* 426dfd */
        "    pop ebp\n" /* 426dff */
        "    ret\n" /* 426e00 */


        /* 0x427250 (through 0x6cce3c with 3DNow!): the float light colour: the
         * light colour matrix 0x6ccdd0.. times the GTE vector, clamped at 0, times
         * the light matrix 0x6cce00.. plus the back colour 0x6cce30.., clamped at
         * 4095, >> 4 into 0x6cccbc..be (code byte copied). */
        ".globl gte_light_colour_3dnow\n"
        ".type gte_light_colour_3dnow,@function\n"
        "gte_light_colour_3dnow:\n"
        "    push ebp\n" /* 427250 */
        "    mov ebp, esp\n" /* 427251 */
        "    sub esp, 0x20\n" /* 427253 */
        "    mov dword ptr [ebp - 0x10], 0x0\n" /* 427256 */
        "    mov dword ptr [ebp - 0x8], 0x457ff000\n" /* 42725d */
        "    femms\n" /* 427264 */
        "    movsx eax, word ptr [0x6ccc64]\n" /* 427266 */
        "    movsx ecx, word ptr [0x6ccc66]\n" /* 42726d */
        "    movsx edx, word ptr [0x6ccc68]\n" /* 427274 */
        "    mov dword ptr [ebp - 0x20], eax\n" /* 42727b */
        "    mov dword ptr [ebp - 0x1c], ecx\n" /* 42727e */
        "    mov dword ptr [ebp - 0x18], edx\n" /* 427281 */
        "    mov dword ptr [ebp - 0x14], 0x0\n" /* 427284 */
        "    lea ecx, [ebp - 0x20]\n" /* 42728b */
        "    lea edx, [ebp - 0x18]\n" /* 427291 */
        "    pi2fd mm0, qword ptr [ecx]\n" /* 427297 */
        "    pi2fd mm1, qword ptr [edx]\n" /* 42729b */
        "    movq mm2, mm0\n" /* 42729f */
        "    movq mm3, mm1\n" /* 4272a2 */
        "    movq mm4, mm0\n" /* 4272a5 */
        "    movq mm5, mm1\n" /* 4272a8 */
        "    lea ecx, [0x6ccdd0]\n" /* 4272ab */
        "    lea edx, [0x6ccdd8]\n" /* 4272b1 */
        "    pfmul mm0, qword ptr [ecx]\n" /* 4272b7 */
        "    pfmul mm1, qword ptr [edx]\n" /* 4272bb */
        "    lea ecx, [0x6ccde0]\n" /* 4272bf */
        "    lea edx, [0x6ccde8]\n" /* 4272c5 */
        "    pfmul mm2, qword ptr [ecx]\n" /* 4272cb */
        "    pfadd mm0, mm1\n" /* 4272cf */
        "    pfmul mm3, qword ptr [edx]\n" /* 4272d3 */
        "    lea ecx, [0x6ccdf0]\n" /* 4272d7 */
        "    lea edx, [0x6ccdf8]\n" /* 4272dd */
        "    pfmul mm4, qword ptr [ecx]\n" /* 4272e3 */
        "    pfadd mm2, mm3\n" /* 4272e7 */
        "    pfmul mm5, qword ptr [edx]\n" /* 4272eb */
        "    pfacc mm0, mm0\n" /* 4272ef */
        "    movd mm6, dword ptr [ebp - 0x10]\n" /* 4272f3 */
        "    pfacc mm2, mm2\n" /* 4272f7 */
        "    pfadd mm4, mm5\n" /* 4272fb */
        "    pfacc mm4, mm4\n" /* 4272ff */
        "    pfmax mm0, mm6\n" /* 427303 */
        "    pfmax mm2, mm6\n" /* 427307 */
        "    movd dword ptr [ebp - 0x20], mm0\n" /* 42730b */
        "    pfmax mm4, mm6\n" /* 42730f */
        "    movd dword ptr [ebp - 0x1c], mm2\n" /* 427313 */
        "    movd dword ptr [ebp - 0x18], mm4\n" /* 427317 */
        "    mov eax, dword ptr [0x6cce30]\n" /* 42731b */
        "    mov dword ptr [ebp - 0x14], eax\n" /* 427320 */
        "    movq mm0, qword ptr [ebp - 0x20]\n" /* 427323 */
        "    mov eax, dword ptr [0x6cce34]\n" /* 427327 */
        "    movq mm1, qword ptr [ebp - 0x18]\n" /* 42732c */
        "    mov dword ptr [ebp - 0x14], eax\n" /* 427330 */
        "    movq mm2, mm0\n" /* 427333 */
        "    mov eax, dword ptr [0x6cce38]\n" /* 427336 */
        "    movq mm3, qword ptr [ebp - 0x18]\n" /* 42733b */
        "    mov dword ptr [ebp - 0x14], eax\n" /* 42733f */
        "    movq mm4, mm0\n" /* 427342 */
        "    movq mm5, qword ptr [ebp - 0x18]\n" /* 427345 */
        "    lea ecx, [0x6cce00]\n" /* 427349 */
        "    lea edx, [0x6cce08]\n" /* 42734f */
        "    pfmul mm0, qword ptr [ecx]\n" /* 427355 */
        "    pfmul mm1, qword ptr [edx]\n" /* 427359 */
        "    lea ecx, [0x6cce10]\n" /* 42735d */
        "    lea edx, [0x6cce18]\n" /* 427363 */
        "    pfmul mm2, qword ptr [ecx]\n" /* 427369 */
        "    pfadd mm0, mm1\n" /* 42736d */
        "    pfmul mm3, qword ptr [edx]\n" /* 427371 */
        "    lea ecx, [0x6cce20]\n" /* 427375 */
        "    lea edx, [0x6cce28]\n" /* 42737b */
        "    pfmul mm4, qword ptr [ecx]\n" /* 427381 */
        "    pfadd mm2, mm3\n" /* 427385 */
        "    pfmul mm5, qword ptr [edx]\n" /* 427389 */
        "    pfacc mm0, mm0\n" /* 42738d */
        "    movd mm6, dword ptr [ebp - 0x8]\n" /* 427391 */
        "    pfacc mm2, mm2\n" /* 427395 */
        "    pfadd mm4, mm5\n" /* 427399 */
        "    pfacc mm4, mm4\n" /* 42739d */
        "    pfmin mm0, mm6\n" /* 4273a1 */
        "    pfmin mm2, mm6\n" /* 4273a5 */
        "    pf2id mm0, mm0\n" /* 4273a9 */
        "    pfmin mm4, mm6\n" /* 4273ad */
        "    movd dword ptr [ebp - 0x20], mm0\n" /* 4273b1 */
        "    pf2id mm2, mm2\n" /* 4273b5 */
        "    mov eax, dword ptr [ebp - 0x20]\n" /* 4273b9 */
        "    pf2id mm4, mm4\n" /* 4273bc */
        "    shr eax, 0x4\n" /* 4273c0 */
        "    movd dword ptr [ebp - 0x1c], mm2\n" /* 4273c3 */
        "    mov byte ptr [0x6cccbc], al\n" /* 4273c7 */
        "    movd dword ptr [ebp - 0x18], mm4\n" /* 4273cc */
        "    mov eax, dword ptr [ebp - 0x1c]\n" /* 4273d0 */
        "    mov ecx, dword ptr [ebp - 0x18]\n" /* 4273d3 */
        "    shr eax, 0x4\n" /* 4273d6 */
        "    shr ecx, 0x4\n" /* 4273d9 */
        "    mov byte ptr [0x6cccbd], al\n" /* 4273dc */
        "    mov byte ptr [0x6cccbe], cl\n" /* 4273e1 */
        "    femms\n" /* 4273e7 */
        "    mov al, byte ptr [0x6ccc7f]\n" /* 4273e9 */
        "    mov byte ptr [0x6cccbf], al\n" /* 4273ee */
        "    mov esp, ebp\n" /* 4273f3 */
        "    pop ebp\n" /* 4273f5 */
        "    ret\n" /* 4273f6 */


        /* 0x427400 (through 0x6cce3c): the same on the x87: rounded by the
         * control word and clamped below 0xfff before >> 4. */
        ".globl gte_light_colour_float\n"
        ".type gte_light_colour_float,@function\n"
        "gte_light_colour_float:\n"
        "    push ebp\n" /* 427400 */
        "    mov ebp, esp\n" /* 427401 */
        "    sub esp, 0x1c\n" /* 427403 */
        "    xor eax, eax\n" /* 427406 */
        "    fild word ptr [0x6ccc64]\n" /* 427408 */
        "    fild word ptr [0x6ccc66]\n" /* 42740e */
        "    fild word ptr [0x6ccc68]\n" /* 427414 */
        "    fld dword ptr [0x6ccdd0]\n" /* 42741a */
        "    fld dword ptr [0x6ccdd4]\n" /* 427420 */
        "    fld dword ptr [0x6ccdd8]\n" /* 427426 */
        "    fxch st(2)\n" /* 42742c */
        "    fmul st, st(5)\n" /* 42742e */
        "    fxch st(1)\n" /* 427430 */
        "    fmul st, st(4)\n" /* 427432 */
        "    fxch st(2)\n" /* 427434 */
        "    fmul st, st(3)\n" /* 427436 */
        "    faddp st(1), st\n" /* 427438 */
        "    faddp st(1), st\n" /* 42743a */
        "    fld dword ptr [0x6ccde0]\n" /* 42743c */
        "    fld dword ptr [0x6ccde4]\n" /* 427442 */
        "    fld dword ptr [0x6ccde8]\n" /* 427448 */
        "    fxch st(2)\n" /* 42744e */
        "    fmul st, st(6)\n" /* 427450 */
        "    fxch st(1)\n" /* 427452 */
        "    fmul st, st(5)\n" /* 427454 */
        "    fxch st(2)\n" /* 427456 */
        "    fmul st, st(4)\n" /* 427458 */
        "    faddp st(1), st\n" /* 42745a */
        "    faddp st(1), st\n" /* 42745c */
        "    fld dword ptr [0x6ccdf0]\n" /* 42745e */
        "    fld dword ptr [0x6ccdf4]\n" /* 427464 */
        "    fld dword ptr [0x6ccdf8]\n" /* 42746a */
        "    fxch st(2)\n" /* 427470 */
        "    fmul st, st(7)\n" /* 427472 */
        "    fxch st(1)\n" /* 427474 */
        "    fmul st, st(6)\n" /* 427476 */
        "    fxch st(2)\n" /* 427478 */
        "    fmul st, st(5)\n" /* 42747a */
        "    faddp st(1), st\n" /* 42747c */
        "    faddp st(1), st\n" /* 42747e */
        "    fxch st(1)\n" /* 427480 */
        "    fstp dword ptr [ebp - 0x8]\n" /* 427482 */
        "    fstp dword ptr [ebp - 0x4]\n" /* 427485 */
        "    fstp dword ptr [ebp - 0xc]\n" /* 427488 */
        "    fcompp\n" /* 42748b */
        "    fstp dword ptr [ebp - 0x10]\n" /* 42748d */
        "    cmp dword ptr [ebp - 0xc], eax\n" /* 427490 */
        "    jge .Lgte_light_colour_float_427498\n" /* 427493 */
        "    mov dword ptr [ebp - 0xc], eax\n" /* 427495 */
        ".Lgte_light_colour_float_427498:\n"
        "    cmp dword ptr [ebp - 0x8], eax\n" /* 427498 */
        "    jge .Lgte_light_colour_float_4274a0\n" /* 42749b */
        "    mov dword ptr [ebp - 0x8], eax\n" /* 42749d */
        ".Lgte_light_colour_float_4274a0:\n"
        "    cmp dword ptr [ebp - 0x4], eax\n" /* 4274a0 */
        "    jge .Lgte_light_colour_float_4274a8\n" /* 4274a3 */
        "    mov dword ptr [ebp - 0x4], eax\n" /* 4274a5 */
        ".Lgte_light_colour_float_4274a8:\n"
        "    fld dword ptr [ebp - 0xc]\n" /* 4274a8 */
        "    fld dword ptr [ebp - 0x8]\n" /* 4274ab */
        "    fld dword ptr [ebp - 0x4]\n" /* 4274ae */
        "    fld dword ptr [0x6cce00]\n" /* 4274b1 */
        "    fld dword ptr [0x6cce04]\n" /* 4274b7 */
        "    fld dword ptr [0x6cce08]\n" /* 4274bd */
        "    fxch st(2)\n" /* 4274c3 */
        "    fmul st, st(5)\n" /* 4274c5 */
        "    fxch st(1)\n" /* 4274c7 */
        "    fmul st, st(4)\n" /* 4274c9 */
        "    fxch st(2)\n" /* 4274cb */
        "    fmul st, st(3)\n" /* 4274cd */
        "    faddp st(1), st\n" /* 4274cf */
        "    faddp st(1), st\n" /* 4274d1 */
        "    fadd dword ptr [0x6cce30]\n" /* 4274d3 */
        "    fld dword ptr [0x6cce10]\n" /* 4274d9 */
        "    fld dword ptr [0x6cce14]\n" /* 4274df */
        "    fld dword ptr [0x6cce18]\n" /* 4274e5 */
        "    fxch st(2)\n" /* 4274eb */
        "    fmul st, st(6)\n" /* 4274ed */
        "    fxch st(1)\n" /* 4274ef */
        "    fmul st, st(5)\n" /* 4274f1 */
        "    fxch st(2)\n" /* 4274f3 */
        "    fmul st, st(4)\n" /* 4274f5 */
        "    faddp st(1), st\n" /* 4274f7 */
        "    faddp st(1), st\n" /* 4274f9 */
        "    fadd dword ptr [0x6cce34]\n" /* 4274fb */
        "    fld dword ptr [0x6cce20]\n" /* 427501 */
        "    fld dword ptr [0x6cce24]\n" /* 427507 */
        "    fld dword ptr [0x6cce28]\n" /* 42750d */
        "    fxch st(2)\n" /* 427513 */
        "    fmul st, st(7)\n" /* 427515 */
        "    fxch st(1)\n" /* 427517 */
        "    fmul st, st(6)\n" /* 427519 */
        "    fxch st(2)\n" /* 42751b */
        "    fmul st, st(5)\n" /* 42751d */
        "    faddp st(1), st\n" /* 42751f */
        "    faddp st(1), st\n" /* 427521 */
        "    fadd dword ptr [0x6cce38]\n" /* 427523 */
        "    fxch st(1)\n" /* 427529 */
        "    fistp dword ptr [ebp - 0x18]\n" /* 42752b */
        "    fistp dword ptr [ebp - 0x1c]\n" /* 42752e */
        "    fistp dword ptr [ebp - 0x14]\n" /* 427531 */
        "    fcompp\n" /* 427534 */
        "    fstp dword ptr [ebp - 0x10]\n" /* 427536 */
        "    mov eax, dword ptr [ebp - 0x14]\n" /* 427539 */
        "    cmp eax, 0xfff\n" /* 42753c */
        "    jl .Lgte_light_colour_float_427548\n" /* 427541 */
        "    mov eax, 0xfff\n" /* 427543 */
        ".Lgte_light_colour_float_427548:\n"
        "    sar eax, 0x4\n" /* 427548 */
        "    mov byte ptr [0x6cccbc], al\n" /* 42754b */
        "    mov eax, dword ptr [ebp - 0x18]\n" /* 427550 */
        "    cmp eax, 0xfff\n" /* 427553 */
        "    jl .Lgte_light_colour_float_42755f\n" /* 427558 */
        "    mov eax, 0xfff\n" /* 42755a */
        ".Lgte_light_colour_float_42755f:\n"
        "    sar eax, 0x4\n" /* 42755f */
        "    mov byte ptr [0x6cccbd], al\n" /* 427562 */
        "    mov eax, dword ptr [ebp - 0x1c]\n" /* 427567 */
        "    cmp eax, 0xfff\n" /* 42756a */
        "    jl .Lgte_light_colour_float_427576\n" /* 42756f */
        "    mov eax, 0xfff\n" /* 427571 */
        ".Lgte_light_colour_float_427576:\n"
        "    sar eax, 0x4\n" /* 427576 */
        "    mov byte ptr [0x6cccbe], al\n" /* 427579 */
        "    mov al, byte ptr [0x6ccc7f]\n" /* 42757e */
        "    mov byte ptr [0x6cccbf], al\n" /* 427583 */
        "    mov esp, ebp\n" /* 427588 */
        "    pop ebp\n" /* 42758a */
        "    ret\n" /* 42758b */


        /* 0x4278f0: the facing value of three projected vertices (x +8, y +0xc):
         * (c.y - a.y) * (b.x - a.x) - (b.y - a.y) * (c.x - a.x), left in st(0). */
        ".globl vec_facing_float\n"
        ".type vec_facing_float,@function\n"
        "vec_facing_float:\n"
        "    push ebp\n" /* 4278f0 */
        "    mov ebp, esp\n" /* 4278f1 */
        "    mov edx, dword ptr [ebp + 0x10]\n" /* 4278f3 */
        "    mov eax, dword ptr [ebp + 0x8]\n" /* 4278f6 */
        "    mov ecx, dword ptr [ebp + 0xc]\n" /* 4278f9 */
        "    fld dword ptr [edx + 0xc]\n" /* 4278fc */
        "    fsub dword ptr [eax + 0xc]\n" /* 4278ff */
        "    fld dword ptr [ecx + 0x8]\n" /* 427902 */
        "    fsub dword ptr [eax + 0x8]\n" /* 427905 */
        "    fld dword ptr [ecx + 0xc]\n" /* 427908 */
        "    fsub dword ptr [eax + 0xc]\n" /* 42790b */
        "    fxch st(1)\n" /* 42790e */
        "    fmulp st(2), st\n" /* 427910 */
        "    fld dword ptr [edx + 0x8]\n" /* 427912 */
        "    fsub dword ptr [eax + 0x8]\n" /* 427915 */
        "    fmulp st(1), st\n" /* 427918 */
        "    fsubp st(1), st\n" /* 42791a */
        "    pop ebp\n" /* 42791c */
        "    ret\n" /* 42791d */


        /* 0x4288c0: a vertex record's depth codes (0x10 near, 0x20 far) and,
         * from the near plane, st(0) = H / z, then the projection 0x426b00 on it
         * (*0x6ccd74 = the record). */
        ".globl vertex_project_codes\n"
        ".type vertex_project_codes,@function\n"
        "vertex_project_codes:\n"
        "    push ebp\n" /* 4288c0 */
        "    mov ebp, esp\n" /* 4288c1 */
        "    push ebx\n" /* 4288c3 */
        "    mov ebx, dword ptr [ebp + 0x8]\n" /* 4288c4 */
        "    fld dword ptr [ebx + 0x4]\n" /* 4288c7 */
        "    mov dword ptr [ebx], 0x10\n" /* 4288ca */
        "    mov eax, dword ptr [ebx + 0x4]\n" /* 4288d0 */
        "    cmp eax, dword ptr [0x6ccd08]\n" /* 4288d3 */
        "    jl .Lvertex_project_codes_4288de\n" /* 4288d9 */
        "    or dword ptr [ebx], 0x20\n" /* 4288db */
        ".Lvertex_project_codes_4288de:\n"
        "    cmp eax, dword ptr [0x6ccd04]\n" /* 4288de */
        "    jl .Lvertex_project_codes_4288ef\n" /* 4288e4 */
        "    fdivr dword ptr [0x6ccd00]\n" /* 4288e6 */
        "    and dword ptr [ebx], -0x11\n" /* 4288ec */
        ".Lvertex_project_codes_4288ef:\n"
        "    mov dword ptr [0x6ccd74], ebx\n" /* 4288ef */
        "    .att_syntax prefix\n"
        "    call 0x426b00\n" /* 4288f5 */
        "    .intel_syntax noprefix\n"
        "    pop ebx\n" /* 4288fa */
        "    pop ebp\n" /* 4288fb */
        "    ret\n" /* 4288fc */


        /* 0x428900: a vertex record projected: x / y (+8 / +0xc) = view x / y
         * (+0x10 / +0x14) * (H / z) + the offset 0x6ccd5c / 60. */
        ".globl vertex_project_float\n"
        ".type vertex_project_float,@function\n"
        "vertex_project_float:\n"
        "    push ebp\n" /* 428900 */
        "    mov ebp, esp\n" /* 428901 */
        "    push ecx\n" /* 428903 */
        "    mov ecx, dword ptr [ebp + 0x8]\n" /* 428904 */
        "    fld dword ptr [ecx + 0x4]\n" /* 428907 */
        "    fdivr dword ptr [0x6ccd00]\n" /* 42890a */
        "    fld dword ptr [ecx + 0x10]\n" /* 428910 */
        "    fmul st, st(1)\n" /* 428913 */
        "    fld dword ptr [ecx + 0x14]\n" /* 428915 */
        "    fmul st, st(2)\n" /* 428918 */
        "    fxch st(1)\n" /* 42891a */
        "    fadd dword ptr [0x6ccd5c]\n" /* 42891c */
        "    fxch st(1)\n" /* 428922 */
        "    fadd dword ptr [0x6ccd60]\n" /* 428924 */
        "    fxch st(1)\n" /* 42892a */
        "    fstp dword ptr [ecx + 0x8]\n" /* 42892c */
        "    fstp dword ptr [ecx + 0xc]\n" /* 42892f */
        "    fstp dword ptr [ebp - 0x4]\n" /* 428932 */
        "    mov esp, ebp\n" /* 428935 */
        "    pop ebp\n" /* 428937 */
        "    ret\n" /* 428938 */


        /* 0x428bd0: the far plane 0x6ccd08 in st(0). */
        ".globl far_plane_get\n"
        ".type far_plane_get,@function\n"
        "far_plane_get:\n"
        "    fld dword ptr [0x6ccd08]\n" /* 428bd0 */
        "    ret\n" /* 428bd6 */


        /* 0x4290b0: an int vector (<< 8 as floats) scaled to length 4096
         * through sqrt (0x4981d4, double), each component rounded to float then
         * by the control word into out. Returns 0. */
        ".globl vec_normalise_x87\n"
        ".type vec_normalise_x87,@function\n"
        "vec_normalise_x87:\n"
        "    push ebp\n" /* 4290b0 */
        "    mov ebp, esp\n" /* 4290b1 */
        "    sub esp, 0x10\n" /* 4290b3 */
        "    mov eax, dword ptr [ebp + 0x8]\n" /* 4290b6 */
        "    mov ecx, dword ptr [eax]\n" /* 4290b9 */
        "    shl ecx, 0x8\n" /* 4290bb */
        "    mov dword ptr [ebp + 0x8], ecx\n" /* 4290be */
        "    fild dword ptr [ebp + 0x8]\n" /* 4290c1 */
        "    mov edx, dword ptr [eax + 0x4]\n" /* 4290c4 */
        "    fstp dword ptr [ebp + 0x8]\n" /* 4290c7 */
        "    shl edx, 0x8\n" /* 4290ca */
        "    mov dword ptr [ebp - 0xc], edx\n" /* 4290cd */
        "    fild dword ptr [ebp - 0xc]\n" /* 4290d0 */
        "    mov eax, dword ptr [eax + 0x8]\n" /* 4290d3 */
        "    fstp dword ptr [ebp - 0x4]\n" /* 4290d6 */
        "    shl eax, 0x8\n" /* 4290d9 */
        "    mov dword ptr [ebp - 0xc], eax\n" /* 4290dc */
        "    fild dword ptr [ebp - 0xc]\n" /* 4290df */
        "    fstp dword ptr [ebp - 0x8]\n" /* 4290e2 */
        "    fld dword ptr [ebp - 0x8]\n" /* 4290e5 */
        "    fmul dword ptr [ebp - 0x8]\n" /* 4290e8 */
        "    fld dword ptr [ebp - 0x4]\n" /* 4290eb */
        "    fmul dword ptr [ebp - 0x4]\n" /* 4290ee */
        "    sub esp, 0x8\n" /* 4290f1 */
        "    faddp st(1), st\n" /* 4290f4 */
        "    fld dword ptr [ebp + 0x8]\n" /* 4290f6 */
        "    fmul dword ptr [ebp + 0x8]\n" /* 4290f9 */
        "    faddp st(1), st\n" /* 4290fc */
        "    fstp qword ptr [esp]\n" /* 4290fe */
        "    .att_syntax prefix\n"
        "    call 0x4981d4\n" /* 429101 */
        "    .intel_syntax noprefix\n"
        "    fstp dword ptr [ebp - 0xc]\n" /* 429106 */
        "    fld dword ptr [ebp + 0x8]\n" /* 429109 */
        "    fmul qword ptr [0x4a3378]\n" /* 42910c */
        "    fdiv dword ptr [ebp - 0xc]\n" /* 429112 */
        "    add esp, 0x8\n" /* 429115 */
        "    fstp dword ptr [ebp + 0x8]\n" /* 429118 */
        "    fld dword ptr [ebp + 0x8]\n" /* 42911b */
        "    fistp dword ptr [ebp - 0x10]\n" /* 42911e */
        "    fld dword ptr [ebp - 0x4]\n" /* 429121 */
        "    fmul qword ptr [0x4a3378]\n" /* 429124 */
        "    fdiv dword ptr [ebp - 0xc]\n" /* 42912a */
        "    mov eax, dword ptr [ebp + 0xc]\n" /* 42912d */
        "    mov ecx, dword ptr [ebp - 0x10]\n" /* 429130 */
        "    mov dword ptr [eax], ecx\n" /* 429133 */
        "    fstp dword ptr [ebp + 0x8]\n" /* 429135 */
        "    fld dword ptr [ebp + 0x8]\n" /* 429138 */
        "    fistp dword ptr [ebp + 0xc]\n" /* 42913b */
        "    fld dword ptr [ebp - 0x8]\n" /* 42913e */
        "    fmul qword ptr [0x4a3378]\n" /* 429141 */
        "    fdiv dword ptr [ebp - 0xc]\n" /* 429147 */
        "    mov edx, dword ptr [ebp + 0xc]\n" /* 42914a */
        "    mov dword ptr [eax + 0x4], edx\n" /* 42914d */
        "    fstp dword ptr [ebp + 0x8]\n" /* 429150 */
        "    fld dword ptr [ebp + 0x8]\n" /* 429153 */
        "    fistp dword ptr [ebp + 0xc]\n" /* 429156 */
        "    mov ecx, dword ptr [ebp + 0xc]\n" /* 429159 */
        "    mov dword ptr [eax + 0x8], ecx\n" /* 42915c */
        "    xor eax, eax\n" /* 42915f */
        "    mov esp, ebp\n" /* 429161 */
        "    pop ebp\n" /* 429163 */
        "    ret\n" /* 429164 */

        ".att_syntax prefix\n");
#else
/* x86-64: the same in C. Each x87 step is long double arithmetic in the
 * original's order (rounded by the control word as the registers were);
 * stores and integer conversions are the original's fstp / fistp. The
 * 3DNow! pair (never selected without 3DNow!) is single-precision C with an
 * exact reciprocal for pfrcp. */

/* fstp qword: rounded to a double by the control word. */
static inline double pc_fstp64(pc_ld v)
{
    double r;
    __asm__ volatile("fstpl %0" : "=m"(r) : "t"(v) : "st");
    return r;
}

/* A float at p + off, widened. */
#define PC_FP(p, off) ((pc_ld)pc_float(U32(p, off)))

/* One row of the float rotate-translate of the GTE vector: ((t + a * x) +
 * b * y) + c * z with t at tr, a / b / c at row. */
static inline pc_ld pc_rt_row(uint32_t tr, uint32_t row)
{
    pc_ld r = PC_F(tr) + PC_F(row) * PC_W(0x6ccc64);

    r = r + PC_F(row + 4) * PC_W(0x6ccc66);
    return r + PC_F(row + 8) * PC_W(0x6ccc68);
}

/* 0x426620. */
void gte_rt_float(void)
{
    GI32(0x6cccd0) = pc_fistp32(pc_rt_row(0x6ccd14, 0x6ccd3c));
    GI32(0x6cccc8) = pc_fistp32(pc_rt_row(0x6ccd0c, 0x6ccd1c));
    GI32(0x6ccccc) = pc_fistp32(pc_rt_row(0x6ccd10, 0x6ccd2c));
}

/* A screen coordinate: offset + v * h, below -1024 (or NaN) -1024, from
 * 1023.0 (0x447fc000) 1023.0, fistp word. */
static inline int16_t pc_screen(uint32_t offset, int32_t v, pc_ld h)
{
    pc_ld s = PC_W(offset) + (pc_ld)v * h;

    if (!(s >= -1024.0L))
        s = -1024.0L;
    if (s >= 1023.0L)
        s = 1023.0L;
    return pc_fistp16(s);
}

static inline int32_t pc_clamp(int32_t v, int32_t lo, int32_t hi)
{
    if (v > hi)
        v = hi;
    if (v < lo)
        v = lo;
    return v;
}

/* 0x4266d0. */
void gte_rtps_float(void)
{
    pc_ld h;
    int32_t v;

    gte_rt_float();
    h = PC_F(0x6ccd00);
    if (GI32(0x6cccd0) > 2)
        h = h / (pc_ld)GI32(0x6cccd0);
    v = pc_clamp(GI32(0x6cccc8), -0x8000, 0x7fff);
    GI32(0x6ccc88) = v;
    G16(0x6ccc9c) = (uint16_t)pc_clamp(v, -0x400, 0x3ff);
    v = pc_clamp(GI32(0x6ccccc), -0x8000, 0x7fff);
    GI32(0x6ccc8c) = v;
    G16(0x6ccc9e) = (uint16_t)pc_clamp(v, -0x400, 0x3ff);
    v = pc_clamp(GI32(0x6cccd0), -0x8000, 0x7fff);
    GI32(0x6ccc90) = v;
    v = pc_clamp(v, 1, 0xffff);
    GI32(0x6cccac) = v;
    GI32(0x6cccb0) = v;
    GS16(0x6ccc9c) = pc_screen(0x6ccc42, GI32(0x6ccc88), h);
    GS16(0x6ccc9e) = pc_screen(0x6ccc46, GI32(0x6ccc8c), h);
}

static inline float pc_gf(uint32_t a)
{
    return pc_float(G32(a));
}

static inline uint32_t pc_bits(float f)
{
    uint32_t r;

    memcpy(&r, &f, 4);
    return r;
}

/* 0x4268c0 (3DNow!): the vertex record (code, z, x, y, view x, view y);
 * leaves nothing for the projection (0x4291f0, a no-op on this path). */
float gte_rtps_3dnow(uint8_t *rec)
{
    float x = (float)GS16(0x6ccc64), y = (float)GS16(0x6ccc66), z = (float)GS16(0x6ccc68);
    float tx = (float)GI32(0x6ccbf4), ty = (float)GI32(0x6ccbf8), tz = (float)GI32(0x6ccbfc);
    uint32_t code = 0x10, zb;

    U32(rec, 4) = pc_bits((x * pc_gf(0x6ccd3c) + z * pc_gf(0x6ccd44)) + (y * pc_gf(0x6ccd40) + tz * pc_gf(0x6ccd48)));
    U32(rec, 0x10) = pc_bits((x * pc_gf(0x6ccd1c) + z * pc_gf(0x6ccd24)) + (y * pc_gf(0x6ccd20) + tx * pc_gf(0x6ccd28)));
    U32(rec, 0x14) = pc_bits((x * pc_gf(0x6ccd2c) + z * pc_gf(0x6ccd34)) + (y * pc_gf(0x6ccd30) + ty * pc_gf(0x6ccd38)));
    zb = U32(rec, 4);
    if ((int32_t)zb >= GI32(0x6ccd08))
        code |= 0x20;
    if ((int32_t)zb >= GI32(0x6ccd04)) {
        float k = 1.0f / pc_float(zb) * pc_gf(0x6ccd00);
        int32_t sx, sy;

        code &= ~0x10u;
        U32(rec, 8) = pc_bits(pc_float(U32(rec, 0x10)) * k + pc_gf(0x6ccd5c));
        U32(rec, 0xc) = pc_bits(pc_float(U32(rec, 0x14)) * k + pc_gf(0x6ccd60));
        sx = I32(rec, 8);
        sy = I32(rec, 0xc);
        if (sx < GI32(0x6ccd64))
            code |= 1;
        else if (sx >= GI32(0x6ccd68))
            code |= 2;
        if (sy < GI32(0x6ccd6c))
            code |= 4;
        else if (sy >= GI32(0x6ccd70))
            code |= 8;
    }
    U32(rec, 0) = code;
    return 0.0f;
}

/* 0x426cd0. */
int32_t gte_rt_depth_c(uint8_t *rec, float *st0)
{
    pc_ld z, r;
    int32_t zi, zb;

    G32(0x6ccbc0) = 0x10;
    z = pc_rt_row(0x6ccd14, 0x6ccd3c);
    G32(0x6ccbc4) = pc_fstp32(z); /* fst */
    zi = pc_fistp32(z);
    if (!((uint32_t)zi & 0x80000000u)) {
        G32(0x6ccbd0) = pc_fstp32(pc_rt_row(0x6ccd0c, 0x6ccd1c));
        G32(0x6ccbd4) = pc_fstp32(pc_rt_row(0x6ccd10, 0x6ccd2c));
        r = PC_F(0x6ccbc4);
        zb = GI32(0x6ccbc4);
        if (zb >= GI32(0x6ccd08))
            G32(0x6ccbc0) |= 0x20;
        if (zb >= GI32(0x6ccd04)) {
            r = PC_F(0x6ccd00) / r;
            G32(0x6ccbc0) &= ~0x10u;
        }
        *st0 = pc_float(pc_fstp32(r));
    }
    G32(0x6ccd74) = A(rec);
    return zi;
}

int32_t gte_rt_depth(uint8_t *rec, float *st0)
{
    return gte_rt_depth_c(rec, st0);
}

/* 0x426dc0. */
void gte_project_pending_c(float st0)
{
    if (!(G32(0x6ccbc0) & 0x10)) {
        uint32_t p = G32(0x6ccd74);
        pc_ld h = st0;

        *(int16_t *)GMEM(p) = pc_fistp16(PC_F(0x6ccbd0) * h + PC_F(0x6ccd4c));
        *(int16_t *)GMEM(p + 2) = pc_fistp16(PC_F(0x6ccbd4) * h + PC_F(0x6ccd50));
    }
}

void gte_project_pending(float st0)
{
    gte_project_pending_c(st0);
}

/* 0x427250 (3DNow!). The back colour's three loads go to one slot, so
 * every channel adds blue's (sic). */
void gte_light_colour_3dnow(void)
{
    float x = (float)GS16(0x6ccc64), y = (float)GS16(0x6ccc66), z = (float)GS16(0x6ccc68);
    float c[3], back = pc_gf(0x6cce38);
    uint32_t i;

    for (i = 0; i < 3; i++) {
        uint32_t m = 0x6ccdd0 + i * 0x10;
        float v = (x * pc_gf(m) + z * pc_gf(m + 8)) + (y * pc_gf(m + 4) + 0.0f * pc_gf(m + 0xc));

        c[i] = v > 0.0f ? v : 0.0f; /* pfmax */
    }
    for (i = 0; i < 3; i++) {
        uint32_t m = 0x6cce00 + i * 0x10;
        float v = (c[0] * pc_gf(m) + c[2] * pc_gf(m + 8)) + (c[1] * pc_gf(m + 4) + back * pc_gf(m + 0xc));

        if (!(v < 4095.0f)) /* pfmin */
            v = 4095.0f;
        G8(0x6cccbc + i) = (uint8_t)((uint32_t)(int32_t)v >> 4); /* pf2id truncates */
    }
    G8(0x6cccbf) = G8(0x6ccc7f);
}

/* 0x427400. */
void gte_light_colour_float(void)
{
    pc_ld x = PC_W(0x6ccc64), y = PC_W(0x6ccc66), z = PC_W(0x6ccc68), c[3];
    int32_t v[3];
    uint32_t i;

    for (i = 0; i < 3; i++) {
        uint32_t m = 0x6ccdd0 + i * 0x10;
        pc_ld r = PC_F(m) * x + PC_F(m + 8) * z;
        uint32_t f = pc_fstp32(r + PC_F(m + 4) * y);

        if ((int32_t)f < 0)
            f = 0;
        c[i] = (pc_ld)pc_float(f);
    }
    for (i = 0; i < 3; i++) {
        uint32_t m = 0x6cce00 + i * 0x10;
        pc_ld r = PC_F(m) * c[0] + PC_F(m + 8) * c[2];

        r = r + PC_F(m + 4) * c[1];
        v[i] = pc_fistp32(r + PC_F(0x6cce30 + i * 4));
    }
    for (i = 0; i < 3; i++)
        G8(0x6cccbc + i) = (uint8_t)((v[i] < 0xfff ? v[i] : 0xfff) >> 4);
    G8(0x6cccbf) = G8(0x6ccc7f);
}

/* 0x4278f0. */
float vec_facing_float_c(const uint8_t *a, const uint8_t *b, const uint8_t *c)
{
    pc_ld p = (PC_FP(c, 0xc) - PC_FP(a, 0xc)) * (PC_FP(b, 8) - PC_FP(a, 8));
    pc_ld q = (PC_FP(b, 0xc) - PC_FP(a, 0xc)) * (PC_FP(c, 8) - PC_FP(a, 8));

    return pc_float(pc_fstp32(p - q));
}

float vec_facing_float(const uint8_t *a, const uint8_t *b, const uint8_t *c)
{
    return vec_facing_float_c(a, b, c);
}

/* 0x4288c0: the projection 0x426b00 takes what the original leaves in
 * st(0). */
void vertex_project_codes(uint8_t *rec)
{
    pc_ld v = PC_FP(rec, 4);
    int32_t zb = I32(rec, 4);

    U32(rec, 0) = 0x10;
    if (zb >= GI32(0x6ccd08))
        U32(rec, 0) |= 0x20;
    if (zb >= GI32(0x6ccd04)) {
        v = PC_F(0x6ccd00) / v;
        U32(rec, 0) &= ~0x10u;
    }
    G32(0x6ccd74) = A(rec);
    GCALL(0x426b00, wide_project_c)(pc_float(pc_fstp32(v)));
}

/* 0x428900. */
void vertex_project_float(uint8_t *rec)
{
    pc_ld k = PC_F(0x6ccd00) / PC_FP(rec, 4);

    U32(rec, 8) = pc_fstp32(PC_FP(rec, 0x10) * k + PC_F(0x6ccd5c));
    U32(rec, 0xc) = pc_fstp32(PC_FP(rec, 0x14) * k + PC_F(0x6ccd60));
}

/* 0x428bd0. */
float far_plane_get(void)
{
    return pc_float(G32(0x6ccd08));
}

/* The C runtime's sqrt (0x4981d4) of a double: fsqrt at the caller's
 * precision, rounded to nearest, the result through a double. */
static inline pc_ld pc_crt_sqrt(double d)
{
    uint16_t cw, near;
    pc_ld r;

    __asm__ volatile("fnstcw %0" : "=m"(cw));
    near = (uint16_t)((cw & 0x300) | 0x7f);
    __asm__ volatile("fldcw %2\n\tfsqrt\n\tfldcw %3" : "=t"(r) : "0"((pc_ld)d), "m"(near), "m"(cw));
    return (pc_ld)pc_fstp64(r);
}

/* 0x4290b0. */
int32_t vec_normalise_x87(const int32_t *in, int32_t *out)
{
    pc_ld f[3], len, k;
    uint32_t i;

    for (i = 0; i < 3; i++)
        f[i] = (pc_ld)pc_float(pc_fstp32((pc_ld)(int32_t)((uint32_t)in[i] << 8)));
    {
        pc_ld s = f[2] * f[2] + f[1] * f[1];

        s = s + f[0] * f[0];
        len = (pc_ld)pc_float(pc_fstp32(pc_crt_sqrt(pc_fstp64(s))));
    }
    {
        double d;

        memcpy(&d, (const void *)0x4a3378, 8);
        k = (pc_ld)d;
    }
    for (i = 0; i < 3; i++)
        out[i] = pc_fistp32((pc_ld)pc_float(pc_fstp32(f[i] * k / len)));
    return 0;
}
#endif
