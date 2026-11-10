#include "com.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/sys/atomic.h>

#include "record.h"

LOG_MODULE_REGISTER(com, LOG_LEVEL_DBG);

#define COM_LINE_SIZE 192
#define COM_QUEUE_DEPTH 16

struct com_line {
	char text[COM_LINE_SIZE];
};

static const struct device *const serial = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
static atomic_t dropped_lines;

K_MSGQ_DEFINE(output_queue, sizeof(struct com_line), COM_QUEUE_DEPTH, 4);

bool com_ready(void)
{
	uint32_t dtr = 0;

	return device_is_ready(serial) &&
	       uart_line_ctrl_get(serial, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr != 0;
}

static int enqueue(const char *line, k_timeout_t timeout)
{
	struct com_line output = {0};

	if (line == NULL) {
		return -EINVAL;
	}
	if (strlen(line) >= sizeof(output.text)) {
		atomic_inc(&dropped_lines);
		return -EMSGSIZE;
	}
	if (!com_ready()) {
		atomic_inc(&dropped_lines);
		return -ENOTCONN;
	}

	memcpy(output.text, line, strlen(line) + 1);
	if (k_msgq_put(&output_queue, &output, timeout) != 0) {
		atomic_inc(&dropped_lines);
		return -EAGAIN;
	}
	return 0;
}

static void output_thread(void *unused1, void *unused2, void *unused3)
{
	struct com_line output;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	for (;;) {
		(void)k_msgq_get(&output_queue, &output, K_FOREVER);
		const struct shell *shell = shell_backend_uart_get_ptr();

		if (shell != NULL && com_ready()) {
			/* Shell TX can wait on USB. Only this thread may wait here. */
			shell_print(shell, "%s", output.text);
		} else {
			atomic_inc(&dropped_lines);
		}
	}
}

K_THREAD_DEFINE(com_tx, 2048, output_thread, NULL, NULL, NULL, 8, 0, 0);

void com_init(void)
{
	/* Zephyr starts USB and the shell. Never wait here for a host connection. */
	if (!device_is_ready(serial)) {
		LOG_ERR("USB serial device is unavailable");
	}
}

void com_print(const char *fmt, ...)
{
	char line[COM_LINE_SIZE];
	va_list args;

	if (fmt == NULL) {
		return;
	}
	va_start(args, fmt);
	int length = vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	if (length < 0 || (size_t)length >= sizeof(line)) {
		atomic_inc(&dropped_lines);
		return;
	}
	(void)enqueue(line, K_NO_WAIT);
}

int com_emit(const char *line, void *context)
{
	ARG_UNUSED(context);
	return enqueue(line, K_MSEC(250));
}

int com_stream(const struct sensor_record *record)
{
	char line[COM_LINE_SIZE];
	int length = record_format(record, line, sizeof(line));

	if (length < 0) {
		return length;
	}
	return enqueue(line, K_NO_WAIT);
}

uint32_t com_dropped_count(void)
{
	return (uint32_t)atomic_get(&dropped_lines);
}
