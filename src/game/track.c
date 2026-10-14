/* Track lookups and the per-step track contact search (0x419550).
 *
 * The track is a 256 x 256 grid of cells in x/z (the top byte of a 16.16
 * coordinate, biased by 0x80). Row z (0x6bfad4 + z * 16) holds up to six
 * u16 spans of occupied x (lo | hi << 8, 0xff ends) and at +0xc a pointer
 * to u16 cell offsets for them, into 0x6bfaf8. A cell is a stack of
 * 12-byte layer records (bit 31 of +4: another layer above, whose
 * +0 >> 13 & 0x3f is its lowest height). Layer +4 >> 16 & 0x3fff indexes
 * 6-byte polygon lists at 0x6bfb40; layer +0 >> 19 & 7 objects at
 * 0x6bfb3c + 4 + (+0 >> 22) * 4. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "gamefn.h"
#include "physics.h"
#include "../coverage.h"


#define g_track_rows (GP(0x6bfad4))
#define g_track_cells (GP(0x6bfaf8))
#define g_track_tiles GP(0x6bfac0) /* pointers to the tiles */
#define g_track_planes (((uint32_t *)GP(0x6bfadc)))
#define g_track_objects GP(0x6bfb3c) /* pointers to the objects */
#define g_track_polygon_lists (GP(0x6bfb40))
#define g_contact_slots (GP(0x6ce740))
#define g_contact_list (GP(0x6ce74c))
#define g_bodies GP(0x6b9b80) /* pointers to the bodies */
#define g_sin_table ((const int16_t *)0x4b24f8)
#define g_cos_table ((const int16_t *)0x4b2578)
#define g_body_count (*(uint32_t *)0x6b9b84 & 0xffff)

/* 0x461030: off the track: the cell offset nearest to x in row z,
 * searching rows z, z+1, z-1, z+2, ... while they can still be nearer. */
uint8_t *GAMEAPI track_nearest_cell(uint8_t *rows, int16_t z, uint32_t x)
{
    uint8_t *best_cell = 0;
    int32_t best = 0xff;
    uint32_t k = 1, x16 = x & 0xffff;

    for (;; k++) {
        int16_t row = (int16_t)((int32_t)k >> 1);
        int ok;

        if (!(k & 1)) {
            row = (int16_t)(z - row);
            ok = row >= 0;
        } else {
            row = (int16_t)(row + z);
            ok = row < 0x100;
        }
        if (ok) {
            uint8_t *r = rows + row * 0x10, *cells = GP(r + 0xc);
            if (cells) {
                int n;
                for (n = 0; n < 6; n++, r += 2) {
                    uint32_t span = *(uint16_t *)r, lo, hi;
                    int32_t d;

                    if (span == 0xff)
                        break;
                    lo = span & 0xff;
                    hi = span >> 8;
                    if (lo <= x16 && x16 <= hi)
                        return cells + (x16 - lo) * 2;
                    d = abs32((int32_t)(lo - x16));
                    if (d < best) {
                        best = d;
                        best_cell = cells;
                        if (d == 1)
                            return cells;
                    }
                    d = abs32((int32_t)(hi - x16));
                    if (d < best) {
                        best_cell = cells + (hi - lo) * 2;
                        best = d;
                        if (d == 1)
                            return best_cell;
                    }
                    cells += 2 + (hi - lo) * 2;
                }
            }
        }
        if (best * 2 <= (int32_t)(k + 1))
            return best_cell;
    }
}

/* 0x419420: the cell layer under (x, y, z) (16.16); *off_track is set when
 * the position is outside every span and the nearest cell was used. */
