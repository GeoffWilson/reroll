/* Just enough DirectInput 5 to call the game's device objects from C. */
#ifndef REROLL_DINPUT_H
#define REROLL_DINPUT_H

#include "../runtime.h"
#include "com.h"

#define DI_OK 0
#define DI_NOEFFECT 1 /* success: e.g. Poll on a device that needs no polling */
#define DIDC_ATTACHED 0x1

/* The IDirectInputDevice2A methods the game uses; a device is the game
 * memory address of the object, as the game stores it. */
static inline int32_t di_acquire(uint32_t d)
{
    return IDirectInputDevice2A_Acquire(d);
}

static inline int32_t di_poll(uint32_t d)
{
    return IDirectInputDevice2A_Poll(d);
}

static inline int32_t di_get_capabilities(uint32_t d, void *caps)
{
    return IDirectInputDevice2A_GetCapabilities(d, caps);
}

static inline int32_t di_get_device_state(uint32_t d, uint32_t size, void *data)
{
    return IDirectInputDevice2A_GetDeviceState(d, size, data);
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
