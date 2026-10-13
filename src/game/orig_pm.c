/* Group PM: the original's still-reachable functions in 0x45e6f0-0x460a90
 * (the rest of the network game's source file, after group PL's
 * 0x45c340-0x45e600) and two of the front end's sparks (0x462ae0,
 * 0x462bb0).
 *
 *  - the network clock (0x45eee0) and the controls by step: stored per
 *    player with a 64-entry history (0x45ea60, 0x45ee60), changed /
 *    ready checks (0x45ee10, 0x45eb60), the wait for every player's
 *    controls with its time-out (0x45ebc0), the sequence step (0x45ef40);
 *  - messages sent: a picked-up weapon (0x45e6f0, type 10), a weapon kind
 *    (0x45e800, 9), the cars chosen (0x45f0c0, 15), the players' states
 *    (0x45f310, 17), the menu mode (0x45f3a0, 18), the name (0x45f7b0, 16),
 *    the results (0x45f990, 24 / 25), a pause (0x45fa60, 27 / 26), a
 *    cheat (0x45fb90, 20), the race results (0x45fca0, 5), the finish
 *    (0x45fc40, 31), all paused (0x460a20, 33), snatched (0x460a90, 34);
 *  - the waits at the race start (0x45ef90, 0x45f040), the timed results
 *    and pause (0x45fde0, 0x45fe70), the state predicates (0x45eba0,
 *    0x45eeb0, 0x45f030, 0x45f0b0, 0x45f390, 0x45f530, 0x45f970), the
 *    players' state counts (0x45f270, 0x45f2c0), the chat lines and the
 *    players' list (0x45f5b0, 0x45f650), the player records compacted
 *    (0x4609b0);
 *  - the kill / weapon messages: the joke lines (0x4600a0) and the
 *    templated ones (0x45ff00);
 *  - the front end's sparks: spawn (0x462ae0) and draw (0x462bb0, line
 *    primitives into the ordering table; x87 float steps as the
 *    original's instructions).
 *
 * The player records: 6 of 0x66c bytes at 0x5d2cd0 (build/groups/
 * pl_formats.txt, pm_formats.txt). Message buffers on the stack: the
 * original leaves the bytes it does not fill (1..7 of every header, the
 * top bits of message 9's word, gaps of message 5) as stack leftovers;
 * here they are 0. Everything outside the group is called by address. */
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
#define g_step G32(0x4c2458)
#define g_local G32(0x5d535c)

/* The network player records: 0x66c bytes at 0x5d2cd0 (32-bit wrap for
 * wild indices, as the original's address arithmetic). */
#define REC(i) (0x5d2cd0u + (uint32_t)(i) * 0x66cu)
#define REC_END 0x5d5358u

