/* Group PI: the original's still-reachable functions in 0x453220-0x455e90
 * (the front end's menu box and 3D menu model source file).
 *
 *  - the menu box: its three backing quads (0x453220, two buffers of three
 *    0x78-byte quads at 0x4cf7c8, linked into an ordering table), the side
 *    panel quad (0x453570 set-up, 0x4535b0 placing, two buffers of 0x68
 *    bytes at 0x4cd880), the two text box frames' set-up (0x453710);
 *  - the 3D menu models: one model drawn with a matrix and a position
 *    (0x4538e0), and a model with the four corner models around it
 *    (0x453a00): each model's rotation is the chain of the caller's angles,
 *    the record's angles (+0x38), its spin (+0x40, plus offsets), its matrix
 *    (+8) and the caller's matrix, multiplied a column at a time through the
 *    GTE emulation (0x427fc0); the corner models facing away (by the facing
 *    value of a little triangle at the top point 0x4cfe98 in camera space)
 *    are drawn before the middle model, the rest after it.
 *
 * Float steps are the original's x87 instructions in inline asm: int -> float
 * (fild / fstp), the GTE matrix load's int16 * scale (fild / fmul / fstp),
 * the facing value (0x4278f0, returned in st(0), stored with fstp) and its
 * compare (fcomp / fnstsw). The transform / projection pair *0x6ccd78 /
 * *0x6ccd80 passes st(0) between them, so it is called from asm. Everything
 * outside the group, and the group's own 0x4538e0 from 0x453a00, is called
 * by address. */
#include "../runtime.h"
#include "gamefn.h"
#include "physics.h"

#define U16(p, off) (*(uint16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define S16(p, off) (*(int16_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define I32(p, off) (*(int32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define U32(p, off) (*(uint32_t *)((uint8_t *)(uintptr_t)(p) + (off)))
#define G16(a) (*(uint16_t *)(uintptr_t)(a))
#define G32(a) (*(uint32_t *)(uintptr_t)(a))
#define GI32(a) (*(int32_t *)(uintptr_t)(a))
#define PTR(a) ((uint8_t *)(uintptr_t)(a))
#define A(x) ((uint32_t)(uintptr_t)(x))

#define g_screen_w GI32(0x4acc38)
#define g_screen_h GI32(0x4acc3c)

#define fade_scale_at(v) GCALL(0x4328a0, fade_scale)(v)
#define fe_attract_left_at(v) GCALL(0x448030, fe_attract_left)(v)
#define word_exchange_at(a, b, c) GCALL(0x429080, word_exchange)(a, b, c)
#define ot_add_prim_at(a, b) GCALL(0x427ab0, ot_add_prim)(a, b)
#define fe_quad_flat_set_at(p, c, x, y, w, h, z) GCALL(0x4314d0, fe_quad_flat_set)(p, c, x, y, w, h, z)
#define fe_frame_sprites_init_at(p) GCALL(0x4316d0, fe_frame_sprites_init)(p)
#define menu_model_find_at(id) GCALL(0x4539d0, menu_model_find)(id)
#define matrix_from_angles_at(a, m) GCALL(0x426f80, matrix_from_angles)((const int16_t *)(a), m)
#define gte_rotate_short_at() GCALL(0x427fc0, gte_rotate_short)()
#define gte_rotate_at() GCALL(0x428090, gte_rotate)()
/* 0x426620: src/game/orig_pc.c's gte_rt_float (an asm entry on i386, no
 * arguments either way). */
#define gte_rt_float_at() ((void(GAMEAPI *)(void))(uintptr_t)0x426620)()
#define menu_model_draw_at(id, m, p, l, f) GCALL(0x4538e0, menu_model_draw)(id, (const uint32_t *)(m), p, l, f)

/* This group's functions (also in build/groups/pi_protos.txt for physics.h). */
void GAMEAPI menu_box_quads_draw(uint32_t ot, int32_t page); /* 0x453220 */
void GAMEAPI fe_side_panel_init(void); /* 0x453570 */
void GAMEAPI fe_side_panel_draw(uint32_t ot, int32_t page, int32_t x, int32_t y); /* 0x4535b0 */
void GAMEAPI fe_corner_frames_init(void); /* 0x453710 */
void GAMEAPI menu_model_draw(int32_t id, const uint32_t *matrix, const int32_t *pos, uint32_t light,
                             uint32_t flags); /* 0x4538e0 */
