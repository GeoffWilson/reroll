/* DINPUT for the native build: the keyboard (a 256-byte DIK_ table from
 * SDL's key state) and SDL3 gamepads as DirectInput joysticks, laid out as
 * Windows (and Wine) show an XInput pad to DirectInput, which is what the
 * game's control settings were made with:
 *   X / Y left stick, Z = left trigger minus right trigger (one axis,
 *   accelerate is negative Z), Rx / Ry right stick, buttons A B X Y LB RB
 *   Back Start LS RS, the d-pad as POV hat 0.
 * Pads with rumble report force feedback, and the game's DirectInput effects
 * are played on the two rumble motors (see "force feedback" below).
 *
 * Hot-plugging: the game finds its joysticks once, at start-up, so they are
 * slots that pads come and go from. The pads connected at start fill the
 * first slots; with none there is still one empty slot (unless
 * REROLL_NO_PADS), so the game sets up a joystick (and its rumble effects)
 * for a pad switched on later. A slot whose pad disconnects reads as
 * centred with nothing pressed until a pad (the same or another) connects
 * and takes the first empty slot. */
#include "keys.h"
#include "rumble.h"
#include "nt.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef LONG HRESULT;
#define DI_OK 0
#define DI_NOEFFECT 1
#define DIERR_UNSUPPORTED 0x80004001u
#define DIERR_INVALIDPARAM 0x80070057u
#define DIERR_DEVICENOTREG 0x80040154u
#define DIERR_NOINTERFACE 0x80004002u

void *nt_named_stub(const char *module, const char *name);
extern uint8_t nt_injected_keys[SDL_SCANCODE_COUNT]; /* user32.c: scripted keys */

/* GUIDs (first dword enough to tell them apart here) */
#define GUID_SYS_KEYBOARD 0x6f1d2b61u
#define IID_DEVICE2A 0x5944e682u
#define IID_DEVICEA 0x5944e680u
#define PAD_GUID 0x52455250u /* ours: "PRER" + index in byte 4 */

static const uint8_t guid_tail[8] = {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00};
static void make_guid(uint8_t out[16], uint32_t first, uint16_t second, uint16_t third)
{
    memcpy(out, &first, 4);
    memcpy(out + 4, &second, 2);
    memcpy(out + 6, &third, 2);
    memcpy(out + 8, guid_tail, 8);
}

#define MAX_PADS 4

#define MAX_EFFECTS 8

struct effect;

struct device {
    void **vtable;
    LONG refs;
    int pad;               /* -1 keyboard, else the gamepad index */
    SDL_Gamepad *gamepad;
    int32_t min[8], max[8]; /* axis ranges, by DIJOYSTATE2 offset / 4 */
    struct effect *effects[MAX_EFFECTS];
    DWORD ff_gain;          /* DIPROP_FFGAIN, 0..10000 */
    uint16_t low, high;     /* the motors as last sent */
    uint64_t sent_at;
    int logged_no_rumble;
    uint16_t pulse_low, pulse_high; /* nt_rumble_pulse, until pulse_until */
    uint64_t pulse_until;
};

struct dinput {
    void **vtable;
    LONG refs;
};

static void **dinput_vtable, **device_vtable;
static struct dinput the_dinput;
static SDL_Gamepad *slot_pads[MAX_PADS]; /* 0: no pad in the slot now */
static int pad_count = -1;               /* slots */
static uint64_t scanned_at;

/* puts SDL gamepad `id` in the first empty slot unless it has one */
static void attach(SDL_JoystickID id)
{
    int i;
    for (i = 0; i < pad_count; i++)
        if (slot_pads[i] && SDL_GetGamepadID(slot_pads[i]) == id)
            return;
    for (i = 0; i < pad_count; i++)
        if (!slot_pads[i]) {
            slot_pads[i] = SDL_OpenGamepad(id);
            if (slot_pads[i])
                nt_log("dinput: pad %d is %s", i, SDL_GetGamepadName(slot_pads[i]));
            return;
        }
}

