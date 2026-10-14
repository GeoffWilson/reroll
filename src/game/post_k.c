/* Per-step updates after the physics (sim_step 0x444120), group K:
 *
 *  - ripple_update (0x43e270): the track's rippling vertices (water and
 *    the like), its sound markers, and random particle bursts and puffs
 *    from those vertices.
 *  - crumble_update (0x405f70): buildings that collapse, face by face as
 *    their vertices sink below a level, with debris, dust and bomblets.
 *  - bomblet_update (0x46bfa0): the bomblets that collapses (and other
 *    bomblets) throw out, which blow up when their fuse runs out.
 *  - glint_update (0x443180): the short-lived glints dust_add starts.
 *  - race_progress (0x46a530): the race's state machine (intro, count-down,
 *    racing, finished), the finishers' messages and the pause check;
 *    returns 1 when the race is over.
 *
 * The sound mixer (0x4722d0), the sound volume calls, the pause menu's
 * setup and the effect spawners (0x42aa30 tree) stay original. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define PTR(p, off) ((uint8_t *)(uintptr_t)*(uint32_t *)((p) + (off))) /* 32-bit pointers in game memory */
#define A(p) ((uint32_t)(uintptr_t)(p))
#define SET_PTR(p, off, v) (*(uint32_t *)((p) + (off)) = A(v))
#define G_SET(a, v) (*(uint32_t *)(a) = A(v))

#define g_race GP(0x4b1960)
#define g_race_ticks (*(uint32_t *)0x6c0a50)
#define g_seed (*(uint32_t *)0x4b11d8)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_screen_scale (*(int32_t *)0x6cd410)
#define g_gte_vector_words ((uint32_t *)0x6ccc64)
#define g_gte_vector ((int16_t *)0x6ccc64)
#define g_gte_short ((int16_t *)0x6ccc88) /* x, y, z, 4 bytes apart */
#define g_gte_result ((const int32_t *)0x6cccc8)

/* Ripples: 0x18-byte records {int16 *vertex; int16 base x, y, z; ...;
 * +0x10 int16 phase, +0x12 uint16 offset, +0x14 kind, +0x15 shift}. */
#define g_ripple_rules GP(0x6c1eb0)
#define g_ripple_count (*(int32_t *)0x6c1eb4)
#define g_ripples GP(0x6c1eb8)
#define g_wave ((const int16_t *)0x4b24f8)
#define g_ripples_moved (*(uint32_t *)0x4b2c64)

/* Collapses: 4 entries of 0xa8 bytes; a queue of 64 {object, next}. */
#define g_crumbles GP(0x6d04a4)
#define g_crumble_queue_head (*(uint8_t *)0x6d04a1)
#define g_crumble_queue_count (*(uint8_t *)0x6d04a2)
#define CRUMBLE_QUEUE_OBJECT(i) (*(uint32_t *)(uintptr_t)(0x6d04c0 + (i) * 8))
#define CRUMBLE_QUEUE_STATE(i) (*(uint32_t *)(uintptr_t)(0x6d04c4 + (i) * 8))
#define g_crumble_setup_step (*(uint32_t *)0x4a7ce4)
#define g_crumble_model GP(0x6cf160)
#define g_crumble_object GP(0x6cf164)
#define g_crumble_shape GP(0x6cf168)
#define g_crumble_entry GP(0x6cf16c)
#define g_settings GP(0x4b2198)

/* Bomblets: 0x24 entries of 8 bytes {object, fuse, slot, kind, owner}. */
#define g_bomblets GP(0x600664)
#define g_bomblet_next (*(uint32_t *)0x4d3dd8)
#define g_bomblet_live (*(uint32_t *)0x4d3de0)
#define g_bomblet_busy ((uint8_t *)0x600640)
#define g_bomblet_object(i) GP(0x600680 + (i) * 4)
#define g_bomblet_shape ((int32_t *)0x600600)

/* Glints: 10 records of 0x14 bytes at 0x4c1b20. */
#define g_glints ((uint8_t *)0x4c1b20)
#define g_glint_next (*(uint32_t *)0x4c1b18)
#define g_detail_flags (*(uint8_t *)0x6ccd84)

/* On-screen messages: 0x1c records of 0x50 bytes; the font. */
#define g_texts ((uint8_t *)0x5f6944)
#define g_menu_texts ((uint8_t *)0x6b95a4)
#define g_font GP(0x5f6940)

/* Still original, owned elsewhere or out of scope. */
#define ripple_marker GCALL(0x4411c0, mesh_rows_touch)
#define particle_slot GCALL(0x462450, piece_take)
#define grid_place GCALL(0x460ff0, object_grid_link2_at)
#define effect_new GCALL(0x42ac30, particle_take)
#define effect_fade GCALL(0x42cd40, particle_fade)
#define effect_burst GCALL(0x42aa30, explosion_effects)
#define sound_play GCALL(0x471c90, sound_play_body)
#define body_remove GCALL(0x419300, body_retire)
#define body_shape GCALL(0x419150, body_spheres_set)
#define crumble_start GCALL(0x4427a0, body_triggers_apply)
#define lap_split GCALL(0x43d750, slot_lap_record)
#define slot_boost GCALL(0x47aa60, boost_start)
#define read_controls GCALL(0x45e8c0, controls_fetch_packet)
#define network_game GCALL(0x45ef80, net_state_game)
#define network_host GCALL(0x45eed0, net_state_host)
#define network_finish GCALL(0x45fc40, net_finish_send)
#define race_end GCALL(0x439a70, race_results_update)
#define sound_pause GCALL(0x471c30, sound_cd_pause)
#define sound_master GCALL(0x471a60, sound_volume_mix)
#define sound_balance GCALL(0x471af0, sound_cd_volume)
#define menu_open GCALL(0x45ddb0, netplay_send_byte)
#define local_player GCALL(0x48cf20, net_local_slot)
#define pause_send GCALL(0x45fa60, net_pause_request)
#define pause_receive GCALL(0x45fe70, net_pause_due)
#define pause_menu GCALL(0x4600a0, net_kill_message)
#define pause_layout GCALL(0x423f20, view_full_screen)
#define pause_panel GCALL(0x463ca0, pause_open)
#define pause_bar GCALL(0x464c10, pause_menu_slider_set)
#define pause_items GCALL(0x463f90, pause_menu_draw)
#define pause_sounds GCALL(0x46fe60, snd_pause_all)
#define pause_player GCALL(0x489f70, ff_device_stop_all)

static uint32_t next_seed(uint32_t s)
{
    return s * 16807 & 0x7fffffff;
}

/* 0x462690: a particle at pos moving at vel, coloured by kind (0x11..0x19;
 * 0x14 also quarters vel). */
