/* Group PT: the original's still-reachable functions in 0x487000-0x489cd0
 * (the DirectDraw display file and the DirectInput file).
 *
 *  - DirectDraw: the driver list (DirectDrawEnumerateA with its callback
 *    0x487a80, which Ghidra never made a function: per driver the name,
 *    description, GUID, 3D flag and video memory in the 0x224-byte records
 *    at 0x4d6464), the device object (0x487020), the display modes
 *    (EnumDisplayModes with its callback 0x487190, also not a Ghidra
 *    function; the 50-entry table at 0x4d5fe4 and its search 0x4870d0), the
 *    two display start-ups (0x487e10 for the 3D game, 0x4885d0 for the
 *    movies / front end: cooperative level, display mode, primary and back
 *    surfaces, clipper, palette), the shut-down 0x487210, the surface
 *    helpers (lock / unlock / description / create / blit / clear) and the
 *    pixel format of the back buffer (0x487480 / 0x487300);
 *  - DirectInput: start-up (0x488d90), the joystick enumeration callback
 *    0x488e80 and its device creation 0x488e20, the keyboard 0x489910,
 *    joystick properties (0x489180 / 0x4891f0 / 0x489500), the force
 *    feedback effects (created by 0x4892b0; set / started by 0x489980,
 *    0x489ab0, 0x489bc0, 0x489cd0; stopped by 0x489a80 / 0x489b90 /
 *    0x489ca0) and the shut-down 0x489560.
 *
 * Windows calls go through the original's import table (the DirectX
 * entry points through their slots 0x4a3024 / 0x4a3028 / 0x4a3030, which
 * the original reaches through its jmp thunks 0x4910f0 / 0x4910f6 /
 * 0x4910fc), COM calls through the object's vtable. The enumeration
 * callbacks are handed over by their original addresses (0x487190,
 * 0x487a80, 0x488e80, 0x4890b0), which the hooks route to the C; the data
 * formats 0x491b50 / 0x492b70 and the GUIDs stay the original's data.
 * Everything outside the group is called by address. */
#include "../runtime.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

#define PT_STD __attribute__((stdcall))

