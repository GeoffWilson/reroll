/* Group XD: the last original functions still reachable after the xa/xb/xc
 * callback groups, mostly callees of those callbacks (0x4219d0-0x496c60):
 *
 *  - the gamma refresh thunk 0x4219d0 (jmp 0x41db00) and the caps lock
 *    probe 0x43d260 (GetKeyState(VK_CAPITAL));
 *  - the name entry box 0x448880 and the name entry's input handler
 *    0x449160 (cursor keys, typing with shift / caps lock / the German
 *    layout, backspace and delete, enter / escape);
 *  - the network menus' drawing: the six player slot quads 0x452980, the
 *    host / join boxes 0x452c20, the session search box 0x452ce0;
 *  - network helpers: player count 0x45f4f0, a type 7 message 0x45fbe0, a
 *    slot's ready state 0x460980, the joined bit 0x48d490, the session
 *    search start / poll 0x48b610 / 0x48b6d0 (IDirectPlay EnumSessions);
 *  - the movie subtitles straight into the locked 16-bit surface: text
 *    0x488ad0 and clearing 0x488cc0;
 *  - the CD player's replay of the requested track 0x496c60.
 *
 * Everything outside the group is called by address; Windows and COM calls
 * by name (winapi.h, com.h). */
#include "../runtime.h"
#include "physics.h"
#include "com.h"

#define U8(p, off) (*(uint8_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G8(a) (*(uint8_t *)(uintptr_t)(a))
#define GS8(a) (*(int8_t *)(uintptr_t)(a))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))

typedef int32_t(GAMEAPI *xd_fn0)(void);
typedef int32_t(GAMEAPI *xd_fn1)(uint32_t a);
typedef int32_t(GAMEAPI *xd_fn2)(uint32_t a, uint32_t b);
typedef int32_t(GAMEAPI *xd_fn3)(uint32_t a, uint32_t b, uint32_t c);
typedef int32_t(GAMEAPI *xd_fn5)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e);
typedef int32_t(GAMEAPI *xd_fn7)(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f,
                                 uint32_t g);

#define CALL0(a) (((xd_fn0)(a))())
#define CALL1(a, x) (((xd_fn1)(a))(A(x)))
#define CALL2(a, x, y) (((xd_fn2)(a))(A(x), A(y)))
#define CALL3(a, x, y, z) (((xd_fn3)(a))(A(x), A(y), A(z)))
#define CALL5(a, x, y, z, w, v) (((xd_fn5)(a))(A(x), A(y), A(z), A(w), A(v)))
#define CALL7(a, x, y, z, w, v, u, t) (((xd_fn7)(a))(A(x), A(y), A(z), A(w), A(v), A(u), A(t)))

#define CONFIG PTR(G32(0x4b2198))

#define D3D_RENDER_STATES 0x41db00
#define BOX_FRAME_PLACE 0x431780 /* fe_box_frame_place(list, packets, colour, x, y, w, h) */
#define FONT_IMAGE_INFO 0x443b20 /* (&pixels, &palette, &stride) */
#define FE_REPEAT_SELECT 0x4475f0
#define FE_MENU_GOTO 0x4479d0
#define FE_MENU_BACK 0x447ab0
#define VOICE_PLAY_PLAIN 0x472a90
#define MEM_FILL 0x476f50
#define SURFACE_LOCK 0x4875a0   /* (0, surface, desc) */
#define SURFACE_UNLOCK 0x487610 /* (surface) */
#define NET_SEND_TO 0x48cdd0
#define CDAUDIO_PLAY 0x496770
#define CRT_STRSTR 0x4980d0
#define CRT_TOUPPER 0x498540
#define CRT_TOLOWER 0x49867b

#define BOX_LIST 0x6cdd90
#define NET_FLAGS 0x5d622c
#define NAME_CURSOR 0x4c99bc
#define KEY_SHIFT 0x4beb38
#define KEY_CTRL 0x4beb3c
#define KEY_ALT 0x4beb40
#define KEY_CODE 0x4af43c

/* fild dword v; fstp dword: the float's bits. */
static inline uint32_t xd_i2f(int32_t v)
{
    uint32_t r;

    __asm__ volatile("fildl %1\n\t"
                     "fstps %0"
                     : "=m"(r)
                     : "m"(v));
    return r;
}

