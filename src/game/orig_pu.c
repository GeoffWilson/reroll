/* Group PU: the original's still-reachable functions in 0x489de0-0x48cdc7
 * (the end of the DirectInput file and the DirectPlay network file).
 *
 *  - DirectInput: the force-feedback effects of a device record (0x43c bytes
 *    at 0x4d75c0; +0xd8 / +0xdc IDirectInputEffect pointers): stop
 *    (0x489de0, 0x489f40), all four effects of a device stopped (0x489f70),
 *    the periodic kick (0x489e10); the device count (0x489fe0 and its thunk
 *    0x48a010, the first original function a run without the exe needs);
 *  - DirectPlay set-up: init (0x48a600: CoCreateInstance, the connection
 *    probe 0x48a9f0, message buffers), the connection enumeration callback
 *    (0x48b3e0) and the provider dialog (0x48b320), both stored only as
 *    immediates, the connection (0x48ace0: a DirectPlayLobby compound
 *    address for IPX / TCP / serial / modem), close / shutdown (0x48a6b0,
 *    0x48a8b0, with the pending-message cancel 0x48a670);
 *  - sessions and players: join (0x48b7e0), host (0x48b940), the local
 *    player (0x48bd70), the player table (0x5d2cd0, 6 x 0x66c: 0x48bd10,
 *    0x48bd40, 0x48bed0, 0x48c930), the receive thread and its events
 *    (0x48bc40, the thread body 0x48ce90, another immediate);
 *  - messages: the receive loop (0x48c630) into the inbox (0x48c310 /
 *    0x48c380, drained by 0x48c460 under the named mutex 0x48c3f0 /
 *    0x48c440), the system messages (0x48bff0), the game messages
 *    (0x48c5f0), the checksum test (0x48bfb0), sending with the resend log
 *    (0x48cc30, 0x48ca00, 0x48cae0, 0x48c9f0) and the acknowledgement table
 *    (0x5d5368, 64 x 0x18: 0x48c530, 0x48c790, 0x48c830, 0x48c900).
 *
 * Windows and COM calls go through winapi.h and com.h. The callbacks the original passes as immediates
 * (0x48b3e0, 0x48b320, 0x48ce90) are ours here; 0x48ac40 (another group's)
 * is passed by address. Everything outside the group is called by
 * address. */
#include "../runtime.h"
#include "physics.h"
#include "com.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define GS8(a) (*(volatile int8_t *)(uintptr_t)(a))
#define G16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define G32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(volatile int32_t *)(uintptr_t)(a))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

#define g_config G32(0x4b2198)

/* IDirectPlayLobbyA / IDirectPlayLobby2A methods (com.h has none). */
#define DPERR_ALREADYINITIALIZED 0x88770005u
#define DPERR_BUFFERTOOSMALL 0x8877001eu
#define DPERR_NOMESSAGES 0x887700beu
#define DPERR_PENDING_SEND 0x8877010eu /* the SendEx retry */
#define DPERR_CONNECTING 0x8877015eu
#define E_PENDING_ 0x8000000au

/* Game state. */
#define DEV_RECORD(dev) (0x4d75c0 + (uint32_t)(dev) * 0x43c)
#define g_dinput_hr G32(0x4d6f2c)
#define g_dplay G32(0x5d2060)
#define g_dplay_hr G32(0x5d6230)
#define g_net_flags G32(0x5d622c)
#define g_conn_count GI32(0x5d26a4)
#define g_conn_chosen GI32(0x5d26a8)
#define CONN(i) (0x5d2064 + (uint32_t)(i) * 0xa0) /* guid, +0x10 copy, +0x14 size, +0x18 kind, +0x1c ok, +0x20 name */
#define g_session_count GI32(0x5d2cc4)
#define g_session_chosen GI32(0x5d2cc8)
#define SESSION(i) (0x5d26ac + (uint32_t)(i) * 0x9c) /* +0 open, +8 guid, +0x18 0x10, +0x1c name */
#define PLAYER(i) (0x5d2cd0 + (uint32_t)(i) * 0x66c) /* +0 used, +4 DPID, +8 name, +0x664 last heard */
#define g_player_count GI32(0x5d5358)
#define g_local_player GI32(0x5d535c)
#define g_local_id G32(0x5d5360)
#define g_recv_buffer G32(0x5d5364)
#define ACK(i) (0x5d5368 + (uint32_t)(i) * 0x18)
#define g_inbox G32(0x5d5968)
#define g_inbox_read G32(0x5d596c)
#define g_inbox_used G32(0x5d5970)
#define g_inbox_count GI32(0x5d5974)
#define g_mutex G32(0x5d5988)
#define g_quit_event G32(0x5d598c)
#define g_recv_event G32(0x5d5990)
#define g_thread G32(0x5d5994)
#define SENT(i) (0x5d5e18 + (int32_t)(i) * 0x18) /* +0 data, +4 size, +8 from, +0xc to, +0x10 flags, +0x14 count */
#define g_sent_index GS8(0x5d6118)
#define g_sent_base G32(0x5d611c)
#define g_sent_end G32(0x5d6120)
#define g_sent_next G32(0x5d6124)
#define CHAT_LINES 0x5d6128 /* 4 x 0x41 */

#define GUID_SP_TCPIP 0x4a4100
#define GUID_SP_IPX 0x4a40f0
#define GUID_SP_SERIAL 0x4a4110
#define GUID_SP_MODEM 0x4a4120

/* Outside the group, by address. */
#define mem_copy_at(d, s, n) GCALL(0x476f30, mem_copy)(PTR(A(d)), PTR(A(s)), n)
#define mem_fill_at(d, c, n) GCALL(0x476f50, mem_fill)(PTR(A(d)), c, n)
#define block_alloc_at(n, t) GCALL(0x476ae0, global_block_alloc)(n, t)
#define block_free_at(p) GCALL(0x476b40, global_block_free)(p)
#define arena_alloc_at(n) GCALL(0x476b80, arena_alloc_low)(n)
#define dieffect_fill_at(e) GCALL(0x489250, ff_effect_defaults)(A(e))
#define dinput_stop_a_at(d) GCALL(0x489a80, ff_road_stop)(d)
#define dinput_stop_b_at(d) GCALL(0x489b90, ff_spring_a_stop)(d)
#define dinput_stop_c_at(d) GCALL(0x489ca0, ff_spring_b_stop)(d)
#define beginthread_at(f, s, a) GCALL(0x498d7c, msvc_beginthread)(A(f), s, a)
#define net_flag9_set_at(x) GCALL(0x45f790, network_flag9_set)(x)
#define serial_baud_at(x) GCALL(0x48cff0, serial_baud)(x)
#define serial_stop_bits_at(x) GCALL(0x48d070, serial_stop_bits)(x)
#define serial_parity_at(x) GCALL(0x48d090, serial_parity)(x)
#define serial_flow_at(x) GCALL(0x48d0e0, serial_flow)(x)
#define net_players_compact_at() GCALL(0x4609b0, net_players_compact)()
#define net_state_bit5_at() GCALL(0x45ef30, net_state_bit5)()
#define net_player_left_at(i) GCALL(0x45cb90, fe_net_player_left)(i)
#define net_race_abort_at() GCALL(0x45d170, fe_net_race_abort)()
#define net_players_send_at() GCALL(0x45dde0, netplay_players_send)()
#define net_states_send_at() GCALL(0x45f310, net_states_send)()
#define text_line_get_at(b, n, f, i) GCALL(0x42e6d0, text_line_get)(PTR(A(b)), n, (const char *)PTR(f), i)
#define sprintf_at(d, f) GCALL(0x4979f0, msvc_sprintf)((char *)PTR(A(d)), (const char *)PTR(A(f)))
#define net_message_box_at(s) GCALL(0x48d270, net_chat_send)((char *)PTR(A(s)))
#define menu_dirty_set_at() GCALL(0x452d20, menu_dirty_set)()
#if defined(__i386__)
/* 0x45d230 reads three arguments; the original pushes the recipient too */
#define net_game_message_handle_at(b, n, f, t) \
    ((void(GAMEAPI *)(uint32_t, uint32_t, uint32_t, uint32_t))(uintptr_t)0x45d230)(A(b), n, f, t)