typedef int32_t(GAMEAPI *pm_fn0)(void);
typedef int32_t(GAMEAPI *pm_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *pm_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *pm_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *pm_fn4)(uint32_t a, uint32_t b, uint32_t c, uint32_t d);
typedef int32_t(GAMEAPI *pm_fn5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
#define CALL0(a) (((pm_fn0)(a))())
#define CALL1(a, x) (((pm_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((pm_fn2)(a))(A(x), A(y)))
#define CALL3(a, x, y, z) (((pm_fn3)(a))(A(x), A(y), A(z)))
#define CALL4(a, x, y, z, w) (((pm_fn4)(a))(A(x), A(y), A(z), A(w)))
#define CALL5(a, x, y, z, w, v) (((pm_fn5)(a))(A(x), A(y), A(z), A(w), A(v)))

typedef uint32_t(STDCALL *pm_api0)(void);
#define pm_timeGetTime_ptr() (*(pm_api0 *)(uintptr_t)0x4a32b4)

/* Outside the group. */
#define game_time_at() ((uint32_t)CALL0(0x43692a))
#define sim_clock_start_at() CALL0(0x43693f)
#define debug_printf_at(s) CALL1(0x476ec0, s)
#define debug_printf1_at(s, v) CALL2(0x476ec0, s, v)
#define mem_copy_at(d, s, n) CALL3(0x476f30, d, s, n)
#define mem_set_at(d, v, n) CALL3(0x476f50, d, v, n)
#define net_send_at(kind, to, msg, size, flags) CALL5(0x48cdd0, kind, to, msg, size, flags)
#define player_of_at(from) CALL1(0x48bd40, from)
#define drop_player_at(i) CALL1(0x48bf70, i)
#define net_pump_a_at() CALL0(0x48c630)
#define net_pump_b_at() CALL0(0x48c460)
#define net_local_slot_at() ((uint32_t)CALL0(0x48cf20))
#define send_byte_at(kind, value, flags) CALL3(0x45ddb0, kind, value, flags)
#define players_send_at() CALL0(0x45dde0)
#define race_start_send_at() CALL0(0x45e200)
#define slot_flag_find_at() ((uint32_t)CALL0(0x45d1e0))
#define net_is_host_at() CALL0(0x45eed0)
#define net_game_at() CALL0(0x45ef80)
#define text_line_get_at(buf, size, sec, idx) CALL4(0x42e6d0, buf, size, sec, idx)
#define sprintf2_at(buf, fmt) CALL2(0x4979f0, buf, fmt)
#define sprintf3_at(buf, fmt, a) CALL3(0x4979f0, buf, fmt, a)
#define sprintf4_at(buf, fmt, a, b) CALL4(0x4979f0, buf, fmt, a, b)
#define chat_post_at(s) CALL1(0x45d150, s)
#define driver_name_at(buf, car) CALL2(0x4484a0, buf, car)
#define chat_send_at(s) CALL1(0x48d270, s)
#define rand_at() CALL0(0x4987b5)
#define vertex_clip_at(v) CALL1(0x426b80, v)
#define word_exchange_at(ot, first, last) CALL3(0x429080, ot, first, last)
#define dword_store_at(p, v) CALL2(0x4290a0, p, v)

#define CRT_FTOL 0x497a80

/* This group's functions (also in build/groups/pm_protos.txt for physics.h). */
int32_t GAMEAPI net_pickup_send(int32_t slot, int32_t which, uint32_t kind, uint32_t data, uint32_t size); /* 0x45e6f0 */
int32_t GAMEAPI net_weapon_kind_send(int32_t slot, int32_t which, uint32_t kind); /* 0x45e800 */
int32_t GAMEAPI net_controls_store(uint32_t a, uint32_t b); /* 0x45ea60 */
int32_t GAMEAPI net_controls_ready(void); /* 0x45eb60 */
int32_t GAMEAPI net_race_over(void); /* 0x45eba0 */
int32_t GAMEAPI net_controls_wait(void); /* 0x45ebc0 */
int32_t GAMEAPI net_controls_changed(uint32_t i); /* 0x45ee10 */
void GAMEAPI net_controls_history_set(uint32_t i, uint32_t a, uint32_t b, uint32_t t); /* 0x45ee60 */
int32_t GAMEAPI net_player_free(uint32_t i); /* 0x45eeb0 */
int32_t GAMEAPI net_time(void); /* 0x45eee0 */
void GAMEAPI net_sequence_advance(void); /* 0x45ef40 */
int32_t GAMEAPI net_wait_players_ready(void); /* 0x45ef90 */
int32_t GAMEAPI net_race_started(void); /* 0x45f030 */
int32_t GAMEAPI net_wait_race_start(void); /* 0x45f040 */
int32_t GAMEAPI net_settings_sent(void); /* 0x45f0b0 */
int32_t GAMEAPI net_cars_collect(void); /* 0x45f0c0 */
void GAMEAPI net_states_count(uint8_t *out); /* 0x45f270 */
int32_t GAMEAPI net_states_changed(void); /* 0x45f2c0 */
int32_t GAMEAPI net_states_send(void); /* 0x45f310 */
int32_t GAMEAPI net_menu_mode_unset(void); /* 0x45f390 */
int32_t GAMEAPI net_menu_mode_send(void); /* 0x45f3a0 */
int32_t GAMEAPI net_all_finished(void); /* 0x45f530 */
void GAMEAPI net_chat_lines(int32_t *count, uint32_t *lines); /* 0x45f5b0 */
void GAMEAPI net_player_list(int32_t *count, uint32_t *ids, uint32_t *names); /* 0x45f650 */
int32_t GAMEAPI net_name_send(uint32_t flag); /* 0x45f7b0 */
int32_t GAMEAPI net_results_allowed(void); /* 0x45f970 */
int32_t GAMEAPI net_results_send(uint32_t value); /* 0x45f990 */
int32_t GAMEAPI net_pause_request(uint32_t player); /* 0x45fa60 */
int32_t GAMEAPI net_player_cheated(uint32_t from); /* 0x45fb90 */
int32_t GAMEAPI net_finish_send(void); /* 0x45fc40 */
int32_t GAMEAPI net_race_results_send(int32_t to); /* 0x45fca0 */
int32_t GAMEAPI net_results_check(void); /* 0x45fde0 */
int32_t GAMEAPI net_pause_due(uint32_t *player); /* 0x45fe70 */
void GAMEAPI net_message_format(int32_t idx, uint32_t s1, uint32_t s2, char *out, uint32_t size); /* 0x45ff00 */
void GAMEAPI net_kill_message(uint32_t victim, uint32_t attacker, uint32_t kind); /* 0x4600a0 */
void GAMEAPI net_players_compact(void); /* 0x4609b0 */
void GAMEAPI net_all_paused(void); /* 0x460a20 */
int32_t GAMEAPI net_snatched_send(void); /* 0x460a90 */
void GAMEAPI sparks_spawn(uint32_t n, uint32_t x, uint32_t y); /* 0x462ae0 */
void GAMEAPI sparks_draw(uint32_t depth); /* 0x462bb0 */

/* ---- small helpers: the original's inline string code ---- */

static uint32_t pm_strlen(const char *s)
{
    uint32_t n = 0;

    while (s[n])
        n++;
    return n;
}

static void pm_strcpy(char *d, const char *s)
{
    uint32_t i = 0;

    do
        d[i] = s[i];
    while (s[i++]);
}

static void pm_strcat(char *d, const char *s)
{
    pm_strcpy(d + pm_strlen(d), s);
}

/* The two network pumps the waits run. */
static void pm_pump(void)
{
    net_pump_a_at();
    net_pump_b_at();
}

/* ---- x87: the original's instructions ---- */

/* fild dword; fstp dword. */
static inline uint32_t x87_int_to_float(int32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fld a; fadd b; fstp. */
static inline uint32_t x87_add(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfadds %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fld a; fsub b; fstp. */
static inline uint32_t x87_sub(uint32_t a, uint32_t b)
{
    uint32_t r;
    __asm__ volatile("flds %1\n\tfsubs %2\n\tfstps %0" : "=m"(r) : "m"(a), "m"(b) : "st");
    return r;
}

/* fild qword (v, 0); fstp dword; fld dword; fmul dword 0x4a33d0; fstp. */
static inline uint32_t x87_depth(uint32_t v)
{
    uint64_t q = v;
    uint32_t r;
    __asm__ volatile("fildq %1\n\tfstps %0\n\tflds %0\n\tfmuls 0x4a33d0\n\tfstps %0"
                     : "=m"(r)
                     : "m"(q)
                     : "st");
    return r;
}

/* ---- the network clock and controls ---- */

/* 0x45eee0: network time: (timeGetTime - 0x5d5998) as an unsigned 64-bit
 * integer to float, / the float at 0x4a33cc, through __ftol. */
int32_t GAMEAPI net_time(void)
{
    uint64_t q = (uint32_t)(pm_timeGetTime_ptr()() - G32(0x5d5998));
    int32_t r;

    __asm__ volatile("fildq %[q]\n\t"
                     "fstps %[q]\n\t"
                     "flds %[q]\n\t"
                     "fdivs 0x4a33cc\n\t"
                     "call *%[ftol]"
                     : "=a"(r), [q] "+m"(q)
                     : [ftol] "S"(CRT_FTOL)
                     : "ecx", "edx", "memory", "cc", X87);
    return r;
}

/* 0x45e6f0: message 10, a weapon picked up / fired: +8 time, +0xc slot
 * (bits 0-4), which (5-9), kind (10-14), size (15-), +0x10 the data
 * (size bytes); 0 and a debug line when size + 0x14 > 0x100. Only when
 * playing, connected, with the host clock (bits 0, 1, 7). */
int32_t GAMEAPI net_pickup_send(int32_t slot, int32_t which, uint32_t kind, uint32_t data, uint32_t size)
{
    uint8_t msg[0x100];

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2) || !(G8(0x5d622c) & 0x80))
        return 1;
    if (size + 0x14 > 0x100) {
        debug_printf_at(0x4b0e70);
        return 0;
    }
    if (slot < 0 || slot >= 6 || which < 0 || which >= 2)
        return 1;
    memset(msg, 0, 8);
    msg[0] = 0xa;
    U32(msg, 8) = (uint32_t)net_time();
    U32(msg, 0xc) = ((uint32_t)slot & 0x1f) | ((uint32_t)which & 0x1f) << 5 | (kind & 0x1f) << 10 | size << 15;
    mem_copy_at(msg + 0x10, data, size);
    return net_send_at(2, 0, msg, size + 0x10, 4);
}

/* 0x45e800: message 9, a weapon kind: +8 time, +0xc slot (bits 0-4),
 * which (5-9), kind (10-14); bits 15- are stack leftovers in the
 * original, 0 here. */
int32_t GAMEAPI net_weapon_kind_send(int32_t slot, int32_t which, uint32_t kind)
{
    uint8_t msg[0x10];

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2) || !(G8(0x5d622c) & 0x80))
        return 1;
    if (slot < 0 || slot >= 6 || which < 0 || which >= 2)
        return 1;
    memset(msg, 0, sizeof msg);
    msg[0] = 9;
    U32(msg, 8) = (uint32_t)net_time();
    U32(msg, 0xc) = (((kind & 0x1f) << 5 | ((uint32_t)which & 0x1f)) << 5) | ((uint32_t)slot & 0x1f);
    return net_send_at(2, 0, msg, 0x10, 4);
}

