/* DDRAW (with Direct3D) for the native build (docs/native/plan.md, step 4):
 * COM objects with the methods the game calls. Surfaces are plain memory
 * (Lock hands out the pixels; Blt / BltFast are done here), so whatever the
 * game draws into them with DirectDraw (loading pictures, movies) is there
 * for the renderer to read. Direct3D draws nothing: the game's packets go to
 * the Vulkan renderer (src/vkgame.c). The device the fake Direct3D offers
 * is the one d7vk reported under Wine ("D5VK HAL", its D3DDEVICEDESC as the
 * game stored it, captures/frame_412_0.rcap), so the game makes the same
 * choices (RGB565 textures, z-buffer mode, the blend pairs).
 *
 * Unimplemented methods stop the game with their interface and slot. */
#include "nt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef LONG HRESULT;
#define DD_OK 0
#define E_NOINTERFACE 0x80004002u
#define DDERR_GENERIC 0x80004005u
#define DDERR_NOCOLORKEY 0x887600d7u
#define DDERR_NOTFOUND 0x887600ffu
#define DDERR_UNSUPPORTED 0x80004001u

/* DDSURFACEDESC fields */
#define SD_FLAGS 0x04
#define SD_HEIGHT 0x08
#define SD_WIDTH 0x0c
#define SD_PITCH 0x10
#define SD_BACKBUFFERS 0x14
#define SD_ZDEPTH 0x18
#define SD_SURFACE 0x24
#define SD_CKSRCBLT 0x40
#define SD_FORMAT 0x48
#define SD_BITS 0x54
#define SD_RMASK 0x58
#define SD_GMASK 0x5c
#define SD_BMASK 0x60
#define SD_AMASK 0x64
#define SD_CAPS 0x68
#define SD_SIZE 0x6c

#define DDSD_CAPS 1
#define DDSD_HEIGHT 2
#define DDSD_WIDTH 4
#define DDSD_PITCH 8
#define DDSD_BACKBUFFERCOUNT 0x20
#define DDSD_ZBUFFERBITDEPTH 0x40
#define DDSD_LPSURFACE 0x800
#define DDSD_PIXELFORMAT 0x1000
#define DDSD_CKSRCBLT 0x10000

#define DDSCAPS_BACKBUFFER 0x4
#define DDSCAPS_COMPLEX 0x8
#define DDSCAPS_FLIP 0x10
#define DDSCAPS_FRONTBUFFER 0x20
#define DDSCAPS_OFFSCREENPLAIN 0x40
#define DDSCAPS_PRIMARYSURFACE 0x200
#define DDSCAPS_SYSTEMMEMORY 0x800
#define DDSCAPS_TEXTURE 0x1000
#define DDSCAPS_3DDEVICE 0x2000
#define DDSCAPS_VIDEOMEMORY 0x4000
#define DDSCAPS_ZBUFFER 0x20000

/* ---- objects ----
 *
 * Objects live in low memory (the game keeps them in 32-bit slots); only
 * our code reads them past the vtable pointer. Out-parameters are 4-byte
 * slots in game memory: OUT() stores an object there. */

#define OUT(slot, p) (*(slot) = (uint32_t)(uintptr_t)(p))

struct com {
    void **vtable;
    LONG refs;
};

struct surface {
    void **vtable;
    LONG refs;
    struct {
        void **vtable; /* IDirect3DTexture2, same object */
        struct surface *owner;
    } texture;
    uint8_t desc[SD_SIZE];
    uint8_t *pixels;
    int own_pixels;
    struct surface *back, *zbuffer;
    uint32_t key_low, key_high;
    int has_key;
};

struct viewport {
    void **vtable;
    LONG refs;
    uint8_t data[0x2c];
};

struct material {
    void **vtable;
    LONG refs;
    uint8_t data[0x44];
};

struct device {
    void **vtable;
    LONG refs;
    uint32_t states[256];
    struct viewport *current;
};

static void **ddraw_vtable, **ddraw2_vtable, **d3d_vtable, **surface_vtable, **texture_vtable, **device_vtable,
    **viewport_vtable, **material_vtable, **clipper_vtable, **palette_vtable;

static struct com ddraw, ddraw2, d3d;
static int display_w = 640, display_h = 480, display_bits = 16;

/* the device d7vk reported (D3DDEVICEDESC, 0xcc bytes) */
static const uint32_t hal_desc[0xcc / 4] = {
    0x000000cc, 0x000007ff, 0x00000002, 0x0009bef1, 0x00000008, 0x00000001, 0x00000001, 0x00000010, 0x0000000f,
    0x00000001, 0x00000008, 0x00000038, 0x00000072, 0x003361b1, 0x000000ff, 0x00001fff, 0x00001fff, 0x000000ff,
    0x000c528a, 0x0000011f, 0x0000003f, 0x000000ff, 0x0000001f, 0x00000000, 0x00000000, 0x00000038, 0x00000072,
    0x003361b1, 0x000000ff, 0x00001fff, 0x00001fff, 0x000000ff, 0x000c528a, 0x0000011f, 0x0000003f, 0x000000ff,
    0x0000001f, 0x00000000, 0x00000000, 0x00000700, 0x00000600, 0x00000000, 0x0000ffff, 0x00000001, 0x00000001,
    0x00002000, 0x00002000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};
