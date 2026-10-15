/* The front end, group L: the front end's per-frame update (0x4302d0) and
 * the menu screen builder (0x44a2a0).
 *
 * Both call every other function by its address, so the hooks route each to
 * its C version (or the original) with the original's order and arguments.
 * The menu builder is one switch over the screen number (0x4af64c): each
 * screen adds its title, items, their targets and text, and the network
 * screens also drive the session calls. Drawing, text, sound and network
 * stay with the functions it calls. */
#include "../log.h"
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define g_race GP(0x4b1960)
#define g_config GP(0x4b2198)
#define g_screen (*(int32_t *)0x4af64c)      /* current menu screen */
#define g_screen_next (*(int32_t *)0x4af650) /* screen to go to, -1 none */
#define g_fade (*(int32_t *)0x4ac990)        /* 0 clear .. 0x100 black */
#define g_fade_dir (*(int32_t *)0x4be330)    /* 1 fading in, 2 out */
#define g_screen_built (*(uint32_t *)0x4cb58c)
#define g_screen_new (*(uint32_t *)0x4cb590)
#define g_quit_frames (*(int32_t *)0x4be450)
#define g_attract_frames (*(uint32_t *)0x4be328)
#define g_attract_level (*(uint32_t *)0x6c8160)
#define g_attract_on (*(uint32_t *)0x4ac994)
#define g_menu_page (*(uint32_t *)0x4ac980)
#define g_item_off (*(uint8_t *)0x4c9dfc)   /* next item left out (seen in game) */
#define g_item_hide (*(uint8_t *)0x4c9dfd)  /* next item greyed out (seen in game; fe_y.c agrees) */
#define g_item_flag (*(uint8_t *)0x4c9dfb)
#define g_item_last (*(uint8_t *)0x4c9e00)
#define g_network (*(uint32_t *)0x4cb48c)   /* the multiplayer screens are for a network game */
#define g_with_select (*(uint32_t *)0x4cb59c)
#define g_text_x (*(int32_t *)0x4ac988)
#define g_text_y (*(int32_t *)0x4ac98c)

#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define I32(p, off) (*(int32_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define G32(a) (*(uint32_t *)(a))
#define P(x) ((uint32_t)(uintptr_t)(x))

/* Menu option handlers (fe_z.c's menu_add_*): */
typedef int32_t(GAMEAPI *option_fn)(int32_t command, char *text);
#define OPTION_FN(a) ((option_fn)(uintptr_t)(a))

/* Calls where the original pushes more arguments than the callee (name)
 * reads: its prototype plus the unread extras, kept as the original passes
 * them (harmless in either calling convention). */
#define XCALL(addr, name, ret, params) ((ret(GAMEAPI *) params)(uintptr_t)(addr))

/* The menu helpers most screens use (other groups, or still original). */
#define menu_begin(title) GCALL(0x448a00, menu_set_title)(GMEM(title), 0)                         /* title string id */
#define menu_add(text, index, target, kind, style) GCALL(0x448140, menu_add_item)(GMEM(text), index, target, kind, style)
#define menu_select(item) GCALL(0x448b50, menu_entry_select)(item)
#define menu_add_pair(a, b, c, d, target, kind, style) GCALL(0x448c00, menu_entry_with_text)(GMEM(a), b, GMEM(c), d, target, kind, style)
#define text_lookup(buf, size, section, index) GCALL(0x4481a0, menu_text_get)((void *)(buf), size, GMEM(section), index)
#define text_block(buf, a, b, x, y, c, d, e, f) GCALL(0x448a50, menu_text_add)((void *)(buf), a, b, x, y, c, d, e, f)
#define track_name(buf, track) GCALL(0x4484a0, driver_name)((void *)(buf), track)
#define league_name(buf, league, track) GCALL(0x4485b0, track_title_text)((void *)(buf), league, track)
#define go_screen(screen, how) GCALL(0x4479d0, fe_menu_goto)(screen, how)
#define net_state(state) GCALL(0x45f500, network_menu_mode)(state)
#define g_name_buf 0x4c9e94

#ifdef REROLL_NATIVE
/* ---- Options > Display (native build): fullscreen or a window, and the
 * resolution (native/user32.c applies and saves them) ---- */
#include "../../native/display.h"
#include "../vkgame.h"

#define DISPLAY_SCREEN 0x7b /* a screen number the original leaves unused */
#define DISPLAY_MODES 32

static struct nt_display display_pending;
static int display_mode_w[DISPLAY_MODES], display_mode_h[DISPLAY_MODES], display_mode_count;
static int display_mode; /* -1 the desktop's, else an index into display_mode_w/h */

static void display_text(char *text, const char *value)
{
    int i;
    for (i = 0; value[i] && i < 0x3f; i++)
        text[i] = value[i];
    text[i] = 0;
}

static int32_t GAMEAPI display_window_option(int32_t command, char *text)
{
    switch (command) {
    case 0x12:
        display_text(text, display_pending.fullscreen ? "Fullscreen" : "Windowed");
        return 1;
    case 0x13:
        return 1;
    case 0x14:
    case 0x15:
    case 0x16:
        display_pending.fullscreen = !display_pending.fullscreen;
        return 1;
    default:
        return 0;
    }
}

static int32_t GAMEAPI display_resolution_option(int32_t command, char *text)
{
    char value[32];
    switch (command) {
    case 0x12:
        if (display_mode < 0) {
            display_text(text, "Desktop");
        } else {
            GCALL(0x4979f0, msvc_sprintf)((void *)(value), (void *)("%dx%d"), display_mode_w[display_mode], display_mode_h[display_mode]); /* sprintf */
            display_text(text, value);
        }
        return 1;
    case 0x13:
        return 1;
    case 0x14: /* left: smaller (the list is largest first; the desktop's after the largest) */
        display_mode = display_mode + 1 >= display_mode_count ? -1 : display_mode + 1;
        return 1;
    case 0x15:
    case 0x16: /* right: larger */
        display_mode = display_mode < 0 ? display_mode_count - 1 : display_mode - 1;
        return 1;
    default:
        return 0;
    }
}

static void GAMEAPI display_apply_selected(uint32_t value)
{
    (void)value;
    display_pending.w = display_mode < 0 ? 0 : display_mode_w[display_mode];
    display_pending.h = display_mode < 0 ? 0 : display_mode_h[display_mode];
    nt_display_set(&display_pending);
}

/* Options > Graphics: anisotropic filtering (the Vulkan renderer's), Off
 * to 16x; applied at once and saved. */
static int32_t GAMEAPI anisotropy_option(int32_t command, char *text)
{
    static const int steps[5] = {1, 2, 4, 8, 16};
    int now = vkgame_anisotropy(), i = 0, n;
    char value[16];

    while (i < 4 && steps[i] < now)
        i++;
    switch (command) {
    case 0x12:
        if (steps[i] == 1) {
            display_text(text, "Off");
        } else {
            GCALL(0x4979f0, msvc_sprintf)((void *)(value), (void *)("%dx"), steps[i]); /* sprintf */
            display_text(text, value);
        }
        return 1;
    case 0x13:
        return 1;
    case 0x14: /* left: less */
    case 0x15:
    case 0x16: /* right: more */
        n = command == 0x14 ? (i == 0 ? 4 : i - 1) : (i == 4 ? 0 : i + 1);
        vkgame_set_anisotropy(steps[n]);
        nt_anisotropy_set(steps[n]);
        return 1;
    default:
        return 0;
    }
}

/* Options > Display: the start-up movies (Psygnosis, ATD, intro) on or
 * off. The game's own switch: settings +0xc (saved in DataPC) stops them
 * (0x444c50); nothing in the original's menus set it. The credits and the
 * league's movies still play. */
static int32_t GAMEAPI intro_movies_option(int32_t command, char *text)
{
    switch (command) {
    case 0x12:
        display_text(text, U8(g_config, 0xc) ? "Off" : "On");
        return 1;
    case 0x13:
        return 1;
    case 0x14:
    case 0x15:
    case 0x16:
        U8(g_config, 0xc) = !U8(g_config, 0xc);
        return 1;
    default:
        return 0;
    }
}

