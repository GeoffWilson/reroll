/* Group PE: the original's still-reachable functions in 0x42fc40-0x432cd0
 * (the end of the front end's text source file and the front end's sprite /
 * colour helpers).
 *
 *  - text: 9-digit decimal formatting (0x42fc40) and parsing (0x42fcd0),
 *    the free text-string slot (0x42fd10), the tab stops (0x42fd60), one
 *    character's sprite pair (0x42fd90), the language (0x430120);
 *  - front-end sprites: a textured quad scaled from 640 x 480 with four
 *    colours (0x430ea0, packet code 0x8c) or one (0x4310f0, code 0x98), the
 *    same scaled from 512 x 256 (0x431300), an untextured quad (0x4314d0,
 *    code 0xa8), the 9-piece text box frame (0x4316d0 set-up, 0x431780
 *    placing);
 *  - colours: the fade (0x432820, 0x4328a0) and the pulsing highlight
 *    (0x4328c0);
 *  - a front-end mode's race set-up (0x432cd0).
 *
 * Float steps are int -> float conversions (fild / fstp in the original),
 * done here with the same x87 instructions. Everything outside the group is
 * called by address. */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

#define g_race G32(0x4b1960)
#define g_options G32(0x4b2198)
#define g_screen_w GI32(0x4acc38)
#define g_screen_h GI32(0x4acc3c)
#define g_fade GI32(0x4ac990)

/* int -> float as the original: fild m32, fstp m32. */
static inline void pe_fild_fstp(int32_t v, uint32_t at)
{
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(*(float *)(uintptr_t)at) : "m"(v));
}

/* Screen coordinates from the 640 x 480 layout (signed truncating division). */
#define SX(v) ((int32_t)((uint32_t)(v) * (uint32_t)g_screen_w) / 640)
#define SY(v) ((int32_t)((uint32_t)(v) * (uint32_t)g_screen_h) / 480)
#define FX(p, off, v) pe_fild_fstp(SX(v), A(p) + (off))
#define FY(p, off, v) pe_fild_fstp(SY(v), A(p) + (off))

#define FLOAT_2000 0x44fa0000u
#define FLOAT_3000 0x453b8000u

/* 0x42fc40: v as 9 decimal digits (100000000 down to 1) and a NUL. The first
 * "digit" is v / 100000000 + '0' whatever its size (sic). */
void GAMEAPI decimal9_format(char *out, uint32_t v)
{
    static const uint32_t powers[9] = {100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1};
    for (int i = 0; i < 9; i++) {
        uint32_t q = v / powers[i];
        v -= powers[i] * q;
        *out++ = (char)(q + '0');
    }
    *out = 0;
}

/* 0x42fcd0: decimal digits at the start of s into *value; returns how many. */
int32_t GAMEAPI decimal_parse(const uint8_t *s, int32_t *value)
{
    int32_t n = 0, v = 0;
    uint8_t c = s[0];
    while (c >= '0' && c <= '9') {
        v = v * 10 + c - '0';
        n++;
        c = s[n];
    }
    *value = v;
    return n;
}

/* 0x42fd10: the first of the 128 text-string records (0x14c bytes at
 * *0x4be310) without flag 0x40 in byte +1, searching round from *0x4be2f8
 * (0 when out of range); -1 when all are in use. */
int32_t GAMEAPI text_slot_free(void)
{
    int32_t i = GI32(0x4be2f8);
    if (i < 0 || i >= 0x80)
        i = 0;
    uint32_t table = G32(0x4be310);
    for (int32_t n = 0; n < 0x80; n++) {
        if (!(U8(table, i * 0x14c + 1) & 0x40))
            return i;
        if (++i >= 0x80)
            i = 0;
    }
    return -1;
}

/* 0x42fd60: the first of the 8 tab stops (words at 0x4ac850) beyond x, else
 * the last. The high half of eax is the table pointer's (0x004a). */
