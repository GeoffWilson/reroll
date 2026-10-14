/* Frame capture for the Vulkan renderer work (docs/renderer/plan.md, step 1).
 *
 * F9 arms a capture of the next drawn frame. While it runs, every ordering
 * table walk (DrawOTag 0x421860, through its wrappers 0x428470 and 0x428480,
 * the second setting the viewport first) records the
 * table, every packet it reaches and the camera/projection globals at that
 * moment. At the next present the capture adds the whole data section, the
 * texture pages read back from the game's own system-memory texture
 * surfaces, and asks the game for its own screenshot of the same frame
 * (\RollCage Dumps\Dump<n>.bmp) to compare against.
 *
 * File: captures\frame_<pid>_<n>.rcap, a sequence of chunks
 *     u32 tag, u32 length, payload
 * tags (little-endian ASCII):
 *   'RCAP' u32 version, u32 frame number, u32 screen w, u32 screen h
 *   'OTAG' u32 start address, u32 walk number    (starts a walk group)
 *   'REGN' u32 address, then bytes               (memory at the time of the
 *          preceding OTAG, or of the frame end after 'BASE')
 *   'BASE' (empty)                               (frame-end memory follows)
 *   'TEXS' u32 slot, page, width, height, pitch, bit count, r/g/b/a masks,
 *          colour key low, colour key high, then height * pitch bytes
 *   'END ' (empty)
 * tools/rcap.py reads it.
 *
 * Live mode (REROLL_LIVE=1, docs/renderer/plan.md step 4): every drawn
 * frame is recorded the same way into shared memory, /dev/shm/reroll_live
 * (the native layer's Z: drive is /), for the viewer tools/rview.c to
 * render while the game runs. Layout (struct live_header):
 *   0x0000   header
 *   0x1000   64 texture slots of LIVE_TEX_STRIDE: u32 version, the 12-word
 *            'TEXS' head (pitch = width * 2), then the pixels; rewritten
 *            only when a page's contents change
 *   then     3 frame slots of LIVE_FRAME_SIZE, each a capture-format chunk
 *            stream ('RCAP', walks, a small 'BASE', 'END ')
 * Live frames are published at the flip (0x4876f0), which every screen
 * update goes through, present_frame or not (loading screens flip
 * themselves). A flip with no packets drawn (the loading pictures are
 * DirectDraw blits) publishes the back buffer instead, as an 'SCRN' chunk:
 * u32 width, height, pitch, bit count, r/g/b masks, then the pixels.
 * Single packets drawn with DrawPrim 0x428460 (the pause panel, the view
 * border quads) are recorded as one-packet walks: a table at the fake
 * address 0x10 whose only word points at a copy of the packet with its link
 * word set to the end mark.
 * The writer fills a slot that is neither the latest nor the one the reader
 * says it is copying, then publishes it (latest, then frame_seq). */
#include "capture.h"

#include "hook.h"
#include "log.h"
#include "runtime.h"
#include "vkgame.h"

__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE h);
__declspec(dllimport) BOOL WINAPI CreateDirectoryA(const char *name, void *sec);
__declspec(dllimport) short WINAPI GetAsyncKeyState(int key);
__declspec(dllimport) DWORD WINAPI GetCurrentProcessId(void);
__declspec(dllimport) HANDLE WINAPI CreateFileMappingA(HANDLE file, void *sec, DWORD protect, DWORD size_high,
                                                       DWORD size_low, const char *name);
__declspec(dllimport) void *WINAPI MapViewOfFile(HANDLE map, DWORD access, DWORD offset_high, DWORD offset_low,
                                                 size_t size);

#define VK_F9 0x78
#define TAG(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

#define DRAW_OTAG ((void (*)(uint32_t))0x421860)
#define G_SCREEN_W (*(int32_t *)0x4acc38)
#define G_SCREEN_H (*(int32_t *)0x4acc3c)
#define G_PRESENT_PENDING (*(int32_t *)0x4be49c) /* the game's screenshot request */

