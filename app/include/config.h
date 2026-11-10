#ifndef LOGGER_CONFIG_H
#define LOGGER_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

struct logger_config {
	uint32_t log_rate_ms;
	uint32_t stream_rate_ms;
	uint32_t flush_rate_ms;
};

void config_defaults(struct logger_config *config);
bool config_rate_valid(uint32_t rate_ms);
/* Application thread only. A failed load leaves the supplied config intact. */
int config_load(struct logger_config *config);
int config_save(const struct logger_config *config);

#endif
