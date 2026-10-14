/* When does a contact sphere first hit the track within a step
 * (0x415ea0), walking the body's list of nearby track polygons (from
 * polygon_contacts, 16 bytes each: +0 kind | 0x10 done, +1 corner,
 * +2 edge code, +3 tile width, +4 cell key, +8 plane, +12 vertices).
 *
 * Each polygon's plane is tested first; a hit point found there must then
 * lie inside the polygon's edges. A point outside is handed to the polygon
 * across the edge it crossed (by cell key), or, with a zero radius, kept
 * as an edge code in +2. With a radius, the second pass tests those edges
 * themselves (0x416d40), averaging two contacts across a shallow fold. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"
#include "../coverage.h"

typedef int32_t(GAMEAPI *normalise_fn)(const int32_t *v, int32_t *out);
/* 0x4290b0: src/game/orig_pc.c's vec_normalise_x87, an x87 asm entry taking its
 * arguments on the stack (no C prototype in gamefn.h) */
#define vec_normalise_float ((normalise_fn)(uintptr_t)0x4290b0)

/* Per polygon results of the plane test, 0x28 bytes each: point, normal,
 * distance (+0x18), time (+0x24). */
#define g_track_hits ((uint8_t *)0x6ce760)
#define g_tile(i) GP(GP(GP(0x6c1e30)) + (i) * 4)
#define g_parity (*(uint32_t *)0x6c0a50)
#define g_gte_diagonal(i) (*(int16_t *)(0x6ccbe0 + (i) * 8))
#define g_gte_vector2(i) (*(int16_t *)(0x6ccc88 + (i) * 4))
#define g_gte_result ((const int32_t *)0x6cccc8)
/* the last wall touched, for the game */
#define g_wall_point ((int32_t *)0x6c80f0)
#define g_wall_vertex ((int16_t *)0x6c80e0)
#define g_wall_normal ((int32_t *)0x6c8010)
#define g_wall_time (*(uint32_t *)0x6c8004)

static void unpack_normal(uint32_t plane, int32_t *n)
{
    n[0] = (int32_t)(plane << 2) >> 22;
    n[1] = (int32_t)(plane << 12) >> 22;
    n[2] = (int32_t)(plane << 22) >> 22;
}

static uint32_t kind_of(const uint8_t *body)
{
    return U32(body, FLAGS) & 0x1ff;
}

/* 0x416cd0 */
void GAMEAPI wall_touched(const int32_t *contact, const int16_t *vertex)
{
    g_wall_point[1] = contact[1];
    g_wall_point[0] = contact[0];
    g_wall_point[2] = contact[2];
    g_wall_vertex[1] = vertex[1];
    g_wall_vertex[0] = vertex[0];
    g_wall_vertex[2] = vertex[2];
    g_wall_normal[1] = contact[4];
    g_wall_normal[0] = contact[3];
    g_wall_normal[2] = contact[5];
    g_wall_time = g_parity;
}

/* 0x416d40: the sphere against the edge a polygon's point left it by
 * (+2), shared with polygon next (0 for the track's border). Across a
 * shallow fold (normals within ~10 degrees) or between two walls, the two
 * plane hits are averaged instead. */