static const uint8_t hal_guid[16] = {0xe0, 0x3d, 0xe6, 0x84, 0xaa, 0x46, 0xcf, 0x11,
                                     0x81, 0x6f, 0x00, 0x00, 0xc0, 0x20, 0x15, 0x6e};

/* ---- unimplemented methods: named stubs ---- */

void *nt_named_stub(const char *module, const char *name); /* loader.c */

static void **make_vtable(const char *interface, int count)
{
    void **v = nt_lowmem_calloc((uint32_t)count, (uint32_t)sizeof *v);
    int i;
    for (i = 0; i < count; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s method %d (+0x%x)", interface, i, i * 4);
        v[i] = nt_named_stub("com", name);
    }
    return v;
}

/* ---- IUnknown ---- */

static LONG WINAPI add_ref(struct com *o)
{
    return ++o->refs;
}

static LONG WINAPI release(struct com *o)
{
    return --o->refs > 0 ? o->refs : 0; /* objects live for the whole run */
}

static HRESULT WINAPI ok0(void *self)
{
    (void)self;
    return DD_OK;
}

static HRESULT WINAPI ok1(void *self, DWORD a)
{
    (void)self;
    (void)a;
    return DD_OK;
}

static HRESULT WINAPI ok2(void *self, DWORD a, DWORD b)
{
    (void)self;
    (void)a;
    (void)b;
    return DD_OK;
}

static HRESULT WINAPI ok3(void *self, DWORD a, DWORD b, DWORD c)
{
    (void)self;
    (void)a;
    (void)b;
    (void)c;
    return DD_OK;
}

static HRESULT WINAPI ok4(void *self, DWORD a, DWORD b, DWORD c, DWORD d)
{
    (void)self;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return DD_OK;
}

static HRESULT WINAPI ok5(void *self, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e)
{
    (void)self;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    return DD_OK;
}

static int iid_is(const void *iid, uint32_t first)
{
    return iid && *(const uint32_t *)iid == first;
}

#define IID_DIRECTDRAW 0x6c14db80u
#define IID_DIRECTDRAW2 0xb3a6f3e0u
#define IID_DIRECT3D2 0x6aae1ec1u
#define IID_TEXTURE2 0x93281502u
#define IID_SURFACE2 0x57805885u
#define IID_SURFACE3 0xda044e00u

/* ---- surfaces ---- */

static struct surface *new_surface(int w, int h, int bits, DWORD caps)
{
    struct surface *s = nt_lowmem_calloc(1, sizeof *s);
    s->vtable = surface_vtable;
    s->refs = 1;
    s->texture.vtable = texture_vtable;
    s->texture.owner = s;
    *(DWORD *)(s->desc) = SD_SIZE;
    *(DWORD *)(s->desc + SD_FLAGS) = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT;
    *(DWORD *)(s->desc + SD_WIDTH) = (DWORD)w;
    *(DWORD *)(s->desc + SD_HEIGHT) = (DWORD)h;
    *(DWORD *)(s->desc + SD_PITCH) = (DWORD)(w * bits / 8 + 3) & ~3u;
    *(DWORD *)(s->desc + SD_FORMAT) = 0x20;
    *(DWORD *)(s->desc + SD_FORMAT + 4) = 0x40; /* DDPF_RGB */
    *(DWORD *)(s->desc + SD_BITS) = (DWORD)bits;
    if (bits == 16) {
        *(DWORD *)(s->desc + SD_RMASK) = 0xf800;
        *(DWORD *)(s->desc + SD_GMASK) = 0x07e0;
        *(DWORD *)(s->desc + SD_BMASK) = 0x001f;
    } else if (bits == 32 || bits == 24) {
        *(DWORD *)(s->desc + SD_RMASK) = 0xff0000;
        *(DWORD *)(s->desc + SD_GMASK) = 0x00ff00;
        *(DWORD *)(s->desc + SD_BMASK) = 0x0000ff;
    } else if (bits == 8) {
        *(DWORD *)(s->desc + SD_FORMAT + 4) = 0x60; /* RGB | PALETTEINDEXED8 */
    }
    *(DWORD *)(s->desc + SD_CAPS) = caps;
    s->pixels = nt_lowmem_calloc(1, *(DWORD *)(s->desc + SD_PITCH) * (uint32_t)(h ? h : 1) + 16);
    s->own_pixels = 1;
    return s;
}

static int sw(const struct surface *s)
{
    return (int)*(const DWORD *)(s->desc + SD_WIDTH);
}

static int sh(const struct surface *s)
{
    return (int)*(const DWORD *)(s->desc + SD_HEIGHT);
}

static int spitch(const struct surface *s)
{
    return (int)*(const DWORD *)(s->desc + SD_PITCH);
}

static int sbytes(const struct surface *s)
{
    return (int)*(const DWORD *)(s->desc + SD_BITS) / 8;
}