void GAMEAPI menu_model_corners_draw(int32_t id, const uint32_t *matrix, const int32_t *pos, uint32_t light,
                                     const uint32_t *angles, const int16_t *spin); /* 0x453a00 */

/* Every x87 register, for asm blocks that call out. */
#define X87 "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)"

/* ---- x87 helpers: the original's instructions ---- */

/* fild dword; fstp dword: an integer to float, rounded by the control word. */
static inline uint32_t pi_fild(uint32_t v)
{
    uint32_t r;
    __asm__ volatile("fildl %1\n\tfstps %0" : "=m"(r) : "m"(v) : "st");
    return r;
}

/* fild word w; fmul dword k; fstp (the original's fstp / fld of the integer
 * as a float in between is exact for 16-bit values). */
static inline uint32_t pi_s16_mul(uint16_t w, uint32_t k)
{
    uint32_t r;
    __asm__ volatile("filds %1\n\tfmuls %2\n\tfstps %0" : "=m"(r) : "m"(w), "m"(k) : "st");
    return r;
}

/* fld a; fcomp b; fnstsw ax: C0 (below or unordered). */
static inline int pi_below(uint32_t a, uint32_t b)
{
    uint16_t sw;
    __asm__ volatile("flds %1\n\tfcomps %2\n\tfnstsw %0" : "=a"(sw) : "m"(a), "m"(b) : "st", "cc");
    return (sw >> 8) & 1;
}

/* The transform *0x6ccd78 (vertex record out, the GTE vector 0x6ccc64 in)
 * then the projection *0x6ccd80, which takes the transform's st(0). */
static inline void pi_transform_project(void *out)
{
#if defined(__i386__)
    __asm__ volatile("pushl %0\n\tcall *0x6ccd78\n\tcall *0x6ccd80\n\taddl $4, %%esp"
                     :
                     : "S"(out)
                     : "eax", "ecx", "edx", "cc", "memory", X87);
#else
    /* x86-64: the transform (*0x6ccd78) returns what the original leaves in
     * st(0); the projection (*0x6ccd80) takes it as its argument. */
    float h = ((float(GAMEAPI *)(uint8_t *))(uintptr_t)G32(0x6ccd78))((uint8_t *)out);

    ((void(GAMEAPI *)(float))(uintptr_t)G32(0x6ccd80))(h);
#endif
}

/* 0x4278f0(a, b, c): the facing value of three projected vertex records,
 * returned in st(0), stored as a single (fstp dword). */
typedef float(GAMEAPI *pi_facing_fn)(void *a, void *b, void *c);

static void pi_facing(uint32_t *out, void *a, void *b, void *c)
{
    float f = ((pi_facing_fn)(uintptr_t)0x4278f0)(a, b, c);

    memcpy(out, &f, 4);
}

/* Screen scaling from 512 x 256: v * size / 512 (or / 256), truncated. */
static inline int32_t pi_sx(int32_t v)
{
    return v * g_screen_w / 512;
}

static inline int32_t pi_sy(int32_t v)
{
    return v * g_screen_h / 256;
}

/* ===================================================================== */
/* The menu box. */

/* 0x453220: the menu box's three backing quads in buffer `page` (0x168
 * bytes at 0x4cf7c8 + page * 0x168: three 0x78-byte quads, vertex records
 * of 0x1c bytes from +8: +0 0x40, +4 z, +8 x, +c y), placed from the box
 * position (0x4b0608 + 0x4be334, 0x4b060c + 0x4be338 less the faded 0x80)
 * scaled from 512 x 256; z 4500, 4500, 4000. Then linked into the ordering
 * table entry `ot` (0x429080 with the first and last quad). */
