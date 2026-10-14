/* The Microsoft C runtime entry points the game calls, group CB
 * (0x497000-0x4a0000): strings and memory, ctype, sprintf / vsprintf with the
 * runtime's own float-to-text conversion, rand, the allocator entries,
 * _ftol / __allmul / _chkstk, the x87 maths (sin, cos, atan, sqrt, pow), the
 * path helpers (_splitpath, _makepath, _getcwd, _chdir, _access), atoi, clock,
 * exit, _beginthread, and two small CD audio helpers that live in this range.
 *
 * Behaviour the game can see is the original's: sprintf output for every
 * format (the float digits come from a port of the runtime's 80-bit decimal
 * converter $I10_OUTPUT, so rounding matches, e.g. "%.2f" of 0.125 gives
 * "0.13"), rand's LCG and seed (the per-thread data's +0x14, from _getptd
 * 0x49bcf1), _ftol's truncation, strncpy's padding.
 *
 * Allocation: malloc / calloc / free go straight to HeapAlloc / HeapFree on
 * the runtime heap (*0x6d1268), as the original's
 * large-block path does; the small-block heap (__sbh_*, 0x49b240-0x49ba3b) is
 * only reached through these three entries, so it is no longer used. Blocks
 * are rounded to 16 bytes as before.
 *
 * Left original and called by address: the per-thread data (_getptd
 * 0x49bcf1), errno (_errno 0x49d48e, __doserrno 0x49d497, _dosmaperr
 * 0x49d41b), the runtime locks (_lock 0x49b161, _unlock 0x49b1c2), the
 * locale-dependent paths (_toupper_lk / _tolower_lk, __crtGetStringTypeA,
 * the MBCS string helpers), the maths error handler _87except (only for
 * real errors: NaN / infinite / out-of-domain arguments, overflow), and the
 * whole stdio / lowio family (fopen, fread, fseek, ftell, fclose, printf:
 * see build/groups/cb_notes.txt). */
#include "../runtime.h"
#include "physics.h"
#include "winapi.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define S8(p, off) (*(int8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define PTR(p, off) ((uint8_t *)(uintptr_t)U32(p, off))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))

#define STDCALL __attribute__((stdcall))

/* Runtime state (data section). */
#define g_crt_heap G32(0x6d1268)           /* HeapCreate handle */
#define g_new_mode GI32(0x4db360)          /* _newmode: retry through the new handler */
#define g_new_handler G32(0x4db364)        /* _pnhHeap */
#define g_lc_ctype GI32(0x4db37c)          /* __lc_handle[LC_CTYPE]: 0 = "C" (no setlocale in the game) */
#define g_setlc_active GI32(0x6d1248)      /* __setlc_active */
#define g_unguarded_readlc 0x6d124c         /* __unguarded_readlc_active (address) */
#define g_pctype ((const uint16_t *)GP(0x4b4b00)) /* _pctype */
#define g_mb_cur_max GI32(0x4b4d0c)        /* __mb_cur_max */
#define g_decimal_point G8(0x4b4d10)       /* *__decimal_point */
#define g_null_string ((const char *)GP(0x4b4da8))      /* "(null)" */
#define g_null_wstring ((const uint16_t *)GP(0x4b4dac)) /* L"(null)" */
#define g_ismbcodepage GI32(0x6d103c)
#define g_mbctype ((const uint8_t *)0x6d1141) /* _mbctype + 1 */
#define g_fastflag GI32(0x4db28c)          /* maths: skip the exception check */

/* Original runtime functions left in place. */
#define crt_getptd GCALL(0x49bcf1, crtc_getptd)
#define crt_initptd GCALL(0x49bcde, crtc_initptd)
#define crt_errno GCALL(0x49d48e, crtc_errno)
#define crt_doserrno GCALL(0x49d497, crtc_doserrno)
#define crt_dosmaperr GCALL(0x49d41b, crtc_dosmaperr)
#define crt_lock GCALL(0x49b161, crtc_lock)
#define crt_unlock GCALL(0x49b1c2, crtc_unlock)
#define crt_toupper_lk GCALL(0x4985af, crtc_toupper_lk)
#define crt_tolower_lk GCALL(0x4986ea, crtc_tolower_lk)
#define crt_get_string_type GCALL(0x49aee3, crtc_get_string_type)
#define crt_mbsdec GCALL(0x49cea2, crtc_mbsdec)
#define crt_mbsnbcpy GCALL(0x49d303, crtc_mbsnbcpy)
#define crt_lockexit GCALL(0x498d50, crtc_lockexit)
#define crt_threadstart 0x498df3

/* ---- CD audio helpers (0x497020-0x4977e0 is the game's CD / mixer code) ---- */

#define cd_enabled GI32(0x4db250)
#define mixer_scan GCALL(0x497040, mixer_scan) /* group CA's C, not hooked: by address */
#define aux_query GCALL(0x496e50, aux_query) /* -1 end, 0 not a CD output, 1 volume read */

/* 0x497740: is there a mixer line for the CD (scanned once: 0x4b4aa0 set
 * means not yet; the result is kept at 0x4db288)? An open mixer
 * (0x4db284) answers from 0x4db280. */
static int32_t cd_mixer_open(void)
{
    if (G32(0x4db284) != 0)
        return G32(0x4db280) != 0;
    if (G32(0x4b4aa0) != 0) {
        G32(0x4b4aa0) = 0;
        mixer_scan(0x2724);
        G32(0x4db288) = G32(0x4db1f0) != 0;
    }
    return GI32(0x4db288);
}

/* 0x497790: the CD line's volume from the mixer. */
static int32_t cd_mixer_volume(void)
{
    mixer_scan(0x272e);
    return GI32(0x4db204);
}

/* 0x497660: is there an auxiliary CD output with a volume (scanned once:
 * 0x4b4a9c set means not yet; the result is kept at 0x4db274)? The aux
 * table (0x4db268) answers from 0x4db26c. */
static int32_t cd_aux_find(void)
{
    uint8_t caps[0x30];
    uint32_t volume, i;
    if (G32(0x4db268) != 0)
        return G32(0x4db26c) != 0;
    if (G32(0x4b4a9c) != 0) {
        G32(0x4b4a9c) = 0;
        for (i = 0;; i++) {
            int32_t r = aux_query(i, caps, &volume);
            if (r == -1) {
                G32(0x4db274) = 0;
                break;
            }
            if (r == 1) {
                G32(0x4db274) = 1;
                break;
            }
        }
    }
    return GI32(0x4db274);
}

/* 0x4976e0: the first auxiliary CD output's volume: the mean of left and
 * right if it has both (AUXCAPS dwSupport & AUXCAPS_LRVOLUME), else the low
 * word; 0 if there is none. */
static int32_t cd_aux_volume(void)
{
    uint8_t caps[0x30];
    uint32_t volume, i;
    for (i = 0;; i++) {
        int32_t r = aux_query(i, caps, &volume);
        if (r == -1)
            return 0;
        if (r == 1)
            break;
    }
    if (U8(caps, 0x2c) & 2)
        return (int32_t)(((volume >> 16) + (volume & 0xffff)) >> 1);
    return (int32_t)(volume & 0xffff);
}

/* 0x497630: the CD volume, from the mixer line if there is one, else the
 * auxiliary output; 0 if CD audio is off or neither exists. */
int32_t GAMEAPI cd_volume_get(void)
{
    if (cd_enabled == 0)
        return 0;
    if (cd_mixer_open() != 0)
        return cd_mixer_volume();
    if (cd_aux_find() != 0)
        return cd_aux_volume();
    return 0;
}

/* 0x4977b0: 1 if the CD volume can be read (mixer line or auxiliary output). */
int32_t GAMEAPI cd_volume_available(void)
{
    if (cd_enabled == 0)
        return 0;
    if (cd_mixer_open() != 0)
        return 1;
    if (cd_aux_find() != 0)
        return 1;
    return 0;
}

/* ---- strings ---- */

static uint32_t str_length(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (uint32_t)(p - s);
}

static void mem_move(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        while (n--)
            d[n] = s[n];
    }
}

/* 0x498450: strcpy (enters strcat's copy loop). */
char *GAMEAPI msvc_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != 0) {
    }
    return dst;
}

/* 0x498460: strcat. */
char *GAMEAPI msvc_strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d)
        d++;
    while ((*d++ = *src++) != 0) {
    }
    return dst;
}

/* 0x498350: strncpy: at most n characters, the rest of the n zero-filled. */
char *GAMEAPI msvc_strncpy(char *dst, const char *src, uint32_t n)
{
    char *d = dst;
    while (n) {
        n--;
        if ((*d++ = *src++) == 0)
            break;
    }
    while (n) {
        n--;
        *d++ = 0;
    }
    return dst;
}

/* 0x4993f0: strncmp: -1 / 0 / 1 (unsigned characters). */
int32_t GAMEAPI msvc_strncmp(const char *a, const char *b, uint32_t n)
{
    uint32_t count = 0, i;
    if (n == 0)
        return 0;
    /* the length of a, including its terminator, at most n */
    while (count < n) {
        if (a[count++] == 0)
            break;
    }
    for (i = 0; i + 1 < count && a[i] == b[i]; i++) {
    }
    if ((uint8_t)b[i] > (uint8_t)a[i])
        return -1;
    if ((uint8_t)b[i] == (uint8_t)a[i])
        return 0;
    return 1;
}

/* 0x4980d0: strstr (a one-character needle goes through strchr 0x49b046). */
char *GAMEAPI msvc_strstr(const char *s, const char *find)
{
    uint32_t i;
    if (find[0] == 0)
        return (char *)s;
    for (;; s++) {
        for (i = 0; find[i] && s[i] == find[i]; i++) {
        }
        if (find[i] == 0)
            return (char *)s;
        if (*s == 0)
            return 0;
    }
}

/* ---- ctype ---- */

/* 0x498051: _isctype(c, mask): table lookup for -1..255, else the locale's
 * string type of the one or two bytes (__crtGetStringTypeA 0x49aee3). */
int32_t GAMEAPI msvc_isctype(int32_t c, uint32_t mask)
{
    char buf[4];
    uint16_t type = 0;
    int32_t n;
    if ((uint32_t)(c + 1) <= 0x100)
        return g_pctype[c] & mask;
    if (g_pctype[(uint8_t)(c >> 8)] & 0x8000) {
        buf[0] = (char)(c >> 8);
        buf[1] = (char)c;
        buf[2] = 0;
        n = 2;
    } else {
        buf[0] = (char)c;
        buf[1] = 0;
        n = 1;
    }
    if (crt_get_string_type(1, buf, n, &type, 0, 0, 1) == 0)
        return 0;
    return type & mask;
}

/* 0x498540: toupper (the "C" locale; a set locale goes through _toupper_lk). */
int32_t GAMEAPI msvc_toupper(int32_t c)
{
    int32_t locked = 0;
    if (g_lc_ctype == 0) {
        if (c >= 'a' && c <= 'z')
            c -= 0x20;
        return c;
    }
    InterlockedIncrement(GMEM(g_unguarded_readlc));
    if (g_setlc_active != 0) {
        InterlockedDecrement(GMEM(g_unguarded_readlc));
        crt_lock(0x13);
        locked = 1;
    }
    c = crt_toupper_lk(c);
    if (locked)
        crt_unlock(0x13);
    else
        InterlockedDecrement(GMEM(g_unguarded_readlc));
    return c;
}

/* 0x49867b: tolower, as toupper. */
int32_t GAMEAPI msvc_tolower(int32_t c)
{
    int32_t locked = 0;
    if (g_lc_ctype == 0) {
        if (c >= 'A' && c <= 'Z')
            c += 0x20;
        return c;
    }
    InterlockedIncrement(GMEM(g_unguarded_readlc));
    if (g_setlc_active != 0) {
        InterlockedDecrement(GMEM(g_unguarded_readlc));
        crt_lock(0x13);
        locked = 1;
    }
    c = crt_tolower_lk(c);
    if (locked)
        crt_unlock(0x13);
    else
        InterlockedDecrement(GMEM(g_unguarded_readlc));
    return c;
}

