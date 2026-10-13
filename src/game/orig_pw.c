/* Group PW: the original's still-reachable functions in 0x493a10-0x496250,
 * the movie player's decoder half (the .fmv reader and its threads,
 * 0x492b90-0x4935e0, are another group's and are called by address):
 *
 *  - audio: the stop (0x493a10: take the lock, kill the multimedia timer,
 *    end its period, release the sound buffer *(player +0x94)), the 4-bit
 *    ADPCM decoders for mono (0x495000) and stereo (0x4950a3) with the step
 *    table 0x4b31d8 (89 x 16 dwords, the index kept as a byte offset 0..
 *    0x1600) and the index steps 0x4b4818, and the run-length unpacker
 *    0x495220;
 *  - video set-up: the frame buffers and the screen's pixel format
 *    (0x494d40, freed by 0x494ed0), the YCbCr -> screen pixel tables
 *    0x4daac8 / 0x4dacc8 / 0x4daec8 (128 dwords each, 0x493aa0, one value
 *    by 0x493bd0: at 15 / 16 bits two pixels per dword, a truncated and a
 *    rounded one, for the horizontally doubled picture), the front / back
 *    buffer swap 0x494d30;
 *  - a frame (0x494f10): the dequantisation table 0x4db0c8 (64 dwords) from
 *    the frame's quality and the base table 0x4b30d8, then 16 x 16
 *    macroblocks (0x494b20): a motion copy from the previous frame, a flat
 *    colour per 8 x 8 quarter (0x493e80), or a coded block (0x495958: six
 *    8 x 8 blocks, Y0..Y3 / Cb / Cr, each coefficients from the bit stream
 *    0x4952c0 (state 0x6d3000..0x6d3008, zig-zag table 0x6d3054), rows and
 *    columns through the 1-D inverse DCT 0x49545b / 0x4955be (x87 for the
 *    odd part, the original's instructions), then 16 rows to pixels
 *    0x4956d2);
 *  - the copy to the screen surface for 15 / 16, 24 and 32 bits (Lock /
 *    Unlock retried until they succeed: 0x494040, 0x4943b0, 0x494740 with
 *    the copies 0x4940e0, 0x494450, 0x4947e0), placed and doubled by the
 *    player's flags (+0x1c).
 *
 * Calling conventions are the original's: MSVC fastcall (ecx, edx, the rest
 * on the stack, callee pops) where it used one; 0x495000 / 0x4950a3 keep
 * every register (pushad) and 0x495958 all but eax, so they get asm entries
 * that do the same; 0x49545b / 0x4955be / 0x4956d2 take esi (and ebx, edi)
 * and get asm entries calling *_c bodies. Windows calls go through the
 * import table, COM calls through the object's vtable, everything outside
 * the group by address. */
#include "../runtime.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))

#define FASTCALL __attribute__((fastcall))
#define STDCALL __attribute__((stdcall))

/* This group's functions (also in build/groups/pw_protos.txt for physics.h). */
int32_t FASTCALL fmv_audio_close(uint8_t *player); /* 0x493a10 */
uint32_t FASTCALL fmv_colour_tables_build(int32_t bpp, uint32_t alpha, uint32_t no_dither); /* 0x493aa0 */
uint32_t FASTCALL fmv_colour_pack(int32_t bpp, int32_t r, int32_t g, int32_t b, uint32_t alpha,
                                  uint32_t no_dither); /* 0x493bd0 */
void FASTCALL fmv_block_fill(uint32_t y0, uint32_t y1, uint32_t y2, uint32_t y3, uint32_t cb, uint32_t cr, uint32_t dst,
                             uint32_t pitch); /* 0x493e80 */
void FASTCALL fmv_present_16(uint8_t *player); /* 0x494040 */
void FASTCALL fmv_blit_16(uint8_t *player, uint32_t dst, uint32_t pitch); /* 0x4940e0 */
void FASTCALL fmv_present_24(uint8_t *player); /* 0x4943b0 */
void FASTCALL fmv_blit_24(uint8_t *player, uint32_t dst, uint32_t pitch); /* 0x494450 */
void FASTCALL fmv_present_32(uint8_t *player); /* 0x494740 */
void FASTCALL fmv_blit_32(uint8_t *player, uint32_t dst, uint32_t pitch); /* 0x4947e0 */
void FASTCALL fmv_frame_decode(uint8_t *player, const uint8_t *src, uint32_t cur, uint32_t prev); /* 0x494b20 */
uint32_t FASTCALL fmv_frames_swap(uint8_t *player); /* 0x494d30 */
void FASTCALL fmv_video_init(uint8_t *player); /* 0x494d40 */
void FASTCALL fmv_video_free(uint8_t *player); /* 0x494ed0 */
void FASTCALL fmv_video_frame(uint8_t *player, const uint32_t *rec); /* 0x494f10 */
void fmv_adpcm_mono(void); /* 0x495000: asm entry, cdecl, keeps every register */
void GAMEAPI fmv_adpcm_mono_c(int32_t start, const uint8_t *src, uint16_t *dst, int32_t n, uint32_t *pred,
                              uint32_t *index); /* 0x495000 body */
void fmv_adpcm_stereo(void); /* 0x4950a3: asm entry, cdecl, keeps every register */
void GAMEAPI fmv_adpcm_stereo_c(uint32_t unused, const uint8_t *src, uint16_t *dst, int32_t n, uint32_t *pred,
                                uint32_t *index); /* 0x4950a3 body */
uint32_t FASTCALL fmv_rle_unpack(const uint8_t *src, uint8_t *dst, int32_t n); /* 0x495220 */
void GAMEAPI fmv_block_coeffs(void); /* 0x4952c0 */
void fmv_idct_rows(void); /* 0x49545b: asm entry, register argument esi (8 coefficients) */
void GAMEAPI fmv_idct_rows_c(const int32_t *s); /* 0x49545b body */
void fmv_idct_cols(void); /* 0x4955be: asm entry, register argument esi (8 values) */
void GAMEAPI fmv_idct_cols_c(const int32_t *s); /* 0x4955be body */
void fmv_row_to_pixels(void); /* 0x4956d2: asm entry, register arguments esi (Y), ebx (Cb, Cr +0x100), edi (out) */
void GAMEAPI fmv_row_to_pixels_c(const uint32_t *y, const uint32_t *c, uint32_t *out); /* 0x4956d2 body */
void fmv_macroblock_decode(void); /* 0x495958: asm entry, cdecl, keeps all but eax */
void GAMEAPI fmv_macroblock_decode_c(const uint8_t *src, uint32_t dst, uint32_t pitch); /* 0x495958 body */

