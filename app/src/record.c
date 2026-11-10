#include "record.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

static uint32_t magnitude(int32_t value)
{
	/* Widen before negating so INT32_MIN is handled without signed overflow. */
	return value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
}

int record_format(const struct sensor_record *record, char *buffer, size_t size)
{
	if (record == NULL || buffer == NULL || size == 0U) {
		return -EINVAL;
	}
	uint32_t temperature = magnitude(record->temperature_mc);
	uint32_t humidity = magnitude(record->humidity_mpct);
	uint32_t pressure = magnitude(record->pressure_pa);

	/* Integer formatting preserves signed fractions and avoids float printf support. */
	int length =
	    snprintf(buffer, size,
		     "%" PRId64 ",%" PRId64 ",%s%" PRIu32 ".%03" PRIu32 ",%s%" PRIu32 ".%03" PRIu32
		     ",%s%" PRIu32 ".%02" PRIu32,
		     record->uptime_ms, record->epoch_ms, record->temperature_mc < 0 ? "-" : "",
		     temperature / 1000U, temperature % 1000U, record->humidity_mpct < 0 ? "-" : "",
		     humidity / 1000U, humidity % 1000U, record->pressure_pa < 0 ? "-" : "",
		     pressure / 100U, pressure % 100U);
	if (length < 0) {
		buffer[0] = '\0';
		return -EIO;
	}
	if ((size_t)length >= size) {
		buffer[0] = '\0';
		return -ENOSPC;
	}
	return length;
}

static int parse_integer(const char **cursor, int64_t minimum, int64_t maximum, int64_t *value)
{
	const char *text = *cursor;
	bool negative = *text == '-';
	uint64_t number = 0U;
	/* Adding one before negation avoids overflowing at INT64_MIN. */
	uint64_t limit = negative ? (uint64_t)(-(minimum + 1)) + 1U : (uint64_t)maximum;

	if (negative) {
		++text;
	}
	if (*text < '0' || *text > '9') {
		return -EBADMSG;
	}
	while (*text >= '0' && *text <= '9') {
		uint64_t digit = (uint64_t)(*text - '0');

		if (number > (limit - digit) / 10U) {
			return -ERANGE;
		}
		number = number * 10U + digit;
		++text;
	}
	if (negative) {
		*value = number == (uint64_t)INT64_MAX + 1U ? INT64_MIN : -(int64_t)number;
	} else {
		*value = (int64_t)number;
	}
	*cursor = text;
	return 0;
}

int record_parse_legacy(const char *line, struct sensor_record *record)
{
	int64_t fields[5];
	const char *cursor = line;

	if (line == NULL || record == NULL) {
		return -EINVAL;
	}
	for (size_t i = 0U; i < 5U; ++i) {
		int err = parse_integer(&cursor, i < 2U ? INT64_MIN : INT32_MIN,
					i < 2U ? INT64_MAX : INT32_MAX, &fields[i]);

		if (err != 0) {
			return err;
		}
		if (*cursor != (i < 4U ? ',' : '\0')) {
			return -EBADMSG;
		}
		if (i < 4U) {
			++cursor;
		}
	}
	*record = (struct sensor_record){.uptime_ms = fields[0],
					 .epoch_ms = fields[1],
					 .temperature_mc = (int32_t)fields[2],
					 .humidity_mpct = (int32_t)fields[3],
					 .pressure_pa = (int32_t)fields[4]};
	return 0;
}
