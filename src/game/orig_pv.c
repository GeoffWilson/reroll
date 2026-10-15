/* Group PV: the original's still-reachable functions in 0x48cdd0-0x4937e0
 * (the end of the DirectPlay network file, the menu water, the snatch
 * mesh drawing, the DirectX import thunks and the start of the movie
 * player "Reaper").
 *
 *  - network: a message to one player / everyone (0x48cdd0), the
 *    duplicate packet filter (0x48cf40, 64 x 12 at 0x5d59b4), joining
 *    closed / opened (0x48d110 / 0x48d1c0: the session description's
 *    flags 0x21), the chat lines (0x48d270 add + send, 0x48d350 clear;
 *    4 x 0x41 at 0x5d6128), the send queue (0x48d390, 0x48d3e0), a
 *    connection kind's availability (0x48d450) and the player time-out
 *    (0x48d4a0);
 *  - the menu water (the record at 0x5d1fe0): set-up (0x48d4f0 with
 *    0x48d530 / 0x48d580 / 0x48d5a0 / 0x48d600), the per-frame mesh
 *    (0x4908a0: camera 0x490740, lights 0x490be0 = 0x490bf0 / 0x490ca0 /
 *    0x490e40, slopes 0x490b90) and its quads (0x490ed0);
 *  - the snatch mesh's quads and ring triangles (0x48fdd0 / 0x48fdf0) and a
 *    dead state check (0x4906f0);
 *  - the import thunks 0x4910f0 / 0x4910f6 / 0x4910fc / 0x492b88;
 *  - the movie player: a chunk read (0x492b90), the play loop (0x492dc0),
 *    open / close (0x492f90 / 0x493140), run (0x493210) and its thread
 *    (0x4932d0), the fatal error box (0x493340), DirectSound set-up /
 *    release (0x493360 / 0x493590), the sound buffer and the 10 ms timer
 *    (0x4935e0) and the timer callback 0x4937e0 (not in Ghidra's list:
 *    its address is only an immediate pushed by 0x4935e0).
 *
 * Float steps are the original's x87 instructions in inline asm (memory
 * operands, so results follow the control word as the original's do); the
 * transform / projection pointers *0x6ccd78 / *0x6ccd80 are called back to
 * back in one asm block. Windows calls are the typed imports of winapi.h,
 * COM calls typed methods (com.h, DirectSound here); the thread procedure and the timer
 * callback the original passes as immediates are ours here. Functions that
 * take their argument in ecx (MSVC fastcall) have an asm entry that calls
 * a *_c body. Everything outside the group is called by address. */
