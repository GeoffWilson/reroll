/* Startup and shutdown, group SA (the WinMain body 0x4351e0 and its tree):
 *
 *  - the main loop 0x4351e0: the init steps, the message pump, the per-frame
 *    calls (front end or race, present, input), the restart loop and the exit;
 *  - the init steps: data paths and command line (0x4356ab, 0x43677b),
 *    window (0x4358b0), options (0x435932), sound (0x4359d3), Direct3D
 *    (0x435aae), the display-mode dialog (0x4370b2);
 *  - the option defaults and their load (0x473d50, 0x4740c0, 0x4745f0), the
 *    pad binding defaults (0x4498f0 and below), the record tables
 *    (0x405320..0x4057c0), the race result flow at a chained restart
 *    (0x444800, 0x444dd0, 0x445170);
 *  - shutdown (0x435515, 0x435e99, 0x435e2c, 0x43758f) and the free helpers.
 *
 * Window creation, DirectDraw/Direct3D, DirectInput, the sound engine, the
 * registry and environment helpers (0x46b2xx..0x46b9xx), file and CD path
 * probing, process relaunch, the memory pool and the C runtime stay original
 * and are called by address. WinAPI calls are by name (winapi.h). */
#include "../runtime.h"
#include "physics.h"
#include "winapi.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))

#define g_config GP(0x4b2198)
#define g_race GP(0x4b1960)

#define STDCALL __attribute__((stdcall))


/* Original functions called by address. */
#define game_time() ((uint32_t)GCALL(0x43692a, game_time_get)())
#define fatal_exit() GCALL(0x43699c, quit_request)()
#define present_frame() GCALL(0x436a52, present_frame)()
#define text_entry(dst, size, section, index) GCALL(0x42e6d0, text_line_get)((uint8_t *)GMEM(dst), size, (const char *)GMEM(section), index)
#define crt_free(p) GCALL(0x49827a, msvc_free)(GMEM(p))
#define time_entry_405480(out, slot, lap, kind) GCALL(0x405480, time_entry_fill)(out, slot, lap, kind)
#define held_by_405380(track, slot) GCALL(0x405380, track_record_held_by)(track, slot)
#define binding_449bf0(set, n) ((uint8_t *)(uintptr_t)GCALL(0x449bf0, binding_get)(set, n))
#define nop_4291f0() GCALL(0x4291f0, dx_error_ignore)()

#define g_window G32(0x6c8000)
#define g_instance G32(0x6c7ffc)
#define g_frontend_mode G32(0x4d4650) /* /FRONTEND: chained restart into the front end */
#define g_screen_w G32(0x4acc38)
#define g_screen_h G32(0x4acc3c)
#define g_screen_bpp G32(0x4acc40)
#define g_windowed G32(0x4acc5c)
#define g_result_menu GI32(0x4af650)

/* ---------------------------------------------------------------- strings */

/* 0x436688: case-insensitive strstr (the C runtime's toupper 0x49867b and
 * tolower 0x498540). */
char *GAMEAPI stristr(char *hay, const char *needle)
{
    uint8_t upper, lower;
    int32_t i;

    if (!hay || !needle)
        return 0;
    if (!(uint8_t)needle[0])
        return hay;
    upper = (uint8_t)GCALL(0x49867b, msvc_tolower)((uint8_t)needle[0]);
    lower = (uint8_t)GCALL(0x498540, msvc_toupper)((uint8_t)needle[0]);
    for (; (uint8_t)*hay; hay++) {
        if ((uint8_t)*hay != upper && (uint8_t)*hay != lower)
            continue;
        for (i = 1;; i++) {
            if (!(uint8_t)needle[i])
                return hay;
            if (GCALL(0x498540, msvc_toupper)((uint8_t)hay[i]) != GCALL(0x498540, msvc_toupper)((uint8_t)needle[i]))
                break;
        }
    }
    return 0;
}

/* 0x474e80: copy `src` without its double quotes (at most 0x103 chars). */
void GAMEAPI unquote_copy(char *dst, const char *src)
{
    int32_t n = 0;
    int32_t done = 0;

    if (!dst)
        return;
    if (src) {
        do {
            char c = *src;
            if (!c)
                done = 1;
            else if (c != '"' && n < 0x103)
                dst[n++] = c;
            src++;
        } while (!done);
    }
    dst[n] = 0;
}

/* 0x474dc0: copy the first token of a command line (ends at a space or comma
 * outside double quotes, quotes dropped, at most 0x103 chars). */
void GAMEAPI command_token_copy(char *dst, const char *src)
{
    int32_t n = 0;
    int32_t done = 0;
    int32_t quoted = 0;

    if (!dst)
        return;
    if (src) {
        do {
            char c = *src;
            if (c == 0) {
                done = 1;
            } else if ((c == ' ' || c == ',') && !quoted) {
                done = 1;
            } else if (c == '"') {
                quoted = !quoted;
            } else if (n < 0x103) {
                dst[n++] = c;
            }
            src++;
        } while (!done);
    }
    dst[n] = 0;
}

/* ---------------------------------------------------------- record tables */

/* 0x405320: the record tables' defaults (0x150 dwords from 0x4a7150 to
 * config +0x444). */
void GAMEAPI records_defaults(void)
{
    uint32_t *dst = (uint32_t *)(g_config + 0x444);
    const uint32_t *src = (const uint32_t *)0x4a7150;
    int32_t i;

    for (i = 0; i < 0x150; i++)
        dst[i] = src[i];
}

/* 0x405340: mark all 16 x 6 record entries (+0x451) not new: clear bit 0,
 * set bits 3..6. */
void GAMEAPI records_mark_old(void)
{
    int32_t off = 0;
    int32_t n;

    do {
        n = 6;
        do {
            off += 0xe;
            U8(g_config, off + 0x443) &= 0xfe;
            U8(g_config, off + 0x443) |= 0x78;
        } while (--n);
    } while (off < 0x540);
}

/* 0x405570: time entry of car slot `slot`, best lap (time_entry_fill kind 1). */
void GAMEAPI time_entry_lap(uint8_t *out, int32_t slot)
{
    time_entry_405480(out, slot, -1, 1);
}

/* 0x4055b0: time entry of car slot `slot`, race time (kind 0). */
void GAMEAPI time_entry_race(uint8_t *out, int32_t slot)
{
    time_entry_405480(out, slot, -1, 0);
}

/* 0x405680: does time entry `entry` (BCD minutes, seconds, hundredths) beat
 * the table's entry `old`? An unset time (00 00 00 or 59 59 99) never
 * beats; it is beaten by any set time. */
int32_t GAMEAPI record_beats(const uint8_t *old, const uint8_t *entry)
{
    uint8_t n0 = entry[0], o0;

    if (n0 == 0x59 && entry[1] == 0x59 && entry[2] == 0x99)
        return 0;
    if (n0 == 0 && entry[1] == 0 && entry[2] == 0)
        return 0;
    o0 = old[0];
    if (o0 == 0x59 && old[1] == 0x59 && old[2] == 0x99)
        return 1;
    if (o0 == 0 && old[1] == 0 && old[2] == 0)
        return 1;
    if (n0 < o0)
        return 1;
    if (n0 > o0)
        return 0;
    if (entry[1] < old[1])
        return 1;
    if (entry[1] > old[1])
        return 0;
    return entry[2] < old[2];
}

