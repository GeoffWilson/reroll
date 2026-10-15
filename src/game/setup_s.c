/* Race setup, group S: the setup dispatcher 0x46a060 (called once when a
 * race starts) and the integer parts of the track loader 0x43fa60's tree:
 * creating the track's objects from its object records, their lights and
 * the fixed TV-camera spots.
 *
 * race_setup calls every setup step by its address, so the hooks route each
 * one to its C version (or the original) exactly as the original does.
 *
 * Still original: the track loader 0x43fa60 itself (x87 colour scaling,
 * archive loading, allocation), file and archive access (0x43b550,
 * 0x43b630, 0x43b660, 0x465340..), the game's allocator 0x4445e0 / 0x444570,
 * the texture upload 0x41e710 and colour set 0x41de10 (DirectDraw), the
 * light placement 0x442c90 (x87) and the object pool helpers 0x460af0,
 * 0x460b20, 0x460b40, 0x460dc0, 0x460e40, 0x460f50 (group U). */
#include "../runtime.h"
#include "physics.h"
#include "gamefn.h"

#define g_race GP(0x4b1960)
#define g_config GP(0x4b2198)
#define g_track_info GP(0x6c1eb0) /* the loaded track file's header */
#define g_track GP(0x6c1e30)      /* the track (0x6bfac0) */
#define g_textures GP(0x6c1e14)   /* 0x10-byte texture records */
#define g_track_models GP(0x6bfac8)
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_controllers ((uint8_t *)0x6bf880) /* 0x5c bytes per car */
#define g_frontend_return (*(uint32_t *)0x4d4654)
#define g_car_serial (*(uint32_t *)0x4d3db0)
#define g_object_pool_used (*(uint32_t *)0x4d087c)
#define g_object_pool_count (*(int32_t *)0x4d1080)
#define g_object_pool_block (*(uint32_t *)0x4d1084)
#define g_object_pool (*(uint32_t *)0x6b9b90)
#define g_object_list ((uint32_t *)GP(0x6ce738))
#define g_object_list_count (*(uint32_t *)0x6ce6fc)
#define g_lit_faces_count (*(uint32_t *)0x5f72a4)
#define g_lights ((uint32_t *)0x5d6e00) /* object pointers */
#define g_markers_count (*(int32_t *)0x4d5800)
#define g_markers ((int32_t *)0x5d6400) /* x, y, z for up to 40 */
#define g_cam_lines ((int32_t *)0x6cfbe0) /* 14 words each, up to 40 */
#define g_cam_line_count (*(int32_t *)0x4b65f4)
#define g_blast_count (*(uint32_t *)0x6d06c0)
#define g_blasts ((uint8_t *)0x6d06e0) /* 0x10 bytes each */
#define g_triggers ((uint32_t *)0x4b5e58) /* object, value pairs */
#define g_trigger_count (*(uint32_t *)0x4b5fa0)
#define g_statics ((uint32_t *)0x6d0a80) /* 3 words each */
#define g_static_count (*(uint32_t *)0x4b5f98)
#define g_textures_paged (*(uint32_t *)0x4bcd78)
#define g_texture_slots ((uint8_t *)0x4ba700) /* 64 of 0x24 bytes */
#define g_ambient (*(uint32_t *)0x6ccd90)

#define I32(p, off) (*(int32_t *)((p) + (off)))
#define U32(p, off) (*(uint32_t *)((p) + (off)))
#define S16(p, off) (*(int16_t *)((p) + (off)))
#define U16(p, off) (*(uint16_t *)((p) + (off)))
#define U8(p, off) (*(uint8_t *)((p) + (off)))
#define PTR(p, off) GP((p) + (off))
#define SHL(x, n) ((int32_t)((uint32_t)(int32_t)(x) << (n)))

/* Called by address. */
int32_t GAMEAPI texture_upload_body(int32_t page, int32_t w, int32_t h, uint32_t pixels, uint32_t palette,
                                    uint32_t flags); /* 0x41e710 (src/vkgame.c's texture_upload) */

/* root_name_get 0x465550 and dx_error_ignore 0x4291f0 read no arguments, but
 * the original pushes one for each; the calls keep it (harmless in 64-bit). */
typedef uint32_t(GAMEAPI *root_name_arg_fn)(uint32_t unused);
typedef void(GAMEAPI *dx_error_ignore_arg_fn)(uint32_t unused);

#define pool_size GCALL(0x460e40, model_faces_carve)
#define pool_take GCALL(0x460af0, link_pool_take)
#define object_clear GCALL(0x460b40, body_header_init)
#define object_register GCALL(0x460f50, body_list_add)
#define object_parts GCALL(0x460dc0, model_instance_init)
#define object_link_at GCALL(0x460ff0, object_grid_link2_at)
#define object_header GCALL(0x460b20, grid_link_init)
#define grid_unlink_object GCALL(0x4611d0, object_grid_unlink)
#define grid_link_object GCALL(0x460fb0, object_grid_link)
#define body_matrix GCALL(0x480780, quat_to_matrix)
#define body_prepare_at GCALL(0x465740, body_prepare)
#define cos_of GCALL(0x480bc0, cos_table)
#define sin_of GCALL(0x480b10, sin_table)
#define direction_fit_at GCALL(0x407e90, direction_fit)
#define vec_length_at GCALL(0x480cd0, vec_length)
#define vec_normalise_at GCALL(0x480c70, vec_normalise)
#define light_place GCALL(0x442c90, body_point_world)
#define texture_upload GCALL(0x41e710, texture_upload_body)
#define colour_set GCALL(0x41de10, d3d_fog_colour_set)
#define game_alloc GCALL(0x4445e0, kr_malloc)