static HRESULT WINAPI surface_query(struct surface *s, const void *iid, uint32_t *out)
{
    if (iid_is(iid, IID_TEXTURE2)) {
        OUT(out, &s->texture);
        s->refs++;
        return DD_OK;
    }
    if (iid_is(iid, IID_SURFACE2) || iid_is(iid, IID_SURFACE3)) {
        OUT(out, s);
        s->refs++;
        return DD_OK;
    }
    nt_log("ddraw: surface QueryInterface %08x: no", iid ? *(const uint32_t *)iid : 0);
    *out = 0;
    return (HRESULT)E_NOINTERFACE;
}

static HRESULT WINAPI surface_add_attached(struct surface *s, struct surface *other)
{
    if (*(DWORD *)(other->desc + SD_CAPS) & DDSCAPS_ZBUFFER)
        s->zbuffer = other;
    return DD_OK;
}

static void rect_or_whole(const int32_t *rect, const struct surface *s, int32_t out[4])
{
    if (rect) {
        memcpy(out, rect, 16);
    } else {
        out[0] = out[1] = 0;
        out[2] = sw(s);
        out[3] = sh(s);
    }
}

/* copies src rect to dst rect (nearest-neighbour stretch); `key` >= 0: skip
 * source pixels in the colour-key range */
static void blit(struct surface *dst, const int32_t d[4], struct surface *src, const int32_t r[4], int key)
{
    int bytes = sbytes(dst), x, y, dw = d[2] - d[0], dh = d[3] - d[1], rw = r[2] - r[0], rh = r[3] - r[1];
    if (dw <= 0 || dh <= 0 || rw <= 0 || rh <= 0 || bytes != sbytes(src) || bytes <= 0)
        return;
    for (y = 0; y < dh; y++) {
        int dy = d[1] + y, ry = r[1] + y * rh / dh;
        if (dy < 0 || dy >= sh(dst) || ry < 0 || ry >= sh(src))
            continue;
        for (x = 0; x < dw; x++) {
            int dx = d[0] + x, rx = r[0] + x * rw / dw;
            const uint8_t *sp;
            uint8_t *dp;
            uint32_t v = 0;
            if (dx < 0 || dx >= sw(dst) || rx < 0 || rx >= sw(src))
                continue;
            sp = src->pixels + ry * spitch(src) + rx * bytes;
            dp = dst->pixels + dy * spitch(dst) + dx * bytes;
            memcpy(&v, sp, (size_t)bytes);
            if (key && src->has_key && v >= src->key_low && v <= src->key_high)
                continue;
            memcpy(dp, sp, (size_t)bytes);
        }
    }
}

static void fill(struct surface *dst, const int32_t d[4], uint32_t colour)
{
    int bytes = sbytes(dst), x, y;
    for (y = d[1] < 0 ? 0 : d[1]; y < d[3] && y < sh(dst); y++)
        for (x = d[0] < 0 ? 0 : d[0]; x < d[2] && x < sw(dst); x++)
            memcpy(dst->pixels + y * spitch(dst) + x * bytes, &colour, (size_t)bytes);
}

#define DDBLT_COLORFILL 0x400
#define DDBLT_KEYSRC 0x8000
#define DDBLT_DEPTHFILL 0x2000000

static HRESULT WINAPI surface_blt(struct surface *s, const int32_t *dst_rect, struct surface *src,
                                  const int32_t *src_rect, DWORD flags, const uint8_t *fx)
{
    int32_t d[4], r[4];
    rect_or_whole(dst_rect, s, d);
    if (flags & (DDBLT_COLORFILL | DDBLT_DEPTHFILL)) {
        if (fx)
            fill(s, d, *(const uint32_t *)(fx + 0x50));
        return DD_OK;
    }
    if (!src)
        return DD_OK;
    rect_or_whole(src_rect, src, r);
    blit(s, d, src, r, (flags & DDBLT_KEYSRC) != 0);
    return DD_OK;
}

#define DDBLTFAST_SRCCOLORKEY 1

static HRESULT WINAPI surface_blt_fast(struct surface *s, DWORD x, DWORD y, struct surface *src,
                                       const int32_t *src_rect, DWORD flags)
{
    int32_t r[4], d[4];
    if (!src)
        return DD_OK;
    rect_or_whole(src_rect, src, r);
    d[0] = (int32_t)x;
    d[1] = (int32_t)y;
    d[2] = d[0] + r[2] - r[0];
    d[3] = d[1] + r[3] - r[1];
    blit(s, d, src, r, (flags & DDBLTFAST_SRCCOLORKEY) != 0);
    return DD_OK;
}

static HRESULT WINAPI surface_flip(struct surface *s, struct surface *target, DWORD flags)
{
    (void)s;
    (void)target;
    (void)flags;
    return DD_OK; /* presenting is the renderer's (the flip 0x4876f0 is hooked) */
}

