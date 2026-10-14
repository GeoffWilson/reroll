/* Car controls, once per car per step before the world step (sim_step,
 * 0x444120): the player's (or network's) control packet becomes the car's
 * control record at car+0x6f8, which becomes steering, throttle and brake.
 *
 * Control record (car+0x6f8):
 *   +0 steering, +1 throttle, +2 brake (0..255, steering centred on 0x80)
 *   +4 previous buttons, +8 buttons, +0xc buttons newly pressed
 *   +0x14 control kind (8 AI, 9/10 player), +0x15 not a local packet
 *   +0x20/+0x21/+0x22 digital ramp positions for steering, throttle, brake
 *   +0x24 the packet
 * Buttons: 0x40 accelerate, 0x80 brake, 0x10 handbrake?, 0x2000 right,
 * 0x8000 left. */
#include "../runtime.h"
#include "body.h"
#include "physics.h"
#include "gamefn.h"

typedef void(GAMEAPI *car_fn)(uint8_t *car);
#define analogue_curve GCALL(0x428e90, controls_curve)    /* controls_curve, gl_ga.c (x87 kept as inline asm) */

/* Per player input state, 0x66c bytes from 0x5d2cd0 */
#define g_net_flags (*(uint32_t *)0x5d622c)   /* bit 0 network game, bit 7 buffered */
#define g_net_local (*(int32_t *)0x5d535c)
#define g_net_time (*(uint32_t *)0x5d599c)
#define g_sim_steps (*(uint32_t *)0x4c2458)
#define g_options (GP(0x4b2198))
#define PLAYER_INPUT(p) ((uint8_t *)0x5d2cd0 + (p) * 0x66c)

#define g_race (GP(0x4b1960))
#define g_car_slots ((uint8_t *)0x6bfb80)
#define CAR_SLOT_SIZE 0x274
#define g_steer_ramp ((const uint8_t *)0x4a7e78)
#define g_throttle_ramp ((const uint8_t *)0x4a7e9c)
#define g_brake_ramp ((const uint8_t *)0x4a7ebc)

#define CONTROLS 0x6f8
#define BUTTON_LEFT 0x8000u
#define BUTTON_RIGHT 0x2000u

/* 0x41b6d0: digital controls ramp the steering, throttle and brake bytes
 * (by the slot's assists, +0x26e, unless AI-driven). */
void GAMEAPI controls_digital(const uint8_t *slot, uint8_t *record)
{
    int32_t assists = *(const int16_t *)(slot + 0x26e), steer = record[0x20], throttle = record[0x21];
    uint32_t buttons = U32(record, 8);
    int left = 0;

    if (record[0x14] == 8)
        assists = 0;
    if (!(assists & 1)) {
        if (buttons & 0x40) {
            if (throttle < 0x1f)
                throttle++;
        } else if ((throttle -= 2) < 0) {
            throttle = 0;
        }
        record[0x21] = (uint8_t)throttle;
        record[1] = g_throttle_ramp[throttle & 0xff];
    } else {
        record[0x21] = 0;
    }
    if (!(assists & 2)) {
        int32_t brake = buttons & 0x80 ? 0xf : 0;

        record[0x22] = (uint8_t)brake;
        record[2] = g_brake_ramp[brake];
    } else {
        record[0x22] = 0;
    }
    if (assists & 4) {
        record[0x20] = 0x10;
        return;
    }
    if (buttons & BUTTON_LEFT) {
        left = 1;
        if (steer > 0x10)
            steer = 0x10;
        else if ((steer -= 3) <= 0)
            steer = 0;
    }
    if (buttons & BUTTON_RIGHT) {
        if (steer < 0x10) {
            steer = 0x10;
        } else {
            steer += 3;
            if (steer >= 0x20)
                steer = 0x20;
        }
    } else if (!left) {
        /* back to the centre */
        if (steer > 0x10) {
            steer -= 4;
            if (steer < 0x10)
                steer = 0x10;
        } else if (steer < 0x10) {
            steer += 4;
            if (steer > 0x10)
                steer = 0x10;
        }
    }
    record[0x20] = (uint8_t)steer;
    record[0] = g_steer_ramp[steer & 0xff];
}

static int32_t net_local_player(void)
{
    return g_net_local == -1 ? 0 : I32(PLAYER_INPUT(g_net_local), 0x18);
}

