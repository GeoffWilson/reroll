/* Group PJ: the original's still-reachable functions in 0x455e90-0x457f80
 * (the front end's highlighted-item 3D model, car choice screen and flame
 * texture source file, after the menu model file of group PI).
 *
 *  - the front end's 3D extras: the highlighted item's model (0x455e90: a
 *    menu model id below 0x80, spinning, in its own viewport; 0x80 and up a
 *    flat sprite from the table at *0x4be340), the second model 0x4af664
 *    (0x456800), the car choice screen (0x457020: the car model with its
 *    three coloured lights, the two text box frames, the driver picture
 *    (0x4114b0 / 0x411740 sprite) and the four handling bars), the
 *    championship / mode model (0x4579a0);
 *  - the flame texture (128 x 84 bytes at 0x6b9d60, palette 0x4cfa98): one
 *    step (0x457bf0: scroll up a row, 50 random hot points on row 81, 3x3
 *    blur), its texture page info (0x457d10), the upload into the locked
 *    surface (0x457d50: 8-bit copy or 16-bit through the converted palette
 *    0x4cb800, the 64 rows then mirrored), step + upload (0x457e90) and the
 *    set-up (0x457ea0).
 *
 * Every model's rotation is a chain of int16 matrices multiplied a column at
 * a time through the GTE emulation (0x427fc0), as 0x453a00 does. Float steps
 * are the original's x87 instructions in inline asm: int -> float (fild /
 * fstp), the GTE matrix load's int16 * scale, the highlighted sprite's
 * corners (fld / fmul / fdiv, fadd). Everything outside the group, and the
 * group's own functions when another of them calls them, is called by
 * address. */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

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
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

#define g_screen_w GI32(0x4acc38)
#define g_screen_h GI32(0x4acc3c)

#define AP(T, x) ((T)GMEM(A(x)))

#define menu_model_find_at(id) PTR(GCALL(0x4539d0, menu_model_find)(id))
#define menu_model_draw_at(id, m, p, l, f) \
    GCALL(0x4538e0, menu_model_draw)(id, AP(const uint32_t *, m), AP(const int32_t *, p), A(l), f)
#define menu_model_corners_draw_at(id, m, p, l, a, s) \
    GCALL(0x453a00, menu_model_corners_draw)(id, AP(const uint32_t *, m), AP(const int32_t *, p), A(l), \
                                             AP(const uint32_t *, a), AP(const int16_t *, s))
#define fade_scale_at(v) GCALL(0x4328a0, fade_scale)(v)
#define view_rect_set_at(x, y, w, h) GCALL(0x428850, view_rect_set)(x, y, w, h)
#define matrix_from_angles_at(a, m) GCALL(0x426f80, matrix_from_angles)(AP(const int16_t *, a), AP(int16_t *, m))
#define matrix_identity_at(m) GCALL(0x4264f0, matrix_identity)(AP(uint8_t *, m))
#define gte_rotate_short_at() GCALL(0x427fc0, gte_rotate_short)()
#define gte_rotate_at() GCALL(0x428090, gte_rotate)()
#define ot_clear_reverse_at(ot, n) GCALL(0x427a70, ot_clear_reverse)(AP(uint32_t *, ot), n)
#define draw_otag_at(ot) GCALL(0x428470, capture_draw_otag)(A(ot))
#define fe_box_frame_place_at(l, p, c, x, y, w, h) \
    GCALL(0x431780, fe_box_frame_place)(A(l), AP(uint8_t *, p), AP(const uint8_t *, c), x, y, w, h)
#define language_get_at() GCALL(0x430120, language_get)()
#define charanim_upload_at(c, f) GCALL(0x4114b0, charanim_upload)(A(c), A(f))
#define charanim_texture_info_at(a, b, c, d, e) \
    GCALL(0x411740, charanim_texture_info)(AP(uint16_t *, a), AP(uint16_t *, b), AP(int16_t *, c), AP(int16_t *, d), \
                                           AP(uint16_t *, e))
#define car_stat_bar_at(s, c) GCALL(0x456f70, car_stat_bar)(A(s), c)
#define texture_surface_lock_at(h, n, px, pal, bpp, pitch) \
    GCALL(0x428c30, texture_surface_lock)(A(h), A(n), A(px), A(pal), A(bpp), A(pitch))
#define texture_surface_unlock_at(h, n, pal) GCALL(0x428c60, texture_surface_unlock)(A(h), A(n), A(pal))
#define texture_handle_new_at() GCALL(0x428c80, texture_handle_new)()
#define texture_surface_upload_at(h, w, ht, a, b, c) \
    GCALL(0x428c90, texture_surface_upload)(A(h), A(w), A(ht), A(a), A(b), A(c))
#define mem_fill_at(p, v, n) GCALL(0x476f50, mem_fill)(AP(uint8_t *, p), (uint8_t)(v), A(n))
#define pixels_rgb_convert_at(s, n, d, f) \
    GCALL(0x41e1f0, pixels_rgb_convert)(AP(const uint8_t *, s), n, AP(uint16_t *, d), A(f))
