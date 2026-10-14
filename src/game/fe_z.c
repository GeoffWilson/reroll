/* Front end, group Z (frontend_frame 0x4302d0's menus):
 *
 *  - the menu item builder: item text (0x4481a0), labels (0x448260), the
 *    item commit into the menu table (0x4482a0) and the kinds of item
 *    built on it (choices, toggles, sliders, labels);
 *  - the track menu (series, track choice, locked tracks), the new record
 *    name entry and the record table renames;
 *  - the controls menu and the key / joystick descriptions;
 *  - the audio option menus, the network lobby listing and the network
 *    setting message;
 *  - the menu background's rippling water (0x48d630).
 *
 * The string table reader (0x42e6d0, it reads the text file), sprintf and
 * strncpy (C runtime), text fitting, the text item list (0x448a50) and the
 * drawing stay original. 0x42e2a0 (allocation) is not ported. */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))

#define g_config GP(0x4b2198)
#define g_race GP(0x4b1960)

/* The menu being built: an item template at 0x4c9df8 (0x730 bytes), copied
 * into the menu table (*0x4cb4b8, 0x40 items) by menu_item_commit. */
#define ITEM_TEMPLATE ((uint8_t *)0x4c9df8)
#define ITEM_SIZE 0x730
#define g_menu_items GP(0x4cb4b8)
#define g_menu_count (*(int32_t *)0x4cb49c)
#define g_menu_open (*(uint32_t *)0x4cb498)
#define g_menu_current (*(uint8_t *)0x4cb588)
#define g_text_section G32(0x4cb4b4) /* a const char * */
#define g_text_next (*(int32_t *)0x4cb4a8)

typedef int32_t(GAMEAPI *menu_callback)(int32_t command, char *text);

#define text_entry(dst, size, section, index) GCALL(0x42e6d0, text_line_get)((uint8_t *)(dst), size, section, index)
#define crt_sprintf GCALL(0x4979f0, msvc_sprintf)
#define crt_strncpy GCALL(0x498350, msvc_strncpy)
#define font_find(font) ((const uint8_t *)GMEM(GCALL(0x42e050, font_by_id)(font)))
#define menu_changed GCALL(0x4456e0, menu_extra_row_reset)
#define text_fit GCALL(0x445340, menu_text_fit)
#define menu_text_add(text, flags, a3, x, y, a6, a7, colour, font) \
    GCALL(0x448a50, menu_text_add)(text, flags, a3, x, y, a6, a7, (int32_t)(uintptr_t)(colour), font)
#define driver_name GCALL(0x4484a0, driver_name)
#define lobby_lines GCALL(0x45f5b0, net_chat_lines)
#define profile_name GCALL(0x4053e0, player_name_at)
#define text_box_draw GCALL(0x431780, fe_box_frame_place)
#define menu_dialog GCALL(0x448ba0, menu_common_entry)
#define series_track_count GCALL(0x448670, world_track_count)
#define series_locked_mask GCALL(0x4486b0, world_locked_tracks)
#define series_names GCALL(0x4486f0, world_track_names)
#define menu_select GCALL(0x448b50, menu_entry_select)
#define menu_mode GCALL(0x448be0, menu_common_footer)
#define lap_time_text GCALL(0x405ba0, slot_lap_time_text)
#define joystick_count GCALL(0x48a010, joy_device_count_thunk)
#define binding_get GCALL(0x449bf0, binding_get)
#define chat_show GCALL(0x48d270, net_chat_send)
#define net_send GCALL(0x48cdd0, net_message_send_to)

static uint32_t str_length(const char *s)
{
    uint32_t n = 0;

    while (s[n])
        n++;
    return n;
}

static void str_copy(char *dst, const char *src)
{
    uint32_t i = 0;

    do
        dst[i] = src[i];
    while (src[i++]);
}

static void str_append(char *dst, const char *src)
{
    str_copy(dst + str_length(dst), src);
}

/* 0x405380: does one of the six record entries of `track` belong to
 * profile `slot` (bit 0 used, bits 3..6 the profile)? */
int32_t GAMEAPI track_record_held_by(int32_t track, int32_t slot)
{
    const uint8_t *entry;
    int32_t i;

    if (track < 0 || track >= 0x10 || slot < 0 || slot >= 6)
        return 0;
    entry = g_config + track * 0x54 + 0x451;
    for (i = 0; i < 6; i++, entry += 0xe)
        if ((*entry & 1) && (*entry >> 3 & 0xf) == (uint32_t)slot)
            return 1;
    return 0;
}

/* 0x405860: copy profile `slot`'s 10-byte name into its record entries of
 * `track`. */
void GAMEAPI track_records_rename(int32_t track, int32_t slot)
{
    const uint8_t *name;
    int32_t i, off;

    if (track < 0 || track >= 0x10 || slot < 0 || slot >= 6)
        return;
    name = profile_name(slot);
    if (!name)
        return;
    off = track * 0x54;
    for (i = 0; i < 6; i++, off += 0xe) {
        uint8_t flags = g_config[off + 0x451];

        if ((flags & 1) && (uint32_t)(flags >> 3 & 0xf) == (uint32_t)slot) {
            uint8_t *dst = g_config + off + 0x447;

            U32(dst, 0) = U32(name, 0);
            U32(dst, 4) = U32(name, 4);
            U16(dst, 8) = U16(name, 8);
        }
    }
}