static void display_menu(void)
{
    int i;

    if (g_screen_new) { /* entering it (the screen is rebuilt on every change) */
        nt_display_get(&display_pending);
        display_mode_count = nt_display_modes(display_mode_w, display_mode_h, DISPLAY_MODES);
        display_mode = -1;
        for (i = 0; i < display_mode_count; i++)
            if (display_mode_w[i] == display_pending.w && display_mode_h[i] == display_pending.h)
                display_mode = i;
    }
    GCALL(0x448a00, menu_set_title)((void *)("Display"), -3);
    *(uint32_t *)0x4af5f4 = 0x1a4; /* a box wide enough for the values (as the network screens) */
    /* labels with the string file's markup (as "Transparency Mode ~>~P0~S"):
     * the value (~P0~) in the right-hand column */
    GCALL(0x44c9d0, menu_add_slider)((void *)("Mode ~>~P0~S"), -3, (void *)("Mode ~>~P0~S"), -3, display_window_option, 1);
    GCALL(0x44c9d0, menu_add_slider)((void *)("Resolution ~>~P0~S"), -3, (void *)("Resolution ~>~P0~S"), -3, display_resolution_option, 1);
    GCALL(0x44c9d0, menu_add_slider)((void *)("Intro Movies ~>~P0~S"), -3, (void *)("Intro Movies ~>~P0~S"), -3, intro_movies_option, 1);
    *(uint32_t *)0x4ca300 = P(display_apply_selected);
    menu_add(P("Apply"), -3, 0x66, 2, 1);
}
#endif

