/* The Vulkan renderer in the game's own window (docs/renderer/plan.md,
 * step 6). native/loader.c sets REROLL_VULKAN=1.
 *
 * - DirectDraw runs in the game's own windowed mode (the /W switch: no
 *   display mode change); its window is the SDL window (native/user32.c,
 *   sized by Options > Display). Direct3D still exists (the game makes
 *   its texture surfaces, viewports and scenes with it) but never reaches
 *   the screen: nothing is drawn into it and its flip is skipped.
 * - Every ordering table walk (DrawOTag, through the wrappers 0x428470 /
 *   0x428480) and single packet (DrawPrim 0x428460) is decoded straight
 *   from game memory (src/render/packets.c) into the frame instead of
 *   going to the original back end; the flip 0x4876f0 presents the frame
 *   (src/render/vkr.c, src/render/vkwin.c).
 * - The image tiles of 0x4203a0 (the loading pictures) are packets too,
 *   handed to the back end directly at 0x420582: that call comes here.
 *   Frames with no packets at all show the game's back buffer.
 * - Textures come from the game's system-memory texture surfaces: read
 *   when a slot's surface changes, after any texture_upload 0x41e710, for
 *   the slots the re-upload queue (0x4bde04 / 0x4b836c, drained by
 *   0x429070) names, and one slot per frame is checked against its
 *   checksum in case of anything else.
 * - The 4:3 game screen is fitted by height and centred; widescreen.c
 *   widens the culling to the window's aspect unless REROLL_ASPECT says
 *   otherwise.
 * If Vulkan cannot start, the original back end carries on (windowed). */
#include "vkgame.h"
#include "widescreen.h"
#ifdef REROLL_NATIVE
#include "../native/display.h"
void nt_profile_flush(void);
#endif

#include "hook.h"
#include "log.h"
#include "render/draw.h"
#include "render/vkr.h"
#include "render/vkwin.h"
#include "runtime.h"
#include "win32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G32(a) (*(uint32_t *)(a))
#define G_WINDOW (*(HWND *)0x6c8000)
#define G_SCREEN_W (*(int32_t *)0x4acc38)
#define G_SCREEN_H (*(int32_t *)0x4acc3c)
#define G_BACK_BUFFER (*(void ***)0x4d5f4c)

#define TEX_SLOTS 64
#define TEX_SLOT(s) ((uint8_t *)0x4ba6fc + (s) * 0x24)
#define TEX_QUEUE_COUNT (*(int32_t *)0x4bde04)
#define TEX_QUEUE ((const int32_t *)0x4b836c)

#define MAX_PRIMS 0x40000
#define MAX_VERTS 0x100000

static int state; /* 0 off, 1 on and waiting for the window, 2 running, -1 failed */
static int target_w, target_h;
static int anisotropy = 1; /* texture filtering samples (1 = off): REROLL_ANISOTROPY, or Options > Graphics */
static struct vkwin win;
static struct rd_prim *prims;
static struct rd_vertex *verts;
static int frame_open, frame_walks;
static struct vkr_mapping map;

static uint32_t tex_signature[TEX_SLOTS], tex_checksum[TEX_SLOTS];
static uint8_t tex_dirty[TEX_SLOTS];
static int tex_rescan, tex_round;
static uint8_t *rgba; /* conversion buffer */
static int debug_frames_left = -1, debug_line_len;
static char debug_line[512];
static uint32_t stat_uploads, stat_frames, stat_walks, stat_prims, stat_max_walks, stat_max_prims, frame_prims;
static uint64_t stat_draw, stat_textures, stat_present, stat_last, stat_freq;

static uint64_t now(void)
{
    uint64_t t;
    QueryPerformanceCounter(&t);
    return t;
}

/* microseconds per frame for a total of `ticks` */
static int32_t per_frame_us(uint64_t ticks)
{
    return stat_frames ? (int32_t)((float)(int32_t)ticks * 1e6f / (float)(int32_t)stat_freq / (float)stat_frames) : 0;
}
static uint32_t rgba_size;

/* ---- memory reads for the decoder ---- */

