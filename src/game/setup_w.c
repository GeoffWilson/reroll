/* Race setup (0x46a060), group W: pieces it calls once when a race starts.
 *
 * - The shield ripple (0x46d2b0): the shield mesh (a track model) gets a
 *   vertex neighbour list (8 bytes per vertex at 0x4d44a0: a count and up to
 *   7 neighbours), a table of the vertex nearest each of 64 directions
 *   (0x4d44a8), and each car's ripple record (6 at 0x6002e0, 0x84 bytes)
 *   gets its two polygon lists and vertex colours; with animated textures
 *   on, the polygons' texture coordinates are mapped from the mesh's x/z
 *   extent (0x46e160).
 * - The heap and the views (0x46a040): viewport rectangles, the projection
 *   centre and the default view (0x4238c0).
 * - Track water (0x41aa60): the rippling track vertices (0x43e050) and the
 *   water meshes' resting waves (0x482b20), plus the top 32 palette entries
 *   used to shade them.
 * - Each car's chase camera view record (0x406be0) and the viewports'
 *   director state (0x6cf6a0).
 * - The weather tint (0x485070): darkens the track palette and the colour
 *   tables of the skies, lights and effects.
 * - Small ones: the pad state reset (0x41b370), tick_sync (0x429220), the
 *   attract / network start messages (0x463860), a player's car choice
 *   (0x449ca0).
 *
 * Still original (called through their addresses): the heap allocator
 * 0x4445e0 and the initial heap 0x444500 (GlobalAlloc), the texture locks
 * of the animated shield texture (0x46d0c0), vec_normalise_float 0x4290b0
 * (x87), the viewport / D3D state (0x41f2d0, 0x423f00, 0x423ce0,
 * set_view_matrix 0x423d10, 0x423d30, 0x427a70, the clear colour 0x428390),
 * bodies (0x460af0, 0x460b40, 0x465640, 0x419150), 0x40afb0, text_create
 * 0x479060 and the empty PlayStation-era waits 0x479ae0 / 0x4291f0 /
 * 0x428060. Fatal (0x41c970) and its formatter stay original too. */
#include "../runtime.h"
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_config (*(uint8_t **)0x4b2198)
#define g_track (*(uint8_t **)0x6c1e30)
#define g_track_info (*(uint8_t **)0x6c1eb0)
#define g_track_colours (*(uint8_t **)0x6c1e14)
#define g_sections (*(uint8_t ***)0x6bfac0)
#define g_palette (*(uint8_t **)0x6bfaf0)
#define g_effect_seed (*(uint32_t *)0x4b11dc)
#define g_sin ((const int16_t *)0x4b24f8)

/* The shield ripple. */
#define g_shield_mesh (*(uint8_t **)0x4d44a4)
#define g_shield_links (*(uint8_t **)0x4d44a0)
#define g_shield_dirs (*(uint8_t **)0x4d44a8)
#define g_shield_vertices (*(int32_t *)0x4d44ac)
#define g_shield_texture (*(int32_t *)0x4b24a0)
#define SHIELDS ((uint8_t *)0x6002e0)
#define SHIELD_SIZE 0x84
#define SHIELDS_END 0x6005f8

/* Views. */
#define g_screen_width (*(int32_t *)0x4acc38)
#define g_screen_height (*(int32_t *)0x4acc3c)
#define g_view_current (*(uint8_t **)0x6ce60c)
#define VIEW_A ((uint8_t *)0x6cd4a0)
#define VIEW_B ((uint8_t *)0x6cdd10)
#define g_view_width (*(uint16_t *)0x6cd462)
#define g_view_height (*(uint16_t *)0x6cd464)
#define g_view_x_scale (*(int32_t *)0x6cd40c)
#define g_view_y_scale (*(int32_t *)0x6cd410)
#define g_view_angles ((int16_t *)0x4bde10)
#define g_view_matrix ((int16_t *)0x6cd480)
#define g_view_flipped (*(int32_t *)0x6cce94)
#define g_view_saved ((uint32_t *)0x4bde98)
#define DIRECTORS ((uint8_t *)0x6cf6a0)
#define DIRECTOR_SIZE 0x150

/* Track water. */
#define g_ripples_count (*(int32_t *)0x6c1eb4)
#define g_ripples (*(uint8_t **)0x6c1eb8)
#define g_waters_count (*(int32_t *)0x5d637c)
#define g_waters (*(uint8_t **)0x5d6378)

/* Pad state, ticks. */
#define PAD_A ((uint8_t *)0x6ce6c0)
#define PAD_B ((uint8_t *)0x6ce680)
#define g_tick_now (*(volatile uint32_t *)0x4be3e8) /* the timer thread's */
#define g_tick_mark (*(uint32_t *)0x4be118)

#define I32(p, off) (*(int32_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define PTR(p, off) (*(uint8_t **)((p) + (off)))
#define SHL(x, n) ((int32_t)((uint32_t)(int32_t)(x) << (n)))

