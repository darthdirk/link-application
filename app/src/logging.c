#include "logging.h"

#include "record.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(logging, LOG_LEVEL_DBG);

#define LOG_MAX_BYTES (1024U * 1024U)
#define LOG_LINE_SIZE 128
#define LOG_PATH_SIZE (sizeof(LOG_MOUNT_POINT) + LOG_NAME_MAX)

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(littlefs);
static struct fs_mount_t mount = {
    .type = FS_LITTLEFS,
    .fs_data = &littlefs,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = LOG_MOUNT_POINT,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static struct fs_file_t file;
static bool mounted;
static bool opened;
static size_t file_bytes;
static int write_error;

/* A basename plus .csv keeps config files and paths outside /lfs inaccessible. */
static bool valid_name(const char *name)
{
	if (name == NULL) {
		return false;
	}

	size_t length = 0;
	while (length < LOG_NAME_MAX && name[length] != '\0') {
		++length;
	}
	if (length < 5 || length == LOG_NAME_MAX || strcmp(name + length - 4, ".csv") != 0) {
		return false;
	}
	for (size_t i = 0; i < length - 4; ++i) {
		char c = name[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		      c == '_' || c == '-')) {
			return false;
		}
	}
	return true;
}

static int make_path(const char *name, char path[LOG_PATH_SIZE])
{
	if (!valid_name(name)) {
		return -EINVAL;
	}
	int length = snprintk(path, LOG_PATH_SIZE, LOG_MOUNT_POINT "/%s", name);
	return length < 0 || (size_t)length >= LOG_PATH_SIZE ? -ENAMETOOLONG : 0;
}

static int write_all(struct fs_file_t *target, const char *data, size_t length)
{
	ssize_t written = fs_write(target, data, length);
	if (written < 0) {
		return (int)written;
	}
	return (size_t)written == length ? 0 : -EIO;
}

int logging_init(void)
{
	if (mounted) {
		return 0;
	}
	int result = fs_mount(&mount);
	mounted = result == 0;
	if (result != 0) {
		LOG_WRN("Storage mount failed: %d; no data erased", result);
	}
	return result;
}

bool logging_ready(void)
{
	return mounted;
}

int logging_format(void)
{
	if (opened) {
		return -EBUSY;
	}
	if (mounted) {
		int result = fs_unmount(&mount);
		if (result != 0) {
			return result;
		}
		mounted = false;
	}
	int result = fs_mkfs(FS_LITTLEFS, FIXED_PARTITION_ID(storage_partition), &littlefs, 0);
	return result == 0 ? logging_init() : result;
}

enum csv_format { CSV_UNKNOWN, CSV_READABLE, CSV_LEGACY };

static enum csv_format header_format(const char *header)
{
	if (strcmp(header, RECORD_CSV_HEADER) == 0) {
		return CSV_READABLE;
	}
	return strcmp(header, RECORD_LEGACY_CSV_HEADER) == 0 ? CSV_LEGACY : CSV_UNKNOWN;
}

/* Read bounded lines and normalize old integer CSVs without changing the input file. */
static int read_csv(struct fs_file_t *input, log_emit_fn emit, void *context)
{
	char chunk[LOG_LINE_SIZE];
	char line[LOG_LINE_SIZE];
	size_t used = 0;
	enum csv_format format = CSV_UNKNOWN;

	for (;;) {
		ssize_t count = fs_read(input, chunk, sizeof(chunk));
		if (count <= 0) {
			return count < 0 ? (int)count
					 : (used == 0 && format != CSV_UNKNOWN ? 0 : -EBADMSG);
		}
		for (ssize_t i = 0; i < count; ++i) {
			unsigned char c = (unsigned char)chunk[i];
			if (c != '\n') {
				if (c < ' ' || c > '~' || used == sizeof(line) - 1) {
					return -EBADMSG;
				}
				line[used++] = (char)c;
				continue;
			}
			line[used] = '\0';
			if (format == CSV_UNKNOWN) {
				format = header_format(line);
				if (format == CSV_UNKNOWN) {
					return -EBADMSG;
				}
				memcpy(line, RECORD_CSV_HEADER, sizeof(RECORD_CSV_HEADER));
			} else if (format == CSV_LEGACY) {
				struct sensor_record record;
				int result = record_parse_legacy(line, &record);
				if (result != 0) {
					return result;
				}
				result = record_format(&record, line, sizeof(line));
				if (result < 0) {
					return result;
				}
			}
			int result = emit(line, context);
			if (result != 0) {
				return result;
			}
			used = 0;
		}
	}
}

