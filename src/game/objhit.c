/* Contact spheres against track objects (0x4148b0): the body's list of
 * object polygons (from object_contacts, at body+0x158, 16 bytes each:
 * +8 object, +12 polygon) is swept like the track (trackhit.c), in the
 * object's own frame. Object models are described in track.c.
 *
 * Hitting some objects is gameplay (pickups and the like); those calls
 * are still the original. */
#include "../runtime.h"
#include "gamefn.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "../coverage.h"

#define SLOT_SIZE 0x48
#define g_slots GP(0x6ce740)
#define g_sin_table ((const int16_t *)0x4b24f8)
#define g_cos_table ((const int16_t *)0x4b2578)
#define g_gte_diagonal(i) (*(int16_t *)(0x6ccbe0 + (i) * 8))
#define g_gte_vector2(i) (*(int16_t *)(0x6ccc88 + (i) * 4))
#define g_gte_result ((const int32_t *)0x6cccc8)

#define object_kind1_hit GCALL(0x47c110, object_debris_break)
#define object_kind2_hit GCALL(0x405280, blast_object_hit)
#define object_struck GCALL(0x412bf0, object_part_hit)
#define object_kind4_struck GCALL(0x4427a0, body_triggers_apply)

static void unpack_normal(uint32_t plane, int32_t *n)
{
    n[0] = (int32_t)(plane << 2) >> 22;
    n[1] = (int32_t)(plane << 12) >> 22;
    n[2] = (int32_t)(plane << 22) >> 22;
}

static int32_t outer(const int32_t *m, const int32_t *u, const int32_t *n)
{
    g_gte_diagonal(0) = (int16_t)m[0];
    g_gte_diagonal(1) = (int16_t)m[1];
    g_gte_diagonal(2) = (int16_t)m[2];
    g_gte_vector2(0) = (int16_t)u[0];
    g_gte_vector2(1) = (int16_t)u[1];
    g_gte_vector2(2) = (int16_t)u[2];
    gte_outer_product();
    return g_gte_result[2] * n[2] + g_gte_result[1] * n[1] + g_gte_result[0] * n[0];
}

/* 0x415940: which edges of a face (4 int16 corners, 8 bytes apart; a
 * triangle when shape is 0x9xxxx) is point p (>> 8) outside of, seen along
 * n? -1 none, 0..3 one edge, 0x201.. two. */
int32_t GAMEAPI face_edges_test(const int16_t *corners, uint32_t shape, const int32_t *n, const int32_t *p)
{
    const int16_t *x, *y, *b = corners + 4;
    int32_t triangle = (shape & 0xffff0000) == 0x90000, found = 0, edge = -1;
    int32_t px = p[0] >> 8, py = p[1] >> 8, pz = p[2] >> 8, va[3], vb[3], vx[3], vy[3];

    if (triangle) {
        y = corners;
        x = corners + 8;
    } else {
        x = corners + 12;
        y = corners + 8;
    }
    vx[0] = x[0] - px;
    vx[1] = x[1] - py;
    vx[2] = x[2] - pz;
    va[0] = corners[0] - px;
    va[1] = corners[1] - py;
    va[2] = corners[2] - pz;
    vb[0] = b[0] - px;
    vb[1] = b[1] - py;
    vb[2] = b[2] - pz;
    vy[0] = y[0] - px;
    vy[1] = y[1] - py;
    vy[2] = y[2] - pz;
    if (outer(vb, va, n) < 0) {
        found = 1;
        edge = 0;
    }
    if (outer(vx, vb, n) < 0) {
        if (found)
            return 0x201;
        found = 2;
        edge = 1;
    }
    if (outer(vy, vx, n) < 0) {
        if (found)
            return 0x202;
        found = 3;
        edge = 2;
    }
    if (!triangle && outer(va, vy, n) < 0) {
        if (found)
            return found + 0x203;
        return 3;
    }
    return found ? edge : -1;
}

static void offset_vertex(int16_t *out, const int16_t *v, int32_t x, int32_t y, int32_t z)
{
    out[0] = (int16_t)(v[0] + x);
    out[1] = (int16_t)(v[1] + y);
    out[2] = (int16_t)(v[2] + z);
}

