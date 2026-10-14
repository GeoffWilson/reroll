/* Front end, group M: the menu page layout (which players' rows show, the
 * cursor and scroll position, the page box), placing menu items' text, the
 * menu set-ups for the driver (car) choice, the time-attack report and the
 * difficulty option, the front end's start-a-mode dispatcher, and the
 * network/serial set-up tables.
 *
 * A menu page (0x4c9df8, 0x51c bytes as cleared) holds rows of 0x730 bytes
 * at *0x4cb4b8 (count 0x4cb49c): +1 shown, +2 free, +4/+5/+6 taken flags,
 * +7 which of the row's three items is used, items at +0xc / +0x1ac /
 * +0x34c (0x1a0 bytes each). An item: +0 text id (-1 none), +5 shown, +6
 * which origin (0x4cb538.. pairs), +8/+0xc position (-1 = given, -100 = the
 * page centre), +0x10/+0x14 offset, +0x18/+0x19 justification (-2 =
 * given), +0x3c/+0x40 extra justification, +0x44 flags (bit 27 white text,
 * bits 28..31 pulsing colours), +0x190 colour, +0x198/+0x19c placed at.
 *
 * Still original (drawing, parked for the new renderer): the text-string
 * objects 0x42ea00..0x42f2b0 (glyph quads at *0x4be304, colours at
 * *0x4be308), the colour pulse/fade helpers 0x432820 / 0x4328a0 /
 * 0x4328c0, the menu fire effect 0x457bf0 / 0x457d10 / 0x457d50 /
 * 0x457e90 and the DirectDraw texture 0x420000; and the allocator 0x476b80.
 * Text building (0x42e6d0, 0x4481a0, 0x448a50, sprintf 0x4979f0) and menu
 * building (0x448a00, 0x448140, 0x448b50, 0x448af0, 0x44c9d0, 0x44ffd0)
 * are called by address. */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define S8(p, off) (*(int8_t *)((p) + (off)))
#define I32(p, off) (*(int32_t *)((p) + (off)))

#define g_options GP(0x4b2198)
#define g_race GP(0x4b1960)

/* The menu page. */
#define g_rows GP(0x4cb4b8)
#define g_row_count (*(int32_t *)0x4cb49c)
#define g_cursor (*(int8_t *)0x4cb588)
#define g_first_row (*(int8_t *)0x4cb589)
#define ROW_SIZE 0x730
#define ROW(i) (g_rows + (i) * ROW_SIZE)
#define g_origins ((int32_t *)0x4cb538) /* three x, y pairs */
#define g_box_x0 (*(int32_t *)0x4cb474)
#define g_box_y0 (*(int32_t *)0x4cb478)
#define g_box_x1 (*(int32_t *)0x4cb47c)
#define g_box_y1 (*(int32_t *)0x4cb480)
#define g_box_cx (*(int32_t *)0x4cb484)
#define g_box_cy (*(int32_t *)0x4cb488)
#define g_box_w (*(int32_t *)0x4cb4cc)
#define g_box_h (*(int32_t *)0x4cb4d0)
#define g_row_w (*(int32_t *)0x4cb4bc)
#define g_row_h (*(int32_t *)0x4cb4c0)
#define g_horizontal (*(int32_t *)0x4cb4fc)

/* The menu items' option callback (fe_z.c). */
typedef int32_t(GAMEAPI *menu_callback)(int32_t command, char *text);
int32_t GAMEAPI menu_add_slider(const char *section1, int32_t index1, const char *section2, int32_t index2,
                                menu_callback callback, uint32_t a6); /* 0x44c9d0 */

/* Drawing (text strings), still original. */
#define string_hide GCALL(0x42eab0, string_off)
#define string_show GCALL(0x42ea40, string_on)
#define string_bounds GCALL(0x42ed10, string_limits_all)
#define string_place GCALL(0x42ed40, string_set_position)
#define string_justify GCALL(0x42eed0, string_set_position_j)
#define string_colours GCALL(0x42f0f0, string_colour_range)
#define string_colour GCALL(0x42f2b0, string_colour_set)
#define colour_pulse GCALL(0x4328c0, colour_pulse_scale)

