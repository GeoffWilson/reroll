/* Group XB: front-end callbacks Ghidra never made into functions (code
 * addresses 0x44daa0..0x450cf0 the original uses only as values: menu item
 * handlers given to the item builders 0x44c6d0 / 0x44c9d0 / 0x4480d0, the
 * entry actions in the item template's slots 0x4ca2f8 / 0x4ca2fc / 0x4ca300,
 * the repeat handler 0x4cb524 and the number-step items of 0x44ffd0).
 *
 * All are cdecl. The item handlers get (event, text buffer, ...): event 0x12
 * asks for the item's text into the buffer, 0x13 is a select, 0x14 a step
 * back, 0x15 / 0x16 a step on; 6 / 7 are the accept events. g_config is the
 * options block *0x4b2198, g_race the race block *0x4b1960.
 *
 * Everything outside the group is called by address. 0x450020 (in
 * xb_owned.txt) is not an entry: it is an instruction inside 0x450000, and
 * the value 0x450020 sits in the version resource (0x6d568c). */
#include "../runtime.h"
#include "physics.h"

#define A(p) ((uint32_t)(uintptr_t)(p))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define U8(a, off) (*(uint8_t *)(uintptr_t)((uint32_t)(a) + (uint32_t)(off)))
#define U16(a, off) (*(uint16_t *)(uintptr_t)((uint32_t)(a) + (uint32_t)(off)))
#define U32(a, off) (*(uint32_t *)(uintptr_t)((uint32_t)(a) + (uint32_t)(off)))
#define G8(a) U8(a, 0)
#define G16(a) U16(a, 0)
#define G32(a) U32(a, 0)
#define GI32(a) (*(int32_t *)(uintptr_t)(a))

#define g_config G32(0x4b2198)
#define g_race G32(0x4b1960)