/* 0x414fe0: the slot's sphere (already in the object's frame) against
 * one polygon of an object: a point, an edge or a face. out as
 * sphere_sweep_point; the slot's +0x38 gets the surface. */
int32_t GAMEAPI object_polygon_sweep(uint8_t *body, uint8_t *object, const uint8_t *polygon, uint8_t *slot,
                                     int32_t *out, int32_t dt)
{
    const uint8_t *model = GP(object + 0x10), *planes = GP(object + 0x2c), *face, *plane;
    const int16_t *vertices, *v;
    uint32_t info = U32(polygon, 0x10), shape = info & 0x1f0000, count, i;
    int32_t ox, oy, oz, rx, ry, rz, vx = I32(slot, 0x14), vy = I32(slot, 0x18), vz = I32(slot, 0x1c);
    int32_t vertex = -1, best = -0x7fffffff, best_point[3], best_normal[3], n[3], d, point[3];
    uint8_t edges[0x14], used = 0;
    int16_t corners[16];

    (void)body;
    out[7] = 0;
    if (!shape)
        return dt;
    oz = I32(object, 0x3c) >> 8;
    ox = I32(object, 0x34) >> 8;
    oy = I32(object, 0x38) >> 8;
    rx = I32(slot, 0x08) - ox;
    ry = I32(slot, 0x0c) - oy;
    rz = I32(slot, 0x10) - oz;
    ox >>= 8;
    oz >>= 8;
    oy >>= 8;
    face = model + *(const uint16_t *)(model + 0x38) + (info & 0xff) * 9;
    vertices = (const int16_t *)(GP(model + 0x40) - 8);

    /* outside a face's plane and not reaching it this step: no contact */
    if (shape > 0x20000 && (int32_t)(count = (info >> 16) & 0x1f) > 0) {
        const uint8_t *index = face + 8;

        for (; (int32_t)count > 0; count--, index += 9) {
            int32_t s;

            plane = planes + *index * 8;
            unpack_normal(U32(plane, 4), n);
            v = vertices + plane[0] * 4;
            d = (((rz - (v[2] << 8)) * n[2] + (ry - (v[1] << 8)) * n[1] + (rx - (v[0] << 8)) * n[0]) >> 8) -
                I32(slot, 0x40);
            if (d < 0)
                continue;
            s = (n[2] * vz + n[1] * vy + n[0] * vx) >> 8;
            if (s >= 0)
                return dt;
            if (dt != 0x10000)
                s = mul16(s, dt);
            if (s + d > 0)
                return dt;
        }
    }

    info = U32(polygon, 0x10);
    if ((info & 0x1f0000) <= 0x20000) {
        if ((info & 0x1f0000) == 0x20000) {
            used = 2;
            edges[0] = edges[1] = face[0];
        } else {
            vertex = -99; /* the polygon is a point */
        }
    } else {
        const uint8_t *record = face + 3;

        for (i = 0; i < ((U32(polygon, 0x10) >> 16) & 0x1f); i++, record += 9) {
            int32_t t, r;
            uint32_t shape_bits;

            plane = planes + record[5] * 8;
            point[0] = I32(slot, 0x08);
            point[1] = I32(slot, 0x0c);
            point[2] = I32(slot, 0x10);
            unpack_normal(U32(plane, 4), n);
            v = vertices + plane[0] * 4;
            memcpy(corners, v, 6);
            d = (((rx - (v[0] << 8)) * n[0] + (rz - (v[2] << 8)) * n[2] + (ry - (v[1] << 8)) * n[1]) >> 8) -
                I32(slot, 0x40);
            if (d < 0) {
                t = 0;
                point[0] += (-d * n[0]) >> 8;
                point[1] += (-d * n[1]) >> 8;
                point[2] += (-d * n[2]) >> 8;
            } else {
                int32_t s;

                t = fix_div8(d << 8, -((n[2] * vz + n[1] * vy + n[0] * vx) >> 8));
                if (t > dt) {
                    out[7] = 0;
                    return dt;
                }
                s = t >> 8;
                point[0] += fix_mul8(vx, s);
                point[1] += fix_mul8(vy, s);
                point[2] += fix_mul8(vz, s);
            }
            corners[1] = (int16_t)(corners[1] + oy);
            corners[2] = (int16_t)(corners[2] + oz);
            corners[0] = (int16_t)(corners[0] + ox);
            offset_vertex(corners + 4, vertices + plane[2] * 4, ox, oy, oz);
            offset_vertex(corners + 8, vertices + plane[1] * 4, ox, oy, oz);
            if (plane[3]) {
                offset_vertex(corners + 12, vertices + plane[3] * 4, ox, oy, oz);
                shape_bits = 0x80000;
            } else {
                shape_bits = 0x90000;
            }
            r = face_edges_test(corners, shape_bits, n, point);
            /* note the edges crossed (the face's first four bytes) */
            if (r > 0x200) {
                switch ((r & 0xf) - 2) {
                case 0:
                    edges[used] = record[-2];
                    edges[used + 1] = record[-1];
                    break;
                case 2:
                    edges[used + 1] = record[-3];
                    edges[used] = record[0];
                    break;
                case 3:
                    edges[used] = record[-2];
                    edges[used + 1] = record[0];
                    break;
                case 4:
                    edges[used] = record[-1];
                    edges[used + 1] = record[0];
                    break;
                default:
                    edges[used] = record[-3];
                    edges[used + 1] = record[-2];
                    break;
                }
                used += 2;
            } else if (r > -1) {
                edges[used++] = record[r - 3];
            }
            if (t > 0 && r == -1) {
                /* on the face within the step */
                COVER(COVER_OBJECT_FACE_HIT);
                out[0] = point[0];
                out[1] = point[1];
                out[2] = point[2];
                out[3] = n[0];
                out[4] = n[1];
                out[5] = n[2];
                out[6] = d;
                I32(slot, 0x38) = -100 - (int32_t)(U32(polygon, 0xc) & 0xf);
                out[7] = 1;
                return t;
            }
            if (d > best) {
                best = d;
                memcpy(best_point, point, 12);
                memcpy(best_normal, n, 12);
            }
            if (used >= 0x10) {
                out[7] = 0;
                return dt;
            }
        }
    }

    if (!I32(slot, 0x40)) {
        if (used || vertex)
            return 0;
    } else if (used > 1) {
        /* an edge crossed from two faces: test the sphere against it */
        uint32_t id = 0, j, k;
        int found = 0;

        for (j = 1; j - 1 < used; j++) {
            if (found)
                break;
            id = edges[j - 1];
            for (k = j; k < used && !found; k++)
                if (edges[k] == id)
                    found = 1;
        }
        if (found) {
            const uint8_t *edge = model + *(const uint16_t *)(model + 0x3a) + id * 8;
            int32_t t;

            offset_vertex(corners, vertices + edge[4] * 4, ox, oy, oz);
            offset_vertex(corners + 4, vertices + edge[5] * 4, ox, oy, oz);
            unpack_normal(U32(edge, 0), n);
            t = sphere_sweep_edge(slot, corners, corners + 4, n, out, 0, dt);
            if (out[7] & 0x80003) {
                COVER(COVER_OBJECT_EDGE_HIT);
                I32(slot, 0x38) = -100 - (int32_t)(U32(polygon, 0xc) & 0xf);
                return t;
            }
            if (!(out[7] & 0x100000)) {
                out[7] = 0;
                return dt;
            }
            vertex = out[6] < 0 ? edge[4] : edge[5];
        }
    }
    if (vertex == -1) {
        /* inside every face: overlapping, at the shallowest */
        COVER(COVER_OBJECT_INSIDE);
        memcpy(out, best_point, 12);
        memcpy(out + 3, best_normal, 12);
        out[6] = best;
        I32(slot, 0x38) = -100 - (int32_t)(U32(polygon, 0xc) & 0xf);
        out[7] = 2;
        return 0;
    }
    {
        int32_t at[3], t;

        if (vertex == -99) {
            at[0] = ((const int16_t *)polygon)[0] + ox;
            at[1] = ((const int16_t *)polygon)[1] + oy;
            at[2] = ((const int16_t *)polygon)[2] + oz;
        } else {
            at[0] = vertices[vertex * 4] + ox;
            at[1] = vertices[vertex * 4 + 1] + oy;
            at[2] = vertices[vertex * 4 + 2] + oz;
        }
        at[1] = (int32_t)((uint32_t)at[1] << 8);
        at[0] = (int32_t)((uint32_t)at[0] << 8);
        at[2] = (int32_t)((uint32_t)at[2] << 8);
        t = sphere_sweep_point(slot, at, out, dt);
        if (!out[7])
            return dt;
        COVER(COVER_OBJECT_CORNER_HIT);
        I32(slot, 0x38) = -100 - dt; /* sic: dt, not the surface */
        return t;
    }
}

