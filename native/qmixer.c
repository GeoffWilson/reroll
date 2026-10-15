/* QMixer for the native build, on OpenAL Soft (docs/native/plan64.md,
 * "After the 64-bit build"): the 25 QSound QMixer calls the sound engine
 * (src/game/snd_da.c, snd_db.c) makes, each of the 32 channels an OpenAL
 * source:
 *  - waves are AL buffers (8 or 16 bit PCM, mono or stereo);
 *  - the polar position (azimuth in degrees, 0 ahead, 90 right; range;
 *    elevation in degrees, up) is a listener-relative position. Range
 *    attenuates as DirectSound 3D does, OpenAL's inverse clamped model
 *    between the distance mapping's near and far, when the channel's
 *    features include QMIX_CHANNEL_RANGE (4); elevation counts with
 *    QMIX_CHANNEL_ELEVATION (1);
 *  - the source velocity (relative to the listener, game axes: x right, y
 *    down, z ahead, units a second; the game keeps it under 283, below the
 *    speed of sound) gives Doppler with QMIX_CHANNEL_DOPPLER (2);
 *  - EAX 1.0: each channel's DirectSound buffer is a small COM object whose
 *    IKsPropertySet takes the EAX listener set (environment 0..25, volume,
 *    decay time, damping) and the buffer set (reverb mix). They drive one
 *    EFX reverb the way OpenAL Soft's own EAX layer does: the environment
 *    loads its EAX 1 preset (volume, decay, damping) over the matching
 *    EFX_REVERB_PRESET_*, volume is the late reverb gain, damping the decay
 *    HF ratio; a channel's reverb mix is the gain of its send to the reverb.
 *    The sound menu's "environmental audio" needs more than 16 channels with
 *    a property set (0x470e70); the reverb is heard while it is on (0x4b1c88).
 * Argument counts as the engine calls them. */
#include "nt.h"

#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include <AL/efx-presets.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef LONG HRESULT;

#define CHANNELS 32
#define FEATURE_ELEVATION 1
#define FEATURE_DOPPLER 2
#define FEATURE_RANGE 4
#define HEADROOM 0.6f /* a race mixes many channels (level matched to the SDL mixer) */
#define EAX_SWITCH 0x4b1c88 /* the game's "environmental audio" setting */

struct wave {
    uint32_t buffer; /* ALuint */
    uint32_t frames, rate;
};

struct com {
    void **vtable;
    int channel;
};

struct channel {
    ALuint source, filter;
    struct wave *wave;
    int paused;
    uint32_t frequency;
    float volume, azimuth, range, elevation, mix;
    float velocity[3];
    DWORD features;
};

static ALCdevice *device;
static ALCcontext *context;
static ALuint slot, reverb;
static int have_efx, active, eax_heard = -1;
static int eax_test; /* REROLL_EAX_TEST=1: every sound fully into a sewer pipe, to hear the reverb */
static struct channel channels[CHANNELS];
static float near_distance = 1.0f, far_distance = 100.0f, rolloff = 1.0f;
static struct com *buffers, *property_sets; /* CHANNELS each, in low memory */
/* every AL buffer made, so a restart in place can free them (the game's
 * wave records go with its memory) */
static ALuint *made;
static int made_count, made_size;

/* ---- EAX 1.0 -> EFX ---- */

struct eax1 {
    uint32_t environment;
    float volume, decay, damping;
};