/* Outside the group (C runtime free / calloc, the fatal error box). */
typedef void(GAMEAPI *pw_fn1)(uint32_t a);
typedef uint32_t(GAMEAPI *pw_fn2)(uint32_t a, uint32_t b);
typedef void(FASTCALL *pw_fast1)(uint32_t a);
#define pw_free(p) (((pw_fn1)0x49827a)(A(p)))
#define pw_calloc(n, size) (((pw_fn2)0x49946c)((n), (size)))
#define pw_fatal(text) (((pw_fast1)0x493340)(text))

/* Windows calls through the import table (stdcall). */
typedef int32_t(STDCALL *pw_api1)(uint32_t a);
typedef int32_t(STDCALL *pw_api2)(uint32_t a, uint32_t b);
#define IAT1(slot, a) ((*(pw_api1 *)(uintptr_t)(slot))(A(a)))
#define IAT2(slot, a, b) ((*(pw_api2 *)(uintptr_t)(slot))(A(a), A(b)))
#define pw_Sleep(ms) IAT1(0x4a3110, ms)
#define pw_InterlockedExchange(p, v) IAT2(0x4a314c, p, v)
#define pw_timeEndPeriod(p) IAT1(0x4a32b8, p)
#define pw_timeKillEvent(id) IAT1(0x4a32c4, id)

/* COM calls through the vtable (stdcall, `this` first). */
typedef int32_t(STDCALL *pw_com1)(uint32_t self);
typedef int32_t(STDCALL *pw_com2)(uint32_t self, uint32_t a);
typedef int32_t(STDCALL *pw_com5)(uint32_t self, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
#define VT(obj, off) (*(uint32_t *)(uintptr_t)(G32(obj) + (off)))
#define COM1(obj, off) (((pw_com1)(uintptr_t)VT(obj, off))(obj))
#define COM2(obj, off, a) (((pw_com2)(uintptr_t)VT(obj, off))(obj, A(a)))
#define COM5(obj, off, a, b, c, d) (((pw_com5)(uintptr_t)VT(obj, off))(obj, A(a), A(b), A(c), A(d)))
/* IDirectDrawSurface: GetPixelFormat 0x54, GetSurfaceDesc 0x58, Lock 0x64,
 * Unlock 0x80; IUnknown Release 8. */

/* The decoder's tables and state. */
#define T_Y 0x4daac8 /* luma -> pixel, 128 dwords */
#define T_CB 0x4dacc8 /* Cb -> pixel */
#define T_CR 0x4daec8 /* Cr -> pixel */
#define DQ 0x4db0c8 /* dequantisation, 64 dwords */
#define COEF 0x4da2a8 /* a block's coefficients, 64 dwords */
#define SCREEN_H 0x4da2a0
#define SCREEN_W 0x4da2a4
#define BS_SRC 0x6d3000 /* bit stream: next byte */
#define BS_BITS 0x6d3004 /* bit buffer */
#define BS_COUNT 0x6d3008 /* bits in the buffer */
#define IDCT_OUT 0x6d300c /* the 1-D inverse DCT's output */
#define ZIGZAG 0x6d3054 /* coefficient order, bytes (read from 63 down) */

/* The original's inline fixed-point multiply: (a * b) >> 16, rounded (adc
 * of the last bit shifted out). */
static inline int32_t pw_fixmul(int32_t a, int32_t b)
{
    int64_t p = (int64_t)a * b;

    return (int32_t)(uint32_t)((uint64_t)(p + 0x8000) >> 16);
}

/* The high half of a signed 32 x 32 product, doubled (imul; shl edx). */
static inline int32_t pw_mulhi2(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)((uint64_t)((int64_t)a * b) >> 32) << 1);
}

/* ===== 0x493a10 (ecx player): stop the movie's sound: when the timer runs
 * (+0x330), take the lock +0x32c (up to 1000 tries, Sleep(1) between),
 * kill the timer (failure: the fatal box 0x493340 with 0x4b30b0) and end
 * its period (+0x334); release the sound buffer +0x94. Returns 0. ===== */
int32_t FASTCALL fmv_audio_close(uint8_t *player)
{
    if (U32(player, 0x330) != 0) {
        int32_t tries = 999;

        for (;;) {
            int32_t r = pw_InterlockedExchange(player + 0x32c, 1);

            if (r != 1)
                break;
            pw_Sleep(r);
            if (tries-- <= 0)
                break;
        }
        if (pw_timeKillEvent(U32(player, 0x330)) == 0)
            U32(player, 0x330) = 0;
        else
            pw_fatal(0x4b30b0);
        pw_timeEndPeriod(U32(player, 0x334));
    }
    if (U32(player, 0x94) != 0)
        COM1(U32(player, 0x94), 8);
    return 0;
}

/* ===== 0x493aa0 (ecx bits per pixel, edx alpha byte for 24 / 32 bits;
 * stack: no_dither): the three colour tables, 128 entries each for the odd
 * values 2i+1 as a signed byte: Y + 128 grey; Cb as G -0.344, B 1.772; Cr
 * as R 1.402, G -0.714 (16.16). Returns the last value. ===== */
uint32_t FASTCALL fmv_colour_tables_build(int32_t bpp, uint32_t alpha, uint32_t no_dither)
{
    uint32_t r = 0;
    int32_t i;

    for (i = 0; i < 128; i++) {
        int32_t y = (int8_t)(uint8_t)(i * 2 + 1) + 0x80;

        r = fmv_colour_pack(bpp, y, y, y, alpha, no_dither);
        G32(T_Y + i * 4) = r;
    }
    for (i = 0; i < 128; i++) {
        int32_t c = (int8_t)(uint8_t)(i * 2 + 1);
        int32_t g = pw_fixmul(c, (int32_t)0xffffa7ea), b = pw_fixmul(c, 0x1c5a1);

        r = fmv_colour_pack(bpp, 0, g, b, 0, no_dither);
        G32(T_CB + i * 4) = r;
    }
    for (i = 0; i < 128; i++) {
        int32_t c = (int8_t)(uint8_t)(i * 2 + 1);
        int32_t red = pw_fixmul(c, 0x166e9), g = pw_fixmul(c, (int32_t)0xffff4931);

        r = fmv_colour_pack(bpp, red, g, 0, 0, no_dither);
        G32(T_CR + i * 4) = r;
    }
    return r;
}

