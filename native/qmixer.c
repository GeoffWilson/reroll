/* QMixer for the native build (docs/native/plan.md, step 6): the 25 QSound
 * QMixer calls the sound engine (src/game/snd_da.c, snd_db.c) makes, as a
 * small mixer on SDL3 audio. Waves are PCM in memory (8 or 16 bit, mono or
 * stereo); each of the 32 channels plays one wave once or looped at its own
 * frequency (0 = the wave's rate) and volume (0..32767), placed by a polar
 * position: azimuth in degrees (0 ahead, 90 right) pans it (constant power,
 * a little quieter behind), and the range attenuates it as DirectSound 3D
 * does (min / (min + rolloff * (range - min)), between the session's
 * distance mapping's near and far) when the channel's features include
 * QMIX_CHANNEL_RANGE (4). Doppler and elevation are ignored.
 * The engine reads no DirectSound objects from here (those calls fail, which
 * it handles). Argument counts as the engine calls them. */
#include "nt.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHANNELS 32
#define OUT_RATE 44100
#define FEATURE_RANGE 4

struct wave {
    float *samples; /* interleaved */
    uint32_t frames;
    int channels;
    uint32_t rate;
};

struct channel {
    struct wave *wave;
    double position; /* in frames */
    int playing, paused, loops; /* loops: -1 forever, else repeats left */
    float volume;
    uint32_t frequency;
    float azimuth, range;
    DWORD features;
};

static SDL_AudioStream *stream;
static struct channel channels[CHANNELS];
static float near_distance = 1.0f, far_distance = 100.0f, rolloff = 1.0f;
static int active;
static float *mix;
static FILE *dump;
static int mix_frames;

static void channel_gains(const struct channel *c, float *left, float *right)
{
    float a = c->azimuth * (float)M_PI / 180.0f, pan = sinf(a), gain = c->volume;
    float angle = (pan + 1.0f) * (float)M_PI / 4.0f;
    if (cosf(a) < 0.0f)
        gain *= 0.8f; /* behind */
    if (c->features & FEATURE_RANGE) {
        float r = c->range < near_distance ? near_distance : c->range > far_distance ? far_distance : c->range;
        gain *= near_distance / (near_distance + rolloff * (r - near_distance));
    }
    gain *= 0.7f; /* headroom: a race mixes many channels */
    *left = gain * cosf(angle) * 1.41421356f * 0.5f;
    *right = gain * sinf(angle) * 1.41421356f * 0.5f;
}

/* SDL asks for more output: mix every playing channel */
static void SDLCALL feed(void *user, SDL_AudioStream *s, int needed, int total)
{
    int frames = needed / (int)(2 * sizeof(float)), i, f;
    (void)user;
    (void)total;
    if (frames <= 0)
        return;
    if (frames > mix_frames) {
        mix = SDL_realloc(mix, (size_t)frames * 2 * sizeof(float));
        mix_frames = frames;
    }
    memset(mix, 0, (size_t)frames * 2 * sizeof(float));
    for (i = 0; i < CHANNELS && active; i++) {
        struct channel *c = &channels[i];
        struct wave *w = c->wave;
        float left, right;
        double step;
        if (!c->playing || c->paused || !w || !w->frames)
            continue;
        channel_gains(c, &left, &right);
        step = (double)(c->frequency ? c->frequency : w->rate) / OUT_RATE;
        for (f = 0; f < frames; f++) {
            uint32_t p = (uint32_t)c->position, q = p + 1;
            float t = (float)(c->position - p), l, r;
            if (q >= w->frames)
                q = c->loops ? 0 : p;
            if (w->channels == 2) {
                l = w->samples[p * 2] + (w->samples[q * 2] - w->samples[p * 2]) * t;
                r = w->samples[p * 2 + 1] + (w->samples[q * 2 + 1] - w->samples[p * 2 + 1]) * t;
            } else {
                l = r = w->samples[p] + (w->samples[q] - w->samples[p]) * t;
            }
            mix[f * 2] += l * left;
            mix[f * 2 + 1] += r * right;
            c->position += step;
            if (c->position >= w->frames) {
                if (c->loops) {
                    c->position -= w->frames;
                    if (c->loops > 0)
                        c->loops--;
                } else {
                    c->playing = 0;
                    break;
                }
            }
        }
    }
    for (f = 0; f < frames * 2; f++)
        mix[f] = mix[f] > 1.0f ? 1.0f : mix[f] < -1.0f ? -1.0f : mix[f];
    SDL_PutAudioStreamData(s, mix, frames * 2 * (int)sizeof(float));
    if (dump) /* REROLL_AUDIO_DUMP: raw float stereo 44.1 kHz, for tests */
        fwrite(mix, sizeof(float), (size_t)frames * 2, dump);
}