static void find_pads(void)
{
    int n = 0, i;
    SDL_JoystickID *ids;
    if (pad_count >= 0)
        return;
    pad_count = 0;
    if (getenv("REROLL_NO_PADS")) { /* headless tests: leave the user's pads alone */
        nt_log("dinput: no gamepads (REROLL_NO_PADS)");
        return;
    }
    ids = SDL_GetGamepads(&n);
    pad_count = n < 1 ? 1 : n > MAX_PADS ? MAX_PADS : n;
    for (i = 0; ids && i < n; i++)
        attach(ids[i]);
    SDL_free(ids);
    nt_log("dinput: %d gamepad(s), %d slot(s)", n, pad_count);
    scanned_at = SDL_GetTicks();
}

/* for the movies' skip (native/user32.c): the pads opened before the game's
 * DirectInput start-up, which comes after the start-up movies */
void nt_pads_open(void)
{
    find_pads();
}

/* pads that went away leave their slots; new ones take empty slots (looked
 * for twice a second while a slot is empty) */
static void pads_refresh(void)
{
    uint64_t now = SDL_GetTicks();
    int i, empty = 0;
    for (i = 0; i < pad_count; i++) {
        if (slot_pads[i] && !SDL_GamepadConnected(slot_pads[i])) {
            nt_log("dinput: pad %d disconnected", i);
            SDL_CloseGamepad(slot_pads[i]);
            slot_pads[i] = 0;
        }
        empty |= !slot_pads[i];
    }
    if (empty && now - scanned_at >= 500) {
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);
        scanned_at = now;
        for (i = 0; ids && i < n; i++)
            attach(ids[i]);
        SDL_free(ids);
    }
}

/* Objects are low memory; out-parameters are 4-byte slots in game memory. */
#define OUT(slot, p) (*(slot) = (uint32_t)(uintptr_t)(p))

/* ---- IDirectInputDevice2A ---- */

static HRESULT WINAPI dev_query(struct device *d, const void *iid, uint32_t *out)
{
    uint32_t first = iid ? *(const uint32_t *)iid : 0;
    if (first == IID_DEVICE2A || first == IID_DEVICEA) {
        OUT(out, d);
        d->refs++;
        return DI_OK;
    }
    *out = 0;
    return (HRESULT)DIERR_NOINTERFACE;
}

static LONG WINAPI dev_add_ref(struct device *d)
{
    return ++d->refs;
}

static LONG WINAPI dev_release(struct device *d)
{
    return --d->refs > 0 ? d->refs : 0;
}

/* the device's slot's pad now (0 while the slot is empty) */
static void open_pad(struct device *d)
{
    SDL_Gamepad *g;
    if (d->pad < 0 || d->pad >= pad_count)
        return;
    pads_refresh();
    g = slot_pads[d->pad];
    if (g != d->gamepad) { /* a new pad: its motors are off */
        d->gamepad = g;
        d->low = d->high = 0;
        d->logged_no_rumble = 0;
    }
}

/* an empty slot reports force feedback too: the game makes its effects only
 * at start-up, and they play once a pad that rumbles is there */
