#ifndef REROLL_VKGAME_H
#define REROLL_VKGAME_H

/* The Vulkan renderer in the game's window (REROLL_VULKAN=1): see vkgame.c. */
#include <stdint.h>

/* At DLL load: with REROLL_VULKAN=1, switches DirectDraw to windowed mode
 * and installs the texture hooks; returns the output's aspect (width /
 * height), or 0 when off. */
float vkgame_install(void);
int vkgame_active(void);

/* Draw an ordering table walk / one packet with Vulkan. 1 if done (the
 * original back end must not draw it), 0 to let the original draw. */
int vkgame_walk(uint32_t start);
int vkgame_prim(uint32_t packet);
/* At the flip 0x4876f0: presents the frame. 1 if done (skip the original flip). */
int vkgame_flip(void);

/* Shows a surface's picture as a frame of its own (the movies). */
int vkgame_show_surface(uint32_t surface);

/* Anisotropic filtering samples (1 = off), applied from the next frame. */
void vkgame_set_anisotropy(int samples);
int vkgame_anisotropy(void);

/* Frame-time accounting: the race loop (src/game/frame.c) adds the
 * counter ticks it spends in physics steps and in the game's drawing; the
 * per-1000-frames log line reports them with the frame-interval spread. */
extern uint64_t vkgame_sim_ticks, vkgame_game_draw_ticks;

/* The native build restarted the game in place. */
void vkgame_restart(void);

#endif