typedef uint8_t *(GAMEAPI *alloc_fn)(uint32_t size);
typedef int32_t(GAMEAPI *int_fn)(void);
typedef void(GAMEAPI *void_fn)(void);
typedef void(GAMEAPI *arg_fn)(int32_t a);
typedef void(GAMEAPI *ptrs_fn)(const void *a, const void *b);
typedef void(GAMEAPI *normalise_fn)(int32_t *v, int32_t *out);
typedef void(GAMEAPI *viewport_fn)(int32_t x, int32_t y, int32_t w, int32_t h);
typedef void(GAMEAPI *ptr_fn)(const void *p);
typedef void(GAMEAPI *clear_fn)(void *p, int32_t n);
typedef void(GAMEAPI *rgb_fn)(int32_t r, int32_t g, int32_t b);
typedef uint8_t *(GAMEAPI *body_new_fn)(int32_t size);
typedef void(GAMEAPI *body_kind_fn)(uint8_t *body, int32_t kind);
typedef void(GAMEAPI *body_place_fn)(uint8_t *body, const int32_t *pos);
typedef void(GAMEAPI *body_spheres_fn)(uint8_t *body, int32_t n, const void *spheres, int32_t a);
typedef void(GAMEAPI *text_fn)(uint32_t text, int32_t a, int32_t b, int32_t y, int32_t c, int32_t d, int32_t e);

#define heap_alloc ((alloc_fn)0x4445e0)
#define heap_init ((void_fn)0x444500)
#define shield_texture_draw ((void_fn)0x46d0c0)
#define textures_unusable ((int_fn)0x429340)
#define vec_normalise_float ((normalise_fn)0x4290b0)
#define wait_frame ((void_fn)0x4291f0)
#define wait_frame_for ((ptrs_fn)0x4291f0)
#define screen_idle ((arg_fn)0x479ae0)
#define screen_idle0 ((void_fn)0x479ae0)
#define screen_ready ((int_fn)0x428060)
#define view_reset ((arg_fn)0x423f00)
#define viewport_set ((viewport_fn)0x41f2d0)
#define view_set_position ((ptr_fn)0x423ce0)
#define view_set_matrix ((ptr_fn)0x423d10)
#define view_set_lights ((void_fn)0x423d30)
#define list_clear ((clear_fn)0x427a70)
#define clear_colour_set ((rgb_fn)0x428390)
#define body_new ((body_new_fn)0x460af0)
#define body_set_kind ((body_kind_fn)0x460b40)
#define body_place ((body_place_fn)0x465640)
#define body_spheres_set ((body_spheres_fn)0x419150)
#define camera_fade ((arg_fn)0x40afb0)
#define text_create ((text_fn)0x479060)

static uint32_t effect_random(void)
{
    g_effect_seed = g_effect_seed * 16807 & 0x7fffffff;
    return ((g_effect_seed >> 15) * 0x7fff) >> 16;
}

/* ---- the shield ripple ---- */

/* 0x46d270: the texture page and the corners of the shield texture's
 * square (u | v << 8). */
void GAMEAPI shield_uv_corners(uint16_t *page, uint16_t *base, uint16_t *u_end, uint16_t *v_end, uint16_t *far)
{
    *page = (uint16_t)g_shield_texture;
    *base = 0x8080;
    *u_end = 0x80ff;
    *v_end = 0xff80;
    *far = 0xffff;
}

/* 0x46d590: link vertex a to b and c (each kept once; up to 7 per vertex). */
void GAMEAPI shield_link_add(uint32_t a, uint32_t b, uint32_t c)
{
    uint8_t *link = g_shield_links + (a & 0xff) * 8, *p;
    uint32_t n = link[0], i;

    for (i = 0, p = link + 1; (int32_t)i < (int32_t)n && *p != (uint8_t)b; i++, p++)
        ;
    if (i == n) {
        link[0] = (uint8_t)(n + 1);
        *p = (uint8_t)b;
        n++;
    }
    for (i = 0, p = link + 1; (int32_t)i < (int32_t)n && *p != (uint8_t)c; i++, p++)
        ;
    if (i == n) {
        link[0]++;
        *p = (uint8_t)c;
    }
}

/* 0x46d320: the shield mesh's vertex links and, for each of 64 directions
 * (a 4x4x4 grid of -0xc0..0xc0), the vertex pointing most that way. */
