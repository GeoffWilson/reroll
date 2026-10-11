/* Startup tree, group SC (main 0x4351e0's per-frame and load steps):
 *
 *  - the game / front-end load step 0x435b64 (run each time the game
 *    switches between front end and race), the front-end load sequence
 *    under it (0x4301f0: data and backdrop archives, pointer fix-ups, menu
 *    system, models, particles) and the 60 Hz tick timer glue;
 *  - the per-frame step 0x435552 (catch-up ticks from game_time, network
 *    service calls) and controls gathering 0x4369b5;
 *  - the keyboard: the per-frame key state with auto-repeat 0x43c420, the
 *    key tables (scan code -> character, virtual key, game key code; a
 *    second layout patches 15 keys) and the binding readers (controls_read
 *    0x427b10 and the menu direction bits) with their joystick helpers.
 *
 * Left original and called by address: the network session code, the sound
 * engine, DirectInput, archive / file loading, allocation, the multimedia
 * timer, texture and palette upload, sprite building and the C runtime. */
#include "../runtime.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))

#define STDCALL __attribute__((stdcall))

#define g_config (*(uint8_t **)0x4b2198)
#define g_race (*(uint8_t **)0x4b1960)
#define g_keyboard (*(uint8_t **)0x4b2ca4) /* DirectInput key state, 256 bytes */
#define g_front_end G32(0x4ac984)          /* 1: front end, 0: race */
#define g_network G32(0x4be47c)            /* network game */
#define g_net_state G32(0x5d622c)          /* bit 7: host, bit 4: session lost */
#define g_ticks_done G32(0x4be3e8)         /* ticks run by the timer callback */
#define g_ticking G32(0x4be46c)            /* catch-up ticks in frame_net_step */
#define g_tick_start G32(0x4be470)
#define g_ticks_run G32(0x4be474)
#define g_now G32(0x6c7ff8)

/* Key state (0x6c1fc0, one byte per scan code: 0 up, 8 first delay, then a
 * 3-frame repeat, 0xff held without repeat) and the key tables. */
#define KEY_STATE PTR(0x6c1fc0)
#define KEY_CHAR PTR(0x6c24c0)  /* character, 0 if none */
#define KEY_VK PTR(0x6c1ec0)    /* Windows virtual key */
#define KEY_CODE PTR(0x6c20c0)  /* game key code (dword per scan code) */

typedef void(GAMEAPI *void_fn)(void);
typedef int32_t(GAMEAPI *int_fn)(void);
typedef uint32_t(GAMEAPI *uint_fn)(void);
typedef void(GAMEAPI *arg1_fn)(uint32_t a);
typedef void(GAMEAPI *arg2_fn)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *int1_fn)(uint32_t a);
typedef int32_t(GAMEAPI *int2_fn)(int32_t a, int32_t b);
typedef int32_t(GAMEAPI *int3_fn)(uint32_t a, uint32_t b, uint32_t c);
typedef uint64_t(GAMEAPI *packet_fn)(int32_t player);
typedef void(GAMEAPI *send4_fn)(int32_t player, uint32_t lo, uint32_t hi, uint32_t network);
typedef void(STDCALL *tick_fn)(uint32_t id, uint32_t msg, uint32_t user, uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *upload_fn)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f);
typedef int32_t(GAMEAPI *palette_fn)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                     uint32_t g);
typedef uint8_t *(GAMEAPI *load_fn)(uint32_t entry, uint32_t dest);
typedef uint8_t *(GAMEAPI *alloc_fn)(uint32_t size);
typedef uint8_t *(GAMEAPI *mesh_fn)(uint32_t model, uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *text_fn)(char *dst, int32_t size, uint32_t section, int32_t index);

#define C0(a) ((void_fn)(a))()
#define I0(a) ((int_fn)(a))()

/* Calls into this group go by address too, so the hook table routes them
 * (and the emulator test can stub them). */
#define game_time() ((uint_fn)0x43692a)()
#define race_setup() I0(0x46a060)
#define net_is_game() I0(0x45ef80)
#define fe_joy_axis_at ((int2_fn)0x489890)
#define controls_packet_at ((packet_fn)0x428e10)
#define tick_callback_at ((tick_fn)0x4365ed)
#define key_tables_init_at() C0(0x43c6a0)
#define keyboard_init_at() C0(0x43c3f0)
#define key_layout_set_at(a) ((arg1_fn)0x43d270)(a)
#define game_speed_set_at() C0(0x43775a)
#define net_session_start_at() I0(0x435df6)
#define tick_timer_start_at() I0(0x436565)
#define tick_timer_stop_at() C0(0x4365a4)
#define tick_count_reset_at() C0(0x436595)
#define quit_request_at() C0(0x43699c)
#define frontend_load_at() I0(0x4301f0)
#define frontend_data_load_at(a, b) ((arg2_fn)0x430c10)(a, b)
#define frontend_backdrop_load_at(a, b) ((int2_fn)0x430d80)(a, b)
#define frontend_palette_set_at() I0(0x430e40)
#define fade_step_at() C0(0x430190)
#define tick_hook_call_at() C0(0x429210)
#define frame_counter_step_at() C0(0x463c70)
#define joy_button_down_at ((int2_fn)0x489770)
#define joy_pov_dir_at ((int2_fn)0x4897c0)
#define joy_axis_x_at ((int1_fn)0x4898e0)
#define joy_axis_y_at ((int1_fn)0x4898f0)
#define joy_axis_z_at ((int1_fn)0x489900)
#define controls_mask_menu_keys_at ((int2_fn)0x427d00)
#define model_top_point_at ((int1_fn)0x453730)
#define pad_config_reset_at ((arg1_fn)0x48a0e0)
#define menu_dirty_set_at() C0(0x452d20)
#define game_heap_setup_at() C0(0x46a030)
#define menu_system_init_at() I0(0x4446f0)
#define race_option_clamp_at() C0(0x433cd0)
#define particles_init_at() C0(0x462a20)
#define frontend_models_setup_at() C0(0x453780)
#define menu_sprites_setup_at() C0(0x4461d0)
#define pad_configs_reset_at() C0(0x48a140)

#define archive_load ((load_fn)0x496590)
#define heap_alloc ((alloc_fn)0x4445e0)
#define crt_free ((arg1_fn)0x49827a)
#define crt_malloc ((alloc_fn)0x4982c2)
#define crt_rand() I0(0x4987b5)
#define texture_upload ((upload_fn)0x428c90)
#define high_colour() I0(0x429340)

/* ---- the load step ----------------------------------------------------- */

/* 0x435b64: load step run when switching between front end and race. The
 * front end path opens the input devices and loads the front end (with the
 * 60 Hz tick timer started and a 100 ms settle); the race path clamps the
 * view distance (float at config+0x64: 0 -> 65536, < 1024 -> 1024,
 * > 65536 -> 65536) and runs race_setup. Returns 0 if a step fails. */
