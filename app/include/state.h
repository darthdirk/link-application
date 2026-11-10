#ifndef LOGGER_STATE_H
#define LOGGER_STATE_H

#include <stdbool.h>

enum app_state { STATE_IDLE, STATE_LOGGING, STATE_STREAMING, STATE_BOTH };
enum state_event { STATE_START_LOG, STATE_STOP_LOG, STATE_START_STREAM, STATE_STOP_STREAM };

/* Commit the returned state only after the requested resource operation succeeds. */
enum app_state state_next(enum app_state current, enum state_event event);
bool state_logs(enum app_state state);
bool state_streams(enum app_state state);
const char *state_name(enum app_state state);

#endif