/* Text and menu building, still original. */
#define time_split GCALL(0x405480, time_entry_fill)
#define time_text GCALL(0x405a80, bcd_time_text)
#define time_best_text GCALL(0x405ba0, slot_lap_time_text)
#define text_lookup GCALL(0x42e6d0, text_line_get)
#define c_sprintf GCALL(0x4979f0, msvc_sprintf)
#if defined(__i386__)
/* the original pushes a fourth argument (a name table) the callee never reads */
#define mode_start(m, k, c, t) ((int32_t(GAMEAPI *)(int32_t, uint32_t, uint32_t, uint32_t))(uintptr_t)0x432cd0)(m, k, c, t)
#else
#define mode_start(m, k, c, t) GCALL(0x432cd0, front_mode_race_setup)(m, k, c)
#endif
#define player_count_text GCALL(0x430ae0, rand_next)
#define menu_open GCALL(0x447650, fe_menu_action)
#define menu_add GCALL(0x448140, menu_add_item)
#define menu_text GCALL(0x4481a0, menu_text_get)
#define car_name_text GCALL(0x4484a0, driver_name)
#define world_name GCALL(0x4486f0, world_track_names)
#define menu_title GCALL(0x448a00, menu_set_title)
#define menu_label GCALL(0x448a50, menu_text_add)
#define menu_buttons GCALL(0x448af0, menu_back_footer)
#define menu_select GCALL(0x448b50, menu_entry_select)
#define menu_option GCALL(0x44c9d0, menu_add_slider)
#define menu_list GCALL(0x44ffd0, laps_menu)

/* Network, still original. */
#define net_close GCALL(0x48a6b0, dplay_close)
#define net_open GCALL(0x48ace0, dplay_connection_init)
#define net_start GCALL(0x48bc40, dplay_thread_start)
#define heap_take GCALL(0x476b80, arena_alloc_low)
#define mem_fill GCALL(0x476f50, mem_fill)

/* 0x405590: a time split into its display parts (0x405480 with the
 * "hundredths" flag set). */
int32_t GAMEAPI time_parts(uint8_t *out, int32_t a, int32_t b)
{
#if defined(__i386__)
    /* 0x405480 is void: the original returns what it leaves in eax */
    return ((int32_t(GAMEAPI *)(uint8_t *, int32_t, int32_t, int32_t))(uintptr_t)0x405480)(out, a, b, 1);
#else
    time_split(out, a, b, 1);
    return 0;
#endif
}

/* 0x405bc0: a time's text. */
int32_t GAMEAPI time_parts_text(char *out, int32_t a, int32_t b)
{
    uint8_t parts[16];

    time_parts(parts, a, b);
#if defined(__i386__)
    /* 0x405a80 is void: the original returns what it leaves in eax */
    return ((int32_t(GAMEAPI *)(char *, const uint8_t *))(uintptr_t)0x405a80)(out, parts);
#else
    time_text(out, parts);
    return 0;
#endif
}

/* 0x4329b0: start a front-end choice: `mode` 0 counts a demo run; 1..10
 * start that mode (0x432cd0 with its table and the chosen car/track, from
 * options +0x1c / +0x1d). Returns 1 (and fades out to the title) if the
 * mode cannot start. */
int32_t GAMEAPI front_mode_start(uint32_t mode)
{
    uint8_t *options = g_options;
    int32_t result;

    if (mode == 0) {
        options[0x3b]++;
    } else {
        options[0x3b] = 0;
        if (mode > 10)
            goto fail;
    }
    switch (mode) {
    case 0:
        result = mode_start(0, 10, player_count_text(4), 0x4acb88);
        break;
    case 1:
        result = mode_start(1, 0, 0, 0x4acb7c);
        break;
    case 2:
        result = mode_start(2, g_options[0x1c], g_options[0x1d], 0x4acb70);
        break;
    case 3:
        result = mode_start(3, g_options[0x1c], g_options[0x1d], 0x4acb64);
        break;
    case 4:
        result = mode_start(4, g_options[0x1c], g_options[0x1d], 0x4acb50);
        break;
    case 5:
        if (*(int32_t *)0x4cb48c)
            result = mode_start(5, 0xc, g_options[0x1d], 0x4acb30);
        else
            result = mode_start(5, g_options[0x1c], g_options[0x1d], 0x4acb30);
        break;
    case 6:
        result = mode_start(6, 0xb, 0x13, 0x4acb20);
        break;
    case 7:
        if (*(int32_t *)0x4cb48c)
            result = mode_start(7, 0xc, g_options[0x1d], 0x4acae0);
        else
            result = mode_start(7, g_options[0x1c], g_options[0x1d], 0x4acae0);
        break;
    case 8:
        result = mode_start(8, 0xd, 0, 0x4acacc);
        break;
    case 9:
        result = mode_start(9, 2, 1, 0x4acb0c);
        break;
    default:
        result = mode_start(10, 2, 1, 0x4acaf4);
        break;
    }
    if (result) {
        *(int32_t *)0x4af650 = 4;
        return 0;
    }
fail:
    *(int32_t *)0x4af64c = 4;
    *(int32_t *)0x4af650 = -1;
    *(int32_t *)0x4cb58c = 1;
    *(int32_t *)0x4be330 = 1;
    *(int32_t *)0x4ac990 = 0x100;
    return 1;
}

