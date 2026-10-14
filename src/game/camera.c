/* The chase camera (0x406e30, once per player car after the world step).
 *
 * The camera record (car slot +0x28): +0 its body (a physics body whose
 * force this sets), +4 the car it follows, +8 look direction, +0x14 the
 * wanted up (ground normal), +0x20 wanted offset from the car, +0x2c the
 * offset eased towards it, +0x38 set once placed, +0x3c touching wheels,
 * +0x3e steps without all four down, +0x40 view (0..2), +0x42 snap,
 * +0x44/+0x46 the view's distance and height, +0x48/+0x4a eased towards
 * them, +0x4c/+0x4d/+0x50 look-back flags, +0x51 the spin flag, +0x52
 * far away (+0x53 its countdown), +0x54 how much to follow the track,
 * +0x58/+0x5a a fade. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"

#define g_race GP(0x4b1960)
#define g_game_mode (*(int32_t *)0x6cf6a0)
#define g_random_seed (*(uint32_t *)0x4b11dc)
#define g_view_distance ((const int16_t *)0x4a77e4)
#define g_view_height ((const int16_t *)0x4a77ec)
#define g_up ((const int32_t *)0x4a77f8)
#define g_slot_cameras ((uint8_t *)0x6bfba8) /* slot +0x28 */
#define CAR_SLOT_SIZE 0x274

#define CAM_BODY(cam) GP(cam)
#define S16(p, off) (*(int16_t *)((p) + (off)))

/* 0x480940: v turned by the quaternion q (q v q*). */
void GAMEAPI quat_rotate(const int32_t *q, int32_t *v)
{
    int32_t a[4] = {0, v[0], v[1], v[2]}, conj[4] = {q[0], -q[1], -q[2], -q[3]}, t[4];

    quat_mul(a, conj, t);
    quat_mul(q, t, a);
    v[0] = a[1];
    v[1] = a[2];
    v[2] = a[3];
}

/* 0x407e00: a turned a little about the car's direction of travel, if it
 * is moving; else out is left alone. */
void GAMEAPI vec_bend_by_velocity(int32_t *out, const int32_t *a, const uint8_t *car)
{
    int32_t v[3], c[3], length;

    v[0] = I32(car, VELOCITY) >> 9;
    v[1] = I32(car, VELOCITY + 4) >> 9;
    v[2] = I32(car, VELOCITY + 8) >> 9;
    length = vec_length(v);
    if (length < 0x20)
        return;
    vec_div(v, length * 2);
    vec_cross(v, a, c);
    out[0] = a[0] + c[0];
    out[1] = a[1] + c[1];
    out[2] = a[2] + c[2];
}

/* 0x407cf0: ka a + kb b, made unit length (when not tiny). If a and b
 * point (nearly) opposite ways, a is bent by the car's travel instead, or
 * without a car the sum is nudged randomly. */
void GAMEAPI vec_blend(int32_t *out, const int32_t *a, const int32_t *b, int32_t ka, int32_t kb, const uint8_t *car)
{
    int32_t nx = 0, ny = 0, nz = 0, length;

    if (vec_dot(a, b) < -65000) {
        uint32_t r;

        if (car) {
            vec_bend_by_velocity(out, a, car);
            return;
        }
        g_random_seed = g_random_seed * 16807 & 0x7fffffff;
        r = ((g_random_seed >> 15) * 0x7fff) >> 16;
        nx = (int32_t)((r & 0x1f) << 6);
        ny = (int32_t)(((r >> 5) & 0x1f) << 6);
        nz = (int32_t)(((r >> 5) & 0x3e0) << 1);
    }
    out[0] = b[0] * kb + nx + a[0] * ka;
    out[1] = b[1] * kb + ny + a[1] * ka;
    out[2] = b[2] * kb + nz + a[2] * ka;
    length = vec_length(out);
    if (length > 0x100)
        vec_div(out, length);
}

/* 0x407bd0: v becomes the blend of the directions of v and w (ka : kb,
 * in sixteenths), with the blended length. Left alone if either is under
 * 1.0 long. */