/* A channel divided by `div`, rounded half away from zero and clamped to
 * +-lim (the sums wrap as the original's lea does). */
static int32_t pw_round(int32_t v, int32_t div, int32_t lim)
{
    int32_t q;

    if (v >= 0) {
        q = (int32_t)((uint32_t)v + (uint32_t)(div / 2)) / div;
        return q > lim ? lim : q;
    }
    q = (int32_t)((uint32_t)v - (uint32_t)(div / 2)) / div;
    return q < -lim ? -lim : q;
}

/* ===== 0x493bd0 (ecx bits per pixel, edx red; stack green, blue, alpha,
 * no_dither): a signed colour as a table value. 15 / 16 bits: two pixels
 * in one dword (high: channels truncated, low: rounded), each part with a
 * borrow from the channel below when that is negative, so the table values
 * can be added; no_dither (16 bits only) zeroes the rounded part. Else
 * alpha:red:green:blue bytes with the same borrows. ===== */
uint32_t FASTCALL fmv_colour_pack(int32_t bpp, int32_t r, int32_t g, int32_t b, uint32_t alpha, uint32_t no_dither)
{
    if (bpp == 15) {
        int32_t hr = r / 8, lr = pw_round(r, 8, 31);
        int32_t hg = g / 8, lg = pw_round(g, 8, 31);
        int32_t hb = b / 8, lb = pw_round(b, 8, 31);
        uint32_t v;

        if (lb < 0)
            lg--;
        if (lg < 0)
            lr--;
        if (lr < 0)
            hb--;
        if (hb < 0)
            hg--;
        if (hg < 0)
            hr--;
        v = ((uint32_t)hg & 0x1f) | ((uint32_t)hr << 5);
        v = (v << 5) | ((uint32_t)hb & 0x1f);
        v = (v << 6) | ((uint32_t)lr & 0x1f);
        v |= 0xffe00020u;
        v = (v << 5) | ((uint32_t)lg & 0x1f);
        return (v << 5) | ((uint32_t)lb & 0x1f);
    }
    if (bpp == 16) {
        int32_t hr = r / 8, lr = pw_round(r, 8, 31);
        int32_t hg = g / 4, lg = pw_round(g, 4, 63);
        int32_t hb = b / 8, lb = pw_round(b, 8, 31);
        uint32_t v;

        if (no_dither != 0)
            lb = lg = lr = 0;
        if (lb < 0)
            lg--;
        if (lg < 0)
            lr--;
        if (lr < 0)
            hb--;
        if (hb < 0)
            hg--;
        if (hg < 0)
            hr--;
        v = ((uint32_t)hr << 6) | ((uint32_t)hg & 0x3f);
        v = (v << 5) | ((uint32_t)hb & 0x1f);
        v = (v << 5) | ((uint32_t)lr & 0x1f);
        v = (v << 6) | ((uint32_t)lg & 0x3f);
        return (v << 5) | ((uint32_t)lb & 0x1f);
    }
    if (b < 0)
        g--;
    if (g < 0)
        r--;
    if (r < 0)
        alpha--;
    return ((((((uint32_t)r & 0xff) | (alpha << 8)) << 8) | ((uint32_t)g & 0xff)) << 8) | ((uint32_t)b & 0xff);
}

/* An 8 x 8 square of one value, rows (pitch & ~3) apart. */
static void pw_fill8(uint32_t at, uint32_t step, uint32_t v)
{
    int32_t r, k;

    for (r = 0; r < 8; r++, at += step)
        for (k = 0; k < 8; k++)
            G32(at + k * 4) = v;
}

/* A table entry for a signed byte level: (level * dq[0] >> 1) & 0x7f. */
static uint32_t pw_level(uint32_t table, uint32_t level)
{
    return G32(table + (((uint32_t)pw_fixmul((int8_t)(uint8_t)level, GI32(DQ)) >> 1) & 0x7f) * 4);
}

/* ===== 0x493e80 (cl Y0, dl Y1; stack Y2, Y3, Cb, Cr (signed bytes), dst,
 * pitch): a macroblock as four flat 8 x 8 quarters (Y0 top left, Y1 top
 * right, Y2 / Y3 below) with one chroma. ===== */
void FASTCALL fmv_block_fill(uint32_t y0, uint32_t y1, uint32_t y2, uint32_t y3, uint32_t cb, uint32_t cr, uint32_t dst,
                             uint32_t pitch)
{
    uint32_t c = pw_level(T_CB, cb) + pw_level(T_CR, cr);
    uint32_t step = (pitch >> 2) * 4;

    pw_fill8(dst, step, pw_level(T_Y, y0) + c);
    pw_fill8(dst + 0x20, step, pw_level(T_Y, y1) + c);
    pw_fill8(dst + pitch * 8, step, pw_level(T_Y, y2) + c);
    pw_fill8(dst + pitch * 8 + 0x20, step, pw_level(T_Y, y3) + c);
}

typedef void(FASTCALL *pw_blit_fn)(uint8_t *player, uint32_t dst, uint32_t pitch);

/* Lock the screen surface (+0x90, DDLOCK_WAIT, retried until it
 * succeeds), copy the frame with `blit` (surface, pitch), unlock (also
 * retried). */
static void pw_present(uint8_t *player, pw_blit_fn blit)
{
    uint32_t desc[0x1b];

    memset(desc, 0, sizeof(desc));
    desc[0] = 0x6c;
    while (COM5(U32(player, 0x90), 0x64, 0, desc, 1, 0) != 0)
        ;
    blit(player, desc[9], desc[4]);
    while (COM2(U32(player, 0x90), 0x80, 0) != 0)
        ;
}

/* ===== 0x494040 (ecx player): the frame to the 15 / 16-bit screen. ===== */
void FASTCALL fmv_present_16(uint8_t *player)
{
    pw_present(player, fmv_blit_16);
}

/* ===== 0x4943b0 (ecx player): the frame to the 24-bit screen. ===== */
void FASTCALL fmv_present_24(uint8_t *player)
{
    pw_present(player, fmv_blit_24);
}

/* ===== 0x494740 (ecx player): the frame to a 32-bit screen. ===== */
void FASTCALL fmv_present_32(uint8_t *player)
{
    pw_present(player, fmv_blit_32);
}

/* The vertical placement shared by the copies: flag 8 centres (flags 0xa0:
 * the half height minus the frame's). */
static uint32_t pw_place_y(uint32_t o, uint32_t fl, int32_t h, uint32_t pitch)
{
    if (fl & 8) {
        int32_t sh = GI32(SCREEN_H);
        int32_t d = !(fl & 0xa0) ? (sh - h) >> 1 : (sh >> 1) - h;

        o += (uint32_t)d * pitch;
    }
    return o;
}