/* ===================================================== small ones */

/* 0x4219d0: thunk, jmp 0x41db00 (the render states, d3d_render_states). */
int32_t GAMEAPI d3d_render_states_thunk(void)
{
    return CALL0(D3D_RENDER_STATES);
}

/* 0x43d260: GetKeyState(VK_CAPITAL), sign-extended. */
int32_t GAMEAPI capslock_state(void)
{
    return GetKeyState(0x14);
}

/* 0x45f4f0: the network player count (signed byte 0x5d5cc8). */
int32_t GAMEAPI net_player_count(void)
{
    return GS8(0x5d5cc8);
}

/* 0x460980: 1 if network player `slot` (0..5; 0x66c bytes from 0x5d2cd0)
 * has state +0x20 == 5. */
int32_t GAMEAPI net_slot_ready(uint32_t slot)
{
    if (slot >= 6)
        return 0;
    return GI32(0x5d2cf0 + slot * 0x66c) == 5;
}

/* 0x48d490: the network flags' bit 1 (joined). */
int32_t GAMEAPI net_joined(void)
{
    return (G32(NET_FLAGS) >> 1) & 1;
}

/* 0x496c60: plays the requested CD track 0x4b4a90 again unless -1. */
void GAMEAPI cdaudio_replay_request(void)
{
    int32_t track = GI32(0x4b4a90);

    if (track != -1)
        CALL1(CDAUDIO_PLAY, track);
}

/* 0x45fbe0: in a network game (flags bits 0 and 1), sends message 7 with
 * the 6 bytes at `data` (dword at +8, word at +0xc of a 16-byte message) to
 * everyone (0x48cdd0(2, 0, msg, 0x10, 0)); returns its result, else 0. The
 * original leaves bytes 1..7 and 0xe..0xf of the message as stack leftovers;
 * ours sends zeros there. */
int32_t GAMEAPI net_send_type7(const uint8_t *data)
{
    uint8_t msg[16];

    if (!(G8(NET_FLAGS) & 1))
        return 0;
    if (!(G8(NET_FLAGS) & 2))
        return 0;
    memset(msg, 0, sizeof msg);
    msg[0] = 7;
    U32(msg, 8) = U32(data, 0);
    U16(msg, 0xc) = U16(data, 4);
    return CALL5(NET_SEND_TO, 2, 0, msg, 0x10, 0);
}

/* ===================================================== session search */

/* 0x48b610 / 0x48b6d0: IDirectPlay (0x5d2060) EnumSessions with a session
 * description (0x50 bytes: size, the application GUID 0x4a3500 at +0x18),
 * timeout 0, callback 0x48bb30, context 0x6c8000, flags 0x92. The result
 * goes to 0x5d6230; success sets the flags' bit 12 and returns 1, failure
 * returns 1 for 0x8877015e (DPERR_CONNECTING) else 0. */
static int32_t xd_enum_sessions(void)
{
    uint8_t desc[0x50];
    uint32_t obj, hr;

    CALL3(MEM_FILL, desc, 0, 0x50);
    U32(desc, 0) = 0x50;
    U32(desc, 0x18) = G32(0x4a3500);
    U32(desc, 0x1c) = G32(0x4a3504);
    U32(desc, 0x20) = G32(0x4a3508);
    U32(desc, 0x24) = G32(0x4a350c);
    obj = G32(0x5d2060);
    hr = (uint32_t)IDirectPlay4A_EnumSessions(obj, desc, 0, (void *)(uintptr_t)0x48bb30, GMEM(G32(0x6c8000)), 0x92);
    G32(0x5d6230) = hr;
    if (hr != 0)
        return hr == 0x8877015e;
    G32(NET_FLAGS) |= 0x1000;
    return 1;
}

/* 0x48b610: starts the session search: flags bit 12 off, message count
 * 0x5d2cc4 = 0, EnumSessions. */
int32_t GAMEAPI net_sessions_search_start(void)
{
    G32(NET_FLAGS) &= ~0x1000u;
    G32(0x5d2cc4) = 0;
    return xd_enum_sessions();
}

/* 0x48b6d0: polls the session search: message count 0x5d2cc4 = 0,
 * EnumSessions. */