static void turn(int32_t *x, int32_t *z, int32_t c, int32_t s)
{
    int32_t dx = *x, dz = *z;

    *x = fix_mul8(dz, s) + fix_mul8(dx, c);
    *z = fix_mul8(-dx, s) + fix_mul8(dz, c);
}

/* 0x414ab0: the slot's sphere against the body's object polygons (in
 * their objects' frames). Some objects react: kind 1 and 2 objects
 * (pickups) and others when struck, flagging the contact (0x40 | 0x200
 * or 0x800). out as track; returns the time. */
int32_t GAMEAPI slot_object_sweep(uint8_t *body, uint8_t *slot, int32_t *out, int32_t limit)
{
    int32_t best = limit, result = 0, n, whole = 0;
    const uint8_t *entry = GP(body + 0x158);
    const char *flag = ((const char *)(uintptr_t)*(const uint32_t *)(body + 0x6ac));

    if ((U32(body, FLAGS) & 0x1ff) == 3 && flag != (const char *)0xffffffff && *flag)
        whole = 1;
    n = *(uint16_t *)(body + 0x15e);
    if (n <= 0) {
        out[7] = 0;
        return limit;
    }
    for (; n > 0; n--, entry += 16) {
        uint8_t *object = GP(entry + 8);
        const uint8_t *polygon = GP(entry + 0xc);
        uint32_t kind = object[0x19a], flags;
        int32_t hit[9], x, z, vx, vz, ox, oz, c, s, t, angle;

        out[7] = 0;
        if (kind == 5 || ((kind == 1 || kind == 2) && !I32(slot, 0x40)))
            continue;
        flags = (((uint32_t *)GP(object + 0x194))[U32(polygon, 0xc) & 0xf] >> 24) & 0xf;
        if (flags == 5)
            continue;
        if (flags == 4 && whole) {
            if (*(uint16_t *)(slot + 0x46) != (uint32_t)(int32_t)(int8_t)body[RIGIDITY])
                continue;
        } else if (*(uint16_t *)(slot + 0x46) == (uint32_t)(int32_t)(int8_t)body[RIGIDITY]) {
            continue;
        }
        /* into the object's frame */
        angle = 0x100 - object[0x199];
        c = g_cos_table[angle] >> 6;
        s = g_sin_table[angle] >> 6;
        x = I32(slot, 0x08);
        z = I32(slot, 0x10);
        vx = I32(slot, 0x14);
        vz = I32(slot, 0x1c);
        ox = I32(object, 0x34) >> 8;
        oz = I32(object, 0x3c) >> 8;
        I32(slot, 0x08) = x - ox;
        I32(slot, 0x10) = z - oz;
        turn(&I32(slot, 0x08), &I32(slot, 0x10), c, s);
        I32(slot, 0x08) += ox;
        I32(slot, 0x10) += oz;
        I32(slot, 0x14) = vx;
        I32(slot, 0x1c) = vz;
        turn(&I32(slot, 0x14), &I32(slot, 0x1c), c, s);
        t = object_polygon_sweep(body, object, polygon, slot, hit, best);
        I32(slot, 0x10) = z;
        I32(slot, 0x08) = x;
        I32(slot, 0x14) = vx;
        I32(slot, 0x1c) = vz;
        if (!hit[7])
            continue;
        if (best < limit && (hit[7] & 0x80000)) {
            out[7] = result;
            return best;
        }
        /* back to the world frame */
        c = g_cos_table[object[0x199]] >> 6;
        s = g_sin_table[object[0x199]] >> 6;
        hit[0] -= ox;
        hit[2] -= oz;
        turn(&hit[0], &hit[2], c, s);
        hit[0] += ox;
        hit[2] += oz;
        turn(&hit[3], &hit[5], c, s);
        if (kind == 1) {
            if (object_kind1_hit(object))
                hit[7] = 0x200 | 0x40;
        } else if (kind == 2) {
            if (object_kind2_hit(object))
                hit[7] = 0x200 | 0x40;
        } else {
            int32_t velocity[3];

            velocity[1] = (int32_t)((uint32_t)I32(slot, 0x18) << 8);
            velocity[0] = (int32_t)((uint32_t)I32(slot, 0x14) << 8);
            velocity[2] = (int32_t)((uint32_t)I32(slot, 0x1c) << 8);
            if (object_struck(object, body, U32(polygon, 0xc) & 0xf, velocity)) {
                if (!object[0x198] && kind == 4)
                    object_kind4_struck(object);
                hit[7] = 0x800 | 0x40;
            }
        }
        best = t;
        result = hit[7];
        out[8] = -4;
        out[3] = (int32_t)((uint32_t)hit[3] << 8);
        out[4] = (int32_t)((uint32_t)hit[4] << 8);
        out[5] = (int32_t)((uint32_t)hit[5] << 8);
        out[6] = hit[6];
        out[0] = hit[0];
        out[1] = hit[1];
        out[2] = hit[2];
    }
    out[7] = result;
    return best;
}

