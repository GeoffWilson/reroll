/* Body against body: when, within a step, do two bodies' contact spheres
 * first touch (0x413c80), testing each part of one against each part of
 * the other. Slots as in detect.c. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"

#define SLOT_SIZE 0x48
#define g_slots (*(uint8_t **)0x6ce740)

/* The slots' relative motion over the step: b - a, the velocity scaled up
 * to the whole step (dt - 1 rounded up to a power of two in 16.16). */
struct relative {
    int32_t d[3], v[3], end[3], reach;
};

static void relative_motion(const uint8_t *a, const uint8_t *b, int32_t dt, struct relative *r)
{
    int32_t shift, i;

    for (i = 0; i < 3; i++) {
        r->d[i] = I32(b, 0x08 + i * 4) - I32(a, 0x08 + i * 4);
        r->v[i] = I32(b, 0x14 + i * 4) - I32(a, 0x14 + i * 4);
    }
    shift = 16 - count_leading_zeros_of((uint32_t)(dt - 1));
    if (shift < 0)
        shift = 0;
    for (i = 0; i < 3; i++)
        r->end[i] = (int32_t)((uint32_t)r->v[i] << shift) + r->d[i];
    r->reach = I32(a, 0x40) + I32(b, 0x40);
}

/* outside the reach on some axis, and not coming back within it */
static int axis_misses(int32_t d, int32_t end, int32_t v, int32_t r)
{
    if (d > r)
        return end > r || v >= 0;
    if (d < -r)
        return end < -r || v <= 0;
    return 0;
}

static int box_misses(const struct relative *r)
{
    return axis_misses(r->d[0], r->end[0], r->v[0], r->reach) || axis_misses(r->d[1], r->end[1], r->v[1], r->reach) ||
           axis_misses(r->d[2], r->end[2], r->v[2], r->reach);
}

static int32_t dot8(const int32_t *p, const int32_t *q)
{
    return fix_mul8(p[0], q[0]) + fix_mul8(p[1], q[1]) + fix_mul8(p[2], q[2]);
}

/* 0x413f70: do the bodies' outer spheres meet within dt? Sets the low byte
 * of type to 1 (returning the time) or 2 (overlapping, or starting to);
 * clears it if they never meet; leaves it if they are far apart. */
int32_t GAMEAPI sphere_pair_bounds(const uint8_t *a, const uint8_t *b, uint32_t *type, int32_t dt)
{
    struct relative r;
    int32_t dist2, reach2, speed2, along, root, t, t2;

    relative_motion(a, b, dt, &r);
    if (box_misses(&r))
        return 0;
    dist2 = dot8(r.d, r.d);
    reach2 = fix_mul8(r.reach, r.reach);
    if (dist2 < reach2) {
        *type = (*type & ~0xffu) + 2;
        return 0;
    }
    speed2 = dot8(r.v, r.v);
    if (!speed2) {
        *type &= ~0xffu;
        return 0;
    }
    along = fix_mul8(r.d[0], r.v[0]) + fix_mul8(r.d[1], r.v[1]) + fix_mul8(r.d[2], r.v[2]);
    root = quad_root(speed2, along, dist2 - reach2);
    if (root < 0) {
        *type &= ~0xffu;
        return 0;
    }
    t = root - along;
    t2 = -(along + root);
    if (t > t2) {
        int32_t swap = t;
        t = t2;
        t2 = swap;
    }
    t = (int32_t)((uint32_t)fix_div8(t, speed2) << 8);
    if (t < 0 && t2 < 0) {
        *type &= ~0xffu;
        return 0;
    }
    if ((t2 ^ t) < 0) {
        *type = (*type & ~0xffu) + 2;
        return 0;
    }
    *type = 1;
    return t;
}

/* 0x4142c0: sphere a against sphere b within dt: the time of contact,
 * with the contact point (on a's surface), the normal from b to a
 * (1 << 12) and, when already overlapping, the depth. type as in
 * slot_track_sweep (0x80000: only grazing). */
