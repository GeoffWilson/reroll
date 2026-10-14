/* Group XA: code Ghidra never made into functions, found as code addresses
 * the original uses as values (callbacks pushed or stored, a data table):
 *
 *  - two track objects' hit callbacks (+0x148, 0x401120 / 0x401280) and the
 *    driller's (0x424990);
 *  - the movie player's per-frame callback (0x42d8e0: subtitle texts, a
 *    key press stops the movie);
 *  - the race clock's timer tick (0x435140) and the two screen OT
 *    draw-and-swap steps it tail-jumps to (0x423af0 / 0x423ba0, reached
 *    from nowhere else, so ported here too);
 *  - the movie menu's table entries 0x447f90.. (winner's movie by language);
 *  - front end menu handlers: the remembered item text (0x448cf0), menu
 *    input handlers (0x448d60, 0x449120), the moved handler (0x44da90) and
 *    the options items' callbacks 0x44ce70..0x44d950 (value setters,
 *    text / step callbacks for commands 0x12..0x16, on/off toggles through
 *    0x447f20).
 *
 * Everything outside the group is called by address; tail jumps are calls
 * returning the callee's result. */
#include "../runtime.h"
#include "physics.h"
#include "winapi.h"
#include "gamefn.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define PTR(p, off) GP((uint8_t *)(uintptr_t)(p) + (off))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define GF(a) (*(float *)(uintptr_t)(a))
#define GPTR(a) GP(a)

#define g_race GPTR(0x4b1960)
#define g_config GPTR(0x4b2198)

#define PA(p) ((uint32_t)(uintptr_t)(p))

#define body_retire(o) GCALL(0x419300, body_retire)(o)
#define grid_unlink(o) GCALL(0x4611d0, object_grid_unlink)(o)
#define crumble_start(o) GCALL(0x4427a0, body_triggers_apply)(o)
#define pair_list_remove(o) GCALL(0x478430, pair_list_remove)(PA(o))
#define object_debris(o) GCALL(0x403950, pair_users_release)(PA(o))
#define explosion_effects(pos, dir, a, b, c) GCALL(0x42aa30, explosion_effects)((const int32_t *)(pos), dir, a, b, c)
#define viewport_apply(v) GCALL(0x4265d0, viewport_apply)((int16_t *)GMEM(v))
#define draw_otag_rect(a, b) GCALL(0x428480, capture_draw_otag_rect)(a, (const int16_t *)GMEM(b))
#define draw_otag(a) GCALL(0x428470, capture_draw_otag)(a)
#define ot_clear_reverse(ot, n) GCALL(0x427a70, ot_clear_reverse)((uint32_t *)GMEM(ot), n)
#define toggle_step(v, one, action) GCALL(0x447f20, option_onoff_step)(v, one, action)
#define menu_param_callback_hooked(cmd, dst, v, flag) GCALL(0x44d000, menu_param_callback)(cmd, dst, v, flag)
#define sprintf_hooked GCALL(0x4979f0, msvc_sprintf)

/* strcpy as the original's inline repne scasb / rep movs. */
static void xa_strcpy(uint8_t *dst, const uint8_t *src)
{
    uint32_t i = 0;
    do
        dst[i] = src[i];
    while (src[i++] != 0);
}

/* ---- hit callbacks (+0x148) ---- */

/* The blast both track objects make: explosion_effects at the object's
 * position raised by 0x8c0000, direction (0, -0x400000, 0). */
static void xa_object_blast(uint8_t *obj, uint32_t kind, uint32_t sound)
{
    int32_t pos[3], dir[3];

    pos[0] = I32(obj, 0x34);
    pos[1] = I32(obj, 0x38) + 0x8c0000;
    pos[2] = I32(obj, 0x3c);
    dir[0] = 0;
    dir[1] = (int32_t)0xffc00000;
    dir[2] = 0;
    explosion_effects(pos, dir, kind, sound, 0xb);
}

