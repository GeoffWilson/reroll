/* Frame capture files: see rcapfile.h and src/capture.c. */
#include "rcapfile.h"

#ifndef RCAP_NO_STDIO /* the freestanding Windows test (tools/vkwintest.c) has no stdio */
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>

static const uint8_t *find(const struct region *r, int n, uint32_t address, uint32_t len)
{
    int i;
    for (i = 0; i < n; i++)
        if (r[i].address <= address && address + len <= r[i].address + r[i].length)
            return r[i].data + (address - r[i].address);
    return 0;
}

const uint8_t *capture_read(void *ctx, uint32_t address, uint32_t len)
{
    const struct reader *r = ctx;
    const uint8_t *p = r->walk ? find(r->walk->regions, r->walk->count, address, len) : 0;
    return p ? p : find(r->cap->base, r->cap->base_count, address, len);
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t capture_u32(const struct capture *cap, uint32_t address)
{
    const uint8_t *p = find(cap->base, cap->base_count, address, 4);
    return p ? u32(p) : 0;
}

int capture_parse(const uint8_t *data, uint32_t size, struct capture *cap)
{
    uint32_t pos = 0;
    struct group *current = 0;

    while (pos + 8 <= size) {
        const uint8_t *tag = data + pos;
        uint32_t len = u32(data + pos + 4);
        const uint8_t *payload = data + pos + 8;
        if (pos + 8 + len > size)
            break;
        if (!memcmp(tag, "RCAP", 4)) {
            memcpy(cap->header, payload, 16);
        } else if (!memcmp(tag, "OTAG", 4)) {
            cap->walks = realloc(cap->walks, (size_t)(cap->walk_count + 1) * sizeof *cap->walks);
            current = &cap->walks[cap->walk_count++];
            current->start = u32(payload);
            current->regions = 0;
            current->count = 0;
        } else if (!memcmp(tag, "BASE", 4)) {
            current = 0;
        } else if (!memcmp(tag, "REGN", 4)) {
            struct region r = {u32(payload), len - 4, payload + 4};
            if (current) {
                current->regions = realloc(current->regions, (size_t)(current->count + 1) * sizeof r);
                current->regions[current->count++] = r;
            } else {
                cap->base = realloc(cap->base, (size_t)(cap->base_count + 1) * sizeof r);
                cap->base[cap->base_count++] = r;
            }
        } else if (!memcmp(tag, "SCRN", 4)) {
            cap->screen = (const uint32_t *)payload;
        } else if (!memcmp(tag, "TEXS", 4)) {
            struct texture_page *t;
            cap->textures = realloc(cap->textures, (size_t)(cap->texture_count + 1) * sizeof *cap->textures);
            t = &cap->textures[cap->texture_count++];
            memcpy(t->head, payload, sizeof t->head);
            t->pixels = payload + sizeof t->head;
        }
        pos += 8 + len;
    }
    return cap->header[0] == 1;
}

void capture_free(struct capture *cap)
{
    int i;
    for (i = 0; i < cap->walk_count; i++)
        free(cap->walks[i].regions);
    free(cap->walks);
    free(cap->base);
    free(cap->textures);
    free(cap->file);
    memset(cap, 0, sizeof *cap);
}

#ifndef RCAP_NO_STDIO
int capture_load(const char *path, struct capture *cap)
{
    FILE *f = fopen(path, "rb");
    long size;

    memset(cap, 0, sizeof *cap);
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    cap->file = malloc((size_t)size);
    if (fread(cap->file, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return 0;
    }
    fclose(f);
    return capture_parse(cap->file, (uint32_t)size, cap);
}
#endif

static int channel(uint32_t v, uint32_t mask)
{
    int shift = 0;
    if (!mask)
        return 255;
    while (!(mask & 1u << shift))
        shift++;
    return (int)(((v & mask) >> shift) * 255 / (mask >> shift));
}

uint8_t *capture_texture_rgba(const uint32_t head[12], const uint8_t *pixels)
{
    uint32_t w = head[2], h = head[3], pitch = head[4], bits = head[5];
    uint32_t key_low = head[10], key_high = head[11], x, y;
    uint8_t *out = malloc((size_t)w * h * 4);

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const uint8_t *p = pixels + y * pitch + x * (bits / 8);
            uint32_t v = bits == 16 ? (uint32_t)(p[0] | p[1] << 8)
                         : bits == 32 ? (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24)
                                      : p[0];
            uint8_t *o = out + ((size_t)y * w + x) * 4;
            o[0] = (uint8_t)channel(v, head[6]);
            o[1] = (uint8_t)channel(v, head[7]);
            o[2] = (uint8_t)channel(v, head[8]);
            o[3] = (v >= key_low && v <= key_high) ? 0 : 255;
        }
    return out;
}
