#ifndef LOGGER_SENSORS_H
#define LOGGER_SENSORS_H

struct sensor_record;

/* Application thread only; failed reads leave the record unchanged. */
int sensors_init(void);
int sensors_read(struct sensor_record *record);

#endif