void GAMEAPI menu_box_quads_draw(uint32_t ot, int32_t page)
{
    int32_t x = GI32(0x4b0608) + GI32(0x4be334);
    int32_t lift = fade_scale_at(0x80);
    int32_t y = GI32(0x4b060c) + GI32(0x4be338) - lift;
    uint8_t *b = PTR(0x4cf7c8 + (page != 0) * 0x168);
    uint32_t x1, y1, x2, y2, x3, y3, y4, x5, y5, x6, y6;

    x1 = pi_fild(pi_sx(x + 1));
    y1 = pi_fild(pi_sy(y - 0x3d));
    U32(b, 0x10) = x1;
    U32(b, 0x14) = y1;
    x2 = pi_fild(pi_sx(x + 0x21));
    U32(b, 0x2c) = x2;
    U32(b, 0x30) = y1;
    U32(b, 0x48) = x1;
    y2 = pi_fild(pi_sy(y + 4));
    U32(b, 0x4c) = y2;
    U32(b, 0x64) = x2;
    U32(b, 0x68) = y2;
    U32(b, 0x0c) = 0x458ca000;
    U32(b, 0x08) = 0x40;
    U32(b, 0x28) = 0x458ca000;
    U32(b, 0x24) = 0x40;
    U32(b, 0x44) = 0x458ca000;
    U32(b, 0x40) = 0x40;
    U32(b, 0x60) = 0x458ca000;
    U32(b, 0x5c) = 0x40;

    U32(b, 0x88) = 0;
    y3 = pi_fild(pi_sy(y - 0x30));
    U32(b, 0x8c) = y3;
    x3 = pi_fild(pi_sx(x));
    U32(b, 0xa4) = x3;
    U32(b, 0xa8) = y3;
    U32(b, 0xc0) = 0;
    y4 = pi_fild(pi_sy(y));
    U32(b, 0xc4) = y4;
    U32(b, 0xdc) = x3;
    U32(b, 0xe0) = y4;
    U32(b, 0x84) = 0x458ca000;
    U32(b, 0x80) = 0x40;
    U32(b, 0xa0) = 0x458ca000;
    U32(b, 0x9c) = 0x40;
    U32(b, 0xbc) = 0x458ca000;
    U32(b, 0xb8) = 0x40;
    U32(b, 0xd8) = 0x458ca000;
    U32(b, 0xd4) = 0x40;

    x5 = pi_fild(pi_sx(x - 0x7d));
    U32(b, 0x100) = x5;
    y5 = pi_fild(pi_sy(y - 0x37));
    U32(b, 0x104) = y5;
    x6 = pi_fild(pi_sx(x + 2));
    U32(b, 0x11c) = x6;
    U32(b, 0x120) = y5;
    U32(b, 0x138) = x5;
    y6 = pi_fild(pi_sy(y + 0xb));
    U32(b, 0x13c) = y6;
    U32(b, 0x154) = x6;
    U32(b, 0x158) = y6;
    U32(b, 0xfc) = 0x457a0000;
    U32(b, 0xf8) = 0x40;
    U32(b, 0x118) = 0x457a0000;
    U32(b, 0x114) = 0x40;
    U32(b, 0x134) = 0x457a0000;
    U32(b, 0x130) = 0x40;
    U32(b, 0x150) = 0x457a0000;
    U32(b, 0x14c) = 0x40;
    word_exchange_at(ot, A(b), (uint32_t *)(b + 0xf0));
}

/* 0x453570: the side panel quad: an untextured 1 x 1 quad (0x4314d0) in
 * the backdrop colour 0x4ac99c at z 2000, at 0x4cd880, copied to the
 * second buffer 0x4cd8e8 (0x68 bytes). */
void GAMEAPI fe_side_panel_init(void)
{
    uint32_t *src = (uint32_t *)PTR(0x4cd880), *dst = (uint32_t *)PTR(0x4cd8e8);
    int i;

    fe_quad_flat_set_at(PTR(0x4cd880), PTR(0x4ac99c), 0, 0, 1, 1, 0x44fa0000);
    for (i = 0; i < 0x1a; i++)
        dst[i] = src[i];
}

/* 0x4535b0: the side panel quad of buffer `page` (0x4cd880 + page * 0x68)
 * placed at x + 0x1ff .. x + 0x200 (scaled from 512) and y .. y + the
 * attract demo's offset (0x448030(0x100)) (scaled from 256), z 2000, added
 * to the ordering table entry `ot`. */
