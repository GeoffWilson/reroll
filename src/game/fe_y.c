/* Front end, group Y: the records and results screens (best times, race
 * reports, game points), the world / league / players menus, the option
 * entries that hand off to the shared option builders, the track tables
 * behind the world and records screens, and the network player slots.
 *
 * Records: the options block (*0x4b2198, the save data) holds 16 tracks x 6
 * records of 0xe bytes at +0x444: +0 time (3 BCD bytes), +3 name (10
 * bytes), +0xd flags (bit 0 new, bit 1 the player's). A car slot's times
 * are dwords at race +0x24 + slot * 0xdc (the low three bytes BCD).
 *
 * Menu screens are built from text items (0x4c30e0, 0x40 of 0x1a0 bytes,
 * 0x448a50) and menu entries (0x448140, still original); the entry made
 * current is remembered per menu in the options block (0x448b50). The
 * string lookups, entry builders, text layout and sprintf (0x4979f0) are
 * still original. Text measuring and layout (0x42e810, 0x42f070, 0x42f0b0,
 * 0x42f2d0, 0x44cc50, 0x44ccf0, 0x44ce20, 0x44ce40) is drawing and parked
 * for the new renderer. */
#include "../runtime.h"
#include "physics.h"

#define g_opts (*(uint8_t **)0x4b2198)
#define g_race (*(uint8_t **)0x4b1960)

#define I32(p, off) (*(int32_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define U8(p, off) (*(uint8_t *)((p) + (off)))

/* Front end globals. */
#define g_text_count (*(int32_t *)0x4cb550)  /* text items in use */
#define g_texts ((uint8_t *)0x4c30e0)         /* 0x40 of 0x1a0 */
#define g_entries (*(uint8_t **)0x4cb4b8)    /* menu entries, 0x730 each */
#define g_menu_slot (*(int32_t *)0x4af64c)   /* this menu's index in the options' memory */
#define g_select_enabled (*(int32_t *)0x4cb590)
#define g_selected (*(int8_t *)0x4cb588)
#define g_entry_next_flag (*(uint8_t *)0x4c9dfd) /* the next entry is greyed out */
#define g_entry_value (*(uint32_t *)0x4ca2f0)
#define g_entry_action (*(uint32_t *)0x4ca2fc)
#define g_entry_draw (*(uint32_t *)0x4ca300)
#define g_menu_back (*(uint32_t *)0x4cb524)   /* the screen's back / continue handler */

/* Still original. */
typedef int32_t(GAMEAPI *entry_add_fn)(const char *table, int32_t index, int32_t a, int32_t b, int32_t c);
typedef void(GAMEAPI *string_fn)(char *buf, int32_t size, const char *table, int32_t index);
typedef void(GAMEAPI *text_set_fn)(uint8_t *text, const char *s);
typedef void(GAMEAPI *name_fn)(char *buf, int32_t index);
typedef int32_t(GAMEAPI *int_int_fn)(int32_t a);
typedef void(GAMEAPI *menu_begin_fn)(const char *name, int32_t a);
typedef void(GAMEAPI *name_edit_fn)(const uint8_t *field, int32_t size, int32_t a, uint32_t b);
typedef void(GAMEAPI *name_entry_fn)(int32_t a, uint8_t *buf, int32_t size, int32_t b, int32_t c, uint32_t done,
                                     int32_t d);
typedef void(GAMEAPI *text_append_fn)(int32_t text, int32_t a, const char *s);
typedef int32_t(GAMEAPI *option6_fn)(const char *t1, int32_t i1, const char *t2, int32_t i2, uint32_t action,
                                     int32_t a);
typedef int32_t(GAMEAPI *option8_fn)(int32_t a, int32_t b, int32_t c, int32_t d, const char *t1, int32_t i1,
                                     const char *t2, int32_t i2);
typedef void(GAMEAPI *void_fn)(void);
typedef int32_t(GAMEAPI *submenu_fn)(uint32_t action, int32_t a);
typedef int32_t(__cdecl *sprintf_fn)(char *buf, const char *format, ...);

#define string_lookup ((string_fn)0x42e6d0)
#define game_time ((int32_t(GAMEAPI *)(void))0x436935)
#define entry_add ((entry_add_fn)0x448140)
#define menu_string ((string_fn)0x4481a0)
#define text_set ((text_set_fn)0x448260)
#define car_name ((name_fn)0x448430)
#define car_kind_name ((name_fn)0x4484a0)
#define world_entry_action ((int_int_fn)0x448770)
#define menu_begin ((menu_begin_fn)0x448a00)
#define name_edit ((name_edit_fn)0x448ec0)
#define name_entry ((name_entry_fn)0x449060)
#define text_append ((text_append_fn)0x44a1c0)
#define option_cycle ((option6_fn)0x44c6d0)
#define option_toggle ((option8_fn)0x44c850)
#define option_list ((option6_fn)0x44c9d0)
#define worlds_prepare ((void_fn)0x44e560)
#define submenu_open ((submenu_fn)0x44ffd0)
#define fe_sprintf ((sprintf_fn)0x4979f0)