#else
#define net_game_message_handle_at(b, n, f, t) ((void)(t), GCALL(0x45d230, netplay_message_handle)(A(b), n, f))
#endif
#define game_time_at() GCALL(0x43692a, game_time_get)()
#define net_packet_filter_at(b, f) GCALL(0x48cf40, net_packet_filter)((const uint32_t *)PTR(A(b)), f)
#define net_race_over_at() GCALL(0x45eba0, net_race_over)()
#define net_sequence_next_at() GCALL(0x48c920, fe_net_sequence_next)()
#define net_checksum_at(b, n) GCALL(0x48cab0, fe_net_checksum)(PTR(A(b)), n)
#define net_drop_player_at(i) GCALL(0x48bf70, fe_net_drop_player)((uint32_t)(i))
#define network_player_set_at(s, n, id) GCALL(0x48be20, network_player_set)(s, (const char *)PTR(A(n)), (int32_t)(id))
#define network_buffer_reset_at() GCALL(0x48c990, network_buffer_reset)()

/* This group's functions (also in build/groups/pu_protos.txt for physics.h). */
void GAMEAPI ff_effect_a_stop(int32_t dev); /* 0x489de0 */
void GAMEAPI ff_periodic_play(int32_t dev, int32_t period, int32_t magnitude, int32_t duration); /* 0x489e10 */
void GAMEAPI ff_effect_b_stop(int32_t dev); /* 0x489f40 */
void GAMEAPI ff_device_stop_all(int32_t dev); /* 0x489f70 */
int32_t GAMEAPI joy_device_count(void); /* 0x489fe0 */
int32_t GAMEAPI joy_device_count_thunk(void); /* 0x48a010 */
int32_t GAMEAPI dplay_init(void); /* 0x48a600 */
void GAMEAPI dplay_pending_cancel(void); /* 0x48a670 */
void GAMEAPI dplay_close(void); /* 0x48a6b0 */
void GAMEAPI dplay_shutdown(void); /* 0x48a8b0 */
int32_t GAMEAPI dplay_connections_probe(void); /* 0x48a9f0 */
int32_t GAMEAPI dplay_connection_init(uint32_t address, uint32_t phone, uint32_t modem); /* 0x48ace0 */
int32_t __attribute__((stdcall)) dplay_provider_dialog_proc(uint32_t hwnd, uint32_t msg, uint32_t wparam,
                                                            uint32_t lparam); /* 0x48b320 */
int32_t __attribute__((stdcall)) dplay_connection_enum_cb(uint32_t guid, uint32_t conn, uint32_t size,
                                                          uint32_t name, uint32_t flags,
                                                          uint32_t context); /* 0x48b3e0 */
int32_t GAMEAPI dplay_session_join(int32_t index, uint32_t name); /* 0x48b7e0 */
int32_t GAMEAPI dplay_session_host(uint32_t name, uint32_t session_name); /* 0x48b940 */
int32_t GAMEAPI dplay_thread_start(void); /* 0x48bc40 */
int32_t GAMEAPI net_player_slot_free(void); /* 0x48bd10 */
int32_t GAMEAPI net_player_slot_of(uint32_t id); /* 0x48bd40 */
int32_t GAMEAPI dplay_player_create(uint32_t name, int32_t host); /* 0x48bd70 */
int32_t GAMEAPI net_player_remove(int32_t slot); /* 0x48bed0 */
int32_t GAMEAPI net_message_checksum_ok(uint8_t *msg, uint32_t size); /* 0x48bfb0 */
void GAMEAPI dplay_system_message(uint32_t msg, uint32_t size, uint32_t from, uint32_t to); /* 0x48bff0 */
int32_t GAMEAPI net_inbox_push(uint32_t from, uint32_t to, uint32_t data, uint32_t size); /* 0x48c310 */
int32_t GAMEAPI net_inbox_pop(uint32_t *from, uint32_t *to, uint32_t buf, uint32_t *size); /* 0x48c380 */
uint32_t GAMEAPI net_lock(int32_t wait); /* 0x48c3f0 */
int32_t GAMEAPI net_unlock(uint32_t h); /* 0x48c440 */
int32_t GAMEAPI net_inbox_dispatch(void); /* 0x48c460 */
int32_t GAMEAPI net_ack_track(uint32_t seq, uint32_t msgid, uint32_t to, uint32_t count, uint32_t flags); /* 0x48c530 */
int32_t GAMEAPI net_player_check(uint32_t id); /* 0x48c5a0 */
int32_t GAMEAPI net_game_message(uint32_t buf, uint32_t size, uint32_t from, uint32_t to); /* 0x48c5f0 */
int32_t GAMEAPI dplay_receive_all(void); /* 0x48c630 */
int32_t GAMEAPI net_ack_wait(uint32_t seq); /* 0x48c790 */
int32_t GAMEAPI net_acks_wait_first(void); /* 0x48c830 */
void GAMEAPI net_acks_clear(void); /* 0x48c900 */
int32_t GAMEAPI net_player_lost_check(uint32_t id); /* 0x48c930 */
void GAMEAPI net_send_buffer_forget(void); /* 0x48c9f0 */
void GAMEAPI net_send_log(uint32_t from, uint32_t to, uint32_t flags, uint32_t data, uint32_t size,
                          uint32_t count); /* 0x48ca00 */
int32_t GAMEAPI net_resend(uint32_t to, uint32_t seq); /* 0x48cae0 */
int32_t GAMEAPI net_message_send(uint32_t to, uint32_t msg, uint32_t size, uint32_t flags); /* 0x48cc30 */
void GAMEAPI dplay_receive_thread(uint32_t arg); /* 0x48ce90 */

/* The original's inline strlen + 1 (repne scasb). */
static uint32_t pu_strsize(uint32_t s)
{
    uint32_t n = 0;

    while (G8(s + n))
        n++;
    return n + 1;
}

/* The original's inline strcpy: the length first, then a forward copy
 * (dwords then bytes: the same bytes as a forward byte copy, also when the
 * strings overlap at least 4 bytes apart). */
static void pu_strcpy(uint32_t dst, uint32_t src)
{
    uint32_t n = pu_strsize(src), i;

    for (i = 0; i < n; i++)
        G8(dst + i) = G8(src + i);
}

/* rep cmpsd over a GUID. */
static int pu_guid_eq(uint32_t a, uint32_t b)
{
    int i;

    for (i = 0; i < 16; i += 4)
        if (G32(a + i) != G32(b + i))
            return 0;
    return 1;
}

static void pu_guid_copy(uint32_t dst, uint32_t src)
{
    int i;

    for (i = 0; i < 16; i += 4)
        G32(dst + i) = G32(src + i);
}

/* ===== DirectInput force feedback ===== */

/* 0x489de0: stops the device's first effect (+0xd8), if any. */
void GAMEAPI ff_effect_a_stop(int32_t dev)
{
    uint32_t fx = G32(DEV_RECORD(dev) + 0xd8);

    if (fx)
        g_dinput_hr = IDirectInputEffect_Stop(fx);
}

/* 0x489e10: the periodic effect (+0xdc): unless the same kick (period
 * within 12000, magnitude within 1000, duration within 10000 of the last
 * one) is still playing, its parameters are set (DIEFFECT from 0x489250,
 * duration, a DIPERIODIC {magnitude, 0, 0, period}; not when period is -1)
 * and it is started. The status goes into the first argument's slot. */
