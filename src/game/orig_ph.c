/* Group PH: the original's still-reachable functions in 0x4427a0-0x453130
 * (the end of the object / glyph source file, the game's bump heap, and the
 * front end's menu drawing and option helpers).
 *
 *  - objects: an object's queued trigger words run and cleared (0x4427a0);
 *  - the glyph sheet packer (0x4432d0): cells of a loaded image cropped to
 *    their ink and packed into 256 x 256 texture pages;
 *  - the heap: a K&R free-list allocator on one GlobalAlloc block (init
 *    0x444500, free 0x444570, malloc 0x4445e0, morecore 0x444670, sbrk
 *    0x4446c0), all five together (state 0x4c2460-0x4c2478);
 *  - menus: text cut to a width (0x445340), the cursor bar sprites' set-up
 *    (0x446250) and drawing (0x446300), slider bars (0x446ea0), item
 *    measuring (0x44cc50, 0x44ccf0, 0x44ce20, 0x44ce40), the on / off and
 *    number option steps (0x447f20, 0x450050), two thunks to the hold
 *    resets (0x447f00, 0x447f10), the Polish winner's movie (0x447ff0), the
 *    background tiles (0x453020 set-up, 0x4530d0 tint + draw) and the frame
 *    sprites (0x453130).
 *
 * Float steps are the original's x87 instructions in inline asm (int ->
 * float through fild / fstp, the cursor bar's y with its wobble, the slider
 * fraction with the C runtime's _ftol); integer logic is C. Everything
 * outside the group is called by address. */
#include "../runtime.h"
#include "physics.h"
#include "winapi.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define GS16(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

#define AP(T, x) ((T)GMEM(A(x)))

/* Callees outside the group. */
#define trigger_effect_start(n) GCALL(0x441370, light_slot_free)(n)
#define trigger_sound(n) GCALL(0x47dca0, light_sprite_release)(n)
#define bitmap_scan_ph(p, step, n, stride, m, pal) \
    GCALL(0x443260, bitmap_scan)(AP(const uint8_t *, p), step, n, stride, m) /* pal: never read */
#define block_alloc_ph(n) GCALL(0x476bc0, arena_alloc_high)(n)
#define block_free_size_ph(n) GCALL(0x476c00, arena_free_high)(n)
#define mem_fill_ph(p, v, n) GCALL(0x476f50, mem_fill)(AP(uint8_t *, p), (uint8_t)(v), A(n))
#define mem_copy_ph(d, s, n) GCALL(0x476f30, mem_copy)(AP(uint8_t *, d), AP(const uint8_t *, s), A(n))
#define image_load_ph(name, img, f) GCALL(0x477530, image_bmp_load)(AP(const char *, name), AP(uint8_t *, img), A(f))
#define image_release_ph(img) GCALL(0x477810, image_release)(AP(uint8_t *, img))
#define texture_handle_new_ph() GCALL(0x428c80, texture_handle_new)()
#define texture_upload_ph(h, w, n, pix, pal, f) \
    GCALL(0x428c90, texture_surface_upload)(A(h), A(w), A(n), A(pix), A(pal), A(f))
#define fatal_error_ph(s) GCALL(0x41c970, fatal_error)(AP(const uint8_t *, s))
#define text_width_printf(font, a, b, c, fmt, s) \
    GCALL(0x42e8f0, string_width_printf)(A(font), A(a), A(b), A(c), AP(const char *, fmt), AP(const char *, s))
#define sprite_gouraud_set(p, t, i, a, b, c, d, z) \
    GCALL(0x430ea0, fe_sprite_gouraud_set)(AP(uint8_t *, p), t, AP(const uint8_t *, i), a, b, c, d, A(z))
#define prim_flag2_set_ph(p, on) GCALL(0x4270e0, prim_flag2_set)(AP(uint8_t *, p), on)
#define ot_add(ot, p) GCALL(0x427ab0, ot_add_prim)(A(ot), AP(uint32_t *, p))
#define fe_texture_width(i) GCALL(0x431670, frontend_texture_width)(i)
#define fe_texture_height(i) GCALL(0x4316a0, frontend_texture_height)(i)
#define colour_pulse(out, v, n, k) GCALL(0x4328c0, colour_pulse_scale)(AP(uint8_t *, out), AP(const uint8_t *, v), n, A(k))
#define quad_link_ph(a, b, c, d, e) \
    GCALL(0x457d10, flame_texture_info)(AP(uint16_t *, a), AP(uint16_t *, b), AP(uint16_t *, c), AP(uint16_t *, d), \
                                        AP(uint16_t *, e))
#define flat_quad_set(p, col, a, b, c, d, z) \
    GCALL(0x4314d0, fe_quad_flat_set)(AP(uint8_t *, p), AP(const uint8_t *, col), a, b, c, d, A(z))
#define sprite_512_set(p, t, i, x, y, w, h, z) \
    GCALL(0x431300, fe_sprite_set_512)(AP(uint8_t *, p), t, AP(const uint8_t *, i), x, y, w, h, A(z))
#define dword_store_ph(p, v) GCALL(0x4290a0, dword_store)(AP(uint32_t *, p), A(v))
#define ot_link_ph(ot, first, last) GCALL(0x429080, word_exchange)(A(ot), A(first), AP(uint32_t *, last))
#define string_printf_ph(a, b, c, d, e, f, g, h, i) \
    GCALL(0x42e810, string_printf)(A(a), A(b), A(c), A(d), AP(const char *, e), AP(const char *, f), \
                                   AP(const char *, g), AP(const char *, h), AP(const char *, i))
#define string_width_ph(h) GCALL(0x42f030, string_get_width)(h)
#define string_height_ph(h) GCALL(0x42f070, string_get_height)(h)
#define string_marks_ph(h) GCALL(0x42f0b0, string_has_marks)(h)
#define string_clip_ph(h, a, b) GCALL(0x42f2d0, string_clip_x)(h, a, b)
#define sprintf_ph(d, f, v) GCALL(0x4979f0, msvc_sprintf)(AP(char *, d), AP(const char *, f), v)
#define CRT_FTOL 0x497a80

/* This group's functions (also in build/groups/ph_protos.txt for physics.h). */
void GAMEAPI body_triggers_apply(uint8_t *obj); /* 0x4427a0 */
int32_t GAMEAPI glyph_sheet_pack(uint32_t name, int32_t cell_w, uint8_t *out, int32_t row, int32_t rows,
                                 int32_t count, int32_t keep, int32_t more); /* 0x4432d0 */