static void copy_string(char *dst, const char *src)
{
    do
        *dst++ = *src;
    while (*src++);
}

/* --- records and times --- */

/* 0x4053e0: player `slot`'s name (10 bytes) in the options, or 0. */
uint8_t *GAMEAPI player_name_at(int32_t slot)
{
    if ((uint32_t)slot > 5)
        return 0;
    return g_opts + 0x984 + slot * 10;
}

/* 0x405480: a time entry (14 bytes): the BCD time of car slot `slot`
 * (dword 1 with `laps` set, 2 without; lap `lap` + 3 when 0..9 and
 * `laps`), its name, and the slot in bits 3..6 of +0xd. */
void GAMEAPI time_entry_fill(uint8_t *out, int32_t slot, int32_t lap, int32_t laps)
{
    const uint8_t *name;
    int32_t index;
    uint32_t time;

    if (!out)
        return;
    U32(out, 3) = 0;
    U32(out, 7) = 0;
    U16(out, 0xb) = 0;
    if (slot < 0 || slot >= 6) {
        out[0] = out[1] = out[2] = 0xff;
        out[0xd] = (uint8_t)((out[0xd] & 0xfe) | 0x7a);
        return;
    }
    index = laps ? 1 : 2;
    if (laps && lap >= 0 && lap <= 9)
        index = lap + 3;
    time = U32(g_race, 0x24 + slot * 0xdc + index * 4);
    out[0] = (uint8_t)(time >> 16);
    out[1] = (uint8_t)(time >> 8);
    out[2] = (uint8_t)time;
    out[0xd] = (uint8_t)((slot & 0xf) << 3 | (out[0xd] & 0x87) | 3);
    name = player_name_at(slot);
    if (name) {
        U32(out, 3) = U32(name, 0);
        U32(out, 7) = U32(name, 4);
        U16(out, 0xb) = U16(name, 8);
    }
}

/* 0x405460 */
void GAMEAPI time_entry_make(uint8_t *out, int32_t slot, int32_t laps)
{
    time_entry_fill(out, slot, -1, laps);
}

static uint8_t *record_at(int32_t track, int32_t place)
{
    return g_opts + 0x444 + (place + track * 6) * 0xe;
}

/* 0x4058f0: record `place` of `track` is new. */
uint32_t GAMEAPI record_is_new(int32_t track, int32_t place)
{
    if (track < 0 || track >= 0x10 || place < 0 || place >= 6)
        return 0;
    return record_at(track, place)[0xd] & 1;
}

/* 0x405930: record `place` of `track` is the player's. */
uint32_t GAMEAPI record_is_players(int32_t track, int32_t place)
{
    if (track < 0 || track >= 0x10 || place < 0 || place >= 6)
        return 0;
    return record_at(track, place)[0xd] >> 1 & 1;
}

/* 0x405970: the record's name (11 bytes cleared, then the name). */
char *GAMEAPI record_name(char *out, int32_t track, int32_t place)
{
    char name[12];
    const uint8_t *record;
    int i;

    if (!out)
        return out;
    for (i = 0; i < 11; i++)
        out[i] = 0;
    if ((track >= 0 && track < 0x10) || (place >= 0 && place < 6)) {
        record = record_at(track, place);
        for (i = 0; i < 10; i++)
            name[i] = (char)record[3 + i];
        name[10] = 0;
        copy_string(out, name);
    }
    return out;
}

/* 0x405b20: a byte as two hex digits. */
void GAMEAPI hex_pair(char *out, uint32_t value)
{
    const char *digits = (const char *)0x4a76a8;

    out[0] = digits[(int32_t)value >> 4 & 0xf];
    out[1] = digits[value & 0xf];
}

/* 0x405a80: a BCD time (3 bytes) as "MM:SS:hh"; none ("YY\x99" or zero)
 * as "--:--:--". */
void GAMEAPI bcd_time_text(char *out, const uint8_t *bcd)
{
    const char *blank = (const char *)0x4a7690, *none = (const char *)0x4a769c;

    if (!out)
        return;
    U32(out, 0) = U32(blank, 0);
    U32(out, 4) = U32(blank, 4);
    out[8] = blank[8];
    if ((bcd[0] == 'Y' && bcd[1] == 'Y' && bcd[2] == 0x99) || (bcd[0] == 0 && bcd[1] == 0 && bcd[2] == 0)) {
        U32(out, 0) = U32(none, 0);
        U32(out, 4) = U32(none, 4);
        out[8] = none[8];
        return;
    }
    hex_pair(out, bcd[0]);
    hex_pair(out + 3, bcd[1]);
    hex_pair(out + 6, bcd[2]);
}

/* 0x405a10: record `place` of `track`'s time as text. */
void GAMEAPI record_time_text(char *out, int32_t track, int32_t place)
{
    const char *blank = (const char *)0x4a7690;

    if (!out)
        return;
    U32(out, 0) = U32(blank, 0);
    U32(out, 4) = U32(blank, 4);
    out[8] = blank[8];
    if (track >= 0 && track < 0x10 && place >= 0 && place < 6)
        bcd_time_text(out, record_at(track, place));
}

