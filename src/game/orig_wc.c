/* Group WC: the bodies of the original functions our wrappers still ran
 * through a trampoline (docs/native/plan64.md, phase 1's last step). Each
 * wrapper keeps its hook and now calls the body here instead:
 *
 *  - 0x41e710 the texture page upload (src/vkgame.c's texture_upload): a
 *    texture slot (0x4ba700, 64 x 0x24), a system-memory surface filled
 *    from 8-bit pixels (16-bit through the colour table, 8-bit with a
 *    DirectDraw palette, or the 0x41e440 converter), then the device
 *    texture loaded from it;
 *  - 0x41f720 the Direct3D capability probe (src/vkgame.c's vk_probe): test
 *    quads drawn, flipped and read back to decide the config bytes
 *    +0x98..+0x9b (x87 for the screen-size floats);
 *  - 0x4771c0 the game window (src/vkgame.c's create_window);
 *  - 0x4876f0 the flip (src/capture.c's capture_flip);
 *  - 0x443b50 race_draw (src/game/frame.c's race_draw, through
 *    race_draw_original);
 *  - 0x471c90 sound_play and 0x471da0 the moving-sound start (src/verify.c's
 *    recording hooks);
 *  - the C runtime's empty function 0x4979b7 (a lone ret), referenced only
 *    as data by _FPmtinit / _FPmtterm (0x4b4ab0 / 0x4b4ab4): a C no-op
 *    and the patch that points those two slots at it.
 *
 * Windows and COM calls go through winapi.h and com.h. Everything outside
 * the group is called by address. */
#include "../runtime.h"
#include "gamefn.h"
#include "physics.h"
#include "com.h"

#define A(p) ((uint32_t)(uintptr_t)(p))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define U8(a, off) (*(uint8_t *)(uintptr_t)((uint32_t)(uintptr_t)(a) + (uint32_t)(off)))
#define U16(a, off) (*(uint16_t *)(uintptr_t)((uint32_t)(uintptr_t)(a) + (uint32_t)(off)))
#define U32(a, off) (*(uint32_t *)(uintptr_t)((uint32_t)(uintptr_t)(a) + (uint32_t)(off)))
#define I32(a, off) (*(int32_t *)(uintptr_t)((uint32_t)(uintptr_t)(a) + (uint32_t)(off)))
#define G8(a) U8(a, 0)
#define G32(a) U32(a, 0)
#define GI32(a) I32(a, 0)

/* Calls kept off their callee's prototype: 0x4291f0 (dx_error_ignore)
 * reads no argument but the original pushes the result; the voice
 * allocation 0x472920 (uint8_t) and the voice start 0x471eb0 (void) are
 * passed on as the whole eax they leave (callers keep only al). */
typedef void(GAMEAPI *wc_dx_error_fn)(uint32_t hr);
typedef uint32_t(GAMEAPI *wc_voice_alloc_fn)(uint32_t sound, uint32_t kind, uint32_t vol, uint32_t pitch,
                                             uint32_t near);
typedef uint32_t(GAMEAPI *wc_voice_start_fn)(uint32_t voice, uint32_t kind, uint32_t left, uint32_t right,
                                             uint32_t pitch, uint32_t flags);
#define wc_dx_error ((wc_dx_error_fn)(uintptr_t)0x4291f0)
#define wc_voice_alloc ((wc_voice_alloc_fn)(uintptr_t)0x472920)
#define wc_voice_start ((wc_voice_start_fn)(uintptr_t)0x471eb0)

#define g_config G32(0x4b2198)
#define g_race G32(0x4b1960)
#define g_screen_w GI32(0x4acc38)
#define g_screen_h GI32(0x4acc3c)
#define g_dd_result G32(0x4d6f18) /* the last DirectDraw / Direct3D result */
#define g_ddraw G32(0x4d5f40)     /* IDirectDraw2 */
#define g_front G32(0x4d5f48)     /* the primary surface */
#define g_back G32(0x4d5f4c)      /* the back buffer */
#define g_draw_flags G32(0x6cce40)
#define CAR_RECORD(i) (0x6bfb80u + (uint32_t)(i) * 0x274u)
#define DDERR_SURFACELOST 0x887601c2u
#define DDERR_WASSTILLDRAWING 0x887600e1u

/* ---- x87: the original's instruction sequences ---- */

