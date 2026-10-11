/* Ordering table decoder: walks one table the way DrawOTag 0x421860 does
 * and turns every packet into draw-list primitives (draw.h). Packet layouts
 * and the back end's choices are from docs/renderer/pipeline.md 2 and the
 * handlers of 0x420be0 (jump table 0x4217a4, index 0x4217dc).
 *
 * Positions follow the back end exactly:
 *   - no corner has clip flags (the fast path): the stored screen x, y are
 *     drawn as they are, so the camera-space position is rebuilt from them,
 *     (s - c) * z / h. Billboards and overlays only fill in screen x, y.
 *   - flags set, and none is 0x40: the clipper works on camera-space x, y, z
 *     and re-projects them.
 *   - a corner has 0x40: the screen-space clipper, on screen x, y again.
 * Colours: each corner's colour dword is sent with R and B swapped and its
 * top byte as alpha. For GT4, GT3 and FT4 the back end first puts the
 * packet's +0x22 byte in corner 0's top byte (FT4 uses that colour for
 * every corner); FT3 forces 0xff; F4 uses r | g | b; a line uses
 * (r/2 + g/2 + b/2) * 10 / 16; the 2D polygons use 0xff. */
#include "draw.h"

#define U8(p, o) ((p)[o])
#define U16(p, o) ((uint16_t)((p)[o] | (p)[(o) + 1] << 8))
#define S16(p, o) ((int16_t)U16(p, o))
#define U32(p, o) ((uint32_t)(p)[o] | (uint32_t)(p)[(o) + 1] << 8 | (uint32_t)(p)[(o) + 2] << 16 | (uint32_t)(p)[(o) + 3] << 24)

static float f32(const uint8_t *p, int o)
{
    union {
        uint32_t u;
        float f;
    } v;
    v.u = U32(p, o);
    return v.f;
}

static float read_f32(rd_read_fn read, void *ctx, uint32_t address)
{
    const uint8_t *p = read(ctx, address, 4);
    return p ? f32(p, 0) : 0.0f;
}

void rd_read_view(rd_read_fn read, void *ctx, uint32_t ot_start, struct rd_view *view)
{
    const uint8_t *p;
    int i;

    view->ot_start = ot_start;
    p = read(ctx, 0x4acc38, 8);
    view->screen_w = p ? (int32_t)U32(p, 0) : 640;
    view->screen_h = p ? (int32_t)U32(p, 4) : 480;
    view->h = read_f32(read, ctx, 0x6ccd00);
    view->cx = read_f32(read, ctx, 0x6ccd5c);
    view->cy = read_f32(read, ctx, 0x6ccd60);
    view->ox = read_f32(read, ctx, 0x6ccd54);
    view->oy = read_f32(read, ctx, 0x6ccd58);
    for (i = 0; i < 4; i++)
        view->clip[i] = read_f32(read, ctx, 0x6ccd64 + 4 * (uint32_t)i);
    view->near_z = read_f32(read, ctx, 0x6ccd04);
    view->far_z = read_f32(read, ctx, 0x6ccd08);
    view->line_width = read_f32(read, ctx, 0x6cd41c);
}

/* Packet sizes by code >> 2, for reading; 0 = no packet body needed. */
static uint32_t packet_size(int kind)
{
    switch (kind) {
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
    default: return 8;
    }
}

struct decoder {
    rd_read_fn read;
    void *ctx;
    struct rd_list *list;
    uint32_t mode;   /* tpage byte | (code & 3) << 8, as cached in 0x6cce5c */
    uint8_t filter;
    uint32_t order;
};

static void set_mode(struct decoder *d, uint8_t tpage, uint8_t code)
{
    d->mode = (uint32_t)tpage | (uint32_t)(code & 3) << 8;
}

static struct rd_prim *new_prim(struct decoder *d, int kind, int count, uint32_t packet, uint8_t code)
{
    struct rd_list *l = d->list;
    struct rd_prim *p;