void GAMEAPI kr_heap_init(void); /* 0x444500 */
uint32_t GAMEAPI kr_free(uint32_t ap); /* 0x444570 */
uint32_t GAMEAPI kr_malloc(uint32_t n); /* 0x4445e0 */
uint32_t GAMEAPI kr_morecore(int32_t nu); /* 0x444670 */
uint32_t GAMEAPI kr_sbrk(uint32_t n); /* 0x4446c0 */
void GAMEAPI menu_text_fit(char *dst, const char *src, int32_t max_w, int32_t mode, uint32_t font); /* 0x445340 */
void GAMEAPI menu_bar_sprites_init(void); /* 0x446250 */
void GAMEAPI menu_cursor_bar_draw(uint32_t ot_text, uint32_t ot, int32_t slot, uint32_t pulse, const uint8_t *col,
                                  uint32_t unused6, uint32_t unused7, int32_t row); /* 0x446300 */
void GAMEAPI menu_slider_bars_draw(uint32_t ot, int32_t slot); /* 0x446ea0 */
void GAMEAPI menu_hold_clear_thunk(void); /* 0x447f00 */
void GAMEAPI menu_hold_reset_thunk(void); /* 0x447f10 */
uint32_t GAMEAPI option_onoff_step(uint32_t v, uint32_t unused, int32_t event); /* 0x447f20 */
void GAMEAPI movie_win_lang6(void); /* 0x447ff0 */
void GAMEAPI menu_measure_all(void); /* 0x44cc50 */
void GAMEAPI menu_item_measure(uint8_t *it); /* 0x44ccf0 */
void GAMEAPI menu_loose_items_measure(void); /* 0x44ce20 */
void GAMEAPI menu_title_measure(void); /* 0x44ce40 */
int32_t GAMEAPI option_number_step(int32_t event, char *buf, int32_t v, int32_t def, int32_t lo, int32_t hi); /* 0x450050 */
void GAMEAPI menu_tiles_init(void); /* 0x453020 */
void GAMEAPI menu_tiles_draw(uint32_t ot, int32_t set, const uint8_t *rgb); /* 0x4530d0 */
void GAMEAPI menu_frame_sprites_init(void); /* 0x453130 */

/* ---- x87 helpers: the original's instructions ---- */