/* 0x445650: an item cleared: shown, origin `origin`, no text. Returns the
 * item. */
uint8_t *GAMEAPI menu_item_reset(uint8_t *item, int32_t shown, int32_t origin)
{
    if (item) {
        U8(item, 4) = (uint8_t)shown;
        U8(item, 6) = (uint8_t)origin;
        I32(item, 0x44) = 0;
        I32(item, 0) = -1;
        U8(item, 5) = 1;
        I32(item, 8) = -1;
        I32(item, 0xc) = -1;
        I32(item, 0x10) = 0;
        I32(item, 0x14) = 0;
        U8(item, 0x18) = 0xfe;
        U8(item, 0x19) = 0xfe;
        I32(item, 0x2c) = -1;
        I32(item, 0x30) = -1;
        I32(item, 0x3c) = -1;
        I32(item, 0x40) = -1;
        I32(item, 0x48) = -1;
        I32(item, 0x194) = *(int32_t *)0x4ac848;
        I32(item, 0x190) = 0;
        I32(item, 0x24) = 0;
        I32(item, 0x28) = 0;
        I32(item, 0x34) = 0;
        I32(item, 0x38) = 0;
        U8(item, 0x50) = 0;
        U8(item, 0x90) = 0;
        U8(item, 0xd0) = 0;
        U8(item, 0x110) = 0;
        U8(item, 0x150) = 0;
        I32(item, 0x4c) = 0;
    }
    return item;
}

/* 0x4455c0: a menu row cleared, with its three items. Returns the third
 * item (what the last reset left in eax). */
uint8_t *GAMEAPI menu_row_reset(uint8_t *row)
{
    int32_t i;

    if (!row)
        return 0;
    for (i = 0; i < 0x1cc; i++)
        ((uint32_t *)row)[i] = 0;
    I32(row, 0x4f4) = -1;
    U8(row, 0) = 0;
    I32(row, 0x500) = 0;
    I32(row, 0x504) = 0;
    I32(row, 0x508) = 0;
    I32(row, 0x50c) = 0;
    I32(row, 0x510) = 0;
    I32(row, 0x514) = 0;
    menu_item_reset(row + 0xc, 1, 1);
    menu_item_reset(row + 0x1ac, 1, 1);
    menu_item_reset(row + 0x34c, 1, 1);
    I32(row, 0x51c) = -1;
    U8(row, 7) = 1;
    return row + 0x34c;
}

/* 0x4456e0: the extra row (0x4c9df8) cleared; 0x4cb598 says whether the
 * cursor is on it (one past the rows), 0x4cb59c also needs 0x4cb590. */
int32_t GAMEAPI menu_extra_row_reset(void)
{
    int32_t on;

    menu_row_reset((uint8_t *)0x4c9df8);
    on = g_cursor == g_row_count;
    *(int32_t *)0x4cb598 = on;
    if (on) {
        *(int32_t *)0x4cb59c = 1;
        if (*(int32_t *)0x4cb590)
            return *(int32_t *)0x4cb590;
    }
    *(int32_t *)0x4cb59c = 0;
    return 0;
}

/* 0x445f50: place an item's text: its position (or the given x, y; -100 =
 * the page centre) plus its offset and origin, pulsing colours, the
 * justification (-2 = the given `hjust` / `vjust`). Returns the text id,
 * or -1 for no item. */