static int pad_rumbles(struct device *d)
{
    if (getenv("REROLL_NO_RUMBLE"))
        return 0;
    open_pad(d);
    return !d->gamepad || SDL_GetBooleanProperty(SDL_GetGamepadProperties(d->gamepad),
                                                 SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
}

/* DIDEVCAPS: size, flags, dev type, axes, buttons, povs, ... */
static HRESULT WINAPI dev_get_capabilities(struct device *d, DWORD *caps)
{
    DWORD size = caps[0];
    memset(caps, 0, size);
    caps[0] = size;
    caps[1] = 1 | 4; /* ATTACHED | POLLEDDEVICE */
    if (d->pad < 0) {
        caps[2] = 0x00000403; /* keyboard, PC enhanced */
        caps[4] = 128;
    } else {
        caps[2] = 0x00010214; /* HID joystick, gamepad */
        caps[3] = 5;
        caps[4] = 10;
        caps[5] = 1;
        if (pad_rumbles(d))
            caps[1] |= 0x100; /* DIDC_FORCEFEEDBACK: the game then creates its effects */
        else if (d->gamepad && !d->logged_no_rumble++)
            nt_log("dinput: pad %d has no rumble", d->pad);
    }
    return DI_OK;
}

/* The enumeration callbacks are game functions (input_enum_joystick_object,
 * input_enum_joystick in src/game), declared with uint32_t or pointer
 * arguments: called with pointer-width arguments (zero-extended), right for
 * either; the records are on the game thread's (low) stack. */

/* DIDEVICEOBJECTINSTANCEA: size, guidType +4, dwOfs +0x14, dwType +0x18,
 * dwFlags +0x1c, name +0x20 (260) ... (0x13c bytes in DirectX 5) */
static HRESULT WINAPI dev_enum_objects(struct device *d, BOOL(WINAPI *callback)(const uint8_t *object, void *ctx),
                                       void *ctx, DWORD flags)
{
    static const struct {
        uint32_t guid_first;
        uint32_t offset, type;
        const char *name;
    } objects[] = {
        {0xa36d02e0u, 0x00, 0x002 | 0 << 8, "X Axis"},
        {0xa36d02e1u, 0x04, 0x002 | 1 << 8, "Y Axis"},
        {0xa36d02e2u, 0x08, 0x002 | 2 << 8, "Z Axis"},
        {0xa36d02f4u, 0x0c, 0x002 | 3 << 8, "X Rotation"},
        {0xa36d02f5u, 0x10, 0x002 | 4 << 8, "Y Rotation"},
        {0xa36d02f2u, 0x20, 0x010 | 0 << 8, "Hat Switch"},
    };
    uint8_t object[0x13c];
    size_t i;
    int b;
    (void)flags;
    if (d->pad < 0)
        return DI_OK;
    for (i = 0; i < sizeof objects / sizeof *objects; i++) {
        memset(object, 0, sizeof object);
        *(DWORD *)object = sizeof object;
        make_guid(object + 4, objects[i].guid_first, 0xc9f3, 0x11cf);
        *(DWORD *)(object + 0x14) = objects[i].offset;
        *(DWORD *)(object + 0x18) = objects[i].type;
        *(DWORD *)(object + 0x1c) = 0;
        snprintf((char *)object + 0x20, 260, "%s", objects[i].name);
        if (!callback(object, ctx))
            return DI_OK;
    }
    for (b = 0; b < 10; b++) {
        memset(object, 0, sizeof object);
        *(DWORD *)object = sizeof object;
        make_guid(object + 4, 0xa36d02f0u, 0xc9f3, 0x11cf);
        *(DWORD *)(object + 0x14) = 0x30 + (DWORD)b;
        *(DWORD *)(object + 0x18) = 0x004 | (DWORD)b << 8; /* PSHBUTTON */
        snprintf((char *)object + 0x20, 260, "Button %d", b);
        if (!callback(object, ctx))
            return DI_OK;
    }
    return DI_OK;
}

/* DIPROPHEADER: size, header size, obj, how (0 device, 1 by offset) */
#define DIPROP_RANGE 4
#define DIPROP_DEADZONE 5
#define DIPROP_SATURATION 6
#define DIPROP_FFGAIN 7

static int axis_index(const DWORD *header, int *all)
{
    *all = header[3] == 0;
    if (header[3] == 1 && header[2] < 0x20)
        return (int)header[2] / 4;
    return -1;
}

static HRESULT WINAPI dev_get_property(struct device *d, const void *prop, DWORD *header)
{
    int all, i = axis_index(header, &all);
    if ((uintptr_t)prop == DIPROP_RANGE) {
        if (i < 0)
            i = 0;
        header[4] = (DWORD)d->min[i];
        header[5] = (DWORD)d->max[i];
        return DI_OK;
    }
    if ((uintptr_t)prop == DIPROP_DEADZONE || (uintptr_t)prop == DIPROP_SATURATION) {
        header[4] = (uintptr_t)prop == DIPROP_DEADZONE ? 0 : 10000;
        return DI_OK;
    }
    if ((uintptr_t)prop == DIPROP_FFGAIN) {
        header[4] = d->ff_gain;
        return DI_OK;
    }
    return (HRESULT)DIERR_UNSUPPORTED;
}

static HRESULT WINAPI dev_set_property(struct device *d, const void *prop, const DWORD *header)
{
    int all, i = axis_index(header, &all), j;
    if ((uintptr_t)prop == DIPROP_RANGE) {
        for (j = 0; j < 8; j++)
            if (all || j == i) {
                d->min[j] = (int32_t)header[4];
                d->max[j] = (int32_t)header[5];
            }
        return DI_OK;
    }
    if ((uintptr_t)prop == DIPROP_FFGAIN)
        d->ff_gain = header[4] > 10000 ? 10000 : header[4];
    return DI_OK; /* dead zone, saturation, buffer size: SDL already filters */
}

static HRESULT WINAPI dev_acquire(struct device *d)
{
    open_pad(d);
    return DI_OK;
}

/* ---- force feedback ----
 *
 * The game (0x4892b0) makes six effects per pad that reports
 * DIDC_FORCEFEEDBACK, for a wheel or a force-feedback stick, all of
 * infinite duration unless set otherwise:
 *   constant force  the strongest hit this frame, pushing left or right
 *                   (0x489980: magnitude = hit x 250, up to 10000);
 *   sine            the engine, period 100 ms at idle down to ~16 ms at
 *                   full revs, magnitude 1500..2150 (0x489cd0);
 *   square          a 100 ms pulse of 2000 (0x489e10);
 *   spring, friction, inertia: the wheel's centring and weight (0x489ab0,
 *                   0x489bc0), which a rumble pad has nothing to play with.
 * On a pad's two motors: the hits and pulses on the low-frequency (heavy)
 * motor. The engine is left out: a buzz all race long. The game leaves the
 * constant force at its last value (it skips changes under 3600), so a hit
 * is played as a thump that fades over FADE_MS from each change. Boost pads
 * add theirs through nt_rumble_pulse (src/main.c). */

#define DIEP_DURATION 0x1
#define DIEP_GAIN 0x4
#define DIEP_TYPESPECIFICPARAMS 0x100
#define DIEP_START 0x20000000
#define DI_INFINITE 0xffffffffu
#define FADE_MS 250
#define RUMBLE_MS 200 /* each request's length; renewed while the effects play */

enum effect_kind { FX_CONSTANT, FX_PERIODIC, FX_OTHER };

struct effect {
    void **vtable;
    LONG refs;
    struct device *device;
    uint8_t guid[16];
    enum effect_kind kind;
    DWORD duration, gain;   /* microseconds (DI_INFINITE), 0..10000 */
    int32_t magnitude;      /* constant: -10000..10000; periodic: 0..10000 */
    DWORD period;           /* periodic, microseconds */
    int playing;
    uint64_t started, changed; /* SDL ticks, ms */
};

static void **effect_vtable;

static int effect_playing(const struct effect *e, uint64_t now)
{
    return e->playing && (e->duration == DI_INFINITE || now - e->started < e->duration / 1000);
}

/* Mix the playing effects into the motors and send them when they change,
 * renewing a running rumble before it runs out. */
static void rumble_update(struct device *d)
{
    uint64_t now = SDL_GetTicks();
    double low = 0, high = 0;
    int i;
    if (!d->gamepad)
        return;
    for (i = 0; i < MAX_EFFECTS; i++) {
        struct effect *e = d->effects[i];
        double m;
        if (!e || !effect_playing(e, now))
            continue;
        m = abs(e->magnitude) / 10000.0 * e->gain / 10000.0;
        if (e->kind == FX_CONSTANT) {
            uint64_t since = now - e->changed;
            m *= since >= FADE_MS ? 0.0 : 1.0 - (double)since / FADE_MS;
            if (m > low)
                low = m;
        } else if (e->kind == FX_PERIODIC && (!e->period || e->period >= 60000)) {
            /* slow pulses; the engine's sine (faster) is left out */
            if (m > low)
                low = m;
        }
    }
    if (now < d->pulse_until) {
        low = SDL_max(low, d->pulse_low / 65535.0);
        high = SDL_max(high, d->pulse_high / 65535.0);
    }
    {
        double gain = d->ff_gain / 10000.0;
        uint16_t l = (uint16_t)(SDL_min(low * gain, 1.0) * 65535.0);
        uint16_t h = (uint16_t)(SDL_min(high * gain, 1.0) * 65535.0);
        int running = d->low || d->high;
        if (l == d->low && h == d->high && (!running || now - d->sent_at < RUMBLE_MS / 2))
            return;
        SDL_RumbleGamepad(d->gamepad, l, h, l || h ? RUMBLE_MS : 0);
        d->low = l;
        d->high = h;
        d->sent_at = now;
    }
}

void nt_rumble_pulse(void *device, uint16_t low, uint16_t high, uint32_t ms)
{
    struct device *d = device;
    if (!d || d->vtable != device_vtable || d->pad < 0 || !pad_rumbles(d))
        return;
    d->pulse_low = low;
    d->pulse_high = high;
    d->pulse_until = SDL_GetTicks() + ms;
    rumble_update(d);
}

static void rumble_stop_all(struct device *d)
{
    int i;
    for (i = 0; i < MAX_EFFECTS; i++)
        if (d->effects[i])
            d->effects[i]->playing = 0;
    d->pulse_until = 0;
    rumble_update(d);
}

/* DIEFFECT: size, flags +4, duration +8, sample period +0xc, gain +0x10,
 * trigger +0x14/+0x18, axes +0x1c/+0x20, direction +0x24, envelope +0x28,
 * type-specific size +0x2c and pointer +0x30 */
static void effect_set(struct effect *e, const uint8_t *params, DWORD flags)
{
    uint32_t size, at;
    const uint8_t *specific;
    if (!params)
        return;
    if (flags & DIEP_DURATION)
        e->duration = *(const DWORD *)(params + 8);
    if (flags & DIEP_GAIN)
        e->gain = SDL_min(*(const DWORD *)(params + 0x10), 10000u);
    if (!(flags & DIEP_TYPESPECIFICPARAMS))
        return;
    size = *(const DWORD *)(params + 0x2c);
    at = *(const DWORD *)(params + 0x30);
    specific = (const uint8_t *)(uintptr_t)at;
    if (!specific)
        return;
    if (e->kind == FX_CONSTANT && size >= 4) {
        int32_t m = *(const int32_t *)specific;
        if (m != e->magnitude)
            e->changed = SDL_GetTicks();
        e->magnitude = m;
    } else if (e->kind == FX_PERIODIC && size >= 16) {
        e->magnitude = (int32_t)SDL_min(*(const DWORD *)specific, 10000u);
        e->period = *(const DWORD *)(specific + 12);
    }
}

static HRESULT WINAPI fx_query(struct effect *e, const void *iid, uint32_t *out)
{
    (void)iid;
    OUT(out, e);
    e->refs++;
    return DI_OK;
}

static LONG WINAPI fx_add_ref(struct effect *e)
{
    return ++e->refs;
}

static LONG WINAPI fx_release(struct effect *e)
{
    int i;
    if (--e->refs > 0)
        return e->refs;
    for (i = 0; i < MAX_EFFECTS; i++)
        if (e->device->effects[i] == e)
            e->device->effects[i] = 0;
    e->playing = 0;
    rumble_update(e->device);
    nt_lowmem_free(e);
    return 0;
}

static HRESULT WINAPI fx_get_guid(struct effect *e, uint8_t *guid)
{
    memcpy(guid, e->guid, 16);
    return DI_OK;
}

static HRESULT WINAPI fx_set_parameters(struct effect *e, const uint8_t *params, DWORD flags)
{
    effect_set(e, params, flags);
    if (flags & DIEP_START) {
        e->playing = 1;
        e->started = SDL_GetTicks();
    }
    rumble_update(e->device);
    return DI_OK;
}

static HRESULT WINAPI fx_start(struct effect *e, DWORD iterations, DWORD flags)
{
    (void)iterations;
    (void)flags;
    e->playing = 1;
    e->started = SDL_GetTicks();
    if (e->kind == FX_CONSTANT)
        e->changed = e->started;
    rumble_update(e->device);
    return DI_OK;
}

static HRESULT WINAPI fx_stop(struct effect *e)
{
    e->playing = 0;
    rumble_update(e->device);
    return DI_OK;
}

static HRESULT WINAPI fx_get_status(struct effect *e, DWORD *status)
{
    *status = effect_playing(e, SDL_GetTicks()) ? 1 : 0; /* DIEGES_PLAYING */
    return DI_OK;
}

static HRESULT WINAPI fx_ok0(struct effect *e)
{
    (void)e;
    return DI_OK;
}

/* CreateEffect(guid, DIEFFECT or null, out, outer): GUID_ConstantForce is
 * 13541c20-8e33-..., the periodic ones (square, sine, triangle, sawtooth
 * up / down) 13541c22..26, the conditions and custom force the rest. */
static HRESULT WINAPI dev_create_effect(struct device *d, const uint8_t *guid, const uint8_t *params,
                                        uint32_t *out, void *outer)
{
    uint32_t first = *(const uint32_t *)guid;
    struct effect *e;
    int i;
    (void)outer;
    *out = 0;
    if (d->pad < 0 || !pad_rumbles(d))
        return (HRESULT)DIERR_UNSUPPORTED;
    for (i = 0; i < MAX_EFFECTS && d->effects[i]; i++)
        ;
    if (i == MAX_EFFECTS)
        return (HRESULT)DIERR_UNSUPPORTED;
    e = nt_lowmem_calloc(1, (uint32_t)sizeof *e);
    e->vtable = effect_vtable;
    e->refs = 1;
    e->device = d;
    memcpy(e->guid, guid, 16);
    e->kind = first == 0x13541c20u ? FX_CONSTANT
              : first >= 0x13541c22u && first <= 0x13541c26u ? FX_PERIODIC : FX_OTHER;
    e->duration = DI_INFINITE;
    e->gain = 10000;
    effect_set(e, params, 0xffffffffu & ~DIEP_START);
    d->effects[i] = e;
    nt_log("dinput: pad %d effect %08x (%s)", d->pad, first,
           e->kind == FX_CONSTANT ? "constant" : e->kind == FX_PERIODIC ? "periodic" : "not played");
    OUT(out, e);
    return DI_OK;
}

/* GetForceFeedbackState: DIGFFS_EMPTY (1) or not, DIGFFS_POWERON (0x80) */
static HRESULT WINAPI dev_get_ff_state(struct device *d, DWORD *state)
{
    int i, any = 0;
    for (i = 0; i < MAX_EFFECTS; i++)
        any |= d->effects[i] != 0;
    *state = 0x80 | (any ? 0 : 1);
    return DI_OK;
}

/* SendForceFeedbackCommand: RESET 1, STOPALL 2, PAUSE 4, ... */
static HRESULT WINAPI dev_send_ff_command(struct device *d, DWORD command)
{
    if (command & 3)
        rumble_stop_all(d);
    return DI_OK;
}

static HRESULT WINAPI dev_unacquire(struct device *d)
{
    open_pad(d);
    if (d->gamepad && (d->low || d->high)) {
        SDL_RumbleGamepad(d->gamepad, 0, 0, 0);
        d->low = d->high = 0;
    }
    return DI_OK;
}

static int32_t scale(const struct device *d, int axis, float v) /* v in -1..1 */
{
    float lo = (float)d->min[axis], hi = (float)d->max[axis];
    if (v < -1.0f)
        v = -1.0f;
    if (v > 1.0f)
        v = 1.0f;
    return (int32_t)(lo + (v + 1.0f) * 0.5f * (hi - lo) + 0.5f);
}

static HRESULT WINAPI dev_get_device_state(struct device *d, DWORD size, uint8_t *data)
{
    memset(data, 0, size);
    SDL_UpdateGamepads();
    if (d->pad < 0) {
        int n = 0, s;
        const bool *keys = SDL_GetKeyboardState(&n);
        for (s = 0; s < n; s++) {
            const struct nt_key *k;
            if ((keys[s] || nt_injected_keys[s]) && (k = nt_key_from_sdl((SDL_Scancode)s)) != 0 && k->dik < size)
                data[k->dik] = 0x80;
        }
        return DI_OK;
    }
    open_pad(d);
    rumble_update(d);
    if (!d->gamepad && size >= 0x110) { /* an empty slot: centred, nothing pressed */
        int i;
        for (i = 0; i < 6; i++)
            ((int32_t *)data)[i] = scale(d, i, 0.0f);
        memset(data + 0x20, 0xff, 16); /* hats centred */
    } /* fades and ends rumble between the game's changes */
    if (d->gamepad && size >= 0x110) {
        int32_t *axes = (int32_t *)data;
        SDL_Gamepad *g = d->gamepad;
        float lt = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
        float rt = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;
        static const SDL_GamepadButton buttons[10] = {
            SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
            SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
            SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK,
            SDL_GAMEPAD_BUTTON_RIGHT_STICK};
        int up, down, left, right, b;
        axes[0] = scale(d, 0, SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f);
        axes[1] = scale(d, 1, SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f);
        axes[2] = scale(d, 2, lt - rt);
        axes[3] = scale(d, 3, SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f);
        axes[4] = scale(d, 4, SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f);
        axes[5] = scale(d, 5, 0.0f);
        up = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_UP);
        down = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        left = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        right = SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        {
            int x = right - left, y = down - up;
            DWORD pov = 0xffffffffu;
            if (x || y) {
                static const DWORD angles[3][3] = {{31500, 0, 4500}, {27000, 0xffffffffu, 9000},
                                                   {22500, 18000, 13500}};
                pov = angles[y + 1][x + 1];
            }
            *(DWORD *)(data + 0x20) = pov;
            *(DWORD *)(data + 0x24) = *(DWORD *)(data + 0x28) = *(DWORD *)(data + 0x2c) = 0xffffffffu;
        }
        for (b = 0; b < 10; b++)
            data[0x30 + b] = SDL_GetGamepadButton(g, buttons[b]) ? 0x80 : 0;
    }
    return DI_OK;
}