/* 0x401120: a track object's hit callback: with flag 0x2000 it comes loose
 * (flags 0x2200 off, 0x1000 on, off the pair list) and gets a random spin
 * from the LCG 0x4b11dc (x * 16807 & 0x7fffffff) in +0x80/+0x84/+0x88;
 * otherwise it blows up (explosion 9 / 0x13aaf, off the grid, retired,
 * collapse, debris 0x403950). Returns 0. */
int32_t GAMEAPI track_object_hit_blast(uint8_t *obj)
{
    uint32_t flags = U32(obj, 0);

    if (flags & 0x2000) {
        uint32_t r, n;

        U32(obj, 0) = (flags & ~0x2200u) | 0x1000;
        pair_list_remove(obj);
        r = (G32(0x4b11dc) * 16807u) & 0x7fffffff;
        G32(0x4b11dc) = r;
        n = ((r >> 15) * 0x7fffu) >> 16;
        U32(obj, 0x80) = ((n & 0xf) + 0x3ff8) << 18;
        U32(obj, 0x84) = (((n >> 4) & 0xf) - 8) << 18;
        U32(obj, 0x88) = (((n >> 8) & 0xf) - 8) << 18;
        return 0;
    }
    xa_object_blast(obj, 9, 0x13aaf);
    grid_unlink(obj);
    body_retire(obj);
    crumble_start(obj);
    object_debris(obj);
    return 0;
}

/* 0x401280: the kind-0x23 track object's hit callback: explosion 7 /
 * 0x13a2f, collapse, off the grid, retired, collapse again (sic), debris.
 * Returns 0. */
int32_t GAMEAPI track_object_hit_k23(uint8_t *obj)
{
    xa_object_blast(obj, 7, 0x13a2f);
    crumble_start(obj);
    grid_unlink(obj);
    body_retire(obj);
    crumble_start(obj);
    object_debris(obj);
    return 0;
}

/* 0x424990: the driller head's hit callback. Hit by a body (not the last
 * two it hit, +0x1b0 / +0x1b4): remembered, car_blast_hit 0x4599c0(other,
 * hit), the owner (+0x180) flagged 0x20000; in race modes 3 / 8 (race
 * +0x551) a car hit scores for the owner (race +8 + owner's +0x64e) after
 * the three checks 0x45ef80 / 0x45eed0 / 0x45ef30 (then 0x45fbe0), or
 * without them when 0x45eed0 says 0; a car hit (not state +0x70c == 8) sends
 * the kill message 0x4600a0(car id, owner id, 9). Hit by the world (not a
 * hit with +0x44 == -4): explosion 1 / 0xd1a49 at the head, +0x198 = 0,
 * +0x194 = 0x10, off the grid, retired, its voice (+0x1ac) stopped, and the
 * three trail parts (+0x184..) ended (0x458a20(part, 1)) and taken off the
 * grid. Returns 0. */