#define X87_CLOBBER "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* fild dword [a]; fstp dword tmp; fld tmp; fsub dword [b]; fstp dword */
static inline uint32_t wc_int_minus(uint32_t a, uint32_t b)
{
    uint32_t r, t;
    __asm__ volatile("fildl %2\n\tfstps %1\n\tflds %1\n\tfsubs %3\n\tfstps %0"
                     : "=m"(r), "=m"(t)
                     : "m"(a), "m"(b)
                     : "st");
    return r;
}

/* 0x420a30 takes its argument in ebx (0x41f720 leaves 0x3f there). */
static void wc_state_select(uint32_t ebx)
{
#if defined(__i386__)
    uint32_t fn = 0x420a30, c, d;
    __asm__ volatile("call *%0" : "+a"(fn), "=c"(c), "=d"(d) : "b"(ebx) : "memory", "cc", X87_CLOBBER);
#else
    render_state_select_c(ebx);
#endif
}

/* ===== 0x41e710: texture page upload ===== */

/* 0x41e710(page, w, h, pixels, palette, flags): finds a free texture slot
 * (0x41ec20), records it for `page` (0x6cd300) and its power-of-two size
 * (0x41e1a0; height = w when 0x4b86c0 asks for square pages, sic) and size
 * codes (0x4bdddc), then, with Direct3D textures on (0x4b86a4), creates a
 * system-memory surface (template 0x4bcce8), locks it (0x4875a0) and fills
 * it from the 8-bit pixels: flags & 2 through the converter 0x41e440;
 * otherwise 16-bit through the colour table *0x4bcd7c (made by 0x41e1d0
 * from the palette) or, with 8-bit textures (0x4bcd54), copied rows and a
 * DirectDraw palette (palette entries after the first get a red byte of at
 * least 1, written back into the caller's palette, sic). Then the device
 * surface (0x4876a0, colour key 0), its texture loaded from the system one,
 * the slot counted (0x4ba6f8) and the system surface kept (0x41d940) or,
 * when the device surface is not in video memory and 0x4b86a0 asks for
 * that, the slot released (0x41d890, returns 0). Returns 1, or 0 on a
 * failure. The original reuses its argument slots as locals; not
 * reproduced (no caller reads them back). */