void GAMEAPI particle_spawn(const int32_t *pos, int32_t *vel, int32_t kind)
{
    uint32_t colour, mode;
    uint16_t life;
    uint8_t glow = 1;
    int32_t index;
    uint8_t *p;

    switch (kind) {
    case 0x11: colour = 0x2020, mode = 0xae000000, life = 8, glow = 0; break;
    case 0x12: colour = 0x183420, mode = 0xae141414, life = 9, glow = 0; break;
    case 0x13: colour = 0x7f4040, mode = 0xae300000, life = 10, glow = 0; break;
    case 0x14:
        colour = 0x183040, mode = 0xae081018, life = 10, glow = 0;
        vel[0] >>= 2;
        vel[1] >>= 2;
        vel[2] >>= 2;
        break;
    case 0x15: colour = 0x9fff9f, mode = 0xae1f3f1f, life = 0xf; break;
    case 0x16: colour = 0x4fffff, mode = 0xae20205f, life = 0xd; break;
    case 0x17: colour = 0xff004f, mode = 0xae5f0020, life = 0xd; break;
    case 0x18: colour = 0x9f, mode = 0xae000020, life = 0x15; break;
    case 0x19: colour = 0x409f, mode = 0xae000020, life = 0x2b; break;
    default: colour = 0x4f5f5f, mode = 0xae5f2020, life = 0x12; break;
    }
    index = particle_slot();
    if (index == -1)
        return;
    p = GP(0x6b983c) + (uint32_t)index * 0x80;
    I32(p, 0x4c) = pos[0];
    I32(p, 0x50) = pos[1];
    I32(p, 0x54) = pos[2];
    grid_place(p, I32(p, 0x4c), I32(p, 0x50), I32(p, 0x54));
    S16(p, 0x66) = (int16_t)(I32(p, 0x50) >> 16);
    U16(p, 8) = life;
    S16(p, 0x6a) = 1;
    S16(p, 0x64) = (int16_t)(I32(p, 0x4c) >> 16);
    S16(p, 0x68) = (int16_t)(I32(p, 0x54) >> 16);
    I32(p, 0x58) = vel[0];
    I32(p, 0x5c) = vel[1];
    I32(p, 0x60) = vel[2];
    U32(p, 0x30) = colour;
    U32(p, 0x14) = mode;
    U8(p, 0x6c) = glow;
}

/* 0x42b8f0: a slow fading puff (effect type 8) at pos. */
void GAMEAPI effect_puff(const int32_t *pos)
{
    uint8_t *e = effect_new();

    U32(e, 0) = 8;
    U8(e, 0x95) = 0x18;
    U16(e, 0x96) = 0x14;
    U16(e, 0xa2) = 0;
    I32(e, 0x88) = pos[0];
    I32(e, 0x8c) = pos[1];
    I32(e, 0x90) = pos[2];
    U16(e, 0x9c) = 0;
    U16(e, 0x9e) = 0xfc00;
    U16(e, 0xa0) = 0;
    effect_fade(e, 0x14, 1);
    grid_place(e, I32(e, 0x88), I32(e, 0x8c), I32(e, 0x90));
    U8(e, 0x94) = 1;
    U32(e, 0xa4) = *(uint32_t *)0x6cc940;
}

/* 0x43e270 */
void GAMEAPI ripple_update(void)
{
    uint8_t *rules = g_ripple_rules;
    uint32_t packed = 0; /* the original reuses its loop counter's slot: 0 after the loop */
    int32_t pos[3], off[3];
    uint32_t r, i, n;

    if (!U8(rules, 0xfc))
        return;
    if (g_ripple_count > 0) {
        uint8_t *rec = g_ripples;
        uint32_t left = (uint32_t)g_ripple_count;

        do {
            int16_t t = (int16_t)(S16(rec, 0x10) + 1);
            int32_t phase = t, shade, wave;
            uint8_t shift;
            int16_t *v, d;
            uint8_t *vb;

            S16(rec, 0x10) = t;
            switch (U8(rec, 0x14)) {
            case 1:
                phase *= 3;
                if (S16(rec, 0x10) > 0x55)
                    S16(rec, 0x10) = 0;
                break;
            case 2:
                phase = (int32_t)((uint32_t)phase << 2);
                if (S16(rec, 0x10) > 0x3f)
                    S16(rec, 0x10) = 0;
                break;
            case 3:
                phase *= 6;
                if (S16(rec, 0x10) > 0x2a)
                    S16(rec, 0x10) = 0;
                break;
            default:
                phase = (int32_t)((uint32_t)phase << 1);
                if (S16(rec, 0x10) > 0x7f)
                    S16(rec, 0x10) = 0;
                break;
            }
            i = ((uint32_t)U16(rec, 0x12) + (uint32_t)phase) & 0xff;
            shift = U8(rec, 0x15);
            v = (int16_t *)(uintptr_t)*(uint32_t *)rec;
            wave = g_wave[i];
            d = (int16_t)(wave >> ((12u - shift) & 31));
            v[0] = (int16_t)(S16(rec, 4) + d);
            v[1] = (int16_t)((int16_t)(wave >> ((11u - shift) & 31)) + S16(rec, 6));
            v[2] = (int16_t)(S16(rec, 8) + d);
            shade = 16 - (g_wave[i] >> 9);
            g_ripples_moved = 1;
            if (shade < 0)
                shade = 0;
            else if (shade > 0x1f)
                shade = 0x1f;
            vb = PTR(rec, 0);
            U16(vb, 6) = (uint16_t)((0xfbc0u - ((uint32_t)shade << 6)) | (U8(vb, 6) & 0x3f));
            rec += 0x18;
        } while (--left);
        rules = g_ripple_rules;
    }
    {
        const uint16_t *m = ((const uint16_t *)(uintptr_t)*(uint32_t *)(rules + 0x118));

        for (n = U8(rules, 0x11c); n; n--, m += 4) {
            packed = (packed & 0xffc00000) | (uint32_t)(m[1] & 0x7ff) << 11 | (m[0] & 0x7ff);
            ripple_marker(packed, *(const uint8_t *)(m + 2));
        }
    }
    r = g_seed = next_seed(g_seed);
    i = r >> 16 & 0x1ff;
    if (i < (uint32_t)g_ripple_count) {
        const int16_t *v = ((int16_t *)(uintptr_t)*(uint32_t *)(g_ripples + i * 0x18));

        pos[0] = (int32_t)((uint32_t)v[0] << 16);
        pos[1] = (int32_t)((uint32_t)v[1] << 16);
        pos[2] = (int32_t)((uint32_t)v[2] << 16);
        r = next_seed(r);
        for (n = r >> 16 & 7; n; n--) {
            uint32_t h;

            g_seed = next_seed(r);
            h = g_seed >> 16;
            off[0] = (int32_t)(((h & 0xf) - 7) << 15);
            off[2] = (int32_t)(((h >> 10 & 0xf) - 7) << 15);
            off[1] = (int32_t)((uint32_t)(-4 - (int32_t)(h >> 6 & 0xf)) << 15);
            particle_spawn(pos, off, 0x18);
            r = g_seed;
        }
    }
    g_seed = next_seed(r);
    i = g_seed >> 16 & 0xff;
    if (i < (uint32_t)g_ripple_count) {
        const int16_t *v = ((int16_t *)(uintptr_t)*(uint32_t *)(g_ripples + i * 0x18));

        pos[1] = (int32_t)((uint32_t)v[1] << 16);
        pos[2] = (int32_t)((uint32_t)v[2] << 16);
        pos[0] = (int32_t)((uint32_t)v[0] << 16);
        effect_puff(pos);
    }
}

/* 0x442f70: a glint effect of type `type` near the int16 point p (p+8..10
 * an RGB colour, doubled), jittered by up to 16 << (p4 + (flags >> 2 & ~1)). */