/* 0x427dd0: a raw control packet (from controls_packet: action mask, then
 * nav, X, Y, Z bytes) into the game's packet: +1 'A' (local), +2/+3
 * buttons (bits inverted: 0 = pressed), +4 the mask's high half, +8/+9/+10
 * analogue steering, brake and throttle by the slot's analogue options
 * (+0x26e). */
void GAMEAPI controls_decode(int32_t player, const uint8_t *raw, uint8_t *packet)
{
    int32_t options = *(const int16_t *)(g_car_slots + player * CAR_SLOT_SIZE + 0x26e);
    uint32_t mask;

    memset(packet, 0, 12);
    packet[1] = 'A';
    packet[2] = 0xff;
    packet[3] = 0xff;
    mask = U32(raw, 0);
    if (g_net_flags & 1) {
        if (!I32(PLAYER_INPUT(player), 0))
            return;
    } else if (player > 3) {
        return;
    }
    if (mask & 0x800)
        packet[2] ^= 8;
    if (mask & 0x100)
        packet[2] ^= 1;
    if (mask & 0x10)
        packet[3] ^= 0x10;
    if (mask & 0x40)
        packet[3] ^= 0x40;
    if (mask & 0x80)
        packet[3] ^= 0x80;
    if (mask & 0x20)
        packet[3] ^= 0x20;
    if (mask & 4)
        packet[3] ^= 8;
    if (mask & 8)
        packet[3] ^= 4;
    if (mask & 1)
        packet[3] ^= 2;
    if (mask & 2)
        packet[3] ^= 1;
    if (options & 0x80) {
        int32_t x = (int8_t)raw[5], y = (int8_t)raw[6];

        if (x < -0x7f)
            x = -0x7f;
        if (x > 0x7f)
            x = 0x7f;
        if (y < -0x7f)
            y = -0x7f;
        if (y > 0x7f)
            y = 0x7f;
        if (options & 4)
            packet[8] = (uint8_t)(x - 0x80);
        if ((options & 2) && y < 0)
            packet[9] = (uint8_t)(-y << 1);
        if ((options & 1) && y >= 0)
            packet[10] = (uint8_t)(y << 1);
    }
    if (mask & 0x1000)
        packet[2] ^= 0x10;
    if (mask & 0x4000)
        packet[2] ^= 0x40;
    if (mask & 0x8000)
        packet[2] ^= 0x80;
    if (mask & 0x2000)
        packet[2] ^= 0x20;
    *(uint16_t *)(packet + 4) = (uint16_t)(mask >> 16);
}

/* 0x45e8c0: this step's packet for a player: the local raw packet (from
 * 0x5d2f14), or in a network game the received one (the newest not after
 * the network clock when buffered); decoded into the player's packet at
 * +0x658. Nothing before the first steps. */
void GAMEAPI controls_fetch_packet(int32_t player, uint8_t *raw, uint32_t *packet)
{
    uint32_t now = g_net_time, first = (g_net_flags & 1) && !g_options[0x37] ? 2 : 1;
    uint8_t *input = PLAYER_INPUT(player), *decoded = input + 0x658;

    memset(raw, 0, 8);
    if (g_sim_steps >= first) {
        if (g_net_flags & 1) {
            if (I32(input, 0)) {
                if (g_net_flags & 0x80) {
                    uint32_t best = 0x40;
                    int32_t found = -1, i;

                    for (i = 0; i < 0x40; i++) {
                        uint32_t t = U32(input, 0x354 + i * 12);

                        if (t <= now && now - t <= best) {
                            found = i;
                            best = now - t;
                        }
                    }
                    if (found != -1) {
                        U32(raw, 0) = U32(input, 0x358 + found * 12);
                        U32(raw, 4) = U32(input, 0x35c + found * 12);
                    }
                } else {
                    U32(raw, 0) = U32(input, 0x2c);
                    U32(raw, 4) = U32(input, 0x30);
                }
            }
        } else if (player < 4) {
            U32(raw, 0) = U32(input, 0x244);
            U32(raw, 4) = U32(input, 0x248);
        }
    }
    controls_decode(player, raw, decoded);
    if ((g_net_flags & 1) && (g_net_flags & 0x80) && player != net_local_player())
        decoded[3] |= 0xf;
    *packet = (uint32_t)(uintptr_t)decoded; /* a 32-bit slot in game memory */
}

/* 0x428fe0: analogue steering, brake and throttle through the response
 * curve, by the slot's options. */
