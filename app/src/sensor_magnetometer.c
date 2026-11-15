#include "sensor_magnetometer.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

/* Pinned BMM150 PM callback falls through RESUME to SUSPEND without triggers.
 * Manage power/conversion here; Zephyr still handles calibration/compensation.
 * Bosch BMM150 datasheet sections 5.5/6: forced measurements return to sleep.
 */
#if DT_NODE_HAS_STATUS(DT_ALIAS(magnetometer), okay)
static const struct i2c_dt_spec magnetometer = I2C_DT_SPEC_GET(DT_ALIAS(magnetometer));

int sensor_magnetometer_idle(void)
{
	return i2c_reg_update_byte_dt(&magnetometer, 0x4b, 0x01, 0x00);
}

static int wake(void)
{
	int result = i2c_reg_update_byte_dt(&magnetometer, 0x4b, 0x01, 0x01);
	if (result == 0) {
		k_sleep(K_MSEC(3));
	}
	return result;
}

int sensor_magnetometer_init(void)
{
	uint8_t id = 0;
	if (!i2c_is_ready_dt(&magnetometer)) {
		return -ENODEV;
	}
	int result = wake();
	if (result == 0) {
		result = i2c_reg_read_byte_dt(&magnetometer, 0x40, &id);
	}
	if (result == 0 && id != 0x32) {
		result = -ENODEV;
	}
	int stop_result = sensor_magnetometer_idle();
	return result != 0 ? result : stop_result;
}

int sensor_magnetometer_prepare(void)
{
	uint8_t raw[8];
	uint8_t status = 0;
	int result = wake();
	/* Low-power preset: three repetitions for XY and Z. */
	if (result == 0) {
		result = i2c_reg_write_byte_dt(&magnetometer, 0x51, 1);
	}
	if (result == 0) {
		result = i2c_reg_write_byte_dt(&magnetometer, 0x52, 2);
	}
	/* Consume old ready flag before this sample's forced conversion. */
	if (result == 0) {
		result = i2c_burst_read_dt(&magnetometer, 0x42, raw, sizeof(raw));
	}
	if (result == 0) {
		result = i2c_reg_update_byte_dt(&magnetometer, 0x4c, 0x06, 0x02);
	}
	if (result == 0) {
		for (unsigned int tries = 0; tries < 10; ++tries) {
			k_sleep(K_MSEC(2));
			result = i2c_reg_read_byte_dt(&magnetometer, 0x48, &status);
			if (result != 0 || (status & 0x01) != 0) {
				break;
			}
		}
		if (result == 0 && (status & 0x01) == 0) {
			result = -ETIMEDOUT;
		}
	}
	if (result == 0) {
		result = i2c_burst_read_dt(&magnetometer, 0x42, raw, sizeof(raw));
	}
	if (result == 0) {
		/* Reject saturation before the pinned driver overflows converting its sentinel.
		 * Forced mode holds this exact sample until the following Zephyr fetch.
		 */
		int16_t x = (int16_t)sys_get_le16(raw) >> 3;
		int16_t y = (int16_t)sys_get_le16(raw + 2) >> 3;
		int16_t z = (int16_t)sys_get_le16(raw + 4) >> 1;
		uint16_t hall = sys_get_le16(raw + 6) >> 2;
		if (x == -4096 || y == -4096 || z == -16384 || hall == 0) {
			return -ERANGE;
		}
	}
	return result;
}
#else
int sensor_magnetometer_init(void)
{
	return -ENODEV;
}

int sensor_magnetometer_prepare(void)
{
	return -ENODEV;
}

int sensor_magnetometer_idle(void)
{
	return -ENODEV;
}
#endif
