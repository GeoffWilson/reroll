/* Front end (frontend_frame 0x4302d0), group N: menu logic.
 *
 * Which menu to open after a race (0x42dd60, 0x42de50): the front end's
 * "come back to" flags pick a menu number for 0x447650.
 *
 * Menu builders: the network messages menu (0x44a260 with its title string
 * 0x45f490), the multiplayer menu (0x4518a0), the laps menus (0x44ffd0,
 * 0x4500f0, 0x4501a0) and the on/off options of the setup menu (meteors,
 * shield texture, water reflections, rubble, ice sheet, speed glow, LOV,
 * catch-up). Each option registers a handler at a fixed address; the
 * handlers (code Ghidra does not mark as functions, reached only through
 * those pointers) are here too, so the builders still pass the original
 * addresses and the hooks route the calls to C.
 *
 * Also: the network menu mode (0x45f500), the car stat bars of the car
 * choice screen (0x456f70), the menu model table lookup (0x4539d0), the
 * identity matrix (0x4264f0) and the background grid's selected-cell
 * setter (0x490ec0).
 *
 * Left original (drawing, parked): the text system (0x42e8f0, 0x42f6a0,
 * 0x42fd90 and helpers), 0x445340 (fitting a string to a width), texture
 * and surface uploads (0x4114b0, 0x41e1f0, 0x428c30, 0x428c60, 0x477860),
 * the 3D menu models (0x4538e0, 0x45b200), the background grid (0x4908a0
 * and helpers), sparks (0x462ae0), the palette rows (0x4530d0), clip codes
 * and ordering-table helpers (0x426b80, 0x427ab0, 0x4290a0); the debug log
 * and its file writing (0x476ec0, 0x4772e0, 0x428310, 0x43b760, 0x43b7b0). */
#include "../runtime.h"
#include "physics.h"

#define g_options (*(uint8_t **)0x4b2198)
#define g_race (*(uint8_t **)0x4b1960)

/* Not ours. */
typedef uint32_t(GAMEAPI *menu_open_fn)(int32_t menu);
typedef uint32_t(GAMEAPI *option_fn)(const char *name, int32_t a, const char *label, int32_t b, uint32_t handler,
                                     int32_t c);
typedef uint32_t(GAMEAPI *title_fn)(const char *name, int32_t a);
typedef uint32_t(GAMEAPI *item_fn)(int32_t a, int32_t b, int32_t text, int32_t c, int32_t d);
typedef uint32_t(GAMEAPI *select_fn)(uint32_t item);
typedef uint32_t(GAMEAPI *list_fn)(const char *text, int32_t a, int32_t b, int32_t c, int32_t d, int32_t e,
                                   int32_t f, const void *g, int32_t h);
typedef uint32_t(GAMEAPI *list_show_fn)(uint32_t list, int32_t a, const char *text);
typedef int32_t(GAMEAPI *string_fn)(char *out, int32_t size, const char *name, int32_t index);
typedef int32_t(__cdecl *sprintf_fn)(char *out, const char *format, ...);
typedef uint32_t(GAMEAPI *text_fn)(const char *text);
typedef uint32_t(GAMEAPI *toggle_fn)(uint32_t value, int32_t step, int32_t event);
typedef int32_t(GAMEAPI *count_fn)(int32_t event, int32_t arg, uint32_t value, int32_t max, int32_t step,
                                   int32_t text);
typedef uint32_t(GAMEAPI *value_text_fn)(int32_t arg, int32_t a, const char *name, uint32_t value);
#define menu_open ((menu_open_fn)0x447650)
#define option_onoff ((option_fn)0x44c6d0)
#define option_choice ((option_fn)0x44c9d0)
#define menu_title ((title_fn)0x448a00)
#define menu_item ((item_fn)0x448140)
#define menu_select ((select_fn)0x448b50)
#define text_list ((list_fn)0x448a50)
#define text_list_show ((list_show_fn)0x44a1c0)
#define string_get ((string_fn)0x42e6d0)
#define crt_sprintf ((sprintf_fn)0x4979f0)
#define network_text_set ((text_fn)0x45d150)
#define option_toggle ((toggle_fn)0x447f20)
#define option_count ((count_fn)0x450050)
#define option_value_text ((value_text_fn)0x4481a0)

