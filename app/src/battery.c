#include "battery.h"

#include <errno.h>
#include <limits.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device_runtime.h>

static const struct device *const voltage = DEVICE_DT_GET_OR_NULL(DT_ALIAS(battery_voltage));

int battery_init(void)
{
	if (voltage == NULL || !device_is_ready(voltage)) {
		return -ENODEV;
	}
	return pm_device_runtime_enable(voltage);
}

int battery_read(int32_t *millivolts)
{
	struct sensor_value value;

	if (millivolts == NULL) {
		return -EINVAL;
	}
	if (voltage == NULL || !device_is_ready(voltage)) {
		return -ENODEV;
	}
	int err = pm_device_runtime_get(voltage);
	if (err != 0) {
		return err;
	}
	err = sensor_sample_fetch_chan(voltage, SENSOR_CHAN_VOLTAGE);
	if (err == 0) {
		err = sensor_channel_get(voltage, SENSOR_CHAN_VOLTAGE, &value);
	}
	int suspend_err = pm_device_runtime_put(voltage);
	if (err != 0 || suspend_err != 0) {
		return err != 0 ? err : suspend_err;
	}
	int64_t mv = sensor_value_to_micro(&value) / 1000;
	if (mv < 0 || mv > INT32_MAX) {
		return -ERANGE;
	}
	*millivolts = (int32_t)mv;
	return 0;
}