/* Update routines given to track objects (+0x148). */
#define UPDATE_SWINGING 0x401230
#define UPDATE_KIND_23 0x401280
#define UPDATE_OBJECT 0x401120
#define UPDATE_KIND_5 0x45bbf0

/* 0x4270e0: set or clear bit 1 of a primitive's byte +7. */
void GAMEAPI prim_flag2_set(uint8_t *prim, int32_t on)
{
    if (on)
        U8(prim, 7) |= 2;
    else
        U8(prim, 7) &= 0xfd;
}

/* 0x480a80: a body's quaternion (+0x58 order w, x, y, z) from the track
 * record's angles. Each set axis gets the cosine of its half angle (x is
 * already in table units, y and z are scaled by 0x6488 >> 9); w is the sine
 * of the last half angle computed (0 when none is set). */
void GAMEAPI quat_from_angles(int32_t x, int32_t y, int32_t z, int32_t *q)
{
    int32_t a = 0;

    q[1] = 0;
    q[2] = 0;
    q[3] = 0;
    if (x) {
        a = x >> 1;
        q[1] = cos_of(a);
    }
    if (y) {
        a = (int32_t)((uint32_t)y * 0x6488) >> 9;
        q[2] = cos_of(a);
    }
    if (z) {
        a = (int32_t)((uint32_t)z * 0x6488) >> 9;
        q[3] = cos_of(a);
    }
    q[0] = sin_of(a);
}

/* 0x465640: a body's position, momentum, orientation and mass from a
 * template (0x80 bytes: +0 position, +0xc momentum, +0x18 angular momentum,
 * +0x24 quaternion, +0x68 mass and inertia), then prepare and link it. */
void GAMEAPI body_init_from_template(uint8_t *object, const uint32_t *t)
{
    int i;

    U32(object, 0x108) = 0;
    for (i = 0; i < 13; i++)
        U32(object, 0x34 + i * 4) = t[i];
    body_matrix((int32_t *)(object + 0x58), (int16_t *)(object + 0xc4));
    for (i = 0; i < 6; i++)
        U32(object, 0x9c + i * 4) = t[0x1a + i];
    U32(object, 0xb4) = 0x20000;
    U32(object, 0x90) = 0;
    U32(object, 0x94) = 0;
    U32(object, 0x98) = 0;
    U32(object, 0x8c) = 0;
    for (i = 0; i < 6; i++)
        U32(object, 0x74 + i * 4) = 0;
    body_prepare_at(object + 0x34);
    grid_link_object(object);
}

/* 0x460ba0: a new object from the pool (`size` bytes plus room for the
 * model's polygon parts unless `shared`), cleared to `kind`, its model's
 * (+0x1c, +0x1e: offsets of the second and third levels of detail) colours
 * and part tables set up. */
uint8_t *GAMEAPI object_create(uint8_t *model, uint32_t kind, int32_t size, int32_t shared)
{
    /* pool_size works in offsets from 0 (`at` = size) */
    uint8_t *main_a = model, *main_b = (uint8_t *)(uintptr_t)(uint32_t)size, *near_a = 0;
    uint8_t *near_b = (uint8_t *)(uintptr_t)(uint32_t)shared, *far_a = 0, *far_b = 0;
    uint8_t *object, *lod1, *lod2;
    uint32_t total;
    int32_t own;

    if (!model) {
        total = (uint32_t)(uintptr_t)pool_size(0, (uint8_t *)(uintptr_t)(uint32_t)size, 0, 0, 0, &main_a, &main_b, &near_a, &near_b, &far_a, &far_b);
        object = pool_take(total);
        object_clear(object, kind);
        object_register(object);
        return object;
    }
    lod1 = model + U16(model, 0x1c);
    lod2 = model + U16(model, 0x1e);
    own = shared == 0;
    total = (uint32_t)(uintptr_t)pool_size(model, (uint8_t *)(uintptr_t)(uint32_t)size, own, own && lod1 != model, own && lod2 != model, &main_a, &main_b, &near_a,
                      &near_b, &far_a, &far_b);
    object = pool_take(total);
    object_clear(object, kind);
    object_register(object);
    U32(object, 0x24) = U32(model, 0x40);
    U8(object, 0x162) = U8(model, 4);
    U8(object, 0x10d) = U8(model, 4);
    U32(object, 0x2c) = U32(model, 0xc);
    U32(object, 0x28) = U32(model, 0xc);
    if (U16(model, 0x1c)) {
        U32(object, 0x2c) = U32(model, U16(model, 0x1c) + 0xc);
        U8(object, 0x162) = U8(model, U16(model, 0x1c) + 4);
    }
    U32(object, 0x30) = U32(object, 0x2c);
    U8(object, 0x163) = U8(object, 0x162);
    if (U16(model, 0x1e)) {
        U32(object, 0x30) = U32(model, U16(model, 0x1e) + 0xc);
        U8(object, 0x163) = U8(model, U16(model, 0x1e) + 4);
    }
    U32(object, 0x10) = (uint32_t)(uintptr_t)model;
    if (shared)
        return object;
    object_parts((uint32_t *)(object + 0x14), model, object + (int32_t)(uintptr_t)main_a,
                 (uint32_t)(uintptr_t)(object + (int32_t)(uintptr_t)main_b));
    if (lod1 == model) {
        U32(object, 0x1c) = U32(object, 0x14);
        U32(object, 0x20) = U32(object, 0x18);
    } else {
        object_parts((uint32_t *)(object + 0x1c), lod1, object + (int32_t)(uintptr_t)near_a,
                     (uint32_t)(uintptr_t)(object + (int32_t)(uintptr_t)near_b));
    }
    if (lod2 == lod1 || lod2 == model) {
        U32(object, 0x14c) = U32(object, 0x1c);
        U32(object, 0x150) = U32(object, 0x20);
    } else {
        object_parts((uint32_t *)(object + 0x14c), lod2, object + (int32_t)(uintptr_t)far_a,
                     (uint32_t)(uintptr_t)(object + (int32_t)(uintptr_t)far_b));
    }
    return object;
}