int32_t GAMEAPI driller_hit(uint8_t *self, uint8_t *other, uint8_t *hit)
{
    if (other != 0) {
        uint8_t *owner;
        uint8_t race_mode;

        if (other == PTR(self, 0x1b0) || other == PTR(self, 0x1b4))
            return 0;
        U32(self, 0x1b4) = U32(self, 0x1b0);
        U32(self, 0x1b0) = PA(other);
        GCALL(0x4599c0, car_blast_hit)(other, hit);
        U32(PTR(self, 0x180), 0) |= 0x20000;
        race_mode = U8(g_race, 0x551);
        if ((race_mode == 3 || race_mode == 8) && (U32(other, 0) & 0x1ff) == 3) {
            owner = PTR(self, 0x180);
            if (GCALL(0x45ef80, net_state_game)() == 1 && GCALL(0x45eed0, net_state_host)() == 1 &&
                GCALL(0x45ef30, net_state_bit5)() == 1) {
                U8(g_race, U16(owner, 0x64e) + 8) += 1;
                GCALL(0x45fbe0, net_send_type7)(g_race + 8);
            } else if (GCALL(0x45eed0, net_state_host)() == 0) {
                U8(g_race, U16(owner, 0x64e) + 8) += 1;
            }
        }
        if ((U32(other, 0) & 0x1ff) == 3) {
            owner = PTR(self, 0x180);
            if (owner != 0 && U8(other, 0x70c) != 8)
                GCALL(0x4600a0, net_kill_message)(U16(other, 0x64e), U16(owner, 0x64e), 9);
        }
        return 0;
    }
    if (hit != 0 && I32(hit, 0x44) == -4)
        return 0;
    {
        int32_t dir[3] = {0, (int32_t)0xffc00000, 0};
        int32_t i;

        explosion_effects(self + 0x34, dir, 1, 0xd1a49, 0xb);
        U32(self, 0x198) = 0;
        U32(self, 0x194) = 0x10;
        grid_unlink(self);
        body_retire(self);
        if (I32(self, 0x1ac) != 0x7f) {
            /* the original pushes ecx with only cl set (a leftover upper part);
             * sound_voice_stop uses the low byte */
            GCALL(0x471f40, sound_voice_stop)(U32(self, 0x1ac) & 0xff);
            I32(self, 0x1ac) = 0x7f;
        }
        for (i = 0; i < 3; i++) {
            uint8_t *part = PTR(self, 0x184 + 4 * i);
            GCALL(0x458a20, trail_update)(part, 1);
            grid_unlink(part);
        }
    }
    return 0;
}

/* ---- movie player ---- */

/* 0x42d8e0: the movie player's per-frame callback. Counts the frame
 * (0x4be2b4); of the queued texts (16-byte records at 0x6cc400: x, y, frame,
 * text; count 0x4be2b8) picks a due frame as the original's loop does, and if
 * it is later than the shown one (0x4ac738) clears the text area (0x488cc0,
 * height 0xc0, or 0x180 for the tall screen 0x4be2bc) and draws every text
 * of that frame (0x488ad0(text, x, y + height)). Then a pending window
 * message: if it is a WM_KEYDOWN of backspace, tab, enter, escape or space
 * returns 1 (stop the movie), else 0. */
int32_t GAMEAPI movie_frame_check(void)
{
    uint32_t msg[7];
    int32_t frame = GI32(0x4be2b4) + 1;
    int32_t count = GI32(0x4be2b8);
    int32_t shown = GI32(0x4ac738);
    int32_t pick = shown;
    int32_t i;

    GI32(0x4be2b4) = frame;
    if (count > 0) {
        const int32_t *rec = (const int32_t *)0x6cc408;
        for (i = count; i != 0; i--, rec += 4) {
            int32_t at = rec[0];
            if (frame < at)
                continue;
            if (pick == -1 || pick < frame || at < pick)
                pick = at;
        }
    }
    if (pick != -1 && pick > shown) {
        uint32_t height = G32(0x4be2bc) ? 0x180 : 0xc0;

        GCALL(0x488cc0, movie_text_clear)((int32_t)height);
        for (i = 0; i < GI32(0x4be2b8); i++) {
            const uint32_t *rec = (const uint32_t *)(uintptr_t)(0x6cc400 + i * 16);
            if ((uint32_t)pick == rec[2])
                GCALL(0x488ad0, movie_text_draw)((const uint8_t *)GMEM(rec[3]), (int32_t)rec[0], (int32_t)(rec[1] + height));
        }
        GI32(0x4ac738) = pick;
    }
    if (PeekMessageA(msg, 0, 0, 0, 0) == 0)
        return 0;
    if (GetMessageA(msg, 0, 0, 0) == 0)
        return 0;
    if (msg[1] != 0x100)
        return 0;
    switch (msg[2]) {
    case 8:
    case 9:
    case 13:
    case 27:
    case 32:
        return 1;
    default:
        return 0;
    }
}

