/* Sound engine, group DA (0x46f000-0x470fff): the QMixer session (init,
 * shutdown, activate), the wave (sample) table load and free, the 64 channel
 * records at 0x6cb500 (0x3c bytes each) and their play / stop / pause /
 * volume / frequency / position wrappers, the sound options and speaker
 * placement, and the profile statistics.
 *
 * The engine talks to QMIXER.DLL (a proxy) through the original's import
 * address table (0x4a31d0 + 4 * n): every call below reads the slot and calls
 * it stdcall, exactly as the original does. Voices, play / stop and the mixer
 * (0x471000 and up, group DB), the C runtime, file loading and the x87
 * direction / profile / refresh code (0x46f230, 0x470160, 0x470790,
 * 0x470ef0) stay original and are called by address.
 *
 * Channel record (0x3c bytes at 0x6cb500 + n * 0x3c):
 *   +00 byte flags: 1 playing, 2 looped, 4 paused, 8 (voice flag), 0x10 audible
 *   +01 byte kind (0 = unused), +02 byte timer, +03 byte wave index (-1 none)
 *   +08 frequency, +0c volume, +10/+14/+18 position (floats), +34, +38 voice data
 * QMixer IAT slots used (0x4a31d0 + 4 * index): 1 StartProfile, 2 SetSpeaker-
 * Placement, 3 EnableChannel, 4 SetOptions, 5 SetDistanceMapping, 7 SetPolar-
 * Position, 8 SetVolume, 9 SetFrequency, 10 IsChannelDone, 11 PauseChannel,
 * 12 FlushChannel, 13 PlayEx, 14 RestartChannel, 15 InitEx, 16 OpenWaveEx,
 * 17 OpenChannel, 18 GetDirectSoundBuffer, 19 GetChannelParams, 20 SetListener-
 * Rolloff, 21 GetDirectSound, 22 Activate, 23 CloseSession, 24 FreeWave. */
#include "../runtime.h"
#include "physics.h"

#define STDCALL __attribute__((stdcall))
#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define G32(a) (*(uint32_t *)(a))
#define GI32(a) (*(int32_t *)(a))

#define CHANNELS 64
#define CHANNEL_SIZE 0x3c
#define CHANNEL_TABLE 0x6cb500
#define CHANNEL(i) ((uint8_t *)(CHANNEL_TABLE + (uint32_t)(i) * CHANNEL_SIZE))
#define CHANNEL_END ((uint8_t *)(CHANNEL_TABLE + CHANNELS * CHANNEL_SIZE))

#define g_inited G32(0x4d4520)  /* snd_init has run */
#define g_open G32(0x4d4524)    /* QMixer session open */
#define g_active G32(0x4d4528)  /* session activated */
#define g_crowded G32(0x4d452c) /* more than 16 voices in use (0x470e70) */
#define g_waves G32(0x4d4530)   /* loaded wave file block */
#define g_wave_count GI32(0x4d4534)
#define g_session G32(0x4d453c) /* QMixer session handle */
#define g_wave_table G32(0x6002cc)
#define g_wave_data G32(0x6002c8)
#define g_last_channel GI32(0x4b1c6c)
#define g_channel_limit GI32(0x4b1c74)
#define g_speaker_mode GI32(0x4d454c)

