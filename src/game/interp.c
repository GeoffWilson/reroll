/* Render interpolation between fixed 32 ms physics steps (stage 2 of
 * decoupling rendering from physics; see frame.c).
 *
 * After every physics step the position and orientation of each car and of
 * each car's camera are recorded. A displayed frame draws with them blended
 * between the last two steps, then puts the exact values back, so physics
 * never sees a blended value.
 *
 * World objects (cars and cameras alike):
 *   +0x0c  grid cell pointer (low bit is a flag)
 *   +0x34  position, 3 x int32 fixed point
 *   +0xc4  orientation, 3x3 int16 rotation matrix, 4096 = 1.0
 * Car slots are 0x274 bytes from 0x6bfb80; slot+0x00 is the car object and
 * slot+0x28 a view struct whose first entry is that car's camera object.
 * A car object points to its four wheel objects at +0x390, +0x454, +0x518
 * and +0x5dc; wheels have world positions of their own, so they are blended
 * too or they would drift against the blended car body.
 * Moving an object needs it relinked into the grid cell for its position:
 * 0x4611d0 unlinks it and 0x460fb0 links it at its current position, as the
 * game's own draw code does when it moves the camera temporarily. */
#include "../runtime.h"
#include "interp.h"
#include "gamefn.h"

#define grid_unlink GCALL(0x4611d0, object_grid_unlink)
#define grid_link GCALL(0x460fb0, object_grid_link)

#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_race_data GP(0x4b1960)
#define g_seed (*(uint32_t *)0x4b11d8) /* the game's Park-Miller generator */

#define MAX_OBJECTS 64 /* per car: body, camera, 4 wheels */
#define WHEEL_POINTERS 0x390
#define WHEEL_POINTER_STRIDE 0xc4
/* A step that moves an object further than this on any axis is a teleport
 * (respawn, reset): draw it at the new position rather than sliding there. */
#define TELEPORT_DISTANCE 0x4000000

/* Cars and cameras are world grid objects (relinked when moved); wheels are
 * not. Car and wheel orientations are unit rotation matrices; a camera's is
 * its view matrix (copied to 0x6ce5a0 by 0x423d10), rows not unit length. */
enum object_kind { KIND_CAR, KIND_WHEEL, KIND_CAMERA, KIND_TV_CAMERA };

/* Only cars and cameras are world grid objects that need relinking. */
static int in_grid(int kind)
{
    return kind == KIND_CAR || kind == KIND_CAMERA;
}

struct pose {
    uint8_t *object;
    int kind; /* enum object_kind */
    int32_t pos[3];
    int16_t rot[9];
};

static struct pose prev[MAX_OBJECTS], curr[MAX_OBJECTS];
static int prev_count, curr_count;

static int32_t abs32(int32_t v)
{
    return v < 0 ? -v : v;
}

