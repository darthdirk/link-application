#ifndef LOGGER_APP_H_
#define LOGGER_APP_H_

#include <stdint.h>

enum app_command_type {
	APP_STATUS,
	APP_LOG_START,
	APP_LOG_STOP,
	APP_LOG_FLUSH,
	APP_LOG_LIST,
	APP_LOG_DUMP,
	APP_STREAM_START,
	APP_STREAM_STOP,
	APP_TIME_GET,
	APP_TIME_SET,
	APP_CONFIG_SHOW,
	APP_CONFIG_LOG_RATE,
	APP_CONFIG_STREAM_RATE,
	APP_CONFIG_FLUSH_RATE,
	APP_CONFIG_SAVE,
	APP_STORAGE_FORMAT,
};

struct app_command {
	enum app_command_type type;
	uint32_t value;
	char text[48];
};

/* Copies the command; never waits for the application worker. */
int app_submit(const struct app_command *command);
void app_run(void);

#endif /* LOGGER_APP_H_ */