uint32_t GAMEAPI text_tab_next(uint32_t x)
{
    uint16_t v = (uint16_t)x;
    for (int i = 0; i < 8; i++)
        if (v < G16(0x4ac850 + 2 * i))
            return 0x004a0000u | G16(0x4ac850 + 2 * i);
    return 0x004a0000u | G16(0x4ac85e);
}

/* 0x42fd90: one character of a font as a pair of textured quads (packet code
 * 0x8c, 0x84 bytes): the font (0x42e050), its glyph record, the vertex
 * words at *0x4be304 and the packets at *0x4be308 / *0x4be30c (slot word
 * 0x4be2fc, counted up) or, with flags & 0x10, the single packet *0x4be2e8
 * and the words at 0x4be2c8. *x moves on by the glyph's width and the
 * font's spacing; *height is the font's height. 0, or -1 for no font, a
 * code above 255, a character the font lacks (logged unless 0xd6) or no
 * free slot. */
int32_t GAMEAPI glyph_place(uint32_t font_id, uint32_t code, const uint8_t *colours, int32_t y, int16_t *x,
                            int16_t *height, uint32_t flags)
{
    uint32_t font = GCALL(0x42e050, font_by_id)(font_id);
    *height = 0;
    if (!font)
        return -1;
    *height = (int16_t)((int32_t)(U32(font, 8) * U32(font, 4)) >> 8);
    if (code > 0xff)
        return -1;
    uint8_t glyph = U8(font, 0x101c + code);
    if (glyph == 0xff) {
        if (code != 0xd6)
            GCALL(0x476ec0, dbg_vlog_printf)((const char *)GMEM(0x4ac950), code);
        return -1;
    }
    uint32_t a, b, w;
    uint16_t own = (uint16_t)(flags & 0x10);
    if (own) {
        a = b = G32(0x4be2e8);
        w = 0x4be2c8;
    } else {
        if (G16(0x4be2fc) >= 0x800)
            return -1;
        uint32_t slot = G16(0x4be2fc);
        a = G32(0x4be308) + slot * 0x84;
        b = G32(0x4be30c) + slot * 0x84;
        w = G32(0x4be304) + slot * 32;
    }
    uint32_t g = font + 0x1c + glyph * 16;
    uint16_t tpage = U16(g, 8) | 0x40;
    uint8_t u0 = U8(g, 0xa);
    uint8_t u1 = (uint8_t)(u0 + U8(g, 4) - 1);
    uint8_t v0 = U8(g, 0xc);
    uint8_t v1 = (uint8_t)(U8(g, 6) + v0 - 1);
    int32_t scale = I32(font, 4);
    int32_t gw = (int32_t)(U16(g, 4) * (uint32_t)scale) >> 8;
    int32_t g2 = U16(g, 2);
    int32_t gy1 = (int32_t)((U16(g, 6) + (uint32_t)g2) * (uint32_t)scale) >> 8;
    int32_t ya = ((int32_t)((uint32_t)g2 * (uint32_t)scale) >> 8) + y;
    int32_t yb = gy1 + y;
    U16(w, 4) = 0;
    U16(w, 0) = (uint16_t)*x;
    U16(w, 2) = (uint16_t)ya;
    U16(w, 0xc) = 0;
    U16(w, 8) = (uint16_t)(*x + gw);
    U16(w, 0xa) = (uint16_t)ya;
    U16(w, 0x10) = (uint16_t)*x;
    U16(w, 0x14) = 0;
    U16(w, 0x12) = (uint16_t)yb;
    U16(w, 0x1c) = 0;
    U16(w, 0x18) = (uint16_t)(*x + gw);
    U16(w, 0x1a) = (uint16_t)yb;
    *x = (int16_t)(*x + gw);
    *x = (int16_t)(((int32_t)(U32(font, 0x14) * U32(font, 4)) >> 8) + *x);
    U8(b, 0x20) = u0;
    U8(a, 0x20) = u0;
    U8(b, 0x21) = v0;
    U8(a, 0x21) = v0;
    U8(b, 0x40) = u1;
    U8(a, 0x40) = u1;
    U8(b, 0x41) = v0;
    U8(a, 0x41) = v0;
    U8(b, 0x60) = u0;
    U8(a, 0x60) = u0;
    U8(b, 0x61) = v1;
    U8(a, 0x61) = v1;
    U8(b, 0x80) = u1;
    U8(a, 0x80) = u1;
    U8(b, 0x81) = v1;
    U8(a, 0x81) = v1;
    U16(b, 0x42) = tpage;
    U16(a, 0x42) = tpage;
    uint32_t t = (U32(a, 0) & 0xffffff) | 0xc000000;
    U16(a, 0x22) = 0xff;
    U32(a, 0) = t;
    U8(a, 7) = 0x8c;
    t = U32(b, 0) & 0xffffff;
    U8(b, 7) = 0x8c;
    U32(b, 0) = t | 0xc000000;
    U16(a, 0x22) = 0xff;
    U8(a, 0x67) = 0xff;
    U8(a, 0x47) = 0xff;
    U8(a, 0x27) = 0xff;
    U16(b, 0x22) = 0xff;
    U8(b, 0x67) = 0xff;
    U8(b, 0x47) = 0xff;
    U8(b, 0x27) = 0xff;
    if (colours) {
        static const uint8_t from[12] = {0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14};
        static const uint8_t to[12] = {4, 5, 6, 0x24, 0x25, 0x26, 0x44, 0x45, 0x46, 0x64, 0x65, 0x66};
        for (int i = 0; i < 12; i++) {
            uint8_t c = colours[from[i]];
            U8(b, to[i]) = c;
            U8(a, to[i]) = c;
        }
    } else {
        static const uint8_t order[3] = {4, 5, 6};
        for (int i = 0; i < 3; i++) {
            uint32_t o = order[i];
            U8(b, o + 0x60) = 0xff;
            U8(b, o + 0x40) = 0xff;
            U8(b, o + 0x20) = 0xff;
            U8(b, o) = 0xff;
            U8(a, o + 0x60) = 0xff;
            U8(a, o + 0x40) = 0xff;
            U8(a, o + 0x20) = 0xff;
            U8(a, o) = 0xff;
        }
    }
    GCALL(0x4270e0, prim_flag2_set)((uint8_t *)GMEM(a), (int32_t)U32(font, 0x18));
    GCALL(0x4270e0, prim_flag2_set)((uint8_t *)GMEM(b), (int32_t)U32(font, 0x18));
    if (!own)
        G16(0x4be2fc)++;
    return 0;
}

