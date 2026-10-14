/* Sound engine, group DB (0x471000-0x473fff): the voice table, the
 * play / stop helpers around sound_play 0x471c90 and the moving-sound player
 * 0x471da0, the CD audio wrappers, sound state resets and the per-frame mixer
 * 0x4722d0.
 *
 * Voices: 64 records of 0x30 bytes at 0x5ff4a0 (layout in db_formats.txt);
 * +0 is the state (0 idle, 0x40 starting, 1 playing, 0x41 stopping). The
 * mixer walks the first *0x4b1c8c of them each frame and pushes volume,
 * pitch and 3D position to the device voice (other group, 0x46f000-0x470fff).
 *
 * Still original (called by address): sound_play 0x471c90 and 0x471da0 (the
 * in-game verifier records them), the EAX property-set wrappers (DirectSound
 * COM calls through vtables: 0x471400, 0x4714c0, 0x4716c0, 0x471740,
 * 0x471780, 0x471810), the x87 ones (0x4714c0, 0x471580, 0x472bb0), the
 * device side (0x46f000-0x470fff), the CD audio player (0x496xxx, MCI), the
 * debug printf 0x496340 and the allocator 0x4445e0. None of the functions
 * here calls QMixer or WinAPI directly. */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define S8(p, off) (*(int8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define PTR(p, off) ((uint8_t *)(uintptr_t)U32(p, off)) /* 32-bit pointers in game memory */
#define SHL(x, n) ((int32_t)((uint32_t)(int32_t)(x) << (n)))
#define G32_AT(a) (*(uint32_t *)(a))

#define g_config GP(0x4b2198)
#define g_race GP(0x4b1960)
#define g_voice_count (*(int32_t *)0x4b1c8c)
#define g_sound_count (*(int32_t *)0x4d4534)
#define g_sound_3d (*(int32_t *)0x4ac984) /* nonzero: voice_alloc sets voices up at once, the mixer is idle */
#define g_eax_open (*(int32_t *)0x4d4524)
#define g_eax_ready (*(int32_t *)0x4d4528)
#define g_eax_sets ((uint8_t *)0x6cb530) /* 64 x 0x3c: +0 float level, +4 property set */
#define g_eax_room (*(int32_t *)0x4b1ca4)
#define g_eax_room_hf (*(int32_t *)0x4b1ca8)
#define g_volume (*(uint16_t *)0x4d45a0)
#define g_cd_state (*(uint16_t *)0x4d45a2)
#define g_flag_play (*(uint32_t *)0x4d459c) /* "flagged" play: voice flag 4 */
#define g_plain_volume (*(uint32_t *)0x4d4640) /* volume not scaled by g_volume */
#define g_mix_frames (*(int32_t *)0x4d45f4)
#define g_level_near (*(uint32_t *)0x4b1eac) /* float */
#define g_level_far (*(uint32_t *)0x4d45f8) /* float */
#define g_debug (*(int32_t *)0x4d4648)
#define g_debug_ticks (*(int32_t *)0x4d464c)
#define g_cd_desc GP(0x5ff384) /* 0x14 bytes */
#define g_cd_desc_at G32_AT(0x5ff384)
#define g_cd_info GP(0x6000d0) /* 0x28 bytes */
#define g_cd_info_at G32_AT(0x6000d0)
#define g_voice_plain ((uint8_t *)0x5ff300) /* per voice: g_plain_volume at start */
#define g_voice_started ((uint8_t *)0x5ff340)

#define VOICES ((uint8_t *)0x5ff4a0)
#define VOICE(i) (VOICES + (i) * 0x30)
#define V_STATE 0x00
#define V_FLAGS 0x01 /* 1 loop, 2 linked parent, 4 flagged, 0x10 linked child, 0x20 nearest listener */
#define V_KIND 0x02
#define V_LINK0 0x03 /* linked voice for listener 1 (in a child: its listener) */
#define V_FRAMES 0x04 /* uint16 */
#define V_NEAR 0x06 /* 1: EAX level from g_level_near */
#define V_SOUND 0x07
#define V_VOL_L 0x08 /* int16 */
#define V_VOL_R 0x0a /* int16 */
#define V_PITCH 0x14
#define V_PITCH2 0x18
#define V_POS 0x24 /* const int32_t[3] * */
#define V_VEL 0x28 /* const int32_t[3] * */
#define V_LINK1 0x2c
#define V_LINK2 0x2d

/* Listener slots 0-3 (0x472290 sets them). */
#define L_POS(l) GP(0x4d4600 + (l) * 4)
#define L_VEL(l) GP(0x4d4610 + (l) * 4)
#define L_MAT ((uint32_t *)0x4d4620)
#define L_D ((uint32_t *)0x4d4630)

/* Still original. */
/* The original pushes arguments these two do not read (ps_wait_none 0x479ae0:
 * xor eax, eax; ret, sound_device_ready 0x428060: returns 1); kept. */
typedef int32_t(GAMEAPI *nothing_fn)(uint32_t a, ...);
typedef uint32_t(GAMEAPI *device_ok_fn)(uint32_t a);
typedef int32_t(GAMEAPI *isqrt_fn)(int32_t x);

#define sound_play GCALL(0x471c90, sound_play_body)
#define sound_moving GCALL(0x471da0, sound_moving_body)
#define eax_get_room GCALL(0x471740, eax_environment_get)
#define eax_get_room_hf GCALL(0x471810, eax_volume_get)
#define eax_set_room GCALL(0x4716c0, eax_environment_set)
#define eax_set_room_hf GCALL(0x471780, eax_volume_set)
#define eax_voice_level GCALL(0x4714c0, eax_voice_reverb_set)
#define eax_voice_fade GCALL(0x471580, eax_voice_fade_step)
#define device_set_level GCALL(0x46f760, snd_set_rolloff)
#define device_voice_free GCALL(0x46fd40, snd_chan_stop)
#define device_voice_pitch GCALL(0x46ffd0, snd_chan_frequency)
#define device_voice_volume GCALL(0x470040, snd_chan_volume)
#define device_voice_pos GCALL(0x4700b0, snd_chan_position)
#define device_voice_dir GCALL(0x470160, snd_chan_velocity)
#define device_voice_play GCALL(0x46fc40, snd_chan_play)
#define device_voice_get GCALL(0x46ffa0, snd_chan_claim)
#define device_frame GCALL(0x470ef0, sound_channels_refresh)
#define device_start GCALL(0x4702a0, snd_distance_map)
#define device_restart GCALL(0x470680, snd_stats_clear)
#define device_debug_on GCALL(0x470740, snd_profile_start)
#define device_debug_off GCALL(0x470790, snd_profile_update)
#define device_debug_clear GCALL(0x470e40, snd_stats_log)
#define voice_3d GCALL(0x472bb0, voice_3d_place)
#define voice_slot GCALL(0x472290, voice_slot_set)
#define sound_mix_thunk GCALL(0x472040, sound_mix_jump)
#define fix_isqrt_orig GCALL(0x40d9d0, fix_isqrt)
#define game_alloc GCALL(0x4445e0, kr_malloc)
#define debug_printf GCALL(0x496340, debug_log_printf)
#define nothing ((nothing_fn)(uintptr_t)0x479ae0) /* xor eax, eax; ret */
#define nothing2 GCALL(0x4291f0, dx_error_ignore) /* ret */
#define device_ok ((device_ok_fn)(uintptr_t)0x428060) /* returns 1 */
#define output_mode GCALL(0x471a70, sound_output_mode)
#define cd_set_volume GCALL(0x496c90, cdaudio_set_volume)
#define cd_play_orig GCALL(0x496770, cdaudio_play)
#define cd_resume GCALL(0x4967f0, cdaudio_pause)
#define cd_stop GCALL(0x4967a0, cdaudio_stop)
#define cd_pause_orig GCALL(0x496890, cdaudio_resume)
#define cd_open GCALL(0x496690, cdaudio_open)
#define cd_set_first GCALL(0x496c50, cdaudio_set_shuffle)
#define cd_set_repeat GCALL(0x496c80, cdaudio_set_skip_first)
#define cd_track_count GCALL(0x497630, cd_volume_get)
#define game_time GCALL(0x43692a, game_time_get)

/* pitch * 11025 / 1024, as the original scales a voice's rate. */
static int32_t rate(int32_t pitch)
{
    return (int32_t)((uint32_t)pitch * 11025u) / 1024;
}

/* The volume after the master volume, unless the voice is played "plain". */
static int32_t scaled(int32_t vol, int plain)
{
    if (plain)
        return SHL(vol, 1);
    return SHL((int32_t)((uint32_t)g_volume * (uint32_t)vol) / 16384, 1);
}

/* 0x471370: reads the EAX room levels into 0x4b1ca4 / 0x4b1ca8, sets the
 * device level 1.0 and room 1. */
int32_t GAMEAPI sound_eax_open(void)
{
    g_eax_room = eax_get_room((uint32_t *)0x4d44d8);
    g_eax_room_hf = eax_get_room_hf((uint32_t *)0x4d44dc);
    device_set_level(0x3f800000);
    eax_set_room(1);
    return 0;
}

/* 0x4713b0: puts the saved EAX room levels back. */
int32_t GAMEAPI sound_eax_close(void)
{
    if (g_eax_room != -1) {
        g_eax_room = -1;
        eax_set_room(G32_AT(0x4d44d8));
    }
    if (g_eax_room_hf != -1) {
        g_eax_room_hf = -1;
        eax_set_room_hf(G32_AT(0x4d44dc));
    }
    return 0;
}

/* 0x471700: the first voice's EAX property set, 0 if none or EAX is off. */
uint32_t GAMEAPI sound_eax_first(void)
{
    if (g_eax_open == 0 || g_eax_ready == 0)
        return 0;
    for (int32_t i = 0; i < 64; i++)
        if (U32(g_eax_sets, i * 0x3c + 4) != 0)
            return U32(g_eax_sets, i * 0x3c + 4);
    return 0;
}

/* 0x471850: the CD track descriptor (allocated at *0x5ff384). */
void GAMEAPI sound_cd_desc_init(uint8_t *p, uint32_t track)
{
    U32(p, 4) = track & 0xffff;
    U32(p, 0) = 7;
    U16(p, 8) = 0;
    U16(p, 0xa) = 0;
    U32(p, 0xc) = 0;
    U32(p, 0x10) = 0;
}

/* 0x4721f0: applies a CD info record's volume (flags 0xc0) to the player. */
void GAMEAPI sound_cd_apply(const uint8_t *p)
{
    if (U8(p, 0) & 0xc0)
        cd_set_volume((uint32_t)SHL(S16(p, 0x10), 2), (uint32_t)SHL(S16(p, 0x12), 2));
}

/* 0x471880: clears the CD info record (*0x6000d0) and applies it. */
void GAMEAPI sound_cd_info_init(uint8_t *p)
{
    U32(p, 0) = 0;
    U16(p, 4) = 0x3fff;
    U16(p, 6) = 0x3fff;
    U16(p, 8) = 0;
    U16(p, 0xa) = 0;
    U16(p, 0x10) = 0;
    U16(p, 0x12) = 0;
    U32(p, 0x14) = 0;
    U32(p, 0x18) = 0;
    U16(p, 0x1c) = 0;
    U16(p, 0x1e) = 0;
    U32(p, 0x20) = 0;
    U32(p, 0x24) = 0;
    sound_cd_apply(p);
}

/* 0x471970: resets the voice table; with a CD track (not 0xff) also sets up
 * the CD descriptor. Returns 0, or 0xf8 if the device is not ready. */
uint8_t GAMEAPI sound_voices_reset(uint32_t track)
{
    nothing2();
    nothing(0x1000);
    nothing(0x7f000);
    nothing(0xa, 0x5ff3e0);
    if ((uint16_t)track != 0xff) {
        if (device_ok((uint32_t)-2) != 1)
            return 0xf8;
        if (device_ok(1) != 1)
            return 0xf8;
        nothing((uint32_t)(int16_t)track);
        g_cd_desc_at = game_alloc(0x14);
        sound_cd_desc_init(g_cd_desc, track);
        nothing(g_cd_desc_at);
        nothing(g_cd_desc_at);
    }
    for (int32_t i = 0; i < 0x300; i++)
        ((uint32_t *)VOICES)[i] = 0;
    for (int32_t i = 0; i < 0x10; i++)
        ((uint32_t *)g_voice_started)[i] = 0;
    for (int32_t i = 0; i < 64; i++) {
        U8(VOICE(i), V_LINK0) = 0x7f;
        U8(VOICE(i), V_LINK1) = 0x7f;
        U8(VOICE(i), V_LINK2) = 0x7f;
    }
    g_cd_info_at = game_alloc(0x28);
    sound_cd_info_init(g_cd_info);
    return 0;
}

/* 0x471a60: sets the master volume and runs the mixer (through its thunk). */
void GAMEAPI sound_volume_mix(uint32_t volume)
{
    g_volume = (uint16_t)volume;
    sound_mix_thunk();
}

/* 0x471aa0: a voice's EAX level choice (byte +6). */
void GAMEAPI voice_set_near(uint32_t voice, uint32_t near)
{
    U8(VOICE(voice & 0xff), V_NEAR) = (uint8_t)near;
}

/* 0x471ac0: three sound settings. */
void GAMEAPI sound_settings_set(uint32_t a, uint32_t b, uint32_t c)
{
    *(uint16_t *)0x4b1e80 = (uint8_t)a;
    *(uint16_t *)0x4d4596 = (uint16_t)b;
    *(uint16_t *)0x4d4594 = (uint16_t)c;
}

/* 0x471af0: the CD volume (left, right). */
void GAMEAPI sound_cd_volume(uint32_t left, uint32_t right)
{
    U32(g_cd_info, 0) = 0xc0;
    U16(g_cd_info, 0x10) = (uint16_t)left;
    U16(g_cd_info, 0x12) = (uint16_t)right;
    sound_cd_apply(g_cd_info);
}

/* 0x471b30: the CD volume, also kept in the race record (+0xa7e). */
void GAMEAPI sound_cd_volume_keep(uint32_t left, uint32_t right)
{
    U32(g_cd_info, 0) = 0xc0;
    U16(g_cd_info, 0x10) = (uint16_t)left;
    U16(g_cd_info, 0x12) = (uint16_t)right;
    sound_cd_apply(g_cd_info);
    U16(g_race, 0xa7e) = (uint16_t)left;
}

/* 0x471b80: the master volume, also kept in the race record (+0xa7c). */
void GAMEAPI sound_volume_set(uint32_t volume)
{
    g_volume = (uint16_t)volume;
    U16(g_race, 0xa7c) = (uint16_t)volume;
}

/* 0x471ba0: the CD channel mapping. */
void GAMEAPI sound_cd_channels(uint32_t a, uint32_t b)
{
    *(uint8_t *)0x6000d4 = 0x7f;
    *(uint8_t *)0x6000d5 = 0x7f;
    *(uint8_t *)0x6000d6 = 0x7f;
    *(uint8_t *)0x6000d7 = 0x7f;
    U32(g_cd_info, 0) = 0x300;
    U32(g_cd_info, 0x14) = b & 0xff;
    U32(g_cd_info, 0x18) = a & 0xff;
    sound_cd_apply(g_cd_info);
}

/* 0x472220: plays a CD track unless the config's CD music is off. */
int32_t GAMEAPI sound_cd_play(uint32_t kind, const uint32_t *track)
{
    (void)kind;
    if (U8(g_config, 0x410) == 0)
        cd_play_orig(track[0]);
    return 0;
}

/* 0x471c00: plays CD track `track` and marks the CD as playing. */
void GAMEAPI sound_cd_track(uint32_t track)
{
    uint32_t desc[2];
    desc[0] = (uint32_t)(int32_t)(int16_t)track;
    desc[1] = 0;
    sound_cd_play(2, desc); /* the original also pushes a 0 */
    g_cd_state = 1;
}

/* 0x472250: CD commands 3 (pause), 8 (stop), 9 (resume). */
int32_t GAMEAPI sound_cd_command(uint32_t command)
{
    if (U8(g_config, 0x410) != 0)
        return 0;
    switch (command & 0xff) {
    case 3:
        cd_pause_orig();
        break;
    case 8:
        cd_stop();
        break;
    case 9:
        cd_resume();
        break;
    }
    return 0;
}

/* 0x471c30: CD pause (0) / resume (1), by the state at 0x4d45a2. */
void GAMEAPI sound_cd_pause(uint32_t resume)
{
    switch (resume & 0xff) {
    case 0:
        if (g_cd_state == 2) {
            sound_cd_command(3); /* the original also pushes two 0s */
            g_cd_state = 1;
        }
        break;
    case 1:
        if (g_cd_state == 1) {
            sound_cd_command(9);
            g_cd_state = 2;
        }
        break;
    }
}

/* 0x471f80: a voice's pitch, clamped to 0..0x3fff. */
void GAMEAPI voice_set_pitch(uint32_t voice, uint32_t pitch)
{
    int16_t p = (int16_t)pitch;
    if (p < 0)
        p = 0;
    else if (p > 0x3fff)
        p = 0x3fff;
    I32(VOICE(voice & 0xff), V_PITCH) = p;
}

/* 0x471fc0: a voice's left / right volume. */
void GAMEAPI voice_set_volume(uint32_t voice, uint32_t left, uint32_t right)
{
    uint8_t *v = VOICE(voice & 0xff);
    U16(v, V_VOL_L) = (uint16_t)left;
    U16(v, V_VOL_R) = (uint16_t)right;
}

/* 0x471eb0: starts a voice record (state 0x40) after sound_play has got
 * the device voice. */
void GAMEAPI voice_start(uint32_t voice, uint32_t kind, uint32_t left, uint32_t right, uint32_t pitch, uint32_t flags)
{
    uint8_t f = (uint8_t)flags;
    uint8_t *v = VOICE(voice & 0xff);
    if (g_flag_play != 0)
        f |= 4;
    U16(v, V_VOL_L) = (uint16_t)left;
    U16(v, V_VOL_R) = (uint16_t)right;
    I32(v, V_PITCH) = (int16_t)pitch;
    U8(v, V_KIND) = (uint8_t)kind;
    I32(v, V_PITCH2) = 0;
    U8(v, V_FLAGS) = f;
    voice_set_near(voice, (f & 4) ? 1 : 0);
    if ((uint8_t)kind == 9)
        U8(v, V_FLAGS) |= 1;
    U8(v, V_STATE) = 0x40;
}

/* 0x471f40: stops a voice (the mixer frees it next frame). */
void GAMEAPI sound_voice_stop(uint32_t voice)
{
    uint8_t *v;
    voice_set_volume(voice, 0, 0);
    voice_set_pitch(voice, 0);
    v = VOICE(voice & 0xff);
    U8(v, V_KIND) = 0;
    U8(v, V_FLAGS) = 0;
    U8(v, V_STATE) = 0x41;
}

/* 0x471d50: sound_play with voice flag 4 when `flagged` is 1. */
uint32_t GAMEAPI sound_play_flagged(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t flagged)
{
    uint8_t r;
    g_flag_play = (uint8_t)flagged == 1;
    r = (uint8_t)sound_play(a1, a2, a3, a4, a5);
    g_flag_play = 0;
    return r;
}

/* 0x471e60: 0x471da0 with voice flag 4 when `flagged` is 1. */
uint32_t GAMEAPI sound_moving_flagged(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                      uint32_t a7, uint32_t flagged)
{
    uint32_t r;
    g_flag_play = (uint8_t)flagged == 1;
    r = sound_moving(a1, a2, a3, a4, a5, a6, a7);
    g_flag_play = 0;
    return r;
}

/* 0x471ff0: distance between two positions, in 0x20000 units, times 2. */
int32_t GAMEAPI sound_distance(const int32_t *a, const int32_t *b)
{
    int32_t dx = (a[0] - b[0]) >> 17;
    int32_t dy = (a[1] - b[1]) >> 17;
    int32_t dz = (a[2] - b[2]) >> 17;
    return SHL(fix_isqrt_orig(dz * dz + dy * dy + dx * dx), 1);
}

/* 0x472050: listener slot `i`: copies its position, velocity and matrix
 * (any may be null) into 0x5ff3a0 / 0x6000a0 / 0x6000e0 and sets the slot.
 * `i` is not range checked (sic). */
void GAMEAPI sound_listener_set(uint32_t i, const uint32_t *pos, const uint32_t *vel, const uint32_t *mat)
{
    uint32_t *const used = (uint32_t *)0x4d45b0, *const pos_at = (uint32_t *)0x4d45c0;
    uint32_t *const vel_at = (uint32_t *)0x4d45d0, *const mat_at = (uint32_t *)0x4d45e0;
    i &= 0xff;
    used[i] = 1;
    if (pos) {
        uint32_t *d = (uint32_t *)0x5ff3a0 + i * 3;
        uint32_t z;
        d[0] = pos[0];
        d[1] = pos[1];
        z = pos[2];
        pos_at[i] = (uint32_t)(uintptr_t)d;
        d[2] = z;
    } else {
        pos_at[i] = 0;
    }
    if (vel) {
        uint32_t *d = (uint32_t *)0x6000a0 + i * 3;
        uint32_t z;
        d[0] = vel[0];
        d[1] = vel[1];
        z = vel[2];
        vel_at[i] = (uint32_t)(uintptr_t)d;
        d[2] = z;
    } else {
        vel_at[i] = 0;
    }
    if (mat) {
        uint32_t *d = (uint32_t *)0x6000e0 + i * 8;
        for (int32_t k = 0; k < 8; k++)
            d[k] = mat[k];
        mat_at[i] = (uint32_t)(uintptr_t)d;
    } else {
        mat_at[i] = 0;
    }
    voice_slot((int32_t)i, pos_at[i], vel_at[i], mat_at[i], 1);
}

/* 0x472920: gets a device voice for sound `sound` (kind 9 = looped) and,
 * with positional sound off, sets its level, pitch, volume and position.
 * Returns the voice, 0x7f for none. */
uint8_t GAMEAPI voice_alloc(uint32_t sound, uint32_t kind, int32_t vol, int32_t pitch, uint32_t near)
{
    int32_t snd = (int8_t)sound;
    uint32_t looped;
    int8_t got;
    int32_t v;
    if ((int8_t)sound < 0 || snd >= g_sound_count)
        return 0x7f;
    looped = (uint8_t)kind == 9;
    got = (int8_t)device_voice_get(looped);
    if (got < 0 || got >= 0x40)
        return 0x7f;
    v = got;
    g_voice_plain[v] = (uint8_t)g_plain_volume;
    if (g_sound_3d == 0)
        return (uint8_t)got;
    eax_voice_level((uint32_t)v, near ? g_level_near : g_level_far);
    device_voice_pitch((uint32_t)v, (uint32_t)rate(pitch));
    device_voice_volume((uint32_t)v, (uint32_t)scaled(vol, g_plain_volume != 0));
    device_voice_pos((uint32_t)v, 0, 0x41200000, 0);
    device_voice_dir((uint32_t)v, 0, 0, 0);
    device_voice_play((uint32_t)snd, (uint32_t)v, looped);
    return (uint8_t)got;
}

/* 0x472a40: sound_play with volume `vol` taken unscaled unless it is 0xffff. */
uint32_t GAMEAPI sound_play_volume(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t vol)
{
    uint32_t r;
    if ((uint16_t)vol != 0xffff)
        g_plain_volume = 1;
    else
        vol = a4;
    r = sound_play(a1, a2, a3, vol, a5);
    g_plain_volume = 0;
    return r;
}

/* 0x472a90: a non-positional voice for sound `sound` started at once.
 * Returns the voice, 0x7f for none. */
int32_t GAMEAPI voice_play_plain(uint32_t sound, int32_t pitch, int32_t vol)
{
    int32_t snd = (int8_t)sound, v;
    int8_t got;
    if ((int8_t)sound < 0 || snd >= g_sound_count)
        return 0x7f;
    got = (int8_t)device_voice_get(0);
    if (got < 0 || got >= 0x40)
        return 0x7f;
    v = got;
    eax_voice_level((uint32_t)v, g_level_far);
    device_voice_pitch((uint32_t)v, (uint32_t)rate(pitch));
    device_voice_volume((uint32_t)v, (uint32_t)scaled(vol, g_plain_volume != 0));
    device_voice_pos((uint32_t)v, 0, 0x41200000, 0);
    device_voice_dir((uint32_t)v, 0, 0, 0);
    device_voice_play((uint32_t)snd, (uint32_t)v, 0);
    return v;
}

/* 0x472b70: voice_play_plain with volume `vol` unscaled unless it is -1. */
int32_t GAMEAPI voice_play_plain_volume(uint32_t sound, int32_t pitch, int32_t vol0, int32_t vol)
{
    int32_t r;
    if (vol != -1)
        g_plain_volume = 1;
    else
        vol = vol0;
    r = voice_play_plain(sound, pitch, vol);
    g_plain_volume = 0;
    return r;
}

/* 0x472e90: toggles the voice debug display. */
void GAMEAPI sound_debug_toggle(void)
{
    g_debug = g_debug == 0;
    if (g_debug) {
        debug_printf((const char *)0x4b1ed0);
        device_debug_on();
    } else {
        device_debug_off();
        device_debug_clear();
    }
}

/* 0x472ed0: with the config's sound debug on, refreshes the display every
 * 4th frame (by toggling it off and on). */
void GAMEAPI sound_debug_tick(void)
{
    if (U8(g_config, 0x411) == 0)
        return;
    if (++g_debug_ticks == 4) {
        sound_debug_toggle();
        g_debug_ticks = 0;
        sound_debug_toggle();
    }
}

/* (fld 0x4b1eac; fsub 0x4d45f8; fdiv 8.0 at 0x4a3420; fstp): the EAX fade
 * step, in x87 so the rounding is the original's. */
static uint32_t eax_fade_step(void)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfdivs %3\n\tfstps %0"
                     : "=m"(r)
                     : "m"(*(const float *)0x4b1eac), "m"(*(const float *)0x4d45f8), "m"(*(const float *)0x4a3420)
                     : "st");
    return r;
}

