/* Race setup (0x46a060), group V: per-race initialisation of the weather,
 * the falling rock, the special track objects (screens and scripted
 * bodies), the body/collision pools and event queue, the car texture
 * animation table, the freeze colour map, the pads, the race clock and the
 * time warp, plus a few small helpers reached from there.
 *
 * Still original (called by address): the allocator 0x4445e0 (and the bump
 * allocator 0x476b80), file I/O, sound, the network checks, the clock read
 * 0x428290 (x87), and everything that builds textures or palettes for
 * DirectDraw: the time-warp texture (0x47d980 and its tree 0x47d5a0,
 * 0x47d6d0, 0x47d7f0, 0x47d8f0), the animated snatch texture (0x486780,
 * 0x4867a0, 0x486450, 0x486540, 0x486600), the pixel format converters
 * 0x41e1f0 / 0x41e440 / 0x41e1d0, the texture slot finder 0x41ec20 and the
 * surface wrappers 0x428c30 / 0x428c60 / 0x428c90. The future renderer
 * replaces those. */
#include "../runtime.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_config (*(uint8_t **)0x4b2198)
#define g_track_info (*(uint8_t **)0x6c1eb0)
#define g_track_lists (*(uint8_t **)0x6c1e14)
#define g_misc_seed (*(uint32_t *)0x4b11dc)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274

/* weather */
#define g_emitters ((uint8_t **)0x5d6360) /* 6 */
#define g_emitter_count (*(uint32_t *)0x5d6248)
#define g_rain (*(uint8_t **)0x5d6340)
#define g_rain_end (*(uint8_t **)0x5d633c)
#define g_snow (*(uint8_t **)0x5d6244)
#define g_snow_end (*(uint8_t **)0x5d6240)
#define g_flakes (*(uint8_t **)0x4d5898)
#define g_flakes_bright (*(uint32_t *)0x4d58f0)
#define g_weather_buffer (*(uint8_t **)0x4d58fc)
#define g_weather_seed ((uint32_t *)0x4b2c50)
#define g_weather_detail (*(uint32_t *)0x5d6338)
#define g_weather_level (*(uint32_t *)0x4b2c5c)
#define g_weather_level_start (*(uint32_t *)0x4b2c58)
#define g_weather_drift (*(uint32_t *)0x4b2c60)
#define g_weather_fade (*(uint32_t *)0x4d58ec)
#define g_weather_fade_time (*(uint32_t *)0x4d58e8)
#define g_gte_shift_in (*(uint32_t *)0x6cccdc)
#define g_gte_shift_out (*(uint32_t *)0x6ccce0)

/* rock */
#define g_rock (*(uint8_t **)0x6b9d50)
#define g_voice_count (*(uint32_t *)0x4b1c8c)

/* special track objects */
#define g_specials_count (*(uint32_t *)0x6ce6fc)
#define g_specials (*(uint32_t ***)0x6ce738)
#define g_screen ((uint8_t *)0x6ce700)

/* body pools */
#define g_events ((uint32_t *)0x6b9860)
#define g_slot_records ((uint8_t *)0x6b9880) /* 64 of 0xc */

/* freeze */
#define g_freeze_map (*(uint32_t **)0x6c3080)

#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define PTR(p, off) (*(uint8_t **)((p) + (off)))

typedef void *(GAMEAPI *alloc_fn)(uint32_t size);
typedef uint32_t(GAMEAPI *sound_fn)(uint32_t sound, uint32_t a, uint32_t b, uint32_t c, const void *pos);
typedef int32_t(GAMEAPI *int_fn)(void);
typedef void(GAMEAPI *void_fn)(void);
typedef void(GAMEAPI *body_fn)(uint8_t *body);
typedef void(GAMEAPI *emitter_fn)(int32_t a, uint8_t *emitter);
typedef void(GAMEAPI *emitter3_fn)(uint8_t *a, uint8_t *b, uint8_t *c);
typedef uint8_t *(GAMEAPI *body_new_fn)(uint32_t a, uint32_t kind, uint32_t size, uint32_t d);
typedef void(GAMEAPI *body_shape_fn)(uint8_t *body, const void *shape);
typedef void(GAMEAPI *spheres_fn)(uint8_t *body, uint32_t count, const void *spheres, uint32_t d);
typedef void(GAMEAPI *script_fn)(uint8_t *body, uint32_t script, int32_t start);
typedef void(GAMEAPI *body_int_fn)(uint8_t *body, uint32_t a);
typedef void(GAMEAPI *index_fn)(uint32_t i);
typedef int32_t(GAMEAPI *char_fn)(int32_t c);