/* the EAX 1.0 SDK's presets (eax.h EAX_PRESET_*), in environment order */
static const float eax1_presets[26][3] = {
    {0.5f, 1.493f, 0.5f},    {0.25f, 0.1f, 0.0f},     {0.417f, 0.4f, 0.666f},  {0.653f, 1.499f, 0.166f},
    {0.208f, 0.478f, 0.0f},  {0.5f, 2.309f, 0.888f},  {0.403f, 4.279f, 0.5f},  {0.5f, 3.961f, 0.5f},
    {0.5f, 2.886f, 1.304f},  {0.361f, 7.284f, 0.332f}, {0.5f, 10.0f, 0.3f},    {0.153f, 0.259f, 2.0f},
    {0.361f, 1.493f, 0.0f},  {0.444f, 2.697f, 0.638f}, {0.25f, 1.752f, 0.776f}, {0.111f, 3.145f, 0.472f},
    {0.111f, 2.767f, 0.224f}, {0.194f, 7.841f, 0.472f}, {1.0f, 1.499f, 0.5f},   {0.097f, 2.767f, 0.224f},
    {0.208f, 1.652f, 1.5f},  {0.652f, 2.886f, 0.25f},  {1.0f, 1.499f, 0.0f},   {0.875f, 8.392f, 1.388f},
    {0.139f, 17.234f, 0.666f}, {0.486f, 7.563f, 0.806f},
};

static const EFXEAXREVERBPROPERTIES efx_presets[26] = {
    EFX_REVERB_PRESET_GENERIC,     EFX_REVERB_PRESET_PADDEDCELL,      EFX_REVERB_PRESET_ROOM,
    EFX_REVERB_PRESET_BATHROOM,    EFX_REVERB_PRESET_LIVINGROOM,      EFX_REVERB_PRESET_STONEROOM,
    EFX_REVERB_PRESET_AUDITORIUM,  EFX_REVERB_PRESET_CONCERTHALL,     EFX_REVERB_PRESET_CAVE,
    EFX_REVERB_PRESET_ARENA,       EFX_REVERB_PRESET_HANGAR,          EFX_REVERB_PRESET_CARPETEDHALLWAY,
    EFX_REVERB_PRESET_HALLWAY,     EFX_REVERB_PRESET_STONECORRIDOR,   EFX_REVERB_PRESET_ALLEY,
    EFX_REVERB_PRESET_FOREST,      EFX_REVERB_PRESET_CITY,            EFX_REVERB_PRESET_MOUNTAINS,
    EFX_REVERB_PRESET_QUARRY,      EFX_REVERB_PRESET_PLAIN,           EFX_REVERB_PRESET_PARKINGLOT,
    EFX_REVERB_PRESET_SEWERPIPE,   EFX_REVERB_PRESET_UNDERWATER,      EFX_REVERB_PRESET_DRUGGED,
    EFX_REVERB_PRESET_DIZZY,       EFX_REVERB_PRESET_PSYCHOTIC,
};

/* the driver's state before the game sets any (EAX 1's defaults: generic) */
static struct eax1 eax = {0, 0.5f, 1.493f, 0.5f};

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void eax_commit(void)
{
    const EFXEAXREVERBPROPERTIES *p = &efx_presets[eax.environment];
    if (!have_efx)
        return;
    if (eax_test) {
        eax = (struct eax1){21, 1.0f, eax1_presets[21][1], eax1_presets[21][2]};
        p = &efx_presets[21];
    }
    alEffectf(reverb, AL_EAXREVERB_DENSITY, p->flDensity);
    alEffectf(reverb, AL_EAXREVERB_DIFFUSION, p->flDiffusion);
    alEffectf(reverb, AL_EAXREVERB_GAIN, p->flGain);
    alEffectf(reverb, AL_EAXREVERB_GAINHF, p->flGainHF);
    alEffectf(reverb, AL_EAXREVERB_GAINLF, p->flGainLF);
    alEffectf(reverb, AL_EAXREVERB_DECAY_TIME, clampf(eax.decay, AL_EAXREVERB_MIN_DECAY_TIME, AL_EAXREVERB_MAX_DECAY_TIME));
    alEffectf(reverb, AL_EAXREVERB_DECAY_HFRATIO,
              clampf(eax.damping, AL_EAXREVERB_MIN_DECAY_HFRATIO, AL_EAXREVERB_MAX_DECAY_HFRATIO));
    alEffectf(reverb, AL_EAXREVERB_DECAY_LFRATIO, p->flDecayLFRatio);
    alEffectf(reverb, AL_EAXREVERB_REFLECTIONS_GAIN, p->flReflectionsGain);
    alEffectf(reverb, AL_EAXREVERB_REFLECTIONS_DELAY, p->flReflectionsDelay);
    alEffectfv(reverb, AL_EAXREVERB_REFLECTIONS_PAN, p->flReflectionsPan);
    alEffectf(reverb, AL_EAXREVERB_LATE_REVERB_GAIN, clampf(eax.volume, 0.0f, 1.0f));
    alEffectf(reverb, AL_EAXREVERB_LATE_REVERB_DELAY, p->flLateReverbDelay);
    alEffectfv(reverb, AL_EAXREVERB_LATE_REVERB_PAN, p->flLateReverbPan);
    alEffectf(reverb, AL_EAXREVERB_ECHO_TIME, p->flEchoTime);
    alEffectf(reverb, AL_EAXREVERB_ECHO_DEPTH, p->flEchoDepth);
    alEffectf(reverb, AL_EAXREVERB_MODULATION_TIME, p->flModulationTime);
    alEffectf(reverb, AL_EAXREVERB_MODULATION_DEPTH, p->flModulationDepth);
    alEffectf(reverb, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, p->flAirAbsorptionGainHF);
    alEffectf(reverb, AL_EAXREVERB_HFREFERENCE, p->flHFReference);
    alEffectf(reverb, AL_EAXREVERB_LFREFERENCE, p->flLFReference);
    alEffectf(reverb, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, p->flRoomRolloffFactor);
    alEffecti(reverb, AL_EAXREVERB_DECAY_HFLIMIT, p->iDecayHFLimit);
    alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, (ALint)reverb);
}

