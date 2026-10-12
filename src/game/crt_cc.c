/* The rest of the Microsoft C runtime, groups CC and CD (0x496260-0x4a2146):
 * everything of the runtime the game or our C still reaches once groups CA
 * and CB took the entry points the game calls.
 *
 *  - the start-up (the exe's entry point 0x498ec5: GetVersion, the runtime
 *    heap, the per-thread data and locks, the low-level I/O table, the
 *    command line and environment, the initialisers, WinMain 0x4351e0,
 *    exit) and exit's pieces (_lockexit, _initterm, _exit, _amsg_exit and
 *    its message box, the stdio terminator 0x49ad74), _beginthread's
 *    thread start;
 *  - the per-thread data (_getptd, the rand seed lives there), errno,
 *    _doserrno, _dosmaperr, the runtime locks;
 *  - stdio: fopen, fread, fseek, ftell, fclose, fputs, fflush, printf and
 *    under them the stream table (_iob 0x4b4e40, __piob 0x6d126c) and the
 *    low-level handle table (__pioinfo 0x6d0f20): _open, _read, _write,
 *    _lseek, _close, _commit, _chsize, text-mode CR-LF translation;
 *  - the multibyte code page set-up (_setmbcp) and the locale helpers our
 *    C reaches by address (__crtGetStringTypeA, __crtLCMapStringA,
 *    _toupper_lk / _tolower_lk, _mbsdec, _mbsnbcpy, _mbctoupper);
 *  - the x87 maths: fmod 0x4987e0 and atan2 0x498820 with the runtime's
 *    two-argument dispatcher, and the maths error machinery our C reaches
 *    by address (_startOneArgErrorHandling 0x49a8e7 / two-arg 0x49a8d0,
 *    _powhlp 0x49ab37, _except2 0x4a0f1a, _handle_qnan2 0x4a0eba and under
 *    them _87except, _handle_exc, _raise_exc, _umatherr, _decomp...).
 *
 * Everything keeps the original's data in the exe's data section at the
 * same addresses (the stream table, the handle table, the lock table, the
 * multibyte tables...), so the in-place restart (which reloads the data
 * section) resets it as before. Windows calls go through the import table
 * in the original's order. Simplified where the game cannot see it: the
 * stack-allocated buffers of the locale helpers are C arrays (no _alloca,
 * no SEH frame; an over-long string falls back to failure as the
 * original's out-of-stack path does), the start-up and thread start set no
 * SEH frame (no handler of the runtime is ever registered), printf formats
 * through _vsnprintf first (CB's engine, by address) and then writes the
 * text to stdout character by character as _output's putc does, _fpmath
 * leaves the float-conversion table alone (only the runtime's own _output
 * / _input used it), the runtime's empty _FPmtinit / _FPmtterm are not
 * called, setSBUpLow's buffers start zeroed (the original reads them
 * uninitialised when a call fails).
 *
 * Called by address: malloc 0x4982c2, free 0x49827a, calloc 0x49946c,
 * strcpy 0x498450, strcat 0x498460, strncpy 0x498350, _vsnprintf 0x49972a,
 * _isctype 0x498051 (group CB), WinMain 0x4351e0. */
#include "../runtime.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define S8(p, off) (*(int8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define PTR(p, off) (*(uint8_t **)((uint8_t *)(p) + (off)))
#define G8(a) (*(uint8_t *)(a))
#define G16(a) (*(uint16_t *)(a))
#define G32(a) (*(uint32_t *)(a))
#define GI32(a) (*(int32_t *)(a))
#define GPTR(a) (*(uint8_t **)(a))

#define STDCALL __attribute__((stdcall))

/* ---------------------------------------------------------------- imports */