/* ---- race clock and the screen OTs ---- */

/* 0x423af0: the first OT draw: view (0x4bde74 & 0xffff) of the 0x870-byte
 * view records 0x6cd4a0 (viewport +0x5c), its OT drawn, the second buffer's
 * OT 0x6ce5f4 drawn, buffers set to 0x6cdd10 / 0x6ce5f8 and their OTs
 * cleared; with race +0x54c set, 0x6ce604 = 1 and 0x6ce600 = 0. */
void GAMEAPI screen_ot_first(void)
{
    viewport_apply(0x6cd4a0 + (G32(0x4bde74) & 0xffff) * 0x870 + 0x5c);
    draw_otag_rect(0x6cdd0c, (G32(0x4bde74) & 0xffff) * 0x870 + 0x6cd4a0);
    draw_otag(0x6ce5f4);
    G32(0x6ce60c) = 0x6cdd10;
    G32(0x6ce608) = 0x6ce5f8;
    ot_clear_reverse(0x6cdd80, 0x200);
    ot_clear_reverse(G32(0x6ce608), 2);
    if (U8(g_race, 0x54c) != 0) {
        G32(0x6ce604) = 1;
        G32(0x6ce600) = 0;
    }
}

/* 0x423ba0: draw the current buffer (0x6ce60c: viewport +0x5c, OT +0x86c,
 * the small OT 0x6ce608 + 4), swap to the other one (0x6cd4a0 / 0x6cdd10,
 * small OTs 0x6ce5f8 / 0x6ce5f0), clear its OTs; 0x6ce600 = 0, 0x6ce604 = 1. */
void GAMEAPI screen_ot_swap(void)
{
    uint32_t next;

    viewport_apply(G32(0x6ce60c) + 0x5c);
    draw_otag_rect(G32(0x6ce60c) + 0x86c, G32(0x6ce60c));
    draw_otag(G32(0x6ce608) + 4);
    if (G32(0x6ce60c) == 0x6cd4a0) {
        next = 0x6cdd10;
        G32(0x6ce608) = 0x6ce5f0;
        G32(0x6ce60c) = next;
    } else {
        next = 0x6cd4a0;
        G32(0x6ce60c) = next;
        G32(0x6ce608) = 0x6ce5f8;
    }
    ot_clear_reverse(next + 0x70, 0x200);
    ot_clear_reverse(G32(0x6ce608), 2);
    G32(0x6ce600) = 0;
    G32(0x6ce604) = 1;
}

/* 0x435140: the race clock's timer tick: counts 0x4be388 unless paused
 * (0x4d1198); with race +0x54c set, a frame pending (0x6ce600) and the tick
 * at or past 0x6c0a38, draws: 0x423ba0 (race +0x552 == 0) or 0x423af0. */
void GAMEAPI clock_tick(void)
{
    if (G32(0x4d1198) == 0)
        G32(0x4be388) += 1;
    if (U8(g_race, 0x54c) == 0 || G32(0x6ce600) == 0 || G32(0x6c0a38) > G32(0x4be388))
        return;
    if (U8(g_race, 0x552) == 0)
        GCALL(0x423ba0, screen_ot_swap)();
    else
        GCALL(0x423af0, screen_ot_first)();
}

/* ---- the movie menu's table (0x4af680: function, name) ---- */

/* 0x447f90..0x447fe0: the winner's movie in language 0..5 (0x42dbf0). */
void GAMEAPI movie_win_lang0(void)
{
    GCALL(0x42dbf0, movie_win)(0);
}

void GAMEAPI movie_win_lang1(void)
{
    GCALL(0x42dbf0, movie_win)(1);
}

void GAMEAPI movie_win_lang2(void)
{
    GCALL(0x42dbf0, movie_win)(2);
}

