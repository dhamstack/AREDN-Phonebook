#ifndef MONITOR_CONFIG_H
#define MONITOR_CONFIG_H

#include "mesh_monitor.h"

// Load mesh monitor configuration from /etc/sipserver.conf
int load_mesh_monitor_config(mesh_monitor_config_t *config);

// Get default configuration
void get_default_monitor_config(mesh_monitor_config_t *config);

#endif // MONITOR_CONFIG_H