/* The common tail of the track object makers: orientation from the
 * record's angles (+0xe), position (+8, int16 world units), and two bytes
 * (+0x26, +0x27) the object's update uses. */
static uint8_t *place_object(uint8_t *object, const uint8_t *record)
{
    quat_from_angles(S16(record, 0xe), S16(record, 0x10), S16(record, 0x12), (int32_t *)(object + 0x58));
    body_matrix((int32_t *)(object + 0x58), (int16_t *)(object + 0xc4));
    grid_unlink_object(object);
    I32(object, 0x34) = SHL(S16(record, 8), 16);
    I32(object, 0x38) = SHL(S16(record, 0xa), 16);
    I32(object, 0x3c) = SHL(S16(record, 0xc), 16);
    grid_link_object(object);
    U16(object, 0x138) = U8(record, 0x26);
    U16(object, 0x13a) = U8(record, 0x27);
    return object;
}

static uint8_t *record_model(const uint8_t *record)
{
    return GP(PTR(g_track_info, 0x18) + U8(record, 3) * 4);
}

/* 0x401300: a moving track object of `kind` (0x19, 0x1f, 0x14 or 0x23). */
uint8_t *GAMEAPI track_object_make(const uint8_t *record, uint32_t kind)
{
    uint8_t *model = record_model(record), *object;

    object = object_create(model, kind == 0x14 ? 0x8e14 : kind | 0xe00, 0x18c, 0);
    if (kind == 0x1f)
        U32(object, 0x148) = UPDATE_SWINGING;
    else if (kind == 0x23)
        U32(object, 0x148) = UPDATE_KIND_23;
    else
        U32(object, 0x148) = UPDATE_OBJECT;
    body_init_from_template(object, (const uint32_t *)0x4a7048);
    return place_object(object, record);
}

/* 0x41ac60: a track object with no update routine. */
uint8_t *GAMEAPI track_object_make_static(const uint8_t *record)
{
    uint8_t *object = object_create(record_model(record), 0x8c18, 0x180, 0);

    U32(object, 0x148) = 0;
    body_init_from_template(object, (const uint32_t *)0x4a7da0);
    return place_object(object, record);
}

/* 0x45bf10: a kind-5 track object (update 0x45bbf0). */
uint8_t *GAMEAPI track_object_make_k5(const uint8_t *record)
{
    uint8_t *object = object_create(record_model(record), 0xe1d, 0x180, 0);

    U32(object, 0x148) = UPDATE_KIND_5;
    body_init_from_template(object, (const uint32_t *)0x4b0c78);
    return place_object(object, record);
}

/* 0x43e5c0: an invisible body (no model) at the record's position. */
uint8_t *GAMEAPI track_marker_make(const uint8_t *record)
{
    uint8_t *object = object_create(0, 0x8020, 0x188, 0);

    body_init_from_template(object, (const uint32_t *)0x4a7048);
    grid_unlink_object(object);
    I32(object, 0x34) = SHL(S16(record, 8), 16);
    I32(object, 0x38) = SHL(S16(record, 0xa), 16);
    I32(object, 0x3c) = SHL(S16(record, 0xc), 16);
    grid_link_object(object);
    return object;
}

/* 0x41aa70: a track object record of type 0x18, by its kind (+0x1f), added
 * to the object list. */
uint8_t *GAMEAPI track_object_spawn(const uint8_t *record)
{
    uint32_t *slot = g_object_list + g_object_list_count;
    uint8_t *object;

    switch (U8(record, 0x1f)) {
    case 3:
        object = track_object_make(record, 0x19);
        break;
    case 5:
        object = track_object_make_k5(record);
        break;
    case 8:
        object = track_object_make(record, 0x1f);
        break;
    case 9:
        object = track_object_make(record, 0x14);
        break;
    case 10:
        object = track_marker_make(record);
        break;
    case 11:
        object = track_object_make(record, 0x23);
        break;
    default:
        object = track_object_make_static(record);
        break;
    }
    *slot = (uint32_t)(uintptr_t)object;
    g_object_list_count++;
    return object;
}

/* 0x47dcc0: a sprite (0x120-byte pool object of type 0x98) at x, y, z with
 * texture record `texture`, linked into the grid. */
uint8_t *GAMEAPI sprite_object_create(int32_t x, int32_t y, int32_t z, uint32_t texture, uint32_t a, uint32_t b)
{
    const uint8_t *t = g_textures + texture * 0x10;
    uint8_t *object = pool_take(0x120);

    U8(object, 0x1f) = 0x98;
    U32(object, 0x18) = (U32(object, 0x18) & 0xffffff) | 0xc000000;
    U32(object, 0x38) = U32(t, 0);
    U32(object, 0x54) = U32(t, 4);
    U16(object, 0x70) = U16(t, 8);
    U16(object, 0x8c) = U16(t, 0xa);
    prim_flag2_set(object + 0x18, 1);
    object_header(object, 2);
    U8(object, 0x17) = (uint8_t)b;
    U8(object, 0x16) = (uint8_t)a;
    U16(object, 0x12) = (uint16_t)y;
    U16(object, 0x10) = (uint16_t)x;
    U16(object, 0x14) = (uint16_t)z;
    object_link_at(object, SHL((int16_t)x, 16), SHL((int16_t)y, 16), SHL((int16_t)z, 16));
    return object;
}