/* Texture slots (docs/renderer/pipeline.md 3.4): 64 records of 0x24 at
 * 0x4ba6fc, +4 system-memory surface, +0x10 width, +0x14 height, +0x18 page. */
#define TEX_SLOTS 64
#define TEX_SLOT(s) ((uint8_t *)0x4ba6fc + (s) * 0x24)

#define MAX_REGIONS 0x8000

#define LIVE_TEX_STRIDE (0x40 + 256 * 256 * 2)
#define LIVE_FRAME_SIZE 0x1000000
#define LIVE_TEX_OFFSET 0x1000
#define LIVE_FRAME_OFFSET (LIVE_TEX_OFFSET + TEX_SLOTS * LIVE_TEX_STRIDE)
#define LIVE_SIZE (LIVE_FRAME_OFFSET + 3 * LIVE_FRAME_SIZE)

struct live_header {
    uint32_t magic;           /* 'RLIV' */
    uint32_t version;         /* 1 */
    volatile uint32_t frame_seq;    /* bumped after each published frame */
    volatile uint32_t latest;       /* slot of the latest frame */
    volatile uint32_t reading;      /* slot the reader is copying (written by the reader), 3 = none */
    uint32_t frame_size[3];
    uint32_t frame_offset[3];
    uint32_t tex_offset, tex_stride, frame_capacity;
    volatile uint32_t pid;          /* the game process writing */
};

struct region {
    uint32_t start, end;
};

static HANDLE file = INVALID_HANDLE_VALUE;
static int armed, key_down;
static uint32_t frame_count, walk_count;
static struct region regions[MAX_REGIONS];
static int region_count;

static int live_state; /* 0 not checked, 1 on, -1 off */
static uint8_t *live_base;
static uint8_t *live_out; /* the frame slot being filled, or null */
static uint32_t live_len, live_slot, live_walks, live_overflow;
static uint32_t live_checksum[TEX_SLOTS];

static void put(const void *data, uint32_t len)
{
    DWORD written;
    if (file != INVALID_HANDLE_VALUE && len)
        WriteFile(file, data, len, &written, 0);
    if (live_out && len) {
        if (live_len + len <= LIVE_FRAME_SIZE) {
            memcpy(live_out + live_len, data, len);
            live_len += len;
        } else {
            live_overflow = 1;
        }
    }
}

static void chunk(uint32_t tag, uint32_t len)
{
    uint32_t head[2];
    head[0] = tag;
    head[1] = len;
    put(head, 8);
}

static void put_region(uint32_t address, uint32_t len)
{
    chunk(TAG('R', 'E', 'G', 'N'), len + 4);
    put(&address, 4);
    put((const void *)(uintptr_t)address, len);
}

static void put_region_data(uint32_t address, const void *data, uint32_t len)
{
    chunk(TAG('R', 'E', 'G', 'N'), len + 4);
    put(&address, 4);
    put(data, len);
}

/* Bytes a packet occupies, by code >> 2 (pipeline.md 2.4 and 2.5). Unknown
 * codes get the largest size so nothing is cut off. */
static uint32_t packet_size(const uint8_t *packet)
{
    switch (packet[7] >> 2) {
    case 0x23: return 0x84; /* GT4 */
    case 0x27: return 0x64; /* GT3 */
    case 0x26: return 0x78; /* FT4 */
    case 0x25: return 0x60; /* FT3 */
    case 0x2a: return 0x68; /* F4 */
    case 0x2b: return 0x3c; /* line */
    case 0x0b: return 0x28; /* 2D POLY_FT4 */
    case 0x0c: return 0x1c; /* 2D POLY_G3 */
    case 0x19:
    case 0x11: return 0x14; /* SPRT */
    case 0x28:
    case 0x2c:
    case 0x2d: return 0x08; /* fog marker, filter, tpage */
    default: return 0x84;
    }
}

