#include "app.h"
#include "com.h"
#include "config.h"
#include "heartbeat.h"
#include "logging.h"
#include "record.h"
#include "sensors.h"
#include "state.h"
#include "timekeeping.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_DBG);
K_MSGQ_DEFINE(commands, sizeof(struct app_command), 8, 4);

/* This thread alone owns application state, the clock and all filesystem IO. */
static struct {
	enum app_state state;
	struct logger_config config;
	int64_t next_log;
	int64_t next_stream;
	int64_t next_flush;
	uint32_t sensor_errors;
	int last_error;
	bool sensor_ready;
} app;

int app_submit(const struct app_command *command)
{
	if (command == NULL || (unsigned int)command->type > APP_STORAGE_FORMAT ||
	    memchr(command->text, '\0', sizeof(command->text)) == NULL) {
		return -EINVAL;
	}
	return k_msgq_put(&commands, command, K_NO_WAIT);
}

static void stop_recording_on_error(int error)
{
	/* A storage failure must leave independent streaming running. */
	int close_error = logging_stop();

	app.state = state_next(app.state, STATE_STOP_LOG);
	app.last_error = error;
	LOG_ERR("Recording stopped: %d (close: %d)", error, close_error);
	com_print("Recording stopped: %d; state=%s", error, state_name(app.state));
}