/* 0x405620: the rank `entry` would take in track `track`'s table
 * (config + (track + 0xd) * 0x54, 6 entries of 0xe), or -1. */
int32_t GAMEAPI record_find_rank(const uint8_t *entry, int32_t track)
{
    uint8_t *old;
    int32_t i;

    if (track < 0 || track >= 0x10)
        return -1;
    old = g_config + (track + 0xd) * 0x54;
    for (i = 0; i < 6; i++, old += 0xe)
        if (record_beats(old, entry))
            return i;
    return -1;
}

static void entry_copy(uint8_t *dst, const uint8_t *src)
{
    U32(dst, 0) = U32(src, 0);
    U32(dst, 4) = U32(src, 4);
    U32(dst, 8) = U32(src, 8);
    U16(dst, 0xc) = U16(src, 0xc);
}

/* 0x405720: insert `entry` at rank `rank` of track `track`'s table, moving
 * the lower ones down (the last drops out). */
void GAMEAPI record_insert_at(const uint8_t *entry, int32_t track, int32_t rank)
{
    uint8_t *table, *p;
    int32_t n;

    if (track < 0 || track >= 0x10 || rank < 0 || rank >= 6)
        return;
    table = g_config + (track + 0xd) * 0x54;
    if (rank < 5) {
        p = table + 0x46;
        n = 5 - rank;
        do {
            entry_copy(p, p - 0xe);
            p -= 0xe;
        } while (--n);
    }
    entry_copy(table + rank * 0xe, entry);
}

/* 0x4055d0: enter car slot `slot`'s best lap into track `track`'s table. */
void GAMEAPI record_insert_slot(int32_t track, int32_t slot)
{
    uint8_t entry[16] = {0}; /* the original's is uninitialised: time_entry_fill keeps bit 7 of +0xd */
    int32_t rank;

    if (track < 0 || track >= 0x10 || slot < 0 || slot >= 6)
        return;
    time_entry_lap(entry, slot);
    rank = record_find_rank(entry, track);
    if (rank != -1)
        record_insert_at(entry, track, rank);
}

/* 0x4057c0: enter every human player's best lap (config +0x8c + i set, race
 * byte i not 8) under the player's name (config +0xd8c + i * 0xb) into track
 * `track`'s table. */
void GAMEAPI records_insert_players(int32_t track)
{
    uint8_t entry[16] = {0}; /* uninitialised in the original, as above */
    int32_t i, off, rank;

    if (track < 0 || track >= 0x10)
        return;
    for (i = 0, off = 0; off < 0x42; i++, off += 0xb) {
        if (!U8(g_config, i + 0x8c) || U8(g_race, i) == 8)
            continue;
        time_entry_lap(entry, i);
        U32(entry, 3) = U32(g_config, off + 0xd8c);
        U32(entry, 7) = U32(g_config, off + 0xd90);
        U16(entry, 0xb) = U16(g_config, off + 0xd94);
        rank = record_find_rank(entry, track);
        if (rank != -1)
            record_insert_at(entry, track, rank);
    }
}

/* ------------------------------------------------------------ text tables */

/* 0x42dfb0: free the loaded string file (0x4be2ec, size 0x4be2f0). */
void GAMEAPI strings_free(void)
{
    if (G32(0x4be2ec))
        crt_free(G32(0x4be2ec));
    G32(0x4be2ec) = 0;
    G32(0x4be2f0) = 0;
}

/* 0x42dfa0: (0x4291f0 is an empty function) free the string file. */
void GAMEAPI strings_unload(void)
{
    nop_4291f0();
    strings_free();
}

/* 0x42dfe0: load archive entry `entry` as the string file: control
 * characters other than tab become 1, "~~" ends a string. */
void GAMEAPI strings_load_entry(int32_t entry)
{
    uint8_t *p;
    uint32_t i;

    strings_free();
    G32(0x4be2f0) = U32((uintptr_t)GCALL(0x496570, archive_get_record)(entry), 8);
    p = (uint8_t *)(uintptr_t)GCALL(0x496590, archive_entry_load)(entry, 0);
    G32(0x4be2ec) = (uint32_t)(uintptr_t)p;
    for (i = 0; i < G32(0x4be2f0); i++) {
        if (p[i] < 0x20 && p[i] != 9) {
            p[i] = 1;
            p = PTR(G32(0x4be2ec));
        }
        if (p[i] == '~' && p[i + 1] == '~') {
            p[i] = 0;
            p = PTR(G32(0x4be2ec));
        }
    }
}

/* 0x42df90: strings_load_entry, returns 0. */
int32_t GAMEAPI strings_load(int32_t entry)
{
    strings_load_entry(entry);
    return 0;
}

/* 0x43756d: load the string file (entry 8) once. */
void GAMEAPI strings_load_once(void)
{
    if (!G32(0x4be4bc)) {
        G32(0x4be4bc) = 1;
        strings_load(8);
    }
}

/* 0x42e3c0: the text language (u16 0x4be2f4). */
void GAMEAPI language_set(uint32_t language)
{
    G16(0x4be2f4) = (uint16_t)language;
}

/* ------------------------------------------------------------- free helpers */

/* 0x42e350: free the front end's four buffers (0x4be304..0x4be310). */
void GAMEAPI frontend_buffers_free(void)
{
    if (G32(0x4be308))
        crt_free(G32(0x4be308));
    if (G32(0x4be30c))
        crt_free(G32(0x4be30c));
    if (G32(0x4be304))
        crt_free(G32(0x4be304));
    if (G32(0x4be310))
        crt_free(G32(0x4be310));
    G32(0x4be308) = 0;
    G32(0x4be30c) = 0;
    G32(0x4be304) = 0;
    G32(0x4be310) = 0;
    G32(0x4be300) = 0;
}

/* 0x430e00: free the front-end texture tables (0x4be344, 0x4be348). */
void GAMEAPI frontend_textures_free(void)
{
    if (G32(0x4be344))
        crt_free(G32(0x4be344));
    G32(0x4be344) = 0;
    if (G32(0x4be348))
        crt_free(G32(0x4be348));
    G32(0x4be348) = 0;
}

/* 0x444790: free the menu table (0x4cb4b8) and close the menu. */
void GAMEAPI menu_table_free(void)
{
    if (G32(0x4cb4b8))
        crt_free(G32(0x4cb4b8));
    G32(0x4cb4b8) = 0;
    G32(0x4cb494) = 0;
    G32(0x4cb498) = 0;
}

/* 0x430ac0: free everything the front end holds. */
void GAMEAPI frontend_free_all(void)
{
    strings_unload();
    frontend_buffers_free();
    frontend_textures_free();
    menu_table_free();
}

/* ------------------------------------------------------------ pad bindings */

/* 0x449c70: index of activity `activity` in the activity table (0x4af7a0, 8
 * bytes each, count 0x4af878), or -1. */
int32_t GAMEAPI activity_index(int32_t activity)
{
    int32_t i, n = GI32(0x4af878);

    for (i = 0; i < n; i++)
        if (activity == GI32(0x4af7a0 + i * 8))
            return i;
    return -1;
}

/* 0x449c40: player `set`'s saved binding of activity `activity` (4 bytes at
 * config + 0x1b0 + set * 0x90 + activity * 4), or 0. */
uint8_t *GAMEAPI binding_saved_get(int32_t set, int32_t activity)
{
    if (set < 0 || set >= 4 || activity < 0 || activity >= 0x20)
        return 0;
    return g_config + (activity + (set * 9 + 0x1b) * 4) * 4;
}