/* 0x429080: store `value` at `address` (top bit ignored), old value out. */
uint32_t *GAMEAPI word_exchange(uint32_t address, uint32_t value, uint32_t *old)
{
    uint32_t *p = (uint32_t *)(uintptr_t)(address & 0x7fffffff);

    *old = *p;
    *p = value;
    return p;
}

/* 0x42e090: a font's width * height / 256, or -1 when there is no such
 * font. */
int32_t GAMEAPI font_scaled_size(int32_t font)
{
    const uint8_t *f = font_find(font);
    int32_t v;

    if (!f)
        return -1;
    v = I32(f, 8) * I32(f, 4);
    return (v + (int32_t)((uint32_t)(v >> 31) & 0xff)) >> 8;
}

/* 0x431670 / 0x4316a0: a front-end texture record's width and height. */
int32_t GAMEAPI frontend_texture_width(int32_t index)
{
    const uint8_t *r = GP(0x4be340) + ((uint32_t)U16(GP(0x4be344), 0x2e) + index) * 0x10;

    return (int32_t)r[4] - (int32_t)r[0] + 1;
}

int32_t GAMEAPI frontend_texture_height(int32_t index)
{
    const uint8_t *r = GP(0x4be340) + ((uint32_t)U16(GP(0x4be344), 0x2e) + index) * 0x10;

    return (int32_t)r[9] - (int32_t)r[1] + 1;
}

/* 0x436b7e: 0x4be494 / 0x4be498, or 0. */
uint32_t GAMEAPI timer_ratio(void)
{
    return G32(0x4be498) ? G32(0x4be494) / G32(0x4be498) : 0;
}

/* 0x43d230: the key code for scan code `code` (0x20000000 | code when it
 * has none). */
uint32_t GAMEAPI key_code_lookup(int32_t code)
{
    uint32_t v;

    if (code < 0 || code > 0xff)
        return 0;
    v = ((uint32_t *)0x6c20c0)[code];
    if (!v)
        return (uint32_t)code | 0x20000000;
    return v;
}

/* 0x446200: the menu's text box, when a menu is up (drawing stays
 * original). */
void GAMEAPI menu_text_box(uint32_t a1, uint32_t unused, uint32_t a3)
{
    (void)unused;
    if (g_menu_count && !G32(0x4cb500))
        text_box_draw(a1, (uint8_t *)GMEM(0x4cac98), (const uint8_t *)GMEM(a3), G32(0x4cb474), G32(0x4cb478), G32(0x4cb4cc), G32(0x4cb4d0));
}

/* 0x4481a0: menu text: entry `index` of text section `section` (with no
 * section, the current one, -1 next, -2 again), or with index -3 the
 * string `section` itself. */
char *GAMEAPI menu_text_get(char *dst, int32_t size, const char *section, int32_t index)
{
    if (!dst || (size != -1 && size <= 0))
        return dst;
    *dst = 0;
    if (index == -3) {
        if (!section)
            return dst;
        if (size == -1) {
            str_copy(dst, section);
            return dst;
        }
        crt_strncpy(dst, section, (uint32_t)size - 1);
        dst[size - 1] = 0;
        return dst;
    }
    if (!section) {
        const char *current = (const char *)GMEM(g_text_section);
        int32_t next = g_text_next;

        if (index == -2)
            index = next - 1;
        if (index == -1)
            index = next;
        g_text_next = index + 1;
        section = current;
        if (!section)
            return dst;
    }
    text_entry(dst, size, section, index);
    return dst;
}

/* 0x448260: an item's 63-character label (+0x50). */
uint8_t *GAMEAPI menu_item_label(uint8_t *item, const char *text)
{
    if (text && *text) {
        memcpy(item + 0x50, text, 0x3f);
        item[0x8f] = 0;
        return item;
    }
    item[0x50] = 0;
    return item;
}

/* 0x4482a0: add the item template to the menu; its index, or -1. */
int32_t GAMEAPI menu_item_commit(uint32_t a1, uint32_t a2, uint32_t a3, uint8_t type)
{
    int32_t n;

    if (!g_menu_open || g_menu_count >= 0x40)
        return -1;
    ITEM_TEMPLATE[0] = type;
    G32(0x4ca2e8) = a2;
    G32(0x4ca2e4) = a1;
    G32(0x4ca2ec) = a3;
    n = g_menu_count;
    memcpy(g_menu_items + n * ITEM_SIZE, ITEM_TEMPLATE, ITEM_SIZE);
    if (G8(0x4c9e00))
        GI32(0x4af5f0) = g_menu_count;
    n = g_menu_count;
    g_menu_count = n + 1;
    menu_changed();
    return n;
}

/* 0x448140: a plain item labelled with text entry (section, index). */
int32_t GAMEAPI menu_add_item(const char *section, int32_t index, uint32_t a3, uint32_t a4, uint32_t a5)
{
    char text[0x200];

    menu_text_get(text, 0x200, section, index);
    menu_item_label((uint8_t *)0x4c9e04, text);
    return menu_item_commit(a3, a4, a5, 1);
}

/* 0x448430: "player n" title. */
void GAMEAPI menu_player_title(char *dst, int32_t player)
{
    char text[0x200];

    *dst = 0;
    if (player < 0 || player >= 6)
        return;
    text_entry(text, 0x200, (const char *)0x4afa24, player);
    str_copy(dst, text);
}

