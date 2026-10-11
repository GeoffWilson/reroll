/* Just enough DirectInput 5 to call the game's device objects from C. */
#ifndef REROLL_DINPUT_H
#define REROLL_DINPUT_H

#include "../runtime.h"

#define DI_OK 0
#define DI_NOEFFECT 1 /* success: e.g. Poll on a device that needs no polling */
#define DIDC_ATTACHED 0x1

/* IDirectInputDevice2A vtable slots used by the game */
enum {
    DIDEV_GET_CAPABILITIES = 3,
    DIDEV_ACQUIRE = 7,
    DIDEV_GET_DEVICE_STATE = 9,
    DIDEV_POLL = 25,
};

typedef struct di_device {
    void **vtbl;
} di_device;

typedef int32_t(WINAPI *di_call0)(di_device *self);
typedef int32_t(WINAPI *di_call_ptr)(di_device *self, void *arg);
typedef int32_t(WINAPI *di_call_state)(di_device *self, uint32_t size, void *data);

static inline int32_t di_acquire(di_device *d)
{
    return ((di_call0)d->vtbl[DIDEV_ACQUIRE])(d);
}

static inline int32_t di_poll(di_device *d)
{
    return ((di_call0)d->vtbl[DIDEV_POLL])(d);
}

static inline int32_t di_get_capabilities(di_device *d, void *caps)
{
    return ((di_call_ptr)d->vtbl[DIDEV_GET_CAPABILITIES])(d, caps);
}

static inline int32_t di_get_device_state(di_device *d, uint32_t size, void *data)
{
    return ((di_call_state)d->vtbl[DIDEV_GET_DEVICE_STATE])(d, size, data);
}

/* DIDEVCAPS (DirectX 5) */
struct di_devcaps {
    uint32_t size;
    uint32_t flags;
    uint32_t dev_type;
    uint32_t axes;
    uint32_t buttons;
    uint32_t povs;
    uint32_t ff_sample_period;
    uint32_t ff_min_time_resolution;
    uint32_t firmware_revision;
    uint32_t hardware_revision;
    uint32_t ff_driver_version;
};

/* DIJOYSTATE2 */
struct di_joystate2 {
    int32_t x, y, z, rx, ry, rz;
    int32_t slider[2];
    uint32_t pov[4];
    uint8_t buttons[128];
    int32_t vx, vy, vz, vrx, vry, vrz, vslider[2];
    int32_t ax, ay, az, arx, ary, arz, aslider[2];
    int32_t fx, fy, fz, frx, fry, frz, fslider[2];
};

#endif
