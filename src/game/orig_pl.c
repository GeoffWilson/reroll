/* Group PL: the original's still-reachable functions in 0x45c340-0x45e600,
 * the network game's message source file (DirectPlay sits behind
 * 0x48cdd0 / 0x48bd40 / 0x48bfb0, outside the group).
 *
 *  - car state over the network: a car (0x45c3e0 / 0x45c690), its body
 *    (0x45c350 / 0x45c5e0) and its second object (0x45c3c0 / 0x45c660)
 *    packed into a 0x154-byte record and sent (0x45c540, 0x45e600) every
 *    0x2ee ms or when the controls change (0x45c7d0); received records are
 *    put back with the grid unlinked (0x45c4e0, 0x45c780);
 *  - weapons fired by other players (0x45c880, 0x45c8c0), the cars' look
 *    bytes (0x45ca30 / 0x45ca90), the players' ready marks (0x45caf0), the
 *    race settings sent as one block (0x45cf50, 0x45cd00, 0x45e400);
 *  - the chat lines shown in a race (0x45cfb0, 0x45cfe0, 0x45c340);
 *  - the message dispatcher (0x45d230, 34 message types) and the senders:
 *    one byte (0x45ddb0), the player table (0x45dde0), the controls
 *    (0x45deb0, 0x45e120), the race start (0x45e200), the clock (0x45e340),
 *    the look (0x45e4a0) and the bad-message reply (0x45e5b0).
 *
 * The player records: 6 of 0x66c bytes at 0x5d2cd0 (see pl_formats.txt).
 * Message buffers on the stack: the original leaves the bytes it does not
 * fill (1..7 of every header, string tails, a gap at +0x52 of the car
 * state) as stack leftovers; here they are 0. Float steps are the
 * original's x87 instructions. Everything outside the group is called by
 * address. */
#include "../runtime.h"
#include "physics.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))

#define STDCALL __attribute__((stdcall))
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

#define g_race G32(0x4b1960)
#define g_options G32(0x4b2198)
#define g_net G32(0x5d622c) /* network state bits */
#define g_players G8(g_race + 0x54d)

/* The car slots: 0x274 bytes at 0x6bfb80 (+0 car, +0x28 second object). */
#define SLOT(i) (0x6bfb80u + (uint32_t)(i) * 0x274u)
/* The network player records: 0x66c bytes at 0x5d2cd0. */
#define REC(i) (0x5d2cd0u + (uint32_t)(i) * 0x66cu)
#define R_ACTIVE 0x0
#define R_ID 0x4
#define R_NAME 0x8
#define R_CAR 0x18
#define R_1C 0x1c
#define R_20 0x20
#define R_LOOK_SENT 0x24
#define R_STATE_A 0x2c
#define R_STATE_B 0x30
#define R_SEQ 0x3c
#define R_STATE_TIME 0x40
#define R_CONTROLS 0x44 /* + seq * 8: two dwords */
#define R_LOCAL_A 0x244
#define R_LOCAL_B 0x248
#define R_CHECK 0x254 /* + seq * 4 */
#define R_TIME 0x664
#define R_FLAG 0x668

