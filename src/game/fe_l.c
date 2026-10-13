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

#define g_race (*(uint8_t **)0x4b1960)
#define g_config (*(uint8_t **)0x4b2198)
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

typedef int32_t(GAMEAPI *fn0)(void);
typedef int32_t(GAMEAPI *fn1)(uint32_t);
typedef int32_t(GAMEAPI *fn2)(uint32_t, uint32_t);
typedef int32_t(GAMEAPI *fn3)(uint32_t, uint32_t, uint32_t);
typedef int32_t(GAMEAPI *fn4)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t(GAMEAPI *fn5)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t(GAMEAPI *fn6)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t(GAMEAPI *fn7)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t(GAMEAPI *fn8)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t(GAMEAPI *fn9)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t);

#define C0(a) ((fn0)(a))()
#define C1(a, x) ((fn1)(a))((uint32_t)(x))
#define C2(a, x, y) ((fn2)(a))((uint32_t)(x), (uint32_t)(y))
#define C3(a, x, y, z) ((fn3)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z))
#define C4(a, x, y, z, w) ((fn4)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z), (uint32_t)(w))
#define C5(a, x, y, z, w, v) ((fn5)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z), (uint32_t)(w), (uint32_t)(v))
#define C6(a, x, y, z, w, v, u)                                                                                   \
    ((fn6)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z), (uint32_t)(w), (uint32_t)(v), (uint32_t)(u))
#define C7(a, x, y, z, w, v, u, t)                                                                                \
    ((fn7)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z), (uint32_t)(w), (uint32_t)(v), (uint32_t)(u),         \
               (uint32_t)(t))
#define C8(a, x, y, z, w, v, u, t, s)                                                                             \
    ((fn8)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z), (uint32_t)(w), (uint32_t)(v), (uint32_t)(u),         \
               (uint32_t)(t), (uint32_t)(s))
#define C9(a, x, y, z, w, v, u, t, s, r)                                                                          \
    ((fn9)(a))((uint32_t)(x), (uint32_t)(y), (uint32_t)(z), (uint32_t)(w), (uint32_t)(v), (uint32_t)(u),         \
               (uint32_t)(t), (uint32_t)(s), (uint32_t)(r))

