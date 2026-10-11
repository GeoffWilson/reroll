/* Scripted bodies (0x462e80, from bodies_apply_forces and the particle
 * update): moving track objects that follow a little script rather than
 * forces. The script is a stream of int16 at body+0x134 + (u16)+0x138:
 *   0 x y z        velocity (<< 8) for this step
 *   1 n _ x y z    hold velocity x y z for n steps (timer +0x13a), then on
 *   2 _ X Y Z      jump to position X Y Z (16.16, dwords)
 *   3 n nodes...   follow a spline through n 16-byte nodes (u16 speed,
 *   7 n nodes...   int32 x y z, flags at +0x12); 7 also turns the body to
 *                  face along the path
 *   4              back to the start position (track table, +0x13f)
 *   6 a b          trigger 0x41ac20 (b a dword)
 * A node's flags (low 4 bits): 1 pause for a count of steps, 2 the body
 * is done (0x45bbf0), 3 an effect once (0x45bd30). The velocity becomes
 * momentum (times the mass, +0xa8). */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "../coverage.h"

typedef void(GAMEAPI *body_done_fn)(uint8_t *body, int32_t a, int32_t b);
typedef void(GAMEAPI *body_effect_fn)(uint8_t *body, int32_t kind);
typedef void(GAMEAPI *body_trigger_fn)(uint8_t *body, int32_t a, int32_t b);
#define body_script_done ((body_done_fn)0x45bbf0)
#define body_script_effect ((body_effect_fn)0x45bd30)
#define body_script_trigger ((body_trigger_fn)0x41ac20)

#define g_track (*(uint8_t **)0x6c1eb0)
#define g_spline_a ((const int16_t *)0x4b11ec) /* pairs per step of 1/256 */
#define g_spline_b ((const int16_t *)0x4b15ec) /* the same, read backwards */

#define SCRIPT 0x134
#define SCRIPT_AT 0x138
#define SCRIPT_TIME 0x13a
#define SCRIPT_COUNT 0x13c
#define SCRIPT_NODE 0x13e
#define SCRIPT_START 0x13f

/* 0x463810: ((b - a) >> 8) * scale */
void GAMEAPI vec_step_diff(int32_t *out, const int32_t *a, const int32_t *b, int32_t scale)
{
    out[0] = ((b[0] - a[0]) >> 8) * scale;
    out[1] = ((b[1] - a[1]) >> 8) * scale;
    out[2] = ((b[2] - a[2]) >> 8) * scale;
}

/* 0x463610: the change along a cubic spline through p0..p3 from t to
 * t + dt (1/256 of the segment, capped at the end). */
void GAMEAPI spline_velocity(int32_t *out, const int32_t *p0, const int32_t *p1, const int32_t *p2,
                             const int32_t *p3, int32_t t, int32_t dt)
{
    int32_t end = t + dt, c0, c1, c2, c3, i;

    if (end > 0x100)
        end = 0x100;
    c0 = (g_spline_a[end * 2] - g_spline_a[t * 2]) << 2;
    c1 = (g_spline_a[end * 2 + 1] - g_spline_a[t * 2 + 1]) << 2;
    c2 = (g_spline_b[1 - end * 2] - g_spline_b[1 - t * 2]) << 2;
    c3 = (g_spline_b[-end * 2] - g_spline_b[-t * 2]) << 2;
    for (i = 0; i < 3; i++)
        out[i] = mul16(p0[i], c0) + mul16(p1[i], c1) + mul16(p2[i], c2) + mul16(p3[i], c3);
}