/* 0x48a5c0: is joystick `device` usable (exists, flag 0x400 not set in the
 * device's +4 bits 8..15, at least 10 buttons)? */
int32_t GAMEAPI joystick_usable(int32_t device)
{
    if (device < 0)
        return 0;
    if (device >= GCALL(0x48a010, joy_device_count_thunk)())
        return 0;
    if (GCALL(0x489fa0, device_usable)(device))
        return 0;
    if (GCALL(0x48a040, fe_joy_button_count)(device) < 10)
        return 0;
    return 1;
}

/* 0x449ab0: player `set`'s default binding of activity `activity` from the
 * default tables (0x4ac398 / 0x4ac498 / 0x4ac508 / 0x4ac578 per player,
 * 0x4ac5e8 for joysticks (device 1), player 0's table for the keyboard
 * (0xff)): the first entry having all of the activity's mask bits. */
int32_t GAMEAPI binding_from_table(int32_t set, int32_t activity, int32_t device)
{
    int32_t k = activity_index(activity);
    uint8_t *binding = binding_449bf0(set, activity);
    uint32_t table, mask;
    int32_t count, i;

    if (!binding || k == -1)
        return 0;
    switch (set) {
    case 0:
        table = 0x4ac398;
        count = GI32(0x4ac6e8);
        break;
    case 1:
        table = 0x4ac498;
        count = GI32(0x4ac6ec);
        break;
    case 2:
        table = 0x4ac508;
        count = GI32(0x4ac6f0);
        break;
    case 3:
        table = 0x4ac578;
        count = GI32(0x4ac6f4);
        break;
    default:
        table = (uint32_t)k; /* sic: never read, the count is 0 */
        count = 0;
        break;
    }
    if (device == 1) {
        table = 0x4ac5e8;
        count = GI32(0x4ac6f8);
    } else if (device == 0xff) {
        if (set == 1) {
            table = 0x4ac498;
            count = GI32(0x4ac6ec);
        } else if (set == 2) {
            table = 0x4ac508;
            count = GI32(0x4ac6f0);
        } else if (set == 3) {
            table = 0x4ac578;
            count = GI32(0x4ac6f4);
        } else {
            table = 0x4ac398;
            count = GI32(0x4ac6e8);
        }
    }
    mask = G32(0x4af7a4 + k * 8);
    for (i = 0; i < count; i++) {
        const uint8_t *e = PTR(table + i * 16);
        if ((U32(e, 0) & mask) == mask) {
            U32(binding, 0) = U32(e, 0);
            U32(binding, 4) = U32(e, 4);
            U32(binding, 8) = U32(e, 8);
            U32(binding, 0xc) = U32(e, 0xc);
            return 1;
        }
    }
    return 0;
}

/* 0x449960: set player `set`'s binding of activity `activity`: from its
 * saved binding (config, kind 2 = saved, 1 = unbound), or from the default
 * tables for its device (config + set * 0x90 + 0x1a1). `save` 1 stores the
 * default into the saved binding. */
void GAMEAPI binding_default(int32_t set, int32_t activity, uint32_t save)
{
    uint8_t *binding = binding_449bf0(set, activity);
    uint8_t *saved = binding_saved_get(set, activity);
    int32_t k = activity_index(activity);
    uint32_t b1, b2, b3;
    int32_t device;

    if (!binding || !saved || k == -1)
        return;
    U32(binding, 4) = 0xffffffff;
    if (GI32(0x4af7a4 + k * 8) == -1)
        return;
    b1 = 0xfffffffe;
    b2 = U8(g_config, set * 0x90 + 0x1a1);
    b3 = save;
    device = joystick_usable((int32_t)b2);
    if (b2 == 0xff)
        device = 0xff;
    else if (device <= 0)
        goto saved_kind;
    if (b3 == 1) {
        saved[0] = 1;
        if (binding_from_table(set, activity, device)) {
            saved[0] = 2;
            saved[1] = U8(binding, 4);
            b1 = saved[1];
            saved[2] = U8(binding, 8);
            b2 = saved[2];
            saved[3] = U8(binding, 0xc);
            b3 = saved[3];
        }
    }
saved_kind:
    if (saved[0] == 1) {
        b1 = 0xffffffff;
    } else {
        if (saved[0] == 2) {
            b1 = saved[1];
            b2 = saved[2];
            b3 = saved[3];
        } else {
            binding_from_table(set, activity, device);
        }
        if (b1 == 0xfffffffe)
            return;
    }
    U32(binding, 4) = b1;
    U32(binding, 8) = b2;
    U32(binding, 0xc) = b3;
    U32(binding, 0) = G32(0x4af7a4 + k * 8);
}

/* 0x449910: clear player `set`'s 0x1b bindings, then set each one. */
void GAMEAPI bindings_reset_player(int32_t set)
{
    uint8_t *binding = binding_449bf0(set, 0);
    int32_t i;

    if (!binding)
        return;
    for (i = 0; i < 0x1b; i++) {
        U32(binding, i * 16 + 4) = 0xffffffff;
        U32(binding, i * 16 + 8) = 0xffffffff;
        U32(binding, i * 16 + 0xc) = 0xffffffff;
    }
    for (i = 0; i < 0x1b; i++)
        binding_default(set, i, 0);
}

/* 0x4498f0: set the bindings of all four players. */
void GAMEAPI bindings_reset_all(void)
{
    int32_t i;

    for (i = 0; i < 4; i++)
        bindings_reset_player(i);
}

/* ------------------------------------------------------------------ options */

/* 0x474770: the installed language (registry Software\Psygnosis\Rollcage
 * Install\Language, 0x46b690), 0..4, else 0. */
uint32_t GAMEAPI language_from_registry(void)
{
    uint32_t value = 0;

    if (GCALL(0x46b690, reg_dword_get)(0x4ac754, 0x4b22d4, (uint32_t)(uintptr_t)&value) == -1 || value > 4)
        return 0;
    return value;
}

/* 0x473c50: the option texts from the string file: with `all` 1 the network
 * session names (+0xdce, 10 of 0x40, "PC MENU MULTIPLAYER NETWORK
 * DEFAULTS", empty ones get 0x4b11bc's 4 bytes), player names (+0xd8c, 6 of
 * 0xb) and network messages (+0x104e, 30 of 0x40 from index 8); always
 * information messages 1 and 7 (+0x17ce, +0x180e). */
void GAMEAPI config_default_texts(int32_t all)
{
    int32_t i, off;

    if (all == 1) {
        for (i = 0, off = 0; off < 0x280; i++, off += 0x40) {
            text_entry(g_config + off + 0xdce, 0x40, 0x4b2254, i);
            if (!U8(g_config, off + 0xdce))
                U32(g_config, off + 0xdce) = G32(0x4b11bc);
        }
        for (i = 0, off = 0; off < 0x42; i++, off += 0xb)
            text_entry(g_config + off + 0xd8c, 0xb, 0x4afa24, i);
        for (i = 0, off = 0; off < 0x780; i++, off += 0x40)
            text_entry(g_config + off + 0x104e, 0x40, 0x4b0e34, i + 8);
    }
    text_entry(g_config + 0x17ce, 0x40, 0x4b223c, 1);
    text_entry(g_config + 0x180e, 0x40, 0x4b223c, 7);
}

/* 0x473d50: option (config, 0x1854 bytes) and PSX-style game state (race,
 * 0xb85b8 bytes) defaults. */