/* 0x448770: the title text kind of a series' track menu. */
int32_t GAMEAPI series_menu_kind(int32_t series)
{
    switch (series) {
    case 1:
        return 0x3b;
    case 2:
        return 0x39;
    case 3:
        return 0x3a;
    case 4:
        return 0x3c;
    case 5:
    case 6:
        return 1;
    default:
        return -1;
    }
}

/* 0x4487c0 / 0x448820: the chosen track of series 1..4 (config +0x44..). */
int32_t GAMEAPI series_track_get(int32_t series)
{
    if (series < 1 || series > 4)
        return -1;
    return g_config[0x43 + series];
}

void GAMEAPI series_track_set(int32_t series, uint8_t track)
{
    if (series >= 1 && series <= 4)
        g_config[0x43 + series] = track;
}

/* 0x448a00: the menu title; also makes `section` the current text
 * section. */
void GAMEAPI menu_set_title(const char *section, int32_t index)
{
    char text[0x200];

    g_text_section = (uint32_t)(uintptr_t)section;
    g_text_next = index + 1;
    menu_text_get(text, 0x200, section, index);
    menu_item_label((uint8_t *)0x4c2920, text);
}

/* 0x448ec0: the name entry's text (with the '_' cursor while editing) into
 * the item template, fitted to `width`. */
void GAMEAPI name_entry_text(const char *text, int32_t width, int32_t editing, int32_t font)
{
    char line[0x200];
    uint32_t count = 0;

    if (G32(0x4cb59c) || G32(0x4cb590))
        G32(0x4c99bc) = 0xffffffff;
    if (font == -1)
        font = GI32(0x4ac848);
    if (!G32(0x4cb598) && !editing) {
        str_copy(line, text);
    } else {
        char *p = line;
        uint32_t i = 0;
        int found = 0;

        G32(0x4c9e48) |= 0x58000000;
        G32(0x4c9f94) = 0x4ac9cc;
        if (str_length(text)) {
            do {
                char c;

                if (i == G32(0x4c99bc)) {
                    *p++ = '_';
                    count = 0;
                    found = 1;
                }
                c = text[i];
                *p++ = c;
                if (c != ' ')
                    count++;
                i++;
            } while (i < str_length(text));
        }
        if (!found) {
            *p++ = '_';
            count = 0;
            G32(0x4c99bc) = str_length(text);
        }
        *p = 0;
    }
    if (width <= 0)
        str_copy((char *)0x4c9e94, line);
    else
        text_fit((char *)0x4c9e94, line, width, 3, font);
    GI32(0x4c9e2c) = font_scaled_size(font);
    G32(0x4c9e48) |= (count & 0xff) << 16;
    G8(0x4c9e1d) = 3;
}

/* 0x449060: the name entry's (0) or second entry's (1) buffer, length and
 * callbacks. */
uint32_t GAMEAPI name_entry_setup(int32_t which, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                  uint32_t a7)
{
    if (!G32(0x4cb528))
        G32(0x4cb528) = G32(0x4cb524);
    if (which == 0) {
        G32(0x4c2914) = 0;
        G32(0x4c98e0) = a2;
        G32(0x4cb468) = a3;
        G32(0x4c2480) = a4;
        G32(0x4cb524) = 0x449120;
        G32(0x4c2918) = a5;
        G32(0x6bf164) = a6;
        G32(0x6bf160) = a7;
        return a7;
    }
    if (which == 1) {
        G32(0x4ca314) = 1;
        G32(0x4ca310) = a2;
        G32(0x4ca318) = a3;
        G32(0x4ca31c) = a4;
        G32(0x4ca2f8) = 0x449860;
        G32(0x4ca320) = a5;
        G32(0x4ca324) = a6;
        G32(0x4ca328) = a7;
        return a7;
    }
    return (uint32_t)which;
}

/* 0x45f5e0: up to *count network players: their records (+0, from
 * 0x5d2cd8, 0x66c apart) and car ids (+0x14); the rest 0 / -1. */
void GAMEAPI network_player_slots(int32_t *count, uint32_t *ids, uint32_t *records)
{
    int32_t n = 0, k;
    uint32_t p;

    for (p = 0x5d2cd8; p < 0x5d5360; p += 0x66c) {
        if (U32(p, -8)) {
            *records++ = p;
            *ids++ = U32(p, 0x14);
            if (++n >= *count)
                break;
        }
    }
    for (k = n; k < *count; k++) {
        *records++ = 0;
        *ids++ = 0xffffffff;
    }
    *count = n;
}

/* 0x45d150: post a line to the network chat, when it is up. */
void GAMEAPI network_chat_post(const char *text)
{
    if (G32(0x4d0774))
        chat_show((char *)text);
}

/* 0x45f430: the "players" line (network message 6 with the count). */
char *GAMEAPI network_players_line(void)
{
    char format[0x80];

    if (text_entry(format, 0x80, (const char *)0x4b0e34, 6) != -1) {
        crt_sprintf((char *)0x4d0778, format, (int32_t) * (int8_t *)0x5d5cc8);
        network_chat_post((const char *)0x4d0778);
    }
    return (char *)0x4d0778;
}

