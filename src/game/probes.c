/* A car's ground probes (0x476200): each wheel casts a zero-radius
 * sphere along its suspension (in the car's frame) for the track and
 * objects below it, giving the wheel its contact point, normal and
 * surface for car_suspension and the tyres.
 *
 * Per wheel w: a wheel record at car+0x180 + w*0x1c (+0: int16 mount
 * x, y, z; +8: int16 probe direction; +0x10: part index), a probe at
 * car+0x1f0 + w*0x4c (+0 offset in the car frame, +0xc its velocity,
 * +0x18 mount offset in the world frame, +0x24 contact velocity, +0x30
 * contact point from the car, +0x3c normal, +0x48 contact time scaled by
 * the probe length), the probe slot at 0x4d4700 + w*0x48 and its sweep
 * result at 0x4d4820 + w*0x28. */
#include "../runtime.h"
#include "body.h"
#include "fixed.h"
#include "physics.h"
#include "gamefn.h"

#define WHEEL(car, w) ((car) + 0x180 + (w) * 0x1c)
#define PROBE(car, w) ((car) + 0x1f0 + (w) * 0x4c)
#define g_probe_slots ((uint8_t *)0x4d4700)
#define g_probe_hits ((uint8_t *)0x4d4820)
#define g_probes_enabled (*(int32_t *)0x4b23d0)
#define g_tile(i) GP(GP(GP(0x6c1e30)) + (i) * 4)
#define g_surface_base (*(int32_t *)0x6c1e14)
#define g_gte_vector ((int16_t *)0x6ccc64)
#define g_gte_vector_words ((uint32_t *)0x6ccc64)
#define g_gte_result ((const int32_t *)0x6cccc8)

/* 0x4768f0: place a wheel's probe slot: from the mount (turned into the
 * world) along the probe direction, 44/16 of it per step. */
void GAMEAPI probe_place(uint8_t *car, const uint8_t *wheel, int32_t *probe, uint8_t *slot)
{
    g_gte_vector_words[0] = U32(wheel, 0);
    g_gte_vector_words[1] = U32(wheel, 4);
    gte_rotate();
    *(int16_t *)(slot + 0x46) = (int16_t)(*(const int16_t *)(wheel + 0x10) + 100);
    probe[6] = (int32_t)((uint32_t)g_gte_result[0] << 8);
    probe[7] = (int32_t)((uint32_t)g_gte_result[1] << 8);
    probe[8] = (int32_t)((uint32_t)g_gte_result[2] << 8);
    vec_cross(VEC(car, ANGULAR_VELOCITY), probe + 6, probe + 3);
    I32(slot, 0x08) = (I32(car, POSITION) + probe[6]) >> 8;
    I32(slot, 0x0c) = (I32(car, POSITION + 4) + probe[7]) >> 8;
    I32(slot, 0x10) = (I32(car, POSITION + 8) + probe[8]) >> 8;
    /* the original leaves an uninitialised stack word in the fourth slot */
    g_gte_vector[0] = (int16_t)(*(const int16_t *)(wheel + 8) << 4);
    g_gte_vector[1] = (int16_t)(*(const int16_t *)(wheel + 0xa) << 4);
    g_gte_vector[2] = (int16_t)(*(const int16_t *)(wheel + 0xc) << 4);
    gte_rotate();
    probe[4] += I32(car, VELOCITY + 4);
    probe[3] += I32(car, VELOCITY);
    probe[5] += I32(car, VELOCITY + 8);
    probe[0] = g_gte_result[0];
    probe[1] = g_gte_result[1];
    probe[2] = g_gte_result[2];
    I32(slot, 0x14) = (probe[0] * 44) >> 4;
    I32(slot, 0x18) = (probe[1] * 44) >> 4;
    I32(slot, 0x40) = 0;
    I32(slot, 0x1c) = (probe[2] * 44) >> 4;
}

/* 0x476780: every wheel's probe, with the probe direction's x and z set
 * (the steering lean). */
void GAMEAPI probes_place(int32_t x, int32_t z, uint8_t *car, uint8_t *probes, uint8_t *slots)
{
    int w;

    gte_load_matrix(car + MATRIX);
    for (w = 0; w < 4; w++) {
        uint8_t *wheel = WHEEL(car, w);

        *(int16_t *)(wheel + 2) = (int16_t)x;
        *(int16_t *)(wheel + 0xa) = (int16_t)z;
        probe_place(car, wheel, (int32_t *)(probes + w * 0x4c), slots + w * 0x48);
    }
}

/* 0x4766a0: each probe against the track (not walls), then objects if
 * nearer; times[w] is the time of the hit (unsigned, so none is large). */
void GAMEAPI probes_sweep(uint8_t *car, uint8_t *slots, uint8_t *hits, uint32_t *times)
{
    int32_t track_state[4];
    int w;

    for (w = 0; w < 4; w++) {
        *(uint16_t *)(slots + w * 0x48 + 0x44) = 0;
        times[w] = slot_track_sweep(car, slots + w * 0x48, (int32_t *)(hits + w * 0x28), 0x1010000);
        track_state[w] = I32(hits + w * 0x28, 0x1c);
    }
    for (w = 0; w < 4; w++) {
        uint32_t t = (uint32_t)slot_object_sweep(car, slots + w * 0x48, (int32_t *)(hits + w * 0x28), 0x10000);

        if (t < times[w])
            times[w] = t;
        else
            I32(hits + w * 0x28, 0x1c) = track_state[w];
    }
    if (!g_probes_enabled)
        for (w = 0; w < 4; w++) {
            times[w] = 0xffffffff;
            I32(hits + w * 0x28, 0x1c) = 0;
        }
}