/* 0x405b70: car slot `slot`'s time (`laps`: dword 1, else 2) as text. */
void GAMEAPI slot_time_text(char *out, int32_t slot, int32_t laps)
{
    uint8_t entry[16] = {0};

    time_entry_make(entry, slot, laps);
    bcd_time_text(out, entry);
}

/* 0x405ba0 */
void GAMEAPI slot_lap_time_text(char *out, int32_t slot)
{
    slot_time_text(out, slot, 1);
}

/* 0x405bf0 */
void GAMEAPI slot_race_time_text(char *out, int32_t slot)
{
    slot_time_text(out, slot, 0);
}

/* --- track tables --- */

/* 0x433550: the world (0..3) track `track` belongs to, or -1. */
int32_t GAMEAPI track_world(int32_t track)
{
    if (track < 0 || track > 0x13)
        return -1;
    return ((const uint8_t *)0x4aca88)[track];
}

/* 0x433570: the same as a world number 1..4, or -1. */
int32_t GAMEAPI track_world_number(int32_t track)
{
    switch (track_world(track)) {
    case 0:
        return 1;
    case 1:
        return 2;
    case 2:
        return 3;
    case 3:
        return 4;
    default:
        return -1;
    }
}

/* 0x4335c0: track `track`'s place within its world, or -1. */
int32_t GAMEAPI track_world_place(int32_t track)
{
    if (track < 0 || track > 0x13)
        return -1;
    return ((const uint8_t *)0x4aca9c)[track];
}

/* 0x4335e0: the track at `place` in world 1..4, or -1. */
int32_t GAMEAPI world_track(int32_t world, int32_t place)
{
    const uint8_t *tracks = (const uint8_t *)0x4ac964, *first = (const uint8_t *)0x4ac978;

    if (world < 1 || world > 4)
        return -1;
    return tracks[place + first[world - 1]];
}

/* 0x448670: the number of tracks in world 1..4. */
int32_t GAMEAPI world_track_count(int32_t world)
{
    switch (world) {
    case 1:
    case 3:
        return 3;
    case 2:
        return 5;
    case 4:
        return 4;
    default:
        return 0;
    }
}

/* 0x4486b0: world `world`'s locked track bits (from race +0x721). */
uint32_t GAMEAPI world_locked_tracks(int32_t world)
{
    if (world == 2)
        return U8(g_race, 0x721) & 0x20 ? 0 : 0x10;
    if (world == 3)
        return U8(g_race, 0x721) & 0x10 ? 0 : 4;
    return 0;
}

/* 0x4486f0: world 1..4's track-name string table, or 0. */
const char *GAMEAPI world_track_names(int32_t world)
{
    switch (world) {
    case 1:
        return (const char *)0x4afb14;
    case 2:
        return (const char *)0x4afafc;
    case 3:
        return (const char *)0x4afae4;
    case 4:
        return (const char *)0x4afacc;
    default:
        return 0;
    }
}

/* --- menu building --- */

/* 0x448000: start the attract-mode timer (options +0x12 ms, default 30000). */
void GAMEAPI attract_timer_start(void)
{
    if (!U16(g_opts, 0x12))
        U16(g_opts, 0x12) = 30000;
    *(int32_t *)0x4cb534 = game_time() + U16(g_opts, 0x12);
}

/* 0x448530: player `slot` (0..6)'s network name (11 bytes from options
 * +0xd8c), or "". */
void GAMEAPI player_network_name(char *out, int32_t slot)
{
    out[0] = 0;
    if (slot >= 0 && slot <= 6)
        copy_string(out, (const char *)g_opts + 0xd8c + slot * 11);
}

/* 0x448a50: a text item on the current screen; its index, or -1 when all
 * 0x40 are in use. */
int32_t GAMEAPI menu_text_add(const char *s, uint32_t flags, int32_t colour, int32_t x, int32_t y, int32_t a,
                              int32_t b, int32_t palette, int32_t c)
{
    uint8_t *text;
    int32_t index;

    if (g_text_count >= 0x40)
        return -1;
    text = g_texts + g_text_count * 0x1a0;
    U32(text, 0x44) |= flags;
    text[6] = (uint8_t)colour;
    I32(text, 8) = x;
    I32(text, 0xc) = y;
    text[0x18] = (uint8_t)a;
    text[0x19] = (uint8_t)b;
    if (palette)
        I32(text, 0x190) = palette;
    if (c != -1)
        I32(text, 0x194) = c;
    text_set(text, s);
    index = g_text_count;
    g_text_count = index + 1;
    return index;
}

/* 0x448b50: make menu entry `entry` current and remember it for this menu;
 * returns what the original leaves in eax. */
