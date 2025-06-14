// config_loader.c
#include "config_loader.h" // This includes common.h
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h> // For isspace

#define MODULE_NAME "CONFIG" // Define MODULE_NAME specifically for this C file

// Define global variables (DECLARED extern in config_loader.h and common.h)
// These are initialized with default values, which will be overwritten by the config file if present.
int g_pb_interval_seconds = 3600; // Default: 1 hour
int g_status_update_interval_seconds = 600; // Default: 10 minutes
ConfigurableServer g_phonebook_servers_list[MAX_PB_SERVERS];
int g_num_phonebook_servers = 0; // Will be populated by the loader

// Helper function to trim leading/trailing whitespace (static to this file)
static char* trim_whitespace(char *str) {
    char *end;

    // Trim leading space
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0)  // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator character
    end[1] = '\0';

    return str;
}

int load_configuration(const char *config_filepath) {
    FILE *fp = fopen(config_filepath, "r");
    if (!fp) {
        LOG_WARN("Configuration file '%s' not found or cannot be opened: %s. Using default values.",
                 config_filepath, strerror(errno));
        // It's not a critical error if the file doesn't exist; we just use defaults.
        return 0;
    }

    char line[512];
    int current_server_idx = 0;
    LOG_INFO("Loading configuration from %s...", config_filepath);

    // Initialize the phonebook servers list to empty to ensure clean state
    memset(g_phonebook_servers_list, 0, sizeof(ConfigurableServer) * MAX_PB_SERVERS);

    while (fgets(line, sizeof(line), fp)) {
        char *trimmed_line = trim_whitespace(line);

        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#') {
            continue; // Skip empty lines and comments
        }

        char *key = trimmed_line;
        char *value = strchr(trimmed_line, '=');
        if (!value) {
            LOG_WARN("Malformed line in config file (missing '='): '%s'. Skipping.", trimmed_line);
            continue;
        }
        *value = '\0'; // Null-terminate key
        value++;       // Move past '=' to start of value
        value = trim_whitespace(value); // Trim whitespace from the value as well

        if (strcmp(key, "PB_INTERVAL_SECONDS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value > 0) {
                g_pb_interval_seconds = parsed_value;
                LOG_DEBUG("Config: PB_INTERVAL_SECONDS = %d", g_pb_interval_seconds);
            } else {
                LOG_WARN("Invalid PB_INTERVAL_SECONDS value '%s'. Using default %d.", value, g_pb_interval_seconds);
            }
        } else if (strcmp(key, "STATUS_UPDATE_INTERVAL_SECONDS") == 0) {
            int parsed_value = atoi(value);
            if (parsed_value > 0) {
                g_status_update_interval_seconds = parsed_value;
                LOG_DEBUG("Config: STATUS_UPDATE_INTERVAL_SECONDS = %d", g_status_update_interval_seconds);
            } else {
                LOG_WARN("Invalid STATUS_UPDATE_INTERVAL_SECONDS value '%s'. Using default %d.", value, g_status_update_interval_seconds);
            }
        } else if (strcmp(key, "PHONEBOOK_SERVER") == 0) {
            if (current_server_idx < MAX_PB_SERVERS) {
                // strtok modifies the string, so it's good if value is a copy or you don't need it later.
                // Here, value is a pointer into 'line', which is fine as we're done with 'line' for this iteration.
                char *host_str = strtok(value, ",");
                char *port_str = strtok(NULL, ",");
                char *path_str = strtok(NULL, ",");

                if (host_str && port_str && path_str) {
                    strncpy(g_phonebook_servers_list[current_server_idx].host, host_str, MAX_SERVER_HOST_LEN - 1);
                    g_phonebook_servers_list[current_server_idx].host[MAX_SERVER_HOST_LEN - 1] = '\0';
                    strncpy(g_phonebook_servers_list[current_server_idx].port, port_str, MAX_SERVER_PORT_LEN - 1);
                    g_phonebook_servers_list[current_server_idx].port[MAX_SERVER_PORT_LEN - 1] = '\0';
                    strncpy(g_phonebook_servers_list[current_server_idx].path, path_str, MAX_SERVER_PATH_LEN - 1);
                    g_phonebook_servers_list[current_server_idx].path[MAX_SERVER_PATH_LEN - 1] = '\0';
                    LOG_DEBUG("Config: Added phonebook server %d: %s:%s%s", current_server_idx + 1,
                              g_phonebook_servers_list[current_server_idx].host,
                              g_phonebook_servers_list[current_server_idx].port,
                              g_phonebook_servers_list[current_server_idx].path);
                    current_server_idx++;
                } else {
                    LOG_WARN("Malformed PHONEBOOK_SERVER line: '%s'. Expected 'host,port,path'. Skipping.", value);
                }
            } else {
                LOG_WARN("Max phonebook servers (%d) reached. Ignoring additional PHONEBOOK_SERVER entries.", MAX_PB_SERVERS);
            }
        } else {
            LOG_WARN("Unknown configuration key: '%s'. Skipping.", key);
        }
    }
    fclose(fp);
    g_num_phonebook_servers = current_server_idx; // Set the actual count of loaded servers
    LOG_INFO("Configuration loaded. Total phonebook servers: %d.", g_num_phonebook_servers);
    return 0; // Success
}