void GAMEAPI track_edge_hit(uint8_t *slot, int32_t *out, int32_t *best, int32_t *state, const uint8_t *entry,
                            const uint8_t *next, const uint8_t *list)
{
    const int16_t *v = ((const int16_t *)(uintptr_t)*(const uint32_t *)(entry + 0xc)), *a, *b;
    uint32_t width = entry[3], kind = entry[0] & 0xf;
    int32_t smooth_edge = 0, d[3], hit[8], t;

    if (kind >= 6 && (next[0] & 0xf) < 6 && kind == 6)
        v -= (width + 1) * 4;
    if (next) {
        int32_t na[3], nb[3];

        unpack_normal(U32(entry, 8), na);
        unpack_normal(U32(next, 8), nb);
        if (nb[2] * na[2] + nb[1] * na[1] + nb[0] * na[0] > 63000 || (kind >= 6 && (next[0] & 0xf) >= 6)) {
            const uint8_t *ha = g_track_hits + ((uint32_t)(entry - list) >> 4) * 0x28;
            const uint8_t *hb = g_track_hits + ((uint32_t)(next - list) >> 4) * 0x28;

            out[1] = (I32(hb, 4) + I32(ha, 4)) >> 1;
            out[2] = (I32(hb, 8) + I32(ha, 8)) >> 1;
            out[0] = (I32(ha, 0) + I32(hb, 0)) >> 1;
            out[3] = (int32_t)((uint32_t)(nb[0] + na[0]) << 7);
            out[4] = (int32_t)((uint32_t)(nb[1] + na[1]) << 7);
            out[5] = (int32_t)((uint32_t)(nb[2] + na[2]) << 7);
            COVER(COVER_TRACK_FOLD);
            out[6] = I32(ha, 0x18);
            *best = I32(ha, 0x24);
            out[8] = (int32_t)U32(I32(ha, 0x18) < I32(hb, 0x18) ? entry : next, 4);
            *state = *best ? 1 : 2;
            return;
        }
    }
    /* vertices along the grid: v[1] is the next in the row, v[width + 1]
     * the one in the next row; byte 6 of a vertex flags edges to skip */
    switch ((uint32_t)(int32_t)(int8_t)entry[2]) {
    case 0:
        a = v;
        b = v + 4;
        smooth_edge = (((const uint8_t *)b)[6] & 0x10) != 0;
        break;
    case 1:
        b = v + (width + 2) * 4;
        smooth_edge = (((const uint8_t *)b)[6] & 0x20) != 0;
        a = v + 4;
        break;
    case 2:
        a = v + (width + 1) * 4;
        smooth_edge = (((const uint8_t *)a)[6] & 0x10) != 0;
        b = a + 4;
        break;
    case 3:
        a = v;
        smooth_edge = (((const uint8_t *)v)[6] & 0x20) != 0;
        b = v + (width + 1) * 4;
        break;
    case 4:
        a = v;
        b = v + (width + 2) * 4;
        break;
    case 5:
        a = v + 4;
        b = a + width * 4;
        break;
    default:
        return;
    }
    if (kind == 0 && smooth_edge)
        return;
    d[1] = b[1] - a[1];
    d[2] = b[2] - a[2];
    d[0] = b[0] - a[0];
    vec_normalise_float(d, d);
    d[0] >>= 4;
    d[1] >>= 4;
    d[2] >>= 4;
    t = sphere_sweep_edge(slot, a, b, d, hit, 0, *best);
    if ((int8_t)hit[7]) {
        COVER(COVER_TRACK_EDGE_HIT);
        *state = hit[7];
        out[3] = (int32_t)((uint32_t)hit[3] << 8);
        out[4] = (int32_t)((uint32_t)hit[4] << 8);
        out[5] = (int32_t)((uint32_t)hit[5] << 8);
        out[0] = hit[0];
        out[1] = hit[1];
        out[2] = hit[2];
        out[8] = (int32_t)U32(entry, 4);
        out[6] = hit[6];
        *best = t;
    }
}

/* Does the edge code of a polygon already done (its +2) meet the edge
 * code just found? */
static int edges_meet(int32_t done, int32_t code)
{
    if (done < 0)
        return 0;
    if (code > 0xf) {
        int32_t e;

        if (done > 3)
            return 0;
        e = (done - 2) & 3;
        return e == code >> 4 || e == (code & 0xf);
    }
    if (code > 3)
        return abs32(done - code) == 1;
    return ((code - 2) & 3) == done >> 4 || ((code - 2) & 3) == (done & 0xf);
}

static int sides_meet(int32_t edge, int32_t side)
{
    if (edge > 0xf)
        return edge >> 4 == side || (edge & 0xf) == side;
    return (edge & 0xf) == side;
}

/* 0x415ea0: the earliest time before limit (16.16 of a step; bits 24..30
 * set: skip the walls) the slot's sphere hits the body's track polygons.
 * out: point, normal (1 << 16), distance, state (as track_edge_hit), the
 * cell key (out[8]). */
