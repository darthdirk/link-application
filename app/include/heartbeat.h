#ifndef LOGGER_HEARTBEAT_H
#define LOGGER_HEARTBEAT_H

/* Call once from the application thread. First 100 ms pulse follows after 10 s. */
int heartbeat_init(void);

#endif
