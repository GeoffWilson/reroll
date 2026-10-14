/* After the world step (sim_step 0x444120), group G: the "snatch" weapon
 * entity (0x48dc40, one body at *0x5d1fc0 whose +0x188 is its state) and
 * two small checks and a counter.
 *
 * The snatch picks the nearest car up to 0x8000 track units ahead of its
 * owner (+0x194) as target (+0x198, 1 for none), launches (state 1),
 * flies at it (2), grabs and pulls it in (0x40), holds it (0x80) and, when
 * the target crosses the owner's side plane close by, puts the target just
 * behind the owner with the owner's motion and kicks the owner forward
 * (0x48e470). States 4 / 0x10 end it (4 with an explosion). +0x180 counts
 * steps in the state.
 *
 * Still original: sound (0x471d50, 0x471e60, 0x471f40), effects
 * (0x42aa30), body / sprite lists (0x419300, 0x4612a0, 0x460ff0), placing
 * the target (0x4687f0, 0x468f80, 0x468bb0), network messages (0x45ef80,
 * 0x45eed0, 0x48cf20, 0x460a90, 0x4600a0) and the float mesh build 0x48f930.
 * The texture animations gated by 0x46e3c0 / 0x4906c0 (0x46d0c0, 0x486540)
 * lock DirectDraw surfaces and stay original. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "gamefn.h"
#include "physics.h"

#define g_race GP(0x4b1960)
#define g_snatch GP(0x5d1fc0)
#define g_early_ticks (*(int32_t *)0x4d3dac)
#define g_snatches_live (*(uint8_t *)0x6b9d43)
#define g_voice_count (*(int32_t *)0x4b1c8c)
#define g_render_flags (*(uint8_t *)0x6ccd84)
#define g_sprite_seed (*(uint32_t *)0x4b2d1c)
#define g_ring_phase (*(uint8_t *)0x4da204)
#define g_ring2_phase (*(uint8_t *)0x4da205)
#define g_snatch_drop ((int32_t *)0x4da1d8)
#define g_snatch_matrix ((int32_t *)0x4da1b8)
#define g_sin ((const int16_t *)0x4b24f8)
#define g_cos ((const int16_t *)0x4b2578)
#define g_spark_colours ((const uint32_t *)0x4b2d34)
#define g_grudge_levels ((const uint16_t *)0x4a7c68)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define SLOT_CAR(i) GP(g_car_slots + (i) * CAR_SLOT_SIZE)
#define SLOT_DISTANCE(i) (*(int32_t *)(g_car_slots + (i) * CAR_SLOT_SIZE + 0x34))

#define I32(p, off) (*(int32_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define SHL(x, n) ((int32_t)((uint32_t)(int32_t)(x) << (n)))
#define PTR(p, off) GP((p) + (off))
#define PTR_SET(p, off, v) (*(uint32_t *)((p) + (off)) = (uint32_t)(uintptr_t)(v))

#define STATE 0x188
#define STEPS 0x180
#define OWNER 0x194
#define TARGET 0x198
#define VOICE 0x2a36
#define GRAB_TIME 0x2a37

/* Still original. */
#define sprite_unlink GCALL(0x4612a0, object_grid_unlink2)
#define sprite_place GCALL(0x460ff0, object_grid_link2_at)
#define body_remove GCALL(0x419300, body_retire)
#define sound_at(sound, a, b, c, pos, id) \
    GCALL(0x471d50, sound_play_flagged)(sound, a, b, c, (uint32_t)(uintptr_t)(pos), id)
#define sound_moving(a, sound, b, c, d, pos, vel, id) \
    GCALL(0x471e60, sound_moving_flagged)(a, sound, b, c, d, (uint32_t)(uintptr_t)(pos), (uint32_t)(uintptr_t)(vel), id)
#define sound_stop GCALL(0x471f40, sound_voice_stop)
#define blast GCALL(0x42aa30, explosion_effects)
#define car_place_test GCALL(0x4687f0, respawn_try)
#define car_place_reset GCALL(0x468f80, car_respawn_sparks)
#define car_place GCALL(0x468bb0, respawn_place)
#define net_is_game GCALL(0x45ef80, net_state_game)
#define net_is_host GCALL(0x45eed0, net_state_host)
#define net_local_car GCALL(0x48cf20, net_local_slot)
#define net_send_snatched GCALL(0x460a90, net_snatched_send)
#define hit_message GCALL(0x4600a0, net_kill_message)
#define snatch_mesh GCALL(0x48f930, snatch_mesh_update)

/* 0x46e3c0: any of the six animated textures (0x6002e0, 0x84 apart) on. */
int32_t GAMEAPI texture_anim_active(void)
{
    const uint8_t *p;

    for (p = (const uint8_t *)0x6002e0; (uint32_t)(uintptr_t)p < 0x6005f8; p += 0x84)
        if (*p)
            return 1;
    return 0;
}

/* 0x4906c0: the snatch entity is live (gates its texture animation). */
int32_t GAMEAPI snatch_active(void)
{
    return I32(g_snatch, STATE) != 0;
}