uint8_t *GAMEAPI track_cell(int32_t x, int32_t y, int32_t z, int32_t *off_track)
{
    uint32_t row_z = (uint32_t)((z >> 24) + 0x80), column = (uint32_t)((x >> 24) + 0x80);
    uint32_t height = (uint32_t)((y >> 24) + 0x20) & 0xffff, column16 = column & 0xffff;
    uint8_t *row = g_track_rows + (row_z & 0xffff) * 0x10, *cells = GP(row + 0xc), *layer;
    uint16_t *cell = 0;
    int32_t index = 0, n;

    if (cells) {
        for (n = 0; n < 6; n++, row += 2) {
            uint32_t span = *(uint16_t *)row, lo = span & 0xff, hi = span >> 8;
            if (span == 0xff || column16 < lo)
                break;
            if (column16 <= hi) {
                index += (int32_t)(column16 - lo);
                cell = (uint16_t *)(cells + index * 2);
                break;
            }
            index += (int32_t)(hi - lo) + 1;
        }
        if (n == 6)
            cell = (uint16_t *)(cells + index * 2);
    }
    if (cell) {
        *off_track = 0;
    } else {
        COVER(COVER_OFF_TRACK);
        cell = (uint16_t *)track_nearest_cell(g_track_rows, (int16_t)row_z, column);
        *off_track = 1;
    }
    layer = g_track_cells + *cell;
    while (U32(layer, 4) & 0x80000000u) {
        if (((U32(layer, 0xc) >> 13) & 0x3f) < height)
            break;
        layer += 0xc;
    }
    return layer;
}

/* A track object (a body-like record from 0x6bfb3c): position at +0x34
 * (16.16), model at +0x10, yaw (1/256 turn) at +0x199, per polygon flags
 * at +0x194 and plane records at +0x2c (8 bytes: vertex index, then the
 * packed plane at +4). Its model has the polygon count at +0x27, offsets
 * of the 20-byte polygons (+0x34), 9-byte face records (+0x38) and 8-byte
 * edges (+0x3a), and at +0x40 the vertices (8 bytes, numbered from 1).
 * A polygon is an int16 point and at +0x10 its face record index (bits
 * 0..7) and kind (bits 16..20: 1 point, 2 edge, otherwise the face's
 * vertex count). */

/* 0x41a390: does the slot's sphere touch one of the object's polygons?
 * Sets out[7] to 2 if it may (points and edges always count), 0 if not. */
void GAMEAPI object_polygon_test(uint8_t *body, uint8_t *object, const uint8_t *polygon, uint8_t *slot,
                                 int32_t *out, int32_t dt)
{
    const uint8_t *model = GP(object + 0x10), *planes = GP(object + 0x2c), *face;
    const int16_t *vertices = (const int16_t *)(GP(model + 0x40) - 8);
    uint32_t info = U32(polygon, 0x10), i;
    int16_t x = (int16_t)(U32(object, 0x34) >> 16), y = (int16_t)(U32(object, 0x38) >> 16),
            z = (int16_t)(U32(object, 0x3c) >> 16);
    int16_t a[3], b[3];
    int32_t point[3], n[3];

    (void)body;
    out[7] = 0;
    face = model + *(const uint16_t *)(model + 0x38) + (info & 0xff) * 9;
    if ((info & 0x1f0000) == 0x10000) {
        COVER(COVER_OBJECT_POINT);
        point[0] = (((const int16_t *)polygon)[0] + (I32(object, 0x34) >> 16)) << 8;
        point[1] = (((const int16_t *)polygon)[1] + (I32(object, 0x38) >> 16)) << 8;
        point[2] = (((const int16_t *)polygon)[2] + (I32(object, 0x3c) >> 16)) << 8;
        sphere_sweep_point(slot, point, out, dt);
    } else if ((info & 0x1f0000) == 0x20000) {
        const uint8_t *edge = model + *(const uint16_t *)(model + 0x3a) + face[0] * 8;
        const int16_t *va = vertices + edge[4] * 4, *vb = vertices + edge[5] * 4;
        uint32_t plane = U32(edge, 0);

        COVER(COVER_OBJECT_EDGE);
        a[0] = (int16_t)(va[0] + x);
        a[1] = (int16_t)(va[1] + y);
        a[2] = (int16_t)(va[2] + z);
        b[0] = (int16_t)(vb[0] + x);
        b[1] = (int16_t)(vb[1] + y);
        b[2] = (int16_t)(vb[2] + z);
        n[0] = (int32_t)(plane << 2) >> 22;
        n[1] = (int32_t)(plane << 12) >> 22;
        n[2] = (int32_t)(plane << 22) >> 22;
        sphere_sweep_edge(slot, a, b, n, out, 0, dt);
    } else if (((info >> 16) & 0x1f) != 1) {
        /* a face: inside every one of its edge planes */
        const uint8_t *index = face + 8;

        i = 0;
        do {
            const uint8_t *plane = planes + *index * 8;
            const int16_t *v = vertices + plane[0] * 4;

            a[0] = (int16_t)(v[0] + x);
            a[1] = (int16_t)(v[1] + y);
            a[2] = (int16_t)(v[2] + z);
            if (!plane_test(slot, a, U32(plane, 4), (int32_t)((uint32_t)dt | 0x80000000u))) {
                COVER(COVER_OBJECT_MISS);
                out[7] = 0;
                return;
            }
            index += 9;
        } while (++i < ((U32(polygon, 0x10) >> 16) & 0x1f) - 1);
        COVER(COVER_OBJECT_FACE);
    }
    out[7] = 2;
}

