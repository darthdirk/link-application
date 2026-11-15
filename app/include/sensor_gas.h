#ifndef LOGGER_SENSOR_GAS_H
#define LOGGER_SENSOR_GAS_H

#include <stdint.h>

int sensor_gas_init(void);
/* Drain the previous complete field before Zephyr's BME680 sample_fetch. */
int sensor_gas_prepare(void);
/* Verify that fetch produced a new completed measurement, then cache its gas field. */
int sensor_gas_finish(void);
int sensor_gas_read(int32_t *ohms);

#endif
