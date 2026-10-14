/* C runtime area, group CA: the game's library code below 0x497000 and the
 * runtime entries at 0x4a1c36..0x4a1ea4.
 *
 *  - the debug log (0x496260-0x496410): "Mono debugger" text file, path at
 *    0x4b4848, FILE * at 0x4db1c8, printf-style writers;
 *  - the data archive (0x496430-0x496680): ROLLCAGE.IDX read into a table
 *    of 16-byte records, entries read from ROLLCAGE.IMG (closest copy to the
 *    current file position, optionally GT20-packed);
 *  - the CD audio player (0x496690-0x496f30 and its internals up to
 *    0x497850): MCI commands, track choice (shuffle), CD volume through the
 *    aux devices and the mixer's CD line;
 *  - strlwr / strupr (0x4a1c3c / 0x4a1d70), _hypot (0x4a1ea4) and the
 *    DirectSoundCreate import thunk (0x4a1c36).
 *
 * Called by address (left to group CB or the runtime): fopen 0x499459,
 * fread 0x49908e, fseek 0x4991a5, fclose 0x4994f9, fputs 0x4996dc, fflush
 * 0x499576, _vsnprintf 0x49972a (the FILE family and the printf engine
 * share state with CB's functions), malloc 0x4982c2, free 0x49827a, strcpy
 * 0x498450, sqrt 0x4981d4, gt20_decompress 0x497860, and in paths the game
 * never takes: the locale lock / LCMapString (strlwr / strupr with a locale
 * set) and _hypot's error reporting (_except 0x4a0f1a, 0x4a0eba).
 * Windows calls by name (winapi.h). */
#include "../runtime.h"
#include "physics.h"
#include "winapi.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(p) + (off)))
#define PTR(p, off) ((uint8_t *)(uintptr_t)U32(p, off)) /* 32-bit pointers in game memory */
#define A(p) ((uint32_t)(uintptr_t)(p))
#define G16(a) (*(uint16_t *)(a))
#define GS16(a) (*(int16_t *)(a))
#define G32(a) (*(uint32_t *)(a))
#define GI32(a) (*(int32_t *)(a))
#define GPTR(a) GP(a)

/* ------------------------------------------------------------ callees */

typedef double(GAMEAPI *sqrt_fn)(double x);
typedef double(GAMEAPI *except_fn)(int32_t flags, int32_t op, double a, double b, double result, int32_t cw);
typedef double(GAMEAPI *qnan_fn)(int32_t op, double a, double b, int32_t cw);

#define crt_fopen GCALL(0x499459, crtc_fopen)
#define crt_fread GCALL(0x49908e, crtc_fread)
#define crt_fseek GCALL(0x4991a5, crtc_fseek)
#define crt_fclose GCALL(0x4994f9, crtc_fclose)
#define crt_fputs GCALL(0x4996dc, crtc_fputs)
#define crt_fflush GCALL(0x499576, crtc_fflush)
#define crt_vsnprintf GCALL(0x49972a, msvc_vsnprintf)
#define crt_malloc GCALL(0x4982c2, msvc_malloc)
#define crt_free GCALL(0x49827a, msvc_free)
#define crt_strcpy GCALL(0x498450, msvc_strcpy)
#define gt20_decompress_orig GCALL(0x497860, gt20_decompress)
/* asm entries (crt_cb.c / crt_cc.c): the original's stack arguments, x87
 * result; i386 only (see crt_hypot) */
#define crt_sqrt ((sqrt_fn)0x4981d4)
#define crt_except ((except_fn)0x4a0f1a)
#define crt_handle_qnan ((qnan_fn)0x4a0eba)
#define crt_lock GCALL(0x49b161, crtc_lock)
#define crt_unlock GCALL(0x49b1c2, crtc_unlock)
#define crt_lcmap GCALL(0x49ba3b, crtc_lcmap_string)


/* ------------------------------------------------------------ debug log */

/* strcpy / strcat as the original's inline rep movs (no runtime calls). */
static void str_copy(char *dst, const char *src)
{
    while ((*dst++ = *src++) != 0)
        ;
}

static void str_append(char *dst, const char *src)
{
    while (*dst)
        dst++;
    str_copy(dst, src);
}

#define g_log_enabled GI32(0x4b494c)
#define g_log_banner GI32(0x4b4950) /* print the session banner on open */
#define g_log_append GI32(0x4db1cc) /* open with "at" instead of "wt" */
#define g_log_file GPTR(0x4db1c8)
#define g_log_file_set(fp) (G32(0x4db1c8) = A(fp))
#define LOG_PATH ((char *)0x4b4848) /* 0x104 bytes */

/* 0x496410: close the log file. */
void GAMEAPI debug_log_close(void)
{
    if (g_log_file) {
        crt_fclose(g_log_file);
        g_log_file_set(0);
    }
}

/* 0x4962c0: set the log path (closing an open log first). */
void GAMEAPI debug_log_set_path(const char *path)
{
    if (g_log_file)
        debug_log_close();
    if (path)
        str_copy(LOG_PATH, path); /* unbounded, as the original */
}

void GAMEAPI debug_log_printf(const char *fmt, ...);

/* 0x496260: open the log file if logging is on ("wt", or "at" when
 * appending) and write the session banner (0x4b4a5c). */
static void debug_log_open(void)
{
    if (!g_log_enabled)
        return;
    if (!g_log_file) {
        g_log_file_set(crt_fopen(LOG_PATH, (const char *)(uintptr_t)(g_log_append ? 0x4b4a7c : 0x4b4a78)));
        if (!g_log_file)
            return;
    }
    if (g_log_banner)
        debug_log_printf((const char *)0x4b4a5c);
}

/* 0x496300: write a string to the log (opening it on first use), flushed. */
static void debug_log_write(const char *text)
{
    if (!g_log_enabled)
        return;
    if (!g_log_file) {
        debug_log_open();
        if (!g_log_file)
            return;
    }
    crt_fputs(text, g_log_file);
    crt_fflush(g_log_file);
}

#if defined(__i386__)
/* the arguments as the C runtime's printf engine reads them: the stack words */
#define LOG_ARGS(fmt, ap, words) ((void)(words), (const uint8_t *)(ap))
#else
/* x86-64: printf arguments (by the format, as the C runtime reads them) into
 * 32-bit stack words: integers, characters and pointers one word, doubles
 * and I64 integers two (orig_pp.c's pp_args_pack). */
