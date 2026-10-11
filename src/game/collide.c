/* Collision response: collision_resolve (0x4615e0) and what it calls.
 *
 * A body against the track (event kind 1) is handled by contact type:
 *   1     bounce, after nudging the body out along the normal
 *   2     push the body out of the surface, then bounce
 *   8     slide: the normal part of the velocity is removed
 *   0x10  0x468400
 *   0x40  soak: velocity and momentum scaled down by the surface value
 *   else  just move on to the contact
 * Two bodies either share an impulse (0x43ba50) or, for type 8, split
 * their relative normal velocity. Bodies of type 7 that end up almost still
 * are stopped outright.
 *
 * Bounce impulses follow the usual rigid-body formula,
 *   j = -(1 + e) (v . n) / (1/m + n . ((I^-1 (r x n)) x r)),
 * in 16.16 fixed point, with the angular terms scaled down by 256. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "../coverage.h"

#define g_contact_slots (*(uint8_t **)0x6ce740) /* per body part, 0x48 bytes */
#define g_rest_speed (*(int32_t *)0x4acc50)     /* also the gravity scale (0x4662b0) */

/* 0x4622f0: contact point from world space (in 1/256 units) to the body's
 * frame; the depth to 16.16. */
void GAMEAPI contact_to_body_frame(uint8_t *body, uint8_t *contact)
{
    int32_t *p = VEC(contact, CONTACT_POINT);
    int32_t y = (int32_t)((uint32_t)p[1] << 8), x = (int32_t)((uint32_t)p[0] << 8);
    int32_t z = (int32_t)((uint32_t)p[2] << 8);

    p[0] = (int32_t)((uint32_t)(x - I32(body, POSITION)) & 0xffffff00u);
    p[1] = (int32_t)((uint32_t)(y - I32(body, POSITION + 4)) & 0xffffff00u);
    p[2] = (int32_t)((uint32_t)(z - I32(body, POSITION + 8)) & 0xffffff00u);
    I32(contact, CONTACT_DEPTH) = (int32_t)((uint32_t)I32(contact, CONTACT_DEPTH) << 8);
}

/* 0x43be60: how far to push the body out: depth along the normal plus
 * normal/128. Negates the stored depth. */
void GAMEAPI contact_push_out(int32_t *out, uint8_t *body, uint8_t *contact)
{
    const int32_t *n = VEC(contact, CONTACT_NORMAL);
    int32_t depth = -I32(contact, CONTACT_DEPTH);

    (void)body;
    I32(contact, CONTACT_DEPTH) = depth;
    out[0] = mul16(n[0], depth);
    out[1] = mul16(n[1], I32(contact, CONTACT_DEPTH));
    out[2] = mul16(n[2], I32(contact, CONTACT_DEPTH));
    out[0] += n[0] >> 7;
    out[1] += n[1] >> 7;
    out[2] = (n[2] >> 7) + out[2];
}

/* 0x43bf00: moves a body by `delta`, relinking it in the grid if it crossed
 * into another cell. */
void GAMEAPI body_translate(uint8_t *body, const int32_t *delta)
{
    int32_t x = I32(body, POSITION), y = I32(body, POSITION + 4), z = I32(body, POSITION + 8);
    int32_t ny = delta[1] + y, nz = delta[2] + z, nx = x + delta[0];

    I32(body, POSITION + 4) = ny;
    I32(body, POSITION) = nx;
    I32(body, POSITION + 8) = nz;
    if ((nz >> 24) != (z >> 24) || (ny >> 24) != (y >> 24) || (nx >> 24) != (x >> 24)) {
        grid_unlink(body);
        grid_link(body);
    }
}

/* 0x468400: contact type 0x10. Type 4 bodies move on to the end of the
 * step; others just jump there in time. */
void GAMEAPI body_contact_kind_16(uint8_t *body, int32_t step)
{
    if ((U32(body, FLAGS) & 0x1ff) == 4) {
        body_move(body, step - I32(body, TIME), 0);
        U32(body, 0x128) = 0x20;
        return;
    }
    U32(body, 0x128) = 0x20;
    I32(body, TIME) = step;
}

/* 0x4660d0: velocity of the contact point: v + w x r. */
void GAMEAPI contact_point_velocity(uint8_t *body, uint8_t *contact, int32_t *out)
{
    int32_t turn[3];

    vec_cross(VEC(body, ANGULAR_VELOCITY), VEC(contact, CONTACT_POINT), turn);
    out[0] = I32(body, VELOCITY) + turn[0];
    out[1] = I32(body, VELOCITY + 4) + turn[1];
    out[2] = I32(body, VELOCITY + 8) + turn[2];
}

