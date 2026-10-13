/* Reimplemented Rollcage.exe functions. Each is named after what it does;
 * docs/functions.csv maps names to original addresses and status. */
#ifndef REROLL_GAME_H
#define REROLL_GAME_H

#include "../runtime.h"

/* 0x497860 */
uint8_t *GAMEAPI gt20_decompress(const uint8_t *blob, uint8_t *out);

/* 0x489600 */
void GAMEAPI input_poll(void);

/* 0x428e10 */
uint64_t GAMEAPI controls_packet(int32_t player);

/* 0x4440b0 */
int32_t GAMEAPI race_frame(void);

/* 0x436a52 */
int32_t GAMEAPI present_frame(void);

/* 0x443b50; the original's body (race_draw_body) is called through race_draw_original */
void GAMEAPI race_draw(void);
extern void(GAMEAPI *race_draw_original)(void);

#endif
