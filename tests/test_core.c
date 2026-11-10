#include "record.h"
#include "state.h"
#include "timekeeping.h"
#include "unity.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

void setUp(void)
{
	timekeeping_reset();
}

void tearDown(void)
{
}

static void test_clock_starts_unset_and_reset_clears_it(void)
{
	char text[21];

	TEST_ASSERT_EQUAL_INT64(-1, timekeeping_epoch_ms(5000));
	TEST_ASSERT_EQUAL_INT(0, timekeeping_format(-1, text, sizeof(text)));
	TEST_ASSERT_EQUAL_STRING("unset", text);
	TEST_ASSERT_EQUAL_INT(0, timekeeping_set("2021-01-01T00:00:00Z", 100));
	timekeeping_reset();
	TEST_ASSERT_EQUAL_INT64(-1, timekeeping_epoch_ms(100));
}

static void test_clock_tracks_monotonic_milliseconds(void)
{
	TEST_ASSERT_EQUAL_INT(0, timekeeping_set("2021-01-01T00:00:00Z", 1500));
	TEST_ASSERT_EQUAL_INT64(INT64_C(1609459200000), timekeeping_epoch_ms(1500));
	TEST_ASSERT_EQUAL_INT64(INT64_C(1609459205432), timekeeping_epoch_ms(6932));
	TEST_ASSERT_EQUAL_INT64(-1, timekeeping_epoch_ms(1499));
}

static void test_epoch_and_dates_after_2038(void)
{
	char text[21];

	TEST_ASSERT_EQUAL_INT(0, timekeeping_set("1970-01-01T00:00:00Z", 0));
	TEST_ASSERT_EQUAL_INT64(0, timekeeping_epoch_ms(0));
	TEST_ASSERT_EQUAL_INT(0, timekeeping_set("2038-01-19T03:14:08Z", 0));
	TEST_ASSERT_EQUAL_INT64(INT64_C(2147483648000), timekeeping_epoch_ms(0));
	TEST_ASSERT_EQUAL_INT(0, timekeeping_format(timekeeping_epoch_ms(0), text, sizeof(text)));
	TEST_ASSERT_EQUAL_STRING("2038-01-19T03:14:08Z", text);
}

static void test_gregorian_leap_year_rules(void)
{
	char text[21];

	TEST_ASSERT_EQUAL_INT(0, timekeeping_set("2000-02-29T00:00:00Z", 0));
	TEST_ASSERT_EQUAL_INT64(INT64_C(951782400000), timekeeping_epoch_ms(0));
	TEST_ASSERT_EQUAL_INT(-EINVAL, timekeeping_set("2100-02-29T00:00:00Z", 0));
	TEST_ASSERT_EQUAL_INT(0, timekeeping_set("2100-02-28T23:59:59Z", 0));
	TEST_ASSERT_EQUAL_INT(0,
			      timekeeping_format(timekeeping_epoch_ms(1000), text, sizeof(text)));
	TEST_ASSERT_EQUAL_STRING("2100-03-01T00:00:00Z", text);
	TEST_ASSERT_EQUAL_INT(0, timekeeping_set("2024-02-28T23:59:59Z", 0));
	TEST_ASSERT_EQUAL_INT(0,
			      timekeeping_format(timekeeping_epoch_ms(2000), text, sizeof(text)));
	TEST_ASSERT_EQUAL_STRING("2024-02-29T00:00:01Z", text);
}

static void test_invalid_dates_preserve_clock(void)
{
	static const char *const invalid[] = {
	    "1969-12-31T23:59:59Z",
	    "2024-00-01T00:00:00Z",
	    "2024-13-01T00:00:00Z",
	    "2024-01-00T00:00:00Z",
	    "2024-04-31T00:00:00Z",
	    "2023-02-29T00:00:00Z",
	    "2024-01-01T24:00:00Z",
	    "2024-01-01T00:60:00Z",
	    "2024-01-01T00:00:60Z",
	    "2024-01-01 00:00:00Z",
	    "2024-01-01T00:00:00",
	    "2024-01-01T00:00:00Zx",
	    "202x-01-01T00:00:00Z",
	    "",
	    "2024-01",
	};

	TEST_ASSERT_EQUAL_INT(0, timekeeping_set("1970-01-01T00:00:00Z", 0));
	for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		TEST_ASSERT_EQUAL_INT_MESSAGE(-EINVAL, timekeeping_set(invalid[i], 0), invalid[i]);
		TEST_ASSERT_EQUAL_INT64(100, timekeeping_epoch_ms(100));
	}
}

