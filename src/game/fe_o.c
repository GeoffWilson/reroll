/* Front end (frontend_frame 0x4302d0), group O: the menu input (pads and
 * keys turned into menu actions, with key repeat), the menu action
 * dispatcher (cursor moves, value changes, selecting, going back through
 * the screen history), the menu and history resets, the attract-mode
 * timer, a few screen builders (league points, quit confirm, option rows,
 * labels) and the network player-drop logic.
 *
 * Still original (called by address): the menu sound 0x472a90 / 0x472b70
 * (sound engine), the 3D car preview 0x4579a0 (drawing, parked), the
 * network messages and transport (0x45ddb0, 0x45dde0, 0x45f7b0, 0x45fc40,
 * 0x45fca0, 0x48c790, 0x48ca00, 0x48cdd0), the menu text and layout
 * helpers (0x448140, 0x448a00, 0x448a50, 0x448af0, 0x448b10, 0x448b50,
 * 0x448ec0, 0x449060, 0x4481a0, 0x4484a0, 0x44c6d0, 0x44c9d0), the
 * string formatter 0x42e6d0, the CRT sprintf 0x4979f0 and the clock
 * 0x43692a (timeGetTime). */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define g_race GP(0x4b1960)
#define g_config GP(0x4b2198)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274

/* the current menu: 0x730-byte items (+0 kind, +2 selectable, +7 state,
 * +0x4ec/+0x4f0 target screen and mode, +0x4f8 value, +0x500.. handlers) */
#define g_items GP(0x4cb4b8)
#define g_item_count (*(int32_t *)0x4cb49c)
#define g_cursor (*(int8_t *)0x4cb588)
#define g_column (*(int8_t *)0x4cb589)
#define ITEM_SIZE 0x730
#define g_menu_input HANDLER(fe_handler1, 0x4cb524) /* menu-wide action handler */
#define g_menu_select HANDLER(fe_handler1, 0x4cb52c)  /* only ever 0 */
#define g_menu_moved HANDLER(fe_handler2, 0x4cb530)
#define g_menu_frozen (*(uint32_t *)0x4cb498)
#define g_menu_wrap (*(uint32_t *)0x4cb4a4)
#define g_menu_sideways (*(uint32_t *)0x4cb4fc) /* items laid out left to right */
#define g_menu_replaying (*(uint32_t *)0x4cb4b0) /* inside menu_back's select */
#define g_menu_changed (*(uint32_t *)0x4cb4ac)
#define g_menu_redraw (*(uint32_t *)0x4cb58c)
#define g_menu_redraw_on_move (*(uint32_t *)0x4cb594)
#define g_menu_highlight (*(uint32_t *)0x4c99bc)
#define g_back_item (*(int32_t *)0x4af5f0)

/* screens */
#define g_screen (*(int32_t *)0x4af64c)
#define g_next_screen (*(int32_t *)0x4af650)
#define g_fade_kind (*(uint32_t *)0x4be32c)
#define g_fade_state (*(uint32_t *)0x4be330)
#define g_history_count (*(int32_t *)0x4cb4a0)
#define g_history ((uint8_t *)0x4c98e4) /* 16 screens */
#define SCREENS 0x82

/* input */
#define g_idle (*(int32_t *)0x4ac73c)
#define g_idle_until (*(uint32_t *)0x4be2c0)
#define g_attract_at (*(uint32_t *)0x4cb534)
#define g_quitting (*(uint32_t *)0x4be450)
#define g_bind_count (*(int32_t *)0x4af878)
#define g_bind_actions ((const uint32_t *)0x4af7a0) /* 8 bytes each */
#define g_joy_count (*(int32_t *)0x4d9554)
#define JOY_SIZE 0x43c /* per pad, from 0x4d75e8 */
#define g_axis_state ((int8_t *)0x4d96d8) /* 0x90 per pad */
#define g_button_state ((uint8_t *)0x4d9658) /* 0x90 per pad */

/* network */
#define g_net_flags (*(uint32_t *)0x5d622c)
#define g_net_local (*(uint32_t *)0x5d535c)
#define NET_PLAYER(i) ((uint8_t *)0x5d2cd0 + (i) * 0x66c)
#define g_replay_state (*(uint32_t *)0x6b8d60)

#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define I32(p, off) (*(int32_t *)((p) + (off)))

/* Handlers in game memory (32-bit code addresses): item +0x500 (action),
 * +0x508 (selected, given the value) and the menu-wide 0x4cb524 / 0x4cb52c
 * take an action or value; item +0x504 (cursor moved) and 0x4cb530 take
 * the cursor and value; item +0x50c / +0x510 are the value and on/off
 * items' callbacks (menu_add_choice / menu_add_slider / menu_add_toggle). */
typedef int32_t(GAMEAPI *fe_handler1)(int32_t a);
typedef int32_t(GAMEAPI *fe_handler2)(int32_t a, int32_t b);
typedef int32_t(GAMEAPI *fe_callback)(int32_t command, char *text);
#define HANDLER(T, a) ((T)(uintptr_t)*(const uint32_t *)(uintptr_t)(a))