void GAMEAPI vec_blend_scaled(int32_t *v, const int32_t *w, int32_t ka, int32_t kb)
{
    int32_t a[3], b[3], la, lb;

    a[0] = v[0] >> 9;
    a[1] = v[1] >> 9;
    a[2] = v[2] >> 9;
    la = (int32_t)((uint32_t)vec_length(a) << 9);
    if (abs32(la) < 0x200)
        return;
    vec_div(a, la >> 9);
    b[0] = w[0] >> 9;
    b[1] = w[1] >> 9;
    b[2] = w[2] >> 9;
    lb = (int32_t)((uint32_t)vec_length(b) << 9);
    if (abs32(lb) < 0x200)
        return;
    vec_div(b, lb >> 9);
    vec_blend(a, a, b, ka, kb, 0);
    vec_scale((la * ka + lb * kb) / 16, a, a);
    v[0] = a[0];
    v[1] = a[1];
    v[2] = a[2];
}

/* 0x407220: a view matrix (int16, 4096 = 1.0) looking along dir with up
 * near up: rows side, up, dir. 0 (and m untouched) if degenerate. */
int32_t GAMEAPI matrix_look(int32_t *dir, const int32_t *up, int16_t *m)
{
    int32_t side[3], top[3], length = vec_length(dir), i;

    if (length < 0x100)
        return 0;
    vec_div(dir, length);
    vec_cross(dir, up, side);
    length = vec_length(side);
    if (length < 0x400)
        return 0;
    vec_div(side, length);
    vec_cross(dir, side, top);
    for (i = 0; i < 3; i++) {
        m[i] = (int16_t)(side[i] >> 4);
        m[3 + i] = (int16_t)(top[i] >> 4);
        m[6 + i] = (int16_t)(dir[i] >> 4);
    }
    return 1;
}

/* 0x40cba0: the fade (+0x5a, 0..8) moves by +0x58 until it ends. */
void GAMEAPI camera_fade_step(uint8_t *cam)
{
    int16_t step = S16(cam, 0x58);

    if (!step)
        return;
    S16(cam, 0x5a) = (int16_t)(S16(cam, 0x5a) + step);
    if (S16(cam, 0x5a) <= 0) {
        S16(cam, 0x5a) = 0;
        S16(cam, 0x58) = 0;
    } else if (S16(cam, 0x5a) >= 8) {
        S16(cam, 0x5a) = 8;
        S16(cam, 0x58) = 0;
    }
}

/* 0x407620: the view buttons (pressed bits 1, 2) step through the three
 * views; distance and height ease 4 a step towards the view's. Held
 * look-back buttons clear +0x4c/+0x4d/+0x50. Leaving or entering the
 * close view starts the fade, and the HUD learns the view and fade. */
void GAMEAPI camera_mode(uint8_t *cam, uint32_t pressed, uint32_t held)
{
    int32_t old = S16(cam, 0x40), i;
    int16_t view, distance;
    uint8_t *slot_cam;

    if (g_game_mode != 0xe) {
        if (pressed & 2)
            S16(cam, 0x40) = (int16_t)(old + 1);
        if (pressed & 1)
            S16(cam, 0x40)--;
    }
    if (S16(cam, 0x40) < 0)
        S16(cam, 0x40) = 0;
    if (S16(cam, 0x40) > 2)
        S16(cam, 0x40) = 2;
    view = S16(cam, 0x40);
    distance = g_view_distance[view];
    S16(cam, 0x44) = distance;
    S16(cam, 0x46) = g_view_height[view];
    if (S16(cam, 0x48) < distance)
        S16(cam, 0x48) = (int16_t)(S16(cam, 0x48) + 4);
    if (S16(cam, 0x48) > distance)
        S16(cam, 0x48) = (int16_t)(S16(cam, 0x48) - 4);
    if (S16(cam, 0x4a) < S16(cam, 0x46))
        S16(cam, 0x4a) = (int16_t)(S16(cam, 0x4a) + 4);
    if (S16(cam, 0x4a) > S16(cam, 0x46))
        S16(cam, 0x4a) = (int16_t)(S16(cam, 0x4a) - 4);
    cam[0x4c] = 3;
    cam[0x4d] = 3;
    cam[0x50] = 3;
    if (!g_race[0x58c]) {
        if (held & 0x10000)
            cam[0x4c] = 0;
        if (held & 0x20000)
            cam[0x4d] = 0;
        if (held & 0x4000)
            cam[0x50] = 0;
    }
    if (old == 1) {
        if (view == 0)
            S16(cam, 0x58) = 1;
    } else if (old == 0 && view == 1) {
        S16(cam, 0x58) = -1;
    }
    for (i = 0, slot_cam = g_slot_cameras; slot_cam < (uint8_t *)0x6c0090; i++, slot_cam += CAR_SLOT_SIZE)
        if (GP(slot_cam) == cam)
            break;
    if (slot_cam >= (uint8_t *)0x6c0090 || i > 1)
        return;
    g_race[0xa92 + i] = S16(cam, 0x5a) ? 8 : 0;
    g_race[0xa90 + i] = cam[0x40];
}

