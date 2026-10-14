/* Group GD: front-end / input / setup leftovers.
 *
 *  - the Direct3D device enumeration callback 0x41d280 and the DirectDraw
 *    depth flag table 0x41d640;
 *  - small menu helpers: the held-button words 0x4474e0 / 0x447500, the
 *    sliders' callbacks 0x44d000 / 0x450c00, the controls table setters
 *    0x452640 / 0x4526f0 / 0x452900, the cheat-code typing 0x4360e1;
 *  - the joystick binding capture 0x48a2f0-0x48a590 and the DirectInput
 *    enumeration callbacks 0x4890b0 / 0x48ac40;
 *  - the video-mode list fill 0x488470 (dialog messages, SendDlgItemMessageA);
 *  - the x87 ones: menu slider step 0x447d80, slider builder 0x44cb10, the
 *    sound options menu 0x451110, EAX voice fade 0x471580, 3D placement
 *    0x472bb0 and the channel refresh 0x470ef0 (QMixer calls).
 *
 * Every float computation uses the original's x87 instructions in inline
 * asm (same operand order and widths, intermediates kept in x87 registers
 * where the original does), so rounding matches; only integer logic, float
 * bit copies and control flow are C. The C runtime (strstr, strlwr,
 * sprintf, atan2, sqrt, fmod, sin, cos, ftol), DirectDraw / DirectInput
 * COM work, the drawing (0x431780 text boxes, the 16-bit font blits
 * 0x488ad0 / 0x488cc0 and 0x448880 / 0x452c20 / 0x452ce0) stay original
 * and are called by address. */
#include "../runtime.h"
#include "gamefn.h"
#include "physics.h"
#include "winapi.h"

#define STDCALL __attribute__((stdcall))

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define S8(p, off) (*(int8_t *)((uint8_t *)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define PTR(p, off) GP((uint8_t *)(p) + (off))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))

#define g_config GP(0x4b2198)
#define g_race GP(0x4b1960)

/* The menu builders in src/game/fe_z.c (not in gamefn.h). */
typedef int32_t(GAMEAPI *gd_menu_callback)(int32_t command, char *text);
int32_t GAMEAPI menu_add_choice(const char *section1, int32_t index1, const char *section2, int32_t index2,
                                gd_menu_callback callback, uint32_t a6); /* 0x44c6d0 */
int32_t GAMEAPI menu_add_slider(const char *section1, int32_t index1, const char *section2, int32_t index2,
                                gd_menu_callback callback, uint32_t a6); /* 0x44c9d0 */
#define GD_CB(a) ((gd_menu_callback)(uintptr_t)(a))
/* A game address held as an int32 (zero-extended, as in 32 bits). */
#define GD_P(a) GMEM((uint32_t)(a))

/* ---- x87 helpers: the original's instructions, memory operands and all ---- */

/* fld a; fcomp b; fnstsw ax: the status word's high byte (C0 = 1, C2 = 4,
 * C3 = 0x40), so NaN and zero compare exactly as in the original. */
static inline uint32_t fcmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 0xff;
}