#define game_time GCALL(0x43692a, game_time_get)
#define binding_get GCALL(0x449bf0, binding_get)
#define joy_count GCALL(0x48a010, joy_device_count_thunk)
#define menu_sound GCALL(0x472a90, voice_play_plain)
#define text_format GCALL(0x42e6d0, text_line_get) /* localised string with a number */
#define text_format2 GCALL(0x4481a0, menu_text_get)
#define text_number GCALL(0x4484a0, driver_name)
#define crt_sprintf GCALL(0x4979f0, msvc_sprintf)
#define menu_title GCALL(0x448a00, menu_set_title)
#define menu_text GCALL(0x448140, menu_add_item)
#define menu_row GCALL(0x448a50, menu_text_add)
#define menu_buttons GCALL(0x448af0, menu_back_footer)
#define menu_buttons_n GCALL(0x448b10, menu_back_entry)
#define menu_default GCALL(0x448b50, menu_entry_select)
#define menu_text_wrap GCALL(0x449060, name_entry_setup)
#define menu_text_colour GCALL(0x448ec0, name_entry_text)
#define menu_option_row GCALL(0x44c6d0, menu_add_choice)
#define menu_option_row2 GCALL(0x44c9d0, menu_add_slider)
#define menu_message GCALL(0x44ebf0, records_menu)
#define item_reset GCALL(0x4455c0, menu_row_reset)
#define items_reset_more GCALL(0x4456e0, menu_extra_row_reset)
#define panel_reset GCALL(0x445650, menu_item_reset)
#define item_cycle GCALL(0x447d80, menu_slider_step)
#define net_send_flag GCALL(0x45ddb0, netplay_send_byte)
#define net_quit_ack GCALL(0x48bed0, net_player_remove)
#define net_is_game GCALL(0x45ef80, net_state_game)
#define net_is_host GCALL(0x45eed0, net_state_host)
#define net_results_due GCALL(0x45ef30, net_state_bit5)
#define race_results GCALL(0x439a70, race_results_update)
#define net_send_results GCALL(0x45fc40, net_finish_send)
#define replay_mark GCALL(0x41c160, replay_finish_mark)
#define replay_step_car GCALL(0x41ba20, replay_step)
#define race_fade GCALL(0x40afb0, race_end_fade_set)
#define race_fade_voice GCALL(0x40a9a0, race_end_signal)
/* group O functions with a verify point are called through their hooks */
#define select_pressed_hooked GCALL(0x449ee0, fe_menu_select_pressed)
#define back_pressed_hooked GCALL(0x449f80, fe_menu_back_pressed)
#define cursor_move_hooked GCALL(0x447b60, fe_menu_cursor_move)
#define history_push_hooked GCALL(0x447a60, fe_history_push)

#define SOUND_MOVE() menu_sound(0x20, 0x400, 14000)
#define SOUND_SCREEN() menu_sound(0x1b, 0x800, 14000)

int32_t GAMEAPI fe_menu_action(int32_t action);

/* forward copy as rep movsd / rep movsb */
static void copy_forward(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t k;

    for (k = n >> 2; k; k--, dst += 4, src += 4)
        *(uint32_t *)dst = *(const uint32_t *)src;
    for (k = n & 3; k; k--)
        *dst++ = *src++;
}

/* strcpy as repne scasb + rep movs */
static void copy_string(char *dst, const char *src)
{
    uint32_t n = 0;

    while (src[n])
        n++;
    copy_forward((uint8_t *)dst, (const uint8_t *)src, n + 1);
}

/* 0x436935: the clock (timeGetTime). */
uint32_t GAMEAPI fe_time(void)
{
    return (uint32_t)game_time();
}

/* --- pads ------------------------------------------------------------ */

/* 0x489890: a pad axis (-0x80..0x7f-ish), 0 when absent. */
int32_t GAMEAPI fe_joy_axis(int32_t pad, int32_t axis)
{
    uint32_t index;

    if (pad >= g_joy_count)
        return 0;
    index = ((uint32_t)pad * 0x10f + (uint32_t)axis) * 4;
    if (!*(uint32_t *)(uintptr_t)(0x4d75f8 + index))
        return 0;
    return *(int32_t *)(uintptr_t)(0x4d76a0 + *(uint32_t *)(uintptr_t)(0x4d7628 + index) + (uint32_t)pad * JOY_SIZE);
}

/* 0x48a020: 0 for a pad that exists (at most 8), else -1. */
int32_t GAMEAPI fe_joy_valid(int32_t pad)
{
    if (pad >= 0 && pad < joy_count() && pad < 8)
        return 0;
    return -1;
}

/* 0x48a040: a pad's button count. */
int32_t GAMEAPI fe_joy_button_count(int32_t pad)
{
    if (fe_joy_valid(pad) == -1)
        return 0;
    return *(int32_t *)(uintptr_t)(0x4d75e8 + (uint32_t)pad * JOY_SIZE);
}

/* 0x48a070: 0 for a button the pad has (at most 0x81), else -1. */
int32_t GAMEAPI fe_joy_button_valid(int32_t pad, int32_t button)
{
    if (button >= 0 && button < fe_joy_button_count(pad) && (uint32_t)button <= 0x80)
        return 0;
    return -1;
}

/* 0x48a0a0: 1 while a button is held. */
int32_t GAMEAPI fe_joy_button_down(int32_t pad, int32_t button)
{
    if (fe_joy_button_valid(pad, button) == -1)
        return 0;
    return *(uint8_t *)(uintptr_t)(0x4d76d0 + (uint32_t)button + (uint32_t)pad * JOY_SIZE) != 0;
}

/* 0x48a170: an axis as a menu direction, remembering the last one: 3 / 0
 * pushed negative (first frame / held), 5 / 2 positive, 4 / 1 centred;
 * -1 for no pad. */