static const uint8_t *log_args_pack(const char *fmt, __builtin_va_list ap, uint32_t *words, uint32_t max)
{
    uint32_t n = 0;

    while (*fmt) {
        int i64 = 0;

        if (*fmt++ != '%')
            continue;
        if (*fmt == '%') {
            fmt++;
            continue;
        }
        for (;; fmt++) {
            char c = *fmt;

            if (c == '*') {
                if (n < max)
                    words[n++] = (uint32_t)__builtin_va_arg(ap, int);
            } else if (c == 'I' && fmt[1] == '6' && fmt[2] == '4') {
                i64 = 1;
                fmt += 2;
            } else if (!(c == '-' || c == '+' || c == ' ' || c == '#' || c == '.' || (c >= '0' && c <= '9') ||
                         c == 'h' || c == 'l' || c == 'L' || c == 'w' || c == 'N' || c == 'F')) {
                break;
            }
        }
        if (!*fmt)
            break;
        switch (*fmt++) {
        case 'e': case 'E': case 'f': case 'g': case 'G': {
            double d = __builtin_va_arg(ap, double);
            uint64_t q;

            memcpy(&q, &d, 8);
            if (n + 1 < max) {
                words[n++] = (uint32_t)q;
                words[n++] = (uint32_t)(q >> 32);
            }
            break;
        }
        case 's': case 'S': case 'p': case 'n': case 'Z':
            if (n < max)
                words[n++] = A(__builtin_va_arg(ap, void *));
            break;
        default:
            if (i64) {
                uint64_t q = __builtin_va_arg(ap, uint64_t);

                if (n + 1 < max) {
                    words[n++] = (uint32_t)q;
                    words[n++] = (uint32_t)(q >> 32);
                }
            } else if (n < max) {
                words[n++] = (uint32_t)__builtin_va_arg(ap, int);
            }
            break;
        }
    }
    return (const uint8_t *)words;
}
#define LOG_ARGS(fmt, ap, words) log_args_pack(fmt, ap, words, 64)
#endif

/* 0x496340: printf to the log with a newline (0x4b4a80) appended; at most
 * 0x7ce characters of formatted text. */
void GAMEAPI debug_log_printf(const char *fmt, ...)
{
    char buf[0x7d0];
    __builtin_va_list ap;
    uint32_t words[64];

    if (!g_log_enabled)
        return;
    __builtin_va_start(ap, fmt);
    crt_vsnprintf(buf, 0x7ce, fmt, LOG_ARGS(fmt, ap, words));
    __builtin_va_end(ap);
    buf[0x7ce] = 0;
    str_append(buf, (const char *)0x4b4a80);
    debug_log_write(buf);
}

/* 0x4963c0: printf to the log as is (at most 0x7cf characters). */
void GAMEAPI debug_log_print(const char *fmt, ...)
{
    char buf[0x7d0];
    __builtin_va_list ap;
    uint32_t words[64];

    if (!g_log_enabled)
        return;
    __builtin_va_start(ap, fmt);
    crt_vsnprintf(buf, 0x7cf, fmt, LOG_ARGS(fmt, ap, words));
    __builtin_va_end(ap);
    buf[0x7cf] = 0;
    debug_log_write(buf);
}

/* ------------------------------------------------------------ data archive */

#define ARCHIVE_IMG ((char *)0x6d0ee0) /* the .IMG path */
#define g_archive_count GS16(0x6d0f04)
#define g_archive_index GPTR(0x6d0f08) /* records of 16 bytes */
#define g_archive_index_set(p) (G32(0x6d0f08) = A(p))
#define g_archive_pos GI32(0x6d0f00)   /* file position after the last read */
#define g_archive_file GPTR(0x4db1dc)
#define g_archive_file_set(fp) (G32(0x4db1dc) = A(fp))
#define g_archive_closed GS16(0x4b4a8c) /* 0: the .IMG is open */
/* allocator pointers: msvc_malloc / msvc_free, or kr_malloc / kr_free
 * (orig_pg.c) */
typedef uint32_t(GAMEAPI *alloc_fn)(uint32_t size);
typedef void(GAMEAPI *release_fn)(void *p);
#define g_alloc(n) ((uint8_t *)(uintptr_t)((alloc_fn)(uintptr_t)G32(0x4b4a84))(n))
#define g_free ((release_fn)(uintptr_t)G32(0x4b4a88))

/* strrchr (0x499780, only used here). */
static char *last_char(char *s, char c)
{
    char *found = 0;

    for (;; s++) {
        if (*s == c)
            found = s;
        if (!*s)
            return found;
    }
}

/* 0x496510: open (0) or close (nonzero) the .IMG; the state is a word.
 * Opening resets the position; the fopen result is not checked. */
void GAMEAPI archive_select(int32_t closed)
{
    int16_t c = (int16_t)closed;

    if (c == g_archive_closed)
        return;
    if (c == 0) {
        void *fp = crt_fopen(ARCHIVE_IMG, (const char *)0x4b3068);
        g_archive_closed = c;
        g_archive_file_set(fp);
        g_archive_pos = 0;
        return;
    }
    crt_fclose(g_archive_file);
    g_archive_closed = c;
}

/* 0x496430: open the archive: `path` names the .I?? file; the index
 * (.IDX, `count` records of 16 bytes) is read into a block from the
 * allocator pointer at 0x4b4a84, then the .IMG is opened. The original
 * copies the path to a 0x20-byte stack buffer (longer paths overran its
 * frame); this one is larger (sic). */
void GAMEAPI archive_open(const char *path, int32_t count)
{
    char idx[0x210]; /* the callers' paths are at most 0x204 bytes */
    char *dot;
    void *fp;

    str_copy(idx, path);
    str_copy(ARCHIVE_IMG, path);
    dot = last_char(ARCHIVE_IMG, '.');
    dot[2] = 'M';
    dot[3] = 'G';
    dot = last_char(idx, '.');
    dot[2] = 'D';
    dot[3] = 'X';
    g_archive_count = (int16_t)count;
    g_archive_index_set(g_alloc((uint32_t)((int32_t)(int16_t)count << 4)));
    fp = crt_fopen(idx, (const char *)0x4b3068);
    g_archive_file_set(fp);
    crt_fread(g_archive_index, 1, (uint32_t)((int32_t)g_archive_count << 4), fp);
    crt_fclose(g_archive_file);
    archive_select(0);
}