/* 0x4765a0: the velocity of the wheel's contact point: the mount plus t of
 * the probe direction, turned into the world, crossed with the spin, plus
 * the car's velocity. */
void GAMEAPI probe_contact_velocity(uint8_t *car, const uint8_t *wheel, uint8_t *probe)
{
    int32_t t = I32(probe, 0x48), at[3];
    const int16_t *w = (const int16_t *)wheel;
    int16_t dx = (int16_t)mul16(w[4], t), dy = (int16_t)mul16(w[5], t), dz = (int16_t)mul16(w[6], t);

    g_gte_vector[0] = (int16_t)(w[0] + dx);
    g_gte_vector[1] = (int16_t)(w[1] + dy);
    g_gte_vector[2] = (int16_t)(w[2] + dz);
    g_gte_vector[3] = (int16_t)(w[6] >> 15);
    gte_rotate();
    at[0] = (int32_t)((uint32_t)g_gte_result[0] << 8);
    at[1] = (int32_t)((uint32_t)g_gte_result[1] << 8);
    at[2] = (int32_t)((uint32_t)g_gte_result[2] << 8);
    vec_cross(VEC(car, ANGULAR_VELOCITY), at, VEC(probe, 0x24));
    I32(probe, 0x24) += I32(car, VELOCITY);
    I32(probe, 0x28) += I32(car, VELOCITY + 4);
    I32(probe, 0x2c) += I32(car, VELOCITY + 8);
}

/* 0x476470: a probe's result into the probe and the wheel record: the
 * contact (or the probe's start, at time 0) relative to the car, the
 * normal, the time scaled by the probe length (+0x320), the contact
 * velocity, the cell key (+0x14) and its surface (+0x18). Returns the
 * scaled time, or -1 for no contact. */
int32_t GAMEAPI probe_result(uint8_t *car, uint8_t *probe, uint8_t *wheel, int32_t t, const uint8_t *slot,
                             const int32_t *hit)
{
    int32_t scaled, key;

    if (!hit[7]) {
        I32(wheel, 0x14) = -1;
        I32(wheel, 0x18) = 0;
        I32(probe, 0x48) = -1;
        return -1;
    }
    I32(probe, 0x40) = hit[4];
    I32(probe, 0x3c) = hit[3];
    I32(probe, 0x44) = hit[5];
    if (t == 0) {
        I32(probe, 0x30) = (int32_t)((uint32_t)I32(slot, 0x08) << 8);
        I32(probe, 0x34) = (int32_t)((uint32_t)I32(slot, 0x0c) << 8);
        I32(probe, 0x38) = (int32_t)((uint32_t)I32(slot, 0x10) << 8);
    } else {
        I32(probe, 0x30) = (int32_t)((uint32_t)hit[0] << 8);
        I32(probe, 0x34) = (int32_t)((uint32_t)hit[1] << 8);
        I32(probe, 0x38) = (int32_t)((uint32_t)hit[2] << 8);
    }
    I32(probe, 0x30) -= I32(car, POSITION);
    I32(probe, 0x34) -= I32(car, POSITION + 4);
    I32(probe, 0x38) -= I32(car, POSITION + 8);
    scaled = mul16(t, (int32_t)((uint32_t)I32(car, 0x320) << 8));
    I32(probe, 0x48) = scaled;
    probe_contact_velocity(car, wheel, probe);
    key = hit[8];
    if (key <= -1) {
        I32(wheel, 0x18) = 0;
        I32(wheel, 0x14) = key;
        return scaled;
    }
    {
        const uint8_t *tile = g_tile(key & 0xff);
        int32_t cell = I32(tile, 8) * (int16_t)((uint32_t)key >> 16) + (int8_t)((uint32_t)key >> 8);

        I32(wheel, 0x18) = (int32_t)((uint32_t)I32(GP(tile + 0x50), cell * 8 + 4) << 4) + g_surface_base;
        I32(wheel, 0x14) = key;
    }
    return scaled;
}

/* 0x476200: probe for the ground below each wheel. If no wheel finds any,
 * try again with the car taken as the other way up (+0x63f), keeping
 * that if it works. */
void GAMEAPI car_ground_probes(uint8_t *car)
{
    int32_t x = *(int16_t *)(car + 0x182), z = *(int16_t *)(car + 0x18a), tries = 0, found = 0;
    uint32_t flipped = car[0x63f], times[4];
    int w;

    for (;;) {
        x = abs32(x);
        z = abs32(z);
        if (flipped) {
            x = -x;
            z = -z;
        }
        probes_place(x, z, car, PROBE(car, 0), g_probe_slots);
        probes_sweep(car, g_probe_slots, g_probe_hits, times);
        gte_load_matrix(car + MATRIX);
        for (w = 0; w < 4; w++) {
            uint8_t *probe = PROBE(car, w), *flags = car + 0x324 + w * 0xc4;

            if (probe_result(car, probe, WHEEL(car, w), (int32_t)times[w], g_probe_slots + w * 0x48,
                             (const int32_t *)(g_probe_hits + w * 0x28)) < 0) {
                *flags = 0;
                I32(probe, 0x48) = -1;
            } else {
                found = 1;
                *flags |= 1;
            }
        }
        if (found) {
            car[0x63f] = (uint8_t)flipped;
            return;
        }
        flipped = !flipped;
        if (++tries >= 2)
            return;
    }
}