int32_t GAMEAPI net_sessions_search_poll(void)
{
    G32(0x5d2cc4) = 0;
    return xd_enum_sessions();
}

/* ===================================================== menu boxes */

/* 0x448880: the name entry's text box frame (packets 0x4c99c0) at
 * (0x4cb540 + 0x1e, 0x82), 0x244 x 0xfa, white edge. */
void GAMEAPI name_entry_box_place(void)
{
    uint32_t colour = 0xffffff;

    CALL7(BOX_FRAME_PLACE, BOX_LIST, 0x4c99c0, &colour, G32(0x4cb540) + 0x1e, 0x82, 0x244, 0xfa);
}

/* 0x452ce0: the session search menu's box (packets 0x4c24d0) at
 * (0x4cb540 + 0x1e, 0x1a4), 0x244 x 0x28, white edge. */
void GAMEAPI net_sessions_box_place(void)
{
    uint32_t colour = 0xffffff;

    CALL7(BOX_FRAME_PLACE, BOX_LIST, 0x4c24d0, &colour, G32(0x4cb540) + 0x1e, 0x1a4, 0x244, 0x28);
}

/* 0x452980: the network menu's six player slots: for each slot i whose
 * entry 0x4cb110[i] is not -1, a textured quad packet (0x78 bytes at
 * 0x4cb198 + 0x78 i, type 0x98 / length 0xc, colour ff ff ff, z 2000.0 and
 * 0x40 per corner) from x 0x4cb540 + 0x238, y 0x8e + 0x32 i (scaled by
 * 0x4acc38 / 640 and 0x4acc3c / 480) over the image rectangle at
 * 0x4be340 + 0x390 (ready, 0x460980) or + 0x380, its u / v moved by 36 x
 * {1, 0, 2, 6, 5, 3, 4}[entry]; linked into the ordering list (0x6cd518 +
 * 8 * (texture page & 0x3f) when 0x6cce84, else 0x6cd520). Entries outside
 * 0..6 read other stack slots in the original; ours reads past the table. */
void GAMEAPI net_slots_place(void)
{
    static const int32_t shift[7] = {1, 0, 2, 6, 5, 3, 4};
    uint32_t base = G32(0x4be340);
    uint32_t ready_rect = base + 0x390, rect = base + 0x380;
    int32_t xb = GI32(0x4cb540) + 0x238;
    int32_t i = 0, y;

    for (y = 0x8e; y < 0x1ba; y += 0x32, i++) {
        uint32_t entry = 0x4cb110 + 4 * (uint32_t)i;
        uint8_t *p = PTR(0x4cb198 + 0x78 * (uint32_t)i);
        uint8_t r[16];
        uint32_t src, w, h, fx0, fy0, fx1, fy1, list;
        uint8_t k;

        if (GI32(entry) == -1)
            continue;
        src = net_slot_ready((uint32_t)i) ? ready_rect : rect;
        memcpy(r, PTR(src), 16);
        w = G32(0x4acc38);
        k = (uint8_t)(shift[GI32(entry)] * 36);
        r[0] = (uint8_t)(r[0] + k);
        r[4] = (uint8_t)(r[0] + 0x23);
        r[8] = (uint8_t)(r[8] + k);
        r[10] = (uint8_t)(r[8] + 0x23);
        h = G32(0x4acc3c);
        fx0 = xd_i2f((int32_t)(w * (uint32_t)xb) / 640);
        fy0 = xd_i2f((int32_t)((uint32_t)y * h) / 480);
        fx1 = xd_i2f((int32_t)((uint32_t)((int32_t)r[4] - (int32_t)r[0] + xb + 1) * w) / 640);
        fy1 = xd_i2f((int32_t)((uint32_t)((int32_t)r[9] - (int32_t)r[1] + y + 1) * h) / 480);
        U32(p, 0x10) = fx0;
        U32(p, 0x14) = fy0;
        U32(p, 0x2c) = fx1;
        U32(p, 0x30) = fy0;
        U32(p, 0x48) = fx0;
        U32(p, 0x4c) = fy1;
        U32(p, 0x64) = fx1;
        U32(p, 0x68) = fy1;
        U32(p, 0xc) = 0x44fa0000; /* 2000.0f */
        U32(p, 8) = 0x40;
        U32(p, 0x28) = 0x44fa0000;
        U32(p, 0x24) = 0x40;
        U32(p, 0x44) = 0x44fa0000;
        U32(p, 0x40) = 0x40;
        U32(p, 0x60) = 0x44fa0000;
        U32(p, 0x5c) = 0x40;
        memcpy(p + 0x20, r, 4);
        memcpy(p + 0x3c, r + 4, 4);
        memcpy(p + 0x58, r + 8, 2);
        memcpy(p + 0x74, r + 10, 2);
        p[4] = 0xff;
        p[5] = 0xff;
        p[6] = 0xff;
        U32(p, 0) = (U32(p, 0) & 0xffffff) | 0xc000000;
        p[7] = 0x98;
        if (G32(0x6cce84))
            list = G32(0x6cd518 + 8 * (U8(p, 0x3e) & 0x3f));
        else
            list = G32(0x6cd520);
        list &= 0x7fffffff;
        U32(p, 0) = U32(list, 0);
        U32(list, 0) = A(p);
    }
}