/* 0x496570: the index record of entry `index` (a word). */
uint8_t *GAMEAPI archive_get_record(int32_t index)
{
    return g_archive_index + ((int32_t)(int16_t)index << 4);
}

/* 0x496590: read entry `index` into `buf` (allocated when 0: unpacked size
 * + record +0xc + 7). An entry can be stored more than once, the copies
 * chained by record +0xe (0 ends); the copy closest to the current file
 * position is read. A packed entry (stored size +4 != unpacked +8) is read
 * to the end of the buffer (4-aligned) and unpacked in place. */
uint8_t *GAMEAPI archive_entry_load(int32_t index, uint8_t *buf)
{
    int32_t best = 0, best_dist = 0x7fffffff;
    uint8_t *rec, *dst;

    for (;;) {
        int32_t d;

        rec = archive_get_record(index);
        d = I32(rec, 0) - g_archive_pos;
        if (d < 0)
            d = -d;
        if (d < best_dist) {
            best_dist = d;
            best = index;
        }
        index = (int32_t)(((uint32_t)index & 0xffff0000u) | U16(rec, 0xe));
        if (!S16(rec, 0xe))
            break;
    }
    rec = archive_get_record(best);
    if (!buf)
        buf = g_alloc((uint32_t)(S16(rec, 0xc) + I32(rec, 8) + 7));
    dst = buf;
    if (U32(rec, 8) != U32(rec, 4))
        dst = (uint8_t *)(uintptr_t)(((uint32_t)(S16(rec, 0xc) - I32(rec, 4) + I32(rec, 8)) + A(buf) + 7) & ~3u);
    crt_fseek(g_archive_file, I32(rec, 0), 0);
    crt_fread(dst, 1, U32(rec, 4), g_archive_file);
    g_archive_pos = I32(rec, 4) + I32(rec, 0);
    if (U32(rec, 8) != U32(rec, 4))
        gt20_decompress_orig(dst, buf);
    return buf;
}

/* 0x496660: free an archive block (allocator pointer 0x4b4a88). */
void GAMEAPI archive_free(void *block)
{
    if (block)
        g_free(block);
}

/* 0x496680: the .IMG state word (0 open). The original leaves the upper
 * half of eax as it was; callers use the word. */
int32_t GAMEAPI archive_state(void)
{
    return (int32_t)(uint32_t)G16(0x4b4a8c);
}

/* ------------------------------------------------------------ CD audio */

#define g_cd_state GI32(0x4db250) /* 0 closed, 1 stopped, 2 playing, 3 paused */
#define g_cd_busy GI32(0x4db254)
#define MCI_OPEN_PARMS ((uint8_t *)0x4db220) /* +4 device id, +8 device type */
#define g_cd_device G32(0x4db224)
#define MCI_GENERIC_PARMS ((uint8_t *)0x4db234)
#define MCI_STATUS_PARMS ((uint8_t *)0x4db240) /* +4 return, +8 item, +0xc track */
#define MCI_SET_PARMS ((uint8_t *)0x4db1f8)    /* +4 time format */
#define MCI_PLAY_PARMS ((uint8_t *)0x4db210)   /* +4 from, +8 to */
#define g_cd_paused_at G32(0x4db1e8)
#define g_cd_tracks GI32(0x4db238)
#define g_cd_request GI32(0x4b4a90) /* the track asked for */
#define g_cd_track GI32(0x4b4a94)   /* the current track */
#define g_cd_shuffle GI32(0x4db258) /* 0 the requested track, 1 shuffle at start, 2 always */
#define g_cd_skip_first GI32(0x4db25c) /* shuffle never picks track 1 (0 -> 1) */
#define g_cd_played G32(0x4db260)   /* shuffle: tracks played, bit per track - 1 */
#define g_cd_seed_time GI32(0x4b4a98)
#define g_cd_seed GI32(0x4db264)

/* aux devices (CD audio outputs): table of 12-byte entries
 * (+0 saved volume, +4 device, +8 dwSupport). */
#define g_aux_scanned GI32(0x4db268)
#define g_aux_count GI32(0x4db26c)
#define g_aux_table GPTR(0x4db270)
#define g_aux_table_set(p) (G32(0x4db270) = A(p))

/* mixer CD volume controls: list of 0x50-byte nodes (layout in ca_formats.txt). */
#define g_mixer_inited GI32(0x4db284)
#define g_mixer_head GPTR(0x4db278)
#define g_mixer_tail GPTR(0x4db27c)
#define g_mixer_head_set(p) (G32(0x4db278) = A(p))
#define g_mixer_tail_set(p) (G32(0x4db27c) = A(p))
#define g_mixer_count G32(0x4db280)
#define g_mixer_found GI32(0x4db1f0)
#define g_mixer_keep GI32(0x4db1ec)  /* keep the controls found (mode 0x271a) */
#define g_mixer_query GI32(0x4db208) /* stop at the first control (modes 0x2724 / 0x272e) */
#define g_mixer_volume G32(0x4db204) /* first control's volume scaled to 0..0xffff */

#define mci(cmd, flags, params) mciSendCommandA(g_cd_device, (cmd), (flags), (DWORD)(uintptr_t)(params))

/* 0x4966b0: open the CD audio device (MCI_OPEN, type id 0x204): 0 or -1. */
int32_t GAMEAPI cdaudio_device_open(void)
{
    uint32_t r;

    g_cd_busy = 1;
    U32(MCI_OPEN_PARMS, 8) = 0x204;
    r = mciSendCommandA(0, 0x803, 0x3100, (DWORD)(uintptr_t)MCI_OPEN_PARMS);
    g_cd_busy = 0;
    return r ? -1 : 0;
}

/* 0x496730: close the CD audio device: 0 or -1. */
int32_t GAMEAPI cdaudio_device_close(void)
{
    uint32_t r;

    g_cd_busy = 1;
    r = mci(0x804, 0, 0);
    g_cd_busy = 0;
    return r ? -1 : 0;
}

/* 0x496690: open the CD player if closed. */
void GAMEAPI cdaudio_open(void)
{
    if (!g_cd_state && cdaudio_device_open() != -1)
        g_cd_state = 1;
}