/* 0x41a150: contacts with one track object: the slot is turned into the
 * object's frame (about its position, by its yaw) for the tests, then
 * restored. Each polygon touched adds a contact (kind 1) to the body's
 * list. */
int32_t GAMEAPI object_contacts(uint8_t *body, uint8_t *object, uint8_t *slot, int32_t dt)
{
    uint32_t count = *(uint16_t *)(body + 0x15e), angle = 0x100 - object[0x199];
    uint8_t *entry = GP(body + 0x158) + count * 16, *model = GP(object + 0x10);
    const uint8_t *polygon = model + *(const uint16_t *)(model + 0x34);
    int32_t x = I32(slot, 0x08), z = I32(slot, 0x10), vx = I32(slot, 0x14), vz = I32(slot, 0x1c);
    int32_t ox = I32(object, 0x34) >> 8, oz = I32(object, 0x3c) >> 8, dx = x - ox, dz = z - oz;
    int32_t c = g_cos_table[angle] >> 6, s = g_sin_table[angle] >> 6, out[8];
    int i;

    I32(slot, 0x08) = fix_mul8(dz, s) + fix_mul8(dx, c) + ox;
    I32(slot, 0x10) = fix_mul8(-dx, s) + fix_mul8(dz, c) + oz;
    I32(slot, 0x1c) = fix_mul8(-vx, s) + fix_mul8(vz, c);
    I32(slot, 0x14) = fix_mul8(vz, s) + fix_mul8(vx, c);
    for (i = 0; i < model[0x27]; i++, polygon += 0x14) {
        if (((((uint32_t *)GP(object + 0x194)))[i] & 0xf000000) == 0x5000000 || !(U32(polygon, 0x10) & 0x1f0000))
            continue;
        object_polygon_test(body, object, polygon, slot, out, dt);
        if (out[7]) {
            count++;
            U32(entry, 0xc) = (uint32_t)(uintptr_t)polygon;
            U32(entry, 8) = (uint32_t)(uintptr_t)object;
            entry[1] = object[0x199];
            U32(entry, 4) = 0xfffffffc;
            entry[0] = (uint8_t)((entry[0] & 0xf1) | 1);
            entry += 16;
        }
    }
    I32(slot, 0x10) = z;
    I32(slot, 0x08) = x;
    I32(slot, 0x14) = vx;
    I32(slot, 0x1c) = vz;
    *(uint16_t *)(body + 0x15e) = (uint16_t)count;
    return dt;
}

/* 0x41a0f0: contacts with the track objects of the body's cell (except
 * kind 5). */
void GAMEAPI body_object_contacts(uint8_t *body, uint8_t *slot, int32_t dt)
{
    uint32_t record = *(uint32_t *)(uintptr_t)(U32(body, 0x0c) & ~1u), n = (record >> 19) & 7;
    uint8_t *object = g_track_objects + 4 + (record >> 22) * 4;

    for (; n; n--, object += 4)
        if (GP(object)[0x19a] != 5)
            object_contacts(body, GP(object), slot, dt);
}

/* A track contact as recorded in a body's list (16 bytes from +0x154):
 *   +0 low nibble: kind (0 to 5: which triangle of a grid cell, 6 and 7:
 *      left and right edge walls); +1 corner; +3 tile width;
 *   +4 cell key (tile index, column, row as u8, u8, u16);
 *   +8 plane (packed normal and distance); +12 vertex. */
