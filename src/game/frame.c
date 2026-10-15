/* Race frame timing: replaces 0x4440b0 (race_frame), 0x436a52
 * (present_frame) and 0x443b50 (race_draw) in Rollcage.exe.
 *
 * Original timing: in single-player races race_frame runs exactly one
 * simulation step per frame, and present_frame busy-waits until 32 ms (or
 * 16 ms when race byte +0x58d is set) have passed since the previous present.
 * So the physics step is effectively the frame: 31.25 steps per second, and
 * the race is drawn by the last step of each frame (sim_step calls race_draw).
 *
 * Deliberate change: in normal single-player races, physics steps come from
 * a high-resolution clock at the same 32/16 ms step, so the handling is
 * unchanged, while drawing happens once per displayed frame (capped at
 * REROLL_MAX_FPS, default 120; 0 = VSync only) with cars and cameras blended
 * between the last two steps (interp.c). Network races, the "no wait" setting
 * (+0x92), the pause/replay path and the menus keep the original behaviour. */
#include "../runtime.h"
#include "game.h"
#include "interp.h"
#include "../capture.h"
#include "../vkgame.h"
#include "gamefn.h"

void GAMEAPI viewport_body(int32_t x, int32_t y, int32_t w, int32_t h); /* 0x41f2d0 (src/widescreen.c's viewport) */
int32_t GAMEAPI flip_body(void); /* 0x4876f0 (src/capture.c's capture_flip) */

#define focus_and_poll_input GCALL(0x4368f0, input_focus_poll)
#define sim_network_mode GCALL(0x45eed0, net_state_host)
#define sim_steps_due GCALL(0x436958, sim_steps_due)
#define sim_pause_check GCALL(0x463b70, pause_frame)
#define sim_step GCALL(0x444120, sim_step)
#define game_time GCALL(0x43692a, game_time_get) /* timeGetTime() */
#define present_prepare_a GCALL(0x41e190, d3d_scene_end)
#define present_prepare_b GCALL(0x429070, texture_palettes_apply) /* src/vkgame.c's texture_queue_flush */
#define present_after_wait GCALL(0x4876f0, flip_body)
#define present_pending_fn GCALL(0x405c10, screenshot_dump)
#define present_rect GCALL(0x41f2d0, viewport_body)
#define present_fn_41e040 GCALL(0x41e040, viewport_clear)
#define present_flip GCALL(0x41e170, d3d_scene_begin)

#define g_race_data GP(0x4b1960)
#define g_settings GP(0x4b2198)
#define g_frontend (*(int32_t *)0x4ac984)
#define g_network (*(int32_t *)0x4be47c)
#define g_render_enabled (*(int32_t *)0x4af5ac)
#define g_sim_paused_flag (*(int32_t *)0x4d1198)
#define g_skipped_steps (*(int32_t *)0x4c2458)
#define g_last_present (*(uint32_t *)0x4be4b4)
#define g_present_pending (*(int32_t *)0x4be49c)
#define g_screen_w (*(int32_t *)0x4acc38)
#define g_screen_h (*(int32_t *)0x4acc3c)
#define g_present_time_total (*(uint32_t *)0x4be494)
#define g_present_count (*(uint32_t *)0x4be498)

/* The original race_draw's body in C (race_draw_body, src/game/orig_wc.c); set in main.c. */
void(GAMEAPI *race_draw_original)(void);

/* What race_frame did, for the present_frame call that follows it. */
enum frame_kind {
    FRAME_ORIGINAL, /* present as the original does (menus, network, ...) */
    FRAME_DRAWN,    /* we drew this frame: present now, the clock paced it */
    FRAME_NOTHING,  /* nothing drawn this time round: do not present */
};
static enum frame_kind frame_kind = FRAME_ORIGINAL;

/* Set while our clock-paced loop runs physics steps: race_draw calls from
 * sim_step are skipped then, as race_frame draws once per displayed frame. */
static int holding_draws;

/* Single-player race with the original fixed-rate timing (only then is the
 * race data pointer valid for the checks below, as in the original). */
static int clock_paced_race(void)
{
    return !g_frontend && !g_network && !g_settings[0x92];
}

/* High-resolution clock in counter ticks. Wine and Windows report a 10 MHz
 * counter, so the frequency fits in 32 bits and differences of a few frames
 * do too; no 64-bit division is needed (there is no C runtime to provide it). */
static uint32_t ticks_per_ms;

static uint64_t ticks_now(void)
{
    uint64_t now;

    QueryPerformanceCounter(&now);
    return now;
}

static void clock_init(void)
{
    uint64_t freq;

    if (ticks_per_ms)
        return;
    QueryPerformanceFrequency(&freq);
    ticks_per_ms = (uint32_t)freq / 1000;
    if (!ticks_per_ms)
        ticks_per_ms = 1;
}

/* Display frame interval from REROLL_MAX_FPS (0 = no cap). By default
 * none: the swapchain presents with vsync, one frame a display refresh
 * (an even cadence; a cap under the refresh rate gave a frame two refreshes
 * one time and three the next). With REROLL_VSYNC=0 the default is 120. */
