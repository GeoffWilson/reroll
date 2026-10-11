#include "coverage.h"

uint32_t coverage[COVER_COUNT];

const char *const coverage_names[COVER_COUNT] = {
    "track bounce", "track push-out", "track slide", "track slide+spin", "track type 0x10",
    "track soak", "track other", "body alone", "body impulse", "body push-out", "body slide",
    "body weighted", "body stopped", "slow motion",
    "car settled", "car roll damped", "car self-right", "tyre hold", "tyre slide", "tyre sparks",
    "power slide", "slide recovery", "off track", "triangle contact", "wall contact",
    "duplicate contact", "edge rejected",
    "object point", "object edge", "object face", "object miss",
    "sweep hit", "sweep resting", "contact saved", "contact lost", "pair event",
    "track edge hit", "track fold", "track followed",
    "object face hit", "object edge hit", "object inside", "object corner hit",
    "spin around", "track special", "script path", "script turned",
    "blast car", "blast reaction", "blast other",
};
