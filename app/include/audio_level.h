#ifndef LOGGER_AUDIO_LEVEL_H
#define LOGGER_AUDIO_LEVEL_H

#include <stddef.h>
#include <stdint.h>

/* Integer RMS of signed 16-bit PCM, including DC; not a calibrated SPL reading.
 * At most 65535 samples; result 0..32768. Output remains unchanged on error.
 */
int pcm_rms(const int16_t *samples, size_t count, int32_t *result);

#endif