#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* fild m32; fstp m32: an integer to float, rounded by the control word. */
static inline uint32_t ph_float(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* The cursor bar's y: fild y; fstp t; fld t; fadd / fsub c; [fadd hf];
 * fild screen height; fstp hf2; fmul hf2; fdiv 0x4a33b0 (480.0); fstp. */
static inline uint32_t ph_bar_y(int32_t y, uint32_t c, int sub)
{
    uint32_t t, hf, r;
    if (sub)
        __asm__ volatile("fildl %[y]\n\tfstps %[t]\n\tflds %[t]\n\tfsubs %[c]\n\tfildl %[H]\n\tfstps %[hf]\n\t"
                         "fmuls %[hf]\n\tfdivs %[K]\n\tfstps %[r]"
                         : [t] "=m"(t), [hf] "=m"(hf), [r] "=m"(r)
                         : [y] "m"(y), [c] "m"(c), [H] "m"(*(const int32_t *)0x4acc3c),
                           [K] "m"(*(const float *)0x4a33b0)
                         : "st", "st(1)");
    else
        __asm__ volatile("fildl %[y]\n\tfstps %[t]\n\tflds %[t]\n\tfadds %[c]\n\tfildl %[H]\n\tfstps %[hf]\n\t"
                         "fmuls %[hf]\n\tfdivs %[K]\n\tfstps %[r]"
                         : [t] "=m"(t), [hf] "=m"(hf), [r] "=m"(r)
                         : [y] "m"(y), [c] "m"(c), [H] "m"(*(const int32_t *)0x4acc3c),
                           [K] "m"(*(const float *)0x4a33b0)
                         : "st", "st(1)");
    return r;
}

/* As ph_bar_y with the item's height (a float) added after the wobble. */
static inline uint32_t ph_bar_y2(int32_t y, uint32_t c, uint32_t h, int sub)
{
    uint32_t t, hf, r;
    if (sub)
        __asm__ volatile("fildl %[y]\n\tfstps %[t]\n\tflds %[t]\n\tfsubs %[c]\n\tfadds %[h]\n\tfildl %[H]\n\t"
                         "fstps %[hf]\n\tfmuls %[hf]\n\tfdivs %[K]\n\tfstps %[r]"
                         : [t] "=m"(t), [hf] "=m"(hf), [r] "=m"(r)
                         : [y] "m"(y), [c] "m"(c), [h] "m"(h), [H] "m"(*(const int32_t *)0x4acc3c),
                           [K] "m"(*(const float *)0x4a33b0)
                         : "st", "st(1)");
    else
        __asm__ volatile("fildl %[y]\n\tfstps %[t]\n\tflds %[t]\n\tfadds %[c]\n\tfadds %[h]\n\tfildl %[H]\n\t"
                         "fstps %[hf]\n\tfmuls %[hf]\n\tfdivs %[K]\n\tfstps %[r]"
                         : [t] "=m"(t), [hf] "=m"(hf), [r] "=m"(r)
                         : [y] "m"(y), [c] "m"(c), [h] "m"(h), [H] "m"(*(const int32_t *)0x4acc3c),
                           [K] "m"(*(const float *)0x4a33b0)
                         : "st", "st(1)");
    return r;
}

/* fld a; fsub b; fstp. */
static inline uint32_t ph_sub(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fcomp b; fnstsw ax: the status word's high byte (C0 = 1, C3 = 0x40). */
static inline uint32_t ph_cmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 0xff;
}

/* fild w; fstp t; fld t; fmul frac; fdiv max; call _ftol. */
static inline int32_t ph_bar_length(int32_t w, uint32_t frac, uint32_t max)
{
#if defined(__i386__)
    int32_t r;
    uint32_t t;
    __asm__ volatile("fildl %[w]\n\tfstps %[t]\n\tflds %[t]\n\tfmuls %[f]\n\tfdivs %[m]\n\tcall *%[ftol]"
                     : "=a"(r), [t] "=m"(t)
                     : [w] "m"(w), [f] "m"(frac), [m] "m"(max), [ftol] "S"(CRT_FTOL)
                     : "ecx", "edx", "memory", "cc", X87);
    return r;
#else
    /* single precision as the game's x87; _ftol truncates to 64 bits, eax
     * is the low half */
    float t = (float)w, f, m;

    memcpy(&f, &frac, 4);
    memcpy(&m, &max, 4);
    t = t * f;
    t = t / m;
    return (int32_t)(uint32_t)(int64_t)t;
#endif
}

/* Screen coordinates from the 640 x 480 layout (wrapping product, signed
 * truncating division). */
#define SX(v) ((int32_t)((uint32_t)(v) * G32(0x4acc38)) / 640)
#define SY(v) ((int32_t)((uint32_t)(v) * G32(0x4acc3c)) / 480)

/* rep movsd + rep movsb, forward (the original's inline strcpy / memcpy). */
static void ph_rep_movs(uint32_t dst, uint32_t src, uint32_t n)
{
    for (uint32_t k = n >> 2; k; k--, dst += 4, src += 4)
        G32(dst) = G32(src);
    for (uint32_t k = n & 3; k; k--)
        G8(dst++) = G8(src++);
}

static uint32_t ph_strlen(uint32_t s)
{
    uint32_t n = 0;
    while (G8(s + n))
        n++;
    return n;
}

/* ---- objects ---- */

/* 0x4427a0: an object's five trigger words (+0x16a): kind (top 4 bits) 1
 * starts effect n+1 (0x441370), 2 plays sound n (0x47dca0), 5 marks track
 * flag word n (0x6bfb34) with bit 1 (bits 0, 2, 3 cleared); each word run is
 * cleared; kind 0 ends the list (left as it is). The original also keeps
 * each word in its own argument slot (stack only). */
void GAMEAPI body_triggers_apply(uint8_t *obj)
{
    for (uint32_t i = 0; i < 5; i++) {
        uint32_t v = U16(obj, 0x16a + 2 * i);
        switch (v >> 12) {
        case 0:
            return;
        case 1:
            trigger_effect_start((v & 0xfff) + 1);
            break;
        case 2:
            trigger_sound(v & 0xfff);
            break;
        case 5: {
            uint32_t p = G32(0x6bfb34) + 2 * (v & 0xfff);
            G16(p) = (uint16_t)((G16(p) & 0xfff2) | 2);
            break;
        }
        default:
            break;
        }
        U16(obj, 0x16a + 2 * i) = 0;
    }
}

/* ---- the glyph sheet packer ---- */

#define GS_IMAGE 0x4c2430 /* image: +2 s16 width, +4 s16 stride, +6 s16 height, +0x10 palette, +0x14 pixels */
#define GS_PAL 0x4c1bf0   /* 256 x RGBA, dark entries 0 */
#define GS_ROWH G32(0x4c1ff0)
#define GS_PAGE G32(0x4c1ff4)
#define GS_X G32(0x4c1ff8)
#define GS_Y G32(0x4c1ffc)
#define GS_BUF G32(0x4c2000)
#define GS_STRIDE ((int32_t)GS16(0x4c2434))

/* 0x4432d0: cut `count` cells `cell_w` wide (count 0: the image width /
 * cell_w) from rows `row`..+`rows` of the image file `name` (loaded unless
 * `more`, with dark palette entries and their pixels cleared), crop each to
 * its ink (0x443260 from four sides; an empty cell is half wide, 1 high) and
 * pack them into 256 x 256 pages (0x4c2000, uploaded with 0x428c90 as each
 * fills and at the end unless `keep`, which also keeps the image). Each
 * cell's record (out +0x1c, 0x10 apart): left, top, width, height, page,
 * x+1, y+1 as int16; out +8 = lowest bottom - highest top, +0xc = count.
 * Returns 0 when the load or an upload fails, else 1. */
int32_t GAMEAPI glyph_sheet_pack(uint32_t name, int32_t cell_w, uint8_t *out, int32_t row, int32_t rows,
                                 int32_t count, int32_t keep, int32_t more)
{
    if (more == 0) {
        GS_BUF = (uint32_t)block_alloc_ph(0x10000);
        mem_fill_ph(GS_BUF, 0, 0x10000);
        if (image_load_ph(name, GS_IMAGE, 0) == 0)
            return 0;
        GS_PAGE = (uint32_t)texture_handle_new_ph();
        GS_X = 0;
        GS_Y = 0;
        GS_ROWH = 0;
        uint32_t s = G32(0x4c2440), d = GS_PAL;
        for (int32_t k = 0x100; k; k--, s += 4, d += 4) {
            G8(d) = G8(s);
            G8(d + 1) = G8(s + 1);
            G8(d + 2) = G8(s + 2);
            G8(d + 3) = 0;
            if ((uint32_t)((int32_t)(G8(s + 1) + G8(s + 2) + G8(s)) >> 2) < 0xf)
                G32(d) = 0;
        }
        int32_t n = (int32_t)GS16(0x4c2436) * (int32_t)GS16(0x4c2434);
        uint32_t p = G32(0x4c2444);
        G32(GS_PAL) = 0;
        if (n - 1 >= 0) {
            for (; n; n--, p++)
                if (G32(GS_PAL + 4 * G8(p)) == 0)
                    G8(p) = 0;
        }
    }
    int32_t h = GS16(0x4c2436);
    if (row >= h)
        row = h - 1;
    if (rows + row > h)
        rows = h - row;
    if (count == 0)
        count = (int32_t)GS16(0x4c2432) / cell_w;
    int32_t max_bottom = -0x8000, min_top = 0x8000;
    if (count > 0) {
        uint32_t col = 0;
        uint8_t *rec = out + 0x1e;
        for (int32_t k = 0; k < count; k++) {
            int32_t stride = GS_STRIDE;
            uint32_t src = (uint32_t)(stride * row) + col + G32(0x4c2444);
            int32_t left = bitmap_scan_ph(src, 1, cell_w - 1, stride, rows - 1, GS_PAL);
            int32_t r = bitmap_scan_ph(src + (uint32_t)(cell_w - 1), -1, cell_w - 1, GS_STRIDE, rows - 1, GS_PAL);
            int32_t right = cell_w - r - 1;
            int32_t top = bitmap_scan_ph(src, GS_STRIDE, rows - 1, 1, cell_w - 1, GS_PAL);
            stride = GS_STRIDE;
            r = bitmap_scan_ph((uint32_t)((rows - 1) * stride) + src, -stride, rows - 1, 1, cell_w - 1, GS_PAL);
            int32_t bottom = rows - r - 1;
            if (left == -1) {
                left = 0;
                top = 0;
                right = cell_w / 2;
                bottom = 1;
            }
            int32_t w3 = right - left + 3, h3 = bottom - top + 3;
            if ((int32_t)GS_X + w3 > 0x100) {
                uint32_t t = GS_ROWH;
                GS_ROWH = 0;
                GS_Y += t;
                GS_X = 0;
            }
            if (h3 > (int32_t)GS_ROWH)
                GS_ROWH = (uint32_t)h3;
            if (bottom > max_bottom)
                max_bottom = bottom;
            if (top < min_top)
                min_top = top;
            if ((int32_t)GS_Y + h3 > 0x100) {
                int32_t e = (int32_t)(GS_ROWH + GS_Y);
                if (e > 0x100)
                    e = 0x100;
                if (texture_upload_ph(GS_PAGE, 0x100, e, GS_BUF, GS_PAL, 0) == 0)
                    return 0;
                mem_fill_ph(GS_BUF, 0, 0x10000);
                GS_PAGE++;
                GS_ROWH = 0;
                GS_Y = 0;
                GS_X = 0;
            }
            int32_t lines = h3 - 2;
            for (int32_t i = 0; i < lines; i++)
                mem_copy_ph(((GS_Y + (uint32_t)i) << 8) + GS_X + GS_BUF + 0x101,
                            (uint32_t)((i + top) * GS_STRIDE) + (uint32_t)left + src, w3 - 2);
            U16(rec, -2) = (uint16_t)left;
            U16(rec, 0) = (uint16_t)top;
            U16(rec, 2) = (uint16_t)(right - left + 1);
            U16(rec, 4) = (uint16_t)(bottom - top + 1);
            U16(rec, 6) = (uint16_t)GS_PAGE;
            U16(rec, 8) = (uint16_t)(GS_X + 1);
            U16(rec, 10) = (uint16_t)(GS_Y + 1);
            GS_X += (uint32_t)w3;
            rec += 0x10;
            col += (uint32_t)cell_w;
        }
    }
    if (keep == 0) {
        if (GS_X != 0 || GS_Y != 0) {
            int32_t e = (int32_t)(GS_ROWH + GS_Y);
            if (e > 0x100)
                e = 0x100;
            if (texture_upload_ph(GS_PAGE, 0x100, e, GS_BUF, GS_PAL, 0) == 0)
                return 0;
        }
        image_release_ph(GS_IMAGE);
        block_free_size_ph(0x10000);
    }
    I32(out, 8) = max_bottom - min_top;
    I32(out, 0xc) = count;
    return 1;
}

/* ---- the heap (K&R malloc on one GlobalAlloc block) ---- */

#define HEAP_BASE 0x4c2460 /* the list head: +0 next, +4 size (0) */
#define HEAP_SIZE G32(0x4c2468)
#define HEAP_TOP G32(0x4c246c)
#define HEAP_START G32(0x4c2474)
#define HEAP_FREEP G32(0x4c2470)
#define HEAP_FIRST G32(0x4c2478)

/* 0x444500: once (0x4c2478 unset): a 0x9c4000-byte zeroed GlobalAlloc block
 * (fatal error when refused), then one malloc of nearly all of it, freed
 * again, so the free list starts as one block. */
void GAMEAPI kr_heap_init(void)
{
    if (HEAP_FIRST != 0)
        return;
    uint32_t p = (uint32_t)(uintptr_t)GlobalAlloc(0x40, 0x9c4000);
    HEAP_START = p;
    if (p == 0) {
        fatal_error_ph(0x4af5b0);
        return;
    }
    HEAP_TOP = p;
    HEAP_SIZE = 0x9c4000;
    HEAP_FREEP = 0;
    HEAP_FIRST = kr_malloc(0x9c3bf8);
    if (HEAP_FIRST != 0)
        kr_free(HEAP_FIRST);
}

/* 0x444570: free: the block (header 8 bytes before) goes back into the
 * address-ordered circular list, merged with its neighbours. Returns the
 * new list pointer. */
uint32_t GAMEAPI kr_free(uint32_t ap)
{
    uint32_t bp = ap - 8, p = HEAP_FREEP;
    for (;;) {
        if (bp > p && bp < U32(p, 0))
            break;
        uint32_t nx = U32(p, 0);
        if (p >= nx && (bp > p || bp < nx))
            break;
        p = nx;
    }
    uint32_t nx = U32(p, 0), sz = U32(bp, 4);
    if (bp + 8 * sz == nx) {
        U32(bp, 4) = U32(nx, 4) + sz;
        nx = U32(U32(p, 0), 0);
    }
    U32(bp, 0) = nx;
    uint32_t link = bp;
    if (p + 8 * U32(p, 4) == bp) {
        uint32_t add = U32(bp, 4);
        U32(p, 4) = U32(p, 4) + add;
        link = U32(bp, 0);
    }
    U32(p, 0) = link;
    HEAP_FREEP = p;
    return p;
}

/* 0x4445e0: malloc: first fit from the list pointer, in 8-byte units plus a
 * header (+0 next, +4 units); the tail of a larger block is handed out. 0
 * when the heap is exhausted. */
uint32_t GAMEAPI kr_malloc(uint32_t n)
{
    uint32_t units = ((n + 7) >> 3) + 1;
    uint32_t q = HEAP_FREEP, prev = q;
    if (q == 0) {
        G32(HEAP_BASE + 4) = 0;
        HEAP_FREEP = HEAP_BASE;
        G32(HEAP_BASE) = HEAP_BASE;
        q = prev = HEAP_BASE;
    }
    uint32_t p = U32(prev, 0);
    while (U32(p, 4) < units) {
        if (p == q) {
            p = kr_morecore((int32_t)units);
            if (p == 0)
                return 0;
            q = HEAP_FREEP;
        }
        prev = p;
        p = U32(p, 0);
    }
    uint32_t size = U32(p, 4);
    if (size == units) {
        U32(prev, 0) = U32(p, 0);
        HEAP_FREEP = prev;
        return p + 8;
    }
    size -= units;
    U32(p, 4) = size;
    p += 8 * size;
    U32(p, 4) = units;
    HEAP_FREEP = prev;
    return p + 8;
}

/* 0x444670: morecore: at least `nu` units, rounded up to 128, from sbrk,
 * freed into the list. Returns the list pointer, 0 when sbrk refuses. */
uint32_t GAMEAPI kr_morecore(int32_t nu)
{
    int32_t t = nu + 0x7f;
    t += (int32_t)((uint32_t)(t >> 31) & 0x7f);
    uint32_t units = (uint32_t)(t >> 7) << 7;
    uint32_t up = kr_sbrk(units * 8);
    if (up == 0xffffffff)
        return 0;
    U32(up, 4) = units;
    kr_free(up + 8);
    return HEAP_FREEP;
}

/* 0x4446c0: sbrk: `n` bytes from the GlobalAlloc block, -1 past its end. */
uint32_t GAMEAPI kr_sbrk(uint32_t n)
{
    uint32_t top = HEAP_TOP + n;
    if (top > HEAP_SIZE + HEAP_START)
        return 0xffffffff;
    HEAP_TOP = top;
    return top - n;
}

/* ---- menus ---- */

/* 0x445340: `src` into `dst` cut until its width in `font` (0x42e8f0 with
 * the format at 0x4afa08) is at most `max_w`, one character more
 * each time: mode 1 from the end, 2 from both ends (half from the front),
 * 3 from the front. Nothing when max_w < 0 or a pointer is null (the
 * original measures src before checking it: a null src faults there). */
void GAMEAPI menu_text_fit(char *dst, const char *src, int32_t max_w, int32_t mode, uint32_t font)
{
    if (max_w < 0 || dst == 0 || src == 0)
        return;
    int32_t len = (int32_t)ph_strlen(A(src));
    dst[0] = 0;
    if (len <= 0)
        return;
    uint32_t d = A(dst), s = A(src), cursor = d;
    for (int32_t i = 0;;) {
        if (mode == 1) {
            ph_rep_movs(d, s, ph_strlen(s) + 1);
            G8(cursor + ph_strlen(d)) = 0;
        } else if (mode == 2) {
            int32_t half = i / 2;
            ph_rep_movs(d, s + (uint32_t)half, ph_strlen(s + (uint32_t)half) + 1);
            G8(d + ph_strlen(d) + (uint32_t)(half - i)) = 0;
        } else if (mode == 3) {
            ph_rep_movs(d, s + (uint32_t)i, ph_strlen(s + (uint32_t)i) + 1);
        } else {
            return;
        }
        if (text_width_printf(font, 0, 0, 0, 0x4afa08, d) <= max_w)
            return;
        i++;
        cursor--;
        if (len <= i)
            return;
    }
}

#define BAR_SPRITES 0x4c2ae0 /* 2 slots x 4 sprites (0x84): ends, two wobbling arrows */
#define BAR_BACKS 0x4c2f10   /* 2 slots: the bar behind the cursor row */

/* 0x446250: the cursor bar's sprites: per slot (2) the back (texture 5,
 * flag 1) and four sprites (textures 5, 5, 0x29, 0x2a; flag cleared twice). */
void GAMEAPI menu_bar_sprites_init(void)
{
    int32_t j = 0;
    for (uint32_t back = BAR_BACKS; (int32_t)back < 0x4c3018; back += 0x84, j += 4) {
        sprite_gouraud_set(back, 5, 0, 0, 0, -1, -1, 0x451c4000);
        prim_flag2_set_ph(back, 1);
        for (int32_t k = 0; k < 4; k++) {
            uint32_t t = 5;
            if (k == 2)
                t = 0x29;
            else if (k == 3)
                t = 0x2a;
            uint32_t q = BAR_SPRITES + (uint32_t)(j + k) * 0x84;
            sprite_gouraud_set(q, t, 0, 0, 0, -1, -1, 0x451c4000);
            prim_flag2_set_ph(q, 0);
            prim_flag2_set_ph(q, 0);
        }
    }
}

/* Four colours (bytes 0-2 of each of col's four dwords) into a quad's
 * vertices (+4, +0x24, +0x44, +0x64), alpha 0xff. */
static void bar_colours(uint32_t q, const uint8_t *col)
{
    G8(q + 4) = col[0];
    G8(q + 5) = col[1];
    G8(q + 6) = col[2];
    G8(q + 0x24) = col[4];
    G8(q + 0x25) = col[5];
    G8(q + 0x26) = col[6];
    G8(q + 0x44) = col[8];
    G8(q + 0x45) = col[9];
    G8(q + 0x46) = col[10];
    G8(q + 0x64) = col[12];
    G8(q + 0x65) = col[13];
    G8(q + 0x66) = col[14];
}

static void quad_z(uint32_t q, uint32_t z)
{
    for (uint32_t v = 0; v < 4; v++) {
        G32(q + 0xc + 0x20 * v) = z;
        G32(q + 8 + 0x20 * v) = 0x40;
    }
}

/* The wobbling arrow above (texture 0x29, y 0x4cb478, + wobble) or below
 * (0x2a, y 0x4cb480, - wobble) the cursor row, centred on it. */
static void bar_arrow(uint32_t ot, uint32_t q, uint32_t tex, uint32_t yg, uint32_t c, int sub, const uint8_t *col)
{
    int32_t w = fe_texture_width(tex);
    int32_t h = fe_texture_height(tex);
    int32_t hw = w / 2, hh = h / 2;
    int32_t xm = (GI32(0x4cb47c) + GI32(0x4cb474)) / 2;
    uint32_t xa = ph_float(SX(xm - hw));
    uint32_t ya = ph_bar_y(GI32(yg) - hh, c, sub);
    uint32_t xb = ph_float(SX(xm - hw + w));
    uint32_t hf = ph_float(h);
    uint32_t yb = ph_bar_y2(GI32(yg) - hh, c, hf, sub);
    G32(q + 0x10) = xa;
    G32(q + 0x14) = ya;
    G32(q + 0x30) = xb;
    G32(q + 0x34) = ya;
    G32(q + 0x50) = xa;
    G32(q + 0x54) = yb;
    G32(q + 0x70) = xb;
    G32(q + 0x74) = yb;
    quad_z(q, 0x44bb8000);
    G8(q + 7) |= 2;
    bar_colours(q, col);
    G16(q + 0x22) = 0xff;
    G8(q + 0x27) = 0xff;
    G8(q + 0x47) = 0xff;
    G8(q + 0x67) = 0xff;
    ot_add(ot, q);
}

/* The wobble per 8 frames (0x4be320 >> 3 & 15); 14 repeats 13 (sic). */
static const uint32_t bar_wobble[16] = {
    0x00000000, 0x3f000000, 0x3f800000, 0x3fc00000, 0x40000000, 0x3fc00000, 0x3f800000, 0x3f000000,
    0x00000000, 0xbf000000, 0xbf800000, 0xbfc00000, 0xc0000000, 0xbfc00000, 0xbfc00000, 0xbf000000,
};

/* 0x446300: when a menu is up (0x4cb49c), not hidden (0x4cb500) and the bar
 * on (0x4cb50c): the two end sprites (texture 5) at the cursor's left / right
 * (0x4cb474 / 0x4cb47c, y 0x4cb510) unless 0x4cb4fc / 0x4cb508, the arrows
 * when 0x4cb4ec / 0x4cb4f0, all coloured from `col` into `ot`; then the back
 * (x 0x4cb474+15 .. 0x4cb47c-15, rows from 0x4cb518 / 0x4cb520 and `row`)
 * in the pulsing colour (0x4328c0 of `pulse`), alpha twice its last blue
 * (max 0xff), linked by 0x457d10 into `ot_text`. Arguments 6 and 7 unused. */
void GAMEAPI menu_cursor_bar_draw(uint32_t ot_text, uint32_t ot, int32_t slot, uint32_t pulse, const uint8_t *col,
                                  uint32_t unused6, uint32_t unused7, int32_t row)
{
    (void)unused6;
    (void)unused7;
    if (G32(0x4cb49c) == 0 || G32(0x4cb500) != 0 || G32(0x4cb50c) == 0)
        return;
    uint32_t c = bar_wobble[(GI32(0x4be320) >> 3) & 0xf];
    uint32_t base = BAR_SPRITES + (uint32_t)slot * 0x210;
    if (G32(0x4cb4fc) == 0 && G32(0x4cb508) == 0) {
        int32_t w = fe_texture_width(5);
        int32_t h = fe_texture_height(5);
        int32_t hw = w / 2, hh = h / 2;
        uint32_t x0 = ph_float(SX(GI32(0x4cb474) - hw));
        G32(base + 0x10) = x0;
        uint32_t y0 = ph_float(SY(GI32(0x4cb510) - hh));
        G32(base + 0x14) = y0;
        uint32_t x1 = ph_float(SX(w - hw + GI32(0x4cb474)));
        G32(base + 0x30) = x1;
        G32(base + 0x34) = y0;
        G32(base + 0x50) = x0;
        uint32_t y1 = ph_float(SY(h - hh + GI32(0x4cb510)));
        G32(base + 0x54) = y1;
        G32(base + 0x70) = x1;
        G32(base + 0x74) = y1;
        quad_z(base, 0x44bb8000);
        uint32_t x2 = ph_float(SX(GI32(0x4cb47c) - hw));
        G32(base + 0x94) = x2;
        G32(base + 0x98) = y0;
        uint32_t x3 = ph_float(SX(w - hw + GI32(0x4cb47c)));
        G32(base + 0xb4) = x3;
        G32(base + 0xb8) = y0;
        G32(base + 0xd4) = x2;
        G32(base + 0xd8) = y1;
        G32(base + 0xf4) = x3;
        G32(base + 0xf8) = y1;
        quad_z(base + 0x84, 0x44bb8000);
        G8(base + 7) |= 2;
        G8(base + 0x8b) |= 2;
        for (uint32_t q = base, k = 2; k; k--, q += 0x84) {
            bar_colours(q, col);
            G16(q + 0x22) = 0xff;
            G8(q + 0x27) = 0xff;
            G8(q + 0x47) = 0xff;
            G8(q + 0x67) = 0xff;
            ot_add(ot, q);
        }
    }
    if (G32(0x4cb4ec) != 0)
        bar_arrow(ot, base + 0x108, 0x29, 0x4cb478, c, 0, col);
    if (G32(0x4cb4f0) != 0)
        bar_arrow(ot, base + 0x18c, 0x2a, 0x4cb480, c, 1, col);

    int32_t x0 = GI32(0x4cb474) + 0xf;
    int32_t dx = GI32(0x4cb47c) - GI32(0x4cb474) - 0x1e;
    int32_t n = 2 * row - GI32(0x4cb518) + GI32(0x4cb520) + 1;
    int32_t span = n + (n >> 1);
    int32_t y0 = GI32(0x4cb518) - ((n >> 1) >> 1) - row;
    uint32_t q = BAR_BACKS + (uint32_t)slot * 0x84;
    uint32_t fx0 = ph_float(SX(x0));
    G32(q + 0x10) = fx0;
    uint32_t fy0 = ph_float(SY(y0));
    G32(q + 0x14) = fy0;
    uint32_t fx1 = ph_float(SX(dx + x0));
    G32(q + 0x30) = fx1;
    G32(q + 0x34) = fy0;
    G32(q + 0x50) = fx0;
    uint32_t fy1 = ph_float(SY(y0 + span));
    G32(q + 0x54) = fy1;
    G32(q + 0x70) = fx1;
    G32(q + 0x74) = fy1;
    quad_z(q, 0x451c4000);
    uint8_t pc[16] = {0};
    colour_pulse(pc, pulse, 4, 1);
    for (uint32_t v = 0; v < 4; v++) {
        G8(q + 4 + 0x20 * v) = pc[4 * v];
        G8(q + 5 + 0x20 * v) = pc[4 * v];
        G8(q + 6 + 0x20 * v) = pc[4 * v];
    }
    uint32_t a = (uint32_t)pc[12] * 2;
    if ((int32_t)a > 0xff)
        a = 0xff;
    G16(q + 0x22) = (uint16_t)a;
    G8(q + 0x27) = (uint8_t)a;
    G8(q + 0x47) = (uint8_t)a;
    G8(q + 0x67) = (uint8_t)a;
    quad_link_ph(q + 0x42, q + 0x20, q + 0x40, q + 0x60, q + 0x80);
    ot_add(ot_text, q);
}

#define g_menu_rows G32(0x4cb4b8)
#define g_menu_count GI32(0x4cb49c)

/* 0x446ea0: per menu row of kind 4 that is shown: its slider (row +0x534
 * value, +0x540 position, +0x53c range): the track (texture 0x2b, z 1000,
 * at row +0x54c minus its width, +0x550 centred; slot quads +0x554, 0x84
 * apart) and the filled part as a flat quad (+0x65c, 0x68 apart, colour row
 * +0x72c or 0x4af670) as long as (position - value) clamped to 0..range
 * scales the width (_ftol); both into `ot`, the track half transparent. */
void GAMEAPI menu_slider_bars_draw(uint32_t ot, int32_t slot)
{
    for (int32_t i = 0; i < g_menu_count; i++) {
        uint32_t off = (uint32_t)i * 0x730;
        if (U8(g_menu_rows, off) != 4 || U8(g_menu_rows, off + 1) == 0)
            continue;
        uint32_t colour = U32(g_menu_rows, off + 0x72c);
        if (colour == 0)
            colour = 0x4af670;
        uint32_t qa = off + (uint32_t)slot * 0x84, qb = off + (uint32_t)slot * 0x68;
        sprite_gouraud_set(g_menu_rows + qa + 0x554, 0x2b, 0x4ac99c, 0, 0, -1, -1, 0x447a0000);
        flat_quad_set(g_menu_rows + qb + 0x65c, colour, 0, 0, -1, -1, 0x447a0000);
        uint32_t r = g_menu_rows + off;
        uint32_t frac = ph_sub(U32(r, 0x540), U32(r, 0x534));
        uint32_t range = U32(r, 0x53c);
        if (ph_cmp(frac, G32(0x4a3318)) & 0x01)
            frac = 0;
        if (!(ph_cmp(frac, range) & 0x41))
            frac = range;
        int32_t w = fe_texture_width(0x2b);
        int32_t len = ph_bar_length(w, frac, range);
        int32_t h = fe_texture_height(0x2b);
        r = g_menu_rows + off;
        int32_t x = I32(r, 0x54c) - w;
        int32_t y = I32(r, 0x550) - h / 2;
        int32_t y1 = h + y;
        uint32_t a = g_menu_rows + qa, b;
        G32(a + 0x564) = ph_float(SX(x));
        G32(g_menu_rows + qa + 0x568) = ph_float(SY(y));
        G32(g_menu_rows + qa + 0x584) = ph_float(SX(x + w));
        G32(g_menu_rows + qa + 0x588) = ph_float(SY(y));
        G32(g_menu_rows + qa + 0x5a4) = ph_float(SX(x));
        G32(g_menu_rows + qa + 0x5a8) = ph_float(SY(y1));
        G32(g_menu_rows + qa + 0x5c4) = ph_float(SX(x + w));
        G32(g_menu_rows + qa + 0x5c8) = ph_float(SY(y1));
        quad_z(g_menu_rows + qa + 0x554, 0x447a0000);
        b = g_menu_rows + qb;
        G32(b + 0x66c) = ph_float(SX(x));
        G32(g_menu_rows + qb + 0x670) = ph_float(SY(y));
        G32(g_menu_rows + qb + 0x684) = ph_float(SX(x + len));
        G32(g_menu_rows + qb + 0x688) = ph_float(SY(y));
        G32(g_menu_rows + qb + 0x69c) = ph_float(SX(x));
        G32(g_menu_rows + qb + 0x6a0) = ph_float(SY(y1));
        G32(g_menu_rows + qb + 0x6b4) = ph_float(SX(x + len));
        G32(g_menu_rows + qb + 0x6b8) = ph_float(SY(y1));
        /* the flat quad's vertices are 0x18 apart */
        for (uint32_t v = 0; v < 4; v++) {
            G32(g_menu_rows + qb + 0x668 + 0x18 * v) = 0x447a0000;
            G32(g_menu_rows + qb + 0x664 + 0x18 * v) = 0x40;
        }
        G8(g_menu_rows + qb + 0x663) |= 2;
        G8(g_menu_rows + qa + 0x55b) |= 2;
        G16(g_menu_rows + qa + 0x576) = 0x80;
        G8(g_menu_rows + qa + 0x57b) = 0x80;
        G8(g_menu_rows + qa + 0x59b) = 0x80;
        G8(g_menu_rows + qa + 0x5bb) = 0x80;
        ot_add(ot, g_menu_rows + qa + 0x554);
        ot_add(ot, g_menu_rows + qb + 0x65c);
    }
}

/* 0x447f00 / 0x447f10: thunks to the held-button resets 0x4474e0 / 0x447500. */
void GAMEAPI menu_hold_clear_thunk(void)
{
    GCALL(0x4474e0, menu_hold_clear)();
}

void GAMEAPI menu_hold_reset_thunk(void)
{
    GCALL(0x447500, menu_hold_reset)();
}

/* 0x447f20: an on / off option's step: events 0xe..0x10 flip it; 0x4cb46c
 * = whether it changed. Returns the value. */
uint32_t GAMEAPI option_onoff_step(uint32_t v, uint32_t unused, int32_t event)
{
    (void)unused;
    uint32_t was = v != 0;
    if (event >= 0xe && event <= 0x10)
        v = v == 0;
    G32(0x4cb46c) = was != (v != 0);
    return v;
}

/* 0x447ff0: the winner's movie in language 6 (0x42dbf0). */
void GAMEAPI movie_win_lang6(void)
{
    GCALL(0x42dbf0, movie_win)(6);
}

/* 0x44ccf0: an item's size: its text (+0x50, if any) laid out with
 * 0x42e810 (font +0x194, colour text +0x190 or 0x4aca0c, size word +0x44)
 * gives width (+30 with marks) and height, clipped by +0x48 / +0x4c; at
 * least +0x24 / +0x28, replaced by +0x2c / +0x30 unless -1, plus +0x34 /
 * +0x38. Kept in +0x1c / +0x20 with the string handle in +0; kinds (+4) 1
 * and 2 raise the maxima 0x4cb4bc / 0x4cb4c0 and 0x4cb4c4 / 0x4cb4c8. */
void GAMEAPI menu_item_measure(uint8_t *it)
{
    int32_t handle = -1, w = 0, h = 0;
    if (it != 0 && U8(it, 0x50) != 0) {
        uint32_t colour = U32(it, 0x190);
        if (colour == 0)
            colour = 0x4aca0c;
        handle = string_printf_ph(U32(it, 0x194), colour, (A(it + 0x50) & 0xffff0000) | U16(it, 0x44), 0, it + 0x50,
                                  it + 0x90, it + 0xd0, it + 0x110, it + 0x150);
        if (handle != -1) {
            w = string_width_ph(handle);
            h = string_height_ph(handle);
            if (string_marks_ph(handle) != 0)
                w += 0x1e;
            if (I32(it, 0x48) > 0)
                string_clip_ph(handle, I32(it, 0x48), I32(it, 0x4c));
        }
    }
    if (w < I32(it, 0x24))
        w = I32(it, 0x24);
    if (h < I32(it, 0x28))
        h = I32(it, 0x28);
    if (I32(it, 0x2c) != -1)
        w = I32(it, 0x2c);
    if (I32(it, 0x30) != -1)
        h = I32(it, 0x30);
    w += I32(it, 0x34);
    h += I32(it, 0x38);
    int8_t kind = (int8_t)U8(it, 4);
    if (kind == 1) {
        if (w > GI32(0x4cb4bc))
            G32(0x4cb4bc) = (uint32_t)w;
        if (h > GI32(0x4cb4c0))
            G32(0x4cb4c0) = (uint32_t)h;
    } else if (kind == 2) {
        if (w > GI32(0x4cb4c4))
            G32(0x4cb4c4) = (uint32_t)w;
        if (h > GI32(0x4cb4c8))
            G32(0x4cb4c8) = (uint32_t)h;
    }
    I32(it, 0x1c) = w;
    I32(it, 0) = handle;
    I32(it, 0x20) = h;
}

/* 0x44ce20: the 64 loose items (0x4c30e0, 0x1a0 apart). */
void GAMEAPI menu_loose_items_measure(void)
{
    for (uint32_t it = 0x4c30e0; (int32_t)it < 0x4c98e0; it += 0x1a0)
        menu_item_measure((uint8_t *)(uintptr_t)it);
}

/* 0x44ce40: the title item 0x4c2920. */
void GAMEAPI menu_title_measure(void)
{
    menu_item_measure((uint8_t *)0x4c2920);
}

/* 0x44cc50: every item's size: 0x4cb590 and the kind-2 maxima cleared, the
 * title and loose items, the kind-1 maxima cleared (after them, sic), then
 * per menu row of kind 1..5 not hidden (+3) its three items (+0xc, +0x1ac,
 * +0x34c); other rows get +4 = 1. */
void GAMEAPI menu_measure_all(void)
{
    G32(0x4cb590) = 0;
    G32(0x4cb4c4) = 0;
    G32(0x4cb4c8) = 0;
    menu_title_measure();
    menu_loose_items_measure();
    G32(0x4cb4bc) = 0;
    G32(0x4cb4c0) = 0;
    for (int32_t i = 0; i < g_menu_count; i++) {
        uint32_t off = (uint32_t)i * 0x730;
        uint8_t kind = U8(g_menu_rows, off);
        if (kind == 0 || kind > 5 || U8(g_menu_rows, off + 3) != 0) {
            U8(g_menu_rows, off + 4) = 1;
        } else {
            menu_item_measure((uint8_t *)(uintptr_t)(g_menu_rows + off + 0xc));
            menu_item_measure((uint8_t *)(uintptr_t)(g_menu_rows + off + 0x1ac));
            menu_item_measure((uint8_t *)(uintptr_t)(g_menu_rows + off + 0x34c));
        }
    }
}

/* 0x450050: a number option's step (value 0 means `def`, at most `hi`):
 * event 0x12 prints the value (at least lo) into buf and 0x13 the maximum
 * (0x4b0230 "%d" / 0x4b022c), returning the value; 0x14 down (below lo
 * wraps to hi), 0x15 / 0x16 up (from at least lo; above hi wraps to lo);
 * else -1. */
int32_t GAMEAPI option_number_step(int32_t event, char *buf, int32_t v, int32_t def, int32_t lo, int32_t hi)
{
    if (v == 0)
        v = def;
    if (v > hi)
        v = hi;
    switch (event) {
    case 0x12:
        sprintf_ph(buf, 0x4b0230, v < lo ? lo : v);
        return v;
    case 0x13:
        sprintf_ph(buf, 0x4b022c, hi);
        return v;
    case 0x14:
        v--;
        if (v < lo)
            v = hi;
        return v;
    case 0x15:
    case 0x16:
        if (v < lo)
            v = lo;
        v++;
        if (v > hi)
            v = lo;
        return v;
    default:
        return -1;
    }
}

#define TILES 0x4cd950 /* 32 x 0x78 sprites, a copy 0xf00 on, chained in each set */

/* 0x453020: the 32 background tiles (64 x 64 from the 512 x 256 page,
 * textures 0x4b0610[i] + 10, z 10000), each copied to the second set and
 * chained to the previous one in both sets (0x4290a0). */
void GAMEAPI menu_tiles_init(void)
{
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t p = TILES + i * 0x78;
        sprite_512_set(p, G8(0x4b0610 + i) + 10, 0x4ac99c, (i & 7) << 6, (i >> 3) << 6, 0x40, 0x40, 0x461c4000);
        ph_rep_movs(p + 0xf00, p, 0x78);
        if (i != 0) {
            dword_store_ph(p - 0x78, p);
            dword_store_ph(p + 0xe88, p + 0xf00);
        }
    }
}