static int32_t is_space(uint32_t c)
{
    return g_mb_cur_max < 2 ? g_pctype[c] & 8 : msvc_isctype((int32_t)c, 8);
}

static int32_t is_digit(int32_t c)
{
    return g_mb_cur_max < 2 ? g_pctype[c] & 4 : msvc_isctype(c, 4);
}

/* 0x498046: atoi (atol 0x497fbb): optional spaces and sign, decimal digits. */
int32_t GAMEAPI msvc_atoi(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    uint32_t c, sign;
    int32_t total = 0;
    while (is_space(*p))
        p++;
    c = *p++;
    sign = c;
    if (c == '-' || c == '+')
        c = *p++;
    while (is_digit((int32_t)c)) {
        total = (int32_t)(c - '0') + total * 10;
        c = *p++;
    }
    return sign == '-' ? -total : total;
}

/* ---- rand, allocation, small helpers ---- */

/* 0x4987b5: rand: the MSVC LCG on the per-thread seed (ptd + 0x14, set to 1
 * by _initptd; the game never calls srand). */
int32_t GAMEAPI msvc_rand(void)
{
    uint8_t *ptd = crt_getptd();
    uint32_t seed = U32(ptd, 0x14) * 0x343fd + 0x269ec3;
    U32(ptd, 0x14) = seed;
    return (int32_t)((seed >> 16) & 0x7fff);
}

/* The runtime heap block: the size rounded up to 16 (0 counts as 1). */
static void *heap_block(uint32_t size, uint32_t flags)
{
    if (size == 0)
        size = 1;
    size = (size + 0xf) & ~0xfu;
    return HeapAlloc(GMEM(g_crt_heap), flags, size);
}

/* _callnewh 0x49ba20: the new handler, if one is set; nonzero to retry. */
static int32_t call_new_handler(uint32_t size)
{
    if (g_new_handler != 0 && ((int32_t(GAMEAPI *)(uint32_t))(uintptr_t)g_new_handler)(size) != 0)
        return 1;
    return 0;
}

/* 0x4982c2: malloc (__nh_malloc 0x4982d4 with _newmode). */
void *GAMEAPI msvc_malloc(uint32_t size)
{
    void *p;
    if (size > 0xffffffe0u)
        return 0;
    for (;;) {
        p = heap_block(size, 0);
        if (p != 0 || g_new_mode == 0)
            return p;
        if (call_new_handler(size) == 0)
            return 0;
    }
}

/* 0x49946c: calloc: zeroed (HEAP_ZERO_MEMORY), new handler retries. */
void *GAMEAPI msvc_calloc(uint32_t count, uint32_t size)
{
    uint32_t total = count * size, rounded = total;
    void *p;
    if (rounded <= 0xffffffe0u) {
        if (rounded == 0)
            rounded = 1;
        rounded = (rounded + 0xf) & ~0xfu;
    }
    for (;;) {
        p = 0;
        if (rounded <= 0xffffffe0u) {
            p = HeapAlloc(GMEM(g_crt_heap), 8, rounded);
            if (p != 0)
                return p;
        }
        if (g_new_mode == 0)
            return p;
        if (call_new_handler(rounded) == 0)
            return 0;
    }
}

/* 0x49827a: free. */
void GAMEAPI msvc_free(void *p)
{
    if (p != 0)
        HeapFree(GMEM(g_crt_heap), 0, p);
}

/* 0x498180: __allmul: 64-bit product (stdcall, edx:eax). */
uint64_t STDCALL msvc_allmul(uint64_t a, uint64_t b)
{
    return a * b;
}

/* 64-bit by 32-bit unsigned division (no compiler helper in the
 * freestanding emulator build). */
static uint64_t udiv64(uint64_t n, uint32_t d, uint32_t *rem)
{
    uint32_t hi = (uint32_t)(n >> 32), lo = (uint32_t)n;
    uint32_t qhi = hi / d, r = hi % d, qlo;
#if defined(__i386__)
    __asm__("divl %4" : "=a"(qlo), "=d"(r) : "a"(lo), "d"(r), "rm"(d));
#else
    {
        uint64_t x = ((uint64_t)r << 32) | lo;
        qlo = (uint32_t)(x / d);
        r = (uint32_t)(x % d);
    }
#endif
    if (rem)
        *rem = r;
    return ((uint64_t)qhi << 32) | qlo;
}

/* 0x499016: clock: 1/10000ths of the 100 ns file time since start-up
 * (_inittime stored the start at 0x4db2e8), i.e. milliseconds. */
int32_t GAMEAPI msvc_clock(void)
{
    uint32_t now[2];
    uint64_t t;
    GetSystemTimeAsFileTime((uint64_t *)now);
    t = ((uint64_t)now[1] << 32) - (((uint64_t)G32(0x4db2ec) << 32) | G32(0x4db2e8)) + now[0];
    return (int32_t)udiv64(t, 10000, 0);
}

/* ---- float to text (the runtime's _cfltcvt family and $I10_OUTPUT) ---- */

/* A 12-byte long double (_LDBL12): +0 16 extra low bits, +2 mantissa low
 * dword, +6 mantissa high dword, +10 sign and exponent. Kept as three
 * dwords so the shifts and adds work a dword at a time as the original's do. */
struct ld12 {
    uint32_t w[3];
};

#define LD12_U16(x, i) (((uint16_t *)(x)->w)[i])
#define LD12_EXP(x) LD12_U16(x, 5)

/* ___add_12 0x4a05d6 with __addl 0x4a05b5. */
static void ld12_add(struct ld12 *x, const struct ld12 *y)
{
    uint32_t s = x->w[0] + y->w[0];
    if (s < x->w[0] || s < y->w[0]) {
        uint32_t t = x->w[1] + 1;
        if (t < x->w[1])
            x->w[2]++;
        x->w[1] = t;
    }
    x->w[0] = s;
    s = x->w[1] + y->w[1];
    if (s < x->w[1] || s < y->w[1])
        x->w[2]++;
    x->w[1] = s;
    x->w[2] += y->w[2];
}

/* __shl_12 0x4a0634 and __shr_12 0x4a0662. */
static void ld12_shl(struct ld12 *x)
{
    uint32_t a = x->w[0], b = x->w[1];
    x->w[0] = a << 1;
    x->w[1] = (b << 1) | (a >> 31);
    x->w[2] = (x->w[2] << 1) | (b >> 31);
}

static void ld12_shr(struct ld12 *x)
{
    uint32_t b = x->w[1];
    x->w[1] = (b >> 1) | (x->w[2] << 31);
    x->w[2] >>= 1;
    x->w[0] = (x->w[0] >> 1) | (b << 31);
}

/* __ld12mul 0x4a1926: x *= y, rounded to the 12-byte format. */
static void ld12_mul(struct ld12 *x, const struct ld12 *y)
{
    uint16_t expx = LD12_EXP(x), expy = LD12_EXP(y);
    uint16_t sign = (uint16_t)((expx ^ expy) & 0x8000);
    uint16_t expsum;
    struct ld12 t = {{0, 0, 0}};
    int32_t i, j, sticky;
    expx &= 0x7fff;
    expy &= 0x7fff;
    expsum = (uint16_t)(expx + expy);
    if (expx >= 0x7fff || expy >= 0x7fff || expsum > 0xbffd) {
        x->w[0] = 0;
        x->w[1] = 0;
        x->w[2] = sign ? 0xffff8000u : 0x7fff8000u;
        return;
    }
    if (expsum <= 0x3fbf) {
        x->w[0] = x->w[1] = x->w[2] = 0;
        return;
    }
    if (expx == 0) {
        expsum++;
        if ((x->w[2] & 0x7fffffff) == 0 && x->w[1] == 0 && x->w[0] == 0) {
            LD12_EXP(x) = 0;
            return;
        }
    }
    if (expy == 0) {
        expsum++;
        if ((y->w[2] & 0x7fffffff) == 0 && y->w[1] == 0 && y->w[0] == 0) {
            x->w[0] = x->w[1] = x->w[2] = 0;
            return;
        }
    }
    for (i = 0; i < 5; i++) {
        uint8_t *r = (uint8_t *)t.w + 2 * i;
        for (j = 0; j < 5 - i; j++) {
            uint32_t prod = (uint32_t)LD12_U16(x, i + j) * LD12_U16(y, 4 - j);
            uint32_t old = *(uint32_t *)r, sum = old + prod;
            *(uint32_t *)r = sum;
            if (sum < old || sum < prod)
                (*(uint16_t *)(r + 4))++;
        }
    }
    expsum = (uint16_t)(expsum + 0xc002);
    while ((int16_t)expsum > 0 && (t.w[2] & 0x80000000u) == 0) {
        ld12_shl(&t);
        expsum--;
    }
    if ((int16_t)expsum <= 0) {
        expsum--;
        sticky = 0;
        while ((int16_t)expsum < 0) {
            if (LD12_U16(&t, 0) & 1)
                sticky++;
            ld12_shr(&t);
            expsum++;
        }
        if (sticky)
            LD12_U16(&t, 0) |= 1;
    }
    if (LD12_U16(&t, 0) > 0x8000 || (t.w[0] & 0x1ffff) == 0x18000) {
        uint32_t lo = (uint32_t)LD12_U16(&t, 1) | ((uint32_t)LD12_U16(&t, 2) << 16);
        uint32_t mid = (uint32_t)LD12_U16(&t, 3) | ((uint32_t)LD12_U16(&t, 4) << 16);
        if (lo == 0xffffffffu) {
            lo = 0;
            if (mid == 0xffffffffu) {
                mid = 0;
                if (LD12_U16(&t, 5) == 0xffff) {
                    LD12_U16(&t, 5) = 0x8000;
                    expsum++;
                } else {
                    LD12_U16(&t, 5)++;
                }
            } else {
                mid++;
            }
        } else {
            lo++;
        }
        LD12_U16(&t, 1) = (uint16_t)lo;
        LD12_U16(&t, 2) = (uint16_t)(lo >> 16);
        LD12_U16(&t, 3) = (uint16_t)mid;
        LD12_U16(&t, 4) = (uint16_t)(mid >> 16);
    }
    if (expsum >= 0x7fff) {
        x->w[0] = 0;
        x->w[1] = 0;
        x->w[2] = sign ? 0xffff8000u : 0x7fff8000u;
        return;
    }
    LD12_U16(x, 0) = LD12_U16(&t, 1);
    LD12_U16(x, 1) = LD12_U16(&t, 2);
    LD12_U16(x, 2) = LD12_U16(&t, 3);
    LD12_U16(x, 3) = LD12_U16(&t, 4);
    LD12_U16(x, 4) = LD12_U16(&t, 5);
    LD12_EXP(x) = (uint16_t)(expsum | sign);
}

/* __multtenpow12 0x4a1b46: x *= 10^pow, from the runtime's power tables
 * (_pow10pos 0x4b5b18, _pow10neg 0x4b5c78: 12-byte entries, 7 per octal
 * digit of the power). An entry whose extra bits round up is used with its
 * mantissa low dword decremented. */