int32_t GAMEAPI game_load_step(void)
{
    /* sic: on the race path the original's wait below would read an
     * uninitialised slot (ecx at entry) if race_setup switched to the front
     * end; 0 here. */
    uint32_t start = 0;
    uint32_t t;

    game_speed_set_at();
    key_layout_set_at(g_config[0x36] == 2);
    if (g_front_end == 1) {
        if (!I0(0x48a600))
            return 0;
        C0(0x45c340);
        if (!tick_timer_start_at())
            quit_request_at();
        start = game_time();
        C0(0x488d90);
        ((arg2_fn)0x489bc0)(0, 0);
        ((arg1_fn)0x428b90)(0x47800000);
        if (!frontend_load_at())
            return 0;
    } else {
        /* x87 fcomp flags: C3 = equal or unordered, C0 = below or
         * unordered; the third test sets only when strictly above. */
        float v = *(float *)(g_config + 0x64);
        float limit = *(const float *)0x4a3318;
        if (!(v < limit) && !(v > limit))
            U32(g_config, 0x64) = 0x47800000;
        v = *(float *)(g_config + 0x64);
        limit = *(const float *)0x4a3398;
        if (!(v >= limit))
            U32(g_config, 0x64) = 0x44800000;
        v = *(float *)(g_config + 0x64);
        limit = *(const float *)0x4a3394;
        if (v > limit)
            U32(g_config, 0x64) = 0x47800000;
        ((arg1_fn)0x428b90)(U32(g_config, 0x64));
        if (!race_setup())
            return 0;
    }
    if (g_front_end == 1) {
        ((arg2_fn)0x46fb80)(0x12, 0x4acd50);
        C0(0x472f10);
        while (game_time() < start + 100)
            ;
        if (g_ticks_done == 0) {
            tick_timer_stop_at();
            if (tick_timer_start_at() == 1) {
                t = game_time();
                while (game_time() < t + 100)
                    ;
            }
            if (g_ticks_done == 0) {
                tick_timer_stop_at();
                g_ticking = 1;
                g_tick_start = game_time();
                g_ticks_run = 0;
            }
        }
    }
    C0(0x472f90);
    if (g_front_end == 0) {
        if (g_network) {
            if (!net_session_start_at())
                quit_request_at();
            C0(0x444080);
        }
        C0(0x43693f);
    }
    tick_count_reset_at();
    return 1;
}

/* 0x435df6: starts the network session (three steps, then 0x48d350); 0 if
 * a step fails. */
int32_t GAMEAPI net_session_start(void)
{
    if (!I0(0x45e4a0))
        return 0;
    if (!I0(0x45ef90))
        return 0;
    if (!I0(0x45f040))
        return 0;
    C0(0x48d350);
    return 1;
}

/* 0x436565: clears the tick count and starts the 16 ms timer that calls
 * tick_callback (0x476f90, handle at 0x4be458); 1 if it started. */
int32_t GAMEAPI tick_timer_start(void)
{
    g_ticks_done = 0;
    return ((int3_fn)0x476f90)(0x4be458, 0x10, 0x4365ed) != 0;
}

/* 0x436595: clears the tick count. */
void GAMEAPI tick_count_reset(void)
{
    g_ticks_done = 0;
}

/* 0x4365ed: the 60 Hz tick (a timeSetEvent callback, stdcall with 5
 * arguments; also run by frame_net_step to catch up): counts the tick,
 * runs the tick hook, keeps a once-a-second stamp (every 4th second calls
 * 0x4968b0 unless config+0x410), steps the fade and a 512-frame counter. */
void STDCALL tick_callback(uint32_t id, uint32_t msg, uint32_t user, uint32_t a, uint32_t b)
{
    uint32_t seconds;

    (void)id, (void)msg, (void)user, (void)a, (void)b;
    G32(0x4be468) = 1;
    g_ticks_done++;
    tick_hook_call_at();
    g_now = game_time();
    if (g_now > G32(0x4be4ac) + 1000) {
        G32(0x4be4ac) = g_now;
        seconds = G32(0x4be4b0);
        G32(0x4be4b0) = seconds + 1;
        if (seconds >= 3) {
            G32(0x4be4b0) = 0;
            if (g_config[0x410] == 0)
                C0(0x4968b0);
        }
    }
    fade_step_at();
    frame_counter_step_at();
    G32(0x4be468) = 0;
}

/* 0x43699c: asks the main loop to quit and stops the sound (0x46f650). */
void GAMEAPI quit_request(void)
{
    G32(0x4be48c) = 1;
    ((arg1_fn)0x46f650)(0);
}

/* 0x43775a: game speed scale 0x4acc50 by the speed option (config+0x97:
 * 1 -> 45000, 2 -> 20000, else 0x10000); always 0x10000 in a network game. */
void GAMEAPI game_speed_set(void)
{
    if (g_network) {
        G32(0x4acc50) = 0x10000;
        return;
    }
    switch (g_config[0x97]) {
    case 1:
        G32(0x4acc50) = 45000;
        break;
    case 2:
        G32(0x4acc50) = 20000;
        break;
    default:
        G32(0x4acc50) = 0x10000;
        break;
    }
}

/* 0x430190: per tick: counts 0x4be320 and steps the fade level 0x4ac990 by
 * 0x15 up (0x4be330 == 2, to 0x100) or down (== 1, to 0). */
void GAMEAPI fade_step(void)
{
    int32_t v;

    G32(0x4be320)++;
    if (G32(0x4be330) == 2) {
        v = GI32(0x4ac990) + 0x15;
        GI32(0x4ac990) = v;
        if (v > 0x100)
            GI32(0x4ac990) = 0x100;
    } else if (G32(0x4be330) == 1) {
        v = GI32(0x4ac990) - 0x15;
        GI32(0x4ac990) = v;
        if (v < 0)
            GI32(0x4ac990) = 0;
    }
}

/* 0x429210: runs the tick hook function at 0x4be114, if set (a tail jump in
 * the original). */
void GAMEAPI tick_hook_call(void)
{
    uint32_t fn = G32(0x4be114);

    if (fn)
        ((void_fn)(uintptr_t)fn)();
}

/* 0x463c70: while 0x4d1198 is set, counts 0x6b8d68 round 0..0x1ff. */
void GAMEAPI frame_counter_step(void)
{
    int32_t v;

    if (G32(0x4d1198)) {
        v = GI32(0x6b8d68) + 1;
        GI32(0x6b8d68) = v;
        if (v > 0x1ff)
            GI32(0x6b8d68) = 0;
    }
}

/* ---- per frame ---------------------------------------------------------- */

/* 0x435552: per-frame step: runs the 60 Hz ticks due since 0x4be470 (when
 * the catch-up mode 0x4be46c is on), then in a network game services the
 * session (host: 0x48c630 + 0x48c460, client: 0x45ebc0) and quits when it
 * is lost. */
