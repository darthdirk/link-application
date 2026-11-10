#ifndef LOGGER_LOGGING_H
#define LOGGER_LOGGING_H

#include <stdbool.h>

struct sensor_record;

#define LOG_NAME_MAX 48
#define LOG_MOUNT_POINT "/lfs"

typedef int (*log_emit_fn)(const char *line, void *context);

/* All calls belong to the application thread. Mounting never formats flash. */
int logging_init(void);
bool logging_ready(void);
int logging_format(void);
int logging_start(const char *name);
int logging_append(const struct sensor_record *record);
int logging_flush(void);
/* Always attempts close, even if the final sync fails. */
int logging_stop(void);
int logging_list(log_emit_fn emit, void *context);
int logging_dump(const char *name, log_emit_fn emit, void *context);

#endif