static void lock(void)
{
    if (stream)
        SDL_LockAudioStream(stream);
}

static void unlock(void)
{
    if (stream)
        SDL_UnlockAudioStream(stream);
}

static int valid(DWORD ch)
{
    return ch < CHANNELS;
}

/* QSWaveMixInitEx(config): the session (one; a static, so a low address) */
void *WINAPI QSWaveMixInitEx(void *config)
{
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, OUT_RATE};
    (void)config;
    if (!stream) {
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, 0);
        if (!stream) {
            nt_log("qmixer: no audio device (%s)", SDL_GetError());
            return 0;
        }
        if (getenv("REROLL_AUDIO_DUMP"))
            dump = fopen(getenv("REROLL_AUDIO_DUMP"), "wb");
        SDL_ResumeAudioStreamDevice(stream);
        nt_log("qmixer: mixing %d channels at %d Hz", CHANNELS, OUT_RATE);
    }
    return (void *)&channels;
}

DWORD WINAPI QSWaveMixActivate(DWORD session, DWORD on)
{
    (void)session;
    lock();
    active = on != 0;
    unlock();
    return 0;
}

DWORD WINAPI QSWaveMixCloseSession(DWORD session)
{
    (void)session;
    lock();
    active = 0;
    memset(channels, 0, sizeof channels);
    unlock();
    return 0;
}

DWORD WINAPI QSWaveMixOpenChannel(DWORD session, DWORD channel, DWORD flags)
{
    (void)session;
    (void)channel;
    (void)flags;
    return 0;
}

/* OpenWaveEx(session, {WAVEFORMATEX *, data, bytes, -, index}, flags 8 = from memory) */
void *WINAPI QSWaveMixOpenWaveEx(DWORD session, const DWORD *params, DWORD flags)
{
    const uint8_t *format = (const uint8_t *)(uintptr_t)params[0], *data = (const uint8_t *)(uintptr_t)params[1];
    uint32_t bytes = params[2], i, count;
    uint16_t tag, chans, bits;
    struct wave *w;
    (void)session;
    (void)flags;
    if (!format || !data)
        return 0;
    tag = *(const uint16_t *)format;
    chans = *(const uint16_t *)(format + 2);
    bits = *(const uint16_t *)(format + 0xe);
    if (tag != 1 || (chans != 1 && chans != 2) || (bits != 8 && bits != 16)) {
        nt_log("qmixer: wave format %u, %u channels, %u bits not supported", tag, chans, bits);
        return 0;
    }
    w = nt_lowmem_calloc(1, (uint32_t)sizeof *w); /* the game keeps it in 32 bits */
    count = bytes / (bits / 8);
    w->channels = chans;
    w->rate = *(const uint32_t *)(format + 4);
    w->frames = count / chans;
    w->samples = SDL_malloc((size_t)(count ? count : 1) * sizeof(float));
    for (i = 0; i < count; i++)
        w->samples[i] = bits == 8 ? ((float)data[i] - 128.0f) / 128.0f : (float)((const int16_t *)data)[i] / 32768.0f;
    return w;
}

DWORD WINAPI QSWaveMixFreeWave(DWORD session, struct wave *w)
{
    int i;
    (void)session;
    if (!w)
        return 0;
    lock();
    for (i = 0; i < CHANNELS; i++)
        if (channels[i].wave == w) {
            channels[i].wave = 0;
            channels[i].playing = 0;
        }
    unlock();
    SDL_free(w->samples);
    nt_lowmem_free(w);
    return 0;
}

/* PlayEx(session, channel, flags, wave, loops (-1 forever), params) */
DWORD WINAPI QSWaveMixPlayEx(DWORD session, DWORD channel, DWORD flags, struct wave *w, DWORD loops, DWORD params)
{
    (void)session;
    (void)flags;
    (void)params;
    if (!valid(channel) || !w)
        return 1;
    lock();
    channels[channel].wave = w;
    channels[channel].position = 0;
    channels[channel].loops = loops == 0xffffffffu ? -1 : (int)loops;
    channels[channel].playing = 1;
    unlock();
    return 0;
}

DWORD WINAPI QSWaveMixPauseChannel(DWORD session, DWORD channel, DWORD flags)
{
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    lock();
    channels[channel].paused = 1;
    unlock();
    return 0;
}

DWORD WINAPI QSWaveMixRestartChannel(DWORD session, DWORD channel, DWORD flags)
{
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    lock();
    channels[channel].paused = 0;
    unlock();
    return 0;
}

DWORD WINAPI QSWaveMixFlushChannel(DWORD session, DWORD channel, DWORD flags)
{
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    lock();
    channels[channel].playing = 0;
    unlock();
    return 0;
}