void GAMEAPI ff_periodic_play(int32_t dev, int32_t period, int32_t magnitude, int32_t duration)
{
    uint32_t slot = DEV_RECORD(dev) + 0xdc;
    uint32_t fx = G32(slot);
    uint32_t periodic[4], effect[13];
    int32_t last = GI32(0x4b2cb8);

    if (!fx)
        return;
    if (last != -1) {
        int32_t d1 = period - last, d2 = magnitude - GI32(0x4d6f24), d3 = duration - GI32(0x4d9ad8);

        d1 = d1 < 0 ? -d1 : d1; /* cdq / xor / sub: INT_MIN stays negative */
        d2 = d2 < 0 ? -d2 : d2;
        d3 = d3 < 0 ? -d3 : d3;
        if (d1 < 12000 && d2 < 1000 && d3 < 10000) {
            IDirectInputEffect_GetEffectStatus(fx, (DWORD *)&dev);
            if (dev & 1)
                return;
        }
    }
    GI32(0x4b2cb8) = period;
    GI32(0x4d6f24) = magnitude;
    GI32(0x4d9ad8) = duration;
    if (period != -1) {
        mem_fill_at(periodic, 0, 0x10);
        periodic[0] = (uint32_t)magnitude;
        periodic[1] = 0;
        periodic[2] = 0;
        periodic[3] = (uint32_t)period;
        dieffect_fill_at(effect);
        effect[12] = A(periodic);
        effect[11] = 0x10;
        effect[2] = (uint32_t)duration;
        g_dinput_hr = IDirectInputEffect_SetParameters(G32(slot), (const uint8_t *)effect, 0x101);
    }
    IDirectInputEffect_GetEffectStatus(G32(slot), (DWORD *)&dev);
    g_dinput_hr = IDirectInputEffect_Start(G32(slot), 1, 0);
}

/* 0x489f40: stops the periodic effect (+0xdc), if any. */
void GAMEAPI ff_effect_b_stop(int32_t dev)
{
    uint32_t fx = G32(DEV_RECORD(dev) + 0xdc);

    if (fx)
        g_dinput_hr = IDirectInputEffect_Stop(fx);
}

/* 0x489f70: every effect of the device stopped. */
void GAMEAPI ff_device_stop_all(int32_t dev)
{
    dinput_stop_a_at(dev);
    dinput_stop_b_at(dev);
    dinput_stop_c_at(dev);
    ff_effect_a_stop(dev);
    ff_effect_b_stop(dev);
}

/* 0x489fe0: the number of input devices (0x4d9554). */
int32_t GAMEAPI joy_device_count(void)
{
    return GI32(0x4d9554);
}

/* 0x48a010: a jump to 0x489fe0. */
int32_t GAMEAPI joy_device_count_thunk(void)
{
    return joy_device_count();
}

/* ===== DirectPlay set-up ===== */

/* 0x48a600: the network state (0x5d2060..0x5d6230) cleared, the
 * connections probed (0x48a9f0), the DirectPlay object created, the
 * receive buffer (0x1000) and the inbox (0x20004) allocated, the send
 * buffer reset (0x48c990). 1, or 0 when CoCreateInstance fails.
 * Deliberate change: a DirectPlay object still there (the front end
 * loading again after a network race) is shut down first (0x48a8b0), so
 * its session ends and the other players are told; the original forgot
 * it, leaving the session open behind the game's back. */
int32_t GAMEAPI dplay_init(void)
{
    if (g_dplay)
        dplay_shutdown();
    mem_fill_at(0x5d2060, 0, 0x41d0);
    dplay_connections_probe();
    g_dplay_hr = CoCreateInstance(GMEM(0x4a40e0), 0, 1, GMEM(0x4a40d0), (uint32_t *)GMEM(0x5d2060));
    if (g_dplay_hr)
        return 0;
    g_recv_buffer = arena_alloc_at(0x1000);
    g_inbox = arena_alloc_at(0x20004);
    network_buffer_reset_at();
    return 1;
}

/* 0x48a670: CancelMessage for each acknowledgement still waiting (used ==
 * 1, flag bit 0 clear), with its message id (+0xc). */
void GAMEAPI dplay_pending_cancel(void)
{
    int i;

    for (i = 0; i < 0x40; i++) {
        uint32_t r = ACK(i);

        if (U32(r, 0) == 1 && !(U8(r, 0x12) & 1))
            IDirectPlay4A_CancelMessage(g_dplay, U32(r, 0xc), 0);
    }
}

/* The receive thread stopped (its quit event set, up to 5 s for flag 0x40)
 * and its handles closed, then the mutex: shared by 0x48a6b0 / 0x48a8b0. */
static void pu_thread_stop(void)
{
    if (g_thread) {
        uint32_t t, end;

        SetEvent(GMEM(g_quit_event));
        t = timeGetTime();
        if (!(g_net_flags & 0x40)) {
            end = t + 5000;
            while (timeGetTime() < end && !(g_net_flags & 0x40))
                ;
        }
        CloseHandle(GMEM(g_quit_event));
        CloseHandle(GMEM(g_recv_event));
        g_thread = 0;
        g_quit_event = 0;
        g_recv_event = 0;
    }
    if (g_mutex) {
        CloseHandle(GMEM(g_mutex));
        g_mutex = 0;
    }
}

static void pu_connections_free(void)
{
    int i;

    for (i = 0; i < g_conn_count; i++) {
        if (U32(CONN(i), 0x10))
            block_free_at(U32(CONN(i), 0x10));
        U32(CONN(i), 0x10) = 0;
    }
}

/* 0x48a6b0: closes the network (when open: thread, mutex, pending
 * messages, Close, connection copies), releases the object, clears the
 * player table and creates a new DirectPlay object (0x45f790(0) when that
 * works). */
void GAMEAPI dplay_close(void)
{
    uint32_t hr;
    int i;

    if (g_net_flags & 1) {
        g_net_flags &= ~4u;
        pu_thread_stop();
        if (g_dplay) {
            dplay_pending_cancel();
            IDirectPlay4A_Close(g_dplay);
        }
        pu_connections_free();
    }
    g_net_flags &= ~1u;
    if (g_dplay)
        IDirectPlay4A_Release(g_dplay);
    g_dplay = 0;
    for (i = 0; i < 6; i++)
        mem_fill_at(PLAYER(i), 0, 0x66c);
    g_player_count = 0;
    hr = CoCreateInstance(GMEM(0x4a40e0), 0, 1, GMEM(0x4a40d0), (uint32_t *)GMEM(0x5d2060));
    g_dplay_hr = hr;
    if (!hr)
        net_flag9_set_at(0);
}

/* 0x48a8b0: the network shut down for good: as 0x48a6b0 when open, plus
 * the send buffer forgotten (0x48c9f0) and both buffers' pointers cleared;
 * the object released, flag 1 cleared. */
void GAMEAPI dplay_shutdown(void)
{
    if (g_net_flags & 1) {
        g_net_flags &= ~4u;
        pu_thread_stop();
        net_send_buffer_forget();
        g_inbox = 0;
        g_recv_buffer = 0;
        if (g_dplay) {
            dplay_pending_cancel();
            IDirectPlay4A_Close(g_dplay);
        }
        pu_connections_free();
    }
    if (g_dplay)
        IDirectPlay4A_Release(g_dplay);
    g_dplay = 0;
    g_net_flags &= ~1u;
}

/* 0x48a9f0: a temporary DirectPlay and DirectPlayLobby object: the
 * connections enumerated (0x48b3e0 into 0x5d2064), the modem service
 * provider initialised with a compound address and its player address
 * read back (DPERR_BUFFERTOOSMALL sizes it) and enumerated (0x48ac40);
 * both released. 1, or 0 on a failure (some failures leak, sic). */