static HRESULT WINAPI surface_get_attached(struct surface *s, const DWORD *caps, uint32_t *out)
{
    struct surface *a;
    if ((*caps & DDSCAPS_ZBUFFER) && s->zbuffer)
        a = s->zbuffer;
    else if (*caps & DDSCAPS_BACKBUFFER) {
        if (!s->back)
            s->back = new_surface(sw(s), sh(s), sbytes(s) * 8,
                                  DDSCAPS_BACKBUFFER | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY);
        a = s->back;
    } else {
        *out = 0;
        return (HRESULT)DDERR_NOTFOUND;
    }
    a->refs++;
    OUT(out, a);
    return DD_OK;
}

static HRESULT WINAPI surface_get_caps(struct surface *s, DWORD *caps)
{
    *caps = *(DWORD *)(s->desc + SD_CAPS);
    return DD_OK;
}

static HRESULT WINAPI surface_get_color_key(struct surface *s, DWORD flags, uint32_t *key)
{
    (void)flags;
    if (!s->has_key)
        return (HRESULT)DDERR_NOCOLORKEY;
    key[0] = s->key_low;
    key[1] = s->key_high;
    return DD_OK;
}

static HRESULT WINAPI surface_set_color_key(struct surface *s, DWORD flags, const uint32_t *key)
{
    (void)flags;
    if (!key) {
        s->has_key = 0;
        return DD_OK;
    }
    s->has_key = 1;
    s->key_low = key[0];
    s->key_high = key[1];
    return DD_OK;
}

static HRESULT WINAPI surface_get_pixel_format(struct surface *s, uint8_t *format)
{
    memcpy(format, s->desc + SD_FORMAT, 0x20);
    return DD_OK;
}

static HRESULT WINAPI surface_get_desc(struct surface *s, uint8_t *desc)
{
    memcpy(desc, s->desc, SD_SIZE);
    return DD_OK;
}

static HRESULT WINAPI surface_lock(struct surface *s, const int32_t *rect, uint8_t *desc, DWORD flags, HANDLE event)
{
    (void)flags;
    (void)event;
    memcpy(desc, s->desc, SD_SIZE);
    *(DWORD *)(desc + SD_FLAGS) |= DDSD_LPSURFACE;
    /* lpSurface: a 32-bit field; the pixels are low memory */
    *(uint32_t *)(desc + SD_SURFACE) =
        (uint32_t)(uintptr_t)(s->pixels + (rect ? rect[1] * spitch(s) + rect[0] * sbytes(s) : 0));
    return DD_OK;
}

static HRESULT WINAPI surface_get_dc(struct surface *s, uint32_t *dc)
{
    (void)s;
    *dc = 0;
    return (HRESULT)DDERR_UNSUPPORTED;
}

/* ---- IDirect3DTexture2 (inside the surface) ---- */

#define TEX_OWNER(t) (((struct {void **v; struct surface *owner;} *)(t))->owner)

static HRESULT WINAPI texture_query(void *t, const void *iid, uint32_t *out)
{
    return surface_query(TEX_OWNER(t), iid, out);
}

static LONG WINAPI texture_add_ref(void *t)
{
    return ++TEX_OWNER(t)->refs;
}

static LONG WINAPI texture_release(void *t)
{
    return --TEX_OWNER(t)->refs > 0 ? TEX_OWNER(t)->refs : 0;
}

static HRESULT WINAPI texture_get_handle(void *t, void *device, DWORD *handle)
{
    (void)device;
    *handle = (DWORD)(uintptr_t)TEX_OWNER(t);
    return DD_OK;
}

/* Load(source texture): copies the source surface's pixels */
static HRESULT WINAPI texture_load(void *t, void *source)
{
    struct surface *d = TEX_OWNER(t), *s = TEX_OWNER(source);
    int y, n = spitch(d) < spitch(s) ? spitch(d) : spitch(s);
    for (y = 0; y < sh(d) && y < sh(s); y++)
        memcpy(d->pixels + y * spitch(d), s->pixels + y * spitch(s), (size_t)n);
    d->has_key = s->has_key;
    d->key_low = s->key_low;
    d->key_high = s->key_high;
    return DD_OK;
}

/* ---- IDirectDraw / IDirectDraw2 ---- */

static HRESULT WINAPI ddraw_query(struct com *o, const void *iid, uint32_t *out)
{
    (void)o;
    if (iid_is(iid, IID_DIRECTDRAW2)) {
        OUT(out, &ddraw2);
        return DD_OK;
    }
    if (iid_is(iid, IID_DIRECTDRAW)) {
        OUT(out, &ddraw);
        return DD_OK;
    }
    if (iid_is(iid, IID_DIRECT3D2)) {
        OUT(out, &d3d);
        return DD_OK;
    }
    nt_log("ddraw: QueryInterface %08x: no", iid ? *(const uint32_t *)iid : 0);
    *out = 0;
    return (HRESULT)E_NOINTERFACE;
}

static HRESULT WINAPI ddraw_create_clipper(struct com *o, DWORD flags, uint32_t *out, void *outer)
{
    static struct com clipper;
    (void)o;
    (void)flags;
    (void)outer;
    clipper.vtable = clipper_vtable;
    OUT(out, &clipper);
    return DD_OK;
}