/* the reverb is heard while the game's environmental audio setting is on */
static void eax_gate(void)
{
    int on = *(volatile uint32_t *)(uintptr_t)EAX_SWITCH != 0;
    if (!have_efx || on == eax_heard)
        return;
    eax_heard = on;
    alAuxiliaryEffectSlotf(slot, AL_EFFECTSLOT_GAIN, on ? 1.0f : 0.0f);
    nt_log("openal: environmental audio %s", on ? "on" : "off");
}

/* a channel's send to the reverb: its reverb mix (0..1) */
static void channel_send(struct channel *c)
{
    if (!have_efx)
        return;
    alFilterf(c->filter, AL_LOWPASS_GAIN, eax_test ? 1.0f : clampf(c->mix, 0.0f, 1.0f));
    alFilterf(c->filter, AL_LOWPASS_GAINHF, 1.0f);
    alSource3i(c->source, AL_AUXILIARY_SEND_FILTER, (ALint)slot, 0, (ALint)c->filter);
}

/* ---- the COM objects: IDirectSoundBuffer (QueryInterface only) and IKsPropertySet ---- */

#define E_NOINTERFACE 0x80004002u
#define E_PROP_ID_UNSUPPORTED 0x80070490u
#define E_INVALIDARG 0x80070057u

static const uint8_t iid_ks_property_set[16] = {0x30, 0xac, 0xef, 0x31, 0x5c, 0x51, 0xd0, 0x11,
                                                0xa9, 0xaa, 0x00, 0xaa, 0x00, 0x61, 0xbe, 0x93};
static const uint8_t eax_listener_set[16] = {0xc1, 0x6f, 0x4e, 0x4a, 0x41, 0xc3, 0xd1, 0x11,
                                             0xb7, 0x3a, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00};
static const uint8_t eax_buffer_set[16] = {0xc0, 0x6f, 0x4e, 0x4a, 0x41, 0xc3, 0xd1, 0x11,
                                           0xb7, 0x3a, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00};

void *nt_named_stub(const char *module, const char *name); /* loader.c */

static void **make_vtable(const char *interface, int count)
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

static LONG WINAPI com_ref(struct com *o)
{
    (void)o;
    return 1; /* they live for the whole run */
}

