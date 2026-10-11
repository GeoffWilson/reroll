#ifndef REROLL_NATIVE_KEYS_H
#define REROLL_NATIVE_KEYS_H

#include <SDL3/SDL.h>
#include <stdint.h>

/* One key: SDL scancode, DirectInput code (PC set 1, 0x80 = E0 prefix),
 * Windows virtual-key code. */
struct nt_key {
    SDL_Scancode sdl;
    uint8_t dik, vk;
};

const struct nt_key *nt_key_from_sdl(SDL_Scancode s);
const struct nt_key *nt_key_from_vk(uint8_t vk);

#endif