/* 0x44a2a0: build the menu for the current screen (g_screen). */
void GAMEAPI menu_screen_build(void)
{
    char text[0x200], format[0x200], line[0x80], full[0x40], open[0x40];
    uint8_t *cfg;
    int32_t item, i, off;
    uint32_t next;

    if (g_screen < 0 || g_screen >= 0x82) {
        *(uint8_t *)0x4cb588 = 0xff;
        *(uint8_t *)0x4cb589 = 0xff;
    } else {
        cfg = g_config;
        *(uint8_t *)0x4cb588 = U8(cfg, g_screen + 0x9cc);
        *(uint8_t *)0x4cb589 = U8(cfg, g_screen + 0xacc);
    }
    GCALL(0x445480, fe_menu_reset)();
    *(int32_t *)0x4af664 = -1;
    switch ((uint32_t)g_screen) {
    case 0: /* error message */
        menu_begin(0x4b0064);
        if (I32(g_config, 0x48) < 0x80) {
            text_lookup(text, 0x200, 0x4b0064, I32(g_config, 0x48) + 2);
        } else {
            text_lookup(format, 0x200, 0x4b0064, 1);
            GCALL(0x4979f0, msvc_sprintf)((void *)(text), (void *)(format), U32(g_config, 0x48), U32(g_config, 0x4c)); /* sprintf */
        }
        text_block(text, 0, 1, g_text_x, g_text_y - 0x14, 1, 1, 0, G32(0x4ac848));
        *(uint32_t *)0x4cb4e0 = 0x1ae;
        menu_add(0x4b0064, 2, 4, 1, -1);
        I32(g_config, 0x48) = -1;
        break;
    case 1:
        menu_begin(0x4b0050);
        menu_add(0, -1, 4, 1, -1);
        break;
    case 2:
        menu_begin(0x4b0038);
        menu_add(0, -1, U8(g_config, 0x20), 1, 1);
        GCALL(0x448be0, menu_common_footer)(0, 1);
        break;
    default:
        g_screen = 4;
        /* fall through */
    case 4: /* main menu */
        U8(g_config, 0x51) = 0xff;
        U8(g_config, 0x55) = 0xff;
        U8(g_config, 0x59) = 0xff;
        U8(g_config, 0x5d) = 0xff;
        U8(g_config, 0x61) = 0xff;
        GCALL(0x4447d0, fe_history_clear)();
        *(uint32_t *)0x4cb490 = 0;
        menu_begin(0x4afb9c);
        *(uint32_t *)0x4cb530 = 0x44da90;
        if (G32(0x4acc54) != 2)
            g_item_hide = 1;
        menu_add(0, -1, 0xc, 1, 0x31);
        if (G32(0x4acc54) != 2)
            g_item_hide = 1;
        menu_add(0, -1, 0x1a, 1, 0x31);
        if (G32(0x4acc54) != 2)
            g_item_hide = 1;
        menu_add(0, -1, 0x23, 1, 0x37);
        menu_add(0, -1, 0x2c, 1, 0x33);
        menu_add(0, -1, 0x5f, 0, 0x32);
        menu_add(0, -1, 0x60, 1, 0x37);
        menu_add(0, -1, 0x66, 1, 1);
        g_item_off = U8(g_config, 7) == 0;
        menu_add(0, -1, 0x70, 1, 1);
        g_item_last = 1;
        menu_add_pair(0, -1, 0, -1, 0x72, 0, 1);
        break;
    case 5: /* replay */
        menu_begin(0x4b0028);
        item = menu_add(0, -1, U8(g_config, 0x20), 1, 1);
        menu_add(0, -1, 6, 0, 1);
        if (g_with_select)
            menu_select(item);
        GCALL(0x448be0, menu_common_footer)(0, 1);
        break;
    case 7:
        GCALL(0x44e470, world_menu)(8, 1);
        break;
    case 8:
        GCALL(0x44fac0, driver_menu_1)(9, 1);
        break;
    case 10:
        GCALL(0x44f5a0, game_points_menu)(4, 1, 0, 0);
        break;
    case 0xc:
        GCALL(0x451900, league_menu)(0xd, 1, 1, 0);
        break;
    case 0xd:
        GCALL(0x44fac0, driver_menu_1)(0xe, 1);
        break;
    case 0xe: /* league: confirm */
        *(uint32_t *)0x4cb4dc = 0x1e;
        *(uint32_t *)0x4af5fc = 0;
        *(uint32_t *)0x4cb4e0 = 0x82;
        *(uint32_t *)0x4af600 = 0;
        menu_begin(0x4b0010);
        item = menu_add(0, -1, 0x11, 0, 0x31);
        GCALL(0x44e2a0, league_title)(GMEM(g_name_buf), U8(g_config, 0x1c));
        menu_add(0, -1, 0xf, 1, 0x31);
        track_name(g_name_buf, U8(g_config, 0x18));
        menu_add(0, -1, 0x10, 1, 0x31);
        GCALL(0x450210, setup_difficulty_option)(0x31, U8(g_config, 0x40));
        GCALL(0x44ffb0, options_submenu)(0x31);
        menu_add_pair(0, -1, 0, -1, 4, 1, 0x31);
        menu_select(item);
        break;
    case 0xf:
        GCALL(0x451900, league_menu)(0x87, 2, 0, 0);
        break;
    case 0x12:
        GCALL(0x44f2d0, new_record_menu)(U8(g_race, 0x579), 0, 0x13, 1);
        break;
    case 0x13:
        next = G32(0x4cb55c) ? 4 : 0x14;
        GCALL(0x44f000, records_menu_track)(U8(g_race, 0x579), next, 1);
        break;
    case 0x14:
        GCALL(0x44f5a0, game_points_menu)(0x15, 1, 0, 0);
        break;
    case 0x15: {
        uint8_t *c = g_config;
        uint32_t target = 0x16, a = 0, b = 1;

        if (U8(c, 0x9c) != 0) {
            target = 0x17;
            a = 1;
            b = 0;
        }
        if (U8(c, 0x9d) != 0) {
            target = 0x18;
            a = 1;
            b = 0;
        }
        GCALL(0x44f860, fe_league_points)(b, target, a, 0);
        break;
    }
    case 0x17:
    case 0x19:
        GCALL(0x451900, league_menu)(0xd, 1, 1, 1);
        break;
    case 0x18: /* league lost */
        menu_begin(0x4afffc);
        text_lookup(text, 0x200, 0, -1);
        text_block(text, 0x80000000, 1, g_text_x, g_text_y - 0x14, 1, 1, 0, G32(0x4ac844));
        GCALL(0x44e410, menu_continue_entry)(1, 0x19, 1, 0x31);
        break;
    case 0x1a:
        U8(g_config, 0x74) = 0;
        GCALL(0x44e470, world_menu)(0x1b, 1);
        break;
    case 0x1b:
        GCALL(0x44fac0, driver_menu_1)(0x1c, 1);
        break;
    case 0x1c: /* arcade: confirm */
        *(uint32_t *)0x4cb4dc = 0x1e;
        *(uint32_t *)0x4af5fc = 0;
        *(uint32_t *)0x4cb4e0 = 0x82;
        *(uint32_t *)0x4af600 = 0;
        U8(g_config, 0x74) = 0;
        menu_begin(0x4affe4);
        item = menu_add(0, -1, 0x1f, 0, 0x31);
        league_name(g_name_buf, U8(g_config, 0x1c), U8(g_config, 0x1d));
        menu_add(0, -1, 0x1d, 1, 0x31);
        track_name(g_name_buf, U8(g_config, 0x18));
        menu_add(0, -1, 0x1e, 1, 0x31);
        GCALL(0x450210, setup_difficulty_option)(0x31, -1);
        GCALL(0x4500f0, laps_menu_race)(0x31);
        menu_add_pair(0, -1, 0, -1, 4, 1, 0x31);
        menu_select(item);
        break;
    case 0x1d:
    case 0x26:
    case 0x37:
        GCALL(0x44e470, world_menu)(0x87, 2);
        break;
    case 0x20:
        GCALL(0x44f2d0, new_record_menu)(U8(g_race, 0x579), 0, 0x21, 1);
        break;
    case 0x21:
        GCALL(0x44f000, records_menu_track)(U8(g_race, 0x579), 0x22, 1);
        break;
    case 0x22:
        GCALL(0x44f5a0, game_points_menu)(0x1c, 1, 0, 0);
        break;
    case 0x23:
        U8(g_config, 0x74) = 0;
        GCALL(0x44e470, world_menu)(0x24, 1);
        break;
    case 0x24:
        GCALL(0x44fac0, driver_menu_1)(0x25, 1);
        break;
    case 0x25: /* time attack: confirm */
        menu_begin(0x4affc8);
        item = menu_add(0, -1, 0x28, 0, 0x37);
        league_name(g_name_buf, U8(g_config, 0x1c), U8(g_config, 0x1d));
        menu_add(0, -1, 0x26, 1, 0x37);
        track_name(g_name_buf, U8(g_config, 0x18));
        menu_add(0, -1, 0x27, 1, 0x37);
        menu_add_pair(0, -1, 0, -1, 4, 1, 0x37);
        menu_select(item);
        break;
    case 0x29:
        GCALL(0x44f2d0, new_record_menu)(U8(g_race, 0x579), 0, 0x2a, 1);
        break;
    case 0x2a:
        GCALL(0x44f000, records_menu_track)(U8(g_race, 0x579), 0x2b, 1);
        break;
    case 0x2b:
        GCALL(0x4488c0, time_attack_report)(U8(g_race, 0x579), 0, 0x25, 1);
        break;
    case 0x2c: /* multiplayer */
        g_network = 0;
        U8(g_config, 0x74) = 0;
        U8(g_config, 0x75) = 0;
        menu_begin(0x4affb4);
        if (G32(0x4acc54) != 2)
            g_item_hide = 1;
        menu_add(0, -1, 0x2e, 1, 0x33);
        menu_add(0, -1, 0x46, 1, 0x33);
        break;
    case 0x2d:
        *(uint32_t *)0x4af798 = 1;
        /* fall through */
    case 0x2e:
        for (off = 0x9c0; off < 0x9cc; off += 2)
            *(uint16_t *)(g_config + off) = 0;
        GCALL(0x44fdb0, players_menu)(g_network ? 0x30 : 0x2f, 1);
        break;
    case 0x2f:
        GCALL(0x44e470, world_menu)(0x30, 1);
        break;
    case 0x30:
        GCALL(0x44fac0, driver_menu_1)(0x31, 1);
        break;
    case 0x31:
        GCALL(0x44fd50, driver_menu_2)(U8(g_config, 0x74) == 2 ? 0x35 : 0x32, 1);
        break;
    case 0x32:
        GCALL(0x44fd70, driver_menu_3)(U8(g_config, 0x74) == 3 ? 0x35 : 0x33, 1);
        break;
    case 0x33:
        GCALL(0x44fd90, driver_menu_4)(0x35, 1);
        break;
    case 0x34:
        g_network = 1;
        /* fall through */
    case 0x35: /* split screen: confirm */
        menu_begin(0x4aff8c);
        item = menu_add(0, -1, 0x3c, 0, 0x33);
        GCALL(0x44e320, fe_split_screen_label)((void *)(text));
        menu_add(P(text), -3, 0x36, 1, 0x33);
        if (g_network) {
            text_lookup(text, 0x200, 0, -1);
            GCALL(0x44e800, fe_world_row)(P(text));
        } else {
            league_name(g_name_buf, U8(g_config, 0x1c), U8(g_config, 0x1d));
            menu_add(0, -1, 0x37, 1, 0x33);
        }
        track_name(g_name_buf, U8(g_config, 0x18));
        menu_add(0, -1, 0x38, 1, 0x33);
        track_name(g_name_buf, U8(g_config, 0x19));
        menu_add(0, -1, 0x39, 1, 0x33);
        cfg = g_config;
        g_item_flag = U8(cfg, 0x74) < 3;
        track_name(g_name_buf, U8(cfg, 0x1a));
        menu_add(0, -1, 0x3a, 1, 0x33);
        cfg = g_config;
        g_item_flag = U8(cfg, 0x74) < 4;
        track_name(g_name_buf, U8(cfg, 0x1b));
        menu_add(0, -1, 0x3b, 1, 0x33);
        cfg = g_config;
        if (g_network) {
            U8(cfg, 0x88) = 3;
            g_item_hide = 1;
        } else if (U8(cfg, 0x88) == 3) {
            U8(cfg, 0x88) = 0;
        }
        GCALL(0x44c9d0, menu_add_slider)(GMEM(0x4aff70), 0, GMEM(0x4aff70), 1, OPTION_FN(0x44de10), 0x33);
        if (!g_network) {
            if (U8(g_config, 0x88) != 0)
                g_item_hide = 1;
            GCALL(0x450210, setup_difficulty_option)(0x33, -1);
            GCALL(0x450150, list_menu_open)();
        }
        menu_add_pair(0, -1, 0, -1, 4, 1, 0x33);
        menu_select(item);
        break;
    case 0x36:
        GCALL(0x44fdb0, players_menu)(0x87, 2);
        break;
    case 0x39:
        GCALL(0x44fd50, driver_menu_2)(0x87, 2);
        break;
    case 0x3a:
        GCALL(0x44fd70, driver_menu_3)(0x87, 2);
        break;
    case 0x3b:
        GCALL(0x44fd90, driver_menu_4)(0x87, 2);
        break;
    case 0x3d:
        if (GCALL(0x44f560, new_record_check)(0, 0x3e))
            break;
        /* fall through */
    case 0x3e:
        if (GCALL(0x44f560, new_record_check)(1, 0x3f))
            break;
        /* fall through */
    case 0x3f:
        if (GCALL(0x44f560, new_record_check)(2, 0x40))
            break;
        /* fall through */
    case 0x40:
        if (GCALL(0x44f560, new_record_check)(3, 0x41))
            break;
        /* fall through */
    case 0x41:
        next = G32(0x4cb558) ? 0x35 : 0x42;
        GCALL(0x44f000, records_menu_track)(U8(g_race, 0x579), next, 1);
        break;
    case 0x42: {
        uint8_t *race = g_race;

        GCALL(0x44f030, splitscreen_report)(U8(race, 0x579), U8(g_config, 0x74), U8(race, 0x551) != 7, 0x35, 1);
        break;
    }
    case 0x43:
        g_network = 1;
        /* fall through */
    case 0x44:
        *(uint32_t *)0x4af798 = 1;
        /* fall through */
    case 0x45:
    case 0x46: /* network */
        *(uint32_t *)0x4cb594 = 1;
        /* Deliberate change: TCP/IP is the only connection (native/dplayx.c
         * offers no IPX, serial cable or modem), so a saved choice of
         * another one goes back to it. */
        U8(g_config, 0x80) = 0;
        if (g_screen_new) {
            GCALL(0x48a6b0, dplay_close)();
            *(uint32_t *)0x4cb574 = 10;
            GCALL(0x45f570, network_players_list)(GMEM(0x4cb574), GMEM(0x4cb16c));
            GCALL(0x44ce50, front_fade_in)();
        }
        menu_begin(0x4aff54);
        if (G32(0x4acc54) != 2)
            g_item_hide = 1;
        item = menu_add(0, -1, 0x4b, 1, 0x33);
        menu_add(0, -1, 0x54, 1, 0x33);
        GCALL(0x44dfd0, player_name_entry)(0x83, 5);
        menu_add(0, -1, 0x83, 5, 0x33);
        menu_add(0, -1, 0x49, 1, 0x33);
        GCALL(0x44c9d0, menu_add_slider)(GMEM(0x4aff3c), 0, GMEM(0x4aff3c), 1, OPTION_FN(0x44dce0), 0x33);
        GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, OPTION_FN(0x44dea0), 0x33);
        if (U8(g_config, 0x80) != 1)
            *(uint32_t *)0x4ca2f8 = 0x44dc50;
        GCALL(0x44c9d0, menu_add_slider)(GMEM(0x4aff14), 0, GMEM(0x4aff14), 1, OPTION_FN(0x44db60), 0x33);
        g_item_hide = U8(g_config, 0x80) == 1;
        menu_add(0, -1, 0x48, 1, 0x33);
        if (g_with_select)
            menu_select(item);
        menu_add_pair(0, -1, 0, -1, 4, 1, 0x33);
        break;
    case 0x48: /* network: connection settings */
        *(uint32_t *)0x4cb594 = 1;
        switch (U8(g_config, 0x80)) {
        case 0:
            menu_begin(0x4afee8);
            *(uint32_t *)0x4cb4c0 += 0xc;
            GCALL(0x4480d0, fe_menu_text_box)((void *)(g_config + 0xa0), GMEM(0x4c2ac0), 0x20, 0x44dfa0, 0);
            break;
        case 1:
            menu_begin(0x4afebc);
            break;
        case 2:
            menu_begin(0x4afe8c);
            GCALL(0x44c9d0, menu_add_slider)(GMEM(0x4afe70), 0, GMEM(0x4afe70), 1, OPTION_FN(0x450ca0), 1);
            GCALL(0x44c9d0, menu_add_slider)(GMEM(0x4afe50), 0, GMEM(0x4afe70), 1, OPTION_FN(0x450cf0), 1);
            GCALL(0x44c9d0, menu_add_slider)(GMEM(0x4afe30), 0, GMEM(0x4afe70), 1, OPTION_FN(0x450d90), 1);
            GCALL(0x44c9d0, menu_add_slider)(GMEM(0x4afe14), 0, GMEM(0x4afe70), 1, OPTION_FN(0x450d40), 1);
            GCALL(0x44c9d0, menu_add_slider)(GMEM(0x4afdf0), 0, GMEM(0x4afe70), 1, OPTION_FN(0x450de0), 1);
            break;
        case 3:
            menu_begin(0x4afdc4);
            *(uint32_t *)0x4cb4c0 += 0xc;
            GCALL(0x44c9d0, menu_add_slider)(0, -1, 0, -2, OPTION_FN(0x44ceb0), 0x33);
            GCALL(0x4480d0, fe_menu_text_box)((void *)(g_config + 0xc0), GMEM(0x4ca82c), 0x20, 0x44e1c0, 0);
            break;
        }
        menu_select(GCALL(0x44e410, menu_continue_entry)(0, 0x87, 2, 0x33));
        break;
    case 0x49: /* network: chat macros */
    case 0x4a: { /* network: system messages */
        int32_t base = g_screen == 0x49 ? 0xdce : 0x104e, end = g_screen == 0x49 ? 0x280 : 0x740;
        uint32_t edit = g_screen == 0x49 ? 0x44e200 : 0x44e250;

        *(uint32_t *)0x4cb594 = 1;
        *(uint32_t *)0x4af5f4 = 0x244;
        menu_begin(g_screen == 0x49 ? 0x4afd98 : 0x4afd64);
        for (off = 0, i = 0; off < end; off += 0x40, i++) {
            *(int32_t *)0x4ca2f0 = i;
            *(uint32_t *)0x4ca2fc = 0x44e1f0;
            if (G32(0x4cb598))
                *(int32_t *)0x4af708 = i;
            *(int32_t *)0x4c9e0c = -100;
            *(uint8_t *)0x4c9e1c = 1;
            GCALL(0x449060, name_entry_setup)(1, P(g_config + off + base), 0x40, 0x87, 2, edit, 0);
            GCALL(0x448ec0, name_entry_text)((void *)(g_config + off + base), 0x230, 0, G32(0x4ac848));
            menu_add(0x4afd90, -3, 0x87, 2, 0x33);
        }
        *(int32_t *)0x4c9e0c = -100;
        *(uint8_t *)0x4c9e1c = 1;
        GCALL(0x44e410, menu_continue_entry)(0, 0x87, 2, 0x33);
        break;
    }
    case 0x4b: /* network: start */
        *(uint32_t *)0x4cb594 = 1;
        menu_begin(0x4afd44);
        item = menu_add(0, -1, 0x4c, 1, 0x33);
        GCALL(0x4480d0, fe_menu_text_box)((void *)(g_config + 0x12b), GMEM(0x4c2484), 0x41, 0x44e080, 0);
        if (U8(g_config, 0x80) != 0 && U8(g_config, 0x80) != 1)
            g_item_hide = 1;
        GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, OPTION_FN(0x44dee0), 0x33);
        GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, OPTION_FN(0x44df20), 0x33);
        GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, OPTION_FN(0x44df60), 0x33);
        menu_add(0, -1, 0x4a, 1, 0x33);
        GCALL(0x44e430, menu_open_dialog)(0, 0x87, 2, 0x33);
        menu_select(item);
        break;
    case 0x4c: /* network: create a game */
        if (!g_screen_new) {
            if (!XCALL(0x48a840, network_restart, int32_t, (uint32_t, uint32_t))(1, 1)) {
                go_screen(0x46, 1);
            } else {
                cfg = g_config;
                GCALL(0x48b940, dplay_session_host)(P(cfg + 0x120), P(cfg + 0x12b));
                GCALL(0x45f790, network_flag9_set)(1);
                g_screen = (g_network != 0) + 0x4d;
                go_screen(g_screen, 0);
            }
        } else {
            GCALL(0x452940, menu_input_delay)();
            *(uint32_t *)0x4cb524 = 0x452950;
            *(uint32_t *)0x4cb5a0 = 1;
            *(int32_t *)(GP(0x4cb4b8) + 0x4f4) = -1;
            menu_begin(0x4afd24);
            text_lookup(text, 0x200, 0, -1);
            text_block(text, 0, 1, g_text_x, g_text_y, 1, 1, 0x4aca1c, -1);
            text_lookup(text, 0x200, 0, -1);
            text_block(text, 0, 2, g_text_x, g_text_y + 0x28, 1, 1, 0x4aca1c, -1);
        }
        break;
    case 0x4d:
        if (g_screen_new)
            net_state(2);
        GCALL(0x44e470, world_menu)(0x4e, 1);
        break;
    case 0x4e:
        if (g_screen_new)
            net_state(3);
        GCALL(0x44fac0, driver_menu_1)(0x4f, 1);
        break;
    case 0x4f: { /* network: host confirm */
        uint32_t league;

        if (g_screen_new) {
            GCALL(0x48d1c0, net_session_unlock)();
            *(uint32_t *)0x4cb57c = GCALL(0x43692a, game_time_get)();
            GCALL(0x45f7b0, net_name_send)(1);
            net_state(4);
        }
        cfg = g_config;
        league = U8(cfg, 0x1c);
        if (league != G32(0x4af88c) || U8(cfg, 0x1d) != G32(0x4af890) || U8(cfg, 0x87) != G32(0x4af894) ||
            U8(cfg, 0x8b) != G32(0x4af898) || U8(cfg, 0x85) != G32(0x4af89c)) {
            GCALL(0x45f710, network_send_setting)(league, U8(cfg, 0x1d), U8(cfg, 0x87), U8(cfg, 0x8b), U8(cfg, 0x85));
            cfg = g_config;
            *(uint32_t *)0x4af88c = U8(cfg, 0x1c);
            *(uint32_t *)0x4af890 = U8(cfg, 0x1d);
            *(uint32_t *)0x4af894 = U8(cfg, 0x87);
            *(uint32_t *)0x4af898 = U8(cfg, 0x8b);
            *(uint32_t *)0x4af89c = U8(cfg, 0x85);
        }
        *(uint32_t *)0x4cb524 = 0x452d30;
        *(uint32_t *)0x4cb594 = 1;
        *(uint32_t *)0x4af5f4 = 0x1a4;
        *(uint32_t *)0x4cb4dc = 0x1e;
        *(uint32_t *)0x4af5fc = 0;
        *(uint32_t *)0x4cb4e0 = 0x82;
        *(uint32_t *)0x4af600 = 0;
        menu_begin(0x4afd04);
        *(uint32_t *)0x4ca2f8 = 0x44dc90;
        item = menu_add(0, -1, 0x52, 1, 0x33);
        if (g_network) {
            text_lookup(text, 0x200, 0, -1);
            GCALL(0x44e800, fe_world_row)(P(text));
        } else {
            league_name(g_name_buf, U8(g_config, 0x1c), U8(g_config, 0x1d));
            menu_add(0, -1, 0x50, 1, 0x33);
        }
        track_name(g_name_buf, U8(g_config, 0x18));
        menu_add(0, -1, 0x51, 1, 0x33);
        if (g_network) {
            U8(g_config, 0x87) = 3;
            g_item_hide = 1;
        } else if (U8(g_config, 0x87) == 3) {
            U8(g_config, 0x87) = 0;
        }
        *(uint32_t *)0x4cb584 = 0;
        GCALL(0x44c9d0, menu_add_slider)(GMEM(0x4aff70), 0, GMEM(0x4aff70), 1, OPTION_FN(0x44dd30), 0x33);
        if (U8(g_config, 0x87) != 0)
            g_item_hide = 1;
        GCALL(0x450460, difficulty_option)(0x33);
        if (g_network == 1)
            g_item_hide = 1;
        GCALL(0x4501a0, laps_menu_other)(0x33);
        GCALL(0x4480d0, fe_menu_text_box)((void *)(g_config + 0x16c), GMEM(0x4cb0dc), 0x33, 0x44e0b0, 0x44e0e0);
        if (g_with_select)
            menu_select(item);
        menu_add_pair(0, -1, 0, -1, 0x46, 1, 0x33);
        GCALL(0x449fd0, network_lobby_build)();
        break;
    }
    case 0x50:
        if (g_screen_new)
            net_state(2);
        GCALL(0x44e470, world_menu)(0x87, 2);
        break;
    case 0x51:
    case 0x59:
        if (g_screen_new)
            net_state(3);
        /* fall through */
    case 0x10:
    case 0x1e:
    case 0x27:
    case 0x38:
        GCALL(0x44fac0, driver_menu_1)(0x87, 2);
        break;
    case 0x52: /* network: waiting for clients */
        if (g_screen_new) {
            net_state(5);
            GCALL(0x48d110, net_session_lock)();
        }
        *(uint32_t *)0x4cb524 = 0x452d30;
        *(uint32_t *)0x4cb594 = 1;
        *(uint32_t *)0x4af5f4 = 0x1a4;
        *(uint32_t *)0x4cb4dc = 0x1e;
        *(uint32_t *)0x4af5fc = 0;
        *(uint32_t *)0x4cb4e0 = 0x82;
        *(uint32_t *)0x4af600 = 0;
        menu_begin(0x4afce8);
        item = menu_add(0, -1, 0x87, 2, 0x33);
        text_lookup(text, 0x200, 0, -1);
        text_block(text, 0, 1, 0x1e, g_text_y, 0, 1, 0x4aca1c, -1);
        text_lookup(text, 0x200, 0, -1);
        text_block(text, 0, 1, 0x1e, g_text_y + 0x28, 0, 1, 0x4aca1c, -1);
        menu_select(item);
        GCALL(0x449fd0, network_lobby_build)();
        break;
    case 0x54: /* network: finding sessions */
        if (g_screen_new) {
            net_state(1);
            GCALL(0x452940, menu_input_delay)();
            *(uint32_t *)0x4cb580 = 0;
        }
        *(uint32_t *)0x4cb524 = 0x452e40;
        *(uint32_t *)0x4cb5a0 = 1;
        *(uint32_t *)0x4cb4dc = 0x1e;
        *(uint32_t *)0x4af5fc = 0;
        *(uint32_t *)0x4cb4e0 = 0x82;
        *(uint32_t *)0x4af600 = 0;
        menu_begin(0x4afcc8);
        *(uint32_t *)0x4ca300 = 0x452e30;
        menu_add(0, -1, 0x83, 5, -1);
        menu_add(0, -1, 0x87, 2, -1);
        text_lookup(text, 0x200, 0, -1);
        text_block(text, 0, 1, g_text_x, g_text_y, 1, 1, 0x4aca1c, -1);
        text_lookup(text, 0x200, 0, -1);
        text_block(text, 0, 1, g_text_x, g_text_y + 0x28, 1, 1, 0x4aca1c, -1);
        GCALL(0x44a260, network_messages_menu)();
        break;
    case 0x55: /* network: session list */
        GCALL(0x42e6d0, text_line_get)((void *)(full), 0x40, GMEM(0x4b0064), 0xb);
        GCALL(0x42e6d0, text_line_get)((void *)(open), 0x40, GMEM(0x4b0064), 0xc);
        if (!GCALL(0x48b780, network_sessions_list)(0x32, GMEM(0x4cb0d8), GMEM(0x4c3018), GMEM(0x4c98f4))) {
            go_screen(0x46, 1);
            break;
        }
        menu_begin(0x4afca0);
        for (i = 0; i < *(int32_t *)0x4cb0d8; i++) {
            uint32_t flags;
            const char *add = 0;

            GCALL(0x445340, menu_text_fit)((void *)(line), GMEM(((uint32_t *)0x4c3018)[i]), 0x1c2, 1, G32(0x4ac848));
            flags = ((uint32_t *)0x4c98f4)[i];
            *(uint32_t *)0x4ca300 = 0x44e460;
            *(int32_t *)0x4ca2f0 = i;
            if (flags)
                g_item_hide = 1;
            if (flags & 2)
                add = full;
            else if (flags & 1)
                add = open;
            if (add) {
                char *d = line;

                while (*d)
                    d++;
                do
                    *d++ = *add;
                while (*add++);
            }
            menu_add(P(line), -3, 0x56, 0, 0x33);
        }
        GCALL(0x44e430, menu_open_dialog)(0, 0x46, 1, 0x33);
        break;
    case 0x56:
        if (!GCALL(0x48b7e0, dplay_session_join)(G32(0x4af60c), P(g_config + 0x120))) {
            go_screen(0x46, 1);
            break;
        }
        g_screen = 0x57;
        /* fall through */
    case 0x57:
        if (g_screen_new)
            net_state(3);
        GCALL(0x44fac0, driver_menu_1)(0x58, 1);
        break;
    case 0x58: /* network: join confirm */
        if (g_screen_new) {
            GCALL(0x45f7b0, net_name_send)(1);
            net_state(4);
        }
        *(uint32_t *)0x4cb524 = 0x452db0;
        *(uint32_t *)0x4cb594 = 1;
        *(uint32_t *)0x4af5f4 = 0x1a4;
        *(uint32_t *)0x4cb4dc = 0x1e;
        *(uint32_t *)0x4af5fc = 0;
        *(uint32_t *)0x4cb4e0 = 0x82;
        *(uint32_t *)0x4af600 = 0;
        menu_begin(0x4afc80);
        *(uint32_t *)0x4ca2f8 = 0x44dc90;
        item = menu_add(0, -1, 0x5a, 1, 0x33);
        g_item_hide = 1;
        league_name(g_name_buf, U8(g_config, 0x1c), U8(g_config, 0x1d));
        menu_add(0, -1, 0x83, 5, 0x33);
        track_name(g_name_buf, U8(g_config, 0x18));
        menu_add(0, -1, 0x59, 1, 0x33);
        g_item_hide = 1;
        *(uint32_t *)0x4cb584 = 1;
        GCALL(0x44c9d0, menu_add_slider)(GMEM(0x4aff70), 0, GMEM(0x4aff70), 1, OPTION_FN(0x44dd30), 0x33);
        g_item_hide = 1;
        GCALL(0x450460, difficulty_option)(0x33);
        g_item_hide = 1;
        GCALL(0x4501a0, laps_menu_other)(0x33);
        GCALL(0x4480d0, fe_menu_text_box)((void *)(g_config + 0x16c), GMEM(0x4cb0dc), 0x33, 0x44e0b0, 0x44e0e0);
        if (g_with_select)
            menu_select(item);
        menu_add_pair(0, -1, 0, -1, 0x46, 1, 0x33);
        GCALL(0x449fd0, network_lobby_build)();
        break;
    case 0x5a: /* network: waiting for the host */
        if (g_screen_new)
            net_state(5);
        *(uint32_t *)0x4cb524 = 0x452db0;
        *(uint32_t *)0x4cb594 = 1;
        *(uint32_t *)0x4af5f4 = 0x1a4;
        *(uint32_t *)0x4cb4dc = 0x1e;
        *(uint32_t *)0x4af5fc = 0;
        *(uint32_t *)0x4cb4e0 = 0x82;
        *(uint32_t *)0x4af600 = 0;
        menu_begin(0x4afc64);
        menu_add(0, -1, 0x87, 2, 0x33);
        text_lookup(text, 0x200, 0, -1);
        text_block(text, 0, 1, 0x1e, g_text_y, 0, 1, 0x4aca1c, -1);
        text_lookup(text, 0x200, 0, -1);
        text_block(text, 0, 1, 0x1e, g_text_y + 0x28, 0, 1, 0x4aca1c, -1);
        GCALL(0x449fd0, network_lobby_build)();
        break;
    case 0x5c:
        next = G32(0x4cb558) ? U8(g_config, 0x89) : 0x5d;
        GCALL(0x44f000, records_menu_track)(U8(g_race, 0x579), next, 1);
        break;
    case 0x5d:
        cfg = g_config;
        GCALL(0x44f5a0, game_points_menu)(U8(cfg, 0x89), 1, U8(cfg, 0x84), 1);
        break;
    case 0x5e: {
        uint8_t *race;

        cfg = g_config;
        race = g_race;
        GCALL(0x448d70, deathmatch_report)(U8(race, 0x579), U8(race, 0x54d), U8(cfg, 0x89), 1);
        break;
    }
    case 0x60: /* records */
        GCALL(0x44e5b0, worlds_menu_plain)(0x61, 0x62, 0x63, 0x64);
        menu_add(0x4afc4c, 0, 0x65, 1, 1);
        break;
    case 0x61:
        GCALL(0x44ebd0, fe_message)(0x87, 2);
        break;
    case 0x62:
        GCALL(0x44efa0, records_menu_world2)(0x87, 2);
        break;
    case 0x63:
        GCALL(0x44efc0, menu_go_3)(0x87, 2);
        break;
    case 0x64:
        GCALL(0x44efe0, menu_go_4)(0x87, 2);
        break;
    case 0x65: /* records: reset? */
        menu_begin(0x4afc4c);
        item = menu_add(0x4afc30, 0, 0x60, 1, 1);
        *(uint32_t *)0x4ca300 = 0x44ffa0;
        menu_add(0x4afc30, 1, 0x60, 1, 1);
        menu_select(item);
        break;
    case 0x66: /* options */
        menu_begin(0x4afc0c);
        menu_add(0, -1, 0x67, 1, 5);
        menu_add(0, -1, 0x6c, 1, 0x36);
