/* The AI's route targets (0x429350, from the thinking step 0x412290):
 * along the planned route (20 entries of 0x34 bytes at 0x6cf220, filled
 * from the top), pick things worth driving at (marker points, weapon
 * items, pickups) and score each, adding a danger cost for hazards and
 * obstacles on the straight line there.
 *
 * Route entry: +0 line, +2 node (or, once a target is set, its index in
 * the target list 0x6cf180), +3 item kind, +0x10 the route's direction
 * (3 x 16.16), +0x25 target type (3 pickup, 4 item, 5 marker), +0x26
 * used, +0x2c score, +0x30 danger. Targets: 8 bytes (int16 x, y, z),
 * count 0x4b7e50 (at most 20), lifetimes 0x6cf640. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

#define g_race (*(uint8_t **)0x4b1960)
#define g_world (*(uint8_t **)0x6c1e30)
#define g_lines (*(uint8_t **)(g_world + 0x60))
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274

#define ROUTE(k) ((uint8_t *)0x6cf220 + (k) * 0x34)
#define ROUTE_LINE(k) (*(uint16_t *)ROUTE(k))
#define ROUTE_NODE(k) (ROUTE(k)[2])
#define ROUTE_DIR(k) ((const int32_t *)(ROUTE(k) + 0x10))
#define ROUTE_TYPE(k) (ROUTE(k)[0x25])
#define ROUTE_USED(k) (ROUTE(k)[0x26])
#define ROUTE_SCORE(k) I32(ROUTE(k), 0x2c)
#define ROUTE_DANGER(k) I32(ROUTE(k), 0x30)
#define ROUTE_COUNT 20

#define g_targets ((int16_t *)0x6cf180)
#define TARGET(i) (g_targets + (i) * 4)
#define g_target_count (*(uint8_t *)0x4b7e50)
#define g_target_life ((uint8_t *)0x6cf640)

/* A racing-line node's flags (+8) and links (+0xc). */
#define NODE_AT(line, node) (*(uint8_t **)(g_lines + (uint16_t)(line) * 0xc + 8) + (uint32_t)(node) * 0x10)

/* Hazards (0x403350): count 0x4b5e54, indices 0x6d0e00, entries at
 * *(0x600664) (8 bytes: object, +6 kind), squared distances 0x6d0e40. */
#define g_hazard_count (*(uint8_t *)0x4b5e54)
#define g_hazard_index ((const uint8_t *)0x6d0e00)
#define g_hazards (*(uint8_t **)0x600664)
#define g_hazard_distance ((const int32_t *)0x6d0e40)

/* Obstacles (0x403ba0): count 0x4b5f9c, indices 0x6d0dc0, entries 0x6d0a80
 * (12 bytes: object, part count, part list), squared distances 0x6d0ce0. */
#define g_obstacle_count (*(uint32_t *)0x4b5f9c)
#define g_obstacle_index ((const uint8_t *)0x6d0dc0)
#define g_obstacles ((uint8_t *)0x6d0a80)
#define g_obstacle_distance ((const int32_t *)0x6d0ce0)
#define g_yaw_cos ((const int16_t *)0x4b2578)
#define g_yaw_sin ((const int16_t *)0x4b24f8)

/* 0x47f3a0: dot product of two int16 vectors. */
int32_t GAMEAPI svec_dot(const int16_t *a, const int16_t *b)
{
    return a[2] * b[2] + a[1] * b[1] + a[0] * b[0];
}

/* 0x403430: the cost of passing hazard idx on the way from `from` along
 * d (squared length len2): only for hazards beside the path (*len and
 * *inv cache its length and 1/length); a bonus (negative) if close to the
 * line, fading with how far along it is at speed. */