static int check_existing(bool *legacy)
{
	char header[LOG_LINE_SIZE];
	size_t used = 0;
	for (;;) {
		char c;
		ssize_t count = fs_read(&file, &c, 1);
		if (count != 1) {
			return count < 0 ? (int)count : -EBADMSG;
		}
		if (c == '\n') {
			break;
		}
		if ((unsigned char)c < ' ' || (unsigned char)c > '~' ||
		    used == sizeof(header) - 1) {
			return -EBADMSG;
		}
		header[used++] = c;
	}
	header[used] = '\0';
	enum csv_format format = header_format(header);
	if (format == CSV_UNKNOWN) {
		return -EBADMSG;
	}
	*legacy = format == CSV_LEGACY;
	int result = fs_seek(&file, -1, FS_SEEK_END);
	if (result != 0) {
		return result;
	}
	char last;
	ssize_t count = fs_read(&file, &last, 1);
	if (count < 0) {
		return (int)count;
	}
	/* Never join a new record to a torn final record after power loss. */
	if (count != 1 || last != '\n') {
		return -EBADMSG;
	}
	return fs_seek(&file, 0, FS_SEEK_END);
}

struct csv_writer {
	struct fs_file_t *target;
	size_t bytes;
};

static int write_csv_line(const char *line, void *context)
{
	struct csv_writer *writer = context;
	char output[LOG_LINE_SIZE];
	size_t length = strlen(line);
	if (length >= sizeof(output) || length + 1 > LOG_MAX_BYTES - writer->bytes) {
		return -EFBIG;
	}
	memcpy(output, line, length);
	output[length++] = '\n';
	int result = write_all(writer->target, output, length);
	if (result == 0) {
		writer->bytes += length;
	}
	return result;
}

