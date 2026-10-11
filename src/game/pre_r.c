/* Before the world step (sim_step 0x444120), group R: the small per-step
 * calls, the engine and surface sounds (0x473250), and sim_step itself.
 *
 * sim_step calls every per-step function by its address, so the hooks route
 * each one to its C version (or the original) exactly as the original
 * dispatcher does.
 *
 * Still original: the sound engine (0x471c90, 0x471da0, 0x471aa0,
 * 0x471f80, 0x471fc0, 0x471f40, 0x471ac0), the force-feedback effects
 * (0x489e10, 0x489cd0, 0x489980), network chat (0x48d270) and the network
 * checks 0x48cf20 / 0x45ef80. */
#include "../runtime.h"
#include <stdlib.h>
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_config (*(uint8_t **)0x4b2198)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define SLOT_CAR(slot) (*(uint8_t **)(slot))
#define g_local_index (*(int32_t *)0x5d535c)
#define g_net_players ((const uint32_t *)0x5d2ce8) /* 0x66c bytes each */
#define g_sound_clock (*(uint32_t *)0x4be388)
#define g_sim_steps (*(uint32_t *)0x4c2458)
#define g_race_steps (*(uint32_t *)0x6c0a50)
#define g_debug_flags (*(uint32_t *)0x6cce40)
#define g_track_info (*(uint8_t **)0x6c1eb0)
#define g_vertex_copy_count (*(int32_t *)0x6c1eb4)
#define g_vertex_copies (*(uint8_t **)0x6c1eb8)
#define g_chat_separator ((const char *)0x4a7f94) /* ": " */

#define I32(p, off) (*(int32_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define S8(p, off) (*(int8_t *)((p) + (off)))
#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define PTR(p, off) (*(uint8_t **)((p) + (off)))
#define SHL(x, n) ((int32_t)((uint32_t)(int32_t)(x) << (n)))
#define ABS(x) ((int32_t)((x) < 0 ? 0u - (uint32_t)(x) : (uint32_t)(x)))

/* `hi` with its low byte replaced by `b`, as the original's byte register
 * loads leave it. The barrier stops clang 23 from folding this into a byte
 * load into the register that also indexes it, which its byte/word fixup
 * pass then wrongly widens to movzx (losing the high bits). */
static inline uint32_t byte_into(uint32_t hi, uint8_t b)
{
    hi &= ~0xffu;
    __asm__("" : "+r"(hi));
    return hi | b;
}

/* Still original. */
typedef int32_t(GAMEAPI *int_fn)(void);
typedef void(GAMEAPI *void_fn)(void);
typedef void(GAMEAPI *ptr_fn)(void *p);
typedef void(GAMEAPI *ptr_int_fn)(void *p, int32_t i);
typedef void(GAMEAPI *int_arg_fn)(int32_t i);
typedef uint32_t(GAMEAPI *sound_play_fn)(uint32_t sound, uint32_t a, uint32_t b, uint32_t c, const void *pos);
typedef uint32_t(GAMEAPI *sound_moving_fn)(uint32_t kind, uint32_t sound, uint32_t a, uint32_t b, uint32_t c,
                                           const void *pos, const void *vel);