int32_t GAMEAPI ai_hazard_cost(const int16_t *from, const int16_t *d, uint32_t idx, int32_t margin, int32_t speed,
                               int32_t extra, int32_t len2, int32_t *len, int32_t *inv)
{
    uint8_t *entry = g_hazards + idx * 8, *obj = *(uint8_t **)entry;
    int32_t radius = *(uint16_t *)(*(uint8_t **)(obj + 0x10) + 0x32), dist = g_hazard_distance[idx];
    int16_t rel[3], perp[3];
    int32_t t, m, k, gap, r, limit, e, i;

    rel[0] = (int16_t)((I32(obj, 0x34) >> 16) - from[0]);
    rel[1] = (int16_t)((I32(obj, 0x38) >> 16) - from[1]);
    rel[2] = (int16_t)((I32(obj, 0x3c) >> 16) - from[2]);
    if (dist < extra || dist > extra * 2 + len2)
        return 0;
    if (*len == -1) {
        *len = (int32_t)fix_sqrt_table(len2);
        *inv = 0x10000 / *len;
    }
    t = (*inv * svec_dot(rel, d)) >> 16;
    if (t < 0 || t > *len + radius)
        return 0;
    m = *inv * t;
    for (i = 0; i < 3; i++)
        perp[i] = (int16_t)(rel[i] - (int16_t)((d[i] * m) >> 16));
    gap = (int32_t)svec_length(perp);
    k = entry[6] == 2 ? 0x37 : entry[6] == 3 ? 0x3c : 0x32;
    if (gap > radius + margin * 2)
        return 0;
    if (gap < (radius >> 1) + margin)
        r = -k;
    else
        r = (gap - margin * 2 - radius) * k / ((radius >> 1) + margin);
    limit = speed < 100 ? 900 : (int32_t)((uint32_t)speed << 11) - 0x31c7c;
    e = t * t + extra * 2;
    if (e > limit * 16)
        return r >> 5;
    if (e <= limit)
        return r;
    return (r - (r >> 5)) * (limit * 16 - e) / (limit * 16 - limit) + (r >> 5);
}

/* 0x403350: the hazards' cost along from -> to (none unless the car is
 * minded to, flag 0x20). */
int32_t GAMEAPI ai_hazards_cost(const uint8_t *car, const int16_t *from, const int16_t *to, int32_t margin,
                                int32_t speed, int32_t extra)
{
    int32_t len = -1, inv = 0, total = 0, len2, i;
    int16_t d[3];

    if (!g_hazard_count || !(car[0x6dc] & 0x20))
        return 0;
    d[0] = (int16_t)(to[0] - from[0]);
    d[1] = (int16_t)(to[1] - from[1]);
    d[2] = (int16_t)(to[2] - from[2]);
    len2 = svec_length_sq(d);
    if (len2 < 4)
        return 0;
    for (i = 0; i < g_hazard_count; i++)
        total += ai_hazard_cost(from, d, g_hazard_index[i], margin, speed, extra, len2, &len, &inv);
    return total;
}

/* 0x403d90: does the segment a -> b (whole units) pass into any convex
 * part of obstacle object obj (entry idx), grown by margin? Each part's
 * faces (planes at obj +0x2c, vertices in its model) are tested in the
 * object's frame (turned by its yaw +0x199). */