/* 0x452c20: the host / join menu's two boxes. The widest / tallest text
 * extents so far (0x4cb7f0 / 0x4cb7f4) grow to the current ones (0x4cb47c -
 * 0x4cb474, 0x4cb480 - 0x4cb478); the first box (packets 0x4ca860) at
 * (w + 0x4cb474 + 0xf, 0x82), (0x23f - w) x 0x14a, the second (packets
 * 0x4c24d0) at (0x4cb540 + 0x1e, h + 0x4cb478 + 0xf), w x (0x13b - h), both
 * with a white edge; then the player slots (0x452980). */
void GAMEAPI net_host_boxes_place(void)
{
    uint32_t colour = 0xffffff;
    int32_t th = GI32(0x4cb480) - GI32(0x4cb478);
    int32_t tw = GI32(0x4cb47c) - GI32(0x4cb474);
    int32_t w = GI32(0x4cb7f0);
    int32_t left = GI32(0x4cb474);
    int32_t h;

    if (w < tw) {
        w = tw;
        G32(0x4cb7f0) = (uint32_t)w;
    }
    if (GI32(0x4cb7f4) < th)
        G32(0x4cb7f4) = (uint32_t)th;
    CALL7(BOX_FRAME_PLACE, BOX_LIST, 0x4ca860, &colour, w + left + 0xf, 0x82, 0x23f - w, 0x14a);
    h = GI32(0x4cb7f4);
    w = GI32(0x4cb7f0);
    CALL7(BOX_FRAME_PLACE, BOX_LIST, 0x4c24d0, &colour, G32(0x4cb540) + 0x1e, h + GI32(0x4cb478) + 0xf, w,
          0x13b - h);
    net_slots_place();
}

/* ===================================================== name entry */

/* The shifted keys: German layout (config +0x36 == 2), else UK. */
static int32_t xd_shift_de(int32_t c)
{
    switch (c) {
    case -0x21: return 0x3f; /* sharp s */
    case 0x2c: return 0x3b;
    case 0x2d: return 0x5f;
    case 0x2e: return 0x3a;
    case 0x30: return 0x3d;
    case 0x31: return 0x21;
    case 0x32: return 0x22;
    case 0x33: return -0x5d; /* pound */
    case 0x34: return 0x24;
    case 0x35: return 0x25;
    case 0x36: return 0x26;
    case 0x37: return 0x2f;
    case 0x38: return 0x28;
    case 0x39: return 0x29;
    case 0x3c: return 0x3e;
    }
    return c;
}

static int32_t xd_shift_uk(int32_t c)
{
    switch (c) {
    case 0x23: return 0x7e;
    case 0x27: return 0x40;
    case 0x2c: return 0x3c;
    case 0x2d: return 0x5f;
    case 0x2e: return 0x3e;
    case 0x2f: return 0x3f;
    case 0x30: return 0x29;
    case 0x31: return 0x21;
    case 0x32: return 0x22;
    case 0x33: return -0x5d;
    case 0x34: return 0x24;
    case 0x35: return 0x25;
    case 0x36: return 0x5e;
    case 0x37: return 0x26;
    case 0x38: return 0x2a;
    case 0x39: return 0x28;
    case 0x3b: return 0x3a;
    case 0x3d: return 0x2b;
    case 0x5b: return 0x7b;
    case 0x5c: return 0x7c;
    case 0x5d: return 0x7d;
    case 0x60: return -0x54; /* not sign */
    }
    return c;
}