/* 0x496be0: the shuffle's random numbers: seed = (seed + 1234) * 16807 mod
 * 2^31, seeded from timeGetTime on first use; returns (seed >> 16) % n, or
 * the whole seed for n == -1 (n == 0: seed >> 16). */
int32_t GAMEAPI cdaudio_random(int32_t n)
{
    int32_t seed, r;

    if (g_cd_seed_time) {
        g_cd_seed_time = 0;
        seed = (int32_t)(timeGetTime() & 0x7fffffff);
    } else {
        seed = g_cd_seed;
    }
    seed = (int32_t)(((uint32_t)seed + 0x4d2) * 16807u & 0x7fffffff);
    g_cd_seed = seed;
    r = seed >> 16;
    if (n)
        r = r % n;
    if (n == -1)
        r = seed;
    return r;
}

/* Step a 0-based track: wraps at `limit` to 0, and 0 becomes 1 when track 1
 * is skipped. */
static int32_t shuffle_step(int32_t c, int32_t limit, int32_t skip_first)
{
    c++;
    if (c >= limit)
        c = 0;
    else if (c != 0)
        return c;
    if (skip_first)
        c = 1;
    return c;
}

/* 0x496a90: choose the track to play (`next`: the one after the current
 * one ended). No tracks: 1. Paused: the current one. Shuffle off: the
 * requested one. Shuffle (mode 2, or mode 1 when not `next`): a random
 * unplayed track among the first 32. Out of range: wrapped into 1..tracks.
 * Returns the track, also kept in 0x4b4a94. */
int32_t GAMEAPI cdaudio_choose_track(int32_t next)
{
    int32_t n = g_cd_tracks, c;

    if (n <= 0) {
        g_cd_track = 1;
        return 1;
    }
    if (g_cd_state == 3) {
        c = g_cd_track;
    } else if (!g_cd_shuffle) {
        c = g_cd_request;
        g_cd_track = c;
    } else if (next && g_cd_shuffle != 2) {
        c = g_cd_track;
    } else {
        int32_t limit = n > 0x20 ? 0x20 : n, k, skip, i;
        uint32_t played;

        g_cd_track = cdaudio_random(limit);
        if (!g_cd_track && g_cd_skip_first)
            g_cd_track = 1;
        k = cdaudio_random(0x7f);
        skip = g_cd_skip_first;
        c = g_cd_track;
        played = g_cd_played;
        for (;;) {
            /* the next unplayed track from c (all played: start again) */
            for (i = 0; i < 0x21; i++) {
                if (i == 0x20)
                    played = 0;
                if (!(played & (1u << (c & 31))))
                    break;
                c = shuffle_step(c, limit, skip);
            }
            if (!k)
                break;
            c = shuffle_step(c, limit, skip);
            k--;
        }
        played |= 1u << (c & 31);
        c++;
        g_cd_played = played;
        n = g_cd_tracks;
        g_cd_track = c;
    }
    if (c >= 1 && c <= n)
        return c;
    c = c % n + 1;
    g_cd_track = c;
    if (c < 1) {
        c = 1;
        g_cd_track = 1;
    }
    if (c > n) {
        g_cd_track = n;
        return n;
    }
    return c;
}

/* 0x496920: play (stopped or paused): read the track count, choose the
 * track (0x496a90), set TMSF time, read its length and play from the
 * track start (or the paused position) to its end. Failures close the
 * player (state 0). */
void GAMEAPI cdaudio_play_track(int32_t next)
{
    int32_t track;
    uint32_t len;

    if (g_cd_state != 1 && g_cd_state != 3)
        return;
    g_cd_busy = 1;
    U32(MCI_STATUS_PARMS, 8) = 3;
    if (mci(0x814, 0x102, MCI_STATUS_PARMS)) {
        g_cd_state = 0;
        g_cd_busy = 0;
        g_cd_tracks = -1;
        return;
    }
    g_cd_tracks = I32(MCI_STATUS_PARMS, 4);
    track = cdaudio_choose_track(next);
    U32(MCI_SET_PARMS, 4) = 10;
    if (mci(0x80d, 0x400, MCI_SET_PARMS)) {
        g_cd_state = 0;
        g_cd_busy = 0;
        return;
    }
    U32(MCI_STATUS_PARMS, 0) = 0;
    U32(MCI_STATUS_PARMS, 4) = 0;
    U32(MCI_STATUS_PARMS, 8) = 1;
    U32(MCI_STATUS_PARMS, 0xc) = (uint32_t)track;
    if (mci(0x814, 0x110, MCI_STATUS_PARMS)) {
        len = 0;
        U32(MCI_STATUS_PARMS, 4) = 0;
    } else {
        len = U32(MCI_STATUS_PARMS, 4);
    }
    if (g_cd_state == 3)
        U32(MCI_PLAY_PARMS, 4) = g_cd_paused_at;
    else
        U32(MCI_PLAY_PARMS, 4) = (uint32_t)track & 0xff;
    U32(MCI_PLAY_PARMS, 8) = len << 8 | (uint32_t)track;
    U32(MCI_PLAY_PARMS, 0) = 0;
    len = mci(0x806, 0xc, MCI_PLAY_PARMS);
    g_cd_busy = 0;
    g_cd_state = len ? 1 : 2;
}

/* 0x4977e0 (internals 0x497850 / 0x4977f0) and the mixer, below. */
static void cdaudio_volume_restore(void);

/* 0x4967a0: stop playing (or paused) and reopen the archive. */
void GAMEAPI cdaudio_stop(void)
{
    if (g_cd_state == 2 || g_cd_state == 3) {
        uint32_t dev = g_cd_device;

        g_cd_busy = 1;
        mciSendCommandA(dev, 0x808, 0, (DWORD)(uintptr_t)MCI_GENERIC_PARMS);
        g_cd_state = 1;
        g_cd_busy = 0;
    }
    archive_select(0);
}

/* 0x496700: close the player: stop, restore the CD volumes, close the
 * device. */
void GAMEAPI cdaudio_close(void)
{
    cdaudio_stop();
    cdaudio_volume_restore();
    if (g_cd_state)
        cdaudio_device_close();
    g_cd_state = 0;
}

