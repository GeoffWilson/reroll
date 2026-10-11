#ifndef REROLL_CAPTURE_H
#define REROLL_CAPTURE_H

#include <stdint.h>

#include "runtime.h"

/* Frame capture for the renderer work: see capture.c. */
void GAMEAPI capture_draw_otag(uint32_t start); /* hook of DrawOTag 0x428470 */
void GAMEAPI capture_draw_otag_rect(uint32_t start, const int16_t *rect); /* hook of 0x428480 */
void GAMEAPI capture_draw_prim(uint32_t packet);                        /* hook of DrawPrim 0x428460 */
void capture_present(int drawn);
void capture_install(void); /* the flip trampoline (live frames) */                /* at the start of each present */

#endif