void GAMEAPI fe_side_panel_draw(uint32_t ot, int32_t page, int32_t x, int32_t y)
{
    int32_t left = fe_attract_left_at(0x100);
    uint8_t *q = PTR(0x4cd880 + (page != 0) * 0x68);
    uint32_t x1, y1, x2, y2;

    x1 = pi_fild(pi_sx(x + 0x1ff));
    y1 = pi_fild(pi_sy(y));
    U32(q, 0x10) = x1;
    U32(q, 0x14) = y1;
    x2 = pi_fild(pi_sx(x + 0x200));
    U32(q, 0x28) = x2;
    U32(q, 0x2c) = y1;
    U32(q, 0x40) = x1;
    y2 = pi_fild(pi_sy(left + y));
    U32(q, 0x44) = y2;
    U32(q, 0x58) = x2;
    U32(q, 0x5c) = y2;
    U32(q, 0x0c) = 0x44fa0000;
    U32(q, 0x08) = 0x40;
    U32(q, 0x24) = 0x44fa0000;
    U32(q, 0x20) = 0x40;
    U32(q, 0x3c) = 0x44fa0000;
    U32(q, 0x38) = 0x40;
    U32(q, 0x54) = 0x44fa0000;
    U32(q, 0x50) = 0x40;
    ot_add_prim_at(ot, (uint32_t *)q);
}

/* 0x453710: the two text box frames' sprites (0x4316d0 at 0x4cfea0 and
 * 0x4d02d8). */
void GAMEAPI fe_corner_frames_init(void)
{
    fe_frame_sprites_init_at(PTR(0x4cfea0));
    fe_frame_sprites_init_at(PTR(0x4d02d8));
}

/* ===================================================================== */
/* The 3D menu models. */

/* 0x4538e0: draw the menu model with this id (record at 0x4cba80, body at
 * +4): the body's matrix (+0xc4, 0x20 bytes) and position (+0x34) set, the
 * front end's work area halves published (0x6c1e18 = 0x4beb84, 0x6c1e24 =
 * 0x6c1e34), its depth (0x45a2a0), lighting (0x45b200, 0x45b400), then
 * drawn: with flags & 1 (in the 3D front end, 0x6cce7c == 0) by level of
 * detail into ordering table entry depth / 32 (+ 10 with flags & 2, at
 * least 1) of 0x6cd510 (0x4226d0), otherwise 0x45a430. No record or no
 * body: nothing. */
void GAMEAPI menu_model_draw(int32_t id, const uint32_t *matrix, const int32_t *pos, uint32_t light,
                             uint32_t flags)
{
    uint8_t *rec;
    uint8_t *body;
    int32_t depth, e;
    int i;

    for (rec = PTR(0x4cba80); rec < PTR(0x4cd6a0); rec += 0x48)
        if (I32(rec, 0) == id)
            break;
    if (rec >= PTR(0x4cd6a0))
        return;
    body = PTR(U32(rec, 4));
    if (!body)
        return;
    for (i = 0; i < 8; i++)
        U32(body, 0xc4 + 4 * i) = matrix[i];
    I32(body, 0x34) = pos[0];
    I32(body, 0x38) = pos[1];
    I32(body, 0x3c) = pos[2];
    G32(0x6c1e18) = G32(0x4beb84);
    G32(0x6c1e24) = G32(0x6c1e34);
    depth = GCALL(0x45a2a0, model_view_depth)(body);
    GCALL(0x45b200, light_colours_set)((const uint32_t *)GMEM(light));
    GCALL(0x45b400, model_lights_set)(body, (const uint32_t *)GMEM(light));
    if ((flags & 1) && G32(0x6cce7c) == 0) {
        e = depth >> 5;
        if (flags & 2)
            e += 10;
        if (e < 1)
            e = 1;
        GCALL(0x4226d0, model_draw_lod)(A(body), (uint32_t)(0x6cd510 + 4 * e), depth);
        return;
    }
    GCALL(0x45a430, model_faces_draw)(body, depth);
}

/* Load an int16 3x3 GTE matrix (five dwords to 0x6ccbe0) and its floats
 * (each * the scale 0x4a3310, rows of four at 0x6ccd1c). */