DWORD WINAPI QSWaveMixIsChannelDone(DWORD session, DWORD channel)
{
    int done;
    (void)session;
    if (!valid(channel))
        return 1;
    lock();
    done = !channels[channel].playing;
    unlock();
    return (DWORD)done;
}

DWORD WINAPI QSWaveMixSetVolume(DWORD session, DWORD channel, DWORD flags, DWORD volume)
{
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    lock();
    channels[channel].volume = (float)(volume > 32767 ? 32767 : volume) / 32767.0f;
    unlock();
    return 0;
}

DWORD WINAPI QSWaveMixSetFrequency(DWORD session, DWORD channel, DWORD flags, DWORD frequency)
{
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    lock();
    channels[channel].frequency = frequency;
    unlock();
    return 0;
}

/* SetPolarPosition(session, channel, flags, QSPOLAR {azimuth, range, elevation}) */
DWORD WINAPI QSWaveMixSetPolarPosition(DWORD session, DWORD channel, DWORD flags, const float *polar)
{
    (void)session;
    (void)flags;
    if (!valid(channel) || !polar)
        return 1;
    lock();
    channels[channel].azimuth = polar[0];
    channels[channel].range = polar[1];
    unlock();
    return 0;
}

DWORD WINAPI QSWaveMixSetSourceVelocity(DWORD session, DWORD channel, DWORD flags, const float *velocity)
{
    (void)session;
    (void)channel;
    (void)flags;
    (void)velocity;
    return 0;
}

/* SetDistanceMapping(session, channel, flags, {size, near, far, scale}) */
DWORD WINAPI QSWaveMixSetDistanceMapping(DWORD session, DWORD channel, DWORD flags, const float *distances)
{
    (void)session;
    (void)channel;
    (void)flags;
    if (distances && distances[1] > 0.0f && distances[2] > distances[1]) {
        lock();
        near_distance = distances[1];
        far_distance = distances[2];
        unlock();
    }
    return 0;
}

/* SetListenerRolloff(session, rolloff (float bits), flags) */
DWORD WINAPI QSWaveMixSetListenerRolloff(DWORD session, DWORD value, DWORD flags)
{
    float r;
    (void)session;
    (void)flags;
    memcpy(&r, &value, 4);
    if (r >= 0.0f && r < 100.0f) {
        lock();
        rolloff = r;
        unlock();
    }
    return 0;
}

/* EnableChannel(session, channel, flags, features) */
DWORD WINAPI QSWaveMixEnableChannel(DWORD session, DWORD channel, DWORD flags, DWORD features)
{
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    lock();
    channels[channel].features = features;
    unlock();
    return 0;
}

/* GetChannelParams(session, channel, params (0xdc bytes, size first)): features at +0x5c */
DWORD WINAPI QSWaveMixGetChannelParams(DWORD session, DWORD channel, DWORD *params)
{
    (void)session;
    if (!valid(channel) || !params)
        return 1;
    /* +4: channel type; 0x02000000 | 0x10000 is what the engine reads as an
     * always-audible channel (kind 0x16, snd_channel_kind 0x46f7f0) */
    params[1] = 0x02010000u;
    params[0x5c / 4] = channels[channel].features;
    return 0;
}

DWORD WINAPI QSWaveMixSetOptions(DWORD session, DWORD options, DWORD mask)
{
    (void)session;
    (void)options;
    (void)mask;
    return 0;
}

DWORD WINAPI QSWaveMixSetSpeakerPlacement(DWORD session, DWORD placement)
{
    (void)session;
    (void)placement;
    return 0;
}

/* no DirectSound objects behind this mixer (out: 4-byte slots in game memory) */
DWORD WINAPI QSWaveMixGetDirectSound(DWORD session, uint32_t *out)
{
    (void)session;
    if (out)
        *out = 0;
    return 1;
}

DWORD WINAPI QSWaveMixGetDirectSoundBuffer(DWORD session, DWORD channel, uint32_t *out)
{
    (void)session;
    (void)channel;
    if (out)
        *out = 0;
    return 1;
}

DWORD WINAPI QSWaveMixStartProfile(DWORD session)
{
    (void)session;
    return 0;
}

/* the game reads five counters back (Rollcage passes 0x14 bytes): none */
DWORD WINAPI QSWaveMixStopProfile(DWORD session, DWORD *stats, DWORD size)
{
    (void)session;
    if (stats)
        memset(stats, 0, size);
    return 0;
}

/* The game restarts in place: nothing plays, its waves are gone (their memory
 * was the game's). */
void nt_qmixer_reset(void)
{
    lock();
    memset(channels, 0, sizeof channels);
    active = 0;
    unlock();
}
