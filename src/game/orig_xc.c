/* Group XC: code Ghidra never made into functions, reached only through
 * code addresses stored as values (menu item callbacks, menu-wide action
 * handlers, body hit callbacks at +0x148), 0x450d40-0x48dc20:
 *
 *  - the comms options' choice callbacks (parity, stop bits, flow control:
 *    0x450d40, 0x450d90, 0x450de0) over menu_choice_callback 0x450c00;
 *  - the set-up options' on / off and choice callbacks (weapons, rear view
 *    mirror, weather, time of day: 0x450e50, 0x450f10, 0x450f70, 0x451060);
 *  - the sound options' callbacks (0x451300-0x451750: volume sliders,
 *    switches, channels, speaker mode, CD track / shuffle);
 *  - the series menu's cursor callback 0x451bf0, the controls menu's
 *    callbacks (0x452350, 0x4523c0, 0x452430, 0x452760);
 *  - menu-wide action handlers (0x452950, 0x452d30, 0x452db0, 0x452e40,
 *    0x453000) and a select callback (0x452e30);
 *  - three body hit callbacks (+0x148: 0x458310, 0x4596f0, 0x48dc20).
 *
 * Menu callbacks take (command, text buffer) as docs/formats.md describes
 * (commands 0x12/0x13 texts, 0x14 down, 0x15/0x16 up, 0xc/0xd and 0x11 for
 * on / off items); body callbacks take (body, other, event). Everything
 * else is called by address. */
#include "../runtime.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define GS8(a) (*(int8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))

