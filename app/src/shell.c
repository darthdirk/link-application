#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/shell/shell.h>

#include "app.h"

static int submit(const struct shell *shell, enum app_command_type type, const char *text,
		  uint32_t value)
{
	struct app_command command = {.type = type, .value = value};

	if (text != NULL) {
		if (strlen(text) >= sizeof(command.text)) {
			shell_error(shell, "Argument is too long (maximum %u characters)",
				    (unsigned int)sizeof(command.text) - 1);
			return -ENAMETOOLONG;
		}
		memcpy(command.text, text, strlen(text) + 1);
	}

	int result = app_submit(&command);
	if (result != 0) {
		shell_error(shell, "Command queue busy (%d); retry", result);
	} else {
		shell_print(shell, "Queued");
	}
	return result;
}

static int cmd_status(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return submit(shell, APP_STATUS, NULL, 0);
}

static int cmd_log_start(const struct shell *shell, size_t argc, char **argv)
{
	return submit(shell, APP_LOG_START, argc == 2 ? argv[1] : "readings.csv", 0);
}

static int cmd_log_stop(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return submit(shell, APP_LOG_STOP, NULL, 0);
}

static int cmd_log_flush(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return submit(shell, APP_LOG_FLUSH, NULL, 0);
}

static int cmd_log_list(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return submit(shell, APP_LOG_LIST, NULL, 0);
}

static int cmd_log_dump(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return submit(shell, APP_LOG_DUMP, argv[1], 0);
}

static int cmd_stream_start(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return submit(shell, APP_STREAM_START, NULL, 0);
}

static int cmd_stream_stop(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return submit(shell, APP_STREAM_STOP, NULL, 0);
}

static int cmd_time_get(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return submit(shell, APP_TIME_GET, NULL, 0);
}

static int cmd_time_set(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return submit(shell, APP_TIME_SET, argv[1], 0);
}

static int cmd_config_show(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return submit(shell, APP_CONFIG_SHOW, NULL, 0);
}

static int parse_uint32(const char *text, uint32_t *value)
{
	uint32_t number = 0;

	if (text == NULL || value == NULL || text[0] == '\0') {
		return -EINVAL;
	}
	for (size_t i = 0; text[i] != '\0'; i++) {
		if (text[i] < '0' || text[i] > '9') {
			return -EINVAL;
		}
		uint32_t digit = (uint32_t)(text[i] - '0');
		if (number > (UINT32_MAX - digit) / 10U) {
			return -ERANGE;
		}
		number = number * 10U + digit;
	}
	*value = number;
	return 0;
}

static int cmd_config_set(const struct shell *shell, size_t argc, char **argv)
{
	enum app_command_type type;
	uint32_t value;

	ARG_UNUSED(argc);
	if (strcmp(argv[1], "log_rate_ms") == 0) {
		type = APP_CONFIG_LOG_RATE;
	} else if (strcmp(argv[1], "stream_rate_ms") == 0) {
		type = APP_CONFIG_STREAM_RATE;
	} else if (strcmp(argv[1], "flush_rate_ms") == 0) {
		type = APP_CONFIG_FLUSH_RATE;
	} else {
		shell_error(shell,
			    "Unknown setting; use log_rate_ms, stream_rate_ms, flush_rate_ms");
		return -EINVAL;
	}
	int result = parse_uint32(argv[2], &value);
	if (result != 0) {
		shell_error(shell, "Rate must be a decimal integer in milliseconds");
		return result;
	}
	return submit(shell, type, NULL, value);
}

static int cmd_config_save(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return submit(shell, APP_CONFIG_SAVE, NULL, 0);
}

static int cmd_storage_format(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	if (strcmp(argv[1], "CONFIRM") != 0) {
		shell_error(
		    shell,
		    "Formatting erases all logs and config.cfg. Use: storage format CONFIRM");
		return -EINVAL;
	}
	return submit(shell, APP_STORAGE_FORMAT, argv[1], 0);
}

static int debug_level(const char *text)
{
	static const char *const names[] = {"off", "err", "warn", "info", "debug"};

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		if (strcmp(text, names[i]) == 0) {
			return (int)i;
		}
	}
	return -EINVAL;
}