int32_t GAMEAPI menu_entry_select(int32_t entry)
{
    if (!g_select_enabled || g_menu_slot < 0 || g_menu_slot >= 0x82)
        return g_select_enabled;
    if (entry == -1)
        return -1;
    g_opts[g_menu_slot + 0x9cc] = (uint8_t)entry;
    g_selected = (int8_t)entry;
    *(int32_t *)0x4cb598 = 1;
    *(int32_t *)0x4cb59c = g_select_enabled;
    return g_select_enabled;
}

/* 0x448ba0: an entry from the common strings (a: it is the back entry). */
int32_t GAMEAPI menu_common_entry(int32_t back, int32_t index, int32_t x, int32_t y, int32_t c)
{
    if (back)
        *(int32_t *)0x4cb4e0 = 0x1ae;
    return entry_add((const char *)0x4afb44, index, x, y, c);
}

/* 0x448c00: a menu entry with a text from `text_table`, at (a, b). */
void GAMEAPI menu_entry_with_text(const char *table, int32_t index, const char *text_table, int32_t text_index,
                                  int32_t a, int32_t b, int32_t c)
{
    char buf[0x200];
    uint8_t *entry;
    int32_t i;

    *(uint8_t *)0x4c9e00 = *(int32_t *)0x4af5f0 == -1;
    i = entry_add(table, index, 0x73, 1, c);
    if (i == -1)
        return;
    menu_string(buf, 0x200, text_table, text_index);
    entry = g_entries + i * 0x730;
    I32(entry, 0x4f8) = a;
    entry = g_entries + i * 0x730;
    I32(entry, 0x4fc) = b;
    copy_string((char *)g_entries + i * 0x730 + 0x1fc, buf);
    U32(g_entries + i * 0x730, 0x508) = 0x448cf0;
}

/* 0x448be0: the common footer entry. */
void GAMEAPI menu_common_footer(int32_t unused, int32_t c)
{
    (void)unused;
    menu_entry_with_text((const char *)0x4afb44, 1, (const char *)0x4afb44, 1, 4, 1, c);
}

/* 0x448b10: the back entry (common string `index`) with its footer, made
 * current. */
int32_t GAMEAPI menu_back_entry(int32_t back, int32_t index, int32_t x, int32_t y, int32_t c)
{
    int32_t entry = menu_common_entry(back, index, x, y, c);

    menu_common_footer(back, c);
    return menu_entry_select(entry);
}

/* 0x448af0 */
int32_t GAMEAPI menu_back_footer(int32_t back, int32_t x, int32_t y, int32_t c)
{
    return menu_back_entry(back, 0, x, y, c);
}

/* 0x44e410 */
int32_t GAMEAPI menu_continue_entry(int32_t back, int32_t x, int32_t y, int32_t c)
{
    return menu_common_entry(back, 2, x, y, c);
}

/* 0x44c820: an enabled / disabled option entry. */
int32_t GAMEAPI option_enabled_entry(int32_t a, int32_t b, int32_t c, int32_t d)
{
    return option_toggle(a, b, c, d, (const char *)0x4b0078, 0, (const char *)0x4b0078, 1);
}

/* 0x448d70: the deathmatch report: each player's name and kills. */
void GAMEAPI deathmatch_report(int32_t unused, int32_t players, int32_t x, int32_t y)
{
    char buf[0x200];
    int32_t i, row = 200, colour;

    (void)unused;
    menu_begin((const char *)0x4afb64, 0);
    menu_string(buf, 0x200, 0, 1);
    menu_text_add(buf, 0, 1, *(int32_t *)0x4af750, 0xa0, 0, 1, 0x4ac9cc, -1);
    for (i = 0; i < players; i++, row += 0x1e) {
        colour = (i & 1) + 1;
        if (U8(g_opts, 0x43) && U8(g_opts, i + 0x8c))
            player_network_name(buf, i);
        else
            car_name(buf, i);
        menu_text_add(buf, 0, colour, *(int32_t *)0x4af74c, row, 0, 1, 0x4ac99c, -1);
        fe_sprintf(buf, (const char *)0x4afb5c, U8(g_race, i + 8));
        menu_text_add(buf, 0, colour, *(int32_t *)0x4af750 + 10, row, 0, 1, 0x4ac9cc, -1);
    }
    menu_back_footer(1, x, y, 0x33);
    g_menu_back = 0x448d60;
}

/* 0x44dfd0: the name entry screen for the player's name (options +0x120). */
void GAMEAPI player_name_entry(int32_t a, int32_t b)
{
    uint8_t *edit = (uint8_t *)0x4c2f00;

    U32(edit, 0) = U32(g_opts, 0x120);
    U32(edit, 4) = U32(g_opts, 0x124);
    U16(edit, 8) = U16(g_opts, 0x128);
    edit[0xa] = U8(g_opts, 0x12a);
    name_entry(1, edit, 0xb, a, b, 0x44e040, 0);
    name_edit(g_opts + 0x120, 0x100, 0, *(uint32_t *)0x4ac848);
}