void GAMEAPI frame_net_step(void)
{
    uint32_t due;

    if (g_ticking == 1) {
        g_now = game_time();
        due = (g_now - g_tick_start) * 0x3c / 1000;
        while (g_ticks_run < due) {
            tick_callback_at(0, 0, 0, 0, 0);
            g_ticks_run++;
        }
    }
    if (g_network) {
        C0(0x48d4a0);
        if (g_net_state >> 7 & 1) {
            C0(0x48c630);
            C0(0x48c460);
        } else {
            C0(0x45ebc0);
        }
        if (!I0(0x48c830)) {
            C0(0x48c900);
            quit_request_at();
        }
        if (g_net_state >> 4 & 1)
            quit_request_at();
    }
}

/* 0x4369b5: per-frame controls: the host sends each player's control packet
 * (0x45deb0, with the network flag the original pushes early) and player
 * 0's (0x45ea60), then 0x45ef40; a client (0x5d622c bit 7) runs 0x45e340,
 * sends player 0's packet and runs 0x45c7d0 in a network game. */
void GAMEAPI controls_gather(void)
{
    uint64_t packet;
    uint32_t network;
    int32_t i;

    if (g_net_state >> 7 & 1) {
        if (g_network)
            C0(0x45e340);
        packet = controls_packet_at(0);
        ((arg2_fn)0x45ea60)((uint32_t)packet, (uint32_t)(packet >> 32));
        if (g_network)
            C0(0x45c7d0);
        return;
    }
    for (i = 0; i < 4; i++) {
        network = g_network;
        packet = controls_packet_at(i);
        ((send4_fn)0x45deb0)(i, (uint32_t)packet, (uint32_t)(packet >> 32), network);
    }
    packet = controls_packet_at(0);
    ((arg2_fn)0x45ea60)((uint32_t)packet, (uint32_t)(packet >> 32));
    C0(0x45ef40);
}

/* ---- keyboard ------------------------------------------------------------ */

/* The last key pressed of three kinds (any, with a character, without):
 * scan code (-1 none), character, virtual key, repeat pulse, new press and
 * game key code. */
struct key_event {
    int32_t code;
    uint32_t chr, vk, pulse, pressed, game_code;
};

/* 0x43c616 (a register-argument helper inside 0x43c420): sets a key event
 * from this frame's key, or keeps the previous one while it is still held. */
static void key_event_set(struct key_event *e, int32_t code, const uint8_t *pulse, const uint8_t *pressed)
{
    if (code == -1) {
        code = e->code;
        if (code == -1 || KEY_STATE[code] == 0) {
            e->code = -1;
            e->pulse = 0;
            e->pressed = 0;
            e->chr = 0;
            e->vk = 0;
            e->game_code = 0;
            return;
        }
    }
    e->code = code;
    e->pulse = pulse[code];
    e->pressed = pressed[code];
    e->chr = KEY_CHAR[code];
    e->vk = KEY_VK[code];
    e->game_code = U32(KEY_CODE, code * 4);
}

/* 0x43c420: per-frame key state: auto-repeat counters per scan code (first
 * repeat after 8 frames, then every 3), the last new key press of each kind
 * (events at 0x4af438 any, 0x4af450 with a character, 0x4af468 without),
 * and the shift / ctrl / alt / navigation key flags 0x4beb38..0x4beb74
 * (either of each key pair). */
void GAMEAPI keyboard_update(void)
{
    uint8_t keys[256], pulse[256], pressed[256];
    int32_t any = -1, with_char = -1, without = -1;
    int32_t i;
    uint8_t s;

    if (G32(0x4beb78) == 0)
        keyboard_init_at();
    for (i = 0; i < 256; i++)
        keys[i] = g_keyboard[i];
    for (i = 0; i < 256; i++) {
        uint8_t p = 0, n = 0;

        if (!(keys[i] & 0x80)) {
            s = 0;
        } else {
            s = KEY_STATE[i];
            if (s == 0) {
                n = 1;
                s = 8;
                p = 1;
            } else if (s != 0xff && --s == 0) {
                s = 3;
                p = 1;
            }
        }
        KEY_STATE[i] = s;
        pulse[i] = p;
        pressed[i] = n;
        if (n) {
            if (KEY_CHAR[i]) {
                if (with_char == -1) {
                    with_char = i;
                    any = i;
                }
            } else if (without == -1) {
                without = i;
                if (any == -1)
                    any = i;
            }
        }
    }
    key_event_set((struct key_event *)0x4af438, any, pulse, pressed);
    key_event_set((struct key_event *)0x4af450, with_char, pulse, pressed);
    key_event_set((struct key_event *)0x4af468, without, pulse, pressed);
    G32(0x4beb38) = (keys[0x2a] | keys[0x36]) >> 7; /* shift */
    G32(0x4beb3c) = (keys[0x1d] | keys[0x9d]) >> 7; /* ctrl */
    G32(0x4beb40) = (keys[0x38] | keys[0xb8]) >> 7; /* alt */
    G32(0x4beb44) = (keys[0xc8] | keys[0x48]) >> 7; /* up */
    G32(0x4beb48) = (keys[0xd0] | keys[0x50]) >> 7; /* down */
    G32(0x4beb4c) = (keys[0xcb] | keys[0x4b]) >> 7; /* left */
    G32(0x4beb50) = (keys[0xcd] | keys[0x4d]) >> 7; /* right */
    G32(0x4beb54) = (keys[0xc9] | keys[0x49]) >> 7; /* page up */
    G32(0x4beb58) = (keys[0xd1] | keys[0x51]) >> 7; /* page down */
    G32(0x4beb5c) = (keys[0xc7] | keys[0x47]) >> 7; /* home */
    G32(0x4beb60) = (keys[0xcf] | keys[0x4f]) >> 7; /* end */
    G32(0x4beb64) = (keys[0xd2] | keys[0x52]) >> 7; /* insert */
    G32(0x4beb68) = (keys[0xd3] | keys[0x53]) >> 7; /* delete */
    G32(0x4beb6c) = (keys[0x1c] | keys[0x9c]) >> 7; /* enter */
    G32(0x4beb70) = (keys[0x0d] | keys[0x4e]) >> 7; /* = / keypad + */
    G32(0x4beb74) = (keys[0x0c] | keys[0x4a]) >> 7; /* - / keypad - */
}

/* 0x43c3f0: first use: clears the key state and builds the key tables. */
void GAMEAPI keyboard_init(void)
{
    int32_t i;

    if (G32(0x4beb78))
        return;
    for (i = 0; i < 0x40; i++)
        G32(0x6c1fc0 + i * 4) = 0;
    key_tables_init_at();
    G32(0x4beb78) = 1;
}

