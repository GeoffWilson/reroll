/* DSOUND for the native build: just what the movie player (src/game/
 * orig_pv.c, 0x493360 / 0x4935e0 / 0x4937e0) needs to stream a movie's
 * sound. (The game's sound effects go through QMixer, native/qmixer.c.)
 *
 * The player makes a DirectSound, a primary buffer (whose size it then uses
 * for its own buffer) and one looping secondary buffer of 16-bit PCM; a
 * 10 ms timer copies each decoded block in behind the play cursor (Lock /
 * Unlock) and the video waits for the blocks written. A secondary buffer is
 * a ring in low memory played through an SDL audio stream: SDL's callback
 * takes the bytes at the play cursor, which is what GetCurrentPosition
 * reports. The primary reports PRIMARY_BYTES: a few of the movies' blocks
 * (at most 2944 bytes, 33 ms), so the picture is at most ~90 ms ahead of
 * its sound. Imported by ordinal 1. */
#include "nt.h"

#include <SDL3/SDL.h>
#include <stdio.h>

typedef LONG HRESULT;
#define DS_OK 0
#define DSERR_INVALIDPARAM 0x80070057u
#define DSERR_NOINTERFACE 0x80004002u
#define DSERR_GENERIC 0x80004005u
#define DSBCAPS_PRIMARYBUFFER 1
#define DSBPLAY_LOOPING 1
#define PRIMARY_BYTES 0x2000

void *nt_named_stub(const char *module, const char *name); /* loader.c */

struct ds {
    void **vtable;
    LONG refs;
};

struct buffer {
    void **vtable;
    LONG refs;
    int primary;
    uint8_t *data;
    uint32_t bytes, rate, frequency;
    uint16_t channels, bits;
    volatile uint32_t play; /* the play cursor, bytes */
    volatile int playing, looping;
    SDL_AudioStream *stream;
};

static void **ds_vtable, **buffer_vtable;
static struct ds the_ds;

#define OUT(slot, p) (*(slot) = (uint32_t)(uintptr_t)(p))

/* ---- buffers ---- */

/* SDL wants more: the bytes at the play cursor, round the ring */
static void SDLCALL feed(void *user, SDL_AudioStream *s, int needed, int total)
{
    struct buffer *b = user;
    uint32_t frame = (uint32_t)b->channels * (b->bits / 8u);
    (void)total;
    if (!b->playing || !b->bytes || needed <= 0)
        return;
    needed -= needed % (int)frame;
    while (needed > 0 && b->playing) {
        uint32_t pos = b->play, n = b->bytes - pos;
        if (n > (uint32_t)needed)
            n = (uint32_t)needed;
        SDL_PutAudioStreamData(s, b->data + pos, (int)n);
        pos += n;
        if (pos >= b->bytes) {
            pos = 0;
            if (!b->looping)
                b->playing = 0;
        }
        b->play = pos;
        needed -= (int)n;
    }
}

static HRESULT WINAPI buf_query(struct buffer *b, const void *iid, uint32_t *out)
{
    (void)b;
    (void)iid;
    if (out)
        *out = 0;
    return (HRESULT)DSERR_NOINTERFACE;
}

static LONG WINAPI buf_add_ref(struct buffer *b)
{
    return ++b->refs;
}

static LONG WINAPI buf_release(struct buffer *b)
{
    int arena;
    if (--b->refs > 0)
        return b->refs;
    b->playing = 0;
    if (b->stream)
        SDL_DestroyAudioStream(b->stream); /* waits for a running callback */
    arena = nt_lowmem_arena(0);
    if (b->data)
        nt_lowmem_free(b->data);
    nt_lowmem_free(b);
    nt_lowmem_arena(arena);
    return 0;
}

/* GetCaps(DSBCAPS: size, flags, buffer bytes, ...) */
static HRESULT WINAPI buf_get_caps(struct buffer *b, DWORD *caps)
{
    if (!caps)
        return (HRESULT)DSERR_INVALIDPARAM;
    caps[1] = b->primary ? DSBCAPS_PRIMARYBUFFER : 0xe0;
    caps[2] = b->bytes;
    return DS_OK;
}