/* 0x46a9c0: counts steps while the race is in its first phases. */
void GAMEAPI race_early_ticks(void)
{
    if (I32(g_race, 0x57c) < 4)
        g_early_ticks++;
}

/* 0x41b4d0: request a force feedback effect on a car's pad record
 * (car+0x6f8). Priority +0x1e, strength +0x16. */
void GAMEAPI pad_effect_request(uint8_t *pad, uint32_t strength, uint32_t on, uint32_t kind, uint32_t off_value,
                                uint32_t priority)
{
    uint8_t current = pad[0x1e];

    if (((uint8_t)kind && (uint8_t)priority < 0x21) || (uint8_t)on != 1) {
        if (current <= (uint8_t)priority || current == 0) {
            pad[0x1f] = (uint8_t)on;
            if ((uint8_t)on) {
                pad[0x1e] = (uint8_t)priority;
                U16(pad, 0x16) = (uint8_t)strength;
                pad[0x1d] = (uint8_t)kind;
                return;
            }
            pad[0x1c] = (uint8_t)off_value;
        }
    }
}

/* 0x467c90: clear the weapon slot holding the given item (+0x67c, or else +0x67d). */
void GAMEAPI car_clear_item(uint8_t *car, uint32_t item)
{
    if (((uint32_t)(int32_t)(int8_t)car[0x67c] & 0xffffffdfu) == (item | 0x40))
        car[0x67c] = 0xff;
    else
        car[0x67d] = 0xff;
}

/* 0x40fe00: an AI car (a) cools its grudge against car b. */
void GAMEAPI ai_grudge_reduce(uint8_t *a, const uint8_t *b, uint32_t amount)
{
    uint32_t index = U16(b, 0x64e), level = I32(a, 0x6e8) >> 12 & 7;
    uint16_t step = (uint16_t)amount, *grudge = (uint16_t *)(a + 0x6ec + index * 2);
    uint8_t bit = (uint8_t)(1u << (index & 0x1f));

    step >>= level > 5;
    *grudge = step < *grudge ? (uint16_t)(*grudge - step) : 0;
    if ((a[0x6cb] & bit) && *grudge < g_grudge_levels[level])
        a[0x6cb] &= (uint8_t)~bit;
}

/* 0x40feb0: an AI car (a) builds a grudge against car b; past its level it
 * may take b over as its revenge target (+0x6e3) from another AI. */
void GAMEAPI ai_grudge_add(uint8_t *a, const uint8_t *b, int32_t amount)
{
    uint32_t index = U16(b, 0x64e), level, i, count;
    uint16_t *grudge = (uint16_t *)(a + 0x6ec + index * 2);
    uint8_t bit = (uint8_t)(1u << (index & 0x1f)), *best = 0;
    int32_t best_margin = 0;

    if ((int32_t)*grudge < 0xffff - amount)
        *grudge = (uint16_t)(*grudge + amount);
    else
        *grudge = 0xffff;
    level = I32(a, 0x6e8) >> 12 & 7;
    if (a[0x6cb] & bit)
        return;
    if (*grudge < g_grudge_levels[level])
        return;
    a[0x6cb] |= bit;
    if ((int8_t)a[0x6e3] != -1)
        return;
    count = g_race[0x54d];
    if (!count)
        return;
    for (i = 0; i < count; i++) {
        uint8_t *other = SLOT_CAR(i);
        int32_t margin;

        if (i == U16(a, 0x64e) || i == index)
            continue;
        if ((int32_t)(int8_t)other[0x6e3] != (int32_t)index)
            continue;
        margin = (int32_t)U16(other, 0x6ec + index * 2) - (int32_t)g_grudge_levels[I32(other, 0x6e8) >> 12 & 7];
        if (!best || margin < best_margin) {
            best = other;
            best_margin = margin;
        }
    }
    if (best && best_margin < (int32_t)*grudge - (int32_t)g_grudge_levels[level]) {
        best[0x6e3] = 0xff;
        a[0x6e3] = (uint8_t)index;
    }
}

/* 0x4102b0: grudges after a snatch between an AI car and a player. */
void GAMEAPI ai_grudge_snatched(uint8_t *snatch)
{
    uint8_t *owner = PTR(snatch, OWNER), *target = PTR(snatch, TARGET);

    if (owner[0x70c] == 8) {
        if (target[0x70c] != 8)
            ai_grudge_reduce(owner, target, 0x32);
    } else if (target[0x70c] == 8) {
        ai_grudge_add(target, owner, 0x32);
    }
}