struct key_def {
    uint8_t code, chr, vk;
    uint32_t game_code;
};

/* The key tables of 0x43c6a0 (US layout): scan code, character, virtual
 * key, game key code (0x800000 | n for special keys, 0x80000000 | character
 * for the keypad). */
static const struct key_def key_defs[111] = {
    {0x01, 0x1b, 0x1b, 0x800018}, {0x02, 0x31, 0x31, 0x31},
    {0x03, 0x32, 0x32, 0x32}, {0x04, 0x33, 0x33, 0x33},
    {0x05, 0x34, 0x34, 0x34}, {0x06, 0x35, 0x35, 0x35},
    {0x07, 0x36, 0x36, 0x36}, {0x08, 0x37, 0x37, 0x37},
    {0x09, 0x38, 0x38, 0x38}, {0x0a, 0x39, 0x39, 0x39},
    {0x0b, 0x30, 0x30, 0x30}, {0x0c, 0x2d, 0x00, 0x2d},
    {0x0d, 0x3d, 0x00, 0x800008}, {0x0e, 0x08, 0x08, 0x800019},
    {0x0f, 0x09, 0x09, 0x80001a}, {0x10, 0x51, 0x51, 0x51},
    {0x11, 0x57, 0x57, 0x57}, {0x12, 0x45, 0x45, 0x45},
    {0x13, 0x52, 0x52, 0x52}, {0x14, 0x54, 0x54, 0x54},
    {0x15, 0x59, 0x59, 0x59}, {0x16, 0x55, 0x55, 0x55},
    {0x17, 0x49, 0x49, 0x49}, {0x18, 0x4f, 0x4f, 0x4f},
    {0x19, 0x50, 0x50, 0x50}, {0x1a, 0x5b, 0x00, 0x80001d},
    {0x1b, 0x5d, 0x00, 0x80001e}, {0x1c, 0x0d, 0x0d, 0x80001b},
    {0x1d, 0x00, 0x11, 0x800031}, {0x1e, 0x41, 0x41, 0x41},
    {0x1f, 0x53, 0x53, 0x53}, {0x20, 0x44, 0x44, 0x44},
    {0x21, 0x46, 0x46, 0x46}, {0x22, 0x47, 0x47, 0x47},
    {0x23, 0x48, 0x48, 0x48}, {0x24, 0x4a, 0x4a, 0x4a},
    {0x25, 0x4b, 0x4b, 0x4b}, {0x26, 0x4c, 0x4c, 0x4c},
    {0x27, 0x3b, 0x00, 0x80001f}, {0x28, 0x27, 0x00, 0x27},
    {0x29, 0x60, 0x00, 0x800021}, {0x2a, 0x00, 0x10, 0x800030},
    {0x2b, 0x23, 0x00, 0x23}, {0x2c, 0x5a, 0x5a, 0x5a},
    {0x2d, 0x58, 0x58, 0x58}, {0x2e, 0x43, 0x43, 0x43},
    {0x2f, 0x56, 0x56, 0x56}, {0x30, 0x42, 0x42, 0x42},
    {0x31, 0x4e, 0x4e, 0x4e}, {0x32, 0x4d, 0x4d, 0x4d},
    {0x33, 0x2c, 0x00, 0x2c}, {0x34, 0x2e, 0x00, 0x2e},
    {0x35, 0x2f, 0x00, 0x2f}, {0x36, 0x00, 0x10, 0x800034},
    {0x37, 0x2a, 0x6a, 0x80800002}, {0x38, 0x00, 0x12, 0x800032},
    {0x39, 0x20, 0x20, 0x80001c}, {0x3a, 0x00, 0x14, 0x80002d},
    {0x3b, 0xe3, 0x70, 0x800009}, {0x3c, 0xe4, 0x71, 0x80000a},
    {0x3d, 0xe5, 0x72, 0x80000b}, {0x3e, 0xe6, 0x73, 0x80000c},
    {0x3f, 0xe7, 0x74, 0x80000d}, {0x40, 0xe8, 0x75, 0x80000e},
    {0x41, 0xe9, 0x76, 0x80000f}, {0x42, 0xea, 0x77, 0x800010},
    {0x43, 0xeb, 0x78, 0x800011}, {0x44, 0xec, 0x79, 0x800012},
    {0x45, 0x00, 0x90, 0x80002e}, {0x46, 0x00, 0x91, 0x80002f},
    {0x47, 0x37, 0x67, 0x80000037}, {0x48, 0x38, 0x68, 0x80000038},
    {0x49, 0x39, 0x69, 0x80000039}, {0x4a, 0x2d, 0x6d, 0x8000002d},
    {0x4b, 0x34, 0x64, 0x80000034}, {0x4c, 0x35, 0x65, 0x80000035},
    {0x4d, 0x36, 0x66, 0x80000036}, {0x4e, 0x2b, 0x6b, 0x8000002b},
    {0x4f, 0x31, 0x61, 0x80000031}, {0x50, 0x32, 0x62, 0x80000032},
    {0x51, 0x33, 0x63, 0x80000033}, {0x52, 0x30, 0x60, 0x80000030},
    {0x53, 0x2e, 0x6e, 0x8000002e}, {0x56, 0x5c, 0x00, 0x5c},
    {0x57, 0xed, 0x7a, 0x800013}, {0x58, 0xee, 0x7b, 0x800014},
    {0x64, 0xef, 0x7c, 0x800015}, {0x65, 0xf0, 0x7d, 0x800016},
    {0x66, 0xf1, 0x7e, 0x800017}, {0x8d, 0x3d, 0x00, 0x80800008},
    {0x91, 0x40, 0x00, 0x40}, {0x92, 0x3a, 0x00, 0x3a},
    {0x93, 0x5f, 0x00, 0x5f}, {0x9c, 0x0d, 0x0d, 0x80800001},
    {0x9d, 0x00, 0x11, 0x800035}, {0xb3, 0x2c, 0x00, 0x8000002c},
    {0xb5, 0x2f, 0x6f, 0x8000002f}, {0xb8, 0x00, 0x12, 0x800036},
    {0xc7, 0xf2, 0x24, 0x800023}, {0xc8, 0xf3, 0x26, 0x800024},
    {0xc9, 0xf4, 0x21, 0x800025}, {0xcb, 0xf5, 0x25, 0x800026},
    {0xcd, 0xf6, 0x27, 0x800027}, {0xcf, 0xf7, 0x23, 0x800028},
    {0xd0, 0xf8, 0x28, 0x800029}, {0xd1, 0xf9, 0x22, 0x80002a},
    {0xd2, 0xfa, 0x2d, 0x80002b}, {0xd3, 0xfb, 0x2e, 0x80002c},
    {0xdb, 0x00, 0x5b, 0x800033}, {0xdc, 0x00, 0x5c, 0x800037},
    {0xdd, 0x00, 0x5d, 0x800038},
};

