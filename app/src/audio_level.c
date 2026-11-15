#include "audio_level.h"

#include <errno.h>

int pcm_rms(const int16_t *samples, size_t count, int32_t *result)
{
	if (samples == NULL || result == NULL || count == 0 || count > UINT16_MAX) {
		return -EINVAL;
	}
	uint64_t sum = 0;
	for (size_t i = 0; i < count; ++i) {
		/* Widen before multiplication: INT16_MIN squared is representable. */
		int32_t value = samples[i];
		sum += (uint64_t)(value * value);
	}
	uint32_t mean = (uint32_t)(sum / count);
	uint32_t low = 0;
	uint32_t high = 32768;
	while (low < high) {
		uint32_t middle = low + (high - low + 1) / 2;
		if (middle * middle <= mean) {
			low = middle;
		} else {
			high = middle - 1;
		}
	}
	*result = (int32_t)low;
	return 0;
}