/* (fld a; fadd b; fstp) and the other float operations. */
static inline uint32_t fadd_s(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

static inline uint32_t fsub_s(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

static inline uint32_t fmul_s(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

static inline uint32_t fdiv_s(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfdivs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fild dword [v]; fstp dword. */
static inline uint32_t fild_f(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* ===== 0x41d640: DirectDraw bit depth -> DDBD flag (-1: none) ===== */
int32_t GAMEAPI ddraw_depth_flag(int32_t bpp)
{
    switch (bpp) {
    case 1: return 0x4000;
    case 2: return 0x2000;
    case 4: return 0x1000;
    case 8: return 0x800;
    case 15:
    case 16: return 0x400;
    case 24: return 0x200;
    case 32: return 0x100;
    default: return -1;
    }
}

/* ===== 0x41d280: Direct3D device enumeration callback (stdcall, 6
 * arguments, returns 1 = go on). Keeps the devices that can draw in the
 * current depth into the table at 0x4b86c4 (0x338 bytes each, count
 * 0x4ba6f4): guid, description, name, the 0xfc-byte device description,
 * and flags derived from it. a3 is the hardware description (a4 the
 * software one, used when a3 has no colour model). */
#define D3D_REC(i) (0x4b86c4 + (uint32_t)(i) * 0x338)
#define D3D_COUNT GI32(0x4ba6f4)

static void d3d_copy_string(uint8_t *dst, const uint8_t *src)
{
    uint32_t n = 0;
    while (src[n] != 0)
        n++;
    n++;
    for (uint32_t i = 0; i < n; i++)
        dst[i] = src[i];
}

int32_t STDCALL d3d_enum_device(const uint8_t *guid, const uint8_t *desc, const uint8_t *name, const uint8_t *hw,
                                const uint8_t *sw, uint32_t user)
{
    const uint8_t *caps = hw;
    uint32_t color_model = U32(hw, 8);
    (void)user;
    if (color_model == 0)
        caps = sw;
    if (G32(0x4d5fa4) != 0 && color_model != 0)
        return 1;
    if ((U32(caps, 0x9c) & (uint32_t)ddraw_depth_flag(GI32(0x4d5f70))) == 0)
        return 1;
    if (U8(caps, 8) & 1) {
        if (!(U8(caps, 0x80) & 4))
            return 1;
    } else {
        if (!(U8(caps, 0x80) & 8))
            return 1;
    }
    if (!(U8(caps, 0x84) & 1))
        return 1;
    uint8_t *rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    GCALL(0x476f30, mem_copy)(rec + 0x304, guid, 0x10);
    d3d_copy_string(rec + 0x104, desc);
    d3d_copy_string(rec + 0x000, name);
    GCALL(0x476f30, mem_copy)(rec + 0x208, caps, 0xfc);
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    U32(rec, 0x314) = color_model;
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    U32(rec, 0x318) = U32(rec, 0x28c) & 1;
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    U32(rec, 0x334) = (U32(rec, 0x28c) >> 5) & 1;
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    U32(rec, 0x31c) = U32(rec, 0x2a8) != 0;
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    U32(rec, 0x330) = 0;
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    uint32_t edx = U32(rec, 0x280);
    uint32_t bits = U32(rec, 0x27c);
    if ((edx & 8) && (bits & 1) && (bits & 2) && (U8(rec, 0x28c) & 0xa)) {
        U32(rec, 0x330) = U32(rec, 0x330) | 1;
    } else {
        uint32_t esi = U32(rec, 0x27c);
        if (((esi & 0x800) || ((esi & 0x10) && (edx & 0x20))) && (U8(rec, 0x28c) & 0xa))
            U32(rec, 0x330) = U32(rec, 0x330) | 2;
        rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
        uint32_t flags = U32(rec, 0x288);
        if ((flags & 0x5000) && (U8(rec, 0x294) & 8))
            U32(rec, 0x330) = U32(rec, 0x330) | 4;
        else if ((flags & 0xa000) && (U8(rec, 0x294) & 8))
            U32(rec, 0x330) = U32(rec, 0x330) | 8;
    }
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    if (U32(rec, 0x330) == 0)
        U32(rec, 0x320) = 0;
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    U32(rec, 0x328) = U32(rec, 0x210) == 2;
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    U32(rec, 0x32c) = ~U32(rec, 0x214) & 1;
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    char *found = GCALL(0x4980d0, msvc_strstr)((const char *)rec, (const char *)0x4a80bc);
    rec = (uint8_t *)(uintptr_t)D3D_REC(D3D_COUNT);
    U32(rec, 0x324) = found != 0 ? 1 : 0;
    D3D_COUNT = D3D_COUNT + 1;
    return 1;
}

/* ===== 0x435d43: network lobby step (mode 1 or 2): returns 0 (fail),
 * 1 (go on), 2 (session ready), 3 (timed out in mode 2). */
int32_t GAMEAPI network_session_poll(int32_t mode)
{
    GCALL(0x48c630, dplay_receive_all)();
    GCALL(0x48c460, net_inbox_dispatch)();
    if (GCALL(0x45f530, net_all_finished)())
        GI32(0x4be4a4) = GI32(0x4be4a4) + 1;
    else
        GI32(0x4be4a4) = 0;
    if (mode == 2 && GI32(0x4be4a4) > 0xf)
        return 3;
    if (mode == 1 && GI32(0x4be4a4) > 0xf) {
        if (GCALL(0x45f0c0, net_cars_collect)() == 0)
            return 0;
        if (GCALL(0x45dde0, netplay_players_send)() == 0)
            return 0;
        if (GCALL(0x45e400, netplay_settings_send)() == 0)
            return 0;
    }
    if (GCALL(0x45f2c0, net_states_changed)() != 0) {
        GCALL(0x45f310, net_states_send)();
        GCALL(0x452d20, menu_dirty_set)();
    }
    if (GCALL(0x45f390, net_menu_mode_unset)() != 0)
        GCALL(0x45f3a0, net_menu_mode_send)();
    if (GCALL(0x45f0b0, net_settings_sent)() != 0)
        return 2;
    return 1;
}

/* ===== 0x4360e1: a typed character (WM_CHAR) for the cheat codes: the
 * last 33 characters typed are kept in 0x4be3bc (newest first, lower
 * cased); a code at the start of that text toggles an option, and a
 * sound plays if one matched. press == 0 does nothing. */
#define CHEAT_BUF 0x4be3bc

static int cheat_match(uint32_t code)
{
    return GCALL(0x4980d0, msvc_strstr)((const char *)CHEAT_BUF, (const char *)(uintptr_t)code) == (const char *)CHEAT_BUF;
}

/* Flip bit n of byte p (the original builds ((b >> n) & 1 ^ 1) << n). */
static void flip_bit(uint8_t *p, int n)
{
    uint8_t flipped = (uint8_t)((((*p >> n) & 1) ^ 1) & 1) << n;
    *p = (uint8_t)((*p & ~(1 << n)) | flipped);
}

void GAMEAPI cheat_code_char(uint32_t unused, int32_t ch, int32_t press)
{
    int32_t mode_hit = 1;
    int32_t code_hit = 1;
    (void)unused;
    if (press == 0)
        return;
    for (int32_t i = 0x20; i > 0; i--)
        G8(CHEAT_BUF + i) = G8(CHEAT_BUF + i - 1);
    G8(CHEAT_BUF) = (uint8_t)ch;
    G8(0x4be3dd) = 0;
    GCALL(0x4a1c3c, crt_strlwr)((char *)CHEAT_BUF);
    if (G32(0x4be47c) == 0) {
        if (cheat_match(0x4acd5c)) {
            g_config[0x97] = 2;
            GCALL(0x43775a, game_speed_set)();
        } else if (cheat_match(0x4acd6c)) {
            g_config[0x97] = 1;
            GCALL(0x43775a, game_speed_set)();
        } else if (cheat_match(0x4acd7c)) {
            g_config[0x97] = 0;
            GCALL(0x43775a, game_speed_set)();
        } else {
            mode_hit = 0;
        }
    } else {
        mode_hit = 0;
    }
    if (cheat_match(0x4acd94)) {
        GCALL(0x447f60, config_flag7_toggle)();
    } else if (cheat_match(0x4acda4)) {
        g_config[0x96] ^= 1;
    } else if (cheat_match(0x4acdac)) {
        g_config[0x95] ^= 1;
    } else if (cheat_match(0x4acdbc)) {
        flip_bit(&g_race[0x721], 6);
    } else if (cheat_match(0x4acdcc)) {
        flip_bit(&g_race[0x720], 7);
    } else if (cheat_match(0x4acddc)) {
        flip_bit(&g_race[0x720], 1);
    } else if (cheat_match(0x4acdec)) {
        flip_bit(&g_race[0x720], 2);
    } else if (cheat_match(0x4acdfc)) {
        flip_bit(&g_race[0x720], 4);
    } else if (cheat_match(0x4ace08)) {
        flip_bit(&g_race[0x720], 5);
    } else if (cheat_match(0x4ace14)) {
        flip_bit(&g_race[0x720], 3);
    } else if (cheat_match(0x4ace20)) {
        flip_bit(&g_race[0x721], 7);
    } else {
        code_hit = 0;
    }
    if (mode_hit != 0 || code_hit != 0)
        GCALL(0x471c90, sound_play_body)(0x23, 5, 0x800, 0x3a98, 0);
}

/* ===== menu helpers ===== */

/* 0x4474e0: the four held-button words cleared. */
void GAMEAPI menu_hold_clear(void)
{
    G32(0x4cb5a8) = 0;
    G32(0x4cb5b8) = 0;
    G32(0x4cb5c8) = 0;
    G32(0x4cb5d8) = 0;
}

/* 0x447500: the repeat counter behind each held-button word cleared when
 * the word is. */
void GAMEAPI menu_hold_reset(void)
{
    if (G32(0x4cb5a8) == 0)
        G32(0x4cb5ac) = 0;
    if (G32(0x4cb5b8) == 0)
        G32(0x4cb5bc) = 0;
    if (G32(0x4cb5c8) == 0)
        G32(0x4cb5cc) = 0;
    if (G32(0x4cb5d8) == 0)
        G32(0x4cb5dc) = 0;
}

/* 0x447f60: the cheat toggle for config byte 7; in game mode 4 the menu is
 * flagged as changed. */
void GAMEAPI config_flag7_toggle(void)
{
    uint8_t *c = g_config;
    c[7] = (uint8_t)(c[7] == 0);
    if (GI32(0x4af64c) == 4)
        GI32(0x4cb58c) = 1;
}

/* 0x448330: *p += step (wrapped into 0..3) until 0x48d450(*p) is 0 for
 * the value reached: at most four steps. */
void GAMEAPI menu_pick_available(int32_t *p, int32_t step)
{
    int32_t tries = 4;
    if (GCALL(0x48d450, net_connection_available)((uint32_t)*p) != 0)
        return;
    while (tries > 0) {
        *p = *p + step;
        if (*p < 0)
            *p = 3;
        if (*p > 3)
            *p = 0;
        tries--;
        if (GCALL(0x48d450, net_connection_available)((uint32_t)*p) != 0)
            return;
    }
}

/* 0x448380: config bytes 0x83 and 0x93 both set to "byte 0x80 is zero". */
void GAMEAPI config_mirror_flag(void)
{
    uint8_t *c = g_config;
    if (c[0x80] != 0) {
        c[0x93] = 0;
        g_config[0x83] = 0;
    } else {
        c[0x93] = 1;
        g_config[0x83] = 1;
    }
}

/* 0x44ebb0: 0x4479f0 with a third argument 0. */
void GAMEAPI menu_call_with_zero(int32_t a, int32_t b)
{
    GCALL(0x4479f0, fe_menu_goto_ex)(a, b, 0);
}

/* 0x452730: the menu flagged as changed and four menu state words reset. */
void GAMEAPI menu_state_reset(void)
{
    GI32(0x4cb58c) = 1;
    GCALL(0x4475d0, fe_repeat_updown)(0);
    GCALL(0x4475f0, fe_repeat_select)(0);
    GCALL(0x447610, fe_repeat_leftright)(0);
    GCALL(0x447630, fe_repeat_page)(0);
}

/* 0x44d000: a menu item's callback for a two-word value (commands
 * 0x12-0x16, -1 for others). With a2 == 100 the value is one of the
 * saved pairs 0x4b00a4.., a2 == 0 the pair 0x4b008c, else a saved entry
 * (0x448580). */
int32_t GAMEAPI menu_param_callback(int32_t cmd, uint8_t *dst, int32_t a2, int32_t a3)
{
    switch ((uint32_t)(cmd - 0x12)) {
    case 0:
        if (a2 == 100 && a3 != 0) {
            U32(dst, 0) = G32(0x4b00a4);
            U32(dst, 4) = G32(0x4b00a8);
            *(uint16_t *)(dst + 8) = G16(0x4b00ac);
            return a2;
        }
        if (a2 != 0) {
            GCALL(0x448580, track_title)(dst, a2 - 1);
            return a2;
        }
        U32(dst, 0) = G32(0x4b008c);
        U32(dst, 4) = G32(0x4b0090);
        return a2;
    case 1:
        U32(dst, 0) = G32(0x4b0094);
        U32(dst, 4) = G32(0x4b0098);
        U32(dst, 8) = G32(0x4b009c);
        *(uint16_t *)(dst + 0xc) = G16(0x4b00a0);
        return a2;
    case 2:
        if (a2 == 100) {
            if (a3 != 0)
                return 0x14;
            return a2 - 1;
        }
        if (a2 == 0)
            return (a3 != 0 ? 0x50 : 0) + 0x14;
        if (a2 <= 1)
            return 0;
        return a2 - 1;
    case 3:
    case 4:
        if (a2 == 100)
            return 0;
        if (a2 == 0)
            return 1;
        if (a2 >= 20) {
            if (a3 == 0)
                return 0;
            return 100;
        }
        return a2 + 1;
    default:
        return -1;
    }
}

/* 0x4503c0: steps the race player's setting at +0x585 down while the
 * mode in 0x4af794 (1 or 2) says that setting is not allowed by the flag
 * bits at +0x720. */
void GAMEAPI race_setting_trim(void)
{
    int32_t mode = GI32(0x4af794);
    if (mode == -1)
        return;
    for (;;) {
        uint8_t *race = g_race;
        uint8_t level = race[0x585];
        if (level > 2)
            return;
        uint8_t flags = race[0x720];
        int32_t both;
        int32_t first;
        if (((flags & 8) && level < 1) || ((flags & 0x10) && level < 2)) {
            both = 1;
            first = 1;
        } else {
            both = (flags & 4) != 0;
            first = (flags & 2) != 0;
        }
        if (mode == 2) {
            if (both)
                return;
            race[0x585] = (uint8_t)(level - 1);
            mode = GI32(0x4af794);
        } else if (mode == 1) {
            if (first)
                return;
            race[0x585] = (uint8_t)(level - 1);
            mode = GI32(0x4af794);
        } else {
            return;
        }
    }
}

/* 0x450c00: a menu choice callback (commands 0x12-0x16): 0x12 clamps *p
 * into 0..max and shows the text, 0x14 steps down, 0x15 / 0x16 up (wrapping);
 * returns 1, or 0 for any other command. */
int32_t GAMEAPI menu_choice_callback(int32_t cmd, int32_t a1, int32_t max, int32_t *p, int32_t a4)
{
    switch ((uint32_t)(cmd - 0x12)) {
    case 0:
        if (*p < 0)
            *p = 0;
        if (*p > max)
            *p = max;
        GCALL(0x4481a0, menu_text_get)((char *)GD_P(a1), -1, (const char *)GD_P(a4), *p + 2);
        return 1;
    case 1:
        return 1;
    case 2:
        *p = *p - 1;
        if (*p < 0 || *p > max)
            *p = max;
        return 1;
    case 3:
    case 4:
        *p = *p + 1;
        if (*p < 0 || *p > max)
            *p = 0;
        return 1;
    default:
        return 0;
    }
}

/* 0x452640: sets a controls entry's record (0x449c40 finds it): type 2
 * with the key (a3, or the saved key for a2 = 0 / 1 / 3) and extra byte
 * a4; any other a2 clears it. Returns -1 if there is no such entry. */
int32_t GAMEAPI control_entry_set(int32_t player, int32_t index, int32_t kind, int32_t key, int32_t extra)
{
    uint8_t *entry = GCALL(0x449c40, binding_saved_get)(player, index);
    if (entry == 0)
        return -1;
    if ((uint32_t)(kind + 1) > 4) {
        entry[0] = 0;
        entry[1] = 0xff;
        GCALL(0x449960, binding_default)(player, index, 0);
        return 0;
    }
    uint32_t k;
    if (kind == 0 || kind == 1 || kind == 3)
        k = g_config[player * 0x90 + 0x1a1];
    else
        k = (uint32_t)key;
    entry[2] = (uint8_t)k;
    entry[0] = 2;
    entry[1] = (uint8_t)kind;
    entry[3] = (uint8_t)extra;
    GCALL(0x449960, binding_default)(player, index, 0);
    return 0;
}

/* 0x4526f0: sets entry's type byte (0..2, else 0). Returns -1 if there is
 * no such entry. */
int32_t GAMEAPI control_type_set(int32_t player, int32_t index, int32_t type)
{
    uint8_t *entry = GCALL(0x449c40, binding_saved_get)(player, index);
    if (entry == 0)
        return -1;
    if (type < 0 || type >= 3)
        type = 0;
    entry[0] = (uint8_t)type;
    GCALL(0x449960, binding_default)(player, index, 0);
    return 0;
}

/* 0x452900: sets a player's controls byte (config +0x1a1 of player * 0x90)
 * and refreshes all 27 entries of the player. */
void GAMEAPI control_button_set(int32_t player, int32_t value)
{
    if (player < 0 || player >= 4)
        return;
    g_config[player * 0x90 + 0x1a1] = (uint8_t)value;
    for (int32_t i = 0; i < 0x1b; i++)
        GCALL(0x449960, binding_default)(player, i, 1);
}

/* ===== 0x46b510 / 0x46b7b0 / 0x474b70: nested tests of a pair of
 * values with 0x46b240 / 0x46b330 (-1 = no). */
int32_t GAMEAPI pair_lookup(int32_t a, int32_t b)
{
    int32_t result = -1;
    if (GCALL(0x46b240, reg_key_open)((uint32_t)a) != -1) {
        if (GCALL(0x46b330, reg_value_type_query)((uint32_t)b) != -1)
            result = GI32(0x4b197c);
        GCALL(0x46b200, reg_key_close)();
    }
    return result;
}

int32_t GAMEAPI pair_lookup_ok(int32_t a, int32_t b)
{
    return pair_lookup(a, b) != -1;
}

int32_t GAMEAPI pair_lookup_default(void)
{
    return pair_lookup_ok(0x4ac754, 0x4b2368);
}

/* ===== joystick binding capture ===== */

/* 0x48a230: 0x48a170 (axis edge). */
int32_t GAMEAPI joy_axis_edge_call(int32_t joy, int32_t axis)
{
    return GCALL(0x48a170, fe_joy_axis_edge)(joy, axis);
}

/* 0x48a2f0: edge detection of a POV direction (0, 0x10, 0x20, 0x30):
 * the latch byte at 0x4d96e4 + 0x90 * joy + dir / 16. Returns 0 pressed
 * (new), 2 held, 3 idle, 1 released, -1 for other directions. */
int32_t GAMEAPI joy_pov_edge(int32_t joy, uint32_t dir)
{
    uint8_t *latch;
    if (dir > 0x30)
        return -1;
    switch (dir) {
    case 0: latch = (uint8_t *)(uintptr_t)(uint32_t)(0x4d96e4 + joy * 0x90); break;
    case 0x10: latch = (uint8_t *)(uintptr_t)(uint32_t)(0x4d96e5 + joy * 0x90); break;
    case 0x20: latch = (uint8_t *)(uintptr_t)(uint32_t)(0x4d96e6 + joy * 0x90); break;
    case 0x30: latch = (uint8_t *)(uintptr_t)(uint32_t)(0x4d96e7 + joy * 0x90); break;
    default: return -1;
    }
    uint8_t was = *latch;
    if (GCALL(0x4897c0, joy_pov_dir)(joy, (int32_t)dir) != 0) {
        if (was != 0)
            return 2;
        *latch = 1;
        return 0;
    }
    if (was == 0)
        return 3;
    *latch = 0;
    return 1;
}

int32_t GAMEAPI joy_pov_edge_0(int32_t joy)
{
    return joy_pov_edge(joy, 0);
}

int32_t GAMEAPI joy_pov_edge_1(int32_t joy)
{
    return joy_pov_edge(joy, 0x10);
}

int32_t GAMEAPI joy_pov_edge_2(int32_t joy)
{
    return joy_pov_edge(joy, 0x20);
}

int32_t GAMEAPI joy_pov_edge_3(int32_t joy)
{
    return joy_pov_edge(joy, 0x30);
}

/* 0x48a420: waits for something pressed on joystick `joy` to bind:
 * 0x5d6234 = joystick, 0x5d6238 = what is being scanned (1 axes, 3 POV,
 * 0 buttons, -1 nothing), 0x5d623c = the code found: axis * 16 | 6
 * (negative) / | 7 (positive), the POV direction 0 / 0x10 / 0x20 / 0x30,
 * or the button number. Returns 1 if found. */
int32_t GAMEAPI input_capture_binding(int32_t joy)
{
    if (joy < 0 || joy >= GCALL(0x48a010, joy_device_count_thunk)()) {
        GI32(0x5d6238) = -1;
        return 0;
    }
    GI32(0x5d6234) = joy;
    GI32(0x5d6238) = 1;
    for (int32_t axis = 0; axis < 12; axis++) {
        int32_t r = GCALL(0x48a230, joy_axis_edge_call)(joy, axis);
        if (r == 0) {
            GI32(0x5d623c) = (axis << 4) | 6;
            return 1;
        }
        if (r == 2) {
            GI32(0x5d623c) = (axis << 4) | 7;
            return 1;
        }
    }
    GI32(0x5d6238) = 3;
    if (GCALL(0x48a3e0, joy_pov_edge_0)(joy) == 0) {
        GI32(0x5d623c) = 0;
        return 1;
    }
    if (GCALL(0x48a3f0, joy_pov_edge_1)(joy) == 0) {
        GI32(0x5d623c) = 0x10;
        return 1;
    }
    if (GCALL(0x48a400, joy_pov_edge_2)(joy) == 0) {
        GI32(0x5d623c) = 0x20;
        return 1;
    }
    if (GCALL(0x48a410, joy_pov_edge_3)(joy) == 0) {
        GI32(0x5d623c) = 0x30;
        return 1;
    }
    GI32(0x5d6238) = 0;
    int32_t button = 0;
    if (GCALL(0x48a040, fe_joy_button_count)(joy) > 0) {
        do {
            if (GCALL(0x48a270, fe_joy_button_edge)(joy, button) == 1) {
                GI32(0x5d623c) = button; /* sic: not shifted like the axes */
                return 1;
            }
            button++;
        } while (button < GCALL(0x48a040, fe_joy_button_count)(joy));
    }
    GI32(0x5d6238) = -1;
    return 0;
}

/* 0x48a560: binding capture over every joystick: 1 on the first hit. */
int32_t GAMEAPI input_capture_any(void)
{
    int32_t joy = 0;
    if (GCALL(0x48a010, joy_device_count_thunk)() > 0) {
        do {
            if (input_capture_binding(joy) != 0)
                return 1;
            joy++;
        } while (joy < GCALL(0x48a010, joy_device_count_thunk)());
    }
    return 0;
}

/* 0x48a590: the joystick record (0x43c bytes at 0x4d74a0), 0 past the count. */
uint8_t *GAMEAPI joy_record(int32_t joy)
{
    if (joy >= GCALL(0x48a010, joy_device_count_thunk)())
        return 0;
    return (uint8_t *)(uintptr_t)(0x4d74a0 + (uint32_t)joy * 0x43c);
}

/* 0x488d60: word 0 of the 0x224-byte record joy of the table at 0x4d667c
 * (0 past the count at 0x4d623c). */
int32_t GAMEAPI joy_device_value(int32_t joy)
{
    if (joy >= GI32(0x4d623c))
        return 0;
    return GI32(0x4d667c + (uint32_t)joy * 0x224);
}

/* 0x4890b0: DirectInput object enumeration callback (stdcall): counts an
 * axis object (flag 2 at +0x18) in joystick `joy`'s table at 0x4d75f0 (up to
 * 12), sets its range through 0x489500 and the dead zone / saturation
 * through 0x4891f0 / 0x489180. Returns 1 = go on. */
int32_t STDCALL input_enum_joystick_object(const uint8_t *object, int32_t joy)
{
    uint32_t base = 0x4d75f0 + (uint32_t)joy * 0x43c;
    if (!(U8(object, 0x18) & 2))
        return 1;
    int32_t n = GI32(base);
    if (n >= 0xc)
        return 1;
    GI32(base) = n + 1;
    uint8_t scratch[0x18]; /* sic: left uninitialised by the original; 0x489500 fills it */
    for (int i = 0; i < 0x18; i++)
        scratch[i] = 0;
    if (GCALL(0x489500, joy_property_get)(joy, (uint32_t)(uintptr_t)scratch, 4, U32(object, 0x14)) == 0)
        return 1;
    uint32_t slot = ((uint32_t)joy * 0x10f + (uint32_t)n) * 4;
    G32(slot + 0x4d75f8) = 1;
    G32(slot + 0x4d7628) = U32(object, 0x14);
    G32(slot + 0x4d7658) = (U8(object, 0x1c) & 1) ? 1 : 0;
    GCALL(0x4891f0, joy_range_set)(joy, U32(object, 0x14));
    GCALL(0x489180, joy_deadzone_set)(joy, 0, U32(object, 0x14));
    return 1;
}

/* 0x48ac40: DirectInput enumeration of joystick names (stdcall, 4
 * arguments, 1 returned): when the GUID is the one at 0x4a4240, `names` (a
 * list of zero-terminated strings ending in an empty one) is copied, each
 * cut to 63 characters, into the table 0x5d5cd8 (64 bytes each, count
 * 0x5d5cd4). */
int32_t STDCALL joy_names_collect(const uint8_t *guid, uint32_t a1, uint8_t *names, uint32_t a3)
{
    (void)a1;
    (void)a3;
    const uint32_t *g = (const uint32_t *)guid;
    const uint32_t *want = (const uint32_t *)0x4a4240;
    if (g[0] != want[0] || g[1] != want[1] || g[2] != want[2] || g[3] != want[3])
        return 1;
    if (names[0] == 0)
        return 1;
    do {
        uint32_t len = 0;
        while (names[len] != 0)
            len++;
        uint8_t *next = names + len + 1;
        if (len >= 0x40) {
            names[0x3f] = 0;
            len = 0x3f;
        }
        uint8_t *dst = (uint8_t *)(uintptr_t)(0x5d5cd8 + (G32(0x5d5cd4) << 6));
        for (uint32_t i = 0; i <= len; i++)
            dst[i] = names[i];
        G32(0x5d5cd4) = G32(0x5d5cd4) + 1;
        names = next;
    } while (names[0] != 0);
    return 1;
}

/* ===== 0x488470: fills the video mode combo box of a dialog: reset it,
 * list the modes (0x4d5fd4 of them, 3 ints each at 0x4d5fe8: width,
 * height, depth) with their text, and select the current mode
 * (0x4acc38.., else 640x480x16, else the first), by SendDlgItemMessageA. */
void GAMEAPI video_mode_list_fill(int32_t dlg, int32_t item)
{
    int32_t selected = -1;
    int32_t fallback = 0;
    char fmt[0x80];
    char text[0x70];
    SendDlgItemMessageA(GMEM(dlg), item, 0x14b, 0, 0);
    if (GCALL(0x487160, dd_modes_enum)() == 0)
        return;
    int32_t n = GI32(0x4d5fd4);
    SendDlgItemMessageA(GMEM(dlg), item, 0x161, n, (int32_t)((uint32_t)n * 260u));
    int32_t i = 0;
    if (GI32(0x4d5fd4) > 0) {
        const int32_t *mode = (const int32_t *)0x4d5fe8;
        do {
            if (GCALL(0x42e6d0, text_line_get)((uint8_t *)fmt, 0x80, (const char *)0x4b223c, 0) != -1) {
                GCALL(0x4979f0, msvc_sprintf)(text, fmt, mode[-1], mode[0], mode[1]);
                SendDlgItemMessageA(GMEM(dlg), item, 0x143, 0, (LPARAM)(uintptr_t)text);
            }
            if (mode[-1] == GI32(0x4acc38) && mode[0] == GI32(0x4acc3c) && mode[1] == GI32(0x4acc40))
                selected = i;
            if (mode[-1] == 0x280 && mode[0] == 0x1e0 && mode[1] == 0x10)
                fallback = i;
            i++;
            mode += 3;
        } while (i < GI32(0x4d5fd4));
    }
    if (selected == -1)
        selected = fallback;
    SendDlgItemMessageA(GMEM(dlg), item, 0x14e, selected, 0);
}

/* ===== x87: menu sliders ===== */

/* 0x447d80: steps the current menu item's float value up (cmd 0x18) or
 * down (0x17) by its step (or its coarse step when 0x4beb38 is set, divided
 * by the item's +0x53c scale), clamps it to [+0x534, +0x538] and, if it
 * changed, calls the item's callback (+0x514) and plays the click. */
#define MENU_ITEM(i) (GP(0x4cb4b8) + (uint32_t)(i) * 0x730)
void GAMEAPI menu_slider_step(int32_t cmd)
{
    int8_t sel = (int8_t)G8(0x4cb588);
    if (sel < 0)
        return;
    if ((int32_t)sel >= GI32(0x4cb49c))
        return;
    uint8_t *item = MENU_ITEM(sel);
    uint32_t value = U32(item, 0x540);
    uint32_t step;
    if (G32(0x4beb38) != 0)
        step = U32(item, 0x548);
    else
        step = U32(item, 0x544);
    if (!(fcmp(step, G32(0x4a3318)) & 0x40))
        step = fdiv_s(U32(item, 0x53c), step);
    if (cmd == 0x17)
        value = fsub_s(value, step);
    else if (cmd == 0x18)
        value = fadd_s(step, value);
    uint32_t edx;
    if (fcmp(value, U32(item, 0x534)) & 1) {
        edx = U32(item, 0x534);
        value = edx;
    } else {
        edx = value;
    }
    if (!(fcmp(value, U32(item, 0x538)) & 0x41)) {
        edx = U32(item, 0x538);
        value = edx;
    }
    if (fcmp(U32(item, 0x540), value) & 0x40)
        return;
    U32(item, 0x540) = edx;
    G16(0x4af610) = 0xffff;
    int8_t sel2 = (int8_t)G8(0x4cb588);
    uint8_t *item2 = MENU_ITEM(sel2);
    uint32_t callback = U32(item2, 0x514);
    if (callback != 0)
        ((void(GAMEAPI *)(uint32_t))(uintptr_t)callback)(edx); /* a float slider's: the value's bits */
    GCALL(0x472b70, voice_play_plain_volume)(0x19, 0x400, 0x36b0, (int16_t)G16(0x4af610));
}

/* 0x44cb10: starts a float slider item in the item template (0x4c9df8):
 * its caption (text a0 / a1), the box size from texture 0x2b, the value
 * range [lo, hi] (swapped if given reversed), the current value a4 clamped
 * into the range, the steps a5 / a6 and the callback a7, then commits it
 * (0x4482a0 with kind 0x86). */
void GAMEAPI menu_slider_begin(int32_t section, int32_t index, uint32_t lo, uint32_t hi, uint32_t cur,
                               uint32_t step, uint32_t coarse, int32_t callback, int32_t font)
{
    char text[0x200];
    uint32_t low;
    uint32_t high;
    uint32_t clamped;
    GCALL(0x4481a0, menu_text_get)(text, 0x200, (const char *)GD_P(section), index);
    GCALL(0x448260, menu_item_label)((uint8_t *)0x4c9e04, text);
    G32(0x4c9e38) = (uint32_t)GCALL(0x431670, frontend_texture_width)(0x2b) + 0x14;
    G32(0x4c9e2c) = (uint32_t)GCALL(0x4316a0, frontend_texture_height)(0x2b);
    if (fcmp(lo, hi) & 0x41) {
        high = hi;
        low = lo;
    } else {
        high = lo;
        low = hi;
        hi = high;
        lo = low;
    }
    if (fcmp(cur, lo) & 1) {
        clamped = low;
        cur = clamped;
    } else {
        clamped = cur;
    }
    if (!(fcmp(cur, hi) & 0x41))
        clamped = high;
    uint32_t range = fsub_s(hi, lo);
    G32(0x4ca30c) = (uint32_t)callback;
    G32(0x4ca330) = high;
    G32(0x4ca338) = clamped;
    G32(0x4ca334) = range;
    G32(0x4ca32c) = low;
    G32(0x4ca33c) = step;
    G32(0x4ca340) = coarse;
    GCALL(0x4482a0, menu_item_commit)(0x86, 4, (uint32_t)font, 4);
}

/* 0x451110: builds the sound options menu: music and effects volume
 * sliders (current values from the race record's +0xa7e / +0xa7c), the
 * choices and toggles around them. Items are made by the menu builders
 * (menu_add_*), the callbacks are the code addresses passed in. */
void GAMEAPI sound_menu_build(void)
{
    GCALL(0x448a00, menu_set_title)((const char *)0x4b04cc, 0);
    uint8_t *config = g_config;
    if (config[0x410] == 0) {
        int32_t r = GCALL(0x4977b0, cd_volume_available)();
        G8(0x4c9dfd) = 0;
        if (r == 0)
            G8(0x4c9dfd) = 1;
    } else {
        G8(0x4c9dfd) = 1;
    }
    uint8_t *race = g_race;
    uint32_t music = fild_f((int32_t)S16(race, 0xa7e));
    GCALL(0x44cb10, menu_slider_begin)(0, -1, 0, 0x467ffc00, music, 0x41800000, 0x40800000, 0x451300, 0x36);
    GCALL(0x4516e0, audio_shuffle_item)();
    GCALL(0x44c980, menu_add_label)((const char *)0x4cb7f8, -3);
    race = g_race;
    uint32_t effects = fild_f((int32_t)S16(race, 0xa7c));
    GCALL(0x44cb10, menu_slider_begin)(0, -1, 0, 0x467ffc00, effects, 0x41800000, 0x40800000, 0x451330, 0x36);
    G8(0x4c9dfb) = 1;
    GCALL(0x448140, menu_add_item)(0, -1, 0x6d, 1, 0x36);
    int32_t r = GCALL(0x470e70, sound_voices_crowded)();
    G8(0x4c9dfd) = (uint8_t)(r == 0);
    GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, GD_CB(0x451400), 0x36);
    if (GI32(0x4b1c88) != 0) {
        G8(0x4c9dfd) = 1;
    } else {
        int32_t r2 = GCALL(0x470eb0, sound_voice_flag8)();
        G8(0x4c9dfd) = 0;
        if (r2 == 0)
            G8(0x4c9dfd) = 1;
    }
    GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, GD_CB(0x4513a0), 0x36);
    GCALL(0x44c9d0, menu_add_slider)(0, -1, 0, -2, GD_CB(0x451590), 0x36);
    GCALL(0x44c980, menu_add_label)((const char *)0x4cb7f8, -3);
    GCALL(0x451630, audio_speaker_item)();
    GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, GD_CB(0x451360), 0x36);
    G8(0x4c9dfc) = 1;
    GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, GD_CB(0x451450), 0x36);
    GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, GD_CB(0x451490), 0x36);
    GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, GD_CB(0x4514d0), 0x36);
    GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, GD_CB(0x451510), 0x36);
    G8(0x4c9dfb) = 1;
    GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, GD_CB(0x451550), 0x36);
}