int32_t GAMEAPI dplay_connections_probe(void)
{
    uint32_t dp = 0, lobby = 0, size = 0, buf;
    uint32_t elem[6];
    uint8_t address[0x100];
    uint32_t hr;

    G32(0x5d5cd4) = 0;
    hr = CoCreateInstance(GMEM(0x4a40e0), 0, 1, GMEM(0x4a40d0), &dp);
    g_dplay_hr = hr;
    if (hr)
        return 0;
    hr = CoCreateInstance(GMEM(0x4a41a0), 0, 1, GMEM(0x4a4190), &lobby);
    g_dplay_hr = hr;
    if (hr)
        return 0;
    g_conn_count = 0;
    hr = IDirectPlay4A_EnumConnections(dp, 0, (void *)dplay_connection_enum_cb, GMEM(G32(0x6c8000)), 1);
    g_dplay_hr = hr;
    if (hr) {
        if (dp)
            IDirectPlay4A_Release(dp);
        if (lobby)
            IDirectPlayLobby2A_Release(lobby);
        return 0;
    }
    pu_guid_copy(A(elem), 0x4a4200);
    elem[4] = 0x10;
    elem[5] = GUID_SP_MODEM;
    {
        uint32_t asize = 0x100;

        hr = IDirectPlayLobby2A_CreateCompoundAddress(lobby, elem, 1, address, &asize);
        g_dplay_hr = hr;
        if (!hr) {
            hr = IDirectPlay4A_InitializeConnection(dp, A(address), 0);
            g_dplay_hr = hr;
        }
    }
    if (hr) {
        if (dp)
            IDirectPlay4A_Release(dp);
        if (lobby)
            IDirectPlayLobby2A_Release(lobby);
        return 0;
    }
    hr = IDirectPlay4A_GetPlayerAddress(dp, 0, 0, A(&size));
    g_dplay_hr = hr;
    if (hr && hr != DPERR_BUFFERTOOSMALL)
        return 0;
    buf = (uint32_t)block_alloc_at(size, 0);
    if (buf) {
        hr = IDirectPlay4A_GetPlayerAddress(dp, 0, buf, A(&size));
        g_dplay_hr = hr;
        if (hr)
            return 0;
        hr = IDirectPlayLobby2A_EnumAddress(lobby, (void *)(uintptr_t)0x48ac40, GMEM(buf), size, 0);
        g_dplay_hr = hr;
        if (hr)
            return 0;
        block_free_at(buf);
    }
    if (lobby)
        IDirectPlayLobby2A_Release(lobby);
    if (dp)
        IDirectPlay4A_Release(dp);
    return 1;
}

/* 0x48ace0: the connection: the connections enumerated, the one of the
 * configured kind (config +0x80: 0 TCP/IP with the address `address` =
 * config +0xa0, 1 IPX, 2 serial (port, baud, stop bits, parity, flow from
 * config +0x3c / 0x3a / 0x3e / 0x3f / 0x3d), 3 modem with the phone number
 * `phone` = config +0xe0 and modem name `modem` = config +0xc0; others:
 * the provider dialog) chosen into 0x5d26a8 (0 when none), its compound
 * address built through a DirectPlayLobby and InitializeConnection called
 * with it (or with the stored connection, when that kind has no address
 * or after the dialog). The three arguments are slots the original also
 * writes: a kind without its strings reads the caller's values. 1, or 0
 * on a failure. */
int32_t GAMEAPI dplay_connection_init(uint32_t address, uint32_t phone, uint32_t modem)
{
    uint32_t lobby1 = 0, lobby2 = 0, size = 0;
    uint32_t elem[18];
    uint32_t comport[5] = {0, 0, 0, 0, 0}; /* left over on the original's stack unless serial */
    uint8_t buf[0x100];
    uint32_t hr, cfg, guid, count;
    int32_t i;
    int use_stored = 0;

    g_conn_count = 0;
    g_conn_chosen = 0;
    hr = IDirectPlay4A_EnumConnections(g_dplay, 0, (void *)dplay_connection_enum_cb, GMEM(G32(0x6c8000)), 1);
    g_dplay_hr = hr;
    if (hr)
        return 0;
    cfg = g_config;
    switch (U8(cfg, 0x80)) {
    case 0:
        for (i = 0; i < g_conn_count; i++)
            if (pu_guid_eq(CONN(i), GUID_SP_TCPIP)) {
                g_conn_chosen = i;
                break;
            }
        address = cfg + 0xa0;
        G32(0x4d9ae8) = 0;
        break;
    case 1:
        for (i = 0; i < g_conn_count; i++)
            if (pu_guid_eq(CONN(i), GUID_SP_IPX)) {
                g_conn_chosen = i;
                break;
            }
        G32(0x4d9ae8) = 0;
        break;
    case 2:
        for (i = 0; i < g_conn_count; i++)
            if (pu_guid_eq(CONN(i), GUID_SP_SERIAL)) {
                g_conn_chosen = i;
                break;
            }
        comport[0] = U8(cfg, 0x3c) + 1u;
        comport[1] = (uint32_t)serial_baud_at(U8(cfg, 0x3a));
        comport[2] = (uint32_t)serial_stop_bits_at(U8(g_config, 0x3e));
        comport[3] = (uint32_t)serial_parity_at(U8(g_config, 0x3f));
        comport[4] = (uint32_t)serial_flow_at(U8(g_config, 0x3d));
        G32(0x4d9ae8) = 0;
        break;
    case 3:
        for (i = 0; i < g_conn_count; i++)
            if (pu_guid_eq(CONN(i), GUID_SP_MODEM)) {
                g_conn_chosen = i;
                break;
            }
        phone = cfg + 0xe0;
        modem = cfg + 0xc0;
        G32(0x4d9ae8) = 0;
        break;
    default:
        G32(0x4d9ae8) = 1;
        g_conn_chosen = DialogBoxParamA(GMEM(G32(0x6c7ffc)), GMEM(0x68), GMEM(G32(0x6c8000)),
                                        (void *)dplay_provider_dialog_proc, 0);
        if (g_conn_chosen == -1)
            return 0;
        if (G32(0x4d9ae8))
            use_stored = 1;
        break;
    }
    if (!use_stored) {
        hr = DirectPlayLobbyCreateA(0, &lobby1, 0, 0, 0);
        g_dplay_hr = hr;
        if (hr)
            return 0;
        hr = IDirectPlayLobbyA_QueryInterface(lobby1, GMEM(0x4a4170), &lobby2);
        g_dplay_hr = hr;
        if (hr)
            return 0;
        guid = CONN(g_conn_chosen);
        count = 0;
        if (pu_guid_eq(guid, GUID_SP_IPX)) {
            pu_guid_copy(A(elem), 0x4a4200);
            elem[4] = 0x10;
            elem[5] = GUID_SP_IPX;
            count = 1;
        } else if (pu_guid_eq(CONN(g_conn_chosen), GUID_SP_TCPIP)) {
            if (address) {
                pu_guid_copy(A(elem), 0x4a4200);
                pu_guid_copy(A(elem + 6), 0x4a4260);
                elem[4] = 0x10;
                elem[5] = GUID_SP_TCPIP;
                elem[10] = pu_strsize(address);
                elem[11] = address;
                count = 2;
            }
        } else if (pu_guid_eq(CONN(g_conn_chosen), GUID_SP_SERIAL)) {
            pu_guid_copy(A(elem), 0x4a4200);
            pu_guid_copy(A(elem + 6), 0x4a4290);
            elem[4] = 0x10;
            elem[5] = GUID_SP_SERIAL;
            elem[10] = 0x14;
            elem[11] = A(comport);
            count = 2;
        } else if (pu_guid_eq(CONN(g_conn_chosen), GUID_SP_MODEM)) {
            if (phone && modem) {
                pu_guid_copy(A(elem), 0x4a4200);
                pu_guid_copy(A(elem + 6), 0x4a4240);
                elem[4] = 0x10;
                elem[5] = GUID_SP_MODEM;
                elem[10] = pu_strsize(phone);
                elem[11] = phone;
                pu_guid_copy(A(elem + 12), 0x4a4220);
                elem[16] = pu_strsize(modem);
                elem[17] = modem;
                count = 3;
            }
        }
        if (count) {
            size = 0x100;
            hr = IDirectPlayLobby2A_CreateCompoundAddress(lobby2, elem, count, buf, &size);
            g_dplay_hr = hr;
            if (hr)
                return 0;
            if (size > 0x100)
                return 0;
            hr = IDirectPlay4A_InitializeConnection(g_dplay, A(buf), 0);
        } else {
            use_stored = 1;
        }
    }
    if (use_stored)
        hr = IDirectPlay4A_InitializeConnection(g_dplay, U32(CONN(g_conn_chosen), 0x10), 0);
    g_dplay_hr = hr;
    if (hr && hr != DPERR_ALREADYINITIALIZED)
        return 0;
    if (lobby2)
        IDirectPlayLobby2A_Release(lobby2);
    if (lobby1)
        IDirectPlayLobbyA_Release(lobby1);
    return 1;
}