void GAMEAPI shield_mesh_links(void)
{
    uint8_t *mesh = g_shield_mesh, *links, *dirs;
    const uint8_t *poly;
    const int16_t *vertex;
    int32_t i, j;

    g_shield_vertices = U16(mesh, 4);
    g_shield_links = heap_alloc((uint32_t)U16(mesh, 4) << 5);
    g_shield_dirs = heap_alloc(0x100);
    links = g_shield_links;
    for (i = 0; i < g_shield_vertices; i++) {
        U32(links, i * 8) = 0;
        U32(links, i * 8 + 4) = 0;
    }
    mesh = g_shield_mesh;
    poly = PTR(mesh, 0xc);
    for (i = 0; i < (int32_t)U16(g_shield_mesh, 6); i++, poly += 8) {
        uint8_t a = (uint8_t)(poly[0] - 1), b = (uint8_t)(poly[1] - 1), c = (uint8_t)(poly[2] - 1);

        shield_link_add(a, b, c);
        shield_link_add(b, a, c);
        shield_link_add(c, a, b);
    }
    vertex = (const int16_t *)PTR(g_shield_mesh, 0x40);
    for (i = 0; i < 0x10; i++)
        U32(g_shield_dirs, i * 4) = 0;
    dirs = g_shield_dirs;
    for (i = 0; i < 0x40; i++, dirs++) {
        int32_t v[3], best = -1, nearest = -1;

        v[0] = (i & 3) * 0x80 - 0xc0;
        v[1] = (i >> 2 & 3) * 0x80 - 0xc0;
        v[2] = (i >> 4 & 3) * 0x80 - 0xc0;
        vec_normalise_float(v, v);
        v[0] >>= 4;
        v[1] >>= 4;
        v[2] >>= 4;
        for (j = 0; j < g_shield_vertices; j++) {
            int32_t w[3], d;

            w[0] = vertex[j * 4];
            w[1] = vertex[j * 4 + 1];
            w[2] = vertex[j * 4 + 2];
            vec_normalise_float(w, w);
            w[2] >>= 4;
            w[0] >>= 4;
            w[1] >>= 4;
            d = fix_mul8(v[0], w[0]);
            d += fix_mul8(v[1], w[1]);
            d = fix_mul8(v[2], w[2]) + d;
            if (d > best) {
                best = d;
                nearest = j;
            }
        }
        *dirs = (uint8_t)nearest;
    }
}

/* 0x46d600: a car's ripple record: its two polygon lists (0x64 bytes per
 * polygon: flags, then the three corners' texture words at +0x20/+0x40/
 * +0x60, which start as the track's shield colour words) and its vertex
 * colours, heights and speeds cleared. */
void GAMEAPI shield_record_init(uint8_t *r)
{
    uint32_t polys = U16(g_shield_mesh, 6), vertices = U16(g_shield_mesh, 4);
    uint8_t *p = heap_alloc(polys * 0xcc);
    const uint32_t *colour;
    uint32_t *c;
    int32_t n, i;

    PTR(r, 0x68) = p;
    PTR(r, 0x6c) = p + polys * 0x64;
    PTR(r, 0x70) = p + polys * 0x64 * 2;
    colour = (const uint32_t *)(g_track_colours + (U16(g_track_info, 0x2e) + 0x4e) * 0x10);
    for (n = (int32_t)(polys * 2); n > 0; n--, p += 0x64) {
        uint32_t flags = U32(p, 0);

        U8(p, 7) = 0x9e;
        U32(p, 0) = (flags & 0xffffff) | 0x9000000;
        U32(p, 0x20) = colour[0];
        U32(p, 0x40) = colour[1];
        U16(p, 0x60) = (uint16_t)colour[2];
    }
    c = (uint32_t *)PTR(r, 0x70);
    for (i = 0; i < (int32_t)vertices; i++)
        c[i] = 0;
    for (i = 0; i < (int32_t)vertices; i++) {
        r[i + 4] = 0;
        r[i + 0x34] = 0;
    }
    I32(r, 0x64) = -1;
    r[0] = 0;
    I32(r, 0x74) = -1;
    I32(r, 0x78) = -1;
    I32(r, 0x7c) = -1;
    I32(r, 0x80) = -1;
}

/* 0x46e160: texture coordinates for every ripple record's polygons, the
 * mesh's x/z extent mapped onto the texture square. */