static HRESULT WINAPI dev_get_device_info(struct device *d, uint8_t *info);

static HRESULT WINAPI dev_poll(struct device *d)
{
    (void)d;
    return DI_NOEFFECT;
}

static HRESULT WINAPI dev_ok1(struct device *d, DWORD a)
{
    (void)d;
    (void)a;
    return DI_OK;
}

static HRESULT WINAPI dev_ok2(struct device *d, DWORD a, DWORD b)
{
    (void)d;
    (void)a;
    (void)b;
    return DI_OK;
}

/* DIDEVICEINSTANCEA: size, guidInstance +4, guidProduct +0x14, dwDevType
 * +0x24, instance name +0x28, product name +0x12c, guidFFDriver +0x230 */
static void fill_instance(uint8_t *info, int pad)
{
    DWORD size = *(DWORD *)info ? *(DWORD *)info : 0x244;
    const char *name = pad < 0 ? "Keyboard" : slot_pads[pad] ? SDL_GetGamepadName(slot_pads[pad]) : 0;
    memset(info, 0, size);
    *(DWORD *)info = size;
    if (pad < 0) {
        make_guid(info + 4, GUID_SYS_KEYBOARD, 0xd5a0, 0x11cf);
        *(DWORD *)(info + 0x24) = 0x00000403;
    } else {
        make_guid(info + 4, PAD_GUID + (uint32_t)pad, 0, 0);
        make_guid(info + 0x14, PAD_GUID + 0x10 + (uint32_t)pad, 0, 0);
        *(DWORD *)(info + 0x24) = 0x00010214;
    }
    snprintf((char *)info + 0x28, 260, "%s", name ? name : "Gamepad");
    snprintf((char *)info + 0x12c, 260, "%s", name ? name : "Gamepad");
}

