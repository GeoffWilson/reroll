/* Group PO: the original's still-reachable functions in 0x46d0c0-0x474fa0
 * (the car effects drawing file, the sound engine's QMixer / EAX file and
 * the start-up file's helpers).
 *
 *  - the shield: its animated texture (0x46d0c0 locks the DirectDraw
 *    texture, 0x46d170 the ripples, 0x46d1f0 the brightening, hand-written
 *    asm in the original), the per-car packets (0x46dc50, 0x46dd20: one
 *    triangle packet per mesh face, sorted into facing / back-facing lists)
 *    and their link into the ordering table (0x46dff0);
 *  - the car's tyre trail: texture words (0x46e450), the per-frame update
 *    (0x46e4b0), the packets (0x46e540) and their texture coordinates
 *    (0x46ea10);
 *  - the skid marks' packets (0x46f230);
 *  - sound: a channel's velocity (0x470160), the profile statistics
 *    (0x470790, x87 doubles), a channel's EAX property set (0x471400), the
 *    EAX reverb mix of a voice (0x4714c0), the listener environment and
 *    volume (0x4716c0 / 0x471740 / 0x471780 / 0x471810), the mixer thunk
 *    0x472040;
 *  - files and processes: replay file deletes (0x474360, 0x474470), the
 *    working directory (0x4747b0), the CD check (0x474840), process launch
 *    (0x474a10, 0x474b90, 0x474cf0), path helpers (0x474ed0, 0x474fa0).
 *
 * Float steps are the original's x87 instructions in inline asm (memory
 * operands, so the results follow the control word as the original's do);
 * the transform / projection pointers *0x6ccd78 / *0x6ccd80, which hand a
 * value over on the x87 stack, are called back to back in one asm block.
 * Windows, QMixer and COM calls go through winapi.h and com.h. Everything
 * outside the group is called by address. */
#include "../runtime.h"
#include "gamefn.h"
#include "physics.h"
#include "com.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

#define g_race G32(0x4b1960)
#define g_config G32(0x4b2198)

/* 0x4722d0 (sound_mix) is void; 0x472040 passes on the eax it leaves. */
typedef int32_t(GAMEAPI *po_mix_fn)(void);

/* Outside the group, by address. Game addresses may be passed as integers
 * or pointers. */
#if defined(__i386__)
#define gte_rtps_float_at() __asm__ volatile("movl $0x4266d0, %%eax\n\tcall *%%eax" ::: "eax", "ecx", "edx", "cc", "memory", X87)
#define gte_rt_float_at() __asm__ volatile("movl $0x426620, %%eax\n\tcall *%%eax" ::: "eax", "ecx", "edx", "cc", "memory", X87)
#else
#define gte_rtps_float_at() GCALL(0x4266d0, gte_rtps_float)()
#define gte_rt_float_at() GCALL(0x426620, gte_rt_float)()
#endif
#define gte_rotate_at() GCALL(0x428090, gte_rotate)()
#define model_camera_matrix_set_at(b) GCALL(0x459e80, model_camera_matrix_set)((uint8_t *)GMEM(b))
#define car_count_min_at() GCALL(0x486270, weather_level_lower)()
#define texture_surface_lock_at(h, a, bits, d, bpp, pitch) \
    GCALL(0x428c30, texture_surface_lock)(A(h), A(a), A(bits), A(d), A(bpp), A(pitch))
#define texture_surface_unlock_at(h, a, d) GCALL(0x428c60, texture_surface_unlock)(A(h), A(a), A(d))
#define snd_chan_busy_at(i) GCALL(0x46fec0, snd_chan_busy)(i)
#define snd_channel_buffer_at(c) ((uint32_t)GCALL(0x46f8f0, snd_channel_buffer)((int32_t)(c)))
#define sound_eax_first_at() ((uint32_t)GCALL(0x471700, sound_eax_first)())
#define sound_mix_at() (((po_mix_fn)(uintptr_t)0x4722d0)())
#define chdir_path_at(p) GCALL(0x475030, dir_change_unquoted)((const char *)GMEM(p))
#define reg_value_read_at(key, name, buf, n) GCALL(0x46b650, reg_string_get)(key, name, A(buf), n)
#define reg_value_delete_at(key, name) GCALL(0x46b770, reg_value_remove)(key, name)
#define reg_key_delete_at(key) GCALL(0x46b2f0, reg_key_unload)(key)
#define text_line_get_at(buf, n, file, line) \
    GCALL(0x42e6d0, text_line_get)((uint8_t *)GMEM(buf), n, (const char *)GMEM(file), line)
#define file_exists_at(p) GCALL(0x476f00, path_find_exists)((const char *)GMEM(p))
#define command_token_copy_at(d, s) GCALL(0x474dc0, command_token_copy)((char *)GMEM(d), (const char *)GMEM(s))
#define replay_files_save_at() GCALL(0x41c550, replay_files_save)()
#define boot_data_save_at() GCALL(0x474260, boot_data_save)()
#define chain_event_close_at() GCALL(0x46b850, app_mutex_close)()
#define chain_event_create_at(out, name) GCALL(0x46b8b0, win_event_create)(A(out), name)
#define chain_event_set_at(e) GCALL(0x46b950, win_event_reset)(e)
#define chain_event_free_at(e) GCALL(0x46b890, win_handle_close_quiet)(e)
#define unquote_copy_at(d, s) GCALL(0x474e80, unquote_copy)((char *)GMEM(d), (const char *)GMEM(s))
#define splitpath_at(p, dr, di, f, e) \
    GCALL(0x4988c5, msvc_splitpath)((const char *)GMEM(p), (char *)GMEM(dr), (char *)GMEM(di), (char *)GMEM(f), \
                                    (char *)GMEM(e))
#define makepath_at(p, dr, di, f, e) \
    GCALL(0x498834, msvc_makepath)((char *)GMEM(p), (const char *)GMEM(dr), (const char *)GMEM(di), \
                                   (const char *)GMEM(f), (const char *)GMEM(e))
#define getcwd_at(b, n) GCALL(0x498a0c, msvc_getcwd)((char *)GMEM(b), n)

#if !defined(__i386__)
float subunit_transform_c(uint8_t *rec); /* 0x426a20: returns what the original leaves in st(0) (orig_wb.c) */
void wide_project_c(float h);            /* 0x426b00: projects *0x6ccd74 with st(0) = h (src/widescreen.c) */
float vec_facing_float_c(const uint8_t *a, const uint8_t *b, const uint8_t *c); /* 0x4278f0 (orig_pc.c) */
#endif