/* 0x47a860: kick a car forward (all four wheels down only). */
void GAMEAPI car_kick_forward(uint8_t *car, int32_t amount)
{
    int32_t k = (int32_t)((uint32_t)amount << 20), facing[3], along;
    int i;

    for (i = 0; i < 4; i++)
        if (!(car[0x324 + i * 0xc4] & 1))
            return;
    I32(car, 0x40) += mul16(S16(car, 200), k);
    I32(car, 0x44) += mul16(S16(car, 0xce), k);
    I32(car, 0x48) += mul16(S16(car, 0xd4), k);
    /* (the original also takes the speed's square root here, unused) */
    facing[0] = SHL(S16(car, 200), 4);
    facing[1] = SHL(S16(car, 0xce), 4);
    facing[2] = SHL(S16(car, 0xd4), 4);
    along = vec_dot(facing, (const int32_t *)(car + 0x40));
    for (i = 0; i < 4; i++)
        I32(car, 0x37c + i * 0xc4) = mul16(along, 0x9697);
    car[0x660] = (uint8_t)amount;
    if (S16(car, 0x710))
        pad_effect_request(car + 0x6f8, 0xff, 1, 0x38, 0, 10);
}

/* 0x458f50: rebuild a short matrix from a forward vector (scaled up 16
 * on the way), keeping the old one when it is too short or upright. */
void GAMEAPI matrix_from_forward(int32_t *forward, int16_t *m)
{
    int32_t side[3], up[3];

    vec_normalise_shifted(forward, forward);
    side[0] = SHL(forward[2], 4);
    side[1] = 0;
    side[2] = forward[0] * -0x10;
    forward[0] = SHL(forward[0], 4);
    forward[1] = SHL(forward[1], 4);
    forward[2] = SHL(forward[2], 4);
    if (vec_length(side) < 0x400)
        return;
    vec_normalise(side);
    vec_cross(forward, side, up);
    if (vec_length(up) < 0xff78)
        return;
    m[0] = (int16_t)(side[0] >> 4);
    m[3] = (int16_t)(side[1] >> 4);
    m[6] = (int16_t)(side[2] >> 4);
    m[1] = (int16_t)(up[0] >> 4);
    m[4] = (int16_t)(up[1] >> 4);
    m[7] = (int16_t)(up[2] >> 4);
    m[2] = (int16_t)(forward[0] >> 4);
    m[5] = (int16_t)(forward[1] >> 4);
    m[8] = (int16_t)(forward[2] >> 4);
}