void GAMEAPI config_defaults(void)
{
    uint32_t size = 0xb;
    uint32_t *p;
    int32_t i;

    p = (uint32_t *)g_race;
    for (i = 0; i < 0x2e16e; i++)
        p[i] = 0;
    p = (uint32_t *)g_config;
    for (i = 0; i < 0x615; i++)
        p[i] = 0;
    U8(g_config, 0x8) = 1;
    U8(g_config, 0x9) = 1;
    U8(g_config, 0x10) = 1;
    U8(g_config, 0x39) = 1;
    U8(g_config, 0x24) = 1;
    U8(g_config, 0x27) = 0;
    U8(g_config, 0x2d) = 1;
    U8(g_config, 0x28) = 1;
    U8(g_config, 0x29) = 1;
    U8(g_config, 0x2a) = 1;
    U8(g_config, 0x2b) = 1;
    U8(g_config, 0x2c) = 1;
    U8(g_config, 0x2e) = 1;
    U8(g_config, 0x32) = 1;
    U8(g_config, 0x33) = 2;
    U8(g_config, 0x31) = 0;
    U8(g_config, 0x30) = 1;
    U32(g_config, 0x68) = 0x40800000;
    U32(g_config, 0x64) = 0x47800000;
    U32(g_config, 0x6c) = 0x42c80000;
    U32(g_config, 0x70) = 0x425c0000;
    U8(g_config, 0x80) = 0;
    U8(g_config, 0x83) = 0;
    U8(g_config, 0x87) = 0;
    U8(g_config, 0x88) = 0;
    U8(g_config, 0x8a) = 0;
    U8(g_config, 0x8b) = 0;
    U8(g_config, 0x93) = 0;
    U8(g_config, 0x3c) = 0;
    U8(g_config, 0x3a) = 8;
    U8(g_config, 0x3d) = 3;
    U8(g_config, 0x3e) = 0;
    U8(g_config, 0x3f) = 0;
    U16(g_config, 0x432) = 0xffff;
    U8(g_config, 0x1a1) = 0xff;
    U8(g_config, 0x231) = 0xff;
    U8(g_config, 0x2c1) = 0xff;
    U8(g_config, 0x351) = 0xff;
    U8(g_config, 0x36) = (uint8_t)language_from_registry();
    U8(g_config, 0x18) = G8(0x4af658);
    U8(g_config, 0x19) = G8(0x4af658);
    U8(g_config, 0x1a) = G8(0x4af658);
    U8(g_config, 0x1b) = G8(0x4af658);
    GetUserNameA((char *)(g_config + 0x120), &size);
    records_defaults();
    U8(g_config, 0x6) = 0;
    config_default_texts(1);
    GCALL(0x4745d0, settings_table_load)();
    U16(g_race, 0xa7c) = 12000;
    U16(g_race, 0xa7e) = 0xffff;
    U8(g_race, 0x720) |= 1;
    U8(g_race, 0xa89) = 0;
    U8(g_race, 0xa8b) = 0;
    U8(g_race, 0xa87) = 1;
    U8(g_race, 0xa86) = 1;
    U16(g_race, 0x61c) = 0;
    U8(g_race, 0xa90) = 1;
    U8(g_race, 0xa91) = 1;
    U8(g_race, 0xa92) = 0;
    U8(g_race, 0xa93) = 0;
}

/* 0x4745f0: the saved display settings (config +0x42c..+0x41f) into the
 * screen globals, when the options were loaded (0x4d4664). */
void GAMEAPI config_display_apply(void)
{
    uint8_t *c;

    if (!G32(0x4d4664))
        return;
    c = g_config;
    if (U16(c, 0x42c))
        g_screen_w = U16(c, 0x42c);
    if (U16(c, 0x42e))
        g_screen_h = U16(c, 0x42e);
    if (U16(c, 0x430))
        g_screen_bpp = U16(c, 0x430);
    G32(0x4be448) = U16(c, 0x432);
    G32(0x4acc44) = U8(c, 0x419);
    G32(0x4acc48) = U8(c, 0x41b);
    G32(0x4be444) = U8(c, 0x41a);
    G32(0x4acc58) = U8(c, 0x41c);
    G32(0x4acc4c) = U8(c, 0x41e);
    G32(0x4be44c) = U8(c, 0x41f);
}

/* 0x4740c0: load the options ("DataPC", 0x1854 bytes) and game state
 * ("DataPSX", 0xaa0) from the registry (0x474030), apply them, set the pad
 * bindings, and fill in the user name and player name. 0 when both loaded,
 * else -1. */
int32_t GAMEAPI config_load(void)
{
    uint8_t *c;
    int32_t n;

    G32(0x4d4664) = GCALL(0x474030, registry_load)(g_config, 0x1854, 0x4b22b0) != -1;
    G32(0x4d4668) = GCALL(0x474030, registry_load)(g_race, 0xaa0, 0x4b22a8) != -1;
    config_display_apply();
    bindings_reset_all();
    U8(g_race, 0x57b) = 1;
    U8(g_config, 0x6) = 0;
    if (!g_frontend_mode) {
        U16(g_config, 0x22) = 0;
        U8(g_config, 0x51) = 0xff;
        U8(g_config, 0x55) = 0xff;
        U8(g_config, 0x59) = 0xff;
        U8(g_config, 0x5d) = 0xff;
        U8(g_config, 0x61) = 0xff;
    }
    c = g_config;
    if (!U8(c, 0x120)) {
        U32(c, 0x120) = G32(0x4b11bc);
        c = g_config;
    }
    if (!U8(c, 0x12b)) {
        /* strcpy as the original's rep movs: length first, then a forward
         * copy (an 11-char name overlaps its copy at +0x12b) */
        int32_t len = 0;
        while (U8(c, 0x120 + len))
            len++;
        for (n = 0; n <= len; n++)
            U8(c, 0x12b + n) = U8(c, 0x120 + n);
        c = g_config;
    }
    G32(0x4db260) = U32(c, 0x414);
    if (G32(0x4d4664) && G32(0x4d4668))
        return 0;
    return -1;
}

/* 0x4745e0: 0x474500(0x65, 0x4b21a0). */
void GAMEAPI settings_load_65(void)
{
    GCALL(0x474500, table_copy)(0x65, (const uint32_t *)GMEM(0x4b21a0));
}

/* 0x4743e0: delete the six replay files named in the options (+0xccc,
 * 0x20 apart, 0x474360) and, with `registry`, the DataReplay value. */
void GAMEAPI replays_delete(int32_t registry)
{
    GCALL(0x474360, replay_file_delete)(g_config + 0xccc);
    GCALL(0x474360, replay_file_delete)(g_config + 0xcec);
    GCALL(0x474360, replay_file_delete)(g_config + 0xd0c);
    GCALL(0x474360, replay_file_delete)(g_config + 0xd2c);
    GCALL(0x474360, replay_file_delete)(g_config + 0xd4c);
    GCALL(0x474360, replay_file_delete)(g_config + 0xd6c);
    if (registry)
        GCALL(0x46b770, reg_value_remove)(0x4b2284, 0x4b22b8);
}

/* 0x474cb0: signal the "ATDGFXCHAINING" event, if it exists. */
void GAMEAPI chain_event_signal(void)
{
    uint32_t event = 0; /* written by 0x46b900 */

    if (GCALL(0x46b900, win_event_open)((uint32_t)(uintptr_t)&event, 0x4b2390) != -1) {
        GCALL(0x46b940, win_event_set)(event);
        GCALL(0x46b890, win_handle_close_quiet)(event);
    }
}

