/* Group PA: the original's 0x41e040-0x420bb0 still-reachable functions (the
 * rest of the Direct3D set-up file and the start of the Direct3D drawing
 * file):
 *
 *  - viewport clear / set (0x41e040, 0x41f210), begin / end scene, the two
 *    materials (background 0x41ed00 / 0x41ec40, release 0x41e0d0);
 *  - texture pixel conversion to the screen's 16-bit format (24-bit
 *    0x41e1f0 / 0x41e1d0, 1555 0x41e440), the free slot search 0x41ec20,
 *    the texture format choice 0x41f060, the PowerVR / nVidia probes
 *    0x41fb60 / 0x41fcf0, the blend mode table 0x41fda0;
 *  - texture lock / unlock / palettes (0x41fef0, 0x420000, 0x420090,
 *    0x420150), a large image cut into 256x256 texture pages and drawn as
 *    sprites (0x4201f0 / 0x4203a0 / 0x41efe0), surface clears and the
 *    pixel read (0x41eeb0, 0x41f400);
 *  - the TL vertex buffer *0x4bcd80 (0x20-byte D3DTLVERTEX, slot offsets
 *    0x4a807c, count 0x4bddd8): the vertex emitters 0x420a90 / 0x420b10
 *    (register arguments), the coordinate snap 0x420600, the four
 *    DrawPrimitive flushes, the render state selection 0x420a30 /
 *    0x420770 / 0x4209c0 and the flat quad 0x41f550.
 *
 * COM calls are the typed methods of com.h, SetRect the typed import of
 * winapi.h, everything outside the group by address. Float code is the
 * original's x87 instruction sequence in inline asm, so results match under
 * either precision control; _ftol (0x497a80) is called by address where the
 * value can be fractional. Stack buffers the original leaves uninitialised
 * (surface descriptions, the sprite packet, out-pointer slots) are zeroed
 * here. */
#include "../runtime.h"
#include "com.h"
#include "physics.h"
#include "gamefn.h"

/* This group's prototypes (also in build/groups/pa_protos.txt for physics.h). */
int32_t GAMEAPI viewport_clear(uint32_t keep_target); /* 0x41e040 */
int32_t GAMEAPI d3d_materials_release(void); /* 0x41e0d0 */
void GAMEAPI material_init(uint32_t *m, uint32_t r, uint32_t g, uint32_t b); /* 0x41e110 */
int32_t GAMEAPI d3d_scene_begin(void); /* 0x41e170 */
void GAMEAPI d3d_scene_end(void); /* 0x41e190 */
void GAMEAPI pixels_rgb_to_screen(const uint8_t *src, int32_t n, uint32_t alpha); /* 0x41e1d0 */
void GAMEAPI pixels_rgb_convert(const uint8_t *src, int32_t n, uint16_t *dst, uint32_t alpha); /* 0x41e1f0 */
void GAMEAPI pixels_1555_convert(const uint16_t *src, int32_t w, int32_t h, uint16_t *dst, int32_t pitch, uint32_t alpha); /* 0x41e440 */
int32_t GAMEAPI texture_slot_free_find(void); /* 0x41ec20 */
void GAMEAPI background_colour_set(int32_t r, int32_t g, int32_t b); /* 0x41ec40 */
int32_t GAMEAPI materials_create(void); /* 0x41ed00 */
int32_t GAMEAPI surfaces_clear(void); /* 0x41eeb0 */
int32_t GAMEAPI texture_page_show(int32_t page); /* 0x41efe0 */
int32_t GAMEAPI texture_format_choose(uint32_t rgb_only, uint32_t alpha_wanted); /* 0x41f060 */
void GAMEAPI d3d_viewport_set(uint32_t x, uint32_t y, uint32_t w, uint32_t h); /* 0x41f210 */
void GAMEAPI vertex_snap_set(uint32_t on); /* 0x41f3f0 */
uint32_t GAMEAPI surface_pixel_read(uint32_t which, uint32_t x, uint32_t y, uint32_t mask); /* 0x41f400 */
int32_t GAMEAPI screen_quad_draw(uint32_t state, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t u0, uint32_t v0, uint32_t u1, uint32_t v1, uint32_t z, uint32_t colour, uint32_t in_scene, uint32_t blend); /* 0x41f550 */
uint32_t GAMEAPI bytes_sum8(const uint8_t *p, int32_t n); /* 0x41f700 */
int32_t GAMEAPI pvrc_texture_probe(void); /* 0x41fb60 */
int32_t GAMEAPI ddraw_device_id_check(void); /* 0x41fcf0 */
void GAMEAPI blend_modes_choose(void); /* 0x41fda0 */
int32_t GAMEAPI texture_lock(int32_t page, uint32_t flags, uint32_t *bits, uint32_t *palette, uint32_t *bit_count, uint32_t *pitch); /* 0x41fef0 */
int32_t GAMEAPI texture_unlock(int32_t page, uint32_t flags, const uint32_t *palette); /* 0x420000 */
void GAMEAPI texture_palettes_apply(void); /* 0x420090 */
int32_t GAMEAPI texture_reload(uint32_t slot); /* 0x420150 */
int32_t GAMEAPI image_tiles_upload(uint32_t src, const uint8_t *palette, int32_t w, int32_t h, int32_t pitch); /* 0x4201f0 */
void GAMEAPI image_tiles_draw(int32_t first, int32_t w, int32_t h, const int32_t *rect, int32_t blend, uint32_t colour); /* 0x4203a0 */
int32_t GAMEAPI texture_slots_release_thunk(void); /* 0x4205e0 */
int32_t GAMEAPI texture_page_next(void); /* 0x4205f0 */
void GAMEAPI vertices_snap(void); /* 0x420600 */
void GAMEAPI vertices_snap_thunk(void); /* 0x420650 */
void GAMEAPI vertices_draw_list_noclip(void); /* 0x420660 */
void GAMEAPI vertices_draw_list(void); /* 0x4206a0 */
void GAMEAPI vertices_draw_fan(void); /* 0x4206e0 */
void GAMEAPI vertices_draw_quads(void); /* 0x420720 */
void GAMEAPI render_blend_set(uint32_t flags, uint32_t mode); /* 0x420770 */
void GAMEAPI render_texture_set(uint32_t page); /* 0x4209c0 */
void render_state_select(void); /* 0x420a30: asm entry, register argument ebx (state) */
void GAMEAPI render_state_select_c(uint32_t state); /* 0x420a30 body */
void vertex_emit_indexed(void); /* 0x420a90: asm entry, register arguments eax (colour), ebx (texture indices), edx (vertex) */
void GAMEAPI vertex_emit_indexed_c(uint32_t colour, uint32_t uv, const uint8_t *v); /* 0x420a90 body */
void vertex_emit(void); /* 0x420b10: asm entry, register argument edx (vertex) */
void GAMEAPI vertex_emit_c(const uint8_t *v); /* 0x420b10 body */
void GAMEAPI vertex_list_draw_fan(const int32_t *list); /* 0x420bb0 */

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define P8(a) ((uint8_t *)(uintptr_t)(a))
#define A32(p) ((uint32_t)(uintptr_t)(p))

#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* COM methods com.h lacks (native/ does not implement them). */
/* Direct3D objects and the last HRESULT. */
#define DDRAW G32(0x4d5f40)
#define D3D G32(0x4b8380)
#define DEVICE G32(0x4b8384)
#define VIEWPORT G32(0x4b8388)
#define g_hresult G32(0x4d6f18)
/* COM out-parameters are pointer-sized (8 bytes on x86-64): through a
 * void * (holding the 32-bit variable's value), stored back as 32 bits. */
#define COM_OUT32(lv, call)                                                                                    \
    do {                                                                                                       \
        uint32_t out_ = (lv);                                                                                  \
        call;                                                                                                  \
        (lv) = out_;                                                                                           \
    } while (0)
#define RENDER_STATE(state, value) IDirect3DDevice2_SetRenderState(DEVICE, (state), (value))