void GAMEAPI movie_win_lang3(void)
{
    GCALL(0x42dbf0, movie_win)(3);
}

void GAMEAPI movie_win_lang4(void)
{
    GCALL(0x42dbf0, movie_win)(4);
}

void GAMEAPI movie_win_lang5(void)
{
    GCALL(0x42dbf0, movie_win)(5);
}

/* ---- front end menu handlers ---- */

/* 0x448cf0: an item's select handler: the current item's (cursor 0x4cb588,
 * if in range) text +0x1fc into 0x4cb5e8 and its value words +0x4f8 /
 * +0x4fc into 0x4af70c / 0x4af710. */
void GAMEAPI menu_item_text_keep(void)
{
    int8_t cursor = (int8_t)G8(0x4cb588);
    uint8_t *item;

    if (cursor < 0 || (int32_t)cursor >= GI32(0x4cb49c))
        return;
    item = GPTR(0x4cb4b8) + (int32_t)cursor * 0x730;
    xa_strcpy((uint8_t *)0x4cb5e8, item + 0x1fc);
    G32(0x4af70c) = U32(item, 0x4f8);
    G32(0x4af710) = U32(item, 0x4fc);
}

/* 0x448d60: a menu input handler: action 10 runs 0x448880. Returns 0. */
int32_t GAMEAPI menu_input_back10(int32_t action)
{
    if (action == 10)
        GCALL(0x448880, name_entry_box_place)();
    return 0;
}

/* 0x449120: a menu input handler passing the action and seven globals
 * (0x4c98e0, 0x4c2914, 0x4cb468, 0x4c2480, 0x4c2918, 0x6bf164, 0x6bf160) on
 * to 0x449160; returns its result. */
int32_t GAMEAPI menu_input_net(int32_t action)
{
    return GCALL(0x449160, name_entry_input)((uint32_t)action, (uint8_t *)GMEM(G32(0x4c98e0)), GI32(0x4c2914),
                                             GI32(0x4cb468), G32(0x4c2480), GI32(0x4c2918), G32(0x6bf164),
                                             G32(0x6bf160));
}

/* 0x44ce70 / 0x44ce80 / 0x44ce90 / 0x44cea0: float slider callbacks: the
 * value into config +0x64 / +0x68 / +0x6c / +0x70 (eax keeps it). */
uint32_t GAMEAPI menu_slider_set_64(uint32_t value)
{
    U32(g_config, 0x64) = value;
    return value;
}

uint32_t GAMEAPI menu_slider_set_68(uint32_t value)
{
    U32(g_config, 0x68) = value;
    return value;
}

uint32_t GAMEAPI menu_slider_set_6c(uint32_t value)
{
    U32(g_config, 0x6c) = value;
    return value;
}

uint32_t GAMEAPI menu_slider_set_70(uint32_t value)
{
    U32(g_config, 0x70) = value;
    return value;
}

/* 0x44ceb0: a names item (index config +0x86 into the names 0x4cb16c,
 * count 0x4cb574, out of range -> 0): the name is always copied to config
 * +0xe0; command 0x12 copies it to dst, 0x13 the 8 bytes at 0x4b008c,
 * 0x14 / 0x15..0x16 step the index down / up with wrap. Returns 1, or 0 for
 * other commands. */
int32_t GAMEAPI menu_names_cb(int32_t cmd, uint8_t *dst)
{
    int32_t index = U8(g_config, 0x86);

    if (index < 0 || index >= GI32(0x4cb574))
        index = 0;
    xa_strcpy(g_config + 0xe0, (const uint8_t *)(uintptr_t)G32(0x4cb16c + index * 4));
    switch (cmd) {
    case 0x12:
        xa_strcpy(dst, (const uint8_t *)(uintptr_t)G32(0x4cb16c + index * 4));
        return 1;
    case 0x13:
        U32(dst, 0) = G32(0x4b008c);
        U32(dst, 4) = G32(0x4b0090);
        return 1;
    case 0x14:
        index--;
        if (index < 0)
            index = GI32(0x4cb574) - 1;
        U8(g_config, 0x86) = (uint8_t)index;
        return 1;
    case 0x15:
    case 0x16:
        index++;
        if (index >= GI32(0x4cb574))
            index = 0;
        U8(g_config, 0x86) = (uint8_t)index;
        return 1;
    default:
        return 0;
    }
}

