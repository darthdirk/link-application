#ifndef LOGGER_SENSORS_H
#define LOGGER_SENSORS_H

#include <stdbool.h>
#include <stdint.h>

struct sensor_record;

/* Application thread only. Each read clears stale values and marks valid groups. */
int sensors_init(void);
int sensors_read(struct sensor_record *record);
uint32_t sensors_available_mask(void);
void sensors_set_active(bool active);

#endif