/* --- which menu after a race --- */

/* 0x42dd60: the menu to come back to, from the front end's return flags
 * (0 when one is already open, -1 for none). */
int32_t GAMEAPI menu_after_race(void)
{
    if (*(uint32_t *)0x4beb44)
        return 0;
    if (*(uint32_t *)0x4beb38 && *(uint8_t *)0x6c1fcf)
        return 0;
    if (*(uint32_t *)0x4beb48 || (!*(uint32_t *)0x4beb38 && *(uint8_t *)0x6c1fcf))
        return 1;
    if (*(uint32_t *)0x4beb4c)
        return 2;
    if (*(uint32_t *)0x4beb50)
        return 3;
    if (*(uint32_t *)0x4beb5c)
        return 4;
    if (*(uint32_t *)0x4beb60)
        return 5;
    if (*(uint32_t *)0x4beb6c)
        return 6;
    if (*(uint8_t *)0x6c1ff9 || *(uint32_t *)0x4beb64 || *(uint32_t *)0x4beb70)
        return 7;
    if (*(uint8_t *)0x6c1fc1)
        return 8;
    if (*(uint8_t *)0x6c1fce || *(uint8_t *)0x6c2093 || *(uint32_t *)0x4beb74)
        return 9;
    return -1;
}

/* 0x42de50: open that menu. */
uint32_t GAMEAPI menu_return(void)
{
    int32_t menu = menu_after_race();

    if (menu == -1)
        return (uint32_t)-1;
    return menu_open(menu);
}

/* --- menu builders --- */

/* 0x45f490: the network messages title (string 7 of PC_NETWORK_MESSAGES
 * with the message count), also handed to the network code. */
char *GAMEAPI network_messages_title(void)
{
    char format[0x80];

    if (string_get(format, 0x80, (const char *)0x4b0e34, 7) != -1) {
        crt_sprintf((char *)0x4d07f8, format, *(uint32_t *)0x5d2cc4);
        network_text_set((const char *)0x4d07f8);
    }
    return (char *)0x4d07f8;
}

/* 0x44a260: the network messages menu. */
uint32_t GAMEAPI network_messages_menu(void)
{
    const char *title = network_messages_title();
    uint32_t list = text_list(title, 0, 1, 0x3e, 0x1ad, 0, 0, (const void *)0x4aca1c, -1);

    return text_list_show(list, 0, title);
}

/* 0x4518a0: the multiplayer menu (two items; the first is selected when
 * 0x4af798 is set).
 * Deliberate change: "Networked" is greyed out and "Single Machine"
 * selected. The network game itself is C (orig_pl/pm/pu/pv), but there is
 * no transport under it yet (native/dplayx.c answers "no network"), so its
 * screens could only lead to an empty session list. */
uint32_t GAMEAPI multiplayer_menu(void)
{
    uint32_t first;

    *(uint32_t *)0x4cb48c = 1;
    g_options[0x75] = 0;
    menu_title((const char *)0x4affb4, 0);
    first = menu_item(0, -1, 0x2d, 1, 0x33);
    *(uint8_t *)0x4c9dfd = 1; /* the next item greyed out */
    menu_item(0, -1, 0x44, 1, 0x33);
    return menu_select(first);
}

/* 0x44ffd0: a laps menu with the given handler. */
uint32_t GAMEAPI laps_menu(uint32_t handler, int32_t arg)
{
    return option_choice((const char *)0x4b021c, 0, (const char *)0x4b021c, 0, handler, arg);
}

/* 0x4500f0, 0x4501a0: the two laps menus. */
uint32_t GAMEAPI laps_menu_race(int32_t arg)
{
    return laps_menu(0x450110, arg);
}

uint32_t GAMEAPI laps_menu_other(int32_t arg)
{
    return laps_menu(0x4501c0, arg);
}