/* The type keys 0xe3..0xee (shifted only). */
static const int32_t xd_type_keys[12] = {0x21, 0x3f, 0x2f, 0x5c, 0x40, 0x23, 0x7e, 0x5f, -0x21, -0x24, -0x2a, -0x3c};

static void xd_refuse(void)
{
    CALL3(VOICE_PLAY_PLAIN, 0x18, 0x800, 0x36b0);
}

/* 0x449160: the name entry's menu input handler (action, text buffer, mode,
 * buffer size, next screen, its argument, change callback, event
 * callback). Mode 2 returns 0; the menu hook 0x4cb528 sees every action.
 * Actions 0 / 1 / 4 / 5 return 0 in mode 1. Action 7 (select, with the
 * mouse flag 0x4be33c and the select repeat) goes to the screen (back when
 * its argument is 2) and tells the event callback 7. Action 10 with a key
 * (0x4af448): enter (0xd, select repeat) does the same with event 6,
 * escape (0x1b, repeat 1) goes back with event 8; other keys edit the text
 * (0x4af444 typed, alt 0x4beb40 not held) at the cursor 0x4c99bc: 0xf5 /
 * 0xf6 left / right (shift: to the ends), a character (tolower, toupper
 * when caps lock xor shift; ctrl + Q / + with the German layout gives @ /
 * ~; shifted punctuation by layout; the type keys 0xe3..0xee; ` as ') is
 * inserted when it is in the list 0x4afb80 or alphanumeric and fits, 8
 * deletes before the cursor (shift: everything before it), 0xfb at it
 * (shift: everything from it); a full buffer or nothing to delete plays the
 * refusal 0x18. A change sets 0x4cb58c and calls the change callback with
 * the text. Returns 1. */
