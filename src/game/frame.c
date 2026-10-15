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
 * REROLL_MAX_FPS, default 500; 0 = none) with cars and cameras blended
 * between the last two steps (interp.c). The "no wait" setting (+0x92), the
 * pause/replay path and the menus keep the original behaviour.
 *
 * Network races: there the original's frame is the lockstep step itself
 * (the main loop gathers and sends the controls, race_frame steps, and
 * 0x435552 waits for the other players), paced to 32 ms by present_frame.
 * Deliberate change (native build): game_main asks frame_race_step_due()
 * whether a 32 ms step is due and the other players' controls for it are
 * in; a frame with one runs all of that once, as the original frame (the
 * wait moved before the step, where it no longer waits), and every other
 * frame only draws, blended between the last two steps, and pumps the
 * network. Behind, the steps catch up one a frame; more than four behind,
 * the backlog is dropped. */
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

/* Display frame interval from REROLL_MAX_FPS (0 = no cap). By default 500:
 * the swapchain presents in mailbox mode (the newest frame at each refresh,
 * src/render/vkr.c), so frames never wait for the compositor, and 500 a
 * second keep a fast GPU busy enough not to drop to idle clocks (where the
 * compositor's own work on a refresh, at 240 Hz, came late) without running
 * it flat out. With REROLL_VSYNC=1 (FIFO, one frame a refresh) there is no
 * cap. */
static uint32_t frame_ticks(void)
{
    static int done;
    static uint32_t ticks;
    char buf[16];
    uint32_t fps = 500, i;

    if (done)
        return ticks;
    done = 1;
    if (GetEnvironmentVariableA("REROLL_VSYNC", buf, sizeof(buf)) && buf[0] == '1')
        fps = 0;
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

/* Network races: this frame is paced by our clock (net_paced), and runs a
 * step (net_step_frame); the next step's time */
static int net_paced, net_step_frame;
static uint64_t net_next_step;

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

/* Whether the game's network wait (0x435552 -> 0x45ebc0, after the
 * previous step) would return at once: every active player's controls
 * sequence (+0x3c of the 0x66c-byte records at 0x5d2cd0) is the current
 * 0x5d59a0 or the next 0x5d59a4, or there is nothing to wait for (not
 * connected, the host clock bit 7, loose synchronisation's odd steps, the
 * race over). The network is pumped first, as the wait does. */
static int net_controls_in(void)
{
    uint32_t flags = *(uint32_t *)0x5d622c, r;

    GCALL(0x48c630, dplay_receive_all)();
    GCALL(0x48c460, net_inbox_dispatch)();
    if (!(flags & 1) || !(flags & 2) || (flags & 0x80))
        return 1;
    if (!g_settings[0x37] && !(*(uint8_t *)0x4c2458 & 1))
        return 1;
    if (GCALL(0x45eba0, net_race_over)())
        return 1;
    for (r = 0x5d2cd0; r < 0x5d5358; r += 0x66c) {
        uint32_t seq = *(uint32_t *)(uintptr_t)(r + 0x3c);
        if (*(uint32_t *)(uintptr_t)r && seq != *(uint32_t *)0x5d59a0 && seq != *(uint32_t *)0x5d59a4)
            return 0;
    }
    return 1;
}

/* Called by game_main (native build) before each frame's controls:
 *  1  the original frame (controls, race_frame, present, then 0x435552);
 *  2  a network race step: 0x435552 first (the wait that followed the
 *     previous step, which no longer waits), then the controls and the step;
 *  0  a network race frame that only draws.
 * A network step is due every 32 ms once the other players' controls for
 * it are in; until they are, frames keep drawing (and pumping the network)
 * rather than blocking in the wait. After 250 ms without them the step
 * runs anyway, and the game's own wait (with its time-out that drops a
 * player) takes over. */
int frame_race_step_due(void)
{
    static uint64_t waiting_since;
    uint64_t now;

    net_paced = !g_frontend && g_network && !g_settings[0x92] && !g_sim_paused_flag && g_render_enabled;
    if (!net_paced) {
        net_next_step = 0;
        return 1;
    }
    clock_init();
    now = ticks_now();
    if (!net_next_step || now - last_frame_seen > (uint64_t)ticks_per_ms * 250 || step_ticks != ticks_per_ms * 32) {
        step_ticks = ticks_per_ms * 32;
        net_next_step = now;
        interp_reset();
    }
    last_frame_seen = now;
    net_step_frame = 0;
    if (now < net_next_step) {
        net_controls_in(); /* keep the network moving between steps */
        waiting_since = 0;
        return 0;
    }
    if (!net_controls_in()) {
        if (!waiting_since)
            waiting_since = now;
        if (now - waiting_since < (uint64_t)ticks_per_ms * 250)
            return 0;
    }
    waiting_since = 0;
    net_step_frame = 1;
    last_step = now; /* the blend runs from when the step actually ran */
    net_next_step += step_ticks;
    if (now >= net_next_step + 4ull * step_ticks) /* far behind: drop the backlog */
        net_next_step = now + step_ticks;
    return 2;
}

/* A blended draw of the race, at most one per display frame (the cap);
 * `force` draws regardless (a network step's own draw, which the original
 * makes once per step). */
static int32_t draw_blended(int steps, int force)
{
    static int steps_since_draw;
    uint64_t now = ticks_now();

    steps_since_draw += steps;
    if (!g_render_enabled || (!force && frame_ticks() && now < next_draw)) {
        frame_kind = FRAME_NOTHING;
        return 1;
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
    return 1;
}

/* race_frame in a network race paced by our clock */
static int32_t race_frame_network(void)
{
    int32_t network, steps, i, result = 1;
    uint64_t now;

    focus_and_poll_input();
    frame_kind = FRAME_ORIGINAL;
    if (!net_step_frame)
        return draw_blended(0, 0);
    network = sim_network_mode();
    steps = sim_steps_due();
    if (!network)
        steps = 1;
    if (g_sim_paused_flag && sim_pause_check()) {
        if (steps > 0)
            g_skipped_steps += steps;
        interp_reset();
        return 1;
    }
    holding_draws = 1;
    now = ticks_now();
    for (i = 0; i < steps; i++) {
        result = sim_step(network, i, steps);
        interp_record_step();
        if (!result)
            break;
    }
    vkgame_sim_ticks += ticks_now() - now;
    vkgame_sim_steps += (uint32_t)(i < steps ? i + 1 : steps);
    holding_draws = 0;
    if (!result)
        return result;
    return draw_blended(steps, 1);
}

void GAMEAPI race_draw(void)
{
    if (holding_draws)
        return;
    race_draw_original();
}

int32_t GAMEAPI race_frame(void)
{
    int32_t network, steps, i, result = 1, paced;
    uint64_t now;

    if (net_paced && !g_frontend)
        return race_frame_network();
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
    vkgame_sim_steps += (uint32_t)(i < steps ? i + 1 : steps);
    holding_draws = 0;
    if (!paced || !result)
        return result;

    /* Draw at most once per display frame, blended between the last steps.
     * HUD flash countdowns advance only on draws that follow a physics step. */
    draw_blended(steps, 0);
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