static HRESULT WINAPI buffer_query(struct com *o, const uint8_t *iid, uint32_t *out)
{
    if (out)
        *out = 0;
    if (!iid || !out || memcmp(iid, iid_ks_property_set, 16) || !have_efx)
        return (HRESULT)E_NOINTERFACE;
    *out = (uint32_t)(uintptr_t)&property_sets[o->channel];
    return 0;
}

static HRESULT WINAPI set_query(struct com *o, const uint8_t *iid, uint32_t *out)
{
    if (out)
        *out = 0;
    if (!iid || !out || memcmp(iid, iid_ks_property_set, 16))
        return (HRESULT)E_NOINTERFACE;
    *out = (uint32_t)(uintptr_t)o;
    return 0;
}

/* QuerySupport(set, id, *support): get and set (3) of both EAX sets */
static HRESULT WINAPI set_support(struct com *o, const uint8_t *set, DWORD id, uint32_t *support)
{
    (void)o;
    if (support)
        *support = 0;
    if (!set || !support)
        return (HRESULT)E_INVALIDARG;
    if (!memcmp(set, eax_listener_set, 16) ? id > 4 : !memcmp(set, eax_buffer_set, 16) ? id > 1 : 1)
        return (HRESULT)E_PROP_ID_UNSUPPORTED;
    *support = 3;
    return 0;
}

/* Set(set, id, instance, instance size, data, size) */
static HRESULT WINAPI set_set(struct com *o, const uint8_t *set, DWORD id, void *instance, DWORD instance_size,
                              const void *data, DWORD size)
{
    float f = 0.0f;
    (void)instance;
    (void)instance_size;
    if (!set || !data || size < 4)
        return (HRESULT)E_INVALIDARG;
    memcpy(&f, data, 4);
    if (!memcmp(set, eax_buffer_set, 16)) { /* 0 all, 1 reverb mix */
        struct channel *c = &channels[o->channel];
        if (id > 1)
            return (HRESULT)E_PROP_ID_UNSUPPORTED;
        if (getenv("REROLL_EAX_LOG"))
            nt_log("eax: channel %d mix %g", o->channel, f);
        c->mix = f < 0.0f ? 1.0f : f; /* EAX_REVERBMIX_USEDISTANCE (-1): full, the distance does the rest */
        channel_send(c);
        eax_gate();
        return 0;
    }
    if (memcmp(set, eax_listener_set, 16))
        return (HRESULT)E_PROP_ID_UNSUPPORTED;
    switch (id) {
    case 0: /* all: EAX_REVERBPROPERTIES */
        if (size < 16 || *(const uint32_t *)data > 25)
            return (HRESULT)E_INVALIDARG;
        memcpy(&eax, data, 16);
        break;
    case 1: { /* environment: its preset */
        uint32_t env = *(const uint32_t *)data;
        if (env > 25)
            return (HRESULT)E_INVALIDARG;
        if (env != eax.environment)
            nt_log("openal: EAX environment %u", env);
        eax.environment = env;
        eax.volume = eax1_presets[env][0];
        eax.decay = eax1_presets[env][1];
        eax.damping = eax1_presets[env][2];
        break;
    }
    case 2:
        eax.volume = clampf(f, 0.0f, 1.0f);
        break;
    case 3:
        eax.decay = clampf(f, 0.1f, 20.0f);
        break;
    case 4:
        eax.damping = clampf(f, 0.0f, 2.0f);
        break;
    default:
        return (HRESULT)E_PROP_ID_UNSUPPORTED;
    }
    eax_commit();
    eax_gate();
    return 0;
}

