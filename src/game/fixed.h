/* Fixed-point maths (fixed.c). Addresses are the originals they replace. */
#ifndef REROLL_FIXED_H
#define REROLL_FIXED_H

#include "../runtime.h"

int32_t GAMEAPI fix_mul8(int32_t a, int32_t b);                          /* 0x4291a0: a*b >> 8 */
int32_t GAMEAPI fix_div16(int32_t a, int32_t b);                         /* 0x42df70: (a << 16) / b */
int32_t GAMEAPI fix_div8(int32_t a, int32_t b);                          /* 0x444490: (a << 8) / b */
uint32_t GAMEAPI count_leading_zeros(void);                              /* 0x427920: of 0x6cccdc */
int32_t GAMEAPI count_leading_zeros_of(uint32_t v);                      /* 0x429170: returns the count */
uint32_t GAMEAPI fix_sqrt_table(int32_t x);                              /* 0x427960: integer sqrt */
int32_t GAMEAPI fix_isqrt(int32_t x);                                    /* 0x40d9d0: sqrt, 16.16 */
int32_t GAMEAPI vec_length(const int32_t *v);                            /* 0x480cd0 */
void GAMEAPI vec_div(int32_t *v, int32_t d);                             /* 0x486e90 */
void GAMEAPI vec_normalise(int32_t *v);                                  /* 0x480c70 */
void GAMEAPI vec_cross(const int32_t *a, const int32_t *b, int32_t *out); /* 0x4806a0 */
int32_t GAMEAPI vec_dot(const int32_t *a, const int32_t *b);             /* 0x40d950 */
void GAMEAPI vec_scale(int32_t s, const int32_t *v, int32_t *out);       /* 0x480090 */
void GAMEAPI vec_scale8(int32_t *out, int32_t s, const int32_t *v);      /* 0x4444b0 */
void GAMEAPI mat_mul_vec(const int32_t *m, const int32_t *v, int32_t *out); /* 0x486d70 */
void GAMEAPI gte_rotate(void);                                           /* 0x428090 */
void GAMEAPI gte_outer_product(void);                                    /* 0x427f50 */
void gte_load_matrix(const uint8_t *matrix); /* int16 matrix and its float copy */

#endif