/* ===== x87: EAX voice fade ===== */

/* 0x471580: moves voice `voice`'s EAX send level (float at 0x6cb530 +
 * 60 * voice) one `step` towards `level` (clamped to 0..1), without
 * overshooting, and hands the new value to 0x4714c0. Returns 1 when the
 * level then equals the target, 0 if not, -1 if EAX is off, the voice is out
 * of range or 0x4714c0 fails. (The original's step compare order matters
 * for NaN: all compares are done in x87.) */
int32_t GAMEAPI eax_voice_fade_step(int32_t voice, uint32_t level, uint32_t step)
{
    if (GI32(0x4d4524) == 0 || GI32(0x4d4528) == 0)
        return -1;
    if (voice < 0 || voice >= 0x40)
        return -1;
    uint32_t ecx;
    if (fcmp(level, G32(0x4a3318)) & 1) {
        level = 0;
        ecx = 0;
    } else if (!(fcmp(level, G32(0x4a331c)) & 0x41)) {
        level = 0x3f800000;
        ecx = level;
    } else {
        ecx = level;
    }
    uint32_t slot = 0x6cb530 + (uint32_t)voice * 0x3c;
    uint32_t cur = G32(slot);
    if (fcmp(cur, level) & 0x40)
        return 1;
    if (fcmp(cur, level) & 1) {
        cur = fadd_s(cur, step);
        if (!(fcmp(cur, level) & 0x41))
            cur = ecx;
        else
            ecx = cur;
    } else {
        cur = fsub_s(cur, step);
        if (fcmp(cur, level) & 1)
            cur = ecx;
        else
            ecx = cur;
    }
    if (fcmp(cur, G32(0x4a3318)) & 1)
        ecx = 0;
    else if (!(fcmp(cur, G32(0x4a331c)) & 0x41))
        ecx = 0x3f800000;
    if (GCALL(0x4714c0, eax_voice_reverb_set)(voice, ecx) == -1)
        return -1;
    return (fcmp(G32(slot), level) & 0x40) ? 1 : 0;
}