/* 0x4148b0: the earliest object hit of the body's parts within its step
 * (from +0x124), recorded like body_track_sweep. A hit by the whole body
 * (a car with +0x6ac set) is kept apart (+0x128 | 0x2000). */
void GAMEAPI body_object_hits(uint8_t *body)
{
    uint8_t *slots = g_slots;
    int32_t limit = I32(body, 0x124), best = limit, current = limit, surface_time = 0x7fffffff;
    uint32_t parts = 0, surface = 0, kind;
    int32_t i = (int8_t)body[RIGIDITY];
    const char *flag = ((const char *)(uintptr_t)*(const uint32_t *)(body + 0x6ac));

    if ((U32(body, FLAGS) & 0x1ff) == 3 && flag != (const char *)0xffffffff && *flag)
        i++;
    for (i--; i >= 0; i--) {
        uint8_t *slot = slots + body[0x140 + i] * SLOT_SIZE;
        int32_t out[9], t;

        t = slot_object_sweep(body, slot, out, current);
        if (current < limit && (out[7] & 0x80000))
            continue;
        if (!out[7])
            continue;
        if (t == best) {
            parts |= 1u << i;
        } else if (t < best) {
            best = t;
            parts = 1u << i;
        }
        memcpy(slot + 0x20, out, 24);
        I32(slot, 0x38) = out[8];
        I32(slot, 0x3c) = out[6];
        if (best < current)
            current = best + 1;
        if ((out[7] & 0x40) && t < surface_time) {
            surface_time = t;
            surface = (uint32_t)out[7] & 0xf00;
        }
    }
    if (!parts)
        return;
    if (best == current)
        kind = 8;
    else if (best == 0)
        kind = surface_time == 0 ? surface | 0x40 : 2;
    else
        kind = surface_time == best ? surface | 0x40 : 1;
    U32(body, 0x128) = kind;
    if (parts & (1u << (body[RIGIDITY] & 31))) {
        U32(body, 0x110) = 0;
        I32(body, 0x124) = best;
        U32(body, 0x128) = kind | 0x2000;
        return;
    }
    I32(body, 0x124) = best;
    U32(body, 0x110) |= parts;
}
