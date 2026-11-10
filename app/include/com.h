#ifndef LOGGER_COM_H_
#define LOGGER_COM_H_

#include <stdbool.h>
#include <stdint.h>

struct sensor_record;

void com_init(void);
bool com_ready(void);
void com_print(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Enqueue a line with a bounded wait; negative errno means it was not accepted. */
int com_emit(const char *line, void *context);

/* Sampling never waits for the USB host. A full queue drops this stream record. */
int com_stream(const struct sensor_record *record);
uint32_t com_dropped_count(void);

#endif /* LOGGER_COM_H_ */
