#include "sensor_color.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

/* BH1749NUC register map: RGB/IR raw counts, 1x gain, 120 ms integration.
 * https://www.rohm.com/products/sensors-mems/color-sensor-ics/bh1749nuc-product
 * A synchronous adapter avoids importing the sdk-nrf-only color driver.
 */
#if DT_NODE_HAS_STATUS(DT_ALIAS(color_sensor), okay)
static const struct i2c_dt_spec color = I2C_DT_SPEC_GET(DT_ALIAS(color_sensor));

int sensor_color_idle(void)
{
	return i2c_reg_write_byte_dt(&color, 0x42, 0x00);
}

int sensor_color_init(void)
{
	uint8_t manufacturer;
	uint8_t part;
	if (!i2c_is_ready_dt(&color)) {
		return -ENODEV;
	}
	int result = i2c_reg_read_byte_dt(&color, 0x92, &manufacturer);
	if (result == 0) {
		result = i2c_reg_read_byte_dt(&color, 0x40, &part);
	}
	if (result != 0) {
		return result;
	}
	if (manufacturer != 0xe0 || (part & 0x3f) != 0x0d) {
		return -ENODEV;
	}
	result = sensor_color_idle();
	if (result == 0) {
		result = i2c_reg_write_byte_dt(&color, 0x41, 0x2a);
	}
	return result;
}

int sensor_color_read(int32_t *red, int32_t *green, int32_t *blue, int32_t *infrared)
{
	uint8_t data[10];
	uint8_t status = 0;
	if (red == NULL || green == NULL || blue == NULL || infrared == NULL) {
		return -EINVAL;
	}
	int result = i2c_reg_write_byte_dt(&color, 0x42, 0x10);
	if (result == 0) {
		k_sleep(K_MSEC(120));
		/* Allow integration-clock tolerance, bounded to 200 ms. */
		for (unsigned int tries = 0; tries < 9; ++tries) {
			result = i2c_reg_read_byte_dt(&color, 0x42, &status);
			if (result != 0 || (status & 0x80) != 0) {
				break;
			}
			if (tries != 8) {
				k_sleep(K_MSEC(10));
			}
		}
		if (result == 0 && (status & 0x80) == 0) {
			result = -ETIMEDOUT;
		}
	}
	if (result == 0) {
		result = i2c_burst_read_dt(&color, 0x50, data, sizeof(data));
	}
	int stop_result = sensor_color_idle();
	if (result == 0) {
		result = stop_result;
	}
	if (result == 0) {
		*red = sys_get_le16(data);
		*green = sys_get_le16(data + 2);
		*blue = sys_get_le16(data + 4);
		/* 0x56/0x57 are reserved; this device has no clear-light channel. */
		*infrared = sys_get_le16(data + 8);
	}
	return result;
}
#else
int sensor_color_init(void)
{
	return -ENODEV;
}

int sensor_color_idle(void)
{
	return -ENODEV;
}

int sensor_color_read(int32_t *red, int32_t *green, int32_t *blue, int32_t *infrared)
{
	return red == NULL || green == NULL || blue == NULL || infrared == NULL ? -EINVAL : -ENODEV;
}
#endif