/* 0x44a1c0: one of an item's four 64-character texts. */
void GAMEAPI menu_text_slot_set(int32_t item, uint32_t slot, const char *text)
{
    if (item < 0 || item >= 0x40 || slot > 3)
        return;
    str_copy((char *)(uintptr_t)(0x4c3170 + slot * 0x40 + item * 0x1a0), text);
}

/* 0x449fd0: the network lobby: each player's name and car, the lines of
 * 0x45f5b0 and the players line. */
void GAMEAPI network_lobby_build(void)
{
    int32_t count, lines;
    uint32_t line_text[4], records[6];
    char text[0x100], name[0x40];
    const char *title = network_players_line();
    int32_t width = (GI32(0x4cb474) - GI32(0x4cb47c)) + 0x21c, i, y, item;

    lines = 4;
    count = 6;
    network_player_slots(&count, (uint32_t *)0x4cb110, records);
    for (i = 0, y = 0xa2; i < count; i++, y += 0x32) {
        if (records[i]) {
            text_fit(text, (const char *)GMEM(records[i]), width, 1, GI32(0x4ac84c));
            item = menu_text_add(text, 0, 1, 0x1de, y - 0x14, 0, 0, (const void *)0x4aca1c, GI32(0x4ac84c));
            menu_text_slot_set(item, 0, text);
        }
        if (((int32_t *)0x4cb110)[i] != -1) {
            driver_name(name, ((int32_t *)0x4cb110)[i]);
            text_fit(text, name, width, 1, GI32(0x4ac84c));
            item = menu_text_add(text, 0, 1, 0x1e8, y, 0, 0, (const void *)0x4aca3c, GI32(0x4ac84c));
            menu_text_slot_set(item, 0, text);
        }
    }
    lobby_lines(&lines, line_text);
    for (i = 0, y = 0x1a5; y > 0x172; i++, y -= 0x11) {
        text_fit(text, (const char *)GMEM(line_text[i]), (GI32(0x4cb47c) - GI32(0x4cb474)) - 0x32, 1, GI32(0x4ac84c));
        item = menu_text_add(text, 0, 1, 0x3e, y, 0, 0, (const void *)0x4aca1c, GI32(0x4ac84c));
        menu_text_slot_set(item, 0, text);
    }
    item = menu_text_add(title, 0, 1, 0x3e, 0x1b6, 0, 0, (const void *)0x4aca0c, GI32(0x4ac84c));
    menu_text_slot_set(item, 0, title);
}

/* 0x44c6d0: a choice item: label and value texts, its callback asked for
 * the shown texts (0xc, 0xd) and whether it is enabled (0x11). */
int32_t GAMEAPI menu_add_choice(const char *section1, int32_t index1, const char *section2, int32_t index2,
                                menu_callback callback, uint32_t a6)
{
    char text[0x200];
    int32_t n;
    uint8_t saved;

    menu_text_get(text, 0x200, section1, index1);
    menu_item_label((uint8_t *)0x4c9e04, text);
    menu_text_get(text, 0x200, section2, index2);
    menu_item_label((uint8_t *)0x4c9fa4, text);
    G32(0x4ca308) = (uint32_t)(uintptr_t)callback;
    n = menu_item_commit(0x84, 4, a6, 2);
    if (n == -1 || !callback)
        return n;
    saved = g_menu_current;
    g_menu_current = (uint8_t)n;
    callback(0xc, text);
    memcpy(g_menu_items + n * ITEM_SIZE + 0x9c, text, 0x3f);
    callback(0xd, text);
    memcpy(g_menu_items + n * ITEM_SIZE + 0x23c, text, 0x3f);
    g_menu_items[n * ITEM_SIZE + 7] = callback(0x11, 0) ? 1 : 2;
    g_menu_current = saved;
    return n;
}

/* 0x44c850: a two-value toggle item (value texts at 0x4c9e94 and
 * 0x4ca034). */
int32_t GAMEAPI menu_add_toggle(const char *section, int32_t index, menu_callback callback, uint32_t a4,
                                const char *section_a, int32_t index_a, const char *section_b, int32_t index_b)
{
    char text[0x200];
    int32_t n;
    uint8_t saved;

    menu_text_get(text, 0x200, section, index);
    menu_item_label((uint8_t *)0x4c9e04, text);
    menu_item_label((uint8_t *)0x4c9fa4, text);
    menu_text_get((char *)0x4c9e94, 0x40, section_a, index_a);
    menu_text_get((char *)0x4ca034, 0x40, section_b, index_b);
    G32(0x4ca308) = (uint32_t)(uintptr_t)callback;
    n = menu_item_commit(0x84, 4, a4, 2);
    if (n != -1 && callback) {
        saved = g_menu_current;
        g_menu_current = (uint8_t)n;
        g_menu_items[n * ITEM_SIZE + 7] = callback(0x11, 0) ? 1 : 2;
        g_menu_current = saved;
    }
    return n;
}

/* 0x44c950: an enabled / disabled toggle. */
int32_t GAMEAPI menu_add_on_off(const char *section, int32_t index, menu_callback callback, uint32_t a4)
{
    return menu_add_toggle(section, index, callback, a4, (const char *)0x4b0078, 1, (const char *)0x4b0078, 0);
}