int32_t GAMEAPI texture_upload_body(int32_t page, int32_t w, int32_t h, uint32_t pixels, uint32_t palette,
                                    uint32_t flags)
{
    uint8_t desc[0x6c];
    uint8_t pal[0x400];
    uint32_t ck[2];
    uint32_t tex = 0, pal_obj = palette, conv = flags & 2, src = conv ? 0 : palette;
    uint32_t pw, ph, rec, i;
    int32_t slot;

    if (!w || !h)
        return 1;
    if (G32(0x4ba6f8) == 0x3f)
        return 0;
    slot = (int32_t)GCALL(0x41ec20, texture_slot_free_find)();
    if (slot == -1)
        return 0;
    if (page > GI32(0x6cce54))
        G32(0x6cce54) = (uint32_t)page;
    G32(0x6cd300 + 4 * (uint32_t)page) = (uint32_t)slot;
    pw = (uint32_t)GCALL(0x41e1a0, next_pow2)(w);
    ph = (uint32_t)GCALL(0x41e1a0, next_pow2)(h);
    if (G32(0x4b86c0))
        ph = (uint32_t)w; /* sic */
    rec = 0x4ba700 + (uint32_t)slot * 0x24;
    U32(rec, 0xc) = pw;
    U32(rec, 0x10) = ph;
    U32(rec, 0x14) = (uint32_t)page;
    for (i = 0; i < 10 && !((int32_t)pw >> i & 1); i++)
        ;
    U32(rec, 0x18) = G32(0x4bdddc + 4 * i);
    for (i = 0; i < 10 && !((int32_t)ph >> i & 1); i++)
        ;
    U32(rec, 0x1c) = G32(0x4bdddc + 4 * i);
    if (!G32(0x4b86a4))
        return 1;

    memcpy(desc, PTR(0x4bcce8), sizeof desc);
    U32(desc, 8) = ph;
    U32(desc, 0xc) = pw;
    U32(desc, 0) = 0x6c;
    U32(desc, 4) = 0x21007;
    U32(desc, 0x68) = 0x401808;
    U32(desc, 0x18) = 1;
    g_dd_result = IDirectDraw_CreateSurface(g_ddraw, desc, (uint32_t *)GMEM(rec), 0);
    if (g_dd_result)
        return 0;
    if (!conv) {
        memset(pal, 0, sizeof pal);
        if (src) {
            for (i = 0; i < 0x100; i++, src += 4) {
                if ((int32_t)i > 0 && G8(src) == 0)
                    U8(src, 0) = 1; /* sic: the caller's palette */
                pal[i * 4] = U8(src, 2);
                pal[i * 4 + 1] = U8(src, 1);
                pal[i * 4 + 2] = U8(src, 0);
            }
        }
    }
    if (!GCALL(0x4875a0, dd_surface_lock)(0, G32(rec), A(desc))) /* lock */
        return 0;
    if (conv) {
        GCALL(0x41e440, pixels_1555_convert)((const uint16_t *)PTR(pixels), w, h, (uint16_t *)PTR(U32(desc, 0x24)),
                                             (int32_t)U32(desc, 0x10), flags & 1);
    } else if (!G32(0x4bcd54)) {
        if (pixels) {
            uint32_t dst = U32(desc, 0x24), pitch = U32(desc, 0x10), row = pixels;
            int32_t rows;
            GCALL(0x41e1d0, pixels_rgb_to_screen)(pal, 0x100, flags & 1);
            for (rows = h; rows > 0; rows--) {
                uint32_t table = G32(0x4bcd7c), s = row, d = dst, n = pw;
                do {
                    U16(d, 0) = U16(table, 2 * (uint32_t)G8(s));
                    s++;
                    d += 2;
                } while (--n);
                row += pw;
                dst += pitch;
            }
        }
    } else if (pixels) {
        uint32_t dst = U32(desc, 0x24), pitch = U32(desc, 0x10), row = pixels;
        int32_t rows;
        for (rows = h; rows > 0; rows--) {
            GCALL(0x476f30, mem_copy)(PTR(dst), PTR(row), pw);
            row += pw;
            dst += pitch;
        }
    }
    GCALL(0x487610, dd_surface_unlock)(G32(rec)); /* unlock */
    if (!conv && G32(0x4bcd54)) {
        g_dd_result = IDirectDraw_CreatePalette(g_ddraw, 0x4c, pal, &pal_obj, 0);
        g_dd_result = IDirectDrawSurface_SetPalette(G32(rec), pal_obj);
        IDirectDrawPalette_Release(pal_obj);
    }
    /* IID_IDirect3DTexture2 */
    g_dd_result = IDirectDrawSurface_QueryInterface(G32(rec), GMEM(0x4a3920), &tex);
    if (g_dd_result)
        return 0;
    if (!GCALL(0x487660, dd_surface_desc_get)(A(desc), G32(rec)))
        return 0;
    U32(desc, 4) = 0x1007;
    U32(desc, 0x68) = G32(0x4b86a0) ? 0x4001000 : 0x4001800;
    if (!GCALL(0x4876a0, dd_surface_create)(A(desc), rec + 4, (int32_t)G32(0x4d5fa4)))
        return 0;
    ck[0] = 0;
    ck[1] = 0;
    IDirectDrawSurface_SetColorKey(U32(rec, 4), 8, ck); /* DDCKEY_SRCBLT */
    if (!conv && G32(0x4bcd54)) {
        memset(pal, 0, sizeof pal);
        g_dd_result = IDirectDraw_CreatePalette(g_ddraw, 0x44, pal, &pal_obj, 0);
        if (g_dd_result)
            return 0;
        g_dd_result = IDirectDrawSurface_SetPalette(U32(rec, 4), pal_obj); /* not released (sic) */
        if (g_dd_result)
            return 0;
    }
    g_dd_result = IDirectDrawSurface_QueryInterface(U32(rec, 4), GMEM(0x4a3920), (uint32_t *)GMEM(rec + 8));
    if (g_dd_result)
        return 0;
    g_dd_result = IDirect3DTexture2_Load(U32(rec, 8), GMEM(tex));
    if (g_dd_result)
        return 0;
    IDirect3DTexture2_Release(tex);
    if (!GCALL(0x487660, dd_surface_desc_get)(A(desc), U32(rec, 4)))
        return 0;
    G32(0x4ba6f8)++;
    if (!(U32(desc, 0x68) & 0x4000) && G32(0x4b86a0)) {
        GCALL(0x41d890, texture_slot_release)(slot);
        return 0;
    }
    GCALL(0x41d940, texture_slot_handle)(slot);
    return 1;
}

/* ===== 0x41f720: the Direct3D capability probe ===== */