/* 0x45ea60: this step's controls: every record's previous controls
 * (+0x34 / +0x38) = current (+0x2c / +0x30); with the host clock (bit 7)
 * the local player's current = (a, b) and every record's history entry
 * (+0x354 + n * 12: time, a, b; n = +0x654, then +1 & 0x3f) is written;
 * otherwise the current controls come from +0x44 + seq * 8 (seq
 * 0x5d59a0). 0x5d599c = the time. Skipped on odd steps when options
 * +0x37 is 0. Returns the time (0x45eee0). */
int32_t GAMEAPI net_controls_store(uint32_t a, uint32_t b)
{
    int32_t t = net_time();
    uint32_t i;

    if (U8(g_options, 0x37) == 0 && (G8(0x4c2458) & 1))
        return t;
    for (i = 0; i < 6; i++) {
        uint32_t r = REC(i);

        U32(r, 0x34) = U32(r, 0x2c);
        U32(r, 0x38) = U32(r, 0x30);
        if (G8(0x5d622c) & 0x80) {
            if (i == g_local) {
                U32(r, 0x2c) = a;
                U32(r, 0x30) = b;
            }
            U32(r + U32(r, 0x654) * 12, 0x354) = (uint32_t)t;
            {
                uint32_t sa = U32(r, 0x2c), sb = U32(r, 0x30);
                uint32_t e = r + U32(r, 0x654) * 12;

                U32(e, 0x358) = sa;
                U32(e, 0x35c) = sb;
            }
            U32(r, 0x654) = U32(r, 0x654) + 1;
            U32(r, 0x654) = U32(r, 0x654) & 0x3f;
        } else {
            uint32_t s = G32(0x5d59a0);

            U32(r, 0x2c) = U32(r + s * 8, 0x44);
            U32(r, 0x30) = U32(r + s * 8, 0x48);
        }
    }
    G32(0x5d599c) = (uint32_t)t;
    return t;
}

/* 0x45eb60: every active player's controls sequence (+0x3c) is 0x5d59a8
 * (or not playing / connected). */
int32_t GAMEAPI net_controls_ready(void)
{
    uint32_t r;

    if ((G8(0x5d622c) & 1) && (G8(0x5d622c) & 2))
        for (r = REC(0); r < REC_END; r += 0x66c)
            if (U32(r, 0) && U32(r, 0x3c) != G32(0x5d59a8))
                return 0;
    return 1;
}

/* 0x45eba0: the race is over (no 0x4ac984 and race +0x57c >= 7). */
int32_t GAMEAPI net_race_over(void)
{
    return G32(0x4ac984) == 0 && I32(g_race, 0x57c) >= 7;
}

/* 0x45ebc0: waits (pumping the network) until every active player's
 * controls sequence is 0x5d59a0 or 0x5d59a4, or the race is over; then
 * the wait's game time goes into 0x5d5978 / 0x5d597c (average) and 1.
 * After 0x5d2ccc ms (timeGetTime against the game time at the start, sic)
 * each player still behind gets the line PC_NETWORK_MESSAGES 5 with the
 * name and is dropped; 0. Skipped (1) when not playing, or on odd steps
 * when options +0x37 is 0. */
int32_t GAMEAPI net_controls_wait(void)
{
    char line[0x80], text[0x80];
    uint32_t start = game_time_at();
    pm_api0 now;
    uint32_t r, i;
    int32_t waiting;

    if (!(G8(0x5d622c) & 1))
        return 1;
    if (U8(g_options, 0x37) == 0 && !(G8(0x4c2458) & 1))
        return 1;
    now = pm_timeGetTime_ptr();
    while (G8(0x5d622c) & 2) {
        pm_pump();
        waiting = 0;
        for (r = REC(0); r < REC_END; r += 0x66c)
            if (U32(r, 0) && U32(r, 0x3c) != G32(0x5d59a0) && U32(r, 0x3c) != G32(0x5d59a4)) {
                waiting = 1;
                break;
            }
        if (net_race_over())
            waiting = 0;
        if (now() > G32(0x5d2ccc) + start) {
            for (i = 0, r = REC(0); r < REC_END; r += 0x66c, i++) {
                if (!U32(r, 0) || U32(r, 0x3c) == G32(0x5d59a0) || U32(r, 0x3c) == G32(0x5d59a4))
                    continue;
                if (text_line_get_at(line, 0x80, 0x4b0e34, 5) != -1) {
                    sprintf3_at(text, line, r + 8);
                    chat_post_at(text);
                }
                drop_player_at(i);
            }
            return 0;
        }
        if (!waiting)
            break;
    }
    G32(0x5d5978) = game_time_at() + (G32(0x5d5978) - start);
    G32(0x5d597c) = G32(0x5d597c) + 1;
    return 1;
}

/* 0x45ee10: player i is active and its controls changed this step
 * (+0x2c / +0x30 against +0x34 / +0x38). */