#ifdef REROLL_NATIVE
        menu_add(P("Display"), -3, DISPLAY_SCREEN, 1, 1); /* (a literal text: the section's numbering goes on) */
#endif
        menu_add(0, -1, 0x6e, 1, 0x34);
        menu_add(0, -1, 0x74, 1, 0x38);
        menu_add(0, -1, 0x6f, 1, 1);
        break;
    case 0x67: /* options: controls */
        menu_begin(0x4afbf0);
        menu_add(0, -1, 0x68, 1, 5);
        menu_add(0, -1, 0x69, 1, 5);
        menu_add(0, -1, 0x6a, 1, 5);
        menu_add(0, -1, 0x6b, 1, 5);
        break;
    case 0x68:
    case 0x69:
    case 0x6a:
    case 0x6b: /* one player's controls */
        XCALL(0x451c00, controls_menu_build, void, (int32_t, uint32_t, uint32_t))(g_screen - 0x68, 0x87, 2);
        break;
    case 0x6c:
        GCALL(0x451110, sound_menu_build)();
        break;
    case 0x6d:
        GCALL(0x451800, audio_detect_menu)();
        break;
    case 0x6e: /* options: graphics (the slider ranges are floats) */
        menu_begin(0x4afbbc);
        GCALL(0x44cb10, menu_slider_begin)(0, -1, 0x44800000, 0x47800000, U32(g_config, 0x64), 0x42000000, 0x41800000, 0x44ce70, 1);
        GCALL(0x44cb10, menu_slider_begin)(0, -1, 0x3f800000, 0x41200000, U32(g_config, 0x68), 0x42000000, 0x41800000, 0x44ce80, 1);
        g_item_off = 1;
        GCALL(0x44cb10, menu_slider_begin)(0, -1, 0x41200000, 0x42c80000, U32(g_config, 0x70), 0x42000000, 0x41800000, 0x44cea0, 1);
        GCALL(0x44cb10, menu_slider_begin)(0, -1, 0x41c00000, 0x42e00000, U32(g_config, 0x6c), 0x42000000, 0x41800000, 0x44ce90, 1);
        GCALL(0x44c9d0, menu_add_slider)(0, -1, 0, -2, OPTION_FN(0x450ae0), 1);
        GCALL(0x450930, fe_option_bilinear)();