/* 0x430120: the language (word 0x4be2f4). */
uint32_t GAMEAPI language_get(void)
{
    return G32(0x4be2f4) & 0xffff;
}

/* The front-end texture record of an image (*0x4be340 + (*0x4be344 +0x2e
 * + image) * 16). */
static uint32_t pe_texture(int32_t image)
{
    return G32(0x4be340) + (uint32_t)((U16(G32(0x4be344), 0x2e) + image) << 4);
}

/* Shared tail of the quads: corners (x, y) (x + w, y) (x, y + h) (x + w,
 * y + h) in screen floats at +0x10 / +0x14 of each vertex (stride bytes
 * apart), 0x40 at +8, z at +0xc, code byte at +7 (type 0xc in byte 3). */
static void pe_quad_corners(uint32_t p, uint32_t stride, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t z,
                            uint8_t code)
{
    FX(p, 0x10, x);
    FY(p, 0x14, y);
    FX(p, 0x10 + stride, x + w);
    FY(p, 0x14 + stride, y);
    FX(p, 0x10 + 2 * stride, x);
    FY(p, 0x14 + 2 * stride, h + y);
    FX(p, 0x10 + 3 * stride, x + w);
    FY(p, 0x14 + 3 * stride, h + y);
    for (int k = 0; k < 4; k++)
        U32(p, 8 + k * stride) = 0x40;
    for (int k = 0; k < 4; k++)
        U32(p, 0xc + k * stride) = z;
    U32(p, 0) = (U32(p, 0) & 0xffffff) | 0xc000000;
    U8(p, 7) = code;
}