int32_t GAMEAPI net_controls_changed(uint32_t i)
{
    uint32_t r = REC(i);

    if (U32(r, 0) == 0)
        return 0;
    return U32(r, 0x2c) != U32(r, 0x34) || U32(r, 0x30) != U32(r, 0x38);
}

/* 0x45ee60: player i's controls history entries from time t on get
 * (a, b). */
void GAMEAPI net_controls_history_set(uint32_t i, uint32_t a, uint32_t b, uint32_t t)
{
    uint32_t e = REC(i) + 0x358;
    int k;

    for (k = 0; k < 0x40; k++, e += 0xc)
        if (U32(e, -4) >= t) {
            U32(e, 0) = a;
            U32(e, 4) = b;
        }
}

/* 0x45eeb0: player record i is free. */
int32_t GAMEAPI net_player_free(uint32_t i)
{
    return U32(REC(i), 0) == 0;
}

/* 0x45ef40: the controls sequences step: checked 0x5d59a0 = sending
 * 0x5d59a4, which goes on (& 0x3f). Skipped on odd steps when options
 * +0x37 is 0. */
void GAMEAPI net_sequence_advance(void)
{
    if (U8(g_options, 0x37) == 0 && (G8(0x4c2458) & 1))
        return;
    G32(0x5d59a0) = G32(0x5d59a4);
    G32(0x5d59a4) = G32(0x5d59a4) + 1;
    G32(0x5d59a4) = G32(0x5d59a4) & 0x3f;
}

/* 0x45ef90: the host waits up to 10 s (pumping) for every player with
 * +0 == 1 to set +0x24; those that did not are dropped. 1. */
int32_t GAMEAPI net_wait_players_ready(void)
{
    pm_api0 now = pm_timeGetTime_ptr();
    uint32_t end = now();
    uint32_t r, i;

    if (!(G8(0x5d622c) & 0x20))
        return 1;
    end += 10000;
    if (now() < end) {
        do {
            for (i = 0, r = REC(0); r < REC(6); r += 0x66c, i++)
                if (U32(r, 0) == 1 && U32(r, 0x24) == 0)
                    break;
            if (i == 6)
                return 1;
            pm_pump();
        } while (now() < end);
    }
    for (i = 0, r = REC(0); r < REC(6); r += 0x66c, i++)
        if (U32(r, 0) == 1 && U32(r, 0x24) == 0)
            drop_player_at(i);
    return 1;
}

/* 0x45f030: state bit 3 (race started). */
int32_t GAMEAPI net_race_started(void)
{
    return (g_net >> 3) & 1;
}

/* 0x45f040: the race start: the host sends it (0x45e200, 0 on failure);
 * then up to 10 s (pumping) until state bit 3. 0 when not playing /
 * connected, else 1. */
int32_t GAMEAPI net_wait_race_start(void)
{
    pm_api0 now = pm_timeGetTime_ptr();
    uint32_t end = now();
    int32_t r;

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2))
        return 0;
    if (G8(0x5d622c) & 0x20) {
        r = race_start_send_at();
        if (r == 0)
            return 0;
    }
    end += 10000;
    if (now() < end)
        do {
            if (net_race_started())
                break;
            pm_pump();
        } while (now() < end);
    return 1;
}

/* 0x45f0b0: state bit 8 (settings sent). */
int32_t GAMEAPI net_settings_sent(void)
{
    return (g_net >> 8) & 1;
}

/* 0x45f0c0: the host collects the cars: every record's car (+0x1c) = -1,
 * its own = options +0x18; message 15 (8 bytes) asks the others; up to
 * 10 s (pumping) for every player with +0 == 1 to answer; the records are
 * compacted (0x4609b0), race +0x1c.. = the cars (0xff none), and each
 * slot still without one gets the lowest car nobody has (or its index).
 * 1 (0 when the send fails); 1 when not the host. */
int32_t GAMEAPI net_cars_collect(void)
{
    pm_api0 now = pm_timeGetTime_ptr();
    uint32_t end = now();
    uint8_t msg[8];
    uint32_t r, i, c, e;
    int32_t sent;

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2) || !(G8(0x5d622c) & 0x20))
        return 1;
    for (r = REC(0) + 0x1c; r < REC(6) + 0x1c; r += 0x66c)
        U32(r, 0) = 0xffffffff;
    U32(REC(g_local), 0x1c) = U8(g_options, 0x18);
    memset(msg, 0, sizeof msg);
    msg[0] = 0xf;
    sent = net_send_at(2, 0, msg, 8, 1);
    if (sent == 0)
        return 0;
    end += 10000;
    if (now() < end)
        do {
            for (i = 0, r = REC(0); r < REC(6); r += 0x66c, i++)
                if (U32(r, 0) == 1 && U32(r, 0x1c) == 0xffffffff)
                    break;
            if (i == 6)
                break;
            pm_pump();
        } while (now() < end);
    net_players_compact();
    for (i = 0, r = REC(0); r < REC(6); r += 0x66c, i++) {
        if (U32(r, 0) == 1 && U32(r, 0x1c) != 0xffffffff)
            U8(g_race, 0x1c + i) = U8(r, 0x1c);
        else
            U8(g_race, 0x1c + i) = 0xff;
    }
    for (i = 0; i < 6; i++) {
        if (U8(g_race, 0x1c + i) != 0xff)
            continue;
        for (c = 0;;) {
            for (e = 0; e < 6; e++)
                if (U8(g_race, 0x1c + e) == c)
                    break;
            if (e >= 6) {
                U8(g_race, 0x1c + i) = (uint8_t)c;
                break;
            }
            if (++c >= 6)
                break;
        }
        if (U8(g_race, 0x1c + i) == 0xff)
            U8(g_race, 0x1c + i) = (uint8_t)i;
    }
    return 1;
}

/* 0x45f270: out[0] = active players, out[1] = those in state (+0x20) 4,
 * out[2] = in state 5. */
void GAMEAPI net_states_count(uint8_t *out)
{
    uint32_t r;

    out[2] = 0;
    out[1] = 0;
    out[0] = 0;
    for (r = REC(0); r < REC(6); r += 0x66c) {
        if (U32(r, 0) == 0)
            continue;
        out[0]++;
        if (U32(r, 0x20) == 4)
            out[1]++;
        if (U32(r, 0x20) == 5)
            out[2]++;
    }
}

/* 0x45f2c0: the host recounts the states into 0x5d5cc8 (old in 0x5d5cc4):
 * 1 when the active count or the state-5 count changed. */