/* The app thread owns all filesystem work. Replace only after a complete, synced copy. */
static int migrate_legacy(const char *path)
{
	static const char temporary[] = LOG_MOUNT_POINT "/log-convert.tmp";
	int result = fs_seek(&file, 0, FS_SEEK_SET);
	if (result != 0) {
		return result;
	}
	struct fs_file_t target;
	fs_file_t_init(&target);
	result = fs_open(&target, temporary, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (result != 0) {
		return result;
	}
	struct csv_writer writer = {.target = &target};
	result = read_csv(&file, write_csv_line, &writer);
	if (result == 0) {
		result = fs_sync(&target);
	}
	int close_result = fs_close(&target);
	if (result == 0) {
		result = close_result;
	}
	close_result = fs_close(&file);
	opened = false;
	if (result == 0) {
		result = close_result;
	}
	if (result == 0) {
		/* LittleFS atomically replaces an existing file; never unlink the original. */
		result = fs_rename(temporary, path);
	}
	if (result != 0) {
		(void)fs_unlink(temporary);
		return result;
	}
	file_bytes = writer.bytes;
	fs_file_t_init(&file);
	result = fs_open(&file, path, FS_O_READ | FS_O_WRITE);
	opened = result == 0;
	return result == 0 ? fs_seek(&file, 0, FS_SEEK_END) : result;
}

int logging_start(const char *name)
{
	char path[LOG_PATH_SIZE];
	int result = make_path(name, path);
	if (result != 0) {
		return result;
	}
	if (!mounted) {
		return -ENODEV;
	}
	if (opened) {
		return -EALREADY;
	}

	struct fs_dirent entry;
	result = fs_stat(path, &entry);
	if (result != 0 && result != -ENOENT) {
		return result;
	}
	bool exists = result == 0;
	if (exists && entry.type != FS_DIR_ENTRY_FILE) {
		return -EISDIR;
	}
	file_bytes = exists ? entry.size : 0;
	if (file_bytes >= LOG_MAX_BYTES) {
		return -EFBIG;
	}

	fs_file_t_init(&file);
	result = fs_open(&file, path, FS_O_CREATE | FS_O_READ | FS_O_WRITE);
	if (result != 0) {
		return result;
	}
	opened = true;
	write_error = 0;
	if (file_bytes != 0) {
		bool legacy = false;
		result = check_existing(&legacy);
		if (result == 0 && legacy) {
			result = migrate_legacy(path);
		}
	} else {
		static const char header[] = RECORD_CSV_HEADER "\n";
		result = write_all(&file, header, sizeof(header) - 1);
		if (result == 0) {
			file_bytes = sizeof(header) - 1;
			result = fs_sync(&file);
		}
	}
	if (result != 0 && opened) {
		int close_result = fs_close(&file);
		opened = false;
		if (close_result != 0) {
			LOG_ERR("Close after failed start: %d", close_result);
		}
	}
	return result;
}

int logging_append(const struct sensor_record *record)
{
	if (record == NULL) {
		return -EINVAL;
	}
	if (!opened) {
		return -EBADF;
	}
	if (write_error != 0) {
		return write_error;
	}
	char line[LOG_LINE_SIZE];
	int length = record_format(record, line, sizeof(line) - 1);
	if (length < 0) {
		return length;
	}
	line[length++] = '\n';
	if ((size_t)length > LOG_MAX_BYTES - file_bytes) {
		return -EFBIG;
	}
	int result = write_all(&file, line, (size_t)length);
	if (result == 0) {
		file_bytes += (size_t)length;
	} else {
		/* A short write may leave a partial CSV line. Require stop and a new file. */
		write_error = result;
	}
	return result;
}

int logging_flush(void)
{
	if (!opened) {
		return -EBADF;
	}
	int result = fs_sync(&file);
	if (write_error == 0) {
		write_error = result;
	}
	return write_error;
}

int logging_stop(void)
{
	if (!opened) {
		return 0;
	}
	int result = logging_flush();
	int close_result = fs_close(&file);
	/* The LittleFS adapter releases its handle even if close reports an error. */
	opened = false;
	return result != 0 ? result : close_result;
}

int logging_list(log_emit_fn emit, void *context)
{
	if (emit == NULL) {
		return -EINVAL;
	}
	if (!mounted) {
		return -ENODEV;
	}
	if (opened) {
		return -EBUSY;
	}
	struct fs_dir_t directory;
	struct fs_dirent entry;
	fs_dir_t_init(&directory);
	int result = fs_opendir(&directory, LOG_MOUNT_POINT);
	if (result != 0) {
		return result;
	}
	for (;;) {
		result = fs_readdir(&directory, &entry);
		if (result != 0 || entry.name[0] == '\0') {
			break;
		}
		if (entry.type == FS_DIR_ENTRY_FILE && valid_name(entry.name)) {
			char line[LOG_LINE_SIZE];
			int length =
			    snprintk(line, sizeof(line), "%s %zu bytes", entry.name, entry.size);
			result = length < 0 || (size_t)length >= sizeof(line) ? -EOVERFLOW
									      : emit(line, context);
			if (result != 0) {
				break;
			}
		}
	}
	int close_result = fs_closedir(&directory);
	return result != 0 ? result : close_result;
}

int logging_dump(const char *name, log_emit_fn emit, void *context)
{
	char path[LOG_PATH_SIZE];
	int result = make_path(name, path);
	if (result != 0 || emit == NULL) {
		return result != 0 ? result : -EINVAL;
	}
	if (!mounted) {
		return -ENODEV;
	}
	if (opened) {
		return -EBUSY;
	}
	struct fs_dirent entry;
	result = fs_stat(path, &entry);
	if (result != 0) {
		return result;
	}
	if (entry.type != FS_DIR_ENTRY_FILE || entry.size > LOG_MAX_BYTES) {
		return -EFBIG;
	}
	struct fs_file_t input;
	fs_file_t_init(&input);
	result = fs_open(&input, path, FS_O_READ);
	if (result != 0) {
		return result;
	}
	result = read_csv(&input, emit, context);
	int close_result = fs_close(&input);
	return result != 0 ? result : close_result;
}
