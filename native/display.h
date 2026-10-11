#ifndef REROLL_NATIVE_DISPLAY_H
#define REROLL_NATIVE_DISPLAY_H

/* Display settings (native/user32.c; the game's Options > Display menu,
 * src/game/fe_l.c): fullscreen or a window, and a resolution in pixels
 * (0x0 = the desktop's). Saved in reroll_display.cfg. */
struct nt_display {
    int fullscreen;
    int w, h;
};
void nt_display_get(struct nt_display *d);
void nt_display_set(const struct nt_display *d);
int nt_display_modes(int *w, int *h, int max);
/* anisotropic filtering samples (1 = off), saved in the same file */
int nt_anisotropy_get(void);
void nt_anisotropy_set(int samples);

#endif