static void ld12_mul_pow10(struct ld12 *x, int32_t pow, int32_t mult12)
{
    const uint8_t *table = (const uint8_t *)0x4b5ab8;
    if (pow == 0)
        return;
    if (pow < 0) {
        pow = -pow;
        table = (const uint8_t *)0x4b5c18;
    }
    if (mult12 == 0)
        LD12_U16(x, 0) = 0;
    while (pow != 0) {
        int32_t last = pow & 7;
        table += 0x54;
        pow >>= 3;
        if (last != 0) {
            struct ld12 y;
            memcpy(&y, table + last * 12, 12);
            if (LD12_U16(&y, 0) >= 0x8000) {
                uint32_t lo = (uint32_t)LD12_U16(&y, 1) | ((uint32_t)LD12_U16(&y, 2) << 16);
                lo--;
                LD12_U16(&y, 1) = (uint16_t)lo;
                LD12_U16(&y, 2) = (uint16_t)(lo >> 16);
            }
            ld12_mul(x, &y);
        }
    }
}

/* The converter's output (FOS): exponent, sign, digit count, digits. */
struct fos {
    int16_t exp;
    char sign;
    char len;
    char man[24];
};

/* $I10_OUTPUT 0x4a0c27: an 80-bit value (mantissa lo, hi; sign and exponent)
 * to at most 21 decimal digits, rounded on the next digit. Returns 0 for
 * infinities and NaNs ("1#INF" ...). */
static int32_t i10_output(uint32_t manlo, uint32_t manhi, uint16_t sexp, int32_t ndigits, uint32_t flags,
                          struct fos *out)
{
    uint16_t expn = sexp & 0x7fff;
    struct ld12 v, tenth = {{0xcccccccc, 0xcccccccc, 0x3ffbcccc}};
    int16_t ir;
    int32_t shift, n;
    char *p, *q;
    out->sign = (sexp & 0x8000) ? '-' : ' ';
    if (expn == 0 && manhi == 0 && manlo == 0)
        goto zero;
    if (expn == 0x7fff) {
        const char *s;
        int32_t len;
        out->exp = 1;
        if ((manhi == 0x80000000u && manlo == 0) || (manhi & 0x40000000u)) {
            if ((sexp & 0x8000) && manhi == 0xc0000000u) {
                if (manlo != 0) {
                    s = "1#QNAN";
                    len = 6;
                } else {
                    s = "1#IND";
                    len = 5;
                }
            } else if (manhi == 0x80000000u && manlo == 0) {
                s = "1#INF";
                len = 5;
            } else {
                s = "1#QNAN";
                len = 6;
            }
        } else {
            s = "1#SNAN";
            len = 6;
        }
        msvc_strcpy(out->man, s);
        out->len = (char)len;
        return 0;
    }
    v.w[0] = manlo << 16;
    v.w[1] = (manlo >> 16) | (manhi << 16);
    v.w[2] = (manhi >> 16) | ((uint32_t)expn << 16);
    ir = (int16_t)(((((uint32_t)expn >> 8) + (manhi >> 24) * 2) * 0x4d - 0x134312f4u + (uint32_t)expn * 0x4d10) >> 16);
    ld12_mul_pow10(&v, -ir, 1);
    if (LD12_EXP(&v) >= 0x3fff) {
        ir++;
        ld12_mul(&v, &tenth);
    }
    out->exp = ir;
    n = ndigits;
    if (flags & 1) {
        n = ndigits + ir;
        if (n <= 0)
            goto zero;
    }
    if (n > 21)
        n = 21;
    shift = (int32_t)LD12_EXP(&v) - 0x3ffe;
    LD12_EXP(&v) = 0;
    {
        int32_t k;
        for (k = 0; k < 8; k++)
            ld12_shl(&v);
        if (shift < 0)
            for (k = (-shift) & 0xff; k != 0; k--)
                ld12_shr(&v);
    }
    p = out->man;
    for (n = n + 1; n > 0; n--) {
        struct ld12 t = v;
        ld12_shl(&v);
        ld12_shl(&v);
        ld12_add(&v, &t);
        ld12_shl(&v);
        *p++ = (char)((v.w[2] >> 24) + '0');
        v.w[2] &= 0x00ffffff;
    }
    q = p - 2;
    if (p[-1] > '4') {
        for (; q >= out->man; q--) {
            if (*q != '9')
                break;
            *q = '0';
        }
        if (q < out->man) {
            q++;
            out->exp++;
        }
        (*q)++;
    } else {
        for (; q >= out->man; q--)
            if (*q != '0')
                break;
        if (q < out->man) {
            out->exp = 0;
            out->sign = ' ';
            out->len = 1;
            out->man[0] = '0';
            out->man[1] = 0;
            return 1;
        }
    }
    out->len = (char)(q - out->man + 1);
    out->man[(int32_t)out->len] = 0;
    return 1;
zero:
    out->exp = 0;
    out->sign = ' ';
    out->len = 1;
    out->man[0] = '0';
    out->man[1] = 0;
    return 1;
}

/* _fltout2's decoded number (STRFLT). */
struct strflt {
    int32_t sign;
    int32_t decpt;
    int32_t flag;
    char *mantissa;
};

/* _fltout2 0x49f4d9 with __dtold 0x49f535: a double to 17 digits. */
static void flt_out(const uint32_t *d, struct strflt *flt, char *result)
{
    uint32_t lo = d[0], hi = d[1], manlo, manhi;
    uint16_t top = (uint16_t)(hi >> 16), exp;
    uint32_t e = (top & 0x7ff0) >> 4, bit = 0x80000000u;
    struct fos fos;
    if (e == 0) {
        if ((hi & 0xfffff) == 0 && lo == 0) {
            manlo = manhi = 0;
            exp = 0;
            goto convert;
        }
        e = 0x3c01;
        bit = 0;
    } else if (e == 0x7ff) {
        e = 0x7fff;
    } else {
        e += 0x3c00;
    }
    manhi = (lo >> 21) | ((hi & 0xfffff) << 11) | bit;
    manlo = lo << 11;
    while ((manhi & 0x80000000u) == 0) {
        manhi = (manlo >> 31) | (manhi << 1);
        manlo <<= 1;
        e = (e + 0xffff) & 0xffff;
    }
    exp = (uint16_t)((top & 0x8000) | (uint16_t)e);
convert:
    flt->flag = i10_output(manlo, manhi, exp, 17, 0, &fos);
    flt->sign = fos.sign;
    flt->decpt = fos.exp;
    msvc_strcpy(result, fos.man);
    flt->mantissa = result;
}

/* _fptostr 0x49f462: digits of the mantissa into buf, rounded. */
static void fp_to_str(char *buf, int32_t digits, struct strflt *flt)
{
    char *p = buf + 1, *m = flt->mantissa;
    buf[0] = '0';
    if (digits > 0) {
        int32_t k = digits;
        digits = 0;
        do {
            char c = *m;
            if (c == 0)
                c = '0';
            else
                m++;
            *p++ = c;
        } while (--k);
    }
    *p = 0;
    if (digits >= 0 && *m > '4') {
        while (*--p == '9')
            *p = '0';
        (*p)++;
    }
    if (buf[0] == '1')
        flt->decpt++;
    else
        mem_move(buf, buf + 1, str_length(buf + 1) + 1);
}

/* shift 0x499c21: move the string at s right by dist. */
static void str_shift(char *s, int32_t dist)
{
    if (dist != 0)
        mem_move(s + dist, s, str_length(s) + 1);
}

/* _cftoe2 0x49997f: exponent form from the digits in buf. */
static void cftoe2(char *buf, int32_t ndec, int32_t caps, struct strflt *flt, int32_t g_fmt)
{
    char *p;
    int32_t exp;
    if (g_fmt)
        str_shift(buf + (flt->sign == '-'), ndec > 0);
    if (flt->sign == '-')
        *buf++ = '-';
    p = buf;
    if (ndec > 0) {
        p[0] = p[1];
        *++p = (char)g_decimal_point;
    }
    p = msvc_strcpy(p + ndec + (g_fmt == 0), "e+000");
    if (caps)
        *p = 'E';
    if (*flt->mantissa != '0') {
        exp = flt->decpt - 1;
        if (exp < 0) {
            exp = -exp;
            p[1] = '-';
        }
        if (exp > 99) {
            p[2] += (char)(exp / 100);
            exp %= 100;
        }
        if (exp > 9) {
            p[3] += (char)(exp / 10);
            exp %= 10;
        }
        p[4] += (char)exp;
    }
}

/* _cftof2 0x499a96: fixed form from the digits in buf. */
static void cftof2(char *buf, int32_t ndec, struct strflt *flt, int32_t g_fmt)
{
    char *p;
    int32_t decpt = flt->decpt;
    if (g_fmt && decpt - 1 == ndec) {
        p = buf + (flt->sign == '-') + (decpt - 1);
        p[0] = '0';
        p[1] = 0;
    }
    p = buf;
    if (flt->sign == '-')
        *p++ = '-';
    if (flt->decpt < 1) {
        str_shift(p, 1);
        *p++ = '0';
    } else {
        p += flt->decpt;
    }
    if (ndec > 0) {
        str_shift(p, 1);
        *p = (char)g_decimal_point;
        decpt = flt->decpt;
        if (decpt < 0) {
            if (g_fmt || -decpt <= ndec)
                ndec = -decpt;
            str_shift(p + 1, ndec);
            memset(p + 1, '0', (uint32_t)ndec);
        }
    }
}

/* _cfltcvt 0x499bd0 (_cftoe 0x49991e, _cftof 0x499a41, _cftog 0x499b3d). */
static void cfltcvt(const uint32_t *value, char *buf, int32_t format, int32_t precision, int32_t caps)
{
    struct strflt flt;
    char digits[24];
    uint32_t v[2];
    v[0] = value[0];
    v[1] = value[1];
    /* the original passes the double through an x87 load and store, which
     * quiets a signalling NaN */
    if ((v[1] & 0x7ff00000) == 0x7ff00000 && ((v[1] & 0xfffff) | v[0]) != 0)
        v[1] |= 0x80000;
    flt_out(v, &flt, digits);
    if (format == 'e' || format == 'E') {
        fp_to_str(buf + (precision > 0) + (flt.sign == '-'), precision + 1, &flt);
        cftoe2(buf, precision, caps, &flt, 0);
    } else if (format == 'f') {
        fp_to_str(buf + (flt.sign == '-'), flt.decpt + precision, &flt);
        cftof2(buf, precision, &flt, 0);
    } else {
        int32_t magnitude = flt.decpt - 1;
        char *p = buf + (flt.sign == '-');
        fp_to_str(p, precision, &flt);
        if (flt.decpt - 1 < -4 || flt.decpt - 1 >= precision) {
            cftoe2(buf, precision, caps, &flt, 1);
        } else {
            if (magnitude < flt.decpt - 1) { /* the rounding added a digit */
                while (*p++) {
                }
                p[-2] = 0;
            }
            cftof2(buf, precision, &flt, 1);
        }
    }
}

/* _forcdecpt 0x499820: put a decimal point after the leading digits. */
static void force_decimal_point(char *buf)
{
    char hold, next;
    if (msvc_tolower(*buf) != 'e') {
        do
            buf++;
        while (is_digit(*buf));
    }
    hold = *buf;
    *buf = (char)g_decimal_point;
    do {
        buf++;
        next = *buf;
        *buf = hold;
        hold = next;
    } while (*buf);
}

/* _cropzeros 0x49987a: drop trailing fraction zeros (and a bare point). */
static void crop_zeros(char *buf)
{
    char *stop;
    while (*buf && *buf != (char)g_decimal_point)
        buf++;
    if (*buf) {
        do
            buf++;
        while (*buf && *buf != 'e' && *buf != 'E');
        stop = buf;
        do
            buf--;
        while (*buf == '0');
        if (*buf == (char)g_decimal_point)
            buf--;
        do
            *++buf = *stop;
        while (*stop++);
    }
}

/* ---- sprintf / vsprintf: the runtime's _output 0x499d5e on a string ---- */

/* wctomb 0x49f99b (with _wctomb_lk 0x49f9f4): one wide character to the
 * locale's multibyte form; in the "C" locale bytes only (EILSEQ above). */