int32_t GAMEAPI ai_obstacle_hit(const int16_t *a, const int16_t *b, const uint8_t *obj, int32_t margin,
                                uint32_t idx)
{
    const uint8_t *entry = g_obstacles + idx * 12, *model;
    uint32_t parts = U32(entry, 4), j;
    const uint8_t *const *list;
    int32_t ox16, oy16, oz16, ox8, oz8, c, s, ax, az, dx, dz, px8, pz8, dx8, dz8, ay, dy;

    if (!parts)
        return 0;
    ox16 = I32(obj, 0x34) >> 16;
    oy16 = I32(obj, 0x38) >> 16;
    oz16 = I32(obj, 0x3c) >> 16;
    model = *(uint8_t **)(obj + 0x10);
    c = g_yaw_cos[0x100 - obj[0x199]] >> 6;
    s = g_yaw_sin[0x100 - obj[0x199]] >> 6;
    ox8 = I32(obj, 0x34) >> 8;
    oz8 = I32(obj, 0x3c) >> 8;
    ax = (int32_t)((uint32_t)a[0] << 8) - ox8;
    az = (int32_t)((uint32_t)a[2] << 8) - oz8;
    px8 = (fix_mul8(ax, c) + fix_mul8(az, s) + ox8) >> 8;
    pz8 = (fix_mul8(-ax, s) + fix_mul8(az, c) + oz8) >> 8;
    dx = (int32_t)((uint32_t)(b[0] - a[0]) << 8);
    dz = (int32_t)((uint32_t)(b[2] - a[2]) << 8);
    dx8 = (fix_mul8(dx, c) + fix_mul8(dz, s)) >> 8;
    dz8 = (fix_mul8(-dx, s) + fix_mul8(dz, c)) >> 8;
    ay = a[1];
    dy = b[1] - a[1];
    list = *(const uint8_t *const **)(entry + 8);
    for (j = 0; j < parts; j++) {
        const uint8_t *part = list[j], *face, *planes = *(uint8_t **)(obj + 0x2c);
        const uint8_t *verts = *(uint8_t **)(model + 0x40) - 8;
        uint32_t flags, faces, f;

        if (!part)
            return 0;
        flags = U32(part, 0x10);
        faces = flags >> 16 & 0x1f;
        if (!faces)
            return 1;
        face = model + *(uint16_t *)(model + 0x38) + (flags & 0xff) * 9 + 8;
        for (f = 0; f < faces; f++, face += 9) {
            const uint8_t *plane = planes + face[0] * 8;
            const int16_t *v = (const int16_t *)(verts + plane[0] * 8);
            int32_t packed = I32(plane, 4);
            int32_t nz = (int32_t)((uint32_t)packed << 22) >> 22, ny = (int32_t)((uint32_t)packed << 12) >> 22;
            int32_t nx = (int32_t)((uint32_t)packed << 2) >> 22, dist, e;
            int16_t x = (int16_t)(v[0] + ox16), y = (int16_t)(v[1] + oy16), z = (int16_t)(v[2] + oz16);

            dist = (((pz8 - z) * nz + (ay - y) * ny + (px8 - x) * nx) >> 8) - margin;
            if (dist < 0)
                continue;
            e = ny * dy + nz * dz8 + nx * dx8;
            if (e >= 0 || (e >> 8) + dist > 0)
                break;
        }
        if (f >= faces)
            return 1;
    }
    return 0;
}

/* 0x403c00: the cost of obstacle idx in the way of a -> b: none if it
 * cannot reach the path (by squared distances), else a big penalty
 * easing with distance at speed. */
int32_t GAMEAPI ai_obstacle_cost(const int16_t *a, const int16_t *b, uint32_t idx, int32_t margin, int32_t speed,
                                 int32_t extra)
{
    const uint8_t *obj = *(uint8_t **)(g_obstacles + idx * 12);
    int32_t dist = g_obstacle_distance[idx], r2, len2, limit;
    uint32_t radius;
    int16_t d[3];

    if (obj[0x19a] == 5)
        return 0;
    radius = *(uint16_t *)(*(uint8_t **)(obj + 0x10) + 0x32);
    r2 = (int32_t)(radius * radius);
    if (dist > r2) {
        if (r2 + dist * 2 < extra)
            return 0;
    } else if (dist + r2 * 2 < extra) {
        return 0;
    }
    d[0] = (int16_t)(b[0] - a[0]);
    d[1] = (int16_t)(b[1] - a[1]);
    d[2] = (int16_t)(b[2] - a[2]);
    len2 = svec_length_sq(d);
    if (len2 > r2) {
        if (len2 > extra) {
            if (!extra) {
                if (dist > r2 + len2 * 2)
                    return 0;
            } else if (dist > r2 + len2 * 8 + extra) {
                return 0;
            }
        } else if (dist > len2 + extra * 8 + r2) {
            return 0;
        }
    } else if (r2 > extra) {
        if (!extra) {
            if (dist > len2 + r2 * 2)
                return 0;
        } else if (dist > len2 + r2 * 8 + extra) {
            return 0;
        }
    } else if (dist > len2 + extra * 8 + r2) {
        return 0;
    }
    if (!ai_obstacle_hit(a, b, obj, margin, idx))
        return 0;
    limit = speed < 100 ? 900 : (int32_t)((uint32_t)speed << 10) - 0x18c7c;
    if (dist > r2 + limit * 18)
        return -0x10;
    if (dist <= r2 + limit * 2)
        return -1000;
    return (dist - limit * 9 - r2) * 0x3d8 / (limit * 8) - 0x10;
}