/* 0x45e890: *out = 0x5d5978 / 0x5d597c, or 0 (shutdown_final discards it,
 * so there the division, and its INT_MIN / -1 fault, may be dropped). */
void GAMEAPI network_average(int32_t *out)
{
    if (G32(0x5d597c)) {
        *out = GI32(0x5d5978) / GI32(0x5d597c);
        return;
    }
    *out = 0;
}

/* 0x401050: cpuid 0's vendor compared with "AuthenticAMD" (0x4a7038);
 * 1 if equal (as cpu_is_intel 0x43c370). */
int32_t GAMEAPI cpu_is_amd(void)
{
    uint32_t v[4], a = 0, b, c, d;
    const uint8_t *p = (const uint8_t *)v, *q = PTR(0x4a7038);

    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    v[0] = b;
    v[1] = d;
    v[2] = c;
    v[3] = 0;
    for (;; p++, q++) {
        if (*p != *q)
            return 0;
        if (*p == 0)
            return 1;
    }
}

/* 0x401000: the AMD 3DNow! check. */
int32_t GAMEAPI cpu_detect_amd(void)
{
    G32(0x4b5e44) = 0;
    G32(0x4b5e48) = 0;
    if (GCALL(0x41c8c0, cpuid_available)() && GCALL(0x401050, cpu_is_amd)() && GCALL(0x4010c0, cpu_has_3dnow)()) {
        G32(0x4b5e44) = 1;
        G32(0x4b5e48) = GCALL(0x4010f0, cpu_ext_level)();
    }
    return 1;
}

/* 0x43c320: the Intel check (0x43c3e0 always says no). */
int32_t GAMEAPI cpu_detect_intel(void)
{
    G32(0x4beb30) = 0;
    G32(0x4beb34) = 0;
    if (GCALL(0x41c8c0, cpuid_available)() && GCALL(0x43c370, cpu_is_intel)() && GCALL(0x43c3e0, cpu_feature_none)()) {
        G32(0x4beb30) = 1;
        G32(0x4beb34) = GCALL(0x4010f0, cpu_ext_level)();
    }
    return 1;
}

/* ------------------------------------------------------- race result flow */

/* 0x4498d0: the series (config +0x40), 0..2. */
void GAMEAPI config_series_set(int32_t series)
{
    if (series >= 0 && series <= 2)
        U8(g_config, 0x40) = (uint8_t)series;
}

/* 0x445160: config +0x9d = 1. */
void GAMEAPI config_flag_9d_set(void)
{
    U8(g_config, 0x9d) = 1;
}

/* 0x4480b0: the menu to return to (config +0x20) is the result menu; the
 * result menu becomes 5. */
void GAMEAPI result_menu_return(void)
{
    U8(g_config, 0x20) = (uint8_t)g_result_menu;
    g_result_menu = 5;
}

/* 0x445170: the race order (0x4ca84c, by race time, 6 slots, count race
 * +0x54d) and, with `points`, the championship order (0x4c24c8, by points
 * race +8 + slot after adding 0x4af668's points). In a time trial
 * (0x4cb558) slot `ghost` is not ranked first. */
void GAMEAPI race_rankings(int32_t points, int32_t ghost)
{
    uint8_t entries[6 * 0xe];
    uint32_t count = U8(g_race, 0x54d);
    uint32_t used, best, t;
    int32_t i, j, pick, max;
    uint8_t *race;

    for (i = 0; i < 6; i++) {
        uint8_t *e = entries + i * 0xe;
        e[0] = 0;
        e[1] = 0;
        e[2] = 0;
        if (i < (int32_t)count)
            time_entry_race(e, i);
        if (e[0] == 0 && e[1] == 0 && e[2] == 0) {
            e[0] = 0x59;
            e[1] = 0x59;
            e[2] = 0x99;
        }
    }
    used = 0;
    for (j = 0; j < 6; j++) {
        pick = -1;
        best = 0xffffffff;
        for (i = 0; i < 6; i++) {
            const uint8_t *e = entries + i * 0xe;
            if ((used & 1u << i) || i >= (int32_t)count)
                continue;
            t = (uint32_t)e[0] << 16 | (uint32_t)e[1] << 8 | e[2];
            if (points && G32(0x4cb558) && i == ghost && j != 5)
                t = 0xffffffff;
            if (t < best) {
                pick = i;
                best = t;
            }
        }
        if (pick != -1)
            used |= 1u << pick;
        G8(0x4ca84c + j) = (uint8_t)pick;
    }
    if (!G32(0x4cb570) && G8(0x4ca84c) != 0xff && U8(g_race, 0x551) != 7)
        U16(g_config, 0x9c0 + (int8_t)G8(0x4ca84c) * 2) += 1;
    if (!points)
        return;
    if (!G32(0x4cb570)) {
        for (j = 0; j < 6; j++) {
            int32_t s = (int8_t)G8(0x4ca84c + j);
            if (s >= 0 && s <= 6)
                U8(g_race, s + 8) += G8(0x4af668 + j);
        }
    }
    race = g_race;
    used = 0;
    for (j = 0; j < 6; j++) {
        pick = -1;
        max = -1;
        for (i = 0; i < 6; i++) {
            if ((used & 1u << i) || i >= (int32_t)count)
                continue;
            if ((int32_t)U8(race, i + 8) > max) {
                pick = i;
                max = U8(race, i + 8);
            }
        }
        if (pick != -1)
            used |= 1u << pick;
        G8(0x4c24c8 + j) = (uint8_t)pick;
    }
}

/* 0x444dd0: the league won texts (0x4ca52c..0x4ca7ac, 0x80 each) and the
 * unlock bits (race +0x720, +0x721) after the last race of a league
 * (config +0x1c: 8 Scorpio, 9 Taurus, else Gemini; race +0x585 the
 * difficulty, race +8 the points). */