/* Whether [address, address + len) is committed memory. The last region
 * found is cached: packets come from a few large buffers, and live mode
 * walks thousands of them a frame. */
static int readable(uint32_t address, uint32_t len)
{
    static uint32_t cache_start, cache_end;
    struct memory_basic_information info;
    if (address < 0x10000)
        return 0;
    if (address >= cache_start && address + len <= cache_end)
        return 1;
    if (!VirtualQuery((const void *)(uintptr_t)address, &info, sizeof info) || info.state != MEM_COMMIT)
        return 0;
    cache_start = (uint32_t)(uintptr_t)info.base_address;
    cache_end = cache_start + (uint32_t)info.region_size;
    return address + len <= cache_end;
}

static void add_region(uint32_t address, uint32_t len)
{
    if (region_count < MAX_REGIONS && readable(address, len)) {
        regions[region_count].start = address;
        regions[region_count].end = address + len;
        region_count++;
    }
}

/* Sorts (shell sort: no C runtime here), merges and writes the regions. */
static void flush_regions(void)
{
    int gap, i, j, out = 0;
    for (gap = region_count / 2; gap > 0; gap /= 2)
        for (i = gap; i < region_count; i++) {
            struct region r = regions[i];
            for (j = i; j >= gap && regions[j - gap].start > r.start; j -= gap)
                regions[j] = regions[j - gap];
            regions[j] = r;
        }
    for (i = 0; i < region_count; i++) {
        if (out && regions[i].start <= regions[out - 1].end) {
            if (regions[i].end > regions[out - 1].end)
                regions[out - 1].end = regions[i].end;
        } else {
            regions[out++] = regions[i];
        }
    }
    for (i = 0; i < out; i++)
        put_region(regions[i].start, regions[i].end - regions[i].start);
    region_count = 0;
}

/* Records one ordering table walk: the start word, the packets it reaches
 * (following the same links as DrawOTag 0x421860) and the render globals
 * (OTs, GTE and view registers, matrices: 0x6cc000-0x6cf000). */
static void record_walk(uint32_t start)
{
    uint32_t word, head[2];
    int guard = 0x100000;

    head[0] = start;
    head[1] = walk_count++;
    chunk(TAG('O', 'T', 'A', 'G'), 8);
    put(head, 8);
    add_region(0x6cc000, 0x3000);
    add_region(start, 4);
    word = *(uint32_t *)(uintptr_t)start;
    while (word != 0xffffffff && guard-- > 0) {
        uint32_t address = word & 0x7fffffff;
        if (!readable(address, 4))
            break;
        if (word & 0x80000000) {
            add_region(address, 4);
            word = *(uint32_t *)(uintptr_t)address;
        } else {
            add_region(address, readable(address, 8) ? packet_size((const uint8_t *)(uintptr_t)address) : 4);
            word = *(uint32_t *)(uintptr_t)address;
        }
    }
    if (guard <= 0)
        log_printf("capture: ordering table %x loops (a packet linked twice?)", start);
    flush_regions();
}

/* DrawOTag 0x428470(start): records the walk while a capture runs, then
 * draws as the original. */
void GAMEAPI capture_draw_otag(uint32_t start)
{
    if (file != INVALID_HANDLE_VALUE || live_out) {
        record_walk(start);
        live_walks++;
    }
    if (!vkgame_walk(start))
        DRAW_OTAG(start);
}

/* 0x428480(start, rect): the other DrawOTag wrapper (the PS1-style buffer
 * swap 0x423b10 / 0x423ba0 used by the pause screen and some front-end
 * screens): sets the D3D viewport from a view rectangle (s16 x, y, w, h),
 * then walks the table. */
void GAMEAPI capture_draw_otag_rect(uint32_t start, const int16_t *rect)
{
    if (file != INVALID_HANDLE_VALUE || live_out) {
        record_walk(start);
        live_walks++;
    }
    ((void (*)(int32_t, int32_t, int32_t, int32_t))0x41f2d0)(rect[0], rect[1], rect[2], rect[3]);
    if (!vkgame_walk(start))
        DRAW_OTAG(start);
}