#define texture_palettes_apply_at() GCALL(0x429070, texture_palettes_apply)() /* src/vkgame.c's texture_queue_flush */
#define flame_step_at() GCALL(0x457bf0, flame_step)()
#define flame_texture_info_at(a, b, c, d, e) \
    GCALL(0x457d10, flame_texture_info)(AP(uint16_t *, a), AP(uint16_t *, b), AP(uint16_t *, c), \
                                                  AP(uint16_t *, d), AP(uint16_t *, e))
#define flame_upload_at() GCALL(0x457d50, flame_upload)()

/* This group's functions (also in build/groups/pj_protos.txt for physics.h). */
void GAMEAPI fe_item_model_draw(void); /* 0x455e90 */
void GAMEAPI fe_second_model_draw(void); /* 0x456800 */
void GAMEAPI fe_car_choice_draw(void); /* 0x457020 */
void GAMEAPI fe_mode_model_draw(void); /* 0x4579a0 */
void GAMEAPI flame_step(void); /* 0x457bf0 */
void GAMEAPI flame_texture_info(uint16_t *page, uint16_t *a, uint16_t *b, uint16_t *c, uint16_t *d); /* 0x457d10 */
void GAMEAPI flame_upload(void); /* 0x457d50 */
void GAMEAPI flame_update(void); /* 0x457e90 */
void GAMEAPI flame_init(void); /* 0x457ea0 */

/* ---- x87 helpers: the original's instructions ---- */

