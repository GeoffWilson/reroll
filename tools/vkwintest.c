/* Windows test of the DLL's renderer path (docs/renderer/plan.md, step 6),
 * built freestanding like the DLL and run under Wine without the game:
 *
 *     wine build/vkwintest.exe CAPTURE.rcap OUT.ppm [child|parent|auto [VSYNC]]
 *
 * It renders the capture offscreen at the game's size to OUT.ppm (compare
 * with build/rcaprender's PNG of the same capture: tools/vkwincompare.py),
 * then presents it 240 times to a window through vulkan-1.dll and the
 * child window (src/render/vkwin.c), and logs the results to reroll.log in
 * the current directory. */
#include "../src/log.h"
#include "../src/render/vkwin.h"
#include "../src/win32.h"
#include "rcapfile.h"

#include <stdlib.h>

__declspec(dllimport) char *WINAPI GetCommandLineA(void);
__declspec(dllimport) DWORD WINAPI GetFileSize(HANDLE file, DWORD *high);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE file, void *buf, DWORD len, DWORD *read, void *ovl);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE h);
__declspec(dllimport) void WINAPI ExitProcess(unsigned int code);

static LRESULT WINAPI top_proc(HWND window, unsigned int message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

/* argv from the command line (no quotes in our arguments) */
static int split(char *line, char **argv, int max)
{
    int argc = 0;
    while (*line && argc < max) {
        while (*line == ' ')
            line++;
        if (!*line)
            break;
        argv[argc++] = line;
        while (*line && *line != ' ')
            line++;
        if (*line)
            *line++ = 0;
    }
    return argc;
}

static uint8_t *read_file(const char *path, uint32_t *size)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, 3 /* OPEN_EXISTING */, FILE_ATTRIBUTE_NORMAL, 0);
    uint8_t *data;
    DWORD got = 0;

    if (f == INVALID_HANDLE_VALUE)
        return 0;
    *size = GetFileSize(f, 0);
    data = malloc(*size);
    if (!data || !ReadFile(f, data, *size, &got, 0) || got != *size) {
        CloseHandle(f);
        return 0;
    }
    CloseHandle(f);
    return data;
}

