#include "heartbeat.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(heartbeat, LOG_LEVEL_INF);

#define HEARTBEAT_PERIOD_MS 10000
#define HEARTBEAT_PULSE_MS 100

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(heartbeat_led), gpios, {0});
static bool initialized;
/* After initialization only the system workqueue changes the pulse state. */
static bool led_on;
static int64_t next_pulse_ms;

static void heartbeat_work(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(heartbeat, heartbeat_work);

static void stop_on_error(int error)
{
	int off_error = gpio_pin_set_dt(&led, 0);

	led_on = false;
	/* Do not reschedule after an error or continuously wake a faulty device. */
	LOG_ERR("Heartbeat stopped: %d (LED off: %d)", error, off_error);
}

static void heartbeat_work(struct k_work *work)
{
	ARG_UNUSED(work);
	bool turn_on = !led_on;
	int err = gpio_pin_set_dt(&led, turn_on);

	if (err != 0) {
		stop_on_error(err);
		return;
	}
	led_on = turn_on;
	int64_t now = k_uptime_get();
	if (turn_on) {
		next_pulse_ms = now + HEARTBEAT_PERIOD_MS;
	}
	/* A delayed workqueue skips missed pulses instead of producing a burst. */
	int64_t delay_ms = turn_on
			       ? HEARTBEAT_PULSE_MS
			       : (next_pulse_ms > now ? next_pulse_ms - now : HEARTBEAT_PERIOD_MS);

	err = k_work_reschedule(&heartbeat, K_MSEC(delay_ms));
	if (err < 0) {
		stop_on_error(err);
	}
}

int heartbeat_init(void)
{
	if (initialized) {
		return -EALREADY;
	}
	if (led.port == NULL || !gpio_is_ready_dt(&led)) {
		return -ENODEV;
	}
	int err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	if (err != 0) {
		return err;
	}
	err = k_work_reschedule(&heartbeat, K_MSEC(HEARTBEAT_PERIOD_MS));
	if (err < 0) {
		return err;
	}
	initialized = true;
	return 0;
}