uint8_t *GAMEAPI glint_spawn(const int16_t *p, uint32_t type, uint32_t fade, uint32_t spread, uint32_t flags)
{
    uint8_t *e = effect_new();
    uint32_t r, v, shift, c;
    const uint8_t *rgb = (const uint8_t *)p + 8;
    uint8_t colour[4];
    int i;

    U32(e, 0) = type;
    U16(e, 0xa2) = 0x8000;
    U16(e, 0x9c) = 0;
    U16(e, 0x9e) = (uint16_t)fade;
    U16(e, 0xa0) = 0;
    I32(e, 0x88) = (int32_t)((uint32_t)p[0] << 16);
    I32(e, 0x8c) = (int32_t)((uint32_t)p[1] << 16);
    I32(e, 0x90) = (int32_t)((uint32_t)p[2] << 16);
    r = g_seed = next_seed(g_seed);
    shift = (((flags >> 2) & 0x3ffffffb) + spread) & 31;
    v = r >> 15 & 0xffff;
    I32(e, 0x88) += (int32_t)(((v & 0x1f) - 16) << shift);
    I32(e, 0x8c) += (int32_t)(((v >> 5 & 0x1f) - 16) << shift);
    I32(e, 0x90) += (int32_t)(((v >> 10 & 0x1f) - 16) << shift);
    grid_place(e, I32(e, 0x88), I32(e, 0x8c), I32(e, 0x90));
    U8(e, 0x94) = 1;
    U32(e, 0xa4) = (flags & 0x10) ? *(uint32_t *)0x6cc940 : *(uint32_t *)0x6ccb9c;
    for (i = 0; i < 3; i++) {
        c = (uint32_t)rgb[i] * 2;
        colour[i] = c >= 0xff ? 0xff : (uint8_t)c;
    }
    colour[3] = 0xff;
    U32(e, 0x98) = (uint32_t)colour[0] | (uint32_t)colour[1] << 8 | (uint32_t)colour[2] << 16 | 0xffu << 24;
    return e;
}

/* 0x443180 */
void GAMEAPI glint_update(void)
{
    uint8_t *g;

    for (g = g_glints; g < g_glints + 10 * 0x14; g += 0x14) {
        int32_t left = I32(g, 0xc), wait;
        uint8_t flags, c;
        uint32_t r;
        uint8_t *e;

        if (left <= 0)
            continue;
        S16(g, 6) = (int16_t)(S16(g, 6) + 1);
        I32(g, 0xc) = --left;
        if ((int32_t)S16(g, 6) < I32(g, 0x10))
            continue;
        flags = U8(g, 0xb);
        S16(g, 6) = 0;
        r = g_seed = next_seed(g_seed);
        wait = (int32_t)(((r >> 15) << 3) >> 16) - 4;
        I32(g, 0x10) = wait + 4;
        if (left < 0x1c2) {
            int32_t x = (int32_t)((uint32_t)left << 8);
            int32_t q = (int32_t)(((int64_t)x * 0xd8a5df) >> 33);

            c = (uint8_t)(q + ((uint32_t)q >> 31) + 0x40);
            U8(g, 8) = c;
            U8(g, 9) = c;
            U8(g, 10) = c;
        }
        e = glint_spawn((const int16_t *)g, 0x22, 0xfffffc00, 0xf, flags);
        U8(e, 0x95) = (uint8_t)(((flags & 0xef) + 3) << 2);
        U16(e, 0x96) = (uint16_t)(wait * 8 + 0x64);
    }
}

/* 0x443100: start a glint at pos if the detail setting allows. */
void GAMEAPI dust_add(const int32_t *pos)
{
    uint8_t *g = g_glints + g_glint_next * 0x14;

    if (!(g_detail_flags & 2))
        return;
    I32(g, 0xc) = 900;
    I32(g, 0x10) = 4;
    S16(g, 6) = 4;
    S16(g, 0) = (int16_t)(pos[0] >> 16);
    S16(g, 2) = (int16_t)(pos[1] >> 16);
    U32(g, 8) = 0xffffff;
    S16(g, 4) = (int16_t)(pos[2] >> 16);
    U8(g, 0xb) = 0x15;
    if (++g_glint_next >= 10)
        g_glint_next = 0;
}

/* 0x46bd60: throw a bomblet of kind 0..3 (smallest first) from pos at vel
 * (>> 8); a kind 2/3 takes a larger body if one is free. */
void GAMEAPI bomblet_spawn(const int32_t *pos, const int32_t *vel, uint32_t kind, uint32_t owner)
{
    uint8_t *e = g_bomblets + g_bomblet_next * 8;
    uint32_t n = 0, s, w, a, b, k;
    uint8_t type;
    uint8_t *obj;

    do {
        if (!U8(e, 4)) {
            switch (kind & 0xff) {
            case 2:
            case 3:
                for (s = 0; s < 4; s++)
                    if (!g_bomblet_busy[s]) {
                        type = 3;
                        goto found;
                    }
                /* fall through */
            case 1:
                for (s = 4; s < 0xc; s++)
                    if (!g_bomblet_busy[s]) {
                        type = 2;
                        goto found;
                    }
                /* fall through */
            case 0:
                for (s = 0xc;; s++) {
                    if (s >= 0x1c)
                        return;
                    if (!g_bomblet_busy[s])
                        break;
                }
                type = 1;
                goto found;
            default:
                return;
            }
        found:
            obj = g_bomblet_object(s);
            U8(e, 4) = 0x40;
            U32(obj, CALLBACK) = 0x46c1a0;
            U16(obj, 0x160) = 0;
            g_bomblet_busy[s] = 1;
            U8(e, 5) = (uint8_t)s;
            SET_PTR(e, 0, obj);
            U8(e, 6) = type;
            I32(obj, 0x34) = pos[0];
            I32(obj, 0x38) = pos[1];
            I32(obj, 0x3c) = pos[2];
            I32(obj, 0x40) = (int32_t)((uint32_t)vel[0] << 8);
            I32(obj, 0x44) = (int32_t)((uint32_t)vel[1] << 8);
            I32(obj, 0x48) = (int32_t)((uint32_t)vel[2] << 8);
            SET_PTR(obj, 0x164, e);
            k = U8(e, 6) == 1 ? 0x1e : U8(e, 6) == 2 ? 0x1e : 0x19;
            w = U16(PTR(obj, 0x10), 0x32);
            a = (w * k / 100) << 8;
            k = U8(e, 6) == 1 ? 0x3c : U8(e, 6) == 2 ? 0x28 : 0x1e;
            b = (w * k / 100) << 8;
            g_bomblet_shape[0] = 0;
            g_bomblet_shape[1] = (int32_t)a;
            g_bomblet_shape[2] = 0;
            g_bomblet_shape[3] = (int32_t)b;
            g_bomblet_shape[4] = 1;
            g_bomblet_shape[5] = (int32_t)a;
            g_bomblet_shape[6] = 0;
            g_bomblet_shape[7] = -(int32_t)a;
            g_bomblet_shape[8] = (int32_t)b;
            g_bomblet_shape[9] = 1;
            g_bomblet_shape[10] = -(int32_t)a;
            g_bomblet_shape[11] = 0;
            g_bomblet_shape[12] = -(int32_t)a;
            g_bomblet_shape[13] = (int32_t)b;
            g_bomblet_shape[14] = 1;
            body_shape(obj, 3, (const uint8_t *)g_bomblet_shape, (int32_t)((owner & 0xffff) + 1));
            U8(e, 7) = (uint8_t)owner;
            grid_link(obj);
            n = 0x1c;
        }
        if (++g_bomblet_next >= 0x1c)
            g_bomblet_next = 0;
        n++;
    } while (n < 0x1c);
}