/* Whether [address, address + len) is committed memory. The decoder reads
 * thousands of packets a frame from a handful of buffers, so the last few
 * regions found are cached. */
static int readable(uint32_t address, uint32_t len)
{
    static uint32_t start[8], end[8];
    static int next;
    struct memory_basic_information info;
    int i;

    if (address < 0x10000 || address + len < address)
        return 0;
    for (i = 0; i < 8; i++)
        if (address >= start[i] && address + len <= end[i])
            return 1;
    if (!VirtualQuery((const void *)address, &info, sizeof info) || info.state != MEM_COMMIT)
        return 0;
    i = next++ & 7;
    start[i] = (uint32_t)info.base_address;
    end[i] = start[i] + info.region_size;
    return address + len <= end[i];
}

static const uint8_t *memory_read(void *ctx, uint32_t address, uint32_t len)
{
    (void)ctx;
    return readable(address, len) ? (const uint8_t *)address : 0;
}

/* DrawPrim: one packet as a walk of its own, from a table at the fake
 * address 0x10 (as src/capture.c records it). */
struct one_packet {
    uint32_t table, address, size;
    uint8_t copy[0x84];
};

static const uint8_t *packet_read(void *ctx, uint32_t address, uint32_t len)
{
    const struct one_packet *p = ctx;
    if (address == 0x10 && len <= 4)
        return (const uint8_t *)&p->table;
    if (address >= p->address && address + len <= p->address + p->size)
        return p->copy + (address - p->address);
    return memory_read(0, address, len);
}

/* Bytes a packet occupies, by code >> 2 (pipeline.md 2.4 and 2.5). */
static uint32_t packet_size(const uint8_t *packet)
{
    switch (packet[7] >> 2) {
    case 0x23: return 0x84;
    case 0x27: return 0x64;
    case 0x26: return 0x78;
    case 0x25: return 0x60;
    case 0x2a: return 0x68;
    case 0x2b: return 0x3c;
    case 0x0b: return 0x28;
    case 0x0c: return 0x1c;
    case 0x19:
    case 0x11: return 0x14;
    case 0x28:
    case 0x2c:
    case 0x2d: return 0x08;
    default: return 0x84;
    }
}

/* ---- pixel conversion ---- */

static uint8_t *rgba_buffer(uint32_t pixels)
{
    if (pixels * 4 > rgba_size) {
        free(rgba);
        rgba_size = pixels * 4;
        rgba = malloc(rgba_size);
        if (!rgba)
            rgba_size = 0;
    }
    return rgba;
}

static uint32_t channel(uint32_t v, uint32_t mask)
{
    int shift = 0;
    if (!mask)
        return 255;
    while (!(mask & 1u << shift))
        shift++;
    return ((v & mask) >> shift) * 255 / (mask >> shift);
}

/* 16-bit pixels to RGBA (colour-key range -> alpha 0) through a table built
 * for the surface's masks and key. */
static const uint32_t *lut16(const uint32_t masks[3], uint32_t key_low, uint32_t key_high)
{
    static uint32_t *table, built[5];
    uint32_t v;

    if (!table) {
        table = malloc(0x10000 * 4);
        if (!table)
            return 0;
        built[0] = ~0u;
    }
    if (built[0] != masks[0] || built[1] != masks[1] || built[2] != masks[2] || built[3] != key_low ||
        built[4] != key_high) {
        for (v = 0; v < 0x10000; v++)
            table[v] = channel(v, masks[0]) | channel(v, masks[1]) << 8 | channel(v, masks[2]) << 16 |
                       (v >= key_low && v <= key_high ? 0u : 0xff000000u);
        built[0] = masks[0];
        built[1] = masks[1];
        built[2] = masks[2];
        built[3] = key_low;
        built[4] = key_high;
    }
    return table;
}

/* Converts a locked surface (DDSURFACEDESC in `desc`) to RGBA in the
 * conversion buffer; returns it, or null for an unsupported format. */