int32_t GAMEAPI menu_item_place(uint8_t *item, int32_t x, int32_t y, int32_t hjust, int32_t vjust)
{
    int32_t id, px, py, slot, flags, colour, h, v;
    uint8_t buf[16];

    if (!item || I32(item, 0) == -1)
        return -1;
    id = I32(item, 0);
    if (!U8(item, 5)) {
        string_hide(id);
        return id;
    }
    px = I32(item, 8);
    if (px == -1)
        px = x;
    if (px == -100)
        px = g_box_cx;
    py = I32(item, 0xc);
    if (py == -1)
        py = y;
    if (py == -100)
        py = g_box_cy;
    px += I32(item, 0x10);
    py += I32(item, 0x14);
    switch (S8(item, 6)) {
    case 0:
        px += g_origins[0];
        py += g_origins[1];
        break;
    case 1:
        px += g_origins[2];
        py += g_origins[3];
        break;
    case 2:
        px += g_origins[4];
        py += g_origins[5];
        break;
    }
    flags = I32(item, 0x44);
    if (flags & 0x8000000)
        colour = 0x4ac99c;
    else
        colour = I32(item, 0x190);
    if (!colour)
        colour = 0x4aca0c;
    slot = -1;
    if (flags & 0x10000000)
        slot = -2 - (int32_t)((uint32_t)flags >> 16 & 0xff);
    if ((uint32_t)flags & 0x80000000) {
        colour_pulse(buf, (const uint8_t *)GMEM(colour), 4, 1);
        string_colours(id, -1, slot, -1, buf);
    }
    if (I32(item, 0x44) & 0x20000000) {
        colour_pulse(buf, (const uint8_t *)GMEM(colour), 4, 4);
        string_colours(id, -1, slot, -1, buf);
    }
    if (I32(item, 0x44) & 0x40000000) {
        colour_pulse(buf, (const uint8_t *)GMEM(colour), 4, 2);
        string_colours(id, -1, slot, -1, buf);
    }
    h = S8(item, 0x18);
    if (h == -2)
        h = hjust;
    v = S8(item, 0x19);
    if (v == -2)
        v = vjust;
    I32(item, 0x198) = px;
    I32(item, 0x19c) = py;
    string_place(id, px, py, h, v);
    if (I32(item, 0x40) != -1)
        string_justify(id, I32(item, 0x3c), I32(item, 0x40));
    string_show(id, h == 3 ? px : 0, v == 3 ? py : 0, 0, 0);
    return id;
}

/* 0x446180: the 64 loose items (0x4c30e0, 0x1a0 each) placed at the
 * default spot (0x4ac988, 0x4ac98c). Returns the last one's id. */
int32_t GAMEAPI menu_loose_items_place(void)
{
    uint8_t *item;
    int32_t id = 0;

    for (item = (uint8_t *)0x4c30e0; item < (uint8_t *)0x4c98e0; item += 0x1a0)
        id = menu_item_place(item, *(int32_t *)0x4ac988, *(int32_t *)0x4ac98c, 1, 1);
    return id;
}

/* 0x4461b0: the page title (0x4c2920) placed. */
int32_t GAMEAPI menu_title_place(void)
{
    return menu_item_place((uint8_t *)0x4c2920, 0xf, 0x28, 0, 0);
}

/* 0x445790: lay a menu page out. The origins (three x, y pairs) are kept;
 * rows taken by nobody are free, the cursor moves off taken rows, the
 * shown window (at most 11 rows, 4 across, or 0x4af604) scrolls to keep
 * the cursor in it, the scroll arrows (0x4cb4ec / 0x4cb4f0), the box size
 * and place (justified by 0x4af5fc / 0x4af600, at most 600 wide), then
 * each shown row's item is placed (0x445f50) and the cursor's row
 * remembered (0x4cb50c..0x4cb520). */
