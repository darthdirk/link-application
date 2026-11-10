#include "sensors.h"

#include "record.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sensors, CONFIG_LOG_MAX_LEVEL);

static const struct device *const bme280 = DEVICE_DT_GET_OR_NULL(DT_ALIAS(environment_sensor));

int sensors_init(void)
{
	if (bme280 == NULL || !device_is_ready(bme280)) {
		LOG_ERR("External BME280 is not ready; check I2C wiring and address");
		return -ENODEV;
	}

	return 0;
}

static int read_milli(enum sensor_channel channel, int32_t *result)
{
	struct sensor_value value;
	int ret = sensor_channel_get(bme280, channel, &value);

	if (ret < 0) {
		return ret;
	}

	/* Zephyr reports deg C, percent RH and kPa, respectively. */
	int64_t milli = sensor_value_to_micro(&value) / 1000;

	if (milli < INT32_MIN || milli > INT32_MAX) {
		return -ERANGE;
	}

	*result = (int32_t)milli;
	return 0;
}

int sensors_read(struct sensor_record *record)
{
	int32_t temperature, humidity, pressure;
	int ret;

	if (record == NULL) {
		return -EINVAL;
	}
	if (bme280 == NULL || !device_is_ready(bme280)) {
		return -ENODEV;
	}

	/* Forced mode sleeps between reads. Fetch once for a coherent sample. */
	ret = sensor_sample_fetch(bme280);
	if (ret == 0) {
		ret = read_milli(SENSOR_CHAN_AMBIENT_TEMP, &temperature);
	}
	if (ret == 0) {
		ret = read_milli(SENSOR_CHAN_HUMIDITY, &humidity);
	}
	if (ret == 0) {
		ret = read_milli(SENSOR_CHAN_PRESS, &pressure);
	}
	if (ret < 0) {
		LOG_WRN("BME280 read failed: %d", ret);
		return ret;
	}

	record->temperature_mc = temperature;
	record->humidity_mpct = humidity;
	record->pressure_pa = pressure;
	return 0;
}
