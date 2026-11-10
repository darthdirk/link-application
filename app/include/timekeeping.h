#ifndef LOGGER_TIMEKEEPING_H
#define LOGGER_TIMEKEEPING_H

#include <stddef.h>
#include <stdint.h>

/* All calls belong to the application thread. No hardware RTC is assumed. */
void timekeeping_reset(void);
/* UTC only: exact YYYY-MM-DDTHH:MM:SSZ, years 1970..9999. */
int timekeeping_set(const char *iso, int64_t uptime_ms);
/* Returns -1 when unset, before the anchor, or outside the supported range. */
int64_t timekeeping_epoch_ms(int64_t uptime_ms);
/* Formats UTC to whole seconds, or "unset" for -1. Returns 0 or negative errno. */
int timekeeping_format(int64_t epoch_ms, char *buffer, size_t size);

#endif