/* This group's functions (also in build/groups/po_protos.txt). */
int32_t GAMEAPI shield_texture_animate(void); /* 0x46d0c0 */
void GAMEAPI shield_texture_ripples(int32_t n, uint32_t bits, int32_t pitch, int32_t bpp); /* 0x46d170 */
void GAMEAPI shield_texture_brighten(uint32_t unused, uint32_t bits, uint32_t pitch, int32_t bpp); /* 0x46d1f0 */
void GAMEAPI shields_draw(void); /* 0x46dc50 */
void GAMEAPI shield_packets_build(uint8_t *rec); /* 0x46dd20 */
void GAMEAPI shield_packets_link(const uint8_t *car, uint32_t ot_back, uint32_t ot_front); /* 0x46dff0 */
void GAMEAPI car_trail_texture_set(uint8_t *trail, uint32_t tex); /* 0x46e450 */
void GAMEAPI car_trail_draw(uint8_t *car, uint32_t ot); /* 0x46e4b0 */
#if defined(__i386__)
void car_trail_packets_build(void); /* 0x46e540: asm entry, (car, ot) + the caller's esi */
#endif
void GAMEAPI car_trail_packets_build_c(uint8_t *car, uint32_t ot, uint32_t caller_esi);
void GAMEAPI car_trail_uv(const uint32_t *pos, uint8_t *u, uint8_t *v, const int32_t *rot); /* 0x46ea10 */
void GAMEAPI skid_marks_draw(void); /* 0x46f230 */
int32_t GAMEAPI snd_chan_velocity(int32_t chan, uint32_t x, uint32_t y, uint32_t z); /* 0x470160 */
int32_t GAMEAPI snd_profile_update(void); /* 0x470790 */
uint32_t GAMEAPI snd_channel_eax_props(uint32_t chan); /* 0x471400 */
int32_t GAMEAPI eax_voice_reverb_set(int32_t voice, uint32_t level); /* 0x4714c0 */
int32_t GAMEAPI eax_environment_set(uint32_t env); /* 0x4716c0 */
int32_t GAMEAPI eax_environment_get(uint32_t *out); /* 0x471740 */
int32_t GAMEAPI eax_volume_set(uint32_t level); /* 0x471780 */
int32_t GAMEAPI eax_volume_get(uint32_t *out); /* 0x471810 */
int32_t GAMEAPI sound_mix_jump(void); /* 0x472040 */
void GAMEAPI replay_file_delete(uint8_t *name); /* 0x474360 */
void GAMEAPI replay_files_delete_all(void); /* 0x474470 */
int32_t GAMEAPI dir_change_sub(const uint8_t *dir); /* 0x4747b0 */
int32_t GAMEAPI cd_find_data_path(int32_t quiet); /* 0x474840 */
int32_t GAMEAPI process_launch(const uint8_t *app, const uint8_t *args, uint32_t dir); /* 0x474a10 */
int32_t GAMEAPI setup_program_launch(void); /* 0x474b90 */
int32_t GAMEAPI chain_launch_and_wait(void); /* 0x474cf0 */
uint8_t *GAMEAPI path_dir_make(uint8_t *dst, const uint8_t *src); /* 0x474ed0 */
uint8_t *GAMEAPI cwd_path_quoted(uint8_t *dst); /* 0x474fa0 */

/* Every x87 register, for asm blocks that call out or use the FPU stack. */
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* ---- x87 helpers: the original's instructions ---- */

/* fld a; fcomp b (floats); fnstsw ax: the status word's high byte. */
static inline uint32_t po_fcmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 0xff;
}

/* fld qword a; fcomp qword b; fnstsw ax. */
static inline uint32_t po_dcmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("fldl (%1)\n\tfcompl (%2)\n\tfnstsw %0" : "=a"(sw) : "r"(PTR(a)), "r"(PTR(b)) : "st", "cc", "memory");
    return (sw >> 8) & 0xff;
}