/* A screen-filling test quad (0x41f550, 13 arguments; x87 for the size). */
static void wc_probe_quad(void)
{
    uint32_t h = wc_int_minus(G32(0x4acc3c), G32(0x4a3330));
    uint32_t w = wc_int_minus(G32(0x4acc38), G32(0x4a3330));
    GCALL(0x41f550, screen_quad_draw)(0x3f, 0, 0, w, h, 0, 0, 0, 0, 0x42c80000, 0xffffffff, 0, 0);
}

/* `n` frames: 0x41e040(0) and the flip. */
static void wc_probe_frames(int32_t n)
{
    for (; n > 0; n--) {
        GCALL(0x41e040, viewport_clear)(0);
        GCALL(0x4876f0, flip_body)();
    }
}

/* 0x41f720: the capability probe. When the device's checksum (0x41f700
 * over 0x4b8690, 16 bytes) matches the config's +0x98 the saved answers
 * stand: +0x9a selects the state (0x41f3f0), +0x9b sets 0x6cce60, and an
 * unneeded extra surface (0x6cce7c, 0x4d5fa0) is detached from the back
 * buffer and released. Otherwise it draws test quads, flips and reads
 * pixels back (0x41f400) to set +0x99 (the extra surface works; else it is
 * released), +0x9a and +0x9b (0x41fb60), then clears and flips three
 * frames and selects state 0x3f (0x420a30, argument in ebx). */
void GAMEAPI vk_probe_body(void)
{
    uint32_t sum, a, b;
    int32_t lag = 0, i;

    sum = GCALL(0x41f700, bytes_sum8)(PTR(0x4b8690), 0x10) & 0xff;
    if ((uint8_t)sum == U8(g_config, 0x98)) {
        GCALL(0x41f3f0, vertex_snap_set)( U8(g_config, 0x9a));
        G32(0x6cce60) = U8(g_config, 0x9b) == 0;
        if (G32(0x6cce7c) && U8(g_config, 0x99) && G32(0x4d5fa0)) {
            IDirectDrawSurface_DeleteAttachedSurface(g_back, 0, G32(0x4d5fa0));
            IDirectDrawSurface_Release(G32(0x4d5fa0));
            G32(0x4d5fa0) = 0;
            G32(0x4d5f98) = 0;
            G32(0x6cce7c) = 0;
        }
        return;
    }
    U8(g_config, 0x98) = (uint8_t)sum;
    U8(g_config, 0x99) = 0;
    U8(g_config, 0x9a) = 0;
    U8(g_config, 0x9b) = 0;
    IDirect3DDevice2_SetRenderState(G32(0x4b8384), 0x29, 0);
    GCALL(0x41ec40, background_colour_set)(0, 0, 0x20);
    GCALL(0x41f2d0, viewport_body)(0, 0, (int32_t)G32(0x4acc38), (int32_t)G32(0x4acc3c));
    GCALL(0x41ddd0, d3d_filter_nearest)();
    GCALL(0x41de60, d3d_fog_off)();
    GCALL(0x41dec0, d3d_zbuffer_off)();
    GCALL(0x41f3f0, vertex_snap_set)( 0);
    wc_probe_frames(3);

    a = GCALL(0x41f400, surface_pixel_read)( 1, 1, 1, 0);
    wc_probe_quad();
    for (i = 0; i < 4; i++) {
        if (GCALL(0x41f400, surface_pixel_read)( 1, 1, 1, 0) != a)
            break;
        GCALL(0x4876f0, flip_body)();
        lag++;
    }
    wc_probe_frames(lag);
    GCALL(0x41e040, viewport_clear)(0);
    a = GCALL(0x41f400, surface_pixel_read)( 1, G32(0x4acc38) - 1, 1, 0);
    wc_probe_quad();
    for (i = lag; i > 0; i--)
        GCALL(0x4876f0, flip_body)();
    b = GCALL(0x41f400, surface_pixel_read)( 1, G32(0x4acc38) - 1, 1, 0);
    if (lag < 4) {
        if (b == a)
            U8(g_config, 0x9a) = 1;
        GCALL(0x41f3f0, vertex_snap_set)( U8(g_config, 0x9a));
        if (!G32(0x6cce7c))
            goto end;
        GCALL(0x41de80, d3d_zbuffer_on)();
        wc_probe_frames(lag);
        GCALL(0x41e040, viewport_clear)(0);
        GCALL(0x41f550, screen_quad_draw)(0x3f, 0, 0, 0x43800000, 0x43800000, 0, 0, 0x3f7fbe77, 0x3f7fbe77, 0x43160000,
                            0xff0000ff, 0, 0);
        for (i = lag; i > 0; i--)
            GCALL(0x4876f0, flip_body)();
        a = GCALL(0x41f400, surface_pixel_read)( 1, 1, 1, 0);
        GCALL(0x41e040, viewport_clear)(0);
        GCALL(0x41f550, screen_quad_draw)(0x3f, 0, 0, 0x43800000, 0x43800000, 0, 0, 0x3f7fbe77, 0x3f7fbe77, 0x43160000,
                            0xff0000ff, 0, 0);
        GCALL(0x41f550, screen_quad_draw)(0x3f, 0, 0, 0x43800000, 0x43800000, 0, 0, 0x3f7fbe77, 0x3f7fbe77, 0x42c80000,
                            0xffffffff, 0, 1);
        for (i = lag; i > 0; i--)
            GCALL(0x4876f0, flip_body)();
        b = GCALL(0x41f400, surface_pixel_read)( 1, 1, 1, 0);
        GCALL(0x41dec0, d3d_zbuffer_off)();
        if (a == b) {
            U8(g_config, 0x99) = 1;
            if (G32(0x4d5fa0)) {
                IDirectDrawSurface_DeleteAttachedSurface(g_back, 0, G32(0x4d5fa0));
                IDirectDrawSurface_Release(G32(0x4d5fa0));
                G32(0x4d5fa0) = 0;
                G32(0x4d5f98) = 0;
                G32(0x6cce7c) = 0;
                goto end;
            }
        }
    }
    if (G32(0x6cce7c))
        GCALL(0x41de80, d3d_zbuffer_on)();
end:
    if (GCALL(0x41fb60, pvrc_texture_probe)())
        U8(g_config, 0x9b) = 1;
    G32(0x6cce60) = U8(g_config, 0x9b) == 0;
    GCALL(0x41ec40, background_colour_set)(0, 0, 0);
    wc_probe_frames(3);
    wc_state_select(0x3f);
}