/* 0x4663c0: sets the velocity, and the momentum to match. */
void GAMEAPI body_set_velocity(uint8_t *body, const int32_t *v)
{
    int32_t z = v[2], x = v[0], mass;

    I32(body, VELOCITY + 4) = v[1];
    mass = I32(body, MASS);
    I32(body, VELOCITY + 8) = z;
    I32(body, VELOCITY) = x;
    I32(body, MOMENTUM) = mul16(x, mass);
    I32(body, MOMENTUM + 4) = mul16(I32(body, VELOCITY + 4), mass);
    I32(body, MOMENTUM + 8) = mul16(I32(body, VELOCITY + 8), mass);
}

/* 0x4661a0: splits v into its part along the unit normal n and the rest. */
void GAMEAPI vec_split(const int32_t *n, const int32_t *v, int32_t *along, int32_t *across)
{
    int32_t d = vec_dot(v, n);
    int32_t ax = mul16(d, n[0]), ay = mul16(d, n[1]), az = mul16(d, n[2]);
    int32_t vy = v[1], vx = v[0], vz = v[2];

    along[0] = ax;
    along[2] = az;
    along[1] = ay;
    across[2] = vz - az;
    across[0] = vx - ax;
    across[1] = vy - ay;
}

/* 0x466440: keeps only the parts of the angular momentum and angular
 * velocity about the contact normal. */
void GAMEAPI body_keep_normal_spin(uint8_t *body, uint8_t *contact)
{
    const int32_t *n = VEC(contact, CONTACT_NORMAL);
    int32_t d = vec_dot(VEC(body, ANGULAR_MOMENTUM), n);

    I32(body, ANGULAR_MOMENTUM) = mul16(d, n[0]);
    I32(body, ANGULAR_MOMENTUM + 4) = mul16(d, n[1]);
    I32(body, ANGULAR_MOMENTUM + 8) = mul16(d, n[2]);
    d = vec_dot(VEC(body, ANGULAR_VELOCITY), n);
    I32(body, ANGULAR_VELOCITY) = mul16(d, n[0]);
    I32(body, ANGULAR_VELOCITY + 4) = mul16(d, n[1]);
    I32(body, ANGULAR_VELOCITY + 8) = mul16(d, n[2]);
}

/* 0x466ae0: stops a body dead and marks it at rest (+0x110 bit 31). */
void GAMEAPI body_stop(uint8_t *body)
{
    COVER(COVER_BODY_STOPPED);
    U32(body, 0x110) |= 0x80000000u;
    U32(body, 0x40) = U32(body, 0x44) = U32(body, 0x48) = 0;
    U32(body, 0x68) = U32(body, 0x6c) = U32(body, 0x70) = 0;
    U32(body, 0x4c) = U32(body, 0x50) = U32(body, 0x54) = 0;
    U32(body, 0xb8) = U32(body, 0xbc) = U32(body, 0xc0) = 0;
    U32(body, 0x80) = U32(body, 0x84) = U32(body, 0x88) = 0;
    U32(body, 0x74) = U32(body, 0x78) = U32(body, 0x7c) = 0;
}

/* 0x4809d0: quaternion product a * b (16.16). Re-reads a[0] and b[0] after
 * writing out[0], as the original does. */
void GAMEAPI quat_mul(const int32_t *a, const int32_t *b, int32_t *out)
{
    int32_t sa[3], sb[3], c[3];

    out[0] = mul16(a[0], b[0]) - vec_dot(a + 1, b + 1);
    vec_scale(a[0], b + 1, sa);
    vec_scale(b[0], a + 1, sb);
    vec_cross(a + 1, b + 1, c);
    out[1] = c[0] + sb[0] + sa[0];
    out[2] = c[1] + sb[1] + sa[1];
    out[3] = c[2] + sb[2] + sa[2];
}

/* 0x461d00: moves the contact point on with the body: r += v * t. */
void GAMEAPI contact_advance(uint8_t *body, uint8_t *contact, int32_t t)
{
    int32_t dx = mul16(I32(body, VELOCITY), t), dy = mul16(I32(body, VELOCITY + 4), t);
    int32_t dz = mul16(I32(body, VELOCITY + 8), t);

    I32(contact, CONTACT_POINT) += dx;
    I32(contact, CONTACT_POINT + 4) += dy;
    I32(contact, CONTACT_POINT + 8) += dz;
}