static void test_clock_rejects_nulls_and_bad_output_buffers(void)
{
	char text[21];

	TEST_ASSERT_EQUAL_INT(-EINVAL, timekeeping_set(NULL, 0));
	TEST_ASSERT_EQUAL_INT(-EINVAL, timekeeping_set("2024-01-01T00:00:00Z", -1));
	TEST_ASSERT_EQUAL_INT(-EINVAL, timekeeping_format(0, NULL, sizeof(text)));
	TEST_ASSERT_EQUAL_INT(-EINVAL, timekeeping_format(0, text, 0));
	TEST_ASSERT_EQUAL_INT(-ENOSPC, timekeeping_format(0, text, 20));
	TEST_ASSERT_EQUAL_STRING("", text);
	TEST_ASSERT_EQUAL_INT(-ENOSPC, timekeeping_format(-1, text, 5));
	TEST_ASSERT_EQUAL_INT(-EINVAL, timekeeping_format(-2, text, sizeof(text)));
}

static void test_clock_upper_range_and_overflow(void)
{
	char text[21];

	TEST_ASSERT_EQUAL_INT(0, timekeeping_set("9999-12-31T23:59:59Z", 0));
	TEST_ASSERT_EQUAL_INT64(INT64_C(253402300799999), timekeeping_epoch_ms(999));
	TEST_ASSERT_EQUAL_INT64(-1, timekeeping_epoch_ms(1000));
	TEST_ASSERT_EQUAL_INT64(-1, timekeeping_epoch_ms(INT64_MAX));
	TEST_ASSERT_EQUAL_INT(0, timekeeping_format(INT64_C(253402300799999), text, sizeof(text)));
	TEST_ASSERT_EQUAL_STRING("9999-12-31T23:59:59Z", text);
	TEST_ASSERT_EQUAL_INT(-EINVAL,
			      timekeeping_format(INT64_C(253402300800000), text, sizeof(text)));
	TEST_ASSERT_EQUAL_INT(0, timekeeping_set("1970-01-01T00:00:00Z", INT64_MAX - 10));
	TEST_ASSERT_EQUAL_INT64(10, timekeeping_epoch_ms(INT64_MAX));
}

static void test_start_stop_controls_are_independent(void)
{
	enum app_state current = STATE_IDLE;

	current = state_next(current, STATE_START_LOG);
	TEST_ASSERT_EQUAL(STATE_LOGGING, current);
	current = state_next(current, STATE_START_STREAM);
	TEST_ASSERT_EQUAL(STATE_BOTH, current);
	current = state_next(current, STATE_STOP_LOG);
	TEST_ASSERT_EQUAL(STATE_STREAMING, current);
	current = state_next(current, STATE_STOP_STREAM);
	TEST_ASSERT_EQUAL(STATE_IDLE, current);
	current = state_next(current, STATE_START_STREAM);
	TEST_ASSERT_EQUAL(STATE_STREAMING, current);
	current = state_next(current, STATE_START_LOG);
	TEST_ASSERT_EQUAL(STATE_BOTH, current);
	current = state_next(current, STATE_STOP_STREAM);
	TEST_ASSERT_EQUAL(STATE_LOGGING, current);
	current = state_next(current, STATE_STOP_LOG);
	TEST_ASSERT_EQUAL(STATE_IDLE, current);
}

static void test_repeated_controls_are_idempotent(void)
{
	TEST_ASSERT_EQUAL(STATE_IDLE, state_next(STATE_IDLE, STATE_STOP_LOG));
	TEST_ASSERT_EQUAL(STATE_IDLE, state_next(STATE_IDLE, STATE_STOP_STREAM));
	TEST_ASSERT_EQUAL(STATE_LOGGING, state_next(STATE_LOGGING, STATE_START_LOG));
	TEST_ASSERT_EQUAL(STATE_LOGGING, state_next(STATE_LOGGING, STATE_STOP_STREAM));
	TEST_ASSERT_EQUAL(STATE_STREAMING, state_next(STATE_STREAMING, STATE_START_STREAM));
	TEST_ASSERT_EQUAL(STATE_STREAMING, state_next(STATE_STREAMING, STATE_STOP_LOG));
	TEST_ASSERT_EQUAL(STATE_BOTH, state_next(STATE_BOTH, STATE_START_LOG));
	TEST_ASSERT_EQUAL(STATE_BOTH, state_next(STATE_BOTH, STATE_START_STREAM));
}