static uint32_t isqrt(uint32_t n)
{
    uint32_t root = 0, bit = 1u << 30;

    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static void read_pose(struct pose *p, uint8_t *object)
{
    p->object = object;
    memcpy(p->pos, object + 0x34, sizeof(p->pos));
    memcpy(p->rot, object + 0xc4, sizeof(p->rot));
}

static void write_pose(const struct pose *p)
{
    memcpy(p->object + 0x34, p->pos, sizeof(p->pos));
    memcpy(p->object + 0xc4, p->rot, sizeof(p->rot));
}

static int linked(const uint8_t *object)
{
    return (*(const uint32_t *)(object + 0x0c) & ~1u) != 0;
}

static void relink(uint8_t *object)
{
    if (linked(object)) {
        grid_unlink(object);
        grid_link(object);
    }
}

#define view_count GCALL(0x40d040, view_count) /* views on screen (split screen) */
#define g_view_slots ((const int32_t *)0x6c0a40) /* car slot shown by each view */
#define TV_CAMERAS 0x6cf6e0u
#define TV_CAMERA_SIZE 0x150u

static int contains(uint8_t *const *objects, int count, const uint8_t *object)
{
    int i;

    for (i = 0; i < count; i++)
        if (objects[i] == object)
            return 1;
    return 0;
}

/* Cars, their wheels and their cameras in the current race. */
static int gather(uint8_t **objects, int *kinds)
{
    int count = 0, cars = g_race_data[0x54d], i, w;

    for (i = 0; i < cars && count + 6 <= MAX_OBJECTS; i++) {
        uint8_t *slot = g_car_slots + i * CAR_SLOT_SIZE;
        uint8_t *car = GP(slot);
        const uint32_t *view = (const uint32_t *)GP(slot + 0x28); /* game slot: 32-bit pointers */

        if (car) {
            kinds[count] = KIND_CAR;
            objects[count++] = car;
            for (w = 0; w < 4; w++) {
                uint8_t *wheel = GP(car + WHEEL_POINTERS + w * WHEEL_POINTER_STRIDE);
                if (wheel && wheel != car) {
                    kinds[count] = KIND_WHEEL; /* relinking these corrupts the grid */
                    objects[count++] = wheel;
                }
            }
        }
        if (view && view[0] && GMEM(view[0]) != car) {
            kinds[count] = KIND_CAMERA;
            objects[count++] = GMEM(view[0]);
        }
    }
    /* The camera of every view on screen, in case one shows a slot beyond
     * the race's cars (e.g. replays watching another car). */
    for (i = 0; i < view_count() && i < 4 && count < MAX_OBJECTS; i++) {
        uint8_t *slot = g_car_slots + g_view_slots[i] * CAR_SLOT_SIZE;
        const uint32_t *view = (const uint32_t *)GP(slot + 0x28); /* game slot: 32-bit pointers */

        if (view && view[0] && !contains(objects, count, GMEM(view[0]))) {
            kinds[count] = KIND_CAMERA;
            objects[count++] = GMEM(view[0]);
        }
    }
    /* TV cameras (end-of-race showcase, replays): drawing a view in that mode
     * (0x408270) overwrites the view's camera object from +0x34 on with this
     * per-view state (0x150 bytes each, same layout), and 0x40ad60 restores
     * it afterwards. Track each entry as a pseudo-object 0x34 bytes before it;
     * it is not a grid object and has no other fields. */
    for (i = 0; i < view_count() && i < 4 && count < MAX_OBJECTS; i++) {
        kinds[count] = KIND_TV_CAMERA;
        objects[count++] = GMEM(TV_CAMERAS + i * TV_CAMERA_SIZE - 0x34);
    }
    return count;
}

/* Rows of a 4096-scaled rotation matrix are close to length 4096; anything
 * else at +0xc4 is not an orientation and must not be blended. */
static int valid_rotation(const int16_t *rot)
{
    int r;

    for (r = 0; r < 3; r++) {
        int32_t x = rot[r * 3], y = rot[r * 3 + 1], z = rot[r * 3 + 2];
        int32_t len2 = x * x + y * y + z * z;
        if (len2 < 3500 * 3500 || len2 > 4700 * 4700)
            return 0;
    }
    return 1;
}

void interp_reset(void)
{
    prev_count = curr_count = 0;
}

void interp_record_step(void)
{
    uint8_t *objects[MAX_OBJECTS];
    int kinds[MAX_OBJECTS];
    int count = gather(objects, kinds), i;


    memcpy(prev, curr, sizeof(curr));
    prev_count = curr_count;
    for (i = 0; i < count; i++) {
        read_pose(&curr[i], objects[i]);
        curr[i].kind = kinds[i];
    }
    curr_count = count;
}

static const struct pose *find_prev(const uint8_t *object)
{
    int i;

    for (i = 0; i < prev_count; i++)
        if (prev[i].object == object)
            return &prev[i];
    return 0;
}

/* Blend a -> b by alpha (0..65536), keeping the matrix rows unit length. */
static void blend(struct pose *out, const struct pose *a, const struct pose *b, uint32_t alpha)
{
    int i, r;

    out->object = b->object;
    for (i = 0; i < 3; i++) {
        if (abs32(b->pos[i] - a->pos[i]) > TELEPORT_DISTANCE) {
            *out = *b;
            return;
        }
    }
    for (i = 0; i < 3; i++)
        out->pos[i] = a->pos[i] + (int32_t)(((int64_t)(b->pos[i] - a->pos[i]) * alpha) >> 16);
    out->kind = b->kind;
    if (b->kind == KIND_CAMERA || b->kind == KIND_TV_CAMERA) {
        /* view matrix: plain element blend, consecutive steps are close */
        for (i = 0; i < 9; i++)
            out->rot[i] = (int16_t)(a->rot[i] + (((b->rot[i] - a->rot[i]) * (int32_t)alpha) >> 16));
        return;
    }
    if (!valid_rotation(a->rot) || !valid_rotation(b->rot)) {
        memcpy(out->rot, b->rot, sizeof(out->rot)); /* leave orientation exact */
        return;
    }
    for (r = 0; r < 3; r++) {
        int32_t row[3];
        uint32_t len;

        for (i = 0; i < 3; i++) {
            int32_t from = a->rot[r * 3 + i], to = b->rot[r * 3 + i];
            row[i] = from + (((to - from) * (int32_t)alpha) >> 16);
        }
        len = isqrt((uint32_t)(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]));
        for (i = 0; i < 3; i++)
            out->rot[r * 3 + i] = (int16_t)(len ? row[i] * 4096 / (int32_t)len : row[i]);
    }
}

/* Trigger queue: drawing queues the trackside trigger zones it sees (count at
 * 0x6b9cb8, entries from 0x6b9ba0), and the next physics step fires their
 * actions (0x442890 -> 0x442c20), which is what moves e.g. the power-up drop
 * ships. 0x6b9b94 is a related counter written while drawing (0x459b20). The
 * original draws once per step; queue only from that one draw, not from the
 * extra in-between draws. */