static uint8_t *convert(const uint8_t *desc, uint32_t key_low, uint32_t key_high, uint32_t *checksum)
{
    uint32_t w = *(const uint32_t *)(desc + 0xc), h = *(const uint32_t *)(desc + 8);
    uint32_t pitch = *(const uint32_t *)(desc + 0x10), bits = *(const uint32_t *)(desc + 0x54);
    const uint32_t *masks = (const uint32_t *)(desc + 0x58);
    const uint8_t *pixels = *(const uint8_t *const *)(desc + 0x24);
    uint32_t *out, x, y, sum = 2166136261u;

    if (!w || !h || w > 4096 || h > 4096 || !(out = (uint32_t *)rgba_buffer(w * h)))
        return 0;
    if (bits == 16) {
        const uint32_t *table = lut16(masks, key_low, key_high);
        if (!table)
            return 0;
        for (y = 0; y < h; y++) {
            const uint16_t *row = (const uint16_t *)(pixels + y * pitch);
            for (x = 0; x < w; x++) {
                sum = (sum ^ row[x]) * 16777619u;
                *out++ = table[row[x]];
            }
        }
    } else if (bits == 32) {
        int rs = 0, gs = 0, bs = 0;
        while (masks[0] && !(masks[0] >> rs & 1))
            rs++;
        while (masks[1] && !(masks[1] >> gs & 1))
            gs++;
        while (masks[2] && !(masks[2] >> bs & 1))
            bs++;
        for (y = 0; y < h; y++) {
            const uint32_t *row = (const uint32_t *)(pixels + y * pitch);
            for (x = 0; x < w; x++)
                *out++ = (row[x] >> rs & 0xff) | (row[x] >> gs & 0xff) << 8 | (row[x] >> bs & 0xff) << 16 |
                         0xff000000u;
        }
    } else {
        return 0;
    }
    if (checksum)
        *checksum = sum ^ w << 16 ^ h;
    return rgba;
}

typedef int32_t(__stdcall *lock_fn)(void *, void *, uint8_t *, DWORD, void *);
typedef int32_t(__stdcall *unlock_fn)(void *, void *);
typedef int32_t(__stdcall *color_key_fn)(void *, DWORD, uint32_t *);

static int lock(void **surface, uint8_t desc[0x6c])
{
    if (!surface || !readable((uint32_t)surface, 4))
        return 0;
    memset(desc, 0, 0x6c);
    *(uint32_t *)desc = 0x6c;
    return ((lock_fn)((void **)*surface)[0x64 / 4])(surface, 0, desc, 1 | 0x10 /* WAIT | READONLY */, 0) == 0;
}

static void unlock(void **surface)
{
    ((unlock_fn)((void **)*surface)[0x80 / 4])(surface, 0);
}

/* ---- textures ---- */

/* Reads texture slot `slot` and uploads it when `force` or its checksum
 * changed. */
static void texture_slot(int slot, int force)
{
    uint8_t *rec = TEX_SLOT(slot), desc[0x6c];
    void **surface = *(void ***)(rec + 4);
    uint32_t key[2] = {0, 0}, sum = 0; /* no key set: texel 0 is see-through, as the captures were checked */
    const uint8_t *pixels;

    if (!lock(surface, desc))
        return;
    ((color_key_fn)((void **)*surface)[0x40 / 4])(surface, 8 /* DDCKEY_SRCBLT */, key);
    pixels = convert(desc, key[0], key[1], &sum);
    unlock(surface);
    if (pixels && (force || sum != tex_checksum[slot])) {
        tex_checksum[slot] = sum;
        stat_uploads++;
        vkr_set_texture(win.r, slot, (int)*(uint32_t *)(desc + 0xc), (int)*(uint32_t *)(desc + 8), pixels);
    }
}