/* 0x44fed0: the meteors option (0, 1, 2 at options +0x2d). */
uint32_t GAMEAPI option_meteors_add(void)
{
    return option_choice((const char *)0x4b01fc, 0, (const char *)0x4b01fc, 1, 0x44fef0, 1);
}

/* The on/off options of the setup menu. */
uint32_t GAMEAPI option_shield_texture_add(void) /* 0x450680 */
{
    return option_onoff((const char *)0x4b02d4, 0, (const char *)0x4b02d4, 1, 0x4506a0, 1);
}

uint32_t GAMEAPI option_water_reflections_add(void) /* 0x4506e0 */
{
    return option_onoff((const char *)0x4b02fc, 0, (const char *)0x4b02fc, 1, 0x450700, 1);
}

uint32_t GAMEAPI option_rubble_add(void) /* 0x450810 */
{
    return option_onoff((const char *)0x4b0344, 0, (const char *)0x4b0344, 1, 0x450830, 1);
}

uint32_t GAMEAPI option_ice_sheet_add(void) /* 0x450870 */
{
    return option_onoff((const char *)0x4b0364, 0, (const char *)0x4b0364, 1, 0x450890, 1);
}

uint32_t GAMEAPI option_speed_glow_add(void) /* 0x4508d0 */
{
    return option_onoff((const char *)0x4b0384, 0, (const char *)0x4b0384, 1, 0x4508f0, 1);
}

uint32_t GAMEAPI option_lov_add(void) /* 0x450ba0 */
{
    return option_onoff((const char *)0x4b0408, 0, (const char *)0x4b0408, 1, 0x450bc0, 1);
}

uint32_t GAMEAPI option_catch_up_add(void) /* 0x450e90 */
{
    return option_onoff((const char *)0x4b0444, 0, (const char *)0x4b0444, 1, 0x450eb0, 1);
}

/* --- option handlers (event, arg): 1 when handled --- */

/* 0x44fef0: meteors. 0x12 shows the value (text 2 + setting), 0x14 steps
 * down, 0x15 and 0x16 step up (wrapping to 0), 0x13 does nothing. */
uint32_t GAMEAPI option_meteors(int32_t event, int32_t arg)
{
    switch (event) {
    case 0x12:
        if (g_options[0x2d] > 2)
            g_options[0x2d] = 2;
        option_value_text(arg, -1, (const char *)0x4b01fc, (uint32_t)g_options[0x2d] + 2);
        return 1;
    case 0x13:
        return 1;
    case 0x14:
        g_options[0x2d]--;
        if (g_options[0x2d] > 2)
            g_options[0x2d] = 2;
        return 1;
    case 0x15:
    case 0x16:
        g_options[0x2d]++;
        if (g_options[0x2d] > 2)
            g_options[0x2d] = 0;
        return 1;
    }
    return 0;
}

/* An on/off setting byte (at `offset` in the block `*base`) stepped by
 * 0x447f20; 1 when it is now off. The block pointer is read again after
 * the call, as the original does. */
static uint32_t option_flag(uint8_t *const *base, uint32_t offset, int32_t event)
{
    uint8_t value = (uint8_t)option_toggle((*base)[offset], 1, event);

    (*base)[offset] = value;
    return (*base)[offset] == 0;
}

uint32_t GAMEAPI option_shield_texture(int32_t event) /* 0x4506a0 */
{
    return option_flag(&g_options, 0x2a, event);
}

uint32_t GAMEAPI option_water_reflections(int32_t event) /* 0x450700 */
{
    return option_flag(&g_options, 0x2b, event);
}

uint32_t GAMEAPI option_rubble(int32_t event) /* 0x450830 */
{
    return option_flag(&g_options, 0x2e, event);
}

uint32_t GAMEAPI option_ice_sheet(int32_t event) /* 0x450890 */
{
    return option_flag(&g_options, 0x32, event);
}