/* 16 dwords from s to d. */
static void pw_copy16(uint32_t d, uint32_t s)
{
    int32_t k;

    for (k = 0; k < 16; k++)
        G32(d + k * 4) = G32(s + k * 4);
}

/* ===== 0x4940e0 (ecx player, edx surface; stack pitch): copy the frame
 * (+0x4c, w +0xc x h +0xe) to a 15 / 16-bit surface. Flags +0x1c: 1 / 2
 * offset by +0x20 pixels / +0x24 rows, 4 / 8 centre (0x50 / 0xa0: on half
 * the screen), 0x10 the source's two pixels per dword (else its low word),
 * 0x20 each row twice (only with 0x10; else nothing), 0x80 every other
 * row. ===== */
void FASTCALL fmv_blit_16(uint8_t *player, uint32_t dst, uint32_t pitch)
{
    int32_t w = S16(player, 0xc), h = S16(player, 0xe);
    uint32_t fl = U32(player, 0x1c), s = U32(player, 0x4c), o = dst;
    uint32_t rows;

    if (fl & 1)
        o = dst + U32(player, 0x20) * 2;
    if (fl & 2)
        o += U32(player, 0x24) * pitch;
    if (fl & 4) {
        int32_t sw = GI32(SCREEN_W);

        if (!(fl & 0x50))
            o += (uint32_t)(sw - w);
        else
            o += (uint32_t)((sw >> 1) - w) * 4;
    }
    o = pw_place_y(o, fl, h, pitch);
    if (fl & 0x20) {
        uint32_t q = pitch >> 2, o2 = o + q * 4;
        int32_t skip = (int32_t)(q * 2) - w;

        if (!(fl & 0x10) || h == 0)
            return;
        rows = (uint32_t)h;
        do {
            int32_t n = w;

            while (n != 0) {
                int32_t k;

                for (k = 0; k < 16; k++) {
                    uint32_t v = G32(s + k * 4);

                    G32(o2 + k * 4) = v;
                    G32(o + k * 4) = v;
                }
                n -= 16;
                s += 0x40;
                o += 0x40;
                o2 += 0x40;
            }
            o += (uint32_t)skip * 4;
            o2 += (uint32_t)skip * 4;
        } while (--rows != 0);
        return;
    }
    if (fl & 0x10) {
        uint32_t q = pitch >> 2;
        int32_t skip = (int32_t)q - w;

        if (fl & 0x80)
            skip += (int32_t)q;
        if (h == 0)
            return;
        rows = (uint32_t)h;
        do {
            int32_t n = w;

            while (n != 0) {
                pw_copy16(o, s);
                n -= 16;
                s += 0x40;
                o += 0x40;
            }
            o += (uint32_t)skip * 4;
        } while (--rows != 0);
        return;
    }
    {
        uint32_t q = pitch >> 1;
        int32_t skip = (int32_t)q - w;

        if (fl & 0x80)
            skip += (int32_t)q;
        if (h == 0)
            return;
        rows = (uint32_t)h;
        do {
            int32_t n = w;

            while (n != 0) {
                int32_t k;

                for (k = 0; k < 16; k++)
                    U16(o, k * 2) = (uint16_t)G32(s + k * 4);
                n -= 16;
                s += 0x40;
                o += 0x20;
            }
            o += (uint32_t)skip * 2;
        } while (--rows != 0);
    }
}

/* Each of 16 source dwords twice, into the row o2 and then o (the doubled
 * 24 / 32-bit copies). */
static void pw_double16(uint32_t o2, uint32_t o, uint32_t s)
{
    int32_t k;

    for (k = 0; k < 16; k++) {
        uint32_t v = G32(s + k * 4);

        G32(o2 + k * 8) = v;
        G32(o + k * 8) = v;
        G32(o2 + k * 8 + 4) = v;
        G32(o + k * 8 + 4) = v;
    }
}

/* The horizontal placement of the 24 / 32-bit copies. */
static uint32_t pw_place_x32(uint8_t *player, uint32_t dst, uint32_t fl, int32_t w, uint32_t pitch)
{
    uint32_t o = dst;

    if (fl & 1)
        o = dst + U32(player, 0x20) * 4;
    if (fl & 2)
        o += U32(player, 0x24) * pitch;
    if (fl & 4) {
        int32_t sw = GI32(SCREEN_W);
        int32_t d = !(fl & 0x50) ? (sw - w) >> 1 : (sw >> 1) - w;

        o += (uint32_t)d * 4;
    }
    return o;
}

/* The doubled-rows copy (flags 0x30) of the 24 / 32-bit copies. */
static void pw_double_rows32(uint32_t o, uint32_t s, int32_t w, int32_t h, uint32_t pitch)
{
    uint32_t q = pitch >> 2, o2 = o + q * 4, skip = (q - (uint32_t)w) * 2;
    uint32_t rows = (uint32_t)h;

    if (h == 0)
        return;
    do {
        int32_t n = w;

        while (n != 0) {
            pw_double16(o2, o, s);
            n -= 16;
            s += 0x40;
            o += 0x80;
            o2 += 0x80;
        }
        o += skip * 4;
        o2 += skip * 4;
    } while (--rows != 0);
}

/* The plain copy of the 24 / 32-bit copies: rows of 16-dword runs. */
static void pw_plain32(uint32_t o, uint32_t s, uint32_t fl, int32_t w, int32_t h, uint32_t pitch)
{
    uint32_t q = pitch >> 2;
    int32_t skip = (int32_t)q - w;
    uint32_t rows = (uint32_t)h;

    if (fl & 0x80)
        skip += (int32_t)q;
    if (h == 0)
        return;
    do {
        int32_t n = w;

        while (n != 0) {
            pw_copy16(o, s);
            n -= 16;
            s += 0x40;
            o += 0x40;
        }
        o += (uint32_t)skip * 4;
    } while (--rows != 0);
}

/* ===== 0x494450 (ecx player, edx surface; stack pitch): copy the frame
 * to a 24-bit surface (4 bytes a pixel, as the original does). Flags as
 * 0x4940e0; 0x30 doubles both ways; 0x10 alone only steps through the
 * rows without writing (sic). ===== */