/* 0x403ba0: all obstacles' cost along a -> b. */
int32_t GAMEAPI ai_obstacles_cost(const int16_t *a, const int16_t *b, int32_t margin, int32_t speed, int32_t extra)
{
    int32_t total = 0;
    uint32_t i;

    for (i = 0; i < g_obstacle_count; i++)
        total += ai_obstacle_cost(a, b, g_obstacle_index[i], margin, speed, extra);
    return total;
}

/* 0x4117d0: the danger of driving straight from the car to route entry
 * k's target: hazards and/or obstacles. */
int32_t GAMEAPI ai_target_danger(const uint8_t *car, int32_t k, int32_t hazards, int32_t obstacles)
{
    const int16_t *target = TARGET(ROUTE_NODE(k));
    int32_t radius = *(uint16_t *)(*(uint8_t **)(car + 0x10) + 0x32), speed = *(const int16_t *)(car + 0x658);
    int32_t total = 0;
    int16_t at[3];

    at[0] = (int16_t)(I32(car, 0x34) >> 16);
    at[1] = (int16_t)(I32(car, 0x38) >> 16);
    at[2] = (int16_t)(I32(car, 0x3c) >> 16);
    if (hazards)
        total = ai_hazards_cost(car, at, target, radius, speed, 0);
    if (obstacles)
        total += ai_obstacles_cost(at, target, radius, speed, 0);
    return total;
}

/* 0x42a1a0: route entry k's danger, plus (when the entry before is on the
 * same line) the hazards from the target back to entry k's node. */
int32_t GAMEAPI ai_target_route_danger(const int16_t *target, int32_t len2, int32_t k, const uint8_t *car)
{
    int32_t radius = *(uint16_t *)(*(uint8_t **)(car + 0x10) + 0x32);
    const int16_t *p;
    int16_t node[3];

    if (k - 1 < 0 || ROUTE_LINE(k - 1) != ROUTE_LINE(k))
        return ROUTE_DANGER(k);
    p = (const int16_t *)NODE_AT(ROUTE_LINE(k), ROUTE_NODE(k));
    node[0] = p[0];
    node[1] = p[1];
    node[2] = p[2];
    return ai_hazards_cost(car, target, node, radius, *(const int16_t *)(car + 0x658), len2) + ROUTE_DANGER(k - 1);
}

/* Record target `at` for route entry *n (type), scoring it, and move on:
 * shared by the three target finders. Returns 0 once the route or the
 * target list is full. */
static int32_t take_target(uint8_t *car, int32_t *n, int32_t k, const int16_t *at, uint8_t type, int32_t score,
                           int32_t obstacles, int32_t len2, int32_t kind, int32_t has_kind)
{
    uint8_t index = g_target_count;
    int32_t entry;

    TARGET(index)[0] = at[0];
    TARGET(index)[1] = at[1];
    TARGET(index)[2] = at[2];
    ROUTE_NODE(*n) = index;
    if (has_kind)
        ROUTE(*n)[3] = (uint8_t)kind;
    ROUTE_TYPE(*n) = type;
    ROUTE_SCORE(*n) += score;
    entry = *n;
    ROUTE_SCORE(entry) += ai_target_danger(car, entry, 1, obstacles);
    entry = *n;
    ROUTE_DANGER(entry) += ai_target_route_danger(TARGET(g_target_count), len2, k, car);
    g_target_life[g_target_count] = 0x1f;
    (*n)--;
    g_target_count++;
    return *n >= 0 && g_target_count < 0x14;
}

/* How much a target ahead is worth: by how well it lines up with the
 * route's direction (at least `ahead`), scaled, less a quarter when far. */