/* 0x496770: play `track` (closes the archive: same disc). */
void GAMEAPI cdaudio_play(int32_t track)
{
    archive_select(1);
    cdaudio_open();
    g_cd_request = track;
    g_cd_state = 1;
    cdaudio_play_track(0);
}

/* 0x4967f0: pause: remember the position (TMSF) and stop. */
void GAMEAPI cdaudio_pause(void)
{
    uint32_t r;

    if (g_cd_state != 2)
        return;
    g_cd_busy = 1;
    U32(MCI_SET_PARMS, 4) = 10;
    mci(0x80d, 0x400, MCI_SET_PARMS);
    U32(MCI_STATUS_PARMS, 8) = 2;
    r = mci(0x814, 0x102, MCI_STATUS_PARMS);
    g_cd_paused_at = r ? 0 : U32(MCI_STATUS_PARMS, 4);
    mci(0x808, 0, MCI_GENERIC_PARMS);
    g_cd_state = 3;
    g_cd_busy = 0;
}

/* 0x496890: resume after a pause. */
void GAMEAPI cdaudio_resume(void)
{
    if (g_cd_state == 3)
        cdaudio_play_track(0);
}

/* 0x4968b0: per-frame check while playing: the track ended (mode stopped)
 * -> the next one; not ready / door open -> reopen the device first. */
void GAMEAPI cdaudio_poll(void)
{
    uint32_t mode;

    if (g_cd_busy || g_cd_state != 2)
        return;
    U32(MCI_STATUS_PARMS, 8) = 4;
    if (mci(0x814, 0x102, MCI_STATUS_PARMS))
        return;
    mode = U32(MCI_STATUS_PARMS, 4);
    if (mode != 0x20c && mode != 0x20d && mode != 0x212)
        return;
    if (mode != 0x20d) {
        cdaudio_device_close();
        cdaudio_device_open();
    }
    g_cd_state = 1;
    cdaudio_play_track(1);
}

/* 0x496c50: shuffle mode. */
void GAMEAPI cdaudio_set_shuffle(int32_t mode)
{
    g_cd_shuffle = mode;
}

/* 0x496c80: shuffle skips track 1. */
void GAMEAPI cdaudio_set_skip_first(int32_t skip)
{
    g_cd_skip_first = skip;
}

/* 0x496e50: aux device `dev`: -1 past the last device, 0 not a CD audio
 * output (or caps failed), 1 with its volume read into *volume, 2 no volume
 * (*volume = 0). volume 0: just 1 / 2 by the volume capability. */
int32_t GAMEAPI aux_query(uint32_t dev, uint8_t *caps, uint32_t *volume)
{
    if (dev >= auxGetNumDevs())
        return -1;
    if (auxGetDevCapsA(dev, caps, 0x30))
        return 0;
    if (U16(caps, 0x28) != 1)
        return 0;
    if (!volume)
        return (U8(caps, 0x2c) & 1) ? 1 : 2;
    if ((U8(caps, 0x2c) & 1) && auxGetVolume(dev, volume) == 0)
        return 1;
    *volume = 0;
    return 2;
}

/* 0x496ed0: drop the aux table. */
void GAMEAPI aux_free(void)
{
    if (g_aux_table)
        crt_free(g_aux_table);
    g_aux_table_set(0);
    g_aux_count = 0;
    g_aux_scanned = 0;
}

/* 0x496d70: once: the CD aux outputs with volume control and their volumes
 * (restored on close). The second pass may store one entry more than the
 * first counted (sic). */
void GAMEAPI aux_scan(void)
{
    uint8_t caps[0x30];
    uint32_t volume;
    uint32_t dev;
    int32_t found = 0, r;

    if (g_aux_scanned)
        return;
    aux_free();
    for (dev = 0;; dev++) {
        r = aux_query(dev, caps, &volume);
        if (r == -1)
            break;
        if (r == 1)
            found++;
    }
    if (found) {
        g_aux_table_set(crt_malloc((uint32_t)found * 12));
        if (g_aux_table) {
            for (dev = 0;; dev++) {
                r = aux_query(dev, caps, &volume);
                if (r == -1)
                    break;
                if (r != 1)
                    continue;
                if (g_aux_count > found)
                    break;
                U32(g_aux_table, g_aux_count * 12 + 4) = dev;
                U32(g_aux_table, g_aux_count * 12) = volume;
                U32(g_aux_table, g_aux_count * 12 + 8) = U32(caps, 0x2c);
                g_aux_count++;
            }
        }
    }
    g_aux_scanned = 1;
}

/* 0x496d00: set the CD aux outputs' volume: left | right << 16 with
 * separate channels, else their average. */
void GAMEAPI aux_set_volume(int32_t left, int32_t right)
{
    int32_t i;

    aux_scan();
    if (!g_aux_table || g_aux_count <= 0)
        return;
    for (i = 0; i < g_aux_count; i++) {
        uint8_t *e = g_aux_table + i * 12;
        uint32_t v;

        if (U8(e, 8) & 2)
            v = (uint32_t)right << 16 | (uint32_t)left;
        else
            v = (uint32_t)((left + right) / 2);
        auxSetVolume(U32(e, 4), v);
    }
}

/* 0x497410: a mixer volume control on a CD line (`items` x `channels`
 * values): read its values; first one scaled to 0..0xffff into 0x4db204.
 * Kept in the list (with a copy of the values for the restore) when
 * 0x4db1ec; returns -1 when only querying (0x4db208), else 0. */