/* 0x44e2a0: league 7..9's name, or "". */
void GAMEAPI league_title(char *out, int32_t league)
{
    char buf[0x200];
    int32_t index;

    out[0] = 0;
    if (league == 7)
        index = 0;
    else if (league == 8)
        index = 1;
    else if (league == 9)
        index = 2;
    else
        return;
    string_lookup(buf, 0x200, (const char *)0x4b00d0, index);
    copy_string(out, buf);
}

static void world_menu_begin(void)
{
    *(int32_t *)0x4cb4dc = 0x1e;
    *(int32_t *)0x4af5fc = 0;
    *(int32_t *)0x4cb4e0 = 0x82;
    *(int32_t *)0x4af600 = 0;
}

/* 0x44e470: the world menu: worlds 1..6, worlds 5 and 6 (the track titles)
 * at (x, y) (kept from the last call when -1). */
void GAMEAPI world_menu(int32_t x, int32_t y)
{
    const uint32_t *world;
    int32_t action, entry;

    world_menu_begin();
    if (x != -1)
        *(int32_t *)0x4cb0d4 = x;
    if (y != -1)
        *(int32_t *)0x4cb0d0 = y;
    worlds_prepare();
    menu_begin((const char *)0x4b00e8, 0);
    for (world = (const uint32_t *)0x4af714; world < (const uint32_t *)0x4af72c; world++) {
        g_entry_action = 0x44e590;
        g_entry_value = *world;
        action = world_entry_action((int32_t)*world);
        if (*world == 5)
            entry = entry_add((const char *)0x4afa88, 2, *(int32_t *)0x4cb0d4, *(int32_t *)0x4cb0d0, action);
        else if (*world == 6)
            entry = entry_add((const char *)0x4afa88, 3, *(int32_t *)0x4cb0d4, *(int32_t *)0x4cb0d0, action);
        else
            entry = entry_add(0, -1, 0x81, 0, action);
        if (U8(g_opts, 0x41) == *world)
            menu_entry_select(entry);
    }
}

/* 0x44e5b0: the four-world menu with fixed actions. */
void GAMEAPI worlds_menu_plain(int32_t a, int32_t b, int32_t c, int32_t d)
{
    world_menu_begin();
    menu_begin((const char *)0x4b00e8, 0);
    entry_add(0, -1, a, 1, 0x3b);
    entry_add(0, -1, b, 1, 0x39);
    entry_add(0, -1, c, 1, 0x3a);
    entry_add(0, -1, d, 1, 0x3c);
}

/* 0x44e640: the four-world menu, worlds not yet open (race +0x721 bits
 * 0..3) greyed out, the last world (options +0x1d) made current. */
void GAMEAPI worlds_menu(int32_t x, int32_t y)
{
    int32_t first, second, third, fourth, entry;

    world_menu_begin();
    menu_begin((const char *)0x4b00e8, 0);
    g_entry_draw = 0x44e7e0;
    g_entry_value = 0xf;
    g_entry_next_flag = (uint8_t)(~U8(g_race, 0x721) & 1);
    first = entry_add(0, -1, x, y, 0x3b);
    g_entry_draw = 0x44e7e0;
    g_entry_value = 0x11;
    g_entry_next_flag = (uint8_t)((uint8_t)~U8(g_race, 0x721) >> 1 & 1);
    second = entry_add(0, -1, x, y, 0x39);
    g_entry_draw = 0x44e7e0;
    g_entry_value = 0x10;
    g_entry_next_flag = (uint8_t)((uint8_t)~U8(g_race, 0x721) >> 2 & 1);
    third = entry_add(0, -1, x, y, 0x3a);
    g_entry_draw = 0x44e7e0;
    g_entry_value = 0x12;
    g_entry_next_flag = (uint8_t)((uint8_t)~U8(g_race, 0x721) >> 3 & 1);
    fourth = entry_add(0, -1, x, y, 0x3c);
    if (!(U8(g_race, 0x721) & 0xf)) {
        menu_entry_select(menu_continue_entry(0, x, y, 1));
        return;
    }
    switch (U8(g_opts, 0x1d)) {
    case 0x10:
        entry = third;
        break;
    case 0x11:
        entry = second;
        break;
    case 0x12:
        entry = fourth;
        break;
    default:
        entry = first;
        break;
    }
    menu_entry_select(entry);
}

/* 0x44ebf0: the records screen for world 1..4: track `place` (-1: the last
 * one seen in this menu, skipping locked tracks) with its six records. */