static int32_t target_worth(const int32_t *d, int32_t k, int32_t length, int32_t ahead, int32_t scale)
{
    int32_t q = vec_dot(d, ROUTE_DIR(k)) / length, v;

    if (q < ahead)
        return 0;
    v = mul16(q, scale) >> 16;
    if (length > 1000)
        v -= v >> 2;
    return v;
}

/* 0x429ea0: pickups (flag 4, with an empty weapon slot): live pickups
 * (list 0x6b8ce0, count 0x6b8ce4) sitting on route nodes marked 0x100. */
void GAMEAPI ai_route_add_pickups(uint8_t *car, int32_t *n)
{
    int32_t k;

    if (!(car[0x6dc] & 4) || (car[0x67c] != 0xff && car[0x67d] != 0xff))
        return;
    for (k = *n + 1; k < ROUTE_COUNT; k++) {
        uint8_t *const *list;
        uint16_t line, node;
        int32_t count;

        if (*n < 0 || g_target_count >= 0x14)
            return;
        line = ROUTE_LINE(k);
        if (!line || ROUTE_USED(k))
            continue;
        node = ROUTE_NODE(k);
        if (!(U32(NODE_AT(line, node), 8) & 0x100))
            continue;
        list = *(uint8_t *const **)0x6b8ce0;
        for (count = *(int32_t *)0x6b8ce4; count > 0; count--, list++) {
            const uint8_t *pickup = *list;
            uint16_t flags = *(const uint16_t *)(pickup + 0x182);
            int32_t d[3], len2, length, v;
            int16_t s[3];

            if (!(flags & 0x80) || !(flags & 3) || *(const uint16_t *)(pickup + 0x18c) != line ||
                *(const uint16_t *)(pickup + 0x18e) != node)
                continue;
            d[0] = I32(pickup, 0x34) - I32(car, 0x34);
            d[1] = I32(pickup, 0x38) - I32(car, 0x38);
            d[2] = I32(pickup, 0x3c) - I32(car, 0x3c);
            s[0] = (int16_t)(d[0] >> 16);
            s[1] = (int16_t)(d[1] >> 16);
            s[2] = (int16_t)(d[2] >> 16);
            len2 = svec_length_sq(s);
            if (len2 > 0x225510 || len2 < 90000)
                continue;
            length = (int32_t)fix_sqrt_table(len2);
            v = target_worth(d, k, length, 0xf03c, 0x1f0000);
            if (v <= 0)
                continue;
            s[0] = (int16_t)(I32(pickup, 0x34) >> 16);
            s[1] = (int16_t)(I32(pickup, 0x38) >> 16);
            s[2] = (int16_t)(I32(pickup, 0x3c) >> 16);
            if (!take_target(car, n, k, s, 3, v, 1, len2, 0, 0))
                return;
        }
    }
}

/* 0x429b50: weapon items (flag 0x800; list *(0x6c1eb0)+0xec of 20-byte
 * records, count 0x5f72a4) on route nodes marked 0x200, except kind 5. */
