/* Fixed-point maths used throughout the physics. Values are 16.16 unless
 * named otherwise; rotation matrices are int16 with 4096 = 1.0.
 *
 * Bit-exact with the originals, including what x86 does that C does not
 * promise: products keep the low 32 bits of the 64-bit result, shift counts
 * wrap at 32, and the 64/32 divisions trap on overflow just like the
 * original's idiv.
 *
 * The leading-zero and square-root routines leave their working values in
 * two globals, and the "GTE" routines (named after the PlayStation geometry
 * coprocessor the game was written for) work on a register file of globals:
 * a 3x3 int16 matrix at 0x6ccbe0, an int16 vector at 0x6ccc64, another at
 * 0x6ccc88 with each value in a 32-bit slot,
 * and an int32 result vector at 0x6cccc8. */
#include "../runtime.h"
#include "fixed.h"

#define g_lz_value (*(uint32_t *)0x6cccdc)
#define g_lz_count (*(int32_t *)0x6ccce0)
#define g_sqrt_table ((const uint8_t *)0x4ac204)

#define g_gte_matrix ((const int16_t *)0x6ccbe0)
#define g_gte_vector ((const int16_t *)0x6ccc64)
#define g_gte_vector2 ((const int32_t *)0x6ccc88) /* int16 values in 32-bit slots */
#define g_gte_result ((int32_t *)0x6cccc8)

static uint32_t shl(uint32_t v, int32_t n)
{
    return v << (n & 31);
}

static int32_t sar(int32_t v, int32_t n)
{
    return v >> (n & 31);
}

/* imul then shr eax,n / shl edx,32-n: bits n..n+31 of the 64-bit product */
static int32_t mul_shift(int32_t a, int32_t b, int n)
{
    return (int32_t)(uint32_t)((uint64_t)((int64_t)a * b) >> n);
}

int32_t GAMEAPI fix_mul8(int32_t a, int32_t b)
{
    return mul_shift(a, b, 8);
}

static int32_t mul16(int32_t a, int32_t b)
{
    return mul_shift(a, b, 16);
}

/* idiv of edx:eax by `d`, as the original: no C runtime 64-bit division */
static int32_t div64(int64_t n, int32_t d)
{
    int32_t q, r;

    __asm__("idivl %4" : "=a"(q), "=d"(r) : "a"((uint32_t)n), "d"((int32_t)(n >> 32)), "rm"(d));
    (void)r;
    return q;
}

int32_t GAMEAPI fix_div16(int32_t a, int32_t b)
{
    return div64((int64_t)a * 65536, b);
}

int32_t GAMEAPI fix_div8(int32_t a, int32_t b)
{
    return div64((int64_t)a * 256, b);
}

/* Returns the (absolute) value, as the original leaves it in eax. */
uint32_t GAMEAPI count_leading_zeros(void)
{
    uint32_t bit = 0x80000000u, v = g_lz_value;

    if (v & bit)
        v = 0u - v;
    g_lz_value = v;
    g_lz_count = 0;
    while (!(bit & v)) {
        g_lz_count++;
        bit >>= 1;
        if (g_lz_count >= 32)
            break;
    }
    return v;
}

/* Returns the count: callers (0x407e90) use it. */
int32_t GAMEAPI count_leading_zeros_of(uint32_t v)
{
    g_lz_value = v;
    count_leading_zeros();
    return g_lz_count;
}

uint32_t GAMEAPI fix_sqrt_table(int32_t x)
{
    int32_t even, scale, v;

    g_lz_value = (uint32_t)x;
    count_leading_zeros();
    if (g_lz_count == 32)
        return 0;
    even = g_lz_count & ~1;
    scale = (0x1f - even) >> 1;
    if (even - 0x18 >= 0)
        v = (int32_t)shl((uint32_t)x, even - 0x18);
    else
        v = sar(x, 0x18 - even);
    v = *(const uint16_t *)(g_sqrt_table + (uint32_t)(v - 0x40) * 2);
    return shl((uint32_t)v, scale) >> 12;
}

int32_t GAMEAPI fix_isqrt(int32_t x)
{
    uint32_t root = 0, square = 0, bit;
    int32_t shift, v, n;

    count_leading_zeros_of(x & 0x80000000 ? 0u - (uint32_t)x : (uint32_t)x);
    shift = (g_lz_count - 2) & ~1;
    if (shift > 16) {
        shift = 16;
        v = (int32_t)shl((uint32_t)x, 16);
    } else if (shift > 0) {
        v = (int32_t)shl((uint32_t)x, shift);
    } else {
        v = sar(x, -shift);
    }
    for (n = 15; n >= 0; n--) {
        uint32_t trial;

        bit = 1u << n;
        trial = ((bit + 2 * root) << n) + square;
        if ((int32_t)trial <= v) {
            square = trial;
            root += bit;
        }
    }
    return (int32_t)shl(root, (16 - shift) / 2);
}

