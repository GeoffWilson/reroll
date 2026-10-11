/* Renders a frame capture with the Vulkan back end (src/render/vkr.c) into a
 * PNG (docs/renderer/plan.md, step 3).
 *
 *     build/rcaprender FILE OUT.png [WIDTH HEIGHT] [--validate]
 *
 * Without a size it renders at the game's own screen size, to compare with
 * the game's screenshot. A different size keeps the captured frame's field
 * of view and fits it by height (pillarboxed on wider screens): the frame
 * was culled for the game's 4:3 view, so widening the view comes with step 5. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "rcapfile.h"
#include "render/vkr.h"

PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *name);

static void put32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    fwrite(b, 1, 4, f);
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint32_t crc = (uint32_t)crc32(0, (const Bytef *)type, 4);
    put32(f, len);
    fwrite(type, 1, 4, f);
    if (len) {
        fwrite(data, 1, len, f);
        crc = (uint32_t)crc32(crc, data, len);
    }
    put32(f, crc);
}

static int write_png(const char *path, int w, int h, const uint8_t *rgba)
{
    FILE *f = fopen(path, "wb");
    size_t raw_size = (size_t)h * ((size_t)w * 4 + 1);
    uint8_t *raw = malloc(raw_size), header[13];
    uLongf packed_size = compressBound((uLong)raw_size);
    uint8_t *packed = malloc(packed_size);
    int y;

    if (!f)
        return 0;
    for (y = 0; y < h; y++) {
        raw[(size_t)y * ((size_t)w * 4 + 1)] = 0;
        memcpy(raw + (size_t)y * ((size_t)w * 4 + 1) + 1, rgba + (size_t)y * (size_t)w * 4, (size_t)w * 4);
        for (int x = 0; x < w; x++)
            raw[(size_t)y * ((size_t)w * 4 + 1) + 1 + (size_t)x * 4 + 3] = 255;
    }
    compress2(packed, &packed_size, raw, (uLong)raw_size, 6);
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    header[0] = (uint8_t)(w >> 24);
    header[1] = (uint8_t)(w >> 16);
    header[2] = (uint8_t)(w >> 8);
    header[3] = (uint8_t)w;
    header[4] = (uint8_t)(h >> 24);
    header[5] = (uint8_t)(h >> 16);
    header[6] = (uint8_t)(h >> 8);
    header[7] = (uint8_t)h;
    header[8] = 8;
    header[9] = 6;
    header[10] = header[11] = header[12] = 0;
    chunk(f, "IHDR", header, 13);
    chunk(f, "IDAT", packed, (uint32_t)packed_size);
    chunk(f, "IEND", 0, 0);
    fclose(f);
    free(raw);
    free(packed);
    return 1;
}

int main(int argc, char **argv)
{
    struct capture cap;
    struct vkr *r;
    struct vkr_state state;
    struct vkr_mapping map;
    static struct rd_prim prims[0x10000];
    static struct rd_vertex verts[0x40000];
    float clear[4] = {0, 0, 0, 1};
    uint32_t background;
    int width, height, validate = 0, i, game_w, game_h;
    float sx, sy;
    uint8_t *pixels;

    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--validate")) {
            validate = 1;
            memmove(&argv[i], &argv[i + 1], (size_t)(argc - i) * sizeof *argv);
            argc--;
            i--;
        }
    if (argc != 3 && argc != 5) {
        fprintf(stderr, "usage: %s FILE OUT.png [WIDTH HEIGHT] [--validate]\n", argv[0]);
        return 2;
    }
    if (!capture_load(argv[1], &cap)) {
        fprintf(stderr, "%s: not a capture\n", argv[1]);
        return 1;
    }
    game_w = (int)cap.header[2];
    game_h = (int)cap.header[3];
    width = argc == 5 ? atoi(argv[3]) : game_w;
    height = argc == 5 ? atoi(argv[4]) : game_h;

    r = vkr_create(vkGetInstanceProcAddr, validate, 0, 0);
    if (r && getenv("REROLL_ANISOTROPY"))
        vkr_set_anisotropy(r, atoi(getenv("REROLL_ANISOTROPY")));
    if (!r) {
        fprintf(stderr, "Vulkan initialisation failed\n");
        return 1;
    }
    for (i = 0; i < cap.texture_count; i++) {
        uint8_t *rgba = capture_texture_rgba(cap.textures[i].head, cap.textures[i].pixels);
        vkr_set_texture(r, (int)cap.textures[i].head[0], (int)cap.textures[i].head[2], (int)cap.textures[i].head[3],
                        rgba);
        free(rgba);
    }
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

    sx = (float)width / (float)game_w;
    sy = (float)height / (float)game_h;
    map.scale = sx < sy ? sx : sy;
    map.offset_x = ((float)width - (float)game_w * map.scale) / 2.0f;
    map.offset_y = ((float)height - (float)game_h * map.scale) / 2.0f;

    background = capture_u32(&cap, 0x6ccd8c); /* the clear colour 0x00BBGGRR (0x428390) */
    clear[0] = (float)(background & 0xff) / 255.0f;
    clear[1] = (float)((background >> 8) & 0xff) / 255.0f;
    clear[2] = (float)((background >> 16) & 0xff) / 255.0f;
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
    pixels = malloc((size_t)width * (size_t)height * 4);
    if (!vkr_end(r, pixels) || !write_png(argv[2], width, height, pixels)) {
        fprintf(stderr, "rendering failed\n");
        return 1;
    }
    printf("%s: %dx%d -> %s\n", argv[1], width, height, argv[2]);
    vkr_destroy(r);
    return 0;
}