static void test_state_flags_names_and_invalid_inputs(void)
{
	TEST_ASSERT_FALSE(state_logs(STATE_IDLE));
	TEST_ASSERT_FALSE(state_streams(STATE_IDLE));
	TEST_ASSERT_TRUE(state_logs(STATE_LOGGING));
	TEST_ASSERT_FALSE(state_streams(STATE_LOGGING));
	TEST_ASSERT_FALSE(state_logs(STATE_STREAMING));
	TEST_ASSERT_TRUE(state_streams(STATE_STREAMING));
	TEST_ASSERT_TRUE(state_logs(STATE_BOTH));
	TEST_ASSERT_TRUE(state_streams(STATE_BOTH));
	TEST_ASSERT_EQUAL_STRING("idle", state_name(STATE_IDLE));
	TEST_ASSERT_EQUAL_STRING("logging+streaming", state_name(STATE_BOTH));
	TEST_ASSERT_EQUAL_STRING("invalid", state_name((enum app_state) - 1));
	TEST_ASSERT_EQUAL(STATE_IDLE, state_next((enum app_state) - 1, STATE_START_LOG));
	TEST_ASSERT_EQUAL(STATE_BOTH, state_next(STATE_BOTH, (enum state_event)99));
}

static void test_csv_prints_engineering_units(void)
{
	struct sensor_record record = {.uptime_ms = 1650947,
				       .epoch_ms = INT64_C(1794330891263),
				       .temperature_mc = 24230,
				       .humidity_mpct = 17985,
				       .pressure_pa = 84484};
	char text[RECORD_TEXT_SIZE];

	TEST_ASSERT_GREATER_THAN_INT(0, record_format(&record, text, sizeof(text)));
	TEST_ASSERT_EQUAL_STRING("1650947,1794330891263,24.230,17.985,844.84", text);
	record = (struct sensor_record){
	    .epoch_ms = -1, .temperature_mc = -1, .humidity_mpct = 1, .pressure_pa = 1};
	TEST_ASSERT_GREATER_THAN_INT(0, record_format(&record, text, sizeof(text)));
	TEST_ASSERT_EQUAL_STRING("0,-1,-0.001,0.001,0.01", text);

	record = (struct sensor_record){.uptime_ms = INT64_MIN,
					.epoch_ms = INT64_MAX,
					.temperature_mc = INT32_MIN,
					.humidity_mpct = INT32_MAX,
					.pressure_pa = INT32_MIN};
	TEST_ASSERT_GREATER_THAN_INT(0, record_format(&record, text, sizeof(text)));
	TEST_ASSERT_EQUAL_STRING("-9223372036854775808,9223372036854775807,-2147483.648,"
				 "2147483.647,-21474836.48",
				 text);
}

static void test_csv_checks_nulls_and_buffer_bounds(void)
{
	const struct sensor_record record = {0};
	char text[sizeof("0,0,0.000,0.000,0.00")];

	TEST_ASSERT_EQUAL_INT(sizeof(text) - 1, record_format(&record, text, sizeof(text)));
	TEST_ASSERT_EQUAL_STRING("0,0,0.000,0.000,0.00", text);
	TEST_ASSERT_EQUAL_INT(-ENOSPC, record_format(&record, text, sizeof(text) - 1U));
	TEST_ASSERT_EQUAL_STRING("", text);
	TEST_ASSERT_EQUAL_INT(-EINVAL, record_format(NULL, text, sizeof(text)));
	TEST_ASSERT_EQUAL_INT(-EINVAL, record_format(&record, NULL, sizeof(text)));
	TEST_ASSERT_EQUAL_INT(-EINVAL, record_format(&record, text, 0));
}

static void test_legacy_csv_converts_saved_reading(void)
{
	struct sensor_record record;
	char text[RECORD_TEXT_SIZE];

	TEST_ASSERT_EQUAL_INT(
	    0, record_parse_legacy("113280,1794331567132,24550,20603,84477", &record));
	TEST_ASSERT_EQUAL_INT64(113280, record.uptime_ms);
	TEST_ASSERT_EQUAL_INT64(INT64_C(1794331567132), record.epoch_ms);
	TEST_ASSERT_EQUAL_INT32(24550, record.temperature_mc);
	TEST_ASSERT_EQUAL_INT32(20603, record.humidity_mpct);
	TEST_ASSERT_EQUAL_INT32(84477, record.pressure_pa);
	TEST_ASSERT_GREATER_THAN_INT(0, record_format(&record, text, sizeof(text)));
	TEST_ASSERT_EQUAL_STRING("113280,1794331567132,24.550,20.603,844.77", text);
}