/* 0x48b320 (not in Ghidra's list; passed as an immediate by 0x48ace0):
 * the service provider dialog: the connection names into combo box 0x3e9
 * (CB_ADDSTRING, then CB_SETCURSEL 0); OK ends it with the selection
 * (CB_GETCURSEL), Cancel with -1. */
int32_t __attribute__((stdcall)) dplay_provider_dialog_proc(uint32_t hwnd, uint32_t msg, uint32_t wparam,
                                                            uint32_t lparam)
{
    int32_t i;

    (void)lparam;
    if (msg == 0x110) {
        for (i = 0; i < g_conn_count; i++)
            SendDlgItemMessageA(GMEM(hwnd), 0x3e9, 0x143, 0, (LPARAM)(CONN(i) + 0x20));
        SendDlgItemMessageA(GMEM(hwnd), 0x3e9, 0x14e, 0, 0);
        return 1;
    }
    if (msg != 0x111)
        return 0;
    if ((uint16_t)wparam == 1) {
        EndDialog(GMEM(hwnd), SendDlgItemMessageA(GMEM(hwnd), 0x3e9, 0x147, 0, 0));
        return 1;
    }
    if ((uint16_t)wparam == 2)
        EndDialog(GMEM(hwnd), -1);
    return 1;
}

/* 0x48b3e0 (not in Ghidra's list; passed as an immediate by 0x48a9f0 and
 * 0x48ace0): the EnumConnections callback: the next connection record
 * (0x5d2064 + n * 0xa0): service provider guid, a copy of the connection
 * (0x476ae0; none: the record isn't counted), its size, short name;
 * whether InitializeConnection works on a fresh DirectPlay object (+0x1c);
 * kind +0x18 (TCP/IP 0, IPX 1, serial 2, modem 3, else -1). Returns 1. */
int32_t __attribute__((stdcall)) dplay_connection_enum_cb(uint32_t guid, uint32_t conn, uint32_t size,
                                                          uint32_t name, uint32_t flags, uint32_t context)
{
    uint32_t dp = 0, copy;

    (void)flags;
    (void)context;
    U32(CONN(g_conn_count), 0x18) = 0;
    U32(CONN(g_conn_count), 0x1c) = 0;
    U32(CONN(g_conn_count), 0x14) = size;
    pu_guid_copy(CONN(g_conn_count), guid);
    copy = (uint32_t)block_alloc_at(size, 0);
    U32(CONN(g_conn_count), 0x10) = copy;
    pu_strcpy(CONN(g_conn_count) + 0x20, U32(name, 8));
    if (!U32(CONN(g_conn_count), 0x10))
        return 1;
    mem_copy_at(U32(CONN(g_conn_count), 0x10), conn, size);
    G32(0x5d5cd4) = 0;
    g_dplay_hr = CoCreateInstance(GMEM(0x4a40e0), 0, 1, GMEM(0x4a40d0), &dp);
    if (!g_dplay_hr) {
        g_dplay_hr = IDirectPlay4A_InitializeConnection(dp, conn, 0);
        if (!g_dplay_hr)
            U32(CONN(g_conn_count), 0x1c) = 1;
        IDirectPlay4A_Release(dp);
    }
    if (pu_guid_eq(guid, GUID_SP_TCPIP))
        U32(CONN(g_conn_count), 0x18) = 0;
    else if (pu_guid_eq(guid, GUID_SP_IPX))
        U32(CONN(g_conn_count), 0x18) = 1;
    else if (pu_guid_eq(guid, GUID_SP_SERIAL))
        U32(CONN(g_conn_count), 0x18) = 2;
    else if (pu_guid_eq(guid, GUID_SP_MODEM))
        U32(CONN(g_conn_count), 0x18) = 3;
    else
        U32(CONN(g_conn_count), 0x18) = 0xffffffff;
    g_conn_count = g_conn_count + 1;
    return 1;
}

/* ===== sessions and players ===== */

/* The session's capabilities: the timeout (DPCAPS +0x24, at least 5000)
 * into 0x5d2ccc, flag 2 set. Shared by 0x48b7e0 / 0x48b940. */
static int32_t pu_session_caps(void)
{
    uint32_t caps[10];
    uint32_t hr, t;

    caps[0] = 0x28;
    hr = IDirectPlay4A_GetCaps(g_dplay, A(caps), 0);
    g_dplay_hr = hr;
    if (hr)
        return 0;
    t = caps[9];
    if (t < 0x1388)
        t = 0x1388;
    G32(0x5d2ccc) = t;
    g_net_flags |= 2;
    return 1;
}

/* 0x48b7e0: joins session `index` (when any were found: Open with its
 * instance guid, retried while connecting; the local player `name`
 * created), then the capabilities. 0 for an index past the count or a
 * failure. */
int32_t GAMEAPI dplay_session_join(int32_t index, uint32_t name)
{
    uint32_t desc[20];
    uint32_t hr;

    if (g_dplay) {
        dplay_pending_cancel();
        IDirectPlay4A_Close(g_dplay);
    }
    if (index >= g_session_count)
        return 0;
    if (g_session_count) {
        g_session_chosen = index;
        mem_fill_at(desc, 0, 0x50);
        desc[0] = 0x50;
        pu_guid_copy(A(desc + 2), SESSION(g_session_chosen) + 8);
        do {
            hr = IDirectPlay4A_Open(g_dplay, A(desc), 0x81);
            g_dplay_hr = hr;
        } while (hr == DPERR_CONNECTING);
        if (hr)
            return 0;
        U32(SESSION(g_session_chosen), 0) = 1;
        g_net_flags &= ~0x20u;
        dplay_player_create(name, 0);
    }
    return pu_session_caps();
}

/* 0x48b940: hosts a session named `session_name` (record 0: the game's
 * guid 0x4a3500; flags 0xa800, 6 players; Open retried while connecting),
 * the host player `name`, then the capabilities. */
int32_t GAMEAPI dplay_session_host(uint32_t name, uint32_t session_name)
{
    uint32_t desc[20];
    uint32_t hr;

    if (g_dplay) {
        dplay_pending_cancel();
        IDirectPlay4A_Close(g_dplay);
    }
    g_session_chosen = 0;
    g_session_count = 1;
    pu_guid_copy(SESSION(g_session_chosen) + 8, 0x4a3500);
    U32(SESSION(g_session_chosen), 0x18) = 0x10;
    pu_strcpy(SESSION(g_session_chosen) + 0x1c, session_name);
    mem_fill_at(desc, 0, 0x50);
    pu_guid_copy(A(desc + 6), 0x4a3500);
    desc[0] = 0x50;
    desc[1] = 0xa800;
    desc[10] = 6;
    desc[12] = SESSION(g_session_chosen) + 0x1c;
    do {
        hr = IDirectPlay4A_Open(g_dplay, A(desc), 0x82);
        g_dplay_hr = hr;
    } while (hr == DPERR_CONNECTING);
    if (hr)
        return 0;
    U32(SESSION(g_session_chosen), 0) = 1;
    g_net_flags |= 0x20;
    dplay_player_create(name, 1);
    return pu_session_caps();
}

/* 0x48bc40: the named mutex (0x4b2ce4) and, unless config +0x34, the two
 * events (receive 0x4b2cd4, quit 0x4b2cc0) and the receive thread
 * (_beginthread of 0x48ce90); each only when not there yet. 1, or 0 on a
 * failure. */