static int change_rate(enum app_command_type type, uint32_t value)
{
	if (!config_rate_valid(value)) {
		return -ERANGE;
	}
	int64_t now = k_uptime_get();

	switch (type) {
	case APP_CONFIG_LOG_RATE:
		app.config.log_rate_ms = value;
		app.next_log = now + value;
		break;
	case APP_CONFIG_STREAM_RATE:
		app.config.stream_rate_ms = value;
		app.next_stream = now + value;
		break;
	case APP_CONFIG_FLUSH_RATE:
		app.config.flush_rate_ms = value;
		app.next_flush = now + value;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int handle_command(const struct app_command *command)
{
	int err;
	int64_t now = k_uptime_get();

	switch (command->type) {
	case APP_STATUS:
		com_print("state=%s sensor=%s storage=%s last_error=%d sensor_errors=%" PRIu32
			  " output_dropped=%" PRIu32,
			  state_name(app.state), app.sensor_ready ? "ready" : "unavailable",
			  logging_ready() ? "ready" : "unavailable", app.last_error,
			  app.sensor_errors, com_dropped_count());
		return 0;
	case APP_LOG_START:
		if (state_logs(app.state)) {
			return -EALREADY;
		}
		if (!app.sensor_ready) {
			return -ENODEV;
		}
		err = logging_start(command->text[0] ? command->text : "readings.csv");
		if (err != 0) {
			return err;
		}
		app.state = state_next(app.state, STATE_START_LOG);
		app.next_log =
		    state_streams(app.state) && app.config.log_rate_ms == app.config.stream_rate_ms
			? app.next_stream
			: now;
		app.next_flush = now + app.config.flush_rate_ms;
		return 0;
	case APP_LOG_STOP:
		if (!state_logs(app.state)) {
			return 0;
		}
		err = logging_stop();
		app.state = state_next(app.state, STATE_STOP_LOG);
		return err;
	case APP_LOG_FLUSH:
		err = logging_flush();
		if (err != 0 && state_logs(app.state)) {
			stop_recording_on_error(err);
		}
		return err;
	case APP_STREAM_START:
		if (!app.sensor_ready) {
			return -ENODEV;
		}
		if (state_streams(app.state)) {
			return -EALREADY;
		}
		app.state = state_next(app.state, STATE_START_STREAM);
		app.next_stream =
		    state_logs(app.state) && app.config.stream_rate_ms == app.config.log_rate_ms
			? app.next_log
			: now;
		com_print(RECORD_STREAM_HEADER);
		return 0;
	case APP_STREAM_STOP:
		app.state = state_next(app.state, STATE_STOP_STREAM);
		return 0;
	case APP_LOG_LIST:
	case APP_LOG_DUMP:
		if (app.state != STATE_IDLE) {
			return -EBUSY;
		}
		return command->type == APP_LOG_LIST ? logging_list(com_emit, NULL)
						     : logging_dump(command->text, com_emit, NULL);
	case APP_TIME_SET:
		return timekeeping_set(command->text, now);
	case APP_TIME_GET: {
		char timestamp[24];
		int64_t epoch_ms = timekeeping_epoch_ms(now);

		err = timekeeping_format(epoch_ms, timestamp, sizeof(timestamp));
		if (err == 0) {
			com_print("UTC=%s epoch_ms=%" PRId64 " uptime_ms=%" PRId64, timestamp,
				  epoch_ms, now);
		}
		return err;
	}
	case APP_CONFIG_SHOW:
		com_print(
		    "log_rate_ms=%" PRIu32 " stream_rate_ms=%" PRIu32 " flush_rate_ms=%" PRIu32,
		    app.config.log_rate_ms, app.config.stream_rate_ms, app.config.flush_rate_ms);
		return 0;
	case APP_CONFIG_LOG_RATE:
	case APP_CONFIG_STREAM_RATE:
	case APP_CONFIG_FLUSH_RATE:
		return change_rate(command->type, command->value);
	case APP_CONFIG_SAVE:
		return config_save(&app.config);
	case APP_STORAGE_FORMAT:
		if (app.state != STATE_IDLE || strcmp(command->text, "CONFIRM") != 0) {
			return -EPERM;
		}
		err = logging_format();
		if (err == 0) {
			config_defaults(&app.config);
		}
		return err;
	default:
		return -EINVAL;
	}
}

/* Preserve cadence during normal conversions; skip missed slots after a stall. */
static int64_t next_sample_deadline(int64_t previous, uint32_t period_ms, int64_t now)
{
	int64_t next = previous + period_ms;
	return next > now ? next : now + period_ms;
}

static void sample_if_due(int64_t now)
{
	bool log_due = state_logs(app.state) && now >= app.next_log;
	bool stream_due = state_streams(app.state) && now >= app.next_stream;

	if (log_due || stream_due) {
		struct sensor_record record = {0};
		int err = sensors_read(&record);
		int64_t acquired = k_uptime_get();

		/* No catch-up bursts after a slow peripheral operation. */
		if (log_due) {
			app.next_log =
			    next_sample_deadline(app.next_log, app.config.log_rate_ms, acquired);
		}
		if (stream_due) {
			app.next_stream = next_sample_deadline(app.next_stream,
							       app.config.stream_rate_ms, acquired);
		}
		if (err == 0) {
			record.uptime_ms = acquired;
			record.epoch_ms = timekeeping_epoch_ms(acquired);
			if (log_due) {
				err = logging_append(&record);
				if (err != 0) {
					stop_recording_on_error(err);
				}
			}
			if (stream_due) {
				/* Output loss is counted by com; recording remains independent. */
				(void)com_stream(&record);
			}
		} else {
			app.sensor_errors++;
			app.last_error = err;
			LOG_WRN("Sensor read failed: %d", err);
		}
	}
	if (state_logs(app.state) && k_uptime_get() >= app.next_flush) {
		int err = logging_flush();

		app.next_flush = k_uptime_get() + app.config.flush_rate_ms;
		if (err != 0) {
			stop_recording_on_error(err);
		}
	}
}

static k_timeout_t next_timeout(void)
{
	int64_t deadline = INT64_MAX;

	if (state_logs(app.state)) {
		deadline = MIN(app.next_log, app.next_flush);
	}
	if (state_streams(app.state)) {
		deadline = MIN(deadline, app.next_stream);
	}
	return deadline == INT64_MAX ? K_FOREVER : K_MSEC(MAX(0, deadline - k_uptime_get()));
}

void app_run(void)
{
	struct app_command command;
	int err;

	k_thread_name_set(k_current_get(), "application");
	com_init();
	err = heartbeat_init();
	if (err != 0) {
		LOG_WRN("Power LED unavailable: %d", err);
	}
	config_defaults(&app.config);
	err = sensors_init();
	app.sensor_ready = (err == 0);
	if (err != 0) {
		app.last_error = err;
		LOG_ERR("BME280 unavailable: %d", err);
	}
	err = logging_init();
	if (err == 0) {
		err = config_load(&app.config);
		if (err != 0 && err != -ENOENT) {
			LOG_WRN("config.cfg rejected (%d); using defaults", err);
		}
	} else {
		app.last_error = err;
		LOG_WRN("Storage not mounted (%d); format explicitly if new", err);
	}
	LOG_INF("Ready: idle, UTC unset; use time set, log start, stream start");
	for (;;) {
		if (k_msgq_get(&commands, &command, next_timeout()) == 0) {
			err = handle_command(&command);
			if (err != 0) {
				app.last_error = err;
				com_print("Command %u failed: %d", (unsigned int)command.type, err);
			} else {
				com_print("Done; state=%s", state_name(app.state));
			}
		}
		/* Check deadlines even under a continuous stream of CLI commands. */
		sample_if_due(k_uptime_get());
	}
}