static void test_legacy_csv_accepts_integer_boundaries(void)
{
	struct sensor_record record;

	TEST_ASSERT_EQUAL_INT(
	    0, record_parse_legacy(
		   "-9223372036854775808,9223372036854775807,-2147483648,2147483647,-2147483648",
		   &record));
	TEST_ASSERT_EQUAL_INT64(INT64_MIN, record.uptime_ms);
	TEST_ASSERT_EQUAL_INT64(INT64_MAX, record.epoch_ms);
	TEST_ASSERT_EQUAL_INT32(INT32_MIN, record.temperature_mc);
	TEST_ASSERT_EQUAL_INT32(INT32_MAX, record.humidity_mpct);
	TEST_ASSERT_EQUAL_INT32(INT32_MIN, record.pressure_pa);
	TEST_ASSERT_EQUAL_INT(
	    0, record_parse_legacy(
		   "9223372036854775807,-9223372036854775808,2147483647,-2147483648,2147483647",
		   &record));
	TEST_ASSERT_EQUAL_INT64(INT64_MAX, record.uptime_ms);
	TEST_ASSERT_EQUAL_INT64(INT64_MIN, record.epoch_ms);
	TEST_ASSERT_EQUAL_INT32(INT32_MAX, record.temperature_mc);
	TEST_ASSERT_EQUAL_INT32(INT32_MIN, record.humidity_mpct);
	TEST_ASSERT_EQUAL_INT32(INT32_MAX, record.pressure_pa);
}

static void test_legacy_csv_rejects_invalid_rows_without_changing_output(void)
{
	static const struct {
		const char *line;
		int error;
	} invalid[] = {
	    {"", -EBADMSG},
	    {"0,-1,24550,20603", -EBADMSG},
	    {"0,-1,24550,20603,", -EBADMSG},
	    {"0,-1,24550,20603,-", -EBADMSG},
	    {"0,-1,24550,20603,84477,0", -EBADMSG},
	    {"0,-1,24550,20603,84477x", -EBADMSG},
	    {"0,-1,24550,20603,84477\n", -EBADMSG},
	    {" 0,-1,24550,20603,84477", -EBADMSG},
	    {"0,-1, 24550,20603,84477", -EBADMSG},
	    {"0,-1,+24550,20603,84477", -EBADMSG},
	    {"0,-1,24.550,20603,84477", -EBADMSG},
	    {"0,-1,,20603,84477", -EBADMSG},
	    {"9223372036854775808,-1,0,0,0", -ERANGE},
	    {"0,-9223372036854775809,0,0,0", -ERANGE},
	    {"0,-1,2147483648,0,0", -ERANGE},
	    {"0,-1,0,-2147483649,0", -ERANGE},
	    {"0,-1,0,0,18446744073709551616", -ERANGE},
	};
	struct sensor_record record = {.uptime_ms = 1,
				       .epoch_ms = -1,
				       .temperature_mc = 2,
				       .humidity_mpct = 3,
				       .pressure_pa = 4};
	unsigned char saved[sizeof(record)];

	memcpy(saved, &record, sizeof(record));
	for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
		TEST_ASSERT_EQUAL_INT_MESSAGE(invalid[i].error,
					      record_parse_legacy(invalid[i].line, &record),
					      invalid[i].line);
		TEST_ASSERT_EQUAL_MEMORY(saved, &record, sizeof(record));
	}
	TEST_ASSERT_EQUAL_INT(-EINVAL, record_parse_legacy(NULL, &record));
	TEST_ASSERT_EQUAL_MEMORY(saved, &record, sizeof(record));
	TEST_ASSERT_EQUAL_INT(-EINVAL, record_parse_legacy("0,-1,0,0,0", NULL));
}

int main(void)
{
	UNITY_BEGIN();
	RUN_TEST(test_clock_starts_unset_and_reset_clears_it);
	RUN_TEST(test_clock_tracks_monotonic_milliseconds);
	RUN_TEST(test_epoch_and_dates_after_2038);
	RUN_TEST(test_gregorian_leap_year_rules);
	RUN_TEST(test_invalid_dates_preserve_clock);
	RUN_TEST(test_clock_rejects_nulls_and_bad_output_buffers);
	RUN_TEST(test_clock_upper_range_and_overflow);
	RUN_TEST(test_start_stop_controls_are_independent);
	RUN_TEST(test_repeated_controls_are_idempotent);
	RUN_TEST(test_state_flags_names_and_invalid_inputs);
	RUN_TEST(test_csv_prints_engineering_units);
	RUN_TEST(test_csv_checks_nulls_and_buffer_bounds);
	RUN_TEST(test_legacy_csv_converts_saved_reading);
	RUN_TEST(test_legacy_csv_accepts_integer_boundaries);
	RUN_TEST(test_legacy_csv_rejects_invalid_rows_without_changing_output);
	return UNITY_END();
}