/* 0x47dbc0: a type-2 sprite (track record type 2), its position also kept
 * in the list at 0x5d6400 (up to 40). */
void GAMEAPI sprite_spawn_marker(int32_t x, int32_t y, int32_t z, uint32_t texture, uint32_t a, uint32_t b)
{
    uint8_t *object = sprite_object_create(x, y, z, texture, a, b);
    int32_t n;

    U32(object, 0) = 2;
    n = g_markers_count;
    if (n < 0x28) {
        g_markers[n * 3] = SHL(S16(object, 0x10), 16);
        g_markers[n * 3 + 1] = SHL(S16(object, 0x12), 16);
        g_markers[n * 3 + 2] = SHL(S16(object, 0x14), 16);
        g_markers_count = n + 1;
    }
}

/* 0x47dc30: a light sprite (type 0xd), entered in the light table at
 * `index`. */
uint8_t *GAMEAPI light_sprite_spawn(int32_t x, int32_t y, int32_t z, uint32_t texture, uint32_t a, uint32_t c0,
                                    uint32_t c1, uint32_t c2, int32_t index)
{
    uint8_t *object = sprite_object_create(x, y, z, texture, a, 0x80);

    g_lights[index] = (uint32_t)(uintptr_t)object;
    U8(object, 0x108) = (uint8_t)c0;
    U32(object, 0) = 0xd;
    U8(object, 0x109) = (uint8_t)c1;
    U8(object, 0x10a) = (uint8_t)c2;
    U8(object, 0x10b) = 0xff;
    U8(object, 0x10c) = 0;
    return object;
}

/* 0x4426e0: light `i` of an object: its sprite at the light point (model
 * +0x4c: 8-byte light points, +7 the light type, an index into the track's
 * light types at +0xd0: 8 bytes, +0..2 colour, +3 size, +4 flag, +6
 * texture); the light's number goes into the low 12 bits of the object's
 * light word. */
void GAMEAPI object_light_spawn(uint8_t *object, uint32_t i, int32_t *counter)
{
    uint8_t *model = PTR(object, 0x10), *point = model + U16(model, 0x4c) + i * 8;
    const uint8_t *type = PTR(g_track_info, 0xd0) + U8(point, 7) * 8;
    int16_t pos[4] = {0, 0, 0, 0};
    uint16_t word;

    light_place(object, (const uint32_t *)point, pos);
    light_sprite_spawn((uint16_t)pos[0] | (uint32_t)(uint16_t)pos[1] << 16,
                       (uint16_t)pos[1] | (uint32_t)(uint16_t)pos[2] << 16,
                       (uint16_t)pos[2] | (uint32_t)(uint16_t)pos[3] << 16, U16(type, 6), U8(type, 3),
                       ((uint32_t)*counter & ~0xffu) | U8(type, 0), U8(type, 1), U8(type, 2), *counter);
    word = U16(object, 0x16a + i * 2);
    U16(object, 0x16a + i * 2) = (uint16_t)(((word ^ (uint16_t)*counter) & 0xfff) ^ word);
    ++*counter;
    if (U8(type, 4))
        U8((uint8_t *)(uintptr_t)g_lights[U16(object, 0x16a + i * 2) & 0xfff], 0x10c) = 1;
}

/* 0x442680: an object's five light words from the record (+0x2e); type 2
 * (top nibble) lights get a sprite. */
void GAMEAPI object_lights_setup(uint8_t *object, const uint8_t *record, int32_t *counter)
{
    uint32_t i;

    for (i = 0; i < 5; i++) {
        uint16_t word = U16(record, 0x2e + i * 2);

        U16(object, 0x16a + i * 2) = word;
        if (word >> 12 == 2)
            object_light_spawn(object, i, counter);
    }
}

/* 0x4039d0: an object's trigger values (record +0x1c, +0x1d) into the
 * trigger list. */
void GAMEAPI object_triggers_register(const uint8_t *record, uint32_t object)
{
    if (U8(record, 0x1c)) {
        g_triggers[g_trigger_count * 2] = object;
        g_triggers[g_trigger_count * 2 + 1] = U8(record, 0x1c);
        g_trigger_count++;
    }
    if (U8(record, 0x1d)) {
        g_triggers[g_trigger_count * 2] = object;
        g_triggers[g_trigger_count * 2 + 1] = U8(record, 0x1d);
        g_trigger_count++;
    }
}

/* 0x403a20: add an object to the list of plain track objects. */
void GAMEAPI track_static_add(uint32_t object)
{
    uint32_t n = g_static_count;

    g_static_count = n + 1;
    g_statics[n * 3] = object;
}

/* 0x405070: an object of class 2 (+0x19a) into the blast table: position
 * (int16 world units), record bytes +4, +5, +2. */
void GAMEAPI track_blast_register(uint8_t *object, const uint8_t *record)
{
    uint32_t n = g_blast_count;
    uint8_t a = U8(record, 5), b = U8(record, 4), c = U8(record, 2), *e = g_blasts + n * 0x10;

    U8(object, 0x19b) = (uint8_t)n;
    S16(e, 0) = (int16_t)(I32(object, 0x34) >> 16);
    S16(e, 2) = (int16_t)(I32(object, 0x38) >> 16);
    U8(e, 6) = b;
    U8(e, 7) = a;
    S16(e, 4) = (int16_t)(I32(object, 0x3c) >> 16);
    U8(e, 8) = c;
    U32(e, 0xc) = (uint32_t)(uintptr_t)object;
    U8(e, 9) = 0;
    g_blast_count++;
}

/* 0x4783d0: add a lit face (polygon, object) to the track's list (+0xec,
 * 0x14 bytes each). */