/* 0x430ea0: a front-end image as a gouraud textured quad (code 0x8c, vertex
 * stride 0x20): texture words from the record, size from it when w / h are
 * -1, four colours (stride 4) when given, corners from the 640 x 480 layout. */
void GAMEAPI fe_sprite_gouraud_set(uint8_t *p, int32_t image, const uint8_t *colours, int32_t x, int32_t y,
                                   int32_t w, int32_t h, uint32_t z)
{
    uint32_t t = pe_texture(image);
    U32(p, 0x20) = U32(t, 0);
    U32(p, 0x40) = U32(t, 4);
    U16(p, 0x60) = U16(t, 8);
    U16(p, 0x80) = U16(t, 0xa);
    if (w == -1)
        w = U8(t, 4) - U8(t, 0) + 1;
    if (h == -1)
        h = U8(t, 5) - U8(t, 1) + 1;
    if (colours) {
        for (int k = 0; k < 4; k++) {
            p[4 + 0x20 * k] = colours[4 * k];
            p[5 + 0x20 * k] = colours[4 * k + 1];
            p[6 + 0x20 * k] = colours[4 * k + 2];
        }
    }
    pe_quad_corners(A(p), 0x20, x, y, w, h, z, 0x8c);
}

/* 0x4310f0: a front-end image as a flat textured quad (code 0x98, vertex
 * stride 0x1c), one colour when given, 640 x 480 layout. */
void GAMEAPI fe_sprite_set(uint8_t *p, int32_t image, const uint8_t *colour, int32_t x, int32_t y, int32_t w,
                           int32_t h, uint32_t z)
{
    uint32_t t = pe_texture(image);
    U32(p, 0x20) = U32(t, 0);
    U32(p, 0x3c) = U32(t, 4);
    U16(p, 0x58) = U16(t, 8);
    U16(p, 0x74) = U16(t, 0xa);
    if (w == -1)
        w = U8(t, 4) - U8(t, 0) + 1;
    if (h == -1)
        h = U8(t, 5) - U8(t, 1) + 1;
    if (colour) {
        p[4] = colour[0];
        p[5] = colour[1];
        p[6] = colour[2];
    }
    pe_quad_corners(A(p), 0x1c, x, y, w, h, z, 0x98);
}

/* 0x431300: as 0x4310f0 with the corners scaled from 512 x 256. */
void GAMEAPI fe_sprite_set_512(uint8_t *p, int32_t image, const uint8_t *colour, int32_t x, int32_t y, int32_t w,
                               int32_t h, uint32_t z)
{
    uint32_t t = pe_texture(image);
    U32(p, 0x20) = U32(t, 0);
    U32(p, 0x3c) = U32(t, 4);
    U16(p, 0x58) = U16(t, 8);
    U16(p, 0x74) = U16(t, 0xa);
    if (w == -1)
        w = U8(t, 4) - U8(t, 0) + 1;
    if (h == -1)
        h = U8(t, 5) - U8(t, 1) + 1;
    if (colour) {
        p[4] = colour[0];
        p[5] = colour[1];
        p[6] = colour[2];
    }
#define SX5(v) ((int32_t)((uint32_t)(v) * (uint32_t)g_screen_w) / 512)
#define SY2(v) ((int32_t)((uint32_t)(v) * (uint32_t)g_screen_h) / 256)
    pe_fild_fstp(SX5(x), A(p) + 0x10);
    pe_fild_fstp(SY2(y), A(p) + 0x14);
    pe_fild_fstp(SX5(w + x), A(p) + 0x2c);
    pe_fild_fstp(SY2(y), A(p) + 0x30);
    pe_fild_fstp(SX5(x), A(p) + 0x48);
    pe_fild_fstp(SY2(y + h), A(p) + 0x4c);
    pe_fild_fstp(SX5(w + x), A(p) + 0x64);
    pe_fild_fstp(SY2(y + h), A(p) + 0x68);
#undef SX5
#undef SY2
    for (int k = 0; k < 4; k++)
        U32(p, 8 + k * 0x1c) = 0x40;
    for (int k = 0; k < 4; k++)
        U32(p, 0xc + k * 0x1c) = z;
    U32(p, 0) = (U32(p, 0) & 0xffffff) | 0xc000000;
    U8(p, 7) = 0x98;
}