int32_t GAMEAPI menu_layout(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int32_t i, last_free, first_free, first_open, partial, count, shown, most;
    int32_t first_shown, last_shown, first_open2, last_open, w, h, bx, by;
    int32_t across, item_x, item_y, step_x, step_y, row_x, j;
    uint8_t *row;
    int8_t first;

    g_origins[0] = x0;
    g_origins[1] = y0;
    g_origins[2] = x1;
    g_origins[3] = y1;
    g_origins[4] = x2;
    g_origins[5] = y2;
    menu_title_place();
    *(int32_t *)0x4cb50c = 0;
    partial = 0; /* sic: only ever replaced when -1 */
    first_free = -1;
    last_free = -1;
    first_open = -1;
    for (i = 0; i < g_row_count; i++) {
        string_hide(I32(ROW(i), 0xc));
        string_hide(I32(ROW(i), 0x1ac));
        string_hide(I32(ROW(i), 0x34c));
        U8(ROW(i), 1) = 0;
        U8(ROW(i), 2) = 0;
        row = ROW(i);
        if (g_cursor == i && (U8(row, 4) || U8(row, 5) || U8(row, 6))) {
            if (last_free == -1)
                g_cursor++;
            else
                g_cursor = (int8_t)last_free;
        }
        if (!U8(row, 4)) {
            if (first_open == -1)
                first_open = i;
            if (!U8(row, 5) && !U8(row, 6)) {
                if (first_free == -1)
                    first_free = i;
                U8(row, 2) = 1;
                last_free = i;
            } else if (partial == -1) {
                partial = i;
            }
        }
    }
    count = g_row_count;
    *(int32_t *)0x4cb500 = *(int32_t *)0x4cb504;
    first = g_first_row;
    if (g_cursor < 0 || g_cursor >= count) {
        g_cursor = (int8_t)first_free;
        if ((int8_t)first_free == -1) {
            g_cursor = (int8_t)partial;
            if ((int8_t)partial == (int8_t)first_free) {
                *(int32_t *)0x4cb500 = 1;
                g_first_row = -1;
                *(int32_t *)0x4af660 = -1;
                goto shown;
            }
        }
    }
    if (g_cursor == -1) {
        *(int32_t *)0x4af660 = -1;
    } else {
        first = g_first_row;
        *(int32_t *)0x4af660 = I32(ROW(g_cursor), 0x4f4);
    }
shown:
    most = g_horizontal ? 4 : 11;
    if (*(int32_t *)0x4af604 != -1)
        most = *(int32_t *)0x4af604;
    shown = 0;
    if (g_cursor != -1) {
        if (first < 0) {
            first = (int8_t)first_open;
            g_first_row = first;
        }
        if (g_cursor < first) {
            first = g_cursor;
            g_first_row = first;
        }
        while (U8(ROW(first), 4)) {
            first++;
            g_first_row = first;
        }
        for (j = first; j < count; j++) {
            if (U8(ROW(j), 4))
                continue;
            shown++;
            U8(ROW(j), 1) = 1;
            if (shown > most) {
                shown--;
                U8(ROW(g_first_row), 1) = 0;
                first = (int8_t)(g_first_row + 1);
                g_first_row = first;
                while (U8(ROW(first), 4)) {
                    first++;
                    g_first_row = first;
                }
            }
            if (shown >= most && U8(ROW(g_cursor), 1)) {
                count = g_row_count;
                break;
            }
            count = g_row_count;
        }
    }

    first_shown = -1;
    last_shown = -1;
    first_open2 = -1;
    last_open = -1;
    for (i = 0; i < count; i++) {
        row = ROW(i);
        if (!U8(row, 4)) {
            if (first_open2 == -1)
                first_open2 = i;
            last_open = i;
        }
        if (U8(row, 1)) {
            if (first_shown == -1)
                first_shown = i;
            last_shown = i;
        }
    }
    if (count > 0 && first_open2 < first_shown)
        *(int32_t *)0x4cb4ec = 1;
    else {
        *(int32_t *)0x4cb4ec = 0;
        if (*(int32_t *)0x4cb4f4)
            *(int32_t *)0x4cb4ec = 1;
    }
    if (last_open > last_shown)
        *(int32_t *)0x4cb4f0 = 1;
    else {
        *(int32_t *)0x4cb4f0 = 0;
        if (*(int32_t *)0x4cb4f8)
            *(int32_t *)0x4cb4f0 = 1;
    }

    if (g_horizontal) {
        w = g_row_w * shown + 0x3c;
        g_box_w = w;
        if (shown > 1) {
            w = w + 4 * (3 * shown) - 0xc;
            g_box_w = w;
        }
        h = g_row_h + 0x10;
        g_box_h = h;
    } else {
        h = g_row_h * shown + 0x10;
        w = g_row_w + 0x3c;
        g_box_w = w;
        g_box_h = h;
        if (shown > 1) {
            h = h + 4 * shown - 4;
            g_box_h = h;
        }
    }
    *(int32_t *)0x4cb4a4 = 1;
    for (i = 0; i < count; i++) {
        if (U8(ROW(i), 2) && !U8(ROW(i), 1)) {
            *(int32_t *)0x4cb4a4 = 0;
            break;
        }
    }
    if (*(int32_t *)0x4af608 != -1)
        *(int32_t *)0x4cb4a4 = *(int32_t *)0x4af608;
    if (*(int32_t *)0x4af5f4 != -1)
        w = *(int32_t *)0x4af5f4;
    if (*(int32_t *)0x4af5f8 != -1)
        h = *(int32_t *)0x4af5f8;
    w += *(int32_t *)0x4cb4d4;
    h += *(int32_t *)0x4cb4d8;
    g_box_w = w;
    g_box_h = h;
    if (w > 600) {
        w = 600;
        g_box_w = w;
    }
    switch (*(int32_t *)0x4af5fc) {
    case 0:
        bx = *(int32_t *)0x4cb4dc;
        break;
    case 2:
        bx = *(int32_t *)0x4cb4dc - w + 1;
        break;
    default:
        bx = (*(int32_t *)0x4cb4dc * 2 - w) / 2;
        break;
    }
    switch (*(int32_t *)0x4af600) {
    case 0:
        by = *(int32_t *)0x4cb4e0;
        break;
    case 2:
        by = *(int32_t *)0x4cb4e0 - h + 1;
        break;
    default:
        by = (*(int32_t *)0x4cb4e0 * 2 - h) / 2;
        break;
    }
    bx += *(int32_t *)0x4cb4e4 + x1;
    g_box_x0 = bx;
    w = bx + w - 1;
    g_box_x1 = w;
    by += *(int32_t *)0x4cb4e8 + y1;
    g_box_y0 = by;
    g_box_cx = (w + bx) / 2;
    h = by + h - 1;
    g_box_y1 = h;
    g_box_cy = (by + h) / 2;

    if (g_horizontal) {
        across = 1;
        item_y = by + 8;
        step_y = 0;
        item_x = g_row_w / 2 + bx + 0x1e;
        row_x = bx - g_row_w;
        step_x = g_row_w + 0xc;
    } else {
        item_x = bx + 0x1e;
        across = 0;
        item_y = by + 8;
        row_x = w - 0x1e;
        step_x = 0;
        step_y = g_row_h + 4;
    }
    item_x -= x1;
    item_y -= y1;
    for (i = 0; i < g_row_count; i++) {
        uint8_t *item;
        int32_t id;

        I32(ROW(i), 0x48) = -1;
        I32(ROW(i), 0x1e8) = -1;
        I32(ROW(i), 0x388) = -1;
        I32(ROW(i), 0x4c) = -1;
        I32(ROW(i), 0x1ec) = -1;
        I32(ROW(i), 0x38c) = -1;
        I32(ROW(i), 0x54c) = row_x;
        I32(ROW(i), 0x550) = g_row_h / 2 + item_y;
        row = ROW(i);
        if (!U8(row, 1))
            continue;
        if (U8(row, 7) == 2)
            item = row + 0x1ac;
        else if (U8(row, 7) == 3)
            item = row + 0x34c;
        else
            item = row + 0xc;
        I32(item, 0x3c) = row_x;
        I32(item, 0x40) = 2;
        id = menu_item_place(item, item_x, item_y, across, 0);
        if (!I32(item, 0x190))
            string_colour(id, -1, (const uint8_t *)GMEM(U8(ROW(i), 5) ? 0x4aca2c : 0x4aca0c));
        if (i == g_cursor) {
            *(int32_t *)0x4cb50c = 1;
            *(int32_t *)0x4cb510 = g_row_h / 2 + item_y;
            string_bounds(id, (int32_t *)0x4cb514, (int32_t *)0x4cb518, (int32_t *)0x4cb51c, (int32_t *)0x4cb520);
        }
        row_x += step_x;
        item_x += step_x;
        item_y += step_y;
    }
    return menu_loose_items_place();
}