int32_t GAMEAPI name_entry_input(uint32_t action, uint8_t *text, int32_t mode, int32_t size, uint32_t screen,
                                 int32_t screen_arg, uint32_t on_change, uint32_t on_event)
{
    int32_t last, len, cur, c, caps, changed = 0;
    uint32_t hook = G32(0x4cb528), key;
    uint8_t buf[2];

    if (mode == 2)
        return 0;
    if (hook)
        CALL1(hook, action);
    switch (action) {
    case 0:
    case 1:
    case 4:
    case 5:
        return mode == 1 ? 0 : 1;
    case 7:
        if (G32(0x4be33c) && CALL1(FE_REPEAT_SELECT, 0)) {
            if (screen_arg == 2)
                CALL0(FE_MENU_BACK);
            else
                CALL2(FE_MENU_GOTO, screen, screen_arg);
            if (on_event)
                CALL1(on_event, 7);
        }
        return 1;
    case 10:
        break;
    default:
        return 1;
    }
    if (G32(0x4af448)) {
        key = G32(KEY_CODE);
        if (key == 0xd) {
            if (CALL1(FE_REPEAT_SELECT, 0) == 0)
                return 1;
            if (screen_arg == 2)
                CALL0(FE_MENU_BACK);
            else
                CALL2(FE_MENU_GOTO, screen, screen_arg);
            if (on_event)
                CALL1(on_event, 6);
            return 1;
        }
        if (key == 0x1b) {
            if (CALL1(FE_REPEAT_SELECT, 1) == 0)
                return 1;
            CALL0(FE_MENU_BACK);
            if (on_event)
                CALL1(on_event, 8);
            return 1;
        }
    }
    if (text == 0 || size <= 1)
        return 1;
    last = size - 1;
    text[last] = 0;
    for (len = 0; text[len]; len++)
        ;
    cur = GI32(NAME_CURSOR);
    if (cur > len || cur < 0) {
        cur = len;
        G32(NAME_CURSOR) = (uint32_t)len;
    }
    if (G32(KEY_ALT) || G32(0x4af444) == 0)
        return 1;
    key = G32(KEY_CODE);
    if (key == 0xf5) {
        if (G32(KEY_SHIFT))
            G32(NAME_CURSOR) = 0;
        else if (cur > 0)
            G32(NAME_CURSOR) = (uint32_t)(cur - 1);
        goto changed;
    }
    if (key == 0xf6) {
        if (G32(KEY_SHIFT))
            G32(NAME_CURSOR) = (uint32_t)len;
        else if (cur < len)
            G32(NAME_CURSOR) = (uint32_t)(cur + 1);
        goto changed;
    }
    c = CALL1(CRT_TOLOWER, key);
    caps = capslock_state();
    if (G32(KEY_SHIFT))
        caps = caps == 0;
    if (caps)
        c = CALL1(CRT_TOUPPER, G32(KEY_CODE));
    if (G32(KEY_CTRL) && U8(CONFIG, 0x36) == 2) {
        int32_t u = CALL1(CRT_TOUPPER, c);
        if (u == 0x2b)
            c = 0x7e;
        else if (u == 0x51)
            c = 0x40;
    }
    if (G32(KEY_SHIFT)) {
        c = U8(CONFIG, 0x36) == 2 ? xd_shift_de(c) : xd_shift_uk(c);
        if ((uint32_t)(c - 0xe3) <= 0xb) {
            c = xd_type_keys[c - 0xe3];
            goto have;
        }
    }
    if (c == 0x60)
        c = 0x27;
have:
    buf[0] = (uint8_t)c;
    buf[1] = 0;
    if (c == 0)
        return 1;
    if (CALL2(CRT_STRSTR, 0x4afb80, buf) != 0 || (c >= 0x30 && c <= 0x39) || (c >= 0x61 && c <= 0x7a) ||
        (c >= 0x41 && c <= 0x5a)) {
        int32_t at = GI32(NAME_CURSOR), i;

        if (at > last) {
            xd_refuse();
        } else {
            if (len <= last) {
                text[len + 1] = 0; /* sic: text[size] when the buffer is full */
                at = GI32(NAME_CURSOR);
            }
            for (i = len + 1; i > at; i--) {
                text[i] = text[i - 1];
                at = GI32(NAME_CURSOR);
            }
            text[at] = (uint8_t)c;
            G32(NAME_CURSOR) = G32(NAME_CURSOR) + 1;
            changed = 1;
            text[last] = 0;
        }
    }
    if (c == 8) {
        int32_t at = GI32(NAME_CURSOR), to;
        uint8_t *s, *d, ch;

        if (at == 0) {
            xd_refuse();
        } else {
            to = G32(KEY_SHIFT) ? 0 : at - 1;
            G32(NAME_CURSOR) = (uint32_t)to;
            d = text + to;
            s = text + at;
            for (;;) {
                ch = *s;
                *d = ch;
                if (ch == 0)
                    break;
                s++;
                d++;
            }
            goto changed;
        }
    } else if (c == 0xfb) {
        int32_t at = GI32(NAME_CURSOR);
        uint8_t *p, ch;

        if (at > last) {
            xd_refuse();
        } else if (G32(KEY_SHIFT)) {
            text[at] = 0;
            goto changed;
        } else {
            p = text + at;
            for (;;) {
                ch = p[1];
                p[0] = ch;
                if (ch == 0)
                    break;
                p++;
            }
            goto changed;
        }
    }
    if (!changed)
        return 1;
changed:
    G32(0x4cb58c) = 1;
    CALL1(on_change, text);
    return 1;
}

/* ===================================================== movie subtitles */

/* 0x488ad0: draws `text` in the font map (glyph index 0x6c1a7c, glyphs of
 * 16 bytes at 0x6c0a7c: x, y, w, h words; space 0x6c0a70, gap 0x6c0a74)
 * straight into the 16-bit surface 0x4d5f48 (when 0x4d5f70 == 16 and it
 * locks) at (x, y - 0x28), pixels through the font's palette. Glyphs of
 * zero width or height loop 2^32 times in the original, as in ours. */
