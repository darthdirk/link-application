#include "timekeeping.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MS_PER_SECOND INT64_C(1000)
#define SECONDS_PER_DAY INT64_C(86400)
#define MAX_EPOCH_MS INT64_C(253402300799999)

static const unsigned int month_days[] = {31U, 28U, 31U, 30U, 31U, 30U,
					  31U, 31U, 30U, 31U, 30U, 31U};
static bool clock_set;
static int64_t anchor_epoch_ms;
static int64_t anchor_uptime_ms;

static bool leap_year(unsigned int year)
{
	return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

static unsigned int days_in_month(unsigned int year, unsigned int month)
{
	return month_days[month - 1U] + ((month == 2U && leap_year(year)) ? 1U : 0U);
}

static unsigned int digits(const char *text, size_t count)
{
	unsigned int value = 0U;

	for (size_t i = 0U; i < count; ++i) {
		value = value * 10U + (unsigned int)(text[i] - '0');
	}
	return value;
}

static int validate_syntax(const char *iso)
{
	if (iso == NULL) {
		return -EINVAL;
	}
	for (size_t i = 0U; i < 20U; ++i) {
		char expected = '\0';

		switch (i) {
		case 4U:
		case 7U:
			expected = '-';
			break;
		case 10U:
			expected = 'T';
			break;
		case 13U:
		case 16U:
			expected = ':';
			break;
		case 19U:
			expected = 'Z';
			break;
		default:
			break;
		}
		if (expected != '\0' ? iso[i] != expected : (iso[i] < '0' || iso[i] > '9')) {
			return -EINVAL;
		}
	}
	return iso[20] == '\0' ? 0 : -EINVAL;
}

void timekeeping_reset(void)
{
	clock_set = false;
	anchor_epoch_ms = 0;
	anchor_uptime_ms = 0;
}

int timekeeping_set(const char *iso, int64_t uptime_ms)
{
	unsigned int year;
	unsigned int month;
	unsigned int day;
	unsigned int hour;
	unsigned int minute;
	unsigned int second;
	int64_t days = 0;

	if (uptime_ms < 0 || validate_syntax(iso) != 0) {
		return -EINVAL;
	}
	year = digits(iso, 4U);
	month = digits(iso + 5, 2U);
	day = digits(iso + 8, 2U);
	hour = digits(iso + 11, 2U);
	minute = digits(iso + 14, 2U);
	second = digits(iso + 17, 2U);
	if (year < 1970U || month < 1U || month > 12U || day < 1U ||
	    day > days_in_month(year, month) || hour > 23U || minute > 59U || second > 59U) {
		return -EINVAL;
	}
	for (unsigned int y = 1970U; y < year; ++y) {
		days += leap_year(y) ? 366 : 365;
	}
	for (unsigned int m = 1U; m < month; ++m) {
		days += days_in_month(year, m);
	}
	days += day - 1U;
	anchor_epoch_ms =
	    (days * SECONDS_PER_DAY + hour * 3600U + minute * 60U + second) * MS_PER_SECOND;
	anchor_uptime_ms = uptime_ms;
	clock_set = true;
	return 0;
}

int64_t timekeeping_epoch_ms(int64_t uptime_ms)
{
	int64_t elapsed;

	if (!clock_set || uptime_ms < anchor_uptime_ms) {
		return -1;
	}
	elapsed = uptime_ms - anchor_uptime_ms;
	if (elapsed > MAX_EPOCH_MS - anchor_epoch_ms) {
		return -1;
	}
	return anchor_epoch_ms + elapsed;
}

int timekeeping_format(int64_t epoch_ms, char *buffer, size_t size)
{
	unsigned int year = 1970U;
	unsigned int month = 1U;
	int64_t seconds;
	unsigned int days;
	unsigned int remainder;
	int length;

	if (buffer == NULL || size == 0U) {
		return -EINVAL;
	}
	buffer[0] = '\0';
	if (epoch_ms == -1) {
		if (size < sizeof("unset")) {
			return -ENOSPC;
		}
		memcpy(buffer, "unset", sizeof("unset"));
		return 0;
	}
	if (epoch_ms < 0 || epoch_ms > MAX_EPOCH_MS) {
		return -EINVAL;
	}
	if (size < sizeof("1970-01-01T00:00:00Z")) {
		return -ENOSPC;
	}
	seconds = epoch_ms / MS_PER_SECOND;
	days = (unsigned int)(seconds / SECONDS_PER_DAY);
	remainder = (unsigned int)(seconds % SECONDS_PER_DAY);
	while (days >= (leap_year(year) ? 366U : 365U)) {
		days -= leap_year(year) ? 366U : 365U;
		++year;
	}
	while (days >= days_in_month(year, month)) {
		days -= days_in_month(year, month);
		++month;
	}
	length = snprintf(buffer, size, "%04u-%02u-%02uT%02u:%02u:%02uZ", year, month, days + 1U,
			  remainder / 3600U, (remainder % 3600U) / 60U, remainder % 60U);
	if (length != 20) {
		buffer[0] = '\0';
		return -EIO;
	}
	return 0;
}
