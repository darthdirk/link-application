#include "state.h"

enum app_state state_next(enum app_state current, enum state_event event)
{
	/* Columns: start log, stop log, start stream, stop stream. */
	static const enum app_state transitions[4][4] = {
	    [STATE_IDLE] = {STATE_LOGGING, STATE_IDLE, STATE_STREAMING, STATE_IDLE},
	    [STATE_LOGGING] = {STATE_LOGGING, STATE_IDLE, STATE_BOTH, STATE_LOGGING},
	    [STATE_STREAMING] = {STATE_BOTH, STATE_STREAMING, STATE_STREAMING, STATE_IDLE},
	    [STATE_BOTH] = {STATE_BOTH, STATE_STREAMING, STATE_BOTH, STATE_LOGGING},
	};

	if ((unsigned int)current >= 4U) {
		return STATE_IDLE;
	}
	if ((unsigned int)event >= 4U) {
		return current;
	}
	return transitions[current][event];
}

bool state_logs(enum app_state state)
{
	return state == STATE_LOGGING || state == STATE_BOTH;
}

bool state_streams(enum app_state state)
{
	return state == STATE_STREAMING || state == STATE_BOTH;
}

const char *state_name(enum app_state state)
{
	static const char *const names[] = {"idle", "logging", "streaming", "logging+streaming"};

	return (unsigned int)state < 4U ? names[state] : "invalid";
}