/* 0x462e80 */
void GAMEAPI body_script_step(uint8_t *body)
{
    uint32_t flags = 0;
    int32_t facing[3] = {0, 0, 0};

    for (;;) {
        const int16_t *op = (const int16_t *)(*(uint8_t **)(body + SCRIPT) + *(uint16_t *)(body + SCRIPT_AT));
        const int16_t *arg = op + 1;
        int32_t code = op[0], advance = 0, again = 0;

        switch (code) {
        case 0:
            advance = 8;
            I32(body, VELOCITY) = arg[0] << 8;
            flags |= 1;
            I32(body, VELOCITY + 4) = arg[1] << 8;
            I32(body, VELOCITY + 8) = arg[2] << 8;
            break;
        case 1:
            flags |= 1;
            if (*(int16_t *)(body + SCRIPT_TIME) == 0) {
                *(int16_t *)(body + SCRIPT_TIME) = arg[0];
                I32(body, VELOCITY) = arg[2] << 8;
                I32(body, VELOCITY + 4) = arg[3] << 8;
                I32(body, VELOCITY + 8) = arg[4] << 8;
            } else if (--*(int16_t *)(body + SCRIPT_TIME) == 0) {
                advance = 0xc;
            }
            break;
        case 2:
            advance = 0x10;
            if (!(U32(body, FLAGS) & 0x8000))
                grid_unlink(body);
            memcpy(body + POSITION, arg + 1, 12);
            if (!(U32(body, FLAGS) & 0x8000))
                grid_link(body);
            again = 1;
            break;
        case 3:
        case 7: {
            int32_t nodes = arg[0], weight = 0, rest = 0x10000, time, step, w[3] = {0, 0, 0}, s[3] = {0, 0, 0};
            const uint8_t *first = (const uint8_t *)(arg + 1), *node = first + body[SCRIPT_NODE] * 16;
            uint32_t speed = *(const uint16_t *)node, node_flags = *(const uint16_t *)(node + 0x12);
            const int32_t *p;

            COVER(COVER_SCRIPT_PATH);
            if (node_flags & 0xf) {
                uint32_t kind = node_flags & 0xf;

                if (kind == 1) {
                    int16_t count;

                    if (*(int16_t *)(body + SCRIPT_COUNT) == 0)
                        *(int16_t *)(body + SCRIPT_COUNT) = (int16_t)(((node_flags >> 4) & 0x7ff) + 1);
                    count = *(int16_t *)(body + SCRIPT_COUNT);
                    if (count > 0) {
                        *(int16_t *)(body + SCRIPT_COUNT) = (int16_t)(count - 1);
                        if (count == 1)
                            *(int16_t *)(body + SCRIPT_COUNT) = -1;
                        I32(body, VELOCITY) = 0;
                        I32(body, VELOCITY + 4) = 0;
                        I32(body, VELOCITY + 8) = 0;
                        flags |= 1;
                        break;
                    }
                } else if (kind == 2) {
                    body_script_done(body, 0, 0);
                    return;
                } else if (kind == 3 && *(int16_t *)(body + SCRIPT_COUNT) == 0) {
                    body_script_effect(body, 7);
                    *(int16_t *)(body + SCRIPT_COUNT) = 1;
                }
            }
            p = (const int32_t *)(node + 4);
            if (*(int16_t *)(body + SCRIPT_TIME) == 0) {
                *(int16_t *)(body + SCRIPT_TIME) = 0x1000;
            } else {
                *(int16_t *)(body + SCRIPT_TIME) = (int16_t)(*(int16_t *)(body + SCRIPT_TIME) - speed);
                if (*(int16_t *)(body + SCRIPT_TIME) <= 0) {
                    *(int16_t *)(body + SCRIPT_TIME) = 0x1000;
                    body[SCRIPT_NODE]++;
                    *(int16_t *)(body + SCRIPT_COUNT) = 0;
                    p = (const int32_t *)(first + body[SCRIPT_NODE] * 16 + 4);
                    if (body[SCRIPT_NODE] == nodes) {
                        /* end of the path */
                        advance = nodes * 16 + 0x34;
                        *(int16_t *)(body + SCRIPT_TIME) = 0;
                        body[SCRIPT_NODE] = 0;
                        *(int16_t *)(body + SCRIPT_COUNT) = 0;
                        again = 1;
                        break;
                    }
                }
            }
            if (code == 7) {
                /* blend between the path's chord and its curve by how
                 * straight it is, and face along it */
                int32_t a[3], b[3], d1[3], d2[3], l1, l2, cosine, e, i;

                vec_step_diff(a, p, p + 4, 1);
                vec_step_diff(b, p + 4, p + 8, 1);
                for (i = 0; i < 3; i++)
                    d1[i] = b[i] + a[i];
                vec_step_diff(a, p + 8, p + 12, 1);
                for (i = 0; i < 3; i++)
                    d2[i] = b[i] + a[i];
                l1 = vec_length(d1);
                l2 = vec_length(d2);
                cosine = fix_div16(mul16(d1[2], d2[2]) + mul16(d1[1], d2[1]) + mul16(d1[0], d2[0]), mul16(l2, l1));
                weight = cosine * 2 - 0x10000;
                if (weight < 0)
                    weight = 0;
                else if (weight > 0x10000)
                    weight = 0x10000;
                rest = 0x10000 - weight;
                e = 0x1000 - *(int16_t *)(body + SCRIPT_TIME);
                vec_scale8(d1, (0x1000 - e) >> 4, d1);
                vec_scale8(d2, e >> 4, d2);
                for (i = 0; i < 3; i++)
                    facing[i] = d2[i] + d1[i];
            }
            time = (0x1000 - *(int16_t *)(body + SCRIPT_TIME)) >> 4;
            flags |= 1;
            step = (int32_t)speed >> 4;
            if (weight) {
                vec_step_diff(w, p + 4, p + 8, step);
                w[0] = mul16(weight, w[0]);
                w[1] = mul16(weight, w[1]);
                w[2] = mul16(weight, w[2]);
            }
            if (rest) {
                spline_velocity(s, p, p + 4, p + 8, p + 12, time, step);
                s[0] = mul16(rest, s[0]);
                s[1] = mul16(rest, s[1]);
                s[2] = mul16(rest, s[2]);
            }
            I32(body, VELOCITY) = w[0] + s[0];
            I32(body, VELOCITY + 4) = w[1] + s[1];
            I32(body, VELOCITY + 8) = s[2] + w[2];
            if (code == 7)
                flags |= 2;
            break;
        }
        case 4: {
            const int16_t *start = (const int16_t *)(*(uint8_t **)(g_track + 0xa8) + body[SCRIPT_START] * 8);

            *(uint16_t *)(body + SCRIPT_AT) = 0;
            again = 1;
            I32(body, POSITION) = start[0] << 16;
            I32(body, POSITION + 4) = start[1] << 16;
            I32(body, POSITION + 8) = start[2] << 16;
            I32(body, VELOCITY) = 0;
            I32(body, VELOCITY + 4) = 0;
            I32(body, VELOCITY + 8) = 0;
            break;
        }
        case 6:
            advance = 8;
            body_script_trigger(body, arg[0], *(const int32_t *)(arg + 1));
            again = 1;
            break;
        default:
            /* 5, 8 and others: nothing (8 waits for ever) */
            break;
        }
        *(uint16_t *)(body + SCRIPT_AT) = (uint16_t)(*(uint16_t *)(body + SCRIPT_AT) + advance);
        if (!again)
            break;
    }

    if (flags & 1) {
        int32_t mass = I32(body, MASS);

        if (mass == 0x10000) {
            I32(body, MOMENTUM) = I32(body, VELOCITY);
            I32(body, MOMENTUM + 4) = I32(body, VELOCITY + 4);
            I32(body, MOMENTUM + 8) = I32(body, VELOCITY + 8);
        } else {
            I32(body, MOMENTUM) = mul16(I32(body, VELOCITY), mass);
            I32(body, MOMENTUM + 4) = mul16(I32(body, VELOCITY + 4), mass);
            I32(body, MOMENTUM + 8) = mul16(I32(body, VELOCITY + 8), mass);
        }
    }
    if (flags & 2) {
        int32_t up[3], side[3], forward[3];

        memcpy(up, facing, 12);
        vec_normalise_shifted(up, up);
        up[2] <<= 4;
        up[0] <<= 4;
        up[1] <<= 4;
        side[0] = up[2];
        side[1] = 0;
        side[2] = -up[0];
        if (vec_length(side) < 0x400)
            return;
        vec_normalise(side);
        vec_cross(up, side, forward);
        if (vec_length(side) < 0xff78)
            return;
        COVER(COVER_SCRIPT_TURNED);
        *(int16_t *)(body + MATRIX + 0x0) = (int16_t)(side[0] >> 4);
        *(int16_t *)(body + MATRIX + 0x6) = (int16_t)(side[1] >> 4);
        *(int16_t *)(body + MATRIX + 0xc) = (int16_t)(side[2] >> 4);
        *(int16_t *)(body + MATRIX + 0x2) = (int16_t)(forward[0] >> 4);
        *(int16_t *)(body + MATRIX + 0x8) = (int16_t)(forward[1] >> 4);
        *(int16_t *)(body + MATRIX + 0xe) = (int16_t)(forward[2] >> 4);
        *(int16_t *)(body + MATRIX + 0x4) = (int16_t)(up[0] >> 4);
        *(int16_t *)(body + MATRIX + 0xa) = (int16_t)(up[1] >> 4);
        *(int16_t *)(body + MATRIX + 0x10) = (int16_t)(up[2] >> 4);
        U32(body, 0x108) |= 1;
    }
}
