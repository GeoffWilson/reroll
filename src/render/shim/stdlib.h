/* The few <stdlib.h> functions the renderer uses, for the freestanding
 * Windows builds (the DLL and tools/vkwintest.c): src/render/heap.c. */
#ifndef REROLL_SHIM_STDLIB_H
#define REROLL_SHIM_STDLIB_H

#include <stddef.h>

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *p, size_t size);
void free(void *p);

#endif