    if (l->prim_count >= l->prim_max || l->vert_count + (uint32_t)count > l->vert_max)
        return 0;
    p = &l->prims[l->prim_count++];
    p->first = l->vert_count;
    l->vert_count += (uint32_t)count;
    p->kind = (uint8_t)kind;
    p->count = (uint8_t)count;
    p->flags = 0;
    p->code = code;
    p->page = (uint8_t)(d->mode & 0x3f);
    p->abr = (uint8_t)((d->mode >> 6) & 3);
    p->tm = (uint8_t)((d->mode >> 8) & 3);
    if (p->page != 0x3f)
        p->flags |= RD_TEXTURED;
    p->filter = d->filter;
    p->order = d->order;
    p->packet = packet;
    return p;
}

/* Corner colour: PS1 order 0x00BBGGRR in the low bytes, alpha given. */
static void set_colour(struct rd_vertex *v, uint32_t rgb, uint8_t a)
{
    v->r = (uint8_t)rgb;
    v->g = (uint8_t)(rgb >> 8);
    v->b = (uint8_t)(rgb >> 16);
    v->a = a;
}

/* Fills the positions of a 3D primitive's corners from their 0x18-byte
 * records (pipeline.md 2.3), following the back end's path choice. */
static void set_positions(struct decoder *d, struct rd_prim *prim, const uint8_t *packet, const int *offsets)
{
    struct rd_list *l = d->list;
    const struct rd_view *view = &l->view;
    uint32_t flags_or = 0;
    int i, screen;

    for (i = 0; i < prim->count; i++)
        flags_or |= U32(packet, offsets[i]);
    screen = flags_or == 0 || (flags_or & 0x40);
    for (i = 0; i < prim->count; i++) {
        const uint8_t *rec = packet + offsets[i];
        struct rd_vertex *v = &l->verts[prim->first + (uint32_t)i];
        uint32_t flags = U32(rec, 0);
        float z = f32(rec, 4), sx = f32(rec, 8), sy = f32(rec, 0xc), x = f32(rec, 0x10), y = f32(rec, 0x14);

        v->z = z;
        v->sx = sx;
        v->sy = sy;
        if (screen && z > 0.0f && view->h != 0.0f) {
            float ex, ey, err;
            v->x = (sx - view->cx) * z / view->h;
            v->y = (sy - view->cy) * z / view->h;
            ex = x * (view->h / z) + view->cx - sx;
            ey = y * (view->h / z) + view->cy - sy;
            err = ex < 0 ? -ex : ex;
            if ((ey < 0 ? -ey : ey) > err)
                err = ey < 0 ? -ey : ey;
            if (err < 0.01f)
                l->stats.camera_vertices++;
            else if (err <= 0.5f)
                l->stats.nudged_vertices++;
            else {
                l->stats.screen_vertices++;
                prim->flags |= RD_SCREEN;
            }
        } else {
            v->x = x;
            v->y = y;
            if (flags & 0x10) {
                prim->flags |= RD_NEAR_CLIP;
                l->stats.clipped_vertices++;
            } else {
                float ex = x * (view->h / z) + view->cx - sx, ey = y * (view->h / z) + view->cy - sy;
                float err = ex < 0 ? -ex : ex;
                if ((ey < 0 ? -ey : ey) > err)
                    err = ey < 0 ? -ey : ey;
                if (err < 0.01f)
                    l->stats.camera_vertices++;
                else if (err <= 0.5f)
                    l->stats.nudged_vertices++;
                else if (err > l->stats.worst_error)
                    l->stats.worst_error = err; /* the clip path ignores the stored screen x, y */
            }
        }
    }
}

static void set_uv(struct rd_vertex *v, const uint8_t *packet, int offset)
{
    v->u = U8(packet, offset);
    v->v = U8(packet, offset + 1);
}

