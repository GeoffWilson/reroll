#ifndef REROLL_LOG_H
#define REROLL_LOG_H

/* Writes reroll.log in the game's working directory.
 * Formats: %s %d %u %x %p and %%. */
void log_init(void);
void log_printf(const char *fmt, ...);

#endif