/* Angular velocity from angular momentum, and the spin quaternion from
 * that: the tail shared by the impulse routines. */
void GAMEAPI body_update_spin(uint8_t *body)
{
    int32_t *w = VEC(body, ANGULAR_VELOCITY), q[4], *spin = VEC(body, SPIN);

    mat_mul_vec(VEC(body, INVERSE_INERTIA), VEC(body, ANGULAR_MOMENTUM), w);
    w[0] >>= 8;
    w[1] >>= 8;
    w[2] >>= 8;
    q[0] = 0;
    q[1] = w[0];
    q[2] = w[1];
    q[3] = w[2];
    quat_mul(q, VEC(body, ORIENTATION), spin);
    spin[0] >>= 1;
    spin[1] >>= 1;
    spin[2] >>= 1;
    spin[3] >>= 1;
}

/* 0x4662b0: a resting contact's share of gravity as a twist about the
 * contact point. */
void GAMEAPI contact_gravity_torque(uint8_t *body, uint8_t *contact)
{
    const int32_t *n = VEC(contact, CONTACT_NORMAL);
    int32_t weight = mul16(I32(body, MASS), g_rest_speed);
    int32_t push = mul16(weight, n[1]) >> 2, impulse[3], twist[3];
    int32_t *w = VEC(body, ANGULAR_VELOCITY);

    impulse[0] = mul16(push, n[0]);
    impulse[1] = mul16(push, n[1]);
    impulse[2] = mul16(push, n[2]);
    vec_cross(impulse, VEC(contact, CONTACT_POINT), twist);
    I32(body, ANGULAR_MOMENTUM) += twist[0];
    I32(body, ANGULAR_MOMENTUM + 4) += twist[1];
    I32(body, ANGULAR_MOMENTUM + 8) += twist[2];
    mat_mul_vec(VEC(body, INVERSE_INERTIA), VEC(body, ANGULAR_MOMENTUM), w);
    w[0] >>= 8;
    w[1] >>= 8;
    w[2] >>= 8;
}

/* 0x462290: remembers the contact in the body part's slot. */
void GAMEAPI contact_save(uint8_t *body, uint8_t *contact)
{
    uint8_t *slot = g_contact_slots + body[(int8_t)body[RIGIDITY] + 0x140] * 0x48;

    I32(slot, 0x30) = I32(contact, 0x30);
    I32(slot, 0x2c) = I32(contact, 0x2c);
    I32(slot, 0x34) = I32(contact, 0x34);
    I32(slot, 0x24) = I32(contact, 0x24);
    I32(slot, 0x20) = I32(contact, 0x20);
    I32(slot, 0x28) = I32(contact, 0x28);
    I32(slot, 0x3c) = I32(contact, 0x3c);
}

/* 0x461d70: two bodies sliding (type 8) share their relative velocity
 * along the normal equally. */
void GAMEAPI contact_share_normal_velocity(uint8_t *event, uint8_t *contact)
{
    uint8_t *a = EVENT_BODY(event, EVENT_A), *b = EVENT_BODY(event, EVENT_B);
    int32_t relative[3], along[3], across[3];

    relative[1] = I32(a, VELOCITY + 4) - I32(b, VELOCITY + 4);
    relative[2] = I32(a, VELOCITY + 8) - I32(b, VELOCITY + 8);
    relative[0] = I32(a, VELOCITY) - I32(b, VELOCITY);
    vec_split(VEC(contact, CONTACT_NORMAL), relative, along, across);
    I32(a, VELOCITY) -= along[0] >> 1;
    I32(a, VELOCITY + 4) -= along[1] >> 1;
    I32(a, VELOCITY + 8) -= along[2] >> 1;
    I32(b, VELOCITY + 4) += along[1] >> 1;
    I32(b, VELOCITY) += along[0] >> 1;
    I32(b, VELOCITY + 8) += along[2] >> 1;
    I32(a, MOMENTUM) = mul16(I32(a, VELOCITY), I32(a, MASS));
    I32(a, MOMENTUM + 4) = mul16(I32(a, VELOCITY + 4), I32(a, MASS));
    I32(a, MOMENTUM + 8) = mul16(I32(a, VELOCITY + 8), I32(a, MASS));
    I32(b, MOMENTUM) = mul16(I32(b, VELOCITY), I32(b, MASS));
    I32(b, MOMENTUM + 4) = mul16(I32(b, VELOCITY + 4), I32(b, MASS));
    I32(b, MOMENTUM + 8) = mul16(I32(b, VELOCITY + 8), I32(b, MASS));
}