static HRESULT WINAPI ddraw_create_palette(struct com *o, DWORD flags, const void *entries, uint32_t *out,
                                           void *outer)
{
    struct com *p = nt_lowmem_calloc(1, sizeof *p);
    (void)o;
    (void)flags;
    (void)entries;
    (void)outer;
    p->vtable = palette_vtable;
    OUT(out, p);
    return DD_OK;
}

static HRESULT WINAPI ddraw_create_surface(struct com *o, const uint8_t *desc, uint32_t *out, void *outer)
{
    DWORD flags = *(const DWORD *)(desc + SD_FLAGS), caps = *(const DWORD *)(desc + SD_CAPS);
    int w = (flags & DDSD_WIDTH) ? (int)*(const DWORD *)(desc + SD_WIDTH) : display_w;
    int h = (flags & DDSD_HEIGHT) ? (int)*(const DWORD *)(desc + SD_HEIGHT) : display_h;
    int bits = (flags & DDSD_PIXELFORMAT) ? (int)*(const DWORD *)(desc + SD_BITS) : display_bits;
    struct surface *s;
    (void)o;
    (void)outer;
    if (caps & DDSCAPS_ZBUFFER)
        bits = (flags & DDSD_ZBUFFERBITDEPTH) ? (int)*(const DWORD *)(desc + SD_ZDEPTH) : 16;
    if (!(caps & DDSCAPS_SYSTEMMEMORY))
        caps |= DDSCAPS_VIDEOMEMORY; /* texture_upload 0x41e710 checks its textures got there */
    s = new_surface(w, h, bits, caps);
    if (flags & DDSD_PIXELFORMAT)
        memcpy(s->desc + SD_FORMAT, desc + SD_FORMAT, 0x20);
    if (caps & DDSCAPS_ZBUFFER) {
        *(DWORD *)(s->desc + SD_FLAGS) |= DDSD_ZBUFFERBITDEPTH;
        *(DWORD *)(s->desc + SD_ZDEPTH) = (DWORD)bits;
    }
    if (flags & DDSD_CKSRCBLT) {
        s->has_key = 1;
        s->key_low = *(const uint32_t *)(desc + SD_CKSRCBLT);
        s->key_high = *(const uint32_t *)(desc + SD_CKSRCBLT + 4);
    }
    if ((caps & DDSCAPS_PRIMARYSURFACE) && (flags & DDSD_BACKBUFFERCOUNT))
        s->back = new_surface(w, h, bits, DDSCAPS_BACKBUFFER | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY);
    OUT(out, s);
    return DD_OK;
}

/* modes offered: 640x480 .. 1600x1200 in 16 and 32 bits */
static const int modes[][2] = {{640, 480}, {800, 600}, {1024, 768}, {1280, 960}, {1600, 1200}};

/* The enumeration callbacks are game functions (src/game: dd_mode_enum_cb,
 * dd_driver_enum_cb, d3d_enum_device, d3d_texture_format_cb), declared
 * there with uint32_t or pointer arguments: they are called with
 * pointer-width arguments (zero-extended), right for either. What they are
 * given is low memory (the game thread's stack). */
typedef HRESULT(WINAPI *desc_cb)(uint8_t *desc, void *ctx);

static HRESULT WINAPI ddraw_enum_modes(struct com *o, DWORD flags, const uint8_t *filter, void *ctx,
                                       desc_cb callback)
{
    size_t i;
    int b;
    (void)o;
    (void)flags;
    (void)filter;
    for (b = 16; b <= 32; b += 16)
        for (i = 0; i < sizeof modes / sizeof *modes; i++) {
            uint8_t desc[SD_SIZE];
            memset(desc, 0, sizeof desc);
            *(DWORD *)desc = SD_SIZE;
            *(DWORD *)(desc + SD_FLAGS) = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT;
            *(DWORD *)(desc + SD_WIDTH) = (DWORD)modes[i][0];
            *(DWORD *)(desc + SD_HEIGHT) = (DWORD)modes[i][1];
            *(DWORD *)(desc + SD_PITCH) = (DWORD)(modes[i][0] * b / 8);
            *(DWORD *)(desc + SD_FORMAT) = 0x20;
            *(DWORD *)(desc + SD_FORMAT + 4) = 0x40;
            *(DWORD *)(desc + SD_BITS) = (DWORD)b;
            *(DWORD *)(desc + SD_RMASK) = b == 16 ? 0xf800 : 0xff0000;
            *(DWORD *)(desc + SD_GMASK) = b == 16 ? 0x07e0 : 0x00ff00;
            *(DWORD *)(desc + SD_BMASK) = b == 16 ? 0x001f : 0x0000ff;
            if (!callback(desc, ctx))
                return DD_OK;
        }
    return DD_OK;
}

/* DDCAPS as d7vk gave it, as far as the game reads it: blit, colour fill,
 * colour key, 3D */