int32_t GAMEAPI net_states_changed(void)
{
    if (!(G8(0x5d622c) & 0x20))
        return 0;
    G32(0x5d5cc4) = G32(0x5d5cc8);
    net_states_count(PTR(0x5d5cc8));
    if (G8(0x5d5cc8) == G8(0x5d5cc4) && G8(0x5d5cca) == G8(0x5d5cc6))
        return 0;
    return 1;
}

/* 0x45f310: message 17 (0x24): +8 the counts 0x5d5cc8, +0xc the six
 * states (+0x20). Host only; 1 when sent. */
int32_t GAMEAPI net_states_send(void)
{
    uint8_t msg[0x24];
    uint32_t r, k;

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2) || !(G8(0x5d622c) & 0x20))
        return 0;
    memset(msg, 0, 8);
    msg[0] = 0x11;
    U32(msg, 8) = G32(0x5d5cc8);
    for (k = 0xc, r = REC(0) + 0x20; r < REC(6) + 0x20; r += 0x66c, k += 4)
        U32(msg, k) = U32(r, 0);
    return net_send_at(2, 0, msg, 0x24, 4) != 0;
}

/* 0x45f390: no menu mode sent yet (0x5d5cd0 == -1). */
int32_t GAMEAPI net_menu_mode_unset(void)
{
    return G32(0x5d5cd0) == 0xffffffff;
}

/* 0x45f3a0: the menu mode 0x5d5ccc: the host puts it in its record
 * (+0x20); a client sends message 18 (0xc, +8 mode); then 0x5d5cd0 = the
 * mode and 1 (0 when the send fails). */
int32_t GAMEAPI net_menu_mode_send(void)
{
    uint8_t msg[0xc];
    int32_t r;

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2))
        return 0;
    if (G8(0x5d622c) & 0x20) {
        U32(REC(g_local), 0x20) = G32(0x5d5ccc);
    } else {
        memset(msg, 0, sizeof msg);
        msg[0] = 0x12;
        U32(msg, 8) = G32(0x5d5ccc);
        r = net_send_at(1, 0, msg, 0xc, 6);
        if (r == 0)
            return 0;
    }
    G32(0x5d5cd0) = G32(0x5d5ccc);
    return 1;
}

/* 0x45f530: more than one active player and all in state 5. */
int32_t GAMEAPI net_all_finished(void)
{
    int32_t n = 0, done = 0;
    uint32_t r;

    for (r = REC(0); r < REC(6); r += 0x66c)
        if (U32(r, 0)) {
            n++;
            if (U32(r, 0x20) == 5)
                done++;
        }
    return n > 1 && n == done;
}

/* 0x45f5b0: up to 4 chat lines (0x41 bytes from 0x5d6128): *count capped
 * at 4 (always written back), lines[k] = line k. */
void GAMEAPI net_chat_lines(int32_t *count, uint32_t *lines)
{
    int32_t n = *count;
    uint32_t p = 0x5d6128;

    if (n > 4)
        n = 4;
    *count = n;
    for (; n > 0; n--, p += 0x41)
        *lines++ = p;
}

/* 0x45f650: the players' list for the name texts: in a network game
 * ids[k] = record index and names[k] = its name (+8), -1 / 0 for free
 * records, at least one entry (sic), the rest of *count filled with
 * -1 / 0, *count = the records listed; otherwise names[k] = 0, ids[k] =
 * race +0x1c + k for k < *count, and *count = 0. */
void GAMEAPI net_player_list(int32_t *count, uint32_t *ids, uint32_t *names)
{
    int32_t n = 0, k;
    uint32_t r;

    if (net_game_at() == 0) {
        for (k = 0; k < *count; k++) {
            names[k] = 0;
            ids[k] = U8(g_race, 0x1c + k);
        }
        *count = 0;
        return;
    }
    for (k = 0, r = REC(0) + 8; r < REC(6) + 8; r += 0x66c, k++) {
        if (U32(r, -8)) {
            *names = r;
            *ids = (uint32_t)k;
        } else {
            *names = 0;
            *ids = 0xffffffff;
        }
        names++;
        ids++;
        n++;
        if (n >= *count)
            break;
    }
    for (k = n; k < *count; k++) {
        *names++ = 0;
        *ids++ = 0xffffffff;
    }
    *count = n;
}

/* 0x45f7b0: the local player's name and car: the host puts them in its
 * record (+0x1c car, +8 name) and the options' name table (+0xd8c +
 * player * 11), then sends the player table (0x45dde0) if asked, else 1;
 * a client sends message 16 (0x20: +8 car, +0xc flag, +0x10 name, cut at
 * 16 characters in the options, sic: a 16-character name's terminator
 * lands on the original's return address, see the notes). */
int32_t GAMEAPI net_name_send(uint32_t flag)
{
    uint8_t msg[0x21];
    uint32_t opt = g_options;

    if (G8(0x5d622c) & 0x20) {
        U32(REC(g_local), 0x1c) = U8(opt, 0x18);
        pm_strcpy((char *)PTR(REC(g_local) + 8), (const char *)PTR(opt + 0x120));
        pm_strcpy((char *)PTR(opt + g_local * 11 + 0xd8c), (const char *)PTR(opt + 0x120));
        if (flag)
            return players_send_at();
        return 1;
    }
    memset(msg, 0, sizeof msg);
    msg[0] = 0x10;
    U32(msg, 0xc) = flag;
    U32(msg, 8) = U8(opt, 0x18);
    U32(REC(g_local), 0x1c) = U8(opt, 0x18);
    if (pm_strlen((const char *)PTR(opt + 0x120)) >= 0x10) {
        U8(opt, 0x130) = 0;
        opt = g_options;
    }
    pm_strcpy((char *)msg + 0x10, (const char *)PTR(opt + 0x120));
    return net_send_at(1, 0, msg, 0x20, 1);
}

/* 0x45f970: results may be sent: not the host, or state bit 14. */
int32_t GAMEAPI net_results_allowed(void)
{
    if (net_is_host_at() == 0)
        return 1;
    return (g_net >> 14) & 1;
}

/* 0x45f990: the results: the host sets state bit 10, 0x5d5cb8 = step + 5,
 * bit 13 = value bit 0 and sends message 25 (0x10: +8 0x5d5cb8, +0xc
 * value); a client sends message 24 (0xc: +8 value). */