/* fld a; fistp dword: a rounded by the control word. */
static inline int32_t po_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fild dword v; fstp dword. */
static inline uint32_t po_fild(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild word w; fstp dword t; fld t; fmul dword k; fstp dword. */
static inline uint32_t po_s16_mul(uint16_t w, uint32_t k)
{
    uint32_t r, t;
    __asm__ volatile("filds %2\n\tfstps %1\n\tflds %1\n\tfmuls %3\n\tfstps %0"
                     : "=m"(r), "=m"(t)
                     : "m"(w), "m"(k)
                     : "st");
    return r;
}

/* fld a; fmul b; fstp. */
static inline uint32_t po_mul(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld b; fdivr a; fstp: a / b. */
static inline uint32_t po_div(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfdivrs %2\n\tfstps %0" : "=m"(r) : "m"(b), "m"(a) : "st");
    return r;
}

/* fld x; fmul x; fld y; fmul y; faddp; fld z; fmul z; faddp; fstp. */
static inline uint32_t po_sumsq(uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfmuls %1\n\tflds %2\n\tfmuls %2\n\tfaddp %%st, %%st(1)\n\t"
                     "flds %3\n\tfmuls %3\n\tfaddp %%st, %%st(1)\n\tfstps %0"
                     : "=m"(r)
                     : "m"(x), "m"(y), "m"(z)
                     : "st", "st(1)");
    return r;
}

/* sqrt (0x4981d4) of the double hi:lo; the result's fstp dword. */
static inline uint32_t po_sqrt_double(uint32_t hi, uint32_t lo)
{
    uint32_t r;
#if defined(__i386__)
    __asm__ volatile("pushl %1\n\tpushl %2\n\tmovl $0x4981d4, %%eax\n\tcall *%%eax\n\taddl $8, %%esp\n\tfstps %0"
                     : "=m"(r)
                     : "r"(hi), "r"(lo)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    uint64_t q = (uint64_t)hi << 32 | lo;
    double d;
    float f;

    memcpy(&d, &q, 8);
    f = (float)__builtin_sqrt(d);
    memcpy(&r, &f, 4);
#endif
    return r;
}

/* fld dword f; fstp qword [esp]; call sqrt; fstp dword. */
static inline uint32_t po_sqrt_float(uint32_t f)
{
    uint32_t r;
#if defined(__i386__)
    __asm__ volatile("flds %1\n\tsubl $8, %%esp\n\tfstpl (%%esp)\n\tmovl $0x4981d4, %%eax\n\tcall *%%eax\n\t"
                     "addl $8, %%esp\n\tfstps %0"
                     : "=m"(r)
                     : "m"(f)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    float v, s;

    memcpy(&v, &f, 4);
    s = (float)__builtin_sqrt((double)v);
    memcpy(&r, &s, 4);
#endif
    return r;
}

/* Double steps on memory: dst = a op b (fld qword a; fop qword b; fstp qword dst). */
static inline void po_dadd(uint32_t dst, uint32_t a, uint32_t b)
{
    __asm__ volatile("fldl (%0)\n\tfaddl (%1)\n\tfstpl (%2)" : : "r"(PTR(a)), "r"(PTR(b)), "r"(PTR(dst)) : "st", "memory");
}

static inline void po_ddiv(uint32_t dst, uint32_t a, uint32_t b)
{
    __asm__ volatile("fldl (%0)\n\tfdivl (%1)\n\tfstpl (%2)" : : "r"(PTR(a)), "r"(PTR(b)), "r"(PTR(dst)) : "st", "memory");
}

static inline void po_dmul(uint32_t dst, uint32_t a, uint32_t b)
{
    __asm__ volatile("fldl (%0)\n\tfmull (%1)\n\tfstpl (%2)" : : "r"(PTR(a)), "r"(PTR(b)), "r"(PTR(dst)) : "st", "memory");
}

/* fild qword {lo, 0}; fstp qword dst. */
static inline void po_dfild64(uint32_t dst, uint32_t lo)
{
    uint32_t q[2] = {lo, 0};
    __asm__ volatile("fildll (%0)\n\tfstpl (%1)" : : "r"(q), "r"(PTR(dst)) : "st", "memory");
}

/* fild dword v; fstp qword dst. */
static inline void po_dfild32(uint32_t dst, int32_t v)
{
    __asm__ volatile("fildl %0\n\tfstpl (%1)" : : "m"(v), "r"(PTR(dst)) : "st", "memory");
}

/* if (src > dst) dst = src: fld src; fcomp dst; test ah, 0x41. */
static inline void po_dmax(uint32_t dst, uint32_t src)
{
    if (!(po_dcmp(src, dst) & 0x41)) {
        G32(dst) = G32(src);
        G32(dst + 4) = G32(src + 4);
    }
}

/* The transform pointer *0x6ccd78 (out) then the projection *0x6ccd80,
 * which takes the transform's st(0). */
static inline void po_transform_project(uint8_t *out)
{
#if defined(__i386__)
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tcall *0x6ccd80"
                     :
                     : "S"(out)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    /* x86-64: the transform 0x426a20 and the projection 0x426b00 on its st(0). */
    wide_project_c(subunit_transform_c(out));
#endif
}

/* 0x4278f0 (a, b, c): the facing value it returns in st(0), fstp dword. */
static inline uint32_t po_facing(const uint8_t *a, const uint8_t *b, const uint8_t *c)
{
    uint32_t r;
#if defined(__i386__)
    __asm__ volatile("pushl %3\n\tpushl %2\n\tpushl %1\n\tmovl $0x4278f0, %%eax\n\tcall *%%eax\n\t"
                     "addl $12, %%esp\n\tfstps %0"
                     : "=m"(r)
                     : "r"(a), "r"(b), "r"(c)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    float f = vec_facing_float_c(a, b, c);

    memcpy(&r, &f, 4);
#endif
    return r;
}

/* ---- string helpers (the original's inlined strcpy / strcat) ---- */

static void po_strcpy(uint8_t *d, const uint8_t *s)
{
    uint32_t i = 0;
    do
        d[i] = s[i];
    while (s[i++]);
}

static void po_strcat(uint8_t *d, const uint8_t *s)
{
    while (*d)
        d++;
    po_strcpy(d, s);
}

static void po_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *o = d;
    const uint8_t *i = s;
    while (n--)
        *o++ = *i++;
}

/* The ordering-table slot of a depth (already >> 5): halved twice above
 * 0x100 and 0x180, at most 0x1ff, at least 1. */
static int32_t po_depth_slot(int32_t z)
{
    if (z > 0x100) {
        z = z / 2 + 0x80;
        if (z > 0x180) {
            z = z / 2 + 0xc0;
            if (z > 0x1ff)
                return 0x1ff;
        }
    }
    if (z < 1)
        z = 1;
    return z;
}

/* ===================================================================== */
/* The shield. */

/* ===== 0x46d0c0: the shield's animated texture (handle 0x4b24a0, -1 =
 * none): frames since the last update (0x4be3e8 - 0x4b1c28, 1 when that
 * was -1 or more than 5 apart), the surface locked (0x428c30, out: bits,
 * bpp, pitch), the ripples drawn for 8 / 16 bits per pixel, unlocked;
 * 0x4b1c28 = the frame. Returns 1. The lock's outputs start at 0 here
 * (stack leftovers in the original when the lock fails without writing
 * them, which it never does). */
int32_t GAMEAPI shield_texture_animate(void)
{
    uint32_t handle = G32(0x4b24a0);
    uint32_t bpp = 0, pitch = 0, bits = 0;
    int32_t frames;

    if (handle == 0xffffffff)
        return 1;
    if (G32(0x4b1c28) == 0xffffffff || (frames = GI32(0x4be3e8) - GI32(0x4b1c28)) > 5)
        frames = 1;
    if (texture_surface_lock_at(handle, 1, &bits, 0x4d401c, &bpp, &pitch) != 0) {
        if (bpp == 8 || bpp == 16)
            shield_texture_ripples(frames, bits, (int32_t)pitch, (int32_t)bpp);
        texture_surface_unlock_at(G32(0x4b24a0), 1, 0x4d401c);
    }
    G32(0x4b1c28) = G32(0x4be3e8);
    return 1;
}

/* ===== 0x46d170: brighten the lower 128 rows (0x46d1f0), then at 8 bits
 * per pixel two ripple fronts: for each counter at 0x4b1c20 / 0x4b1c24,
 * 128 rows from (bits + pitch * 128 + 0xc0) get colour 0x84 at +(c & 63)
 * and -(c & 63) - 1, c dropping by one every second row; the counter then
 * steps. (The original keeps the start in its fourth argument's slot.) */
void GAMEAPI shield_texture_ripples(int32_t n, uint32_t bits, int32_t pitch, int32_t bpp)
{
    uint32_t start, *counter;

    shield_texture_brighten((uint32_t)n, bits, (uint32_t)pitch, bpp);
    if (bpp != 8)
        return;
    start = ((uint32_t)pitch << 7) + bits + 0xc0;
    for (counter = (uint32_t *)0x4b1c20; counter < (uint32_t *)0x4b1c28; counter++) {
        uint32_t c = *counter, row = start;
        int32_t k;

        for (k = 0; k < 0x80; k++) {
            uint32_t o = c & 0x3f;

            G8(row + o) = 0x84;
            G8(row - o - 1) = 0x84;
            if (k & 1)
                c--;
            row += (uint32_t)pitch;
        }
        *counter = *counter + 1;
    }
}

/* ===== 0x46d1f0 (hand-written asm in the original): at 8 bits per pixel,
 * 128 rows of 128 bytes from bits + pitch * 128 + 0x80: each dword
 * + 0x02020202, then 2 taken back from each byte whose top bit is clear
 * (carries between bytes as in the original). The first argument is not
 * read. */
void GAMEAPI shield_texture_brighten(uint32_t unused, uint32_t bits, uint32_t pitch, int32_t bpp)
{
    uint32_t p;
    int32_t r, k;

    (void)unused;
    if (bpp != 8)
        return;
    p = bits + pitch * 0x80 + 0x80;
    for (r = 0; r < 0x80; r++) {
        for (k = 0; k < 0x20; k++) {
            uint32_t v = G32(p) + 0x02020202;

            G32(p) = v - (((v & 0x80808080) ^ 0x80808080) >> 6);
            p += 4;
        }
        p += pitch - 0x80;
    }
}

/* ===== 0x46dc50: per car slot (race +0x54d, 0x6bfb80 + 0x274 * i), a
 * car with a shield record (car +0x6ac, -1 none): record +0x74 = -1 (no
 * packets); when the record is on (+0): the body (record +0x64) moved by
 * its +0x14bc offset for the camera matrix (0x459e80) and put back, then
 * the packets (0x46dd20). */
void GAMEAPI shields_draw(void)
{
    uint32_t i = 0, slot = 0x6bfb80;

    if (U8(g_race, 0x54d) == 0)
        return;
    do {
        uint32_t rec = U32(G32(slot), 0x6ac);

        if (rec != 0xffffffff) {
            uint8_t on = U8(rec, 0);

            U32(rec, 0x74) = 0xffffffff;
            if (on) {
                uint32_t body = U32(rec, 0x64);
                uint32_t x = U32(body, 0x34), y = U32(body, 0x38), z = U32(body, 0x3c);

                U32(body, 0x34) = U32(body, 0x14bc) + x;
                U32(body, 0x38) = U32(body, 0x14c0) + y;
                U32(body, 0x3c) = U32(body, 0x14c4) + z;
                model_camera_matrix_set_at(body);
                U32(body, 0x34) = x;
                U32(body, 0x38) = y;
                U32(body, 0x3c) = z;
                shield_packets_build(PTR(rec));
            }
        }
        i++;
        slot += 0x274;
    } while (i < U8(g_race, 0x54d));
}

/* ===== 0x46dd20: a car's shield packets. The origin through 0x4266d0;
 * nothing when its depth (0x6cccd0) < 1. The mesh (*0x4d44a4): vertices
 * (+0x40, 8 bytes each, +4 count + 1 of them) through the transform /
 * projection pointers into 0x18-byte records; faces (+0xc, 8 bytes, +6 of
 * them) into the record's packets of 0x64 (+0x68, or +0x6c for the second
 * buffer 0x6ce60c != 0x6cd4a0): three vertex records (+8 / +0x28 / +0x48,
 * face bytes 0..2, 1-based), skipped when the three clip words share a bit;
 * without the texture (config +0x2a off or 0x4b24a0 = -1) the texture words
 * from the 0x6c1e14 record (+0x2e of *0x6c1eb0, + 0x6c0a50 & 7, + 0x4e);
 * the vertex colours (record +0x70, 1-based), code 0x9e, +0x22 the first
 * colour's top byte; 0x4278f0's facing (negated when 0x6cce94) < 0: the
 * front list (+0x7c head, +0x80 tail), else the back list (+0x74 / +0x78).
 * The vertex records sit in a 132-record stack buffer in the original; a
 * face index of 0 or past the vertices reads other locals / stale stack
 * there, a zero record here. */
void GAMEAPI shield_packets_build(uint8_t *rec)
{
    uint8_t verts[133 * 0x18];
    uint32_t negate = G32(0x6cce94);
    uint32_t tex = (U16(G32(0x6c1eb0), 0x2e) + (G32(0x6c0a50) & 7) + 0x4e) * 16 + G32(0x6c1e14);
    uint32_t uv0 = U32(tex, 0), uv1 = U32(tex, 4);
    uint16_t uv2 = U16(tex, 8);
    uint32_t back_head = 0, back_tail = 0, front_head = 0, front_tail = 0;
    uint32_t mesh, pos, faces, colours, packet, count, k, n;
    int notex;

    G32(0x6ccc64) = 0;
    G32(0x6ccc68) = 0;
    gte_rtps_float_at();
    if (GI32(0x6cccd0) < 1)
        return;
    notex = U8(g_config, 0x2a) == 0 || G32(0x4b24a0) == 0xffffffff;
    for (k = 0; k < 0x18; k++)
        verts[k] = 0;
    pos = U32(G32(0x4d44a4), 0x40);
    G32(0x6ccc64) = U32(pos, 0);
    G32(0x6ccc68) = U32(pos, 4);
    po_transform_project(verts + 0x18);
    n = U16(G32(0x4d44a4), 4);
    for (k = 1; k <= n; k++) {
        pos += 8;
        G32(0x6ccc64) = U32(pos, 0);
        G32(0x6ccc68) = U32(pos, 4);
        po_transform_project(verts + 0x18 * (k + 1));
    }
    packet = G32(0x6ce60c) == 0x6cd4a0 ? U32(rec, 0x68) : U32(rec, 0x6c);
    mesh = G32(0x4d44a4);
    faces = U32(mesh, 0xc);
    colours = U32(rec, 0x70) - 4;
    for (count = U16(mesh, 6); count; count--, packet += 0x64) {
        uint32_t f = U32(faces, 0);
        uint32_t b0 = f & 0xff, b1 = (f >> 8) & 0xff, b2 = (f >> 16) & 0xff;
        uint32_t facing;

        po_copy(PTR(packet + 8), verts + 0x18 * b0, 0x18);
        po_copy(PTR(packet + 0x28), verts + 0x18 * b1, 0x18);
        po_copy(PTR(packet + 0x48), verts + 0x18 * b2, 0x18);
        faces += 8;
        if (U32(packet, 0x28) & U32(packet, 8) & U32(packet, 0x48))
            continue;
        if (notex) {
            U32(packet, 0x20) = uv0;
            U32(packet, 0x40) = uv1;
            U16(packet, 0x60) = uv2;
        }
        U32(packet, 4) = U32(colours, 4 * b0);
        U32(packet, 0x24) = U32(colours, 4 * b1);
        U32(packet, 0x44) = U32(colours, 4 * b2);
        U16(packet, 0x22) = (uint16_t)(U32(colours, 4 * b0) >> 24);
        U8(packet, 7) = 0x9e;
        facing = po_facing(PTR(packet + 8), PTR(packet + 0x48), PTR(packet + 0x28));
        if (negate)
            facing ^= 0x80000000; /* fchs */
        if (po_fcmp(facing, G32(0x4a3318)) & 1) {
            if (!front_tail)
                front_head = packet;
            else
                U32(front_tail, 0) = packet;
            front_tail = packet;
        } else {
            if (!back_tail)
                back_head = packet;
            else
                U32(back_tail, 0) = packet;
            back_tail = packet;
        }
    }
    U32(rec, 0x74) = back_head;
    U32(rec, 0x78) = back_tail;
    U32(rec, 0x7c) = front_head;
    U32(rec, 0x80) = front_tail;
}

/* ===== 0x46dff0: link a car's shield packet lists into the ordering
 * table: with the depth table on (0x6cce84) both at the slot for the car's
 * depth (+0x14b4 >> 5) in 0x6cdd80, front list first; else the front list
 * at ot_front, the back list at ot_back. Nothing without a record, when it
 * is off, or with no packets (+0x74 = -1). */
void GAMEAPI shield_packets_link(const uint8_t *car, uint32_t ot_back, uint32_t ot_front)
{
    uint32_t rec = U32(car, 0x6ac), tail, entry;

    if (rec == 0xffffffff || U8(rec, 0) == 0 || U32(rec, 0x74) == 0xffffffff)
        return;
    if (G32(0x6cce84)) {
        int32_t slot = po_depth_slot(I32(car, 0x14b4) >> 5);

        tail = U32(rec, 0x80);
        if (tail) {
            entry = G32(0x6cdd80 + 4 * slot) & 0x7fffffff;
            U32(tail, 0) = U32(entry, 0);
            U32(entry, 0) = U32(rec, 0x7c);
        }
        tail = U32(rec, 0x78);
        if (tail) {
            entry = G32(0x6cdd80 + 4 * slot) & 0x7fffffff;
            U32(tail, 0) = U32(entry, 0);
            U32(entry, 0) = U32(rec, 0x74);
        }
        return;
    }
    tail = U32(rec, 0x80);
    if (tail) {
        entry = ot_front & 0x7fffffff;
        U32(tail, 0) = U32(entry, 0);
        U32(entry, 0) = U32(rec, 0x7c);
    }
    tail = U32(rec, 0x78);
    if (tail) {
        entry = ot_back & 0x7fffffff;
        U32(tail, 0) = U32(entry, 0);
        U32(entry, 0) = U32(rec, 0x74);
    }
}

/* ===================================================================== */
/* The tyre trail. */

/* ===== 0x46e450: the trail's texture: record (+0x2e of *0x6c1eb0 + tex)
 * of the 0x6c1e14 table (16 bytes): bytes 0 / 1 to trail +0x5a / +0x5b,
 * words +2 / +6 into each of the 0x78 packets (0x9c apart from *trail) at
 * +0x2e / +0x4e. */
void GAMEAPI car_trail_texture_set(uint8_t *trail, uint32_t tex)
{
    uint32_t t = (U16(G32(0x6c1eb0), 0x2e) + tex) * 16 + G32(0x6c1e14);
    uint8_t b1 = U8(t, 1), b0 = U8(t, 0);
    uint32_t p = U32(trail, 0) + 0x2e;
    uint16_t w6, w2;
    int32_t k;

    U8(trail, 0x5a) = b0;
    U8(trail, 0x5b) = b1;
    w6 = U16(t, 6);
    w2 = U16(t, 2);
    for (k = 0; k < 0x78; k++, p += 0x9c) {
        U16(p, 0x20) = w6;
        U16(p, 0) = w2;
    }
}

/* ===== 0x46e4b0: the trail (car +0x10ac) when trails are on (config
 * +0x24), not hidden (0x6ccd84 & 4) and due (+0x58): on a surface (car
 * +0x11c >= 0, its low byte into **0x6c1e30) with flag 8 the texture 0x57
 * once (+0x59 = 0 from 1), else 0x56 once (+0x59 = 1 from 0); then the
 * packets (0x46e540) and +0x58 = 0. */
void GAMEAPI car_trail_draw(uint8_t *car, uint32_t ot)
{
    uint8_t *trail = car + 0x10ac;
    int32_t surface;

    if (U8(g_config, 0x24) == 0 || (G8(0x6ccd84) & 4) || U8(trail, 0x58) == 0)
        return;
    surface = I32(car, 0x11c);
    if (surface >= 0) {
        uint32_t s = U32(U32(G32(0x6c1e30), 0), 4 * (surface & 0xff));
        uint8_t state = U8(car, 0x1105);

        if (U8(s, 4) & 8) {
            if (state == 1) {
                car_trail_texture_set(trail, 0x57);
                U8(trail, 0x59) = 0;
            }
        } else if (state == 0) {
            car_trail_texture_set(trail, 0x56);
            U8(trail, 0x59) = 1;
        }
    }
    car_trail_packets_build_c(car, ot, A(trail));
    U8(trail, 0x58) = 0;
}

/* ===== 0x46e540: the trail's packets. The car model (+0x10) is matched
 * against the 7 models listed at 0x4b1c50 (indices through *0x6c1e30 +0x58
 * into +0x18) for the size 0x4b1c34[k] (0 when none); the colour's level
 * is a second 0x486270() * size >> 8 when 0x486270() * size >> 7 (even)
 * exceeds the size (even), else half the size, as (level | 0xffff8e00) spread into the colour bytes; texture
 * offsets car +0x1106 / +0x1107 to 0x4d44b1 / 0x4d44b0. The car matrix
 * (+0xc4..+0xd4) to 0x6ccbe0 and as floats * 1/4096 to 0x6ccd1c..; the
 * translation ((+0x34 / +0x3c >> 20) & 0x7f) - 0x40, 0 to 0x6ccbf4.. and
 * as floats to 0x6ccd0c... The model's vertices (+0x48, +4 count + 1)
 * rotated (0x428090). For each face byte from +0x10b4 to *(+0x10fc), the
 * face (model +0xc, 8 bytes; bytes 1 and 2 swapped) is skipped when a
 * vertex has rotated y > 0; else a 0x9c packet (+0x10ac buffer, or +0x10b0
 * for the second, from +0xc): texture coordinates per vertex (0x46ea10,
 * model +0x40 positions), the four 0x18-byte vertex blocks from the face's
 * 0x84 record at car +0x1100, the colour, code from record +7 | 2, linked
 * at the depth slot (fistp of the float at +0x18 when 0x6cce84) or ot; at
 * most 60. A face byte of 0 reads the caller's esi in the original (its
 * saved register sits below the vertex buffer): the asm entry passes it. */
void GAMEAPI car_trail_packets_build_c(uint8_t *car, uint32_t ot, uint32_t caller_esi)
{
    int32_t rot[133][3];
    uint32_t model = U32(car, 0x10);
    uint32_t uvrec = U32(car, 0x1100);
    uint32_t built = 0;
    uint32_t pos = U32(model, 0x40), faces = U32(model, 0xc);
    uint32_t table, indices, models, k, colour, buffer, v, n, p, bx;
    int32_t size, level, r;

    if (U8(g_config, 0x24) == 0)
        return;
    table = G32(0x6c1e30);
    indices = U32(table, 0x58);
    models = U32(table, 0x18);
    for (k = 0; k < 7; k++)
        if (model == U32(models, 4 * (uint32_t)(int32_t)S16(indices, 2 * U16(0x4b1c50, 2 * k))))
            break;
    if (k >= 7)
        k = 0;
    size = GI32(0x4b1c34 + 4 * k);
    r = car_count_min_at();
    if ((((r * size) >> 7) & -2) > (size & -2))
        level = (car_count_min_at() * GI32(0x4b1c34 + 4 * k)) >> 8;
    else
        level = size >> 1;
    G8(0x4d44b1) = U8(car, 0x1106);
    colour = (uint32_t)level | (((((uint32_t)level | 0xffff8e00) << 8) | (uint32_t)level) << 8);
    G8(0x4d44b0) = U8(car, 0x1107);
    buffer = G32(0x6ce60c) == 0x6cd4a0 ? U32(car, 0x10ac) : U32(car, 0x10b0);
    for (k = 0; k < 5; k++)
        G32(0x6ccbe0 + 4 * k) = U32(car, 0xc4 + 4 * k);
    G32(0x6ccd1c) = po_s16_mul(G16(0x6ccbe0), G32(0x4a3310));
    G32(0x6ccd20) = po_s16_mul(G16(0x6ccbe2), G32(0x4a3310));
    G32(0x6ccd24) = po_s16_mul(G16(0x6ccbe4), G32(0x4a3310));
    G32(0x6ccd2c) = po_s16_mul(G16(0x6ccbe6), G32(0x4a3310));
    G32(0x6ccd30) = po_s16_mul(G16(0x6ccbe8), G32(0x4a3310));
    G32(0x6ccd34) = po_s16_mul(G16(0x6ccbea), G32(0x4a3310));
    G32(0x6ccd3c) = po_s16_mul(G16(0x6ccbec), G32(0x4a3310));
    G32(0x6ccd40) = po_s16_mul(G16(0x6ccbee), G32(0x4a3310));
    G32(0x6ccd44) = po_s16_mul(G16(0x6ccbf0), G32(0x4a3310));
    G32(0x6ccbf4) = (uint32_t)(((I32(car, 0x34) >> 20) & 0x7f) - 0x40);
    G32(0x6ccbf8) = 0;
    G32(0x6ccbfc) = (uint32_t)(((I32(car, 0x3c) >> 20) & 0x7f) - 0x40);
    G32(0x6ccd0c) = po_fild(GI32(0x6ccbf4));
    G32(0x6ccd10) = po_fild(GI32(0x6ccbf8));
    G32(0x6ccd14) = po_fild(GI32(0x6ccbfc));
    v = U32(model, 0x48);
    n = (uint32_t)U16(model, 4) + 1;
    for (k = 0; k < n; k++, v += 8) {
        G32(0x6ccc64) = U32(v, 0);
        G32(0x6ccc68) = U32(v, 4);
        gte_rotate_at();
        rot[k][0] = GI32(0x6cccc8);
        rot[k][1] = GI32(0x6ccccc);
        rot[k][2] = GI32(0x6cccd0);
    }
    p = A(car) + 0x10b4;
    if (p >= U32(car, 0x10fc))
        return;
    bx = buffer + 0x2c;
    do {
        uint32_t f = U8(p, 0);
        uint32_t d = U32(faces, 8 * f);
        uint32_t q = uvrec + 4 * (f * 33);
        uint32_t s = (d & 0xff0000ff) | ((d & 0xff00) << 8) | (((d >> 16) & 0xff) << 8);
        uint32_t u, w, c, j, entry;

#define TRAIL_Y(b) ((b) ? rot[(b) - 1][1] : (int32_t)caller_esi)
        if (TRAIL_Y(s & 0xff) > 0 || TRAIL_Y((s >> 8) & 0xff) > 0 || TRAIL_Y((s >> 16) & 0xff) > 0)
            goto next;
        if ((s >> 24) != 0 && TRAIL_Y(s >> 24) > 0)
            goto next;
#undef TRAIL_Y
        u = bx;
        w = bx + 1;
        c = s;
        for (j = 0; j < 4; j++) {
            uint32_t b = c & 0xff;

            c >>= 8;
            if (b) {
                car_trail_uv((const uint32_t *)PTR(pos + 8 * b - 8), PTR(u), PTR(w), rot[b - 1]);
                u += 0x20;
                w += 0x20;
            }
        }
        po_copy(PTR(bx - 0x18), PTR(q + 8), 0x18);
        po_copy(PTR(bx + 8), PTR(q + 0x28), 0x18);
        po_copy(PTR(bx + 0x28), PTR(q + 0x48), 0x18);
        po_copy(PTR(bx + 0x48), PTR(q + 0x68), 0x18);
        U32(bx, -0x1c) = colour;
        U32(bx, 4) = colour;
        U32(bx, 0x24) = colour;
        U32(bx, 0x44) = colour;
        U8(bx, -0x19) = U8(q, 7) | 2;
        if (G32(0x6cce84))
            entry = G32(0x6cdd80 + 4 * po_depth_slot(po_fist(U32(bx, -0x14)) >> 5));
        else
            entry = ot;
        entry &= 0x7fffffff;
        U32(bx, -0x20) = U32(entry, 0);
        U32(entry, 0) = bx - 0x20;
        bx += 0x9c;
        if (++built >= 0x3c)
            return;
    next:
        p++;
    } while (p < U32(car, 0x10fc));
}

#if defined(__i386__)
__asm__(".text\n"
        ".globl car_trail_packets_build\n"
        "car_trail_packets_build:\n\t"
        "pushl %esi\n\t"
        "pushl 12(%esp)\n\t"
        "pushl 12(%esp)\n\t"
        "call car_trail_packets_build_c\n\t"
        "addl $12, %esp\n\t"
        "ret\n");
#endif

/* ===== 0x46ea10: a trail vertex's texture coordinates: its position
 * through 0x426620; u = clamp((rot x >> 10) + (x >> 1) + 0x40, 0, 0x7f)
 * + *0x4d44b1, v the same from z + *0x4d44b0. */
void GAMEAPI car_trail_uv(const uint32_t *pos, uint8_t *u, uint8_t *v, const int32_t *rot)
{
    int32_t z, x, a, c;

    G32(0x6ccc64) = pos[0];
    G32(0x6ccc68) = pos[1];
    gte_rt_float_at();
    z = GI32(0x6cccd0);
    x = GI32(0x6cccc8);
    a = (rot[0] >> 10) + (x >> 1) + 0x40;
    c = (rot[2] >> 10) + (z >> 1) + 0x40;
    if (a < 0)
        a = 0;
    if (c < 0)
        c = 0;
    if (a > 0x7f)
        a = 0x7f;
    if (c > 0x7f)
        c = 0x7f;
    c += (int32_t)(G32(0x4d44b0) & 0xff);
    *u = (uint8_t)(G8(0x4d44b1) + (uint8_t)a);
    *v = (uint8_t)c;
}

/* ===================================================================== */
/* The skid marks. */

/* ===== 0x46f230: the skid marks' packets when shown (0x6ccd84 & 2,
 * config +0x2c): each of the 0x4b1c68 records (0x124 bytes from
 * *0x6002d4) in use (+8 & 0x8000): its two points (+0 / +0xc) and each
 * point plus its packed side offset (+8: three signed 5..7-bit fields
 * * 4) through the transform / projection pointers, given the first is in
 * the depth range (-250 <= z <= 5000); unless all four clip words share a
 * bit, a 0x84-byte packet (+0x1c, +0x84 more for the second buffer): the
 * four vertices, colours *0x4d44b8 (+0x18 / +0x1a set) or *0x4d44bc, end
 * alpha bytes, linked at the depth slot of the third vertex's z (fistp,
 * 1048576 past it; >> 5, less 2 or 3) in 0x6cdd80 or 0x6cd510. The offset
 * point's fourth word is a stack leftover in the original (it reaches
 * 0x6ccc6a): 0 here. */
void GAMEAPI skid_marks_draw(void)
{
    uint8_t buf[0x60];
    uint32_t rec = G32(0x6002d4), off = 0, i;

    if (!(G8(0x6ccd84) & 2))
        return;
    if (U8(g_config, 0x2c) == 0)
        return;
    if (G32(0x6ce60c) == 0x6cd4a0)
        off = 0x84;
    for (i = 0; i < G32(0x4b1c68); i++, rec += 0x124) {
        uint32_t s = rec + 8, packet, j, edge0, edge1, entry, zf;
        int32_t z;

        if (!(U32(s, 0) & 0x8000))
            continue;
        packet = rec + off + 0x1c;
        for (j = 0; j < 2; j++, s += 0xc) {
            uint8_t *o = buf + 0x30 * j;
            uint32_t m;
            uint16_t px, py, pz;

            G32(0x6ccc64) = U32(s, -8);
            G32(0x6ccc68) = U32(s, -4);
            po_transform_project(o);
            m = U32(s, 0);
            px = (uint16_t)((((int32_t)(m << 27) >> 25) & -4) + U16(s, -8));
            py = (uint16_t)((((int32_t)(m << 22) >> 25) & -4) + U16(s, -6));
            pz = (uint16_t)((((int32_t)(m << 17) >> 25) & ~3) + U16(s, -4));
            if (po_fcmp(U32(o, 4), G32(0x4a33e0)) & 1)
                goto next;
            if (!(po_fcmp(U32(o, 4), G32(0x4a33dc)) & 0x41))
                goto next;
            G32(0x6ccc64) = px | ((uint32_t)py << 16);
            G32(0x6ccc68) = pz;
            po_transform_project(o + 0x18);
        }
        if (U32(buf, 0x48) & U32(buf, 0x30) & U32(buf, 0x18) & U32(buf, 0))
            continue;
        po_copy(PTR(packet + 8), buf, 0x18);
        po_copy(PTR(packet + 0x28), buf + 0x18, 0x18);
        po_copy(PTR(packet + 0x48), buf + 0x30, 0x18);
        po_copy(PTR(packet + 0x68), buf + 0x48, 0x18);
        edge0 = U16(rec, 0x18) != 0 ? 0 : 0xff;
        edge1 = U16(rec, 0x1a) != 0 ? 0 : 0xff;
        U32(packet, 4) = edge0 ? G32(0x4d44b8) : G32(0x4d44bc);
        U32(packet, 0x24) = edge0 ? G32(0x4d44b8) : G32(0x4d44bc);
        U32(packet, 0x44) = edge1 ? G32(0x4d44b8) : G32(0x4d44bc);
        U32(packet, 0x64) = edge1 ? G32(0x4d44b8) : G32(0x4d44bc);
        U16(packet, 0x22) = (uint16_t)edge0;
        U8(packet, 0x27) = (uint8_t)edge0;
        U8(packet, 0x47) = (uint8_t)edge1;
        U8(packet, 0x67) = (uint8_t)edge1;
        zf = U32(buf, 0x34);
        if (!(po_fcmp(zf, G32(0x4a33d8)) & 1))
            zf = 0x49800000;
        z = po_fist(zf) >> 5;
        z = z < 0xd ? z - 2 : z - 3;
        z = po_depth_slot(z);
        entry = (G32(0x6cce84) ? G32(0x6cdd80 + 4 * z) : G32(0x6cd510 + 4 * z)) & 0x7fffffff;
        U32(packet, 0) = U32(entry, 0);
        U32(entry, 0) = packet;
    next:;
    }
}

/* ===================================================================== */
/* Sound: QMixer and EAX. */

/* ===== 0x470160: a channel's velocity (x, y, z floats) when the mixer is
 * open (0x4d4524, 0x4d4528) and 0 <= chan < 64: scaled down to length
 * sqrt(80000) when its square is above 80000 (x87, sqrt 0x4981d4); unless
 * equal to the cached one (channel record 0x6cb500 + 0x3c * chan, +0x1c;
 * x87 'equal or unordered'), cached and sent (QSWaveMixSetSourceVelocity,
 * flags 0): 0 / -1 by its result; -1 when closed or out of range. */
int32_t GAMEAPI snd_chan_velocity(int32_t chan, uint32_t x, uint32_t y, uint32_t z)
{
    uint32_t sq, cache;

    if (!G32(0x4d4524) || !G32(0x4d4528) || chan < 0 || chan >= 0x40)
        return -1;
    sq = po_sumsq(x, y, z);
    if (!(po_fcmp(sq, G32(0x4a3408)) & 0x41)) {
        uint32_t limit = po_sqrt_double(0x40f38800, 0);
        uint32_t length = po_sqrt_float(sq);
        uint32_t k = po_div(limit, length);

        x = po_mul(k, x);
        y = po_mul(k, y);
        z = po_mul(k, z);
    }
    cache = 0x6cb51c + (uint32_t)chan * 0x3c;
    if ((po_fcmp(G32(cache), x) & 0x40) && (po_fcmp(G32(cache + 4), y) & 0x40) &&
        (po_fcmp(G32(cache + 8), z) & 0x40))
        return 0;
    G32(cache) = x;
    G32(cache + 4) = y;
    G32(cache + 8) = z;
    return QSWaveMixSetSourceVelocity(G32(0x4d453c), (uint32_t)chan, 0, (const float *)GMEM(cache)) != 0 ? -1 : 0;
}

/* ===== 0x470790: the profile statistics, once per started profile
 * (0x4d4558, cleared): frames 0x600160 += 1.0; QSWaveMixStopProfile into
 * five dwords (doubles at 0x600180..); channels in use / playing (0x46fec0)
 * / playing and not 3D-off (+0 & 0xc) to 0x600168 / 70 / 78; running sums
 * 0x6001a8.., averages 0x6001e8.., maxima 0x600228..; per-100 ratios
 * 0x600268.. (from the latest), 0x600280.. (sums), 0x600298.. (averages);
 * their maxima 0x6002b0... All x87 doubles at the control word's
 * precision. -1 when the mixer is closed, else 0. */
int32_t GAMEAPI snd_profile_update(void)
{
    uint32_t stats[5] = {0, 0, 0, 0, 0};
    uint32_t k[2];
    int32_t used = 0, playing = 0, solid = 0, i;

    if (!G32(0x4d4524))
        return -1;
    if (!G32(0x4d4558))
        return 0;
    po_dadd(0x600160, 0x600160, 0x4a3410);
    G32(0x4d4558) = 0;
    QSWaveMixStopProfile(G32(0x4d453c), stats, 0x14);
    po_dfild64(0x600180, stats[0]);
    po_dfild64(0x600188, stats[1]);
    po_dfild64(0x600190, stats[2]);
    po_dfild64(0x600198, stats[3]);
    po_dfild64(0x6001a0, stats[4]);
    for (i = 0; i < GI32(0x4b1c8c); i++) {
        uint32_t rec = 0x6cb500 + 0x3c * (uint32_t)i;

        if (U8(rec, 1) == 0)
            continue;
        used++;
        if (snd_chan_busy_at(i) == 0)
            continue;
        playing++;
        if (!(U8(rec, 0) & 0xc))
            solid++;
    }
    po_dfild32(0x600168, used);
    po_dfild32(0x600170, playing);
    po_dfild32(0x600178, solid);
    for (i = 0; i < 8; i++)
        po_dadd(0x6001a8 + 8 * i, 0x6001a8 + 8 * i, 0x600168 + 8 * i);
    for (i = 0; i < 8; i++)
        po_ddiv(0x6001e8 + 8 * i, 0x6001a8 + 8 * i, 0x600160);
    for (i = 0; i < 8; i++)
        po_dmax(0x600228 + 8 * i, 0x600168 + 8 * i);
    po_ddiv(A(k), 0x4a3368, 0x6001a0);
    po_dmul(0x600268, 0x600188, A(k));
    po_dmul(0x600270, 0x600190, A(k));
    po_dmul(0x600278, 0x600198, A(k));
    po_ddiv(A(k), 0x4a3368, 0x6001e0);
    po_dmul(0x600280, 0x6001c8, A(k));
    po_dmul(0x600288, 0x6001d0, A(k));
    po_dmul(0x600290, 0x6001d8, A(k));
    po_ddiv(A(k), 0x4a3368, 0x600220);
    po_dmul(0x600298, 0x600208, A(k));
    po_dmul(0x6002a0, 0x600210, A(k));
    po_dmul(0x6002a8, 0x600218, A(k));
    po_dmax(0x6002b0, 0x600268);
    po_dmax(0x6002b8, 0x600270);
    po_dmax(0x6002c0, 0x600278);
    return 0;
}

/* ===== 0x471400: a channel's EAX property set: the channel's DirectSound
 * buffer (0x46f8f0) QueryInterface'd for 0x4a4330 (never released, sic),
 * returned when it supports get and set (QuerySupport & 3) of both the
 * listener (0x4a33e8) and buffer (0x4a33f8) property sets; else 0. */
uint32_t GAMEAPI snd_channel_eax_props(uint32_t chan)
{
    uint32_t support = 0, props = 0;
    uint32_t buffer = snd_channel_buffer_at(chan);

    if (!buffer)
        return 0;
    if (IDirectSoundBuffer_QueryInterface(buffer, GMEM(0x4a4330), &props) < 0)
        return 0;
    if (!props)
        return 0;
    if (IKsPropertySet_QuerySupport(props, GMEM(0x4a33e8), 0, &support) < 0)
        return 0;
    if ((support & 3) != 3)
        return 0;
    if (IKsPropertySet_QuerySupport(props, GMEM(0x4a33f8), 0, &support) < 0)
        return 0;
    return (support & 3) == 3 ? props : 0;
}

/* ===== 0x4714c0: a voice's EAX reverb mix (0..1, clamped) when the mixer
 * is open and 0 <= voice < 64 with a property set (0x6cb534 + 0x3c * i):
 * unless equal to the cached level (+0x30; equal or unordered), cached and
 * Set (buffer set 0x4a33f8, property 1): 0, -1 when it fails, when closed
 * or out of range; 0 without a property set. */
int32_t GAMEAPI eax_voice_reverb_set(int32_t voice, uint32_t level)
{
    uint32_t base, props;

    if (!G32(0x4d4524) || !G32(0x4d4528) || voice < 0 || voice >= 0x40)
        return -1;
    base = (uint32_t)voice * 0x3c;
    props = G32(0x6cb534 + base);
    if (!props)
        return 0;
    if (po_fcmp(level, G32(0x4a3318)) & 1)
        level = 0;
    else if (!(po_fcmp(level, G32(0x4a331c)) & 0x41))
        level = 0x3f800000;
    if (po_fcmp(G32(0x6cb530 + base), level) & 0x40)
        return 0;
    G32(0x6cb530 + base) = level;
    if (IKsPropertySet_Set(props, GMEM(0x4a33f8), 1, 0, 0, &level, 4) < 0)
        return -1;
    return 0;
}

/* ===== 0x4716c0: the EAX listener environment (0..25, else 1) through the
 * first voice's property set (0x471700): 1, -1 when Set fails, 0 without
 * EAX. */
int32_t GAMEAPI eax_environment_set(uint32_t env)
{
    uint32_t props = sound_eax_first_at();

    if (!props)
        return 0;
    if (env >= 0x1a)
        env = 1;
    return IKsPropertySet_Set(props, GMEM(0x4a33e8), 1, 0, 0, &env, 4) >= 0 ? 1 : -1;
}

/* ===== 0x471740: Get the EAX environment into *out: 0, -1 when Get fails
 * or without EAX. */
int32_t GAMEAPI eax_environment_get(uint32_t *out)
{
    uint32_t got;
    uint32_t props = sound_eax_first_at();

    if (!props)
        return -1;
    return IKsPropertySet_Get(props, GMEM(0x4a33e8), 1, 0, 0, out, 4, &got) >= 0 ? 0 : -1;
}

/* ===== 0x471780: the EAX listener volume (0..1, clamped; property 2):
 * 1, -1 when Set fails or the mixer is closed, 0 without EAX. */
int32_t GAMEAPI eax_volume_set(uint32_t level)
{
    uint32_t props = sound_eax_first_at();

    if (!props)
        return 0;
    if (!G32(0x4d4524) || !G32(0x4d4528))
        return -1;
    if (po_fcmp(level, G32(0x4a3318)) & 1)
        level = 0;
    else if (!(po_fcmp(level, G32(0x4a331c)) & 0x41))
        level = 0x3f800000;
    return IKsPropertySet_Set(props, GMEM(0x4a33e8), 2, 0, 0, &level, 4) >= 0 ? 1 : -1;
}

/* ===== 0x471810: Get the EAX volume into *out: 0, -1 when Get fails or
 * without EAX. */
int32_t GAMEAPI eax_volume_get(uint32_t *out)
{
    uint32_t got;
    uint32_t props = sound_eax_first_at();

    if (!props)
        return -1;
    return IKsPropertySet_Get(props, GMEM(0x4a33e8), 2, 0, 0, out, 4, &got) >= 0 ? 0 : -1;
}

/* ===== 0x472040: jmp to the mixer 0x4722d0. */
int32_t GAMEAPI sound_mix_jump(void)
{
    return sound_mix_at();
}

/* ===================================================================== */
/* Files and processes. */

/* ===== 0x474360: delete "..\TEMP\" + name (DeleteFileA) unless empty;
 * the name is emptied. The original's buffer is 0x40 bytes (a longer name
 * overruns its frame); this one has room for any name. */
void GAMEAPI replay_file_delete(uint8_t *name)
{
    uint8_t buf[0x40 + 0x110];

    if (*name) {
        po_copy(buf, PTR(0x4a7f7c), 9);
        po_strcat(buf, name);
        DeleteFileA((const char *)buf);
    }
    *name = 0;
}

/* ===== 0x474470: every "..\TEMP\*.ARF" not starting with '.' deleted
 * (0x474360); FindClose even when FindFirstFileA failed (sic). */
void GAMEAPI replay_files_delete_all(void)
{
    uint8_t fd[0x140];
    uint8_t pattern[0x10];
    uint32_t h;

    po_copy(pattern, PTR(0x4b22c4), 14);
    h = (uint32_t)(uintptr_t)FindFirstFileA((const char *)pattern, fd);
    if (h != 0xffffffff) {
        do {
            if (fd[0x2c] != '.')
                replay_file_delete(fd + 0x2c);
        } while (FindNextFileA(GMEM(h), fd));
    }
    FindClose(GMEM(h));
}

/* ===== 0x4747b0: change to dir + "Direct3D\" (0x475030). */
int32_t GAMEAPI dir_change_sub(const uint8_t *dir)
{
    uint8_t buf[0x200];

    buf[0] = 0;
    if (dir)
        po_strcat(buf, dir);
    po_strcat(buf, PTR(0x4b22e0));
    return chdir_path_at(buf);
}

/* ===== 0x474840: the installer's Source (registry 0x46b650) must be a CD
 * drive (GetDriveTypeA of its first 3 characters == 5), else message 8 of
 * 0x4b0064 (or the text at 0x4b2334) unless quiet and 0. On the CD,
 * Source + "Rollcage\IDXData\ROLLCAGE.IDX" present (0x476f00): 2; else
 * message 9 (or 0x4b22ec): 1 when quiet, else OK / Cancel: 1 unless
 * Cancel. */
int32_t GAMEAPI cd_find_data_path(int32_t quiet)
{
    uint8_t msg[0x104], source[0x104], root[0x104];

    if (reg_value_read_at(0x4ac754, 0x4ac77c, source, 0x104) == -1)
        root[0] = 0;
    else {
        po_strcpy(root, source);
        root[3] = 0;
    }
    if (GetDriveTypeA((const char *)root) != 5) {
        text_line_get_at(msg, 0x104, 0x4b0064, 8);
        if (msg[0] == 0)
            po_copy(msg, PTR(0x4b2334), 0x34);
        if (!quiet)
            MessageBoxA(0, (const char *)msg, GMEM(0x4acc24), 0x10);
        return 0;
    }
    po_strcat(source, PTR(0x4ac740));
    po_strcat(source, PTR(0x4b2324));
    if (file_exists_at(source))
        return 2;
    text_line_get_at(msg, 0x104, 0x4b0064, 9);
    if (msg[0] == 0)
        po_copy(msg, PTR(0x4b22ec), 0x35);
    if (quiet)
        return 1;
    return MessageBoxA(0, (const char *)msg, GMEM(0x4acc24), 0x11) != 2;
}

/* ===== 0x474a10: CreateProcessA(app, app [+ " " args] [+ " " 0x4d4674],
 * default error mode, directory dir); returns 1. */
int32_t GAMEAPI process_launch(const uint8_t *app, const uint8_t *args, uint32_t dir)
{
    uint32_t pi[4], si[17];
    uint8_t cmd[0x104];
    int32_t k;

    po_strcpy(cmd, app);
    if (args) {
        po_strcat(cmd, PTR(0x4a8064));
        po_strcat(cmd, args);
    }
    if (G8(0x4d4674)) {
        po_strcat(cmd, PTR(0x4a8064));
        po_strcat(cmd, PTR(0x4d4674));
    }
    for (k = 0; k < 17; k++)
        si[k] = 0;
    for (k = 0; k < 4; k++)
        pi[k] = 0;
    si[0] = 0x44;
    CreateProcessA((const char *)app, (char *)cmd, 0, 0, 0, 0x4000000, 0, GMEM(dir), si, pi);
    return 1;
}

/* ===== 0x474b90: the uninstaller (registry Installer\Uninstall; -1 when
 * missing) after the question (config +0x17ce text, +0x180e caption,
 * yes / no / cancel): Yes deletes the BootSys values DataPC / DataPSX /
 * DataReplay and the Rollcage key first, Cancel returns 0; then
 * CreateProcessA(the command's first token, the command). Returns 0. */
int32_t GAMEAPI setup_program_launch(void)
{
    uint32_t pi[4], si[17];
    uint8_t command[0x104], exe[0x104];
    int32_t r, k;

    if (reg_value_read_at(0x4ac754, 0x4b2368, command, -1) == -1)
        return -1;
    r = MessageBoxA(0, GMEM(g_config + 0x17ce), GMEM(g_config + 0x180e), 0x23);
    if (r == 6) {
        reg_value_delete_at(0x4b2284, 0x4b22b0);
        reg_value_delete_at(0x4b2284, 0x4b22a8);
        reg_value_delete_at(0x4b2284, 0x4b22b8);
        reg_key_delete_at(0x4b2374);
    } else if (r != 7 && r == 2)
        return 0;
    for (k = 0; k < 4; k++)
        pi[k] = 0;
    for (k = 0; k < 17; k++)
        si[k] = 0;
    si[0] = 0x44;
    command_token_copy_at(exe, command);
    CreateProcessA((const char *)exe, (char *)command, 0, 0, 0, 0x4000000, 0, 0, si, pi);
    return 0;
}

/* ===== 0x474cf0: hand over to the front end (the chained game): replays
 * saved unless race +0x58c, config +0x22 counted, 0x4d46f4 = 0, settings
 * saved (0x474260), to "..\Direct3D\", the quoted working directory into
 * 0x5ff0c0; the ATDGFXCHAINING event made (0x46b8b0) and set, Rollcage.exe
 * /FRONTEND started there, then waited for (100 ms steps, up to 10 s past
 * the start) and freed; the event closed (0x46b850) before and after.
 * Returns 0x46b850's result. */
int32_t GAMEAPI chain_launch_and_wait(void)
{
    uint32_t event = 0;
    uint32_t start = (uint32_t)timeGetTime();

    if (U8(g_race, 0x58c) == 0)
        replay_files_save_at();
    U16(g_config, 0x22)++;
    G32(0x4d46f4) = 0;
    boot_data_save_at();
    dir_change_sub(PTR(0x4ac788));
    cwd_path_quoted(PTR(0x5ff0c0));
    chain_event_close_at();
    if (chain_event_create_at(&event, 0x4b2390) != -1) {
        chain_event_set_at(event);
        process_launch(PTR(0x4b23a0), PTR(0x4b23b0), 0x5ff0c0);
        if (WaitForSingleObject(GMEM(event), 100) != 0) {
            start += 10000;
            while ((uint32_t)timeGetTime() <= start && WaitForSingleObject(GMEM(event), 100) != 0)
                ;
        }
        chain_event_free_at(event);
    }
    return chain_event_close_at();
}

/* ===== 0x474ed0: dst = the directory path of src (quotes removed,
 * _splitpath / _makepath), with a '\' added when it had a file name;
 * dst empty when src is 0; returns dst. The locals keep the original's
 * layout (drive at +0 and the file name at +4 share the frame). */
uint8_t *GAMEAPI path_dir_make(uint8_t *dst, const uint8_t *src)
{
    uint8_t f[0x408];

    if (!dst)
        return dst;
    if (!src) {
        *dst = 0;
        return dst;
    }
    unquote_copy_at(f + 0x304, src);
    splitpath_at(f + 0x304, f, f + 0x204, f + 4, f + 0x104);
    makepath_at(dst, f, f + 0x204, f + 4, f + 0x104);
    if (f[4])
        po_strcat(dst, PTR(0x4ac784));
    return dst;
}

/* ===== 0x474fa0: dst = the working directory (_getcwd) in quotes through
 * 0x474ed0; empty when _getcwd fails; returns dst. */
uint8_t *GAMEAPI cwd_path_quoted(uint8_t *dst)
{
    uint8_t buf[0x108];

    if (!dst)
        return dst;
    buf[0] = '"';
    if (getcwd_at(buf + 1, 0x104) == 0) {
        *dst = 0;
        return dst;
    }
    po_strcat(buf, PTR(0x4b23bc));
    path_dir_make(dst, buf);
    return dst;
}