/* 0x46bfa0 */
void GAMEAPI bomblet_update(void)
{
    uint32_t i;

    g_bomblet_live = 0;
    for (i = 0; i < 0x24; i++) {
        uint8_t *e = g_bomblets + i * 8, *obj;
        uint32_t limit, f;

        if (!U8(e, 4))
            continue;
        limit = U8(e, 6) == 1 ? 700 : U8(e, 6) == 2 ? 1000 : 1200;
        g_bomblet_live++;
        obj = PTR(e, 0);
        f = U32(obj, 0);
        if (U8(e, 4) < 0x3e)
            f = (f & 0xfffffe07) | 7;
        else
            f = (f & 0xfffffe15) | 0x15;
        U32(obj, 0) = f;
        if (U8(e, 4) && !(g_race_ticks & 0x3f))
            U8(e, 4)--;
        obj = PTR(e, 0);
        if (U16(obj, 0x160) < limit && U8(e, 4)) {
            U16(obj, 0x160) = 0;
            continue;
        }
        body_remove(obj);
        grid_unlink(PTR(e, 0));
        g_bomblet_busy[U8(e, 5)] = 0;
        U8(e, 4) = 0;
        sound_play(0x11, 7, 900, 13000, A(PTR(e, 0) + 0x34));
        obj = PTR(e, 0);
        if (U8(e, 6) == 1)
            effect_burst(VEC(obj, 0x34), VEC(obj, 0x68), 2, 0x24, 0);
        else
            effect_burst(VEC(obj, 0x34), VEC(obj, 0x68), U8(e, 6) * 2, 0x26, 0);
        if (U8(e, 6) > 1) {
            int32_t vel[3], a[3], b[3];

            obj = PTR(e, 0);
            vel[0] = I32(obj, 0x40) >> 10;
            vel[1] = I32(obj, 0x44) >> 10;
            vel[2] = I32(obj, 0x48) >> 10;
            a[0] = I32(obj, 0x34) + 0x300000;
            a[1] = I32(obj, 0x38);
            a[2] = I32(obj, 0x3c) - 0x300000;
            b[0] = I32(obj, 0x34) - 0x300000;
            b[1] = I32(obj, 0x38);
            b[2] = I32(obj, 0x3c) + 0x300000;
            bomblet_spawn(a, vel, (uint8_t)(U8(e, 6) - 2), i);
            bomblet_spawn(b, vel, (uint8_t)(U8(e, 6) - 2), i);
        }
    }
}

/* 0x412bd0: take size bytes from the entry's scratch (+0x90). */
uint32_t GAMEAPI crumble_alloc(uint8_t *entry, uint32_t size)
{
    uint32_t at = U32(entry, 0x90);

    U32(entry, 0x90) = size + at;
    return at;
}

/* 0x4038e0: the collapsing object no longer blocks its two racing lines. */
void GAMEAPI crumble_release_lines(uint8_t *object)
{
    uint8_t *lines = PTR(GP(0x6c1e30), 0x60);

    if (U8(object, 0x1a6) && U8(lines, U8(object, 0x1a6) * 0xc + 3))
        U8(lines, U8(object, 0x1a6) * 0xc + 3)--;
    if (U8(object, 0x1a7) && U8(lines, U8(object, 0x1a7) * 0xc + 3))
        U8(lines, U8(object, 0x1a7) * 0xc + 3)--;
}

/* 0x480bc0: -cos from the quarter-wave table 0x4b277c (angle in 2pi =
 * 0x6487f units), the counterpart of sin_table. */
int32_t GAMEAPI cos_table(int32_t angle)
{
    const int32_t *t = (const int32_t *)0x4b277c;
    int32_t a = abs32((int32_t)((uint32_t)angle + 0x19220));

    if (a > 0x6487f)
        a -= 0x6487f;
    if (a <= 0x19220)
        return -t[a >> 9];
    if (a <= 0x3243f)
        return t[(0x3243f - a) >> 9];
    if (a <= 0x4b65f)
        return t[(a - 0x3243f) >> 9];
    if (a <= 0x6487f)
        return -t[(0x6487f - a) >> 9];
    return 0;
}

/* 0x412dd0: set the current collapse up over two steps (copy the object's
 * faces and matrix, then its sizes, sinking speed and per-group state);
 * 1 once done. */
int32_t GAMEAPI crumble_setup(void)
{
    uint8_t *e;
    uint32_t i;
    int32_t angle;

    switch (g_crumble_setup_step) {
    case 0: {
        const uint32_t *from;
        uint32_t *to;

        e = g_crumble_entry;
        U32(e, 0x90) = U32(e, 0x8c);
        from = ((const uint32_t *)(uintptr_t)*(uint32_t *)(g_crumble_object + 0x24));
        U32(g_crumble_entry, 0x98) =
            crumble_alloc(g_crumble_entry, (uint32_t)U16(g_crumble_model, 4) << 3);
        to = ((uint32_t *)(uintptr_t)*(uint32_t *)(g_crumble_entry + 0x98));
        for (i = 0; i < (uint32_t)U16(g_crumble_model, 4) << 1; i++) {
            to[i] = from[i];
            if (i & 1)
                to[i] = from[i] & 0xffff;
        }
        U32(g_crumble_object, 0x24) = U32(g_crumble_entry, 0x98);
        for (i = 0; i < 8; i++)
            U32(g_crumble_entry, 8 + i * 4) = U32(g_crumble_object, 0xc4 + i * 4);
        g_crumble_setup_step++;
        return 0;
    }
    case 1:
        *(uint32_t *)0x6cf168 = (uint32_t)U16(PTR(g_crumble_entry, 4), 0x190) * 0x10 + *(uint32_t *)0x6bfb14; /* g_crumble_shape */
        U32(g_crumble_entry, 0x9c) = (uint32_t)U16(g_crumble_model, 0x34) + A(g_crumble_model);
        U16(g_crumble_entry, 0) = U16(PTR(g_crumble_entry, 4), 0x192);
        U16(g_crumble_entry, 0x30) = U8(g_crumble_model, 0x27);
        U16(g_crumble_entry, 0x32) = U16(g_crumble_model, 6);
        U32(g_crumble_entry, 0x94) = U32(g_crumble_object, 0x38);
        U16(g_crumble_entry, 0x5a) = U16(g_crumble_shape, 0xe);
        U16(g_crumble_entry, 0x58) = 0;
        U32(g_crumble_entry, 0x28) = 0;
        U32(g_crumble_entry, 0xa0) = crumble_alloc(g_crumble_entry, ((uint32_t)U16(g_crumble_model, 6) + 3) & ~3u);
        U32(g_crumble_entry, 0xa4) =
            crumble_alloc(g_crumble_entry, ((uint32_t)U16(g_crumble_model, 4) * 6 + 3) & ~3u);
        U16(g_crumble_entry, 0x50) = 0;
        U16(g_crumble_entry, 0x52) = 0;
        U16(g_crumble_entry, 0x54) = 0;
        U32(g_crumble_entry, 0x34) = 0x10000;
        U32(g_crumble_entry, 0x40) = U16(g_crumble_shape, 0xa);
        U32(g_crumble_entry, 0x44) = U16(g_crumble_shape, 8);
        U32(g_crumble_entry, 0x3c) = U16(g_crumble_shape, 2);
        U32(g_crumble_entry, 0x38) = U16(g_crumble_shape, 0);
        angle = (int32_t)((uint32_t)I32(g_crumble_entry, 0x3c) << 12) / 0x168;
        angle = (int32_t)((uint32_t)angle * 0x6488) >> 8;
        S16(g_crumble_entry, 0x4c) = (int16_t)((uint32_t)(sin_table(angle) * I32(g_crumble_entry, 0x44)) >> 16);
        S16(g_crumble_entry, 0x4a) =
            (int16_t)((uint32_t)(I32(g_crumble_entry, 0x44) * I32(g_crumble_entry, 0x38)) >> 16);
        angle = (int32_t)((uint32_t)I32(g_crumble_entry, 0x3c) << 12) / 0x168;
        angle = (int32_t)((uint32_t)angle * 0x6488) >> 8;
        S16(g_crumble_entry, 0x48) = (int16_t)((uint32_t)(cos_table(angle) * I32(g_crumble_entry, 0x44)) >> 16);
        for (i = 0; i < (uint32_t)(int32_t)S16(g_crumble_entry, 0x30); i++) {
            uint8_t *obj = PTR(g_crumble_entry, 4);

            U8(g_crumble_entry, 0x5c + i) =
                (uint8_t)((*(uint32_t *)(PTR(obj, 0x194) + *(uint32_t *)0x4b7e54 * 4) >> 24) & 0xf);
            obj = PTR(g_crumble_entry, 4);
            U8(g_crumble_entry, 0x6c + i) =
                U8(GP(0x6bfb10), ((uint32_t)U16(obj, 0x18e) + i) * 8 + 3);
            U8(g_crumble_entry, 0x7c + i) = 0;
        }
        e = g_crumble_entry;
        for (i = 0; i < U8(e, 0x32); i++) {
            U8(PTR(e, 0xa0), i) = 0;
            e = g_crumble_entry;
        }
        g_crumble_setup_step = 0xff;
        U16(e, 2) = 3;
        return 0;
    case 0xff:
        return 1;
    default:
        return 0;
    }
}