/* n . ((I^-1 (r x n)) x r), the angular part of the impulse denominator;
 * `late_shift`: shift by 256 after the second cross product (as for the
 * second body of 0x43ba50) instead of before it. */
static int32_t angular_term(uint8_t *body, const int32_t *r, const int32_t *n, int late_shift)
{
    int32_t t[3], w[3];

    vec_cross(r, n, t);
    mat_mul_vec(VEC(body, INVERSE_INERTIA), t, w);
    if (!late_shift) {
        w[1] >>= 8;
        w[0] >>= 8;
        w[2] >>= 8;
    }
    vec_cross(w, r, t);
    if (late_shift) {
        t[0] >>= 8;
        t[1] >>= 8;
        t[2] >>= 8;
    }
    return vec_dot(n, t);
}

/* Applies impulse j along n at r: momentum, velocity, angular momentum
 * (for rigid bodies) and spin. `velocity_first`: 0x43ba50's order. */
static void apply_impulse(uint8_t *body, int32_t j, const int32_t *n, const int32_t *r, int velocity_first)
{
    int32_t p[3], twist[3];

    vec_scale(j, n, p);
    I32(body, MOMENTUM) += p[0];
    I32(body, MOMENTUM + 4) += p[1];
    I32(body, MOMENTUM + 8) += p[2];
    if (velocity_first)
        vec_scale(I32(body, INVERSE_MASS), VEC(body, MOMENTUM), VEC(body, VELOCITY));
    if ((int8_t)body[RIGIDITY] > 1) {
        vec_cross(r, p, twist);
        I32(body, ANGULAR_MOMENTUM) += twist[0];
        I32(body, ANGULAR_MOMENTUM + 4) += twist[1];
        I32(body, ANGULAR_MOMENTUM + 8) += twist[2];
    }
    if (!velocity_first)
        vec_scale(I32(body, INVERSE_MASS), VEC(body, MOMENTUM), VEC(body, VELOCITY));
    body_update_spin(body);
}

/* 0x43b870: bounce of one body off the track, given the contact point's
 * velocity; nothing if it is already moving away. */
void GAMEAPI impulse_track(uint8_t *body, uint8_t *contact, const int32_t *point_velocity)
{
    const int32_t *n = VEC(contact, CONTACT_NORMAL), *r = VEC(contact, CONTACT_POINT);
    int32_t bounce = I32(body, RESTITUTION) + 0x10000;
    int32_t vn = vec_dot(point_velocity, n), j;

    if (vn >= 0)
        return;
    j = mul16(vn, -bounce);
    j = fix_div16(j, I32(body, INVERSE_MASS) + angular_term(body, r, n, 0));
    apply_impulse(body, j, n, r, 0);
}

/* Weight class for 0x43ba50: cars (type 3) use +0x2280, or 3 while their
 * +0x6ac entry is 1; other bodies 0. */
static int32_t weight_class(uint8_t *body)
{
    const char *state;

    if ((U32(body, FLAGS) & 0x1ff) != 3)
        return 0;
    state = *(const char **)(body + 0x6ac);
    if (state != (const char *)0xffffffff && *state == 1)
        return 3;
    return I32(body, 0x2280);
}

/* 0x43ba50: impulse between two bodies; the heavier class takes its share
 * shifted down by the difference. */
void GAMEAPI impulse_bodies(uint8_t *a, uint8_t *contact_a, const int32_t *relative_velocity, uint8_t *b,
                            uint8_t *contact_b)
{
    int32_t wa = weight_class(a), wb = weight_class(b), shift_a, shift_b;
    const int32_t *na = VEC(contact_a, CONTACT_NORMAL), *ra = VEC(contact_a, CONTACT_POINT);
    const int32_t *nb = VEC(contact_b, CONTACT_NORMAL), *rb = VEC(contact_b, CONTACT_POINT);
    int32_t bounce = I32(a, RESTITUTION) + 0x10000, j, ka, kb;

    if (wa != wb)
        COVER(COVER_BODY_WEIGHTED);
    if (wa > wb) {
        shift_a = wa - wb;
        shift_b = 0;
    } else {
        shift_b = wb - wa;
        shift_a = 0;
    }
    j = mul16(vec_dot(relative_velocity, na), -bounce);
    ka = angular_term(a, ra, na, 0);
    kb = angular_term(b, rb, nb, 1);
    j = fix_div16(j, I32(b, INVERSE_MASS) + kb + ka + I32(a, INVERSE_MASS));
    apply_impulse(a, j >> (shift_a & 31), na, ra, 1);
    apply_impulse(b, j >> (shift_b & 31), nb, rb, 1);
}

