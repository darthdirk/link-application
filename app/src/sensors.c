#include "sensors.h"

#include "battery.h"
#include "microphone.h"
#include "record.h"
#include "sensor_color.h"
#include "sensor_gas.h"
#include "sensor_magnetometer.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sensors, CONFIG_LOG_MAX_LEVEL);

static const struct device *const external = DEVICE_DT_GET_OR_NULL(DT_ALIAS(environment_sensor));
static const struct device *const environment =
    DEVICE_DT_GET_OR_NULL(DT_ALIAS(onboard_environment));
static const struct device *const imu = DEVICE_DT_GET_OR_NULL(DT_ALIAS(imu));
static const struct device *const accelerometer = DEVICE_DT_GET_OR_NULL(DT_ALIAS(low_power_accel));
static const struct device *const magnetometer = DEVICE_DT_GET_OR_NULL(DT_ALIAS(magnetometer));
static uint32_t available;

static bool ready(const struct device *device)
{
	return device != NULL && device_is_ready(device);
}

static int set_imu_frequency(int32_t frequency)
{
	struct sensor_value value = {.val1 = frequency};
	int result =
	    sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &value);
	/* Always attempt both channels, including shutdown after a partial failure. */
	int gyro_result =
	    sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &value);
	return result != 0 ? result : gyro_result;
}

static int initialize_imu(void)
{
	/* This pinned driver's range attributes use g and degrees/second. */
	struct sensor_value value = {.val1 = 2};
	int result = set_imu_frequency(0);
	if (result == 0) {
		result =
		    sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &value);
	}

	if (result == 0) {
		value.val1 = 250;
		result = sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE, &value);
	}
	if (result == 0) {
		value.val1 = 1;
		result =
		    sensor_attr_set(imu, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_OVERSAMPLING, &value);
	}
	if (result == 0) {
		result =
		    sensor_attr_set(imu, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_OVERSAMPLING, &value);
	}
	int stop_result = set_imu_frequency(0);
	return result != 0 ? result : stop_result;
}

int sensors_init(void)
{
	available = 0;
	if (ready(external)) {
		available |= RECORD_VALID_EXTERNAL;
	} else {
		LOG_WRN("External BME280 unavailable; check I2C wiring/address");
	}
	if (ready(environment)) {
		available |= RECORD_VALID_BME688;
		if (sensor_gas_init() == 0) {
			available |= RECORD_VALID_GAS;
		}
	}
	if (ready(imu) && initialize_imu() == 0) {
		available |= RECORD_VALID_BMI270;
	}
	if (ready(accelerometer)) {
		available |= RECORD_VALID_ADXL362;
	}
	if (ready(magnetometer) && sensor_magnetometer_init() == 0) {
		available |= RECORD_VALID_BMM150;
	}
	if (sensor_color_init() == 0) {
		available |= RECORD_VALID_BH1749;
	}
	if (battery_init() == 0) {
		available |= RECORD_VALID_BATTERY;
	}
	if (microphone_init() == 0) {
		available |= RECORD_VALID_MICROPHONE;
	}
	LOG_INF("Available sensor mask: 0x%03x", (unsigned int)available);
	return available != 0 ? 0 : -ENODEV;
}

uint32_t sensors_available_mask(void)
{
	return available;
}

void sensors_set_active(bool active)
{
	/* Acquisitions already stop BMI270/BH1749/BMM150. Retry cleanup at idle. */
	if (!active) {
		if (ready(imu)) {
			int result = set_imu_frequency(0);
			if (result != 0) {
				LOG_WRN("BMI270 idle failed: %d", result);
			}
		}
		if ((available & RECORD_VALID_BMM150) != 0) {
			(void)sensor_magnetometer_idle();
		}
		if ((available & RECORD_VALID_BH1749) != 0) {
			(void)sensor_color_idle();
		}
	}
}

static int read_scaled(const struct device *device, enum sensor_channel channel, int32_t divisor,
		       int32_t *result)
{
	struct sensor_value value;
	int error = sensor_channel_get(device, channel, &value);

	if (error != 0) {
		return error;
	}
	int64_t scaled = sensor_value_to_micro(&value) / divisor;
	if (scaled < INT32_MIN || scaled > INT32_MAX) {
		return -ERANGE;
	}
	*result = (int32_t)scaled;
	return 0;
}

static int read_environment(const struct device *device, int32_t *temperature, int32_t *humidity,
			    int32_t *pressure)
{
	int result = sensor_sample_fetch(device);

	/* Zephyr uses degrees C, percent RH and kPa. Keep fixed-point values in RAM. */
	if (result == 0) {
		result = read_scaled(device, SENSOR_CHAN_AMBIENT_TEMP, 1000, temperature);
	}
	if (result == 0) {
		result = read_scaled(device, SENSOR_CHAN_HUMIDITY, 1000, humidity);
	}
	if (result == 0) {
		result = read_scaled(device, SENSOR_CHAN_PRESS, 1000, pressure);
	}
	return result;
}