#define mem_alloc ((alloc_fn)0x4445e0)
#define sound_play ((sound_fn)0x471c90)
#define net_is_game ((int_fn)0x45ef80)
#define net_local_car ((int_fn)0x48cf20)
#define game_time ((int_fn)0x43692a)
#define weather_indoors ((emitter_fn)0x4846c0)
#define weather_box ((emitter3_fn)0x4848c0)
#define weather_drops_spawn ((emitter3_fn)0x4846f0)
#define body_new ((body_new_fn)0x460ba0)
#define body_shape_set ((body_shape_fn)0x465640)
#define grid_unlink_body ((body_fn)0x4611d0)
#define shape_list_add ((body_fn)0x45c010)
#define body_spheres_set ((spheres_fn)0x419150)
#define body_script_start ((script_fn)0x462e30)
#define body_flag_set ((body_int_fn)0x478400)
#define car_texture_frames ((index_fn)0x475430)
#define clock_nop ((void_fn)0x4291f0)
#define clock_read ((int_fn)0x428290)
#define char_upper ((char_fn)0x498540)

/* --- weather --- */

/* 0x486160: stamp a seed with the game time. */
void GAMEAPI time_stamp(uint32_t *seed)
{
    *seed = (uint32_t)game_time();
}

/* 0x486170: weather buffers and levels: config +0x26 is the detail setting
 * (0 high .. 2 off; other values keep the last one), race +0x555 a storm
 * start level for kind 1 / 2 (race +0x553). */
void GAMEAPI weather_level_setup(void)
{
    uint32_t detail;

    g_weather_buffer = mem_alloc(0x1000);
    time_stamp(g_weather_seed);
    switch (U8(g_config, 0x26)) {
    case 0:
        detail = g_weather_detail = 2;
        break;
    case 1:
        detail = g_weather_detail = 1;
        break;
    case 2:
        detail = g_weather_detail = 0;
        break;
    default:
        detail = g_weather_detail;
        break;
    }
    if (U8(g_race, 0x555)) {
        if (U8(g_race, 0x553) == 1)
            g_weather_level = 0x80;
        if (U8(g_race, 0x553) == 2)
            g_weather_level = 0xc0;
    }
    if (detail != 0 && detail == 1) {
        g_weather_fade = 0x1e;
        g_weather_drift = g_weather_level_start;
        g_weather_fade_time = 0;
        return;
    }
    g_weather_fade = 0;
    g_weather_drift = 0x100;
    g_weather_fade_time = 0;
}

/* 0x483fa0: kind 1 (rain): 0xa0 drop sprites of 0x3c bytes. */
void GAMEAPI rain_setup(void)
{
    uint8_t *p = mem_alloc(0x2580);
    int32_t i;

    g_rain_end = p + 0x2580;
    g_rain = p;
    for (i = 0; i < 0xa0; i++, p += 0x3c) {
        uint32_t head = U32(p, 0);

        U32(p, 4) = 0x2f0000;
        U32(p, 0x20) = 0x3f3f3f;
        U8(p, 7) = 0xae;
        U32(p, 0) = (head & 0xffffff) | 0xc000000;
    }
}

/* 0x483ff0: kind 2 (snow): 0xaf flake sprites of 0x78 bytes, texture
 * coordinates from track list entry +0x66. */