/* 0x4530d0: set `set` (0 / non-0) of the tiles coloured rgb, its chain put
 * into `ot` (0x429080, the last tile's link 0x4ce7d8). */
void GAMEAPI menu_tiles_draw(uint32_t ot, int32_t set, const uint8_t *rgb)
{
    uint32_t off = set != 0 ? 0xf00 : 0;
    uint32_t a = TILES + 4 + off;
    for (int32_t k = 32; k; k--, a += 0x78) {
        G8(a) = rgb[0];
        G8(a + 1) = rgb[1];
        G8(a + 2) = rgb[2];
    }
    ot_link_ph(ot, TILES + off, 0x4ce7d8 + off);
}

/* 0x453130: the three frame sprites (textures 0, 2, 6, one texel, z 4500 /
 * 4500 / 4000) with their texture edges pulled in by one, each copied
 * 0x168 on and chained to the previous one in both sets. */
void GAMEAPI menu_frame_sprites_init(void)
{
    sprite_512_set(0x4cf7c8, 0, 0x4ac99c, 0, 0, 1, 1, 0x458ca000);
    sprite_512_set(0x4cf840, 2, 0x4ac99c, 0, 0, 1, 1, 0x458ca000);
    sprite_512_set(0x4cf8b8, 6, 0x4ac99c, 0, 0, 1, 1, 0x457a0000);
    uint8_t a = G8(0x4cf8f4), b = G8(0x4cf92c);
    G8(0x4cf8f4) = (uint8_t)(a - 1);
    G8(0x4cf92c) = (uint8_t)(b - 1);
    for (uint32_t s = 0x4cf804; (int32_t)s < 0x4cf96c; s += 0x78) {
        G8(s) = (uint8_t)(G8(s) - 1);
        G8(s + 0x38) = (uint8_t)(G8(s + 0x38) - 1);
        G8(s - 0x1c) = (uint8_t)(G8(s - 0x1c) + 1);
        G8(s + 0x1c) = (uint8_t)(G8(s + 0x1c) + 1);
        ph_rep_movs(s + 0x12c, s - 0x3c, 0x78);
        if (s != 0x4cf804) {
            dword_store_ph(s - 0xb4, s - 0x3c);
            dword_store_ph(s + 0xb4, s + 0x12c);
        }
    }
}