/* fild dword; fstp dword: an integer to float, rounded by the control word. */
static inline uint32_t pj_fild(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild word w; fmul dword k; fstp (the original's fstp / fld of the integer
 * as a float in between is exact for 16-bit values). */
static inline uint32_t pj_s16_mul(uint16_t w, uint32_t k)
{
    uint32_t r;
    __asm__ volatile("filds %1\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(w), "m"(k) : "st");
    return r;
}

/* fld a; fmul b; fdiv c; fstp. */
static inline uint32_t pj_mul_div(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfdivs %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* fld a; fadd b; fmul c; fdiv d; fstp. */
static inline uint32_t pj_add_mul_div(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfmuls %3\n\tfdivs %4\n\tfstps %0"
                     : "=m"(r)
                     : "m"(a), "m"(b), "m"(c), "m"(d)
                     : "st");
    return r;
}

/* v * k / d, the product wrapping in 32 bits, the division truncating. */
static inline int32_t pj_scale(int32_t v, int32_t k, int32_t d)
{
    return (int32_t)((uint32_t)v * (uint32_t)k) / d;
}

/* ---- GTE emulation, as the original inlines it ---- */

/* Load an int16 3x3 GTE matrix (five dwords to 0x6ccbe0) and its floats
 * (each * the scale 0x4a3310, rows of four at 0x6ccd1c). */
static void pj_gte_matrix_load(const void *m)
{
    static const uint32_t dst[9] = {0x6ccd1c, 0x6ccd20, 0x6ccd24, 0x6ccd2c, 0x6ccd30,
                                    0x6ccd34, 0x6ccd3c, 0x6ccd40, 0x6ccd44};
    const uint8_t *s = (const uint8_t *)m;
    int i;

    for (i = 0; i < 5; i++)
        G32(0x6ccbe0 + 4 * i) = U32(s, 4 * i);
    for (i = 0; i < 9; i++)
        G32(dst[i]) = pj_s16_mul(G16(0x6ccbe0 + 2 * i), G32(0x4a3310));
}

/* out = GTE matrix * in, a column at a time through the short vector at
 * 0x6ccc88 (0x427fc0); in may be out. */
static void pj_gte_matrix_mul(const int16_t *in, int16_t *out)
{
    int j;

    for (j = 0; j < 3; j++) {
        G16(0x6ccc88) = (uint16_t)in[j];
        G16(0x6ccc8c) = (uint16_t)in[3 + j];
        G16(0x6ccc90) = (uint16_t)in[6 + j];
        gte_rotate_short_at();
        out[j] = (int16_t)G16(0x6ccc88);
        out[3 + j] = (int16_t)G16(0x6ccc8c);
        out[6 + j] = (int16_t)G16(0x6ccc90);
    }
}

/* The ordering table entry a sprite is linked into: by its texture page
 * (the low 6 bits of `page`) when 0x6cce84 is set, else `fixed`. */
static uint32_t pj_ot_entry(uint32_t page, uint32_t fixed)
{
    uint32_t e = G32(0x6cce84) ? G32(0x6cd518 + 8 * (page & 0x3f)) : G32(fixed);

    return e & 0x7fffffffu;
}

/* The front-end models' light: direction (-0x12c, -0x64, 0x4b0) << 16,
 * colour 0xdc 0xdc 0xff, 10 at +0x10. The byte at +0xf and the word at +0x12
 * are stack leftovers in the original; here 0. */
static void pj_front_light(uint32_t *l)
{
    l[0] = 0xfed40000u;
    l[1] = 0xff9c0000u;
    l[2] = 0x04b00000u;
    l[3] = 0x00ffdcdcu;
    l[4] = 10;
}

/* ===================================================================== */

/* 0x455e90: the highlighted front-end item 0x4af660 (-1 none). Below 0x80
 * (signed): its menu model (0x4539d0), in a viewport at (0x4be334 + 0x154,
 * 0x4be338 + 3) of 512 x 256 (y less fade_scale(0x80)), size 0x8a x 0x3e,
 * turned by 0x4d0714 (+= 0x1e each call): rotation = record matrix (+8) *
 * spin (+0x40 with x + 0x4b, z - 0x4b) * (0, 0x4d0714, 0) * record angles
 * (+0x38), drawn at (0, 0, 0x578) with the front light, then the two
 * ordering tables and the full-screen viewport. 0x80 and up: the flat
 * sprite 0x4cba00 (texture from the 16-byte record (id - 0x80) of
 * *0x4be340), corners from the screen size and 0x4be338, linked into the
 * ordering table, then the two ordering tables. The matrix handed to
 * 0x4538e0 has stack leftovers past the 3x3 (its translation); here 0. */
void GAMEAPI fe_item_model_draw(void)
{
    int32_t id = GI32(0x4af660);

    if (id == -1)
        return;
    if (id < 0x80) {
        int16_t a[16], b[16], c[16], t[16], ang[4], spin[4];
        uint32_t light[5], lights[3];
        int32_t pos[3], sw, sh, f, x, y;
        uint8_t *rec = menu_model_find_at(id);

        if (!rec)
            return;
        memset(a, 0, sizeof a);
        memset(b, 0, sizeof b);
        memset(c, 0, sizeof c);
        memset(t, 0, sizeof t);
        sh = pj_scale(g_screen_h, 0x3e, 256);
        sw = pj_scale(g_screen_w, 0x8a, 512);
        f = fade_scale_at(0x80);
        y = pj_scale(GI32(0x4be338) + 3, g_screen_h, 256) - f;
        x = pj_scale(GI32(0x4be334) + 0x154, g_screen_w, 512);
        view_rect_set_at(x, y, sw, sh);
        pos[0] = 0;
        pos[1] = 0;
        pos[2] = 0x5780000;
        pj_front_light(light);
        lights[0] = A(light);
        lights[1] = 0;
        lights[2] = 0;
        G16(0x4d0714) = (uint16_t)(G16(0x4d0714) + 0x1e);
        ang[0] = 0;
        ang[1] = (int16_t)G16(0x4d0714);
        ang[2] = 0;
        ang[3] = 0;
        memcpy(spin, rec + 0x40, 8);
        spin[0] = (int16_t)(spin[0] + 0x4b);
        spin[2] = (int16_t)(spin[2] - 0x4b);
        matrix_from_angles_at(ang, a);
        matrix_from_angles_at(rec + 0x38, b);
        matrix_from_angles_at(spin, c);
        pj_gte_matrix_load(a);
        pj_gte_matrix_mul(b, t);
        pj_gte_matrix_load(c);
        pj_gte_matrix_mul(t, t);
        pj_gte_matrix_load(rec + 8);
        pj_gte_matrix_mul(t, t);
        menu_model_draw_at(G32(0x4af660), t, pos, lights, 0);
        draw_otag_at(0x6cdd0c);
        draw_otag_at(0x6ce57c);
        view_rect_set_at(0, 0, g_screen_w, g_screen_h);
    } else {
        uint8_t *rec = PTR(G32(0x4be340) + (uint32_t)(id - 0x80) * 16);
        uint32_t fa, fw, fh, entry;
        int32_t f;

        ot_clear_reverse_at(0x6cd510, 0x200);
        ot_clear_reverse_at(0x6cdd80, 0x200);
        f = fade_scale_at(0x80);
        G8(0x4cba04) = 0xff;
        G8(0x4cba05) = 0xff;
        G8(0x4cba06) = 0xff;
        fa = pj_fild(GI32(0x4be338) - f + 9);
        fw = pj_fild(g_screen_w);
        G32(0x4cba10) = G32(0x4cba48) = pj_mul_div(fw, G32(0x4a33c8), G32(0x4a33c4));
        fh = pj_fild(g_screen_h);
        G32(0x4cba14) = G32(0x4cba30) = pj_mul_div(fh, fa, G32(0x4a33c0));
        G32(0x4cba2c) = G32(0x4cba64) = pj_mul_div(fw, G32(0x4a33bc), G32(0x4a33c4));
        G32(0x4cba0c) = G32(0x4cba28) = G32(0x4cba44) = G32(0x4cba60) = 0x455ac000u; /* 3500.0 */
        G32(0x4cba4c) = G32(0x4cba68) = pj_add_mul_div(fa, G32(0x4a33b8), fh, G32(0x4a33c0));
        G32(0x4cba08) = G32(0x4cba24) = G32(0x4cba40) = G32(0x4cba5c) = 0x40;
        G32(0x4cba20) = U32(rec, 0);
        G32(0x4cba3c) = U32(rec, 4);
        G16(0x4cba58) = U16(rec, 8);
        G16(0x4cba74) = U16(rec, 0xa);
        G32(0x4cba00) = (G32(0x4cba00) & 0xffffffu) | 0x0c000000u;
        G8(0x4cba07) = 0x98;
        entry = pj_ot_entry(G8(0x4cba3e), 0x6cd52c);
        G32(0x4cba00) = G32(entry);
        G32(entry) = 0x4cba00;
        draw_otag_at(0x6cdd0c);
        draw_otag_at(0x6ce57c);
    }
}

/* 0x456800: the front end's second model 0x4af664 (-1 none): its record's
 * angles' y (+0x32) = 0x4d0716 -= 0x1e; rotation = record matrix (+8) *
 * spin (+0x40) * angles (+0x30) * angles (+0x38) (translation 0 from the
 * identity 0x4264f0); the ordering tables cleared, viewport
 * (0x4cb47c + 0x32, 0x4cb478 - 0x14) size 220 x 300 of 640 x 480; drawn at
 * (0, 0, 0x578) with the front light, the two ordering tables, full-screen
 * viewport. */
void GAMEAPI fe_second_model_draw(void)
{
    int16_t a[16], b[16], c[16], t[16];
    uint32_t light[5], lights[3], spin[2];
    int32_t pos[3], d, cw, y, x;
    int32_t id = GI32(0x4af664);
    uint8_t *rec;

    if (id == -1)
        return;
    rec = menu_model_find_at(id);
    if (!rec)
        return;
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    memset(c, 0, sizeof c);
    memset(t, 0, sizeof t);
    matrix_identity_at(t);
    G16(0x4d0716) = (uint16_t)(G16(0x4d0716) - 0x1e);
    U16(rec, 0x32) = G16(0x4d0716);
    spin[0] = U32(rec, 0x40);
    spin[1] = U32(rec, 0x44);
    matrix_from_angles_at(rec + 0x30, a);
    matrix_from_angles_at(rec + 0x38, b);
    matrix_from_angles_at(spin, c);
    pj_gte_matrix_load(a);
    pj_gte_matrix_mul(b, t);
    pj_gte_matrix_load(c);
    pj_gte_matrix_mul(t, t);
    pj_gte_matrix_load(rec + 8);
    pj_gte_matrix_mul(t, t);
    pos[0] = 0;
    pos[1] = 0;
    pos[2] = 0x5780000;
    pj_front_light(light);
    lights[0] = A(light);
    lights[1] = 0;
    lights[2] = 0;
    ot_clear_reverse_at(0x6cd510, 0x200);
    ot_clear_reverse_at(0x6cdd80, 0x200);
    d = pj_scale(g_screen_h, 300, 480);
    cw = pj_scale(g_screen_w, 220, 640);
    y = pj_scale(GI32(0x4cb478) - 0x14, g_screen_h, 480);
    x = pj_scale(GI32(0x4cb47c) + 0x32, g_screen_w, 640);
    view_rect_set_at(x, y, cw, d);
    menu_model_draw_at(G32(0x4af664), t, pos, lights, 0);
    draw_otag_at(0x6cdd0c);
    draw_otag_at(0x6ce57c);
    view_rect_set_at(0, 0, g_screen_w, g_screen_h);
}

/* 0x457020: the car choice screen, when 0x4af660 is one of the seven ids at
 * 0x4af614 (index i): the car model 0x4af630[i] in a viewport
 * (centre of 0x4cb474..0x4cb47c - 0xaf, 35) size 350 x 350 of 640 x 480, lit
 * by three lights (angles 0x4d0718 + 8k, each turned by 0x4b0a40 + 8k
 * after use; position 0x4b0a28 + 8k rotated (0x428090) << 16 + (0, 0,
 * 0x578), colour 0x4b0a58 + 4k, 10), turned by (0, 0x4d0730 -= 10,
 * 0x4b0a64 (+10 while button 0x2000 is held without 0x1800)), spin
 * (400, 0, 0), matrix identity with the diagonal * 500 / 256 (0x453a00);
 * the two text box frames (0x431780, the second's height 200 in language
 * other than 0), the driver picture's frame 0x4af654 / 2 (0x4114b0) as the
 * sprite 0x4cf750 (texture info 0x411740), and the four handling bars
 * (0x456f70 of car {2, 5, 3, 1, 4, 0, 6}[i]: lengths 0x4d0734.. moving 4 at a
 * time towards (0x214 - 0x210 + 1) * bar >> 8 of the colour record
 * *0x4be340 + 0x210, quads at 0x4cd6a0 + 0x78k coloured between that
 * record's two colours). The lights' buffers past their 0x12 bytes and the
 * texture info's word at -0x7e (the high half of 0x4cf770) are stack
 * leftovers in the original; here 0. */
void GAMEAPI fe_car_choice_draw(void)
{
    static const int32_t cars[7] = {2, 5, 3, 1, 4, 0, 6};
    uint8_t lb[3][0x40], ti[12];
    int16_t m[16], mx[16], ang[4], spin[4];
    uint32_t lights[3], colour, x0f, y0f, x1f, y1f, entry, den;
    int32_t pos[3], idx, model, k, base, xs, bx, hh, frame, car, y;
    uint8_t *bar, *pk;

    for (idx = 0; idx < 7; idx++)
        if (G32(0x4af660) == G32(0x4af614 + 4 * idx))
            break;
    if (idx >= 7)
        return;
    model = GI32(0x4af630 + 4 * idx);
    ot_clear_reverse_at(0x6cd510, 0x200);
    ot_clear_reverse_at(0x6cdd80, 0x200);
    {
        int32_t d = pj_scale(g_screen_h, 350, 480);
        int32_t c = pj_scale(g_screen_w, 350, 640);
        int32_t b = pj_scale(g_screen_h, 35, 480);
        int32_t l = GI32(0x4cb474);
        int32_t a = pj_scale(((int32_t)((uint32_t)GI32(0x4cb47c) - (uint32_t)l) >> 1) + l - 0xaf, g_screen_w, 640);
        view_rect_set_at(a, b, c, d);
    }
    memset(lb, 0, sizeof lb);
    memset(m, 0, sizeof m);
    lights[0] = A(lb[1]); /* the original's buffers: -0x128, -0x168, -0xe8 */
    lights[1] = A(lb[0]);
    lights[2] = A(lb[2]);
    pos[0] = 0;
    pos[1] = 0;
    pos[2] = 0x5780000;
    for (k = 0; k < 3; k++) {
        uint8_t *l = PTR(lights[k]);
        int16_t a1, a2;

        matrix_from_angles_at(0x4d0718 + 8 * k, m);
        pj_gte_matrix_load(m);
        G32(0x6ccc64) = G32(0x4b0a28 + 8 * k);
        G32(0x6ccc68) = G32(0x4b0a2c + 8 * k);
        gte_rotate_at();
        U32(l, 0) = G32(0x6cccc8) << 16;
        U32(l, 4) = G32(0x6ccccc) << 16;
        U32(l, 8) = G32(0x6cccd0) << 16;
        a1 = (int16_t)(GS16(0x4b0a42 + 8 * k) + GS16(0x4d071a + 8 * k));
        a2 = (int16_t)(GS16(0x4b0a44 + 8 * k) + GS16(0x4d071c + 8 * k));
        G16(0x4d0718 + 8 * k) = (uint16_t)(G16(0x4d0718 + 8 * k) + G16(0x4b0a40 + 8 * k));
        G16(0x4d071a + 8 * k) = (uint16_t)a1;
        G16(0x4d071c + 8 * k) = (uint16_t)a2;
        U32(l, 4) += (uint32_t)pos[1];
        U32(l, 8) += (uint32_t)pos[2];
        U32(l, 0) += (uint32_t)pos[0];
        U32(l, 0xc) = G32(0x4b0a58 + 4 * k);
        U16(l, 0x10) = 10;
    }
    if ((G32(0x4d0730) & 0x2000) && !(G32(0x4d0730) & 0x1800))
        G16(0x4b0a64) = (uint16_t)(G16(0x4b0a64) + 10);
    G16(0x4d0730) = (uint16_t)(G16(0x4d0730) - 10);
    ang[0] = 0;
    ang[1] = (int16_t)G16(0x4d0730);
    ang[2] = (int16_t)G16(0x4b0a64);
    ang[3] = 0;
    spin[0] = 400;
    spin[1] = 0;
    spin[2] = 0;
    spin[3] = 0;
    memset(mx, 0, sizeof mx);
    matrix_identity_at(mx);
    mx[0] = (int16_t)((int32_t)mx[0] * 500 >> 8);
    mx[4] = (int16_t)((int32_t)mx[4] * 500 >> 8);
    mx[8] = (int16_t)((int32_t)mx[8] * 500 >> 8);
    menu_model_corners_draw_at(model, mx, pos, lights, ang, spin);
    draw_otag_at(0x6cdd0c);
    draw_otag_at(0x6ce57c);

    bar = PTR(G32(0x4be340) + 0x210);
    den = (uint32_t)U8(bar, 4) - (uint32_t)U8(bar, 0);
    colour = 0xffffff;
    ot_clear_reverse_at(0x6cd510, 0x200);
    ot_clear_reverse_at(0x6cdd80, 0x200);
    base = GI32(0x4cb540);
    xs = base + 0x1da;
    fe_box_frame_place_at(0x6cdd90, 0x4cfea0, &colour, base + 0x1d6, 0x13b, 0x96, 0x8c);
    base = GI32(0x4cb540);
    hh = language_get_at() ? 0xc8 : 0x96;
    bx = base + 0x14 + 0xf;
    fe_box_frame_place_at(0x6cdd90, 0x4d02d8, &colour, base + 0x14, 0x13b, hh, 0x8c);
    frame = (int32_t)(G32(0x4af654) & 0xfffffffeu) > 0 ? GI32(0x4af654) >> 1 : 0;
    charanim_upload_at(idx, frame);
    memset(ti, 0, sizeof ti);
    charanim_texture_info_at(ti + 6, ti + 0, ti + 4, ti + 8, ti + 10);
    view_rect_set_at(0, 0, g_screen_w, g_screen_h);

    /* the driver picture */
    x0f = pj_fild(pj_scale(xs, g_screen_w, 640));
    G8(0x4cf754) = 0xff;
    G8(0x4cf755) = 0xff;
    G8(0x4cf756) = 0xff;
    G32(0x4cf760) = x0f;
    y0f = pj_fild(pj_scale(g_screen_h, 319, 480));
    G32(0x4cf764) = y0f;
    G32(0x4cf780) = y0f;
    x1f = pj_fild(pj_scale((int32_t)ti[4] + xs - 7, g_screen_w, 640));
    G32(0x4cf798) = x0f;
    G32(0x4cf77c) = x1f;
    G32(0x4cf758) = 0x40;
    y1f = pj_fild(pj_scale((int32_t)ti[9] + 0x138, g_screen_h, 480));
    G32(0x4cf78c) = U32(ti, 4);
    entry = G32(0x4cf750);
    G32(0x4cf774) = 0x40;
    G32(0x4cf790) = 0x40;
    G32(0x4cf7ac) = 0x40;
    G32(0x4cf79c) = y1f;
    G32(0x4cf7b8) = y1f;
    G32(0x4cf75c) = G32(0x4cf778) = G32(0x4cf794) = G32(0x4cf7b0) = 0x455ac000u; /* 3500.0 */
    G16(0x4cf7a8) = U16(ti, 8);
    G32(0x4cf770) = U32(ti, 0);
    G32(0x4cf7b4) = x1f;
    G16(0x4cf7c4) = U16(ti, 10);
    G32(0x4cf750) = (entry & 0xffffffu) | 0x0c000000u;
    G8(0x4cf757) = 0x98;
    entry = pj_ot_entry(U32(ti, 6), 0x6cd52c);
    G32(0x4cf750) = G32(entry);
    G32(entry) = 0x4cf750;

    /* the four handling bars */
    car = cars[idx];
    pk = PTR(0x4cd6b4);
    y = 0x155;
    for (k = 0; k < 4; k++, y += 0x20, pk += 0x78) {
        uint32_t *p = (uint32_t *)(uintptr_t)(0x4d0734 + 4 * k);
        int32_t r = car_stat_bar_at(k, car), dlt, ad;
        uint32_t target = ((uint32_t)((int32_t)U8(bar, 4) - (int32_t)U8(bar, 0) + 1) * (uint32_t)r) >> 8;
        uint32_t len, f0, g0, f1, g1;
        uint8_t lo;

        dlt = (int32_t)(*p - target);
        ad = (dlt ^ (dlt >> 31)) - (dlt >> 31);
        if (ad < 4)
            *p = target;
        else if ((int32_t)*p < (int32_t)target)
            *p += 4;
        else if ((int32_t)*p > (int32_t)target)
            *p -= 4;
        len = *p & ~3u;
        *p = len;
        f0 = pj_fild(pj_scale(bx, g_screen_w, 640));
        U32(pk, -4) = f0;
        g0 = pj_fild(pj_scale(y, g_screen_h, 480));
        U32(pk, 0) = g0;
        f1 = pj_fild(pj_scale(bx + (int32_t)len, g_screen_w, 640));
        U32(pk, 0x18) = f1;
        U32(pk, 0x1c) = g0;
        U32(pk, 0x34) = f0;
        g1 = pj_fild(pj_scale(y + 8, g_screen_h, 480));
        U32(pk, 0x38) = g1;
        U32(pk, 0x50) = f1;
        U32(pk, 0x54) = g1;
        U32(pk, -8) = 0x447a0000u; /* 1000.0 */
        U32(pk, -0xc) = 0x40;
        U32(pk, 0x14) = 0x447a0000u;
        U32(pk, 0x10) = 0x40;
        U32(pk, 0x30) = 0x447a0000u;
        U32(pk, 0x2c) = 0x40;
        U32(pk, 0x4c) = 0x447a0000u;
        U32(pk, 0x48) = 0x40;
        U32(pk, 0xc) = U32(bar, 0);
        U32(pk, 0x28) = U32(bar, 4);
        U16(pk, 0x44) = U16(bar, 8);
        U16(pk, 0x60) = U16(bar, 0xa);
        lo = U8(bar, 0);
        U8(pk, 0x28) = (uint8_t)(((uint32_t)((int32_t)U8(bar, 4) - (int32_t)lo) * len) / den + lo);
        lo = U8(bar, 8);
        U8(pk, 0x60) = (uint8_t)(((uint32_t)((int32_t)U8(bar, 0xa) - (int32_t)lo) * len) / den + lo);
        U8(pk, -0x10) = 0xff;
        U8(pk, -0xf) = 0xff;
        U8(pk, -0xe) = 0xff;
        U32(pk, -0x14) = (U32(pk, -0x14) & 0xffffffu) | 0x0c000000u;
        U8(pk, -0xd) = 0x98;
        entry = pj_ot_entry(U8(pk, 0x2a), 0x6cd518);
        U32(pk, -0x14) = G32(entry);
        G32(entry) = A(pk - 0x14);
    }
    draw_otag_at(0x6cdd0c);
    draw_otag_at(0x6ce57c);
    view_rect_set_at(0, 0, g_screen_w, g_screen_h);
}

/* 0x4579a0: the mode model: 0x4af660 when it is 0x39..0x3c, or for items 3
 * and 4 the model by the option byte (*0x4b2198)[0x1c] (1..4: 0x3b, 0x39,
 * 0x3a, 0x3c; others: nothing). Viewport (0x4cb47c + 0x1e, 0x4cb478 - 0x4b)
 * size 350 x 350 of 640 x 480, turned by (0, 0x4d0744 -= 0x14, 0), no spin,
 * matrix identity with the diagonal doubled, at (0, 0, 0x578), light
 * direction (-0x1f4, -0x96, 0x44c) << 16 (0x453a00), then the two ordering
 * tables and the full-screen viewport. */
void GAMEAPI fe_mode_model_draw(void)
{
    int16_t mx[16], ang[4], spin[4];
    uint32_t light[5], lights[3];
    int32_t pos[3], id = -1, item = GI32(0x4af660);

    if (item == 3 || item == 4) {
        switch (U8(G32(0x4b2198), 0x1c)) {
        case 1:
            id = 0x3b;
            break;
        case 2:
            id = 0x39;
            break;
        case 3:
            id = 0x3a;
            break;
        case 4:
            id = 0x3c;
            break;
        default:
            return;
        }
    }
    if (item == 0x3b || item == 0x39 || item == 0x3a || item == 0x3c)
        id = item;
    else if (id == -1)
        return;
    ot_clear_reverse_at(0x6cd510, 0x200);
    ot_clear_reverse_at(0x6cdd80, 0x200);
    {
        int32_t d = pj_scale(g_screen_h, 350, 480);
        int32_t c = pj_scale(g_screen_w, 350, 640);
        int32_t b = pj_scale(GI32(0x4cb478) - 0x4b, g_screen_h, 480);
        int32_t a = pj_scale(GI32(0x4cb47c) + 0x1e, g_screen_w, 640);

        view_rect_set_at(a, b, c, d);
    }
    pos[0] = 0;
    pos[1] = 0;
    pos[2] = 0x5780000;
    light[0] = 0xfe0c0000u;
    light[1] = 0xff6a0000u;
    light[2] = 0x044c0000u;
    light[3] = 0x00ffdcdcu;
    light[4] = 10;
    lights[0] = A(light);
    lights[1] = 0;
    lights[2] = 0;
    G16(0x4d0744) = (uint16_t)(G16(0x4d0744) - 0x14);
    ang[0] = 0;
    ang[1] = (int16_t)G16(0x4d0744);
    ang[2] = 0;
    ang[3] = 0;
    memset(spin, 0, sizeof spin);
    memset(mx, 0, sizeof mx);
    matrix_identity_at(mx);
    mx[0] = (int16_t)((uint32_t)(uint16_t)mx[0] << 1);
    mx[4] = (int16_t)((uint32_t)(uint16_t)mx[4] << 1);
    mx[8] = (int16_t)((uint32_t)(uint16_t)mx[8] << 1);
    menu_model_corners_draw_at(id, mx, pos, lights, ang, spin);
    draw_otag_at(0x6cdd0c);
    draw_otag_at(0x6ce57c);
    view_rect_set_at(0, 0, g_screen_w, g_screen_h);
}

/* 0x457bf0: one step of the flame (128 x 84 bytes at 0x6b9d60, a spare
 * row's worth after it): scroll up a row, 50 hot points on row 81 (x and
 * value from the generator 0x4b11d8, seed * 16807 mod 2^31 twice each), then
 * the 8-neighbour average of rows 1..83, columns 1..127 into 0x6bc760 and
 * the whole 0x2a00 bytes back. */
void GAMEAPI flame_step(void)
{
    uint32_t i, s, a, x, y;

    for (i = 0; i < 0xa60; i++)
        G32(0x6b9d60 + 4 * i) = G32(0x6b9de0 + 4 * i);
    s = G32(0x4b11d8);
    for (i = 0; i < 0x32; i++) {
        a = s * 16807u & 0x7fffffffu;
        s = a * 16807u & 0x7fffffffu;
        G8(0x6bc5e0 + (((a >> 15) << 7) >> 16)) = (uint8_t)(((s >> 15) * 0xc0) >> 16);
    }
    G32(0x4b11d8) = s;
    for (y = 1; y < 0x54; y++)
        for (x = 1; x < 0x80; x++) {
            uint32_t o = y * 0x80 + x;

            G8(0x6bc760 + o) = (uint8_t)((G8(0x6b9cdf + o) + G8(0x6b9ce0 + o) + G8(0x6b9ce1 + o) + G8(0x6b9d5f + o) +
                                          G8(0x6b9d61 + o) + G8(0x6b9ddf + o) + G8(0x6b9de0 + o) + G8(0x6b9de1 + o)) >>
                                         3);
        }
    for (i = 0; i < 0xa80; i++)
        G32(0x6b9d60 + 4 * i) = G32(0x6bc760 + 4 * i);
}

/* 0x457d10: the flame texture's page (handle 0x4cba78) and its corners
 * (0, 0x7f, 0x7f00, 0x7f7f), as words. */
void GAMEAPI flame_texture_info(uint16_t *page, uint16_t *a, uint16_t *b, uint16_t *c, uint16_t *d)
{
    *page = G16(0x4cba78);
    *a = 0;
    *b = 0x7f;
    *c = 0x7f00;
    *d = 0x7f7f;
}

/* 0x457d50: upload the flame into its texture: lock (0x428c30, palette
 * out), the palette replaced by 0x4cfa98; 8 bits: the 64 rows then the same
 * rows bottom-up; 16 bits: the same through the converted palette 0x4cb800;
 * other depths nothing; unlock (0x428c60) with the palette. */
void GAMEAPI flame_upload(void)
{
    uint32_t pal[256], pixels = 0, bpp = 0, pitch = 0;
    uint16_t w[6];
    uint32_t src, dst, row, i;

    memset(w, 0, sizeof w);
    memset(pal, 0, sizeof pal);
    flame_texture_info_at(&w[0], &w[3], &w[5], &w[4], &w[2]);
    if (!texture_surface_lock_at(w[0], 1, &pixels, pal, &bpp, &pitch))
        return;
    memcpy(pal, PTR(0x4cfa98), 0x400);
    if (bpp == 8) {
        src = 0x6b9d60;
        dst = pixels;
        for (row = 0; row < 0x40; row++) {
            for (i = 0; i < 0x20; i++, src += 4, dst += 4)
                G32(dst) = G32(src);
            dst += pitch - 0x80;
        }
        src -= 0x80;
        for (row = 0; row < 0x40; row++) {
            for (i = 0; i < 0x20; i++, src += 4, dst += 4)
                G32(dst) = G32(src);
            dst += pitch - 0x80;
            src -= 0x100;
        }
    } else if (bpp == 0x10) {
        src = 0x6b9d60;
        dst = pixels;
        for (row = 0; row < 0x40; row++) {
            for (i = 0; i < 0x80; i++, src++, dst += 2)
                G16(dst) = G16(0x4cb800 + 2 * G8(src));
            dst += pitch - 0x100;
        }
        src -= 0x80;
        for (row = 0; row < 0x40; row++) {
            for (i = 0; i < 0x80; i++, src++, dst += 2)
                G16(dst) = G16(0x4cb800 + 2 * G8(src));
            dst += pitch - 0x100;
            src -= 0x100;
        }
    }
    texture_surface_unlock_at(w[0], 1, pal);
}

/* 0x457e90: one flame step (0x457bf0), then its upload (0x457d50). */
void GAMEAPI flame_update(void)
{
    flame_step_at();
    flame_upload_at();
}

/* 0x457ea0: the flame's set-up: both buffers and the palette cleared, a new
 * texture (0x428c80, handle 0x4cba78, 256 x 128, 0x428c90), the palette
 * (12 of (0, 0, 0x15 k), 24 of (10 k, 0, 0xf0), 220 white), converted
 * (0x41e1f0 into 0x4cb800), 50 steps, the upload, then the palettes
 * re-applied (0x429070). */
void GAMEAPI flame_init(void)
{
    uint32_t i, p;

    mem_fill_at(0x6b9d60, 0, 0x2a00);
    mem_fill_at(0x6bc760, 0, 0x2a00);
    mem_fill_at(0x4cfa98, 0, 0x400);
    G32(0x4cba78) = (uint32_t)texture_handle_new_at();
    texture_surface_upload_at(G32(0x4cba78), 0x100, 0x80, 0, 0, 0);
    p = 0x4cfa98;
    for (i = 0; i < 12; i++, p += 4) {
        G8(p) = 0;
        G8(p + 1) = 0;
        G8(p + 2) = (uint8_t)(i * 0x15);
    }
    for (i = 0; i < 24; i++, p += 4) {
        G8(p) = (uint8_t)(i * 10);
        G8(p + 1) = 0;
        G8(p + 2) = 0xf0;
    }
    for (i = 0; i < 0xdc; i++, p += 4)
        G32(p) = 0xffffff;
    pixels_rgb_convert_at(0x4cfa98, 0x100, 0x4cb800, 1);
    for (i = 0; i < 0x32; i++)
        flame_step_at();
    flame_upload_at();
    texture_palettes_apply_at();
}
