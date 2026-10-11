#ifndef REROLL_NATIVE_RUMBLE_H
#define REROLL_NATIVE_RUMBLE_H

#include <stdint.h>

/* A rumble on one of the game's DirectInput joysticks (its device object),
 * mixed with the game's own force-feedback effects (native/dinput.c):
 * strengths 0..65535 for the heavy and light motors, for `ms`. */
void nt_rumble_pulse(void *device, uint16_t low, uint16_t high, uint32_t ms);

#endif