/* 0x44c980: a label item. */
int32_t GAMEAPI menu_add_label(const char *section, int32_t index)
{
    char text[0x200];

    menu_text_get(text, 0x200, section, index);
    menu_item_label((uint8_t *)0x4c9e04, text);
    G8(0x4c9dfe) = 1;
    return menu_item_commit(0x83, 5, 0xffffffff, 5);
}

/* 0x44c9d0: a slider item: its callback gives the shown texts (0x12,
 * 0x13). */
int32_t GAMEAPI menu_add_slider(const char *section1, int32_t index1, const char *section2, int32_t index2,
                                menu_callback callback, uint32_t a6)
{
    char text[0x200];
    int32_t n;
    uint8_t saved;

    menu_text_get(text, 0x200, section1, index1);
    menu_item_label((uint8_t *)0x4c9e04, text);
    menu_text_get(text, 0x200, section2, index2);
    menu_item_label((uint8_t *)0x4c9fa4, text);
    G8(0x4c9dff) = 1;
    G32(0x4ca304) = (uint32_t)(uintptr_t)callback;
    n = menu_item_commit(0x85, 4, a6, 3);
    if (n == -1 || !callback)
        return n;
    saved = g_menu_current;
    g_menu_current = (uint8_t)n;
    callback(0x12, text);
    memcpy(g_menu_items + n * ITEM_SIZE + 0x9c, text, 0x3f);
    callback(0x13, text);
    memcpy(g_menu_items + n * ITEM_SIZE + 0x23c, text, 0x3f);
    g_menu_current = saved;
    return n;
}

/* 0x44e430: open a dialog (kind 1). */
int32_t GAMEAPI menu_open_dialog(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    G8(0x4c9e00) = GI32(0x4af5f0) == -1;
    return menu_dialog(a1, 1, a2, a3, a4);
}

/* 0x44e560: the chosen series (config +0x41, 1..6, else 1) into +0x1c. */
void GAMEAPI series_validate(void)
{
    uint8_t s = g_config[0x41];

    if (s == 0 || s > 6) {
        g_config[0x41] = 1;
        g_config[0x1c] = g_config[0x41];
        return;
    }
    g_config[0x1c] = g_config[0x41];
}

/* 0x44ea70: the series' chosen track, moved off a locked one and into
 * range (100 and 0x65 kept), into config +0x1d. */
void GAMEAPI track_menu_choice_fix(void)
{
    int32_t n, t;
    uint32_t locked;

    series_validate();
    n = series_track_count(g_config[0x41]);
    locked = (uint32_t)series_locked_mask(g_config[0x41]);
    t = series_track_get(g_config[0x41]);
    if (t != 100 && t != 0x65) {
        if (locked & 1u << (t & 31))
            t--;
        if (t < 0 || t > n)
            t = 0;
    }
    series_track_set(g_config[0x41], (uint8_t)t);
    g_config[0x1d] = (uint8_t)t;
}

/* 0x44e8d0: the track menu of the chosen series. */
void GAMEAPI track_menu_build(void)
{
    int32_t n, kind, item, i;
    uint32_t locked;
    const char *names;

    G32(0x4cb4dc) = 0x1e;
    G32(0x4af5fc) = 0;
    G32(0x4cb4e0) = 0x82;
    G32(0x4af600) = 0;
    track_menu_choice_fix();
    n = series_track_count(g_config[0x41]);
    locked = (uint32_t)series_locked_mask(g_config[0x41]);
    kind = series_menu_kind(g_config[0x41]);
    names = (const char *)series_names(g_config[0x41]);
    if (!names)
        return;
    menu_set_title((const char *)0x4afa88, g_race[0x58b] != 0);
    for (i = 0; i < n; i++) {
        G32(0x4ca2f8) = 0x44eb20;
        G8(0x4c9dfd) = (uint8_t)(1u << (i & 31)) & (uint8_t)locked;
        G32(0x4ca2fc) = 0x44eb00;
        G32(0x4ca2f0) = (uint32_t)i;
        item = menu_add_item(names, i, 0x83, 5, (uint32_t)kind);
        if (g_config[0x1d] == (uint32_t)i)
            menu_select(item);
    }
    G32(0x4ca2f8) = 0x44eb20;
    G32(0x4ca2fc) = 0x44eb00;
    G32(0x4ca2f0) = 100;
    item = menu_add_item((const char *)0x4afa88, 2, 0x83, 5, 1);
    if (g_config[0x1d] == 100)
        menu_select(item);
    G32(0x4ca2f8) = 0x44eb20;
    G32(0x4ca2fc) = 0x44eb00;
    G32(0x4ca2f0) = 0x65;
    item = menu_add_item((const char *)0x4afa88, 3, 0x83, 5, 1);
    if (g_config[0x1d] == 0x65)
        menu_select(item);
}

/* 0x44efc0 / 0x44efe0: go to menus 3 and 4. records_menu (0x44ebf0) is
 * void: these return what it leaves in eax, as the original. */
typedef int32_t(GAMEAPI *menu_go_eax_fn)(int32_t world, int32_t place, int32_t x, int32_t y);
#define menu_go ((menu_go_eax_fn)(uintptr_t)0x44ebf0)

int32_t GAMEAPI menu_go_3(uint32_t a, uint32_t b)
{
    return menu_go(3, -1, a, b);
}

int32_t GAMEAPI menu_go_4(uint32_t a, uint32_t b)
{
    return menu_go(4, -1, a, b);
}