typedef int32_t(GAMEAPI *xc_fn0)(void);
typedef int32_t(GAMEAPI *xc_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *xc_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *xc_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *xc_fn4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(GAMEAPI *xc_fn5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);

#define CALL0(a) (((xc_fn0)(a))())
#define CALL1(a, x) (((xc_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((xc_fn2)(a))(A(x), A(y)))
#define CALL3(a, x, y, z) (((xc_fn3)(a))(A(x), A(y), A(z)))
#define CALL4(a, x, y, z, w) (((xc_fn4)(a))(A(x), A(y), A(z), A(w)))
#define CALL5(a, x, y, z, w, v) (((xc_fn5)(a))(A(x), A(y), A(z), A(w), A(v)))

#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

#define CONFIG PTR(G32(0x4b2198))
#define RACE PTR(G32(0x4b1960))

#define CHOICE_CALLBACK 0x450c00 /* menu_choice_callback(cmd, text, max, &value, key) */
#define ONOFF_STEP 0x447f20      /* option_onoff_step(value, 1, cmd) -> new value */
#define MENU_TEXT_GET 0x4481a0   /* (text, -1, key, index) */
#define SPRINTF 0x4979f0
#define FE_REPEAT_SELECT 0x4475f0
#define FE_MENU_GOTO 0x4479d0
#define FE_MENU_BACK 0x447ab0
#define SND_SETTINGS_APPLY 0x4705d0
#define VOICE_PLAY_PLAIN 0x472a90
#define MENU_STATE_RESET 0x452730
#define GAME_TIME 0x43692a
#define CRT_FTOL 0x497a80

/* fld dword v; call _ftol: the low dword. */
static inline int32_t xc_ftol(uint32_t v)
{
    int32_t r;

    __asm__ volatile("flds %1\n\t"
                     "call *%2"
                     : "=a"(r)
                     : "m"(v), "r"(CRT_FTOL)
                     : "ecx", "edx", "memory", "cc", X87);
    return r;
}

/* ===================================================== comms options */

/* The three comms choices: config byte `off` through the choice callback
 * (max `max`, text key `key`); stored back when it handles the command. */
static int32_t comm_choice(int32_t cmd, uint32_t text, uint32_t off, uint32_t max, uint32_t key)
{
    uint32_t value = U8(CONFIG, off);

    if (CALL5(CHOICE_CALLBACK, cmd, text, max, &value, key) == 0)
        return 0;
    U8(CONFIG, off) = (uint8_t)value;
    return 1;
}

/* 0x450d40: parity choice (config +0x3f, 0..4). */
int32_t GAMEAPI comm_parity_option(int32_t cmd, uint32_t text)
{
    return comm_choice(cmd, text, 0x3f, 4, 0x4afe14);
}

/* 0x450d90: stop bits choice (config +0x3e, 0..1). */
int32_t GAMEAPI comm_stopbits_option(int32_t cmd, uint32_t text)
{
    return comm_choice(cmd, text, 0x3e, 1, 0x4afe30);
}

/* 0x450de0: flow control choice (config +0x3d, 0..3). */
int32_t GAMEAPI comm_flowcontrol_option(int32_t cmd, uint32_t text)
{
    return comm_choice(cmd, text, 0x3d, 3, 0x4afdf0);
}

/* ===================================================== set-up options */

/* 0x450e50: weapons on / off (race +0xa86); returns setting == 0. */
int32_t GAMEAPI setup_weapons_toggle(int32_t cmd)
{
    uint32_t v = (uint32_t)CALL3(ONOFF_STEP, U8(RACE, 0xa86), 1, cmd);
    U8(RACE, 0xa86) = (uint8_t)v;
    return U8(RACE, 0xa86) == 0;
}

/* 0x450f10: rear view mirror on / off (config +0x25); returns setting == 0. */
int32_t GAMEAPI setup_mirror_toggle(int32_t cmd)
{
    uint32_t v = (uint32_t)CALL3(ONOFF_STEP, U8(CONFIG, 0x25), 1, cmd);
    U8(CONFIG, 0x25) = (uint8_t)v;
    return U8(CONFIG, 0x25) == 0;
}

/* A 0..max byte choice: 0x12 clamps it and shows text index value + 2 of
 * `key`, 0x13 handled, 0x14 down (clamped to max when it leaves the
 * range), 0x15 / 0x16 up (wrapping to 0). */
static int32_t byte_choice(int32_t cmd, uint32_t text, uint32_t base_ptr, uint32_t off, uint32_t max, uint32_t key)
{
    switch ((uint32_t)(cmd - 0x12)) {
    case 0:
        if (U8(G32(base_ptr), off) > max)
            U8(G32(base_ptr), off) = (uint8_t)max;
        CALL4(MENU_TEXT_GET, text, -1, key, U8(G32(base_ptr), off) + 2);
        return 1;
    case 1:
        return 1;
    case 2:
        U8(G32(base_ptr), off) = (uint8_t)(U8(G32(base_ptr), off) - 1);
        if (U8(G32(base_ptr), off) > max)
            U8(G32(base_ptr), off) = (uint8_t)max;
        return 1;
    case 3:
    case 4:
        U8(G32(base_ptr), off) = (uint8_t)(U8(G32(base_ptr), off) + 1);
        if (U8(G32(base_ptr), off) > max)
            U8(G32(base_ptr), off) = 0;
        return 1;
    default:
        return 0;
    }
}

/* 0x450f70: weather choice (race +0xa89, 0..2). */
int32_t GAMEAPI setup_weather_choice(int32_t cmd, uint32_t text)
{
    return byte_choice(cmd, text, 0x4b1960, 0xa89, 2, 0x4b048c);
}

/* 0x451060: time of day choice (config +0x27, 0..3). */
int32_t GAMEAPI setup_daytime_choice(int32_t cmd, uint32_t text)
{
    return byte_choice(cmd, text, 0x4b2198, 0x27, 3, 0x4b04ac);
}

/* ===================================================== sound options */

/* 0x451300: music volume slider: _ftol(value) to 0x471b30 (twice), the
 * slider sound's volume word 0x4af610 = race +0xa7e. */
void GAMEAPI audio_music_volume_slider(uint32_t value)
{
    int32_t v = xc_ftol(value);
    CALL2(0x471b30, v, v);
    G16(0x4af610) = U16(RACE, 0xa7e);
}

/* 0x451330: effects volume slider: _ftol(value) to 0x471b80, 0x4af610 =
 * race +0xa7c. */
void GAMEAPI audio_effects_volume_slider(uint32_t value)
{
    int32_t v = xc_ftol(value);
    CALL1(0x471b80, v);
    G16(0x4af610) = U16(RACE, 0xa7c);
}

/* A sound switch: the dword at `a` stepped, the settings applied if sound
 * is up (0x4cb46c). */
static void sound_switch(uint32_t a, int32_t cmd)
{
    G32(a) = (uint32_t)CALL3(ONOFF_STEP, G32(a), 1, cmd);
    if (G32(0x4cb46c) != 0)
        CALL0(SND_SETTINGS_APPLY);
}

/* 0x451360: switch 0x4d4548; returns it == 0. */
int32_t GAMEAPI audio_toggle_4d4548(int32_t cmd)
{
    sound_switch(0x4d4548, cmd);
    return G32(0x4d4548) == 0;
}

/* 0x4513a0: switch 0x4b1c84; returns 1 if 0x470eb0 says no, 0 if 0x4b1c88
 * is on, else it == 0. */
int32_t GAMEAPI audio_toggle_4b1c84(int32_t cmd)
{
    sound_switch(0x4b1c84, cmd);
    if (CALL0(0x470eb0) == 0)
        return 1;
    if (G32(0x4b1c88) != 0)
        return 0;
    return G32(0x4b1c84) == 0;
}

/* 0x451400: switch 0x4b1c88 (also marks the menu for a redraw, 0x4cb58c,
 * when sound is up); returns 1 if 0x470e70 says no, else it == 0. */
int32_t GAMEAPI audio_toggle_4b1c88(int32_t cmd)
{
    G32(0x4b1c88) = (uint32_t)CALL3(ONOFF_STEP, G32(0x4b1c88), 1, cmd);
    if (G32(0x4cb46c) != 0) {
        CALL0(SND_SETTINGS_APPLY);
        G32(0x4cb58c) = 1;
    }
    if (CALL0(0x470e70) == 0)
        return 1;
    return G32(0x4b1c88) == 0;
}

/* 0x451450: switch 0x4d4540; returns it == 0. */
int32_t GAMEAPI audio_toggle_4d4540(int32_t cmd)
{
    sound_switch(0x4d4540, cmd);
    return G32(0x4d4540) == 0;
}

/* 0x451490: switch 0x4d4544; returns it == 0. */
int32_t GAMEAPI audio_toggle_4d4544(int32_t cmd)
{
    sound_switch(0x4d4544, cmd);
    return G32(0x4d4544) == 0;
}

/* 0x4514d0: switch 0x4b1c78; returns it == 0. */
int32_t GAMEAPI audio_toggle_4b1c78(int32_t cmd)
{
    sound_switch(0x4b1c78, cmd);
    return G32(0x4b1c78) == 0;
}

/* 0x451510: switch 0x4b1c7c; returns it == 0. */
int32_t GAMEAPI audio_toggle_4b1c7c(int32_t cmd)
{
    sound_switch(0x4b1c7c, cmd);
    return G32(0x4b1c7c) == 0;
}

/* 0x451550: switch 0x4b1c80; returns it == 0. */
int32_t GAMEAPI audio_toggle_4b1c80(int32_t cmd)
{
    sound_switch(0x4b1c80, cmd);
    return G32(0x4b1c80) == 0;
}

/* 0x451590: channel count choice: 0x12 prints 0x4b1c74 clamped to 8..64
 * ("%d"), 0x13 prints 64 ("%dW" sic), 0x14 / 0x15 / 0x16 step the count by
 * 4 (EAX on, 0x4beb38) or 1 through 0x470640. */
int32_t GAMEAPI audio_channels_choice(int32_t cmd, uint32_t text)
{
    int32_t step = (G32(0x4beb38) != 0 ? 3 : 0) + 1;
    int32_t n;

    switch ((uint32_t)(cmd - 0x12)) {
    case 0:
        n = GI32(0x4b1c74);
        if (n < 8)
            n = 8;
        else if (n > 0x40)
            n = 0x40;
        CALL3(SPRINTF, text, 0x4b0230, n);
        return 1;
    case 1:
        CALL3(SPRINTF, text, 0x4b022c, 0x40);
        return 1;
    case 2:
        CALL1(0x470640, -step);
        return 1;
    case 3:
    case 4:
        CALL1(0x470640, step);
        return 1;
    default:
        return 0;
    }
}

/* 0x451650: speaker mode choice: 0x12 shows text 3 / 4 / 2 for mode
 * 0x4d454c 1 / 2 / other, 0x13 text 1, 0x14 / 0x15 / 0x16 cycle it
 * (0x4705f0(1 or 0, 0)). */
int32_t GAMEAPI audio_speaker_choice(int32_t cmd, uint32_t text)
{
    int32_t index;

    switch ((uint32_t)(cmd - 0x12)) {
    case 0:
        index = GI32(0x4d454c) == 1 ? 3 : GI32(0x4d454c) == 2 ? 4 : 2;
        CALL4(MENU_TEXT_GET, text, -1, 0x4b04e8, index);
        return 1;
    case 1:
        CALL4(MENU_TEXT_GET, text, -1, 0x4b04e8, 1);
        return 1;
    case 2:
        CALL2(0x4705f0, 1, 0);
        return 1;
    case 3:
    case 4:
        CALL2(0x4705f0, 0, 0);
        return 1;
    default:
        return 0;
    }
}

/* 0x451720: the CD shuffle item's action handler: on select (6) with the
 * select key's repeat passed, 0x496c60 (next CD track); returns 1, else 0. */
int32_t GAMEAPI audio_cd_track_action(int32_t action)
{
    if (action == 6 && CALL1(FE_REPEAT_SELECT, 0) != 0) {
        CALL0(0x496c60);
        return 1;
    }
    return 0;
}

/* 0x451750: CD shuffle mode choice (race word +0x61e, 0..2): 0x12 text
 * mode + 2, 0x13 text 1, 0x14 down (to 2 when it leaves the range), 0x15 /
 * 0x16 up (to 0), then 0x496c50(mode). */
int32_t GAMEAPI audio_cd_shuffle_choice(int32_t cmd, uint32_t text)
{
    uint16_t step, wrap;

    switch ((uint32_t)(cmd - 0x12)) {
    case 0:
        CALL4(MENU_TEXT_GET, text, -1, 0x4b0504, U16(RACE, 0x61e) + 2);
        return 1;
    case 1:
        CALL4(MENU_TEXT_GET, text, -1, 0x4b0504, 1);
        return 1;
    case 2:
        step = 0xffff;
        wrap = 2;
        break;
    case 3:
    case 4:
        step = 1;
        wrap = 0;
        break;
    default:
        return 0;
    }
    U16(RACE, 0x61e) = (uint16_t)(U16(RACE, 0x61e) + step);
    if (U16(RACE, 0x61e) > 2)
        U16(RACE, 0x61e) = wrap;
    CALL1(0x496c50, U16(RACE, 0x61e));
    return 1;
}

/* ===================================================== series / controls */

/* 0x451bf0: the series items' cursor callback (index, value): 0x4498d0
 * (value), its result returned. */
int32_t GAMEAPI series_cursor_arrived(int32_t index, int32_t value)
{
    (void)index;
    return CALL1(0x4498d0, value);
}

/* The controls menu's player 0x4cb7e8: config + 0x90 * player. */
#define PLAYER_CONFIG(off) U8(CONFIG, GI32(0x4cb7e8) * 0x90 + (off))

/* 0x452350: the player's on / off byte +0x1a2; returns it == 0. */
int32_t GAMEAPI control_toggle_1a2(int32_t cmd)
{
    uint32_t v = (uint32_t)CALL3(ONOFF_STEP, PLAYER_CONFIG(0x1a2), 1, cmd);
    PLAYER_CONFIG(0x1a2) = (uint8_t)v;
    return PLAYER_CONFIG(0x1a2) == 0;
}

/* 0x4523c0: the player's on / off byte +0x1a3; returns it == 0. */
int32_t GAMEAPI control_toggle_1a3(int32_t cmd)
{
    uint32_t v = (uint32_t)CALL3(ONOFF_STEP, PLAYER_CONFIG(0x1a3), 1, cmd);
    PLAYER_CONFIG(0x1a3) = (uint8_t)v;
    return PLAYER_CONFIG(0x1a3) == 0;
}

/* The sound after a binding change, then the menu's state reset. */
static int32_t binding_done(void)
{
    CALL3(VOICE_PLAY_PLAIN, 0x1b, 0x800, 0x36b0);
    CALL0(MENU_STATE_RESET);
    return 1;
}

/* 0x452430: the controls menu's binding items' action handler. With a
 * valid player (0x4cb7e8 0..3) and cursor item: 0x48a420(player's pad)
 * polls for input; select (6 / 7) with the select repeat passed starts
 * capturing (0x48a560, 0x4cb7ec = 1, item +7 = 3, a sound); 0xa while
 * capturing stores a key (0x4af448 set: key 0x4af438 unless it is Esc
 * 0x1b) or a pad input (0x5d6234..0x5d623c) through control_entry_set
 * 0x452640, or, not capturing, a type key (0xe3 / 0xe4 / 0xe5 -> 1 / 0 / 2)
 * through 0x4526f0. Returns 1 when handled. */
int32_t GAMEAPI control_binding_action(int32_t action)
{
    int32_t player = GI32(0x4cb7e8), cursor, got, kind;
    uint32_t value;
    uint8_t *item;

    if (player < 0 || player >= 4 || GS8(0x4cb588) < 0 || GS8(0x4cb588) >= GI32(0x4cb49c)) {
        G32(0x4cb7ec) = 0;
        return 0;
    }
    cursor = GS8(0x4cb588);
    value = U32(G32(0x4cb4b8), cursor * 0x730 + 0x4f8);
    got = CALL1(0x48a420, U8(CONFIG, player * 0x90 + 0x1a1));
    if (action < 6)
        return 0;
    if (action <= 7) {
        if (CALL1(FE_REPEAT_SELECT, 0) == 0)
            return 1;
        CALL0(0x48a560);
        item = PTR(G32(0x4cb4b8) + GS8(0x4cb588) * 0x730);
        G32(0x4cb7ec) = 1;
        item[7] = 3;
        CALL3(VOICE_PLAY_PLAIN, 0x20, 0x400, 0x36b0);
        return 1;
    }
    if (action != 0xa)
        return 0;
    if (G32(0x4cb7ec) != 0) {
        if (G32(0x4af448) != 0) {
            if (G32(0x4af43c) != 0x1b)
                CALL5(0x452640, player, value, 2, 0, G32(0x4af438));
        } else {
            if (got == 0)
                return 1;
            CALL5(0x452640, player, value, G32(0x5d6238), G32(0x5d6234), G32(0x5d623c));
        }
        G32(0x4cb7ec) = 0;
        return binding_done();
    }
    if (G32(0x4af448) == 0)
        return 1;
    if (G32(0x4af43c) == 0xe3)
        kind = 1;
    else if (G32(0x4af43c) == 0xe4)
        kind = 0;
    else if (G32(0x4af43c) == 0xe5)
        kind = 2;
    else
        return 1;
    CALL3(0x4526f0, player, value, kind);
    return binding_done();
}

/* 0x452760: the controls menu's pad choice (config +0x1a1 + 0x90 * player,
 * 0xff none; pad count from 0x48a010): 0x12 shows the pad's name (0x48a590;
 * a name over 24 characters starts after its first space in the last 24,
 * "Unknown" if none, PC_MENU_OPTIONS_NONE for no pad), 0x13 handled, 0x14 /
 * 0x15 / 0x16 step through none and the pads, set by 0x452900 (redraw
 * 0x4cb58c). Returns 1 when handled. */
int32_t GAMEAPI control_pad_choice(int32_t cmd, uint8_t *text)
{
    int32_t player = GI32(0x4cb7e8), count, len, i;
    uint8_t pad, local;
    uint8_t *name;

    if (player < 0 || player >= 4)
        return 0;
    pad = U8(CONFIG, player * 0x90 + 0x1a1);
    local = pad;
    count = CALL0(0x48a010);
    switch ((uint32_t)(cmd - 0x12)) {
    case 0:
        if (pad == 0xff || count == 0) {
            CALL4(0x42e6d0, text, 0x20, 0x4b05dc, 0);
            return 1;
        }
        name = PTR(CALL1(0x48a590, local));
        if (name == 0) {
            U32(text, 0) = G32(0x4b05f4);
            U32(text, 4) = G32(0x4b05f8);
            return 1;
        }
        for (len = 0; name[len] != 0; len++)
            ;
        if (len > 0x18) {
            for (i = len - 0x18; i < len; i++) {
                if (name[i] == ' ') {
                    name = name + i + 1;
                    break;
                }
            }
        }
        i = 0;
        do
            text[i] = name[i];
        while (name[i++] != 0);
        return 1;
    case 1:
        return 1;
    case 2:
        if (count > 0) {
            if (pad == 0)
                local = 0xff;
            else if (pad != 0xff && (int32_t)local < count)
                local = (uint8_t)(pad - 1);
            else
                local = (uint8_t)(count - 1);
        }
        break;
    case 3:
    case 4:
        if (count > 0) {
            if (pad == 0xff)
                local = 0;
            else if ((int32_t)local >= count - 1)
                local = 0xff;
            else
                local = (uint8_t)(pad + 1);
        }
        break;
    default:
        return 0;
    }
    CALL2(0x452900, player, local);
    G32(0x4cb58c) = 1;
    return 1;
}

/* ===================================================== menu handlers */

/* 0x452950: a menu's action handler: on 0xa the countdown 0x4cb168 (if
 * positive) steps down, a redraw (0x4cb58c) when it reaches 0. Returns 0. */
int32_t GAMEAPI menu_countdown_handler(int32_t action)
{
    if (action == 0xa && GI32(0x4cb168) > 0) {
        G32(0x4cb168) = G32(0x4cb168) - 1;
        if (G32(0x4cb168) == 0)
            G32(0x4cb58c) = 1;
    }
    return 0;
}

/* 0x452d30: the network host menu's handler: 0xb refreshes (0x452c20; a
 * redraw if 0x4cb540 or 0x4af8a0 is set, 0x4af8a0 = 0x4cb540); 0xa polls:
 * 0x48d490 failing goes to screen 0x46, the session poll (2) answering 3
 * sets flag 9 off and goes to screen 0x53 (both drop the handler). */
int32_t GAMEAPI net_host_menu_handler(int32_t action)
{
    if (action == 0xb) {
        CALL0(0x452c20);
        if (G32(0x4cb540) != 0 || G32(0x4af8a0) != 0)
            G32(0x4cb58c) = 1;
        G32(0x4af8a0) = G32(0x4cb540);
        return 0;
    }
    if (action == 0xa) {
        if (CALL0(0x48d490) == 0) {
            G32(0x4cb524) = 0;
            CALL2(FE_MENU_GOTO, 0x46, 0);
        }
        if (CALL1(0x435d43, 2) == 3) {
            G32(0x4cb524) = 0;
            CALL1(0x45f790, 0);
            CALL2(FE_MENU_GOTO, 0x53, 0);
        }
    }
    return 0;
}

/* 0x452db0: the network join menu's handler: as 0x452d30 with 0x4af8a4,
 * the session poll (0) answering 2 going to screen 0x5b. */
int32_t GAMEAPI net_join_menu_handler(int32_t action)
{
    if (action == 0xb) {
        CALL0(0x452c20);
        if (G32(0x4cb540) != 0 || G32(0x4af8a4) != 0)
            G32(0x4cb58c) = 1;
        G32(0x4af8a4) = G32(0x4cb540);
        return 0;
    }
    if (action == 0xa) {
        if (CALL0(0x48d490) == 0) {
            G32(0x4cb524) = 0;
            CALL2(FE_MENU_GOTO, 0x46, 0);
        }
        if (CALL1(0x435d43, 0) == 2) {
            G32(0x4cb524) = 0;
            CALL2(FE_MENU_GOTO, 0x5b, 0);
        }
    }
    return 0;
}

/* 0x452e30: the session search item's select callback: 0x4cb580 = 1
 * (stop searching). */
void GAMEAPI net_sessions_found_select(void)
{
    G32(0x4cb580) = 1;
}

/* 0x452e40: the session search menu's handler. 0xb: 0x452ce0 and a
 * redraw. 0xa: the countdown 0x4cb168 reaching 0 restarts the network
 * (0x48a840(1, 0)), stamps the times 0x4cb578 / 0x4cb57c and opens the
 * search (0x48b610); then every 500 ms 0x48b6d0 and a session list
 * (0x48b780); a list with sessions (serial / modem link, config +0x80 >= 2)
 * stops it (0x4cb580). Stopped, or after 5 s (120 s for config +0x80 == 3)
 * with no countdown, it goes to screen 0x55. Failures go back. Returns 0. */
int32_t GAMEAPI net_sessions_handler(int32_t action)
{
    uint32_t wait = CONFIG[0x80] == 3 ? 0x78 : 5;
    int32_t n;

    if (action == 0xb) {
        CALL0(0x452ce0);
        G32(0x4cb58c) = 1;
        return 0;
    }
    if (action != 0xa)
        return 0;
    n = GI32(0x4cb168);
    if (n > 0) {
        n--;
        G32(0x4cb168) = (uint32_t)n;
        if (n != 0)
            goto waiting;
        if (CALL2(0x48a840, 1, 0) == 0) {
            CALL0(FE_MENU_BACK);
            return 0;
        }
        G32(0x4cb578) = G32(0x4cb57c) = (uint32_t)CALL0(GAME_TIME);
        if (CALL0(0x48b610) == 0) {
            CALL0(FE_MENU_BACK);
            return 0;
        }
        n = GI32(0x4cb168);
    }
    if (n != 0)
        goto waiting;
    if ((uint32_t)CALL0(GAME_TIME) <= G32(0x4cb57c) + 0x1f4)
        goto waiting;
    if (CALL0(0x48b6d0) == 0) {
        CALL0(FE_MENU_BACK);
        return 0;
    }
    G32(0x4cb57c) = (uint32_t)CALL0(GAME_TIME);
    if (CALL4(0x48b780, 0x32, 0x4cb0d8, 0x4c3018, 0x4c98f4) != 1)
        return 0;
    if (CONFIG[0x80] < 2 || GI32(0x4cb0d8) <= 0)
        return 0;
    G32(0x4cb580) = 1;
    return 0;

waiting:
    if (G32(0x4cb580) == 0) {
        if (G32(0x4cb168) != 0)
            return 0;
        if ((uint32_t)CALL0(GAME_TIME) <= G32(0x4cb578) + wait * 1000)
            return 0;
    }
    if (G32(0x4cb168) == 0)
        CALL0(0x48b6d0);
    G32(0x4cb524) = 0;
    CALL2(FE_MENU_GOTO, 0x55, 0);
    return 0;
}

/* 0x453000: a menu's action handler: on 0xa the counter 0x4af654 steps up
 * while it is 0..0x29. Returns 0. */
int32_t GAMEAPI menu_counter_handler(int32_t action)
{
    if (action == 0xa && GI32(0x4af654) >= 0 && GI32(0x4af654) < 0x2a)
        G32(0x4af654) = G32(0x4af654) + 1;
    return 0;
}

/* ===================================================== body hit callbacks */

/* 0x458310: a fallen pickup's hit callback (+0x148): 0x458320(body).
 * Returns 0. */
int32_t GAMEAPI pickup_fallen_hit(uint8_t *body, uint8_t *other, int32_t event)
{
    (void)other;
    (void)event;
    CALL1(0x458320, body);
    return 0;
}

/* 0x4596f0: the homing missile's hit callback (+0x148): ends it (0x459610
 * (body, 0)); hitting something: a car (type 3) gets the blast (0x4599c0
 * (car, event)) and, from another car (owner +0x18c) not in state 8 at
 * +0x70c, the kill message (0x4600a0(car +0x64e, owner +0x64e, 0xa)); the
 * owner's flags get 0x20000 (sic: no check for an owner). Returns 0. */
int32_t GAMEAPI homer_hit(uint8_t *body, uint8_t *other, int32_t event)
{
    uint8_t *owner;

    CALL2(0x459610, body, 0);
    if (other == 0)
        return 0;
    if ((U32(other, 0) & 0x1ff) == 3) {
        CALL2(0x4599c0, other, event);
        owner = PTR(U32(body, 0x18c));
        if (owner != 0 && U8(other, 0x70c) != 8)
            CALL3(0x4600a0, U16(other, 0x64e), U16(owner, 0x64e), 0xa);
    }
    U32(U32(body, 0x18c), 0) |= 0x20000;
    return 0;
}

/* 0x48dc20: the snatcher's hit callback (+0x148): 0x48ddd0(body, 1).
 * Returns 0. */
int32_t GAMEAPI snatch_hit(uint8_t *body, uint8_t *other, int32_t event)
{
    (void)other;
    (void)event;
    CALL2(0x48ddd0, body, 1);
    return 0;
}