static int read_vector(const struct device *device, enum sensor_channel first, int32_t divisor,
		       int32_t values[3])
{
	for (size_t axis = 0; axis < 3; ++axis) {
		int result = read_scaled(device, (enum sensor_channel)(first + axis), divisor,
					 &values[axis]);
		if (result != 0) {
			return result;
		}
	}
	return 0;
}

static int read_imu(struct sensor_record *record)
{
	int result = set_imu_frequency(25);
	if (result == 0) {
		/* Allow gyro startup plus two 25 Hz periods; sleeping yields the CPU. */
		k_sleep(K_MSEC(200));
		result = sensor_sample_fetch(imu);
	}
	if (result == 0) {
		result = read_vector(imu, SENSOR_CHAN_ACCEL_X, 1000, record->bmi270_accel_mms2);
	}
	if (result == 0) {
		result = read_vector(imu, SENSOR_CHAN_GYRO_X, 1000, record->bmi270_gyro_mrads);
	}
	int stop_result = set_imu_frequency(0);
	return result != 0 ? result : stop_result;
}

static void mark_result(struct sensor_record *record, uint32_t mask, int result, const char *name)
{
	if (result == 0) {
		record->valid_mask |= mask;
	} else {
		LOG_WRN("%s read failed: %d", name, result);
	}
}

int sensors_read(struct sensor_record *record)
{
	if (record == NULL) {
		return -EINVAL;
	}
	/* Never reuse values from an earlier sample, even when one device fails. */
	*record = (struct sensor_record){.epoch_ms = -1};
	if ((available & RECORD_VALID_EXTERNAL) != 0) {
		mark_result(record, RECORD_VALID_EXTERNAL,
			    read_environment(external, &record->temperature_mc,
					     &record->humidity_mpct, &record->pressure_pa),
			    "BME280");
	}
	if ((available & RECORD_VALID_BME688) != 0) {
		int result = sensor_gas_prepare();
		if (result == 0) {
			result = read_environment(environment, &record->bme688_temperature_mc,
						  &record->bme688_humidity_mpct,
						  &record->bme688_pressure_pa);
		}
		if (result == 0) {
			result = sensor_gas_finish();
		}
		mark_result(record, RECORD_VALID_BME688, result, "BME688");
		if (result == 0 && (available & RECORD_VALID_GAS) != 0) {
			mark_result(record, RECORD_VALID_GAS,
				    sensor_gas_read(&record->bme688_gas_ohm), "BME688 gas");
		}
	}
	if ((available & RECORD_VALID_BMI270) != 0) {
		mark_result(record, RECORD_VALID_BMI270, read_imu(record), "BMI270");
	}
	if ((available & RECORD_VALID_ADXL362) != 0) {
		int result = sensor_sample_fetch(accelerometer);
		if (result == 0) {
			result = read_vector(accelerometer, SENSOR_CHAN_ACCEL_X, 1000,
					     record->adxl362_accel_mms2);
		}
		if (result == 0) {
			result = read_scaled(accelerometer, SENSOR_CHAN_DIE_TEMP, 1000,
					     &record->adxl362_temperature_mc);
		}
		mark_result(record, RECORD_VALID_ADXL362, result, "ADXL362");
	}
	if ((available & RECORD_VALID_BMM150) != 0) {
		int result = sensor_magnetometer_prepare();
		if (result == 0) {
			result = sensor_sample_fetch(magnetometer);
		}
		if (result == 0) {
			/* Zephyr reports gauss; 1 gauss = 100 uT, so microgauss / 10 = milli-uT. */
			result = read_vector(magnetometer, SENSOR_CHAN_MAGN_X, 10,
					     record->bmm150_milli_ut);
		}
		int stop_result = sensor_magnetometer_idle();
		mark_result(record, RECORD_VALID_BMM150, result != 0 ? result : stop_result,
			    "BMM150");
	}
	if ((available & RECORD_VALID_BH1749) != 0) {
		mark_result(record, RECORD_VALID_BH1749,
			    sensor_color_read(&record->bh1749_red, &record->bh1749_green,
					      &record->bh1749_blue, &record->bh1749_ir),
			    "BH1749");
	}
	if ((available & RECORD_VALID_BATTERY) != 0) {
		mark_result(record, RECORD_VALID_BATTERY, battery_read(&record->battery_mv),
			    "Battery");
	}
	if ((available & RECORD_VALID_MICROPHONE) != 0) {
		mark_result(record, RECORD_VALID_MICROPHONE,
			    microphone_read(&record->microphone_rms), "Microphone");
	}
	return record->valid_mask != 0 ? 0 : -EIO;
}