void GAMEAPI movie_text_draw(const uint8_t *text, int32_t x, int32_t y)
{
    uint32_t pixels, palette, stride;
    uint8_t desc[0x6c];

    y -= 0x28;
    CALL3(FONT_IMAGE_INFO, &pixels, &palette, &stride);
    if (G32(0x4d5f70) != 0x10)
        return;
    if (CALL3(SURFACE_LOCK, 0, G32(0x4d5f48), desc) == 0)
        return;
    for (; *text; text++) {
        uint8_t ch = *text, idx;
        uint32_t g, gw, gh, gx, gy, pitch, rows, cols;
        uint8_t *src, *dst;

        if (ch == 0x20) {
            x += GI32(0x6c0a70);
            continue;
        }
        idx = G8(0x6c1a7c + ch);
        if (idx == 0xff)
            continue;
        g = 0x6c0a7c + ((uint32_t)idx << 4);
        gx = U16(g, 0);
        gy = U16(g, 2);
        gw = U16(g, 4);
        gh = U16(g, 6);
        pitch = U32(desc, 0x10);
        src = PTR(pixels + gy * stride + gx);
        dst = PTR(U32(desc, 0x24) + (gy + (uint32_t)y) * pitch + 2 * (uint32_t)x);
        rows = gh;
        do {
            cols = gw;
            do {
                *(uint16_t *)dst = U16(palette, 2 * (uint32_t)*src);
                src++;
                dst += 2;
            } while (--cols);
            src += stride - gw;
            dst += pitch - 2 * gw;
        } while (--rows);
        x += (int32_t)(U16(g, 4) + G32(0x6c0a74));
    }
    CALL1(SURFACE_UNLOCK, G32(0x4d5f48));
}

/* 0x488cc0: clears the 16-bit surface 0x4d5f48 from row `top` down (screen
 * 0x4acc38 x 0x4acc3c) when 0x4d5f70 == 16: returns 0 otherwise, else 1
 * (also when the lock fails). top == height loops 2^32 rows, as the
 * original. */
int32_t GAMEAPI movie_text_clear(int32_t top)
{
    uint8_t desc[0x6c];
    uint32_t rows, w, pitch, n;
    uint8_t *dst;

    if (G32(0x4d5f70) != 0x10)
        return 0;
    if (CALL3(SURFACE_LOCK, 0, G32(0x4d5f48), desc) == 0)
        return 1;
    rows = G32(0x4acc3c) - (uint32_t)top;
    w = G32(0x4acc38);
    pitch = U32(desc, 0x10);
    dst = PTR(pitch * (uint32_t)top + U32(desc, 0x24));
    do {
        for (n = w; n; n--) {
            *(uint16_t *)dst = 0;
            dst += 2;
        }
        dst += pitch - 2 * w;
    } while (--rows);
    CALL1(SURFACE_UNLOCK, G32(0x4d5f48));
    return 1;
}

/* 0x48bb30: EnumSessions callback (stdcall: session description, timeout
 * pointer, flags, context), from 0x48b610 / 0x48b6d0. Timed out (flags bit
 * 0): 0, stop. Else session record n (count 0x5d2cc4) of the 0x9c-byte
 * records at 0x5d26b0: +4 the instance GUID (desc +8), +0x14 = 0x10, +0x18
 * the name (desc +0x30, unbounded copy as the original), +0 flags: 1 when
 * full (players +0x2c >= max +0x28, unsigned), 2 when desc flags +4 has
 * 0x21; count up; 1, go on. */
int32_t __attribute__((stdcall)) net_session_enum_cb(const uint8_t *desc, uint32_t *timeout, uint32_t flags,
                                                     void *context)
{
    uint32_t rec = 0x5d26b0 + G32(0x5d2cc4) * 0x9c;
    const char *name = (const char *)(uintptr_t)U32(desc, 0x30);
    char *to = (char *)(uintptr_t)(rec + 0x18);

    (void)timeout;
    (void)context;
    if (flags & 1)
        return 0;
    for (uint32_t k = 0; k < 16; k += 4)
        G32(rec + 4 + k) = U32(desc, 8 + k);
    G32(rec + 0x14) = 0x10;
    G32(rec) = 0;
    do
        *to++ = *name;
    while (*name++);
    if (U32(desc, 0x2c) >= U32(desc, 0x28))
        G32(rec) |= 1;
    if (desc[4] & 0x21)
        G32(rec) |= 2;
    G32(0x5d2cc4)++;
    return 1;
}