/* The GTE rotation of an int16 vector given as its two dwords. */
static void rotate_words(uint32_t xy, uint32_t z)
{
    g_gte_vector_words[0] = xy;
    g_gte_vector_words[1] = z;
    gte_rotate();
}

static void rotate_short(int16_t x, int16_t y, int16_t z)
{
    g_gte_vector[0] = x;
    g_gte_vector[1] = y;
    g_gte_vector[2] = z;
    gte_rotate();
}

/* 0x405f70 */
void GAMEAPI crumble_update(void)
{
    static const int32_t debris_vel[3] = {0, (int32_t)0xffe00000, 0};
    int32_t origin[3];      /* the current face group's anchor, in the world */
    int32_t vert[3] = {0};  /* the last vertex placed; the original keeps it across entries */
    int16_t m[9];
    uint32_t i;

    crumble_setup();
    for (i = 0; i < 4; i++) {
#define E(off) (g_crumbles + i * 0xa8 + (off))
        uint8_t *groups, *obj;

        if (!*(uint16_t *)E(2) && g_crumble_queue_count) {
            uint8_t q = g_crumble_queue_head;

            *(uint32_t *)E(4) = CRUMBLE_QUEUE_OBJECT(q);
            *(uint16_t *)E(2) = 1;
            *(uint16_t *)E(0x2c) = g_crumble_queue_head;
            *(uint16_t *)E(0x2e) = (uint16_t)CRUMBLE_QUEUE_STATE(g_crumble_queue_head);
            q = g_crumble_queue_head;
            CRUMBLE_QUEUE_STATE(q) = 0xfe;
            g_crumble_queue_count--;
            q++;
            g_crumble_queue_head = q;
            if (q >= 0x40)
                g_crumble_queue_head = 0;
        }
        groups = GP(E(0x9c));
        if (g_crumble_setup_step == 0xff && *(uint16_t *)E(2) == 1) {
            uint8_t *e = E(0);

            G_SET(0x6cf16c, e); /* g_crumble_entry */
            G_SET(0x6cf164, PTR(e, 4)); /* g_crumble_object */
            g_crumble_setup_step = 0;
            G_SET(0x6cf160, PTR(PTR(e, 4), 0x10)); /* g_crumble_model */
            U16(e, 2) = 2;
            U8(g_crumble_object, 0x19a) = 5;
            crumble_release_lines(g_crumble_object);
        }
        if (*(int16_t *)E(0x30) && *(uint16_t *)E(2) >= 3 && *(uint16_t *)E(2) == 4) {
            uint8_t group = 0, *verts, *face, *model;
            uint32_t c, f;
            int32_t j;

            obj = GP(E(4));
            if (!*(uint32_t *)E(0x28))
                crumble_start(obj);
            c = ++*(uint32_t *)E(0x28);
            if (!(c & 7))
                sound_play((int32_t)(((c & 0xffffff00) | ((c & 8) ? 0xff : 0)) + 0x10), 2, 0x400, 12000,
                           A(GP(E(4)) + 0x34));
            c = *(uint32_t *)E(0x34);
            if ((int32_t)(c & 0xffff0000) < 0x200000)
                *(uint32_t *)E(0x34) = *(uint32_t *)E(0x40) + c;
            *(int16_t *)E(0x50) += *(int16_t *)E(0x48);
            *(int16_t *)E(0x52) += *(int16_t *)E(0x4a);
            *(int16_t *)E(0x54) += *(int16_t *)E(0x4c);
            if (*E(0x5c + i) != 3) {
                int col;

                matrix_from_angles((const int16_t *)E(0x50), m);
                gte_load_matrix(E(8));
                for (col = 0; col < 3; col++) {
                    g_gte_short[0] = m[col];
                    g_gte_short[2] = m[3 + col];
                    g_gte_short[4] = m[6 + col];
                    gte_rotate_short();
                    S16(obj, MATRIX + col * 2) = g_gte_short[0];
                    S16(obj, MATRIX + 6 + col * 2) = g_gte_short[2];
                    S16(obj, MATRIX + 12 + col * 2) = g_gte_short[4];
                }
            }
            grid_unlink(obj);
            I32(obj, 0x38) += *(int32_t *)E(0x34);
            grid_link(obj);
            gte_load_matrix(obj + MATRIX);
            rotate_words(U32(groups, 0), U32(groups, 4));
            origin[0] = (int32_t)((uint32_t)g_gte_result[0] << 16) + I32(obj, 0x34);
            origin[1] = (int32_t)((uint32_t)g_gte_result[1] << 16) + I32(obj, 0x38);
            origin[2] = (int32_t)((uint32_t)g_gte_result[2] << 16) + I32(obj, 0x3c);
            for (j = 0; j < *(int16_t *)E(0x30); j++) {
                if (*E(0x5c + i) == 2) {
                    effect_burst(origin, VEC(obj, 0x68), 5, 0x2102f, 0xb);
                    U16(obj, 8) |= (uint16_t)(1u << (i & 31));
                    *E(0x5c + i) = 5;
                }
            }
            verts = PTR(obj, 0x24);
            model = PTR(obj, 0x10);
            face = PTR(obj, 0x28);
            f = 0;
            if (U16(model, 6)) {
                uint8_t *g = groups;

                do {
                    uint32_t corners = U32(face, 0), hits = 0, k;
                    uint8_t gi, third;

                    if ((int32_t)f > (int32_t)U8(g, 9)) {
                        group++;
                        g = groups + group * 20;
                        rotate_short((int16_t)U32(g, 0), (int16_t)(U32(g, 0) >> 16), (int16_t)U32(g, 4));
                        origin[0] = (int32_t)((uint32_t)g_gte_result[0] << 16) + I32(obj, 0x34);
                        origin[1] = (int32_t)((uint32_t)g_gte_result[1] << 16) + I32(obj, 0x38);
                        origin[2] = (int32_t)((uint32_t)g_gte_result[2] << 16) + I32(obj, 0x3c);
                    }
                    gi = group;
                    third = (uint8_t)(((uint32_t)U8(g, 9) - U8(g, 8)) >> 2);
                    if (!U8(GP(E(0xa0)), f)) {
                        for (k = 0; k < 4; k++) {
                            uint8_t corner = (uint8_t)corners;
                            uint8_t *v;

                            corners >>= 8;
                            if (!corner)
                                continue;
                            v = verts + (uint8_t)(corner - 1) * 8;
                            if (U16(v, 6) == 1) {
                                hits++;
                                continue;
                            }
                            rotate_words(U32(v, 0), U32(v, 4));
                            vert[0] = (int32_t)((uint32_t)g_gte_result[0] << 16) + I32(obj, 0x34);
                            vert[1] = (int32_t)((uint32_t)g_gte_result[1] << 16) + I32(obj, 0x38);
                            vert[2] = (int32_t)((uint32_t)g_gte_result[2] << 16) + I32(obj, 0x3c);
                            if (vert[1] < *(int32_t *)E(0x94))
                                continue;
                            U16(v, 6) = 1;
                            hits++;
                            if ((f & 3) > 2) {
                                int32_t p[3], d[3];

                                p[0] = vert[0];
                                p[1] = vert[1] + (int32_t)0xffb80000;
                                p[2] = vert[2];
                                d[0] = debris_vel[0];
                                d[1] = debris_vel[1];
                                d[2] = debris_vel[2];
                                if (k == 0) {
                                    p[1] += 0xa0000;
                                    dust_add(p);
                                    effect_burst(p, d, 1, 0x20410, 0);
                                } else {
                                    effect_burst(p, d, 1, 0x20010, 0);
                                }
                            }
                        }
                        if (hits > 2) {
                            *E(gi + 0x7c) += 1;
                            if (*E(gi + 0x6c)) {
                                int32_t v[3];
                                uint8_t sk;

                                gte_load_matrix(obj + MATRIX);
                                rotate_short(0, (int16_t)0xfc00, 0);
                                v[0] = (int32_t)((uint32_t)g_gte_result[0] << 2);
                                v[1] = g_gte_result[1];
                                v[2] = (int32_t)((uint32_t)g_gte_result[2] << 2);
                                sk = *E(gi + 0x6c);
                                if (!(sk & 0x80))
                                    bomblet_spawn(origin, v, (uint8_t)(sk - 1), *(uint16_t *)E(0));
                                else if (U8(g_settings, 0x2e))
                                    bomblet_spawn(origin, v, (uint8_t)((sk & 0x7f) - 1), *(uint16_t *)E(0));
                                *E(gi + 0x6c) = 0;
                            }
                            if (*E(gi + 0x7c) == third) {
                                effect_burst(origin, VEC(obj, 0x40), 0, 0x40001, hits > 3 ? 0xd : 0xb);
                                effect_burst(origin, VEC(obj, 0x40), 3, 0x800, 0);
                            }
                            if (!(f & 3))
                                effect_burst(vert, VEC(obj, 0x40), 1, 0x24, 0);
                            U8(GP(E(0xa0)), f) = 5;
                            *(int16_t *)E(0x32) -= 1;
                        }
                    }
                    f++;
                    face += 8;
                } while ((int32_t)f < (int32_t)U16(PTR(obj, 0x10), 6));
            }
            if (!*(int16_t *)E(0x32)) {
                grid_unlink(obj);
                CRUMBLE_QUEUE_STATE(*(uint16_t *)E(0x2c)) = 0;
                *(uint16_t *)E(2) = 0;
            }
        }
        if (*(uint16_t *)E(2) == 3) {
            uint16_t next = *(uint16_t *)E(0x2e);

            if (next == 0xff || CRUMBLE_QUEUE_STATE(next) == 0xff) {
                *(uint16_t *)E(2) = 4;
                CRUMBLE_QUEUE_STATE(*(uint16_t *)E(0x2c)) = 0xff;
            }
        }
#undef E
    }
}