void GAMEAPI ai_route_add_items(uint8_t *car, int32_t *n)
{
    int16_t at[3];
    int32_t k;

    if (!(U32(car, 0x6dc) & 0x800))
        return;
    at[0] = (int16_t)(I32(car, 0x34) >> 16);
    at[1] = (int16_t)(I32(car, 0x38) >> 16);
    at[2] = (int16_t)(I32(car, 0x3c) >> 16);
    for (k = *n + 1; k < ROUTE_COUNT; k++) {
        const uint8_t *item;
        uint16_t line, node;
        uint32_t count, j;

        if (*n < 0 || g_target_count >= 0x14)
            return;
        line = ROUTE_LINE(k);
        if (!line || ROUTE_USED(k))
            continue;
        node = ROUTE_NODE(k);
        if (!(U32(NODE_AT(line, node), 8) & 0x200))
            continue;
        item = *(uint8_t **)(*(uint8_t **)0x6c1eb0 + 0xec);
        count = *(uint32_t *)0x5f72a4;
        for (j = 0; j < count; j++, item += 0x14) {
            const uint8_t *a = *(uint8_t **)item, *b = *(uint8_t **)(item + 4);
            const int16_t *p = (const int16_t *)(item + 8);
            int32_t d[3], len2, length, v;
            int16_t s[3];

            if ((U32(*(uint8_t **)(b + 0x194), (U32(a, 0xc) & 0xf) * 4) & 0xf000000) == 0x5000000 ||
                item[0x12] != line || *(const uint16_t *)(item + 0x10) != node)
                continue;
            s[0] = (int16_t)(p[0] - at[0]);
            s[1] = (int16_t)(p[1] - at[1]);
            s[2] = (int16_t)(p[2] - at[2]);
            len2 = svec_length_sq(s);
            d[0] = (int32_t)((uint32_t)s[0] << 16);
            d[1] = (int32_t)((uint32_t)s[1] << 16);
            d[2] = (int32_t)((uint32_t)s[2] << 16);
            if (len2 > 0x225510)
                continue;
            length = (int32_t)fix_sqrt_table(len2);
            v = target_worth(d, k, length, 0xf03c, 0x190000);
            if (v <= 0)
                continue;
            if (!take_target(car, n, k, p, 4, v, 0, len2, b[0x192], 1))
                return;
        }
    }
}

/* 0x429380: marker points (flag 8; *(0x6c1e30)+8: +0xe4 list of 20-byte
 * records, +0xf5 count) on route nodes marked 0x400. */
void GAMEAPI ai_route_add_markers(uint8_t *car, int32_t *n)
{
    int32_t k;

    if (!(car[0x6dc] & 8))
        return;
    for (k = *n + 1; k < ROUTE_COUNT; k++) {
        const uint8_t *world = *(uint8_t **)(g_world + 8);
        const int16_t *marker;
        uint16_t line, node;
        int32_t count;

        if (*n < 0 || g_target_count >= 0x14)
            return;
        line = ROUTE_LINE(k);
        if (!line || ROUTE_USED(k))
            continue;
        node = ROUTE_NODE(k);
        if (!(U32(NODE_AT(line, node), 8) & 0x400))
            continue;
        marker = *(const int16_t **)(world + 0xe4);
        for (count = world[0xf5]; count > 0; count--, marker += 10) {
            int32_t d[3], len2, length, v;
            int16_t s[3];

            if (((const uint8_t *)marker)[0x12] != line || (uint16_t)marker[8] != node)
                continue;
            d[0] = (int32_t)((uint32_t)marker[0] << 16) - I32(car, 0x34);
            d[1] = (int32_t)((uint32_t)marker[1] << 16) - I32(car, 0x38);
            d[2] = (int32_t)((uint32_t)marker[2] << 16) - I32(car, 0x3c);
            s[0] = (int16_t)(d[0] >> 16);
            s[1] = (int16_t)(d[1] >> 16);
            s[2] = (int16_t)(d[2] >> 16);
            len2 = svec_length_sq(s);
            if (len2 > 0x225510 || len2 < 90000)
                continue;
            length = (int32_t)fix_sqrt_table(len2);
            v = target_worth(d, k, length, 0xdcb4, 0x1e0000);
            if (v <= 0)
                continue;
            if (!take_target(car, n, k, marker, 5, v, 1, len2, 0, 0))
                return;
        }
    }
}

/* 0x4297a0: how much the nearest player matters to route choice. */
int32_t GAMEAPI ai_player_weight(const uint8_t *car)
{
    int32_t gap = I32(car, 0x6cc);

    if ((int8_t)car[0x6ca] == -1)
        return 0x8000;
    if (gap < -0x2000)
        return 0x10000;
    if (gap < 0x2000)
        return 0x8000;
    if (gap < 0x4000)
        return 0x4000;
    return gap >= 0x8000 ? -0x10000 : -0x4000;
}

/* 0x429ad0: is the given line the one car slot `player` plans to take, or
 * one of the two linked from its next node (+0xc low and bits 14..18)? */