void GAMEAPI league_won_texts(void)
{
    uint32_t level, section;
    int32_t index, extra_a = -1, extra_b = -1, all_points;

    U8(g_config, 0x9c) = 1;
    if (G32(0x4cb570))
        return;
    G8(0x4ca5ac) = 0;
    G8(0x4ca52c) = 0;
    G8(0x4ca6ac) = 0;
    G8(0x4ca62c) = 0;
    G8(0x4ca7ac) = 0;
    G8(0x4ca72c) = 0;
    level = U8(g_race, 0x585);
    if (U8(g_config, 0x1c) == 8) {
        all_points = U8(g_race, 8) == 0x1e;
        U8(g_race, 0x720) |= 4;
        section = 0x4af9b4;
        config_series_set(2);
        if (all_points) {
            if (level == 0)
                U8(g_race, 0x721) |= 2;
            else if (level == 1)
                U8(g_race, 0x721) |= 8;
        }
        index = 3;
    } else if (U8(g_config, 0x1c) == 9) {
        G32(0x4cb560) = 1;
        all_points = U8(g_race, 8) == 0x28;
        section = 0x4af9d0;
        if (all_points) {
            if (level == 0)
                U8(g_race, 0x721) |= 0x20;
            else if (level == 1)
                U8(g_race, 0x721) |= 0x10;
        }
        if (level == 1) {
            G32(0x4cb568) = 1;
            if (!(U8(g_race, 0x720) & 0x10)) {
                U8(g_race, 0x720) &= 0xfd;
                U8(g_race, 0x720) &= 0xfb;
            }
            U8(g_race, 0x720) |= 8;
            index = 4;
            U8(g_race, 0x720) |= 0x10;
            U8(g_race, 0x585) = 2;
            extra_a = 3;
            extra_b = 3;
            config_series_set(0);
        } else if (level == 2) {
            U8(g_race, 0x720) |= 0x80;
            index = 5;
            G32(0x4cb56c) = 1;
            config_series_set(0);
        } else {
            G32(0x4cb564) = 1;
            if (!(U8(g_race, 0x720) & 0x18)) {
                U8(g_race, 0x720) &= 0xfd;
                U8(g_race, 0x720) &= 0xfb;
            }
            U8(g_race, 0x720) |= 0x20;
            index = 3;
            extra_a = 0;
            extra_b = 1;
            U8(g_race, 0x720) |= 8;
            U8(g_race, 0x585) = 1;
            config_series_set(0);
        }
    } else {
        all_points = U8(g_race, 8) == 0x1e;
        section = 0x4af9ec;
        U8(g_race, 0x720) |= 2;
        config_series_set(1);
        if (all_points) {
            if (level == 0)
                U8(g_race, 0x721) |= 1;
            else if (level == 1)
                U8(g_race, 0x721) |= 4;
        }
        index = 3;
    }
    text_entry(0x4ca5ac, 0x80, section, 0);
    if (index != -1)
        text_entry(0x4ca52c, 0x80, section, index);
    if (level <= 1) {
        if (all_points) {
            text_entry(0x4ca6ac, 0x80, 0x4af9a0, 2);
            text_entry(0x4ca62c, 0x80, section, level + 1);
        } else {
            text_entry(0x4ca6ac, 0x80, 0x4af9a0, 0);
            text_entry(0x4ca62c, 0x80, 0x4af9a0, 1);
        }
    }
    if (extra_a != -1)
        text_entry(0x4ca7ac, 0x80, 0x4af984, extra_a);
    if (extra_b != -1)
        text_entry(0x4ca72c, 0x80, 0x4af984, extra_b);
}

/* 0x444800: after a chained restart into the front end (/FRONTEND), pick the
 * result menu (0x4af650) for the race just run (race +0x57c: 7, 8 time
 * trial, 9; race +0x551 the kind) and record its times and rankings. */
void GAMEAPI race_result_flow(void)
{
    int32_t done = 0;
    int32_t i;

    g_result_menu = 4;
    G32(0x4cb554) = 0;
    G32(0x4cb558) = 0;
    G32(0x4cb55c) = 0;
    G32(0x4cb570) = 0;
    if (I32(g_config, 0x48) != -1) {
        g_result_menu = 0;
        return;
    }
    if (G32(0x4be318)) {
        g_result_menu = (G32(0x4be31c) != 0) + 1;
        return;
    }
    G32(0x4cb570) = U8(g_config, 0x17) == 1;
    if (!G32(0x4cb570)) {
        U8(g_config, 0x9c) = 0;
        U8(g_config, 0x9d) = 0;
    }
    switch (I32(g_race, 0x57c)) {
    case 7:
        G32(0x4cb554) = 1;
        G32(0x4cb558) = 0;
        G32(0x4cb55c) = 0;
        break;
    case 8:
        G32(0x4cb554) = 1;
        G32(0x4cb558) = 1;
        G32(0x4cb55c) = 1;
        break;
    case 9:
        G32(0x4cb55c) = 0;
        G32(0x4cb554) = 1;
        G32(0x4cb558) = 1;
        break;
    default:
        if (!G32(0x4cb554))
            goto end;
        break;
    }
    if (!U8(g_config, 0x14))
        goto end;
    if (!G32(0x4cb570)) {
        records_mark_old();
        for (i = 0; i < 6; i++)
            U8(g_race, i + 0x10) -= 1;
    }
    switch (U8(g_race, 0x551)) {
    case 0:
        race_rankings(1, 0);
        if (!G32(0x4cb570))
            record_insert_slot(U8(g_race, 0x579), 0);
        if ((int32_t)U8(g_config, 0x1d) + 1 >= (int32_t)U8(g_config, 0x1e)) {
            if (!G8(0x4c24c8))
                league_won_texts();
            else
                config_flag_9d_set();
        }
        if (held_by_405380(U8(g_race, 0x579), 0))
            g_result_menu = 0x12;
        else if (!G32(0x4cb55c))
            g_result_menu = 0x14;
        done = 1;
        break;
    case 1:
        if (!G32(0x4cb570))
            record_insert_slot(U8(g_race, 0x579), 0);
        g_result_menu = 0x2b;
        if (held_by_405380(U8(g_race, 0x579), 0))
            g_result_menu = 0x29;
        done = 1;
        break;
    case 5:
    case 6:
    case 7:
        race_rankings(1, U8(g_config, 0x84));
        if (U8(g_config, 0x43)) {
            if (!G32(0x4cb570))
                records_insert_players(U8(g_race, 0x579));
            g_result_menu = 0x5d;
            if (G32(0x4cb558))
                g_result_menu = U8(g_config, 0x89);
            if (held_by_405380(U8(g_race, 0x579), U8(g_config, 0x84)))
                g_result_menu = 0x5c;
        } else {
            if (!G32(0x4cb570))
                for (i = 0; i < (int32_t)U8(g_config, 0x74); i++)
                    record_insert_slot(U8(g_race, 0x579), i);
            g_result_menu = 0x42;
            if (G32(0x4cb558))
                g_result_menu = 0x35;
            for (i = 0; i < (int32_t)U8(g_config, 0x74); i++) {
                G8(0x4c2908 + i) = 0;
                if (held_by_405380(U8(g_race, 0x579), i)) {
                    g_result_menu = 0x3d;
                    G8(0x4c2908 + i) = 1;
                }
            }
        }
        if (!U8(g_config, 0x43))
            done = 1;
        break;
    case 8:
        g_result_menu = 0x5e;
        if (!U8(g_config, 0x43))
            done = 1;
        break;
    case 9:
        race_rankings(1, 0);
        if (!G32(0x4cb570))
            record_insert_slot(U8(g_race, 0x579), 0);
        if (held_by_405380(U8(g_race, 0x579), 0))
            g_result_menu = 0x20;
        else
            g_result_menu = G32(0x4cb558) ? 0x1c : 0x22;
        done = 1;
        break;
    default:
        break;
    }
end:
    if (!G32(0x4cb558) && done)
        result_menu_return();
}

/* ------------------------------------------------------------ init steps */

/* 0x43677b: the command line (first passed to the C runtime's 0x4a1d70):
 * /PLAYBACK, /MONO=path (0x4962c0 with the unquoted path), /MONO- and
 * /MONO+ (debug flag 0x4b494c), /FRONTEND (chained restart), NOCD, all
 * case-insensitive; /W (windowed off) and /E (0x4be45c) by strstr. */