/* The keys the second layout (0x4beb7c set, German) changes. */
static const struct key_def key_defs_alt[15] = {
    {0x0c, 0xdf, 0x00, 0xffffffdf}, {0x0d, 0x27, 0x00, 0x27},
    {0x15, 0x5a, 0x5a, 0x5a}, {0x1a, 0xdc, 0x00, 0xffffffdc},
    {0x1b, 0x2b, 0x00, 0x2b}, {0x27, 0xd6, 0x00, 0xffffffd6},
    {0x28, 0xc4, 0x00, 0xffffffc4}, {0x29, 0x5e, 0x00, 0x5e},
    {0x2b, 0x23, 0x00, 0x23}, {0x2c, 0x59, 0x59, 0x59},
    {0x33, 0x2c, 0x00, 0x2c}, {0x34, 0x2e, 0x00, 0x2e},
    {0x35, 0x2d, 0x00, 0x2d}, {0x53, 0x2c, 0x00, 0x8000002c},
    {0x56, 0x3c, 0x00, 0x3c},
};

static void key_defs_apply(const struct key_def *d, int32_t count)
{
    int32_t i;

    for (i = 0; i < count; i++) {
        KEY_CHAR[d[i].code] = d[i].chr;
        KEY_VK[d[i].code] = d[i].vk;
        U32(KEY_CODE, d[i].code * 4) = d[i].game_code;
    }
}

/* 0x43c6a0: builds the key tables: clears them, fills the US layout, then
 * the second layout's keys when 0x4beb7c is set. */
void GAMEAPI key_tables_init(void)
{
    int32_t i;

    for (i = 0; i < 0x100; i++)
        G32(0x6c20c0 + i * 4) = 0;
    for (i = 0; i < 0x40; i++) {
        G32(0x6c24c0 + i * 4) = 0;
        G32(0x6c1ec0 + i * 4) = 0;
    }
    key_defs_apply(key_defs, sizeof key_defs / sizeof key_defs[0]);
    if (G32(0x4beb7c))
        key_defs_apply(key_defs_alt, sizeof key_defs_alt / sizeof key_defs_alt[0]);
}

/* 0x43d270: selects the key layout (config+0x36 == 2: the second one) and
 * rebuilds the key tables. */
void GAMEAPI key_layout_set(uint32_t alt)
{
    G32(0x4beb7c) = alt;
    key_tables_init_at();
}

/* ---- control bindings ---------------------------------------------------- */

/* Each player's bindings: *0x4ac6fc[player] -> records of 16 bytes (action
 * mask, kind 0 button / 1 axis / 2 key / 3 hat, device, code), count at
 * *0x4ac70c[player]. */
#define BINDINGS(p) (*(uint8_t **)(uintptr_t)(0x4ac6fc + (p) * 4))
#define BINDING_COUNT(p) (**(int32_t **)(uintptr_t)(0x4ac70c + (p) * 4))

/* 0x427b10: the action mask from a player's bindings. Axis bindings (code
 * low nibble: 0/1 X -/+, 2/3 Y, 4/5 Z, 6/7 axis code >> 4) are skipped when
 * `no_axes`; in a network game the axis value's low 5 bits are ignored.
 * Player 0's Escape always gives 0x800. */
uint32_t GAMEAPI controls_read(int32_t player, int32_t no_axes)
{
    uint8_t *b;
    int32_t count, value;
    uint32_t keys = 0, joy = 0, mask;

    if (player < 0 || player > 3)
        return 0;
    count = BINDING_COUNT(player);
    b = BINDINGS(player);
    mask = net_is_game() ? 0xffffffe0 : 0xffffffff;
    if (player == 0 && (g_keyboard[1] & 0x80))
        keys = 0x800;
    for (; count > 0; count--, b += 16) {
        switch (U32(b, 4)) {
        case 0:
            if (joy_button_down_at(I32(b, 8), I32(b, 12)))
                joy |= U32(b, 0);
            break;
        case 1:
            if (no_axes)
                break;
            switch (U32(b, 12) & 0xf) {
            case 0:
                value = joy_axis_x_at(U32(b, 8));
                goto negative;
            case 1:
                value = joy_axis_x_at(U32(b, 8));
                goto positive;
            case 2:
                value = joy_axis_y_at(U32(b, 8));
                goto negative;
            case 3:
                value = joy_axis_y_at(U32(b, 8));
                goto positive;
            case 4:
                value = joy_axis_z_at(U32(b, 8));
                goto negative;
            case 5:
                value = joy_axis_z_at(U32(b, 8));
                goto positive;
            case 6:
                value = fe_joy_axis_at(I32(b, 8), I32(b, 12) >> 4);
                goto negative;
            case 7:
                value = fe_joy_axis_at(I32(b, 8), I32(b, 12) >> 4);
                goto positive;
            default:
                break;
            }
            break;
        negative:
            if ((int32_t)(mask & (uint32_t)value) < 0)
                joy |= U32(b, 0);
            break;
        positive:
            if ((int32_t)(mask & (uint32_t)value) > 0)
                joy |= U32(b, 0);
            break;
        case 2:
            if (g_keyboard[U32(b, 12)] & 0x80)
                keys |= U32(b, 0);
            break;
        case 3:
            if (joy_pov_dir_at(I32(b, 8), I32(b, 12)))
                joy |= U32(b, 0);
            break;
        default:
            break;
        }
    }
    return keys | joy;
}

/* 0x427d00: clears from `mask` the actions bound to a held menu key
 * (arrows, Enter, Space, Escape), so menus don't also see them as actions. */
uint32_t GAMEAPI controls_mask_menu_keys(int32_t player, uint32_t mask)
{
    const uint8_t *keyboard;
    uint8_t *b;
    int32_t count, code;

    if (player < 0 || player > 3)
        return 0;
    b = BINDINGS(player);
    count = BINDING_COUNT(player);
    if (count <= 0)
        return mask;
    keyboard = g_keyboard;
    for (; count; count--, b += 16) {
        if (I32(b, 4) != 2)
            continue;
        code = I32(b, 12);
        if (code != 0xc8 && code != 0xd0 && code != 0xcb && code != 0xcd && code != 0x1c && code != 0x39 &&
            code != 1)
            continue;
        if (keyboard[code] & 0x80)
            mask &= ~U32(b, 0);
    }
    return mask;
}

/* 0x428d20: menu direction bits from the keyboard: 1 up, 2 down, 4 left,
 * 8 right, 0x10 Enter or Space, 0x20 Escape. */
uint32_t GAMEAPI keyboard_nav_bits(void)
{
    const uint8_t *k = g_keyboard;
    uint32_t bits = 0;

    if (k[0xc8] & 0x80)
        bits = 1;
    if (k[0xd0] & 0x80)
        bits |= 2;
    if (k[0xcb] & 0x80)
        bits |= 4;
    if (k[0xcd] & 0x80)
        bits |= 8;
    if (k[0x1c] & 0x80)
        bits |= 0x10;
    if (k[0x39] & 0x80)
        bits |= 0x10;
    if (k[1] & 0x80)
        bits |= 0x20;
    return bits;
}