uint32_t GAMEAPI option_speed_glow(int32_t event) /* 0x4508f0 */
{
    return option_flag(&g_options, 0x30, event);
}

uint32_t GAMEAPI option_lov(int32_t event) /* 0x450bc0 */
{
    return option_flag(&g_options, 0x31, event);
}

uint32_t GAMEAPI option_catch_up(int32_t event) /* 0x450eb0 (race setting +0xa87) */
{
    return option_flag(&g_race, 0xa87, event);
}

/* A laps count (1..3, text 0x32) stepped by 0x450050; 0 when unchanged. */
static uint32_t option_laps(uint32_t offset, int32_t event, int32_t arg)
{
    int32_t laps = option_count(event, arg, g_options[offset], 3, 1, 0x32);

    if (laps != -1)
        g_options[offset] = (uint8_t)laps;
    return laps != -1;
}

uint32_t GAMEAPI option_laps_race(int32_t event, int32_t arg) /* 0x450110 (options +0x35) */
{
    return option_laps(0x35, event, arg);
}

uint32_t GAMEAPI option_laps_other(int32_t event, int32_t arg) /* 0x4501c0 (options +0x85) */
{
    return option_laps(0x85, event, arg);
}

/* --- the rest --- */

/* 0x45f500: the network menu mode; mode 1 also clears the message count. */
int32_t GAMEAPI network_menu_mode(int32_t mode)
{
    *(int32_t *)0x5d5cd0 = -1;
    *(int32_t *)0x5d5ccc = mode;
    if (mode == 1)
        *(uint32_t *)0x5d2cc4 = 0;
    return mode;
}

/* 0x456f70: a car's stat bar (0..255) on the car choice screen, from its
 * handling table (0x10 bytes per car at 0x4a7910): 0 speed (+8), 1 grip
 * (+0), 2 weight (+0xc), 3 acceleration (+4). */
int32_t GAMEAPI car_stat_bar(uint32_t stat, int32_t car)
{
    const int32_t *stats = (const int32_t *)(0x4a7910 + (uint32_t)car * 0x10);
    int32_t value = 0, low = 0, high = 10;

    switch (stat) {
    case 0:
        value = stats[2];
        low = 0x40000;
        high = 0x120000;
        break;
    case 1:
        value = stats[0];
        low = -80000;
        high = 80000;
        break;
    case 2:
        value = stats[3] + 1;
        low = 0;
        high = 3;
        break;
    case 3:
        value = stats[1];
        low = -0xdac;
        high = 5000;
        break;
    }
    return (int32_t)(((uint32_t)value - (uint32_t)low) * 0xff) / (high - low);
}

/* 0x4539d0: the menu model record (0x48 bytes, 100 of them at 0x4cba80)
 * with this id, or 0. */
uint8_t *GAMEAPI menu_model_find(int32_t id)
{
    uint8_t *record;

    for (record = (uint8_t *)0x4cba80; record < (uint8_t *)0x4cd6a0; record += 0x48)
        if (*(int32_t *)record == id)
            return record;
    return 0;
}

/* 0x4264f0: an identity rotation (1.0 = 0x1000) and zero translation. */
uint8_t *GAMEAPI matrix_identity(uint8_t *m)
{
    *(uint32_t *)(m + 0) = 0;
    *(uint32_t *)(m + 4) = 0;
    *(uint32_t *)(m + 8) = 0;
    *(uint32_t *)(m + 0xc) = 0;
    *(uint16_t *)(m + 0x10) = 0;
    *(uint16_t *)(m + 0) = 0x1000;
    *(uint16_t *)(m + 8) = 0x1000;
    *(uint16_t *)(m + 0x10) = 0x1000;
    *(uint32_t *)(m + 0x14) = 0;
    *(uint32_t *)(m + 0x18) = 0;
    *(uint32_t *)(m + 0x1c) = 0;
    return m;
}

/* 0x490ec0: the background grid's highlighted cell. */
int32_t GAMEAPI grid_highlight_set(int32_t cell)
{
    *(int32_t *)0x4da214 = cell;
    return cell;
}