int32_t GAMEAPI fe_joy_axis_edge(int32_t pad, int32_t axis)
{
    int8_t *state, last, now;
    int32_t v;

    if (fe_joy_valid(pad) == -1)
        return -1;
    state = g_axis_state + (uint32_t)pad * 0x90 + (uint32_t)axis;
    last = *state;
    v = fe_joy_axis(pad, axis);
    if (v <= -0x40 || v >= 0x40) {
        if (v < 0)
            now = last == -1 || last == 0 || last == 3 ? 3 : 0;
        else
            now = last == -1 || last == 2 || last == 5 ? 5 : 2;
    } else {
        now = last == -1 || last == 1 || last == 4 ? 4 : 1;
    }
    *state = now;
    return now;
}

/* 0x48a250 / 0x48a260: the x and y axes. */
int32_t GAMEAPI fe_joy_x_edge(int32_t pad)
{
    return fe_joy_axis_edge(pad, 0);
}

int32_t GAMEAPI fe_joy_y_edge(int32_t pad)
{
    return fe_joy_axis_edge(pad, 1);
}

/* 0x48a270: a button as 1 pressed (first frame), 3 held, 0 released, 2
 * up; -1 for no such button. */
int32_t GAMEAPI fe_joy_button_edge(int32_t pad, int32_t button)
{
    uint8_t *state, last;

    if (fe_joy_button_valid(pad, button) == -1)
        return -1;
    state = g_button_state + (uint32_t)pad * 0x90 + (uint32_t)button;
    last = *state;
    if (fe_joy_button_down(pad, button)) {
        if (last)
            return 3;
        *state = 1;
        return 1;
    }
    if (!last)
        return 2;
    *state = 0;
    return 0;
}

/* --- menu input ------------------------------------------------------ */

/* 0x449e10: the pad a player's menu bindings use (config +0x1a1 per
 * player), 0xff when none of the bound actions is a pad button or axis.
 * Only the low byte matters; the rest is what the original leaves in eax. */
uint32_t GAMEAPI fe_menu_pad(int32_t player)
{
    int32_t i, n = g_bind_count;

    for (i = 0; i < n;) {
        uint8_t *binding = binding_get(player, g_bind_actions[i * 2]);

        if (binding) {
            int32_t kind = I32(binding, 4);

            if (kind >= 0 && (kind <= 1 || kind == 3))
                return U8(g_config, (uint32_t)player * 0x90 + 0x1a1);
        }
        n = g_bind_count;
        i++;
    }
    return (uint32_t)n | 0xff;
}

/* 0x449db0 / 0x449e80: the player's pad x / y axis as a menu action:
 * `negative` when pushed left / up, `positive` right / down, else `none`. */
int32_t GAMEAPI fe_menu_axis_x(int32_t player, int32_t negative, int32_t positive, int32_t none)
{
    uint8_t pad = (uint8_t)fe_menu_pad(player);

    if (pad != 0xff) {
        switch ((uint32_t)fe_joy_x_edge(pad)) {
        case 0:
        case 3:
            return negative;
        case 2:
        case 5:
            return positive;
        }
    }
    return none;
}

int32_t GAMEAPI fe_menu_axis_y(int32_t player, int32_t negative, int32_t positive, int32_t none)
{
    uint8_t pad = (uint8_t)fe_menu_pad(player);

    if (pad != 0xff) {
        switch ((uint32_t)fe_joy_y_edge(pad)) {
        case 0:
        case 3:
            return negative;
        case 2:
        case 5:
            return positive;
        }
    }
    return none;
}

/* 0x449f10: 1 when a pad button bound to `action` (for any of the four
 * players' binding tables) has just been pressed or is held. */
int32_t GAMEAPI fe_menu_action_pressed(int32_t player, uint32_t action)
{
    uint8_t pad = (uint8_t)fe_menu_pad(player);
    int32_t i;

    if (pad == 0xff)
        return 0;
    for (i = 0; i < 4; i++) {
        uint8_t *binding = binding_get(i, action);

        if (binding && !U32(binding, 4)) {
            int32_t r = fe_joy_button_edge(pad, I32(binding, 0xc));

            if (r == 1 || r == 3)
                return 1;
        }
    }
    return 0;
}

/* 0x449ee0: select (actions 2 and 5). */
int32_t GAMEAPI fe_menu_select_pressed(int32_t player)
{
    if (fe_menu_action_pressed(player, 2))
        return 1;
    return fe_menu_action_pressed(player, 5) != 0;
}

/* 0x449f80: back (actions 3, 4 and 6). */
int32_t GAMEAPI fe_menu_back_pressed(int32_t player)
{
    if (fe_menu_action_pressed(player, 3))
        return 1;
    if (fe_menu_action_pressed(player, 4))
        return 1;
    return fe_menu_action_pressed(player, 6) != 0;
}

/* 0x42de70: once per front-end frame, player 1's pad as menu actions
 * (0/1 up/down, 2/3 left/right, 7 select, 9 back). Any action keeps the
 * menu awake (0x4ac73c = 0) for a second. */
void GAMEAPI fe_menu_input_poll(void)
{
    int32_t moved_x = 0, moved_y = 0, selected = 0, backed = 0, action;

    if (!U8(g_config, 8))
        return;
    if (g_idle == -1 || g_idle == 0) {
        action = fe_menu_axis_x(0, 2, 3, -1);
        if (action != -1) {
            fe_menu_action(action);
            moved_x = 1;
        }
        action = fe_menu_axis_y(0, 0, 1, -1);
        if (action != -1) {
            fe_menu_action(action);
            moved_y = 1;
        }
        if (select_pressed_hooked(0)) {
            fe_menu_action(7);
            selected = 1;
        }
        if (back_pressed_hooked(0)) {
            fe_menu_action(9);
            backed = 1;
        }
        if (moved_x || moved_y || selected || backed) {
            g_idle = 0;
            g_idle_until = fe_time() + 1000;
        }
    }
    if (fe_time() > g_idle_until)
        g_idle = -1;
}