static int32_t wide_to_mb(char *out, uint16_t wc)
{
    int32_t locked = 0, r;
    InterlockedIncrement(GMEM(g_unguarded_readlc));
    if (g_setlc_active != 0) {
        InterlockedDecrement(GMEM(g_unguarded_readlc));
        crt_lock(0x13);
        locked = 1;
    }
    if (out == 0) {
        r = 0;
    } else if (g_lc_ctype == 0) {
        if (wc < 0x100) {
            *out = (char)wc;
            r = 1;
        } else {
            *crt_errno() = 0x2a;
            r = -1;
        }
    } else {
        int32_t used = 0;
        r = WideCharToMultiByte(G32(0x4db38c), 0x220, &wc, 1, out, g_mb_cur_max, 0, &used);
        if (r == 0 || used != 0) {
            *crt_errno() = 0x2a;
            r = -1;
        }
    }
    if (locked)
        crt_unlock(0x13);
    else
        InterlockedDecrement(GMEM(g_unguarded_readlc));
    return r;
}

/* The string "stream" (a FILE with _IOWRT | _IOSTRG): room for cnt more
 * characters (sprintf: 0x7fffffff); once it is full, _flsbuf 0x499c46 fails
 * on a string and the count becomes -1, which stops the output. */
struct sink {
    char *ptr;
    int32_t cnt;
    int32_t count;
};

static void out_char(struct sink *s, int32_t c)
{
    if (--s->cnt >= 0) {
        *s->ptr++ = (char)c;
        s->count++;
    } else {
        s->count = -1;
    }
}

static void out_repeat(struct sink *s, int32_t c, int32_t n)
{
    while (n-- > 0) {
        out_char(s, c);
        if (s->count == -1)
            break;
    }
}

static void out_string(struct sink *s, const char *p, int32_t n)
{
    while (n-- > 0) {
        out_char(s, *p++);
        if (s->count == -1)
            break;
    }
}

/* The terminator: written if there is room (else _flsbuf fails, sic). */
static void out_end(struct sink *s)
{
    if (--s->cnt >= 0)
        *s->ptr = 0;
}

enum { ST_NORMAL, ST_PERCENT, ST_FLAG, ST_WIDTH, ST_DOT, ST_PRECIS, ST_SIZE, ST_TYPE };

/* The format grammar: the class of a character ' '..'x' (0 other, 1 '%',
 * 2 '.', 3 '*', 4 '0', 5 digit, 6 flag, 7 size, 8 type) and the next state
 * for each class and state (the runtime's __lookuptable 0x4a4d28). */
static int32_t char_class(uint8_t c)
{
    if (c < ' ' || c > 'x')
        return 0;
    switch (c) {
    case '%':
        return 1;
    case '.':
        return 2;
    case '*':
        return 3;
    case '0':
        return 4;
    case ' ':
    case '#':
    case '+':
    case '-':
        return 6;
    case 'F':
    case 'I':
    case 'L':
    case 'N':
    case 'h':
    case 'l':
    case 'w':
        return 7;
    case 'B':
    case 'C':
    case 'E':
    case 'G':
    case 'S':
    case 'X':
    case 'Z':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'i':
    case 'n':
    case 'o':
    case 'p':
    case 's':
    case 'u':
    case 'x':
        return 8;
    }
    return c >= '1' && c <= '9' ? 5 : 0;
}

static const uint8_t next_state[9][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 0, 0, 1}, {0, 4, 4, 4, 0, 0, 0, 0},
    {0, 3, 3, 0, 5, 0, 0, 0}, {0, 2, 2, 3, 5, 5, 0, 0}, {0, 3, 3, 3, 5, 5, 0, 0},
    {0, 2, 2, 0, 0, 0, 0, 0}, {0, 6, 6, 6, 6, 6, 6, 0}, {0, 7, 7, 7, 7, 7, 7, 0},
};

#define FL_SIGN 0x1
#define FL_SIGNSP 0x2
#define FL_LEFT 0x4
#define FL_LEADZERO 0x8
#define FL_LONG 0x10
#define FL_SHORT 0x20
#define FL_SIGNED 0x40
#define FL_ALTERNATE 0x80
#define FL_NEGATIVE 0x100
#define FL_FORCEOCTAL 0x200
#define FL_WIDECHAR 0x800
#define FL_I64 0x8000

static int32_t arg_int(const uint8_t **ap)
{
    int32_t v = *(const int32_t *)*ap;
    *ap += 4;
    return v;
}

static int32_t output(struct sink *s, const char *format, const uint8_t *ap)
{
    char buffer[512];
    const char *fmt = format;
    char *text = 0;
    int32_t textlen = 0, state = ST_NORMAL, flags = 0, fldwidth = 0, precision = 0, prefixlen = 0;
    int32_t capexp = 0, no_output = 0, bufferiswide = 0, hexadd = 0, radix = 0;
    char prefix[2] = {0, 0};
    uint8_t ch;
    s->count = 0;
    while ((ch = (uint8_t)*fmt) != 0) {
        fmt++;
        if (s->count < 0)
            break;
        state = next_state[char_class(ch)][state];
        switch (state) {
        case ST_NORMAL:
            bufferiswide = 0;
            if (g_pctype[ch] & 0x8000) { /* lead byte: copy the pair */
                out_char(s, (int8_t)ch);
                ch = (uint8_t)*fmt++;
            }
            out_char(s, (int8_t)ch);
            break;
        case ST_PERCENT:
            precision = -1;
            capexp = 0;
            no_output = 0;
            fldwidth = 0;
            prefixlen = 0;
            flags = 0;
            bufferiswide = 0;
            break;
        case ST_FLAG:
            if (ch == ' ')
                flags |= FL_SIGNSP;
            else if (ch == '#')
                flags |= FL_ALTERNATE;
            else if (ch == '+')
                flags |= FL_SIGN;
            else if (ch == '-')
                flags |= FL_LEFT;
            else if (ch == '0')
                flags |= FL_LEADZERO;
            break;
        case ST_WIDTH:
            if (ch == '*') {
                fldwidth = arg_int(&ap);
                if (fldwidth < 0) {
                    flags |= FL_LEFT;
                    fldwidth = -fldwidth;
                }
            } else {
                fldwidth = fldwidth * 10 + ((int8_t)ch - '0');
            }
            break;
        case ST_DOT:
            precision = 0;
            break;
        case ST_PRECIS:
            if (ch == '*') {
                precision = arg_int(&ap);
                if (precision < 0)
                    precision = -1;
            } else {
                precision = precision * 10 + ((int8_t)ch - '0');
            }
            break;
        case ST_SIZE:
            if (ch == 'I') {
                if (fmt[0] == '6' && fmt[1] == '4') {
                    fmt += 2;
                    flags |= FL_I64;
                } else { /* not I64: an ordinary character */
                    state = ST_NORMAL;
                    bufferiswide = 0;
                    if (g_pctype[ch] & 0x8000) {
                        out_char(s, (int8_t)ch);
                        ch = (uint8_t)*fmt++;
                    }
                    out_char(s, (int8_t)ch);
                }
            } else if (ch == 'h') {
                flags |= FL_SHORT;
            } else if (ch == 'l') {
                flags |= FL_LONG;
            } else if (ch == 'w') {
                flags |= FL_WIDECHAR;
            }
            break;
        case ST_TYPE: {
            int32_t is_number = 0;
            switch (ch) {
            case 'C':
                if ((flags & (FL_SHORT | FL_LONG | FL_WIDECHAR)) == 0)
                    flags |= FL_WIDECHAR;
                /* fall through */
            case 'c':
                if (flags & (FL_LONG | FL_WIDECHAR)) {
                    uint16_t wc = *(const uint16_t *)ap;
                    ap += 4;
                    textlen = wide_to_mb(buffer, wc);
                    if (textlen < 0)
                        no_output = 1;
                } else {
                    buffer[0] = (char)arg_int(&ap);
                    textlen = 1;
                }
                text = buffer;
                break;
            case 'Z': {
                const uint8_t *str = (const uint8_t *)(uintptr_t)(uint32_t)arg_int(&ap);
                if (str == 0 || PTR(str, 4) == 0) {
                    text = (char *)g_null_string;
                    textlen = (int32_t)str_length(text);
                } else if (flags & FL_WIDECHAR) {
                    text = (char *)PTR(str, 4);
                    textlen = (int32_t)((uint32_t)(int32_t)S16(str, 0) >> 1);
                    bufferiswide = 1;
                } else {
                    text = (char *)PTR(str, 4);
                    textlen = S16(str, 0);
                    bufferiswide = 0;
                }
                break;
            }
            case 'S':
                if ((flags & (FL_SHORT | FL_LONG | FL_WIDECHAR)) == 0)
                    flags |= FL_WIDECHAR;
                /* fall through */
            case 's': {
                int32_t i = precision == -1 ? 0x7fffffff : precision;
                text = (char *)(uintptr_t)(uint32_t)arg_int(&ap);
                if (flags & (FL_LONG | FL_WIDECHAR)) {
                    const uint16_t *w;
                    if (text == 0)
                        text = (char *)g_null_wstring;
                    bufferiswide = 1;
                    for (w = (const uint16_t *)text; i != 0 && *w; w++)
                        i--;
                    textlen = (int32_t)(w - (const uint16_t *)text);
                } else {
                    const char *p;
                    if (text == 0)
                        text = (char *)g_null_string;
                    for (p = text; i != 0 && *p; p++)
                        i--;
                    textlen = (int32_t)(p - text);
                }
                break;
            }
            case 'n': {
                uint8_t *p = (uint8_t *)(uintptr_t)(uint32_t)arg_int(&ap);
                if (flags & FL_SHORT)
                    *(int16_t *)p = (int16_t)s->count;
                else
                    *(int32_t *)p = s->count;
                no_output = 1;
                break;
            }
            case 'E':
            case 'G':
                capexp = 1;
                ch = (uint8_t)(ch + 0x20);
                /* fall through */
            case 'e':
            case 'f':
            case 'g': {
                uint32_t value[2];
                flags |= FL_SIGNED;
                text = buffer;
                if (precision < 0)
                    precision = 6;
                else if (precision == 0 && ch == 'g')
                    precision = 1;
                value[0] = *(const uint32_t *)ap;
                value[1] = *(const uint32_t *)(ap + 4);
                ap += 8;
                cfltcvt(value, buffer, ch, precision, capexp);
                if ((flags & FL_ALTERNATE) && precision == 0)
                    force_decimal_point(buffer);
                if (ch == 'g' && (flags & FL_ALTERNATE) == 0)
                    crop_zeros(buffer);
                if (buffer[0] == '-') {
                    flags |= FL_NEGATIVE;
                    text = buffer + 1;
                }
                textlen = (int32_t)str_length(text);
                break;
            }
            case 'd':
            case 'i':
                flags |= FL_SIGNED;
                radix = 10;
                is_number = 1;
                break;
            case 'u':
                radix = 10;
                is_number = 1;
                break;
            case 'p':
                precision = 8;
                /* fall through */
            case 'X':
                hexadd = 7;
                goto hex;
            case 'x':
                hexadd = 0x27;
            hex:
                radix = 16;
                if (flags & FL_ALTERNATE) {
                    prefix[0] = '0';
                    prefix[1] = (char)(hexadd + 'Q');
                    prefixlen = 2;
                }
                is_number = 1;
                break;
            case 'o':
                radix = 8;
                if (flags & FL_ALTERNATE)
                    flags |= FL_FORCEOCTAL;
                is_number = 1;
                break;
            default: /* 'B': nothing converted; the last text is printed again (sic) */
                break;
            }
            if (is_number) {
                uint64_t number;
                char *p;
                if (flags & FL_I64) {
                    number = *(const uint64_t *)ap;
                    ap += 8;
                } else if (flags & FL_SHORT) {
                    if (flags & FL_SIGNED)
                        number = (uint64_t)(int64_t)(int16_t)arg_int(&ap);
                    else
                        number = (uint16_t)arg_int(&ap);
                } else if (flags & FL_SIGNED) {
                    number = (uint64_t)(int64_t)arg_int(&ap);
                } else {
                    number = (uint32_t)arg_int(&ap);
                }
                if ((flags & FL_SIGNED) && (int64_t)number < 0) {
                    number = (uint64_t)0 - number;
                    flags |= FL_NEGATIVE;
                }
                if ((flags & FL_I64) == 0)
                    number &= 0xffffffffu;
                if (precision < 0)
                    precision = 1;
                else
                    flags &= ~FL_LEADZERO;
                if (number == 0)
                    prefixlen = 0;
                p = buffer + 511;
                while (precision-- > 0 || number != 0) {
                    uint32_t digit;
                    number = udiv64(number, (uint32_t)radix, &digit);
                    digit += '0';
                    if (digit > '9')
                        digit += (uint32_t)hexadd;
                    *p-- = (char)digit;
                }
                textlen = (int32_t)(buffer + 511 - p);
                text = p + 1;
                if ((flags & FL_FORCEOCTAL) && (text[0] != '0' || textlen == 0)) {
                    *--text = '0';
                    textlen++;
                }
            }
            if (!no_output) {
                int32_t padding;
                if (flags & FL_SIGNED) {
                    if (flags & FL_NEGATIVE) {
                        prefix[0] = '-';
                        prefixlen = 1;
                    } else if (flags & FL_SIGN) {
                        prefix[0] = '+';
                        prefixlen = 1;
                    } else if (flags & FL_SIGNSP) {
                        prefix[0] = ' ';
                        prefixlen = 1;
                    }
                }
                padding = fldwidth - prefixlen - textlen;
                if ((flags & (FL_LEFT | FL_LEADZERO)) == 0)
                    out_repeat(s, ' ', padding);
                out_string(s, prefix, prefixlen);
                if ((flags & FL_LEADZERO) && (flags & FL_LEFT) == 0)
                    out_repeat(s, '0', padding);
                if (bufferiswide && textlen > 0) {
                    const uint16_t *w = (const uint16_t *)text;
                    int32_t n = textlen;
                    char mb[4];
                    do {
                        int32_t len;
                        n--;
                        len = wide_to_mb(mb, *w);
                        if (len <= 0)
                            break;
                        out_string(s, mb, len);
                        w++;
                    } while (n != 0);
                } else {
                    out_string(s, text, textlen);
                }
                if (flags & FL_LEFT)
                    out_repeat(s, ' ', padding);
            }
            break;
        }
        }
    }
    return s->count;
}