int32_t GAMEAPI mixer_add_control(uint32_t dev, uint32_t h, uint32_t dest, uint32_t src, uint32_t index,
                                  uint32_t channels, uint32_t items, uint8_t *ctl)
{
    uint32_t n = channels * items, bytes = n * 4, range;
    uint8_t *node, *values, *saved;

    if (!bytes)
        return 0;
    node = crt_malloc(0x50);
    values = crt_malloc(bytes);
    saved = crt_malloc(bytes);
    if (!node || !values || !saved) {
        if (node)
            crt_free(node);
        if (values)
            crt_free(values);
        if (saved)
            crt_free(saved);
        return 0;
    }
    memset(node, 0, 0x50);
    U32(node, 0x20) = 0x18;
    U32(node, 0x28) = channels;
    U32(node, 0x24) = U32(ctl, 4);
    U32(node, 0x2c) = U32(ctl, 0x10);
    U32(node, 0x30) = 4;
    U32(node, 0x34) = A(values);
    if (mixerGetControlDetailsA(GMEM(h), node + 0x20, 0)) {
        crt_free(node);
        crt_free(values);
        crt_free(saved);
        return 0;
    }
    g_mixer_found++;
    g_mixer_volume = 0;
    range = 0;
    if (U32(ctl, 0x68) > U32(ctl, 0x64)) {
        uint32_t v;

        range = U32(ctl, 0x68) - U32(ctl, 0x64);
        v = (U32(values, 0) * 0xffffu - U32(ctl, 0x64) * 0xffffu) / range;
        g_mixer_volume = v > 0xffff ? 0xffff : v;
    }
    if (!g_mixer_keep) {
        crt_free(node);
        crt_free(values);
        crt_free(saved);
        return g_mixer_query ? -1 : 0;
    }
    U32(node, 4) = dev;
    U32(node, 0) = 0;
    U32(node, 8) = dest;
    U32(node, 0xc) = src;
    U32(node, 0x10) = index;
    U32(node, 0x14) = U32(ctl, 0x64);
    U32(node, 0x44) = items;
    U32(node, 0x40) = channels;
    U32(node, 0x4c) = bytes;
    U32(node, 0x18) = U32(ctl, 0x68);
    U32(node, 0x1c) = range;
    U32(node, 0x38) = A(values);
    U32(node, 0x3c) = A(saved);
    U32(node, 0x48) = n;
    memcpy(saved, values, bytes);
    if (!g_mixer_head)
        g_mixer_head_set(node);
    else
        U32(g_mixer_tail, 0) = A(node);
    g_mixer_count++;
    g_mixer_tail_set(node);
    return g_mixer_query ? -1 : 0;
}

/* 0x497320: control `index` of a CD line: an enabled volume control
 * (0x50030001) with unsigned units -> 0x497410. */
int32_t GAMEAPI mixer_check_control(uint32_t dev, uint32_t h, uint32_t dest, uint32_t src, uint8_t *ctl,
                                    uint32_t index, uint32_t channels)
{
    uint32_t lc[6];
    uint8_t one[0x94];
    uint32_t items;

    if (U32(ctl, 0xc) & 0x80000000u)
        return 0;
    if (U32(ctl, 8) != 0x50030001)
        return 0;
    lc[0] = 0x18;
    lc[1] = 0; /* dwLineID: not set by the original (stack) */
    lc[2] = U32(ctl, 4);
    lc[3] = 0; /* cControls: not set by the original (stack) */
    lc[4] = 0x94;
    lc[5] = (uint32_t)(uintptr_t)one;
    if (mixerGetLineControlsA(GMEM(h), lc, 1))
        return 0;
    if ((U32(one, 8) & 0xf0000000u) != 0x50000000u)
        return 0;
    if (U8(one, 0xc) & 1)
        channels = 1;
    items = (U8(one, 0xc) & 2) ? U32(one, 0x10) : 1;
    return mixer_add_control(dev, h, dest, src, index, channels, items, one);
}

/* 0x4971f0: source `src` of a destination line: if it is the compact disc
 * (0x1005), each of its controls (0x497320). Returns -1 after looking at
 * any controls (sic: that ends the whole scan), else 0. */
int32_t GAMEAPI mixer_scan_source(uint32_t dev, uint32_t h, uint32_t dest, uint32_t src)
{
    uint8_t line[0xa8];
    uint32_t lc[6];
    uint8_t *controls, *c;
    uint32_t i;

    U32(line, 0) = 0xa8;
    U32(line, 4) = dest;
    U32(line, 8) = src;
    if (mixerGetLineInfoA(GMEM(h), line, 1))
        return 0;
    if (U32(line, 0x18) != 0x1005)
        return 0;
    if (!U32(line, 0x24))
        return 0;
    controls = crt_malloc(U32(line, 0x24) * 0x94);
    if (!controls)
        return 0;
    lc[0] = 0x18;
    lc[1] = U32(line, 0xc);
    lc[2] = 0; /* not set by the original (stack) */
    lc[3] = U32(line, 0x24);
    lc[4] = 0x94;
    lc[5] = (uint32_t)(uintptr_t)controls;
    if (!mixerGetLineControlsA(GMEM(h), lc, 0) && U32(line, 0x24)) {
        for (i = 0, c = controls; i < U32(line, 0x24); i++, c += 0x94)
            if (mixer_check_control(dev, h, dest, src, c, i, U32(line, 0x1c)) == -1)
                break;
    }
    crt_free(controls);
    return -1;
}

/* 0x497160: destination `dest`: speakers / headphones (4, 5) -> each
 * source (0x4971f0). -1 ends the scan. */
int32_t GAMEAPI mixer_scan_destination(uint32_t dev, uint32_t h, uint32_t dest)
{
    uint8_t line[0xa8];
    uint32_t i;

    U32(line, 0) = 0xa8;
    U32(line, 4) = dest;
    if (mixerGetLineInfoA(GMEM(h), line, 0))
        return 0;
    if (U32(line, 0x18) < 4 || U32(line, 0x18) > 5)
        return 0;
    for (i = 0; i < U32(line, 0x20); i++)
        if (mixer_scan_source(dev, h, dest, i) == -1)
            return -1;
    return 0;
}

/* 0x4970c0: mixer device `dev`: each destination (0x497160). */
int32_t GAMEAPI mixer_scan_device(uint32_t dev)
{
    uint8_t caps[0x30];
    uint32_t h, i;
    int32_t result = 0;

    if (mixerGetDevCapsA(dev, caps, 0x30))
        return 0;
    if (mixerOpen(&h, dev, 0, 0, 0))
        return 0;
    for (i = 0; i < U32(caps, 0x2c); i++) {
        if (mixer_scan_destination(dev, h, i) == -1) {
            result = -1;
            break;
        }
    }
    mixerClose(GMEM(h));
    return result;
}

/* 0x497040: scan the mixers for CD volume controls: 0x271a keeps them (for
 * setting / restoring), 0x2724 / 0x272e only count / read the first. -1 when
 * the scan stopped early (or an unknown mode), else 0. */