/* Outside the group. */
#define mem_fill(p, v, n) GCALL(0x476f50, mem_fill)(GMEM(p), v, n)
#define mem_copy(d, s, n) GCALL(0x476f30, mem_copy)(GMEM(d), GMEM(s), n)
#define block_alloc(n, f) ((uint32_t)GCALL(0x476ae0, global_block_alloc)(n, f))
#define block_free(p) GCALL(0x476b40, global_block_free)(p)
#define surface_desc_get(desc, surf) GCALL(0x487660, dd_surface_desc_get)(desc, surf)
#define surface_lock(a, surf, desc) GCALL(0x4875a0, dd_surface_lock)(a, surf, desc)
#define surface_unlock(surf) GCALL(0x487610, dd_surface_unlock)(surf)
#define texture_slot_release_x(i) GCALL(0x41d890, texture_slot_release)(i)
#define texture_slot_handle_x(i) GCALL(0x41d940, texture_slot_handle)(i)
#define texture_slots_release_x() GCALL(0x41d910, texture_slots_release)()
#define texture_page_create(i, w, h, pix, pal, f) GCALL(0x41e710, texture_upload_body)(i, w, h, pix, pal, f)
#define screen_area_set(x, y, w, h) GCALL(0x41f2d0, viewport_body)(x, y, w, h)
/* the call at 0x420582: to the Vulkan renderer (it falls back to 0x420be0) */
#ifdef REROLL_NATIVE
void GAMEAPI vkgame_image_tile(uint32_t packet);
#define sprite_packet_draw(p) vkgame_image_tile((uint32_t)(p))
#else
#define sprite_packet_draw(p) GCALL(0x420be0, d3d_packet_draw)(p)
#endif
#define vertices_quad_close() GCALL(0x421800, d3d_vertex_repeat)()

/* Texture slots: 64 records of 0x24 bytes at 0x4ba6fc (+0 handle, +4 / +8
 * surfaces, +0xc texture, +0x10 / +0x14 size, +0x18 page, +0x1c / +0x20 u / v
 * coordinate tables); page -> slot 0x6cd300. */
#define SLOT(field, s) G32(0x4ba6fc + (field) + (s) * 0x24)
#define PAGE_SLOT(page) G32(0x6cd300 + (page) * 4)

/* Screen pixel format: per channel a right shift (bits lost) and a left
 * shift (position): red 0x4bcd58 / 59, green 0x4bcd60 / 61, blue 0x4bcd68 /
 * 69, alpha 0x4bcd70 / 71 (alpha 0: no alpha). */
#define SH(a) (G8(a) & 31u)

/* ---- x87 helpers ---- */

/* fld dword [p]; call _ftol: truncated to an integer (low dword). */
static inline int32_t pa_ftol(uint32_t address)
{
    int32_t r;

#if defined(__i386__)
    __asm__ volatile("flds (%1)\n\t"
                     "call *%2"
                     : "=a"(r)
                     : "r"(address), "S"(0x497a80)
                     : "ecx", "edx", "memory", "cc", X87);
#else
    /* x86-64: _ftol's steps (rounding set to chop around a fistp qword; the
     * low dword). */
    uint16_t cw, chop;
    int64_t q;

    __asm__ volatile("fnstcw %0" : "=m"(cw));
    chop = (uint16_t)(cw | 0xc00);
    __asm__ volatile("flds %2\n\tfldcw %3\n\tfistpll %0\n\tfldcw %1"
                     : "=m"(q)
                     : "m"(cw), "m"(*(const float *)P8(address)), "m"(chop)
                     : "st", "memory");
    r = (int32_t)q;
#endif
    return r;
}