/* 0x447ed0: back to the main menu (page 10). */
int32_t GAMEAPI menu_back_to_main(void)
{
    *(int32_t *)0x4cb4ac = 0;
    return menu_open(10);
}

/* 0x4483f0: "LAP n" for the time-attack report (n = lap + 1). */
int32_t GAMEAPI lap_title_text(char *out, int32_t lap)
{
    char format[0x200];

    text_lookup((uint8_t *)format, 0x200, (const char *)0x4afa10, 0);
    return c_sprintf(out, format, lap + 1);
}

/* 0x448730: a world's number from its menu entry (1..4 -> 0..3). */
int32_t GAMEAPI world_number(int32_t world)
{
    switch (world) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 3:
        return 2;
    case 4:
        return 3;
    default:
        return -1;
    }
}

/* 0x4485b0: a track choice's title: worlds 5 and 6 are fixed tracks, 100
 * is "random", 101 "in order", else the world's own track name. `out`
 * gets "" if there is none. Returns what the copy leaves in eax. */
int32_t GAMEAPI track_title_text(char *out, int32_t world, int32_t track)
{
    char text[0x200];
    uint32_t name;
    int32_t n, i;

    if (world == 5 || world == 6) {
        track = (world != 5) + 2;
        name = 0x4afa88;
    } else if (track == 100) {
        name = 0x4afab4;
        track = world_number(world);
    } else if (track == 0x65) {
        name = 0x4afa9c;
        track = world_number(world);
    } else {
        name = (uint32_t)(uintptr_t)world_name(world);
    }
    out[0] = 0;
    if (!name || track == -1)
        return track;
    text_lookup((uint8_t *)text, 0x200, (const char *)GMEM(name), track);
    for (n = 0; text[n]; n++)
        ;
    n++;
    for (i = 0; i < n; i++)
        out[i] = text[i];
    return n;
}