void GAMEAPI shield_uvs(void)
{
    uint8_t *mesh = g_shield_mesh;
    uint32_t n = U16(mesh, 4);
    const int16_t *v = (const int16_t *)PTR(mesh, 0x40);
    int32_t min_x = 0x7fff, min_y = 0x7fff, min_z = 0x7fff, max_x = -0x7fff, max_y = -0x7fff, max_z = -0x7fff;
    int32_t u_scale, v_scale;
    uint16_t page, base, u_end, v_end, far;
    const uint8_t *vertex;
    uint8_t *r;

    shield_uv_corners(&page, &base, &u_end, &v_end, &far);
    for (; n; n--, v += 4) {
        int32_t x = v[0], y = v[1], z = v[2];

        if (x > max_x)
            max_x = x;
        if (y > max_y)
            max_y = y;
        if (z > max_z)
            max_z = z;
        if (x < min_x)
            min_x = x;
        if (y < min_y)
            min_y = y;
        if (z < min_z)
            min_z = z;
    }
    (void)min_y;
    (void)max_y;
    u_scale = SHL((u_end & 0xff) - (base & 0xff), 16) / (max_x - min_x);
    v_scale = SHL((v_end >> 8) - (base >> 8), 16) / (max_z - min_z);
    vertex = PTR(g_shield_mesh, 0x40) - 8;
    for (r = SHIELDS; (uint32_t)r < SHIELDS_END; r += SHIELD_SIZE) {
        uint8_t *a = PTR(r, 0x68), *b = PTR(r, 0x6c);
        const uint8_t *poly = PTR(g_shield_mesh, 0xc);
        int32_t polys = U16(g_shield_mesh, 6);

        for (; polys; polys--, poly += 8, a += 0x64, b += 0x64) {
            uint32_t corners = U32(poly, 0);
            const uint8_t *p0 = vertex + (corners & 0xff) * 8, *p1 = vertex + (corners >> 8 & 0xff) * 8,
                          *p2 = vertex + (corners >> 16 & 0xff) * 8;

            a[0x20] = (uint8_t)((S16(p0, 0) - min_x) * u_scale >> 16);
            a[0x21] = (uint8_t)((S16(p0, 4) - min_z) * v_scale >> 16);
            a[0x40] = (uint8_t)((S16(p1, 0) - min_x) * u_scale >> 16);
            a[0x41] = (uint8_t)((S16(p1, 4) - min_z) * v_scale >> 16);
            a[0x60] = (uint8_t)((S16(p2, 0) - min_x) * u_scale >> 16);
            a[0x61] = (uint8_t)((S16(p2, 4) - min_z) * v_scale >> 16);
            U16(a, 0x20) += base;
            U16(a, 0x40) += base;
            U16(a, 0x60) += base;
            U16(a, 0x42) = page;
            U16(b, 0x42) = page;
            U16(b, 0x20) = U16(a, 0x20);
            U16(b, 0x40) = U16(a, 0x40);
            U16(b, 0x60) = U16(a, 0x60);
        }
    }
}

/* 0x46d250: draw the shield texture's 64 animation frames. */
int32_t GAMEAPI shield_textures_draw(void)
{
    int32_t i;

    for (i = 0x40; i; i--)
        shield_texture_draw();
    return 1;
}

/* 0x46d2b0: set the shield ripple up for the race. */
void GAMEAPI shield_setup(void)
{
    uint8_t *r;

    g_shield_mesh = ((uint8_t **)PTR(g_track, 0x18))[S16(PTR(g_track, 0x58), 0xe)];
    shield_mesh_links();
    for (r = SHIELDS; (int32_t)r < SHIELDS_END; r += SHIELD_SIZE)
        shield_record_init(r);
    if (textures_unusable())
        g_config[0x2a] = 0;
    if (g_config[0x2a] && g_shield_texture != -1) {
        shield_uvs();
        shield_textures_draw();
    }
}

/* ---- the heap and the views ---- */

/* 0x4291c0: a rectangle of four words. */
int32_t GAMEAPI rect_set(int16_t *r, int32_t x, int32_t y, int32_t w, int32_t h)
{
    r[0] = (int16_t)x;
    r[1] = (int16_t)y;
    r[2] = (int16_t)w;
    r[3] = (int16_t)h;
    return 0;
}

/* 0x4265d0: make a view's rectangle the viewport. */
int16_t *GAMEAPI viewport_apply(int16_t *r)
{
    viewport_set(r[0], r[1], r[2], r[3]);
    return r;
}

/* 0x426600: keep a copy of a view (0x5c bytes) at 0x4bde98. */
const uint32_t *GAMEAPI view_keep(const uint32_t *view)
{
    int32_t i;

    for (i = 0; i < 0x17; i++)
        g_view_saved[i] = view[i];
    return view;
}

/* 0x423a90: the view's angles to zero and its matrix scaled (mirrored in
 * x when the view is flipped). */
void GAMEAPI view_angles_reset(void)
{
    g_view_angles[0] = 0;
    g_view_angles[1] = 0;
    g_view_angles[2] = 0;
    matrix_from_angles(g_view_angles, g_view_matrix);
    *(uint16_t *)0x6cd488 = 0x2000;
    *(uint16_t *)0x6cd490 = 0x2000;
    *(uint16_t *)0x6cd480 = (uint16_t)((g_view_flipped ? 0xc000 : 0) + 0x2000);
}

/* 0x4238c0: the two views (0x6cd4a0, 0x6cdd10): full screen rectangles,
 * the projection centre from the race's (+0xa8c, +0xa8e), the default
 * camera, and the screen scale factors. Returns what the original leaves in
 * eax. */
