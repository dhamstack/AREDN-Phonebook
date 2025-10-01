#ifndef REMOTE_REPORTER_H
#define REMOTE_REPORTER_H

#include "mesh_monitor.h"
#include <stdbool.h>

/**
 * Remote reporter thread entry point
 * Periodically sends agent health and network status to collector
 * @param arg Pointer to mesh_monitor_config_t
 * @return NULL on exit
 */
void* remote_reporter_thread(void *arg);

/**
 * Send health report to collector immediately
 */
void send_health_report(void);

/**
 * Send network status report to collector immediately
 */
void send_network_report(void);

/**
 * Shutdown remote reporter thread
 */
void remote_reporter_shutdown(void);

/**
 * Check if remote reporter is enabled and running
 * @return true if reporter is active
 */
bool is_remote_reporter_enabled(void);

#endif // REMOTE_REPORTER_H