typedef int32_t(GAMEAPI *pt_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *pt_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *pt_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *pt_fn5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef int32_t(PT_STD *pt_std1)(uint32_t a);
typedef int32_t(PT_STD *pt_std2)(uint32_t a, uint32_t b);
typedef int32_t(PT_STD *pt_std3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(PT_STD *pt_std4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(PT_STD *pt_std5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef int32_t(PT_STD *pt_std6)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f);

#define CALL1(a, x) (((pt_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((pt_fn2)(a))(A(x), A(y)))
#define CALL3(a, x, y, z) (((pt_fn3)(a))(A(x), A(y), A(z)))
#define CALL5(a, x, y, z, w, v) (((pt_fn5)(a))(A(x), A(y), A(z), A(w), A(v)))

/* COM methods: slot off of the object's vtable (stdcall, this first). */
#define PT_VT(obj, off) (*(uint32_t *)(uintptr_t)(*(uint32_t *)(uintptr_t)(obj) + (off)))
#define COM1(o, off) (((pt_std1)PT_VT(o, off))(A(o)))
#define COM2(o, off, a) (((pt_std2)PT_VT(o, off))(A(o), A(a)))
#define COM3(o, off, a, b) (((pt_std3)PT_VT(o, off))(A(o), A(a), A(b)))
#define COM4(o, off, a, b, c) (((pt_std4)PT_VT(o, off))(A(o), A(a), A(b), A(c)))
#define COM5(o, off, a, b, c, d) (((pt_std5)PT_VT(o, off))(A(o), A(a), A(b), A(c), A(d)))
#define COM6(o, off, a, b, c, d, e) (((pt_std6)PT_VT(o, off))(A(o), A(a), A(b), A(c), A(d), A(e)))

/* Imports (stdcall) through the original's import slots. */
#define pt_DirectDrawCreate(g, out, outer) ((*(pt_std3 *)0x4a3024)(A(g), A(out), A(outer)))
#define pt_DirectDrawEnumerateA(cb, ctx) ((*(pt_std2 *)0x4a3028)(A(cb), A(ctx)))
#define pt_DirectInputCreateA(h, v, out, outer) ((*(pt_std4 *)0x4a3030)(A(h), A(v), A(out), A(outer)))
#define pt_SetFocus(w) ((*(pt_std1 *)0x4a327c)(A(w)))
#define pt_ShowWindow(w, c) ((*(pt_std2 *)0x4a329c)(A(w), A(c)))
#define pt_ClientToScreen(w, pt) ((*(pt_std2 *)0x4a323c)(A(w), A(pt)))
#define pt_SetRect(r, l, t, rr, b) ((*(pt_std5 *)0x4a3254)(A(r), A(l), A(t), A(rr), A(b)))
#define pt_CreateICA(d, n, o, m) ((*(pt_std4 *)0x4a3058)(A(d), A(n), A(o), A(m)))
#define pt_GetDeviceCaps(h, i) ((*(pt_std2 *)0x4a3048)(A(h), A(i)))
#define pt_DeleteDC(h) ((*(pt_std1 *)0x4a304c)(A(h)))
#define pt_SendDlgItemMessageA(d, i, m, w, l) ((*(pt_std5 *)0x4a3290)(A(d), A(i), A(m), A(w), A(l)))

/* Outside the group, by address. */
#define mem_copy_at(d, s, n) CALL3(0x476f30, d, s, n)
#define mem_fill_at(p, v, n) CALL3(0x476f50, p, v, n)
#define dx_error_at(hr) CALL1(0x4291f0, hr) /* an empty debug hook */
#define d3d_window_prepare_at(a, b, c, d, e) CALL5(0x428a50, a, b, c, d, e)
#define global_block_alloc_at(n, f) CALL2(0x476ae0, n, f)
#define arena_alloc_low_at(n) CALL1(0x476b80, n)
#define joy_effects_release_at(j) CALL1(0x489f70, j)

/* This group's functions (also in build/groups/pt_protos.txt). */
void GAMEAPI dd_surfaces_clear(void); /* 0x487000 */
int32_t GAMEAPI dd_create(int32_t dev); /* 0x487020 */
int32_t GAMEAPI dd_mode_find(int32_t w, int32_t h, int32_t bpp); /* 0x4870d0 */
int32_t GAMEAPI dd_modes_enum(void); /* 0x487160 */
int32_t PT_STD dd_mode_enum_cb(uint32_t desc, uint32_t ctx); /* 0x487190 */
void GAMEAPI dd_shutdown(void); /* 0x487210 */
int32_t GAMEAPI dd_mask_loss(uint32_t mask); /* 0x487300 */
void GAMEAPI dd_pixel_format_get(uint32_t desc, uint32_t out); /* 0x487480 */
int32_t GAMEAPI dd_surfaces_release(void); /* 0x487570 */
int32_t GAMEAPI dd_surface_lock(uint32_t rect, uint32_t surf, uint32_t desc); /* 0x4875a0 */
int32_t GAMEAPI dd_surface_unlock(uint32_t surf); /* 0x487610 */
int32_t GAMEAPI dd_surface_desc_get(uint32_t desc, uint32_t surf); /* 0x487660 */
int32_t GAMEAPI dd_surface_create(uint32_t desc, uint32_t out, int32_t sysmem); /* 0x4876a0 */
int32_t GAMEAPI dd_window_origin(uint32_t hwnd, int32_t w, int32_t h); /* 0x4877d0 */
int32_t GAMEAPI dd_primary_clear(void); /* 0x487830 */
int32_t GAMEAPI dd_blit(uint32_t src, uint32_t dst, int32_t l, int32_t t, int32_t r, int32_t b, int32_t x, int32_t y); /* 0x4878d0 */
int32_t PT_STD dd_driver_enum_cb(uint32_t guid, uint32_t desc, uint32_t name, uint32_t ctx); /* 0x487a80 */
int32_t GAMEAPI dd_drivers_enum(void); /* 0x487d50 */
int32_t GAMEAPI dd_probe(void); /* 0x487d70 */
int32_t GAMEAPI dd_start_3d(int32_t full, int32_t flip, int32_t w, int32_t h, int32_t d3d, int32_t no3d_hw, int32_t bpp); /* 0x487e10 */
void GAMEAPI dd_driver_list_fill(uint32_t dlg, uint32_t id); /* 0x4883c0 */
int32_t GAMEAPI dd_start_2d(int32_t full, int32_t flip, int32_t w, int32_t h, int32_t d3d, int32_t half, int32_t bpp); /* 0x4885d0 */
int32_t GAMEAPI input_init(void); /* 0x488d90 */
int32_t GAMEAPI di_device_create(uint32_t di, uint32_t guid, uint32_t out, uint32_t outer); /* 0x488e20 */
int32_t PT_STD input_enum_joystick(uint32_t inst, uint32_t ctx); /* 0x488e80 */
void GAMEAPI joy_deadzone_set(int32_t joy, int32_t value, uint32_t obj); /* 0x489180 */
void GAMEAPI joy_range_set(int32_t joy, uint32_t obj); /* 0x4891f0 */
void GAMEAPI ff_effect_defaults(uint32_t e); /* 0x489250 */
void GAMEAPI input_create_ff_effects(int32_t joy); /* 0x4892b0 */
int32_t GAMEAPI joy_property_get(int32_t joy, uint32_t buf, uint32_t prop, uint32_t obj); /* 0x489500 */
void GAMEAPI input_shutdown(void); /* 0x489560 */
int32_t GAMEAPI input_init_keyboard(void); /* 0x489910 */
void GAMEAPI ff_road_set(uint32_t joy, int32_t angle, int32_t mag); /* 0x489980 */
void GAMEAPI ff_road_stop(int32_t joy); /* 0x489a80 */
void GAMEAPI ff_spring_a_set(uint32_t joy, int32_t v); /* 0x489ab0 */
void GAMEAPI ff_spring_a_stop(int32_t joy); /* 0x489b90 */
void GAMEAPI ff_spring_b_set(uint32_t joy, int32_t v); /* 0x489bc0 */
void GAMEAPI ff_spring_b_stop(int32_t joy); /* 0x489ca0 */
void GAMEAPI ff_engine_set(uint32_t joy, int32_t period, int32_t mag); /* 0x489cd0 */

#define DDERR_SURFACELOST 0x887601c2u

/* Globals. */
#define g_hwnd G32(0x6c8000)
#define g_hinstance G32(0x6c7ffc)
#define g_screen_w G32(0x4acc38)
#define g_screen_h G32(0x4acc3c)
#define g_screen_bpp G32(0x4acc40)
#define g_dd G32(0x4d5f40)        /* IDirectDraw */
#define g_clipper G32(0x4d5f44)
#define g_primary G32(0x4d5f48)
#define g_back G32(0x4d5f4c)
#define g_bpp G32(0x4d5f70)
#define g_palette_bits G32(0x4d5f74)
#define g_palette G32(0x4d5f78)
#define g_flip G32(0x4d5f80)       /* back buffer attached to a flipping primary */
#define g_fullscreen G32(0x4d5f84)
#define g_mode_set G32(0x4be3e4)
#define g_dd_result G32(0x4d6f18)
#define g_mode_count GI32(0x4d5fd4)
#define g_driver_count GI32(0x4d623c)
#define g_di G32(0x4d6f30)        /* IDirectInput */
#define g_keyboard G32(0x4d6f34)
#define g_di_result G32(0x4d6f2c)
#define g_joy_count GI32(0x4d9554)

#define DRIVER(n) (0x4d6464u + (uint32_t)(n) * 0x224u) /* driver records */
#define JOY(n) (0x4d7374u + (uint32_t)(n) * 0x43cu)    /* joystick records */
#define JOY_DEV(n) G32(JOY(n) + 0x244)                  /* IDirectInputDevice2 */

/* 0x487000: the five texture surface slots 0x4d5f28..0x4d5f38 = 0. */
void GAMEAPI dd_surfaces_clear(void)
{
    G32(0x4d5f28) = 0;
    G32(0x4d5f2c) = 0;
    G32(0x4d5f30) = 0;
    G32(0x4d5f34) = 0;
    G32(0x4d5f38) = 0;
}

/* 0x487020: releases the DirectDraw object and creates it for driver dev
 * (0 or less: the primary display; the driver record is copied to 0x4d6240,
 * a later driver also sets full screen). 1 = done. */
int32_t GAMEAPI dd_create(int32_t dev)
{
    int32_t hr;
    if (g_dd) {
        COM1(g_dd, 8);
        g_dd = 0;
    }
    if (dev > 0) {
        g_fullscreen = 1;
        mem_copy_at(0x4d6240, DRIVER(dev), 0x224);
        hr = pt_DirectDrawCreate(DRIVER(dev) + 0x208, 0x4d5f40, 0);
    } else {
        mem_copy_at(0x4d6240, DRIVER(0), 0x224);
        hr = pt_DirectDrawCreate(0, 0x4d5f40, 0);
    }
    g_dd_result = (uint32_t)hr;
    if (hr) {
        dx_error_at(hr);
        return 0;
    }
    return 1;
}

/* 0x4870d0: finds w x h x bpp (15 counts as 16) in the mode table
 * (0x4d5fe4, 12 bytes each); found: 0x4d5fd8..0x4d5fe0 = the mode,
 * 0x4d5f70 = bpp, 1. Else 0x4d5f70 = 0 and 0. */
int32_t GAMEAPI dd_mode_find(int32_t w, int32_t h, int32_t bpp)
{
    int32_t n = g_mode_count;
    if (bpp == 15)
        bpp = 16;
    g_bpp = 0;
    for (int32_t i = 0; i < n; i++) {
        uint32_t rec = 0x4d5fe4 + (uint32_t)i * 12;
        if (I32(rec, 0) == w && I32(rec, 4) == h && I32(rec, 8) == bpp) {
            G32(0x4d5fd8) = U32(rec, 0);
            G32(0x4d5fdc) = U32(rec, 4);
            g_bpp = (uint32_t)bpp;
            G32(0x4d5fe0) = U32(rec, 8);
            return 1;
        }
    }
    return 0;
}

/* 0x487160: empties the mode table and enumerates the display modes
 * (EnumDisplayModes, refresh rates, callback 0x487190). 1 = done. */
int32_t GAMEAPI dd_modes_enum(void)
{
    uint32_t dd = g_dd;
    int32_t hr;
    g_mode_count = 0;
    hr = COM5(dd, 0x20, 2, 0, 0, 0x487190);
    g_dd_result = (uint32_t)hr;
    return hr == 0;
}

/* 0x487190 (not a Ghidra function): EnumDisplayModes callback; modes of 15
 * bits or more go into the table (width, height, bits; 15 as 16), up to
 * 50. 0 = stop. */
int32_t PT_STD dd_mode_enum_cb(uint32_t desc, uint32_t ctx)
{
    int32_t n = g_mode_count;
    (void)ctx;
    if (n >= 50)
        return 0;
    if (U32(desc, 0x54) >= 15) { /* the count is read again for each store (sic) */
        U32(0x4d5fe4 + (uint32_t)g_mode_count * 12, 0) = U32(desc, 0xc);
        U32(0x4d5fe4 + (uint32_t)g_mode_count * 12, 4) = U32(desc, 8);
        U32(0x4d5fe4 + (uint32_t)g_mode_count * 12, 8) = U32(desc, 0x54);
        if (U32(0x4d5fe4 + (uint32_t)g_mode_count * 12, 8) == 15)
            U32(0x4d5fe4 + (uint32_t)g_mode_count * 12, 8) = 16;
        g_mode_count = g_mode_count + 1;
    }
    return 1;
}

/* 0x487210: display shut-down: palette, clipper, display mode, texture
 * surfaces (0x487570), back buffer (unless attached), primary, normal
 * cooperative level, DirectDraw. */
void GAMEAPI dd_shutdown(void)
{
    g_palette_bits = 0;
    if (!g_dd)
        return;
    if (g_palette) {
        COM2(g_primary, 0x7c, 0);
        COM2(g_back, 0x7c, 0);
        COM1(g_palette, 8);
        g_palette = 0;
    }
    if (g_clipper) {
        COM2(g_primary, 0x70, 0);
        COM1(g_clipper, 8);
        g_clipper = 0;
    }
    if (g_fullscreen) {
        COM1(g_dd, 0x4c);
        g_mode_set = 0;
    }
    dd_surfaces_release();
    if (g_flip == 0 && g_back) {
        COM1(g_back, 8);
        g_back = 0;
    }
    if (g_primary) {
        COM1(g_primary, 8);
        g_primary = 0;
    }
    COM3(g_dd, 0x50, g_hwnd, 8);
    COM1(g_dd, 8);
    g_dd = 0;
}

/* 0x487300: bits a colour channel loses against 8: 8 - width of a mask
 * shifted down to bit 0 (1 -> 7 ... 0xff -> 0; anything else 8). */
int32_t GAMEAPI dd_mask_loss(uint32_t mask)
{
    switch (mask) {
    case 1:
        return 7;
    case 3:
        return 6;
    case 7:
        return 5;
    case 0xf:
        return 4;
    case 0x1f:
        return 3;
    case 0x3f:
        return 2;
    case 0x7f:
        return 1;
    case 0xff:
        return 0;
    default:
        return 8;
    }
}

/* One channel: the mask's shift (desc's mask shifted down in place, sic)
 * and its loss. */
static void pt_channel(uint32_t desc, uint32_t off, uint32_t out, uint32_t at)
{
    uint8_t shift = 0;
    while ((U8(desc, off) & 1) == 0) {
        U32(desc, off) >>= 1;
        shift++;
    }
    U8(out, at + 1) = shift;
    U8(out, at) = (uint8_t)dd_mask_loss(U32(desc, off));
}

/* 0x487480: pixel format record (0x4d5f50) from a surface description's
 * masks (+0x58 red, +0x5c green, +0x60 blue, +0x64 alpha): per channel
 * loss byte, shift byte, mask. The description's masks are left shifted
 * down (sic). A zero colour mask never ends (as the original). */
void GAMEAPI dd_pixel_format_get(uint32_t desc, uint32_t out)
{
    uint32_t a;
    U32(out, 4) = U32(desc, 0x58);
    U32(out, 0xc) = U32(desc, 0x5c);
    U32(out, 0x14) = U32(desc, 0x60);
    pt_channel(desc, 0x58, out, 0);
    pt_channel(desc, 0x5c, out, 8);
    pt_channel(desc, 0x60, out, 0x10);
    a = U32(desc, 0x64);
    if (a == 0) {
        U32(out, 0x1c) = 0;
        U8(out, 0x19) = 0;
        U8(out, 0x18) = 0;
        return;
    }
    U32(out, 0x1c) = a;
    pt_channel(desc, 0x64, out, 0x18);
}

/* 0x487570: releases the texture surfaces 0x4d5f28..0x4d5f38. 1. */
int32_t GAMEAPI dd_surfaces_release(void)
{
    for (uint32_t p = 0x4d5f28; p < 0x4d5f3c; p += 4) {
        if (G32(p))
            COM1(G32(p), 8);
        G32(p) = 0;
    }
    return 1;
}

/* 0x4875a0: Lock (wait, write-only) of rect on surf into desc (size set
 * first); a lost surface is restored and locked again. 1 = locked. */
int32_t GAMEAPI dd_surface_lock(uint32_t rect, uint32_t surf, uint32_t desc)
{
    int32_t hr;
    U32(desc, 0) = 0x6c;
    if (!surf)
        return 0;
    hr = COM5(surf, 0x64, rect, desc, 0x801, 0);
    g_dd_result = (uint32_t)hr;
    if (hr == 0)
        return 1;
    if ((uint32_t)hr == DDERR_SURFACELOST) {
        COM1(surf, 0x6c);
        hr = COM5(surf, 0x64, rect, desc, 0x801, 0);
        g_dd_result = (uint32_t)hr;
        if (hr == 0)
            return 1;
    }
    return 0;
}

/* 0x487610: Unlock(surf, 0); a lost surface is restored and unlocked
 * again. 1 = done. */
int32_t GAMEAPI dd_surface_unlock(uint32_t surf)
{
    int32_t hr = COM2(surf, 0x80, 0);
    g_dd_result = (uint32_t)hr;
    if (hr == 0)
        return 1;
    if ((uint32_t)hr == DDERR_SURFACELOST) {
        COM1(surf, 0x6c);
        hr = COM2(surf, 0x80, 0);
        g_dd_result = (uint32_t)hr;
        if (hr == 0)
            return 1;
    }
    return 0;
}

/* 0x487660: surf's description into desc (cleared, size 0x6c). 1 = done. */
int32_t GAMEAPI dd_surface_desc_get(uint32_t desc, uint32_t surf)
{
    int32_t hr;
    mem_fill_at(desc, 0, 0x6c);
    U32(desc, 0) = 0x6c;
    hr = COM2(surf, 0x58, desc);
    g_dd_result = (uint32_t)hr;
    if (hr) {
        dx_error_at(hr);
        return 0;
    }
    return 1;
}

/* 0x4876a0: CreateSurface(desc) into *out; sysmem adds DDSCAPS_SYSTEMMEMORY
 * (0x800) to the caps. 1 = done. */
int32_t GAMEAPI dd_surface_create(uint32_t desc, uint32_t out, int32_t sysmem)
{
    int32_t hr;
    if (sysmem)
        U32(desc, 0x68) |= 0x800;
    hr = COM4(g_dd, 0x18, desc, out, 0);
    g_dd_result = (uint32_t)hr;
    if (hr) {
        dx_error_at(hr);
        return 0;
    }
    return 1;
}

/* 0x4877d0: the window's screen origin (0x4d5f90, client (0, 0) through
 * ClientToScreen) and the view size 0x4d5f88 / 0x4d5f8c = w, h; with the
 * display mode set the origin stays 0 and the size is the screen's.
 * Returns the width stored. */
int32_t GAMEAPI dd_window_origin(uint32_t hwnd, int32_t w, int32_t h)
{
    G32(0x4d5f94) = 0;
    G32(0x4d5f90) = 0;
    if (g_mode_set) {
        G32(0x4d5f88) = g_screen_w;
        G32(0x4d5f8c) = g_screen_h;
        return (int32_t)g_screen_w;
    }
    pt_ClientToScreen(hwnd, 0x4d5f90);
    G32(0x4d5f88) = (uint32_t)w;
    G32(0x4d5f8c) = (uint32_t)h;
    return w;
}

/* 0x487830: colour fill (0, wait) of the whole primary surface. 1 = done
 * or no primary. */
int32_t GAMEAPI dd_primary_clear(void)
{
    uint32_t rect[4];
    uint32_t fx[25];
    uint32_t desc[27];
    int32_t hr;
    if (!g_primary)
        return 1;
    if (!dd_surface_desc_get(A(desc), g_primary))
        return 0;
    mem_fill_at(fx, 0, 100);
    fx[0] = 100;
    pt_SetRect(rect, 0, 0, desc[3], desc[2]);
    hr = COM6(g_primary, 0x14, rect, 0, 0, 0x1000400, fx);
    g_dd_result = (uint32_t)hr;
    if (hr)
        return 0;
    return 1;
}

/* 0x4878d0: copies (l, t)-(r, b) of src to (x, y) of dst, clipped to the
 * screen size: full screen BltFast (wait), windowed Blt (the primary's
 * position through ClientToScreen); a lost surface is restored on both
 * and the copy tried again. 1 = done or nothing to copy. */
int32_t GAMEAPI dd_blit(uint32_t src, uint32_t dst, int32_t l, int32_t t, int32_t r, int32_t b, int32_t x, int32_t y)
{
    int32_t sw = (int32_t)g_screen_w, sh = (int32_t)g_screen_h;
    int32_t right, bottom, hr;
    int32_t srect[4], drect[4], pt[2];
    if (x >= sw || y >= sh)
        return 1;
    right = r - l + x;
    if (right <= 0)
        return 1;
    bottom = b - t + y;
    if (bottom <= 0)
        return 1;
    if (right > sw)
        r = sw - x + l;
    if (bottom > sh)
        b = sh - y + t;
    srect[0] = l;
    srect[1] = t;
    srect[2] = r;
    srect[3] = b;
    if (g_fullscreen) {
        hr = COM6(dst, 0x1c, x, y, src, srect, 0x10);
        g_dd_result = (uint32_t)hr;
        if ((uint32_t)hr == DDERR_SURFACELOST) {
            COM1(src, 0x6c);
            COM1(dst, 0x6c);
            hr = COM6(dst, 0x1c, x, y, src, srect, 0x10);
            g_dd_result = (uint32_t)hr;
        }
        return hr == 0;
    }
    pt[0] = x;
    pt[1] = y;
    if (dst == g_primary)
        pt_ClientToScreen(g_hwnd, pt);
    drect[0] = pt[0];
    drect[1] = pt[1];
    drect[2] = pt[0] - l + r;
    drect[3] = pt[1] - t + b;
    hr = COM6(dst, 0x14, drect, src, srect, 0x1000200, 0);
    g_dd_result = (uint32_t)hr;
    if ((uint32_t)hr == DDERR_SURFACELOST) {
        COM1(src, 0x6c);
        COM1(dst, 0x6c);
        hr = COM6(dst, 0x14, drect, src, srect, 0x1000200, 0);
        g_dd_result = (uint32_t)hr;
    }
    return hr == 0;
}

/* A C string copy (the original's inline strcpy). */
static void pt_strcpy(uint32_t dst, uint32_t src)
{
    uint32_t i = 0;
    do {
        U8(dst, i) = U8(src, i);
    } while (U8(src, i++));
}

/* 0x487a80 (not a Ghidra function): DirectDrawEnumerateA callback. A
 * driver with a GUID only while full screen is wanted; its DirectDraw
 * object gives the 3D flag (+0x218, HAL caps bit 0; only when GetCaps
 * succeeds), total video memory (+0x21c) and texture memory (+0x220,
 * IDirectDraw2 GetAvailableVidMem). A 3D driver is skipped when 0x4d5fa4
 * is set. The record (0x224 at 0x4d6464 + n): name, description, GUID (0
 * for the primary). Always 1 (go on). */
int32_t PT_STD dd_driver_enum_cb(uint32_t guid, uint32_t desc, uint32_t name, uint32_t ctx)
{
    uint8_t hal[0x17c], hel[0x17c];
    uint32_t dd = 0, dd2 = 0, total = 0, scaps[1], free_mem = 0;
    int32_t hr;
    uint32_t rec;
    (void)ctx;
    if (guid && !g_fullscreen)
        return 1;
    if (pt_DirectDrawCreate(guid, &dd, 0))
        return 1;
    mem_fill_at(hal, 0, 0x17c);
    mem_fill_at(hel, 0, 0x17c);
    U32(hal, 0) = 0x17c;
    U32(hel, 0) = 0x17c;
    if (COM3(dd, 0x2c, hal, hel) == 0)
        U32(DRIVER(g_driver_count), 0x218) = (U8(hal, 4) & 1) ? 1 : 0;
    U32(DRIVER(g_driver_count), 0x21c) = U32(hal, 0x3c);
    U32(DRIVER(g_driver_count), 0x220) = 0;
    hr = COM3(dd, 0, 0x4a3540, &dd2);
    g_dd_result = (uint32_t)hr;
    if (hr == 0) {
        scaps[0] = 0x1000;
        hr = COM4(dd2, 0x5c, scaps, &total, &free_mem);
        g_dd_result = (uint32_t)hr;
        if (hr == 0)
            U32(DRIVER(g_driver_count), 0x220) = total;
        COM1(dd2, 8);
    }
    COM1(dd, 8);
    if (guid) {
        rec = DRIVER(g_driver_count);
        if (U32(rec, 0x218) && G32(0x4d5fa4))
            return 1;
        pt_strcpy(rec, name);
        pt_strcpy(DRIVER(g_driver_count) + 0x104, desc);
        mem_copy_at(DRIVER(g_driver_count) + 0x208, guid, 0x10);
    } else {
        pt_strcpy(DRIVER(g_driver_count), name);
        pt_strcpy(DRIVER(g_driver_count) + 0x104, desc);
        mem_fill_at(DRIVER(g_driver_count) + 0x208, 0, 0x10);
    }
    g_driver_count = g_driver_count + 1;
    return 1;
}

/* 0x487d50: empties the driver list and enumerates the DirectDraw drivers
 * (callback 0x487a80). Returns the result (also 0x4d6f18). */
int32_t GAMEAPI dd_drivers_enum(void)
{
    int32_t hr;
    g_driver_count = 0;
    hr = pt_DirectDrawEnumerateA(0x487a80, 0);
    g_dd_result = (uint32_t)hr;
    return hr;
}

/* 0x487d70: the set-up dialog's probe: full screen, the driver list, the
 * primary display's DirectDraw object, its caps (unused) and modes. 1 =
 * done. */
int32_t GAMEAPI dd_probe(void)
{
    uint8_t hal[0x17c], hel[0x17c];
    int32_t hr;
    g_fullscreen = 1;
    dd_surfaces_clear();
    dd_drivers_enum();
    dd_create(0);
    mem_fill_at(hal, 0, 0x17c);
    mem_fill_at(hel, 0, 0x17c);
    U32(hal, 0) = 0x17c;
    U32(hel, 0) = 0x17c;
    hr = COM3(g_dd, 0x2c, hal, hel);
    g_dd_result = (uint32_t)hr;
    if (hr) {
        dx_error_at(hr);
        return 0;
    }
    dd_modes_enum();
    return 1;
}

/* The HAL caps flags 0x4d5fb0..0x4d5fd0 (0x487e10 / 0x4885d0). */
static void pt_caps_flags(uint32_t c)
{
    G32(0x4d5fb0) = (c & 0x2000000) != 0;
    G32(0x4d5fb4) = (c & 0x40) != 0;
    G32(0x4d5fb8) = (c & 0x4000000) != 0;
    G32(0x4d5fbc) = (c & 0x80000000u) != 0;
    G32(0x4d5fc0) = (c & 1) != 0;
    G32(0x4d5fc4) = (c & 0x8000000) != 0;
    G32(0x4d5fc8) = (c & 0x400000) != 0;
    G32(0x4d5fcc) = (c & 0x800) != 0;
    G32(0x4d5fd0) = (c & 0x4000) != 0;
}

/* The windowed bits per pixel: GetDeviceCaps(BITSPIXEL) of the display. */
static void pt_desktop_bpp(void)
{
    int32_t hdc = pt_CreateICA(0x4b2c94, 0, 0, 0);
    g_bpp = (uint32_t)pt_GetDeviceCaps(hdc, 0xc);
    pt_DeleteDC(hdc);
}

/* The clipper (windowed), after the surfaces. */
static void pt_clipper(void)
{
    if (!g_fullscreen) {
        g_dd_result = (uint32_t)COM4(g_dd, 0x10, 0, 0x4d5f44, 0);
        g_dd_result = (uint32_t)COM3(g_clipper, 0x20, 0, g_hwnd);
        g_dd_result = (uint32_t)COM2(g_primary, 0x70, g_clipper);
    }
}

/* The 8-bit palette (0x400 bytes of entries at p) on both surfaces. */
static void pt_palette(uint32_t p)
{
    g_palette_bits = p;
    if (p) {
        g_dd_result = (uint32_t)COM5(g_dd, 0x14, 0x44, p, 0x4d5f78, 0);
        COM2(g_primary, 0x7c, g_palette);
        COM2(g_back, 0x7c, g_palette);
    }
}

/* 0x487e10: display start-up for the 3D game. full: full screen (else
 * windowed); flip: a flipping back buffer (full screen only; forced by
 * 0x4d6458); d3d: 3D device surfaces (and 0x428a50 first); no3d_hw ->
 * 0x4d5fa4 (skip 3D drivers); w, h, bpp: the mode wanted. Driver from the
 * settings (0x4be448), caps flags, cooperative level, mode (full screen),
 * primary (two back buffers with 0x4be44c, then one, then none) and back
 * surface; a flipping chain not in video memory is made again with one
 * back buffer. Then the pixel format, clipper, palette (8 bit). 1 = done;
 * 0 on a DirectDraw error or an 8-bit desktop for 3D with 0x4d6458. */
int32_t GAMEAPI dd_start_3d(int32_t full, int32_t flip, int32_t w, int32_t h, int32_t d3d, int32_t no3d_hw,
                            int32_t bpp)
{
    uint8_t hal[0x17c], hel[0x17c];
    uint32_t desc[27] = {0};
    uint32_t caps[1];
    uint32_t two;
    int32_t hr;
    g_flip = full ? (uint32_t)flip : 0;
    G32(0x4d5fa4) = (uint32_t)no3d_hw;
    G32(0x4d5fa8) = 0;
    g_fullscreen = (uint32_t)full;
    G32(0x4d5f7c) = 0;
    pt_SetFocus(g_hwnd);
    dd_surfaces_clear();
    dd_drivers_enum();
    if (d3d)
        d3d_window_prepare_at(G32(0x4acc44), G32(0x4be444), G32(0x4acc48), G32(0x4acc4c), G32(0x4acc58));
    pt_ShowWindow(g_hwnd, 1);
    dd_create((int32_t)(G32(0x4be448) & 0xffff));
    if (G32(0x4d6458) && g_fullscreen)
        g_flip = 1;
    mem_fill_at(hal, 0, 0x17c);
    mem_fill_at(hel, 0, 0x17c);
    U32(hal, 0) = 0x17c;
    U32(hel, 0) = 0x17c;
    hr = COM3(g_dd, 0x2c, hal, hel);
    g_dd_result = (uint32_t)hr;
    if (hr) {
        dx_error_at(hr);
        return 0;
    }
    pt_caps_flags(U32(hal, 4));
    hr = COM3(g_dd, 0x50, g_hwnd, g_fullscreen ? 0x813 : 0x808);
    g_dd_result = (uint32_t)hr;
    if (hr) {
        dx_error_at(hr);
        return 0;
    }
    if (g_fullscreen) {
        dd_modes_enum();
        dd_mode_find(w, h, bpp);
    } else {
        pt_desktop_bpp();
    }
    if (G32(0x4d6458) && d3d && g_bpp == 8)
        return 0;
    if (g_fullscreen) {
        g_screen_w = G32(0x4d5fd8);
        g_screen_h = G32(0x4d5fdc);
        g_screen_bpp = G32(0x4d5fe0);
        hr = COM4(g_dd, 0x54, g_screen_w, g_screen_h, g_screen_bpp);
        g_dd_result = (uint32_t)hr;
        if (hr)
            goto fail;
        g_mode_set = 1;
    }
    G32(0x4d5f88) = g_screen_w;
    G32(0x4d5f8c) = g_screen_h;
    two = G32(0x4be44c);
    desc[0] = 0x6c;
    for (;;) {
        if (g_flip) {
            desc[1] = 0x21; /* caps, back buffer count */
            desc[26] = d3d ? 0x2218 : 0x218;
            desc[5] = (two != 0) + 1;
        } else {
            desc[1] = 1;
            desc[26] = 0x200;
        }
        hr = COM4(g_dd, 0x18, desc, 0x4d5f48, 0);
        g_dd_result = (uint32_t)hr;
        if (hr) {
            if (G32(0x4be44c)) {
                desc[5] = 1;
                hr = COM4(g_dd, 0x18, desc, 0x4d5f48, 0);
                g_dd_result = (uint32_t)hr;
            }
            if (hr)
                goto fail;
        }
        if (g_mode_set)
            dd_primary_clear();
        if (g_flip) {
            caps[0] = 4;
            hr = COM3(g_primary, 0x30, caps, 0x4d5f4c);
            g_dd_result = (uint32_t)hr;
            if (hr)
                goto fail;
        } else {
            desc[3] = g_screen_w;
            desc[2] = g_screen_h;
            desc[26] = d3d ? 0x2040 : 0x40;
            desc[1] = 7;
            hr = COM4(g_dd, 0x18, desc, 0x4d5f4c, 0);
            g_dd_result = (uint32_t)hr;
            if (hr)
                goto fail;
        }
        desc[0] = 0x6c;
        COM2(g_back, 0x58, desc);
        if (desc[26] & 0x4000) {
            G32(0x4d5fac) = 1;
            break;
        }
        G32(0x4d5fac) = 0;
        if (g_flip == 0 || G32(0x4be44c) == 0)
            break;
        COM1(g_primary, 8);
        g_primary = 0;
        G32(0x4be44c) = 0;
        two = 0;
    }
    dd_pixel_format_get(A(desc), 0x4d5f50);
    pt_clipper();
    if (g_bpp == 8)
        pt_palette((uint32_t)global_block_alloc_at(0x400, 0));
    return 1;
fail:
    dx_error_at(hr);
    return 0;
}

/* 0x4883c0: fills the dialog's driver combo box (descriptions), selecting
 * the last 3D one, or the settings' choice (+0x432) when in range. */
void GAMEAPI dd_driver_list_fill(uint32_t dlg, uint32_t id)
{
    uint32_t sel = 0;
    int32_t n;
    pt_SendDlgItemMessageA(dlg, id, 0x14b, 0, 0);
    n = g_driver_count;
    pt_SendDlgItemMessageA(dlg, id, 0x161, n, (uint32_t)n * 0x104);
    n = g_driver_count;
    if (n > 0) {
        int32_t i = 0;
        do {
            uint32_t text = 0x4d6568 + (uint32_t)i * 0x224;
            if (U32(text, 0x114))
                sel = (uint32_t)i;
            pt_SendDlgItemMessageA(dlg, id, 0x143, 0, text);
            n = g_driver_count;
            i++;
        } while (i < n);
    }
    if ((int32_t)U16(G32(0x4b2198), 0x432) < n)
        sel = U16(G32(0x4b2198), 0x432);
    pt_SendDlgItemMessageA(dlg, id, 0x14e, sel, 0);
}

/* 0x4885d0: display start-up for the movies and front end: like 0x487e10
 * but the primary display (driver 0), no 3D skip, one back buffer, no
 * remake; half: try the half-size mode first; palette memory from the low
 * arena. 1 = done, 0 on a DirectDraw error. */
int32_t GAMEAPI dd_start_2d(int32_t full, int32_t flip, int32_t w, int32_t h, int32_t d3d, int32_t half, int32_t bpp)
{
    uint8_t hal[0x17c], hel[0x17c];
    uint32_t desc[27] = {0};
    uint32_t caps[1];
    int32_t hr;
    g_flip = full ? (uint32_t)flip : 0;
    G32(0x4d5fa4) = 0;
    G32(0x4d5fa8) = 0;
    g_fullscreen = (uint32_t)full;
    G32(0x4d5f7c) = 0;
    pt_SetFocus(g_hwnd);
    dd_surfaces_clear();
    dd_drivers_enum();
    pt_ShowWindow(g_hwnd, 1);
    dd_create(0);
    if (G32(0x4d6458) && g_fullscreen)
        g_flip = 1;
    mem_fill_at(hal, 0, 0x17c);
    mem_fill_at(hel, 0, 0x17c);
    U32(hal, 0) = 0x17c;
    U32(hel, 0) = 0x17c;
    hr = COM3(g_dd, 0x2c, hal, hel);
    g_dd_result = (uint32_t)hr;
    if (hr)
        goto fail;
    pt_caps_flags(U32(hal, 4));
    hr = COM3(g_dd, 0x50, g_hwnd, g_fullscreen ? 0x813 : 0x808);
    g_dd_result = (uint32_t)hr;
    if (hr)
        goto fail;
    if (g_fullscreen) {
        dd_modes_enum();
        if (half) {
            if (!dd_mode_find(w >> 1, h >> 1, bpp))
                dd_mode_find(w, h, bpp);
        } else {
            dd_mode_find(w, h, bpp);
        }
    } else {
        pt_desktop_bpp();
    }
    if (g_fullscreen) {
        g_screen_w = G32(0x4d5fd8);
        g_screen_h = G32(0x4d5fdc);
        g_screen_bpp = G32(0x4d5fe0);
        hr = COM4(g_dd, 0x54, g_screen_w, g_screen_h, g_screen_bpp);
        g_dd_result = (uint32_t)hr;
        if (hr)
            goto fail;
        g_mode_set = 1;
    }
    G32(0x4d5f8c) = g_screen_h;
    G32(0x4d5f88) = g_screen_w;
    desc[0] = 0x6c;
    if (g_flip) {
        desc[1] = 0x21;
        desc[26] = d3d ? 0x2218 : 0x218;
        desc[5] = 1;
    } else {
        desc[1] = 1;
        desc[26] = 0x200;
    }
    hr = COM4(g_dd, 0x18, desc, 0x4d5f48, 0);
    g_dd_result = (uint32_t)hr;
    if (hr)
        goto fail;
    if (g_mode_set)
        dd_primary_clear();
    if (g_flip) {
        caps[0] = 4;
        hr = COM3(g_primary, 0x30, caps, 0x4d5f4c);
    } else {
        desc[3] = g_screen_w;
        desc[2] = g_screen_h;
        desc[26] = d3d ? 0x2040 : 0x40;
        desc[1] = 7;
        hr = COM4(g_dd, 0x18, desc, 0x4d5f4c, 0);
    }
    g_dd_result = (uint32_t)hr;
    if (hr)
        goto fail;
    desc[0] = 0x6c;
    COM2(g_back, 0x58, desc);
    G32(0x4d5fac) = (desc[26] >> 14) & 1;
    dd_pixel_format_get(A(desc), 0x4d5f50);
    pt_clipper();
    if (g_bpp == 8)
        pt_palette((uint32_t)arena_alloc_low_at(0x400));
    return 1;
fail:
    dx_error_at(hr);
    return 0;
}

/* ------------------------------------------------------------ DirectInput */

/* 0x488d90: DirectInput start-up: clears its state (0x4d6f30, 0x2728
 * bytes), DirectInputCreateA (version 0x500), enumerates attached
 * joysticks (callback 0x488e80), acquires the first, then the keyboard.
 * 1 = done (0 on an error). */
int32_t GAMEAPI input_init(void)
{
    int32_t hr;
    mem_fill_at(0x4d6f30, 0, 0x2728);
    hr = pt_DirectInputCreateA(g_hinstance, 0x500, 0x4d6f30, 0);
    g_di_result = (uint32_t)hr;
    if (hr)
        return 0;
    hr = COM5(g_di, 0x10, 4, 0x488e80, 0, 1);
    g_di_result = (uint32_t)hr;
    if (hr)
        return 0;
    if (g_joy_count > 0) {
        hr = COM1(JOY_DEV(0), 0x1c);
        g_di_result = (uint32_t)hr;
        if (hr != 0 && hr != 1)
            return 0;
    }
    input_init_keyboard();
    return 1;
}

/* 0x488e20: CreateDevice(guid) and its IDirectInputDevice2 (0x4a3eb0)
 * into *out (0 on failure); the first interface is released. Returns the
 * last result (also 0x4d6f2c). */
int32_t GAMEAPI di_device_create(uint32_t di, uint32_t guid, uint32_t out, uint32_t outer)
{
    uint32_t dev = di; /* the original reuses its first argument's slot */
    int32_t hr = COM4(di, 0xc, guid, &dev, outer);
    g_di_result = (uint32_t)hr;
    if (hr) {
        U32(out, 0) = 0;
        return (int32_t)g_di_result;
    }
    g_di_result = (uint32_t)COM3(dev, 0, 0x4a3eb0, out);
    COM1(dev, 8);
    return (int32_t)g_di_result;
}

/* 0x488e80: EnumDevices callback (stdcall): joystick record n
 * (0x4d7374 + n * 0x43c) = the device instance, its device, caps (+0x248;
 * buttons / axes / POVs copied to +0x274 / +0x278 / +0x280), exclusive
 * background level, joystick data format (0x491b50), its objects (callback
 * 0x4890b0), force feedback effects when the caps have
 * DIDC_FORCEFEEDBACK. 1 = go on, 0 when the objects fail. */
int32_t PT_STD input_enum_joystick(uint32_t inst, uint32_t ctx)
{
    int32_t hr;
    (void)ctx;
    mem_fill_at(JOY(g_joy_count), 0, 4);
    mem_copy_at(JOY(g_joy_count), inst, 0x244);
    g_di_result = (uint32_t)di_device_create(g_di, inst + 4, JOY(g_joy_count) + 0x244, 0);
    mem_fill_at(JOY(g_joy_count) + 0x248, 0, 0x2c);
    U32(JOY(g_joy_count), 0x248) = 0x2c;
    g_di_result = (uint32_t)COM2(JOY_DEV(g_joy_count), 0xc, JOY(g_joy_count) + 0x248);
    U32(JOY(g_joy_count), 0x274) = U32(JOY(g_joy_count), 0x258);
    U32(JOY(g_joy_count), 0x278) = U32(JOY(g_joy_count), 0x254);
    U32(JOY(g_joy_count), 0x280) = U32(JOY(g_joy_count), 0x25c);
    U32(JOY(g_joy_count), 0x27c) = 0;
    g_di_result = (uint32_t)COM3(JOY_DEV(g_joy_count), 0x34, g_hwnd, 5);
    g_di_result = (uint32_t)COM2(JOY_DEV(g_joy_count), 0x2c, 0x491b50);
    hr = COM4(JOY_DEV(g_joy_count), 0x10, 0x4890b0, g_joy_count, 0);
    g_di_result = (uint32_t)hr;
    if (hr)
        return 0;
    if (U32(JOY(g_joy_count), 0x24c) & 0x100)
        input_create_ff_effects(g_joy_count);
    g_joy_count = g_joy_count + 1;
    return 1;
}

/* 0x489180: dead zone (DIPROP_DEADZONE, at least 100) of object obj. */
void GAMEAPI joy_deadzone_set(int32_t joy, int32_t value, uint32_t obj)
{
    uint32_t p[5];
    if (value < 100)
        value = 100;
    p[0] = 0x14;
    p[1] = 0x10;
    p[2] = obj;
    p[3] = 1;
    p[4] = (uint32_t)value;
    g_di_result = (uint32_t)COM3(JOY_DEV(joy), 0x18, 5, p);
}

/* 0x4891f0: range -127..127 (DIPROP_RANGE) of object obj. */
void GAMEAPI joy_range_set(int32_t joy, uint32_t obj)
{
    uint32_t p[6];
    p[0] = 0x18;
    p[2] = obj;
    p[1] = 0x10;
    p[3] = 1;
    p[4] = (uint32_t)-127;
    p[5] = 0x7f;
    g_di_result = (uint32_t)COM3(JOY_DEV(joy), 0x18, 4, p);
}

/* 0x489250: a DIEFFECT (0x34) with the game's defaults: object offsets and
 * cartesian, infinite, gain 10000, no trigger, the two axes 0x4b2c9c and
 * directions 0x4d9adc. */
void GAMEAPI ff_effect_defaults(uint32_t e)
{
    mem_fill_at(e, 0, 0x34);
    U32(e, 0) = 0x34;
    U32(e, 4) = 0x22;
    U32(e, 8) = 0xffffffffu;
    U32(e, 0xc) = 0;
    U32(e, 0x10) = 10000;
    U32(e, 0x14) = 0xffffffffu;
    U32(e, 0x18) = 0;
    U32(e, 0x1c) = 2;
    U32(e, 0x20) = 0x4b2c9c;
    U32(e, 0x24) = 0x4d9adc;
    U32(e, 0x28) = 0;
}

/* 0x4892b0: the force feedback effects of joystick joy (acquired for the
 * creation, then unacquired): a constant force (+0x314), two conditions
 * (+0x31c, +0x318), a condition with dead bands (+0x320) and two
 * periodic ones (+0x324, +0x328). */
void GAMEAPI input_create_ff_effects(int32_t joy)
{
    uint32_t eff[13];
    uint32_t force[1];
    uint32_t cond_a[12], cond_b[12], cond_c[12];
    uint32_t wave[4];
    uint32_t rec = JOY(joy);
    g_di_result = (uint32_t)COM1(U32(rec, 0x244), 0x1c);
    ff_effect_defaults(A(eff));
    eff[12] = A(force);
    force[0] = 10000;
    eff[11] = 4;
    g_di_result = (uint32_t)COM5(U32(rec, 0x244), 0x48, 0x4a3fc0, eff, rec + 0x314, 0);
    mem_fill_at(cond_a, 0, 0x30);
    eff[12] = A(cond_a);
    cond_a[7] = 10000;
    cond_a[1] = 10000;
    cond_a[8] = 10000;
    cond_a[2] = 10000;
    eff[11] = 0x30;
    g_di_result = (uint32_t)COM5(U32(rec, 0x244), 0x48, 0x4a4030, eff, rec + 0x31c, 0);
    mem_fill_at(cond_b, 0, 0x30);
    eff[12] = A(cond_b);
    cond_b[7] = 10000;
    cond_b[1] = 10000;
    cond_b[8] = 10000;
    cond_b[2] = 10000;
    eff[11] = 0x30;
    g_di_result = (uint32_t)COM5(U32(rec, 0x244), 0x48, 0x4a4060, eff, rec + 0x318, 0);
    cond_c[11] = 1000;
    cond_c[5] = 1000;
    eff[12] = A(cond_c);
    cond_c[6] = 0;
    cond_c[0] = 0;
    cond_c[7] = 10000;
    cond_c[1] = 10000;
    cond_c[8] = 10000;
    cond_c[2] = 10000;
    cond_c[9] = 10000;
    cond_c[3] = 10000;
    cond_c[10] = 10000;
    cond_c[4] = 10000;
    eff[11] = 0x30;
    g_di_result = (uint32_t)COM5(U32(rec, 0x244), 0x48, 0x4a4050, eff, rec + 0x320, 0);
    mem_fill_at(wave, 0, 0x10);
    eff[12] = A(wave);
    wave[0] = 0x9c4;
    wave[1] = 0;
    wave[2] = 0;
    wave[3] = 0x249f0;
    eff[11] = 0x10;
    g_di_result = (uint32_t)COM5(U32(rec, 0x244), 0x48, 0x4a3ff0, eff, rec + 0x324, 0);
    g_di_result = (uint32_t)COM5(U32(rec, 0x244), 0x48, 0x4a3fe0, eff, rec + 0x328, 0);
    g_di_result = (uint32_t)COM1(U32(rec, 0x244), 0x20);
}

/* 0x489500: GetProperty(prop) of object obj into the DIPROPRANGE-sized
 * buffer buf (cleared, header filled). 1. */
int32_t GAMEAPI joy_property_get(int32_t joy, uint32_t buf, uint32_t prop, uint32_t obj)
{
    mem_fill_at(buf, 0, 0x18);
    U32(buf, 8) = obj;
    U32(buf, 0) = 0x18;
    U32(buf, 4) = 0x10;
    U32(buf, 0xc) = 1;
    g_di_result = (uint32_t)COM3(JOY_DEV(joy), 0x14, prop, buf);
    return 1;
}

/* 0x489560: DirectInput shut-down: keyboard, joysticks (their effects
 * through 0x489f70), DirectInput. */
void GAMEAPI input_shutdown(void)
{
    if (!g_di)
        return;
    if (g_keyboard) {
        COM1(g_keyboard, 0x20);
        COM1(g_keyboard, 8);
        g_keyboard = 0;
    }
    for (int32_t i = 0; i < g_joy_count; i++) {
        uint32_t p = JOY(i) + 0x244;
        if (G32(p)) {
            joy_effects_release_at(i);
            COM1(G32(p), 0x20);
            COM1(G32(p), 8);
            G32(p) = 0;
        }
    }
    g_joy_count = 0;
    COM1(g_di, 8);
    g_di = 0;
}

/* 0x489910: the keyboard device (GUID_SysKeyboard 0x4a3fa0): non-exclusive
 * foreground, keyboard data format (0x492b70), acquired. 1. */
int32_t GAMEAPI input_init_keyboard(void)
{
    g_di_result = (uint32_t)di_device_create(g_di, 0x4a3fa0, 0x4d6f34, 0);
    if (g_keyboard) {
        g_di_result = (uint32_t)COM3(g_keyboard, 0x34, g_hwnd, 9);
        g_di_result = (uint32_t)COM2(g_keyboard, 0x2c, 0x492b70);
        g_di_result = (uint32_t)COM1(g_keyboard, 0x1c);
    }
    return 1;
}

/* |a - b| as the original's cdq / xor / sub (wraps at INT_MIN). */
static int32_t pt_absdiff(int32_t a, int32_t b)
{
    int32_t d = a - b;
    int32_t s = d >> 31;
    return (d ^ s) - s;
}

/* 0x489980: the constant force effect (+0x314) of joystick joy: polar
 * direction angle, magnitude mag. Unchanged while within 1000 / 3600 of
 * the last values (0x4b2ca8 / 0x4d6f28, -1 = none) and still playing;
 * else set (direction and parameters), status read, started. */
void GAMEAPI ff_road_set(uint32_t joy, int32_t angle, int32_t mag)
{
    uint32_t eff[13];
    int32_t dir[2];
    int32_t force[1];
    uint32_t status = joy; /* the original's status lands in its first argument's slot */
    uint32_t p = JOY(joy) + 0x314;
    if (!G32(p))
        return;
    if (GI32(0x4b2ca8) != -1 && pt_absdiff(mag, GI32(0x4b2ca8)) < 1000 &&
        pt_absdiff(angle, GI32(0x4d6f28)) < 0xe10) {
        COM2(G32(p), 0x24, &status);
        if (status & 1)
            return;
    }
    G32(0x4b2ca8) = (uint32_t)mag;
    G32(0x4d6f28) = (uint32_t)angle;
    dir[0] = angle;
    dir[1] = 0;
    force[0] = mag;
    ff_effect_defaults(A(eff));
    eff[12] = A(force);
    eff[9] = A(dir);
    eff[1] = 0x20;
    eff[11] = 4;
    g_di_result = (uint32_t)COM3(G32(p), 0x18, eff, 0x140);
    COM2(G32(p), 0x24, &status);
    g_di_result = (uint32_t)COM3(G32(p), 0x1c, 1, 0);
}

/* 0x489a80: stops the constant force effect of joystick joy. */
void GAMEAPI ff_road_stop(int32_t joy)
{
    uint32_t e = U32(JOY(joy), 0x314);
    if (e)
        COM1(e, 0x20);
}

/* A condition effect's coefficients (0x489ab0 / 0x489bc0): v on both axes'
 * positive and negative coefficients; unchanged while within 1000 of the
 * last value (*last, -1 = none) and still playing. */
static void pt_spring_set(uint32_t joy, int32_t v, uint32_t slot, uint32_t last)
{
    uint32_t cond[12];
    uint32_t eff[13];
    uint32_t status = joy; /* the original's first argument's slot */
    uint32_t p = JOY(joy) + slot;
    if (!G32(p))
        return;
    if (GI32(last) != -1 && pt_absdiff(v, GI32(last)) < 1000) {
        COM2(G32(p), 0x24, &status);
        if (status & 1)
            return;
    }
    G32(last) = (uint32_t)v;
    mem_fill_at(cond, 0, 0x30);
    cond[7] = (uint32_t)v;
    cond[1] = (uint32_t)v;
    cond[8] = (uint32_t)v;
    cond[2] = (uint32_t)v;
    ff_effect_defaults(A(eff));
    eff[12] = A(cond);
    eff[11] = 0x30;
    g_di_result = (uint32_t)COM3(G32(p), 0x18, eff, 0x100);
    COM2(G32(p), 0x24, &status);
    g_di_result = (uint32_t)COM3(G32(p), 0x1c, 1, 0);
}

/* 0x489ab0: the condition effect +0x31c (last value 0x4b2cac). */
void GAMEAPI ff_spring_a_set(uint32_t joy, int32_t v)
{
    pt_spring_set(joy, v, 0x31c, 0x4b2cac);
}

/* 0x489b90: stops the condition effect +0x31c. */
void GAMEAPI ff_spring_a_stop(int32_t joy)
{
    uint32_t e = U32(JOY(joy), 0x31c);
    if (e)
        g_di_result = (uint32_t)COM1(e, 0x20);
}

/* 0x489bc0: the condition effect +0x318 (last value 0x4b2cb0). */
void GAMEAPI ff_spring_b_set(uint32_t joy, int32_t v)
{
    pt_spring_set(joy, v, 0x318, 0x4b2cb0);
}

/* 0x489ca0: stops the condition effect +0x318. */
void GAMEAPI ff_spring_b_stop(int32_t joy)
{
    uint32_t e = U32(JOY(joy), 0x318);
    if (e)
        g_di_result = (uint32_t)COM1(e, 0x20);
}

/* 0x489cd0: the periodic effect +0x324 of joystick joy: period (-1 = keep
 * the parameters) and magnitude; unchanged while within 12000 / 1000 of
 * the last values (0x4b2cb4 / 0x4d6f20) and still playing; else set,
 * status read, started. */
void GAMEAPI ff_engine_set(uint32_t joy, int32_t period, int32_t mag)
{
    uint32_t wave[4];
    uint32_t eff[13];
    uint32_t status = joy; /* the original's first argument's slot */
    uint32_t p = JOY(joy) + 0x324;
    if (!G32(p))
        return;
    if (GI32(0x4b2cb4) != -1 && pt_absdiff(period, GI32(0x4b2cb4)) < 12000 &&
        pt_absdiff(mag, GI32(0x4d6f20)) < 1000) {
        COM2(G32(p), 0x24, &status);
        if (status & 1)
            return;
    }
    G32(0x4b2cb4) = (uint32_t)period;
    G32(0x4d6f20) = (uint32_t)mag;
    if (period != -1) {
        mem_fill_at(wave, 0, 0x10);
        wave[0] = (uint32_t)mag;
        wave[1] = 0;
        wave[2] = 0;
        wave[3] = (uint32_t)period;
        ff_effect_defaults(A(eff));
        eff[12] = A(wave);
        eff[11] = 0x10;
        g_di_result = (uint32_t)COM3(G32(p), 0x18, eff, 0x100);
    }
    COM2(G32(p), 0x24, &status);
    g_di_result = (uint32_t)COM3(G32(p), 0x1c, 1, 0);
}