void GAMEAPI lit_face_add(uint32_t object, uint32_t face)
{
    uint32_t *e = (uint32_t *)(PTR(g_track_info, 0xec) + g_lit_faces_count * 0x14);

    e[0] = face;
    e[1] = object;
    g_lit_faces_count++;
}

/* 0x478370: an object's faces (model +0x34, 0x14 bytes each, +0x27 the
 * count) whose colour word (+0x194 list) has type 4 go in the lit list. */
void GAMEAPI object_lit_faces_register(uint8_t *object)
{
    uint8_t *face;
    uint32_t i;

    if (!U8(object, 0x198))
        return;
    face = PTR(object, 0x10) + U16(PTR(object, 0x10), 0x34);
    for (i = 0; i < U8(PTR(object, 0x10), 0x27); i++, face += 0x14)
        if ((U32(PTR(object, 0x194), i * 4) & 0xf000000) == 0x4000000)
            lit_face_add((uint32_t)(uintptr_t)object, (uint32_t)(uintptr_t)face);
}

/* 0x478e70: count the polygons of a model level (+6 count) whose entry in
 * the polygon table (0x6bfb5c, by byte offset index >> 2, +2) is set. */
void GAMEAPI faces_count_lit(const uint16_t *list, const uint8_t *model, int32_t *count)
{
    const uint8_t *table = GP(0x6bfb5c);
    uint32_t n = U16(model, 6);
    int32_t found = 0;

    for (; n; n--, list++)
        if (S16(table, (*list >> 2) + 2) != -1)
            found++;
    *count += found;
}

/* 0x478ec0: the lit polygons of all three levels of a model, for a track
 * object record (+0x28, +0x2a, +0x2c: indices into 0x6bfb58). */
void GAMEAPI model_count_lit(const uint8_t *record, const uint8_t *model, int32_t *count)
{
    const uint8_t *lod1 = U16(model, 0x1c) ? model + U16(model, 0x1c) : 0;
    const uint8_t *lod2 = U16(model, 0x1e) ? model + U16(model, 0x1e) : 0;
    int32_t n = 0;

    faces_count_lit((const uint16_t *)(GP(0x6bfb58) + U16(record, 0x28) * 2), model, &n);
    if (lod1)
        faces_count_lit((const uint16_t *)(GP(0x6bfb58) + U16(record, 0x2a) * 2), lod1, &n);
    if (lod2)
        faces_count_lit((const uint16_t *)(GP(0x6bfb58) + U16(record, 0x2c) * 2), lod2, &n);
    *count = n;
}

/* 0x478f80: flag (+8 bit 1) the models of track object records (0x38
 * bytes, +1 bit 2) that have lit polygons. */
int32_t GAMEAPI track_models_mark_lit(int32_t count, const uint8_t *record)
{
    const uint8_t *models = g_track_models;

    if (count < 1)
        return 1;
    for (; count; count--, record += 0x38) {
        uint8_t *model;
        int32_t n;

        if (!(U8(record, 1) & 4))
            continue;
        model = GP(PTR(models, 0x18) + U8(record, 3) * 4);
        if (!model || (U8(model, 8) & 3))
            continue;
        n = 0;
        model_count_lit(record, model, &n);
        if (n)
            U8(model, 8) |= 2;
    }
    return 1;
}

/* 0x4044c0: the AI route tables for `a` and `b` entries (0x78 and 0x20
 * bytes each). */
void GAMEAPI track_tables_alloc(int32_t a, int32_t b)
{
    *(int32_t *)0x6d0a00 = b;
    *(int32_t *)0x6d0a78 = a;
    *(uint32_t *)0x4b5fc8 = game_alloc((uint32_t)a * 0x78);
    *(uint32_t *)0x4b5fcc = game_alloc(*(uint32_t *)0x6d0a00 * 0x78);
    *(uint32_t *)0x6d0a3c = game_alloc(*(uint32_t *)0x6d0a78 << 5);
    *(uint32_t *)0x6d0a38 = game_alloc(*(uint32_t *)0x6d0a00 << 5);
    *(uint32_t *)0x4b5fd8 = *(uint32_t *)0x4b5fc8;
    *(uint32_t *)0x4b5fc0 = *(uint32_t *)0x4b5fcc;
    *(uint32_t *)0x4b5fc4 = 0;
    *(uint32_t *)0x4b5fd4 = 0;
}

/* 0x460aa0: empty the object pool for a track with `count` extra entries
 * (allocated once, 128-byte aligned). */
void GAMEAPI object_pool_reset(int32_t count)
{
    uint32_t block = g_object_pool_block;

    g_object_pool_used = 0;
    g_object_pool_count = count;
    if (!block) {
        block = game_alloc((uint32_t)(count + 85000) << 5);
        g_object_pool_block = block;
    }
    g_object_pool = (block + 0x7f) & ~0x7fu;
}

/* 0x41f1b0: the bytes texture page `id` takes (its palette 0x300 more in
 * paletted mode). */
int32_t GAMEAPI texture_bytes(int32_t id)
{
    int32_t scale = g_textures_paged ? 1 : 2, i;

    for (i = 0; i < 0x40; i++) {
        const uint8_t *e = g_texture_slots + i * 0x24;

        if (U32(e, 0) && I32(e, 0x14) == id) {
            int32_t bytes = I32(e, 0x10) * I32(e, 0xc) * scale;

            return scale == 1 ? bytes + 0x300 : bytes;
        }
    }
    return 0;
}

/* 0x428130: upload the track's texture pages (0xc-byte records: +0 width,
 * +2 height, +4 palette, +8 pixel offset; 16-bit pixels; 256x256 when
 * `flags` bit 1); pages first..first+n-1 are marked. 0 if one fails. */