static void decode_3d(struct decoder *d, uint32_t address, const uint8_t *p, int kind)
{
    static const int gt4_v[] = {0x08, 0x28, 0x48, 0x68}, gt4_uv[] = {0x20, 0x40, 0x60, 0x80}, gt4_rgb[] = {0x04, 0x24, 0x44, 0x64};
    static const int ft4_v[] = {0x08, 0x24, 0x40, 0x5c}, ft4_uv[] = {0x20, 0x3c, 0x58, 0x74};
    static const int f4_v[] = {0x08, 0x20, 0x38, 0x50}, line_v[] = {0x08, 0x24};
    uint8_t code = U8(p, 7);
    struct rd_prim *prim;
    struct rd_vertex *v;
    int i, n;

    switch (kind) {
    case 0x23: /* GT4 */
    case 0x27: /* GT3 */
        n = kind == 0x23 ? 4 : 3;
        set_mode(d, U8(p, 0x42), code);
        prim = new_prim(d, n == 4 ? RD_QUAD : RD_TRI, n, address, code);
        if (!prim)
            return;
        v = &d->list->verts[prim->first];
        for (i = 0; i < n; i++) {
            uint32_t rgb = U32(p, gt4_rgb[i]);
            set_colour(&v[i], rgb, i == 0 ? U8(p, 0x22) : (uint8_t)(rgb >> 24));
            set_uv(&v[i], p, gt4_uv[i]);
        }
        set_positions(d, prim, p, gt4_v);
        break;
    case 0x26: /* FT4 */
    case 0x25: /* FT3 */
        n = kind == 0x26 ? 4 : 3;
        set_mode(d, U8(p, 0x3e), code);
        prim = new_prim(d, n == 4 ? RD_QUAD : RD_TRI, n, address, code);
        if (!prim)
            return;
        v = &d->list->verts[prim->first];
        for (i = 0; i < n; i++) {
            set_colour(&v[i], U32(p, 4), n == 4 ? U8(p, 0x22) : 0xff);
            set_uv(&v[i], p, ft4_uv[i]);
        }
        set_positions(d, prim, p, ft4_v);
        break;
    case 0x2a: { /* F4, untextured */
        uint32_t rgb = U32(p, 4);
        set_mode(d, 0x3f, code);
        prim = new_prim(d, RD_QUAD, 4, address, code);
        if (!prim)
            return;
        v = &d->list->verts[prim->first];
        for (i = 0; i < 4; i++) {
            set_colour(&v[i], rgb, (uint8_t)(rgb | rgb >> 8 | rgb >> 16));
            v[i].u = v[i].v = 0;
        }
        set_positions(d, prim, p, f4_v);
        break;
    }
    case 0x2b: { /* line: drawn as a quad line_width wide */
        uint32_t rgb = U32(p, 4), half = (rgb & 0xfefefe) >> 1;
        uint8_t a = (uint8_t)((((half & 0xff) + ((half >> 8) & 0xff) + (half >> 16)) * 10) >> 4);
        set_mode(d, 0x3f, code);
        prim = new_prim(d, RD_LINE, 2, address, code);
        if (!prim)
            return;
        v = &d->list->verts[prim->first];
        set_colour(&v[0], rgb, a);
        set_colour(&v[1], U32(p, 0x20), a);
        v[0].u = v[0].v = v[1].u = v[1].v = 0;
        set_positions(d, prim, p, line_v);
        break;
    }
    }
}

static void put_2d(struct rd_vertex *v, const struct rd_view *view, int x, int y)
{
    v->x = v->sx = (float)x + view->ox;
    v->y = v->sy = (float)y + view->oy;
    v->z = 0.0f;
}

