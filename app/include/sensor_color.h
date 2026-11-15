#ifndef LOGGER_SENSOR_COLOR_H
#define LOGGER_SENSOR_COLOR_H

#include <stdint.h>

int sensor_color_init(void);
int sensor_color_read(int32_t *red, int32_t *green, int32_t *blue, int32_t *infrared);
int sensor_color_idle(void);

#endif