/* Frees linked voice *link if it is a live voice index. */
static void mix_unlink(uint8_t *link, uint8_t *tmp)
{
    uint32_t id;
    *tmp = *link;
    id = *tmp;
    if ((int32_t)id < g_voice_count) {
        device_voice_free(id);
        U8(VOICE(id), V_STATE) = 0;
        *link = 0x7f;
    }
}

/* 0x4722d0: the per-frame mixer. Starts (0x40), updates (1) and frees
 * (0x41) the voice records: volume (by the master volume unless the voice is
 * plain), pitch, EAX level, and for positional voices the position relative
 * to the nearest listener (flag 0x20) or one listener per linked voice
 * (flag 2 parents: listeners 1-3 get child voices, flag 0x10). */
void GAMEAPI sound_mix(void)
{
    /* `tmp` is the original's byte at [esp+0x14]: the last linked voice id
     * looked at. A flag-0x10 voice reads it before any write in the frame
     * from stack garbage (sic): here it starts at 0. */
    uint8_t tmp = 0;
    g_mix_frames++;
    device_frame();
    sound_debug_tick();
    if (g_sound_3d != 0)
        return;
    for (int32_t i = 0; i < g_voice_count; i++) {
        uint8_t *v = VOICE(i);
        uint8_t *links[3] = {v + V_LINK0, v + V_LINK1, v + V_LINK2};
        uint8_t state = U8(v, V_STATE);
        int32_t vol, pitch;
        int8_t best;
        uint8_t far;
        int32_t rel_pos[3], rel_vel[3];
        uint32_t mat, d;
        uint8_t flags;

        if (state == 0)
            continue;
        if (state != 0x40 && state != 1) {
            if (state != 0x41)
                debug_printf((const char *)0x4b1eb0, i, (uint32_t)state);
            device_voice_free((uint32_t)i);
            U8(v, V_STATE) = 0;
            for (int32_t k = 0; k < 3; k++)
                mix_unlink(links[k], &tmp);
            continue;
        }
        if (state == 0x40)
            U16(v, V_FRAMES) = 0;
        U16(v, V_FRAMES)++;
        vol = (S16(v, V_VOL_R) + S16(v, V_VOL_L)) >> 1;
        far = 0;
        vol = scaled(vol, g_voice_plain[i] != 0);
        pitch = rate(I32(v, V_PITCH));
        rel_pos[0] = rel_pos[1] = rel_pos[2] = 0;
        rel_vel[0] = rel_vel[1] = rel_vel[2] = 0;
        mat = d = 0;
        if (U8(v, V_STATE) == 0x40) {
            eax_voice_level((uint32_t)i, U8(v, V_NEAR) ? g_level_near : g_level_far);
        } else {
            uint32_t level = U8(v, V_NEAR) ? g_level_near : g_level_far;
            eax_voice_fade((uint32_t)i, level, eax_fade_step());
        }
        flags = U8(v, V_FLAGS);
        if ((flags & 0x32) == 0) {
            device_voice_pitch((uint32_t)i, (uint32_t)pitch);
            device_voice_volume((uint32_t)i, (uint32_t)vol);
            device_voice_pos((uint32_t)i, 0, G32_AT(0x4b1ea8), 0);
            device_voice_dir((uint32_t)i, 0, 0, 0);
            goto started;
        }
        best = -1;
        if (flags & 0x20) {
            int32_t nearest = 0x100000;
            for (int8_t l = 0; l < 4; l++) {
                if (L_POS(l)) {
                    int32_t dist = sound_distance((const int32_t *)L_POS(l), (const int32_t *)PTR(v, V_POS));
                    if (dist < nearest) {
                        best = l;
                        nearest = dist;
                    }
                }
            }
            if (nearest >= 0x100000)
                far = 1;
        } else if (flags & 2) {
            best = 0;
            if (sound_distance((const int32_t *)L_POS(0), (const int32_t *)PTR(v, V_POS)) > 0x100000)
                goto stop;
            for (uint8_t l = 1; l < 4; l++) {
                uint8_t *link = links[l - 1];
                uint8_t *lpos = L_POS(l);
                uint32_t id;
                int32_t dist = 0;
                tmp = *link;
                if (lpos != 0)
                    dist = sound_distance((const int32_t *)lpos, (const int32_t *)PTR(v, V_POS));
                id = tmp;
                if ((int32_t)id < g_voice_count) {
                    if (lpos == 0 || dist > 0x100000) {
                        *link = 0x7f;
                        device_voice_free(id);
                        U8(VOICE(id), V_STATE) = 0;
                    } else {
                        uint8_t *c = VOICE(id);
                        U16(c, V_VOL_L) = U16(v, V_VOL_L);
                        U16(c, V_VOL_R) = U16(v, V_VOL_R);
                        I32(c, V_PITCH) = I32(v, V_PITCH);
                    }
                } else if (lpos != 0 && dist <= 0x100000) {
                    uint32_t pos = U32(v, V_POS);
                    uint8_t r = (uint8_t)sound_moving(
                        l, ((uint32_t)vol & 0xffffff00u) | U8(v, V_SOUND), (pos & 0xffffff00u) | U8(v, V_KIND),
                        (U32(v, V_VEL) & 0xffff0000u) | U16(v, V_PITCH), (uint32_t)vol, pos,
                        U32(v, V_VEL));
                    tmp = r;
                    if ((int32_t)r < g_voice_count) {
                        uint8_t *c = VOICE(r);
                        *link = r;
                        U8(c, V_LINK0) = l;
                        U8(c, V_FLAGS) = (U8(c, V_FLAGS) & ~2) | 0x10;
                    }
                }
            }
            goto relative;
        } else {
            /* flags & 0x10 */
            best = (int8_t)U8(VOICE(tmp), V_LINK0); /* sic: tmp, not this voice */
        }
        if (best < 0 || best >= 4 || far)
            goto stop;
    relative:
        mat = L_MAT[best];
        d = L_D[best];
        if (L_POS(best)) {
            const int32_t *p = (const int32_t *)L_POS(best);
            rel_pos[0] = p[0];
            rel_pos[1] = p[1];
            rel_pos[2] = p[2];
        }
        if (PTR(v, V_POS)) {
            const int32_t *p = (const int32_t *)PTR(v, V_POS);
            rel_pos[0] -= p[0];
            rel_pos[1] -= p[1];
            rel_pos[2] -= p[2];
        }
        if (PTR(v, V_VEL) && (U8(v, V_FLAGS) & 0x12)) {
            const int32_t *p = (const int32_t *)PTR(v, V_VEL);
            const int32_t *lv = (const int32_t *)L_VEL(best);
            rel_vel[0] = p[0];
            rel_vel[1] = p[1];
            rel_vel[2] = p[2];
            if (lv) {
                rel_vel[0] -= lv[0];
                rel_vel[1] -= lv[1];
                rel_vel[2] -= lv[2];
            }
        }
        goto place;
    stop:
        if (U8(v, V_KIND) != 9) {
            int32_t count = g_voice_count;
            for (int32_t k = 0; k < 3; k++) {
                uint32_t id = *links[k];
                if ((int32_t)id < count)
                    U8(VOICE(id), V_STATE) = 0x41;
            }
            device_voice_free((uint32_t)i);
            U8(v, V_STATE) = 0;
            continue;
        }
        vol = 0;
    place:
        device_voice_pitch((uint32_t)i, (uint32_t)pitch);
        device_voice_volume((uint32_t)i, (uint32_t)vol);
        voice_3d((int32_t)i, rel_pos, rel_vel, GMEM(mat), d);
    started:
        if (U8(v, V_STATE) == 0x40) {
            U8(v, V_STATE) = 1;
            g_voice_started[i] = 1;
            device_voice_play(U8(v, V_SOUND), (uint32_t)i, U8(v, V_KIND) == 9);
        }
    }
}