static HRESULT WINAPI ddraw_get_caps(struct com *o, uint8_t *driver, uint8_t *hel)
{
    (void)o;
    if (driver) {
        DWORD size = *(DWORD *)driver;
        memset(driver, 0, size);
        *(DWORD *)driver = size;
        *(DWORD *)(driver + 4) = 0x84400041u;
        *(DWORD *)(driver + 8) = 0x00080000u; /* caps2: no-page-lock-required */
        *(DWORD *)(driver + 0x6c) = DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER | DDSCAPS_FLIP |
                                    DDSCAPS_BACKBUFFER | DDSCAPS_PRIMARYSURFACE | DDSCAPS_VIDEOMEMORY |
                                    DDSCAPS_OFFSCREENPLAIN | DDSCAPS_COMPLEX | DDSCAPS_SYSTEMMEMORY;
    }
    if (hel) {
        DWORD size = *(DWORD *)hel;
        memset(hel, 0, size);
        *(DWORD *)hel = size;
        *(DWORD *)(hel + 4) = 0x84400041u;
    }
    return DD_OK;
}

static HRESULT WINAPI ddraw_get_display_mode(struct com *o, uint8_t *desc)
{
    struct surface *s = new_surface(display_w, display_h, display_bits, 0);
    (void)o;
    memcpy(desc, s->desc, SD_SIZE);
    nt_lowmem_free(s->pixels);
    nt_lowmem_free(s);
    return DD_OK;
}

static HRESULT WINAPI ddraw_set_display_mode(struct com *o, DWORD w, DWORD h, DWORD bits)
{
    (void)o;
    display_w = (int)w;
    display_h = (int)h;
    display_bits = (int)bits;
    nt_log("ddraw: display mode %ux%u, %u bits", w, h, bits);
    return DD_OK;
}

static HRESULT WINAPI ddraw2_set_display_mode(struct com *o, DWORD w, DWORD h, DWORD bits, DWORD rate, DWORD flags)
{
    (void)rate;
    (void)flags;
    return ddraw_set_display_mode(o, w, h, bits);
}

static HRESULT WINAPI ddraw2_available_memory(struct com *o, const DWORD *caps, DWORD *total, DWORD *free_bytes)
{
    (void)o;
    (void)caps;
    if (total)
        *total = 256u << 20;
    if (free_bytes)
        *free_bytes = 256u << 20;
    return DD_OK;
}

/* ---- IDirect3D2 ---- */

static HRESULT WINAPI d3d_enum_devices(struct com *o,
                                       HRESULT(WINAPI *callback)(void *guid, char *description, char *name,
                                                                 void *hw, void *hel, void *ctx),
                                       void *ctx)
{
    uint8_t guid[16], hw[0xcc], hel[0xcc];
    char description[] = "D5VK HAL", name[] = "D5VK HAL";
    (void)o;
    memcpy(guid, hal_guid, 16);
    memcpy(hw, hal_desc, sizeof hw);
    memset(hel, 0, sizeof hel);
    *(DWORD *)hel = 0xcc;
    callback(guid, description, name, hw, hel, ctx);
    return DD_OK;
}

static struct device the_device;

static HRESULT WINAPI d3d_create_device(struct com *o, const void *clsid, struct surface *target, uint32_t *out)
{
    (void)o;
#if defined(__i386__)
    {
        /* as Direct3D does unless DDSCL_FPUPRESERVE (the game asks for FPUSETUP):
         * x87 single precision, round to nearest, exceptions masked. (x86-64:
         * the C's float arithmetic is SSE, which the x87 setting doesn't touch.) */
        uint16_t cw;
        __asm__ volatile("fnstcw %0" : "=m"(cw));
        cw = (uint16_t)((cw & ~0xf3f) | 0x3f);
        __asm__ volatile("fldcw %0" : : "m"(cw));
    }
#endif
    (void)clsid;
    (void)target;
    the_device.vtable = device_vtable;
    the_device.refs = 1;
    OUT(out, &the_device);
    return DD_OK;
}

static HRESULT WINAPI d3d_create_viewport(struct com *o, uint32_t *out, void *outer)
{
    struct viewport *v = nt_lowmem_calloc(1, sizeof *v);
    (void)o;
    (void)outer;
    v->vtable = viewport_vtable;
    v->refs = 1;
    OUT(out, v);
    return DD_OK;
}

static HRESULT WINAPI d3d_create_material(struct com *o, uint32_t *out, void *outer)
{
    struct material *m = nt_lowmem_calloc(1, sizeof *m);
    (void)o;
    (void)outer;
    m->vtable = material_vtable;
    m->refs = 1;
    OUT(out, m);
    return DD_OK;
}

/* ---- IDirect3DDevice2 ---- */

static HRESULT WINAPI device_get_caps(struct device *d, void *hw, void *hel)
{
    (void)d;
    if (hw)
        memcpy(hw, hal_desc, 0xcc);
    if (hel) {
        memset(hel, 0, 0xcc);
        *(DWORD *)hel = 0xcc;
    }
    return DD_OK;
}