/* 0x407300: with all four wheels down, the camera's wanted offset from
 * the car: along dir by the distance, up the wheels' mean normal by the
 * height, lifted to the plane through the wheels; and that normal. 0 if
 * not (counting the steps without, +0x3e). */
int32_t GAMEAPI camera_target(uint8_t *cam, const uint8_t *car, const int32_t *dir, int32_t *offset, int32_t *normal)
{
    int32_t n[3] = {0, 0, 0}, p[3] = {0, 0, 0}, count = 0, w, length, a, b, d;

    for (w = 0; w < 4; w++) {
        const uint8_t *wheel = car + 0x324 + w * 0xc4;

        if (!(wheel[0] & 1))
            continue;
        count++;
        n[0] += I32(wheel, 0x40);
        n[1] += I32(wheel, 0x44);
        n[2] += I32(wheel, 0x48);
        p[0] += I32(wheel, 0x1c) / 4;
        p[1] += I32(wheel, 0x20) / 4;
        p[2] += I32(wheel, 0x24) / 4;
    }
    S16(cam, 0x3c) = (int16_t)count;
    if (count != 4) {
        if (*(uint16_t *)(cam + 0x3e) < 0x100)
            (*(uint16_t *)(cam + 0x3e))++;
        return 0;
    }
    S16(cam, 0x3e) = 0;
    length = vec_length(n);
    if (length < 0x400)
        return 0;
    vec_scale(fix_div16(0x10000, length), n, n);
    a = S16(cam, 0x48);
    b = S16(cam, 0x4a);
    d = -vec_dot(n, p);
    offset[0] = b * n[0] + mul16(n[0], d) + dir[0] * a;
    offset[1] = dir[1] * a + b * n[1] + mul16(n[1], d);
    offset[2] = b * n[2] + mul16(n[2], d) + dir[2] * a;
    normal[0] = n[0];
    normal[1] = n[1];
    normal[2] = n[2];
    return 1;
}

/* 0x4074c0: after a while airborne: a short offset grows by 1/32, the
 * wanted up eases back to world up, the offset's height drifts to the
 * view height, and it swings behind the car's ground travel. */
void GAMEAPI camera_settle(uint8_t *cam, const uint8_t *car)
{
    int32_t v[3], length, y;

    v[0] = I32(cam, 0x20) >> 4;
    v[1] = I32(cam, 0x24) >> 4;
    v[2] = I32(cam, 0x28) >> 4;
    if ((int32_t)((uint32_t)vec_length(v) << 4) < 0xf00000) {
        I32(cam, 0x20) = I32(cam, 0x20) / 32 * 33;
        I32(cam, 0x24) = I32(cam, 0x24) / 32 * 33;
        I32(cam, 0x28) = I32(cam, 0x28) / 32 * 33;
    }
    vec_blend(VEC(cam, 0x14), VEC(cam, 0x14), g_up, 0xf, 1, car);
    y = I32(cam, 0x24);
    I32(cam, 0x24) = y - (int32_t)((uint32_t)S16(cam, 0x4a) * 0x10000 + (uint32_t)y) / 32;
    v[0] = I32(car, VELOCITY) >> 8;
    v[1] = 0;
    v[2] = I32(car, VELOCITY + 8) >> 8;
    length = vec_length(v);
    if (length > 0x80) {
        int32_t k = fix_div16(0x10000, length), a = S16(cam, 0x48);
        int32_t x = I32(cam, 0x20), z = I32(cam, 0x28);

        I32(cam, 0x20) = (-(a * mul16(v[0], k)) - x) / 32 + x;
        I32(cam, 0x28) = (-(a * mul16(v[2], k)) - z) / 32 + z;
    }
}