/* fild dword; fstp dword. */
static inline uint32_t pa_from_int(int32_t v)
{
    uint32_t r;

    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild dword v; fstp dword t; fld t; fdiv dword [k]; fstp: v / k. */
static inline uint32_t pa_int_div(int32_t v, uint32_t k_address)
{
    uint32_t t, r;

    __asm__ volatile("fildl %2\n\tfstps %1\n\tflds %1\n\tfdivs (%3)\n\tfstps %0"
                     : "=m"(r), "=m"(t)
                     : "m"(v), "r"(P8(k_address))
                     : "st", "memory");
    return r;
}

/* fld dword; fistp dword: rounded by the current rounding mode. */
static inline uint32_t pa_fist(const uint8_t *p)
{
    uint32_t r;

    __asm__ volatile("flds (%1)\n\tfistpl %0" : "=m"(r) : "r"(p) : "st", "memory");
    return r;
}

/* ===================================================================== */

/* ===== 0x41e040: clear the viewport rectangle (floats 0x6ccd64 x0,
 * 0x6ccd6c y0, 0x6ccd68 x1, 0x6ccd70 y1, through _ftol): the target unless
 * `keep_target`, the z-buffer when there is one (0x4d5f98). 1 when nothing
 * to clear or the Clear succeeded. ===== */
int32_t GAMEAPI viewport_clear(uint32_t keep_target)
{
    uint32_t flags = keep_target == 0;
    int32_t rect[4];

    if (G32(0x4d5f98) != 0)
        flags |= 2;
    if (flags == 0)
        return 1;
    rect[0] = pa_ftol(0x6ccd64);
    rect[1] = pa_ftol(0x6ccd6c);
    rect[2] = pa_ftol(0x6ccd68);
    rect[3] = pa_ftol(0x6ccd70);
    g_hresult = (uint32_t)IDirect3DViewport2_Clear(VIEWPORT, 1, A32(rect), flags);
    return g_hresult == 0;
}

/* ===== 0x41e0d0: release the two materials, then all texture slots
 * (tail call of 0x41d910). ===== */
int32_t GAMEAPI d3d_materials_release(void)
{
    if (G32(0x4bddd0) != 0) {
        IDirect3DMaterial2_Release(G32(0x4bddd0));
        G32(0x4bddd0) = 0;
    }
    if (G32(0x4bddcc) != 0) {
        IDirect3DMaterial2_Release(G32(0x4bddcc));
        G32(0x4bddcc) = 0;
    }
    return texture_slots_release_x();
}

/* ===== 0x41e110: a D3DMATERIAL (0x50 bytes): zeroed, size 0x50, diffuse /
 * ambient / specular r g b (float bits) with alpha 1.0, emissive alpha 1.0
 * (sic: the emissive colour stays 0 but +0x40 is set). ===== */
void GAMEAPI material_init(uint32_t *m, uint32_t r, uint32_t g, uint32_t b)
{
    int i;

    for (i = 0; i < 0x14; i++)
        m[i] = 0;
    m[0] = 0x50;
    m[9] = m[1] = m[5] = r;
    m[10] = m[2] = m[6] = g;
    m[11] = m[3] = m[7] = b;
    m[12] = m[4] = m[8] = 0x3f800000;
    m[13] = m[14] = m[15] = m[16] = 0x3f800000;
}

/* ===== 0x41e170: BeginScene; 1 on success. ===== */
int32_t GAMEAPI d3d_scene_begin(void)
{
    g_hresult = (uint32_t)IDirect3DDevice2_BeginScene(DEVICE);
    return g_hresult == 0;
}

/* ===== 0x41e190: EndScene when there is a device. ===== */
void GAMEAPI d3d_scene_end(void)
{
    if (DEVICE != 0)
        IDirect3DDevice2_EndScene(DEVICE);
}

/* The 16-bit colour of 8-bit r, g, b in the screen format (shifts at
 * 0x4bcd58..0x4bcd69), low 16 bits significant; a colour that comes out 0
 * but is not black gets the lowest blue bit. */
static uint32_t pa_pack(uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t v = ((g >> SH(0x4bcd60)) << SH(0x4bcd61)) | ((r >> SH(0x4bcd58)) << SH(0x4bcd59)) |
                 ((b >> SH(0x4bcd68)) << SH(0x4bcd69));

    if ((v & 0xffff) == 0 && (b | r | g) != 0)
        v = 1u << SH(0x4bcd69);
    return v;
}

/* The alpha of a pixel: any colour -> 0xff when 0x4b86bc bit 0, else the
 * brightest channel scaled by mult (0x100 or 0x200), at most 0xff. */
static uint32_t pa_alpha(uint32_t r, uint32_t g, uint32_t b, uint32_t mult)
{
    uint32_t a;

    if (G8(0x4b86bc) & 1)
        return (r | g | b) != 0 ? 0xff : 0;
    a = r > g ? r : g;
    if (a <= b)
        a = b;
    a *= mult;
    if ((a & ~0xffu) >= 0xff00)
        return 0xff;
    return a >> 8;
}

/* ===== 0x41e1f0: n 32-bit pixels (bytes r, g, b, x) to the screen's
 * 16-bit format. With `alpha` and an alpha channel the alpha comes from the
 * colour (pa_alpha; mult 0x200 when the alpha shift 0x4bcd70 is 7, else
 * 0x100); otherwise, with an alpha channel, every pixel is opaque except a
 * black first one (sic). ===== */
void GAMEAPI pixels_rgb_convert(const uint8_t *src, int32_t n, uint16_t *dst, uint32_t alpha)
{
    uint8_t abits = G8(0x4bcd70);
    int32_t i;

    if (alpha != 0 && abits != 0) {
        uint32_t mult = abits != 7 ? 0x100 : 0x200;
        uint32_t left = (uint32_t)n;

        if (n <= 0)
            return;
        do {
            uint32_t r = src[0], g = src[1], b = src[2];
            uint32_t a = pa_alpha(r, g, b, mult);
            uint32_t v = pa_pack(r, g, b);

            src += 4;
            *dst++ = (uint16_t)(((a >> SH(0x4bcd70)) << SH(0x4bcd71)) | v);
        } while (--left != 0);
        return;
    }
    for (i = 0; i < n; i++) {
        uint32_t v = pa_pack(src[0], src[1], src[2]);

        if (abits != 0) {
            uint32_t a = (i == 0 && (v & 0xffff) == 0) ? 0 : 0xff;

            v |= (a >> SH(0x4bcd70)) << SH(0x4bcd71);
        }
        src += 4;
        *dst++ = (uint16_t)v;
    }
}

/* ===== 0x41e1d0: pixels_rgb_convert into the conversion buffer
 * *0x4bcd7c. ===== */
void GAMEAPI pixels_rgb_to_screen(const uint8_t *src, int32_t n, uint32_t alpha)
{
    pixels_rgb_convert(src, n, (uint16_t *)(uintptr_t)G32(0x4bcd7c), alpha);
}

/* ===== 0x41e440: a w x h block of 1555 pixels (rows of w, packed) to the
 * screen's 16-bit format (rows `pitch` bytes apart). With `alpha` and an
 * alpha channel, set pixels (bit 15) get pa_alpha of their colour (each
 * channel's 5 bits << 3), clear ones 0, and a colour that comes out 0 but
 * is not black gets the lowest blue bit; otherwise set pixels are opaque
 * and clear ones black and transparent. The plain path loops w times
 * without a check (sic: w <= 0 runs away). ===== */
void GAMEAPI pixels_1555_convert(const uint16_t *src, int32_t w, int32_t h, uint16_t *dst, int32_t pitch,
                                 uint32_t alpha)
{
    uint8_t abits = G8(0x4bcd70);
    int32_t rows;

    if (alpha != 0 && abits != 0) {
        uint32_t mult = abits != 7 ? 0x100 : 0x200;

        if (h <= 0)
            return;
        rows = h;
        do {
            const uint16_t *s = src;
            uint16_t *d = dst;

            if (w > 0) {
                uint32_t left = (uint32_t)w;

                do {
                    uint32_t px = *s++;
                    uint32_t r = ((px >> 10) & 0x1f) << 3, g = ((px >> 5) & 0x1f) << 3, b = (px & 0x1f) << 3;
                    uint32_t a = (px & 0x8000) ? pa_alpha(r, g, b, mult) : 0;
                    uint32_t v = pa_pack(r, g, b);

                    *d++ = (uint16_t)(((a >> SH(0x4bcd70)) << SH(0x4bcd71)) | v);
                } while (--left != 0);
            }
            src += w;
            dst = (uint16_t *)((uint8_t *)dst + pitch);
        } while (--rows != 0);
        return;
    }
    if (h <= 0)
        return;
    rows = h;
    do {
        const uint16_t *s = src;
        uint16_t *d = dst;
        uint32_t left = (uint32_t)w;

        do {
            uint32_t px = *s++;
            uint32_t r = 0, g = 0, b = 0, a = 0, v = 0;

            if (px & 0x8000) {
                r = ((px >> 10) & 0x1f) << 3;
                g = ((px >> 5) & 0x1f) << 3;
                b = (px & 0x1f) << 3;
                a = 0xff;
            }
            if (abits != 0)
                v = (a >> SH(0x4bcd70)) << SH(0x4bcd71);
            v |= (r >> SH(0x4bcd58)) << SH(0x4bcd59);
            v |= (g >> SH(0x4bcd60)) << SH(0x4bcd61);
            v |= (b >> SH(0x4bcd68)) << SH(0x4bcd69);
            *d++ = (uint16_t)v;
        } while (--left != 0);
        src += w;
        dst = (uint16_t *)((uint8_t *)dst + pitch);
    } while (--rows != 0);
}

/* ===== 0x41ec20: the first free texture slot (surface 0x4ba700 == 0), or
 * -1. ===== */
int32_t GAMEAPI texture_slot_free_find(void)
{
    int32_t i;

    for (i = 0; i < 64; i++)
        if (SLOT(4, i) == 0)
            return i;
    return -1;
}

/* ===== 0x41ec40: the background colour (integers r, g, b / 255.0 from
 * 0x4a332c) into the background material 0x4bddcc, its handle to the
 * viewport. The handle's slot starts as (float)r (the argument slot the
 * original reuses). ===== */
void GAMEAPI background_colour_set(int32_t r, int32_t g, int32_t b)
{
    uint32_t m[0x14];
    uint32_t handle;

    if (G32(0x4bddcc) == 0)
        return;
    {
        uint32_t fb = pa_int_div(b, 0x4a332c);
        uint32_t fg = pa_int_div(g, 0x4a332c);
        uint32_t fr = pa_int_div(r, 0x4a332c);

        material_init(m, fr, fg, fb);
    }
    handle = pa_from_int(r);
    m[0x11] = 0x40a00000;
    m[0x13] = 1;
    m[0x12] = 0;
    if (IDirect3DMaterial2_SetMaterial(G32(0x4bddcc), (const uint8_t *)m) != 0)
        return;
    if (IDirect3DMaterial2_GetHandle(G32(0x4bddcc), GMEM(DEVICE), &handle) != 0)
        return;
    IDirect3DViewport2_SetBackground(VIEWPORT, handle);
}

/* ===== 0x41ed00: create the background material 0x4bddcc (black, power
 * 5, ramp 1; its handle to the viewport) and the white material 0x4bddd0
 * (texture handle *0x4ba6fc, ramp 0x40; handle 0x4bddd4, set as the
 * material light state). 0 when a call fails. The original's handle slot
 * is uninitialised: 0 here. ===== */
int32_t GAMEAPI materials_create(void)
{
    uint32_t m1[0x14], m2[0x14];
    uint32_t handle = 0;

    /* empty debug hook (dx_error_ignore), a RECT by value: the original's
     * pushes, which it does not read */
    ((void(GAMEAPI *)(uint32_t, uint32_t, uint32_t, uint32_t))(uintptr_t)0x4291f0)(0, 0, G32(0x4d5f88),
                                                                                  G32(0x4d5f8c));
    material_init(m1, 0, 0, 0);
    m1[0x11] = 0x40a00000;
    m1[0x13] = 1;
    m1[0x12] = 0;
    if (IDirect3D2_CreateMaterial(D3D, GMEM(0x4bddcc), 0) != 0)
        return 0;
    if (IDirect3DMaterial2_SetMaterial(G32(0x4bddcc), (const uint8_t *)m1) != 0)
        return 0;
    if (IDirect3DMaterial2_GetHandle(G32(0x4bddcc), GMEM(DEVICE), &handle) != 0)
        return 0;
    if (IDirect3DViewport2_SetBackground(VIEWPORT, handle) != 0)
        return 0;
    material_init(m2, 0x3f800000, 0x3f800000, 0x3f800000);
    m2[0x12] = G32(0x4ba6fc);
    m2[0x11] = 0x3f800000;
    m2[0x13] = 0x40;
    if (IDirect3D2_CreateMaterial(D3D, GMEM(0x4bddd0), 0) != 0)
        return 0;
    if (IDirect3DMaterial2_SetMaterial(G32(0x4bddd0), (const uint8_t *)m2) != 0)
        return 0;
    if (IDirect3DMaterial2_GetHandle(G32(0x4bddd0), GMEM(DEVICE), GMEM(0x4bddd4)) != 0)
        return 0;
    IDirect3DDevice2_SetLightState(DEVICE, 1, G32(0x4bddd4));
    return 1;
}

/* ===== 0x41eeb0: colour-fill (Blt, DDBLT_COLORFILL | DDBLT_WAIT, colour 0)
 * the surfaces 0x4d5f48 and 0x4d5f4c, each whole (size from 0x487660). 0
 * when a description or Blt fails. ===== */
int32_t GAMEAPI surfaces_clear(void)
{
    static const uint32_t surfaces[2] = {0x4d5f48, 0x4d5f4c};
    uint8_t desc[0x6c];
    uint32_t fx[0x19];
    int32_t rect[4];
    int k, i;

    for (i = 0; i < 0x6c; i++)
        desc[i] = 0;
    for (k = 0; k < 2; k++) {
        uint32_t surf = G32(surfaces[k]);
        int32_t ok;

        if (surf == 0)
            continue;
        ok = surface_desc_get(A32(desc), surf);
        if (ok == 0)
            return ok;
        for (i = 0; i < 0x19; i++)
            fx[i] = 0;
        fx[0] = 100;
        SetRect(rect, 0, 0, (int32_t)U32(desc, 0x0c), (int32_t)U32(desc, 0x08));
        g_hresult = (uint32_t)IDirectDrawSurface_Blt(G32(surfaces[k]), rect, 0, 0, 0x1000400, (const uint8_t *)fx);
        if (g_hresult != 0)
            return 0;
    }
    return 1;
}

/* ===== 0x41efe0: show texture page `page` whole on the screen: screen area
 * (0, 0, *0x4acc38, *0x4acc3c), then the page as sprites. ===== */
int32_t GAMEAPI texture_page_show(int32_t page)
{
    int32_t rect[4];
    int32_t slot;

    screen_area_set(0, 0, G32(0x4acc38), G32(0x4acc3c));
    slot = (int32_t)PAGE_SLOT(page);
    SetRect(rect, 0, 0, (int32_t)SLOT(0x10, slot), (int32_t)SLOT(0x14, slot));
    image_tiles_draw(page, (int32_t)SLOT(0x10, slot), (int32_t)SLOT(0x14, slot), rect, 1, 0xffffffffu);
    return 1;
}

/* ===== 0x41f060: choose a texture format (records of 0x94 bytes at
 * 0x4bb000, count 0x4baffc: +0x54 bit count, +0x6c palettised, +0x70.. channel
 * masks and shifts). With `alpha_wanted` or 0x4b86bc bit 1: the RGB format
 * (<= 16 bits) with the widest alpha (narrowest when 0x4b86bc bit 0; then
 * format 1 is the default, sic). Else, unless `rgb_only`, the first 8-bit
 * palettised one; else the first <= 16-bit RGB with 6, 5 or 4 green bits
 * (sic: the 4-bit test reads the red mask), else 0. ===== */
int32_t GAMEAPI texture_format_choose(uint32_t rgb_only, uint32_t alpha_wanted)
{
    int32_t n = GI32(0x4baffc);
    int32_t i;

#define REC(off, i) (0x4bb000 + (off) + (uint32_t)(i) * 0x94)
    if (alpha_wanted != 0 || (G8(0x4b86bc) & 2)) {
        uint32_t lowest = G32(0x4b86bc) & 1;
        uint32_t best = lowest ? 0xff : 0;
        int32_t pick = (int32_t)lowest;

        for (i = 0; i < n; i++) {
            uint32_t width;

            if (G32(REC(0x6c, i)) != 0)
                continue;
            width = G32(REC(0x8c, i)) >> (G8(REC(0x89, i)) & 31);
            if (width == 0)
                continue;
            if (lowest ? width < best : width > best) {
                if (G32(REC(0x54, i)) <= 0x10) {
                    best = width;
                    pick = i;
                }
            }
        }
        if (best != 0)
            return pick;
    }
    if (rgb_only == 0)
        for (i = 0; i < n; i++)
            if (G32(REC(0x6c, i)) != 0 && G32(REC(0x90, i)) == 8)
                return i;
    for (i = 0; i < n; i++)
        if (G32(REC(0x6c, i)) == 0 && (G32(REC(0x7c, i)) >> (G8(REC(0x79, i)) & 31)) == 0x3f &&
            G32(REC(0x54, i)) <= 0x10)
            return i;
    for (i = 0; i < n; i++)
        if (G32(REC(0x6c, i)) == 0 && (G32(REC(0x74, i)) >> (G8(REC(0x71, i)) & 31)) == 0x1f &&
            G32(REC(0x54, i)) <= 0x10)
            return i;
    for (i = 0; i < n; i++)
        if (G32(REC(0x6c, i)) == 0 && (G32(REC(0x74, i)) >> (G8(REC(0x71, i)) & 31)) == 0xf &&
            G32(REC(0x54, i)) <= 0x10)
            return i;
    return 0;
#undef REC
}

/* ===== 0x41f210: set the viewport (D3DVIEWPORT, 0x2c bytes): x, y, w, h,
 * scale s = w / 2.0 (0x4a3314) both ways, max x w / 2s, max y h / 2s, z 0..1.
 * Width and height are converted as unsigned 64-bit integers. ===== */
void GAMEAPI d3d_viewport_set(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t vp[11];
    uint64_t w64 = w, h64 = h;
    uint32_t wf, hf;
    int i;

    for (i = 0; i < 11; i++)
        vp[i] = 0;
    vp[0] = 0x2c;
    vp[1] = x;
    vp[2] = y;
    vp[3] = w;
    vp[4] = h;
    vp[9] = 0;
    vp[10] = 0x3f800000;
    __asm__ volatile("fildll %2\n\tfstps %1\n\tflds %1\n\tfdivs 0x4a3314\n\tfstps %0"
                     : "=m"(vp[5]), "=m"(wf)
                     : "m"(w64)
                     : "st", "memory");
    vp[6] = vp[5];
    /* fld wf; fld s; fadd st, st; fdivp st(1), st */
    __asm__ volatile("flds %1\n\tflds %2\n\t.byte 0xdc, 0xc0\n\t.byte 0xde, 0xf9\n\tfstps %0"
                     : "=m"(vp[7])
                     : "m"(wf), "m"(vp[5])
                     : "st", "st(1)");
    __asm__ volatile("fildll %2\n\tfstps %1\n\tflds %1\n\tflds %3\n\t.byte 0xdc, 0xc0\n\t.byte 0xde, 0xf9\n\t"
                     "fstps %0"
                     : "=m"(vp[8]), "=m"(hf)
                     : "m"(h64), "m"(vp[6])
                     : "st", "st(1)", "memory");
    g_hresult = (uint32_t)IDirect3DViewport2_SetViewport(VIEWPORT, (const uint8_t *)vp);
}

/* ===== 0x41f3f0: the vertex snap switch 0x4b86b8 (0x420600). ===== */
void GAMEAPI vertex_snap_set(uint32_t on)
{
    G32(0x4b86b8) = on;
}

/* ===== 0x41f400: read pixel (x, y) of the back (which 0: 0x4d5f48) or
 * front surface (0x4d5f4c) by locking it; bytes per pixel from the bit
 * count (16 / 15: 2, 24: 3, 32: 4, else 1). With `mask` (and more than one
 * byte) each channel of the screen format (masks 0x4d5f54 / 5c / 64,
 * shifts 0x4d5f51 / 59 / 61) is and-ed with ~mask. 0 when the lock
 * fails. ===== */
uint32_t GAMEAPI surface_pixel_read(uint32_t which, uint32_t x, uint32_t y, uint32_t mask)
{
    uint8_t desc[0x6c];
    uint32_t surf = which == 0 ? G32(0x4d5f48) : G32(0x4d5f4c);
    uint32_t bytes, value, nm;
    uint8_t *p;
    int i;

    for (i = 0; i < 0x6c; i++)
        desc[i] = 0;
    if (surface_lock(0, surf, A32(desc)) == 0)
        return 0;
    switch (U32(desc, 0x54)) {
    case 0x10:
    case 0x0f:
        bytes = 2;
        break;
    case 0x18:
        bytes = 3;
        break;
    default:
        bytes = U32(desc, 0x54) != 0x20 ? 1 : 4;
        break;
    }
    p = P8(U32(desc, 0x24) + y * U32(desc, 0x10)) + x * bytes;
    if (bytes == 1)
        value = p[0];
    else if (bytes == 2)
        value = *(uint16_t *)p;
    else if (bytes == 3)
        value = ((uint32_t)p[2] << 16) | *(uint16_t *)p;
    else
        value = p[0]; /* sic: 32-bit reads one byte */
    surface_unlock(surf);
    if (mask == 0 || bytes <= 1)
        return value;
    nm = ~mask;
    return (((G32(0x4d5f54) & value) >> SH(0x4d5f51) & nm) << SH(0x4d5f51)) |
           (((G32(0x4d5f5c) & value) >> SH(0x4d5f59) & nm) << SH(0x4d5f59)) |
           (((G32(0x4d5f64) & value) >> SH(0x4d5f61) & nm) << SH(0x4d5f61));
}

/* ===== 0x41f550: draw a flat-shaded or textured rectangle as a triangle
 * list: corners (x0, y0) .. (x1, y1) with texture coordinates (u0, v0) ..
 * (u1, v1) as given (floats), z `z`, colour bytes r g b a as floats; render
 * state `state` (| 0x200 with `blend`, which also goes to the blend mode).
 * Inside its own BeginScene / EndScene unless `in_scene`. 0 when the scene
 * cannot begin. ===== */
int32_t GAMEAPI screen_quad_draw(uint32_t state, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t u0,
                                 uint32_t v0, uint32_t u1, uint32_t v1, uint32_t z, uint32_t colour,
                                 uint32_t in_scene, uint32_t blend)
{
    uint32_t v[4][12];
    uint32_t fr, fg, fb, fa;
    int k;

    mem_fill(A32(v), 0, 0xc0);
    v[0][2] = x1;
    v[1][2] = x1;
    v[1][3] = y1;
    v[3][3] = y1;
    v[0][3] = y0;
    v[2][3] = y0;
    v[2][2] = x0;
    v[3][2] = x0;
    if (in_scene == 0 && d3d_scene_begin() == 0)
        return 0;
    if (blend != 0)
        state |= 0x200;
    render_blend_set(0, blend);
    render_state_select_c(state);
    fr = pa_from_int((int32_t)(colour & 0xff));
    fg = pa_from_int((int32_t)((colour >> 8) & 0xff));
    fb = pa_from_int((int32_t)((colour >> 16) & 0xff));
    fa = pa_from_int((int32_t)(colour >> 24));
    for (k = 0; k < 4; k++) {
        v[k][1] = z;
        v[k][8] = fr;
        v[k][9] = fg;
        v[k][10] = fb;
        v[k][11] = fa;
    }
    v[0][6] = u1;
    v[1][6] = u1;
    v[1][7] = v1;
    v[3][7] = v1;
    v[2][6] = u0;
    v[3][6] = u0;
    v[0][7] = v0;
    v[2][7] = v0;
    vertex_emit_c(P8(A32(v[0])));
    vertex_emit_c(P8(A32(v[1])));
    vertex_emit_c(P8(A32(v[2])));
    vertices_quad_close();
    vertex_emit_c(P8(A32(v[3])));
    G32(0x4bddd8) = G32(0x4bddd8) + 1;
    vertices_draw_list_noclip();
    if (in_scene == 0)
        d3d_scene_end();
    return 1;
}

/* ===== 0x41f700: the byte sum of n bytes (a checksum). ===== */
uint32_t GAMEAPI bytes_sum8(const uint8_t *p, int32_t n)
{
    uint8_t sum = 0;

    for (; n > 0; n--)
        sum = (uint8_t)(sum + *p++);
    return sum;
}

/* ===== 0x41fb60: can the card make PowerVR compressed textures ('PVRC'
 * FOURCC)? A 2x2 texture surface from DirectDraw2 (0x4a3540), locked and
 * unlocked, its texture interface and handle; 1 when the locked surface's
 * second dword is non-zero (read after the unlock, sic). The DirectDraw2
 * interface is never released (sic). The surface description is zeroed
 * here (the original leaves its other fields uninitialised). ===== */
int32_t GAMEAPI pvrc_texture_probe(void)
{
    uint8_t desc[0x6c];
    uint32_t dd2 = 0, surf = 0, tex = 0, handle = 0, bits;
    int32_t ok;
    int i;

    for (i = 0; i < 0x6c; i++)
        desc[i] = 0;
    COM_OUT32(dd2, g_hresult = (uint32_t)IDirectDraw_QueryInterface(DDRAW, GMEM(0x4a3540), &out_));
    if (g_hresult != 0)
        return 0;
    U32(desc, 0x08) = 2;
    U32(desc, 0x0c) = 2;
    U32(desc, 0x00) = 0x6c;
    U32(desc, 0x04) = 0x1007;
    U32(desc, 0x68) = 0x1000;
    U32(desc, 0x48) = 0x20;
    U32(desc, 0x4c) = 4;
    U32(desc, 0x50) = 0x43525650;
    COM_OUT32(surf, g_hresult = (uint32_t)IDirectDraw_CreateSurface(dd2, desc, &out_, 0));
    if (g_hresult != 0)
        return 0;
    g_hresult = (uint32_t)IDirectDrawSurface_Lock(surf, 0, desc, 1, 0);
    if (g_hresult != 0) {
        IDirectDrawSurface_Release(surf);
        return 0;
    }
    bits = U32(desc, 0x24);
    g_hresult = (uint32_t)IDirectDrawSurface_Unlock(surf, A32(desc));
    if (g_hresult != 0) {
        IDirectDrawSurface_Release(surf);
        return 0;
    }
    COM_OUT32(tex, g_hresult = (uint32_t)IDirectDrawSurface_QueryInterface(surf, GMEM(0x4a3920), &out_));
    if (g_hresult != 0) {
        IDirectDrawSurface_Release(surf);
        return 0;
    }
    g_hresult = (uint32_t)IDirect3DTexture2_GetHandle(tex, GMEM(DEVICE), &handle);
    if (g_hresult != 0) {
        IDirect3DTexture2_Release(tex);
        IDirectDrawSurface_Release(surf);
        return 0;
    }
    ok = G32(bits + 4) != 0;
    IDirect3DTexture2_Release(tex);
    IDirectDrawSurface_Release(surf);
    return ok;
}

/* ===== 0x41fcf0: the display card from DirectDraw4's device identifier
 * (0x4a3550): 1 for vendor 0x12d2 device 0x18 / 0x19 (Riva 128), 2 for
 * vendor 0x10de device 0x20 / 0x28 / 0x2c (TNT family), else 0. ===== */
int32_t GAMEAPI ddraw_device_id_check(void)
{
    uint8_t ident[0x428];
    uint32_t dd4 = 0, vendor, device;
    int i;

    for (i = 0; i < 0x428; i++)
        ident[i] = 0;
    COM_OUT32(dd4, g_hresult = (uint32_t)IDirectDraw_QueryInterface(DDRAW, GMEM(0x4a3550), &out_));
    if (g_hresult != 0)
        return 0;
    g_hresult = (uint32_t)IDirectDraw4_GetDeviceIdentifier(dd4, ident, 0);
    IDirectDraw_Release(dd4);
    if (g_hresult != 0)
        return 0;
    vendor = U32(ident, 0x408);
    device = U32(ident, 0x40c);
    if (vendor == 0x12d2) {
        if (device == 0x18 || device == 0x19)
            return 1;
    } else if (vendor == 0x10de) {
        if (device == 0x20 || device == 0x28 || device == 0x2c)
            return 2;
    }
    return 0;
}

/* ===== 0x41fda0: the blend mode tables from the device's blend caps
 * (0x4b86bc kind bits, caps 0x4b8608 / 0x4b860c): source / destination
 * blend 0x4bcd8c / 90 (normal) and 0x4bcd94 / 98 (additive), the alpha
 * flags 0x6ccd84; then card (0x41fcf0) and the config's blend setting
 * (+0x434) overrides. ===== */
void GAMEAPI blend_modes_choose(void)
{
    uint32_t kind = G32(0x4b86bc);
    int32_t card;

    G32(0x6ccd84) = 0;
    if (kind != 0) {
        if (kind & 1) {
            if ((G8(0x4b8608) & 2) && (G8(0x4b860c) & 8))
                G32(0x6ccd84) = 2;
            G32(0x4bcd8c) = 2;
            G32(0x4bcd90) = 2;
            G32(0x4bcd94) = 1;
            G32(0x4bcd98) = 4;
        } else {
            if (kind & 2) {
                if ((G8(0x4b8608) & 0x20) && (G8(0x4b860c) & 0x10))
                    G32(0x6ccd84) = 2;
                G32(0x4bcd8c) = 5;
                G32(0x4bcd90) = 6;
                G32(0x4bcd94) = 6;
                G32(0x4bcd98) = 5;
            }
            if (kind & 4)
                G32(0x6ccd84) |= 5;
            if (kind & 8)
                G32(0x6ccd84) |= 5;
        }
    }
    card = ddraw_device_id_check();
    if (card == 1) {
        G8(0x6ccd84) &= 0xfd;
        ddraw_device_id_check();
        return;
    }
    if (card == 2) {
        G32(0x4bcd90) = 4;
        ddraw_device_id_check();
        return;
    }
    switch (G32(G32(0x4b2198) + 0x434)) {
    case 1:
        if (G8(0x4b86bc) & 1) {
            G32(0x4bcd90) = 4;
            ddraw_device_id_check();
            return;
        }
        break;
    case 3:
        if (G8(0x4b86bc) & 1)
            G32(0x4bcd90) = 4;
        /* fall through */
    case 2:
        G8(0x6ccd84) &= 0xfd;
        break;
    case 4:
        G32(0x4bcd8c) = 1;
        G8(0x6ccd84) &= 0xfd;
        G32(0x4bcd90) = 1;
        break;
    }
    ddraw_device_id_check();
}

/* The palette store: up to 4 texture slots (0x4b836c, count 0x4bde04) with
 * 256-entry palettes at 0x4bcdc4 + i * 0x400. */
#define PAL_SLOT(i) G32(0x4b836c + (uint32_t)(i) * 4)
#define PAL_COUNT GI32(0x4bde04)
#define PAL_DATA(i) (0x4bcdc4 + (uint32_t)(i) * 0x400)

/* ===== 0x41fef0: lock texture page `page`: its bits, bit count and pitch;
 * with flags bit 0 also its palette (the stored copy, else the surface's
 * palette entries when palettes are in use, 0x4bcd78). 0 for a page past
 * the last (0x6cce54) or a failed lock. The palette slot starts as the page
 * number (the reused argument slot, sic). ===== */
int32_t GAMEAPI texture_lock(int32_t page, uint32_t flags, uint32_t *bits, uint32_t *palette, uint32_t *bit_count,
                             uint32_t *pitch)
{
    uint8_t desc[0x6c];
    uint32_t slot;
    int32_t i, n;
    int k;

    if (page > GI32(0x6cce54))
        return 0;
    for (k = 0; k < 0x6c; k++)
        desc[k] = 0;
    slot = PAGE_SLOT(page);
    i = surface_lock(0, SLOT(4, slot), A32(desc));
    if (i == 0)
        return i;
    *bits = U32(desc, 0x24);
    *bit_count = U32(desc, 0x54);
    *pitch = U32(desc, 0x10);
    if ((flags & 1) == 0)
        return 1;
    n = PAL_COUNT;
    for (i = 0; i < n; i++)
        if (PAL_SLOT(i) == slot) {
            const uint32_t *s = (const uint32_t *)(uintptr_t)PAL_DATA(i);

            for (k = 0; k < 0x100; k++)
                palette[k] = s[k];
            return 1;
        }
    if (G32(0x4bcd78) != 0) {
        uint32_t pal = (uint32_t)page;

        COM_OUT32(pal, g_hresult = (uint32_t)IDirectDrawSurface_GetPalette(SLOT(4, slot), &out_));
        if (pal != 0) {
            IDirectDrawPalette_GetEntries(pal, 0, 0, 0x100, palette);
            IDirectDrawPalette_Release(pal);
        }
    }
    return 1;
}

/* ===== 0x420000: unlock texture page `page`; unless flags bit 1, note its
 * slot in the palette store (when new and there is room) and with bit 0
 * store `palette` there. 0 for a page past the last. ===== */
int32_t GAMEAPI texture_unlock(int32_t page, uint32_t flags, const uint32_t *palette)
{
    uint32_t slot;
    int32_t i, n;
    int k;

    if (page > GI32(0x6cce54))
        return 0;
    slot = PAGE_SLOT(page);
    surface_unlock(SLOT(4, slot));
    if (flags & 2)
        return 1;
    n = PAL_COUNT;
    i = 0;
    if (n > 0) {
        while (i < n && PAL_SLOT(i) != slot)
            i++;
        if (i >= n && i < 4) {
            PAL_SLOT(i) = slot;
            PAL_COUNT = n + 1;
        }
    } else {
        PAL_SLOT(0) = slot;
        PAL_COUNT = n + 1;
    }
    if ((flags & 1) && i < 4) {
        uint32_t *d = (uint32_t *)(uintptr_t)PAL_DATA(i);

        for (k = 0; k < 0x100; k++)
            d[k] = palette[k];
    }
    return 1;
}

/* ===== 0x420090: reload every texture in the palette store (0x420150)
 * and, when palettes are in use, give both its surfaces a new palette
 * (DDPCAPS_8BIT | DDPCAPS_ALLOW256) from the stored entries; empty the
 * store. The palette pointer's slot starts uninitialised (the caller's
 * ecx): 0 here. ===== */
void GAMEAPI texture_palettes_apply(void)
{
    uint32_t pal = 0;
    int32_t i;

    for (i = 0; i < PAL_COUNT; i++) {
        uint32_t slot = PAL_SLOT(i);

        texture_reload(slot);
        if (G32(0x4bcd78) != 0) {
            COM_OUT32(pal, g_hresult = (uint32_t)IDirectDraw_CreatePalette(DDRAW, 0x4c, GMEM(PAL_DATA(i)), &out_, 0));
            g_hresult = (uint32_t)IDirectDrawSurface_SetPalette(SLOT(4, slot), pal);
            g_hresult = (uint32_t)IDirectDrawSurface_SetPalette(SLOT(8, slot), pal);
            IDirectDrawPalette_Release(pal);
        }
    }
    PAL_COUNT = 0;
}

/* ===== 0x420150: reload texture slot `slot`: the system-memory surface's
 * texture interface Loaded into the video one (whose texture interface goes
 * to +0xc, then its handle via 0x41d940). 0 when a call fails. ===== */
int32_t GAMEAPI texture_reload(uint32_t slot)
{
    uint32_t tex = 0;
    int32_t r;

    COM_OUT32(tex, g_hresult = (uint32_t)IDirectDrawSurface_QueryInterface(SLOT(4, slot), GMEM(0x4a3920), &out_));
    if (g_hresult != 0)
        return 0;
    g_hresult = (uint32_t)IDirectDrawSurface_QueryInterface(SLOT(8, slot), GMEM(0x4a3920),
                                                            (uint32_t *)GMEM(0x4ba708 + slot * 0x24)); /* the slot's +0xc */
    if (g_hresult != 0)
        return 0;
    r = texture_slot_handle_x(slot);
    if (r == 0)
        return r;
    g_hresult = (uint32_t)IDirect3DTexture2_Load(SLOT(0xc, slot), GMEM(tex));
    if (g_hresult != 0)
        return 0;
    IDirect3DTexture2_Release(tex);
    return 1;
}

/* ===== 0x4201f0: cut an 8-bit w x h image (rows `pitch` apart) into
 * 256x256 texture pages (zero-padded) from the next free page on, with the
 * palette (4 bytes per entry, the fourth cleared). Returns the first page,
 * -1 when an allocation or a page fails (sic: nothing freed then). ===== */
int32_t GAMEAPI image_tiles_upload(uint32_t src, const uint8_t *palette, int32_t w, int32_t h, int32_t pitch)
{
    int32_t first = texture_page_next();
    int32_t page = first;
    uint32_t buf, pal, row;
    int32_t hrem, k;

    buf = block_alloc(0x10000, 0);
    pal = block_alloc(0x400, 0);
    if (buf == 0 || pal == 0)
        return -1;
    for (k = 0; k < 0x100; k++) {
        G8(pal + k * 4) = palette[k * 4];
        G8(pal + k * 4 + 1) = palette[k * 4 + 1];
        G8(pal + k * 4 + 2) = palette[k * 4 + 2];
        G8(pal + k * 4 + 3) = 0;
    }
    hrem = h;
    row = src;
    if (hrem > 0) {
        do {
            int32_t wrem = w;
            uint32_t col = row;

            if (wrem > 0) {
                do {
                    int32_t cols = wrem < 0x100 ? wrem : 0x100;
                    int32_t rows = hrem < 0x100 ? hrem : 0x100;
                    uint32_t d = buf, s = col;

                    mem_fill(buf, 0, 0x10000);
                    for (; rows > 0; rows--) {
                        mem_copy(d, s, cols);
                        s += (uint32_t)pitch;
                        d += 0x100;
                    }
                    if (texture_page_create(page, 0x100, 0x100, buf, pal, 0) == 0)
                        return -1;
                    wrem -= 0x100;
                    page++;
                    col += 0x100;
                } while (wrem > 0);
            }
            row += (uint32_t)pitch << 8;
            hrem -= 0x100;
        } while (hrem > 0);
    }
    block_free(buf);
    block_free(pal);
    return first;
}

/* ===== 0x4203a0: draw an image's texture pages (from `first`, 256x256
 * each, w x h texels) stretched over `rect` (left, top, right, bottom) as
 * sprite packets (0x420be0): code 0x98 (0x9a with `blend`), colour
 * `colour` (premultiplied by its alpha unless 0x6ccd84 bit 0), z 2.1. The
 * steps are computed before the first == -1 check (a zero size traps,
 * as in the original). The packet is zeroed here (the original leaves its
 * unused fields uninitialised). ===== */
void GAMEAPI image_tiles_draw(int32_t first, int32_t w, int32_t h, const int32_t *rect, int32_t blend,
                              uint32_t colour)
{
    uint8_t pk[0x78];
    volatile int32_t step_x, step_y;
    int32_t dx, dy, y, hrem;
    uint32_t a;
    int k;

    step_x = (int32_t)((uint32_t)(rect[2] - rect[0]) << 8) / w;
    step_y = (int32_t)((uint32_t)(rect[3] - rect[1]) << 8) / h;
    if (first == -1)
        return;
    dx = step_x;
    dy = step_y;
    for (k = 0; k < 0x78; k++)
        pk[k] = 0;
    U32(pk, 0x60) = U32(pk, 0x44) = U32(pk, 0x28) = U32(pk, 0x0c) = 0x40066666;
    pk[0x58] = 0;
    pk[0x20] = 0;
    pk[0x3d] = 0;
    pk[0x21] = 0;
    U32(pk, 0x08) = 0x40;
    a = colour >> 24;
    if (G8(0x6ccd84) & 1) {
        U32(pk, 0x04) = colour;
    } else {
        uint32_t c0 = colour & 0xff, c1 = (colour >> 8) & 0xff, c2 = (colour >> 16) & 0xff;

        U32(pk, 0x04) = (a << 24) | (((c2 * a) >> 8) << 16) | (((c1 * a) >> 8) << 8) | ((c0 * a) >> 8);
    }
    pk[0x22] = (uint8_t)a;
    pk[0x07] = blend != 0 ? 0x9a : 0x98;
    y = rect[1];
    hrem = h;
    if (hrem <= 0)
        return;
    do {
        int32_t x = rect[0];
        int32_t hh = hrem < 0x100 ? hrem : 0x100;
        int32_t wrem = w;

        if (wrem > 0) {
            int32_t y2 = y + dy;
            int32_t x2 = x + dx;

            do {
                int32_t ww = wrem < 0x100 ? wrem : 0x100;
                uint32_t f;

                f = pa_from_int(x);
                U32(pk, 0x48) = f;
                U32(pk, 0x10) = f;
                f = pa_from_int(y);
                U32(pk, 0x30) = f;
                U32(pk, 0x14) = f;
                f = pa_from_int(x2 < rect[2] ? x2 : rect[2]);
                U32(pk, 0x64) = f;
                U32(pk, 0x2c) = f;
                f = pa_from_int(y2 < rect[3] ? y2 : rect[3]);
                U32(pk, 0x68) = f;
                U32(pk, 0x4c) = f;
                U16(pk, 0x3e) = (uint16_t)first;
                pk[0x74] = pk[0x3c] = (uint8_t)(ww - 1); /* through float and _ftol: exact */
                pk[0x75] = pk[0x59] = (uint8_t)(hh - 1);
                sprite_packet_draw(A32(pk));
                first++;
                wrem -= 0x100;
                x += dx;
                x2 += dx;
            } while (wrem > 0);
        }
        y += dy;
        hrem -= 0x100;
    } while (hrem > 0);
}

/* ===== 0x4205e0: thunk of 0x41d910 (release all texture slots). ===== */
int32_t GAMEAPI texture_slots_release_thunk(void)
{
    return texture_slots_release_x();
}

/* ===== 0x4205f0: the next texture page number (*0x6cce54 + 1). ===== */
int32_t GAMEAPI texture_page_next(void)
{
    return GI32(0x6cce54) + 1;
}

/* ===== 0x420600: with the snap switch 0x4b86b8, round every vertex's
 * screen x / y (*0x4bcd80, stride *0x4a8080, count 0x4bddd8) to whole
 * pixels (fistp through 0x4b8368 / 0x4bddc4, current rounding). The count
 * is not checked (sic: 0 runs away). ===== */
void GAMEAPI vertices_snap(void)
{
    uint32_t p, stride, n;

    if (G32(0x4b86b8) == 0)
        return;
    p = G32(0x4bcd80);
    stride = G32(0x4a8080);
    n = G32(0x4bddd8);
    do {
        __asm__ volatile("flds (%0)\n\t"
                         "flds 4(%0)\n\t"
                         "fistpl 0x4bddc4\n\t"
                         "fistpl 0x4b8368\n\t"
                         "fildl 0x4bddc4\n\t"
                         "fildl 0x4b8368\n\t"
                         "fstps (%0)\n\t"
                         "fstps 4(%0)"
                         :
                         : "r"(P8(p))
                         : "memory", "st", "st(1)");
        p += stride;
    } while (--n != 0);
}

/* ===== 0x420650: thunk of 0x420600. ===== */
void GAMEAPI vertices_snap_thunk(void)
{
    vertices_snap();
}

/* ===== 0x420660 / 0x4206a0 / 0x4206e0: snap, DrawPrimitive the vertex
 * buffer as TL vertices (type 3): a triangle list with D3DDP_DONOTCLIP, a
 * triangle list (flags 0x1c), a triangle fan (0x1c); empty the buffer. ===== */
void GAMEAPI vertices_draw_list_noclip(void)
{
    vertices_snap();
    g_hresult = (uint32_t)IDirect3DDevice2_DrawPrimitive(DEVICE, 4, 3, G32(0x4bcd80), G32(0x4bddd8), 8);
    G32(0x4bddd8) = 0;
}

void GAMEAPI vertices_draw_list(void)
{
    vertices_snap();
    g_hresult = (uint32_t)IDirect3DDevice2_DrawPrimitive(DEVICE, 4, 3, G32(0x4bcd80), G32(0x4bddd8), 0x1c);
    G32(0x4bddd8) = 0;
}

void GAMEAPI vertices_draw_fan(void)
{
    vertices_snap();
    g_hresult = (uint32_t)IDirect3DDevice2_DrawPrimitive(DEVICE, 6, 3, G32(0x4bcd80), G32(0x4bddd8), 0x1c);
    G32(0x4bddd8) = 0;
}

/* ===== 0x420720: snap, then DrawIndexedPrimitive (triangle list, the six
 * quad indices 0x4a80b0) while the count is positive, 4 less each time
 * (sic: always from the buffer's start). ===== */
void GAMEAPI vertices_draw_quads(void)
{
    uint32_t p;
    int32_t n;

    vertices_snap();
    n = GI32(0x4bddd8);
    p = G32(0x4bcd80);
    while (n > 0) {
        g_hresult = (uint32_t)IDirect3DDevice2_DrawIndexedPrimitive(DEVICE, 4, 3, p, (uint32_t)n, 0x4a80b0, 6, 0x1c);
        n = GI32(0x4bddd8) - 4;
        GI32(0x4bddd8) = n;
    }
}

/* ===== 0x420770: the blend render states for `flags` (bit 1 additive:
 * source / destination blend 0x13 / 0x14 from 0x4bcd94 / 98, else 0x4bcd8c /
 * 90, only when the flags change) and `mode` (0 opaque, 1 transparent,
 * else blended): alpha blend enable 0x1b, colour key 0x29, alpha test 0x21
 * and z write 0xe (when 0x6cce80) by the device kind 0x4b86bc. Remembered in
 * 0x6cce64 / 0x6cce68. ===== */
void GAMEAPI render_blend_set(uint32_t flags, uint32_t mode)
{
    if (G32(0x6cce64) != flags) {
        if (flags & 2) {
            RENDER_STATE(0x13, G32(0x4bcd94));
            RENDER_STATE(0x14, G32(0x4bcd98));
        } else {
            RENDER_STATE(0x13, G32(0x4bcd8c));
            RENDER_STATE(0x14, G32(0x4bcd90));
        }
    }
    if (G8(0x4b86bc) & 1) {
        if (mode == 0) {
            RENDER_STATE(0x29, 1);
            RENDER_STATE(0x1b, 0);
            if (G32(0x6cce80) != 0)
                RENDER_STATE(0x0e, 1);
        } else if (mode == 1) {
            RENDER_STATE(0x29, 1);
            RENDER_STATE(0x1b, 0);
            if (G32(0x6cce80) != 0)
                RENDER_STATE(0x0e, 0);
        } else {
            RENDER_STATE(0x29, 0);
            RENDER_STATE(0x1b, 1);
            if (G32(0x6cce80) != 0)
                RENDER_STATE(0x0e, 0);
        }
    } else if (mode == 0) {
        RENDER_STATE(0x29, (flags & 1) ? 1 : 0);
        if (G8(0x4b86bc) & 2)
            RENDER_STATE(0x1b, 0);
        if (G8(0x4b86bc) & 8)
            RENDER_STATE(0x21, 0);
        if (G32(0x6cce80) != 0)
            RENDER_STATE(0x0e, 1);
    } else {
        uint32_t blended = mode != 1;

        RENDER_STATE(0x29, blended ? 0 : 1);
        if (G8(0x4b86bc) & 2)
            RENDER_STATE(0x1b, blended);
        if (G8(0x4b86bc) & 8)
            RENDER_STATE(0x21, blended);
        if (G32(0x6cce80) != 0)
            RENDER_STATE(0x0e, 0);
    }
    G32(0x6cce68) = mode;
    G32(0x6cce64) = flags;
}

/* ===== 0x4209c0: the current texture page (0x6cce58; 0x3f none): its
 * coordinate tables to 0x6ce660 / 0x6ce65c (none: both *0x4bddfc) and its
 * handle as render state 1. ===== */
void GAMEAPI render_texture_set(uint32_t page)
{
    uint32_t slot, u, v, handle;

    G32(0x6cce58) = page;
    if (page == 0x3f) {
        G32(0x6ce660) = G32(0x4bddfc);
        G32(0x6ce65c) = G32(0x4bddfc);
        RENDER_STATE(1, 0);
        return;
    }
    slot = PAGE_SLOT(page);
    v = SLOT(0x20, slot);
    u = SLOT(0x1c, slot);
    handle = SLOT(0, slot);
    G32(0x6ce65c) = v;
    G32(0x6ce660) = u;
    RENDER_STATE(1, handle);
}

/* ===== 0x420a30 (ebx: state): select render state `state` (low 10 bits:
 * texture page 0..5, blend flags 6..7 | *0x6cce60, mode 8..9) when it
 * changed (0x6cce5c). ===== */
void GAMEAPI render_state_select_c(uint32_t state)
{
    uint32_t s = state & 0x3ff;
    uint32_t flags, mode, page;

    if (s == G32(0x6cce5c))
        return;
    G32(0x6cce5c) = s;
    flags = ((s >> 6) & 3) | G32(0x6cce60);
    mode = s >> 8;
    page = s & 0x3f;
    if (mode != G32(0x6cce68) || flags != G32(0x6cce64))
        render_blend_set(flags, mode);
    if (page != G32(0x6cce58))
        render_texture_set(page);
}

#if defined(__i386__) /* x86-64: every caller is C and calls the body */
__asm__(".text\n"
        ".globl render_state_select\n"
        "render_state_select:\n\t"
        "pushl %ebx\n\t"
        "call render_state_select_c\n\t"
        "addl $4, %esp\n\t"
        "ret\n");
#endif

/* The next TL vertex slot: *0x4bcd80 + the 0x4a807c offset for the count
 * 0x4bddd8 (incremented). */
static uint8_t *pa_vertex_next(void)
{
    uint32_t n = G32(0x4bddd8);

    G32(0x4bddd8) = n + 1;
    return P8(G32(0x4bcd80) + G32(0x4a807c + n * 4));
}

/* x87 for both emitters: fld w; fld1; fdiv st, st(1) -> +0xc (rhw 1 / w);
 * fmul 1/65536 -> +8 (z). */
static inline void pa_vertex_depth(uint8_t *o, const uint8_t *v)
{
    uint32_t k = 0x37800000;

    __asm__ volatile("flds 4(%1)\n\t"
                     "fld1\n\t"
                     ".byte 0xd8, 0xf1\n\t"
                     "fstps 0xc(%0)\n\t"
                     "fmuls %2\n\t"
                     "fstps 8(%0)"
                     :
                     : "r"(o), "r"(v), "m"(k)
                     : "memory", "st", "st(1)");
}

/* ===== 0x420a90 (eax colour, ebx texture indices, edx vertex): append a
 * TL vertex: screen x / y from vertex +8 / +0xc, z / rhw from +4, colour
 * with red and blue swapped, u / v from the current tables by index (bl,
 * bh). ===== */
void GAMEAPI vertex_emit_indexed_c(uint32_t colour, uint32_t uv, const uint8_t *v)
{
    uint8_t *o = pa_vertex_next();
    uint32_t x = U32(v, 8), y = U32(v, 0xc);

    U32(o, 0) = x;
    U32(o, 4) = y;
    U32(o, 0x18) = G32(G32(0x6ce660) + (uv & 0xff) * 4);
    U32(o, 0x1c) = G32(G32(0x6ce65c) + ((uv >> 8) & 0xff) * 4);
    U32(o, 0x10) = (colour & 0xff00ff00u) | ((colour & 0xff) << 16) | ((colour >> 16) & 0xff);
    pa_vertex_depth(o, v);
}

#if defined(__i386__) /* x86-64: every caller is C and calls the body */
__asm__(".text\n"
        ".globl vertex_emit_indexed\n"
        "vertex_emit_indexed:\n\t"
        "pushl %edx\n\t"
        "pushl %ebx\n\t"
        "pushl %eax\n\t"
        "call vertex_emit_indexed_c\n\t"
        "addl $12, %esp\n\t"
        "ret\n");
#endif

/* ===== 0x420b10 (edx vertex, the clipper's 0x30-byte layout): append a TL
 * vertex: screen x / y, z / rhw, u / v (+0x18 / +0x1c) and the colour
 * floats +0x20..+0x2c rounded (fistp) to bytes; alpha 0 unless 0x6ccd84
 * bit 0. ===== */
void GAMEAPI vertex_emit_c(const uint8_t *v)
{
    uint8_t *o = pa_vertex_next();
    uint32_t c = 0;

    U32(o, 0) = U32(v, 8);
    U32(o, 4) = U32(v, 0xc);
    U32(o, 0x18) = U32(v, 0x18);
    U32(o, 0x1c) = U32(v, 0x1c);
    pa_vertex_depth(o, v);
    if (G32(0x6ccd84) & 1)
        c = (pa_fist(v + 0x2c) & 0xff) << 24;
    c |= (pa_fist(v + 0x20) & 0xff) << 16;
    c |= (pa_fist(v + 0x24) & 0xff) << 8;
    c |= pa_fist(v + 0x28) & 0xff;
    U32(o, 0x10) = c;
}

#if defined(__i386__) /* x86-64: every caller is C and calls the body */
__asm__(".text\n"
        ".globl vertex_emit\n"
        "vertex_emit:\n\t"
        "pushl %edx\n\t"
        "call vertex_emit_c\n\t"
        "popl %edx\n\t"
        "ret\n");
#endif

/* ===== 0x420bb0: emit a vertex list (count, then vertex pointers) and
 * draw it as a fan; nothing for a null or empty list. ===== */
void GAMEAPI vertex_list_draw_fan(const int32_t *list)
{
    if (list != 0 && list[0] > 0) {
        int32_t n = list[0];
        const uint32_t *p = (const uint32_t *)list + 1;

        do {
            vertex_emit_c(P8(*p));
            p++;
        } while (--n != 0);
        vertices_draw_fan();
    }
}