/* One packet drawn on its own (DrawPrim): a one-entry walk, see the top. */
static void record_prim(uint32_t packet)
{
    uint32_t head[2], size, fake_table = 0x10;
    uint8_t copy[0x84];

    if (!readable(packet, 8))
        return;
    size = packet_size((const uint8_t *)(uintptr_t)packet);
    if (!readable(packet, size))
        return;
    head[0] = fake_table;
    head[1] = walk_count++;
    chunk(TAG('O', 'T', 'A', 'G'), 8);
    put(head, 8);
    put_region(0x6cc000, 0x3000);
    put_region_data(fake_table, &packet, 4);
    memcpy(copy, (const void *)(uintptr_t)packet, size);
    *(uint32_t *)copy = 0xffffffff;
    put_region_data(packet, copy, size);
}

/* DrawPrim 0x428460(packet): draws one packet (0x420be0). */
void GAMEAPI capture_draw_prim(uint32_t packet)
{
    if (file != INVALID_HANDLE_VALUE || live_out) {
        record_prim(packet);
        live_walks++;
    }
    if (!vkgame_prim(packet))
        ((void (*)(uint32_t))0x420be0)(packet);
}

/* Reads one texture slot's system-memory surface back (IDirectDrawSurface:
 * GetColorKey +0x40, Lock +0x64, Unlock +0x80). */
static void record_texture(int slot)
{
    uint8_t *rec = TEX_SLOT(slot);
    void **surface = (void **)(uintptr_t)*(uint32_t *)(rec + 4);
    uint8_t desc[0x6c];
    uint32_t key[2] = {0, 0}, head[12], y, height, pitch;
    int i;
    int32_t(__stdcall * get_color_key)(void *, DWORD, uint32_t *);
    int32_t(__stdcall * lock)(void *, void *, uint8_t *, DWORD, void *);
    int32_t(__stdcall * unlock)(void *, void *);

    if (!surface || !readable((uint32_t)(uintptr_t)surface, 4))
        return;
    get_color_key = (int32_t(__stdcall *)(void *, DWORD, uint32_t *))((void **)*surface)[0x40 / 4];
    lock = (int32_t(__stdcall *)(void *, void *, uint8_t *, DWORD, void *))((void **)*surface)[0x64 / 4];
    unlock = (int32_t(__stdcall *)(void *, void *))((void **)*surface)[0x80 / 4];
    get_color_key(surface, 8 /* DDCKEY_SRCBLT */, key);
    for (i = 0; i < (int)sizeof desc; i++)
        desc[i] = 0;
    *(uint32_t *)desc = sizeof desc;
    if (lock(surface, 0, desc, 1 | 0x10 /* DDLOCK_WAIT | DDLOCK_READONLY */, 0) != 0) {
        log_printf("capture: texture slot %d: Lock failed", slot);
        return;
    }
    height = *(uint32_t *)(desc + 8);
    pitch = *(uint32_t *)(desc + 0x10);
    head[0] = (uint32_t)slot;
    head[1] = *(uint32_t *)(rec + 0x18);
    head[2] = *(uint32_t *)(desc + 0xc);
    head[3] = height;
    head[4] = pitch;
    head[5] = *(uint32_t *)(desc + 0x54);
    head[6] = *(uint32_t *)(desc + 0x58);
    head[7] = *(uint32_t *)(desc + 0x5c);
    head[8] = *(uint32_t *)(desc + 0x60);
    head[9] = *(uint32_t *)(desc + 0x64);
    head[10] = key[0];
    head[11] = key[1];
    chunk(TAG('T', 'E', 'X', 'S'), sizeof head + height * pitch);
    put(head, sizeof head);
    for (y = 0; y < height; y++)
        put(((uint8_t *)(uintptr_t)*(uint32_t *)(desc + 0x24)) + y * pitch, pitch);
    unlock(surface, 0);
}