int32_t GAMEAPI track_textures_load(int32_t count, int32_t first, int32_t n, const uint8_t *records,
                                    uint32_t palettes, uint32_t pixels, uint32_t flags)
{
    int32_t last = first + n - 1, i, offset = 0;

    for (i = 0; i < count; i++, records += 0xc) {
        uint32_t w, h;

        if (flags & 2) {
            w = 0x100;
            h = 0x100;
        } else {
            offset = I32(records, 8);
            w = U16(records, 0);
            h = U16(records, 2);
        }
        if (!texture_upload(i, (int32_t)w, (int32_t)h, pixels + offset, U16(records, 4) * 0x400 + palettes,
                            (i < first || i > last ? 0 : 1) | flags)) {
            U32(g_config, 0x48) = 4;
            return 0;
        }
        texture_bytes(i);
        offset += (int32_t)(h * w * 2);
    }
    return 1;
}

/* 0x428350: the ambient colour (also to the renderer). */
void GAMEAPI ambient_colour_set(uint32_t r, uint32_t g, uint32_t b)
{
    r &= 0xff;
    g &= 0xff;
    b &= 0xff;
    g_ambient = (b << 8 | g) << 8 | r;
    colour_set(r, g, b);
}

/* The TV camera's fixed rails: start and end points (int16 world units, y
 * raised by 0x32) per track; tracks 15 to 19 have none. */
static const int16_t tv_rails[58][6] = {
    {-4279, -30, 5858, 2125, 114, 4711},      {2564, -15, 1944, 794, -29, -5991},
    {-2300, -28, -2054, -5428, -28, 4455},    {3040, -531, 7424, 2432, -511, 1798},
    {-2725, -161, 1912, -2271, -225, 7991},   {-2841, -30, 6623, -1646, -913, 12267},
    {2409, 360, -7008, -1682, -297, -7686},   {-3503, -30, -3982, -3878, -61, 4848},
    {-1249, -47, 5948, 3660, -31, 480},       {2801, -31, 2667, 2881, -31, -5748},
    {-1174, -89, 3278, -1818, -19, 9560},     {3816, 15, 13064, 5842, 62, 6812},
    {5946, 63, 8101, 5239, 55, -2036},        {4462, -607, 3606, 3464, -31, -4948},
    {1979, -26, -5402, -795, -396, 825},      {-2078, -31, -5735, -5626, -30, -5023},
    {-5801, -68, -3915, -2512, 129, -1763},   {-749, -517, 1886, 6757, 309, -16},
    {7141, 261, -374, 6031, 204, -5846},      {5801, 213, -5972, -600, 169, -5518},
    {2078, -610, -2692, 2262, -722, 5557},    {651, -76, 7337, -5410, -36, 7390},
    {-5580, -172, 6411, -6386, -1106, -682},  {-4389, -415, -3896, -1963, -256, 1763},
    {-1633, -251, 1722, 1368, -632, -3038},   {-6121, -848, 1910, -6044, -848, -3392},
    {3979, -29, -1022, 4194, -31, 4621},      {930, -11, 10545, -6009, -53, 5988},
    {-6398, 78, 4586, -2176, -109, -1193},    {1867, -621, 9091, -774, -626, 5859},
    {-2107, -168, 3922, -1688, 579, -446},    {-1658, 580, -299, 509, 619, -5454},
    {3880, -179, -6723, 2332, -253, 653},     {2888, -31, -2881, -310, -31, -5569},
    {-2823, -31, -4822, -3529, -31, 2290},    {-2856, 9, 6866, -3399, 407, 10862},
    {1429, -215, 4833, 1731, -145, 181},      {-4697, -608, 2443, -3675, -582, 7565},
    {7321, -1469, 101, 1911, 126, -4267},     {-4985, -32, -5115, -5018, -32, -1},
    {-2031, -33, -1888, -4150, -569, 2614},   {-3430, -31, -175, -3317, -31, 8690},
    {5537, -743, 6780, 6405, -770, 790},      {1276, -1032, -1375, 4819, 330, -3468},
    {8769, 1852, -4900, 6431, 1215, -8582},   {-2496, -5, -6350, -3603, -39, 818},
    {3737, -749, 7798, 6821, -750, 4043},     {1498, -94, 635, -2348, -146, 53},
    {324, -31, -6318, 417, -31, -162},        {-349, -31, -859, 434, -109, 4862},
    {-2285, -204, 4191, 2533, -258, 206},     {4315, -416, 1506, 5336, -519, 7056},
    {3350, -202, 9029, -2777, -206, 11085},   {861, -31, -3041, 1851, -64, 2624},
    {4001, -373, 5865, 2271, -169, 11591},    {-901, -35, 1586, -22, -32, -2715},
    {-951, -848, 2247, -1546, -848, 7178},    {2963, -848, 7396, 2764, -848, 2908},
};
static const uint8_t tv_rail_count[20] = {3, 3, 4, 5, 5, 6, 3, 4, 4, 4, 5, 4, 3, 3, 2, 0, 0, 0, 0, 0};
static const uint8_t tv_rail_first[20] = {0, 3, 6, 10, 15, 20, 26, 29, 33, 37, 41, 46, 50, 53, 56, 0, 0, 0, 0, 0};

/* 0x40b5b0: add the track's (race +0x579) TV camera rails to the camera
 * lines the track file gave (0x6cfbe0: +0 type, 1 for a rail; +4 start,
 * +0x10 end, +0x1c midpoint, +0x28 direction, +0x34 length), then work out
 * each rail's midpoint, direction and length. */
