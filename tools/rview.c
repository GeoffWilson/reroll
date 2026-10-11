/* Live viewer (docs/renderer/plan.md, step 4): renders the frames the game
 * publishes to /dev/shm/reroll_live (src/capture.c, REROLL_LIVE=1) with the
 * Vulkan back end (src/render/vkr.c) in its own window, next to the
 * original.
 *
 *     build/rview [--validate] [--test N]
 *
 * --test N exits after presenting N frames (for checking the viewer with
 * tools/rlive_feed.py instead of the game).
 * The window can be any size: the game's frame is fitted by height
 * (pillarboxed on wider windows; widening the game's view is step 5). Its
 * title shows the frame rate and the game's frame number. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "rcapfile.h"
#include "render/vkr.h"

#define SHM_PATH "/dev/shm/reroll_live"
#define TEX_SLOTS 64

struct live_header {
    uint32_t magic, version;
    volatile uint32_t frame_seq, latest, reading;
    uint32_t frame_size[3], frame_offset[3];
    uint32_t tex_offset, tex_stride, frame_capacity;
    volatile uint32_t pid;
};

static uint8_t *map_shared(size_t *size)
{
    struct stat st;
    int fd = open(SHM_PATH, O_RDWR);
    void *p;

    if (fd < 0)
        return 0;
    if (fstat(fd, &st) || st.st_size < 0x1000) {
        close(fd);
        return 0;
    }
    p = mmap(0, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED)
        return 0;
    *size = (size_t)st.st_size;
    return p;
}

int main(int argc, char **argv)
{
    int validate = 0, test_frames = 0, presented = 0, a;
    SDL_Window *window;
    VkSurfaceKHR surface;
    Uint32 extension_count;
    const char *const *extensions;
    struct vkr *r;
    uint8_t *shared = 0, *frame = 0;
    size_t shared_size = 0;
    uint32_t last_seq = 0, tex_versions[TEX_SLOTS] = {0}, frames = 0, game_frame = 0, last_pid = 0;
    int width, height, running = 1, resized = 0, have_frame = 0;
    Uint64 last_title = 0;
    static struct rd_prim prims[0x10000];
    static struct rd_vertex verts[0x40000];
    struct capture cap;

    for (a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "--validate"))
            validate = 1;
        else if (!strcmp(argv[a], "--test") && a + 1 < argc)
            test_frames = atoi(argv[++a]);
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    {
        /* open at the widescreen aspect the game culls for (REROLL_ASPECT), if set */
        const char *aspect_env = getenv("REROLL_ASPECT");
        int window_w = 1440;
        if (aspect_env) {
            float a = 0, b = 0;
            if (sscanf(aspect_env, "%fx%f", &a, &b) == 2 || sscanf(aspect_env, "%f:%f", &a, &b) == 2)
                a = b > 0 ? a / b : 0;
            if (a > 1.34f && a < 4.0f)
                window_w = (int)(1080 * a + 0.5f);
        }
        window = SDL_CreateWindow("reroll live", window_w, 1080, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    }
    if (!window) {
        fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }
    extensions = SDL_Vulkan_GetInstanceExtensions(&extension_count);
    r = vkr_create((PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr(), validate, extensions,
                   extension_count);
    if (!r || !SDL_Vulkan_CreateSurface(window, vkr_instance(r), 0, &surface)) {
        fprintf(stderr, "Vulkan initialisation failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GetWindowSizeInPixels(window, &width, &height);
    vkr_set_surface(r, surface, width, height);
    memset(&cap, 0, sizeof cap);

    while (running) {
        SDL_Event event;
        struct live_header *h;
        int new_frame = 0;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = 0;
            else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
                resized = 1;
        }
        if (resized) {
            SDL_GetWindowSizeInPixels(window, &width, &height);
            if (width > 0 && height > 0)
                vkr_set_surface(r, surface, width, height);
            resized = 0;
        }
        if (!shared)
            shared = map_shared(&shared_size);
        h = (struct live_header *)shared;
        if (h && h->magic == 0x56494c52 /* RLIV */ && h->frame_seq != last_seq && h->latest < 3 &&
            h->frame_offset[2] + h->frame_capacity <= shared_size) {
            uint32_t slot = h->latest, size;
            int i;

            if (h->pid != last_pid) { /* a new game process: its textures start again */
                last_pid = h->pid;
                memset(tex_versions, 0, sizeof tex_versions);
            }
            h->reading = slot;
            __sync_synchronize();
            slot = h->latest; /* the writer may have published again meanwhile */
            h->reading = slot;
            __sync_synchronize();
            size = h->frame_size[slot];
            last_seq = h->frame_seq;
            frame = realloc(frame, size ? size : 1);
            memcpy(frame, shared + h->frame_offset[slot], size);
            h->reading = 3;
            capture_free(&cap);
            new_frame = capture_parse(frame, size, &cap);
            for (i = 0; i < TEX_SLOTS; i++) {
                const uint8_t *t = shared + h->tex_offset + (size_t)i * h->tex_stride;
                uint32_t version = *(const volatile uint32_t *)t;
                if (version && version != tex_versions[i]) {
                    const uint32_t *head = (const uint32_t *)(t + 4);
                    uint8_t *rgba = capture_texture_rgba(head, t + 0x40);
                    vkr_set_texture(r, (int)head[0], (int)head[2], (int)head[3], rgba);
                    free(rgba);
                    tex_versions[i] = version;
                }
            }
            have_frame |= new_frame;
            game_frame = cap.header[1];
        }
        if (new_frame) {
            struct vkr_state state;
            struct vkr_mapping map;
            float clear[4] = {0, 0, 0, 1}, sx, sy;
            uint32_t background = capture_u32(&cap, 0x6ccd8c);
            int game_w = (int)cap.header[2], game_h = (int)cap.header[3], i;

            state.blend_pair[0] = capture_u32(&cap, 0x4bcd8c);
            state.blend_pair[1] = capture_u32(&cap, 0x4bcd90);
            state.blend_pair[2] = capture_u32(&cap, 0x4bcd94);
            state.blend_pair[3] = capture_u32(&cap, 0x4bcd98);
            state.caps = capture_u32(&cap, 0x4b86bc) & 0xff;
            state.abr_or = capture_u32(&cap, 0x6cce60);
            state.zbuffer = capture_u32(&cap, 0x6cce7c);
            state.zwrite_by_tm = capture_u32(&cap, 0x6cce80);
            for (i = 0; i < 64; i++)
                state.page_slot[i] = capture_u32(&cap, 0x6cd300 + 4 * (uint32_t)i);
            clear[0] = (float)(background & 0xff) / 255.0f;
            clear[1] = (float)((background >> 8) & 0xff) / 255.0f;
            clear[2] = (float)((background >> 16) & 0xff) / 255.0f;
            if (game_w <= 0 || game_h <= 0) {
                game_w = 640;
                game_h = 480;
            }
            sx = (float)width / (float)game_w;
            sy = (float)height / (float)game_h;
            map.scale = sx < sy ? sx : sy;
            map.offset_x = ((float)width - (float)game_w * map.scale) / 2.0f;
            map.offset_y = ((float)height - (float)game_h * map.scale) / 2.0f;

            if (!cap.walk_count && cap.screen) { /* drawn without packets (loading pictures): the game's back buffer */
                uint32_t head[12], sw = cap.screen[0], sh = cap.screen[1];
                uint8_t *rgba;
                struct rd_list list;
                struct rd_vertex *v = verts;
                head[0] = VKR_IMAGE_SLOT;
                head[1] = 0;
                head[2] = sw;
                head[3] = sh;
                head[4] = cap.screen[2];
                head[5] = cap.screen[3];
                head[6] = cap.screen[4];
                head[7] = cap.screen[5];
                head[8] = cap.screen[6];
                head[9] = 0;
                head[10] = 1; /* no colour key */
                head[11] = 0;
                rgba = capture_texture_rgba(head, (const uint8_t *)(cap.screen + 7));
                vkr_set_texture(r, VKR_IMAGE_SLOT, (int)sw, (int)sh, rgba);
                free(rgba);
                memset(&list, 0, sizeof list);
                list.view.screen_w = (int32_t)sw;
                list.view.screen_h = (int32_t)sh;
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
                    memset(&v[i], 0, sizeof v[i]);
                    v[i].x = (float)((i & 1) * sw);
                    v[i].y = (float)((i >> 1) * sh);
                    v[i].u = (uint8_t)((i & 1) * 255);
                    v[i].v = (uint8_t)((i >> 1) * 255);
                    v[i].r = v[i].g = v[i].b = v[i].a = 255;
                }
                game_w = (int)sw;
                game_h = (int)sh;
                sx = (float)width / (float)game_w;
                sy = (float)height / (float)game_h;
                map.scale = sx < sy ? sx : sy;
                map.offset_x = ((float)width - (float)game_w * map.scale) / 2.0f;
                map.offset_y = ((float)height - (float)game_h * map.scale) / 2.0f;
                state.zbuffer = 0;
                clear[0] = clear[1] = clear[2] = 0.0f;
                vkr_begin(r, width, height, clear);
                vkr_draw(r, &list, &state, &map);
            } else {
            vkr_begin(r, width, height, clear);
            for (i = 0; i < cap.walk_count; i++) {
                struct reader reader = {&cap, &cap.walks[i]};
                struct rd_list list;
                memset(&list, 0, sizeof list);
                list.prims = prims;
                list.verts = verts;
                list.prim_max = sizeof prims / sizeof *prims;
                list.vert_max = sizeof verts / sizeof *verts;
                rd_read_view(capture_read, &reader, cap.walks[i].start, &list.view);
                rd_decode(capture_read, &reader, &list);
                vkr_draw(r, &list, &state, &map);
            }
            }
            if (!vkr_present(r))
                resized = 1;
            frames++;
            if (test_frames && ++presented >= test_frames) {
                printf("presented %d frames (%d walks in the last), window %dx%d\n", presented, cap.walk_count, width,
                       height);
                running = 0;
            }
        } else {
            SDL_Delay(1);
        }
        if (SDL_GetTicks() - last_title >= 1000) {
            char title[128];
            snprintf(title, sizeof title, have_frame ? "reroll live: %u fps, game frame %u, %dx%d"
                                                     : "reroll live: waiting for the game (REROLL_LIVE=1)",
                     frames, game_frame, width, height);
            SDL_SetWindowTitle(window, title);
            frames = 0;
            last_title = SDL_GetTicks();
        }
    }
    capture_free(&cap);
    vkr_destroy(r);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
