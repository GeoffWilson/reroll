/* Keyboard codes: SDL scancodes (USB usage) to the PC set-1 scancodes that
 * DirectInput's DIK_ values are (0x80 = the E0-prefixed keys), and to
 * Windows virtual-key codes. */
#include "keys.h"

static const struct nt_key table[] = {
    {SDL_SCANCODE_A, 0x1e, 'A'}, {SDL_SCANCODE_B, 0x30, 'B'}, {SDL_SCANCODE_C, 0x2e, 'C'},
    {SDL_SCANCODE_D, 0x20, 'D'}, {SDL_SCANCODE_E, 0x12, 'E'}, {SDL_SCANCODE_F, 0x21, 'F'},
    {SDL_SCANCODE_G, 0x22, 'G'}, {SDL_SCANCODE_H, 0x23, 'H'}, {SDL_SCANCODE_I, 0x17, 'I'},
    {SDL_SCANCODE_J, 0x24, 'J'}, {SDL_SCANCODE_K, 0x25, 'K'}, {SDL_SCANCODE_L, 0x26, 'L'},
    {SDL_SCANCODE_M, 0x32, 'M'}, {SDL_SCANCODE_N, 0x31, 'N'}, {SDL_SCANCODE_O, 0x18, 'O'},
    {SDL_SCANCODE_P, 0x19, 'P'}, {SDL_SCANCODE_Q, 0x10, 'Q'}, {SDL_SCANCODE_R, 0x13, 'R'},
    {SDL_SCANCODE_S, 0x1f, 'S'}, {SDL_SCANCODE_T, 0x14, 'T'}, {SDL_SCANCODE_U, 0x16, 'U'},
    {SDL_SCANCODE_V, 0x2f, 'V'}, {SDL_SCANCODE_W, 0x11, 'W'}, {SDL_SCANCODE_X, 0x2d, 'X'},
    {SDL_SCANCODE_Y, 0x15, 'Y'}, {SDL_SCANCODE_Z, 0x2c, 'Z'},
    {SDL_SCANCODE_1, 0x02, '1'}, {SDL_SCANCODE_2, 0x03, '2'}, {SDL_SCANCODE_3, 0x04, '3'},
    {SDL_SCANCODE_4, 0x05, '4'}, {SDL_SCANCODE_5, 0x06, '5'}, {SDL_SCANCODE_6, 0x07, '6'},
    {SDL_SCANCODE_7, 0x08, '7'}, {SDL_SCANCODE_8, 0x09, '8'}, {SDL_SCANCODE_9, 0x0a, '9'},
    {SDL_SCANCODE_0, 0x0b, '0'},
    {SDL_SCANCODE_RETURN, 0x1c, 0x0d}, {SDL_SCANCODE_ESCAPE, 0x01, 0x1b}, {SDL_SCANCODE_BACKSPACE, 0x0e, 0x08},
    {SDL_SCANCODE_TAB, 0x0f, 0x09}, {SDL_SCANCODE_SPACE, 0x39, 0x20}, {SDL_SCANCODE_MINUS, 0x0c, 0xbd},
    {SDL_SCANCODE_EQUALS, 0x0d, 0xbb}, {SDL_SCANCODE_LEFTBRACKET, 0x1a, 0xdb},
    {SDL_SCANCODE_RIGHTBRACKET, 0x1b, 0xdd}, {SDL_SCANCODE_BACKSLASH, 0x2b, 0xdc},
    {SDL_SCANCODE_SEMICOLON, 0x27, 0xba}, {SDL_SCANCODE_APOSTROPHE, 0x28, 0xde}, {SDL_SCANCODE_GRAVE, 0x29, 0xc0},
    {SDL_SCANCODE_COMMA, 0x33, 0xbc}, {SDL_SCANCODE_PERIOD, 0x34, 0xbe}, {SDL_SCANCODE_SLASH, 0x35, 0xbf},
    {SDL_SCANCODE_CAPSLOCK, 0x3a, 0x14},
    {SDL_SCANCODE_F1, 0x3b, 0x70}, {SDL_SCANCODE_F2, 0x3c, 0x71}, {SDL_SCANCODE_F3, 0x3d, 0x72},
    {SDL_SCANCODE_F4, 0x3e, 0x73}, {SDL_SCANCODE_F5, 0x3f, 0x74}, {SDL_SCANCODE_F6, 0x40, 0x75},
    {SDL_SCANCODE_F7, 0x41, 0x76}, {SDL_SCANCODE_F8, 0x42, 0x77}, {SDL_SCANCODE_F9, 0x43, 0x78},
    {SDL_SCANCODE_F10, 0x44, 0x79}, {SDL_SCANCODE_F11, 0x57, 0x7a}, {SDL_SCANCODE_F12, 0x58, 0x7b},
    {SDL_SCANCODE_PRINTSCREEN, 0xb7, 0x2c}, {SDL_SCANCODE_SCROLLLOCK, 0x46, 0x91}, {SDL_SCANCODE_PAUSE, 0xc5, 0x13},
    {SDL_SCANCODE_INSERT, 0xd2, 0x2d}, {SDL_SCANCODE_HOME, 0xc7, 0x24}, {SDL_SCANCODE_PAGEUP, 0xc9, 0x21},
    {SDL_SCANCODE_DELETE, 0xd3, 0x2e}, {SDL_SCANCODE_END, 0xcf, 0x23}, {SDL_SCANCODE_PAGEDOWN, 0xd1, 0x22},
    {SDL_SCANCODE_RIGHT, 0xcd, 0x27}, {SDL_SCANCODE_LEFT, 0xcb, 0x25}, {SDL_SCANCODE_DOWN, 0xd0, 0x28},
    {SDL_SCANCODE_UP, 0xc8, 0x26}, {SDL_SCANCODE_NUMLOCKCLEAR, 0x45, 0x90}, {SDL_SCANCODE_KP_DIVIDE, 0xb5, 0x6f},
    {SDL_SCANCODE_KP_MULTIPLY, 0x37, 0x6a}, {SDL_SCANCODE_KP_MINUS, 0x4a, 0x6d}, {SDL_SCANCODE_KP_PLUS, 0x4e, 0x6b},
    {SDL_SCANCODE_KP_ENTER, 0x9c, 0x0d}, {SDL_SCANCODE_KP_1, 0x4f, 0x61}, {SDL_SCANCODE_KP_2, 0x50, 0x62},
    {SDL_SCANCODE_KP_3, 0x51, 0x63}, {SDL_SCANCODE_KP_4, 0x4b, 0x64}, {SDL_SCANCODE_KP_5, 0x4c, 0x65},
    {SDL_SCANCODE_KP_6, 0x4d, 0x66}, {SDL_SCANCODE_KP_7, 0x47, 0x67}, {SDL_SCANCODE_KP_8, 0x48, 0x68},
    {SDL_SCANCODE_KP_9, 0x49, 0x69}, {SDL_SCANCODE_KP_0, 0x52, 0x60}, {SDL_SCANCODE_KP_PERIOD, 0x53, 0x6e},
    {SDL_SCANCODE_LCTRL, 0x1d, 0x11}, {SDL_SCANCODE_LSHIFT, 0x2a, 0x10}, {SDL_SCANCODE_LALT, 0x38, 0x12},
    {SDL_SCANCODE_LGUI, 0xdb, 0x5b}, {SDL_SCANCODE_RCTRL, 0x9d, 0x11}, {SDL_SCANCODE_RSHIFT, 0x36, 0x10},
    {SDL_SCANCODE_RALT, 0xb8, 0x12}, {SDL_SCANCODE_RGUI, 0xdc, 0x5c},
};

const struct nt_key *nt_key_from_sdl(SDL_Scancode s)
{
    size_t i;
    for (i = 0; i < sizeof table / sizeof *table; i++)
        if (table[i].sdl == s)
            return &table[i];
    return 0;
}

const struct nt_key *nt_key_from_vk(uint8_t vk)
{
    size_t i;
    for (i = 0; i < sizeof table / sizeof *table; i++)
        if (table[i].vk == vk)
            return &table[i];
    return 0;
}