int32_t GAMEAPI ai_player_takes_line(int32_t player, uint32_t line)
{
    const uint8_t *slot = g_car_slots + player * CAR_SLOT_SIZE;
    uint16_t target = (*(uint8_t **)(slot + 0x64))[5];
    uint32_t links;

    if (target == (uint16_t)line)
        return 1;
    links = U32(NODE_AT(target, *(const uint16_t *)(slot + 0x68)), 0xc);
    if ((links & 0x1f) == (line & 0xffff))
        return 1;
    return (links >> 14 & 0x1f) == (line & 0xffff);
}

/* 0x4298e0: how good a branch (route entry k) is: its own worth (node
 * bits 30..31) weighted by the player, less its risk (bits 28..29) by
 * the car's caution; following or avoiding the player near it; doubled
 * on the last lap. */
int32_t GAMEAPI ai_route_branch_score(const uint8_t *car, int32_t weight, int32_t k)
{
    const uint8_t *slot = g_car_slots + *(const uint16_t *)(car + 0x64e) * CAR_SLOT_SIZE;
    int32_t player = (int8_t)car[0x6ca], gap = I32(car, 0x6cc), worth, risk, caution, score;
    uint8_t last_lap = g_race[0x586];
    uint32_t flags = U32(NODE_AT(ROUTE_LINE(k), ROUTE_NODE(k)), 8);

    switch (flags >> 30) {
    case 0:
        worth = -0x1e0000;
        break;
    case 2:
        worth = 0x1e0000;
        break;
    case 3:
        worth = 0x3c0000;
        break;
    default:
        worth = 0xa0000;
        break;
    }
    caution = ((const int32_t *)0x4a7c78)[U32(car, 0x6e8) >> 15 & 7];
    switch (flags >> 28 & 3) {
    case 1:
        risk = mul16(0xf0000, caution);
        break;
    case 2:
        risk = mul16(0x1e0000, caution);
        break;
    case 3:
        risk = mul16(0x3c0000, caution);
        break;
    default:
        risk = mul16(0, caution);
        break;
    }
    score = mul16(worth, weight);
    if (player != -1 && abs32(gap) < 0x2000) {
        if (ai_player_takes_line(player, ROUTE_LINE(k)))
            score += 0x100000;
        else if (gap < 0 && gap > -0x1000)
            score -= 0x40000;
    }
    if (slot[0x2c] == last_lap)
        risk = gap < 0 ? risk >> 1 : (int32_t)((uint32_t)risk << 1);
    score -= risk;
    if (slot[0x2c] == last_lap)
        score = (int32_t)((uint32_t)score << 1);
    return score >> 16;
}

/* 0x429680: score the route's branch points (nodes marked 4) above n;
 * easy drivers (without flag 0x40) just mark them down. Entries after a
 * branch on the same line share its score. */
void GAMEAPI ai_route_branches(uint8_t *car, int32_t n)
{
    int32_t simple = 0, have_weight = 0, weight = 0, k, j;

    if (!(car[0x6dc] & 0x40)) {
        if (car[0x6e1] > 2)
            return;
        simple = 1;
    }
    for (k = 0x13; k > n; k--) {
        uint16_t line = ROUTE_LINE(k);
        int32_t score;

        if (!line || !(NODE_AT(line, ROUTE_NODE(k))[8] & 4))
            continue;
        if (simple) {
            ROUTE_SCORE(k) -= 0x14;
            continue;
        }
        if (!have_weight) {
            weight = ai_player_weight(car);
            have_weight = 1;
        }
        score = ai_route_branch_score(car, weight, k);
        ROUTE_SCORE(k) += score;
        for (j = k - 1; j > n && ROUTE_LINE(j) == line; j--)
            ROUTE_SCORE(j) += score;
        k = j + 1;
    }
}

/* 0x429350 */
void GAMEAPI ai_route_targets(uint8_t *car, int32_t *n)
{
    ai_route_add_pickups(car, n);
    ai_route_add_items(car, n);
    ai_route_branches(car, *n);
    ai_route_add_markers(car, n);
}