int32_t GAMEAPI command_line_parse(char *line)
{
    char *p;

    GCALL(0x4a1d70, crt_strupr)(line);
    if (stristr(line, (const char *)0x4ace2c))
        G32(0x4be3f0) = 1;
    p = stristr(line, (const char *)0x4ace38);
    if (p) {
        p = stristr(p, (const char *)0x4ace40);
        if (p) {
            p++;
            unquote_copy((char *)0x5ff0c0, p);
            GCALL(0x4962c0, debug_log_set_path)((const char *)GMEM(0x5ff0c0));
        }
    }
    if (stristr(line, (const char *)0x4ace44))
        G32(0x4b494c) = 0;
    if (stristr(line, (const char *)0x4ace4c))
        G32(0x4b494c) = 1;
    if (stristr(line, (const char *)0x4ace54)) {
        G32(0x4d4650) = 1;
        G32(0x4d4654) = 0;
        G32(0x4d4658) = 1;
        G32(0x4ac984) = 1;
    }
    if (stristr(line, (const char *)0x4ace60))
        G32(0x4be4a0) = 1;
    if (GCALL(0x4980d0, msvc_strstr)(line, (const char *)GMEM(0x4ace68))) {
        g_windowed = 0;
        G32(0x4acc60) = 0;
    }
    if (GCALL(0x4980d0, msvc_strstr)(line, (const char *)GMEM(0x4ace6c)))
        G32(0x4be45c) = 1;
    return 1;
}

/* 0x4356ab: first init step: debug variable, language check (Japanese
 * Windows refused), single instance, working directory, data archive path
 * (current directory or the installed Source path), command line, string
 * file, CD check, options, COM, CPU checks, renderer tables. */
int32_t GAMEAPI startup_init(char *line)
{
    char path[0x204];
    char source[0x204];

    G32(0x4b494c) = GCALL(0x46b9e0, env_var_exists)(0x4acc70);
    if (G32(0x4b494c))
        GCALL(0x4962c0, debug_log_set_path)((const char *)GMEM(0x4acc88));
    if (((uint32_t)GetUserDefaultLangID() & 0xffff & 0x3ff) == 0x11) {
        MessageBoxA(0, GMEM(0x4acc90), GMEM(0x4acc24), 0x10);
        return 0;
    }
    if (GCALL(0x46b870, app_mutex_create)(0x4accd0) == 1)
        return 0;
    GCALL(0x474fa0, cwd_path_quoted)((uint8_t *)GMEM(0x5fefa0));
    GCALL(0x475030, dir_change_unquoted)((const char *)GMEM(0x4acce0));
    GCALL(0x498450, msvc_strcpy)(path, (const char *)GMEM(0x4accec));
    if (!GCALL(0x475070, path_access_ok)(path) && GCALL(0x46b650, reg_string_get)(0x4acd04, 0x4accfc, (uint32_t)(uintptr_t)source, 0x104) != -1) {
        GCALL(0x474ed0, path_dir_make)((uint8_t *)source, (const uint8_t *)source);
        GCALL(0x498460, msvc_strcat)(source, (const char *)GMEM(0x4acd2c));
        GCALL(0x498460, msvc_strcat)(source, (const char *)GMEM(0x4acd40));
        if (GCALL(0x475070, path_access_ok)(source))
            GCALL(0x498450, msvc_strcpy)(path, source);
    }
    GCALL(0x496430, archive_open)(path, 0x158);
    command_line_parse(line);
    strings_load_once();
#ifdef REROLL_NATIVE
    /* Deliberate change: no CD check. The original (0x474840) wants the
     * Rollcage CD in a CD-ROM drive and otherwise runs with League, Arcade
     * and Time Attack disabled (1); the installed data (IDXData) is all
     * the game reads, and the music comes from the CD image. */
    G32(0x4acc54) = 2;
#else
    G32(0x4acc54) = GCALL(0x474840, cd_find_data_path)(g_frontend_mode);
#endif
    if (!G32(0x4acc54))
        return 0;
    if (g_frontend_mode) {
        G32(0x4b4950) = 0;
        G32(0x4db1cc) = 1;
    }
    config_defaults();
    config_load();
    language_set(U8(g_config, 0x36));
    config_default_texts(0);
    GCALL(0x476a50, arena_init)();
    if (CoInitialize(0))
        return 0;
    GCALL(0x41c930, cpu_check)();
    cpu_detect_amd();
    cpu_detect_intel();
    GCALL(0x426090, projection_init)();
    return 1;
}

/* 0x4358b0: register the window class (unless a previous instance) and
 * create the window. */
int32_t GAMEAPI startup_window(uint32_t previous)
{
    if (!previous) {
        uint32_t brush = (uint32_t)(uintptr_t)GetStockObject(4);
        if (!(GCALL(0x477130, window_class_register)(0x435ed1, 0x65, brush) & 0xffff))
            return 0;
    }
    g_window = (uint32_t)GCALL(0x4771c0, create_window_body)(1, 0, 0, g_screen_w, g_screen_h, g_windowed);
    if (!g_window)
        return 0;
    SetForegroundWindow(GMEM(g_window));
    SetFocus(GMEM(g_window));
    return 1;
}

/* 0x435932: option state for the session: the display dialog flag
 * (config +0xa), and either the chained restart's result flow or the old
 * replays deleted. */
int32_t GAMEAPI startup_options(void)
{
    G32(0x4be440) = U8(g_config, 0xa);
    if (g_frontend_mode) {
        ShowWindow(GMEM(g_window), 1);
        chain_event_signal();
        if (G32(0x4d4658))
            race_result_flow();
        G32(0x4be440) = 1;
    } else {
        replays_delete(1);
        GCALL(0x474470, replay_files_delete_all)();
        U8(g_config, 0x410) = (uint8_t)G32(0x4be4a0);
        U8(g_config, 0x3b) = 0;
        U32(g_config, 0x48) = 0xffffffff;
        U32(g_config, 0x4c) = 0xffffffff;
    }
    U8(g_config, 0x82) = 0;
    return 1;
}

/* 0x4359d3: sound settings and the sound engine (0x46f4f0). */
int32_t GAMEAPI startup_sound(void)
{
    settings_load_65();
    G32(0x4d4538) = g_window;
    GCALL(0x46f4f0, snd_init)();
    return 1;
}

/* 0x435aae: DirectDraw (0x487e10) and Direct3D (0x41ced0) for the window. */
int32_t GAMEAPI startup_direct3d(void)
{
    int32_t rect[4];

    GetClientRect(GMEM(g_window), rect);
    GCALL(0x4877d0, dd_window_origin)(g_window, rect[2], rect[3]);
    if (!GCALL(0x487e10, dd_start_3d)(g_windowed, G32(0x4acc60), g_screen_w, g_screen_h, 1, G32(0x4be45c), g_screen_bpp)) {
        GCALL(0x487210, dd_shutdown)();
        return 0;
    }
    if (!GCALL(0x41ced0, d3d_init)(G32(0x4be45c), G32(0x6cce7c))) {
        GCALL(0x41d130, d3d_shutdown)();
        GCALL(0x487210, dd_shutdown)();
        return 0;
    }
    GCALL(0x41eeb0, surfaces_clear)();
    GCALL(0x41e040, viewport_clear)(0);
    GCALL(0x4876f0, flip_body)();
    GCALL(0x428270, zbuffer_restore)();
    return 1;
}

/* 0x4370b2: the display mode dialog (resource 0x81) unless skipped
 * (0x4be440): the chosen mode's width, height and depth (mode list
 * 0x4d5fe4, 12 bytes each, index in the high word); -2 runs the uninstaller
 * (0x474b90). */