uint32_t GAMEAPI views_setup(void)
{
    struct {
        int32_t position[4];
        int16_t matrix[10];
    } camera = {{0, 0, 0x3e80000, 0}, {0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000, 0}};
    int16_t cx, cy;
    uint32_t w;

    g_view_current = VIEW_A;
    g_view_width = (uint16_t)g_screen_width;
    g_view_height = (uint16_t)g_screen_height;
    view_reset(0x68);
    rect_set((int16_t *)0x6cd4fc, 0, 0, g_view_width, *(uint32_t *)0x6cd464 & 0xffff);
    rect_set((int16_t *)0x6cdd6c, 0, 0, g_view_width, *(uint32_t *)0x6cd464 & 0xffff);
    cx = (int16_t)((S16(g_race, 0xa8c) >> 2) * 60 / 100);
    *(int16_t *)0x6cd504 = cx;
    *(int16_t *)0x6cdd74 = cx;
    cy = (int16_t)(S16(g_race, 0xa8e) >> 2);
    *(int16_t *)0x6cd506 = cy;
    *(int16_t *)0x6cdd76 = cy;
    *(uint16_t *)0x6cd508 = g_view_width;
    *(uint16_t *)0x6cdd78 = g_view_width;
    *(uint16_t *)0x6cd50a = g_view_height;
    *(uint16_t *)0x6cdd7a = g_view_height;
    viewport_apply((int16_t *)(g_view_current + 0x5c));
    view_keep((const uint32_t *)g_view_current);
    g_view_current = g_view_current == VIEW_A ? VIEW_B : VIEW_A;
    view_set_position(camera.position);
    view_set_matrix(camera.matrix);
    view_angles_reset();
    view_set_lights();
    w = (uint32_t)g_screen_width << 8;
    g_view_x_scale = (int32_t)w / 0x140;
    g_view_y_scale = (int32_t)w / 0x280;
    return w * 0x66666667u;
}

/* 0x423880: the views, and the three draw lists cleared. */
void GAMEAPI views_init(void)
{
    views_setup();
    list_clear((void *)0x6cd510, 0x200);
    list_clear((void *)0x6cdd80, 0x200);
    list_clear((void *)0x6ce580, 2);
}

/* 0x43c300: the leftover PlayStation screen waits (all empty). */
void GAMEAPI screen_waits(void)
{
    screen_idle(3);
    screen_idle(0);
    wait_frame();
}

/* 0x46a040: the initial heap and the views. */
void GAMEAPI heap_and_views_init(void)
{
    screen_waits();
    wait_frame();
    wait_frame();
    heap_init();
    wait_frame();
    views_init();
}

/* ---- track water ---- */

/* 0x43e050: the rippling track vertices: each of the track info's ripple
 * cells (+0x100, +0xfc of them: word row, byte section, byte column) adds
 * its four corner vertices once (0x18 byte records at 0x6c1eb8: the vertex,
 * its rest position, a phase 0, a random speed, size and direction), and
 * the top 32 palette entries become a dark-to-light ramp for them. */
void GAMEAPI ripple_vertices_setup(void)
{
    uint32_t cells = g_track_info[0xfc];
    const uint8_t *cell = PTR(g_track_info, 0x100);
    uint8_t *list, *rec;
    int32_t count = 0, i;

    g_ripples_count = 0;
    list = heap_alloc(0x6000);
    g_ripples = list;
    for (; (int32_t)cells > 0; cells--, cell += 4) {
        uint8_t *section = g_sections[cell[2]];
        uint32_t width = U32(section, 8) + 1;
        uint8_t *base = PTR(section, 0x20) + (U16(cell, 0) * width + cell[3]) * 8, *corner[4];
        int32_t k;

        corner[0] = base;
        corner[1] = base + 8;
        corner[2] = base + width * 8;
        corner[3] = base + width * 8 + 8;
        for (k = 0; k < 4; k++) {
            for (i = 0, rec = list; i < count; i++, rec += 0x18)
                if (PTR(rec, 0) == corner[k])
                    break;
            if (i < count)
                continue;
            count++;
            PTR(rec, 0) = corner[k];
            S16(rec, 6) = S16(corner[k], 2);
            S16(rec, 4) = S16(corner[k], 0);
            S16(rec, 8) = S16(corner[k], 4);
            list = g_ripples;
        }
    }
    g_ripples_count = count;
    if (!count)
        return;
    for (rec = list; count > 0; count--, rec += 0x18) {
        U16(rec, 0x10) = 0;
        U16(rec, 0x12) = (uint16_t)(effect_random() & 0xff);
        rec[0x14] = (uint8_t)(effect_random() & 3);
        rec[0x15] = (uint8_t)(effect_random() & 1);
    }
    {
        uint8_t shade = g_palette[(U16(PTR(g_ripples, 0), 6) >> 6) * 4 + 3];

        for (i = 0; i < 0x20; i++) {
            uint8_t *c = g_palette + 0xfbc - i * 4;

            c[1] = (uint8_t)i;
            c[2] = (uint8_t)i;
            c[0] = (uint8_t)((i + 8) * 4);
            c[3] = shade;
        }
    }
}

/* 0x482ae0: the resting wave height at (b, c) (a phase step a). */
int32_t GAMEAPI water_wave(int32_t a, int32_t b, int32_t c)
{
    uint32_t u = (uint32_t)(a + b * 8 + c) * 4;
    int32_t s = g_sin[u & 0xff];

    return (u >> 8 & 3) ? s >> 10 : s >> 9;
}