/* --- menus ----------------------------------------------------------- */

/* 0x4447d0: the screen history emptied. */
void GAMEAPI fe_history_clear(void)
{
    g_history_count = 0;
    *(int32_t *)0x4c98e4 = -1;
    *(int32_t *)0x4c98e8 = -1;
    *(int32_t *)0x4c98ec = -1;
    *(int32_t *)0x4c98f0 = -1;
}

/* 0x445580: one menu item reset (unless the menu is frozen). */
void GAMEAPI fe_menu_item_clear(int32_t index)
{
    if (!g_menu_frozen && index >= 0 && index < 0x40)
        item_reset((uint8_t *)(uintptr_t)((uint32_t)index * ITEM_SIZE + (uint32_t)(uintptr_t)g_items));
}

/* 0x445560: all 64 items reset. */
void GAMEAPI fe_menu_items_clear(void)
{
    int32_t i;

    for (i = 0; i < 0x40; i++)
        fe_menu_item_clear(i);
}

/* 0x445730: the 0x3e text panels at 0x4c30e0 reset. */
void GAMEAPI fe_menu_panels_reset(void)
{
    uint32_t panel;

    for (panel = 0x4c30e0; (int32_t)panel < 0x4c98e0; panel += 0x1a0)
        panel_reset((uint8_t *)(uintptr_t)panel, 2, 1);
}

/* 0x445760: the title panel reset. */
void GAMEAPI fe_menu_title_reset(void)
{
    panel_reset((uint8_t *)0x4c2920, 3, 0);
    *(uint32_t *)0x4c2ab4 = *(uint32_t *)0x4ac844;
    *(uint32_t *)0x4c2ab0 = 0x4aca5c;
}

/* 0x445480: a new menu: layout, handlers, items and panels reset. */
void GAMEAPI fe_menu_reset(void)
{
    *(uint32_t *)0x4cb4dc = *(uint32_t *)0x4ac988;
    *(uint32_t *)0x4af5fc = 1;
    *(uint32_t *)0x4af600 = 1;
    *(uint32_t *)0x4cb4e0 = *(uint32_t *)0x4ac98c + 0x3c;
    *(uint32_t *)0x4cb4d4 = 0;
    *(uint32_t *)0x4cb4d8 = 0;
    *(int32_t *)0x4af5f4 = -1;
    *(int32_t *)0x4af5f8 = -1;
    *(uint32_t *)0x4cb4e4 = 0;
    *(uint32_t *)0x4cb4e8 = 0;
    *(uint32_t *)0x4cb4f4 = 0;
    *(uint32_t *)0x4cb4f8 = 0;
    g_menu_sideways = 0;
    *(int32_t *)0x4af604 = -1;
    *(int32_t *)0x4af608 = -1;
    *(uint32_t *)0x4cb508 = 0;
    g_item_count = 0;
    *(uint32_t *)0x4cb4a8 = 0;
    *(uint32_t *)0x4cb4b4 = 0;
    *(uint32_t *)0x4cb524 = 0;
    *(uint32_t *)0x4cb528 = 0;
    *(uint32_t *)0x4cb52c = 0;
    *(uint32_t *)0x4cb530 = 0;
    *(uint32_t *)0x4cb5a0 = 0;
    g_back_item = -1;
    g_menu_replaying = 0;
    *(uint32_t *)0x4cb504 = 0;
    g_menu_redraw_on_move = 0;
    fe_menu_items_clear();
    items_reset_more();
    fe_menu_title_reset();
    *(uint32_t *)0x4cb550 = 0;
    fe_menu_panels_reset();
}

/* 0x447540: key repeat: fires on the first call after a release (timer
 * +0 is cleared each frame elsewhere), then after `first` ms, then every
 * `next` ms. Timer: +0 seen this frame, +4 running, +8 last fire, +c wait. */
int32_t GAMEAPI fe_repeat(uint32_t *timer, int32_t enable, uint32_t first, uint32_t next)
{
    uint32_t now, last;

    if (timer[0])
        return 0;
    timer[0] = 1;
    if (!enable) {
        first = 0;
        next = 0;
    }
    now = fe_time();
    if (!timer[1]) {
        timer[3] = first;
        timer[2] = now;
        timer[1] = 1;
        return 1;
    }
    last = timer[2];
    if (!last || !now || now < last || !timer[3]) {
        timer[2] = now;
        timer[3] = first;
        return 0;
    }
    if (now - last > timer[3]) {
        timer[2] = now;
        timer[3] = next;
        return 1;
    }
    return 0;
}

/* 0x4475d0, 0x4475f0, 0x447610, 0x447630: the up/down, select, left/right
 * and page timers. */
int32_t GAMEAPI fe_repeat_updown(int32_t enable)
{
    return fe_repeat((uint32_t *)0x4cb5a8, enable, 500, 100);
}

int32_t GAMEAPI fe_repeat_select(int32_t enable)
{
    return fe_repeat((uint32_t *)0x4cb5b8, enable, 1500, 300);
}