void GAMEAPI records_menu(int32_t world, int32_t place, int32_t x, int32_t y)
{
    char buf[0x200];
    int32_t count = world_track_count(world), track, i, row, fixed;
    uint32_t locked = world_locked_tracks(world), flags, palette;
    const char *names = world_track_names(world);
    uint8_t shown;

    *(int32_t *)0x4cb594 = 1;
    menu_begin((const char *)0x4b0110, 0);
    if (place == -1) {
        shown = g_opts[g_menu_slot + 0xbcc];
        for (i = 0;;) {
            if ((int32_t)shown >= count)
                shown = (uint8_t)(count - 1);
            if (!(locked & 1u << (shown & 0x1f)))
                break;
            shown--;
            if (++i >= 0x20)
                break;
        }
        g_opts[g_menu_slot + 0xbcc] = shown;
        place = shown;
        *(int32_t *)0x4ca528 = count;
        fixed = 0;
        *(uint32_t *)0x4c2910 = locked;
        g_menu_back = 0x44eeb0;
        *(int32_t *)0x4cb4f4 = 1;
        *(int32_t *)0x4cb4f8 = 1;
    } else {
        if (place >= count)
            place = count - 1;
        if (place < 0)
            place = 0;
        g_menu_back = 0x448d60;
        fixed = 1;
    }
    track = world_track(world, place);
    menu_string(buf, 0x200, names, place);
    menu_text_add(buf, 0, 0, 0x1fa, 0x3d, 1, 1, 0, -1);
    menu_string(buf, 0x200, (const char *)0x4b00f8, 1);
    menu_text_add(buf, 0, 1, 0x32, 0xa0, 0, 1, 0x4aca1c, -1);
    menu_string(buf, 0x200, (const char *)0x4b00f8, 0);
    menu_text_add(buf, 0, 1, 0x122, 0xa0, 0, 1, 0x4aca1c, -1);
    for (i = 0, row = 200; i < 6; i++, row += 0x1e) {
        flags = 0;
        palette = 0x4aca3c;
        if (fixed && record_is_new(track, i))
            flags = 0x40000000;
        if (record_is_players(track, i))
            palette = 0x4ac9bc;
        record_name(buf, track, i);
        menu_text_add(buf, flags, 1, 0x32, row, 0, 1, (int32_t)palette, -1);
        record_time_text(buf, track, i);
        menu_text_add(buf, flags | 8, 1, 0x122, row, 0, 1, (int32_t)palette, -1);
    }
    if (fixed)
        menu_back_footer(1, x, y, -1);
    else
        menu_continue_entry(1, x, y, -1);
}

/* 0x44efa0 */
void GAMEAPI records_menu_world2(int32_t x, int32_t y)
{
    records_menu(2, -1, x, y);
}

/* 0x44f000: the records screen for `track`. */
void GAMEAPI records_menu_track(int32_t track, int32_t x, int32_t y)
{
    int32_t place = track_world_place(track);

    records_menu(track_world_number(track), place, x, y);
}

/* 0x44f030: the split-screen race report: position, name, car, both times
 * and (with `points`) the points of each finisher in order (0x4c24c8). */
void GAMEAPI splitscreen_report(int32_t unused, int32_t players, int32_t points, int32_t x, int32_t y)
{
    char buf[0x200];
    int32_t i, slot, row = 200, colour;

    (void)unused;
    menu_begin((const char *)0x4b0134, 0);
    menu_string(buf, 0x200, 0, 2);
    menu_text_add(buf, 0, 1, *(int32_t *)0x4af740, 0xa0, 0, 1, 0x4ac9ec, -1);
    menu_string(buf, 0x200, 0, 3);
    menu_text_add(buf, 0, 1, *(int32_t *)0x4af744, 0xa0, 0, 1, 0x4ac9dc, -1);
    if (points) {
        menu_string(buf, 0x200, 0, 1);
        menu_text_add(buf, 0, 1, *(int32_t *)0x4af748, 0xa0, 0, 1, 0x4ac9cc, -1);
    }
    for (i = 0; i < 6; i++) {
        slot = ((const int8_t *)0x4c24c8)[i];
        colour = (i & 1) + 1;
        if (slot < 0 || slot >= 6)
            continue;
        menu_string(buf, 0x200, (const char *)0x4b0120, i);
        menu_text_add(buf, 0, colour, *(int32_t *)0x4af734, row, 0, 1, 0x4ac99c, -1);
        if (slot < players) {
            car_name(buf, slot);
            menu_text_add(buf, 0, colour, *(int32_t *)0x4af738, row, 0, 1, 0x4ac9bc, -1);
        }
        if (U8(g_race, slot + 0x1c) < 7) {
            car_kind_name(buf, U8(g_race, slot + 0x1c));
            menu_text_add(buf, 0, colour, *(int32_t *)0x4af73c, row, 0, 1, 0x4ac9bc, -1);
        }
        slot_race_time_text(buf, slot);
        menu_text_add(buf, 8, colour, *(int32_t *)0x4af740, row, 0, 1, 0x4ac9ec, -1);
        slot_lap_time_text(buf, slot);
        menu_text_add(buf, 8, colour, *(int32_t *)0x4af744, row, 0, 1, 0x4ac9dc, -1);
        if (points) {
            fe_sprintf(buf, (const char *)0x4afb5c, U16(g_opts, slot * 2 + 0x9c0));
            menu_text_add(buf, 0, colour, *(int32_t *)0x4af748, row, 0, 1, 0x4ac9cc, -1);
        }
        row += 0x1e;
    }
    menu_back_footer(1, x, y, 0x33);
    g_menu_back = 0x448d60;
}

