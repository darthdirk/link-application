#ifndef LOGGER_BATTERY_H
#define LOGGER_BATTERY_H

#include <stdint.h>

/* Application thread only. The switched divider is off between readings. */
int battery_init(void);
/* Output is millivolts and is unchanged on error. */
int battery_read(int32_t *millivolts);

#endif