static void pi_gte_matrix_load(const void *m)
{
    static const uint32_t dst[9] = {0x6ccd1c, 0x6ccd20, 0x6ccd24, 0x6ccd2c, 0x6ccd30,
                                    0x6ccd34, 0x6ccd3c, 0x6ccd40, 0x6ccd44};
    const uint32_t *s = (const uint32_t *)m;
    int i;

    for (i = 0; i < 5; i++)
        G32(0x6ccbe0 + 4 * i) = s[i];
    for (i = 0; i < 9; i++)
        G32(dst[i]) = pi_s16_mul(G16(0x6ccbe0 + 2 * i), G32(0x4a3310));
}

/* out = GTE matrix * in, a column at a time through the short vector at
 * 0x6ccc88 (0x427fc0); in may be out. */
static void pi_gte_matrix_mul(const int16_t *in, int16_t *out)
{
    int j;

    for (j = 0; j < 3; j++) {
        G16(0x6ccc88) = (uint16_t)in[j];
        G16(0x6ccc8c) = (uint16_t)in[3 + j];
        G16(0x6ccc90) = (uint16_t)in[6 + j];
        gte_rotate_short_at();
        out[j] = (int16_t)G16(0x6ccc88);
        out[3 + j] = (int16_t)G16(0x6ccc8c);
        out[6 + j] = (int16_t)G16(0x6ccc90);
    }
}

/* Local MATRIX-sized buffers (0x20 bytes, int16 3x3 then padding). The
 * original's are stack slots it never fully writes: the padding (and m[9],
 * which 0x426f80 leaves) is read back as whole dwords into 0x6ccbf0 and
 * copied to the body's matrix (0x4538e0 copies 0x20 bytes); here they are
 * zero. */
struct pi_state {
    int16_t a[16], b[16], c[16], m[16], n[16];
    int32_t t[3];
    uint32_t angles[2];
    uint32_t facing[4];
    uint8_t va[0x18], vb[0x18], vc[0x18];
};

/* A menu model's full rotation into s->m and position into s->t: the GTE
 * matrix chain caller matrix * record matrix (+8) * spin (+0x40 plus the
 * offsets) * record angles (+0x38) * caller angles, then the record's corner
 * offset (+0x28) rotated (0x428090) by it, << 16, plus pos. */
static void pi_model_place(struct pi_state *s, uint8_t *rec, const uint32_t *angles, const uint32_t *matrix,
                           const int32_t *pos, const int16_t *spin)
{
    int16_t ang[4];

    *(uint32_t *)&ang[0] = U32(rec, 0x40);
    ang[0] = (int16_t)(ang[0] + spin[0]);
    ang[1] = (int16_t)(ang[1] + spin[1]);
    *(uint32_t *)&ang[2] = U32(rec, 0x44);
    ang[2] = (int16_t)(ang[2] + spin[2]);
    matrix_from_angles_at(angles, s->a);
    matrix_from_angles_at(rec + 0x38, s->b);
    matrix_from_angles_at(ang, s->c);
    pi_gte_matrix_load(s->a);
    pi_gte_matrix_mul(s->b, s->m);
    pi_gte_matrix_load(s->c);
    pi_gte_matrix_mul(s->m, s->m);
    pi_gte_matrix_load(rec + 8);
    pi_gte_matrix_mul(s->m, s->m);
    pi_gte_matrix_load(matrix);
    pi_gte_matrix_mul(s->m, s->m);
    pi_gte_matrix_load(s->m);
    G32(0x6ccc64) = U32(rec, 0x28);
    G32(0x6ccc68) = U32(rec, 0x2c);
    gte_rotate_at();
    s->t[0] = (int32_t)((uint32_t)pos[0] + (G32(0x6cccc8) << 16));
    s->t[1] = (int32_t)((uint32_t)pos[1] + (G32(0x6ccccc) << 16));
    s->t[2] = (int32_t)((uint32_t)pos[2] + (G32(0x6cccd0) << 16));
}

/* The GTE translation (0x6ccbf4, ints and floats at 0x6ccd0c) from three
 * ints. */
static void pi_gte_translation_set(uint32_t x, uint32_t y, uint32_t z)
{
    G32(0x6ccbf4) = x;
    G32(0x6ccbf8) = y;
    G32(0x6ccbfc) = z;
    G32(0x6ccd0c) = pi_fild(G32(0x6ccbf4));
    G32(0x6ccd10) = pi_fild(G32(0x6ccbf8));
    G32(0x6ccd14) = pi_fild(G32(0x6ccbfc));
}