int32_t GAMEAPI net_results_send(uint32_t value)
{
    uint8_t msg[0x10];
    int32_t r;

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2))
        return 0;
    r = net_results_allowed();
    if (r == 0)
        return 0;
    memset(msg, 0, sizeof msg);
    if (G8(0x5d622c) & 0x20) {
        g_net = g_net | 0x400;
        G32(0x5d5cb8) = g_step + 5;
        g_net = (g_net & ~0x2000u) | (value & 1) << 13;
        msg[0] = 0x19;
        U32(msg, 0xc) = value;
        U32(msg, 8) = G32(0x5d5cb8);
        return net_send_at(2, 0, msg, 0x10, 1);
    }
    msg[0] = 0x18;
    U32(msg, 8) = value;
    return net_send_at(1, 0, msg, 0xc, 1);
}

/* 0x45fa60: a pause asked by a player (out of range: 0), unless one is
 * pending (bit 11), 0x4d1198 is set or within 15 steps of the results
 * time 0x5d5cb8 (then 1): clears bit 14, a debug line; the host sets bit
 * 11, 0x5d5cbc = step + 5, 0x5d5cc0 = player, clears every record's
 * +0x668 and sends message 27 (0x10); a client sends byte message 26. */
int32_t GAMEAPI net_pause_request(uint32_t player)
{
    uint8_t msg[0x10];
    uint32_t r;

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2))
        return 0;
    if ((g_net & 0x800) == 0x800 || G32(0x4d1198) != 0 || g_step < G32(0x5d5cb8) + 0xf)
        return 1;
    if (player >= U8(g_race, 0x54d))
        player = 0;
    g_net = g_net & ~0x4000u;
    debug_printf1_at(0x4b0e9c, player);
    if (!(G8(0x5d622c) & 0x20))
        return send_byte_at(1, 0x1a, 1);
    g_net = g_net | 0x800;
    G32(0x5d5cbc) = g_step + 5;
    G32(0x5d5cc0) = player;
    memset(msg, 0, sizeof msg);
    msg[0] = 0x1b;
    U32(msg, 8) = G32(0x5d5cbc);
    U32(msg, 0xc) = G32(0x5d5cc0);
    for (r = REC(0) + 0x668; r < REC(6) + 0x668; r += 0x66c)
        U32(r, 0) = 0;
    return net_send_at(2, 0, msg, 0x10, 1);
}

/* 0x45fb90: a player caught cheating: dropped (0x48bd40, 0x48bf70) and
 * sent message 20 (8 bytes). */
int32_t GAMEAPI net_player_cheated(uint32_t from)
{
    uint8_t msg[8];

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2))
        return 0;
    memset(msg, 0, sizeof msg);
    msg[0] = 0x14;
    drop_player_at(player_of_at(from));
    return net_send_at(0, from, msg, 8, 0);
}

/* 0x45fc40: the end of the race: the host sends the results to all
 * (0x45fca0(-1)); a client sends message 31 (8 bytes). */
int32_t GAMEAPI net_finish_send(void)
{
    uint8_t msg[8];

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2))
        return 0;
    if (G8(0x5d622c) & 0x20)
        return net_race_results_send(-1);
    memset(msg, 0, sizeof msg);
    msg[0] = 0x1f;
    return net_send_at(1, 0, msg, 8, 2);
}

/* 0x45fca0: message 5, the race results (host only): +8 n = race +0x586,
 * +0xc race +0x10..+0x15, +0x12 race +8..+0xd, +0x18 + p * 0xdc the first
 * n + 3 dwords of race +0x24 + p * 0xdc (6 players); 0x540 bytes to all
 * (to == -1) or (n * 3 + 12) * 8 bytes to one. */
int32_t GAMEAPI net_race_results_send(int32_t to)
{
    /* 0x540 in the original (its stack above that for n > 52, sic). */
    uint8_t msg[0x1900];
    uint32_t race, n = 0, k, c;

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2) || !(G8(0x5d622c) & 0x20))
        return 0;
    race = g_race;
    memset(msg, 0, sizeof msg);
    msg[0] = 5;
    U32(msg, 8) = U8(race, 0x586);
    for (k = 0; k < 6; k++) {
        msg[0xc + k] = U8(race, 0x10 + k);
        msg[0x12 + k] = U8(race, 8 + k);
        n = U32(msg, 8);
        for (c = 0; c < n + 3; c++)
            U32(msg, 0x18 + k * 0xdc + c * 4) = U32(race, 0x24 + k * 0xdc + c * 4);
    }
    if (to == -1)
        return net_send_at(2, 0, msg, 0x540, 2);
    return net_send_at(0, (uint32_t)to, msg, (n * 3 + 0xc) * 8, 2);
}

/* 0x45fde0: the results time reached (bit 10; the host at once, a client
 * at step 0x5d5cb8): bits 10 and 13 cleared, 0x5d5cb8 = step, the host
 * starts the clock (0x43693f); 3 when bit 13 was set, else 1; 0 when not
 * yet. */
int32_t GAMEAPI net_results_check(void)
{
    int32_t r = 1;
    uint32_t step;
    int32_t host;

    if ((g_net & 0x400) != 0x400)
        return 0;
    host = net_is_host_at();
    step = g_step;
    if (host != 1 && step < G32(0x5d5cb8))
        return 0;
    if ((g_net & 0x2000) == 0x2000)
        r = 3;
    g_net = g_net & ~0x400u;
    g_net = g_net & ~0x2000u;
    G32(0x5d5cb8) = step;
    if (net_is_host_at())
        sim_clock_start_at();
    return r;
}

/* 0x45fe70: the pause time reached (bit 11; the host at once, a client at
 * step 0x5d5cbc): bit 11 cleared, *player = 0x5d5cc0, 0x5d5cbc = step;
 * the host marks its own record (+0x668 = 1) and 1, a client sends byte
 * message 32; 0 when not yet. */
int32_t GAMEAPI net_pause_due(uint32_t *player)
{
    if ((g_net & 0x800) != 0x800)
        return 0;
    if (net_is_host_at() != 1 && g_step < G32(0x5d5cbc))
        return 0;
    g_net = g_net & ~0x800u;
    *player = G32(0x5d5cc0);
    G32(0x5d5cbc) = g_step;
    if (G8(0x5d622c) & 0x20) {
        U32(REC(net_local_slot_at()), 0x668) = 1;
        return 1;
    }
    return send_byte_at(1, 0x20, 1);
}