/* 0x41b2d0: the HUD's mode, when it has one. */
void GAMEAPI hud_set_mode(int32_t mode)
{
    if (*(int32_t *)0x6ce704) {
        *(int32_t *)0x6ce700 = 0;
        *(int32_t *)0x6ce704 = mode;
    }
}

/* 0x46a9a0: one beep of the count-down. */
void GAMEAPI countdown_tick(void)
{
    if (*(int32_t *)0x4d3da8 > 0) {
        *(int32_t *)0x4d3dac = 0;
        (*(int32_t *)0x4d3da8)--;
    }
}

/* 0x479690: slide message i left 0x20; 1 once off screen, 2 while it
 * crosses the middle. */
int32_t GAMEAPI text_slide(uint32_t i)
{
    uint8_t *t = g_texts + i * 0x50;
    uint32_t half = U16(t, 0xa) >> 1, width = U16(t, 0xa);
    int16_t x = S16(t, 4);
    int32_t right = (int32_t)x + (int32_t)half > (int32_t)(U16(g_race, 0xa82) >> 1);
    int16_t nx = (int16_t)(x - 0x20);

    S16(t, 4) = nx;
    if ((int32_t)width + nx < 0)
        return 1;
    if (right && (int32_t)nx + (int32_t)half <= (int32_t)(U16(g_race, 0xa82) >> 1))
        return 2;
    return 0;
}

/* 0x479280 */
void GAMEAPI text_free(int32_t i)
{
    if (i != -1)
        U8(g_texts, (uint32_t)i * 0x50 + 8) = 0;
}