void GAMEAPI snow_setup(void)
{
    const uint8_t *uv = g_track_lists + (U16(g_track_info, 0x2e) + 0x66) * 0x10;
    uint32_t a = U32(uv, 0), b = U32(uv, 4);
    uint16_t c = U16(uv, 8), d = U16(uv, 0xa);
    uint8_t *p = mem_alloc(0x5208);
    int32_t i;

    g_snow = p;
    g_snow_end = p + 0x5208;
    for (i = 0; i < 0xaf; i++, p += 0x78) {
        uint32_t head = U32(p, 0);

        U32(p, 4) = 0x606060;
        U8(p, 7) = 0x9a;
        U32(p, 0x20) = a;
        U32(p, 0) = (head & 0xffffff) | 0xc000000;
        U32(p, 0x3c) = b;
        U16(p, 0x58) = c;
        U16(p, 0x74) = d;
    }
}

/* 0x483ea0: emitter i follows a car's camera (the extra two follow the
 * local car, as does emitter 0 in a network game). */
void GAMEAPI weather_emitter_setup(int32_t i)
{
    uint8_t *e = g_emitters[i];
    int32_t n;
    uint32_t bit;

    if (i >= (int32_t)g_emitter_count - 2)
        i = 0;
    else if (i != 0)
        goto follow;
    if (net_is_game())
        i = net_local_car();
follow:
    U32(e, 0x10) = **(uint32_t **)(g_car_slots + i * CAR_SLOT_SIZE + 0x28);
    for (n = 0; n < 400; n++)
        U16(e, 0x1a + n * 8) = 0;
    PTR(e, 0xc94) = e + 0x14;
    weather_indoors(0, e);
    weather_box(e, e + 8, e);
    weather_drops_spawn(e, e + 8, e);
    U16(e, 0xc98) = 0;
    U16(e, 0xc9a) = (U8(g_race, 0x553) & 1) ? 0x32 : 8;
    U16(e, 0xc9c) = 0;
    g_gte_shift_in = 8;
    g_gte_shift_out = 0;
    for (n = 0, bit = 0x80000000u; !(bit & 8) && n < 0x20; bit >>= 1)
        n++;
    g_gte_shift_out = (uint32_t)n;
    U16(e, 0xca2) = (uint16_t)(0x1f - n);
}

/* 0x4856a0: the 32 screen-space flake records (0xa0 bytes), texture
 * coordinates from track list entries +0xa1 / +0xa0 alternately (no
 * weather), +0x5a (kind 1) or +0x66 (kind 2). Any other kind keeps the
 * entry pointer it came in with, which the original takes from ecx. */
void __attribute__((fastcall)) weather_flakes_setup(const uint8_t *uv)
{
    int32_t i;

    weather_level_setup();
    g_flakes = mem_alloc(0x1400);
    g_flakes_bright = 0;
    if (U8(g_race, 0x553) == 0) {
        g_misc_seed = g_misc_seed * 16807 & 0x7fffffff;
        if (((g_misc_seed >> 15) * 0x7fff & 0xf0000) < 0x20000)
            g_flakes_bright = 1;
    }
    for (i = 0; i < 0x1400 / 0xa0; i++) {
        uint8_t *r;

        switch (U8(g_race, 0x553)) {
        case 0:
            uv = g_track_lists + ((uint32_t)U16(g_track_info, 0x2e) + ((i & 1) ? 0xa0 : 0xa1)) * 0x10;
            break;
        case 1:
            uv = g_track_lists + ((uint32_t)U16(g_track_info, 0x2e) + 0x5a) * 0x10;
            break;
        case 2:
            uv = g_track_lists + ((uint32_t)U16(g_track_info, 0x2e) + 0x66) * 0x10;
            break;
        }
        U32(g_flakes, i * 0xa0) = 0;
        U32(g_flakes, i * 0xa0 + 0x98) = 0;
        r = g_flakes + i * 0xa0 + 4;
        U32(r, 0x20) = U32(uv, 0);
        U32(r, 0x3c) = U32(uv, 4);
        U16(r, 0x58) = U16(uv, 8);
        U16(r, 0x74) = U16(uv, 0xa);
    }
}

