/* Shadow verification of reimplemented code against the original, in game
 * (REROLL_VERIFY=1). See verify.c. */
#ifndef REROLL_VERIFY_H
#define REROLL_VERIFY_H

#include "runtime.h"
#include "hook.h"

typedef int32_t(GAMEAPI *verify_fn1)(int32_t arg);
typedef int32_t(GAMEAPI *verify_fn2)(int32_t a, int32_t b);

/* A verified entry point: every `sample`th call is checked; a summary is
 * logged every `report_every` checks. */
struct verify_point {
    const char *name;
    uint32_t sample, report_every;
    int log_coverage;
    uint32_t seen, calls, mismatched;
    int void_result; /* the function returns nothing: eax is not compared */
    uint32_t micros; /* time spent checking */
};
#define VERIFY_POINT(name, sample, report_every) {name, sample, report_every, 0, 0, 0, 0, 0, 0}
#define VERIFY_POINT_VOID(name, sample, report_every) {name, sample, report_every, 0, 0, 0, 0, 1, 0}

/* Reads REROLL_VERIFY and, if set, prepares the buffers and the sound hook.
 * `hooks`: the replacement hooks switched off while the original runs. */
void verify_init(const struct hook *hooks, int count);
int verify_enabled(void);
/* Logs every point checked so far (calls and mismatches); called at exit. */
void verify_summary(void);

/* Runs `original` (with the hooks off) and `ours` from the same game state,
 * logs every difference in game memory and in the result, then keeps the
 * original's state and returns its result. Without verification it just
 * calls `ours`. */
int32_t verify_call1(const char *name, verify_fn1 original, verify_fn1 ours, int32_t arg);
/* The same for a one- or two-argument (cdecl) function at a verify point. */
int32_t verify_call2(struct verify_point *point, verify_fn2 original, verify_fn2 ours, int32_t a, int32_t b);

#endif