#ifdef REROLL_NATIVE
        GCALL(0x44c9d0, menu_add_slider)((void *)("Anisotropic Filtering ~>~P0~S"), -3, (void *)("Anisotropic Filtering ~>~P0~S"), -3, anisotropy_option, 1);
#endif
        GCALL(0x4509a0, fe_option_dithering)();
        GCALL(0x450ef0, mirror_option)();
        GCALL(0x450ba0, option_lov_add)();
        GCALL(0x450560, fe_option_reflections)();
        GCALL(0x4506e0, option_water_reflections_add)();
        GCALL(0x4505c0, fe_option_wormhole_trails)();
        GCALL(0x450680, option_shield_texture_add)();
        GCALL(0x450620, timewarp_texture_option)();
        GCALL(0x450870, option_ice_sheet_add)();
        GCALL(0x450740, skidmarks_option)();
        GCALL(0x4508d0, option_speed_glow_add)();
        GCALL(0x450a10, fe_option_clouds)();
        GCALL(0x450810, option_rubble_add)();
        break;
#ifdef REROLL_NATIVE
    case DISPLAY_SCREEN:
        display_menu();
        break;
#endif
    case 0x6f: /* options: setup */
        menu_begin(0x4afbd8);
        GCALL(0x450f50, weather_option)();
        GCALL(0x451040, fe_option_daytime)();
        GCALL(0x44fed0, option_meteors_add)();
        GCALL(0x450e30, weapons_option)();
        GCALL(0x450e90, option_catch_up_add)();
        break;
    case 0x70: /* debug */
        menu_begin(0x4afbac);
        GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, OPTION_FN(0x44d4a0), -1);
        GCALL(0x44c9d0, menu_add_slider)(0, -1, 0, -2, OPTION_FN(0x44cfc0), -1);
        GCALL(0x44c9d0, menu_add_slider)(0, -1, 0, -2, OPTION_FN(0x44d170), -1);
        *(uint32_t *)0x4ca2f8 = 0x44d290;
        GCALL(0x44c9d0, menu_add_slider)(0, -1, 0, -2, OPTION_FN(0x44d2f0), -1);
        GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, OPTION_FN(0x44d3e0), -1);
        GCALL(0x44c950, menu_add_on_off)(0, -1, OPTION_FN(0x44d420), -1);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d460, -1);
        GCALL(0x44c980, menu_add_label)(GMEM(0x4cb7f8), -3);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d4e0, -1);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d540, -1);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d590, -1);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d5e0, -1);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d640, -1);
        GCALL(0x44c980, menu_add_label)(GMEM(0x4cb7f8), -3);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d6a0, -1);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d700, -1);
        GCALL(0x44c980, menu_add_label)(GMEM(0x4cb7f8), -3);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d760, -1);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d7b0, -1);
        GCALL(0x44c980, menu_add_label)(GMEM(0x4cb7f8), -3);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d810, -1);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d870, -1);
        GCALL(0x44c980, menu_add_label)(GMEM(0x4cb7f8), -3);
        menu_add(0, -1, 0x71, 0, 1);
        GCALL(0x44c820, option_enabled_entry)(0, -1, 0x44d8d0, -1);
        GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, OPTION_FN(0x44d910), -1);
        GCALL(0x44c9d0, menu_add_slider)(0, -1, 0, -2, OPTION_FN(0x44d950), -1);
        GCALL(0x44c9d0, menu_add_slider)(0, -1, 0, -2, OPTION_FN(0x44d130), -1);
        break;
    case 0x73:
        GCALL(0x44e3c0, fe_quit_confirm_menu)();
        break;
    case 0x74: { /* bonus bits: the cheats unlocked (race +0x720/+0x721) */
        menu_begin(0x4afc1c);
        g_item_hide = (uint8_t)~U8(g_race, 0x720) >> 5 & 1;
        GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, OPTION_FN(0x44daa0), 0x38);
        g_item_off = (uint8_t)~U8(g_race, 0x720) >> 7;
        GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, OPTION_FN(0x44dae0), 0x38);
        g_item_off = (uint8_t)~U8(g_race, 0x721) >> 6 & 1;
        GCALL(0x44c6d0, menu_add_choice)(0, -1, 0, -1, OPTION_FN(0x44db20), 0x38);
        g_item_hide = (uint8_t)~U8(g_race, 0x721) & 1;
        menu_add(0, -1, 0x75, 1, 0x38);
        g_item_hide = (uint8_t)~U8(g_race, 0x721) >> 1 & 1;
        menu_add(0, -1, 0x76, 1, 0x38);
        g_item_hide = (uint8_t)~U8(g_race, 0x721) >> 2 & 1;
        menu_add(0, -1, 0x77, 1, 0x38);
        g_item_hide = (uint8_t)~U8(g_race, 0x721) >> 3 & 1;
        menu_add(0, -1, 0x78, 1, 0x38);
        menu_add(0, -1, 0x87, 2, 1);
        break;
    }
    case 0x75: /* the bonus leagues */
        U8(g_config, 0x1d) = 0xf;
        GCALL(0x4518a0, multiplayer_menu)();
        break;
    case 0x76:
        U8(g_config, 0x1d) = 0x11;
        GCALL(0x4518a0, multiplayer_menu)();
        break;
    case 0x77:
        U8(g_config, 0x1d) = 0x10;
        GCALL(0x4518a0, multiplayer_menu)();
        break;
    case 0x78:
        U8(g_config, 0x1d) = 0x12;
        GCALL(0x4518a0, multiplayer_menu)();
        break;
    case 0x7e:
        GCALL(0x44e640, worlds_menu)(0x87, 2);
        break;
    case 0x80:
        GCALL(0x44e470, world_menu)(-1, -1);
        break;
    case 0x81:
        GCALL(0x44e8d0, track_menu_build)();
        break;
    }
    cfg = g_config;
    *(uint8_t *)0x4cb588 = U8(cfg, g_screen + 0x9cc);
    *(uint8_t *)0x4cb589 = U8(cfg, g_screen + 0xacc);
    *(uint32_t *)0x4cb598 = 0;
    GCALL(0x44cc50, menu_measure_all)();
}