/* 0x483d90: the weather of the race (race +0x553: 0 none, 1 rain, 2 snow):
 * an emitter per player view (+0x552 picks 1..4) plus two, each with its
 * own looping sound when it rains. */
void GAMEAPI weather_setup(void)
{
    uint32_t count = 1, i;

    for (i = 0; i < 6; i++)
        g_emitters[i] = (uint8_t *)0xffffffff;
    if (U8(g_race, 0x553)) {
        switch (U8(g_race, 0x552)) {
        case 0:
            count = 1;
            break;
        case 1:
        case 2:
            count = 2;
            break;
        case 3:
            count = 3;
            break;
        case 4:
            count = 4;
            break;
        }
        count += 2;
        g_emitter_count = count;
        for (i = 0; i < count; i++)
            g_emitters[i] = mem_alloc(0xca4);
        if (U8(g_race, 0x553) == 1)
            rain_setup();
        else if (U8(g_race, 0x553) == 2)
            snow_setup();
        for (i = 0; i < count; i++) {
            if (U8(g_race, 0x553) & 1) {
                uint8_t voice = (uint8_t)sound_play(0x27, 9, 0x400, 0, 0);

                U8(g_emitters[i], 0xca1) = voice;
            } else
                U8(g_emitters[i], 0xca1) = 0x7f;
            weather_emitter_setup((int32_t)i);
        }
    }
    /* With weather the original passes a leftover ecx, read only by kinds
     * above 2, which no track uses. */
    weather_flakes_setup(0);
}

/* --- falling rock --- */

/* 0x457f80: the falling rock body, on tracks that have one (race +0x554). */
void GAMEAPI rock_setup(void)
{
    if (!U8(g_race, 0x554))
        return;
    g_rock = body_new(0, 0x821, 0x180, 0);
    U32(g_rock, 0x148) = 0x458310;
    body_shape_set(g_rock, (const void *)0x4b0a70);
    grid_unlink_body(g_rock);
    U32(g_rock, 0x164) = 0x6b9d50;
    *(uint32_t *)0x6b9d54 = 0;
    *(uint8_t *)0x6b9d58 = (uint8_t)g_voice_count;
    *(uint32_t *)0x6b9d44 = 0;
    *(uint32_t *)0x6b9d48 = 0;
}

/* --- special track objects --- */

/* 0x43e620: a spray (kind 0x20) starts idle. */
void GAMEAPI spray_reset(uint8_t *body)
{
    U32(body, 0x180) = 0;
    U16(body, 0x184) = 0;
    U16(body, 0x186) = 0;
}

/* 0x41ad20: start the body's script (+0x138, start +0x13a) if it has one. */
void GAMEAPI special_script_start(uint8_t *body)
{
    uint32_t script = U16(body, 0x138);

    if (script) {
        shape_list_add(body);
        body_script_start(body, script, S16(body, 0x13a));
    }
}

/* 0x4013f0: a scripted collidable object (kinds 0x14 screen, 0x19, 0x1f,
 * 0x23): its collision spheres and script. */
void GAMEAPI special_body_setup(uint8_t *body)
{
    uint32_t kind = U32(body, 0) & 0x1ff;

    U32(body, 0x184) = 1;
    if (kind == 0x14) {
        shape_list_add(body);
    } else if (kind == 0x23) {
        body_spheres_set(body, 1, (const void *)0x4a7134, 0);
    } else {
        body_spheres_set(body, 1, (const void *)0x4a7120, 0);
        body_flag_set(body, 0);
    }
    body_script_start(body, U16(body, 0x138), S16(body, 0x13a));
}

/* 0x45bfd0: kind 0x1d: four collision spheres and its script. */
void GAMEAPI special_spheres4_setup(uint8_t *body)
{
    body_spheres_set(body, 4, (const void *)0x4b0d50, 0);
    body_script_start(body, U16(body, 0x138), S16(body, 0x13a));
    body_flag_set(body, 0);
}

/* 0x41aef0: the track's screen (kind 0x14): three rows of picture texture
 * coordinates (8 bytes each at 0x6ce714) from the model's coordinates at
 * +0x122c.., stepped by each row's height less 5; colour 0x808080. */