int32_t GAMEAPI mixer_scan(int32_t mode)
{
    uint32_t n, i;

    g_mixer_found = 0;
    g_mixer_volume = 0;
    if (mode == 0x271a) {
        g_mixer_keep = 1;
        g_mixer_query = 0;
    } else if (mode == 0x2724 || mode == 0x272e) {
        g_mixer_keep = 0;
        g_mixer_query = 1;
    } else {
        return -1;
    }
    n = mixerGetNumDevs();
    for (i = 0; i < n; i++)
        if (mixer_scan_device(i) == -1)
            return -1;
    return 0;
}

/* 0x496f30: set the kept mixer CD controls to (left + right) / 2 (scaled
 * into each control's range), or (restore) back to the saved values. A
 * mixer that fails to open is retried with the same node (sic). */
void GAMEAPI mixer_set_volume(int32_t restore, int32_t left, int32_t right)
{
    uint8_t *node;
    uint32_t i, h;

    if (!restore && !g_mixer_inited) { /* 0x497020 */
        g_mixer_inited = 1;
        mixer_scan(0x271a);
    }
    node = g_mixer_head;
    if (!g_mixer_count)
        return;
    for (i = 0; i < g_mixer_count; i++) {
        if (!node)
            return;
        if (mixerOpen(&h, U32(node, 4), 0, 0, 0))
            continue;
        if (restore) {
            memcpy(PTR(node, 0x38), PTR(node, 0x3c), U32(node, 0x4c));
        } else {
            uint32_t v = (uint32_t)((left + right) / 2) * U32(node, 0x1c);
            uint32_t k;

            v = (uint32_t)(((uint64_t)v * 0x80008001u) >> 47) + U32(node, 0x14);
            for (k = 0; k < U32(node, 0x48); k++)
                U32(PTR(node, 0x38), k * 4) = v;
        }
        mixerSetControlDetails(GMEM(h), node + 0x20, 0);
        mixerClose(GMEM(h));
        node = PTR(node, 0);
    }
}

/* 0x4977e0: restore the CD volumes on close: the mixer controls (0x497850)
 * and the aux outputs (0x4977f0), then drop the aux table. */
static void cdaudio_volume_restore(void)
{
    mixer_set_volume(1, 0, 0);
    if (g_aux_scanned) {
        if (g_aux_table && g_aux_count > 0) {
            int32_t i;

            for (i = 0; i < g_aux_count; i++)
                auxSetVolume(U32(g_aux_table, i * 12 + 4), U32(g_aux_table, i * 12));
        }
        aux_free();
    }
}

/* 0x496c90: CD volume (each 0..0xffff, clamped): mixer controls and aux
 * outputs. Only with the player open. */
void GAMEAPI cdaudio_set_volume(int32_t left, int32_t right)
{
    if (!g_cd_state)
        return;
    if (left > 0xffff)
        left = 0xffff;
    else if (left < 0)
        left = 0;
    if (right > 0xffff)
        right = 0xffff;
    else if (right < 0)
        right = 0;
    mixer_set_volume(0, left, right); /* 0x496f10 */
    aux_set_volume(left, right);
}

/* ------------------------------------------------------------ runtime */

/* 0x4a1c36: the DirectSoundCreate import thunk (dsound #1, stdcall with
 * three arguments): a jump to DirectSoundCreate (through the import slot in
 * the emulator build, as the original); it reads no arguments itself.
 * x86-64 (native only): no C calls 0x4a1c36 (orig_pv.c calls
 * DirectSoundCreate itself); the tail jump leaves the register arguments as
 * they are, so it stays a jump (a C body would need physics.h's void(void)
 * declaration changed to DirectSoundCreate's prototype). */
__attribute__((naked)) void crt_dsound_create(void)
{
#if defined(REROLL_NATIVE) || !defined(__i386__)
    __asm__ volatile("jmp %P0" : : "i"(DirectSoundCreate));
#else
    __asm__ volatile("jmp *0x4a3040");
#endif
}

#define g_lc_ctype G32(0x4db37c)          /* locale handle (0: "C"; setlocale is never called) */
#define g_setlc_active GI32(0x6d1248)
#define g_unguarded_readlc ((int32_t *)0x6d124c)

/* 0x4a1c3c / 0x4a1d70: strlwr / strupr. With a locale set the runtime maps
 * the string through LCMapString under its locale lock (kept as calls to
 * the runtime: the game never sets a locale). */
static char *str_case(char *s, uint32_t map, int upper)
{
    char *p, *buf = 0;
    int32_t locked, n;

    if (g_lc_ctype) {
        InterlockedIncrement(g_unguarded_readlc);
        if (g_setlc_active) {
            InterlockedDecrement(g_unguarded_readlc);
            crt_lock(0x13);
            locked = 1;
        } else {
            locked = 0;
        }
        if (g_lc_ctype) {
            n = crt_lcmap(g_lc_ctype, map, s, -1, 0, 0, 0, 1);
            if (n) {
                buf = crt_malloc((uint32_t)n);
                if (buf && crt_lcmap(g_lc_ctype, map, s, -1, buf, n, 0, 1))
                    crt_strcpy(s, buf);
            }
            if (locked)
                crt_unlock(0x13);
            else
                InterlockedDecrement(g_unguarded_readlc);
            crt_free(buf);
            return s;
        }
        if (locked)
            crt_unlock(0x13);
        else
            InterlockedDecrement(g_unguarded_readlc);
    }
    for (p = s; *p; p++) {
        if (upper) {
            if (*p >= 'a' && *p <= 'z')
                *p -= 0x20;
        } else if (*p >= 'A' && *p <= 'Z') {
            *p += 0x20;
        }
    }
    return s;
}

/* 0x4a1c3c: strlwr. */
char *GAMEAPI crt_strlwr(char *s)
{
    return str_case(s, 0x100, 0);
}

/* 0x4a1d70: strupr. */
char *GAMEAPI crt_strupr(char *s)
{
    return str_case(s, 0x200, 1);
}

/* x87 control word (_ctrlfp 0x4a15ab): set the bits of `mask` from `cw`,
 * return the old word (sign-extended). */
static int32_t fpu_control(uint32_t cw, uint32_t mask)
{
    uint16_t old, now;

    __asm__ volatile("fwait\n\tfnstcw %0" : "=m"(old) : : "memory");
    now = (uint16_t)((~mask & old) | (cw & mask));
    __asm__ volatile("fldcw %0" : : "m"(now) : "memory");
    return (int16_t)old;
}