static void decode_2d(struct decoder *d, uint32_t address, const uint8_t *p, int kind)
{
    const struct rd_view *view = &d->list->view;
    uint8_t code = U8(p, 7);
    struct rd_prim *prim;
    struct rd_vertex *v;
    int i;

    switch (kind) {
    case 0x0b: /* POLY_FT4: +8 xy0, +0xc uv0, +0x10 xy1, +0x14 uv1, +0x16 tpage, ... */
        set_mode(d, U8(p, 0x16), code);
        prim = new_prim(d, RD_QUAD, 4, address, code);
        if (!prim)
            return;
        prim->flags |= RD_2D;
        v = &d->list->verts[prim->first];
        for (i = 0; i < 4; i++) {
            put_2d(&v[i], view, S16(p, 8 + 8 * i), S16(p, 0xa + 8 * i));
            set_uv(&v[i], p, 0xc + 8 * i);
            set_colour(&v[i], U32(p, 4), 0xff);
        }
        break;
    case 0x0c: /* POLY_G3: +4 rgb0, +8 xy0, +0xc rgb1, +0x10 xy1, +0x14 rgb2, +0x18 xy2 */
        set_mode(d, 0x3f, code);
        prim = new_prim(d, RD_TRI, 3, address, code);
        if (!prim)
            return;
        prim->flags |= RD_2D;
        v = &d->list->verts[prim->first];
        for (i = 0; i < 3; i++) {
            put_2d(&v[i], view, S16(p, 8 + 8 * i), S16(p, 0xa + 8 * i));
            v[i].u = v[i].v = 0;
            set_colour(&v[i], U32(p, 4 + 8 * i), 0xff);
        }
        break;
    case 0x11: /* SPRT with its own page byte at +0xe, then as SPRT */
        set_mode(d, U8(p, 0xe), code);
        /* fall through */
    case 0x19: { /* SPRT: +8 x, y, +0xc u, v, +0x10 w, h; the current page */
        int x = S16(p, 8), y = S16(p, 0xa), w = S16(p, 0x10), h = S16(p, 0x12);
        prim = new_prim(d, RD_QUAD, 4, address, code);
        if (!prim)
            return;
        prim->flags |= RD_2D;
        v = &d->list->verts[prim->first];
        for (i = 0; i < 4; i++) {
            put_2d(&v[i], view, x + (i & 1) * w, y + (i >> 1) * h);
            v[i].u = (uint8_t)(U8(p, 0xc) + (i & 1) * w);
            v[i].v = (uint8_t)(U8(p, 0xd) + (i >> 1) * h);
            set_colour(&v[i], U32(p, 4), 0xff);
        }
        break;
    }
    }
}

void rd_decode(rd_read_fn read, void *ctx, struct rd_list *list)
{
    struct decoder d;
    const uint8_t *p;
    uint32_t word;
    int guard = 0x100000;

    d.read = read;
    d.ctx = ctx;
    d.list = list;
    d.mode = 0x3f; /* DrawOTag resets the cached mode first */
    d.filter = 1;
    d.order = 0;
    list->prim_count = list->vert_count = 0;
    {
        struct rd_stats zero = {0};
        list->stats = zero;
    }
    p = read(ctx, list->view.ot_start, 4);
    word = p ? U32(p, 0) : 0xffffffff;
    while (word != 0xffffffff && guard-- > 0) {
        uint32_t address = word & 0x7fffffff;
        if (word & 0x80000000) { /* link to an ordering table entry */
            p = read(ctx, address, 4);
            if (!p) {
                list->stats.unreadable++;
                break;
            }
            word = U32(p, 0);
            continue;
        }
        p = read(ctx, address, 8);
        if (!p) {
            list->stats.unreadable++;
            break;
        }
        {
            uint8_t code = U8(p, 7);
            int kind = code >> 2;
            uint32_t next = U32(p, 0);
            const uint8_t *body = read(ctx, address, packet_size(kind));

            list->stats.packets++;
            if (!body) {
                list->stats.unreadable++;
            } else {
                switch (kind) {
                case 0x23: case 0x27: case 0x26: case 0x25: case 0x2a: case 0x2b:
                    decode_3d(&d, address, body, kind);
                    break;
                case 0x0b: case 0x0c: case 0x11: case 0x19:
                    decode_2d(&d, address, body, kind);
                    break;
                case 0x2d: /* DR_TPAGE: +4 tpage byte */
                    set_mode(&d, U8(body, 4), code);
                    break;
                case 0x2c: /* filter: +4 != 0 bilinear */
                    d.filter = U8(body, 4) != 0;
                    break;
                case 0x28: /* fog marker: no effect in this build */
                    list->stats.ignored++;
                    break;
                default: /* the dispatcher ignores other codes (0 included) */
                    if (kind < 0xb || kind > 0x2d)
                        list->stats.ignored++;
                    else
                        list->stats.unknown++;
                    break;
                }
            }
            d.order++;
            word = next;
        }
    }
}