typedef int32_t(STDCALL *qm1_fn)(uint32_t);
typedef int32_t(STDCALL *qm2_fn)(uint32_t, uint32_t);
typedef int32_t(STDCALL *qm3_fn)(uint32_t, uint32_t, uint32_t);
typedef int32_t(STDCALL *qm4_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t(STDCALL *qm6_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#define QM_START_PROFILE ((qm1_fn)G32(0x4a31d4))
#define QM_SET_SPEAKERS ((qm2_fn)G32(0x4a31d8))
#define QM_ENABLE_CHANNEL ((qm4_fn)G32(0x4a31dc))
#define QM_SET_OPTIONS ((qm3_fn)G32(0x4a31e0))
#define QM_SET_DISTANCE ((qm4_fn)G32(0x4a31e4))
#define QM_SET_POSITION ((qm4_fn)G32(0x4a31ec))
#define QM_SET_VOLUME ((qm4_fn)G32(0x4a31f0))
#define QM_SET_FREQUENCY ((qm4_fn)G32(0x4a31f4))
#define QM_IS_DONE ((qm2_fn)G32(0x4a31f8))
#define QM_PAUSE ((qm3_fn)G32(0x4a31fc))
#define QM_FLUSH ((qm3_fn)G32(0x4a3200))
#define QM_PLAY ((qm6_fn)G32(0x4a3204))
#define QM_RESTART ((qm3_fn)G32(0x4a3208))
#define QM_INIT ((qm1_fn)G32(0x4a320c))
#define QM_OPEN_WAVE ((qm3_fn)G32(0x4a3210))
#define QM_OPEN_CHANNEL ((qm3_fn)G32(0x4a3214))
#define QM_GET_BUFFER ((qm3_fn)G32(0x4a3218))
#define QM_GET_PARAMS ((qm3_fn)G32(0x4a321c))
#define QM_SET_ROLLOFF ((qm3_fn)G32(0x4a3220))
#define QM_GET_DSOUND ((qm2_fn)G32(0x4a3224))
#define QM_ACTIVATE ((qm2_fn)G32(0x4a3228))
#define QM_CLOSE ((qm1_fn)G32(0x4a322c))
#define QM_FREE_WAVE ((qm2_fn)G32(0x4a3230))

/* Original functions called by address (other groups, the x87 code, the
 * runtime). */
#define fix_sqrt_table ((int32_t(GAMEAPI *)(uint32_t))0x427960)
#define sound_voices_crowded_orig ((int32_t(GAMEAPI *)(void))0x470e70)
#define voices_init_orig ((void(GAMEAPI *)(void))0x471370)
#define voices_exit_orig ((void(GAMEAPI *)(void))0x4713b0)
#define voice_data_orig ((uint32_t(GAMEAPI *)(int32_t))0x471400)
#define channel_defaults_orig ((void(GAMEAPI *)(int32_t, uint32_t))0x4714c0)
#define channel_direction_orig ((void(GAMEAPI *)(int32_t, uint32_t, uint32_t, uint32_t))0x470160)
#define channels_refresh_orig ((int32_t(GAMEAPI *)(void))0x470ef0)
#define file_load_orig ((uint32_t(GAMEAPI *)(uint32_t, uint32_t))0x496590)
#define mem_free_orig ((void(GAMEAPI *)(uint32_t))0x49827a)
#define sprintf_orig ((int32_t(GAMEAPI *)(char *, const char *, ...))0x4979f0)
#define log_print_orig ((int32_t(GAMEAPI *)(const char *, const char *))0x4963c0)

int32_t GAMEAPI snd_channel_kind(int32_t channel);
uint32_t GAMEAPI snd_channel_buffer(int32_t channel);
int32_t GAMEAPI snd_voice_flags(void);

/* 0x46f1b0: length of a short vector (3 int16) by the square root table
 * 0x427960; 0x30 when a component exceeds 0x30 or the square sum 0x900. */
int32_t GAMEAPI snd_vec_length(const int16_t *v)
{
    int32_t x = v[0], y, z, a;
    uint32_t sum;

    a = x < 0 ? -x : x;
    if (a > 0x30)
        return 0x30;
    y = v[1];
    a = y < 0 ? -y : y;
    if (a > 0x30)
        return 0x30;
    z = v[2];
    a = z < 0 ? -z : z;
    if (a > 0x30)
        return 0x30;
    sum = (uint32_t)(z * z + y * y + x * x);
    if (sum > 0x900)
        return 0x30;
    return fix_sqrt_table(sum);
}

/* 0x46f080: merge `box` (6 int16: min, max at +0/+0xc) into the box `holder`
 * (pointer at +0xb8) refers to: when the two diagonals are about parallel and
 * their lengths add up under 0x30, `box` takes over the holder's box data
 * (+0xc..+0x17), is flagged (+0x1a = 1) and becomes the holder's box. 1 when
 * merged, else 0. `box` == -1 means none. */
int32_t GAMEAPI snd_box_merge(uint8_t *holder, uint8_t *box)
{
    int16_t s1[3], s2[3];
    int32_t len1, len2, dot, prod, limit;
    uint8_t *other;

    if ((int32_t)box == -1)
        return 0;
    s1[0] = (int16_t)(S16(box, 0xc) - S16(box, 0));
    s1[1] = (int16_t)(S16(box, 0xe) - S16(box, 2));
    s1[2] = (int16_t)(S16(box, 0x10) - S16(box, 4));
    len1 = snd_vec_length(s1);
    if (len1 >= 0x30)
        return 0;
    other = *(uint8_t **)(holder + 0xb8);
    s2[0] = (int16_t)(S16(other, 0xc) - S16(other, 0));
    s2[1] = (int16_t)(S16(other, 0xe) - S16(other, 2));
    s2[2] = (int16_t)(S16(other, 0x10) - S16(other, 4));
    len2 = snd_vec_length(s2);
    if (len1 + len2 >= 0x30)
        return 0;
    dot = s2[2] * s1[2] + s2[1] * s1[1] + s2[0] * s1[0];
    prod = len2 * len1;
    limit = (int32_t)(((uint32_t)prod << 8) - (uint32_t)prod) >> 8;
    if (dot <= limit)
        return 0;
    other = *(uint8_t **)(holder + 0xb8);
    U32(box, 0xc) = U32(other, 0xc);
    U32(box, 0x10) = U32(other, 0x10);
    U32(box, 0x14) = U32(other, 0x14);
    U16(box, 0x1a) = 1;
    U32(holder, 0xb8) = (uint32_t)box;
    return 1;
}

/* 0x46f6e0: the mixer sample rate for a requested rate: 11025 (0x2b11) for
 * 10026..12024, 44100 (0xac44) for 43101..45099, else 22050. */
int32_t GAMEAPI snd_rate_for(int32_t rate)
{
    if (rate > 0x2729 && rate < 0x2ef9)
        return 0x2b11;
    if (rate > 0xa85c && rate < 0xb02c)
        return 0xac44;
    return 0x5622;
}

/* 0x46f650: activate (1) or deactivate (0) the session; on success set the
 * active flag and (re)fill every channel's kind (0x46f7f0), buffer
 * (0x46f8f0) and voice data (0x471400) -- zero when deactivating -- then
 * refresh the voice flags (0x4704b0). -1 without a session. */
int32_t GAMEAPI snd_activate(int32_t on)
{
    uint32_t flag;
    int32_t i;

    if (!g_open)
        return -1;
    flag = on != 0;
    if (QM_ACTIVATE(g_session, flag) != 0)
        return 0;
    g_active = flag;
    for (i = 0; i < CHANNELS; i++) {
        uint8_t *ch = CHANNEL(i);

        if (flag) {
            U8(ch, 1) = (uint8_t)snd_channel_kind(i);
            U32(ch, 0x38) = snd_channel_buffer(i);
            U32(ch, 0x34) = voice_data_orig(i);
        } else {
            U8(ch, 1) = 0;
            U32(ch, 0x38) = 0;
            U32(ch, 0x34) = 0;
        }
    }
    snd_voice_flags();
    return 0;
}

/* 0x46f720: the session's DirectSound object (GetDirectSound), 0 without a
 * session, when inactive or on error. */
uint32_t GAMEAPI snd_dsound(void)
{
    uint32_t object;

    if (!g_open)
        return 0;
    if (!g_active)
        return 0;
    return QM_GET_DSOUND(g_session, (uint32_t)&object) == 0 ? object : 0;
}

/* 0x46f760: set the listener rolloff (a float passed as its bits). 0 / -1. */
int32_t GAMEAPI snd_set_rolloff(uint32_t rolloff)
{
    if (!g_open)
        return -1;
    if (!g_active)
        return -1;
    return QM_SET_ROLLOFF(g_session, rolloff, 0) != 0 ? -1 : 0;
}

/* 0x46f7a0: channel `channel`'s QMixer parameters into `params` (0xdc bytes,
 * size field first, cleared before). 0 / -1. */
int32_t GAMEAPI snd_channel_params(uint32_t *params, int32_t channel)
{
    int32_t i;

    for (i = 0; i < 0x37; i++)
        params[i] = 0;
    params[0] = 0xdc;
    if (!g_open)
        return -1;
    if (!g_active)
        return -1;
    if (channel < 0 || channel >= CHANNELS)
        return -1;
    return QM_GET_PARAMS(g_session, (uint32_t)channel, (uint32_t)params) != 0 ? -1 : 0;
}

/* 0x46f7f0: a channel's kind code from its QMixer flags (params +4): group
 * 0x01000000 looks at bits 16..23 (1 -> 0x15, 2 -> 0x29, 4 -> 0x49, 8 -> 0x85)
 * and falls through to the second table when none matches; group 0x02000000
 * uses the second table (1 -> 0x16, 2 -> 0x2a, 4 -> 0x4a, 8 -> 0x86); 0
 * otherwise. */
int32_t GAMEAPI snd_channel_kind(int32_t channel)
{
    uint32_t params[0x37];
    uint32_t flags, group, bits;

    if (snd_channel_params(params, channel) == -1)
        return 0;
    flags = params[1];
    group = flags & 0x7f000000;
    bits = flags & 0xff0000;
    if (group == 0x1000000) {
        if (bits == 0x40000)
            return 0x49;
        if (bits == 0x10000)
            return 0x15;
        if (bits == 0x20000)
            return 0x29;
        if (bits == 0x80000)
            return 0x85;
    } else if (group != 0x2000000) {
        return 0;
    }
    if (bits == 0x40000)
        return 0x4a;
    if (bits == 0x10000)
        return 0x16;
    if (bits == 0x20000)
        return 0x2a;
    if (bits == 0x80000)
        return 0x86;
    return 0;
}

/* 0x46f8f0: channel `channel`'s DirectSound buffer, 0 when unavailable. */
uint32_t GAMEAPI snd_channel_buffer(int32_t channel)
{
    uint32_t buffer = 0;

    if (!g_open)
        return 0;
    if (!g_active)
        return 0;
    if (channel < 0 || channel >= CHANNELS)
        return 0;
    return QM_GET_BUFFER(g_session, (uint32_t)channel, (uint32_t)&buffer) == 0 ? buffer : 0;
}

/* 0x46f940: open the session's channels (OpenChannel 0, 1). 0 / -1. */
int32_t GAMEAPI snd_open_channels(void)
{
    if (!g_open)
        return -1;
    if (!g_active)
        return -1;
    return QM_OPEN_CHANNEL(g_session, 0, 1) != 0 ? -1 : 0;
}

/* 0x470040: channel volume (|v| up to 0x7fff, cached at +0xc); QMixer
 * SetVolume only when it changed. 0 / -1. */
int32_t GAMEAPI snd_chan_volume(int32_t channel, int32_t volume)
{
    uint8_t *ch;

    if (!g_open)
        return -1;
    if (!g_active)
        return -1;
    if (channel < 0 || channel >= CHANNELS)
        return -1;
    if (volume < 0)
        volume = (int32_t)(0u - (uint32_t)volume);
    if (volume > 0x7fff)
        volume = 0x7fff;
    ch = CHANNEL(channel);
    if (I32(ch, 0xc) == volume)
        return 0;
    I32(ch, 0xc) = volume;
    return QM_SET_VOLUME(g_session, (uint32_t)channel, 0, (uint32_t)volume) != 0 ? -1 : 0;
}

/* 0x46ffd0: channel frequency (|v| up to 0xffff, cached at +8); QMixer
 * SetFrequency only when it changed. 0 / -1. */
int32_t GAMEAPI snd_chan_frequency(int32_t channel, int32_t frequency)
{
    uint8_t *ch;

    if (!g_open)
        return -1;
    if (!g_active)
        return -1;
    if (channel < 0 || channel >= CHANNELS)
        return -1;
    if (frequency < 0)
        frequency = (int32_t)(0u - (uint32_t)frequency);
    if (frequency > 0xffff)
        frequency = 0xffff;
    ch = CHANNEL(channel);
    if (I32(ch, 8) == frequency)
        return 0;
    I32(ch, 8) = frequency;
    return QM_SET_FREQUENCY(g_session, (uint32_t)channel, 0, (uint32_t)frequency) != 0 ? -1 : 0;
}

/* The x87 compare of two floats (fcomp, C3 clear = ordered and different),
 * on their bits: true when both are numbers and differ (+0 and -0 are equal). */
static int float_differs(uint32_t a, uint32_t b)
{
    if ((a & 0x7fffffff) > 0x7f800000 || (b & 0x7fffffff) > 0x7f800000)
        return 0;
    if (((a | b) & 0x7fffffff) == 0)
        return 0;
    return a != b;
}

/* 0x4700b0: channel position (3 floats at +0x10, passed as bits); QMixer
 * SetPolarPosition only when one of them changed (the original's x87 compare:
 * NaN counts as equal). 0 / -1. */
int32_t GAMEAPI snd_chan_position(int32_t channel, uint32_t x, uint32_t y, uint32_t z)
{
    uint8_t *ch;

    if (!g_open)
        return -1;
    if (!g_active)
        return -1;
    if (channel < 0 || channel >= CHANNELS)
        return -1;
    ch = CHANNEL(channel);
    if (!(float_differs(U32(ch, 0x10), x) || float_differs(U32(ch, 0x14), y) || float_differs(U32(ch, 0x18), z)))
        return 0;
    U32(ch, 0x10) = x;
    U32(ch, 0x14) = y;
    U32(ch, 0x18) = z;
    return QM_SET_POSITION(g_session, (uint32_t)channel, 0, (uint32_t)(ch + 0x10)) != 0 ? -1 : 0;
}

/* 0x46f970: every channel back to its defaults: frequency / volume cache -10,
 * position cache (+0x10, +0x1c) 0xc7f12000, +0x30 = 100.0f, then volume 0,
 * frequency 0, position (0, 10.0, 0), direction (0, 0, 0, 0x470160) and the
 * voice defaults (0x4714c0). 0 / -1. */
int32_t GAMEAPI snd_channels_reset(void)
{
    int32_t i;

    if (!g_open)
        return -1;
    if (!g_active)
        return -1;
    for (i = 0; i < CHANNELS; i++) {
        uint8_t *ch = CHANNEL(i);

        I32(ch, 0xc) = -10;
        I32(ch, 8) = -10;
        U32(ch, 0x10) = 0xc7f12000;
        U32(ch, 0x1c) = 0xc7f12000;
        U32(ch, 0x30) = 0x42c80000;
        snd_chan_volume(i, 0);
        snd_chan_frequency(i, 0);
        snd_chan_position(i, 0, 0x41200000, 0);
        channel_direction_orig(i, 0, 0, 0);
        channel_defaults_orig(i, 0);
    }
    return 0;
}

/* 0x46fa00: fill a 0x12-byte wave format (PCM header): tag, channels, rate,
 * bytes per second, block align, bits. Returns the format. */
uint8_t *GAMEAPI snd_wave_format(uint8_t *fmt, uint32_t tag, uint32_t channels, uint32_t rate, uint32_t bits)
{
    uint16_t block;

    U32(fmt, 0) = 0;
    U32(fmt, 4) = 0;
    U32(fmt, 8) = 0;
    U32(fmt, 0xc) = 0;
    U16(fmt, 0x10) = 0;
    block = (uint16_t)((uint16_t)bits >> 3);
    block = (uint16_t)(block * channels);
    U16(fmt, 0) = (uint16_t)tag;
    U16(fmt, 2) = (uint16_t)channels;
    U32(fmt, 4) = rate;
    U32(fmt, 8) = (uint32_t)block * rate;
    U16(fmt, 0xc) = block;
    U16(fmt, 0xe) = (uint16_t)bits;
    return fmt;
}

/* 0x46fa60: open a wave in the session (OpenWaveEx with {format, data, size,
 * 0, index}, flags 8). The wave handle, 0 without a session. */
uint32_t GAMEAPI snd_wave_open(uint32_t data, uint32_t size, uint32_t format, uint32_t index)
{
    uint32_t desc[5];

    if (!g_open)
        return 0;
    desc[0] = format;
    desc[1] = data;
    desc[2] = size;
    desc[3] = 0;
    desc[4] = index;
    return (uint32_t)QM_OPEN_WAVE(g_session, (uint32_t)desc, 8);
}

/* 0x46fad0: free a wave handle (FreeWave). 0 / -1; 0 for a null handle. */
int32_t GAMEAPI snd_wave_free(uint32_t wave)
{
    if (!g_open)
        return 0;
    if (!wave)
        return 0;
    return QM_FREE_WAVE(g_session, wave) != 0 ? -1 : 0;
}

/* 0x46fb00: free every loaded wave (0x40-byte records from g_wave_table, handle
 * at +0x10) and the file block. */
void GAMEAPI snd_waves_free(void)
{
    int32_t i;

    if (!g_waves) {
        g_waves = 0;
        g_wave_count = 0;
        return;
    }
    for (i = 0; i < g_wave_count; i++) {
        uint32_t wave = U32(g_wave_table, i * 0x40 + 0x10);

        if (wave) {
            snd_wave_free(wave);
            U32(g_wave_table, i * 0x40 + 0x10) = 0;
        }
    }
    mem_free_orig(g_waves);
    g_waves = 0;
    g_wave_count = 0;
}

/* 0x46fb80: load the wave file `name` (file loader 0x496590): count at +0x40,
 * 0x40-byte records from +0x44 (data offset +0, size +4, rate +0xc, bits
 * +0x14, channels +0x15) followed by the sample data; open each in the
 * session. Returns what the original leaves in eax (0, the table, or the
 * count). */
uint32_t GAMEAPI snd_waves_load(uint32_t name)
{
    uint32_t block, table;
    int32_t i;
    uint8_t fmt[0x14];

    snd_waves_free();
    block = file_load_orig(name, 0);
    g_waves = block;
    if (!block)
        return 0;
    g_wave_count = I32(block, 0x40);
    table = block + 0x44;
    g_wave_table = table;
    g_wave_data = table + ((uint32_t)I32(block, 0x40) << 6);
    if (I32(block, 0x40) <= 0)
        return table;
    for (i = 0; i < g_wave_count; i++) {
        uint8_t *rec = (uint8_t *)(g_wave_table + ((uint32_t)i << 6));

        snd_wave_format(fmt, 1, U8(rec, 0x15), U32(rec, 0xc), U8(rec, 0x14));
        U32(rec, 0x10) = snd_wave_open(U32(rec, 0) + g_wave_data, U32(rec, 4), (uint32_t)fmt, (uint32_t)i);
    }
    return (uint32_t)g_wave_count;
}

/* 0x46fd40: stop a channel (FlushChannel); clears its playing bit and timer.
 * 0 / -1. */
int32_t GAMEAPI snd_chan_stop(int32_t channel)
{
    uint8_t *ch;

    if (!g_open || !g_active || channel < 0 || channel >= CHANNELS)
        return 0;
    if (QM_FLUSH(g_session, (uint32_t)channel, 0) != 0)
        return -1;
    ch = CHANNEL(channel);
    U8(ch, 0) &= 0xfe;
    U8(ch, 2) = 0;
    return 0;
}

/* 0x46fc40: play wave `wave` on `channel` (PlayEx, looped unless the wave's
 * own flag at +0x16 forces it), restart it and set the channel record:
 * playing | looped bit, timer 10, paused / audible bits cleared, wave index.
 * 0 (also when it does not play), -1 when PlayEx fails. */
int32_t GAMEAPI snd_chan_play(int32_t wave, int32_t channel, uint32_t loop)
{
    uint8_t *rec, *ch;
    uint32_t handle, looped;

    if (!g_open || !g_active || channel < 0 || channel >= CHANNELS)
        return 0;
    if (!g_waves)
        return 0;
    if (wave < 0 || wave >= g_wave_count)
        return 0;
    rec = (uint8_t *)(g_wave_table + ((uint32_t)wave << 6));
    handle = U32(rec, 0x10);
    if (!handle)
        return 0;
    looped = 1;
    if (U8(rec, 0x16) == 0)
        looped = loop;
    if (QM_PLAY(g_session, (uint32_t)channel, 1, handle, looped ? 0xffffffffu : 0, 0) != 0)
        return -1;
    QM_RESTART(g_session, (uint32_t)channel, 0);
    ch = CHANNEL(channel);
    U8(ch, 0) = (uint8_t)(((looped & 1) << 1) | (U8(ch, 0) & 0xfd) | 1);
    U8(ch, 2) = 0xa;
    U8(ch, 0) &= 0xf3;
    U8(ch, 3) = (uint8_t)wave;
    return 0;
}

/* 0x46fda0: pause a channel (PauseChannel unless bit 8) and set bit 4. 0 / -1. */
int32_t GAMEAPI snd_chan_pause(int32_t channel)
{
    uint8_t *ch;

    if (!g_open || !g_active || channel < 0 || channel >= CHANNELS)
        return 0;
    ch = CHANNEL(channel);
    if (!(U8(ch, 0) & 8)) {
        if (QM_PAUSE(g_session, (uint32_t)channel, 0) != 0)
            return -1;
    }
    U8(ch, 0) |= 4;
    return 0;
}

/* 0x46fe00: resume a channel (RestartChannel unless bit 8), clear bit 4. */
int32_t GAMEAPI snd_chan_resume(int32_t channel)
{
    uint8_t *ch;

    if (!g_open || !g_active || channel < 0 || channel >= CHANNELS)
        return 0;
    ch = CHANNEL(channel);
    if (!(U8(ch, 0) & 8)) {
        if (QM_RESTART(g_session, (uint32_t)channel, 0) != 0)
            return -1;
    }
    U8(ch, 0) &= 0xfb;
    return 0;
}

/* 0x46fe60: pause every channel whose kind (+1) is set. */
void GAMEAPI snd_pause_all(void)
{
    int32_t i;

    for (i = 0; i < CHANNELS; i++)
        if (U8(CHANNEL(i), 1))
            snd_chan_pause(i);
}

/* 0x46fe90: resume every channel whose kind (+1) is set. */
void GAMEAPI snd_resume_all(void)
{
    int32_t i;

    for (i = 0; i < CHANNELS; i++)
        if (U8(CHANNEL(i), 1))
            snd_chan_resume(i);
}

/* 0x46fec0: 1 while the channel is (or may be) still playing, 0 when it is
 * free: not marked playing, or QMixer reports it done (the bit is cleared). */
int32_t GAMEAPI snd_chan_busy(int32_t channel)
{
    uint8_t *ch;

    if (!g_open || !g_active || channel < 0 || channel >= CHANNELS)
        return 1;
    ch = CHANNEL(channel);
    if (!(U8(ch, 0) & 1))
        return 0;
    if (U8(ch, 2) != 0)
        return 1;
    if (!QM_IS_DONE(g_session, (uint32_t)channel))
        return 1;
    U8(ch, 0) &= 0xfe;
    return 0;
}

/* 0x46ff30: the next free audible channel after the last one used (round
 * robin over 64): marks its wave index -1 and returns it, -1 if none. */
int32_t GAMEAPI snd_chan_find(void)
{
    int32_t i, n;

    if (!g_open || !g_active)
        return -1;
    n = g_last_channel;
    for (i = 0; i < CHANNELS; i++) {
        n++;
        if (n < 0 || n >= CHANNELS)
            n = 0;
        if ((U8(CHANNEL(n), 0) & 0x10) && snd_chan_busy(n) == 0) {
            g_last_channel = n;
            U8(CHANNEL(n), 3) = 0xff;
            return n;
        }
    }
    return -1;
}

/* 0x46ffa0: claim a free channel (arg unused, callers push one) and give it a
 * 10-step timer. The channel or -1. */
int32_t GAMEAPI snd_chan_claim(int32_t unused)
{
    int32_t n;

    (void)unused;
    n = snd_chan_find();
    if (n != -1)
        U8(CHANNEL(n), 2) = 0xa;
    return n;
}

/* 0x4702a0: tell QMixer the distance range (a 16-byte descriptor at 0x4d44c8
 * with the range floats 0x4b1c90 / 0x4b1c94). 0 / -1. */
int32_t GAMEAPI snd_distance_map(void)
{
    uint32_t near_d, far_d;

    if (!g_open)
        return -1;
    near_d = G32(0x4b1c90);
    far_d = G32(0x4b1c94);
    G32(0x4d44c8) = 0;
    G32(0x4d44cc) = 0;
    G32(0x4d44d0) = 0;
    G32(0x4d44d4) = 0;
    G32(0x4d44c8) = 0x10;
    G32(0x4d44cc) = near_d;
    G32(0x4d44d0) = far_d;
    return QM_SET_DISTANCE(g_session, 0, 1, 0x4d44c8) != 0 ? -1 : 0;
}

/* 0x470300: session options: 4 when 0x4d4548, |3 when 0x4b1c78 (mask 7). 0 / -1. */
int32_t GAMEAPI snd_options_apply(void)
{
    uint32_t options = 0;

    if (G32(0x4d4548))
        options = 4;
    if (G32(0x4b1c78))
        options |= 3;
    if (!g_open)
        return -1;
    if (!g_active)
        return -1;
    return QM_SET_OPTIONS(g_session, options, 7) != 0 ? -1 : 0;
}

/* 0x470350: enable flags for every channel in use: from the settings
 * 0x4b1c7c (1), 0x4d4544 (8), 0x4d4550 (4), 0x4d4554 (2) and 0x4b1c80 == 0
 * (0x10), combined with the channel's own QMixer flags (params +0x5c) minus
 * the bits in 0x4b1cac. 0, or -1 when any channel failed (or no session). */
int32_t GAMEAPI snd_channels_enable(void)
{
    uint32_t params[0x37];
    uint32_t flags;
    int32_t result, i, j;

    flags = G32(0x4b1c7c) != 0;
    if (G32(0x4d4544))
        flags |= 8;
    if (G32(0x4d4550))
        flags |= 4;
    if (G32(0x4d4554))
        flags |= 2;
    if (G32(0x4b1c80) == 0)
        flags |= 0x10;
    if (!g_open)
        return -1;
    if (!g_active)
        return -1;
    result = 0;
    for (i = 0; i < CHANNELS; i++) {
        if (!U8(CHANNEL(i), 1))
            continue;
        for (j = 0; j < 0x37; j++)
            params[j] = 0;
        params[0] = 0xdc;
        if (QM_GET_PARAMS(g_session, (uint32_t)i, (uint32_t)params) == 0) {
            uint32_t value = (~G32(0x4b1cac) & params[0x17]) | flags;

            if (QM_ENABLE_CHANNEL(g_session, (uint32_t)i, 0, value) == 0)
                continue;
        }
        result = -1;
    }
    return result;
}

/* 0x470470: clamp the channel count 0x4b1c74 to 8..32 and refresh the
 * channels (0x470ef0). 0. */
int32_t GAMEAPI snd_channel_limit(void)
{
    int32_t n = g_channel_limit;

    if (n < 8) {
        g_channel_limit = 8;
        channels_refresh_orig();
        return 0;
    }
    if (n > 0x20)
        g_channel_limit = 0x20;
    channels_refresh_orig();
    return 0;
}

/* 0x4704b0: voices crowded flag (0x4d452c, from 0x470e70 when 0x4b1c88) and
 * every channel's audible bit (0x10): with a crowd, those with voice data
 * (+0x34); otherwise those of kind bit 4, or kind bit 8 when 0x4b1c84. Then
 * refresh (0x470ef0). 0. */
int32_t GAMEAPI snd_voice_flags(void)
{
    int32_t crowded = 0;
    uint32_t is_open, is_active, extra;
    uint8_t *ch;

    if (G32(0x4b1c88))
        crowded = sound_voices_crowded_orig();
    extra = G32(0x4b1c84);
    is_open = g_open;
    is_active = g_active;
    g_crowded = (uint32_t)crowded;
    for (ch = (uint8_t *)CHANNEL_TABLE; ch < CHANNEL_END; ch += CHANNEL_SIZE) {
        uint8_t flags = U8(ch, 0) & 0xef;

        U8(ch, 0) = flags;
        if (!is_open || !is_active)
            continue;
        if (crowded) {
            if (U32(ch, 0x34) == 0)
                continue;
        } else {
            uint8_t kind = U8(ch, 1);

            if (!(kind & 4)) {
                if (!(kind & 8))
                    continue;
                if (extra == 0)
                    continue;
            }
        }
        U8(ch, 0) = flags | 0x10;
    }
    channels_refresh_orig();
    return 0;
}

/* 0x470530: apply the speaker placement mode 0x4d454c (0 -> 0, 1 -> 3, 2 -> 1,
 * 3 -> 2; anything else is reset to 0). 0 / -1. */
int32_t GAMEAPI snd_speakers(void)
{
    uint32_t placement;

    if (!g_open)
        return -1;
    if (!g_active)
        return -1;
    switch ((uint32_t)g_speaker_mode) {
    case 1:
        placement = 3;
        break;
    case 2:
        placement = 1;
        break;
    case 3:
        placement = 2;
        break;
    case 0:
        placement = 0;
        break;
    default:
        g_speaker_mode = 0;
        placement = 0;
        break;
    }
    return QM_SET_SPEAKERS(g_session, placement) != 0 ? -1 : 0;
}

/* 0x4705d0: apply all the sound settings: channel limit, voice flags,
 * speakers, options, channel enables, then the refresh (0x470ef0), whose
 * result it returns. */
int32_t GAMEAPI snd_settings_apply(void)
{
    snd_channel_limit();
    snd_voice_flags();
    snd_speakers();
    snd_options_apply();
    snd_channels_enable();
    return channels_refresh_orig();
}

/* 0x4705f0: step the speaker mode 0x4d454c forward (`next`) or back through
 * 0..3, skipping 3 unless `allow3`, and apply it. 0 / -1. */
int32_t GAMEAPI snd_speaker_cycle(int32_t next, int32_t allow3)
{
    int32_t mode = g_speaker_mode;

    if (mode < 0 || mode >= 4) {
        g_speaker_mode = 0;
        return snd_speakers();
    }
    for (;;) {
        if (next) {
            mode++;
            if (mode >= 4)
                mode = 0;
        } else {
            mode--;
            if (mode < 0)
                mode = 3;
        }
        if (allow3 == 0 && mode == 3)
            continue;
        break;
    }
    g_speaker_mode = mode;
    return snd_speakers();
}

/* 0x470640: add `delta` to the channel count 0x4b1c74 (clamped to 8..32) and
 * apply it when it changed; else the clamped count. */
int32_t GAMEAPI snd_channel_count_add(int32_t delta)
{
    int32_t old = g_channel_limit;
    int32_t n = delta + old;

    if (n < 8)
        n = 8;
    else if (n > 0x20)
        n = 0x20;
    if (old == n)
        return n;
    g_channel_limit = n;
    return snd_channel_limit();
}

/* 0x4706e0: clear 16 dwords (a profile row of 8 doubles). Returns p. */
uint32_t *GAMEAPI snd_zero16(uint32_t *p)
{
    p[0] = 0;
    p[2] = 0;
    p[4] = 0;
    p[6] = 0;
    p[8] = 0;
    p[10] = 0;
    p[12] = 0;
    p[14] = 0;
    p[1] = 0;
    p[3] = 0;
    p[5] = 0;
    p[7] = 0;
    p[9] = 0;
    p[11] = 0;
    p[13] = 0;
    p[15] = 0;
    return p;
}

/* 0x470720: clear 6 dwords (3 doubles). Returns p. */
uint32_t *GAMEAPI snd_zero6(uint32_t *p)
{
    p[0] = 0;
    p[2] = 0;
    p[4] = 0;
    p[1] = 0;
    p[3] = 0;
    p[5] = 0;
    return p;
}

/* 0x470680: clear the profile statistics (0x600160 samples, the four rows of
 * 8 doubles at 0x600168.., then the four of 3 doubles from 0x600268). */
void GAMEAPI snd_stats_clear(void)
{
    G32(0x600160) = 0;
    G32(0x600164) = 0;
    snd_zero16((uint32_t *)0x600168);
    snd_zero16((uint32_t *)0x6001a8);
    snd_zero16((uint32_t *)0x6001e8);
    snd_zero16((uint32_t *)0x600228);
    snd_zero6((uint32_t *)0x600268);
    snd_zero6((uint32_t *)0x600280);
    snd_zero6((uint32_t *)0x600298);
    snd_zero6((uint32_t *)0x6002b0);
}

/* 0x470740: start the QMixer profile once (0x4d4558): clears the statistics
 * first when 0x4b1cb0 is set. 0, -1 without a session. */
int32_t GAMEAPI snd_profile_start(void)
{
    if (!g_open)
        return -1;
    if (G32(0x4d4558) == 0) {
        uint32_t clear = G32(0x4b1cb0);

        G32(0x4d4558) = 1;
        if (clear) {
            snd_stats_clear();
            G32(0x4b1cb0) = 0;
        }
        QM_START_PROFILE(g_session);
    }
    return 0;
}

/* 0x470ca0: format the profile statistics into `dst` with the format at
 * 0x4b1cb4: 29 doubles read from 0x600160.. in the order the original pushes
 * them (as dword pairs), and a final 0. Returns sprintf's result. */
int32_t GAMEAPI snd_stats_format(char *dst)
{
#define D(a) G32(a), G32((a) + 4)
    return sprintf_orig(dst, (const char *)0x4b1cb4, D(0x600160), D(0x600168), D(0x6001a8), D(0x6001e8), D(0x600228),
                        D(0x600170), D(0x6001b0), D(0x6001f0), D(0x600230), D(0x600178), D(0x6001b8), D(0x6001f8),
                        D(0x600238), D(0x600180), D(0x6001c0), D(0x600200), D(0x600240), D(0x600268), D(0x600280),
                        D(0x600298), D(0x6002b0), D(0x600270), D(0x600288), D(0x6002a0), D(0x6002b8), D(0x600278),
                        D(0x600290), D(0x6002a8), D(0x6002c0), 0);
#undef D
}

/* 0x470e40: write the statistics text (0x4963c0 with 0x4b1e48). */
int32_t GAMEAPI snd_stats_log(void)
{
    char text[0x800];

    snd_stats_format(text);
    return log_print_orig((const char *)0x4b1e48, text);
}

/* 0x46f610: shut the sound engine down: voices (0x4713b0), deactivate,
 * close the session; clears the init and open flags. */
int32_t GAMEAPI snd_shutdown(void)
{
    int32_t result = 0;

    if (g_open) {
        voices_exit_orig();
        snd_activate(0);
        result = QM_CLOSE(g_session);
    }
    g_inited = 0;
    g_open = 0;
    return result;
}

/* 0x46f4f0: start the sound engine once: sample rate (0x46f6e0), clear the
 * 64 channel records and the session description (0x4d44e0: 0x40, 0x18 bits,
 * rate, window 0x4d4538, 0x20 channels), open the session (InitEx); on
 * success distance map, activate, voices (0x471370), settings, channel
 * open and defaults. Returns what the original leaves in eax. */
int32_t GAMEAPI snd_init(void)
{
    int32_t rate, result, i;
    uint8_t *ch;
    uint32_t window;

    if (g_inited)
        return (int32_t)g_inited;
    rate = snd_rate_for(GI32(0x4b1c70));
    for (i = 0; i < 0x3c0; i++)
        ((uint32_t *)CHANNEL_TABLE)[i] = 0;
    G32(0x4b1c70) = (uint32_t)rate;
    g_inited = 1;
    g_open = 1;
    g_active = 0;
    g_crowded = 0;
    for (ch = (uint8_t *)CHANNEL_TABLE; ch < CHANNEL_END; ch += CHANNEL_SIZE) {
        U8(ch, 0) &= 0xe0;
        U8(ch, 2) = 0;
        U8(ch, 1) = 0;
        U32(ch, 0x38) = 0;
        U32(ch, 0x34) = 0;
        U8(ch, 3) = 0xff;
    }
    for (i = 0; i < 0x10; i++)
        ((uint32_t *)0x4d44e0)[i] = 0;
    window = G32(0x4d4538);
    G32(0x4d44e0) = 0x40;
    G32(0x4d44f4) = 0x40;
    G32(0x4d44e4) = 0x18;
    G32(0x4d44e8) = (uint32_t)rate;
    G32(0x4d4504) = window;
    G32(0x4d4508) = 0x20;
    G32(0x4d450c) = 0x20;
    G32(0x4d451c) = 0x20;
    result = QM_INIT(0x4d44e0);
    G32(0x4d453c) = (uint32_t)result;
    if (result == 0) {
        g_open = 0;
        return 0;
    }
    snd_distance_map();
    snd_activate(1);
    result = 0;
    if (g_active) {
        voices_init_orig();
        snd_settings_apply();
        snd_open_channels();
        result = snd_channels_reset();
    }
    g_last_channel = -1;
    return result;
}
