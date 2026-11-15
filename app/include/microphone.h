#ifndef LOGGER_MICROPHONE_H
#define LOGGER_MICROPHONE_H

#include <stdint.h>

/* Application thread only. No sampling or microphone clock while idle. */
int microphone_init(void);
/* 100 ms PCM snapshot after 100 ms settling; RMS amplitude 0..32768, not dB SPL.
 * Stops PDM on success or error. Output remains unchanged on error.
 */
int microphone_read(int32_t *rms);

#endif