static void add_contact(uint8_t *entry, uint32_t kind, uint32_t corner, uint8_t width, uint32_t key, uint32_t plane,
                        const int16_t *vertex)
{
    entry[3] = width;
    U32(entry, 4) = key;
    entry[0] = (uint8_t)((entry[0] & 0xf0) | (kind & 0xf));
    U32(entry, 8) = plane;
    U32(entry, 0xc) = (uint32_t)(uintptr_t)vertex;
    entry[1] = (uint8_t)corner;
}

#define VX(v) ((int32_t)(v)[0])
#define VY(v) ((int32_t)(v)[1])
#define VZ(v) ((int32_t)(v)[2])

/* 0x41a020: is the body (slot position, reach at +0x40) touching a
 * polygon's plane? The plane goes through vertex (int16 x, y, z, in
 * 1/256 of slot units) with a normal packed as three signed 10-bit
 * fields (x in bits 20..29, y in 10..19, z in 0..9). In front of the
 * plane, it counts only when moving towards it fast enough to reach it
 * this step; behind it, only within twice the reach unless dt is
 * negative. */
int32_t GAMEAPI plane_test(uint8_t *slot, const int16_t *vertex, uint32_t plane, int32_t dt)
{
    int32_t nz = (int32_t)(plane << 22) >> 22;
    int32_t ny = (int32_t)(plane << 12) >> 22;
    int32_t nx = (int32_t)(plane << 2) >> 22;
    int32_t reach = I32(slot, 0x40), distance, approach;

    distance = (int32_t)((uint32_t)(I32(slot, 0x0c) - (vertex[1] << 8)) * ny +
                         (uint32_t)(I32(slot, 0x10) - (vertex[2] << 8)) * nz +
                         (uint32_t)(I32(slot, 0x08) - (vertex[0] << 8)) * nx) >> 8;
    distance -= reach;
    if (distance < 0) {
        if (dt < 0)
            return 1;
        return distance >= -(reach + reach);
    }
    approach = (int32_t)((uint32_t)I32(slot, 0x1c) * nz + (uint32_t)I32(slot, 0x18) * ny +
                         (uint32_t)I32(slot, 0x14) * nx);
    if (approach >= 0)
        return 0;
    return (approach >> 8) + distance <= 0;
}

/* 0x419820: contacts between a body (its slot's position and reach) and
 * the triangles of one patch of a track tile's height grid.
 * `polygons`: u16 row | column << 10, u8 tile, u8 pad, u8 radius,
 * u8 rows - 1 (low nibble) | columns << 4. Also notes the nearest grid
 * vertex as the body's track position (+0x11c, distance in +0x120). */