typedef void(GAMEAPI *voice_fn)(uint32_t voice);
typedef void(GAMEAPI *voice2_fn)(uint32_t voice, uint32_t value);
typedef void(GAMEAPI *voice3_fn)(uint32_t voice, uint32_t a, uint32_t b);
typedef void(GAMEAPI *music_fn)(uint32_t kind, uint32_t a, uint32_t b);
typedef void(GAMEAPI *pad_fn)(uint8_t *pad, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef void(GAMEAPI *effect3_fn)(int32_t device, int32_t a, int32_t b);
typedef void(GAMEAPI *effect4_fn)(int32_t device, int32_t a, int32_t b, int32_t c);
typedef int32_t(GAMEAPI *dot_fn)(const void *a, const void *b);
typedef void(GAMEAPI *chat_fn)(char *text);
typedef void(GAMEAPI *particles_fn)(uint32_t buttons, uint32_t previous, int32_t skip);
typedef int32_t(GAMEAPI *world_step_fn)(int32_t dt);

#define sound_play ((sound_play_fn)0x471c90)
#define sound_moving ((sound_moving_fn)0x471da0)
#define voice_stop ((voice_fn)0x471f40)
#define voice_reverb ((voice2_fn)0x471aa0)
#define voice_pitch ((voice2_fn)0x471f80)
#define voice_volume ((voice3_fn)0x471fc0)
#define music_mode ((music_fn)0x471ac0)
#define pad_effect ((pad_fn)0x41b4d0)
#define ff_kick ((effect4_fn)0x489e10)
#define ff_engine ((effect3_fn)0x489cd0)
#define ff_road ((effect3_fn)0x489980)
#define dot ((dot_fn)0x40d950)
#define net_local_car ((int_fn)0x48cf20)
#define net_is_game ((int_fn)0x45ef80)
#define chat_send ((chat_fn)0x48d270)

/* 0x48cef0: the local player's car index (0 outside a network game). */
int32_t GAMEAPI local_car_index(void)
{
    if (g_local_index == -1)
        return 0;
    return (int32_t)g_net_players[g_local_index * 0x19b];
}

/* 0x435100: the sound clock. */
uint32_t GAMEAPI sound_clock(void)
{
    return g_sound_clock;
}

/* 0x48e7b0: the buttons the step works from: a network car's delayed ones
 * (+0x684) while it has a pending packet (+0x680), else this step's. */
void GAMEAPI car_step_buttons(uint8_t *car)
{
    if (!I32(car, 0x680))
        U32(car, 0x6fc) = U32(car, 0x700);
    else
        U32(car, 0x6fc) = U32(car, 0x684);
}

/* 0x48e7e0: network cars: take the controls from the pending packet
 * (+0x680 points at a car-shaped record) for up to +0x688 steps, keeping
 * this car's own identity fields. */
void GAMEAPI cars_network_controls(void)
{
    uint32_t i;
    uint8_t *slot = g_car_slots;

    for (i = 0; i < U8(g_race, 0x54d); i++, slot += CAR_SLOT_SIZE) {
        uint8_t *car = SLOT_CAR(slot), *packet = PTR(car, 0x680);
        int8_t left;

        if (!packet)
            continue;
        U32(car, 0x684) = U32(car, 0x700);
        left = (int8_t)(U8(car, 0x688) - 1);
        U8(car, 0x688) = (uint8_t)left;
        if ((left < 0x3c && U32(car, 0x704)) || left <= 0) {
            I32(car, 0x680) = 0;
        } else {
            uint8_t *dst = car + 0x6f8;
            const uint8_t *src = packet + 0x6f8;
            uint8_t b14 = dst[0x14], b15 = dst[0x15], b20 = dst[0x20], b21 = dst[0x21], b22 = dst[0x22];
            uint16_t w16 = U16(dst, 0x16), w18 = U16(dst, 0x18);
            uint32_t d10 = U32(dst, 0x10), d1c = U32(dst, 0x1c), d24 = U32(dst, 0x24);
            int k;

            for (k = 0; k < 10; k++)
                U32(dst, k * 4) = U32(src, k * 4);
            dst[0x14] = b14;
            dst[0x15] = b15;
            U32(dst, 0x10) = d10;
            U16(dst, 0x16) = w16;
            U32(dst, 0x1c) = d1c;
            dst[0x20] = b20;
            U16(dst, 0x18) = w18;
            dst[0x21] = b21;
            dst[0x22] = b22;
            U32(dst, 0x24) = d24;
            U32(dst, 0x4) &= 0xf0f0;
            U32(dst, 0x8) &= 0xf0f0;
            U32(dst, 0xc) &= 0xf0f0;
        }
    }
}

/* 0x43e570: copy the track's moved vertices into place (when the track
 * has any). Each record: target pointer, then x, y, z as int16. */
void GAMEAPI track_vertices_copy(void)
{
    uint8_t *rec;
    int32_t n;

    if (!U8(g_track_info, 0xfc) || g_vertex_copy_count <= 0)
        return;
    for (rec = g_vertex_copies, n = g_vertex_copy_count; n; n--, rec += 0x18) {
        uint8_t *v = PTR(rec, 0);

        U16(v, 0) = U16(rec, 4);
        U16(v, 2) = U16(rec, 6);
        U16(v, 4) = U16(rec, 8);
    }
}

/* 0x4112e0: the catch-up handicap: each racing car's gap behind the
 * leader (+0x6d0) and from it a grip/drag factor (+0x14ba, 0x1000 = none);
 * finished cars (slot +0x259 bit 0) are left out. Off in some modes. */
void GAMEAPI cars_catch_up(void)
{
    uint8_t *order[6];
    uint32_t n, i, removed = 0;
    uint8_t *slot;

    if (U8(g_race, 0x551) == 4 || !U8(g_race, 0xa87)) {
        for (i = 0; i < U8(g_race, 0x54d); i++) {
            uint8_t *car = SLOT_CAR(g_car_slots + i * CAR_SLOT_SIZE);

            U16(car, 0x14b8) = 0x40;
            U16(car, 0x14ba) = 0x1000;
        }
        return;
    }
    n = U8(g_race, 0x54d);
    for (i = 0, slot = g_car_slots; i < n; i++, slot += CAR_SLOT_SIZE)
        order[U8(slot, 0x2d) - 1] = slot; /* by race position */
    for (i = 0; i < n; i++) {
        if (!order[i])
            break;
        if (U8(order[i], 0x259) & 1) {
            uint32_t k;

            for (k = i + 1; k < n; k++)
                order[k - 1] = order[k];
            removed++;
            order[n - 1] = 0;
            i--;
        }
    }
    if (removed == n)
        return;
    {
        int32_t lead = SHL(U8(order[0], 0x2c), 16) + I32(order[0], 0x34);

        for (i = 0; i < (uint32_t)U8(g_race, 0x54d) - removed; i++) {
            uint8_t *car = SLOT_CAR(order[i]);
            int32_t gap = lead - SHL(U8(order[i], 0x2c), 16) - I32(order[i], 0x34), v;

            I32(car, 0x6d0) = gap;
            if (gap > 0x2000)
                v = 0x4000;
            else
                v = (int32_t)((int64_t)fix_div16(SHL(gap, 16), 0x20000000) * 0x4000 >> 16);
            U16(car, 0x14b8) = 0x40;
            S16(car, 0x14ba) = (int16_t)((v + 0x10000) >> 4);
        }
    }
}

/* 0x41c6d0: send chat macro `i` (config +0xdce + i * 0x40) as
 * "name: macro". */
void GAMEAPI chat_send_macro(int32_t i)
{
    char text[128];
    const char *macro = (const char *)g_config + SHL(i, 6) + 0xdce, *s;
    char *d = text;

    if (!*macro)
        return;
    for (s = (const char *)g_config + 0x120; (*d = *s) != 0; d++, s++)
        ;
    for (s = g_chat_separator; (*d = *s) != 0; d++, s++)
        ;
    for (s = macro; (*d = *s) != 0; d++, s++)
        ;
    chat_send(text);
}

/* 0x41c7a0: the local player's extra buttons (+0x704 bits 20..31): chat
 * macros in a network game, and two display toggles. */
void GAMEAPI car_extra_buttons(uint8_t *slot)
{
    uint32_t buttons = U32(SLOT_CAR(slot), 0x704);
    int32_t i;

    if (slot != g_car_slots + net_local_car() * CAR_SLOT_SIZE)
        return;
    if (net_is_game())
        for (i = 0; i < 10; i++)
            if (buttons & (0x100000u << i))
                chat_send_macro(i);
    if (buttons & 0x40000000)
        U8(g_config, 0x25) ^= 1;
    if (buttons & 0x80000000)
        g_debug_flags ^= 8;
}

/* Per player engine sound state, 0x18 bytes from 0x5ff1e0. */
#define ENGINE(i) ((uint8_t *)0x5ff1e0 + (i) * 0x18)
#define E_LEVEL 0x0 /* int32: volume level, high half used */
#define E_SURFACE_VOICE 0x4
#define E_ENGINE_VOICE 0x5
#define E_MOTOR_VOICE 0x6
#define E_BOOST_VOICE 0x7
#define E_REVS 0x8 /* int16 */
#define E_SMOOTH 0xa /* int16 */
#define E_GROUND 0xc /* int16 */
#define E_AIR 0xe /* int16: steps off the ground */
#define E_GEAR 0x10 /* int8 */
#define E_SHIFT 0x11
#define E_LAST_SURFACE 0x12 /* int8 */
#define E_LOUD 0x14 /* int16 */
#define E_SKID 0x16 /* int16 */
/* Per player horn / tune state, 0x10 bytes from 0x5ff280. */
#define TUNE(i) ((uint8_t *)0x5ff280 + (i) * 0x10)
#define g_music_player (*(int32_t *)0x4b2034)
#define g_engine_pitch (*(int32_t *)0x4b2038)
#define g_boom_voice (*(uint32_t *)0x4b2044)
#define g_gear_volumes ((const int32_t *)0x4b2024)
#define g_surface_sounds ((const uint8_t *)0x4b1ee4) /* 0x14 bytes each */
#define g_tunes ((uint8_t *const *)0x4b2180)
#define g_warp_state (*(int32_t *)0x5d9b20)
#define g_warp_car (*(uint8_t **)0x5d9b24)
#define g_warp_scale (*(uint32_t *)0x5d9b2c)
#define g_voice_live ((const uint8_t *)0x5ff4a0) /* 0x30 bytes each */
#define g_track (*(uint8_t ***)0x6c1e30) /* +0: the section pointers */

/* 0x473250: a car's engine, surface, boost and horn sounds and its
 * force feedback, once per step. `p`: its player index. */
void GAMEAPI car_sounds(uint8_t *car, int32_t p)
{
    uint8_t *e = ENGINE(p), *slot = g_car_slots + p * CAR_SLOT_SIZE;
    int32_t spin = ABS(I32(car, 0x5ec)) + ABS(I32(car, 0x528));
    uint8_t rev_boost = U8(car, 0x63d);
    int airborne = (U8(car, 0x4ac) | U8(car, 0x570)) & 1;
    uint8_t kind = U8(g_race, p + 0x1c);
    const uint8_t *tune = g_tunes[kind];
    uint32_t moving_kind = (p == 1 && U8(g_race, 0x551) == 2) ? 2 : 1;
    int32_t level = I32(e, E_LEVEL), revs = S16(e, E_REVS), half, surface = 0;
    uint32_t warp = 0, voice;
    int loud;

    if (g_warp_state && g_warp_car != car)
        warp = (0x10000u - g_warp_scale) >> 7;
    if (U16(g_race, 0x61c)) {
        uint8_t *t = TUNE(p);

        if (kind == 6) {
            if (U32(car, 0x700) & 0x100) {
                if (U32(t, 8) == 0x7f)
                    U32(t, 8) = sound_play(0x3a, 9, 0x400, 16000, 0);
            } else if (U32(t, 8) != 0x7f) {
                voice_stop(U32(t, 8) & 0xff);
                U32(t, 8) = 0x7f;
            }
        } else {
            if ((U32(car, 0x700) & 0x100) && U32(t, 0) == 0xff) {
                U32(t, 0) = 0;
                U32(t, 4) = 0;
                U32(t, 8) = 0x7f;
            }
            if (U32(t, 0) != 0xff) {
                int32_t left = I32(t, 4) + (int32_t)(U32(t, 0xc) - sound_clock());

                I32(t, 4) = left;
                if (left <= 0) {
                    uint32_t note = U32(t, 0), length = U16(tune, note * 4 + 2);

                    U32(t, 4) = length;
                    if (length == 0xff) {
                        U32(t, 0) = 0xff;
                    } else {
                        U32(t, 4) = length * 2;
                        U32(t, 8) = sound_play(byte_into(note, tune[note * 4]), 7, 900, 16000, 0);
                        U32(t, 0) = U32(t, 0) + 1;
                    }
                }
            }
        }
        U32(t, 0xc) = sound_clock();
    }

    if (U8(car, 0x570) & 1) {
        uint8_t *under = PTR(car, 0x1ec), *touch = PTR(car, 0x1d0);

        surface = under ? (int32_t)(U32(under, 0xc) & 0xff) : 0;
        if ((touch && U8(touch, 0xc) == 1) || surface == 1) {
            if (U8(e, E_BOOST_VOICE) == 0x7f)
                U8(e, E_BOOST_VOICE) = (uint8_t)sound_play(0x3e, 7, 0x600, 10000, car + 0x34);
            else if (!g_voice_live[U8(e, E_BOOST_VOICE) * 0x30])
                U8(e, E_BOOST_VOICE) = 0x7f;
        } else {
            U8(e, E_BOOST_VOICE) = 0x7f;
        }
        if (surface > 0x1a)
            surface = 0;
    }

    if (U8(car, 0x70c) == 10) {
        if (p == 0)
            moving_kind = 1;
        if (p == 1)
            moving_kind = 2;
    }
    if (I32(car, 0x11c) != -1) {
        if (U8(((uint8_t **)g_track[0])[U32(car, 0x11c) & 0xff], 4) & 2) {
            if (g_music_player == -1 && U8(car, 0x70c) != 8) {
                g_music_player = p;
                music_mode(1, 0x400, 0x7fff);
            }
            I32(car, 0x6a4) = 1;
        } else {
            if (g_music_player == p && U8(car, 0x70c) != 8) {
                g_music_player = -1;
                music_mode(2, 0x400, 0);
            }
            I32(car, 0x6a4) = 0;
        }
    }

    if (airborne) {
        if (I32(g_race, 0x57c) < 4)
            revs = I32(car, 0x6a0) >> 4;
        else if (I32(car, 0x504) > I32(car, 0x5c8))
            revs = I32(car, 0x504) >> 8;
        else
            revs = I32(car, 0x5c8) >> 8;
        if (S16(e, E_AIR) > 0)
            S16(e, E_AIR) = (int16_t)(S16(e, E_AIR) - 2);
        else
            S16(e, E_AIR) = 0;
    } else {
        if (revs < 9000 && rev_boost > 0x20)
            revs += rev_boost;
        else if (revs > 0)
            revs -= 100;
        else
            revs = 0;
        S16(e, E_AIR) = (int16_t)(S16(e, E_AIR) + 1);
    }
    if (S16(e, E_AIR) == 0 && I32(g_race, 0x57c) >= 4) {
        int32_t top = (int32_t)((uint32_t)level & 0xffff0000);

        if (top > 0x32c80000 && !U8(e, E_SHIFT) && S8(e, E_GEAR) < 2) {
            pad_effect(car + 0x6f8, 200, 1, 0x46, 0, 10);
            U8(e, E_GEAR) = (uint8_t)(U8(e, E_GEAR) + 1);
            U8(e, E_SHIFT) = (uint8_t)(U8(e, E_GEAR) << 2);
        }
        if (top < 0x1f400000 && revs > 0 && S8(e, E_GEAR) > 0) {
            U8(e, E_GEAR) = (uint8_t)(U8(e, E_GEAR) - 1);
            pad_effect(car + 0x6f8, 0, 0, 1, 3, 2);
        }
        if (revs < 0)
            U8(e, E_GEAR) = 0;
    }

    loud = 1;
    if ((I32(g_race, 0x57c) > 4 && (U8(car, 0x71a) || U8(car, 0x6fa))) || ABS(revs) < 500)
        loud = 0;
    {
        int32_t target = ((I32(car, 0x5c8) >> 8) + (I32(car, 0x504) >> 8)) >> 1, smooth = S16(e, E_SMOOTH);

        if (target > smooth)
            S16(e, E_SMOOTH) = (int16_t)(((target - smooth) >> 1) + smooth);
        else if (smooth >= 100)
            S16(e, E_SMOOTH) = (int16_t)(smooth - 100);
    }
    if (U8(e, E_SHIFT)) {
        U8(e, E_SHIFT) = (uint8_t)(U8(e, E_SHIFT) - 1);
        if (U8(e, E_SHIFT) == 1 && U8(slot, 0x268))
            ff_kick(S16(slot, 0x26c), 100000, 2000, 100000);
        level -= 0x1000000;
    } else if (!loud) {
        level -= 0x1000000;
    } else {
        level = (int32_t)((uint32_t)ABS(revs) * (uint32_t)g_gear_volumes[S8(e, E_GEAR)]);
    }
    if ((int32_t)((uint32_t)level & 0xffff0000) > 0x3e800000)
        level = 0x3e800000;
    if ((int32_t)((uint32_t)level & 0xffff0000) <= 0)
        level = 0;
    I32(e, E_LEVEL) = level;
    S16(e, E_REVS) = (int16_t)revs;
    S16(e, E_LOUD) = (int16_t)(level >> 16);
    half = (int16_t)(level >> 16) >> 1;

    if (U8(slot, 0x268)) {
        int32_t clipped = half < 0x1964 ? half : 0x1964, facing[3], along;
        uint32_t bump;
        uint8_t *own = SLOT_CAR(slot);

        ff_engine(S16(slot, 0x26c), 100000 - clipped * 13, half / 5 + 0x5dc);
        facing[0] = SHL(S16(car, 0xc4), 4);
        facing[1] = SHL(S16(car, 0xca), 4);
        facing[2] = SHL(S16(car, 0xd0), 4);
        along = dot(own + 0x670, facing);
        bump = U8(own, 0x65f) * 250u;
        if (bump >= 10000)
            bump = 10000;
        ff_road(S16(slot, 0x26c), along <= 0 ? 90000 : 27000, (int32_t)bump);
    }

    voice = U8(e, E_MOTOR_VOICE);
    if (voice == 0x7f) {
        U8(e, E_MOTOR_VOICE) = (uint8_t)sound_moving(moving_kind, 0, 9, (uint32_t)((g_engine_pitch + half) >> 1),
                                                       12000, car + 0x34, car + 0x68);
    } else {
        voice_reverb(voice, U8(car, 0x6a4));
        voice_pitch(byte_into(warp, U8(e, E_MOTOR_VOICE)), (uint32_t)(((g_engine_pitch + half) >> 1) - (int32_t)warp + 0x80));
        {
            uint32_t volume = 10000u - (uint16_t)(int16_t)(S16(e, E_LOUD) >> 2);

            voice_volume(byte_into(volume, U8(e, E_MOTOR_VOICE)), volume, volume);
        }
    }

    {
        int32_t smooth = S16(e, E_SMOOTH);

        if (smooth >= 16000)
            smooth = 16000;
        voice = U8(e, E_ENGINE_VOICE);
        if (voice == 0x7f) {
            U8(e, E_ENGINE_VOICE) = (uint8_t)sound_play(1, 9, 0, 9000, car + 0x34);
        } else {
            uint32_t volume = (uint32_t)smooth * 2;

            if ((uint16_t)volume > 9000)
                volume = 9000;
            voice_reverb(voice, U8(car, 0x6a4));
            voice_pitch(byte_into(smooth < 0 ? 0xffffff00u : 0, U8(e, E_ENGINE_VOICE)),
                        (uint32_t)(ABS(smooth >> 3) - (int32_t)(warp >> 1)));
            voice_volume(U8(e, E_ENGINE_VOICE), volume, volume);
        }
    }

    if (spin > 0x1e0000)
        spin = 0x1e0000;
    if (S16(e, E_GROUND) && airborne)
        S16(e, E_GROUND) = (int16_t)(S16(e, E_GROUND) - 1);
    else
        S16(e, E_GROUND) = 0;
    {
        const uint8_t *row = g_surface_sounds + surface * 0x14;
        uint32_t start_sound = U32(row, 0), loop_sound = U32(row, 4), volume = U32(row, 8);
        int32_t pitch = I32(row, 0xc), always = I32(row, 0x10);
        int32_t top = (int32_t)((uint32_t)spin & 0xffff0000);
        uint32_t v;

        if (top < 0x40000 && U8(e, E_SURFACE_VOICE) != 0x7f && !always) {
            voice_stop(byte_into(loop_sound, U8(e, E_SURFACE_VOICE)));
            U8(e, E_SURFACE_VOICE) = 0x7f;
            S16(e, E_SKID) = 5;
        } else {
            v = U8(e, E_SURFACE_VOICE);
            if (top <= 0x60000 && !always) {
                U8(e, E_SURFACE_VOICE) = (uint8_t)v;
            } else {
                if (top > 0x60000 && U8(car, 0x70c) == 10)
                    pad_effect(car + 0x6f8, (uint32_t)(int8_t)((((spin >> 18) & 2) | 1) + 4) * 10, 1, 0x28, 0, 8);
                if (S16(e, E_SKID) > 1) {
                    if (S16(e, E_SKID) == 5)
                        sound_play(start_sound, 7, (uint32_t)pitch, 0, car + 0x34);
                    if (S16(e, E_SKID) > 1)
                        S16(e, E_SKID) = (int16_t)(S16(e, E_SKID) - 1);
                } else if (v == 0x7f) {
                    v = sound_play(loop_sound, 9, (uint32_t)pitch, 0, car + 0x34) & 0xff;
                }
                if (v != 0x7f) {
                    if (surface != S8(e, E_LAST_SURFACE) && S16(e, E_SKID) == 1) {
                        if (v != 0x7f)
                            voice_stop(v);
                        v = sound_play(loop_sound, 9, (uint32_t)pitch, volume, car + 0x34) & 0xff;
                    }
                    if (v != 0x7f) {
                        voice_reverb(v, U8(car, 0x6a4));
                        if (top > 0x60000) {
                            voice_pitch(v, (uint32_t)(ABS(revs >> 6) + (pitch - (int32_t)warp)));
                            voice_volume(v, volume, volume);
                        } else if (always) {
                            uint32_t quiet = (uint32_t)((ABS(revs) * 3) >> 2);

                            voice_pitch(v, (uint32_t)(pitch - (int32_t)warp));
                            voice_volume(v, quiet, quiet);
                        }
                    }
                }
                U8(e, E_LAST_SURFACE) = (uint8_t)surface;
                U8(e, E_SURFACE_VOICE) = (uint8_t)v;
            }
        }
    }

    {
        uint32_t boom = U8(car, 0x65f);

        if (boom & 0xf0)
            g_boom_voice = sound_play(0x13, 7, boom * 8 + 0x100, boom << 8, car + 0x34);
    }
    U8(car, 0x660) = 0;
    U8(car, 0x65f) = 0;
}

/* 0x444120: one simulation step. Every callee is called through its
 * address so the hooks pick the C version or the original. `step` of
 * `count` steps this frame: the last one draws. 0 when the race ends. */
int32_t GAMEAPI sim_step(int32_t network, int32_t step, int32_t count)
{
    int32_t local = ((int_fn)0x48cef0)(), i, n;
    uint8_t *slot, *local_car;

    (void)network;
#ifdef REROLL_NATIVE
    {
        /* REROLL_DEBUG_WARP=n: car 1 fires a time warp every n steps (profiling) */
        static int every = -1, steps;
        if (every < 0) {
            const char *e = getenv("REROLL_DEBUG_WARP");
            every = e ? atoi(e) : 0;
        }
        if (every > 0 && U8(g_race, 0x54d) > 1 && ++steps % every == 0)
            ((int32_t(GAMEAPI *)(const uint8_t *, int32_t))0x479890)(g_car_slots + CAR_SLOT_SIZE, 0);
    }
#endif
    for (i = 0; i < U8(g_race, 0x54d); i++)
        ((ptr_fn)0x48e7b0)(SLOT_CAR(g_car_slots + i * CAR_SLOT_SIZE));
    for (i = 0; i < U8(g_race, 0x54d); i++) {
        uint8_t *car = SLOT_CAR(g_car_slots + i * CAR_SLOT_SIZE);

        if (U8(car, 0x70c) == 8)
            ((ptr_fn)0x4015e0)(car);
    }
    for (i = 0; i < U8(g_race, 0x54d); i++) {
        uint8_t *car = SLOT_CAR(g_car_slots + i * CAR_SLOT_SIZE);

        if (U8(car, 0x70c) == 8)
            ((ptr_fn)0x425ad0)(car);
    }
    for (i = 0; i < U8(g_race, 0x54d); i++)
        ((ptr_int_fn)0x473250)(SLOT_CAR(g_car_slots + i * CAR_SLOT_SIZE), i);
    for (i = 0, slot = g_car_slots; i < U8(g_race, 0x54d); i++, slot += CAR_SLOT_SIZE) {
        ((ptr_int_fn)0x41b520)(SLOT_CAR(slot), i);
        ((ptr_fn)0x41c7a0)(slot);
    }
    ((void_fn)0x48e7e0)();
    for (i = 0; i < U8(g_race, 0x54d); i++)
        ((ptr_fn)0x41b7d0)(SLOT_CAR(g_car_slots + i * CAR_SLOT_SIZE));
    ((void_fn)0x41ac50)();
    ((void_fn)0x43e570)();
    ((void_fn)0x43d350)();
    ((void_fn)0x4112e0)();
    ((world_step_fn)0x461410)(0x10000);
    ((void_fn)0x442840)();
    ((void_fn)0x468450)();
    for (i = 0, slot = g_car_slots; i < U8(g_race, 0x54d); i++, slot += CAR_SLOT_SIZE) {
        ((ptr_fn)0x438620)(slot);
        ((ptr_fn)0x47aa00)(slot);
    }
    if (U8(g_config, 0x2a) && ((int_fn)0x46e3c0)())
        ((void_fn)0x46d0c0)();
    if (U8(g_config, 0x28) && ((int_fn)0x4906c0)())
        ((void_fn)0x486540)();
    ((void_fn)0x48dc40)();
    ((void_fn)0x47ab40)();
    ((void_fn)0x458010)();
    ((void_fn)0x46a9c0)();
    for (i = 0, slot = g_car_slots; i < U8(g_race, 0x54d); i++, slot += CAR_SLOT_SIZE) {
        ((ptr_fn)0x40de60)(SLOT_CAR(slot));
        ((ptr_fn)0x406e30)(slot);
    }
    ((void_fn)0x47b8f0)();
    ((void_fn)0x462840)();
    n = ((int_fn)0x40d040)();
    for (i = 0; i < n; i++)
        ((int_arg_fn)0x408360)(i);
    ((void_fn)0x484080)();
    ((void_fn)0x405140)();
    ((void_fn)0x42b990)();
    ((void_fn)0x46c300)();
    ((void_fn)0x47c210)();
    ((void_fn)0x481f20)();
    ((void_fn)0x4751b0)();
    ((void_fn)0x43e270)();
    ((void_fn)0x405f70)();
    ((void_fn)0x46bfa0)();
    ((void_fn)0x443180)();
    if (step == count - 1) {
        ((void_fn)0x443b50)(); /* race_draw */
        ((void_fn)0x442890)();
        local_car = SLOT_CAR(g_car_slots + local * CAR_SLOT_SIZE);
        ((particles_fn)0x441450)(U32(local_car, 0x700), U32(local_car, 0x6fc), 0);
        ((void_fn)0x41c2c0)();
    } else {
        ((void_fn)0x442890)();
        local_car = SLOT_CAR(g_car_slots + local * CAR_SLOT_SIZE);
        ((particles_fn)0x441450)(U32(local_car, 0x700), U32(local_car, 0x6fc), 1);
    }
    ((void_fn)0x472040)();
    if (((int_fn)0x46a530)())
        return 0;
    g_sim_steps++;
    g_race_steps++;
    return 1;
}