/* 0x44f2d0: the new record menu: name entry for profile `slot` on
 * `track`, player title, lap / race time. */
void GAMEAPI new_record_menu(int32_t track, int32_t slot, uint32_t a3, uint32_t a4)
{
    char text[0x200];
    const uint8_t *name;
    int32_t item, held;

    G32(0x4cb524) = 0x448d60;
    menu_set_title((const char *)0x4b015c, 0);
    if (slot < 0 || slot >= 6)
        return;
    G32(0x4cb504) = 1;
    G8(0x4c9dfb) = 1;
    G8(0x4c9e00) = 1;
    menu_mode(0, 1);
    G32(0x4ca854) = 0;
    G32(0x4ca858) = 0;
    *(uint16_t *)0x4ca85c = 0;
    G8(0x4ca85e) = 0;
    name = profile_name(slot);
    if (name) {
        G32(0x4ca854) = U32(name, 0);
        G32(0x4ca858) = U32(name, 4);
        *(uint16_t *)0x4ca85c = U16(name, 8);
    }
    G8(0x4c24c5) = (uint8_t)track;
    G8(0x4c291c) = (uint8_t)slot;
    name_entry_setup(0, 0x4ca854, 0xb, a3, a4, 0x44f510, 0);
    name_entry_text((const char *)0x4ca854, -1, 1, GI32(0x4ac844));
    item = menu_text_add((const char *)0x4afd90, G32(0x4c9e48), 1, GI32(0x4af76c), GI32(0x4af770), 1, 3,
                         (const void *)0x4ac9bc, GI32(0x4ac844));
    if (item != -1) {
        menu_text_slot_set(item, 0, (const char *)0x4c9e94);
        GI32(0x4c3108 + item * 0x1a0) = font_scaled_size(GI32(0x4ac844));
    }
    G8(0x4c9dfc) = 1;
    track_records_rename(track, slot);
    held = track_record_held_by(track, slot);
    menu_player_title(text, slot);
    menu_text_add(text, 0, 1, GI32(0x4af754), GI32(0x4af758), 1, 1, (const void *)0x4aca5c, GI32(0x4ac844));
    menu_text_get(text, 0x200, (const char *)0x4b00f8, 0);
    menu_text_add(text, held ? 0x20000000 : 0, 1, GI32(0x4af75c), GI32(0x4af760), 1, 1, (const void *)0x4aca3c,
                  GI32(0x4ac84c));
    lap_time_text(text, slot);
    menu_text_add(text, (held ? 0x40000000 : 0) | 8, 1, GI32(0x4af764), GI32(0x4af768), 1, 1,
                  (const void *)0x4aca5c, -1);
}

/* 0x44f560: open the new record menu when profile `slot` set one. */
int32_t GAMEAPI new_record_check(int32_t slot, uint32_t a2)
{
    if (!G8(0x4c2908 + slot)) {
        G32(0x4af64c) = a2;
        return 0;
    }
    new_record_menu(g_race[0x579], slot, a2, 1);
    return 1;
}

/* 0x451630 / 0x4516e0: the speaker mode and CD shuffle sliders. */
int32_t GAMEAPI audio_speaker_item(void)
{
    return menu_add_slider((const char *)0x4b04e8, 0, (const char *)0x4b04e8, 0, (menu_callback)0x451650, 0x36);
}

int32_t GAMEAPI audio_shuffle_item(void)
{
    G8(0x4c9dfd) = g_config[0x410];
    G32(0x4ca2f8) = 0x451720;
    return menu_add_slider((const char *)0x4b0504, 0, (const char *)0x4b0504, 0, (menu_callback)0x451750, 0x36);
}

/* 0x451800: the audio auto-detect screen's three lines. */
void GAMEAPI audio_detect_menu(void)
{
    static const int32_t offsets[3] = {-0x28, -10, 0x14};
    static const uint32_t colours[3] = {0, 0, 0x4ac9bc};
    char text[0x200];
    int32_t i;

    menu_set_title((const char *)0x4b0524, 0);
    for (i = 0; i < 3; i++) {
        menu_text_get(text, 0x200, 0, -1);
        menu_text_add(text, 0, 2, GI32(0x4ac988), offsets[i] + GI32(0x4ac98c), 1, 1, GMEM(colours[i]),
                      -1);
    }
}

/* 0x451ec0: the current text entry. */
void GAMEAPI menu_text_next_set(int32_t index)
{
    g_text_next = index;
}

/* 0x451ed0: an activity's name. */
void GAMEAPI activity_name(char *dst, int32_t activity)
{
    text_entry(dst, 0x20, (const char *)0x4b0580, activity);
}

void GAMEAPI input_text(char *dst, int32_t kind, int32_t code);

/* 0x451f10: a binding's description (a missing binding is still read). */
void GAMEAPI binding_text(char *dst, const uint8_t *binding)
{
    if (!binding)
        input_text(dst, -2, -1);
    input_text(dst, ((volatile const int32_t *)binding)[1], ((volatile const int32_t *)binding)[3]);
}

/* 0x451ef0: the description of `player`'s binding for `activity`. */
void GAMEAPI binding_description(char *dst, int32_t player, int32_t activity)
{
    binding_text(dst, binding_get(player, activity));
}

