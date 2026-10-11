/* Swept-sphere tests (0x413190..0x413c77): when, within a step, does a
 * body's contact sphere first touch a point or an edge?
 *
 * The sphere is a contact slot (0x48 bytes): position at +0x08, velocity
 * per step at +0x14 (both 1/256 of world units) and radius at +0x40. A
 * test fills an 8-int result: contact point (0..2), normal (3..5, a unit
 * vector at 1 << 12), depth (6) and state (7: 0 none, 1 hit at the
 * returned time, 2 already overlapping, 0x100000 off the edge's ends),
 * and returns the time of contact (16.16 of the step; dt when there is
 * none). Used for track object contacts (0x41a390) and between bodies
 * (0x414fe0, 0x416d40). */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

typedef int32_t(GAMEAPI *normalise_fn)(const int32_t *v, int32_t *out);
/* x87: out = v * 4096 / |v| (0x4290b0, still the original) */
#define vec_normalise_float ((normalise_fn)0x4290b0)

#define g_gte_diagonal(i) (*(int32_t *)(0x6ccbe0 + (i) * 8))
#define g_gte_vector2(i) (*(int16_t *)(0x6ccc88 + (i) * 4))
#define g_gte_result ((const int32_t *)0x6cccc8)

/* 0x413590: the larger root term of a*t^2 + 2*b*t + c = 0, i.e.
 * sqrt(b^2 - a*c), with the inputs scaled down to keep it in range;
 * -1 when there is no real root. */
int32_t GAMEAPI quad_root(int32_t a, int32_t b, int32_t c)
{
    int32_t shift = 16 - count_leading_zeros_of((uint32_t)(a | c)), b2, ac, d;

    if (shift <= 0) {
        shift = 0;
        b2 = (int32_t)((uint32_t)(b >> 4) * (uint32_t)(b >> 4));
    } else {
        b2 = b >> (shift + 4);
        b2 = (int32_t)((uint32_t)b2 * (uint32_t)b2);
        a >>= shift;
        c >>= shift;
    }
    ac = fix_mul8(a, c);
    if (ac < 0)
        return -1;
    d = (int32_t)((uint32_t)b2 - (uint32_t)ac);
    if (d < 0)
        return -1;
    return (int32_t)((fix_sqrt_table(d) << 4) << shift);
}

/* 0x413be0: normalise v into out (x87), first scaling v down in place so
 * its largest component has at most 14 significant bits. Returns the
 * shift. */
int32_t GAMEAPI vec_normalise_shifted(int32_t *v, int32_t *out)
{
    int32_t shift = 18 - count_leading_zeros_of((uint32_t)(abs32(v[0]) | abs32(v[2]) | abs32(v[1])));

    if (shift > 0) {
        v[0] >>= shift;
        v[1] >>= shift;
        v[2] >>= shift;
        vec_normalise_float(v, out);
        return shift;
    }
    vec_normalise_float(v, out);
    return 0;
}

static void normal_from(int32_t *v, int32_t *out)
{
    vec_normalise_shifted(v, out);
    out[0] >>= 4;
    out[1] >>= 4;
    out[2] >>= 4;
}

/* Sphere against one axis of the box test: outside on that axis and not
 * moving back inside within the step. */
static int axis_misses(int32_t d, int32_t end, int32_t v, int32_t r)
{
    if (d > r)
        return end > r || v >= 0;
    if (d < -r)
        return end < -r || v <= 0;
    return 0;
}