/* ===== x87: 3D placement of a voice (0x472bb0) ===== */

#if !defined(__i386__)
/* x86-64: the C runtime's x87 math (sqrt 0x4981d4, sin 0x497b74, cos
 * 0x497ac4, atan2 0x498820, fmod 0x4987e0) as the runtime does it: the x87
 * instruction at the caller's precision, rounded to nearest, the result
 * through a double. long double arithmetic is x87 arithmetic. */
typedef long double pc_ld;
enum { GD_SQRT, GD_SIN, GD_COS };

static inline uint16_t gd_cw_near(uint16_t *cw)
{
    __asm__ volatile("fnstcw %0" : "=m"(*cw));
    return (uint16_t)((*cw & 0x300) | 0x7f);
}

static double gd_crt1(int op, double x)
{
    uint16_t cw, near = gd_cw_near(&cw);
    double r;

    if (op == GD_SQRT)
        __asm__ volatile("fldcw %1\n\tfldl %2\n\tfsqrt\n\tfstpl %0\n\tfldcw %3"
                         : "=m"(r) : "m"(near), "m"(x), "m"(cw) : "st");
    else if (op == GD_SIN)
        __asm__ volatile("fldcw %1\n\tfldl %2\n\tfsin\n\tfstpl %0\n\tfldcw %3"
                         : "=m"(r) : "m"(near), "m"(x), "m"(cw) : "st");
    else
        __asm__ volatile("fldcw %1\n\tfldl %2\n\tfcos\n\tfstpl %0\n\tfldcw %3"
                         : "=m"(r) : "m"(near), "m"(x), "m"(cw) : "st");
    return r;
}