/* 0x406f80: jump the camera straight to its wanted place. */
void GAMEAPI camera_snap(uint8_t *cam, const uint8_t *car)
{
    uint8_t *body = CAM_BODY(cam);

    grid_unlink(body);
    I32(body, POSITION) = I32(car, POSITION) + I32(cam, 0x20);
    I32(body, POSITION + 4) = I32(car, POSITION + 4) + I32(cam, 0x24);
    I32(body, POSITION + 8) = I32(car, POSITION + 8) + I32(cam, 0x28);
    memcpy(cam + 8, cam + 0x14, 12);
    memcpy(cam + 0x2c, cam + 0x20, 12);
    grid_link(body);
    S16(cam, 0x42) = 0;
}

/* 0x407850, 0x407870: far from the car the camera body stops colliding
 * (flag 0x200) for at least 20 steps. */
void GAMEAPI camera_far(uint8_t *cam)
{
    cam[0x53] = 0x14;
    U32(CAM_BODY(cam), 0) |= 0x200;
    cam[0x52] = 1;
}

void GAMEAPI camera_near(uint8_t *cam)
{
    if ((int8_t)cam[0x53] > 0) {
        cam[0x53]--;
        return;
    }
    cam[0x53] = 0;
    U32(CAM_BODY(cam), 0) &= ~0x200u;
    cam[0x52] = 0;
}

/* 0x4077e0: by a cheap distance (4 x the largest axis plus the others). */
void GAMEAPI camera_range_check(uint8_t *cam, const int32_t *v)
{
    int32_t a = abs32(v[0]), b = abs32(v[1]), c = abs32(v[2]), t;

    if (a < b) {
        t = a;
        a = b;
        b = t;
    }
    if (a < c) {
        t = a;
        a = c;
        c = t;
    }
    t = c + a * 4 + b;
    if (!cam[0x52]) {
        if (t > 0x3480000)
            camera_far(cam);
    } else if (t < 0x16e0000) {
        camera_near(cam);
    }
}

/* 0x406ff0: ease the offset, then push the camera body towards the car
 * plus it: a quarter of the gap less half its momentum. */
void GAMEAPI camera_follow(uint8_t *cam, const uint8_t *car)
{
    uint8_t *body = CAM_BODY(cam);
    int32_t gap[3];

    vec_blend_scaled(VEC(cam, 0x2c), VEC(cam, 0x20), 0xe, 2);
    gap[0] = I32(car, POSITION) - I32(body, POSITION) + I32(cam, 0x2c);
    gap[1] = I32(cam, 0x30) - I32(body, POSITION + 4) + I32(car, POSITION + 4);
    gap[2] = I32(cam, 0x34) - I32(body, POSITION + 8) + I32(car, POSITION + 8);
    camera_range_check(cam, gap);
    I32(body, FORCE) = (gap[0] >> 2) - I32(body, MOMENTUM) / 2;
    I32(body, FORCE + 4) = (gap[1] >> 2) - I32(body, MOMENTUM + 4) / 2;
    I32(body, FORCE + 8) = (gap[2] >> 2) - I32(body, MOMENTUM + 8) / 2;
}

/* 0x4070a0: point the camera body at the car, leaning by the eased up
 * (+8), unless looking back turns it (the look-back flags). */
void GAMEAPI camera_aim(uint8_t *cam, const uint8_t *car)
{
    uint8_t *body = CAM_BODY(cam);
    int32_t dir[3], up[3], q[4], length, shift;

    dir[0] = (I32(car, POSITION) - I32(body, POSITION)) >> 8;
    dir[1] = (I32(car, POSITION + 4) - I32(body, POSITION + 4)) >> 8;
    dir[2] = (I32(car, POSITION + 8) - I32(body, POSITION + 8)) >> 8;
    vec_blend(up, VEC(cam, 8), VEC(cam, 0x14), 0xf, 1, car);
    memcpy(cam + 8, up, 12);
    length = vec_length(dir);
    if (length < 0x100)
        return;
    vec_div(dir, length);
    shift = g_race[0x552] ? 3 : 2;
    dir[0] += up[0] >> shift;
    dir[1] += up[1] >> shift;
    dir[2] += up[2] >> shift;
    if (!cam[0x50]) {
        q[0] = 0;
        q[1] = up[0];
        q[2] = up[1];
        q[3] = up[2];
        quat_rotate(q, dir);
    } else if (!cam[0x4c]) {
        q[0] = 0x10000;
        q[1] = up[0];
        q[2] = up[1];
        q[3] = up[2];
        quat_rotate(q, dir);
    } else if (!cam[0x4d]) {
        q[0] = 0x10000;
        q[1] = -up[0];
        q[2] = -up[1];
        q[3] = -up[2];
        quat_rotate(q, dir);
    }
    matrix_look(dir, up, (int16_t *)(body + MATRIX));
}