static HRESULT WINAPI buf_get_position(struct buffer *b, uint32_t *play, uint32_t *write)
{
    uint32_t p = b->primary ? 0 : b->play;
    if (play)
        *play = p;
    if (write)
        *write = p;
    return DS_OK;
}

/* Lock(offset, bytes, *p1, *n1, *p2, *n2, flags): round the ring */
static HRESULT WINAPI buf_lock(struct buffer *b, DWORD offset, DWORD bytes, uint32_t *p1, uint32_t *n1, uint32_t *p2,
                               uint32_t *n2, DWORD flags)
{
    uint32_t first;
    (void)flags;
    if (!b->data || offset >= b->bytes || !p1 || !n1)
        return (HRESULT)DSERR_INVALIDPARAM;
    if (bytes > b->bytes)
        bytes = b->bytes;
    first = b->bytes - offset < bytes ? b->bytes - offset : bytes;
    OUT(p1, b->data + offset);
    *n1 = first;
    if (p2)
        OUT(p2, first < bytes ? b->data : 0);
    if (n2)
        *n2 = bytes - first;
    return DS_OK;
}

static HRESULT WINAPI buf_unlock(struct buffer *b, void *p1, DWORD n1, void *p2, DWORD n2)
{
    (void)b;
    (void)p1;
    (void)n1;
    (void)p2;
    (void)n2;
    return DS_OK;
}

static HRESULT WINAPI buf_play(struct buffer *b, DWORD reserved, DWORD priority, DWORD flags)
{
    (void)reserved;
    (void)priority;
    if (b->primary)
        return DS_OK;
    if (!b->stream) {
        SDL_AudioSpec spec = {b->bits == 8 ? SDL_AUDIO_U8 : SDL_AUDIO_S16, b->channels, (int)b->rate};
        b->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, b);
        if (!b->stream) {
            nt_log("dsound: no audio device (%s)", SDL_GetError());
            return (HRESULT)DSERR_GENERIC;
        }
        if (b->frequency && b->frequency != b->rate)
            SDL_SetAudioStreamFrequencyRatio(b->stream, (float)b->frequency / (float)b->rate);
    }
    b->looping = (flags & DSBPLAY_LOOPING) != 0;
    b->playing = 1;
    SDL_ResumeAudioStreamDevice(b->stream);
    return DS_OK;
}

static HRESULT WINAPI buf_stop(struct buffer *b)
{
    b->playing = 0;
    return DS_OK;
}

/* SetFormat(WAVEFORMATEX): the primary's, kept for nothing */
static HRESULT WINAPI buf_set_format(struct buffer *b, const uint8_t *wfx)
{
    (void)b;
    (void)wfx;
    return DS_OK;
}

static HRESULT WINAPI buf_set_frequency(struct buffer *b, DWORD frequency)
{
    b->frequency = frequency;
    if (b->stream && frequency && b->rate)
        SDL_SetAudioStreamFrequencyRatio(b->stream, (float)frequency / (float)b->rate);
    return DS_OK;
}

static HRESULT WINAPI buf_ok1(struct buffer *b, DWORD a)
{
    (void)b;
    (void)a;
    return DS_OK;
}

/* ---- IDirectSound ---- */

static HRESULT WINAPI ds_query(struct ds *d, const void *iid, uint32_t *out)
{
    (void)iid;
    OUT(out, d);
    d->refs++;
    return DS_OK;
}

static LONG WINAPI ds_add_ref(struct ds *d)
{
    return ++d->refs;
}

static LONG WINAPI ds_release(struct ds *d)
{
    return --d->refs > 0 ? d->refs : 0;
}