/* 0x45ff00: message template idx (options +0x104e + idx * 64, idx < 30):
 * '1' and '2' become the names s1 / s2 (each at most once, else *out
 * stays ""), cut to size - 1 characters, into out. */
void GAMEAPI net_message_format(int32_t idx, uint32_t s1, uint32_t s2, char *out, uint32_t size)
{
    char text[0x100], fmt[0x100];
    const char *tpl;
    char *p = fmt;
    int32_t one = 0, two = 0, two_first = 0;
    int32_t len, i;

    out[0] = 0;
    if (idx >= 0x1e)
        return;
    tpl = (const char *)PTR(g_options + (uint32_t)idx * 64 + 0x104e);
    len = (int32_t)pm_strlen(tpl);
    for (i = 0; i < len; i++, p++) {
        char ch = tpl[i];

        if (ch == '1') {
            if (one)
                return;
            *p++ = '%';
            one = 1;
            *p = 's';
        } else if (ch == '2') {
            if (two)
                return;
            two = 1;
            if (!one)
                two_first = 1;
            *p++ = '%';
            *p = 's';
        } else {
            *p = ch;
        }
    }
    *p = 0;
    if (one) {
        if (two) {
            if (two_first)
                sprintf4_at(text, fmt, s2, s1);
            else
                sprintf4_at(text, fmt, s1, s2);
        } else {
            sprintf3_at(text, fmt, s1);
        }
    } else if (two) {
        sprintf3_at(text, fmt, s2);
    } else {
        sprintf2_at(text, fmt);
    }
    if (pm_strlen(text) >= size - 1)
        text[size - 1] = 0;
    pm_strcpy(out, text);
}

/* The joke lines (race +0x721 bit 7) by kind, the second when the coin is
 * 0 (0 = none). Kind 2's second line is "signals " + a player's name +
 * "'s death knell". */
static const uint32_t pm_joke_lines[13][2] = {
    {0x4b1198, 0x4b1170}, {0x4b1158, 0x4b1140}, {0x4b1120, 0x4b1114}, {0x4b10e8, 0x4b10c8}, {0x4b10b0, 0x4b1094},
    {0x4b1074, 0x4b1050}, {0x4b1028, 0x4b1000}, {0x4b0fe4, 0x4b0fcc}, {0x4b0fb0, 0x4b0fb0}, {0x4b0f94, 0x4b0f6c},
    {0x4b0f50, 0x4b0f2c}, {0x4b0f14, 0x4b0ef0}, {0x4b0ed0, 0x4b0eb4},
};

/* The templates (0x45ff00) by kind: index (+ the coin when < 0x1c) and
 * whether the names swap. */
static const uint8_t pm_template_index[16] = {6, 4, 8, 0xc, 0xe, 0x10, 0x12, 0xa, 0, 0x14, 0x16, 0x18, 0x1a, 2, 0x1c, 0x1d};

/* 0x4600a0: a kill / weapon line for the chat (0x48d270): the host, or a
 * client for kinds with bit 7. The coin is bit 16 of the LCG 0x4b11d8
 * (* 16807 & 0x7fffffff). Names: the players' (+8; when empty the driver
 * name 0x4484a0 of race +0x1c + i), "???" past the player count. With
 * race +0x721 bit 7 the joke lines (kinds 0..12), else a template (kinds
 * 0..15, 0x45ff00). */
void GAMEAPI net_kill_message(uint32_t victim, uint32_t attacker, uint32_t kind)
{
    char text[0x80], name1[0x20], name2[0x20];
    uint32_t race, seed, coin, n1, n2;

    if (!(G8(0x5d622c) & 1) || !(G8(0x5d622c) & 2))
        return;
    if (!(G8(0x5d622c) & 0x20) && !(kind & 0x80))
        return;
    race = g_race;
    kind &= 0x7f;
    seed = (G32(0x4b11d8) * 16807u) & 0x7fffffff;
    G32(0x4b11d8) = seed;
    coin = (seed >> 16) & 1;
    if (victim < U8(race, 0x54d)) {
        n1 = REC(victim) + 8;
        if (U8(n1, 0) == 0) {
            driver_name_at(name1, U8(race, 0x1c + victim));
            race = g_race;
            n1 = A(name1);
        }
    } else {
        n1 = 0x4b11bc;
    }
    if (attacker < U8(race, 0x54d)) {
        n2 = REC(attacker) + 8;
        if (U8(n2, 0) == 0) {
            driver_name_at(name2, U8(race, 0x1c + attacker));
            n2 = A(name2);
        }
    } else {
        n2 = 0x4b11bc;
    }
    pm_strcpy(text, (const char *)PTR(n1));
    if (U8(g_race, 0x721) & 0x80) {
        if (kind > 0xc)
            return;
        if (kind == 8 || coin) {
            pm_strcat(text, (const char *)PTR(pm_joke_lines[kind][0]));
        } else {
            pm_strcat(text, (const char *)PTR(pm_joke_lines[kind][1]));
            if (kind == 2) {
                pm_strcat(text, (const char *)PTR(REC(slot_flag_find_at()) + 8));
                pm_strcat(text, (const char *)PTR(0x4b1104));
            }
        }
    } else {
        uint32_t idx;

        if (kind > 0xf)
            return;
        idx = pm_template_index[kind] + (kind < 0xe ? coin : 0);
        if (kind >= 9 && kind <= 12)
            net_message_format((int32_t)idx, n2, n1, text, 0x80);
        else
            net_message_format((int32_t)idx, n1, n2, text, 0x80);
    }
    if (pm_strlen(text) != 0)
        chat_send_at(text);
}

/* 0x4609b0: the active player records moved to the front (0x66c bytes
 * each, dwords forwards), the rest cleared (0x476f50). */
void GAMEAPI net_players_compact(void)
{
    uint32_t dst = REC(0), src, k;
    int32_t n = 0;

    for (src = REC(0); src < REC_END; src += 0x66c) {
        if (U32(src, 0) == 0)
            continue;
        for (k = 0; k < 0x66c; k += 4)
            U32(dst, k) = U32(src, k);
        n++;
        dst += 0x66c;
    }
    if (n < 6)
        for (src = REC(n); src < REC_END; src += 0x66c)
            mem_set_at(src, 0, 0x66c);
}

/* 0x460a20: the host, once every active player has paused (+0x668):
 * the first time (bit 14 clear) a debug line and byte message 33; sets
 * bit 14. */