#define TRIGGER_QUEUE_START 0x6b9ba0
#define TRIGGER_QUEUE_END 0x6b9cbc /* through the count at 0x6b9cb8 */
#define g_trigger_counter (*(uint32_t *)0x6b9b94)

struct trigger_queue {
    uint8_t queue[TRIGGER_QUEUE_END - TRIGGER_QUEUE_START];
    uint32_t counter;
};

static void save_trigger_queue(struct trigger_queue *q)
{
    memcpy(q->queue, GMEM(TRIGGER_QUEUE_START), sizeof(q->queue));
    q->counter = g_trigger_counter;
}

static void restore_trigger_queue(const struct trigger_queue *q)
{
    memcpy(GMEM(TRIGGER_QUEUE_START), q->queue, sizeof(q->queue));
    g_trigger_counter = q->counter;
}

/* Other draw-time counters that the original advances once per drawn frame
 * (docs/renderer/views.md 1.7), kept to once per step like the above:
 * - car +0x6a8, the camera shake countdown (s16, 0x466f70);
 * - 0x6c7fdc, the HUD fade-in level (+2 per draw), and per car 4 bytes from
 *   0x6c7fe0: the HUD flash state and its countdown (0x439d70) and the
 *   incoming-missile warning beep countdown +2 (0x438310, which plays the
 *   beep when it finds it at 0 and reloads it);
 * - view slot +0x256 (0x6bfb80 + i * 0x274), the water/lava screen tint
 *   strength (+-8 per draw, 0x479b30). */
#define HUD_COUNTERS 0x6c7fdc
#define HUD_COUNTERS_SIZE 0x14
#define BEEP_COUNTDOWN(car) (*(uint8_t *)(uintptr_t)(0x6c7fe2 + (car) * 4))
#define VIEW_SLOTS 4
#define VIEW_TINT(i) (*(uint8_t *)(uintptr_t)(0x6bfb80 + (i) * 0x274 + 0x256))

struct draw_counters {
    uint8_t hud[HUD_COUNTERS_SIZE];
    uint8_t tint[VIEW_SLOTS];
};

static void save_draw_counters(struct draw_counters *c)
{
    int i;
    memcpy(c->hud, GMEM(HUD_COUNTERS), HUD_COUNTERS_SIZE);
    for (i = 0; i < VIEW_SLOTS; i++)
        c->tint[i] = VIEW_TINT(i);
    /* a beep due now (countdown 0) belongs to the step's own draw: an extra
     * draw must not play it, so it sees 1 (and the value is put back after) */
    for (i = 0; i < 4; i++)
        if (!BEEP_COUNTDOWN(i))
            BEEP_COUNTDOWN(i) = 1;
}

static void restore_draw_counters(const struct draw_counters *c)
{
    int i;
    memcpy(GMEM(HUD_COUNTERS), c->hud, HUD_COUNTERS_SIZE);
    for (i = 0; i < VIEW_SLOTS; i++)
        VIEW_TINT(i) = c->tint[i];
}

void interp_draw(void(GAMEAPI *draw)(void), uint32_t alpha, int hud_tick)
{
    struct trigger_queue triggers;
    struct draw_counters counters;
    struct pose exact[MAX_OBJECTS];
    uint8_t hud[MAX_OBJECTS][2];
    int16_t shake[MAX_OBJECTS];
    uint32_t seed = 0;
    int count = curr_count, i;

    /* HUD flash countdowns (car +0x65c, +0x65d) and the trigger queue advance
     * once per drawn frame in the original; keep them to once per physics step
     * (hud_tick: this draw is the first since a step). */
    for (i = 0; i < count; i++) {
        if (curr[i].kind == KIND_CAR) {
            hud[i][0] = curr[i].object[0x65c];
            hud[i][1] = curr[i].object[0x65d];
            shake[i] = *(int16_t *)(curr[i].object + 0x6a8);
        }
    }
    for (i = 0; i < count; i++) {
        const struct pose *a = find_prev(curr[i].object);
        struct pose mixed;

        read_pose(&exact[i], curr[i].object);
        if (!a)
            continue;
        blend(&mixed, a, &curr[i], alpha);
        write_pose(&mixed);
        if (in_grid(curr[i].kind))
            relink(curr[i].object);
    }

    if (!hud_tick) {
        save_trigger_queue(&triggers);
        save_draw_counters(&counters);
        seed = g_seed;
    }
    draw();
    if (!hud_tick) {
        restore_trigger_queue(&triggers);
        restore_draw_counters(&counters);
        g_seed = seed; /* the random generator physics shares (network races stay in step) */
    }

    for (i = 0; i < count; i++) {
        write_pose(&exact[i]);
        if (in_grid(curr[i].kind))
            relink(curr[i].object);
        if (!hud_tick && curr[i].kind == KIND_CAR) {
            curr[i].object[0x65c] = hud[i][0];
            curr[i].object[0x65d] = hud[i][1];
            *(int16_t *)(curr[i].object + 0x6a8) = shake[i];
        }
    }
}
