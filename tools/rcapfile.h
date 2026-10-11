/* Frame capture files (src/capture.c) for the native renderer tools. */
#ifndef REROLL_RCAPFILE_H
#define REROLL_RCAPFILE_H

#include <stdint.h>

struct region {
    uint32_t address, length;
    const uint8_t *data;
};

struct group {
    uint32_t start;
    struct region *regions;
    int count;
};

struct texture_page {
    uint32_t head[12]; /* slot, page, width, height, pitch, bits, r/g/b/a masks, key low, key high */
    const uint8_t *pixels;
};

struct capture {
    uint8_t *file;
    uint32_t header[4]; /* version, frame, screen w, screen h */
    struct region *base;
    int base_count;
    struct group *walks;
    int walk_count;
    struct texture_page *textures;
    int texture_count;
    const uint32_t *screen; /* 'SCRN': width, height, pitch, bits, r/g/b masks, then pixels (null if none) */
};

struct reader {
    const struct capture *cap;
    const struct group *walk; /* null: frame-end memory only */
};

int capture_load(const char *path, struct capture *cap);
/* Parses a chunk stream in memory (kept, not copied) into `cap`. */
int capture_parse(const uint8_t *data, uint32_t size, struct capture *cap);
/* Frees what capture_parse / capture_load allocated (the file buffer only for capture_load). */
void capture_free(struct capture *cap);
/* A texture surface (TEXS head + pixels) as RGBA8; colour-key texels get
 * alpha 0. The caller frees it. */
uint8_t *capture_texture_rgba(const uint32_t head[12], const uint8_t *pixels);
/* rd_read_fn over a capture: the walk's memory, then the frame-end memory. */
const uint8_t *capture_read(void *reader, uint32_t address, uint32_t len);
uint32_t capture_u32(const struct capture *cap, uint32_t address);

#endif