typedef uint32_t(GAMEAPI *xb_fn0)(void);
typedef uint32_t(GAMEAPI *xb_fn1)(uint32_t a);
typedef uint32_t(GAMEAPI *xb_fn2)(uint32_t a, uint32_t b);
typedef uint32_t(GAMEAPI *xb_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef uint32_t(GAMEAPI *xb_fn4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef uint32_t(GAMEAPI *xb_fn5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef uint32_t(GAMEAPI *xb_fn6)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f);

#define CALL0(f) ((xb_fn0)(f))()
#define CALL1(f, a) ((xb_fn1)(f))(A(a))
#define CALL2(f, a, b) ((xb_fn2)(f))(A(a), A(b))
#define CALL3(f, a, b, c) ((xb_fn3)(f))(A(a), A(b), A(c))
#define CALL4(f, a, b, c, d) ((xb_fn4)(f))(A(a), A(b), A(c), A(d))
#define CALL5(f, a, b, c, d, e) ((xb_fn5)(f))(A(a), A(b), A(c), A(d), A(e))
#define CALL6(f, a, b, c, d, e, g) ((xb_fn6)(f))(A(a), A(b), A(c), A(d), A(e), A(g))

#define option_onoff_step(v, e) CALL3(0x447f20, v, 1, e)            /* (value, unused, event) -> new value */
#define menu_text_get(buf, section, index) CALL4(0x4481a0, buf, -1, section, index)
#define menu_pick_available(p, step) CALL2(0x448330, p, step)       /* steps *p past unavailable choices */
#define config_mirror_flag() CALL0(0x448380)
#define menu_choice_callback(e, buf, max, p, table) CALL5(0x450c00, e, buf, max, p, table)
#define option_number_step(e, buf, v, def, lo, hi) CALL6(0x450050, e, buf, v, def, lo, hi)
#define fe_repeat_select(on) CALL1(0x4475f0, on)
#define fe_repeat_updown(on) CALL1(0x4475d0, on)
#define fe_menu_goto(screen, mode) CALL2(0x4479d0, screen, mode)
#define fe_menu_back() CALL0(0x447ab0)
#define voice_play_plain(s, pitch, vol) CALL3(0x472a90, s, pitch, vol)
#define race_setting_trim() CALL0(0x4503c0)
#define blend_modes_choose() CALL0(0x41fda0)
#define render_flags_apply(a, b, c, d, e) CALL5(0x428a50, a, b, c, d, e)
#define gamma_refresh() CALL0(0x4219d0)
#define crt_sprintf(buf, fmt, v) CALL3(0x4979f0, buf, fmt, v)

/* An on/off option byte at *base + off through option_onoff_step;
 * returns 1 when it is now off. */
static int32_t xb_onoff(uint32_t base, uint32_t off, int32_t event)
{
    uint32_t v = option_onoff_step(U8(G32(base), off), event);
    U8(G32(base), off) = (uint8_t)v;
    return U8(G32(base), off) == 0;
}

/* A choice option byte g_config + off through menu_choice_callback. */
static int32_t xb_choice(uint32_t off, int32_t event, uint32_t buf, int32_t max, uint32_t table)
{
    int32_t v = U8(g_config, off);
    if (!menu_choice_callback(event, buf, max, &v, table))
        return 0;
    U8(g_config, off) = (uint8_t)v;
    return 1;
}

/* A three-way option byte g_config + off (0..2), texts section + 2.. */
static int32_t xb_three(uint32_t off, uint32_t section, int32_t event, uint32_t buf)
{
    switch (event) {
    case 0x12:
        if (U8(g_config, off) > 2)
            U8(g_config, off) = 2;
        menu_text_get(buf, section, U8(g_config, off) + 2);
        return 1;
    case 0x13:
        return 1;
    case 0x14:
        U8(g_config, off)--;
        if (U8(g_config, off) > 2)
            U8(g_config, off) = 2;
        return 1;
    case 0x15:
    case 0x16:
        U8(g_config, off)++;
        if (U8(g_config, off) > 2)
            U8(g_config, off) = 0;
        return 1;
    }
    return 0;
}

/* strcpy as the original's inline rep movs (forward). */
static void xb_strcpy(uint8_t *dst, const uint8_t *src)
{
    while ((*dst++ = *src++) != 0)
        ;
}

/* 0x44daa0: cheats menu: on/off race +0x58b. */
int32_t GAMEAPI fecb_cheat_58b(int32_t event)
{
    return xb_onoff(0x4b1960, 0x58b, event);
}

/* 0x44dae0: cheats menu: on/off race +0x58d. */
int32_t GAMEAPI fecb_cheat_58d(int32_t event)
{
    return xb_onoff(0x4b1960, 0x58d, event);
}

/* 0x44db20: cheats menu: on/off option +0x92. */
int32_t GAMEAPI fecb_onoff_92(int32_t event)
{
    return xb_onoff(0x4b2198, 0x92, event);
}

/* 0x44db60: the four-way option +0x80 (texts 0x4aff14), skipping the
 * unavailable ones (0x448330); a change runs config_mirror_flag. */
int32_t GAMEAPI fecb_choice4_80(int32_t event, uint32_t buf)
{
    int32_t v = U8(g_config, 0x80);
    switch (event) {
    case 0x12:
        menu_pick_available(&v, 1);
        menu_text_get(buf, 0x4aff14, v + 2);
        return 1;
    case 0x13:
        return 1;
    case 0x14:
        if (--v < 0)
            v = 3;
        menu_pick_available(&v, -1);
        U8(g_config, 0x80) = (uint8_t)v;
        config_mirror_flag();
        return 1;
    case 0x15:
    case 0x16:
        if (++v > 3)
            v = 0;
        menu_pick_available(&v, 1);
        U8(g_config, 0x80) = (uint8_t)v;
        config_mirror_flag();
        return 1;
    }
    return 0;
}

/* 0x44dc50: entry action: on accept (6/7) with select repeat, go to screen
 * 0x48. */
int32_t GAMEAPI fecb_goto_48(int32_t event)
{
    if (event < 6 || event > 7)
        return 0;
    if (!fe_repeat_select(1))
        return 0;
    fe_menu_goto(0x48, 1);
    return 1;
}

/* 0x44dc90: entry action: on accept with fewer than 2 (0x45f4f0) and select
 * repeat, the "can't" sound 0x18. */
int32_t GAMEAPI fecb_refuse_sound(int32_t event)
{
    if (event < 6 || event > 7)
        return 0;
    if (CALL0(0x45f4f0) >= 2)
        return 0;
    if (!fe_repeat_select(1))
        return 0;
    voice_play_plain(0x18, 0x800, 0x36b0);
    return 1;
}

/* 0x44dce0: choice option +0x8a, 4 values (0x4aff3c). */
int32_t GAMEAPI fecb_choice_8a(int32_t event, uint32_t buf)
{
    return xb_choice(0x8a, event, buf, 4, 0x4aff3c);
}

/* 0x44dd30: choice option +0x87 (0x4aff70): 3 values when 0x4cb584 is set,
 * fixed at 3 in a network game, else 2 values. */
int32_t GAMEAPI fecb_choice_87(int32_t event, uint32_t buf)
{
    if (G32(0x4cb584) != 0)
        return xb_choice(0x87, event, buf, 3, 0x4aff70);
    if (G32(0x4cb48c) != 0) {
        U8(g_config, 0x87) = 3;
        menu_text_get(buf, 0x4aff70, U8(g_config, 0x87) + 2);
        return 1;
    }
    return xb_choice(0x87, event, buf, 2, 0x4aff70);
}

/* 0x44de10: choice option +0x88 (0x4aff70): fixed at 3 in a network game,
 * else 2 values. */
int32_t GAMEAPI fecb_choice_88(int32_t event, uint32_t buf)
{
    if (G32(0x4cb48c) != 0) {
        U8(g_config, 0x88) = 3;
        menu_text_get(buf, 0x4aff70, U8(g_config, 0x88) + 2);
        return 1;
    }
    return xb_choice(0x88, event, buf, 2, 0x4aff70);
}

/* 0x44dea0: on/off option +0x34. */
int32_t GAMEAPI fecb_onoff_34(int32_t event)
{
    return xb_onoff(0x4b2198, 0x34, event);
}

/* 0x44dee0: on/off option +0x83. */
int32_t GAMEAPI fecb_onoff_83(int32_t event)
{
    return xb_onoff(0x4b2198, 0x83, event);
}

/* 0x44df20: on/off option +0x37. */
int32_t GAMEAPI fecb_onoff_37(int32_t event)
{
    return xb_onoff(0x4b2198, 0x37, event);
}

/* 0x44df60: on/off option +0x93. */
int32_t GAMEAPI fecb_onoff_93(int32_t event)
{
    return xb_onoff(0x4b2198, 0x93, event);
}

/* 0x44dfa0: text entry done: 0x20 bytes from the edit buffer 0x4c2ac0 to
 * option +0xa0. */
int32_t GAMEAPI fecb_store_a0(void)
{
    memcpy(PTR(g_config + 0xa0), PTR(0x4c2ac0), 0x20);
    return 1;
}

/* 0x44e040: text entry done: 11 bytes from 0x4c2f00 to option +0x120 (the
 * player name). */
int32_t GAMEAPI fecb_store_120(void)
{
    uint32_t p = g_config + 0x120;
    U32(p, 0) = G32(0x4c2f00);
    U32(p, 4) = G32(0x4c2f04);
    U16(p, 8) = G16(0x4c2f08);
    U8(p, 0xa) = G8(0x4c2f0a);
    return 1;
}

/* 0x44e080: text entry done: 0x41 bytes from 0x4c2484 to option +0x12b. */
int32_t GAMEAPI fecb_store_12b(void)
{
    memcpy(PTR(g_config + 0x12b), PTR(0x4c2484), 0x41);
    return 1;
}

/* 0x44e0b0: text entry done: 0x33 bytes from 0x4cb0dc to option +0x16c
 * (the chat line). */
int32_t GAMEAPI fecb_store_16c(void)
{
    memcpy(PTR(g_config + 0x16c), PTR(0x4cb0dc), 0x33);
    return 1;
}

/* 0x44e0e0: chat line accepted (6/7): "<name><0x4a7f94><line>" sent
 * (0x48d270), the line cleared, the screen rebuilt. The original's buffer is
 * 0x40 bytes on its stack, unchecked. */
int32_t GAMEAPI fecb_chat_send(int32_t event)
{
    uint8_t buf[0x40];
    uint8_t *e;
    uint32_t c;

    if (event != 6 && event != 7)
        return 1;
    c = g_config;
    xb_strcpy(buf, PTR(c + 0x120));
    for (e = buf; *e; e++)
        ;
    xb_strcpy(e, PTR(0x4a7f94));
    for (; *e; e++)
        ;
    xb_strcpy(e, PTR(c + 0x16c));
    CALL1(0x48d270, buf);
    U8(g_config, 0x16c) = 0;
    G32(0x4cb58c) = 1;
    return 1;
}

/* 0x44e1c0: text entry done: 0x20 bytes from 0x4ca82c to option +0xc0. */
int32_t GAMEAPI fecb_store_c0(void)
{
    memcpy(PTR(g_config + 0xc0), PTR(0x4ca82c), 0x20);
    return 1;
}

/* 0x44e1f0: entry action: the line being edited (0x4af708) = the entry's
 * value. */
void GAMEAPI fecb_edit_line(int32_t event, int32_t value)
{
    (void)event;
    G32(0x4af708) = (uint32_t)value;
}

/* 0x44e200: text entry done: chat macro 0x4af708 (0..9) = text, at option
 * +0xdce + 0x40 * n. */
int32_t GAMEAPI fecb_store_macro(const char *text)
{
    int32_t n = GI32(0x4af708);
    if (n >= 0 && n < 10)
        xb_strcpy(PTR(((uint32_t)n << 6) + g_config + 0xdce), (const uint8_t *)text);
    return 1;
}

/* 0x44e250: text entry done: system message 0x4af708 (0..0x1d) = text, at
 * option +0x104e + 0x40 * n. */
int32_t GAMEAPI fecb_store_message(const char *text)
{
    int32_t n = GI32(0x4af708);
    if (n >= 0 && n < 0x1e)
        xb_strcpy(PTR(((uint32_t)n << 6) + g_config + 0x104e), (const uint8_t *)text);
    return 1;
}

/* 0x44e460: entry draw hook: 0x4af60c = value & 0xff. */
void GAMEAPI fecb_value_60c(uint32_t value)
{
    G32(0x4af60c) = value & 0xff;
}

/* 0x44e590: world menu entry action: option +0x41 = the world, then
 * series_validate (tail jump). */
void GAMEAPI fecb_world_pick(int32_t event, int32_t world)
{
    U8(g_config, 0x41) = (uint8_t)world;
    CALL2(0x44e560, event, world);
}

/* 0x44e7e0: entry draw hook: option +0x1d = value when 0xf..0x12. */
void GAMEAPI fecb_set_1d(int32_t value)
{
    if (value >= 0xf && value <= 0x12)
        U8(g_config, 0x1d) = (uint8_t)value;
}

/* 0x44eb00: track menu entry action: series_track_set(option +0x41, track),
 * then track_menu_choice_fix (tail jump). */
void GAMEAPI fecb_track_pick(int32_t event, int32_t track)
{
    CALL2(0x448820, U8(g_config, 0x41), track);
    CALL2(0x44ea70, event, track);
}

/* 0x44eb20: track menu: accept (6/7) goes back (0x4cb0d0 == 2) or to screen
 * 0x4cb0d4 with mode 0x4cb0d0; 8/9 run 0x44ebb0(0x80, 0); each only with
 * select repeat. */
int32_t GAMEAPI fecb_track_menu(int32_t event)
{
    switch (event) {
    case 6:
    case 7:
        if (fe_repeat_select(1)) {
            int32_t mode = GI32(0x4cb0d0);
            if (mode == 2) {
                fe_menu_back();
                return 1;
            }
            fe_menu_goto(G32(0x4cb0d4), mode);
            return 1;
        }
        return 1;
    case 8:
    case 9:
        if (fe_repeat_select(1))
            CALL2(0x44ebb0, 0x80, 0);
        return 1;
    }
    return 0;
}

/* 0x44eeb0: repeat handler: up (0) / down (1) steps the screen's choice
 * byte option +0xbcc + screen through 0..*0x4ca528-1 (wrapping on the way
 * up, clamped), skipping the values whose bit is set in the mask *0x4c2910
 * (at most 0x20 tries); a change rebuilds the screen with sound 0x1b; 10
 * runs 0x448880. The original keeps the old value in its argument's slot. */
int32_t GAMEAPI fecb_step_choice(int32_t dir)
{
    uint8_t old, v;
    int32_t n, i;
    uint32_t mask;

    if (dir < 0)
        return 0;
    if (dir > 1) {
        if (dir == 10)
            CALL0(0x448880);
        return 0;
    }
    if (!fe_repeat_updown(1))
        return 1;
    old = U8(G32(0x4af64c) + g_config, 0xbcc);
    n = GI32(0x4ca528);
    mask = G32(0x4c2910);
    v = old;
    for (i = 0;;) {
        if (dir == 0) {
            v--;
        } else {
            v++;
            if ((int32_t)v >= n)
                v = 0;
        }
        if ((int32_t)v >= n)
            v = (uint8_t)(n - 1);
        if (!(mask & (1u << (v & 31))))
            break;
        if (++i >= 0x20)
            break;
    }
    U8(G32(0x4af64c) + g_config, 0xbcc) = v;
    if (v != old) {
        G32(0x4cb58c) = 1;
        voice_play_plain(0x1b, 0x800, 0x36b0);
    }
    return 1;
}

/* 0x44f510: name entry done: the profile name of slot *0x4c291c (low byte)
 * = the 10 bytes at 0x4ca854, then track_records_rename(track 0x4c24c5,
 * slot). */
int32_t GAMEAPI fecb_profile_rename(void)
{
    uint32_t p = CALL1(0x4053e0, G32(0x4c291c) & 0xff);
    U32(p, 0) = G32(0x4ca854);
    U32(p, 4) = G32(0x4ca858);
    U16(p, 8) = G16(0x4ca85c);
    CALL2(0x405860, G8(0x4c24c5), G32(0x4c291c) & 0xff);
    return 1;
}

/* 0x44fe60: entry draw hook: option +4 = mode 1..4; modes 1/2 set +0x76 =
 * 0/1 and +0x74 = 2, modes 3/4 set +0x74 = mode. */
void GAMEAPI fecb_set_mode(int32_t mode)
{
    if (mode < 1 || mode > 4)
        return;
    U8(g_config, 4) = (uint8_t)mode;
    switch (mode) {
    case 1:
        U8(g_config, 0x76) = 0;
        U8(g_config, 0x74) = 2;
        break;
    case 2:
        U8(g_config, 0x76) = 1;
        U8(g_config, 0x74) = 2;
        break;
    default:
        U8(g_config, 0x74) = (uint8_t)mode;
        break;
    }
}

/* 0x450000: number option +0x2f (default and minimum race +0x585 + 3, up to
 * 0x32). */
int32_t GAMEAPI fecb_number_2f(int32_t event, uint32_t buf)
{
    uint32_t lo = U8(g_race, 0x585) + 3;
    int32_t r = (int32_t)option_number_step(event, buf, U8(g_config, 0x2f), lo, lo, 0x32);
    if (r != -1)
        U8(g_config, 0x2f) = (uint8_t)r;
    return r != -1;
}

/* 0x450160: number option +0x77 (default 3, 1..0x32). */
int32_t GAMEAPI fecb_number_77(int32_t event, uint32_t buf)
{
    int32_t r = (int32_t)option_number_step(event, buf, U8(g_config, 0x77), 3, 1, 0x32);
    if (r != -1)
        U8(g_config, 0x77) = (uint8_t)r;
    return r != -1;
}

/* 0x450260: race +0x585 (0..2, texts 0x4b0234), stepping over 1 unless
 * race +0x720 bit 3 and over 2 unless bit 4; a step runs
 * race_setting_trim. */
int32_t GAMEAPI fecb_race_585(int32_t event, uint32_t buf)
{
    switch (event) {
    case 0x12:
        if (U8(g_race, 0x585) > 2)
            U8(g_race, 0x585) = 2;
        menu_text_get(buf, 0x4b0234, U8(g_race, 0x585) + 2);
        return 1;
    case 0x13:
        return 1;
    case 0x14:
        U8(g_race, 0x585)--;
        if (U8(g_race, 0x585) > 2)
            U8(g_race, 0x585) = 2;
        if (U8(g_race, 0x585) == 2 && !(U8(g_race, 0x720) & 0x10))
            U8(g_race, 0x585) = 1;
        if (U8(g_race, 0x585) == 1 && !(U8(g_race, 0x720) & 8))
            U8(g_race, 0x585) = 0;
        race_setting_trim();
        return 1;
    case 0x15:
    case 0x16:
        U8(g_race, 0x585)++;
        if (U8(g_race, 0x585) > 2)
            U8(g_race, 0x585) = 0;
        if (U8(g_race, 0x585) == 1 && !(U8(g_race, 0x720) & 8))
            U8(g_race, 0x585) = 2;
        if (U8(g_race, 0x585) == 2 && !(U8(g_race, 0x720) & 0x10))
            U8(g_race, 0x585) = 0;
        race_setting_trim();
        return 1;
    }
    return 0;
}

/* 0x450490: three-way option +0x8b (texts 0x4b0234). */
int32_t GAMEAPI fecb_three_8b(int32_t event, uint32_t buf)
{
    return xb_three(0x8b, 0x4b0234, event, buf);
}

/* 0x450580: on/off option +0x24. */
int32_t GAMEAPI fecb_onoff_24(int32_t event)
{
    return xb_onoff(0x4b2198, 0x24, event);
}

/* 0x4505e0: on/off option +0x28. */
int32_t GAMEAPI fecb_onoff_28(int32_t event)
{
    return xb_onoff(0x4b2198, 0x28, event);
}

/* 0x450640: on/off option +0x29. */
int32_t GAMEAPI fecb_onoff_29(int32_t event)
{
    return xb_onoff(0x4b2198, 0x29, event);
}

/* 0x450760: three-way option +0x2c (texts 0x4b0324). */
int32_t GAMEAPI fecb_three_2c(int32_t event, uint32_t buf)
{
    return xb_three(0x2c, 0x4b0324, event, buf);
}

/* 0x450950: on/off render flag 0x4acc44, applied (0x428a50 with
 * 0x4be444, 0x4acc48, 0x4acc4c, 0x4acc58) and refreshed (0x4219d0). */
int32_t GAMEAPI fecb_render_44(int32_t event)
{
    uint32_t v = option_onoff_step(G32(0x4acc44), event);
    G32(0x4acc44) = v;
    render_flags_apply(v, G32(0x4be444), G32(0x4acc48), G32(0x4acc4c), G32(0x4acc58));
    gamma_refresh();
    return G32(0x4acc44) == 0;
}

/* 0x4509c0: on/off render flag 0x4acc48, as 0x450950. */
int32_t GAMEAPI fecb_render_48(int32_t event)
{
    uint32_t v = option_onoff_step(G32(0x4acc48), event);
    G32(0x4acc48) = v;
    render_flags_apply(G32(0x4acc44), G32(0x4be444), v, G32(0x4acc4c), G32(0x4acc58));
    gamma_refresh();
    return G32(0x4acc48) == 0;
}

/* 0x450a30: three-way option +0x33 (texts 0x4b03e8). */
int32_t GAMEAPI fecb_three_33(int32_t event, uint32_t buf)
{
    return xb_three(0x33, 0x4b03e8, event, buf);
}

/* 0x450ae0: option dword +0x434 (0..3), shown as a number from 1
 * (0x4b0230); a step runs blend_modes_choose. */
int32_t GAMEAPI fecb_blend_434(int32_t event, uint32_t buf)
{
    switch (event) {
    case 0x12:
        crt_sprintf(buf, 0x4b0230, U32(g_config, 0x434) + 1);
        return 1;
    case 0x13:
        return 1;
    case 0x14:
        U32(g_config, 0x434)--;
        if (U32(g_config, 0x434) > 3)
            U32(g_config, 0x434) = 3;
        blend_modes_choose();
        return 1;
    case 0x15:
    case 0x16:
        U32(g_config, 0x434)++;
        if (U32(g_config, 0x434) > 3)
            U32(g_config, 0x434) = 0;
        blend_modes_choose();
        return 1;
    }
    return 0;
}

/* 0x450cf0: choice option +0x3a, 9 values (0x4afe50). */
int32_t GAMEAPI fecb_choice_3a(int32_t event, uint32_t buf)
{
    return xb_choice(0x3a, event, buf, 9, 0x4afe50);
}