static HRESULT WINAPI dev_get_device_info(struct device *d, uint8_t *info)
{
    fill_instance(info, d->pad);
    return DI_OK;
}

/* ---- IDirectInputA ---- */

static HRESULT WINAPI di_query(struct dinput *o, const void *iid, uint32_t *out)
{
    (void)iid;
    OUT(out, o);
    return DI_OK;
}

static LONG WINAPI di_add_ref(struct dinput *o)
{
    return ++o->refs;
}

static LONG WINAPI di_release(struct dinput *o)
{
    return --o->refs > 0 ? o->refs : 0;
}

static HRESULT WINAPI di_create_device(struct dinput *o, const uint8_t *guid, uint32_t *out, void *outer)
{
    struct device *d;
    uint32_t first;
    int i;
    (void)o;
    (void)outer;
    first = *(const uint32_t *)guid;
    d = nt_lowmem_calloc(1, (uint32_t)sizeof *d);
    d->vtable = device_vtable;
    d->refs = 1;
    d->ff_gain = 10000;
    for (i = 0; i < 8; i++) {
        d->min[i] = 0;
        d->max[i] = 65535;
    }
    if (first == GUID_SYS_KEYBOARD) {
        d->pad = -1;
    } else if (first - PAD_GUID < MAX_PADS && (int)(first - PAD_GUID) < pad_count) {
        d->pad = (int)(first - PAD_GUID);
    } else {
        nt_lowmem_free(d);
        *out = 0;
        return (HRESULT)DIERR_DEVICENOTREG;
    }
    OUT(out, d);
    return DI_OK;
}