void GAMEAPI screen_uv_setup(uint8_t *body)
{
    const uint8_t *model = PTR(body, 0x14);
    int32_t height[3], sum = 0, i;
    uint8_t top = model[0x122c], v = top;

    U32(g_screen, 4) = 1;
    U32(g_screen, 0) = 0;
    PTR(g_screen, 8) = body;
    U16(g_screen, 0xc) = 3;
    g_screen[0x10] = 0x23;
    g_screen[0x11] = 0x24;
    g_screen[0x12] = 0x25;
    g_screen[0x13] = 0xff;
    height[0] = (int32_t)model[0x126c] - (int32_t)top;
    height[1] = (int32_t)model[0x12f0] - (int32_t)model[0x12b0];
    height[2] = (int32_t)model[0x1374] - (int32_t)model[0x1334];
    for (i = 0; i < S16(g_screen, 0xc); i++) {
        uint8_t *row = g_screen + 0x14 + i * 8;
        uint8_t next;

        row[0] = v;
        sum += height[i] - 5;
        next = (uint8_t)((uint8_t)sum + top);
        row[4] = next;
        row[2] = row[0];
        row[6] = next;
        row[1] = model[0x122d];
        row[3] = model[0x124d];
        row[5] = model[0x126d];
        row[7] = model[0x128d];
        v = next;
    }
    g_screen[0x34] = 0x80;
    g_screen[0x35] = 0x80;
    g_screen[0x36] = 0x80;
}

/* 0x41ab80: set up every special track object by kind (the list at
 * *0x6ce738, *0x6ce6fc of them). */
void GAMEAPI track_specials_setup(void)
{
    uint32_t **list = g_specials, n;

    for (n = g_specials_count; n; n--, list++) {
        uint8_t *body = (uint8_t *)*list;

        switch (U32(body, 0) & 0x1ff) {
        case 0x14:
            screen_uv_setup(body);
            special_body_setup(body);
            break;
        case 0x19:
        case 0x1f:
        case 0x23:
            special_body_setup(body);
            break;
        case 0x1d:
            special_spheres4_setup(body);
            break;
        case 0x20:
            spray_reset(body);
            break;
        default:
            special_script_start(body);
            break;
        }
    }
}

/* --- pools --- */

/* 0x434e70: an event queue of `capacity` entries of 0x68 bytes: the
 * entries (+8), an order list (+0xc, one longer) and a free list (+0x10,
 * entry i as i << 20); +0 used, +4 capacity, +0x14 last free index. */
void GAMEAPI event_queue_setup(uint32_t capacity, uint32_t *queue)
{
    uint32_t *free_list, i;

    capacity &= 0xffff;
    queue[2] = (uint32_t)mem_alloc(capacity * 0x68);
    queue[3] = (uint32_t)mem_alloc(capacity * 4 + 4);
    free_list = mem_alloc(capacity * 4);
    queue[4] = (uint32_t)free_list;
    queue[1] = capacity;
    queue[0] = 0;
    for (i = 0; i < capacity; i++)
        free_list[i] = i << 20;
    queue[5] = capacity - 1;
}

/* 0x418fa0: the collision event queue (100), contact buffers, the 256-entry
 * slot map (identity, with two zeroed tables), the body list (0x180 bytes)
 * and 64 sweep slot records of 0x84 bytes. */