/* 0x48eb30: a car's facing column, flipped when +0x63f is clear. */
void GAMEAPI car_facing(const uint8_t *car, int32_t *out)
{
    int32_t x = S16(car, 0xc6) * 0x10, y = S16(car, 0xcc) * 0x10, z = S16(car, 0xd2) * 0x10;

    if (!car[0x63f]) {
        x = S16(car, 0xc6) * -0x10;
        y = S16(car, 0xcc) * -0x10;
        z = S16(car, 0xd2) * -0x10;
    }
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

/* 0x48ebf0 */
void GAMEAPI vec_clamp(int32_t *v, int32_t limit)
{
    int i;

    for (i = 0; i < 3; i++) {
        if (v[i] < -limit)
            v[i] = -limit;
        if (limit < v[i])
            v[i] = limit;
    }
}

/* 0x48eb80: momentum (body+0x40) an eighth of the way to a point, clamped. */
void GAMEAPI body_pull_toward(const int32_t *point, int32_t *body)
{
    int32_t d[3];

    d[0] = (point[0] - body[0]) >> 3;
    d[1] = (point[1] - body[1]) >> 3;
    d[2] = (point[2] - body[2]) >> 3;
    vec_clamp(d, 0xe0000);
    body[3] = d[0];
    body[4] = d[1];
    body[5] = d[2];
}

/* 0x48db80: target the nearest car up to 0x8000 ahead of the owner along the track. */
void GAMEAPI snatch_pick_target(uint8_t *owner, uint8_t *snatch)
{
    uint32_t count = g_race[0x54d], i;
    int32_t best = 0x20000, own, d;
    uint8_t *target = (uint8_t *)1;

    if (!count) {
        PTR_SET(snatch, TARGET, 1);
        return;
    }
    for (i = 0; i < count; i++) {
        if (SLOT_CAR(i) == owner)
            continue;
        own = SLOT_DISTANCE(U16(owner, 0x64e));
        d = SLOT_DISTANCE(i);
        if (d < own)
            d += 0x10000;
        d -= own;
        if (d < 0x8000 && d < best) {
            target = SLOT_CAR(i);
            best = d;
        }
    }
    PTR_SET(snatch, TARGET, target);
}

/* 0x48ea80: where the snatch aims: ahead of the target's nose, plus its velocity. */
void GAMEAPI snatch_aim_point(const uint8_t *snatch, int32_t *out)
{
    const uint8_t *t = PTR(snatch, TARGET);
    int32_t facing[3];

    car_facing(t, facing);
    out[0] = I32(t, 0x68) + I32(t, 0x34) + S16(t, 200) * 0x1000 + facing[0] * 0x40;
    out[2] = I32(t, 0x70) + I32(t, 0x3c) + S16(t, 0xd4) * 0x1000 + facing[2] * 0x40;
    out[1] = I32(t, 0x6c) + I32(t, 0x38) + S16(t, 0xce) * 0x1000 + facing[1] * 0x40;
}

/* 0x48ea40: snatch to target. */
void GAMEAPI snatch_to_target(int32_t *out, const uint8_t *snatch)
{
    const uint8_t *t = PTR(snatch, TARGET);

    out[2] = I32(t, 0x3c) - I32(snatch, 0x3c);
    out[0] = I32(t, 0x34) - I32(snatch, 0x34);
    out[1] = I32(t, 0x38) - I32(snatch, 0x38);
}

/* 0x48ea10: which side of the grab plane (+0x1d0) the target is on. */
int32_t GAMEAPI snatch_side(const uint8_t *snatch)
{
    int32_t v[3];

    snatch_to_target(v, snatch);
    return vec_dot(v, (const int32_t *)(snatch + 0x1d0));
}

/* 0x48e970: keep the matrix at the grab (+0x1b8, nine ints, scaled up 16). */
void GAMEAPI snatch_save_matrix(uint8_t *snatch)
{
    static const uint16_t from[9] = {0xc4, 0xca, 0xd0, 0xc6, 0xcc, 0xd2, 200, 0xce, 0xd4};
    int i;

    for (i = 0; i < 9; i++)
        I32(snatch, 0x1b8 + i * 4) = SHL(S16(snatch, from[i]), 4);
}

/* 0x48e610: the point 0x8c0 behind the owner, where a snatched car is put. */
void GAMEAPI snatch_drop_point(const uint8_t *snatch, int32_t *out)
{
    const uint8_t *o = PTR(snatch, OWNER);
    int32_t y = I32(o, 0x38), z = I32(o, 0x3c);

    out[1] = y;
    out[0] = I32(o, 0x34);
    out[2] = z;
    out[1] = S16(o, 0xce) * -0x8c0 + y;
    out[0] += S16(o, 200) * -0x8c0;
    out[2] = S16(o, 0xd4) * -0x8c0 + z;
}

/* 0x48e6a0: the target takes the owner's motion and wheel state. */
void GAMEAPI snatch_copy_motion(uint8_t *snatch)
{
    uint8_t *t = PTR(snatch, TARGET), *o = PTR(snatch, OWNER);
    static const uint16_t vectors[5] = {0x4c, 0xb8, 0x40, 0x68, 0x90};
    int i, j;

    for (i = 0; i < 5; i++) {
        int32_t a = I32(o, vectors[i] + 8), b = I32(o, vectors[i]);

        I32(t, vectors[i] + 4) = I32(o, vectors[i] + 4);
        I32(t, vectors[i]) = b;
        I32(t, vectors[i] + 8) = a;
    }
    I32(t, 0x8c) = I32(o, 0x8c);
    I32(t, 0x634) = I32(o, 0x634);
    for (j = 0x63c; j < 0x640; j++)
        t[j] = o[j];
    I32(t, 0x640) = I32(o, 0x640);
    for (i = 0; i < 4; i++)
        I32(t, 0x37c + i * 0xc4) = I32(o, 0x37c + i * 0xc4);
}

/* 0x48ee70: take the snatch's sprites out of the draw lists. */
void GAMEAPI snatch_hide_sprites(uint8_t *snatch)
{
    int i;

    for (i = 0; i < 5; i++)
        sprite_unlink(PTR(snatch, 0x1e0 + i * 4));
    for (i = 0; i < 0x20; i++)
        sprite_unlink(PTR(snatch, 0x1f4 + i * 4));
    for (i = 0; i < 10; i++) {
        if (I32(snatch, 0x2a4 + i * 0x10) > 0)
            sprite_unlink(PTR(snatch, 0x274 + i * 4));
        I32(snatch, 0x2a4 + i * 0x10) = 0;
    }
}

/* The original's inline colour fade: each colour byte times s / 256 (the
 * multiplies carry the upper bits of eax from one byte to the next, which
 * only matters for s past 0xff), top byte kept. */
static uint32_t colour_scale(uint32_t colour, uint32_t s)
{
    uint32_t eax, r2, r1, r0;

    eax = ((colour >> 16 & 0xff) * s) >> 8;
    r2 = eax & 0xff;
    eax = ((eax & ~0xffu) | (colour >> 8 & 0xff)) * s >> 8;
    r1 = eax & 0xff;
    eax = ((eax & ~0xffu) | (colour & 0xff)) * s >> 8;
    r0 = eax & 0xff;
    return (colour & 0xff000000u) | r2 << 16 | r1 << 8 | r0;
}

static uint32_t sprite_random(void)
{
    g_sprite_seed = g_sprite_seed * 0x41a7 & 0x7fffffff;
    return g_sprite_seed >> 15;
}

static void ring_point(const int32_t *a, const int32_t *b, int32_t radius, uint8_t angle, int32_t *out)
{
    int32_t first[3];

    vec_scale((int32_t)((uint32_t)(g_sin[angle] * radius) << 2), a, first);
    vec_scale((int32_t)((uint32_t)(g_cos[angle] * ((int32_t)((uint32_t)radius << 2) / 5)) << 2), b, out);
    out[1] += first[1];
    out[0] += first[0];
    out[2] += first[2];
}

static void ring_colour(uint32_t colour, uint32_t s, uint8_t *out)
{
    *(uint32_t *)out = (g_render_flags & 1) ? colour : colour_scale(colour, s);
    out[3] = (uint8_t)s;
}

/* 0x48f130: one of the five ring sprites around the snatch. */
void GAMEAPI snatch_ring_sprite(const int32_t *a, const int32_t *b, int32_t radius, int32_t i, int32_t *out,
                                uint8_t *colour)
{
    uint32_t alpha;

    g_ring_phase++;
    ring_point(a, b, radius, (uint8_t)((int32_t)((uint32_t)i << 8) / 5 + g_ring_phase), out);
    if (radius > 0x40)
        radius = 0x40;
    alpha = (sprite_random() & 0x3f) + (uint32_t)radius * 4;
    if (alpha > 0xff)
        alpha = 0xff;
    ring_colour(0xff8040, alpha, colour);
}

/* 0x48f2b0: one of the 32 inner ring sprites. */
void GAMEAPI snatch_ring2_sprite(const int32_t *a, const int32_t *b, int32_t radius, int32_t i, int32_t *out,
                                 uint8_t *colour)
{
    uint32_t alpha;
    int32_t n = (int32_t)((uint32_t)i << 8);

    g_ring2_phase++;
    ring_point(a, b, radius, (uint8_t)((n + (int32_t)((uint32_t)(n >> 31) & 0x1f)) >> 5) + g_ring2_phase, out);
    if (radius > 0x40)
        radius = 0x40;
    alpha = (sprite_random() & 0x1f) + (uint32_t)radius * 4;
    if (alpha > 0xff)
        alpha = 0xff;
    ring_colour(0xff7070, alpha, colour);
}

/* 0x48f430: a new spark: random offsets and colour. */
void GAMEAPI snatch_spark_reset(int32_t *spark)
{
    spark[0] = (int32_t)(sprite_random() & 0x7f) - 0x40;
    spark[1] = (int32_t)(sprite_random() & 0x7f) - 0x40;
    spark[3] = (int32_t)g_spark_colours[sprite_random() & 3];
}

/* 0x48f4f0: move spark i (of ten) out along the snatch's nose, fading in
 * and out; a new one when it gets to the end. */
void GAMEAPI snatch_spark_sprite(const int32_t *nose, const int32_t *a, const int32_t *b, uint8_t *snatch,
                                 int32_t radius, int32_t i, int32_t unused1, int32_t unused2)
{
    uint8_t *sprite = PTR(snatch, 0x274 + i * 4), *spark = snatch + 0x29c + i * 0x10;
    int32_t t, sa[3], sb[3], p[3];
    int16_t y, z;

    (void)radius;
    (void)unused1;
    (void)unused2;
    if (I32(spark, 8) > 0)
        sprite_unlink(sprite);
    t = I32(spark, 8) + 0x19;
    I32(spark, 8) = t;
    if (t > 0x180) {
        I32(spark, 8) = t - 0x180;
        snatch_spark_reset((int32_t *)spark);
    }
    t = I32(spark, 8);
    if (t <= 0)
        return;
    vec_scale((int32_t)((uint32_t)I32(spark, 0) << 16), a, sa);
    vec_scale((int32_t)((uint32_t)I32(spark, 4) << 16), b, sb);
    vec_scale((int32_t)((uint32_t)t << 16), nose, p);
    p[0] = p[0] + sa[0] + sb[0];
    p[1] = p[1] + sa[1] + sb[1];
    p[2] = p[2] + sa[2] + sb[2];
    t = I32(spark, 8);
    if (t < 0x80)
        ring_colour((uint32_t)I32(spark, 0xc), (uint32_t)(t * 2), sprite + 0x108);
    else if (t < 0x141)
        I32(sprite, 0x108) = I32(spark, 0xc);
    else
        ring_colour((uint32_t)I32(spark, 0xc), (uint32_t)(0x5ff - t * 4), sprite + 0x108);
    S16(sprite, 0x10) = (int16_t)((uint32_t)(I32(snatch, 0x34) + p[0]) >> 16);
    y = (int16_t)((uint32_t)(I32(snatch, 0x38) + p[1]) >> 16);
    S16(sprite, 0x12) = y;
    z = (int16_t)((uint32_t)(I32(snatch, 0x3c) + p[2]) >> 16);
    S16(sprite, 0x14) = z;
    if (I32(spark, 8) > 0)
        sprite_place(sprite, SHL(S16(sprite, 0x10), 16), SHL(y, 16), SHL(z, 16));
}

/* 0x48eef0: place the snatch's ring and spark sprites, grow it. */
void GAMEAPI snatch_effects(uint8_t *snatch)
{
    int32_t a[3], b[3], p[3], radius;
    int i;

    a[0] = SHL(S16(snatch, 0xc4), 4);
    a[1] = SHL(S16(snatch, 0xca), 4);
    a[2] = SHL(S16(snatch, 0xd0), 4);
    b[0] = SHL(S16(snatch, 0xc6), 4);
    b[1] = SHL(S16(snatch, 0xcc), 4);
    b[2] = SHL(S16(snatch, 0xd2), 4);
    for (i = 0; i < 5; i++) {
        uint8_t *sprite = PTR(snatch, 0x1e0 + i * 4);
        int16_t z;

        sprite_unlink(sprite);
        snatch_ring_sprite(a, b, I32(snatch, 0x33c) / 2, i, p, sprite + 0x108);
        S16(sprite, 0x10) = (int16_t)((uint32_t)(I32(snatch, 0x34) + p[0]) >> 16);
        S16(sprite, 0x12) = (int16_t)((uint32_t)(p[1] + I32(snatch, 0x38)) >> 16);
        z = (int16_t)((uint32_t)(I32(snatch, 0x3c) + p[2]) >> 16);
        S16(sprite, 0x14) = z;
        sprite_place(sprite, SHL(S16(sprite, 0x10), 16), SHL(S16(sprite, 0x12), 16), SHL(z, 16));
    }
    for (i = 0; i < 0x20; i++) {
        uint8_t *sprite = PTR(snatch, 0x1f4 + i * 4);
        int16_t z;

        sprite_unlink(sprite);
        snatch_ring2_sprite(a, b, I32(snatch, 0x33c) / 2, i, p, sprite + 0x108);
        S16(sprite, 0x10) = (int16_t)((uint32_t)(I32(snatch, 0x34) + p[0]) >> 16);
        S16(sprite, 0x12) = (int16_t)((uint32_t)(p[1] + I32(snatch, 0x38)) >> 16);
        z = (int16_t)((uint32_t)(I32(snatch, 0x3c) + p[2]) >> 16);
        S16(sprite, 0x14) = z;
        sprite_place(sprite, SHL(S16(sprite, 0x10), 16), SHL(S16(sprite, 0x12), 16), SHL(z, 16));
    }
    p[0] = S16(snatch, 200) * -0x10;
    p[1] = S16(snatch, 0xce) * -0x10;
    p[2] = S16(snatch, 0xd4) * -0x10;
    for (i = 0; i < 10; i++)
        snatch_spark_sprite(p, a, b, snatch, I32(snatch, 0x33c) / 2, i, 0, 0);
    radius = I32(snatch, 0x33c);
    if (I32(snatch, STATE) == 0x40 || I32(snatch, STATE) == 0x80) {
        if (radius < 0xb0)
            I32(snatch, 0x33c) = radius + 8;
    } else if (radius < 0x80) {
        I32(snatch, 0x33c) = radius + 3;
    }
    snatch_mesh(snatch);
}

/* 0x48dea0, state 1: rise out of the owner's nose for 40 steps, then pick a target and go. */
void GAMEAPI snatch_launch(uint8_t *snatch)
{
    uint8_t *o = PTR(snatch, OWNER);
    int32_t p[3], time;

    car_facing(o, p);
    vec_scale(I32(snatch, 0x18c), p, p);
    p[0] = p[0] + I32(o, 0x34) + S16(o, 200) * 0x800;
    p[1] = S16(o, 0xce) * 0x800 + I32(o, 0x38) + p[1];
    p[2] = S16(o, 0xd4) * 0x800 + I32(o, 0x3c) + p[2];
    time = I32(snatch, STEPS);
    I32(snatch, 0x18c) += time * -0xccc + 0x20000;
    I32(snatch, STEPS) = time + 1;
    if (time + 1 < 0x29) {
        if (S16(PTR(snatch, OWNER), 0x710)) {
            uint8_t strength = (uint8_t)(snatch[STEPS] + 0x40);

            pad_effect_request(PTR(snatch, OWNER) + 0x6f8, strength, 1, strength, 0, 10);
        }
    } else {
        I32(snatch, STATE) = 2;
        I32(snatch, STEPS) = 0;
        snatch[GRAB_TIME] = 0;
        snatch_pick_target(o, snatch);
        I32(snatch, 0x19c) = SHL(S16(o, 200), 4);
        I32(snatch, 0x1a0) = SHL(S16(o, 0xce), 4);
        I32(snatch, 0x1a4) = SHL(S16(o, 0xd4), 4);
        I32(snatch, 0x1a8) = 0x4000000;
        sound_at(0x20, 6, 0x400, 15000, PTR(snatch, OWNER) + 0x34, PTR(snatch, OWNER)[0x6a4]);
        if (S16(PTR(snatch, OWNER), 0x710))
            pad_effect_request(PTR(snatch, OWNER) + 0x6f8, 0xff, 1, 0x32, 0, 10);
        snatch[VOICE] = (uint8_t)sound_moving(1, 0x21, 9, 0x400, 16000, snatch + 0x34, snatch + 0x68,
                                              PTR(snatch, OWNER)[0x6a4]);
    }
    body_pull_toward(p, (int32_t *)(snatch + 0x34));
    I32(snatch, 0x1ac) = SHL(S16(o, 200), 4);
    I32(snatch, 0x1b0) = SHL(S16(o, 0xce), 4);
    I32(snatch, 0x1b4) = SHL(S16(o, 0xd4), 4);
}

/* 0x48e0c0, state 2: home in on the target for up to 300 steps. Returns 1
 * when the state ended (timed out or the target reached). */
int32_t GAMEAPI snatch_fly(uint8_t *snatch)
{
    int32_t time = I32(snatch, STEPS) + 1, *heading = (int32_t *)(snatch + 0x1ac), k, x2, y2, z2;

    I32(snatch, STEPS) = time;
    if (time > 299) {
        I32(snatch, STATE) = 4;
        I32(snatch, STEPS) = 0;
        return 1;
    }
    if (I32(snatch, TARGET) != 1) {
        int32_t aim[3], d[3], length;

        snatch_aim_point(snatch, aim);
        d[0] = (aim[0] >> 10) - (I32(snatch, 0x34) >> 10);
        d[1] = (aim[1] >> 10) - (I32(snatch, 0x38) >> 10);
        d[2] = (aim[2] >> 10) - (I32(snatch, 0x3c) >> 10);
        length = vec_length(d);
        if (length < 0x2000) {
            I32(snatch, STATE) = 0x40;
            I32(snatch, STEPS) = 0;
            sound_at(0x22, 6, 0x400, 16000, snatch + 0x34, PTR(snatch, OWNER)[0x6a4]);
            return 1;
        }
        if (length > 0x7f0000) {
            I32(snatch, TARGET) = 1;
            return 0;
        }
        I32(snatch, 0x1a8) = (int32_t)((uint32_t)length << 6);
        vec_div(d, length);
        I32(snatch, 0x19c) = d[0];
        I32(snatch, 0x1a0) = d[1];
        I32(snatch, 0x1a4) = d[2];
    }
    k = I32(snatch, 0x1a8) >> 20;
    if (k < 1)
        k = 1;
    else if (k > 8)
        k = 8;
    vec_blend(heading, heading, (const int32_t *)(snatch + 0x19c), k, 1, 0);
    y2 = I32(snatch, 0x1b0) * 2;
    z2 = I32(snatch, 0x1b4) * 2;
    x2 = heading[0] * 2;
    I32(snatch, 0x74) = (x2 + (x2 >> 1)) - (I32(snatch, 0x68) >> 5);
    I32(snatch, 0x78) = (y2 + (y2 >> 1)) - (I32(snatch, 0x6c) >> 5);
    I32(snatch, 0x7c) = (z2 + (z2 >> 1)) - (I32(snatch, 0x70) >> 5);
    return 0;
}

/* 0x48ddd0, states 4 (with a blast) and 0x10: end the snatch. */
void GAMEAPI snatch_finish(uint8_t *snatch, int32_t explode)
{
    if (explode == 1) {
        int32_t pos[3], dir[3];

        pos[0] = I32(snatch, 0x34);
        pos[2] = I32(snatch, 0x3c);
        pos[1] = I32(snatch, 0x38) + 0x280000;
        dir[0] = 0;
        dir[1] = (int32_t)0xffc00000;
        dir[2] = 0;
        blast(pos, dir, 3, 0x50a28, 0xb);
    }
    grid_unlink(snatch);
    body_remove(snatch);
    I32(snatch, STEPS) = 0;
    snatch[GRAB_TIME] = 0;
    I32(snatch, STATE) = 0;
    g_snatches_live--;
    snatch_hide_sprites(snatch);
    if ((int32_t)snatch[VOICE] < g_voice_count) {
        sound_stop(snatch[VOICE]);
        snatch[VOICE] = 0xff;
    }
    car_clear_item(PTR(snatch, OWNER), 7);
}

/* 0x48e290, state 0x40: hold onto the target for 60 steps, slowing, then
 * remember the grab plane and hold (0x80). */
void GAMEAPI snatch_grab(uint8_t *snatch)
{
    int32_t aim[3], facing[3], k, time;
    uint8_t *t;

    snatch_aim_point(snatch, aim);
    body_pull_toward(aim, (int32_t *)(snatch + 0x34));
    k = (I32(snatch, STEPS) - 0x3b) * 0x10000 / 0x3b + 0x18000;
    if (k > 0x10000)
        k = 0x10000;
    vec_scale(k, (const int32_t *)(snatch + 0x40), (int32_t *)(snatch + 0x40));
    time = I32(snatch, STEPS) + 1;
    I32(snatch, STEPS) = time;
    if (time > 0x3b) {
        I32(snatch, STATE) = 0x80;
        I32(snatch, STEPS) = 0;
        snatch_save_matrix(snatch);
        I32(snatch, 0x1dc) = snatch_side(snatch);
        return;
    }
    t = PTR(snatch, TARGET);
    I32(snatch, 0x1ac) = I32(snatch, 0x34) - I32(t, 0x34);
    I32(snatch, 0x1b0) = I32(snatch, 0x38) - I32(t, 0x38);
    I32(snatch, 0x1b4) = I32(snatch, 0x3c) - I32(t, 0x3c);
    car_facing(t, facing);
    I32(snatch, 0x1ac) += facing[0] * -0x40;
    I32(snatch, 0x1b0) += facing[1] * -0x40;
    I32(snatch, 0x1b4) += facing[2] * -0x40;
}

/* 0x48e470: swap the target in behind the owner, if there's room. */
void GAMEAPI snatch_swap(uint8_t *snatch)
{
    uint8_t *orientation = PTR(snatch, OWNER) + 0x58, *t, *o;
    int32_t found = (int32_t)(uintptr_t)snatch;
    uint32_t slot;

    snatch_drop_point(snatch, g_snatch_drop);
    if (!car_place_test(PTR(snatch, TARGET), g_snatch_drop, (const int32_t *)orientation, (int16_t *)g_snatch_matrix, 0,
                        (uint32_t *)&found)) {
        I32(snatch, STATE) = 4;
        I32(snatch, STEPS) = 0;
        return;
    }
    slot = U16(PTR(snatch, OWNER), 0x64e);
    car_place_reset(PTR(snatch, TARGET), 1);
    car_place(PTR(snatch, TARGET), g_snatch_drop, (const int32_t *)orientation, (const int16_t *)g_snatch_matrix,
              GP(g_car_slots + slot * CAR_SLOT_SIZE + 0x64),
              *(uint32_t *)(g_car_slots + slot * CAR_SLOT_SIZE + 0x68), (uint32_t)found);
    sound_at(0x23, 6, 0x400, 16000, snatch + 0x34, PTR(snatch, OWNER)[0x6a4]);
    snatch_copy_motion(snatch);
    t = PTR(snatch, TARGET);
    o = PTR(snatch, OWNER);
    PTR_SET(t, 0x680, o);
    t[0x688] = 0x5a;
    I32(t, 0x684) = I32(t, 0x6fc);
    car_kick_forward(o, 2);
    t[0x689] = 0x1f;
    I32(snatch, STATE) = 0x10;
    I32(snatch, STEPS) = 0;
    ai_grudge_snatched(snatch);
    t = PTR(snatch, TARGET);
    o = PTR(snatch, OWNER);
    if (net_is_game() == 1 && net_is_host() == 1 && (uint32_t)U16(t, 0x64e) == (uint32_t)net_local_car())
        net_send_snatched();
    if (t && o && t[0x70c] != 8)
        hit_message(U16(t, 0x64e), U16(o, 0x64e), 0xc);
}

/* 0x48e3c0, state 0x80: hold still up to 90 steps; swap when the target
 * crosses the grab plane close by. */
void GAMEAPI snatch_hold(uint8_t *snatch)
{
    int32_t time = I32(snatch, STEPS) + 1;
    uint32_t before, after;

    I32(snatch, STEPS) = time;
    if (time > 0x59) {
        I32(snatch, STATE) = 4;
        I32(snatch, STEPS) = 0;
        return;
    }
    before = (uint32_t)I32(snatch, 0x1dc);
    after = (uint32_t)snatch_side(snatch);
    I32(snatch, 0x1dc) = (int32_t)after;
    if ((int32_t)(after ^ before) < 0) {
        int32_t d[3];

        snatch_to_target(d, snatch);
        d[1] >>= 12;
        d[0] >>= 12;
        d[2] >>= 12;
        if (vec_length(d) < 0x700)
            snatch_swap(snatch);
    }
    I32(snatch, 0x40) = 0;
    I32(snatch, 0x44) = 0;
    I32(snatch, 0x48) = 0;
}

/* 0x48dc60 */
void GAMEAPI snatch_step(uint8_t *snatch)
{
    switch (I32(snatch, STATE)) {
    case 1:
        snatch_launch(snatch);
        break;
    case 2:
        if (snatch_fly(snatch) && (int32_t)snatch[VOICE] < g_voice_count) {
            sound_stop(snatch[VOICE]);
            snatch[VOICE] = 0xff;
        }
        break;
    case 4:
        snatch_finish(snatch, 1);
        break;
    case 0x10:
        snatch_finish(snatch, 0);
        break;
    case 0x40:
        snatch_grab(snatch);
        if ((int8_t)snatch[GRAB_TIME] != -1)
            snatch[GRAB_TIME]++;
        break;
    case 0x80:
        snatch_hold(snatch);
        break;
    }
    matrix_from_forward((int32_t *)(snatch + 0x1ac), (int16_t *)(snatch + 0xc4));
    if (I32(snatch, STATE))
        snatch_effects(snatch);
}

/* 0x48dc40, once per step after the world step. */
void GAMEAPI snatch_update(void)
{
    if (I32(g_snatch, STATE))
        snatch_step(g_snatch);
}