/* EnumDevices(type, callback, ctx, flags): the gamepads, as joysticks (type 4) */
static HRESULT WINAPI di_enum_devices(struct dinput *o, DWORD type, BOOL(WINAPI *callback)(uint8_t *instance, void *ctx),
                                      void *ctx, DWORD flags)
{
    uint8_t instance[0x244];
    int i;
    (void)o;
    (void)flags;
    find_pads();
    if (type != 0 && type != 4)
        return DI_OK;
    for (i = 0; i < pad_count; i++) {
        memset(instance, 0, sizeof instance);
        *(DWORD *)instance = sizeof instance;
        fill_instance(instance, i);
        if (!callback(instance, ctx))
            break;
    }
    return DI_OK;
}

static void **stubs(const char *interface, int count)
{
    void **v = nt_lowmem_calloc((uint32_t)count, (uint32_t)sizeof *v);
    int i;
    for (i = 0; i < count; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s method %d (+0x%x)", interface, i, i * 4);
        v[i] = nt_named_stub("com", name);
    }
    return v;
}

HRESULT WINAPI DirectInputCreateA(void *instance, DWORD version, uint32_t *out, void *outer)
{
    (void)instance;
    (void)version;
    (void)outer;
    if (!dinput_vtable) {
        int arena = nt_lowmem_arena(0); /* the tables outlive the game's runs */
        dinput_vtable = stubs("IDirectInputA", 8);
        dinput_vtable[0] = (void *)di_query;
        dinput_vtable[1] = (void *)di_add_ref;
        dinput_vtable[2] = (void *)di_release;
        dinput_vtable[3] = (void *)di_create_device;
        dinput_vtable[4] = (void *)di_enum_devices;
        device_vtable = stubs("IDirectInputDevice2A", 27);
        device_vtable[0] = (void *)dev_query;
        device_vtable[1] = (void *)dev_add_ref;
        device_vtable[2] = (void *)dev_release;
        device_vtable[3] = (void *)dev_get_capabilities;
        device_vtable[4] = (void *)dev_enum_objects;
        device_vtable[5] = (void *)dev_get_property;
        device_vtable[6] = (void *)dev_set_property;
        device_vtable[7] = (void *)dev_acquire;
        device_vtable[8] = (void *)dev_unacquire;
        device_vtable[9] = (void *)dev_get_device_state;
        device_vtable[11] = (void *)dev_ok1;  /* SetDataFormat */
        device_vtable[12] = (void *)dev_ok1;  /* SetEventNotification */
        device_vtable[13] = (void *)dev_ok2;  /* SetCooperativeLevel */
        device_vtable[15] = (void *)dev_get_device_info;
        device_vtable[18] = (void *)dev_create_effect;
        device_vtable[21] = (void *)dev_get_ff_state;
        device_vtable[22] = (void *)dev_send_ff_command;
        device_vtable[25] = (void *)dev_poll;
        effect_vtable = stubs("IDirectInputEffect", 13);
        effect_vtable[0] = (void *)fx_query;
        effect_vtable[1] = (void *)fx_add_ref;
        effect_vtable[2] = (void *)fx_release;
        effect_vtable[4] = (void *)fx_get_guid;
        effect_vtable[6] = (void *)fx_set_parameters;
        effect_vtable[7] = (void *)fx_start;
        effect_vtable[8] = (void *)fx_stop;
        effect_vtable[9] = (void *)fx_get_status;
        effect_vtable[10] = (void *)fx_ok0; /* Download */
        effect_vtable[11] = (void *)fx_ok0; /* Unload */
        the_dinput.vtable = dinput_vtable;
        nt_lowmem_arena(arena);
    }
    the_dinput.refs = 1;
    OUT(out, &the_dinput);
    return DI_OK;
}