/* 0x453a00: draw menu model `id` and, if it is one of the seven listed at
 * 0x4b09f0 with a corner set (first id at 0x4b0a0c, four ids in a row),
 * its four corner models, each placed with pi_model_place. For each corner
 * model, the facing value (0x4278f0) of a triangle at the top point
 * (*0x4cfe98: x, y, z; z negated when the corner is further back (+0x2c)
 * than the middle model; + 100 in x, - 100 in y) in camera space (0x6ce5d0
 * matrix and translation, the model's position by 0x426620): below 0x4a3318
 * (or unordered) it is drawn now (flags 1); the middle model is drawn next
 * (flags 1), then the remaining corners (flags 3). */
void GAMEAPI menu_model_corners_draw(int32_t id, const uint32_t *matrix, const int32_t *pos, uint32_t light,
                                     const uint32_t *angles, const int16_t *spin)
{
    struct pi_state s;
    int32_t first = 0, cid;
    uint8_t *rec, *centre;
    int i, k;

    memset(&s, 0, sizeof s);
    for (i = 0; i < 7; i++)
        if (id == GI32(0x4b09f0 + 4 * i))
            break;
    if (i < 7) {
        first = GI32(0x4b0a0c + 4 * i);
        if (first != 0) {
            centre = menu_model_find_at(id);
            cid = first;
            for (k = 0; k < 4; k++, cid++) {
                uint8_t *top;
                uint32_t xy, w, xyb, xyc;

                rec = menu_model_find_at(cid);
                s.angles[0] = angles[0];
                s.angles[1] = angles[1];
                pi_model_place(&s, rec, s.angles, matrix, pos, spin);

                /* camera space */
                pi_gte_matrix_load(PTR(0x6ce5d0));
                pi_gte_matrix_mul(s.m, s.n);
                pi_gte_translation_set(G32(0x6ce5e4), G32(0x6ce5e8), G32(0x6ce5ec));
                G16(0x6ccc68) = (uint16_t)((uint32_t)s.t[2] >> 16);
                G16(0x6ccc64) = (uint16_t)((uint32_t)s.t[0] >> 16);
                G16(0x6ccc66) = (uint16_t)((uint32_t)s.t[1] >> 16);
                gte_rt_float_at();
                pi_gte_translation_set(G32(0x6cccc8), G32(0x6ccccc), G32(0x6cccd0));
                pi_gte_matrix_load(s.n);

                top = PTR(G32(0x4cfe98));
                xy = U32(top, 0);
                w = U32(top, 4);
                if (S16(rec, 0x2c) > S16(centre, 0x2c))
                    w = (w & 0xffff0000u) | ((0u - w) & 0xffff);
                xyb = (xy & 0xffff0000u) | ((xy + 100) & 0xffff);
                xyc = (xy & 0xffff) | ((((xy >> 16) - 100) & 0xffff) << 16);
                G32(0x6ccc64) = xy;
                G32(0x6ccc68) = w;
                pi_transform_project(s.va);
                G32(0x6ccc64) = xyb;
                G32(0x6ccc68) = w;
                pi_transform_project(s.vb);
                G32(0x6ccc64) = xyc;
                G32(0x6ccc68) = w;
                pi_transform_project(s.vc);
                if (S16(rec, 0x2c) > S16(centre, 0x2c))
                    pi_facing(&s.facing[k], s.vb, s.va, s.vc);
                else
                    pi_facing(&s.facing[k], s.vc, s.va, s.vb);
                if (pi_below(s.facing[k], G32(0x4a3318)))
                    menu_model_draw_at(cid, s.m, s.t, light, 1);
            }
        }
    }

    rec = menu_model_find_at(id);
    pi_model_place(&s, rec, angles, matrix, pos, spin);
    menu_model_draw_at(id, s.m, s.t, light, 1);

    if (first != 0) {
        cid = first;
        for (k = 0; k < 4; k++, cid++) {
            if (pi_below(s.facing[k], G32(0x4a3318)))
                continue;
            rec = menu_model_find_at(cid);
            s.angles[0] = angles[0];
            s.angles[1] = angles[1];
            pi_model_place(&s, rec, s.angles, matrix, pos, spin);
            menu_model_draw_at(cid, s.m, s.t, light, 3);
        }
    }
}