void GAMEAPI net_all_paused(void)
{
    uint32_t r;

    if (net_is_host_at() == 0 || !(G8(0x5d622c) & 0x20))
        return;
    for (r = REC(0) + 0x668; r < REC(6) + 0x668; r += 0x66c)
        if (U32(r, -0x668) != 0 && U32(r, 0) == 0)
            return;
    if (!(g_net & 0x4000)) {
        debug_printf_at(0x4b11c0);
        send_byte_at(2, 0x21, 1);
    }
    g_net = g_net | 0x4000;
}

/* 0x460a90: byte message 34 (snatched). */
int32_t GAMEAPI net_snatched_send(void)
{
    return send_byte_at(2, 0x22, 0);
}

/* ---- the front end's sparks ---- */

/* 0x462ae0: n (low byte) sparks at (x, y) + rand & 7 into the ring of 255
 * 0x4c-byte particles at *0x4d108c (next 0x4d1094): life 0x14, colour
 * (0xff, ((next & 0xf) + 0x30) * 4, 0xc0), velocity ((rand & 0x7f) - 0x40)
 * << 6 and << 5. */
void GAMEAPI sparks_spawn(uint32_t n, uint32_t x, uint32_t y)
{
    uint32_t p = G32(0x4d108c) + G32(0x4d1094) * 0x4c;
    uint32_t r;

    n &= 0xff;
    for (; n; n--) {
        U8(p, 0) = 0x14;
        U16(p, 0x48) = (uint16_t)((rand_at() & 7) + x);
        U16(p, 0x4a) = (uint16_t)((rand_at() & 7) + y);
        U16(p, 0x40) = (uint16_t)(((rand_at() & 0x7f) - 0x40) << 6);
        r = rand_at() & 0x7f;
        U8(p, 3) = 0xc0;
        U16(p, 0x42) = (uint16_t)((r - 0x40) << 5);
        U8(p, 1) = 0xff;
        U8(p, 2) = (uint8_t)(((G8(0x4d1094) & 0xf) + 0x30) << 2);
        p += 0x4c;
        G32(0x4d1094) = G32(0x4d1094) + 1;
        if (G32(0x4d1094) >= 0xff) {
            p = G32(0x4d108c);
            G32(0x4d1094) = 0;
        }
    }
}

/* 0x462bb0: the sparks drawn as lines at depth (the ordering table entry
 * 0x6cdd80 + depth * 4, z = depth * 0x4a33d0): the clip rectangle
 * 0x6ccd64..0x6ccd70 widened by 0x6cd418 meanwhile. Each live spark ages
 * (to 1), moves (position += velocity >> 8, y velocity += 0x80), and its
 * line from the position to position + velocity >> 8 (screen offset
 * 0x4be334 / 0x4be338) is clipped (0x426b80 per end: off screen kills it)
 * and linked; at life 1 it fades (blue - 6 down to 0xb, then dies; green
 * and red - 8, at least 8, as bytes). The chain goes in with 0x429080. */
void GAMEAPI sparks_draw(uint32_t depth)
{
    uint32_t d = G32(0x6cd418);
    uint32_t ot = 0x6cdd80 + depth * 4;
    uint32_t first = 0, last = 0, r, z;
    int k;

    G32(0x6ccd64) = x87_add(G32(0x6ccd64), d);
    G32(0x6ccd68) = x87_sub(G32(0x6ccd68), d);
    G32(0x6ccd6c) = x87_add(G32(0x6ccd6c), d);
    G32(0x6ccd70) = x87_sub(G32(0x6ccd70), d);
    for (k = 0, r = G32(0x4d108c); k < 0xff; k++, r += 0x4c) {
        uint8_t life = U8(r, 0), c1, c2, c3;
        uint16_t vy;

        if (life == 0)
            continue;
        if (first == 0)
            first = r + 4;
        if (life > 1)
            U8(r, 0) = (uint8_t)(life - 1);
        S16(r, 0x48) = (int16_t)(S16(r, 0x48) + (S16(r, 0x40) >> 8));
        vy = U16(r, 0x42);
        S16(r, 0x4a) = (int16_t)(S16(r, 0x4a) + ((int16_t)vy >> 8));
        U16(r, 0x42) = (uint16_t)(vy + 0x80);
        U32(r, 0x14) = x87_int_to_float((int32_t)S16(r, 0x48) + GI32(0x4be334));
        U32(r, 0x18) = x87_int_to_float((int32_t)S16(r, 0x4a) + GI32(0x4be338));
        U32(r, 0x30) = x87_int_to_float(GI32(0x4be334) + (S16(r, 0x40) >> 8) + S16(r, 0x48));
        U32(r, 0x34) = x87_int_to_float(GI32(0x4be338) + (S16(r, 0x42) >> 8) + S16(r, 0x4a));
        z = x87_depth(depth);
        U32(r, 0x2c) = z;
        U32(r, 0x10) = z;
        vertex_clip_at(r + 0xc);
        vertex_clip_at(r + 0x28);
        if (U32(r, 0xc) | U32(r, 0x28)) {
            U8(r, 0) = 0;
            continue;
        }
        c1 = U8(r, 1);
        U8(r, 0x24) = c1;
        U8(r, 8) = (uint8_t)(c1 - 7);
        life = U8(r, 0);
        c2 = U8(r, 2);
        U8(r, 0x25) = c2;
        U8(r, 9) = (uint8_t)(c2 - 7);
        c3 = U8(r, 3);
        U8(r, 0x26) = c3;
        U8(r, 0xa) = (uint8_t)(c3 - 7);
        if (life == 1) {
            if (c3 > 0xb)
                U8(r, 3) = (uint8_t)(c3 - 6);
            else
                U8(r, 0) = 0;
            U8(r, 2) = (uint8_t)(U8(r, 2) - 8);
            if (U8(r, 2) < 8)
                U8(r, 2) = 8;
            U8(r, 1) = (uint8_t)(U8(r, 1) - 8);
            if (U8(r, 1) < 8)
                U8(r, 1) = 8;
        }
        if (last)
            dword_store_at(last, r + 4);
        last = r + 4;
    }
    if (first && last)
        word_exchange_at(ot, first, last);
    G32(0x6ccd64) = x87_sub(G32(0x6ccd64), d);
    G32(0x6ccd68) = x87_add(G32(0x6ccd68), d);
    G32(0x6ccd6c) = x87_sub(G32(0x6ccd6c), d);
    G32(0x6ccd70) = x87_add(G32(0x6ccd70), d);
}
