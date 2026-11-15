#include "sensor_gas.h"

#include <errno.h>
#include <limits.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

/* sdk-zephyr's BME680 driver covers T/P/H but only the older gas-low variant.
 * Gas-high register mapping and resistance transfer function are described by
 * Bosch BME68x SensorAPI v4.4.8, read_field_data/calc_gas_resistance_high:
 * https://github.com/boschsensortec/BME68x_SensorAPI/tree/v4.4.8
 */
#if DT_NODE_HAS_STATUS(DT_ALIAS(onboard_environment), okay)
static const struct i2c_dt_spec gas = I2C_DT_SPEC_GET(DT_ALIAS(onboard_environment));
static uint8_t variant;
static uint8_t previous_index;
static uint8_t gas_bytes[2];
static bool initialized;
static bool completed;

static int wait_sleep(void)
{
	for (unsigned int tries = 0; tries < 31; ++tries) {
		uint8_t mode;
		int result = i2c_reg_read_byte_dt(&gas, 0x74, &mode);
		if (result != 0 || (mode & 0x03) == 0) {
			return result;
		}
		if (tries != 30) {
			k_sleep(K_MSEC(10));
		}
	}
	return -ETIMEDOUT;
}

int sensor_gas_init(void)
{
	initialized = false;
	completed = false;
	if (!i2c_is_ready_dt(&gas)) {
		return -ENODEV;
	}
	/* Driver initialization starts a forced conversion. Change gas mode in sleep. */
	int result = wait_sleep();
	if (result == 0) {
		result = i2c_reg_read_byte_dt(&gas, 0xf0, &variant);
	}
	if (result != 0) {
		return result;
	}
	if (variant > 1) {
		return -ENOTSUP;
	}
	initialized = true;
	return 0;
}

int sensor_gas_prepare(void)
{
	uint8_t field[17];
	completed = false;
	if (!initialized) {
		int result = sensor_gas_init();
		if (result != 0) {
			return result;
		}
	}
	int result = wait_sleep();
	if (result == 0) {
		/* Include gas-high at 0x2c/0x2d: older driver only reads through 0x2b. */
		result = i2c_burst_read_dt(&gas, 0x1d, field, sizeof(field));
	}
	if (result == 0) {
		previous_index = field[1];
		/* Preserve heater profile; run_gas=2 selects the gas-high variant. */
		result = i2c_reg_update_byte_dt(&gas, 0x71, 0x30, variant == 1 ? 0x20 : 0x10);
	}
	return result;
}

int sensor_gas_finish(void)
{
	uint8_t field[17];
	if (!initialized) {
		return -ENODEV;
	}
	/* Do not silently stamp an old driver sample with a new timestamp. */
	int result = i2c_burst_read_dt(&gas, 0x1d, field, sizeof(field));
	if (result != 0) {
		return result;
	}
	if (field[1] == previous_index || (field[0] & 0x60) != 0) {
		return -EAGAIN;
	}
	unsigned int offset = variant == 1 ? 15U : 13U;
	gas_bytes[0] = field[offset];
	gas_bytes[1] = field[offset + 1];
	completed = true;
	return 0;
}

int sensor_gas_read(int32_t *ohms)
{
	if (ohms == NULL) {
		return -EINVAL;
	}
	if (!completed) {
		return -EAGAIN;
	}
	/* Both gas-valid and heater-stable are required, even during startup. */
	if ((gas_bytes[1] & 0x30) != 0x30) {
		return -EAGAIN;
	}
	if (variant == 1) {
		uint32_t adc = ((uint32_t)gas_bytes[0] << 2) | (gas_bytes[1] >> 6);
		uint32_t range = gas_bytes[1] & 0x0f;
		uint32_t denominator = (uint32_t)(4096 + 3 * ((int32_t)adc - 512));
		/* 10-bit adc guarantees positive denominator (2560..5629). */
		uint64_t resistance =
		    (UINT64_C(10000) * (UINT32_C(262144) >> range) / denominator) * 100U;
		if (resistance > INT32_MAX) {
			return -ERANGE;
		}
		*ohms = (int32_t)resistance;
		return 0;
	}
	struct sensor_value value;
	const struct device *device = DEVICE_DT_GET(DT_ALIAS(onboard_environment));
	int result = sensor_channel_get(device, SENSOR_CHAN_GAS_RES, &value);
	if (result == 0) {
		*ohms = value.val1;
	}
	return result;
}
#else
int sensor_gas_init(void)
{
	return -ENODEV;
}

int sensor_gas_prepare(void)
{
	return -ENODEV;
}

int sensor_gas_finish(void)
{
	return -ENODEV;
}

int sensor_gas_read(int32_t *ohms)
{
	return ohms == NULL ? -EINVAL : -ENODEV;
}
#endif
