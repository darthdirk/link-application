#ifndef LOGGER_SENSOR_MAGNETOMETER_H
#define LOGGER_SENSOR_MAGNETOMETER_H

int sensor_magnetometer_init(void);
/* Forced measurement before using Zephyr's compensated fetch/get. */
int sensor_magnetometer_prepare(void);
int sensor_magnetometer_idle(void);

#endif