typedef int32_t(GAMEAPI *pl_fn0)(void);
typedef int32_t(GAMEAPI *pl_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *pl_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *pl_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *pl_fn4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(GAMEAPI *pl_fn5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef int32_t(GAMEAPI *pl_fn7)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                 uint32_t g);
#define CALL0(a) (((pl_fn0)(a))())
#define CALL1(a, x) (((pl_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((pl_fn2)(a))(A(x), A(y)))
#define CALL3(a, x, y, z) (((pl_fn3)(a))(A(x), A(y), A(z)))
#define CALL4(a, x, y, z, w) (((pl_fn4)(a))(A(x), A(y), A(z), A(w)))
#define CALL5(a, x, y, z, w, v) (((pl_fn5)(a))(A(x), A(y), A(z), A(w), A(v)))
#define CALL7(a, x, y, z, w, v, u, t) (((pl_fn7)(a))(A(x), A(y), A(z), A(w), A(v), A(u), A(t)))

typedef uint32_t(STDCALL *pl_api0)(void);
#define pl_timeGetTime() ((*(pl_api0 *)(uintptr_t)0x4a32b4)())

/* Outside the group. */
#define key_slots_reset_at() CALL0(0x439fd0)
#define game_time_at() ((uint32_t)CALL0(0x43692a))
#define net_time_at() ((uint32_t)CALL0(0x45eee0))
#define debug_printf_at(s) CALL1(0x476ec0, s)
#define weapon_fire_at(slot, which, z, age, data) CALL5(0x467900, slot, which, z, age, data)
#define net_player_active_at(i) CALL1(0x48cfd0, i)
#define mem_alloc_at(size, flags) ((uint32_t)CALL2(0x476ae0, size, flags))
#define mem_free_at(p) CALL1(0x476b40, p)
#define mem_copy_at(d, s, n) CALL3(0x476f30, d, s, n)
#define chat_lines_at(count, lines) CALL2(0x45f5b0, count, lines)
#define text_free_at(id) CALL1(0x479280, id)
#define text_create_at(s, a, x, y, f, c, z) CALL7(0x479060, s, a, x, y, f, c, z)
#define message_check_at(msg, size) CALL2(0x48bfb0, msg, size)
#define player_of_at(from) CALL1(0x48bd40, from)
#define player_cheat_note_at(from) CALL1(0x45fb90, from)
#define text_line_get_at(buf, size, sec, idx) CALL4(0x42e6d0, buf, size, sec, idx)
#define sprintf_at(buf, fmt, a) CALL3(0x4979f0, buf, fmt, a)
#define chat_post_at(s) CALL1(0x45d150, s)
#define car_state_seen_at(p, a, b, t) CALL4(0x45ee60, p, a, b, t)
#define player_left_at(i) CALL1(0x48bed0, i)
#define net_pause_at(x) CALL1(0x45f7b0, x)
#define send_setting_at(a, b, c, d, e) CALL5(0x45f710, a, b, c, d, e)
#define menu_dirty_at() CALL0(0x452d20)
#define drop_player_at(i) CALL1(0x48bf70, i)
#define race_abort_at() CALL0(0x45d170)
#define race_options_at(a, b, c, d, e) CALL5(0x452fa0, a, b, c, d, e)
#define net_is_host_at() CALL0(0x45eed0)
#define net_bit5_at() CALL0(0x45ef30)
#define net_game_at() CALL0(0x45ef80)
#define results_at(x) CALL1(0x45f990, x)
#define snatched_at(i) CALL1(0x45fa60, i)
#define quit_ack_at(from) CALL1(0x45fca0, from)
#define net_close_at() CALL0(0x4906f0)
#define net_send_at(kind, to, msg, size, flags) CALL5(0x48cdd0, kind, to, msg, size, flags)
#define net_lock_at(x) ((uint32_t)CALL1(0x48c3f0, x))
#define net_unlock_at(h) CALL1(0x48c440, h)
#define car_position_sum_at(slot) CALL1(0x444050, slot)
#define controls_ready_at() CALL0(0x45eb60)
#define net_local_slot_at() ((uint32_t)CALL0(0x48cf20))
#define car_state_changed_at(i) CALL1(0x45ee10, i)
#define net_frame_due_at() CALL0(0x48d390)
#define net_frame_skip_at() CALL0(0x48d3e0)
#define player_free_at(i) CALL1(0x45eeb0, i)
#define grid_unlink_at(b) CALL1(0x4611d0, b)
#define grid_link_at(b) CALL1(0x460fb0, b)
#define body_prepare_at(p) CALL1(0x465740, p)

#define CRT_FTOL 0x497a80

/* This group's functions (also in build/groups/pl_protos.txt for physics.h). */
int32_t GAMEAPI netplay_chat_reset(void); /* 0x45c340 */
void GAMEAPI netplay_body_pack(uint32_t body, uint32_t out); /* 0x45c350 */
void GAMEAPI netplay_record_copy(const uint32_t *src, uint32_t *dst); /* 0x45c3c0 */
void GAMEAPI netplay_car_pack(uint32_t car, uint32_t out); /* 0x45c3e0 */
void GAMEAPI netplay_car_state_apply(int32_t slot, uint32_t rec); /* 0x45c4e0 */
void GAMEAPI netplay_car_state_send(int32_t slot); /* 0x45c540 */
void GAMEAPI netplay_body_unpack(uint32_t rec, uint32_t body); /* 0x45c5e0 */
void GAMEAPI netplay_record_copy_keep(const uint32_t *src, uint32_t *dst); /* 0x45c660 */
void GAMEAPI netplay_car_unpack(uint32_t rec, uint32_t car); /* 0x45c690 */
void GAMEAPI netplay_car_state_load(uint32_t car, uint32_t rec, uint32_t *objp, uint32_t rec2, uint32_t rec3); /* 0x45c780 */
void GAMEAPI netplay_car_state_tick(void); /* 0x45c7d0 */
void GAMEAPI netplay_weapon_kind_set(int32_t slot, uint32_t kind, int32_t which); /* 0x45c880 */
void GAMEAPI netplay_weapon_fire(int32_t slot, uint32_t kind, int32_t which, int32_t age, int32_t size,
                                 uint32_t data); /* 0x45c8c0 */
void GAMEAPI netplay_car_look_get(int32_t slot, uint8_t *a, uint8_t *b, uint8_t *c, uint16_t *d); /* 0x45ca30 */
void GAMEAPI netplay_car_look_set(int32_t slot, const uint8_t *a, const uint8_t *b, const uint8_t *c,
                                  const uint16_t *d); /* 0x45ca90 */
void GAMEAPI netplay_players_mark(void); /* 0x45caf0 */
void GAMEAPI netplay_settings_apply(uint32_t src); /* 0x45cd00 */
uint32_t GAMEAPI netplay_settings_pack(uint32_t head, uint32_t *size); /* 0x45cf50 */
void GAMEAPI netplay_chat_texts_clear(void); /* 0x45cfb0 */
void GAMEAPI netplay_chat_show(uint32_t info, uint32_t colour); /* 0x45cfe0 */
int32_t GAMEAPI netplay_slot_flag_find(void); /* 0x45d1e0 */
void GAMEAPI netplay_race_bytes_set(const uint8_t *src); /* 0x45d210 */
void GAMEAPI netplay_message_handle(uint32_t msg, uint32_t size, uint32_t from); /* 0x45d230 */
int32_t GAMEAPI netplay_send_byte(uint32_t kind, uint32_t value, uint32_t flags); /* 0x45ddb0 */
int32_t GAMEAPI netplay_players_send(void); /* 0x45dde0 */
int32_t GAMEAPI netplay_controls_send(int32_t player, uint32_t a, uint32_t b, int32_t send); /* 0x45deb0 */
int32_t GAMEAPI netplay_controls_relay(void); /* 0x45e120 */
int32_t GAMEAPI netplay_race_start_send(void); /* 0x45e200 */
int32_t GAMEAPI netplay_clock_send(void); /* 0x45e340 */
void GAMEAPI netplay_player_count_update(void); /* 0x45e3c0 */
int32_t GAMEAPI netplay_settings_send(void); /* 0x45e400 */
int32_t GAMEAPI netplay_look_send(void); /* 0x45e4a0 */
int32_t GAMEAPI netplay_bad_message_reply(uint32_t to, uint32_t seq); /* 0x45e5b0 */
int32_t GAMEAPI netplay_car_state_post(uint32_t data, uint32_t size, uint32_t time, int32_t slot); /* 0x45e600 */

/* strcpy as the original inlines it (repne scasb, rep movsd, rep movsb):
 * strlen + 1 bytes, dwords first, forwards. */
static void pl_strcpy(uint32_t dst, uint32_t src)
{
    uint32_t n = 0;
    while (G8(src + n))
        n++;
    n++;
    uint32_t i = 0;
    for (; i < (n & ~3u); i += 4)
        G32(dst + i) = G32(src + i);
    for (; i < n; i++)
        G8(dst + i) = G8(src + i);
}

/* (n + 1) as an unsigned 64-bit integer -> float, * the float at 0x4a33cc,
 * through __ftol (fild qword, fstp dword, fld dword, fmul dword, call). */
static int32_t pl_ticks_ftol(uint32_t n)
{
    uint64_t q = (uint64_t)(uint32_t)(n + 1);
    int32_t r;

    __asm__ volatile("fildq %[q]\n\t"
                     "fstps %[q]\n\t"
                     "flds %[q]\n\t"
                     "fmuls 0x4a33cc\n\t"
                     "call *%[ftol]"
                     : "=a"(r), [q] "+m"(q)
                     : [ftol] "S"(CRT_FTOL)
                     : "ecx", "edx", "memory", "cc", X87);
    return r;
}

/* The "player cheated" chat line: the line (section 0x4b0e34, entry 4)
 * with the player's name, posted to the chat. */
static void pl_cheat_line(uint32_t from, uint32_t name)
{
    char line[0x80], text[0x80];

    player_cheat_note_at(from);
    if (text_line_get_at(line, 0x80, 0x4b0e34, 4) != -1) {
        sprintf_at(text, line, name);
        chat_post_at(text);
    }
}

/* 0x45c340: the chat texts forgotten (0x45cfb0) and the key slots reset
 * (0x439fd0); 1. */
int32_t GAMEAPI netplay_chat_reset(void)
{
    netplay_chat_texts_clear();
    key_slots_reset_at();
    return 1;
}

/* 0x45c350: a body's 13 dwords from +0x34 (position, velocity, ...) to out. */
void GAMEAPI netplay_body_pack(uint32_t body, uint32_t out)
{
    for (uint32_t i = 0; i < 0x34; i += 4)
        U32(out, i) = U32(body, 0x34 + i);
}

/* 0x45c3c0: 0x17 dwords from src to dst. */
void GAMEAPI netplay_record_copy(const uint32_t *src, uint32_t *dst)
{
    for (int i = 0; i < 0x17; i++)
        dst[i] = src[i];
}

/* 0x45c3e0: a car's network state into out (0xc4 bytes): its body
 * (0x45c350), then words and dwords of the car, 0x48 bytes from +0x6b0, 0x28
 * from +0x6f8 and three bytes of the block *(car +0x6ac) (0 when that is
 * -1). out +0x52 is not written. */
void GAMEAPI netplay_car_pack(uint32_t car, uint32_t out)
{
    netplay_body_pack(car, out);
    U16(out, 0x34) = U16(car, 0x648);
    U16(out, 0x36) = U16(car, 0x64a);
    U16(out, 0x38) = U16(car, 0x64c);
    U16(out, 0x3a) = U8(car, 0x661);
    U16(out, 0x3c) = U16(car, 0x67c);
    U16(out, 0x40) = U16(car, 0x690);
    U16(out, 0x42) = U16(car, 0x692);
    U16(out, 0x3e) = U16(car, 0x694);
    U32(out, 0x44) = U32(car, 0x698);
    U32(out, 0x48) = U32(car, 0x69c);
    U16(out, 0x4c) = U16(car, 0x2284);
    U16(out, 0x4e) = U16(car, 0x2288);
    for (uint32_t i = 0; i < 0x48; i += 4)
        U32(out, 0x54 + i) = U32(car, 0x6b0 + i);
    for (uint32_t i = 0; i < 0x28; i += 4)
        U32(out, 0x9c + i) = U32(car, 0x6f8 + i);
    if (U32(car, 0x6ac) != 0xffffffffu) {
        U8(out, 0x50) = U8(U32(car, 0x6ac), 0);
        U8(out, 0x51) = U8(U32(car, 0x6ac), 1);
        U8(out, 0x53) = U8(U32(car, 0x6ac), 3);
        return;
    }
    U8(out, 0x50) = 0;
    U8(out, 0x51) = 0;
    U8(out, 0x53) = 0;
}

/* 0x45c4e0: a received car state record (0x154 bytes) put into a car slot
 * that exists. */
void GAMEAPI netplay_car_state_apply(int32_t slot, uint32_t rec)
{
    if (slot < 0 || slot >= (int32_t)g_players)
        return;
    netplay_car_state_load(G32(SLOT(slot)), rec, (uint32_t *)(uintptr_t)G32(SLOT(slot) + 0x28), rec + 0xc4,
                           rec + 0x120);
}

/* 0x45c540: a car slot's state sent (0x45e600): the car (0xc4 bytes), its
 * second object (0x5c) and that object's body (0x34) with the network
 * time. */
void GAMEAPI netplay_car_state_send(int32_t slot)
{
    uint8_t rec[0x154];

    if (slot < 0 || slot >= (int32_t)g_players)
        return;
    rec[0x52] = 0; /* a stack leftover in the original */
    netplay_car_pack(G32(SLOT(slot)), A(rec));
    netplay_record_copy((const uint32_t *)(uintptr_t)G32(SLOT(slot) + 0x28), (uint32_t *)(rec + 0xc4));
    netplay_body_pack(G32(G32(SLOT(slot) + 0x28)), A(rec + 0x120));
    uint32_t t = net_time_at();
    netplay_car_state_post(A(rec), 0x154, t, slot);
}

/* 0x45c5e0: 13 dwords from rec to a body's +0x34, then 0x465740 on them. */
void GAMEAPI netplay_body_unpack(uint32_t rec, uint32_t body)
{
    for (uint32_t i = 0; i < 0x34; i += 4)
        U32(body, 0x34 + i) = U32(rec, i);
    body_prepare_at(body + 0x34);
}

/* 0x45c660: 0x17 dwords from src to dst, keeping dst's first two. */
void GAMEAPI netplay_record_copy_keep(const uint32_t *src, uint32_t *dst)
{
    uint32_t d0 = dst[0], d1 = dst[1];
    for (int i = 0; i < 0x17; i++)
        dst[i] = src[i];
    dst[1] = d1;
    dst[0] = d0;
}

/* 0x45c690: the reverse of 0x45c3e0 (car +0x71c kept; +0x2284 / +0x2288
 * sign-extended). */
void GAMEAPI netplay_car_unpack(uint32_t rec, uint32_t car)
{
    netplay_body_unpack(rec, car);
    U16(car, 0x648) = U16(rec, 0x34);
    U16(car, 0x64a) = U16(rec, 0x36);
    U16(car, 0x64c) = U16(rec, 0x38);
    U8(car, 0x661) = U8(rec, 0x3a);
    U16(car, 0x67c) = U16(rec, 0x3c);
    U16(car, 0x690) = U16(rec, 0x40);
    U16(car, 0x692) = U16(rec, 0x42);
    U16(car, 0x694) = U16(rec, 0x3e);
    U32(car, 0x698) = U32(rec, 0x44);
    U32(car, 0x69c) = U32(rec, 0x48);
    uint32_t keep = U32(car, 0x71c);
    I32(car, 0x2284) = S16(rec, 0x4c);
    I32(car, 0x2288) = S16(rec, 0x4e);
    for (uint32_t i = 0; i < 0x48; i += 4)
        U32(car, 0x6b0 + i) = U32(rec, 0x54 + i);
    for (uint32_t i = 0; i < 0x28; i += 4)
        U32(car, 0x6f8 + i) = U32(rec, 0x9c + i);
    U32(car, 0x71c) = keep;
    if (U32(car, 0x6ac) != 0xffffffffu) {
        U8(U32(car, 0x6ac), 0) = U8(rec, 0x50);
        U8(U32(car, 0x6ac), 1) = U8(rec, 0x51);
        U8(U32(car, 0x6ac), 3) = U8(rec, 0x53);
    }
}

/* 0x45c780: a car and its second object (*objp) loaded from a state record,
 * each out of the grid meanwhile. */
void GAMEAPI netplay_car_state_load(uint32_t car, uint32_t rec, uint32_t *objp, uint32_t rec2, uint32_t rec3)
{
    grid_unlink_at(car);
    netplay_car_unpack(rec, car);
    grid_link_at(car);
    netplay_record_copy_keep((const uint32_t *)(uintptr_t)rec2, objp);
    grid_unlink_at(*objp);
    netplay_body_unpack(rec3, *objp);
    grid_link_at(*objp);
}

/* 0x45c7d0: the local car's state sent when 0x2ee ms have passed or its
 * state changed (and the frame is due) unless the frame is skipped; the
 * host (0x45ef30) also sends the cars of free player slots every 0x2ee ms. */
void GAMEAPI netplay_car_state_tick(void)
{
    uint32_t t = game_time_at();
    uint32_t slot = net_local_slot_at();
    int32_t changed = car_state_changed_at(slot);

    if (t > G32(0x4d076c) + 0x2ee)
        goto send;
    if (changed && net_frame_due_at())
        goto send;
    if (net_frame_skip_at())
        goto host;
send:
    G32(0x4d076c) = t;
    netplay_car_state_send((int32_t)slot);
host:
    if (net_bit5_at() != 1)
        return;
    if (t <= G32(0x4d0770) + 0x2ee)
        return;
    G32(0x4d0770) = t;
    if (!g_players)
        return;
    int32_t i = 0;
    do {
        if (player_free_at(i))
            netplay_car_state_send(i);
        i++;
    } while (i < (int32_t)g_players);
}

/* 0x45c880: car slot's weapon `which` (0 / 1) kind byte (car +0x67c). */
void GAMEAPI netplay_weapon_kind_set(int32_t slot, uint32_t kind, int32_t which)
{
    if (slot < 0 || slot >= (int32_t)g_players)
        return;
    U8(G32(SLOT(slot)) + (uint32_t)which, 0x67c) = (uint8_t)kind;
}

/* 0x45c8c0: another player's weapon fired: the kind set (0x45c880), then
 * 0x467900 with twice the age; kinds 0, 1, 2 and 7 complain (debug
 * printf) when the data size is not 0x60; kinds above 7 do nothing. */
void GAMEAPI netplay_weapon_fire(int32_t slot, uint32_t kind, int32_t which, int32_t age, int32_t size,
                                 uint32_t data)
{
    if (slot < 0 || slot >= (int32_t)g_players)
        return;
    if (which < 0 || which >= 2)
        return;
    netplay_weapon_kind_set(slot, kind, which);
    int32_t age2 = (int32_t)((uint32_t)age << 1);
    if (kind > 7)
        return;
    if (size != 0x60) {
        if (kind == 0)
            debug_printf_at(0x4b0e0c);
        else if (kind == 1)
            debug_printf_at(0x4b0dec);
        else if (kind == 2)
            debug_printf_at(0x4b0dc8);
        else if (kind == 7)
            debug_printf_at(0x4b0da4);
    }
    weapon_fire_at(SLOT(slot), which, 0, age2, data);
}

/* 0x45ca30: car slot's look (slot +0x269, +0x26a, +0x26b, word +0x26e). */
void GAMEAPI netplay_car_look_get(int32_t slot, uint8_t *a, uint8_t *b, uint8_t *c, uint16_t *d)
{
    if (slot < 0 || slot >= (int32_t)g_players)
        return;
    *a = G8(SLOT(slot) + 0x269);
    *b = G8(SLOT(slot) + 0x26a);
    *c = G8(SLOT(slot) + 0x26b);
    *d = U16(SLOT(slot), 0x26e);
}

/* 0x45ca90: the reverse of 0x45ca30. */
void GAMEAPI netplay_car_look_set(int32_t slot, const uint8_t *a, const uint8_t *b, const uint8_t *c,
                                  const uint16_t *d)
{
    if (slot < 0 || slot >= (int32_t)g_players)
        return;
    G8(SLOT(slot) + 0x269) = *a;
    G8(SLOT(slot) + 0x26a) = *b;
    G8(SLOT(slot) + 0x26b) = *c;
    U16(SLOT(slot), 0x26e) = *d;
}

/* 0x45caf0: every player marked 10 (active, 0x48cfd0) or 8 in the race
 * table and its car (+0x70c); the options' per-player byte +0x8c set, or
 * cleared for an inactive one while race +0x57c < 6. */
void GAMEAPI netplay_players_mark(void)
{
    int32_t i = 0;

    if (!g_players)
        return;
    do {
        uint32_t car;
        if (net_player_active_at(i)) {
            G8(g_race + (uint32_t)i) = 10;
            car = G32(SLOT(i));
            if (car)
                U8(car, 0x70c) = 10;
            G8(g_options + (uint32_t)i + 0x8c) = 1;
        } else {
            G8(g_race + (uint32_t)i) = 8;
            car = G32(SLOT(i));
            if (car)
                U8(car, 0x70c) = 8;
            if (I32(g_race, 0x57c) < 6)
                G8(g_options + (uint32_t)i + 0x8c) = 0;
        }
        i++;
    } while (i < (int32_t)g_players);
}

/* 0x45cd00: the race settings from the host's block (a copy of the race
 * table, then the options from +0xa9c). Options +0x26 comes from +0xac3,
 * the block's +0x27 (sic). */
void GAMEAPI netplay_settings_apply(uint32_t src)
{
    static const uint16_t bytes[] = {0xe,   0xf,   0x54d, 0x550, 0x551, 0x554, 0x553,
                                     0x555, 0x579, 0x585, 0x586, 0x587, 0x58b, 0x58d, 0x58c};
    for (unsigned k = 0; k < sizeof bytes / sizeof bytes[0]; k++)
        U8(g_race, bytes[k]) = U8(src, bytes[k]);
    U32(g_race, 0x618) = U32(src, 0x618);
    U8(g_race, 0xa86) = U8(src, 0xa86);
    U8(g_race, 0xa87) = U8(src, 0xa87);
    U32(g_race, 0x57c) = U32(src, 0x57c);
    for (uint32_t i = 0; i < 8; i++)
        U8(g_race, i) = U8(src, i);
    for (uint32_t i = 0; i < 6; i++) {
        U8(g_race, 0x10 + i) = U8(src, 0x10 + i);
        U8(g_race, 0x16 + i) = U8(src, 0x16 + i);
        U8(g_race, 0x1c + i) = U8(src, 0x1c + i);
        U8(g_race, 0x8 + i) = U8(src, 0x8 + i);
    }
    U8(g_options, 0x26) = U8(src, 0xac3);
    U8(g_options, 0x87) = U8(src, 0xb23);
    U8(g_options, 0x85) = U8(src, 0xb21);
    U8(g_options, 0x14) = U8(src, 0xab0);
    U8(g_options, 0x15) = U8(src, 0xab1);
    U8(g_options, 0x16) = U8(src, 0xab2);
    U8(g_options, 0x37) = U8(src, 0xad3);
    U8(g_options, 0x93) = U8(src, 0xb2f);
    U8(g_options, 0x2e) = U8(src, 0xaca);
    U8(g_options, 0x43) = U8(src, 0xadf);
}

/* 0x45cf50: a block of head + 0xb3c bytes allocated (0x476ae0): the race
 * table (0xa9c bytes) at +head, the options (0xa0) after it; *size = head +
 * 0xb3c. The copies are made even when the allocation fails (sic). */
uint32_t GAMEAPI netplay_settings_pack(uint32_t head, uint32_t *size)
{
    uint32_t p = mem_alloc_at(head + 0xb3c, 0);
    mem_copy_at(p + head, g_race, 0xa9c);
    mem_copy_at(p + head + 0xa9c, g_options, 0xa0);
    *size = head + 0xb3c;
    return p;
}

/* 0x45cfb0: the five chat text ids (0x4d0758) = -1. */
void GAMEAPI netplay_chat_texts_clear(void)
{
    for (uint32_t a = 0x4d0758; a < 0x4d076c; a += 4)
        G32(a) = 0xffffffffu;
}

/* 0x45cfe0: the chat lines (0x45f5b0, up to 5) shown as texts once 0x4d0774
 * is set: the old texts freed, then (not in a replay / attract mode, when
 * the info's level +0x2c is within race +0x586 and the race mode +0x551 is
 * not 0, 1, 4 or 9) one text per non-empty line, 0x12 apart from y 10 (from
 * the bottom with option +0x25), the colour dropping 0x18 a line; the time
 * stamp 0x4b0da0. */
void GAMEAPI netplay_chat_show(uint32_t info, uint32_t colour)
{
    int32_t count = 5;
    uint32_t lines[5];

    if (!G32(0x4d0774))
        return;
    chat_lines_at(&count, lines);
    for (uint32_t a = 0x4d0758; a < 0x4d076c; a += 4) {
        if (G32(a) != 0xffffffffu) {
            text_free_at(G32(a));
            G32(a) = 0xffffffffu;
        }
    }
    if (G8(0x6cce40) & 0x12)
        return;
    if (U8(info, 0x2c) > U8(g_race, 0x586))
        return;
    uint8_t mode = U8(g_race, 0x551);
    if (mode == 0 || mode == 9 || mode == 1 || mode == 4)
        return;
    int32_t y = 10;
    for (int32_t i = 0; i < count; i++) {
        uint32_t s = lines[i];
        if (s && G8(s)) {
            int32_t ty;
            if (U8(g_options, 0x25))
                ty = (int32_t)U16(g_race, 0xa80) - ((int32_t)((uint32_t)(y + 0x14) * G32(0x6cd410)) >> 8);
            else
                ty = (int32_t)((uint32_t)y * G32(0x6cd410)) >> 8;
            uint32_t x = (uint32_t)U16(g_race, 0xa82) >> 1;
            G32(0x4d0758 + 4 * (uint32_t)i) = (uint32_t)text_create_at(s, 0, x, ty, 0x302, colour, 0);
        }
        colour -= 0x18;
        if (colour > 0xff)
            colour = 0;
        y += 0x12;
    }
    G32(0x4b0da0) = game_time_at();
}

/* 0x45d1e0: the first car slot whose byte +0x2d is 1, else 0. */
int32_t GAMEAPI netplay_slot_flag_find(void)
{
    int32_t n = g_players;
    for (int32_t i = 0; i < n; i++)
        if (G8(SLOT(i) + 0x2d) == 1)
            return i;
    return 0;
}

/* 0x45d210: race table +8..+0xd from src. */
void GAMEAPI netplay_race_bytes_set(const uint8_t *src)
{
    for (uint32_t i = 0; i < 6; i++)
        U8(g_race, 8 + i) = src[i];
}

/* 0x45d230: one received message (type in byte 0) from player `from`. A
 * message 0x48bfb0 rejects gets the bad-message reply (0x45e5b0) with its
 * word +2. See pl_formats.txt for the types. */
void GAMEAPI netplay_message_handle(uint32_t msg, uint32_t size, uint32_t from)
{
    if (!message_check_at(msg, size)) {
        netplay_bad_message_reply(from, U16(msg, 2));
        return;
    }
    switch (U8(msg, 0)) {
    case 1: { /* a player's controls for one step */
        int32_t i = player_of_at(from);
        if (i >= 0 && G32(REC(i) + R_ACTIVE)) {
            if (U32(msg, 0x10) == G32(0x5d59a0) &&
                U32(msg, 0x14) != G32(REC(i) + R_CHECK + U32(msg, 0x10) * 4))
                pl_cheat_line(from, REC(i) + R_NAME);
            G32(REC(i) + R_SEQ) = U32(msg, 0x10);
            uint32_t s = U32(msg, 0x10);
            G32(REC(i) + R_CONTROLS + s * 8) = U32(msg, 8);
            G32(REC(i) + R_CONTROLS + s * 8 + 4) = U32(msg, 0xc);
            G32(REC(i) + R_CHECK + U32(msg, 0x10) * 4) = U32(msg, 0x14);
        }
        if (U8(g_options, 0x93) == 1 && net_bit5_at() && controls_ready_at())
            netplay_controls_relay();
        return;
    }
    case 3: { /* a car's state */
        if (!(G8(0x5d622c) & 8))
            return;
        uint32_t p = U32(msg, 0x14);
        if (U32(msg, 0x18) < G32(REC(p) + R_STATE_TIME))
            return;
        G32(REC(p) + R_STATE_TIME) = U32(msg, 0x18);
        p = U32(msg, 0x14);
        G32(REC(p) + R_STATE_A) = U32(msg, 8);
        G32(REC(p) + R_STATE_B) = U32(msg, 0xc);
        car_state_seen_at(U32(msg, 0x14), U32(msg, 8), U32(msg, 0xc), U32(msg, 0x18));
        netplay_car_state_apply(I32(msg, 0x14), msg + 0x24);
        return;
    }
    case 4: { /* a player left */
        int32_t i = player_of_at(from);
        if (i != -1)
            player_left_at(i);
        return;
    }
    case 5: { /* the race table's per-player bytes and blocks */
        for (uint32_t k = 0, off = 0x24; off < 0x54c; off += 0xdc, k++) {
            U8(g_race, 0x10 + k) = U8(msg, 0xc + k);
            U8(g_race, 0x8 + k) = U8(msg, 0x12 + k);
            if (U32(msg, 8) + 3 != 0) {
                uint32_t c = 0, o = off;
                do {
                    c++;
                    U32(g_race, o) = U32(msg, o - 0xc);
                    o += 4;
                } while (c < U32(msg, 8) + 3);
            }
        }
        return;
    }
    case 6: { /* the player table (from 0x45dde0) */
        G32(0x5d5358) = U32(msg, 8);
        G32(0x5d535c) = 0xffffffffu;
        uint32_t q = msg + 0x10, off = 0;
        for (uint32_t k = 0; k < 6; k++, q += 0x24, off += 0xb) {
            uint32_t r = REC(k);
            G32(r + R_ACTIVE) = U32(q, -4);
            G32(r + R_ID) = U32(q, 0);
            G32(r + R_CAR) = U32(q, 0x14);
            G32(r + R_1C) = U32(q, 0x18);
            G32(r + R_20) = U32(q, 0x1c);
            pl_strcpy(r + R_NAME, q + 4);
            pl_strcpy(g_options + off + 0xd8c, q + 4);
            if (G32(r + R_ACTIVE) && G32(r + R_ID) == G32(0x5d5360)) {
                G32(0x5d535c) = k;
                U8(g_options, 0x84) = (uint8_t)k;
            }
        }
        return;
    }
    case 7:
        netplay_race_bytes_set(PTR(msg + 8));
        return;
    case 8: { /* the race start (from 0x45e200) */
        uint32_t t = game_time_at();
        uint32_t base = t - (uint32_t)pl_ticks_ftol(U32(msg, 0x2c));
        uint32_t look = msg + 0xa;
        for (uint32_t k = 0; k < 6; k++, look += 6) {
            netplay_car_look_set(I32(REC(k), R_CAR), PTR(look - 2), PTR(look - 1), PTR(look),
                                 (const uint16_t *)(uintptr_t)(look + 2));
            G32(REC(k) + R_TIME) = t;
        }
        G32(0x5d59a0) = 0;
        G32(0x5d59a4) = 1;
        G32(0x5d59a8) = 0xfffffffeu;
        G32(0x5d59ac) = 0;
        G32(0x5d59b0) = 1;
        g_net &= ~0x800u;
        g_net &= ~0x400u;
        G32(0x5d5cbc) = 0;
        G32(0x5d5cb8) = 0;
        g_net |= 8;
        g_net = ((U32(msg, 0x30) & 1) << 7) | (g_net & ~0x80u);
        for (uint32_t k = 0; k < 6; k++)
            G32(REC(k) + R_SEQ) = 0;
        G32(0x5d5998) = base;
        return;
    }
    case 9: {
        uint32_t v = U32(msg, 0xc);
        netplay_weapon_kind_set((int32_t)(v & 0x1f), (v >> 10) & 0x1f, (int32_t)((v >> 5) & 0x1f));
        return;
    }
    case 10: { /* a weapon fired: its age in network time, at most 15 */
        int32_t age;
        if (net_time_at() - U32(msg, 8) != 0 && net_time_at() - U32(msg, 8) >= 0xf)
            age = 0xf;
        else if (net_time_at() - U32(msg, 8) == 0)
            age = 0;
        else
            age = (int32_t)(net_time_at() - U32(msg, 8));
        uint32_t v = U32(msg, 0xc);
        netplay_weapon_fire((int32_t)(v & 0x1f), (v >> 10) & 0x1f, (int32_t)((v >> 5) & 0x1f), age,
                            (int32_t)(v >> 15), msg + 0x10);
        return;
    }
    case 11: { /* the clock (from 0x45e340) */
        uint32_t t = game_time_at();
        t -= (uint32_t)pl_ticks_ftol(U32(msg, 8));
        net_time_at();
        G32(0x5d5998) = t;
        return;
    }
    case 12: /* the race settings (from 0x45e400) */
        g_net = ((U32(msg, 8) & 1) << 7) | (g_net & ~0x80u);
        netplay_settings_apply(msg + 0xc);
        netplay_players_mark();
        g_net |= 0x100;
        return;
    case 13:
        netplay_settings_send();
        return;
    case 14: { /* a player's look */
        int32_t i = player_of_at(from);
        if (i == -1)
            return;
        G32(REC(i) + R_LOOK_SENT) = 1;
        netplay_car_look_set(I32(REC(i), R_CAR), PTR(msg + 8), PTR(msg + 9), PTR(msg + 0xa),
                             (const uint16_t *)(uintptr_t)(msg + 0xc));
        return;
    }
    case 15:
        net_pause_at(0);
        return;
    case 16: { /* a player's name and car; the host passes the table on */
        int32_t i = player_of_at(from);
        if (i != -1) {
            G32(REC(i) + R_1C) = U32(msg, 8);
            pl_strcpy(REC(i) + R_NAME, msg + 0x10);
            pl_strcpy(g_options + (uint32_t)i * 0xb + 0xd8c, msg + 0x10);
        }
        if ((G8(0x5d622c) & 0x20) && U32(msg, 0xc)) {
            netplay_players_send();
            uint32_t o = g_options;
            send_setting_at(U8(o, 0x1c), U8(o, 0x1d), U8(o, 0x87), U8(o, 0x8b), U8(o, 0x85));
            menu_dirty_at();
        }
        return;
    }
    case 17:
        G32(0x5d5cc8) = U32(msg, 8);
        for (uint32_t k = 0; k < 6; k++)
            G32(REC(k) + R_20) = U32(msg, 0xc + 4 * k);
        menu_dirty_at();
        return;
    case 18: {
        int32_t i = player_of_at(from);
        if (i != -1)
            G32(REC(i) + R_20) = U32(msg, 8);
        return;
    }
    case 20:
        drop_player_at(player_of_at(from));
        race_abort_at();
        return;
    case 21:
        race_options_at(U32(msg, 8), U32(msg, 0xc), U32(msg, 0x10), U32(msg, 0x14), U32(msg, 0x18));
        return;
    case 22:
        netplay_send_byte(2, 0x17, 0);
        /* fall through */
    case 23:
        race_abort_at();
        g_net &= ~2u;
        if (!net_is_host_at())
            for (uint32_t k = 0; k < 6; k++)
                G32(REC(k) + R_ACTIVE) = 0;
        return;
    case 24:
        results_at(U32(msg, 8));
        return;
    case 25:
        g_net |= 0x400;
        G32(0x5d5cb8) = U32(msg, 8);
        g_net = ((U32(msg, 0xc) & 1) << 13) | (g_net & ~0x2000u);
        return;
    case 26:
        snatched_at(player_of_at(from));
        return;
    case 27:
        g_net |= 0x800;
        G32(0x5d5cbc) = U32(msg, 8);
        G32(0x5d5cc0) = U32(msg, 0xc);
        return;
    case 28: { /* every player's controls (from 0x45e120) */
        uint32_t t = game_time_at();
        for (uint32_t k = 0; k < 6; k++) {
            if (k == G32(0x5d535c) || !G32(REC(k) + R_ACTIVE))
                continue;
            if (U32(msg, 0x38) == G32(0x5d59a0) &&
                U32(msg, 0x3c + 4 * k) != G32(REC(k) + R_CHECK + U32(msg, 0x38) * 4))
                pl_cheat_line(from, REC(k) + R_NAME);
            G32(REC(k) + R_SEQ) = U32(msg, 0x38);
            uint32_t s = U32(msg, 0x38);
            G32(REC(k) + R_CONTROLS + s * 8) = U32(msg, 8 + 8 * k);
            G32(REC(k) + R_CONTROLS + s * 8 + 4) = U32(msg, 0xc + 8 * k);
            G32(REC(k) + R_CHECK + U32(msg, 0x38) * 4) = U32(msg, 0x3c + 4 * k);
            G32(REC(k) + R_TIME) = t;
        }
        return;
    }
    case 31:
        quit_ack_at(from);
        return;
    case 32: {
        int32_t i = player_of_at(from);
        if (i >= 0 && i < 6)
            G32(REC(i) + R_FLAG) = 1;
        return;
    }
    case 33:
        g_net |= 0x4000;
        return;
    case 34:
        net_close_at();
        return;
    default:
        return;
    }
}

/* 0x45ddb0: an 8-byte message whose first byte is `value`. */
int32_t GAMEAPI netplay_send_byte(uint32_t kind, uint32_t value, uint32_t flags)
{
    uint8_t m[8] = {0};

    m[0] = (uint8_t)value;
    return net_send_at(kind, 0, m, 8, flags);
}

/* 0x45dde0: the host's player table to everyone (message 6, 0xe4 bytes);
 * the host becomes player 0. 1 when not the host. */
int32_t GAMEAPI netplay_players_send(void)
{
    uint8_t m[0xe4];

    if (!(G8(0x5d622c) & 0x20))
        return 1;
    G32(0x5d535c) = 0;
    U8(g_options, 0x84) = 0;
    memset(m, 0, sizeof m);
    m[0] = 6;
    U32(m, 8) = G32(0x5d5358);
    for (uint32_t k = 0; k < 6; k++) {
        uint8_t *out = m + 0x10 + k * 0x24;
        uint32_t r = REC(k);
        U32(out, -4) = G32(r + R_ACTIVE);
        U32(out, 0) = G32(r + R_ID);
        U32(out, 0x14) = G32(r + R_CAR);
        U32(out, 0x18) = G32(r + R_1C);
        U32(out, 0x1c) = G32(r + R_20);
        pl_strcpy(A(out + 4), r + R_NAME);
    }
    return net_send_at(2, 0, m, 0xe4, 1) != 0;
}

/* 0x45deb0: a local player's controls for this step: stored (+0x244) when
 * no network game; in one, player 0's (when `send`) are checked against
 * every player's car position sum (0x444050) for the sequence 0x5d59a4,
 * stored and sent (message 1) or, for the host with option +0x93 == 1,
 * relayed (0x45e120). 0 when the lock (0x48c3f0) or the send fails. */
int32_t GAMEAPI netplay_controls_send(int32_t player, uint32_t a, uint32_t b, int32_t send)
{
    uint8_t m[0x18];

    if (!net_game_at()) {
        G32(REC(player) + R_LOCAL_A) = a;
        G32(REC(player) + R_LOCAL_B) = b;
        return 1;
    }
    if (player != 0 || !send)
        return 1;
    if (!(G8(0x5d622c) & 2))
        return 1;
    if (!U8(g_options, 0x37) && (G8(0x4c2458) & 1))
        return 1;
    uint32_t lock = net_lock_at(1);
    if (!lock)
        return 0;
    for (uint32_t k = 0; k < 6; k++) {
        uint32_t r = REC(k);
        if (!G32(r + R_ACTIVE))
            continue;
        uint32_t v = (uint32_t)car_position_sum_at(G32(r + R_CAR));
        if (G32(r + R_SEQ) == G32(0x5d59a4) && v != G32(r + R_CHECK + G32(0x5d59a4) * 4))
            pl_cheat_line(G32(r + R_ID), r + R_NAME);
        G32(r + R_CHECK + G32(0x5d59a4) * 4) = v;
    }
    uint32_t me = REC(G32(0x5d535c));
    uint32_t seq = G32(0x5d59a4);
    G32(me + R_SEQ) = seq;
    G32(me + R_CONTROLS + seq * 8) = a;
    G32(me + R_CONTROLS + seq * 8 + 4) = b;
    memset(m, 0, sizeof m);
    m[0] = 1;
    U32(m, 8) = a;
    U32(m, 0xc) = b;
    U32(m, 0x10) = seq;
    U32(m, 0x14) = G32(me + R_CHECK + seq * 4);
    int32_t ok;
    if (U8(g_options, 0x93) == 1) {
        if (net_bit5_at()) {
            G32(0x5d59a8) = G32(0x5d59a4);
            if (controls_ready_at())
                netplay_controls_relay();
            net_unlock_at(lock);
            return 1;
        }
        ok = net_send_at(1, 0, m, 0x18, 0xe);
    } else {
        ok = net_send_at(2, 0, m, 0x18, 0xe);
    }
    net_unlock_at(lock);
    return ok ? 1 : 0;
}

/* 0x45e120: the host relays every player's controls and checksums for the
 * sequence 0x5d59a8 (message 0x1c, 0x54 bytes) once per sequence (last
 * 0x4b0e30). */
int32_t GAMEAPI netplay_controls_relay(void)
{
    uint8_t m[0x54];

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2) || !(G8(0x5d622c) & 0x20) || !U8(g_options, 0x93) ||
        G32(0x4b0e30) == G32(0x5d59a8))
        return 1;
    G32(0x4b0e30) = G32(0x5d59a8);
    memset(m, 0, sizeof m);
    m[0] = 0x1c;
    uint32_t s = G32(0x5d59a8);
    U32(m, 0x38) = s;
    for (uint32_t k = 0; k < 6; k++) {
        U32(m, 8 + 8 * k) = G32(REC(k) + R_CONTROLS + s * 8);
        U32(m, 0xc + 8 * k) = G32(REC(k) + R_CONTROLS + s * 8 + 4);
        U32(m, 0x3c + 4 * k) = G32(REC(k) + R_CHECK + s * 4);
    }
    return net_send_at(2, 0, m, 0x54, 0xe) != 0;
}