void GAMEAPI controls_analogue(int32_t player)
{
    const uint8_t *slot = g_car_slots + player * CAR_SLOT_SIZE;
    uint8_t *record = GP(slot) + CONTROLS;
    const uint8_t *packet = GP(record + 0x24);

    if (slot[0x26e] & 4)
        record[0] = (uint8_t)analogue_curve(packet[8], player, 0);
    if (slot[0x26e] & 2)
        record[2] = (uint8_t)analogue_curve(packet[10], player, 1);
    if (slot[0x26e] & 1)
        record[1] = (uint8_t)analogue_curve(packet[9], player, 2);
}

/* 0x41b520: read the player's packet into the car's control record (kinds
 * 9 and 10); AI cars (by the race's player kind, +0x650 -> +0x1e880) are
 * driven by 0x41ba20. Mirrored tracks (race +0x58b) swap left and right. */
int32_t GAMEAPI car_controls_read(uint8_t *car, int32_t player)
{
    uint8_t *record = car + CONTROLS, scratch[8], *packet;
    int32_t driver;

    if (record[0x14] == 9) {
        controls_fetch_packet(player, scratch, (uint32_t *)(record + 0x24));
        packet = GP(record + 0x24);
        record[0x15] = packet[1] != 'A';
        U32(record, 8) = ~((uint32_t)packet[2] * 0x100 + packet[3]);
        return 0;
    }
    if (record[0x14] == 10) {
        uint32_t buttons;

        controls_fetch_packet(player, scratch, (uint32_t *)(record + 0x24));
        packet = GP(record + 0x24);
        record[0x15] = packet[1] != 'A';
        U32(record, 8) = (~((uint32_t)packet[2] * 0x100 + packet[3]) & 0xffff) |
                         (uint32_t)*(const uint16_t *)(packet + 4) << 16;
        controls_analogue(player);
        if (g_race[0x58b]) {
            buttons = U32(record, 8);
            record[0] = (uint8_t)(0xff - record[0]);
            U32(record, 8) = buttons & ~(BUTTON_LEFT | BUTTON_RIGHT);
            if (buttons & BUTTON_LEFT)
                U32(record, 8) |= BUTTON_RIGHT;
            if (buttons & BUTTON_RIGHT)
                U32(record, 8) |= BUTTON_LEFT;
        }
    }
    driver = I32(GP(car + 0x650), 0x1e880);
    if (driver == 5 || driver == 6) {
        replay_step(car);
    } else {
        packet = GP(record + 0x24);
        if ((packet && packet[1] != '#') || record[0x14] == 8)
            controls_digital(g_car_slots + player * CAR_SLOT_SIZE, record);
        if (I32(g_race, 0x57c) == 2) {
            record[0x22] = 0xf;
            record[2] = g_brake_ramp[0xf];
        }
    }
    driver = I32(GP(car + 0x650), 0x1e880);
    if (driver == 2 || driver == 3 || driver == 7)
        replay_step(car);
    U32(record, 0xc) = ~(~U32(record, 8) | U32(record, 4));
    return 0;
}

/* 0x41b8a0: the handbrake (button 0x10) brakes against the speed. */
void GAMEAPI car_handbrake(uint8_t *car, const uint8_t *record)
{
    int32_t speed;

    if (!(record[8] & 0x10))
        return;
    speed = I32(car, 0x654) >> 16;
    if (speed < 5)
        speed = 5;
    I32(car, 0x640) = (int32_t)0xffff0000 / speed;
}

/* 0x41b7d0: the control record into steering (+0x634, a dead zone of 8 each
 * side of the centre), throttle (+0x63d) and brake (+0x63e); the throttle
 * drives +0x6a0, or +0x640 in some race kinds (race +0x57c >= 4). */
void GAMEAPI car_controls_apply(uint8_t *car)
{
    uint8_t *record = car + CONTROLS;
    int32_t steer;

    car[0x63c] = record[0];
    car[0x63d] = record[1];
    steer = car[0x63c] - 0x80;
    if (steer > 0)
        steer = steer < 8 ? 0 : steer - 8;
    else if (steer < 0)
        steer = steer > -8 ? 0 : steer + 8;
    I32(car, 0x634) = steer * 0x140;
    car[0x63e] = record[2] < 0x10 ? 0 : 0xff;
    if (car[0x63d] < 0x10)
        car[0x63d] = 0;
    if (car[0x63d] > 0xf0)
        car[0x63d] = 0xff;
    if (I32(g_race, 0x57c) >= 4) {
        I32(car, 0x640) = car[0x63d] << 8;
        car_handbrake(car, record);
        return;
    }
    I32(car, 0x6a0) = car[0x63d] << 8;
}
