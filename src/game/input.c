/* Per-frame input polling: replaces 0x489600 (input_poll) in Rollcage.exe.
 *
 * Reads every enumerated joystick into its DIJOYSTATE2 and the keyboard into
 * a 256-byte DIK_* table, as the original does. Joystick and keyboard state
 * is only refreshed while the game window is active; the keyboard table is
 * cleared every call regardless. */
#include "../runtime.h"
#include "../log.h"
#include "dinput.h"
#include "game.h"

/* Joystick records filled by the enumeration callback (0x488e80). */
struct joystick {
    uint8_t instance[0x244];   /* DIDEVICEINSTANCEA; product name at +0x12c */
    di_device *device;         /* +0x244 (0x4d75b8 for joystick 0) */
    struct di_devcaps caps;    /* +0x248 */
    uint8_t unknown[0xb8];     /* +0x274: effect objects and copies of caps */
    struct di_joystate2 state; /* +0x32c */
};
_Static_assert(sizeof(struct joystick) == 0x43c, "joystick record is 0x43c bytes");

#define g_dinput_result (*(int32_t *)0x4d6f2c)
#define g_keyboard (*(di_device **)0x4d6f34)
#define g_joysticks ((struct joystick *)0x4d7374)
#define g_joystick_count (*(int32_t *)0x4d9554)
#define g_keyboard_state ((uint8_t *)0x4d9558)
#define g_main_window (*(HANDLE *)0x6c8000)

/* Record the joysticks the game found, once, for troubleshooting. */
static void log_joysticks_once(void)
{
    static int done;
    int32_t i;

    if (done)
        return;
    done = 1;
    log_printf("input_poll: %d joystick(s)", g_joystick_count);
    for (i = 0; i < g_joystick_count; i++)
        log_printf("  joystick %d: '%s' caps flags %x, %u axes, %u buttons, %u povs", i,
                   (const char *)g_joysticks[i].instance + 0x12c, g_joysticks[i].caps.flags,
                   g_joysticks[i].caps.axes, g_joysticks[i].caps.buttons, g_joysticks[i].caps.povs);
}

void GAMEAPI input_poll(void)
{
    int32_t i;

    memset(g_keyboard_state, 0, 0x100);
    if (GetActiveWindow() != g_main_window)
        return;
    log_joysticks_once();

    for (i = 0; i < g_joystick_count; i++) {
        struct joystick *joy = &g_joysticks[i];
        struct di_devcaps caps;

        if (!joy->device)
            continue;
        memset(&caps, 0, sizeof(caps));
        caps.size = sizeof(caps);
        g_dinput_result = di_get_capabilities(joy->device, &caps);
        if (!(caps.flags & DIDC_ATTACHED)) {
            memset(&joy->state, 0, sizeof(joy->state));
            continue;
        }
        /* Deliberate change: the original read state only when Poll returned
         * DI_OK exactly. USB pads (and Wine) return DI_NOEFFECT, "no polling
         * needed", which is also success, so the original never read them. */
        g_dinput_result = di_poll(joy->device);
        if (g_dinput_result >= 0 ||
            ((g_dinput_result = di_acquire(joy->device)) == DI_OK &&
             (g_dinput_result = di_poll(joy->device)) >= 0))
            g_dinput_result = di_get_device_state(joy->device, sizeof(joy->state), &joy->state);
    }

    if (!g_keyboard) {
        memset(g_keyboard_state, 0, 0x100);
        return;
    }
    g_dinput_result = di_get_device_state(g_keyboard, 0x100, g_keyboard_state);
    if (g_dinput_result != DI_OK && (g_dinput_result = di_acquire(g_keyboard)) == DI_OK)
        g_dinput_result = di_get_device_state(g_keyboard, 0x100, g_keyboard_state);
}