/* 0x428d70: menu direction bits from the player's joystick (beyond +-0x40);
 * with no direction, 0x10 for action 0x40 and 0x20 for 0x800 in `mask`
 * (after clearing actions on held menu keys). */
uint32_t GAMEAPI controls_nav_bits(uint32_t mask, int32_t player)
{
    int32_t joystick = S16(0x6bfdec, player * 0x274);
    uint32_t m = (uint32_t)controls_mask_menu_keys_at(player, (int32_t)mask);
    uint32_t bits = 0;

    if (joy_axis_y_at(joystick) < -0x40)
        bits = 1;
    if (joy_axis_y_at(joystick) > 0x40)
        bits |= 2;
    if (joy_axis_x_at(joystick) < -0x40)
        bits |= 4;
    if (joy_axis_x_at(joystick) > 0x40)
        bits |= 8;
    if (bits == 0) {
        if (m & 0x40)
            bits = 0x10;
        if (m & 0x800)
            bits |= 0x20;
    }
    return bits;
}

/* Joystick records: 0x43c bytes from 0x4d75e8, count at 0x4d9554. */
#define PAD(p) PTR(0x4d75e8 + (uint32_t)(p) * 0x43c)

/* 0x489770: 1 if the pad's button is down. */
int32_t GAMEAPI joy_button_down(int32_t pad, int32_t button)
{
    if (pad >= GI32(0x4d9554))
        return 0;
    if (button >= I32(PAD(pad), 0))
        return 0;
    return U8(PAD(pad), 0xe8 + button) != 0;
}

/* 0x4897c0: 1 if hat (code & 0xf) points in direction (code >> 4 & 0xf:
 * 0 up, 1 right, 2 down, 3 left; hundredths of a degree, 45 either side). */
int32_t GAMEAPI joy_pov_dir(int32_t pad, int32_t code)
{
    int32_t hat = code & 0xf, v;

    if (pad >= GI32(0x4d9554))
        return 0;
    if (hat >= I32(PAD(pad), 0xc))
        return 0;
    v = I32(PAD(pad), 0xd8 + hat * 4);
    if ((uint16_t)v == 0xffff)
        return 0;
    switch ((uint32_t)(code >> 4) & 0xf) {
    case 0:
        return v > 0x7b0c || v < 0x1194;
    case 1:
        return v > 0x1194 && v < 0x34bc;
    case 2:
        return v > 0x34bc && v < 0x57e4;
    case 3:
        return v > 0x57e4 && v < 0x7b0c;
    default:
        return 0;
    }
}

/* 0x4898e0, 0x4898f0, 0x489900: the pad's X, Y and Z axes. */
int32_t GAMEAPI joy_axis_x(int32_t pad)
{
    return fe_joy_axis_at(pad, 0);
}

int32_t GAMEAPI joy_axis_y(int32_t pad)
{
    return fe_joy_axis_at(pad, 1);
}

int32_t GAMEAPI joy_axis_z(int32_t pad)
{
    return fe_joy_axis_at(pad, 2);
}

/* 0x444050: sum of the X, Y, Z position of the car in player slot `slot`. */
int32_t GAMEAPI car_position_sum(int32_t slot)
{
    uint8_t *car = *(uint8_t **)(uintptr_t)(0x6bfb80 + slot * 0x274);

    return I32(car, 0x3c) + I32(car, 0x38) + I32(car, 0x34);
}

/* ---- front-end load ------------------------------------------------------ */

/* 0x4301f0: loads the front end: heap, sprite pool, the backdrop (archive
 * entries 0x26/0x27, or 0x76/0x77 in high colour) unless 0x4d4650, the
 * front-end data (entries 0..7 by colour depth and 0x6cce7c), text, menus,
 * sprites, models, particles, pads; resets the fade. 0 if the backdrop
 * fails. */
int32_t GAMEAPI frontend_load(void)
{
    int32_t r;

    game_heap_setup_at();
    ((arg1_fn)0x423f00)(0x96);
    if (G32(0x4d4650) == 0) {
        if (high_colour())
            r = frontend_backdrop_load_at(0x76, 0x77);
        else
            r = frontend_backdrop_load_at(0x26, 0x27);
        if (!r)
            return r;
    }
    if (high_colour()) {
        if (G32(0x6cce7c))
            frontend_data_load_at(4, 5);
        else
            frontend_data_load_at(6, 7);
    } else {
        if (G32(0x6cce7c))
            frontend_data_load_at(0, 1);
        else
            frontend_data_load_at(2, 3);
    }
    C0(0x42e2a0);
    ((arg1_fn)0x42e0c0)(0);
    menu_system_init_at();
    C0(0x453020);
    C0(0x453130);
    C0(0x453570);
    race_option_clamp_at();
    particles_init_at();
    C0(0x453710);
    C0(0x4116f0);
    C0(0x457ea0);
    C0(0x48d4f0);
    G32(0x6c8160) = 0;
    G32(0x4be320) = 0;
    G32(0x4be328) = 0;
    G32(0x4ac990) = 0x100;
    return 1;
}

/* 0x430c10: loads the front-end model block (archive entry `models`, to
 * 0x4be344) and its palette entry (`palette`, to 0x4be348), turns the
 * block's offsets into pointers, publishes them, allocates the 0x60ae0-byte
 * work area (two halves), then sets up the models and the palette. */