int32_t GAMEAPI sphere_pair_contact(const uint8_t *a, const uint8_t *b, uint32_t *type, int32_t *point,
                                    int32_t *normal, int32_t *depth, int32_t dt)
{
    struct relative r;
    int32_t along, dist2, outer, speed2, root, t, gap, i;

    relative_motion(a, b, dt, &r);
    if (box_misses(&r))
        return dt;
    along = fix_mul8(r.d[0], r.v[0]) + fix_mul8(r.d[1], r.v[1]) + fix_mul8(r.d[2], r.v[2]);
    dist2 = dot8(r.d, r.d);
    outer = r.reach >> 4;
    if (dist2 > outer * outer) {
        /* apart now */
        if (along > 0) {
            *type &= ~0xffu;
            return dt;
        }
        speed2 = dot8(r.v, r.v);
        if (!speed2) {
            *type &= ~0xffu;
            return dt;
        }
        root = quad_root(speed2, along, dist2 - (r.reach * r.reach >> 8));
        if (root < 0) {
            *type &= ~0xffu;
            return 0;
        }
        t = root - along;
        if (-(root + along) < t)
            t = -(root + along);
        t = (int32_t)((uint32_t)fix_div8(t, speed2) << 8);
        gap = (int32_t)(fix_sqrt_table(dist2) << 4) - r.reach;
        if (t >= 0) {
            int32_t pa[3], pb[3];

            if (t > dt) {
                *type &= ~0xffu;
                return dt;
            }
            *type = 1;
            vec_scale8(pa, t >> 8, (const int32_t *)(a + 0x14));
            for (i = 0; i < 3; i++)
                pa[i] += I32(a, 0x08 + i * 4);
            vec_scale8(pb, t >> 8, (const int32_t *)(b + 0x14));
            for (i = 0; i < 3; i++)
                pb[i] += I32(b, 0x08 + i * 4);
            normal[0] = pb[0] - pa[0];
            normal[1] = pb[1] - pa[1];
            normal[2] = pb[2] - pa[2];
            vec_normalise_shifted(normal, normal);
            normal[0] >>= 4;
            normal[1] >>= 4;
            normal[2] >>= 4;
            vec_scale8(point, I32(a, 0x40), normal);
            point[1] += pa[1];
            point[0] += pa[0];
            point[2] += pa[2];
            if (gap < 0x100) {
                int32_t closing = fix_mul8(normal[0], r.v[0]) + fix_mul8(normal[1], r.v[1]) +
                                  fix_mul8(normal[2], r.v[2]);

                if (closing > 0 && closing < 0x1000) {
                    *type = (*type & ~0xffu) | 0x80000;
                    return dt;
                }
            }
            if (t)
                return t;
            *type = 2;
            *depth = -0x10;
            return 0;
        }
        /* overlapping at the start */
        vec_normalise_shifted(r.d, normal);
        normal[0] = -(normal[0] >> 4);
        normal[1] = -(normal[1] >> 4);
        normal[2] = -(normal[2] >> 4);
        *depth = gap;
        vec_scale8(point, -((gap >> 1) + I32(a, 0x40)), normal);
        vec_scale8(point, -I32(a, 0x40), normal);
    } else {
        int32_t inner = (r.reach - 0x100) >> 4;

        if (dist2 >= inner * inner && dist2 <= outer * outer && along > 0) {
            *type &= ~0xffu;
            return dt;
        }
        gap = (int32_t)(fix_sqrt_table(dist2) << 4);
        vec_normalise_shifted(r.d, normal);
        normal[0] = -(normal[0] >> 4);
        normal[1] = -(normal[1] >> 4);
        normal[2] = -(normal[2] >> 4);
        gap -= r.reach;
        *depth = gap;
        vec_scale8(point, -((gap >> 1) + I32(a, 0x40)), normal);
    }
    point[0] += I32(a, 0x08);
    point[1] += I32(a, 0x0c);
    point[2] += I32(a, 0x10);
    *type = (*type & ~0xffu) + 2;
    return 0;
}

static int part_override(const uint8_t *body)
{
    const char *flag = *(const char *const *)(body + 0x6ac);

    return (U32(body, FLAGS) & 0x1ff) == 3 && flag != (const char *)-1 && *flag;
}

/* 0x413c80: when within dt do bodies a and b first touch? Fills the
 * contact (point, normal, depth when overlapping) and the relative
 * velocity; type 1 hit, 2 overlapping, 8 resting at dt, 0 none. A car
 * with +0x6ac set uses only its whole-body sphere. */
int32_t GAMEAPI body_pair_test(uint8_t *a, uint8_t *b, uint32_t *type, uint8_t *contact, int32_t *velocity,
                               int32_t dt)
{
    int32_t dx, dy, dz, a_parts = (int8_t)a[RIGIDITY], b_parts = (int8_t)b[RIGIDITY];
    int32_t a_first = 0, b_first = 0, i, j, best = 0x7fffffff, flag = -1;

    *type = 0;
    dx = (I32(a, POSITION) - I32(b, POSITION)) >> 16;
    dy = (I32(a, POSITION + 4) - I32(b, POSITION + 4)) >> 16;
    dz = (I32(a, POSITION + 8) - I32(b, POSITION + 8)) >> 16;
    if ((uint32_t)(dz * dz + dy * dy + dx * dx) > 0x4000)
        return dt;
    if (!((U32(b, FLAGS) ^ U32(a, FLAGS)) & 0x1ff) && (U32(a, FLAGS) & 0x4000))
        return dt;
    if (a_parts > 1 || b_parts > 1) {
        int32_t t = sphere_pair_bounds(g_slots + a[0x140 + a_parts] * SLOT_SIZE,
                                       g_slots + b[0x140 + b_parts] * SLOT_SIZE, type, dt);

        if (!*type)
            return dt;
        if (t > dt) {
            *type = 0;
            return dt;
        }
    }
    if (part_override(a))
        a_first = a_parts++;
    if (part_override(b))
        b_first = b_parts++;
    for (i = a_first; i < a_parts; i++) {
        const uint8_t *sa = g_slots + a[0x140 + i] * SLOT_SIZE;

        for (j = b_first; j < b_parts; j++) {
            const uint8_t *sb = g_slots + b[0x140 + j] * SLOT_SIZE;
            int32_t point[3], normal[3], t;

            *type = 0;
            t = sphere_pair_contact(sa, sb, type, point, normal, &flag, dt);
            if (!*type)
                continue;
            memcpy(contact + 0x20, point, 12);
            I32(contact, 0x2c) = (int32_t)((uint32_t)normal[0] << 8);
            I32(contact, 0x30) = (int32_t)((uint32_t)normal[1] << 8);
            I32(contact, 0x34) = (int32_t)((uint32_t)normal[2] << 8);
            velocity[1] = I32(sa, 0x18) - I32(sb, 0x18);
            velocity[2] = I32(sa, 0x1c) - I32(sb, 0x1c);
            velocity[0] = I32(sa, 0x14) - I32(sb, 0x14);
            if (*type == 2) {
                I32(contact, 0x3c) = flag;
                return t;
            }
            best = t;
            flag = *type == 1;
        }
    }
    if (best >= 0x7fffffff)
        return dt;
    if (best == dt && !flag) {
        *type = 8;
        return best;
    }
    *type = 1;
    return best;
}