static void textures_update(void)
{
    uint64_t t0 = now();
    int slot;

    for (slot = 0; slot < TEX_SLOTS; slot++) {
        const uint8_t *rec = TEX_SLOT(slot);
        uint32_t signature = G32(rec + 4) ^ G32(rec + 0x10) << 20 ^ G32(rec + 0x14) << 10 ^ G32(rec + 0x18);
        if (!G32(rec + 0x10) || !G32(rec + 4))
            continue;
        if (signature != tex_signature[slot] || tex_dirty[slot] || tex_rescan) {
            /* a new surface is uploaded; a queued or rescanned one only if its pixels changed */
            texture_slot(slot, signature != tex_signature[slot]);
            tex_signature[slot] = signature;
            tex_dirty[slot] = 0;
        }
    }
    tex_rescan = 0;
    /* anything else that changes a texture: one slot a frame by checksum */
    slot = tex_round++ & (TEX_SLOTS - 1);
    if (G32(TEX_SLOT(slot) + 0x10) && G32(TEX_SLOT(slot) + 4))
        texture_slot(slot, 0);
    stat_textures += now() - t0;
}

/* texture_upload 0x41e710(page, w, h, pixels, palette, flags), through a trampoline. */
typedef int32_t(GAMEAPI *upload_fn)(int32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static upload_fn upload_original;

static int32_t GAMEAPI texture_upload(int32_t page, uint32_t w, uint32_t h, uint32_t pixels, uint32_t palette,
                                      uint32_t flags)
{
    int32_t result = upload_original(page, w, h, pixels, palette, flags);
    tex_rescan = 1;
    return result;
}

/* 0x429070 (jmp 0x420090): drains the texture re-upload queue. */
static void GAMEAPI texture_queue_flush(void)
{
    int32_t i;
    for (i = 0; i < TEX_QUEUE_COUNT && i < 0x100; i++)
        if (TEX_QUEUE[i] >= 0 && TEX_QUEUE[i] < TEX_SLOTS)
            tex_dirty[TEX_QUEUE[i]] = 1;
    ((void (*)(void))0x420090)();
}

/* ---- frames ---- */

static void read_state(struct vkr_state *s)
{
    int i;
    for (i = 0; i < 4; i++)
        s->blend_pair[i] = G32(0x4bcd8c + 4 * i);
    s->caps = G32(0x4b86bc) & 0xff;
    s->abr_or = G32(0x6cce60);
    s->zbuffer = G32(0x6cce7c);
    s->zwrite_by_tm = G32(0x6cce80);
    for (i = 0; i < 64; i++)
        s->page_slot[i] = G32(0x6cd300 + 4 * i);
}

/* Fits a game screen of w x h into the window, by height when wider. */
static void fit(int w, int h)
{
    float sx, sy;
    if (w <= 0 || h <= 0) {
        w = 640;
        h = 480;
    }
    sx = (float)win.w / (float)w;
    sy = (float)win.h / (float)h;
    map.scale = sx < sy ? sx : sy;
    map.offset_x = ((float)win.w - (float)w * map.scale) / 2.0f;
    map.offset_y = ((float)win.h - (float)h * map.scale) / 2.0f;
}

static int open_frame(int black)
{
    uint32_t background = G32(0x6ccd8c);
    float clear[4] = {0, 0, 0, 1};

    if (frame_open)
        return 1;
    if (!vkwin_sync(&win))
        return 0;
    if (win.w > 0 && win.h > 0)
        widescreen_set_aspect((float)win.w / (float)win.h);
    vkr_set_anisotropy(win.r, anisotropy);
    textures_update();
    if (!black) {
        clear[0] = (float)(background & 0xff) / 255.0f;
        clear[1] = (float)(background >> 8 & 0xff) / 255.0f;
        clear[2] = (float)(background >> 16 & 0xff) / 255.0f;
    }
    if (!vkr_begin(win.r, win.w, win.h, clear))
        return 0;
    fit(G_SCREEN_W, G_SCREEN_H);
    frame_open = 1;
    frame_walks = 0;
    return 1;
}

/* REROLL_DEBUG_LEAKS: the translucent list (0x6cdd80) must not run into the
 * world list (0x6cd510): finds the packet that links across. */
static void check_leak(uint32_t start)
{
    static int on = -1;
    static uint32_t found;
    uint32_t word = start, steps = 0, hist[8] = {0}, entry = 0;
    if (on < 0)
        on = getenv("REROLL_DEBUG_LEAKS") != 0;
    if (!on || start != 0x6ce57c)
        return;
    while (word && word != 0xffffffffu && steps++ < 200000) {
        uint32_t a = word & 0x7fffffffu, k;
        if (a >= 0x6cd510 && a < 0x6cdd10) {
            if (found == 0) {
                void widescreen_log_pools(void);
                widescreen_log_pools();
            }
            if (found++ < 12) {
                char line[400];
                int len = 0;
                for (k = 0; k < 8; k++) {
                    uint32_t h = hist[(steps + k) & 7];
                    if (h)
                        len += snprintf(line + len, sizeof line - (size_t)len, " %x/%x", h,
                                        h >= 0x10000 ? (uint32_t)((const uint8_t *)h)[7] : 0u);
                }
                log_printf("leak: from list entry %x:%s -> world %x", entry, line, a);
            }
            return;
        }
        if (a >= 0x6cdd80 && a < 0x6ce580)
            entry = a;
        if (!readable(a, 8))
            return;
        hist[steps & 7] = a;
        word = *(const uint32_t *)a;
    }
}

static void draw_walk(rd_read_fn read, void *ctx, uint32_t start)
{
    check_leak(start);
    struct rd_list list;
    struct vkr_state s;
    uint64_t t0;

    if (tex_rescan) /* textures uploaded since the frame began (image tiles) */
        textures_update();
    memset(&list, 0, sizeof list);
    list.prims = prims;
    list.verts = verts;
    list.prim_max = MAX_PRIMS;
    list.vert_max = MAX_VERTS;
    t0 = now();
    rd_read_view(read, ctx, start, &list.view);
    rd_decode(read, ctx, &list);
    read_state(&s);
    vkr_draw(win.r, &list, &s, &map);
    if (list.prim_count >= list.prim_max || list.vert_count + 64 > list.vert_max) {
        static uint32_t full;
        if (full++ % 200 == 0)
            log_printf("vulkan: walk %d from %x is full (%u prims, %u vertices, %u packets; %u times so far)",
                       frame_walks, start, list.prim_count, list.vert_count, list.stats.packets, full);
    }
    if (debug_frames_left > 0 && frame_walks < 24) { /* REROLL_DEBUG_FRAMES */
        uint32_t k, blend = 0;
        for (k = 0; k < list.prim_count; k++)
            blend += (list.prims[k].code & 2) != 0;
        debug_line_len += snprintf(debug_line + debug_line_len, sizeof debug_line - debug_line_len, " %u/%u",
                                   list.prim_count, blend);
        if (debug_frames_left == 1)
            log_printf("frame walk %d starts at %x", frame_walks, start);
    }
    frame_walks++;
    frame_prims += list.prim_count;
    stat_draw += now() - t0;
}

/* A frame with no packets: the back buffer (the loading pictures). */
static void draw_back_buffer(void)
{
    void **surface = G_BACK_BUFFER;
    uint8_t desc[0x6c];
    const uint8_t *pixels;
    struct rd_list list;
    struct vkr_state s;
    int w, h, i;

    if (!lock(surface, desc))
        return;
    pixels = convert(desc, 1, 0, 0);
    w = (int)*(uint32_t *)(desc + 0xc);
    h = (int)*(uint32_t *)(desc + 8);
    unlock(surface);
    if (!pixels)
        return;
    vkr_set_texture(win.r, VKR_IMAGE_SLOT, w, h, pixels);
    /* the game's screen is the top left of a larger buffer (fullscreen) or all of it */
    if (G_SCREEN_W > 0 && G_SCREEN_W < w)
        w = G_SCREEN_W;
    if (G_SCREEN_H > 0 && G_SCREEN_H < h)
        h = G_SCREEN_H;
    fit(w, h);
    memset(&list, 0, sizeof list);
    memset(&s, 0, sizeof s);
    list.view.screen_w = w;
    list.view.screen_h = h;
    list.prims = prims;
    list.verts = verts;
    list.prim_count = 1;
    list.vert_count = 4;
    memset(prims, 0, sizeof *prims);
    prims[0].kind = RD_QUAD;
    prims[0].count = 4;
    prims[0].flags = RD_2D | RD_TEXTURED | RD_IMAGE;
    prims[0].page = 0x3f;
    for (i = 0; i < 4; i++) {
        memset(&verts[i], 0, sizeof verts[i]);
        verts[i].x = (float)((i & 1) * w);
        verts[i].y = (float)((i >> 1) * h);
        verts[i].u = (uint8_t)((i & 1) * 255);
        verts[i].v = (uint8_t)((i >> 1) * 255);
        verts[i].r = verts[i].g = verts[i].b = verts[i].a = 255;
    }
    if (w < *(int32_t *)(desc + 0xc) || h < *(int32_t *)(desc + 8)) {
        /* only part of the image: scale the texel corners (u, v are 0..255 over the whole texture) */
        verts[1].u = verts[3].u = (uint8_t)(255 * w / *(int32_t *)(desc + 0xc));
        verts[2].v = verts[3].v = (uint8_t)(255 * h / *(int32_t *)(desc + 8));
    }
    vkr_draw(win.r, &list, &s, &map);
}

/* Makes the game's window a borderless one of the target size. */
static void take_window(HWND window)
{
    SetWindowLongA(window, GWL_STYLE, (int32_t)(WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN));
    SetWindowPos(window, 0, 0, 0, target_w, target_h, SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    SetForegroundWindow(window);
}

static int start(void)
{
    char value[8];
    HWND window = G_WINDOW;
    int validation = 0, vsync = -1;

    if (!window)
        return 0;
    if (GetEnvironmentVariableA("REROLL_VK_VALIDATE", value, sizeof value) && value[0] == '1')
        validation = 1;
    if (GetEnvironmentVariableA("REROLL_VSYNC", value, sizeof value))
        vsync = value[0] == '1';
    take_window(window);
    ShowCursor(0); /* as the game does in fullscreen */
    prims = malloc(MAX_PRIMS * sizeof *prims);
    verts = malloc(MAX_VERTS * sizeof *verts);
    if (!prims || !verts || !vkwin_open(&win, window, validation, vsync)) {
        log_printf("vulkan: could not start, the original renderer carries on");
        state = -1;
        return 0;
    }
    tex_rescan = 1;
    state = 2;
    {
        struct win_rect rect;
        GetClientRect(window, &rect);
        log_printf("vulkan: window %p (%dx%d), foreground %p", (uint32_t)window, rect.right, rect.bottom,
                   (uint32_t)GetForegroundWindow());
    }
    return 1;
}

int vkgame_walk(uint32_t start_word)
{
    if (state == 1)
        start();
    if (state != 2 || !open_frame(0))
        return state == 2; /* running but nothing to draw to (minimised): drop it */
    draw_walk(memory_read, 0, start_word);
    return 1;
}

int vkgame_prim(uint32_t packet)
{
    struct one_packet one;

    if (state == 1)
        start();
    if (state != 2)
        return 0;
    if (!open_frame(0) || !readable(packet, 8))
        return 1;
    one.table = packet;
    one.address = packet;
    one.size = packet_size((const uint8_t *)packet);
    if (!readable(packet, one.size))
        return 1;
    memcpy(one.copy, (const void *)packet, one.size);
    *(uint32_t *)one.copy = 0xffffffff;
    draw_walk(packet_read, &one, 0x10);
    return 1;
}

/* The Direct3D capability probe 0x41f720 draws test quads, flips and reads
 * the pixels back: its flips stay the original's. Arguments unknown, so the
 * wrapper keeps the caller's stack as it is. */
int vk_probing;
uint32_t vk_probe_return;
void *vk_probe_original;
void vk_probe(void);
__asm__(".text\n"
        ".globl " ASM_SYM("vk_probe") "\n"
        ASM_SYM("vk_probe") ":\n"
        "    incl " ASM_SYM("vk_probing") "\n"
        "    popl " ASM_SYM("vk_probe_return") "\n"
        "    call *" ASM_SYM("vk_probe_original") "\n"
        "    pushl " ASM_SYM("vk_probe_return") "\n"
        "    decl " ASM_SYM("vk_probing") "\n"
        "    ret\n");

int vkgame_flip(void)
{
    static uint32_t presents;
    uint64_t t0;

    if (vk_probing)
        return 0;
    if (state == 1)
        start();
    if (state != 2)
        return 0;
    if (!frame_open || !frame_walks) {
        frame_open = 0;
        if (!open_frame(1))
            return 1;
        draw_back_buffer();
        frame_walks = 0;
    }
    if (presents < 400 && (presents < 3 || presents % 100 == 0))
        log_printf("vulkan: frame %u: %d walks, window %p, foreground %p", presents, frame_walks, (uint32_t)win.parent,
                   (uint32_t)GetForegroundWindow());
    t0 = now();
    vkwin_present(&win);
    widescreen_frame_end();
    stat_present += now() - t0;
    if (debug_frames_left < 0) {
        const char *e = getenv("REROLL_DEBUG_FRAMES"); /* "first,count": per-walk prims/blended of those frames */
        debug_frames_left = 0;
        if (e)
            debug_frames_left = atoi(e) ? -(atoi(e) + 2) : 0;
    }
    if (debug_frames_left < -1 && (int)presents >= -debug_frames_left - 2) {
        const char *e = getenv("REROLL_DEBUG_FRAMES"), *comma = strchr(e, ',');
        debug_frames_left = comma ? atoi(comma + 1) : 300;
    }
    if (debug_frames_left > 0) {
        log_printf("frame %u:%s", presents, debug_line);
        debug_frames_left--;
    }
    debug_line_len = 0;
    debug_line[0] = 0;
    stat_walks += (uint32_t)frame_walks;
    stat_prims += frame_prims;
    if ((uint32_t)frame_walks > stat_max_walks)
        stat_max_walks = (uint32_t)frame_walks;
    if (frame_prims > stat_max_prims)
        stat_max_prims = frame_prims;
    frame_prims = 0;
    frame_open = 0;
    presents++;
#ifdef REROLL_NATIVE
    nt_profile_flush();
#endif
    if (++stat_frames == 1000) {
        uint64_t t = now();
        if (!stat_freq)
            QueryPerformanceFrequency(&stat_freq);
        if (stat_last)
            log_printf("vulkan: %u frames, %d us per frame: draw %d, textures %d (%u uploads), present %d; "
                       "walks %u (max %u), prims %u (max %u) per frame",
                       presents, per_frame_us(t - stat_last), per_frame_us(stat_draw), per_frame_us(stat_textures),
                       stat_uploads, per_frame_us(stat_present), stat_walks / 1000, stat_max_walks, stat_prims / 1000,
                       stat_max_prims);
        stat_last = t;
        stat_frames = 0;
        stat_draw = stat_textures = stat_present = 0;
        stat_uploads = 0;
        stat_walks = stat_prims = stat_max_walks = stat_max_prims = 0;
    }
    return 1;
}

/* The loading pictures (and other images, 0x4203a0) are drawn as tiles of
 * screen-space packets straight into the back end: the call at 0x420582
 * comes here instead. */
void GAMEAPI vkgame_image_tile(uint32_t packet)
{
    if (!vkgame_prim(packet))
        ((void(GAMEAPI *)(uint32_t))0x420be0)(packet);
}

#define image_tile vkgame_image_tile

static void patch_image_tile_call(void)
{
    uint8_t *call = (uint8_t *)0x420582;
    if (call[0] == 0xe9 || call[0] == 0xcc) /* 0x4203a0 is C now (src/game/orig_pa.c calls vkgame_image_tile) */
        return;
    if (call[0] != 0xe8 || *(int32_t *)(call + 1) != 0x420be0 - 0x420587) {
        log_printf("vulkan: unexpected bytes at 0x420582, loading pictures stay on the old path");
        return;
    }
    *(int32_t *)(call + 1) = (int32_t)((uint32_t)image_tile - 0x420587);
}

/* 0x4771c0(class, x, y, w, h, fullscreen): the game's window, created the
 * fullscreen way (a popup) while DirectDraw runs windowed; native/user32.c
 * puts it on the SDL window either way. */
typedef uint32_t(GAMEAPI *window_fn)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
static window_fn window_original;

static uint32_t GAMEAPI create_window(uint32_t class_id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t fullscreen)
{
    (void)fullscreen;
    return window_original(class_id, x, y, w, h, 1);
}

static const struct hook window_hook = {"vk_window", 0x4771c0, create_window,
                                        {0x8b, 0x44, 0x24, 0x08, 0x83, 0xec, 0x10, 0x85}};
static const struct hook upload_hook = {"vk_texture_upload", 0x41e710, texture_upload,
                                        {0x55, 0x8b, 0xec, 0x81, 0xec, 0x88, 0x04, 0x00}};
static const struct hook queue_hook = {"vk_texture_queue", 0x429070, texture_queue_flush,
                                       {0xe9, 0x1b, 0x70, 0xff, 0xff, 0x90, 0x90, 0x90}};
static const struct hook probe_hook = {"vk_probe", 0x41f720, vk_probe, {0x55, 0x8b, 0xec, 0x51, 0x53, 0x56, 0x57, 0x6a}};

/* Reads "WxH". */
static int parse_size(const char *s, int *w, int *h)
{
    int a = 0, b = 0, second = 0;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') {
            if (second)
                b = b * 10 + (*s - '0');
            else
                a = a * 10 + (*s - '0');
        } else if (*s == 'x' || *s == 'X') {
            second = 1;
        }
    }
    if (a < 320 || b < 240 || a > 16384 || b > 16384)
        return 0;
    *w = a;
    *h = b;
    return 1;
}