/* 0x479060: put up a message (menu list if menu == 1); its index or -1. */
int32_t GAMEAPI text_create(const uint8_t *s, uint32_t owner, int32_t x, int32_t y, uint32_t flags, int32_t id,
                            int32_t menu)
{
    uint8_t *t, *race;
    uint32_t count, n = 0;
    int32_t i, width = 0, scale;

    if (menu == 1)
        t = g_menu_texts, count = 8;
    else
        t = g_texts, count = 0x1c;
    for (i = 0; i < (int32_t)count && U8(t, 8); i++)
        t += 0x50;
    if (i >= (int32_t)count)
        return -1;
    if (flags & 0x200)
        U16(t, 0xe) = 0x100;
    else
        U16(t, 0xe) = U16(g_font, 4);
    scale = g_screen_scale;
    do {
        uint8_t c = s[n];

        U8(t, 0x10 + n) = c;
        if (c == ' ') {
            width += I32(g_font, 0x10) * scale >> 8;
        } else {
            uint8_t glyph = U8(g_font, 0x101c + c);

            if (glyph != 0xff) {
                width += ((int32_t)((uint32_t)U16(g_font, ((uint32_t)glyph + 2) * 0x10) * S16(t, 0xe)) >> 8) *
                             g_screen_scale >> 8;
                width += I32(g_font, 0x14);
                scale = g_screen_scale;
            }
        }
        n++;
    } while (s[n] && (int32_t)n < 0x40);
    U8(t, 8) = (uint8_t)n;
    U16(t, 0xc) = (flags & 0x100) ? (uint16_t)id : 0xffff;
    race = g_race;
    S16(t, 4) = (int16_t)x;
    S16(t, 6) = (int16_t)y;
    if (flags & 4) {
        if (!menu) {
            U16(t, 4) += U16(race, 0xa82) >> 1;
            U16(t, 6) += U16(race, 0xa80) >> 1;
        } else {
            U16(t, 4) += (uint16_t)(*(uint32_t *)0x6b8d6c >> 1);
            U16(t, 6) += (uint16_t)(*(uint32_t *)0x6b8d70 >> 1);
        }
    }
    if (flags & 0x40) {
        U16(t, 4) += U16(race, 0xa82) >> 1;
        U16(t, 6) += U16(race, 0xa80);
    }
    if (flags & 8)
        S16(t, 4) = (int16_t)(((int32_t)(U16(race, 0xa82) * 3) >> 1) - (width >> 1));
    if (flags & 2)
        S16(t, 4) -= (int16_t)(width >> 1);
    else if (flags & 0x10)
        S16(t, 4) -= (int16_t)width;
    U8(t, 9) = (uint8_t)flags;
    U32(t, 0) = owner;
    S16(t, 0xa) = (int16_t)width;
    if (menu == 1) {
        ((int16_t *)0x6b9828)[i] = (int16_t)id;
        (*(int32_t *)0x6b9824)++;
    }
    return i;
}

#define MESSAGE(i) ((const uint8_t *)GP(0x4d1098 + (i) * 4))

/* 0x464e10: each finisher's messages: place, then a two-line verdict,
 * slid on and off. */
void GAMEAPI finish_messages(void)
{
    uint32_t n = U8(g_race, 0x54d);
    uint8_t *slot = g_car_slots;

    if ((int32_t)n <= 0)
        return;
    do {
        uint8_t *race = g_race, *s = slot + 0x25b;
        uint8_t place = U8(slot, 0x2d);
        int32_t r1, r2;

        switch (s[-1]) {
        case 1:
            switch (U8(race, 0x551)) {
            case 1:
            case 3:
            case 4:
                break;
            case 10:
                s[-1] = 10;
                break;
            default:
                s[-1] = 2;
                break;
            }
            break;
        case 2:
            s[1] = (uint8_t)text_create(place >= 2 && place <= 6 ? MESSAGE(place - 1) : MESSAGE(0), A(slot),
                                        (int32_t)((uint32_t)g_screen_scale << 5) >> 8, 0, 0xc, 0, 0);
            s[-1] = 3;
            s[0] = 0;
            break;
        case 3:
            if (s[0] && s[0] <= 0x32) {
                s[0]++;
                break;
            }
            r1 = text_slide(s[1]);
            if (r1 == 2) {
                s[0]++;
            } else if (r1 == 1) {
                text_free(s[1]);
                s[-1] = 4;
            }
            break;
        case 4: {
            const uint8_t *top = MESSAGE(6), *bottom = MESSAGE(7);

            if (place >= 2 && place <= 6) {
                top = MESSAGE(6 + 2 * (place - 1));
                bottom = MESSAGE(7 + 2 * (place - 1));
            }
            s[1] = (uint8_t)text_create(top, A(slot), 0,
                                        (int32_t)((uint32_t)g_screen_scale * (uint32_t)-10) >> 8, 0xc, 0, 0);
            if (U8(g_race, 0x551))
                bottom = (const uint8_t *)GP(0x4d1144);
            s[2] = (uint8_t)text_create(bottom, A(slot), 0, (int32_t)((uint32_t)g_screen_scale * 10) >> 8,
                                        0xc, 0, 0);
            s[-1] = 5;
            s[0] = 0;
            break;
        }
        case 5:
            if (s[0] && s[0] <= 0x96) {
                s[0]++;
                break;
            }
            r1 = text_slide(s[1]);
            r2 = text_slide(s[2]);
            if (r1 == 2) {
                s[0]++;
            } else if (r1 == 1 && r2 == r1) {
                text_free(s[1]);
                text_free(s[2]);
                s[0] = 0;
                s[-1] = 0xc;
            }
            break;
        case 6:
            if (++s[0] != 0x1e)
                break;
            if (U8(race, 0x552) == 2) {
                text_create((const uint8_t *)GP(0x4d10ec), A(slot), 0,
                            (int32_t)((uint32_t)g_screen_scale * (uint32_t)-62) >> 8, 0x43, 0, 0);
                text_create((const uint8_t *)GP(0x4d10f0), A(slot), 0,
                            (int32_t)((uint32_t)g_screen_scale * (uint32_t)-40) >> 8, 0x43, 0, 0);
            } else {
                text_create((const uint8_t *)GP(0x4d10e8), A(slot), 0,
                            (int32_t)((uint32_t)g_screen_scale * (uint32_t)-40) >> 8, 0x43, 0, 0);
            }
            s[0] = 0;
            s[-1] = 7;
            break;
        case 0xc:
            if (I32(race, 0x57c) == 6)
                s[-1] = 6;
            break;
        }
        slot += CAR_SLOT_SIZE;
    } while (--n);
}

/* 0x4651f0: the "go" banner's two lines. */
void GAMEAPI race_go_banner(void)
{
    uint32_t h = U16(g_race, 0xa80), w = U16(g_race, 0xa82);
    uint32_t y = (h >> 1) - 0x3c, x = w - 0x14;

    text_create((const uint8_t *)GP(0x4d112c), 0, (int32_t)((uint32_t)g_screen_scale * x >> 8),
                (int32_t)((uint32_t)g_screen_scale * y >> 8), 0x31, 0, 0);
    text_create((const uint8_t *)GP(0x4d1130), 0, (int32_t)((uint32_t)g_screen_scale * x >> 8),
                (int32_t)((y + 0x16) * (uint32_t)g_screen_scale >> 8), 0x31, 0, 0);
}

/* 0x41c230: every player's buttons, newly pressed in the low half. */
uint32_t GAMEAPI players_buttons(void)
{
    uint32_t buttons = 0;
    uint32_t i;
    uint8_t *slot = g_car_slots;

    if (!U8(g_race, 0x54d))
        return 0;
    for (i = 0; (int32_t)i < (int32_t)U8(g_race, 0x54d); i++) {
        uint8_t *car = PTR(slot, 0), *p;
        uint8_t scratch[8];

        read_controls((int32_t)i, scratch, (void *)(car + 0x71c));
        p = PTR(car, 0x71c);
        slot += CAR_SLOT_SIZE;
        buttons |= (~((uint32_t)p[2] * 0x100 + p[3]) & 0xffff) | (uint32_t)U16(p, 4) << 16;
    }
    return buttons;
}