/* 0x452220: "button n". */
void GAMEAPI joystick_button_text(char *dst, int32_t button)
{
    char word[0x100], text[0x100];

    if (!dst)
        return;
    text_entry(word, 0x100, (const char *)0x4b05bc, 6);
    crt_sprintf(text, (const char *)0x4b05d4, word, button);
    text[0x1f] = 0;
    str_copy(dst, text);
}

/* 0x452130: a joystick axis (low 4 bits the axis, kinds 6 / 7 by the high
 * bits). */
void GAMEAPI joystick_axis_text(char *dst, uint32_t code)
{
    int32_t entry, high = (int32_t)code >> 4;

    if (!dst)
        return;
    switch (code & 0xf) {
    case 0:
        entry = 0;
        break;
    case 1:
        entry = 1;
        break;
    case 2:
    case 3:
    case 4:
    case 5:
        entry = (int32_t)(code & 0xf);
        break;
    case 6:
        entry = high == 0 ? 0 : high == 1 ? 2 : 4;
        break;
    case 7:
        entry = high == 0 ? 1 : high == 1 ? 3 : 5;
        break;
    default:
        *dst = 0;
        return;
    }
    text_entry(dst, 0x20, (const char *)0x4b05bc, entry);
}

/* 0x4522a0: a joystick hat direction. */
void GAMEAPI joystick_hat_text(char *dst, int32_t direction)
{
    char text[0x100];
    int32_t entry;

    if (!dst)
        return;
    entry = direction == 0x10 ? 8 : direction == 0x20 ? 10 : direction == 0x30 ? 7 : 9;
    text_entry(text, 0x100, (const char *)0x4b05bc, entry);
    text[0x1f] = 0;
    str_copy(dst, text);
}

/* 0x451fc0: a key's name. */
void GAMEAPI key_text(char *dst, int32_t code)
{
    char key[0x100], text[0x100];
    uint32_t k;

    if (!dst)
        return;
    k = key_code_lookup(code);
    if (!k) {
        key[0] = '?';
        key[1] = '?';
        key[2] = 0;
    } else if (k & 0x20000000) {
        crt_sprintf(key, (const char *)0x4b05b0, k & 0xff);
    } else if (k & 0x800000) {
        text_entry(key, 0x100, (const char *)0x4b059c, (int32_t)(k & 0xff));
    } else {
        crt_sprintf(key, (const char *)0x4b0598, k & 0xff);
    }
    text[0] = 0;
    if (k & 0x80000000) {
        text_entry(text, 0x100, (const char *)0x4b059c, 0);
        str_append(text, (const char *)0x4a8064);
    }
    str_append(text, key);
    text[0x1f] = 0;
    str_copy(dst, text);
}

/* 0x451f40: an input's description by kind (0 joystick button, 1 axis,
 * 2 key, 3 hat, -1 unbound). */
void GAMEAPI input_text(char *dst, int32_t kind, int32_t code)
{
    if (!dst)
        return;
    switch (kind) {
    case 0:
        joystick_button_text(dst, code);
        return;
    case 1:
        joystick_axis_text(dst, (uint32_t)code);
        return;
    case 2:
        key_text(dst, code);
        return;
    case 3:
        joystick_hat_text(dst, code);
        return;
    case -1:
        G32(dst) = G32(0x4b0594);
        return;
    default:
        *dst = 0;
    }
}

/* 0x451c00: the controls menu of `player`: device, steering, buttons and
 * an item per activity with its binding. */
void GAMEAPI controls_menu_build(int32_t player)
{
    char name[0x20], binding[0x20], text[0x200];
    uint8_t device;
    int32_t i, activity;
    const int32_t *entry;

    if (player < 0 || player >= 4)
        return;
    G32(0x4cb7e8) = (uint32_t)player;
    G32(0x4af5f4) = 0x244;
    menu_set_title((const char *)0x4b0568, player);
    menu_text_next_set(5);
    device = g_config[player * 0x90 + 0x1a1];
    if (joystick_count() > 1 && device != 0xff)
        crt_sprintf((char *)0x4c9ed4, (const char *)0x4b0230, device + 1);
    menu_add_slider(0, -1, 0, -2, (menu_callback)0x452760, 5);
    menu_add_choice(0, -1, 0, -1, (menu_callback)0x4523c0, 5);
    menu_add_choice(0, -1, 0, -1, (menu_callback)0x452350, 5);
    menu_add_label((const char *)0x4cb7f8, -3);
    for (i = 0, entry = (const int32_t *)0x4af7a0; i < GI32(0x4af878); i++, entry += 2) {
        activity = *entry;
        binding_description(binding, player, activity);
        activity_name(name, activity);
        menu_text_get((char *)0x4ca194, 0x40, 0, 4);
        str_copy(text, name);
        str_append(text, (const char *)0x4b0564);
        str_copy((char *)0x4ca1d4, text);
        G32(0x4ca188) |= 0x40000000;
        G32(0x4ca14c) = 0xffffff9c;
        G8(0x4ca15c) = 1;
        G32(0x4ca2d4) = 0x4ac9bc;
        str_copy(text, name);
        str_append(text, (const char *)0x4b0558);
        str_append(text, binding);
        if (player && (activity == 0xf || activity == 0x10))
            G8(0x4c9dfb) = 1;
        G32(0x4ca2f0) = (uint32_t)activity;
        G32(0x4ca2f8) = 0x452430;
        menu_add_item(text, -3, 0x83, 5, 5);
    }
}