/* 0x482b20: the water meshes: every track section with a water band
 * (flags +6 bits 0-5: its height in rows) gets a 0x1c byte record at
 * 0x5d6378 (section, rows, columns, step direction, the band's first and
 * edge vertex rows, the section's direction scaled by 45/64), its vertices
 * get a resting wave, and the top 32 palette entries a water ramp. */
void GAMEAPI water_meshes_setup(void)
{
    uint32_t i, n;
    uint8_t *w;

    n = 0;
    for (i = 0; i < U32(g_track, 0x10); i++)
        if (((uint8_t **)PTR(g_track, 0))[i][6] & 0x3f)
            n++;
    g_waters_count = (int32_t)n;
    g_waters = 0;
    if (!n)
        return;
    w = heap_alloc(n * 0x1c);
    g_waters = w;
    for (i = 0; i < U32(g_track, 0x10); i++) {
        uint8_t *section;
        uint32_t band, row, col, stride;
        int32_t d[3], dx, dz;
        uint8_t *v;
        int16_t *e;

        if (!(((uint8_t **)PTR(g_track, 0))[i][6] & 0x3f))
            continue;
        U16(w, 0) = (uint16_t)i;
        U16(w, 2) = 0;
        band = U16(((uint8_t **)PTR(g_track, 0))[i], 6) & 0x3f;
        U16(w, 0xa) = (uint16_t)band;
        section = g_sections[i & 0xffff];
        U16(w, 4) = (uint16_t)(U16(section, 8) - band + 1);
        U16(w, 6) = U16(section, 0xc);
        U16(w, 8) = (section[6] & 0x40) ? 1 : 0xffff;
        PTR(w, 0xc) = PTR(section, 0x20);
        if (U16(w, 8) == 1) {
            PTR(w, 0xc) = PTR(section, 0x20) + band * 8;
            PTR(w, 0x10) = PTR(w, 0xc) - 8;
        } else {
            PTR(w, 0x10) = PTR(w, 0xc) + U16(w, 4) * 8;
        }
        v = PTR(w, 0xc);
        for (row = 0; row < U16(w, 6); row++) {
            for (col = 0; col < U16(w, 4); col++, v += 8)
                U16(v, 2) += (uint16_t)water_wave(0, (int32_t)col, (int32_t)row);
            v += band * 8;
        }
        {
            uint32_t width = U32(section, 8);
            const uint8_t *mid = PTR(section, 0x20) + (U32(section, 0xc) >> 1) * (width + 1) * 8;

            d[0] = -S16(mid, 0) + S16(mid, width * 8);
            d[1] = -S16(mid, 2) + S16(mid, width * 8 + 2);
            d[2] = -S16(mid, 4) + S16(mid, width * 8 + 4);
        }
        vec_normalise_shifted(d, d);
        e = (int16_t *)PTR(w, 0x10);
        S16(w, 0x14) = (int16_t)(d[0] * 45 >> 6);
        S16(w, 0x18) = (int16_t)(d[2] * 45 >> 6);
        S16(w, 0x16) = (int16_t)(d[1] * 45 >> 6);
        dx = S16(w, 0x14);
        dz = S16(w, 0x18);
        stride = (U16(w, 4) + U16(w, 0xa)) * 8;
        for (row = 0; row < U16(w, 6); row++) {
            int32_t h = water_wave(0, -1, (int32_t)row);

            e[0] = (int16_t)(e[0] + (h * dx >> 9));
            e[2] = (int16_t)(e[2] + (h * dz >> 9));
            e = (int16_t *)((uint8_t *)e + stride);
        }
        w += 0x1c;
    }
    if (g_waters_count) {
        uint8_t shade = g_palette[(U16(PTR(g_waters, 0xc), 6) >> 6) * 4 + 3], level = 0x28;

        for (i = 0; i < 0x20; i++, level += 3) {
            uint8_t *c = g_palette + 0xfbc - i * 4;

            c[0] = level;
            c[1] = level;
            c[2] = level;
            c[3] = shade;
        }
    }
}

/* 0x41aa60: the track's water. */
void GAMEAPI track_water_setup(void)
{
    ripple_vertices_setup();
    water_meshes_setup();
}

/* ---- cameras ---- */

/* 0x406be0: a car's chase camera view record (0x5c bytes): its camera
 * body placed at the start spot (index's entry of 0x6bfb08, raised), the
 * camera kind and the field of view from the race (+0xa90.., kind tables
 * 0x4a77e4 / 0x4a77ec), and the viewports' director state reset. */