/* 0x413190: the sphere against a point. */
int32_t GAMEAPI sphere_sweep_point(const uint8_t *slot, const int32_t *point, int32_t *out, int32_t dt)
{
    int32_t vx = I32(slot, 0x14), vy = I32(slot, 0x18), vz = I32(slot, 0x1c), r;
    int32_t d[3], ex = 0, ey = 0, ez = 0, a, b, c, r2, dist, root, t, t2, s;

    out[7] = 0;
    d[0] = I32(slot, 0x08) - point[0];
    d[1] = I32(slot, 0x0c) - point[1];
    d[2] = I32(slot, 0x10) - point[2];
    if (dt > 0) {
        s = ((dt - 1) >> 16) & 31;
        ex = (int32_t)((uint32_t)vx << s);
        ey = (int32_t)((uint32_t)vy << s);
        ez = (int32_t)((uint32_t)vz << s);
    }
    ex += d[0];
    ey += d[1];
    ez += d[2];
    r = I32(slot, 0x40);
    if (axis_misses(d[0], ex, vx, r) || axis_misses(d[1], ey, vy, r) || axis_misses(d[2], ez, vz, r))
        return dt;

    a = fix_mul8(vx, vx) + fix_mul8(vy, vy) + fix_mul8(vz, vz);
    if (a == 0) {
        out[7] = 0;
        return dt;
    }
    b = fix_mul8(d[0], vx) + fix_mul8(d[1], vy) + fix_mul8(d[2], vz);
    c = fix_mul8(d[0], d[0]) + fix_mul8(d[1], d[1]) + fix_mul8(d[2], d[2]);
    r2 = fix_mul8(I32(slot, 0x40), I32(slot, 0x40));
    dist = (int32_t)(fix_sqrt_table(c) << 4);
    out[6] = dist;
    out[6] = dist = (int32_t)((uint32_t)dist - (uint32_t)I32(slot, 0x40));
    if (dist >= 0) {
        root = quad_root(a, b, c - r2);
        if (root < 0) {
            out[7] = 0;
            return dt;
        }
        t = root - b;
        t2 = -(b + root);
        if (t >= t2) {
            int32_t swap = t;
            t = t2;
            t2 = swap;
        }
        t = (int32_t)((uint32_t)fix_div8(t, a) << 8);
        if ((int32_t)((uint32_t)t2 << 8) < 0) {
            out[7] = 0;
            return dt;
        }
        if (t > dt) {
            out[7] = 0;
            return dt;
        }
        if (t >= 0) {
            out[7] = 1;
            s = t >> 8;
            out[3] = I32(slot, 0x08) + fix_mul8(s, vx) - point[0];
            out[4] = I32(slot, 0x0c) + fix_mul8(s, vy) - point[1];
            out[5] = fix_mul8(s, vz) + I32(slot, 0x10) - point[2];
            normal_from(out + 3, out + 3);
            out[0] = point[0];
            out[1] = point[1];
            out[2] = point[2];
            return t;
        }
    }
    normal_from(d, out + 3);
    out[0] = point[0];
    out[1] = point[1];
    out[2] = point[2];
    out[7] = 2;
    return 0;
}

static int32_t edge_side(const int32_t *p, const int16_t *v, const int32_t *n)
{
    return (int32_t)((uint32_t)(p[1] - (v[1] << 8)) * (uint32_t)n[1] +
                     (uint32_t)(p[2] - (v[2] << 8)) * (uint32_t)n[2] +
                     (uint32_t)(p[0] - (v[0] << 8)) * (uint32_t)n[0]);
}

/* 0x413630: the sphere (radius grown by extra) against the edge from a to
 * b (int16 vertices, 1/256 units scaled down further by 256) along unit
 * direction n (1 << 16). */