/* 0x4314d0: an untextured flat quad (code 0xa8, vertex stride 0x18), one
 * colour when given, 640 x 480 layout. */
void GAMEAPI fe_quad_flat_set(uint8_t *p, const uint8_t *colour, int32_t x, int32_t y, int32_t w, int32_t h,
                              uint32_t z)
{
    if (colour) {
        p[4] = colour[0];
        p[5] = colour[1];
        p[6] = colour[2];
    }
    pe_quad_corners(A(p), 0x18, x, y, w, h, z, 0xa8);
}

/* 0x4316d0: the text box frame's 9 sprites (0x78 bytes each, images from the
 * bytes at 0x4aca7c, z 2000): texture corners pulled in by one texel, each
 * linked after the one before (0x4290a0); the first sprite's texture words
 * +0x22 / +0x3e = 0x20 / 0x3f and flag 2 in byte 7. */
void GAMEAPI fe_frame_sprites_init(uint8_t *p)
{
    for (int32_t i = 0; i < 9; i++) {
        uint8_t *r = p + i * 0x78;
        fe_sprite_set(r, G8(0x4aca7c + i), 0, 0, 0, -1, -1, FLOAT_2000);
        r[0x20]++;
        r[0x21]++;
        r[0x3c]--;
        r[0x3d]++;
        r[0x58]++;
        r[0x59]--;
        r[0x74]--;
        r[0x75]--;
        if (i)
            GCALL(0x4290a0, dword_store)((uint32_t *)(r - 0x78), A(r));
    }
    uint8_t f = p[7];
    U16(p, 0x3e) = 0x3f;
    p[7] = f | 2;
    U16(p, 0x22) = 0x20;
}

/* One frame piece: corners (x0, y0) (x1, y0) (x0, y1) (x1, y1), then 0x40 /
 * 2000 at +8 / +0xc of each vertex (stride 0x1c), in the original's order. */
static void pe_piece(uint32_t q, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    FX(q, 0x10, x0);
    FY(q, 0x14, y0);
    FX(q, 0x2c, x1);
    FY(q, 0x30, y0);
    FX(q, 0x48, x0);
    FY(q, 0x4c, y1);
    FX(q, 0x64, x1);
    FY(q, 0x68, y1);
    for (int k = 0; k < 4; k++) {
        U32(q, 0xc + k * 0x1c) = FLOAT_2000;
        U32(q, 8 + k * 0x1c) = 0x40;
    }
}

/* 0x431780: places the text box frame round (x, y, w, h): corner images 7..10
 * centred on the corners, edges 3 / 4 between them (one pixel overlap), the
 * background quad (z 3000, colour 5 5 0x30), the edge colour when given, and
 * links the 9 sprites into the list (0x429080). */