float vkgame_install(void)
{
    char value[32];

    if (!GetEnvironmentVariableA("REROLL_VULKAN", value, sizeof value) || value[0] != '1')
        return 0.0f;
    if (!GetEnvironmentVariableA("REROLL_RES", value, sizeof value) || !parse_size(value, &target_w, &target_h)) {
        target_w = GetSystemMetrics(SM_CXSCREEN);
        target_h = GetSystemMetrics(SM_CYSCREEN);
    }
    if (target_w < 320 || target_h < 240) {
        target_w = 1024;
        target_h = 768;
    }
#ifdef REROLL_NATIVE
    anisotropy = nt_anisotropy_get();
#endif
    if (GetEnvironmentVariableA("REROLL_ANISOTROPY", value, sizeof value)) {
        int n = 0, i;
        for (i = 0; value[i] >= '0' && value[i] <= '9'; i++)
            n = n * 10 + (value[i] - '0');
        anisotropy = n > 1 ? n : 1;
    }
    /* DirectDraw windowed, as the /W switch does: no display mode change */
    G32(0x4acc5c) = 0;
    G32(0x4acc60) = 0;
    upload_original = (upload_fn)hook_install_trampoline(&upload_hook, 9);
    hook_install(&queue_hook);
    vk_probe_original = hook_install_trampoline(&probe_hook, 7);
    patch_image_tile_call();
    window_original = (window_fn)hook_install_trampoline(&window_hook, 7);
    state = 1;
    log_printf("vulkan: the game window will be %dx%d", target_w, target_h);
    return (float)target_w / (float)target_h;
}

int vkgame_active(void)
{
    return state > 0;
}

/* The native build restarts the game in place (docs/native/plan.md step 7):
 * the data section is fresh, so the windowed-mode flags go back in, and the
 * old run's textures are forgotten. The window and Vulkan carry on. */
void vkgame_restart(void)
{
    if (state == 0)
        return;
    G32(0x4acc5c) = 0;
    G32(0x4acc60) = 0;
    frame_open = 0;
    memset(tex_signature, 0, sizeof tex_signature);
    memset(tex_checksum, 0, sizeof tex_checksum);
    memset(tex_dirty, 0, sizeof tex_dirty);
    tex_rescan = 1;
}

/* Anisotropic filtering samples (1 = off), from the next frame. */
void vkgame_set_anisotropy(int samples)
{
    anisotropy = samples > 1 ? samples : 1;
}

int vkgame_anisotropy(void)
{
    return anisotropy;
}