/* 0x4078a0: the direction to sit along: behind the car (its forward
 * axis, negated), turned towards the track's direction as the car spins
 * about the track's up (+0x54 eases how much, +0x51 with hysteresis). */
void GAMEAPI camera_direction(int32_t *out, const uint8_t *car, uint8_t *cam)
{
    int32_t dir[3], up[3], back[3], follow = 0, length;

    if (track_direction(dir, up, car)) {
        int32_t spin = abs32(vec_dot(up, VEC(car, ANGULAR_VELOCITY))) - (cam[0x51] ? 5000 : 7000), k;

        if (spin < 0) {
            k = 0;
            cam[0x51] = 0;
        } else {
            k = (int32_t)((uint32_t)spin << 4);
            if (k > 0x10000) {
                k = 0x10000;
                cam[0x51] = 1;
            } else if (k < 0x4000) {
                cam[0x51] = 0;
            } else {
                cam[0x51] = 1;
            }
        }
        follow = (I32(cam, 0x54) * 7 + k) / 8;
        I32(cam, 0x54) = follow;
    }
    back[0] = (int32_t)((uint32_t)-S16(car, MATRIX + 4) << 4);
    back[1] = (int32_t)((uint32_t)-S16(car, MATRIX + 10) << 4);
    back[2] = (int32_t)((uint32_t)-S16(car, MATRIX + 16) << 4);
    vec_normalise(back);
    if (follow <= 0) {
        memcpy(out, back, 12);
        return;
    }
    length = vec_length(dir);
    if (length > 0x100)
        vec_div(dir, length);
    vec_scale(follow, dir, dir);
    vec_scale(0x10000 - follow, back, back);
    out[0] = back[0] + dir[0];
    out[1] = back[1] + dir[1];
    out[2] = back[2] + dir[2];
    length = vec_length(out);
    if (length > 0x400)
        vec_div(out, length);
}

/* 0x406ed0: place the camera for the car: its first good target places
 * it; airborne a while it settles; a snap request jumps there. */
void GAMEAPI camera_track(uint8_t *cam, const uint8_t *car)
{
    int32_t dir[3], found;

    camera_direction(dir, car, cam);
    found = camera_target(cam, car, dir, VEC(cam, 0x20), VEC(cam, 0x14));
    if (!I32(cam, 0x38)) {
        if (!found)
            return;
        memcpy(cam + 8, cam + 0x14, 12);
        memcpy(cam + 0x2c, cam + 0x20, 12);
        I32(cam, 0x38) = 1;
    }
    if (*(uint16_t *)(cam + 0x3e) > 5)
        camera_settle(cam, car);
    if (S16(cam, 0x42) == 1)
        camera_snap(cam, car);
    camera_follow(cam, car);
    camera_aim(cam, car);
}

/* 0x406e30: the camera body is moved by force only: no spin. */
void GAMEAPI camera_update(uint8_t *slot)
{
    uint8_t *car = GP(slot), *cam = GP(slot + 0x28), *body = CAM_BODY(cam);
    const uint8_t *target = GP(cam + 4);

    memset(body + ANGULAR_MOMENTUM, 0, 12);
    memset(body + TORQUE, 0, 12);
    memset(body + SPIN, 0, 16);
    memset(body + ORIENTATION, 0, 16);
    memset(body + ANGULAR_VELOCITY, 0, 12);
    camera_mode(cam, U32(car, 0x704), U32(car, 0x700));
    camera_fade_step(cam);
    camera_track(cam, target);
}
