/* Per-player control packet: replaces 0x428e10 (controls_packet) in Rollcage.exe.
 *
 * Returns 64 bits: the low half is the action mask from the player's bindings;
 * the high half holds four bytes: direction bits (keyboard arrows and stick,
 * for menus), then the raw X, Y and Z axis values of the player's joystick.
 * With the game's analogue option on, races steer from X and throttle from Y.
 *
 * Deliberate change: the triggers (Z, one combined DirectInput axis) replace
 * the stick's Y axis as the throttle, so analog throttle and brake come only
 * from the triggers and stick drift while steering cannot touch them. The
 * signs already agree: accelerate is negative Y (stick up) and the trigger
 * the default bindings treat as accelerate is negative Z. Bindings that use
 * the stick's Y axis (e.g. look behind on stick down) are unaffected.
 *
 * Deliberate change: the menu bits (read by the pause menu) also take the
 * pad's d-pad as directions and A / B (buttons 0 / 1) as select / back.
 * The original takes select and back only from the accelerate and pause
 * bindings, so with accelerate on a trigger A did nothing in the pause
 * menu. As in the original, select and back count only with no direction. */
#include "../runtime.h"
#include "game.h"
#include "gamefn.h"

/* Player records are 0x274 bytes; the joystick index is an s16 at 0x6bfdec. */
#define player_joystick(p) (*(int16_t *)GMEM(0x6bfdec + (p) * 0x274))

/* menu bits from a pad's d-pad (hat 0: code direction << 4) and A / B */
static uint8_t pad_nav_bits(int32_t pad)
{
    uint8_t bits = 0;

    if (pad < 0)
        return 0;
    if (GCALL(0x4897c0, joy_pov_dir)(pad, 0x00))
        bits |= 1;
    if (GCALL(0x4897c0, joy_pov_dir)(pad, 0x20))
        bits |= 2;
    if (GCALL(0x4897c0, joy_pov_dir)(pad, 0x30))
        bits |= 4;
    if (GCALL(0x4897c0, joy_pov_dir)(pad, 0x10))
        bits |= 8;
    if (bits == 0) {
        if (GCALL(0x489770, joy_button_down)(pad, 0))
            bits = 0x10;
        if (GCALL(0x489770, joy_button_down)(pad, 1))
            bits |= 0x20;
    }
    return bits;
}

uint64_t GAMEAPI controls_packet(int32_t player)
{
    uint32_t mask = GCALL(0x427b10, controls_read)(player, 0);
    uint8_t nav = (uint8_t)GCALL(0x428d20, keyboard_nav_bits)();
    int32_t x, y, z;

    nav |= (uint8_t)GCALL(0x428d70, controls_nav_bits)(mask, player);
    nav |= pad_nav_bits(player_joystick(player));
    x = GCALL(0x4898e0, joy_axis_x)(player_joystick(player));
    /* The original reads Y here; keep the call so the order of joystick
     * reads matches, but throttle comes from the triggers only. */
    (void)GCALL(0x4898f0, joy_axis_y)(player_joystick(player));
    z = GCALL(0x489900, joy_axis_z)(player_joystick(player));
    y = z;

    return (uint64_t)((uint32_t)nav | (uint32_t)(uint8_t)x << 8 | (uint32_t)(uint8_t)y << 16 |
                      (uint32_t)(uint8_t)z << 24)
               << 32 |
           mask;
}