void GAMEAPI polygon_contacts(uint8_t *slot, const uint8_t *polygons, uint8_t *body, int32_t dt,
                              const int16_t *centre, int32_t reach)
{
    uint32_t start_count = *(uint16_t *)(body + 0x15c), count = start_count;
    uint32_t w0 = *(const uint16_t *)polygons, w2 = *(const uint16_t *)(polygons + 4);
    uint32_t column = w0 >> 10, row = w0 & 0x3ff, rows = (w2 >> 8) & 0xf, columns = w2 >> 12;
    uint32_t tile_index = polygons[2] & 0x7f, radius = w2 & 0xff;
    uint8_t *tile = GP(g_track_tiles + tile_index * 4);
    int32_t width = I32(tile, 8), stride = width + 1, skip = width - (int32_t)columns;
    int32_t first = stride * (int32_t)row + (int32_t)column;
    const int16_t *vertex = (const int16_t *)(uintptr_t)(U32(tile, 0x20) + first * 8), *v;
    const uint8_t *plane_row = (const uint8_t *)(uintptr_t)(U32(tile, 0x50) + (first - (int32_t)row) * 8);
    int32_t cx = centre[0], cy = centre[1], cz = centre[2];
    int32_t threshold = (int32_t)(radius * radius + (uint32_t)reach) >> 6;
    uint32_t nearest_distance = 0x7fffffff, nearest_key = 0, key, start_key;
    uint16_t distances[256], *d;
    int32_t last_plane = 0, test = 0;
    uint32_t r, c;
    uint8_t *list = GP(body + 0x154), *entry;

    start_key = tile_index | (column & 0xff) << 8 | row << 16;
    d = distances;
    v = vertex;
    for (r = 0; r <= rows; r++) {
        for (c = 0; c <= columns; c++, v += 4) {
            int32_t dx = VX(v) - cx, dy = VY(v) - cy, dz = VZ(v) - cz;
            uint32_t dist = (uint32_t)((int32_t)((uint32_t)dz * (uint32_t)dz + (uint32_t)dy * (uint32_t)dy +
                                                 (uint32_t)dx * (uint32_t)dx) >> 6);
            *d++ = (uint16_t)(dist & 0xffff0000u ? 0xffff : dist);
        }
        v += skip * 4;
    }

    key = start_key;
    d = distances;
    for (r = 0; r < rows; r++) {
        last_plane = -1;
        key = (key & 0xffff00ffu) | (column & 0xff) << 8;
        for (c = 0; c < columns; c++) {
            uint32_t best = d[0], corner = 0;
            int32_t tests, kind = 0, corner_vertex = 0, plane_index;

            if (d[1] < best) {
                best = d[1];
                corner = 1;
            }
            if (d[columns + 1] < best) {
                best = d[columns + 1];
                corner = 2;
            }
            if (d[columns + 2] < best) {
                best = d[columns + 2];
                corner = 3;
            }
            if (best < nearest_distance) {
                nearest_key = key;
                nearest_key = (nearest_key & 0xffff00ffu) | (((corner & 1) + ((key >> 8) & 0xff)) & 0xff) << 8;
                nearest_key = (nearest_key & 0xffffu) | ((nearest_key >> 16) + ((int32_t)corner >> 1)) << 16;
                nearest_distance = best;
            }
            if ((int32_t)best <= threshold) {
                uint32_t offset = count << 4;

                tests = (((const uint8_t *)vertex)[width * 8 + 0x16] & 2) ? 2 : 1;
                do {
                    uint8_t flags = ((const uint8_t *)vertex)[(width + 2) * 8 + 6];
                    const int16_t *at;
                    uint32_t plane;

                    plane_index = *(const uint16_t *)(plane_row + 2);
                    if (!(flags & 2)) {
                        corner_vertex = (int32_t)(corner & 1) + ((corner & 2) ? stride : 0);
                        at = vertex;
                        kind = 0;
                    } else if (!(flags & 4)) {
                        if (tests == 2) {
                            corner_vertex = 1;
                            at = vertex + 4;
                            kind = 5;
                        } else {
                            kind = 4;
                            corner_vertex = stride;
                            plane_index++;
                            at = vertex + stride * 4;
                        }
                    } else if (tests == 2) {
                        corner_vertex = 0;
                        at = vertex;
                        kind = 2;
                    } else {
                        corner_vertex = width + 2;
                        at = vertex + (width + 2) * 4;
                        kind = 3;
                        plane_index++;
                    }
                    plane = g_track_planes[plane_index];
                    if ((int32_t)plane != last_plane)
                        test = plane_test(slot, at, plane, dt);
                    last_plane = (int32_t)plane;
                    if (test == 1) {
                        int duplicate = 0;
                        uint32_t k;

                        entry = list;
                        for (k = start_count; k; k--, entry += 0x10) {
                            if (key == U32(entry, 4) && (uint32_t)kind == (entry[0] & 0xfu)) {
                                duplicate = 1;
                                COVER(COVER_DUPLICATE_CONTACT);
                                break;
                            }
                        }
                        if (!duplicate) {
                            int accept = 1;

                            entry = list + offset;
                            if ((int32_t)best > reach >> 6) {
                                /* near the limit: the centre must be on the
                                 * inside of one of the corner's edges */
                                uint32_t a_index = (corner & 1) + ((corner & 2) ? (uint32_t)stride : 0), b_index;
                                const int16_t *a = vertex + a_index * 4, *b;
                                int32_t ex = cx - VX(a), ey = cy - VY(a), ez = cz - VZ(a);

                                b_index = ((((int32_t)corner >> 1) - 1) & 1) + ((corner & 1) ? (uint32_t)stride : 0);
                                b = vertex + b_index * 4;
                                if ((int32_t)((uint32_t)(VY(b) - VY(a)) * (uint32_t)ey +
                                              (uint32_t)(VZ(b) - VZ(a)) * (uint32_t)ez +
                                              (uint32_t)(VX(b) - VX(a)) * (uint32_t)ex) < 0) {
                                    b_index = (uint32_t)((int32_t)corner >> 1) + ((corner & 1) ? 0 : (uint32_t)stride);
                                    b = vertex + b_index * 4;
                                    if ((int32_t)((uint32_t)(VY(b) - VY(a)) * (uint32_t)ey +
                                                  (uint32_t)(VZ(b) - VZ(a)) * (uint32_t)ez +
                                                  (uint32_t)(VX(b) - VX(a)) * (uint32_t)ex) < 0) {
                                        accept = 0;
                                        COVER(COVER_EDGE_REJECTED);
                                    }
                                }
                            }
                            if (accept) {
                                COVER(COVER_TRIANGLE_CONTACT);
                                offset += 0x10;
                                add_contact(entry, (uint32_t)kind, (uint32_t)corner_vertex, (uint8_t)width, key,
                                            plane, vertex);
                                count++;
                            }
                        }
                    }
                } while (--tests > 0);
            }
            d++;
            key = (key & 0xffff00ffu) | (((key >> 8) + 1) & 0xff) << 8;
            plane_row += 8;
            vertex += 4;
        }
        key = (key & 0xffffu) | ((key >> 16) + 1) << 16;
        d++;
        plane_row += skip * 8;
        vertex += skip * 4 + 4;
    }

    /* the tile's edge walls, where this patch reaches them */
    entry = list + count * 0x10;
    if (!(tile[4] & 1)) {
        uint32_t left = tile[0x26], right = (uint32_t)(width - tile[0x27]);
        int side;

        for (side = 0; side < 2; side++) {
            const uint16_t *walls;
            const int16_t *at;
            int32_t index;
            uint32_t kind;

            if (side == 0 ? column != left : columns + column != right)
                continue;
            walls = (const uint16_t *)(uintptr_t)(U32(tile, 0x38) + row * 4);
            index = (int32_t)(row + 1) * stride;
            key = start_key;
            if (side == 0) {
                key = (key & 0xffff00ffu) | (left & 0xff) << 8;
                index += (int32_t)left;
                kind = 6;
            } else {
                key = (key & 0xffff00ffu) | (right & 0xff) << 8;
                kind = 7;
                index = index + ((int32_t)right - width) - 1;
            }
            at = (const int16_t *)(uintptr_t)(U32(tile, 0x20) + index * 8);
            for (r = 0; r < rows; r++) {
                uint16_t wall = side == 0 ? walls[0] : walls[1];

                if (wall != 0xffff) {
                    int32_t dy = cy - VY(at);
                    if (dy <= 0 || (int32_t)((uint32_t)dy * (uint32_t)dy) >> 6 <= threshold) {
                        uint32_t plane = g_track_planes[wall];
                        if (plane_test(slot, at, plane, dt)) {
                            COVER(COVER_WALL_CONTACT);
                            count++;
                            add_contact(entry, kind, 0, (uint8_t)width, key, plane, at);
                            entry += 0x10;
                        }
                    }
                }
                at += stride * 4;
                walls += 2;
                key = (key & 0xffffu) | ((key >> 16) + 1) << 16;
            }
        }
    }
    if ((int32_t)count > 0x40)
        count = 0x40;
    *(uint16_t *)(body + 0x15c) = (uint16_t)count;
    if ((int32_t)(nearest_distance << 6) < I32(body, 0x120)) {
        I32(body, 0x120) = (int32_t)(nearest_distance << 6);
        U32(body, 0x11c) = nearest_key;
    }
}