void GAMEAPI body_pools_setup(void)
{
    uint8_t *map, *p;
    int32_t i;

    event_queue_setup(100, g_events);
    *(void **)0x6ce740 = mem_alloc(0x4800);
    map = mem_alloc(0x300);
    *(uint8_t **)0x6ce73c = map;
    *(uint8_t **)0x6ce744 = map + 0x100;
    *(uint8_t **)0x6ce750 = map + 0x200;
    *(uint32_t *)0x6ce754 = 0;
    for (i = 0; i < 0x100; i++) {
        map[i] = (uint8_t)i;
        map[0x100 + i] = 0;
        map[0x200 + i] = 0;
    }
    *(uint32_t *)0x6ce748 = 0xffffffff;
    *(uint32_t *)0x6ce758 = 0xff;
    *(void **)0x6ce74c = mem_alloc(0x8000);
    *(void **)0x6b9b80 = mem_alloc(0x180);
    *(uint16_t *)0x6b9b84 = 0;
    *(uint16_t *)0x6b9b86 = 0;
    *(uint32_t *)0x6b9b88 = 0;
    p = mem_alloc(0x2108);
    for (i = 0; i < 64; i++, p += 0x84) {
        uint8_t *r = g_slot_records + i * 0xc;

        PTR(r, 0) = p;
        U16(r, 4) = 0;
        U16(r, 6) = 0;
        U32(r, 8) = 0;
    }
    *(uint16_t *)0x6b9854 = 0;
    *(uint16_t *)0x6b9856 = 0;
    *(uint32_t *)0x6b9858 = 0;
    *(uint32_t *)0x6b9b8c = 0;
    *(uint8_t **)0x6b9850 = p;
}

/* --- car textures --- */

/* 0x4750b0: reset car texture animation i (0x5f7400 + i*0x1088) and
 * rebuild the shared 5 x 8 table at 0x5f72c0 (rows 8 bytes apart, columns
 * 0x28): ring r of radius 16r at angle 45 degrees * c, sin / 14000,
 * cos / 32768, height 0x50 - (6r)^2 / 16. */
void GAMEAPI car_texture_setup(uint32_t i)
{
    uint8_t *anim = (uint8_t *)0x5f7400 + i * 0x1088;
    int32_t r;

    U32(anim, 0x1080) = 0;
    U32(anim, 0x1084) = 0xffffffff;
    car_texture_frames(i);
    for (r = 0; r < 5; r++) {
        int32_t radius = (r * 0x40) >> 2, d = r * 6;
        int16_t height = (int16_t)(0x50 - d * d / 16);
        uint32_t c;

        for (c = 0; c < 8; c++) {
            uint8_t *e = (uint8_t *)0x5f72c0 + r * 8 + c * 0x28;
            int32_t s = *(const int16_t *)(0x4b24f8 + c * 0x40) * radius;
            int32_t k = *(const int16_t *)(0x4b2578 + c * 0x40) * radius;

            S16(e, 0) = (int16_t)(s / 14000);
            S16(e, 2) = (int16_t)(k / 32768);
            S16(e, 4) = height;
        }
    }
}

/* 0x475090: all six car texture animations. */
void GAMEAPI car_textures_setup(void)
{
    uint32_t i;

    for (i = 0; i < 6; i++)
        car_texture_setup(i);
}

/* --- the rest --- */

/* 0x43a8d0: the freezer's colour map (0x400 entries, all unused) and its
 * free colour count. */
void GAMEAPI freeze_setup(void)
{
    uint32_t *map;
    int32_t i;

    *(uint32_t *)0x6c30a0 = 0;
    map = mem_alloc(0x800);
    g_freeze_map = map;
    *(uint8_t *)0x4beb24 = 0x7f;
    for (i = 0; i < 0x200; i++)
        map[i] = 0xffffffff;
    *(uint16_t *)0x6c3e94 = 0x3cf;
}

/* 0x41b440: clear a pad record (car+0x6f8, 0x28 bytes); +0x20 = 0x10. */
void GAMEAPI pad_reset(uint8_t *pad)
{
    int32_t i;

    for (i = 0; i < 10; i++)
        U32(pad, i * 4) = 0;
    pad[0x20] = 0x10;
}

/* 0x41b460: each car's pad and controller kind (race +i: below 8 a player,
 * 10; otherwise that kind, e.g. 8 for the AI). */