void FASTCALL fmv_blit_24(uint8_t *player, uint32_t dst, uint32_t pitch)
{
    int32_t w = S16(player, 0xc), h = S16(player, 0xe);
    uint32_t fl = U32(player, 0x1c), s = U32(player, 0x4c);
    uint32_t o = pw_place_y(pw_place_x32(player, dst, fl, w, pitch), fl, h, pitch);

    if (fl & 0x20) {
        if (fl & 0x10)
            pw_double_rows32(o, s, w, h, pitch);
        return;
    }
    if (fl & 0x10) {
        /* sic: nothing written; a width that is not a multiple of 8 never
         * ends the original's step loop. */
        if (h != 0 && (w & 7) != 0)
            for (;;)
                ;
        return;
    }
    pw_plain32(o, s, fl, w, h, pitch);
}

/* ===== 0x4947e0 (ecx player, edx surface; stack pitch): copy the frame
 * to a 32-bit surface. Flags as 0x4940e0; 0x30 doubles both ways, 0x10
 * each pixel twice across. ===== */
void FASTCALL fmv_blit_32(uint8_t *player, uint32_t dst, uint32_t pitch)
{
    int32_t w = S16(player, 0xc), h = S16(player, 0xe);
    uint32_t fl = U32(player, 0x1c), s = U32(player, 0x4c);
    uint32_t o = pw_place_y(pw_place_x32(player, dst, fl, w, pitch), fl, h, pitch);

    if (fl & 0x20) {
        if (fl & 0x10)
            pw_double_rows32(o, s, w, h, pitch);
        return;
    }
    if (fl & 0x10) {
        uint32_t q = pitch >> 2;
        int32_t skip = (int32_t)q - w * 2;
        uint32_t rows = (uint32_t)h;

        if (fl & 0x80)
            skip += (int32_t)q;
        if (h == 0)
            return;
        do {
            int32_t n = w;

            while (n != 0) {
                int32_t k;

                for (k = 0; k < 8; k++) {
                    uint32_t v = G32(s + k * 4);

                    G32(o + k * 8 + 4) = v;
                    G32(o + k * 8) = v;
                }
                n -= 8;
                s += 0x20;
                o += 0x40;
            }
            o += (uint32_t)skip * 4;
        } while (--rows != 0);
        return;
    }
    pw_plain32(o, s, fl, w, h, pitch);
}

/* ===== 0x494b20 (ecx player, edx frame data; stack cur, prev): decode a
 * frame into `cur` (w x h dwords) as 16 x 16 macroblocks in rows. Each
 * starts with a byte ^ 0xb6: above 12 the size of a coded block
 * (0x495958); 1 a motion copy from `prev` (one byte: dx high nibble, dy
 * low, signed); 3 / 6 / 12 flat quarters (Y; Y0..Y3; Y at even bytes), then
 * Cb, Cr; anything else is skipped. ===== */
void FASTCALL fmv_frame_decode(uint8_t *player, const uint8_t *src, uint32_t cur, uint32_t prev)
{
    int32_t y, x, w;

    for (y = 0; y < S16(player, 0xe); y += 16) {
        for (x = 0, w = S16(player, 0xc); x < w; x += 16, w = S16(player, 0xc)) {
            uint32_t len = *src++ ^ 0xb6u;
            uint32_t at = cur + (uint32_t)(y * w + x) * 4;

            if ((int32_t)len > 12) {
                /* the call goes through the asm entry's body directly */
                fmv_macroblock_decode_c(src, at, (uint32_t)w * 4);
            } else if (len == 1) {
                int32_t dx = src[0] >> 4, dy = src[0] & 0xf, r;
                uint32_t from;

                if (dx > 7)
                    dx -= 16;
                if (dy > 7)
                    dy -= 16;
                from = prev + (uint32_t)((y - dy) * w - dx + x) * 4;
                for (r = 0; r < 16; r++) {
                    uint32_t step;

                    pw_copy16(at, from);
                    step = (uint32_t)S16(player, 0xc) * 4;
                    at += step;
                    from += step;
                }
            } else if (len == 3) {
                fmv_block_fill(src[0], src[0], src[0], src[0], src[1], src[2], at, (uint32_t)w * 4);
            } else if (len == 6) {
                fmv_block_fill(src[0], src[1], src[2], src[3], src[4], src[5], at, (uint32_t)w * 4);
            } else if (len == 12) {
                fmv_block_fill(src[0], src[2], src[4], src[6], src[8], src[10], at, (uint32_t)w * 4);
            }
            src += len;
        }
    }
}

/* ===== 0x494d30 (ecx player): swap the frame buffers +0x4c / +0x50;
 * returns the old +0x4c (left in eax). ===== */
uint32_t FASTCALL fmv_frames_swap(uint8_t *player)
{
    uint32_t a = U32(player, 0x4c);

    U32(player, 0x4c) = U32(player, 0x50);
    U32(player, 0x50) = a;
    return a;
}

/* Bits set in 0..30 of a mask. */
static int32_t pw_bits(uint32_t m)
{
    int32_t n = 0, i;

    for (i = 0; i < 31; i++)
        if (m & (1u << i))
            n++;
    return n;
}

/* The lowest set bit of a mask (sar until bit 0; a zero mask never ends
 * the original's loop). */
static int32_t pw_shift(uint32_t m)
{
    int32_t n = 0;
    int32_t v = (int32_t)m;

    if (m == 0)
        for (;;)
            ; /* sic */
    while (!(v & 1)) {
        v >>= 1;
        n++;
    }
    return n;
}

/* ===== 0x494d40 (ecx player): two zeroed frame buffers (w * h dwords,
 * calloc 0x49946c) at +0x4c / +0x50; the screen size from the surface
 * +0x90 (0x4da2a4 width, 0x4da2a0 height, when GetSurfaceDesc succeeds);
 * its pixel format: bits per channel +0x6c / +0x70 / +0x74, shifts +0x78 /
 * +0x7c / +0x80, mask +0x84, total +0x88; the colour tables for that
 * total; +0x58 = 1. ===== */