#if !defined(__i386__)
/* x86-64: the variadic arguments as the 32-bit stack words output() reads,
 * walking the format with output()'s own grammar: '*' widths and
 * precisions, %c %C and the integers int-sized (I64: 64 bits, two words),
 * %s %S %Z %n %p... pointers (game memory or our stack, below 4 GB, one
 * word), %e %f %g %E %G doubles (two words). */
uint32_t crt_args_pack(const char *fmt, __builtin_va_list ap, uint32_t *words, uint32_t max); /* crt_cc.c's printf too */
uint32_t crt_args_pack(const char *fmt, __builtin_va_list ap, uint32_t *words, uint32_t max)
{
    uint32_t n = 0;
    int32_t state = ST_NORMAL, i64 = 0;
    uint8_t ch;

    while ((ch = (uint8_t)*fmt) != 0 && n + 2 <= max) {
        fmt++;
        state = next_state[char_class(ch)][state];
        switch (state) {
        case ST_NORMAL:
            if (g_pctype[ch] & 0x8000)
                fmt++;
            break;
        case ST_PERCENT:
            i64 = 0;
            break;
        case ST_WIDTH:
        case ST_PRECIS:
            if (ch == '*')
                words[n++] = (uint32_t)__builtin_va_arg(ap, int);
            break;
        case ST_SIZE:
            if (ch == 'I') {
                if (fmt[0] == '6' && fmt[1] == '4') {
                    fmt += 2;
                    i64 = 1;
                } else {
                    state = ST_NORMAL;
                    if (g_pctype[ch] & 0x8000)
                        fmt++;
                }
            }
            break;
        case ST_TYPE:
            switch (ch) {
            case 'Z': case 's': case 'S': case 'n':
                words[n++] = (uint32_t)(uintptr_t)__builtin_va_arg(ap, const void *);
                break;
            case 'e': case 'f': case 'g': case 'E': case 'G': {
                uint64_t q;
                double d = __builtin_va_arg(ap, double);
                __builtin_memcpy(&q, &d, 8);
                words[n++] = (uint32_t)q;
                words[n++] = (uint32_t)(q >> 32);
                break;
            }
            case 'c': case 'C':
                words[n++] = (uint32_t)__builtin_va_arg(ap, int);
                break;
            case 'd': case 'i': case 'u': case 'p': case 'X': case 'x': case 'o':
                if (i64) {
                    uint64_t q = __builtin_va_arg(ap, uint64_t);
                    words[n++] = (uint32_t)q;
                    words[n++] = (uint32_t)(q >> 32);
                } else if (ch == 'p') {
                    words[n++] = (uint32_t)(uintptr_t)__builtin_va_arg(ap, const void *);
                } else {
                    words[n++] = (uint32_t)__builtin_va_arg(ap, int);
                }
                break;
            default: /* 'B': no argument */
                break;
            }
            break;
        }
    }
    return n;
}
#endif

/* 0x4979f0: sprintf. */
int32_t GAMEAPI msvc_sprintf(char *buf, const char *format, ...)
{
    struct sink s;
    __builtin_va_list ap;
    int32_t n;
    __builtin_va_start(ap, format);
    s.ptr = buf;
    s.cnt = 0x7fffffff;
#if defined(__i386__)
    n = output(&s, format, (const uint8_t *)ap);
#else
    {
        uint32_t words[128];

        crt_args_pack(format, ap, words, 128);
        n = output(&s, format, (const uint8_t *)words);
    }
#endif
    __builtin_va_end(ap);
    out_end(&s);
    return n;
}

/* 0x498c0b: vsprintf (the arguments as a pointer to the stack words). */
int32_t GAMEAPI msvc_vsprintf(char *buf, const char *format, const uint8_t *args)
{
    struct sink s;
    int32_t n;
    s.ptr = buf;
    s.cnt = 0x7fffffff;
    n = output(&s, format, args);
    out_end(&s);
    return n;
}

/* 0x49972a: _vsnprintf: at most count characters; -1 if they did not all
 * fit (then no terminator either). */
int32_t GAMEAPI msvc_vsnprintf(char *buf, int32_t count, const char *format, const uint8_t *args)
{
    struct sink s;
    int32_t n;
    s.ptr = buf;
    s.cnt = count;
    n = output(&s, format, args);
    out_end(&s);
    return n;
}

/* ---- paths, directories, threads, exit ---- */

/* _mbsdec 0x49cea2 for a single-byte code page: the previous character. */
static const uint8_t *mb_dec(const uint8_t *start, const uint8_t *p)
{
    if (g_ismbcodepage != 0)
        return crt_mbsdec(start, p);
    return start < p ? p - 1 : 0;
}

/* _mbsnbcpy 0x49d303: strncpy for a single-byte code page. */
static void mb_ncopy(char *dst, const char *src, uint32_t n)
{
    if (g_ismbcodepage != 0)
        crt_mbsnbcpy((uint8_t *)dst, (const uint8_t *)src, n);
    else
        msvc_strncpy(dst, src, n);
}

/* 0x498834: _makepath: drive ':', dir (with a '\\' added unless it ends in
 * a slash), file name, '.' and extension. */
void GAMEAPI msvc_makepath(char *path, const char *drive, const char *dir, const char *fname, const char *ext)
{
    char *p = path;
    if (drive != 0 && *drive != 0) {
        *p++ = *drive;
        *p++ = ':';
    }
    if (dir != 0 && *dir != 0) {
        const char *q = dir;
        uint8_t last;
        do
            *p++ = *q++;
        while (*q);
        last = *mb_dec((const uint8_t *)dir, (const uint8_t *)q);
        if (last != '/' && last != '\\')
            *p++ = '\\';
    }
    if (fname != 0) {
        while (*fname)
            *p++ = *fname++;
    }
    if (ext != 0) {
        char c;
        if (*ext != 0 && *ext != '.')
            *p++ = '.';
        do {
            c = *ext++;
            *p++ = c;
        } while (c);
    } else {
        *p = 0;
    }
}

/* 0x4988c5: _splitpath: drive, directory (up to the last slash), file name
 * and extension (from the last '.'), each at most 255 characters. */
void GAMEAPI msvc_splitpath(const char *path, char *drive, char *dir, char *fname, char *ext)
{
    const char *p, *end, *last_slash = 0, *dot = 0;
    uint32_t len;
    if (str_length(path) >= 1 && path[1] == ':') {
        if (drive != 0) {
            mb_ncopy(drive, path, 2);
            drive[2] = 0;
        }
        path += 2;
    } else if (drive != 0) {
        *drive = 0;
    }
    p = path;
    end = path;
    if (*path != 0) {
        do {
            uint8_t c = (uint8_t)*p;
            if (g_mbctype[c] & 4)
                p++; /* lead byte: skip its trail byte */
            else if (c == '/' || c == '\\')
                last_slash = p + 1;
            else if (c == '.')
                dot = p;
            p++;
        } while (*p);
        end = p;
    }
    if (last_slash != 0) {
        if (dir != 0) {
            len = (uint32_t)(last_slash - path);
            if (len >= 0xff)
                len = 0xff;
            mb_ncopy(dir, path, len);
            dir[len] = 0;
        }
        path = last_slash;
    } else if (dir != 0) {
        *dir = 0;
    }
    if (dot != 0 && dot >= path) {
        if (fname != 0) {
            len = (uint32_t)(dot - path);
            if (len >= 0xff)
                len = 0xff;
            mb_ncopy(fname, path, len);
            fname[len] = 0;
        }
        if (ext != 0) {
            len = (uint32_t)(end - dot);
            if (len >= 0xff)
                len = 0xff;
            mb_ncopy(ext, dot, len);
            ext[len] = 0;
        }
    } else {
        if (fname != 0) {
            len = (uint32_t)(end - path);
            if (len >= 0xff)
                len = 0xff;
            mb_ncopy(fname, path, len);
            fname[len] = 0;
        }
        if (ext != 0)
            *ext = 0;
    }
}

/* 0x498a0c: _getcwd (_getdcwd 0x498a33 for the current drive, under the
 * runtime's lock 0xc): into buf, or a new block of at least size bytes. */
char *GAMEAPI msvc_getcwd(char *buf, int32_t size)
{
    char local[0x104];
    char *r = 0;
    uint32_t len;
    crt_lock(0xc);
    len = (uint32_t)GetCurrentDirectoryA(0x104, local);
    if (len != 0 && len + 1 < 0x105) {
        if (buf == 0) {
            int32_t n = (int32_t)(len + 1) <= size ? size : (int32_t)(len + 1);
            char *p = msvc_malloc((uint32_t)n);
            if (p != 0)
                r = msvc_strcpy(p, local);
            else
                *crt_errno() = 0xc;
        } else if ((int32_t)(len + 1) <= size) {
            r = msvc_strcpy(buf, local);
        } else {
            *crt_errno() = 0x22;
        }
    }
    crt_unlock(0xc);
    return r;
}

