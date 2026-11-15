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

int record_format_environment(const struct sensor_record *record, char *buffer, size_t size)
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

_Static_assert(sizeof(RECORD_CSV_HEADER) <= RECORD_TEXT_SIZE, "CSV header exceeds line buffer");

struct csv_field {
	int32_t value;
	uint32_t valid_bit;
	unsigned int decimals;
};

/* Append exactly one column; empty columns explicitly represent missing readings. */
static int append_field(char *buffer, size_t size, size_t *used, const struct csv_field *field,
			uint32_t valid_mask)
{
	int length;
	char *next = buffer + *used;
	size_t remaining = size - *used;
	uint32_t value = magnitude(field->value);
	const char *sign = field->value < 0 ? "-" : "";

	if ((valid_mask & field->valid_bit) == 0U) {
		length = snprintf(next, remaining, ",");
	} else if (field->decimals == 3U) {
		length = snprintf(next, remaining, ",%s%" PRIu32 ".%03" PRIu32, sign, value / 1000U,
				  value % 1000U);
	} else if (field->decimals == 2U) {
		length = snprintf(next, remaining, ",%s%" PRIu32 ".%02" PRIu32, sign, value / 100U,
				  value % 100U);
	} else {
		length = snprintf(next, remaining, ",%" PRId32, field->value);
	}
	if (length < 0 || (size_t)length >= remaining) {
		buffer[0] = '\0';
		return length < 0 ? -EIO : -ENOSPC;
	}
	*used += (size_t)length;
	return 0;
}

int record_format(const struct sensor_record *record, char *buffer, size_t size)
{
	if (record == NULL || buffer == NULL || size == 0U) {
		return -EINVAL;
	}
	const struct csv_field fields[] = {
	    {record->temperature_mc, RECORD_VALID_EXTERNAL, 3U},
	    {record->humidity_mpct, RECORD_VALID_EXTERNAL, 3U},
	    {record->pressure_pa, RECORD_VALID_EXTERNAL, 2U},
	    {record->bme688_temperature_mc, RECORD_VALID_BME688, 3U},
	    {record->bme688_humidity_mpct, RECORD_VALID_BME688, 3U},
	    {record->bme688_pressure_pa, RECORD_VALID_BME688, 2U},
	    {record->bme688_gas_ohm, RECORD_VALID_GAS, 0U},
	    {record->bmi270_accel_mms2[0], RECORD_VALID_BMI270, 3U},
	    {record->bmi270_accel_mms2[1], RECORD_VALID_BMI270, 3U},
	    {record->bmi270_accel_mms2[2], RECORD_VALID_BMI270, 3U},
	    {record->bmi270_gyro_mrads[0], RECORD_VALID_BMI270, 3U},
	    {record->bmi270_gyro_mrads[1], RECORD_VALID_BMI270, 3U},
	    {record->bmi270_gyro_mrads[2], RECORD_VALID_BMI270, 3U},
	    {record->adxl362_accel_mms2[0], RECORD_VALID_ADXL362, 3U},
	    {record->adxl362_accel_mms2[1], RECORD_VALID_ADXL362, 3U},
	    {record->adxl362_accel_mms2[2], RECORD_VALID_ADXL362, 3U},
	    {record->adxl362_temperature_mc, RECORD_VALID_ADXL362, 3U},
	    {record->bmm150_milli_ut[0], RECORD_VALID_BMM150, 3U},
	    {record->bmm150_milli_ut[1], RECORD_VALID_BMM150, 3U},
	    {record->bmm150_milli_ut[2], RECORD_VALID_BMM150, 3U},
	    {record->bh1749_red, RECORD_VALID_BH1749, 0U},
	    {record->bh1749_green, RECORD_VALID_BH1749, 0U},
	    {record->bh1749_blue, RECORD_VALID_BH1749, 0U},
	    {record->bh1749_ir, RECORD_VALID_BH1749, 0U},
	    {record->battery_mv, RECORD_VALID_BATTERY, 3U},
	    {record->microphone_rms, RECORD_VALID_MICROPHONE, 0U},
	};
	int length =
	    snprintf(buffer, size, "%" PRId64 ",%" PRId64, record->uptime_ms, record->epoch_ms);
	if (length < 0 || (size_t)length >= size) {
		buffer[0] = '\0';
		return length < 0 ? -EIO : -ENOSPC;
	}
	size_t used = (size_t)length;
	for (size_t i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i) {
		int err = append_field(buffer, size, &used, &fields[i], record->valid_mask);
		if (err != 0) {
			return err;
		}
	}
	length = snprintf(buffer + used, size - used, ",%" PRIu32, record->valid_mask);
	if (length < 0 || (size_t)length >= size - used) {
		buffer[0] = '\0';
		return length < 0 ? -EIO : -ENOSPC;
	}
	return (int)(used + (size_t)length);
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
					 .pressure_pa = (int32_t)fields[4],
					 .valid_mask = RECORD_VALID_EXTERNAL};
	return 0;
}