/* A double through the x87 (fld / fstp: a signalling NaN comes out quiet),
 * as the original passes its arguments on. */
static double x87_copy(double x)
{
    double out;

    __asm__ volatile("fldl %1\n\tfstpl %0" : "=m"(out) : "m"(x));
    return out;
}

static uint16_t top_word(double x)
{
    uint64_t b;

    memcpy(&b, &x, 8);
    return (uint16_t)(b >> 48);
}

/* _set_exp 0x49fba5: the exponent field set to e + 0x3fe (bits 4-14 of the
 * top word; the sign and top mantissa bits kept). */
static double set_exponent(double x, int32_t e)
{
    uint64_t b;
    uint16_t w;

    memcpy(&b, &x, 8);
    w = (uint16_t)(((uint32_t)(e + 0x3fe) << 4) | ((uint32_t)(b >> 48) & 0x800f));
    b = (b & 0x0000ffffffffffffull) | (uint64_t)w << 48;
    memcpy(&x, &b, 8);
    return x;
}

/* _get_exp 0x49fbce. */
static int32_t get_exponent(double x)
{
    return (int16_t)(((top_word(x) >> 4) & 0x7ff) - 0x3fe);
}

/* _decomp 0x49fc3c: x = m * 2^e with m in [0.5, 1) (denormals normalised);
 * 0 -> 0, e 0. */
static double decompose(double x, int32_t *e)
{
    uint64_t b;
    uint32_t hi, lo;

    if (x == 0.0) {
        *e = 0;
        return 0.0;
    }
    memcpy(&b, &x, 8);
    hi = (uint32_t)(b >> 32);
    lo = (uint32_t)b;
    if (!(hi & 0x7ff00000) && ((hi & 0xfffff) || lo)) {
        int neg = x < 0.0;
        int32_t k = -0x3fd;

        while (!(hi & 0x00100000)) {
            hi = hi << 1 | lo >> 31;
            lo <<= 1;
            k--;
        }
        hi &= ~0x00100000u;
        if (neg)
            hi |= 0x80000000u;
        b = (uint64_t)hi << 32 | lo;
        memcpy(&x, &b, 8);
        *e = k;
        return set_exponent(x, 0);
    }
    *e = (int32_t)((hi >> 20) & 0x7ff) - 0x3fe;
    return set_exponent(x, 0);
}

#if !defined(__i386__)
/* x86-64: the runtime's asm entries above are i386 only. sqrt 0x4981d4 is
 * fsqrt at the caller's precision (here extended: _hypot's control word
 * 0x133f); the error reports (paths the game never takes) restore the
 * control word and return the masked result: the given value, or the NaN. */
#undef crt_sqrt
#undef crt_except
#undef crt_handle_qnan
static double crt_sqrt(double v)
{
    double r;

    __asm__ volatile("fldl %1\n\tfsqrt\n\tfstpl %0" : "=m"(r) : "m"(v));
    return r;
}

static int32_t fpu_control(uint32_t cw, uint32_t mask);

static double crt_except(int32_t flags, int32_t op, double a, double b, double result, int32_t cw)
{
    (void)flags, (void)op, (void)a, (void)b;
    fpu_control((uint32_t)cw, 0xffff);
    return result;
}

static double crt_handle_qnan(int32_t op, double a, double b, int32_t cw)
{
    volatile double r = (long double)a + (long double)b;

    (void)op;
    fpu_control((uint32_t)cw, 0xffff);
    return r;
}
#endif

/* 0x4a1ea4: _hypot(x, y) (0x4a1ec1 with op 0x18): at extended precision,
 * m = max(|x|, |y|), sqrt((x/m)^2 + (y/m)^2) * m with the exponents added
 * separately; every intermediate is stored as a double, as in the
 * original. Infinities give +inf, NaNs and over / underflow (and an
 * unmasked precision exception) go to the runtime's error reporting. */
double GAMEAPI crt_hypot(double x_in, double y_in)
{
    volatile double x = x_in, y = y_in, m, sum, root, frac, prod;
    int32_t cw = fpu_control(0x133f, 0xffff);
    uint16_t hx = top_word(x_in), hy = top_word(y_in);
    int32_t e1, e2, e;
    double r;

    if ((hx & 0x7ff0) == 0x7ff0 || (hy & 0x7ff0) == 0x7ff0) {
        uint64_t bx, by;

        memcpy(&bx, &x_in, 8);
        memcpy(&by, &y_in, 8);
        if (((hx & 0x7ff8) == 0x7ff0 && (bx & 0x0007ffffffffffffull)) ||
            ((hy & 0x7ff8) == 0x7ff0 && (by & 0x0007ffffffffffffull))) {
            volatile double s = (long double)x + (long double)y;

            return crt_except(8, 0x18, x87_copy(x), x87_copy(y), s, cw);
        }
        if ((hx & 0x7ff8) == 0x7ff8 || (hy & 0x7ff8) == 0x7ff8)
            return crt_handle_qnan(0x18, x87_copy(x), x87_copy(y), cw);
        fpu_control((uint32_t)cw, 0xffff);
        return *(const double *)0x4b5840;
    }
    if (x < 0.0)
        x = -x;
    if (y < 0.0)
        y = -y;
    m = y > x ? y : x;
    if (m == 0.0) {
        fpu_control((uint32_t)cw, 0xffff);
        return *(const double *)0x4a4d18;
    }
    x = (long double)x / (long double)m;
    y = (long double)y / (long double)m;
    sum = (long double)x * (long double)x + (long double)y * (long double)y;
    root = crt_sqrt(sum);
    frac = decompose(root, &e1);
    prod = (long double)decompose(m, &e2) * (long double)frac;
    e = get_exponent(prod) + e2 + e1;
    if (e > 0x400)
        return crt_except(0x11, 0x18, x, y, set_exponent(prod, e - 0x600), cw);
    if (e < -0x3fd)
        return crt_except(0x12, 0x18, x, y, set_exponent(prod, e + 0x600), cw);
    r = set_exponent(prod, e);
    if (cw & 0x20) {
        fpu_control((uint32_t)cw, 0xffff);
        return r;
    }
    return crt_except(0x10, 0x18, x, y, r, cw);
}
