#ifndef LOGGER_RECORD_H
#define LOGGER_RECORD_H

#include <stddef.h>
#include <stdint.h>

#define RECORD_CSV_HEADER "uptime_ms,epoch_ms,temperature_C,humidity_pct,pressure_hPa"
#define RECORD_LEGACY_CSV_HEADER "uptime_ms,epoch_ms,temperature_mC,humidity_mpercent,pressure_Pa"
#define RECORD_STREAM_HEADER RECORD_CSV_HEADER
#define RECORD_TEXT_SIZE 112U

struct sensor_record {
	int64_t uptime_ms;
	/* -1 means UTC has not been set since boot. */
	int64_t epoch_ms;
	int32_t temperature_mc;
	int32_t humidity_mpct;
	int32_t pressure_pa;
};

/* CSV in degrees C, percent RH and hPa. Returns length or negative errno; no newline. */
int record_format(const struct sensor_record *record, char *buffer, size_t size);

/* Parse an old integer CSV row without a newline; output is unchanged on error. */
int record_parse_legacy(const char *line, struct sensor_record *record);

#endif