/* 0x45f920: in a network game, open the quit menu (0 if it could not). */
int32_t GAMEAPI network_quit_menu(void)
{
    if (!(*(uint8_t *)0x5d622c & 1) || !(*(uint8_t *)0x5d622c & 2))
        return 0;
    if (*(uint8_t *)0x5d622c & 0x20) {
        menu_open(2, 0x17, 1);
        *(uint8_t *)0x5d622c &= 0xfd;
        return 1;
    }
    return menu_open(1, 0x16, 1);
}

/* 0x4639b0: open the pause menu when a player (or, in a network game, the
 * other end) asks for it. */
void GAMEAPI pause_check(void)
{
    uint32_t who = (uint32_t)local_player(), i, buttons;
    uint8_t *slot = g_car_slots;

    if (I32(g_race, 0x57c) >= 6 || U8(g_race, 0x58c))
        return;
    if (network_game() && network_host()) {
        if (U32(PTR(g_car_slots + who * CAR_SLOT_SIZE, 0), 0x704) & 0x800)
            pause_send((int32_t)who);
        if (pause_receive(&who) != 1)
            return;
        buttons = 0;
        slot = g_car_slots + who * CAR_SLOT_SIZE;
        i = who;
    } else {
        uint32_t n = U8(g_race, 0x54d);

        if (!n)
            return;
        for (i = 0;; i++, slot += CAR_SLOT_SIZE) {
            if (i >= n)
                return;
            if (U8(PTR(slot, 0), 0x70c) == 10 && (U32(PTR(slot, 0), 0x704) & 0x800))
                break;
        }
        buttons = U32(PTR(slot, 0), 0x700);
    }
    pause_menu((int32_t)i, 0, 0xe);
    *(uint32_t *)0x4d1198 = 1;
    {
        uint32_t layout = (uint32_t)pause_layout();

        *(uint32_t *)0x6b8d74 = buttons;
        *(uint32_t *)0x6b8d6c = layout & 0xffff;
        *(uint32_t *)0x6b8d70 = layout >> 16;
        *(uint32_t *)0x6b8d7c = buttons;
        *(uint32_t *)0x6b8d78 = buttons;
    }
    pause_panel((void *)slot);
    pause_bar(*(int8_t *)0x6b983a, 2, (int32_t)((uint32_t)*(int32_t *)0x6b8d84 * 0xc0) >> 14);
    pause_bar(*(int8_t *)0x6b983b, 3, (int32_t)((uint32_t)*(int32_t *)0x6b8d88 * 0xc0) >> 14);
    pause_items();
    sound_pause(1);
    pause_sounds();
    for (i = 0; i < U8(g_race, 0x54d); i++)
        pause_player(*(int16_t *)(uintptr_t)(0x6bfdec + i * CAR_SLOT_SIZE));
}

/* 0x46a530 */
int32_t GAMEAPI race_progress(void)
{
    uint8_t *race = g_race;
    int32_t state = I32(race, 0x57c), t = I32(race, 0x580);

    switch (state) {
    case 1:
        if (!t && U8(race, 0x551) == 4)
            race_go_banner();
        if (++t >= 0xf) {
            state = 2;
            t = 0;
            hud_set_mode(2);
        }
        break;
    case 2:
        if (++t >= 0x96) {
            t = 0x3f;
            state = 3;
            sound_play(0x29, 6, 0x514, 16000, 0);
            hud_set_mode(4);
        }
        break;
    case 3:
        if (t > 0 && t < 0x20 && t % 10 == 0)
            countdown_tick();
        if (--t == 0) {
            uint8_t *slot = g_car_slots;
            int32_t clock = (int32_t)race_clock(), k;

            for (k = 0; k < (int32_t)U8(g_race, 0x54d); k++, slot += CAR_SLOT_SIZE) {
                I32(PTR(slot, 0), 0x640) = I32(PTR(slot, 0), 0x6a0);
                I32(slot, 0x60) = clock;
                lap_split(slot);
            }
            t = 0;
            state = 4;
            sound_play(0x2a, 6, 0x514, 16000, 0);
            sound_pause(0);
            hud_set_mode(5);
        }
        break;
    case 4:
        if (t <= 200) {
            uint8_t *slot = g_car_slots;
            int32_t n = U8(race, 0x54d);

            for (; n > 0; n--, slot += CAR_SLOT_SIZE) {
                uint8_t *car = PTR(slot, 0);
                int32_t v = I32(car, 0x6a0);

                if (v > 0) {
                    v += (int32_t)((uint32_t)U8(car, 0x63d) << 8) > v ? -0x200 : -0x5dc;
                    I32(car, 0x6a0) = v;
                    if (v < 0)
                        I32(car, 0x6a0) = -1;
                } else if (v == 0 && t < 7 && U8(car, 0x63d)) {
                    slot_boost(slot, 0, t + 1);
                }
            }
        }
        t++;
        break;
    case 5:
        t++;
        finish_messages();
        break;
    case 6:
        t++;
        if ((players_buttons() & 0x800) && !network_quit_menu()) {
            state = 7;
            t = 0;
        }
        finish_messages();
        break;
    case 7:
    case 8:
    case 9:
    case 10:
    case 11: {
        int32_t a, b, f;

        finish_messages();
        race = g_race;
        if (!t && U8(race, 0x54f) == 1) {
            U8(race, 0x54f) = 2;
            race = g_race;
        }
        if (++t > 0x34) {
            if (!U8(race, 0x58c)) {
                if (!network_game() || !network_host())
                    race_end(0);
                race = g_race;
            }
            if (U8(race, 0x54f) == 2)
                U8(race, 0x54f) = 1;
            return 1;
        }
        a = S16(race, 0xa7c) / 50;
        b = S16(race, 0xa7e) / 50;
        f = 0x32 - t;
        if (f < 0)
            f = 0;
        sound_master(a * f);
        sound_balance(f * b, f * b);
        break;
    }
    }
    I32(g_race, 0x57c) = state;
    I32(g_race, 0x580) = t;
    race = g_race;
    if (!U8(race, 0x58c)) {
        pause_check();
        race = g_race;
    } else if (S16(race, 0x58e) > 0) {
        uint32_t mask;

        if (U8(race, 0x58c) != 2) {
            S16(race, 0x58e) = (int16_t)(S16(race, 0x58e) - 1);
            race = g_race;
        }
        mask = U8(race, 0x58c) == 2 ? 0x800 : 0xffff;
        if (mask & players_buttons())
            S16(g_race, 0x58e) = 0;
        race = g_race;
        if (!S16(race, 0x58e)) {
            I32(race, 0x57c) = 7;
            I32(g_race, 0x580) = 0;
            race = g_race;
        }
    }
    if (U8(race, 0x551) == 3 || U8(race, 0x551) == 8) {
        uint32_t k;

        for (k = 0; k < U8(race, 0x54d); k++) {
            if (U8(race, 8 + k) < 0xf)
                continue;
            if (network_game() == 1 && network_host() == 1)
                network_finish();
            race = g_race;
            if (I32(race, 0x57c) != 7) {
                I32(race, 0x57c) = 7;
                I32(g_race, 0x580) = 0;
                race = g_race;
            }
        }
    }
    return 0;
}