/* Get(set, id, instance, instance size, data, size, *returned) */
static HRESULT WINAPI set_get(struct com *o, const uint8_t *set, DWORD id, void *instance, DWORD instance_size,
                              void *data, DWORD size, uint32_t *returned)
{
    uint32_t n = 4;
    (void)instance;
    (void)instance_size;
    if (returned)
        *returned = 0;
    if (!set || !data || size < 4)
        return (HRESULT)E_INVALIDARG;
    if (!memcmp(set, eax_buffer_set, 16)) {
        if (id > 1)
            return (HRESULT)E_PROP_ID_UNSUPPORTED;
        memcpy(data, &channels[o->channel].mix, 4);
    } else if (!memcmp(set, eax_listener_set, 16)) {
        switch (id) {
        case 0:
            if (size < 16)
                return (HRESULT)E_INVALIDARG;
            memcpy(data, &eax, 16);
            n = 16;
            break;
        case 1:
            memcpy(data, &eax.environment, 4);
            break;
        case 2:
            memcpy(data, &eax.volume, 4);
            break;
        case 3:
            memcpy(data, &eax.decay, 4);
            break;
        case 4:
            memcpy(data, &eax.damping, 4);
            break;
        default:
            return (HRESULT)E_PROP_ID_UNSUPPORTED;
        }
    } else {
        return (HRESULT)E_PROP_ID_UNSUPPORTED;
    }
    if (returned)
        *returned = n;
    return 0;
}

static void make_objects(void)
{
    int arena = nt_lowmem_arena(0), i; /* they outlive the game's runs */
    void **bv = make_vtable("IDirectSoundBuffer(qmixer)", 21), **pv = make_vtable("IKsPropertySet", 6);
    bv[0] = (void *)buffer_query;
    bv[1] = bv[2] = (void *)com_ref;
    pv[0] = (void *)set_query;
    pv[1] = pv[2] = (void *)com_ref;
    pv[3] = (void *)set_get;
    pv[4] = (void *)set_set;
    pv[5] = (void *)set_support;
    buffers = nt_lowmem_calloc(CHANNELS, (uint32_t)sizeof *buffers);
    property_sets = nt_lowmem_calloc(CHANNELS, (uint32_t)sizeof *property_sets);
    for (i = 0; i < CHANNELS; i++) {
        buffers[i] = (struct com){bv, i};
        property_sets[i] = (struct com){pv, i};
    }
    nt_lowmem_arena(arena);
}

/* ---- channels ---- */

static int valid(DWORD ch)
{
    return ch < CHANNELS && context;
}