/* 0x4302d0: one front-end frame: go to the next screen when the fade allows
 * (0x4af650), fade in, rebuild the menu for a new screen, then the menu,
 * the backdrop, the attract-mode demo countdown and the screen's panels.
 * Returns 0 to quit (screen 0x72). */
int32_t GAMEAPI frontend_frame(void)
{
    uint8_t shade[4] = {0};
    uint32_t colour, t, s, attract, level;
    int32_t left, top, mid, low, wobble, x0, x1;

    (*(uint32_t *)0x4be324)++;
    *(uint32_t *)0x4cb590 = 0;
    U8(g_config, 0x82) = 0;
    GCALL(0x457e90, flame_update)();
    if (g_screen_next != -1 && (*(int32_t *)0x4af64c == -1 || (g_fade == 0x100 && g_fade_dir == 2))) {
        switch (g_screen_next) {
        case 6:
            GCALL(0x4329b0, front_mode_start)(1);
            break;
        case 9:
            GCALL(0x4329b0, front_mode_start)(9);
            break;
        case 0xb:
            GCALL(0x4329b0, front_mode_start)(10);
            break;
        case 0x16:
            U8(g_config, 0x1d)++;
            /* fall through */
        case 0x11:
            GCALL(0x4329b0, front_mode_start)(2);
            break;
        case 0x1f:
            GCALL(0x4329b0, front_mode_start)(3);
            break;
        case 0x28:
            GCALL(0x4329b0, front_mode_start)(4);
            break;
        case 0x3c:
            GCALL(0x4329b0, front_mode_start)(5);
            break;
        case 0x53:
            U8(g_config, 0x82) = 1;
            U8(g_config, 0x81) = 1;
            GCALL(0x4329b0, front_mode_start)(7);
            break;
        case 0x5b:
            U8(g_config, 0x82) = 1;
            U8(g_config, 0x81) = 0;
            GCALL(0x4329b0, front_mode_start)(8);
            break;
        case 0x5f:
            GCALL(0x4329b0, front_mode_start)(6);
            break;
        case 0x71:
            GCALL(0x4329b0, front_mode_start)(0);
            break;
        case 0x72:
            return 0;
        default:
            g_screen = g_screen_next;
            g_screen_next = -1;
            g_screen_built = 1;
            g_screen_new = 1;
            g_fade_dir = 1;
            g_fade = 0x100;
            break;
        }
        GCALL(0x448000, attract_timer_start)();
    }
    if (GCALL(0x448070, fe_attract_due)())
        GCALL(0x4329b0, front_mode_start)(0);
    if (g_fade == 0 && g_fade_dir == 1) { /* faded in: the menu sounds and the corner sparkles */
        int32_t w, h, ox, oy, y0, y1;

        GCALL(0x472a90, voice_play_plain)(0x12, 0x200, 16000);
        GCALL(0x472a90, voice_play_plain)(0x11, 0x400, 16000);
        g_fade_dir = 0;
        w = *(int32_t *)0x4acc38;
        ox = w * 0x14 / 0x280;
        x0 = w * *(int32_t *)0x4cb474 / 0x280 + ox;
        x1 = *(int32_t *)0x4cb47c * w / 0x280 - ox;
        h = *(int32_t *)0x4acc3c;
        oy = h * 0x14 / 0x1e0;
        y0 = h * *(int32_t *)0x4cb478 / 0x1e0 + oy;
        y1 = *(int32_t *)0x4cb480 * h / 0x1e0 - oy;
        if (GCALL(0x452ff0, menu_settled)()) {
            GCALL(0x462ae0, sparks_spawn)(0x20, x0, y0);
            GCALL(0x462ae0, sparks_spawn)(0x20, x0, y1);
            GCALL(0x462ae0, sparks_spawn)(0x20, x1, y0);
            GCALL(0x462ae0, sparks_spawn)(0x20, x1, y1);
        }
    }
    if (g_screen_built) {
        g_screen_built = 0;
        GCALL(0x42e2a0, text_buffers_alloc)();
        GCALL(0x44a2a0, menu_screen_build)();
    }
    GCALL(0x4368f0, input_focus_poll)();
    GCALL(0x427a70, ot_clear_reverse)(GMEM(0x6cd510), 0x200);
    GCALL(0x427a70, ot_clear_reverse)(GMEM(0x6cdd80), 0x200);
    GCALL(0x428240, filter_set_thunk)();
    GCALL(0x447ed0, menu_back_to_main)();

    /* The backdrop colour, darkened while the attract demo fades in. */
    GCALL(0x432820, colour_fade_apply)((void *)(shade), GMEM(0x4ac99c));
    t = 0xffff - (g_attract_level >> 1);
    s = (t & 0xffffff00) < 0xff00 ? t >> 8 : 0xff;
    shade[0] = (uint8_t)(shade[0] * s >> 8);
    shade[1] = (uint8_t)(shade[1] * s >> 8);
    shade[2] = (uint8_t)(shade[2] * s >> 8);
    GCALL(0x4530d0, menu_tiles_draw)(0x6cd560, g_menu_page, (void *)(shade));
    GCALL(0x432820, colour_fade_apply)((void *)(shade), GMEM(0x4ac99c));

    /* The menu box, swinging in or out with the fade. */
    left = *(int32_t *)0x4be334;
    top = GCALL(0x4328a0, fade_scale)(0x80);
    colour = *(uint32_t *)0x4be338; /* also the 4th byte of 0x446200's colour below */
    mid = *(int32_t *)0x4be334;
    top = (int32_t)colour - top;
    low = mid;
    wobble = GCALL(0x4328a0, fade_scale)(0x40) >> 2;
    wobble *= GCALL(0x4328a0, fade_scale)(0x40);
    switch (*(int32_t *)0x4be32c) {
    case 1:
        if (g_fade_dir == 1) {
            mid += wobble;
            low -= wobble;
        } else {
            mid -= wobble;
            low += wobble;
        }
        break;
    case 2:
        if (g_fade_dir == 1) {
            mid -= wobble;
            low += wobble;
        } else {
            mid += wobble;
            low -= wobble;
        }
        break;
    }
    if ((g_quit_frames < 1 && g_fade > 0) || g_quit_frames == 0) {
        XCALL(0x453220, menu_box_quads_draw, void, (uint32_t, int32_t, int32_t, int32_t))(0x6cdda4, g_menu_page, left, top);
        GCALL(0x445790, menu_layout)(left, top, mid, colour, low, colour);
    }
    if (g_screen == 4 && U8(g_config, 0x11) != 0)
        GCALL(0x4535b0, fe_side_panel_draw)(0x6cd520, g_menu_page, 0, 0);
    if (g_fade == 0) {
        GCALL(0x447f00, menu_hold_clear_thunk)();
        *(uint32_t *)0x4be33c = 0;
        GCALL(0x42de50, menu_return)();
        *(uint32_t *)0x4be33c = 1;
        GCALL(0x42de70, fe_menu_input_poll)();
        *(uint32_t *)0x4be33c = 0;
        GCALL(0x447f10, menu_hold_reset_thunk)();
    }
    GCALL(0x447ef0, fe_menu_action_b)();

    /* Attract mode: after a while on the menus the demo fades in. */
    if (!(*(uint8_t *)0x6ccd84 & 4) && g_attract_on) {
        attract = g_attract_frames;
        t = attract & 0x17ff;
        if ((int32_t)attract > 10 && (int32_t)attract < 0x82) {
            if ((int32_t)GCALL(0x436b7e, timer_ratio)() < 5)
                g_attract_on = 0;
            attract = g_attract_frames;
        }
        if (t > 0x258) {
            level = g_attract_level;
            if (level > 0) {
                level -= 400;
                g_attract_level = level;
            }
            if (level > 0xffff)
                g_attract_level = 0;
        } else if (t > 0x7d) {
            level = g_attract_level;
            if (level < 0xffff) {
                level += 400;
                g_attract_level = level;
            }
            if (level > 0xffff)
                g_attract_level = 0xffff;
        }
        GCALL(0x48d630, menu_water_update)(t < 0x5dc, (int32_t)attract >> 12 & 1);
        if (g_attract_level) {
            GCALL(0x490ec0, grid_highlight_set)((uint32_t)(0x100 - g_fade) * g_attract_level >> 8);
            GCALL(0x490ed0, water_mesh_draw)();
        }
        g_attract_frames++;
    }
    GCALL(0x428470, capture_draw_otag)(0x6cdd0c);
    GCALL(0x428470, capture_draw_otag)(0x6ce57c);
    GCALL(0x427a70, ot_clear_reverse)(GMEM(0x6cd510), 0x200);
    GCALL(0x427a70, ot_clear_reverse)(GMEM(0x6cdd80), 0x200);
    if ((g_quit_frames < 1 && g_fade > 0) || g_quit_frames == 0)
        GCALL(0x455e90, fe_item_model_draw)();
    GCALL(0x456800, fe_second_model_draw)();
    GCALL(0x457020, fe_car_choice_draw)();
    GCALL(0x4579a0, fe_mode_model_draw)();
    GCALL(0x462bb0, sparks_draw)(1);
    GCALL(0x428470, capture_draw_otag)(0x6ce57c);
    GCALL(0x427a70, ot_clear_reverse)(GMEM(0x6cd510), 0x200);
    GCALL(0x427a70, ot_clear_reverse)(GMEM(0x6cdd80), 0x200);
    if ((g_quit_frames < 1 && g_fade > 0) || g_quit_frames == 0) {
        GCALL(0x432820, colour_fade_apply)((void *)(&colour), GMEM(0x4ac99c));
        GCALL(0x446200, menu_text_box)(0x6cdd90, g_menu_page, P(&colour));
        GCALL(0x446300, menu_cursor_bar_draw)(0x6cdd94, 0x6cdd88, g_menu_page, 0x4ac9bc, GMEM(0x4ac9fc), 0x4ac9fc, 0x14, 5);
        GCALL(0x446ea0, menu_slider_bars_draw)(0x6cdd88, g_menu_page);
        XCALL(0x42f3c0, strings_draw, int32_t, (uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))(0x6cdd88, 0x6cdd88, g_menu_page, 0, 0);
    }
    GCALL(0x428470, capture_draw_otag)(0x6cdd0c);
    GCALL(0x428470, capture_draw_otag)(0x6ce57c);
    GCALL(0x472040, sound_mix_jump)();
    if (g_quit_frames > 1)
        g_quit_frames--;
    return 1;
}