/* 0x4488c0: the time-attack report page: best lap, then five laps. */
int32_t GAMEAPI time_attack_report(int32_t unused, int32_t times, int32_t a, int32_t b)
{
    char text[0x200];
    int32_t y = 200, lap, colour;

    (void)unused;
    menu_title((const char *)0x4afb28, 0);
    menu_text(text, 0x200, 0, -1);
    menu_label(text, 0, 2, *(int32_t *)0x4af72c, 0xa0, 0, 1, 0x4ac9cc, -1);
    time_best_text(text, times);
    menu_label(text, 8, 2, *(int32_t *)0x4af730, 0xa0, 0, 1, 0x4ac99c, -1);
    for (lap = 0; lap < 5; lap++) {
        colour = (lap & 1) + 1;
        lap_title_text(text, lap);
        menu_label(text, 0, colour, *(int32_t *)0x4af72c, y, 0, 1, 0x4ac9ec, -1);
        time_parts_text(text, times, lap);
        menu_label(text, 8, colour, *(int32_t *)0x4af730, y, 0, 1, 0x4ac99c, -1);
        y += 0x1e;
    }
    colour = menu_buttons(1, a, b, 0x37);
    *(uint32_t *)0x4cb524 = 0x448d60;
    return colour;
}

/* 0x44ce50: fade the front end in. */
void GAMEAPI front_fade_in(void)
{
    *(int32_t *)0x4be330 = 1;
    *(int32_t *)0x4ac990 = 0x100;
}

/* 0x44fcd0: the driver menu's choice: player 0x4af790's car (options +0x18
 * + player) set to `car` (0..7). */
int32_t GAMEAPI driver_choose(int32_t unused, int32_t car)
{
    (void)unused;
    *(int32_t *)0x4af654 = 0;
    if (car >= 0 && car <= 7 && (*(uint32_t *)0x4af790 & 0xff) <= 3)
        g_options[0x18 + (*(uint32_t *)0x4af790 & 0xff)] = (uint8_t)car;
    return 1;
}

/* 0x44fae0: the driver (car) menu for player `player`: the seven cars of
 * the table 0x4af658 (car 6 only once unlocked, race +0x720 bit 4), the
 * player's current one selected (an out-of-range one is reset to the
 * first), and four statistics labels. Returns the last label's result. */
int32_t GAMEAPI driver_menu(uint32_t player, int32_t a, int32_t b)
{
    char text[0x200];
    int32_t car, i, id, y, result = 0;
    const int32_t *slot;

    *(uint32_t *)0x4cb524 = 0x453000;
    *(int32_t *)0x4af654 = 0;
    if (player > 3)
        return 0;
    car = g_options[0x18 + player];
    *(uint8_t *)0x4af790 = (uint8_t)player;
    if (car < 0 || car > 6)
        driver_choose(0, *(int32_t *)0x4af658 & 0xff);
    menu_title((const char *)0x4b01b4, (int32_t)player);
    *(int32_t *)0x4cb4e0 = 0x19a;
    *(int32_t *)0x4af600 = 0;
    for (i = 0, slot = (const int32_t *)0x4af614; slot < (const int32_t *)0x4af630; slot++, i++) {
        int32_t choice = *(uint8_t *)(uintptr_t)(0x4af658 + i);

        if (choice == 6 && !(g_race[0x720] & 0x10))
            *(uint8_t *)0x4c9dfc = 1;
        *(uint32_t *)0x4ca2fc = 0x44fcd0;
        *(uint32_t *)0x4ca2f8 = 0x453000;
        *(int32_t *)0x4ca2f0 = choice;
        *(int32_t *)0x4c9e0c = -100;
        *(uint8_t *)0x4c9e1c = 1;
        car_name_text(text, choice);
        id = menu_add(text, -3, a, b, *slot);
        if (choice == car)
            menu_select(id);
    }
    *(int32_t *)0x4af604 = 1;
    *(int32_t *)0x4af608 = 1;
    *(int32_t *)0x4cb4f4 = 1;
    *(int32_t *)0x4cb4f8 = 1;
    *(int32_t *)0x4cb508 = 1;
    for (i = 0, y = 0x14b; i < 4; i++, y += 0x20) {
        menu_text(text, 0x200, (const char *)0x4b019c, i);
        result = menu_label(text, 0, 1, 0x23, y, 0, 1, 0x4aca3c, *(int32_t *)0x4ac84c);
    }
    return result;
}