/* the source's place, distance rolloff and velocity from the channel's state */
static void channel_place(struct channel *c)
{
    float a = c->azimuth * (float)M_PI / 180.0f;
    float e = c->features & FEATURE_ELEVATION ? c->elevation * (float)M_PI / 180.0f : 0.0f;
    float r = c->range > 0.0f ? c->range : 0.0f;
    if (!(c->features & FEATURE_RANGE)) /* direction only, at the near distance */
        r = near_distance;
    alSource3f(c->source, AL_POSITION, r * sinf(a) * cosf(e), r * sinf(e), -r * cosf(a) * cosf(e));
    alSourcef(c->source, AL_ROLLOFF_FACTOR, c->features & FEATURE_RANGE ? rolloff : 0.0f);
    alSourcef(c->source, AL_REFERENCE_DISTANCE, near_distance);
    alSourcef(c->source, AL_MAX_DISTANCE, far_distance);
    if (c->features & FEATURE_DOPPLER)
        alSource3f(c->source, AL_VELOCITY, c->velocity[0], -c->velocity[1], -c->velocity[2]);
    else
        alSource3f(c->source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
}

static void channel_pitch(struct channel *c)
{
    float pitch = 1.0f;
    if (c->wave && c->frequency && c->wave->rate)
        pitch = (float)c->frequency / (float)c->wave->rate;
    alSourcef(c->source, AL_PITCH, clampf(pitch, 0.01f, 10.0f));
}

static void channel_stop(struct channel *c)
{
    alSourceStop(c->source);
    alSourcei(c->source, AL_BUFFER, 0);
}

static void open_device(void)
{
    ALCint attrs[] = {ALC_MAX_AUXILIARY_SENDS, 1, 0};
    int i;
    const char *name;
    device = alcOpenDevice(0);
    if (!device) {
        nt_log("qmixer: OpenAL has no output device");
        return;
    }
    context = alcCreateContext(device, attrs);
    if (!context || !alcMakeContextCurrent(context)) {
        nt_log("qmixer: no OpenAL context");
        if (context)
            alcDestroyContext(context);
        alcCloseDevice(device);
        context = 0;
        device = 0;
        return;
    }
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alListenerf(AL_GAIN, 0.0f); /* until activated */
    have_efx = alcIsExtensionPresent(device, "ALC_EXT_EFX") && !getenv("REROLL_NO_EAX");
    eax_test = getenv("REROLL_EAX_TEST") && atoi(getenv("REROLL_EAX_TEST"));
    if (have_efx) {
        alGenAuxiliaryEffectSlots(1, &slot);
        alGenEffects(1, &reverb);
        alEffecti(reverb, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
        if (alGetError() != AL_NO_ERROR) {
            nt_log("qmixer: no EFX reverb");
            have_efx = 0;
        }
    }
    for (i = 0; i < CHANNELS; i++) {
        struct channel *c = &channels[i];
        alGenSources(1, &c->source);
        alSourcei(c->source, AL_SOURCE_RELATIVE, AL_TRUE);
        if (have_efx) {
            alGenFilters(1, &c->filter);
            alFilteri(c->filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
            channel_send(c);
        }
        channel_place(c);
    }
    if (have_efx) {
        eax_commit();
        eax_gate();
    }
    make_objects();
    name = alcGetString(device, ALC_ALL_DEVICES_SPECIFIER);
    nt_log("qmixer: OpenAL Soft %s, %d sources on \"%s\"%s", alGetString(AL_VERSION), CHANNELS, name ? name : "?",
           have_efx ? ", EAX reverb by EFX" : "");
}

/* QSWaveMixInitEx(config): the session (one; a static, so a low address) */
void *WINAPI QSWaveMixInitEx(void *config)
{
    (void)config;
    if (!context)
        open_device();
    return context ? (void *)&channels : 0;
}

DWORD WINAPI QSWaveMixActivate(DWORD session, DWORD on)
{
    (void)session;
    active = on != 0;
    if (context)
        alListenerf(AL_GAIN, active ? HEADROOM : 0.0f);
    return 0;
}

DWORD WINAPI QSWaveMixCloseSession(DWORD session)
{
    int i;
    (void)session;
    active = 0;
    if (!context)
        return 0;
    alListenerf(AL_GAIN, 0.0f);
    for (i = 0; i < CHANNELS; i++) {
        channel_stop(&channels[i]);
        channels[i].wave = 0;
        channels[i].paused = 0;
    }
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
    uint32_t bytes = params[2], frame;
    uint16_t tag, chans, bits;
    ALuint buffer = 0;
    ALenum al_format;
    struct wave *w;
    (void)session;
    (void)flags;
    if (!format || !data || !context)
        return 0;
    tag = *(const uint16_t *)format;
    chans = *(const uint16_t *)(format + 2);
    bits = *(const uint16_t *)(format + 0xe);
    if (tag != 1 || (chans != 1 && chans != 2) || (bits != 8 && bits != 16)) {
        nt_log("qmixer: wave format %u, %u channels, %u bits not supported", tag, chans, bits);
        return 0;
    }
    al_format = chans == 1 ? (bits == 8 ? AL_FORMAT_MONO8 : AL_FORMAT_MONO16)
                           : (bits == 8 ? AL_FORMAT_STEREO8 : AL_FORMAT_STEREO16);
    frame = chans * (bits / 8u);
    bytes -= bytes % frame;
    w = nt_lowmem_calloc(1, (uint32_t)sizeof *w); /* the game keeps it in 32 bits */
    w->rate = *(const uint32_t *)(format + 4);
    w->frames = bytes / frame;
    alGenBuffers(1, &buffer);
    if (bytes)
        alBufferData(buffer, al_format, data, (ALsizei)bytes, (ALsizei)w->rate);
    w->buffer = buffer;
    if (made_count == made_size) {
        made_size = made_size ? made_size * 2 : 256;
        made = realloc(made, (size_t)made_size * sizeof *made);
    }
    made[made_count++] = buffer;
    return w;
}

static void forget(ALuint buffer)
{
    int i;
    for (i = 0; i < made_count; i++)
        if (made[i] == buffer) {
            made[i] = made[--made_count];
            return;
        }
}

DWORD WINAPI QSWaveMixFreeWave(DWORD session, struct wave *w)
{
    int i;
    (void)session;
    if (!w || !context)
        return 0;
    for (i = 0; i < CHANNELS; i++)
        if (channels[i].wave == w) {
            channel_stop(&channels[i]);
            channels[i].wave = 0;
        }
    alDeleteBuffers(1, &w->buffer);
    forget(w->buffer);
    nt_lowmem_free(w);
    return 0;
}

/* PlayEx(session, channel, flags, wave, loops (-1 forever), params) */
DWORD WINAPI QSWaveMixPlayEx(DWORD session, DWORD channel, DWORD flags, struct wave *w, DWORD loops, DWORD params)
{
    struct channel *c;
    (void)session;
    (void)flags;
    (void)params;
    if (!valid(channel) || !w)
        return 1;
    c = &channels[channel];
    channel_stop(c);
    c->wave = w;
    if (!w->frames)
        return 0;
    if (loops == 0xffffffffu || loops == 0) {
        alSourcei(c->source, AL_BUFFER, (ALint)w->buffer);
        alSourcei(c->source, AL_LOOPING, loops ? AL_TRUE : AL_FALSE);
    } else { /* a few repeats: the buffer queued that many more times */
        ALuint queue[64];
        uint32_t n = loops + 1 > 64 ? 64 : loops + 1, i;
        for (i = 0; i < n; i++)
            queue[i] = w->buffer;
        alSourcei(c->source, AL_LOOPING, AL_FALSE);
        alSourceQueueBuffers(c->source, (ALsizei)n, queue);
    }
    channel_pitch(c);
    alSourcePlay(c->source);
    if (c->paused)
        alSourcePause(c->source);
    return 0;
}

DWORD WINAPI QSWaveMixPauseChannel(DWORD session, DWORD channel, DWORD flags)
{
    ALint state;
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    channels[channel].paused = 1;
    alGetSourcei(channels[channel].source, AL_SOURCE_STATE, &state);
    if (state == AL_PLAYING)
        alSourcePause(channels[channel].source);
    return 0;
}

DWORD WINAPI QSWaveMixRestartChannel(DWORD session, DWORD channel, DWORD flags)
{
    ALint state;
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    channels[channel].paused = 0;
    alGetSourcei(channels[channel].source, AL_SOURCE_STATE, &state);
    if (state == AL_PAUSED)
        alSourcePlay(channels[channel].source);
    return 0;
}

DWORD WINAPI QSWaveMixFlushChannel(DWORD session, DWORD channel, DWORD flags)
{
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    channel_stop(&channels[channel]);
    return 0;
}

DWORD WINAPI QSWaveMixIsChannelDone(DWORD session, DWORD channel)
{
    ALint state;
    (void)session;
    if (!valid(channel))
        return 1;
    alGetSourcei(channels[channel].source, AL_SOURCE_STATE, &state);
    return state != AL_PLAYING && state != AL_PAUSED;
}

DWORD WINAPI QSWaveMixSetVolume(DWORD session, DWORD channel, DWORD flags, DWORD volume)
{
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    channels[channel].volume = (float)(volume > 32767 ? 32767 : volume) / 32767.0f;
    alSourcef(channels[channel].source, AL_GAIN, channels[channel].volume);
    return 0;
}

DWORD WINAPI QSWaveMixSetFrequency(DWORD session, DWORD channel, DWORD flags, DWORD frequency)
{
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    channels[channel].frequency = frequency;
    channel_pitch(&channels[channel]);
    return 0;
}

/* SetPolarPosition(session, channel, flags, QSPOLAR {azimuth, range, elevation}) */
DWORD WINAPI QSWaveMixSetPolarPosition(DWORD session, DWORD channel, DWORD flags, const float *polar)
{
    struct channel *c;
    (void)session;
    (void)flags;
    if (!valid(channel) || !polar)
        return 1;
    c = &channels[channel];
    c->azimuth = polar[0];
    c->range = polar[1];
    c->elevation = polar[2];
    channel_place(c);
    eax_gate();
    return 0;
}

/* SetSourceVelocity(session, channel, flags, {x, y, z}) */
DWORD WINAPI QSWaveMixSetSourceVelocity(DWORD session, DWORD channel, DWORD flags, const float *velocity)
{
    struct channel *c;
    (void)session;
    (void)flags;
    if (!valid(channel) || !velocity)
        return 1;
    c = &channels[channel];
    memcpy(c->velocity, velocity, sizeof c->velocity);
    channel_place(c);
    return 0;
}

/* SetDistanceMapping(session, channel, flags, {size, near, far, scale}) */
DWORD WINAPI QSWaveMixSetDistanceMapping(DWORD session, DWORD channel, DWORD flags, const float *distances)
{
    int i;
    (void)session;
    (void)channel;
    (void)flags;
    if (distances && distances[1] > 0.0f && distances[2] > distances[1]) {
        near_distance = distances[1];
        far_distance = distances[2];
        for (i = 0; context && i < CHANNELS; i++)
            channel_place(&channels[i]);
    }
    return 0;
}

/* SetListenerRolloff(session, rolloff (float bits), flags) */
DWORD WINAPI QSWaveMixSetListenerRolloff(DWORD session, DWORD value, DWORD flags)
{
    float r;
    int i;
    (void)session;
    (void)flags;
    memcpy(&r, &value, 4);
    if (r >= 0.0f && r < 100.0f) {
        rolloff = r;
        for (i = 0; context && i < CHANNELS; i++)
            channel_place(&channels[i]);
    }
    return 0;
}

/* EnableChannel(session, channel, flags, features) */
DWORD WINAPI QSWaveMixEnableChannel(DWORD session, DWORD channel, DWORD flags, DWORD features)
{
    static DWORD logged = 0xffffffffu;
    (void)session;
    (void)flags;
    if (!valid(channel))
        return 1;
    if (features != logged)
        nt_log("qmixer: channel features 0x%x", (unsigned)features);
    logged = features;
    channels[channel].features = features;
    channel_place(&channels[channel]);
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

/* the output's speakers are OpenAL Soft's to choose (stereo, surround or
 * HRTF on headphones) */
DWORD WINAPI QSWaveMixSetSpeakerPlacement(DWORD session, DWORD placement)
{
    (void)session;
    (void)placement;
    return 0;
}

/* no DirectSound object behind this mixer (out: 4-byte slots in game memory) */
DWORD WINAPI QSWaveMixGetDirectSound(DWORD session, uint32_t *out)
{
    (void)session;
    if (out)
        *out = 0;
    return 1;
}

/* a channel's "DirectSound buffer": only its EAX property set is reachable */
DWORD WINAPI QSWaveMixGetDirectSoundBuffer(DWORD session, DWORD channel, uint32_t *out)
{
    (void)session;
    if (out)
        *out = 0;
    if (!valid(channel) || !out || !have_efx)
        return 1;
    *out = (uint32_t)(uintptr_t)&buffers[channel];
    return 0;
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

/* The game restarts in place: nothing plays, its waves are gone (their
 * records were in its memory), so are their AL buffers. */
void nt_qmixer_reset(void)
{
    int i;
    active = 0;
    if (!context)
        return;
    alListenerf(AL_GAIN, 0.0f);
    for (i = 0; i < CHANNELS; i++) {
        channel_stop(&channels[i]);
        channels[i].wave = 0;
        channels[i].paused = 0;
        channels[i].mix = 0.0f;
        channel_send(&channels[i]);
    }
    if (made_count)
        alDeleteBuffers(made_count, made);
    made_count = 0;
    eax = (struct eax1){0, 0.5f, 1.493f, 0.5f};
    eax_commit();
}