int32_t GAMEAPI vec_length(const int32_t *v)
{
    uint32_t x = (uint32_t)mul16(v[0], v[0]);
    uint32_t y = (uint32_t)mul16(v[1], v[1]);
    uint32_t z = (uint32_t)mul16(v[2], v[2]);
    int32_t scaled = ((x | y | z) & 0xe0000000u) != 0;

    if (scaled) {
        x >>= 2;
        y >>= 2;
        z >>= 2;
    }
    return (int32_t)shl((uint32_t)fix_isqrt((int32_t)(z + y + x)), scaled);
}

void GAMEAPI vec_div(int32_t *v, int32_t d)
{
    int32_t inverse = fix_div16(0x10000, d);

    v[0] = mul16(v[0], inverse);
    v[1] = mul16(v[1], inverse);
    v[2] = mul16(v[2], inverse);
}

static int32_t abs32(int32_t v)
{
    return (int32_t)(((uint32_t)v ^ (uint32_t)(v >> 31)) - (uint32_t)(v >> 31));
}

void GAMEAPI vec_normalise(int32_t *v)
{
    if (abs32(v[0]) > 0x230000 || abs32(v[1]) > 0x230000 || abs32(v[2]) > 0x230000) {
        v[0] >>= 3;
        v[1] >>= 3;
        v[2] >>= 3;
    }
    vec_div(v, vec_length(v));
}

/* Stores in the original's order, so an output that overlaps an input gives
 * the same (odd) result. */
void GAMEAPI vec_cross(const int32_t *a, const int32_t *b, int32_t *out)
{
    out[0] = mul16(a[1], b[2]) - mul16(a[2], b[1]);
    out[1] = mul16(a[2], b[0]) - mul16(a[0], b[2]);
    out[2] = mul16(a[0], b[1]) - mul16(a[1], b[0]);
}

int32_t GAMEAPI vec_dot(const int32_t *a, const int32_t *b)
{
    return mul16(a[2], b[2]) + mul16(a[1], b[1]) + mul16(a[0], b[0]);
}

void GAMEAPI vec_scale(int32_t s, const int32_t *v, int32_t *out)
{
    out[0] = mul16(v[0], s);
    out[1] = mul16(v[1], s);
    out[2] = mul16(v[2], s);
}

void GAMEAPI vec_scale8(int32_t *out, int32_t s, const int32_t *v)
{
    out[0] = fix_mul8(s, v[0]);
    out[1] = fix_mul8(s, v[1]);
    out[2] = fix_mul8(s, v[2]);
}

void GAMEAPI mat_mul_vec(const int32_t *m, const int32_t *v, int32_t *out)
{
    int32_t x = v[0], y = v[1], z = v[2];

    out[0] = mul16(m[2], z) + mul16(m[1], y) + mul16(m[0], x);
    out[1] = mul16(m[5], z) + mul16(m[4], y) + mul16(m[3], x);
    out[2] = mul16(m[8], z) + mul16(m[7], y) + mul16(m[6], x);
}

/* Load a body's int16 matrix (as its 5 dwords) into the GTE, with the
 * float copy the x87 paths use. */
void gte_load_matrix(const uint8_t *matrix)
{
    int i;

    for (i = 0; i < 5; i++)
        ((uint32_t *)0x6ccbe0)[i] = ((const uint32_t *)matrix)[i];
    for (i = 0; i < 9; i++)
        ((float *)0x6ccd1c)[(i / 3) * 4 + i % 3] = (float)g_gte_matrix[i] * *(const float *)0x4a3310;
}

void GAMEAPI gte_rotate(void)
{
    const int16_t *m = g_gte_matrix, *v = g_gte_vector;
    int32_t x = v[0], y = v[1], z = v[2];

    g_gte_result[0] = (int32_t)((uint32_t)(m[0] * x) + (uint32_t)(m[1] * y) + (uint32_t)(m[2] * z)) >> 12;
    g_gte_result[1] = (int32_t)((uint32_t)(m[3] * x) + (uint32_t)(m[4] * y) + (uint32_t)(m[5] * z)) >> 12;
    g_gte_result[2] = (int32_t)((uint32_t)(m[6] * x) + (uint32_t)(m[7] * y) + (uint32_t)(m[8] * z)) >> 12;
}

/* Cross product of the matrix diagonal with the second vector (the PS1 GTE
 * "outer product" command). */
void GAMEAPI gte_outer_product(void)
{
    const int16_t *m = g_gte_matrix;
    int32_t v[3] = {(int16_t)g_gte_vector2[0], (int16_t)g_gte_vector2[1], (int16_t)g_gte_vector2[2]};

    g_gte_result[0] = (int32_t)((uint32_t)(m[4] * v[2]) - (uint32_t)(m[8] * v[1]));
    g_gte_result[1] = (int32_t)((uint32_t)(v[0] * m[8]) - (uint32_t)(m[0] * v[2]));
    g_gte_result[2] = (int32_t)((uint32_t)(m[0] * v[1]) - (uint32_t)(v[0] * m[4]));
}