/* 0x45e200: the host starts the race: sequences reset, the network clock
 * zeroed at timeGetTime, message 8 (0x34 bytes: every slot's look, the
 * network time, option +0x83) to everyone; network bit 3 set when sent.
 * 1 when not the host or sent. */
int32_t GAMEAPI netplay_race_start_send(void)
{
    uint8_t m[0x34];
    uint32_t t = game_time_at();

    if (!(G8(0x5d622c) & 0x20))
        return 1;
    G32(0x5d59a0) = 0;
    G32(0x5d59a4) = 1;
    G32(0x5d59a8) = 0xfffffffeu;
    G32(0x5d59ac) = 0;
    G32(0x5d59b0) = 1;
    g_net &= ~0x800u;
    g_net &= ~0x400u;
    G32(0x5d5cbc) = 0;
    G32(0x5d5cb8) = 0;
    G32(0x5d5998) = pl_timeGetTime();
    memset(m, 0, sizeof m);
    m[0] = 8;
    U32(m, 0x2c) = net_time_at();
    U32(m, 0x30) = U8(g_options, 0x83);
    g_net = ((uint32_t)(U8(g_options, 0x83) & 1) << 7) | (g_net & ~0x80u);
    for (uint32_t k = 0; k < 6; k++) {
        uint8_t *look = m + 0xa + 6 * k;
        netplay_car_look_get(I32(REC(k), R_CAR), look - 2, look - 1, look, (uint16_t *)(look + 2));
        G32(REC(k) + R_TIME) = t;
    }
    for (uint32_t k = 0; k < 6; k++)
        G32(REC(k) + R_SEQ) = 0;
    if (!net_send_at(2, 0, m, 0x34, 1))
        return 0;
    g_net |= 8;
    return 1;
}