/* 0x44f5a0: the game points screen: position, name (network name when
 * `names` and the player has one), lap and race time per car in finishing
 * order (0x4ca84c); car `highlight` is highlighted. */
void GAMEAPI game_points_menu(int32_t x, int32_t y, int32_t highlight, int32_t names)
{
    char buf[0x200];
    int32_t i, slot, row = 200;
    uint32_t flags, palette;

    menu_begin((const char *)0x4b0170, 0);
    menu_string(buf, 0x200, (const char *)0x4b0170, 1);
    menu_text_add(buf, 0, 1, *(int32_t *)0x4af774, 0xa0, 0, 1, 0x4aca1c, -1);
    menu_string(buf, 0x200, (const char *)0x4b0170, 2);
    menu_text_add(buf, 0, 1, *(int32_t *)0x4af778, 0xa0, 0, 1, 0x4aca1c, -1);
    menu_string(buf, 0x200, (const char *)0x4b0170, 4);
    menu_text_add(buf, 0, 1, *(int32_t *)0x4af780, 0xa0, 0, 1, 0x4aca1c, -1);
    menu_string(buf, 0x200, (const char *)0x4b0170, 3);
    menu_text_add(buf, 0, 1, *(int32_t *)0x4af77c, 0xa0, 0, 1, 0x4aca1c, -1);
    for (i = 0; i < (int32_t)U8(g_race, 0x54d); i++, row += 0x1e) {
        slot = ((const int8_t *)0x4ca84c)[i];
        if (slot < 0 || slot >= 6)
            continue;
        flags = slot == highlight ? 0x80000000u : 0;
        palette = slot == highlight ? 0x4ac9bc : 0;
        menu_string(buf, 0x200, (const char *)0x4b0120, i);
        menu_text_add(buf, flags, 1, *(int32_t *)0x4af774, row, 0, 1, (int32_t)palette, -1);
        if (U8(g_race, slot + 0x1c) < 7) {
            if (names && U8(g_opts, slot + 0x8c) && U8(g_race, slot) != 8)
                player_network_name(buf, slot);
            else
                car_kind_name(buf, U8(g_race, slot + 0x1c));
            menu_text_add(buf, flags, 1, *(int32_t *)0x4af778, row, 0, 1, (int32_t)palette, -1);
        }
        slot_lap_time_text(buf, slot);
        menu_text_add(buf, flags | 8, 1, *(int32_t *)0x4af780, row, 0, 1, (int32_t)palette, -1);
        slot_race_time_text(buf, slot);
        menu_text_add(buf, flags | 8, 1, *(int32_t *)0x4af77c, row, 0, 1, (int32_t)palette, -1);
    }
    menu_back_footer(1, x, y, 0x37);
    g_menu_back = 0x448d60;
}

/* 0x44fdb0: the split-screen players menu (1..4 players, options +4). */
void GAMEAPI players_menu(int32_t x, int32_t y)
{
    int32_t entries[4], i;

    menu_begin((const char *)0x4b01d4, 0);
    for (i = 0; i < 4; i++) {
        g_entry_draw = 0x44fe60;
        g_entry_value = (uint32_t)(i + 1);
        entries[i] = entry_add((const char *)0x4b01c4, i, x, y, 0x33);
    }
    if (U8(g_opts, 4) == 0 || U8(g_opts, 4) > 4) {
        U8(g_opts, 0x76) = 0;
        U8(g_opts, 0x74) = 2;
        U8(g_opts, 4) = 1;
    }
    menu_entry_select(entries[U8(g_opts, 4) - 1]);
}

/* 0x44ffb0 */
int32_t GAMEAPI options_submenu(int32_t a)
{
    return submenu_open(0x450000, a);
}

/* 0x450210: the difficulty option (greyed out until race +0x720 bits 3..4
 * are set). */
int32_t GAMEAPI setup_difficulty_option(int32_t a, int32_t b)
{
    if (!(U8(g_race, 0x720) & 0x18))
        g_entry_next_flag = 1;
    *(int32_t *)0x4af794 = b;
    return option_list((const char *)0x4b0234, 0, (const char *)0x4b0234, 1, 0x450260, a);
}

/* 0x450620 */
int32_t GAMEAPI timewarp_texture_option(void)
{
    return option_cycle((const char *)0x4b02ac, 0, (const char *)0x4b02ac, 1, 0x450640, 1);
}

/* 0x450740 */
int32_t GAMEAPI skidmarks_option(void)
{
    return option_list((const char *)0x4b0324, 0, (const char *)0x4b0324, 1, 0x450760, 1);
}

/* 0x450e30 */
int32_t GAMEAPI weapons_option(void)
{
    return option_cycle((const char *)0x4b0424, 0, (const char *)0x4b0424, 1, 0x450e50, 1);
}

/* 0x450ef0 */
int32_t GAMEAPI mirror_option(void)
{
    return option_cycle((const char *)0x4b0464, 0, (const char *)0x4b0464, 1, 0x450f10, 1);
}