int32_t GAMEAPI fe_repeat_leftright(int32_t enable)
{
    return fe_repeat((uint32_t *)0x4cb5c8, enable, 500, 100);
}

int32_t GAMEAPI fe_repeat_page(int32_t enable)
{
    return fe_repeat((uint32_t *)0x4cb5d8, enable, 500, 100);
}

/* 0x447a60: a screen remembered for going back: if `screen` is already in
 * the history it is cut back to there, else `from` is pushed (16 at most). */
void GAMEAPI fe_history_push(int32_t from, uint32_t screen)
{
    int32_t n = g_history_count, i;

    for (i = 0; i < n; i++) {
        if (g_history[i] == screen) {
            g_history_count = i;
            return;
        }
    }
    if (n < 0x10) {
        g_history[n] = (uint8_t)from;
        g_history_count = n + 1;
    }
}

/* 0x447b40: the last screen in the history, or -1. */
int32_t GAMEAPI fe_history_pop(void)
{
    int32_t n = g_history_count;

    if (n <= 0)
        return -1;
    g_history_count = --n;
    return g_history[n];
}

/* 0x4479f0: go to a screen: mode 1 remembers the current one for going
 * back, mode 0 does not, anything else does nothing. */
void GAMEAPI fe_menu_goto_ex(int32_t screen, int32_t mode, int32_t fade)
{
    if (mode) {
        if (mode != 1 || screen < 0 || screen >= SCREENS)
            return;
        history_push_hooked(g_screen, screen);
    }
    g_next_screen = screen;
    g_fade_kind = 2 - (fade != 0);
    g_fade_state = 2;
    SOUND_SCREEN();
}

/* 0x4479d0: go to a screen, fading. */
void GAMEAPI fe_menu_goto(int32_t screen, int32_t mode)
{
    fe_menu_goto_ex(screen, mode, 1);
}

/* 0x447ab0: back: to the last screen in the history, or (none left) by
 * selecting the menu's back item. */
void GAMEAPI fe_menu_back(void)
{
    int32_t screen;

    if (g_history_count <= 0) {
        int8_t cursor;

        if (g_back_item == -1 || g_menu_replaying)
            return;
        cursor = g_cursor;
        g_cursor = (int8_t)g_back_item;
        g_menu_replaying = 1;
        fe_menu_action(7);
        g_cursor = cursor;
        g_menu_replaying = 0;
        return;
    }
    screen = fe_history_pop();
    if (screen < 0 || screen >= SCREENS)
        return;
    g_next_screen = screen;
    g_fade_kind = 2;
    g_fade_state = 2;
    SOUND_SCREEN();
}

/* 0x447b60: the cursor to the previous (up != 0) or next selectable item,
 * wrapping round if the menu wraps. */
void GAMEAPI fe_menu_cursor_move(int32_t up)
{
    int32_t start, i, wrap, count;
    uint8_t *items;

    if ((uint8_t)g_cursor == 0xff)
        return;
    wrap = (int32_t)g_menu_wrap;
    count = g_item_count;
    items = g_items;
    start = i = g_cursor;
    for (;;) {
        if (up)
            i--;
        else
            i++;
        if (!wrap && (i < 0 || i >= count))
            return;
        if (i < 0)
            i = count - 1;
        if (i >= count)
            i = 0;
        if (i == start)
            return;
        if (U8(items, (uint32_t)i * ITEM_SIZE + 2)) {
            g_cursor = (int8_t)i;
            return;
        }
    }
}

/* 0x447cb0: an on/off item flipped: its handler is asked (0x11) for the
 * old state, then given `value`; item +7 becomes 1 (on) or 2 (off). */
void GAMEAPI fe_menu_item_toggle(int32_t value)
{
    fe_callback handler;
    int32_t before, after;

    if (g_cursor < 0 || g_cursor >= g_item_count)
        return;
    handler = HANDLER(fe_callback, g_items + (int32_t)g_cursor * ITEM_SIZE + 0x510);
    if (!handler)
        return;
    before = handler(0x11, 0) != 0;
    after = HANDLER(fe_callback, g_items + (int32_t)g_cursor * ITEM_SIZE + 0x510)(value, 0) != 0;
    U8(g_items, (int32_t)g_cursor * ITEM_SIZE + 7) = (uint8_t)(!after + 1);
    if (after != before) {
        SOUND_MOVE();
        g_menu_changed = 1;
    }
}

/* 0x447c40: a value item stepped by its handler. */
void GAMEAPI fe_menu_item_spin(int32_t value)
{
    fe_callback handler;

    if (g_cursor < 0 || g_cursor >= g_item_count)
        return;
    handler = HANDLER(fe_callback, g_items + (int32_t)g_cursor * ITEM_SIZE + 0x50c);
    if (handler && handler(value, 0)) {
        SOUND_MOVE();
        g_menu_redraw = 1;
        g_menu_changed = 1;
    }
}

/* 0x447bd0: left / right on an item: kind 2 on/off, 3 value, 4 list. */
void GAMEAPI fe_menu_item_adjust(int32_t back, int32_t kind)
{
    if (kind == 2)
        fe_menu_item_toggle(back ? 0xf : 0x10);
    else if (kind == 3)
        fe_menu_item_spin(back ? 0x14 : 0x15);
    else if (kind == 4)
        item_cycle(back ? 0x17 : 0x18);
}