static double gd_atan2(double y, double x)
{
    uint16_t cw, near = gd_cw_near(&cw);
    double r;

    __asm__ volatile("fldcw %1\n\tfldl %2\n\tfldl %3\n\tfpatan\n\tfstpl %0\n\tfldcw %4"
                     : "=m"(r) : "m"(near), "m"(y), "m"(x), "m"(cw) : "st", "st(1)");
    return r;
}

static double gd_fmod(double x, double y)
{
    uint16_t cw, near = gd_cw_near(&cw);
    double r;

    __asm__ volatile("fldcw %1\n\tfldl %3\n\tfldl %2\n"
                     "1:\n\tfprem\n\tfnstsw %%ax\n\ttestw $0x400, %%ax\n\tjnz 1b\n\t"
                     "fstp %%st(1)\n\tfstpl %0\n\tfldcw %4"
                     : "=m"(r) : "m"(near), "m"(x), "m"(y), "m"(cw) : "st", "st(1)", "ax", "cc");
    return r;
}

static inline float gd_float(uint32_t bits)
{
    float f;

    memcpy(&f, &bits, 4);
    return f;
}

static inline double gd_double(uint32_t lo, uint32_t hi)
{
    uint64_t q = (uint64_t)hi << 32 | lo;
    double d;

    memcpy(&d, &q, 8);
    return d;
}