/* 0x44fac0, 0x44fd50, 0x44fd70, 0x44fd90: the driver menu for players 1..4. */
int32_t GAMEAPI driver_menu_1(int32_t a, int32_t b)
{
    return driver_menu(0, a, b);
}

int32_t GAMEAPI driver_menu_2(int32_t a, int32_t b)
{
    return driver_menu(1, a, b);
}

int32_t GAMEAPI driver_menu_3(int32_t a, int32_t b)
{
    return driver_menu(2, a, b);
}

int32_t GAMEAPI driver_menu_4(int32_t a, int32_t b)
{
    return driver_menu(3, a, b);
}

/* 0x450150: the list menu whose entries come from 0x450160 (0x33). */
int32_t GAMEAPI list_menu_open(void)
{
    return menu_list(0x450160, 0x33);
}

/* 0x450460: the difficulty option. */
int32_t GAMEAPI difficulty_option(int32_t a)
{
    return menu_option((const char *)0x4b0234, 0, (const char *)0x4b0234, 1, (menu_callback)(uintptr_t)0x450490, (uint32_t)a);
}

/* 0x452940: 30 frames before the next menu input. */
void GAMEAPI menu_input_delay(void)
{
    *(int32_t *)0x4cb168 = 0x1e;
}

/* 0x45f790: network flag bit 9 (0x5d622c). */
uint32_t GAMEAPI network_flag9_set(uint32_t on)
{
    uint32_t flags = (*(uint32_t *)0x5d622c & 0xfffffdff) | (on & 1) << 9;

    *(uint32_t *)0x5d622c = flags;
    return flags;
}

/* 0x48c990: the network send buffer (0x8000 bytes, taken once) emptied. */
int32_t GAMEAPI network_buffer_reset(void)
{
    *(uint8_t *)0x5d6118 = 0;
    if (!*(int32_t *)0x5d611c)
        *(int32_t *)0x5d611c = heap_take(0x8000);
    *(int32_t *)0x5d6120 = *(int32_t *)0x5d611c + 0x8000;
    *(int32_t *)0x5d6124 = *(int32_t *)0x5d611c;
    return mem_fill((uint8_t *)0x5d5e18, 0, 0x300);
}

/* 0x48a840: the network restarted: closed, opened and started (0 if
 * either fails), then the player and message tables cleared. */
int32_t GAMEAPI network_restart(void)
{
    int32_t result;
    uint32_t a;

    net_close();
    result = net_open(0, 0, 0);
    if (!result)
        return result;
    result = net_start();
    if (!result)
        return result;
    for (a = 0x5d5368; a < 0x5d5968; a += 0x18)
        *(uint32_t *)(uintptr_t)a = 0;
    network_buffer_reset();
    *(uint32_t *)0x5d622c |= 1;
    *(uint32_t *)0x5d622c |= 4;
    for (a = 0x5d6128; a < 0x5d622c; a += 0x41)
        *(uint8_t *)(uintptr_t)a = 0;
    return 1;
}

/* 0x48cff0: a serial speed choice's baud rate (-1 for none). */
int32_t GAMEAPI serial_baud(int32_t choice)
{
    static const int32_t baud[] = {1200, 2400, 4800, 9600, 14400, 19200, 38400, 56000, 57600, 115200};

    if ((uint32_t)choice > 9)
        return -1;
    return baud[choice];
}

/* 0x48d070: a serial stop bits choice (0 -> 0, 1 -> 2). */
int32_t GAMEAPI serial_stop_bits(int32_t choice)
{
    if (choice == 0)
        return 0;
    if (choice != 1)
        return -1;
    return 2;
}

/* 0x48d090: a serial parity choice (0..4, else -1). */
int32_t GAMEAPI serial_parity(int32_t choice)
{
    if ((uint32_t)choice > 4)
        return -1;
    return choice;
}

/* 0x48d0e0: a serial flow control choice (1 -> 2, 2 -> 3, 3 -> 4, else 0). */
int32_t GAMEAPI serial_flow(int32_t choice)
{
    if (choice == 1)
        return 2;
    if (choice == 2)
        return 3;
    if (choice == 3)
        return 4;
    return 0;
}