void FASTCALL fmv_video_init(uint8_t *player)
{
    uint32_t desc[0x1b], pf[8];
    uint32_t surf;

    U32(player, 0x4c) = pw_calloc(1, (uint32_t)(S16(player, 0xc) * S16(player, 0xe)) << 2);
    U32(player, 0x50) = pw_calloc(1, (uint32_t)(S16(player, 0xc) * S16(player, 0xe)) << 2);
    memset(desc, 0, sizeof(desc));
    desc[0] = 0x6c;
    if (COM2(U32(player, 0x90), 0x58, desc) == 0) {
        G32(SCREEN_W) = desc[3];
        G32(SCREEN_H) = desc[2];
    }
    surf = U32(player, 0x90);
    if (surf != 0) {
        int32_t rb, gb, bb;

        memset(pf, 0, sizeof(pf));
        pf[0] = 0x20;
        COM2(surf, 0x54, pf);
        rb = pw_bits(pf[4]);
        U32(player, 0x6c) = (uint32_t)rb;
        gb = pw_bits(pf[5]);
        U32(player, 0x70) = (uint32_t)gb;
        bb = pw_bits(pf[6]);
        U32(player, 0x74) = (uint32_t)bb;
        U32(player, 0x78) = (uint32_t)pw_shift(pf[4]);
        U32(player, 0x7c) = (uint32_t)pw_shift(pf[5]);
        U32(player, 0x80) = (uint32_t)pw_shift(pf[6]);
        U32(player, 0x88) = (uint32_t)(bb + gb + rb);
        U32(player, 0x84) = pf[4] | pf[5] | pf[6];
    }
    fmv_colour_tables_build(I32(player, 0x88), 0xff, 0);
    U32(player, 0x58) = 1;
}

/* ===== 0x494ed0 (ecx player): free the frame buffers (free 0x49827a)
 * when +0x58 is set, and clear it (the pointers are left). ===== */
void FASTCALL fmv_video_free(uint8_t *player)
{
    if (U32(player, 0x58) == 0)
        return;
    if (U32(player, 0x4c) != 0)
        pw_free(U32(player, 0x4c));
    if (U32(player, 0x50) != 0)
        pw_free(U32(player, 0x50));
    U32(player, 0x58) = 0;
}

/* ===== 0x494f10 (ecx player, edx the frame's queue record: data +0,
 * kind +8): a video frame (kind 3): the dequantisation table from the
 * frame's quality (signed byte at *(+0x64) + frame count +0x344): entry
 * (row, col) = base 0x4b30d8 * ((row + col) * step / 14 + q0), q0 = 8 +
 * 22 s / 100, step = 10 + 50 s / 100 - q0, s = 100 - quality; then the
 * frame into the buffer +0x4c from +0x50. ===== */
void FASTCALL fmv_video_frame(uint8_t *player, const uint32_t *rec)
{
    uint32_t data = rec[0];
    int32_t s, q0, step, row, i, c, e;

    if (rec[2] != 3)
        return;
    s = 100 - (int8_t)G8(U32(player, 0x344) + U32(player, 0x64));
    q0 = s * 22 / 100 + 8;
    step = s * 50 / 100 + 10 - q0;
    for (row = 0, i = 0; i < 64; row += step)
        for (c = 0, e = row; c < 8; c++, i++, e += step)
            GI32(DQ + i * 4) = pw_fixmul(GI32(0x4b30d8 + i * 4), (int32_t)((uint32_t)(e / 14 + q0) << 16));
    fmv_frame_decode(player, PTR(data), U32(player, 0x4c), U32(player, 0x50));
}

/* One ADPCM step: the predictor from the step table at byte offset
 * `index` (clamped to 16 bits), the index moved by the step 0x4b4818
 * (clamped to 0..0x1600). */
static int32_t pw_adpcm(int32_t *pred, int32_t index, uint32_t nibble)
{
    int32_t p = *pred + GI32((uint32_t)index + nibble * 4 + 0x4b31d8);

    if (p > 0x7fff)
        p = 0x7fff;
    else if (p < -0x8000)
        p = -0x8000;
    *pred = p;
    index += GI32(0x4b4818 + (nibble & 7) * 4);
    if (index > 0x1600)
        index = 0x1600;
    else if (index < 0)
        index = 0;
    return index;
}

/* ===== 0x495000 (cdecl; keeps every register): mono 4-bit ADPCM, samples
 * start .. start + n (at least one; even ones from a byte's high nibble),
 * into 16-bit `dst`; the predictor (low word, sign-extended) and the index
 * are read from and written back to *pred / *index. ===== */
void GAMEAPI fmv_adpcm_mono_c(int32_t start, const uint8_t *src, uint16_t *dst, int32_t n, uint32_t *pred,
                              uint32_t *index)
{
    int32_t i = start, end = n + start;
    int32_t idx = (int32_t)*index, p = (int16_t)*pred;

    do {
        uint32_t b = *src;

        if (!(i & 1)) {
            b >>= 4;
        } else {
            b &= 0xf;
            src++;
        }
        idx = pw_adpcm(&p, idx, b);
        *dst++ = (uint16_t)p;
        i++;
    } while (i < end);
    *pred = (uint32_t)p;
    *index = (uint32_t)idx;
}

__asm__(".text\n"
        ".globl fmv_adpcm_mono\n"
        "fmv_adpcm_mono:\n\t"
        "pushal\n\t"
        "pushl 0x38(%esp)\n\t"
        "pushl 0x38(%esp)\n\t"
        "pushl 0x38(%esp)\n\t"
        "pushl 0x38(%esp)\n\t"
        "pushl 0x38(%esp)\n\t"
        "pushl 0x38(%esp)\n\t"
        "call fmv_adpcm_mono_c\n\t"
        "addl $24, %esp\n\t"
        "popal\n\t"
        "ret\n");

/* ===== 0x4950a3 (cdecl; keeps every register): stereo 4-bit ADPCM, n
 * byte pairs (at least one): high nibble left, low right, into 16-bit
 * pairs. *pred / *index hold left in the low word, right in the high one;
 * the working state is 0x4b4838 / 0x4b483c (predictors) and 0x4b4840 /
 * 0x4b4844 (indexes). The first argument is unused. ===== */
void GAMEAPI fmv_adpcm_stereo_c(uint32_t unused, const uint8_t *src, uint16_t *dst, int32_t n, uint32_t *pred,
                                uint32_t *index)
{
    int32_t i = 0;
    uint32_t v;

    (void)unused;
    v = *index;
    GI32(0x4b4840) = (int16_t)v;
    GI32(0x4b4844) = (int16_t)(v >> 16);
    v = *pred;
    GI32(0x4b4838) = (int16_t)v;
    GI32(0x4b483c) = (int16_t)(v >> 16);
    do {
        int32_t p;

        p = GI32(0x4b4838);
        GI32(0x4b4840) = pw_adpcm(&p, GI32(0x4b4840), (uint32_t)(*src >> 4));
        dst[i * 2] = (uint16_t)p;
        GI32(0x4b4838) = p;
        p = GI32(0x4b483c);
        GI32(0x4b4844) = pw_adpcm(&p, GI32(0x4b4844), (uint32_t)(*src & 0xf));
        dst[i * 2 + 1] = (uint16_t)p;
        GI32(0x4b483c) = p;
        i++;
        src++;
    } while (i < n);
    *pred = (G32(0x4b483c) << 16) | (G32(0x4b4838) & 0xffff);
    *index = (G32(0x4b4844) << 16) | (G32(0x4b4840) & 0xffff);
}