/* 0x498b3f: _chdir: also sets the drive's "=C:" environment variable. */
int32_t GAMEAPI msvc_chdir(const char *path)
{
    char local[0x108];
    char env[4];
    if (SetCurrentDirectoryA(path) != 0 && GetCurrentDirectoryA(0x105, local) != 0) {
        uint8_t c = (uint8_t)local[0];
        if ((c == '\\' || c == '/') && local[0] == local[1])
            return 0;
        env[0] = '=';
        env[1] = (char)(g_mbctype[c] & 0x20 ? G8(0x6d1040 + c) : c); /* _mbctoupper 0x49d4a0 */
        env[2] = ':';
        env[3] = 0;
        if (SetEnvironmentVariableA(env, local) != 0)
            return 0;
    }
    crt_dosmaperr((uint32_t)GetLastError());
    return -1;
}

/* 0x498bc5: _access: -1 if the file is missing, or read-only and mode asks
 * for write (EACCES). */
int32_t GAMEAPI msvc_access(const char *path, uint32_t mode)
{
    uint32_t attr = (uint32_t)GetFileAttributesA(path);
    if (attr == 0xffffffffu) {
        crt_dosmaperr((uint32_t)GetLastError());
        return -1;
    }
    if ((attr & 1) && (mode & 2)) {
        *crt_errno() = 0xd;
        *crt_doserrno() = 5;
        return -1;
    }
    return 0;
}

/* _initterm 0x498d62: call each nonzero pointer in [first, last). */
static void call_table(uint32_t first, uint32_t last)
{
    for (; first < last; first += 4)
        if (G32(first) != 0)
            ((void(GAMEAPI *)(void))(uintptr_t)G32(first))();
}

/* 0x498c89: exit (doexit 0x498cab with quick = 0, retcaller = 0): the atexit
 * table backwards, the pre-terminators and terminators, ExitProcess. */
void GAMEAPI msvc_exit(int32_t code)
{
    crt_lockexit();
    if (GI32(0x4db2d4) == 1)
        TerminateProcess(GetCurrentProcess(), (uint32_t)code);
    G32(0x4db2d0) = 1;
    G8(0x4db2cc) = 0;
    if (G32(0x6d2294) != 0) {
        uint32_t p = G32(0x6d2290) - 4;
        while (p >= G32(0x6d2294)) {
            if (G32(p) != 0)
                ((void(GAMEAPI *)(void))(uintptr_t)G32(p))();
            p -= 4;
        }
    }
    call_table(0x4a701c, 0x4a7024);
    call_table(0x4a7028, 0x4a702c);
    G32(0x4db2d4) = 1;
    ExitProcess((uint32_t)code);
}

/* 0x498d7c: _beginthread: a per-thread data block (calloc 0x74, _initptd)
 * holding the start address and argument, a suspended thread at the
 * runtime's _threadstart 0x498df3, then resumed. -1 on failure. */
uint32_t GAMEAPI msvc_beginthread(uint32_t start, uint32_t stack_size, uint32_t arg)
{
    uint32_t error = 0;
    uint8_t *ptd = msvc_calloc(1, 0x74);
    if (ptd != 0) {
        uint32_t h;
        crt_initptd(ptd);
        U32(ptd, 0x48) = start;
        U32(ptd, 0x4c) = arg;
        h = (uint32_t)(uintptr_t)CreateThread(0, stack_size, (void *)(uintptr_t)crt_threadstart, ptd, 4, (DWORD *)ptd);
        U32(ptd, 4) = h;
        if (h != 0 && (int32_t)ResumeThread(GMEM(h)) != -1)
            return h;
        error = (uint32_t)GetLastError();
    }
    msvc_free(ptd);
    if (error != 0)
        crt_dosmaperr(error);
    return 0xffffffffu;
}

/* ---- x87 maths: _ftol, __chkstk, cos, sin, atan, sqrt, pow ----
 *
 * These keep the original's register conventions (a double on the stack,
 * the result in st(0); _ftol: st(0) in, edx:eax out; _chkstk: eax in), so
 * they are written in assembly, instruction for instruction as the
 * runtime's x87 code, with its constants read from the data section:
 * 0x4a4da8 control word 0x27f, 0x4a4daa 2 pi (80-bit), 0x4b4db0 the
 * "indefinite" NaN, 0x4b4dba pi / 2, 0x4b4de0 infinity, 0x4b4af0 0.5,
 * 0x4a4da0 1.0, the range-check scales at 0x4a4db4-0x4a4ddc.
 *
 * The exit check (_math_exit 0x49a7eb): if the caller's control word is not
 * the default 0x27f and its precision exception is masked while the status
 * shows an inexact result, the original calls _87except (0x49a8e7) with
 * INEXACT, which, masked, only rounds the result to double and restores the
 * control word; that is done here directly. Real errors (NaN, infinite or
 * out-of-domain arguments, overflow, underflow) still go to the original
 * _87except (0x49a8e7, pow's 0x49a8d0) and pow's special-argument helper
 * _powhlp (0x49ab37), called by address from the same stack layout. */
