#ifndef REROLL_WIDESCREEN_H
#define REROLL_WIDESCREEN_H

/* Widescreen culling (REROLL_ASPECT): see widescreen.c. */
#include <stdint.h>

/* REROLL_ASPECT, else `output_aspect` (the Vulkan window's, 0 = none). */
void widescreen_install(float output_aspect);
/* the output window's aspect changed */
void widescreen_set_aspect(float output_aspect);


#endif