void GAMEAPI track_tv_spots(void)
{
    const uint8_t *race = g_race;
    uint32_t track = U8(race, 0x579), k;
    int32_t n = g_cam_line_count, i;

    if (track < 0x14 && tv_rail_count[track]) {
        k = 0;
        do {
            const int16_t *s = tv_rails[tv_rail_first[track] + k];

            if (n < 0x28) {
                int32_t *line = g_cam_lines + n * 14;

                line[0] = 1;
                line[1] = SHL(s[0], 16);
                line[2] = SHL(s[1] - 0x32, 16);
                line[3] = SHL(s[2], 16);
                line[4] = SHL(s[3], 16);
                line[5] = SHL(s[4] - 0x32, 16);
                line[6] = SHL(s[5], 16);
                g_cam_line_count = ++n;
            }
            k++;
            track = U8(race, 0x579);
        } while (k < tv_rail_count[track]);
    }
    for (i = 0; i < n; i++) {
        int32_t *line = g_cam_lines + i * 14, v[3], shift;

        if (line[0] != 1)
            continue;
        line[10] = line[4] - line[1];
        line[11] = line[5] - line[2];
        line[12] = line[6] - line[3];
        v[0] = line[10];
        v[1] = line[11];
        v[2] = line[12];
        shift = direction_fit_at(v);
        line[13] = (int32_t)((uint32_t)vec_length_at(v) << (shift & 31));
        line[7] = (line[10] >> 1) + line[1];
        line[8] = (line[11] >> 1) + line[2];
        line[9] = (line[12] >> 1) + line[3];
        direction_fit_at(line + 10);
        vec_normalise_at(line + 10);
        n = g_cam_line_count;
    }
}

/* 0x46a060: set up a race: reset the race state, load the track (0x43fa60
 * with the track name from 0x465550), then every per-race setup step and
 * each car slot in the original order. 0 if the track fails to load. */