#if defined(__i386__)
__asm__(".intel_syntax noprefix\n"
        ".text\n"

        /* 0x497a80: _ftol: st(0) to a 64-bit integer, truncating. */
        ".globl msvc_ftol\n"
        ".type msvc_ftol,@function\n"
        "msvc_ftol:\n"
        "    push ebp\n"
        "    mov ebp, esp\n"
        "    add esp, -0xc\n"
        "    fwait\n"
        "    fnstcw word ptr [ebp-2]\n"
        "    fwait\n"
        "    mov ax, word ptr [ebp-2]\n"
        "    or ah, 0xc\n"
        "    mov word ptr [ebp-4], ax\n"
        "    fldcw word ptr [ebp-4]\n"
        "    fistp qword ptr [ebp-0xc]\n"
        "    fldcw word ptr [ebp-2]\n"
        "    mov eax, dword ptr [ebp-0xc]\n"
        "    mov edx, dword ptr [ebp-8]\n"
        "    leave\n"
        "    ret\n"

        /* 0x497a50: _chkstk / _alloca_probe: move esp down by eax bytes,
         * touching each page on the way; ecx kept. */
        ".globl msvc_chkstk\n"
        ".type msvc_chkstk,@function\n"
        "msvc_chkstk:\n"
        "    push ecx\n"
        "    cmp eax, 0x1000\n"
        "    lea ecx, [esp+8]\n"
        "    jb 2f\n"
        "1:  sub ecx, 0x1000\n"
        "    sub eax, 0x1000\n"
        "    test dword ptr [ecx], eax\n"
        "    cmp eax, 0x1000\n"
        "    jae 1b\n"
        "2:  sub ecx, eax\n"
        "    mov eax, esp\n"
        "    test dword ptr [ecx], eax\n"
        "    mov esp, ecx\n"
        "    mov ecx, dword ptr [eax]\n"
        "    mov eax, dword ptr [eax+4]\n"
        "    push eax\n"
        "    ret\n"

        /* __fload_withFB 0x49a785: the double at [edx] onto the stack (an
         * infinity or NaN widened by hand); eax its high dword (masked to
         * the exponent when finite), ZF set for an infinity or NaN. */
        "crt_fload:\n"
        "    mov eax, dword ptr [edx+4]\n"
        "    and eax, 0x7ff00000\n"
        "    cmp eax, 0x7ff00000\n"
        "    je 1f\n"
        "    fld qword ptr [edx]\n"
        "    ret\n"
        "1:  mov eax, dword ptr [edx+4]\n"
        "    sub esp, 0xa\n"
        "    or eax, 0x7fff0000\n"
        "    mov dword ptr [esp+6], eax\n"
        "    mov eax, dword ptr [edx+4]\n"
        "    mov ecx, dword ptr [edx]\n"
        "    shld eax, ecx, 0xb\n"
        "    shl ecx, 0xb\n"
        "    mov dword ptr [esp+4], eax\n"
        "    mov dword ptr [esp], ecx\n"
        "    fld tbyte ptr [esp]\n"
        "    add esp, 0xa\n"
        "    test eax, 0\n"
        "    mov eax, dword ptr [edx+4]\n"
        "    ret\n"

        /* 0x49a755: the caller's precision, all exceptions masked. */
        "crt_set_cw:\n"
        "    mov edx, dword ptr [esp+4]\n"
        "    and edx, 0x300\n"
        "    or edx, 0x7f\n"
        "    mov word ptr [esp+6], dx\n"
        "    fldcw word ptr [esp+6]\n"
        "    ret\n"

        /* 0x49a76c: a NaN argument: quiet -> error 7, signalling -> quieted, 1. */
        "crt_nan_check:\n"
        "    test eax, 0x80000\n"
        "    je 1f\n"
        "    mov eax, 7\n"
        "    ret\n"
        "1:  fadd qword ptr [0x4a4da0]\n"
        "    mov eax, 1\n"
        "    ret\n"

        /* 0x49a740: 2^st(0). */
        "crt_exp2:\n"
        "    fld st(0)\n"
        "    frndint\n"
        "    .byte 0xdc, 0xe1\n" /* fsubr st(1), st: st(1) = st(0) - st(1) */
        "    fxch st(1)\n"
        "    fchs\n"
        "    f2xm1\n"
        "    fld1\n"
        "    faddp st(1), st\n"
        "    fscale\n"
        "    fstp st(1)\n"
        "    ret\n"

        /* 0x497ea5: is st(0) an integer (consumed): cl 0 no, 1 odd, 2 even. */
        "crt_int_check:\n"
        "    fld st(0)\n"
        "    frndint\n"
        "    fcomp st(1)\n"
        "    mov cl, 0\n"
        "    fwait\n"
        "    fnstsw ax\n"
        "    sahf\n"
        "    jne 2f\n"
        "    fmul qword ptr [0x4b4af0]\n"
        "    inc cl\n"
        "    fld st(0)\n"
        "    frndint\n"
        "    fcompp\n"
        "    fwait\n"
        "    fnstsw ax\n"
        "    sahf\n"
        "    jne 1f\n"
        "    inc cl\n"
        "1:  ret\n"
        "2:  fstp st(0)\n"
        "    ret\n"

        /* 0x49a7de: restore the control word, drop the slot. */
        "crt_fast_exit:\n"
        "    cmp word ptr [esp], 0x27f\n"
        "    je 1f\n"
        "    fldcw word ptr [esp]\n"
        "1:  pop edx\n"
        "    ret\n"

        /* 0x49a7eb: _math_exit (see above). */
        "crt_math_exit:\n"
        "    mov ax, word ptr [esp]\n"
        "    cmp ax, 0x27f\n"
        "    je 2f\n"
        "    and ax, 0x20\n"
        "    je 1f\n"
        "    fwait\n"
        "    fnstsw ax\n"
        "    and ax, 0x20\n"
        "    je 1f\n"
        "    sub esp, 8\n"
        "    fstp qword ptr [esp]\n"
        "    fld qword ptr [esp]\n"
        "    add esp, 8\n"
        "1:  fldcw word ptr [esp]\n"
        "2:  pop edx\n"
        "    ret\n"

        /* 0x49a829: pow's exit: an underflowed or overflowed result is
         * scaled and reported (types 4 / 3), else the control word back. */
        "crt_math_exit_range:\n"
        "    sub esp, 8\n"
        "    fst qword ptr [esp]\n"
        "    mov eax, dword ptr [esp+4]\n"
        "    add esp, 8\n"
        "    and eax, 0x7ff00000\n"
        "    je 5f\n"
        "    cmp eax, 0x7ff00000\n"
        "    je 6f\n"
        "    mov ax, word ptr [esp]\n"
        "    cmp ax, 0x27f\n"
        "    je 4f\n"
        "    and ax, 0x20\n"
        "    jne 3f\n"
        "    fwait\n"
        "    fnstsw ax\n"
        "    and ax, 0x20\n"
        "    je 3f\n"
        "    mov eax, 8\n"
        "1:  cmp edx, 0x1d\n"
        "    je 2f\n"
        "    .att_syntax prefix\n"
        "    call 0x49a8e7\n"
        "    .intel_syntax noprefix\n"
        "    pop edx\n"
        "    ret\n"
        "2:\n"
        "    .att_syntax prefix\n"
        "    call 0x49a8d0\n"
        "    .intel_syntax noprefix\n"
        "    pop edx\n"
        "    ret\n"
        "3:  fldcw word ptr [esp]\n"
        "4:  pop edx\n"
        "    ret\n"
        "5:  fld qword ptr [0x4a4dcc]\n"
        "    fxch st(1)\n"
        "    fscale\n"
        "    fstp st(1)\n"
        "    fld st(0)\n"
        "    fabs\n"
        "    fcomp qword ptr [0x4a4dbc]\n"
        "    fwait\n"
        "    fnstsw ax\n"
        "    sahf\n"
        "    mov eax, 4\n"
        "    jae 1b\n"
        "    fmul qword ptr [0x4a4ddc]\n"
        "    jmp 1b\n"
        "6:  fld qword ptr [0x4a4dc4]\n"
        "    fxch st(1)\n"
        "    fscale\n"
        "    fstp st(1)\n"
        "    fld st(0)\n"
        "    fabs\n"
        "    fcomp qword ptr [0x4a4db4]\n"
        "    fwait\n"
        "    fnstsw ax\n"
        "    sahf\n"
        "    mov eax, 3\n"
        "    jbe 1b\n"
        "    fmul qword ptr [0x4a4dd4]\n"
        "    jmp 1b\n"

        /* 0x497ac4: cos (at the 0x27f control word; |x| >= 2^63 reduced
         * by 2 pi first). Infinity: the indefinite NaN, error 1. */
        ".globl msvc_cos\n"
        ".type msvc_cos,@function\n"
        "msvc_cos:\n"
        "    lea edx, [esp+4]\n"
        "    call crt_fload\n"
        "    push edx\n"
        "    fwait\n"
        "    fnstcw word ptr [esp]\n"
        "    je 4f\n"
        "    cmp word ptr [esp], 0x27f\n"
        "    je 1f\n"
        "    fldcw word ptr [0x4a4da8]\n"
        "1:  fcos\n"
        "    fwait\n"
        "    fnstsw ax\n"
        "    sahf\n"
        "    jp 3f\n"
        "2:  cmp dword ptr [0x4db28c], 0\n"
        "    jne crt_fast_exit\n"
        "    mov edx, 0x12\n"
        "    mov ecx, 0x4b4ac0\n"
        "    jmp crt_math_exit\n"
        "3:  fld tbyte ptr [0x4a4daa]\n"
        "    fxch st(1)\n"
        "7:  fprem1\n"
        "    fwait\n"
        "    fnstsw ax\n"
        "    sahf\n"
        "    jp 7b\n"
        "    fstp st(1)\n"
        "    fcos\n"
        "    jmp 2b\n"
        "6:  call crt_nan_check\n"
        "    jmp 5f\n"
        "4:  test eax, 0xfffff\n"
        "    jne 6b\n"
        "    cmp dword ptr [esp+8], 0\n"
        "    jne 6b\n"
        "    fstp st(0)\n"
        "    fld tbyte ptr [0x4b4db0]\n"
        "    mov eax, 1\n"
        "5:  cmp dword ptr [0x4db28c], 0\n"
        "    jne crt_fast_exit\n"
        "    mov edx, 0x12\n"
        "    mov ecx, 0x4b4ac0\n"
        "    .att_syntax prefix\n"
        "    call 0x49a8e7\n"
        "    .intel_syntax noprefix\n"
        "    pop edx\n"
        "    ret\n"

        /* 0x497b74: sin, as cos. */
        ".globl msvc_sin\n"
        ".type msvc_sin,@function\n"
        "msvc_sin:\n"
        "    lea edx, [esp+4]\n"
        "    call crt_fload\n"
        "    push edx\n"
        "    fwait\n"
        "    fnstcw word ptr [esp]\n"
        "    je 4f\n"
        "    cmp word ptr [esp], 0x27f\n"
        "    je 1f\n"
        "    fldcw word ptr [0x4a4da8]\n"
        "1:  fsin\n"
        "    fwait\n"
        "    fnstsw ax\n"
        "    sahf\n"
        "    jp 3f\n"
        "2:  cmp dword ptr [0x4db28c], 0\n"
        "    jne crt_fast_exit\n"
        "    mov edx, 0x1e\n"
        "    mov ecx, 0x4b4ad0\n"
        "    jmp crt_math_exit\n"
        "3:  fld tbyte ptr [0x4a4daa]\n"
        "    fxch st(1)\n"
        "7:  fprem1\n"
        "    fwait\n"
        "    fnstsw ax\n"
        "    sahf\n"
        "    jp 7b\n"
        "    fstp st(1)\n"
        "    fsin\n"
        "    jmp 2b\n"
        "6:  call crt_nan_check\n"
        "    jmp 5f\n"
        "4:  test eax, 0xfffff\n"
        "    jne 6b\n"
        "    cmp dword ptr [esp+8], 0\n"
        "    jne 6b\n"
        "    fstp st(0)\n"
        "    fld tbyte ptr [0x4b4db0]\n"
        "    mov eax, 1\n"
        "5:  cmp dword ptr [0x4db28c], 0\n"
        "    jne crt_fast_exit\n"
        "    mov edx, 0x1e\n"
        "    mov ecx, 0x4b4ad0\n"
        "    .att_syntax prefix\n"
        "    call 0x49a8e7\n"
        "    .intel_syntax noprefix\n"
        "    pop edx\n"
        "    ret\n"

        /* 0x497c24: atan (fpatan with 1); atan(+-infinity) = +-pi / 2. */
        ".globl msvc_atan\n"
        ".type msvc_atan,@function\n"
        "msvc_atan:\n"
        "    lea edx, [esp+4]\n"
        "    call crt_fload\n"
        "    push edx\n"
        "    fwait\n"
        "    fnstcw word ptr [esp]\n"
        "    je 4f\n"
        "    cmp word ptr [esp], 0x27f\n"
        "    je 1f\n"
        "    fldcw word ptr [0x4a4da8]\n"
        "1:  fld1\n"
        "    fpatan\n"
        "2:  cmp dword ptr [0x4db28c], 0\n"
        "    jne crt_fast_exit\n"
        "    mov edx, 0xf\n"
        "    mov ecx, 0x4b4ae0\n"
        "    jmp crt_math_exit\n"
        "3:  call crt_nan_check\n"
        "    jmp 5f\n"
        "4:  test eax, 0xfffff\n"
        "    jne 3b\n"
        "    cmp dword ptr [esp+8], 0\n"
        "    jne 3b\n"
        "    fstp st(0)\n"
        "    fld tbyte ptr [0x4b4dba]\n"
        "    test eax, 0x80000000\n"
        "    je 2b\n"
        "    fchs\n"
        "    jmp 2b\n"
        "5:  cmp dword ptr [0x4db28c], 0\n"
        "    jne crt_fast_exit\n"
        "    mov edx, 0xf\n"
        "    mov ecx, 0x4b4ae0\n"
        "    .att_syntax prefix\n"
        "    call 0x49a8e7\n"
        "    .intel_syntax noprefix\n"
        "    pop edx\n"
        "    ret\n"

        /* 0x4981d4: sqrt at the caller's precision; sqrt(-0) = -0, a
         * negative argument the indefinite NaN, error 1. */
        ".globl msvc_sqrt\n"
        ".type msvc_sqrt,@function\n"
        "msvc_sqrt:\n"
        "    lea edx, [esp+4]\n"
        "    call crt_fload\n"
        "    push edx\n"
        "    fwait\n"
        "    fnstcw word ptr [esp]\n"
        "    mov eax, dword ptr [esp+0xc]\n"
        "    je 5f\n"
        "    cmp word ptr [esp], 0x27f\n"
        "    je 1f\n"
        "    call crt_set_cw\n"
        "1:  test eax, 0x80000000\n"
        "    jne 3f\n"
        "    fsqrt\n"
        "2:  cmp dword ptr [0x4db28c], 0\n"
        "    jne crt_fast_exit\n"
        "    mov edx, 5\n"
        "    mov ecx, 0x4b4d20\n"
        "    jmp crt_math_exit\n"
        "3:  test eax, 0x7ff00000\n"
        "    jne 6f\n"
        "    test eax, 0xfffff\n"
        "    jne 6f\n"
        "    cmp dword ptr [esp+8], 0\n"
        "    jne 6f\n"
        "    jmp 2b\n"
        "4:  call crt_nan_check\n"
        "    jmp 7f\n"
        "5:  test eax, 0xfffff\n"
        "    jne 4b\n"
        "    cmp dword ptr [esp+8], 0\n"
        "    jne 4b\n"
        "    and eax, 0x80000000\n"
        "    je 2b\n"
        "6:  fstp st(0)\n"
        "    fld tbyte ptr [0x4b4db0]\n"
        "    mov eax, 1\n"
        "7:  cmp dword ptr [0x4db28c], 0\n"
        "    jne crt_fast_exit\n"
        "    mov edx, 5\n"
        "    mov ecx, 0x4b4d20\n"
        "    .att_syntax prefix\n"
        "    call 0x49a8e7\n"
        "    .intel_syntax noprefix\n"
        "    pop edx\n"
        "    ret\n"

        /* 0x497cc9: pow(x, y) at the caller's precision: y * log2 |x|,
         * then 2^that, negated for a negative x and odd integer y; zero,
         * infinite and NaN arguments as the original (_powhlp 0x49ab37 for
         * infinities). */
        ".globl msvc_pow\n"
        ".type msvc_pow,@function\n"
        "msvc_pow:\n"
        "    lea edx, [esp+0xc]\n"
        "    call crt_fload\n"
        "    mov ecx, eax\n"
        "    push eax\n"
        "    fwait\n"
        "    fnstcw word ptr [esp]\n"
        "    cmp word ptr [esp], 0x27f\n"
        "    je 1f\n"
        "    call crt_set_cw\n"
        "1:  and ecx, 0x7ff00000\n"
        "    lea edx, [esp+8]\n"
        "    cmp ecx, 0x7ff00000\n"
        "    je .Lpow_y_special\n"
        "    call crt_fload\n"
        "    je .Lpow_x_special\n"
        "    test eax, 0x7ff00000\n"
        "    je .Lpow_x_small\n"
        ".Lpow_x_finite:\n"
        "    mov cl, byte ptr [esp+0xf]\n"
        "    and cl, 0x80\n"
        "    jne .Lpow_x_negative\n"
        ".Lpow_main:\n"
        "    fyl2x\n"
        "    call crt_exp2\n"
        "    cmp cl, 1\n"
        "    jne 2f\n"
        "    fchs\n"
        "2:  cmp dword ptr [0x4db28c], 0\n"
        "    jne crt_fast_exit\n"
        "    mov ecx, 0x4b4af8\n"
        "    mov edx, 0x1d\n"
        "    jmp crt_math_exit_range\n"
        ".Lpow_error:\n"
        "    cmp dword ptr [0x4db28c], 0\n"
        "    jne crt_fast_exit\n"
        "    mov ecx, 0x4b4af8\n"
        "    mov edx, 0x1d\n"
        "    .att_syntax prefix\n"
        "    call 0x49a8d0\n"
        "    .intel_syntax noprefix\n"
        "    pop edx\n"
        "    ret\n"
        ".Lpow_y_nan:\n"
        "    lea edx, [esp+8]\n"
        "    call crt_fload\n"
        "    test byte ptr [esp+0x16], 8\n"
        "    jne .Lpow_nan_quiet\n"
        "    inc ecx\n"
        "    jmp .Lpow_check\n"
        ".Lpow_nan_quiet:\n"
        "    faddp st(1), st\n"
        "    mov eax, 1\n"
        "    jmp .Lpow_error\n"
        ".Lpow_x_nan:\n"
        "    test byte ptr [esp+0xe], 8\n"
        "    jne .Lpow_nan_quiet\n"
        ".Lpow_nan_signal:\n"
        "    faddp st(1), st\n"
        "    mov eax, 7\n"
        "    jmp .Lpow_error\n"
        ".Lpow_x_special:\n"
        "    xor ecx, ecx\n"
        "    jmp .Lpow_check\n"
        ".Lpow_y_special:\n"
        "    xor ecx, ecx\n"
        "    and eax, 0xfffff\n"
        "    or eax, dword ptr [esp+0x10]\n"
        "    jne .Lpow_y_nan\n"
        "    lea edx, [esp+8]\n"
        "    call crt_fload\n"
        ".Lpow_check:\n"
        "    mov eax, dword ptr [esp+0xc]\n"
        "    mov edx, eax\n"
        "    and eax, 0x7ff00000\n"
        "    and edx, 0xfffff\n"
        "    cmp eax, 0x7ff00000\n"
        "    jne 3f\n"
        "    or edx, dword ptr [esp+8]\n"
        "    jne .Lpow_x_nan\n"
        "3:  test ecx, ecx\n"
        "    jne .Lpow_nan_signal\n"
        "    sub esp, 0x74\n"
        "    mov ecx, esp\n"
        "    push ecx\n"
        "    sub esp, 0x10\n"
        "    fstp qword ptr [esp]\n"
        "    fstp qword ptr [esp+8]\n"
        "    fwait\n"
        "    fnsave [ecx+8]\n"
        "    .att_syntax prefix\n"
        "    call 0x49ab37\n"
        "    .intel_syntax noprefix\n"
        "    add esp, 0x10\n"
        "    pop ecx\n"
        "    frstor [ecx+8]\n"
        "    fld qword ptr [ecx]\n"
        "    add esp, 0x74\n"
        "    test eax, eax\n"
        "    je crt_fast_exit\n"
        "    mov eax, 1\n"
        "    jmp .Lpow_error\n"
        ".Lpow_x_small:\n"
        "    mov eax, dword ptr [esp+0xc]\n"
        "    and eax, 0xfffff\n"
        "    or eax, dword ptr [esp+8]\n"
        "    jne .Lpow_x_finite\n"
        "    fstp st(0)\n"
        "    mov eax, dword ptr [esp+0x14]\n"
        "    test eax, 0x7ff00000\n"
        "    je 5f\n"
        "    call crt_int_check\n"
        "    mov ch, byte ptr [esp+0xf]\n"
        "    shr ch, 7\n"
        "    test dword ptr [esp+0x17], 0x80\n"
        "    je 4f\n"
        "    fld tbyte ptr [0x4b4de0]\n"
        "    test ch, cl\n"
        "    je 6f\n"
        "    fchs\n"
        "6:  mov eax, 2\n"
        "    jmp .Lpow_error\n"
        "4:  fldz\n"
        "    test ch, cl\n"
        "    je crt_fast_exit\n"
        "    fchs\n"
        "    jmp crt_fast_exit\n"
        "5:  fstp st(0)\n"
        "    and eax, 0xfffff\n"
        "    or eax, dword ptr [esp+0x10]\n"
        "    jne 7f\n"
        "    fld1\n"
        "    jmp crt_fast_exit\n"
        "7:  fldz\n"
        "    jmp crt_fast_exit\n"
        ".Lpow_x_negative:\n"
        "    fld st(1)\n"
        "    call crt_int_check\n"
        "    fchs\n"
        "    test cl, cl\n"
        "    jne .Lpow_main\n"
        "    fstp st(0)\n"
        "    fstp st(0)\n"
        "    fld tbyte ptr [0x4b4db0]\n"
        "    mov eax, 1\n"
        "    jmp .Lpow_error\n"
        ".att_syntax prefix\n");
