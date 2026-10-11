/* DINPUT for the native build: the keyboard (a 256-byte DIK_ table from
 * SDL's key state) and SDL3 gamepads as DirectInput joysticks, laid out as
 * Windows (and Wine) show an XInput pad to DirectInput, which is what the
 * game's control settings were made with:
 *   X / Y left stick, Z = left trigger minus right trigger (one axis,
 *   accelerate is negative Z), Rx / Ry right stick, buttons A B X Y LB RB
 *   Back Start LS RS, the d-pad as POV hat 0.
 * Force feedback is not offered yet (the game creates its effects only for
 * devices that report it). */
#include "keys.h"
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

struct device {
    void **vtable;
    LONG refs;
    int pad;               /* -1 keyboard, else the gamepad index */
    SDL_Gamepad *gamepad;
    int32_t min[8], max[8]; /* axis ranges, by DIJOYSTATE2 offset / 4 */
};

struct dinput {
    void **vtable;
    LONG refs;
};

static void **dinput_vtable, **device_vtable;
static struct dinput the_dinput;
static SDL_JoystickID pad_ids[MAX_PADS];
static int pad_count = -1;

static void find_pads(void)
{
    int n = 0, i;
    SDL_JoystickID *ids;
    if (pad_count >= 0)
        return;
    ids = SDL_GetGamepads(&n);
    pad_count = 0;
    for (i = 0; ids && i < n && pad_count < MAX_PADS; i++)
        if (!getenv("REROLL_NO_PADS")) /* headless tests: leave the user's pads alone */
            pad_ids[pad_count++] = ids[i];
    SDL_free(ids);
    nt_log("dinput: %d gamepad(s)", pad_count);
}

/* ---- IDirectInputDevice2A ---- */

static HRESULT WINAPI dev_query(struct device *d, const void *iid, void **out)
{
    uint32_t first = iid ? *(const uint32_t *)iid : 0;
    if (first == IID_DEVICE2A || first == IID_DEVICEA) {
        *out = d;
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
    }
    return DI_OK;
}

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
    return DI_OK; /* dead zone, saturation, buffer size: SDL already filters */
}

static HRESULT WINAPI dev_acquire(struct device *d)
{
    if (d->pad >= 0 && !d->gamepad && d->pad < pad_count)
        d->gamepad = SDL_OpenGamepad(pad_ids[d->pad]);
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
    if (!d->gamepad)
        dev_acquire(d);
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

static HRESULT WINAPI dev_ok0(struct device *d)
{
    (void)d;
    return DI_OK;
}

/* DIDEVICEINSTANCEA: size, guidInstance +4, guidProduct +0x14, dwDevType
 * +0x24, instance name +0x28, product name +0x12c, guidFFDriver +0x230 */
static void fill_instance(uint8_t *info, int pad)
{
    DWORD size = *(DWORD *)info ? *(DWORD *)info : 0x244;
    const char *name = pad < 0 ? "Keyboard" : SDL_GetGamepadNameForID(pad_ids[pad]);
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

static HRESULT WINAPI di_query(struct dinput *o, const void *iid, void **out)
{
    (void)iid;
    *out = o;
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

static HRESULT WINAPI di_create_device(struct dinput *o, const uint8_t *guid, struct device **out, void *outer)
{
    struct device *d;
    uint32_t first;
    int i;
    (void)o;
    (void)outer;
    first = *(const uint32_t *)guid;
    d = nt_lowmem_calloc(1, sizeof *d);
    d->vtable = device_vtable;
    d->refs = 1;
    for (i = 0; i < 8; i++) {
        d->min[i] = 0;
        d->max[i] = 65535;
    }
    if (first == GUID_SYS_KEYBOARD) {
        d->pad = -1;
    } else if (first - PAD_GUID < MAX_PADS && (int)(first - PAD_GUID) < pad_count) {
        d->pad = (int)(first - PAD_GUID);
    } else {
        free(d);
        *out = 0;
        return (HRESULT)DIERR_DEVICENOTREG;
    }
    *out = d;
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
    void **v = nt_lowmem_calloc((size_t)count, sizeof *v);
    int i;
    for (i = 0; i < count; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s method %d (+0x%x)", interface, i, i * 4);
        v[i] = nt_named_stub("com", name);
    }
    return v;
}

HRESULT WINAPI DirectInputCreateA(void *instance, DWORD version, struct dinput **out, void *outer)
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
        device_vtable[8] = (void *)dev_ok0;   /* Unacquire */
        device_vtable[9] = (void *)dev_get_device_state;
        device_vtable[11] = (void *)dev_ok1;  /* SetDataFormat */
        device_vtable[12] = (void *)dev_ok1;  /* SetEventNotification */
        device_vtable[13] = (void *)dev_ok2;  /* SetCooperativeLevel */
        device_vtable[15] = (void *)dev_get_device_info;
        device_vtable[25] = (void *)dev_poll;
        the_dinput.vtable = dinput_vtable;
        nt_lowmem_arena(arena);
    }
    the_dinput.refs = 1;
    *out = &the_dinput;
    return DI_OK;
}