/* 0x44cfc0 / 0x44d130: a two-word value item on config +0x0b / +0x1f
 * through menu_param_callback 0x44d000 (flag 0 / 1): -1 -> 0, else the
 * result is stored and 1 returned. */
int32_t GAMEAPI menu_param_cb_0b(int32_t cmd, uint8_t *dst)
{
    int32_t r = menu_param_callback_hooked(cmd, dst, U8(g_config, 0xb), 0);
    if (r == -1)
        return 0;
    U8(g_config, 0xb) = (uint8_t)r;
    return 1;
}

int32_t GAMEAPI menu_param_cb_1f(int32_t cmd, uint8_t *dst)
{
    int32_t r = menu_param_callback_hooked(cmd, dst, U8(g_config, 0x1f), 1);
    if (r == -1)
        return 0;
    U8(g_config, 0x1f) = (uint8_t)r;
    return 1;
}

/* 0x44d170: the split screen mode item (config +0x0f, 0..4): command 0x12
 * shows "2H" / "2V" / "3" / "4" (0x4b00bc / b8 / b4 / b0) or the 8 bytes at
 * 0x4b008c, 0x13 the same 8 bytes, 0x14 steps down (above 4 -> 4), 0x15 /
 * 0x16 up (above 4 -> 0). Returns 1, or 0 for other commands. */
int32_t GAMEAPI menu_split_mode_cb(int32_t cmd, uint8_t *dst)
{
    switch (cmd) {
    case 0x12:
        switch (U8(g_config, 0xf)) {
        case 1:
            U16(dst, 0) = G16(0x4b00bc);
            U8(dst, 2) = G8(0x4b00be);
            return 1;
        case 2:
            U16(dst, 0) = G16(0x4b00b8);
            U8(dst, 2) = G8(0x4b00ba);
            return 1;
        case 3:
            U16(dst, 0) = G16(0x4b00b4);
            return 1;
        case 4:
            U16(dst, 0) = G16(0x4b00b0);
            return 1;
        }
        /* 0 or out of range: as 0x13 */
        __attribute__((fallthrough));
    case 0x13:
        U32(dst, 0) = G32(0x4b008c);
        U32(dst, 4) = G32(0x4b0090);
        return 1;
    case 0x14:
        U8(g_config, 0xf) -= 1;
        if (U8(g_config, 0xf) > 4)
            U8(g_config, 0xf) = 4;
        return 1;
    case 0x15:
    case 0x16:
        U8(g_config, 0xf) += 1;
        if (U8(g_config, 0xf) > 4)
            U8(g_config, 0xf) = 0;
        return 1;
    default:
        return 0;
    }
}

/* The movie menu's index (config +0x0d) held below the count 0x4af704. */
static void xa_movie_index_clamp(void)
{
    if ((int32_t)U8(g_config, 0xd) >= GI32(0x4af704))
        U8(g_config, 0xd) = (uint8_t)(G8(0x4af704) - 1);
}

/* 0x44d290: the movie menu's select handler: actions 6 / 7, when 0x4475f0(0)
 * allows, run the chosen entry of the table 0x4af680 between 0x443a10 and
 * 0x443b00 and return 1; else 0. */