static void write_ppm(const char *path, int w, int h, const uint8_t *rgba)
{
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    char head[32] = "P6\n";
    char *p = head + 3;
    int values[2] = {w, h}, i, n;
    uint8_t *rgb = malloc((size_t)w * h * 3);
    DWORD written;

    for (i = 0; i < 2; i++) {
        char digits[12];
        int v = values[i];
        n = 0;
        do {
            digits[n++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        while (n)
            *p++ = digits[--n];
        *p++ = i ? '\n' : ' ';
    }
    memcpy(p, "255\n", 4);
    p += 4;
    for (i = 0; i < w * h; i++) {
        rgb[i * 3] = rgba[i * 4];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    WriteFile(f, head, (DWORD)(p - head), &written, 0);
    WriteFile(f, rgb, (DWORD)(w * h * 3), &written, 0);
    CloseHandle(f);
    free(rgb);
}

static void draw_capture(struct vkr *r, struct capture *cap, int width, int height, struct rd_prim *prims,
                         struct rd_vertex *verts)
{
    struct vkr_state state;
    struct vkr_mapping map;
    float clear[4] = {0, 0, 0, 1}, sx, sy;
    uint32_t background = capture_u32(cap, 0x6ccd8c);
    int game_w = (int)cap->header[2], game_h = (int)cap->header[3], i;

    for (i = 0; i < 4; i++)
        state.blend_pair[i] = capture_u32(cap, 0x4bcd8c + 4 * (uint32_t)i);
    state.caps = capture_u32(cap, 0x4b86bc) & 0xff;
    state.abr_or = capture_u32(cap, 0x6cce60);
    state.zbuffer = capture_u32(cap, 0x6cce7c);
    state.zwrite_by_tm = capture_u32(cap, 0x6cce80);
    for (i = 0; i < 64; i++)
        state.page_slot[i] = capture_u32(cap, 0x6cd300 + 4 * (uint32_t)i);
    sx = (float)width / (float)game_w;
    sy = (float)height / (float)game_h;
    map.scale = sx < sy ? sx : sy;
    map.offset_x = ((float)width - (float)game_w * map.scale) / 2.0f;
    map.offset_y = ((float)height - (float)game_h * map.scale) / 2.0f;
    clear[0] = (float)(background & 0xff) / 255.0f;
    clear[1] = (float)(background >> 8 & 0xff) / 255.0f;
    clear[2] = (float)(background >> 16 & 0xff) / 255.0f;
    vkr_begin(r, width, height, clear);
    for (i = 0; i < cap->walk_count; i++) {
        struct reader reader = {cap, &cap->walks[i]};
        struct rd_list list;
        memset(&list, 0, sizeof list);
        list.prims = prims;
        list.verts = verts;
        list.prim_max = 0x10000;
        list.vert_max = 0x40000;
        rd_read_view(capture_read, &reader, cap->walks[i].start, &list.view);
        rd_decode(capture_read, &reader, &list);
        vkr_draw(r, &list, &state, &map);
    }
}

void WINAPI start(void)
{
    char *argv[8];
    int argc = split(GetCommandLineA(), argv, 8), child = -1, i, presented = 0;
    uint32_t size = 0;
    uint8_t *data, *pixels;
    struct capture cap;
    struct vkwin win;
    struct win_class c;
    struct win_msg msg;
    HWND top;
    uint64_t t0, t1, freq;
    struct rd_prim *prims = malloc(0x10000 * sizeof *prims);
    struct rd_vertex *verts = malloc(0x40000 * sizeof *verts);

    log_init();
    if (argc < 3) {
        log_printf("vkwintest: usage: vkwintest.exe CAPTURE OUT.ppm [child|parent|auto [VSYNC]]");
        ExitProcess(2);
    }
    if (argc > 3)
        child = argv[3][0] == 'c' ? 1 : argv[3][0] == 'p' ? 0 : -1;
    memset(&cap, 0, sizeof cap);
    data = read_file(argv[1], &size);
    if (!data || !capture_parse(data, size, &cap)) {
        log_printf("vkwintest: cannot read %s", argv[1]);
        ExitProcess(1);
    }
    memset(&c, 0, sizeof c);
    c.proc = top_proc;
    c.instance = GetModuleHandleA(0);
    c.class_name = "vkwintest";
    RegisterClassA(&c);
    top = CreateWindowExA(0, "vkwintest", "vkwintest", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 1296,
                          759, 0, 0, c.instance, 0);
    if (!top || !vkwin_open(&win, top, child, 0, argc > 4 ? argv[4][0] - '0' : -1)) {
        log_printf("vkwintest: window or Vulkan failed");
        ExitProcess(1);
    }
    for (i = 0; i < cap.texture_count; i++) {
        uint8_t *rgba = capture_texture_rgba(cap.textures[i].head, cap.textures[i].pixels);
        vkr_set_texture(win.r, (int)cap.textures[i].head[0], (int)cap.textures[i].head[2],
                        (int)cap.textures[i].head[3], rgba);
        free(rgba);
    }

    /* offscreen at the game's size */
    draw_capture(win.r, &cap, (int)cap.header[2], (int)cap.header[3], prims, verts);
    pixels = malloc(cap.header[2] * cap.header[3] * 4);
    if (!vkr_end(win.r, pixels)) {
        log_printf("vkwintest: offscreen render failed");
        ExitProcess(1);
    }
    write_ppm(argv[2], (int)cap.header[2], (int)cap.header[3], pixels);
    log_printf("vkwintest: wrote %s (%ux%u, %d walks)", argv[2], cap.header[2], cap.header[3], cap.walk_count);

    /* to the window */
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    for (i = 0; i < 240; i++) {
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!vkwin_sync(&win))
            continue;
        draw_capture(win.r, &cap, win.w, win.h, prims, verts);
        presented += vkwin_present(&win);
    }
    QueryPerformanceCounter(&t1);
    log_printf("vkwintest: presented %d of 240 frames at %dx%d (%s), %d us per frame", presented, win.w, win.h,
               win.window != top ? "child" : "parent", (int32_t)((float)(int32_t)(t1 - t0) / (float)(int32_t)freq * (1e6f / 240.0f)));
    vkwin_close(&win);
    ExitProcess(presented > 200 ? 0 : 1);
}