/* ===== 0x4771c0: the game window ===== */

/* 0x4771c0(unused, x, y, w, h, fullscreen): CreateWindowExA of class
 * 0x4acc18, title 0x4acc24, hidden (ShowWindow 0). Fullscreen: a topmost
 * popup the size of the screen at 0, 0. Otherwise an overlapped window
 * whose outside fits a w x h client area (w / h 0: three quarters of the
 * screen) at x, y (0: CW_USEDEFAULT). Returns the window, or 0. */
uint32_t GAMEAPI create_window_body(uint32_t unused, int32_t x, int32_t y, int32_t w, int32_t h, int32_t fullscreen)
{
    int32_t rect[4];
    uint32_t window;

    (void)unused;
    if (!x)
        x = (int32_t)0x80000000;
    if (!y)
        y = (int32_t)0x80000000;
    if (!w)
        w = GetSystemMetrics(0) / 4 * 3;
    if (!h)
        h = GetSystemMetrics(1) / 4 * 3;
    if (fullscreen) {
        uint32_t sh = (uint32_t)GetSystemMetrics(1);
        uint32_t sw = (uint32_t)GetSystemMetrics(0);
        window = (uint32_t)(uintptr_t)CreateWindowExA(8, GMEM(0x4acc18), GMEM(0x4acc24), 0x80000000, 0, 0, (int32_t)sw,
                                                       (int32_t)sh, 0, 0, GMEM(G32(0x6c7ffc)), 0);
    } else {
        rect[0] = 0;
        rect[1] = 0;
        rect[2] = w;
        rect[3] = h;
        AdjustWindowRectEx(rect, 0xcf0000, 0, 0x300);
        window = (uint32_t)(uintptr_t)CreateWindowExA(0x300, GMEM(0x4acc18), GMEM(0x4acc24), 0xcf0000, x, y,
                                                      rect[2] - rect[0], rect[3] - rect[1], 0, 0,
                                                      GMEM(G32(0x6c7ffc)), 0);
    }
    if (!window)
        return 0;
    ShowWindow(GMEM(window), 0);
    return window;
}

/* ===== 0x4876f0: the flip ===== */

/* 0x4876f0: fullscreen (0x4d5f80): Flip(0, DDFLIP_WAIT) of the primary
 * surface, restoring both surfaces and flipping again when they were lost;
 * the result in 0x4d6f18; an error other than "lost" or "still drawing"
 * goes to the debug report 0x4291f0; returns 1 on success, else 0.
 * Windowed: the back buffer's 0, 0, 0x4d5f88 x 0x4d5f8c rectangle copied
 * to the screen (0x4878d0); returns 1. */
