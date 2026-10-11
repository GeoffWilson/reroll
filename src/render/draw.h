#ifndef REROLL_RENDER_DRAW_H
#define REROLL_RENDER_DRAW_H

/* The draw list: one ordering table walk decoded into primitives a new
 * renderer can draw at any resolution (docs/renderer/plan.md, step 2).
 * Plain C, no platform calls: it builds in the DLL and natively. */
#include <stdint.h>

/* Reads `len` bytes of game memory at `address`; null if not available. */
typedef const uint8_t *(*rd_read_fn)(void *ctx, uint32_t address, uint32_t len);

/* Projection and viewport at the time of the walk (pipeline.md 2.3, views.md). */
struct rd_view {
    uint32_t ot_start;          /* the DrawOTag argument */
    int32_t screen_w, screen_h; /* 0x4acc38 / 0x4acc3c */
    float h;                    /* focal length in pixels, 0x6ccd00 */
    float cx, cy;               /* projection centre in screen pixels, 0x6ccd5c / 0x6ccd60 */
    float ox, oy;               /* viewport origin (2D packets are relative to it), 0x6ccd54 / 0x6ccd58 */
    float clip[4];              /* viewport clip rect left, right, top, bottom, 0x6ccd64..0x6ccd70 */
    float near_z, far_z;        /* 0x6ccd04 / 0x6ccd08 */
    float line_width;           /* 0x6cd41c */
};

struct rd_vertex {
    float x, y, z;  /* camera space (3D), or screen pixels with z = 0 (2D) */
    float sx, sy;   /* the original's screen position (undefined when near-clipped) */
    uint8_t u, v;   /* texel coordinates within the page (0..255) */
    uint8_t r, g, b, a;
    uint8_t pad[2];
};

enum rd_kind { RD_TRI, RD_QUAD, RD_LINE };

enum rd_flags {
    RD_2D = 1,          /* screen-pixel coordinates (2D packets) */
    RD_SCREEN = 2,      /* 3D, but at least one corner was built in screen space and
                           rebuilt as (s - c) * z / h */
    RD_TEXTURED = 4,
    RD_NEAR_CLIP = 8,   /* a corner is in front of the near plane */
    RD_IMAGE = 16,      /* a whole picture (not a game packet): drawn from the renderer's image slot,
                           with u, v 0..255 spanning the whole texture */
};

/* Corner order is the PS1 one: quads are TL, TR, BL, BR, drawn as the
 * triangles (0,1,2) and (1,3,2). */
struct rd_prim {
    uint32_t first;     /* index of the first vertex */
    uint8_t kind;       /* enum rd_kind */
    uint8_t count;      /* vertices: 3, 4 or 2 */
    uint8_t flags;      /* enum rd_flags */
    uint8_t code;       /* the packet code byte (code & 2 = semi-transparent) */
    uint8_t page;       /* texture page 0..0x3e, 0x3f = none */
    uint8_t abr;        /* PS1 semi-transparency mode 0..3 (tpage bits 6-7) */
    uint8_t tm;         /* code & 3: 0 opaque, 1 colour-keyed, 2/3 blended */
    uint8_t filter;     /* 1 bilinear, 0 point (filter packets) */
    uint32_t order;     /* position in the walk (draw order, far to near) */
    uint32_t packet;    /* the packet's address, for debugging */
};

struct rd_stats {
    uint32_t packets, ignored, unknown, unreadable;
    uint32_t camera_vertices;   /* camera-space x, y re-project exactly (< 0.01 px) */
    uint32_t nudged_vertices;   /* within 0.5 px: the track seam fix */
    uint32_t screen_vertices;   /* rebuilt from the screen position */
    uint32_t clipped_vertices;  /* near-clipped: x, y, z as stored */
    float worst_error;          /* largest re-projection error left after the rule */
};

struct rd_list {
    struct rd_view view;
    struct rd_prim *prims;
    struct rd_vertex *verts;
    uint32_t prim_count, prim_max;
    uint32_t vert_count, vert_max;
    struct rd_stats stats;
};

/* Reads the view globals (rd_view) for the walk starting at `ot_start`. */
void rd_read_view(rd_read_fn read, void *ctx, uint32_t ot_start, struct rd_view *view);

/* Decodes one ordering table walk into `list` (whose arrays the caller
 * provides; decoding stops when they are full). The view must have been
 * read first. */
void rd_decode(rd_read_fn read, void *ctx, struct rd_list *list);

#endif