/* The menu helpers most screens use (other groups, or still original). */
#define menu_begin(title) C2(0x448a00, title, 0)                         /* title string id */
#define menu_add(text, index, target, kind, style) C5(0x448140, text, index, target, kind, style)
#define menu_select(item) C1(0x448b50, item)
#define menu_add_pair(a, b, c, d, target, kind, style) C7(0x448c00, a, b, c, d, target, kind, style)
#define text_lookup(buf, size, section, index) C4(0x4481a0, P(buf), size, section, index)
#define text_block(buf, a, b, x, y, c, d, e, f) C9(0x448a50, P(buf), a, b, x, y, c, d, e, f)
#define track_name(buf, track) C2(0x4484a0, P(buf), track)
#define league_name(buf, league, track) C3(0x4485b0, P(buf), league, track)
#define go_screen(screen, how) C2(0x4479d0, screen, how)
#define net_state(state) C1(0x45f500, state)
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
            C4(0x4979f0, P(value), P("%dx%d"), display_mode_w[display_mode], display_mode_h[display_mode]); /* sprintf */
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
            C3(0x4979f0, P(value), P("%dx"), steps[i]); /* sprintf */
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
    C2(0x448a00, P("Display"), -3);
    *(uint32_t *)0x4af5f4 = 0x1a4; /* a box wide enough for the values (as the network screens) */
    /* labels with the string file's markup (as "Transparency Mode ~>~P0~S"):
     * the value (~P0~) in the right-hand column */
    C6(0x44c9d0, P("Mode ~>~P0~S"), -3, P("Mode ~>~P0~S"), -3, P(display_window_option), 1);
    C6(0x44c9d0, P("Resolution ~>~P0~S"), -3, P("Resolution ~>~P0~S"), -3, P(display_resolution_option), 1);
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
    C0(0x445480);
    *(int32_t *)0x4af664 = -1;
    switch ((uint32_t)g_screen) {
    case 0: /* error message */
        menu_begin(0x4b0064);
        if (I32(g_config, 0x48) < 0x80) {
            text_lookup(text, 0x200, 0x4b0064, I32(g_config, 0x48) + 2);
        } else {
            text_lookup(format, 0x200, 0x4b0064, 1);
            C4(0x4979f0, P(text), P(format), U32(g_config, 0x48), U32(g_config, 0x4c)); /* sprintf */
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
        C2(0x448be0, 0, 1);
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
        C0(0x4447d0);
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
        C2(0x448be0, 0, 1);
        break;
    case 7:
        C2(0x44e470, 8, 1);
        break;
    case 8:
        C2(0x44fac0, 9, 1);
        break;
    case 10:
        C4(0x44f5a0, 4, 1, 0, 0);
        break;
    case 0xc:
        C4(0x451900, 0xd, 1, 1, 0);
        break;
    case 0xd:
        C2(0x44fac0, 0xe, 1);
        break;
    case 0xe: /* league: confirm */
        *(uint32_t *)0x4cb4dc = 0x1e;
        *(uint32_t *)0x4af5fc = 0;
        *(uint32_t *)0x4cb4e0 = 0x82;
        *(uint32_t *)0x4af600 = 0;
        menu_begin(0x4b0010);
        item = menu_add(0, -1, 0x11, 0, 0x31);
        C2(0x44e2a0, g_name_buf, U8(g_config, 0x1c));
        menu_add(0, -1, 0xf, 1, 0x31);
        track_name(g_name_buf, U8(g_config, 0x18));
        menu_add(0, -1, 0x10, 1, 0x31);
        C2(0x450210, 0x31, U8(g_config, 0x40));
        C1(0x44ffb0, 0x31);
        menu_add_pair(0, -1, 0, -1, 4, 1, 0x31);
        menu_select(item);
        break;
    case 0xf:
        C4(0x451900, 0x87, 2, 0, 0);
        break;
    case 0x12:
        C4(0x44f2d0, U8(g_race, 0x579), 0, 0x13, 1);
        break;
    case 0x13:
        next = G32(0x4cb55c) ? 4 : 0x14;
        C3(0x44f000, U8(g_race, 0x579), next, 1);
        break;
    case 0x14:
        C4(0x44f5a0, 0x15, 1, 0, 0);
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
        C4(0x44f860, b, target, a, 0);
        break;
    }
    case 0x17:
    case 0x19:
        C4(0x451900, 0xd, 1, 1, 1);
        break;
    case 0x18: /* league lost */
        menu_begin(0x4afffc);
        text_lookup(text, 0x200, 0, -1);
        text_block(text, 0x80000000, 1, g_text_x, g_text_y - 0x14, 1, 1, 0, G32(0x4ac844));
        C4(0x44e410, 1, 0x19, 1, 0x31);
        break;
    case 0x1a:
        U8(g_config, 0x74) = 0;
        C2(0x44e470, 0x1b, 1);
        break;
    case 0x1b:
        C2(0x44fac0, 0x1c, 1);
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
        C2(0x450210, 0x31, -1);
        C1(0x4500f0, 0x31);
        menu_add_pair(0, -1, 0, -1, 4, 1, 0x31);
        menu_select(item);
        break;
    case 0x1d:
    case 0x26:
    case 0x37:
        C2(0x44e470, 0x87, 2);
        break;
    case 0x20:
        C4(0x44f2d0, U8(g_race, 0x579), 0, 0x21, 1);
        break;
    case 0x21:
        C3(0x44f000, U8(g_race, 0x579), 0x22, 1);
        break;
    case 0x22:
        C4(0x44f5a0, 0x1c, 1, 0, 0);
        break;
    case 0x23:
        U8(g_config, 0x74) = 0;
        C2(0x44e470, 0x24, 1);
        break;
    case 0x24:
        C2(0x44fac0, 0x25, 1);
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
        C4(0x44f2d0, U8(g_race, 0x579), 0, 0x2a, 1);
        break;
    case 0x2a:
        C3(0x44f000, U8(g_race, 0x579), 0x2b, 1);
        break;
    case 0x2b:
        C4(0x4488c0, U8(g_race, 0x579), 0, 0x25, 1);
        break;
    case 0x2c: /* multiplayer */
        g_network = 0;
        U8(g_config, 0x74) = 0;
        U8(g_config, 0x75) = 0;
        menu_begin(0x4affb4);
        if (G32(0x4acc54) != 2)
            g_item_hide = 1;
        menu_add(0, -1, 0x2e, 1, 0x33);
        /* Deliberate change: "Networked" greyed out, no transport yet
         * (as multiplayer_menu, fe_n.c) */
        g_item_hide = 1;
        menu_add(0, -1, 0x46, 1, 0x33);
        break;
    case 0x2d:
        *(uint32_t *)0x4af798 = 1;
        /* fall through */
    case 0x2e:
        for (off = 0x9c0; off < 0x9cc; off += 2)
            *(uint16_t *)(g_config + off) = 0;
        C2(0x44fdb0, g_network ? 0x30 : 0x2f, 1);
        break;
    case 0x2f:
        C2(0x44e470, 0x30, 1);
        break;
    case 0x30:
        C2(0x44fac0, 0x31, 1);
        break;
    case 0x31:
        C2(0x44fd50, U8(g_config, 0x74) == 2 ? 0x35 : 0x32, 1);
        break;
    case 0x32:
        C2(0x44fd70, U8(g_config, 0x74) == 3 ? 0x35 : 0x33, 1);
        break;
    case 0x33:
        C2(0x44fd90, 0x35, 1);
        break;
    case 0x34:
        g_network = 1;
        /* fall through */
    case 0x35: /* split screen: confirm */
        menu_begin(0x4aff8c);
        item = menu_add(0, -1, 0x3c, 0, 0x33);
        C1(0x44e320, P(text));
        menu_add(P(text), -3, 0x36, 1, 0x33);
        if (g_network) {
            text_lookup(text, 0x200, 0, -1);
            C1(0x44e800, P(text));
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
        C6(0x44c9d0, 0x4aff70, 0, 0x4aff70, 1, 0x44de10, 0x33);
        if (!g_network) {
            if (U8(g_config, 0x88) != 0)
                g_item_hide = 1;
            C2(0x450210, 0x33, -1);
            C0(0x450150);
        }
        menu_add_pair(0, -1, 0, -1, 4, 1, 0x33);
        menu_select(item);
        break;
    case 0x36:
        C2(0x44fdb0, 0x87, 2);
        break;
    case 0x39:
        C2(0x44fd50, 0x87, 2);
        break;
    case 0x3a:
        C2(0x44fd70, 0x87, 2);
        break;
    case 0x3b:
        C2(0x44fd90, 0x87, 2);
        break;
    case 0x3d:
        if (C2(0x44f560, 0, 0x3e))
            break;
        /* fall through */
    case 0x3e:
        if (C2(0x44f560, 1, 0x3f))
            break;
        /* fall through */
    case 0x3f:
        if (C2(0x44f560, 2, 0x40))
            break;
        /* fall through */
    case 0x40:
        if (C2(0x44f560, 3, 0x41))
            break;
        /* fall through */
    case 0x41:
        next = G32(0x4cb558) ? 0x35 : 0x42;
        C3(0x44f000, U8(g_race, 0x579), next, 1);
        break;
    case 0x42: {
        uint8_t *race = g_race;

        C5(0x44f030, U8(race, 0x579), U8(g_config, 0x74), U8(race, 0x551) != 7, 0x35, 1);
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
        if (g_screen_new) {
            C0(0x48a6b0);
            *(uint32_t *)0x4cb574 = 10;
            C2(0x45f570, 0x4cb574, 0x4cb16c);
            C0(0x44ce50);
        }
        menu_begin(0x4aff54);
        if (G32(0x4acc54) != 2)
            g_item_hide = 1;
        item = menu_add(0, -1, 0x4b, 1, 0x33);
        menu_add(0, -1, 0x54, 1, 0x33);
        C2(0x44dfd0, 0x83, 5);
        menu_add(0, -1, 0x83, 5, 0x33);
        menu_add(0, -1, 0x49, 1, 0x33);
        C6(0x44c9d0, 0x4aff3c, 0, 0x4aff3c, 1, 0x44dce0, 0x33);
        C6(0x44c6d0, 0, -1, 0, -1, 0x44dea0, 0x33);
        if (U8(g_config, 0x80) != 1)
            *(uint32_t *)0x4ca2f8 = 0x44dc50;
        C6(0x44c9d0, 0x4aff14, 0, 0x4aff14, 1, 0x44db60, 0x33);
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
            C5(0x4480d0, P(g_config + 0xa0), 0x4c2ac0, 0x20, 0x44dfa0, 0);
            break;
        case 1:
            menu_begin(0x4afebc);
            break;
        case 2:
            menu_begin(0x4afe8c);
            C6(0x44c9d0, 0x4afe70, 0, 0x4afe70, 1, 0x450ca0, 1);
            C6(0x44c9d0, 0x4afe50, 0, 0x4afe70, 1, 0x450cf0, 1);
            C6(0x44c9d0, 0x4afe30, 0, 0x4afe70, 1, 0x450d90, 1);
            C6(0x44c9d0, 0x4afe14, 0, 0x4afe70, 1, 0x450d40, 1);
            C6(0x44c9d0, 0x4afdf0, 0, 0x4afe70, 1, 0x450de0, 1);
            break;
        case 3:
            menu_begin(0x4afdc4);
            *(uint32_t *)0x4cb4c0 += 0xc;
            C6(0x44c9d0, 0, -1, 0, -2, 0x44ceb0, 0x33);
            C5(0x4480d0, P(g_config + 0xc0), 0x4ca82c, 0x20, 0x44e1c0, 0);
            break;
        }
        menu_select(C4(0x44e410, 0, 0x87, 2, 0x33));
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
            C7(0x449060, 1, P(g_config + off + base), 0x40, 0x87, 2, edit, 0);
            C4(0x448ec0, P(g_config + off + base), 0x230, 0, G32(0x4ac848));
            menu_add(0x4afd90, -3, 0x87, 2, 0x33);
        }
        *(int32_t *)0x4c9e0c = -100;
        *(uint8_t *)0x4c9e1c = 1;
        C4(0x44e410, 0, 0x87, 2, 0x33);
        break;
    }
    case 0x4b: /* network: start */
        *(uint32_t *)0x4cb594 = 1;
        menu_begin(0x4afd44);
        item = menu_add(0, -1, 0x4c, 1, 0x33);
        C5(0x4480d0, P(g_config + 0x12b), 0x4c2484, 0x41, 0x44e080, 0);
        if (U8(g_config, 0x80) != 0 && U8(g_config, 0x80) != 1)
            g_item_hide = 1;
        C6(0x44c6d0, 0, -1, 0, -1, 0x44dee0, 0x33);
        C6(0x44c6d0, 0, -1, 0, -1, 0x44df20, 0x33);
        C6(0x44c6d0, 0, -1, 0, -1, 0x44df60, 0x33);
        menu_add(0, -1, 0x4a, 1, 0x33);
        C4(0x44e430, 0, 0x87, 2, 0x33);
        menu_select(item);
        break;
    case 0x4c: /* network: create a game */
        if (!g_screen_new) {
            if (!C2(0x48a840, 1, 1)) {
                go_screen(0x46, 1);
            } else {
                cfg = g_config;
                C2(0x48b940, P(cfg + 0x120), P(cfg + 0x12b));
                C1(0x45f790, 1);
                g_screen = (g_network != 0) + 0x4d;
                go_screen(g_screen, 0);
            }
        } else {
            C0(0x452940);
            *(uint32_t *)0x4cb524 = 0x452950;
            *(uint32_t *)0x4cb5a0 = 1;
            *(int32_t *)(*(uint8_t **)0x4cb4b8 + 0x4f4) = -1;
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
        C2(0x44e470, 0x4e, 1);
        break;
    case 0x4e:
        if (g_screen_new)
            net_state(3);
        C2(0x44fac0, 0x4f, 1);
        break;
    case 0x4f: { /* network: host confirm */
        uint32_t league;

        if (g_screen_new) {
            C0(0x48d1c0);
            *(uint32_t *)0x4cb57c = C0(0x43692a);
            C1(0x45f7b0, 1);
            net_state(4);
        }
        cfg = g_config;
        league = U8(cfg, 0x1c);
        if (league != G32(0x4af88c) || U8(cfg, 0x1d) != G32(0x4af890) || U8(cfg, 0x87) != G32(0x4af894) ||
            U8(cfg, 0x8b) != G32(0x4af898) || U8(cfg, 0x85) != G32(0x4af89c)) {
            C5(0x45f710, league, U8(cfg, 0x1d), U8(cfg, 0x87), U8(cfg, 0x8b), U8(cfg, 0x85));
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
            C1(0x44e800, P(text));
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
        C6(0x44c9d0, 0x4aff70, 0, 0x4aff70, 1, 0x44dd30, 0x33);
        if (U8(g_config, 0x87) != 0)
            g_item_hide = 1;
        C1(0x450460, 0x33);
        if (g_network == 1)
            g_item_hide = 1;
        C1(0x4501a0, 0x33);
        C5(0x4480d0, P(g_config + 0x16c), 0x4cb0dc, 0x33, 0x44e0b0, 0x44e0e0);
        if (g_with_select)
            menu_select(item);
        menu_add_pair(0, -1, 0, -1, 0x46, 1, 0x33);
        C0(0x449fd0);
        break;
    }
    case 0x50:
        if (g_screen_new)
            net_state(2);
        C2(0x44e470, 0x87, 2);
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
        C2(0x44fac0, 0x87, 2);
        break;
    case 0x52: /* network: waiting for clients */
        if (g_screen_new) {
            net_state(5);
            C0(0x48d110);
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
        C0(0x449fd0);
        break;
    case 0x54: /* network: finding sessions */
        if (g_screen_new) {
            net_state(1);
            C0(0x452940);
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
        C0(0x44a260);
        break;
    case 0x55: /* network: session list */
        C4(0x42e6d0, P(full), 0x40, 0x4b0064, 0xb);
        C4(0x42e6d0, P(open), 0x40, 0x4b0064, 0xc);
        if (!C4(0x48b780, 0x32, 0x4cb0d8, 0x4c3018, 0x4c98f4)) {
            go_screen(0x46, 1);
            break;
        }
        menu_begin(0x4afca0);
        for (i = 0; i < *(int32_t *)0x4cb0d8; i++) {
            uint32_t flags;
            const char *add = 0;

            C5(0x445340, P(line), ((uint32_t *)0x4c3018)[i], 0x1c2, 1, G32(0x4ac848));
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
        C4(0x44e430, 0, 0x46, 1, 0x33);
        break;
    case 0x56:
        if (!C2(0x48b7e0, G32(0x4af60c), P(g_config + 0x120))) {
            go_screen(0x46, 1);
            break;
        }
        g_screen = 0x57;
        /* fall through */
    case 0x57:
        if (g_screen_new)
            net_state(3);
        C2(0x44fac0, 0x58, 1);
        break;
    case 0x58: /* network: join confirm */
        if (g_screen_new) {
            C1(0x45f7b0, 1);
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
        C6(0x44c9d0, 0x4aff70, 0, 0x4aff70, 1, 0x44dd30, 0x33);
        g_item_hide = 1;
        C1(0x450460, 0x33);
        g_item_hide = 1;
        C1(0x4501a0, 0x33);
        C5(0x4480d0, P(g_config + 0x16c), 0x4cb0dc, 0x33, 0x44e0b0, 0x44e0e0);
        if (g_with_select)
            menu_select(item);
        menu_add_pair(0, -1, 0, -1, 0x46, 1, 0x33);
        C0(0x449fd0);
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
        C0(0x449fd0);
        break;
    case 0x5c:
        next = G32(0x4cb558) ? U8(g_config, 0x89) : 0x5d;
        C3(0x44f000, U8(g_race, 0x579), next, 1);
        break;
    case 0x5d:
        cfg = g_config;
        C4(0x44f5a0, U8(cfg, 0x89), 1, U8(cfg, 0x84), 1);
        break;
    case 0x5e: {
        uint8_t *race;

        cfg = g_config;
        race = g_race;
        C4(0x448d70, U8(race, 0x579), U8(race, 0x54d), U8(cfg, 0x89), 1);
        break;
    }
    case 0x60: /* records */
        C4(0x44e5b0, 0x61, 0x62, 0x63, 0x64);
        menu_add(0x4afc4c, 0, 0x65, 1, 1);
        break;
    case 0x61:
        C2(0x44ebd0, 0x87, 2);
        break;
    case 0x62:
        C2(0x44efa0, 0x87, 2);
        break;
    case 0x63:
        C2(0x44efc0, 0x87, 2);
        break;
    case 0x64:
        C2(0x44efe0, 0x87, 2);
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
        C3(0x451c00, g_screen - 0x68, 0x87, 2);
        break;
    case 0x6c:
        C0(0x451110);
        break;
    case 0x6d:
        C0(0x451800);
        break;
    case 0x6e: /* options: graphics (the slider ranges are floats) */
        menu_begin(0x4afbbc);
        C9(0x44cb10, 0, -1, 0x44800000, 0x47800000, U32(g_config, 0x64), 0x42000000, 0x41800000, 0x44ce70, 1);
        C9(0x44cb10, 0, -1, 0x3f800000, 0x41200000, U32(g_config, 0x68), 0x42000000, 0x41800000, 0x44ce80, 1);
        g_item_off = 1;
        C9(0x44cb10, 0, -1, 0x41200000, 0x42c80000, U32(g_config, 0x70), 0x42000000, 0x41800000, 0x44cea0, 1);
        C9(0x44cb10, 0, -1, 0x41c00000, 0x42e00000, U32(g_config, 0x6c), 0x42000000, 0x41800000, 0x44ce90, 1);
        C6(0x44c9d0, 0, -1, 0, -2, 0x450ae0, 1);
        C0(0x450930);
#ifdef REROLL_NATIVE
        C6(0x44c9d0, P("Anisotropic Filtering ~>~P0~S"), -3, P("Anisotropic Filtering ~>~P0~S"), -3,
           P(anisotropy_option), 1);
#endif
        C0(0x4509a0);
        C0(0x450ef0);
        C0(0x450ba0);
        C0(0x450560);
        C0(0x4506e0);
        C0(0x4505c0);
        C0(0x450680);
        C0(0x450620);
        C0(0x450870);
        C0(0x450740);
        C0(0x4508d0);
        C0(0x450a10);
        C0(0x450810);
        break;
#ifdef REROLL_NATIVE
    case DISPLAY_SCREEN:
        display_menu();
        break;
#endif
    case 0x6f: /* options: setup */
        menu_begin(0x4afbd8);
        C0(0x450f50);
        C0(0x451040);
        C0(0x44fed0);
        C0(0x450e30);
        C0(0x450e90);
        break;
    case 0x70: /* debug */
        menu_begin(0x4afbac);
        C6(0x44c6d0, 0, -1, 0, -1, 0x44d4a0, -1);
        C6(0x44c9d0, 0, -1, 0, -2, 0x44cfc0, -1);
        C6(0x44c9d0, 0, -1, 0, -2, 0x44d170, -1);
        *(uint32_t *)0x4ca2f8 = 0x44d290;
        C6(0x44c9d0, 0, -1, 0, -2, 0x44d2f0, -1);
        C6(0x44c6d0, 0, -1, 0, -1, 0x44d3e0, -1);
        C4(0x44c950, 0, -1, 0x44d420, -1);
        C4(0x44c820, 0, -1, 0x44d460, -1);
        C2(0x44c980, 0x4cb7f8, -3);
        C4(0x44c820, 0, -1, 0x44d4e0, -1);
        C4(0x44c820, 0, -1, 0x44d540, -1);
        C4(0x44c820, 0, -1, 0x44d590, -1);
        C4(0x44c820, 0, -1, 0x44d5e0, -1);
        C4(0x44c820, 0, -1, 0x44d640, -1);
        C2(0x44c980, 0x4cb7f8, -3);
        C4(0x44c820, 0, -1, 0x44d6a0, -1);
        C4(0x44c820, 0, -1, 0x44d700, -1);
        C2(0x44c980, 0x4cb7f8, -3);
        C4(0x44c820, 0, -1, 0x44d760, -1);
        C4(0x44c820, 0, -1, 0x44d7b0, -1);
        C2(0x44c980, 0x4cb7f8, -3);
        C4(0x44c820, 0, -1, 0x44d810, -1);
        C4(0x44c820, 0, -1, 0x44d870, -1);
        C2(0x44c980, 0x4cb7f8, -3);
        menu_add(0, -1, 0x71, 0, 1);
        C4(0x44c820, 0, -1, 0x44d8d0, -1);
        C6(0x44c6d0, 0, -1, 0, -1, 0x44d910, -1);
        C6(0x44c9d0, 0, -1, 0, -2, 0x44d950, -1);
        C6(0x44c9d0, 0, -1, 0, -2, 0x44d130, -1);
        break;
    case 0x73:
        C0(0x44e3c0);
        break;
    case 0x74: { /* bonus bits: the cheats unlocked (race +0x720/+0x721) */
        menu_begin(0x4afc1c);
        g_item_hide = (uint8_t)~U8(g_race, 0x720) >> 5 & 1;
        C6(0x44c6d0, 0, -1, 0, -1, 0x44daa0, 0x38);
        g_item_off = (uint8_t)~U8(g_race, 0x720) >> 7;
        C6(0x44c6d0, 0, -1, 0, -1, 0x44dae0, 0x38);
        g_item_off = (uint8_t)~U8(g_race, 0x721) >> 6 & 1;
        C6(0x44c6d0, 0, -1, 0, -1, 0x44db20, 0x38);
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
        C0(0x4518a0);
        break;
    case 0x76:
        U8(g_config, 0x1d) = 0x11;
        C0(0x4518a0);
        break;
    case 0x77:
        U8(g_config, 0x1d) = 0x10;
        C0(0x4518a0);
        break;
    case 0x78:
        U8(g_config, 0x1d) = 0x12;
        C0(0x4518a0);
        break;
    case 0x7e:
        C2(0x44e640, 0x87, 2);
        break;
    case 0x80:
        C2(0x44e470, -1, -1);
        break;
    case 0x81:
        C0(0x44e8d0);
        break;
    }
    cfg = g_config;
    *(uint8_t *)0x4cb588 = U8(cfg, g_screen + 0x9cc);
    *(uint8_t *)0x4cb589 = U8(cfg, g_screen + 0xacc);
    *(uint32_t *)0x4cb598 = 0;
    C0(0x44cc50);
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
    C0(0x457e90);
    if (g_screen_next != -1 && (*(int32_t *)0x4af64c == -1 || (g_fade == 0x100 && g_fade_dir == 2))) {
        switch (g_screen_next) {
        case 6:
            C1(0x4329b0, 1);
            break;
        case 9:
            C1(0x4329b0, 9);
            break;
        case 0xb:
            C1(0x4329b0, 10);
            break;
        case 0x16:
            U8(g_config, 0x1d)++;
            /* fall through */
        case 0x11:
            C1(0x4329b0, 2);
            break;
        case 0x1f:
            C1(0x4329b0, 3);
            break;
        case 0x28:
            C1(0x4329b0, 4);
            break;
        case 0x3c:
            C1(0x4329b0, 5);
            break;
        case 0x53:
            U8(g_config, 0x82) = 1;
            U8(g_config, 0x81) = 1;
            C1(0x4329b0, 7);
            break;
        case 0x5b:
            U8(g_config, 0x82) = 1;
            U8(g_config, 0x81) = 0;
            C1(0x4329b0, 8);
            break;
        case 0x5f:
            C1(0x4329b0, 6);
            break;
        case 0x71:
            C1(0x4329b0, 0);
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
        C0(0x448000);
    }
    if (C0(0x448070))
        C1(0x4329b0, 0);
    if (g_fade == 0 && g_fade_dir == 1) { /* faded in: the menu sounds and the corner sparkles */
        int32_t w, h, ox, oy, y0, y1;

        C3(0x472a90, 0x12, 0x200, 16000);
        C3(0x472a90, 0x11, 0x400, 16000);
        g_fade_dir = 0;
        w = *(int32_t *)0x4acc38;
        ox = w * 0x14 / 0x280;
        x0 = w * *(int32_t *)0x4cb474 / 0x280 + ox;
        x1 = *(int32_t *)0x4cb47c * w / 0x280 - ox;
        h = *(int32_t *)0x4acc3c;
        oy = h * 0x14 / 0x1e0;
        y0 = h * *(int32_t *)0x4cb478 / 0x1e0 + oy;
        y1 = *(int32_t *)0x4cb480 * h / 0x1e0 - oy;
        if (C0(0x452ff0)) {
            C3(0x462ae0, 0x20, x0, y0);
            C3(0x462ae0, 0x20, x0, y1);
            C3(0x462ae0, 0x20, x1, y0);
            C3(0x462ae0, 0x20, x1, y1);
        }
    }
    if (g_screen_built) {
        g_screen_built = 0;
        C0(0x42e2a0);
        C0(0x44a2a0);
    }
    C0(0x4368f0);
    C2(0x427a70, 0x6cd510, 0x200);
    C2(0x427a70, 0x6cdd80, 0x200);
    C0(0x428240);
    C0(0x447ed0);

    /* The backdrop colour, darkened while the attract demo fades in. */
    C2(0x432820, P(shade), 0x4ac99c);
    t = 0xffff - (g_attract_level >> 1);
    s = (t & 0xffffff00) < 0xff00 ? t >> 8 : 0xff;
    shade[0] = (uint8_t)(shade[0] * s >> 8);
    shade[1] = (uint8_t)(shade[1] * s >> 8);
    shade[2] = (uint8_t)(shade[2] * s >> 8);
    C3(0x4530d0, 0x6cd560, g_menu_page, P(shade));
    C2(0x432820, P(shade), 0x4ac99c);

    /* The menu box, swinging in or out with the fade. */
    left = *(int32_t *)0x4be334;
    top = C1(0x4328a0, 0x80);
    colour = *(uint32_t *)0x4be338; /* also the 4th byte of 0x446200's colour below */
    mid = *(int32_t *)0x4be334;
    top = (int32_t)colour - top;
    low = mid;
    wobble = C1(0x4328a0, 0x40) >> 2;
    wobble *= C1(0x4328a0, 0x40);
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
        C4(0x453220, 0x6cdda4, g_menu_page, left, top);
        C6(0x445790, left, top, mid, colour, low, colour);
    }
    if (g_screen == 4 && U8(g_config, 0x11) != 0)
        C4(0x4535b0, 0x6cd520, g_menu_page, 0, 0);
    if (g_fade == 0) {
        C0(0x447f00);
        *(uint32_t *)0x4be33c = 0;
        C0(0x42de50);
        *(uint32_t *)0x4be33c = 1;
        C0(0x42de70);
        *(uint32_t *)0x4be33c = 0;
        C0(0x447f10);
    }
    C0(0x447ef0);

    /* Attract mode: after a while on the menus the demo fades in. */
    if (!(*(uint8_t *)0x6ccd84 & 4) && g_attract_on) {
        attract = g_attract_frames;
        t = attract & 0x17ff;
        if ((int32_t)attract > 10 && (int32_t)attract < 0x82) {
            if (C0(0x436b7e) < 5)
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
        C2(0x48d630, t < 0x5dc, (int32_t)attract >> 12 & 1);
        if (g_attract_level) {
            C1(0x490ec0, (uint32_t)(0x100 - g_fade) * g_attract_level >> 8);
            C0(0x490ed0);
        }
        g_attract_frames++;
    }
    C1(0x428470, 0x6cdd0c);
    C1(0x428470, 0x6ce57c);
    C2(0x427a70, 0x6cd510, 0x200);
    C2(0x427a70, 0x6cdd80, 0x200);
    if ((g_quit_frames < 1 && g_fade > 0) || g_quit_frames == 0)
        C0(0x455e90);
    C0(0x456800);
    C0(0x457020);
    C0(0x4579a0);
    C1(0x462bb0, 1);
    C1(0x428470, 0x6ce57c);
    C2(0x427a70, 0x6cd510, 0x200);
    C2(0x427a70, 0x6cdd80, 0x200);
    if ((g_quit_frames < 1 && g_fade > 0) || g_quit_frames == 0) {
        C2(0x432820, P(&colour), 0x4ac99c);
        C3(0x446200, 0x6cdd90, g_menu_page, P(&colour));
        C8(0x446300, 0x6cdd94, 0x6cdd88, g_menu_page, 0x4ac9bc, 0x4ac9fc, 0x4ac9fc, 0x14, 5);
        C2(0x446ea0, 0x6cdd88, g_menu_page);
        C5(0x42f3c0, 0x6cdd88, 0x6cdd88, g_menu_page, 0, 0);
    }
    C1(0x428470, 0x6cdd0c);
    C1(0x428470, 0x6ce57c);
    C0(0x472040);
    if (g_quit_frames > 1)
        g_quit_frames--;
    return 1;
}