int32_t GAMEAPI slots_controls_setup(uint8_t *slot)
{
    uint32_t i;

    if (!U8(g_race, 0x54d))
        return 0;
    for (i = 0; i < U8(g_race, 0x54d); i++, slot += CAR_SLOT_SIZE) {
        uint8_t *pad = PTR(slot, 0) + 0x6f8;
        uint8_t kind;

        pad_reset(pad);
        kind = U8(g_race, i);
        if (kind < 8) {
            pad[0x14] = 10;
        } else {
            pad[0x14] = kind;
            U32(pad, 0x24) = 0;
        }
    }
    return 0;
}

/* 0x429200: set the timer's callback. */
void GAMEAPI timer_callback_set(uint32_t callback)
{
    *(uint32_t *)0x4be114 = callback;
}

/* 0x435110: restart the race clock with its tick callback 0x435140. */
void GAMEAPI clock_start(void)
{
    clock_nop();
    *(uint32_t *)0x4be388 = 0;
    *(uint32_t *)0x4be38c = (uint32_t)clock_read();
    timer_callback_set(0x435140);
    clock_nop();
}

/* 0x4351a0: restart the race clock. */
void GAMEAPI clock_restart(void)
{
    clock_nop();
    *(uint32_t *)0x4be388 = 0;
    *(uint32_t *)0x4be38c = (uint32_t)clock_read();
    clock_nop();
}

/* 0x479720: no time warp. */
void GAMEAPI time_warp_reset(void)
{
    *(uint32_t *)0x5d9b2c = 0x10000;
    *(uint32_t *)0x5d9b20 = 0;
    *(uint32_t *)0x5d9b24 = 0;
    *(uint32_t *)0x5d9b28 = 0;
}

/* 0x41e1a0: the smallest power of two >= n (signed compare), 0 if none. */
int32_t GAMEAPI next_pow2(int32_t n)
{
    int32_t i;

    for (i = 0; i < 32; i++)
        if (n <= (int32_t)(1u << i))
            return (int32_t)(1u << i);
    return 0;
}

/* 0x476f30: copy n bytes (byte, word, then dwords). */
void GAMEAPI mem_copy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    volatile uint8_t *d = dst; /* keep it a plain copy loop */

    if (n & 1)
        *d++ = *src++;
    if (n & 2) {
        *(volatile uint16_t *)d = *(const uint16_t *)src;
        d += 2;
        src += 2;
    }
    for (n >>= 2; n; n--, d += 4, src += 4)
        *(volatile uint32_t *)d = *(const uint32_t *)src;
}

/* 0x476f50: fill n bytes with c; leaves the fill pattern in eax. */
uint32_t GAMEAPI mem_fill(uint8_t *dst, uint8_t c, uint32_t n)
{
    volatile uint8_t *d = dst;
    uint32_t pattern = c * 0x01010101u;

    if (n & 1)
        *d++ = c;
    if (n & 2) {
        *(volatile uint16_t *)d = (uint16_t)pattern;
        d += 2;
    }
    for (n >>= 2; n; n--, d += 4)
        *(volatile uint32_t *)d = pattern;
    return pattern;
}

/* 0x43b5a0: archive names equal ignoring case, with '\' as '/', "_" as '.'
 * and "__" as '/'. */
int32_t GAMEAPI archive_name_equal(const char *a, const char *b)
{
    for (;;) {
        int32_t x = char_upper((uint8_t)*a++), y;

        if (x == 0x5c) {
            x = 0x2f;
        } else if (x == 0x5f) {
            x = 0x2e;
            if (*a == 0x5f) {
                x = 0x2f;
                a++;
            }
        }
        y = char_upper((uint8_t)*b++);
        if (y == 0x5c) {
            y = 0x2f;
        } else if (y == 0x5f) {
            y = 0x2e;
            if (*b == 0x5f) {
                y = 0x2f;
                b++;
            }
        }
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

/* 0x43b730: clear the archive file cache once. */
void GAMEAPI file_cache_init(void)
{
    int32_t n;
    uint32_t *p;

    if (*(uint32_t *)0x4beb28)
        return;
    for (n = *(int32_t *)0x4adaa4, p = (uint32_t *)0x6c25c0; n > 0; n--, p += 2)
        *p = 0;
    *(uint32_t *)0x4beb28 = 1;
}