int32_t GAMEAPI flip_body(void)
{
    uint32_t hr;
    int32_t rect[4];

    if (!G32(0x4d5f80)) {
        SetRect(rect, 0, 0, (int32_t)G32(0x4d5f88), (int32_t)G32(0x4d5f8c));
        GCALL(0x4878d0, dd_blit)(g_back, g_front, rect[0], rect[1], rect[2], rect[3], 0, 0);
        return 1;
    }
    hr = IDirectDrawSurface_Flip(g_front, 0, 1);
    g_dd_result = hr;
    if (hr == DDERR_SURFACELOST) {
        IDirectDrawSurface_Restore(g_front);
        IDirectDrawSurface_Restore(g_back);
        hr = IDirectDrawSurface_Flip(g_front, 0, 1);
        g_dd_result = hr;
    }
    if (!hr)
        return 1;
    if (hr != DDERR_WASSTILLDRAWING && hr != DDERR_SURFACELOST)
        wc_dx_error(hr);
    return 0;
}

/* ===== 0x443b50: race_draw ===== */

/* The original's frame, as byte offsets from its esp after the prologue:
 * +0xc the view index, +0x10 the view count (later the mirror's y), +0x14
 * .. +0x33 a view's rectangles kept over its drawing (the mirror's
 * x and y in +0x10 / +0xc), +0x34 the
 * local players' car positions (12 bytes each), +0x7c .. +0x153 a car's
 * 0xd8-byte camera block. There are six car records (the view list
 * 0x6c0a40 follows them), so the positions end where the camera block
 * starts; room is left for up to 255 anyway (the original would overrun
 * its frame). */
#define RD_FRAME_BYTES (0x34 + 12 * 256)
#define RD(off) (*(uint32_t *)(frame + (off)))

/* 0x443b50: draws the race. With the race on (0x4af5ac): keeps the local
 * players' car positions (+0x34 of each car's body), draws each view
 * (0x40d040 counts them; 0x6c0a40 lists their car records): HUD and camera
 * (0x4386f0, 0x40b000), the car's view rectangle taken for the slot's,
 * then the TV camera (draw flag 4) or the listener (net games: the local
 * player's, 0x472050) and the view (0x479be0, 0x423c40), the slot's
 * rectangle put back. Then the rear-view mirror (config +0x25, not with
 * the TV camera) and the map inset (draw flag 8), and the car positions
 * put back, each car relinked in the grid (0x4611d0, 0x460fb0). */