int32_t GAMEAPI dplay_thread_start(void)
{
    if (!g_mutex)
        g_mutex = (uint32_t)(uintptr_t)CreateMutexA(0, 0, GMEM(0x4b2ce4));
    if (!g_mutex)
        return 0;
    if (U8(g_config, 0x34))
        return 1;
    if (!g_recv_event)
        g_recv_event = (uint32_t)(uintptr_t)CreateEventA(0, 0, 0, GMEM(0x4b2cd4));
    if (!g_recv_event)
        return 0;
    if (!g_quit_event) {
        g_quit_event = (uint32_t)(uintptr_t)CreateEventA(0, 0, 0, GMEM(0x4b2cc0));
        if (!g_quit_event)
            return 0;
    }
    if (!g_thread)
        g_thread = beginthread_at(dplay_receive_thread, 0, 0);
    if (g_thread == 0xffffffff) {
        g_thread = 0;
        return 0;
    }
    return 1;
}

/* 0x48bd10: the first unused player slot after compacting the table
 * (0x4609b0), -1 when full. */
int32_t GAMEAPI net_player_slot_free(void)
{
    int32_t i;

    if (g_player_count < 6) {
        net_players_compact_at();
        for (i = 0; i < 6; i++)
            if (!U32(PLAYER(i), 0))
                return i;
    }
    return -1;
}

/* 0x48bd40: the player slot with DirectPlay id `id`, or -1. */
int32_t GAMEAPI net_player_slot_of(uint32_t id)
{
    int32_t i;

    for (i = 0; i < 6; i++)
        if (U32(PLAYER(i), 4) == id)
            return i;
    return -1;
}

/* 0x48bd70: the local player: slot 0 named (0x48be20), CreatePlayer
 * (server player when `host`) with the receive event, its id into
 * 0x5d5360 and slot 0; 0x5d535c = 0. 1, or 0 on a failure. */
int32_t GAMEAPI dplay_player_create(uint32_t name, int32_t host)
{
    uint32_t dpname[4];
    uint32_t hr;

    if (!network_player_set_at(0, name, 0))
        return 0;
    mem_fill_at(dpname, 0, 0x10);
    dpname[0] = 0x10;
    dpname[2] = PLAYER(0) + 8;
    hr = IDirectPlay4A_CreatePlayer(g_dplay, (DWORD *)GMEM(0x5d5360), dpname, GMEM(g_recv_event), 0, 0,
                                    host ? 0x100 : 0);
    g_dplay_hr = hr;
    g_local_player = 0;
    U32(PLAYER(0), 4) = g_local_id;
    if (hr)
        return 0;
    g_player_count = g_player_count + 1;
    return 1;
}

/* 0x48bed0: player slot `slot` (0..5) removed: when used, destroyed
 * (DestroyPlayer when 0x45ef30 and it has an id) and cleared, the count
 * kept >= 0; then 0x45cb90(slot). 1, or 0 for a bad slot. */
int32_t GAMEAPI net_player_remove(int32_t slot)
{
    uint32_t p;

    if (slot < 0 || slot >= 6)
        return 0;
    p = PLAYER(slot);
    if (U32(p, 0)) {
        if (net_state_bit5_at() && U32(p, 4))
            IDirectPlay4A_DestroyPlayer(g_dplay, U32(p, 4));
        mem_fill_at(p, 0, 0x66c);
        g_player_count = g_player_count - 1;
        if (g_player_count < 0)
            g_player_count = 0;
    }
    net_player_left_at(slot);
    return 1;
}

/* 0x48bfb0: whether the byte sum of the message (`size` bytes; 0 runs
 * 2^32, sic) with its +4 dword zeroed equals that dword (restored). */
int32_t GAMEAPI net_message_checksum_ok(uint8_t *msg, uint32_t size)
{
    uint32_t stored = U32(msg, 4), sum = 0, n = size;
    uint8_t *p = msg;

    U32(msg, 4) = 0;
    do {
        sum += *p++;
    } while (--n);
    U32(msg, 4) = stored;
    return sum == stored;
}

/* 0x48bff0: a DirectPlay system message: player created (3) / destroyed
 * (5), session lost (0x31), host (0x101), chat (0x109: the four chat
 * lines 0x5d6128 shifted down), send complete (0x10d: its acknowledgement
 * record marked), 0x107 to 0x45d230; others ignored. */
void GAMEAPI dplay_system_message(uint32_t msg, uint32_t size, uint32_t from, uint32_t to)
{
    uint8_t line[0x80], text[0x80];
    uint32_t type = U32(msg, 0);
    int32_t i;

    (void)size;
    (void)from;
    if (type == 3) {
        uint32_t id, pname;

        if (U32(msg, 4) != 1 || g_player_count >= 6)
            return;
        id = U32(msg, 8);
        pname = U32(msg, 0x20);
        if (!network_player_set_at(net_player_slot_free(), pname, id))
            return;
        g_player_count = g_player_count + 1;
        if (!(G8(0x5d622c) & 0x20) || !(g_net_flags & 0x200))
            return;
        net_players_send_at();
        net_states_send_at();
    } else if (type == 5) {
        if (U32(msg, 4) != 1)
            return;
        i = net_player_slot_of(U32(msg, 8));
        if (i == -1)
            return;
        if (i != g_local_player) {
            net_player_remove(i);
            if (i == 0)
                net_race_abort_at();
        }
        if (g_player_count < 2) {
            net_race_abort_at();
            return;
        }
        if (!net_state_bit5_at() || !(g_net_flags & 0x200))
            return;
        net_players_send_at();
        net_states_send_at();
    } else if (type == 0x31) {
        net_race_abort_at();
        g_net_flags &= ~2u;
        if (text_line_get_at(line, 0x80, 0x4b0e34, 2) != -1)
            sprintf_at(text, line);
        else
            memset(text, 0, sizeof text); /* the original's stack leftovers */
        net_message_box_at(text);
    } else if (type == 0x101) {
        net_race_abort_at();
        g_net_flags &= ~2u;
        if (text_line_get_at(text, 0x80, 0x4b0e34, 3) != -1)
            sprintf_at(line, text);
        else
            memset(line, 0, sizeof line); /* the original's stack leftovers */
        net_message_box_at(line);
    } else if (type == 0x107) {
        net_game_message_handle_at(U32(msg, 0xc), U32(msg, 0x10), U32(msg, 8), to);
    } else if (type == 0x109) {
        uint32_t dst = CHAT_LINES + 3 * 0x41, src;

        do {
            src = dst - 0x41;
            pu_strcpy(dst, src);
            dst = src;
        } while ((int32_t)dst > CHAT_LINES);
        pu_strcpy(CHAT_LINES, U32(U32(msg, 0x14), 8));
        menu_dirty_set_at();
    } else if (type == 0x10d) {
        for (i = 0; i < 0x40; i++)
            if (U32(ACK(i), 0) && U32(ACK(i), 0xc) == U32(msg, 0x1c)) {
                U16(ACK(i), 0x12) |= 1;
                return;
            }
    }
}

/* ===== the inbox ===== */

/* 0x48c310: a message appended to the inbox (*0x5d5968: {from, to, size,
 * data} records, 0x20000 bytes): 1, or 0 when it doesn't fit. */
int32_t GAMEAPI net_inbox_push(uint32_t from, uint32_t to, uint32_t data, uint32_t size)
{
    uint32_t r;

    if (g_inbox_used + size + 0xc > 0x20000)
        return 0;
    r = g_inbox + g_inbox_used;
    U32(r, 0) = from;
    U32(r, 4) = to;
    U32(r, 8) = size;
    mem_copy_at(r + 0xc, data, size);
    g_inbox_used = g_inbox_used + size + 0xc;
    g_inbox_count = g_inbox_count + 1;
    return 1;
}

/* 0x48c380: the inbox record at the read position (0x5d596c) out: from,
 * to, size, data into `buf`; the position advanced, the count lowered.
 * Returns 1. */