void GAMEAPI camera_view_setup(int32_t index, int32_t car, int32_t *cam)
{
    const int16_t *spot = (const int16_t *)(*(uint8_t **)0x6bfb08 + index * 8);
    int32_t *pos = (int32_t *)0x4a76f8, i;
    uint8_t *c = (uint8_t *)cam, *body;
    int16_t kind;

    for (i = 0; i < 0x17; i++)
        cam[i] = 0;
    pos[0] = SHL(spot[0], 16);
    pos[1] = SHL(spot[1] - 0x3f, 16);
    pos[2] = SHL(spot[2] + 0xa0, 16);
    body = body_new(0x180);
    PTR(c, 0) = body;
    body_set_kind(body, 0x804);
    body_place(PTR(c, 0), pos);
    I32(PTR(c, 0), 0x108) = 1;
    I32(PTR(c, 0), 0xb4) = 0;
    body_spheres_set(PTR(c, 0), 1, (const void *)0x4a77d0, 0);
    cam[0xe] = 0;
    if (index == 0) {
        S16(c, 0x40) = (int8_t)g_race[0xa90];
        S16(c, 0x5a) = (int8_t)g_race[0xa92];
    } else {
        S16(c, 0x40) = (int8_t)g_race[0xa91];
        S16(c, 0x5a) = (int8_t)g_race[0xa93];
    }
    kind = S16(c, 0x40);
    S16(c, 0x58) = 0;
    S16(c, 0x44) = *(const int16_t *)(0x4a77e4 + kind * 2);
    S16(c, 0x46) = *(const int16_t *)(0x4a77ec + kind * 2);
    S16(c, 0x42) = 0;
    S16(c, 0x48) = (int16_t)(S16(c, 0x44) / 2);
    c[0x51] = 0;
    cam[0x15] = 0;
    S16(c, 0x4a) = (int16_t)(S16(c, 0x46) / 2);
    cam[1] = car;
    c[0x50] = 3;
    c[0x4c] = 3;
    c[0x4d] = 3;
    c[0x52] = 0;
    c[0x53] = 0;
    cam[2] = 0;
    cam[4] = 0;
    cam[3] = (int32_t)0xffff0000;
    PTR(PTR(c, 0), 0x164) = c;
    for (i = 0; i < 0x150; i++)
        ((uint32_t *)DIRECTORS)[i] = 0;
    for (i = 0; i < 4; i++) {
        uint8_t *d = DIRECTORS + i * DIRECTOR_SIZE;
        uint8_t mode;

        U16(d, 0x2c) = 0;
        U16(d, 0x32) = 0;
        U32(d, 0x20) = 0;
        U16(d, 0x36) = 0;
        U32(d, 0x3c) = 0;
        U16(d, 0x3a) = 0xffff;
        U32(d, 0) = 0xe;
        U16(d, 0x34) = 0x96;
        if (i == 0 && ((mode = g_race[0x551]) == 5 || mode == 6 || mode == 7 || mode == 8))
            U32(d, 0x144) = g_config[0x84];
        else
            I32(d, 0x144) = -1;
    }
    camera_fade(1);
}

/* ---- the weather tint ---- */

/* 0x404940: scale an r, g, b triple by r/256, g/256, b/256. */
uint8_t *GAMEAPI rgb_scale(uint8_t *c, int32_t r, int32_t g, int32_t b)
{
    c[0] = (uint8_t)((int32_t)c[0] * r >> 8);
    c[1] = (uint8_t)((int32_t)c[1] * g >> 8);
    c[2] = (uint8_t)((int32_t)c[2] * b >> 8);
    return c;
}

/* 0x4047e0: tint the colour tables (the 64 skies at 0x4b5fb0, the lights
 * 0x4b5fc8 / 0x4b5fcc, the 32 effects 0x4b5fdc), the track's fog colour
 * (track info +0x30, doubled into both views) and 0x4b5fd0, then set the
 * clear colour. */
void GAMEAPI tint_colour_tables(int32_t r, int32_t g, int32_t b)
{
    uint8_t *c;
    uint32_t i;

    c = *(uint8_t **)0x4b5fb0 + 4;
    for (i = 0x40; i; i--, c += 0x78)
        rgb_scale(c, r, g, b);
    c = *(uint8_t **)0x4b5fc8 + 4;
    for (i = 0; i < *(uint32_t *)0x6d0a78; i++, c += 0x78)
        rgb_scale(c, r, g, b);
    c = *(uint8_t **)0x4b5fcc + 4;
    for (i = 0; i < *(uint32_t *)0x6d0a00; i++, c += 0x78)
        rgb_scale(c, r, g, b);
    c = *(uint8_t **)0x4b5fdc + 4;
    for (i = 0x20; i; i--, c += 0x60)
        rgb_scale(c, r, g, b);
    rgb_scale(g_track_info + 0x30, r, g, b);
    c = g_track_info;
    *(uint8_t *)0x6cd4b9 = (uint8_t)(c[0x30] << 1);
    *(uint8_t *)0x6cdd29 = (uint8_t)(c[0x30] << 1);
    *(uint8_t *)0x6cd4ba = (uint8_t)(c[0x31] << 1);
    *(uint8_t *)0x6cdd2a = (uint8_t)(c[0x31] << 1);
    *(uint8_t *)0x6cd4bb = (uint8_t)(c[0x32] << 1);
    *(uint8_t *)0x6cdd2b = (uint8_t)(c[0x32] << 1);
    rgb_scale((uint8_t *)0x4b5fd0, r, g, b);
    c = g_track_info;
    clear_colour_set((uint8_t)(c[0x30] << 1), (uint8_t)(c[0x31] << 1), (uint8_t)(c[0x32] << 1));
}