static inline double gd_mem_double(uint32_t a)
{
    double d;

    memcpy(&d, (const void *)(uintptr_t)a, 8);
    return d;
}

/* fstp dword / qword: rounded by the control word. */
static inline uint32_t gd_fstps(pc_ld v)
{
    uint32_t r;
    __asm__ volatile("fstps %0" : "=m"(r) : "t"(v) : "st");
    return r;
}

static inline double gd_fstpl(pc_ld v)
{
    double r;
    __asm__ volatile("fstpl %0" : "=m"(r) : "t"(v) : "st");
    return r;
}
#endif

/* result = (float)crt(arg), arg a double passed on the stack as lo / hi
 * (sqrt 0x4981d4, sin 0x497b74, cos 0x497ac4). */
static inline uint32_t crt_d1(uint32_t fn, uint32_t lo, uint32_t hi)
{
#if defined(__i386__)
    uint32_t r;
    __asm__ volatile("pushl %[hi]\n\t"
                     "pushl %[lo]\n\t"
                     "call *%[fn]\n\t"
                     "addl $8,%%esp\n\t"
                     "fstps %[r]"
                     : [r] "=m"(r)
                     : [fn] "r"(fn), [lo] "r"(lo), [hi] "r"(hi)
                     : "eax", "ecx", "edx", "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)",
                       "st(6)", "st(7)");
    return r;
#else
    double d = gd_double(lo, hi);

    return gd_fstps((pc_ld)(fn == 0x4981d4 ? gd_crt1(GD_SQRT, d) : fn == 0x497b74 ? gd_crt1(GD_SIN, d)
                                                                                  : gd_crt1(GD_COS, d)));
#endif
}

/* Y*Y + XX + ZZ as a double (fld; fmul; fadd; fadd; fstp qword). */
static inline uint64_t sum3_d(uint32_t y, uint32_t xx, uint32_t zz)
{
    uint64_t d;
    __asm__ volatile("flds %1\n\tfmuls %1\n\tfadds %2\n\tfadds %3\n\tfstpl %0"
                     : "=m"(d)
                     : "m"(y), "m"(xx), "m"(zz)
                     : "st");
    return d;
}

/* XX + ZZ as a double. */
static inline uint64_t sum2_d(uint32_t xx, uint32_t zz)
{
    uint64_t d;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstpl %0" : "=m"(d) : "m"(xx), "m"(zz) : "st");
    return d;
}

/* ref * k1 / k2 (fld; fmul; fdiv; fstp): 0x4b1ea4 * 4096.0 / 1048576.0. */
static inline uint32_t mul_div_s(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfdivs %3\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b), "m"(c) : "st");
    return r;
}

/* 810.0 - atan2(y, x) * 180 / pi, for floats x and y (the doubles are
 * pushed y first at the lower address, 0x498820). */
static inline uint32_t atan2_810(uint32_t x, uint32_t y)
{
#if defined(__i386__)
    uint32_t r;
    __asm__ volatile("flds %[y]\n\t"
                     "flds %[x]\n\t"
                     "subl $16,%%esp\n\t"
                     "fstpl 8(%%esp)\n\t"
                     "fstpl (%%esp)\n\t"
                     "movl $0x498820,%%eax\n\t"
                     "call *%%eax\n\t"
                     "fmull 0x4a3440\n\t"
                     "addl $16,%%esp\n\t"
                     "fdivl 0x4a3438\n\t"
                     "fsubrl 0x4a3430\n\t"
                     "fstps %[r]"
                     : [r] "=m"(r)
                     : [x] "m"(x), [y] "m"(y)
                     : "eax", "ecx", "edx", "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)",
                       "st(6)", "st(7)");
    return r;
#else
    pc_ld t = (pc_ld)gd_atan2((double)gd_float(y), (double)gd_float(x)) * (pc_ld)gd_mem_double(0x4a3440);

    t = t / (pc_ld)gd_mem_double(0x4a3438);
    return gd_fstps((pc_ld)gd_mem_double(0x4a3430) - t);
#endif
}

/* fmod((double)v, 360.0) as a float; `v` is the sum azimuth [- matrix yaw +
 * 3780.0] (mat != 0) or the azimuth alone. */
