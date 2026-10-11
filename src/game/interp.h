#ifndef REROLL_INTERP_H
#define REROLL_INTERP_H

#include "../runtime.h"

/* Forget recorded state (new race, or after a pause). */
void interp_reset(void);

/* Record car and camera state after one physics step. */
void interp_record_step(void);

/* Call `draw` with cars and cameras blended between the last two recorded
 * steps by alpha (0 = previous step, 65536 = latest), then restore them.
 * hud_tick: whether HUD flash countdowns may advance on this draw. */
void interp_draw(void(GAMEAPI *draw)(void), uint32_t alpha, int hud_tick);

#endif