/* 0x450f50 */
int32_t GAMEAPI weather_option(void)
{
    return option_list((const char *)0x4b048c, 0, (const char *)0x4b048c, 1, 0x450f70, 1);
}

/* 0x451900: the league menu (leagues 7..9; the second open with race
 * +0x720 bit 1, the third with bit 2, both in the special cases), the
 * difficulty and options entries, and the league number when one of the
 * leagues is the current entry.
 *
 * As in the original, the second league's entry index takes the place of
 * the "second league open" flag once it is made. */
void GAMEAPI league_menu(int32_t x, int32_t y, int32_t options, int32_t done)
{
    char buf[0x200];
    uint8_t open = U8(g_race, 0x720), *opts;
    int32_t special, second_open, third_open, first, second, third, number = 0;

    special = ((open & 8) && U8(g_race, 0x585) < 1) || ((open & 0x10) && U8(g_race, 0x585) < 2);
    second_open = special || (open & 2);
    third_open = special || (open & 4);
    *(int32_t *)0x4cb594 = 1;
    world_menu_begin();
    menu_begin((const char *)0x4b0548, 0);
    opts = g_opts;
    if (opts[0x40] > 2) {
        opts[0x40] = 0;
        opts = g_opts;
    }
    if (opts[0x40] == 2 && !third_open) {
        opts[0x40] = 1;
        opts = g_opts;
    }
    if (opts[0x40] == 1 && !second_open) {
        opts[0x40] = 0;
        opts = g_opts;
    }
    opts[0x1d] = 0;
    U8(g_opts, 0x74) = 0;
    g_entry_value = 0;
    g_entry_action = 0x451bf0;
    g_entry_next_flag = 0;
    first = entry_add(0, -1, x, y, 0x31);
    g_entry_value = 1;
    g_entry_action = 0x451bf0;
    g_entry_next_flag = !second_open;
    second = entry_add(0, -1, x, y, 0x31);
    g_entry_value = 2;
    g_entry_action = 0x451bf0;
    g_entry_next_flag = !third_open;
    third = entry_add(0, -1, x, y, 0x31);
    switch (U8(g_opts, 0x40)) {
    case 1:
        menu_entry_select(second);
        U8(g_opts, 0x1c) = 8;
        U8(g_opts, 0x1e) = 3;
        break;
    case 2:
        menu_entry_select(third);
        U8(g_opts, 0x1c) = 9;
        U8(g_opts, 0x1e) = 4;
        break;
    default:
        U8(g_opts, 0x40) = 0;
        /* fall through */
    case 0:
        menu_entry_select(first);
        U8(g_opts, 0x1c) = 7;
        U8(g_opts, 0x1e) = 3;
        break;
    }
    *(int32_t *)0x4af664 = -1;
    if (g_selected == first) {
        number = 3;
        *(int32_t *)0x4af664 = 0x2e;
    }
    if (g_selected == second) {
        number = 3;
        *(int32_t *)0x4af664 = 0x2f;
    }
    if (g_selected == third) {
        number = 4;
        *(int32_t *)0x4af664 = 0x30;
    }
    if (options) {
        setup_difficulty_option(0x31, -1);
        options_submenu(0x31);
    }
    *(uint8_t *)0x4c9dfb = done == 0;
    menu_entry_with_text(0, -1, 0, -1, 4, 1, 0x31);
    menu_string(buf, 0x200, 0, -1);
    if (number) {
        int32_t text = menu_text_add(buf, 0, 1, 0x3e, 0x1b8, 0, 0, 0x4aca1c, -1);

        fe_sprintf(buf, (const char *)0x4b0230, number);
        text_append(text, 0, buf);
    }
}

/* --- network players --- */

/* 0x45f570: up to `*count` of the network player records (0x5d5cd8, 0x40
 * each); `*count` is cut to how many there are. */
void GAMEAPI network_players_list(int32_t *count, uint8_t **out)
{
    int32_t n = *count, i;

    if (*(int32_t *)0x5d5cd4 < n)
        n = *(int32_t *)0x5d5cd4;
    *count = n;
    for (i = 0; i < n; i++)
        out[i] = (uint8_t *)0x5d5cd8 + i * 0x40;
}

/* 0x48be20: set up network player slot `slot` (0..5): its name (or
 * "???") and id. */
int32_t GAMEAPI network_player_set(int32_t slot, const char *name, int32_t id)
{
    uint8_t *player;

    if (slot < 0 || slot >= 6)
        return 0;
    if (!name)
        name = (const char *)0x4b11bc;
    player = (uint8_t *)0x5d2cd0 + slot * 0x66c;
    copy_string((char *)player + 8, name);
    I32(player, 4) = id;
    U32(player, 0x3c) = *(uint32_t *)0x5d59a4;
    I32(player, 0x40) = 0;
    I32(player, 0x18) = slot;
    I32(player, 0x24) = 0;
    I32(player, 0x1c) = -1;
    I32(player, 0) = 1;
    return 1;
}