uint32_t GAMEAPI slot_track_sweep(uint8_t *body, uint8_t *slot, int32_t *out, uint32_t limit)
{
    uint8_t *list = GP(body + 0x154), *entry, *next = 0, *p;
    int32_t total = *(uint16_t *)(body + 0x15c), remaining = total, radius = I32(slot, 0x40);
    int32_t prev_kind = 0, prev_plane = -1, index = 0, edge_hits = 0, code = 0, result = 0;
    int32_t dist = 0x7fffffff, state = 0, t, best, n[3] = {0, 0, 0}, point[3] = {0, 0, 0}, i;
    const int16_t *prev_vertices = 0;
    uint32_t prev_key = 0;
    int32_t x, y, z, vx, vy, vz;

    for (i = 0, p = list; i < total; i++, p += 16) {
        uint8_t kind = p[0];

        p[2] = 0xff;
        p[0] = kind & ~0x10;
        if (((int32_t)limit >> 24) && (kind & 0xf) >= 6) {
            p[0] |= 0x10;
            remaining--;
        }
    }
    if (!remaining) {
        out[7] = 0;
        return limit;
    }
    x = I32(slot, 0x08);
    y = I32(slot, 0x0c);
    z = I32(slot, 0x10);
    vx = I32(slot, 0x14);
    vy = I32(slot, 0x18);
    vz = I32(slot, 0x1c);
    limit &= 0x7fffff;
    t = (int32_t)limit;
    best = (int32_t)limit;

    do {
        const int16_t *vertices, *v, *a, *b, *c, *d, *first;
        int16_t wall_a[4], wall_b[4];
        uint32_t key, plane, kind, width;
        int32_t fresh = 1, edge = -1, crossed = 0, m[3], u[3], start[3], px, py, pz, rotation;

        if (next) {
            entry = next;
            next = 0;
        } else {
            entry = list + index * 16;
            index++;
            while (entry[0] & 0x10) {
                entry += 16;
                index++;
            }
        }
        entry[0] |= 0x10;
        remaining--;
        kind = entry[0] & 0xf;
        key = U32(entry, 4);
        plane = U32(entry, 8);
        vertices = (const int16_t *)GP(entry + 0xc);

        if ((int32_t)plane == prev_plane && prev_kind < 6) {
            /* same plane as the last polygon: next to it, or level with it */
            if ((uint32_t)((int8_t)(prev_key >> 8) - (int8_t)(key >> 8) + 1) <= 2 &&
                (uint32_t)((int16_t)(prev_key >> 16) - (int16_t)(key >> 16) + 1) <= 2)
                fresh = 0;
            else if ((vertices[2] - prev_vertices[2]) * n[2] + (vertices[1] - prev_vertices[1]) * n[1] +
                         (vertices[0] - prev_vertices[0]) * n[0] ==
                     0)
                fresh = 0;
        }
        if (fresh) {
            int32_t dx, dy, dz, along, end;

            prev_plane = (int32_t)plane;
            unpack_normal(plane, n);
            prev_vertices = vertices;
            prev_kind = (int32_t)kind;
            state = 0;
            v = vertices;
            if (kind != 2 && (kind <= 5 || kind > 7))
                v = vertices + entry[1] * 4;
            dx = x - (v[0] << 8);
            dy = y - (v[1] << 8);
            dz = z - (v[2] << 8);
            along = (dz * n[2] + dy * n[1] + dx * n[0]) >> 8;
            dist = along - radius;
            if (dist <= 0) {
                state = 2;
                t = 0;
                point[0] = -(along * n[0]) >> 8;
                point[1] = -(along * n[1]) >> 8;
                point[2] = -(along * n[2]) >> 8;
            } else {
                end = (((dz + vz) * n[2] + (dy + vy) * n[1] + (dx + vx) * n[0]) >> 8) - radius;
                if (end < 0) {
                    /* crosses the plane during the step */
                    t = (int32_t)((uint32_t)dist << 16) / (dist - end);
                    if (t == 0)
                        state = 2;
                    else if (t < best)
                        state = 1;
                    if (state) {
                        int32_t s = t >> 8, hx = s * vx, hy = s * vy, hz = s * vz;

                        if (radius) {
                            hx -= radius * n[0];
                            hy -= radius * n[1];
                            hz -= radius * n[2];
                        }
                        point[0] = hx >> 8;
                        point[1] = hy >> 8;
                        point[2] = hz >> 8;
                    }
                } else if (end < 0x100 && dist < 0x100 && kind_of(body) != 4) {
                    /* resting on it */
                    state = 0x80000;
                    point[0] = -(along * n[0]) >> 8;
                    point[1] = -(along * n[1]) >> 8;
                    point[2] = -(along * n[2]) >> 8;
                }
            }
        }
        prev_key = key;
        if (!state)
            continue;
        if (state & 0x80000) {
            if (best < (int32_t)limit)
                continue;
        } else if (state == 2 && best == 0 && dist > out[6]) {
            continue;
        }
        if (fresh) {
            point[0] += x;
            point[1] += y;
            point[2] += z;
        }
        if (radius) {
            uint8_t *hit = g_track_hits + ((uint32_t)(entry - list) >> 4) * 0x28;

            I32(hit, 0x00) = point[0];
            I32(hit, 0x08) = point[2];
            I32(hit, 0x0c) = n[0];
            I32(hit, 0x10) = n[1];
            I32(hit, 0x04) = point[1];
            I32(hit, 0x14) = n[2];
            I32(hit, 0x18) = dist;
            I32(hit, 0x24) = t;
        }

        /* the polygon's corners in order: a, b, c, d (d = a: a triangle) */
        width = entry[3];
        switch (kind) {
        case 2:
            a = vertices + (width + 1) * 4;
            b = vertices;
            c = vertices + 4;
            d = a;
            break;
        case 3:
            b = vertices + (width + 2) * 4;
            a = vertices + 4;
            c = vertices + (width + 1) * 4;
            d = a;
            break;
        case 4:
            a = vertices + (width + 2) * 4;
            b = vertices + (width + 1) * 4;
            c = vertices;
            d = a;
            break;
        case 5:
            a = vertices;
            b = vertices + 4;
            c = vertices + (width + 2) * 4;
            d = vertices;
            break;
        case 6:
            /* walls: the edge and the same edge 256 lower */
            memcpy(wall_a, vertices, 6);
            wall_a[1] -= 0x100;
            b = vertices - (width + 1) * 4;
            memcpy(wall_b, b, 6);
            wall_b[1] -= 0x100;
            d = wall_a;
            c = vertices;
            a = wall_b;
            break;
        case 7:
            memcpy(wall_a, vertices, 6);
            wall_a[1] -= 0x100;
            d = vertices + (width + 1) * 4;
            memcpy(wall_b, d, 6);
            wall_b[1] -= 0x100;
            a = vertices;
            b = wall_a;
            c = wall_b;
            break;
        default:
            a = vertices;
            b = vertices + 4;
            c = vertices + (width + 2) * 4;
            d = vertices + (width + 1) * 4;
            break;
        }
        /* which edges is the point outside of? edge i runs from corner
         * i to the next; up to two, as i | j << 4 */
        px = point[0] >> 8;
        py = point[1] >> 8;
        pz = point[2] >> 8;
        for (i = 0; i < 4; i++) {
            int32_t s;

            if (i == 3 && a == d)
                break;
            if (i == 0) {
                m[0] = b[0] - px;
                m[1] = b[1] - py;
                m[2] = b[2] - pz;
                start[0] = u[0] = a[0] - px;
                start[1] = u[1] = a[1] - py;
                start[2] = u[2] = a[2] - pz;
            } else {
                first = i == 1 ? c : d;
                u[0] = m[0];
                u[1] = m[1];
                u[2] = m[2];
                if (i == 3) {
                    m[0] = start[0];
                    m[1] = start[1];
                    m[2] = start[2];
                } else {
                    m[0] = first[0] - px;
                    m[1] = first[1] - py;
                    m[2] = first[2] - pz;
                }
            }
            g_gte_vector2(0) = (int16_t)u[0];
            g_gte_diagonal(0) = (int16_t)m[0];
            g_gte_diagonal(1) = (int16_t)m[1];
            g_gte_diagonal(2) = (int16_t)m[2];
            g_gte_vector2(1) = (int16_t)u[1];
            g_gte_vector2(2) = (int16_t)u[2];
            gte_outer_product();
            s = g_gte_result[0] * n[0] + g_gte_result[1] * n[1] + g_gte_result[2] * n[2];
            if (s < 0) {
                if (crossed) {
                    edge |= i << 4;
                    break;
                }
                crossed = 1;
                edge = i;
            }
        }

        if (!((kind >= 6 && edge == (kind == 6 ? 3 : 1)) || edge == -1)) {
            edge_hits++;
            rotation = kind == 2 ? 3 : kind == 3 ? 1 : kind == 4 ? 2 : 0;
            if (!(edge & ~0xf)) {
                if (edge == 2 && a == d)
                    code = (rotation & 1) + 4;
                else
                    code = (edge + rotation) & 3;
            } else if (d != a) {
                code = edge;
            } else if ((edge & ~0xf) == 0x20) {
                code = 0x7f;
            } else {
                code = ((((edge >> 4) + rotation) << 4) & 0x30) | ((rotation + edge) & 3);
            }
            entry[2] = (uint8_t)code;
            if (radius)
                continue;
            if (!remaining)
                break;
            {
                /* follow the point into the polygon across that edge */
                int32_t col = (int8_t)(key >> 8), row = (int16_t)(key >> 16), left;
                uint32_t want;

                switch (code) {
                case 4:
                case 5:
                case 0x7f:
                    break;
                case 1:
                case 0x20:
                    col++;
                    break;
                case 2:
                case 0x31:
                    row++;
                    break;
                case 3:
                    col--;
                    break;
                case 0x10:
                    row--;
                    col++;
                    break;
                case 0x21:
                    col++;
                    row++;
                    break;
                case 0x30:
                    row--;
                    col--;
                    break;
                case 0x32:
                    col--;
                    row++;
                    break;
                default:
                    row--;
                    break;
                }
                if (col < 0)
                    col = 0;
                if (col > (int32_t)width)
                    col = (int32_t)width;
                if (kind >= 6)
                    col = (int8_t)(key >> 8);
                want = (key & 0xff) | (uint32_t)(uint8_t)col << 8 | (uint32_t)(uint16_t)row << 16;
                for (left = total, p = list; left > 0; left--, p += 16) {
                    if (U32(p, 4) != want)
                        continue;
                    if (!(p[0] & 0x10)) {
                        next = p; /* not tested yet: next */
                        COVER(COVER_TRACK_FOLLOWED);
                        break;
                    }
                    if (edges_meet((int8_t)p[2], code))
                        break; /* tested from the other side: on the edge */
                }
                if (left <= 0 || !(p[0] & 0x10) || !edges_meet((int8_t)p[2], code))
                    continue;
            }
        }
        /* a hit */
        out[0] = point[0];
        out[1] = point[1];
        out[2] = point[2];
        out[3] = (int32_t)((uint32_t)n[0] << 8);
        out[4] = (int32_t)((uint32_t)n[1] << 8);
        out[5] = (int32_t)((uint32_t)n[2] << 8);
        out[6] = dist;
        out[8] = (int32_t)key;
        result = state;
        best = t;
        if (kind >= 6 && kind_of(body) != 4)
            wall_touched(out, vertices);
        if (!radius)
            break;
    } while (remaining);

    if (radius && edge_hits) {
        int32_t left;

        for (left = total, p = list; left > 0 && edge_hits; left--, p += 16) {
            int32_t e = (int8_t)p[2], col, row, side;
            uint32_t pkey = U32(p, 4), want;
            const uint8_t *q;
            int32_t k;

            if (e < 0)
                continue;
            edge_hits--;
            if (e > 5)
                continue;
            col = (int8_t)(pkey >> 8);
            row = (int16_t)(pkey >> 16);
            switch (e) {
            case 1:
                side = 3;
                if ((p[0] & 0xf) < 6)
                    col++;
                break;
            case 2:
                side = 0;
                row++;
                break;
            case 3:
                side = 1;
                if ((p[0] & 0xf) < 6)
                    col--;
                break;
            case 4:
                side = 5;
                break;
            case 5:
                side = 4;
                break;
            default:
                side = 2;
                row--;
                break;
            }
            if ((p[0] & 0xf) < 6) {
                if (row < 0 || (uint32_t)row >= U32(g_tile(pkey & 0xff), 0xc)) {
                    track_edge_hit(slot, out, &best, &result, p, 0, list);
                    continue;
                }
            } else if (e == 0 || e == 2) {
                continue;
            }
            want = (pkey & 0xff) | (uint32_t)(uint8_t)col << 8 | (uint32_t)(uint16_t)row << 16;
            for (k = total, q = list; k > 0; k--, q += 16)
                if (U32(q, 4) == want && left != k && sides_meet((int8_t)q[2], side)) {
                    track_edge_hit(slot, out, &best, &result, p, q, list);
                    break;
                }
        }
    }
    out[7] = result;
    return (uint32_t)best;
}
