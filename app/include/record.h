#ifndef LOGGER_RECORD_H
#define LOGGER_RECORD_H

#include <stddef.h>
#include <stdint.h>

#define RECORD_ENVIRONMENT_HEADER "uptime_ms,epoch_ms,temperature_C,humidity_pct,pressure_hPa"
#define RECORD_CSV_HEADER                                                                          \
	RECORD_ENVIRONMENT_HEADER                                                                  \
	",bme688_temperature_C,bme688_humidity_pct,bme688_pressure_hPa,bme688_gas_ohm"             \
	",bmi270_accel_x_m_s2,bmi270_accel_y_m_s2,bmi270_accel_z_m_s2"                             \
	",bmi270_gyro_x_rad_s,bmi270_gyro_y_rad_s,bmi270_gyro_z_rad_s"                             \
	",adxl362_accel_x_m_s2,adxl362_accel_y_m_s2,adxl362_accel_z_m_s2,adxl362_temperature_C"    \
	",bmm150_x_uT,bmm150_y_uT,bmm150_z_uT"                                                     \
	",bh1749_red,bh1749_green,bh1749_blue,bh1749_ir,battery_V,microphone_rms,valid_mask"
#define RECORD_LEGACY_CSV_HEADER "uptime_ms,epoch_ms,temperature_mC,humidity_mpercent,pressure_Pa"
#define RECORD_STREAM_HEADER RECORD_CSV_HEADER
/* Includes the terminating NUL. Large enough for the header and every int32 field. */
#define RECORD_TEXT_SIZE 1024U

enum record_validity {
	RECORD_VALID_EXTERNAL = 1U << 0,
	RECORD_VALID_BME688 = 1U << 1,
	RECORD_VALID_GAS = 1U << 2,
	RECORD_VALID_BMI270 = 1U << 3,
	RECORD_VALID_ADXL362 = 1U << 4,
	RECORD_VALID_BMM150 = 1U << 5,
	RECORD_VALID_BH1749 = 1U << 6,
	RECORD_VALID_BATTERY = 1U << 7,
	RECORD_VALID_MICROPHONE = 1U << 8,
};

struct sensor_record {
	int64_t uptime_ms;
	/* -1 means UTC has not been set since boot. */
	int64_t epoch_ms;
	int32_t temperature_mc;
	int32_t humidity_mpct;
	int32_t pressure_pa;
	int32_t bme688_temperature_mc;
	int32_t bme688_humidity_mpct;
	int32_t bme688_pressure_pa;
	int32_t bme688_gas_ohm;
	int32_t bmi270_accel_mms2[3];
	int32_t bmi270_gyro_mrads[3];
	int32_t adxl362_accel_mms2[3];
	int32_t adxl362_temperature_mc;
	int32_t bmm150_milli_ut[3];
	int32_t bh1749_red;
	int32_t bh1749_green;
	int32_t bh1749_blue;
	int32_t bh1749_ir;
	int32_t battery_mv;
	int32_t microphone_rms;
	/* Only fields belonging to a set bit contain measurements from this sample. */
	uint32_t valid_mask;
};

/* Engineering-unit CSV; missing sensor groups are empty fields. No newline. */
int record_format(const struct sensor_record *record, char *buffer, size_t size);
/* Original five-column engineering-unit format, for dumping older files only. */
int record_format_environment(const struct sensor_record *record, char *buffer, size_t size);
/* Parse an old integer CSV row without a newline; output is unchanged on error. */
int record_parse_legacy(const char *line, struct sensor_record *record);

#endif