/* Type 7 bodies (and sliding ones) come to rest below these speeds. */
static int nearly_still(uint8_t *body)
{
    return (abs32(I32(body, VELOCITY + 8)) | abs32(I32(body, VELOCITY + 4)) | abs32(I32(body, VELOCITY))) <
               g_rest_speed * 2 &&
           (abs32(I32(body, ANGULAR_VELOCITY + 8)) | abs32(I32(body, ANGULAR_VELOCITY + 4)) |
            abs32(I32(body, ANGULAR_VELOCITY))) < 5000;
}

/* Type 0x40: v * 1/2, 3/4, 7/8 or 3/8 for surface 0x400, 0x200, 0x100,
 * 0x800 (each part shifted separately, as the original). */
static int32_t soak(int32_t v, int32_t surface)
{
    switch (surface) {
    case 0x400:
        return v >> 1;
    case 0x200:
        return (v >> 1) + (v >> 2);
    case 0x100:
        return (v >> 1) + ((v >> 3) + (v >> 2));
    case 0x800:
        return (v >> 2) + (v >> 3);
    default:
        return v;
    }
}

static void track_contact(uint8_t *event, uint8_t *a, uint32_t type, int32_t dt, int32_t step)
{
    uint8_t *contact = event + EVENT_CONTACT;
    int32_t point_velocity[3], push[3], along[3], across[3];

    U32(a, 0x128) = 0;
    switch (type) {
    case 1:
        COVER(COVER_TRACK_BOUNCE);
        body_move(a, dt, VEC(contact, CONTACT_NORMAL));
        contact_to_body_frame(a, contact);
        contact_point_velocity(a, contact, point_velocity);
        impulse_track(a, contact, point_velocity);
        if ((U32(a, FLAGS) & 0x1ff) == 7 && nearly_still(a))
            body_stop(a);
        break;
    case 2:
        COVER(COVER_TRACK_PUSH_OUT);
        contact_to_body_frame(a, contact);
        contact_push_out(push, a, contact);
        body_translate(a, push);
        contact_point_velocity(a, contact, point_velocity);
        impulse_track(a, contact, point_velocity);
        if ((U32(a, FLAGS) & 0x1ff) == 7 && nearly_still(a))
            body_stop(a);
        break;
    case 8:
        vec_split(VEC(contact, CONTACT_NORMAL), VEC(a, VELOCITY), along, across);
        body_set_velocity(a, across);
        if (*(int16_t *)(event + 0x58) <= (int16_t)((int8_t)a[RIGIDITY] >> 1)) {
            COVER(COVER_TRACK_SLIDE);
            body_move(a, dt, 0);
            contact_to_body_frame(a, contact);
            contact_advance(a, contact, dt);
            if ((int8_t)a[RIGIDITY] > 1)
                contact_gravity_torque(a, contact);
        } else {
            COVER(COVER_TRACK_SLIDE_SPIN);
            body_keep_normal_spin(a, contact);
            body_move(a, dt, 0);
            contact_to_body_frame(a, contact);
            contact_advance(a, contact, dt);
            if (nearly_still(a))
                body_stop(a);
        }
        break;
    case 0x10:
        COVER(COVER_TRACK_KIND_16);
        body_contact_kind_16(a, step);
        return;
    case 0x40: {
        int32_t surface;

        COVER(COVER_TRACK_SOAK);
        body_move(a, dt, 0);
        surface = *(int16_t *)(event + EVENT_SURFACE);
        I32(a, MOMENTUM) = soak(I32(a, MOMENTUM), surface);
        I32(a, VELOCITY) = soak(I32(a, VELOCITY), surface);
        I32(a, VELOCITY + 4) = soak(I32(a, VELOCITY + 4), surface);
        I32(a, VELOCITY + 8) = soak(I32(a, VELOCITY + 8), surface);
        I32(a, MOMENTUM + 4) = soak(I32(a, MOMENTUM + 4), surface);
        I32(a, MOMENTUM + 8) = soak(I32(a, MOMENTUM + 8), surface);
        return;
    }
    default:
        COVER(COVER_TRACK_OTHER);
        body_move(a, dt, 0);
        return;
    }
    contact_save(a, contact);
}