__asm__(".text\n"
        ".globl fmv_adpcm_stereo\n"
        "fmv_adpcm_stereo:\n\t"
        "pushal\n\t"
        "pushl 0x38(%esp)\n\t"
        "pushl 0x38(%esp)\n\t"
        "pushl 0x38(%esp)\n\t"
        "pushl 0x38(%esp)\n\t"
        "pushl 0x38(%esp)\n\t"
        "pushl 0x38(%esp)\n\t"
        "call fmv_adpcm_stereo_c\n\t"
        "addl $24, %esp\n\t"
        "popal\n\t"
        "ret\n");

/* ===== 0x495220 (ecx src, edx dst; stack n): unpack n source bytes: a
 * byte equal to the one before it is followed by a count of further
 * copies. Returns the bytes written. ===== */
uint32_t FASTCALL fmv_rle_unpack(const uint8_t *src, uint8_t *dst, int32_t n)
{
    uint32_t out = 0;
    int32_t in = 0;
    uint8_t prev = (uint8_t)(src[0] - 1);

    if (n <= 0)
        return 0;
    do {
        uint8_t b = *src++;

        in++;
        *dst++ = b;
        out++;
        if (b == prev) {
            uint8_t count = *src++;
            uint32_t k;

            in++;
            for (k = 0; k < count; k++)
                dst[k] = b;
            dst += count;
            out += count;
        }
        prev = b;
    } while (in < n);
    return out;
}

/* The bit stream: drop `used` bits and refill the top with the next
 * byte at bit (count - 8) when the original does. */
#define BS_TAKE8()                                            \
    do {                                                      \
        bits >>= 8;                                           \
        bits |= (uint32_t)G8(s) << ((count - 8) & 31);        \
        s++;                                                  \
    } while (0)

/* ===== 0x4952c0: one block's 64 coefficients (0x4da2a8, in the order of
 * the table 0x6d3054 from 63 down) from the bit stream: the DC a signed
 * byte * dq[0]; then 3-bit codes: 0 zero, 2 / 6 +- dq, 4 two zeros, 1 a
 * run of 2 * (5-bit count) zeros, 5 one zero and such a run, 3 / 7 a
 * signed 6-bit level * dq (0x3f: the next byte as a signed level). ===== */
void GAMEAPI fmv_block_coeffs(void)
{
    uint32_t s = G32(BS_SRC), bits = G32(BS_BITS), count = G32(BS_COUNT);
    int32_t k = 63;
    uint32_t z = 0;

    GI32(COEF) = pw_fixmul((int32_t)(G32(BS_BITS) << 24) >> 8, GI32(DQ));
    BS_TAKE8();
    do {
        int took8 = 0;

        z = G8(ZIGZAG + k);
        switch (bits & 7) {
        case 2:
            GI32(COEF + z * 4) = GI32(DQ + z * 4);
            break;
        case 6:
            GI32(COEF + z * 4) = -GI32(DQ + z * 4);
            break;
        case 0:
            GI32(COEF + z * 4) = 0;
            break;
        case 4:
            GI32(COEF + z * 4) = 0;
            z = G8(ZIGZAG + k - 1);
            k--;
            GI32(COEF + z * 4) = 0;
            break;
        case 5:
        case 1: {
            uint32_t run;

            if ((bits & 7) == 5) {
                GI32(COEF + z * 4) = 0;
                z = G8(ZIGZAG + k - 1);
                k--;
            }
            run = (bits & 0xff) >> 3; /* sic: 0 runs 2^32 times */
            do {
                GI32(COEF + z * 4) = 0;
                z = G8(ZIGZAG + k - 1);
                GI32(COEF + z * 4) = 0;
                z = G8(ZIGZAG + k - 2);
                k -= 2;
            } while (--run != 0);
            k++;
            took8 = 1;
            break;
        }
        default: /* 3, 7 */
            if ((bits & 0xfc) != 0xfc) {
                GI32(COEF + z * 4) = pw_fixmul((int32_t)((bits & 0xfc) << 24) >> 10, GI32(DQ + z * 4));
            } else {
                BS_TAKE8();
                GI32(COEF + z * 4) = pw_fixmul((int32_t)(bits << 24) >> 8, GI32(DQ + z * 4));
            }
            took8 = 1;
            break;
        }
        if (took8) {
            BS_TAKE8();
        } else {
            bits >>= 3;
            count -= 3;
            if ((int32_t)count < 8) {
                bits |= (uint32_t)G8(s) << (count & 31);
                s++;
                count += 8;
            }
        }
    } while (--k > 0);
    G32(BS_SRC) = s;
    G32(BS_BITS) = bits;
    G32(BS_COUNT) = count;
}

/* The 1-D inverse DCT shared by the row and column passes (stride 9 and 1
 * in the output); the odd part's rotation is the original's x87 sequence,
 * rounded by the 1.5 * 2^52 constant 0x6d3030 through the doubles at
 * 0x6d3010 / 0x6d3018 (left there, then overwritten as scratch). */