/* 0x447650: a menu action (-1 none, 0/1 up/down, 2/3 left/right, 4/5
 * first/last item, 6/7 select, 8/9 back); the item's and the menu's own
 * handlers get it first. Returns 1 if handled (0 for an unknown action),
 * or what a handler returned. The cursor and column are kept per screen
 * in the config (+0x9cc / +0xacc). */
int32_t GAMEAPI fe_menu_action(int32_t action)
{
    int32_t result, old_cursor, old_column, target = -1, mode = -1, i;
    uint32_t kind = 0;
    int8_t cursor = g_cursor;

    if (cursor >= 0 && cursor < g_item_count) {
        fe_handler1 handler = HANDLER(fe_handler1, g_items + (int32_t)cursor * ITEM_SIZE + 0x500);

        if (handler) {
            result = handler(action);
            if (result)
                goto done;
        }
    }
    if (g_menu_input) {
        result = g_menu_input(action);
        if (result)
            goto done;
    }
    cursor = g_cursor;
    if (cursor >= 0 && cursor < g_item_count) {
        uint8_t *item = g_items + (int32_t)cursor * ITEM_SIZE;

        kind = item[0];
        target = I32(item, 0x4ec);
        mode = I32(item, 0x4f0);
    }
    old_column = g_column;
    old_cursor = cursor;
    result = 1;
    switch (action) {
    case 0:
    case 1:
        if (fe_repeat_updown(1)) {
            if (!g_menu_sideways)
                cursor_move_hooked(action == 0);
            else
                fe_menu_item_adjust(action == 0, (int32_t)kind);
        }
        break;
    case 2:
    case 3:
        if (fe_repeat_leftright(1)) {
            if (!g_menu_sideways)
                fe_menu_item_adjust(action == 2, (int32_t)kind);
            else
                cursor_move_hooked(action == 2);
        }
        break;
    case 4:
    case 5:
        if (fe_repeat_page(1)) {
            int32_t count = g_item_count;
            uint8_t *selectable = g_items + 2;

            for (i = 0; i < count; i++, selectable += ITEM_SIZE) {
                if (*selectable) {
                    g_cursor = (int8_t)i;
                    if (action == 4)
                        break;
                }
            }
        }
        break;
    case 6:
    case 7:
        if (fe_repeat_select(1) || g_menu_replaying) {
            uint8_t *item = g_items + (int32_t)g_cursor * ITEM_SIZE;
            int32_t value = I32(item, 0x4f8);
            fe_handler1 handler = HANDLER(fe_handler1, item + 0x508);

            if (handler)
                handler(value);
            if (g_menu_select)
                g_menu_select(value);
            if (kind != 2 && kind != 3 && kind != 4) {
                if (mode == 2)
                    fe_menu_back();
                else
                    fe_menu_goto(target, mode);
            }
        }
        break;
    case 8:
    case 9:
        if (fe_repeat_select(1))
            fe_menu_back();
        break;
    case -1:
        break;
    default:
        result = 0;
    }
    cursor = g_cursor;
    if (old_cursor != cursor || old_column != g_column) {
        int32_t value = -1;

        if (cursor >= 0 && cursor <= g_item_count) {
            uint8_t *item = g_items + (int32_t)cursor * ITEM_SIZE;
            fe_handler2 handler = HANDLER(fe_handler2, item + 0x504);

            value = I32(item, 0x4f8);
            if (handler)
                handler(cursor, value);
        }
        if (g_menu_moved)
            g_menu_moved(g_cursor, value);
        SOUND_MOVE();
        if (g_menu_redraw_on_move)
            g_menu_redraw = 1;
        g_menu_highlight = 0xffffffff;
    }
done:
    if (g_screen >= 0 && g_screen < 0x100 && !g_menu_replaying) {
        U8(g_config, g_screen + 0x9cc) = (uint8_t)g_cursor;
        U8(g_config, g_screen + 0xacc) = (uint8_t)g_column;
    }
    return result;
}

/* 0x447ef0: action 0xb (unknown to the dispatcher: only handlers see it). */
int32_t GAMEAPI fe_menu_action_b(void)
{
    return fe_menu_action(0xb);
}

/* 0x448030: `scale` times the share of the attract-mode wait left
 * (config +0x12 ms, default 30000). */
int32_t GAMEAPI fe_attract_left(int32_t scale)
{
    uint32_t wait;
    int32_t left;

    if (!U16(g_config, 0x12))
        U16(g_config, 0x12) = 30000;
    left = (int32_t)(g_attract_at - fe_time());
    if (left < 0)
        left = 0;
    wait = U16(g_config, 0x12);
    if (left > (int32_t)wait)
        left = (int32_t)wait;
    return (int32_t)((uint32_t)left * (uint32_t)scale) / (int32_t)wait;
}

/* 0x448070: 1 when the title screen (4) has waited long enough for the
 * attract mode (config +0x10 on). */
int32_t GAMEAPI fe_attract_due(void)
{
    uint32_t now = fe_time();

    if (U8(g_config, 0x10) && g_screen == 4 && g_attract_at && now > g_attract_at && !g_quitting)
        return 1;
    return 0;
}

/* 0x4480d0: a text box: the text copied to `dst`, wrapped and shown. */
void GAMEAPI fe_menu_text_box(const uint8_t *src, uint8_t *dst, uint32_t size, int32_t a, int32_t b)
{
    copy_forward(dst, src, size);
    menu_text_wrap(1, (uint32_t)(uintptr_t)dst, size, 0x83, 5, (uint32_t)a, (uint32_t)b);
    menu_text_colour((const char *)dst, 0x100, 0, *(int32_t *)0x4ac848);
    menu_text(0, -1, 0x83, 5, 0x33);
}