void GAMEAPI frontend_data_load(uint32_t models, uint32_t palette)
{
    uint8_t *base, *work, *e;
    uint32_t i;

    C0(0x430e00);
    G32(0x4be348) = (uint32_t)(uintptr_t)archive_load(palette, 0);
    base = archive_load(models, 0);
    G32(0x4be344) = (uint32_t)(uintptr_t)base;
    U32(base, 0x110) += (uint32_t)(uintptr_t)base;
    U32(base, 0x18) += (uint32_t)(uintptr_t)base;
    U32(base, 0xd8) += (uint32_t)(uintptr_t)base;
    U32(base, 0xcc) += (uint32_t)(uintptr_t)base;
    U32(base, 0x84) += (uint32_t)(uintptr_t)base;
    G32(0x6c1e30) = 0x6bfac0;
    G32(0x6c1eb0) = (uint32_t)(uintptr_t)base;
    G32(0x6c1e14) = U32(base, 0x110);
    G32(0x4be340) = U32(base, 0x110);
    G32(0x6bfb58) = U32(base, 0xcc);
    G32(0x6bfb5c) = U32(base, 0xd8);
    G32(0x6bfad8) = U32(base, 0x18);
    G32(0x6bfb18) = U32(base, 0x84);
    work = heap_alloc(0x60ae0);
    G32(0x4beb84) = (uint32_t)(uintptr_t)work;
    G32(0x6c1e1c) = (uint32_t)(uintptr_t)work + 0x30570;
    G32(0x6c1e34) = (uint32_t)(uintptr_t)work + 0x30570;
    G32(0x6c1e38) = (uint32_t)(uintptr_t)work + 0x60ae0;
    base = PTR(G32(0x4be344));
    for (i = 0; i < U16(base, 0x2c); i++) {
        uint8_t **table = (uint8_t **)(uintptr_t)U32(base, 0x18);

        table[i] = (uint8_t *)table[i] + (uint32_t)(uintptr_t)base;
        e = table[i];
        U32(e, 0xc) += (uint32_t)(uintptr_t)e;
        U32(e, 0x40) += (uint32_t)(uintptr_t)e;
        U32(e, 0x18) += (uint32_t)(uintptr_t)e;
        if (U32(e, 0x44))
            U32(e, 0x44) += (uint32_t)(uintptr_t)e;
        base = PTR(G32(0x4be344));
    }
    ((arg1_fn)0x460aa0)(0);
    C0(0x47c2c0);
    frontend_models_setup_at();
    frontend_palette_set_at();
}

/* 0x430d80: shows the backdrop: loads archive entry `entry` to 0x4be348,
 * sets its palette, uploads it as a 256x128 texture and frees it. Returns
 * the palette result, 0 if the upload failed. `unused` is not read. */
int32_t GAMEAPI frontend_backdrop_load(uint32_t unused, uint32_t entry)
{
    int32_t r;

    (void)unused;
    if (G32(0x4be348))
        crt_free(G32(0x4be348));
    G32(0x4be348) = 0;
    G32(0x4be348) = (uint32_t)(uintptr_t)archive_load(entry, 0);
    r = frontend_palette_set_at();
    if (!texture_upload((uint32_t)I0(0x428c80), 0x100, 0x80, 0, 0, 0))
        r = 0;
    C0(0x4205e0);
    if (G32(0x4be348))
        crt_free(G32(0x4be348));
    G32(0x4be348) = 0;
    return r;
}

/* 0x430e40: sets the palette from the image at 0x4be348 (0x428130 with its
 * size, two bytes, three offsets and 2 in high colour); 0 without one. */
int32_t GAMEAPI frontend_palette_set(void)
{
    uint8_t *p = PTR(G32(0x4be348));
    uint32_t mode;

    if (!p)
        return 0;
    mode = high_colour() ? 2 : 0;
    return ((palette_fn)0x428130)(U16(p, 0x34), U8(p, 0x32), U8(p, 0x33), U32(p, 0x38) + (uint32_t)(uintptr_t)p,
                                  U32(p, 0x3c) + (uint32_t)(uintptr_t)p, U32(p, 0x24) + (uint32_t)(uintptr_t)p,
                                  mode);
}

/* 0x433cd0: keeps the race option race+0x585 (0..2) within what the flags at
 * race+0x720 allow (2 needs bit 0x10, 1 needs bit 8). */
void GAMEAPI race_option_clamp(void)
{
    if (g_race[0x585] > 2)
        g_race[0x585] = 2;
    if (g_race[0x585] == 2 && !(g_race[0x720] & 0x10))
        g_race[0x585] = 1;
    if (g_race[0x585] == 1 && !(g_race[0x720] & 8))
        g_race[0x585] = 0;
}

/* 0x439fd0: sets the two 6-entry tables 0x4beaf0 and 0x4beb08 to -1. */
void GAMEAPI key_slots_reset(void)
{
    int32_t i;

    for (i = 0; i < 6; i++)
        G32(0x4beaf0 + i * 4) = 0xffffffff;
    for (i = 0; i < 6; i++)
        G32(0x4beb08 + i * 4) = 0xffffffff;
}

/* 0x4446f0: menu system set-up: allocates the menu table (0x1cc00 bytes, to
 * 0x4cb4b8) the first time, builds the menu sprites, resets the pad
 * configs, opens the main menu (screen 4, unless 0x4d4650). -1 if the table
 * is missing, else 0. */
int32_t GAMEAPI menu_system_init(void)
{
    uint8_t *table;

    if (G32(0x4cb494) == 0) {
        G32(0x4cb494) = 1;
        table = crt_malloc(0x1cc00);
        G32(0x4cb4b8) = (uint32_t)(uintptr_t)table;
        if (!table)
            return -1;
        G32(0x4cb498) = 1;
    } else if (G32(0x4cb498) == 0) {
        return -1;
    }
    menu_sprites_setup_at();
    C0(0x446250);
    C0(0x445480);
    pad_configs_reset_at();
    if (G32(0x4d4650) == 0) {
        if (g_config[0x9d0] == 8)
            g_config[0x9d0] = 0;
        G32(0x4af650) = 4;
    }
    G32(0x4af64c) = 0xffffffff;
    G32(0x4be32c) = 1;
    C0(0x4447d0);
    return 0;
}

/* 0x4461d0: builds four menu sprite sets (0x4316d0). */
void GAMEAPI menu_sprites_setup(void)
{
    ((arg1_fn)0x4316d0)(0x4cac98);
    ((arg1_fn)0x4316d0)(0x4c99c0);
    ((arg1_fn)0x4316d0)(0x4ca860);
    ((arg1_fn)0x4316d0)(0x4c24d0);
}

/* 0x453730: the model's vertex (8 bytes each, at +0x40, count u16 at +4)
 * with the greatest y (s16 at +4); the first one if none beats -0x7fff. */
uint32_t GAMEAPI model_top_point(uint8_t *model)
{
    uint32_t count = U16(model, 4);
    uint8_t *v = PTR(U32(model, 0x40));
    int32_t best = 0, top = -0x7fff;
    uint32_t i;

    for (i = 0; (int32_t)i < (int32_t)count; i++) {
        if (top < S16(v, 4 + i * 8)) {
            top = S16(v, 4 + i * 8);
            best = (int32_t)i;
        }
    }
    return (uint32_t)(uintptr_t)v + (uint32_t)best * 8;
}

/* 0x453780: sets up the 53 front-end models (table 0x4b0630, 16 bytes: id,
 * model index, flag, angle) as records of 0x48 bytes at 0x4cba80: id,
 * mesh instance (0x460ba0), angles; ids 6..0x21 listed in the four tables
 * at 0x4b0980 (7 each) get a corner offset and spin phase, the first table's
 * model also gives the top point 0x4cfe98. */