static int cmd_debug(const struct shell *shell, size_t argc, char **argv)
{
	if (shell->log_backend == NULL || shell->log_backend->backend == NULL) {
		shell_error(shell, "Shell diagnostic backend is unavailable");
		return -ENODEV;
	}
	const struct log_backend *backend = shell->log_backend->backend;

	if (argc == 2 && strcmp(argv[1], "list") == 0) {
		for (uint32_t i = 0; i < log_src_cnt_get(0) && i <= INT16_MAX; i++) {
			const char *name = log_source_name_get(0, i);
			if (name != NULL) {
				shell_print(shell,
					    "%s: level %u (0 off, 1 err, 2 warn, 3 info, 4 debug)",
					    name, log_filter_get(backend, 0, (int16_t)i, true));
			}
		}
		return 0;
	}
	if (argc != 3) {
		shell_error(shell,
			    "Use: debug list | debug <module|all> <off|err|warn|info|debug>");
		return -EINVAL;
	}
	int level = debug_level(argv[2]);
	if (level < 0) {
		shell_error(shell, "Unknown level; use off, err, warn, info, debug");
		return level;
	}

	if (strcmp(argv[1], "all") == 0) {
		for (uint32_t i = 0; i < log_src_cnt_get(0) && i <= INT16_MAX; i++) {
			(void)log_filter_set(NULL, 0, (int16_t)i, (uint32_t)level);
		}
		shell_print(shell, "All modules updated; compiled limits apply (debug list)");
		return 0;
	}
	int source = log_source_id_get(argv[1]);
	if (source < 0 || source > INT16_MAX) {
		shell_error(shell, "Unknown module; use: debug list");
		return -ENOENT;
	}
	uint32_t actual = log_filter_set(NULL, 0, (int16_t)source, (uint32_t)level);
	shell_print(shell, "%s: level %u (limited by each module's compiled level)", argv[1],
		    actual);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_log,
    SHELL_CMD_ARG(start, NULL, "Start/append CSV log: log start [FILE.csv]", cmd_log_start, 1, 1),
    SHELL_CMD_ARG(stop, NULL, "Flush and close the current log", cmd_log_stop, 1, 0),
    SHELL_CMD_ARG(flush, NULL, "Synchronize buffered records to flash", cmd_log_flush, 1, 0),
    SHELL_CMD_ARG(list, NULL, "List files (idle only)", cmd_log_list, 1, 0),
    SHELL_CMD_ARG(dump, NULL, "Display FILE.csv (idle only)", cmd_log_dump, 2, 0),
    SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_stream, SHELL_CMD_ARG(start, NULL, "Start CSV streaming", cmd_stream_start, 1, 0),
    SHELL_CMD_ARG(stop, NULL, "Stop streaming", cmd_stream_stop, 1, 0), SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_time, SHELL_CMD_ARG(get, NULL, "Display UTC time (unset after reset)", cmd_time_get, 1, 0),
    SHELL_CMD_ARG(set, NULL, "Set UTC: time set YYYY-MM-DDTHH:MM:SSZ", cmd_time_set, 2, 0),
    SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_config, SHELL_CMD_ARG(show, NULL, "Display current rates", cmd_config_show, 1, 0),
    SHELL_CMD_ARG(set, NULL, "Set <log_rate_ms|stream_rate_ms|flush_rate_ms> <milliseconds>",
		  cmd_config_set, 3, 0),
    SHELL_CMD_ARG(save, NULL, "Save rates to config.cfg", cmd_config_save, 1, 0),
    SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_storage,
			       SHELL_CMD_ARG(format, NULL,
					     "Erase filesystem (idle only): storage format CONFIRM",
					     cmd_storage_format, 2, 0),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_ARG_REGISTER(status, NULL, "Display application status", cmd_status, 1, 0);
SHELL_CMD_REGISTER(log, &sub_log, "Sensor record storage", NULL);
SHELL_CMD_REGISTER(stream, &sub_stream, "Live CSV readings", NULL);
SHELL_CMD_REGISTER(time, &sub_time, "UTC wall clock", NULL);
SHELL_CMD_REGISTER(config, &sub_config, "Sampling and flush rates", NULL);
SHELL_CMD_REGISTER(storage, &sub_storage, "Filesystem maintenance", NULL);
SHELL_CMD_ARG_REGISTER(debug, NULL, "debug list | debug <module|all> <off|err|warn|info|debug>",
		       cmd_debug, 2, 1);