/* 0x452ff0: no menu change pending. */
int32_t GAMEAPI menu_settled(void)
{
    return G32(0x4cb5a0) == 0;
}

/* 0x45f710: send a setting to the other players (message 0x15); 0 when
 * there is no network game or the send failed. The original leaves bytes
 * 1..7 of the message as stack leftovers; here they are 0. */
int32_t GAMEAPI network_send_setting(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint8_t packet[0x1c];

    if (!(G8(0x5d622c) & 1))
        return 0;
    if (!(G8(0x5d622c) & 2))
        return 0;
    if (G8(0x5d622c) & 0x20) {
        memset(packet, 0, 8);
        packet[0] = 0x15;
        U32(packet, 8) = a1;
        U32(packet, 0xc) = a2;
        U32(packet, 0x10) = a3;
        U32(packet, 0x14) = a4;
        U32(packet, 0x18) = a5;
        if (!net_send(2, 0, (uint32_t)(uintptr_t)packet, 0x1c, 4))
            return 0;
    }
    return 1;
}

/* 0x470e70: more than 16 sound voices in use. */
int32_t GAMEAPI sound_voices_crowded(void)
{
    uint32_t p;
    int32_t n = 0;

    if (!G32(0x4d4524) || !G32(0x4d4528))
        return 0;
    for (p = 0x6cb534; p < 0x6cc434; p += 0x3c)
        if (G32(p))
            n++;
    return n > 0x10;
}

/* 0x470eb0: a sound voice with flag 8. */
int32_t GAMEAPI sound_voice_flag8(void)
{
    uint32_t p;

    if (G32(0x4d4524) && G32(0x4d4528))
        for (p = 0x6cb501; p < 0x6cc401; p += 0x3c)
            if (G8(p) && (G8(p) & 8))
                return 1;
    return 0;
}

/* 0x48b780: up to `max` network sessions: names (+0x18) and ids (+0),
 * from 0x5d26b0, 0x9c apart. */
int32_t GAMEAPI network_sessions_list(int32_t max, int32_t *count, uint32_t *names, uint32_t *ids)
{
    uint32_t p = 0x5d26b0;

    if (max >= GI32(0x5d2cc4))
        max = GI32(0x5d2cc4);
    *count = max;
    for (; max > 0; max--, p += 0x9c) {
        *names++ = p + 0x18;
        *ids++ = G32(p);
    }
    return 1;
}

/* 0x48d630: the menu background's water: each cell {+0 speed, +4 height,
 * +0x24 / +0x28 slopes} pulls on its right and lower neighbours; with
 * `poke`, a drop at the cursor cell (moved to `corner` 0..3). */
void GAMEAPI menu_water_update(int32_t poke, uint32_t corner)
{
    int32_t *base = (int32_t *)GP(0x5d1fe0), *c;
    int32_t cols = (GI32(0x5d1fe4) * 3) / 150, rows = (GI32(0x5d1fe8) * 3) / 150, r, col, x, y;

    if (cols > 0) {
        int32_t *a = base + 10, *b = base + cols * 14 + 10, k;

        for (k = cols; k; k--, a += 14, b += 14) {
            int32_t v = b[-9] - a[-9];

            a[0] = v;
            b[0] = v;
        }
    }
    c = base;
    for (r = 0; r < rows; r++) {
        int below = r < rows - 1;
        int32_t d = c[15] - c[1];

        c[9] = d;
        c[23] = d;
        for (col = 0; col < cols; col++, c += 14) {
            int32_t h = c[1], v = c[0];

            if (col < cols - 1) {
                int32_t e = c[15] - h;

                c[14] -= e >> 2;
                v += e >> 2;
                c[9] = (c[9] + e) >> 1;
                c[23] = e;
            }
            if (below) {
                int32_t e = c[cols * 14 + 1] - h;

                c[cols * 14] -= e >> 2;
                v += e >> 2;
                c[10] = (c[10] + e) >> 1;
                c[cols * 14 + 10] = e;
            }
            if (v > 0x200000)
                v = 0x200000;
            else if (v < -0x200000)
                v = -0x200000;
            h += v >> 2;
            c[1] = h;
            v -= h >> 5;
            v -= v >> 7;
            c[0] = v;
        }
    }
    if (poke && corner <= 3) {
        x = (corner & 1) ? cols - 1 : 0;
        y = (corner & 2) ? rows - 1 : 0;
        GI32(0x5d200c) = x;
        GI32(0x5d2010) = y;
    } else {
        x = GI32(0x5d200c);
        y = GI32(0x5d2010);
    }
    if (x < 0) {
        x = 0;
        GI32(0x5d200c) = 0;
    }
    if (y < 0) {
        y = 0;
        GI32(0x5d2010) = 0;
    }
    if (x > cols - 1) {
        x = cols - 1;
        GI32(0x5d200c) = x;
    }
    if (y > rows - 1) {
        y = rows - 1;
        GI32(0x5d2010) = y;
    }
    if (poke) {
        int32_t i = y * cols + x;

        ((int32_t *)GP(0x5d1fe0))[i * 14 + 1] = 0x500000;
        ((int32_t *)GP(0x5d1fe0))[i * 14] = 0;
    }
}