/* --- screens --------------------------------------------------------- */

/* 0x44e320: the split-screen label for the config's player count (+0x74;
 * two players +0x76 side by side). */
void GAMEAPI fe_split_screen_label(char *dst)
{
    char text[0x200];
    int32_t kind = 0; /* the original uses a leftover stack word (dst is never null) */

    if (dst) {
        switch (U8(g_config, 0x74)) {
        case 2:
            kind = (U8(g_config, 0x76) != 0) + 3;
            break;
        case 3:
            kind = 5;
            break;
        case 4:
            kind = 6;
            break;
        default:
            kind = 2;
        }
    }
    text_format((uint8_t *)text, 0x200, (const char *)0x4aff3c, kind); /* PC_MENU_SPLIT_SCREEN */
    copy_string(dst, text);
}

/* 0x44e840: the world label (0xf..0x12 -> PC_MENU_WORLD 1, 3, 2, 4). */
void GAMEAPI fe_world_label(char *dst, int32_t unused, int32_t world)
{
    char text[0x200];
    int32_t n;

    (void)unused;
    switch (world) {
    case 0xf:
        n = 1;
        break;
    case 0x10:
        n = 3;
        break;
    case 0x11:
        n = 2;
        break;
    case 0x12:
        n = 4;
        break;
    default:
        return;
    }
    text_format((uint8_t *)text, 0x200, (const char *)0x4b00e8, n);
    copy_string(dst, text);
}

/* 0x44e800: the world row (config +0x1c / +0x1d) shown. */
void GAMEAPI fe_world_row(int32_t row)
{
    fe_world_label((char *)0x4c9e94, U8(g_config, 0x1c), U8(g_config, 0x1d));
    menu_text((const char *)(uintptr_t)(uint32_t)row, -3, 0x7e, 1, 0x33);
}

/* 0x44e3c0: the quit-with-confirm menu. */
void GAMEAPI fe_quit_confirm_menu(void)
{
    int32_t first;

    menu_title((const char *)0x4cb5e8, -3);
    first = menu_text((const char *)0x4afc30, 0, 0x87, 2, 1);
    menu_text((const char *)0x4afc30, 1, *(uint32_t *)0x4af70c, *(uint32_t *)0x4af710, 1);
    menu_default(first);
}

/* 0x44ebd0: a message box. */
void GAMEAPI fe_message(int32_t a, int32_t b)
{
    menu_message(1, -1, a, b);
}

/* 0x44faa0: three buttons. */
void GAMEAPI fe_menu_buttons3(int32_t a, int32_t b, int32_t c, int32_t d)
{
    menu_buttons_n(a, 3, b, c, d);
}

/* 0x44f860: the league points table: headings, then per finishing place
 * (0x4c24c8) the position, the points (race +0x1c) and the score (race
 * +8), the `highlight` car's row picked out. */
void GAMEAPI fe_league_points(int32_t buttons3, int32_t a, int32_t b, int32_t highlight)
{
    char text[0x200];
    int32_t i, y;

    menu_title((const char *)0x4b0184, 0); /* PC_MENU_LEAGUE_POINTS */
    text_format2(text, 0x200, (const char *)0x4b0184, 1);
    menu_row(text, (uint32_t)0, 1, *(int32_t *)0x4af784, 0xa0, 0, 1, 0x4aca1c, -1);
    text_format2(text, 0x200, (const char *)0x4b0184, 2);
    menu_row(text, (uint32_t)0, 1, *(int32_t *)0x4af788, 0xa0, 0, 1, 0x4aca1c, -1);
    text_format2(text, 0x200, (const char *)0x4b0184, 3);
    menu_row(text, (uint32_t)0, 1, *(int32_t *)0x4af78c, 0xa0, 0, 1, 0x4aca1c, -1);
    for (i = 0, y = 200; i < 6; i++, y += 0x1e) {
        int32_t car = *(int8_t *)(uintptr_t)(0x4c24c8 + i), flags, colour;
        uint32_t points;

        if (car < 0 || car >= 6)
            continue;
        flags = car == highlight ? (int32_t)0x80000000 : 0;
        colour = car == highlight ? 0x4ac9bc : 0;
        text_format2(text, 0x200, (const char *)0x4b0120, i); /* PC_MENU_POSITION */
        menu_row(text, (uint32_t)flags, 1, *(int32_t *)0x4af784, y, 0, 1, colour, -1);
        points = U8(g_race, car + 0x1c);
        if (points < 7) {
            text_number(text, (int32_t)points);
            menu_row(text, (uint32_t)flags, 1, *(int32_t *)0x4af788, y, 0, 1, colour, -1);
        }
        crt_sprintf(text, (const char *)0x4afb5c, U8(g_race, car + 8)); /* "%02d" */
        menu_row(text, (uint32_t)flags | 8, 1, *(int32_t *)0x4af78c, y, 0, 1, colour, -1);
    }
    if (buttons3)
        fe_menu_buttons3(1, a, b, 0x31);
    else
        menu_buttons(1, a, b, 0x31);
    *(uint32_t *)0x4cb524 = 0x448d60;
}

/* 0x450560, 0x4505c0, 0x450930, 0x4509a0: on/off option rows
 * (reflections, wormhole trails, bilinear filtering, dithering);
 * 0x450a10, 0x451040: the clouds and daytime rows. */