void GAMEAPI race_draw_body(void)
{
    uint8_t frame[RD_FRAME_BYTES];
    uint32_t n, i, k, count, rec;

    if (!G32(0x4af5ac))
        return;
    memset(frame, 0, sizeof frame);
    n = U8(g_race, 0x54d);
    for (k = 0; k < n; k++) {
        uint32_t p = G32(G32(0x6bfba8 + k * 0x274)) + 0x34;
        memcpy(frame + 0x34 + 12 * k, PTR(p), 12);
    }
    count = (uint32_t)GCALL(0x40d040, view_count)();
    RD(0x10) = count;
    RD(0xc) = 0;
    g_draw_flags &= ~0x20u;
    for (i = 0; (int32_t)i < (int32_t)count; i++) {
        uint32_t slot = 0x6bfb88 + i * 0x274, list = 0x6c0a40 + i * 4;
        memcpy(frame + 0x14, PTR(slot), 16);
        memcpy(frame + 0x24, PTR(slot + 0x10), 16);
        GCALL(0x4386f0, hud_update)((uint32_t *)PTR(CAR_RECORD(G32(list))));
        rec = CAR_RECORD(G32(list));
        GCALL(0x40b000, camera_speed_zoom)(PTR(rec), (int32_t *)PTR(G32(U32(rec, 0x28)) + 0x34));
        rec = CAR_RECORD(G32(list));
        memcpy(PTR(slot), PTR(rec + 8), 16);
        memcpy(PTR(slot + 0x10), PTR(rec + 0x18), 16);
        if (G8(0x6cce40) & 4) {
            GCALL(0x408270, tv_camera_begin)((int32_t)RD(0xc));
            GCALL(0x479be0, draw_view_body)(PTR(slot - 8));
            GCALL(0x423c40, ot_frame_flush)();
            GCALL(0x40ad60, tv_camera_end)();
        } else {
            if (GCALL(0x45ef80, net_state_game)()) {
                int32_t me = GCALL(0x48cef0, local_car_index)();
                if (me > 0 && me < (int32_t)U8(g_race, 0x54d)) {
                    uint32_t r = CAR_RECORD(me), car = U32(r, 0);
                    GCALL(0x472050, sound_listener_set)(0, (const uint32_t *)PTR(car + 0x34), (const uint32_t *)PTR(car + 0x68),
                                                        (const uint32_t *)PTR(G32(U32(r, 0x28)) + 0xc4));
                }
            }
            GCALL(0x479be0, draw_view_body)(PTR(CAR_RECORD(G32(list))));
            GCALL(0x423c40, ot_frame_flush)();
        }
        memcpy(PTR(slot), frame + 0x14, 16);
        memcpy(PTR(slot + 0x10), frame + 0x24, 16);
        RD(0xc) = i + 1;
    }

    if (!(G8(0x6cce40) & 4) && U8(g_config, 0x25)) {
        int32_t sw = g_screen_w, sh = g_screen_h;
        uint32_t mw = (uint32_t)(sw / 5 * 2) & ~1u, mh, keep8, keepc;
        RD(0x10) = (uint32_t)((sw - (int32_t)mw) >> 1);
        mh = (uint32_t)(sh / 8) & ~1u;
        RD(0xc) = (uint32_t)((sh - (int32_t)mh) >> 5);
        rec = CAR_RECORD(G32(0x6c0a40));
        memcpy(frame + 0x7c, PTR(G32(U32(rec, 0x28)) + 0x34), 0xd8);
        keep8 = U32(rec, 8);
        keepc = U32(rec, 0xc);
        RD(0x1c) = U32(rec, 0x10);
        RD(0x20) = U32(rec, 0x14);
        GCALL(0x428a80, projection_push)();
        GCALL(0x423f00, view_zoom_set)(0x68);
        g_draw_flags |= 0x20;
        rec = CAR_RECORD(G32(0x6c0a40));
        U32(rec, 8) = RD(0x10);
        U32(rec, 0xc) = RD(0xc);
        U32(rec, 0x10) = mw;
        U32(rec, 0x14) = mh;
        GCALL(0x40d090, car_view)((uint8_t **)PTR(rec), (int32_t *)PTR(G32(U32(rec, 0x28)) + 0x34));
        GCALL(0x479be0, draw_view_body)(PTR(CAR_RECORD(G32(0x6c0a40))));
        GCALL(0x423c40, ot_frame_flush)();
        rec = CAR_RECORD(G32(0x6c0a40));
        U32(rec, 8) = keep8;
        U32(rec, 0xc) = keepc;
        U32(rec, 0x10) = RD(0x1c);
        U32(rec, 0x14) = RD(0x20);
        memcpy(PTR(G32(U32(rec, 0x28)) + 0x34), frame + 0x7c, 0xd8);
        g_draw_flags &= ~0x20u;
        GCALL(0x428b00, projection_pop)();
    }

    {
        uint32_t f = g_draw_flags;
        if (!(f & 4) && (f & 8)) {
            int32_t sw = g_screen_w, q = sw >> 2;
            uint32_t k8 = G32(0x6bfb88), kc = G32(0x6bfb8c), k10 = G32(0x6bfb90), k14 = G32(0x6bfb94);
            G32(0x6bfb88) = (uint32_t)(sw - q - 10);
            G32(0x6bfb8c) = (uint32_t)((g_screen_h - q) >> 1);
            G32(0x6bfb90) = (uint32_t)q;
            G32(0x6bfb94) = (uint32_t)q;
            g_draw_flags = f | 0x20;
            GCALL(0x408270, tv_camera_begin)(0);
            GCALL(0x479be0, draw_view_body)(PTR(0x6bfb80));
            GCALL(0x423c40, ot_frame_flush)();
            GCALL(0x40ad60, tv_camera_end)();
            G32(0x6bfb88) = k8;
            G32(0x6bfb8c) = kc;
            G32(0x6bfb90) = k10;
            G32(0x6bfb94) = k14;
            g_draw_flags &= ~0x20u;
        }
    }

    for (k = 0; k < U8(g_race, 0x54d); k++) {
        uint32_t p = G32(G32(0x6bfba8 + k * 0x274)) + 0x34;
        memcpy(PTR(p), frame + 0x34 + 12 * k, 12);
        GCALL(0x4611d0, object_grid_unlink)(PTR(G32(G32(0x6bfba8 + k * 0x274))));
        GCALL(0x460fb0, object_grid_link)(PTR(G32(G32(0x6bfba8 + k * 0x274))));
    }
}