/* CreateSoundBuffer(DSBUFFERDESC: size, flags, bytes, reserved, WAVEFORMATEX *) */
static HRESULT WINAPI ds_create_buffer(struct ds *d, const DWORD *desc, uint32_t *out, void *outer)
{
    struct buffer *b;
    int arena;
    (void)d;
    (void)outer;
    if (!desc || !out)
        return (HRESULT)DSERR_INVALIDPARAM;
    *out = 0;
    arena = nt_lowmem_arena(0); /* ours: released by the player, and not lost with a restart in place */
    b = nt_lowmem_calloc(1, (uint32_t)sizeof *b);
    b->vtable = buffer_vtable;
    b->refs = 1;
    if (desc[1] & DSBCAPS_PRIMARYBUFFER) {
        b->primary = 1;
        b->bytes = PRIMARY_BYTES;
    } else {
        const uint8_t *wfx = (const uint8_t *)(uintptr_t)desc[4];
        if (!wfx || *(const uint16_t *)wfx != 1 || !desc[2]) {
            nt_lowmem_free(b);
            nt_lowmem_arena(arena);
            return (HRESULT)DSERR_INVALIDPARAM;
        }
        b->channels = *(const uint16_t *)(wfx + 2);
        b->rate = *(const uint32_t *)(wfx + 4);
        b->bits = *(const uint16_t *)(wfx + 0xe);
        b->frequency = b->rate;
        b->bytes = desc[2] - desc[2] % ((uint32_t)b->channels * (b->bits / 8u));
        b->data = nt_lowmem_calloc(1, b->bytes);
        nt_log("dsound: %u-byte buffer, %u Hz, %u channels, %u bits", b->bytes, b->rate, b->channels, b->bits);
    }
    nt_lowmem_arena(arena);
    OUT(out, b);
    return DS_OK;
}

/* GetCaps(DSCAPS: size, flags, ...): stereo and mono, 8 and 16 bits, any rate */
static HRESULT WINAPI ds_get_caps(struct ds *d, DWORD *caps)
{
    (void)d;
    if (!caps)
        return (HRESULT)DSERR_INVALIDPARAM;
    caps[1] = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20; /* PRIMARYMONO / STEREO / 8BIT / 16BIT, CONTINUOUSRATE */
    return DS_OK;
}

static HRESULT WINAPI ds_ok2(struct ds *d, DWORD a, DWORD b)
{
    (void)d;
    (void)a;
    (void)b;
    return DS_OK;
}

static void **stubs(const char *interface, int count)
{
    void **v = nt_lowmem_calloc((uint32_t)count, (uint32_t)sizeof *v);
    int i;
    for (i = 0; i < count; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s method %d", interface, i);
        v[i] = nt_named_stub("com", name);
    }
    return v;
}

/* out: a 4-byte slot in game memory */
LONG WINAPI DirectSoundCreate(const void *guid, uint32_t *out, void *outer)
{
    (void)guid;
    (void)outer;
    if (!out)
        return (LONG)DSERR_INVALIDPARAM;
    if (!ds_vtable) {
        int arena = nt_lowmem_arena(0); /* the tables outlive the game's runs */
        ds_vtable = stubs("IDirectSound", 11);
        ds_vtable[0] = (void *)ds_query;
        ds_vtable[1] = (void *)ds_add_ref;
        ds_vtable[2] = (void *)ds_release;
        ds_vtable[3] = (void *)ds_create_buffer;
        ds_vtable[4] = (void *)ds_get_caps;
        ds_vtable[6] = (void *)ds_ok2; /* SetCooperativeLevel */
        buffer_vtable = stubs("IDirectSoundBuffer", 21);
        buffer_vtable[0] = (void *)buf_query;
        buffer_vtable[1] = (void *)buf_add_ref;
        buffer_vtable[2] = (void *)buf_release;
        buffer_vtable[3] = (void *)buf_get_caps;
        buffer_vtable[4] = (void *)buf_get_position;
        buffer_vtable[11] = (void *)buf_lock;
        buffer_vtable[12] = (void *)buf_play;
        buffer_vtable[14] = (void *)buf_set_format;
        buffer_vtable[15] = (void *)buf_ok1; /* SetVolume */
        buffer_vtable[16] = (void *)buf_ok1; /* SetPan */
        buffer_vtable[17] = (void *)buf_set_frequency;
        buffer_vtable[18] = (void *)buf_stop;
        buffer_vtable[19] = (void *)buf_unlock;
        the_ds.vtable = ds_vtable;
        nt_lowmem_arena(arena);
    }
    the_ds.refs++;
    OUT(out, &the_ds);
    return DS_OK;
}