int32_t GAMEAPI race_setup(void)
{
    uint8_t *slot, *controller;
    int32_t i, j, cars;
    uint32_t offset, name;
    const uint16_t *index;

    if (!g_frontend_return)
        U8(g_race, 0x57b) = 0;
    U8(g_race, 0x54c) = 0;
    *(uint32_t *)0x6c0a38 = 0;
    *(uint32_t *)0x6c0a54 = 0;
    U8(g_race, 0x550) = 0;
    if (U8(g_race, 0x551) == 4)
        U8(g_race, 0x586) = 7;
    U16(g_race, 0xa82) = 0x280;
    U16(g_race, 0xa80) = 0x1e0;
    U32(g_race, 0x580) = 0;
    U8(g_race, 0x587) = 0;
    if (!U8(g_config, 0x2b) || U8(g_race, 0x579) != 0xc)
        U8(g_race, 0x54e) = 0;
    *(uint32_t *)0x4b11dc = U32(g_race, 0x618);
    if (U8(g_race, 0x552) > U8(g_race, 0x54d)) {
        U8(g_race, 0x552) = U8(g_race, 0x54d);
        if (U8(g_race, 0x552) == 2)
            U8(g_race, 0x552) = 1;
    }
    for (offset = 0; offset < 0xb7318; offset += 0x1e884)
        U32(g_race, offset + 0x1fb20) = 8;
    GCALL(0x46a040, heap_and_views_init)();
    *(uint32_t *)0x6c0a50 = 0;
    *(uint32_t *)0x4c2458 = 0;
    GCALL(0x41b370, pads_reset)();
    *(uint32_t *)0x4d11ec = 0;
    *(uint32_t *)0x6b9cb8 = 0;
    *(uint32_t *)0x6b9b94 = 0;
    GCALL(0x41aa40, specials_reset)();
    GCALL(0x405050, explosions_reset)();
    GCALL(0x478340, car_targets_reset)();
    GCALL(0x418fa0, body_pools_setup)();
    GCALL(0x442660, attached_points_reset)();
    name = ((root_name_arg_fn)(uintptr_t)0x465550)(0x6bfac0);
    if (!GCALL(0x43fa60, track_load)((const char *)GMEM(name), GMEM(0x6bfac0)))
        return 0;
    GCALL(0x444080, viewports_setup)();
    GCALL(0x41aa60, track_water_setup)();
    GCALL(0x435110, clock_start)();
    GCALL(0x47c330, model_buffers_init)();
    GCALL(0x441b90, track_lights_init)();
    GCALL(0x479af0, text_lists_reset)();
    GCALL(0x46c210, shadows_reset)();
    GCALL(0x441d70, lights_reset)();
    GCALL(0x46ba00, bomblets_setup)();
    GCALL(0x4377c0, hud_layout)();
    GCALL(0x47d980, warp_init)();
    GCALL(0x486780, snatch_texture_setup)();
    GCALL(0x465270, messages_load)();
    if (U8(g_race, 0x58c))
        GCALL(0x463860, race_start_messages)(U8(g_race, 0x58c));
    GCALL(0x463990, replay_state_clear)();
    GCALL(0x467120, pickups_create)();
    GCALL(0x4583e0, weapon_bodies_create)();
    GCALL(0x43a8d0, freeze_setup)();
    GCALL(0x479720, time_warp_reset)();
    GCALL(0x46d2b0, shield_setup)();
    GCALL(0x457f80, rock_setup)();
    GCALL(0x47bd70, debris_init)();
    GCALL(0x433d30, trail_prims_init)();
    GCALL(0x462360, pieces_init)();
    GCALL(0x4040f0, quarter_prims_init)();
    GCALL(0x434380, markers_init)();
    GCALL(0x42a610, effects_init)();
    GCALL(0x46eab0, skid_marks_init)();
    GCALL(0x41ab80, track_specials_setup)();
    /* swap the two halves of eight texture records after the track's */
    for (j = 1; j < 5; j++) {
        uint8_t *t = g_textures + (U16(g_track_info, 0x2e) + 0x91 + j) * 0x10;
        uint16_t a = U16(t, 4);

        U16(t, 4) = U16(t, 8);
        U16(t, 8) = a;
    }
    for (j = 1; j < 9; j++) {
        uint8_t *t = g_textures + (U16(g_track_info, 0x2e) + 0x86 + j) * 0x10;
        uint16_t a = U16(t, 4);

        U16(t, 4) = U16(t, 8);
        U16(t, 8) = a;
    }
    GCALL(0x423f00, view_zoom_set)(*(uint16_t *)0x4b1964);
    GCALL(0x47b140, glow_flash_reset)();
    cars = U8(g_race, 0x54d);
    if (cars > 0) {
        i = 0;
        slot = g_car_slots;
        controller = g_controllers;
        do {
            GCALL(0x40e670, car_setup)(slot, i);
            if (i < cars) {
                U32(slot, 0x28) = (uint32_t)(uintptr_t)controller;
                GCALL(0x406be0, camera_view_setup)(i, I32(slot, 0), (int32_t *)controller);
                GCALL(0x490390, slot_glow_init)(PTR(slot, 0), (int32_t)g_car_serial);
                g_car_serial++;
            } else {
                U32(slot, 0x28) = 0x12345678;
            }
            GCALL(0x401470, ai_car_race_start)(slot);
            U16(slot, 0x254) = 200;
            U8(slot, 0x256) = 0;
            U32(slot, 0x270) = 0;
            U16(slot, 0x26c) = (uint16_t)GCALL(0x449ca0, player_car_choice)(i);
            U8(slot, 0x26b) = 0x32;
            U8(slot, 0x269) = 0x40;
            U8(slot, 0x26a) = 0;
            U8(slot, 0x268) = 0;
            if (!U8(g_race, 0x58c) && GCALL(0x449cd0, config_player_on)(i))
                U8(slot, 0x268) = (uint8_t)GCALL(0x428cc0, pad_device_bound)(i, (const int16_t *)(slot + 0x26c));
            /* the analogue flags: the original pushes 0x81, 0x82, 0x84 before
             * calling 0x489fa0 (one argument), so they become 0x449d00's
             * steer/throttle/brake flags */
            U16(slot, 0x26e) = (uint16_t)GCALL(0x449d00, pad_button_flags)(
                i, (uint32_t)GCALL(0x489fa0, device_usable)((int32_t)S16(slot, 0x26c)), 0x84, 0x82, 0x81);
            i++;
            controller += 0x5c;
            slot += CAR_SLOT_SIZE;
        } while (i < U8(g_race, 0x54d));
        /* Deliberate change: in a network race the cars are the players'
         * in their table order, so the car of this machine's player (its
         * record's +0x18) is seldom slot 0, but the loop above takes slot
         * i's pad device, force feedback and analogue flags from player i's
         * set-up: the local car came out digital and without rumble. It
         * takes player 1's (whose controls this machine sends), and the
         * other players' cars get no force feedback here (their analogue
         * flags come from their own machines with the race start). */
        if (*(int32_t *)0x4be47c && (*(uint8_t *)0x5d622c & 1)) {
            int32_t local = *(int32_t *)0x5d535c, car = -1;

            if (local >= 0 && local < 6)
                car = *(int32_t *)(uintptr_t)(0x5d2cd0u + (uint32_t)local * 0x66cu + 0x18);
            for (i = 0, slot = g_car_slots; i < cars; i++, slot += CAR_SLOT_SIZE) {
                if (i != car) {
                    U8(slot, 0x268) = 0;
                    continue;
                }
                U16(slot, 0x26c) = (uint16_t)GCALL(0x449ca0, player_car_choice)(0);
                U8(slot, 0x268) = 0;
                if (!U8(g_race, 0x58c) && GCALL(0x449cd0, config_player_on)(0))
                    U8(slot, 0x268) = (uint8_t)GCALL(0x428cc0, pad_device_bound)(0, (const int16_t *)(slot + 0x26c));
                U16(slot, 0x26e) = (uint16_t)GCALL(0x449d00, pad_button_flags)(
                    0, (uint32_t)GCALL(0x489fa0, device_usable)((int32_t)S16(slot, 0x26c)), 0x84, 0x82, 0x81);
            }
        }
    }
    /* the models of the shared objects: seven by index (0x4b1968) and 8 */
    for (index = (const uint16_t *)0x4b1968; index < (const uint16_t *)0x4b1976; index++)
        GCALL(0x40f1e0, model_normals_make)(PTR(PTR(g_track, 0x18), S16(PTR(g_track, 0x58), *index * 2) * 4));
    GCALL(0x40f1e0, model_normals_make)(PTR(PTR(g_track, 0x18), S16(PTR(g_track, 0x58), 0x10) * 4));
    GCALL(0x475090, car_textures_setup)();
    GCALL(0x481e50, tyre_trails_init)();
    GCALL(0x483d90, weather_setup)();
    GCALL(0x4730b0, race_sounds_init)();
    GCALL(0x41b460, slots_controls_setup)(g_car_slots);
    if (U8(g_race, 0x58c))
        GCALL(0x463930, race_fade_init)(U8(g_race, 0x58c));
    GCALL(0x429220, tick_sync)(0);
    GCALL(0x4351a0, clock_restart)();
    *(uint32_t *)0x6cce94 = U8(g_race, 0x58b);
    GCALL(0x46a940, countdown_init)();
    GCALL(0x4430f0, glints_clear)();
    if (U8(g_race, 0x555))
        GCALL(0x485070, weather_tint_setup)();
    ((dx_error_ignore_arg_fn)(uintptr_t)0x4291f0)(1);
    return 1;
}