int32_t GAMEAPI menu_movie_play(int32_t action)
{
    xa_movie_index_clamp();
    if (action < 6 || action > 7)
        return 0;
    if (GCALL(0x4475f0, fe_repeat_select)(0) == 0)
        return 0;
    GCALL(0x443a10, font_map_init)();
    /* the table holds void(void) movie players (0x42da20.., 0x447f90..) */
    ((void(GAMEAPI *)(void))(uintptr_t)G32(0x4af680 + U8(g_config, 0xd) * 8))();
    GCALL(0x443b00, font_image_release)();
    return 1;
}

/* 0x44d2f0: the movie menu item: command 0x12 the chosen entry's name, 0x13
 * the text at 0x4af6f0, 0x14 index down (no wrap, sic), 0x15 / 0x16 up with
 * wrap. Returns 1, or 0 for other commands. */
int32_t GAMEAPI menu_movie_cb(int32_t cmd, uint8_t *dst)
{
    xa_movie_index_clamp();
    switch (cmd) {
    case 0x12:
        xa_strcpy(dst, (const uint8_t *)(uintptr_t)G32(0x4af684 + U8(g_config, 0xd) * 8));
        return 1;
    case 0x13:
        xa_strcpy(dst, (const uint8_t *)0x4af6f0);
        return 1;
    case 0x14:
        U8(g_config, 0xd) -= 1;
        return 1;
    case 0x15:
    case 0x16:
        U8(g_config, 0xd) += 1;
        if ((int32_t)U8(g_config, 0xd) >= GI32(0x4af704))
            U8(g_config, 0xd) = 0;
        return 1;
    default:
        return 0;
    }
}

/* On/off items: the byte goes through 0x447f20(value, 1, action); returns
 * 1 when it is now off. */
static int32_t xa_config_toggle(int32_t action, uint32_t off)
{
    U8(g_config, off) = (uint8_t)toggle_step(U8(g_config, off), 1, action);
    return U8(g_config, off) == 0;
}

/* 0x44d3e0 / 0x44d420 / 0x44d460 / 0x44d4a0 / 0x44d8d0 / 0x44d910: config
 * +0x0c / +0x0e / +0x39 / +0x0a / +0x10 / +0x11 on/off. */
int32_t GAMEAPI menu_toggle_0c(int32_t action)
{
    return xa_config_toggle(action, 0xc);
}

int32_t GAMEAPI menu_toggle_0e(int32_t action)
{
    return xa_config_toggle(action, 0xe);
}

int32_t GAMEAPI menu_toggle_39(int32_t action)
{
    return xa_config_toggle(action, 0x39);
}

int32_t GAMEAPI menu_toggle_0a(int32_t action)
{
    return xa_config_toggle(action, 0xa);
}

int32_t GAMEAPI menu_toggle_10(int32_t action)
{
    return xa_config_toggle(action, 0x10);
}

int32_t GAMEAPI menu_toggle_11(int32_t action)
{
    return xa_config_toggle(action, 0x11);
}

/* On/off items on one bit of the race's flag bytes +0x720 / +0x721. */
static int32_t xa_race_bit_toggle(int32_t action, uint32_t off, uint32_t bit)
{
    uint32_t r = (uint32_t)toggle_step((U8(g_race, off) >> bit) & 1, 1, action);
    U8(g_race, off) = (uint8_t)((U8(g_race, off) & ~(1u << bit)) | ((r & 1) << bit));
    return (int32_t)((((uint32_t)U8(g_race, off) ^ 0xff) >> bit) & 1);
}

/* 0x44d4e0, 0x44d760, 0x44d7b0, 0x44d810, 0x44d870: race +0x720 bits 5, 1,
 * 2, 3, 4; 0x44d540, 0x44d590, 0x44d5e0, 0x44d640, 0x44d6a0, 0x44d700:
 * race +0x721 bits 0, 1, 2, 3, 5, 4. Returns 1 when the bit is now clear. */
int32_t GAMEAPI menu_race_bit_720_5(int32_t action)
{
    return xa_race_bit_toggle(action, 0x720, 5);
}