int32_t GAMEAPI startup_mode_dialog(void)
{
    uint32_t result = 0;

#ifdef REROLL_NATIVE
    return 1; /* no launcher dialog natively: the saved display settings, as with /FRONTEND */
#endif
    if (G32(0x4be440))
        return 1;
    GCALL(0x487d70, dd_probe)();
    if (!G32(0x4be440)) {
        result = (uint32_t)DialogBoxParamA(GMEM(g_instance), GMEM(0x81), GMEM(g_window), (void *)(uintptr_t)0x437171, 0);
        if (result != 0xffffffff && result != 0xfffffffe) {
            g_screen_w = G32(0x4d5fe4 + (result >> 16) * 0xc);
            g_screen_h = G32(0x4d5fe8 + (result >> 16) * 0xc);
            g_screen_bpp = G32(0x4d5fec + (result >> 16) * 0xc);
        }
    }
    GCALL(0x487210, dd_shutdown)();
    if (result == 0xfffffffe) {
        GCALL(0x474b90, setup_program_launch)();
        return 0;
    }
    if (result == 0xffffffff)
        return 0;
    return 1;
}

/* ---------------------------------------------------------------- shutdown */

/* 0x43758f: the error dialog (resource 0x83) for a pending error
 * (config +0x48). */
void GAMEAPI error_dialog(void)
{
    if (I32(g_config, 0x48) != -1) {
        DialogBoxParamA(GMEM(g_instance), GMEM(0x83), 0, (void *)(uintptr_t)0x4375c7, 0);
        U32(g_config, 0x48) = 0xffffffff;
    }
}

/* 0x4365a4: stop the frame timer (0x477090), wait up to a second for its
 * callback to finish (0x4be468). */
int32_t GAMEAPI timer_stop(void)
{
    uint32_t start = game_time();

    GCALL(0x477090, mm_timer_stop_default)((uint32_t *)GMEM(0x4be458));
    while (G32(0x4be468) && game_time() < start + 1000)
        ;
    G32(0x4be46c) = 0;
    return 1;
}

/* 0x435e2c: shut everything down once (0x4be4a8): timer, C runtime
 * (0x496700), sound, front end, network, input, Direct3D, DirectDraw,
 * memory pool; then exit (0x496410) or the error dialog. */
void GAMEAPI shutdown_all(int32_t quit)
{
    if (G32(0x4be4a8))
        return;
    G32(0x4be4a8) = 1;
    timer_stop();
    GCALL(0x496700, cdaudio_close)();
    GCALL(0x46fb00, snd_waves_free)();
    GCALL(0x46f610, snd_shutdown)();
    frontend_free_all();
    nop_4291f0();
    GCALL(0x48a8b0, dplay_shutdown)();
    nop_4291f0();
    GCALL(0x489560, input_shutdown)();
    GCALL(0x41e190, d3d_scene_end)();
    GCALL(0x41d130, d3d_shutdown)();
    GCALL(0x487210, dd_shutdown)();
    GCALL(0x476a90, arena_shutdown)();
    if (quit)
        GCALL(0x496410, debug_log_close)();
    else
        error_dialog();
}

/* 0x435e99: the window is closing (0x4be48c): chained restart (0x4d4660:
 * shut down and relaunch with /FRONTEND, 0x474cf0) or delete the replays. */
void GAMEAPI shutdown_restart(void)
{
    if (G32(0x4d4660)) {
        G32(0x4acc34) = 0;
        shutdown_all(1);
        GCALL(0x474cf0, chain_launch_and_wait)();
    } else {
        replays_delete(1);
        GCALL(0x474470, replay_files_delete_all)();
    }
}

/* 0x435515: last shutdown: COM, working directory back, cursor, instance
 * mutex. */
void GAMEAPI shutdown_final(void)
{
    int32_t average;

    network_average(&average);
    CoUninitialize();
    GCALL(0x475030, dir_change_unquoted)((const char *)GMEM(0x5fefa0));
    if (G32(0x4acc34))
        ShowCursor(1);
    GCALL(0x46b850, app_mutex_close)();
}

/* --------------------------------------------------------------- main loop */

static void frame_failed(void)
{
    int32_t i;

    GCALL(0x428390, clear_colour_store)(0, 0, 0);
    for (i = 0; i < 2; i++)
        present_frame();
    fatal_exit();
}

/* 0x4351e0: the WinMain body (stdcall, 4 arguments): init, then the message
 * pump and frame loop until quit (0x4be460) or a display change
 * (0x4be450 == 1, 0x435607 restarts the display); returns the last
 * message's wParam. */
int32_t STDCALL game_main(uint32_t instance, uint32_t previous, char *line, int32_t show)
{
    uint32_t msg[7] = {0}; /* MSG; the original's is uninitialised until the first PeekMessage */
    uint32_t start;

    (void)show;
    g_instance = instance;
    G32(0x6c7ff8) = game_time();
    U32(g_config, 0x48) = 0xffffffff;
    if (!startup_init(line)) {
        chain_event_signal();
        error_dialog();
        return 0;
    }
    if (g_windowed && g_frontend_mode)
        ShowCursor(0);
    if (!startup_window(previous)) {
        chain_event_signal();
        error_dialog();
        return 0;
    }
    if (!startup_options()) {
        error_dialog();
        return 0;
    }
    if (!g_frontend_mode) {
        if (!startup_mode_dialog()) {
            error_dialog();
            return 0;
        }
        if (g_windowed)
            ShowCursor(0);
    }
    if (!startup_sound() || !GCALL(0x4359f1, startup_movies)() || !startup_direct3d()) {
        error_dialog();
        return 0;
    }
    for (;;) {
        GCALL(0x436b9d, loading_screen_show)(0);
        start = game_time();
        if (!GCALL(0x435b64, game_load_step)())
            fatal_exit();
        if (!GCALL(0x41e170, d3d_scene_begin)())
            fatal_exit();
        while (game_time() < start + 3000)
            ;
        while (!G32(0x4be460) && G32(0x4be450) != 1) {
            if (PeekMessageA(msg, 0, 0, 0, 0)) {
                if (!GetMessageA(msg, 0, 0, 0))
                    break;
                TranslateMessage(msg);
                DispatchMessageA(msg);
            }
            GCALL(0x43c420, keyboard_update)();
            if (G32(0x4af444))
                GCALL(0x43605a, debug_key_event)(G32(0x4af440));
            if (G32(0x4be484) && !G32(0x4be488))
                continue;
            G32(0x4be488) = 0;
            if (!G32(0x4be460) && !G32(0x4be48c)) {
                GCALL(0x4369b5, controls_gather)();
                if (G32(0x4ac984)) {
                    if (!GCALL(0x4302d0, frontend_frame)())
                        frame_failed();
                } else {
                    if (!GCALL(0x4440b0, race_frame)())
                        frame_failed();
                }
                if (G32(0x4be464)) {
                    GCALL(0x428850, view_rect_set)(0, 0, g_screen_w, g_screen_h);
                    if (!GCALL(0x41efe0, texture_page_show)(G32(0x4be478)))
                        G32(0x4be478) = 0;
                }
                if (!present_frame())
                    fatal_exit();
                GCALL(0x435552, frame_net_step)();
            }
            if (G32(0x4be48c)) {
                shutdown_restart();
                DestroyWindow(GMEM(g_window));
                G32(0x4be48c) = 0;
            }
        }
        if (G32(0x4be450) != 1 || !GCALL(0x435607, front_end_return)())
            break;
    }
    shutdown_final();
    return (int32_t)msg[2];
}