void GAMEAPI fe_option_reflections(void)
{
    menu_option_row((const char *)0x4b0258, 0, (const char *)0x4b0258, 1, (fe_callback)(uintptr_t)0x450580, 1);
}

void GAMEAPI fe_option_wormhole_trails(void)
{
    menu_option_row((const char *)0x4b0284, 0, (const char *)0x4b0284, 1, (fe_callback)(uintptr_t)0x4505e0, 1);
}

void GAMEAPI fe_option_bilinear(void)
{
    menu_option_row((const char *)0x4b03a8, 0, (const char *)0x4b03a8, 1, (fe_callback)(uintptr_t)0x450950, 1);
}

void GAMEAPI fe_option_dithering(void)
{
    menu_option_row((const char *)0x4b03c8, 0, (const char *)0x4b03c8, 1, (fe_callback)(uintptr_t)0x4509c0, 1);
}

void GAMEAPI fe_option_clouds(void)
{
    menu_option_row2((const char *)0x4b03e8, 0, (const char *)0x4b03e8, 1, (fe_callback)(uintptr_t)0x450a30, 1);
}

void GAMEAPI fe_option_daytime(void)
{
    menu_option_row2((const char *)0x4b04ac, 0, (const char *)0x4b04ac, 1, (fe_callback)(uintptr_t)0x451060, 1);
}

/* --- network players ------------------------------------------------- */

/* 0x48cfd0: a network player's "connected" word. */
uint32_t GAMEAPI fe_net_player_active(int32_t player)
{
    return *(uint32_t *)NET_PLAYER((uint32_t)player);
}

/* 0x48cab0: a network packet's checksum: the byte sum of `size` bytes
 * with the checksum word (+4) zeroed first. */
uint32_t GAMEAPI fe_net_checksum(uint8_t *packet, uint32_t size)
{
    uint32_t sum = 0;

    U32(packet, 4) = 0;
    do {
        sum += *packet++;
    } while (--size);
    return sum;
}

/* 0x48c920: the next packet sequence number (16 bits; callers use ax). */
uint32_t GAMEAPI fe_net_sequence_next(void)
{
    return ++*(uint16_t *)0x4d9ae4;
}

/* 0x45e550: leaving a network game: the host acknowledges, a client tells
 * the host. 1 unless the message fails. */
int32_t GAMEAPI fe_net_leave(void)
{
    if (!(g_net_flags & 1) || !(g_net_flags & 2))
        return 1;
    g_net_flags &= ~2u;
    if (g_net_flags & 0x20) {
        net_quit_ack((int32_t)g_net_local);
        return 1;
    }
    return net_send_flag(1, 4, 0);
}

/* 0x45d170: the race abandoned (race state 6 -> 7, below 7 -> 8). */
void GAMEAPI fe_net_race_abort(void)
{
    int32_t state;

    if (*(uint32_t *)0x4ac984)
        return;
    fe_net_leave();
    state = I32(g_race, 0x57c);
    if (state == 6) {
        I32(g_race, 0x57c) = 7;
        I32(g_race, 0x580) = 0;
        g_replay_state = 0;
        return;
    }
    if (state < 7) {
        I32(g_race, 0x57c) = 8;
        I32(g_race, 0x580) = 0;
    }
    g_replay_state = 0;
}

/* 0x45cb90: a network player gone: their car handed to the AI; with fewer
 * than two humans left the race is abandoned, and once every remaining
 * human has finished (slot +0x259 bit 0) the race ends. */
void GAMEAPI fe_net_player_left(int32_t player)
{
    int32_t finished = 0, humans = 0, i, n;
    uint8_t *race = g_race;

    if (player >= 0 && player < U8(race, 0x54d)) {
        uint8_t *car;

        U8(race, player) = 8;
        car = GP(g_car_slots + (uint32_t)player * CAR_SLOT_SIZE);
        if (car)
            U8(car, 0x70c) = 8;
        if (I32(g_race, 0x57c) < 6)
            U8(g_config, player + 0x8c) = 0;
    }
    race = g_race;
    n = U8(race, 0x54d);
    if (n > 0) {
        uint8_t *config = g_config;

        for (i = 0; i < n; i++) {
            if (U8(g_car_slots, (uint32_t)i * CAR_SLOT_SIZE + 0x259) & 1)
                finished++;
            if (U8(race, i) != 8 && U8(config, i + 0x8c))
                humans++;
        }
    }
    if (n <= 0 || humans < 2) {
        fe_net_race_abort();
        race = g_race;
    }
    if (finished < humans || I32(race, 0x57c) != 5)
        return;
    I32(race, 0x57c) = 6;
    I32(g_race, 0x580) = 0;
    if (net_is_game() == 1 && net_is_host() == 1) {
        if (net_results_due())
            race_results(1);
        net_send_results();
    }
    if (U8(g_race, 0x54d)) {
        for (i = 0;;) {
            uint8_t *slot = g_car_slots + (uint32_t)i * CAR_SLOT_SIZE;

            replay_mark(GP(slot));
            replay_step_car(GP(slot));
            replay_step_car(GP(slot));
            if (++i >= U8(g_race, 0x54d))
                break;
        }
    }
    race_fade(1);
    race_fade_voice();
}

/* 0x48bf70: a network player dropped (0..5): 1, else 0. */
int32_t GAMEAPI fe_net_drop_player(uint32_t player)
{
    if (player >= 6)
        return 0;
    *(uint32_t *)NET_PLAYER(player) = 0;
    fe_net_player_left((int32_t)player);
    return 1;
}
