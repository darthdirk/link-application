#include "config.h"

#include "logging.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/printk.h>

#define CONFIG_PATH LOG_MOUNT_POINT "/config.cfg"
#define CONFIG_TEMP_PATH LOG_MOUNT_POINT "/config.tmp"
#define CONFIG_TEXT_SIZE 192

void config_defaults(struct logger_config *config)
{
	if (config != NULL) {
		*config = (struct logger_config){
		    .log_rate_ms = 30000,
		    .stream_rate_ms = 30000,
		    .flush_rate_ms = 10000,
		};
	}
}

bool config_rate_valid(uint32_t rate_ms)
{
	return rate_ms >= 100 && rate_ms <= 3600000;
}

static int parse_rate(const char *text, uint32_t *value)
{
	if (*text == '\0') {
		return -EINVAL;
	}
	uint32_t parsed = 0;
	for (; *text != '\0'; ++text) {
		if (*text < '0' || *text > '9' || parsed > 360000) {
			return -EINVAL;
		}
		parsed = parsed * 10 + (uint32_t)(*text - '0');
	}
	if (!config_rate_valid(parsed)) {
		return -EINVAL;
	}
	*value = parsed;
	return 0;
}

static int parse_config(char *text, struct logger_config *config)
{
	unsigned int seen = 0;
	while (*text != '\0') {
		char *line = text;
		char *end = strchr(line, '\n');
		if (end != NULL) {
			*end = '\0';
			text = end + 1;
		} else {
			text += strlen(text);
		}
		size_t length = strlen(line);
		if (length != 0 && line[length - 1] == '\r') {
			line[--length] = '\0';
		}
		char *separator = strchr(line, '=');
		if (separator == NULL) {
			return -EINVAL;
		}
		*separator++ = '\0';
		uint32_t *destination;
		unsigned int bit;
		if (strcmp(line, "log_rate_ms") == 0) {
			destination = &config->log_rate_ms;
			bit = 1;
		} else if (strcmp(line, "stream_rate_ms") == 0) {
			destination = &config->stream_rate_ms;
			bit = 2;
		} else if (strcmp(line, "flush_rate_ms") == 0) {
			destination = &config->flush_rate_ms;
			bit = 4;
		} else {
			return -EINVAL;
		}
		if ((seen & bit) != 0 || parse_rate(separator, destination) != 0) {
			return -EINVAL;
		}
		seen |= bit;
	}
	return seen == 7 ? 0 : -EINVAL;
}

int config_load(struct logger_config *config)
{
	if (config == NULL) {
		return -EINVAL;
	}
	if (!logging_ready()) {
		return -ENODEV;
	}
	struct fs_file_t file;
	fs_file_t_init(&file);
	int result = fs_open(&file, CONFIG_PATH, FS_O_READ);
	if (result != 0) {
		return result;
	}
	char text[CONFIG_TEXT_SIZE];
	size_t used = 0;
	while (used < sizeof(text) - 1) {
		ssize_t count = fs_read(&file, text + used, sizeof(text) - 1 - used);
		if (count <= 0) {
			result = (int)count;
			break;
		}
		used += (size_t)count;
	}
	if (result == 0 && used == sizeof(text) - 1) {
		char extra;
		ssize_t count = fs_read(&file, &extra, 1);
		result = count < 0 ? (int)count : (count == 0 ? 0 : -EFBIG);
	}
	int close_result = fs_close(&file);
	if (result != 0 || close_result != 0) {
		return result != 0 ? result : close_result;
	}
	if (memchr(text, '\0', used) != NULL) {
		return -EINVAL;
	}
	text[used] = '\0';
	struct logger_config candidate = {0};
	result = parse_config(text, &candidate);
	if (result == 0) {
		*config = candidate;
	}
	return result;
}

int config_save(const struct logger_config *config)
{
	if (config == NULL || !config_rate_valid(config->log_rate_ms) ||
	    !config_rate_valid(config->stream_rate_ms) ||
	    !config_rate_valid(config->flush_rate_ms)) {
		return -EINVAL;
	}
	if (!logging_ready()) {
		return -ENODEV;
	}
	char text[CONFIG_TEXT_SIZE];
	int length = snprintk(text, sizeof(text),
			      "log_rate_ms=%" PRIu32 "\nstream_rate_ms=%" PRIu32
			      "\nflush_rate_ms=%" PRIu32 "\n",
			      config->log_rate_ms, config->stream_rate_ms, config->flush_rate_ms);
	if (length < 0 || (size_t)length >= sizeof(text)) {
		return -EOVERFLOW;
	}
	struct fs_file_t file;
	fs_file_t_init(&file);
	int result = fs_open(&file, CONFIG_TEMP_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (result != 0) {
		return result;
	}
	ssize_t written = fs_write(&file, text, (size_t)length);
	result = written < 0 ? (int)written : (written == length ? 0 : -EIO);
	if (result == 0) {
		result = fs_sync(&file);
	}
	int close_result = fs_close(&file);
	if (result == 0) {
		result = close_result;
	}
	if (result == 0) {
		/* LittleFS renames atomically; do not unlink the old config first. */
		result = fs_rename(CONFIG_TEMP_PATH, CONFIG_PATH);
	}
	if (result != 0) {
		(void)fs_unlink(CONFIG_TEMP_PATH);
	}
	return result;
}