/* 0x45e340: the host sends the network time (message 0xb) every 60 s. */
int32_t GAMEAPI netplay_clock_send(void)
{
    uint8_t m[0xc];

    if ((G8(0x5d622c) & 1) && (G8(0x5d622c) & 2) && (G8(0x5d622c) & 0x80) && (G8(0x5d622c) & 0x20)) {
        uint32_t now = pl_timeGetTime();
        if (now > G32(0x4d0878) + 60000) {
            G32(0x4d0878) = now;
            memset(m, 0, sizeof m);
            m[0] = 0xb;
            U32(m, 8) = net_time_at();
            return net_send_at(2, 0, m, 0xc, 0);
        }
    }
    return 1;
}

/* 0x45e3c0: race +0x54d = 1 + the last player record with active == 1
 * (0 if none), unless race mode +0x551 is 5. */
void GAMEAPI netplay_player_count_update(void)
{
    uint8_t n = 0;
    for (uint32_t k = 0; k < 6; k++)
        if (G32(REC(k) + R_ACTIVE) == 1)
            n = (uint8_t)(k + 1);
    if (U8(g_race, 0x551) != 5)
        U8(g_race, 0x54d) = n;
}

/* 0x45e400: the host sends the race settings (message 0xc: 0xc-byte head,
 * the race table and options from 0x45cf50, freed after); network bit 8
 * set when sent. */
