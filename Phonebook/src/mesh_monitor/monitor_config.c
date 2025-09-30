#include "monitor_config.h"
#include "../log_manager/log_manager.h"
#include <string.h>
#include <stdlib.h>

#define MODULE_NAME "MONITOR_CONFIG"

void get_default_monitor_config(mesh_monitor_config_t *config) {
    if (!config) return;

    memset(config, 0, sizeof(mesh_monitor_config_t));

    // Default values from FSD Section 4.1
    config->enabled = false;  // Disabled by default
    config->mode = MONITOR_MODE_LIGHTWEIGHT;

    // Network status measurement
    config->network_status_interval_s = 40;
    config->probe_window_s = 5;
    config->neighbor_targets = 2;
    config->rotating_peer = 1;
    config->max_probe_kbps = 80;
    config->probe_port = 40050;
    config->dscp_ef = true;

    // Routing daemon integration
    config->routing_daemon = ROUTING_AUTO;
    config->routing_cache_s = 5;

    // Remote reporting (optional)
    config->network_status_report_s = 40;
    memset(config->collector_url, 0, sizeof(config->collector_url));
}

int load_mesh_monitor_config(mesh_monitor_config_t *config) {
    if (!config) return -1;

    // Start with defaults
    get_default_monitor_config(config);

    FILE *fp = fopen("/etc/sipserver.conf", "r");
    if (!fp) {
        LOG_WARN("Configuration file not found, using defaults");
        return 0;  // Not an error, just use defaults
    }

    char line[512];
    bool in_mesh_monitor_section = false;

    while (fgets(line, sizeof(line), fp)) {
        // Remove trailing newline
        line[strcspn(line, "\r\n")] = 0;

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') continue;

        // Check for section headers
        if (strncmp(line, "[mesh_monitor]", 14) == 0) {
            in_mesh_monitor_section = true;
            continue;
        } else if (line[0] == '[') {
            in_mesh_monitor_section = false;
            continue;
        }

        // Only parse lines in [mesh_monitor] section
        if (!in_mesh_monitor_section) continue;

        // Parse key=value pairs
        char *equals = strchr(line, '=');
        if (!equals) continue;

        *equals = '\0';
        char *key = line;
        char *value = equals + 1;

        // Trim whitespace
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;

        // Parse configuration values
        if (strcmp(key, "enabled") == 0) {
            config->enabled = (atoi(value) != 0);
        } else if (strcmp(key, "mode") == 0) {
            if (strcmp(value, "disabled") == 0) {
                config->mode = MONITOR_MODE_DISABLED;
            } else if (strcmp(value, "lightweight") == 0) {
                config->mode = MONITOR_MODE_LIGHTWEIGHT;
            } else if (strcmp(value, "full") == 0) {
                config->mode = MONITOR_MODE_FULL;
            }
        } else if (strcmp(key, "network_status_interval_s") == 0) {
            config->network_status_interval_s = atoi(value);
        } else if (strcmp(key, "probe_window_s") == 0) {
            config->probe_window_s = atoi(value);
        } else if (strcmp(key, "neighbor_targets") == 0) {
            config->neighbor_targets = atoi(value);
        } else if (strcmp(key, "rotating_peer") == 0) {
            config->rotating_peer = atoi(value);
        } else if (strcmp(key, "max_probe_kbps") == 0) {
            config->max_probe_kbps = atoi(value);
        } else if (strcmp(key, "probe_port") == 0) {
            config->probe_port = atoi(value);
        } else if (strcmp(key, "dscp_ef") == 0) {
            config->dscp_ef = (atoi(value) != 0);
        } else if (strcmp(key, "routing_daemon") == 0) {
            if (strcmp(value, "auto") == 0) {
                config->routing_daemon = ROUTING_AUTO;
            } else if (strcmp(value, "olsr") == 0) {
                config->routing_daemon = ROUTING_OLSR;
            } else if (strcmp(value, "babel") == 0) {
                config->routing_daemon = ROUTING_BABEL;
            }
        } else if (strcmp(key, "routing_cache_s") == 0) {
            config->routing_cache_s = atoi(value);
        } else if (strcmp(key, "network_status_report_s") == 0) {
            config->network_status_report_s = atoi(value);
        } else if (strcmp(key, "collector_url") == 0) {
            strncpy(config->collector_url, value, sizeof(config->collector_url) - 1);
        }
    }

    fclose(fp);

    LOG_INFO("Mesh monitor configuration loaded (enabled=%d, mode=%d)",
             config->enabled, config->mode);

    return 0;
}