static inline uint32_t fmod360(uint32_t azimuth)
{
#if defined(__i386__)
    uint32_t r;
    __asm__ volatile("flds %[a]\n\t"
                     "pushl $0x40768000\n\t"
                     "pushl $0\n\t"
                     "subl $8,%%esp\n\t"
                     "fstpl (%%esp)\n\t"
                     "movl $0x4987e0,%%eax\n\t"
                     "call *%%eax\n\t"
                     "addl $16,%%esp\n\t"
                     "fstps %[r]"
                     : [r] "=m"(r)
                     : [a] "m"(azimuth)
                     : "eax", "ecx", "edx", "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)",
                       "st(6)", "st(7)");
    return r;
#else
    return gd_fstps((pc_ld)gd_fmod((double)gd_float(azimuth), 360.0));
#endif
}

static inline uint32_t fmod360_rel(uint32_t azimuth, uint32_t yaw)
{
#if defined(__i386__)
    uint32_t r;
    __asm__ volatile("flds %[a]\n\t"
                     "fsubs %[y]\n\t"
                     "fadds 0x4a3428\n\t"
                     "pushl $0x40768000\n\t"
                     "pushl $0\n\t"
                     "subl $8,%%esp\n\t"
                     "fstpl (%%esp)\n\t"
                     "movl $0x4987e0,%%eax\n\t"
                     "call *%%eax\n\t"
                     "addl $16,%%esp\n\t"
                     "fstps %[r]"
                     : [r] "=m"(r)
                     : [a] "m"(azimuth), [y] "m"(yaw)
                     : "eax", "ecx", "edx", "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)",
                       "st(6)", "st(7)");
    return r;
#else
    pc_ld t = (pc_ld)gd_float(azimuth) - (pc_ld)gd_float(yaw);

    t = t + (pc_ld)gd_float(G32(0x4a3428));
    return gd_fstps((pc_ld)gd_fmod(gd_fstpl(t), 360.0));
#endif
}

/* 180 / pi: atan2(-Y, sqrt(dd)) * 180 / pi (the elevation), dd a double:
 * sqrt result stored as a double over the argument slot, -Y pushed below. */
static inline uint32_t elevation(uint32_t lo, uint32_t hi, const uint32_t *y)
{
#if defined(__i386__)
    uint32_t r;
    __asm__ volatile("pushl %[hi]\n\t"
                     "pushl %[lo]\n\t"
                     "movl $0x4981d4,%%eax\n\t"
                     "call *%%eax\n\t"
                     "fstpl (%%esp)\n\t"
                     "flds (%[y])\n\t"
                     "subl $8,%%esp\n\t"
                     "fchs\n\t"
                     "fstpl (%%esp)\n\t"
                     "movl $0x498820,%%eax\n\t"
                     "call *%%eax\n\t"
                     "fmull 0x4a3440\n\t"
                     "addl $16,%%esp\n\t"
                     "fdivl 0x4a3438\n\t"
                     "fstps %[r]"
                     : [r] "=m"(r)
                     : [lo] "r"(lo), [hi] "r"(hi), [y] "S"(y)
                     : "eax", "ecx", "edx", "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)",
                       "st(6)", "st(7)");
    return r;
#else
    double h = gd_crt1(GD_SQRT, gd_double(lo, hi));
    pc_ld t = (pc_ld)gd_atan2(gd_fstpl(-(pc_ld)gd_float(*y)), h) * (pc_ld)gd_mem_double(0x4a3440);

    return gd_fstps(t / (pc_ld)gd_mem_double(0x4a3438));
#endif
}

/* a * pi / 180 (fld; fmul qword; fdiv qword; fstp dword). */
static inline uint32_t deg_to_rad(uint32_t a)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmull 0x4a3438\n\tfdivl 0x4a3440\n\tfstps %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* The float as a double (fld; fstp qword). */
static inline uint64_t widen(uint32_t a)
{
    uint64_t d;
    __asm__ volatile("flds %1\n\tfstpl %0" : "=m"(d) : "m"(a) : "st");
    return d;
}

/* fld a; fmul b; fchs; fld c; fmul d; faddp: -(a*b) + c*d. */
static inline uint32_t neg_mul_add(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfchs\n\tflds %3\n\tfmuls %4\n\tfaddp %%st,%%st(1)\n\tfstps %0"
                     : "=m"(r)
                     : "m"(a), "m"(b), "m"(c), "m"(d)
                     : "st", "st(1)");
    return r;
}

/* fld a; fmul b; fld c; fmul d; faddp: a*b + c*d. */
static inline uint32_t mul_add(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tflds %3\n\tfmuls %4\n\tfaddp %%st,%%st(1)\n\tfstps %0"
                     : "=m"(r)
                     : "m"(a), "m"(b), "m"(c), "m"(d)
                     : "st", "st(1)");
    return r;
}

/* 0x472bb0: places voice `voice` (0x4700b0) and points its velocity
 * (0x470160) relative to the listener. pos / vel are 3 ints (scaled by
 * 0x5ff380 / 0x5ff2e0). The distance (times 0x4b1ea4 / 4096 / 1048576)
 * under 0x4b1e98 puts the voice dead ahead at 0x4b1ea0; else azimuth
 * (810 - atan2(z, x) in degrees, relative to the matrix yaw if there is a
 * matrix, 0..360, mirrored if the race flag +0x58b is set) and elevation
 * go in. `a4` is a float bit pattern: nonzero says "was close last time"
 * (hysteresis against 0x4b1e9c); it also comes back as the matrix yaw
 * that rotates the velocity. */
void GAMEAPI voice_3d_place(int32_t voice, const int32_t *pos, const int32_t *vel, const uint8_t *mat, uint32_t a4)
{
    uint32_t px = fmul_s(fild_f(pos[0]), G32(0x5ff380));
    uint32_t py = fmul_s(fild_f(pos[1]), G32(0x5ff380));
    uint32_t pz = fmul_s(fild_f(pos[2]), G32(0x5ff380));
    uint32_t scale = mul_div_s(G32(0x4b1ea4), G32(0x4a3448), G32(0x4a33d8));
    uint32_t zz = fmul_s(pz, pz);
    uint32_t xx = fmul_s(px, px);
    uint64_t dd = sum3_d(py, xx, zz);
    uint32_t dist = crt_d1(0x4981d4, (uint32_t)dd, (uint32_t)(dd >> 32));
    uint32_t r2 = fmul_s(dist, scale);
    uint64_t dxz = sum2_d(xx, zz);
    uint32_t lo = (uint32_t)dxz;
    uint32_t hi = (uint32_t)(dxz >> 32);
    uint32_t horiz = crt_d1(0x4981d4, lo, hi);
    scale = fmul_s(horiz, scale);
    uint32_t edi;
    uint32_t eax;
    uint32_t ecx;
    int pitch = 1;
    if (fcmp(r2, G32(0x4b1e98)) & 1) {
        eax = G32(0x4b1ea0);
        edi = 0;
        a4 = 0;
        ecx = 0;
        pitch = 0;
    } else if (a4 != 0 && (fcmp(scale, G32(0x4b1e9c)) & 1)) {
        a4 = 0;
        edi = 0;
    } else {
        uint32_t azimuth = atan2_810(px, pz);
        if (mat == 0) {
            a4 = 0;
            azimuth = fmod360(azimuth);
        } else {
            uint32_t fa = fild_f((int32_t)S16(mat, 0xc));
            uint32_t fb = fild_f((int32_t)S16(mat, 0x10));
            a4 = atan2_810(fa, fb);
            azimuth = fmod360_rel(azimuth, a4);
        }
        if (g_race[0x58b] != 0)
            azimuth = fsub_s(G32(0x4a3424), azimuth);
        edi = azimuth;
    }
    if (pitch) {
        ecx = elevation(lo, hi, &py);
        eax = r2;
    }
    GCALL(0x4700b0, snd_chan_position)((int8_t)voice, edi, eax, ecx);
    uint32_t vx = fmul_s(fild_f(vel[0]), G32(0x5ff2e0));
    uint32_t vy = fmul_s(fild_f(vel[1]), G32(0x5ff2e0));
    uint32_t vz = fmul_s(fild_f(vel[2]), G32(0x5ff2e0));
    uint32_t ang = deg_to_rad(a4);
    uint64_t angd = widen(ang);
    uint32_t sn = crt_d1(0x497b74, (uint32_t)angd, (uint32_t)(angd >> 32));
    uint32_t cs = crt_d1(0x497ac4, (uint32_t)angd, (uint32_t)(angd >> 32));
    uint32_t w1 = neg_mul_add(sn, vz, cs, vx);
    uint32_t w3 = mul_add(cs, vz, sn, vx);
    GCALL(0x470160, snd_chan_velocity)((int8_t)voice, w1, vy, w3);
}