int32_t GAMEAPI netplay_settings_send(void)
{
    uint32_t size = 0;

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2) || !(G8(0x5d622c) & 0x20))
        return 1;
    netplay_player_count_update();
    uint32_t p = netplay_settings_pack(0xc, &size);
    if (!p)
        return 1;
    netplay_players_mark();
    U8(p, 0) = 0xc;
    U32(p, 8) = (g_net >> 7) & 1;
    int32_t ok = net_send_at(2, 0, p, size, 2);
    mem_free_at(p);
    if (!ok)
        return 0;
    g_net |= 0x100;
    return 1;
}

/* 0x45e4a0: the local player's look to the host (message 0xe); the host
 * just marks its own as sent. */
int32_t GAMEAPI netplay_look_send(void)
{
    uint8_t m[0x10];

    if (!(G8(0x5d622c) & 1))
        return 1;
    if (!(G8(0x5d622c) & 2))
        return 1;
    if (G8(0x5d622c) & 0x20) {
        G32(REC(G32(0x5d535c)) + R_LOOK_SENT) = 1;
        return 1;
    }
    memset(m, 0, sizeof m);
    m[0] = 0xe;
    netplay_car_look_get(I32(REC(G32(0x5d535c)), R_CAR), m + 8, m + 9, m + 0xa, (uint16_t *)(m + 0xc));
    return net_send_at(1, 0, m, 0x10, 2);
}

