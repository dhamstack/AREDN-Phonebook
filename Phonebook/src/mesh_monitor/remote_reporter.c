#include "remote_reporter.h"
#include "http_client.h"
#include "../software_health/software_health.h"

#define MODULE_NAME "REMOTE_REPORTER"
#include "../log_manager/log_manager.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool reporter_running = false;
static char collector_url[256] = {0};
static int report_interval_s = 0;

void* remote_reporter_thread(void *arg) {
    mesh_monitor_config_t *config = (mesh_monitor_config_t *)arg;

    // Copy configuration
    strncpy(collector_url, config->collector_url, sizeof(collector_url) - 1);
    report_interval_s = config->network_status_report_s;

    LOG_INFO("Remote reporter thread started (interval=%ds, url=%s)",
             report_interval_s, collector_url);

    reporter_running = true;
    time_t last_health_report = 0;
    time_t last_network_report = 0;

    while (reporter_running) {
        time_t now = time(NULL);

        // Send health report every 60 seconds
        if (now - last_health_report >= 60) {
            send_health_report();
            last_health_report = now;
        }

        // Send network status at configured interval
        if (report_interval_s > 0 &&
            now - last_network_report >= report_interval_s) {
            send_network_report();
            last_network_report = now;
        }

        // Check every 10 seconds
        sleep(10);
    }

    LOG_INFO("Remote reporter thread stopped");
    return NULL;
}

void send_health_report(void) {
    agent_health_t health;
    populate_agent_health(&health);

    char *json = agent_health_to_json_string(&health);
    if (!json) {
        LOG_ERROR("Failed to generate health JSON");
        return;
    }

    int ret = http_post_json(collector_url, json);
    if (ret != 0) {
        LOG_WARN("Failed to send health report to collector");
    } else {
        LOG_DEBUG("Health report sent to collector");
    }

    free(json);
}

void send_network_report(void) {
    // Read network JSON from file (avoids code duplication)
    FILE *fp = fopen("/tmp/meshmon_network.json", "r");
    if (!fp) {
        LOG_DEBUG("No network data to report yet");
        return;
    }

    // Get file size
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {  // Max 1MB
        LOG_WARN("Invalid network JSON file size: %ld", size);
        fclose(fp);
        return;
    }

    // Read file
    char *json = malloc(size + 1);
    if (!json) {
        LOG_ERROR("Failed to allocate memory for network JSON");
        fclose(fp);
        return;
    }

    size_t read_bytes = fread(json, 1, size, fp);
    json[read_bytes] = '\0';
    fclose(fp);

    if (read_bytes == 0) {
        LOG_WARN("Empty network JSON file");
        free(json);
        return;
    }

    // Send to collector
    int ret = http_post_json(collector_url, json);
    if (ret != 0) {
        LOG_WARN("Failed to send network report to collector");
    } else {
        LOG_DEBUG("Network report sent to collector");
    }

    free(json);
}

void remote_reporter_shutdown(void) {
    LOG_INFO("Shutting down remote reporter");
    reporter_running = false;
}

bool is_remote_reporter_enabled(void) {
    return reporter_running;
}