/* texture formats: RGB565, ARGB1555, ARGB4444 (the game takes 565) */
static HRESULT WINAPI device_enum_texture_formats(struct device *d, desc_cb callback, void *ctx)
{
    static const uint32_t formats[3][4] = {
        {0xf800, 0x07e0, 0x001f, 0}, {0x7c00, 0x03e0, 0x001f, 0x8000}, {0x0f00, 0x00f0, 0x000f, 0xf000}};
    int i;
    (void)d;
    for (i = 0; i < 3; i++) {
        uint8_t desc[SD_SIZE];
        memset(desc, 0, sizeof desc);
        *(DWORD *)desc = SD_SIZE;
        *(DWORD *)(desc + SD_FLAGS) = DDSD_CAPS | DDSD_PIXELFORMAT;
        *(DWORD *)(desc + SD_FORMAT) = 0x20;
        *(DWORD *)(desc + SD_FORMAT + 4) = formats[i][3] ? 0x41 : 0x40; /* RGB (| ALPHAPIXELS) */
        *(DWORD *)(desc + SD_BITS) = 16;
        *(DWORD *)(desc + SD_RMASK) = formats[i][0];
        *(DWORD *)(desc + SD_GMASK) = formats[i][1];
        *(DWORD *)(desc + SD_BMASK) = formats[i][2];
        *(DWORD *)(desc + SD_AMASK) = formats[i][3];
        *(DWORD *)(desc + SD_CAPS) = DDSCAPS_TEXTURE;
        if (!callback(desc, ctx))
            break;
    }
    return DD_OK;
}

static HRESULT WINAPI device_set_viewport(struct device *d, struct viewport *v)
{
    d->current = v;
    return DD_OK;
}

static HRESULT WINAPI device_get_render_state(struct device *d, DWORD state, DWORD *value)
{
    *value = state < 256 ? d->states[state] : 0;
    return DD_OK;
}

static HRESULT WINAPI device_set_render_state(struct device *d, DWORD state, DWORD value)
{
    if (state < 256)
        d->states[state] = value;
    return DD_OK;
}

/* ---- IDirect3DViewport2 ---- */

static HRESULT WINAPI viewport_set(struct viewport *v, const uint8_t *data)
{
    memcpy(v->data, data, sizeof v->data);
    return DD_OK;
}

static HRESULT WINAPI viewport_get(struct viewport *v, uint8_t *data)
{
    memcpy(data, v->data, *(const DWORD *)data <= sizeof v->data ? *(const DWORD *)data : sizeof v->data);
    return DD_OK;
}

/* ---- IDirect3DMaterial2 ---- */

static HRESULT WINAPI material_set(struct material *m, const uint8_t *data)
{
    memcpy(m->data, data, sizeof m->data);
    return DD_OK;
}

static HRESULT WINAPI material_get_handle(struct material *m, void *device, DWORD *handle)
{
    (void)device;
    *handle = (DWORD)(uintptr_t)m;
    return DD_OK;
}

/* ---- set-up ---- */

#define SET(v, i, f) ((v)[i] = (void *)(f))

static HRESULT WINAPI draw_indexed(void *s, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g);