/* 0x419550: for every body, its contact slot gets its position and
 * velocity (1/256 units), then candidate track polygons around the middle
 * of its path this step are tested (0x419820); then object contacts.
 * +0x154 and +0x158 note where each body's contacts start in the list at
 * 0x6ce74c (16 bytes each, +0x15c counts them). */
void GAMEAPI track_contacts_find(int32_t dt)
{
    uint8_t *slots = g_contact_slots, *cursor = g_contact_list;
    uint8_t *bodies = g_bodies;
    uint32_t n = g_body_count, i;

    for (i = 0; i < n; i++) {
        uint8_t *b = GP(bodies + i * 4);
        uint8_t *slot = slots + b[(int8_t)b[RIGIDITY] + 0x140] * 0x48, *cell, *list;
        int32_t hx, hy, hz, cx, cy, cz, off_track, reach, count;
        int16_t centre[3];

        U32(b, 0x154) = (uint32_t)(uintptr_t)cursor;
        I32(slot, 0x08) = I32(b, POSITION) >> 8;
        I32(slot, 0x0c) = I32(b, POSITION + 4) >> 8;
        I32(slot, 0x10) = I32(b, POSITION + 8) >> 8;
        I32(slot, 0x18) = I32(b, VELOCITY + 4) >> 8;
        I32(slot, 0x14) = I32(b, VELOCITY) >> 8;
        I32(slot, 0x1c) = I32(b, VELOCITY + 8) >> 8;
        *(uint16_t *)(b + 0x15c) = 0;
        *(uint16_t *)(b + 0x15e) = 0;
        if ((U32(b, FLAGS) & 0x200) || (U32(b, 0x110) & 0x80000000u))
            continue;
        hx = I32(slot, 0x14) >> 1;
        hz = I32(slot, 0x1c) >> 1;
        hy = I32(slot, 0x18) >> 1;
        cx = I32(slot, 0x08) + hx;
        cy = I32(slot, 0x0c) + hy;
        cz = I32(slot, 0x10) + hz;
        cell = track_cell((int32_t)((uint32_t)cx << 8), (int32_t)((uint32_t)cy << 8), (int32_t)((uint32_t)cz << 8),
                          &off_track);
        if (off_track) {
            if (U32(b, 0x0c) & 1) {
                U32(b, 0x128) = 0x10;
                continue;
            }
            cell = (uint8_t *)(uintptr_t)(U32(b, 0x0c) & ~1u);
        }
        centre[0] = (int16_t)(cx >> 8);
        centre[1] = (int16_t)(cy >> 8);
        centre[2] = (int16_t)(cz >> 8);
        if ((U32(b, FLAGS) & 0x1ff) == 0x11) {
            I32(g_contact_slots + b[0x140] * 0x48, 0x40) = 0x1900;
            I32(g_contact_slots + b[0x141] * 0x48, 0x40) = 0x5000;
        }
        reach = (int32_t)((uint32_t)hz * (uint32_t)hz + (uint32_t)hy * (uint32_t)hy + (uint32_t)hx * (uint32_t)hx) >> 16;
        if (reach)
            reach = (int32_t)fix_sqrt_table(reach);
        reach += I32(slot, 0x40) >> 8;
        reach = (int32_t)((uint32_t)reach * (uint32_t)reach);
        list = g_track_polygon_lists + ((U32(cell, 4) >> 16) & 0x3fff) * 6;
        count = (*(uint16_t *)(list + 2) >> 9) + 1;
        do {
            polygon_contacts(slot, list, b, dt, centre, reach);
            list += 6;
        } while (--count > 0);
        cursor += *(uint16_t *)(b + 0x15c) * 0x10;
    }
    n = g_body_count;
    bodies = g_bodies;
    for (i = 0; i < n; i++) {
        uint8_t *b = GP(bodies + i * 4);

        U32(b, 0x158) = (uint32_t)(uintptr_t)cursor;
        if ((U32(b, FLAGS) & 0x200) || (U32(b, FLAGS) & 0x1ff) == 4)
            continue;
        body_object_contacts(b, slots + b[(int8_t)b[RIGIDITY] + 0x140] * 0x48, dt);
        cursor += *(uint16_t *)(b + 0x15c) * 0x10;
    }
}