#include "../runtime.h"
#include "com.h"
#include "physics.h"
#include "gamefn.h"
#ifdef REROLL_NATIVE
#include "../vkgame.h"
#endif

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define V32(p, off) (*(volatile uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define GS16(a) (*(int16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

/* MSVC fastcall callees (ecx, edx, then the stack; the callee pops it). */
#define PV_FAST __attribute__((fastcall))

/* Out-parameters of DirectSound / COM calls: 32-bit slots in game memory
 * (native/ writes 32 bits). `call` names the parameter out_; the value is the
 * call's. */
#define OUT32(addr, call) ({ uint32_t *out_ = (uint32_t *)GMEM(addr); call; })

/* stdcall: the timer callback. */
#define PV_STD __attribute__((stdcall))

#define DPERR_BUFFERTOOSMALL 0x8877001eu

/* IDirectSound / IDirectSoundBuffer, which com.h lacks (native/ makes no
 * DirectSound object): the DirectX signatures. */
/* Game state. */
#define g_dplay G32(0x5d2060)
#define g_dplay_hr G32(0x5d6230)
#define g_net_flags G32(0x5d622c)
#define g_local_id G32(0x5d5360)
#define g_conn_count GI32(0x5d26a4)
#define CHAT_LINES 0x5d6128u /* 4 x 0x41 */
#define WATER 0x5d1fe0u        /* the menu water record */
#define SNATCH_VERTS 0x4d9af8u /* 6 rings x 12 projected vertices (0x18 each) */
#define SNATCH_CENTRE 0x4da1e8u
#define g_movie_error G32(0x4da218)
#define g_movie_busy 0x4da284u
#define g_ds_bytes G32(0x4da288)
#define g_ds G32(0x4da28c)
#define g_ds_primary G32(0x4da290)
#define g_ds_owned G32(0x4da294)
#define g_ds_primary_owned G32(0x4da298)
#define g_ds_16bit G32(0x4da29c)

/* Outside the group, by address. */
#define net_local_slot_at() GCALL(0x48cf20, net_local_slot)()
#define net_player_active_at(i) GCALL(0x48cfd0, fe_net_player_active)(i)
#define net_message_send_at(to, m, n, f) GCALL(0x48cc30, net_message_send)(to, m, n, f)
#define block_alloc_at(n, t) ((uint32_t)GCALL(0x476ae0, global_block_alloc)(n, t))
#define block_free_at(p) GCALL(0x476b40, global_block_free)(p)
#define game_time_at() ((uint32_t)GCALL(0x43692a, game_time_get)())
#define net_player_remove_at(i) GCALL(0x48bed0, net_player_remove)(i)
#define heap_alloc_at(n) ((uint32_t)GCALL(0x4445e0, kr_malloc)(n))
#define textures_reset_at() GCALL(0x4291f0, dx_error_ignore)()
#define rot_matrix_at(ang, m) GCALL(0x426f80, matrix_from_angles)(GMEM(ang), GMEM(m))
#define normal_colour_at(n, c, out) GCALL(0x4292b0, light_colour_at)(GMEM(n), GMEM(c), GMEM(out))
#define light_matrix_set_at(m) GCALL(0x428940, light_matrix_load)(GMEM(m))
#define ot_add_at(ot, p) GCALL(0x427ab0, ot_add_prim)(ot, GMEM(p))
#define crt_fread_at(b, s, n, f) GCALL(0x49908e, crtc_fread)(GMEM(b), s, n, GMEM(f))
#define crt_malloc_at(n) A(GCALL(0x4982c2, msvc_malloc)(n))
#define crt_free_at(p) GCALL(0x49827a, msvc_free)(GMEM(p))
#define crt_clock_at() ((uint32_t)GCALL(0x499016, msvc_clock)())
#define crt_fseek_at(f, o, w) GCALL(0x4991a5, crtc_fseek)(GMEM(f), o, w)
#define crt_calloc_at(n, s) A(GCALL(0x49946c, msvc_calloc)(n, s))
#define crt_fopen_at(p, m) A(GCALL(0x499459, crtc_fopen)(GMEM(p), GMEM(m)))
#define crt_strncmp_at(a, b, n) GCALL(0x4993f0, msvc_strncmp)(GMEM(a), GMEM(b), n)
#define crt_ftell_at(f) ((uint32_t)GCALL(0x49925e, crtc_ftell)(GMEM(f)))
#define crt_fclose_at(f) GCALL(0x4994f9, crtc_fclose)(GMEM(f))
#define crt_exit_at(c) GCALL(0x498c89, msvc_exit)(c)
#define adpcm_mono_at(a, src, dst, n, s, i) GCALL(0x495000, fmv_adpcm_mono_c)(a, GMEM(src), GMEM(dst), n, GMEM(s), GMEM(i))
#define adpcm_stereo_at(a, src, dst, n, s, i) GCALL(0x4950a3, fmv_adpcm_stereo_c)(a, GMEM(src), GMEM(dst), n, GMEM(s), GMEM(i))
#define movie_frame_decode_at(m, slot) GCALL(0x494f10, fmv_video_frame)(GMEM(m), GMEM(slot))
#define movie_blit16_at(m) GCALL(0x494040, fmv_present_16)(GMEM(m))
#define movie_blit24_at(m) GCALL(0x4943b0, fmv_present_24)(GMEM(m))
#define movie_blit_other_at(m) GCALL(0x494740, fmv_present_32)(GMEM(m))
#define movie_frame_done_at(m) GCALL(0x494d30, fmv_frames_swap)(GMEM(m))
#define movie_video_init_at(m) GCALL(0x494d40, fmv_video_init)(GMEM(m))
#define movie_video_free_at(m) GCALL(0x494ed0, fmv_video_free)(GMEM(m))
#define movie_audio_stop_at(m) GCALL(0x493a10, fmv_audio_close)(GMEM(m))
#define movie_tables_init_at(buf, out, n) GCALL(0x495220, fmv_rle_unpack)(GMEM(buf), GMEM(out), n)

/* This group's functions (also in build/groups/pv_protos.txt). */
int32_t GAMEAPI net_message_send_to(int32_t mode, uint32_t to, uint32_t msg, uint32_t size, uint32_t flags); /* 0x48cdd0 */
int32_t GAMEAPI net_packet_filter(const uint32_t *packet, uint32_t from); /* 0x48cf40 */
int32_t GAMEAPI net_session_lock(void); /* 0x48d110 */
int32_t GAMEAPI net_session_unlock(void); /* 0x48d1c0 */
int32_t GAMEAPI net_chat_send(char *text); /* 0x48d270 */
void GAMEAPI net_chat_clear(void); /* 0x48d350 */
int32_t GAMEAPI net_send_queue_low(void); /* 0x48d390 */
int32_t GAMEAPI net_message_queue_count(void); /* 0x48d3e0 */
int32_t GAMEAPI net_connection_available(uint32_t kind); /* 0x48d450 */
void GAMEAPI net_players_timeout(void); /* 0x48d4a0 */
void GAMEAPI menu_water_init(void); /* 0x48d4f0 */
void GAMEAPI water_mesh_alloc(uint32_t *w); /* 0x48d530 */
void GAMEAPI water_mesh_reset(uint8_t *w); /* 0x48d580 */
void GAMEAPI water_textures_set(uint8_t *w, uint32_t name_a, uint32_t name_b); /* 0x48d5a0 */
void GAMEAPI water_texture_get(uint32_t *dst); /* 0x48d600 */
void GAMEAPI snatch_draw(void); /* 0x48fdd0 */
void GAMEAPI snatch_mesh_draw(uint8_t *body); /* 0x48fdf0 */
void GAMEAPI snatch_state_check(void); /* 0x4906f0 */
void GAMEAPI menu_camera_set(const int32_t *pos, const uint32_t *matrix); /* 0x490740 */
void GAMEAPI water_mesh_transform(uint32_t *w); /* 0x4908a0 */
int32_t GAMEAPI water_slope_lookup(int32_t v); /* 0x490b90 */
void GAMEAPI water_lights_set(void); /* 0x490be0 */
void GAMEAPI water_light_matrix_set(void); /* 0x490bf0 */
void GAMEAPI water_colour_matrix_set(void); /* 0x490ca0 */
void GAMEAPI water_back_colour_set(void); /* 0x490e40 */
void GAMEAPI water_mesh_draw(void); /* 0x490ed0 */
void ddraw_create_thunk(void); /* 0x4910f0 */
void ddraw_enumerate_thunk(void); /* 0x4910f6 */
void dinput_create_thunk(void); /* 0x4910fc */
void dplay_lobby_create_thunk(void); /* 0x492b88 */
void movie_chunk_read(void); /* 0x492b90: asm entry, movie in ecx */
int32_t GAMEAPI movie_chunk_read_c(uint8_t *m);
int32_t GAMEAPI movie_play_loop(uint8_t *m); /* 0x492dc0 */
uint32_t GAMEAPI movie_open(uint32_t path, uint32_t window, const uint32_t *config, uint32_t offset); /* 0x492f90 */
int32_t GAMEAPI movie_close(uint8_t *m); /* 0x493140 */
uint32_t GAMEAPI movie_player_run(uint32_t path, uint32_t window, const uint32_t *config); /* 0x493210 */
int32_t GAMEAPI movie_thread_proc(uint8_t *m); /* 0x4932d0 */
void movie_fatal_error(void); /* 0x493340: asm entry, text in ecx */
void GAMEAPI movie_fatal_error_c(uint32_t text);
void movie_sound_init(void); /* 0x493360: asm entry, movie in ecx */
void GAMEAPI movie_sound_init_c(uint8_t *m);
void movie_sound_release(void); /* 0x493590: asm entry, movie in ecx */
void GAMEAPI movie_sound_release_c(uint8_t *m);
void movie_sound_start(void); /* 0x4935e0: asm entry, movie in ecx */
int32_t GAMEAPI movie_sound_start_c(uint8_t *m);
void PV_STD movie_sound_timer(uint32_t id, uint32_t msg, uint32_t user, uint32_t dw1, uint32_t dw2); /* 0x4937e0 */

/* Every x87 register, for asm blocks that call out or use the FPU stack. */
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* ---- x87 helpers: the original's instructions ---- */

/* fld a; fcomp b (floats); fnstsw ax: the status word's high byte. */
static inline uint32_t pv_fcmp(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (uint32_t)(sw >> 8);
}

/* fld a; fadd b; fstp (floats). */
static inline uint32_t pv_fadd(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fdiv b; fstp t; fld t; fistp: the depth before the >> 5. */
static inline int32_t pv_fdiv_fist(uint32_t a, uint32_t b)
{
    uint32_t t;
    int32_t r;
    __asm__ volatile("flds %2\n\tfdivs %3\n\tfstps %1\n\tflds %1\n\tfistpl %0" : "=m"(r), "=m"(t) : "m"(a), "m"(b)
                     : "st");
    return r;
}

/* fld a; fistp. */
static inline int32_t pv_fist(uint32_t a)
{
    int32_t r;
    __asm__ volatile("flds %1\n\tfistpl %0" : "=m"(r) : "m"(a) : "st");
    return r;
}

/* fild dword; fstp float. */
static inline uint32_t pv_fild(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild word src; fstp t; fld t; fmul [0x4a3310]; fstp: a matrix entry
 * scaled to a float. */
static inline uint32_t pv_fild16_scale(uint32_t src)
{
    uint32_t t, r;
    __asm__ volatile("filds (%2)\n\tfstps %1\n\tflds %1\n\tfmuls 0x4a3310\n\tfstps %0"
                     : "=m"(r), "=m"(t)
                     : "r"(PTR(src))
                     : "st", "memory");
    return r;
}

/* The transform pointer *0x6ccd78 (out) then the projection *0x6ccd80,
 * which takes the transform's st(0). */
static inline void pv_transform_project(uint32_t out)
{
#if defined(__i386__)
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\taddl $4, %%esp\n\tcall *0x6ccd80"
                     :
                     : "S"(out)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    /* x86-64: the transform (*0x6ccd78) returns what the original leaves in
     * st(0); the projection (*0x6ccd80) takes it as its argument. */
    float h = ((float(GAMEAPI *)(uint8_t *))(uintptr_t)G32(0x6ccd78))(PTR(out));

    ((void(GAMEAPI *)(float))(uintptr_t)G32(0x6ccd80))(h);
#endif
}

/* idiv as the original: traps on a zero divisor where it does (a C
 * division by zero would let the compiler drop or move it). */
static inline int32_t pv_idiv(int32_t a, int32_t b)
{
    int32_t q, r;
    __asm__ volatile("cltd\n\tidivl %3" : "=a"(q), "=&d"(r) : "a"(a), "r"(b) : "cc");
    (void)r;
    return q;
}

static void pv_copy6(uint32_t dst, uint32_t src)
{
    int i;
    for (i = 0; i < 6; i++)
        U32(dst, 4 * i) = U32(src, 4 * i);
}

/* rep movsd / movsb: a forward copy. */
static void pv_copy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

static uint32_t pv_strlen(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* The inlined strcpy: the length first, then a forward copy. */
static void pv_strcpy(uint8_t *d, const uint8_t *s)
{
    pv_copy(d, s, pv_strlen((const char *)s) + 1);
}

/* The depth slot of an ordering-table entry: > 0x100 halved towards
 * 0x180, then again towards 0x1ff; 1 at most for <= 0. */
static uint32_t pv_depth_slot(int32_t d)
{
    if (d <= 0)
        return 1;
    if (d > 0x100) {
        d = d / 2 + 0x80;
        if (d > 0x180) {
            d = d / 2 + 0xc0;
            if (d > 0x1ff)
                d = 0x1ff;
        }
    }
    return (uint32_t)d;
}

/* Link a packet at the head of ordering-table slot d. */
static void pv_ot_link(uint32_t d, uint32_t packet)
{
    uint32_t ot = G32(0x6cce84) ? G32(0x6cdd80 + 4 * d) : G32(0x6cd510 + 4 * d);

    ot &= 0x7fffffff;
    U32(packet, 0) = U32(ot, 0);
    U32(ot, 0) = packet;
}

/* =================================================================
 * network
 * ================================================================= */

/* 0x48cdd0: a message (0x48cc30) when the local player is active (else
 * 1): mode 2 to every other active player (0 at the first failure), mode
 * 1 to id 1 (the host), otherwise to `to`. */
int32_t GAMEAPI net_message_send_to(int32_t mode, uint32_t to, uint32_t msg, uint32_t size, uint32_t flags)
{
    uint32_t p;

    if (net_player_active_at(net_local_slot_at()) == 0)
        return 1;
    if (mode == 2) {
        for (p = 0x5d2cd4; p < 0x5d535c; p += 0x66c) {
            if (G32(p - 4) != 0 && G32(p) != g_local_id)
                if (net_message_send_at(G32(p), msg, size, flags) == 0)
                    return 0;
        }
        return 1;
    }
    return net_message_send_at(mode == 1 ? 1 : to, msg, size, flags);
}

/* 0x48cf40: 1 when the packet's first 8 bytes and the sender are in the
 * 64-entry ring at 0x5d59b4 (12 bytes each), else recorded at the ring
 * index 0x5d5cb4 and 0. */
int32_t GAMEAPI net_packet_filter(const uint32_t *packet, uint32_t from)
{
    uint32_t e, i;

    for (e = 0x5d59bc; e < 0x5d5cbc; e += 0xc)
        if (G32(e - 8) == packet[0] && G32(e - 4) == packet[1] && G32(e) == from)
            return 1;
    i = G32(0x5d5cb4);
    G32(0x5d59b4 + 12 * i) = packet[0];
    G32(0x5d59b8 + 12 * i) = packet[1];
    G32(0x5d59bc + 12 * G32(0x5d5cb4)) = from;
    G32(0x5d5cb4) = G32(0x5d5cb4) + 1;
    G32(0x5d5cb4) = G32(0x5d5cb4) & 0x3f;
    return 0;
}

/* 0x48d110 / 0x48d1c0: the session description (GetSessionDesc for the
 * size, a block 0x476ae0, GetSessionDesc), flags 0x21 (no new players, no
 * joining) set or cleared, SetSessionDesc, the block freed. 1 when done.
 * The size's first value is a leftover register in the original when the
 * first call succeeds (0 here). */
static int32_t net_session_flags(int set)
{
    uint32_t size = 0, desc;

    g_dplay_hr = IDirectPlay4A_GetSessionDesc(g_dplay, 0, A(&size));
    if (g_dplay_hr != 0 && g_dplay_hr != DPERR_BUFFERTOOSMALL)
        return 0;
    desc = block_alloc_at(size, 0);
    if (desc == 0)
        return 0;
    g_dplay_hr = IDirectPlay4A_GetSessionDesc(g_dplay, desc, A(&size));
    if (g_dplay_hr != 0)
        return 0;
    if (set)
        U32(desc, 4) |= 0x21;
    else
        U32(desc, 4) &= ~0x21u;
    g_dplay_hr = IDirectPlay4A_SetSessionDesc(g_dplay, desc, 0);
    if (g_dplay_hr != 0)
        return 0;
    block_free_at(desc);
    return 1;
}

int32_t GAMEAPI net_session_lock(void)
{
    return net_session_flags(1);
}

int32_t GAMEAPI net_session_unlock(void)
{
    return net_session_flags(0);
}

/* 0x48d270: with chat on (0x5d622c bit 0): a text longer than 0x40 gets
 * '0' (sic: not a terminator) at 0x40; the chat lines scroll down and the
 * text goes to the first; with bit 1, SendChatMessage of it to everyone.
 * Returns 1. */
int32_t GAMEAPI net_chat_send(char *text)
{
    uint32_t d, s;
    uint32_t chat[3];

    if (g_net_flags & 1) {
        if (pv_strlen(text) > 0x40)
            text[0x40] = '0'; /* sic */
        d = CHAT_LINES + 3 * 0x41;
        do {
            s = d - 0x41;
            pv_strcpy(PTR(d), PTR(s));
            d = s;
        } while ((int32_t)s > (int32_t)CHAT_LINES);
        pv_strcpy(PTR(CHAT_LINES), (const uint8_t *)text);
        if (g_net_flags & 2) {
            chat[0] = 0xc;
            chat[1] = 0;
            chat[2] = CHAT_LINES;
            IDirectPlay4A_SendChatMessage(g_dplay, g_local_id, 0, 0, A(chat));
        }
    }
    return 1;
}

/* 0x48d350: the four chat lines emptied. */
void GAMEAPI net_chat_clear(void)
{
    uint32_t p;

    for (p = CHAT_LINES; p < CHAT_LINES + 4 * 0x41; p += 0x41)
        G8(p) = 0;
}

/* 0x48d390: 1 when the send queue (GetMessageQueue, flag 1) holds fewer
 * than 2 messages; 0 on failure. */
int32_t GAMEAPI net_send_queue_low(void)
{
    uint32_t count = 0, bytes = 0;

    g_dplay_hr = IDirectPlay4A_GetMessageQueue(g_dplay, 0, 0, 1, &count, &bytes);
    if (g_dplay_hr != 0)
        return 0;
    return count < 2;
}

/* 0x48d3e0: the messages in the send queue (flag 1) plus the receive queue
 * (flag 2); 0 when the second call fails. The first count is a leftover
 * stack value in the original if its call fails without writing it (0
 * here). */
int32_t GAMEAPI net_message_queue_count(void)
{
    uint32_t bytes = 0, recv = 0, sent = 0;

    g_dplay_hr = IDirectPlay4A_GetMessageQueue(g_dplay, 0, 0, 1, &sent, &bytes);
    g_dplay_hr = IDirectPlay4A_GetMessageQueue(g_dplay, 0, 0, 2, &recv, &bytes);
    if (g_dplay_hr != 0)
        return 0;
    return (int32_t)(recv + sent);
}

/* 0x48d450: 1 when an enumerated connection (0x5d2064, 0xa0 each) of this
 * kind (+0x18) works (+0x1c). */
int32_t GAMEAPI net_connection_available(uint32_t kind)
{
    int32_t i;
    uint32_t c = 0x5d2080;

    for (i = 0; i < g_conn_count; i++, c += 0xa0)
        if (G32(c - 4) == kind && G32(c) != 0)
            return 1;
    return 0;
}

/* 0x48d4a0: other players not heard from (+0x664) for 3 s are removed
 * (0x48bed0). */
void GAMEAPI net_players_timeout(void)
{
    int32_t me = net_local_slot_at(), i = 0;
    uint32_t limit = game_time_at() - 3000, p;

    for (p = 0x5d3334; p < 0x5d59bc; p += 0x66c, i++)
        if (i != me && G32(p - 0x664) != 0 && G32(p) < limit)
            net_player_remove_at(i);
}

/* =================================================================
 * menu water
 * ================================================================= */

/* 0x48d4f0: the menu water: mesh, textures reset (0x4291f0), state, the
 * reflection and sea textures. */
void GAMEAPI menu_water_init(void)
{
    water_mesh_alloc((uint32_t *)PTR(WATER));
    textures_reset_at();
    water_mesh_reset(PTR(WATER));
    water_textures_set(PTR(WATER), 0x4b2cf4, 0x4b2d04);
}

/* 0x48d530: 1500 x 1500 at 750, 900 points of 0x38 (0x4445e0) with their
 * first two words cleared, +0x10 = 1, the cursor cell +0x2c / +0x30 = 0. */
void GAMEAPI water_mesh_alloc(uint32_t *w)
{
    uint32_t off;

    w[1] = 0x5dc;
    w[2] = 0x5dc;
    w[3] = 0x2ee;
    w[0] = heap_alloc_at(0xc4e0);
    for (off = 0; off < 0xc4e0; off += 0x38) {
        U32(w[0], off) = 0;
        U32(w[0], off + 4) = 0;
    }
    w[4] = 1;
    w[0xb] = 0;
    w[0xc] = 0;
}

/* 0x48d580: position +0x34..+0x3c and angles +0x64..+0x68 cleared. */
void GAMEAPI water_mesh_reset(uint8_t *w)
{
    U32(w, 0x3c) = 0;
    U32(w, 0x38) = 0;
    U32(w, 0x34) = 0;
    U16(w, 0x68) = 0;
    U16(w, 0x66) = 0;
    U16(w, 0x64) = 0;
}

/* 0x48d5a0: the two texture records +0x14 and +0x20 (0x48d600, which
 * ignores the names and sizes it is given). */
void GAMEAPI water_textures_set(uint8_t *w, uint32_t name_a, uint32_t name_b)
{
    (void)name_a;
    (void)name_b;
    water_texture_get((uint32_t *)(w + 0x14));
    water_texture_get((uint32_t *)(w + 0x20));
}

/* 0x48d600: a texture record (2 dwords, 2 words) copied from the texture
 * table *0x4be340 + 0x3a0. */
void GAMEAPI water_texture_get(uint32_t *dst)
{
    uint32_t t = G32(0x4be340) + 0x3a0;

    dst[0] = U32(t, 0);
    dst[1] = U32(t, 4);
    U16(dst, 8) = U16(t, 8);
    U16(dst, 0xa) = U16(t, 0xa);
}

/* 0x490740: the camera: translation (ints at 0x6ccbf4, floats at
 * 0x6ccd0c) and the rotation matrix (5 dwords at 0x6ccbe0; its nine words
 * scaled by *0x4a3310 into 0x6ccd1c.., three per row 8 bytes apart). */
void GAMEAPI menu_camera_set(const int32_t *pos, const uint32_t *matrix)
{
    static const uint32_t dst[9] = {0x6ccd1c, 0x6ccd20, 0x6ccd24, 0x6ccd2c, 0x6ccd30,
                                    0x6ccd34, 0x6ccd3c, 0x6ccd40, 0x6ccd44};
    int i;

    G32(0x6ccbf4) = (uint32_t)pos[0];
    G32(0x6ccbf8) = (uint32_t)pos[1];
    G32(0x6ccbfc) = (uint32_t)pos[2];
    G32(0x6ccd0c) = pv_fild(pos[0]);
    G32(0x6ccd10) = pv_fild(GI32(0x6ccbf8));
    G32(0x6ccd14) = pv_fild(GI32(0x6ccbfc));
    for (i = 0; i < 5; i++)
        G32(0x6ccbe0 + 4 * i) = matrix[i];
    for (i = 0; i < 9; i++)
        G32(dst[i]) = pv_fild16_scale(0x6ccbe0 + 2 * i);
}

/* 0x490b90: the slope table 0x4b2d74 at |v| * 256 / 150 (at most 0xff),
 * with v's sign. */
int32_t GAMEAPI water_slope_lookup(int32_t v)
{
    int neg = 0;
    int32_t i, r;

    if (v < 0) {
        v = -v;
        neg = 1;
    }
    i = (int32_t)((uint32_t)v << 8) / 150;
    if (i >= 0x100)
        i = 0xff;
    r = GS16(0x4b2d74 + 2 * (uint32_t)i);
    return neg ? -r : r;
}

/* 0x490bf0: the light matrix (rows of 0x4b2f78 / 0x4b2f88 / 0x4b2f98,
 * transposed) through 0x428940. */
void GAMEAPI water_light_matrix_set(void)
{
    uint16_t m[10] = {0};

    m[0] = G16(0x4b2f78);
    m[3] = G16(0x4b2f7a);
    m[6] = G16(0x4b2f7c);
    m[1] = G16(0x4b2f88);
    m[4] = G16(0x4b2f8a);
    m[7] = G16(0x4b2f8c);
    m[2] = G16(0x4b2f98);
    m[5] = G16(0x4b2f9a);
    m[8] = G16(0x4b2f9c);
    light_matrix_set_at(m);
}

/* 0x490ca0: the light colour matrix (0x4b2f80.. / 0x4b2f90.. / 0x4b2fa0..)
 * to 0x6ccc00 and, scaled by *0x4a3310, to the floats 0x6ccdd0... */
void GAMEAPI water_colour_matrix_set(void)
{
    static const uint32_t dst[9] = {0x6ccdd0, 0x6ccdd4, 0x6ccdd8, 0x6ccde0, 0x6ccde4,
                                    0x6ccde8, 0x6ccdf0, 0x6ccdf4, 0x6ccdf8};
    uint16_t m[10] = {0};
    int i;

    m[0] = G16(0x4b2f80);
    m[1] = G16(0x4b2f82);
    m[2] = G16(0x4b2f84);
    m[3] = G16(0x4b2f90);
    m[4] = G16(0x4b2f92);
    m[5] = G16(0x4b2f94);
    m[6] = G16(0x4b2fa0);
    m[7] = G16(0x4b2fa2);
    m[8] = G16(0x4b2fa4);
    for (i = 0; i < 5; i++)
        G32(0x6ccc00 + 4 * i) = (uint32_t)m[2 * i] | (uint32_t)m[2 * i + 1] << 16;
    for (i = 0; i < 9; i++)
        G32(dst[i]) = pv_fild16_scale(0x6ccc00 + 2 * i);
}

/* 0x490e40: the back colour (0x80, 0x80, 0x80) as 0x80f each, and as the
 * floats 2063.0. */
void GAMEAPI water_back_colour_set(void)
{
    G32(0x6ccc14) = 0x80f;
    G32(0x6ccc18) = 0x80f;
    G32(0x6ccc1c) = 0x80f;
    G32(0x6cce38) = pv_fild(GI32(0x6ccc1c));
    G32(0x6cce34) = pv_fild(GI32(0x6ccc18));
    G32(0x6cce30) = pv_fild(GI32(0x6ccc14));
}

/* 0x490be0: the water's lights. */
void GAMEAPI water_lights_set(void)
{
    water_light_matrix_set();
    water_colour_matrix_set();
    water_back_colour_set();
}

/* 0x4908a0: the water mesh for this frame: lights, the camera (angles
 * +0x64 = (0x400, 0, 0) through 0x426f80 into +0x44, position (0, 100,
 * 750)), then every point (cols x rows of 0x38 from +0) projected from
 * its grid position and height (+4 >> 11) into +0xc, its normal (+0x2c)
 * from the slopes of +0x24 / +0x28, texture coordinates +0x34 / +0x35
 * from the grid position less the normal, its colour +8 (0x4292b0); the
 * cursor cell (+0x2c, +0x30) red. The vertex's z word goes to 0x6ccc68
 * with a stack leftover above it in the original (0 here). */
void GAMEAPI water_mesh_transform(uint32_t *w)
{
    uint32_t pts = w[0], cols, rows, sel_col, sel_row, row, col;
    int32_t colstep, rowstep, du, dv, uacc, vacc, t, n0, n2, sa, a, b;
    uint32_t x, x0, z, out[6];
    int i;

    cols = (uint32_t)((int32_t)(w[1] * 3) / 150);
    rows = (uint32_t)((int32_t)(w[2] * 3) / 150);
    sel_row = w[0xc];
    sel_col = w[0xb];
    water_lights_set();
    U16(w, 0x64) = 0x400;
    U16(w, 0x68) = 0;
    U16(w, 0x66) = 0;
    w[0xd] = 0;
    w[0xe] = 100;
    w[0xf] = 0x2ee;
    rot_matrix_at((uint8_t *)w + 0x64, (uint8_t *)w + 0x44);
    menu_camera_set((const int32_t *)(w + 0xd), w + 0x11);
    colstep = pv_idiv((int32_t)w[1], (int32_t)cols);
    vacc = 0;
    row = 0;
    rowstep = pv_idiv((int32_t)w[2], (int32_t)rows);
    z = (uint32_t)(-(rowstep * (int32_t)rows)) >> 1;
    du = pv_idiv(0x10000, (int32_t)cols);
    dv = pv_idiv(0x10000, (int32_t)rows);
    if (rows == 0)
        return;
    x0 = (uint32_t)(-(colstep * (int32_t)cols)) >> 1;
    do {
        uacc = 0;
        col = 0;
        x = x0;
        if (cols != 0) {
            do {
                G32(0x6ccc64) = (x & 0xffff) | (uint32_t)(I32(pts, 4) >> 11) << 16;
                G32(0x6ccc68) = z & 0xffff; /* the high word is a stack leftover in the original */
                pv_transform_project(A(out));
                a = water_slope_lookup(I32(pts, 0x24) >> 11);
                a = (a >> 7) & 0xff;
                b = water_slope_lookup(I32(pts, 0x28) >> 11);
                sa = GS16(0x4b2578 + 2 * a);
                b = (b >> 7) & 0xff;
                U16(pts, 0x2c) = (uint16_t)(GS16(0x4b24f8 + 2 * a) >> 2);
                U16(pts, 0x2e) = (uint16_t)(-(GS16(0x4b2578 + 2 * b) * sa) >> 16);
                U16(pts, 0x30) = (uint16_t)((GS16(0x4b24f8 + 2 * b) * sa) >> 16);
                for (i = 0; i < 6; i++)
                    U32(pts, 0xc + 4 * i) = out[i];
                n0 = S16(pts, 0x2c);
                t = uacc - n0 * 4;
                if (t < 0)
                    U8(pts, 0x34) = 0;
                else if ((int32_t)((uint32_t)t & 0xffffff00) > 0xff00)
                    U8(pts, 0x34) = 0xff;
                else
                    U8(pts, 0x34) = (uint8_t)(t >> 8);
                n2 = S16(pts, 0x30);
                t = vacc - n2 * 4;
                if (t < 0)
                    U8(pts, 0x35) = 0;
                else if ((int32_t)((uint32_t)t & 0xffffff00) > 0xff00)
                    U8(pts, 0x35) = 0xff;
                else
                    U8(pts, 0x35) = (uint8_t)(t >> 8);
                normal_colour_at(pts + 0x2c, 0x4da20c, pts + 8);
                if (col == sel_col && row == sel_row) {
                    U8(pts, 8) = 0xff;
                    U8(pts, 9) = 0;
                    U8(pts, 10) = 0;
                }
                x += (uint32_t)colstep;
                col++;
                pts += 0x38;
                uacc += du;
            } while (col < cols);
        }
        row++;
        z += (uint32_t)rowstep;
        vacc += dv;
    } while (row < rows);
}

/* 0x490ed0: with the water on (0x4b2fb0), its quads (0x4db620, 0x84
 * each, code 0x8e, texture word +0x1a): grey by the highlight level
 * 0x4da214, depth from the first corner's z (1..0x1ff), into the second
 * ordering table (0x427ab0). The mesh is transformed first (0x4908a0). */
void GAMEAPI water_mesh_draw(void)
{
    uint32_t pts = G32(WATER), cols, rows, level, colour, r, c, a, e, pk, d;
    uint16_t tex = G16(WATER + 0x1a);

    cols = (uint32_t)((int32_t)(G32(WATER + 4) * 3) / 150);
    rows = (uint32_t)((int32_t)(G32(WATER + 8) * 3) / 150);
    level = (G32(0x4da214) << 7) >> 16;
    if (G32(0x4b2fb0) == 0)
        return;
    if (level > 0xff)
        level = 0xff;
    colour = level | (level << 8 | level) << 8;
    water_mesh_transform((uint32_t *)PTR(WATER));
    pk = 0x4db620;
    for (r = rows - 1; r != 0; r--) {
        c = cols - 1;
        if (c != 0) {
            a = pts + 0x44;
            e = pts + 0x38 * cols + 0x6c;
            pts += (cols - 1) * 0x38;
            do {
                uint32_t head = U32(pk, 0);
                U32(pk, 4) = colour;
                U32(pk, 0x24) = colour;
                U32(pk, 0x44) = colour;
                U32(pk, 0) = (head & 0xffffff) | 0xc000000;
                U32(pk, 0x64) = colour;
                U8(pk, 7) = 0x8e;
                U16(pk, 0x42) = tex;
                U16(pk, 0x20) = U16(a, -0x10);
                U16(pk, 0x40) = U16(a, 0x28);
                U16(pk, 0x60) = U16(e, -0x38);
                U16(pk, 0x80) = U16(e, 0);
                pv_copy6(pk + 8, a - 0x38);
                pv_copy6(pk + 0x28, a);
                pv_copy6(pk + 0x48, e - 0x60);
                pv_copy6(pk + 0x68, e - 0x28);
                U32(pk, 8) = 0x40;
                U32(pk, 0x6c) = 0x459c4000;
                U32(pk, 0x4c) = 0x459c4000;
                U32(pk, 0x2c) = 0x459c4000;
                U32(pk, 0xc) = 0x459c4000;
                d = (uint32_t)pv_fist(U32(a, -0x34));
                if (d < 1)
                    d = 1;
                else if (d > 0x1ff)
                    d = 0x1ff;
                ot_add_at(0x6cdd80 + 4 * d, pk);
                a += 0x38;
                e += 0x38;
                pk += 0x84;
            } while (--c != 0);
        }
        pts += 0x38;
    }
}

/* =================================================================
 * the snatch mesh
 * ================================================================= */

/* 0x48fdd0: the first slot's snatch body drawn while active. */
void GAMEAPI snatch_draw(void)
{
    uint32_t body = G32(0x5d1fc0);

    if (U32(body, 0x188) != 0)
        snatch_mesh_draw(PTR(body));
}

/* 0x48fdf0: the snatch mesh: its 72 points (+0x344, 6 rings of 12) and
 * centre (+0x6a4) projected to 0x4d9af8 / 0x4da1e8; 5 x 12 quads (+0x6ac,
 * 0x84 each, colours per ring from +0x2a1c, the texture of frame +0x340
 * of 4) not off-screen together and in front, depth from their mean z;
 * with the option on (config +0x28, 0x4b24a0 != -1, 0x6ccd84 bit 2 off)
 * the 12 triangles of the first ring to the centre (+0x259c, 0x60 each),
 * grey by +0x2a37 or the timer +0x180. The triangles' farthest z compares
 * the last ring's vertex (0x4da09c) where the quads use the first (sic). */
void GAMEAPI snatch_mesh_draw(uint8_t *body)
{
    uint32_t i, j, jn, row, tex, d0, d1, c0, c1, ca, cb, p, v00, v01, v10, v11, sum, max, k;
    uint16_t w8, wa;
    uint8_t b0, b1;
    int32_t level;

    for (i = 0; i < 0x48; i++) {
        G32(0x6ccc64) = U32(body, 0x344 + 8 * i);
        G32(0x6ccc68) = U32(body, 0x348 + 8 * i);
        pv_transform_project(SNATCH_VERTS + 0x18 * i);
    }
    G32(0x6ccc64) = U32(body, 0x6a4);
    G32(0x6ccc68) = U32(body, 0x6a8);
    pv_transform_project(SNATCH_CENTRE);
    tex = ((uint32_t)U16(G32(0x6c1eb0), 0x2e) + 0x5c) * 16 + G32(0x6c1e14);
    if ((int8_t)U8(body, 0x340) >= 4)
        U8(body, 0x340) = 0;
    tex += (uint32_t)((int32_t)(int8_t)U8(body, 0x340) * 16);
    d0 = U32(tex, 0);
    d1 = U32(tex, 4);
    w8 = U16(tex, 8);
    wa = U16(tex, 0xa);
    p = A(body) + 0x6ac;
    for (row = 0; row < 5; row++) {
        ca = U32(body, 0x2a1c + 4 * row);
        cb = U32(body, 0x2a20 + 4 * row);
        b0 = (uint8_t)(ca >> 24);
        c0 = (ca & 0xffffff) | 0x8e000000;
        c1 = (cb & 0xffffff) | 0x8e000000;
        b1 = (uint8_t)(cb >> 24);
        for (j = 1; j <= 12; j++, p += 0x84) {
            jn = j < 12 ? j : 0;
            v00 = SNATCH_VERTS + row * 0x120 + (j - 1) * 0x18;
            v01 = SNATCH_VERTS + row * 0x120 + jn * 0x18;
            v10 = v00 + 0x120;
            v11 = v01 + 0x120;
            if (G32(v10) & G32(v11) & G32(v01) & G32(v00))
                continue;
            sum = max = G32(v00 + 4);
            sum = pv_fadd(sum, G32(v01 + 4));
            if (!(pv_fcmp(G32(v01 + 4), max) & 1))
                max = G32(v01 + 4);
            sum = pv_fadd(sum, G32(v10 + 4));
            if (!(pv_fcmp(G32(v10 + 4), max) & 1))
                max = G32(v10 + 4);
            sum = pv_fadd(sum, G32(v11 + 4));
            if (!(pv_fcmp(G32(v11 + 4), max) & 1))
                max = G32(v11 + 4);
            if (pv_fcmp(max, G32(0x4a34b8)) & 0x41)
                continue;
            k = pv_depth_slot(pv_fdiv_fist(sum, G32(0x4a3344)) >> 5);
            pv_copy6(p + 8, v00);
            pv_copy6(p + 0x28, v01);
            pv_copy6(p + 0x48, v10);
            pv_copy6(p + 0x68, v11);
            U32(p, 0x40) = d1;
            U16(p, 0x80) = wa;
            U32(p, 0x20) = d0;
            U32(p, 4) = c0;
            U32(p, 0x24) = c0;
            U16(p, 0x60) = w8;
            U32(p, 0x44) = c1;
            U32(p, 0x64) = c1;
            U8(p, 0x27) = b0;
            U16(p, 0x22) = b0;
            U8(p, 0x47) = b1;
            U8(p, 0x67) = b1;
            pv_ot_link(k, p);
        }
    }
    if (U8(G32(0x4b2198), 0x28) == 0 || GI32(0x4b24a0) == -1 || (G8(0x6ccd84) & 4))
        return;
    if (U8(body, 0x2a37) != 0) {
        level = (int32_t)U8(body, 0x2a37) * 4 + 0x30;
        if (level >= 0xff)
            level = 0xff;
    } else if (I32(body, 0x180) < 0x28) {
        level = ((int32_t)((uint32_t)I32(body, 0x180) << 8) / 40 * 48) >> 8;
    } else {
        level = 0x30;
    }
    c0 = (uint32_t)level | (((uint32_t)level | 0xffff9600) << 8 | (uint32_t)level) << 8;
    p = A(body) + 0x259c;
    for (j = 1; j <= 12; j++, p += 0x60) {
        jn = j < 12 ? j : 0;
        v00 = SNATCH_VERTS + (j - 1) * 0x18;
        v01 = SNATCH_VERTS + jn * 0x18;
        if (G32(v01) & G32(v00) & G32(SNATCH_CENTRE))
            continue;
        sum = max = G32(v00 + 4);
        sum = pv_fadd(sum, G32(v01 + 4));
        if (!(pv_fcmp(G32(0x4da09c + jn * 0x18), max) & 1)) /* sic: the last ring */
            max = G32(0x4da09c + jn * 0x18);
        sum = pv_fadd(G32(SNATCH_CENTRE + 4), sum);
        if (pv_fcmp(max, G32(SNATCH_CENTRE + 4)) & 0x41)
            max = G32(SNATCH_CENTRE + 4);
        if (pv_fcmp(max, G32(0x4a34b8)) & 0x41)
            continue;
        k = pv_depth_slot(pv_fdiv_fist(sum, G32(0x4a345c)) >> 5);
        pv_copy6(p + 8, v00);
        pv_copy6(p + 0x24, v01);
        pv_copy6(p + 0x40, SNATCH_CENTRE);
        U32(p, 4) = c0;
        pv_ot_link(k, p);
    }
}

/* 0x4906f0: meant to stop a snatch in states 0x10..0x3f, but tests the
 * first slot only and for >= 0x40 and < 0x10 together, so never does
 * (sic). */
void GAMEAPI snatch_state_check(void)
{
    uint32_t s = 0x5d1fc0, i = 0;
    int32_t state;

    while (1) {
        state = *(volatile int32_t *)(uintptr_t)(*(volatile uint32_t *)(uintptr_t)s + 0x188);
        if (state >= 0x40) {
            if (state < 0x10) /* sic: never */
                break;
        }
        s += 4;
        i++;
        if (s >= 0x5d1fc4)
            return;
    }
    U32(G32(0x5d1fc0 + 4 * i), 0x188) = 0x10;
    U32(G32(0x5d1fc0 + 4 * i), 0x180) = 0;
}

/* =================================================================
 * import thunks: jumps through the import slots, as the original (the
 * native build jumps to its own implementation, as winapi.h calls it)
 * ================================================================= */

/* x86-64 (native only): no C calls these addresses (orig_pt.c and the rest
 * call the imports themselves); a tail jump leaves the register arguments
 * as they are, so they stay jumps (C bodies would need physics.h's
 * void(void) declarations changed to the imports' prototypes). */
#if defined(REROLL_NATIVE) || !defined(__i386__)
#define PV_IMPORT_JMP(name, slot) __asm__ volatile("jmp " #name)
#else
#define PV_IMPORT_JMP(name, slot) __asm__ volatile("jmp *" #slot)
#endif

/* 0x4910f0: DirectDrawCreate. */
__attribute__((naked)) void ddraw_create_thunk(void)
{
    PV_IMPORT_JMP(DirectDrawCreate, 0x4a3024);
}

/* 0x4910f6: DirectDrawEnumerateA. */
__attribute__((naked)) void ddraw_enumerate_thunk(void)
{
    PV_IMPORT_JMP(DirectDrawEnumerateA, 0x4a3028);
}

/* 0x4910fc: DirectInputCreateA. */
__attribute__((naked)) void dinput_create_thunk(void)
{
    PV_IMPORT_JMP(DirectInputCreateA, 0x4a3030);
}

/* 0x492b88: DirectPlayLobbyCreateA (dplayx ordinal 4). */
__attribute__((naked)) void dplay_lobby_create_thunk(void)
{
    PV_IMPORT_JMP(DirectPlayLobbyCreateA, 0x4a3038);
}

/* =================================================================
 * the movie player
 * ================================================================= */

/* 0x492b90 (movie in ecx): the next chunk (8-byte header: id, size with
 * the header) read into a block (malloc). A video frame (0x0b85120f) goes
 * to the frame ring (+0x198, 12 each: data, +8 = 3; write index +0x368,
 * count +0x36c): 1. Sound (0x90fc0302) with sound on (+0x12) is unpacked
 * (0x4950a3 stereo, else 0x495000) into a block of 4 x the size in the
 * sound ring (+0x98, 8 each: data, size; index +0x360, count +0x370): 2.
 * The end chunk (0xd6bd0106): 3; anything else -1, as are end of file and
 * a short header. */
int32_t GAMEAPI movie_chunk_read_c(uint8_t *m)
{
    uint8_t hdr[8];
    uint32_t id, size, buf, out, k;

    if (U8(U32(m, 0x48), 0xc) & 0x10)
        return -1;
    if (crt_fread_at(hdr, 1, 8, U32(m, 0x48)) != 8)
        return -1;
    id = (uint32_t)hdr[0] | (uint32_t)hdr[1] << 8 | (uint32_t)hdr[2] << 16 | (uint32_t)hdr[3] << 24;
    size = ((uint32_t)hdr[4] | (uint32_t)hdr[5] << 8 | (uint32_t)hdr[6] << 16 | (uint32_t)hdr[7] << 24) - 8;
    buf = crt_malloc_at(size);
    crt_fread_at(buf, 1, size, U32(m, 0x48));
    if (id == 0x0b85120f) {
        U32(m, (U32(m, 0x368) + 0x22) * 0xc) = buf;
        U32(m, U32(m, 0x368) * 0xc + 0x1a0) = 3;
        U32(m, 0x368) = (U32(m, 0x368) + 1) & 0x1f;
        U32(m, 0x36c) = U32(m, 0x36c) + 1;
        return 1;
    }
    if (id != 0x90fc0302) {
        crt_free_at(buf);
        return id == 0xd6bd0106 ? 3 : -1;
    }
    if (U16(m, 0x12) != 0) {
        out = crt_malloc_at(size * 4);
        U32(m, 8 * U32(m, 0x360) + 0x98) = out;
        U32(m, 8 * U32(m, 0x360) + 0x9c) = size * 4;
        k = U32(m, 0x360);
        if (U16(m, 0x18) == 2)
            adpcm_stereo_at(0, buf, U32(m, 8 * k + 0x98), size, m + 0x31c, m + 0x324);
        else
            adpcm_mono_at(0, buf, U32(m, 8 * k + 0x98), size * 2, m + 0x31c, m + 0x324);
        U32(m, 0x360) = (U32(m, 0x360) + 1) & 0x1f;
        U32(m, 0x370) = U32(m, 0x370) + 1;
    }
    crt_free_at(buf);
    return 2;
}

#if defined(__i386__) /* x86-64: every caller is C and calls the body */
__asm__(".text\n"
        ".globl movie_chunk_read\n"
        "movie_chunk_read:\n\t"
        "pushl %ecx\n\t"
        "call movie_chunk_read_c\n\t"
        "addl $4, %esp\n\t"
        "ret\n");
#endif

/* 0x492dc0: the play loop (in the caller or the movie thread): start /
 * end clocks (+0x33c / +0x340); until the stop word +0x68, the end chunk,
 * the callback +0x40 (flag 0x400) returning non-zero or the frames run
 * out (flag 0x100 rewinds to +0x60 instead): chunks read ahead (a second
 * one after a frame with sound or after sound), a frame decoded (0x494f10)
 * and drawn by the pixel format +0x88 unless +0x5c frames are to be
 * skipped, the sound started with the first frame (0x4935e0), the frame's
 * block freed; with sound the loop waits for the timer to catch up
 * (+0x34c) and the lead is the next skip count. */
int32_t GAMEAPI movie_play_loop(uint8_t *m)
{
    int32_t done = 0, r, skip;
    uint32_t slot, played, fed;

    U32(m, 0x5c) = 0;
    U32(m, 0x348) = 0;
    U32(m, 0x33c) = crt_clock_at();
    while (U16(m, 0x68) == 0) {
        r = movie_chunk_read_c(m);
        if (r == 1) {
            if (U16(m, 0x12) != 0)
                movie_chunk_read_c(m);
        } else if (r == 2) {
            movie_chunk_read_c(m);
        } else if (r == 3) {
            done = 1;
        }
        if (U32(m, 0x1c) & 0x400)
            done = ((int32_t(GAMEAPI *)(void))(uintptr_t)U32(m, 0x40))(); /* movie_frame_check 0x42d8e0 */
        if (U32(m, (U32(m, 0x364) + 0x22) * 0xc) != 0) {
            movie_frame_decode_at(m, m + (U32(m, 0x364) + 0x22) * 0xc);
            skip = I32(m, 0x5c);
            if (skip > 2) {
                U32(m, 0x5c) = (uint32_t)(skip - 1);
            } else if (skip <= 1) {
                r = I32(m, 0x88);
                if (r == 0x10 || r == 0xf)
                    movie_blit16_at(m);
                else if (r == 0x18)
                    movie_blit24_at(m);
                else
                    movie_blit_other_at(m);
#ifdef REROLL_NATIVE
                /* the frame went into the screen surface (+0x90), which the
                 * original's display showed as it was: here it is presented */
                vkgame_show_surface(U32(m, 0x90));
#endif
            }
            U32(m, 0x344) = U32(m, 0x344) + 1;
            movie_frame_done_at(m);
            if (U32(m, 0x348) == 0 && U16(m, 0x12) != 0) {
                if (U32(m, 0x350) == 0)
                    movie_sound_start_c(m);
                U32(m, 0x34c) = 0;
            }
            crt_free_at(U32(m, (U32(m, 0x364) + 0x22) * 0xc));
            U32(m, (U32(m, 0x364) + 0x22) * 0xc) = 0;
            U32(m, 0x364) = (U32(m, 0x364) + 1) & 0x1f;
            InterlockedIncrement((LONG *)(m + 0x348));
            U32(m, 0x36c) = U32(m, 0x36c) - 1;
        } else if (U32(m, 0x1c) & 0x100) {
            slot = U32(m, 0x60);
            fed = U32(m, 0x48);
            U32(m, 0x348) = 0;
            U32(m, 0x344) = 0;
            U32(m, 0x5c) = 0;
            U32(m, 0x34c) = 0;
            crt_fseek_at(fed, slot, 0);
            if (done)
                break;
            continue;
        } else {
            done = 1;
        }
        if (U16(m, 0x12) != 0) {
            if (done)
                break;
            played = V32(m, 0x348);
            fed = V32(m, 0x34c);
            if (V32(m, 0x370) != 0) {
                while ((int32_t)(played - fed) >= 1) {
                    fed = V32(m, 0x34c);
                    if (V32(m, 0x370) == 0)
                        break;
                }
            }
            U32(m, 0x5c) = fed - played;
            if ((int32_t)(fed - played) < 0)
                U32(m, 0x5c) = 0;
        }
        if (done)
            break;
    }
    U32(m, 0x340) = crt_clock_at();
    return 0;
}

/* 0x492f90: a movie opened: the 0x37c-byte record (calloc; error 3), the
 * caller's 0x28-byte config at +0x1c, the window +0x44, the file +0x48
 * (fopen "rb", fseek to `offset` if given), its 0x1c-byte header (error 2
 * unless it starts "Reap"), sound set-up (0x493360), the rings cleared,
 * the colour tables from the header's rest (0x495220), the data start
 * +0x60 (ftell), 16 sound chunks read ahead when there is sound, the video
 * set-up (0x494d40). The error code goes to 0x4da218. */
uint32_t GAMEAPI movie_open(uint32_t path, uint32_t window, const uint32_t *config, uint32_t offset)
{
    uint32_t m = crt_calloc_at(1, 0x37c), f, n, buf;
    int i;

    if (m == 0) {
        g_movie_error = 3;
        return 0;
    }
    for (i = 0; i < 10; i++)
        U32(m, 0x1c + 4 * i) = config[i];
    U32(m, 0x8c) = U32(m, 0x28);
    U32(m, 0x90) = U32(m, 0x2c);
    U32(m, 0x44) = window;
    U32(m, 0x58) = 0;
    U32(m, 0x4c) = 0;
    U32(m, 0x50) = 0;
    U32(m, 0x5c) = 0;
    U32(m, 0x348) = 0;
    g_movie_error = 0;
    U32(m, 0x350) = 0;
    U16(m, 0x68) = 0;
    f = crt_fopen_at(path, 0x4b3068);
    U32(m, 0x48) = f;
    if (offset != 0)
        crt_fseek_at(f, offset, 0);
    crt_fread_at(m, 0x1c, 1, U32(m, 0x48));
    if (crt_strncmp_at(m, 0x4b3060, 5) != 0) {
        crt_free_at(m);
        g_movie_error = 2;
        return 0;
    }
    movie_sound_init_c(PTR(m));
    for (i = 0; i < 0x20; i++) {
        U32(m, 0x98 + 8 * i) = 0;
        U32(m, 0x198 + 0xc * i) = 0;
    }
    U32(m, 0x64) = crt_malloc_at((int32_t)S16(m, 0x10));
    n = U32(m, 8) - 0x1c;
    buf = crt_malloc_at(n);
    crt_fread_at(buf, 1, n, U32(m, 0x48));
    movie_tables_init_at(buf, U32(m, 0x64), n);
    crt_free_at(buf);
    U32(m, 0x364) = 0;
    U32(m, 0x368) = 0;
    U32(m, 0x36c) = 0;
    U32(m, 0x344) = 0;
    U32(m, 0x60) = crt_ftell_at(U32(m, 0x48));
    if (U16(m, 0x12) != 0) {
        U32(m, 0x370) = 0;
        i = 0;
        do {
            if (movie_chunk_read_c(PTR(m)) == 2)
                i++;
        } while (i < 0x10);
    }
    movie_video_init_at(m);
    U32(m, 0x5c) = 0;
    U32(m, 0x348) = 0;
    return m;
}

/* 0x493140: a movie closed (0 if its file is): the play time +0x338, the
 * file, the frame and sound rings' blocks, the sound (0x493a10, 0x493590),
 * the video (0x494ed0), the table +0x64 and the record freed. 1. */
int32_t GAMEAPI movie_close(uint8_t *m)
{
    uint32_t p;
    int i;

    if (U32(m, 0x48) == 0)
        return 0;
    U32(m, 0x338) = U32(m, 0x340) - U32(m, 0x33c);
    crt_fclose_at(U32(m, 0x48));
    U32(m, 0x48) = 0;
    for (i = 0, p = A(m) + 0x198; i < 0x20; i++, p += 0xc)
        if (U32(p, 0) != 0) {
            crt_free_at(U32(p, 0));
            U32(p, 0) = 0;
        }
    if (U16(m, 0x12) != 0) {
        movie_audio_stop_at(m);
        for (i = 0, p = A(m) + 0x98; i < 0x20; i++, p += 8)
            if (U32(p, 0) != 0) {
                crt_free_at(U32(p, 0));
                U32(p, 0) = 0;
            }
    }
    movie_sound_release_c(m);
    movie_video_free_at(m);
    if (U32(m, 0x64) != 0)
        crt_free_at(U32(m, 0x64));
    if (m != 0)
        crt_free_at(m);
    return 1;
}

/* 0x493210: a movie played (0x492f90 at offset 0): with flag 0x200 on a
 * thread of its own (CreateThread of 0x4932d0, handle +0x3c, id +0x38;
 * error 4), else here between busy flags 0x4da284; the record (freed by
 * then in the second case, sic) or 0. */
uint32_t GAMEAPI movie_player_run(uint32_t path, uint32_t window, const uint32_t *config)
{
    uint32_t m = movie_open(path, window, config, 0), code;

    if (m == 0)
        return 0;
    if (U32(m, 0x1c) & 0x200) {
        U32(m, 0x3c) = (uint32_t)(uintptr_t)CreateThread(0, 0, (void *)movie_thread_proc, GMEM(m), 0, GMEM(m + 0x38));
        if (U32(m, 0x3c) == 0) {
            g_movie_error = 4;
            return 0;
        }
        return m;
    }
    InterlockedExchange(GMEM(g_movie_busy), 1);
    movie_play_loop(PTR(m));
    if ((U32(m, 0x1c) & 0x200) && U32(m, 0x3c) != 0) {
        code = A(config); /* the argument's slot in the original */
        if (GetExitCodeThread(GMEM(U32(m, 0x3c)), &code) != 0)
            ExitThread(code);
    }
    movie_close(PTR(m));
    InterlockedExchange(GMEM(g_movie_busy), 0);
    return m;
}

/* 0x4932d0: the movie thread (a cdecl function given to CreateThread,
 * sic): busy flag, the play loop, the record closed. */
int32_t GAMEAPI movie_thread_proc(uint8_t *m)
{
    uint32_t code = 0;

    InterlockedExchange(GMEM(g_movie_busy), 1);
    movie_play_loop(m);
    if ((U32(m, 0x1c) & 0x200) && U32(m, 0x3c) != 0)
        if (GetExitCodeThread(GMEM(U32(m, 0x3c)), &code) != 0)
            ExitThread(code);
    movie_close(m);
    InterlockedExchange(GMEM(g_movie_busy), 0);
    return 0;
}

/* 0x493340 (text in ecx): "Reaper Fatal Error" box, exit(1). */
void GAMEAPI movie_fatal_error_c(uint32_t text)
{
    MessageBoxA(0, GMEM(text), GMEM(0x4b306c), 0);
    crt_exit_at(1);
}

#if defined(__i386__) /* x86-64: every caller is C and calls the body */
__asm__(".text\n"
        ".globl movie_fatal_error\n"
        "movie_fatal_error:\n\t"
        "pushl %ecx\n\t"
        "call movie_fatal_error_c\n\t"
        "addl $4, %esp\n\t"
        "ret\n");
#endif

/* 0x493360 (movie in ecx): with sound (flag 0x800): the DirectSound
 * (flag 0x1000: the caller's +0x30, else one made here, 0x4da294 = 1,
 * cooperative level 3 on the window) and its caps (16-bit flag 0x4da29c);
 * the primary buffer (flag 0x2000: the caller's +0x34, its size to
 * 0x4da288) or one made here: PCM at the movie's rate (+0x14), stereo /
 * 16 bits as the caps allow, 0x4da298 = 1, its size to 0x4da288. */
void GAMEAPI movie_sound_init_c(uint8_t *m)
{
    uint32_t caps[0x18], desc[5], dcaps[5], window;
    uint8_t wfx[0x14];
    uint16_t ch, bits, align;
    int32_t rate;
    int i;

    if (!(U32(m, 0x1c) & 0x800))
        return;
    if (U32(m, 0x1c) & 0x1000) {
        g_ds_owned = 0;
        g_ds = U32(m, 0x30);
    }
    if (g_ds == 0) {
        g_ds_owned = 1;
        window = U32(m, 0x44);
        if (OUT32(0x4da28c, DirectSoundCreate(0, out_, 0)) != 0 || IDirectSound_SetCooperativeLevel(g_ds, GMEM(window), 3) != 0)
            g_ds = 0;
        if (g_ds == 0) {
            g_ds_primary = 0;
            return;
        }
    }
    for (i = 0; i < 0x18; i++)
        caps[i] = 0;
    caps[0] = 0x60;
    IDirectSound_GetCaps(g_ds, caps);
    g_ds_16bit = (caps[1] >> 5) & 1;
    if (U32(m, 0x1c) & 0x2000) {
        g_ds_primary_owned = 0;
        g_ds_primary = U32(m, 0x34);
        if (g_ds_primary == 0)
            goto create;
        dcaps[0] = 0x14;
        dcaps[1] = 0;
        dcaps[2] = 0;
        dcaps[3] = 0;
        dcaps[4] = 0;
        IDirectSoundBuffer_GetCaps(g_ds_primary, dcaps);
        g_ds_bytes = dcaps[2];
    }
    if (g_ds_primary != 0)
        return;
create:
    desc[0] = 0x14;
    desc[1] = 0x81;
    desc[2] = 0;
    desc[3] = 0;
    desc[4] = 0;
    if (OUT32(0x4da290, IDirectSound_CreateSoundBuffer(g_ds, desc, out_, 0)) != 0)
        return;
    ch = (caps[1] & 2) ? 2 : 1;
    rate = S16(m, 0x14);
    bits = (caps[1] & 8) ? 16 : 8;
    align = (uint16_t)((bits >> 3) * ch);
    U16(wfx, 0) = 1;
    U16(wfx, 2) = ch;
    U32(wfx, 4) = (uint32_t)rate;
    U32(wfx, 8) = (uint32_t)(rate * (int32_t)align);
    U16(wfx, 0xc) = align;
    U16(wfx, 0xe) = bits;
    U16(wfx, 0x10) = 0;
    if (IDirectSoundBuffer_SetFormat(g_ds_primary, wfx) != 0)
        return;
    dcaps[0] = 0x14;
    dcaps[1] = 0;
    dcaps[2] = 0;
    dcaps[3] = 0;
    dcaps[4] = 0;
    IDirectSoundBuffer_GetCaps(g_ds_primary, dcaps);
    g_ds_primary_owned = 1;
    g_ds_bytes = dcaps[2];
}

#if defined(__i386__) /* x86-64: every caller is C and calls the body */
__asm__(".text\n"
        ".globl movie_sound_init\n"
        "movie_sound_init:\n\t"
        "pushl %ecx\n\t"
        "call movie_sound_init_c\n\t"
        "addl $4, %esp\n\t"
        "ret\n");
#endif

/* 0x493590 (movie in ecx): with sound, the primary buffer released when
 * made here and given (flags 0x298 and 0x2000, sic), the DirectSound when
 * made here. */
void GAMEAPI movie_sound_release_c(uint8_t *m)
{
    if (!(U32(m, 0x1c) & 0x800))
        return;
    if (g_ds_primary_owned != 0 && (U32(m, 0x1c) & 0x2000) && g_ds_primary != 0) {
        IDirectSoundBuffer_Release(g_ds_primary);
        g_ds_primary = 0;
    }
    if (g_ds_owned != 0 && g_ds != 0)
        IDirectSound_Release(g_ds);
}

#if defined(__i386__) /* x86-64: every caller is C and calls the body */
__asm__(".text\n"
        ".globl movie_sound_release\n"
        "movie_sound_release:\n\t"
        "pushl %ecx\n\t"
        "call movie_sound_release_c\n\t"
        "addl $4, %esp\n\t"
        "ret\n");
#endif

/* 0x4935e0 (movie in ecx): the sound buffer (+0x94: 16-bit PCM at the
 * movie's rate +0x14 and channels +0x18, the primary's size, flags 0xe0;
 * frequency set) looping from 0, its size +0x354; without one a 0x8000
 * byte virtual clock (+0x318 = 1, +0x378). Then a 10 ms periodic timer
 * (0x4937e0, period clamped to the device caps, +0x334 / +0x330; fatal
 * errors 0x4b3098 / 0x4b3080), waiting for its first tick (+0x374). */
int32_t GAMEAPI movie_sound_start_c(uint8_t *m)
{
    uint8_t wfx[0x14];
    uint32_t desc[5], dcaps[5], tc[2], period, min;
    uint16_t ch;
    int32_t rate;

    if (g_ds != 0 && g_ds_primary != 0) {
        ch = U16(m, 0x18);
        rate = S16(m, 0x14);
        U16(wfx, 0) = 1;
        U16(wfx, 2) = ch;
        U32(wfx, 4) = (uint32_t)rate;
        U32(wfx, 8) = (uint32_t)(rate * (int16_t)ch * 2);
        U16(wfx, 0xc) = (uint16_t)(ch * 2);
        U16(wfx, 0xe) = U16(m, 0x16);
        U16(wfx, 0x10) = 0;
        desc[0] = 0x14;
        desc[1] = 0xe0;
        desc[2] = g_ds_bytes;
        desc[3] = 0;
        desc[4] = A(wfx);
        if (OUT32(A(m + 0x94), IDirectSound_CreateSoundBuffer(g_ds, desc, out_, 0)) == 0)
            IDirectSoundBuffer_SetFrequency(U32(m, 0x94), (DWORD)(int32_t)S16(m, 0x14));
        else
            U32(m, 0x94) = 0;
    } else {
        U32(m, 0x94) = 0;
    }
    if (g_ds != 0 && g_ds_primary != 0 && U32(m, 0x94) != 0) {
        U32(m, 0x318) = 0;
        dcaps[0] = 0x14;
        dcaps[1] = 0;
        dcaps[2] = 0;
        dcaps[3] = 0;
        dcaps[4] = 0;
        IDirectSoundBuffer_GetCaps(U32(m, 0x94), dcaps);
        U32(m, 0x354) = dcaps[2];
        U32(m, 0x358) = 0;
        IDirectSoundBuffer_Play(U32(m, 0x94), 0, 0, 1);
    } else {
        U32(m, 0x378) = 0;
        U32(m, 0x354) = 0x8000;
        U32(m, 0x318) = 1;
        U32(m, 0x358) = 0;
    }
    tc[0] = 0; /* stack leftovers in the original if the call fails */
    tc[1] = 0;
    U32(m, 0x350) = 1;
    U32(m, 0x374) = 0;
    if (timeGetDevCaps(tc, 8) != 0)
        movie_fatal_error_c(0x4b3098);
    min = tc[0] > 1 ? tc[0] : 1;
    if (min >= tc[1])
        period = tc[1];
    else
        period = tc[0] > 1 ? tc[0] : 1;
    U32(m, 0x334) = period;
    timeBeginPeriod(period);
    U32(m, 0x330) = timeSetEvent(10, U32(m, 0x334), (void *)movie_sound_timer, A(m), 1);
    if (U32(m, 0x330) == 0)
        movie_fatal_error_c(0x4b3080);
    while (V32(m, 0x374) == 0)
        ;
    return 0;
}

#if defined(__i386__) /* x86-64: every caller is C and calls the body */
__asm__(".text\n"
        ".globl movie_sound_start\n"
        "movie_sound_start:\n\t"
        "pushl %ecx\n\t"
        "call movie_sound_start_c\n\t"
        "addl $4, %esp\n\t"
        "ret\n");
#endif

/* 0x4937e0: the 10 ms timer (stdcall TimeProc; user = the movie), unless
 * already running (+0x32c): once started (+0x374) with sound (+0x350),
 * the play position (the virtual clock +0x378 advanced 0x372, or the
 * buffer's play cursor) and, when the room before it from the write
 * position +0x358 holds the next sound block (+0x35c), the block copied
 * in (Lock / Unlock; not for the virtual clock), the write position
 * advanced round the buffer, the block freed, +0x370 down and the fed
 * count +0x34c up. The tick count +0x374 goes up. The play cursor and the
 * first Lock size share the TimeProc's msg argument slot in the original,
 * the write cursor and the first Lock pointer a local (as here); the
 * second Lock pointer and size are stack leftovers if Lock fails (0
 * here). */
void PV_STD movie_sound_timer(uint32_t id, uint32_t msg, uint32_t user, uint32_t dw1, uint32_t dw2)
{
    uint8_t *m = PTR(user);
    uint32_t pos, room, k, p1 = 0, n1 = msg, p2 = 0, n2 = 0;

    (void)id;
    (void)dw1;
    (void)dw2;
    if (InterlockedExchange((LONG *)(m + 0x32c), 1) != 0)
        return;
    if (U32(m, 0x374) != 0 && U32(m, 0x350) != 0) {
        if (U32(m, 0x318) != 0) {
            U32(m, 0x378) = U32(m, 0x378) + 0x372;
            if (U32(m, 0x378) >= U32(m, 0x354))
                U32(m, 0x378) = U32(m, 0x378) - U32(m, 0x354);
            pos = U32(m, 0x378);
        } else {
            IDirectSoundBuffer_GetCurrentPosition(U32(m, 0x94), &n1, &p1);
            pos = n1;
        }
        if (U32(m, 0x358) > pos)
            room = pos - U32(m, 0x358) + U32(m, 0x354);
        else
            room = pos - U32(m, 0x358);
        k = U32(m, 0x35c);
        if ((int32_t)room >= I32(m, 8 * k + 0x9c) && U32(m, 8 * U32(m, 0x35c) + 0x98) != 0) {
            if (U32(m, 0x318) == 0) {
                uint32_t q1 = p1, q2 = p2;

                IDirectSoundBuffer_Lock(U32(m, 0x94), U32(m, 0x358), U32(m, 8 * U32(m, 0x35c) + 0x9c), &q1, &n1, &q2,
                                        &n2, 0);
                p1 = q1;
                p2 = q2;
                pv_copy(PTR(p1), PTR(U32(m, 8 * U32(m, 0x35c) + 0x98)), n1);
                if (n2 != 0)
                    pv_copy(PTR(p2), PTR(U32(m, 8 * U32(m, 0x35c) + 0x98) + n1), n2);
                IDirectSoundBuffer_Unlock(U32(m, 0x94), GMEM(p1), n1, GMEM(p2), n2);
            }
            U32(m, 0x358) = (U32(m, 8 * U32(m, 0x35c) + 0x9c) + U32(m, 0x358)) % U32(m, 0x354);
            crt_free_at(U32(m, 8 * U32(m, 0x35c) + 0x98));
            U32(m, 8 * U32(m, 0x35c) + 0x98) = 0;
            U32(m, 0x35c) = (U32(m, 0x35c) + 1) & 0x1f;
            U32(m, 0x370) = U32(m, 0x370) - 1;
            InterlockedIncrement((LONG *)(m + 0x34c));
        }
    }
    U32(m, 0x374) = U32(m, 0x374) + 1;
    InterlockedExchange((LONG *)(m + 0x32c), 0);
}