static void build_vtables(void)
{
    int i, arena;
    if (ddraw_vtable)
        return;
    arena = nt_lowmem_arena(0); /* they outlive the game's runs */
    ddraw_vtable = make_vtable("IDirectDraw", 23);
    ddraw2_vtable = make_vtable("IDirectDraw2", 24);
    for (i = 0; i < 2; i++) {
        void **v = i ? ddraw2_vtable : ddraw_vtable;
        SET(v, 0, ddraw_query);
        SET(v, 1, add_ref);
        SET(v, 2, release);
        SET(v, 3, ok0);                 /* Compact */
        SET(v, 4, ddraw_create_clipper);
        SET(v, 5, ddraw_create_palette);
        SET(v, 6, ddraw_create_surface);
        SET(v, 8, ddraw_enum_modes);
        SET(v, 10, ok0);                /* FlipToGDISurface */
        SET(v, 11, ddraw_get_caps);
        SET(v, 12, ddraw_get_display_mode);
        SET(v, 19, ok0);                /* RestoreDisplayMode */
        SET(v, 20, ok2);                /* SetCooperativeLevel */
        SET(v, 21, i ? (void *)ddraw2_set_display_mode : (void *)ddraw_set_display_mode);
        SET(v, 22, ok2);                /* WaitForVerticalBlank */
    }
    SET(ddraw2_vtable, 23, ddraw2_available_memory);

    d3d_vtable = make_vtable("IDirect3D2", 9);
    SET(d3d_vtable, 0, ddraw_query);
    SET(d3d_vtable, 1, add_ref);
    SET(d3d_vtable, 2, release);
    SET(d3d_vtable, 3, d3d_enum_devices);
    SET(d3d_vtable, 5, d3d_create_material);
    SET(d3d_vtable, 6, d3d_create_viewport);
    SET(d3d_vtable, 8, d3d_create_device);

    surface_vtable = make_vtable("IDirectDrawSurface", 40);
    SET(surface_vtable, 0, surface_query);
    SET(surface_vtable, 1, add_ref);
    SET(surface_vtable, 2, release);
    SET(surface_vtable, 3, surface_add_attached);
    SET(surface_vtable, 5, surface_blt);
    SET(surface_vtable, 7, surface_blt_fast);
    SET(surface_vtable, 8, ok2);   /* DeleteAttachedSurface */
    SET(surface_vtable, 11, surface_flip);
    SET(surface_vtable, 12, surface_get_attached);
    SET(surface_vtable, 13, ok1);  /* GetBltStatus */
    SET(surface_vtable, 14, surface_get_caps);
    SET(surface_vtable, 16, surface_get_color_key);
    SET(surface_vtable, 17, surface_get_dc);
    SET(surface_vtable, 18, ok1);  /* GetFlipStatus */
    SET(surface_vtable, 21, surface_get_pixel_format);
    SET(surface_vtable, 22, surface_get_desc);
    SET(surface_vtable, 24, ok0);  /* IsLost */
    SET(surface_vtable, 25, surface_lock);
    SET(surface_vtable, 26, ok1);  /* ReleaseDC */
    SET(surface_vtable, 27, ok0);  /* Restore */
    SET(surface_vtable, 28, ok1);  /* SetClipper */
    SET(surface_vtable, 29, surface_set_color_key);
    SET(surface_vtable, 31, ok1);  /* SetPalette */
    SET(surface_vtable, 32, ok1);  /* Unlock */
    SET(surface_vtable, 37, ok1);  /* PageLock (Surface2) */
    SET(surface_vtable, 38, ok1);  /* PageUnlock */

    texture_vtable = make_vtable("IDirect3DTexture2", 6);
    SET(texture_vtable, 0, texture_query);
    SET(texture_vtable, 1, texture_add_ref);
    SET(texture_vtable, 2, texture_release);
    SET(texture_vtable, 3, texture_get_handle);
    SET(texture_vtable, 4, ok2);   /* PaletteChanged */
    SET(texture_vtable, 5, texture_load);

    device_vtable = make_vtable("IDirect3DDevice2", 33);
    SET(device_vtable, 1, add_ref);
    SET(device_vtable, 2, release);
    SET(device_vtable, 3, device_get_caps);
    SET(device_vtable, 6, ok1);    /* AddViewport */
    SET(device_vtable, 7, ok1);    /* DeleteViewport */
    SET(device_vtable, 9, device_enum_texture_formats);
    SET(device_vtable, 10, ok0);   /* BeginScene */
    SET(device_vtable, 11, ok0);   /* EndScene */
    SET(device_vtable, 13, device_set_viewport);
    SET(device_vtable, 22, device_get_render_state);
    SET(device_vtable, 23, device_set_render_state);
    SET(device_vtable, 24, ok2);   /* GetLightState */
    SET(device_vtable, 25, ok2);   /* SetLightState */
    SET(device_vtable, 26, ok2);   /* SetTransform */
    SET(device_vtable, 29, ok5);   /* DrawPrimitive(type, vertex type, vertices, count, flags) */
    SET(device_vtable, 31, ok1);   /* SetClipStatus */
    SET(device_vtable, 32, ok1);   /* GetClipStatus */
    SET(device_vtable, 30, draw_indexed); /* DrawIndexedPrimitive: 7 arguments */

    viewport_vtable = make_vtable("IDirect3DViewport2", 18);
    SET(viewport_vtable, 1, add_ref);
    SET(viewport_vtable, 2, release);
    SET(viewport_vtable, 4, viewport_get);
    SET(viewport_vtable, 5, viewport_set);
    SET(viewport_vtable, 8, ok1);  /* SetBackground */
    SET(viewport_vtable, 12, ok3); /* Clear(count, rects, flags) */
    SET(viewport_vtable, 16, viewport_get);
    SET(viewport_vtable, 17, viewport_set);

    material_vtable = make_vtable("IDirect3DMaterial2", 6);
    SET(material_vtable, 1, add_ref);
    SET(material_vtable, 2, release);
    SET(material_vtable, 3, material_set);
    SET(material_vtable, 5, material_get_handle);

    clipper_vtable = make_vtable("IDirectDrawClipper", 9);
    SET(clipper_vtable, 1, add_ref);
    SET(clipper_vtable, 2, release);
    SET(clipper_vtable, 8, ok2);   /* SetHWnd */

    palette_vtable = make_vtable("IDirectDrawPalette", 7);
    SET(palette_vtable, 1, add_ref);
    SET(palette_vtable, 2, release);
    SET(palette_vtable, 6, ok4);   /* SetEntries */

    ddraw.vtable = ddraw_vtable;
    ddraw2.vtable = ddraw2_vtable;
    d3d.vtable = d3d_vtable;
    nt_lowmem_arena(arena);
}

static HRESULT WINAPI draw_indexed(void *s, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g)
{
    (void)s;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    return DD_OK;
}

HRESULT WINAPI DirectDrawCreate(const void *guid, uint32_t *out, void *outer)
{
    (void)guid;
    (void)outer;
    build_vtables();
    ddraw.refs = 1;
    OUT(out, &ddraw);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateA(BOOL(WINAPI *callback)(void *guid, char *description, char *name, void *ctx),
                                    void *ctx)
{
    char description[] = "Primary Display Driver", name[] = "display";
    callback(0, description, name, ctx);
    return DD_OK;
}