static void begin(void)
{
    char name[64] = "captures\\frame_";
    uint32_t head[4], values[2], v, i;
    char *p = name + 15;

    CreateDirectoryA("captures", 0);
    values[0] = GetCurrentProcessId();
    values[1] = frame_count;
    for (i = 0; i < 2; i++) {
        char digits[12];
        int n = 0;
        v = values[i];
        do {
            digits[n++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        while (n)
            *p++ = digits[--n];
        *p++ = i ? '.' : '_';
    }
    for (i = 0; i < 5; i++)
        *p++ = "rcap"[i];
    file = CreateFileA(name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        log_printf("capture: cannot create %s", name);
        return;
    }
    log_printf("capture: recording %s", name);
    walk_count = 0;
    head[0] = 1;
    head[1] = frame_count++;
    head[2] = (uint32_t)G_SCREEN_W;
    head[3] = (uint32_t)G_SCREEN_H;
    chunk(TAG('R', 'C', 'A', 'P'), sizeof head);
    put(head, sizeof head);
}

static void finish(void)
{
    int slot;
    uint8_t *live = live_out;

    live_out = 0; /* the whole data section and the textures go to the file only */
    chunk(TAG('B', 'A', 'S', 'E'), 0);
    put_region(0x4a7000, 0x6d3000 - 0x4a7000);
    for (slot = 0; slot < TEX_SLOTS; slot++)
        if (*(uint32_t *)(TEX_SLOT(slot) + 0x10))
            record_texture(slot);
    chunk(TAG('E', 'N', 'D', ' '), 0);
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    log_printf("capture: done, %u ordering table walks", walk_count);
    G_PRESENT_PENDING = 1; /* the game's own screenshot of this frame */
    live_out = live;
}

/* ---- live mode ---- */

static void live_init(void)
{
    char value[8];
    HANDLE f, map;
    struct live_header *h;

    live_state = -1;
    if (!GetEnvironmentVariableA("REROLL_LIVE", value, sizeof value) || value[0] != '1')
        return;
    f = CreateFileA("Z:\\dev\\shm\\reroll_live", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (f == INVALID_HANDLE_VALUE) {
        log_printf("live: cannot open /dev/shm/reroll_live");
        return;
    }
    map = CreateFileMappingA(f, 0, PAGE_READWRITE, 0, LIVE_SIZE, 0);
    live_base = map ? MapViewOfFile(map, 2 /* FILE_MAP_WRITE */, 0, 0, LIVE_SIZE) : 0;
    if (!live_base) {
        log_printf("live: cannot map /dev/shm/reroll_live");
        return;
    }
    h = (struct live_header *)live_base;
    h->magic = TAG('R', 'L', 'I', 'V');
    h->version = 1;
    h->latest = 3;
    h->reading = 3;
    h->frame_offset[0] = LIVE_FRAME_OFFSET;
    h->frame_offset[1] = LIVE_FRAME_OFFSET + LIVE_FRAME_SIZE;
    h->frame_offset[2] = LIVE_FRAME_OFFSET + 2 * LIVE_FRAME_SIZE;
    h->tex_offset = LIVE_TEX_OFFSET;
    h->tex_stride = LIVE_TEX_STRIDE;
    h->frame_capacity = LIVE_FRAME_SIZE;
    h->pid = GetCurrentProcessId();
    memset(live_checksum, 0, sizeof live_checksum);
    live_state = 1;
    log_printf("live: publishing frames to /dev/shm/reroll_live");
}

/* Copies a texture slot to the shared memory when its contents changed. */
static void live_texture(int slot)
{
    uint8_t *rec = TEX_SLOT(slot), *out = live_base + LIVE_TEX_OFFSET + slot * LIVE_TEX_STRIDE;
    void **surface = (void **)(uintptr_t)*(uint32_t *)(rec + 4);
    uint8_t desc[0x6c];
    uint32_t key[2] = {0, 0}, *head = (uint32_t *)(out + 4), w, h, pitch, y, x, sum = 2166136261u;
    const uint8_t *pixels;
    int32_t(__stdcall * get_color_key)(void *, DWORD, uint32_t *);
    int32_t(__stdcall * lock)(void *, void *, uint8_t *, DWORD, void *);
    int32_t(__stdcall * unlock)(void *, void *);

    if (!surface || !readable((uint32_t)(uintptr_t)surface, 4))
        return;
    get_color_key = (int32_t(__stdcall *)(void *, DWORD, uint32_t *))((void **)*surface)[0x40 / 4];
    lock = (int32_t(__stdcall *)(void *, void *, uint8_t *, DWORD, void *))((void **)*surface)[0x64 / 4];
    unlock = (int32_t(__stdcall *)(void *, void *))((void **)*surface)[0x80 / 4];
    memset(desc, 0, sizeof desc);
    *(uint32_t *)desc = sizeof desc;
    if (lock(surface, 0, desc, 1 | 0x10, 0) != 0)
        return;
    w = *(uint32_t *)(desc + 0xc);
    h = *(uint32_t *)(desc + 8);
    pitch = *(uint32_t *)(desc + 0x10);
    pixels = (const uint8_t *)(uintptr_t)*(const uint32_t *)(desc + 0x24);
    if (*(uint32_t *)(desc + 0x54) != 16 || w > 256 || h > 256) {
        unlock(surface, 0);
        return;
    }
    for (y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)(pixels + y * pitch);
        for (x = 0; x < w / 2; x++)
            sum = (sum ^ row[x]) * 16777619u;
    }
    sum ^= w << 16 ^ h ^ (uint32_t)slot << 24;
    if (sum != live_checksum[slot]) {
        get_color_key(surface, 8, key);
        live_checksum[slot] = sum;
        head[0] = (uint32_t)slot;
        head[1] = *(uint32_t *)(rec + 0x18);
        head[2] = w;
        head[3] = h;
        head[4] = w * 2;
        head[5] = 16;
        head[6] = *(uint32_t *)(desc + 0x58);
        head[7] = *(uint32_t *)(desc + 0x5c);
        head[8] = *(uint32_t *)(desc + 0x60);
        head[9] = *(uint32_t *)(desc + 0x64);
        head[10] = key[0];
        head[11] = key[1];
        for (y = 0; y < h; y++)
            memcpy(out + 0x40 + y * w * 2, pixels + y * pitch, w * 2);
        ++*(volatile uint32_t *)out; /* version, after the data */
    }
    unlock(surface, 0);
}

/* The back buffer (*0x4d5f4c, IDirectDrawSurface Lock +0x64 / Unlock +0x80)
 * as an 'SCRN' chunk, for screens drawn without packets. */
static void live_screen(void)
{
    void **surface = (void **)(uintptr_t)*(uint32_t *)0x4d5f4c;
    uint8_t desc[0x6c];
    uint32_t head[7], y, w, h, pitch, bytes;
    int32_t(__stdcall * lock)(void *, void *, uint8_t *, DWORD, void *);
    int32_t(__stdcall * unlock)(void *, void *);

    if (!surface || !readable((uint32_t)(uintptr_t)surface, 4))
        return;
    lock = (int32_t(__stdcall *)(void *, void *, uint8_t *, DWORD, void *))((void **)*surface)[0x64 / 4];
    unlock = (int32_t(__stdcall *)(void *, void *))((void **)*surface)[0x80 / 4];
    memset(desc, 0, sizeof desc);
    *(uint32_t *)desc = sizeof desc;
    if (lock(surface, 0, desc, 1 | 0x10, 0) != 0)
        return;
    w = *(uint32_t *)(desc + 0xc);
    h = *(uint32_t *)(desc + 8);
    pitch = *(uint32_t *)(desc + 0x10);
    bytes = *(uint32_t *)(desc + 0x54) / 8;
    head[0] = w;
    head[1] = h;
    head[2] = w * bytes;
    head[3] = *(uint32_t *)(desc + 0x54);
    head[4] = *(uint32_t *)(desc + 0x58);
    head[5] = *(uint32_t *)(desc + 0x5c);
    head[6] = *(uint32_t *)(desc + 0x60);
    if (bytes && w <= 2048 && h <= 2048) {
        chunk(TAG('S', 'C', 'R', 'N'), sizeof head + h * w * bytes);
        put(head, sizeof head);
        for (y = 0; y < h; y++)
            put(((uint8_t *)(uintptr_t)*(uint32_t *)(desc + 0x24)) + y * pitch, w * bytes);
    }
    unlock(surface, 0);
}

/* At each flip: publishes the live frame recorded since the last one and
 * starts the next. */
static void live_present(int drawn)
{
    struct live_header *h = (struct live_header *)live_base;
    uint32_t head[4];
    int slot;

    if (live_out && drawn && !live_walks)
        live_screen();
    if (live_out && drawn) {
        chunk(TAG('B', 'A', 'S', 'E'), 0);
        put_region(0x4acc38, 8);   /* screen size */
        put_region(0x4b86bc, 4);   /* blend capabilities */
        put_region(0x4bcd8c, 16);  /* blend pairs */
        put_region(0x6cc000, 0x3000); /* page map, z-buffer flags, clear colour, ... */
        chunk(TAG('E', 'N', 'D', ' '), 0);
        if (!live_overflow) {
            h->frame_size[live_slot] = live_len;
            h->latest = live_slot;
            __asm__ volatile("" ::: "memory");
            h->frame_seq++;
        }
        live_out = 0;
    }
    if (live_out)
        return; /* nothing drawn yet: keep recording into the same frame */
    for (slot = 0; slot < TEX_SLOTS; slot++)
        if (*(uint32_t *)(TEX_SLOT(slot) + 0x10))
            live_texture(slot);
    for (live_slot = 0; live_slot < 3; live_slot++)
        if (live_slot != h->latest && live_slot != h->reading)
            break;
    if (live_slot == 3)
        live_slot = 0;
    live_out = live_base + h->frame_offset[live_slot];
    live_len = 0;
    live_walks = 0;
    live_overflow = 0;
    head[0] = 1;
    head[1] = h->frame_seq;
    head[2] = (uint32_t)G_SCREEN_W;
    head[3] = (uint32_t)G_SCREEN_H;
    chunk(TAG('R', 'C', 'A', 'P'), sizeof head);
    put(head, sizeof head);
}

/* Called at the start of every present: ends a capture whose frame has been
 * drawn, and arms one on an F9 press. */
void capture_present(int drawn)
{
    int down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;


    if (file != INVALID_HANDLE_VALUE && drawn && walk_count)
        finish();
    if (down && !key_down)
        armed = 1;
    key_down = down;
    if (armed && file == INVALID_HANDLE_VALUE) {
        armed = 0;
        begin();
    }
}

/* The flip 0x4876f0: live frames end here; the original's body is
 * flip_body (src/game/orig_wc.c). */
int32_t GAMEAPI flip_body(void); /* 0x4876f0, src/game/orig_wc.c (capture.c does not include game/physics.h) */

static uint32_t GAMEAPI capture_flip(void)
{
    static uint32_t flips;
    if (++flips % 500 == 0)
        log_printf("flip %u", flips);
    if (!live_state)
        live_init();
    if (live_state > 0)
        live_present(1);
    if (vkgame_flip())
        return 0; /* DD_OK: presented with Vulkan */
    return (uint32_t)flip_body();
}

static const struct hook flip_hook = {"flip", 0x4876f0, capture_flip, {0xa1, 0x80, 0x5f, 0x4d, 0x00, 0x83, 0xec, 0x10}};

void capture_install(void)
{
    hook_install(&flip_hook);
}