/* ===== 0x471c90 / 0x471da0: starting sounds ===== */

/* The voice record of voice v (0x5ff4a0, 0x30 bytes each). */
#define VOICE(v) (0x5ff4a0u + (uint32_t)(v) * 0x30u)

/* 0x471c90 sound_play(sound, kind, pitch, vol, world): a device voice for
 * `sound` (0x472920, 0x4d459c the near flag); unless 0x4ac984 is set or no
 * voice was got (0x7f), its record is reset (sound, world pointer, states
 * 0x7f) and started (0x471eb0: both levels `vol`; flag 0x20 with a world
 * position). The voice goes into the low byte of the `pitch` argument
 * slot, which is passed on whole as 0x471eb0's voice (sic). Returns the
 * voice. */
uint32_t GAMEAPI sound_play_body(uint32_t sound, uint32_t kind, uint32_t pitch, uint32_t vol, uint32_t world)
{
    uint32_t got = wc_voice_alloc(sound, kind, (uint32_t)(int16_t)vol, (uint32_t)(int16_t)pitch, G32(0x4d459c));
    uint32_t voice = (pitch & 0xffffff00u) | (got & 0xff), r;

    if (G32(0x4ac984))
        return got & 0xff;
    if ((uint8_t)got == 0x7f)
        return voice & 0xff;
    r = VOICE(voice & 0xff);
    U8(r, 7) = (uint8_t)sound;
    U32(r, 0x1c) = 0;
    U32(r, 0x20) = 0;
    U8(r, 3) = 0x7f;
    U8(r, 0x2c) = 0x7f;
    U8(r, 0x2d) = 0x7f;
    U32(r, 0x24) = world;
    U32(r, 0x28) = 0;
    wc_voice_start(voice, kind, vol, vol, pitch, world ? 0x20 : 0);
    return voice & 0xff;
}

/* 0x471da0(unused, sound, kind, pitch, vol, world, velocity): as
 * sound_play for a sound that follows `world` and `velocity` (flag 2; the
 * voice in the low byte of the `pitch` slot again). Returns
 * 0x472920's eax when no voice starts, else the voice in al over the upper
 * bits 0x471eb0 left in eax (callers keep only al). */
uint32_t GAMEAPI sound_moving_body(uint32_t unused, uint32_t sound, uint32_t kind, uint32_t pitch, uint32_t vol,
                                   uint32_t world, uint32_t velocity)
{
    uint32_t got = wc_voice_alloc(sound, kind, vol & 0xffff, pitch & 0xffff, G32(0x4d459c));
    uint32_t voice = (pitch & 0xffffff00u) | (got & 0xff), r, eax;

    (void)unused;
    if (G32(0x4ac984) || (uint8_t)got == 0x7f)
        return got;
    r = VOICE(voice & 0xff);
    U32(r, 0x1c) = 0;
    U32(r, 0x20) = 0;
    U8(r, 3) = 0x7f;
    U8(r, 0x2c) = 0x7f;
    U8(r, 0x2d) = 0x7f;
    U32(r, 0x24) = world;
    U32(r, 0x28) = velocity;
    U8(r, 7) = (uint8_t)sound;
    eax = wc_voice_start(voice, kind, vol, vol, pitch, 2);
    return (eax & 0xffffff00u) | (voice & 0xff);
}

/* ===== 0x4979b7: the C runtime's empty function ===== */

/* 0x4979b7: a lone ret, stored as _FPmtinit / _FPmtterm (0x4b4ab0 /
 * 0x4b4ab4) and read only by the original thread start 0x498df3 (ported in
 * src/game/crt_cc.c, which does not call them). */
void GAMEAPI crt_fpmt_nop(void)
{
}

/* Points those two data slots at the C no-op, so nothing in the game's
 * memory refers to 0x4979b7 any more (called at start-up and restart,
 * after the data section is loaded). */
void crt_fpmt_patch(void)
{
    if (G32(0x4b4ab0) == 0x4979b7)
        G32(0x4b4ab0) = A(crt_fpmt_nop);
    if (G32(0x4b4ab4) == 0x4979b7)
        G32(0x4b4ab4) = A(crt_fpmt_nop);
}