/* 0x45e5b0: message 0x65 (0xc bytes, `seq` at +8) to one player. */
int32_t GAMEAPI netplay_bad_message_reply(uint32_t to, uint32_t seq)
{
    uint8_t m[0xc];

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2))
        return 1;
    memset(m, 0, sizeof m);
    m[0] = 0x65;
    U32(m, 8) = seq;
    return net_send_at(0, to, m, 0xc, 4) != 0;
}

/* 0x45e600: a car state to everyone (message 3: the slot's state words,
 * 0x5d59b0, slot, time, 1, size, then the data), in a game with network
 * bit 7; 0 (and a debug line) when size + 0x28 is over 0x400. */
int32_t GAMEAPI netplay_car_state_post(uint32_t data, uint32_t size, uint32_t time, int32_t slot)
{
    uint8_t m[0x400];

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2) || !(G8(0x5d622c) & 0x80))
        return 1;
    if (size + 0x28 > 0x400) {
        debug_printf_at(0x4b0e48);
        return 0;
    }
    memset(m, 0, 0x24);
    m[0] = 3;
    U32(m, 8) = G32(REC(slot) + R_STATE_A);
    U32(m, 0xc) = G32(REC(slot) + R_STATE_B);
    U32(m, 0x10) = G32(0x5d59b0);
    U32(m, 0x1c) = 1;
    U32(m, 0x14) = (uint32_t)slot;
    U32(m, 0x18) = time;
    U32(m, 0x20) = size;
    mem_copy_at(m + 0x24, data, size);
    return net_send_at(2, 0, m, size + 0x24, 4);
}