#else /* x86-64 */
/* Nothing calls these with the original's register / x87-stack conventions
 * on x86-64 (every caller is C), so they are C with real prototypes, the
 * same x87 instructions where the result depends on them. The control word
 * handling is the original's: cos / sin / atan at 0x27f, sqrt / pow at the
 * caller's precision with every exception masked. Not reproduced: the
 * error reports through _87except (errno, _matherr) for NaN / infinite /
 * out-of-domain arguments and overflow / underflow; the results there are
 * the masked-exception values (the argument NaN quieted, the indefinite
 * NaN for a domain error, libm's pow for infinite or NaN pow arguments). */

#define CRT_INDEFINITE 0xfff8000000000000ull /* the "indefinite" NaN 0x4b4db0 as a double */

static double crt_bits_double(uint64_t q)
{
    double d;
    __builtin_memcpy(&d, &q, 8);
    return d;
}

static uint64_t crt_double_bits(double d)
{
    uint64_t q;
    __builtin_memcpy(&q, &d, 8);
    return q;
}

/* An infinity or NaN argument: a NaN comes back quieted, an infinity
 * gives `inf_result`. */
static int crt_special(double x, double *out, double inf_result)
{
    uint64_t q = crt_double_bits(x);

    if ((q & 0x7ff0000000000000ull) != 0x7ff0000000000000ull)
        return 0;
    *out = (q & 0xfffffffffffffull) ? x + 1.0 : inf_result;
    return 1;
}

/* 0x497a80: _ftol: truncation to 64 bits (out of range: 0x8000000000000000,
 * as fistp's indefinite integer). */
int64_t msvc_ftol(double x)
{
    return (int64_t)x;
}

/* 0x497a50: _chkstk: C callers need no stack probe. */
void msvc_chkstk(void)
{
}

/* fcos / fsin at control word 0x27f, |x| >= 2^63 reduced by 2 pi (0x4a4daa)
 * with fprem1 first. */
#define CRT_TRIG(name, insn)                                                                                   \
    double name(double x)                                                                                      \
    {                                                                                                          \
        double r;                                                                                              \
        uint16_t old, cw = 0x27f;                                                                              \
                                                                                                               \
        if (crt_special(x, &r, crt_bits_double(CRT_INDEFINITE)))                                               \
            return r;                                                                                          \
        __asm__ volatile("fnstcw %1\n\t"                                                                       \
                         "fldcw %2\n\t"                                                                        \
                         "fldl %3\n\t" insn "\n\t"                                                             \
                         "fnstsw %%ax\n\t"                                                                     \
                         "testb $4, %%ah\n\t"                                                                  \
                         "jz 2f\n\t"                                                                           \
                         "fldt %4\n\t"                                                                         \
                         "fxch %%st(1)\n"                                                                      \
                         "1:\n\t"                                                                              \
                         "fprem1\n\t"                                                                          \
                         "fnstsw %%ax\n\t"                                                                     \
                         "testb $4, %%ah\n\t"                                                                  \
                         "jnz 1b\n\t"                                                                          \
                         "fstp %%st(1)\n\t" insn "\n"                                                          \
                         "2:\n\t"                                                                              \
                         "fstpl %0\n\t"                                                                        \
                         "fldcw %1"                                                                            \
                         : "=m"(r), "=m"(old)                                                                  \
                         : "m"(cw), "m"(x), "m"(*(const long double *)(uintptr_t)0x4a4daa)                      \
                         : "eax", "cc", "st", "st(1)");                                                       \
        return r;                                                                                              \
    }

/* 0x497ac4 */
CRT_TRIG(msvc_cos, "fcos")
/* 0x497b74 */
CRT_TRIG(msvc_sin, "fsin")

/* 0x497c24: atan: fpatan with 1 at 0x27f; atan(+-infinity) = +-pi / 2. */
double msvc_atan(double x)
{
    double r;
    uint16_t old, cw = 0x27f;
    uint64_t q = crt_double_bits(x);

    if ((q & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (q & 0xfffffffffffffull))
        return x + 1.0;
    __asm__ volatile("fnstcw %1\n\tfldcw %2\n\tfldl %3\n\tfld1\n\tfpatan\n\tfstpl %0\n\tfldcw %1"
                     : "=m"(r), "=m"(old)
                     : "m"(cw), "m"(x)
                     : "st", "st(1)");
    return r;
}

/* The caller's control word with every exception masked (0x49a755). */
static uint16_t crt_caller_cw(void)
{
    uint16_t cw;

    __asm__ volatile("fnstcw %0" : "=m"(cw));
    return cw == 0x27f ? cw : (uint16_t)((cw & 0x300) | 0x7f);
}

/* 0x4981d4: sqrt at the caller's precision; sqrt(-0) = -0, a negative
 * argument the indefinite NaN. */
double msvc_sqrt(double x)
{
    double r;
    uint16_t old, cw = crt_caller_cw();
    uint64_t q = crt_double_bits(x);

    if (crt_special(x, &r, (q >> 63) ? crt_bits_double(CRT_INDEFINITE) : x))
        return r;
    if ((q >> 63) && (q << 1) != 0)
        return crt_bits_double(CRT_INDEFINITE);
    __asm__ volatile("fnstcw %1\n\tfldcw %2\n\tfldl %3\n\tfsqrt\n\tfstpl %0\n\tfldcw %1"
                     : "=m"(r), "=m"(old)
                     : "m"(cw), "m"(x)
                     : "st");
    return r;
}

/* 0x497ea5: 0 if y is not an integer, 1 odd, 2 even. */
static int crt_int_kind(double y)
{
    int64_t i;

    if (__builtin_fabs(y) >= 9007199254740992.0)
        return 2;
    i = (int64_t)y;
    if ((double)i != y)
        return 0;
    return (i & 1) ? 1 : 2;
}

/* 0x497cc9: pow(x, y) at the caller's precision: 2^(y * log2 |x|) (fyl2x,
 * then 0x49a740's frndint / f2xm1 / fscale), negated for a negative x and
 * odd integer y; a zero x as the original; infinite or NaN arguments
 * through libm's pow (the original's _powhlp gives the C99 values). */
double msvc_pow(double x, double y)
{
    extern double pow(double, double);
    uint64_t qx = crt_double_bits(x), qy = crt_double_bits(y);
    uint16_t old, cw;
    double ax, r;
    int kind = 2;

    if ((qx & 0x7ff0000000000000ull) == 0x7ff0000000000000ull ||
        (qy & 0x7ff0000000000000ull) == 0x7ff0000000000000ull)
        return pow(x, y);
    if ((qx << 1) == 0) { /* x = +-0 */
        int neg;

        if ((qy & 0x7ff0000000000000ull) == 0) /* y zero or denormal */
            return (qy << 1) == 0 ? 1.0 : 0.0;
        neg = (int)(qx >> 63) & (crt_int_kind(y) == 1);
        if (qy >> 63)
            return neg ? -__builtin_inf() : __builtin_inf();
        return neg ? -0.0 : 0.0;
    }
    ax = x;
    if (qx >> 63) {
        kind = crt_int_kind(y);
        if (kind == 0)
            return crt_bits_double(CRT_INDEFINITE);
        ax = -x;
    }
    cw = crt_caller_cw();
    __asm__ volatile("fnstcw %1\n\t"
                     "fldcw %2\n\t"
                     "fldl %3\n\t"
                     "fldl %4\n\t"
                     "fyl2x\n\t"
                     "fld %%st(0)\n\t"
                     "frndint\n\t"
                     ".byte 0xdc, 0xe1\n\t" /* fsubr st(1), st: st(1) = st(0) - st(1) */
                     "fxch %%st(1)\n\t"
                     "fchs\n\t"
                     "f2xm1\n\t"
                     "fld1\n\t"
                     "faddp\n\t"
                     "fscale\n\t"
                     "fstp %%st(1)\n\t"
                     "fstpl %0\n\t"
                     "fldcw %1"
                     : "=m"(r), "=m"(old)
                     : "m"(cw), "m"(y), "m"(ax)
                     : "st", "st(1)");
    return kind == 1 ? -r : r;
}
#endif