int32_t GAMEAPI sphere_sweep_edge(const uint8_t *slot, const int16_t *a, const int16_t *b, const int32_t *n,
                                  int32_t *out, int32_t extra, int32_t dt)
{
    int32_t reach = I32(slot, 0x40) + extra, vx = I32(slot, 0x14), vy = I32(slot, 0x18), vz = I32(slot, 0x1c);
    int32_t r[3], p[3], u[3], w[3], m[3], q[3], along, dist, h, k, g, speed, t, result;

    out[7] = 0;
    r[0] = I32(slot, 0x08) - (a[0] << 8);
    r[1] = I32(slot, 0x0c) - (a[1] << 8);
    r[2] = I32(slot, 0x10) - (a[2] << 8);
    along = (int32_t)((uint32_t)n[2] * (uint32_t)r[2] + (uint32_t)n[1] * (uint32_t)r[1] +
                      (uint32_t)n[0] * (uint32_t)r[0]);
    p[0] = mul16(along, n[0]);
    p[1] = mul16(along, n[1]);
    p[2] = mul16(along, n[2]);
    /* u: from the nearest point of the edge's line to the sphere */
    u[0] = r[0] - p[0];
    u[1] = r[1] - p[1];
    u[2] = r[2] - p[2];
    if (abs32(u[0]) > 0xb504 || abs32(u[1]) > 0xb504 || abs32(u[2]) > 0xb504)
        return dt;
    if ((((int32_t)((uint32_t)vy * (uint32_t)u[1]) >> 8) + ((int32_t)((uint32_t)vz * (uint32_t)u[2]) >> 8) +
         ((int32_t)((uint32_t)vx * (uint32_t)u[0]) >> 8)) >= 0)
        return dt;
    p[0] += a[0] << 8;
    p[1] += a[1] << 8;
    p[2] += a[2] << 8;
    dist = (int32_t)(fix_sqrt_table(fix_mul8(u[0], u[0]) + fix_mul8(u[1], u[1]) + fix_mul8(u[2], u[2])) << 4);

    if (dist <= reach) {
        out[7] = 2;
        result = 0;
        q[0] = u[0];
        q[1] = u[1];
        q[2] = u[2];
        normal_from(q, q);
        out[3] = q[0];
        out[4] = q[1];
        out[5] = q[2];
        reach -= dist;
        if (reach < 0)
            reach = 0;
        out[6] = -reach;
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
    } else {
        /* w: perpendicular to both the velocity and the edge */
        g_gte_diagonal(0) = vx;
        g_gte_diagonal(1) = vy;
        g_gte_diagonal(2) = vz;
        g_gte_vector2(0) = (int16_t)n[0];
        g_gte_vector2(1) = (int16_t)n[1];
        g_gte_vector2(2) = (int16_t)n[2];
        gte_outer_product();
        w[2] = g_gte_result[2];
        w[0] = g_gte_result[0];
        w[1] = g_gte_result[1];
        if ((w[2] | w[1] | w[0]) == 0)
            return dt;
        vec_normalise_shifted(w, w);
        w[1] >>= 4;
        w[2] >>= 4;
        w[0] >>= 4;
        /* h: closest the sphere's path gets to the edge's line */
        h = (int32_t)((uint32_t)w[2] * (uint32_t)u[2] + (uint32_t)w[1] * (uint32_t)u[1] +
                      (uint32_t)w[0] * (uint32_t)u[0]) >> 8;
        if (h < 0) {
            h = -h;
            w[0] = -w[0];
            w[1] = -w[1];
            w[2] = -w[2];
        }
        if (h > reach)
            return dt;
        g_gte_diagonal(0) = n[0];
        g_gte_diagonal(1) = n[1];
        g_gte_diagonal(2) = n[2];
        g_gte_vector2(0) = (int16_t)w[0];
        g_gte_vector2(1) = (int16_t)w[1];
        g_gte_vector2(2) = (int16_t)w[2];
        gte_outer_product();
        m[0] = g_gte_result[0] >> 8;
        m[1] = g_gte_result[1] >> 8;
        m[2] = g_gte_result[2] >> 8;
        k = (int32_t)((uint32_t)vy * (uint32_t)m[1] + (uint32_t)vz * (uint32_t)m[2] +
                      (uint32_t)vx * (uint32_t)m[0]);
        if (k > 0) {
            m[0] = -m[0];
            m[1] = -m[1];
            m[2] = -m[2];
        } else {
            k = -k;
        }
        k >>= 8;
        vec_scale8(q, (int32_t)(fix_sqrt_table(fix_mul8(reach, reach) - fix_mul8(h, h)) << 4), m);
        vec_scale8(w, h, w);
        w[0] += q[0];
        w[1] += q[1];
        w[2] += q[2];
        q[0] = w[0] - u[0];
        q[1] = w[1] - u[1];
        q[2] = w[2] - u[2];
        g = -(int32_t)((uint32_t)q[2] * (uint32_t)m[2] + (uint32_t)q[1] * (uint32_t)m[1] +
                       (uint32_t)q[0] * (uint32_t)m[0]);
        g >>= 8;
        if (g < 0)
            g = 0;
        speed = mul16(dt, k);
        if (speed == 0 || g > speed)
            return dt;
        t = (int32_t)((uint32_t)g << 16) / speed;
        if (t > dt)
            return dt;
        result = t;
        out[0] = mul16(t, vx) - w[0] + I32(slot, 0x08);
        out[1] = mul16(t, vy) - w[1] + I32(slot, 0x0c);
        out[2] = mul16(t, vz) - w[2] + I32(slot, 0x10);
        normal_from(w, w);
        out[3] = w[0];
        out[4] = w[1];
        out[5] = w[2];
        out[6] = 0;
        out[7] = 1;
    }
    /* the contact must lie between the edge's ends */
    if (edge_side(out, a, n) < 0 || edge_side(out, b, n) > 0) {
        out[7] = 0x100000;
        return dt;
    }
    return result;
}