#define IAT(slot, type) (*(type *)(slot))
typedef uint32_t(STDCALL *w0_fn)(void);
typedef uint32_t(STDCALL *w1_fn)(uint32_t);
typedef uint32_t(STDCALL *w2_fn)(uint32_t, uint32_t);
typedef uint32_t(STDCALL *w3_fn)(uint32_t, uint32_t, uint32_t);
typedef uint32_t(STDCALL *w4_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(STDCALL *w5_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(STDCALL *w6_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(STDCALL *w7_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef uint32_t(STDCALL *w8_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#define A(x) ((uint32_t)(uintptr_t)(x))

#define ReadFile_(h, b, n, r, o) IAT(0x4a3070, w5_fn)(A(h), A(b), (n), A(r), (o))
#define WriteFile_(h, b, n, r, o) IAT(0x4a3074, w5_fn)(A(h), A(b), (n), A(r), (o))
#define SetEndOfFile_(h) IAT(0x4a3094, w1_fn)(A(h))
#define LoadLibraryA_(s) IAT(0x4a30a4, w1_fn)(A(s))
#define FlushFileBuffers_(h) IAT(0x4a30b0, w1_fn)(A(h))
#define GetFileType_(h) IAT(0x4a30b4, w1_fn)(A(h))
#define SetHandleCount_(n) IAT(0x4a30bc, w1_fn)(n)
#define GetEnvironmentStringsW_() IAT(0x4a30c0, w0_fn)()
#define GetEnvironmentStrings_() IAT(0x4a30c4, w0_fn)()
#define FreeEnvironmentStringsW_(p) IAT(0x4a30c8, w1_fn)(A(p))
#define FreeEnvironmentStringsA_(p) IAT(0x4a30cc, w1_fn)(A(p))
#define GetModuleFileNameA_(m, b, n) IAT(0x4a30d0, w3_fn)((m), A(b), (n))
#define GetOEMCP_() IAT(0x4a30dc, w0_fn)()
#define GetACP_() IAT(0x4a30e0, w0_fn)()
#define GetCPInfo_(cp, i) IAT(0x4a30e4, w2_fn)((cp), A(i))
#define SetStdHandle_(n, h) IAT(0x4a30f0, w2_fn)((n), A(h))
#define RaiseException_(c, f, n, a) IAT(0x4a30f4, w4_fn)((c), (f), (n), A(a))
#define GetStdHandle_(n) IAT(0x4a30f8, w1_fn)(n)
#define CloseHandle_(h) IAT(0x4a3100, w1_fn)(A(h))
#define SetFilePointer_(h, d, hi, m) IAT(0x4a3104, w4_fn)(A(h), (d), A(hi), (m))
#define CreateFileA_(n, a, s, sa, c, f, t) IAT(0x4a3108, w7_fn)(A(n), (a), (s), A(sa), (c), (f), (t))
#define GetLastError_() IAT(0x4a310c, w0_fn)()
#define TlsGetValue_(i) IAT(0x4a311c, w1_fn)(i)
#define SetLastError_(e) IAT(0x4a3120, w1_fn)(e)
#define TlsAlloc_() IAT(0x4a3124, w0_fn)()
#define GetCurrentThreadId_() IAT(0x4a3128, w0_fn)()
#define LCMapStringW_(l, f, s, n, d, m) IAT(0x4a312c, w6_fn)((l), (f), A(s), (n), A(d), (m))
#define LCMapStringA_(l, f, s, n, d, m) IAT(0x4a3130, w6_fn)((l), (f), A(s), (n), A(d), (m))
#define WideCharToMultiByte_(cp, f, w, n, s, m, d, u) \
    IAT(0x4a3134, w8_fn)((cp), (f), A(w), (n), A(s), (m), A(d), A(u))
#define DeleteCriticalSection_(c) IAT(0x4a3150, w1_fn)(A(c))
#define InitializeCriticalSection_(c) IAT(0x4a3154, w1_fn)(A(c))
#define HeapCreate_(f, i, m) IAT(0x4a315c, w3_fn)((f), (i), (m))
#define HeapDestroy_(h) IAT(0x4a3160, w1_fn)(h)
#define GetStringTypeW_(t, s, n, o) IAT(0x4a3164, w4_fn)((t), A(s), (n), A(o))
#define GetStringTypeA_(l, t, s, n, o) IAT(0x4a3168, w5_fn)((l), (t), A(s), (n), A(o))
#define MultiByteToWideChar_(cp, f, s, n, w, m) IAT(0x4a316c, w6_fn)((cp), (f), A(s), (n), A(w), (m))
#define LeaveCriticalSection_(c) IAT(0x4a3170, w1_fn)(A(c))
#define EnterCriticalSection_(c) IAT(0x4a3174, w1_fn)(A(c))
#define GetProcAddress_(m, n) IAT(0x4a3178, w2_fn)((m), A(n))
#define GetSystemTimeAsFileTime_(t) IAT(0x4a317c, w1_fn)(A(t))
#define GetVersion_() IAT(0x4a3180, w0_fn)()
#define GetCommandLineA_() IAT(0x4a3184, w0_fn)()
#define GetStartupInfoA_(s) IAT(0x4a3188, w1_fn)(A(s))
#define GetModuleHandleA_(n) IAT(0x4a318c, w1_fn)(A(n))
#define TlsSetValue_(i, v) IAT(0x4a3190, w2_fn)((i), A(v))
#define GetCurrentProcess_() IAT(0x4a3198, w0_fn)()
#define TerminateProcess_(h, c) IAT(0x4a319c, w2_fn)((h), (c))
#define ExitProcess_(c) IAT(0x4a31a0, w1_fn)(c)
#define ExitThread_(c) IAT(0x4a31b0, w1_fn)(c)
#define HeapAlloc_(h, f, n) IAT(0x4a31c4, w3_fn)((h), (f), (n))

/* ------------------------------------------------------ our entries */

void GAMEAPI crtc_amsg_exit(int32_t n);
void GAMEAPI crtc_lock(int32_t n);
void GAMEAPI crtc_unlock(int32_t n);
int32_t *GAMEAPI crtc_errno(void);
int32_t *GAMEAPI crtc_doserrno(void);
void GAMEAPI crtc_dosmaperr(uint32_t oserr);
void GAMEAPI crtc_initmbctable(void);
uint8_t *GAMEAPI crtc_getptd(void);

/* ------------------------------------------------- other groups' functions */

typedef void *(GAMEAPI *malloc_fn)(uint32_t size);
typedef void(GAMEAPI *free_fn)(void *p);
typedef void *(GAMEAPI *calloc_fn)(uint32_t n, uint32_t size);
typedef char *(GAMEAPI *strcpy_fn)(char *dst, const char *src);
typedef char *(GAMEAPI *strncpy_fn)(char *dst, const char *src, uint32_t n);
typedef int32_t(GAMEAPI *vsnprintf_fn)(char *buf, uint32_t n, const char *fmt, __builtin_va_list ap);
typedef int32_t(GAMEAPI *isctype_fn)(int32_t c, int32_t mask);
typedef int32_t(STDCALL *winmain_fn)(uint32_t inst, uint32_t prev, uint8_t *cmdline, uint32_t show);
typedef void(GAMEAPI *exit_fn)(int32_t code);
typedef void(GAMEAPI *vfn)(void);
#define rt_malloc ((malloc_fn)0x4982c2)
#define rt_free ((free_fn)0x49827a)
#define rt_calloc ((calloc_fn)0x49946c)
#define rt_strcpy ((strcpy_fn)0x498450)
#define rt_strcat ((strcpy_fn)0x498460)
#define rt_strncpy ((strncpy_fn)0x498350)
#define rt_vsnprintf ((vsnprintf_fn)0x49972a)
#define rt_isctype ((isctype_fn)0x498051)
#define game_winmain ((winmain_fn)0x4351e0)
#define rt_exit ((exit_fn)0x498c89)

/* ------------------------------------------------------------ runtime data */

#define g_osplatform G32(0x4db298)
#define g_winver G32(0x4db29c)
#define g_winmajor G32(0x4db2a0)
#define g_winminor G32(0x4db2a4)
#define g_argc G32(0x4db2a8)
#define g_argv G32(0x4db2ac)
#define g_environ G32(0x4db2b4)
#define g_pgmptr G32(0x4db2c4)
#define g_exitflag G8(0x4db2cc)
#define g_c_termination G32(0x4db2d0)
#define g_c_exit_done G32(0x4db2d4)
#define g_aenvptr GPTR(0x4db2d8)
#define g_error_mode GI32(0x4db2e0)
#define g_clock_start 0x4db2e8 /* 64-bit */
#define g_cflush GI32(0x4db2f0)
#define g_stdbuf 0x4db2f4 /* [2] */
#define g_stringtype_f GI32(0x4db2fc)
#define g_lc_ctype G32(0x4db37c)
#define g_lc_codepage G32(0x4db38c)
#define g_lcmap_f GI32(0x4db394)
#define g_sysfixed_cp GI32(0x4db398)
#define g_pgmname 0x4db39c /* [0x104] */
#define g_crt_message_box G32(0x4db50c)
#define g_crt_active_window G32(0x4db510)
#define g_crt_last_popup G32(0x4db514)
#define g_commode G32(0x4db518)
#define g_fmode G32(0x4db540)
#define g_umaskval G32(0x4db294)
#define g_adjust_fdiv G32(0x4db290)
#define g_fastflag G32(0x4db28c)
#define g_pnhheap 0x4db4a4 /* the banner hook (*_pRawDllMain-like, 0) */
#define g_env_f GI32(0x4db4a0)
#define g_app_type GI32(0x4b4d84)
#define g_aexit_rtn G32(0x4b4d80)
#define g_fpinit G32(0x4b4aac)
#define g_iob 0x4b4e40u
#define g_iob_end 0x4b50c0u
#define IOB(i) ((uint8_t *)(uintptr_t)(g_iob + 0x20 * (i)))
#define g_stdout IOB(1)
#define g_stderr IOB(2)
#define g_locktable 0x4b50c4u
#define g_tlsindex G32(0x4b5294)
#define g_errtable 0x4b5550u /* {oserr, errno} x 45 */
#define g_badioinfo ((uint8_t *)0x4b5750)
#define g_xcptacttab 0x4b56b8u
#define g_rterrs 0x4b5778u /* {number, text} x 18 */
#define g_pioinfo 0x6d0f20u /* [64] */
#define g_nhandle G32(0x6d1020)
#define g_mbcodepage G32(0x6d1024)
#define g_mbulinfo 0x6d1030u /* [3] */
#define g_ismbcodepage GI32(0x6d103c)
#define g_mbcasemap ((uint8_t *)0x6d1040)
#define g_mbctype ((uint8_t *)0x6d1140) /* [257]: entry c at +c+1 */
#define g_mblcid G32(0x6d1244)
#define g_crtheap G32(0x6d1268)
#define g_piob GPTR(0x6d126c)
#define g_nstream GI32(0x6d2280)
#define g_acmdln GPTR(0x6d2284)
#define g_env_initialized G32(0x6d2288)
#define g_mbctype_initialized GI32(0x6d228c)
#define g_onexitend G32(0x6d2290)
#define g_onexitbegin G32(0x6d2294)
#define g_math_flag GI32(0x4b5948) /* nonzero: no _matherr call */

/* FILE: +0 _ptr, +4 _cnt, +8 _base, +0xc _flag, +0x10 _file, +0x14
 * _charbuf, +0x18 _bufsiz, +0x1c _tmpfname; a stream from __piob beyond
 * the 20 in _iob has its lock (a CRITICAL_SECTION) at +0x20. */
#define F_PTR(s) PTR(s, 0)
#define F_CNT(s) I32(s, 4)
#define F_BASE(s) PTR(s, 8)
#define F_FLAG(s) U32(s, 0xc)
#define F_FILE(s) I32(s, 0x10)
#define F_BUFSIZ(s) I32(s, 0x18)
#define F_TMPFNAME(s) PTR(s, 0x1c)

#define IOREAD 0x1
#define IOWRT 0x2
#define IONBF 0x4
#define IOMYBUF 0x8
#define IOEOF 0x10
#define IOERR 0x20
#define IOSTRG 0x40
#define IORW 0x80
#define IOYOURBUF 0x100
#define IOSETVBUF 0x400
#define IOFLRTN 0x1000
#define IOCTRLZ 0x2000
#define IOCOMMIT 0x4000

/* ioinfo (0x24 bytes): +0 osfhnd, +4 osfile, +5 pipech, +8 lockinitflag,
 * +0xc lock. */
#define FOPEN 0x01
#define FEOFLAG 0x02
#define FCRLF 0x04
#define FPIPE 0x08
#define FNOINHERIT 0x10
#define FAPPEND 0x20
#define FDEV 0x40
#define FTEXT 0x80

static uint8_t *pioinfo(int32_t fh)
{
    return GPTR(g_pioinfo + 4 * (uint32_t)(fh >> 5)) + (fh & 0x1f) * 0x24;
}

#define OSFHND(fh) I32(pioinfo(fh), 0)
#define OSFILE(fh) U8(pioinfo(fh), 4)
#define PIPECH(fh) U8(pioinfo(fh), 5)

static void copy_bytes(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (s < d && d < s + n) { /* the runtime's memcpy copies overlaps safely */
        while (n--)
            d[n] = s[n];
    } else {
        while (n--)
            *d++ = *s++;
    }
}

static uint32_t str_len(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (uint32_t)(p - s);
}

/* ======================================================= per-thread data */

/* 0x49bcde: _initptd. */
void GAMEAPI crtc_initptd(uint8_t *ptd)
{
    U32(ptd, 0x50) = g_xcptacttab;
    U32(ptd, 0x14) = 1;
}

/* 0x49bcf1: _getptd: this thread's data, made on first use (the last
 * error is kept). */
uint8_t *GAMEAPI crtc_getptd(void)
{
    uint32_t error = GetLastError_();
    uint8_t *ptd = (uint8_t *)(uintptr_t)TlsGetValue_(g_tlsindex);
    if (ptd == 0) {
        ptd = rt_calloc(1, 0x74);
        if (ptd != 0 && TlsSetValue_(g_tlsindex, ptd) != 0) {
            crtc_initptd(ptd);
            U32(ptd, 0) = GetCurrentThreadId_();
            U32(ptd, 4) = 0xffffffffu;
        } else {
            crtc_amsg_exit(0x10);
        }
    }
    SetLastError_(error);
    return ptd;
}

/* 0x49bd58: _freeptd. */
static void freeptd(uint8_t *ptd)
{
    if (g_tlsindex == 0xffffffffu)
        return;
    if (ptd != 0 || (ptd = (uint8_t *)(uintptr_t)TlsGetValue_(g_tlsindex)) != 0) {
        if (U32(ptd, 0x24))
            rt_free(PTR(ptd, 0x24));
        if (U32(ptd, 0x28))
            rt_free(PTR(ptd, 0x28));
        if (U32(ptd, 0x30))
            rt_free(PTR(ptd, 0x30));
        if (U32(ptd, 0x38))
            rt_free(PTR(ptd, 0x38));
        if (U32(ptd, 0x40))
            rt_free(PTR(ptd, 0x40));
        if (U32(ptd, 0x44))
            rt_free(PTR(ptd, 0x44));
        if (U32(ptd, 0x50) != g_xcptacttab)
            rt_free(PTR(ptd, 0x50));
        rt_free(ptd);
    }
    TlsSetValue_(g_tlsindex, 0);
}

/* 0x49d48e / 0x49d497: errno and _doserrno (the thread's +8 / +0xc). */
int32_t *GAMEAPI crtc_errno(void)
{
    return (int32_t *)(crtc_getptd() + 8);
}

int32_t *GAMEAPI crtc_doserrno(void)
{
    return (int32_t *)(crtc_getptd() + 0xc);
}

/* 0x49d41b: _dosmaperr: a Windows error to errno (table 0x4b5550). */
void GAMEAPI crtc_dosmaperr(uint32_t oserr)
{
    uint32_t i;
    *crtc_doserrno() = (int32_t)oserr;
    for (i = 0; i < 45; i++) {
        if (G32(g_errtable + 8 * i) == oserr) {
            *crtc_errno() = GI32(g_errtable + 8 * i + 4);
            return;
        }
    }
    if (oserr >= 0x13 && oserr <= 0x24)
        *crtc_errno() = 0xd; /* EACCES: write-protect .. sharing errors */
    else if (oserr >= 0xbc && oserr <= 0xca)
        *crtc_errno() = 8; /* ENOEXEC */
    else
        *crtc_errno() = 0x16; /* EINVAL */
}

/* =========================================================== the locks */

/* 0x49b138: _mtinitlocks: the four preallocated locks. */
static void mtinitlocks(void)
{
    InitializeCriticalSection_(G32(0x4b5108));
    InitializeCriticalSection_(G32(0x4b50f8));
    InitializeCriticalSection_(G32(0x4b50e8));
    InitializeCriticalSection_(G32(0x4b50c8));
}

/* 0x49b161: _lock: a runtime lock (table 0x4b50c4), made on first use. */
void GAMEAPI crtc_lock(int32_t n)
{
    uint32_t *slot = (uint32_t *)(uintptr_t)(g_locktable + 4 * (uint32_t)n);
    if (*slot == 0) {
        void *cs = rt_malloc(0x18);
        if (cs == 0)
            crtc_amsg_exit(0x11);
        crtc_lock(0x11);
        if (*slot == 0) {
            InitializeCriticalSection_(cs);
            *slot = A(cs);
        } else {
            rt_free(cs);
        }
        crtc_unlock(0x11);
    }
    EnterCriticalSection_(*slot);
}

/* 0x49b1c2: _unlock. */
void GAMEAPI crtc_unlock(int32_t n)
{
    LeaveCriticalSection_(G32(g_locktable + 4 * (uint32_t)n));
}

/* 0x49ad88 / 0x49adda: lock a stream (an _iob entry by its runtime lock
 * 0x1c + index, any other by its own critical section). */
static void lock_file(uint8_t *s)
{
    uint32_t a = A(s);
    if (a >= g_iob && a <= 0x4b50a0)
        crtc_lock((int32_t)((a - g_iob) >> 5) + 0x1c);
    else
        EnterCriticalSection_(s + 0x20);
}

static void unlock_file(uint8_t *s)
{
    uint32_t a = A(s);
    if (a >= g_iob && a <= 0x4b50a0)
        crtc_unlock((int32_t)((a - g_iob) >> 5) + 0x1c);
    else
        LeaveCriticalSection_(s + 0x20);
}

/* 0x49adb7 / 0x49ae09: the same by index into __piob. */
static void lock_file2(int32_t i, uint8_t *s)
{
    if (i < 0x14)
        crtc_lock(i + 0x1c);
    else
        EnterCriticalSection_(s + 0x20);
}

static void unlock_file2(int32_t i, uint8_t *s)
{
    if (i < 0x14)
        crtc_unlock(i + 0x1c);
    else
        LeaveCriticalSection_(s + 0x20);
}

/* =================================================== low-level handles */

/* 0x4a0265 / 0x4a02c4: _lock_fhandle / _unlock_fhandle. */
static void lock_fhandle(int32_t fh)
{
    uint8_t *io = pioinfo(fh);
    if (I32(io, 8) == 0) {
        crtc_lock(0x11);
        if (I32(io, 8) == 0) {
            InitializeCriticalSection_(io + 0xc);
            I32(io, 8)++;
        }
        crtc_unlock(0x11);
    }
    EnterCriticalSection_(pioinfo(fh) + 0xc);
}

static void unlock_fhandle(int32_t fh)
{
    LeaveCriticalSection_(pioinfo(fh) + 0xc);
}

static void ioinfo_block_init(uint8_t *block)
{
    uint8_t *p;
    for (p = block; p < block + 0x480; p += 0x24) {
        U8(p, 4) = 0;
        I32(p, 0) = -1;
        U32(p, 8) = 0;
        U8(p, 5) = 10;
    }
}

/* 0x4a0005: _alloc_osfhnd: a free handle number, returned locked (-1 if
 * none: 64 blocks of 32). */
static int32_t alloc_osfhnd(void)
{
    int32_t fh = -1, i;
    crtc_lock(0x12);
    for (i = 0; i < 64; i++) {
        uint8_t *block = GPTR(g_pioinfo + 4 * i), *p;
        if (block == 0) {
            block = rt_malloc(0x480);
            if (block != 0) {
                g_nhandle += 0x20;
                GPTR(g_pioinfo + 4 * i) = block;
                ioinfo_block_init(block);
                fh = i << 5;
                lock_fhandle(fh);
            }
            break;
        }
        for (p = block; p < GPTR(g_pioinfo + 4 * i) + 0x480; p += 0x24) {
            if (U8(p, 4) & FOPEN)
                continue;
            if (I32(p, 8) == 0) {
                crtc_lock(0x11);
                if (I32(p, 8) == 0) {
                    InitializeCriticalSection_(p + 0xc);
                    I32(p, 8)++;
                }
                crtc_unlock(0x11);
            }
            EnterCriticalSection_(p + 0xc);
            if (!(U8(p, 4) & FOPEN)) {
                I32(p, 0) = -1;
                fh = (int32_t)((p - GPTR(g_pioinfo + 4 * i)) / 0x24) + i * 0x20;
                goto done;
            }
            LeaveCriticalSection_(p + 0xc);
        }
    }
done:
    crtc_unlock(0x12);
    return fh;
}

static void ebadf(void)
{
    *crtc_errno() = 9;
    *crtc_doserrno() = 0;
}

/* 0x4a0128: _set_osfhnd. */
static int32_t set_osfhnd(int32_t fh, uint32_t h)
{
    if ((uint32_t)fh < g_nhandle && OSFHND(fh) == -1) {
        if (g_app_type == 1) {
            if (fh == 0)
                SetStdHandle_(0xfffffff6u, h);
            else if (fh == 1)
                SetStdHandle_(0xfffffff5u, h);
            else if (fh == 2)
                SetStdHandle_(0xfffffff4u, h);
        }
        OSFHND(fh) = (int32_t)h;
        return 0;
    }
    ebadf();
    return -1;
}

/* 0x4a01a4: _free_osfhnd. */
static int32_t free_osfhnd(int32_t fh)
{
    if ((uint32_t)fh < g_nhandle && (OSFILE(fh) & FOPEN) && OSFHND(fh) != -1) {
        if (g_app_type == 1) {
            if (fh == 0)
                SetStdHandle_(0xfffffff6u, 0);
            else if (fh == 1)
                SetStdHandle_(0xfffffff5u, 0);
            else if (fh == 2)
                SetStdHandle_(0xfffffff4u, 0);
        }
        OSFHND(fh) = -1;
        return 0;
    }
    ebadf();
    return -1;
}

/* 0x4a0223: _get_osfhandle. */
static int32_t get_osfhandle(int32_t fh)
{
    if ((uint32_t)fh < g_nhandle && (OSFILE(fh) & FOPEN))
        return OSFHND(fh);
    ebadf();
    return -1;
}

static int32_t fh_valid(int32_t fh)
{
    return (uint32_t)fh < g_nhandle && (OSFILE(fh) & FOPEN);
}

/* 0x49e76a: _lseek_lk. */
static int32_t lseek_lk(int32_t fh, int32_t pos, uint32_t method)
{
    uint32_t h = (uint32_t)get_osfhandle(fh), newpos, error;
    if (h == 0xffffffffu) {
        *crtc_errno() = 9;
        return -1;
    }
    newpos = SetFilePointer_(h, (uint32_t)pos, 0, method);
    error = newpos == 0xffffffffu ? GetLastError_() : 0;
    if (error == 0) {
        OSFILE(fh) &= ~FEOFLAG;
        return (int32_t)newpos;
    }
    crtc_dosmaperr(error);
    return -1;
}

/* 0x49e705: _lseek. */
static int32_t crt_lseek(int32_t fh, int32_t pos, uint32_t method)
{
    int32_t r;
    if (!fh_valid(fh)) {
        ebadf();
        return -1;
    }
    lock_fhandle(fh);
    r = lseek_lk(fh, pos, method);
    unlock_fhandle(fh);
    return r;
}

/* 0x49e1e9: _read_lk: up to `cnt` bytes, text mode turning CR-LF into LF
 * and stopping at ^Z. */
static int32_t read_lk(int32_t fh, char *buf, uint32_t cnt)
{
    int32_t bytes_read = 0;
    char *p, *q, *end, *dst = buf;
    uint32_t os_read, error;
    char peek;
    if (cnt == 0 || (OSFILE(fh) & FEOFLAG))
        return 0;
    if ((OSFILE(fh) & (FPIPE | FDEV)) && PIPECH(fh) != 10) {
        cnt--;
        *dst++ = (char)PIPECH(fh);
        bytes_read = 1;
        PIPECH(fh) = 10;
    }
    if (ReadFile_(OSFHND(fh), dst, cnt, &os_read, 0) == 0) {
        error = GetLastError_();
        if (error == 5) {
            *crtc_errno() = 9;
            *crtc_doserrno() = 5;
        } else {
            if (error == 0x6d) /* broken pipe: end of file */
                return 0;
            crtc_dosmaperr(error);
        }
        return -1;
    }
    if (!(OSFILE(fh) & FTEXT))
        return bytes_read + (int32_t)os_read;
    if (os_read != 0 && *buf == '\n')
        OSFILE(fh) |= FCRLF;
    else
        OSFILE(fh) &= ~FCRLF;
    end = buf + bytes_read + os_read;
    p = q = buf;
    while (p < end) {
        if (*p == 0x1a) {
            if (!(OSFILE(fh) & FDEV))
                OSFILE(fh) |= FEOFLAG;
            break;
        }
        if (*p != '\r') {
            *q++ = *p++;
            continue;
        }
        if (p < end - 1) {
            if (p[1] == '\n') {
                p += 2;
                *q++ = '\n';
            } else {
                *q++ = *p++;
            }
            continue;
        }
        p++;
        if ((ReadFile_(OSFHND(fh), &peek, 1, &os_read, 0) == 0 && GetLastError_() != 0) || os_read == 0) {
            *q++ = '\r';
        } else if (OSFILE(fh) & (FDEV | FPIPE)) {
            if (peek == '\n') {
                *q++ = '\n';
            } else {
                *q++ = '\r';
                PIPECH(fh) = (uint8_t)peek;
            }
        } else if (q == buf && peek == '\n') {
            *q++ = '\n';
        } else {
            lseek_lk(fh, -1, 1);
            if (peek != '\n')
                *q++ = '\r';
        }
    }
    return (int32_t)(q - buf);
}

/* 0x49e184: _read. */
static int32_t crt_read(int32_t fh, char *buf, uint32_t cnt)
{
    int32_t r;
    if (!fh_valid(fh)) {
        ebadf();
        return -1;
    }
    lock_fhandle(fh);
    r = read_lk(fh, buf, cnt);
    unlock_fhandle(fh);
    return r;
}

/* 0x49ec7b: _write_lk: text mode writes LF as CR-LF through a 1 KB
 * buffer; append mode seeks to the end first. */
static int32_t write_lk(int32_t fh, const char *buf, uint32_t cnt)
{
    uint32_t written, charcount = 0, lfcount = 0, error = 0;
    char lfbuf[0x404];
    if (cnt == 0)
        return 0;
    if (OSFILE(fh) & FAPPEND)
        lseek_lk(fh, 0, 2);
    if (OSFILE(fh) & FTEXT) {
        const char *p = buf;
        while ((uint32_t)(p - buf) < cnt) {
            char *q = lfbuf;
            int32_t n;
            while ((uint32_t)(p - buf) < cnt) {
                char ch = *p++;
                if (ch == '\n') {
                    lfcount++;
                    *q++ = '\r';
                }
                *q++ = ch;
                if (q - lfbuf >= 0x400)
                    break;
            }
            n = (int32_t)(q - lfbuf);
            if (WriteFile_(OSFHND(fh), lfbuf, (uint32_t)n, &written, 0) == 0) {
                error = GetLastError_();
                break;
            }
            charcount += written;
            if ((int32_t)written < n)
                break;
        }
    } else {
        if (WriteFile_(OSFHND(fh), buf, cnt, &written, 0) != 0) {
            error = 0;
            charcount = written;
        } else {
            error = GetLastError_();
        }
    }
    if (charcount != 0)
        return (int32_t)(charcount - lfcount);
    if (error != 0) {
        if (error == 5) {
            *crtc_errno() = 9;
            *crtc_doserrno() = 5;
        } else {
            crtc_dosmaperr(error);
        }
        return -1;
    }
    if ((OSFILE(fh) & FDEV) && *buf == 0x1a)
        return 0;
    *crtc_errno() = 0x1c; /* ENOSPC */
    *crtc_doserrno() = 0;
    return -1;
}

/* 0x49ec16: _write. */
static int32_t crt_write(int32_t fh, const char *buf, uint32_t cnt)
{
    int32_t r;
    if (!fh_valid(fh)) {
        ebadf();
        return -1;
    }
    lock_fhandle(fh);
    r = write_lk(fh, buf, cnt);
    unlock_fhandle(fh);
    return r;
}

/* 0x49ead5: _close_lk (stdout and stderr sharing one handle close once). */
static int32_t close_lk(int32_t fh)
{
    uint32_t error = 0;
    if (get_osfhandle(fh) != -1) {
        int32_t shared = 0;
        if (fh == 1 || fh == 2) {
            int32_t h2 = get_osfhandle(2);
            shared = get_osfhandle(1) == h2;
        }
        if (shared) {
            /* the other one still uses it */
        } else if (CloseHandle_(get_osfhandle(fh)) == 0) {
            error = GetLastError_();
        }
    }
    free_osfhnd(fh);
    OSFILE(fh) = 0;
    if (error == 0)
        return 0;
    crtc_dosmaperr(error);
    return -1;
}

/* 0x49ea78: _close. */
static int32_t crt_close(int32_t fh)
{
    int32_t r;
    if (!fh_valid(fh)) {
        ebadf();
        return -1;
    }
    lock_fhandle(fh);
    r = close_lk(fh);
    unlock_fhandle(fh);
    return r;
}

/* 0x49eb83: _commit: FlushFileBuffers. */
static int32_t crt_commit(int32_t fh)
{
    int32_t r;
    if ((uint32_t)fh >= g_nhandle || !(OSFILE(fh) & FOPEN)) {
        *crtc_errno() = 9;
        return -1;
    }
    lock_fhandle(fh);
    if (OSFILE(fh) & FOPEN) {
        uint32_t error = FlushFileBuffers_(get_osfhandle(fh)) == 0 ? GetLastError_() : 0;
        if (error == 0) {
            unlock_fhandle(fh);
            return 0;
        }
        *crtc_doserrno() = (int32_t)error;
    }
    *crtc_errno() = 9;
    r = -1;
    unlock_fhandle(fh);
    return r;
}

/* 0x4a1bc2: _setmode_lk: text / binary, returns the old mode. */
static int32_t setmode_lk(int32_t fh, int32_t mode)
{
    uint8_t old = OSFILE(fh);
    if (mode == 0x8000) {
        OSFILE(fh) = old & 0x7f;
    } else if (mode == 0x4000) {
        OSFILE(fh) = old | 0x80;
    } else {
        *crtc_errno() = 0x16;
        return -1;
    }
    return (old & 0x80) ? 0x4000 : 0x8000;
}

/* 0x4a1801: _chsize_lk: grow with zeros (binary writes) or truncate. */
static int32_t chsize_lk(int32_t fh, int32_t size)
{
    int32_t filepos, end, extend, r = 0;
    char zeros[0x1000];
    filepos = lseek_lk(fh, 0, 1);
    if (filepos == -1)
        return -1;
    end = lseek_lk(fh, 0, 2);
    if (end == -1)
        return -1;
    extend = size - end;
    if (extend > 0) {
        int32_t oldmode, i;
        for (i = 0; i < 0x1000; i++)
            zeros[i] = 0;
        oldmode = setmode_lk(fh, 0x8000);
        for (;;) {
            int32_t n = write_lk(fh, zeros, (uint32_t)(extend < 0x1000 ? extend : 0x1000));
            if (n == -1) {
                if (*crtc_doserrno() == 5)
                    *crtc_errno() = 0xd;
                r = -1;
                break;
            }
            extend -= n;
            if (extend <= 0)
                break;
        }
        setmode_lk(fh, oldmode);
    } else if (extend < 0) {
        lseek_lk(fh, size, 0);
        r = SetEndOfFile_(get_osfhandle(fh)) != 0 ? 0 : -1;
        if (r == -1) {
            uint32_t error;
            *crtc_errno() = 0xd;
            error = GetLastError_();
            *crtc_doserrno() = (int32_t)error;
        }
    }
    lseek_lk(fh, filepos, 0);
    return r;
}

/* 0x4a02e6: _sopen: CreateFileA from the _O_ flags, a handle number for
 * it; a text read-write file ending in ^Z has the ^Z cut off. */
static int32_t crt_sopen(const char *path, uint32_t oflag, uint32_t shflag, uint32_t pmode)
{
    uint32_t sa[3], access, share, create, attrib, h, ftype;
    uint8_t fileflags;
    int32_t fh, r;
    sa[0] = 0xc;
    sa[1] = 0;
    if (oflag & 0x80) { /* _O_NOINHERIT */
        sa[2] = 0;
        fileflags = FNOINHERIT;
    } else {
        fileflags = 0;
        sa[2] = 1;
    }
    if (!(oflag & 0x8000) && ((oflag & 0x4000) || g_fmode != 0x8000))
        fileflags |= FTEXT;
    switch (oflag & 3) {
    case 0:
        access = 0x80000000u;
        break;
    case 1:
        access = 0x40000000u;
        break;
    case 2:
        access = 0xc0000000u;
        break;
    default:
        goto einval;
    }
    switch (shflag) {
    case 0x10:
        share = 0;
        break;
    case 0x20:
        share = 1;
        break;
    case 0x30:
        share = 2;
        break;
    case 0x40:
        share = 3;
        break;
    default:
        goto einval;
    }
    switch (oflag & 0x700) {
    case 0:
    case 0x400:
        create = 3; /* OPEN_EXISTING */
        break;
    case 0x100:
        create = 4; /* OPEN_ALWAYS */
        break;
    case 0x200:
    case 0x600:
        create = 5; /* TRUNCATE_EXISTING */
        break;
    case 0x300:
        create = 2; /* CREATE_ALWAYS */
        break;
    case 0x500:
    case 0x700:
        create = 1; /* CREATE_NEW */
        break;
    default:
    einval:
        *crtc_errno() = 0x16;
        *crtc_doserrno() = 0;
        return -1;
    }
    attrib = 0x80;
    if ((oflag & 0x100) && !(~g_umaskval & pmode & 0x80))
        attrib = 1; /* read-only */
    if (oflag & 0x40) { /* _O_TEMPORARY */
        attrib |= 0x4000000;
        access |= 0x10000;
    }
    if (oflag & 0x1000)
        attrib |= 0x100;
    if (oflag & 0x20)
        attrib |= 0x8000000;
    else if (oflag & 0x10)
        attrib |= 0x10000000;
    fh = alloc_osfhnd();
    if (fh == -1) {
        *crtc_errno() = 0x18; /* EMFILE */
        *crtc_doserrno() = 0;
        return -1;
    }
    h = CreateFileA_(path, access, share, sa, create, attrib, 0);
    if (h == 0xffffffffu)
        goto oserror;
    ftype = GetFileType_(h);
    if (ftype == 0) {
        CloseHandle_(h);
        goto oserror;
    }
    if (ftype == 2)
        fileflags |= FDEV;
    else if (ftype == 3)
        fileflags |= FPIPE;
    set_osfhnd(fh, h);
    fileflags |= FOPEN;
    OSFILE(fh) = fileflags;
    r = fh;
    if (!(fileflags & (FDEV | FPIPE)) && (fileflags & FTEXT) && (oflag & 2)) {
        int32_t pos = lseek_lk(fh, -1, 2);
        if (pos == -1) {
            if (*crtc_doserrno() != 0x83)
                goto fail;
        } else {
            char last = 0;
            if ((read_lk(fh, &last, 1) == 0 && last == 0x1a && chsize_lk(fh, pos) == -1) || lseek_lk(fh, 0, 0) == -1)
                goto fail;
        }
    }
    if (!(fileflags & (FDEV | FPIPE)) && (oflag & 8))
        OSFILE(fh) |= FAPPEND;
    unlock_fhandle(fh);
    return r;
fail:
    crt_close(fh);
    unlock_fhandle(fh);
    return -1;
oserror:
    crtc_dosmaperr(GetLastError_());
    unlock_fhandle(fh);
    return -1;
}

/* 0x49f972: _isatty. */
static int32_t crt_isatty(int32_t fh)
{
    if ((uint32_t)fh >= g_nhandle)
        return 0;
    return OSFILE(fh) & FDEV;
}

/* ================================================================= stdio */

/* 0x49f92e: _getbuf: a 4 KB buffer, else the 2-byte _charbuf. */
static void getbuf(uint8_t *s)
{
    g_cflush++;
    F_BASE(s) = rt_malloc(0x1000);
    if (F_BASE(s) == 0) {
        F_FLAG(s) |= IONBF;
        F_BASE(s) = s + 0x14;
        F_BUFSIZ(s) = 2;
    } else {
        F_FLAG(s) |= IOMYBUF;
        F_BUFSIZ(s) = 0x1000;
    }
    F_CNT(s) = 0;
    F_PTR(s) = F_BASE(s);
}

/* 0x49eb58: _freebuf. */
static void freebuf(uint8_t *s)
{
    if ((F_FLAG(s) & (IOREAD | IOWRT | IORW)) && (F_FLAG(s) & IOMYBUF)) {
        rt_free(F_BASE(s));
        U16(s, 0xc) &= 0xfbf7;
        F_PTR(s) = 0;
        F_BASE(s) = 0;
        F_CNT(s) = 0;
    }
}

/* 0x4995d3: _flush: write out the buffer of a writing stream. */
static int32_t flush(uint8_t *s)
{
    int32_t r = 0;
    if ((F_FLAG(s) & 3) == IOWRT && (F_FLAG(s) & (IOMYBUF | IOYOURBUF))) {
        int32_t n = (int32_t)(F_PTR(s) - F_BASE(s));
        if (n > 0) {
            if (crt_write(F_FILE(s), (const char *)F_BASE(s), (uint32_t)n) == n) {
                if (F_FLAG(s) & IORW)
                    F_FLAG(s) &= ~IOWRT;
            } else {
                F_FLAG(s) |= IOERR;
                r = -1;
            }
        }
    }
    F_CNT(s) = 0;
    F_PTR(s) = F_BASE(s);
    return r;
}

/* 0x4995a5: _fflush_lk. */
static int32_t fflush_lk(uint8_t *s)
{
    if (flush(s) != 0)
        return -1;
    if (F_FLAG(s) & IOCOMMIT)
        return crt_commit(F_FILE(s)) != 0 ? -1 : 0;
    return 0;
}

/* 0x499638: flsall: flush every stream (1: count the flushed, 0: only
 * writing ones, -1 on any failure). */
static int32_t flsall(int32_t mode)
{
    int32_t count = 0, err = 0, i;
    crtc_lock(2);
    for (i = 0; i < g_nstream; i++) {
        uint8_t *s = PTR(g_piob, 4 * i);
        if (s != 0 && (F_FLAG(s) & (IOREAD | IOWRT | IORW))) {
            lock_file2(i, s);
            s = PTR(g_piob, 4 * i);
            if (F_FLAG(s) & (IOREAD | IOWRT | IORW)) {
                if (mode == 1) {
                    if (fflush_lk(s) != -1)
                        count++;
                } else if (mode == 0 && (F_FLAG(s) & IOWRT)) {
                    if (fflush_lk(s) == -1)
                        err = -1;
                }
            }
            unlock_file2(i, PTR(g_piob, 4 * i));
        }
    }
    crtc_unlock(2);
    return mode == 1 ? count : err;
}

/* 0x499576: fflush (NULL: every writing stream). */
int32_t GAMEAPI crtc_fflush(uint8_t *s)
{
    int32_t r;
    if (s == 0)
        return flsall(0);
    lock_file(s);
    r = fflush_lk(s);
    unlock_file(s);
    return r;
}

/* 0x499c46: _flsbuf: put a character when the buffer is full (or there is
 * none yet), writing out what is buffered. */
static int32_t flsbuf(int32_t ch, uint8_t *s)
{
    uint32_t flag = F_FLAG(s);
    int32_t fh = F_FILE(s), written, count;
    char c = (char)ch;
    if (!(flag & (IOWRT | IORW)) || (flag & IOSTRG))
        goto error;
    if (flag & IOREAD) {
        F_CNT(s) = 0;
        if (!(flag & IOEOF))
            goto error;
        F_PTR(s) = F_BASE(s);
        F_FLAG(s) = flag & ~IOREAD;
    }
    flag = F_FLAG(s);
    F_CNT(s) = 0;
    written = 0;
    F_FLAG(s) = (flag & ~IOEOF) | IOWRT;
    if (!(flag & (IOMYBUF | IONBF | IOYOURBUF)) && ((s != g_stdout && s != g_stderr) || crt_isatty(fh) == 0))
        getbuf(s);
    if (U16(s, 0xc) & (IOMYBUF | IOYOURBUF)) {
        uint8_t *base = F_BASE(s);
        count = (int32_t)(F_PTR(s) - base);
        F_PTR(s) = base + 1;
        F_CNT(s) = F_BUFSIZ(s) - 1;
        if (count > 0) {
            written = crt_write(fh, (const char *)base, (uint32_t)count);
        } else {
            uint8_t *io = fh == -1 ? g_badioinfo : pioinfo(fh);
            if (U8(io, 4) & FAPPEND)
                crt_lseek(fh, 0, 2);
        }
        *F_BASE(s) = (uint8_t)c;
    } else {
        count = 1;
        written = crt_write(fh, &c, 1);
    }
    if (written == count)
        return ch & 0xff;
    F_FLAG(s) |= IOERR;
    return -1;
error:
    F_FLAG(s) = flag | IOERR;
    return -1;
}

static int32_t putc_lk(int32_t ch, uint8_t *s)
{
    if (--F_CNT(s) >= 0) {
        *F_PTR(s)++ = (uint8_t)ch;
        return ch & 0xff;
    }
    return flsbuf(ch, s);
}

/* 0x49e0a8: _filbuf: refill the buffer, return its first character. */
static int32_t filbuf(uint8_t *s)
{
    uint32_t flag = F_FLAG(s);
    if (!(flag & (IOREAD | IOWRT | IORW)) || (flag & IOSTRG))
        return -1;
    if (flag & IOWRT) {
        F_FLAG(s) = flag | IOERR;
        return -1;
    }
    F_FLAG(s) = flag | IOREAD;
    if (!(flag & (IOMYBUF | IONBF | IOYOURBUF)))
        getbuf(s);
    else
        F_PTR(s) = F_BASE(s);
    F_CNT(s) = crt_read(F_FILE(s), (char *)F_BASE(s), (uint32_t)F_BUFSIZ(s));
    if (F_CNT(s) == 0 || F_CNT(s) == -1) {
        F_FLAG(s) |= F_CNT(s) != 0 ? IOERR : IOEOF;
        F_CNT(s) = 0;
        return -1;
    }
    if (!(F_FLAG(s) & (IOWRT | IORW))) {
        int32_t fh = F_FILE(s);
        uint8_t *io = fh == -1 ? g_badioinfo : pioinfo(fh);
        if ((U8(io, 4) & (FTEXT | FEOFLAG)) == (FTEXT | FEOFLAG))
            F_FLAG(s) |= IOCTRLZ;
    }
    if (F_BUFSIZ(s) == 0x200 && (F_FLAG(s) & IOMYBUF) && !(F_FLAG(s) & IOSETVBUF))
        F_BUFSIZ(s) = 0x1000;
    F_CNT(s)--;
    return *F_PTR(s)++;
}

/* 0x4990bd: _fread_lk. */
static uint32_t fread_lk(uint8_t *data, uint32_t size, uint32_t num, uint8_t *s)
{
    uint32_t total = size * num, count = total, bufsize;
    if (total == 0)
        return 0;
    bufsize = (F_FLAG(s) & (IOMYBUF | IONBF | IOYOURBUF)) ? (uint32_t)F_BUFSIZ(s) : 0x1000;
    while (count != 0) {
        if ((F_FLAG(s) & (IOMYBUF | IONBF | IOYOURBUF)) && F_CNT(s) != 0) {
            uint32_t n = count < (uint32_t)F_CNT(s) ? count : (uint32_t)F_CNT(s);
            copy_bytes(data, F_PTR(s), n);
            count -= n;
            F_CNT(s) -= (int32_t)n;
            F_PTR(s) += n;
            data += n;
        } else if (count >= bufsize) {
            uint32_t n = bufsize ? count - count % bufsize : count;
            int32_t got = crt_read(F_FILE(s), (char *)data, n);
            if (got == 0) {
                F_FLAG(s) |= IOEOF;
                return (total - count) / size;
            }
            if (got == -1) {
                F_FLAG(s) |= IOERR;
                return (total - count) / size;
            }
            count -= (uint32_t)got;
            data += got;
        } else {
            int32_t c = filbuf(s);
            if (c == -1)
                return (total - count) / size;
            *data++ = (uint8_t)c;
            count--;
            bufsize = (uint32_t)F_BUFSIZ(s);
        }
    }
    return num;
}

/* 0x49908e: fread. */
uint32_t GAMEAPI crtc_fread(void *buf, uint32_t size, uint32_t num, uint8_t *s)
{
    uint32_t r;
    lock_file(s);
    r = fread_lk(buf, size, num, s);
    unlock_file(s);
    return r;
}

/* 0x49ee06: _fwrite_lk. */
static uint32_t fwrite_lk(const char *data, uint32_t size, uint32_t num, uint8_t *s)
{
    uint32_t total = size * num, count = total, bufsize;
    if (total == 0)
        return 0;
    bufsize = (F_FLAG(s) & (IOMYBUF | IONBF | IOYOURBUF)) ? (uint32_t)F_BUFSIZ(s) : 0x1000;
    while (count != 0) {
        if ((F_FLAG(s) & (IOMYBUF | IOYOURBUF)) && F_CNT(s) != 0) {
            uint32_t n = count < (uint32_t)F_CNT(s) ? count : (uint32_t)F_CNT(s);
            copy_bytes(F_PTR(s), data, n);
            F_CNT(s) -= (int32_t)n;
            F_PTR(s) += n;
            count -= n;
            data += n;
        } else if (count >= bufsize) {
            uint32_t n, w;
            if ((F_FLAG(s) & (IOMYBUF | IOYOURBUF)) && flush(s) != 0)
                return (total - count) / size;
            n = bufsize ? count - count % bufsize : count;
            w = (uint32_t)crt_write(F_FILE(s), data, n);
            if (w == 0xffffffffu) {
                F_FLAG(s) |= IOERR;
                return (total - count) / size;
            }
            count -= w;
            if (w < n) {
                F_FLAG(s) |= IOERR;
                return (total - count) / size;
            }
            data += w;
        } else {
            if (flsbuf(*data, s) == -1)
                return (total - count) / size;
            data++;
            count--;
            bufsize = (uint32_t)F_BUFSIZ(s);
            if ((int32_t)bufsize <= 0)
                bufsize = 1;
        }
    }
    return num;
}

/* 0x49ae2c: _stbuf: stdout / stderr on a device get a temporary 4 KB
 * buffer for one call (1 if given). */
static int32_t stbuf(uint8_t *s)
{
    int32_t i;
    uint32_t *slot;
    if (crt_isatty(F_FILE(s)) == 0)
        return 0;
    if (s == g_stdout)
        i = 0;
    else if (s == g_stderr)
        i = 1;
    else
        return 0;
    g_cflush++;
    if (F_FLAG(s) & (IOMYBUF | IONBF | IOYOURBUF))
        return 0;
    slot = (uint32_t *)(uintptr_t)(g_stdbuf + 4 * i);
    if (*slot == 0) {
        *slot = A(rt_malloc(0x1000));
        if (*slot == 0) {
            F_BASE(s) = s + 0x14;
            F_PTR(s) = s + 0x14;
            F_BUFSIZ(s) = 2;
            F_CNT(s) = 2;
            goto done;
        }
    }
    F_BUFSIZ(s) = 0x1000;
    F_BASE(s) = (uint8_t *)(uintptr_t)*slot;
    F_PTR(s) = (uint8_t *)(uintptr_t)*slot;
    F_CNT(s) = 0x1000;
done:
    U16(s, 0xc) |= 0x1102; /* IOWRT | IOYOURBUF | IOFLRTN */
    return 1;
}

/* 0x49aeb9: _ftbuf: flush and drop the temporary buffer. */
static void ftbuf(int32_t flag, uint8_t *s)
{
    if (flag != 0 && (F_FLAG(s) & IOFLRTN)) {
        flush(s);
        U8(s, 0xd) &= 0xee;
        F_BUFSIZ(s) = 0;
        F_PTR(s) = 0;
        F_BASE(s) = 0;
    }
}

/* 0x4996dc: fputs: 0, or -1 if not all was written. */
int32_t GAMEAPI crtc_fputs(const char *str, uint8_t *s)
{
    uint32_t len = str_len(str), done;
    int32_t buffing;
    lock_file(s);
    buffing = stbuf(s);
    done = fwrite_lk(str, 1, len, s);
    ftbuf(buffing, s);
    unlock_file(s);
    return done == len ? 0 : -1;
}

/* 0x497f7a: printf: _vsnprintf (group CB's engine) into a buffer, then the
 * text through the stream as _output puts it (stops at the first failed
 * write: -1). */
int32_t GAMEAPI crtc_printf(const char *format, ...)
{
    static char big[0x10000];
    char small[0x1000], *text = small;
    __builtin_va_list ap;
    int32_t buffing, n, i, r;
    lock_file2(1, g_stdout);
    buffing = stbuf(g_stdout);
    __builtin_va_start(ap, format);
    n = rt_vsnprintf(small, sizeof small, format, ap);
    __builtin_va_end(ap);
    if (n < 0) { /* longer than 4 KB: the stdout lock guards the big buffer */
        text = big;
        __builtin_va_start(ap, format);
        n = rt_vsnprintf(big, sizeof big, format, ap);
        __builtin_va_end(ap);
        if (n < 0)
            n = sizeof big;
    }
    r = n;
    for (i = 0; i < n; i++) {
        if (putc_lk((uint8_t)text[i], g_stdout) == -1) {
            r = -1;
            break;
        }
    }
    ftbuf(buffing, g_stdout);
    unlock_file2(1, g_stdout);
    return r;
}

/* 0x49e94d: _getstream: a free stream from __piob (a new one beyond the
 * _iob entries is allocated with its lock), returned locked. */
static uint8_t *getstream(void)
{
    uint8_t *found = 0;
    int32_t i;
    crtc_lock(2);
    for (i = 0; i < g_nstream; i++) {
        uint8_t *s = PTR(g_piob, 4 * i);
        if (s == 0) {
            PTR(g_piob, 4 * i) = rt_malloc(0x38);
            if (PTR(g_piob, 4 * i) != 0) {
                InitializeCriticalSection_(PTR(g_piob, 4 * i) + 0x20);
                EnterCriticalSection_(PTR(g_piob, 4 * i) + 0x20);
                found = PTR(g_piob, 4 * i);
                goto init;
            }
            break;
        }
        if (!(F_FLAG(s) & (IOREAD | IOWRT | IORW))) {
            lock_file2(i, s);
            s = PTR(g_piob, 4 * i);
            if (!(F_FLAG(s) & (IOREAD | IOWRT | IORW))) {
                found = PTR(g_piob, 4 * i);
                goto init;
            }
            unlock_file2(i, s);
        }
    }
    goto out;
init:
    if (found != 0) {
        F_FILE(found) = -1;
        F_CNT(found) = 0;
        F_FLAG(found) = 0;
        F_BASE(found) = 0;
        F_PTR(found) = 0;
        F_TMPFNAME(found) = 0;
    }
out:
    crtc_unlock(2);
    return found;
}

/* 0x49e7dd: _openfile: the mode string to _O_ and stream flags, _sopen. */
static uint8_t *openfile(const char *name, const char *mode, uint32_t shflag, uint8_t *s)
{
    uint32_t oflag, streamflag = g_commode;
    int32_t whileflag = 1, scanset = 0, commodeset = 0, fh;
    switch (*mode) {
    case 'r':
        oflag = 0;
        streamflag |= IOREAD;
        break;
    case 'w':
        oflag = 0x301;
        streamflag |= IOWRT;
        break;
    case 'a':
        oflag = 0x109;
        streamflag |= IOWRT;
        break;
    default:
        return 0;
    }
    while (*++mode && whileflag) {
        switch (*mode) {
        case '+':
            if (oflag & 2) {
                whileflag = 0;
            } else {
                oflag = (oflag & ~1u) | 2;
                streamflag = (streamflag & ~3u) | IORW;
            }
            break;
        case 'b':
            if (oflag & 0xc000)
                whileflag = 0;
            else
                oflag |= 0x8000;
            break;
        case 't':
            if (oflag & 0xc000)
                whileflag = 0;
            else
                oflag |= 0x4000;
            break;
        case 'c':
            if (commodeset)
                whileflag = 0;
            else {
                commodeset = 1;
                streamflag |= IOCOMMIT;
            }
            break;
        case 'n':
            if (commodeset)
                whileflag = 0;
            else {
                commodeset = 1;
                streamflag &= ~IOCOMMIT;
            }
            break;
        case 'S':
            if (scanset)
                whileflag = 0;
            else {
                scanset = 1;
                oflag |= 0x20;
            }
            break;
        case 'R':
            if (scanset)
                whileflag = 0;
            else {
                scanset = 1;
                oflag |= 0x10;
            }
            break;
        case 'T':
            if (oflag & 0x1000)
                whileflag = 0;
            else
                oflag |= 0x1000;
            break;
        case 'D':
            if (oflag & 0x40)
                whileflag = 0;
            else
                oflag |= 0x40;
            break;
        default:
            whileflag = 0;
            break;
        }
    }
    fh = crt_sopen(name, oflag, shflag, 0x1a4);
    if (fh < 0)
        return 0;
    g_cflush++;
    F_FLAG(s) = streamflag;
    F_CNT(s) = 0;
    F_PTR(s) = 0;
    F_BASE(s) = 0;
    F_TMPFNAME(s) = 0;
    F_FILE(s) = fh;
    return s;
}

/* 0x499459: fopen (_fsopen 0x499428 with _SH_DENYNO). */
uint8_t *GAMEAPI crtc_fopen(const char *name, const char *mode)
{
    uint8_t *s = getstream(), *r;
    if (s == 0)
        return 0;
    r = openfile(name, mode, 0x40, s);
    unlock_file(s);
    return r;
}

/* 0x49952a: _fclose_lk. */
static int32_t fclose_lk(uint8_t *s)
{
    int32_t r = -1;
    if (F_FLAG(s) & (IOREAD | IOWRT | IORW)) {
        r = flush(s);
        freebuf(s);
        if (crt_close(F_FILE(s)) < 0) {
            r = -1;
        } else if (F_TMPFNAME(s) != 0) {
            rt_free(F_TMPFNAME(s));
            F_TMPFNAME(s) = 0;
        }
    }
    F_FLAG(s) = 0;
    return r;
}

/* 0x4994f9: fclose. */
int32_t GAMEAPI crtc_fclose(uint8_t *s)
{
    int32_t r;
    if (F_FLAG(s) & IOSTRG) {
        F_FLAG(s) = 0;
        return -1;
    }
    lock_file(s);
    r = fclose_lk(s);
    unlock_file(s);
    return r;
}

/* 0x499280: _ftell_lk: the position as the caller sees it (text mode
 * counts each buffered LF as CR-LF). */
static int32_t ftell_lk(uint8_t *s)
{
    int32_t fd = F_FILE(s), filepos, offset;
    uint32_t flag;
    if (F_CNT(s) < 0)
        F_CNT(s) = 0;
    filepos = crt_lseek(fd, 0, 1);
    if (filepos < 0)
        return -1;
    flag = F_FLAG(s);
    if (!(flag & (IOMYBUF | IOYOURBUF)))
        return filepos - F_CNT(s);
    offset = (int32_t)(F_PTR(s) - F_BASE(s));
    if (flag & (IOREAD | IOWRT)) {
        if (OSFILE(fd) & FTEXT) {
            uint8_t *p;
            for (p = F_BASE(s); p < F_PTR(s); p++)
                if (*p == '\n')
                    offset++;
        }
    } else if (!(flag & IORW)) {
        *crtc_errno() = 0x16;
        return -1;
    }
    if (filepos == 0)
        return offset;
    if (F_FLAG(s) & IOREAD) {
        uint32_t rdcnt;
        if (F_CNT(s) == 0) {
            offset = 0;
        } else {
            rdcnt = (uint32_t)(F_CNT(s) + (F_PTR(s) - F_BASE(s)));
            if (OSFILE(fd) & FTEXT) {
                if (crt_lseek(fd, 0, 2) == filepos) {
                    uint8_t *p, *max = F_BASE(s) + rdcnt;
                    for (p = F_BASE(s); p < max; p++)
                        if (*p == '\n')
                            rdcnt++;
                    if (U8(s, 0xd) & 0x20) /* IOCTRLZ */
                        rdcnt++;
                } else {
                    crt_lseek(fd, filepos, 0);
                    if (rdcnt <= 0x200 && (F_FLAG(s) & IOMYBUF) && !(F_FLAG(s) & IOSETVBUF))
                        rdcnt = 0x200;
                    else
                        rdcnt = (uint32_t)F_BUFSIZ(s);
                    if (OSFILE(fd) & FCRLF)
                        rdcnt++;
                }
            }
            filepos -= (int32_t)rdcnt;
        }
    }
    return filepos + offset;
}

/* 0x49925e: ftell. */
int32_t GAMEAPI crtc_ftell(uint8_t *s)
{
    int32_t r;
    lock_file(s);
    r = ftell_lk(s);
    unlock_file(s);
    return r;
}

/* 0x4991d1: _fseek_lk. */
static int32_t fseek_lk(uint8_t *s, int32_t offset, int32_t whence)
{
    if (!(F_FLAG(s) & (IOREAD | IOWRT | IORW)) || (whence != 0 && whence != 1 && whence != 2)) {
        *crtc_errno() = 0x16;
        return -1;
    }
    F_FLAG(s) &= ~IOEOF;
    if (whence == 1) {
        offset += ftell_lk(s);
        whence = 0;
    }
    flush(s);
    if (F_FLAG(s) & IORW)
        F_FLAG(s) &= ~3u;
    else if ((F_FLAG(s) & IOREAD) && (F_FLAG(s) & IOMYBUF) && !(F_FLAG(s) & IOSETVBUF))
        F_BUFSIZ(s) = 0x200;
    return crt_lseek(F_FILE(s), offset, (uint32_t)whence) == -1 ? -1 : 0;
}

/* 0x4991a5: fseek. */
int32_t GAMEAPI crtc_fseek(uint8_t *s, int32_t offset, int32_t whence)
{
    int32_t r;
    lock_file(s);
    r = fseek_lk(s, offset, whence);
    unlock_file(s);
    return r;
}

/* 0x49fda1: _fcloseall: close every stream but the standard three, free
 * the allocated ones. */
static int32_t fcloseall(void)
{
    int32_t count = 0, i;
    crtc_lock(2);
    for (i = 3; i < g_nstream; i++) {
        uint8_t *s = PTR(g_piob, 4 * i);
        if (s != 0) {
            if ((F_FLAG(s) & (IOREAD | IOWRT | IORW)) && crtc_fclose(s) != -1)
                count++;
            if (i >= 0x14) {
                DeleteCriticalSection_(PTR(g_piob, 4 * i) + 0x20);
                rt_free(PTR(g_piob, 4 * i));
                PTR(g_piob, 4 * i) = 0;
            }
        }
    }
    crtc_unlock(2);
    return count;
}

/* 0x49accc: _initstdio (a .CRT$XI initialiser): __piob with _nstream (512
 * by default, at least 20) entries, the first 20 pointing at _iob; the
 * standard streams without a handle get _file -1. */
void GAMEAPI crtc_initstdio(void)
{
    int32_t i;
    if (g_nstream == 0)
        g_nstream = 0x200;
    else if (g_nstream < 0x14)
        g_nstream = 0x14;
    g_piob = rt_calloc((uint32_t)g_nstream, 4);
    if (g_piob == 0) {
        g_nstream = 0x14;
        g_piob = rt_calloc(0x14, 4);
        if (g_piob == 0)
            crtc_amsg_exit(0x1a);
    }
    for (i = 0; i < 0x14; i++)
        U32(g_piob, 4 * i) = g_iob + 0x20 * i;
    for (i = 0; i < 3; i++) {
        int32_t h = I32(pioinfo(i), 0);
        if (h == -1 || h == 0)
            F_FILE(IOB(i)) = -1;
    }
}

/* 0x49ad74: _endstdio (the .CRT$XP terminator exit runs): flush all, and
 * close all when exiting without terminating (_cexit); eax as left by them. */
int32_t GAMEAPI crtc_endstdio(void)
{
    int32_t r = flsall(1);
    if (g_exitflag)
        r = fcloseall();
    return r;
}

/* ===================================================== multibyte set-up */

/* 0x49ff3a: _ismbblead. */
static int32_t ismbblead(uint32_t c)
{
    return (g_mbctype[(c & 0xff) + 1] & 4) != 0;
}

/* 0x49bc5f: strncnt: the length of `s`, at most `n`. */
static int32_t strncnt(const char *s, int32_t n)
{
    const char *p = s;
    int32_t i = n;
    while (i != 0 && (i--, *p != 0))
        p++;
    if (*p != 0)
        return n;
    return (int32_t)(p - s);
}

#define WBUF_MAX 0x8000 /* wide buffers beyond this fail, as the original's _alloca would run out */

/* 0x49aee3: __crtGetStringTypeA: GetStringTypeW on the string widened in
 * code page `cp` (else the current one), or GetStringTypeA where only that
 * works. */
int32_t GAMEAPI crtc_get_string_type(uint32_t type, const char *src, int32_t n, uint16_t *out, uint32_t cp,
                                     uint32_t lcid, int32_t error)
{
    uint16_t dummy[2];
    if (g_stringtype_f == 0) {
        if (GetStringTypeW_(1, 0x4a4df4, 1, dummy) != 0)
            g_stringtype_f = 1;
        else if (GetStringTypeA_(0, 1, 0x4a4df0, 1, dummy) != 0)
            g_stringtype_f = 2;
        else
            return 0;
    }
    if (g_stringtype_f == 2) {
        if (lcid == 0)
            lcid = g_lc_ctype;
        return (int32_t)GetStringTypeA_(lcid, type, src, (uint32_t)n, out);
    }
    if (g_stringtype_f == 1) {
        int32_t size, got, i;
        if (cp == 0)
            cp = g_lc_codepage;
        size = (int32_t)MultiByteToWideChar_(cp, error ? 9 : 1, src, (uint32_t)n, 0, 0);
        if (size == 0 || size > WBUF_MAX)
            return 0;
        {
            uint16_t wide[size];
            for (i = 0; i < size; i++)
                wide[i] = 0;
            got = (int32_t)MultiByteToWideChar_(cp, 1, src, (uint32_t)n, wide, (uint32_t)size);
            if (got == 0)
                return 0;
            return (int32_t)GetStringTypeW_(type, wide, (uint32_t)got, out);
        }
    }
    return 0;
}

/* 0x49ba3b: __crtLCMapStringA: LCMapStringW on the widened string, back to
 * `cp` (else the current code page), or LCMapStringA where only that
 * works. */
int32_t GAMEAPI crtc_lcmap_string(uint32_t lcid, uint32_t flags, const char *src, int32_t n, char *dst,
                                  int32_t dstn, uint32_t cp, int32_t error)
{
    int32_t insize, outsize;
    if (g_lcmap_f == 0) {
        if (LCMapStringW_(0, 0x100, 0x4a4df4, 1, 0, 0) != 0)
            g_lcmap_f = 1;
        else if (LCMapStringA_(0, 0x100, 0x4a4df0, 1, 0, 0) != 0)
            g_lcmap_f = 2;
        else
            return 0;
    }
    if (n > 0)
        n = strncnt(src, n);
    if (g_lcmap_f == 2)
        return (int32_t)LCMapStringA_(lcid, flags, src, (uint32_t)n, dst, (uint32_t)dstn);
    if (g_lcmap_f != 1)
        return 0;
    if (cp == 0)
        cp = g_lc_codepage;
    insize = (int32_t)MultiByteToWideChar_(cp, error ? 9 : 1, src, (uint32_t)n, 0, 0);
    if (insize == 0 || insize > WBUF_MAX)
        return 0;
    {
        uint16_t in[insize];
        if (MultiByteToWideChar_(cp, 1, src, (uint32_t)n, in, (uint32_t)insize) == 0)
            return 0;
        outsize = (int32_t)LCMapStringW_(lcid, flags, in, (uint32_t)insize, 0, 0);
        if (outsize == 0)
            return 0;
        if (flags & 0x400) { /* LCMAP_SORTKEY: bytes straight into dst */
            if (dstn == 0)
                return outsize;
            if (dstn < outsize)
                return 0;
            if (LCMapStringW_(lcid, flags, in, (uint32_t)insize, dst, (uint32_t)dstn) == 0)
                return 0;
            return outsize;
        }
        if (outsize > WBUF_MAX)
            return 0;
        {
            uint16_t out[outsize];
            int32_t r;
            if (LCMapStringW_(lcid, flags, in, (uint32_t)insize, out, (uint32_t)outsize) == 0)
                return 0;
            if (dstn == 0)
                dst = 0;
            r = (int32_t)WideCharToMultiByte_(cp, 0x220, out, (uint32_t)outsize, dst, (uint32_t)dstn, 0, 0);
            return r;
        }
    }
}

/* 0x49d106: CPtoLCID for the four far-east code pages. */
static uint32_t cp_to_lcid(uint32_t cp)
{
    switch (cp) {
    case 0x3a4:
        return 0x411;
    case 0x3a8:
        return 0x804;
    case 0x3b5:
        return 0x412;
    case 0x3b6:
        return 0x404;
    }
    return 0;
}

static void clear_mbctype(void)
{
    int32_t i;
    for (i = 0; i < 0x101; i++)
        g_mbctype[i] = 0;
}

/* 0x49d139: setSBCS: the "C" single-byte code page. */
static void set_sbcs(void)
{
    clear_mbctype();
    g_mbcodepage = 0;
    g_ismbcodepage = 0;
    g_mblcid = 0;
    G32(g_mbulinfo) = 0;
    G32(g_mbulinfo + 4) = 0;
    G32(g_mbulinfo + 8) = 0;
}

/* 0x49d162: setSBUpLow: the single-byte upper / lower flags and case map
 * of the code page (from the system, else ASCII). */
static void set_sb_up_low(void)
{
    uint8_t info[20];
    if (GetCPInfo_(g_mbcodepage, info) == 1) {
        uint8_t sb[0x100], lower[0x100], upper[0x100];
        uint16_t type[0x100];
        uint32_t c;
        const uint8_t *lead;
        /* the original leaves these uninitialised (read if a call below
         * fails): zeros here */
        for (c = 0; c < 0x100; c++) {
            sb[c] = (uint8_t)c;
            lower[c] = upper[c] = 0;
            type[c] = 0;
        }
        sb[0] = ' ';
        for (lead = info + 6; lead[0] != 0; lead += 2) {
            for (c = lead[0]; c <= lead[1]; c++)
                sb[c] = ' ';
        }
        crtc_get_string_type(1, (const char *)sb, 0x100, type, g_mbcodepage, g_mblcid, 0);
        crtc_lcmap_string(g_mblcid, 0x100, (const char *)sb, 0x100, (char *)lower, 0x100, g_mbcodepage, 0);
        crtc_lcmap_string(g_mblcid, 0x200, (const char *)sb, 0x100, (char *)upper, 0x100, g_mbcodepage, 0);
        for (c = 0; c < 0x100; c++) {
            if (type[c] & 1) {
                g_mbctype[c + 1] |= 0x10;
                g_mbcasemap[c] = lower[c];
            } else if (type[c] & 2) {
                g_mbctype[c + 1] |= 0x20;
                g_mbcasemap[c] = upper[c];
            } else {
                g_mbcasemap[c] = 0;
            }
        }
    } else {
        uint32_t c;
        for (c = 0; c < 0x100; c++) {
            if (c >= 'A' && c <= 'Z') {
                g_mbctype[c + 1] |= 0x10;
                g_mbcasemap[c] = (uint8_t)(c + 0x20);
            } else if (c >= 'a' && c <= 'z') {
                g_mbctype[c + 1] |= 0x20;
                g_mbcasemap[c] = (uint8_t)(c - 0x20);
            } else {
                g_mbcasemap[c] = 0;
            }
        }
    }
}

/* 0x49cf0f: _setmbcp: the multibyte tables for a code page (-2 OEM, -3
 * ANSI, -4 the locale's); the four far-east ones from the runtime's own
 * table (0x4b5458), the others from GetCPInfo. */
static int32_t setmbcp(int32_t codepage)
{
    uint32_t cp;
    int32_t r = 0;
    crtc_lock(0x19);
    if (codepage == -2) {
        g_sysfixed_cp = 1;
        cp = GetOEMCP_();
    } else if (codepage == -3) {
        g_sysfixed_cp = 1;
        cp = GetACP_();
    } else if (codepage == -4) {
        g_sysfixed_cp = 1;
        cp = g_lc_codepage;
    } else {
        g_sysfixed_cp = 0;
        cp = (uint32_t)codepage;
    }
    if (cp == g_mbcodepage)
        goto out;
    if (cp == 0) {
        set_sbcs();
        set_sb_up_low();
        goto out;
    }
    {
        uint32_t i, k;
        for (i = 0; i < 5; i++) {
            uint32_t entry = 0x4b5458 + 0x30 * i;
            if (G32(entry) != cp)
                continue;
            clear_mbctype();
            for (k = 0; k < 4; k++) {
                const uint8_t *range = (const uint8_t *)(uintptr_t)(entry + 0x10 + 8 * k);
                for (; range[0] != 0 && range[1] != 0; range += 2) {
                    uint32_t c;
                    for (c = range[0]; c <= range[1]; c++)
                        g_mbctype[c + 1] |= G8(0x4b5450 + k);
                }
            }
            g_ismbcodepage = 1;
            g_mbcodepage = cp;
            g_mblcid = cp_to_lcid(cp);
            G32(g_mbulinfo) = G32(entry + 4);
            G32(g_mbulinfo + 4) = G32(entry + 8);
            G32(g_mbulinfo + 8) = G32(entry + 0xc);
            set_sb_up_low();
            goto out;
        }
    }
    {
        uint8_t info[20];
        if (GetCPInfo_(cp, info) == 1) {
            g_mblcid = 0;
            clear_mbctype();
            if (U32(info, 0) < 2) {
                g_ismbcodepage = 0;
                g_mbcodepage = cp;
            } else {
                uint32_t c;
                g_mbcodepage = cp;
                if (info[6] != 0) {
                    const uint8_t *lead = info + 7;
                    for (;;) {
                        if (lead[0] == 0)
                            break;
                        for (c = lead[-1]; c <= lead[0]; c++)
                            g_mbctype[c + 1] |= 4;
                        if (lead[1] == 0)
                            break;
                        lead += 2;
                    }
                }
                for (c = 1; c < 0xff; c++)
                    g_mbctype[c + 1] |= 8;
                g_mblcid = cp_to_lcid(cp);
                g_ismbcodepage = 1;
            }
            G32(g_mbulinfo) = 0;
            G32(g_mbulinfo + 4) = 0;
            G32(g_mbulinfo + 8) = 0;
        } else {
            if (g_sysfixed_cp == 0) {
                r = -1;
                goto out;
            }
            set_sbcs();
        }
        set_sb_up_low();
    }
out:
    crtc_unlock(0x19);
    return r;
}

/* 0x49d2e7: __initmbctable (a .CRT$XI initialiser): the ANSI code page. */
void GAMEAPI crtc_initmbctable(void)
{
    if (g_mbctype_initialized == 0) {
        setmbcp(-3);
        g_mbctype_initialized = 1;
    }
}

/* ======================================================= locale helpers */

/* 0x4985af / 0x4986ea: _toupper_lk / _tolower_lk ("C" locale: ASCII; else
 * LCMapString). */
static int32_t case_lk(int32_t c, uint32_t map, int32_t ctype)
{
    uint8_t in[4], out[4] = {0, 0, 0, 0};
    int32_t size;
    const uint16_t *pctype = *(const uint16_t **)0x4b4b00;
    if (c < 0x100) {
        int32_t is = GI32(0x4b4d0c) < 2 ? (pctype[c] & ctype) : rt_isctype(c, ctype);
        if (is == 0)
            return c;
    }
    if (((const uint8_t *)pctype)[((uint32_t)c >> 8 & 0xff) * 2 + 1] & 0x80) {
        in[0] = (uint8_t)((uint32_t)c >> 8);
        in[1] = (uint8_t)c;
        in[2] = 0;
        in[3] = (uint8_t)((uint32_t)c >> 24);
        size = 2;
    } else {
        in[0] = (uint8_t)c;
        in[1] = 0;
        in[2] = (uint8_t)((uint32_t)c >> 16);
        in[3] = (uint8_t)((uint32_t)c >> 24);
        size = 1;
    }
    size = crtc_lcmap_string(g_lc_ctype, map, (const char *)in, size, (char *)out, 3, 0, 1);
    if (size == 0)
        return c;
    if (size == 1)
        return out[0];
    return out[0] | out[1] << 8;
}

int32_t GAMEAPI crtc_toupper_lk(int32_t c)
{
    if (g_lc_ctype == 0)
        return c > 0x60 && c < 0x7b ? c - 0x20 : c;
    return case_lk(c, 0x200, 2);
}

int32_t GAMEAPI crtc_tolower_lk(int32_t c)
{
    if (g_lc_ctype == 0)
        return c > 0x40 && c < 0x5b ? c + 0x20 : c;
    return case_lk(c, 0x100, 1);
}

/* 0x49cea2: _mbsdec: the character before `p` (0 at the start). */
uint8_t *GAMEAPI crtc_mbsdec(const uint8_t *start, const uint8_t *p)
{
    const uint8_t *q;
    if (start >= p)
        return 0;
    if (g_ismbcodepage == 0)
        return (uint8_t *)(p - 1);
    crtc_lock(0x19);
    q = p - 1;
    if (g_mbctype[p[-1] + 1] & 4) {
        crtc_unlock(0x19);
        return (uint8_t *)(p - 2);
    }
    do {
        q--;
    } while (q >= start && (g_mbctype[*q + 1] & 4));
    crtc_unlock(0x19);
    return (uint8_t *)(p - 1 - ((p - q) & 1));
}

/* 0x49d303: _mbsnbcpy: at most n bytes, not splitting a lead byte, padded
 * with zeros. */
uint8_t *GAMEAPI crtc_mbsnbcpy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint8_t *d = dst;
    uint32_t left = 0;
    if (g_ismbcodepage == 0)
        return (uint8_t *)rt_strncpy((char *)dst, (const char *)src, n);
    crtc_lock(0x19);
    if (n != 0) {
        for (;;) {
            uint8_t c = *src;
            uint8_t type = g_mbctype[c + 1];
            *d = c;
            left = n - 1;
            if (!(type & 4)) {
                d++;
                src++;
                if (c == 0)
                    break;
            } else {
                if (left == 0) {
                    *d = 0;
                    d++;
                    break;
                }
                c = src[1];
                left = n - 2;
                d[1] = c;
                d += 2;
                src += 2;
                if (c == 0) {
                    d[-2] = 0;
                    break;
                }
            }
            n = left;
            if (left == 0)
                break;
        }
    }
    while (left != 0) {
        *d++ = 0;
        left--;
    }
    crtc_unlock(0x19);
    return dst;
}

/* 0x49d4a0: _mbctoupper. */
uint32_t GAMEAPI crtc_mbctoupper(uint32_t c)
{
    uint8_t in[2], out[2];
    if (c < 0x100) {
        if ((g_mbctype[c + 1] & 0x20) == 0x20)
            return g_mbcasemap[c];
        return c;
    }
    in[0] = (uint8_t)(c >> 8);
    in[1] = (uint8_t)c;
    if ((g_mbctype[in[0] + 1] & 4) &&
        crtc_lcmap_string(g_mblcid, 0x200, (const char *)in, 2, (char *)out, 2, g_mbcodepage, 1) != 0)
        return (uint32_t)out[0] << 8 | out[1];
    return c;
}

/* ============================================================ exit */

/* 0x498d50 / 0x498d59: _lockexit / _unlockexit. */
void GAMEAPI crtc_lockexit(void)
{
    crtc_lock(0xd);
}

void GAMEAPI crtc_unlockexit(void)
{
    crtc_unlock(0xd);
}

/* 0x498d62: _initterm: call each non-null function pointer in [first,
 * last). */
void GAMEAPI crtc_initterm(uint32_t *first, uint32_t *last)
{
    for (; first < last; first++)
        if (*first != 0)
            ((vfn)(uintptr_t)*first)();
}

/* 0x498cab: doexit: the atexit table and the terminators unless `quick`,
 * then ExitProcess unless `retcaller`. */
static void doexit(int32_t code, int32_t quick, int32_t retcaller)
{
    crtc_lockexit();
    if (g_c_exit_done == 1)
        TerminateProcess_(GetCurrentProcess_(), (uint32_t)code);
    g_c_termination = 1;
    g_exitflag = (uint8_t)retcaller;
    if (quick == 0) {
        if (g_onexitbegin != 0) {
            uint32_t *p;
            for (p = (uint32_t *)(uintptr_t)g_onexitend - 1; A(p) >= g_onexitbegin; p--)
                if (*p != 0)
                    ((vfn)(uintptr_t)*p)();
        }
        crtc_initterm((uint32_t *)0x4a701c, (uint32_t *)0x4a7024); /* .CRT$XP: _endstdio */
    }
    crtc_initterm((uint32_t *)0x4a7028, (uint32_t *)0x4a702c); /* .CRT$XT: none */
    if (retcaller) {
        crtc_unlockexit();
        return;
    }
    g_c_exit_done = 1;
    ExitProcess_((uint32_t)code);
}

/* 0x498c9a: _exit. */
void GAMEAPI crtc_exit_quick(int32_t code)
{
    doexit(code, 1, 0);
}

/* 0x49ff7c: __crtMessageBoxA (user32 loaded on first use; owned by the
 * active window's last popup). */
static int32_t crt_message_box(const char *text, const char *caption, uint32_t type)
{
    uint32_t owner = 0;
    if (g_crt_message_box == 0) {
        uint32_t user32 = LoadLibraryA_(0x4a516c);
        if (user32 == 0)
            return 0;
        g_crt_message_box = GetProcAddress_(user32, 0x4a5160);
        if (g_crt_message_box == 0)
            return 0;
        g_crt_active_window = GetProcAddress_(user32, 0x4a5150);
        g_crt_last_popup = GetProcAddress_(user32, 0x4a513c);
    }
    if (g_crt_active_window != 0) {
        owner = ((w0_fn)(uintptr_t)g_crt_active_window)();
        if (owner != 0 && g_crt_last_popup != 0)
            owner = ((w1_fn)(uintptr_t)g_crt_last_popup)(owner);
    }
    return (int32_t)((w4_fn)(uintptr_t)g_crt_message_box)(owner, A(text), A(caption), type);
}

/* 0x49dee9: _NMSG_WRITE: a runtime error message to stderr (console) or a
 * message box ("Runtime Error! Program: ..."), texts at 0x4b5778. */
static void nmsg_write(uint32_t number)
{
    uint32_t i;
    for (i = 0; i < 18; i++)
        if (number == G32(g_rterrs + 8 * i))
            break;
    if (number != G32(g_rterrs + 8 * i))
        return;
    if (g_error_mode == 1 || (g_error_mode == 0 && g_app_type == 1)) {
        uint32_t written;
        const char *msg = (const char *)(uintptr_t)G32(g_rterrs + 8 * i + 4);
        uint32_t len = str_len(msg);
        WriteFile_(GetStdHandle_(0xfffffff4u), msg, len, &written, 0);
    } else if (number != 0xfc) {
        char progname[0x104], outmsg[0xa0], *name = progname;
        if (GetModuleFileNameA_(0, progname, 0x104) == 0)
            rt_strcpy(progname, (const char *)0x4a5124); /* "<program name unknown>" */
        if (str_len(progname) + 1 > 0x3c) {
            name = progname + str_len(progname) - 0x3b;
            rt_strncpy(name, (const char *)0x4a5120, 3); /* "..." */
        }
        rt_strcpy(outmsg, (const char *)0x4a5104); /* "Runtime Error!\n\nProgram: " */
        rt_strcat(outmsg, name);
        rt_strcat(outmsg, (const char *)0x4a5100); /* "\n\n" */
        rt_strcat(outmsg, (const char *)(uintptr_t)G32(g_rterrs + 8 * i + 4));
        crt_message_box(outmsg, (const char *)0x4a50d8, 0x12010);
    }
}

/* 0x49deb0: _FF_MSGBANNER. */
static void ff_msgbanner(void)
{
    if (g_error_mode == 1 || (g_error_mode == 0 && g_app_type == 1)) {
        nmsg_write(0xfc);
        if (G32(g_pnhheap) != 0)
            ((vfn)(uintptr_t)G32(g_pnhheap))();
        nmsg_write(0xff);
    }
}

/* 0x498fcd: _amsg_exit: report runtime error `n` and leave (_exit 255
 * through _aexit_rtn). */
void GAMEAPI crtc_amsg_exit(int32_t n)
{
    if (g_error_mode == 1)
        ff_msgbanner();
    nmsg_write((uint32_t)n);
    ((exit_fn)(uintptr_t)g_aexit_rtn)(0xff);
}

/* 0x49f925: _fptrap, the float-conversion table's entries before _fpmath
 * fills it: "floating point not loaded". */
void GAMEAPI crtc_fptrap(void)
{
    crtc_amsg_exit(2);
}

/* 0x498ff2: fast_error_exit (start-up failures). */
static void fast_error_exit(uint32_t n)
{
    if (g_error_mode == 1)
        ff_msgbanner();
    nmsg_write(n);
    ExitProcess_(0xff);
}

/* ========================================================== threads */

/* 0x498e86: _endthread. */
static void endthread(void)
{
    uint8_t *ptd;
    /* _FPmtterm (0x4b4ab4) is the runtime's empty function: not called */
    ptd = crtc_getptd();
    if (ptd == 0)
        crtc_amsg_exit(0x10);
    if (U32(ptd, 4) != 0xffffffffu)
        CloseHandle_(U32(ptd, 4));
    freeptd(ptd);
    ExitThread_(0);
}

/* 0x498df3: _threadstart, the thread procedure _beginthread passes to
 * CreateThread: the thread's data, then start(arg), then _endthread (no
 * SEH frame). */
uint32_t STDCALL crtc_threadstart(uint8_t *ptd)
{
    if (TlsSetValue_(g_tlsindex, ptd) == 0)
        crtc_amsg_exit(0x10);
    /* _FPmtinit (0x4b4ab0) is the runtime's empty function: not called */
    ((void(GAMEAPI *)(uint32_t))(uintptr_t)U32(ptd, 0x48))(U32(ptd, 0x4c));
    endthread();
    return 0;
}

/* ========================================================= start-up */

/* 0x49b0fc: _heap_init with __sbh_heap_init's region table (0x140 bytes;
 * the small-block heap itself is no longer used, see crt_cb.c). */
static int32_t heap_init(int32_t mt)
{
    g_crtheap = HeapCreate_(mt == 0, 0x1000, 0);
    if (g_crtheap == 0)
        return 0;
    G32(0x6d1264) = HeapAlloc_(g_crtheap, 0, 0x140);
    if (G32(0x6d1264) != 0) {
        G32(0x6d125c) = 0;
        G32(0x6d1260) = 0;
        G32(0x6d1258) = G32(0x6d1264);
        G32(0x6d1250) = 0x10;
        return 1;
    }
    HeapDestroy_(g_crtheap);
    return 0;
}

/* 0x49bc8a: _mtinit: locks, the TLS slot, the main thread's data. */
static int32_t mtinit(void)
{
    uint8_t *ptd;
    mtinitlocks();
    g_tlsindex = TlsAlloc_();
    if (g_tlsindex == 0xffffffffu)
        return 0;
    ptd = rt_calloc(1, 0x74);
    if (ptd == 0 || TlsSetValue_(g_tlsindex, ptd) == 0)
        return 0;
    crtc_initptd(ptd);
    U32(ptd, 0) = GetCurrentThreadId_();
    U32(ptd, 4) = 0xffffffffu;
    return 1;
}

/* 0x49dcf4: _ioinit: the first handle block (32), inherited handles from
 * the start-up info, the standard handles. */
static void ioinit(void)
{
    uint8_t si[0x44], *block;
    int32_t i;
    block = rt_malloc(0x480);
    if (block == 0)
        crtc_amsg_exit(0x1b);
    g_nhandle = 0x20;
    GPTR(g_pioinfo) = block;
    ioinfo_block_init(block);
    GetStartupInfoA_(si);
    if (U16(si, 0x32) != 0 && U32(si, 0x34) != 0) { /* cbReserved2, lpReserved2 */
        uint8_t *p = PTR(si, 0x34);
        int32_t n = I32(p, 0), count;
        uint8_t *flags = p + 4;
        uint32_t *handles = (uint32_t *)(p + 4 + n);
        if (n > 0x7ff)
            n = 0x800;
        count = n;
        if ((int32_t)g_nhandle < n) {
            uint32_t *slot = (uint32_t *)(uintptr_t)(g_pioinfo + 4);
            do {
                uint8_t *more = rt_malloc(0x480);
                if (more == 0) {
                    count = (int32_t)g_nhandle;
                    break;
                }
                g_nhandle += 0x20;
                *slot++ = A(more);
                ioinfo_block_init(more);
                count = n;
            } while ((int32_t)g_nhandle < n);
        }
        for (i = 0; i < count; i++, flags++, handles++) {
            if (*handles != 0xffffffffu && (*flags & FOPEN) && ((*flags & FPIPE) || GetFileType_(*handles) != 0)) {
                uint8_t *io = pioinfo(i);
                U32(io, 0) = *handles;
                U8(io, 4) = *flags;
            }
        }
    }
    for (i = 0; i < 3; i++) {
        uint8_t *io = GPTR(g_pioinfo) + i * 0x24;
        if (I32(io, 0) == -1) {
            uint32_t h, type;
            U8(io, 4) = 0x81; /* FOPEN | FTEXT */
            h = GetStdHandle_(i == 0 ? 0xfffffff6u : i == 1 ? 0xfffffff5u : 0xfffffff4u);
            if (h != 0xffffffffu && (type = GetFileType_(h)) != 0) {
                U32(io, 0) = h;
                if ((type & 0xff) == 2)
                    U8(io, 4) |= FDEV;
                else if ((type & 0xff) == 3)
                    U8(io, 4) |= FPIPE;
            } else {
                U8(io, 4) |= FDEV;
            }
        } else {
            U8(io, 4) |= FTEXT;
        }
    }
    SetHandleCount_(g_nhandle);
}

/* 0x49dbc2: __crtGetEnvironmentStringsA: a malloc'd copy of the
 * environment block (from the wide one where available). */
static uint8_t *get_environment_strings(void)
{
    uint16_t *wide = 0;
    uint8_t *narrow = 0;
    if (g_env_f == 0) {
        wide = (uint16_t *)(uintptr_t)GetEnvironmentStringsW_();
        if (wide != 0) {
            g_env_f = 1;
        } else {
            narrow = (uint8_t *)(uintptr_t)GetEnvironmentStrings_();
            if (narrow == 0)
                return 0;
            g_env_f = 2;
        }
    } else if (g_env_f != 1 && g_env_f != 2) {
        return 0;
    }
    if (g_env_f == 1) {
        uint16_t *p;
        int32_t n;
        uint32_t size;
        uint8_t *copy = 0;
        if (wide == 0 && (wide = (uint16_t *)(uintptr_t)GetEnvironmentStringsW_()) == 0)
            return 0;
        for (p = wide; *p != 0;) {
            while (*p != 0)
                p++;
            p++;
        }
        n = (int32_t)(p - wide) + 1;
        size = WideCharToMultiByte_(0, 0, wide, (uint32_t)n, 0, 0, 0, 0);
        if (size != 0 && (copy = rt_malloc(size)) != 0 &&
            WideCharToMultiByte_(0, 0, wide, (uint32_t)n, copy, size, 0, 0) == 0) {
            rt_free(copy);
            copy = 0;
        }
        FreeEnvironmentStringsW_(wide);
        return copy;
    }
    {
        uint8_t *p, *copy;
        uint32_t size;
        if (narrow == 0 && (narrow = (uint8_t *)(uintptr_t)GetEnvironmentStrings_()) == 0)
            return 0;
        for (p = narrow; *p != 0;) {
            while (*p != 0)
                p++;
            p++;
        }
        size = (uint32_t)(p + 1 - narrow);
        copy = rt_malloc(size);
        if (copy != 0)
            copy_bytes(copy, narrow, size);
        FreeEnvironmentStringsA_(narrow);
        return copy;
    }
}

/* 0x49da0e: parse_cmdline: argv and the argument text (or just their
 * counts), Windows quoting rules. */
static void parse_cmdline(const uint8_t *p, uint32_t *argv, uint8_t *args, int32_t *numargs, int32_t *numchars)
{
    int32_t inquote = 0;
    uint8_t c;
    *numchars = 0;
    *numargs = 1;
    if (argv != 0)
        *argv++ = A(args);
    if (*p == '"') {
        while (*++p != '"' && *p != 0) {
            if (ismbblead(*p)) {
                ++*numchars;
                if (args != 0)
                    *args++ = *p++;
            }
            ++*numchars;
            if (args != 0)
                *args++ = *p;
        }
        ++*numchars;
        if (args != 0)
            *args++ = 0;
        if (*p == '"')
            p++;
    } else {
        do {
            ++*numchars;
            if (args != 0)
                *args++ = *p;
            c = *p++;
            if (ismbblead(c)) {
                ++*numchars;
                if (args != 0)
                    *args++ = *p;
                p++;
            }
        } while (c != ' ' && c != 0 && c != '\t');
        if (c == 0)
            p--;
        else if (args != 0)
            args[-1] = 0;
    }
    for (;;) {
        if (*p != 0)
            while (*p == ' ' || *p == '\t')
                p++;
        if (*p == 0)
            break;
        if (argv != 0)
            *argv++ = A(args);
        ++*numargs;
        for (;;) {
            int32_t copychar = 1;
            uint32_t numslash = 0;
            while (*p == '\\') {
                p++;
                numslash++;
            }
            if (*p == '"') {
                if ((numslash & 1) == 0) {
                    if (inquote && p[1] == '"')
                        p++;
                    else
                        copychar = 0;
                    inquote = !inquote;
                }
                numslash >>= 1;
            }
            while (numslash--) {
                if (args != 0)
                    *args++ = '\\';
                ++*numchars;
            }
            if (*p == 0 || (!inquote && (*p == ' ' || *p == '\t')))
                break;
            if (copychar) {
                if (args != 0) {
                    if (ismbblead(*p)) {
                        *args++ = *p++;
                        ++*numchars;
                    }
                    *args++ = *p;
                } else if (ismbblead(*p)) {
                    p++;
                    ++*numchars;
                }
                ++*numchars;
            }
            p++;
        }
        if (args != 0)
            *args++ = 0;
        ++*numchars;
    }
    if (argv != 0)
        *argv = 0;
    ++*numargs;
}

/* 0x49d975: _setargv: __argc / __argv from the command line (the program
 * name from GetModuleFileNameA). */
static void setargv(void)
{
    int32_t numargs, numchars;
    const uint8_t *cmd;
    uint8_t *block;
    if (g_mbctype_initialized == 0)
        crtc_initmbctable();
    GetModuleFileNameA_(0, g_pgmname, 0x104);
    g_pgmptr = g_pgmname;
    cmd = *g_acmdln != 0 ? g_acmdln : (const uint8_t *)g_pgmname;
    parse_cmdline(cmd, 0, 0, &numargs, &numchars);
    block = rt_malloc((uint32_t)(numchars + numargs * 4));
    if (block == 0)
        crtc_amsg_exit(8);
    parse_cmdline(cmd, (uint32_t *)block, block + numargs * 4, &numargs, &numchars);
    g_argv = A(block);
    g_argc = (uint32_t)(numargs - 1);
}

/* 0x49d8bc: _setenvp: _environ from the environment block (entries
 * starting with '=' left out), the block freed. */
static void setenvp(void)
{
    const char *p;
    int32_t count = 0;
    uint32_t *env;
    if (g_mbctype_initialized == 0)
        crtc_initmbctable();
    for (p = (const char *)g_aenvptr; *p != 0; p += str_len(p) + 1)
        if (*p != '=')
            count++;
    env = rt_malloc((uint32_t)(count * 4 + 4));
    g_environ = A(env);
    if (env == 0)
        crtc_amsg_exit(9);
    for (p = (const char *)g_aenvptr; *p != 0;) {
        uint32_t len = str_len(p);
        if (*p != '=') {
            *env = A(rt_malloc(len + 1));
            if (*env == 0)
                crtc_amsg_exit(9);
            rt_strcpy((char *)(uintptr_t)*env, p);
            env++;
        }
        p += len + 1;
    }
    rt_free(g_aenvptr);
    g_aenvptr = 0;
    *env = 0;
    g_env_initialized = 1;
}

/* 0x49d864: _wincmdln: the command line after the program name. */
static uint8_t *wincmdln(void)
{
    uint8_t *p;
    if (g_mbctype_initialized == 0)
        crtc_initmbctable();
    p = g_acmdln;
    if (*p == '"') {
        while (p[1] != '"' && p[1] != 0) {
            p++;
            if (ismbblead(*p))
                p++;
        }
        p++;
        if (*p == '"')
            p++;
    } else {
        while (*p > ' ')
            p++;
    }
    while (*p != 0 && *p <= ' ')
        p++;
    return p;
}

/* 0x49ef5b / 0x49efed: the x87 control word to the runtime's abstract
 * one and back. */
static uint32_t abstract_cw(uint32_t cw)
{
    uint32_t a = 0;
    if (cw & 1)
        a = 0x10;
    if (cw & 4)
        a |= 8;
    if (cw & 8)
        a |= 4;
    if (cw & 0x10)
        a |= 2;
    if (cw & 0x20)
        a |= 1;
    if (cw & 2)
        a |= 0x80000;
    switch (cw & 0xc00) {
    case 0x400:
        a |= 0x100;
        break;
    case 0x800:
        a |= 0x200;
        break;
    case 0xc00:
        a |= 0x300;
        break;
    }
    if ((cw & 0x300) == 0)
        a |= 0x20000;
    else if ((cw & 0x300) == 0x200)
        a |= 0x10000;
    if (cw & 0x1000)
        a |= 0x40000;
    return a;
}

static uint32_t hw_cw(uint32_t a)
{
    uint32_t cw = (a & 0x10) != 0;
    if (a & 8)
        cw |= 4;
    if (a & 4)
        cw |= 8;
    if (a & 2)
        cw |= 0x10;
    if (a & 1)
        cw |= 0x20;
    if (a & 0x80000)
        cw |= 2;
    switch (a & 0x300) {
    case 0x100:
        cw |= 0x400;
        break;
    case 0x200:
        cw |= 0x800;
        break;
    case 0x300:
        cw |= 0xc00;
        break;
    }
    if ((a & 0x30000) == 0)
        cw |= 0x300;
    else if ((a & 0x30000) == 0x10000)
        cw |= 0x200;
    if (a & 0x40000)
        cw |= 0x1000;
    return cw;
}

/* 0x49ef45 / 0x49ef10: _controlfp / _control87. */
static uint32_t controlfp(uint32_t value, uint32_t mask)
{
    uint16_t cw, hw;
    uint32_t a;
    mask &= 0xfff7ffff;
    __asm__ volatile("fwait\n\tfnstcw %0" : "=m"(cw));
    a = (abstract_cw(cw) & ~mask) | (value & mask);
    hw = (uint16_t)hw_cw(a);
    __asm__ volatile("fldcw %0" : : "m"(hw));
    return a;
}

/* 0x4997b9: _ms_p5_test_fdiv: the Pentium FDIV bug, 4195835 / 3145727
 * (the original's x87 instructions: they leave the same status bits). */
static uint32_t p5_test_fdiv(void)
{
    uint32_t b[2], a[2], r[2];
    uint16_t sw;
    __asm__ volatile("fldl 0x4a4ce8\n\t"
                     "fstpl %[b]\n\t"
                     "fldl 0x4a4ce0\n\t"
                     "fstpl %[a]\n\t"
                     "fldl %[a]\n\t"
                     "fdivl %[b]\n\t"
                     "fmull %[b]\n\t"
                     "fsubrl %[a]\n\t"
                     "fstpl %[r]\n\t"
                     "fldl %[r]\n\t"
                     "fcompl 0x4a3410\n\t"
                     "fnstsw %[sw]"
                     : [b] "=m"(b), [a] "=m"(a), [r] "=m"(r), [sw] "=m"(sw)
                     :
                     : "st", "cc", "memory");
    return (sw & 0x4100) ? 0 : 1; /* above 1.0 */
}

/* 0x4979a0: _fpmath (_FPinit 0x4b4aac): the FDIV check (_adjust_fdiv:
 * IsProcessorFeaturePresent(0) when kernel32 has it), 53-bit precision,
 * exceptions cleared. Not done: _cfltcvt_init (0x4979b8), which points the
 * float-conversion table 0x4b4d90-0x4b4da4 at the runtime's _cfltcvt,
 * _cropzeros, _fassign... Only the runtime's own _output / _input called
 * through it, and group CB's sprintf has its own conversion, so the table
 * keeps its _fptrap entries (the original's code is not referenced). */
void GAMEAPI crtc_fpmath(void)
{
    uint32_t kernel32, present;
    kernel32 = GetModuleHandleA_(0x4a4d0c);
    if (kernel32 != 0 && (present = GetProcAddress_(kernel32, 0x4a4cf0)) != 0)
        g_adjust_fdiv = ((w1_fn)(uintptr_t)present)(0);
    else
        g_adjust_fdiv = p5_test_fdiv();
    controlfp(0x10000, 0x30000);
    __asm__ volatile("fnclex");
}

/* 0x499059: the clock's start time (a .CRT$XI initialiser). */
void GAMEAPI crtc_clock_init(void)
{
    uint32_t ft[2];
    GetSystemTimeAsFileTime_(ft);
    G32(g_clock_start) = ft[0];
    G32(g_clock_start + 4) = ft[1];
}

/* 0x498c5c: _cinit: _FPinit, then the .CRT$XI (0x4a7008: the clock, stdio
 * and multibyte initialisers) and .CRT$XC (0x4a7000: empty) tables; their
 * entries are the hooked runtime functions above. */
static void cinit(void)
{
    if (g_fpinit != 0)
        ((vfn)(uintptr_t)g_fpinit)();
    crtc_initterm((uint32_t *)0x4a7008, (uint32_t *)0x4a7018);
    crtc_initterm((uint32_t *)0x4a7000, (uint32_t *)0x4a7004);
}

/* 0x498ec5: the exe's entry point (WinMainCRTStartup): Windows version,
 * heap, threads, low-level I/O, command line and environment, the
 * initialisers, then WinMain (0x4351e0: hInstance from GetModuleHandleA(0),
 * no previous instance, the command line after the program name, the
 * start-up info's show command or SW_SHOWDEFAULT) and exit with its
 * result. No SEH frame: the runtime's exception filter is not installed. */
void GAMEAPI crtc_entry(void)
{
    uint8_t si[0x44];
    uint8_t *cmdline;
    uint32_t version = GetVersion_(), show;
    int32_t r;
    g_winminor = version >> 8 & 0xff;
    g_winmajor = version & 0xff;
    g_winver = (version & 0xff) * 0x100 + (version >> 8 & 0xff);
    g_osplatform = version >> 16;
    if (heap_init(1) == 0)
        fast_error_exit(0x1c);
    if (mtinit() == 0)
        fast_error_exit(0x10);
    ioinit();
    g_acmdln = (uint8_t *)(uintptr_t)GetCommandLineA_();
    g_aenvptr = get_environment_strings();
    setargv();
    setenvp();
    cinit();
    U32(si, 0x2c) = 0; /* dwFlags */
    GetStartupInfoA_(si);
    cmdline = wincmdln();
    show = (U8(si, 0x2c) & 1) ? U16(si, 0x30) : 10;
    r = game_winmain(GetModuleHandleA_(0), 0, cmdline, show);
    rt_exit(r);
}

/* ====================================================== x87 maths
 *
 * fmod 0x4987e0 and atan2 0x498820 (the game's sound placement calls them)
 * and the error machinery our C reaches by address: _startOneArgErrorHandling
 * 0x49a8e7 and its two-argument entry 0x49a8d0 (crt_cb.c's sin, cos, atan,
 * sqrt, pow), _powhlp 0x49ab37 (pow), _except2 0x4a0f1a and _handle_qnan2
 * 0x4a0eba (crt_ca.c's _hypot). Like crt_cb.c's maths these keep the
 * original's register and stack conventions and x87 state (control word,
 * exception flags, 80-bit results), so they are the runtime's code
 * instruction for instruction, generated from the disassembly
 * (labels .Lcc_<original address>):
 *
 *  - __ctrandisp2 0x49be7b with __fload 0x49c02e (sic: an infinity or NaN
 *    keeps its low mantissa dword unshifted), __trandisp2 0x49a5d7 (fxam
 *    classes of both arguments pick a handler from the function's table;
 *    the tables at 0x4b4d40 / 0x4b5410 are copied below as our handlers'
 *    addresses, the function's descriptor 0x4b4d30 / 0x4b5400 still
 *    supplies the name and op code), the handlers (0x4987f4 fmod's fprem
 *    loop, 0x49ce1c atan2's fpatan, 0x49a674-0x49a739, 0x49ce6c-0x49ce9d)
 *    and the exit 0x49bec3 (fast flag, error code, underflow / overflow
 *    scaling, _87except);
 *  - _87except 0x49fad5, _handle_exc 0x4a1272, _raise_exc 0x4a0fbf (the
 *    IEEE record and RaiseException for unmasked exceptions), _umatherr
 *    0x4a1489, _set_errno, _get_fname, _errcode, _matherr (returns 0),
 *    _statfp, _clrfp, _ctrlfp, _set_statfp, _decomp, _set_exp, _get_exp,
 *    _sptype, _fpclass, _frnd, _d_inttype.
 *
 * One change: with _adjust_fdiv set (Pentium FDIV bug, never on hardware
 * IsProcessorFeaturePresent(0) answers for) the fprem loop called the
 * runtime's P5-safe remainder 0x49ca7c; that gives fprem's result on any
 * other CPU, so it is fprem here. errno is crtc_errno. */
__asm__(".intel_syntax noprefix\n"
        ".text\n"
        ".globl crtc_fmod\n"
        ".type crtc_fmod,@function\n"
        "crtc_fmod: mov edx, 0x4b4d30\n"
        ".Lcc_4987e5: jmp .Lcc_49be7b\n"
        /* 0x4987f4 fmod_core */
        ".Lcc_4987f4: fxch st(1)\n"
        ".Lcc_4987f6: cmp dword ptr [0x4db290], 0x1\n"
        ".Lcc_4987fd: je .Lcc_498803\n"
        ".Lcc_4987ff: fprem\n"
        ".Lcc_498801: jmp .Lcc_498808\n"
        ".Lcc_498803: fprem\n"
        ".Lcc_498808: fwait\n"
        ".Lcc_498809: fnstsw ax\n"
        ".Lcc_49880b: fwait\n"
        ".Lcc_49880c: sahf\n"
        ".Lcc_49880d: jp .Lcc_4987f6\n"
        ".Lcc_49880f: fstp st(1)\n"
        ".Lcc_498811: ret\n"
        ".globl crtc_atan2\n"
        ".type crtc_atan2,@function\n"
        "crtc_atan2: mov edx, 0x4b5400\n"
        ".Lcc_498825: jmp .Lcc_49be7b\n"
        /* 0x49a5d7 trandisp2 */
        ".Lcc_49a5d7: cmp byte ptr [edx + 0xe], 0x5\n"
        ".Lcc_49a5db: jne .Lcc_49a5ee\n"
        ".Lcc_49a5dd: mov bx, word ptr [ebp - 0xa4]\n"
        ".Lcc_49a5e4: or bh, 0x2\n"
        ".Lcc_49a5e7: and bh, -0x2\n"
        ".Lcc_49a5ea: mov bl, 0x3f\n"
        ".Lcc_49a5ec: jmp .Lcc_49a5f2\n"
        ".Lcc_49a5ee: mov bx, 0x133f\n"
        ".Lcc_49a5f2: mov word ptr [ebp - 0xa2], bx\n"
        ".Lcc_49a5f9: fldcw word ptr [ebp - 0xa2]\n"
        ".Lcc_49a5ff: mov ebx, 0x4b4dcc\n"
        ".Lcc_49a604: fxam\n"
        ".Lcc_49a606: mov dword ptr [ebp - 0x94], edx\n"
        ".Lcc_49a60c: fwait\n"
        ".Lcc_49a60d: fnstsw word ptr [ebp - 0xa0]\n"
        ".Lcc_49a613: mov byte ptr [ebp - 0x90], 0x0\n"
        ".Lcc_49a61a: fxch st(1)\n"
        ".Lcc_49a61c: mov cl, byte ptr [ebp - 0x9f]\n"
        ".Lcc_49a622: fxam\n"
        ".Lcc_49a624: fwait\n"
        ".Lcc_49a625: fnstsw word ptr [ebp - 0xa0]\n"
        ".Lcc_49a62b: fxch st(1)\n"
        ".Lcc_49a62d: mov ch, byte ptr [ebp - 0x9f]\n"
        ".Lcc_49a633: shl ch, 1\n"
        ".Lcc_49a635: sar ch, 1\n"
        ".Lcc_49a637: rol ch, 1\n"
        ".Lcc_49a639: mov al, ch\n"
        ".Lcc_49a63b: and al, 0xf\n"
        ".Lcc_49a63d: xlatb\n"
        ".Lcc_49a63e: mov ah, al\n"
        ".Lcc_49a640: shl cl, 1\n"
        ".Lcc_49a642: sar cl, 1\n"
        ".Lcc_49a644: rol cl, 1\n"
        ".Lcc_49a646: mov al, cl\n"
        ".Lcc_49a648: and al, 0xf\n"
        ".Lcc_49a64a: xlatb\n"
        ".Lcc_49a64b: shl ah, 1\n"
        ".Lcc_49a64d: shl ah, 1\n"
        ".Lcc_49a64f: or al, ah\n"
        ".Lcc_49a651: movsx eax, al\n"
        ".Lcc_49a654: and ecx, 0x404\n"
        ".Lcc_49a65a: cmp edx, 0x4b4d30\n"
        "    je 1f\n"
        "    lea ebx, [eax + crtc_atan2_jump]\n"
        "    jmp dword ptr [ebx]\n"
        "1:  lea ebx, [eax + crtc_fmod_jump]\n"
        "    jmp dword ptr [ebx]\n"
        ".Lcc_49a663: call .Lcc_49a729\n"
        ".Lcc_49a668: fxch st(1)\n"
        ".Lcc_49a66a: fstp st(0)\n"
        ".Lcc_49a66c: ret\n"
        ".Lcc_49a66d: call .Lcc_49a729\n"
        ".Lcc_49a672: jmp .Lcc_49a66a\n"
        ".Lcc_49a674: fstp st(0)\n"
        ".Lcc_49a676: fstp st(0)\n"
        ".Lcc_49a678: fldz\n"
        ".Lcc_49a67a: ret\n"
        ".Lcc_49a67b: fstp st(0)\n"
        ".Lcc_49a67d: fstp st(0)\n"
        ".Lcc_49a67f: fld1\n"
        ".Lcc_49a681: ret\n"
        ".Lcc_49a682: fstp tbyte ptr [ebp - 0x9e]\n"
        ".Lcc_49a688: fld tbyte ptr [ebp - 0x9e]\n"
        ".Lcc_49a68e: test byte ptr [ebp - 0x97], 0x40\n"
        ".Lcc_49a695: je .Lcc_49a69f\n"
        ".Lcc_49a697: mov byte ptr [ebp - 0x90], 0x7\n"
        ".Lcc_49a69e: ret\n"
        ".Lcc_49a69f: mov byte ptr [ebp - 0x90], 0x1\n"
        ".Lcc_49a6a6: fadd qword ptr [0x4b4dc4]\n"
        ".Lcc_49a6ac: ret\n"
        ".Lcc_49a6ad: fxch st(1)\n"
        ".Lcc_49a6af: fstp tbyte ptr [ebp - 0x9e]\n"
        ".Lcc_49a6b5: fld tbyte ptr [ebp - 0x9e]\n"
        ".Lcc_49a6bb: test byte ptr [ebp - 0x97], 0x40\n"
        ".Lcc_49a6c2: je .Lcc_49a6cd\n"
        ".Lcc_49a6c4: mov byte ptr [ebp - 0x90], 0x7\n"
        ".Lcc_49a6cb: jmp .Lcc_49a6d4\n"
        ".Lcc_49a6cd: mov byte ptr [ebp - 0x90], 0x1\n"
        ".Lcc_49a6d4: faddp st(1), st\n"
        ".Lcc_49a6d6: ret\n"
        ".Lcc_49a6d7: fstp tbyte ptr [ebp - 0x9e]\n"
        ".Lcc_49a6dd: fld tbyte ptr [ebp - 0x9e]\n"
        ".Lcc_49a6e3: test byte ptr [ebp - 0x97], 0x40\n"
        ".Lcc_49a6ea: je .Lcc_49a70c\n"
        ".Lcc_49a6ec: fxch st(1)\n"
        ".Lcc_49a6ee: fstp tbyte ptr [ebp - 0x9e]\n"
        ".Lcc_49a6f4: fld tbyte ptr [ebp - 0x9e]\n"
        ".Lcc_49a6fa: test byte ptr [ebp - 0x97], 0x40\n"
        ".Lcc_49a701: je .Lcc_49a70c\n"
        ".Lcc_49a703: mov byte ptr [ebp - 0x90], 0x7\n"
        ".Lcc_49a70a: jmp .Lcc_49a713\n"
        ".Lcc_49a70c: mov byte ptr [ebp - 0x90], 0x1\n"
        ".Lcc_49a713: faddp st(1), st\n"
        ".Lcc_49a715: ret\n"
        ".Lcc_49a716: fstp st(0)\n"
        ".Lcc_49a718: fstp st(0)\n"
        ".Lcc_49a71a: fld tbyte ptr [0x4b4db0]\n"
        ".Lcc_49a720: cmp byte ptr [ebp - 0x90], 0x0\n"
        ".Lcc_49a727: jg .Lcc_49a730\n"
        ".Lcc_49a729: mov byte ptr [ebp - 0x90], 0x1\n"
        ".Lcc_49a730: or cl, cl\n"
        ".Lcc_49a732: ret\n"
        ".Lcc_49a733: or cl, cl\n"
        ".Lcc_49a735: je .Lcc_49a739\n"
        ".Lcc_49a737: fchs\n"
        ".Lcc_49a739: ret\n"
        ".globl crtc_two_arg_error\n"
        ".type crtc_two_arg_error,@function\n"
        "crtc_two_arg_error: push ebp\n"
        ".Lcc_49a8d1: mov ebp, esp\n"
        ".Lcc_49a8d3: add esp, -0x20\n"
        ".Lcc_49a8d6: mov dword ptr [ebp - 0x20], eax\n"
        ".Lcc_49a8d9: mov eax, dword ptr [ebp + 0x18]\n"
        ".Lcc_49a8dc: mov dword ptr [ebp - 0x10], eax\n"
        ".Lcc_49a8df: mov eax, dword ptr [ebp + 0x1c]\n"
        ".Lcc_49a8e2: mov dword ptr [ebp - 0xc], eax\n"
        ".Lcc_49a8e5: jmp .Lcc_49a8f0\n"
        ".globl crtc_one_arg_error\n"
        ".type crtc_one_arg_error,@function\n"
        "crtc_one_arg_error: push ebp\n"
        ".Lcc_49a8e8: mov ebp, esp\n"
        ".Lcc_49a8ea: add esp, -0x20\n"
        ".Lcc_49a8ed: mov dword ptr [ebp - 0x20], eax\n"
        ".Lcc_49a8f0: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_49a8f3: mov dword ptr [ebp - 0x1c], ecx\n"
        ".Lcc_49a8f6: mov eax, dword ptr [ebp + 0x10]\n"
        ".Lcc_49a8f9: mov ecx, dword ptr [ebp + 0x14]\n"
        ".Lcc_49a8fc: mov dword ptr [ebp - 0x18], eax\n"
        ".Lcc_49a8ff: mov dword ptr [ebp - 0x14], ecx\n"
        ".Lcc_49a902: lea eax, [ebp + 0x8]\n"
        ".Lcc_49a905: lea ecx, [ebp - 0x20]\n"
        ".Lcc_49a908: push eax\n"
        ".Lcc_49a909: push ecx\n"
        ".Lcc_49a90a: push edx\n"
        ".Lcc_49a90b: call .Lcc_49fad5\n"
        ".Lcc_49a910: add esp, 0xc\n"
        ".Lcc_49a913: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_49a916: cmp word ptr [ebp + 0x8], 0x27f\n"
        ".Lcc_49a91c: je .Lcc_49a921\n"
        ".Lcc_49a91e: fldcw word ptr [ebp + 0x8]\n"
        ".Lcc_49a921: leave\n"
        ".Lcc_49a922: ret\n"
        ".globl crtc_powhlp\n"
        ".type crtc_powhlp,@function\n"
        "crtc_powhlp: push ebp\n"
        ".Lcc_49ab38: mov ebp, esp\n"
        ".Lcc_49ab3a: push ecx\n"
        ".Lcc_49ab3b: push ecx\n"
        ".Lcc_49ab3c: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49ab3f: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_49ab45: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49ab48: push esi\n"
        ".Lcc_49ab49: xor edx, edx\n"
        ".Lcc_49ab4b: xor esi, esi\n"
        ".Lcc_49ab4d: fnstsw ax\n"
        ".Lcc_49ab4f: sahf\n"
        ".Lcc_49ab50: jae .Lcc_49ab54\n"
        ".Lcc_49ab52: fchs\n"
        ".Lcc_49ab54: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_49ab57: mov eax, 0x7ff00000\n"
        ".Lcc_49ab5c: mov ecx, 0xfff00000\n"
        ".Lcc_49ab61: cmp dword ptr [ebp + 0x14], eax\n"
        ".Lcc_49ab64: jne .Lcc_49ab9a\n"
        ".Lcc_49ab66: cmp dword ptr [ebp + 0x10], edx\n"
        ".Lcc_49ab69: jne .Lcc_49abc2\n"
        ".Lcc_49ab6b: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_49ab6e: fcomp qword ptr [0x4a3410]\n"
        ".Lcc_49ab74: fnstsw ax\n"
        ".Lcc_49ab76: sahf\n"
        ".Lcc_49ab77: ja .Lcc_49abde\n"
        ".Lcc_49ab79: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_49ab7c: fcomp qword ptr [0x4a3410]\n"
        ".Lcc_49ab82: fnstsw ax\n"
        ".Lcc_49ab84: sahf\n"
        ".Lcc_49ab85: jb .Lcc_49abf4\n"
        ".Lcc_49ab87: fld qword ptr [0x4b5848]\n"
        ".Lcc_49ab8d: mov eax, dword ptr [ebp + 0x18]\n"
        ".Lcc_49ab90: push 0x1\n"
        ".Lcc_49ab92: pop esi\n"
        ".Lcc_49ab93: fstp qword ptr [eax]\n"
        ".Lcc_49ab95: jmp .Lcc_49ac62\n"
        ".Lcc_49ab9a: cmp dword ptr [ebp + 0x14], ecx\n"
        ".Lcc_49ab9d: jne .Lcc_49abc2\n"
        ".Lcc_49ab9f: cmp dword ptr [ebp + 0x10], edx\n"
        ".Lcc_49aba2: jne .Lcc_49abc2\n"
        ".Lcc_49aba4: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_49aba7: fcomp qword ptr [0x4a3410]\n"
        ".Lcc_49abad: fnstsw ax\n"
        ".Lcc_49abaf: sahf\n"
        ".Lcc_49abb0: ja .Lcc_49abf4\n"
        ".Lcc_49abb2: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_49abb5: fcomp qword ptr [0x4a3410]\n"
        ".Lcc_49abbb: fnstsw ax\n"
        ".Lcc_49abbd: sahf\n"
        ".Lcc_49abbe: jae .Lcc_49ab87\n"
        ".Lcc_49abc0: jmp .Lcc_49abde\n"
        ".Lcc_49abc2: cmp dword ptr [ebp + 0xc], eax\n"
        ".Lcc_49abc5: jne .Lcc_49abf8\n"
        ".Lcc_49abc7: cmp dword ptr [ebp + 0x8], edx\n"
        ".Lcc_49abca: jne .Lcc_49ac62\n"
        ".Lcc_49abd0: fld qword ptr [ebp + 0x10]\n"
        ".Lcc_49abd3: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_49abd9: fnstsw ax\n"
        ".Lcc_49abdb: sahf\n"
        ".Lcc_49abdc: jbe .Lcc_49abe6\n"
        ".Lcc_49abde: fld qword ptr [0x4b5840]\n"
        ".Lcc_49abe4: jmp .Lcc_49ac5d\n"
        ".Lcc_49abe6: fld qword ptr [ebp + 0x10]\n"
        ".Lcc_49abe9: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_49abef: fnstsw ax\n"
        ".Lcc_49abf1: sahf\n"
        ".Lcc_49abf2: jae .Lcc_49ac5b\n"
        ".Lcc_49abf4: fldz\n"
        ".Lcc_49abf6: jmp .Lcc_49ac5d\n"
        ".Lcc_49abf8: cmp dword ptr [ebp + 0xc], ecx\n"
        ".Lcc_49abfb: jne .Lcc_49ac62\n"
        ".Lcc_49abfd: cmp dword ptr [ebp + 0x8], edx\n"
        ".Lcc_49ac00: jne .Lcc_49ac62\n"
        ".Lcc_49ac02: fld qword ptr [ebp + 0x10]\n"
        ".Lcc_49ac05: push ecx\n"
        ".Lcc_49ac06: push ecx\n"
        ".Lcc_49ac07: fstp qword ptr [esp]\n"
        ".Lcc_49ac0a: call .Lcc_49ac67\n"
        ".Lcc_49ac0f: fld qword ptr [ebp + 0x10]\n"
        ".Lcc_49ac12: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_49ac18: pop ecx\n"
        ".Lcc_49ac19: pop ecx\n"
        ".Lcc_49ac1a: mov ecx, eax\n"
        ".Lcc_49ac1c: fnstsw ax\n"
        ".Lcc_49ac1e: sahf\n"
        ".Lcc_49ac1f: jbe .Lcc_49ac36\n"
        ".Lcc_49ac21: fld qword ptr [0x4b5840]\n"
        ".Lcc_49ac27: cmp ecx, 0x1\n"
        ".Lcc_49ac2a: jne .Lcc_49ac2e\n"
        ".Lcc_49ac2c: fchs\n"
        ".Lcc_49ac2e: fstp qword ptr [ebp + 0x10]\n"
        ".Lcc_49ac31: fld qword ptr [ebp + 0x10]\n"
        ".Lcc_49ac34: jmp .Lcc_49ac5d\n"
        ".Lcc_49ac36: fld qword ptr [ebp + 0x10]\n"
        ".Lcc_49ac39: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_49ac3f: fnstsw ax\n"
        ".Lcc_49ac41: sahf\n"
        ".Lcc_49ac42: jae .Lcc_49ac5b\n"
        ".Lcc_49ac44: cmp ecx, 0x1\n"
        ".Lcc_49ac47: jne .Lcc_49ac51\n"
        ".Lcc_49ac49: fld qword ptr [0x4b5860]\n"
        ".Lcc_49ac4f: jmp .Lcc_49ac53\n"
        ".Lcc_49ac51: fldz\n"
        ".Lcc_49ac53: fstp qword ptr [ebp + 0x10]\n"
        ".Lcc_49ac56: fld qword ptr [ebp + 0x10]\n"
        ".Lcc_49ac59: jmp .Lcc_49ac5d\n"
        ".Lcc_49ac5b: fld1\n"
        ".Lcc_49ac5d: mov eax, dword ptr [ebp + 0x18]\n"
        ".Lcc_49ac60: fstp qword ptr [eax]\n"
        ".Lcc_49ac62: mov eax, esi\n"
        ".Lcc_49ac64: pop esi\n"
        ".Lcc_49ac65: leave\n"
        ".Lcc_49ac66: ret\n"
        /* 0x49ac67 d_inttype */
        ".Lcc_49ac67: push ebp\n"
        ".Lcc_49ac68: mov ebp, esp\n"
        ".Lcc_49ac6a: push ecx\n"
        ".Lcc_49ac6b: push ecx\n"
        ".Lcc_49ac6c: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49ac6f: push ecx\n"
        ".Lcc_49ac70: push ecx\n"
        ".Lcc_49ac71: fstp qword ptr [esp]\n"
        ".Lcc_49ac74: call .Lcc_49fd0f\n"
        ".Lcc_49ac79: pop ecx\n"
        ".Lcc_49ac7a: test al, -0x70\n"
        ".Lcc_49ac7c: pop ecx\n"
        ".Lcc_49ac7d: jne .Lcc_49acc8\n"
        ".Lcc_49ac7f: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49ac82: push ecx\n"
        ".Lcc_49ac83: push ecx\n"
        ".Lcc_49ac84: fstp qword ptr [esp]\n"
        ".Lcc_49ac87: call .Lcc_49fcfd\n"
        ".Lcc_49ac8c: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_49ac8f: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_49ac92: fcomp qword ptr [ebp + 0x8]\n"
        ".Lcc_49ac95: pop ecx\n"
        ".Lcc_49ac96: pop ecx\n"
        ".Lcc_49ac97: fnstsw ax\n"
        ".Lcc_49ac99: sahf\n"
        ".Lcc_49ac9a: jne .Lcc_49acc8\n"
        ".Lcc_49ac9c: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49ac9f: .byte 0xdc, 0x35, 0xe8, 0x4d, 0x4a, 0x00  /* fdiv qword ptr [0x4a4de8] */\n"
        ".Lcc_49aca5: push ecx\n"
        ".Lcc_49aca6: push ecx\n"
        ".Lcc_49aca7: fstp qword ptr [ebp + 0x8]\n"
        ".Lcc_49acaa: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49acad: fstp qword ptr [esp]\n"
        ".Lcc_49acb0: call .Lcc_49fcfd\n"
        ".Lcc_49acb5: fcomp qword ptr [ebp + 0x8]\n"
        ".Lcc_49acb8: pop ecx\n"
        ".Lcc_49acb9: pop ecx\n"
        ".Lcc_49acba: fnstsw ax\n"
        ".Lcc_49acbc: sahf\n"
        ".Lcc_49acbd: jne .Lcc_49acc3\n"
        ".Lcc_49acbf: push 0x2\n"
        ".Lcc_49acc1: jmp .Lcc_49acc5\n"
        ".Lcc_49acc3: push 0x1\n"
        ".Lcc_49acc5: pop eax\n"
        ".Lcc_49acc6: leave\n"
        ".Lcc_49acc7: ret\n"
        ".Lcc_49acc8: xor eax, eax\n"
        ".Lcc_49acca: leave\n"
        ".Lcc_49accb: ret\n"
        /* 0x49be7b ctrandisp2 */
        ".Lcc_49be7b: push ebp\n"
        ".Lcc_49be7c: mov ebp, esp\n"
        ".Lcc_49be7e: add esp, 0xfffffd30\n"
        ".Lcc_49be84: push ebx\n"
        ".Lcc_49be85: push dword ptr [ebp + 0xc]\n"
        ".Lcc_49be88: push dword ptr [ebp + 0x8]\n"
        ".Lcc_49be8b: call .Lcc_49c02e\n"
        ".Lcc_49be90: add esp, 0x8\n"
        ".Lcc_49be93: push dword ptr [ebp + 0x14]\n"
        ".Lcc_49be96: push dword ptr [ebp + 0x10]\n"
        ".Lcc_49be99: call .Lcc_49c02e\n"
        ".Lcc_49be9e: add esp, 0x8\n"
        ".Lcc_49bea1: fwait\n"
        ".Lcc_49bea2: fnstcw word ptr [ebp - 0xa4]\n"
        ".Lcc_49bea8: or byte ptr [ebp - 0x2c8], 0x2\n"
        ".Lcc_49beaf: mov byte ptr [ebp - 0x8f], 0x1\n"
        ".Lcc_49beb6: call .Lcc_49a5d7\n"
        ".Lcc_49bebb: call .Lcc_49bec3\n"
        ".Lcc_49bec0: pop ebx\n"
        ".Lcc_49bec1: leave\n"
        ".Lcc_49bec2: ret\n"
        /* 0x49bec3 ctrandisp2_exit */
        ".Lcc_49bec3: and byte ptr [ebp - 0x2c8], -0x2\n"
        ".Lcc_49beca: cmp dword ptr [0x4db28c], 0x0\n"
        ".Lcc_49bed1: jne .Lcc_49bf1f\n"
        ".Lcc_49bed3: fst qword ptr [ebp - 0x2d0]\n"
        ".Lcc_49bed9: mov al, byte ptr [ebp - 0x90]\n"
        ".Lcc_49bedf: or al, al\n"
        ".Lcc_49bee1: je .Lcc_49befd\n"
        ".Lcc_49bee3: cmp al, -0x1\n"
        ".Lcc_49bee5: je .Lcc_49bf27\n"
        ".Lcc_49bee7: cmp al, -0x2\n"
        ".Lcc_49bee9: je .Lcc_49bf27\n"
        ".Lcc_49beeb: or al, al\n"
        ".Lcc_49beed: je .Lcc_49bf1f\n"
        ".Lcc_49beef: movsx eax, al\n"
        ".Lcc_49bef2: mov dword ptr [ebp - 0x8e], eax\n"
        ".Lcc_49bef8: jmp .Lcc_49bf99\n"
        ".Lcc_49befd: mov ax, word ptr [ebp - 0xa4]\n"
        ".Lcc_49bf04: and ax, 0x20\n"
        ".Lcc_49bf08: jne .Lcc_49bf1f\n"
        ".Lcc_49bf0a: fwait\n"
        ".Lcc_49bf0b: fnstsw ax\n"
        ".Lcc_49bf0d: and ax, 0x20\n"
        ".Lcc_49bf11: je .Lcc_49bf1f\n"
        ".Lcc_49bf13: mov dword ptr [ebp - 0x8e], 0x8\n"
        ".Lcc_49bf1d: jmp .Lcc_49bf99\n"
        ".Lcc_49bf1f: fldcw word ptr [ebp - 0xa4]\n"
        ".Lcc_49bf25: fwait\n"
        ".Lcc_49bf26: ret\n"
        ".Lcc_49bf27: mov ax, word ptr [ebp - 0x2ca]\n"
        ".Lcc_49bf2e: and ax, 0x7ff0\n"
        ".Lcc_49bf32: or ax, ax\n"
        ".Lcc_49bf35: je .Lcc_49bf3f\n"
        ".Lcc_49bf37: cmp ax, 0x7ff0\n"
        ".Lcc_49bf3b: je .Lcc_49bf6d\n"
        ".Lcc_49bf3d: jmp .Lcc_49befd\n"
        ".Lcc_49bf3f: mov dword ptr [ebp - 0x8e], 0x4\n"
        ".Lcc_49bf49: fld qword ptr [0x4a4e38]\n"
        ".Lcc_49bf4f: fxch st(1)\n"
        ".Lcc_49bf51: fscale\n"
        ".Lcc_49bf53: fstp st(1)\n"
        ".Lcc_49bf55: fld st(0)\n"
        ".Lcc_49bf57: fabs\n"
        ".Lcc_49bf59: fcomp qword ptr [0x4a4e28]\n"
        ".Lcc_49bf5f: fwait\n"
        ".Lcc_49bf60: fnstsw ax\n"
        ".Lcc_49bf62: sahf\n"
        ".Lcc_49bf63: jae .Lcc_49bf99\n"
        ".Lcc_49bf65: fmul qword ptr [0x4a4e48]\n"
        ".Lcc_49bf6b: jmp .Lcc_49bf99\n"
        ".Lcc_49bf6d: mov dword ptr [ebp - 0x8e], 0x3\n"
        ".Lcc_49bf77: fld qword ptr [0x4a4e30]\n"
        ".Lcc_49bf7d: fxch st(1)\n"
        ".Lcc_49bf7f: fscale\n"
        ".Lcc_49bf81: fstp st(1)\n"
        ".Lcc_49bf83: fld st(0)\n"
        ".Lcc_49bf85: fabs\n"
        ".Lcc_49bf87: fcomp qword ptr [0x4a4e20]\n"
        ".Lcc_49bf8d: fwait\n"
        ".Lcc_49bf8e: fnstsw ax\n"
        ".Lcc_49bf90: sahf\n"
        ".Lcc_49bf91: jbe .Lcc_49bf99\n"
        ".Lcc_49bf93: fmul qword ptr [0x4a4e40]\n"
        ".Lcc_49bf99: push esi\n"
        ".Lcc_49bf9a: push edi\n"
        ".Lcc_49bf9b: mov ebx, dword ptr [ebp - 0x94]\n"
        ".Lcc_49bfa1: inc ebx\n"
        ".Lcc_49bfa2: mov dword ptr [ebp - 0x8a], ebx\n"
        ".Lcc_49bfa8: test byte ptr [ebp - 0x2c8], 0x1\n"
        ".Lcc_49bfaf: jne .Lcc_49bfcb\n"
        ".Lcc_49bfb1: cld\n"
        ".Lcc_49bfb2: lea esi, [ebp + 0x8]\n"
        ".Lcc_49bfb5: lea edi, [ebp - 0x86]\n"
        ".Lcc_49bfbb: .byte 0xa5  /* movsd */\n"
        ".Lcc_49bfbc: .byte 0xa5  /* movsd */\n"
        ".Lcc_49bfbd: cmp byte ptr [ebx + 0xc], 0x1\n"
        ".Lcc_49bfc1: je .Lcc_49bfcb\n"
        ".Lcc_49bfc3: lea esi, [ebp + 0x10]\n"
        ".Lcc_49bfc6: lea edi, [ebp - 0x7e]\n"
        ".Lcc_49bfc9: .byte 0xa5  /* movsd */\n"
        ".Lcc_49bfca: .byte 0xa5  /* movsd */\n"
        ".Lcc_49bfcb: fstp qword ptr [ebp - 0x76]\n"
        ".Lcc_49bfce: lea eax, [ebp - 0x8e]\n"
        ".Lcc_49bfd4: lea ebx, [ebp - 0xa4]\n"
        ".Lcc_49bfda: push ebx\n"
        ".Lcc_49bfdb: push eax\n"
        ".Lcc_49bfdc: mov ebx, dword ptr [ebp - 0x94]\n"
        ".Lcc_49bfe2: mov al, byte ptr [ebx + 0xe]\n"
        ".Lcc_49bfe5: movsx eax, al\n"
        ".Lcc_49bfe8: push eax\n"
        ".Lcc_49bfe9: call .Lcc_49fad5\n"
        ".Lcc_49bfee: add esp, 0xc\n"
        ".Lcc_49bff1: pop edi\n"
        ".Lcc_49bff2: pop esi\n"
        ".Lcc_49bff3: fld qword ptr [ebp - 0x76]\n"
        ".Lcc_49bff6: jmp .Lcc_49bf1f\n"
        /* 0x49c02e fload */
        ".Lcc_49c02e: push ebp\n"
        ".Lcc_49c02f: mov ebp, esp\n"
        ".Lcc_49c031: add esp, -0xc\n"
        ".Lcc_49c034: push ebx\n"
        ".Lcc_49c035: mov ax, word ptr [ebp + 0xe]\n"
        ".Lcc_49c039: mov bx, ax\n"
        ".Lcc_49c03c: and ax, 0x7ff0\n"
        ".Lcc_49c040: cmp ax, 0x7ff0\n"
        ".Lcc_49c044: jne .Lcc_49c064\n"
        ".Lcc_49c046: or bx, 0x7fff\n"
        ".Lcc_49c04b: mov word ptr [ebp - 0x2], bx\n"
        ".Lcc_49c04f: mov eax, dword ptr [ebp + 0xc]\n"
        ".Lcc_49c052: mov ebx, dword ptr [ebp + 0x8]\n"
        ".Lcc_49c055: shld eax, ebx, 0xb\n"
        ".Lcc_49c059: mov dword ptr [ebp - 0x6], eax\n"
        ".Lcc_49c05c: mov dword ptr [ebp - 0xa], ebx\n"
        ".Lcc_49c05f: fld tbyte ptr [ebp - 0xa]\n"
        ".Lcc_49c062: jmp .Lcc_49c067\n"
        ".Lcc_49c064: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49c067: pop ebx\n"
        ".Lcc_49c068: leave\n"
        ".Lcc_49c069: ret\n"
        /* 0x49ce1c atan2_core */
        ".Lcc_49ce1c: mov byte ptr [ebp - 0x90], -0x2\n"
        ".Lcc_49ce23: fabs\n"
        ".Lcc_49ce25: fxch st(1)\n"
        ".Lcc_49ce27: fabs\n"
        ".Lcc_49ce29: fxch st(1)\n"
        ".Lcc_49ce2b: fpatan\n"
        ".Lcc_49ce2d: or cl, cl\n"
        ".Lcc_49ce2f: je .Lcc_49ce35\n"
        ".Lcc_49ce31: fldpi\n"
        ".Lcc_49ce33: .byte 0xde, 0xe1  /* fsubrp st(1), st */\n"
        ".Lcc_49ce35: or ch, ch\n"
        ".Lcc_49ce37: je .Lcc_49ce3b\n"
        ".Lcc_49ce39: fchs\n"
        ".Lcc_49ce3b: ret\n"
        ".Lcc_49ce3c: fabs\n"
        ".Lcc_49ce3e: fld st(0)\n"
        ".Lcc_49ce40: fld st(0)\n"
        ".Lcc_49ce42: fld1\n"
        ".Lcc_49ce44: .byte 0xde, 0xe1  /* fsubrp st(1), st */\n"
        ".Lcc_49ce46: fxch st(1)\n"
        ".Lcc_49ce48: fld1\n"
        ".Lcc_49ce4a: faddp st(1), st\n"
        ".Lcc_49ce4c: fmulp st(1), st\n"
        ".Lcc_49ce4e: ftst\n"
        ".Lcc_49ce50: fwait\n"
        ".Lcc_49ce51: fnstsw word ptr [ebp - 0xa0]\n"
        ".Lcc_49ce57: fwait\n"
        ".Lcc_49ce58: test byte ptr [ebp - 0x9f], 0x1\n"
        ".Lcc_49ce5f: jne .Lcc_49ce66\n"
        ".Lcc_49ce61: xor ch, ch\n"
        ".Lcc_49ce63: fsqrt\n"
        ".Lcc_49ce65: ret\n"
        ".Lcc_49ce66: pop eax\n"
        ".Lcc_49ce67: jmp .Lcc_49a716\n"
        ".Lcc_49ce6c: fstp st(0)\n"
        ".Lcc_49ce6e: fld tbyte ptr [0x4b4dba]\n"
        ".Lcc_49ce74: ret\n"
        ".Lcc_49ce75: fstp st(0)\n"
        ".Lcc_49ce77: or cl, cl\n"
        ".Lcc_49ce79: je .Lcc_49ce86\n"
        ".Lcc_49ce7b: fstp st(0)\n"
        ".Lcc_49ce7d: fldpi\n"
        ".Lcc_49ce7f: or ch, ch\n"
        ".Lcc_49ce81: je .Lcc_49ce85\n"
        ".Lcc_49ce83: fchs\n"
        ".Lcc_49ce85: ret\n"
        ".Lcc_49ce86: jmp .Lcc_49a676\n"
        ".Lcc_49ce8b: fstp st(0)\n"
        ".Lcc_49ce8d: jmp .Lcc_49a718\n"
        ".Lcc_49ce92: fstp st(0)\n"
        ".Lcc_49ce94: mov cl, ch\n"
        ".Lcc_49ce96: jmp .Lcc_49ce98\n"
        ".Lcc_49ce98: call .Lcc_49ce6c\n"
        ".Lcc_49ce9d: jmp .Lcc_49a733\n"
        /* 0x49fad5 87except */
        ".Lcc_49fad5: push ebp\n"
        ".Lcc_49fad6: mov ebp, esp\n"
        ".Lcc_49fad8: sub esp, 0x58\n"
        ".Lcc_49fadb: mov eax, dword ptr [ebp + 0x10]\n"
        ".Lcc_49fade: push ebx\n"
        ".Lcc_49fadf: push esi\n"
        ".Lcc_49fae0: mov esi, dword ptr [ebp + 0xc]\n"
        ".Lcc_49fae3: movzx ecx, word ptr [eax]\n"
        ".Lcc_49fae6: mov eax, dword ptr [esi]\n"
        ".Lcc_49fae8: push edi\n"
        ".Lcc_49fae9: dec eax\n"
        ".Lcc_49faea: mov dword ptr [ebp + 0x10], ecx\n"
        ".Lcc_49faed: je .Lcc_49fb1a\n"
        ".Lcc_49faef: dec eax\n"
        ".Lcc_49faf0: je .Lcc_49fb16\n"
        ".Lcc_49faf2: dec eax\n"
        ".Lcc_49faf3: je .Lcc_49fb12\n"
        ".Lcc_49faf5: dec eax\n"
        ".Lcc_49faf6: je .Lcc_49fb0e\n"
        ".Lcc_49faf8: dec eax\n"
        ".Lcc_49faf9: je .Lcc_49fb1a\n"
        ".Lcc_49fafb: dec eax\n"
        ".Lcc_49fafc: dec eax\n"
        ".Lcc_49fafd: je .Lcc_49fb06\n"
        ".Lcc_49faff: dec eax\n"
        ".Lcc_49fb00: jne .Lcc_49fb70\n"
        ".Lcc_49fb02: push 0x10\n"
        ".Lcc_49fb04: jmp .Lcc_49fb1c\n"
        ".Lcc_49fb06: mov dword ptr [esi], 0x1\n"
        ".Lcc_49fb0c: jmp .Lcc_49fb70\n"
        ".Lcc_49fb0e: push 0x12\n"
        ".Lcc_49fb10: jmp .Lcc_49fb1c\n"
        ".Lcc_49fb12: push 0x11\n"
        ".Lcc_49fb14: jmp .Lcc_49fb1c\n"
        ".Lcc_49fb16: push 0x4\n"
        ".Lcc_49fb18: jmp .Lcc_49fb1c\n"
        ".Lcc_49fb1a: push 0x8\n"
        ".Lcc_49fb1c: pop ebx\n"
        ".Lcc_49fb1d: lea edi, [esi + 0x18]\n"
        ".Lcc_49fb20: push ecx\n"
        ".Lcc_49fb21: push edi\n"
        ".Lcc_49fb22: push ebx\n"
        ".Lcc_49fb23: call .Lcc_4a1272\n"
        ".Lcc_49fb28: add esp, 0xc\n"
        ".Lcc_49fb2b: test eax, eax\n"
        ".Lcc_49fb2d: jne .Lcc_49fb70\n"
        ".Lcc_49fb2f: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_49fb32: cmp eax, 0x10\n"
        ".Lcc_49fb35: je .Lcc_49fb47\n"
        ".Lcc_49fb37: cmp eax, 0x16\n"
        ".Lcc_49fb3a: je .Lcc_49fb47\n"
        ".Lcc_49fb3c: cmp eax, 0x1d\n"
        ".Lcc_49fb3f: je .Lcc_49fb47\n"
        ".Lcc_49fb41: and dword ptr [ebp - 0x20], -0x2\n"
        ".Lcc_49fb45: jmp .Lcc_49fb59\n"
        ".Lcc_49fb47: mov ecx, dword ptr [ebp - 0x20]\n"
        ".Lcc_49fb4a: fld qword ptr [esi + 0x10]\n"
        ".Lcc_49fb4d: and ecx, -0x1d\n"
        ".Lcc_49fb50: fstp qword ptr [ebp - 0x30]\n"
        ".Lcc_49fb53: or ecx, 0x3\n"
        ".Lcc_49fb56: mov dword ptr [ebp - 0x20], ecx\n"
        ".Lcc_49fb59: lea ecx, [esi + 0x8]\n"
        ".Lcc_49fb5c: push edi\n"
        ".Lcc_49fb5d: push ecx\n"
        ".Lcc_49fb5e: push eax\n"
        ".Lcc_49fb5f: lea eax, [ebp + 0x10]\n"
        ".Lcc_49fb62: push ebx\n"
        ".Lcc_49fb63: push eax\n"
        ".Lcc_49fb64: lea eax, [ebp - 0x58]\n"
        ".Lcc_49fb67: push eax\n"
        ".Lcc_49fb68: call .Lcc_4a0fbf\n"
        ".Lcc_49fb6d: add esp, 0x18\n"
        ".Lcc_49fb70: push 0xffff\n"
        ".Lcc_49fb75: push dword ptr [ebp + 0x10]\n"
        ".Lcc_49fb78: call .Lcc_4a15ab\n"
        ".Lcc_49fb7d: cmp dword ptr [esi], 0x8\n"
        ".Lcc_49fb80: pop ecx\n"
        ".Lcc_49fb81: pop ecx\n"
        ".Lcc_49fb82: je .Lcc_49fb98\n"
        ".Lcc_49fb84: cmp dword ptr [0x4b5948], 0x0\n"
        ".Lcc_49fb8b: jne .Lcc_49fb98\n"
        ".Lcc_49fb8d: push esi\n"
        ".Lcc_49fb8e: call .Lcc_4a158b\n"
        ".Lcc_49fb93: test eax, eax\n"
        ".Lcc_49fb95: pop ecx\n"
        ".Lcc_49fb96: jne .Lcc_49fba0\n"
        ".Lcc_49fb98: push dword ptr [esi]\n"
        ".Lcc_49fb9a: call .Lcc_4a1511\n"
        ".Lcc_49fb9f: pop ecx\n"
        ".Lcc_49fba0: pop edi\n"
        ".Lcc_49fba1: pop esi\n"
        ".Lcc_49fba2: pop ebx\n"
        ".Lcc_49fba3: leave\n"
        ".Lcc_49fba4: ret\n"
        /* 0x49fba5 set_exp */
        ".Lcc_49fba5: push ebp\n"
        ".Lcc_49fba6: mov ebp, esp\n"
        ".Lcc_49fba8: push ecx\n"
        ".Lcc_49fba9: push ecx\n"
        ".Lcc_49fbaa: mov eax, dword ptr [ebp + 0x10]\n"
        ".Lcc_49fbad: mov ecx, dword ptr [ebp + 0xe]\n"
        ".Lcc_49fbb0: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49fbb3: add eax, 0x3fe\n"
        ".Lcc_49fbb8: and cx, 0x800f\n"
        ".Lcc_49fbbd: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_49fbc0: shl eax, 0x4\n"
        ".Lcc_49fbc3: or eax, ecx\n"
        ".Lcc_49fbc5: mov word ptr [ebp - 0x2], ax\n"
        ".Lcc_49fbc9: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_49fbcc: leave\n"
        ".Lcc_49fbcd: ret\n"
        /* 0x49fbce get_exp */
        ".Lcc_49fbce: mov eax, dword ptr [esp + 0xa]\n"
        ".Lcc_49fbd2: shr eax, 0x4\n"
        ".Lcc_49fbd5: and ax, 0x7ff\n"
        ".Lcc_49fbd9: sub eax, 0x3fe\n"
        ".Lcc_49fbde: movsx eax, ax\n"
        ".Lcc_49fbe1: ret\n"
        /* 0x49fbe2 sptype */
        ".Lcc_49fbe2: push ebp\n"
        ".Lcc_49fbe3: mov ebp, esp\n"
        ".Lcc_49fbe5: xor edx, edx\n"
        ".Lcc_49fbe7: cmp dword ptr [ebp + 0xc], 0x7ff00000\n"
        ".Lcc_49fbee: jne .Lcc_49fbf9\n"
        ".Lcc_49fbf0: cmp dword ptr [ebp + 0x8], edx\n"
        ".Lcc_49fbf3: jne .Lcc_49fc0b\n"
        ".Lcc_49fbf5: push 0x1\n"
        ".Lcc_49fbf7: jmp .Lcc_49fc35\n"
        ".Lcc_49fbf9: cmp dword ptr [ebp + 0xc], 0xfff00000\n"
        ".Lcc_49fc00: jne .Lcc_49fc0b\n"
        ".Lcc_49fc02: cmp dword ptr [ebp + 0x8], edx\n"
        ".Lcc_49fc05: jne .Lcc_49fc0b\n"
        ".Lcc_49fc07: push 0x2\n"
        ".Lcc_49fc09: jmp .Lcc_49fc35\n"
        ".Lcc_49fc0b: mov ecx, dword ptr [ebp + 0xe]\n"
        ".Lcc_49fc0e: mov eax, 0x7ff8\n"
        ".Lcc_49fc13: and ecx, eax\n"
        ".Lcc_49fc15: cmp cx, ax\n"
        ".Lcc_49fc18: jne .Lcc_49fc1e\n"
        ".Lcc_49fc1a: push 0x3\n"
        ".Lcc_49fc1c: jmp .Lcc_49fc35\n"
        ".Lcc_49fc1e: cmp cx, 0x7ff0\n"
        ".Lcc_49fc23: jne .Lcc_49fc38\n"
        ".Lcc_49fc25: test dword ptr [ebp + 0xc], 0x7ffff\n"
        ".Lcc_49fc2c: jne .Lcc_49fc33\n"
        ".Lcc_49fc2e: cmp dword ptr [ebp + 0x8], edx\n"
        ".Lcc_49fc31: je .Lcc_49fc38\n"
        ".Lcc_49fc33: push 0x4\n"
        ".Lcc_49fc35: pop eax\n"
        ".Lcc_49fc36: pop ebp\n"
        ".Lcc_49fc37: ret\n"
        ".Lcc_49fc38: xor eax, eax\n"
        ".Lcc_49fc3a: pop ebp\n"
        ".Lcc_49fc3b: ret\n"
        /* 0x49fc3c decomp */
        ".Lcc_49fc3c: push ebp\n"
        ".Lcc_49fc3d: mov ebp, esp\n"
        ".Lcc_49fc3f: push ecx\n"
        ".Lcc_49fc40: push ecx\n"
        ".Lcc_49fc41: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49fc44: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_49fc4a: push esi\n"
        ".Lcc_49fc4b: fnstsw ax\n"
        ".Lcc_49fc4d: sahf\n"
        ".Lcc_49fc4e: jne .Lcc_49fc5c\n"
        ".Lcc_49fc50: fldz\n"
        ".Lcc_49fc52: xor esi, esi\n"
        ".Lcc_49fc54: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_49fc57: jmp .Lcc_49fcf2\n"
        ".Lcc_49fc5c: xor ecx, ecx\n"
        ".Lcc_49fc5e: test word ptr [ebp + 0xe], 0x7ff0\n"
        ".Lcc_49fc64: jne .Lcc_49fccb\n"
        ".Lcc_49fc66: test dword ptr [ebp + 0xc], 0xfffff\n"
        ".Lcc_49fc6d: jne .Lcc_49fc74\n"
        ".Lcc_49fc6f: cmp dword ptr [ebp + 0x8], ecx\n"
        ".Lcc_49fc72: je .Lcc_49fccb\n"
        ".Lcc_49fc74: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49fc77: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_49fc7d: mov esi, 0xfffffc03\n"
        ".Lcc_49fc82: fnstsw ax\n"
        ".Lcc_49fc84: sahf\n"
        ".Lcc_49fc85: jae .Lcc_49fc8c\n"
        ".Lcc_49fc87: push 0x1\n"
        ".Lcc_49fc89: pop eax\n"
        ".Lcc_49fc8a: jmp .Lcc_49fc8e\n"
        ".Lcc_49fc8c: xor eax, eax\n"
        ".Lcc_49fc8e: test byte ptr [ebp + 0xe], 0x10\n"
        ".Lcc_49fc92: jne .Lcc_49fca7\n"
        ".Lcc_49fc94: shl dword ptr [ebp + 0xc], 1\n"
        ".Lcc_49fc97: test byte ptr [ebp + 0xb], -0x80\n"
        ".Lcc_49fc9b: je .Lcc_49fca1\n"
        ".Lcc_49fc9d: or dword ptr [ebp + 0xc], 0x1\n"
        ".Lcc_49fca1: shl dword ptr [ebp + 0x8], 1\n"
        ".Lcc_49fca4: dec esi\n"
        ".Lcc_49fca5: jmp .Lcc_49fc8e\n"
        ".Lcc_49fca7: and word ptr [ebp + 0xe], 0xffef\n"
        ".Lcc_49fcad: cmp eax, ecx\n"
        ".Lcc_49fcaf: je .Lcc_49fcb5\n"
        ".Lcc_49fcb1: or byte ptr [ebp + 0xf], -0x80\n"
        ".Lcc_49fcb5: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49fcb8: push ecx\n"
        ".Lcc_49fcb9: push ecx\n"
        ".Lcc_49fcba: push ecx\n"
        ".Lcc_49fcbb: fstp qword ptr [esp]\n"
        ".Lcc_49fcbe: call .Lcc_49fba5\n"
        ".Lcc_49fcc3: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_49fcc6: add esp, 0xc\n"
        ".Lcc_49fcc9: jmp .Lcc_49fcf2\n"
        ".Lcc_49fccb: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49fcce: push ecx\n"
        ".Lcc_49fccf: push ecx\n"
        ".Lcc_49fcd0: push ecx\n"
        ".Lcc_49fcd1: fstp qword ptr [esp]\n"
        ".Lcc_49fcd4: call .Lcc_49fba5\n"
        ".Lcc_49fcd9: mov eax, dword ptr [ebp + 0xe]\n"
        ".Lcc_49fcdc: add esp, 0xc\n"
        ".Lcc_49fcdf: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_49fce2: shr eax, 0x4\n"
        ".Lcc_49fce5: and ax, 0x7ff\n"
        ".Lcc_49fce9: movsx esi, ax\n"
        ".Lcc_49fcec: sub esi, 0x3fe\n"
        ".Lcc_49fcf2: mov eax, dword ptr [ebp + 0x10]\n"
        ".Lcc_49fcf5: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_49fcf8: mov dword ptr [eax], esi\n"
        ".Lcc_49fcfa: pop esi\n"
        ".Lcc_49fcfb: leave\n"
        ".Lcc_49fcfc: ret\n"
        /* 0x49fcfd frnd */
        ".Lcc_49fcfd: push ebp\n"
        ".Lcc_49fcfe: mov ebp, esp\n"
        ".Lcc_49fd00: push ecx\n"
        ".Lcc_49fd01: push ecx\n"
        ".Lcc_49fd02: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49fd05: frndint\n"
        ".Lcc_49fd07: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_49fd0a: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_49fd0d: leave\n"
        ".Lcc_49fd0e: ret\n"
        /* 0x49fd0f fpclass */
        ".Lcc_49fd0f: push ebp\n"
        ".Lcc_49fd10: mov ebp, esp\n"
        ".Lcc_49fd12: mov eax, dword ptr [ebp + 0xe]\n"
        ".Lcc_49fd15: mov ecx, 0x7ff0\n"
        ".Lcc_49fd1a: mov edx, eax\n"
        ".Lcc_49fd1c: and edx, ecx\n"
        ".Lcc_49fd1e: cmp dx, cx\n"
        ".Lcc_49fd21: jne .Lcc_49fd4f\n"
        ".Lcc_49fd23: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49fd26: push ecx\n"
        ".Lcc_49fd27: push ecx\n"
        ".Lcc_49fd28: fstp qword ptr [esp]\n"
        ".Lcc_49fd2b: call .Lcc_49fbe2\n"
        ".Lcc_49fd30: pop ecx\n"
        ".Lcc_49fd31: dec eax\n"
        ".Lcc_49fd32: pop ecx\n"
        ".Lcc_49fd33: je .Lcc_49fd48\n"
        ".Lcc_49fd35: dec eax\n"
        ".Lcc_49fd36: je .Lcc_49fd44\n"
        ".Lcc_49fd38: dec eax\n"
        ".Lcc_49fd39: je .Lcc_49fd40\n"
        ".Lcc_49fd3b: push 0x1\n"
        ".Lcc_49fd3d: pop eax\n"
        ".Lcc_49fd3e: pop ebp\n"
        ".Lcc_49fd3f: ret\n"
        ".Lcc_49fd40: push 0x2\n"
        ".Lcc_49fd42: jmp .Lcc_49fd3d\n"
        ".Lcc_49fd44: push 0x4\n"
        ".Lcc_49fd46: jmp .Lcc_49fd3d\n"
        ".Lcc_49fd48: mov eax, 0x200\n"
        ".Lcc_49fd4d: pop ebp\n"
        ".Lcc_49fd4e: ret\n"
        ".Lcc_49fd4f: and eax, 0x8000\n"
        ".Lcc_49fd54: test dx, dx\n"
        ".Lcc_49fd57: mov ecx, eax\n"
        ".Lcc_49fd59: jne .Lcc_49fd79\n"
        ".Lcc_49fd5b: test dword ptr [ebp + 0xc], 0xfffff\n"
        ".Lcc_49fd62: jne .Lcc_49fd6a\n"
        ".Lcc_49fd64: cmp dword ptr [ebp + 0x8], 0x0\n"
        ".Lcc_49fd68: je .Lcc_49fd79\n"
        ".Lcc_49fd6a: mov eax, ecx\n"
        ".Lcc_49fd6c: neg eax\n"
        ".Lcc_49fd6e: sbb eax, eax\n"
        ".Lcc_49fd70: and al, -0x70\n"
        ".Lcc_49fd72: add eax, 0x80\n"
        ".Lcc_49fd77: pop ebp\n"
        ".Lcc_49fd78: ret\n"
        ".Lcc_49fd79: fld qword ptr [ebp + 0x8]\n"
        ".Lcc_49fd7c: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_49fd82: fnstsw ax\n"
        ".Lcc_49fd84: sahf\n"
        ".Lcc_49fd85: mov eax, ecx\n"
        ".Lcc_49fd87: jne .Lcc_49fd94\n"
        ".Lcc_49fd89: neg eax\n"
        ".Lcc_49fd8b: sbb eax, eax\n"
        ".Lcc_49fd8d: and al, -0x20\n"
        ".Lcc_49fd8f: add eax, 0x40\n"
        ".Lcc_49fd92: pop ebp\n"
        ".Lcc_49fd93: ret\n"
        ".Lcc_49fd94: neg eax\n"
        ".Lcc_49fd96: sbb eax, eax\n"
        ".Lcc_49fd98: and al, 0x8\n"
        ".Lcc_49fd9a: add eax, 0x100\n"
        ".Lcc_49fd9f: pop ebp\n"
        ".Lcc_49fda0: ret\n"
        ".globl crtc_handle_qnan2\n"
        ".type crtc_handle_qnan2,@function\n"
        "crtc_handle_qnan2: push ebp\n"
        ".Lcc_4a0ebb: mov ebp, esp\n"
        ".Lcc_4a0ebd: push ecx\n"
        ".Lcc_4a0ebe: push ecx\n"
        ".Lcc_4a0ebf: fld qword ptr [ebp + 0xc]\n"
        ".Lcc_4a0ec2: fadd qword ptr [ebp + 0x14]\n"
        ".Lcc_4a0ec5: cmp dword ptr [0x4b5948], 0x0\n"
        ".Lcc_4a0ecc: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_4a0ecf: jne .Lcc_4a0efb\n"
        ".Lcc_4a0ed1: push dword ptr [ebp + 0x1c]\n"
        ".Lcc_4a0ed4: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_4a0ed7: push ecx\n"
        ".Lcc_4a0ed8: push ecx\n"
        ".Lcc_4a0ed9: fstp qword ptr [esp]\n"
        ".Lcc_4a0edc: fld qword ptr [ebp + 0x14]\n"
        ".Lcc_4a0edf: push ecx\n"
        ".Lcc_4a0ee0: push ecx\n"
        ".Lcc_4a0ee1: fstp qword ptr [esp]\n"
        ".Lcc_4a0ee4: fld qword ptr [ebp + 0xc]\n"
        ".Lcc_4a0ee7: push ecx\n"
        ".Lcc_4a0ee8: push ecx\n"
        ".Lcc_4a0ee9: fstp qword ptr [esp]\n"
        ".Lcc_4a0eec: push dword ptr [ebp + 0x8]\n"
        ".Lcc_4a0eef: push 0x1\n"
        ".Lcc_4a0ef1: call .Lcc_4a1489\n"
        ".Lcc_4a0ef6: add esp, 0x24\n"
        ".Lcc_4a0ef9: leave\n"
        ".Lcc_4a0efa: ret\n"
        ".Lcc_4a0efb: call crtc_errno\n"
        ".Lcc_4a0f00: push 0xffff\n"
        ".Lcc_4a0f05: mov dword ptr [eax], 0x21\n"
        ".Lcc_4a0f0b: push dword ptr [ebp + 0x1c]\n"
        ".Lcc_4a0f0e: call .Lcc_4a15ab\n"
        ".Lcc_4a0f13: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_4a0f16: pop ecx\n"
        ".Lcc_4a0f17: pop ecx\n"
        ".Lcc_4a0f18: leave\n"
        ".Lcc_4a0f19: ret\n"
        ".globl crtc_except2\n"
        ".type crtc_except2,@function\n"
        "crtc_except2: push ebp\n"
        ".Lcc_4a0f1b: mov ebp, esp\n"
        ".Lcc_4a0f1d: sub esp, 0x58\n"
        ".Lcc_4a0f20: push dword ptr [ebp + 0x28]\n"
        ".Lcc_4a0f23: lea eax, [ebp + 0x20]\n"
        ".Lcc_4a0f26: push eax\n"
        ".Lcc_4a0f27: push dword ptr [ebp + 0x8]\n"
        ".Lcc_4a0f2a: call .Lcc_4a1272\n"
        ".Lcc_4a0f2f: add esp, 0xc\n"
        ".Lcc_4a0f32: test eax, eax\n"
        ".Lcc_4a0f34: jne .Lcc_4a0f64\n"
        ".Lcc_4a0f36: mov eax, dword ptr [ebp - 0x20]\n"
        ".Lcc_4a0f39: and al, -0x1d\n"
        ".Lcc_4a0f3b: or al, 0x3\n"
        ".Lcc_4a0f3d: mov dword ptr [ebp - 0x20], eax\n"
        ".Lcc_4a0f40: lea eax, [ebp + 0x20]\n"
        ".Lcc_4a0f43: push eax\n"
        ".Lcc_4a0f44: lea eax, [ebp + 0x10]\n"
        ".Lcc_4a0f47: push eax\n"
        ".Lcc_4a0f48: lea eax, [ebp + 0x28]\n"
        ".Lcc_4a0f4b: push dword ptr [ebp + 0xc]\n"
        ".Lcc_4a0f4e: fld qword ptr [ebp + 0x18]\n"
        ".Lcc_4a0f51: push dword ptr [ebp + 0x8]\n"
        ".Lcc_4a0f54: fstp qword ptr [ebp - 0x30]\n"
        ".Lcc_4a0f57: push eax\n"
        ".Lcc_4a0f58: lea eax, [ebp - 0x58]\n"
        ".Lcc_4a0f5b: push eax\n"
        ".Lcc_4a0f5c: call .Lcc_4a0fbf\n"
        ".Lcc_4a0f61: add esp, 0x18\n"
        ".Lcc_4a0f64: push dword ptr [ebp + 0x8]\n"
        ".Lcc_4a0f67: call .Lcc_4a155e\n"
        ".Lcc_4a0f6c: cmp dword ptr [0x4b5948], 0x0\n"
        ".Lcc_4a0f73: pop ecx\n"
        ".Lcc_4a0f74: jne .Lcc_4a0fa3\n"
        ".Lcc_4a0f76: test eax, eax\n"
        ".Lcc_4a0f78: je .Lcc_4a0fa3\n"
        ".Lcc_4a0f7a: push dword ptr [ebp + 0x28]\n"
        ".Lcc_4a0f7d: fld qword ptr [ebp + 0x20]\n"
        ".Lcc_4a0f80: push ecx\n"
        ".Lcc_4a0f81: push ecx\n"
        ".Lcc_4a0f82: fstp qword ptr [esp]\n"
        ".Lcc_4a0f85: fld qword ptr [ebp + 0x18]\n"
        ".Lcc_4a0f88: push ecx\n"
        ".Lcc_4a0f89: push ecx\n"
        ".Lcc_4a0f8a: fstp qword ptr [esp]\n"
        ".Lcc_4a0f8d: fld qword ptr [ebp + 0x10]\n"
        ".Lcc_4a0f90: push ecx\n"
        ".Lcc_4a0f91: push ecx\n"
        ".Lcc_4a0f92: fstp qword ptr [esp]\n"
        ".Lcc_4a0f95: push dword ptr [ebp + 0xc]\n"
        ".Lcc_4a0f98: push eax\n"
        ".Lcc_4a0f99: call .Lcc_4a1489\n"
        ".Lcc_4a0f9e: add esp, 0x24\n"
        ".Lcc_4a0fa1: leave\n"
        ".Lcc_4a0fa2: ret\n"
        ".Lcc_4a0fa3: push eax\n"
        ".Lcc_4a0fa4: call .Lcc_4a1511\n"
        ".Lcc_4a0fa9: mov dword ptr [esp], 0xffff\n"
        ".Lcc_4a0fb0: push dword ptr [ebp + 0x28]\n"
        ".Lcc_4a0fb3: call .Lcc_4a15ab\n"
        ".Lcc_4a0fb8: fld qword ptr [ebp + 0x20]\n"
        ".Lcc_4a0fbb: pop ecx\n"
        ".Lcc_4a0fbc: pop ecx\n"
        ".Lcc_4a0fbd: leave\n"
        ".Lcc_4a0fbe: ret\n"
        /* 0x4a0fbf raise_exc */
        ".Lcc_4a0fbf: push ebp\n"
        ".Lcc_4a0fc0: mov ebp, esp\n"
        ".Lcc_4a0fc2: mov ecx, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a0fc5: xor eax, eax\n"
        ".Lcc_4a0fc7: push ebx\n"
        ".Lcc_4a0fc8: push esi\n"
        ".Lcc_4a0fc9: mov dword ptr [ecx + 0x4], eax\n"
        ".Lcc_4a0fcc: mov ecx, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a0fcf: push edi\n"
        ".Lcc_4a0fd0: push 0x1\n"
        ".Lcc_4a0fd2: mov dword ptr [ecx + 0x8], eax\n"
        ".Lcc_4a0fd5: mov ecx, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a0fd8: pop ebx\n"
        ".Lcc_4a0fd9: mov dword ptr [ecx + 0xc], eax\n"
        ".Lcc_4a0fdc: mov cl, byte ptr [ebp + 0x10]\n"
        ".Lcc_4a0fdf: test cl, 0x10\n"
        ".Lcc_4a0fe2: je .Lcc_4a0ff1\n"
        ".Lcc_4a0fe4: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a0fe7: mov dword ptr [ebp + 0x10], 0xc000008f\n"
        ".Lcc_4a0fee: or dword ptr [eax + 0x4], ebx\n"
        ".Lcc_4a0ff1: test cl, 0x2\n"
        ".Lcc_4a0ff4: je .Lcc_4a1004\n"
        ".Lcc_4a0ff6: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a0ff9: mov dword ptr [ebp + 0x10], 0xc0000093\n"
        ".Lcc_4a1000: or dword ptr [eax + 0x4], 0x2\n"
        ".Lcc_4a1004: test bl, cl\n"
        ".Lcc_4a1006: je .Lcc_4a1016\n"
        ".Lcc_4a1008: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a100b: mov dword ptr [ebp + 0x10], 0xc0000091\n"
        ".Lcc_4a1012: or dword ptr [eax + 0x4], 0x4\n"
        ".Lcc_4a1016: test cl, 0x4\n"
        ".Lcc_4a1019: je .Lcc_4a1029\n"
        ".Lcc_4a101b: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a101e: mov dword ptr [ebp + 0x10], 0xc000008e\n"
        ".Lcc_4a1025: or dword ptr [eax + 0x4], 0x8\n"
        ".Lcc_4a1029: test cl, 0x8\n"
        ".Lcc_4a102c: je .Lcc_4a103c\n"
        ".Lcc_4a102e: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a1031: mov dword ptr [ebp + 0x10], 0xc0000090\n"
        ".Lcc_4a1038: or dword ptr [eax + 0x4], 0x10\n"
        ".Lcc_4a103c: mov esi, dword ptr [ebp + 0xc]\n"
        ".Lcc_4a103f: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a1042: push 0x2\n"
        ".Lcc_4a1044: mov ecx, dword ptr [esi]\n"
        ".Lcc_4a1046: mov edx, dword ptr [eax + 0x8]\n"
        ".Lcc_4a1049: not ecx\n"
        ".Lcc_4a104b: and ecx, ebx\n"
        ".Lcc_4a104d: and edx, -0x11\n"
        ".Lcc_4a1050: shl ecx, 0x4\n"
        ".Lcc_4a1053: or ecx, edx\n"
        ".Lcc_4a1055: pop edi\n"
        ".Lcc_4a1056: mov dword ptr [eax + 0x8], ecx\n"
        ".Lcc_4a1059: mov ecx, dword ptr [esi]\n"
        ".Lcc_4a105b: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a105e: not ecx\n"
        ".Lcc_4a1060: mov edx, dword ptr [eax + 0x8]\n"
        ".Lcc_4a1063: and ecx, 0x4\n"
        ".Lcc_4a1066: shl ecx, 1\n"
        ".Lcc_4a1068: and edx, -0x9\n"
        ".Lcc_4a106b: or ecx, edx\n"
        ".Lcc_4a106d: mov dword ptr [eax + 0x8], ecx\n"
        ".Lcc_4a1070: mov ecx, dword ptr [esi]\n"
        ".Lcc_4a1072: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a1075: not ecx\n"
        ".Lcc_4a1077: mov edx, dword ptr [eax + 0x8]\n"
        ".Lcc_4a107a: shr ecx, 1\n"
        ".Lcc_4a107c: and ecx, 0x4\n"
        ".Lcc_4a107f: and edx, -0x5\n"
        ".Lcc_4a1082: or ecx, edx\n"
        ".Lcc_4a1084: mov dword ptr [eax + 0x8], ecx\n"
        ".Lcc_4a1087: mov ecx, dword ptr [esi]\n"
        ".Lcc_4a1089: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a108c: not ecx\n"
        ".Lcc_4a108e: mov edx, dword ptr [eax + 0x8]\n"
        ".Lcc_4a1091: shr ecx, 0x3\n"
        ".Lcc_4a1094: and ecx, edi\n"
        ".Lcc_4a1096: and edx, -0x3\n"
        ".Lcc_4a1099: or ecx, edx\n"
        ".Lcc_4a109b: mov dword ptr [eax + 0x8], ecx\n"
        ".Lcc_4a109e: mov ecx, dword ptr [esi]\n"
        ".Lcc_4a10a0: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a10a3: not ecx\n"
        ".Lcc_4a10a5: mov edx, dword ptr [eax + 0x8]\n"
        ".Lcc_4a10a8: shr ecx, 0x5\n"
        ".Lcc_4a10ab: and ecx, ebx\n"
        ".Lcc_4a10ad: and edx, -0x2\n"
        ".Lcc_4a10b0: or ecx, edx\n"
        ".Lcc_4a10b2: mov dword ptr [eax + 0x8], ecx\n"
        ".Lcc_4a10b5: call .Lcc_4a158e\n"
        ".Lcc_4a10ba: test bl, al\n"
        ".Lcc_4a10bc: je .Lcc_4a10c5\n"
        ".Lcc_4a10be: mov ecx, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a10c1: or dword ptr [ecx + 0xc], 0x10\n"
        ".Lcc_4a10c5: test al, 0x4\n"
        ".Lcc_4a10c7: je .Lcc_4a10d0\n"
        ".Lcc_4a10c9: mov ecx, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a10cc: or dword ptr [ecx + 0xc], 0x8\n"
        ".Lcc_4a10d0: test al, 0x8\n"
        ".Lcc_4a10d2: je .Lcc_4a10db\n"
        ".Lcc_4a10d4: mov ecx, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a10d7: or dword ptr [ecx + 0xc], 0x4\n"
        ".Lcc_4a10db: test al, 0x10\n"
        ".Lcc_4a10dd: je .Lcc_4a10e5\n"
        ".Lcc_4a10df: mov ecx, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a10e2: or dword ptr [ecx + 0xc], edi\n"
        ".Lcc_4a10e5: test al, 0x20\n"
        ".Lcc_4a10e7: je .Lcc_4a10ef\n"
        ".Lcc_4a10e9: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a10ec: or dword ptr [eax + 0xc], ebx\n"
        ".Lcc_4a10ef: mov eax, dword ptr [esi]\n"
        ".Lcc_4a10f1: mov ecx, 0xc00\n"
        ".Lcc_4a10f6: and eax, ecx\n"
        ".Lcc_4a10f8: je .Lcc_4a112e\n"
        ".Lcc_4a10fa: cmp eax, 0x400\n"
        ".Lcc_4a10ff: je .Lcc_4a1120\n"
        ".Lcc_4a1101: cmp eax, 0x800\n"
        ".Lcc_4a1106: je .Lcc_4a1114\n"
        ".Lcc_4a1108: cmp eax, ecx\n"
        ".Lcc_4a110a: jne .Lcc_4a1134\n"
        ".Lcc_4a110c: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a110f: or dword ptr [eax], 0x3\n"
        ".Lcc_4a1112: jmp .Lcc_4a1134\n"
        ".Lcc_4a1114: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a1117: mov ecx, dword ptr [eax]\n"
        ".Lcc_4a1119: and ecx, -0x2\n"
        ".Lcc_4a111c: or ecx, edi\n"
        ".Lcc_4a111e: jmp .Lcc_4a112a\n"
        ".Lcc_4a1120: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a1123: mov ecx, dword ptr [eax]\n"
        ".Lcc_4a1125: and ecx, -0x3\n"
        ".Lcc_4a1128: or ecx, ebx\n"
        ".Lcc_4a112a: mov dword ptr [eax], ecx\n"
        ".Lcc_4a112c: jmp .Lcc_4a1134\n"
        ".Lcc_4a112e: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a1131: and dword ptr [eax], -0x4\n"
        ".Lcc_4a1134: mov eax, dword ptr [esi]\n"
        ".Lcc_4a1136: mov ecx, 0x300\n"
        ".Lcc_4a113b: and eax, ecx\n"
        ".Lcc_4a113d: je .Lcc_4a115f\n"
        ".Lcc_4a113f: cmp eax, 0x200\n"
        ".Lcc_4a1144: je .Lcc_4a1152\n"
        ".Lcc_4a1146: cmp eax, ecx\n"
        ".Lcc_4a1148: jne .Lcc_4a116c\n"
        ".Lcc_4a114a: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a114d: and dword ptr [eax], -0x1d\n"
        ".Lcc_4a1150: jmp .Lcc_4a116c\n"
        ".Lcc_4a1152: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a1155: mov ecx, dword ptr [eax]\n"
        ".Lcc_4a1157: and ecx, -0x19\n"
        ".Lcc_4a115a: or ecx, 0x4\n"
        ".Lcc_4a115d: jmp .Lcc_4a116a\n"
        ".Lcc_4a115f: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a1162: mov ecx, dword ptr [eax]\n"
        ".Lcc_4a1164: and ecx, -0x15\n"
        ".Lcc_4a1167: or ecx, 0x8\n"
        ".Lcc_4a116a: mov dword ptr [eax], ecx\n"
        ".Lcc_4a116c: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a116f: mov ecx, dword ptr [ebp + 0x14]\n"
        ".Lcc_4a1172: and ecx, 0xfff\n"
        ".Lcc_4a1178: mov edx, dword ptr [eax]\n"
        ".Lcc_4a117a: shl ecx, 0x5\n"
        ".Lcc_4a117d: and edx, 0xfffe001f\n"
        ".Lcc_4a1183: or ecx, edx\n"
        ".Lcc_4a1185: mov dword ptr [eax], ecx\n"
        ".Lcc_4a1187: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a118a: or dword ptr [eax + 0x20], ebx\n"
        ".Lcc_4a118d: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a1190: mov ecx, dword ptr [eax + 0x20]\n"
        ".Lcc_4a1193: and ecx, -0x1d\n"
        ".Lcc_4a1196: or ecx, edi\n"
        ".Lcc_4a1198: mov dword ptr [eax + 0x20], ecx\n"
        ".Lcc_4a119b: mov eax, dword ptr [ebp + 0x18]\n"
        ".Lcc_4a119e: fld qword ptr [eax]\n"
        ".Lcc_4a11a0: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a11a3: fstp qword ptr [eax + 0x10]\n"
        ".Lcc_4a11a6: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a11a9: or dword ptr [eax + 0x50], ebx\n"
        ".Lcc_4a11ac: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a11af: mov ecx, dword ptr [eax + 0x50]\n"
        ".Lcc_4a11b2: and ecx, -0x1d\n"
        ".Lcc_4a11b5: or ecx, edi\n"
        ".Lcc_4a11b7: mov edi, dword ptr [ebp + 0x1c]\n"
        ".Lcc_4a11ba: mov dword ptr [eax + 0x50], ecx\n"
        ".Lcc_4a11bd: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a11c0: fld qword ptr [edi]\n"
        ".Lcc_4a11c2: fstp qword ptr [eax + 0x40]\n"
        ".Lcc_4a11c5: call .Lcc_4a159c\n"
        ".Lcc_4a11ca: lea eax, [ebp + 0x8]\n"
        ".Lcc_4a11cd: push eax\n"
        ".Lcc_4a11ce: push ebx\n"
        ".Lcc_4a11cf: push 0x0\n"
        ".Lcc_4a11d1: push dword ptr [ebp + 0x10]\n"
        ".Lcc_4a11d4: call dword ptr [0x4a30f4]\n"
        ".Lcc_4a11da: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a11dd: test byte ptr [eax + 0x8], 0x10\n"
        ".Lcc_4a11e1: je .Lcc_4a11e6\n"
        ".Lcc_4a11e3: and dword ptr [esi], -0x2\n"
        ".Lcc_4a11e6: test byte ptr [eax + 0x8], 0x8\n"
        ".Lcc_4a11ea: je .Lcc_4a11ef\n"
        ".Lcc_4a11ec: and dword ptr [esi], -0x5\n"
        ".Lcc_4a11ef: test byte ptr [eax + 0x8], 0x4\n"
        ".Lcc_4a11f3: je .Lcc_4a11f8\n"
        ".Lcc_4a11f5: and dword ptr [esi], -0x9\n"
        ".Lcc_4a11f8: test byte ptr [eax + 0x8], 0x2\n"
        ".Lcc_4a11fc: je .Lcc_4a1201\n"
        ".Lcc_4a11fe: and dword ptr [esi], -0x11\n"
        ".Lcc_4a1201: test byte ptr [eax + 0x8], bl\n"
        ".Lcc_4a1204: je .Lcc_4a1209\n"
        ".Lcc_4a1206: and dword ptr [esi], -0x21\n"
        ".Lcc_4a1209: mov ecx, dword ptr [eax]\n"
        ".Lcc_4a120b: mov edx, 0xfffff3ff\n"
        ".Lcc_4a1210: and ecx, 0x3\n"
        ".Lcc_4a1213: sub ecx, 0x0\n"
        ".Lcc_4a1216: je .Lcc_4a123d\n"
        ".Lcc_4a1218: dec ecx\n"
        ".Lcc_4a1219: je .Lcc_4a1231\n"
        ".Lcc_4a121b: dec ecx\n"
        ".Lcc_4a121c: je .Lcc_4a1227\n"
        ".Lcc_4a121e: dec ecx\n"
        ".Lcc_4a121f: jne .Lcc_4a123f\n"
        ".Lcc_4a1221: or byte ptr [esi + 0x1], 0xc\n"
        ".Lcc_4a1225: jmp .Lcc_4a123f\n"
        ".Lcc_4a1227: mov ecx, dword ptr [esi]\n"
        ".Lcc_4a1229: and ch, -0x5\n"
        ".Lcc_4a122c: or ch, 0x8\n"
        ".Lcc_4a122f: jmp .Lcc_4a1239\n"
        ".Lcc_4a1231: mov ecx, dword ptr [esi]\n"
        ".Lcc_4a1233: and ch, -0x9\n"
        ".Lcc_4a1236: or ch, 0x4\n"
        ".Lcc_4a1239: mov dword ptr [esi], ecx\n"
        ".Lcc_4a123b: jmp .Lcc_4a123f\n"
        ".Lcc_4a123d: and dword ptr [esi], edx\n"
        ".Lcc_4a123f: mov ecx, dword ptr [eax]\n"
        ".Lcc_4a1241: shr ecx, 0x2\n"
        ".Lcc_4a1244: and ecx, 0x7\n"
        ".Lcc_4a1247: sub ecx, 0x0\n"
        ".Lcc_4a124a: je .Lcc_4a125f\n"
        ".Lcc_4a124c: dec ecx\n"
        ".Lcc_4a124d: je .Lcc_4a1256\n"
        ".Lcc_4a124f: dec ecx\n"
        ".Lcc_4a1250: jne .Lcc_4a1268\n"
        ".Lcc_4a1252: and dword ptr [esi], edx\n"
        ".Lcc_4a1254: jmp .Lcc_4a1268\n"
        ".Lcc_4a1256: mov ecx, dword ptr [esi]\n"
        ".Lcc_4a1258: and ecx, edx\n"
        ".Lcc_4a125a: or ch, 0x2\n"
        ".Lcc_4a125d: jmp .Lcc_4a1266\n"
        ".Lcc_4a125f: mov ecx, dword ptr [esi]\n"
        ".Lcc_4a1261: and ecx, edx\n"
        ".Lcc_4a1263: or ch, 0x3\n"
        ".Lcc_4a1266: mov dword ptr [esi], ecx\n"
        ".Lcc_4a1268: fld qword ptr [eax + 0x40]\n"
        ".Lcc_4a126b: fstp qword ptr [edi]\n"
        ".Lcc_4a126d: pop edi\n"
        ".Lcc_4a126e: pop esi\n"
        ".Lcc_4a126f: pop ebx\n"
        ".Lcc_4a1270: pop ebp\n"
        ".Lcc_4a1271: ret\n"
        /* 0x4a1272 handle_exc */
        ".Lcc_4a1272: push ebp\n"
        ".Lcc_4a1273: mov ebp, esp\n"
        ".Lcc_4a1275: sub esp, 0xc\n"
        ".Lcc_4a1278: mov eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a127b: push ebx\n"
        ".Lcc_4a127c: push edi\n"
        ".Lcc_4a127d: mov edi, eax\n"
        ".Lcc_4a127f: and edi, 0x1f\n"
        ".Lcc_4a1282: push 0x1\n"
        ".Lcc_4a1284: test al, 0x8\n"
        ".Lcc_4a1286: pop ebx\n"
        ".Lcc_4a1287: je .Lcc_4a129d\n"
        ".Lcc_4a1289: test byte ptr [ebp + 0x10], bl\n"
        ".Lcc_4a128c: je .Lcc_4a129d\n"
        ".Lcc_4a128e: push ebx\n"
        ".Lcc_4a128f: call .Lcc_4a15ce\n"
        ".Lcc_4a1294: pop ecx\n"
        ".Lcc_4a1295: and edi, -0x9\n"
        ".Lcc_4a1298: jmp .Lcc_4a1467\n"
        ".Lcc_4a129d: test al, 0x4\n"
        ".Lcc_4a129f: je .Lcc_4a12b7\n"
        ".Lcc_4a12a1: test byte ptr [ebp + 0x10], 0x4\n"
        ".Lcc_4a12a5: je .Lcc_4a12b7\n"
        ".Lcc_4a12a7: push 0x4\n"
        ".Lcc_4a12a9: call .Lcc_4a15ce\n"
        ".Lcc_4a12ae: pop ecx\n"
        ".Lcc_4a12af: and edi, -0x5\n"
        ".Lcc_4a12b2: jmp .Lcc_4a1467\n"
        ".Lcc_4a12b7: test bl, al\n"
        ".Lcc_4a12b9: je .Lcc_4a1391\n"
        ".Lcc_4a12bf: test byte ptr [ebp + 0x10], 0x8\n"
        ".Lcc_4a12c3: je .Lcc_4a1391\n"
        ".Lcc_4a12c9: push 0x8\n"
        ".Lcc_4a12cb: call .Lcc_4a15ce\n"
        ".Lcc_4a12d0: pop ecx\n"
        ".Lcc_4a12d1: mov eax, 0xc00\n"
        ".Lcc_4a12d6: mov ecx, dword ptr [ebp + 0x10]\n"
        ".Lcc_4a12d9: and ecx, eax\n"
        ".Lcc_4a12db: je .Lcc_4a1369\n"
        ".Lcc_4a12e1: cmp ecx, 0x400\n"
        ".Lcc_4a12e7: je .Lcc_4a1341\n"
        ".Lcc_4a12e9: cmp ecx, 0x800\n"
        ".Lcc_4a12ef: je .Lcc_4a1319\n"
        ".Lcc_4a12f1: cmp ecx, eax\n"
        ".Lcc_4a12f3: jne .Lcc_4a1389\n"
        ".Lcc_4a12f9: mov ecx, dword ptr [ebp + 0xc]\n"
        ".Lcc_4a12fc: fld qword ptr [ecx]\n"
        ".Lcc_4a12fe: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_4a1304: fld qword ptr [0x4b5850]\n"
        ".Lcc_4a130a: fnstsw ax\n"
        ".Lcc_4a130c: sahf\n"
        ".Lcc_4a130d: ja .Lcc_4a1311\n"
        ".Lcc_4a130f: fchs\n"
        ".Lcc_4a1311: fstp qword ptr [ebp - 0xc]\n"
        ".Lcc_4a1314: fld qword ptr [ebp - 0xc]\n"
        ".Lcc_4a1317: jmp .Lcc_4a1387\n"
        ".Lcc_4a1319: mov ecx, dword ptr [ebp + 0xc]\n"
        ".Lcc_4a131c: fld qword ptr [ecx]\n"
        ".Lcc_4a131e: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_4a1324: fnstsw ax\n"
        ".Lcc_4a1326: sahf\n"
        ".Lcc_4a1327: jbe .Lcc_4a1331\n"
        ".Lcc_4a1329: fld qword ptr [0x4b5840]\n"
        ".Lcc_4a132f: jmp .Lcc_4a1339\n"
        ".Lcc_4a1331: fld qword ptr [0x4b5850]\n"
        ".Lcc_4a1337: fchs\n"
        ".Lcc_4a1339: fstp qword ptr [ebp - 0xc]\n"
        ".Lcc_4a133c: fld qword ptr [ebp - 0xc]\n"
        ".Lcc_4a133f: jmp .Lcc_4a1387\n"
        ".Lcc_4a1341: mov ecx, dword ptr [ebp + 0xc]\n"
        ".Lcc_4a1344: fld qword ptr [ecx]\n"
        ".Lcc_4a1346: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_4a134c: fnstsw ax\n"
        ".Lcc_4a134e: sahf\n"
        ".Lcc_4a134f: jbe .Lcc_4a1359\n"
        ".Lcc_4a1351: fld qword ptr [0x4b5850]\n"
        ".Lcc_4a1357: jmp .Lcc_4a1361\n"
        ".Lcc_4a1359: fld qword ptr [0x4b5840]\n"
        ".Lcc_4a135f: fchs\n"
        ".Lcc_4a1361: fstp qword ptr [ebp - 0xc]\n"
        ".Lcc_4a1364: fld qword ptr [ebp - 0xc]\n"
        ".Lcc_4a1367: jmp .Lcc_4a1387\n"
        ".Lcc_4a1369: mov ecx, dword ptr [ebp + 0xc]\n"
        ".Lcc_4a136c: fld qword ptr [ecx]\n"
        ".Lcc_4a136e: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_4a1374: fld qword ptr [0x4b5840]\n"
        ".Lcc_4a137a: fnstsw ax\n"
        ".Lcc_4a137c: sahf\n"
        ".Lcc_4a137d: ja .Lcc_4a1381\n"
        ".Lcc_4a137f: fchs\n"
        ".Lcc_4a1381: fstp qword ptr [ebp - 0xc]\n"
        ".Lcc_4a1384: fld qword ptr [ebp - 0xc]\n"
        ".Lcc_4a1387: fstp qword ptr [ecx]\n"
        ".Lcc_4a1389: and edi, -0x2\n"
        ".Lcc_4a138c: jmp .Lcc_4a1467\n"
        ".Lcc_4a1391: test al, 0x2\n"
        ".Lcc_4a1393: je .Lcc_4a1467\n"
        ".Lcc_4a1399: test byte ptr [ebp + 0x10], 0x10\n"
        ".Lcc_4a139d: je .Lcc_4a1467\n"
        ".Lcc_4a13a3: push esi\n"
        ".Lcc_4a13a4: xor esi, esi\n"
        ".Lcc_4a13a6: test al, 0x10\n"
        ".Lcc_4a13a8: je .Lcc_4a13ac\n"
        ".Lcc_4a13aa: mov esi, ebx\n"
        ".Lcc_4a13ac: mov eax, dword ptr [ebp + 0xc]\n"
        ".Lcc_4a13af: fld qword ptr [eax]\n"
        ".Lcc_4a13b1: fstp qword ptr [ebp - 0xc]\n"
        ".Lcc_4a13b4: fld qword ptr [ebp - 0xc]\n"
        ".Lcc_4a13b7: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_4a13bd: fnstsw ax\n"
        ".Lcc_4a13bf: sahf\n"
        ".Lcc_4a13c0: je .Lcc_4a1455\n"
        ".Lcc_4a13c6: fld qword ptr [ebp - 0xc]\n"
        ".Lcc_4a13c9: lea eax, [ebp - 0x4]\n"
        ".Lcc_4a13cc: push eax\n"
        ".Lcc_4a13cd: push ecx\n"
        ".Lcc_4a13ce: push ecx\n"
        ".Lcc_4a13cf: fstp qword ptr [esp]\n"
        ".Lcc_4a13d2: call .Lcc_49fc3c\n"
        ".Lcc_4a13d7: mov eax, dword ptr [ebp - 0x4]\n"
        ".Lcc_4a13da: add esp, 0xc\n"
        ".Lcc_4a13dd: fstp qword ptr [ebp - 0xc]\n"
        ".Lcc_4a13e0: lea ecx, [eax - 0x600]\n"
        ".Lcc_4a13e6: cmp ecx, 0xfffffbce\n"
        ".Lcc_4a13ec: jge .Lcc_4a13f7\n"
        ".Lcc_4a13ee: fldz\n"
        ".Lcc_4a13f0: mov esi, ebx\n"
        ".Lcc_4a13f2: fstp qword ptr [ebp - 0xc]\n"
        ".Lcc_4a13f5: jmp .Lcc_4a144b\n"
        ".Lcc_4a13f7: fld qword ptr [ebp - 0xc]\n"
        ".Lcc_4a13fa: fcomp qword ptr [0x4a4d18]\n"
        ".Lcc_4a1400: fnstsw ax\n"
        ".Lcc_4a1402: sahf\n"
        ".Lcc_4a1403: jae .Lcc_4a1409\n"
        ".Lcc_4a1405: mov edx, ebx\n"
        ".Lcc_4a1407: jmp .Lcc_4a140b\n"
        ".Lcc_4a1409: xor edx, edx\n"
        ".Lcc_4a140b: mov al, byte ptr [ebp - 0x6]\n"
        ".Lcc_4a140e: and eax, 0xf\n"
        ".Lcc_4a1411: or al, 0x10\n"
        ".Lcc_4a1413: mov word ptr [ebp - 0x6], ax\n"
        ".Lcc_4a1417: mov eax, 0xfffffc03\n"
        ".Lcc_4a141c: cmp ecx, eax\n"
        ".Lcc_4a141e: jge .Lcc_4a143f\n"
        ".Lcc_4a1420: sub eax, ecx\n"
        ".Lcc_4a1422: test byte ptr [ebp - 0xc], bl\n"
        ".Lcc_4a1425: je .Lcc_4a142d\n"
        ".Lcc_4a1427: test esi, esi\n"
        ".Lcc_4a1429: jne .Lcc_4a142d\n"
        ".Lcc_4a142b: mov esi, ebx\n"
        ".Lcc_4a142d: shr dword ptr [ebp - 0xc], 1\n"
        ".Lcc_4a1430: test byte ptr [ebp - 0x8], bl\n"
        ".Lcc_4a1433: je .Lcc_4a1439\n"
        ".Lcc_4a1435: or byte ptr [ebp - 0x9], -0x80\n"
        ".Lcc_4a1439: shr dword ptr [ebp - 0x8], 1\n"
        ".Lcc_4a143c: dec eax\n"
        ".Lcc_4a143d: jne .Lcc_4a1422\n"
        ".Lcc_4a143f: test edx, edx\n"
        ".Lcc_4a1441: je .Lcc_4a144b\n"
        ".Lcc_4a1443: fld qword ptr [ebp - 0xc]\n"
        ".Lcc_4a1446: fchs\n"
        ".Lcc_4a1448: fstp qword ptr [ebp - 0xc]\n"
        ".Lcc_4a144b: fld qword ptr [ebp - 0xc]\n"
        ".Lcc_4a144e: mov eax, dword ptr [ebp + 0xc]\n"
        ".Lcc_4a1451: fstp qword ptr [eax]\n"
        ".Lcc_4a1453: jmp .Lcc_4a1457\n"
        ".Lcc_4a1455: mov esi, ebx\n"
        ".Lcc_4a1457: test esi, esi\n"
        ".Lcc_4a1459: pop esi\n"
        ".Lcc_4a145a: je .Lcc_4a1464\n"
        ".Lcc_4a145c: push 0x10\n"
        ".Lcc_4a145e: call .Lcc_4a15ce\n"
        ".Lcc_4a1463: pop ecx\n"
        ".Lcc_4a1464: and edi, -0x3\n"
        ".Lcc_4a1467: test byte ptr [ebp + 0x8], 0x10\n"
        ".Lcc_4a146b: je .Lcc_4a147e\n"
        ".Lcc_4a146d: test byte ptr [ebp + 0x10], 0x20\n"
        ".Lcc_4a1471: je .Lcc_4a147e\n"
        ".Lcc_4a1473: push 0x20\n"
        ".Lcc_4a1475: call .Lcc_4a15ce\n"
        ".Lcc_4a147a: pop ecx\n"
        ".Lcc_4a147b: and edi, -0x11\n"
        ".Lcc_4a147e: xor eax, eax\n"
        ".Lcc_4a1480: test edi, edi\n"
        ".Lcc_4a1482: pop edi\n"
        ".Lcc_4a1483: pop ebx\n"
        ".Lcc_4a1484: sete al\n"
        ".Lcc_4a1487: leave\n"
        ".Lcc_4a1488: ret\n"
        /* 0x4a1489 umatherr */
        ".Lcc_4a1489: push ebp\n"
        ".Lcc_4a148a: mov ebp, esp\n"
        ".Lcc_4a148c: sub esp, 0x20\n"
        ".Lcc_4a148f: push dword ptr [ebp + 0xc]\n"
        ".Lcc_4a1492: call .Lcc_4a1539\n"
        ".Lcc_4a1497: test eax, eax\n"
        ".Lcc_4a1499: pop ecx\n"
        ".Lcc_4a149a: mov dword ptr [ebp - 0x1c], eax\n"
        ".Lcc_4a149d: je .Lcc_4a14f4\n"
        ".Lcc_4a149f: mov eax, dword ptr [ebp + 0x10]\n"
        ".Lcc_4a14a2: push esi\n"
        ".Lcc_4a14a3: mov dword ptr [ebp - 0x18], eax\n"
        ".Lcc_4a14a6: mov eax, dword ptr [ebp + 0x14]\n"
        ".Lcc_4a14a9: mov dword ptr [ebp - 0x14], eax\n"
        ".Lcc_4a14ac: mov eax, dword ptr [ebp + 0x18]\n"
        ".Lcc_4a14af: mov esi, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a14b2: mov dword ptr [ebp - 0x10], eax\n"
        ".Lcc_4a14b5: mov eax, dword ptr [ebp + 0x1c]\n"
        ".Lcc_4a14b8: push 0xffff\n"
        ".Lcc_4a14bd: push dword ptr [ebp + 0x28]\n"
        ".Lcc_4a14c0: mov dword ptr [ebp - 0xc], eax\n"
        ".Lcc_4a14c3: mov eax, dword ptr [ebp + 0x20]\n"
        ".Lcc_4a14c6: mov dword ptr [ebp - 0x20], esi\n"
        ".Lcc_4a14c9: mov dword ptr [ebp - 0x8], eax\n"
        ".Lcc_4a14cc: mov eax, dword ptr [ebp + 0x24]\n"
        ".Lcc_4a14cf: mov dword ptr [ebp - 0x4], eax\n"
        ".Lcc_4a14d2: call .Lcc_4a15ab\n"
        ".Lcc_4a14d7: lea eax, [ebp - 0x20]\n"
        ".Lcc_4a14da: push eax\n"
        ".Lcc_4a14db: call .Lcc_4a158b\n"
        ".Lcc_4a14e0: add esp, 0xc\n"
        ".Lcc_4a14e3: test eax, eax\n"
        ".Lcc_4a14e5: jne .Lcc_4a14ee\n"
        ".Lcc_4a14e7: push esi\n"
        ".Lcc_4a14e8: call .Lcc_4a1511\n"
        ".Lcc_4a14ed: pop ecx\n"
        ".Lcc_4a14ee: fld qword ptr [ebp - 0x8]\n"
        ".Lcc_4a14f1: pop esi\n"
        ".Lcc_4a14f2: leave\n"
        ".Lcc_4a14f3: ret\n"
        ".Lcc_4a14f4: push 0xffff\n"
        ".Lcc_4a14f9: push dword ptr [ebp + 0x28]\n"
        ".Lcc_4a14fc: call .Lcc_4a15ab\n"
        ".Lcc_4a1501: push dword ptr [ebp + 0x8]\n"
        ".Lcc_4a1504: call .Lcc_4a1511\n"
        ".Lcc_4a1509: fld qword ptr [ebp + 0x20]\n"
        ".Lcc_4a150c: add esp, 0xc\n"
        ".Lcc_4a150f: leave\n"
        ".Lcc_4a1510: ret\n"
        /* 0x4a1511 set_errno */
        ".Lcc_4a1511: mov eax, dword ptr [esp + 0x4]\n"
        ".Lcc_4a1515: cmp eax, 0x1\n"
        ".Lcc_4a1518: je .Lcc_4a152d\n"
        ".Lcc_4a151a: jle .Lcc_4a1538\n"
        ".Lcc_4a151c: cmp eax, 0x3\n"
        ".Lcc_4a151f: jg .Lcc_4a1538\n"
        ".Lcc_4a1521: call crtc_errno\n"
        ".Lcc_4a1526: mov dword ptr [eax], 0x22\n"
        ".Lcc_4a152c: ret\n"
        ".Lcc_4a152d: call crtc_errno\n"
        ".Lcc_4a1532: mov dword ptr [eax], 0x21\n"
        ".Lcc_4a1538: ret\n"
        /* 0x4a1539 get_fname */
        ".Lcc_4a1539: xor ecx, ecx\n"
        ".Lcc_4a153b: mov eax, 0x4b5870\n"
        ".Lcc_4a1540: mov edx, dword ptr [eax]\n"
        ".Lcc_4a1542: cmp edx, dword ptr [esp + 0x4]\n"
        ".Lcc_4a1546: je .Lcc_4a1556\n"
        ".Lcc_4a1548: add eax, 0x8\n"
        ".Lcc_4a154b: inc ecx\n"
        ".Lcc_4a154c: cmp eax, 0x4b5948\n"
        ".Lcc_4a1551: jl .Lcc_4a1540\n"
        ".Lcc_4a1553: xor eax, eax\n"
        ".Lcc_4a1555: ret\n"
        ".Lcc_4a1556: mov eax, dword ptr [8*ecx + 0x4b5874]\n"
        ".Lcc_4a155d: ret\n"
        /* 0x4a155e errcode */
        ".Lcc_4a155e: mov al, byte ptr [esp + 0x4]\n"
        ".Lcc_4a1562: test al, 0x20\n"
        ".Lcc_4a1564: je .Lcc_4a156a\n"
        ".Lcc_4a1566: push 0x5\n"
        ".Lcc_4a1568: jmp .Lcc_4a1580\n"
        ".Lcc_4a156a: test al, 0x8\n"
        ".Lcc_4a156c: je .Lcc_4a1572\n"
        ".Lcc_4a156e: push 0x1\n"
        ".Lcc_4a1570: jmp .Lcc_4a1580\n"
        ".Lcc_4a1572: test al, 0x4\n"
        ".Lcc_4a1574: je .Lcc_4a157a\n"
        ".Lcc_4a1576: push 0x2\n"
        ".Lcc_4a1578: jmp .Lcc_4a1580\n"
        ".Lcc_4a157a: test al, 0x1\n"
        ".Lcc_4a157c: je .Lcc_4a1582\n"
        ".Lcc_4a157e: push 0x3\n"
        ".Lcc_4a1580: pop eax\n"
        ".Lcc_4a1581: ret\n"
        ".Lcc_4a1582: movzx eax, al\n"
        ".Lcc_4a1585: and eax, 0x2\n"
        ".Lcc_4a1588: shl eax, 1\n"
        ".Lcc_4a158a: ret\n"
        /* 0x4a158b matherr */
        ".Lcc_4a158b: xor eax, eax\n"
        ".Lcc_4a158d: ret\n"
        /* 0x4a158e statfp */
        ".Lcc_4a158e: push ebp\n"
        ".Lcc_4a158f: mov ebp, esp\n"
        ".Lcc_4a1591: push ecx\n"
        ".Lcc_4a1592: fwait\n"
        ".Lcc_4a1593: fnstsw word ptr [ebp - 0x2]\n"
        ".Lcc_4a1596: movsx eax, word ptr [ebp - 0x2]\n"
        ".Lcc_4a159a: leave\n"
        ".Lcc_4a159b: ret\n"
        /* 0x4a159c clrfp */
        ".Lcc_4a159c: push ebp\n"
        ".Lcc_4a159d: mov ebp, esp\n"
        ".Lcc_4a159f: push ecx\n"
        ".Lcc_4a15a0: fnstsw word ptr [ebp - 0x2]\n"
        ".Lcc_4a15a3: fnclex\n"
        ".Lcc_4a15a5: movsx eax, word ptr [ebp - 0x2]\n"
        ".Lcc_4a15a9: leave\n"
        ".Lcc_4a15aa: ret\n"
        /* 0x4a15ab ctrlfp */
        ".Lcc_4a15ab: push ebp\n"
        ".Lcc_4a15ac: mov ebp, esp\n"
        ".Lcc_4a15ae: push ecx\n"
        ".Lcc_4a15af: fwait\n"
        ".Lcc_4a15b0: fnstcw word ptr [ebp - 0x4]\n"
        ".Lcc_4a15b3: mov eax, dword ptr [ebp + 0xc]\n"
        ".Lcc_4a15b6: mov ecx, eax\n"
        ".Lcc_4a15b8: and eax, dword ptr [ebp + 0x8]\n"
        ".Lcc_4a15bb: not ecx\n"
        ".Lcc_4a15bd: and ecx, dword ptr [ebp - 0x4]\n"
        ".Lcc_4a15c0: or ecx, eax\n"
        ".Lcc_4a15c2: mov dword ptr [ebp + 0xc], ecx\n"
        ".Lcc_4a15c5: fldcw word ptr [ebp + 0xc]\n"
        ".Lcc_4a15c8: movsx eax, word ptr [ebp - 0x4]\n"
        ".Lcc_4a15cc: leave\n"
        ".Lcc_4a15cd: ret\n"
        /* 0x4a15ce set_statfp */
        ".Lcc_4a15ce: push ebp\n"
        ".Lcc_4a15cf: mov ebp, esp\n"
        ".Lcc_4a15d1: push ecx\n"
        ".Lcc_4a15d2: push ecx\n"
        ".Lcc_4a15d3: mov cl, byte ptr [ebp + 0x8]\n"
        ".Lcc_4a15d6: test cl, 0x1\n"
        ".Lcc_4a15d9: je .Lcc_4a15e5\n"
        ".Lcc_4a15db: fld tbyte ptr [0x4b594c]\n"
        ".Lcc_4a15e1: fistp dword ptr [ebp + 0x8]\n"
        ".Lcc_4a15e4: fwait\n"
        ".Lcc_4a15e5: test cl, 0x8\n"
        ".Lcc_4a15e8: je .Lcc_4a15fa\n"
        ".Lcc_4a15ea: fwait\n"
        ".Lcc_4a15eb: fnstsw ax\n"
        ".Lcc_4a15ed: fld tbyte ptr [0x4b594c]\n"
        ".Lcc_4a15f3: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_4a15f6: fwait\n"
        ".Lcc_4a15f7: fwait\n"
        ".Lcc_4a15f8: fnstsw ax\n"
        ".Lcc_4a15fa: test cl, 0x10\n"
        ".Lcc_4a15fd: je .Lcc_4a1609\n"
        ".Lcc_4a15ff: fld tbyte ptr [0x4b5958]\n"
        ".Lcc_4a1605: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_4a1608: fwait\n"
        ".Lcc_4a1609: test cl, 0x4\n"
        ".Lcc_4a160c: je .Lcc_4a1617\n"
        ".Lcc_4a160e: fldz\n"
        ".Lcc_4a1610: fld1\n"
        ".Lcc_4a1612: .byte 0xde, 0xf1  /* fdivrp st(1), st */\n"
        ".Lcc_4a1614: fstp st(0)\n"
        ".Lcc_4a1616: fwait\n"
        ".Lcc_4a1617: test cl, 0x20\n"
        ".Lcc_4a161a: je .Lcc_4a1622\n"
        ".Lcc_4a161c: fldpi\n"
        ".Lcc_4a161e: fstp qword ptr [ebp - 0x8]\n"
        ".Lcc_4a1621: fwait\n"
        ".Lcc_4a1622: leave\n"
        ".Lcc_4a1623: ret\n"

        ".section .rodata\n"
        ".p2align 2\n"
        "crtc_fmod_jump:\n"
        ".long .Lcc_4987f4\n"
        ".long .Lcc_49a716\n"
        ".long .Lcc_49a6af\n"
        ".long .Lcc_49a716\n"
        ".long .Lcc_49a674\n"
        ".long .Lcc_49a716\n"
        ".long .Lcc_49a6af\n"
        ".long .Lcc_49a716\n"
        ".long .Lcc_49a6ad\n"
        ".long .Lcc_49a6ad\n"
        ".long .Lcc_49a6d7\n"
        ".long .Lcc_49a6ad\n"
        ".long .Lcc_49a716\n"
        ".long .Lcc_49a716\n"
        ".long .Lcc_49a6af\n"
        ".long .Lcc_49a716\n"
        "crtc_atan2_jump:\n"
        ".long .Lcc_49ce1c\n"
        ".long .Lcc_49ce92\n"
        ".long .Lcc_49a6af\n"
        ".long .Lcc_49ce75\n"
        ".long .Lcc_49ce75\n"
        ".long .Lcc_49ce75\n"
        ".long .Lcc_49a6af\n"
        ".long .Lcc_49ce75\n"
        ".long .Lcc_49a6ad\n"
        ".long .Lcc_49a6ad\n"
        ".long .Lcc_49a6d7\n"
        ".long .Lcc_49a6ad\n"
        ".long .Lcc_49ce92\n"
        ".long .Lcc_49ce92\n"
        ".long .Lcc_49a6af\n"
        ".long .Lcc_49a716\n"
        ".text\n"
        ".att_syntax prefix\n");