int32_t GAMEAPI menu_race_bit_720_1(int32_t action)
{
    return xa_race_bit_toggle(action, 0x720, 1);
}

int32_t GAMEAPI menu_race_bit_720_2(int32_t action)
{
    return xa_race_bit_toggle(action, 0x720, 2);
}

int32_t GAMEAPI menu_race_bit_720_3(int32_t action)
{
    return xa_race_bit_toggle(action, 0x720, 3);
}

int32_t GAMEAPI menu_race_bit_720_4(int32_t action)
{
    return xa_race_bit_toggle(action, 0x720, 4);
}

int32_t GAMEAPI menu_race_bit_721_0(int32_t action)
{
    return xa_race_bit_toggle(action, 0x721, 0);
}

int32_t GAMEAPI menu_race_bit_721_1(int32_t action)
{
    return xa_race_bit_toggle(action, 0x721, 1);
}

int32_t GAMEAPI menu_race_bit_721_2(int32_t action)
{
    return xa_race_bit_toggle(action, 0x721, 2);
}

int32_t GAMEAPI menu_race_bit_721_3(int32_t action)
{
    return xa_race_bit_toggle(action, 0x721, 3);
}

int32_t GAMEAPI menu_race_bit_721_5(int32_t action)
{
    return xa_race_bit_toggle(action, 0x721, 5);
}

int32_t GAMEAPI menu_race_bit_721_4(int32_t action)
{
    return xa_race_bit_toggle(action, 0x721, 4);
}

/* 0x44d950: the time item (config word +0x12, milliseconds; step 100, or
 * 1000 with 0x4beb38 set): command 0x12 clamps it (0 -> 30000, then into
 * 5000..60000) and prints it in seconds ("%.2f" 0x4b00c8 of the float
 * value / 1000.0f), 0x13 prints 60.0 with 0x4b00c0, 0x14 / 0x15..0x16 step
 * it down / up (kept in 0..60000). Returns 1, or 0 for other commands. */
int32_t GAMEAPI menu_time_cb(int32_t cmd, uint8_t *dst)
{
    int32_t step = G32(0x4beb38) ? 1000 : 100;

    switch (cmd) {
    case 0x12: {
        volatile float v, q;
        double d;

        if (U16(g_config, 0x12) == 0)
            U16(g_config, 0x12) = 30000;
        if (U16(g_config, 0x12) < 5000)
            U16(g_config, 0x12) = 5000;
        if (U16(g_config, 0x12) > 60000)
            U16(g_config, 0x12) = 60000;
        v = (float)(int32_t)U16(g_config, 0x12);
        q = v / GF(0x4a33b4);
        d = q;
        sprintf_hooked((char *)dst, (const char *)0x4b00c8, d);
        return 1;
    }
    case 0x13:
        sprintf_hooked((char *)dst, (const char *)0x4b00c0, 60.0); /* 0x404e0000_00000000 */
        return 1;
    case 0x14:
        if ((int32_t)U16(g_config, 0x12) < step)
            U16(g_config, 0x12) = (uint16_t)step;
        U16(g_config, 0x12) -= (uint16_t)step;
        return 1;
    case 0x15:
    case 0x16:
        if ((int32_t)U16(g_config, 0x12) > 60000 - step)
            U16(g_config, 0x12) = (uint16_t)(60000 - step);
        U16(g_config, 0x12) += (uint16_t)step;
        return 1;
    default:
        return 0;
    }
}

/* 0x44da90: a menu's moved handler: a tail jump to 0x448000 (attract mode
 * timer restart); returns its result. */
int32_t GAMEAPI menu_moved_idle(void)
{
#if defined(__i386__)
    /* attract_timer_start is void: the original returns whatever it leaves in eax */
    return ((int32_t(GAMEAPI *)(void))(uintptr_t)0x448000)();
#else
    GCALL(0x448000, attract_timer_start)();
    return 0;
#endif
}