static uint32_t frame_ticks(void)
{
    static int done;
    static uint32_t ticks;
    char buf[16];
    uint32_t fps = 0, i;

    if (done)
        return ticks;
    done = 1;
    if (GetEnvironmentVariableA("REROLL_VSYNC", buf, sizeof(buf)) && buf[0] == '0')
        fps = 120;
    if (GetEnvironmentVariableA("REROLL_MAX_FPS", buf, sizeof(buf)) && buf[0]) {
        fps = 0;
        for (i = 0; buf[i] >= '0' && buf[i] <= '9'; i++)
            fps = fps * 10 + (uint32_t)(buf[i] - '0');
    }
    if (fps && fps < 31)
        fps = 31; /* never fewer frames than physics steps */
    ticks = fps ? ticks_per_ms * 1000 / fps : 0;
    return ticks;
}

/* Physics step clock; the next draw's time (with a frame cap) */
static uint64_t next_step, last_step, last_frame_seen, next_draw;
static uint32_t step_ticks;

/* Steps due now. A gap of over 250 ms (race start, loading, pause) restarts
 * the clock with one step due; after a hitch at most 4 steps run in one frame
 * and the rest are dropped, as a slow frame would in the original. */
static int32_t clock_steps_due(uint64_t now)
{
    uint32_t ticks = ticks_per_ms * (g_race_data[0x58d] ? 16 : 32);
    int32_t due = 0;

    if (!next_step || now - last_frame_seen > (uint64_t)ticks_per_ms * 250 || ticks != step_ticks) {
        step_ticks = ticks;
        next_step = now;
        interp_reset();
    }
    last_frame_seen = now;
    while (now >= next_step && due < 4) {
        last_step = next_step;
        next_step += step_ticks;
        due++;
    }
    if (now >= next_step) { /* still behind: drop the backlog */
        last_step = now;
        next_step = now + step_ticks;
    }
    return due;
}

/* How far we are from the last step towards the next one, 0..65536. */
static uint32_t step_alpha(uint64_t now)
{
    uint32_t since = (uint32_t)(now - last_step);

    if (since >= step_ticks)
        return 65536;
    return (since << 12) / (step_ticks >> 4);
}

void GAMEAPI race_draw(void)
{
    if (holding_draws)
        return;
    race_draw_original();
}

int32_t GAMEAPI race_frame(void)
{
    static int steps_since_draw;
    int32_t network, steps, i, result = 1, paced;
    uint64_t now;

    focus_and_poll_input();
    network = sim_network_mode();
    steps = sim_steps_due(); /* still called, so its own counters stay as in the original */
    frame_kind = FRAME_ORIGINAL;
    paced = !network && clock_paced_race();
    if (!network)
        steps = 1;
    if (paced) {
        clock_init();
        now = ticks_now();
        steps = clock_steps_due(now);
    }

    if (g_sim_paused_flag && sim_pause_check()) {
        if (steps > 0)
            g_skipped_steps += steps;
        interp_reset();
        return 1; /* the original presents (and paces) here */
    }

    holding_draws = paced;
    now = ticks_now();
    for (i = 0; i < steps; i++) {
        result = sim_step(network, i, steps);
        if (paced)
            interp_record_step();
        if (!result)
            break;
    }
    vkgame_sim_ticks += ticks_now() - now;
    holding_draws = 0;
    if (!paced || !result)
        return result;

    /* Draw at most once per display frame, blended between the last steps.
     * HUD flash countdowns advance only on draws that follow a physics step. */
    steps_since_draw += steps;
    now = ticks_now();
    if (!g_render_enabled || (frame_ticks() && now < next_draw)) {
        frame_kind = FRAME_NOTHING;
        return result;
    }
    /* deadlines a frame apart (not "a frame after this one"), so the
     * cadence is the cap's; after a stall, from now */
    next_draw += frame_ticks();
    if (now >= next_draw)
        next_draw = now + frame_ticks();
    interp_draw(race_draw_original, step_alpha(now), steps_since_draw > 0);
    vkgame_game_draw_ticks += ticks_now() - now;
    steps_since_draw = 0;
    frame_kind = FRAME_DRAWN;
    return result;
}

int32_t GAMEAPI present_frame(void)
{
    enum frame_kind kind = frame_kind;
    uint32_t interval = 0x20, start;
    int nowait = 0;

    frame_kind = FRAME_ORIGINAL;
    if (!g_render_enabled)
        return 1;
    if (!g_frontend && !g_network && g_race_data[0x58d])
        interval = 0x10;
    if (!g_frontend && !g_network && g_settings[0x92])
        nowait = 1;
    capture_present(kind != FRAME_NOTHING);
    if (kind == FRAME_NOTHING) {
        /* until the next draw: whole milliseconds while over 1.5 remain, then yields */
        uint64_t t = ticks_now();
        if (next_draw > t && next_draw - t > (uint64_t)ticks_per_ms * 3 / 2)
            Sleep(1);
        else
            Sleep(0);
        return 1;
    }
    if (kind == FRAME_DRAWN)
        nowait = 1;

    present_prepare_a();
    present_prepare_b();
    start = game_time();
    /* the original spins here; sleep while more than 2 ms remain */
    while (!nowait && game_time() < g_last_present + interval)
        if ((int32_t)(g_last_present + interval - game_time()) > 2)
            Sleep(1);
    g_last_present = game_time();
    present_after_wait();
    if (g_present_pending) {
        g_present_pending = 0;
        present_pending_fn();
    }
    present_rect(0, 0, g_screen_w, g_screen_h);
    present_fn_41e040(0);
    if (!present_flip())
        return 0;
    g_present_time_total += g_last_present - start;
    g_present_count++;
    return 1;
}