int32_t GAMEAPI net_inbox_pop(uint32_t *from, uint32_t *to, uint32_t buf, uint32_t *size)
{
    uint32_t r = g_inbox_read + g_inbox;

    *from = U32(r, 0);
    *to = U32(r, 4);
    *size = U32(r, 8);
    mem_copy_at(buf, r + 0xc, U32(r, 8));
    g_inbox_read = g_inbox_read + U32(r, 8) + 0xc;
    g_inbox_count = g_inbox_count - 1;
    return 1;
}

/* 0x48c3f0: the network mutex (OpenMutexA 0x4b2ce4) taken, waiting
 * forever when `wait`, else not at all: its handle, or 0 (not taken:
 * released and closed, sic). */
uint32_t GAMEAPI net_lock(int32_t wait)
{
    uint32_t h = (uint32_t)(uintptr_t)OpenMutexA(0x1f0001, 0, GMEM(0x4b2ce4));

    if (!h)
        return 0;
    if (WaitForSingleObject(GMEM(h), wait ? 0xffffffff : 0)) {
        ReleaseMutex(GMEM(h));
        CloseHandle(GMEM(h));
        return 0;
    }
    return h;
}

/* 0x48c440: the mutex released and its handle closed. */
int32_t GAMEAPI net_unlock(uint32_t h)
{
    ReleaseMutex(GMEM(h));
    return CloseHandle(GMEM(h));
}

/* 0x48c460: when connected (flags 1 and 2), under the mutex, every inbox
 * message handled (system messages, from 0, by 0x48bff0, the rest by
 * 0x45d230) and the inbox emptied. 0 when the mutex can't be had, else 1. */
int32_t GAMEAPI net_inbox_dispatch(void)
{
    uint32_t h, from, to, size;

    if (!(g_net_flags & 1) || !(g_net_flags & 2))
        return 1;
    h = net_lock(1);
    if (!h)
        return 0;
    g_inbox_read = 0;
    while (g_inbox_count > 0) {
        if (!net_inbox_pop(&from, &to, g_recv_buffer, &size))
            break;
        if (!from)
            dplay_system_message(g_recv_buffer, size, 0, to);
        else
            net_game_message_handle_at(g_recv_buffer, size, from, to);
    }
    g_inbox_used = 0;
    net_unlock(h);
    return 1;
}

/* ===== acknowledgements ===== */

/* 0x48c530: a free acknowledgement record (0x5d5368, 64 x 0x18) filled:
 * +8 time, +4 to, +0x10 seq (word), +0xc message id, +0x14 resend count,
 * +0x12 flags (word; bit 0: acknowledged), +0 = 1. 1, or 0 when all are used. */
int32_t GAMEAPI net_ack_track(uint32_t seq, uint32_t msgid, uint32_t to, uint32_t count, uint32_t flags)
{
    uint32_t r;
    int i;

    for (i = 0; i < 0x40; i++)
        if (!U32(ACK(i), 0))
            break;
    if (i == 0x40)
        return 0;
    r = ACK(i);
    U32(r, 8) = timeGetTime();
    U32(r, 4) = to;
    U16(r, 0x10) = (uint16_t)seq;
    U32(r, 0xc) = msgid;
    U32(r, 0x14) = count;
    U16(r, 0x12) = (uint16_t)flags;
    U32(r, 0) = 1;
    return 1;
}

/* 0x48c5a0: 1 when `id` is a known player or its DirectPlay flags have
 * 0x100; 0 otherwise or when GetPlayerFlags fails (flags into the
 * argument's slot). */
int32_t GAMEAPI net_player_check(uint32_t id)
{
    uint32_t who = id;

    if (net_player_slot_of(who) != -1)
        return 1;
    if (IDirectPlay4A_GetPlayerFlags(g_dplay, who, A(&id)))
        return 0;
    return (id & 0x100) ? 1 : 0;
}

/* 0x48c5f0: a game message (first byte 0x4b2cbc) handled at once
 * (0x45d230): 1; else 0. */
int32_t GAMEAPI net_game_message(uint32_t buf, uint32_t size, uint32_t from, uint32_t to)
{
    if (U8(buf, 0) != G8(0x4b2cbc))
        return 0;
    net_game_message_handle_at(buf, size, from, to);
    return 1;
}

/* 0x48c630: when connected and receiving (flags 1, 2, 4), under the mutex
 * (not waiting), every message received: the sender's last-heard time
 * (+0x664) set, filtered (0x48cf40) and checked (0x48c5a0), game messages
 * handled (0x48c5f0), the rest queued (0x48c310). 1 when not receiving,
 * 0 after the queue is empty or without the mutex. */
int32_t GAMEAPI dplay_receive_all(void)
{
    uint32_t buf = g_recv_buffer;
    uint32_t now = game_time_at();
    uint32_t h, from, to, size, hr;
    int32_t i;

    if (!(G8(0x5d622c) & 1) || !(g_net_flags & 2) || !(g_net_flags & 4))
        return 1;
    h = net_lock(0);
    if (!h)
        return 0;
    for (;;) {
        size = 0x1000;
        to = 0;
        from = 0;
        hr = IDirectPlay4A_Receive(g_dplay, &from, &to, 1, GMEM(g_recv_buffer), &size);
        if (hr) {
            if (hr == DPERR_NOMESSAGES)
                break;
            continue;
        }
        if (from) {
            i = net_player_slot_of(from);
            if (i >= 0 && i < 6)
                U32(PLAYER(i), 0x664) = now;
            if (net_packet_filter_at(buf, from))
                continue;
            net_player_check(from);
        }
        if (net_game_message(buf, size, from, to))
            continue;
        net_inbox_push(from, to, g_recv_buffer, size);
    }
    net_unlock(h);
    return 0;
}

/* The resend / timeout test of a waiting acknowledgement record (fewer
 * than 2 resends and 250 ms per try passed: 0x48cae0, done). */
static int pu_ack_resend_due(uint32_t r)
{
    if (U32(r, 0x14) >= 2)
        return 0;
    return timeGetTime() > U32(r, 8) + (U32(r, 0x14) + 1) * 250;
}

/* 0x48c790: waits for the acknowledgement of message `seq` (word): 1 when
 * there is none waiting or it arrives (flag bit 0); 0 after a resend
 * (0x48cae0; the record freed) or 2 s. */
int32_t GAMEAPI net_ack_wait(uint32_t seq)
{
    uint32_t r = 0;
    int i;

    for (i = 0; i < 0x40; i++) {
        r = ACK(i);
        if (U32(r, 0) == 1 && U16(r, 0x10) == (uint16_t)seq)
            break;
    }
    if (i == 0x40)
        return 1;
    if (U8(r, 0x12) & 1)
        return 1;
    for (;;) {
        if (pu_ack_resend_due(r)) {
            net_resend(U32(r, 4), U16(r, 0x10));
            U16(r, 0x12) |= 1;
            U32(r, 0) = 0;
            return 0;
        }
        if (timeGetTime() >= U32(r, 8) + 2000)
            return 0;
        if (U8(r, 0x12) & 1)
            return 1;
    }
}

/* 0x48c830: the first waiting acknowledgement waited for (resend / 2 s,
 * as 0x48c790; flag 8 records are skipped to the timeout unless config
 * +0x37 or 0x4c2458 bit 0): 0. When none wait: the race-over step
 * (0x45eba0), its game time added to 0x5d5978, 0x5d597c counted: 1. */