static void body_contact(uint8_t *event, uint8_t *a, uint8_t *b, uint32_t type, int32_t dt)
{
    uint8_t *contact = event + EVENT_CONTACT;
    int32_t other[0x40 / 4]; /* b's contact: point, reversed normal, depth */
    uint8_t *contact_b = (uint8_t *)other;
    int32_t *pb = VEC(contact_b, CONTACT_POINT), *nb = VEC(contact_b, CONTACT_NORMAL);
    int32_t *p = VEC(contact, CONTACT_POINT), push[3], behind, dt_b;

    if (!b) {
        COVER(COVER_BODY_ALONE);
        U32(a, FLAGS) &= ~0x2000u;
        body_move(a, dt, 0);
        return;
    }
    U32(a, 0x128) = 0;
    U32(b, 0x114) = 0xffffffff;
    U32(b, 0x128) = 0;
    U32(b, 0x110) &= 0xefffffffu;
    U32(a, FLAGS) &= ~0x2000u;
    U32(b, FLAGS) &= ~0x2000u;
    nb[0] = -I32(contact, CONTACT_NORMAL);
    nb[1] = -I32(contact, CONTACT_NORMAL + 4);
    nb[2] = -I32(contact, CONTACT_NORMAL + 8);
    pb[0] = p[0];
    pb[1] = p[1];
    pb[2] = p[2];
    I32(contact_b, CONTACT_DEPTH) = I32(contact, CONTACT_DEPTH);
    if (type == 2) {
        COVER(COVER_BODY_PUSH_OUT);
        I32(contact, CONTACT_DEPTH) = (int32_t)((uint32_t)I32(contact, CONTACT_DEPTH) << 7);
        contact_push_out(push, a, contact);
        body_translate(a, push);
        push[0] = -push[0];
        push[1] = -push[1];
        push[2] = -push[2];
        body_translate(b, push);
    } else if (type == 8) {
        COVER(COVER_BODY_SLIDE);
        contact_share_normal_velocity(event, contact);
    }
    behind = I32(a, TIME) - I32(b, TIME);
    if (behind < 0)
        dt += behind;
    if (dt)
        body_move(a, dt, VEC(contact, CONTACT_NORMAL));
    dt_b = behind + dt;
    if (dt_b)
        body_move(b, dt_b, nb);
    /* both contact points from 1/256 world units to the bodies' frames */
    p[0] = (int32_t)((uint32_t)p[0] << 8) - I32(a, POSITION);
    p[1] = (int32_t)((uint32_t)p[1] << 8) - I32(a, POSITION + 4);
    p[2] = (int32_t)((uint32_t)p[2] << 8) - I32(a, POSITION + 8);
    pb[0] = (int32_t)((uint32_t)pb[0] << 8) - I32(b, POSITION);
    pb[1] = (int32_t)((uint32_t)pb[1] << 8) - I32(b, POSITION + 4);
    pb[2] = (int32_t)((uint32_t)pb[2] << 8) - I32(b, POSITION + 8);
    if (type == 8) {
        contact_advance(a, contact, dt);
        contact_advance(b, contact_b, dt_b);
        return;
    }
    COVER(COVER_BODY_IMPULSE);
    impulse_bodies(a, contact, VEC(event, EVENT_VELOCITY), b, contact_b);
}

/* 0x4615e0: resolves the contact `event`, moving the bodies on by `dt` to
 * the impact first; `step` is the length of the whole step. */
void GAMEAPI collision_resolve(uint8_t *event, int32_t dt, int32_t step)
{
    uint8_t *a = EVENT_BODY(event, EVENT_A), *b = EVENT_BODY(event, EVENT_B);
    uint32_t type;

    if (!a)
        return;
    a[CONTACT_MODE]--;
    if (b)
        a[CONTACT_MODE]--;
    type = U32(event, EVENT_TYPE) & 0xff;
    U32(a, 0x110) &= 0xefffffffu;
    U32(a, 0x114) = 0xffffffff;
    if (U32(event, EVENT_KIND) == 1)
        track_contact(event, a, type, dt, step);
    else
        body_contact(event, a, b, type, dt);
}