void GAMEAPI fe_box_frame_place(uint32_t list, uint8_t *p, const uint8_t *colour, int32_t x, int32_t y, int32_t w,
                                int32_t h)
{
    uint32_t q = A(p);
    int32_t r = w + x, b = y + h;
    int32_t cw = GCALL(0x431670, frontend_texture_width)(7), ch = GCALL(0x4316a0, frontend_texture_height)(7);
    int32_t tl_x0 = x - cw / 2, tl_x1 = cw - cw / 2 + x;
    int32_t tl_y0 = y - ch / 2, tl_y1 = ch - ch / 2 + y;
    pe_piece(q + 0x3c0, tl_x0, tl_y0, tl_x1, tl_y1);
    cw = GCALL(0x431670, frontend_texture_width)(8);
    ch = GCALL(0x4316a0, frontend_texture_height)(8);
    int32_t tr_x0 = r - cw / 2, tr_x1 = cw - cw / 2 + r;
    int32_t tr_y0 = y - ch / 2, tr_y1 = ch - ch / 2 + y;
    pe_piece(q + 0xf0, tr_x0, tr_y0, tr_x1, tr_y1);
    cw = GCALL(0x431670, frontend_texture_width)(9);
    ch = GCALL(0x4316a0, frontend_texture_height)(9);
    int32_t bl_x0 = x - cw / 2, bl_x1 = cw - cw / 2 + x;
    int32_t bl_y0 = b - ch / 2, bl_y1 = ch - ch / 2 + b;
    pe_piece(q + 0x2d0, bl_x0, bl_y0, bl_x1, bl_y1);
    cw = GCALL(0x431670, frontend_texture_width)(10);
    ch = GCALL(0x4316a0, frontend_texture_height)(10);
    int32_t br_x0 = r - cw / 2, br_x1 = cw - cw / 2 + r;
    int32_t br_y0 = b - ch / 2, br_y1 = ch - ch / 2 + b;
    pe_piece(q + 0x1e0, br_x0, br_y0, br_x1, br_y1);
    /* top and bottom edges (image 3's height) */
    int32_t eh = GCALL(0x4316a0, frontend_texture_height)(3);
    int32_t half = eh / 2;
    pe_piece(q + 0x78, tl_x1 - 1, y - half, tr_x0 + 1, eh - half + y);
    pe_piece(q + 0x258, bl_x1 - 1, b - half + 1, br_x0 + 1, eh - half + 1 + b);
    /* left and right edges (image 4's width) */
    int32_t ew = GCALL(0x431670, frontend_texture_width)(4);
    half = ew / 2;
    pe_piece(q + 0x168, r - half - 1, tl_y1 - 1, ew - half - 1 + r, bl_y0 + 1);
    pe_piece(q + 0x348, x - half, tr_y1 - 1, ew - half + x, br_y0 + 1);
    /* the background */
    FX(q, 0x10, x);
    FY(q, 0x14, y);
    FX(q, 0x2c, r);
    FY(q, 0x30, y);
    FX(q, 0x48, x);
    FY(q, 0x4c, b);
    FX(q, 0x64, r);
    FY(q, 0x68, b);
    for (int k = 0; k < 4; k++) {
        U32(q, 0xc + k * 0x1c) = FLOAT_3000;
        U32(q, 8 + k * 0x1c) = 0x40;
    }
    p[4] = 5;
    p[5] = 5;
    p[6] = 0x30;
    if (colour) {
        for (int i = 0; i < 8; i++) {
            p[0x7c + i * 0x78] = colour[0];
            p[0x7d + i * 0x78] = colour[1];
            p[0x7e + i * 0x78] = colour[2];
        }
    }
    GCALL(0x429080, word_exchange)(list, q, (uint32_t *)GMEM(q + 0x3c0));
}

/* 0x432820: out rgb = in rgb * (256 - fade) / 256. */
void GAMEAPI colour_fade_apply(uint8_t *out, const uint8_t *in)
{
    out[0] = (uint8_t)((int32_t)((uint32_t)(0x100 - g_fade) * in[0]) / 256);
    out[1] = (uint8_t)((int32_t)((uint32_t)in[1] * (uint32_t)(0x100 - g_fade)) / 256);
    out[2] = (uint8_t)((int32_t)((uint32_t)in[2] * (uint32_t)(0x100 - g_fade)) / 256);
}

/* 0x4328a0: v * fade / 256. */
int32_t GAMEAPI fade_scale(int32_t v)
{
    return (int32_t)((uint32_t)g_fade * (uint32_t)v) / 256;
}

/* 0x4328c0: the pulsing highlight: 1 or 4 colours (count 4, else 1; stride
 * 4) scaled by a triangle wave 64..127 / 127 of the frame count 0x4be320
 * (speed 2, 1 with flag 4, 4 with flag 2); source 0x4be350 when none. */