/* 0x472f10: starts the sound engine for the front end. */
void GAMEAPI sound_engine_start(void)
{
    uint32_t a = G32_AT(0x4b1e8c), b = G32_AT(0x4b1e90), c = G32_AT(0x4b1e94);
    G32_AT(0x5ff380) = 0x35800000;
    G32_AT(0x5ff2e0) = 0x37f00000;
    G32_AT(0x4b1c90) = a;
    G32_AT(0x4b1c94) = b;
    G32_AT(0x4b1c98) = c;
    device_start();
    sound_voices_reset(1);
    output_mode(1);
    sound_volume_set(U16(g_race, 0xa7c));
}

/* 0x472f70: restarts the device and resets the voices. */
void GAMEAPI sound_engine_restart(void)
{
    *(int32_t *)0x4b1c6c = -1;
    device_restart();
    sound_voices_reset(1);
}

/* 0x472f90: race music: CD track for the track (or 2 / 9 by the clock in
 * positional mode), CD volume (picked at random first time) and channels. */
void GAMEAPI race_music_start(void)
{
    static const uint8_t tracks[20] = {2, 7, 5, 2, 4, 5, 2, 5, 3, 6, 3, 8, 4, 2, 2, 4, 2, 7, 8, 6};
    uint8_t t;
    cd_open();
    cd_set_first(U16(g_race, 0x61e));
    cd_set_repeat(1);
    if (S16(g_race, 0xa7e) == -1) {
        int32_t r = cd_track_count();
        U16(g_race, 0xa7e) = (uint16_t)(r / 4);
    }
    sound_cd_volume_keep(U16(g_race, 0xa7e), U16(g_race, 0xa7e));
    sound_cd_channels(1, 0);
    if (g_sound_3d != 0) {
        sound_cd_track((game_time() & 1) ? 2 : 9);
        return;
    }
    t = U8(g_race, 0x579);
    /* sic: the original indexes its 20-byte stack table without a check;
     * past it, it reads the stack (here 0). */
    sound_cd_track((t < 20 ? tracks[t] : 0) + 1u);
}