/* ===== x87: channel refresh (0x470ef0) ===== */

/* Calls ftol (0x497a80) on `a * b`, returning the low byte (fld; fmul;
 * call). */
static inline uint32_t ftol_mul(uint32_t a, uint32_t b)
{
#if defined(__i386__)
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tmovl $0x497a80,%%eax\n\tcall *%%eax"
                     : "=a"(r)
                     : "m"(a), "m"(b)
                     : "ecx", "edx", "cc", "memory", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)",
                       "st(7)");
    return r;
#else
    uint16_t cw, chop;
    int64_t q;

    __asm__ volatile("fnstcw %0" : "=m"(cw));
    chop = (uint16_t)(cw | 0xc00);
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfldcw %3\n\tfistpll %0\n\tfldcw %4"
                     : "=m"(q)
                     : "m"(a), "m"(b), "m"(chop), "m"(cw)
                     : "st");
    return (uint32_t)q;
#endif
}

/* fi / (v * v) with the product left unrounded (fld fi; fld v; fmul v; fdivp). */
static inline uint32_t div_by_square(uint32_t fi, uint32_t v)
{
    uint32_t r;
    __asm__ volatile("flds %2\n\tfmuls %2\n\tfdivrs %1\n\tfstps %0" : "=m"(r) : "m"(fi), "m"(v) : "st");
    return r;
}

#define CH_BASE 0x6cb500u
#define CH(i) ((uint8_t *)(uintptr_t)(CH_BASE + (uint32_t)(i) * 0x3c))

/* 0x470ef0: once a frame, refreshes the sound channel table (64 entries of
 * 0x3c bytes at 0x6cb500: +0 flags (1 playing, 2 keep, 4 fixed, 8 paused,
 * 0x10 positional), +1 voice id, +2 countdown, +4 next in priority list,
 * +5 rank, +0xc int, +0x14 float, +0x28 / +0x2c priority, +0x30 float
 * level): finished channels are dropped (QMixer IsChannelDone), the others
 * sorted into a list by priority (descending), the first 0x4b1c74 of them
 * resumed and the rest paused or flushed; sets the EAX room effect by
 * whether a loud near enough channel is playing. With config +0x411 the
 * table is printed (0x4963c0). Returns -1 if EAX is off. */
int32_t GAMEAPI sound_channels_refresh(void)
{
    int8_t head;
    uint32_t i;
    if (GI32(0x4d4524) == 0)
        return -1;
    if (GI32(0x4d4528) == 0)
        return -1;
    head = -1;
    for (i = 0; i < 64; i++) {
        uint8_t *ch = CH(i);
        ch[5] = 0xff;
        if (ch[1] == 0)
            continue;
        if ((int8_t)ch[2] > 0)
            ch[2] = (uint8_t)(ch[2] - 1);
        if (!(ch[0] & 1))
            continue;
        if (ch[2] == 0) {
            if (QSWaveMixIsChannelDone(GI32(0x4d453c), i) != 0) {
                ch[0] &= 0xfe;
                continue;
            }
        }
        if (ch[0] & 4)
            continue;
        if (!(ch[0] & 0x10))
            continue;
        uint32_t v = U32(ch, 0x14);
        uint32_t w;
        uint32_t ecx = v;
        if (!(fcmp(v, G32(0x4b1c94)) & 1)) {
            w = 0;
        } else {
            if (fcmp(v, G32(0x4b1c90)) & 1) {
                ecx = G32(0x4b1c90);
                v = ecx;
            }
            uint32_t fi = fild_f(I32(ch, 0xc));
            w = div_by_square(fi, v);
        }
        U32(ch, 0x2c) = w;
        U32(ch, 0x28) = ecx;
        int8_t *link = &head;
        int8_t cur = head;
        while (cur != -1) {
            uint8_t *c = CH(cur);
            if (!(fcmp(w, U32(c, 0x2c)) & 0x41))
                break;
            link = (int8_t *)(c + 4);
            cur = (int8_t)c[4];
        }
        ch[4] = (uint8_t)*link;
        *link = (int8_t)i;
    }
    int32_t found = 0;
    uint32_t rank = 0;
    if (head != -1) {
        int32_t cur = head;
        do {
            uint8_t *ch = CH(cur);
            ch[5] = (uint8_t)rank;
            uint8_t flags = ch[0];
            if ((int32_t)rank < GI32(0x4b1c74)) {
                if (flags & 8) {
                    QSWaveMixRestartChannel(GI32(0x4d453c), cur, 0);
                    ch[0] &= 0xf7;
                }
                if (found == 0) {
                    if (!(fcmp(U32(ch, 0x30), G32(0x4a341c)) & 0x41)) {
                        if (fcmp(U32(ch, 0x28), G32(0x4b1c9c)) & 0x41) {
                            if (!(fcmp(U32(ch, 0x2c), G32(0x4b1ca0)) & 1))
                                found = 1;
                        }
                    }
                }
            } else if (!(flags & 2)) {
                QSWaveMixFlushChannel(GI32(0x4d453c), cur, 0);
                ch[0] &= 0xfe;
                ch[2] = 0;
            } else if (!(flags & 8)) {
                ch[0] = flags | 8;
                QSWaveMixPauseChannel(GI32(0x4d453c), cur, 0);
            }
            cur = (int8_t)ch[4];
            rank++;
        } while (cur != -1);
    }
    if (found != 0) {
        if (G32(0x4d455c) == 0) {
            G32(0x4d455c) = 1;
            GCALL(0x4716c0, eax_environment_set)(0x15);
        }
    } else if (G32(0x4d455c) != 0) {
        G32(0x4d455c) = 0;
        GCALL(0x4716c0, eax_environment_set)(1);
    }
    if (g_config[0x411] != 0) {
        uint8_t b_state[0x200];
        uint8_t b_id[0x200];
        uint8_t b_rank[0x200];
        uint8_t b_level[0x200];
        uint8_t b_pad1[0x200];
        uint8_t b_pad2[0x200];
        for (i = 0; i < 0x200; i++) {
            b_state[i] = b_id[i] = b_rank[i] = b_level[i] = b_pad1[i] = b_pad2[i] = 0;
        }
        for (i = 0; i < 64; i++) {
            uint8_t *ch = CH(i);
            uint8_t flags = ch[0];
            uint8_t state;
            if (!(flags & 1)) {
                state = 0x20;
            } else if (flags & 4) {
                state = (flags & 8) ? 0x70 : 0x50;
            } else if (flags & 8) {
                state = 0x53;
            } else {
                state = (flags & 2) ? 0x4c : 0x58;
            }
            b_state[i] = state;
            uint32_t k = (uint32_t)(int32_t)(int8_t)ch[1] - 0x15;
            char id = '?';
            if (k <= 0x71) {
                switch (k) {
                case 0: id = '0'; break;
                case 1: id = '4'; break;
                case 0x14: id = '1'; break;
                case 0x15: id = '5'; break;
                case 0x34: id = '2'; break;
                case 0x35: id = '6'; break;
                case 0x70: id = '3'; break;
                case 0x71: id = '7'; break;
                default: id = '?'; break;
                }
            }
            b_id[i] = (uint8_t)id;
            int8_t r = (int8_t)ch[5];
            uint8_t rc;
            if (r < 0 || r >= 0x20) {
                rc = 0x3f;
            } else {
                rc = (uint8_t)(r + 0x41);
                if (rc > 0x5a)
                    rc = (uint8_t)(rc + 6);
            }
            b_rank[i] = rc;
            uint8_t digit = (uint8_t)(0x30 - (uint8_t)ftol_mul(U32(ch, 0x30), G32(0x4a3418)));
            if (digit < 0x30 || digit > 0x39)
                digit = 0x3f;
            b_level[i] = digit;
        }
        GCALL(0x4963c0, debug_log_print)((const char *)0x4b1e4c, b_state, b_id, b_rank, b_level, b_pad1, b_pad2);
    }
    return 0;
}