int32_t GAMEAPI net_acks_wait_first(void)
{
    uint32_t t0 = game_time_at(), r = 0;
    int i;

    for (i = 0; i < 0x40; i++) {
        r = ACK(i);
        if (U32(r, 0) == 1)
            break;
    }
    if (i == 0x40) {
        uint32_t t1;

        net_race_over_at();
        t1 = game_time_at();
        G32(0x5d5978) = t1 + (G32(0x5d5978) - t0);
        G32(0x5d597c) = G32(0x5d597c) + 1;
        return 1;
    }
    for (;;) {
        uint16_t f = U16(r, 0x12);
        int check = 0;

        if (!(f & 1)) {
            if (!(f & 8) || U8(g_config, 0x37) || (G8(0x4c2458) & 1))
                check = 1;
        }
        if (check && pu_ack_resend_due(r)) {
            net_resend(U32(r, 4), U16(r, 0x10));
            U8(r, 0x12) |= 1;
            U32(r, 0) = 0;
            return 0;
        }
        if (timeGetTime() >= U32(r, 8) + 2000)
            return 0;
    }
}

/* 0x48c900: every acknowledgement record freed. */
void GAMEAPI net_acks_clear(void)
{
    int i;

    for (i = 0; i < 0x40; i++)
        U32(ACK(i), 0) = 0;
}

/* 0x48c930: when `id`'s DirectPlay flags can't be read, the player (if
 * in the table) dropped (0x48bf70): 1; else 0 (flags into the argument's
 * slot). */
int32_t GAMEAPI net_player_lost_check(uint32_t id)
{
    uint32_t who = id;
    int32_t i;

    if (!IDirectPlay4A_GetPlayerFlags(g_dplay, who, A(&id)))
        return 0;
    for (i = 0; i < 6; i++)
        if (U32(PLAYER(i), 4) == who) {
            net_drop_player_at(i);
            return 1;
        }
    return 1;
}

/* ===== sending ===== */

/* 0x48c9f0: the send buffer pointer (0x5d611c) forgotten. */
void GAMEAPI net_send_buffer_forget(void)
{
    g_sent_base = 0;
}

/* 0x48ca00: a sent message logged for resending: record 0x5d6118 (signed
 * byte, wraps at 32) of 0x5d5e18 with a copy in the ring 0x5d611c ..
 * 0x5d6120 (restarting at the base when it doesn't fit; the next position
 * then still advances from the old one, sic); nothing when it doesn't fit
 * at all. */
void GAMEAPI net_send_log(uint32_t from, uint32_t to, uint32_t flags, uint32_t data, uint32_t size, uint32_t count)
{
    uint32_t r = SENT(g_sent_index);
    uint32_t at;

    at = g_sent_next + size > g_sent_end ? g_sent_base : g_sent_next;
    if (at + size > g_sent_end)
        return;
    U32(r, 0) = at;
    U32(r, 8) = from;
    U32(r, 0xc) = to;
    U32(r, 0x10) = flags;
    U32(r, 4) = size;
    U32(r, 0x14) = count;
    mem_copy_at(at, data, size);
    g_sent_next = g_sent_next + size;
    g_sent_index = (int8_t)(g_sent_index + 1);
    if (g_sent_index >= 0x20)
        g_sent_index = 0;
}

/* 0x48cae0: resends the logged message to `to` with sequence `seq` (the
 * message's +2 word): SendEx (retried for a second), its message id into
 * the `seq` slot; on a failure the player check (0x48c930: 1) or flag
 * 0x10 (0). The record's resend count raised. 0 when not logged. */
int32_t GAMEAPI net_resend(uint32_t to, uint32_t seq)
{
    uint32_t r = 0, t, hr;
    int i;

    for (i = 0; i < 0x20; i++) {
        r = SENT(i);
        if (U32(r, 0xc) == to && U32(r, 0) && (uint32_t)U16(U32(r, 0), 2) == seq)
            break;
    }
    if (i == 0x20)
        return 0;
    t = timeGetTime();
    hr = IDirectPlay4A_SendEx(g_dplay, U32(r, 8), U32(r, 0xc), U32(r, 0x10), GMEM(U32(r, 0)), U32(r, 4), 0, 0, 0,
                              &seq);
    g_dplay_hr = hr;
    if (hr == DPERR_PENDING_SEND) {
        t += 1000;
        do {
            if (timeGetTime() > t) {
                hr = g_dplay_hr;
                break;
            }
            hr = IDirectPlay4A_SendEx(g_dplay, U32(r, 8), U32(r, 0xc), U32(r, 0x10), GMEM(U32(r, 0)), U32(r, 4), 0,
                                      0, 0, &seq);
            g_dplay_hr = hr;
        } while (hr == DPERR_PENDING_SEND);
    }
    if (hr && hr != E_PENDING_) {
        if (net_player_lost_check(to))
            return 1;
        g_net_flags |= 0x10;
        return 0;
    }
    U32(r, 0x14) = U32(r, 0x14) + 1;
    if ((U32(r, 0x10) & 0x401) == 0x401) /* never: 0x48cc30 doesn't set both */
        net_ack_track((U32(r, 0x14) & 0xffff0000u) | U16(U32(r, 0), 2), seq, to, U32(r, 0x14), 0);
    return 1;
}

/* 0x48cc30: sends message `msg` (`size` bytes) to `to` when the network
 * is open: flags 1 (guaranteed, for `flags` & 3) / 0x200 + 0x400
 * (`flags` & 4); its sequence (+2: next for guaranteed, else 0xffff),
 * counter byte (+1, 0x4d9aec) and checksum (+4, 0x48cab0) set; logged
 * (0x48ca00), SendEx (retried for a second); on a failure the player
 * check (1) or flag 0x10 (0); `flags` & 1 waits for the acknowledgement
 * (0x48c790). 1 otherwise. */
int32_t GAMEAPI net_message_send(uint32_t to, uint32_t msg, uint32_t size, uint32_t flags)
{
    uint32_t dpflags = 0, t0, hr, msgid = 0;

    if (!(G8(0x5d622c) & 1))
        return 1;
    if (flags & 3)
        dpflags = 1;
    if (flags & 4) {
        dpflags |= 0x200;
        if (!(dpflags & 1))
            dpflags |= 0x400;
    }
    t0 = timeGetTime();
    if (dpflags & 1)
        U16(msg, 2) = (uint16_t)net_sequence_next_at();
    else
        U16(msg, 2) = 0xffff;
    U8(msg, 1) = G8(0x4d9aec);
    G8(0x4d9aec) = (uint8_t)(G8(0x4d9aec) + 1);
    U32(msg, 4) = net_checksum_at(msg, size);
    net_send_log(g_local_id, to, dpflags, msg, size, 0);
    hr = IDirectPlay4A_SendEx(g_dplay, g_local_id, to, dpflags, GMEM(msg), size, 0, 0, 0, &msgid);
    g_dplay_hr = hr;
    if (hr == DPERR_PENDING_SEND) {
        uint32_t end = t0 + 1000; /* kept in the second argument's slot in the original */

        do {
            if (timeGetTime() > end) {
                hr = g_dplay_hr;
                break;
            }
            hr = IDirectPlay4A_SendEx(g_dplay, g_local_id, to, dpflags, GMEM(msg), size, 0, 0, 0, &msgid);
            g_dplay_hr = hr;
        } while (hr == DPERR_PENDING_SEND);
    }
    if (hr && hr != E_PENDING_) {
        if (net_player_lost_check(to))
            return 1;
        g_net_flags |= 0x10;
        return 0;
    }
    if ((dpflags & 0x401) == 0x401) /* never: 1 and 0x400 exclude each other */
        net_ack_track((hr & 0xffff0000u) | U16(msg, 2), msgid, to, 0, flags & 8);
    if (flags & 1)
        net_ack_wait(U16(msg, 2));
    return 1;
}

/* 0x48ce90 (not in Ghidra's list; the thread 0x48bc40 starts): waits on
 * the receive and quit events, receiving (0x48c630) while the receive
 * event is the one set; flag 0x40 when it ends. */
void GAMEAPI dplay_receive_thread(uint32_t arg)
{
    HANDLE handles[2];

    (void)arg;
    handles[0] = GMEM(g_recv_event);
    handles[1] = GMEM(g_quit_event);
    while (!WaitForMultipleObjects(2, handles, 0, 0xffffffff))
        dplay_receive_all();
    g_net_flags |= 0x40;
}