static void pw_idct(const int32_t *s, int32_t *o, int32_t step)
{
    int32_t e = s[3] + s[5], a = s[5] - s[3];
    int32_t f = s[7] + s[1], b = s[1] - s[7];
    int32_t z = e + f, m1, m2, plo, qlo, t, u, v, w, lo, hi, x, y;

    GI32(0x6d3010) = a;
    GI32(0x6d3014) = b;
    m1 = pw_mulhi2(f - e, GI32(0x6d3020));
    __asm__ volatile("fildl 0x6d3010\n\t"
                     "fildl 0x6d3014\n\t"
                     "fld %%st(0)\n\t"
                     "fmuls 0x6d3028\n\t"
                     "fxch %%st(2)\n\t"
                     ".byte 0xdc, 0xc1\n\t" /* fadd st(1), st */
                     "fmuls 0x6d3024\n\t"
                     "fxch %%st(1)\n\t"
                     "fmuls 0x6d302c\n\t"
                     ".byte 0xdc, 0xc1\n\t" /* fadd st(1), st */
                     ".byte 0xde, 0xea\n\t" /* fsubp st(2), st */
                     "fadds 0x6d3030\n\t"
                     "fxch %%st(1)\n\t"
                     "fadds 0x6d3030\n\t"
                     "fstpl 0x6d3018\n\t"
                     "fstpl 0x6d3010"
                     :
                     :
                     : "memory", "st", "st(1)", "st(2)");
    qlo = GI32(0x6d3010);
    plo = GI32(0x6d3018);
    GI32(0x6d3014) = qlo + m1;
    GI32(0x6d3018) = m1 + plo;
    GI32(0x6d301c) = plo + z;
    t = s[6] + s[2];
    u = s[2] - s[6];
    v = s[4] + s[0];
    w = s[0] - s[4];
    m2 = pw_mulhi2(u, GI32(0x6d3020));
    t += m2;
    lo = m2 + w;
    hi = w - m2;
    x = t + v;
    y = v - t;
    o[2 * step] = hi + GI32(0x6d3014);
    o[5 * step] = hi - GI32(0x6d3014);
    o[3 * step] = y + GI32(0x6d3010);
    o[4 * step] = y - GI32(0x6d3010);
    o[0] = x + GI32(0x6d301c);
    o[1 * step] = lo + GI32(0x6d3018);
    o[6 * step] = lo - GI32(0x6d3018);
    o[7 * step] = x - GI32(0x6d301c);
}

/* ===== 0x49545b (esi: 8 coefficients): the row pass into the column of
 * the 9-wide table at *0x6d300c; all-zero AC copies the DC. ===== */
void GAMEAPI fmv_idct_rows_c(const int32_t *s)
{
    int32_t *o = (int32_t *)PTR(G32(IDCT_OUT));

    if ((s[1] | s[5] | s[7] | s[3] | s[2] | s[6] | s[4]) == 0) {
        int32_t k;

        for (k = 0; k < 8; k++)
            o[k * 9] = s[0];
        return;
    }
    pw_idct(s, o, 9);
}

__asm__(".text\n"
        ".globl fmv_idct_rows\n"
        "fmv_idct_rows:\n\t"
        "pushl %esi\n\t"
        "call fmv_idct_rows_c\n\t"
        "addl $4, %esp\n\t"
        "ret\n");

/* ===== 0x4955be (esi: 8 values): the column pass into 8 dwords at
 * *0x6d300c (which it also reads once, unused). ===== */
void GAMEAPI fmv_idct_cols_c(const int32_t *s)
{
    (void)*(volatile uint32_t *)PTR(G32(IDCT_OUT));
    pw_idct(s, (int32_t *)PTR(G32(IDCT_OUT)), 1);
}

__asm__(".text\n"
        ".globl fmv_idct_cols\n"
        "fmv_idct_cols:\n\t"
        "pushl %esi\n\t"
        "call fmv_idct_cols_c\n\t"
        "addl $4, %esp\n\t"
        "ret\n");

/* ===== 0x4956d2 (esi 16 Y, ebx 8 Cb with Cr at +0x100, edi 16 pixels):
 * a row of pixels: the tables at bits 17..23 of each value, one chroma for
 * two pixels. ===== */
void GAMEAPI fmv_row_to_pixels_c(const uint32_t *y, const uint32_t *c, uint32_t *out)
{
    int32_t j;

    for (j = 0; j < 8; j++) {
        uint32_t ch = G32(T_CB + ((c[j] >> 17) & 0x7f) * 4);
        uint32_t cr = G32(T_CR + ((c[j + 0x40] >> 17) & 0x7f) * 4);

        out[j * 2] = G32(T_Y + ((y[j * 2] >> 17) & 0x7f) * 4) + ch + cr;
        out[j * 2 + 1] = G32(T_Y + ((y[j * 2 + 1] >> 17) & 0x7f) * 4) + ch + cr;
    }
}

__asm__(".text\n"
        ".globl fmv_row_to_pixels\n"
        "fmv_row_to_pixels:\n\t"
        "pushl %edi\n\t"
        "pushl %ebx\n\t"
        "pushl %esi\n\t"
        "call fmv_row_to_pixels_c\n\t"
        "addl $12, %esp\n\t"
        "ret\n");

/* ===== 0x495958 (cdecl src, dst, pitch; keeps all but eax): a coded
 * macroblock: the bit stream from src, six blocks (Y0..Y3 into the 16 x 16
 * table 0x4da4c8, Cb 0x4da8c8, Cr 0x4da9c8, 8 x 8) through rows (into
 * 0x4da3a8, 9 wide) and columns, then 16 pixel rows to dst. ===== */
void GAMEAPI fmv_macroblock_decode_c(const uint8_t *src, uint32_t dst, uint32_t pitch)
{
    static const uint32_t out[6] = {0x4da4c8, 0x4da4e8, 0x4da6c8, 0x4da6e8, 0x4da8c8, 0x4da9c8};
    static const uint32_t stride[6] = {0x40, 0x40, 0x40, 0x40, 0x20, 0x20};
    int32_t b, i;

    G32(BS_SRC) = A(src + 1);
    G32(BS_BITS) = src[0];
    G32(BS_COUNT) = 8;
    for (b = 0; b < 6; b++) {
        fmv_block_coeffs();
        for (i = 0; i < 8; i++) {
            G32(IDCT_OUT) = 0x4da3a8 + i * 4;
            fmv_idct_rows_c((const int32_t *)PTR(COEF + i * 0x20));
        }
        for (i = 0; i < 8; i++) {
            G32(IDCT_OUT) = out[b] + stride[b] * i;
            fmv_idct_cols_c((const int32_t *)PTR(0x4da3a8 + i * 0x24));
        }
    }
    for (i = 0; i < 16; i++) {
        fmv_row_to_pixels_c((const uint32_t *)PTR(0x4da4c8 + i * 0x40), (const uint32_t *)PTR(0x4da8c8 + (i >> 1) * 0x20),
                            (uint32_t *)PTR(dst));
        if (i < 15)
            dst += pitch;
    }
}

__asm__(".text\n"
        ".globl fmv_macroblock_decode\n"
        "fmv_macroblock_decode:\n\t"
        "pushl %ecx\n\t"
        "pushl %edx\n\t"
        "pushl 0x14(%esp)\n\t"
        "pushl 0x14(%esp)\n\t"
        "pushl 0x14(%esp)\n\t"
        "call fmv_macroblock_decode_c\n\t"
        "addl $12, %esp\n\t"
        "popl %edx\n\t"
        "popl %ecx\n\t"
        "ret\n");