/* 0x485070: the weather tint (race +0x553: 1 a blue dusk, 2 a purple
 * night) over the track palette and the colour tables. */
void GAMEAPI weather_tint_setup(void)
{
    uint8_t weather = g_race[0x553], *c = PTR(g_track_info, 0x4c);
    int32_t r, g, b, i;

    if (!weather)
        return;
    if (weather == 1) {
        r = 0x80;
        b = 0x90;
        g = 0x80;
    } else {
        r = 0xc0;
        g = 0x60;
        b = 0xc0;
    }
    for (i = 0x400; i; i--, c += 4) {
        c[0] = (uint8_t)((int32_t)c[0] * r >> 8);
        c[1] = (uint8_t)((int32_t)c[1] * g >> 8);
        c[2] = (uint8_t)((int32_t)c[2] * b >> 8);
    }
    tint_colour_tables(r, g, b);
}

/* ---- small ones ---- */

/* 0x41b8e0: clear a pad state (13 words). */
void GAMEAPI pad_state_clear(uint32_t *p)
{
    int32_t i;

    for (i = 0; i < 0xd; i++)
        p[i] = 0;
}

/* 0x429220: n 0 waits a tick, 1 returns the time since the last call
 * (ticks * 0x107), more than 1 waits n ticks; all mark the time. */
int32_t GAMEAPI tick_sync(int32_t n)
{
    int32_t r, i;

    if (n == 0) {
        wait_frame();
        g_tick_mark = g_tick_now;
        return 0;
    }
    if (n == 1) {
        uint32_t now = g_tick_now;

        r = (int32_t)(now - g_tick_mark) * 0x107;
        g_tick_mark = g_tick_now;
        return r;
    }
    if (n < 1) {
        r = (int32_t)g_tick_now;
        g_tick_mark = g_tick_now;
        return r;
    }
    for (i = n; i; i--)
        wait_frame();
    g_tick_mark = g_tick_now;
    return n;
}

/* 0x41b370: both pads' states cleared (a pad whose second byte is 0x80
 * is flagged at 0x6ce6f4 / 0x6ce6f8). */
int32_t GAMEAPI pads_reset(void)
{
    const uint8_t *a, *b;

    screen_idle0();
    tick_sync(3);
    wait_frame_for(PAD_A, PAD_B);
    tick_sync(3);
    screen_ready();
    tick_sync(3);
    if (PAD_A[1] == 0x80) {
        pad_state_clear((uint32_t *)PAD_A);
        a = PAD_A;
        *(int32_t *)0x6ce6f4 = 1;
    } else {
        pad_state_clear((uint32_t *)PAD_A);
        a = PAD_A + 2;
    }
    if (PAD_B[1] == 0x80) {
        pad_state_clear((uint32_t *)PAD_B);
        b = PAD_B;
        *(int32_t *)0x6ce6f8 = 1;
    } else {
        pad_state_clear((uint32_t *)PAD_B);
        b = PAD_B + 2;
    }
    screen_idle0();
    tick_sync(3);
    wait_frame_for(a, b);
    tick_sync(3);
    screen_ready();
    tick_sync(3);
    return 0;
}

/* 0x463860: the start messages: mode 1 (attract) makes every car an AI
 * (race +0..) with a 30 s demo timer; otherwise a 99 count and two lines. */
void GAMEAPI race_start_messages(int32_t mode)
{
    if (mode == 1) {
        uint32_t i;

        for (i = 0; i < g_race[0x54d]; i++)
            g_race[i] = 8;
        U16(g_race, 0x58e) = 0x708;
        text_create(*(uint32_t *)0x4d1134, 0, 0, g_view_y_scale * 0x3c >> 8, 7, 0, 0);
        return;
    }
    U16(g_race, 0x58e) = 0x63;
    text_create(*(uint32_t *)0x4d113c, 0, 0, g_view_y_scale * 0x3c >> 8, 7, 0, 0);
    text_create(*(uint32_t *)0x4d1140, 0, 0, g_view_y_scale * 0x52 >> 8, 0x26, 0, 0);
}

/* 0x449ca0: player i's (0..3) car choice from the options, or -1. */
int32_t GAMEAPI player_car_choice(int32_t i)
{
    if (i >= 0 && i < 4)
        return g_config[i * 0x90 + 0x1a1];
    return -1;
}

/* 0x47b140: clear the engine glow's flash state (0x5d92a0..0x5d92a6). */
void GAMEAPI glow_flash_reset(void)
{
    *(uint32_t *)0x5d92a0 = 0;
    *(uint16_t *)0x5d92a4 = 0;
    *(uint8_t *)0x5d92a6 = 0;
}