void GAMEAPI colour_pulse_scale(uint8_t *out, const uint8_t *in, int32_t count, uint32_t flags)
{
    int32_t speed = 2;
    if (flags & 4)
        speed = 1;
    if (flags & 2)
        speed = 4;
    if (!out)
        return;
    if (!in)
        in = (const uint8_t *)(uintptr_t)0x4be350;
    int32_t k = (int32_t)(G32(0x4be320) * (uint32_t)speed) & 0x7f;
    if (k < 0x40)
        k = 0x7f - k;
    if (count != 4)
        count = 1;
    for (int32_t n = count; n > 0; n--) {
        out[0] = (uint8_t)((int32_t)(in[0] * (uint32_t)k) / 127);
        out[1] = (uint8_t)((int32_t)(in[1] * (uint32_t)k) / 127);
        out[2] = (uint8_t)((int32_t)(in[2] * (uint32_t)k) / 127);
        out += 4;
        in += 4;
    }
}

/* 0x432cd0: a front-end mode's race: the random seeds (0x4a7148 /
 * 0x4b11d8); mode 1 only marks options +0x17; otherwise options +0x17 = -1,
 * +0x21 = 0, +0x8c..+0x91 = 1, then unless mode 8 the race option clamp, the
 * choice bytes +0x14..+0x16, the track (0x433670; options +0xb 1..20 picks
 * one) and its world / place / start time into the race. Mode 7 (the
 * attract demo) also sets the kind, weapons and option, the AI level from
 * options +0x87, six cars (0x433ab0) and polls the network session
 * (0x435d43, 0 returned when it fails). 1 (and 0x4be450 = 1), -1 when no
 * track. */
int32_t GAMEAPI front_mode_race_setup(int32_t mode, uint32_t kind, uint32_t choice)
{
    int32_t track;
    G32(0x4a7148) = 0x87654321;
    G32(0x4b11d8) = 0x12345678;
    if (mode == 1) {
        U8(g_options, 0x17) = (uint8_t)mode;
        goto done;
    }
    U8(g_options, 0x17) = 0xff;
    U8(g_options, 0x21) = 0;
    for (int32_t i = 1; i <= 6; i++)
        U8(g_options, i + 0x8b) = 1;
    if (mode == 8) {
        track = mode;
    } else {
        GCALL(0x433cd0, race_option_clamp)();
        U8(g_options, 0x43) = U8(g_options, 0x82);
        U8(g_options, 0x14) = (uint8_t)mode;
        U8(g_options, 0x15) = (uint8_t)kind;
        U8(g_options, 0x16) = (uint8_t)choice;
        uint32_t o = g_options;
        track = GCALL(0x433670, race_track_choose)(U8(o, 0x15), U8(o, 0x16));
        if (track == -1)
            return -1;
        uint8_t t = U8(g_options, 0xb);
        if (t >= 1 && t <= 20)
            track = t - 1;
        U8(g_race, 0x579) = (uint8_t)track;
        uint8_t world = (uint8_t)GCALL(0x433550, track_world)(track);
        U8(g_race, 0xe) = world;
        uint8_t place = (uint8_t)GCALL(0x4335c0, track_world_place)(track);
        U8(g_race, 0xf) = place;
        uint32_t now = GCALL(0x436935, fe_time)();
        U32(g_race, 0x618) = now;
    }
    if (mode == 7) {
        uint32_t o = g_options;
        if (U8(o, 0x43) && U8(o, 0x83))
            U8(o, 0x43) = 2;
        GCALL(0x432b70, start_kind_set)();
        GCALL(0x432bd0, weapons_flags_set)(track, 1);
        GCALL(0x432c80, race_option554_set)();
        uint8_t level;
        switch (U8(g_options, 0x87)) {
        case 1: level = 6; break;
        case 2: level = 7; break;
        case 3: level = 8; break;
        default: level = 5; break;
        }
        U8(g_race, 0x551) = level;
        uint8_t v = U8(g_options, 0x8b);
        U8(g_race, 0x585) = v;
        U8(g_race, 0x54d) = 6;
        GCALL(0x433ab0, race_slots_init)(0, 1, 0, 0, 0);
        if (GCALL(0x435d43, network_session_poll)(1) == 0)
            return 0;
    }
done:
    G32(0x4be450) = 1;
    return 1;
}