void GAMEAPI frontend_models_setup(void)
{
    uint8_t *def = PTR(0x4b0630);
    uint8_t *rec = PTR(0x4cba84);
    uint32_t model, mesh;
    int32_t id, j;
    uint16_t angle;

    do {
        uint8_t *set = PTR(G32(0x6c1e30));

        model = U32(PTR(U32(set, 0x18)), S16(PTR(U32(set, 0x58)), I32(def, 4) * 2) * 4);
        if (U32(def, 8))
            ((arg1_fn)0x40f1e0)(model);
        U32(rec, -4) = U32(def, 0);
        mesh = (uint32_t)(uintptr_t)((mesh_fn)0x460ba0)(model, 0xc00, 0x180, 0);
        U32(rec, 0) = mesh;
        U16(rec, 0x24) = 0;
        U16(rec, 0x26) = 0;
        U16(rec, 0x28) = 0;
        ((arg1_fn)0x4264f0)(mesh + 0xc4);
        ((arg1_fn)0x4264f0)((uint32_t)(uintptr_t)(rec + 4));
        id = I32(def, 0);
        U16(rec, 0x30) = 0;
        U16(rec, 0x2e) = 0;
        U16(rec, 0x2c) = 0;
        U16(rec, 0x38) = 0;
        U16(rec, 0x36) = 0;
        U16(rec, 0x34) = 0;
        U16(rec, 0x40) = 0;
        U16(rec, 0x3e) = 0;
        U16(rec, 0x3c) = 0;
        if (id >= 6 && id <= 0x21) {
            U16(rec, 0x36) = 0x400;
            for (j = 0; j < 0x1c; j += 4) {
                if (id == GI32(0x4b0980 + j)) {
                    U16(rec, 0x24) += 0x21;
                    U16(rec, 0x28) += 0x25;
                    U16(rec, 0x2c) = 0;
                    G32(0x4cfe98) = (uint32_t)model_top_point_at(model);
                    break;
                }
                if (id == GI32(0x4b099c + j)) {
                    U16(rec, 0x24) += 0xffe7;
                    U16(rec, 0x28) += 0x25;
                    U16(rec, 0x2c) = 100;
                    break;
                }
                if (id == GI32(0x4b09b8 + j)) {
                    U16(rec, 0x24) += 0xffe7;
                    U16(rec, 0x28) += 0xffdb;
                    U16(rec, 0x2c) = 200;
                    break;
                }
                if (id == GI32(0x4b09d4 + j)) {
                    U16(rec, 0x24) += 0x21;
                    U16(rec, 0x28) += 0xffdb;
                    U16(rec, 0x2c) = 300;
                    break;
                }
            }
        }
        angle = (uint16_t)((U32(def, 0xc) << 12) >> 8);
        def += 0x10;
        U16(rec, 4) = angle;
        U16(rec, 0xc) = angle;
        U16(rec, 0x14) = angle;
        rec += 0x48;
    } while ((uintptr_t)def < 0x4b0980);
}

/* 0x462a20: allocates and sets up the 255 particles (0x4c bytes each, to
 * 0x4d108c): kind 0xc, flags 0xae, white, random drift in +-0x40 << 8. */
void GAMEAPI particles_init(void)
{
    uint8_t *p = heap_alloc(0x4bb4);
    int32_t i, off;

    G32(0x4d108c) = (uint32_t)(uintptr_t)p;
    for (i = 0; i < 0x12ed; i++)
        U32(p, i * 4) = 0;
    for (off = 0; off < 0x4bb4; off += 0x4c) {
        p = PTR(G32(0x4d108c));
        U32(p, off + 4) = (U32(p, off + 4) & 0xffffff) | 0xc000000;
        U8(p, off + 0xb) = 0xac;
        U8(p, off + 0xb) |= 2;
        U16(PTR(G32(0x4d108c)), off + 0x40) = (uint16_t)(((crt_rand() & 0x7f) - 0x40) << 8);
        U16(PTR(G32(0x4d108c)), off + 0x42) = (uint16_t)(((crt_rand() & 0x7f) - 0x40) << 8);
        U8(PTR(G32(0x4d108c)), off + 1) = 0xff;
        U8(PTR(G32(0x4d108c)), off + 2) = 0xff;
        U8(PTR(G32(0x4d108c)), off + 3) = 0xff;
    }
}

/* 0x46a030: game heap (0x444500) and 0x423880. */
void GAMEAPI game_heap_setup(void)
{
    C0(0x444500);
    C0(0x423880);
}

/* 0x48a0e0: resets a pad's configuration (0x90 bytes at 0x4d9658): all
 * buttons mapped (0x80 bytes of 1), three axes unassigned (-1), four
 * flags on. Skipped if 0x48a020 says the pad is absent. */
void GAMEAPI pad_config_reset(int32_t pad)
{
    uint8_t *c;
    int32_t i;

    if (((int1_fn)0x48a020)((uint32_t)pad) == -1)
        return;
    c = PTR(0x4d9658 + (uint32_t)pad * 0x90);
    for (i = 0; i < 0x20; i++)
        U32(c, i * 4) = 0x1010101;
    U32(c, 0x80) = 0xffffffff;
    U32(c, 0x84) = 0xffffffff;
    U32(c, 0x88) = 0xffffffff;
    U8(c, 0x8c) = 1;
    U8(c, 0x8d) = 1;
    U8(c, 0x8e) = 1;
    U8(c, 0x8f) = 1;
}

/* 0x48a140: resets every pad's configuration (count from 0x48a010). */
void GAMEAPI pad_configs_reset(void)
{
    int32_t i = 0;

    if (I0(0x48a010) <= 0)
        return;
    do {
        pad_config_reset_at((uint32_t)i);
        i++;
    } while (i < I0(0x48a010));
}

/* 0x452d20: marks the menu as changed (0x4cb58c). */
void GAMEAPI menu_dirty_set(void)
{
    G32(0x4cb58c) = 1;
}

/* 0x452fa0: stores five race options (config +0x1c, +0x1d, +0x87, +0x8b,
 * +0x85) and marks the menu changed. */
void GAMEAPI race_options_set(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    g_config[0x1c] = (uint8_t)a;
    g_config[0x1d] = (uint8_t)b;
    g_config[0x87] = (uint8_t)c;
    g_config[0x8b] = (uint8_t)d;
    g_config[0x85] = (uint8_t)e;
    menu_dirty_set_at();
}

/* 0x4484a0: driver `index` (0..6)'s name from the text file, from the
 * countries / colours / names section by config+5 (1 / 2 / other); "" when
 * out of range. */
void GAMEAPI driver_name(char *dst, int32_t index)
{
    char text[0x200];
    uint32_t section;
    int32_t i;

    *dst = 0;
    if (index < 0 || index > 6)
        return;
    if (g_config[5] == 1)
        section = 0x4afa3c;
    else if (g_config[5] == 2)
        section = 0x4afa58;
    else
        section = 0x4afa70;
    ((text_fn)0x42e6d0)(text, 0x200, section, index);
    i = 0;
    do
        dst[i] = text[i];
    while (text[i++]);
}
