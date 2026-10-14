/* In-race leftovers, group GC:
 *
 *  - dynamic lights (particles_update 0x441450 and its per-kind helpers:
 *    shape-animated colour, fading flashes, lights riding objects, the
 *    car's head light 0x4418a0), the light list countdown and the queued
 *    light spawns;
 *  - wheel dust 0x4219e0, the skid marks 0x46ecb0-0x46efa0;
 *  - effect triggers (0x41ac20, 0x45bd30, 0x45be10, 0x4599c0, the blast
 *    mark 0x46d760), the trigger queue 0x442890 and its actions
 *    0x442c20-0x442f20;
 *  - rumble 0x41c2c0, the step clock 0x43693f / 0x436958;
 *  - the pause screen: the per-frame check 0x463b70, its input handler
 *    0x464030, opening it 0x463ca0, the camera choice 0x469bb0;
 *  - the snatch effect's mesh 0x48f930, the object light point 0x442c90.
 *
 * The x87 parts are inline x87 asm with the original's instructions and
 * memory operand widths (the body matrix to floats, int to float through a
 * memory temporary, one float compare). Still original, called by address:
 * the HUD sliders 0x464c10 / 0x464a20 (sprite building, x87), the float
 * transform 0x426620, the light list helpers 0x441220 / 0x4413a0 /
 * 0x441370, the menu page switch 0x464650, network, CD and force feedback,
 * drawing. */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S8(p, off) (*(int8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define PTR(p, off) GP((uint8_t *)(uintptr_t)(p) + (off))
#define PTR_SET(p, off, v) (U32(p, off) = (uint32_t)(uintptr_t)(v))
#define A(x) ((uint32_t)(uintptr_t)(x))
/* the "none" pointer (-1) some pointer fields hold */
#define NO_PTR ((uint8_t *)(uintptr_t)0xffffffffu)
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define S16G(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define I32G(a) (*(int32_t *)(uintptr_t)(a))

#define g_race GP(0x4b1960)
#define g_settings GP(0x4b2198)
#define g_seed (*(uint32_t *)0x4b11d8)
#define g_seed2 (*(uint32_t *)0x4b11dc)
#define g_track_info GP(0x6c1eb0)
#define g_textures GP(0x6c1e14)
#define g_screen_scale (*(int32_t *)0x6cd410)
#define g_detail_flags (*(uint8_t *)0x6ccd84)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_wave ((const int16_t *)0x4b24f8)
#define g_wave2 ((const int16_t *)0x4b2578)

/* Lights: 0x40-byte records at 0x5d8000 (index 0 unused), count *0x5d7fe0. */
#define g_lights ((uint8_t *)0x5d8000)
#define g_light_count (*(uint32_t *)0x5d7fe0)
#define LIGHT(i) (g_lights + (uint32_t)(i) * 0x40)
/* Light countdown list (u32: top 3 bits a counter), 0x5d71e0, count *0x5d7fe4. */
#define g_light_ages ((uint32_t *)0x5d71e0)
#define g_light_age_count (*(uint32_t *)0x5d7fe4)
/* Queued light spawns: 0x24-byte records from 0x5d7300, count *0x5d72e0. */
#define g_light_queue ((uint8_t *)0x5d7300)
#define g_light_queue_count (*(uint32_t *)0x5d72e0)
/* Fixed lights' path positions (8 bytes: s16 x, y, z, u16 moved), light i at i - 1. */
#define g_light_paths ((uint8_t *)0x6c1b88)

/* Trigger queue: u32 objects at 0x6b9ba0, count *0x6b9cb8. */
#define g_trigger_queue ((uint32_t *)0x6b9ba0)
#define g_trigger_count (*(uint32_t *)0x6b9cb8)

/* Step clock. */
#define g_step_clock_base (*(uint32_t *)0x4be390)
#define g_steps_done (*(uint32_t *)0x4be3e0)

/* Pause screen. */
#define g_paused (*(uint32_t *)0x6b8d60)
#define g_pause_car GP(0x6b8d80) /* the paused car's slot (its car at +0) */
#define g_pause_page (*(int16_t *)0x6b8d64)
#define g_pause_item (*(int16_t *)0x6b8d66)
#define g_pause_blink (*(int32_t *)0x6b8d68)
#define g_pause_x (*(uint32_t *)0x6b8d6c)
#define g_pause_y (*(uint32_t *)0x6b8d70)
#define g_pause_pad (*(uint32_t *)0x6b8d74)
#define g_pause_pressed (*(uint32_t *)0x6b8d78)
#define g_pause_pad_old (*(uint32_t *)0x6b8d7c)
#define g_pause_cd_volume (*(int32_t *)0x6b8d84)
#define g_pause_volume (*(int32_t *)0x6b8d88)
#define g_pause_items (*(int16_t *)0x6b9584)
#define g_pause_item_on ((uint8_t *)0x6b9586)
#define g_pause_return (*(uint32_t *)0x4d1198)

/* Skid marks: 0x124-byte records, *0x6002d8 next, from *0x6002d4 to *0x6002d0. */
#define g_skid_next GP(0x6002d8)
#define g_skid_next_set(v) (G32(0x6002d8) = A(v))
#define g_skid_end GP(0x6002d0)
#define g_skid_first GP(0x6002d4)

/* GTE-style scratch. */
#define g_gte_matrix ((uint32_t *)0x6ccbe0)
#define g_gte_vector ((uint32_t *)0x6ccc64)
#define g_gte_result ((const int32_t *)0x6cccc8)
#define g_gte_offset ((int32_t *)0x6ccbf4)

/* Called by address (arguments may be game addresses or pointers). */
#define game_time() GCALL(0x43692a, game_time_get)()
#define surface_kind_at(car) ((uint32_t)GCALL(0x40d6f0, surface_kind)((uint8_t *)GMEM(A(car))))
#define rumble_set_a(joy, v) GCALL(0x489ab0, ff_spring_a_set)(joy, (int32_t)(v)) /* force feedback effects */
#define rumble_set_b(joy, v) GCALL(0x489bc0, ff_spring_b_set)(joy, (int32_t)(v))
#define light_spawn_queued(pos, a, b) GCALL(0x441220, light_slot_new)((int32_t *)GMEM(A(pos)), A(a), b)
#define light_move(n, pos) GCALL(0x4413a0, light_reposition)(n, (const int32_t *)GMEM(A(pos)))
#define light_free(n) GCALL(0x441370, light_slot_free)(n)
#define body_script_step_at(body) GCALL(0x462e80, body_script_step)((uint8_t *)GMEM(A(body)))
#define gte_rotate_at() GCALL(0x428090, gte_rotate)()
#define gte_transform_float() GCALL(0x426620, gte_rt_float)() /* x87: 0x6ccc64 by the float matrix + offset */
#define particle_spawn_at(pos, vel, kind) \
    GCALL(0x462690, particle_spawn)((const int32_t *)GMEM(A(pos)), (int32_t *)GMEM(A(vel)), (int32_t)(kind))
#define glint_spawn_at(p, type, fade, spread, flags) \
    GCALL(0x442f70, glint_spawn)((const int16_t *)GMEM(A(p)), type, fade, spread, flags)
#define object_type_set(obj, n, value) GCALL(0x426060, object_trigger_set)((uint8_t *)GMEM(A(obj)), n, value)
#define explosion_effects_at(at, dir, size, what, sound) \
    GCALL(0x42aa30, explosion_effects)((const int32_t *)GMEM(A(at)), (const int32_t *)GMEM(A(dir)), size, what, sound)
/* (the original passes a third argument, unread) */
#define fix_mul8_at(a, b, unused) ((void)(unused), GCALL(0x4291a0, fix_mul8)((int32_t)(a), (int32_t)(b)))
#define sound_play_flagged_at(a1, a2, a3, a4, at, flagged) \
    GCALL(0x471d50, sound_play_flagged)(a1, a2, a3, a4, A(at), flagged)
#define sound_play_at(a1, a2, a3, a4, at) GCALL(0x471c90, sound_play_body)(a1, a2, a3, a4, A(at))
#define sound_play_volume_at(a1, a2, a3, a4, a5, vol) GCALL(0x472a40, sound_play_volume)(a1, a2, a3, a4, a5, vol)
#define sound_cd_volume_keep_at(l, r) GCALL(0x471b30, sound_cd_volume_keep)(l, r)
#define sound_volume_set_at(v) GCALL(0x471b80, sound_volume_set)(v)
#define sound_cd_pause_at(r) GCALL(0x471c30, sound_cd_pause)(r)
#define snd_resume_all_at() GCALL(0x46fe90, snd_resume_all)()
#define mixer_thunk() GCALL(0x472040, sound_mix_jump)()
#define vec_length_at(v) ((uint32_t)GCALL(0x480cd0, vec_length)((const int32_t *)GMEM(A(v))))
#define vec_div_at(v, d) GCALL(0x486e90, vec_div)((int32_t *)GMEM(A(v)), (int32_t)(d))
#define vec_cross_at(a, b, out) \
    GCALL(0x4806a0, vec_cross)((const int32_t *)GMEM(A(a)), (const int32_t *)GMEM(A(b)), (int32_t *)GMEM(A(out)))
#define vec_dot_at(a, b) \
    ((uint32_t)GCALL(0x40d950, vec_dot)((const int32_t *)GMEM(A(a)), (const int32_t *)GMEM(A(b))))
#define snd_box_merge_at(holder, box) \
    ((uint32_t)GCALL(0x46f080, snd_box_merge)((uint8_t *)GMEM(A(holder)), (uint8_t *)GMEM(A(box))))
#define car_stats_set_at(car, index, kind, special) \
    GCALL(0x40eae0, car_stats_set)((uint8_t *)GMEM(A(car)), (int32_t)(index), (int32_t)(kind), special)
#define local_car_index_at() ((uint32_t)GCALL(0x48cef0, local_car_index)())
#define net_local_slot_at() GCALL(0x48cf20, net_local_slot)()
#define net_state_game_at() GCALL(0x45ef80, net_state_game)()
#define net_state_host_at() GCALL(0x45eed0, net_state_host)()
#define net_player_active_at(player) GCALL(0x48cfd0, fe_net_player_active)((int32_t)(player))
#define net_frame() GCALL(0x460a20, net_all_paused)()
#define net_pause_state() ((uint32_t)GCALL(0x45fde0, net_results_check)())
#define net_pause_set(v) GCALL(0x45f990, net_results_send)(v)
#define net_race_abort() GCALL(0x45d170, fe_net_race_abort)()
#define net_unpause(slot) GCALL(0x48bed0, net_player_remove)((int32_t)(slot))
#define net_message(victim, attacker, kind) GCALL(0x4600a0, net_kill_message)(victim, attacker, kind)
#define net_drop_player(player) GCALL(0x48bf70, fe_net_drop_player)(player)
/* (the packet pointer's slot is game memory: car +0x71c) */
#define controls_fetch_at(player, raw, packet) \
    GCALL(0x45e8c0, controls_fetch_packet)((int32_t)(player), (uint8_t *)GMEM(A(raw)), (uint32_t *)GMEM(A(packet)))
#define cd_present() ((uint32_t)GCALL(0x4977b0, cd_volume_available)())
#define race_draw_at() GCALL(0x443b50, race_draw_body)()
#define screen_present() GCALL(0x423f20, view_full_screen)()
#define pause_draw() GCALL(0x463f90, pause_menu_draw)()
/* x87 sprite building */
#define pause_hud_slider(item, kind, angle) GCALL(0x464c10, pause_menu_slider_set)(item, kind, angle)
#define pause_menu_page(kind, sel) GCALL(0x464650, pause_menu_open)(kind, sel)
#define replay_state_clear_at() GCALL(0x463990, replay_state_clear)()

/* imul: the high dword of the signed product. */
static inline int32_t imul_hi(int32_t a, int32_t b)
{
    return (int32_t)(uint32_t)((uint64_t)((int64_t)a * b) >> 32);
}

static inline uint32_t seed_next(uint32_t s)
{
    return s * 16807u & 0x7fffffff;
}

/* ---- x87 helpers: each is one block of the original's instructions. ---- */

/* fild word [src]; fstp dword [tmp]; fld dword [tmp]; fmul dword [0x4a3310];
 * fstp dword [dst] */
static void fix_to_float(uint32_t src, uint32_t dst)
{
    float tmp;
    __asm__ volatile("filds (%1)\n\t"
                     "fstps %0\n\t"
                     "flds %0\n\t"
                     "fmuls 0x4a3310\n\t"
                     "fstps (%2)"
                     : "=m"(tmp)
                     : "r"(GMEM(src)), "r"(GMEM(dst))
                     : "st", "memory");
}

/* The body's int16 matrix (+0xc4) to 0x6ccbe0 and as floats (scaled by
 * *0x4a3310) to the float transform's matrix. */
static void body_matrix_to_float(const uint8_t *body)
{
    static const uint16_t dst[9] = {0xcd1c, 0xcd20, 0xcd24, 0xcd2c, 0xcd30, 0xcd34, 0xcd3c, 0xcd40, 0xcd44};
    for (int i = 0; i < 5; i++)
        g_gte_matrix[i] = U32(body, 0xc4 + i * 4);
    for (int i = 0; i < 9; i++)
        fix_to_float(0x6ccbe0 + i * 2, 0x6c0000 + dst[i]);
}

/* mov [tmp], v; fild dword [tmp]; fstp dword [dst] */
static void int_to_float(int32_t v, uint32_t dst)
{
    __asm__ volatile("fildl %0\n\t"
                     "fstps (%1)"
                     :
                     : "m"(v), "r"(GMEM(dst))
                     : "st", "memory");
}

/* The translation (position >> 16) as ints at 0x6ccbf4 and floats at 0x6ccd0c. */
static void body_offset_to_float(int32_t x, int32_t y, int32_t z)
{
    g_gte_offset[0] = x;
    g_gte_offset[1] = y;
    g_gte_offset[2] = z;
    int_to_float(g_gte_offset[0], 0x6ccd0c);
    int_to_float(g_gte_offset[1], 0x6ccd10);
    int_to_float(g_gte_offset[2], 0x6ccd14);
}

/* fild qword (a u32 with a zero high dword); fstp dword */
static uint32_t fild_q(uint32_t v)
{
    uint64_t q = v;
    uint32_t r;
    __asm__ volatile("fildll %1\n\t"
                     "fstps %0"
                     : "=m"(r)
                     : "m"(q)
                     : "st");
    return r;
}

/* mov [tmp], v; fild dword [tmp]; fstp dword [tmp] */
static uint32_t fild_d(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\t"
                     "fstps %0"
                     : "=m"(r)
                     : "m"(v)
                     : "st");
    return r;
}

/* fld dword [a]; fadd dword [b]; fstp dword [dst] */
static void fadd_store(uint32_t a, const void *b, void *dst)
{
    __asm__ volatile("flds %0\n\t"
                     "fadds (%1)\n\t"
                     "fstps (%2)"
                     :
                     : "m"(a), "r"(b), "r"(dst)
                     : "st", "memory");
}

/* fld dword [a]; fcomp dword [b]; fnstsw ax */
static uint16_t fcomp_status(const void *a, const void *b)
{
    uint16_t sw;
    __asm__ volatile("flds (%1)\n\t"
                     "fcomps (%2)\n\t"
                     "fnstsw %0"
                     : "=a"(sw)
                     : "r"(a), "r"(b)
                     : "st", "cc", "memory");
    return sw;
}

/* ---- small helpers ---- */

/* 0x403950: for the first two entries of the pair table 0x4b5e58 (count
 * *0x4b5fa0) with this key (the first match and the one right after it, if
 * it matches too), decrements the byte +3 of their 12-byte record in
 * *(*0x6c1e30 + 0x60) unless 0. */
void GAMEAPI pair_users_release(uint32_t key)
{
    uint32_t n = G32(0x4b5fa0), i = 0;
    uint8_t *b;

    if (n != 0) {
        for (i = 0; i < n; i++)
            if (G32(0x4b5e58 + i * 8) == key)
                break;
        if (i < n) {
            b = PTR(G32(0x6c1e30), 0x60) + G32(0x4b5e5c + i * 8) * 12 + 3;
            if (*b != 0) {
                *b = *b - 1;
                n = G32(0x4b5fa0);
            }
        }
    }
    i++;
    if (i >= n || G32(0x4b5e58 + i * 8) != key)
        return;
    b = PTR(G32(0x6c1e30), 0x60) + G32(0x4b5e5c + i * 8) * 12 + 3;
    if (*b != 0)
        *b = *b - 1;
}

/* 0x41ac20: for an object of type 0x1a, 0x426060 (n below 100). */
void GAMEAPI object_trigger_1a(uint8_t *obj, uint32_t n, uint32_t value)
{
    if (n < 100 && (U32(obj, 0) & 0x1ff) == 0x1a)
        object_type_set(obj, n, value);
}

/* 0x426060: n == 2: +0x180 = 3; else the word +0x184 = value. */
void GAMEAPI object_trigger_set(uint8_t *obj, uint32_t n, uint32_t value)
{
    if (n == 2)
        U32(obj, 0x180) = 3;
    else
        U16(obj, 0x184) = (uint16_t)value;
}

/* 0x41c2c0: per human car, the force feedback by surface roughness (wheels'
 * surface +0xc bits 8-12, 0x1a without a wheel surface, 0x14 in the air)
 * and speed (+0x658). */
void GAMEAPI rumble_update(void)
{
    for (uint32_t i = 0; (int32_t)i < (int32_t)U8(g_race, 0x54d); i++) {
        uint8_t *slot = g_car_slots + i * CAR_SLOT_SIZE, *car;
        int32_t rough, t, speed, k, effect;

        if (U8(slot, 0x268) == 0)
            continue;
        car = PTR(slot, 0);
        rough = 0;
        if (surface_kind_at(car) == 0) {
            for (int w = 0; w < 4; w++) {
                uint8_t *s = PTR(car, 0x198 + w * 0x1c);
                rough += s ? (int32_t)((U32(s, 0xc) >> 8) & 0x1f) : 0x1a;
            }
            rough >>= 2;
            if (rough >= 0x14)
                rough = 0x14;
        } else {
            rough = 0x14;
        }
        t = rough * 20;
        t = (imul_hi(t, (int32_t)0xb21642c9) + t) >> 4;
        t = t + (int32_t)((uint32_t)t >> 31) + 2;
        speed = S16(car, 0x658) * 50;
        if (speed >= 10000)
            speed = 10000;
        k = 20 - t;
        effect = imul_hi(k * speed, 0x66666667) >> 3;
        effect += (int32_t)((uint32_t)effect >> 31);
        rumble_set_a((uint32_t)(int32_t)S16(slot, 0x26c), (uint32_t)effect);
        effect = imul_hi(k * 10000, 0x66666667) >> 3;
        effect += (int32_t)((uint32_t)effect >> 31);
        effect = ((1000 - S16(car, 0x658)) * effect) >> 10;
        rumble_set_b((uint32_t)(int32_t)S16(slot, 0x26c), (uint32_t)effect);
    }
}

/* 0x4219c0: 1 when *0x4bcd54 is 0. */
uint32_t GAMEAPI flag_4bcd54_clear(void)
{
    return G32(0x4bcd54) == 0;
}

/* 0x427d90: the packet's pause-menu bits to pad bits. */
uint32_t GAMEAPI pause_pad_bits(uint32_t b)
{
    uint32_t r = 0;
    if (b & 1)
        r = 0x1000;
    if (b & 2)
        r |= 0x4000;
    if (b & 4)
        r |= 0x8000;
    if (b & 8)
        r |= 0x2000;
    if (b & 0x10)
        r |= 0x40;
    if (b & 0x20)
        r |= 0x800;
    return r;
}

/* 0x43693f: the step clock starts now. */
void GAMEAPI sim_clock_start(void)
{
    g_step_clock_base = game_time();
    g_steps_done = 0;
}

/* 0x436958: steps of 33 ms due since the last call (at least 1; unsigned). */
uint32_t GAMEAPI sim_steps_due(void)
{
    uint32_t due = (game_time() - g_step_clock_base) / 0x21 - g_steps_done;
    g_steps_done += due;
    return due < 1 ? 1 : due;
}

/* ---- lights ---- */

/* 0x4416a0: a light with a colour shape steps through it (u16 15-bit
 * colours, 5 bits each, << 2). */
void GAMEAPI light_shape_step(uint8_t *light)
{
    const uint8_t *shape = PTR(light, 0x28);
    uint32_t i, c;

    if (shape == 0)
        return;
    i = (uint16_t)(U16(light, 0x2c) + 1);
    if (!((uint16_t)i < U16(light, 0x2e)))
        i = 0;
    U16(light, 0x2c) = (uint16_t)i;
    c = U16(shape, i * 2);
    U16(light, 0x12) = 2;
    U32(light, 0xc) = ((uint32_t)(uint8_t)(((c >> 10) & 0x1f) << 2) << 16) |
                      ((uint32_t)(uint8_t)(((c >> 5) & 0x1f) << 2) << 8) | (uint8_t)((c & 0x1f) << 2);
}

/* 0x441720: a fading light (+0x26 steps left): mode 2 a fixed orange, else
 * a flickering one; freed when it runs out. */
void GAMEAPI light_fade_step(uint8_t *light, uint32_t index, uint32_t mode)
{
    int32_t left, level, r;

    S16(light, 0x26) = (int16_t)(S16(light, 0x26) - 1);
    left = S16(light, 0x26);
    if (left <= 0) {
        light_free(index);
        return;
    }
    level = left << 2;
    U16(light, 0x12) = 2;
    if (level > 0x80)
        level = 0x80;
    if (mode == 2) {
        r = level * 3;
        r = (r + (int32_t)((uint32_t)(r >> 31) & 3)) >> 2;
        U32(light, 0xc) = (uint32_t)(r << 8) | (uint32_t)level;
        return;
    }
    g_seed = seed_next(g_seed);
    r = (int32_t)((((g_seed >> 15) * 96) >> 16) + 0x20) * level;
    r = (r + (int32_t)((uint32_t)(r >> 31) & 0x7f)) >> 7;
    U32(light, 0xc) = (uint32_t)(((r - (r >> 31)) >> 1) << 8) | (uint32_t)r;
}

/* 0x4417e0: a light on an object (+0x34) coloured by its +0x1b4..+0x1b6;
 * freed when the object's +0x188 is 0x10. */
void GAMEAPI light_object_step(uint8_t *light, uint32_t index)
{
    uint8_t *o = PTR(light, 0x34);

    if (I32(o, 0x188) == 0x10) {
        light_free(index);
        return;
    }
    U8(light, 0xc) = U8(o, 0x1b4) >> 3;
    U8(light, 0xd) = U8(o, 0x1b5) >> 3;
    U8(light, 0xe) = U8(o, 0x1b6) >> 3;
    light_move(index, o + 0x34);
}

/* 0x441840: the same coloured by +0x2a38..+0x2a3a / 2; freed when +0x188 is 0. */
void GAMEAPI light_object_step2(uint8_t *light, uint32_t index)
{
    uint8_t *o = PTR(light, 0x34);

    if (I32(o, 0x188) == 0) {
        light_free(index);
        return;
    }
    U8(light, 0xc) = U8(o, 0x2a38) >> 1;
    U8(light, 0xd) = U8(o, 0x2a39) >> 1;
    U8(light, 0xe) = U8(o, 0x2a3a) >> 1;
    light_move(index, o + 0x34);
}

/* colour channel + pulse, halved and clamped to 0..0xff */
static uint8_t head_light_channel(int32_t v)
{
    if ((v & ~1) >= 0x1fe)
        return 0xff;
    if ((v >> 1) <= 0)
        return 0;
    return (uint8_t)(v >> 1);
}

/* 0x4418a0: a car's head light: colour from +0x13f8..+0x13fa plus a pulse
 * (wave by +0x13fc, -150 while +0x13fd is odd), dimmed below energy
 * 0x400000 (+0x654), placed at (0, -20, -150) in the car's frame. */
void GAMEAPI light_head_step(uint8_t *light, uint32_t index)
{
    uint8_t *car = PTR(light, 0x34);
    uint32_t r0 = U8(car, 0x13f8), g0 = U8(car, 0x13f9), b0 = U8(car, 0x13fa);
    int32_t m, pulse, f;
    uint8_t r, g, b;
    int32_t pos[3];

    m = (int32_t)(r0 > g0 ? r0 : g0);
    if (!(m > (int32_t)b0))
        m = (int32_t)b0;
    pulse = ((g_wave[U8(car, 0x13fc)] >> 9) * m) >> 8;
    if (U8(car, 0x13fd) != 0 && (U8(car, 0x13fd) & 1))
        pulse = -150;
    r = head_light_channel((int32_t)r0 + pulse);
    g = head_light_channel((int32_t)g0 + pulse);
    b = head_light_channel((int32_t)b0 + pulse);
    if (I32(car, 0x654) < 0x400000) {
        f = (int32_t)((uint32_t)I32(car, 0x654) << 8);
        f = (f + (int32_t)((uint32_t)(f >> 31) & 0x3fffff)) >> 22;
        U8(light, 0xc) = (uint8_t)((r * f) >> 8);
        U8(light, 0xd) = (uint8_t)((g * f) >> 8);
        U8(light, 0xe) = (uint8_t)((b * f) >> 8);
    } else {
        U8(light, 0xc) = r;
        U8(light, 0xd) = g;
        U8(light, 0xe) = b;
    }
    body_matrix_to_float(car);
    g_gte_vector[0] = 0xffec0000u;
    g_gte_vector[1] = 0x0000ff6au;
    gte_rotate_at();
    pos[0] = g_gte_result[0] + I32(car, 0x34);
    pos[1] = g_gte_result[1] + I32(car, 0x38);
    pos[2] = g_gte_result[2] + I32(car, 0x3c);
    light_move(index, pos);
}

/* 0x441450: per step (third argument 0) the light countdowns: light +0x12,
 * the countdown list (top 3 bits; at 0 the entry is replaced by the last),
 * the queued spawns (0x441220, then position / timers copied); every time,
 * each dynamic light's kind (+0x24) step, then lights on objects (+0x30:
 * moved by the object's script and velocity) and moved path lights are
 * re-placed (0x4413a0). The first two arguments are not read. */
void GAMEAPI particles_update(uint32_t unused1, uint32_t unused2, uint32_t no_tick)
{
    uint32_t i;
    (void)unused1;
    (void)unused2;

    if (no_tick == 0) {
        uint8_t *end = LIGHT(g_light_count);
        uint32_t *p, *last;

        for (uint8_t *l = LIGHT(1); l < end; l += 0x40)
            if (U16(l, 0x12) != 0)
                U16(l, 0x12) = (uint16_t)(U16(l, 0x12) - 1);
        p = &g_light_ages[g_light_age_count - 1];
        last = p;
        for (; A(p) >= 0x5d71e0; p--) {
            uint32_t v = *p, top = v & 0xe0000000;
            if (top != 0)
                *p = (~v & 0x1fffffff) ^ (top - 1);
            if ((*p & 0xe0000000) == 0) {
                *p = *last;
                last--;
                g_light_age_count--;
            }
        }
        for (i = 0; i < g_light_queue_count; i++) {
            uint8_t *q = g_light_queue + 0xc + i * 0x24;
            /* the third argument is ax: the upper half is the count's */
            int32_t r = light_spawn_queued(q - 0xc, q,
                                           (g_light_queue_count & 0xffff0000u) | U16(q, 4));
            if (r != 0) {
                uint8_t *l = LIGHT(r);
                U16(l, 0x24) = U16(q, 6);
                U16(l, 0x26) = U16(q, 8);
                for (int k = 0; k < 3; k++)
                    U32(l, 0x34 + k * 4) = U32(q, 0xc + k * 4);
            }
        }
        g_light_queue_count = 0;
    }
    for (i = 1; i < g_light_count; i++) {
        uint8_t *l = LIGHT(i);
        switch (U16(l, 0x24)) {
        case 1:
            light_shape_step(l);
            break;
        case 2:
            light_fade_step(l, i, 2);
            break;
        case 3:
            light_fade_step(l, i, 3);
            break;
        case 5:
            light_object_step(l, i);
            break;
        case 7:
            light_object_step2(l, i);
            break;
        case 8:
            light_head_step(l, i);
            break;
        }
    }
    for (i = 1; i < g_light_count; i++) {
        uint8_t *l = LIGHT(i), *o, *path = g_light_paths + (i - 1) * 8;
        if (S16(l, 0x24) != 1)
            continue;
        o = PTR(l, 0x30);
        if (o != 0) {
            body_script_step_at(o);
            I32(o, 0x34) += I32(o, 0x68);
            I32(o, 0x38) += I32(o, 0x6c);
            I32(o, 0x3c) += I32(o, 0x70);
            light_move(i, o + 0x34);
        } else if (U16(path, 6) == 1) {
            int32_t v[3];
            v[0] = (int32_t)((uint32_t)(int32_t)S16(path, 0) << 16);
            v[1] = (int32_t)((uint32_t)(int32_t)S16(path, 2) << 16);
            v[2] = (int32_t)((uint32_t)(int32_t)S16(path, 4) << 16);
            light_move(i, v);
            U16(path, 6) = 0;
        }
    }
}

/* ---- the trigger queue ---- */

/* 0x442e40: action 2: an object (0x5d6e00 table) with +0x10c set takes
 * light colours (light code + 1, << 1) into +0x108 from the trigger's
 * light actions (kind 1). The low word of `code`'s slot is reused (sic). */
void GAMEAPI trigger_light_colour(uint8_t *obj, uint32_t code)
{
    uint8_t *target = GP(0x5d6e00 + (code & 0xfff) * 4);

    if (U8(target, 0x10c) == 0)
        return;
    for (int i = 0; i < 5; i++) {
        uint32_t c = (code & 0xffff0000u) | U16(obj, 0x16a + i * 2);
        uint32_t kind = c & 0xf000;
        if (kind == 0)
            return;
        if (kind == 0x1000)
            U32(target, 0x108) = U32(LIGHT(c & 0xfff), 0x4c) << 1;
    }
}

/* 0x442eb0: action 3: every third time, a glint from track point (info
 * +0x108, 12 bytes) with its colour byte +0xb. */
void GAMEAPI trigger_glint(uint32_t code)
{
    uint8_t *e = PTR(g_track_info, 0x108) + (code & 0xfff) * 12, *g;
    uint32_t c, k;

    U16(e, 6) = (uint16_t)(U16(e, 6) + 1);
    if (S16(e, 6) < 3)
        return;
    c = U8(e, 0xb);
    U16(e, 6) = 0;
    k = c & ~0x10u;
    g = glint_spawn_at(e, 0x1b, (uint32_t)-(int32_t)(k << 17), 0xe, c);
    U8(g, 0x95) = (uint8_t)(((uint8_t)k + 3) << 2);
    U16(g, 0x96) = 0x28;
}

/* 0x442f20: action 4: a glint from track point (info +0x10c). */
void GAMEAPI trigger_glint2(uint32_t code)
{
    uint8_t *e = PTR(g_track_info, 0x10c) + (code & 0xfff) * 12, *g;
    uint32_t c = U8(e, 0xb);

    g = glint_spawn_at(e, 0x1c, 0x4000, 0x11, c);
    U16(g, 0x96) = 8;
    U8(g, 0x95) = (uint8_t)(((uint8_t)(c & 0xef) + 0x28) << 1);
}

/* 0x442c20: an object's trigger actions (+0x16a, up to 5 words: kind in
 * the top 4 bits, 0 ends). The object pointer's slot gets the word (sic). */
void GAMEAPI trigger_fire(uint8_t *obj)
{
    for (int i = 0; i < 5; i++) {
        uint32_t c = (A(obj) & 0xffff0000u) | U16(obj, 0x16a + i * 2);
        if ((c & 0xf000) == 0)
            return;
        switch ((c & 0xffff) >> 12) {
        case 2:
            trigger_light_colour(obj, c);
            break;
        case 3:
            trigger_glint(c);
            break;
        case 4:
            trigger_glint2(c);
            break;
        }
    }
}

/* 0x442890: the trigger queue: duplicates dropped (when race +0x552), then
 * each fired; emptied. */
void GAMEAPI trigger_queue_process(void)
{
    uint32_t n, i;

    if (U8(g_race, 0x552) != 0) {
        n = g_trigger_count;
        G32(0x6b9b94) = 0;
        if (n != 0) {
            for (i = 0; i < n; i++) {
                uint32_t v = g_trigger_queue[i];
                uint32_t *last = &g_trigger_queue[n - 1];
                for (uint32_t j = 0; j < n; j++) {
                    if (i != j && v == g_trigger_queue[j]) {
                        g_trigger_queue[j] = *last;
                        n--;
                        last--;
                    }
                }
            }
            g_trigger_count = n;
        }
    }
    for (i = 0; i < g_trigger_count; i++)
        trigger_fire((uint8_t *)GMEM(g_trigger_queue[i]));
    g_trigger_count = 0;
}

/* ---- effects ---- */

/* 0x46d760: a blast on a car: the first time (record *(car +0x6ac) byte 3
 * clear) a sound and its hit counter; then the blast direction in the car's
 * frame picks a damage cell (byte table *0x4d44a8) that takes `level`. */
uint32_t GAMEAPI blast_mark(uint8_t *car, const int32_t *dir, int32_t level)
{
    uint8_t *e = PTR(car, 0x6ac);
    int32_t a, b, c, idx;

    if (U8(e, 3) == 0) {
        int32_t hits = S8(e, 1) - (level >> 5);
        if (hits < 0)
            hits = 0;
        sound_play_flagged_at(0x18, 6, 0x600, 0x3fff, car + 0x34, U8(car, 0x6a4));
        U8(e, 1) = (uint8_t)hits;
        U8(e, 3) = 10;
    }
    a = fix_mul8_at((uint32_t)-S16(car, 0xc4), (uint32_t)dir[0], 0);
    a += fix_mul8_at((uint32_t)-S16(car, 0xca), (uint32_t)dir[1], 0);
    a += fix_mul8_at((uint32_t)-S16(car, 0xd0), (uint32_t)dir[2], 0);
    b = fix_mul8_at((uint32_t)-S16(car, 0xc6), (uint32_t)dir[0], 0);
    b += fix_mul8_at((uint32_t)-S16(car, 0xcc), (uint32_t)dir[1], 0);
    b += fix_mul8_at((uint32_t)-S16(car, 0xd2), (uint32_t)dir[2], 0);
    c = fix_mul8_at((uint32_t)-S16(car, 0xc8), (uint32_t)dir[0], 0);
    c += fix_mul8_at((uint32_t)-S16(car, 0xce), (uint32_t)dir[1], 0);
    c += fix_mul8_at((uint32_t)-S16(car, 0xd4), (uint32_t)dir[2], 0);
    a += 0x1000;
    a = a < 0 ? 0 : a > 0x1fff ? 0x1fff : a;
    b += 0x1000;
    b = b < 0 ? 0 : b > 0x1fff ? 0x1fff : b;
    c += 0x1000;
    c = c < 0 ? 0 : c > 0x1fff ? 0x1fff : c;
    idx = ((c >> 7) & 0x30) | (((b >> 11) & 3) << 2) | ((a >> 11) & 3);
    U8(e, 4 + U8(G32(0x4d44a8), idx)) = (uint8_t)level;
    return (uint8_t)level;
}

/* 0x4599c0: a hit on a car (type 3): with its damage record active, a
 * blast mark (0x100, 0x100, 0x100) of 0x180; else a random kick to its
 * angular momentum, an explosion effect at it and its motion stopped. */
void GAMEAPI car_blast_hit(uint8_t *obj, const uint8_t *from)
{
    uint8_t *e = PTR(obj, 0x6ac);
    int32_t at[3], dir[3];
    uint32_t r;

    if ((U32(obj, 0) & 0x1ff) != 3)
        return;
    if (e != NO_PTR && U8(e, 0) != 0) {
        int32_t v[3] = {0x100, 0x100, 0x100};
        blast_mark(obj, v, 0x180);
        return;
    }
    g_seed2 = seed_next(g_seed2);
    r = ((g_seed2 >> 15) * 0x7fff) >> 16;
    I32(obj, 0x80) += (int32_t)(((r & 7) - 4) << 20);
    r = (uint32_t)((int32_t)r >> 3);
    I32(obj, 0x84) += (int32_t)(((r & 7) - 4) << 20);
    r = (uint32_t)((int32_t)r >> 3);
    I32(obj, 0x88) += (int32_t)(((r & 7) - 4) << 20);
    dir[0] = 0;
    dir[1] = (int32_t)0xffc00000;
    dir[2] = 0;
    at[0] = I32(obj, 0x34) + (int32_t)((uint32_t)I32(from, 0x38) << 4);
    at[1] = I32(obj, 0x38) + 0x320000;
    at[2] = I32(obj, 0x3c) + (int32_t)((uint32_t)I32(from, 0x40) << 4);
    explosion_effects_at(at, dir, 2, 0xa21, 0xb);
    I32(obj, 0x68) = 0;
    I32(obj, 0x6c) = 0;
    I32(obj, 0x70) = 0;
    I32(obj, 0x40) = 0;
    I32(obj, 0x44) = 0;
    I32(obj, 0x48) = 0;
}

/* 0x45bd30: n sparks (particle 0x15) below a body, with half its velocity
 * and random spread across. */
void GAMEAPI body_sparks(uint8_t *body, int32_t n)
{
    int32_t pos[3], vel[3];
    int32_t vx = I32(body, 0x68) >> 1, vy = I32(body, 0x6c) >> 1, vz = I32(body, 0x70) >> 1;

    pos[0] = I32(body, 0x34);
    pos[1] = I32(body, 0x38) - 0x1e0000;
    pos[2] = I32(body, 0x3c);
    if (n <= 0)
        return;
    do {
        uint32_t s = seed_next(g_seed);
        vel[1] = vy;
        vel[0] = (int32_t)((((s >> 16) & 0x3f) - 0x20) << 15) + vx;
        s = seed_next(s);
        g_seed = s;
        vel[2] = (int32_t)((((s >> 16) & 0x3f) - 0x20) << 15) + vz;
        particle_spawn_at(pos, vel, 0x15);
    } while (--n != 0);
}

/* 0x45be10: a particle of `kind` at a contact point (+0x20, << 8), thrown
 * back against the body's velocity / 4 and up, with random spread. */
void GAMEAPI contact_particle(uint8_t *body, const uint8_t *contact, uint32_t kind)
{
    int32_t pos[3], vel[3];
    uint32_t s;

    pos[0] = (int32_t)((uint32_t)I32(contact, 0x20) << 8);
    pos[1] = (int32_t)((uint32_t)I32(contact, 0x24) << 8);
    pos[2] = (int32_t)((uint32_t)I32(contact, 0x28) << 8);
    s = seed_next(g_seed);
    vel[0] = (-I32(body, 0x68) >> 2) + (int32_t)((((s >> 16) & 0x3f) - 0x20) << 13);
    s = seed_next(s);
    vel[1] = (int32_t)((uint32_t)-(int32_t)((s >> 16) & 0x3f) << 12);
    s = seed_next(s);
    g_seed = s;
    vel[2] = (-I32(body, 0x70) >> 2) + (int32_t)((((s >> 16) & 0x3f) - 0x20) << 13);
    particle_spawn_at(pos, vel, kind);
}

/* 0x46c1a0: body callback (+0x148): squared distance of the velocities
 * (>> 16) into the word +0x160. Returns 1. */
uint32_t GAMEAPI body_velocity_gap(uint8_t *a, const uint8_t *b)
{
    int32_t dx, dy, dz;

    if (b == 0)
        return 1;
    dx = (I32(a, 0x68) >> 16) - (I32(b, 0x68) >> 16);
    dy = (I32(a, 0x6c) >> 16) - (I32(b, 0x6c) >> 16);
    dz = (I32(a, 0x70) >> 16) - (I32(b, 0x70) >> 16);
    U16(a, 0x160) = (uint16_t)(dz * dz + dy * dy + dx * dx);
    return 1;
}

/* 0x478430: drops the pair with this key from the table 0x5f7220 (count
 * *0x5f72a0) by moving the last over it; without a match the last is
 * copied past the end (sic). Returns the new count. */
uint32_t GAMEAPI pair_list_remove(uint32_t key)
{
    uint32_t n = G32(0x5f72a0), left = n;
    uint32_t *e = (uint32_t *)0x5f7220, *last = (uint32_t *)GMEM(0x5f7218 + n * 8);

    while (left != 0 && e[0] != key) {
        left--;
        e += 2;
    }
    e[0] = last[0];
    e[1] = last[1];
    G32(0x5f72a0) = G32(0x5f72a0) - 1;
    return G32(0x5f72a0);
}

/* ---- wheel dust and skid marks ---- */

/* 0x4219e0: dust behind the driven rear wheels (2, 3) on loose surfaces
 * (kinds 5, 6, 8, 9, 0xe -> particles 0x12, 0x11, 0x13, 0x14, 0x13), more
 * with speed (+0x654) and doubled with the detail setting (float +0x68). */
void GAMEAPI car_wheel_dust(uint8_t *car)
{
    uint32_t seed;

    if (I32(car, 0x654) < 0x20000)
        return;
    body_matrix_to_float(car);
    seed = g_seed;
    for (int32_t w = 2; w < 4; w++) {
        uint8_t *wheel = car + 0x324 + w * 0xc4, *surface;
        int32_t kind, n;

        if (!(U8(wheel, 0) & 1))
            continue;
        surface = PTR(car, 0x198 + w * 0x1c);
        if (surface == 0)
            continue;
        switch ((U32(surface, 0xc) & 0xff) - 5) {
        case 0:
            kind = 0x12;
            break;
        case 1:
            kind = 0x11;
            break;
        case 3:
        case 9:
            kind = 0x13;
            break;
        case 4:
            kind = 0x14;
            break;
        default:
            continue;
        }
        seed = seed_next(seed);
        g_seed = seed;
        n = (int32_t)((seed >> 16) & 3) + (I32(car, 0x654) >> 20);
        if (!(fcomp_status(g_settings + 0x68, (const void *)0x4a3344) & 0x100)) {
            n <<= 1;
            if (kind == 0x13)
                n <<= 1;
        }
        if (n <= 0)
            continue;
        do {
            int32_t pos[3], vel[3];
            uint16_t vx;
            int32_t e, f;

            seed = seed_next(seed);
            g_seed = seed;
            vx = (uint16_t)((w != 2 ? 0x4000 : 0) - 0x2000);
            vx = (uint16_t)(vx + (uint16_t)((int32_t)((seed >> 16) - 0x6000) >> 1));
            seed = seed_next(seed);
            g_seed = seed;
            /* the vector's fourth word is an unset stack word in the
             * original; 0 here */
            g_gte_vector[0] = vx | (uint32_t)(U8(car, 0x63f) ? 0xf600 : 0x0a00) << 16;
            g_gte_vector[1] = (uint16_t)(((int32_t)((seed >> 16) - 0x6000) >> 1) - 0x2000);
            gte_rotate_at();
            pos[0] = (int32_t)((uint32_t)(int32_t)(int16_t)g_gte_result[0] << 8) + I32(car, 0x34);
            pos[1] = (int32_t)((uint32_t)(int32_t)(int16_t)g_gte_result[1] << 8) + I32(car, 0x38);
            pos[2] = (int32_t)((uint32_t)(int32_t)(int16_t)g_gte_result[2] << 8) + I32(car, 0x3c);
            vel[0] = (I32(wheel, 0x60) >> 3) - (I32(car, 0x68) >> 3);
            f = (I32(wheel, 0x64) >> 3) - (I32(car, 0x6c) >> 3);
            vel[2] = (I32(wheel, 0x68) >> 3) - (I32(car, 0x70) >> 3);
            seed = seed_next(seed);
            g_seed = seed;
            e = I32(car, 0x654) >> (kind == 0x12 ? 5 : 6);
            vel[1] = f - (e + (int32_t)(seed >> 16) * 4) - (I32(car, 0x654) >> 5);
            particle_spawn_at(pos, vel, (uint32_t)kind);
            seed = g_seed;
        } while (--n != 0);
    }
}

/* 0x46ee30: a skid mark point: the wheel's contact (car position + wheel
 * offset +0x1c + car velocity + wheel velocity +0x40 * 4) less its side
 * direction (normal +0x10 x velocity, << 3 >> 16), into +0xa0..+0xa4; the
 * direction's low 5 bits each packed into +0xa8 (with 0x8000 set) and the
 * wheel velocity kept at +0xac. */
uint32_t GAMEAPI skid_mark_point(uint8_t *w)
{
    uint8_t *car = PTR(g_car_slots + U8(w, 0x9c) * CAR_SLOT_SIZE, 0);
    int32_t x, y, z, len;
    union {
        int32_t d[3];
        int16_t h[6];
    } v;
    int32_t out[3];
    uint32_t ex, ey, ez, packed;

    x = I32(car, 0x34) + I32(w, 0x1c) + I32(car, 0x68) + (int32_t)((uint32_t)I32(w, 0x40) << 2);
    y = I32(car, 0x38) + I32(w, 0x20) + I32(car, 0x6c) + (int32_t)((uint32_t)I32(w, 0x44) << 2);
    z = I32(car, 0x3c) + I32(w, 0x24) + I32(car, 0x70) + (int32_t)((uint32_t)I32(w, 0x48) << 2);
    v.d[0] = I32(w, 0x10);
    v.d[1] = I32(w, 0x14);
    v.d[2] = I32(w, 0x18);
    len = (int32_t)vec_length_at(v.d);
    if (len < 0x400) {
        v.h[2] = 0;
        v.h[1] = 0;
        v.h[0] = 0;
    } else {
        vec_div_at(v.d, len);
        vec_cross_at(v.d, w + 0x40, out);
        v.h[0] = (int16_t)(out[0] >> 13);
        v.h[1] = (int16_t)(out[1] >> 13);
        v.h[2] = (int16_t)(out[2] >> 13);
    }
    ex = (uint16_t)v.h[0];
    ey = (uint16_t)v.h[1];
    ez = (uint16_t)v.h[2];
    packed = ((((ez & 0x1f) | 0x20) << 5 | (ey & 0x1f)) << 5) | (ex & 0x1f);
    U16(w, 0xa0) = (uint16_t)((x >> 16) - (int32_t)ex);
    I32(w, 0xac) = I32(w, 0x40);
    I32(w, 0xb0) = I32(w, 0x44);
    I32(w, 0xb4) = I32(w, 0x48);
    packed |= U32(w, 0xa8) & 0xffff8000u;
    U16(w, 0xa2) = (uint16_t)((y >> 16) - (int32_t)ey);
    U16(w, 0xa4) = (uint16_t)((z >> 16) - (int32_t)ez);
    U32(w, 0xa8) = packed;
    return packed;
}

/* 0x46efa0: 1 when the wheel's contact has moved more than 0xa0 on an axis
 * from its last mark point, or its velocity turned (dot < 60000). */
uint32_t GAMEAPI skid_mark_moved(uint8_t *w)
{
    uint8_t *car = PTR(g_car_slots + U8(w, 0x9c) * CAR_SLOT_SIZE, 0);
    int32_t x = I32(car, 0x34) + I32(w, 0x1c) + I32(car, 0x68);
    int32_t y = I32(car, 0x38) + I32(w, 0x20) + I32(car, 0x6c);
    int32_t z = I32(car, 0x3c) + I32(w, 0x24) + I32(car, 0x70);
    int32_t d;

    d = (x >> 16) - S16(w, 0xa0);
    if ((int32_t)(((uint32_t)d ^ (uint32_t)(d >> 31)) - (uint32_t)(d >> 31)) > 0xa0)
        return 1;
    d = (y >> 16) - S16(w, 0xa2);
    if ((int32_t)(((uint32_t)d ^ (uint32_t)(d >> 31)) - (uint32_t)(d >> 31)) > 0xa0)
        return 1;
    d = (z >> 16) - S16(w, 0xa4);
    if ((int32_t)(((uint32_t)d ^ (uint32_t)(d >> 31)) - (uint32_t)(d >> 31)) > 0xa0)
        return 1;
    return (int32_t)vec_dot_at(w + 0x40, w + 0xac) < 60000;
}

/* 0x46ed80: a new skid mark segment record (*0x6002d8, 0x124 bytes): the
 * old point, a new point, linked to the wheel (+0xb8); merged with the
 * previous segment's box (0x46f080) or else the next record taken
 * (wrapping). */
uint32_t GAMEAPI skid_mark_start(uint8_t *w, uint8_t *prev)
{
    uint8_t *rec = g_skid_next, *p;
    uint32_t r;

    U16(rec, 0x18) = U8(w, 0x9e) != 0 ? 0 : 1;
    U16(rec, 0x1a) = 1;
    U32(rec, 0) = U32(w, 0xa0);
    U32(rec, 4) = U32(w, 0xa4);
    U32(rec, 8) = U32(w, 0xa8);
    skid_mark_point(w);
    U32(rec, 0xc) = U32(w, 0xa0);
    U32(rec, 0x10) = U32(w, 0xa4);
    U32(rec, 0x14) = U32(w, 0xa8);
    PTR_SET(w, 0xb8, rec);
    r = snd_box_merge_at(w, prev);
    if (r != 0)
        return r;
    p = g_skid_next + 0x124;
    g_skid_next_set(p);
    if (!(p < g_skid_end))
        g_skid_next_set(g_skid_first);
    return A(p);
}

/* 0x46ecb0: a wheel's skid marks (setting +0x2c): marking while the slip
 * is above 0x1b0000 (0xe0000 once marking), on surface kinds 0..2 and the
 * wheel's +0x58 below 0x160000; segments start when the point has moved. */
void GAMEAPI wheel_skid_marks(uint8_t *w, int32_t slip, int32_t surface)
{
    uint8_t *prev = PTR(w, 0xb8);
    int32_t limit = 0x1b0000, mark = 0;
    uint32_t flags, marking;

    if (U8(g_settings, 0x2c) == 0)
        return;
    flags = U32(w, 0xa8);
    marking = flags & 0x8000;
    if (marking)
        limit = 0xe0000;
    if (slip > limit && surface < 3 && surface >= 0) {
        int32_t s = I32(w, 0x58);
        if ((int32_t)(((uint32_t)s ^ (uint32_t)(s >> 31)) - (uint32_t)(s >> 31)) < 0x160000)
            mark = 1;
    }
    I32(w, 0xb8) = -1;
    if (!mark) {
        U32(w, 0xa8) = flags & ~0x8000u;
        U8(w, 0x9e) = 0;
        return;
    }
    if (marking && skid_mark_moved(w) == 0) {
        if (prev != NO_PTR)
            U16(prev, 0x1a) = 0;
        skid_mark_start(w, prev);
        U8(w, 0x9e) = 1;
        return;
    }
    skid_mark_point(w);
    U8(w, 0x9e) = 0;
}

/* ---- object light point, snatch mesh ---- */

/* 0x442c90: an int16 point in a body's frame to the world (float
 * transform), int16 out. Returns out. */
int16_t *GAMEAPI body_point_world(uint8_t *body, const uint32_t *in, int16_t *out)
{
    body_matrix_to_float(body);
    body_offset_to_float(I32(body, 0x34) >> 16, I32(body, 0x38) >> 16, I32(body, 0x3c) >> 16);
    g_gte_vector[0] = in[0];
    g_gte_vector[1] = in[1];
    gte_transform_float();
    out[0] = (int16_t)g_gte_result[0];
    out[1] = (int16_t)g_gte_result[1];
    out[2] = (int16_t)g_gte_result[2];
    return out;
}

/* 0x48f930: the snatch's mesh: 6 rings of 12 points (radius from +0x33c
 * growing by 1/16, sunk by i^2 * r / 5) in world space at +0x344, the
 * centre at +0x6a4, ring colours at +0x2a1c (fading out; by the grab level
 * +0x2a34), its spin (+0x340, +0x341 by 8, +0x342 wave); the grab level
 * grows by 0x80 to 0x1000 while +0x188 is 0x40 or 0x80. */
void GAMEAPI snatch_mesh_update(uint8_t *obj)
{
    int32_t r = I32(obj, 0x33c) >> 1, q, outer, inner, grab, rest, c1, c2;
    uint8_t *v = obj + 0x344, *col;
    uint32_t colour, top;
    uint8_t spin;
    int8_t turn;

    body_matrix_to_float(obj);
    body_offset_to_float(I32(obj, 0x34) >> 16, I32(obj, 0x38) >> 16, I32(obj, 0x3c) >> 16);
    q = (r + (int32_t)((uint32_t)(r >> 31) & 0xf)) >> 4;
    inner = r;
    outer = (int32_t)((uint32_t)r << 2);
    for (int32_t i = 0; i < 6; i++) {
        int32_t sink = imul_hi(i * i * r, (int32_t)0x99999999) >> 1;
        int32_t across = imul_hi(0x66666667, outer) >> 1;
        sink += (int32_t)((uint32_t)sink >> 31);
        across += (int32_t)((uint32_t)across >> 31);
        for (int32_t a = 0; a < 0xc00; a += 0x100) {
            int32_t k = imul_hi(a, 0x2aaaaaab) >> 1;
            k += (int32_t)((uint32_t)k >> 31);
            S16(v, 0) = (int16_t)((int32_t)((uint32_t)(g_wave[k] * inner) << 2) >> 16);
            S16(v, 2) = (int16_t)((int32_t)((uint32_t)(g_wave2[k] * across) << 2) >> 16);
            U16(v, 4) = (uint16_t)sink;
            g_gte_vector[0] = U32(v, 0);
            g_gte_vector[1] = U32(v, 4);
            gte_transform_float();
            U16(v, 0) = (uint16_t)g_gte_result[0];
            U16(v, 2) = (uint16_t)g_gte_result[1];
            U16(v, 4) = (uint16_t)g_gte_result[2];
            v += 8;
        }
        outer += q * 4;
        inner += q;
    }
    U16(obj, 0x6a4) = 0;
    U16(obj, 0x6a6) = 0;
    U16(obj, 0x6a8) = 0;
    g_gte_vector[0] = U32(obj, 0x6a4);
    g_gte_vector[1] = U32(obj, 0x6a8);
    gte_transform_float();
    grab = S16(obj, 0x2a34);
    U16(obj, 0x6a4) = (uint16_t)g_gte_result[0];
    U16(obj, 0x6a6) = (uint16_t)g_gte_result[1];
    U16(obj, 0x6a8) = (uint16_t)g_gte_result[2];
    c1 = (int32_t)((uint32_t)(grab * 5) << 9) >> 16;
    rest = 0x1000 - grab;
    top = (uint32_t)(((int32_t)((uint32_t)((rest + grab) * 255) << 4)) >> 16);
    c2 = (int32_t)((uint32_t)(rest * 5) << 9) >> 16;
    colour = ((top << 8 | (uint32_t)c1) << 8) | (uint32_t)c2;
    col = obj + 0x2a1c;
    for (int32_t j = 0; j < 0x5fa; j += 0xff, col += 4) {
        int32_t fade = imul_hi(0x66666667, j) >> 1;
        uint32_t level;
        fade += (int32_t)((uint32_t)fade >> 31);
        level = (uint32_t)(0xff - fade);
        if (g_detail_flags & 1) {
            U32(col, 0) = colour;
            U8(col, 3) = (uint8_t)level;
        } else {
            /* the original's inline asm: three byte * level >> 8 with
             * mul's upper bits carried in eax (always 0 here: level <= 0xff) */
            uint32_t p0, p1, p2;
            p0 = (((colour >> 16) & 0xff) * level) >> 8;
            p1 = (((p0 & ~0xffu) | ((colour >> 8) & 0xff)) * level) >> 8;
            p2 = (((p1 & ~0xffu) | (colour & 0xff)) * level) >> 8;
            U32(col, 0) = (colour & 0xff000000u) | (p0 & 0xff) << 16 | (p1 & 0xff) << 8 | (p2 & 0xff);
            U8(col, 3) = (uint8_t)level;
        }
    }
    {
        int32_t e = (int32_t)((uint32_t)(rest * 7) << 5) >> 12;
        uint32_t c = (uint32_t)((int32_t)((uint32_t)(grab * 7) << 5) >> 4);
        c = (c & 0xffffff00u) | ((uint32_t)e | 0xffffff00u) << 16 | (uint32_t)e;
        spin = (uint8_t)(U8(obj, 0x340) + 1);
        turn = (int8_t)(U8(obj, 0x341) + 8);
        U32(obj, 0x2a1c) = c;
        U8(obj, 0x340) = spin;
        U8(obj, 0x341) = (uint8_t)turn;
        U16(obj, 0x342) = (uint16_t)g_wave[turn];
    }
    if (I32(obj, 0x188) == 0x80 || I32(obj, 0x188) == 0x40) {
        if (S16(obj, 0x2a34) < 0x1000)
            U16(obj, 0x2a34) = (uint16_t)(U16(obj, 0x2a34) + 0x80);
        else
            U16(obj, 0x2a34) = 0x1000;
    }
}

/* ---- the pause screen ---- */

/* 0x463970: 1 when the paused car is the local player's. */
uint32_t GAMEAPI pause_is_local(void)
{
    uint32_t local = local_car_index_at();
    return U16(GP(g_pause_car), 0x64e) == local;
}

/* 0x469c50: copies a camera setting into a car. */
void GAMEAPI camera_setting_copy(uint8_t *dst, const uint8_t *src)
{
    for (int i = 0x10; i <= 0x30; i += 4)
        U32(dst, i) = U32(src, i);
    U8(dst, 0x10d) = U8(src, 0x10d);
    U32(dst, 0x14c) = U32(src, 0x14c);
    U32(dst, 0x150) = U32(src, 0x150);
    U8(dst, 0x162) = U8(src, 0x162);
    U8(dst, 0x163) = U8(src, 0x163);
}

/* 0x469cd0: copies a camera view record (+4, +0x2ec, +1, +0x2f6..+0x2fd,
 * +0x2e0..+0x2e7, +8, +0xa). */
void GAMEAPI camera_view_copy(uint8_t *dst, const uint8_t *src)
{
    U32(dst, 4) = U32(src, 4);
    U16(dst, 0x2ec) = U16(src, 0x2ec);
    U8(dst, 1) = U8(src, 1);
    for (int i = 0; i < 8; i++)
        U8(dst, 0x2f6 + i) = U8(src, 0x2f6 + i);
    for (int i = 0; i < 4; i++) {
        U8(dst, 0x2e0 + i) = U8(src, 0x2e0 + i);
        U8(dst, 0x2e4 + i) = U8(src, 0x2e4 + i);
    }
    U16(dst, 8) = U16(src, 8);
    U16(dst, 0xa) = U16(src, 0xa);
}

/* 0x469bb0: the car's camera choice (+0x2470 -> +0x1218, 0..5, wrapping)
 * moved by `delta`: its settings, view, the car's handling and race +0x1c. */
void GAMEAPI camera_cycle(uint8_t *car, uint32_t delta)
{
    uint8_t *cams = PTR(car, 0x2470);
    int8_t c = (int8_t)(U8(cams, 0x1218) + (uint8_t)delta);
    int32_t k;

    U8(cams, 0x1218) = (uint8_t)c;
    if (c < 0)
        U8(cams, 0x1218) = 5;
    else if (c >= 6)
        U8(cams, 0x1218) = 0;
    k = S8(cams, 0x1218);
    camera_setting_copy(car, PTR(cams, k * 4));
    camera_view_copy(car + 0x1108, cams + S8(cams, 0x1218) * 0x300 + 0x18);
    car_stats_set_at(car, U16(car, 0x64e), (uint32_t)(int32_t)S8(cams, 0x1218), 0);
    U8(g_race, 0x1c) = U8(cams, 0x1218);
}

/* 0x464030: the pause menu's input: volume sliders drawn (0x464c10) on
 * pages 0-2, the local pad's new presses (0x6b8d74/78/7c), up / down over
 * the enabled items, then by page << 4 | item: CD / master volume, the
 * sub-pages, quit / restart, the camera choice; a click sound. */
void GAMEAPI pause_menu_input(void)
{
    uint32_t sound = 0, pitch = 0, volume = (uint32_t)-1, pad, pressed;
    uint8_t raw[8] = {0};
    int32_t item, sel, step, v = 0;
    uint8_t *car;

    net_local_slot_at();
    if (g_pause_page >= 0 && g_pause_page <= 2) {
        pause_hud_slider((uint32_t)(int32_t)S8(0x6b983a, 0), 2,
                         (uint32_t)((int32_t)((uint32_t)g_pause_cd_volume * 192) >> 14));
        pause_hud_slider((uint32_t)(int32_t)S8(0x6b983b, 0), 3,
                         (uint32_t)((int32_t)((uint32_t)g_pause_volume * 192) >> 14));
    }
    car = GP(g_pause_car);
    controls_fetch_at(U16(car, 0x64e), raw, car + 0x71c);
    pad = pause_pad_bits(raw[4]); /* the packet's byte 4 */
    pressed = ~(~pad | g_pause_pad);
    g_pause_pad_old = g_pause_pad;
    g_pause_pad = pad;
    g_pause_pressed = pressed;
    if (net_state_game_at() != 0 && net_state_host_at() != 0) {
        if (pause_is_local() == 0) {
            pad = 0;
            pressed = 0;
            goto dispatch;
        }
        if (pressed & 0x800) {
            net_pause_set(0);
            return;
        }
    } else if (pressed & 0x800) {
        g_paused = 0;
        return;
    }
    if (pressed == 0x1000) {
        if ((pad & 0xf000) != pressed)
            goto dispatch;
        step = -1;
    } else if (pressed == 0x4000) {
        if ((pad & 0xf000) != pressed)
            goto dispatch;
        step = 1;
    } else {
        goto dispatch;
    }
    {
        int32_t i = g_pause_item, count = g_pause_items;
        do {
            i += step;
            if (i < 0)
                i = count - 1;
            else if (i >= count)
                i = 0;
        } while (g_pause_item_on[i] == 0);
        g_pause_item = (int16_t)i;
        sound = 0x19;
        pitch = 10000;
    }
dispatch:
    item = (int32_t)(((uint32_t)(int32_t)g_pause_page << 4) | (uint32_t)(int32_t)g_pause_item);
    sel = g_pause_item;
    if (item > 0x10) {
        if (item > 0x23)
            goto high;
        if (item == 0x23)
            goto page_20;
        switch (item) {
        case 0x11:
        case 0x21:
            goto master_volume;
        case 0x12:
        case 0x22:
            goto page_10;
        case 0x14:
            goto leave;
        case 0x20:
            goto cd_volume;
        }
        goto end;
    }
    switch (item) {
    case 0:
    case 0x10:
        goto cd_volume;
    case 1:
        goto master_volume;
    case 2:
        goto page_10;
    case 3:
        if (!(pressed & 0x40))
            goto end;
        pause_menu_page(0xb, 0);
        goto page_sound;
    case 4:
        goto leave;
    }
    goto end;

cd_volume:
    if (pause_is_local() == 0 || cd_present() == 0)
        goto end;
    if (pad & 0x8000) {
        v = g_pause_cd_volume - 0x100;
        g_pause_cd_volume = v;
        if (v < 0)
            g_pause_cd_volume = 0;
    } else if (pad & 0x2000) {
        v = g_pause_cd_volume + 0x100;
        g_pause_cd_volume = v;
        if (v > 0x3fff)
            g_pause_cd_volume = 0x3fff;
    }
    if (!(pad & 0xa000))
        goto end;
    /* ax and dx: the upper halves are leftovers (the callee takes words) */
    sound_cd_volume_keep_at(((uint32_t)v & 0xffff0000u) | (uint16_t)g_pause_cd_volume, (uint16_t)g_pause_cd_volume);
    if (g_pause_blink % 3 != 0)
        goto end;
    pitch = (uint32_t)g_pause_cd_volume;
    sound = 0x19;
    volume = pitch;
    goto play;

master_volume:
    if (pause_is_local() == 0)
        goto end;
    if (pad & 0x8000) {
        v = g_pause_volume - 0x100;
        g_pause_volume = v;
        if (v < 0)
            g_pause_volume = 0;
    } else if (pad & 0x2000) {
        v = g_pause_volume + 0x100;
        g_pause_volume = v;
        if (v > 0x3fff)
            g_pause_volume = 0x3fff;
    }
    if (!(pad & 0xa000))
        goto end;
    sound_volume_set_at((uint16_t)g_pause_volume);
    if (!(G8(0x6b8d68) & 8))
        goto end;
    pitch = (uint32_t)g_pause_volume;
    sound = 0x19;
    volume = pitch;
    goto play;

page_10:
    if (!(pressed & 0x40))
        goto end;
    pause_menu_page(0xa, 0);
    goto page_sound;

page_20:
    if (!(pressed & 0x40))
        goto end;
    pause_menu_page(0x14, 0);
page_sound:
    sound = 0x23;
    pitch = 10000;
    goto play;

leave:
    if (!(pressed & 0x40))
        goto end;
    if (net_state_game_at() == 0 || net_state_host_at() == 0) {
        g_paused = 0;
        return;
    }
    net_pause_set(0);
    return;

high:
    if (item > 0xb0) {
        if (item == 0xb1) {
            if (pressed & 0x10)
                goto page_0_3;
            if (!(pressed & 0x40))
                goto end;
            g_paused = 0;
            sound = 0x23;
            I32(g_race, 0x57c) = 8;
            I32(g_race, 0x580) = 0;
            pitch = 10000;
            goto play;
        }
        if (item <= 0x13f || item > 0x145)
            goto end;
        if (pressed & 0x10) {
            pause_menu_page(2, 4);
            goto end;
        }
        if (!(pressed & 0x40))
            goto end;
        {
            uint8_t *car0 = PTR(g_car_slots, 0);
            camera_cycle(car0, (uint32_t)(sel - S8(PTR(car0, 0x2470), 0x1218)));
        }
        sound = 0x23;
        pitch = 10000;
        g_paused = 0;
        goto play;
    }
    if (item == 0xb0) {
        if (pressed & 0x50)
            goto page_0_3;
        goto end;
    }
    if (item == 0x24)
        goto leave;
    if (item == 0xa0) {
        if (!(pressed & 0x50))
            goto end;
        sound = 0x23;
        pitch = 10000;
        pause_menu_page((uint32_t)(int32_t)S16G(0x6b95a2), 2);
        goto play;
    }
    if (item != 0xa1)
        goto end;
    if (pressed & 0x10) {
        sound = 0x23;
        pitch = 10000;
        pause_menu_page((uint32_t)(int32_t)S16G(0x6b95a2), 2);
    }
    if (!(pressed & 0x40))
        goto end;
    sound = 0x23;
    pitch = 10000;
    if (net_state_game_at() != 0 && net_state_host_at() != 0) {
        net_message(U16(GP(g_pause_car), 0x64e), 0, 0x8f);
        net_pause_set(1);
        goto play;
    }
    {
        uint32_t local = pause_is_local();
        net_message(U16(GP(g_pause_car), 0x64e), 0, 0xf);
        g_paused = 0;
        if (net_state_game_at() == 1)
            net_drop_player(U16(GP(g_pause_car), 0x64e));
        if (!(net_state_game_at() == 1 && local == 1) && net_state_game_at() != 0)
            goto play;
        if (U8(g_race, 0x551) == 1)
            I32(g_race, 0x57c) = 7;
        else if (U8(g_race, 0x551) == 0)
            I32(g_race, 0x57c) = 9;
        else
            I32(g_race, 0x57c) = 8;
        I32(g_race, 0x580) = 0;
        goto play;
    }

page_0_3:
    sound = 0x23;
    pitch = 10000;
    pause_menu_page(0, 3);
    goto play;

end:
    if (sound == 0)
        return;
play:
    sound_play_volume_at(sound, 1, 0x800, pitch, 0, volume);
}

/* 0x463b70: the race frame's pause check: in a network game the shared
 * pause (0x45fde0) is followed (the host or the local player can end it);
 * while paused the menu runs and draws; when it ends the CD and sounds
 * resume. Returns *0x4d1198 while paused, else 0 (and clears it). */
uint32_t GAMEAPI pause_frame(void)
{
    if (net_state_game_at() != 0) {
        uint32_t state;
        net_frame();
        if (net_player_active_at(U16(GP(g_pause_car), 0x64e)) == 0)
            net_pause_set(1);
        state = net_pause_state();
        if (state != 0) {
            if (net_state_host_at() != 0) {
                if (!(state & 2))
                    goto unpaused;
                if (U16(GP(g_pause_car), 0x64e) == 0 || pause_is_local() == 1)
                    net_race_abort();
                if (pause_is_local() != 0)
                    goto unpaused;
            }
            net_unpause(U16(GP(g_pause_car), 0x64e));
        unpaused:
            g_paused = 0;
            goto resume;
        }
    }
    if (g_paused != 0) {
        mixer_thunk();
        pause_menu_input();
        race_draw_at();
        screen_present();
        pause_draw();
        if (g_paused != 0)
            return g_pause_return;
    }
resume:
    if (I32(g_race, 0x57c) < 7)
        sound_cd_pause_at(0);
    snd_resume_all_at();
    g_pause_return = 0;
    return 0;
}

/* The pause panel's 9 sprites (0x6b8d8c, 0x78 bytes; table 0x4b15f0: s16 x,
 * y, u8 w, h, -, texture): drawing, kept bit-exact for now (x87). */
static void pause_panel_sprites(void)
{
    uint8_t *spr = (uint8_t *)0x6b8d90;

    for (const uint8_t *e = (const uint8_t *)0x4b15f4; e < (const uint8_t *)0x4b163c; e += 8, spr += 0x78) {
        const uint8_t *t;
        uint32_t w, h, x, y, fx, fy, fw, fh;

        U32(spr, -4) = (U32(spr, -4) & 0xffffff) | 0xc000000;
        U8(spr, 0) = 0x80;
        U8(spr, 1) = 0x80;
        U8(spr, 2) = 0x80;
        U8(spr, 3) = 0x98;
        t = g_textures + ((uint32_t)U8(e, 3) + U16(g_track_info, 0x2e)) * 16;
        U32(spr, 0x1c) = U32(t, 0);
        U32(spr, 0x38) = U32(t, 4);
        U16(spr, 0x54) = U16(t, 8);
        U16(spr, 0x3a) |= 0x40;
        U16(spr, 0x70) = U16(t, 0xa);
        U8(spr, 0x1c)++;
        U8(spr, 0x1d)++;
        U8(spr, 0x38)--;
        U8(spr, 0x39)++;
        U8(spr, 0x54)++;
        U8(spr, 0x55)--;
        U8(spr, 0x70)--;
        U8(spr, 0x71)--;
        w = U8(e, 0) + 1u;
        h = U8(e, 1) + 1u;
        y = (uint32_t)((int32_t)((uint32_t)(int32_t)S16(e, -2) * (uint32_t)g_screen_scale) >> 8) + (g_pause_y >> 1);
        x = (uint32_t)((int32_t)((uint32_t)(int32_t)S16(e, -4) * (uint32_t)g_screen_scale) >> 8) + (g_pause_x >> 1);
        fx = fild_q(x);
        U32(spr, 0x44) = fx;
        U32(spr, 0xc) = fx;
        fy = fild_q(y);
        U32(spr, 0x2c) = fy;
        U32(spr, 0x10) = fy;
        fw = fild_d((int32_t)((uint32_t)g_screen_scale * w) >> 8);
        fadd_store(fw, spr + 0xc, spr + 0x28);
        U32(spr, 4) = 0x40;
        fadd_store(fw, spr + 0x44, spr + 0x60);
        fh = fild_d((int32_t)((uint32_t)g_screen_scale * h) >> 8);
        fadd_store(fh, &fy, &fh);
        U32(spr, 0x48) = fh;
        U32(spr, 0x64) = fh;
        U32(spr, 0x5c) = 0x40400000;
        U32(spr, 0x40) = 0x40400000;
        U32(spr, 0x24) = 0x40400000;
        U32(spr, 8) = 0x40400000;
    }
}

/* 0x463ca0: opens the pause screen for a car (pointer to its pointer):
 * paused, the volumes from the race record, a sound, the panel's sprites
 * (colours by detail flag 2) and the first page (0x464650: (2, 3) in a
 * race kind 4, (1, 4) in kinds 1-9, else (0, 4)). */
void GAMEAPI pause_open(uint32_t *car_ref)
{
    uint32_t keep, k;
    int32_t volume;

    replay_state_clear_at();
    g_paused = 1;
    G32(0x6b8d80) = A(car_ref);
    keep = U32(GP(car_ref), 0x700);
    g_pause_pad_old = keep;
    g_pause_cd_volume = S16(g_race, 0xa7e);
    volume = S16(g_race, 0xa7c);
    g_pause_volume = volume;
    sound_play_at(0x23, 1, 0x800, (keep & 0xffff0000u) | (uint16_t)volume, 0);
    pause_panel_sprites();
    if (g_detail_flags & 2) {
        G8(0x6b8d93) |= 2;
        G16(0x6b8dca) = 0xbf;
        G8(0x6b8d91) = 0xaf;
        G8(0x6b8d90) = 0xaf;
        G8(0x6b8d92) = 5;
        G16(0x6b8dae) = 0xaf;
    } else {
        G8(0x6b8d93) &= (uint8_t)~2;
        G16(0x6b8dca) = 0x3f;
        G8(0x6b8d91) = 0;
        G8(0x6b8d90) = 0;
        G8(0x6b8d92) = 0x32;
        G16(0x6b8dae) = 0xff;
    }
    k = U8(g_race, 0x551) - 1u;
    if (k > 8)
        pause_menu_page(0, 4);
    else if (k == 3)
        pause_menu_page(2, 3);
    else
        pause_menu_page(1, 4);
}
